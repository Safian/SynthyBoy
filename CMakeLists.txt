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
# iOS (-DCMAKE_SYSTEM_NAME=iOS): 15.0, the lowest target the libc++ of the current SDK still supports without a warning (and JUCE 8 needs 12).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target")
else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")
endif()

project(SynthyBoy VERSION 0.1.0 LANGUAGES C CXX)

# The version shown in Preferences / About (UiHost::aboutText, S15-20): the project version above, for every target (the plug-in shells and the tests)
add_compile_definitions(SYNTHYBOY_VERSION_STRING="${PROJECT_VERSION}")

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
option(SYNTHYBOY_BUILD_MPC_SHELL "Build the MPC VST2 shell (shell-mpc)" OFF)
set(MPC_SDK_DIR "" CACHE PATH "Path to the MPC plugin SDK (for shell-mpc)")
# SYNTHYBOY_BITMAP_GUI: the native CLAP module (shell-clap, synthyboy.clap) renders the editor of shell-juce/ui into a pixel buffer and offers it through the CLAP extension
# com.safian.bitmap-gui/1 (docs/BITMAP-GUI-EXTENSION.md). It links its own JUCE (fetched, statically, hidden symbols), so it needs JUCE even when the JUCE plug-in is not built.
# ON with the plug-in (desktop builds); OFF without it unless asked for (the ARMv7 module build, scripts/build-armv7-clap.sh, asks). Never part of the static library
# synthyboy_static (iOS, docs/ARCHITECTURE.md 2.2): nothing of JUCE or of the UI is linked into it, scripts/check-no-vital.sh stays clean.
#   SYNTHYBOY_STATIC_UI     the static library WITH the bitmap GUI for hosts that link the plug-in (iOS: the Kick app, whose own JUCE 7 is in the same binary): synthyboy_static_ui
#                           (shell-clap/CMakeLists.txt, docs/ARCHITECTURE.md 4.3). Needs SYNTHYBOY_BITMAP_GUI, and a JUCE of its own in a C++ namespace (SYNTHYBOY_JUCE_NAMESPACE, below).
#   SYNTHYBOY_JUCE_NAMESPACE  the name that "juce" gets in this build (cmake/JuceNamespace.cmake). Default: none on the desktop (hidden symbols are enough in a dynamic module),
#                           "synthyboy_juce8" whenever SYNTHYBOY_STATIC_UI is ON (a static link sees hidden symbols too).
option(SYNTHYBOY_BITMAP_GUI "Native CLAP module with the bitmap GUI extension (needs JUCE)" ${SYNTHYBOY_BUILD_PLUGIN})
option(SYNTHYBOY_STATIC_UI "Static library synthyboy_static_ui: synthyboy_static with the bitmap GUI and its own namespaced JUCE (iOS)" OFF)
if(SYNTHYBOY_STATIC_UI)
    set(SYNTHYBOY_BITMAP_GUI ON CACHE BOOL "Native CLAP module with the bitmap GUI extension (needs JUCE)" FORCE)
    set(SYNTHYBOY_JUCE_NAMESPACE_DEFAULT "synthyboy_juce8")
else()
    set(SYNTHYBOY_JUCE_NAMESPACE_DEFAULT "")
endif()
set(SYNTHYBOY_JUCE_NAMESPACE "${SYNTHYBOY_JUCE_NAMESPACE_DEFAULT}" CACHE STRING "C++ namespace that JUCE is built in (empty: juce)")
if(SYNTHYBOY_STATIC_UI AND NOT SYNTHYBOY_JUCE_NAMESPACE)
    message(FATAL_ERROR "SYNTHYBOY_STATIC_UI=ON needs SYNTHYBOY_JUCE_NAMESPACE: the host links its own JUCE next to this one")
endif()
if(SYNTHYBOY_STATIC_UI AND SYNTHYBOY_BUILD_PLUGIN)
    message(FATAL_ERROR "SYNTHYBOY_STATIC_UI=ON is the build for a host app (iOS): without the JUCE plug-in (-DSYNTHYBOY_BUILD_PLUGIN=OFF)")
endif()
if(SYNTHYBOY_BUILD_PLUGIN OR SYNTHYBOY_BITMAP_GUI)
    set(SYNTHYBOY_NEEDS_JUCE ON)
else()
    set(SYNTHYBOY_NEEDS_JUCE OFF)
endif()

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

# Apple ld reports every static library that two link lines name ("ignoring duplicate libraries"): the packages of core/ list each other and the targets
# above them list core again, which is how CMake keeps static libraries in dependency order. The duplicates are harmless; the message is not a finding.
if(APPLE)
    add_link_options(-Wl,-no_warn_duplicate_libraries)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies - exact versions are pinned (see THIRD_PARTY.md).
# ---------------------------------------------------------------------------
if(SYNTHYBOY_NEEDS_JUCE)
    # JUCE, used under its AGPL-3.0 option. 8.0.17-security is the newest 8.x release.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.17-security
        GIT_SHALLOW    TRUE)
endif()

if(SYNTHYBOY_BUILD_PLUGIN)

    # clap-juce-extensions (MIT). Pinned commit. SOURCE_SUBDIR points at a folder without a CMakeLists.txt: FetchContent only fetches, the patched copy below is the one that is built.
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
        GIT_SUBMODULES_RECURSE TRUE
        SOURCE_SUBDIR  synthyboy-patched-copy-is-built)

    FetchContent_MakeAvailable(JUCE clap-juce-extensions)
    if(SYNTHYBOY_JUCE_NAMESPACE)
        message(FATAL_ERROR "SYNTHYBOY_JUCE_NAMESPACE is for the build without the JUCE plug-in (clap-juce-extensions and the plug-in wrappers say juce::)")
    endif()

    # The wrapper of clap-juce-extensions has no hook for a CLAP extension of the plug-in (the native shell offers "synthyboy.content", shell-clap/synthyboy_content_ext.h). The patch in
    # third_party/patches adds one (clap_juce_audio_processor_capabilities::clapPluginExtension, called by the wrapper's extension ()); it is applied to a COPY of the fetched sources in the
    # build directory, the fetched tree (which may be shared between build directories) is never modified. The copy is made again when the patch or the fetched sources change.
    set(SYNTHYBOY_CJE_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/patches/clap-juce-extensions-extension-hook.patch")
    set(SYNTHYBOY_CJE_PATCHED "${CMAKE_BINARY_DIR}/third_party/clap-juce-extensions-patched")
    file(SHA256 "${SYNTHYBOY_CJE_PATCH}" SYNTHYBOY_CJE_PATCH_HASH)
    file(SHA256 "${clap-juce-extensions_SOURCE_DIR}/src/wrapper/clap-juce-wrapper.cpp" SYNTHYBOY_CJE_WRAPPER_HASH)
    set(SYNTHYBOY_CJE_STAMP "${SYNTHYBOY_CJE_PATCH_HASH} ${SYNTHYBOY_CJE_WRAPPER_HASH}")
    set(SYNTHYBOY_CJE_STAMP_FILE "${SYNTHYBOY_CJE_PATCHED}/.synthyboy-patch-stamp")
    set(SYNTHYBOY_CJE_CURRENT "")
    if(EXISTS "${SYNTHYBOY_CJE_STAMP_FILE}")
        file(READ "${SYNTHYBOY_CJE_STAMP_FILE}" SYNTHYBOY_CJE_CURRENT)
    endif()
    if(NOT SYNTHYBOY_CJE_CURRENT STREQUAL SYNTHYBOY_CJE_STAMP)
        find_program(SYNTHYBOY_PATCH_PROGRAM patch REQUIRED)
        message(STATUS "clap-juce-extensions: applying ${SYNTHYBOY_CJE_PATCH}")
        file(REMOVE_RECURSE "${SYNTHYBOY_CJE_PATCHED}")
        file(COPY "${clap-juce-extensions_SOURCE_DIR}/" DESTINATION "${SYNTHYBOY_CJE_PATCHED}" PATTERN ".git" EXCLUDE)
        execute_process(COMMAND "${SYNTHYBOY_PATCH_PROGRAM}" -p1 -i "${SYNTHYBOY_CJE_PATCH}"
                        WORKING_DIRECTORY "${SYNTHYBOY_CJE_PATCHED}" RESULT_VARIABLE SYNTHYBOY_CJE_PATCH_RESULT)
        if(NOT SYNTHYBOY_CJE_PATCH_RESULT EQUAL 0)
            file(REMOVE_RECURSE "${SYNTHYBOY_CJE_PATCHED}")
            message(FATAL_ERROR "the patch ${SYNTHYBOY_CJE_PATCH} does not apply to the clap-juce-extensions sources in ${clap-juce-extensions_SOURCE_DIR}")
        endif()
        file(WRITE "${SYNTHYBOY_CJE_STAMP_FILE}" "${SYNTHYBOY_CJE_STAMP}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SYNTHYBOY_CJE_PATCH}")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)   # the headers of the clap libraries are system headers for our warning level
        add_subdirectory("${SYNTHYBOY_CJE_PATCHED}" "${CMAKE_BINARY_DIR}/third_party/clap-juce-extensions-build" SYSTEM)
    else()
        add_subdirectory("${SYNTHYBOY_CJE_PATCHED}" "${CMAKE_BINARY_DIR}/third_party/clap-juce-extensions-build")
    endif()
    set(CLAP_INCLUDE_DIR "${SYNTHYBOY_CJE_PATCHED}/clap-libs/clap/include" CACHE INTERNAL "CLAP include dir")
else()
    # Plugin OFF: fetch lightweight CLAP headers only (for synthyboy_static and tests); JUCE too when the module has the bitmap GUI
    if(SYNTHYBOY_BITMAP_GUI)
        FetchContent_MakeAvailable(JUCE)
        if(SYNTHYBOY_JUCE_NAMESPACE)
            include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/JuceNamespace.cmake")
            synthyboy_juce_namespace("${SYNTHYBOY_JUCE_NAMESPACE}" "${juce_SOURCE_DIR}")
        endif()
    endif()
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        1.2.7
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(clap)
    set(CLAP_INCLUDE_DIR "${clap_SOURCE_DIR}/include" CACHE INTERNAL "CLAP include dir")
endif()

add_subdirectory(core)
find_package(Threads REQUIRED)
if(SYNTHYBOY_NEEDS_JUCE)
    # The custom GUI (shell-juce/ui, Phase 5): pure view-model library synthyboy_ui_model + the host independent Components library synthyboy_ui, which the JUCE plug-in, the native
    # module's bitmap GUI and the tests link. What ties it to the JUCE plug-in lives in shell-juce/ itself: the ParameterStore on the APVTS and the thin AudioProcessorEditor.
    add_subdirectory(shell-juce/ui)
endif()
add_subdirectory(shell-clap)
if(SYNTHYBOY_BUILD_MPC_SHELL)
    add_subdirectory(shell-mpc)
endif()
if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(bench)
    add_subdirectory(tools/paramdump)
    add_subdirectory(tools/preset-render)
    if(SYNTHYBOY_WITH_VITAL)
        add_subdirectory(tools/param-audit)
        add_subdirectory(tools/ref-compare)
        add_subdirectory(tools/own-oracle)
    endif()
    add_subdirectory(tools/vital-import)
    add_subdirectory(tools/serum-import)
    add_subdirectory(tools/contentpack)
    add_subdirectory(scripts/tests)
    # The reference renderer hosts the real Serum 2 plug-in (macOS, needs the JUCE audio processors module).
    if(APPLE AND SYNTHYBOY_NEEDS_JUCE AND NOT IOS)
        add_subdirectory(tools/serum-reference)
    endif()
endif()

# The tests of the bitmap GUI of the native module (tests/bitmap-gui): a CLAP host without JUCE that loads the built synthyboy.clap. With the JUCE plug-in they are added at the end of this
# file (the coexistence test loads SynthyBoy.clap too, its target must exist).
if(NOT SYNTHYBOY_BUILD_PLUGIN)
    if(SYNTHYBOY_BITMAP_GUI AND SYNTHYBOY_BUILD_TESTS AND NOT WIN32)
        add_subdirectory(tests/bitmap-gui)
    endif()
    return()
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------

# Formats: VST3 everywhere; the Audio Unit (v2 component, macOS only) is added on Apple desktop builds. The AU uses the existing
# PLUGIN_MANUFACTURER_CODE/PLUGIN_CODE (aumu Syby Safn) and the type follows IS_SYNTH (aumu = kAudioUnitType_MusicDevice); the MPC (VST2) and
# CLAP builds are separate targets and do not see this.
set(SYNTHYBOY_PLUGIN_FORMATS VST3)
if(APPLE AND NOT IOS)
    list(APPEND SYNTHYBOY_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(SynthyBoy
    COMPANY_NAME                 "Safian"
    BUNDLE_ID                    "com.safian.synthyboy"
    PLUGIN_MANUFACTURER_CODE     Safn
    PLUGIN_CODE                  Syby
    FORMATS                      ${SYNTHYBOY_PLUGIN_FORMATS}
    PRODUCT_NAME                 "SynthyBoy"
    IS_SYNTH                     TRUE
    NEEDS_MIDI_INPUT             TRUE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              Instrument Synth
    AU_MAIN_TYPE                 kAudioUnitType_MusicDevice
    AU_EXPORT_PREFIX             SynthyBoyAU)

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
    set(SYNTHYBOY_BUNDLE_TARGETS SynthyBoy_VST3 SynthyBoy_CLAP)
    if(TARGET SynthyBoy_AU)
        list(APPEND SYNTHYBOY_BUNDLE_TARGETS SynthyBoy_AU)
    endif()
    foreach(bundle_target ${SYNTHYBOY_BUNDLE_TARGETS})
        add_custom_command(TARGET ${bundle_target} POST_BUILD
            COMMAND codesign --force --sign "${SYNTHYBOY_CODESIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:${bundle_target}>"
            VERBATIM)
    endforeach()
endif()

# The UI library itself was added above (shell-juce/ui). The adapters of the JUCE plug-in:
set(SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES
    shell-juce/ApvtsParameterStore.cpp
    shell-juce/ApvtsParameterStore.h
    shell-juce/EditorAdapter.cpp
    shell-juce/EditorAdapter.h)

target_sources(SynthyBoy PRIVATE
    shell-juce/PluginProcessor.cpp
    shell-juce/PluginProcessor.h
    shell-juce/ClapBridge.cpp                    # the CLAP extension synthyboy.content in the JUCE-wrapped CLAP build (REMAINDER item 3)
    shell-clap/synthyboy_content_service.cpp     # ... shared with the native shell
    shell-clap/synthyboy_content_service.h
    ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES})
target_compile_definitions(SynthyBoy PRIVATE SYNTHYBOY_CLAP_BRIDGE=1)
target_include_directories(SynthyBoy PRIVATE shell-clap "${CLAP_INCLUDE_DIR}")

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
        synthyboy_ui
        clap_juce_extensions
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

# The CLAP extension "synthyboy.content" in the JUCE-wrapped CLAP build (REMAINDER item 3): the tests of the native shell (core/tests/test_clap_content.cpp) run against the .clap bundle
# (the wrapper asks the processor for the extension, third_party/patches/clap-juce-extensions-extension-hook.patch).
if(SYNTHYBOY_BUILD_TESTS AND TARGET synthyboy_test_clap_content AND TARGET SynthyBoy_CLAP)
    add_dependencies(synthyboy_test_clap_content SynthyBoy_CLAP)
    # macOS: a .clap bundle (a directory); Linux / Windows: the module is a single file (TARGET_BUNDLE_DIR exists for bundles only)
    if(APPLE)
        set(_synthyboy_clap_under_test "$<TARGET_BUNDLE_DIR:SynthyBoy_CLAP>")
    else()
        set(_synthyboy_clap_under_test "$<TARGET_FILE:SynthyBoy_CLAP>")
    endif()
    add_test(NAME synthyboy_test_clap_content_juce
        COMMAND ${CMAKE_COMMAND} -E env "SYNTHYBOY_CLAP_UNDER_TEST=${_synthyboy_clap_under_test}" $<TARGET_FILE:synthyboy_test_clap_content>)
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
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
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

    # the library alone: a real SynthyBoyEditor on a UiHost that is no AudioProcessor and has no APVTS (what the Kick app is); no PluginProcessor, no adapter
    synthyboy_add_test(synthyboy_test_ui_plainhost
        shell-juce/ui/tests/test_ui_plainhost.cpp
        LINK synthyboy_ui
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_compile_definitions(synthyboy_test_ui_plainhost PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

    synthyboy_add_test(synthyboy_test_glb
        shell-juce/ui/tests/test_glb.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
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
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
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

    synthyboy_add_test(synthyboy_test_ui_fxinst
        shell-juce/ui/tests/test_fxinstances_ui.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_fxinst PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_ui_fxinst PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_fxinst PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_fxinst PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    synthyboy_add_test(synthyboy_test_ui_panels
        shell-juce/ui/tests/test_ui_panels.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
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
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
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
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
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
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
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

    synthyboy_add_test(synthyboy_test_ui_shellfeat
        shell-juce/ui/tests/test_ui_shellfeat.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_shellfeat PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_ui_shellfeat PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        SYNTHYBOY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_shellfeat PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_shellfeat PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    # the content pipeline of the shell (WP-01): library, sample / table / IR flows, Vital import, Missing Files, library menus (synthetic fixtures only)
    synthyboy_add_test(synthyboy_test_ui_content
        shell-juce/ui/tests/test_ui_content.cpp
        core/vital-import/tests/fixtures.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_content PRIVATE shell-juce core/vital-import core/vital-import/tests core/serum-import/tests)
    target_compile_definitions(synthyboy_test_ui_content PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_content PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_content PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()

    # GROUPD (WP-14 / 15 / 16): Switch to wavetable, Render OSC Warp, Resample, the last note as audio, wavetable frame <-> LFO shape
    synthyboy_add_test(synthyboy_test_ui_groupd
        shell-juce/ui/tests/test_ui_groupd.cpp
        shell-juce/PluginProcessor.cpp
        ${SYNTHYBOY_SHELL_UI_ADAPTER_SOURCES}
        LINK synthyboy_ui
             juce::juce_audio_processors
             juce::juce_audio_utils
             juce::juce_gui_basics
             juce::juce_recommended_config_flags
             juce::juce_recommended_warning_flags)
    target_include_directories(synthyboy_test_ui_groupd PRIVATE shell-juce)
    target_compile_definitions(synthyboy_test_ui_groupd PRIVATE
        JucePlugin_Name="SynthyBoy"
        SYNTHYBOY_DEFAULT_TIER=${SYNTHYBOY_DEFAULT_TIER}
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
        target_compile_definitions(synthyboy_test_ui_groupd PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=1)
    else()
        target_compile_definitions(synthyboy_test_ui_groupd PRIVATE SYNTHYBOY_SHELL_ENGINE_VITAL=0)
    endif()
endif()

if(SYNTHYBOY_BITMAP_GUI AND SYNTHYBOY_BUILD_TESTS AND NOT WIN32)
    add_subdirectory(tests/bitmap-gui)
endif()
