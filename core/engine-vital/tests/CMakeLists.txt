# SynthyBoy - engine-vital tests (own runner, registered with CTest): the adapter's feature tests and the ported
# unit tests of the Vital-derived DSP. Added by core/CMakeLists.txt when SYNTHYBOY_WITH_VITAL=ON and tests are built.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

find_package(Threads REQUIRED)

synthyboy_add_test(synthyboy_test_vital_engine test_vital_engine.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_params test_vital_params.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_params PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_audio test_vital_audio.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_audio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")
# about 25 s in a release build: the audio quality gates (docs/QUALITY.md). CI runs them natively and under the sanitizers, and leaves them out
# of the QEMU (ARMv7) and ThreadSanitizer jobs: `ctest -LE slow`.
set_tests_properties(synthyboy_test_vital_audio PROPERTIES LABELS slow)

synthyboy_add_test(synthyboy_test_vital_wp123 test_wp123.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_wp123 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_wp4 test_wp4.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_wp4 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_wp5 test_wp5.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_wp5 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_wp6 test_wp6.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_wp6 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_wp7 test_wp7.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_wp7 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_lfovoice test_lfovoice.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_lfovoice PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

# the ported unit tests of the Vital-derived DSP (tests/synthesis/**, translated from upstream's tests/synthesis)
file(GLOB_RECURSE SBVITAL_PORTED_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/synthesis/*.cpp")
synthyboy_add_test(synthyboy_test_vital_dsp test_vital_dsp.cpp ${SBVITAL_PORTED_TESTS}
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_dsp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/synthesis")
set_source_files_properties(${SBVITAL_PORTED_TESTS} PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
