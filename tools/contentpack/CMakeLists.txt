# SynthyBoy - contentpack: the ContentPack build / verify / split tool (a Python script, stdlib only) and its tests. Added by the
# top level CMakeLists.txt when SYNTHYBOY_BUILD_TESTS is ON. Usage: tools/contentpack/contentpack.py --help
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    # unit tests of the tool (temporary packs; the real factory pack builds, verifies and is reproducible)
    add_test(NAME synthyboy_test_contentpack
             COMMAND "${Python3_EXECUTABLE}" -I -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)

    # the shipped factory pack is exactly what its contentpack.json says (every file, SHA-256 and size, nothing unlisted)
    add_test(NAME synthyboy_factory_pack_verifies
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/contentpack.py" verify
                     --dir "${PROJECT_SOURCE_DIR}/content/presets")

    # the factory content packs of sound set batch 2 (wavetables; samples, instruments, impulse responses) are exactly what their manifests say
    foreach(pack factory-wavetables factory-samples)
        add_test(NAME synthyboy_${pack}_verifies
                 COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/contentpack.py" verify
                         --dir "${PROJECT_SOURCE_DIR}/content/${pack}")
    endforeach()

    # ... and exactly what scripts/gen-content-pack.py makes (numpy: the files are reproducible with the numpy they were made with; skipped where numpy is missing)
    execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import numpy" RESULT_VARIABLE synthyboy_numpy_missing OUTPUT_QUIET ERROR_QUIET)
    if(NOT synthyboy_numpy_missing)
        add_test(NAME synthyboy_factory_content_reproducible
                 COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/gen-content-pack.py"
                         --check --out-root "${PROJECT_SOURCE_DIR}/content")
    endif()
endif()
