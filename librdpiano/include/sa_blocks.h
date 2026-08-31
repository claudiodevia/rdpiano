#ifndef SA_BLOCKS_H
#define SA_BLOCKS_H

#include "mame_utils.h"
#include "sa_tables.h"

// Los tres bloques de SoundChip::update() (REFACTORIZACION §5, §17.5).
//
// El bucle original eran 135 líneas con tres bloques delimitados por
// comentarios `// IC19`, `// IC9`, `// IC8` y llaves anónimas, y el estado
// entre ellos viajaba en cuatro variables sueltas declaradas arriba. Esas
// cuatro variables documentaban sin querer **cuál es el bus real entre los
// chips**: aquí están en la firma, que es donde se ven.
//
// La aritmética no se ha tocado ni una operación. Cada `& 0x3fff`, cada `+ 1`,
// cada `|= 0x3c00` replica un sumador de verdad y no se "simplifica" (§20).
// Lo que fija que la extracción fue neutra son dos cosas a la vez: los ~2.200
// vectores de test/vectors/ic_blocks.txt, capturados del código anterior, y
// los 16 hashes del golden.
//
// Van `inline` en la cabecera a propósito: eran bloques dentro de un bucle
// caliente y tienen que poder seguir integrándose igual.

// El estado de una parte. Diez por voz, dieciséis voces.
struct SA_Part
{
  uint32_t sub_phase = 0;
  uint32_t env_value = 0;

  uint16_t pitch_lut_i;
  uint8_t wave_addr_loop;
  uint8_t wave_addr_high;
  uint8_t env_dest;
  uint8_t env_speed;
  bool flags_0;
  bool flags_1;
  uint8_t env_offset;
};

// LUT de la velocidad de la envolvente (IC19) y de la dirección (IC8).
inline constexpr uint32_t env_table[] = {
    0x000000, 0x000023, 0x000026, 0x000029, 0x00002d, 0x000031, 0x000036,
    0x00003b, 0x000040, 0x000046, 0x00004c, 0x000052, 0x00005a, 0x000062,
    0x00006c, 0x000076, 0x000080, 0x00008c, 0x000098, 0x0000a4, 0x0000b4,
    0x0000c4, 0x0000d8, 0x0000ec, 0x000104, 0x00011c, 0x000134, 0x00014c,
    0x00016c, 0x00018c, 0x0001b4, 0x0001dc, 0x000200, 0x000230, 0x000260,
    0x000290, 0x0002d0, 0x000310, 0x000360, 0x0003b0, 0x000400, 0x000460,
    0x0004c0, 0x000520, 0x0005a0, 0x000620, 0x0006c0, 0x000760, 0x000800,
    0x0008c0, 0x000980, 0x000a40, 0x000b40, 0x000c40, 0x000d80, 0x000ec0,
    0x001000, 0x001180, 0x001300, 0x001480, 0x001680, 0x001880, 0x001b00,
    0x001d80, 0x002000, 0x002300, 0x002600, 0x002900, 0x002d00, 0x003100,
    0x003600, 0x003b00, 0x004000, 0x004600, 0x004c00, 0x005200, 0x005a00,
    0x006200, 0x006c00, 0x007600, 0x008000, 0x008c00, 0x009800, 0x00a400,
    0x00b400, 0x00c400, 0x00d800, 0x00ec00, 0x010000, 0x011800, 0x013000,
    0x014800, 0x016800, 0x018800, 0x01b000, 0x01d800, 0x020000, 0x023000,
    0x026000, 0x029000, 0x02d000, 0x031000, 0x036000, 0x03b000, 0x040000,
    0x046000, 0x04c000, 0x052000, 0x05a000, 0x062000, 0x06c000, 0x076000,
    0x080000, 0x08c000, 0x098000, 0x0a4000, 0x0b4000, 0x0c4000, 0x0d8000,
    0x0ec000, 0x100000, 0x118000, 0x130000, 0x148000, 0x168000, 0x188000,
    0x1b0000, 0x1d8000, 0x000000, 0x1fffdc, 0x1fffd9, 0x1fffd6, 0x1fffd2,
    0x1fffce, 0x1fffc9, 0x1fffc4, 0x1fffbf, 0x1fffb9, 0x1fffb3, 0x1fffad,
    0x1fffa5, 0x1fff9d, 0x1fff93, 0x1fff89, 0x1fff7f, 0x1fff73, 0x1fff67,
    0x1fff5b, 0x1fff4b, 0x1fff3b, 0x1fff27, 0x1fff13, 0x1ffefb, 0x1ffee3,
    0x1ffecb, 0x1ffeb3, 0x1ffe93, 0x1ffe73, 0x1ffe4b, 0x1ffe23, 0x1ffdff,
    0x1ffdcf, 0x1ffd9f, 0x1ffd6f, 0x1ffd2f, 0x1ffcef, 0x1ffc9f, 0x1ffc4f,
    0x1ffbff, 0x1ffb9f, 0x1ffb3f, 0x1ffadf, 0x1ffa5f, 0x1ff9df, 0x1ff93f,
    0x1ff89f, 0x1ff7ff, 0x1ff73f, 0x1ff67f, 0x1ff5bf, 0x1ff4bf, 0x1ff3bf,
    0x1ff27f, 0x1ff13f, 0x1fefff, 0x1fee7f, 0x1fecff, 0x1feb7f, 0x1fe97f,
    0x1fe77f, 0x1fe4ff, 0x1fe27f, 0x1fdfff, 0x1fdcff, 0x1fd9ff, 0x1fd6ff,
    0x1fd2ff, 0x1fceff, 0x1fc9ff, 0x1fc4ff, 0x1fbfff, 0x1fb9ff, 0x1fb3ff,
    0x1fadff, 0x1fa5ff, 0x1f9dff, 0x1f93ff, 0x1f89ff, 0x1f7fff, 0x1f73ff,
    0x1f67ff, 0x1f5bff, 0x1f4bff, 0x1f3bff, 0x1f27ff, 0x1f13ff, 0x1effff,
    0x1ee7ff, 0x1ecfff, 0x1eb7ff, 0x1e97ff, 0x1e77ff, 0x1e4fff, 0x1e27ff,
    0x1dffff, 0x1dcfff, 0x1d9fff, 0x1d6fff, 0x1d2fff, 0x1cefff, 0x1c9fff,
    0x1c4fff, 0x1bffff, 0x1b9fff, 0x1b3fff, 0x1adfff, 0x1a5fff, 0x19dfff,
    0x193fff, 0x189fff, 0x17ffff, 0x173fff, 0x167fff, 0x15bfff, 0x14bfff,
    0x13bfff, 0x127fff, 0x113fff, 0x0fffff, 0x0e7fff, 0x0cffff, 0x0b7fff,
    0x097fff, 0x077fff, 0x04ffff, 0x027fff};

inline constexpr uint16_t addr_table[] = {0x1e0, 0x080, 0x060, 0x04d, 0x040, 0x036, 0x02d, 0x026,
                                          0x020, 0x01b, 0x016, 0x011, 0x00d, 0x00a, 0x006, 0x003};

// IC19 -> IC8: el volumen logarítmico, y si la envolvente acabó su segmento.
struct Ic19Out
{
  uint32_t volume;
  bool irq;
};

// IC9 -> IC8: la dirección en la wave ROM y los dos selectores.
struct Ic9Out
{
  uint32_t waverom_addr;
  bool sel_sample_type;
  bool phase_hi;
};

// IC19: envolvente. Avanza `part.env_value` y dispara IRQ al terminar un
// segmento. `flags` es la parte 0 de la voz: los flags son comunes a la voz.
inline Ic19Out tick_ic19(SA_Part &part, const SA_Part &flags)
{
  Ic19Out out;

  bool env_speed_some_high = (part.env_speed & 0b01111111) != 0;
  bool env_speed_inv = (part.env_speed & 0b10000000) != 0;

  uint32_t adder1_a = part.env_value;
  if (!flags.flags_0)
    adder1_a = 1 << 25;
  uint32_t adder1_b = env_table[part.env_speed];
  bool adder1_ci = env_speed_some_high && env_speed_inv;
  if (adder1_ci)
    adder1_b |= 0x7f << 21;

  uint32_t adder3_o = 1 + (adder1_a >> 20) + part.env_offset;
  uint32_t adder3_of = adder3_o > 0xff;
  adder3_o &= 0xff;

  out.volume = ~(
                   (flags.flags_0 ? ((adder1_a >> 14) & 0b111111) : 0) |
                   ((adder3_o & 0b1111) << 6) |
                   (adder3_of ? ((adder3_o & 0b11110000) << 6) : 0)) &
               0x3fff;

  uint32_t adder1_o = adder1_a + adder1_b + (adder1_ci ? 1 : 0);
  uint32_t adder1_of = adder1_o > 0xfffffff;
  adder1_o &= 0xfffffff;

  uint32_t adder2_o = (adder1_o >> 20) + (~part.env_dest & 0xff) + 1;
  uint32_t adder2_of = adder2_o > 0xff;

  bool end_reached = env_speed_some_high && ((adder1_of != env_speed_inv) || (env_speed_inv != adder2_of));
  out.irq = end_reached;

  part.env_value = end_reached ? (part.env_dest << 20) : adder1_o;

  return out;
}

// IC9: acumulador de fase. Avanza `part.sub_phase` y produce la dirección de
// la wave ROM.
inline Ic9Out tick_ic9(SA_Part &part, const SA_Part &flags, const SaTables &tables)
{
  Ic9Out out;

  uint32_t adder1 = (tables.phase_exp[part.pitch_lut_i] + part.sub_phase) & 0xffffff;
  uint32_t adder2 = 1 + (adder1 >> 16) + ((~part.wave_addr_loop) & 0xff);
  bool adder2_co = adder2 > 0xff;
  adder2 &= 0xff;
  uint32_t adder1_and = !flags.flags_1 ? 0 : (adder1 & 0xffff);
  adder1_and |= (!flags.flags_1 ? 0 : (adder2_co ? adder2 : (adder1 >> 16))) << 16;

  part.sub_phase = adder1_and;
  out.waverom_addr = (part.wave_addr_high << 11) | ((part.sub_phase >> 9) & 0x7ff);

  out.sel_sample_type = BIT(out.waverom_addr, 16) || BIT(out.waverom_addr, 15) || BIT(out.waverom_addr, 14) ||
                        !((BIT(out.waverom_addr, 13) && !BIT(out.waverom_addr, 11) && !BIT(out.waverom_addr, 12)) || !BIT(out.waverom_addr, 13));
  out.phase_hi = ((BIT(part.pitch_lut_i, 15) && BIT(part.pitch_lut_i, 14)) ||
                  (BIT(part.sub_phase, 23) || BIT(part.sub_phase, 22) || BIT(part.sub_phase, 21) || BIT(part.sub_phase, 20)) ||
                  !flags.flags_1);

  return out;
}

// IC8: suma logarítmica de volumen y muestra. Los cuatro valores de la wave
// ROM entran por parámetro porque son del juego de ROMs cargado, no del bloque:
// así el bloque se puede probar sin ROMs.
//
// Devuelve la muestra tal cual la calcula el chip. Que la voz suene o no —el
// hack `investigate` de env_value == 0— lo decide el bucle, que es donde se ve.
inline s32 tick_ic8(const SA_Part &part, const Ic19Out &ic19, const Ic9Out &ic9,
                    uint16_t waverom_exp, bool sign_pa, uint16_t waverom_delta,
                    bool sign_pb, const SaTables &tables)
{
  uint32_t volume = ic19.volume;
  uint32_t waverom_pa = waverom_exp;
  uint32_t waverom_pb = waverom_delta;
  waverom_pa |= ic9.sel_sample_type ? 1 : 0;
  waverom_pb |= ic9.sel_sample_type ? 0 : 1;

  if (ic9.phase_hi)
    volume |= 0b1111 << 10;

  uint32_t tmp_1, tmp_2;

  uint32_t adder1_o = volume + waverom_pa;
  bool adder1_co = adder1_o > 0x3fff;
  adder1_o &= 0x3fff;
  if (adder1_co)
    adder1_o |= 0x3c00;
  tmp_1 = adder1_o;

  uint32_t adder3_o = addr_table[(part.sub_phase >> 5) & 0xf] + (waverom_pb & 0x1ff);
  bool adder3_of = adder3_o > 0x1ff;
  adder3_o &= 0x1ff;
  if (adder3_of)
    adder3_o |= 0x1e0;

  adder1_o = volume + (adder3_o << 5);
  adder1_co = adder1_o > 0x3fff;
  adder1_o &= 0x3fff;
  if (adder1_co)
    adder1_o |= 0x3c00;
  tmp_2 = adder1_o;

  int32_t exp_val1 = tables.samples_exp[(16384 * sign_pa) + (1024 * (tmp_1 >> 10)) + (tmp_1 & 1023)];
  int32_t exp_val2 = tables.samples_exp[(16384 * sign_pb) + (1024 * (tmp_2 >> 10)) + (tmp_2 & 1023)];
  if (sign_pa)
    exp_val1 = exp_val1 - 0x8000;
  if (sign_pb)
    exp_val2 = exp_val2 - 0x8000;

  return exp_val1 + exp_val2;
}

#endif
