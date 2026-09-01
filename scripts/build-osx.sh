#!/bin/sh
# Compila el plugin desde el CMakeLists de la raíz. Desde la fase 3 no hay
# Projucer ni .jucer: el mismo sistema de build construye el núcleo, sus
# pruebas y el plugin (REFACTORIZACION §16.3).
#
# Recibe el formato a compilar como primer argumento —AU, AUv3, LV2, Standalone,
# VST3 o ALL—, porque compilar los cinco es caro y casi nunca hace falta:
#
#   sh scripts/build-osx.sh AU     # sólo el .component
#   sh scripts/build-osx.sh ALL    # los cinco, lo que corre la CI
#
# Sin argumento no hace nada: no hay defecto a propósito, para que nadie se
# coma los cinco formatos por descuido.
#
# El segundo argumento elige las arquitecturas. Por omisión `universal`
# (arm64;x86_64), que es lo que se publica; `nativo` compila sólo la de esta
# máquina y tarda la mitad, que para probar en el DAW de aquí sobra. Cada modo
# tiene su propio binary dir (build/plugin y build/plugin-nativo) para que
# alternar no invalide la caché del otro y obligue a recompilarlo entero.
#
# Generador Xcode a propósito: la API CMake de JUCE sólo crea el objetivo AUv3
# con ese generador, y el .jucer sí producía un .appex.
#
# La salida de CMake y de Xcode —miles de líneas— va entera a
# logs/build-osx-<fecha>-<hora>.log; por pantalla sólo pasan las etiquetas de
# cada paso. Si algo falla se vuelcan las últimas líneas del log. El andamiaje
# (colores, log, pasos, trap) está en common.sh, compartido con download-juce.sh.
set -e

ROOT=$(cd "$(dirname "$0")/.."; pwd)
. "$ROOT/scripts/common.sh"

uso()
{
    cat >&2 <<USAGE
${C_FUERTE}uso:${C_OFF} build-osx.sh <FORMATO> [universal|nativo]

  ${C_FUERTE}AU${C_OFF}          Audio Unit (.component)
  ${C_FUERTE}AUv3${C_OFF}        Audio Unit v3 (.appex)
  ${C_FUERTE}LV2${C_OFF}         LV2 (rdpiano_juce.lv2)
  ${C_FUERTE}Standalone${C_OFF}  aplicación suelta (.app)
  ${C_FUERTE}VST3${C_OFF}        VST3 (.vst3)
  ${C_FUERTE}ALL${C_OFF}         los cinco formatos

  ${C_FUERTE}universal${C_OFF}   arm64 y x86_64 (por omisión), en build/plugin
  ${C_FUERTE}nativo${C_OFF}      sólo $(uname -m), la mitad de tiempo, en build/plugin-nativo

Ningún nombre distingue mayúsculas de minúsculas (au, Vst3, all...).
USAGE
    exit 1
}

[ $# -ge 1 ] && [ $# -le 2 ] || uso

minusculas()
{
    echo "$1" | tr '[:upper:]' '[:lower:]'
}

# Un solo sitio donde traducir el argumento al objetivo de CMake: los que crea
# juce_add_plugin son rdpiano_juce_<FORMATO> más el agregado rdpiano_juce_All.
case $(minusculas "$1") in
    au)         FORMAT=AU ;;
    auv3)       FORMAT=AUv3 ;;
    lv2)        FORMAT=LV2 ;;
    standalone) FORMAT=Standalone ;;
    vst3)       FORMAT=VST3 ;;
    all)        FORMAT=All ;;
    *)
        printf '%sFormato desconocido:%s %s\n\n' "$C_ROJO" "$C_OFF" "$1" >&2
        uso
        ;;
esac

case $(minusculas "${2:-universal}") in
    universal)     ARCHS="arm64;x86_64"; BUILD="$ROOT/build/plugin" ;;
    nativo|native) ARCHS=$(uname -m);    BUILD="$ROOT/build/plugin-nativo" ;;
    *)
        printf '%sArquitectura desconocida:%s %s\n\n' "$C_ROJO" "$C_OFF" "$2" >&2
        uso
        ;;
esac

ARTEFACTS="$BUILD/rdpiano_juce/rdpiano_juce_artefacts/Release"

log_abrir build-osx
titulo "Formato: $FORMAT ($ARCHS)"

[ -f "$ROOT/build/juce/CMakeLists.txt" ] ||
    fatal "JUCE no está en build/juce. Ejecuta scripts/download-juce.sh"

# Se borra el producto anterior antes de compilar: Xcode actualiza el bundle in
# situ y no limpia lo que sobra dentro, así que un .component/.vst3/.app viejo a
# medio regenerar es indistinguible de uno recién hecho al probarlo en un DAW.
if [ "$FORMAT" = All ]; then
    PRODUCTOS="$ARTEFACTS"
else
    PRODUCTOS="$ARTEFACTS/$FORMAT"
fi
if [ -e "$PRODUCTOS" ]; then
    paso "Borrando el producto anterior: $PRODUCTOS"
    rm -rf "$PRODUCTOS"
fi

# Configurar cuesta (JUCE compila juceaide) y CMake ya regenera el proyecto
# Xcode él solo cuando cambia un CMakeLists, así que sólo se hace si la caché
# que hay no sirve: no existe, es de otro generador, es de otras arquitecturas
# o se configuró sin JUCE y por tanto sin plugin.
CACHE="$BUILD/CMakeCache.txt"
if [ -f "$CACHE" ] && [ -d "$BUILD/rdpiano_juce" ] &&
   grep -qx 'CMAKE_GENERATOR:INTERNAL=Xcode' "$CACHE" &&
   grep -qx "CMAKE_OSX_ARCHITECTURES:STRING=$ARCHS" "$CACHE"; then
    paso "CMake ya configurado en $BUILD"
else
    paso "Configurando CMake (Xcode, $ARCHS)"
    cmd cmake -S "$ROOT" -B "$BUILD" -G Xcode -DCMAKE_OSX_ARCHITECTURES="$ARCHS"
fi

paso "Compilando rdpiano_juce_$FORMAT (Release)"
cmd cmake --build "$BUILD" --config Release --target "rdpiano_juce_$FORMAT"

fin "Listo en $(transcurrido)s," "$(avisos) (log completo en $LOG)"
ruta "Productos en" "$PRODUCTOS/"
