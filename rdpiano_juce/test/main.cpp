/**
 * @file main.cpp
 * @brief rdpiano_plugin_tests: lo único que hay que probar con JUCE delante.
 *
 * El plugin propiamente dicho: parámetros, presets y programas. El motor se prueba sin JUCE en
 * `librdpiano/test/unit/`.
 *
 * `juce_add_console_app` enlaza el mismo código que los cinco formatos, así que esto instancia el
 * `AudioProcessor` de verdad. El andamiaje —`check.h`, `unit_test.h`, `test_runner.h`— es el del núcleo.
 *
 * Uso: rdpiano_plugin_tests [--filter SUBCADENA] [--list]
 */

#include "test_runner.h"

int main(int argc, char **argv) { return run_suites(argc, argv, {}); }
