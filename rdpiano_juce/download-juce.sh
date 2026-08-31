#!/bin/bash
# Descarga JUCE (modules + Projucer precompilado) en rdpiano_juce/JUCE.
#
# El zip de release trae el arbol completo: JUCE/modules (que es donde el
# .jucer busca los modulos, ver MODULEPATH) y JUCE/Projucer.app, que es lo que
# usa build-osx.sh. Por eso basta con una sola descarga.
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

echo "Listo: modulos en $ROOT/JUCE/modules, Projucer en $ROOT/JUCE"
