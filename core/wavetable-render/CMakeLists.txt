# SynthyBoy - clean-room wavetable renderer (namespace synthyboy::wtrender), compiled into synthyboy_core.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
# Added by core/CMakeLists.txt (one add_subdirectory line, after the tests, because the test helper is defined there).

target_sources(synthyboy_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/wavetable_render.cpp")

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    synthyboy_add_test(synthyboy_test_wavetable_render tests/test_wavetable_render.cpp)
    # label `slow` (more than about 5 s alone: `ctest -LE slow` is the quick set, docs/dsp-knowledge/testing.md)
    set_tests_properties(synthyboy_test_wavetable_render PROPERTIES LABELS slow)
endif()
