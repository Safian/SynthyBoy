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

# Output level (docs/MPC.md "Output level"): the shell adds a fixed make-up gain to the engine's output (shell-mpc/output_stage.h).
# The factory presets are levelled to a chord of -20 dBFS RMS with peaks near full scale; on the MPC that reads as quiet next to the stock instruments.
# 0 = no gain (comparisons, shell-mpc/tests/output_level_host.cpp). Range -24 .. 24. Default 18 dB.
set(SYNTHYBOY_MPC_OUTPUT_GAIN_DB 18 CACHE STRING "MPC shell: make-up gain of the output stage in dB")
if(NOT SYNTHYBOY_MPC_OUTPUT_GAIN_DB MATCHES "^[+-]?[0-9]+(\\.[0-9]+)?$" OR SYNTHYBOY_MPC_OUTPUT_GAIN_DB LESS -24 OR SYNTHYBOY_MPC_OUTPUT_GAIN_DB GREATER 24)
    message(FATAL_ERROR "SYNTHYBOY_MPC_OUTPUT_GAIN_DB must be a number from -24 to 24, not '${SYNTHYBOY_MPC_OUTPUT_GAIN_DB}'")
endif()
target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_OUTPUT_GAIN_DB=${SYNTHYBOY_MPC_OUTPUT_GAIN_DB})

# Peak limiter after the make-up gain: by default OFF (the signal may exceed 0 dBFS, the player can place a limiter insert on the track in the MPC).
# SYNTHYBOY_MPC_OUTPUT_LIMITER=ON restores the stereo-linked peak limiter at -1 dBFS.
option(SYNTHYBOY_MPC_OUTPUT_LIMITER "MPC shell: peak limiter at -1 dBFS after the make-up gain" OFF)
if(SYNTHYBOY_MPC_OUTPUT_LIMITER)
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_OUTPUT_LIMITER=1)
else()
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_OUTPUT_LIMITER=0)
endif()

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

# Live filter curve on a free display plane (docs/MPC.md "Overlay (experimental)"): the module shell-mpc/overlay/ draws the response of Filter 1 on an overlay plane above the MPC's
# own UI, from a thread of its own, and shows it while the "Filter 1" tab of the overlay skin (scripts/overlay-skin/mpc-ui-pages-overlay.json) is on screen. EXPERIMENTAL and OFF by default: it uses
# a display plane the MPC firmware does not offer to plugins (Key 61 only so far). OFF builds the plugin byte for byte as it was without this code.
# No display constant of any model is compiled in: the CRTC, the planes, the panel, the refresh and the rotation are found at run time (shell-mpc/overlay/discovery.h).
# The overlay build has ONE more display-only parameter after the spectrum columns, "Overlay Instance" (which plugin instance's page is on screen: several instances); the display thread
# reports it to the host, so the overlay needs SYNTHYBOY_MPC_LFO_LIVE.
option(SYNTHYBOY_MPC_OVERLAY "MPC shell: live pictures (filter, LFO, oscillator, envelope, spectrum) on a free display plane (experimental, Key 61 and MPC XL)" OFF)
if(SYNTHYBOY_MPC_OVERLAY)
    if(NOT SYNTHYBOY_MPC_LFO_LIVE)
        message(FATAL_ERROR "SYNTHYBOY_MPC_OVERLAY needs SYNTHYBOY_MPC_LFO_LIVE (the display thread reports the parameter 'Overlay Instance' to the host)")
    endif()
    find_package(Threads REQUIRED)
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_OVERLAY=1)
    target_link_libraries(synthyboy_mpc PRIVATE Threads::Threads)
endif()

# GPU drawing of the overlay in a helper process (docs/MPC.md "GPU drawing in a helper process"): the plugin starts the executable synthyboy-overlay-gpu (shell-mpc/overlay/gpu-helper/)
# when a view first needs to be shown, hands it the display descriptor and the scenes, and falls back to the CPU drawing whenever the helper is missing, fails or is switched off
# (/tmp/synthyboy-overlay-nogpu). AUTO (default) = ON for a cross build for ARM with SYNTHYBOY_MPC_OVERLAY ON, else OFF; ON / OFF force it (ON without the overlay is an error).
# The helper is linked against libc and libdl only (it opens the firmware's EGL / GBM / GLES libraries at run time), so the build needs nothing but the cross compiler.
# With SYNTHYBOY_MPC_OVERLAY OFF nothing of this exists: the plugin is byte for byte the one without the module.
set(SYNTHYBOY_MPC_OVERLAY_GPU "AUTO" CACHE STRING "MPC shell overlay: GPU drawing in a helper process (AUTO, ON, OFF)")
set_property(CACHE SYNTHYBOY_MPC_OVERLAY_GPU PROPERTY STRINGS AUTO ON OFF)
set(SYNTHYBOY_MPC_OVERLAY_GPU_ACTIVE OFF)
if(SYNTHYBOY_MPC_OVERLAY)
    if(SYNTHYBOY_MPC_OVERLAY_GPU STREQUAL "AUTO")
        if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
            set(SYNTHYBOY_MPC_OVERLAY_GPU_ACTIVE ON)
        endif()
    elseif(SYNTHYBOY_MPC_OVERLAY_GPU)
        set(SYNTHYBOY_MPC_OVERLAY_GPU_ACTIVE ON)
    endif()
elseif(NOT SYNTHYBOY_MPC_OVERLAY_GPU STREQUAL "AUTO" AND SYNTHYBOY_MPC_OVERLAY_GPU)
    message(FATAL_ERROR "SYNTHYBOY_MPC_OVERLAY_GPU=ON needs SYNTHYBOY_MPC_OVERLAY=ON")
endif()
if(SYNTHYBOY_MPC_OVERLAY_GPU_ACTIVE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "SYNTHYBOY_MPC_OVERLAY_GPU needs a Linux target (DRM, dlopen)")
    endif()
    set(SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS 33.3 CACHE STRING "MPC shell overlay GPU path: milliseconds between two scenes handed to the helper (33.3 = 30 a second, 16.7 = 60)")
    if(NOT SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS MATCHES "^[0-9]+(\\.[0-9]+)?$" OR SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS LESS 8 OR SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS GREATER 200)
        message(FATAL_ERROR "SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS must be a number from 8 to 200, not '${SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS}'")
    endif()
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_OVERLAY_GPU=1 SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS=${SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS})
    target_link_libraries(synthyboy_mpc PRIVATE ${CMAKE_DL_LIBS})
    add_subdirectory(overlay/gpu-helper)
endif()

# Pad pressure to Filter 1 cutoff (docs/MPC.md "Pad pressure"): two automatable parameters "Pad Pressure" and "Pad Pressure Amount" after the spectrum columns (before the overlay build's "Overlay Instance", which stays last), saved in
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

# Host helper tool for dumping factory preset chunks (native build only; built on request: docs/RELEASE.md "Generating .xpl files", shell-mpc/accept.sh)
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(dump_mpc_chunks EXCLUDE_FROM_ALL tests/dump_chunks_host.cpp)
    target_include_directories(dump_mpc_chunks PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${MPC_SDK_DIR}/framework"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(dump_mpc_chunks PRIVATE ${CMAKE_DL_LIBS})
    target_compile_features(dump_mpc_chunks PRIVATE cxx_std_17)
    target_compile_options(dump_mpc_chunks PRIVATE -Wall -Wextra)

    add_executable(mixfx_ui_host EXCLUDE_FROM_ALL overlay/tests/mixfx_ui_host.cpp)
    target_include_directories(mixfx_ui_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(mixfx_ui_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(mixfx_ui_host PRIVATE cxx_std_17)
    target_compile_options(mixfx_ui_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(mod_ui_host EXCLUDE_FROM_ALL overlay/tests/mod_ui_host.cpp)
    target_include_directories(mod_ui_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(mod_ui_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(mod_ui_host PRIVATE cxx_std_17)
    target_compile_options(mod_ui_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(filter_ui_host EXCLUDE_FROM_ALL overlay/tests/filter_ui_host.cpp)
    add_executable(filter_ui_demo EXCLUDE_FROM_ALL overlay/tests/filter_ui_demo.cpp)
    foreach(t filter_ui_host filter_ui_demo)
        target_include_directories(${t} PRIVATE
            "${CMAKE_SOURCE_DIR}/core/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
            "${CMAKE_CURRENT_SOURCE_DIR}/ui")
        target_link_libraries(${t} PRIVATE synthyboy_core Threads::Threads)
        target_compile_features(${t} PRIVATE cxx_std_17)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
    add_test(NAME filter_ui_host COMMAND filter_ui_host)

    add_executable(env_ui_host EXCLUDE_FROM_ALL overlay/tests/env_ui_host.cpp)
    target_include_directories(env_ui_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(env_ui_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(env_ui_host PRIVATE cxx_std_17)
    target_compile_options(env_ui_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(lfo_ui_host EXCLUDE_FROM_ALL overlay/tests/lfo_ui_host.cpp)
    target_include_directories(lfo_ui_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(lfo_ui_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(lfo_ui_host PRIVATE cxx_std_17)
    target_compile_options(lfo_ui_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(ui_env_host EXCLUDE_FROM_ALL ui/tests/ui_env_host.cpp)
    target_include_directories(ui_env_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_compile_features(ui_env_host PRIVATE cxx_std_17)
    target_compile_options(ui_env_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(q_router_review_host EXCLUDE_FROM_ALL tests/q_router_review_host.cpp)
    target_include_directories(q_router_review_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(q_router_review_host PRIVATE Threads::Threads)
    target_compile_features(q_router_review_host PRIVATE cxx_std_17)
    target_compile_options(q_router_review_host PRIVATE -Wall -Wextra)
    add_test(NAME q_router_review_host COMMAND q_router_review_host)

    add_executable(q_step_rule_host EXCLUDE_FROM_ALL tests/q_step_rule_host.cpp)
    target_include_directories(q_step_rule_host PRIVATE "${CMAKE_SOURCE_DIR}/core/include" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(q_step_rule_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(q_step_rule_host PRIVATE cxx_std_17)
    add_test(NAME q_step_rule_host COMMAND q_step_rule_host)

    add_executable(ui_time_ruler_host EXCLUDE_FROM_ALL ui/tests/ui_time_ruler_host.cpp)
    target_include_directories(ui_time_ruler_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_compile_features(ui_time_ruler_host PRIVATE cxx_std_17)
    target_compile_options(ui_time_ruler_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(ui_lfo_host EXCLUDE_FROM_ALL ui/tests/ui_lfo_host.cpp)
    target_include_directories(ui_lfo_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_compile_features(ui_lfo_host PRIVATE cxx_std_17)
    target_compile_options(ui_lfo_host PRIVATE -Wall -Wextra -Wpedantic)

    add_test(NAME env_ui_host COMMAND env_ui_host)
    add_test(NAME lfo_ui_host COMMAND lfo_ui_host)
    add_test(NAME ui_env_host COMMAND ui_env_host)
    add_test(NAME ui_lfo_host COMMAND ui_lfo_host)
    add_test(NAME ui_time_ruler_host COMMAND ui_time_ruler_host)

    add_test(NAME mod_ui_host COMMAND mod_ui_host)
    add_test(NAME mixfx_ui_host COMMAND mixfx_ui_host)

    add_executable(mastervoice_ui_host EXCLUDE_FROM_ALL overlay/tests/mastervoice_ui_host.cpp)
    target_include_directories(mastervoice_ui_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(mastervoice_ui_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(mastervoice_ui_host PRIVATE cxx_std_17)
    target_compile_options(mastervoice_ui_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(page_view_host EXCLUDE_FROM_ALL overlay/tests/page_view_host.cpp)
    target_include_directories(page_view_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(page_view_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(page_view_host PRIVATE cxx_std_17)
    target_compile_options(page_view_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(ui_gallery_host EXCLUDE_FROM_ALL overlay/tests/ui_gallery_host.cpp)
    target_include_directories(ui_gallery_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(ui_gallery_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(ui_gallery_host PRIVATE cxx_std_17)
    target_compile_options(ui_gallery_host PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ui_gallery_host COMMAND ui_gallery_host --test)

    add_executable(osc_ui_host EXCLUDE_FROM_ALL overlay/tests/osc_ui_host.cpp)
    target_include_directories(osc_ui_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(osc_ui_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(osc_ui_host PRIVATE cxx_std_17)
    target_compile_options(osc_ui_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(osc_page_host EXCLUDE_FROM_ALL overlay/tests/osc_page_host.cpp)
    target_include_directories(osc_page_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(osc_page_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(osc_page_host PRIVATE cxx_std_17)
    target_compile_options(osc_page_host PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(osc_page_ui_demo EXCLUDE_FROM_ALL overlay/tests/osc_page_ui_demo.cpp)
    target_include_directories(osc_page_ui_demo PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(osc_page_ui_demo PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(osc_page_ui_demo PRIVATE cxx_std_17)
    target_compile_options(osc_page_ui_demo PRIVATE -Wall -Wextra -Wpedantic)

    add_test(NAME osc_page_host COMMAND osc_page_host)
    add_test(NAME osc_ui_host COMMAND osc_ui_host)
endif()

