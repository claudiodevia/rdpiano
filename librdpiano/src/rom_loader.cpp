#include "../include/rom_loader.h"

/**
 * @file rom_loader.cpp
 * @brief Descifrado de las ROM con las permutaciones de la cabecera.
 */

void decode_program_rom(u8 *dst, const u8 *src)
{
    for (size_t srcpos = 0x00; srcpos < PROG_ROM_BYTES; srcpos++)
        dst[srcpos] = unscramble_data_cpub(src[unscramble_addr_cpub(srcpos)]);
}

void decode_params_window(u8 *dst, const u8 *src, size_t from_addr)
{
    size_t from_addr_aligned = from_addr >> 15 << 15;
    for (size_t srcpos = 0x00; srcpos < PARAMS_PAGE_BYTES; srcpos++)
        dst[srcpos] = unscramble_data_params(src[unscramble_addr_params(srcpos + from_addr_aligned)]);
}

void decode_params_page(u8 *dst, const u8 *src, size_t from_addr)
{
    for (size_t srcpos = 0x00; srcpos < PARAMS_ROM_BYTES; srcpos++)
        dst[srcpos] = 0xff;

    decode_params_window(dst + PARAMS_PAGE_OFFSET, src, from_addr);
    write_params_header(dst, from_addr);
}

void write_params_header(u8 *dst, size_t from_addr)
{
    const size_t target = params_patch_target(from_addr);
    dst[0x00] = 0x01;
    dst[0x01] = (target >> 8) & 0xff;
    dst[0x02] = target & 0xff;
}
