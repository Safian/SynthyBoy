// SynthyBoy - own engine: the effect chain.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fx_chain.h"

#include <cstdarg>
#include <cstdio>

namespace synthyboy
{
namespace own
{
namespace
{
int K (const char* fmt, ...)
{
    char key[96];
    va_list ap;
    va_start (ap, fmt);
    std::vsnprintf (key, sizeof key, fmt, ap);
    va_end (ap);
    return findParamIndex (hashKey (key));
}

FxIndex build()
{
    FxIndex f {};
    for (int i = 0; i < 16; ++i)
        f.order[i] = K ("fx.order.%d", i + 1);
    const char* onKeys[16] = { "chorus.on", "compressor.on", "delay.on", "distortion.on", "eq.on", "fxfilter.on", "flanger.on", "phaser.on", "reverb.on",
                               "hyperdim.on", "utility.on", "bode.on", "convolve.on", "splitter_lh.on", "splitter_lmh.on", "splitter_ms.on" };
    const char* levelKeys[16] = { "chorus.level", "compressor.level", "delay.level", "distortion.level", "eq.level", "fxfilter.level", "flanger.level",
                                  "phaser.level", "reverb.level", "hyperdim.level", "utility.level", "bode.level", "convolve.level", "", "", "" };
    for (int i = 0; i < 16; ++i)
    {
        f.on[i] = K ("%s", onKeys[i]);
        f.level[i] = levelKeys[i][0] ? K ("%s", levelKeys[i]) : -1;
    }
    f.chMix = K ("chorus.mix"); f.chRate = K ("chorus.rate"); f.chSync = K ("chorus.sync"); f.chDivision = K ("chorus.division"); f.chDepth = K ("chorus.depth");
    f.chFeedback = K ("chorus.feedback"); f.chDelay1 = K ("chorus.delay1"); f.chDelay2 = K ("chorus.delay2"); f.chVoices = K ("chorus.voices");
    f.chCutoff = K ("chorus.filter_cutoff"); f.chSpread = K ("chorus.filter_spread");
    f.cpMix = K ("compressor.mix"); f.cpAttack = K ("compressor.attack"); f.cpRelease = K ("compressor.release"); f.cpBands = K ("compressor.bands");
    f.cpXLow = K ("compressor.crossover_low"); f.cpXHigh = K ("compressor.crossover_high");
    const char* bandName[3] = { "low", "mid", "high" };
    for (int b = 0; b < 3; ++b)
    {
        f.cpBand[b][0] = K ("compressor.%s.lower_threshold", bandName[b]); f.cpBand[b][1] = K ("compressor.%s.upper_threshold", bandName[b]);
        f.cpBand[b][2] = K ("compressor.%s.lower_ratio", bandName[b]); f.cpBand[b][3] = K ("compressor.%s.upper_ratio", bandName[b]);
        f.cpBand[b][4] = K ("compressor.%s.gain", bandName[b]);
        f.eqBand[b][0] = K ("eq.%s.mode", bandName[b]); f.eqBand[b][1] = K ("eq.%s.cutoff", bandName[b]);
        f.eqBand[b][2] = K ("eq.%s.gain", bandName[b]); f.eqBand[b][3] = K ("eq.%s.resonance", bandName[b]);
    }
    f.eqMid2[0] = K ("eq.mid2.mode"); f.eqMid2[1] = K ("eq.mid2.cutoff"); f.eqMid2[2] = K ("eq.mid2.gain"); f.eqMid2[3] = K ("eq.mid2.resonance");
    f.dlMix = K ("delay.mix"); f.dlFeedback = K ("delay.feedback"); f.dlTime = K ("delay.time"); f.dlTime2 = K ("delay.time2"); f.dlSync = K ("delay.sync");
    f.dlDivision = K ("delay.division"); f.dlDivision2 = K ("delay.division2"); f.dlStyle = K ("delay.style"); f.dlCutoff = K ("delay.filter_cutoff");
    f.dlSpread = K ("delay.filter_spread");
    f.diType = K ("distortion.type"); f.diDrive = K ("distortion.drive"); f.diMix = K ("distortion.mix"); f.diOrder = K ("distortion.filter_order");
    f.diCutoff = K ("distortion.filter_cutoff"); f.diRes = K ("distortion.filter_resonance"); f.diBlend = K ("distortion.filter_blend");
    f.flMix = K ("flanger.mix"); f.flFeedback = K ("flanger.feedback"); f.flRate = K ("flanger.rate"); f.flSync = K ("flanger.sync"); f.flDivision = K ("flanger.division");
    f.flDepth = K ("flanger.depth"); f.flCenter = K ("flanger.center"); f.flPhase = K ("flanger.phase_offset");
    f.phMix = K ("phaser.mix"); f.phFeedback = K ("phaser.feedback"); f.phRate = K ("phaser.rate"); f.phSync = K ("phaser.sync"); f.phDivision = K ("phaser.division");
    f.phDepth = K ("phaser.depth"); f.phCenter = K ("phaser.center"); f.phBlend = K ("phaser.blend"); f.phPhase = K ("phaser.phase_offset");
    f.rvMix = K ("reverb.mix"); f.rvSize = K ("reverb.size"); f.rvDecay = K ("reverb.decay"); f.rvPre = K ("reverb.predelay"); f.rvLowCut = K ("reverb.pre_low_cut");
    f.rvHighCut = K ("reverb.pre_high_cut"); f.rvHsCut = K ("reverb.high_shelf_cutoff"); f.rvHsGain = K ("reverb.high_shelf_gain");
    f.rvChAmount = K ("reverb.chorus_amount"); f.rvChRate = K ("reverb.chorus_rate"); f.rvType = K ("reverb.type"); f.rvWidth = K ("reverb.width");
    f.rvDiffA = K ("reverb.diff_a"); f.rvDiffB = K ("reverb.diff_b");
    f.hdRate = K ("hyperdim.rate"); f.hdUnison = K ("hyperdim.unison"); f.hdDetune = K ("hyperdim.detune"); f.hdMix = K ("hyperdim.mix");
    f.hdDimSize = K ("hyperdim.dim_size"); f.hdDimMix = K ("hyperdim.dim_mix"); f.hdLevel = K ("hyperdim.level");
    f.utPolL = K ("utility.polarity_l"); f.utPolR = K ("utility.polarity_r"); f.utLpf = K ("utility.lpf"); f.utHpf = K ("utility.hpf");
    f.utMonoBass = K ("utility.mono_bass"); f.utMonoFreq = K ("utility.mono_freq"); f.utWidth = K ("utility.width"); f.utPan = K ("utility.pan");
    f.utMix = K ("utility.mix"); f.utLevel = K ("utility.level");
    f.ffOn = K ("fxfilter.on"); f.ffModel = K ("fxfilter.model"); f.ffStyle = K ("fxfilter.style"); f.ffCutoff = K ("fxfilter.cutoff");
    f.ffRes = K ("fxfilter.resonance"); f.ffDrive = K ("fxfilter.drive"); f.ffBlend = K ("fxfilter.blend"); f.ffMix = K ("fxfilter.mix");
    f.ffVar = K ("fxfilter.var"); f.ffLevel = K ("fxfilter.level");
    return f;
}

constexpr float kDivisionBeats[13] = { 0.0f, 128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
} // namespace

const FxIndex& fxIndex() noexcept
{
    static const FxIndex index = build();
    return index;
}

float FxChain::G (const float* base, const float* delta, int idx) const noexcept
{
    if (idx < 0)
        return 0.0f;
    const float d = delta != nullptr ? delta[idx] : 0.0f;
    return d == 0.0f ? base[idx] : modulatedValue (paramTable()[idx], destLawOf (static_cast<size_t> (idx)), base[idx], d);
}

float FxChain::lfoRate (const float* base, const float* delta, int rateIdx, int syncIdx, int divIdx, double bpm) const noexcept
{
    const int sync = static_cast<int> (base[syncIdx] + 0.5f);
    if (sync == 0)
        return G (base, delta, rateIdx);
    const float beats = kDivisionBeats[std::clamp (static_cast<int> (base[divIdx] + 0.5f), 0, 12)];
    const float r = beats > 0.0f ? static_cast<float> (bpm / 60.0) / beats : 0.0f;
    return sync == 2 ? r / 1.5f : (sync == 3 ? r * 1.5f : r);
}

void FxChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    chorus.prepare (sr);
    compressor.prepare (sr);
    delay.prepare (sr);
    distortion.prepare (sr);
    eq.prepare (sr);
    fxFilter.prepare (sr);
    flanger.prepare (sr);
    phaser.prepare (sr);
    reverb.prepare (sr);
    hyper.prepare (sr, std::max (maxBlock, kControlPeriod));
    utility.prepare (sr, std::max (maxBlock, kControlPeriod));
    reset();
}

void FxChain::reset() noexcept
{
    chorus.reset(); compressor.reset(); delay.reset(); distortion.reset(); eq.reset(); fxFilter.reset(); flanger.reset(); phaser.reset(); reverb.reset();
    hyper.reset(); utility.reset();
    for (int i = 0; i < 16; ++i)
        levelGain[i] = levelGainPrev[i] = 1.0f;
}

void FxChain::update (const float* base, const float* delta, double bpm, uint32_t fxMask, int periodSamples) noexcept
{
    const FxIndex& f = fxIndex();
    period = periodSamples;
    active = false;
    for (int i = 0; i < 16; ++i)
    {
        order[i] = f.order[i] >= 0 ? std::clamp (static_cast<int> (base[f.order[i]] + 0.5f), 0, 15) : i;
        const uint32_t bit = 1u << order[i];
        on[order[i]] = f.on[order[i]] >= 0 && base[f.on[order[i]]] >= 0.5f && (fxMask & bit) != 0;
    }
    // the modules that exist
    static const bool implemented[16] = { true, true, true, true, true, true, true, true, true, true, true, false, false, false, false, false };
    for (int m = 0; m < 16; ++m)
    {
        on[m] = on[m] && implemented[m];
        levelGainPrev[m] = levelGain[m];
        levelGain[m] = f.level[m] >= 0 ? dbToGain (G (base, delta, f.level[m])) : 1.0f;
        active = active || on[m];
    }
    if (!active)
        return;
    auto pct = [&] (int idx) { return G (base, delta, idx) * 0.01f; };
    if (on[0])
    {
        Chorus::Params p;
        p.mix = clamp01 (pct (f.chMix));
        p.rate = lfoRate (base, delta, f.chRate, f.chSync, f.chDivision, bpm);
        p.depth = clamp01 (pct (f.chDepth));
        p.feedback = pct (f.chFeedback);
        p.delay1 = G (base, delta, f.chDelay1) * 0.001f;
        p.delay2 = G (base, delta, f.chDelay2) * 0.001f;
        p.voices = static_cast<int> (base[f.chVoices] + 0.5f);
        p.cutoff = G (base, delta, f.chCutoff);
        p.spread = clamp01 (pct (f.chSpread));
        chorus.set (p);
    }
    if (on[1])
    {
        Compressor::Params p;
        p.mix = clamp01 (pct (f.cpMix));
        p.attack = clamp01 (pct (f.cpAttack));
        p.release = clamp01 (pct (f.cpRelease));
        p.bands = static_cast<int> (base[f.cpBands] + 0.5f);
        p.crossLow = base[f.cpXLow];
        p.crossHigh = base[f.cpXHigh];
        for (int b = 0; b < 3; ++b)
        {
            p.band[b].lowerDb = base[f.cpBand[b][0]];
            p.band[b].upperDb = base[f.cpBand[b][1]];
            p.band[b].lowerRatio = base[f.cpBand[b][2]] * 0.01f;
            p.band[b].upperRatio = base[f.cpBand[b][3]] * 0.01f;
            p.band[b].gainDb = G (base, delta, f.cpBand[b][4]);
        }
        compressor.set (p);
    }
    if (on[2])
    {
        Delay::Params p;
        p.mix = clamp01 (pct (f.dlMix));
        p.feedback = pct (f.dlFeedback);
        const int sync = static_cast<int> (base[f.dlSync] + 0.5f);
        if (sync == 0)
        {
            p.timeL = G (base, delta, f.dlTime) * 0.001f;
            p.timeR = G (base, delta, f.dlTime2) * 0.001f;
        }
        else
        {
            // 9 divisions from 4 bars to 1/64 note: beats = 16 / 2^i
            const float scale = sync == 2 ? 1.5f : (sync == 3 ? 2.0f / 3.0f : 1.0f);
            const float spb = 60.0f / static_cast<float> (bpm);
            p.timeL = 16.0f / std::exp2 (std::clamp (base[f.dlDivision], 0.0f, 8.0f)) * spb * scale;
            p.timeR = 16.0f / std::exp2 (std::clamp (base[f.dlDivision2], 0.0f, 8.0f)) * spb * scale;
        }
        p.cutoff = G (base, delta, f.dlCutoff);
        p.spread = clamp01 (pct (f.dlSpread));
        p.style = static_cast<int> (base[f.dlStyle] + 0.5f);
        delay.set (p);
    }
    if (on[3])
    {
        Distortion::Params p;
        p.type = static_cast<int> (base[f.diType] + 0.5f);
        p.driveDb = G (base, delta, f.diDrive);
        p.mix = clamp01 (pct (f.diMix));
        p.order = static_cast<int> (base[f.diOrder] + 0.5f);
        p.cutoff = G (base, delta, f.diCutoff);
        p.resonance = clamp01 (pct (f.diRes));
        p.blend = clamp01 (pct (f.diBlend)) * 2.0f;
        distortion.set (p);
    }
    if (on[4])
    {
        Equalizer::Params p;
        Equalizer::Band* bands[3] = { &p.low, &p.mid, &p.high };
        for (int b = 0; b < 3; ++b)
        {
            bands[b]->mode = static_cast<int> (base[f.eqBand[b][0]] + 0.5f);
            bands[b]->hz = G (base, delta, f.eqBand[b][1]);
            bands[b]->gainDb = G (base, delta, f.eqBand[b][2]);
            bands[b]->res = clamp01 (pct (f.eqBand[b][3]));
        }
        p.mid2.mode = static_cast<int> (base[f.eqMid2[0]] + 0.5f);
        p.mid2.hz = G (base, delta, f.eqMid2[1]);
        p.mid2.gainDb = G (base, delta, f.eqMid2[2]);
        p.mid2.res = clamp01 (pct (f.eqMid2[3]));
        eq.set (p);
    }
    if (on[5])
    {
        FilterSettings s;
        s.model = static_cast<int> (base[f.ffModel] + 0.5f);
        s.style = static_cast<int> (base[f.ffStyle] + 0.5f);
        s.cutoffHz = G (base, delta, f.ffCutoff);
        s.resonance = pct (f.ffRes);
        s.drive = G (base, delta, f.ffDrive) * (1.0f / 20.0f);
        s.blend = pct (f.ffBlend);
        s.mix = 1.0f;
        s.var = pct (f.ffVar);
        fxFilter.set (s);
        fxFilterMix = clamp01 (pct (f.ffMix));
    }
    if (on[6])
    {
        Flanger::Params p;
        p.mix = clamp01 (pct (f.flMix));
        p.feedback = pct (f.flFeedback);
        p.rate = lfoRate (base, delta, f.flRate, f.flSync, f.flDivision, bpm);
        p.depth = clamp01 (pct (f.flDepth));
        p.centerHz = G (base, delta, f.flCenter);
        p.phaseOffset = G (base, delta, f.flPhase) / 360.0f;
        flanger.set (p);
    }
    if (on[7])
    {
        Phaser::Params p;
        p.mix = clamp01 (pct (f.phMix));
        p.feedback = pct (f.phFeedback);
        p.rate = lfoRate (base, delta, f.phRate, f.phSync, f.phDivision, bpm);
        p.depthSemis = G (base, delta, f.phDepth);
        p.centerHz = G (base, delta, f.phCenter);
        p.blend = clamp01 (pct (f.phBlend));
        p.phaseOffset = G (base, delta, f.phPhase) / 360.0f;
        phaser.set (p);
    }
    if (on[8])
    {
        Reverb::Params p;
        p.mix = clamp01 (pct (f.rvMix));
        p.size = clamp01 (pct (f.rvSize));
        p.decay = G (base, delta, f.rvDecay) * 0.001f;
        p.predelay = G (base, delta, f.rvPre) * 0.001f;
        p.lowCut = G (base, delta, f.rvLowCut);
        p.highCut = G (base, delta, f.rvHighCut);
        p.highShelfDb = base[f.rvHsGain];
        p.highShelfHz = G (base, delta, f.rvHsCut);
        p.chorusAmount = clamp01 (pct (f.rvChAmount));
        p.chorusRate = G (base, delta, f.rvChRate);
        p.type = static_cast<int> (base[f.rvType] + 0.5f);
        p.width = pct (f.rvWidth);
        p.diffA = clamp01 (pct (f.rvDiffA));
        p.diffB = clamp01 (pct (f.rvDiffB));
        reverb.set (p);
    }
    if (on[9])
    {
        fx::HyperDimension::Params p;
        p.enabled = true;
        p.hyperRate = G (base, delta, f.hdRate);
        p.hyperUnison = static_cast<int> (base[f.hdUnison] + 0.5f);
        p.hyperDetune = clamp01 (pct (f.hdDetune));
        p.hyperMix = clamp01 (pct (f.hdMix));
        p.dimSize = clamp01 (pct (f.hdDimSize));
        p.dimMix = clamp01 (pct (f.hdDimMix));
        p.levelDb = 0.0f; // the chain applies the level
        hyper.setParams (p);
    }
    if (on[10])
    {
        fx::Utility::Params p;
        p.enabled = true;
        p.invertL = base[f.utPolL] >= 0.5f;
        p.invertR = base[f.utPolR] >= 0.5f;
        p.lpfFreq = G (base, delta, f.utLpf);
        p.lpfEnabled = p.lpfFreq < 19990.0f;
        p.hpfFreq = G (base, delta, f.utHpf);
        p.hpfEnabled = p.hpfFreq > 20.5f;
        p.monoBassEnabled = base[f.utMonoBass] >= 0.5f;
        p.monoBassFreq = G (base, delta, f.utMonoFreq);
        p.width = pct (f.utWidth);
        p.pan = pct (f.utPan);
        p.mix = clamp01 (pct (f.utMix));
        p.levelDb = 0.0f;
        utility.setParams (p);
    }
}

void FxChain::process (float* l, float* r, int n) noexcept
{
    if (!active)
        return;
    bool done[16] = {};
    const float invP = 1.0f / static_cast<float> (period);
    (void) invP;
    for (int pos = 0; pos < 16; ++pos)
    {
        const int m = order[pos];
        if (done[m] || !on[m])
            continue;
        done[m] = true;
        switch (m)
        {
            case 0: chorus.process (l, r, n); break;
            case 1: compressor.process (l, r, n); break;
            case 2: delay.process (l, r, n); break;
            case 3: distortion.process (l, r, n); break;
            case 4: eq.process (l, r, n); break;
            case 5:
            {
                float wl[kControlPeriod], wr[kControlPeriod];
                fxFilter.process (l, r, wl, wr, n);
                for (int i = 0; i < n; ++i)
                {
                    l[i] = mixLaw (l[i], wl[i], fxFilterMix);
                    r[i] = mixLaw (r[i], wr[i], fxFilterMix);
                }
                break;
            }
            case 6: flanger.process (l, r, n); break;
            case 7: phaser.process (l, r, n); break;
            case 8: reverb.process (l, r, n); break;
            case 9: hyper.process (l, r, n); break;
            case 10: utility.process (l, r, n); break;
            default: break;
        }
        if (levelGain[m] != 1.0f || levelGainPrev[m] != 1.0f)
            for (int i = 0; i < n; ++i)
            {
                const float g = lerp (levelGainPrev[m], levelGain[m], static_cast<float> (i + 1) * invP);
                l[i] *= g;
                r[i] *= g;
            }
    }
}
} // namespace own
} // namespace synthyboy
