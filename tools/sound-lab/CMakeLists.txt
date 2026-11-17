# SynthyBoy - sound-lab: scripted offline renders of one preset under a device profile (for the sound design measurements of scripts/sound-lab.py).
# Added by the top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS is ON.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_sound_lab sound_lab.cpp)
target_link_libraries(synthyboy_sound_lab PRIVATE synthyboy_core)
target_include_directories(synthyboy_sound_lab PRIVATE "${PROJECT_SOURCE_DIR}/shell-mpc")
set_target_properties(synthyboy_sound_lab PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_sound_lab PRIVATE /W4)
else()
    target_compile_options(synthyboy_sound_lab PRIVATE -Wall -Wextra -Wpedantic)
endif()
