# SynthyBoy - vital-import: command line front end of the Vital preset importer (.vital -> .sbpreset + <name>.report.json).
# Added by the top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS is ON.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_vital_import vital_import_cli.cpp)
target_link_libraries(synthyboy_vital_import PRIVATE synthyboy_core)
set_target_properties(synthyboy_vital_import PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_vital_import PRIVATE /W4)
else()
    target_compile_options(synthyboy_vital_import PRIVATE -Wall -Wextra -Wpedantic)
endif()
