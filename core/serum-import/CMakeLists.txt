# SynthyBoy - Serum 2 preset importer (.SerumPreset and the other Serum 2 file types -> ParamValues + content + report), compiled into synthyboy_core.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
# Added by core/CMakeLists.txt after vital-import (one add_subdirectory line). Engine-free, no I/O: also part of the iOS build.

set(SB_SERUM_IMPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(synthyboy_core PRIVATE
    "${SB_SERUM_IMPORT_DIR}/zstd_decode.cpp"
    "${SB_SERUM_IMPORT_DIR}/cbor_reader.cpp"
    "${SB_SERUM_IMPORT_DIR}/audio_decode.cpp"
    "${SB_SERUM_IMPORT_DIR}/serum_container.cpp"
    "${SB_SERUM_IMPORT_DIR}/serum_map.cpp"
    "${SB_SERUM_IMPORT_DIR}/serum_import.cpp"
    "${SB_SERUM_IMPORT_DIR}/serum_voice.cpp"
    "${SB_SERUM_IMPORT_DIR}/serum_content.cpp"
    "${SB_SERUM_IMPORT_DIR}/serum_fx.cpp"
    "${SB_SERUM_IMPORT_DIR}/serum_api.cpp"
    "${SB_SERUM_IMPORT_DIR}/serum_plugin_import.cpp")
target_include_directories(synthyboy_core PRIVATE "${SB_SERUM_IMPORT_DIR}" "${PROJECT_SOURCE_DIR}/core/vital-import")

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    synthyboy_add_test(synthyboy_test_serum_container tests/test_serum_container.cpp)
    target_include_directories(synthyboy_test_serum_container PRIVATE "${SB_SERUM_IMPORT_DIR}" "${PROJECT_SOURCE_DIR}/core/vital-import")
    target_compile_definitions(synthyboy_test_serum_container PRIVATE
        SB_SERUM_PRESETS_DIR="${PROJECT_SOURCE_DIR}/tests/serum-presets"
        SB_SERUM_PRESETS_MAIN="${PROJECT_SOURCE_DIR}/../../../tests/serum-presets")
endif()
