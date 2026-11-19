# SynthyBoy - preset-render: renders the factory presets through an engine at every quality tier and checks them (finite, not silent,
# no clipping, bounded tail); prints the level spread and the velocity curve. Added by the top level CMakeLists.txt when
# SYNTHYBOY_BUILD_TESTS is ON.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_preset_render preset_render.cpp)
target_link_libraries(synthyboy_preset_render PRIVATE synthyboy_core synthyboy_fx_serum_b)
target_include_directories(synthyboy_preset_render PRIVATE "${PROJECT_SOURCE_DIR}/core/fx-serum-b")
set_target_properties(synthyboy_preset_render PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_preset_render PRIVATE /W4)
else()
    target_compile_options(synthyboy_preset_render PRIVATE -Wall -Wextra -Wpedantic)
endif()

# level-probe: the levels of every factory preset after the shells' output stage, for the levelling against the stock MPC instruments (scripts/level-to-stock.py).
add_executable(synthyboy_level_probe level_probe.cpp)
target_link_libraries(synthyboy_level_probe PRIVATE synthyboy_core)
set_target_properties(synthyboy_level_probe PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_level_probe PRIVATE /W4)
else()
    target_compile_options(synthyboy_level_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The gate: every factory preset x 3 tiers passes the checks (the velocity sweep is off here, it is for the report).
# Label `slow` like the other audio quality gates.
# The factory presets are written for the Vital-derived engine (the own engine of builds without Vital, iOS, has no sample / granular / spectral
# oscillators), so the gate runs only where that engine is built.
# The files the presets of sound set batch 2 name (wavetables, samples, instruments, impulse responses: pack id + file) are read from a content store, <root>/<pack id>/<file>:
# links to the factory packs content/factory-* named by their ids (the shell reads them from Content/packs/<pack id>, scripts/build-mpc.sh). Without a store a sample, granular,
# spectral or multisample oscillator is silent and Convolve absent, and the gate would not check what those presets sound like.
set(SYNTHYBOY_CONTENT_STORE "${CMAKE_CURRENT_BINARY_DIR}/content-store")
file(MAKE_DIRECTORY "${SYNTHYBOY_CONTENT_STORE}")
file(CREATE_LINK "${PROJECT_SOURCE_DIR}/content/factory-wavetables" "${SYNTHYBOY_CONTENT_STORE}/com.safian.synthyboy.factory-wavetables" SYMBOLIC COPY_ON_ERROR)
file(CREATE_LINK "${PROJECT_SOURCE_DIR}/content/factory-samples" "${SYNTHYBOY_CONTENT_STORE}/com.safian.synthyboy.factory-samples" SYMBOLIC COPY_ON_ERROR)

if(SYNTHYBOY_WITH_VITAL)
    add_test(NAME synthyboy_preset_sanity
             COMMAND synthyboy_preset_render --presets "${PROJECT_SOURCE_DIR}/content/presets" --content-dir "${SYNTHYBOY_CONTENT_STORE}" --summary-only)
    set_tests_properties(synthyboy_preset_sanity PROPERTIES LABELS slow TIMEOUT 1500)
endif()

# The shipped factory presets are exactly what scripts/gen-factory-presets.py generates (docs/params.json -> state v3, deterministic).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME synthyboy_factory_presets_reproducible
             COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/scripts/gen-factory-presets.py"
                     --check --out "${PROJECT_SOURCE_DIR}/content/presets")
endif()
