// SynthyBoy - own engine: the 13 effect modules as objects (key reading + DSP), shared by instance 0 of the chain and the slots of the instance pool.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <cstdarg>
#include <cstdio>

#include "fx_effects.h"
#include "fx_module.h"
#include "synthyboy/fx/hyper_dimension.h"
#include "synthyboy/fx/utility.h"
#include "bode.h"
#include "convolve.h"

namespace synthyboy
{
namespace own
{
// ---------------------------------------------------------------------------------------------------------------------------------- worker
FxWorker& FxWorker::instance()
{
    static FxWorker w;
    return w;
}

FxWorker::FxWorker() : thread ([this] { loop(); }) {}

FxWorker::~FxWorker()
{
    stop.store (true);
    cv.notify_all();
    if (thread.joinable())
        thread.join();
}

bool FxWorker::submit (Fn fn, void* ctx) noexcept
{
    for (auto& j : jobs)
    {
        int expected = 0;
        if (j.state.compare_exchange_strong (expected, 1))
        {
            j.fn = fn;
            j.ctx = ctx;
            j.state.store (2, std::memory_order_release);
            cv.notify_one();
            return true;
        }
    }
    return false;
}

void FxWorker::loop()
{
    while (!stop.load())
    {
        bool any = false;
        for (auto& j : jobs)
            if (j.state.load (std::memory_order_acquire) == 2)
            {
                j.fn (j.ctx);
                j.state.store (0, std::memory_order_release);
                any = true;
            }
        if (!any)
        {
            std::unique_lock<std::mutex> lock (mutex);
            cv.wait_for (lock, std::chrono::milliseconds (2));
        }
    }
}

namespace
{
int K (const char* key) noexcept { return findParamIndex (hashKey (key)); }

constexpr float kDivisionBeats[13] = { 0.0f, 128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };

float V (const float* base, const float* delta, int idx) noexcept { return fxValue (base, delta, idx); }
float pct (const float* base, const float* delta, int idx) noexcept { return fxValue (base, delta, idx) * 0.01f; }
int choice (const float* base, int idx) noexcept { return idx >= 0 ? static_cast<int> (base[idx] + 0.5f) : 0; }

float lfoRate (const float* base, const float* delta, int rateIdx, int syncIdx, int divIdx, double bpm) noexcept
{
    const int sync = choice (base, syncIdx);
    if (sync == 0)
        return V (base, delta, rateIdx);
    const float beats = kDivisionBeats[std::clamp (choice (base, divIdx), 0, 12)];
    const float r = beats > 0.0f ? static_cast<float> (bpm / 60.0) / beats : 0.0f;
    return sync == 2 ? r / 1.5f : (sync == 3 ? r * 1.5f : r);
}

// ---------------------------------------------------------------------------------------------------------------------------------- chorus
class ChorusModule final : public FxModule
{
public:
    ChorusModule() : FxModule (K ("chorus.level"))
    {
        mix = K ("chorus.mix"); rate = K ("chorus.rate"); sync = K ("chorus.sync"); division = K ("chorus.division"); depth = K ("chorus.depth");
        feedback = K ("chorus.feedback"); d1 = K ("chorus.delay1"); d2 = K ("chorus.delay2"); voices = K ("chorus.voices");
        cutoff = K ("chorus.filter_cutoff"); spread = K ("chorus.filter_spread");
    }
    int moduleIndex() const noexcept override { return 0; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame& f, const float* b, const float* d) noexcept override
    {
        Chorus::Params p;
        p.mix = clamp01 (pct (b, d, mix));
        p.rate = lfoRate (b, d, rate, sync, division, f.bpm);
        p.depth = clamp01 (pct (b, d, depth));
        p.feedback = pct (b, d, feedback);
        p.delay1 = V (b, d, d1) * 0.001f;
        p.delay2 = V (b, d, d2) * 0.001f;
        p.voices = choice (b, voices);
        p.cutoff = V (b, d, cutoff);
        p.spread = clamp01 (pct (b, d, spread));
        fx.set (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }

private:
    Chorus fx;
    int mix, rate, sync, division, depth, feedback, d1, d2, voices, cutoff, spread;
};

// ---------------------------------------------------------------------------------------------------------------------------------- compressor
class CompressorModule final : public FxModule
{
public:
    CompressorModule() : FxModule (K ("compressor.level"))
    {
        mix = K ("compressor.mix"); attack = K ("compressor.attack"); release = K ("compressor.release"); bands = K ("compressor.bands");
        xLow = K ("compressor.crossover_low"); xHigh = K ("compressor.crossover_high"); limit = K ("compressor.limit"); limitComp = K ("compressor.limit_latency_comp");
        static const char* names[3] = { "low", "mid", "high" };
        for (int b = 0; b < 3; ++b)
        {
            char key[64];
            const char* suffix[5] = { "lower_threshold", "upper_threshold", "lower_ratio", "upper_ratio", "gain" };
            for (int k = 0; k < 5; ++k)
            {
                std::snprintf (key, sizeof key, "compressor.%s.%s", names[b], suffix[k]);
                band[b][k] = K (key);
            }
        }
    }
    int moduleIndex() const noexcept override { return 1; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame&, const float* b, const float* d) noexcept override
    {
        Compressor::Params p;
        p.mix = clamp01 (pct (b, d, mix));
        p.attack = clamp01 (pct (b, d, attack));
        p.release = clamp01 (pct (b, d, release));
        p.bands = choice (b, bands);
        p.mode = std::clamp (choice (b, limit), 0, 2);
        p.crossLow = V (b, d, xLow);
        p.crossHigh = std::max (V (b, d, xHigh), p.crossLow * 1.25f);
        for (int i = 0; i < 3; ++i)
        {
            p.band[i].lowerDb = b[band[i][0]];
            p.band[i].upperDb = b[band[i][1]];
            p.band[i].lowerRatio = b[band[i][2]] * 0.01f;
            p.band[i].upperRatio = b[band[i][3]] * 0.01f;
            p.band[i].gainDb = V (b, d, band[i][4]);
        }
        fx.set (p);
        reported = limitComp < 0 || b[limitComp] >= 0.5f;
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }
    int latency() const noexcept override { return fx.latencySamples(); }
    bool latencyReported() const noexcept override { return reported; }

private:
    Compressor fx;
    int mix, attack, release, bands, xLow, xHigh, limit, limitComp, band[3][5];
    bool reported = true;
};

// ---------------------------------------------------------------------------------------------------------------------------------- delay
class DelayModule final : public FxModule
{
public:
    DelayModule() : FxModule (K ("delay.level"))
    {
        mix = K ("delay.mix"); feedback = K ("delay.feedback"); time = K ("delay.time"); time2 = K ("delay.time2"); sync = K ("delay.sync");
        division = K ("delay.division"); division2 = K ("delay.division2"); style = K ("delay.style"); cutoff = K ("delay.filter_cutoff");
        spread = K ("delay.filter_spread"); offset = K ("delay.offset"); offsetLeft = K ("delay.offset_left"); hq = K ("delay.hq");
    }
    int moduleIndex() const noexcept override { return 2; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame& f, const float* b, const float* d) noexcept override
    {
        Delay::Params p;
        p.mix = clamp01 (pct (b, d, mix));
        p.feedback = pct (b, d, feedback);
        const int s = choice (b, sync);
        if (s == 0)
        {
            p.timeL = V (b, d, time) * 0.001f;
            p.timeR = V (b, d, time2) * 0.001f;
        }
        else
        {
            // 9 divisions from 4 bars to 1/64 note: beats = 16 / 2^i
            const float sc = s == 2 ? 1.5f : (s == 3 ? 2.0f / 3.0f : 1.0f);
            const float spb = 60.0f / static_cast<float> (f.bpm);
            p.timeL = 16.0f / std::exp2 (std::clamp (b[division], 0.0f, 8.0f)) * spb * sc;
            p.timeR = 16.0f / std::exp2 (std::clamp (b[division2], 0.0f, 8.0f)) * spb * sc;
        }
        p.cutoff = V (b, d, cutoff);
        p.spread = clamp01 (pct (b, d, spread));
        p.style = choice (b, style);
        p.offsetR = offset >= 0 ? V (b, d, offset) : 1.0f;
        p.offsetL = offsetLeft >= 0 ? V (b, d, offsetLeft) : 1.0f;
        p.hq = hq >= 0 && b[hq] >= 0.5f;
        fx.set (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }

private:
    Delay fx;
    int mix, feedback, time, time2, sync, division, division2, style, cutoff, spread, offset, offsetLeft, hq;
};

// ---------------------------------------------------------------------------------------------------------------------------------- distortion
class DistortionModule final : public FxModule
{
public:
    DistortionModule() : FxModule (K ("distortion.level"))
    {
        type = K ("distortion.type"); drive = K ("distortion.drive"); mix = K ("distortion.mix"); order = K ("distortion.filter_order");
        cutoff = K ("distortion.filter_cutoff"); res = K ("distortion.filter_resonance"); blend = K ("distortion.filter_blend"); keytrack = K ("distortion.filter_keytrack");
    }
    int moduleIndex() const noexcept override { return 3; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame& f, const float* b, const float* d) noexcept override
    {
        Distortion::Params p;
        p.type = choice (b, type);
        p.driveDb = V (b, d, drive);
        p.mix = clamp01 (pct (b, d, mix));
        p.order = choice (b, order);
        const float kt = keytrack >= 0 ? pct (b, d, keytrack) : 0.0f;
        p.cutoff = V (b, d, cutoff) * std::exp2 (kt * (f.lastNote - 60.0f) * (1.0f / 12.0f));
        p.resonance = clamp01 (pct (b, d, res));
        p.blend = clamp01 (pct (b, d, blend)) * 2.0f;
        fx.set (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }

private:
    Distortion fx;
    int type, drive, mix, order, cutoff, res, blend, keytrack;
};

// ---------------------------------------------------------------------------------------------------------------------------------- EQ
class EqModule final : public FxModule
{
public:
    EqModule() : FxModule (K ("eq.level"))
    {
        static const char* names[3] = { "low", "mid", "high" };
        for (int b = 0; b < 3; ++b)
        {
            char key[64];
            const char* suffix[4] = { "mode", "cutoff", "gain", "resonance" };
            for (int k = 0; k < 4; ++k)
            {
                std::snprintf (key, sizeof key, "eq.%s.%s", names[b], suffix[k]);
                band[b][k] = K (key);
            }
        }
        mid2[0] = K ("eq.mid2.mode"); mid2[1] = K ("eq.mid2.cutoff"); mid2[2] = K ("eq.mid2.gain"); mid2[3] = K ("eq.mid2.resonance");
        law = K ("eq.law");
    }
    int moduleIndex() const noexcept override { return 4; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame&, const float* b, const float* d) noexcept override
    {
        Equalizer::Params p;
        Equalizer::Band* bands[3] = { &p.low, &p.mid, &p.high };
        for (int i = 0; i < 3; ++i)
        {
            bands[i]->mode = choice (b, band[i][0]);
            bands[i]->hz = V (b, d, band[i][1]);
            bands[i]->gainDb = V (b, d, band[i][2]);
            bands[i]->res = clamp01 (pct (b, d, band[i][3]));
        }
        p.mid2.mode = choice (b, mid2[0]);
        p.mid2.hz = V (b, d, mid2[1]);
        p.mid2.gainDb = V (b, d, mid2[2]);
        p.mid2.res = clamp01 (pct (b, d, mid2[3]));
        p.law = choice (b, law);
        fx.set (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n, callOffset(), callPeriod()); }

private:
    Equalizer fx;
    int band[3][4], mid2[4], law;
};

// ---------------------------------------------------------------------------------------------------------------------------------- FX filter
class FilterModule final : public FxModule
{
public:
    FilterModule() : FxModule (K ("fxfilter.level"))
    {
        model = K ("fxfilter.model"); style = K ("fxfilter.style"); cutoff = K ("fxfilter.cutoff"); res = K ("fxfilter.resonance"); drive = K ("fxfilter.drive");
        blend = K ("fxfilter.blend"); mix = K ("fxfilter.mix"); var = K ("fxfilter.var");
    }
    int moduleIndex() const noexcept override { return 5; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame&, const float* b, const float* d) noexcept override
    {
        FilterSettings s;
        s.model = choice (b, model);
        s.style = choice (b, style);
        s.cutoffHz = V (b, d, cutoff);
        s.resonance = pct (b, d, res);
        s.drive = V (b, d, drive) * (1.0f / 20.0f);
        s.blend = pct (b, d, blend);
        s.mix = 1.0f;
        s.var = pct (b, d, var);
        fx.set (s);
        wetMix = clamp01 (pct (b, d, mix));
    }
    void run (float* l, float* r, int n) noexcept override
    {
        float wl[kControlPeriod], wr[kControlPeriod];
        fx.process (l, r, wl, wr, n);
        for (int i = 0; i < n; ++i) // the filters mix linearly (document 06, 1)
        {
            l[i] = l[i] + (wl[i] - l[i]) * wetMix;
            r[i] = r[i] + (wr[i] - r[i]) * wetMix;
        }
    }

private:
    FilterUnit fx;
    float wetMix = 1.0f;
    int model, style, cutoff, res, drive, blend, mix, var;
};

// ---------------------------------------------------------------------------------------------------------------------------------- flanger
class FlangerModule final : public FxModule
{
public:
    FlangerModule() : FxModule (K ("flanger.level"))
    {
        mix = K ("flanger.mix"); feedback = K ("flanger.feedback"); rate = K ("flanger.rate"); sync = K ("flanger.sync"); division = K ("flanger.division");
        depth = K ("flanger.depth"); center = K ("flanger.center"); phase = K ("flanger.phase_offset");
    }
    int moduleIndex() const noexcept override { return 6; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame& f, const float* b, const float* d) noexcept override
    {
        Flanger::Params p;
        p.mix = clamp01 (pct (b, d, mix));
        p.feedback = pct (b, d, feedback);
        p.rate = lfoRate (b, d, rate, sync, division, f.bpm);
        p.depth = clamp01 (pct (b, d, depth));
        p.centerHz = V (b, d, center);
        p.phaseOffset = V (b, d, phase) / 360.0f;
        fx.set (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }

private:
    Flanger fx;
    int mix, feedback, rate, sync, division, depth, center, phase;
};

// ---------------------------------------------------------------------------------------------------------------------------------- phaser
class PhaserModule final : public FxModule
{
public:
    PhaserModule() : FxModule (K ("phaser.level"))
    {
        mix = K ("phaser.mix"); feedback = K ("phaser.feedback"); rate = K ("phaser.rate"); sync = K ("phaser.sync"); division = K ("phaser.division");
        depth = K ("phaser.depth"); center = K ("phaser.center"); blend = K ("phaser.blend"); phase = K ("phaser.phase_offset"); depth2 = K ("phaser.depth2");
    }
    int moduleIndex() const noexcept override { return 7; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame& f, const float* b, const float* d) noexcept override
    {
        Phaser::Params p;
        p.mix = clamp01 (pct (b, d, mix));
        p.feedback = pct (b, d, feedback);
        p.rate = lfoRate (b, d, rate, sync, division, f.bpm);
        p.depthSemis = V (b, d, depth);
        p.centerHz = V (b, d, center);
        p.blend = clamp01 (pct (b, d, blend));
        p.phaseOffset = V (b, d, phase) / 360.0f;
        p.depth2 = depth2 >= 0 ? V (b, d, depth2) : 0.0f;
        fx.set (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }

private:
    Phaser fx;
    int mix, feedback, rate, sync, division, depth, center, blend, phase, depth2;
};

// ---------------------------------------------------------------------------------------------------------------------------------- reverb
class ReverbModule final : public FxModule
{
public:
    ReverbModule() : FxModule (K ("reverb.level"))
    {
        mix = K ("reverb.mix"); size = K ("reverb.size"); decay = K ("reverb.decay"); pre = K ("reverb.predelay"); lowCut = K ("reverb.pre_low_cut");
        highCut = K ("reverb.pre_high_cut"); hsCut = K ("reverb.high_shelf_cutoff"); hsGain = K ("reverb.high_shelf_gain"); lsCut = K ("reverb.low_shelf_cutoff");
        lsGain = K ("reverb.low_shelf_gain"); chAmount = K ("reverb.chorus_amount"); chRate = K ("reverb.chorus_rate"); type = K ("reverb.type");
        width = K ("reverb.width"); diffA = K ("reverb.diff_a"); diffB = K ("reverb.diff_b"); room = K ("reverb.room"); er = K ("reverb.er_size"); feedback = K ("reverb.feedback");
    }
    int moduleIndex() const noexcept override { return 8; }
    void prepare (double sr, int) override { fx.prepare (sr); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame&, const float* b, const float* d) noexcept override
    {
        Reverb::Params p;
        p.mix = clamp01 (pct (b, d, mix));
        p.size = clamp01 (pct (b, d, size));
        p.decay = V (b, d, decay) * 0.001f;
        p.predelay = V (b, d, pre) * 0.001f;
        p.lowCut = V (b, d, lowCut);
        p.highCut = V (b, d, highCut);
        p.highShelfDb = b[hsGain];
        p.highShelfHz = V (b, d, hsCut);
        p.lowShelfDb = b[lsGain];
        p.lowShelfHz = V (b, d, lsCut);
        p.chorusAmount = clamp01 (pct (b, d, chAmount));
        p.chorusRate = V (b, d, chRate);
        p.type = choice (b, type);
        p.room = choice (b, room);
        p.width = pct (b, d, width);
        p.diffA = clamp01 (pct (b, d, diffA));
        p.diffB = clamp01 (pct (b, d, diffB));
        p.erSize = er >= 0 ? clamp01 (pct (b, d, er)) : 0.5f;
        p.feedback = feedback >= 0 ? clamp01 (pct (b, d, feedback)) : 0.5f;
        fx.set (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }

private:
    Reverb fx;
    int mix, size, decay, pre, lowCut, highCut, hsCut, hsGain, lsCut, lsGain, chAmount, chRate, type, width, diffA, diffB, room, er, feedback;
};

// ---------------------------------------------------------------------------------------------------------------------------------- Hyper / Dimension
class HyperModule final : public FxModule
{
public:
    HyperModule() : FxModule (K ("hyperdim.level"))
    {
        rate = K ("hyperdim.rate"); unison = K ("hyperdim.unison"); detune = K ("hyperdim.detune"); mix = K ("hyperdim.mix");
        dimSize = K ("hyperdim.dim_size"); dimMix = K ("hyperdim.dim_mix");
    }
    int moduleIndex() const noexcept override { return 9; }
    void prepare (double sr, int maxBlock) override { fx.prepare (sr, std::max (maxBlock, kControlPeriod)); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame&, const float* b, const float* d) noexcept override
    {
        fx::HyperDimension::Params p;
        p.enabled = true;
        p.hyperRate = V (b, d, rate);
        p.hyperUnison = choice (b, unison);
        p.hyperDetune = clamp01 (pct (b, d, detune));
        p.hyperMix = clamp01 (pct (b, d, mix));
        p.dimSize = clamp01 (pct (b, d, dimSize));
        p.dimMix = clamp01 (pct (b, d, dimMix));
        p.levelDb = 0.0f; // the module applies the level
        fx.setParams (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }

private:
    fx::HyperDimension fx;
    int rate, unison, detune, mix, dimSize, dimMix;
};

// ---------------------------------------------------------------------------------------------------------------------------------- Utility
class UtilityModule final : public FxModule
{
public:
    UtilityModule() : FxModule (K ("utility.level"))
    {
        polL = K ("utility.polarity_l"); polR = K ("utility.polarity_r"); lpf = K ("utility.lpf"); hpf = K ("utility.hpf"); monoBass = K ("utility.mono_bass");
        monoFreq = K ("utility.mono_freq"); width = K ("utility.width"); pan = K ("utility.pan"); mix = K ("utility.mix");
    }
    int moduleIndex() const noexcept override { return 10; }
    void prepare (double sr, int maxBlock) override { fx.prepare (sr, std::max (maxBlock, kControlPeriod)); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame&, const float* b, const float* d) noexcept override
    {
        fx::Utility::Params p;
        p.enabled = true;
        p.invertL = b[polL] >= 0.5f;
        p.invertR = b[polR] >= 0.5f;
        p.lpfFreq = V (b, d, lpf);
        p.lpfEnabled = p.lpfFreq < 19990.0f;
        p.hpfFreq = V (b, d, hpf);
        p.hpfEnabled = p.hpfFreq > 20.5f;
        p.monoBassEnabled = b[monoBass] >= 0.5f;
        p.monoBassFreq = V (b, d, monoFreq);
        p.width = pct (b, d, width);
        p.pan = pct (b, d, pan);
        p.mix = clamp01 (pct (b, d, mix));
        p.levelDb = 0.0f;
        fx.setParams (p);
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }

private:
    fx::Utility fx;
    int polL, polR, lpf, hpf, monoBass, monoFreq, width, pan, mix;
};

// ---------------------------------------------------------------------------------------------------------------------------------- Bode
class BodeModule final : public FxModule
{
public:
    BodeModule() : FxModule (K ("bode.level"))
    {
        mono = K ("bode.mono_input"); shift = K ("bode.shift"); range = K ("bode.range"); dir = K ("bode.dir"); width = K ("bode.width"); delay = K ("bode.delay");
        sync = K ("bode.sync"); division = K ("bode.division"); feed = K ("bode.feed"); balance = K ("bode.balance"); blur = K ("bode.blur"); mix = K ("bode.mix");
    }
    int moduleIndex() const noexcept override { return 11; }
    void prepare (double sr, int maxBlock) override { fx.prepare (sr, std::max (maxBlock, kControlPeriod)); }
    void reset() noexcept override { fx.reset(); }
    void configure (const FxFrame& f, const float* b, const float* d) noexcept override
    {
        fx::Bode::Params p;
        p.monoInput = b[mono] >= 0.5f;
        p.shift = pct (b, d, shift);
        p.range = V (b, d, range);
        p.dir = pct (b, d, dir);
        p.width = clamp01 (pct (b, d, width));
        const int s = choice (b, sync);
        p.bpm = s != 0;
        p.tempoBpm = f.bpm;
        p.delay = V (b, d, delay) * 0.001f;
        p.delayBeats = 16.0 / std::exp2 (std::clamp (b[division], 0.0f, 8.0f)) * (s == 2 ? 1.5 : (s == 3 ? 2.0 / 3.0 : 1.0));
        p.feedback = clamp01 (pct (b, d, feed)) * 0.95f;
        p.balance = std::clamp (pct (b, d, balance), -1.0f, 1.0f);
        p.blur = clamp01 (pct (b, d, blur));
        p.mix = clamp01 (pct (b, d, mix));
        p.level = 0.0f;
        fx.params = p;
    }
    void run (float* l, float* r, int n) noexcept override { fx.process (l, r, n); }
    int latency() const noexcept override { return fx.latency(); }

private:
    fx::Bode fx;
    int mono, shift, range, dir, width, delay, sync, division, feed, balance, blur, mix;
};

// ---------------------------------------------------------------------------------------------------------------------------------- Convolve
/** The impulse response convolution of document 06 (the Vital-free partitioned convolver of core/fx-serum-b). A change of the impulse or of a parameter that shapes
    it is built on the helper thread (never on the audio thread); the audio thread takes the new convolver over when it is ready, or, when `Quality::realtime` is off,
    waits for it so that an offline render does not depend on the thread timing. */
class ConvolveModule final : public FxModule
{
public:
    ConvolveModule() : FxModule (K ("convolve.level"))
    {
        ir = K ("convolve.ir"); size = K ("convolve.size"); tone = K ("convolve.tone"); phiMin = K ("convolve.phi_min"); predelay = K ("convolve.predelay");
        sync = K ("convolve.sync"); division = K ("convolve.division"); attack = K ("convolve.attack"); decay = K ("convolve.decay"); damp = K ("convolve.damp");
        irGain = K ("convolve.ir_gain"); mix = K ("convolve.mix"); law = K ("convolve.law");
    }
    ~ConvolveModule() override
    {
        while (jobBusy.load (std::memory_order_acquire) != 0)
            std::this_thread::yield();
        delete ready.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete retiredLate.exchange (nullptr);
        delete active;
    }
    int moduleIndex() const noexcept override { return 12; }
    void prepare (double sr, int maxBlock) override
    {
        while (jobBusy.load (std::memory_order_acquire) != 0)
            std::this_thread::yield();
        sampleRate = sr;
        block = std::max (maxBlock, kControlPeriod);
        delete ready.exchange (nullptr);
        delete active;
        active = nullptr;
        builtKey = Key();
        wantKey = Key();
        FxWorker::instance(); // starts the thread here, not in process ()
    }
    void reset() noexcept override
    {
        if (active != nullptr)
            active->reset();
    }
    void configure (const FxFrame& f, const float* b, const float* d) noexcept override
    {
        const bool haveIr = ir >= 0 && b[ir] >= 0.5f && f.content != nullptr && f.content->impulse != nullptr;
        present = haveIr;
        if (!haveIr)
            return;
        const bool serum = choice (b, law) == 1;
        Key k;
        k.impulse = f.content->impulse.get();
        k.serum = serum;
        k.phiMin = phiMin >= 0 && b[phiMin] >= 0.5f;
        const float sizeKnob = clamp01 (V (b, d, size) * 0.01f);
        k.size = serum ? std::pow (10.0f, 0.7f * (sizeKnob * 100.0f - 50.0f) / 50.0f) : 0.25f * std::pow (16.0f, sizeKnob);
        k.attack = V (b, d, attack) * 0.001f;
        const float decayKnob = clamp01 (V (b, d, decay) * 0.01f);
        if (serum)
            k.decay = decayKnob >= 0.9999f ? 0.0f : decayKnob * 40.0f;
        else
            k.decay = decayKnob >= 0.9999f ? 0.0f : std::max (0.01f, decayKnob * static_cast<float> (f.content->impulse->frames / f.content->impulse->rate) * k.size);
        k.damp = clamp01 (pct (b, d, damp));
        k.irGain = V (b, d, irGain);
        // the keys that shape the impulse are quantised a little so that a smooth modulation does not rebuild at every period
        k.size = std::round (k.size * 200.0f) / 200.0f;
        k.attack = std::round (k.attack * 1000.0f) / 1000.0f;
        k.decay = std::round (k.decay * 100.0f) / 100.0f;
        k.damp = std::round (k.damp * 100.0f) / 100.0f;
        k.irGain = std::round (k.irGain * 10.0f) / 10.0f;
        wantKey = k;
        // live parameters
        live.tone = std::clamp (pct (b, d, tone), -1.0f, 1.0f);
        live.mix = clamp01 (pct (b, d, mix));
        const int s = choice (b, sync);
        live.bpm = s != 0;
        live.tempoBpm = f.bpm;
        live.preDelay = V (b, d, predelay) * 0.001f;
        live.preDelayBeats = 16.0 / std::exp2 (std::clamp (b[division], 0.0f, 8.0f)) * (s == 2 ? 1.5 : (s == 3 ? 2.0 / 3.0 : 1.0));
        takeOver (f);
        if (!(wantKey == builtKey) && !(wantKey == pendingKey) && jobBusy.load (std::memory_order_acquire) == 0)
            startBuild (f, wantKey);
        if (active != nullptr)
        {
            active->params.tone = live.tone;
            active->params.mix = 1.0f; // the module mixes itself (equal power)
            active->params.bpm = live.bpm;
            active->params.tempoBpm = live.tempoBpm;
            active->params.preDelay = live.preDelay;
            active->params.preDelayBeats = live.preDelayBeats;
            active->params.level = 0.0f;
        }
        if (!f.realtime)
        {
            // offline: wait for the build so that the output never depends on the thread timing
            for (int guard = 0; guard < 200000 && jobBusy.load (std::memory_order_acquire) != 0; ++guard)
                std::this_thread::yield();
            takeOver (f);
        }
    }
    void run (float* l, float* r, int n) noexcept override
    {
        if (!present || active == nullptr)
            return;
        float wl[kControlPeriod], wr[kControlPeriod];
        std::copy (l, l + n, wl);
        std::copy (r, r + n, wr);
        active->process (wl, wr, n);
        for (int i = 0; i < n; ++i)
        {
            l[i] = mixLaw (l[i], wl[i], live.mix);
            r[i] = mixLaw (r[i], wr[i], live.mix);
        }
    }

private:
    struct Key
    {
        const ContentSet::Impulse* impulse = nullptr;
        bool serum = false, phiMin = false;
        float size = 1.0f, attack = 0.0f, decay = 0.0f, damp = 0.0f, irGain = 0.0f;
        bool operator== (const Key& o) const noexcept
        {
            return impulse == o.impulse && serum == o.serum && phiMin == o.phiMin && size == o.size && attack == o.attack && decay == o.decay && damp == o.damp && irGain == o.irGain;
        }
    };
    struct Job
    {
        ConvolveModule* self = nullptr;
        std::shared_ptr<const ContentSet::Impulse> impulse;
        Key key;
    };

    void takeOver (const FxFrame&) noexcept
    {
        fx::Convolve* fresh = ready.exchange (nullptr, std::memory_order_acq_rel);
        if (fresh == nullptr)
            return;
        fx::Convolve* old = active;
        active = fresh;
        builtKey = pendingKey;
        // the old convolver is freed by the next build job (on the helper thread)
        fx::Convolve* prev = retired.exchange (old, std::memory_order_acq_rel);
        if (prev != nullptr)
            retiredLate.store (prev, std::memory_order_release);
    }
    void startBuild (const FxFrame& f, const Key& k) noexcept
    {
        job.self = this;
        job.impulse = f.content->impulse;
        job.key = k;
        pendingKey = k;
        jobBusy.store (1, std::memory_order_release);
        if (!FxWorker::instance().submit (&ConvolveModule::runJob, &job))
            jobBusy.store (0, std::memory_order_release);
    }
    static void runJob (void* ctx)
    {
        Job* j = static_cast<Job*> (ctx);
        ConvolveModule* m = j->self;
        auto* c = new fx::Convolve();
        c->prepare (m->sampleRate, m->block);
        fx::Convolve::Params p;
        p.size = j->key.size;
        p.minimumPhase = j->key.phiMin;
        p.attack = j->key.attack;
        p.decay = j->key.decay;
        p.damp = j->key.damp;
        p.irGain = j->key.irGain;
        p.mix = 1.0f;
        p.serumLaw = j->key.serum;
        p.hostRate = m->sampleRate;
        c->params = p;
        c->loadIr (j->impulse->data.data(), j->impulse->frames, 2, j->impulse->rate);
        c->params.tone = m->live.tone;
        c->params.mix = 1.0f;
        delete m->retired.exchange (nullptr);
        delete m->retiredLate.exchange (nullptr);
        delete m->ready.exchange (c, std::memory_order_acq_rel);
        j->impulse.reset();
        m->jobBusy.store (0, std::memory_order_release);
    }

    int ir, size, tone, phiMin, predelay, sync, division, attack, decay, damp, irGain, mix, law;
    double sampleRate = 48000.0;
    int block = 512;
    bool present = false;
    fx::Convolve* active = nullptr;
    std::atomic<fx::Convolve*> ready { nullptr }, retired { nullptr }, retiredLate { nullptr };
    std::atomic<int> jobBusy { 0 };
    Job job;
    Key wantKey, builtKey, pendingKey;
    struct { float tone = 0.0f, mix = 0.3f, preDelay = 0.0f; bool bpm = false; double tempoBpm = 120.0, preDelayBeats = 0.25; } live;
};
} // namespace

int fxModuleKeyIndex (const char* key) noexcept { return K (key); }

std::unique_ptr<FxModule> makeFxModule (int module)
{
    switch (module)
    {
        case 0: return std::make_unique<ChorusModule>();
        case 1: return std::make_unique<CompressorModule>();
        case 2: return std::make_unique<DelayModule>();
        case 3: return std::make_unique<DistortionModule>();
        case 4: return std::make_unique<EqModule>();
        case 5: return std::make_unique<FilterModule>();
        case 6: return std::make_unique<FlangerModule>();
        case 7: return std::make_unique<PhaserModule>();
        case 8: return std::make_unique<ReverbModule>();
        case 9: return std::make_unique<HyperModule>();
        case 10: return std::make_unique<UtilityModule>();
        case 11: return std::make_unique<BodeModule>();
        case 12: return std::make_unique<ConvolveModule>();
        default: return nullptr;
    }
}
} // namespace own
} // namespace synthyboy
