# SynthyBoy - formula parser and wavetable frame evaluator.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

target_sources(synthyboy_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/formula.cpp
)

target_include_directories(synthyboy_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(synthyboy_test_formula
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_formula.cpp
        ${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp
    )
    target_link_libraries(synthyboy_test_formula PRIVATE synthyboy_core Threads::Threads)
    target_include_directories(synthyboy_test_formula PRIVATE
        "${PROJECT_SOURCE_DIR}/core/src"
        "${PROJECT_SOURCE_DIR}/core/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    set_target_properties(synthyboy_test_formula PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_formula PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_formula PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_formula COMMAND synthyboy_test_formula)
endif()
