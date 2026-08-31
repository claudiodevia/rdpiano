#!/bin/bash
# Compila el plugin (VST3/AU/AUv3/LV2/Standalone) desde el CMakeLists de la
# raíz. Desde la fase 3 no hay Projucer ni .jucer: el mismo sistema de build
# construye el núcleo, sus pruebas y el plugin (REFACTORIZACION §16.3).
#
# Generador Xcode a propósito: la API CMake de JUCE sólo crea el objetivo AUv3
# con ese generador, y el .jucer sí producía un .appex.
set -e

ROOT=$(cd "$(dirname "$0")/../.."; pwd)
BUILD="$ROOT/build"

if [ ! -f "$ROOT/rdpiano_juce/JUCE/CMakeLists.txt" ]; then
  echo "JUCE no está en rdpiano_juce/JUCE. Ejecuta rdpiano_juce/download-juce.sh" >&2
  exit 1
fi

cmake -S "$ROOT" -B "$BUILD" -G Xcode \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

cmake --build "$BUILD" --config Release --target rdpiano_juce_All

echo
echo "Productos en $BUILD/rdpiano_juce/rdpiano_juce_artefacts/Release/"
