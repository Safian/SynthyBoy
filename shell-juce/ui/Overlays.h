// SynthyBoy - UI: ModRing (purple depth ring) and ValueBubble (value pop-up with the fine-adjust ladder).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "UiContext.h"

namespace synthyboy::ui
{
/** Overlay on a knob (ring) or slider (bar), mouse transparent. Draws, over the control's own value arc:
    - while a source is armed and the control is a valid target: a thin purple target ring,
    - one arc per assigned modulation from the knob position by amount (purple positive, lighter purple negative, symmetric when the slot
      is bipolar, dashed grey when bypassed, thin grey when its source is not the armed one),
    - a count badge when something is assigned. The amount is shown as a fraction of the control's normalised range. */
class ModRing final : public juce::Component
{
public:
    enum class Shape { ring, bar };

    ModRing (const Theme& themeToUse, Shape shapeToUse);

    struct State
    {
        std::vector<ModEntry> entries;
        float position = 0.0f;   // normalised value of the control
        bool target = false;     // armed and modulatable
        int armedSource = 0;     // choice value, 0 = none
        int activeSlot = -1;     // slot being dragged
        int midiCc = -1;         // the CC mapped to the control (MIDI learn), -1 none
        bool learning = false;   // waiting for a CC
        float live = -1.0f;      // the normalised value the engine really plays now (all modulations in), -1 = unknown (GROUPA2 WP-17)
    };

    void setState (State newState);
    const State& state() const noexcept { return current; }

    /** Pure geometry used by paint and by tests: the normalised span [from, to] an entry covers around `position`. */
    static std::pair<float, float> spanFor (const ModEntry& e, float position) noexcept;

    void paint (juce::Graphics&) override;

private:
    const Theme& theme;
    Shape shape;
    State current;
};

/** Floating value pop-up above the finger. Added to the top level component by the control while a drag runs. */
class ValueBubble final : public juce::Component
{
public:
    explicit ValueBubble (const Theme& themeToUse) : theme (themeToUse) { setInterceptsMouseClicks (false, false); }

    void setContent (const juce::String& valueText, FineZone activeZone);
    juce::Rectangle<int> idealSize() const;
    void paint (juce::Graphics&) override;

private:
    const Theme& theme;
    juce::String text;
    FineZone zone = FineZone::normal;
};

/** A modal line-of-text prompt (a preset name, a bin value) inside the editor: a dimmed scrim, a title, a text field and two touch-size buttons. It replaces the
    juce::AlertWindow, which is a desktop level window. `done` is called with the text when the user confirms (Return or the OK button), never when it is cancelled
    (Escape, Cancel, a tap outside). Deletes itself afterwards or with the editor. Message thread. */
void showTextPrompt (UiContext& context, const juce::String& title, const juce::String& label, const juce::String& initial, const juce::String& okText,
                     std::function<void (const juce::String&)> done);
} // namespace synthyboy::ui
