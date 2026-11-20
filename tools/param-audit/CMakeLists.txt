# SynthyBoy - param-audit: the parameter effect audit (docs/ui-design/PARAM-AUDIT-REPORT.md). Added by the top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS and
# SYNTHYBOY_WITH_VITAL are ON (the Vital-derived engine is the engine under test; the iOS / no-Vital build has neither the tool nor the tests).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_param_audit_lib STATIC rig.cpp metrics.cpp contexts.cpp)
target_link_libraries(synthyboy_param_audit_lib PUBLIC synthyboy_core)
target_include_directories(synthyboy_param_audit_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(synthyboy_param_audit_lib PROPERTIES CXX_EXTENSIONS OFF)

add_executable(synthyboy_param_audit param_audit.cpp)
target_link_libraries(synthyboy_param_audit PRIVATE synthyboy_param_audit_lib)
set_target_properties(synthyboy_param_audit PROPERTIES CXX_EXTENSIONS OFF)

foreach(target synthyboy_param_audit_lib synthyboy_param_audit)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

# Stage 2: the semantic checks (own test runner of core/tests). Label `slow`: every test sweeps whole ranges (hundreds of renders).
synthyboy_add_test(synthyboy_test_param_semantics test_param_semantics.cpp LINK synthyboy_param_audit_lib)
set_tests_properties(synthyboy_test_param_semantics PROPERTIES LABELS "slow;audit" TIMEOUT 1800)

# Stage 1 as a gate: the quick run (the representative values of every key of instance 1 and of the last instance of every family, plus the modulation sweep of every
# modulatable key) against the committed baseline docs/ui-design/param-audit-baseline.json. Label `audit` (the CI job `param-audit` runs it; the build matrix leaves it out with
# `-LE audit`) and `slow`. The full run (every value of every key) is the same script without --quick: add -DSYNTHYBOY_PARAM_AUDIT_FULL=ON to register it (hours).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND NOT WIN32)
    add_test(NAME synthyboy_param_audit_scan
             COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/run-param-audit.sh" --quick --no-build --no-semantics --jobs 4
                     --build-dir "${CMAKE_BINARY_DIR}" --out "${CMAKE_BINARY_DIR}/param-audit/ctest-quick")
    set_tests_properties(synthyboy_param_audit_scan PROPERTIES LABELS "slow;audit" TIMEOUT 3600)
    option(SYNTHYBOY_PARAM_AUDIT_FULL "Register the full parameter audit (every value of every key, hours) as a ctest" OFF)
    if(SYNTHYBOY_PARAM_AUDIT_FULL)
        add_test(NAME synthyboy_param_audit_scan_full
                 COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/run-param-audit.sh" --full --no-build --no-semantics
                         --build-dir "${CMAKE_BINARY_DIR}" --out "${CMAKE_BINARY_DIR}/param-audit/ctest-full")
        set_tests_properties(synthyboy_param_audit_scan_full PROPERTIES LABELS "slow;audit-full" TIMEOUT 86400)
    endif()
    # the mutation proof: seven injected faults must all be caught
    add_test(NAME synthyboy_param_audit_selftest
             COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/run-param-audit.sh" --self-test --no-build --build-dir "${CMAKE_BINARY_DIR}")
    set_tests_properties(synthyboy_param_audit_selftest PROPERTIES LABELS "slow;audit" TIMEOUT 1800)
endif()
