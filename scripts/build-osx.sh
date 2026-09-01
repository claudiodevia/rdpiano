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
# Siempre se compila universal (arm64 y x86_64) en build/plugin. Hubo un modo
# `nativo`, que compilaba sólo la arquitectura de esta máquina: ahorraba tiempo
# de compilación y ninguno en ejecución —macOS carga una sola rebanada del
# universal, y es el mismo código con los mismos flags—, mientras que a cambio
# partía el binary dir en dos y daba un producto que no carga en un host bajo
# Rosetta. Se quitó; `universal` se sigue aceptando, y no hace nada, porque es
# lo único que hay.
#
#   sh scripts/build-osx.sh AU install
#
# Generador Xcode a propósito: la API CMake de JUCE sólo crea el objetivo AUv3
# con ese generador, y el .jucer sí producía un .appex.
#
# Con `install`, al terminar bien, los bundles se copian a los directorios del
# sistema (/Library/Audio/Plug-Ins/… y /Applications) reemplazando lo que
# hubiera: copiarlos a mano es justo el paso que se olvida, y el DAW se queda
# cargando el binario de ayer. Es opcional y no hay defecto —pide contraseña de
# administrador y pisa lo instalado—, así que sin la palabra no se toca nada
# fuera de build/ (la CI, por tanto, sólo compila).
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
${C_FUERTE}uso:${C_OFF} build-osx.sh <FORMATO> [install]

  ${C_FUERTE}AU${C_OFF}          Audio Unit (.component)
  ${C_FUERTE}AUv3${C_OFF}        Audio Unit v3 (.appex)
  ${C_FUERTE}LV2${C_OFF}         LV2 (rdpiano_juce.lv2)
  ${C_FUERTE}Standalone${C_OFF}  aplicación suelta (.app)
  ${C_FUERTE}VST3${C_OFF}        VST3 (.vst3)
  ${C_FUERTE}ALL${C_OFF}         los cinco formatos

  ${C_FUERTE}install${C_OFF}     además, instala en el sistema (contraseña de administrador):
              AU en /Library/Audio/Plug-Ins/Components, VST3 en .../VST3,
              LV2 en .../LV2 y Standalone en /Applications, reemplazando lo
              que hubiera. AUv3 no tiene destino propio: viaja empotrado en
              el .app del Standalone. Sin esta palabra sólo se compila.

Ningún nombre distingue mayúsculas de minúsculas (au, Vst3, all...). Los cinco
formatos salen universales (arm64 y x86_64), que es lo único que se compila:

  sh scripts/build-osx.sh AU install          compila el .component y lo instala
  sh scripts/build-osx.sh AU                  sólo compila
  sh scripts/build-osx.sh ALL install         los cinco formatos, instalados
USAGE
    exit 1
}

[ $# -ge 1 ] && [ $# -le 3 ] || uso

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

# El resto de argumentos: sólo la instalación, que no tiene defecto a propósito
# —pide contraseña y pisa lo que haya en /Library—, así que hay que nombrarla.
# `universal` se acepta y no hace nada: ya no hay otro modo que elegir.
ARCHS="arm64;x86_64"
BUILD="$ROOT/build/plugin"
INSTALAR=
shift
for ARG do
    case $(minusculas "$ARG") in
        install|instalar) INSTALAR=1 ;;
        universal)        ;;
        *)
            printf '%sArgumento desconocido:%s %s\n\n' "$C_ROJO" "$C_OFF" "$ARG" >&2
            uso
            ;;
    esac
done

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

# Instalación (sólo con `install`) en los directorios del sistema, no en
# ~/Library: hacen falta permisos de administrador, así que se pide la
# contraseña una sola vez —con sudo -v, antes de tocar nada— y las copias
# siguientes reutilizan esa credencial. Se copia con
# ditto, que conserva atributos extendidos y firma del bundle, sobre el destino
# ya borrado: actualizar un bundle in situ deja dentro restos del anterior.
#
# AUv3 no aparece aquí porque no tiene destino propio: juce_add_plugin lo
# empotra en el .app del Standalone (XCODE_EMBED_APP_EXTENSIONS), y se registra
# al copiar la aplicación a /Applications. Tampoco hay VST2 —JUCE 9 lo quitó—,
# de ahí que /Library/Audio/Plug-Ins/VST se quede como estaba.
destino_de()
{
    case $1 in
        AU)         echo /Library/Audio/Plug-Ins/Components ;;
        VST3)       echo /Library/Audio/Plug-Ins/VST3 ;;
        LV2)        echo /Library/Audio/Plug-Ins/LV2 ;;
        Standalone) echo /Applications ;;
        *)          echo '' ;;
    esac
}

instalar()
{
    DESTINO=$(destino_de "$1")
    for ORIGEN in "$ARTEFACTS/$1"/*; do
        [ -e "$ORIGEN" ] || continue
        NOMBRE=$(basename "$ORIGEN")
        paso "Instalando $NOMBRE en $DESTINO"
        cmd sudo mkdir -p "$DESTINO"
        cmd sudo rm -rf "$DESTINO/$NOMBRE"
        cmd sudo ditto "$ORIGEN" "$DESTINO/$NOMBRE"
        INSTALADOS="$INSTALADOS $DESTINO/$NOMBRE"
    done
}

if [ "$FORMAT" = All ]; then
    FORMATOS="AU AUv3 LV2 Standalone VST3"
else
    FORMATOS=$FORMAT
fi

INSTALADOS=
if [ -n "$INSTALAR" ]; then
    PENDIENTES=
    for F in $FORMATOS; do
        if [ -n "$(destino_de "$F")" ] && [ -d "$ARTEFACTS/$F" ]; then
            PENDIENTES="$PENDIENTES $F"
        fi
    done

    if [ -n "$PENDIENTES" ]; then
        paso "Instalando en el sistema (contraseña de administrador)"
        sudo -v || fatal "sin permisos de administrador no se puede instalar"
        for F in $PENDIENTES; do
            instalar "$F"
        done
    fi
fi

fin "Listo en $(transcurrido)s," "$(avisos) (log completo en $LOG)"
ruta "Productos en" "$PRODUCTOS/"
for P in $INSTALADOS; do
    ruta "Instalado en" "$P"
done
case "$INSTALAR: $FORMATOS " in
    1:*" AUv3 "*)
        printf '%sAUv3 va empotrado en rdpiano_juce.app; no tiene destino propio.%s\n' \
               "$C_TENUE" "$C_OFF"
        ;;
esac
