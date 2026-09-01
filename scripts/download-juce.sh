#!/bin/sh
# Descarga JUCE en build/juce.
#
# Todo lo que este proyecto genera —descargas incluidas— vive bajo `build/`:
# `build/juce` (esto), `build/plugin` (el build de la raíz, con los artefactos
# del plugin) y `build/core`, `build/core-asan` (los del núcleo suelto). Así
# `rm -rf build` deja el árbol como recién clonado, y borrar sólo los binary
# dirs (`rm -rf build/plugin build/core*`) no obliga a volver a bajar 162 MB.
#
# Y por eso mismo no se baja dos veces: si `build/juce` ya es la versión que
# toca, el script no hace nada. `--forzar` lo baja igualmente (árbol a medias,
# tocado a mano...).
#
# Desde la fase 3 el plugin se construye con la API CMake de JUCE
# (juce_add_plugin), asi que lo que hace falta del zip es el arbol de fuentes
# completo: juce/CMakeLists.txt, juce/modules y juce/extras/Build, de donde sale
# juceaide. El Projucer que trae el zip ya no se usa.
#
# La salida de curl y unzip va a logs/download-juce-<fecha>-<hora>.log; por
# pantalla sólo pasan las etiquetas de cada paso. El andamiaje (colores, log,
# pasos, trap) está en common.sh, compartido con build-osx.sh.
set -e

VERSION=9.0.1
ROOT=$(cd "$(dirname "$0")/.."; pwd)
. "$ROOT/scripts/common.sh"

DEST="$ROOT/build"
JUCE_DIR="$DEST/juce"
ZIP="$DEST/juce-$VERSION-osx.zip"
TMP="$DEST/.juce-unzip"

FORZAR=
case ${1:-} in
    "")                 ;;
    -f|--forzar|--force) FORZAR=1 ;;
    *) fatal "uso: download-juce.sh [--forzar]" ;;
esac

# El árbol ya dice qué versión es —`project(JUCE VERSION x.y.z)` en su raíz—,
# así que no hace falta sello aparte para saber si sirve el que hay.
juce_ya_esta()
{
    grep -q "project(JUCE VERSION $VERSION" "$JUCE_DIR/CMakeLists.txt" 2>/dev/null
}

if [ -z "$FORZAR" ] && juce_ya_esta; then
    printf '%sJUCE %s ya está descargado%s %s(--forzar para volver a bajarlo)%s\n' \
           "$C_FUERTE" "$VERSION" "$C_OFF" "$C_TENUE" "$C_OFF"
    ruta "JUCE en   " "$JUCE_DIR"
    exit 0
fi

mkdir -p "$DEST"

# La llama el trap de common.sh al salir, haya fallado o no.
limpiar()
{
    rm -rf "$ZIP" "$TMP"
}

log_abrir download-juce
titulo "JUCE $VERSION (osx)"

paso "Descargando el zip (162 MB)"
cmd curl -fL --silent --show-error --retry 3 --retry-delay 2 \
    "https://github.com/juce-framework/JUCE/releases/download/$VERSION/juce-$VERSION-osx.zip" \
    -o "$ZIP"

# El zip tiene JUCE/ como raiz. Se descomprime aparte y se mueve, en vez de
# renombrar en sitio: en un volumen sensible a mayusculas `JUCE` y `juce` no
# son el mismo directorio.
paso "Descomprimiendo en $JUCE_DIR"
rm -rf "$JUCE_DIR" "$TMP"
cmd unzip -qo "$ZIP" -d "$TMP"
mv "$TMP/JUCE" "$JUCE_DIR"

juce_ya_esta || fatal "el zip descomprimido no es JUCE $VERSION"

fin "Listo en $(transcurrido)s." "JUCE descargado"
ruta "JUCE en   " "$JUCE_DIR"
ruta "Modulos en" "$JUCE_DIR/modules"
