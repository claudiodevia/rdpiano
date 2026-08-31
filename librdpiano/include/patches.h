#ifndef PATCHES_H
#define PATCHES_H

#include <stddef.h>

// Tabla de parches compartida entre el plugin y las pruebas.
// Sin dependencias: solo datos. Los punteros a las ROMs los resuelve cada
// consumidor (BinaryData en el plugin, ficheros en las pruebas), pero los
// nombres canónicos de fichero viven aquí para que no puedan discrepar.
//
// `inline constexpr` (C++17) y no `static const`: con `static` cada unidad de
// traducción recibiría su propia copia de cada tabla.

enum RomSetId
{
    ROMSET_MKS20_A = 0,
    ROMSET_MKS20_B = 1,
    ROMSET_MK80 = 2,

    ROMSET_COUNT
};

inline constexpr int NUM_PATCHES = 16;

inline constexpr const char *patchNames[NUM_PATCHES] = {
    "MKS-20: Piano 1", "MKS-20: Piano 2", "MKS-20: Piano 3",
    "MKS-20: Harpsichord", "MKS-20: Clavi", "MKS-20: Vibraphone",
    "MKS-20: E-Piano 1", "MKS-20: E-Piano 2",

    "MK-80: Classic", "MK-80: Special", "MK-80: Blend",
    "MK-80: Contemporary", "MK-80: A. Piano 1", "MK-80: A. Piano 2",
    "MK-80: Clavi", "MK-80: Vibraphone"};

inline constexpr int patchToRomSetId[NUM_PATCHES] = {
    ROMSET_MKS20_A, ROMSET_MKS20_A, ROMSET_MKS20_A, ROMSET_MKS20_B,
    ROMSET_MKS20_B, ROMSET_MKS20_B, ROMSET_MKS20_B, ROMSET_MKS20_B,
    ROMSET_MK80, ROMSET_MK80, ROMSET_MK80, ROMSET_MK80,
    ROMSET_MK80, ROMSET_MK80, ROMSET_MK80, ROMSET_MK80};

// Offset dentro de la params ROM donde empieza cada parche.
inline constexpr size_t patchToOffset[NUM_PATCHES] = {
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

inline constexpr int patchSampleRates[NUM_PATCHES] = {
    // MKS-20
    20000, 20000, 20000, 32000, 32000, 20000, 20000, 32000,
    // MK80
    20000, 20000, 20000, 32000, 20000, 20000, 32000, 20000};

// Las cuatro ROMs de cada juego, en el orden de RomChip. El plugin las empotra
// como BinaryData y las pruebas las leen de roms/: los nombres tienen que ser
// los mismos, y esta es la única lista.
enum RomChip
{
    ROM_IC5 = 0,  // onda
    ROM_IC6 = 1,  // onda
    ROM_IC7 = 2,  // onda
    ROM_IC18 = 3, // params

    ROM_CHIP_COUNT
};

inline constexpr const char *romSetFiles[ROMSET_COUNT][ROM_CHIP_COUNT] = {
    // ROMSET_MKS20_A
    {"mks20_15179738.BIN", "mks20_15179737.BIN", "mks20_15179736.BIN",
     "mks20_15179757.BIN"},
    // ROMSET_MKS20_B
    {"mks20_15179741.BIN", "mks20_15179740.BIN", "mks20_15179739.BIN",
     "mks20_15179757.BIN"},
    // ROMSET_MK80
    {"MK80_IC5.bin", "MK80_IC6.bin", "MK80_IC7.bin", "MK80_IC18.bin"},
};

// Ojo: el handshake del bus depende de direcciones fijas de este firmware.
// Ver docs/FIRMWARE.md.
inline constexpr const char *PROG_ROM_FILE = "RD200_B.bin";

inline constexpr size_t WAVE_ROM_SIZE = 0x20000;
inline constexpr size_t PROG_ROM_SIZE = 0x2000;

#endif
