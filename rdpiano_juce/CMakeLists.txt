# El plugin, con la API CMake de JUCE (REFACTORIZACION §16.3, fase 3).
#
# Sustituye a `rdpiano_juce.jucer` + Projucer. Los valores de aquí son los que
# tenía el `.jucer`, uno a uno: el `.vst3`, el `.component`, el `.app` y el
# `.appex` que salen de este fichero llevan el mismo nombre, el mismo bundle id
# (`com.GiulioZausa.rdpiano`) y los mismos códigos de fabricante y de plugin, de
# forma que un host que ya tenga sesiones guardadas los siga reconociendo.
#
# Lo que cambia es de dónde sale el núcleo: el `.jucer` listaba las fuentes de
# `librdpiano/src/` una por una y las recompilaba; aquí se enlaza el target
# `librdpiano` que construye `librdpiano/CMakeLists.txt`. Añadir un `.cpp` al
# núcleo ya no obliga a tocar nada de este directorio.

# Objetivo de despliegue: macOS 11 (Big Sur).
#
# Hasta JUCE 8.0.1 estaba clavado en 10.13 —el que ponía el Projucer— y no era
# cosmético: aquel JUCE llamaba a `CGWindowListCreateImage` sin más, y el SDK
# de macOS 15+ lo marca como no disponible, así que con el objetivo del
# anfitrión `juce_gui_basics` ni compilaba. JUCE 9 lo arregla: usa
# ScreenCaptureKit cuando el objetivo es >= 14.4 y, por debajo, envuelve la
# llamada vieja en `JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS`
# (`juce_Windowing_mac.mm`). Ya no hay nada que forzar hacia abajo.
#
# 11.0 es el suelo del binario universal: la primera versión que corre en
# Apple Silicon, y por encima del mínimo que declara JUCE 9 (10.11). Subir a
# 14.4 o más metería ScreenCaptureKit en el bundle y dejaría fuera a medio
# mundo, que en un plugin de audio no compensa.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# `juceaide` (el generador de BinaryData, plists e iconos) se compila en una
# invocación anidada de CMake que no hereda ninguna variable de caché: la única
# vía para darle el mismo objetivo de despliegue es el entorno, de donde CMake
# inicializa CMAKE_OSX_DEPLOYMENT_TARGET.
set(ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")

# JUCE trae su propio árbol CMake; `download-juce.sh` lo deja en build/juce
# (RDPIANO_JUCE_DIR, definido en el CMakeLists de la raíz). Como está fuera de
# este directorio, hay que darle binary dir explícito.
add_subdirectory(${RDPIANO_JUCE_DIR} juce EXCLUDE_FROM_ALL)

juce_add_plugin(rdpiano_juce
  # El binario se llama rdpiano_juce (era `targetName` en el .jucer) y el
  # plugin se anuncia como RdPiano (era `pluginName`). No son lo mismo y el
  # .jucer también los distinguía.
  PRODUCT_NAME              "rdpiano_juce"
  PLUGIN_NAME               "RdPiano"
  # Versión oficial del plugin, y la única que hay: de aquí salen
  # CFBundleShortVersionString/CFBundleVersion de los cuatro bundles, las
  # versiones del .ttl del LV2 y las macros JucePlugin_Version*. Explícita a
  # propósito: si se omite, JUCE la hereda del `project()` que toque en ese
  # momento (hoy el de la raíz), y cambiaría sin que nadie lo pidiera.
  VERSION                   "1.0.0"
  DESCRIPTION               "RdPiano"
  COMPANY_NAME              "Giulio Zausa"
  BUNDLE_ID                 "com.GiulioZausa.rdpiano"
  PLUGIN_MANUFACTURER_CODE  GlZs
  PLUGIN_CODE               RDPN
  IS_SYNTH                  TRUE
  NEEDS_MIDI_INPUT          TRUE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD   FALSE
  LV2URI                    "https://www.rdpiano.com/plugins/rdpiano"
  VST3_CATEGORIES           Instrument
  AU_MAIN_TYPE              kAudioUnitType_MusicDevice
  FORMATS                   AU AUv3 LV2 Standalone VST3)

# Las ROMs y los assets del panel, empotrados. Los nombres de fichero son los
# de `patches.h` (`romSetFiles`, `PROG_ROM_FILE`) y `test_patches.cpp`
# comprueba contra este fichero que las dos listas no se separen; los símbolos
# que genera `juceaide` —BinaryData::mks20_15179738_BIN y compañía— son los
# mismos que generaba el Projucer.
juce_add_binary_data(rdpiano_resources
  SOURCES
    ../ui/alphadial.png
    ../ui/background.png
    ../ui/interactable.png
    ../roms/MK80_IC5.bin
    ../roms/MK80_IC6.bin
    ../roms/MK80_IC7.bin
    ../roms/MK80_IC18.bin
    ../roms/mks20_15179736.BIN
    ../roms/mks20_15179737.BIN
    ../roms/mks20_15179738.BIN
    ../roms/mks20_15179739.BIN
    ../roms/mks20_15179740.BIN
    ../roms/mks20_15179741.BIN
    ../roms/mks20_15179757.BIN
    ../roms/RD200_B.bin)

juce_generate_juce_header(rdpiano_juce)

target_sources(rdpiano_juce PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/PluginParams.cpp
  Source/lcd/Lcd.cpp)

target_compile_definitions(rdpiano_juce PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_STRICT_REFCOUNTEDPOINTER=1)

# PUBLIC y no PRIVATE, que es lo habitual en los ejemplos de JUCE: la app de
# consola de las pruebas enlaza este mismo código compartido y necesita heredar
# los módulos. Volverlos a enlazar por su cuenta duplicaría los objetos.
target_link_libraries(rdpiano_juce
  PUBLIC
    rdpiano_resources
    librdpiano
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)

target_compile_features(rdpiano_juce PRIVATE cxx_std_23)

# Dead-stripping en el enlazado de los cinco formatos. El Projucer lo ponía
# (`DEAD_CODE_STRIPPING = YES`) y ni Xcode ni CMake lo activan por su cuenta:
# sin él, el bundle se lleva entero el código de los módulos JUCE que nadie
# llama —el ejecutable pasa de 15 MB a 30 MB, con __text triplicado y cuatro
# veces más símbolos—. Los puntos de entrada de cada formato son símbolos
# exportados, que el enlazador trata como raíces, así que no se los lleva.
foreach(fmt IN ITEMS AU AUv3 LV2 Standalone VST3)
  if(TARGET rdpiano_juce_${fmt})
    target_link_options(rdpiano_juce_${fmt} PRIVATE -Wl,-dead_strip)
  endif()
endforeach()

# Ida y vuelta de presets sin abrir un DAW (REFACTORIZACION §17.7). Es una app
# de consola que enlaza el mismo código compartido que los cinco formatos:
# instancia el AudioProcessor de verdad y le pide getStateInformation /
# setStateInformation. Sólo es posible desde que el plugin se construye con
# CMake — con el Projucer no había forma de compilar el procesador sin envolverlo
# en un formato de plugin.
juce_add_console_app(rdpiano_plugin_tests
  PRODUCT_NAME "rdpiano_plugin_tests")

juce_generate_juce_header(rdpiano_plugin_tests)

target_sources(rdpiano_plugin_tests PRIVATE
  test/main.cpp
  test/test_plugin_state.cpp)

target_include_directories(rdpiano_plugin_tests PRIVATE
  ../librdpiano/test ../librdpiano/test/unit Source)

target_link_libraries(rdpiano_plugin_tests PRIVATE rdpiano_juce)

target_compile_features(rdpiano_plugin_tests PRIVATE cxx_std_23)

add_test(NAME plugin_state COMMAND rdpiano_plugin_tests)
