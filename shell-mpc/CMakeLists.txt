# SynthyBoy - MPC VST2 shell (headless).
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

target_compile_options(synthyboy_mpc PRIVATE -Wall -Wextra -Wpedantic)

# Hardware generation visual policy for Q-Link framing:
#   auto: armv7/arm32 -> gen1 (frames enabled for 4-knob setups); aarch64/arm64 -> gen2 (frames compiled out); host -> gen1
#   gen1: force Gen 1 (factory-style bank frame and single unnumbered Q marker on 4-knob setups; zero numbered chips)
#   gen2: force Gen 2 (no bank frame, no Q marker, specific Q-frame drawing code compiled out)
set(SYNTHYBOY_MPC_UI_GEN "auto" CACHE STRING "MPC UI hardware generation policy for Q-Link framing (auto, gen1, gen2)")
set_property(CACHE SYNTHYBOY_MPC_UI_GEN PROPERTY STRINGS auto gen1 gen2)

if(SYNTHYBOY_MPC_UI_GEN STREQUAL "gen1")
    set(SYNTHYBOY_MPC_UI_GEN_N 1)
elseif(SYNTHYBOY_MPC_UI_GEN STREQUAL "gen2")
    set(SYNTHYBOY_MPC_UI_GEN_N 2)
elseif(SYNTHYBOY_MPC_UI_GEN STREQUAL "auto")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
        set(SYNTHYBOY_MPC_UI_GEN_N 2)
    else()
        set(SYNTHYBOY_MPC_UI_GEN_N 1)
    endif()
else()
    message(FATAL_ERROR "SYNTHYBOY_MPC_UI_GEN must be auto, gen1, or gen2, not '${SYNTHYBOY_MPC_UI_GEN}'")
endif()

if(SYNTHYBOY_MPC_UI_GEN_N EQUAL 2)
    set(SYNTHYBOY_MPC_QFRAME_ENABLED_N 0)
else()
    set(SYNTHYBOY_MPC_QFRAME_ENABLED_N 1)
endif()

target_compile_definitions(synthyboy_mpc PRIVATE
    SYNTHYBOY_MPC_UI_GEN=${SYNTHYBOY_MPC_UI_GEN_N}
    SYNTHYBOY_MPC_QFRAME_ENABLED=${SYNTHYBOY_MPC_QFRAME_ENABLED_N})

# Live LFO display (docs/MPC.md "LFO live display and the display thread"): four display-only cursor parameters after the engine's list, driven by a low priority thread.
# The overlay's LFO pages read them, so the MPC package needs the plugin built with this ON (the default of this shell).
# SYNTHYBOY_MPC_LFO_LIVE=OFF builds the plugin as it was before the display existed (no extra parameter); it is for comparisons only.
option(SYNTHYBOY_MPC_LFO_LIVE "MPC shell: the live LFO display parameters" ON)
if(SYNTHYBOY_MPC_LFO_LIVE)
    find_package(Threads REQUIRED)
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_LFO_LIVE=1)
    target_link_libraries(synthyboy_mpc PRIVATE Threads::Threads)
endif()

# Live spectrum (docs/MPC.md "Spectrum"): 32 more display-only parameters "Spectrum 1" .. "Spectrum 32" after the four LFO cursors, written by the same display thread from an FFT of
# the plugin's own output (the audio thread only copies samples into a lock-free ring). The overlay's Macros page reads them, so the MPC package needs
# the plugin built with this ON (the default of this shell). The source default is 0; OFF builds the plugin byte for byte as it was without this code. Needs SYNTHYBOY_MPC_LFO_LIVE.
option(SYNTHYBOY_MPC_SPECTRUM "MPC shell: the live spectrum display parameters (needs SYNTHYBOY_MPC_LFO_LIVE)" ON)
if(SYNTHYBOY_MPC_SPECTRUM)
    if(NOT SYNTHYBOY_MPC_LFO_LIVE)
        message(FATAL_ERROR "SYNTHYBOY_MPC_SPECTRUM needs SYNTHYBOY_MPC_LFO_LIVE (the spectrum runs on the display thread)")
    endif()
    target_compile_definitions(synthyboy_mpc PRIVATE SYNTHYBOY_MPC_SPECTRUM=1)
endif()

# Live filter curve on a free display plane (docs/MPC.md "Overlay (experimental)"): the module shell-mpc/overlay/ draws the response of Filter 1 on an overlay plane above the MPC's
# own UI, from a thread of its own, and shows it while the "Filter 1" tab of the overlay skin (skin/TUI.json) is on screen. It uses
# a display plane the MPC firmware does not offer to plugins. It is always built in (no option).
# No display constant of any model is compiled in: the CRTC, the planes, the panel, the refresh and the rotation are found at run time (shell-mpc/overlay/discovery.h).
# The plugin has ONE more display-only parameter after the spectrum columns, "Overlay Instance" (which plugin instance's page is on screen: several instances); the display thread
# reports it to the host, so the overlay needs SYNTHYBOY_MPC_LFO_LIVE.
# The developer log (shell-mpc/kit/log.h, macro MPCKIT_DEV_LOG) and the files the plugin and its GPU helper write into /tmp for the developer (shell-mpc/write_files.h, macro SYNTHYBOY_MPC_WRITE_FILES).
# ON (default, today's behaviour: the device measurements need the status file): the log exists (every category off until /tmp/synthyboy-log.conf switches it on) and the files are written as before.
# OFF (the public release: scripts/build-mpc.sh --release): no log code, no log strings, no log thread; no report / probe / error files; the helper's output goes to /dev/null; the status file only while
# /tmp/synthyboy-overlay-status-on exists. docs/MPC.md "Logging in the plugin".
option(SYNTHYBOY_MPC_DEV_LOG "MPC shell: developer log and diagnostic files in /tmp (OFF for the public release)" ON)
if(SYNTHYBOY_MPC_DEV_LOG)
    target_compile_definitions(synthyboy_mpc PRIVATE MPCKIT_DEV_LOG=1 SYNTHYBOY_MPC_WRITE_FILES=1)
else()
    target_compile_definitions(synthyboy_mpc PRIVATE MPCKIT_DEV_LOG=0 SYNTHYBOY_MPC_WRITE_FILES=0)
endif()

# The overlay is always part of the plugin; it needs the live LFO display (the display thread reports the parameter 'Overlay Instance' to the host).
if(NOT SYNTHYBOY_MPC_LFO_LIVE)
    message(FATAL_ERROR "the MPC shell needs SYNTHYBOY_MPC_LFO_LIVE=ON (the overlay display thread reports the parameter 'Overlay Instance' to the host)")
endif()
find_package(Threads REQUIRED)
target_link_libraries(synthyboy_mpc PRIVATE Threads::Threads)

# GPU drawing of the overlay in a helper process (docs/MPC.md "GPU drawing in a helper process"): the plugin starts the executable synthyboy-overlay-gpu (shell-mpc/overlay/gpu-helper/)
# when a view first needs to be shown, hands it the display descriptor and the scenes, and falls back to the CPU drawing whenever the helper is missing, fails or is switched off
# (/tmp/synthyboy-overlay-nogpu). AUTO (default) = ON for a cross build for ARM, else OFF; ON / OFF force it.
# The helper is linked against libc and libdl only (it opens the firmware's EGL / GBM / GLES libraries at run time), so the build needs nothing but the cross compiler.
set(SYNTHYBOY_MPC_OVERLAY_GPU "AUTO" CACHE STRING "MPC shell overlay: GPU drawing in a helper process (AUTO, ON, OFF)")
set_property(CACHE SYNTHYBOY_MPC_OVERLAY_GPU PROPERTY STRINGS AUTO ON OFF)
set(SYNTHYBOY_MPC_OVERLAY_GPU_ACTIVE OFF)
if(SYNTHYBOY_MPC_OVERLAY_GPU STREQUAL "AUTO")
    if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
        set(SYNTHYBOY_MPC_OVERLAY_GPU_ACTIVE ON)
    endif()
elseif(SYNTHYBOY_MPC_OVERLAY_GPU)
    set(SYNTHYBOY_MPC_OVERLAY_GPU_ACTIVE ON)
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
# the state chunk. The overlay's Master page reads them, so the MPC package needs the plugin built with this ON (the default of this shell). The
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

    # loads a program or a chunk into the built plugin and prints every parameter (the check of the Sounds-browser files); run by hand
    add_executable(chunk_load_host EXCLUDE_FROM_ALL tests/chunk_load_host.cpp)
    target_include_directories(chunk_load_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${MPC_SDK_DIR}/framework"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(chunk_load_host PRIVATE ${CMAKE_DL_LIBS})
    target_compile_features(chunk_load_host PRIVATE cxx_std_17)
    target_compile_options(chunk_load_host PRIVATE -Wall -Wextra)

    add_executable(mixer_stereo_host EXCLUDE_FROM_ALL tests/mixer_stereo_host.cpp)
    target_include_directories(mixer_stereo_host PRIVATE "${MPC_SDK_DIR}/framework")
    target_link_libraries(mixer_stereo_host PRIVATE synthyboy_core Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_features(mixer_stereo_host PRIVATE cxx_std_17)
    add_test(NAME mixer_stereo_host COMMAND mixer_stereo_host)


    add_executable(filter_style_display_host EXCLUDE_FROM_ALL tests/filter_style_display_host.cpp)
    target_include_directories(filter_style_display_host PRIVATE "${MPC_SDK_DIR}/framework")
    target_link_libraries(filter_style_display_host PRIVATE synthyboy_core Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_features(filter_style_display_host PRIVATE cxx_std_17)
    add_test(NAME filter_style_display_host COMMAND filter_style_display_host)


    add_executable(mixer_binding_host EXCLUDE_FROM_ALL overlay/tests/mixer_binding_host.cpp)
    target_include_directories(mixer_binding_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(mixer_binding_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(mixer_binding_host PRIVATE cxx_std_17)
    target_compile_options(mixer_binding_host PRIVATE -Wall -Wextra -Wpedantic)







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


    # The NOTE / VELO curve editor (ui/scalar_curve_editor.h, overlay/scalar_curve_ui.h): component, page logic, content round trip against the engine's own evaluation


    add_test(NAME ui_time_ruler_host COMMAND ui_time_ruler_host)

    add_test(NAME mixer_binding_host COMMAND mixer_binding_host)



    # the automatic layout lint of the plugin-drawn pages (every page, Init and every factory preset): a finding that is not in the baseline fails it, a baseline entry that no longer occurs too

    # the names of the macros on the overlay pages: short forms, cards, chips, the keyboard page (docs/ui-design/macro-names.md)



    # the FX slot editor driven by the tables of the effects (ui/fx_layout.h): the tables against the engine, the one rule for what does nothing, the pages, the number field kind

    # Standalone native FX aperture/ownership regression.  This deliberately
    # remains separate from the broad Mixer+FX legacy host executable so a
    # native-control failure is reported without masking unrelated fixtures.

    # the host's standby dim (overlay/standby.h) and the parameter coverage of the pages without a test of their own

    # Robustness review (docs: .hw-local/mpc-ui-plans/robustness-review.md): the host tests that reproduced the findings; they pass now and are registered.
    add_executable(safe_file_host EXCLUDE_FROM_ALL tests/safe_file_host.cpp)
    target_include_directories(safe_file_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(safe_file_host PRIVATE Threads::Threads)
    target_compile_features(safe_file_host PRIVATE cxx_std_17)
    target_compile_options(safe_file_host PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME safe_file_host COMMAND safe_file_host)

    add_executable(host_strings_host EXCLUDE_FROM_ALL tests/host_strings_host.cpp)
    target_include_directories(host_strings_host PRIVATE "${CMAKE_SOURCE_DIR}/core/include" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(host_strings_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(host_strings_host PRIVATE cxx_std_17)
    target_compile_options(host_strings_host PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME host_strings_host COMMAND host_strings_host)

    add_executable(preset_index_host EXCLUDE_FROM_ALL tests/preset_index_host.cpp)
    target_include_directories(preset_index_host PRIVATE "${CMAKE_SOURCE_DIR}/core/include" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(preset_index_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(preset_index_host PRIVATE cxx_std_17)
    target_compile_options(preset_index_host PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME preset_index_host COMMAND preset_index_host "${CMAKE_SOURCE_DIR}/content/presets")

    add_executable(poll_guard_host EXCLUDE_FROM_ALL tests/poll_guard_host.cpp)
    target_include_directories(poll_guard_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(poll_guard_host PRIVATE Threads::Threads)
    target_compile_features(poll_guard_host PRIVATE cxx_std_17)
    target_compile_options(poll_guard_host PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME poll_guard_host COMMAND poll_guard_host)

    # The kit's display thread (kit/display_thread.h): the features in order, guarded, busy / idle, stop. Logic only, no plugin needed.
    add_executable(display_thread_host EXCLUDE_FROM_ALL tests/display_thread_host.cpp)
    target_include_directories(display_thread_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(display_thread_host PRIVATE Threads::Threads)
    target_compile_features(display_thread_host PRIVATE cxx_std_17)
    target_compile_options(display_thread_host PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME display_thread_host COMMAND display_thread_host)

    # The start-up record and the developer log (kit/log.h): logic only, no plugin needed.
    foreach(t startup_timing_host log_host log_off_host log_rt_host)
        add_executable(${t} EXCLUDE_FROM_ALL tests/${t}.cpp)
        target_include_directories(${t} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${t} PRIVATE Threads::Threads)
        target_compile_features(${t} PRIVATE cxx_std_17)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()

    add_executable(stroke_strip_host EXCLUDE_FROM_ALL tests/stroke_strip_host.cpp)
    target_include_directories(stroke_strip_host PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay")
    target_link_libraries(stroke_strip_host PRIVATE Threads::Threads)
    target_compile_features(stroke_strip_host PRIVATE cxx_std_17)
    target_compile_options(stroke_strip_host PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME stroke_strip_host COMMAND stroke_strip_host)

    add_executable(close_hang_host EXCLUDE_FROM_ALL tests/close_hang_host.cpp)
    target_include_directories(close_hang_host PRIVATE "${MPC_SDK_DIR}/framework" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(close_hang_host PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_features(close_hang_host PRIVATE cxx_std_17)
    target_compile_options(close_hang_host PRIVATE -Wall -Wextra)
    add_test(NAME close_hang_host COMMAND close_hang_host $<TARGET_FILE:synthyboy_mpc>)

    add_executable(controller_review_host EXCLUDE_FROM_ALL overlay/tests/controller_review_host.cpp)
    target_include_directories(controller_review_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/overlay"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(controller_review_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(controller_review_host PRIVATE cxx_std_17)
    target_compile_options(controller_review_host PRIVATE -Wall -Wextra)

    add_executable(chunk_review_host EXCLUDE_FROM_ALL tests/chunk_review_host.cpp)
    target_include_directories(chunk_review_host PRIVATE
        "${CMAKE_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(chunk_review_host PRIVATE synthyboy_core Threads::Threads)
    target_compile_features(chunk_review_host PRIVATE cxx_std_17)
    target_compile_options(chunk_review_host PRIVATE -Wall -Wextra)

    add_test(NAME chunk_review_host COMMAND chunk_review_host)
    add_test(NAME controller_review_host COMMAND controller_review_host)

    # --- Q-Link Visual Build Policy Tests and Fixtures ---




    # The quality profile per device (device_profile.h): the profile texts, the device table, the overrides
    add_executable(device_profile_host tests/device_profile_host.cpp)
    target_include_directories(device_profile_host PRIVATE "${CMAKE_SOURCE_DIR}/core/include")
    target_compile_features(device_profile_host PRIVATE cxx_std_17)
    target_compile_options(device_profile_host PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME device_profile_host COMMAND device_profile_host)

endif()

