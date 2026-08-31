#!/bin/bash
# Descarga JUCE en rdpiano_juce/JUCE.
#
# Desde la fase 3 el plugin se construye con la API CMake de JUCE
# (juce_add_plugin), asi que lo que hace falta del zip es el arbol de fuentes
# completo: JUCE/CMakeLists.txt, JUCE/modules y JUCE/extras/Build, de donde sale
# juceaide. El Projucer que trae el zip ya no se usa.
set -e

VERSION=8.0.1
ROOT=$(cd "$(dirname "$0")"; pwd)

ZIP="$ROOT/juce-$VERSION-osx.zip"
trap 'rm -f "$ZIP"' EXIT

echo "Descargando JUCE $VERSION (osx)..."
curl -fL "https://github.com/juce-framework/JUCE/releases/download/$VERSION/juce-$VERSION-osx.zip" -o "$ZIP"

# El zip tiene JUCE/ como raiz, asi que descomprimir en $ROOT deja $ROOT/JUCE.
rm -rf "$ROOT/JUCE"
unzip -qo "$ZIP" -d "$ROOT"

echo "Listo: JUCE en $ROOT/JUCE (modulos en $ROOT/JUCE/modules)"
