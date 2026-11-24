# SynthyBoy - core/granular: the granular oscillator DSP (grain clouds on a sample) and its unit tests.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_granular STATIC src/granular.cpp)
target_include_directories(synthyboy_granular PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(synthyboy_granular PUBLIC synthyboy_sampleosc)
target_compile_features(synthyboy_granular PUBLIC cxx_std_17)
set_target_properties(synthyboy_granular PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_granular PRIVATE /W4)
else()
    target_compile_options(synthyboy_granular PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(synthyboy_test_granular tests/test_granular.cpp "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_link_libraries(synthyboy_test_granular PRIVATE synthyboy_granular synthyboy_core Threads::Threads)
    target_include_directories(synthyboy_test_granular PRIVATE "${PROJECT_SOURCE_DIR}/core/include" "${PROJECT_SOURCE_DIR}/core/tests")
    set_target_properties(synthyboy_test_granular PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_granular PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_granular PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_granular COMMAND synthyboy_test_granular)
endif()
