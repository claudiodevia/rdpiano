cmake_minimum_required(VERSION 3.22)

# Un solo sistema de build (REFACTORIZACION §16.3, fase 3).
#
# Hasta la fase 2 había dos sin relación: `librdpiano/CMakeLists.txt` para el
# núcleo y sus pruebas, y `rdpiano_juce.jucer` → Projucer → Xcode para el
# plugin. Los dos compilaban las mismas fuentes por caminos distintos y con
# flags distintos, y añadir un `.cpp` al núcleo obligaba a editar un XML con
# `id` inventados a mano. Desde aquí se construye todo: el núcleo, sus pruebas
# y el plugin, que enlaza `librdpiano` en vez de recompilarla.
#
#   cmake -B build/plugin -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/plugin --target rdpiano_juce_All   # el plugin
#   ctest --test-dir build/plugin --output-on-failure      # las pruebas
#
# El binary dir cuelga de `build/` como todo lo generado —incluido el propio
# JUCE, en `build/juce`—, para que `rm -rf build` lo limpie de una vez.
#
# `librdpiano/` sigue siendo configurable por su cuenta (`cmake -S librdpiano`)
# y es lo que hace la CI del núcleo: es más rápido y no necesita JUCE.

project(RdPiano VERSION 1.0)

# El plugin es un bundle: todo lo que enlaza dentro tiene que ser PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

enable_testing()

add_subdirectory(librdpiano)

# Sin JUCE descargado no hay plugin, pero el núcleo y sus pruebas siguen
# construyéndose: `download-juce.sh` es un paso aparte y no todo el mundo lo da.
option(RDPIANO_BUILD_PLUGIN "Compilar el plugin JUCE (necesita build/juce)" ON)

# JUCE no es fuente del proyecto: es una descarga, y por eso vive con el resto
# de lo generado bajo `build/` (ver `scripts/download-juce.sh`). La ruta es una
# variable de caché para poder apuntar a un JUCE ya instalado en la máquina.
set(RDPIANO_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/juce" CACHE PATH
    "Árbol de fuentes de JUCE 8.0.1")

if(RDPIANO_BUILD_PLUGIN AND EXISTS "${RDPIANO_JUCE_DIR}/CMakeLists.txt")
  add_subdirectory(rdpiano_juce)
elseif(RDPIANO_BUILD_PLUGIN)
  message(STATUS
    "JUCE no está en ${RDPIANO_JUCE_DIR}: se omite el plugin. "
    "Ejecuta `bash scripts/download-juce.sh` para compilarlo.")
endif()
