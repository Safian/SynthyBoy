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

# the curve of a factory preset: every line of the output is checked (point count, finite numbers, the axis from 20 Hz to 20 kHz)
set(SB_FR_PRESET "${PROJECT_SOURCE_DIR}/content/presets/Bass/warm_analog_bass.sbpreset")
add_test(NAME synthyboy_filter_response_cli
         COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:synthyboy_filter_response> -DPRESET=${SB_FR_PRESET} -DPOINTS=64
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/check_cli_output.cmake")

# bad arguments give exit code 3, the named message on stderr and no curve on stdout; a switched off filter gives 2
function(sb_fr_cli_exit name code errRegex)
    string(REPLACE ";" " " argText "${ARGN}")
    add_test(NAME synthyboy_filter_response_cli_${name}
             COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:synthyboy_filter_response> -DPRESET=${SB_FR_PRESET} -DEXPECT=${code} "-DERRRE=${errRegex}" "-DARGS=${argText}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/check_cli_exit.cmake")
endfunction()
sb_fr_cli_exit(fmin_above_fmax 3 "bad arguments" --fmin 5000 --fmax 100)
sb_fr_cli_exit(fmin_negative 3 "bad arguments" --fmin -10)
sb_fr_cli_exit(missing_value 3 "needs a value" --resonance)
sb_fr_cli_exit(not_a_number 3 "needs a number" --resonance abc)
sb_fr_cli_exit(nan_value 3 "needs a number" --cutoff nan)
sb_fr_cli_exit(bad_set 3 "needs KEY=VALUE" --set filter1.cutoff=abc)
sb_fr_cli_exit(bad_points 3 "bad arguments" --points 0)
sb_fr_cli_exit(filter_off 2 "" --filter 2)
