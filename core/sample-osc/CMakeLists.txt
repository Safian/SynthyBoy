# SynthyBoy - core/sample-osc: standalone sample oscillator DSP, non-RT loader, and unit tests.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_sampleosc STATIC
    src/sample_data.cpp
    src/sample_voice.cpp
    src/wav_reader.cpp
)

target_include_directories(synthyboy_sampleosc
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(synthyboy_sampleosc PUBLIC cxx_std_17)
set_target_properties(synthyboy_sampleosc PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(synthyboy_sampleosc PRIVATE /W4)
else()
    target_compile_options(synthyboy_sampleosc PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(synthyboy_test_sampleosc
        tests/test_sample_osc.cpp
        "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp"
    )
    target_link_libraries(synthyboy_test_sampleosc PRIVATE synthyboy_sampleosc synthyboy_core Threads::Threads)
    target_include_directories(synthyboy_test_sampleosc PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/core/include"
        "${PROJECT_SOURCE_DIR}/core/src"
        "${PROJECT_SOURCE_DIR}/core/tests"
    )
    set_target_properties(synthyboy_test_sampleosc PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_sampleosc PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_sampleosc PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_sampleosc COMMAND synthyboy_test_sampleosc)
endif()
