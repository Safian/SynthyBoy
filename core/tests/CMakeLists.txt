# SynthyBoyVST - core tests (own runner, registered with CTest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Threads REQUIRED)

function(synthyboy_add_test name)
    add_executable(${name} ${ARGN} test_main.cpp)
    target_link_libraries(${name} PRIVATE synthyboy_core Threads::Threads)
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/core/src")
    set_target_properties(${name} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

synthyboy_add_test(synthyboy_test_params  test_params.cpp)
synthyboy_add_test(synthyboy_test_state   test_state.cpp)
synthyboy_add_test(synthyboy_test_engine  test_engine.cpp)
synthyboy_add_test(synthyboy_test_rt      test_rt_alloc.cpp)   # replaces global operator new/delete
synthyboy_add_test(synthyboy_test_threads test_threads.cpp)    # run under ThreadSanitizer in CI
