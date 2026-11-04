# SynthyBoy - engine-vital: the Vital-derived DSP (core/engine-vital/upstream, see docs/UPSTREAM.md) built JUCE-free.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Added only when SYNTHYBOY_WITH_VITAL=ON (core/CMakeLists.txt). Nothing outside this directory contains Vital-derived code.

file(GLOB_RECURSE SBVITAL_UPSTREAM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src/*.cpp")

set(SBVITAL_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/third_party"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src/common/wavetable"
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src/interface/look_and_feel")
# the imported sources include each other by bare file name, from every synthesis sub directory
file(GLOB SBVITAL_SYNTHESIS_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src/synthesis/*")
foreach(dir IN LISTS SBVITAL_SYNTHESIS_DIRS)
    if(IS_DIRECTORY "${dir}")
        list(APPEND SBVITAL_INCLUDE_DIRS "${dir}")
    endif()
endforeach()

set(SBVITAL_ADAPTER_SOURCES)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/adapter/vital_engine.cpp")
    list(APPEND SBVITAL_ADAPTER_SOURCES adapter/vital_engine.cpp)
endif()

add_library(synthyboy_engine_vital STATIC ${SBVITAL_UPSTREAM_SOURCES} ${SBVITAL_ADAPTER_SOURCES})

target_include_directories(synthyboy_engine_vital PUBLIC ${SBVITAL_INCLUDE_DIRS})
target_include_directories(synthyboy_engine_vital PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
target_compile_features(synthyboy_engine_vital PUBLIC cxx_std_17)
set_target_properties(synthyboy_engine_vital PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

# Vital's VITAL_ASSERT checks (finite values, index ranges) are on in Debug builds only.
target_compile_definitions(synthyboy_engine_vital PUBLIC $<$<CONFIG:Debug>:DEBUG=1>)

# Only 32-bit x86 needs an explicit SSE2 switch (x86_64 has it; ARMv7 gets -mfpu=neon from the toolchain file).
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^i[3-6]86$")
    target_compile_options(synthyboy_engine_vital PUBLIC -msse2 -mfpmath=sse)
endif()

# Imported sources: warnings off (they are upstream's, not ours). Our adapter files keep the strict set.
if(MSVC)
    target_compile_options(synthyboy_engine_vital PRIVATE /W4 /bigobj)
    set_source_files_properties(${SBVITAL_UPSTREAM_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
else()
    target_compile_options(synthyboy_engine_vital PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(${SBVITAL_UPSTREAM_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# The adapter implements synthyboy::IEngine and uses the parameter table: link the core (a cycle between static
# libraries is fine, the core only references createVitalEngine()).
target_link_libraries(synthyboy_engine_vital PUBLIC synthyboy_core)

# core <-> engine-vital is a cycle of static libraries; CMake repeats them on the link line and Apple's ld warns about it.
if(APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" SBVITAL_LD_NO_WARN_DUPLICATES)
    if(SBVITAL_LD_NO_WARN_DUPLICATES)
        target_link_options(synthyboy_engine_vital INTERFACE "LINKER:-no_warn_duplicate_libraries")
    endif()
endif()
