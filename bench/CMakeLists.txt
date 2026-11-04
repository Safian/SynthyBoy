# SynthyBoy - benchmark (real-time factor and block times of an engine). Added by the top level CMakeLists.txt when
# SYNTHYBOY_BUILD_TESTS is ON.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_bench bench_engine.cpp)
target_link_libraries(synthyboy_bench PRIVATE synthyboy_core)
set_target_properties(synthyboy_bench PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_bench PRIVATE /W4)
else()
    target_compile_options(synthyboy_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()
