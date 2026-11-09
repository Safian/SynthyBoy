// SynthyBoy - UI: SynthyBoyEditor, the editor shell that replaces the generic parameter editor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>

#include "ContentSheets.h"
#include "ModSourceBar.h"
#include "PanelHost.h"
#include "Strip.h"

namespace synthyboy::ui
{
class SynthyBoyEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer,
                              private juce::AudioProcessorParameter::Listener
{
public:
    SynthyBoyEditor (juce::AudioProcessor& owner, UiHost& host);
    ~SynthyBoyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    UiContext& context() noexcept { return ctx; }
    Theme& theme() noexcept { return uiTheme; }
    PanelHost& panelHost() noexcept { return *panels; }
    TopBar& topBar() noexcept { return *top; }
    MacroRow& macroRow() noexcept { return *macros; }
    PageTabs& pageTabs() noexcept { return *tabs; }
    KeyboardStrip& keyboardStrip() noexcept { return *keyboard; }
    ModSourceBar& sourceBar() noexcept { return *sources; }
    bool isSourceBarOpen() const noexcept { return sourcesOpen; }

    void setSourceBarOpen (bool open);

    // ---- content pipeline (WP-01): Missing Files dialog on load, Vital import with its report ----
    /** Looks for references the library cannot resolve and opens the Missing Files dialog when there are some (the timer does it after a load). */
    bool checkMissingContent();
    /** Menu "Import Vital file...": asks for the file, imports it and shows the report. */
    void importVitalFromMenu();
    /** Imports a file and shows the report sheet (the menu and the tests call this). */
    VitalFlowResult importVitalFile (const juce::File& file);
    /** The open sheet (Missing Files / Vital report), null when none. */
    juce::Component* contentSheet() noexcept { return sheet.get(); }
    void closeContentSheet();
    /** A/B compare: stores the live state in the active slot and applies the other one (decision Q4). */
    void toggleAb();
    void copyActiveToOtherAb();
    void performUndo();
    void performRedo();
    void updateUndoRedoButtons();
    UndoHistory& undoStack() noexcept { return undoHistory; }
    void setUiScale (float scale);
    void setDarkMode (bool dark);
    /** Creates an editor sized for the given reference device (phone 390x844, tablet 1024x768, ...). */
    void setReferenceSize (int width, int height);

private:
    void timerCallback() override;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool starting) override;
    void showMenu();
    void updateAbLabel();
    ParamValues readLiveValues() const;
    void applyValues (const ParamValues& values);

    UiHost& host;
    Theme uiTheme;
    SynthLookAndFeel laf;
    ModAssignController assign;
    UiContext ctx;
    UndoHistory undoHistory;
    int openGestures = 0;
    ParamValues gestureStartValues;
    juce::String lastPresetName;

    std::unique_ptr<TopBar> top;
    std::unique_ptr<MacroRow> macros;
    std::unique_ptr<ModSourceBar> sources;
    std::unique_ptr<PageTabs> tabs;
    std::unique_ptr<PanelHost> panels;
    std::unique_ptr<KeyboardStrip> keyboard;
    juce::TooltipWindow tooltips { this, 600 };
    bool sourcesOpen = false;
    std::unique_ptr<juce::Component> sheet;
    std::vector<std::unique_ptr<juce::Component>> retiredSheets;
    uint32_t checkedContentRevision = 0;
    void showSheet (std::unique_ptr<juce::Component> component);
};
} // namespace synthyboy::ui
