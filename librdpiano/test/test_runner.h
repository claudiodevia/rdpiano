#ifndef RDPIANO_TEST_RUNNER_H
#define RDPIANO_TEST_RUNNER_H

/**
 * @file test_runner.h
 * @brief El `main` de rdpiano_tests y de rdpiano_plugin_tests: recorre el registro de suites.
 */

#include <initializer_list>
#include <string>

/** @brief Una opción `--nombre VALOR` propia de un ejecutable de pruebas. */
struct RunnerFlag
{
    const char *name;   ///< Con los dos guiones, como se escribe en la línea de órdenes.
    std::string *value; ///< Dónde se guarda el valor.
};

/**
 * @brief Corre las suites registradas e imprime el resumen.
 *
 * Opciones comunes: `--filter SUBCADENA` y `--list`; el resto, las de `flags`.
 *
 * @param flags Opciones con valor que entiende este ejecutable además de las comunes.
 * @return El código de salida: 0 si todo pasa, 1 si falla algo, 2 si la línea de órdenes no se entiende.
 */
int run_suites(int argc, char **argv, std::initializer_list<RunnerFlag> flags);

#endif
