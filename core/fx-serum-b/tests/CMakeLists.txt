# SynthyBoy - core/fx-serum-b tests (registered with CTest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Threads REQUIRED)

synthyboy_add_test(synthyboy_test_fx_serum_b
    test_fft.cpp
    test_wav_reader.cpp
    test_bode.cpp
    test_convolve.cpp
    LINK synthyboy_fx_serum_b
)
target_include_directories(synthyboy_test_fx_serum_b PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
