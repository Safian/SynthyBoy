// SynthyBoy - core: one cycle of the wavetable oscillator of the own engine as it plays it (for the oscillator displays: the MPC overlay).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ---- Oscillator cycle (how a display uses this) ----
//
//   OscCycle cycle;                                            // once: allocates (an oscillator with its morph buffers)
//   cycle.setContent (content, store);                         // when the state's content changes (NOT real time: decodes user wavetables and remap curves)
//   OscCycle::Request req;  req.osc = 1;
//   req.framePercent = live.target[k];                         // the modulated values of the readout (readoutIndices (osc, idx)); leave unset for the knob values
//   float y[OscCycle::kSamples];
//   OscCycle::Kind kind = cycle.compute (values, req, y);      // any thread (one at a time), no allocation, no engine involved, deterministic
//
// The cycle is rendered by the oscillator of the voice (own::WtOsc, the same code and the same tick values as Voice::tick builds): the wavetable of oscN.wavetable (a
// built-in table, or the preset table of the content) at the frame position INCLUDING the smooth / stepped frame reading, the frame remap, warp 1 and 2 (type, amount,
// phase), the spectral morph and the oversampling the warps need, at one voice (no unison), one note long: the fundamental is chosen so that one cycle is kSamples samples,
// the cycle starts at phase 0 and the filter delay of the oversampled path is taken out. The result is NOT normalised: 1 is a full scale oscillator (level 100 %).
// What is not in it: unison, pan, level, the pitch parameters and the oscillator's own start phase (the cycle always starts at 0), the warps that read another source (FM / AM /
// sync from another oscillator: their source is silent here), key and velocity ranges, and the envelopes.
// Kind: wavetable = a cycle was written; other = the oscillator is a sample / granular / spectral / multisample one: no single cycle picture exists, zeros are written;
// off = the oscillator is switched off, zeros are written.
#pragma once

#include <memory>
#include <optional>

#include "state.h"

namespace synthyboy
{
struct StateContent;
class IContentStore;

class OscCycle
{
public:
    static constexpr int kSamples = 512;

    enum class Kind { wavetable, other, off };

    struct Request
    {
        int osc = 1;                                       // 1 .. 3
        std::optional<float> framePercent;                 // oscN.frame, plain percent 0 .. 100 (e.g. the modulated value of the readout)
        std::optional<float> warpAmountPercent[2];         // oscN.warp.amount / oscN.warp2.amount
        std::optional<float> morphAmountPercent;           // oscN.morph.amount
    };

    OscCycle();
    ~OscCycle();
    OscCycle (const OscCycle&) = delete;
    OscCycle& operator= (const OscCycle&) = delete;

    /** Decodes the content (user wavetable, remap curves) with `store` (may be null). Not real time. */
    void setContent (const StateContent& content, const IContentStore* store);

    /** Table indices (IEngine::setReadoutTargets order) of oscN.frame, oscN.warp.amount, oscN.warp2.amount, oscN.morph.amount; -1 for an oscillator other than 1 .. 3. */
    static void readoutIndices (int osc, int out[4]) noexcept;

    /** Writes kSamples values (one cycle). Never fails: bad input gives Kind::other and zeros. */
    Kind compute (const ParamValues& values, const Request& request, float* out) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace synthyboy
