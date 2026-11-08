# SynthyBoy - core/shapes tests (registered with CTest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Threads REQUIRED)

add_executable(synthyboy_test_shapes
    test_shape.cpp
    test_lfo.cpp
    test_envelope.cpp
    test_remap.cpp
    "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")

target_link_libraries(synthyboy_test_shapes PRIVATE synthyboy_shapes Threads::Threads)
target_include_directories(synthyboy_test_shapes PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${PROJECT_SOURCE_DIR}/core/tests")

set_target_properties(synthyboy_test_shapes PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_test_shapes PRIVATE /W4)
else()
    target_compile_options(synthyboy_test_shapes PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME synthyboy_test_shapes COMMAND synthyboy_test_shapes)
