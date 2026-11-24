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

# The pair kernels (Filter::processPair): the two filters of a stereo pair in one pass must give the samples of two process () calls to the last bit. The test has its
# own copy of the library's sources, compiled without fused multiply-add (as the ARM builds that use the kernels are), and with the NEON lanes switched on where the
# CPU of the build has them (Apple's arm64 too, where the library itself runs the scalar path).
add_executable(synthyboy_test_filter_pair
    test_filter_pair.cpp
    "${PROJECT_SOURCE_DIR}/core/filters-serum/src/filter.cpp"
    "${PROJECT_SOURCE_DIR}/core/filters-serum/src/filter_types.cpp"
    "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
target_include_directories(synthyboy_test_filter_pair PRIVATE
    "${PROJECT_SOURCE_DIR}/core/tests"
    "${PROJECT_SOURCE_DIR}/core/filters-serum/include"
    "${PROJECT_SOURCE_DIR}/core/filters-serum/src")
target_compile_features(synthyboy_test_filter_pair PRIVATE cxx_std_17)
set_target_properties(synthyboy_test_filter_pair PROPERTIES CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)
target_link_libraries(synthyboy_test_filter_pair PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(synthyboy_test_filter_pair PRIVATE /W4 /fp:precise)
else()
    target_compile_options(synthyboy_test_filter_pair PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    target_compile_definitions(synthyboy_test_filter_pair PRIVATE SYNTHYBOY_FILTER_TEST_NEON=1)
endif()
add_test(NAME synthyboy_test_filter_pair COMMAND synthyboy_test_filter_pair)
