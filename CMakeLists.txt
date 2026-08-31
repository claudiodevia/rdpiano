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
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target rdpiano_juce_All   # el plugin
#   ctest --test-dir build --output-on-failure      # las pruebas
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
option(RDPIANO_BUILD_PLUGIN "Compilar el plugin JUCE (necesita rdpiano_juce/JUCE)" ON)

if(RDPIANO_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rdpiano_juce/JUCE/CMakeLists.txt")
  add_subdirectory(rdpiano_juce)
elseif(RDPIANO_BUILD_PLUGIN)
  message(STATUS
    "JUCE no está en rdpiano_juce/JUCE: se omite el plugin. "
    "Ejecuta `bash rdpiano_juce/download-juce.sh` para compilarlo.")
endif()
