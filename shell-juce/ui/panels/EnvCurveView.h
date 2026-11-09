// SynthyBoy - UI: EnvCurveView - Interactive ADSHR envelope curve display.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

#include "../ParamHandle.h"
#include "../UiContext.h"

namespace synthyboy::ui
{

/** Big visual for the Envelope panel (docs/ui-design/05-envelopes-lfos.md section 2.1).
    Direct manipulation: drag nodes (delay, attack, hold, decay/sustain, release) and curve handles.
    Draws faint ENV1 curve behind for Envelope 2..6.
    Supports Milliseconds vs Beats units, time ruler, and zoom lock. */
class EnvCurveView : public juce::Component
{
public:
    EnvCurveView (UiContext& context, int envIndex);
    ~EnvCurveView() override;

    int getEnvIndex() const noexcept { return envIdx; }

    bool isZoomLocked() const noexcept { return zoomLocked; }
    void setZoomLocked (bool locked);
    /** Time zoom while the lock is off: 1 = the graph fits, > 1 shows the first part of the envelope larger. Pinch / wheel change it. */
    float zoom() const noexcept { return zoomLocked ? 1.0f : timeZoom; }
    void zoomBy (float factor);
    /** The magnifier slider: sets the time zoom (1 .. 16) while the lock is off. */
    void setZoom (float newZoom);
    static constexpr float minZoom() noexcept { return 1.0f; }
    static constexpr float maxZoom() noexcept { return 16.0f; }
    /** Fired when the zoom or the lock changed (the magnifier follows a pinch). */
    std::function<void()> onZoomChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    enum class HitNode
    {
        none,
        delay,       // N1
        attack,      // N2
        hold,        // N3
        decaySustain,// N4 (x = decay, y = sustain)
        release,     // N5
        attackCurve, // Curve dot 1
        decayCurve,  // Curve dot 2
        releaseCurve // Curve dot 3
    };

    /** WP-17: where the playhead of the heard voice is (in the coordinates of the view); false while no voice sounds or the engine gives no read-out. */
    bool livePlayhead (juce::Point<float>& out) const;

    /** Pure logic test hook: returns node positions and hit testing without requiring a display. */
    juce::Point<float> getNodePosition (HitNode node) const;
    HitNode hitTestNode (juce::Point<float> pos) const;

private:
    struct EnvValues
    {
        float delay = 0.0f;
        float attack = 0.005f;
        float hold = 0.0f;
        float decay = 0.4f;
        float sustain = 1.0f;
        float release = 0.2f;
        float attackCurve = 0.0f;
        float decayCurve = -3.0f;
        float releaseCurve = -3.0f;
        bool isBeats = false;
        float delayBeats = 0.0f;
        float attackBeats = 0.0625f;
        float holdBeats = 0.0f;
        float decayBeats = 1.0f;
        float releaseBeats = 0.5f;
    };

    EnvValues readCurrentValues() const;
    EnvValues readEnv1Values() const;

    juce::Rectangle<float> plotArea() const;
    float timeToX (float timeSec, float totalTimeSec, const juce::Rectangle<float>& area) const;
    float xToTime (float x, float totalTimeSec, const juce::Rectangle<float>& area) const;
    float valToY (float val, const juce::Rectangle<float>& area) const;
    float yToVal (float y, const juce::Rectangle<float>& area) const;

    juce::Path buildPath (const EnvValues& v, const juce::Rectangle<float>& area, float totalTime) const;

    UiContext& ctx;
    int envIdx = 1;
    bool zoomLocked = true;
    float timeZoom = 1.0f;
    static constexpr float kMinTimeZoom = 1.0f, kMaxTimeZoom = 16.0f;
    float displayZoom() const noexcept { return zoom(); }

    HitNode activeNode = HitNode::none;
    std::string activeKey;
    float dragStartValX = 0.0f;
    float dragStartValY = 0.0f;
    juce::Point<float> dragStartMouse;
    bool inGesture = false;

    // Parameter handles for active envelope
    ParamHandle hDelay, hAttack, hHold, hDecay, hSustain, hRelease;
    ParamHandle hAttackCurve, hDecayCurve, hReleaseCurve;
    ParamHandle hUnits;
    ParamHandle hDelayBeats, hAttackBeats, hHoldBeats, hDecayBeats, hReleaseBeats;

    juce::String dragBubbleText;
};

} // namespace synthyboy::ui
