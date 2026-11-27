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
    src/mvid_util.c
    src/mvid_decode.c
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
    tests/test_mvid.c
)
target_link_libraries(ppu16_tests PRIVATE ppu16 m)
target_compile_options(ppu16_tests PRIVATE -Wall -Wextra)
target_compile_definitions(ppu16_tests PRIVATE PPU16_SRC_DIR="${CMAKE_SOURCE_DIR}" MVID_HAVE_FILEIO=1)

enable_testing()
add_test(NAME ppu16_tests COMMAND ppu16_tests)

add_executable(mode7_demo examples/host/mode7_demo.c)
target_link_libraries(mode7_demo PRIVATE ppu16 m)
target_compile_options(mode7_demo PRIVATE -Wall -Wextra)

# MVID clip player demo (host only; uses mvid_read_file).
add_executable(mvid_demo examples/host/mvid_demo.c)
target_link_libraries(mvid_demo PRIVATE ppu16)
target_compile_options(mvid_demo PRIVATE -Wall -Wextra)
target_compile_definitions(mvid_demo PRIVATE MVID_HAVE_FILEIO=1)

# Host-only MVID encoder (authoring clips; never compiled into firmware).
add_library(mvid_encoder STATIC tools/mvid_encode.c)
target_include_directories(mvid_encoder PUBLIC include)
target_compile_options(mvid_encoder PRIVATE -Wall -Wextra)
