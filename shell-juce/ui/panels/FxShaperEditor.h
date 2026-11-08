// SynthyBoy - UI: editor of the two X-Shaper transfer curves of the distortion (distortion.type = X-Shaper / X-Shaper (Asym); docs/ui-design/13-new-keys.md
// 4.14, G75).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The curves are CONTENT, not parameters (content.curves.shaper_a / shaper_b): read and written through UiHost::content () / setContent ().
// x = input magnitude, y = output magnitude (both 0..1); X-Shaper uses curve A for both polarities, X-Shaper (Asym) uses A for the positive and B for the
// negative half. The curve is drawn with the engine's own evaluator (core/shapes), so the display is what the distortion bakes.
//
// Gestures: drag a point (the end points move vertically only), double-tap empty plot = add a point, double-tap a point = delete it, drag the diamond in
// the middle of a segment = bend (power) of that segment (double-tap = straight). Buttons (the non-gesture alternatives, 44 point high): A / B, Add,
// Delete, Presets, Reset. Edits are written to the host at 15 Hz while dragging and at once on release.
#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../UiContext.h"
#include "../model/FxModel.h"
#include "synthyboy/shapes/shape.h"

namespace synthyboy::ui
{
class ShaperCurveEditor final : public juce::Component,
                                private juce::Timer
{
public:
    explicit ShaperCurveEditor (UiContext& context);
    ~ShaperCurveEditor() override;

    /** 0 = curve A, 1 = curve B. */
    void setCurve (int which);
    int curve() const noexcept { return which; }
    /** Curve B only exists for X-Shaper (Asym): its tab is dimmed otherwise. */
    void setAsymmetric (bool isAsymmetric);
    bool asymmetric() const noexcept { return asym; }
    /** Re-reads the points of the shown curve from the host (preset load). Ignored while a gesture is open. */
    void reloadFromHost();

    // ---- editing API (what the gestures and the buttons call) ----
    bool addPointAt (float x, float y);
    bool deletePoint (int index);
    bool movePoint (int index, float x, float y);
    bool setSegmentPower (int segment, float power);
    void applyPreset (CurvePreset preset);
    void reset();
    /** Writes the pending edits to the host now. */
    void commitNow();

    const std::vector<ContentPoint>& points() const noexcept { return current; }
    int selectedPoint() const noexcept { return selected; }
    /** The value of the curve at an input magnitude (the engine's evaluator). */
    float evaluate (float x) const;

    // ---- geometry and parts (tests) ----
    juce::Rectangle<float> plotBounds() const noexcept { return plot; }
    juce::Point<float> pointPosition (int index) const;
    juce::Point<float> handlePosition (int segment) const;
    juce::TextButton* button (const juce::String& text);
    int preferredHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Drag { none, point, handle };

    void timerCallback() override;
    void rebuildCache();
    void markDirty();
    float powerForMiddle (int segment, float targetY) const;
    int hitPoint (juce::Point<float> p) const;
    int hitHandle (juce::Point<float> p) const;
    juce::Point<float> toView (float x, float y) const;
    juce::Point<float> fromView (juce::Point<float> p) const;
    void showPresetMenu();

    UiContext& ctx;
    int which = 0;
    bool asym = false;
    std::vector<ContentPoint> current;
    mutable shapes::ShapeData cache;
    int selected = -1;
    Drag drag = Drag::none;
    int dragIndex = -1;
    bool dirty = false;
    juce::Rectangle<float> plot;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
};
} // namespace synthyboy::ui
