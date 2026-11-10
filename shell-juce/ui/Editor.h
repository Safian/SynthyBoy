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
/** The whole SynthyBoy editor as a plain juce::Component: it knows nothing of a plug-in format or of a host window. The plug-in wraps it in a thin
    juce::AudioProcessorEditor (shell-juce/EditorAdapter.h); the Kick app puts it into its PluginPanel. Lays itself out for any size from
    kMinWidth x kMinHeight (the default is 1024 x 768, the layout follows the width). */
class SynthyBoyEditor final : public juce::Component,
                              public OverlayHost,
                              private juce::Timer,
                              private ParameterStore::Listener
{
public:
    static constexpr int kMinWidth = 360, kMinHeight = 480, kMaxWidth = 1800, kMaxHeight = 1400;

    explicit SynthyBoyEditor (UiHost& host);
    ~SynthyBoyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    // OverlayHost: loupes, bubbles, toasts, sheets, call-outs and prompts live inside the editor
    juce::Component& overlayRoot() noexcept override { return *this; }
    bool confineOverlays() const noexcept override { return host.overlaysInsideEditor(); }

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
    /** Menu "Import Serum 2 file..." (WP-21): .SerumPreset / .SerumFX / .SerumFXRack / .XferShape with the report. */
    void importSerumFromMenu();
    SerumFlowResult importSerumFile (const juce::File& file);
    // ---- GROUPD WP-15 (13-new-keys.md 4.40): the Rendering menu (Serum's main menu, manual p.324) and the last note as audio ----
    /** Render OSC Warp of oscillator `osc` (1..3; 0 = the oscillator of the open tab). The result is shown in a message sheet. */
    FlowResult renderOscWarpFromMenu (int osc = 0);
    /** Resample to oscillator 1..3, or 0 = Osc A + Osc B. */
    FlowResult resampleFromMenu (int target);
    /** Opens the "Last note as audio" sheet (render, Save as WAV, Share, drag-out). */
    LastNoteSheet* showLastNoteSheet();
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
    void parameterGestureChanged (int index, bool starting) override;
    void showMenu();
    void updateAbLabel();
    ParamValues readLiveValues() const { return readAllValues (host.parameterStore()); }
    void applyValues (const ParamValues& values) { writeAllValues (host.parameterStore(), values); }

    UiHost& host;
    Theme uiTheme;
    SynthLookAndFeel laf;
    ModAssignController assign;
    UiContext ctx;
    UndoHistory undoHistory;
    int openGestures = 0;
    bool replayingHistory = false;
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
