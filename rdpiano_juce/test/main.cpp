// rdpiano_plugin_tests: lo único que hay que probar con JUCE delante.
//
// El motor entero se prueba sin JUCE en `librdpiano/test/unit/test_engine.cpp`.
// Lo que queda aquí es el plugin propiamente dicho —parámetros, presets y
// programas—, que hasta la fase 3 sólo se podía verificar abriendo un DAW
// (REFACTORIZACION §17.7). Es posible desde que el plugin se construye con
// CMake: `juce_add_console_app` enlaza el mismo código compartido que los cinco
// formatos, así que esto instancia el `AudioProcessor` de verdad.
//
// Comparte el andamiaje de `librdpiano/test/` —`check.h` y `unit_test.h`— para
// que un fallo se lea igual que los del núcleo.

#include <stdio.h>
#include <string.h>

#include "unit_test.h"

// Las suites del núcleo los usan; aquí no se lee ninguna ROM de disco (el
// plugin las lleva empotradas), pero el registro es el mismo.
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

    if (arg == "--filter" && next)
      filter = argv[++i];
    else if (arg == "--list")
      listOnly = true;
    else
    {
      fprintf(stderr, "uso: %s [--filter SUBCADENA] [--list]\n", argv[0]);
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

    printf("%-28s %4d comprobacion(es)%s\n", suite.name, checks.total,
           checks.failed() ? "  <-- FALLA" : "");
    checks.print_failures();

    suites++;
    total += checks.total;
    failed += checks.failed();
  }

  if (listOnly)
    return 0;

  printf("\n%d suite(s), %d comprobacion(es), %d fallida(s)\n", suites, total,
         failed);

  return failed ? 1 : 0;
}
