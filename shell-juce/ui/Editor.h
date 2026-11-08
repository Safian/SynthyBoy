// SynthyBoy - UI: SynthyBoyEditor, the editor shell that replaces the generic parameter editor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>

#include "ModSourceBar.h"
#include "PanelHost.h"
#include "Strip.h"

namespace synthyboy::ui
{
class SynthyBoyEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    SynthyBoyEditor (juce::AudioProcessor& owner, UiHost& host);
    ~SynthyBoyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

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
    /** A/B compare: stores the live state in the active slot and applies the other one (decision Q4). */
    void toggleAb();
    void copyActiveToOtherAb();
    void setUiScale (float scale);
    void setDarkMode (bool dark);
    /** Creates an editor sized for the given reference device (phone 390x844, tablet 1024x768, ...). */
    void setReferenceSize (int width, int height);

private:
    void timerCallback() override;
    void showMenu();
    void updateAbLabel();
    ParamValues readLiveValues() const;
    void applyValues (const ParamValues& values);

    UiHost& host;
    Theme uiTheme;
    SynthLookAndFeel laf;
    ModAssignController assign;
    UiContext ctx;

    std::unique_ptr<TopBar> top;
    std::unique_ptr<MacroRow> macros;
    std::unique_ptr<ModSourceBar> sources;
    std::unique_ptr<PageTabs> tabs;
    std::unique_ptr<PanelHost> panels;
    std::unique_ptr<KeyboardStrip> keyboard;
    juce::TooltipWindow tooltips { this, 600 };
    bool sourcesOpen = false;
};
} // namespace synthyboy::ui
