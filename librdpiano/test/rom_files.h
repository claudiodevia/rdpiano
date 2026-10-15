#ifndef RDPIANO_ROM_FILES_H
#define RDPIANO_ROM_FILES_H

/**
 * @file rom_files.h
 * @brief Lectura de los dumps de `roms/` para las pruebas, el harness y el ejecutable de escucha.
 *
 * Los nombres y tamaños son los de patches.h, los mismos que empotra el plugin. Aquí no se decide qué
 * hacer si falta una ROM: cada llamante falla a su manera (ROMS_OR_FAIL en las suites, salir en e2e).
 */

#include <stdio.h>

#include <map>
#include <string>
#include <vector>

#include "patches.h"
#include "rd_engine.h"

/**
 * @brief Lee una ROM entera.
 * @param dir Directorio de los dumps.
 * @param name Nombre del fichero, de patches.h.
 * @param size Tamaño exacto que debe tener.
 * @return Su contenido; vacío si no existe o es más corta que `size`.
 */
inline std::vector<u8> read_rom(const std::string &dir, const char *name, size_t size)
{
    std::vector<u8> buf;
    FILE *f = fopen((dir + "/" + name).c_str(), "rb");
    if (!f)
        return buf;
    buf.resize(size);
    const size_t got = fread(buf.data(), 1, size, f);
    fclose(f);
    if (got != size)
        buf.clear();
    return buf;
}

/**
 * @brief Tamaño de un fichero de `dir`.
 * @return Bytes, o -1 si no existe.
 */
inline long rom_file_size(const std::string &dir, const char *name)
{
    FILE *f = fopen((dir + "/" + name).c_str(), "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fclose(f);
    return len;
}

/**
 * @brief Las ROM leídas, una vez cada una: varios juegos comparten fichero y leerlas por prueba dominaría
 *        el tiempo de la suite.
 */
struct RomCache
{
    std::string dir;                              ///< Directorio de los dumps.
    std::map<std::string, std::vector<u8>> files; ///< Ya leídas, por nombre.

    /**
     * @brief Una ROM, leída la primera vez que se pide.
     * @return Puntero estable mientras viva la caché; nullptr si falta o no mide `size`.
     */
    const u8 *get(const char *name, size_t size)
    {
        auto it = files.find(name);
        if (it == files.end())
        {
            std::vector<u8> data = read_rom(dir, name, size);
            if (data.empty())
                return nullptr;
            it = files.emplace(name, std::move(data)).first;
        }
        return it->second.data();
    }

    /**
     * @brief Los cuatro chips de un juego.
     * @return false si falta alguno; `set` puede quedar a medias.
     */
    bool romSet(int id, RdRomSet &set)
    {
        set.ic5 = get(romSetFiles[id][ROM_IC5], WAVE_ROM_SIZE);
        set.ic6 = get(romSetFiles[id][ROM_IC6], WAVE_ROM_SIZE);
        set.ic7 = get(romSetFiles[id][ROM_IC7], WAVE_ROM_SIZE);
        set.ic18 = get(romSetFiles[id][ROM_IC18], PARAMS_ROM_SIZE);
        return set.ic5 && set.ic6 && set.ic7 && set.ic18;
    }

    /**
     * @brief Lo que pide el constructor de RdPianoEngine: los ROMSET_COUNT juegos y el firmware.
     * @return false si falta cualquiera.
     */
    bool engineRoms(RdRomSet (&sets)[ROMSET_COUNT], const u8 *&prog)
    {
        prog = get(PROG_ROM_FILE, PROG_ROM_SIZE);
        bool ok = prog != nullptr;
        for (int s = 0; s < ROMSET_COUNT; s++)
            ok = romSet(s, sets[s]) && ok;
        return ok;
    }
};

#endif
