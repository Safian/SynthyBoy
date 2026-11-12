// SynthyBoy - UI panel: Sub oscillator (docs/ui-design/02-oscillators.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SubPanel.h"

#include <cmath>

namespace synthyboy::ui
{
// ---- SubWaveView -------------------------------------------------------------------------------------------------

SubWaveView::SubWaveView (UiContext& context)
    : ctx (context),
      levelHandle (context, "sub.level"),
      panHandle (context, "sub.pan"),
      onHandle (context, "sub.on"),
      waveHandle (context, "sub.waveform"),
      phaseHandle (context, "sub.phase"),
      contiguousHandle (context, "sub.phase_contiguous")
{
    setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
    const auto repaintCb = [this] { repaint(); };
    phaseHandle.onChange = repaintCb;
    contiguousHandle.onChange = repaintCb;
    levelHandle.onChange = repaintCb;
    panHandle.onChange = repaintCb;
    onHandle.onChange = repaintCb;
    waveHandle.onChange = repaintCb;
}

SubWaveView::~SubWaveView() = default;

int SubWaveView::currentWaveform() const
{
    return waveHandle.valid() ? juce::jlimit (0, 5, static_cast<int> (waveHandle.plain() + 0.5f)) : 0;
}

float SubWaveView::startPhase() const
{
    return phaseHandle.valid() ? juce::jlimit (0.0f, 1.0f, phaseHandle.plain() * 0.01f) : 0.0f;
}

bool SubWaveView::phaseContiguous() const
{
    return contiguousHandle.valid() && contiguousHandle.plain() > 0.5f;
}

void SubWaveView::resized()
{
}

void SubWaveView::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    dragStartPos = e.position;
    if (levelHandle.valid())
    {
        levelHandle.beginGesture();
        dragStartLevel = levelHandle.normalised();
    }
    if (panHandle.valid())
    {
        panHandle.beginGesture();
        dragStartPan = panHandle.normalised();
    }
}

void SubWaveView::mouseDrag (const juce::MouseEvent& e)
{
    dragging = true;
    const float w = static_cast<float> (juce::jmax (1, getWidth()));
    const float h = static_cast<float> (juce::jmax (1, getHeight()));
    const float dx = e.position.x - dragStartPos.x;
    const float dy = e.position.y - dragStartPos.y;

    if (panHandle.valid())
        panHandle.setNormalised (juce::jlimit (0.0f, 1.0f, dragStartPan + dx / w));
    if (levelHandle.valid())
        levelHandle.setNormalised (juce::jlimit (0.0f, 1.0f, dragStartLevel - dy / h));
}

void SubWaveView::mouseUp (const juce::MouseEvent&)
{
    if (levelHandle.valid())
        levelHandle.endGesture();
    if (panHandle.valid())
        panHandle.endGesture();
    dragging = false;
}

void SubWaveView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const bool isOff = onHandle.valid() && onHandle.plain() < 0.5f;

    const float alpha = isOff ? 0.4f : 1.0f;
    g.beginTransparencyLayer (alpha);

    const auto r = getLocalBounds().toFloat().reduced (t.px (1.0f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (r, t.px (8.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (r, t.px (8.0f), 1.0f);

    static const char* const kWaveNames[] = {
        "Sine", "Rounded Rect", "Triangle", "Saw", "Square", "Pulse"
    };
    const int waveIdx = currentWaveform();
    const juce::String waveName = (waveIdx >= 0 && waveIdx < 6) ? kWaveNames[waveIdx] : "Sub";

    g.setFont (t.labelFont());
    g.setColour (c.textDim);
    g.drawText (waveName + " Sub", r.reduced (t.px (8.0f), t.px (4.0f)).removeFromTop (t.px (18.0f)).toNearestInt(), juce::Justification::topLeft, false);

    if (isOff)
    {
        g.setColour (c.warn);
        g.drawText ("OFF", r.reduced (t.px (8.0f), t.px (4.0f)).removeFromTop (t.px (18.0f)).toNearestInt(), juce::Justification::topRight, false);
    }

    const auto scopeArea = r.reduced (t.px (8.0f), t.px (8.0f)).withTrimmedTop (t.px (18.0f));
    const int numPoints = juce::roundToInt (scopeArea.getWidth());
    if (numPoints > 2)
    {
        const float midY = scopeArea.getCentreY();
        const float amp = scopeArea.getHeight() * 0.38f * (levelHandle.valid() ? levelHandle.normalised() : 0.75f);
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        juce::Path p;
        float firstY = midY;
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = scopeArea.getX() + static_cast<float> (i);
            const float phase = (static_cast<float> (i) / static_cast<float> (numPoints)) * 2.0f + startPhase(); // 2 periods, from the start phase
            const float cyclePos = phase - std::floor (phase); // 0..1
            float v = 0.0f;

            switch (waveIdx)
            {
                case 0: // Sine
                    v = std::sin (twoPi * cyclePos);
                    break;
                case 1: // Rounded Rect
                    v = std::tanh (3.0f * std::sin (twoPi * cyclePos));
                    break;
                case 2: // Triangle
                    v = 4.0f * std::abs (cyclePos - 0.5f) - 1.0f;
                    break;
                case 3: // Saw
                    v = 1.0f - 2.0f * cyclePos;
                    break;
                case 4: // Square
                    v = cyclePos < 0.5f ? 1.0f : -1.0f;
                    break;
                case 5: // Pulse (25% duty cycle)
                    v = cyclePos < 0.25f ? 1.0f : -1.0f;
                    break;
                default:
                    v = std::sin (twoPi * cyclePos);
                    break;
            }

            const float y = midY - v * amp;
            if (i == 0)
                firstY = y;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }

        g.setColour (c.accent);
        g.strokePath (p, juce::PathStrokeType (t.px (2.0f)));

        // the point where a note starts, and the tag of the contiguous option
        g.setColour (c.mod);
        g.fillEllipse (scopeArea.getX() - t.px (3.0f), firstY - t.px (3.0f), t.px (6.0f), t.px (6.0f));
        if (phaseContiguous())
        {
            g.setFont (t.labelFont());
            g.setColour (c.textDim);
            g.drawText ("CONT", scopeArea.toNearestInt(), juce::Justification::bottomRight, false);
        }
    }

    g.endTransparencyLayer();
}

// ---- SubPanel ----------------------------------------------------------------------------------------------------

SubPanel::SubPanel (UiContext& context)
    : Panel (context),
      subWaveView (context),
      onHandle (context, "sub.on"),
      waveformHandle (context, "sub.waveform")
{
    addAndMakeVisible (subWaveView);

    static const char* const kPickerLabels[] = {
        "SIN", "RECT", "TRI", "SAW", "SQR", "PULSE"
    };

    for (int i = 0; i < 6; ++i)
    {
        waveformButtons[static_cast<size_t> (i)] = std::make_unique<juce::TextButton> (kPickerLabels[i]);
        auto* btn = waveformButtons[static_cast<size_t> (i)].get();
        btn->onClick = [this, i]
        {
            if (waveformHandle.valid())
            {
                waveformHandle.setPlainComplete (static_cast<float> (i));
                updateWaveformButtons();
                subWaveView.repaint();
            }
        };
        addAndMakeVisible (btn);
    }

    onHandle.onChange = [this]
    {
        repaint();
        subWaveView.repaint();
    };

    waveformHandle.onChange = [this]
    {
        updateWaveformButtons();
        subWaveView.repaint();
    };

    updateWaveformButtons();
}

SubPanel::~SubPanel() = default;

void SubPanel::updateWaveformButtons()
{
    const int current = waveformHandle.valid() ? juce::jlimit (0, 5, static_cast<int> (waveformHandle.plain() + 0.5f)) : 0;
    const auto& t = ctx.theme;
    for (int i = 0; i < 6; ++i)
    {
        const bool active = (i == current);
        waveformButtons[static_cast<size_t> (i)]->setColour (juce::TextButton::buttonColourId,
                                                             active ? t.colours.accent : t.colours.panelRaised);
        waveformButtons[static_cast<size_t> (i)]->setColour (juce::TextButton::textColourOnId,
                                                             active ? t.colours.textOnAccent : t.colours.text);
        waveformButtons[static_cast<size_t> (i)]->setColour (juce::TextButton::textColourOffId,
                                                             active ? t.colours.textOnAccent : t.colours.text);
        waveformButtons[static_cast<size_t> (i)]->repaint();
    }
}

std::vector<std::string> SubPanel::keys() const
{
    return {
        // Primary (4 controls next to the visual, 02-oscillators.md §2.3)
        "sub.level", "sub.pan", "sub.octave", "sub.tune",

        // Behind More (7 controls)
        "sub.on", "sub.destination", "sub.mono", "sub.waveform",
        "sub.phase", "sub.phase_contiguous",
        "sub.filter_blend", "sub.bus1_send", "sub.bus2_send"
    };
}

void SubPanel::build()
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

int SubPanel::preferredHeight (int width) const
{
    const auto& t = ctx.theme;
    const int touchH = t.touch();
    const int visualH = t.ipx (120.0f);
    const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
    return headerHeight() + touchH + visualH + 4 * t.gap() + const_cast<SubPanel*> (this)->layoutControls (area, true);
}

void SubPanel::resized()
{
    const auto& t = ctx.theme;
    const int touchH = t.touch();
    const int visualH = t.ipx (120.0f);
    auto area = contentArea();

    // Waveform picker row (02-27, >= 44pt touch targets)
    auto pickerRow = area.removeFromTop (touchH);
    const int totalW = pickerRow.getWidth();
    const int gap = juce::jmax (1, t.gap() / 2);
    const int btnW = (totalW - 5 * gap) / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto btnArea = (i == 5) ? pickerRow : pickerRow.removeFromLeft (btnW);
        waveformButtons[static_cast<size_t> (i)]->setBounds (btnArea);
        if (i < 5)
            pickerRow.removeFromLeft (gap);
    }

    area.removeFromTop (t.gap());
    auto topArea = area.removeFromTop (visualH);
    subWaveView.setBounds (topArea);
    area.removeFromTop (t.gap());
    layoutControls (area, false);
}

void SubPanel::paint (juce::Graphics& g)
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

SYNTHYBOY_REGISTER_PANEL (SubPanel, Page::osc, 35)

} // namespace synthyboy::ui
