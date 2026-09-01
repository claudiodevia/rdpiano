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
    "MKS-20: Piano 1",   "MKS-20: Piano 2",    "MKS-20: Piano 3",   "MKS-20: Harpsichord",
    "MKS-20: Clavi",     "MKS-20: Vibraphone", "MKS-20: E-Piano 1", "MKS-20: E-Piano 2",

    "MK-80: Classic",    "MK-80: Special",     "MK-80: Blend",      "MK-80: Contemporary",
    "MK-80: A. Piano 1", "MK-80: A. Piano 2",  "MK-80: Clavi",      "MK-80: Vibraphone"};

inline constexpr int patchToRomSetId[NUM_PATCHES] = {ROMSET_MKS20_A, ROMSET_MKS20_A, ROMSET_MKS20_A, ROMSET_MKS20_B,
                                                     ROMSET_MKS20_B, ROMSET_MKS20_B, ROMSET_MKS20_B, ROMSET_MKS20_B,
                                                     ROMSET_MK80,    ROMSET_MK80,    ROMSET_MK80,    ROMSET_MK80,
                                                     ROMSET_MK80,    ROMSET_MK80,    ROMSET_MK80,    ROMSET_MK80};

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

// Compensación de ganancia por parche (FIABILIDAD §4 · N3).
//
// El emulador no tiene headroom. FIABILIDAD lo midió sobre la señal seca —8 de
// los 16 parches por encima de fondo de escala con un acorde de 16 notas a
// velocity 127, el parche 0 en 2,82— y medido después con la cadena entera del
// motor, con el EQ de +8 dB que es lo que de verdad sale, resultó ser peor:
// pasaban los 16, de +1,9 dBFS (Harpsichord) a +13,7 (E-Piano 1). Y no pasaban
// lo mismo: casi 12 dB entre el más flojo y el más caliente, así que cambiar de
// sonido cambiaba de nivel.
//
// Estos factores normalizan los 16 al mismo pico, medido con la cadena del
// motor (seca, EQ incluido) a 48 kHz. El motor los aplica en la salida,
// después de los efectos: el camino entero del emulador y de lsp/ es
// aritmética entera y sigue intacto, así que ni el golden del e2e ni los
// hashes de test_lsp.cpp se mueven.
//
// El objetivo es +3 dBFS y es una decisión de nivel, no de seguridad: a los
// -6 dBFS que la tabla tuvo hasta ahora el plugin sonaba flojo a volumen
// máximo. El caso que se normaliza es el peor razonable —acorde de 16 notas a
// velocity 127 con `volume` a tope—, así que tocar normal queda muy por
// debajo; lo que se acepta a cambio es que ESE caso pase de fondo de escala.
//
// Detrás NO hay limitador, así que lo que se pase recorta en la salida del
// host. Medido, el chorus a profundidad de fábrica añade hasta +4,9 dB sobre
// la señal seca —el phaser, al revés, atenúa—, de modo que con la seca a
// +3 dBFS el peor caso de toda la cadena (Vibraphone MKS-20 con chorus) llega
// a +7,9 dBFS. Es el precio del nivel y está medido, no es un descuido: si
// alguna vez se quiere volver a un objetivo que no recorte, el número es
// -6 dBFS (0.5f) y basta con regenerar la tabla.
//
// Se regeneran con `rdpiano_e2e --headroom`, que mide el pico de cada parche
// con estos factores ya puestos y escribe la tabla corregida: es idempotente,
// una segunda pasada devuelve los mismos números.
inline constexpr float HEADROOM_TARGET_PEAK = 1.41254f; // +3 dBFS

inline constexpr float patchOutputGain[NUM_PATCHES] = {
    // MKS-20                 sin compensar     ya compensado, c/chorus
    0.50135f, // Piano 1       2,82   +9,0 dBFS   1,40   +2,9 dBFS
    0.87199f, // Piano 2       1,62   +4,2 dBFS   2,29   +7,2 dBFS
    0.67421f, // Piano 3       2,10   +6,4 dBFS   1,77   +5,0 dBFS
    1.13184f, // Harpsichord   1,25   +1,9 dBFS   2,10   +6,4 dBFS
    0.73226f, // Clavi         1,93   +5,7 dBFS   1,82   +5,2 dBFS
    1.08648f, // Vibraphone    1,30   +2,3 dBFS   2,47   +7,9 dBFS <- peor
    0.29215f, // E-Piano 1     4,83  +13,7 dBFS   0,83   -1,6 dBFS
    0.39838f, // E-Piano 2     3,55  +11,0 dBFS   1,10   +0,8 dBFS

    // MK80
    0.78134f, // Classic       1,81   +5,1 dBFS   1,94   +5,7 dBFS
    0.48444f, // Special       2,92   +9,3 dBFS   1,24   +1,9 dBFS
    0.48921f, // Blend         2,89   +9,2 dBFS   1,28   +2,1 dBFS
    0.83507f, // Contemporary  1,69   +4,6 dBFS   2,03   +6,1 dBFS
    0.48602f, // A. Piano 1    2,91   +9,3 dBFS   1,36   +2,7 dBFS
    0.84343f, // A. Piano 2    1,67   +4,5 dBFS   1,99   +6,0 dBFS
    1.03420f, // Clavi         1,37   +2,7 dBFS   2,02   +6,1 dBFS
    0.72528f, // Vibraphone    1,95   +5,8 dBFS   1,42   +3,1 dBFS
};

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
    {"mks20_15179738.BIN", "mks20_15179737.BIN", "mks20_15179736.BIN", "mks20_15179757.BIN"},
    // ROMSET_MKS20_B
    {"mks20_15179741.BIN", "mks20_15179740.BIN", "mks20_15179739.BIN", "mks20_15179757.BIN"},
    // ROMSET_MK80
    {"MK80_IC5.bin", "MK80_IC6.bin", "MK80_IC7.bin", "MK80_IC18.bin"},
};

// Ojo: el handshake del bus depende de direcciones fijas de este firmware.
// Ver docs/FIRMWARE.md.
inline constexpr const char *PROG_ROM_FILE = "RD200_B.bin";

inline constexpr size_t WAVE_ROM_SIZE = 0x20000;
inline constexpr size_t PROG_ROM_SIZE = 0x2000;

// Coherencia de las tablas paralelas, en compilación. Lo que no cabe aquí
// —que los ficheros existan y midan lo que deben— está en
// librdpiano/test/unit/test_patches.cpp.
namespace patches_detail
{

    template <typename T, size_t N> constexpr size_t count(const T (&)[N]) { return N; }

    constexpr bool offsets_in_range()
    {
        for (int i = 0; i < NUM_PATCHES; i++)
            if (patchToOffset[i] >= WAVE_ROM_SIZE)
                return false;
        return true;
    }

    constexpr bool rom_sets_in_range()
    {
        for (int i = 0; i < NUM_PATCHES; i++)
            if (patchToRomSetId[i] < 0 || patchToRomSetId[i] >= ROMSET_COUNT)
                return false;
        return true;
    }

    constexpr bool sample_rates_known()
    {
        for (int i = 0; i < NUM_PATCHES; i++)
            if (patchSampleRates[i] != 20000 && patchSampleRates[i] != 32000)
                return false;
        return true;
    }

    constexpr bool gains_in_range()
    {
        // Ni mudo ni un impulsor: una compensación fuera de este margen es un
        // error de la tabla, no una decisión de producto.
        for (int i = 0; i < NUM_PATCHES; i++)
            if (!(patchOutputGain[i] > 0.05f) || !(patchOutputGain[i] < 4.0f))
                return false;
        return true;
    }

    constexpr bool names_present()
    {
        for (int i = 0; i < NUM_PATCHES; i++)
            if (patchNames[i] == nullptr || patchNames[i][0] == '\0')
                return false;
        return true;
    }

    constexpr bool rom_files_present()
    {
        for (int s = 0; s < ROMSET_COUNT; s++)
            for (int c = 0; c < ROM_CHIP_COUNT; c++)
                if (romSetFiles[s][c] == nullptr || romSetFiles[s][c][0] == '\0')
                    return false;
        return true;
    }

} // namespace patches_detail

static_assert(patches_detail::count(patchNames) == NUM_PATCHES);
static_assert(patches_detail::count(patchToRomSetId) == NUM_PATCHES);
static_assert(patches_detail::count(patchToOffset) == NUM_PATCHES);
static_assert(patches_detail::count(patchSampleRates) == NUM_PATCHES);
static_assert(patches_detail::count(patchOutputGain) == NUM_PATCHES);
static_assert(patches_detail::count(romSetFiles) == ROMSET_COUNT);

static_assert(patches_detail::offsets_in_range());
static_assert(patches_detail::rom_sets_in_range());
static_assert(patches_detail::sample_rates_known());
static_assert(patches_detail::gains_in_range());
static_assert(patches_detail::names_present());
static_assert(patches_detail::rom_files_present());

#endif
