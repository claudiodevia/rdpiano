#!/bin/bash
# Descarga JUCE en build/juce.
#
# Todo lo que este proyecto genera —descargas incluidas— vive bajo `build/`:
# `build/juce` (esto), `build/plugin` (el build de la raíz, con los artefactos
# del plugin) y `build/core`, `build/core-asan` (los del núcleo suelto). Así
# `rm -rf build` deja el árbol como recién clonado, y borrar sólo los binary
# dirs (`rm -rf build/plugin build/core*`) no obliga a volver a bajar 162 MB.
#
# Desde la fase 3 el plugin se construye con la API CMake de JUCE
# (juce_add_plugin), asi que lo que hace falta del zip es el arbol de fuentes
# completo: juce/CMakeLists.txt, juce/modules y juce/extras/Build, de donde sale
# juceaide. El Projucer que trae el zip ya no se usa.
set -e

VERSION=9.0.1
ROOT=$(cd "$(dirname "$0")/.."; pwd)
DEST="$ROOT/build"
JUCE_DIR="$DEST/juce"

mkdir -p "$DEST"
ZIP="$DEST/juce-$VERSION-osx.zip"
TMP="$DEST/.juce-unzip"
trap 'rm -rf "$ZIP" "$TMP"' EXIT

echo "Descargando JUCE $VERSION (osx)..."
curl -fL "https://github.com/juce-framework/JUCE/releases/download/$VERSION/juce-$VERSION-osx.zip" -o "$ZIP"

# El zip tiene JUCE/ como raiz. Se descomprime aparte y se mueve, en vez de
# renombrar en sitio: en un volumen sensible a mayusculas `JUCE` y `juce` no
# son el mismo directorio.
rm -rf "$JUCE_DIR" "$TMP"
unzip -qo "$ZIP" -d "$TMP"
mv "$TMP/JUCE" "$JUCE_DIR"

echo "Listo: JUCE en $JUCE_DIR (modulos en $JUCE_DIR/modules)"
