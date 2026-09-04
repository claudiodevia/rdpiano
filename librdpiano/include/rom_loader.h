#ifndef ROM_LOADER_H
#define ROM_LOADER_H

#include <stddef.h>

#include "mame_utils.h"

/**
 * @file rom_loader.h
 * @brief Carga y descifrado de las ROM: funciones puras, sin CPU ni estado.
 *
 * Las ROM llegan con las líneas de dirección y de datos permutadas en el PCB;
 * las permutaciones de aquí deshacen ese cableado. **No son cosméticas**:
 * ninguna reordenación es inocente.
 */

/** @brief Dirección de la ROM de programa (8 KB); 13 o 14 bits según el modelo. */
constexpr u32 unscramble_addr_cpub(u32 i) { return bitswap<14>(i, 13, 12, 11, 8, 9, 10, 7, 6, 5, 4, 3, 2, 1, 0); }
/** @brief Dato de la ROM de programa. */
constexpr u8 unscramble_data_cpub(u8 d) { return bitswap<8>(d, 7, 0, 6, 1, 5, 2, 4, 3); }

/** @brief Dirección de la ROM de parámetros (128 KB); no es la de la ROM de programa. */
constexpr u32 unscramble_addr_params(u32 i)
{
    return bitswap<17>(i, 16, 15, 13, 12, 14, 11, 8, 9, 10, 7, 6, 5, 4, 3, 2, 1, 0);
}
/** @brief Dato de la ROM de parámetros: la misma permutación que la ROM de programa. */
constexpr u8 unscramble_data_params(u8 d) { return unscramble_data_cpub(d); }

/** @brief Dirección de una ROM de onda (IC5/IC6/IC7, 128 KB cada una). */
constexpr u32 unscramble_addr_wave(u32 i)
{
    return (BIT(i, 16) << 16) | (BIT(i, 15) << 15) | (BIT(i, 14) << 14) | (BIT(i, 1) << 13) | (BIT(i, 4) << 12) |
           (BIT(i, 9) << 11) | (BIT(i, 5) << 10) | (BIT(i, 10) << 9) | (BIT(i, 3) << 8) | (BIT(i, 0) << 7) |
           (BIT(i, 6) << 6) | (BIT(i, 11) << 5) | (BIT(i, 7) << 4) | (BIT(i, 2) << 3) | (BIT(i, 12) << 2) |
           (BIT(i, 8) << 1) | (BIT(i, 13) << 0);
}
/** @brief Dato de una ROM de onda: van sin permutar. */
constexpr u8 unscramble_data_wave(u8 d) { return bitswap<8>(d, 7, 6, 5, 4, 3, 2, 1, 0); }

inline constexpr size_t PROG_ROM_BYTES = 0x2000;    ///< ROM de programa ya descifrada.
inline constexpr size_t PARAMS_ROM_BYTES = 0x20000; ///< Espacio de params del bus.
inline constexpr size_t PARAMS_PAGE_BYTES = 0x8000; ///< Ventana de params que ve la CPU.
inline constexpr size_t WAVE_ROM_BYTES = 0x20000;   ///< Una ROM de onda ya descifrada.

/**
 * @brief Descifra la ROM de programa entera.
 * @param dst Destino de PROG_ROM_BYTES.
 * @param src ROM tal como viene del dump.
 */
void decode_program_rom(u8 *dst, const u8 *src);

/**
 * @brief Descifra una ROM de onda entera.
 * @param dst Destino de WAVE_ROM_BYTES.
 * @param src ROM tal como viene del dump.
 */
void decode_wave_rom(u8 *dst, const u8 *src);

/**
 * @brief Descifra la página de 32 KB alineada que contiene `from_addr`.
 *
 * unscramble_addr_params() se aplica índice a índice, así que el resultado es
 * byte a byte el mismo que descifrar los 0x20000 en un temporal y copiar la
 * ventana.
 *
 * @param dst Destino de PARAMS_PAGE_BYTES.
 * @param src ROM de parámetros sin descifrar.
 * @param from_addr Offset del parche dentro de la ROM.
 */
void decode_params_window(u8 *dst, const u8 *src, size_t from_addr);

/**
 * @brief Deja el espacio de params como lo espera el bus para un parche.
 *
 * Todo a 0xff, la página descifrada mapeada en 0x8000, y los bytes 0x00-0x02
 * parcheados para redirigir al firmware al parche elegido.
 *
 * @param dst Destino de PARAMS_ROM_BYTES.
 * @param src ROM de parámetros sin descifrar.
 * @param from_addr Offset del parche dentro de la ROM.
 */
void decode_params_page(u8 *dst, const u8 *src, size_t from_addr);

/**
 * @brief El destino al que apuntan los bytes 0x00-0x02.
 * @param from_addr Offset del parche dentro de la ROM de parámetros.
 * @return Dirección del bus a la que redirigir al firmware.
 */
constexpr size_t params_patch_target(size_t from_addr) { return (from_addr - (from_addr >> 15 << 15)) + 0x4000; }

#endif
