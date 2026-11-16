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

add_executable(synthyboy_noteon_bench noteon_bench.cpp)
target_link_libraries(synthyboy_noteon_bench PRIVATE synthyboy_core)
set_target_properties(synthyboy_noteon_bench PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_noteon_bench PRIVATE /W4)
else()
    target_compile_options(synthyboy_noteon_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Which quality profile a device can run (bench/tier_bench.cpp; the profiles are those of shell-mpc/device_profile.h). SYNTHYBOY_TIER_BENCH_STATIC=ON links it fully
# static (cross builds for the MPC: the binary then needs nothing of the device but the kernel).
add_executable(synthyboy_tier_bench tier_bench.cpp)
target_link_libraries(synthyboy_tier_bench PRIVATE synthyboy_core)
target_include_directories(synthyboy_tier_bench PRIVATE "${PROJECT_SOURCE_DIR}/shell-mpc")
set_target_properties(synthyboy_tier_bench PROPERTIES CXX_EXTENSIONS OFF)
option(SYNTHYBOY_TIER_BENCH_STATIC "Link synthyboy_tier_bench fully static (Linux cross builds for the MPC)" OFF)
if(SYNTHYBOY_TIER_BENCH_STATIC)
    target_link_options(synthyboy_tier_bench PRIVATE -static)
endif()
if(MSVC)
    target_compile_options(synthyboy_tier_bench PRIVATE /W4)
else()
    target_compile_options(synthyboy_tier_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()
