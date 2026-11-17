// SynthyBoy - core: waveform cycle, sample overview, magnitude spectrum and display data of the own engine (see osc_cycle.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/osc_cycle.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/multisample/multisample.h"
#include "synthyboy/params.h"
#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/sampleosc/sample_types.h"
#include "synthyboy/spectral/spectral.h"
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
constexpr int kMaxDisplayPoints = 4096;

float divisionBeats (int idx) noexcept
{
    idx = std::clamp (idx, 0, 23);
    const float whole = 4.0f / static_cast<float> (1 << (idx / 3));
    const int m = idx % 3;
    return whole * (m == 1 ? 2.0f / 3.0f : (m == 2 ? 1.5f : 1.0f));
}

void computeSampleOverview (const sampleosc::SampleView& view, int numPoints, float* outMin, float* outMax) noexcept
{
    const int64_t numFrames = view.numFrames;
    const float* buf = view.channelData != nullptr ? view.channelData[0] : nullptr;
    if (numFrames <= 0 || buf == nullptr || numPoints < 2)
        return;

    for (int px = 0; px < numPoints; ++px)
    {
        const int64_t f0 = (static_cast<int64_t> (px) * numFrames) / numPoints;
        int64_t f1 = (static_cast<int64_t> (px + 1) * numFrames) / numPoints;
        if (f1 <= f0)
            f1 = f0 + 1;
        f1 = std::min (f1, numFrames);

        float minV = buf[f0];
        float maxV = minV;
        for (int64_t i = f0 + 1; i < f1; ++i)
        {
            const float s = buf[i];
            if (s < minV) minV = s;
            if (s > maxV) maxV = s;
        }
        if (!std::isfinite (minV)) minV = 0.0f;
        if (!std::isfinite (maxV)) maxV = 0.0f;

        if (outMax != nullptr)
        {
            outMin[px] = minV;
            outMax[px] = maxV;
        }
        else
        {
            outMin[2 * px] = minV;
            outMin[2 * px + 1] = maxV;
        }
    }
}
} // namespace

struct OscCycle::Impl
{
    Impl()
    {
        osc.prepareMorph();
        L.assign (static_cast<size_t> ((kWarmCycles + 1) * kMaxDisplayPoints + 256), 0.0f);
        R = L;
        specBins.assign (spectral::kBins, 0.0f);
        specFilterGain.assign (spectral::kBins, 1.0f);
        fltX.assign (257, 0.0f);
        fltY.assign (257, 0.0f);
    }

    own::WtOsc osc;
    own::Rng rng;
    std::unique_ptr<own::ContentSet> set;
    std::vector<float> L, R;

    spectral::Scratch specScratch;
    std::vector<float> specBins;
    std::vector<float> specFilterGain;
    std::vector<float> fltX, fltY;
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

    Result res = computeDisplay (values, r, out, nullptr, kSamples);
    return res.kind;
}

OscCycle::Result OscCycle::compute (const ParamValues& values, const Request& request,
                                    float* outMin, float* outMax, int numPoints) noexcept
{
    return computeDisplay (values, request, outMin, outMax, numPoints);
}

OscCycle::Result OscCycle::computeDisplay (const ParamValues& values, const Request& r,
                                          float* outMin, float* outMax, int numPoints) noexcept
{
    Result res;
    if (outMin == nullptr || numPoints < 2)
    {
        res.kind = Kind::other;
        return res;
    }

    numPoints = std::min (numPoints, kMaxDisplayPoints);
    std::fill (outMin, outMin + numPoints, 0.0f);
    if (outMax != nullptr)
        std::fill (outMax, outMax + numPoints, 0.0f);

    const ParamInfo* table = paramTable();
    auto P = [&] (int i)
    {
        if (i < 0)
            return 0.0f;
        const float x = values.plain[i];
        return std::isfinite (x) ? std::clamp (x, table[i].min, table[i].max) : table[i].def;
    };

    Impl& m = *impl;
    const own::ContentSet* cs = m.set.get();

    // Noise source (osc 4)
    if (r.osc == 4)
    {
        const own::NoiseIdx& ni = own::paramIndex().noise;
        if (P (ni.on) < 0.5f)
        {
            res.kind = Kind::off;
            return res;
        }
        const int noiseType = std::clamp (static_cast<int> (P (ni.type) + 0.5f), 0, 1);
        const int noiseColour = std::clamp (static_cast<int> (P (ni.sample) + 0.5f), 0, 8);
        res.isNoise = true;
        res.noiseType = noiseType;
        res.noiseColour = noiseColour;

        if (noiseType == 1) // Sample
        {
            const own::ContentSet::Sample* smp = cs != nullptr ? &cs->sample[3] : nullptr;
            if (smp == nullptr || !smp->valid || !smp->view.isValid())
            {
                res.kind = Kind::noSample;
                return res;
            }
            computeSampleOverview (smp->view, numPoints, outMin, outMax);
            res.start = 0.0f;
            res.end = 1.0f;
            res.loopStart = 0.0f;
            res.loopEnd = 1.0f;
            res.loopOn = P (ni.loop) >= 0.5f;
            res.kind = Kind::sample;
            return res;
        }
        else // Generator ("Noise")
        {
            res.kind = Kind::noise;
            return res;
        }
    }

    if (r.osc < 1 || r.osc > 3)
    {
        res.kind = Kind::other;
        return res;
    }

    const int o = r.osc - 1;
    const own::OscIdx& oi = own::paramIndex().osc[o];
    const own::LibKeys& lk = own::libKeys (o);

    if (P (oi.on) < 0.5f)
    {
        res.kind = Kind::off;
        return res;
    }

    const int oscType = static_cast<int> (P (oi.type) + 0.5f);

    // 1. Wavetable oscillator
    if (oscType == kOscTypeWavetable)
    {
        own::OscTick t;
        t.active = true;
        int wtChoice = std::clamp (static_cast<int> (P (oi.wavetable) + 0.5f), 0, kNumWavetableChoices - 1);
        t.wt = wtChoice == kWavetablePresetTable ? (cs != nullptr ? cs->userTable[o].get() : nullptr) : own::builtinWavetable (wtChoice);
        if (t.wt == nullptr)
            t.wt = own::builtinWavetable (kWavetableSaw);
        const double f0 = kRate / static_cast<double> (numPoints);
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
            const float xx = own::clamp01 (frame) * 256.0f;
            const int ci = std::min (static_cast<int> (xx), 255);
            frame = own::clamp01 (own::lerp (cs->remap[o][slot][static_cast<size_t> (ci)], cs->remap[o][slot][static_cast<size_t> (ci) + 1], xx - static_cast<float> (ci)));
        }
        t.frame = frame;
        t.oversample = false;
        t.lowWarp = false;
        t.lowMorph = false;
        t.fastMath = false;
        t.scalarWarp = false;
        t.shareMorph = false;
        t.remap = nullptr;
        if (cs != nullptr && cs->remap[o][slot].size() == 257)
            t.remap = cs->remap[o][slot].data();

        own::WtOsc& w = m.osc;
        w.clearState();
        w.prepareMorph();
        w.start (0.0f, 0.0f, m.rng);
        const int total = (kWarmCycles + 1) * numPoints + 256;
        bool first = true;
        for (int s = 0; s < total; s += own::kControlPeriod)
        {
            w.update (t, kRate, m.rng);
            w.updateMorph (kRate, first, nullptr);
            first = false;
            w.render (0, own::kControlPeriod, m.L.data() + s, m.R.data() + s, nullptr, kRate);
        }
        const bool warped = own::warpNeedsOversampling (own::warpFamily (t.warp[0])) || own::warpNeedsOversampling (own::warpFamily (t.warp[1]));
        const int os = own::WtOsc::kOversample;
        const int delay = warped ? (own::WtOsc::kTaps - 1) / 2 / os + 1 : 0;
        const float* src = m.L.data() + kWarmCycles * numPoints + delay;
        std::copy (src, src + numPoints, outMin);
        for (int i = 0; i < numPoints; ++i)
            if (!std::isfinite (outMin[i]))
                outMin[i] = 0.0f;
        if (outMax != nullptr)
            std::copy (outMin, outMin + numPoints, outMax);
        res.kind = Kind::wavetable;
        return res;
    }

    // Common sample region and loop parameters for sample and granular
    const float start = own::clamp01 (P (lk.idx[own::lk_sStart]) * 0.01f);
    const float end = own::clamp01 (P (lk.idx[own::lk_sEnd]) * 0.01f);
    float loopLo = own::clamp01 (P (lk.idx[own::lk_sLoopStart]) * 0.01f);
    float loopHi = own::clamp01 (P (lk.idx[own::lk_sLoopEnd]) * 0.01f);
    if (P (lk.idx[own::lk_sLoopRelative]) >= 0.5f)
    {
        loopLo = start + loopLo * (end - start);
        loopHi = start + loopHi * (end - start);
    }
    const int lm = static_cast<int> (P (lk.idx[own::lk_sLoopMode]) + 0.5f);
    const bool loopOn = lm != 0;

    // 2. Sample oscillator
    if (oscType == kOscTypeSample)
    {
        const own::ContentSet::Sample* smp = cs != nullptr ? &cs->sample[o] : nullptr;
        if (smp == nullptr || !smp->valid || !smp->view.isValid())
        {
            res.kind = Kind::noSample;
            return res;
        }
        computeSampleOverview (smp->view, numPoints, outMin, outMax);
        res.start = start;
        res.end = end;
        res.loopStart = loopLo;
        res.loopEnd = loopHi;
        res.loopOn = loopOn;
        res.kind = Kind::sample;
        return res;
    }

    // 3. Granular oscillator
    if (oscType == kOscTypeGranular)
    {
        const own::ContentSet::Sample* smp = cs != nullptr ? &cs->sample[o] : nullptr;
        if (smp == nullptr || !smp->valid || !smp->view.isValid())
        {
            res.kind = Kind::noSample;
            return res;
        }
        computeSampleOverview (smp->view, numPoints, outMin, outMax);
        res.start = start;
        res.end = end;
        res.loopStart = loopLo;
        res.loopEnd = loopHi;
        res.loopOn = loopOn;

        const bool manual = P (lk.idx[own::lk_gManual]) >= 0.5f;
        const float posRel = own::clamp01 (P (lk.idx[own::lk_gPosition]) * 0.01f);
        res.grainPosition = manual ? (start + posRel * (end - start)) : start;

        const int lmod = static_cast<int> (P (lk.idx[own::lk_gLengthMode]) + 0.5f);
        float lengthSec = std::max (P (lk.idx[own::lk_gLength]), 1.0f) * 0.001f;
        const double bpm = r.bpm ? *r.bpm : 120.0;
        if (lmod == 1) // BPM Sync
            lengthSec = divisionBeats (static_cast<int> (P (lk.idx[own::lk_gLengthSync]) + 0.5f)) * 60.0f / std::max (static_cast<float> (bpm), 1.0f);
        else if (lmod == 2) // Percent
        {
            const int dm = static_cast<int> (P (lk.idx[own::lk_gDensityMode]) + 0.5f);
            float density = std::clamp (P (lk.idx[own::lk_gDensity]), 0.1f, 1000.0f);
            if (dm == 1)
                density = static_cast<float> (bpm) / 60.0f / divisionBeats (static_cast<int> (P (lk.idx[own::lk_gDensitySync]) + 0.5f));
            else if (dm == 2)
                density = std::max (1.0f, P (lk.idx[own::lk_gDensityGrains])) / std::max (lengthSec, 1.0e-3f);
            density = std::clamp (density, 0.1f, 1000.0f);
            lengthSec = std::max (P (lk.idx[own::lk_gLengthPercent]), 5.0f) * 0.01f / density;
        }
        lengthSec = std::clamp (lengthSec, 0.001f, 8.0f);
        const double fileSec = smp->view.sampleRate > 0.0 ? static_cast<double> (smp->view.numFrames) / smp->view.sampleRate : 1.0;
        res.grainSize = static_cast<float> (lengthSec / fileSec);

        res.kind = Kind::granular;
        return res;
    }

    // 4. Multisample oscillator
    if (oscType == kOscTypeMultisample)
    {
        if (cs == nullptr || cs->instrument[o] == nullptr || cs->instrument[o]->numPlayable() == 0)
        {
            res.kind = Kind::noSample;
            return res;
        }
        const multisample::Instrument& inst = *cs->instrument[o];
        const int note = r.note;
        const int key = multisample::Instrument::timbreKey (note, std::clamp (P (lk.idx[own::lk_mTimbre]) * 0.01f, -1.0f, 1.0f));
        int zoneIdx = -1;
        const int numSelected = inst.selectZones (key, 64, 0.0f, 0, inst.switchDefault(), &zoneIdx, 1);
        if (numSelected <= 0 || zoneIdx < 0)
        {
            res.kind = Kind::noSample;
            return res;
        }
        const multisample::Instrument::Zone& z = inst.zone (zoneIdx);
        if (z.sample < 0)
        {
            res.kind = Kind::noSample;
            return res;
        }
        const sampleosc::SampleView* view = inst.view (z.sample);
        if (view == nullptr || !view->isValid())
        {
            res.kind = Kind::noSample;
            return res;
        }
        computeSampleOverview (*view, numPoints, outMin, outMax);
        res.start = z.startFraction;
        res.end = z.endFraction;
        res.loopStart = z.loopStartFraction;
        res.loopEnd = z.loopEndFraction;
        res.loopOn = z.loopType != 0;
        res.kind = Kind::multisample;
        return res;
    }

    // 5. Spectral oscillator
    if (oscType == kOscTypeSpectral)
    {
        const own::ContentSet::Sample* smp = cs != nullptr ? &cs->sample[o] : nullptr;
        if (smp == nullptr || !smp->valid || !smp->analysis.valid())
        {
            res.kind = Kind::noSample;
            return res;
        }

        const bool manual = P (lk.idx[own::lk_pManual]) >= 0.5f;
        const float posRel = own::clamp01 (P (lk.idx[own::lk_pPosition]) * 0.01f);
        const float frac = manual ? (start + posRel * (end - start)) : start;
        const double frame = frac * static_cast<double> (std::max (smp->analysis.numFrames() - 1, 0));

        spectral::SpectralParams sp;
        sp.hostRate = kRate;
        sp.regionLo = start;
        sp.regionHi = end;
        sp.freqLoHz = std::clamp (P (lk.idx[own::lk_pFreqLo]), 20.0f, 20000.0f);
        sp.freqHiHz = std::clamp (P (lk.idx[own::lk_pFreqHi]), 20.0f, 20000.0f);
        sp.bandSmooth = P (lk.idx[own::lk_pLohiSmooth]) >= 0.5f;
        sp.bandPostWarp = P (lk.idx[own::lk_pLohiPost]) >= 0.5f;
        sp.freqShiftHz = P (lk.idx[own::lk_pFreqShift]);

        const float transposeTune = P (oi.transpose) + P (oi.tune) * 0.01f;
        const float noteSemis = static_cast<float> (r.note - 60) + transposeTune;
        sp.pitchRatio = own::semitonesToRatio (noteSemis);

        for (int k = 0; k < 2; ++k)
        {
            const int wt = k == 0 ? static_cast<int> (P (oi.warpType) + 0.5f) : static_cast<int> (P (oi.warp2Type) + 0.5f);
            sp.warpType[k] = wt >= kFirstSpectralWarp && wt <= (kFirstSpectralWarp + spectral::kNumSpectralWarps - 1)
                                 ? wt - kFirstSpectralWarp
                                 : -1;
            sp.warpAmount[k] = own::clamp01 ((k == 0 ? (r.warpAmountPercent[0] ? *r.warpAmountPercent[0] : P (oi.warpAmount))
                                                     : (r.warpAmountPercent[1] ? *r.warpAmountPercent[1] : P (oi.warp2Amount))) * 0.01f);
            sp.warpVar[k] = own::clamp01 ((k == 0 ? P (oi.warpPhase) : P (oi.warp2Phase)) * (1.0f / 360.0f));
        }

        // Spectral filter
        const float cut = std::clamp (P (lk.idx[own::lk_pCut]) * 0.01f, -1.0f, 1.0f);
        const float mix = own::clamp01 (P (lk.idx[own::lk_pMix]) * 0.01f);
        const own::Wavetable* fwt = nullptr;
        if (P (lk.idx[own::lk_fltUse]) >= 0.5f)
        {
            const int fltTableChoice = std::clamp (static_cast<int> (P (lk.idx[own::lk_fltTable]) + 0.5f), 0, kNumWavetableChoices - 1);
            fwt = fltTableChoice == kWavetablePresetTable ? (cs != nullptr ? cs->userTable[o].get() : nullptr) : own::builtinWavetable (fltTableChoice);
        }
        if (fwt != nullptr)
        {
            const float pos = std::round (own::clamp01 (P (lk.idx[own::lk_fltFrame]) * 0.01f) * static_cast<float> (std::max (fwt->numFrames() - 1, 0)) * 256.0f) / 256.0f;
            const int f0 = std::min (static_cast<int> (pos), std::max (fwt->numFrames() - 2, 0));
            const float u = fwt->numFrames() > 1 ? pos - static_cast<float> (f0) : 0.0f;
            const float* a = fwt->cycle (f0, 0);
            const float* b = u > 0.0f ? fwt->cycle (f0 + 1, 0) : nullptr;
            for (int j = 0; j < 257; ++j)
            {
                float sum = 0.0f;
                for (int k = -4; k < 4; ++k)
                {
                    const int idx = (j * 8 + k + own::Wavetable::kCycle) % own::Wavetable::kCycle;
                    sum += b != nullptr ? own::lerp (a[idx], b[idx], u) : a[idx];
                }
                m.fltX[static_cast<size_t> (j)] = static_cast<float> (j) / 256.0f;
                m.fltY[static_cast<size_t> (j)] = own::clamp01 ((sum * 0.125f + 1.0f) * 0.5f);
            }
            spectral::buildFilterGains (m.specFilterGain.data(), m.fltX.data(), m.fltY.data(), 257, cut, mix, smp->analysis.fileRate() / spectral::kFftSize);
            sp.filterGain = m.specFilterGain.data();
        }
        else if (cs != nullptr && !cs->specFilterX[o].empty() && cs->specFilterX[o].size() == cs->specFilterY[o].size())
        {
            spectral::buildFilterGains (m.specFilterGain.data(), cs->specFilterX[o].data(), cs->specFilterY[o].data(),
                                        static_cast<int> (cs->specFilterX[o].size()), cut, mix, smp->analysis.fileRate() / spectral::kFftSize);
            sp.filterGain = m.specFilterGain.data();
        }

        spectral::computeMagnitudeSpectrum (smp->analysis, sp, frame, m.specBins.data(), m.specScratch);

        const double nyq = 0.5 * kRate;
        const double hostBinHz = kRate / static_cast<double> (spectral::kFftSize);
        const double logMin = std::log10 (20.0);
        const double logMax = std::log10 (20000.0);
        const double logSpan = logMax - logMin;

        for (int i = 0; i < numPoints; ++i)
        {
            const double fracLog = static_cast<double> (i) / static_cast<double> (std::max (numPoints - 1, 1));
            const double f = std::pow (10.0, logMin + fracLog * logSpan);
            const double fc = std::min (f, nyq);

            const double fracLo = (static_cast<double> (i) - 0.5) / static_cast<double> (std::max (numPoints - 1, 1));
            const double fracHi = (static_cast<double> (i) + 0.5) / static_cast<double> (std::max (numPoints - 1, 1));
            const double fLo = std::pow (10.0, logMin + fracLo * logSpan);
            const double fHi = std::pow (10.0, logMin + fracHi * logSpan);

            int kLo = std::max (1, static_cast<int> (fLo / hostBinHz));
            int kHi = std::min (spectral::kBins - 1, static_cast<int> (std::ceil (fHi / hostBinHz)));

            float mag = 0.0f;
            for (int k = kLo; k <= kHi; ++k)
                mag = std::max (mag, m.specBins[static_cast<size_t> (k)]);

            if (kLo > kHi || mag == 0.0f)
            {
                const double bin = fc / hostBinHz;
                const int k = std::min (static_cast<int> (bin), spectral::kBins - 2);
                const float u = static_cast<float> (bin - k);
                mag = (1.0f - u) * m.specBins[static_cast<size_t> (k)] + u * m.specBins[static_cast<size_t> (k + 1)];
            }

            const float db = 20.0f * std::log10 (std::max (mag, 1.0e-6f));
            outMin[i] = std::clamp (db, -120.0f, 60.0f);
        }

        if (outMax != nullptr)
            std::copy (outMin, outMin + numPoints, outMax);

        res.kind = Kind::spectral;
        return res;
    }

    res.kind = Kind::other;
    return res;
}

// ---- OscDisplay sibling class implementation ---------------------------------------------------------------------------------

OscDisplay::OscDisplay() : cycle_ (std::make_unique<OscCycle>()) {}
OscDisplay::~OscDisplay() = default;

void OscDisplay::setContent (const StateContent& content, const IContentStore* store)
{
    cycle_->setContent (content, store);
}

void OscDisplay::readoutIndices (int osc, int out[4]) noexcept
{
    OscCycle::readoutIndices (osc, out);
}

OscDisplay::Result OscDisplay::compute (const ParamValues& values, const Request& request,
                                       float* outMin, float* outMax, int numPoints) noexcept
{
    return cycle_->computeDisplay (values, request, outMin, outMax, numPoints);
}

} // namespace synthyboy
