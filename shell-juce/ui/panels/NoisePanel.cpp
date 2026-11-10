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
      sampleHandle (context, "noise.sample"),
      typeHandle (context, "noise.type")
{
    addAndMakeVisible (noiseView);
    addAndMakeVisible (loadBtn);
    addAndMakeVisible (removeBtn);
    addAndMakeVisible (prevSampleBtn);
    addAndMakeVisible (sampleNameBtn);
    addAndMakeVisible (nextSampleBtn);

    prevSampleBtn.onClick = [this] { stepSample (-1); };
    nextSampleBtn.onClick = [this] { stepSample (1); };
    sampleNameBtn.onClick = [this] { showSampleMenu(); };
    loadBtn.onClick = [this] { requestSampleFile(); };
    removeBtn.onClick = [this] { removeSample(); };
    typeHandle.onChange = [this]
    {
        updateSampleButtonText();
        noiseView.repaint();
    };

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
    if (typeHandle.valid() && usesSample())
        typeHandle.setPlainComplete (0.0f); // stepping through the generators leaves the content sample
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
    const bool sample = usesSample();
    for (int i = 0; i < 9; ++i)
        menu.addItem (i + 1, kSampleNames[i], true, ! sample && now == i);
    menu.addSeparator();
    const auto content = ctx.host.content(); // a copy: the pointer below points into it
    const ContentSample* loaded = sampleOf (content, kNoiseSampleOsc);
    if (loaded != nullptr)
        menu.addItem (30, juce::String (loaded->ref.name), true, sample);
    menu.addItem (31, "Load Sample...", ctx.host.canRequestFile());
    menu.addItem (32, "Remove Sample", loaded != nullptr);
    menu.showMenuAsync (popupOptions (sampleNameBtn),
                        [safe = juce::Component::SafePointer<NoisePanel> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0 || ! safe->sampleHandle.valid())
                                return;
                            if (result <= 9)
                            {
                                if (safe->typeHandle.valid())
                                    safe->typeHandle.setPlainComplete (0.0f);
                                safe->sampleHandle.setPlainComplete (static_cast<float> (result - 1));
                            }
                            else if (result == 30 && safe->typeHandle.valid())
                                safe->typeHandle.setPlainComplete (1.0f);
                            else if (result == 31)
                                safe->requestSampleFile();
                            else if (result == 32)
                                safe->removeSample();
                            safe->updateSampleButtonText();
                            safe->noiseView.repaint();
                        });
}

bool NoisePanel::usesSample() const
{
    return typeHandle.valid() && typeHandle.plain() > 0.5f;
}

void NoisePanel::requestSampleFile()
{
    FileRequest request;
    request.title = "Load a sample into the noise oscillator";
    request.wildcards = audioFileWildcard();
    request.purpose = "sample";
    if (! ctx.host.requestFile (request, [safe = juce::Component::SafePointer<NoisePanel> (this)] (const juce::File& file)
        {
            if (safe != nullptr)
                safe->loadSampleFile (file);
        }))
    {
        statusLine = "This host cannot ask for a file";
        repaint();
    }
}

FlowResult NoisePanel::loadSampleFile (const juce::File& file)
{
    const FlowResult result = synthyboy::ui::loadSampleFile (ctx, kNoiseSampleOsc, file);
    statusLine = result.message;
    noiseView.setStatus (statusLine);
    updateSampleButtonText();
    repaint();
    return result;
}

FlowResult NoisePanel::removeSample()
{
    const FlowResult result = synthyboy::ui::removeSample (ctx, kNoiseSampleOsc);
    statusLine = result.message;
    noiseView.setStatus (statusLine);
    updateSampleButtonText();
    repaint();
    return result;
}

void NoisePanel::updateSampleButtonText()
{
    static const char* const kSampleNames[] = {
        "White Noise", "Pink Noise", "Brown Noise", "Geiger",
        "Tape Hiss", "Vinyl Clicks", "Digital Hash", "Velvet Noise", "Atmosphere"
    };
    const int now = sampleHandle.valid() ? juce::jlimit (0, 8, static_cast<int> (sampleHandle.plain() + 0.5f)) : 0;
    const auto content = ctx.host.content(); // a copy: the pointer below points into it
    const ContentSample* loaded = sampleOf (content, kNoiseSampleOsc);
    // noise.type = Sample: the file's name (without the content the engine plays the generator, so the generator's name stays)
    sampleNameBtn.setButtonText (usesSample() && loaded != nullptr ? juce::String (loaded->ref.name) : juce::String (kSampleNames[now]));
    removeBtn.setEnabled (loaded != nullptr);
    loadBtn.setEnabled (ctx.host.canRequestFile());
}

std::vector<std::string> NoisePanel::keys() const
{
    return {
        // Primary (4 controls next to the visual, 02-oscillators.md §2.3)
        "noise.level", "noise.pan", "noise.pitch", "noise.fine",

        // Behind More (10 controls)
        "noise.on", "noise.type", "noise.sample", "noise.destination", "noise.phase",
        "noise.random_phase", "noise.keytrack", "noise.loop", "noise.bounce",
        "noise.stereo", "noise.filter",
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
    return headerHeight() + 2 * touchH + visualH + 5 * t.gap() + const_cast<NoisePanel*> (this)->layoutControls (area, true);
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

    // the file row: Load... and Remove (the sample of the noise source, WP-01 flow)
    area.removeFromTop (t.gap());
    auto fileRow = area.removeFromTop (touchH);
    removeBtn.setBounds (fileRow.removeFromRight (fileRow.getWidth() / 3));
    fileRow.removeFromRight (t.gap());
    loadBtn.setBounds (fileRow);

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
