# SynthyBoy - core/filters-serum tests.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

if(COMMAND synthyboy_add_test)
    synthyboy_add_test(synthyboy_test_filters_serum test_filters_serum.cpp LINK synthyboy_filters_serum)
    target_include_directories(synthyboy_test_filters_serum PRIVATE "${PROJECT_SOURCE_DIR}/core/filters-serum/include")
else()
    find_package(Threads REQUIRED)
    add_executable(synthyboy_test_filters_serum
        test_filters_serum.cpp
        "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_link_libraries(synthyboy_test_filters_serum PRIVATE synthyboy_filters_serum synthyboy_core Threads::Threads)
    target_include_directories(synthyboy_test_filters_serum PRIVATE
        "${PROJECT_SOURCE_DIR}/core/tests"
        "${PROJECT_SOURCE_DIR}/core/filters-serum/include")
    set_target_properties(synthyboy_test_filters_serum PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_filters_serum PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_filters_serum PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_filters_serum COMMAND synthyboy_test_filters_serum)
endif()
