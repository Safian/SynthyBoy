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
    # The overlay runtime (plugin-drawn pages): OFF until migration step 8 of docs/ui-components/TEMPLATE.md; src/overlay_hook.h says what it needs.
    option(MPCKIT_OVERLAY "MpcKit Demo: draw the pages on the overlay plane (needs the generic overlay runtime)" OFF)
    if(MPCKIT_OVERLAY)
        target_compile_definitions(mpckit_demo PRIVATE MPCKIT_OVERLAY=1)
    endif()
else()
    message(STATUS "mpckit_demo: MPC_SDK_DIR is not set, the plugin module is not configured (the host test still is)")
endif()

# ---- the host test: the pages without a display or a host ---------------------------------------------------------------------------------------------------------------
add_executable(mpckit_demo_pages_host EXCLUDE_FROM_ALL tests/pages_host.cpp)
target_compile_features(mpckit_demo_pages_host PRIVATE cxx_std_17)
target_compile_options(mpckit_demo_pages_host PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME mpckit_demo_pages_host COMMAND mpckit_demo_pages_host)
