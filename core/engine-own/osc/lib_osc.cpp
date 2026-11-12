// SynthyBoy - own engine: the sample, granular, spectral and multisample oscillator types of one oscillator slot of a voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lib_osc.h"

#include <cstdio>

#include "synthyboy/hash.h"
#include "synthyboy/params.h"

namespace synthyboy
{
namespace own
{
namespace
{
constexpr float kSpectralGain = 1.9498f; // calibrated on vector S4 (-2.90 dB): the library voice is 8.7 dB below the sample level, the vector wants -2.90 (the +14.6 dB of the spec is relative to Vital's own wrapper)
constexpr float kGranularGain = 1.0f; // S4: -1.52 dB measured against the vector -1.49 dB
constexpr int kFirstSpectralWarp = 76, kLastSpectralWarp = 98;

float shapeFn (int type, float x, float amount)
{
    return warpSample (type, x, amount, 0.0f);
}

bool isPointwiseWarp (int t) noexcept
{
    return t == kWarpQuantize || (t >= kWarpDistTube && t <= kWarpDistSoftSat);
}

float divisionBeats (int idx) noexcept
{
    idx = std::clamp (idx, 0, 23);
    const float whole = 4.0f / static_cast<float> (1 << (idx / 3));
    const int m = idx % 3;
    return whole * (m == 1 ? 2.0f / 3.0f : (m == 2 ? 1.5f : 1.0f));
}

void panLaw (float p, float& l, float& r) noexcept
{
    p = std::clamp (p, -1.0f, 1.0f);
    l = 1.41421356f * std::sin (kPif * 0.25f * (1.0f - p));
    r = 1.41421356f * std::sin (kPif * 0.25f * (1.0f + p));
}
} // namespace

const LibKeys& libKeys (int osc) noexcept
{
    static const struct All
    {
        LibKeys k[3];
        All()
        {
            static const char* const names[kLibKeyCount] = {
#define X(name, str) str,
                SB_LIB_KEYS (X)
#undef X
            };
            for (int o = 0; o < 3; ++o)
                for (int i = 0; i < kLibKeyCount; ++i)
                {
                    char key[96];
                    std::snprintf (key, sizeof key, "osc%d.%s", o + 1, names[i]);
                    k[o].idx[i] = findParamIndex (hashKey (key));
                }
        }
    } all;
    return all.k[std::clamp (osc, 0, 2)];
}

LibOsc::LibOsc() = default;
LibOsc::~LibOsc() = default;
LibOsc::LibOsc (LibOsc&&) noexcept = default;
LibOsc& LibOsc::operator= (LibOsc&&) noexcept = default;

void LibOsc::prepare (double sampleRate)
{
    sr = sampleRate;
    sampleVoices.assign (kMaxU, sampleosc::SampleVoice());
    grainVoices.assign (kMaxU, granular::GranularVoice());
    specVoices.assign (kSpecU, spectral::SpectralVoice());
    layers.clear();
    layers.resize (static_cast<size_t> (kSlots * kMaxU));
    for (auto& v : sampleVoices)
        v.prepare (sampleRate, 64);
    for (auto& v : grainVoices)
        v.prepare (sampleRate);
    for (auto& v : specVoices)
        v.prepare (sampleRate);
    for (auto& l : layers)
        l.voice.prepare (sampleRate);
    filterGain.assign (spectral::kBins, 1.0f);
    for (auto* b : { &tmpL, &tmpR, &mixL, &mixR, &modBuf, &rateBuf })
        b->assign (static_cast<size_t> (kControlPeriod), 0.0f);
    fadeTotal = std::max (1, static_cast<int> (0.010 * sampleRate));
}

void LibOsc::stopAll() noexcept
{
    for (auto& v : sampleVoices)
        v.reset();
    for (auto& v : grainVoices)
        v.reset();
    for (auto& v : specVoices)
        v.reset();
    for (auto& l : layers)
        l.voice.reset();
    for (auto& s : noteStarted)
        s = true; // a stopped player is not restarted before the next note
    from = -1;
}

void LibOsc::resetForNote() noexcept
{
    stopAll();
    for (auto& s : noteStarted)
        s = false;
    cur = 0;
    from = -1;
    fadePos = 0;
    boundSet = nullptr;
    for (auto& u : uni)
        u = Unison();
    lastCut = lastMix = 1.0e9f;
}

void LibOsc::setType (int newType, bool fresh) noexcept
{
    newType = std::clamp (newType, 0, 4);
    if (fresh)
    {
        cur = newType;
        from = -1;
        return;
    }
    if (newType == cur)
        return;
    from = cur;
    cur = newType;
    fadePos = 0;
    noteStarted[cur] = false;
}

void LibOsc::wavetableWeights (int /*t0*/, int n, float& w0, float& w1) const noexcept
{
    if (from < 0)
    {
        w0 = w1 = cur == 0 ? 1.0f : 0.0f;
        return;
    }
    const float r0 = clamp01 (static_cast<float> (fadePos) / static_cast<float> (fadeTotal));
    const float r1 = clamp01 (static_cast<float> (fadePos + n) / static_cast<float> (fadeTotal));
    if (cur == 0)
    {
        w0 = r0;
        w1 = r1;
    }
    else if (from == 0)
    {
        w0 = 1.0f - r0;
        w1 = 1.0f - r1;
    }
    else
        w0 = w1 = 0.0f;
}

void LibOsc::update (const OscTick& t, const LibParams& p, const ContentSet* content, int oscIndex) noexcept
{
    tick = t;
    for (int s = 0; s < 2; ++s)
    {
        tick.warpSrc[s] = warpCrossSource (t.warp[s]);
        tick.warp[s] = warpFamily (t.warp[s]);
    }
    lp = p;
    oscIdx = oscIndex;
    if (boundSet != content)
    {
        if (boundSet != nullptr)
            stopAll();
        boundSet = content;
    }
    if (cur == 0 && from <= 0)
        return;
    const float* v = lp.v;
    const ContentSet::Sample* smp = content != nullptr && oscIndex >= 0 && oscIndex < 3 ? &content->sample[oscIndex] : nullptr;
    const bool haveSample = smp != nullptr && smp->valid;
    const int type = cur;
    if (cur == kOscTypeSample && !noteStarted[kOscTypeSample])
    {
        // slices: the key chooses a slice and pitch tracking is off
        sliced = false;
        sparams.startOffset = clamp01 (v[lk_sStart] * 0.01f);
        sparams.endOffset = clamp01 (v[lk_sEnd] * 0.01f);
        const int sm = static_cast<int> (v[lk_sSliceMode] + 0.5f);
        if (sm != 0 && haveSample)
        {
            float arr[130];
            int n = 0;
            if (sm == 1)
            {
                arr[n++] = 0.0f;
                const float need = 1.0f - clamp01 (v[lk_sSliceThreshold] * 0.01f);
                for (const auto& on : smp->onsets)
                    if (on.strength >= need && on.position > 0.0f && n < 128)
                        arr[n++] = on.position;
            }
            else
                for (float f : smp->manualSlices)
                    if (n < 128)
                        arr[n++] = f;
            if (n > 0)
            {
                const bool single = v[lk_sSlicePlay] >= 0.5f;
                int idx = lp.key - static_cast<int> (v[lk_sSliceRoot] + 0.5f);
                if (single)
                    idx += static_cast<int> (std::lround (v[lk_sSlice] * 0.01f * static_cast<float> (n - 1)));
                idx = std::clamp (idx, 0, n - 1);
                sparams.startOffset = arr[idx];
                if (single)
                    sparams.endOffset = idx + 1 < n ? arr[idx + 1] : 1.0f;
                sliced = true;
            }
        }
    }
    // ---- pitch relative to the root of the recorded sample (the zone centre for the multisample type)
    const float root = type == kOscTypeMultisample ? 0.0f : v[lk_sRoot];
    const bool keyTrack = lp.pitchTrack && (type == kOscTypeMultisample || v[lk_sKeytrack] >= 0.5f) && !sliced;
    float semis = lp.transposeTune;
    if (keyTrack)
        semis += lp.noteSemis - root;
    else if (lp.bendTrack && !sliced)
        semis += lp.noteSemis - lp.noteRaw;
    // ---- unison
    const int maxU = type == kOscTypeSpectral ? kSpecU : kMaxU;
    U = std::clamp (tick.voices, 1, maxU);
    float panL, panR, panL2, panR2;
    panLaw (tick.pan, panL, panL2);
    panLaw (tick.panRight, panR2, panR);
    // the left lane drives the left channel, the right lane the right channel (as in the wavetable oscillator)
    (void) panL2;
    (void) panR2;
    const float blend = clamp01 (tick.blend);
    const float centreW = 1.0f - 0.5f * blend, detunedW = std::max (0.25f, 1.0f - (1.0f - blend) * (1.0f - blend));
    float sumSq = 0.0f;
    float w[kMaxU];
    float tt[kMaxU];
    for (int u = 0; u < U; ++u)
    {
        tt[u] = U > 1 ? 2.0f * static_cast<float> (u) / static_cast<float> (U - 1) - 1.0f : 0.0f;
        const bool centre = U == 1 || ((U & 1) != 0 && u == (U - 1) / 2);
        w[u] = centre ? centreW : detunedW;
        sumSq += w[u] * w[u];
    }
    const float norm = 1.0f / std::sqrt (std::max (sumSq, 1.0e-9f));
    const float typeGain = type == kOscTypeGranular ? kGranularGain : (type == kOscTypeSpectral ? kSpectralGain : 1.0f);
    float gNorm = 1.0f;
    if (type != kOscTypeMultisample && v[lk_sNormalize] >= 0.5f && haveSample)
        gNorm = smp->data.normalizeGain();
    for (int u = 0; u < U; ++u)
    {
        Unison& un = uni[u];
        un.gPrevL = un.gL;
        un.gPrevR = un.gR;
        const float sh = (tt[u] < 0.0f ? -1.0f : 1.0f) * powerShape (std::fabs (tt[u]), tick.curve);
        un.det = sh * tick.detuneSemis;
        un.semis = semis + un.det;
        float pl, pr;
        panLaw (tt[u] * clamp01 (tick.spread), pl, pr);
        un.gL = w[u] * norm * pl * panL * tick.level * typeGain * gNorm;
        un.gR = w[u] * norm * pr * panR * tick.level * typeGain * gNorm;
        if (!anyPlaying)
        {
            un.gPrevL = un.gL;
            un.gPrevR = un.gR;
        }
    }
    for (int u = U; u < kMaxU; ++u)
        uni[u].gL = uni[u].gR = 0.0f;
    // ---- the region and speed shared by the sample, granular and spectral types
    const float start = clamp01 (v[lk_sStart] * 0.01f), end = clamp01 (v[lk_sEnd] * 0.01f);
    float loopLo = clamp01 (v[lk_sLoopStart] * 0.01f), loopHi = clamp01 (v[lk_sLoopEnd] * 0.01f);
    if (v[lk_sLoopRelative] >= 0.5f)
    {
        loopLo = start + loopLo * (end - start);
        loopHi = start + loopHi * (end - start);
    }
    const int lm = static_cast<int> (v[lk_sLoopMode] + 0.5f);
    sampleosc::LoopMode loopMode = sampleosc::LoopMode::none;
    switch (lm)
    {
        case 1: loopMode = sampleosc::LoopMode::forward; break;
        case 2: loopMode = sampleosc::LoopMode::reverse; break;
        case 3: loopMode = sampleosc::LoopMode::pingPong; break;
        case 4: loopMode = sampleosc::LoopMode::tailed; break;
        default: break;
    }
    const double fileSeconds = haveSample ? static_cast<double> (smp->view.numFrames) / smp->view.sampleRate : 1.0;
    const float scanRange = static_cast<float> (1 << std::clamp (static_cast<int> (v[lk_sScanRange] + 0.5f), 0, 3));
    double speed = static_cast<double> (v[lk_sScan]) * 0.01 * scanRange;
    if (v[lk_sScanReverse] >= 0.5f)
        speed = -speed;
    if (v[lk_sScanLockTempo] >= 0.5f)
        speed *= lp.bpm / 120.0;
    if (v[lk_sScanToBpm] >= 0.5f)
    {
        const double target = static_cast<double> (std::max (v[lk_sLengthBeats], 0.01f)) * 60.0 / std::max (static_cast<double> (lp.bpm), 1.0);
        speed *= (std::fabs (end - start) * fileSeconds) / target;
    }
    rateSpeed = speed;
    const float crossFadeFrac = [&]
    {
        const double loopSec = std::fabs (loopHi - loopLo) * fileSeconds;
        return loopSec > 1.0e-6 ? clamp01 (static_cast<float> (v[lk_sLoopXfade] * 0.001 / loopSec)) : 0.0f;
    }();

    if (type == kOscTypeSample)
    {
        sparams.rootKey = 60;
        sparams.pitchTracking = true;
        sparams.loopMode = loopMode;
        if (!sliced)
        {
            sparams.startOffset = start;
            sparams.endOffset = end;
        }
        sparams.loopStart = loopLo;
        sparams.loopEnd = loopHi;
        sparams.loopCrossfade = crossFadeFrac;
        sparams.reverse = v[lk_sReverse] >= 0.5f;
        static const float kFadeMs[9] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f };
        sparams.edgeFadeMs = kFadeMs[std::clamp (static_cast<int> (v[lk_sFadeEdges] + 0.5f), 0, 8)];
        sparams.quality = static_cast<sampleosc::QualityTier> (std::clamp (lp.quality, 0, 2));
        sparams.level = 1.0f;
        sparams.pan = 0.0f;
    }
    else if (type == kOscTypeGranular)
    {
        granular::GrainParams& g = gparams;
        g.regionLo = start;
        g.regionHi = end;
        g.loopMode = loopMode;
        g.loopLo = loopLo;
        g.loopHi = loopHi;
        g.loopGrains = v[lk_gLoopGrains] >= 0.5f;
        g.loopCrossfade = crossFadeFrac;
        g.manual = v[lk_gManual] >= 0.5f;
        g.position = clamp01 (v[lk_gPosition] * 0.01f);
        g.scanSpeed = static_cast<float> (speed) * (v[lk_gScanKeytrack] >= 0.5f ? semitonesToRatio (lp.noteSemis - 60.0f) : 1.0f);
        const int dm = static_cast<int> (v[lk_gDensityMode] + 0.5f), lmod = static_cast<int> (v[lk_gLengthMode] + 0.5f);
        float lengthSec = std::max (v[lk_gLength], 1.0f) * 0.001f;
        if (lmod == 1)
            lengthSec = divisionBeats (static_cast<int> (v[lk_gLengthSync] + 0.5f)) * 60.0f / std::max (lp.bpm, 1.0f);
        float density = std::clamp (v[lk_gDensity], 0.1f, 1000.0f);
        if (dm == 1)
            density = lp.bpm / 60.0f / divisionBeats (static_cast<int> (v[lk_gDensitySync] + 0.5f));
        else if (dm == 2)
            density = std::max (1.0f, v[lk_gDensityGrains]) / std::max (lengthSec, 1.0e-3f);
        density = std::clamp (density, 0.1f, 1000.0f);
        if (lmod == 2)
            lengthSec = std::max (v[lk_gLengthPercent], 5.0f) * 0.01f / density;
        g.densityHz = density;
        g.lengthSec = std::clamp (lengthSec, 0.001f, 8.0f);
        g.maxGrains = std::clamp (static_cast<int> (v[lk_gMaxGrains]) / U, 1, granular::kMaxGrains);
        g.shape = static_cast<granular::WindowShape> (std::clamp (static_cast<int> (v[lk_gWindowShape] + 0.5f), 0, granular::kNumWindowShapes - 1));
        g.windowAmount = clamp01 (v[lk_gWindowAmount] * 0.01f);
        g.windowSkew = std::clamp (v[lk_gWindowSkew] * 0.01f, -1.0f, 1.0f);
        g.randOffset = clamp01 (v[lk_gRandOffset] * 0.01f);
        g.randDir = clamp01 (v[lk_gRandDir] * 0.01f);
        g.reverseGrains = v[lk_gReverseGrains] >= 0.5f;
        g.randPitch = v[lk_gRandPitch];
        g.randLength = clamp01 (v[lk_gRandLength] * 0.01f);
        g.randPan = clamp01 (v[lk_gRandPan] * 0.01f);
        g.randGain = clamp01 (v[lk_gRandLevel] * 0.01f);
        g.randWindow = clamp01 (v[lk_gRandWindow] * 0.01f);
        g.randSkew = clamp01 (v[lk_gRandSkew] * 0.01f);
        g.randWarp[0] = clamp01 (v[lk_gRandWarp] * 0.01f);
        g.randWarp[1] = clamp01 (v[lk_gRandWarp2] * 0.01f);
        g.shapeFn = &shapeFn;
        for (int s = 0; s < 2; ++s)
        {
            g.shapeType[s] = isPointwiseWarp (tick.warp[s]) ? tick.warp[s] : -1;
            g.shapeAmount[s] = tick.warpAmount[s];
        }
        g.gain = 1.0f;
    }
    else if (type == kOscTypeSpectral)
    {
        spectral::SpectralParams& s = specParams;
        s.scratch = lp.scratch;
        s.hostRate = sr;
        s.regionLo = start;
        s.regionHi = end;
        s.loopMode = loopMode;
        s.loopLo = loopLo;
        s.loopHi = loopHi;
        s.manual = v[lk_pManual] >= 0.5f;
        s.position = clamp01 (v[lk_pPosition] * 0.01f);
        s.scanSpeed = static_cast<float> (speed) * (v[lk_pScanKeytrack] >= 0.5f ? semitonesToRatio (lp.noteSemis - 60.0f) : 1.0f);
        s.freqLoHz = std::clamp (v[lk_pFreqLo], 20.0f, 20000.0f);
        s.freqHiHz = std::clamp (v[lk_pFreqHi], 20.0f, 20000.0f);
        s.bandSmooth = v[lk_pLohiSmooth] >= 0.5f;
        s.bandPostWarp = v[lk_pLohiPost] >= 0.5f;
        s.phaseLock = v[lk_pPhaseLock] >= 0.5f;
        s.transients = v[lk_pTransients] >= 0.5f;
        s.freqShiftHz = v[lk_pFreqShift];
        for (int k = 0; k < 2; ++k)
        {
            const int wt = tick.warp[k];
            s.warpType[k] = wt >= kFirstSpectralWarp && wt <= kLastSpectralWarp ? wt - kFirstSpectralWarp : -1;
            s.warpAmount[k] = tick.warpAmount[k];
            s.warpVar[k] = clamp01 (tick.warpPhase[k]);
            s.external[0] = s.external[1] = s.external[2] = nullptr;
        }
        s.gain = 1.0f;
        // the spectral filter: a wavetable frame (flt_use) or the drawn mask
        const float cut = std::clamp (v[lk_pCut] * 0.01f, -1.0f, 1.0f), mix = clamp01 (v[lk_pMix] * 0.01f);
        const Wavetable* fwt = nullptr;
        int fltTableChoice = -1;
        if (v[lk_fltUse] >= 0.5f && haveSample)
        {
            fltTableChoice = std::clamp (static_cast<int> (v[lk_fltTable] + 0.5f), 0, kNumWavetableChoices - 1);
            fwt = fltTableChoice == kWavetablePresetTable ? (content != nullptr ? content->userTable[oscIndex].get() : nullptr) : builtinWavetable (fltTableChoice);
        }
        if (fwt != nullptr)
        {
            const float pos = std::round (clamp01 (v[lk_fltFrame] * 0.01f) * static_cast<float> (std::max (fwt->numFrames() - 1, 0)) * 256.0f) / 256.0f;
            if (pos != lastFltPos || fltTableChoice != lastFltTable || cut != lastCut || mix != lastMix || lastFilterOsc != -2 - oscIndex)
            {
                const int f0 = std::min (static_cast<int> (pos), std::max (fwt->numFrames() - 2, 0));
                const float u = fwt->numFrames() > 1 ? pos - static_cast<float> (f0) : 0.0f;
                const float* a = fwt->cycle (f0, 0);
                const float* b = u > 0.0f ? fwt->cycle (f0 + 1, 0) : nullptr;
                for (int j = 0; j < 257; ++j)
                {
                    float sum = 0.0f;
                    for (int k = -4; k < 4; ++k)
                    {
                        const int idx = (j * 8 + k + Wavetable::kCycle) % Wavetable::kCycle;
                        sum += b != nullptr ? lerp (a[idx], b[idx], u) : a[idx];
                    }
                    fltX[j] = static_cast<float> (j) / 256.0f;
                    fltY[j] = clamp01 ((sum * 0.125f + 1.0f) * 0.5f);
                }
                spectral::buildFilterGains (filterGain.data(), fltX, fltY, 257, cut, mix, smp->analysis.fileRate() / spectral::kFftSize);
                lastFltPos = pos;
                lastFltTable = fltTableChoice;
                lastCut = cut;
                lastMix = mix;
                lastFilterOsc = -2 - oscIndex;
            }
            s.filterGain = filterGain.data();
        }
        else if (content != nullptr && !content->specFilterX[oscIndex].empty() && haveSample)
        {
            const std::vector<float>& fx = content->specFilterX[oscIndex];
            if (cut != lastCut || mix != lastMix || lastFilterOsc != oscIndex)
            {
                spectral::buildFilterGains (filterGain.data(), fx.data(), content->specFilterY[oscIndex].data(), static_cast<int> (fx.size()), cut, mix,
                                            smp->analysis.fileRate() / spectral::kFftSize);
                lastCut = cut;
                lastMix = mix;
                lastFilterOsc = oscIndex;
                lastFltTable = -1;
            }
            s.filterGain = filterGain.data();
        }
        else
            s.filterGain = nullptr;
    }

    // ---- the note starts (first period of the note, or a type change)
    if (!noteStarted[type])
    {
        noteStarted[type] = true;
        rng.seed (lp.serial * 977u + static_cast<uint32_t> (oscIndex) * 31u + static_cast<uint32_t> (type));
        randomPerNote = rng.uni();
        beginType (type);
    }
    if (type == kOscTypeSample)
    {
        for (int u = 0; u < U; ++u)
        {
            sampleosc::SampleVoice& sv = sampleVoices[static_cast<size_t> (u)];
            sv.setParams (sparams);
            sv.setSemitoneOffset (uni[u].semis);
            sv.setSpeedMultiplier (rateSpeed);
        }
    }
    anyPlaying = true;
}

void LibOsc::beginType (int type) noexcept
{
    const ContentSet* cs = boundSet;
    const float* v = lp.v;
    const int oi = oscIdx;
    const ContentSet::Sample* smp = cs != nullptr ? &cs->sample[oi] : nullptr;
    const bool haveSample = smp != nullptr && smp->valid;
    const float phase = lp.v[lk_phase] * (1.0f / 360.0f), rp = clamp01 (v[lk_randomPhase] * 0.01f);
    const float r0 = randomPerNote;
    auto startFraction = [&] (int u) noexcept
    {
        const float tu = U > 1 ? static_cast<float> (u) / static_cast<float> (U - 1) : 0.0f;
        const float ru = rng.uni();
        float f = phase + rp * r0 + clamp01 (v[lk_sUniStart] * 0.01f) * ru + clamp01 (v[lk_sUniSpan] * 0.01f) * tu;
        return f - std::floor (f);
    };
    if (type == kOscTypeSample && haveSample)
    {
        (void) smp;
        for (int u = 0; u < U; ++u)
        {
            sampleosc::SampleVoice& sv = sampleVoices[static_cast<size_t> (u)];
            sv.reset();
            sv.noteOn (60, 1.0f, &smp->view, sparams);
            sv.seekFraction (startFraction (u));
        }
    }
    else if (type == kOscTypeGranular && haveSample)
    {
        const int pattern = static_cast<int>(v[lk_gSpawnPattern] + 0.5f);
        for (int u = 0; u < U; ++u)
        {
            const float f = static_cast<float> (u) / static_cast<float> (U);
            float spawn = 0.0f;
            if (pattern == 1)
                spawn = f;
            else if (pattern == 2)
                spawn = std::exp2 (f) - 1.0f;
            else if (pattern == 3)
                spawn = rng.uni();
            grainVoices[static_cast<size_t> (u)].reset();
            gparams.pitchRatio = semitonesToRatio (uni[u].semis);
            grainVoices[static_cast<size_t> (u)].noteOn (&smp->view, gparams, lp.serial * 131u + static_cast<uint32_t> (u) * 17u + static_cast<uint32_t> (oi), spawn,
                                                         v[lk_gJumpStart] >= 0.5f, startFraction (u));
        }
    }
    else if (type == kOscTypeSpectral && haveSample && smp->analysis.valid() && lp.scratch != nullptr)
    {
        for (int u = 0; u < U; ++u)
        {
            specVoices[static_cast<size_t> (u)].reset();
            specParams.pitchRatio = semitonesToRatio (uni[u].semis);
            specVoices[static_cast<size_t> (u)].noteOn (&smp->analysis, specParams, startFraction (u));
        }
    }
    else if (type == kOscTypeMultisample && cs != nullptr && cs->instrument[oi] != nullptr)
    {
        const multisample::Instrument& inst = *cs->instrument[oi];
        for (auto& l : layers)
            l.voice.reset();
        noteVelocity127 = std::clamp (static_cast<int> (lp.velocity * 127.0f + 0.5f), 1, 127);
        const int key = multisample::Instrument::timbreKey (lp.key, std::clamp (v[lk_mTimbre] * 0.01f, -1.0f, 1.0f));
        int zones[multisample::kMaxLayers];
        numHeld = inst.selectZones (key, noteVelocity127, rng.uni(), lp.serial, lp.switchKey >= 0 ? lp.switchKey : inst.switchDefault(), zones, multisample::kMaxLayers);
        multisample::EnvelopeSettings env;
        const bool over = v[lk_mOverride] >= 0.5f;
        env.delay = v[lk_mDelay] * 0.001f;
        env.attack = v[lk_mAttack] * 0.001f;
        env.hold = v[lk_mHold] * 0.001f;
        env.decay = v[lk_mDecay] * 0.001f;
        env.sustain = clamp01 (v[lk_mSustain] * 0.01f);
        env.release = v[lk_mRelease] * 0.001f;
        const float velTrack = v[lk_mVelOverride] >= 0.5f ? v[lk_mVelTrack] : -1.0f;
        if (numHeld > 0)
        {
            lastZone_ = zones[0];
            lastKey_ = lp.key;
        }
        for (int h = 0; h < numHeld; ++h)
        {
            heldZones[h] = zones[h];
            layerCount[h] = U;
            for (int u = 0; u < U; ++u)
                layers[static_cast<size_t> (h * kMaxU + u)].voice.noteOn (inst, zones[h], lp.key, noteVelocity127, over ? &env : nullptr, velTrack, lp.serial,
                                                                          static_cast<sampleosc::QualityTier> (std::clamp (lp.quality, 0, 2)));
            // choke: a zone with a group releases the other layers of this voice that it chokes
            const int grp = inst.zone (zones[h]).region.group;
            if (grp > 0)
                chokeGroup (grp);
        }
    }
}

void LibOsc::chokeGroup (int group) noexcept
{
    for (auto& l : layers)
        if (l.voice.isActive() && l.voice.offBy() == group)
            l.voice.noteOff (l.voice.offFast());
}

void LibOsc::noteOff (bool loopExit) noexcept
{
    for (auto& v : sampleVoices)
        if (v.isActive() && (loopExit || sparams.loopMode == sampleosc::LoopMode::tailed))
            v.releaseLoop();
    for (auto& v : grainVoices)
        if (loopExit || gparams.loopMode == sampleosc::LoopMode::tailed)
            v.releaseLoop();
    for (auto& v : specVoices)
        if (loopExit || specParams.loopMode == sampleosc::LoopMode::tailed)
            v.releaseLoop();
    if (cur == kOscTypeMultisample && boundSet != nullptr && boundSet->instrument[oscIdx] != nullptr)
    {
        const multisample::Instrument& inst = *boundSet->instrument[oscIdx];
        for (auto& l : layers)
            if (l.voice.isActive())
                l.voice.noteOff (false);
        int zones[multisample::kMaxLayers];
        const int n = inst.selectReleaseZones (lp.key, noteVelocity127, rng.uni(), lp.serial, lp.switchKey >= 0 ? lp.switchKey : inst.switchDefault(), zones,
                                               multisample::kMaxLayers);
        for (int h = 0; h < n; ++h)
            for (int u = 0; u < U; ++u)
                layers[static_cast<size_t> ((multisample::kMaxLayers + h) * kMaxU + u)].voice.noteOn (inst, zones[h], lp.key, noteVelocity127, nullptr, -1.0f, lp.serial,
                                                                                                   static_cast<sampleosc::QualityTier> (std::clamp (lp.quality, 0, 2)));
    }
}

float LibOsc::modulator (int /*warpType*/, int slot, const float* const* cross, int k, float& m) const noexcept
{
    const int cs = tick.warpSrc[slot];
    m = cs >= 0 && cross != nullptr && cross[cs] != nullptr ? cross[cs][k] : 0.0f;
    return m;
}

void LibOsc::renderType (int type, int t0, int n, float* L, float* R, const float* const* cross) noexcept
{
    const float invP = 1.0f / static_cast<float> (kControlPeriod);
    const bool pointwiseOnVoice = type == kOscTypeSample;
    // frequency modulation of the read speed (sample and granular types) and the sample domain warps
    bool fm = false;
    float modA[2] = { 0, 0 };
    int fmKind[2] = { 0, 0 };
    for (int s = 0; s < 2; ++s)
    {
        const int w = tick.warp[s];
        switch (w)
        {
            case kWarpFmA: case kWarpFmB: case kWarpFmLinA: case kWarpFmLinB: case kWarpFmTzA: case kWarpFmTzB: fmKind[s] = 1; fm = true; break;
            case kWarpFmExpA: case kWarpFmExpB: fmKind[s] = 2; fm = true; break;
            default: break;
        }
        modA[s] = tick.warpAmount[s];
    }
    float* rm = nullptr;
    if (fm && (type == kOscTypeSample || type == kOscTypeGranular))
    {
        for (int k = 0; k < n; ++k)
        {
            float rate = 1.0f;
            for (int s = 0; s < 2; ++s)
                if (fmKind[s] != 0)
                {
                    float m;
                    modulator (tick.warp[s], s, cross, k, m);
                    const float a2 = 4.0f * modA[s] * modA[s] * m;
                    rate *= fmKind[s] == 1 ? std::max(1.0f + a2, 0.0f) : std::exp2 (a2);
                }
            rateBuf[static_cast<size_t> (k)] = rate;
        }
        rm = rateBuf.data();
    }
    auto postWarp = [&] (float y, int k) noexcept
    {
        for (int s = 0; s < 2; ++s)
        {
            const int w = tick.warp[s];
            if (w == kWarpNone)
                continue;
            const float a = tick.warpAmount[s];
            if (w == kWarpRmA || w == kWarpRmB)
            {
                float m;
                modulator (w, s, cross, k, m);
                y *= 1.0f + a * (m - 1.0f);
            }
            else if (w == kWarpAmA || w == kWarpAmB)
            {
                float m;
                modulator (w, s, cross, k, m);
                y *= (1.0f - a) + a * (0.5f + 0.5f * m);
            }
            else if (w == kWarpQuantize)
            {
                const float levels = std::exp2 (1.0f + 14.0f * (1.0f - a) * (1.0f - a) * (1.0f - a));
                y = std::round (y * levels) / levels;
            }
            else if (w >= kWarpDistTube && w <= kWarpDistSoftSat)
                y = warpSample (w, y, a, 0.0f);
        }
        return y;
    };
    const bool anyPost = tick.warp[0] != 0 || tick.warp[1] != 0;
    float* a = tmpL.data();
    float* b = tmpR.data();
    if (type == kOscTypeSample)
    {
        for (int u = 0; u < U; ++u)
        {
            sampleosc::SampleVoice& sv = sampleVoices[static_cast<size_t> (u)];
            if (!sv.isActive())
                continue;
            sv.processModulated (a, b, n, rm);
            for (int k = 0; k < n; ++k)
            {
                const float t = static_cast<float> (t0 + k + 1) * invP;
                float sl = a[k], sr2 = b[k];
                if (anyPost && pointwiseOnVoice)
                {
                    sl = postWarp (sl, k);
                    sr2 = postWarp (sr2, k);
                }
                L[k] += sl * lerp (uni[u].gPrevL, uni[u].gL, t);
                R[k] += sr2 * lerp (uni[u].gPrevR, uni[u].gR, t);
            }
        }
    }
    else if (type == kOscTypeGranular)
    {
        for (int u = 0; u < U; ++u)
        {
            granular::GranularVoice& gv = grainVoices[static_cast<size_t> (u)];
            if (!gv.isActive())
                continue;
            granular::GrainParams gp = gparams;
            gp.pitchRatio = semitonesToRatio (uni[u].semis);
            gv.process (gp, a, b, n, rm);
            for (int k = 0; k < n; ++k)
            {
                const float t = static_cast<float> (t0 + k + 1) * invP;
                float sl = a[k], sr2 = b[k];
                if (tick.warp[0] == kWarpRmA || tick.warp[0] == kWarpRmB || tick.warp[0] == kWarpAmA || tick.warp[0] == kWarpAmB || tick.warp[1] == kWarpRmA
                    || tick.warp[1] == kWarpRmB || tick.warp[1] == kWarpAmA || tick.warp[1] == kWarpAmB)
                {
                    sl = postWarp (sl, k);
                    sr2 = postWarp (sr2, k);
                }
                L[k] += sl * lerp (uni[u].gPrevL, uni[u].gL, t);
                R[k] += sr2 * lerp (uni[u].gPrevR, uni[u].gR, t);
            }
        }
    }
    else if (type == kOscTypeSpectral)
    {
        for (int u = 0; u < U && u < kSpecU; ++u)
        {
            spectral::SpectralVoice& sv = specVoices[static_cast<size_t> (u)];
            if (!sv.isActive() || specParams.scratch == nullptr)
                continue;
            spectral::SpectralParams sp = specParams;
            sp.pitchRatio = semitonesToRatio (uni[u].semis);
            sv.process (sp, a, b, n);
            for (int k = 0; k < n; ++k)
            {
                const float t = static_cast<float> (t0 + k + 1) * invP;
                L[k] += a[k] * lerp (uni[u].gPrevL, uni[u].gL, t);
                R[k] += b[k] * lerp (uni[u].gPrevR, uni[u].gR, t);
            }
        }
    }
    else if (type == kOscTypeMultisample && boundSet != nullptr && boundSet->instrument[oscIdx] != nullptr)
    {
        const multisample::Instrument& inst = *boundSet->instrument[oscIdx];
        const float t = static_cast<float> (t0 + n) * invP;
        for (int slot = 0; slot < kSlots; ++slot)
            for (int u = 0; u < U; ++u)
            {
                Layer& l = layers[static_cast<size_t> (slot * kMaxU + u)];
                if (!l.voice.isActive())
                    continue;
                const int zi = l.voice.zoneIndex();
                float st = lp.transposeTune;
                const float center = static_cast<float> (inst.zone (zi).region.center);
                if (lp.pitchTrack)
                    st += lp.noteSemis - center;
                else
                    st += (lp.bendTrack ? lp.noteSemis - lp.noteRaw : 0.0f);
                st += uni[u].det;
                l.voice.processAdd (L, R, a, b, n, st, nullptr, lerp (uni[u].gPrevL, uni[u].gL, t), lerp (uni[u].gPrevR, uni[u].gR, t));
            }
    }
}

void LibOsc::render (int t0, int n, float* L, float* R, const float* const* cross) noexcept
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    if (cur == 0 && from <= 0)
        return;
    if (from < 0)
    {
        renderType (cur, t0, n, L, R, cross);
        return;
    }
    const float r0 = clamp01 (static_cast<float> (fadePos) / static_cast<float> (fadeTotal));
    const float r1 = clamp01 (static_cast<float> (fadePos + n) / static_cast<float> (fadeTotal));
    float* ml = mixL.data();
    float* mr = mixR.data();
    auto mixIn = [&] (int type, float g0, float g1)
    {
        if (type == 0)
            return;
        std::fill (ml, ml + n, 0.0f);
        std::fill (mr, mr + n, 0.0f);
        renderType (type, t0, n, ml, mr, cross);
        for (int k = 0; k < n; ++k)
        {
            const float g = lerp (g0, g1, static_cast<float> (k + 1) / static_cast<float> (n));
            L[k] += ml[k] * g;
            R[k] += mr[k] * g;
        }
    };
    mixIn (cur, r0, r1);
    mixIn (from, 1.0f - r0, 1.0f - r1);
    fadePos += n;
    if (fadePos >= fadeTotal)
        from = -1;
}
} // namespace own
} // namespace synthyboy
