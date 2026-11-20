// SynthyBoy - own engine: one effect module as an object (instance 0 of the chain and the slots of the instance pool share it).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <limits>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "../dsp/helper_thread.h"
#include "../dsp/util.h"
#include "../dsp/peak.h"
#include "../matrix/mod_matrix.h"
#include "../osc/content_set.h"
#include "fx_makeup.h"

namespace synthyboy
{
namespace own
{
/** What every module needs besides its own keys: the host tempo, the length of a control period, the last played note (key tracks), the global rate. */
struct FxFrame
{
    double bpm = 120.0;
    int period = kControlPeriod;
    float lastNote = 60.0f;
    bool noSmoothing = false;            // quality.no_smoothing: the module's settings apply at once (configure () runs twice, the level trim does not ramp)
    uint32_t noteOns = 0;                // the number of notes started so far (Hyper and Bode retrigger on a change of it)
    double songBeats = 0.0;
    const ContentSet* content = nullptr; // the set this block reads (Convolve takes its impulse from it)
    bool lowCost = false;                // tier low: the cheaper forms of dsp/fastmath.h in the per sample code
    bool realtime = false;               // Quality::realtime: the audio thread never waits for a helper thread
    bool muteSkip = false;               // economy group Control: a module whose mix is exactly 0 is not processed (FxModule::process)
    bool makeup = true;                  // Quality::fxMakeup: a bypassed effect that is on in the preset leaves its loudness as a gain (FxChain, FxModule::makeupGain)
    bool scalarLoops = false;            // measurements (setOwnSavingsOff, kOwnSavingVectorFilters): the scalar loops also where a vector form exists (the chorus LFO refresh)
    double sampleRate = 44100.0;
};

/** One helper thread for the slow work of the effects that must not run on the audio thread (the partitioning of a Convolve impulse). A job is a function and a
    context; the queue is a fixed array (submitting never allocates). Process wide, started on first use (never on the audio thread). */
class FxWorker
{
public:
    using Fn = void (*) (void*);
    static FxWorker& instance();
    /** false when the queue is full. */
    bool submit (Fn fn, void* ctx) noexcept;
    ~FxWorker();

private:
    FxWorker();
    void loop();
    struct Job { std::atomic<int> state { 0 }; Fn fn = nullptr; void* ctx = nullptr; };
    static constexpr int kJobs = 64;
    Job jobs[kJobs];
    WakeSignal wake;   // no timeout: submit () and the destructor notify (dsp/helper_thread.h)
    std::atomic<bool> stop { false };
    std::thread thread;
};

/** The modules of the fx.order choices 0 .. 12 (instance 0) and the types of the pool (type - 1). */
constexpr int kNumFxModules = 13;

/** A module reads its keys from `base` / `delta`: for instance 0 the engine's arrays, for a pool slot an array that holds the plain values of the source keys
    (docs/engine-spec/06 section 12). */
inline float fxValue (const float* base, const float* delta, int idx) noexcept
{
    if (idx < 0)
        return 0.0f;
    const float d = delta != nullptr ? delta[idx] : 0.0f;
    return d == 0.0f ? base[idx] : modulatedValue (paramTable()[static_cast<size_t> (idx)], destLawOf (static_cast<size_t> (idx)), base[idx], d);
}

class FxModule
{
public:
    explicit FxModule (int levelKeyIndex) noexcept : levelIdx (levelKeyIndex) {}
    virtual ~FxModule() = default;

    virtual void prepare (double sampleRate, int maxBlock)
    {
        sampleRate_ = sampleRate;
    }
    void setSampleRate (double sampleRate) noexcept { sampleRate_ = sampleRate; }
    virtual void reset() noexcept = 0;
    /** Reads the keys once per control period. */
    virtual void configure (const FxFrame& f, const float* base, const float* delta) noexcept = 0;
    virtual void run (float* l, float* r, int n) noexcept = 0;
    /** Advances what runs on its own (the LFO phase) by n samples without processing audio: the module is skipped while its input is silent and its tail is gone (tier low). */
    virtual void idleAdvance (int /*n*/) noexcept {}
    /** Latency in frames of the chain rate, a function of the configuration only (document 06 section 11). */
    virtual int latency() const noexcept { return 0; }
    /** false: the host is not told about this module's latency (limiter with limit_latency_comp off); the signal is delayed all the same. */
    virtual bool latencyReported() const noexcept { return true; }
    /** The module index 0 .. 12. */
    virtual int moduleIndex() const noexcept = 0;
    /** true: the module has a model of its loudness (makeupPower), so that a tier that bypasses it can leave that loudness behind (core/engine-own/fx/fx_makeup.h). */
    virtual bool hasMakeup() const noexcept { return false; }
    /** The power the module gives the program, against the bypassed module (1 = the same level), from the module's own keys (`base` / `delta` as configure () reads them) and the program the chain's meter sees at the module's place (`program`). Called by
        the chain for a module that is on in the preset but bypassed by the quality mask, once per control period, on the audio thread: it must be cheap (the models cache their result
        while the keys are the same) and may start work on the helper thread but never wait for it. The level trim of the module is not in it (makeupGain). */
    virtual float makeupPower (const FxFrame& /*f*/, const float* /*base*/, const float* /*delta*/, const makeup::ProgramDesc& /*program*/) noexcept { return 1.0f; }
    /** The amplitude with which a copy of the input of the module on another path adds up with the module's output (the part of the output that is coherent with the input, per unit of
        input): the dry part of the mix by default of the models; Convolve adds the real part of its impulse response. Used where a bypassed effect sits on a bus (FxChain::busMakeup). */
    virtual float makeupCoherent (const FxFrame& /*f*/, const float* /*base*/, const float* /*delta*/, const makeup::ProgramDesc& /*program*/) noexcept { return 0.0f; }
    /** The gain at the place of the bypassed module, its power (with the level trim) and its coherent amplitude (with the level trim). */
    void makeup (const FxFrame& f, const float* base, const float* delta, const makeup::ProgramDesc& program, float& gain, float& power, float& coherent) noexcept
    {
        const float level = levelGainOf (base, delta);
        const float p = std::max (makeupPower (f, base, delta, program), 1.0e-4f);
        gain = level * std::sqrt (p);
        power = level * level * p;
        coherent = level * makeupCoherent (f, base, delta, program);
    }
    /** The gain at the place of the bypassed module: the trim of its Level key and the square root of makeupPower (). */
    float makeupGain (const FxFrame& f, const float* base, const float* delta, const makeup::ProgramDesc& program) noexcept
    {
        const float level = levelGainOf (base, delta);
        return level * std::sqrt (std::max (makeupPower (f, base, delta, program), 1.0e-4f));
    }
    /** Position of the current call inside the control period (set by process ()). */
    int callOffset() const noexcept { return t0Now; }
    int callPeriod() const noexcept { return periodNow; }

    void update (const FxFrame& f, const float* base, const float* delta) noexcept
    {
        if (f.sampleRate > 0.0)
            sampleRate_ = f.sampleRate;
        levelPrev = levelGain;
        levelGain = levelGainOf (base, delta);
        if (!levelSeen)
        {
            levelPrev = levelGain;
            levelSeen = true;
        }
        float mixBefore = mixNow;
        mixNow = -1.0f;
        probing = false;
        configure (f, base, delta);
        if (f.noSmoothing)
        {
            configure (f, base, delta); // the same settings twice: a module that ramps from its previous to its new settings finds them equal
            levelPrev = levelGain;
        }
        if (!mixSeen)
        {
            mixBefore = mixNow; // the first period of the module's life: there was no mix before it
            mixSeen = true;
        }
        // Muted: the mix (after its modulation) is exactly 0 in this control period and was in the one before, so nothing of the module reaches its output in this period
        // (every module that reports its mix applies it as one value per period, dry * 1 + wet * 0). Only for a module without latency (a skipped lookahead would move the dry signal).
        muted = f.muteSkip && mixNow == 0.0f && mixBefore == 0.0f && latency() == 0;
        if (muted && !clean)
        {
            // its state still holds a tail: it is flushed, run on silence with the mix opened so that the tail can be watched (process ()); the keys are read once more for that
            probing = true;
            configure (f, base, delta);
            mixNow = 0.0f;
        }
        quietNeeded = static_cast<int> (flushSeconds() * static_cast<float> (sampleRate_));
    }
    /** The module and its output trim (ramped across the period); t0 = position of this call in the period, period = its length.
        A muted module (update ()) is not run: its output is its input. While its state is clean (never fed since it was made, or flushed) that costs nothing but the
        advance of its free running phase. A muted module with a tail in its state is flushed first: it runs on silence, mix opened, into a scratch buffer, until what
        comes out has stayed below -120 dBFS for flushSeconds (); the signal passes untouched meanwhile. So when the mix is opened again the module starts from silence:
        it plays what is fed from then on, without the tail of the notes before (the unmuted engine would have had it), and without a click. */
    void process (float* l, float* r, int n, int t0, int period) noexcept
    {
        t0Now = t0;
        periodNow = period;
        if (muted && n <= kControlPeriod)
        {
            if (clean)
                idleAdvance (n);
            else
            {
                float zl[kControlPeriod] = {}, zr[kControlPeriod] = {};
                run (zl, zr, n);
                const float zp = own::peak::peakAbsPair (zl, zr, n);
                quietRun = zp < 1.0e-6f ? quietRun + n : 0; // (a non finite sample is not below the threshold: the module stays in the flush)
                if (quietRun >= quietNeeded)
                    clean = true;
            }
        }
        else
        {
            run (l, r, n);
            clean = false;
            quietRun = 0;
        }
        if (levelGain != 1.0f || levelPrev != 1.0f)
        {
            const float invP = 1.0f / static_cast<float> (std::max (period, 1));
            for (int i = 0; i < n; ++i)
            {
                const float g = lerp (levelPrev, levelGain, static_cast<float> (t0 + i + 1) * invP);
                l[i] *= g;
                r[i] *= g;
            }
        }
    }
    void clearLevel() noexcept { levelSeen = false; }
    /** The module is muted and skipped in this control period (tests, statistics). */
    bool skippedNow() const noexcept { return muted && clean; }

protected:
    /** A module that can be muted passes its mix (0 .. 1, after modulation) through this in configure (): the value is noted, and while the module is flushed the
        mix it runs with is 1. A module that never calls it is never muted. */
    float wetMix (float configured) noexcept
    {
        mixNow = configured;
        return probing ? 1.0f : configured;
    }
    /** How long the flushed output has to stay below -120 dBFS before the state counts as silent: longer than the longest gap between two echoes of the module. */
    virtual float flushSeconds() const noexcept { return 1.0f; }

private:
    /** dbToGain of the Level key, computed again only when the key's value changes (a bit identical cache of a pow). */
    float levelGainOf (const float* base, const float* delta) const noexcept
    {
        if (levelIdx < 0)
            return 1.0f;
        const float db = fxValue (base, delta, levelIdx);
        if (!(db == levelDbCached) || makeup::legacyCaches())
        {
            levelGainCached = dbToGain (db);
            levelDbCached = db;
        }
        return levelGainCached;
    }
    mutable float levelDbCached = std::numeric_limits<float>::quiet_NaN(), levelGainCached = 1.0f;
    int levelIdx;
    float mixNow = -1.0f;     // the mix of this period as the module reported it (-1: not reported)
    bool muted = false, probing = false, mixSeen = false;
    bool clean = true;        // nothing was fed since the module was made, or it was flushed
    int quietRun = 0, quietNeeded = 44100;
    double sampleRate_ = 44100.0;
    int t0Now = 0, periodNow = kControlPeriod;
    float levelGain = 1.0f, levelPrev = 1.0f;
    bool levelSeen = false;
};

/** A module of the chain, by index 0 .. 12 (nullptr beyond). The memory is allocated here (prepare thread). */
std::unique_ptr<FxModule> makeFxModule (int module);

/** The table indices of the keys a module needs (built once). */
int fxModuleKeyIndex (const char* key) noexcept;
} // namespace own
} // namespace synthyboy
