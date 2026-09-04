/**
 * @file main.cpp
 * @brief rdpiano_tests: la suite unitaria del núcleo.
 *
 * Complementa a rdpiano_e2e, que es agregado (un hash por parche, con firmware y CPU emulada).
 * Aquí se prueban unidades sueltas, sin emular audio, para que un fallo diga qué se rompió y no
 * solo que algo cambió.
 *
 * Uso: rdpiano_tests [--roms DIR] [--vectors DIR] [--filter SUBCADENA] [--list]
 */

#include <stdio.h>
#include <string.h>

#include "unit_test.h"

std::string g_roms_dir = "roms";
std::string g_vectors_dir = "test/vectors";

std::vector<TestSuite> &test_registry()
{
    static std::vector<TestSuite> registry;
    return registry;
}

int main(int argc, char **argv)
{
    std::string filter;
    bool listOnly = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (arg == "--roms" && next)
            g_roms_dir = argv[++i];
        else if (arg == "--vectors" && next)
            g_vectors_dir = argv[++i];
        else if (arg == "--filter" && next)
            filter = argv[++i];
        else if (arg == "--list")
            listOnly = true;
        else
        {
            fprintf(stderr,
                    "uso: %s [--roms DIR] [--vectors DIR] [--filter SUBCADENA] "
                    "[--list]\n",
                    argv[0]);
            return 2;
        }
    }

    int suites = 0;
    int total = 0;
    int failed = 0;

    for (const TestSuite &suite : test_registry())
    {
        if (!filter.empty() && strstr(suite.name, filter.c_str()) == NULL)
            continue;

        if (listOnly)
        {
            printf("%s\n", suite.name);
            continue;
        }

        CheckRun checks;
        suite.fn(checks);

        printf("%-28s %4d comprobacion(es)%s\n", suite.name, checks.total, checks.failed() ? "  <-- FALLA" : "");
        checks.print_failures();

        suites++;
        total += checks.total;
        failed += checks.failed();
    }

    if (listOnly)
        return 0;

    printf("\n%d suite(s), %d comprobacion(es), %d fallida(s)\n", suites, total, failed);

    return failed ? 1 : 0;
}
