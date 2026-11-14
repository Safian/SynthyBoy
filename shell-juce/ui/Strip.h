// SynthyBoy - UI: the always-visible strip (docs/ui-design/01-shell-layout.md section 2): top bar, macro row, keyboard.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h> // MidiKeyboardComponent (the only use of juce_audio_utils)

#include "ParamControls.h"
#include "ParamHandle.h"
#include "model/ModModel.h"

namespace synthyboy::ui
{
/** Stereo peak output meter (left and right channels) with peak decay. */
class StereoMeter final : public juce::Component, private juce::Timer
{
public:
    explicit StereoMeter (UiContext& context);
    ~StereoMeter() override;

    void paint (juce::Graphics&) override;
    void refresh();

private:
    void timerCallback() override;

    UiContext& ctx;
    float levelL = 0.0f;
    float levelR = 0.0f;
    int lastBarL = -1, lastBarR = -1; // what the bars show now (quantised), -1 before the first refresh
};

/** Preset name with prev / next (browsing is Kick's: the buttons only call UiHost::stepPreset), Undo/Redo, A/B compare, assign-mode toggle, menu. */
class TopBar final : public juce::Component
{
public:
    explicit TopBar (UiContext& context);

    void setPresetName (const juce::String& name);
    void setAbLabel (const juce::String& text) { ab.setButtonText (text); }
    void setAssignOpen (bool open) { assign.setToggleState (open, juce::dontSendNotification); }
    void updateUndoRedo (bool canUndo, bool canRedo);
    /** "3/16v": the sounding voices against the voice cap; a tap jumps to GLOBAL (design 07). Shown where the top bar has room for it. */
    void setVoiceBadge (int active, int cap);
    bool voiceBadgeShown() const noexcept { return voices.isVisible(); }

    std::function<void()> onUndo, onRedo, onAbToggle, onAssignToggle, onMenu, onVoiceBadge, onPresetNameClick;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    juce::TextButton prev { "<" }, next { ">" };
    juce::TextButton undo { "U" }, redo { "R" };
    juce::TextButton ab { "A" }, assign { "MOD" }, menu { "..." };
    juce::TextButton voices { "0/16v" };

private:
    UiContext& ctx;
    juce::String presetName;
};

/** macro.1 .. macro.8 as compact knobs plus MAIN volume knob and stereo output meter. */
class MacroRow final : public juce::Component
{
public:
    explicit MacroRow (UiContext& context);
    void resized() override;
    const std::vector<std::unique_ptr<ParamKnob>>& knobs() const noexcept { return macros; }
    ParamKnob& mainVolumeKnob() noexcept { return *mainKnob; }
    StereoMeter& stereoMeter() noexcept { return *meter; }

private:
    UiContext& ctx;
    juce::Viewport viewport;
    juce::Component macroStrip;
    std::vector<std::unique_ptr<ParamKnob>> macros;
    std::unique_ptr<ParamKnob> mainKnob;
    std::unique_ptr<StereoMeter> meter;
};

/** Pitch wheel strip: vertical drag, springs back to centre on release (range master.bend_range). */
class PitchWheel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PitchWheel (UiContext& context);

    float value() const noexcept { return pos; }
    void setValue (float v); // -1.0 .. +1.0
    void resetToCenter();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void sendPitchBend (float v);

    UiContext& ctx;
    float pos = 0.0f;
    float dragStartPos = 0.0f;
    juce::Point<float> mouseDownPos;
};

/** Modulation wheel strip: vertical drag, stays in place. */
class ModWheel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModWheel (UiContext& context);

    float value() const noexcept { return pos; }
    void setValue (float v); // 0.0 .. 1.0
    void resetToZero();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void sendModWheel (float v);

    UiContext& ctx;
    float pos = 0.0f;
    float dragStartPos = 0.0f;
    juce::Point<float> mouseDownPos;
};

class LatchingKeyboard;

/** Mini keyboard with pitch / mod wheels, transpose, hold and scale controls. */
class KeyboardStrip final : public juce::Component
{
public:
    explicit KeyboardStrip (UiContext& context);
    ~KeyboardStrip() override;

    void resized() override;
    juce::MidiKeyboardComponent& keyboard() noexcept;
    void shiftOctave (int octaves);

    PitchWheel& pitch() noexcept { return *pitchWheel; }
    ModWheel& mod() noexcept { return *modWheel; }
    juce::TextButton& hold() noexcept { return holdButton; }
    juce::TextButton& scale() noexcept { return scaleButton; }
    juce::TextButton& transposeBtn() noexcept { return transposeValue; }

    juce::TextButton octDown { "oct-" }, octUp { "oct+" };
    juce::TextButton transposeDown { "<" }, transposeValue { "0" }, transposeUp { ">" };
    juce::TextButton holdButton { "HOLD" }, scaleButton { "SCALE" };

private:
    void showScaleMenu();
    void updateScaleButton();
    void updateTransposeButton();

    UiContext& ctx;
    std::unique_ptr<PitchWheel> pitchWheel;
    std::unique_ptr<ModWheel> modWheel;
    std::unique_ptr<LatchingKeyboard> keys;
    std::unique_ptr<ParamHandle> transposeHandle;
    std::unique_ptr<ParamHandle> scaleKeyHandle;
    std::unique_ptr<ParamHandle> scaleTypeHandle;
    std::unique_ptr<ParamHandle> scaleModeHandle;
};
} // namespace synthyboy::ui
