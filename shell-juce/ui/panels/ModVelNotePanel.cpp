// SynthyBoy - UI panel: Velocity & Note Curves (docs/ui-design/06-modulation.md, page `mod`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "ModLogic.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace synthyboy::ui
{
namespace
{
/** Interactive response curve visual for Velocity & Note curves. */
class ModResponseCurveView final : public juce::Component
{
public:
    explicit ModResponseCurveView (UiContext& context)
        : ctx (context)
    {
        resetPoints();
    }

    void setMode (bool isNote)
    {
        noteMode = isNote;
        repaint();
    }

    bool isNoteMode() const noexcept { return noteMode; }

    void resetPoints()
    {
        points = {
            { 0.0f, 0.0f },
            { 0.25f, 0.25f },
            { 0.50f, 0.50f },
            { 0.75f, 0.75f },
            { 1.0f, 1.0f }
        };
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (4.0f));

        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        const auto plotArea = bounds.reduced (t.px (28.0f), t.px (18.0f));
        if (plotArea.getWidth() <= 10.0f || plotArea.getHeight() <= 10.0f)
            return;

        // Grid lines
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (plotArea.getY()), plotArea.getX(), plotArea.getRight());
        g.drawHorizontalLine (juce::roundToInt (plotArea.getCentreY()), plotArea.getX(), plotArea.getRight());
        g.drawHorizontalLine (juce::roundToInt (plotArea.getBottom()), plotArea.getX(), plotArea.getRight());

        const float quarterW = plotArea.getWidth() * 0.25f;
        for (int i = 0; i <= 4; ++i)
            g.drawVerticalLine (juce::roundToInt (plotArea.getX() + static_cast<float> (i) * quarterW), plotArea.getY(), plotArea.getBottom());

        // Labels
        g.setFont (t.font (t.px (10.0f)));
        g.setColour (t.colours.textDim);
        g.drawText ("100%", juce::Rectangle<float> (plotArea.getX() - t.px (26.0f), plotArea.getY() - t.px (6.0f), t.px (24.0f), t.px (12.0f)), juce::Justification::centredRight, false);
        g.drawText ("50%", juce::Rectangle<float> (plotArea.getX() - t.px (26.0f), plotArea.getCentreY() - t.px (6.0f), t.px (24.0f), t.px (12.0f)), juce::Justification::centredRight, false);
        g.drawText ("0%", juce::Rectangle<float> (plotArea.getX() - t.px (26.0f), plotArea.getBottom() - t.px (6.0f), t.px (24.0f), t.px (12.0f)), juce::Justification::centredRight, false);

        if (noteMode)
        {
            g.drawText ("C-1 (0)", juce::Rectangle<float> (plotArea.getX() - t.px (10.0f), plotArea.getBottom() + t.px (2.0f), t.px (45.0f), t.px (12.0f)), juce::Justification::centredLeft, false);
            g.drawText ("C1 (36)", juce::Rectangle<float> (plotArea.getX() + quarterW - t.px (16.0f), plotArea.getBottom() + t.px (2.0f), t.px (40.0f), t.px (12.0f)), juce::Justification::centred, false);
            g.drawText ("C3 (60)", juce::Rectangle<float> (plotArea.getX() + 2 * quarterW - t.px (16.0f), plotArea.getBottom() + t.px (2.0f), t.px (40.0f), t.px (12.0f)), juce::Justification::centred, false);
            g.drawText ("C5 (84)", juce::Rectangle<float> (plotArea.getX() + 3 * quarterW - t.px (16.0f), plotArea.getBottom() + t.px (2.0f), t.px (40.0f), t.px (12.0f)), juce::Justification::centred, false);
            g.drawText ("G9 (127)", juce::Rectangle<float> (plotArea.getRight() - t.px (40.0f), plotArea.getBottom() + t.px (2.0f), t.px (45.0f), t.px (12.0f)), juce::Justification::centredRight, false);
        }
        else
        {
            g.drawText ("0", juce::Rectangle<float> (plotArea.getX(), plotArea.getBottom() + t.px (2.0f), t.px (20.0f), t.px (12.0f)), juce::Justification::centredLeft, false);
            g.drawText ("32", juce::Rectangle<float> (plotArea.getX() + quarterW - t.px (10.0f), plotArea.getBottom() + t.px (2.0f), t.px (20.0f), t.px (12.0f)), juce::Justification::centred, false);
            g.drawText ("64", juce::Rectangle<float> (plotArea.getX() + 2 * quarterW - t.px (10.0f), plotArea.getBottom() + t.px (2.0f), t.px (20.0f), t.px (12.0f)), juce::Justification::centred, false);
            g.drawText ("96", juce::Rectangle<float> (plotArea.getX() + 3 * quarterW - t.px (10.0f), plotArea.getBottom() + t.px (2.0f), t.px (20.0f), t.px (12.0f)), juce::Justification::centred, false);
            g.drawText ("127", juce::Rectangle<float> (plotArea.getRight() - t.px (20.0f), plotArea.getBottom() + t.px (2.0f), t.px (20.0f), t.px (12.0f)), juce::Justification::centredRight, false);
        }

        // Draw curve connecting points
        juce::Path p;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float px = plotArea.getX() + points[i].x * plotArea.getWidth();
            const float py = plotArea.getBottom() - points[i].y * plotArea.getHeight();
            if (i == 0)
                p.startNewSubPath (px, py);
            else
                p.lineTo (px, py);
        }

        g.setColour (t.colours.mod.withAlpha (0.35f));
        g.strokePath (p, juce::PathStrokeType (t.px (4.0f)));
        g.setColour (t.colours.mod);
        g.strokePath (p, juce::PathStrokeType (t.px (2.0f)));

        // Draw point handles
        const float r = t.px (5.0f);
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float px = plotArea.getX() + points[i].x * plotArea.getWidth();
            const float py = plotArea.getBottom() - points[i].y * plotArea.getHeight();
            g.setColour (t.colours.accent);
            g.fillEllipse (px - r, py - r, r * 2.0f, r * 2.0f);
            g.setColour (t.colours.text);
            g.drawEllipse (px - r, py - r, r * 2.0f, r * 2.0f, 1.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto plotArea = getLocalBounds().toFloat().reduced (ctx.theme.px (28.0f), ctx.theme.px (18.0f));
        if (plotArea.getWidth() <= 10.0f || plotArea.getHeight() <= 10.0f)
            return;

        selectedPoint = -1;
        float minDistSq = 20.0f * 20.0f;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const float px = plotArea.getX() + points[i].x * plotArea.getWidth();
            const float py = plotArea.getBottom() - points[i].y * plotArea.getHeight();
            const float dx = e.position.x - px;
            const float dy = e.position.y - py;
            const float distSq = dx * dx + dy * dy;
            if (distSq < minDistSq)
            {
                minDistSq = distSq;
                selectedPoint = static_cast<int> (i);
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (selectedPoint < 0 || selectedPoint >= static_cast<int> (points.size()))
            return;

        const auto plotArea = getLocalBounds().toFloat().reduced (ctx.theme.px (28.0f), ctx.theme.px (18.0f));
        if (plotArea.getWidth() <= 10.0f || plotArea.getHeight() <= 10.0f)
            return;

        const float normY = std::clamp ((plotArea.getBottom() - e.position.y) / plotArea.getHeight(), 0.0f, 1.0f);
        points[static_cast<size_t> (selectedPoint)].y = normY;

        // Endpoints x are fixed at 0 and 1; middle points can move horizontally between neighbors
        if (selectedPoint > 0 && selectedPoint < static_cast<int> (points.size()) - 1)
        {
            const float minX = points[static_cast<size_t> (selectedPoint - 1)].x + 0.05f;
            const float maxX = points[static_cast<size_t> (selectedPoint + 1)].x - 0.05f;
            const float normX = std::clamp ((e.position.x - plotArea.getX()) / plotArea.getWidth(), minX, maxX);
            points[static_cast<size_t> (selectedPoint)].x = normX;
        }

        repaint();
    }

private:
    struct Point { float x, y; };
    UiContext& ctx;
    bool noteMode = false;
    std::vector<Point> points;
    int selectedPoint = -1;
};
} // namespace

class ModVelNotePanel final : public Panel
{
public:
    explicit ModVelNotePanel (UiContext& context)
        : Panel (context), curveView (context)
    {
    }

    juce::String title() const override { return "Velocity & Note Curves"; }

    std::vector<std::string> keys() const override
    {
        return {
            "master.velocity_track",
            "curve.velocity.legato",
            "curve.note.legato"
        };
    }

    void build() override
    {
        const auto all = keys();
        for (const auto& k : all)
            addControl (k);

        veloTabButton.setButtonText ("Velocity Curve");
        veloTabButton.setClickingTogglesState (true);
        veloTabButton.setToggleState (true, juce::dontSendNotification);
        veloTabButton.onClick = [this] {
            curveView.setMode (false);
            veloTabButton.setToggleState (true, juce::dontSendNotification);
            noteTabButton.setToggleState (false, juce::dontSendNotification);
        };
        addAndMakeVisible (veloTabButton);

        noteTabButton.setButtonText ("Note Curve");
        noteTabButton.setClickingTogglesState (true);
        noteTabButton.setToggleState (false, juce::dontSendNotification);
        noteTabButton.onClick = [this] {
            curveView.setMode (true);
            noteTabButton.setToggleState (true, juce::dontSendNotification);
            veloTabButton.setToggleState (false, juce::dontSendNotification);
        };
        addAndMakeVisible (noteTabButton);

        gapBadge.setText ("GAP", juce::dontSendNotification);
        gapBadge.setJustificationType (juce::Justification::centred);
        gapBadge.setColour (juce::Label::backgroundColourId, ctx.theme.colours.warn);
        gapBadge.setColour (juce::Label::textColourId, ctx.theme.colours.textOnAccent);
        gapBadge.setFont (ctx.theme.font (ctx.theme.px (9.0f), true));
        gapBadge.setTooltip ("Custom multi-point curve storage is an engine GAP feature.");
        addAndMakeVisible (gapBadge);

        initButton.setButtonText ("Init Graph");
        initButton.setTooltip ("Reset curve points to linear 1:1 default");
        initButton.onClick = [this] { curveView.resetPoints(); };
        addAndMakeVisible (initButton);

        assignButton.setButtonText ("Assign Source +");
        assignButton.setTooltip ("Arm Velocity or Note source in Assign Mode");
        assignButton.onClick = [this] {
            const int sourceChoice = curveView.isNoteMode() ? 24 : 23;
            ctx.assign.tapSource (sourceChoice);
        };
        addAndMakeVisible (assignButton);

        addAndMakeVisible (curveView);
    }

    /** One bar: [Velocity][Note] .. [GAP][Init][Assign]; narrower than that Init / Assign drop to a second bar. */
    bool twoBars (int innerWidth) const
    {
        const auto& t = ctx.theme;
        return innerWidth < t.ipx (110.0f) + t.ipx (4.0f) + t.ipx (100.0f) + 4 * t.gap() + t.ipx (34.0f) + t.ipx (80.0f) + t.ipx (115.0f);
    }

    int preferredHeight (int width) const override
    {
        const int touch = ctx.theme.touch();
        const int gap = ctx.theme.gap();
        const int bars = twoBars (width - 4 * gap) ? 2 : 1;
        const int visualH = ctx.theme.ipx (160.0f);
        // the flow of the controls (and More) as laid out at this width: it needs more than one row on a narrow tile
        const int controlsH = const_cast<ModVelNotePanel*> (this)->layoutControls (juce::Rectangle<int> (0, 0, width - 2 * gap, 100000), true);
        return headerHeight() + bars * touch + (bars - 1) * gap + gap + visualH + gap + controlsH + gap;
    }

    void resized() override
    {
        auto area = contentArea();
        const int h = ctx.theme.touch();
        const int gap = ctx.theme.gap();

        auto topBar = area.removeFromTop (h);
        const bool two = twoBars (area.getWidth());
        juce::Rectangle<int> secondBar;
        if (two)
        {
            area.removeFromTop (gap);
            secondBar = area.removeFromTop (h);
        }
        veloTabButton.setBounds (topBar.removeFromLeft (juce::jmax (h, ctx.theme.ipx (110.0f))));
        topBar.removeFromLeft (ctx.theme.ipx (4.0f));
        noteTabButton.setBounds (topBar.removeFromLeft (juce::jmax (h, ctx.theme.ipx (100.0f))));
        topBar.removeFromLeft (gap);

        auto& right = two ? secondBar : topBar;
        gapBadge.setBounds (topBar.removeFromRight (ctx.theme.ipx (34.0f)));
        topBar.removeFromRight (gap);
        assignButton.setBounds (right.removeFromRight (juce::jmax (h, ctx.theme.ipx (115.0f))));
        right.removeFromRight (gap);
        initButton.setBounds (right.removeFromRight (juce::jmax (h, ctx.theme.ipx (80.0f))));

        area.removeFromTop (gap);
        const int visualH = ctx.theme.ipx (160.0f);
        curveView.setBounds (area.removeFromTop (visualH));

        area.removeFromTop (gap);
        layoutControls (area, false);
    }

private:
    juce::TextButton veloTabButton;
    juce::TextButton noteTabButton;
    juce::Label gapBadge;
    juce::TextButton initButton;
    juce::TextButton assignButton;

    ModResponseCurveView curveView;
};

SYNTHYBOY_REGISTER_PANEL (ModVelNotePanel, Page::mod, 30)
} // namespace synthyboy::ui
