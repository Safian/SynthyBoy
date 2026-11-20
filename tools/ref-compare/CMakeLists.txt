# SynthyBoy - ref-compare: the render side of the reference comparison (docs/ui-design/REFCOMPARE-REPORT.md). Renders imported presets (.sbpreset) through the Vital-derived
# engine with the same note script, block size and sample rate as tools/serum-reference uses for the real Serum 2. The comparison itself is tools/ref-compare/compare.py.
# Added by the top level CMakeLists.txt when the tests and the Vital engine are part of the build.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_ref_render ref_render.cpp)
target_link_libraries(synthyboy_ref_render PRIVATE synthyboy_core)
set_target_properties(synthyboy_ref_render PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_ref_render PRIVATE /W4)
else()
    target_compile_options(synthyboy_ref_render PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The self test of the comparison (synthetic signals: an identical pair scores 100, the broken variants score clearly lower, the alignment, LUFS, f0). Skipped (77) without numpy / scipy.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME synthyboy_ref_compare_metrics COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_metrics.py")
    set_tests_properties(synthyboy_ref_compare_metrics PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
