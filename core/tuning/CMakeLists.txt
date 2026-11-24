# SynthyBoy - core/tuning: microtonal tuning package (SCL, KBM, TUN, MTS-ESP seam).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

target_sources(synthyboy_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tuning.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scl_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kbm_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tun_parser.cpp"
)

target_include_directories(synthyboy_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(synthyboy_test_tuning
        test_tuning.cpp
        "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp"
    )
    target_link_libraries(synthyboy_test_tuning PRIVATE
        synthyboy_core
        Threads::Threads
    )
    target_include_directories(synthyboy_test_tuning PRIVATE
        "${PROJECT_SOURCE_DIR}/core/src"
        "${PROJECT_SOURCE_DIR}/core/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set_target_properties(synthyboy_test_tuning PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_tuning PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_tuning PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_tuning COMMAND synthyboy_test_tuning)
endif()
