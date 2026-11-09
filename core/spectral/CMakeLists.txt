# SynthyBoy - core/spectral: the spectral oscillator DSP (STFT resynthesis of a sample) and its unit tests.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_spectral STATIC src/spectral.cpp)
target_include_directories(synthyboy_spectral PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(synthyboy_spectral PUBLIC synthyboy_sampleosc)
target_compile_features(synthyboy_spectral PUBLIC cxx_std_17)
set_target_properties(synthyboy_spectral PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_spectral PRIVATE /W4)
else()
    target_compile_options(synthyboy_spectral PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(synthyboy_test_spectral tests/test_spectral.cpp "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_link_libraries(synthyboy_test_spectral PRIVATE synthyboy_spectral synthyboy_core Threads::Threads)
    target_include_directories(synthyboy_test_spectral PRIVATE "${PROJECT_SOURCE_DIR}/core/include" "${PROJECT_SOURCE_DIR}/core/tests")
    set_target_properties(synthyboy_test_spectral PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_spectral PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_spectral PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_spectral COMMAND synthyboy_test_spectral)
endif()
