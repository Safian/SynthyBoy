// SynthyBoy - UI: a control's binding to one parameter, by KEY.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Message thread only. All writes go through juce::ParameterAttachment (setValueNotifyingHost with begin / end gesture for host
// automation); reads of the plain value use the parameter's atomic. Nothing here is ever called from the audio thread.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "UiContext.h"

namespace synthyboy::ui
{
class ParamHandle
{
public:
    /** An unknown key gives valid() == false: controls then draw a warning placeholder instead of crashing. */
    ParamHandle (UiContext& context, const std::string& key);

    bool valid() const noexcept { return param != nullptr && ref.valid(); }
    const std::string& key() const noexcept { return keyText; }
    /** The parameter whose range, curve, unit and names apply (for a control of a pool slot: the key of instance 0 it stands for). */
    const ParamInfo& info() const noexcept { return *ref.info; }
    /** The stored parameter: the one that is automated and modulated (for a control of a pool slot: fxiNN.pKK). */
    int index() const noexcept { return ref.index; }
    /** Table index of info() (the same as index() except for a control of a pool slot). */
    int infoIndex() const noexcept { return ref.sourceIndex(); }
    /** True if the control can be a modulation destination: the stored parameter is one and, for a pool slot, so is the key of the type. */
    bool modulatable() const noexcept;

    float plain() const;
    float normalised() const;
    float defaultPlain() const { return info().def; }
    CapState caps() const;

    /** Fired on the message thread whenever the value changes, from the UI, the host or a state load. */
    std::function<void()> onChange;

    void beginGesture();
    void setNormalised (float normalised); // inside a gesture
    void endGesture();
    bool inGesture() const noexcept { return gestureOpen; }
    /** begin + set + end in one go (tap, reset, numeric entry, menu choice). */
    void setPlainComplete (float plain);
    void resetToDefault();

private:
    UiContext& ctx;
    std::string keyText;
    KeyRef ref;
    juce::RangedAudioParameter* param = nullptr;
    std::atomic<float>* rawValue = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool gestureOpen = false;
};
} // namespace synthyboy::ui
