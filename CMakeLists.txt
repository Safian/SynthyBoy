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

# Engine of the JUCE shell (the plugin that ships). The Vital-derived engine is the default whenever it is part of the build; "own"
# is the fallback (and the engine of builds without Vital, e.g. iOS) and can be forced with -DSYNTHYBOY_SHELL_ENGINE=own.
if(SYNTHYBOY_WITH_VITAL)
    set(SYNTHYBOY_SHELL_ENGINE_DEFAULT "vital")
else()
    set(SYNTHYBOY_SHELL_ENGINE_DEFAULT "own")
endif()
set(SYNTHYBOY_SHELL_ENGINE "${SYNTHYBOY_SHELL_ENGINE_DEFAULT}" CACHE STRING "Engine used by the JUCE plugin: own | vital")
set_property(CACHE SYNTHYBOY_SHELL_ENGINE PROPERTY STRINGS own vital)
if(NOT SYNTHYBOY_SHELL_ENGINE STREQUAL "own" AND NOT SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
    message(FATAL_ERROR "SYNTHYBOY_SHELL_ENGINE must be 'own' or 'vital' (got '${SYNTHYBOY_SHELL_ENGINE}')")
endif()
if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital" AND NOT SYNTHYBOY_WITH_VITAL)
    message(FATAL_ERROR "SYNTHYBOY_SHELL_ENGINE=vital needs SYNTHYBOY_WITH_VITAL=ON (the Vital-derived engine is not part of this build)")
endif()

# Quality tier of the JUCE shell (docs/ARCHITECTURE.md 3.6): high on desktop. There is no user-visible parameter for it.
set(SYNTHYBOY_DEFAULT_TIER "high" CACHE STRING "Quality tier of the JUCE plugin: low | medium | high")
set_property(CACHE SYNTHYBOY_DEFAULT_TIER PROPERTY STRINGS low medium high)
if(NOT SYNTHYBOY_DEFAULT_TIER MATCHES "^(low|medium|high)$")
    message(FATAL_ERROR "SYNTHYBOY_DEFAULT_TIER must be low, medium or high (got '${SYNTHYBOY_DEFAULT_TIER}')")
endif()

include(CTest)
enable_testing()

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies - exact versions are pinned (see THIRD_PARTY.md).
# ---------------------------------------------------------------------------
if(SYNTHYBOY_BUILD_PLUGIN)
    # JUCE, used under its AGPL-3.0 option. 8.0.17-security is the newest 8.x release.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.17-security
        GIT_SHALLOW    TRUE)

    # clap-juce-extensions (MIT). Pinned commit.
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
        GIT_SUBMODULES_RECURSE TRUE)

    FetchContent_MakeAvailable(JUCE clap-juce-extensions)
    set(CLAP_INCLUDE_DIR "${clap-juce-extensions_SOURCE_DIR}/clap-libs/clap/include" CACHE INTERNAL "CLAP include dir")
else()
    # Plugin OFF: fetch lightweight CLAP headers only (for synthyboy_static and tests)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        1.2.7
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(clap)
    set(CLAP_INCLUDE_DIR "${clap_SOURCE_DIR}/include" CACHE INTERNAL "CLAP include dir")
endif()

add_subdirectory(core)
add_subdirectory(shell-clap)
if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(bench)
    add_subdirectory(tools/paramdump)
    add_subdirectory(tools/preset-render)
endif()

if(NOT SYNTHYBOY_BUILD_PLUGIN)
    return()
endif()

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

# The custom GUI (shell-juce/ui, Phase 5): pure view-model library + Components compiled into the plugin
find_package(Threads REQUIRED)
add_subdirectory(shell-juce/ui)

target_sources(SynthyBoy PRIVATE
    shell-juce/PluginProcessor.cpp
    shell-juce/PluginProcessor.h
    ${SYNTHYBOY_UI_SOURCES})

target_include_directories(SynthyBoy PRIVATE shell-juce)

if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
    target_compile_definitions(SynthyBoy PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
else()
    target_compile_definitions(SynthyBoy PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
endif()
target_compile_definitions(SynthyBoy PRIVATE SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER})
message(STATUS "JUCE shell engine: ${SYNTHYBOY_SHELL_ENGINE}, tier: ${SYNTHYBOY_DEFAULT_TIER}")

target_compile_definitions(SynthyBoy PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SynthyBoy
    PRIVATE
        synthyboy_core
        synthyboy_ui_model
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
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
    target_include_directories(clap_smoke PRIVATE "${CLAP_INCLUDE_DIR}")
    target_link_libraries(clap_smoke PRIVATE synthyboy_core ${CMAKE_DL_LIBS})
    add_dependencies(clap_smoke SynthyBoy_CLAP)
endif()

# ---------------------------------------------------------------------------
# GUI tests with real Components, no window and no display needed: the controls bound to the real processor, the assign-by-touch flow,
# A/B, keyboard to audio, 44 point touch targets, the panel registry contract and the phone / tablet screenshots
# (written to ui-screenshots/ in the test's working directory, or to $SYNTHYBOY_UI_SCREENSHOT_DIR).
# ---------------------------------------------------------------------------
if(SYNTHYBOY_BUILD_TESTS)
    synthyboy_add_test(synthyboy_test_ui_render
        shell-juce/ui/tests/test_wavetable.cpp
        shell-juce/ui/tests/test_ui_render.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_UI_SOURCES}
        LINK synthyboy_ui_model
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_render PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_ui_render PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_render PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_render PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    synthyboy_add_test(synthyboy_test_glb
        shell-juce/ui/tests/test_glb.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_UI_SOURCES}
        LINK synthyboy_ui_model
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_glb PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_glb PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_glb PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_glb PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    synthyboy_add_test(synthyboy_test_fx
        shell-juce/ui/tests/test_fx.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_UI_SOURCES}
        LINK synthyboy_ui_model
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_fx PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_fx PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_fx PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_fx PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    synthyboy_add_test(synthyboy_test_ui_panels
        shell-juce/ui/tests/test_ui_panels.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_UI_SOURCES}
        LINK synthyboy_ui_model
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_panels PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_ui_panels PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        SYNTHYBOY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_panels PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_panels PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    synthyboy_add_test(synthyboy_test_ui_lfo
        shell-juce/ui/tests/test_ui_lfo.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_UI_SOURCES}
        LINK synthyboy_ui_model
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_lfo PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_ui_lfo PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        SYNTHYBOY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_lfo PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_lfo PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    synthyboy_add_test(synthyboy_test_ui_wtremap
        shell-juce/ui/tests/test_ui_wtremap.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_UI_SOURCES}
        LINK synthyboy_ui_model
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_wtremap PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_ui_wtremap PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        SYNTHYBOY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_wtremap PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_wtremap PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    synthyboy_add_test(synthyboy_test_ui_gaps
        shell-juce/ui/tests/test_ui_gaps.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_UI_SOURCES}
        LINK synthyboy_ui_model
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_gaps PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_ui_gaps PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        SYNTHYBOY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_gaps PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_gaps PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()
endif()
