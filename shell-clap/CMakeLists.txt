# SynthyBoy - native JUCE-free CLAP shell (docs/ARCHITECTURE.md 4).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

if(NOT DEFINED CLAP_INCLUDE_DIR)
    if(DEFINED clap-juce-extensions_SOURCE_DIR AND EXISTS "${clap-juce-extensions_SOURCE_DIR}/clap-libs/clap/include/clap/clap.h")
        set(CLAP_INCLUDE_DIR "${clap-juce-extensions_SOURCE_DIR}/clap-libs/clap/include")
    elseif(DEFINED clap_SOURCE_DIR AND EXISTS "${clap_SOURCE_DIR}/include/clap/clap.h")
        set(CLAP_INCLUDE_DIR "${clap_SOURCE_DIR}/include")
    endif()
endif()

# Target: synthyboy_static (static library with synthyboy_clap_get_entry for iOS / MPC / embedded hosts)
add_library(synthyboy_static STATIC
    synthyboy_clap.cpp
    synthyboy_clap.h
    synthyboy_plugin.cpp
    synthyboy_plugin.h
    synthyboy_ui_link.h
    synthyboy_content_service.cpp
    synthyboy_content_service.h)

target_include_directories(synthyboy_static
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CLAP_INCLUDE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/core/include")

target_link_libraries(synthyboy_static PUBLIC synthyboy_core)
target_compile_features(synthyboy_static PUBLIC cxx_std_17)
set_target_properties(synthyboy_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Target: synthyboy_clap (dynamic CLAP module .clap for standalone CLAP hosts like Kick). Not on iOS: an app there has no dlopen of its own plug-ins.
if(NOT IOS)
    add_library(synthyboy_clap MODULE
        synthyboy_clap.cpp
        synthyboy_clap.h
        synthyboy_plugin.cpp
        synthyboy_plugin.h
        synthyboy_ui_link.h
        synthyboy_content_service.cpp
        synthyboy_content_service.h)

    target_include_directories(synthyboy_clap
        PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
                "${CLAP_INCLUDE_DIR}"
        PRIVATE "${CMAKE_SOURCE_DIR}/core/include")

    target_link_libraries(synthyboy_clap PUBLIC synthyboy_core)
    target_compile_features(synthyboy_clap PUBLIC cxx_std_17)
    set_target_properties(synthyboy_clap PROPERTIES
        PREFIX ""
        SUFFIX ".clap"
        OUTPUT_NAME "synthyboy"
        POSITION_INDEPENDENT_CODE ON)
endif()

if(MSVC)
    target_compile_options(synthyboy_static PRIVATE /W4)
    if(TARGET synthyboy_clap)
        target_compile_options(synthyboy_clap PRIVATE /W4)
    endif()
else()
    target_compile_options(synthyboy_static PRIVATE -Wall -Wextra -Wpedantic)
    if(TARGET synthyboy_clap)
        target_compile_options(synthyboy_clap PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# The bitmap GUI (docs/BITMAP-GUI-EXTENSION.md): the module renders shell-juce/ui itself and exports it through com.safian.bitmap-gui/1. synthyboy_static never has it; the static library
# of hosts that want the GUI too (iOS) is synthyboy_static_ui below.
if(SYNTHYBOY_BITMAP_GUI)
    add_subdirectory(bitmap)
    if(TARGET synthyboy_clap)
        target_link_libraries(synthyboy_clap PRIVATE synthyboy_bitmap_gui)
        target_compile_definitions(synthyboy_clap PRIVATE SYNTHYBOY_BITMAP_GUI=1)
        synthyboy_bitmap_module_options(synthyboy_clap)
    endif()
endif()

# Target: synthyboy_static_ui (docs/ARCHITECTURE.md 4.3, docs/BITMAP-GUI-EXTENSION.md section 14): the same plug-in as synthyboy_static, with the bitmap GUI, for an app that links it statically
# and has a JUCE of its own (the Kick app on iOS: JUCE 7). Everything JUCE is built in the namespace SYNTHYBOY_JUCE_NAMESPACE (cmake/JuceNamespace.cmake), the Objective-C classes of JUCE carry its
# prefix, the fonts are embedded, the renderer is JUCE's software renderer, nothing is dlopen'd, and the only symbol the archive exports is synthyboy_clap_get_entry (scripts/check-static-ui-symbols.py).
# It replaces synthyboy_static in the link, it is not linked next to it. The libraries below it (synthyboy_core and its packages: no JUCE) are the same as for synthyboy_static.
if(SYNTHYBOY_STATIC_UI)
    if(NOT APPLE)
        message(FATAL_ERROR "SYNTHYBOY_STATIC_UI is implemented for Apple platforms (iOS); no other static host asked for it")
    endif()
    add_library(synthyboy_static_ui_parts STATIC
        synthyboy_clap.cpp
        synthyboy_clap.h
        synthyboy_plugin.cpp
        synthyboy_plugin.h
        synthyboy_ui_link.h
        synthyboy_content_service.cpp
        synthyboy_content_service.h)
    target_include_directories(synthyboy_static_ui_parts
        PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
                "${CLAP_INCLUDE_DIR}"
        PRIVATE "${CMAKE_SOURCE_DIR}/core/include")
    target_link_libraries(synthyboy_static_ui_parts PUBLIC synthyboy_core PRIVATE synthyboy_bitmap_gui)
    target_compile_features(synthyboy_static_ui_parts PUBLIC cxx_std_17)
    target_compile_definitions(synthyboy_static_ui_parts PRIVATE SYNTHYBOY_BITMAP_GUI=1 SYNTHYBOY_STATIC_ONLY=1)
    target_compile_options(synthyboy_static_ui_parts PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(synthyboy_static_ui_parts PROPERTIES POSITION_INDEPENDENT_CODE ON)
    synthyboy_bitmap_module_options(synthyboy_static_ui_parts)
    # the finished library: libsynthyboy_static_ui.a (build directory shell-clap/), the one that goes into the app; the parts are not
    include("${PROJECT_SOURCE_DIR}/cmake/StaticUi.cmake")
    synthyboy_add_static_ui(synthyboy_static_ui synthyboy_static_ui_parts ARCHIVES synthyboy_ui_model EXPORT synthyboy_clap_get_entry)
endif()

# On Windows the module exports clap_entry (and only that: SYNTHYBOY_CLAP_API is __declspec(dllexport) with this define)
if(WIN32 AND TARGET synthyboy_clap)
    target_compile_definitions(synthyboy_clap PRIVATE SYNTHYBOY_CLAP_EXPORT=1)
endif()
