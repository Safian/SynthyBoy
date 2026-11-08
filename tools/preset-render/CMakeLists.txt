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

# The gate: every factory preset x 3 tiers passes the checks (the velocity sweep is off here, it is for the report).
# Label `slow` like the other audio quality gates.
add_test(NAME synthyboy_preset_sanity
         COMMAND synthyboy_preset_render --presets "${PROJECT_SOURCE_DIR}/content/presets" --summary-only)
set_tests_properties(synthyboy_preset_sanity PROPERTIES LABELS slow TIMEOUT 1500)

# The shipped factory presets are exactly what scripts/gen-factory-presets.py generates (docs/params.json -> state v3, deterministic).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME synthyboy_factory_presets_reproducible
             COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/scripts/gen-factory-presets.py"
                     --check --out "${PROJECT_SOURCE_DIR}/content/presets")
endif()
