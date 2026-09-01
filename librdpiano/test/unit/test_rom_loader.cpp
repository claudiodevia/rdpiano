// Descifrado de ROM: permutaciones y páginas de params
// (REFACTORIZACION §6, §17.4).
//
// Funciones puras sobre u8[], sin CPU y sin audio: la parte del núcleo que más
// barato sale probar y la que más caro sale equivocarse, porque una
// permutación mal reordenada no da un error, da otro timbre.

#include <stdio.h>
#include <stdlib.h>

#include <set>
#include <string>
#include <vector>

#include "patches.h"
#include "rom_loader.h"
#include "unit_test.h"

static u64 fnv1a(const void *data, size_t n)
{
    const unsigned char *b = (const unsigned char *)data;
    u64 h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++)
    {
        h ^= b[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

// Lee una ROM entera. Devuelve vacío si no está: las suites de más abajo lo
// tratan como "no se puede comprobar", no como fallo — de que exista ya se
// queja patches_rom_files.
static std::vector<u8> read_rom(const std::string &name, size_t want)
{
    std::vector<u8> buf;
    FILE *f = fopen((g_roms_dir + "/" + name).c_str(), "rb");
    if (!f)
        return buf;
    buf.resize(want);
    size_t got = fread(buf.data(), 1, want, f);
    fclose(f);
    if (got != want)
        buf.clear();
    return buf;
}

// ---------------------------------------------------------------------------
// Las permutaciones son cableado del PCB: barajan bits, no los pierden. Luego
// son biyectivas, y eso es una propiedad —no un vector— que caza cualquier
// reordenación accidental sin necesidad de escuchar nada.

template <typename F> static void check_bijection(CheckRun &checks, const char *name, size_t n, F f)
{
    std::vector<bool> seen(n, false);
    size_t outOfRange = 0;
    size_t collisions = 0;

    for (size_t i = 0; i < n; i++)
    {
        size_t v = (size_t)f((u32)i);
        if (v >= n)
        {
            outOfRange++;
            continue;
        }
        if (seen[v])
            collisions++;
        seen[v] = true;
    }

    checks.add(std::string(name) + " sin salirse de rango", outOfRange == 0,
               check_fmt("%zu de %zu fuera de [0,%zu)", outOfRange, n, n));
    checks.add(std::string(name) + " sin colisiones", collisions == 0, check_fmt("%zu colisión(es)", collisions));
}

TEST_SUITE(rom_loader_bijections)
{
    check_bijection(checks, "addr_cpub", 0x4000, unscramble_addr_cpub);
    check_bijection(checks, "addr_params", 0x20000, unscramble_addr_params);
    check_bijection(checks, "addr_wave", 0x20000, unscramble_addr_wave);
    check_bijection(checks, "data_cpub", 0x100, [](u32 d) { return unscramble_data_cpub((u8)d); });
    check_bijection(checks, "data_wave", 0x100, [](u32 d) { return unscramble_data_wave((u8)d); });

    // La ROM de programa se lee en un espacio de 0x2000, pero la permutación es
    // de 14 bits: la mitad alta del índice sale del bit 13, que el bus nunca
    // levanta. Que sea biyectiva en 0x4000 es lo que hace segura la máscara.
    CHECK_EQ(unscramble_addr_cpub(0), 0);
    CHECK_EQ(unscramble_data_cpub(0), 0);
    CHECK_EQ(unscramble_data_cpub(0xff), 0xff);

    // Los datos de onda van sin permutar: la identidad, escrita como bitswap.
    size_t waveDataIdentity = 0;
    for (u32 d = 0; d < 0x100; d++)
        if (unscramble_data_wave((u8)d) == (u8)d)
            waveDataIdentity++;
    CHECK_EQ(waveDataIdentity, 0x100);

    // Los datos de params usan la misma permutación que los de la ROM de
    // programa: si un día dejan de coincidir, que se vea aquí.
    size_t paramsDataSame = 0;
    for (u32 d = 0; d < 0x100; d++)
        if (unscramble_data_params((u8)d) == unscramble_data_cpub((u8)d))
            paramsDataSame++;
    CHECK_EQ(paramsDataSame, 0x100);
}

// ---------------------------------------------------------------------------
// Hash del params_rom resultante para los 16 offsets de la tabla de parches.
// Capturados con el `loadSounds` monolítico anterior a la fase 1: son los que
// autorizan a partirlo en loadRomSet()/selectPatch().

static constexpr u64 PARAMS_ROM_HASHES[NUM_PATCHES] = {
    0x4a0c7db560b285cbull, //  0 MKS-20: Piano 1
    0x6665bfa9095c0115ull, //  1 MKS-20: Piano 2
    0x13cb6c03046581b5ull, //  2 MKS-20: Piano 3
    0x7c5e07c1665e799dull, //  3 MKS-20: Harpsichord
    0xd6b90a7b6193fa97ull, //  4 MKS-20: Clavi
    0x4128dc9297d45638ull, //  5 MKS-20: Vibraphone
    0xb53e6b5bb326c227ull, //  6 MKS-20: E-Piano 1
    0xd8b170ac68de9b33ull, //  7 MKS-20: E-Piano 2
    0x7e938859e1350511ull, //  8 MK-80: Classic
    0x7c4d96ffef17dcb4ull, //  9 MK-80: Special
    0x233283bf6117fe85ull, // 10 MK-80: Blend
    0x864118cb49346c09ull, // 11 MK-80: Contemporary
    0xd5d00ab509c0ae1dull, // 12 MK-80: A. Piano 1
    0x25741a2854a291fbull, // 13 MK-80: A. Piano 2
    0x48dc0408c2313e26ull, // 14 MK-80: Clavi
    0x497ae2d4ffcc667aull, // 15 MK-80: Vibraphone
};

// Y el de la ROM de programa, que no depende del parche.
static constexpr u64 PROGRAM_ROM_HASH = 0x32e8a60dd3121a8eull;

TEST_SUITE(rom_loader_params_pages)
{
    std::vector<u8> out(PARAMS_ROM_BYTES);

    for (int patch = 0; patch < NUM_PATCHES; patch++)
    {
        const char *name = romSetFiles[patchToRomSetId[patch]][ROM_IC18];
        std::vector<u8> src = read_rom(name, WAVE_ROM_SIZE);
        if (src.empty())
            continue;

        decode_params_page(out.data(), src.data(), patchToOffset[patch]);

        CHECK_HASH(patchNames[patch], fnv1a(out.data(), out.size()), PARAMS_ROM_HASHES[patch]);

        // Los bytes 0x00-0x02 redirigen al firmware al parche elegido. Hasta ahora
        // era un efecto lateral que nadie comprobaba.
        size_t target = params_patch_target(patchToOffset[patch]);
        checks.add(std::string("cabecera ") + patchNames[patch],
                   out[0] == 0x01 && out[1] == ((target >> 8) & 0xff) && out[2] == (target & 0xff),
                   check_fmt("%02x %02x %02x, esperado 01 %02x %02x", out[0], out[1], out[2],
                             (unsigned)((target >> 8) & 0xff), (unsigned)(target & 0xff)));

        // El target cae dentro de la ventana que la CPU ve mapeada en 0x4000.
        checks.add(std::string("target en ventana ") + patchNames[patch], target >= 0x4000 && target < 0xc000,
                   check_fmt("target %04zx", target));
    }
}

TEST_SUITE(rom_loader_program_rom)
{
    std::vector<u8> src = read_rom(PROG_ROM_FILE, PROG_ROM_SIZE);
    if (src.empty())
        return;

    std::vector<u8> out(PROG_ROM_BYTES);
    decode_program_rom(out.data(), src.data());

    CHECK_HASH("program_rom", fnv1a(out.data(), out.size()), PROGRAM_ROM_HASH);
}

// ---------------------------------------------------------------------------
// La equivalencia que autoriza el refactor de §6: partir el trabajo caro
// (ROM SET) del barato (PARCHE) no puede cambiar un byte del resultado, ni
// siquiera arrastrando estado de un parche al siguiente.

TEST_SUITE(rom_loader_select_patch_is_stateless)
{
    const char *name = romSetFiles[ROMSET_MKS20_A][ROM_IC18];
    std::vector<u8> src = read_rom(name, WAVE_ROM_SIZE);
    if (src.empty())
        return;

    std::vector<u8> fresh(PARAMS_ROM_BYTES);
    std::vector<u8> reused(PARAMS_ROM_BYTES, 0x5a);

    int diffs = 0;
    for (int patch = 0; patch < NUM_PATCHES; patch++)
    {
        // "reused" ya trae encima el parche anterior; "fresh" nace limpio.
        decode_params_page(fresh.data(), src.data(), patchToOffset[patch]);
        decode_params_page(reused.data(), src.data(), patchToOffset[patch]);
        if (fresh != reused)
            diffs++;
    }

    CHECK_EQ(diffs, 0);

    // Dos parches del mismo ROM set y de la misma página de 32 KB solo se
    // diferencian en la cabecera: es exactamente el trabajo que selectPatch()
    // ahorra frente a recargar el ROM set entero.
    decode_params_page(fresh.data(), src.data(), 0x000000);
    decode_params_page(reused.data(), src.data(), 0x003c20);

    size_t bodyDiffs = 0;
    for (size_t i = 0x03; i < PARAMS_ROM_BYTES; i++)
        if (fresh[i] != reused[i])
            bodyDiffs++;

    CHECK_EQ(bodyDiffs, 0);
    CHECK(fresh[1] != reused[1] || fresh[2] != reused[2]);
}
