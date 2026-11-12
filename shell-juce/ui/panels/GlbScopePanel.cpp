// SynthyBoy - UI: master oscilloscope / spectrum analyser (see GlbScopePanel.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GlbScopePanel.h"

#include <cmath>

#include "synthyboy/wavetable_edit.h"

namespace synthyboy::ui
{
ScopeView::ScopeView (UiContext& context) : ctx (context)
{
    setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
    fingers.onGesture = [this] (const TouchTracker::Gesture& g) { handleTouchGesture (g); };
    startTimerHz (30);
}

ScopeView::~ScopeView()
{
    stopTimer();
}

void ScopeView::setMode (Mode newMode)
{
    currentMode = newMode;
    refresh();
}

void ScopeView::toggleMode()
{
    setMode (currentMode == Mode::wave ? Mode::fft : Mode::wave);
}

void ScopeView::handleTouchGesture (const TouchTracker::Gesture& g)
{
    if (g.kind == TouchTracker::Kind::pinch)
        toggleMode();
}

void ScopeView::refresh()
{
    numSamples = ctx.host.copyScope (samples.data(), static_cast<int> (samples.size()));
    lastPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        lastPeak = juce::jmax (lastPeak, std::abs (samples[static_cast<size_t> (i)]));

    spectrum.clear();
    if (currentMode == Mode::fft && numSamples == static_cast<int> (samples.size()))
    {
        std::array<float, 2048> windowed {};
        for (size_t i = 0; i < windowed.size(); ++i)
            windowed[i] = samples[i] * (0.5f - 0.5f * std::cos (6.2831853f * static_cast<float> (i) / static_cast<float> (windowed.size() - 1)));
        std::array<float, 2048> mags {}, phases {};
        wtedit::Radix2Fft::realToPolar (windowed.data(), mags.data(), phases.data(), windowed.size());
        spectrum.resize (1024);
        for (size_t k = 0; k < spectrum.size(); ++k)
            spectrum[k] = juce::jlimit (-100.0f, 0.0f, 20.0f * std::log10 (juce::jmax (1.0e-6f, mags[k + 1] * 2.0f)));
    }
    repaint();
    if (onRefresh)
        onRefresh();
}

void ScopeView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const auto& c = t.colours;
    const auto r = getLocalBounds().toFloat().reduced (t.px (1.0f));
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (r, t.px (6.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (r, t.px (6.0f), 1.0f);
    const auto area = r.reduced (t.px (6.0f));

    g.setColour (c.outline.withAlpha (0.5f));
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

    juce::Path path;
    if (currentMode == Mode::wave)
    {
        // the first rising zero crossing starts the trace, so a steady tone stands still
        int start = 0;
        for (int i = 1; i < juce::jmin (numSamples, 1024); ++i)
            if (samples[static_cast<size_t> (i) - 1] < 0.0f && samples[static_cast<size_t> (i)] >= 0.0f)
            {
                start = i;
                break;
            }
        const int count = juce::jmax (0, juce::jmin (numSamples - start, 1024));
        for (int i = 0; i < count; ++i)
        {
            const float x = area.getX() + area.getWidth() * static_cast<float> (i) / 1023.0f;
            const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, samples[static_cast<size_t> (start + i)]) * area.getHeight() * 0.45f;
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
    }
    else if (! spectrum.empty())
    {
        // log frequency axis over the bins, dB on the vertical axis (-100 .. 0)
        const float logMax = std::log (static_cast<float> (spectrum.size()));
        for (size_t k = 0; k < spectrum.size(); ++k)
        {
            const float x = area.getX() + area.getWidth() * std::log (static_cast<float> (k) + 1.0f) / logMax;
            const float y = area.getBottom() - (spectrum[k] + 100.0f) / 100.0f * area.getHeight();
            if (k == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
    }
    g.setColour (c.accent);
    g.strokePath (path, juce::PathStrokeType (t.px (1.5f)));

    g.setColour (c.textDim);
    g.setFont (t.labelFont());
    g.drawText (currentMode == Mode::wave ? "Wave" : "FFT", area.toNearestInt(), juce::Justification::topRight, false);
    if (numSamples == 0)
        g.drawText ("no signal yet", area.toNearestInt(), juce::Justification::centred, false);
}

void ScopeView::mouseDown (const juce::MouseEvent& e)
{
    fingers.down (e);
    pairActive = e.source.getIndex() > 0 || fingers.twoDown();
    moved = false;
}

void ScopeView::mouseDrag (const juce::MouseEvent& e)
{
    fingers.drag (e);
    if (e.getDistanceFromDragStart() > 6)
        moved = true;
}

void ScopeView::mouseUp (const juce::MouseEvent& e)
{
    fingers.up (e);
    if (e.source.getIndex() > 0)
        return;
    if (! pairActive && ! moved && getLocalBounds().contains (e.getPosition()))
        toggleMode(); // a tap
    pairActive = fingers.twoDown();
}

void ScopeView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    fingers.magnify (e.position, scaleFactor);
}

// ---- GlbScopePanel ------------------------------------------------------------------------------------------------------------------

GlbScopePanel::GlbScopePanel (UiContext& context) : Panel (context), polyHandle (context, "master.polyphony") {}
GlbScopePanel::~GlbScopePanel() = default;

void GlbScopePanel::build()
{
    view = std::make_unique<ScopeView> (ctx);
    view->onRefresh = [this] { repaint (getLocalBounds().removeFromBottom (ctx.theme.ipx (28.0f))); };
    addAndMakeVisible (*view);
    waveBtn.setClickingTogglesState (false);
    fftBtn.setClickingTogglesState (false);
    waveBtn.onClick = [this] { view->setMode (ScopeView::Mode::wave); waveBtn.setToggleState (true, juce::dontSendNotification); fftBtn.setToggleState (false, juce::dontSendNotification); };
    fftBtn.onClick = [this] { view->setMode (ScopeView::Mode::fft); waveBtn.setToggleState (false, juce::dontSendNotification); fftBtn.setToggleState (true, juce::dontSendNotification); };
    waveBtn.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (waveBtn);
    addAndMakeVisible (fftBtn);
}

int GlbScopePanel::preferredHeight (int) const
{
    const auto& t = ctx.theme;
    return headerHeight() + 3 * t.gap() + t.touch() + t.ipx (110.0f) + t.ipx (20.0f);
}

void GlbScopePanel::resized()
{
    const auto& t = ctx.theme;
    auto area = contentArea();
    auto row = area.removeFromTop (t.touch());
    const int w = juce::jmax (t.touch() * 2, t.ipx (80.0f));
    fftBtn.setBounds (row.removeFromRight (w));
    row.removeFromRight (t.gap());
    waveBtn.setBounds (row.removeFromRight (w));
    area.removeFromTop (t.gap());
    area.removeFromBottom (t.ipx (20.0f));
    view->setBounds (area);
}

juce::String GlbScopePanel::readout() const
{
    const int cap = polyHandle.valid() ? juce::roundToInt (polyHandle.plain()) : 0;
    return juce::String (ctx.host.activeVoiceCount()) + " / " + juce::String (cap) + " voices   CPU " + juce::String (juce::roundToInt (ctx.host.cpuLoad() * 100.0f)) + " %";
}

void GlbScopePanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    const auto& t = ctx.theme;
    g.setColour (t.colours.textDim);
    g.setFont (t.labelFont());
    g.drawText (readout(), getLocalBounds().reduced (t.gap(), t.gap()).removeFromBottom (t.ipx (18.0f)), juce::Justification::centredLeft, false);
}

SYNTHYBOY_REGISTER_PANEL (GlbScopePanel, Page::glb, 75)
} // namespace synthyboy::ui
