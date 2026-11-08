// SynthyBoy - UI panel: Wavetable view and editor (docs/ui-design/03-wavetable.md, page `osc`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <string>
#include <vector>

#include "../GestureKit.h"
#include "../Panel.h"
#include "WavetableEditorSheet.h"

namespace synthyboy::ui
{

/** Big interactive wavetable visual component with 2D, 3D stacked-frame, and SP modes. */
class WavetableView final : public juce::Component
{
public:
    enum class ViewMode { mode2D, mode3D, modeSP };

    WavetableView (UiContext& context, std::function<void()> onOpenEditorCallback = nullptr);
    ~WavetableView() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

    ViewMode viewMode() const noexcept { return currentViewMode; }
    void setViewMode (ViewMode mode);
    void cycleViewMode();

    /** Sizing recommendation. */
    int preferredHeight() const;

    /** Access to internal wavetable. */
    const wtedit::Wavetable& wavetable() const noexcept { return wt; }
    void setWavetable (const wtedit::Wavetable& newWt);

    /** Zoom controls for 3D tilt/zoom. */
    void zoomIn();
    void zoomOut();

    /** 3D view: two fingers (or the trackpad / wheel) tilt and zoom, design 03 row "3D view". Pinch = zoom, two-finger drag up / down = tilt
        (how steep the stack of frames rises), sideways = yaw (how far the frames slide). Double tap on the view and Reset restore it. */
    float tilt() const noexcept { return tiltY; }
    float yaw() const noexcept { return yawX; }
    float zoom() const noexcept { return zoomLevel; }
    void handleTouchGesture (const TouchTracker::Gesture& g);
    void resetView();

private:
    int headerHeight() const;
    void stepWavetable (int direction);
    void updateTableFromChoice();

    UiContext& ctx;
    std::function<void()> onOpenEditor;
    ViewMode currentViewMode = ViewMode::mode3D;
    wtedit::Wavetable wt;

    // Handles to relevant osc1 parameters
    ParamHandle handleWavetable;
    ParamHandle handleFrame;
    ParamHandle handleFrameSpread;
    ParamHandle handlePhase;
    ParamHandle handleRandomPhase;
    ParamHandle handleWarpType;
    ParamHandle handleWarpAmount;
    ParamHandle handleMorphType;
    ParamHandle handleMorphAmount;

    // Direct manipulation state
    bool isDragging = false;
    juce::Point<float> dragStartPos;
    float startFrameVal = 0.0f;
    float zoomLevel = 1.0f;
    float tiltY = 1.0f; // 0.25 .. 2: factor on the vertical step between frames
    float yawX = 0.0f;  // -1 .. 1: factor 1 + yaw on the horizontal step
    TwoFingerInput fingers;
    bool pairActive = false; // a two-finger gesture is running: the one-finger scrub is off
    int selectedHarmonic = 1;

    // Controls embedded in the visual header
    juce::Label tableNameLabel;
    juce::TextButton prevTableBtn { "<" };
    juce::TextButton nextTableBtn { ">" };
    juce::TextButton mode2DBtn { "2D" };
    juce::TextButton mode3DBtn { "3D" };
    juce::TextButton modeSpBtn { "SP" };
    juce::TextButton editBtn { "EDIT" };
    juce::TextButton zoomInBtn { "+" };
    juce::TextButton zoomOutBtn { "-" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableView)
};

/** Wavetable panel registered on Page::osc after the oscillators. */
class WavetablePanel final : public Panel
{
public:
    explicit WavetablePanel (UiContext& context);
    ~WavetablePanel() override = default;

    juce::String title() const override { return "Wavetable"; }
    std::vector<std::string> keys() const override;
    void build() override;

    int preferredHeight (int width) const override;
    void resized() override;

    WavetableView* visual() noexcept { return waveView.get(); }
    void openEditorSheet();

private:
    std::unique_ptr<WavetableView> waveView;
    std::unique_ptr<WavetableEditorSheet> editorSheet;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetablePanel)
};

} // namespace synthyboy::ui
