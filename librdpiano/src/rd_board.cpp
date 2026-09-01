#include "../include/rd_board.h"

RdBoard::RdBoard(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_progrom,
                 const u8 *temp_paramsrom)
    : sound_chip(temp_ic5, temp_ic6, temp_ic7)
{
    decode_program_rom(program_rom, temp_progrom);

    loadRomSet(temp_ic5, temp_ic6, temp_ic7, temp_paramsrom);
    selectPatch(0x00);
}

void RdBoard::prepareRomSet(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7)
{
    sound_chip.decode_samples(temp_ic5, temp_ic6, temp_ic7);
}

void RdBoard::publishRomSet(const u8 *temp_paramsrom)
{
    sound_chip.publish_samples();
    params_rom_src = temp_paramsrom;
}

void RdBoard::loadRomSet(const u8 *temp_ic5, const u8 *temp_ic6, const u8 *temp_ic7, const u8 *temp_paramsrom)
{
    prepareRomSet(temp_ic5, temp_ic6, temp_ic7);
    publishRomSet(temp_paramsrom);
}

void RdBoard::selectPatch(size_t from_addr) { decode_params_page(params_rom, params_rom_src, from_addr); }
