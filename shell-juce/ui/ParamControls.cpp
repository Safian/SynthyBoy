// SynthyBoy - UI: knob, slider, choice, toggle.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ParamControls.h"

#include <cmath>

#include "Sheet.h"

namespace synthyboy::ui
{
namespace
{
constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.25f;
constexpr float kRotaryEnd = juce::MathConstants<float>::pi * 2.75f;
} // namespace

// ---- ParamControl -----------------------------------------------------------------------------------------------------------------

ParamControl::ParamControl (UiContext& context, const std::string& key, const juce::String& labelOverride)
    : ctx (context), handle (context, key)
{
    caps = handle.caps();
    labelText = labelOverride.isNotEmpty() ? labelOverride : juce::String (handle.valid() ? shortName (handle.info()) : std::string ("?"));

    // not available in this engine: dimmed, still editable (the value is stored and saved), with the tooltip
    setAlpha (caps.available ? 1.0f : 0.4f);
    if (! caps.available)
        setTooltip (caps.tooltip);
    else if (handle.valid())
        setTooltip (juce::String (handle.info().name));

    handle.onChange = [this] { valueChanged(); };
    ctx.assign.addChangeListener (this);
}

ParamControl::~ParamControl()
{
    stopTimer();
    ctx.assign.removeChangeListener (this);
}

void ParamControl::setLabel (const juce::String& newLabel)
{
    labelText = newLabel;
    repaint();
}

void ParamControl::changeListenerCallback (juce::ChangeBroadcaster*)
{
    assignStateChanged();
}

bool ParamControl::isAssignTarget() const
{
    return handle.modulatable() && ctx.assign.flow().isTarget (handle.index());
}

void ParamControl::beginPress()
{
    consumedByMenu = false;
    startTimer (kLongPressMs);
}

void ParamControl::cancelLongPress()
{
    stopTimer();
}

void ParamControl::timerCallback()
{
    stopTimer();
    consumedByMenu = true;
    longPressed();
}

void ParamControl::showSheet()
{
    if (handle.valid())
        showParamSheet (ctx, handle.key(), *this);
}

void ParamControl::showValueBox()
{
    if (onValueBox)
        onValueBox();
    else if (handle.valid())
        showParamSheet (ctx, handle.key(), *this, true);
}

void ParamControl::paintUnknownKey (juce::Graphics& g) const
{
    g.setColour (ctx.theme.colours.warn);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), ctx.theme.px (6.0f), 1.0f);
    g.setFont (ctx.theme.labelFont());
    g.drawFittedText ("? " + juce::String (handle.key()), getLocalBounds().reduced (2), juce::Justification::centred, 2);
}

// ---- ParamDragControl -------------------------------------------------------------------------------------------------------------

ParamDragControl::~ParamDragControl()
{
    ctx.live->remove (this);
    hideBubble();
}

/** WP-17: a control that is modulated and on screen asks the engine for its modulated value and listens to the live bus. */
void ParamDragControl::updateLiveSubscription()
{
    const bool want = handle.valid() && handle.modulatable() && LiveBus::shown (*this) && ! ctx.assign.snapshot().entriesFor (handle.index()).empty();
    if (want == liveSubscribed)
        return;
    liveSubscribed = want;
    if (want)
    {
        ctx.live->subscribe (this, { handle.index() });
        ctx.live->listen (this, [this] { liveTick(); });
    }
    else
    {
        ctx.live->remove (this);
        liveNormalised = -1.0f;
        updateRing();
    }
}

void ParamDragControl::liveTick()
{
    if (! liveSubscribed || ! handle.valid())
        return;
    const float live = ctx.live->hasLive (handle.index()) ? toNormalised (handle.info(), ctx.live->liveOf (handle.index(), handle.plain())) : -1.0f;
    if (std::fabs (live - liveNormalised) > 0.002f)
    {
        liveNormalised = live;
        updateRing();
    }
}

void ParamDragControl::visibilityChanged()
{
    updateLiveSubscription();
}

void ParamDragControl::parentHierarchyChanged()
{
    updateLiveSubscription();
}

void ParamDragControl::initRing()
{
    ring = std::make_unique<ModRing> (ctx.theme, ringShape());
    addAndMakeVisible (*ring);
    updateRing();
}

void ParamDragControl::resized()
{
    if (ring != nullptr)
        ring->setBounds (ringBounds());
}

float ParamDragControl::pixelsForFullRange() const
{
    return kDragPixelsForFullRange * juce::jmax (1.0f, ctx.theme.scale());
}

void ParamDragControl::valueChanged()
{
    updateRing();
    if (bubble != nullptr && bubble->getParentComponent() != nullptr)
        showBubble (zone);
    repaint();
}

void ParamDragControl::assignStateChanged()
{
    updateLiveSubscription();
    updateRing();
    repaint();
}

void ParamDragControl::updateRing()
{
    if (ring == nullptr || ! handle.valid())
        return;
    ModRing::State s;
    s.entries = ctx.assign.snapshot().entriesFor (handle.index());
    s.position = handle.normalised();
    s.target = isAssignTarget();
    s.armedSource = ctx.assign.flow().armedSource();
    s.activeSlot = ctx.assign.flow().activeSlot();
    s.midiCc = ctx.host.midiCcForKey (handle.key());
    s.learning = ctx.host.midiLearnKey() == handle.key();
    s.live = liveSubscribed ? liveNormalised : -1.0f;
    ring->setState (std::move (s));
}

void ParamDragControl::showBubble (FineZone z)
{
    zone = z;
    if (! ctx.host.preferences().valueBubbles) // "Param value tooltips" off (S p.313)
        return;
    auto* top = getTopLevelComponent();
    if (top == nullptr || top == this || ! handle.valid())
        return;
    if (bubble == nullptr)
        bubble = std::make_unique<ValueBubble> (ctx.theme);

    juce::String text;
    if (assignDrag)
    {
        const ParamInfo& amount = paramTable()[modSlotParams (ctx.assign.flow().activeSlot()).amount];
        text = juce::String (displayText (amount, ctx.assign.matrix().get (modSlotParams (ctx.assign.flow().activeSlot()).amount)));
    }
    else
        text = juce::String (valueText (ctx, handle.info(), handle.plain()));
    bubble->setContent (text, z);

    if (bubble->getParentComponent() != top)
        top->addChildComponent (*bubble);
    const auto size = bubble->idealSize();
    const auto area = top->getLocalArea (this, getLocalBounds());
    int y = area.getY() - size.getHeight() - ctx.theme.gap();
    if (y < 0)
        y = area.getBottom() + ctx.theme.gap();
    bubble->setBounds (juce::Rectangle<int> (area.getCentreX() - size.getWidth() / 2, y, size.getWidth(), size.getHeight())
                           .constrainedWithin (top->getLocalBounds()));
    bubble->setVisible (true);
    bubble->toFront (false);
}

void ParamDragControl::hideBubble()
{
    if (bubble != nullptr)
    {
        if (auto* parent = bubble->getParentComponent())
            parent->removeChildComponent (bubble.get());
        bubble->setVisible (false);
    }
}

juce::Rectangle<int> ParamDragControl::modulatorBadgeBounds() const
{
    if (ring == nullptr || modulatorCount() == 0)
        return {};
    // the badge is drawn at the top left of the ring; the hit area is a little larger than the drawn dot
    const int size = ctx.theme.ipx (26.0f);
    return juce::Rectangle<int> (ring->getX(), ring->getY(), size, size).getIntersection (getLocalBounds());
}

int ParamDragControl::modulatorCount() const
{
    return handle.valid() ? static_cast<int> (ctx.assign.snapshot().entriesFor (handle.index()).size()) : 0;
}

bool ParamDragControl::selectModulator (int slot)
{
    if (! handle.valid())
        return false;
    for (const auto& e : ctx.assign.snapshot().entriesFor (handle.index()))
        if (e.slot == slot)
        {
            if (ctx.assign.flow().armedSource() != e.source)
                ctx.assign.tapSource (e.source);
            return ctx.assign.flow().armedSource() == e.source;
        }
    return false;
}

void ParamDragControl::showModulatorList()
{
    const auto entries = ctx.assign.snapshot().entriesFor (handle.index());
    if (entries.empty())
        return;
    badgeMenuShown = true;
    juce::PopupMenu menu;
    menu.addSectionHeader ("Modulators of " + juce::String (handle.info().name));
    for (const auto& e : entries)
        menu.addItem (e.slot + 1, juce::String (modSourceTable()[e.source - 1].name) + "   " + juce::String (juce::roundToInt (e.amount)) + " %");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<ParamDragControl> (this)] (int result)
                        {
                            if (result > 0 && safe != nullptr)
                                safe->selectModulator (result - 1);
                        });
}

void ParamDragControl::mouseDown (const juce::MouseEvent& e)
{
    if (! handle.valid())
        return;
    // Appendix G: Cmd-click (Ctrl-click on Windows) resets the control to its default; with the keyboard shortcuts preference off it is an ordinary press
    if (ctx.host.preferences().keyboardShortcuts && (e.mods.isCommandDown() || (e.mods.isCtrlDown() && ! e.mods.isPopupMenu())) && ctx.assign.flow().phase() == AssignFlow::Phase::idle)
    {
        consumedByMenu = true;
        pressed = false;
        handle.resetToDefault();
        return;
    }
    // Appendix G: Shift-Alt-click on a modulated control switches the modulation between directional and bi-directional
    if (ctx.host.preferences().keyboardShortcuts && e.mods.isShiftDown() && e.mods.isAltDown() && ! e.mods.isPopupMenu() && modulatorCount() > 0
        && ctx.assign.flow().phase() == AssignFlow::Phase::idle)
    {
        consumedByMenu = true;
        pressed = false;
        toggleModulationPolarity();
        return;
    }
    if (e.mods.isPopupMenu())
    {
        consumedByMenu = true;
        showSheet();
        return;
    }
    badgePress = ctx.assign.flow().phase() == AssignFlow::Phase::idle && modulatorBadgeBounds().contains (e.getPosition());
    if (badgePress)
        return;

    pressed = true;
    dragging = false;
    assignDrag = false;
    zone = FineZone::normal;
    downPos = lastPos = e.position;
    beginPress();
}

void ParamDragControl::mouseDrag (const juce::MouseEvent& e)
{
    if (badgePress)
        return;
    if (! pressed || ! handle.valid())
        return;

    if (! dragging)
    {
        if ((e.position - downPos).getDistanceFromOrigin() <= kLongPressSlopPx)
            return;
        dragging = true;
        cancelLongPress();

        // armed source + modulatable control: this drag sets the modulation depth instead of the value (06-modulation.md 2.1)
        auto& flow = ctx.assign.flow();
        flow.setLimitToHeadroom (ctx.host.preferences().limitModDepth); // "Limit Mod depth on drop" (S p.314)
        if (flow.phase() == AssignFlow::Phase::armed && handle.modulatable() && flow.isTarget (handle.index()))
        {
            const auto result = flow.beginDrag (handle.index());
            assignDrag = result == AssignFlow::BeginResult::ok;
            if (result == AssignFlow::BeginResult::matrixFull)
                ctx.assign.setMessage ("Mod Matrix full (64/64 slots used)");
            ctx.assign.sendChangeMessage();
        }
        if (! assignDrag)
            handle.beginGesture();
    }

    const float delta = isHorizontal() ? e.position.x - lastPos.x : lastPos.y - e.position.y;
    const float sideways = isHorizontal() ? e.position.y - downPos.y : e.position.x - downPos.x;
    lastPos = e.position;
    zone = zoneForOffset (sideways, ctx.theme.px (kFineZoneWidthPx));
    // Appendix G: Shift-drag fine tunes (the same step as the fine zone at the side)
    if (zone == FineZone::normal && e.mods.isShiftDown() && ctx.host.preferences().keyboardShortcuts)
        zone = FineZone::fine;

    if (assignDrag)
    {
        ctx.assign.flow().dragBy (delta, zone);
        updateRing();
    }
    else
        handle.setNormalised (applyDragDelta (handle.normalised(), delta, zone, pixelsForFullRange()));

    if (assignDrag || handle.inGesture())
        showBubble (zone);
}

void ParamDragControl::mouseUp (const juce::MouseEvent& e)
{
    cancelLongPress();
    hideBubble();
    if (badgePress)
    {
        badgePress = false;
        if (modulatorBadgeBounds().contains (e.getPosition()))
            showModulatorList();
        return;
    }
    if (! pressed)
        return;
    pressed = false;

    if (assignDrag)
    {
        assignDrag = false;
        dragging = false;
        ctx.assign.flow().endDrag();
        ctx.assign.refresh();
        ctx.assign.sendChangeMessage();
        return;
    }
    if (dragging)
    {
        dragging = false;
        handle.endGesture();
        return;
    }
    if (consumedByMenu)
        return;

    // a tap: the second one within the double-tap window resets to the default (decision Q3)
    const auto now = juce::Time::getMillisecondCounter();
    if (isDoubleTap (static_cast<int> (now - lastTapMs), e.position.getDistanceFrom (lastTapPos)))
    {
        // "Double-click params" (S p.313): reset to the default, or open the value box
        if (ctx.host.preferences().doubleClickResets)
            handle.resetToDefault();
        else
            showValueBox();
        lastTapMs = 0;
    }
    else
    {
        lastTapMs = now;
        lastTapPos = e.position;
    }
}

bool ParamDragControl::toggleModulationPolarity()
{
    const auto& entries = ctx.assign.snapshot().entriesFor (handle.index());
    if (entries.empty())
        return false;
    const float wanted = entries.front().bipolar ? 0.0f : 1.0f;
    auto& matrix = ctx.assign.matrix();
    for (const auto& entry : entries)
    {
        const int param = modSlotParams (entry.slot).bipolar;
        matrix.beginGesture (param);
        matrix.set (param, wanted);
        matrix.endGesture (param);
    }
    ctx.assign.refresh();
    ctx.assign.sendChangeMessage();
    return true;
}

void ParamDragControl::longPressed()
{
    pressed = false;
    dragging = false;
    showSheet();
}

void ParamDragControl::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! ctx.host.preferences().mouseWheel) // "Mouse wheel param control" off (S p.314): the wheel scrolls the page
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }
    if (handle.valid() && wheel.deltaY != 0.0f)
        nudgeNormalised (wheel.deltaY * 0.05f);
}

void ParamDragControl::nudgeNormalised (float delta)
{
    if (! handle.valid())
        return;
    handle.beginGesture();
    handle.setNormalised (juce::jlimit (0.0f, 1.0f, handle.normalised() + delta));
    handle.endGesture();
}

// ---- ParamKnob --------------------------------------------------------------------------------------------------------------------

ParamKnob::ParamKnob (UiContext& context, const std::string& key, const juce::String& labelOverride)
    : ParamDragControl (context, key, labelOverride)
{
    initRing();
}

void ParamKnob::setCompact (bool shouldBeCompact)
{
    compact = shouldBeCompact;
    resized();
    repaint();
}

juce::Point<int> ParamKnob::preferredSize() const
{
    const auto& t = ctx.theme;
    if (compact)
        return { t.touch() + t.ipx (4.0f), t.touch() + t.ipx (14.0f) };
    return { juce::jmax (t.touch(), t.ipx (72.0f)), t.ipx (16.0f) + t.knobDiameter() + t.ipx (20.0f) };
}

juce::Rectangle<float> ParamKnob::dialBounds() const
{
    const auto& t = ctx.theme;
    if (compact)
    {
        const float cd = static_cast<float> (juce::jmax (t.touch(), juce::jmin (getWidth(), getHeight() - t.ipx (14.0f))));
        return { (static_cast<float> (getWidth()) - cd) * 0.5f, 0.0f, cd, cd };
    }
    const int labelH = t.ipx (16.0f), valueH = t.ipx (20.0f);
    const float d = static_cast<float> (juce::jmax (t.touch(), juce::jmin (getWidth() - t.ipx (8.0f), getHeight() - labelH - valueH)));
    return { (static_cast<float> (getWidth()) - d) * 0.5f, static_cast<float> (labelH), d, d };
}

void ParamKnob::paint (juce::Graphics& g)
{
    if (! handle.valid())
    {
        paintUnknownKey (g);
        return;
    }

    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const ParamInfo& info = handle.info();

    g.setColour (c.textDim);
    g.setFont (t.labelFont());
    if (compact)
        g.drawFittedText (labelText, 0, getHeight() - t.ipx (14.0f), getWidth(), t.ipx (14.0f), juce::Justification::centred, 1);
    else
        g.drawFittedText (labelText, 0, 0, getWidth(), t.ipx (16.0f), juce::Justification::centred, 1);

    const auto dial = dialBounds();
    const float stroke = t.px (4.0f);
    const auto arcArea = dial.reduced (stroke * 1.5f);
    const auto centre = arcArea.getCentre();
    const float radius = arcArea.getWidth() * 0.5f;
    const float norm = handle.normalised();
    const bool centred = info.scale != Scale::stepped && info.min < 0.0f && info.max > 0.0f;
    const float zeroNorm = centred ? toNormalised (info, 0.0f) : 0.0f;

    g.setColour (c.panelRaised);
    g.fillEllipse (arcArea.reduced (stroke * 1.2f));

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, kRotaryStart, kRotaryEnd, true);
    g.setColour (c.track);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float a0 = kRotaryStart + zeroNorm * (kRotaryEnd - kRotaryStart);
    const float a1 = kRotaryStart + norm * (kRotaryEnd - kRotaryStart);
    if (std::fabs (a1 - a0) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        g.setColour (c.accent);
        g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // pointer
    const float inner = radius * 0.35f, outer = radius * 0.8f;
    juce::Line<float> pointer (centre.x + inner * std::sin (a1), centre.y - inner * std::cos (a1),
                               centre.x + outer * std::sin (a1), centre.y - outer * std::cos (a1));
    g.setColour (c.text);
    g.drawLine (pointer, t.px (2.5f));

    if (! compact)
    {
        g.setColour (c.text);
        g.setFont (t.valueFont());
        g.drawFittedText (juce::String (valueText (ctx, info, handle.plain())), 0, getHeight() - t.ipx (20.0f), getWidth(), t.ipx (20.0f),
                          juce::Justification::centred, 1);
    }
}

// ---- ParamSlider ------------------------------------------------------------------------------------------------------------------

ParamSlider::ParamSlider (UiContext& context, const std::string& key, const juce::String& labelOverride)
    : ParamDragControl (context, key, labelOverride)
{
    initRing();
}

juce::Point<int> ParamSlider::preferredSize() const
{
    return { ctx.theme.ipx (200.0f), ctx.theme.ipx (16.0f) + ctx.theme.touch() };
}

juce::Rectangle<float> ParamSlider::trackBounds() const
{
    const auto& t = ctx.theme;
    const float pad = t.px (12.0f);
    const float y = static_cast<float> (getHeight()) - static_cast<float> (t.touch()) * 0.5f;
    return { pad, y - t.px (3.0f), static_cast<float> (getWidth()) - 2.0f * pad, t.px (6.0f) };
}

juce::Rectangle<int> ParamSlider::ringBounds() const
{
    const auto tb = trackBounds();
    return juce::Rectangle<float> (tb.getX(), tb.getBottom() + ctx.theme.px (2.0f), tb.getWidth(), ctx.theme.px (8.0f)).toNearestInt();
}

void ParamSlider::paint (juce::Graphics& g)
{
    if (! handle.valid())
    {
        paintUnknownKey (g);
        return;
    }
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const ParamInfo& info = handle.info();

    g.setFont (t.labelFont());
    g.setColour (c.textDim);
    g.drawFittedText (labelText, 0, 0, getWidth() / 2, t.ipx (16.0f), juce::Justification::centredLeft, 1);
    g.setColour (c.text);
    g.setFont (t.valueFont());
    g.drawFittedText (juce::String (valueText (ctx, info, handle.plain())), getWidth() / 2, 0, getWidth() / 2, t.ipx (16.0f),
                      juce::Justification::centredRight, 1);

    const auto tb = trackBounds();
    const float norm = handle.normalised();
    const bool centred = info.scale != Scale::stepped && info.min < 0.0f && info.max > 0.0f;
    const float zeroNorm = centred ? toNormalised (info, 0.0f) : 0.0f;
    g.setColour (c.track);
    g.fillRoundedRectangle (tb, tb.getHeight() * 0.5f);
    const float x0 = tb.getX() + juce::jmin (zeroNorm, norm) * tb.getWidth();
    const float x1 = tb.getX() + juce::jmax (zeroNorm, norm) * tb.getWidth();
    g.setColour (c.accent);
    g.fillRoundedRectangle (x0, tb.getY(), x1 - x0, tb.getHeight(), tb.getHeight() * 0.5f);
    const float thumb = t.px (22.0f);
    g.setColour (c.text);
    g.fillEllipse (tb.getX() + norm * tb.getWidth() - thumb * 0.5f, tb.getCentreY() - thumb * 0.5f, thumb, thumb);
}

// ---- ParamChoice ------------------------------------------------------------------------------------------------------------------

ParamChoice::ParamChoice (UiContext& context, const std::string& key, const juce::String& labelOverride)
    : ParamControl (context, key, labelOverride)
{
}

juce::Point<int> ParamChoice::preferredSize() const
{
    return { ctx.theme.ipx (132.0f), ctx.theme.ipx (16.0f) + ctx.theme.touch() };
}

juce::Rectangle<int> ParamChoice::arrowArea (bool left) const
{
    const int h = ctx.theme.touch();
    auto pill = getLocalBounds().withTop (getHeight() - h);
    const int w = juce::jmax (h * 3 / 4, ctx.theme.ipx (32.0f));
    return left ? pill.removeFromLeft (w) : pill.removeFromRight (w);
}

void ParamChoice::paint (juce::Graphics& g)
{
    if (! handle.valid())
    {
        paintUnknownKey (g);
        return;
    }
    const auto& t = ctx.theme;
    const Palette& c = t.colours;

    g.setFont (t.labelFont());
    g.setColour (c.textDim);
    g.drawFittedText (labelText, 0, 0, getWidth(), t.ipx (16.0f), juce::Justification::centredLeft, 1);

    const auto pill = getLocalBounds().withTop (getHeight() - t.touch()).toFloat().reduced (0.5f);
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (pill, t.px (8.0f));
    g.setColour (isAssignTarget() ? c.mod.withAlpha (0.5f) : c.outline);
    g.drawRoundedRectangle (pill, t.px (8.0f), 1.0f);

    g.setColour (c.textDim);
    g.setFont (t.titleFont());
    g.drawText ("<", arrowArea (true), juce::Justification::centred, false);
    g.drawText (">", arrowArea (false), juce::Justification::centred, false);

    g.setColour (c.text);
    g.setFont (t.valueFont());
    g.drawFittedText (juce::String (valueText (ctx, handle.info(), handle.plain())),
                      pill.toNearestInt().withTrimmedLeft (arrowArea (true).getWidth()).withTrimmedRight (arrowArea (false).getWidth()),
                      juce::Justification::centred, 1);
}

void ParamChoice::mouseDown (const juce::MouseEvent& e)
{
    if (! handle.valid())
        return;
    if (e.mods.isPopupMenu())
    {
        consumedByMenu = true;
        showSheet();
        return;
    }
    pressed = true;
    downPos = e.position;
    beginPress();
}

void ParamChoice::mouseUp (const juce::MouseEvent& e)
{
    cancelLongPress();
    if (! pressed)
        return;
    pressed = false;
    if (consumedByMenu || (e.position - downPos).getDistanceFromOrigin() > kLongPressSlopPx || ! handle.valid())
        return;

    const auto p = e.getPosition();
    if (arrowArea (true).contains (p))
        step (-1);
    else if (arrowArea (false).contains (p))
        step (+1);
    else
        showOptions();
}

void ParamChoice::step (int direction)
{
    if (! handle.valid() || direction == 0)
        return;
    const ParamInfo& info = handle.info();
    const int count = static_cast<int> (choiceCount (info));
    const int current = static_cast<int> (std::lround (handle.plain()));
    for (int i = current + direction; i >= 0 && i < count; i += direction)
        if (entryAllowed (i) && capStateForValue (handle.infoIndex(), static_cast<float> (i), ctx.host.engineCaps()).available)
        {
            handle.setPlainComplete (static_cast<float> (i));
            return;
        }
}

juce::PopupMenu ParamChoice::buildOptionMenu() const
{
    juce::PopupMenu menu;
    if (! handle.valid())
        return menu;
    const ParamInfo& info = handle.info();
    const int count = static_cast<int> (choiceCount (info));
    const int current = static_cast<int> (std::lround (handle.plain()));
    const auto item = [&] (int i)
    {
        const bool ok = entryAllowed (i) && capStateForValue (handle.infoIndex(), static_cast<float> (i), ctx.host.engineCaps()).available;
        return juce::PopupMenu::Item (juce::String (choiceName (info, static_cast<size_t> (i)))).setID (i + 1).setEnabled (ok).setTicked (i == current);
    };

    if (groups.empty())
    {
        for (int i = 0; i < count; ++i)
            menu.addItem (item (i));
        if (extraAction)
        {
            menu.addSeparator();
            menu.addItem (juce::PopupMenu::Item (extraLabel).setID (kExtraActionId));
        }
        return menu;
    }
    // tap a group, tap a mode: a group with a single mode is that mode itself; a gap group is greyed
    for (const auto& group : groups)
    {
        if (group.gap || group.items.empty())
        {
            menu.addItem (juce::PopupMenu::Item (juce::String (group.name) + " (not available)").setEnabled (false));
            continue;
        }
        juce::PopupMenu sub;
        bool hasCurrent = false;
        for (const int i : group.items)
            if (i >= 0 && i < count)
            {
                sub.addItem (item (i));
                hasCurrent = hasCurrent || i == current;
            }
        menu.addSubMenu (group.name, sub, true, nullptr, hasCurrent);
    }
    if (extraAction)
    {
        menu.addSeparator();
        menu.addItem (juce::PopupMenu::Item (extraLabel).setID (kExtraActionId));
    }
    return menu;
}

void ParamChoice::showOptions()
{
    if (! handle.valid())
        return;
    auto menu = buildOptionMenu();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe = juce::Component::SafePointer<ParamChoice> (this)] (int result)
                        {
                            if (result == kExtraActionId && safe != nullptr)
                                safe->runExtraAction();
                            else if (result > 0 && safe != nullptr)
                                safe->handle.setPlainComplete (static_cast<float> (result - 1));
                        });
}

void ParamChoice::setExtraAction (juce::String label, std::function<void()> action)
{
    extraLabel = std::move (label);
    extraAction = std::move (action);
}

// ---- ParamToggle ------------------------------------------------------------------------------------------------------------------

ParamToggle::ParamToggle (UiContext& context, const std::string& key, const juce::String& labelOverride)
    : ParamControl (context, key, labelOverride)
{
}

juce::Point<int> ParamToggle::preferredSize() const
{
    return { juce::jmax (ctx.theme.touch() * 2, ctx.theme.ipx (96.0f)), ctx.theme.touch() };
}

void ParamToggle::paint (juce::Graphics& g)
{
    if (! handle.valid())
    {
        paintUnknownKey (g);
        return;
    }
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const bool on = isOn();
    const auto pill = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (on ? c.accent : c.panelRaised);
    g.fillRoundedRectangle (pill, t.px (8.0f));
    g.setColour (isAssignTarget() ? c.mod.withAlpha (0.5f) : c.outline);
    g.drawRoundedRectangle (pill, t.px (8.0f), 1.0f);
    g.setColour (on ? c.textOnAccent : c.text);
    g.setFont (t.valueFont());
    g.drawFittedText (labelText, getLocalBounds().reduced (t.ipx (6.0f), 0), juce::Justification::centred, 1);
}

void ParamToggle::mouseDown (const juce::MouseEvent& e)
{
    if (! handle.valid())
        return;
    if (e.mods.isPopupMenu())
    {
        consumedByMenu = true;
        showSheet();
        return;
    }
    pressed = true;
    beginPress();
}

void ParamToggle::mouseUp (const juce::MouseEvent& e)
{
    cancelLongPress();
    if (! pressed)
        return;
    pressed = false;
    if (! consumedByMenu && getLocalBounds().contains (e.getPosition()))
        toggle();
}

void ParamToggle::toggle()
{
    if (handle.valid())
        handle.setPlainComplete (isOn() ? 0.0f : 1.0f);
}

std::unique_ptr<ParamControl> makeControl (UiContext& context, const std::string& key, const juce::String& labelOverride)
{
    const KeyRef ref = context.resolve (key);
    if (ref.valid())
        switch (defaultKind (*ref.info))
        {
            case ControlKind::toggle: return std::make_unique<ParamToggle> (context, key, labelOverride);
            case ControlKind::choice: return std::make_unique<ParamChoice> (context, key, labelOverride);
            case ControlKind::slider: return std::make_unique<ParamSlider> (context, key, labelOverride);
            case ControlKind::knob:   break;
        }
    return std::make_unique<ParamKnob> (context, key, labelOverride);
}
} // namespace synthyboy::ui
