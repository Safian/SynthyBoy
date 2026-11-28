# SynthyBoy - tests of the release scripts (package-release.py, fill-manifest.py, check-manifest.py) and of plugin.json. Added by the
# top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS is ON.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME synthyboy_test_release_scripts
             COMMAND "${Python3_EXECUTABLE}" -I -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -v)
    # label `slow` (more than about 5 s alone: `ctest -LE slow` is the quick set, docs/dsp-knowledge/testing.md)
    set_tests_properties(synthyboy_test_release_scripts PROPERTIES LABELS slow)

    # the checked-in plugin.json against schema/plugin-manifest.schema.json, CMakeLists.txt and the content packs it declares
    add_test(NAME synthyboy_manifest_check
             COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/scripts/check-manifest.py")
endif()
