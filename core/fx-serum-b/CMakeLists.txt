# SynthyBoy - core/fx-serum-b: Serum-exclusive FX modules (Bode and Convolve).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_fx_serum_b STATIC
    fft.cpp
    wav_reader.cpp
    bode.cpp
    convolve.cpp
)

target_include_directories(synthyboy_fx_serum_b
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(synthyboy_fx_serum_b PUBLIC cxx_std_17)
set_target_properties(synthyboy_fx_serum_b PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(synthyboy_fx_serum_b PRIVATE /W4)
else()
    target_compile_options(synthyboy_fx_serum_b PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
