# SynthyBoy - JUCE-free core library (engine interface, parameters, state, own engine) and its tests.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_core STATIC
    src/number.cpp
    src/engine_factory.cpp
    src/params.cpp
    src/param_table.cpp
    src/modulation.cpp
    src/state.cpp
    src/content_state.cpp
    src/engine_caps.cpp
    src/sha256.cpp
    src/preset.cpp
    src/content_pack.cpp
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

# Vital-derived code (docs/ARCHITECTURE.md 2.2) lives only in engine-vital/; with SYNTHYBOY_WITH_VITAL=OFF
# (iOS) nothing of it is added to the build.
if(SYNTHYBOY_WITH_VITAL)
    message(STATUS "SYNTHYBOY_WITH_VITAL=ON: Vital-derived engine is built (core/engine-vital)")
    add_subdirectory(engine-vital)
    # the adapter (our code, strict warnings like the rest of the core) is compiled into the core; the imported DSP is
    # the separate static library
    target_sources(synthyboy_core PRIVATE engine-vital/adapter/vital_engine.cpp engine-vital/adapter/vital_bindings.cpp
                                          engine-vital/adapter/vital_tables.cpp)
    target_include_directories(synthyboy_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/engine-vital/adapter")
    target_link_libraries(synthyboy_core PRIVATE synthyboy_engine_vital)
    target_compile_definitions(synthyboy_core PUBLIC SYNTHYBOY_HAS_VITAL=1)
else()
    message(STATUS "SYNTHYBOY_WITH_VITAL=OFF: own engine only")
    target_compile_definitions(synthyboy_core PUBLIC SYNTHYBOY_HAS_VITAL=0)
endif()

if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(tests)
    if(SYNTHYBOY_WITH_VITAL)
        add_subdirectory(engine-vital/tests)
    endif()
endif()
