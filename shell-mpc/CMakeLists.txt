# SynthyBoy - MPC VST2 shell (headless, no JUCE, no CLAP).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_mpc MODULE
    synthyboy_mpc.cpp)

target_include_directories(synthyboy_mpc
    PRIVATE "${CMAKE_SOURCE_DIR}/core/include"
            "${MPC_SDK_DIR}/framework")

target_link_libraries(synthyboy_mpc PUBLIC synthyboy_core)
target_compile_features(synthyboy_mpc PUBLIC cxx_std_17)
set_target_properties(synthyboy_mpc PROPERTIES
    PREFIX ""
    OUTPUT_NAME "synthyboy_mpc"
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# Engine selection follows SYNTHYBOY_HAS_VITAL from the core target (core/CMakeLists.txt ~66/69).
# The core target defines SYNTHYBOY_HAS_VITAL as a PUBLIC compile definition;
# the shell source selects the engine via #if SYNTHYBOY_HAS_VITAL.

target_compile_options(synthyboy_mpc PRIVATE -Wall -Wextra -Wpedantic)

# macOS: use export list + dead_strip to remove unreferenced symbols
# Mach-O symbols have a leading underscore
if(APPLE)
    set(EXPORT_LIST "${CMAKE_CURRENT_BINARY_DIR}/mpc_exports.txt")
    file(WRITE ${EXPORT_LIST} "_VSTPluginMain\n")
    target_link_options(synthyboy_mpc PRIVATE
        "-Wl,-exported_symbols_list,${EXPORT_LIST}"
        "-Wl,-dead_strip")
endif()

# Linux: use version script + exclude-libs (no leading underscore)
if(UNIX AND NOT APPLE)
    set(VERSION_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/mpc_exports.map")
    file(WRITE ${VERSION_SCRIPT} "{
        global: VSTPluginMain;
        local: *;
    };")
    target_link_options(synthyboy_mpc PRIVATE
        "-Wl,--version-script,${VERSION_SCRIPT}"
        "-Wl,--exclude-libs,ALL"
        "-Wl,--gc-sections"
        "-Wl,--as-needed"
        "-static-libstdc++"
        "-static-libgcc")
endif()
