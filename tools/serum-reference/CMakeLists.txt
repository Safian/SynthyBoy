# SynthyBoy - serum-reference: a small JUCE host that loads the real Serum 2 plug-in (installed on the machine, never shipped) and renders a .SerumPreset
# offline with a deterministic note script (reference audio for tools/ref-compare). macOS only: added by the top level CMakeLists.txt when JUCE and the tests are
# part of the build; not built on other platforms, in the iOS build or in the build without Vital.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_executable(synthyboy_serum_reference serum_reference.cpp)
target_link_libraries(synthyboy_serum_reference PRIVATE
    synthyboy_core
    juce::juce_audio_processors juce::juce_audio_formats juce::juce_audio_basics juce::juce_events juce::juce_core juce::juce_cryptography
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
    "-framework CoreAudioKit" "-framework AudioUnit")
target_compile_definitions(synthyboy_serum_reference PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=1 JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)
target_include_directories(synthyboy_serum_reference PRIVATE "${PROJECT_SOURCE_DIR}/core/serum-import")
target_compile_features(synthyboy_serum_reference PRIVATE cxx_std_17)
set_target_properties(synthyboy_serum_reference PROPERTIES CXX_EXTENSIONS OFF)
