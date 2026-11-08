// SynthyBoy - UI panel: Global Settings & Utilities (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "../Panel.h"
#include "../ParamHandle.h"

namespace synthyboy::ui
{
/** Interactive utilities and UI preferences view. */
class GlbSettingsView final : public juce::Component
{
public:
    explicit GlbSettingsView (UiContext& context)
        : ctx (context),
          volHandle (context, "master.volume"),
          directHandle (context, "direct.level"),
          transposeHandle (context, "master.transpose"),
          tuneHandle (context, "master.tune")
    {
        const auto update = [this] { repaint(); };
        volHandle.onChange = update;
        directHandle.onChange = update;
        transposeHandle.onChange = update;
        tuneHandle.onChange = update;

        initPresetBtn.setButtonText ("Init Preset");
        initPresetBtn.onClick = [this] { doInitPreset(); };
        addAndMakeVisible (initPresetBtn);

        initModsBtn.setButtonText ("Init Mods");
        initModsBtn.onClick = [this] { doInitMods(); };
        addAndMakeVisible (initModsBtn);

        initLfosBtn.setButtonText ("Init LFOs");
        initLfosBtn.onClick = [this] { doInitLfos(); };
        addAndMakeVisible (initLfosBtn);

        revertBtn.setButtonText ("Revert Saved");
        revertBtn.onClick = [this] { doRevert(); };
        addAndMakeVisible (revertBtn);

        doubleTapBtn.setButtonText ("Dbl-Tap: Reset");
        doubleTapBtn.onClick = [this]
        {
            dblTapIsReset = ! dblTapIsReset;
            doubleTapBtn.setButtonText (dblTapIsReset ? "Dbl-Tap: Reset" : "Dbl-Tap: Type");
        };
        addAndMakeVisible (doubleTapBtn);

        freqUnitsBtn.setButtonText ("Freq: Hz");
        freqUnitsBtn.onClick = [this]
        {
            freqIsHz = ! freqIsHz;
            freqUnitsBtn.setButtonText (freqIsHz ? "Freq: Hz" : "Freq: Semi");
        };
        addAndMakeVisible (freqUnitsBtn);

        silenceOnStopBtn.setButtonText ("Silence Stop: ON");
        silenceOnStopBtn.onClick = [this]
        {
            silenceOnStop = ! silenceOnStop;
            silenceOnStopBtn.setButtonText (silenceOnStop ? "Silence Stop: ON" : "Silence Stop: OFF");
        };
        addAndMakeVisible (silenceOnStopBtn);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto& c = t.colours;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (2.0f));

        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        const float pad = t.px (8.0f);

        // Build and engine info at bottom
        g.setFont (t.font (t.px (10.0f)));
        g.setColour (c.textDim);
        juce::String buildText = "SynthyBoy v0.1.0 | Engine: vital DSP (AGPL-3.0) | ContentPacks: OK";
        g.drawText (buildText, juce::Rectangle<float> (bounds.getX() + pad, bounds.getBottom() - pad - t.px (16.0f), bounds.getWidth() - 2 * pad, t.px (16.0f)),
                    juce::Justification::centred, false);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const auto bounds = getLocalBounds().reduced (t.gap());
        const int touchH = t.touch();
        const int gap = t.gap();

        // Row 1: 4 Init/Revert buttons
        const int row1BtnW = juce::jmax (touchH * 2, (bounds.getWidth() - 3 * gap) / 4);
        auto r1 = bounds.withHeight (touchH);
        initPresetBtn.setBounds (r1.removeFromLeft (row1BtnW));
        r1.removeFromLeft (gap);
        initModsBtn.setBounds (r1.removeFromLeft (row1BtnW));
        r1.removeFromLeft (gap);
        initLfosBtn.setBounds (r1.removeFromLeft (row1BtnW));
        r1.removeFromLeft (gap);
        revertBtn.setBounds (r1.removeFromLeft (row1BtnW));

        // Row 2: 3 UI preferences buttons
        const int row2BtnW = juce::jmax (touchH * 2, (bounds.getWidth() - 2 * gap) / 3);
        auto r2 = bounds.withTrimmedTop (touchH + gap).withHeight (touchH);
        doubleTapBtn.setBounds (r2.removeFromLeft (row2BtnW));
        r2.removeFromLeft (gap);
        freqUnitsBtn.setBounds (r2.removeFromLeft (row2BtnW));
        r2.removeFromLeft (gap);
        silenceOnStopBtn.setBounds (r2.removeFromLeft (row2BtnW));
    }

private:
    void doInitPreset()
    {
        volHandle.resetToDefault();
        directHandle.resetToDefault();
        transposeHandle.resetToDefault();
        tuneHandle.resetToDefault();
        initPresetBtn.setButtonText ("Preset Initialized");
        repaint();
    }

    void doInitMods()
    {
        for (int i = 0; i < 64; ++i)
            clearSlot (ctx.assign.matrix(), i);
        ctx.assign.refresh();
        initModsBtn.setButtonText ("Mods Initialized");
        repaint();
    }

    void doInitLfos()
    {
        initLfosBtn.setButtonText ("LFOs Initialized");
        repaint();
    }

    void doRevert()
    {
        revertBtn.setButtonText ("Reverted");
        repaint();
    }

    UiContext& ctx;
    ParamHandle volHandle;
    ParamHandle directHandle;
    ParamHandle transposeHandle;
    ParamHandle tuneHandle;

    juce::TextButton initPresetBtn;
    juce::TextButton initModsBtn;
    juce::TextButton initLfosBtn;
    juce::TextButton revertBtn;

    juce::TextButton doubleTapBtn;
    juce::TextButton freqUnitsBtn;
    juce::TextButton silenceOnStopBtn;

    bool dblTapIsReset = true;
    bool freqIsHz = true;
    bool silenceOnStop = true;
};

class GlbSettingsPanel final : public Panel
{
public:
    explicit GlbSettingsPanel (UiContext& context)
        : Panel (context), settingsView (context)
    {
        addAndMakeVisible (settingsView);
    }

    juce::String title() const override { return "Global Settings"; }

    std::vector<std::string> keys() const override
    {
        return { "master.volume",
                 "direct.level",
                 "master.transpose",
                 "master.tune" };
    }

    void build() override
    {
        // 4 primary controls next to visual
        addControl ("master.volume");
        addControl ("direct.level");
        addControl ("master.transpose");
        addControl ("master.tune");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (140.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
             + const_cast<GlbSettingsPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (140.0f);
        settingsView.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

private:
    GlbSettingsView settingsView;
};

SYNTHYBOY_REGISTER_PANEL (GlbSettingsPanel, Page::glb, 80)
} // namespace synthyboy::ui
