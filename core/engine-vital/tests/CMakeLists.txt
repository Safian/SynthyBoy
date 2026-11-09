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

synthyboy_add_test(synthyboy_test_vital_wtremap test_wtremap.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_wtremap PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_fxextra test_fxextra.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_fxextra PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_lfovoice test_lfovoice.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_lfovoice PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_pitchuni test_pitchuni.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_pitchuni PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_oscextra test_oscextra.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_oscextra PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_wp_voiceglb test_wp_voiceglb.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_wp_voiceglb PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")
synthyboy_add_test(synthyboy_test_vital_modamt test_modamt.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_modamt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_warpenv test_warpenv.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_warpenv PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_fxinstances test_fxinstances.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_fxinstances PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_fxinstances_rt test_fxinstances_rt.cpp   # replaces global operator new / delete; also run under ThreadSanitizer
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_fxinstances_rt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

# the ported unit tests of the Vital-derived DSP (tests/synthesis/**, translated from upstream's tests/synthesis)
file(GLOB_RECURSE SBVITAL_PORTED_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/synthesis/*.cpp")
synthyboy_add_test(synthyboy_test_vital_dsp test_vital_dsp.cpp ${SBVITAL_PORTED_TESTS}
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_dsp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/synthesis")
set_source_files_properties(${SBVITAL_PORTED_TESTS} PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")

synthyboy_add_test(synthyboy_test_vital_noisesample test_noisesample.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_noisesample PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_mpe test_mpe.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_mpe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_voicecontrol test_voicecontrol.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_voicecontrol PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_silence test_silence.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_silence PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_readout test_readout.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_readout PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_oscextra2 test_oscextra2.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_oscextra2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")

synthyboy_add_test(synthyboy_test_vital_modsources test_modsources.cpp
    LINK synthyboy_engine_vital)
target_include_directories(synthyboy_test_vital_modsources PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter")
