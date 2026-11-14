// SynthyBoy - core: one cycle of the wavetable oscillator of the own engine (see osc_cycle.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/osc_cycle.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/params.h"
#include "dsp/util.h"
#include "osc/content_set.h"
#include "osc/lib_osc.h"
#include "osc/wt_osc.h"
#include "voice/param_index.h"

namespace synthyboy
{
namespace
{
constexpr double kRate = 48000.0;        // the rate the oscillator is run at; one cycle is kSamples samples of it
constexpr int kWarmCycles = 2;           // cycles run before the one that is kept (the oversampling filter, the morph cross-fade)
}

struct OscCycle::Impl
{
    Impl()
    {
        osc.prepareMorph();
        L.assign (static_cast<size_t> ((kWarmCycles + 1) * OscCycle::kSamples + 256), 0.0f);
        R = L;
    }

    own::WtOsc osc;
    own::Rng rng;
    std::unique_ptr<own::ContentSet> set;
    std::vector<float> L, R;
};

OscCycle::OscCycle() : impl (new Impl) {}
OscCycle::~OscCycle() = default;

void OscCycle::setContent (const StateContent& content, const IContentStore* store)
{
    impl->set = own::ContentSet::build (content, store);
}

void OscCycle::readoutIndices (int osc, int out[4]) noexcept
{
    if (out == nullptr)
        return;
    out[0] = out[1] = out[2] = out[3] = -1;
    if (osc < 1 || osc > 3)
        return;
    const own::OscIdx& oi = own::paramIndex().osc[osc - 1];
    out[0] = oi.frame;
    out[1] = oi.warpAmount;
    out[2] = oi.warp2Amount;
    out[3] = own::libKeys (osc - 1).idx[own::lk_morphAmount];
}

OscCycle::Kind OscCycle::compute (const ParamValues& values, const Request& r, float* out) noexcept
{
    if (out == nullptr)
        return Kind::other;
    std::fill (out, out + kSamples, 0.0f);
    if (r.osc < 1 || r.osc > 3)
        return Kind::other;
    const int o = r.osc - 1;
    const own::OscIdx& oi = own::paramIndex().osc[o];
    const own::LibKeys& lk = own::libKeys (o);
    const ParamInfo* table = paramTable();
    auto P = [&] (int i)
    {
        if (i < 0)
            return 0.0f;
        const float x = values.plain[i];
        return std::isfinite (x) ? std::clamp (x, table[i].min, table[i].max) : table[i].def;
    };
    if (P (oi.on) < 0.5f)
        return Kind::off;
    if (static_cast<int> (P (oi.type) + 0.5f) != kOscTypeWavetable)
        return Kind::other;

    Impl& m = *impl;
    const own::ContentSet* cs = m.set.get();
    own::OscTick t;
    t.active = true;
    int wtChoice = std::clamp (static_cast<int> (P (oi.wavetable) + 0.5f), 0, kNumWavetableChoices - 1);
    t.wt = wtChoice == kWavetablePresetTable ? (cs != nullptr ? cs->userTable[o].get() : nullptr) : own::builtinWavetable (wtChoice);
    if (t.wt == nullptr)
        t.wt = own::builtinWavetable (kWavetableSaw);   // as the voice: the preset table needs its content
    const double f0 = kRate / static_cast<double> (kSamples);
    t.semis = static_cast<float> (69.0 + 12.0 * std::log2 (f0 / 440.0));
    float frame = own::clamp01 ((r.framePercent ? *r.framePercent : P (oi.frame)) * 0.01f);
    t.frameSpread = 0.0f;
    t.level = 1.0f;
    t.pan = t.panRight = 0.0f;
    t.voices = 1;
    t.warp[0] = static_cast<int> (P (oi.warpType) + 0.5f);
    t.warpAmount[0] = own::clamp01 ((r.warpAmountPercent[0] ? *r.warpAmountPercent[0] : P (oi.warpAmount)) * 0.01f);
    t.warpPhase[0] = P (oi.warpPhase) * (1.0f / 360.0f);
    t.warp[1] = static_cast<int> (P (oi.warp2Type) + 0.5f);
    t.warpAmount[1] = own::clamp01 ((r.warpAmountPercent[1] ? *r.warpAmountPercent[1] : P (oi.warp2Amount)) * 0.01f);
    t.warpPhase[1] = P (oi.warp2Phase) * (1.0f / 360.0f);
    t.morphType = static_cast<int> (P (lk.idx[own::lk_morphType]) + 0.5f);
    t.morphAmount = own::clamp01 ((r.morphAmountPercent ? *r.morphAmountPercent : P (lk.idx[own::lk_morphAmount])) * 0.01f);
    t.morphSpread = 0.0f;
    t.smooth = P (lk.idx[own::lk_smooth]) >= 0.5f;
    const int slot = std::clamp (static_cast<int> (P (lk.idx[own::lk_remapSlot]) + 0.5f), 0, kNumRemapSlots - 1);
    if (P (lk.idx[own::lk_remapOn]) >= 0.5f && cs != nullptr && cs->remap[o][slot].size() == 257)
    {
        // the drawn remap of the frame position (the slew with rise / fall is a time effect: not shown)
        const float xx = own::clamp01 (frame) * 256.0f;
        const int ci = std::min (static_cast<int> (xx), 255);
        frame = own::clamp01 (own::lerp (cs->remap[o][slot][static_cast<size_t> (ci)], cs->remap[o][slot][static_cast<size_t> (ci) + 1], xx - static_cast<float> (ci)));
    }
    t.frame = frame;
    t.oversample = false;
    t.lowCost = false;
    t.shareMorph = false;
    t.remap = nullptr;
    if (cs != nullptr && cs->remap[o][slot].size() == 257)
        t.remap = cs->remap[o][slot].data();

    own::WtOsc& w = m.osc;
    w.clearState();
    w.prepareMorph();
    w.start (0.0f, 0.0f, m.rng);
    const int total = (kWarmCycles + 1) * kSamples + 256;
    bool first = true;
    for (int s = 0; s < total; s += own::kControlPeriod)
    {
        w.update (t, kRate, m.rng);
        w.updateMorph (kRate, first, nullptr);
        first = false;
        w.render (0, own::kControlPeriod, m.L.data() + s, m.R.data() + s, nullptr, kRate);
    }
    // the oversampled path (a warp that needs it, or a morph) delays the output by half of its filter: that many samples into the cycle
    const bool warped = own::warpNeedsOversampling (own::warpFamily (t.warp[0])) || own::warpNeedsOversampling (own::warpFamily (t.warp[1]));
    const int os = own::WtOsc::kOversample;
    const int delay = warped ? (own::WtOsc::kTaps - 1) / 2 / os + 1 : 0;
    const float* src = m.L.data() + kWarmCycles * kSamples + delay;
    std::copy (src, src + kSamples, out);
    for (int i = 0; i < kSamples; ++i)
        if (!std::isfinite (out[i]))
            out[i] = 0.0f;
    return Kind::wavetable;
}
} // namespace synthyboy
