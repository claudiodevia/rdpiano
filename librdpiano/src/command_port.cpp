#include "../include/command_port.h"

/**
 * @file command_port.cpp
 * @brief Codificación del master tune; el resto del protocolo es inline en la cabecera.
 */

MasterTuneBytes encode_master_tune(int16_t tune)
{
    const int magnitude = tune < 0 ? -(int)tune : (int)tune;

    int lsb = magnitude * 16 / 32767 * 4;
    if (lsb > 0x3c)
        lsb = 0x3c;

    MasterTuneBytes out;
    out.msb = tune < 0 ? 0x7f : 0x00;
    out.lsb = (u8)(tune < 0 ? 0x48 + lsb : lsb);
    return out;
}
