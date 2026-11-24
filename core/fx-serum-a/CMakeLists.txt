# SynthyBoy - core/fx-serum-a: Serum-exclusive FX modules (Hyper/Dimension, Utility, Splitters).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_fx_serum_a STATIC
    src/hyper_dimension.cpp
    src/utility.cpp
    src/splitter_lh.cpp
    src/splitter_lmh.cpp
    src/splitter_ms.cpp
    src/stereo_splitter.cpp
)

target_include_directories(synthyboy_fx_serum_a
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(synthyboy_fx_serum_a PUBLIC cxx_std_17)
set_target_properties(synthyboy_fx_serum_a PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(synthyboy_fx_serum_a PRIVATE /W4)
else()
    target_compile_options(synthyboy_fx_serum_a PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNTHYBOY_BUILD_TESTS)
    add_executable(synthyboy_test_fx_serum_a
        tests/test_fx_serum_a.cpp
        "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp"
    )
    target_link_libraries(synthyboy_test_fx_serum_a PRIVATE synthyboy_fx_serum_a)
    target_include_directories(synthyboy_test_fx_serum_a PRIVATE
        "${PROJECT_SOURCE_DIR}/core/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    set_target_properties(synthyboy_test_fx_serum_a PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_fx_serum_a PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_fx_serum_a PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_fx_serum_a COMMAND synthyboy_test_fx_serum_a)
endif()
