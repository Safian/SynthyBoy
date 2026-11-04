# SynthyBoyVST - JUCE-free core library (engine interface, parameters, state, own engine) and its tests.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_core STATIC
    src/number.cpp
    src/params.cpp
    src/state.cpp
    engine-own/own_engine.cpp)

target_include_directories(synthyboy_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/engine-own")

target_compile_features(synthyboy_core PUBLIC cxx_std_17)
set_target_properties(synthyboy_core PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(synthyboy_core PRIVATE /W4)
else()
    target_compile_options(synthyboy_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Vital-derived code (docs/ARCHITECTURE.md 2.2) will live only in engine-vital/; nothing is added yet.
if(SYNTHYBOY_WITH_VITAL)
    message(STATUS "SYNTHYBOY_WITH_VITAL=ON: engine-vital is empty in this phase, nothing added")
else()
    message(STATUS "SYNTHYBOY_WITH_VITAL=OFF: own engine only")
endif()

if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
