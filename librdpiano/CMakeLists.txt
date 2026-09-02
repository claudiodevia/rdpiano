cmake_minimum_required(VERSION 3.22)

# project name and version
project(RdPiano VERSION 1.0)

# set c++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED True)

# ASAN por defecto cuando se configura `librdpiano/` por su cuenta: es
# intencional para desarrollo. Apagarlo (-DRDPIANO_SANITIZE=OFF) hace que el
# harness e2e corra bastante más rápido.
#
# Desde la raíz del repositorio el defecto es OFF: ese build compila también el
# plugin, y un producto instrumentado con ASan no es lo que nadie quiere cargar
# en un DAW. `add_compile_options` es de directorio hacia abajo, así que lo que
# se enciende aquí no alcanza a `rdpiano_juce/` de todas formas; lo que cambia
# es a qué se enlaza `librdpiano`.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(RDPIANO_SANITIZE_DEFAULT ON)
else()
    set(RDPIANO_SANITIZE_DEFAULT OFF)
endif()

option(RDPIANO_SANITIZE "Build with AddressSanitizer" ${RDPIANO_SANITIZE_DEFAULT})
if (RDPIANO_SANITIZE)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-constant-logical-operand")

# Avisos encendidos para el núcleo y sus pruebas. `add_compile_options` es de
# directorio hacia abajo, así que esto no alcanza a `rdpiano_juce/`. Sin
# `-Werror`: eso es decisión de la CI, no del árbol local.
add_compile_options(-Wall -Wextra)

include_directories("include")

# library target
#
# Ademas del emulador, aqui viven los efectos (lsp/) y el resampler
# (resample/): son C/C++ puro sin nada de JUCE, y desde la fase 2 el motor de
# audio los usa (REFACTORIZACION §1). El plugin los compila desde aqui.
add_library(librdpiano src/mcu.cpp src/rd_board.cpp src/sound_chip.cpp src/sa_tables.cpp
    src/rom_loader.cpp src/command_port.cpp src/rd_trace.cpp
    src/lsp/spaced.cpp src/lsp/phaser.cpp
    src/rd_engine.cpp
    src/resample/resample.c src/resample/filterkit.c src/resample/resamplesubs.c)
# `include/` es PUBLIC: quien enlaza librdpiano —el plugin desde
# `rdpiano_juce/`, sin ir más lejos— no tiene por qué saber dónde vive el
# núcleo en el árbol.
target_include_directories(librdpiano PUBLIC include)
target_include_directories(librdpiano PRIVATE src src/resample)

# `resample/` es código de terceros exceptuado del estilo del árbol (trampa 5
# de CLAUDE.md): sus comparaciones con signo no se van a arreglar, y dejarlas
# avisando entierra los avisos que sí son accionables.
set_source_files_properties(src/resample/resample.c src/resample/filterkit.c
    src/resample/resamplesubs.c PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare")

# end-to-end test harness: sin dependencias externas, siempre se compila
add_executable(rdpiano_e2e test/e2e.cpp)
target_link_libraries(rdpiano_e2e librdpiano)
target_include_directories(rdpiano_e2e PRIVATE test)

# suite unitaria: sin emular audio, tiene que terminar en menos de un segundo.
# Lista explicita de fuentes, sin glob (REFACTORIZACION §17.2).
add_executable(rdpiano_tests
    test/unit/main.cpp
    test/unit/test_board.cpp
    test/unit/test_command_port.cpp
    test/unit/test_engine.cpp
    test/unit/test_lsp.cpp
    test/unit/test_patches.cpp
    test/unit/test_resampler.cpp
    test/unit/test_rom_loader.cpp
    test/unit/test_sa_tables.cpp
    test/unit/test_sound_chip_blocks.cpp)
target_link_libraries(rdpiano_tests librdpiano)
target_include_directories(rdpiano_tests PRIVATE test test/unit)

enable_testing()
add_test(NAME unit COMMAND rdpiano_tests --roms ${CMAKE_CURRENT_SOURCE_DIR}/../roms
                                       --vectors ${CMAKE_CURRENT_SOURCE_DIR}/test/vectors)
add_test(NAME e2e COMMAND rdpiano_e2e --roms ${CMAKE_CURRENT_SOURCE_DIR}/../roms
                                      --golden ${CMAKE_CURRENT_SOURCE_DIR}/test/golden.txt)

# SDL2 + portmidi son opcionales: solo hacen falta para la app interactiva
find_package(SDL2 QUIET)
find_path(PORTMIDI_INCLUDE_DIR portmidi.h)
find_library(PORTMIDI_LIBRARY portmidi)

if (SDL2_FOUND AND PORTMIDI_INCLUDE_DIR AND PORTMIDI_LIBRARY)
    add_executable(rdpiano_standalone test/standalone.cpp)
    target_include_directories(rdpiano_standalone
        PRIVATE test ${SDL2_INCLUDE_DIRS} ${PORTMIDI_INCLUDE_DIR})
    target_link_libraries(rdpiano_standalone
        ${SDL2_LIBRARIES} ${PORTMIDI_LIBRARY} librdpiano)
else()
    message(STATUS "SDL2/portmidi no encontrados: se omite rdpiano_standalone")
endif()
