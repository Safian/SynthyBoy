# SynthyBoy - wavetable editor core library (data model, operations, FFT, import, export).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_wtedit STATIC
    src/fft.cpp
    src/wavetable.cpp
    src/drawing_tools.cpp
    src/frame_ops.cpp
    src/vital_model.cpp
    src/undo.cpp
    src/audio_import.cpp
    src/image_import.cpp
    src/wav_export.cpp
    src/player.cpp)

target_include_directories(synthyboy_wtedit
    PUBLIC  "${PROJECT_SOURCE_DIR}/core/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(synthyboy_wtedit PUBLIC cxx_std_17)
set_target_properties(synthyboy_wtedit PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(synthyboy_wtedit PRIVATE /W4)
else()
    target_compile_options(synthyboy_wtedit PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(synthyboy_core PUBLIC synthyboy_wtedit)

if(SYNTHYBOY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(synthyboy_test_wtedit
        tests/test_wtedit.cpp
        "${PROJECT_SOURCE_DIR}/core/tests/test_main.cpp")
    target_link_libraries(synthyboy_test_wtedit PRIVATE synthyboy_wtedit synthyboy_core Threads::Threads)
    target_include_directories(synthyboy_test_wtedit PRIVATE
        "${PROJECT_SOURCE_DIR}/core/include"
        "${PROJECT_SOURCE_DIR}/core/tests")
    set_target_properties(synthyboy_test_wtedit PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(synthyboy_test_wtedit PRIVATE /W4)
    else()
        target_compile_options(synthyboy_test_wtedit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME synthyboy_test_wtedit COMMAND synthyboy_test_wtedit)
endif()
