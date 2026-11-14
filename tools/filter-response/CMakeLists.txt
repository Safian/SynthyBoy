# SynthyBoy - filter-response: prints the frequency response of filter 1 / 2 of a preset as CSV (frequency, dB), measured on the real filter code of the own engine.
# Added by the top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS is ON.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_filter_response filter_response_tool.cpp)
target_link_libraries(synthyboy_filter_response PRIVATE synthyboy_core)
set_target_properties(synthyboy_filter_response PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_filter_response PRIVATE /W4)
else()
    target_compile_options(synthyboy_filter_response PRIVATE -Wall -Wextra -Wpedantic)
endif()

# smoke test: a factory preset gives a curve with the requested number of points
add_test(NAME synthyboy_filter_response_cli
         COMMAND synthyboy_filter_response "${PROJECT_SOURCE_DIR}/content/presets/Bass/warm_analog_bass.sbpreset" --points 64)
set_tests_properties(synthyboy_filter_response_cli PROPERTIES PASS_REGULAR_EXPRESSION "\n20\\.00,")
