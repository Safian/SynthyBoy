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

# What a profile step changes in the sound (bench/profile_compare.cpp): level and spectrum differences of every factory preset between two profiles.
add_executable(synthyboy_profile_compare profile_compare.cpp)
target_link_libraries(synthyboy_profile_compare PRIVATE synthyboy_core)
target_include_directories(synthyboy_profile_compare PRIVATE "${PROJECT_SOURCE_DIR}/shell-mpc")
set_target_properties(synthyboy_profile_compare PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_profile_compare PRIVATE /W4)
else()
    target_compile_options(synthyboy_profile_compare PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The power gain of one effect module on pink noise, the calibration of the tier level makeup (bench/fx_makeup_calibrate.cpp, scripts/tier-makeup-calibrate.py).
add_executable(synthyboy_fx_makeup_calibrate fx_makeup_calibrate.cpp)
target_link_libraries(synthyboy_fx_makeup_calibrate PRIVATE synthyboy_core Threads::Threads)
target_include_directories(synthyboy_fx_makeup_calibrate PRIVATE "${PROJECT_SOURCE_DIR}/core/engine-own")
set_target_properties(synthyboy_fx_makeup_calibrate PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_fx_makeup_calibrate PRIVATE /W4)
else()
    target_compile_options(synthyboy_fx_makeup_calibrate PRIVATE -Wall -Wextra -Wpedantic)
endif()
