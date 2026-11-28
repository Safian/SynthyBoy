# SynthyBoy - JUCE-free core library (engine interface, parameters, state, own engine) and its tests.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_core STATIC
    src/number.cpp
    src/engine_factory.cpp
    src/params.cpp
    src/param_table.cpp
    src/modulation.cpp
    src/fx_types.cpp
    src/fx_rack.cpp
    src/state.cpp
    src/content_state.cpp
    src/engine_caps.cpp
    src/sha256.cpp
    src/preset.cpp
    src/content_pack.cpp
    src/content_store.cpp
    src/user_content.cpp
    src/wavetable_file.cpp
    src/wavetable_catalogue.cpp
    src/user_library.cpp
    src/library_items.cpp
    src/shell_features.cpp
    src/preferences.cpp
    src/offline_render.cpp
    src/filter_response.cpp
    src/lfo_display.cpp
    src/env_display.cpp
    src/osc_cycle.cpp
    src/wave_convert.cpp
    engine-own/own_engine.cpp
    engine-own/own_support.cpp
    engine-own/fx/fx_budget.cpp
    engine-own/fx/fx_chain.cpp
    engine-own/fx/fx_modules.cpp
    engine-own/fx/fx_makeup.cpp
    engine-own/osc/wavetable_bank.cpp
    engine-own/osc/morph.cpp
    engine-own/osc/content_set.cpp
    engine-own/osc/lib_osc.cpp
    engine-own/matrix/mod_matrix.cpp
    engine-own/voice/param_index.cpp
    engine-own/voice/voice.cpp
    engine-own/dsp/section_timing.cpp)

target_include_directories(synthyboy_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/engine-own")

option(SYNTHYBOY_SECTION_TIMING "Benchmark only: time the sections of the own engine (synthyboy_bench --sections)" OFF)
if(SYNTHYBOY_SECTION_TIMING)
    target_compile_definitions(synthyboy_core PUBLIC SB_SECTION_TIMING=1) # public: the engine headers that the tests include must be the same code
endif()

target_compile_features(synthyboy_core PUBLIC cxx_std_17)
set_target_properties(synthyboy_core PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(synthyboy_core PRIVATE /W4)
else()
    target_compile_options(synthyboy_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(tuning)
add_subdirectory(formula)
add_subdirectory(filters-serum)
add_subdirectory(wavetable-edit)
add_subdirectory(shapes)
# the custom LFO shapes and the velocity / note curves are evaluated with the shapes package
target_link_libraries(synthyboy_core PUBLIC synthyboy_shapes) # PUBLIC: the GUI previews custom LFO shapes with the same evaluator

if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Packages with their own CMake (after the tests: they use synthyboy_add_test)
add_subdirectory(wavetable-render)
add_subdirectory(vital-import)
add_subdirectory(serum-import)
add_subdirectory(fx-serum-a)
add_subdirectory(fx-serum-b)

add_subdirectory(sample-osc)
add_subdirectory(granular)
add_subdirectory(spectral)
add_subdirectory(multisample)
# the user content library decodes WAV files with the sample oscillator's reader (also in the own-engine-only build)
target_link_libraries(synthyboy_core PUBLIC synthyboy_sampleosc synthyboy_granular synthyboy_spectral synthyboy_multisample)
# the own engine runs Hyper/Dimension and Utility of fx-serum-a (and, later, Bode and Convolve of fx-serum-b) in its effect chain
target_link_libraries(synthyboy_core PUBLIC synthyboy_fx_serum_a synthyboy_fx_serum_b)
