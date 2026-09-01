#!/bin/bash
# Compila el plugin desde el CMakeLists de la raíz. Desde la fase 3 no hay
# Projucer ni .jucer: el mismo sistema de build construye el núcleo, sus
# pruebas y el plugin (REFACTORIZACION §16.3).
#
# Recibe el formato a compilar como único argumento —AU, AUv3, LV2, Standalone,
# VST3 o ALL—, porque compilar los cinco es caro y casi nunca hace falta:
#
#   bash scripts/build-osx.sh AU     # sólo el .component
#   bash scripts/build-osx.sh ALL    # los cinco, lo que corre la CI
#
# Sin argumento no hace nada: no hay defecto a propósito, para que nadie se
# coma los cinco formatos por descuido.
#
# Generador Xcode a propósito: la API CMake de JUCE sólo crea el objetivo AUv3
# con ese generador, y el .jucer sí producía un .appex.
set -e

ROOT=$(cd "$(dirname "$0")/.."; pwd)
BUILD="$ROOT/build/plugin"

usage()
{
  cat >&2 <<'USAGE'
uso: build-osx.sh <FORMATO>

  AU          Audio Unit (.component)
  AUv3        Audio Unit v3 (.appex)
  LV2         LV2 (rdpiano_juce.lv2)
  Standalone  aplicación suelta (.app)
  VST3        VST3 (.vst3)
  ALL         los cinco formatos

El nombre no distingue mayúsculas de minúsculas (au, Vst3, all...).
USAGE
  exit 1
}

if [ $# -ne 1 ]; then
  usage
fi

# Un solo sitio donde traducir el argumento al objetivo de CMake: los que crea
# juce_add_plugin son rdpiano_juce_<FORMATO> más el agregado rdpiano_juce_All.
case $(echo "$1" | tr '[:upper:]' '[:lower:]') in
  au)         FORMAT=AU ;;
  auv3)       FORMAT=AUv3 ;;
  lv2)        FORMAT=LV2 ;;
  standalone) FORMAT=Standalone ;;
  vst3)       FORMAT=VST3 ;;
  all)        FORMAT=All ;;
  *)
    echo "Formato desconocido: $1" >&2
    usage
    ;;
esac

if [ ! -f "$ROOT/build/juce/CMakeLists.txt" ]; then
  echo "JUCE no está en build/juce. Ejecuta scripts/download-juce.sh" >&2
  exit 1
fi

ARTEFACTS="$BUILD/rdpiano_juce/rdpiano_juce_artefacts/Release"

# Se borra el producto anterior antes de compilar: Xcode actualiza el bundle in
# situ y no limpia lo que sobra dentro, así que un .component/.vst3/.app viejo a
# medio regenerar es indistinguible de uno recién hecho al probarlo en un DAW.
if [ "$FORMAT" = All ]; then
  VIEJO="$ARTEFACTS"
else
  VIEJO="$ARTEFACTS/$FORMAT"
fi
if [ -e "$VIEJO" ]; then
  echo "Borrando el producto anterior: $VIEJO"
  rm -rf "$VIEJO"
fi

cmake -S "$ROOT" -B "$BUILD" -G Xcode \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

cmake --build "$BUILD" --config Release --target "rdpiano_juce_$FORMAT"

echo
if [ "$FORMAT" = All ]; then
  echo "Productos en $ARTEFACTS/"
else
  echo "Productos en $ARTEFACTS/$FORMAT/"
fi
