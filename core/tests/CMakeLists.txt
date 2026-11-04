# SynthyBoy - core tests (own runner, registered with CTest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Threads REQUIRED)

# Also used by core/engine-vital/tests (the Vital-specific tests), which link the Vital library as well:
#   synthyboy_add_test(name sources... [LINK libs...])
function(synthyboy_add_test name)
    cmake_parse_arguments(ARG "" "" "LINK" ${ARGN})
    add_executable(${name} ${ARG_UNPARSED_ARGUMENTS} "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_link_libraries(${name} PRIVATE synthyboy_core Threads::Threads ${ARG_LINK})
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/core/src" "${PROJECT_SOURCE_DIR}/core/tests")
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
synthyboy_add_test(synthyboy_test_preset  test_preset.cpp)
synthyboy_add_test(synthyboy_test_engine  test_engine.cpp)
synthyboy_add_test(synthyboy_test_rt      test_rt_alloc.cpp)   # replaces global operator new/delete
synthyboy_add_test(synthyboy_test_threads test_threads.cpp)    # run under ThreadSanitizer in CI
