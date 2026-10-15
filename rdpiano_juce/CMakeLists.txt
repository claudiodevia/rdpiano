# El plugin, con la API CMake de JUCE.
#
# El bundle id (`com.GiulioZausa.rdpiano`) y los códigos de fabricante y de
# plugin no cambian: con ellos reconoce el anfitrión las sesiones guardadas
# (trampa 11 de CLAUDE.md).
#
# El núcleo no se recompila aquí: se enlaza el target `librdpiano` que construye
# `librdpiano/CMakeLists.txt`. Añadir un `.cpp` al núcleo no obliga a tocar nada
# de este directorio.

# Objetivo de despliegue: macOS 11 (Big Sur), el suelo del binario universal
# (la primera versión que corre en Apple Silicon) y por encima del mínimo de
# JUCE 9 (10.11). Desde 14.4 JUCE capturaría ventanas con ScreenCaptureKit y el
# bundle dejaría fuera los macOS anteriores.
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
  # El binario se llama rdpiano_juce y el plugin se anuncia como RdPiano.
  PRODUCT_NAME              "rdpiano_juce"
  PLUGIN_NAME               "RdPiano"
  # Versión oficial del plugin, y la única que hay: de aquí salen
  # CFBundleShortVersionString/CFBundleVersion de los cuatro bundles, las
  # versiones del .ttl del LV2 y las macros JucePlugin_Version*. Explícita a
  # propósito: si se omite, JUCE la hereda del `project()` que toque en ese
  # momento (hoy el de la raíz), y cambiaría sin que nadie lo pidiera.
  VERSION                   "1.0.3"
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
  Source/PanelButton.cpp
  Source/PanelDisplay.cpp
  Source/PanelLookAndFeel.cpp
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/PluginParams.cpp
  Source/PluginState.cpp
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
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)

target_compile_features(rdpiano_juce PRIVATE cxx_std_23)

# Dead-stripping en el enlazado de los cinco formatos: ni Xcode ni CMake lo
# activan por su cuenta, y sin él el bundle se lleva entero el código de los
# módulos JUCE que nadie llama (el ejecutable pasa de 15 MB a 30 MB). Los puntos
# de entrada de cada formato son símbolos exportados, que el enlazador trata
# como raíces, así que no se los lleva.
foreach(fmt IN ITEMS AU AUv3 LV2 Standalone VST3)
  if(TARGET rdpiano_juce_${fmt})
    target_link_options(rdpiano_juce_${fmt} PRIVATE -Wl,-dead_strip)
  endif()
endforeach()

# Las pruebas del plugin sin abrir un DAW: una app de consola que enlaza el
# mismo código compartido que los cinco formatos e instancia el AudioProcessor
# de verdad.
juce_add_console_app(rdpiano_plugin_tests
  PRODUCT_NAME "rdpiano_plugin_tests")

juce_generate_juce_header(rdpiano_plugin_tests)

target_sources(rdpiano_plugin_tests PRIVATE
  ../librdpiano/test/test_runner.cpp
  test/main.cpp
  test/test_panel_display.cpp
  test/test_plugin_state.cpp)

target_include_directories(rdpiano_plugin_tests PRIVATE
  ../librdpiano/test ../librdpiano/test/unit Source)

target_link_libraries(rdpiano_plugin_tests PRIVATE rdpiano_juce)

target_compile_features(rdpiano_plugin_tests PRIVATE cxx_std_23)

add_test(NAME plugin COMMAND rdpiano_plugin_tests)
