/**
 * @file test_runner.cpp
 * @brief Registro de suites y su recorrido, compartidos por las pruebas del núcleo y del plugin.
 */

#include "test_runner.h"

#include <stdio.h>
#include <string.h>

#include "unit_test.h"

std::vector<TestSuite> &test_registry()
{
    static std::vector<TestSuite> registry;
    return registry;
}

namespace
{

    /** @brief Imprime el uso con las opciones de este ejecutable. */
    void usage(const char *argv0, std::initializer_list<RunnerFlag> flags)
    {
        fprintf(stderr, "uso: %s", argv0);
        for (const RunnerFlag &f : flags)
            fprintf(stderr, " [%s DIR]", f.name);
        fprintf(stderr, " [--filter SUBCADENA] [--list]\n");
    }

} // namespace

int run_suites(int argc, char **argv, std::initializer_list<RunnerFlag> flags)
{
    std::string filter;
    bool listOnly = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;

        const RunnerFlag *own = nullptr;
        for (const RunnerFlag &f : flags)
            if (arg == f.name)
                own = &f;

        if (own && hasValue)
            *own->value = argv[++i];
        else if (arg == "--filter" && hasValue)
            filter = argv[++i];
        else if (arg == "--list")
            listOnly = true;
        else
        {
            usage(argv[0], flags);
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

        printf("%-28s %4d comprobación(es)%s\n", suite.name, checks.total, checks.failed() ? "  <-- FALLA" : "");
        checks.print_failures();

        suites++;
        total += checks.total;
        failed += checks.failed();
    }

    if (listOnly)
        return 0;

    printf("\n%d suite(s), %d comprobación(es), %d fallida(s)\n", suites, total, failed);

    return failed ? 1 : 0;
}
