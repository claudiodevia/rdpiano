cmake_minimum_required(VERSION 3.10)

# project name and version
project(RdPiano VERSION 1.0)

# set c++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ASAN por defecto: es intencional para desarrollo. Apagarlo (-DRDPIANO_SANITIZE=OFF)
# hace que el harness e2e corra bastante más rápido.
option(RDPIANO_SANITIZE "Build with AddressSanitizer" ON)
if (RDPIANO_SANITIZE)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-constant-logical-operand")

include_directories("include")

# library target
add_library(librdpiano src/mcu.cpp src/sound_chip.cpp)
target_include_directories(librdpiano PRIVATE src)

# end-to-end test harness: sin dependencias externas, siempre se compila
add_executable(rdpiano_e2e test/e2e.cpp)
target_link_libraries(rdpiano_e2e librdpiano)
target_include_directories(rdpiano_e2e PRIVATE test)

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
