#include "../include/rd_board.h"

RdBoard::RdBoard(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_progrom,
                 const u8 *temp_paramsrom)
    : sound_chip(temp_ic5, temp_ic6, temp_ic7)
{
    decode_program_rom(program_rom, temp_progrom);

    loadRomSet(temp_ic5, temp_ic6, temp_ic7, temp_paramsrom);
    selectPatch(0x00);
}

void RdBoard::decodeRomSet(unsigned slot, const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7)
{
    sound_chip.decode_samples(slot, temp_ic5, temp_ic6, temp_ic7);
}

void RdBoard::selectRomSet(unsigned slot, const u8 *temp_paramsrom)
{
    sound_chip.select_samples(slot);
    params_rom_src = temp_paramsrom;
}

void RdBoard::loadRomSet(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_paramsrom)
{
    decodeRomSet(0, temp_ic5, temp_ic6, temp_ic7);
    selectRomSet(0, temp_paramsrom);
}

void RdBoard::selectPatch(size_t from_addr) { decode_params_page(params_rom, params_rom_src, from_addr); }

void RdBoard::selectPatchPage(const u8 *page, size_t from_addr)
{
    for (size_t i = 0; i < PARAMS_PAGE_BYTES; i++)
        params_rom[i + 0x8000] = page[i];

    const size_t target = params_patch_target(from_addr);
    params_rom[0x00] = 0x01;
    params_rom[0x01] = (target >> 8) & 0xff;
    params_rom[0x02] = target & 0xff;
}

void RdBoard::reset()
{
    for (size_t i = 0; i < sizeof(ram); i++)
        ram[i] = 0;

    latch_val = 0x00;
    command_port.queue().clear();
    sound_chip.reset();
}
