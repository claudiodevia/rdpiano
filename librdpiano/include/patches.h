#ifndef PATCHES_H
#define PATCHES_H

#include <stddef.h>

// Tabla de parches compartida entre el plugin y el harness de test.
// Sin dependencias: solo datos. Los punteros a las ROMs los resuelve cada
// consumidor (BinaryData en el plugin, ficheros en el test).

enum RomSetId
{
    ROMSET_MKS20_A = 0,
    ROMSET_MKS20_B = 1,
    ROMSET_MK80 = 2,

    ROMSET_COUNT
};

static const int NUM_PATCHES = 16;

static const char *const patchNames[NUM_PATCHES] = {
    "MKS-20: Piano 1", "MKS-20: Piano 2", "MKS-20: Piano 3",
    "MKS-20: Harpsichord", "MKS-20: Clavi", "MKS-20: Vibraphone",
    "MKS-20: E-Piano 1", "MKS-20: E-Piano 2",

    "MK-80: Classic", "MK-80: Special", "MK-80: Blend",
    "MK-80: Contemporary", "MK-80: A. Piano 1", "MK-80: A. Piano 2",
    "MK-80: Clavi", "MK-80: Vibraphone"};

static const int patchToRomSetId[NUM_PATCHES] = {
    ROMSET_MKS20_A, ROMSET_MKS20_A, ROMSET_MKS20_A, ROMSET_MKS20_B,
    ROMSET_MKS20_B, ROMSET_MKS20_B, ROMSET_MKS20_B, ROMSET_MKS20_B,
    ROMSET_MK80, ROMSET_MK80, ROMSET_MK80, ROMSET_MK80,
    ROMSET_MK80, ROMSET_MK80, ROMSET_MK80, ROMSET_MK80};

// Offset dentro de la params ROM donde empieza cada parche.
static const size_t patchToOffset[NUM_PATCHES] = {
    // MKS-20
    0x000000, // Piano 1
    0x008000, // Piano 2
    0x010000, // Piano 3
    0x018000, // Harpsichord
    0x003c20, // Clavi
    0x00ab50, // Vibraphone
    0x014260, // E-Piano 1
    0x01bef0, // E-Piano 2

    // MK80
    0x000020, // Classic
    0x008000, // Special
    0x010000, // Blend
    0x018000, // Contemporary
    0x002c00, // A. Piano 1
    0x00b1f0, // A. Piano 2
    0x012910, // Clavi
    0x0199f0, // Vibraphone
};

static const int patchSampleRates[NUM_PATCHES] = {
    // MKS-20
    20000, 20000, 20000, 32000, 32000, 20000, 20000, 32000,
    // MK80
    20000, 20000, 20000, 32000, 20000, 20000, 32000, 20000};

#endif
