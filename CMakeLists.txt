# SynthyBoy - minimal 8-voice synthesizer (VST3 + CLAP from one code base)
# core/      JUCE-free static library synthyboy_core (engine interface, parameters, state, own engine) + tests
# shell-juce/ thin JUCE AudioProcessor -> VST3 + CLAP
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
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")

project(SynthyBoy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# What to build:
#   SYNTHYBOY_BUILD_PLUGIN  VST3 + CLAP via JUCE (fetches JUCE and clap-juce-extensions); OFF builds only the core
#                           (used by the ARMv7 / AArch64 cross builds and the sanitizer jobs)
#   SYNTHYBOY_BUILD_TESTS   core unit / real-time / thread tests (CTest)
#   SYNTHYBOY_WITH_VITAL    Vital-derived engine in core/engine-vital (docs/ARCHITECTURE.md 2.2); OFF for iOS (nothing
#                           of core/engine-vital is then added to the build)
option(SYNTHYBOY_BUILD_PLUGIN "Build the JUCE plugin (VST3 + CLAP)" ON)
option(SYNTHYBOY_BUILD_TESTS "Build the core tests" ON)
option(SYNTHYBOY_WITH_VITAL "Include the Vital-derived engine (core/engine-vital)" ON)

include(CTest)
enable_testing()

add_subdirectory(core)

if(NOT SYNTHYBOY_BUILD_PLUGIN)
    return()
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies - exact versions are pinned (see THIRD_PARTY.md).
# ---------------------------------------------------------------------------

# JUCE, used under its AGPL-3.0 option. 8.0.17-security is the newest 8.x release.
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.17-security
    GIT_SHALLOW    TRUE)

# clap-juce-extensions (MIT). The project does not publish recent tags (the newest one,
# 0.26.0, dates from 2022), so a commit on its main branch is pinned instead.
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
    GIT_SUBMODULES_RECURSE TRUE)

FetchContent_MakeAvailable(JUCE clap-juce-extensions)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------

juce_add_plugin(SynthyBoy
    COMPANY_NAME                 "Safian"
    BUNDLE_ID                    "com.safian.synthyboy"
    PLUGIN_MANUFACTURER_CODE     Safn
    PLUGIN_CODE                  Syby
    FORMATS                      VST3
    PRODUCT_NAME                 "SynthyBoy"
    IS_SYNTH                     TRUE
    NEEDS_MIDI_INPUT             TRUE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              Instrument Synth)

# Adds the SynthyBoy_CLAP target (same sources, CLAP wrapper on top of the JUCE processor).
clap_juce_extensions_plugin(
    TARGET        SynthyBoy
    CLAP_ID       "com.safian.synthyboy"
    CLAP_FEATURES instrument synthesizer
    # split the host block at event boundaries (sample-accurate notes/automation, 64-sample grid)
    CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 64)

if(APPLE)
    # JUCE ad-hoc signs the VST3 bundle, but then writes Resources/moduleinfo.json, which invalidates
    # the seal on a fresh build; the CLAP bundle is only linker-signed (invalid once Info.plist and
    # resources are added). Re-sign both as the last post-build step so they verify with `codesign`.
    # Use -DSYNTHYBOY_CODESIGN_IDENTITY="Developer ID Application: ..." to sign for real.
    set(SYNTHYBOY_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity for the plugin bundles (- = ad hoc)")
    foreach(bundle_target SynthyBoy_VST3 SynthyBoy_CLAP)
        add_custom_command(TARGET ${bundle_target} POST_BUILD
            COMMAND codesign --force --sign "${SYNTHYBOY_CODESIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:${bundle_target}>"
            VERBATIM)
    endforeach()
endif()

target_sources(SynthyBoy PRIVATE
    shell-juce/PluginProcessor.cpp
    shell-juce/PluginProcessor.h)

target_include_directories(SynthyBoy PRIVATE shell-juce)

target_compile_definitions(SynthyBoy PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SynthyBoy
    PRIVATE
        synthyboy_core
        juce::juce_audio_processors
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# CLAP smoke host: parameter text round trip + state reproducibility + parity with core's parameter table +
# audio (see tests/clap-smoke).
# Run: build/clap_smoke build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap
# ---------------------------------------------------------------------------
if(NOT WIN32)
    add_executable(clap_smoke tests/clap-smoke/state_roundtrip.cpp)
    target_include_directories(clap_smoke PRIVATE "${clap-juce-extensions_SOURCE_DIR}/clap-libs/clap/include")
    target_link_libraries(clap_smoke PRIVATE synthyboy_core ${CMAKE_DL_LIBS})
    add_dependencies(clap_smoke SynthyBoy_CLAP)
endif()
