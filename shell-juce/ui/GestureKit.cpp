// SynthyBoy - UI: gesture helpers (see GestureKit.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GestureKit.h"

#include "ModuleSheet.h"

namespace synthyboy::ui
{
// ---- Loupe --------------------------------------------------------------------------------------------------------------------------

void Loupe::show (juce::Component& source, juce::Point<int> localPoint)
{
    auto* top = overlayRootOf (source);
    if (top == nullptr)
        return;
    const int radius = theme.ipx (24.0f);
    const auto region = juce::Rectangle<int> (localPoint.x - radius, localPoint.y - radius, 2 * radius, 2 * radius).getIntersection (source.getLocalBounds());
    if (region.isEmpty())
        return;
    image = source.createComponentSnapshot (region, false, kMagnification);

    const int size = juce::roundToInt (static_cast<float> (2 * radius) * kMagnification);
    const auto finger = top->getLocalPoint (&source, localPoint);
    // above the finger (a thumb covers what is under it); below only when there is no room above
    int y = finger.y - size - theme.ipx (36.0f);
    if (y < 0)
        y = finger.y + theme.ipx (36.0f);
    juce::Rectangle<int> bounds (finger.x - size / 2, y, size, size);
    bounds = bounds.constrainedWithin (top->getLocalBounds());

    if (getParentComponent() != top)
    {
        if (auto* old = getParentComponent())
            old->removeChildComponent (this);
        top->addChildComponent (this);
    }
    host = &source;
    setBounds (bounds);
    setVisible (true);
    toFront (false);
    repaint();
}

void Loupe::hide()
{
    if (isVisible())
        setVisible (false);
    if (auto* parent = getParentComponent())
        parent->removeChildComponent (this);
}

void Loupe::paint (juce::Graphics& g)
{
    const auto& c = theme.colours;
    const auto r = getLocalBounds().toFloat();
    juce::Path clip;
    clip.addEllipse (r.reduced (1.0f));
    g.saveState();
    g.reduceClipRegion (clip);
    g.fillAll (c.panel);
    if (image.isValid())
        g.drawImage (image, getLocalBounds().toFloat(), juce::RectanglePlacement::centred);
    g.restoreState();
    g.setColour (c.accent);
    g.drawEllipse (r.reduced (1.5f), theme.px (2.0f));
    // the cross hair marks the exact point
    g.setColour (c.mod);
    g.drawLine (r.getCentreX(), r.getCentreY() - theme.px (8.0f), r.getCentreX(), r.getCentreY() + theme.px (8.0f), 1.0f);
    g.drawLine (r.getCentreX() - theme.px (8.0f), r.getCentreY(), r.getCentreX() + theme.px (8.0f), r.getCentreY(), 1.0f);
}

// ---- TwoFingerInput -----------------------------------------------------------------------------------------------------------------

void TwoFingerInput::feed (TouchTracker::Phase phase, const juce::MouseEvent& e)
{
    // a mouse (index 0 only) never forms a pair: the tracker needs two ids
    const auto g = tracker.feed (phase, e.source.getIndex(), { e.position.x, e.position.y }, e.eventTime.toMilliseconds());
    if (g.kind != TouchTracker::Kind::none && onGesture)
        onGesture (g);
}

void TwoFingerInput::magnify (juce::Point<float> centre, float scaleFactor)
{
    TouchTracker::Gesture g;
    g.kind = TouchTracker::Kind::pinch;
    g.scale = scaleFactor;
    g.centre = { centre.x, centre.y };
    inject (g);
}

// ---- Toast --------------------------------------------------------------------------------------------------------------------------

namespace
{
juce::Component::SafePointer<Toast>& toastSlot()
{
    static juce::Component::SafePointer<Toast> slot;
    return slot;
}
} // namespace

Toast::Toast (UiContext& context, juce::Component& parent, const juce::String& text, int milliseconds) : ctx (context), host (&parent), message (text)
{
    setInterceptsMouseClicks (false, false);
    parent.addAndMakeVisible (this);
    parent.addComponentListener (this);
    const int w = juce::jmin (parent.getWidth() - 20, 320);
    setBounds (juce::Rectangle<int> (parent.getWidth() / 2 - w / 2, parent.getHeight() - 150, w, 44).constrainedWithin (parent.getLocalBounds()));
    toFront (false);
    startTimer (milliseconds);
}

Toast::~Toast()
{
    stopTimer();
    if (host != nullptr)
        host->removeComponentListener (this);
}

void Toast::show (UiContext& context, juce::Component& anchor, const juce::String& text, int milliseconds)
{
    auto* top = overlayRootOf (anchor);
    if (top == nullptr)
        return;
    if (auto* old = toastSlot().getComponent())
        delete old;
    toastSlot() = new Toast (context, *top, text, milliseconds);
}

Toast* Toast::current()
{
    return toastSlot().getComponent();
}

void Toast::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (t.colours.panelRaised.withAlpha (0.95f));
    g.fillRoundedRectangle (r, t.px (10.0f));
    g.setColour (t.colours.accent);
    g.drawRoundedRectangle (r, t.px (10.0f), t.px (1.5f));
    g.setColour (t.colours.text);
    g.setFont (t.valueFont());
    g.drawFittedText (message, getLocalBounds().reduced (t.ipx (12.0f), t.ipx (4.0f)), juce::Justification::centred, 2);
}

void Toast::timerCallback()
{
    stopTimer();
    if (host != nullptr)
        host->removeChildComponent (this);
    delete this;
}

void Toast::componentBeingDeleted (juce::Component&)
{
    host = nullptr; // the editor goes first: nothing to remove from
    delete this;
}

// ---- ModuleMenuButton ---------------------------------------------------------------------------------------------------------------

ModuleMenuButton::ModuleMenuButton (UiContext& context, std::string prefix)
    : juce::TextButton ("..."), ctx (context), modulePrefix (std::move (prefix))
{
    setTooltip ("Module sheet: copy / paste / swap / init");
    onClick = [this] { showModuleSheet (ctx, modulePrefix, *this); };
    ctx.assign.addChangeListener (this); // the lock toggles broadcast
    refreshLock();
}

ModuleMenuButton::~ModuleMenuButton()
{
    ctx.assign.removeChangeListener (this);
}

void ModuleMenuButton::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshLock();
}

void ModuleMenuButton::refreshLock()
{
    const bool locked = module_ops::isLocked (ctx, modulePrefix);
    const juce::String text = locked ? "LOCK" : "...";
    if (getButtonText() != text)
        setButtonText (text);
    setTooltip (locked ? juce::String ("Module sheet (locked: a preset change keeps this module)") : juce::String ("Module sheet: copy / paste / swap / init / lock"));
}
} // namespace synthyboy::ui
