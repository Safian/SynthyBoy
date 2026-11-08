# SynthyBoy - custom GUI kit and editor shell (namespace synthyboy::ui), docs/ui-design, shell-juce/ui/README.md.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# synthyboy_ui_model   pure C++17 view-model (no JUCE): key resolution, cap dimming, gestures, assign flow, A/B
# SYNTHYBOY_UI_SOURCES the JUCE Components (kit + editor + every panels/*.cpp), compiled INTO the plugin target and the render test.
#                      Panels register themselves from static initialisers, so their object files must be linked directly
#                      (not through a static library): a new panel is one new file in panels/, picked up by the glob below.

set(UI_DIR "${CMAKE_CURRENT_LIST_DIR}")

add_library(synthyboy_ui_model STATIC
    ${UI_DIR}/model/ControlModel.cpp
    ${UI_DIR}/model/GestureModel.cpp
    ${UI_DIR}/model/ModModel.cpp)
target_include_directories(synthyboy_ui_model PUBLIC ${UI_DIR}/model)
# the consumers (plugin, tests) link synthyboy_core themselves; only its headers are needed here
target_include_directories(synthyboy_ui_model PUBLIC $<TARGET_PROPERTY:synthyboy_core,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(synthyboy_ui_model PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(synthyboy_ui_model PRIVATE /W4)
else()
    target_compile_options(synthyboy_ui_model PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

file(GLOB SYNTHYBOY_UI_PANEL_SOURCES CONFIGURE_DEPENDS ${UI_DIR}/panels/*.cpp)
set(SYNTHYBOY_UI_SOURCES
    ${UI_DIR}/Theme.cpp
    ${UI_DIR}/UiContext.cpp
    ${UI_DIR}/ParamHandle.cpp
    ${UI_DIR}/Overlays.cpp
    ${UI_DIR}/ParamControls.cpp
    ${UI_DIR}/Sheet.cpp
    ${UI_DIR}/ModSourceBar.cpp
    ${UI_DIR}/Panel.cpp
    ${UI_DIR}/PanelHost.cpp
    ${UI_DIR}/Strip.cpp
    ${UI_DIR}/Editor.cpp
    ${SYNTHYBOY_UI_PANEL_SOURCES}
    PARENT_SCOPE)

if(SYNTHYBOY_BUILD_TESTS)
    synthyboy_add_test(synthyboy_test_ui_model tests/test_ui_model.cpp LINK synthyboy_ui_model)
endif()
