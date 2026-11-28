# SynthyBoy - param-audit: the parameter effect audit (docs/ui-design/PARAM-AUDIT-REPORT.md). Added by the top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS and
# the tests are part of the build (the own engine is the engine under test).
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

