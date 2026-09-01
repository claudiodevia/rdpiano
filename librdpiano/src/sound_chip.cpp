#include "../include/sound_chip.h"

#include "../include/rd_trace.h"
#include "../include/sa_blocks.h"
#include "../include/rom_loader.h"

#include <cmath>

// LUT for the address speed
// LUT for bits 5/6/7/8 of the subphase

// Las permutaciones de pines de las ROM viven en rom_loader.h.

SoundChip::SoundChip(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7) : tables(sa_tables())
{
    load_samples(temp_ic5, temp_ic6, temp_ic7);
}

u8 SoundChip::read(size_t offset) { return m_irq_id; }

void SoundChip::write(size_t offset, u8 data)
{
    uint8_t voiceI = offset / 0x100;
    uint8_t partI = offset % 0x100 / 0x10;
    uint8_t field = offset % 8;

    if (voiceI >= NUM_VOICES || partI >= PARTS_PER_VOICE_MEM || field >= 8)
    {
        RD_TRACE("ERROR: received invalid SA write %02x %02x %02x %02x\n", voiceI, partI, field, data);
        return;
    }

    SA_Part &part = m_parts[voiceI][partI];

    // flags seems to be common for all parts?
    if (field == 0x6)
    {
        m_parts[voiceI][0].flags_0 = data & 1;
        m_parts[voiceI][0].flags_1 = (data >> 1) & 1;
    }
    else if (field == 0x0)
    {
        part.pitch_lut_i &= 0x00FF;
        part.pitch_lut_i |= data << 8;
    }
    else if (field == 0x1)
    {
        part.pitch_lut_i &= 0xFF00;
        part.pitch_lut_i |= data;
    }
    else if (field == 0x2)
        part.wave_addr_loop = data;
    else if (field == 0x3)
        part.wave_addr_high = data;
    else if (field == 0x4)
        part.env_dest = data;
    else if (field == 0x5)
        part.env_speed = data;
    else if (field == 0x7)
        part.env_offset = data;
}

s32 SoundChip::update()
{
    s32 result = 0;

    for (size_t voiceI = 0; voiceI < NUM_VOICES; voiceI++)
    {
        SA_Part &partFlags = m_parts[voiceI][0];
        for (size_t partI = 0; partI < PARTS_PER_VOICE; partI++)
        {
            SA_Part &part = m_parts[voiceI][partI];

            // hack for performance
            if (part.env_value == 0 && part.env_dest == 0)
            {
                part.sub_phase = 0;
                continue;
            }

            const Ic19Out ic19 = tick_ic19(part, partFlags);
            const Ic9Out ic9 = tick_ic9(part, partFlags, tables);
            const s32 exp_val =
                tick_ic8(part, ic19, ic9, waves->exp[ic9.waverom_addr], waves->exp_sign[ic9.waverom_addr],
                         waves->delta[ic9.waverom_addr], waves->delta_sign[ic9.waverom_addr], tables);

            // hack to prevent voices ringing when env value is 0, investigate
            if (part.env_value != 0)
                result += exp_val;

            if (ic19.irq && !m_irq_triggered)
            {
                m_irq_id = partI | (voiceI << 4);
                m_irq_triggered = true;
            }
        }
    }

    return result;
}

void SoundChip::load_samples(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7)
{
    decode_samples(temp_ic5, temp_ic6, temp_ic7);
    publish_samples();
}

void SoundChip::publish_samples()
{
    if (!waves_pending)
        return;

    WaveTables *t = waves;
    waves = waves_back;
    waves_back = t;
    waves_pending = false;
}

void SoundChip::decode_samples(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7)
{
    // Se descifra byte a byte en el sitio: sin los 384 KB de temporales que
    // costaría descifrar las tres ROM enteras primero.
    WaveTables &out = *waves_back;

    // Wave rom values
    for (size_t i = 0; i < 0x20000; i++)
    {
        size_t descrambled_i =
            (((i >> 0) & 1) << 0 | ((~i >> 1) & 1) << 1 | ((i >> 2) & 1) << 2 | ((~i >> 3) & 1) << 3 |
             ((i >> 4) & 1) << 4 | ((~i >> 5) & 1) << 5 | ((i >> 6) & 1) << 6 | ((i >> 7) & 1) << 7 |
             ((~i >> 8) & 1) << 8 | ((~i >> 9) & 1) << 9 | ((i >> 10) & 1) << 10 | ((i >> 11) & 1) << 11 |
             ((i >> 12) & 1) << 12 | ((i >> 13) & 1) << 13 | ((i >> 14) & 1) << 14 | ((i >> 15) & 1) << 15 |
             ((i >> 16) & 1) << 16);

        const u8 b5 = unscramble_data_wave(temp_ic5[unscramble_addr_wave((u32)descrambled_i)]);
        const u8 b6 = unscramble_data_wave(temp_ic6[unscramble_addr_wave((u32)descrambled_i)]);
        const u8 b7 = unscramble_data_wave(temp_ic7[unscramble_addr_wave((u32)descrambled_i)]);

        uint16_t exp_sample =
            (((b5 >> 0) & 1) << 13 | ((b6 >> 4) & 1) << 12 | ((b7 >> 4) & 1) << 11 | ((~b6 >> 0) & 1) << 10 |
             ((b7 >> 7) & 1) << 9 | ((b5 >> 7) & 1) << 8 | ((~b5 >> 5) & 1) << 7 | ((b6 >> 2) & 1) << 6 |
             ((b7 >> 2) & 1) << 5 | ((b7 >> 1) & 1) << 4 | ((~b5 >> 1) & 1) << 3 | ((b5 >> 3) & 1) << 2 |
             ((b6 >> 5) & 1) << 1 | ((~b6 >> 7) & 1) << 0);
        bool exp_sign = (~b7 >> 3) & 1;
        out.exp[i] = exp_sample;
        out.exp_sign[i] = exp_sign;

        uint16_t delta_sample = (((~b7 >> 6) & 1) << 8 | ((b5 >> 4) & 1) << 7 | ((b7 >> 0) & 1) << 6 |
                                 ((~b6 >> 3) & 1) << 5 | ((b5 >> 2) & 1) << 4 | ((~b5 >> 6) & 1) << 3 |
                                 ((b6 >> 6) & 1) << 2 | ((b7 >> 5) & 1) << 1 | ((~b6 >> 7) & 1) << 0);
        bool delta_sign = (b6 >> 1) & 1;
        out.delta[i] = delta_sample;
        out.delta_sign[i] = delta_sign;
    }

    waves_pending = true;
}
