// SynthyBoy - UI: free-form LFO shape editor (lfoN.shape = Custom, point list in content.lfo_shapes[]; docs/ui-design/05 sections 2.4, 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Component used by the LFO panel in place of the read-only shape display while the shape is Custom. The point list is CONTENT, not a
// parameter: it is read and written through UiHost::content () / setContent () (the processor validates it, hands it to the engine and
// keeps it in the host state). Edits are previewed with the engine's own evaluator (core/shapes), so the display is what is baked.
//
// Gestures: drag a point (end points move vertically only), double-tap the line = add a point, double-tap a point = delete it,
// drag the diamond in the middle of a segment = curve power of that segment (double-tap = straight), Draw tool = paint the shape in
// 32 columns, Snap = grid 16 x 8. Buttons are the non-gesture alternatives (Add, Delete, Undo, Redo, Presets, Smooth).
// One drag = one undo step.
#pragma once

#include <memory>
#include <vector>

#include "../UiContext.h"
#include "synthyboy/shapes/shape.h"

namespace synthyboy::ui
{
/** The built-in lfoN.shape choice 0..11 as a point list for lfo `lfo` (1..10); anything else gives a flat centre line. */
ContentLfoShape lfoShapePreset (int lfo, int builtInShape);
/** Name of the preset (the lfoN.shape choice name) 0..11. */
const char* lfoShapePresetName (int builtInShape);

/** Copies every lfoN.* (or rlfoN.*) parameter of LFO `from` onto LFO `to` (design 05 section 3: drag a tab onto another tab), and the
    custom shape point list of a normal LFO. Returns false for a bad index / the same LFO. One host gesture per parameter. */
bool copyLfoSettings (UiContext& ctx, bool random, int from, int to);

class LfoShapeEditor final : public juce::Component, private juce::Timer
{
public:
    LfoShapeEditor (UiContext& context, int lfoIndex);
    ~LfoShapeEditor() override;

    /** Height wanted at `width` (button rows + the plot). */
    int preferredHeight (int width) const;

    /** The panel shows the editor (shape = Custom) or not. Activating reads the point list from the host; if the LFO has none yet it is
        created from built-in shape `startFrom` and written to the host, so what is drawn is what plays. */
    void setActive (bool shouldBeActive, int startFrom = 0);
    bool isActive() const noexcept { return active; }
    /** Re-reads the point list from the host (preset load, copy from another LFO). Ignored while a gesture is open. */
    void reloadFromHost();

    // ---- state (tests, panel) ----
    const ContentLfoShape& shape() const noexcept { return current; }
    int selectedPoint() const noexcept { return selected; }
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    bool snapOn() const noexcept { return snap; }
    bool drawToolOn() const noexcept { return drawTool; }

    // ---- editing API (what the gestures and buttons call; each call is one undo step) ----
    bool addPointAt (float x, float y);
    bool deletePoint (int index);
    bool deleteSelected() { return deletePoint (selected); }
    bool movePoint (int index, float x, float y);
    bool setSegmentPower (int segment, float power);
    void applyPreset (int builtInShape);
    void setSmooth (bool shouldSmooth);
    void setSnap (bool shouldSnap);
    void setDrawTool (bool shouldDraw);
    void undo();
    void redo();
    /** Writes pending edits to the host now (a timer does it at 30 Hz while dragging, mouse up does it at once). */
    void commitNow();

    // ---- geometry (tests) ----
    juce::Rectangle<float> plotBounds() const noexcept { return plot; }
    juce::Point<float> pointPosition (int index) const;
    juce::Point<float> handlePosition (int segment) const;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void pushUndo();
    void touched();                       // after a change: repaint, mark dirty
    int barHeight (int width, bool layout);
    float toX (float x) const noexcept;
    float toY (float y) const noexcept;
    float fromX (float px) const noexcept;
    float fromY (float py) const noexcept;
    int hitPoint (juce::Point<float> p) const;
    int hitHandle (juce::Point<float> p) const;
    float evaluateAt (float x) const;
    void drawAt (juce::Point<float> p);
    void updateButtons();

    UiContext& ctx;
    int lfo;
    bool active = false;
    ContentLfoShape current;
    std::vector<ContentLfoShape> undoStack, redoStack;
    int selected = -1;
    bool snap = false, drawTool = false, dirty = false;
    int idleTicks = 0;

    // open gesture
    enum class Drag { none, point, handle, draw } drag = Drag::none;
    int dragIndex = -1;
    float dragStartPower = 0.0f;
    ContentLfoShape gestureStart;
    bool gestureChanged = false;
    int lastDrawColumn = -1;
    float lastDrawY = 0.0f;

    mutable shapes::ShapeData cache; // the engine's evaluator on `current`, rebuilt after every change
    mutable bool cacheValid = false;

    juce::Rectangle<float> plot;
    juce::TextButton pointBtn { "Point" }, drawBtn { "Draw" }, snapBtn { "Snap" }, smoothBtn { "Smooth" }, addBtn { "Add" },
        deleteBtn { "Delete" }, undoBtn { "Undo" }, redoBtn { "Redo" }, presetBtn { "Presets" };
    std::vector<juce::TextButton*> buttons;
};
} // namespace synthyboy::ui
