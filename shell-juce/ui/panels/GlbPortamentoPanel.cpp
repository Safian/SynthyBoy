// SynthyBoy - UI panel: Portamento & Legato (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include "../Panel.h"
#include "../ParamHandle.h"

namespace synthyboy::ui
{
namespace
{
/** Pure helper for portamento curve calculation (0 <= t <= 1, -8 <= slope <= 8). Returns 0..1. */
float calculatePortaCurveY (float t, float slope) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);
    slope = juce::jlimit (-8.0f, 8.0f, slope);
    if (std::abs (slope) < 1.0e-3f)
        return t;
    if (slope > 0.0f)
        return 1.0f - std::pow (1.0f - t, 1.0f + slope * 0.4f);
    return std::pow (t, 1.0f - slope * 0.4f);
}
} // namespace

/** Direct manipulation curve view for Portamento: drag vertical = slope, horizontal = glide time. */
class GlbPortaCurveView final : public juce::Component
{
public:
    GlbPortaCurveView (UiContext& context)
        : ctx (context),
          timeHandle (context, "master.portamento.time"),
          slopeHandle (context, "master.portamento.slope"),
          legatoHandle (context, "master.legato"),
          alwaysHandle (context, "master.portamento.always"),
          scaledHandle (context, "master.portamento.scaled"),
          polyHandle (context, "master.polyphony")
    {
        setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
        const auto update = [this] { repaint(); };
        timeHandle.onChange = update;
        slopeHandle.onChange = update;
        legatoHandle.onChange = update;
        alwaysHandle.onChange = update;
        scaledHandle.onChange = update;
        polyHandle.onChange = update;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto& c = t.colours;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (2.0f));

        // Background
        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        // Header / Status badges
        const auto font = t.labelFont();
        g.setFont (font);
        g.setColour (c.textDim);

        const float pad = t.px (8.0f);
        const float headerY = pad + t.px (2.0f);

        // Glide curve coordinate area
        const float graphTop = headerY + t.px (20.0f);
        const float graphBottom = bounds.getBottom() - pad - t.px (18.0f);
        const float graphLeft = bounds.getX() + pad + t.px (10.0f);
        const float graphRight = bounds.getRight() - pad - t.px (10.0f);
        const float graphW = graphRight - graphLeft;
        const float graphH = graphBottom - graphTop;

        if (graphW <= 10.0f || graphH <= 10.0f)
            return;

        // Grid lines
        g.setColour (c.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (graphBottom), graphLeft, graphRight);
        g.drawVerticalLine (juce::roundToInt (graphLeft), graphTop, graphBottom);
        g.drawHorizontalLine (juce::roundToInt (graphTop), graphLeft, graphRight);
        g.drawVerticalLine (juce::roundToInt (graphRight), graphTop, graphBottom);

        const float timeVal = timeHandle.plain();
        const float slopeVal = slopeHandle.plain();

        // Calculate and draw curve
        juce::Path curvePath;
        const int steps = juce::jmax (20, juce::roundToInt (graphW * 0.5f));
        for (int i = 0; i <= steps; ++i)
        {
            const float fracX = static_cast<float> (i) / static_cast<float> (steps);
            const float fracY = calculatePortaCurveY (fracX, slopeVal);
            const float px = graphLeft + fracX * graphW;
            const float py = graphBottom - fracY * graphH;
            if (i == 0)
                curvePath.startNewSubPath (px, py);
            else
                curvePath.lineTo (px, py);
        }

        // Fill area under curve
        juce::Path fillPath = curvePath;
        fillPath.lineTo (graphRight, graphBottom);
        fillPath.lineTo (graphLeft, graphBottom);
        fillPath.closeSubPath();
        g.setColour (c.accent.withAlpha (0.15f));
        g.fillPath (fillPath);

        // Draw curve stroke
        g.setColour (c.accent);
        g.strokePath (curvePath, juce::PathStrokeType (t.px (2.5f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Draw endpoint handle
        const float endX = graphRight;
        const float endY = graphBottom - calculatePortaCurveY (1.0f, slopeVal) * graphH;
        g.setColour (c.accent);
        g.fillEllipse (endX - t.px (5.0f), endY - t.px (5.0f), t.px (10.0f), t.px (10.0f));
        g.setColour (c.textOnAccent);
        g.drawEllipse (endX - t.px (5.0f), endY - t.px (5.0f), t.px (10.0f), t.px (10.0f), 1.5f);

        // Overlay status text
        g.setColour (c.text);
        juce::String slopeDesc = "Linear";
        if (slopeVal > 0.1f)
            slopeDesc = "Convex (Fast start)";
        else if (slopeVal < -0.1f)
            slopeDesc = "Concave (Fast arrival)";

        juce::String infoText = "Glide: " + juce::String (timeVal, 1) + " ms | Slope: "
                              + (slopeVal >= 0.0f ? "+" : "") + juce::String (slopeVal, 1)
                              + " (" + slopeDesc + ")";
        g.drawText (infoText, juce::Rectangle<float> (graphLeft, bounds.getBottom() - pad - t.px (16.0f), graphW, t.px (16.0f)),
                    juce::Justification::centredLeft, true);

        // Status tags at top
        const bool isMono = (polyHandle.plain() <= 1.0f);
        const bool isLegato = (legatoHandle.plain() >= 0.5f);
        const bool isAlways = (alwaysHandle.plain() >= 0.5f);
        const bool isScaled = (scaledHandle.plain() >= 0.5f);

        juce::String status = (isMono ? "[MONO] " : "[POLY] ")
                            + juce::String (isLegato ? "[LEGATO] " : "")
                            + (isAlways ? "[ALWAYS] " : "")
                            + (isScaled ? "[SCALED]" : "");
        g.drawText (status, juce::Rectangle<float> (graphLeft, headerY, graphW, t.px (16.0f)),
                    juce::Justification::centredRight, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        downPos = e.position;
        startSlope = slopeHandle.plain();
        startTimeNorm = timeHandle.normalised();
        timeHandle.beginGesture();
        slopeHandle.beginGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float dx = e.position.x - downPos.x;
        const float dy = e.position.y - downPos.y;
        const float w = static_cast<float> (juce::jmax (10, getWidth()));
        const float h = static_cast<float> (juce::jmax (10, getHeight()));

        // Vertical drag = slope (-8 .. +8)
        const float slopeDelta = - (dy / (h * 0.5f)) * 8.0f;
        const float newSlope = juce::jlimit (-8.0f, 8.0f, startSlope + slopeDelta);
        slopeHandle.setNormalised (toNormalised (slopeHandle.info(), newSlope));

        // Horizontal drag = time (0 .. 8000 ms normalised)
        const float timeDelta = dx / (w * 0.7f);
        const float newNormTime = juce::jlimit (0.0f, 1.0f, startTimeNorm + timeDelta);
        timeHandle.setNormalised (newNormTime);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        timeHandle.endGesture();
        slopeHandle.endGesture();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        timeHandle.resetToDefault();
        slopeHandle.resetToDefault();
        repaint();
    }

private:
    UiContext& ctx;
    ParamHandle timeHandle;
    ParamHandle slopeHandle;
    ParamHandle legatoHandle;
    ParamHandle alwaysHandle;
    ParamHandle scaledHandle;
    ParamHandle polyHandle;

    juce::Point<float> downPos;
    float startSlope = 0.0f;
    float startTimeNorm = 0.0f;
};

class GlbPortamentoPanel final : public Panel
{
public:
    explicit GlbPortamentoPanel (UiContext& context)
        : Panel (context), curveView (context), polyHandle (context, "master.polyphony")
    {
        addAndMakeVisible (curveView);

        // [MONO] pill (design 07 section 3): polyphony 1 <-> the last polyphonic voice count
        monoButton.setClickingTogglesState (false);
        monoButton.setTooltip ("Mono: one voice. Tap again for the last voice count.");
        monoButton.onClick = [this] { toggleMono(); };
        addAndMakeVisible (monoButton);
        polyHandle.onChange = [this] { updateMonoButton(); };
        updateMonoButton();
    }

    /** Mono on = master.polyphony 1; off = the voice count it had before (at least 2). Public for the tests. */
    void toggleMono()
    {
        if (! polyHandle.valid())
            return;
        if (currentPolyphony() > 1.5f)
        {
            lastPolyphony = currentPolyphony();
            polyHandle.setPlainComplete (1.0f);
        }
        else
        {
            polyHandle.setPlainComplete (juce::jlimit (2.0f, polyHandle.info().max, lastPolyphony >= 2.0f ? lastPolyphony : polyHandle.info().def));
        }
    }
    bool isMono() const { return polyHandle.valid() && currentPolyphony() <= 1.5f; }

    juce::String title() const override { return "Portamento / Legato"; }

    std::vector<std::string> keys() const override
    {
        return { "master.portamento.time",
                 "master.portamento.slope",
                 "master.legato",
                 "master.portamento.always",
                 "master.portamento.scaled" };
    }

    void build() override
    {
        // 4 primary controls next to the visual, rest behind More
        addControl ("master.portamento.time");
        addControl ("master.portamento.slope");
        addControl ("master.legato");
        addControl ("master.portamento.always");

        addMoreControl ("master.portamento.scaled");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (120.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap() + t.touch() + t.gap()
             + const_cast<GlbPortamentoPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (120.0f);
        curveView.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        monoButton.setBounds (area.removeFromTop (ctx.theme.touch()).removeFromLeft (juce::jmax (ctx.theme.touch() * 2, ctx.theme.ipx (96.0f))));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

private:
    /** Read from the parameter itself: inside onChange the raw atomic still holds the previous value. */
    float currentPolyphony() const
    {
        const auto& info = polyHandle.info();
        return info.min + polyHandle.normalised() * (info.max - info.min);
    }

    void updateMonoButton()
    {
        monoButton.setToggleState (isMono(), juce::dontSendNotification);
        repaint();
    }

    GlbPortaCurveView curveView;
    ParamHandle polyHandle;
    juce::TextButton monoButton { "MONO" };
    float lastPolyphony = 0.0f;
};

SYNTHYBOY_REGISTER_PANEL (GlbPortamentoPanel, Page::glb, 20)
} // namespace synthyboy::ui
