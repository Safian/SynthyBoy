# MpcKit Demo - the starter template for an Akai MPC VST2 plugin with plugin-drawn pages (template-mpc/README.md).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# NOT part of the default build. To add it to the repository's build, ONE line in the top-level CMakeLists.txt, right after `add_subdirectory(shell-mpc)` (line 203, inside
# `if(SYNTHYBOY_BUILD_MPC_SHELL)`):
#
#     add_subdirectory(template-mpc)
#
# It needs the same MPC_SDK_DIR as the shell (the folder of the SDK that holds framework/mpc_vst2_abi.h) and, for the host test, a native compiler. Nothing here links the synthesizer engine
# (synthyboy_core): the template uses only shell-mpc/kit/, shell-mpc/ui/ and the generic files of shell-mpc/overlay/.
#
# Standalone (outside the repository's build):  cmake -S template-mpc -B build-template -DMPC_SDK_DIR=$MPC_SDK
# Targets: mpckit_demo (the plugin, a MODULE: mpckit_demo.so; cross build it like the shell: scripts/build-mpc.sh builds only synthyboy_mpc, see README "Build"), mpckit_demo_pages_host (host test, EXCLUDE_FROM_ALL).

cmake_minimum_required(VERSION 3.16)
if(NOT PROJECT_NAME)
    project(mpckit_demo LANGUAGES CXX)
endif()

set(MPC_SDK_DIR "" CACHE PATH "Path to the MPC plugin SDK (the folder that holds framework/)")

# ---- the plugin ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
if(MPC_SDK_DIR)
    add_library(mpckit_demo MODULE src/demo_plugin.cpp)
    target_include_directories(mpckit_demo PRIVATE "${MPC_SDK_DIR}/framework")
    target_compile_features(mpckit_demo PUBLIC cxx_std_17)
    target_compile_options(mpckit_demo PRIVATE -Wall -Wextra -Wpedantic)
    find_package(Threads REQUIRED)
    target_link_libraries(mpckit_demo PRIVATE Threads::Threads)
    set_target_properties(mpckit_demo PROPERTIES
        PREFIX ""
        OUTPUT_NAME "mpckit_demo"
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    # The module exports VSTPluginMain only and links no dynamic libstdc++ (the checks of scripts/mpc-container-build.sh: the MPC firmware has its own libstdc++; the same options as shell-mpc/CMakeLists.txt).
    if(UNIX AND NOT APPLE)
        set(MPCKIT_EXPORTS "${CMAKE_CURRENT_BINARY_DIR}/mpckit_exports.map")
        file(WRITE ${MPCKIT_EXPORTS} "{
            global: VSTPluginMain;
            local: *;
        };")
        target_link_options(mpckit_demo PRIVATE "-Wl,--version-script,${MPCKIT_EXPORTS}" "-Wl,--exclude-libs,ALL" "-Wl,--gc-sections" "-Wl,--as-needed" "-static-libstdc++" "-static-libgcc")
    endif()
    # The overlay runtime (plugin-drawn pages): shell-mpc/overlay/ (header only, Linux code for the device) and its GPU helper, a separate executable (shell-mpc/overlay/gpu-helper). OFF by default: without it the
    # plugin is an instrument with the host's own widgets (skin/pages.json). ON: a Linux target only (a cross build for the device: the same toolchain as scripts/build-mpc.sh); the helper is built as
    # mpckit-demo-overlay-gpu (the name of kBranding in src/overlay_hook.h) and goes next to the plugin binary in the package (scripts/build-mpc.sh --plugin template-mpc does it).
    option(MPCKIT_OVERLAY "MpcKit Demo: draw the pages on the overlay plane (needs the generic overlay runtime)" OFF)
    if(MPCKIT_OVERLAY)
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
            message(FATAL_ERROR "MPCKIT_OVERLAY needs a Linux target (DRM, evdev, dlopen): cross build for the MPC")
        endif()
        find_package(Threads REQUIRED)
        target_compile_definitions(mpckit_demo PRIVATE MPCKIT_OVERLAY=1 SYNTHYBOY_MPC_OVERLAY_GPU=1 SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS=33.3)
        target_link_libraries(mpckit_demo PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../shell-mpc/overlay/gpu-helper" gpu-helper)
        set_target_properties(synthyboy_overlay_gpu PROPERTIES OUTPUT_NAME "mpckit-demo-overlay-gpu")
    endif()
else()
    message(STATUS "mpckit_demo: MPC_SDK_DIR is not set, the plugin module is not configured (the host test still is)")
endif()

# ---- the host test: the pages without a display or a host ---------------------------------------------------------------------------------------------------------------
add_executable(mpckit_demo_pages_host EXCLUDE_FROM_ALL tests/pages_host.cpp)
target_compile_features(mpckit_demo_pages_host PRIVATE cxx_std_17)
target_compile_options(mpckit_demo_pages_host PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME mpckit_demo_pages_host COMMAND mpckit_demo_pages_host)
