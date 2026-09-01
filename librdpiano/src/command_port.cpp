#include "../include/command_port.h"

#include <math.h>

MasterTuneBytes encode_master_tune(int16_t tune)
{
    // Copia literal de las cuatro líneas que estaban duplicadas en
    // PluginProcessor.cpp (setMasterTune y mcuReset). `magnitude` en vez de
    // `abs()` para no depender de qué sobrecarga entra por la cabecera: para un
    // int16 el valor es el mismo, incluido -32768.
    int magnitude = tune < 0 ? -(int)tune : (int)tune;

    MasterTuneBytes out;
    out.msb = tune < 0 ? 0x7f : 0x00;
    out.lsb = (int8_t)(floor(magnitude / 32767.0 * 16.0) * 4) & 0xff;
    if (out.lsb > 0x3c)
        out.lsb = 0x3c;
    if (tune < 0)
        out.lsb = 0x48 + out.lsb;
    return out;
}
