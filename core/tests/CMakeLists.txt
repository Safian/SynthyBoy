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
synthyboy_add_test(synthyboy_test_macro_names test_macro_names.cpp)
target_compile_definitions(synthyboy_test_macro_names PRIVATE SYNTHYBOY_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
synthyboy_add_test(synthyboy_test_lfo_grid   test_lfo_grid.cpp)
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
synthyboy_add_test(synthyboy_test_own_osc_options test_own_osc_options.cpp)
synthyboy_add_test(synthyboy_test_own_content_opt test_own_content_opt.cpp)   # the CPU optimisations of the library oscillators change no output sample
target_include_directories(synthyboy_test_own_content_opt PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_compressor_opt test_own_compressor_opt.cpp)   # the CPU optimisation of the Compressor limiter changes no output sample
target_include_directories(synthyboy_test_own_compressor_opt PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_preset_render_output test_preset_render_output.cpp LINK synthyboy_fx_serum_b)   # the files of synthyboy_preset_render --float32 / --hash
target_include_directories(synthyboy_test_preset_render_output PRIVATE "${PROJECT_SOURCE_DIR}/tools/preset-render" "${PROJECT_SOURCE_DIR}/core/fx-serum-b")
synthyboy_add_test(synthyboy_test_own_helper_thread test_own_helper_thread.cpp)   # the wake-up flag and the helper threads of the engine
target_include_directories(synthyboy_test_own_helper_thread PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_libkeys_warm test_own_libkeys_warm.cpp)   # libKeys () is built in prepare (), not on the audio thread (a process of its own: built once per process)
target_include_directories(synthyboy_test_own_libkeys_warm PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_osc_types test_own_osc_types.cpp)
target_include_directories(synthyboy_test_own_osc_types PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_unison_cap test_own_unison_cap.cpp)
target_include_directories(synthyboy_test_own_unison_cap PRIVATE "${PROJECT_SOURCE_DIR}/bench")
synthyboy_add_test(synthyboy_test_own_filter_response test_own_filter_response.cpp)
synthyboy_add_test(synthyboy_test_own_filter_response_rates test_own_filter_response_rates.cpp)
synthyboy_add_test(synthyboy_test_own_filter_response_review test_own_filter_response_review.cpp)
synthyboy_add_test(synthyboy_test_own_displays test_own_displays.cpp)
synthyboy_add_test(synthyboy_test_own_osc_displays test_own_osc_displays.cpp)
target_include_directories(synthyboy_test_own_osc_displays PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_pad_pressure test_own_pad_pressure.cpp)
target_include_directories(synthyboy_test_own_pad_pressure PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
target_compile_definitions(synthyboy_test_own_pad_pressure PRIVATE SYNTHYBOY_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# The four lane form of the warped oscillator samples against the scalar functions: a translation unit of its own with the NEON forms switched on (also on Apple's
# arm64, where the engine runs the scalar code) and without fused multiply-add, as the ARM builds that use the form are compiled. It links nothing of the engine.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$" AND NOT MSVC)
    add_executable(synthyboy_test_own_warp_lanes test_own_warp_lanes.cpp "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_include_directories(synthyboy_test_own_warp_lanes PRIVATE "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/core/include" "${PROJECT_SOURCE_DIR}/core/engine-own")
    target_compile_features(synthyboy_test_own_warp_lanes PRIVATE cxx_std_17)
    target_compile_definitions(synthyboy_test_own_warp_lanes PRIVATE SB_TEST_NEON=1)
    target_compile_options(synthyboy_test_own_warp_lanes PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(synthyboy_test_own_warp_lanes PRIVATE Threads::Threads)
    add_test(NAME synthyboy_test_own_warp_lanes COMMAND synthyboy_test_own_warp_lanes)
    # The integer NEON compares (dsp/neon_cmp.h) against the float compares on every bit pattern, and the simd.h loops built on them (NEON forms on, no fused
    # multiply-add); header only, links nothing of the engine.
    add_executable(synthyboy_test_neon_cmp test_neon_cmp.cpp "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_include_directories(synthyboy_test_neon_cmp PRIVATE "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/core/engine-own")
    target_compile_features(synthyboy_test_neon_cmp PRIVATE cxx_std_17)
    target_compile_definitions(synthyboy_test_neon_cmp PRIVATE SB_TEST_NEON=1)
    target_compile_options(synthyboy_test_neon_cmp PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(synthyboy_test_neon_cmp PRIVATE Threads::Threads)
    add_test(NAME synthyboy_test_neon_cmp COMMAND synthyboy_test_neon_cmp)
    # The NEON block peak (dsp/peak.h: strip, bus, master and effect chain peaks) against the scalar loops it replaced, on every special value, default and flush-to-zero float mode; header only.
    add_executable(synthyboy_test_own_peak test_own_peak.cpp "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_include_directories(synthyboy_test_own_peak PRIVATE "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/core/engine-own")
    target_compile_features(synthyboy_test_own_peak PRIVATE cxx_std_17)
    target_compile_definitions(synthyboy_test_own_peak PRIVATE SB_TEST_NEON=1)
    target_compile_options(synthyboy_test_own_peak PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(synthyboy_test_own_peak PRIVATE Threads::Threads)
    add_test(NAME synthyboy_test_own_peak COMMAND synthyboy_test_own_peak)
    # The engine's master stage (dsp/master_stage.h) in its hoisted scalar and NEON forms against the per sample loop it replaced; header only.
    add_executable(synthyboy_test_own_master_stage test_own_master_stage.cpp "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_include_directories(synthyboy_test_own_master_stage PRIVATE "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/core/engine-own")
    target_compile_features(synthyboy_test_own_master_stage PRIVATE cxx_std_17)
    target_compile_definitions(synthyboy_test_own_master_stage PRIVATE SB_TEST_NEON=1)
    target_compile_options(synthyboy_test_own_master_stage PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(synthyboy_test_own_master_stage PRIVATE Threads::Threads)
    add_test(NAME synthyboy_test_own_master_stage COMMAND synthyboy_test_own_master_stage)
    # The voice filter (FilterUnit) and the chorus with their vector forms against their scalar loops (the --scalar-filters switch): the filters-serum sources compiled
    # into the test with the pair kernels and the NEON loops on, without fused multiply-add.
    add_executable(synthyboy_test_own_filter_unit_lanes test_own_filter_unit_lanes.cpp test_own_chorus_lanes.cpp "${PROJECT_SOURCE_DIR}/core/filters-serum/src/filter.cpp"
        "${PROJECT_SOURCE_DIR}/core/filters-serum/src/filter_types.cpp" "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_include_directories(synthyboy_test_own_filter_unit_lanes PRIVATE "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/core/include" "${PROJECT_SOURCE_DIR}/core/engine-own"
        "${PROJECT_SOURCE_DIR}/core/filters-serum/include" "${PROJECT_SOURCE_DIR}/core/filters-serum/src")
    target_compile_features(synthyboy_test_own_filter_unit_lanes PRIVATE cxx_std_17)
    target_compile_definitions(synthyboy_test_own_filter_unit_lanes PRIVATE SB_TEST_NEON=1 SYNTHYBOY_FILTER_TEST_NEON=1)
    target_compile_options(synthyboy_test_own_filter_unit_lanes PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(synthyboy_test_own_filter_unit_lanes PRIVATE Threads::Threads)
    add_test(NAME synthyboy_test_own_filter_unit_lanes COMMAND synthyboy_test_own_filter_unit_lanes)
endif()
# The lane mix of the library players (LibOsc::mixLane: simd::rampLerp + simd::mulAddTo) against the scalar loop, with the NEON forms on (also on Apple's arm64) and without fused multiply-add.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$" AND NOT MSVC)
    add_executable(synthyboy_test_own_mix_lanes test_own_mix_lanes.cpp "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_include_directories(synthyboy_test_own_mix_lanes PRIVATE "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/core/include" "${PROJECT_SOURCE_DIR}/core/engine-own")
    target_compile_features(synthyboy_test_own_mix_lanes PRIVATE cxx_std_17)
    target_compile_definitions(synthyboy_test_own_mix_lanes PRIVATE SB_TEST_NEON=1)
    target_compile_options(synthyboy_test_own_mix_lanes PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(synthyboy_test_own_mix_lanes PRIVATE Threads::Threads)
    add_test(NAME synthyboy_test_own_mix_lanes COMMAND synthyboy_test_own_mix_lanes)
endif()
# The NEON inverse transform of the spectral morph cycle against its scalar loops: morph.cpp and the wavetable it needs compiled into the test with the NEON form on (also on Apple's arm64) and
# without fused multiply-add; it links nothing else of the engine.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$" AND NOT MSVC)
    add_executable(synthyboy_test_own_morph_transform test_own_morph_transform.cpp "${PROJECT_SOURCE_DIR}/core/engine-own/osc/morph.cpp"
        "${PROJECT_SOURCE_DIR}/core/engine-own/osc/wavetable_bank.cpp" "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_include_directories(synthyboy_test_own_morph_transform PRIVATE "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/core/include" "${PROJECT_SOURCE_DIR}/core/engine-own"
        "${PROJECT_SOURCE_DIR}/core/src")
    target_compile_features(synthyboy_test_own_morph_transform PRIVATE cxx_std_17)
    target_compile_definitions(synthyboy_test_own_morph_transform PRIVATE SB_TEST_NEON=1)
    target_compile_options(synthyboy_test_own_morph_transform PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(synthyboy_test_own_morph_transform PRIVATE Threads::Threads)
    add_test(NAME synthyboy_test_own_morph_transform COMMAND synthyboy_test_own_morph_transform)
endif()
# The optimised wavetable oscillator (docs/release/CPU-REVIEW-OSC.md) against the code it replaced (WtOsc::setReference), bit for bit on random patches; the NEON forms on, no fused multiply-add.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$" AND NOT MSVC)
    add_executable(synthyboy_test_own_wt_osc_opt test_own_wt_osc_opt.cpp "${PROJECT_SOURCE_DIR}/core/engine-own/osc/morph.cpp"
        "${PROJECT_SOURCE_DIR}/core/engine-own/osc/wavetable_bank.cpp" "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_include_directories(synthyboy_test_own_wt_osc_opt PRIVATE "${PROJECT_SOURCE_DIR}/core/tests" "${PROJECT_SOURCE_DIR}/core/include" "${PROJECT_SOURCE_DIR}/core/engine-own"
        "${PROJECT_SOURCE_DIR}/core/src")
    target_compile_features(synthyboy_test_own_wt_osc_opt PRIVATE cxx_std_17)
    target_compile_definitions(synthyboy_test_own_wt_osc_opt PRIVATE SB_TEST_NEON=1)
    target_compile_options(synthyboy_test_own_wt_osc_opt PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_link_libraries(synthyboy_test_own_wt_osc_opt PRIVATE Threads::Threads)
    add_test(NAME synthyboy_test_own_wt_osc_opt COMMAND synthyboy_test_own_wt_osc_opt)
endif()
# A legato note takes the envelope scaling of its voice control step also while nothing of the voice is modulated (docs/release/CPU-REVIEW-VOICE-MOD.md V1 side note).
synthyboy_add_test(synthyboy_test_own_voice_legato_env test_own_voice_legato_env.cpp)
target_include_directories(synthyboy_test_own_voice_legato_env PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_voice_opt test_own_voice_opt.cpp)   # the CPU optimisations of the voice control and the matrix change no sample and no display value
target_include_directories(synthyboy_test_own_voice_opt PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
target_compile_definitions(synthyboy_test_own_voice_opt PRIVATE SYNTHYBOY_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
synthyboy_add_test(synthyboy_test_own_fx_mute test_own_fx_mute.cpp)   # an effect whose mix is exactly 0 is not processed (economy group Control)
target_include_directories(synthyboy_test_own_fx_mute PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_fx_makeup test_own_fx_makeup.cpp)   # the tier level makeup of the bypassed effects
target_include_directories(synthyboy_test_own_fx_makeup PRIVATE "${PROJECT_SOURCE_DIR}/bench")
# The output stage of the shells (core/include/synthyboy/output_stage.h) on every factory preset, chord and single note, Key 61 and XL profiles, at several make-up gains:
# never above the ceiling, finite, no safety clip, no loudness inversion; prints the gain reduction per category. Needs the factory content packs as a store (links by pack id).
set(SYNTHYBOY_OUTPUT_STAGE_STORE "${CMAKE_CURRENT_BINARY_DIR}/output-stage-content-store")
file(MAKE_DIRECTORY "${SYNTHYBOY_OUTPUT_STAGE_STORE}")
file(CREATE_LINK "${PROJECT_SOURCE_DIR}/content/factory-wavetables" "${SYNTHYBOY_OUTPUT_STAGE_STORE}/com.safian.synthyboy.factory-wavetables" SYMBOLIC COPY_ON_ERROR)
file(CREATE_LINK "${PROJECT_SOURCE_DIR}/content/factory-samples" "${SYNTHYBOY_OUTPUT_STAGE_STORE}/com.safian.synthyboy.factory-samples" SYMBOLIC COPY_ON_ERROR)
synthyboy_add_test(synthyboy_test_output_stage_presets test_output_stage_presets.cpp)
target_include_directories(synthyboy_test_output_stage_presets PRIVATE "${PROJECT_SOURCE_DIR}/shell-mpc")
target_compile_definitions(synthyboy_test_output_stage_presets PRIVATE SYNTHYBOY_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}" SYNTHYBOY_TEST_CONTENT_STORE="${SYNTHYBOY_OUTPUT_STAGE_STORE}")
set_tests_properties(synthyboy_test_output_stage_presets PROPERTIES LABELS slow TIMEOUT 1800)
synthyboy_add_test(synthyboy_test_output_stage test_output_stage.cpp)
target_include_directories(synthyboy_test_output_stage PRIVATE "${PROJECT_SOURCE_DIR}/shell-mpc")
# The voice-count gain of the own engine (docs/release/VOICE-COUNT-GAIN-SPEC.md): acceptance tests written before the implementation (they fail until it exists).
synthyboy_add_test(synthyboy_test_own_voice_count_gain test_own_voice_count_gain.cpp)
target_include_directories(synthyboy_test_own_voice_count_gain PRIVATE "${PROJECT_SOURCE_DIR}/shell-mpc")
target_compile_definitions(synthyboy_test_own_voice_count_gain PRIVATE SYNTHYBOY_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}" SYNTHYBOY_TEST_CONTENT_STORE="${SYNTHYBOY_OUTPUT_STAGE_STORE}")
set_tests_properties(synthyboy_test_own_voice_count_gain PROPERTIES LABELS slow TIMEOUT 1800)
synthyboy_add_test(synthyboy_test_own_profile test_own_profile.cpp)   # the economy groups of a quality (the device profiles of shell-mpc/device_profile.h)
target_include_directories(synthyboy_test_own_profile PRIVATE "${PROJECT_SOURCE_DIR}/bench")
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
synthyboy_add_test(synthyboy_test_wavetable_file test_wavetable_file.cpp)
synthyboy_add_test(synthyboy_test_wavetable_catalogue test_wavetable_catalogue.cpp)
synthyboy_add_test(synthyboy_test_user_content test_user_content.cpp "${PROJECT_SOURCE_DIR}/core/vital-import/tests/fixtures.cpp")
target_include_directories(synthyboy_test_user_content PRIVATE "${PROJECT_SOURCE_DIR}/core/vital-import" "${PROJECT_SOURCE_DIR}/core/vital-import/tests")
synthyboy_add_test(synthyboy_test_clap_content test_clap_content.cpp LINK synthyboy_static ${CMAKE_DL_LIBS})
# GROUPD (WP-14 / 15 / 16): sample -> wavetable, frame <-> LFO shape, WAV of a render, the offline render driver on the own engine
synthyboy_add_test(synthyboy_test_wave_convert test_wave_convert.cpp)
synthyboy_add_test(synthyboy_test_own_ladder_range test_own_ladder_range.cpp)
target_include_directories(synthyboy_test_own_ladder_range PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_morph_range test_own_morph_range.cpp)
target_include_directories(synthyboy_test_own_morph_range PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_comb_pitch test_own_comb_pitch.cpp)
target_include_directories(synthyboy_test_own_comb_pitch PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
synthyboy_add_test(synthyboy_test_own_engine_fixes test_own_engine_fixes.cpp)
synthyboy_add_test(synthyboy_test_own_filter_styles test_own_filter_styles.cpp)
target_include_directories(synthyboy_test_own_filter_styles PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own" "${PROJECT_SOURCE_DIR}/core/filters-serum/include")
synthyboy_add_test(synthyboy_test_own_keys_claimed test_own_keys_claimed.cpp)
synthyboy_add_test(synthyboy_test_own_note_storm test_own_note_storm.cpp)
synthyboy_add_test(synthyboy_test_own_content_policy test_own_content_policy.cpp)
synthyboy_add_test(synthyboy_test_own_convolve_bypass test_own_convolve_bypass.cpp)
target_include_directories(synthyboy_test_own_convolve_bypass PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")   # Convolve is bypassed at tier low (quality.h) also with an impulse loaded
