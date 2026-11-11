// SynthyBoy - UI: EnvCurveView - Interactive ADSHR envelope curve display.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EnvCurveView.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::ui
{

namespace
{
inline float envPowerScale (float t, float power) noexcept
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    if (std::fabs (power) < 0.001f)
        return t;
    const float p = juce::jlimit (-25.0f, 25.0f, power);
    const float num = std::expm1 (p * t);
    const float den = std::expm1 (p);
    if (std::fabs (den) < 1.0e-7f)
        return t;
    const float val = num / den;
    return juce::jlimit (0.0f, 1.0f, val);
}
} // namespace

EnvCurveView::EnvCurveView (UiContext& context, int envIndex)
    : ctx (context),
      envIdx (juce::jlimit (1, 6, envIndex)),
      hDelay (context, "env" + std::to_string (envIdx) + ".delay"),
      hAttack (context, "env" + std::to_string (envIdx) + ".attack"),
      hHold (context, "env" + std::to_string (envIdx) + ".hold"),
      hDecay (context, "env" + std::to_string (envIdx) + ".decay"),
      hSustain (context, "env" + std::to_string (envIdx) + ".sustain"),
      hRelease (context, "env" + std::to_string (envIdx) + ".release"),
      hAttackCurve (context, "env" + std::to_string (envIdx) + ".attack_curve"),
      hDecayCurve (context, "env" + std::to_string (envIdx) + ".decay_curve"),
      hReleaseCurve (context, "env" + std::to_string (envIdx) + ".release_curve"),
      hUnits (context, "env" + std::to_string (envIdx) + ".units"),
      hDelayBeats (context, "env" + std::to_string (envIdx) + ".delay_beats"),
      hAttackBeats (context, "env" + std::to_string (envIdx) + ".attack_beats"),
      hHoldBeats (context, "env" + std::to_string (envIdx) + ".hold_beats"),
      hDecayBeats (context, "env" + std::to_string (envIdx) + ".decay_beats"),
      hReleaseBeats (context, "env" + std::to_string (envIdx) + ".release_beats")
{
    setOpaque (false);
    ctx.live->bind (*this, {}, [this] { if (ctx.live->changed()) repaint(); }); // WP-17: the playhead follows the heard voice while the view is on screen
}

bool EnvCurveView::livePlayhead (juce::Point<float>& out) const
{
    if (! ctx.live->available())
        return false;
    const auto& lv = ctx.live->values();
    const int n = envIdx - 1;
    const int stage = static_cast<int> (lv.envStage[n]);
    if (lv.activeVoices <= 0 || stage < 1 || stage > 4)
        return false;
    const float position = juce::jlimit (0.0f, 1.0f, lv.envPosition[n]);
    const float level = juce::jlimit (0.0f, 1.0f, lv.source[n]);
    const auto area = plotArea();
    const auto v = readCurrentValues();
    const float tHoldVisual = juce::jmax (40.0f, (v.delay + v.attack + v.hold + v.decay + v.release) * 0.12f);
    const float totalTime = juce::jmax (200.0f, v.delay + v.attack + v.hold + v.decay + tHoldVisual + v.release) / displayZoom();
    const float t1 = v.delay, t2 = t1 + v.attack, t3 = t2 + v.hold, t4 = t3 + v.decay, tSusHold = t4 + tHoldVisual;
    float time = 0.0f;
    switch (stage)
    {
        case 1: time = (level < 1.0e-4f && v.delay > 0.0f) ? position * t1 : t1 + position * v.attack; break;   // the delay and the attack are one state of the engine
        case 2: time = t2 + position * v.hold; break;
        case 3: time = t3 + position * v.decay; break;                                                          // at the end of the decay the voice sustains: the dot waits at the sustain node
        default: time = tSusHold + position * v.release; break;
    }
    out = { timeToX (time, totalTime, area), valToY (level, area) };
    return true;
}

EnvCurveView::~EnvCurveView()
{
    if (inGesture)
    {
        if (activeNode == HitNode::delay) hDelay.endGesture();
        else if (activeNode == HitNode::attack) hAttack.endGesture();
        else if (activeNode == HitNode::hold) hHold.endGesture();
        else if (activeNode == HitNode::decaySustain) { hDecay.endGesture(); hSustain.endGesture(); }
        else if (activeNode == HitNode::release) hRelease.endGesture();
        else if (activeNode == HitNode::attackCurve) hAttackCurve.endGesture();
        else if (activeNode == HitNode::decayCurve) hDecayCurve.endGesture();
        else if (activeNode == HitNode::releaseCurve) hReleaseCurve.endGesture();
    }
}

void EnvCurveView::setZoomLocked (bool locked)
{
    if (zoomLocked != locked)
    {
        zoomLocked = locked;
        if (locked)
            timeZoom = 1.0f; // locked = the graph fits the display again (design 05 section 2.1)
        repaint();
        if (onZoomChanged)
            onZoomChanged();
    }
}

void EnvCurveView::setZoom (float newZoom)
{
    if (zoomLocked)
        return;
    timeZoom = juce::jlimit (kMinTimeZoom, kMaxTimeZoom, newZoom);
    repaint();
    if (onZoomChanged)
        onZoomChanged();
}

void EnvCurveView::zoomBy (float factor)
{
    if (zoomLocked)
        return;
    timeZoom = juce::jlimit (kMinTimeZoom, kMaxTimeZoom, timeZoom * factor);
    repaint();
    if (onZoomChanged)
        onZoomChanged();
}

void EnvCurveView::mouseMagnify (const juce::MouseEvent&, float scaleFactor)
{
    zoomBy (scaleFactor); // pinch while the zoom lock is off (design 05 section 3)
}

void EnvCurveView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    zoomBy (std::pow (1.5f, wheel.deltaY * 2.0f)); // two-finger scroll / wheel = the same zoom on a desktop
}

EnvCurveView::EnvValues EnvCurveView::readCurrentValues() const
{
    EnvValues v;
    const bool isBeats = hUnits.valid() && (hUnits.plain() >= 0.5f);
    v.isBeats = isBeats;
    if (isBeats)
    {
        v.delayBeats = hDelayBeats.valid() ? hDelayBeats.plain() : 0.0f;
        v.attackBeats = hAttackBeats.valid() ? hAttackBeats.plain() : 0.0625f;
        v.holdBeats = hHoldBeats.valid() ? hHoldBeats.plain() : 0.0f;
        v.decayBeats = hDecayBeats.valid() ? hDecayBeats.plain() : 1.0f;
        v.releaseBeats = hReleaseBeats.valid() ? hReleaseBeats.plain() : 0.5f;

        // In beats mode, map 1 beat ~= 0.5s for coordinate display purposes
        v.delay = v.delayBeats * 500.0f;
        v.attack = v.attackBeats * 500.0f;
        v.hold = v.holdBeats * 500.0f;
        v.decay = v.decayBeats * 500.0f;
        v.release = v.releaseBeats * 500.0f;
    }
    else
    {
        v.delay = hDelay.valid() ? hDelay.plain() : 0.0f;
        v.attack = hAttack.valid() ? hAttack.plain() : 5.0f;
        v.hold = hHold.valid() ? hHold.plain() : 0.0f;
        v.decay = hDecay.valid() ? hDecay.plain() : 400.0f;
        v.release = hRelease.valid() ? hRelease.plain() : 200.0f;
    }
    v.sustain = hSustain.valid() ? (hSustain.plain() * 0.01f) : 1.0f; // sustain is 0..100%
    v.attackCurve = hAttackCurve.valid() ? hAttackCurve.plain() : 0.0f;
    v.decayCurve = hDecayCurve.valid() ? hDecayCurve.plain() : -3.0f;
    v.releaseCurve = hReleaseCurve.valid() ? hReleaseCurve.plain() : -3.0f;
    return v;
}

EnvCurveView::EnvValues EnvCurveView::readEnv1Values() const
{
    EnvValues v;
    auto& store = ctx.store();
    const auto getVal = [&] (const char* key, float def)
    {
        const int index = resolveKey (key).index;
        return store.contains (index) ? store.plain (index) : def;
    };
    v.delay = getVal ("env1.delay", 0.0f);
    v.attack = getVal ("env1.attack", 5.0f);
    v.hold = getVal ("env1.hold", 0.0f);
    v.decay = getVal ("env1.decay", 400.0f);
    v.sustain = getVal ("env1.sustain", 100.0f) * 0.01f;
    v.release = getVal ("env1.release", 200.0f);
    v.attackCurve = getVal ("env1.attack_curve", 0.0f);
    v.decayCurve = getVal ("env1.decay_curve", -3.0f);
    v.releaseCurve = getVal ("env1.release_curve", -3.0f);
    return v;
}

juce::Rectangle<float> EnvCurveView::plotArea() const
{
    const auto& t = ctx.theme;
    const float mX = t.px (14.0f);
    const float mTop = t.px (10.0f);
    const float mBottom = t.px (22.0f); // room for ruler
    return getLocalBounds().toFloat().reduced (mX, 0.0f).withTrimmedTop (mTop).withTrimmedBottom (mBottom);
}

float EnvCurveView::timeToX (float timeMs, float totalTimeMs, const juce::Rectangle<float>& area) const
{
    if (totalTimeMs <= 0.0f)
        return area.getX();
    const float frac = juce::jlimit (0.0f, 1.0f, timeMs / totalTimeMs);
    return area.getX() + frac * area.getWidth();
}

float EnvCurveView::xToTime (float x, float totalTimeMs, const juce::Rectangle<float>& area) const
{
    if (area.getWidth() <= 0.0f)
        return 0.0f;
    const float frac = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    return frac * totalTimeMs;
}

float EnvCurveView::valToY (float val, const juce::Rectangle<float>& area) const
{
    const float clamped = juce::jlimit (0.0f, 1.0f, val);
    return area.getBottom() - clamped * area.getHeight();
}

float EnvCurveView::yToVal (float y, const juce::Rectangle<float>& area) const
{
    if (area.getHeight() <= 0.0f)
        return 0.0f;
    const float frac = (area.getBottom() - y) / area.getHeight();
    return juce::jlimit (0.0f, 1.0f, frac);
}

juce::Path EnvCurveView::buildPath (const EnvValues& v, const juce::Rectangle<float>& area, float totalTime) const
{
    juce::Path p;
    const float t0 = 0.0f;
    const float t1 = v.delay;
    const float t2 = t1 + v.attack;
    const float t3 = t2 + v.hold;
    const float t4 = t3 + v.decay;
    const float tSustainHold = t4 + juce::jmax (totalTime * 0.12f, 40.0f); // display segment for sustain
    const float t5 = tSustainHold + v.release;

    const float x0 = timeToX (t0, totalTime, area);
    const float y0 = valToY (0.0f, area);
    p.startNewSubPath (x0, y0);

    // Delay segment
    const float x1 = timeToX (t1, totalTime, area);
    const float y1 = valToY (0.0f, area);
    p.lineTo (x1, y1);

    // Attack segment with curve
    const float x2 = timeToX (t2, totalTime, area);
    const float y2 = valToY (1.0f, area);
    constexpr int kSteps = 24;
    for (int i = 1; i <= kSteps; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (kSteps);
        const float curvedT = envPowerScale (t, v.attackCurve);
        const float curX = x1 + t * (x2 - x1);
        const float curY = y1 + curvedT * (y2 - y1);
        p.lineTo (curX, curY);
    }

    // Hold segment
    const float x3 = timeToX (t3, totalTime, area);
    const float y3 = valToY (1.0f, area);
    p.lineTo (x3, y3);

    // Decay segment with curve
    const float x4 = timeToX (t4, totalTime, area);
    const float y4 = valToY (v.sustain, area);
    for (int i = 1; i <= kSteps; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (kSteps);
        const float curvedT = envPowerScale (t, v.decayCurve);
        const float curX = x3 + t * (x4 - x3);
        const float curY = y3 + curvedT * (y4 - y3);
        p.lineTo (curX, curY);
    }

    // Sustain display hold
    const float xSusHold = timeToX (tSustainHold, totalTime, area);
    p.lineTo (xSusHold, y4);

    // Release segment with curve
    const float x5 = timeToX (t5, totalTime, area);
    const float y5 = valToY (0.0f, area);
    for (int i = 1; i <= kSteps; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (kSteps);
        const float curvedT = envPowerScale (t, v.releaseCurve);
        const float curX = xSusHold + t * (x5 - xSusHold);
        const float curY = y4 + curvedT * (y5 - y4);
        p.lineTo (curX, curY);
    }

    return p;
}

juce::Point<float> EnvCurveView::getNodePosition (HitNode node) const
{
    const auto area = plotArea();
    const auto v = readCurrentValues();
    const float tHoldVisual = juce::jmax (40.0f, (v.delay + v.attack + v.hold + v.decay + v.release) * 0.12f);
    const float totalTime = juce::jmax (200.0f, v.delay + v.attack + v.hold + v.decay + tHoldVisual + v.release) / displayZoom();

    const float t1 = v.delay;
    const float t2 = t1 + v.attack;
    const float t3 = t2 + v.hold;
    const float t4 = t3 + v.decay;
    const float tSusHold = t4 + tHoldVisual;
    const float t5 = tSusHold + v.release;

    switch (node)
    {
        case HitNode::delay:
            return { timeToX (t1, totalTime, area), valToY (0.0f, area) };
        case HitNode::attack:
            return { timeToX (t2, totalTime, area), valToY (1.0f, area) };
        case HitNode::hold:
            return { timeToX (t3, totalTime, area), valToY (1.0f, area) };
        case HitNode::decaySustain:
            return { timeToX (t4, totalTime, area), valToY (v.sustain, area) };
        case HitNode::release:
            return { timeToX (t5, totalTime, area), valToY (0.0f, area) };
        case HitNode::attackCurve:
        {
            const float midT = envPowerScale (0.5f, v.attackCurve);
            return { timeToX (t1 + 0.5f * v.attack, totalTime, area), valToY (midT, area) };
        }
        case HitNode::decayCurve:
        {
            const float midT = envPowerScale (0.5f, v.decayCurve);
            return { timeToX (t3 + 0.5f * v.decay, totalTime, area), valToY (1.0f + midT * (v.sustain - 1.0f), area) };
        }
        case HitNode::releaseCurve:
        {
            const float midT = envPowerScale (0.5f, v.releaseCurve);
            return { timeToX (tSusHold + 0.5f * v.release, totalTime, area), valToY (v.sustain * (1.0f - midT), area) };
        }
        case HitNode::none:
        default:
            return { -100.0f, -100.0f };
    }
}

EnvCurveView::HitNode EnvCurveView::hitTestNode (juce::Point<float> pos) const
{
    const float touchRadius = juce::jmax (22.0f, static_cast<float> (ctx.theme.touch()) * 0.5f);
    const HitNode candidates[] = {
        HitNode::decaySustain,
        HitNode::attack,
        HitNode::release,
        HitNode::delay,
        HitNode::hold,
        HitNode::attackCurve,
        HitNode::decayCurve,
        HitNode::releaseCurve
    };

    HitNode best = HitNode::none;
    float bestDistSq = touchRadius * touchRadius;
    for (auto node : candidates)
    {
        const auto p = getNodePosition (node);
        const float dx = pos.x - p.x;
        const float dy = pos.y - p.y;
        const float distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            best = node;
        }
    }
    return best;
}

void EnvCurveView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const auto b = getLocalBounds().toFloat();
    const auto area = plotArea();

    // Background
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (b, t.px (8.0f));
    g.setColour (c.outline.withAlpha (0.4f));
    g.drawRoundedRectangle (b, t.px (8.0f), 1.0f);

    const auto v = readCurrentValues();
    const float tHoldVisual = juce::jmax (40.0f, (v.delay + v.attack + v.hold + v.decay + v.release) * 0.12f);
    const float totalTime = juce::jmax (200.0f, v.delay + v.attack + v.hold + v.decay + tHoldVisual + v.release) / displayZoom();

    // Horizontal grid lines
    g.setColour (c.outline.withAlpha (0.15f));
    for (int i = 1; i <= 3; ++i)
    {
        const float y = area.getY() + static_cast<float> (i) * 0.25f * area.getHeight();
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }

    // Time ruler / ticks at bottom
    g.setColour (c.textDim);
    g.setFont (t.font (t.px (10.0f)));
    const int numTicks = 5;
    for (int i = 0; i < numTicks; ++i)
    {
        const float frac = static_cast<float> (i) / static_cast<float> (numTicks - 1);
        const float x = area.getX() + frac * area.getWidth();
        const float tickTime = frac * totalTime;
        g.drawVerticalLine (juce::roundToInt (x), area.getBottom(), area.getBottom() + t.px (4.0f));

        juce::String label;
        if (v.isBeats)
        {
            const float beats = tickTime / 500.0f;
            label = juce::String (beats, 1) + "b";
        }
        else
        {
            if (tickTime >= 1000.0f)
                label = juce::String (tickTime * 0.001f, 1) + "s";
            else
                label = juce::String (juce::roundToInt (tickTime)) + "ms";
        }
        g.drawFittedText (label, juce::roundToInt (x - 24.0f), juce::roundToInt (area.getBottom() + t.px (4.0f)), 48, 14,
                          juce::Justification::centred, 1);
    }

    // Faint ENV 1 curve behind when active envelope is 2..6
    if (envIdx > 1)
    {
        const auto env1 = readEnv1Values();
        const float totalTimeEnv1 = juce::jmax (200.0f, env1.delay + env1.attack + env1.hold + env1.decay + tHoldVisual + env1.release) / displayZoom();
        const auto pathEnv1 = buildPath (env1, area, totalTimeEnv1);
        g.setColour (c.textDim.withAlpha (0.28f));
        g.strokePath (pathEnv1, juce::PathStrokeType (t.px (1.5f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Active envelope curve
    const auto activePath = buildPath (v, area, totalTime);

    // Fill under curve
    juce::Path fillPath (activePath);
    fillPath.lineTo (area.getRight(), area.getBottom());
    fillPath.lineTo (area.getX(), area.getBottom());
    fillPath.closeSubPath();
    juce::ColourGradient grad (c.accent.withAlpha (0.22f), 0.0f, area.getY(),
                               c.accent.withAlpha (0.02f), 0.0f, area.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (fillPath);

    // Stroke
    g.setColour (c.accent);
    g.strokePath (activePath, juce::PathStrokeType (t.px (2.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Nodes (closed dots)
    const float nodeRadius = t.px (4.5f);
    const HitNode mainNodes[] = { HitNode::delay, HitNode::attack, HitNode::hold, HitNode::decaySustain, HitNode::release };
    for (auto node : mainNodes)
    {
        const auto p = getNodePosition (node);
        const bool isDrag = (activeNode == node);
        g.setColour (isDrag ? c.textOnAccent : c.panel);
        g.fillEllipse (p.x - nodeRadius - 1.0f, p.y - nodeRadius - 1.0f, (nodeRadius + 1.0f) * 2.0f, (nodeRadius + 1.0f) * 2.0f);
        g.setColour (isDrag ? c.accentDim : c.accent);
        g.fillEllipse (p.x - nodeRadius, p.y - nodeRadius, nodeRadius * 2.0f, nodeRadius * 2.0f);
        g.setColour (c.text);
        g.drawEllipse (p.x - nodeRadius, p.y - nodeRadius, nodeRadius * 2.0f, nodeRadius * 2.0f, 1.2f);
    }

    // Curve handles (open dots)
    const float curveRadius = t.px (3.5f);
    const HitNode curveNodes[] = { HitNode::attackCurve, HitNode::decayCurve, HitNode::releaseCurve };
    for (auto node : curveNodes)
    {
        const auto p = getNodePosition (node);
        const bool isDrag = (activeNode == node);
        g.setColour (isDrag ? c.accent : c.panelRaised);
        g.fillEllipse (p.x - curveRadius, p.y - curveRadius, curveRadius * 2.0f, curveRadius * 2.0f);
        g.setColour (isDrag ? c.textOnAccent : c.accent);
        g.drawEllipse (p.x - curveRadius, p.y - curveRadius, curveRadius * 2.0f, curveRadius * 2.0f, 1.5f);
    }

    // WP-17: the playhead of the voice that is heard
    if (juce::Point<float> head; livePlayhead (head))
    {
        g.setColour (c.text.withAlpha (0.7f));
        g.drawVerticalLine (juce::roundToInt (head.x), area.getY(), area.getBottom());
        const float r = t.px (5.0f);
        g.setColour (c.mod);
        g.fillEllipse (head.x - r, head.y - r, 2.0f * r, 2.0f * r);
        g.setColour (c.text);
        g.drawEllipse (head.x - r, head.y - r, 2.0f * r, 2.0f * r, 1.2f);
    }

    // Drag value bubble / tooltip
    if (inGesture && dragBubbleText.isNotEmpty())
    {
        g.setFont (t.font (t.px (12.0f), true));
        const int tw = juce::roundToInt (Theme::textWidth (g.getCurrentFont(), dragBubbleText)) + t.ipx (16.0f);
        const int th = t.ipx (22.0f);
        const juce::Rectangle<int> bubbleBounds (juce::roundToInt (area.getCentreX() - static_cast<float> (tw) * 0.5f),
                                                 juce::roundToInt (area.getY() + t.px (6.0f)), tw, th);
        g.setColour (c.panel.withAlpha (0.92f));
        g.fillRoundedRectangle (bubbleBounds.toFloat(), t.px (4.0f));
        g.setColour (c.accent);
        g.drawRoundedRectangle (bubbleBounds.toFloat(), t.px (4.0f), 1.0f);
        g.setColour (c.text);
        g.drawFittedText (dragBubbleText, bubbleBounds, juce::Justification::centred, 1);
    }
}

void EnvCurveView::resized()
{
}

void EnvCurveView::mouseDown (const juce::MouseEvent& e)
{
    activeNode = hitTestNode (e.position);
    if (activeNode == HitNode::none)
        return;

    inGesture = true;
    dragStartMouse = e.position;
    const auto v = readCurrentValues();

    switch (activeNode)
    {
        case HitNode::delay:
            activeKey = v.isBeats ? ("env" + std::to_string (envIdx) + ".delay_beats")
                                  : ("env" + std::to_string (envIdx) + ".delay");
            dragStartValX = v.isBeats ? v.delayBeats : v.delay;
            hDelay.beginGesture();
            break;
        case HitNode::attack:
            activeKey = v.isBeats ? ("env" + std::to_string (envIdx) + ".attack_beats")
                                  : ("env" + std::to_string (envIdx) + ".attack");
            dragStartValX = v.isBeats ? v.attackBeats : v.attack;
            hAttack.beginGesture();
            break;
        case HitNode::hold:
            activeKey = v.isBeats ? ("env" + std::to_string (envIdx) + ".hold_beats")
                                  : ("env" + std::to_string (envIdx) + ".hold");
            dragStartValX = v.isBeats ? v.holdBeats : v.hold;
            hHold.beginGesture();
            break;
        case HitNode::decaySustain:
            activeKey = v.isBeats ? ("env" + std::to_string (envIdx) + ".decay_beats")
                                  : ("env" + std::to_string (envIdx) + ".decay");
            dragStartValX = v.isBeats ? v.decayBeats : v.decay;
            dragStartValY = v.sustain * 100.0f;
            hDecay.beginGesture();
            hSustain.beginGesture();
            break;
        case HitNode::release:
            activeKey = v.isBeats ? ("env" + std::to_string (envIdx) + ".release_beats")
                                  : ("env" + std::to_string (envIdx) + ".release");
            dragStartValX = v.isBeats ? v.releaseBeats : v.release;
            hRelease.beginGesture();
            break;
        case HitNode::attackCurve:
            activeKey = "env" + std::to_string (envIdx) + ".attack_curve";
            dragStartValY = v.attackCurve;
            hAttackCurve.beginGesture();
            break;
        case HitNode::decayCurve:
            activeKey = "env" + std::to_string (envIdx) + ".decay_curve";
            dragStartValY = v.decayCurve;
            hDecayCurve.beginGesture();
            break;
        case HitNode::releaseCurve:
            activeKey = "env" + std::to_string (envIdx) + ".release_curve";
            dragStartValY = v.releaseCurve;
            hReleaseCurve.beginGesture();
            break;
        case HitNode::none:
        default:
            break;
    }
    repaint();
}

void EnvCurveView::mouseDrag (const juce::MouseEvent& e)
{
    if (! inGesture || activeNode == HitNode::none)
        return;

    const auto area = plotArea();
    const float dx = e.position.x - dragStartMouse.x;
    const float dy = dragStartMouse.y - e.position.y; // moving up increases value
    const auto v = readCurrentValues();

    if (activeNode == HitNode::delay)
    {
        if (v.isBeats)
        {
            const float scale = 8.0f / area.getWidth();
            const float newVal = juce::jlimit (0.0f, 8.0f, dragStartValX + dx * scale);
            hDelayBeats.setNormalised (toNormalised (hDelayBeats.info(), newVal));
            dragBubbleText = "Delay: " + juce::String (newVal, 2) + " beats";
        }
        else
        {
            const float scale = 4000.0f / area.getWidth();
            const float newVal = juce::jlimit (0.0f, 4000.0f, dragStartValX + dx * scale);
            hDelay.setNormalised (toNormalised (hDelay.info(), newVal));
            dragBubbleText = "Delay: " + juce::String (juce::roundToInt (newVal)) + " ms";
        }
    }
    else if (activeNode == HitNode::attack)
    {
        if (v.isBeats)
        {
            const float scale = 16.0f / area.getWidth();
            const float newVal = juce::jlimit (0.0f, 16.0f, dragStartValX + dx * scale);
            hAttackBeats.setNormalised (toNormalised (hAttackBeats.info(), newVal));
            dragBubbleText = "Attack: " + juce::String (newVal, 2) + " beats";
        }
        else
        {
            const float scale = 10000.0f / area.getWidth();
            const float newVal = juce::jlimit (0.0f, 10000.0f, dragStartValX + dx * scale);
            hAttack.setNormalised (toNormalised (hAttack.info(), newVal));
            dragBubbleText = "Attack: " + (newVal >= 1000.0f ? juce::String (newVal * 0.001f, 2) + " s"
                                                             : juce::String (juce::roundToInt (newVal)) + " ms");
        }
    }
    else if (activeNode == HitNode::hold)
    {
        if (v.isBeats)
        {
            const float scale = 8.0f / area.getWidth();
            const float newVal = juce::jlimit (0.0f, 8.0f, dragStartValX + dx * scale);
            hHoldBeats.setNormalised (toNormalised (hHoldBeats.info(), newVal));
            dragBubbleText = "Hold: " + juce::String (newVal, 2) + " beats";
        }
        else
        {
            const float scale = 4000.0f / area.getWidth();
            const float newVal = juce::jlimit (0.0f, 4000.0f, dragStartValX + dx * scale);
            hHold.setNormalised (toNormalised (hHold.info(), newVal));
            dragBubbleText = "Hold: " + juce::String (juce::roundToInt (newVal)) + " ms";
        }
    }
    else if (activeNode == HitNode::decaySustain)
    {
        // Horizontal: decay time
        if (v.isBeats)
        {
            const float scale = 16.0f / area.getWidth();
            const float newValX = juce::jlimit (0.0f, 16.0f, dragStartValX + dx * scale);
            hDecayBeats.setNormalised (toNormalised (hDecayBeats.info(), newValX));
        }
        else
        {
            const float scale = 10000.0f / area.getWidth();
            const float newValX = juce::jlimit (0.0f, 10000.0f, dragStartValX + dx * scale);
            hDecay.setNormalised (toNormalised (hDecay.info(), newValX));
        }
        // Vertical: sustain level (0..100%)
        const float scaleY = 100.0f / area.getHeight();
        const float newValY = juce::jlimit (0.0f, 100.0f, dragStartValY + dy * scaleY);
        hSustain.setNormalised (toNormalised (hSustain.info(), newValY));

        dragBubbleText = "Sustain: " + juce::String (juce::roundToInt (newValY)) + " %";
    }
    else if (activeNode == HitNode::release)
    {
        if (v.isBeats)
        {
            const float scale = 16.0f / area.getWidth();
            const float newVal = juce::jlimit (0.0f, 16.0f, dragStartValX + dx * scale);
            hReleaseBeats.setNormalised (toNormalised (hReleaseBeats.info(), newVal));
            dragBubbleText = "Release: " + juce::String (newVal, 2) + " beats";
        }
        else
        {
            const float scale = 10000.0f / area.getWidth();
            const float newVal = juce::jlimit (0.0f, 10000.0f, dragStartValX + dx * scale);
            hRelease.setNormalised (toNormalised (hRelease.info(), newVal));
            dragBubbleText = "Release: " + (newVal >= 1000.0f ? juce::String (newVal * 0.001f, 2) + " s"
                                                              : juce::String (juce::roundToInt (newVal)) + " ms");
        }
    }
    else if (activeNode == HitNode::attackCurve)
    {
        const float scaleY = 40.0f / area.getHeight();
        const float newVal = juce::jlimit (-20.0f, 20.0f, dragStartValY + dy * scaleY);
        hAttackCurve.setNormalised (toNormalised (hAttackCurve.info(), newVal));
        dragBubbleText = "Attack Curve: " + juce::String (newVal, 1);
    }
    else if (activeNode == HitNode::decayCurve)
    {
        const float scaleY = 40.0f / area.getHeight();
        const float newVal = juce::jlimit (-20.0f, 20.0f, dragStartValY + dy * scaleY);
        hDecayCurve.setNormalised (toNormalised (hDecayCurve.info(), newVal));
        dragBubbleText = "Decay Curve: " + juce::String (newVal, 1);
    }
    else if (activeNode == HitNode::releaseCurve)
    {
        const float scaleY = 40.0f / area.getHeight();
        const float newVal = juce::jlimit (-20.0f, 20.0f, dragStartValY + dy * scaleY);
        hReleaseCurve.setNormalised (toNormalised (hReleaseCurve.info(), newVal));
        dragBubbleText = "Release Curve: " + juce::String (newVal, 1);
    }
    repaint();
}

void EnvCurveView::mouseUp (const juce::MouseEvent&)
{
    if (inGesture)
    {
        switch (activeNode)
        {
            case HitNode::delay:        hDelay.endGesture(); break;
            case HitNode::attack:       hAttack.endGesture(); break;
            case HitNode::hold:         hHold.endGesture(); break;
            case HitNode::decaySustain: hDecay.endGesture(); hSustain.endGesture(); break;
            case HitNode::release:      hRelease.endGesture(); break;
            case HitNode::attackCurve:  hAttackCurve.endGesture(); break;
            case HitNode::decayCurve:   hDecayCurve.endGesture(); break;
            case HitNode::releaseCurve: hReleaseCurve.endGesture(); break;
            case HitNode::none:
            default: break;
        }
        inGesture = false;
        activeNode = HitNode::none;
        dragBubbleText = {};
        repaint();
    }
}

void EnvCurveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitTestNode (e.position);
    switch (hit)
    {
        case HitNode::delay:        hDelay.resetToDefault(); break;
        case HitNode::attack:       hAttack.resetToDefault(); break;
        case HitNode::hold:         hHold.resetToDefault(); break;
        case HitNode::decaySustain: hDecay.resetToDefault(); hSustain.resetToDefault(); break;
        case HitNode::release:      hRelease.resetToDefault(); break;
        case HitNode::attackCurve:  hAttackCurve.resetToDefault(); break;
        case HitNode::decayCurve:   hDecayCurve.resetToDefault(); break;
        case HitNode::releaseCurve: hReleaseCurve.resetToDefault(); break;
        case HitNode::none:
        default: break;
    }
    repaint();
}

} // namespace synthyboy::ui
