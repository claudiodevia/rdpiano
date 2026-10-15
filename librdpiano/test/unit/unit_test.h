#ifndef RDPIANO_UNIT_TEST_H
#define RDPIANO_UNIT_TEST_H

/**
 * @file unit_test.h
 * @brief Registro de suites de rdpiano_tests.
 *
 * Cada test_*.cpp escribe `TEST_SUITE(nombre) { CHECK(algo); }` y la suite se
 * registra sola; run_suites() (test_runner.h) las recorre y `--filter SUBCADENA`
 * selecciona un subconjunto. Las del motor y el chip sí emulan audio, pero en
 * trozos cortos: la suite entera dura unos segundos.
 */

#include <string>
#include <vector>

#include "check.h"

/** @brief El cuerpo de una suite: registra sus comprobaciones en el CheckRun que recibe. */
typedef void (*TestFn)(CheckRun &);

/** @brief Una suite registrada. */
struct TestSuite
{
    const char *name; ///< El nombre con el que la selecciona --filter.
    TestFn fn;        ///< Su cuerpo.
};

/** @brief El registro global de suites, en el orden en que se registraron. */
std::vector<TestSuite> &test_registry();

/** @brief Registra una suite al construirse; lo instancia TEST_SUITE. */
struct TestRegistrar
{
    TestRegistrar(const char *name, TestFn fn) { test_registry().push_back({name, fn}); }
};

#define TEST_SUITE(name)                                \
    static void name(CheckRun &checks);                 \
    static TestRegistrar name##_registrar(#name, name); \
    static void name(CheckRun &checks)

/// Directorio pasado con --roms; las suites que no leen ROM lo ignoran.
extern std::string g_roms_dir;

/// Directorio pasado con --vectors: los casos capturados de test/vectors/.
extern std::string g_vectors_dir;

/// Sin ROM la suite no puede comprobar nada: falla y sale. Que existan y midan lo que deben lo detalla
/// patches_rom_files.
#define ROMS_OR_FAIL(ok)                                                                            \
    do                                                                                              \
    {                                                                                               \
        if (!(ok))                                                                                  \
        {                                                                                           \
            checks.add("ROMs disponibles", false, check_fmt("sin ROMs en %s", g_roms_dir.c_str())); \
            return;                                                                                 \
        }                                                                                           \
    } while (0)

#endif
