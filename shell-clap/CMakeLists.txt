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

# Target: synthyboy_clap (dynamic CLAP module .clap for standalone CLAP hosts like Kick)
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

if(MSVC)
    target_compile_options(synthyboy_static PRIVATE /W4)
    target_compile_options(synthyboy_clap PRIVATE /W4)
else()
    target_compile_options(synthyboy_static PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(synthyboy_clap PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The bitmap GUI (docs/BITMAP-GUI-EXTENSION.md): the module renders shell-juce/ui itself and exports it through com.safian.bitmap-gui/1. Only the .clap has it, never the static library.
if(SYNTHYBOY_BITMAP_GUI)
    add_subdirectory(bitmap)
    target_link_libraries(synthyboy_clap PRIVATE synthyboy_bitmap_gui)
    target_compile_definitions(synthyboy_clap PRIVATE SYNTHYBOY_BITMAP_GUI=1)
    synthyboy_bitmap_module_options(synthyboy_clap)
endif()

# On Windows the module exports clap_entry (and only that: SYNTHYBOY_CLAP_API is __declspec(dllexport) with this define)
if(WIN32)
    target_compile_definitions(synthyboy_clap PRIVATE SYNTHYBOY_CLAP_EXPORT=1)
endif()
