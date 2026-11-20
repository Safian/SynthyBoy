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

# The defaults of synthyboy_bench are those of the devices: 44.1 kHz and flush-to-zero on (the header line prints "engine own, 44100 Hz").
add_test(NAME synthyboy_bench_defaults COMMAND synthyboy_bench --engine own --patch default --seconds 0.3)
set_tests_properties(synthyboy_bench_defaults PROPERTIES PASS_REGULAR_EXPRESSION "own, 44100 Hz" TIMEOUT 120)

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
option(SYNTHYBOY_TIER_BENCH_STATIC_LIBCXX "Link the benchmarks with -static-libstdc++ -static-libgcc but dynamic glibc, like the plugin (libm of the device)" OFF)
if(MSVC)
    target_compile_options(synthyboy_tier_bench PRIVATE /W4)
else()
    target_compile_options(synthyboy_tier_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The libm of the device against the libm of the build (bench/libm_probe.cpp): links nothing of the engine; the same link rule as synthyboy_tier_bench.
add_executable(synthyboy_libm_probe libm_probe.cpp)
set_target_properties(synthyboy_libm_probe PROPERTIES CXX_EXTENSIONS OFF)
if(SYNTHYBOY_TIER_BENCH_STATIC)
    target_link_options(synthyboy_libm_probe PRIVATE -static)
    target_compile_definitions(synthyboy_libm_probe PRIVATE SB_LIBM_PROBE_LINK="static")
else()
    target_compile_definitions(synthyboy_libm_probe PRIVATE SB_LIBM_PROBE_LINK="dynamic")
    if(SYNTHYBOY_TIER_BENCH_STATIC_LIBCXX)
        target_link_options(synthyboy_tier_bench PRIVATE -static-libstdc++ -static-libgcc)
        target_link_options(synthyboy_libm_probe PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()
if(MSVC)
    target_compile_options(synthyboy_libm_probe PRIVATE /W4)
else()
    target_compile_options(synthyboy_libm_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The hardware performance counters (bench/pmu_probe.cpp, bench/perf/perf_pmu.h): links nothing of the engine; the same link rule as synthyboy_tier_bench.
add_executable(synthyboy_pmu_probe pmu_probe.cpp)
target_include_directories(synthyboy_pmu_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(synthyboy_pmu_probe PROPERTIES CXX_EXTENSIONS OFF)
if(SYNTHYBOY_TIER_BENCH_STATIC)
    target_link_options(synthyboy_pmu_probe PRIVATE -static)
elseif(SYNTHYBOY_TIER_BENCH_STATIC_LIBCXX)
    target_link_options(synthyboy_pmu_probe PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(MSVC)
    target_compile_options(synthyboy_pmu_probe PRIVATE /W4)
else()
    target_compile_options(synthyboy_pmu_probe PRIVATE -Wall -Wextra -Wpedantic)
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

# The cost of the math functions the engine calls and of the two clocks (bench/libm_probe.cpp; run it on the device, a static and a dynamic build of it give the
# difference of the two links).
add_test(NAME synthyboy_libm_probe COMMAND synthyboy_libm_probe --quick)
set_tests_properties(synthyboy_libm_probe PROPERTIES PASS_REGULAR_EXPRESSION "LIBM-PROBE-RESULT ok" TIMEOUT 120)

# The performance counters a device offers (bench/pmu_probe.cpp, bench/perf/perf_pmu.h): prints which events open and what a group reads; usable on any platform.
add_test(NAME synthyboy_pmu_probe COMMAND synthyboy_pmu_probe)
set_tests_properties(synthyboy_pmu_probe PROPERTIES PASS_REGULAR_EXPRESSION "PMU-PROBE-RESULT" TIMEOUT 120)

# The synthetic loads of docs/plans/distortion-antialiasing.md (task A3): dist:<type> and warp:<type>:<amount> are known load names (--list prints them).
add_test(NAME tier_bench_alias_load_dist COMMAND synthyboy_tier_bench --list --config low --load dist:1)
set_tests_properties(tier_bench_alias_load_dist PROPERTIES PASS_REGULAR_EXPRESSION "load +dist:1 " TIMEOUT 120)
add_test(NAME tier_bench_alias_load_warp COMMAND synthyboy_tier_bench --list --config low --load warp:1:35)
set_tests_properties(tier_bench_alias_load_warp PROPERTIES PASS_REGULAR_EXPRESSION "load +warp:1:35 " TIMEOUT 120)

# CLI smoke tests of the benchmarks, one line per test in bench/perf/cli_smoke.txt:  name~platform~arguments~regular expression
# platform: any | posix (not Windows) | linux;  @PRESETS@ = content/presets;  the test is synthyboy_tier_bench_<name>
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/perf/cli_smoke.txt" SYNTHYBOY_CLI_SMOKE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/perf/cli_smoke.txt")
foreach(line IN LISTS SYNTHYBOY_CLI_SMOKE)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    string(REPLACE "~" ";" fields "${line}")
    list(GET fields 0 smoke_name)
    list(GET fields 1 smoke_platform)
    list(GET fields 2 smoke_args)
    list(GET fields 3 smoke_regex)
    if((smoke_platform STREQUAL "linux" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux") OR (smoke_platform STREQUAL "posix" AND WIN32))
        continue()
    endif()
    string(REPLACE "@PRESETS@" "${PROJECT_SOURCE_DIR}/content/presets" smoke_args "${smoke_args}")
    separate_arguments(smoke_argv UNIX_COMMAND "${smoke_args}")
    add_test(NAME synthyboy_tier_bench_${smoke_name} COMMAND synthyboy_tier_bench ${smoke_argv})
    set_tests_properties(synthyboy_tier_bench_${smoke_name} PROPERTIES PASS_REGULAR_EXPRESSION "${smoke_regex}" TIMEOUT 120)
endforeach()
