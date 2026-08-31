#ifndef ROM_LOADER_H
#define ROM_LOADER_H

#include <stddef.h>

#include "mame_utils.h"

// Carga y descifrado de las ROM (REFACTORIZACION §6, §17.4).
//
// Las ROM llegan con las líneas de dirección y de datos permutadas en el PCB.
// Las permutaciones de abajo deshacen ese cableado: **no son cosméticas** y
// ninguna reordenación es inocente (§20). Estaban como macros repartidas entre
// mcu.cpp y sound_chip.cpp; aquí son `constexpr` en un solo sitio, para que
// test_rom_loader.cpp pueda comprobar que siguen siendo biyectivas.
//
// Estas funciones son puras: entran `const u8 *`, sale un buffer. No hay CPU,
// ni chip de sonido, ni estado. Por eso se prueban en milisegundos.

// ROM de programa (firmware, 8 KB). Puede ser de 13 o 14 bits según el modelo.
constexpr u32 unscramble_addr_cpub(u32 i)
{
  return bitswap<14>(i, 13, 12, 11, 8, 9, 10, 7, 6, 5, 4, 3, 2, 1, 0);
}
constexpr u8 unscramble_data_cpub(u8 d)
{
  return bitswap<8>(d, 7, 0, 6, 1, 5, 2, 4, 3);
}

// ROM de parámetros (128 KB). Los datos usan la misma permutación que la ROM
// de programa; las direcciones, no.
constexpr u32 unscramble_addr_params(u32 i)
{
  return bitswap<17>(i, 16, 15, 13, 12, 14, 11, 8, 9, 10, 7, 6, 5, 4, 3, 2, 1, 0);
}
constexpr u8 unscramble_data_params(u8 d) { return unscramble_data_cpub(d); }

// ROM de onda (IC5/IC6/IC7, 128 KB cada una). Los datos van sin permutar.
constexpr u32 unscramble_addr_wave(u32 i)
{
  return (BIT(i, 16) << 16) | (BIT(i, 15) << 15) | (BIT(i, 14) << 14) |
         (BIT(i, 1) << 13) | (BIT(i, 4) << 12) | (BIT(i, 9) << 11) |
         (BIT(i, 5) << 10) | (BIT(i, 10) << 9) | (BIT(i, 3) << 8) |
         (BIT(i, 0) << 7) | (BIT(i, 6) << 6) | (BIT(i, 11) << 5) |
         (BIT(i, 7) << 4) | (BIT(i, 2) << 3) | (BIT(i, 12) << 2) |
         (BIT(i, 8) << 1) | (BIT(i, 13) << 0);
}
constexpr u8 unscramble_data_wave(u8 d)
{
  return bitswap<8>(d, 7, 6, 5, 4, 3, 2, 1, 0);
}

inline constexpr size_t PROG_ROM_BYTES = 0x2000;    // ROM de programa ya descifrada
inline constexpr size_t PARAMS_ROM_BYTES = 0x20000; // espacio de params del bus
inline constexpr size_t PARAMS_PAGE_BYTES = 0x8000; // ventana que ve la CPU
inline constexpr size_t WAVE_ROM_BYTES = 0x20000;

// Descifra la ROM de programa entera. `dst` mide PROG_ROM_BYTES.
void decode_program_rom(u8 *dst, const u8 *src);

// Descifra una ROM de onda entera. `dst` mide WAVE_ROM_BYTES.
void decode_wave_rom(u8 *dst, const u8 *src);

// Deja `dst` (PARAMS_ROM_BYTES) como lo espera el bus para el parche que
// empieza en `from_addr`: todo a 0xff, la página de 32 KB alineada que
// contiene el parche mapeada en 0x8000, y los bytes 0x00-0x02 parcheados para
// redirigir al firmware al parche elegido.
//
// Solo se descifra la página que se va a mapear: `unscramble_addr_params` se
// aplica índice a índice, así que el resultado es byte a byte el mismo que
// descifrar los 0x20000 en un temporal y copiar la ventana.
void decode_params_page(u8 *dst, const u8 *src, size_t from_addr);

// El destino al que apuntan los bytes 0x00-0x02 para `from_addr`. Se expone
// para poder comprobarlo sin hurgar en el buffer.
constexpr size_t params_patch_target(size_t from_addr)
{
  return (from_addr - (from_addr >> 15 << 15)) + 0x4000;
}

#endif
