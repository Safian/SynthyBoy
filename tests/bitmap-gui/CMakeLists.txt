# SynthyBoy - tests of the bitmap GUI (com.safian.bitmap-gui/1): hosts without JUCE that load the built synthyboy.clap. Added by the top level CMakeLists.txt when
# SYNTHYBOY_BITMAP_GUI and SYNTHYBOY_BUILD_TESTS are ON.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

# the one Objective-C++ file (macOS) is compiled as C++ with -x objective-c++: no OBJCXX language is enabled for it
if(APPLE AND NOT IOS)
    set_source_files_properties(foreground_mac.mm PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-x;objective-c++")
endif()

# With SYNTHYBOY_STATIC_UI the plug-in under test is the finished static library synthyboy_static_ui (the one the iOS app links), linked into the test and entered through
# synthyboy_clap_get_entry, not a .clap that is loaded: the same tests, the same goldens, the code of the iOS archive (namespaced JUCE, partial link). On iOS they run in the simulator
# (scripts/test-ios-static-ui.sh); on a Mac they are ordinary ctest tests.
# The own engine (SYNTHYBOY_WITH_VITAL=OFF: the engine of iOS) does not act on most keys the GUI shows: the few assertions about the SOUND of an edit say so (SYNTHYBOY_HARNESS_OWN_ENGINE).
set(_engine_definitions "")
if(NOT SYNTHYBOY_WITH_VITAL)
    set(_engine_definitions SYNTHYBOY_HARNESS_OWN_ENGINE=1)
endif()
set(_static_ui FALSE)
if(TARGET synthyboy_static_ui)
    set(_static_ui TRUE)
endif()

function(synthyboy_add_bitmap_gui_test name)
    synthyboy_add_test(${name} ${ARGN})
    if(APPLE AND NOT IOS)
        target_sources(${name} PRIVATE foreground_mac.mm)
        target_link_libraries(${name} PRIVATE "-framework Cocoa")
    endif()
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/shell-clap")
    target_link_libraries(${name} PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(${name} PRIVATE ${_engine_definitions})
    if(_static_ui)
        target_link_libraries(${name} PRIVATE synthyboy_static_ui_link)
        target_compile_definitions(${name} PRIVATE SYNTHYBOY_HARNESS_STATIC_ENTRY=1)
        add_dependencies(${name} synthyboy_static_ui)
        set(_env "SYNTHYBOY_USER_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}-userdir")
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "${_env}" TIMEOUT 600)
        return()
    endif()
    add_dependencies(${name} synthyboy_clap)
    set(_env "SYNTHYBOY_CLAP_UNDER_TEST=$<TARGET_FILE:synthyboy_clap>;SYNTHYBOY_USER_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}-userdir")
    # the JUCE-wrapped CLAP build, when this build has it: "another JUCE in the process" for the coexistence test (a macOS .clap is a bundle directory)
    if(TARGET SynthyBoy_CLAP)
        add_dependencies(${name} SynthyBoy_CLAP)
        if(APPLE)
            list(APPEND _env "SYNTHYBOY_JUCE_CLAP_UNDER_TEST=$<TARGET_BUNDLE_DIR:SynthyBoy_CLAP>")
        else()
            list(APPEND _env "SYNTHYBOY_JUCE_CLAP_UNDER_TEST=$<TARGET_FILE:SynthyBoy_CLAP>")
        endif()
    endif()
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${_env}" TIMEOUT 600)
endfunction()

synthyboy_add_bitmap_gui_test(synthyboy_test_bitmap_gui test_bitmap_gui.cpp)
synthyboy_add_bitmap_gui_test(synthyboy_test_bitmap_golden test_bitmap_golden.cpp)
# The pictures depend on what the engine can do (the UI dims the keys it cannot): one set per engine, golden/ for the Vital-derived engine, golden-own/ for the own engine (docs/ui-design/OWN-ENGINE-GAP.md).
if(SYNTHYBOY_WITH_VITAL)
    target_compile_definitions(synthyboy_test_bitmap_golden PRIVATE SYNTHYBOY_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
else()
    target_compile_definitions(synthyboy_test_bitmap_golden PRIVATE SYNTHYBOY_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden-own")
endif()

if(_static_ui)
    return() # the bench loads the module and the export check reads a .clap: neither exists in this build
endif()

# not a test: frame times (docs/ui-design/BITMAPGUI-REPORT.md); run it on the target
add_executable(synthyboy_bitmap_gui_bench bench_bitmap_gui.cpp)
target_include_directories(synthyboy_bitmap_gui_bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/shell-clap" "${CLAP_INCLUDE_DIR}")
target_link_libraries(synthyboy_bitmap_gui_bench PRIVATE synthyboy_core Threads::Threads ${CMAKE_DL_LIBS})
target_compile_features(synthyboy_bitmap_gui_bench PRIVATE cxx_std_17)
add_dependencies(synthyboy_bitmap_gui_bench synthyboy_clap)

# The module exports clap_entry and synthyboy_clap_get_entry and nothing else (its JUCE is private to it); the script tests itself
find_program(SYNTHYBOY_BASH bash)
if(SYNTHYBOY_BASH)
    add_test(NAME synthyboy_clap_exports COMMAND "${SYNTHYBOY_BASH}" "${PROJECT_SOURCE_DIR}/scripts/check-clap-exports.sh" "$<TARGET_FILE:synthyboy_clap>")
    add_test(NAME synthyboy_clap_exports_self_test COMMAND "${SYNTHYBOY_BASH}" "${PROJECT_SOURCE_DIR}/scripts/check-clap-exports.sh" --self-test)
endif()
