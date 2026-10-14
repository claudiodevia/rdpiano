#!/bin/sh
# Los tres trabajos de prueba de la CI (.github/workflows/main.yml), que llama a
# este script con un modo por trabajo: la CI y la máquina local corren lo mismo.
#
#   sh scripts/verificar.sh          los tres, en orden
#   sh scripts/verificar.sh nucleo   núcleo sin ASan (unit + e2e); `rapido` es lo mismo
#   sh scripts/verificar.sh asan     núcleo con ASan
#   sh scripts/verificar.sh plugin   cinco formatos del plugin y sus pruebas
#
# Usa los mismos directorios que la CI (build/core, build/core-asan, build/plugin),
# así que lo ya compilado no se repite. Se detiene en el primer fallo y vuelca la
# cola del log. El modo plugin necesita JUCE en build/juce (download-juce.sh).
set -e

ROOT=$(cd "$(dirname "$0")/.."; pwd)
. "$ROOT/scripts/common.sh"

uso()
{
    cat >&2 <<USAGE
${C_FUERTE}uso:${C_OFF} verificar.sh [nucleo|rapido|asan|plugin]

  ${C_FUERTE}(nada)${C_OFF}          lo que corre la CI: los tres modos de abajo, en orden
  ${C_FUERTE}nucleo${C_OFF}, ${C_FUERTE}rapido${C_OFF}  ctest del núcleo sin ASan, para iterar
  ${C_FUERTE}asan${C_OFF}            ctest del núcleo con AddressSanitizer
  ${C_FUERTE}plugin${C_OFF}          los cinco formatos del plugin y sus pruebas
USAGE
    exit 1
}

NUCLEO= ASAN= PLUGIN=
case $# in
    0) NUCLEO=1 ASAN=1 PLUGIN=1 ;;
    1)
        case $(minusculas "$1") in
            nucleo|núcleo|rapido|rápido) NUCLEO=1 ;;
            asan)                        ASAN=1 ;;
            plugin)                      PLUGIN=1 ;;
            *) uso ;;
        esac
        ;;
    *) uso ;;
esac

# Configura librdpiano en $1 con ASan $2 y tipo de build $3, compila sus pruebas
# —y el ejecutable de escucha, si CMake encontró SDL2 y portmidi— y corre ctest;
# $4 es la etiqueta de los pasos.
probar_nucleo()
{
    paso "$4: configurando"
    cmd cmake -S "$ROOT/librdpiano" -B "$ROOT/$1" -DRDPIANO_SANITIZE="$2" -DCMAKE_BUILD_TYPE="$3"
    OBJETIVOS="rdpiano_tests rdpiano_e2e"
    if cmake --build "$ROOT/$1" --target help 2>/dev/null | grep -q rdpiano_standalone; then
        OBJETIVOS="$OBJETIVOS rdpiano_standalone"
    fi
    paso "$4: compilando $OBJETIVOS"
    # Sin comillas a propósito: es una lista de objetivos.
    # shellcheck disable=SC2086
    cmd cmake --build "$ROOT/$1" --parallel --target $OBJETIVOS
    paso "$4: ctest"
    cmd ctest --test-dir "$ROOT/$1" --output-on-failure
}

log_abrir verificar

if [ -n "$NUCLEO" ] && [ -n "$ASAN" ] && [ -n "$PLUGIN" ]; then
    titulo "Verificación completa: lo que corre la CI"
else
    titulo "Verificación: ${NUCLEO:+núcleo}${ASAN:+núcleo con ASan}${PLUGIN:+plugin}"
fi
[ -z "$PLUGIN" ] || exigir_juce "(o usa \`verificar.sh nucleo\`)"

[ -z "$NUCLEO" ] || probar_nucleo build/core OFF Release "Núcleo"
[ -z "$ASAN" ] || probar_nucleo build/core-asan ON RelWithDebInfo "Núcleo con ASan"

if [ -n "$PLUGIN" ]; then
    paso "Plugin: los cinco formatos (build-osx.sh ALL)"
    cmd sh "$ROOT/scripts/build-osx.sh" ALL
    paso "Plugin: compilando sus pruebas"
    cmd cmake --build "$ROOT/build/plugin" --config Release --target rdpiano_plugin_tests
    paso "Plugin: ctest"
    cmd ctest --test-dir "$ROOT/build/plugin" -C Release -R plugin --output-on-failure
fi

fin "Todo en verde en $(transcurrido)s," "$(avisos) (log completo en $LOG)"
