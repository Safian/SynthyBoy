# SynthyBoy - core/filters-serum: Serum filter collection.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_filters_serum STATIC
    src/filter.cpp
    src/filter_types.cpp
)

target_include_directories(synthyboy_filters_serum
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(synthyboy_filters_serum PUBLIC cxx_std_17)
set_target_properties(synthyboy_filters_serum PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(synthyboy_filters_serum PRIVATE /W4)
else()
    target_compile_options(synthyboy_filters_serum PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(synthyboy_core PUBLIC synthyboy_filters_serum)

if(SYNTHYBOY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
