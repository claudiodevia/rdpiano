/**
 * @file test_patches.cpp
 * @brief La tabla de parches: lo que no cabe en un static_assert.
 *
 * La coherencia de las tablas paralelas ya la cubre patches.h;
 * aquí se comprueba lo que depende del disco —que las ROMs existan, midan lo que deben y sean las
 * mismas que empotra el plugin.
 */

#include <stdio.h>

#include <set>
#include <string>

#include "patches.h"
#include "unit_test.h"

static long file_size(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fclose(f);
    return len;
}

/**
 * @brief Las 4 ROMs de cada juego + el firmware, con su tamaño exacto.
 */
TEST_SUITE(patches_rom_files)
{
    for (int set = 0; set < ROMSET_COUNT; set++)
    {
        for (int chip = 0; chip < ROM_CHIP_COUNT; chip++)
        {
            const char *name = romSetFiles[set][chip];
            long len = file_size(g_roms_dir + "/" + name);

            checks.add(std::string("existe ") + name, len >= 0, check_fmt("no se encuentra en %s", g_roms_dir.c_str()));
            if (len >= 0)
                checks.add(std::string("tamaño ") + name, (size_t)len == WAVE_ROM_SIZE,
                           check_fmt("%ld bytes, esperados %zu", len, WAVE_ROM_SIZE));
        }
    }

    long prog = file_size(g_roms_dir + "/" + PROG_ROM_FILE);
    checks.add(std::string("existe ") + PROG_ROM_FILE, prog >= 0,
               check_fmt("no se encuentra en %s", g_roms_dir.c_str()));
    if (prog >= 0)
        checks.add(std::string("tamaño ") + PROG_ROM_FILE, (size_t)prog == PROG_ROM_SIZE,
                   check_fmt("%ld bytes, esperados %zu", prog, PROG_ROM_SIZE));
}

/**
 * @brief Cada parche apunta a un juego de ROMs que existe y a una página válida de la params ROM (offset
 *        alineable a 32 KB dentro de los 128 KB).
 */
TEST_SUITE(patches_table)
{
    for (int patch = 0; patch < NUM_PATCHES; patch++)
    {
        size_t page = patchToOffset[patch] >> 15;
        checks.add(check_fmt("parche %d: página de params", patch), page < 4,
                   check_fmt("offset %06zx", patchToOffset[patch]));
    }

    // Los nombres se muestran en el LCD y en getProgramName: sin repetidos.
    std::set<std::string> names;
    for (int patch = 0; patch < NUM_PATCHES; patch++)
        names.insert(patchNames[patch]);
    CHECK_EQ(names.size(), NUM_PATCHES);
}

/**
 * @brief El plugin no lee de disco: empotra las ROMs con `juce_add_binary_data` desde su CMakeLists. Si una
 *        de las dos listas cambia sin la otra, plugin y pruebas dejan de probar lo mismo.
 */
TEST_SUITE(patches_plugin_resources)
{
    std::string cmakePath = g_roms_dir + "/../rdpiano_juce/CMakeLists.txt";
    FILE *f = fopen(cmakePath.c_str(), "rb");
    if (!f)
    {
        printf("    (nota: %s no encontrado, se omite)\n", cmakePath.c_str());
        return;
    }

    std::string cmakeText;
    char buf[4096];
    size_t read;
    while ((read = fread(buf, 1, sizeof buf, f)) > 0)
        cmakeText.append(buf, read);
    fclose(f);

    for (int set = 0; set < ROMSET_COUNT; set++)
        for (int chip = 0; chip < ROM_CHIP_COUNT; chip++)
            checks.add(std::string("empotrada ") + romSetFiles[set][chip],
                       cmakeText.find(std::string("../roms/") + romSetFiles[set][chip]) != std::string::npos,
                       "no aparece en juce_add_binary_data");

    checks.add(std::string("empotrada ") + PROG_ROM_FILE,
               cmakeText.find(std::string("../roms/") + PROG_ROM_FILE) != std::string::npos,
               "no aparece en juce_add_binary_data");
}
