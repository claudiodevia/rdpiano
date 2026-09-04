#ifndef PATCHES_H
#define PATCHES_H

#include <stddef.h>

/**
 * @file patches.h
 * @brief Tabla de parches compartida entre el plugin y las pruebas: solo datos, sin dependencias.
 *
 * Cada consumidor resuelve los punteros a las ROM —BinaryData en el plugin,
 * ficheros en las pruebas—, pero los nombres canónicos están aquí. Todo es
 * `inline constexpr` y no `static const`: con `static` habría una copia de cada
 * tabla por unidad de traducción.
 */

/** @brief Los tres juegos de ROM. */
enum RomSetId
{
    ROMSET_MKS20_A = 0,
    ROMSET_MKS20_B = 1,
    ROMSET_MK80 = 2,

    ROMSET_COUNT
};

inline constexpr int NUM_PATCHES = 16; ///< Parches que trae el firmware.

/** @brief Nombre de cada parche, tal como lo enseña el display. */
inline constexpr const char *patchNames[NUM_PATCHES] = {
    "MKS-20: Piano 1",   "MKS-20: Piano 2",    "MKS-20: Piano 3",   "MKS-20: Harpsichord",
    "MKS-20: Clavi",     "MKS-20: Vibraphone", "MKS-20: E-Piano 1", "MKS-20: E-Piano 2",

    "MK-80: Classic",    "MK-80: Special",     "MK-80: Blend",      "MK-80: Contemporary",
    "MK-80: A. Piano 1", "MK-80: A. Piano 2",  "MK-80: Clavi",      "MK-80: Vibraphone"};

/** @brief Juego de ROM que necesita cada parche. */
inline constexpr int patchToRomSetId[NUM_PATCHES] = {ROMSET_MKS20_A, ROMSET_MKS20_A, ROMSET_MKS20_A, ROMSET_MKS20_B,
                                                     ROMSET_MKS20_B, ROMSET_MKS20_B, ROMSET_MKS20_B, ROMSET_MKS20_B,
                                                     ROMSET_MK80,    ROMSET_MK80,    ROMSET_MK80,    ROMSET_MK80,
                                                     ROMSET_MK80,    ROMSET_MK80,    ROMSET_MK80,    ROMSET_MK80};

/** @brief Offset dentro de la ROM de parámetros donde empieza cada parche. */
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

/** @brief Tasa a la que corre el emulador en cada parche, en Hz. */
inline constexpr int patchSampleRates[NUM_PATCHES] = {
    // MKS-20
    20000, 20000, 20000, 32000, 32000, 20000, 20000, 32000,
    // MK80
    20000, 20000, 20000, 32000, 20000, 20000, 32000, 20000};

/// Pico al que se normalizan los 16 parches: +6 dBFS. Detrás NO hay limitador y
/// el chorus de fábrica añade hasta +4,8 dB, así que el peor caso de toda la
/// cadena llega a +10,8 dBFS. Para un objetivo que no recorte, 0.5f (-6 dBFS).
inline constexpr float HEADROOM_TARGET_PEAK = 1.99526f;

/**
 * @brief Compensación de ganancia por parche.
 *
 * Normaliza los 16 al mismo pico con el peor caso razonable —acorde de 16 notas
 * a velocity 127 y `volume` a tope—, medido con la cadena del motor a 48 kHz;
 * sin ella hay casi 12 dB entre el parche más flojo y el más caliente. Se aplica
 * en la salida, después de los efectos, así que ni el golden ni los hashes de
 * test_lsp.cpp se mueven. Se regenera con `rdpiano_e2e --headroom` (idempotente).
 */
inline constexpr float patchOutputGain[NUM_PATCHES] = {
    // MKS-20                 sin compensar     ya compensado, c/chorus
    0.70817f, // Piano 1       2,82   +9,0 dBFS  1,98   +5,9 dBFS
    1.23172f, // Piano 2       1,62   +4,2 dBFS  3,23  +10,2 dBFS
    0.93018f, // Piano 3       2,10   +6,4 dBFS  2,40   +7,6 dBFS
    1.61771f, // Harpsichord   1,25   +1,9 dBFS  3,02   +9,6 dBFS
    1.03435f, // Clavi         1,93   +5,7 dBFS  2,58   +8,2 dBFS
    1.55132f, // Vibraphone    1,30   +2,3 dBFS  3,47  +10,8 dBFS <- peor
    0.40819f, // E-Piano 1     4,83  +13,7 dBFS  1,18   +1,4 dBFS
    0.55217f, // E-Piano 2     3,55  +11,0 dBFS  1,52   +3,7 dBFS

    // MK80
    1.13067f, // Classic       1,81   +5,1 dBFS  2,82   +9,0 dBFS
    0.63764f, // Special       2,92   +9,3 dBFS  1,59   +4,0 dBFS
    0.68827f, // Blend         2,89   +9,2 dBFS  1,81   +5,2 dBFS
    1.15068f, // Contemporary  1,69   +4,6 dBFS  2,84   +9,1 dBFS
    0.71207f, // A. Piano 1    2,91   +9,3 dBFS  1,98   +5,9 dBFS
    1.31191f, // A. Piano 2    1,67   +4,5 dBFS  3,01   +9,6 dBFS
    1.46084f, // Clavi         1,37   +2,7 dBFS  2,86   +9,1 dBFS
    0.95056f, // Vibraphone    1,95   +5,8 dBFS  1,97   +5,9 dBFS
};

/** @brief Las cuatro ROM de un juego. */
enum RomChip
{
    ROM_IC5 = 0,  ///< ROM de onda.
    ROM_IC6 = 1,  ///< ROM de onda.
    ROM_IC7 = 2,  ///< ROM de onda.
    ROM_IC18 = 3, ///< ROM de parámetros.

    ROM_CHIP_COUNT
};

/**
 * @brief Nombre de fichero de cada ROM.
 *
 * El plugin las empotra como BinaryData y las pruebas las leen de roms/: esta es
 * la única lista.
 */
inline constexpr const char *romSetFiles[ROMSET_COUNT][ROM_CHIP_COUNT] = {
    // ROMSET_MKS20_A
    {"mks20_15179738.BIN", "mks20_15179737.BIN", "mks20_15179736.BIN", "mks20_15179757.BIN"},
    // ROMSET_MKS20_B
    {"mks20_15179741.BIN", "mks20_15179740.BIN", "mks20_15179739.BIN", "mks20_15179757.BIN"},
    // ROMSET_MK80
    {"MK80_IC5.bin", "MK80_IC6.bin", "MK80_IC7.bin", "MK80_IC18.bin"},
};

/// Ojo: el handshake del bus depende de direcciones fijas de este firmware.
/// Ver docs/FIRMWARE.md.
inline constexpr const char *PROG_ROM_FILE = "RD200_B.bin";

inline constexpr size_t WAVE_ROM_SIZE = 0x20000; ///< Tamaño de una ROM de onda.
inline constexpr size_t PROG_ROM_SIZE = 0x2000;  ///< Tamaño de la ROM de programa.

/**
 * @brief Coherencia de las tablas paralelas, comprobada en compilación.
 *
 * Que los ficheros existan y midan lo que deben lo comprueba
 * test/unit/test_patches.cpp.
 */
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

    /// Fuera de este margen es un error de la tabla, no una decisión.
    constexpr bool gains_in_range()
    {
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
