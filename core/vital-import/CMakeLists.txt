# SynthyBoy - Vital preset importer (.vital -> ParamValues + report), compiled into synthyboy_core.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
# Added by core/CMakeLists.txt after wavetable-render (one add_subdirectory line). Vital-free: also part of the iOS build.

set(SB_VITAL_IMPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(synthyboy_core PRIVATE
    "${SB_VITAL_IMPORT_DIR}/json_reader.cpp"
    "${SB_VITAL_IMPORT_DIR}/vital_map.cpp"
    "${SB_VITAL_IMPORT_DIR}/vital_content.cpp"
    "${SB_VITAL_IMPORT_DIR}/builtin_ref.cpp"
    "${SB_VITAL_IMPORT_DIR}/vital_import.cpp")
target_include_directories(synthyboy_core PRIVATE "${SB_VITAL_IMPORT_DIR}")

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    synthyboy_add_test(synthyboy_test_vital_import tests/test_vital_import.cpp tests/fixtures.cpp)
    target_include_directories(synthyboy_test_vital_import PRIVATE "${SB_VITAL_IMPORT_DIR}" "${SB_VITAL_IMPORT_DIR}/tests")
    target_compile_definitions(synthyboy_test_vital_import PRIVATE
        SB_VITAL_PRESETS_DIR="${PROJECT_SOURCE_DIR}/tests/vital-presets")
    # a cross-check against the engine's own built-in tables needs the Vital-derived engine
    if(SYNTHYBOY_WITH_VITAL)
        synthyboy_add_test(synthyboy_test_vital_import_crosscheck tests/test_crosscheck.cpp
            LINK synthyboy_engine_vital)
        target_include_directories(synthyboy_test_vital_import_crosscheck PRIVATE
            "${SB_VITAL_IMPORT_DIR}" "${PROJECT_SOURCE_DIR}/core/engine-vital/adapter")
    endif()
endif()
