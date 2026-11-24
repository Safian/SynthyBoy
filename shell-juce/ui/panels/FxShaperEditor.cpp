// SynthyBoy - UI: editor of the X-Shaper transfer curves (see FxShaperEditor.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxShaperEditor.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::ui
{
namespace
{
constexpr float kPointRadius = 7.0f;      // points
constexpr float kHitRadius = 22.0f;       // points: a finger
constexpr float kHandleSize = 6.0f;
constexpr int kToolbarHeight = 44;        // the touch target
constexpr float kPlotHeight = 150.0f;
constexpr float kMargin = 22.0f;

shapes::ShapeData toShapeData (const std::vector<ContentPoint>& points)
{
    shapes::ShapeData d;
    d.numPoints = std::min<size_t> (points.size(), shapes::ShapeData::kMaxPoints);
    for (size_t i = 0; i < d.numPoints; ++i)
    {
        d.points[i].x = points[i].x;
        d.points[i].y = points[i].y;
        d.points[i].curve = points[i].power;
        d.points[i].smooth = false;
    }
    d.sanitize();
    return d;
}
} // namespace

ShaperCurveEditor::ShaperCurveEditor (UiContext& context) : ctx (context)
{
    setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
    const auto add = [this] (const juce::String& text, const juce::String& tip, std::function<void()> action)
    {
        auto b = std::make_unique<juce::TextButton> (text);
        b->setTooltip (tip);
        b->onClick = std::move (action);
        addAndMakeVisible (*b);
        buttons.push_back (std::move (b));
    };
    add ("A", "Waveshaper A (the DRIVE knob at 0 %)", [this] { setCurve (0); });
    add ("B", "Waveshaper B (the DRIVE knob at 100 %)", [this] { setCurve (1); });
    add ("Add", "Add a point in the largest gap", [this]
        {
            float bestGap = 0.0f, at = 0.5f;
            for (size_t i = 1; i < current.size(); ++i)
                if (current[i].x - current[i - 1].x > bestGap)
                {
                    bestGap = current[i].x - current[i - 1].x;
                    at = 0.5f * (current[i].x + current[i - 1].x);
                }
            addPointAt (at, evaluate (at));
        });
    add ("Del", "Delete the selected point", [this] { deletePoint (selected); });
    add ("Preset", "Starting shapes", [this] { showPresetMenu(); });
    add ("Reset", "Back to the straight line", [this] { reset(); });
    current = identityCurve();
    rebuildCache();
    reloadFromHost();
    setAsymmetric (false);
    startTimerHz (15);
}

ShaperCurveEditor::~ShaperCurveEditor()
{
    stopTimer();
    if (dirty)
        commitNow();
}

void ShaperCurveEditor::setCurve (int newWhich)
{
    newWhich = newWhich != 0 ? 1 : 0;
    if (newWhich == which)
        return;
    if (dirty)
        commitNow();
    which = newWhich;
    selected = -1;
    reloadFromHost();
}

void ShaperCurveEditor::setAsymmetric (bool isAsymmetric)
{
    asym = isAsymmetric; // (both waveshapers exist for both types: the drive morphs from A to B, Serum 2 manual p.169)
    repaint();
}

void ShaperCurveEditor::reloadFromHost()
{
    if (drag != Drag::none)
        return;
    const auto content = ctx.host.content();
    current = curveOrIdentity (which == 0 ? content.shaperA : content.shaperB);
    selected = std::min (selected, static_cast<int> (current.size()) - 1);
    dirty = false;
    rebuildCache();
    repaint();
}

void ShaperCurveEditor::rebuildCache()
{
    cache = toShapeData (current);
}

float ShaperCurveEditor::evaluate (float x) const
{
    return juce::jlimit (0.0f, 1.0f, cache.evaluate (juce::jlimit (0.0f, 1.0f, x)));
}

void ShaperCurveEditor::markDirty()
{
    dirty = true;
    rebuildCache();
    repaint();
}

void ShaperCurveEditor::commitNow()
{
    dirty = false;
    auto content = ctx.host.content();
    auto& target = which == 0 ? content.shaperA : content.shaperB;
    if (isIdentityCurve (current))
        target.clear();
    else
        target = current;
    ctx.host.setContent (content);
}

void ShaperCurveEditor::timerCallback()
{
    if (dirty)
        commitNow();
}

// ---- editing ------------------------------------------------------------------------------------------------------------------------
bool ShaperCurveEditor::addPointAt (float x, float y)
{
    int index = -1;
    if (! curveAddPoint (current, x, y, &index))
        return false;
    selected = index;
    markDirty();
    commitNow();
    return true;
}

bool ShaperCurveEditor::deletePoint (int index)
{
    if (! curveRemovePoint (current, index))
        return false;
    selected = -1;
    markDirty();
    commitNow();
    return true;
}

bool ShaperCurveEditor::movePoint (int index, float x, float y)
{
    if (! curveMovePoint (current, index, x, y))
        return false;
    selected = index;
    markDirty();
    return true;
}

bool ShaperCurveEditor::setSegmentPower (int segment, float power)
{
    if (! curveSetPower (current, segment, power))
        return false;
    markDirty();
    return true;
}

void ShaperCurveEditor::applyPreset (CurvePreset preset)
{
    current = curvePreset (preset);
    selected = -1;
    markDirty();
    commitNow();
}

void ShaperCurveEditor::reset()
{
    applyPreset (CurvePreset::identity);
}

void ShaperCurveEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int> (CurvePreset::kNumPresets); ++i)
        menu.addItem (i + 1, curvePresetName (static_cast<CurvePreset> (i)));
    menu.showMenuAsync (popupOptions (button ("Preset")),
                        [this] (int result)
                        {
                            if (result > 0)
                                applyPreset (static_cast<CurvePreset> (result - 1));
                        });
}

// ---- geometry -----------------------------------------------------------------------------------------------------------------------
int ShaperCurveEditor::preferredHeight() const
{
    return ctx.theme.ipx (static_cast<float> (kToolbarHeight)) + ctx.theme.gap() + ctx.theme.ipx (kPlotHeight);
}

juce::TextButton* ShaperCurveEditor::button (const juce::String& text)
{
    for (auto& b : buttons)
        if (b->getButtonText() == text)
            return b.get();
    return nullptr;
}

juce::Point<float> ShaperCurveEditor::toView (float x, float y) const
{
    return { plot.getX() + x * plot.getWidth(), plot.getBottom() - y * plot.getHeight() };
}

juce::Point<float> ShaperCurveEditor::fromView (juce::Point<float> p) const
{
    return { juce::jlimit (0.0f, 1.0f, (p.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth())),
             juce::jlimit (0.0f, 1.0f, 1.0f - (p.y - plot.getY()) / juce::jmax (1.0f, plot.getHeight())) };
}

juce::Point<float> ShaperCurveEditor::pointPosition (int index) const
{
    if (index < 0 || static_cast<size_t> (index) >= current.size())
        return {};
    return toView (current[static_cast<size_t> (index)].x, current[static_cast<size_t> (index)].y);
}

juce::Point<float> ShaperCurveEditor::handlePosition (int segment) const
{
    if (segment < 0 || static_cast<size_t> (segment) + 1 >= current.size())
        return {};
    const float x = 0.5f * (current[static_cast<size_t> (segment)].x + current[static_cast<size_t> (segment) + 1].x);
    return toView (x, evaluate (x));
}

int ShaperCurveEditor::hitPoint (juce::Point<float> p) const
{
    int best = -1;
    float bestD = ctx.theme.px (kHitRadius) * ctx.theme.px (kHitRadius);
    for (int i = 0; i < static_cast<int> (current.size()); ++i)
    {
        const float d = pointPosition (i).getDistanceSquaredFrom (p);
        if (d <= bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

int ShaperCurveEditor::hitHandle (juce::Point<float> p) const
{
    int best = -1;
    float bestD = ctx.theme.px (kHitRadius) * ctx.theme.px (kHitRadius);
    for (int i = 0; i + 1 < static_cast<int> (current.size()); ++i)
    {
        const float d = handlePosition (i).getDistanceSquaredFrom (p);
        if (d <= bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

float ShaperCurveEditor::powerForMiddle (int segment, float targetY) const
{
    // the value at the middle of the segment is monotonic in its power: bisection on -8 .. 8
    std::vector<ContentPoint> trial = current;
    const float x = 0.5f * (trial[static_cast<size_t> (segment)].x + trial[static_cast<size_t> (segment) + 1].x);
    float lo = -8.0f, hi = 8.0f;
    trial[static_cast<size_t> (segment)].power = lo;
    const float atLo = toShapeData (trial).evaluate (x);
    trial[static_cast<size_t> (segment)].power = hi;
    const float atHi = toShapeData (trial).evaluate (x);
    const bool rising = atHi > atLo;
    for (int i = 0; i < 24; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        trial[static_cast<size_t> (segment)].power = mid;
        const float v = toShapeData (trial).evaluate (x);
        if ((v < targetY) == rising)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

void ShaperCurveEditor::resized()
{
    auto area = getLocalBounds();
    auto row = area.removeFromTop (ctx.theme.ipx (static_cast<float> (kToolbarHeight)));
    const int n = static_cast<int> (buttons.size());
    for (int i = 0; i < n; ++i)
    {
        const int w = row.getWidth() / (n - i);
        buttons[static_cast<size_t> (i)]->setBounds (row.removeFromLeft (w).reduced (1, 0));
    }
    area.removeFromTop (ctx.theme.gap());
    plot = area.toFloat().reduced (ctx.theme.px (kMargin), ctx.theme.px (8.0f));
}

// ---- drawing ------------------------------------------------------------------------------------------------------------------------
void ShaperCurveEditor::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const auto frame = plot.expanded (t.px (kMargin), t.px (8.0f));
    g.setColour (t.colours.panelRaised);
    g.fillRoundedRectangle (frame, t.px (6.0f));
    g.setColour (t.colours.outline);
    g.drawRoundedRectangle (frame, t.px (6.0f), 1.0f);

    // grid and the straight line (no shaping)
    g.setColour (t.colours.outline.withAlpha (0.4f));
    for (int i = 0; i <= 4; ++i)
    {
        const float f = static_cast<float> (i) / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (plot.getBottom() - f * plot.getHeight()), plot.getX(), plot.getRight());
        g.drawVerticalLine (juce::roundToInt (plot.getX() + f * plot.getWidth()), plot.getY(), plot.getBottom());
    }
    g.setColour (t.colours.textDim.withAlpha (0.5f));
    g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

    g.setFont (t.font (t.px (10.0f)));
    g.setColour (t.colours.textDim);
    g.drawText ("in", juce::Rectangle<float> (plot.getRight() - t.px (24.0f), plot.getBottom() + t.px (1.0f), t.px (24.0f), t.px (11.0f)), juce::Justification::centredRight, false);
    g.drawText ("out", juce::Rectangle<float> (frame.getX() + t.px (2.0f), plot.getY() - t.px (6.0f), t.px (22.0f), t.px (11.0f)), juce::Justification::centredLeft, false);
    g.drawText (juce::String (which == 0 ? "A (drive 0 %)" : "B (drive 100 %)") + (asym ? ": -1 .. 1, centre = silence" : ": magnitude"),
                juce::Rectangle<float> (plot.getX() + t.px (6.0f), plot.getY() + t.px (2.0f), t.px (200.0f), t.px (12.0f)), juce::Justification::centredLeft, false);
    if (asym) // the signed graph: the middle of both axes is silence
    {
        g.setColour (t.colours.textDim.withAlpha (0.7f));
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
        g.drawVerticalLine (juce::roundToInt (plot.getCentreX()), plot.getY(), plot.getBottom());
    }

    // the curve, as the engine bakes it
    juce::Path path;
    const int steps = 128;
    for (int i = 0; i <= steps; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (steps);
        const auto p = toView (x, evaluate (x));
        if (i == 0)
            path.startNewSubPath (p);
        else
            path.lineTo (p);
    }
    const auto colour = which == 0 ? t.colours.accent : t.colours.mod;
    g.setColour (colour.withAlpha (0.35f));
    g.strokePath (path, juce::PathStrokeType (t.px (4.0f)));
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (t.px (2.0f)));

    // the bend handles and the points
    for (int i = 0; i + 1 < static_cast<int> (current.size()); ++i)
    {
        const auto h = handlePosition (i);
        const float s = t.px (kHandleSize);
        juce::Path diamond;
        diamond.addQuadrilateral (h.x, h.y - s, h.x + s, h.y, h.x, h.y + s, h.x - s, h.y);
        g.setColour (t.colours.panel);
        g.fillPath (diamond);
        g.setColour (current[static_cast<size_t> (i)].power != 0.0f ? t.colours.warn : t.colours.textDim);
        g.strokePath (diamond, juce::PathStrokeType (1.0f));
    }
    for (int i = 0; i < static_cast<int> (current.size()); ++i)
    {
        const auto p = pointPosition (i);
        const float r = t.px (kPointRadius);
        g.setColour (i == selected ? t.colours.warn : colour);
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        g.setColour (t.colours.text);
        g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 1.0f);
    }
}

// ---- gestures -----------------------------------------------------------------------------------------------------------------------
void ShaperCurveEditor::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    const int point = hitPoint (e.position);
    if (point >= 0)
    {
        selected = point;
        drag = Drag::point;
        dragIndex = point;
        repaint();
        return;
    }
    const int handle = hitHandle (e.position);
    if (handle >= 0)
    {
        drag = Drag::handle;
        dragIndex = handle;
        return;
    }
    selected = -1;
    repaint();
}

void ShaperCurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::point)
    {
        const auto p = fromView (e.position);
        movePoint (dragIndex, p.x, p.y);
    }
    else if (drag == Drag::handle)
    {
        const auto p = fromView (e.position);
        setSegmentPower (dragIndex, powerForMiddle (dragIndex, p.y));
    }
}

void ShaperCurveEditor::mouseUp (const juce::MouseEvent&)
{
    const bool wasDragging = drag != Drag::none;
    drag = Drag::none;
    if (wasDragging && dirty)
        commitNow();
}

void ShaperCurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int point = hitPoint (e.position);
    if (point >= 0)
    {
        deletePoint (point);
        return;
    }
    const int handle = hitHandle (e.position);
    if (handle >= 0)
    {
        setSegmentPower (handle, 0.0f);
        commitNow();
        return;
    }
    if (plot.expanded (ctx.theme.px (kMargin * 0.5f)).contains (e.position))
    {
        const auto p = fromView (e.position);
        addPointAt (p.x, p.y);
    }
}
} // namespace synthyboy::ui
