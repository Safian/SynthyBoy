# SynthyBoy - core tests (own runner, registered with CTest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Threads REQUIRED)

# Also used by core/engine-vital/tests (the Vital-specific tests), which link the Vital library as well:
#   synthyboy_add_test(name sources... [LINK libs...])
function(synthyboy_add_test name)
    cmake_parse_arguments(ARG "" "" "LINK" ${ARGN})
    add_executable(${name} ${ARG_UNPARSED_ARGUMENTS} "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_link_libraries(${name} PRIVATE synthyboy_core Threads::Threads ${ARG_LINK})
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/core/src" "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/shell-clap" "${CLAP_INCLUDE_DIR}")
    set_target_properties(${name} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

synthyboy_add_test(synthyboy_test_params     test_params.cpp)
synthyboy_add_test(synthyboy_test_state      test_state.cpp)
synthyboy_add_test(synthyboy_test_keys       test_new_keys.cpp)
synthyboy_add_test(synthyboy_test_caps       test_engine_caps.cpp)
synthyboy_add_test(synthyboy_test_state_v3   test_state_v3.cpp)
synthyboy_add_test(synthyboy_test_state_v4   test_state_v4.cpp)
synthyboy_add_test(synthyboy_test_fxextra_keys test_fxextra_keys.cpp)
synthyboy_add_test(synthyboy_test_fxinstance_keys test_fxinstance_keys.cpp)
synthyboy_add_test(synthyboy_test_fxrack test_fxrack.cpp)
synthyboy_add_test(synthyboy_test_events     test_events.cpp)
synthyboy_add_test(synthyboy_test_shell_features test_shell_features.cpp)
synthyboy_add_test(synthyboy_test_preferences test_preferences.cpp)
synthyboy_add_test(synthyboy_test_preset     test_preset.cpp)
synthyboy_add_test(synthyboy_test_shell_clap test_shell_clap.cpp LINK synthyboy_static)
synthyboy_add_test(synthyboy_test_engine     test_engine.cpp)
synthyboy_add_test(synthyboy_test_own_engine test_own_engine.cpp)
target_include_directories(synthyboy_test_own_engine PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own" "${PROJECT_SOURCE_DIR}/tools/own-oracle")
synthyboy_add_test(synthyboy_test_own_voice test_own_voice.cpp)
target_include_directories(synthyboy_test_own_voice PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own" "${PROJECT_SOURCE_DIR}/tools/own-oracle")
synthyboy_add_test(synthyboy_test_own_fx test_own_fx.cpp)
target_include_directories(synthyboy_test_own_fx PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own" "${PROJECT_SOURCE_DIR}/tools/own-oracle")
synthyboy_add_test(synthyboy_test_own_osc_types test_own_osc_types.cpp)
target_include_directories(synthyboy_test_own_osc_types PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_unison_cap test_own_unison_cap.cpp)
target_include_directories(synthyboy_test_own_unison_cap PRIVATE "${PROJECT_SOURCE_DIR}/bench")
synthyboy_add_test(synthyboy_test_own_pad_pressure test_own_pad_pressure.cpp)
target_include_directories(synthyboy_test_own_pad_pressure PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_rt         test_rt_alloc.cpp)   # replaces global operator new/delete
synthyboy_add_test(synthyboy_test_threads    test_threads.cpp)    # run under ThreadSanitizer in CI

# Fuzz smoke (garbage / mutated documents into every parser, fixed seeds, bounded; part of the sanitizer jobs). Seeds: the factory presets
# and the Vital fixtures of the importer's tests. SYNTHYBOY_FUZZ_SCALE=N in the environment multiplies the iterations.
synthyboy_add_test(synthyboy_fuzz_smoke fuzz_smoke.cpp "${PROJECT_SOURCE_DIR}/core/vital-import/tests/fixtures.cpp"
    LINK synthyboy_fx_serum_b synthyboy_sampleosc)
target_include_directories(synthyboy_fuzz_smoke PRIVATE "${PROJECT_SOURCE_DIR}/core/vital-import" "${PROJECT_SOURCE_DIR}/core/vital-import/tests"
    "${PROJECT_SOURCE_DIR}/core/fx-serum-b")
target_compile_definitions(synthyboy_fuzz_smoke PRIVATE SYNTHYBOY_FUZZ_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# The user content library of the shells (WP-01): store, references, Missing Files logic, library, Vital import inside the plugin (synthetic fixtures)
synthyboy_add_test(synthyboy_test_user_content test_user_content.cpp "${PROJECT_SOURCE_DIR}/core/vital-import/tests/fixtures.cpp")
target_include_directories(synthyboy_test_user_content PRIVATE "${PROJECT_SOURCE_DIR}/core/vital-import" "${PROJECT_SOURCE_DIR}/core/vital-import/tests")
synthyboy_add_test(synthyboy_test_clap_content test_clap_content.cpp LINK synthyboy_static ${CMAKE_DL_LIBS})
# GROUPD (WP-14 / 15 / 16): sample -> wavetable, frame <-> LFO shape, WAV of a render, the offline render driver on the own engine
synthyboy_add_test(synthyboy_test_wave_convert test_wave_convert.cpp)
