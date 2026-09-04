#ifndef RDPIANO_UNIT_TEST_H
#define RDPIANO_UNIT_TEST_H

/**
 * @file unit_test.h
 * @brief Registro de suites de rdpiano_tests.
 *
 * Cada test_*.cpp escribe `TEST_SUITE(nombre) { CHECK(algo); }` y la suite se
 * registra sola; main.cpp las recorre y `--filter SUBCADENA` selecciona un
 * subconjunto. A diferencia del harness e2e, estas pruebas no emulan audio: la
 * suite entera tiene que terminar en menos de un segundo.
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

#endif
