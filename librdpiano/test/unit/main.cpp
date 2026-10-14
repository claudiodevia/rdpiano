/**
 * @file main.cpp
 * @brief rdpiano_tests: la suite unitaria del núcleo.
 *
 * Complementa a rdpiano_e2e, que es agregado (un hash por parche, con firmware y CPU emulada).
 * Aquí se prueban unidades sueltas para que un fallo diga qué se rompió y no solo que algo cambió.
 *
 * Uso: rdpiano_tests [--roms DIR] [--vectors DIR] [--filter SUBCADENA] [--list]
 */

#include "test_runner.h"
#include "unit_test.h"

std::string g_roms_dir = "roms";
std::string g_vectors_dir = "test/vectors";

int main(int argc, char **argv)
{
    return run_suites(argc, argv, {{"--roms", &g_roms_dir}, {"--vectors", &g_vectors_dir}});
}
