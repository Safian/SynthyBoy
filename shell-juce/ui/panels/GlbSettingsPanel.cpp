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

        // Row group 1: Init / Revert buttons, row group 2: UI preferences. Each wraps into as many rows as the width needs,
        // every button at least two touch targets wide.
        juce::TextButton* const initRow[] = { &initPresetBtn, &initModsBtn, &initLfosBtn, &revertBtn };
        juce::TextButton* const prefRow[] = { &doubleTapBtn, &freqUnitsBtn, &silenceOnStopBtn };
        auto area = bounds;
        layoutGrid (area, initRow, 4);
        layoutGrid (area, prefRow, 3);
    }

    /** Height this view needs at `viewWidth`: the button rows plus the build info line. */
    int requiredHeight (int viewWidth) const
    {
        const auto& t = ctx.theme;
        const int inner = viewWidth - 2 * t.gap();
        const int rows = rowsFor (4, inner) + rowsFor (3, inner);
        return 2 * t.gap() + rows * t.touch() + rows * t.gap() + t.ipx (24.0f);
    }

private:
    int columnsFor (int count, int width) const
    {
        const auto& t = ctx.theme;
        return juce::jlimit (1, count, (width + t.gap()) / (t.touch() * 2 + t.gap()));
    }

    int rowsFor (int count, int width) const
    {
        const int cols = columnsFor (count, width);
        return (count + cols - 1) / cols;
    }

    void layoutGrid (juce::Rectangle<int>& area, juce::TextButton* const* buttons, int count)
    {
        const auto& t = ctx.theme;
        const int cols = columnsFor (count, area.getWidth());
        const int rows = (count + cols - 1) / cols;
        const int perRow = (count + rows - 1) / rows;
        for (int row = 0; row < rows; ++row)
        {
            auto rowArea = area.removeFromTop (t.touch());
            area.removeFromTop (t.gap());
            const int inRow = juce::jmin (perRow, count - row * perRow);
            const int w = (rowArea.getWidth() - (inRow - 1) * t.gap()) / inRow;
            for (int i = 0; i < inRow; ++i)
            {
                buttons[row * perRow + i]->setBounds (rowArea.removeFromLeft (w));
                rowArea.removeFromLeft (t.gap());
            }
        }
    }

public:
    /** Every parameter to its default and the content (custom LFO shapes ...) emptied: a new patch. */
    void doInitPreset()
    {
        applyValues (ctx, ParamValues::defaults());
        ctx.host.setContent (StateContent {});
        ctx.assign.refresh();
        Toast::show (*this, "Preset initialised");
        repaint();
    }

    void doInitMods()
    {
        for (int i = 0; i < 64; ++i)
            clearSlot (ctx.assign.matrix(), i);
        ctx.assign.refresh();
        Toast::show (*this, "All modulations cleared");
        repaint();
    }

    /** All 10 LFOs and the 4 random LFOs back to their defaults, and the custom shapes with them. */
    void doInitLfos()
    {
        auto live = readLiveValues (ctx);
        for (int n = 1; n <= 10; ++n)
            ModuleClipboard::initModule ("lfo" + std::to_string (n), live);
        for (int n = 1; n <= 4; ++n)
            ModuleClipboard::initModule ("rlfo" + std::to_string (n), live);
        applyValues (ctx, live);
        auto content = ctx.host.content();
        content.lfoShapes.clear();
        ctx.host.setContent (content);
        Toast::show (*this, "LFOs initialised");
        repaint();
    }

    void doRevert()
    {
        Toast::show (*this, ctx.host.revertToSaved() ? "Reverted to the saved patch" : "Nothing saved to revert to");
        repaint();
    }

    juce::TextButton& initPresetButton() noexcept { return initPresetBtn; }
    juce::TextButton& initModsButton() noexcept { return initModsBtn; }
    juce::TextButton& initLfosButton() noexcept { return initLfosBtn; }
    juce::TextButton& revertButton() noexcept { return revertBtn; }

private:

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
        const int visualH = settingsView.requiredHeight (width - 2 * t.gap());
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
             + const_cast<GlbSettingsPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = settingsView.requiredHeight (area.getWidth());
        settingsView.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

private:
    GlbSettingsView settingsView;
};

SYNTHYBOY_REGISTER_PANEL (GlbSettingsPanel, Page::glb, 80)
} // namespace synthyboy::ui
