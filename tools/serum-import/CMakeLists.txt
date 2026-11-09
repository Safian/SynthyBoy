# SynthyBoy - serum-import: command line front end of the Serum 2 importer (.SerumPreset -> .sbpreset + <name>.report.json).
# Added by the top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS is ON.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_serum_import serum_import_cli.cpp)
target_link_libraries(synthyboy_serum_import PRIVATE synthyboy_core)
set_target_properties(synthyboy_serum_import PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_serum_import PRIVATE /W4)
else()
    target_compile_options(synthyboy_serum_import PRIVATE -Wall -Wextra -Wpedantic)
endif()
