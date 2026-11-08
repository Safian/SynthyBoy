# SynthyBoy - engine-vital: the Vital-derived DSP (core/engine-vital/upstream, see docs/UPSTREAM.md) built JUCE-free.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Added only when SYNTHYBOY_WITH_VITAL=ON (core/CMakeLists.txt). Nothing outside this directory contains Vital-derived code.
# synthyboy_engine_vital is the imported DSP alone (it depends on nothing of ours); the adapter in adapter/ is compiled into
# synthyboy_core, which links this library (core/CMakeLists.txt), so there is no cycle between the libraries.

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

add_library(synthyboy_engine_vital STATIC ${SBVITAL_UPSTREAM_SOURCES})

# SYSTEM: warnings inside the imported headers are not ours (keeps our adapter and tests at -Wall -Wextra -Wpedantic)
if(MSVC)
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
    target_compile_options(synthyboy_engine_vital PUBLIC /external:W0)
endif()
target_include_directories(synthyboy_engine_vital SYSTEM PUBLIC ${SBVITAL_INCLUDE_DIRS})
target_compile_features(synthyboy_engine_vital PUBLIC cxx_std_17)
set_target_properties(synthyboy_engine_vital PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

# Vital's VITAL_ASSERT checks (finite values, index ranges) are on in Debug builds only.
target_compile_definitions(synthyboy_engine_vital PUBLIC $<$<CONFIG:Debug>:DEBUG=1>)

# Only 32-bit x86 needs an explicit SSE2 switch (x86_64 has it; ARMv7 gets -mfpu=neon from the toolchain file).
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^i[3-6]86$")
    target_compile_options(synthyboy_engine_vital PUBLIC -msse2 -mfpmath=sse)
endif()

# The imported sources are upstream's: warnings off.
if(MSVC)
    target_compile_definitions(synthyboy_engine_vital PUBLIC _USE_MATH_DEFINES NOMINMAX)
    target_compile_options(synthyboy_engine_vital PRIVATE /w /bigobj)
else()
    target_compile_options(synthyboy_engine_vital PRIVATE -w)
endif()

# The sample oscillator type (producers/sample_oscillator.cpp) renders with the standalone core/sample-osc package, the filter models of state
# version 3 (modules/serum_filter_bank.cpp) with core/filters-serum, the Serum modules of the effect chain (modules/serum_fx_modules.cpp) with core/fx-serum-a and -b: our code in the fork,
# not Vital's.
target_link_libraries(synthyboy_engine_vital PUBLIC synthyboy_sampleosc synthyboy_filters_serum synthyboy_fx_serum_a synthyboy_fx_serum_b)
find_package(Threads REQUIRED)   # the builder thread of the Convolve module
target_link_libraries(synthyboy_engine_vital PUBLIC Threads::Threads)
