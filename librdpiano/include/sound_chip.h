#ifndef SOUND_CHIP_H
#define SOUND_CHIP_H

#include "mame_utils.h"
#include "sa_blocks.h"
#include "sa_tables.h"

class SoundChip
{
  public:
    SoundChip(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

    // Ver Mcu: no copiable ni movible, por el mismo motivo.
    SoundChip(const SoundChip &) = delete;
    SoundChip &operator=(const SoundChip &) = delete;
    SoundChip(SoundChip &&) = delete;
    SoundChip &operator=(SoundChip &&) = delete;

    u8 read(size_t offset);
    void write(size_t offset, u8 data);

    s32 update();

    // Estado de síntesis a cero: las 160 SA_Part y la IRQ pendiente. No toca las
    // tablas de onda —son la ROM descifrada, no estado— ni el juego de reserva
    // que espera publicación.
    void reset();

    // Carga de las ROM de onda, partida en dos por coste. `decode_samples()` es
    // la parte cara (~2,9 ms) y escribe en el juego de reserva: no toca nada de
    // lo que lee `update()`, así que el integrador puede correrla fuera del
    // cerrojo que serializa con el hilo de audio. `publish_samples()` activa lo
    // descifrado y es un intercambio de punteros.
    void decode_samples(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);
    void publish_samples();

    // decode_samples() + publish_samples(): hace el trabajo caro siempre.
    void load_samples(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7);

    // if there is an IRQ currently waiting
    bool m_irq_triggered = false;

  private:
    static constexpr unsigned NUM_VOICES = 16;
    static constexpr unsigned PARTS_PER_VOICE = 10;
    static constexpr unsigned PARTS_PER_VOICE_MEM = 16;

    // Las tablas de onda ya descifradas. Hay dos juegos: `waves` es el que lee
    // update() y `waves_back` donde descifra decode_samples().
    struct WaveTables
    {
        uint16_t exp[0x20000];
        bool exp_sign[0x20000];
        uint16_t delta[0x20000];
        bool delta_sign[0x20000];
    };

    WaveTables wave_banks[2];
    WaveTables *waves = &wave_banks[0];
    WaveTables *waves_back = &wave_banks[1];
    bool waves_pending = false;

    // LUT deterministas, compartidas por todas las instancias.
    const SaTables &tables;

    SA_Part m_parts[NUM_VOICES][PARTS_PER_VOICE_MEM]; // channel memory
    uint8_t m_irq_id = 0;                             // voice/part that triggered the IRQ
};

#endif
