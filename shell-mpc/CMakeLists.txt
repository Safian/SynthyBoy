# SynthyBoy - MPC VST2 shell (headless, no JUCE, no CLAP).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(synthyboy_mpc MODULE
    synthyboy_mpc.cpp)

target_include_directories(synthyboy_mpc
    PRIVATE "${CMAKE_SOURCE_DIR}/core/include"
            "${MPC_SDK_DIR}/framework")

target_link_libraries(synthyboy_mpc PUBLIC synthyboy_core)
target_compile_features(synthyboy_mpc PUBLIC cxx_std_17)
set_target_properties(synthyboy_mpc PROPERTIES
    PREFIX ""
    OUTPUT_NAME "synthyboy_mpc"
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# Engine selection follows SYNTHYBOY_SHELL_ENGINE (same as the root CMakeLists.txt for the JUCE shell).
# When SYNTHYBOY_SHELL_ENGINE is "vital" and Vital is in the build, SYNTHYBOY_MPC_ENGINE_VITAL=1;
# otherwise SYNTHYBOY_MPC_ENGINE_VITAL=0. The source uses #if SYNTHYBOY_MPC_ENGINE_VITAL && SYNTHYBOY_HAS_VITAL.
if(SYNTHYBOY_SHELL_ENGINE STREQUAL "vital")
    set(SYNTHYBOY_MPC_ENGINE_VITAL 1)
else()
    set(SYNTHYBOY_MPC_ENGINE_VITAL 0)
endif()
target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_ENGINE_VITAL=${SYNTHYBOY_MPC_ENGINE_VITAL})

target_compile_options(synthyboy_mpc PRIVATE -Wall -Wextra -Wpedantic)

# Output level (docs/MPC.md "Output level"): the shell adds a fixed make-up gain to the engine's output and limits the peaks at -1 dBFS (shell-mpc/output_stage.h).
# The factory presets are levelled to a chord of -20 dBFS RMS with peaks near full scale; on the MPC that reads as quiet next to the stock instruments, and a preset
# cannot be raised further without clipping. 0 = no gain, only the limiter (comparisons, shell-mpc/tests/output_level_host.cpp). Range -24 .. 24.
set(SYNTHYBOY_MPC_OUTPUT_GAIN_DB 6 CACHE STRING "MPC shell: make-up gain of the output stage in dB (the peak limiter at -1 dBFS is always on)")
if(NOT SYNTHYBOY_MPC_OUTPUT_GAIN_DB MATCHES "^-?[0-9]+(\\.[0-9]+)?$" OR SYNTHYBOY_MPC_OUTPUT_GAIN_DB LESS -24 OR SYNTHYBOY_MPC_OUTPUT_GAIN_DB GREATER 24)
    message(FATAL_ERROR "SYNTHYBOY_MPC_OUTPUT_GAIN_DB must be a number from -24 to 24, not '${SYNTHYBOY_MPC_OUTPUT_GAIN_DB}'")
endif()
target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_OUTPUT_GAIN_DB=${SYNTHYBOY_MPC_OUTPUT_GAIN_DB})

# Live LFO display (docs/MPC.md "LFO pictures"): four display-only cursor parameters after the engine's list, driven by a low priority thread.
# The skin of scripts/mpc-skin-extras/30_lfo.py refers to them, so the MPC package needs the plugin built with this ON (the default of this shell).
# SYNTHYBOY_MPC_LFO_LIVE=OFF builds the plugin as it was before the display existed (no extra parameter); it is for comparisons only.
option(SYNTHYBOY_MPC_LFO_LIVE "MPC shell: the live LFO display parameters" ON)
set(SYNTHYBOY_MPC_LFO_LIVE_HZ 10 CACHE STRING "MPC shell: refresh rate of the LFO display thread (ticks a second; 10 = every 100 ms)")
option(SYNTHYBOY_MPC_LFO_LIVE_NOTIFY "MPC shell: tell the host (audioMasterAutomate) when a cursor moves; OFF = CPU baseline, the host never redraws" ON)
option(SYNTHYBOY_MPC_LFO_LIVE_SELFTEST "MPC shell: sweep the four cursors without a voice (host-side measurements only)" OFF)
if(SYNTHYBOY_MPC_LFO_LIVE)
    find_package(Threads REQUIRED)
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_LFO_LIVE=1 SYNTHYBOY_MPC_LFO_LIVE_HZ=${SYNTHYBOY_MPC_LFO_LIVE_HZ})
    if(SYNTHYBOY_MPC_LFO_LIVE_NOTIFY)
        target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_LFO_LIVE_NOTIFY=1)
    else()
        target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_LFO_LIVE_NOTIFY=0)
    endif()
    if(SYNTHYBOY_MPC_LFO_LIVE_SELFTEST)
        target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_LFO_LIVE_SELFTEST=1)
    endif()
    target_link_libraries(synthyboy_mpc PRIVATE Threads::Threads)
endif()

# Diagnostics: statistics of the audio callback (gap between two calls, time inside the call) written to a text file by a low priority thread
# (docs/MPC.md "LFO pictures"). OFF = the plugin is byte for byte the one without this code. Not for the package.
option(SYNTHYBOY_MPC_CALLBACK_STATS "MPC shell: measure the audio callback and write /tmp/synthyboy-callback-stats.txt (diagnostics only)" OFF)
if(SYNTHYBOY_MPC_CALLBACK_STATS)
    find_package(Threads REQUIRED)
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_CALLBACK_STATS=1)
    target_link_libraries(synthyboy_mpc PRIVATE Threads::Threads)
endif()

# Live spectrum (docs/MPC.md "Spectrum"): 32 more display-only parameters "Spectrum 1" .. "Spectrum 32" after the four LFO cursors, written by the same display thread from an FFT of
# the plugin's own output (the audio thread only copies samples into a lock-free ring). The skin of scripts/mpc-skin-extras/40_spectrum.py refers to them, so the MPC package needs
# the plugin built with this ON (the default of this shell). The source default is 0; OFF builds the plugin byte for byte as it was without this code. Needs SYNTHYBOY_MPC_LFO_LIVE.
option(SYNTHYBOY_MPC_SPECTRUM "MPC shell: the live spectrum display parameters (needs SYNTHYBOY_MPC_LFO_LIVE)" ON)
option(SYNTHYBOY_MPC_SPECTRUM_SELFTEST "MPC shell: a travelling wave over the 32 columns without a sound (host-side measurements only)" OFF)
if(SYNTHYBOY_MPC_SPECTRUM)
    if(NOT SYNTHYBOY_MPC_LFO_LIVE)
        message(FATAL_ERROR "SYNTHYBOY_MPC_SPECTRUM needs SYNTHYBOY_MPC_LFO_LIVE (the spectrum runs on the display thread)")
    endif()
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_SPECTRUM=1)
    if(SYNTHYBOY_MPC_SPECTRUM_SELFTEST)
        target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_SPECTRUM_SELFTEST=1)
    endif()
endif()

# Pad pressure to Filter 1 cutoff (docs/MPC.md "Pad pressure"): two automatable parameters "Pad Pressure" and "Pad Pressure Amount" at the very end of the parameter list, saved in
# the state chunk. The skin of scripts/mpc-skin-extras/45_pad_pressure.py refers to them, so the MPC package needs the plugin built with this ON (the default of this shell). The
# source default is 0: OFF builds the plugin byte for byte as it was without this code. The layer itself is in the own engine (the engine of the MPC package).
option(SYNTHYBOY_MPC_PAD_PRESSURE "MPC shell: pad pressure to Filter 1 cutoff (two parameters, state)" ON)
if(SYNTHYBOY_MPC_PAD_PRESSURE)
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_PAD_PRESSURE=1)
endif()

# macOS: use export list + dead_strip to remove unreferenced symbols
# Mach-O symbols have a leading underscore
if(APPLE)
    set(EXPORT_LIST "${CMAKE_CURRENT_BINARY_DIR}/mpc_exports.txt")
    file(WRITE ${EXPORT_LIST} "_VSTPluginMain\n")
    target_link_options(synthyboy_mpc PRIVATE
        "-Wl,-exported_symbols_list,${EXPORT_LIST}"
        "-Wl,-dead_strip")
endif()

# Linux: use version script + exclude-libs (no leading underscore).
# The ARM build for the MPC is made with GCC (scripts/build-mpc.sh, both architectures);
# --exclude-libs,ALL is not understood by zig's linker, and a zig-built module crashed at host exit on the device.
if(UNIX AND NOT APPLE)
    set(VERSION_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/mpc_exports.map")
    file(WRITE ${VERSION_SCRIPT} "{
        global: VSTPluginMain;
        local: *;
    };")
    target_link_options(synthyboy_mpc PRIVATE
        "-Wl,--version-script,${VERSION_SCRIPT}"
        "-Wl,--exclude-libs,ALL"
        "-Wl,--gc-sections"
        "-Wl,--as-needed"
        "-static-libstdc++"
        "-static-libgcc")
endif()
