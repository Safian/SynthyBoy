# SynthyBoy - core/fx-serum-b tests (registered with CTest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Threads REQUIRED)

# The tests of the Convolve optimisation (docs/plans/reverb-convolve.md): every test_conv_<x>.cpp joins the executable (a new task adds no line here).
file(GLOB SYNTHYBOY_CONV_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_conv_*.cpp")

synthyboy_add_test(synthyboy_test_fx_serum_b
    test_fft.cpp
    test_wav_reader.cpp
    test_bode.cpp
    test_convolve.cpp
    ${SYNTHYBOY_CONV_TESTS}
    LINK synthyboy_fx_serum_b
)
target_include_directories(synthyboy_test_fx_serum_b PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}")
