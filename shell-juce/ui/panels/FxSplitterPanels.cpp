// SynthyBoy - UI panels: Splitter L/H, Splitter L/M/H, Splitter M/S (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
// ==============================================================================
// 1. Splitter L/H (Low / High Band Crossover)
// ==============================================================================
class FxSplitterLhView final : public FxVisualBase
{
public:
    explicit FxSplitterLhView (UiContext& context)
        : FxVisualBase (context, "2-Band Crossover (Drag Line: Split Frequency)"),
          freqHandle (context, "splitter_lh.freq"),
          lowBypHandle (context, "splitter_lh.low_bypass"),
          highBypHandle (context, "splitter_lh.high_bypass")
    {
        freqHandle.onChange = [this] { repaint(); };
        lowBypHandle.onChange = [this] { repaint(); };
        highBypHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 4, 3);

        const float normFreq = fxFreqToNorm (freqHandle.plain());
        const float splitX = area.getX() + area.getWidth() * normFreq;

        const bool loByp = (lowBypHandle.plain() >= 0.5f);
        const bool hiByp = (highBypHandle.plain() >= 0.5f);

        // Low Band Region
        const auto loRect = juce::Rectangle<float> (area.getX(), area.getY(), splitX - area.getX(), area.getHeight());
        g.setColour (loByp ? t.colours.track.withAlpha (0.3f) : t.colours.accentDim.withAlpha (0.25f));
        g.fillRect (loRect);

        // High Band Region
        const auto hiRect = juce::Rectangle<float> (splitX, area.getY(), area.getRight() - splitX, area.getHeight());
        g.setColour (hiByp ? t.colours.track.withAlpha (0.3f) : t.colours.accentDim.withAlpha (0.15f));
        g.fillRect (hiRect);

        // Split frequency divider line
        g.setColour (t.colours.accent);
        g.drawLine (splitX, area.getY(), splitX, area.getBottom(), t.px (2.5f));

        // Node handle on split line
        drawNode (g, juce::Point<float> (splitX, area.getCentreY()), draggingSplit,
                  juce::String (juce::roundToInt (freqHandle.plain())) + " Hz");

        // Labels
        g.setColour (loByp ? t.colours.warn : t.colours.text);
        g.setFont (t.font (t.px (10.0f), true));
        g.drawText (loByp ? "LOW (BYP)" : "LOW BAND", loRect.reduced (t.px (4.0f)), juce::Justification::topLeft, false);

        g.setColour (hiByp ? t.colours.warn : t.colours.text);
        g.drawText (hiByp ? "HIGH (BYP)" : "HIGH BAND", hiRect.reduced (t.px (4.0f)), juce::Justification::topRight, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        if (area.contains (e.position))
        {
            draggingSplit = true;
            freqHandle.beginGesture();
            updateFromMouse (e.position, area);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingSplit)
        {
            const auto& t = ctx.theme;
            const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
            updateFromMouse (e.position, area);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingSplit)
        {
            freqHandle.endGesture();
            draggingSplit = false;
        }
    }

private:
    void updateFromMouse (juce::Point<float> pos, juce::Rectangle<float> area)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
        freqHandle.setNormalised (normX);
        repaint();
    }

    ParamHandle freqHandle;
    ParamHandle lowBypHandle;
    ParamHandle highBypHandle;
    bool draggingSplit = false;
};

class FxSplitterLhPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Splitter L/H"; }

    std::vector<std::string> keys() const override
    {
        return {
            "splitter_lh.freq", "splitter_lh.level",
            "splitter_lh.on", "splitter_lh.low_bypass", "splitter_lh.high_bypass", "splitter_lh.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxSplitterLhView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("splitter_lh.freq");
        addControl ("splitter_lh.level");

        // More controls (4 keys)
        addMoreControl ("splitter_lh.on");
        addMoreControl ("splitter_lh.low_bypass");
        addMoreControl ("splitter_lh.high_bypass");
        addMoreControl ("splitter_lh.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (110.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxSplitterLhPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (110.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxSplitterLhView> visual;
};

// ==============================================================================
// 2. Splitter L/M/H (3-Band Crossover)
// ==============================================================================
class FxSplitterLmhView final : public FxVisualBase
{
public:
    explicit FxSplitterLmhView (UiContext& context)
        : FxVisualBase (context, "3-Band Crossover (Drag Lines: Split Low/High)"),
          freqLowHandle (context, "splitter_lmh.freq_low"),
          freqHighHandle (context, "splitter_lmh.freq_high"),
          lowBypHandle (context, "splitter_lmh.low_bypass"),
          midBypHandle (context, "splitter_lmh.mid_bypass"),
          highBypHandle (context, "splitter_lmh.high_bypass")
    {
        freqLowHandle.onChange = [this] { repaint(); };
        freqHighHandle.onChange = [this] { repaint(); };
        lowBypHandle.onChange = [this] { repaint(); };
        midBypHandle.onChange = [this] { repaint(); };
        highBypHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        drawGrid (g, area, 5, 3);

        const float normLo = fxFreqToNorm (freqLowHandle.plain());
        const float normHi = fxFreqToNorm (freqHighHandle.plain());
        const float loX = area.getX() + area.getWidth() * normLo;
        const float hiX = area.getX() + area.getWidth() * normHi;

        const bool loByp = (lowBypHandle.plain() >= 0.5f);
        const bool midByp = (midBypHandle.plain() >= 0.5f);
        const bool hiByp = (highBypHandle.plain() >= 0.5f);

        // Bands shading
        g.setColour (loByp ? t.colours.track.withAlpha (0.3f) : t.colours.accentDim.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), loX - area.getX(), area.getHeight()));

        g.setColour (midByp ? t.colours.track.withAlpha (0.3f) : t.colours.accentDim.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (loX, area.getY(), hiX - loX, area.getHeight()));

        g.setColour (hiByp ? t.colours.track.withAlpha (0.3f) : t.colours.accentDim.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (hiX, area.getY(), area.getRight() - hiX, area.getHeight()));

        // Divider lines
        g.setColour (t.colours.accent);
        g.drawLine (loX, area.getY(), loX, area.getBottom(), t.px (2.0f));
        g.drawLine (hiX, area.getY(), hiX, area.getBottom(), t.px (2.0f));

        // Node handles
        drawNode (g, juce::Point<float> (loX, area.getCentreY()), draggingLo,
                  juce::String (juce::roundToInt (freqLowHandle.plain())) + " Hz");
        drawNode (g, juce::Point<float> (hiX, area.getCentreY()), draggingHi,
                  juce::String (juce::roundToInt (freqHighHandle.plain())) + " Hz");

        // Band titles
        g.setFont (t.font (t.px (9.0f), true));
        g.setColour (loByp ? t.colours.warn : t.colours.text);
        g.drawText (loByp ? "LOW (BYP)" : "LOW", juce::Rectangle<float> (area.getX(), area.getY(), loX - area.getX(), t.px (14.0f)), juce::Justification::centred, false);

        g.setColour (midByp ? t.colours.warn : t.colours.text);
        g.drawText (midByp ? "MID (BYP)" : "MID", juce::Rectangle<float> (loX, area.getY(), hiX - loX, t.px (14.0f)), juce::Justification::centred, false);

        g.setColour (hiByp ? t.colours.warn : t.colours.text);
        g.drawText (hiByp ? "HIGH (BYP)" : "HIGH", juce::Rectangle<float> (hiX, area.getY(), area.getRight() - hiX, t.px (14.0f)), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        const float normLo = fxFreqToNorm (freqLowHandle.plain());
        const float normHi = fxFreqToNorm (freqHighHandle.plain());
        const float loX = area.getX() + area.getWidth() * normLo;
        const float hiX = area.getX() + area.getWidth() * normHi;

        if (std::abs (e.position.x - loX) < std::abs (e.position.x - hiX))
        {
            draggingLo = true;
            freqLowHandle.beginGesture();
        }
        else
        {
            draggingHi = true;
            freqHighHandle.beginGesture();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        const float normX = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());

        if (draggingLo)
        {
            const float maxNorm = fxFreqToNorm (freqHighHandle.plain()) - 0.02f;
            freqLowHandle.setNormalised (juce::jlimit (0.0f, juce::jmax (0.0f, maxNorm), normX));
            repaint();
        }
        else if (draggingHi)
        {
            const float minNorm = fxFreqToNorm (freqLowHandle.plain()) + 0.02f;
            freqHighHandle.setNormalised (juce::jlimit (juce::jmin (1.0f, minNorm), 1.0f, normX));
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingLo)
        {
            freqLowHandle.endGesture();
            draggingLo = false;
        }
        if (draggingHi)
        {
            freqHighHandle.endGesture();
            draggingHi = false;
        }
    }

private:
    ParamHandle freqLowHandle;
    ParamHandle freqHighHandle;
    ParamHandle lowBypHandle;
    ParamHandle midBypHandle;
    ParamHandle highBypHandle;

    bool draggingLo = false;
    bool draggingHi = false;
};

class FxSplitterLmhPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Splitter L/M/H"; }

    std::vector<std::string> keys() const override
    {
        return {
            "splitter_lmh.freq_low", "splitter_lmh.freq_high", "splitter_lmh.level",
            "splitter_lmh.on", "splitter_lmh.low_bypass", "splitter_lmh.mid_bypass", "splitter_lmh.high_bypass", "splitter_lmh.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxSplitterLmhView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("splitter_lmh.freq_low");
        addControl ("splitter_lmh.freq_high");
        addControl ("splitter_lmh.level");

        // More controls (5 keys)
        addMoreControl ("splitter_lmh.on");
        addMoreControl ("splitter_lmh.low_bypass");
        addMoreControl ("splitter_lmh.mid_bypass");
        addMoreControl ("splitter_lmh.high_bypass");
        addMoreControl ("splitter_lmh.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (110.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxSplitterLmhPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (110.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxSplitterLmhView> visual;
};

// ==============================================================================
// 3. Splitter M/S (Mid / Side Splitter)
// ==============================================================================
class FxSplitterMsView final : public FxVisualBase
{
public:
    explicit FxSplitterMsView (UiContext& context)
        : FxVisualBase (context, "Mid / Side Splitter (Mid & Side Processing Fields)"),
          midBypHandle (context, "splitter_ms.mid_bypass"),
          sideBypHandle (context, "splitter_ms.side_bypass"),
          levelHandle (context, "splitter_ms.level")
    {
        midBypHandle.onChange = [this] { repaint(); };
        sideBypHandle.onChange = [this] { repaint(); };
        levelHandle.onChange = [this] { repaint(); };
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        const float midX = area.getCentreX();
        const float midY = area.getCentreY();
        const float radius = juce::jmin (area.getWidth() * 0.22f, area.getHeight() * 0.38f);

        const bool midByp = (midBypHandle.plain() >= 0.5f);
        const bool sideByp = (sideBypHandle.plain() >= 0.5f);

        // Center circle: Mid signal (mono center)
        const auto midCircle = juce::Rectangle<float> (midX - radius, midY - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (midByp ? t.colours.track.withAlpha (0.3f) : t.colours.accentDim.withAlpha (0.25f));
        g.fillEllipse (midCircle);
        g.setColour (midByp ? t.colours.warn : t.colours.accent);
        g.drawEllipse (midCircle, t.px (2.0f));

        // Side lobes: Left and Right lobes
        const auto leftLobe = juce::Rectangle<float> (midX - radius * 2.0f, midY - radius * 0.8f, radius * 1.5f, radius * 1.6f);
        const auto rightLobe = juce::Rectangle<float> (midX + radius * 0.5f, midY - radius * 0.8f, radius * 1.5f, radius * 1.6f);

        g.setColour (sideByp ? t.colours.track.withAlpha (0.2f) : t.colours.track.withAlpha (0.4f));
        g.drawEllipse (leftLobe, t.px (1.5f));
        g.drawEllipse (rightLobe, t.px (1.5f));

        // Text
        g.setFont (t.font (t.px (10.0f), true));
        g.setColour (midByp ? t.colours.warn : t.colours.text);
        g.drawText (midByp ? "MID (BYP)" : "MID", midCircle, juce::Justification::centred, false);

        g.setColour (sideByp ? t.colours.warn : t.colours.text);
        g.drawText (sideByp ? "SIDE (BYP)" : "SIDE", juce::Rectangle<float> (area.getX() + t.px (4.0f), area.getY(), t.px (60.0f), t.px (16.0f)), juce::Justification::topLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));
        const float midX = area.getCentreX();
        const float midY = area.getCentreY();
        const float radius = juce::jmin (area.getWidth() * 0.22f, area.getHeight() * 0.38f);

        // Tap inside center circle toggles mid bypass; tap outside toggles side bypass
        if (e.position.getDistanceSquaredFrom ({ midX, midY }) < radius * radius)
        {
            const float curr = midBypHandle.plain();
            midBypHandle.setPlainComplete (curr >= 0.5f ? 0.0f : 1.0f);
        }
        else
        {
            const float curr = sideBypHandle.plain();
            sideBypHandle.setPlainComplete (curr >= 0.5f ? 0.0f : 1.0f);
        }
        repaint();
    }

private:
    ParamHandle midBypHandle;
    ParamHandle sideBypHandle;
    ParamHandle levelHandle;
};

class FxSplitterMsPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Splitter M/S"; }

    std::vector<std::string> keys() const override
    {
        return {
            "splitter_ms.level",
            "splitter_ms.on", "splitter_ms.mid_bypass", "splitter_ms.side_bypass", "splitter_ms.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxSplitterMsView> (ctx);
        addAndMakeVisible (*visual);

        // Primary (1 key)
        addControl ("splitter_ms.level");

        // More controls (4 keys)
        addMoreControl ("splitter_ms.on");
        addMoreControl ("splitter_ms.mid_bypass");
        addMoreControl ("splitter_ms.side_bypass");
        addMoreControl ("splitter_ms.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (110.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxSplitterMsPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (110.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxSplitterMsView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxSplitterLhPanel, Page::fx, 150)
SYNTHYBOY_REGISTER_PANEL (FxSplitterLmhPanel, Page::fx, 160)
SYNTHYBOY_REGISTER_PANEL (FxSplitterMsPanel, Page::fx, 170)
} // namespace synthyboy::ui
