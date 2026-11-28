# SynthyBoy - minimal 8-voice synthesizer (standalone Akai MPC plugin)
# core/      JUCE-free static library synthyboy_core (engine interface, parameters, state, own engine) + tests
# shell-mpc/  the MPC VST2 shell
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.22)

# Must be set before project(). macOS builds target the host architecture by default
# (arm64 on Apple Silicon); pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build.
# iOS (-DCMAKE_SYSTEM_NAME=iOS): 15.0, the lowest target the libc++ of the current SDK still supports without a warning (and JUCE 8 needs 12).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target")
else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")
endif()

project(SynthyBoy VERSION 0.1.0 LANGUAGES C CXX)

# Compiler cache (optional): with ccache installed the compile steps of an unchanged translation unit are served from its cache (a rebuild after a branch switch or in a
# fresh build directory). ON by default only when ccache is found; a missing ccache is never an error. A launcher the caller set (CMAKE_<LANG>_COMPILER_LAUNCHER) is kept.
find_program(SYNTHYBOY_CCACHE_PROGRAM ccache)
if(SYNTHYBOY_CCACHE_PROGRAM)
    set(SYNTHYBOY_USE_CCACHE_DEFAULT ON)
else()
    set(SYNTHYBOY_USE_CCACHE_DEFAULT OFF)
endif()
option(SYNTHYBOY_USE_CCACHE "Use ccache as the compiler launcher when it is installed" ${SYNTHYBOY_USE_CCACHE_DEFAULT})
if(SYNTHYBOY_USE_CCACHE AND SYNTHYBOY_CCACHE_PROGRAM)
    if(NOT CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${SYNTHYBOY_CCACHE_PROGRAM}")
    endif()
    if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SYNTHYBOY_CCACHE_PROGRAM}")
    endif()
    message(STATUS "ccache: ${SYNTHYBOY_CCACHE_PROGRAM}")
elseif(SYNTHYBOY_USE_CCACHE)
    message(STATUS "ccache: not found (SYNTHYBOY_USE_CCACHE has no effect)")
endif()

set(SYNTHYBOY_BUILD_NUMBER 1)
math(EXPR SYNTHYBOY_VST2_VERSION "${PROJECT_VERSION_MAJOR} * 1000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH} * 10 + ${SYNTHYBOY_BUILD_NUMBER}")

# The version shown in Preferences / About (UiHost::aboutText, S15-20): the project version above, for every target (the plug-in shells and the tests)
add_compile_definitions(SYNTHYBOY_VERSION_STRING="${PROJECT_VERSION}")
add_compile_definitions(SYNTHYBOY_BUILD_NUMBER=${SYNTHYBOY_BUILD_NUMBER})
add_compile_definitions(SYNTHYBOY_VST2_VERSION=${SYNTHYBOY_VST2_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# What to build:
#   SYNTHYBOY_BUILD_TESTS     core unit / real-time / thread tests (CTest)
#   SYNTHYBOY_BUILD_MPC_SHELL the MPC VST2 shell (shell-mpc)
option(SYNTHYBOY_BUILD_TESTS "Build the core tests" ON)
option(SYNTHYBOY_BUILD_MPC_SHELL "Build the MPC VST2 shell (shell-mpc)" OFF)
set(MPC_SDK_DIR "" CACHE PATH "Path to the MPC plugin SDK (for shell-mpc)")

include(CTest)
enable_testing()

# Apple ld reports every static library that two link lines name ("ignoring duplicate libraries"): the packages of core/ list each other and the targets
# above them list core again, which is how CMake keeps static libraries in dependency order. The duplicates are harmless; the message is not a finding.
if(APPLE)
    add_link_options(-Wl,-no_warn_duplicate_libraries)
endif()

add_subdirectory(core)
find_package(Threads REQUIRED)
if(SYNTHYBOY_BUILD_MPC_SHELL)
    add_subdirectory(shell-mpc)
endif()
if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(bench)
    add_subdirectory(tools/paramdump)
    add_subdirectory(tools/preset-render)
    add_subdirectory(tools/sound-lab)
    add_subdirectory(tools/filter-response)
    add_subdirectory(tools/param-audit)
    add_subdirectory(tools/contentpack)
    add_subdirectory(scripts/tests)
endif()
