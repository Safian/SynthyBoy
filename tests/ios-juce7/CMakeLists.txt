# SynthyBoy - the static UI library next to a JUCE 7 in ONE binary (docs/ARCHITECTURE.md 4.3, docs/BITMAP-GUI-EXTENSION.md section 14): a stand-in for the Kick app on iOS.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# A project of its own (scripts/test-ios-static-ui.sh configures it): JUCE 7 and JUCE 8 cannot be two "juce::" targets of one CMake project, which is the point. It builds
#   * juce7_standin  JUCE 7.0.12 as Kick has it (the source tree of Kick's cache, JUCE7_DIR), NOT in a namespace, with the modules Kick uses (the GUI, the audio devices and utilities)
#   * the test       one executable that links juce7_standin whole (-force_load: every symbol of JUCE 7 is in the binary, the worst case) and libsynthyboy_static_ui.a with what it needs
#                    (the link list the SynthyBoy build writes), runs JUCE 7 first, SynthyBoy's GUI next to it, then both again
# Arguments: -DJUCE7_DIR=<JUCE 7 source> -DSYNTHYBOY_SOURCE_DIR=<this repository> -DSYNTHYBOY_STATIC_UI_LIB=<libsynthyboy_static_ui.a> -DSYNTHYBOY_STATIC_UI_LINK_LIST=<synthyboy_static_ui.link.txt>
#            -DCLAP_INCLUDE_DIR=<clap/include>
cmake_minimum_required(VERSION 3.22)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "")
endif()
project(SynthyBoyStaticUiWithJuce7 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
foreach(_required JUCE7_DIR SYNTHYBOY_SOURCE_DIR SYNTHYBOY_STATIC_UI_LIB SYNTHYBOY_STATIC_UI_LINK_LIST CLAP_INCLUDE_DIR)
    if(NOT ${_required})
        message(FATAL_ERROR "${_required} is not set")
    endif()
endforeach()

add_subdirectory("${JUCE7_DIR}" juce7 EXCLUDE_FROM_ALL)
add_library(juce7_standin STATIC juce7_standin.cpp)
# PRIVATE: the sources of the JUCE modules are compiled into juce7_standin and only there (linked PUBLIC they would be compiled into the executable as well: JUCE 7 twice)
target_link_libraries(juce7_standin PRIVATE juce::juce_gui_basics juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_formats juce::juce_recommended_config_flags)
target_compile_definitions(juce7_standin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

# what the SynthyBoy build says an app links with its library: archives and frameworks
file(STRINGS "${SYNTHYBOY_STATIC_UI_LINK_LIST}" _link_lines)
set(_synthyboy_link "")
foreach(_line ${_link_lines})
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "" OR _line MATCHES "^#")
        continue()
    endif()
    list(APPEND _synthyboy_link "${_line}")
endforeach()

add_executable(ios_static_ui_with_juce7 main.cpp)
target_include_directories(ios_static_ui_with_juce7 PRIVATE "${SYNTHYBOY_SOURCE_DIR}/shell-clap" "${CLAP_INCLUDE_DIR}")
target_link_libraries(ios_static_ui_with_juce7 PRIVATE juce7_standin "${SYNTHYBOY_STATIC_UI_LIB}" ${_synthyboy_link} "-framework Foundation" "-framework UIKit")
# the whole of JUCE 7 goes in, whether the test refers to it or not: a symbol that both JUCEs define is a link error here
target_link_options(ios_static_ui_with_juce7 PRIVATE "LINKER:-force_load,$<TARGET_FILE:juce7_standin>")
add_dependencies(ios_static_ui_with_juce7 juce7_standin)
