// SynthyBoy - UI: the drawn wavetable position remap graph of an oscillator (row 02-14 "Remap graph (pencil)", docs/ui-design/13-new-keys.md 4.14).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The curve maps the position of the wavetable (x: where the frame knob / modulation points, 0..1 over the frames) to the position that is
// played (y). It is CONTENT, not a parameter: content.remaps[] entry (osc, slot) with the slot chosen by oscN.remap.slot (Remap 1..4); a slot
// that nobody drew is the identity (the diagonal). Read and written through UiHost::content () / setContent (); the engine bakes the same
// point list (core/shapes) that is previewed here.
//
// Gestures: drag a point (the two end points move vertically only), double-tap the line = add a point, double-tap a point = delete it, drag
// the diamond on a segment = curve power of that segment (double-tap = straight). The Pencil button turns the graph into a freehand tool
// (drag across the plot to paint the curve; one stroke = one undo step). Buttons are the non-gesture alternatives: Add, Delete, Undo, Redo,
// Presets (Linear, Reverse, ease curves, steps), Reset (back to the identity, the entry is removed from the content).
#pragma once

#include <vector>

#include "../ParamHandle.h"
#include "../UiContext.h"
#include "synthyboy/content_state.h"
#include "synthyboy/shapes/shape.h"

namespace synthyboy::ui
{
class RemapEditor final : public juce::Component, private juce::Timer
{
public:
    RemapEditor (UiContext& context, int oscIndex);
    ~RemapEditor() override;

    int preferredHeight (int width) const;

    // ---- state (tests, panel) ----
    int osc() const noexcept { return oscN; }
    /** The slot oscN.remap.slot shows, 1..4. */
    int slot() const;
    const ContentRemap& remap() const noexcept { return current; }
    /** True if the content holds a drawn curve for this oscillator and slot (false: the identity, nothing stored). */
    bool isDrawn() const noexcept { return stored; }
    int selectedPoint() const noexcept { return selected; }
    bool pencilOn() const noexcept { return pencil; }
    void setPencil (bool on);
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    juce::TextButton* button (const juce::String& text);

    static constexpr int kNumPresets = 8;
    static const char* presetName (int preset) noexcept; // Linear Reverse Ease in Ease out S curve Steps 4 Steps 8 Jump

    /** The curve as the engine bakes it: y for x in 0..1 (the identity while nothing is drawn). */
    float evaluateAt (float x) const;

    // ---- editing API (what the gestures and buttons call; each call is one undo step) ----
    bool addPointAt (float x);
    bool deletePoint (int index);
    bool deleteSelected() { return deletePoint (selected); }
    bool movePoint (int index, float x, float y);
    bool setSegmentPower (int segment, float power);
    /** The freehand stroke of the pencil: the plot positions (0..1 x 0..1) the finger went through, in order. */
    bool paintStroke (const std::vector<juce::Point<float>>& stroke);
    void applyPreset (int preset);
    void resetToIdentity();
    void undo();
    void redo();
    /** Writes pending edits to the host now (a timer does it at 30 Hz while dragging, mouse up does it at once). */
    void commitNow();
    /** Re-reads the curve of the current slot from the host (slot changed, preset load). Ignored while a gesture is open. */
    void reloadFromHost();

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
    void touched();
    int barHeight (int width, bool layout);
    float toX (float x) const noexcept;
    float toY (float y) const noexcept;
    float fromX (float px) const noexcept;
    float fromY (float py) const noexcept;
    int hitPoint (juce::Point<float> p) const;
    int hitHandle (juce::Point<float> p) const;
    void setIdentityPoints();
    void updateButtons();
    shapes::ShapeData shapeData() const;

    UiContext& ctx;
    int oscN;
    ParamHandle slotHandle; // oscN.remap.slot
    ContentRemap current;
    bool stored = false; // an entry for (osc, slot) exists in the content
    int loadedSlot = 0;
    std::vector<ContentRemap> undoStack, redoStack;
    int selected = -1;
    bool pencil = false, dirty = false;
    int idleTicks = 0;

    enum class Drag { none, point, handle, pencil } drag = Drag::none;
    int dragIndex = -1;
    float dragStartPower = 0.0f;
    ContentRemap gestureStart;
    bool gestureChanged = false;
    std::vector<juce::Point<float>> strokePoints;

    mutable shapes::ShapeData cache;
    mutable bool cacheValid = false;

    juce::Rectangle<float> plot;
    juce::TextButton pencilBtn { "Pencil" }, addBtn { "Add" }, deleteBtn { "Delete" }, undoBtn { "Undo" }, redoBtn { "Redo" },
        presetBtn { "Presets" }, resetBtn { "Reset" };
    std::vector<juce::TextButton*> buttons;
};
} // namespace synthyboy::ui
