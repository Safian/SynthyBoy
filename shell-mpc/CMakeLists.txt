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

# Engine selection follows SYNTHYBOY_SHELL_ENGINE (same as the root CMakeLists.txt for the JUCE shell).
# When SYNTHYBOY_SHELL_ENGINE is "vital" and Vital is in the build, SYNTHYBOY_MPC_ENGINE_VITAL=1;
# otherwise SYNTHYBOY_MPC_ENGINE_VITAL=0. The source uses #if SYNTHYBOY_MPC_ENGINE_VITAL && SYNTHYBOY_HAS_VITAL.
if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
    set(SYNTHYBOY_MPC_ENGINE_VITAL 1)
else()
    set(SYNTHYBOY_MPC_ENGINE_VITAL 0)
endif()
target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_ENGINE_VITAL=${SYNTHYBOY_MPC_ENGINE_VITAL})

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

# Linux: use version script + exclude-libs (no leading underscore).
# The ARM build for the MPC is made with GCC (scripts/build-mpc.sh, both architectures);
# --exclude-libs,ALL is not understood by zig's linker, and a zig-built module crashed at host exit on the device.
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
