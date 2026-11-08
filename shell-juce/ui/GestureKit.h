// SynthyBoy - UI: helpers shared by the gestures of the audit gap closure: loupe, two-finger input, long-press detection, header sheet.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "UiContext.h"
#include "model/GestureModel.h"

namespace synthyboy::ui
{
/** The loupe above the finger (design 02 / 03: marker and canvas drags). While `show ()` is active a magnified snapshot of the source
    around the finger is drawn on the top level component, above the finger so the hand does not cover it. */
class Loupe final : public juce::Component
{
public:
    explicit Loupe (Theme& themeToUse) : theme (themeToUse)
    {
        setInterceptsMouseClicks (false, false);
        setVisible (false);
    }
    ~Loupe() override { hide(); }

    /** `localPoint` is in `source` coordinates. Safe to call on every drag event. */
    void show (juce::Component& source, juce::Point<int> localPoint);
    void hide();
    bool isShowing() const noexcept { return isVisible(); }
    /** Bounds of the loupe in the coordinates of its parent (the tests check that it does not sit under the finger). */
    juce::Rectangle<int> loupeBounds() const noexcept { return getBounds(); }

    void paint (juce::Graphics&) override;

    static constexpr float kMagnification = 2.5f;

private:
    Theme& theme;
    juce::Image image;
    juce::Component::SafePointer<juce::Component> host;
};

/** Feeds a TouchTracker with the mouse events of a component (every finger is its own mouse source) and calls `onGesture`. A component
    forwards its mouseDown / mouseDrag / mouseUp and mouseMagnify here; the desktop wheel with Ctrl/Cmd is a pinch, the plain wheel a pan. */
class TwoFingerInput
{
public:
    std::function<void (const TouchTracker::Gesture&)> onGesture;

    void down (const juce::MouseEvent& e) { feed (TouchTracker::Phase::down, e); }
    void drag (const juce::MouseEvent& e) { feed (TouchTracker::Phase::drag, e); }
    void up (const juce::MouseEvent& e) { feed (TouchTracker::Phase::up, e); }
    /** A trackpad / platform pinch. */
    void magnify (juce::Point<float> centre, float scaleFactor);
    /** True while two fingers are down: the component should not run its one-finger gesture. */
    bool twoDown() const noexcept { return tracker.activeTouches() >= 2; }
    TouchTracker& touchTracker() noexcept { return tracker; }

    /** Feeds a ready-made gesture (tests, hardware). */
    void inject (const TouchTracker::Gesture& g)
    {
        if (onGesture)
            onGesture (g);
    }

private:
    void feed (TouchTracker::Phase phase, const juce::MouseEvent& e);
    TouchTracker tracker;
};

/** Long-press detector for components that cannot derive from ParamControl (headers, chips, strips, canvases). */
class LongPressDetector final : private juce::Timer
{
public:
    std::function<void()> onLongPress;

    void press (juce::Point<float> where)
    {
        downPos = where;
        startTimer (kLongPressMs);
    }
    /** Cancels when the finger moved beyond the slop. */
    void moved (juce::Point<float> where)
    {
        if ((where - downPos).getDistanceFromOrigin() > kLongPressSlopPx)
            cancel();
    }
    void cancel() { stopTimer(); }
    bool pending() const { return isTimerRunning(); }
    /** Fires now (tests). */
    void fire()
    {
        stopTimer();
        if (onLongPress)
            onLongPress();
    }

private:
    void timerCallback() override { fire(); }
    juce::Point<float> downPos;
};

/** A short confirmation at the bottom of the editor ("Preset initialised"): added to the top level component of the anchor, removes itself
    after a moment (or with its parent). One at a time. */
class Toast final : public juce::Component, private juce::Timer, private juce::ComponentListener
{
public:
    static void show (juce::Component& anchor, const juce::String& text, int milliseconds = 1800);
    /** The toast that is up now, null if none. */
    static Toast* current();
    const juce::String& text() const noexcept { return message; }
    void paint (juce::Graphics&) override;

private:
    Toast (juce::Component& parent, const juce::String& text, int milliseconds);
    ~Toast() override;
    void timerCallback() override;
    void componentBeingDeleted (juce::Component&) override;

    juce::Component::SafePointer<juce::Component> host;
    juce::String message;
};

/** The "..." button of a module header: the visible alternative to the header long-press (design 01: opens the same module sheet). */
class ModuleMenuButton final : public juce::TextButton
{
public:
    ModuleMenuButton (UiContext& context, std::string prefix);
    const std::string& prefix() const noexcept { return modulePrefix; }

private:
    UiContext& ctx;
    std::string modulePrefix;
};
} // namespace synthyboy::ui
