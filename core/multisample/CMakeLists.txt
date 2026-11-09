# SynthyBoy - core/multisample: the multisample oscillator (SFZ instruments, zone choice, layer voices with the SFZ / override envelope) and its unit tests.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_multisample STATIC src/multisample.cpp)
target_include_directories(synthyboy_multisample PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(synthyboy_multisample PUBLIC synthyboy_sampleosc)
target_compile_features(synthyboy_multisample PUBLIC cxx_std_17)
set_target_properties(synthyboy_multisample PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_multisample PRIVATE /W4)
else()
    target_compile_options(synthyboy_multisample PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(synthyboy_test_multisample tests/test_multisample.cpp "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_link_libraries(synthyboy_test_multisample PRIVATE synthyboy_multisample synthyboy_core Threads::Threads)
    target_include_directories(synthyboy_test_multisample PRIVATE "${PROJECT_SOURCE_DIR}/core/include" "${PROJECT_SOURCE_DIR}/core/tests")
    set_target_properties(synthyboy_test_multisample PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_multisample PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_multisample PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_multisample COMMAND synthyboy_test_multisample)
endif()
