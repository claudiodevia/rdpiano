#!/bin/sh
# Reproduce en local los tres trabajos de prueba de la CI (.github/workflows/main.yml):
#
#   sh scripts/verificar.sh          núcleo, núcleo con ASan y plugin (cinco formatos + sus pruebas)
#   sh scripts/verificar.sh rapido   sólo el núcleo sin ASan (unit + e2e)
#
# Usa los mismos directorios que la CI (build/core, build/core-asan, build/plugin),
# así que lo ya compilado no se repite. Se detiene en el primer fallo y vuelca la
# cola del log. El modo completo necesita JUCE en build/juce (download-juce.sh).
set -e

ROOT=$(cd "$(dirname "$0")/.."; pwd)
. "$ROOT/scripts/common.sh"

uso()
{
    cat >&2 <<USAGE
${C_FUERTE}uso:${C_OFF} verificar.sh [rapido]

  ${C_FUERTE}(nada)${C_OFF}   lo que corre la CI: ctest del núcleo, ctest con ASan, los cinco
           formatos del plugin y sus pruebas
  ${C_FUERTE}rapido${C_OFF}   sólo el ctest del núcleo sin ASan, para iterar
USAGE
    exit 1
}

RAPIDO=
case $# in
    0) ;;
    1)
        case $(echo "$1" | tr '[:upper:]' '[:lower:]') in
            rapido|rápido) RAPIDO=1 ;;
            *) uso ;;
        esac
        ;;
    *) uso ;;
esac

# Configura librdpiano en $1 con ASan $2 y tipo de build $3, compila sus pruebas
# y corre ctest; $4 es la etiqueta de los pasos.
probar_nucleo()
{
    paso "$4: configurando"
    cmd cmake -S "$ROOT/librdpiano" -B "$ROOT/$1" -DRDPIANO_SANITIZE="$2" -DCMAKE_BUILD_TYPE="$3"
    paso "$4: compilando las pruebas"
    cmd cmake --build "$ROOT/$1" --parallel --target rdpiano_tests rdpiano_e2e
    paso "$4: ctest"
    cmd ctest --test-dir "$ROOT/$1" --output-on-failure
}

log_abrir verificar

if [ -n "$RAPIDO" ]; then
    titulo "Verificación rápida: núcleo sin ASan"
else
    titulo "Verificación completa: lo que corre la CI"
    [ -f "$ROOT/build/juce/CMakeLists.txt" ] ||
        fatal "JUCE no está en build/juce. Ejecuta scripts/download-juce.sh (o usa \`verificar.sh rapido\`)"
fi

probar_nucleo build/core OFF Release "Núcleo"

if [ -z "$RAPIDO" ]; then
    probar_nucleo build/core-asan ON RelWithDebInfo "Núcleo con ASan"

    paso "Plugin: los cinco formatos (build-osx.sh ALL)"
    cmd sh "$ROOT/scripts/build-osx.sh" ALL
    paso "Plugin: compilando sus pruebas"
    cmd cmake --build "$ROOT/build/plugin" --config Release --target rdpiano_plugin_tests
    paso "Plugin: ctest"
    cmd ctest --test-dir "$ROOT/build/plugin" -C Release -R plugin_state --output-on-failure
fi

fin "Todo en verde en $(transcurrido)s," "$(avisos) del núcleo (log completo en $LOG)"
