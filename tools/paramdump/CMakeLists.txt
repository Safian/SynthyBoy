# SynthyBoy - paramdump: writes the whole parameter table as JSON (docs/params.json). Added by the top level CMakeLists.txt
# when SYNTHYBOY_BUILD_TESTS is ON, so it is built whenever the tests are.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_paramdump paramdump.cpp)
target_link_libraries(synthyboy_paramdump PRIVATE synthyboy_core)
target_include_directories(synthyboy_paramdump PRIVATE "${PROJECT_SOURCE_DIR}/core/src") # number.h: the exact float formatting
set_target_properties(synthyboy_paramdump PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_paramdump PRIVATE /W4)
else()
    target_compile_options(synthyboy_paramdump PRIVATE -Wall -Wextra -Wpedantic)
endif()

# A smoke test: the dump runs and starts like the document the generator script expects (the real check, that docs/params.json
# is up to date, is the CI step "parameter documentation is up to date").
add_test(NAME synthyboy_paramdump COMMAND synthyboy_paramdump)
set_tests_properties(synthyboy_paramdump PROPERTIES PASS_REGULAR_EXPRESSION "^\\{\"format\":\"synthyboy-params\"")

add_test(NAME synthyboy_paramdump_kick_pages COMMAND synthyboy_paramdump --kick-pages)
set_tests_properties(synthyboy_paramdump_kick_pages PROPERTIES PASS_REGULAR_EXPRESSION "\"format\": \"synthyboy-kick-pages\"")
