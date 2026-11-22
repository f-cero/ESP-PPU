# ---------------------------------------------------------------------------
# PPU-16 — host build (tests + examples).
#
#   cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build && ctest --test-dir build
#
# The same sources build as an ESP-IDF component: see idf_component/.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(ppu16 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(ppu16 STATIC
    src/ppu.c
    src/bg.c
    src/affine.c
    src/sprite.c
    src/link.c
    src/link_gpu.c
    src/hal.c
)
target_include_directories(ppu16 PUBLIC include)
target_compile_options(ppu16 PRIVATE -Wall -Wextra -Wno-unused-parameter)

find_package(Threads)

add_executable(ppu16_tests
    tests/test_main.c
    tests/test_color.c
    tests/test_bg.c
    tests/test_sprite.c
    tests/test_affine.c
    tests/test_render.c
    tests/test_link.c
)
target_link_libraries(ppu16_tests PRIVATE ppu16 m)
target_compile_options(ppu16_tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME ppu16_tests COMMAND ppu16_tests)

add_executable(mode7_demo examples/host/mode7_demo.c)
target_link_libraries(mode7_demo PRIVATE ppu16 m)
target_compile_options(mode7_demo PRIVATE -Wall -Wextra)
