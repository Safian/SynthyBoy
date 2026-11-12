# SynthyBoy - own-oracle: black-box comparison of engine-own against engine-vital through IEngine (levels, peaks, aliasing, envelope times).
# Added by the top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS and SYNTHYBOY_WITH_VITAL are ON. It uses only the public interface of both engines.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_own_oracle own_oracle.cpp)
target_link_libraries(synthyboy_own_oracle PRIVATE synthyboy_core)
set_target_properties(synthyboy_own_oracle PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_own_oracle PRIVATE /W4)
else()
    target_compile_options(synthyboy_own_oracle PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(synthyboy_osc_oracle osc_oracle.cpp)
target_link_libraries(synthyboy_osc_oracle PRIVATE synthyboy_core)
target_include_directories(synthyboy_osc_oracle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(synthyboy_osc_oracle PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_osc_oracle PRIVATE /W4)
else()
    target_compile_options(synthyboy_osc_oracle PRIVATE -Wall -Wextra -Wpedantic)
endif()
