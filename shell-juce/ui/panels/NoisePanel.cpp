// SynthyBoy - UI panel: Noise generator (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NoisePanel.h"

namespace synthyboy::ui
{
NoisePanel::NoisePanel (UiContext& context)
    : Panel (context),
      noiseView (context),
      onHandle (context, "noise.on"),
      sampleHandle (context, "noise.sample")
{
    addAndMakeVisible (noiseView);
    addAndMakeVisible (prevSampleBtn);
    addAndMakeVisible (sampleNameBtn);
    addAndMakeVisible (nextSampleBtn);

    prevSampleBtn.onClick = [this] { stepSample (-1); };
    nextSampleBtn.onClick = [this] { stepSample (1); };
    sampleNameBtn.onClick = [this] { showSampleMenu(); };

    onHandle.onChange = [this]
    {
        repaint();
        noiseView.repaint();
    };

    sampleHandle.onChange = [this]
    {
        updateSampleButtonText();
        noiseView.repaint();
    };

    updateSampleButtonText();
}

NoisePanel::~NoisePanel() = default;

void NoisePanel::stepSample (int direction)
{
    if (! sampleHandle.valid())
        return;
    constexpr int count = 9;
    const int now = juce::jlimit (0, count - 1, static_cast<int> (sampleHandle.plain() + 0.5f));
    const int next = (now + direction + count) % count;
    sampleHandle.setPlainComplete (static_cast<float> (next));
    updateSampleButtonText();
    noiseView.repaint();
}

void NoisePanel::showSampleMenu()
{
    if (! sampleHandle.valid())
        return;
    juce::PopupMenu menu;
    static const char* const kSampleNames[] = {
        "White Noise", "Pink Noise", "Brown Noise", "Geiger",
        "Tape Hiss", "Vinyl Clicks", "Digital Hash", "Velvet Noise", "Atmosphere"
    };
    const int now = static_cast<int> (sampleHandle.plain() + 0.5f);
    for (int i = 0; i < 9; ++i)
    {
        menu.addItem (i + 1, kSampleNames[i], true, now == i);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sampleNameBtn),
                        [this] (int result) {
                            if (result > 0 && sampleHandle.valid())
                            {
                                sampleHandle.setPlainComplete (static_cast<float> (result - 1));
                                updateSampleButtonText();
                                noiseView.repaint();
                            }
                        });
}

void NoisePanel::updateSampleButtonText()
{
    static const char* const kSampleNames[] = {
        "White Noise", "Pink Noise", "Brown Noise", "Geiger",
        "Tape Hiss", "Vinyl Clicks", "Digital Hash", "Velvet Noise", "Atmosphere"
    };
    const int now = sampleHandle.valid() ? juce::jlimit (0, 8, static_cast<int> (sampleHandle.plain() + 0.5f)) : 0;
    sampleNameBtn.setButtonText (kSampleNames[now]);
}

std::vector<std::string> NoisePanel::keys() const
{
    return {
        // Primary (4 controls next to the visual, 02-oscillators.md §2.3)
        "noise.level", "noise.pan", "noise.pitch", "noise.fine",

        // Behind More (10 controls)
        "noise.on", "noise.sample", "noise.destination", "noise.phase",
        "noise.random_phase", "noise.keytrack", "noise.loop",
        "noise.filter_blend", "noise.bus1_send", "noise.bus2_send"
    };
}

void NoisePanel::build()
{
    const auto all = keys();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (i < 4)
            addControl (all[i]);
        else
            addMoreControl (all[i]);
    }
}

int NoisePanel::preferredHeight (int width) const
{
    const auto& t = ctx.theme;
    const int touchH = t.touch();
    const int visualH = t.ipx (120.0f);
    const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
    return headerHeight() + touchH + visualH + 4 * t.gap() + const_cast<NoisePanel*> (this)->layoutControls (area, true);
}

void NoisePanel::resized()
{
    const auto& t = ctx.theme;
    const int touchH = t.touch();
    const int visualH = t.ipx (120.0f);
    auto area = contentArea();

    // Sample picker row (02-28, >= 44pt touch targets)
    auto sampleRow = area.removeFromTop (touchH);
    prevSampleBtn.setBounds (sampleRow.removeFromLeft (touchH));
    sampleRow.removeFromLeft (t.gap());
    nextSampleBtn.setBounds (sampleRow.removeFromRight (touchH));
    sampleRow.removeFromRight (t.gap());
    sampleNameBtn.setBounds (sampleRow);

    area.removeFromTop (t.gap());
    auto topArea = area.removeFromTop (visualH);
    noiseView.setBounds (topArea);
    area.removeFromTop (t.gap());
    layoutControls (area, false);
}

void NoisePanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    if (onHandle.valid() && onHandle.plain() < 0.5f)
    {
        const auto& t = ctx.theme;
        g.setFont (t.labelFont());
        g.setColour (t.colours.warn);
        g.drawText ("[MUTED / OFF]", getLocalBounds().reduced (t.gap(), 0).withHeight (headerHeight() + t.gap() / 2),
                    juce::Justification::centredRight, false);
    }
}

SYNTHYBOY_REGISTER_PANEL (NoisePanel, Page::osc, 40)

} // namespace synthyboy::ui
