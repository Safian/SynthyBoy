# SynthyBoy - core/shapes: free-form shape, LFO, and envelope evaluators.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_shapes STATIC
    src/shape.cpp
    src/lfo.cpp
    src/envelope.cpp
    src/remap.cpp)

target_include_directories(synthyboy_shapes
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(synthyboy_shapes PUBLIC cxx_std_17)
set_target_properties(synthyboy_shapes PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(synthyboy_shapes PRIVATE /W4)
else()
    target_compile_options(synthyboy_shapes PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
