// SynthyBoy - own engine: one effect module as an object (instance 0 of the chain and the slots of the instance pool share it).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "../dsp/util.h"
#include "../matrix/mod_matrix.h"
#include "../osc/content_set.h"

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
    double songBeats = 0.0;
    const ContentSet* content = nullptr; // the set this block reads (Convolve takes its impulse from it)
    bool lowCost = false;                // tier low: the cheaper forms of dsp/fastmath.h in the per sample code
    bool realtime = false;               // Quality::realtime: the audio thread never waits for a helper thread
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
    std::mutex mutex;
    std::condition_variable cv;
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

    virtual void prepare (double sampleRate, int maxBlock) = 0;
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
    /** Position of the current call inside the control period (set by process ()). */
    int callOffset() const noexcept { return t0Now; }
    int callPeriod() const noexcept { return periodNow; }

    void update (const FxFrame& f, const float* base, const float* delta) noexcept
    {
        levelPrev = levelGain;
        levelGain = levelIdx >= 0 ? dbToGain (fxValue (base, delta, levelIdx)) : 1.0f;
        if (!levelSeen)
        {
            levelPrev = levelGain;
            levelSeen = true;
        }
        configure (f, base, delta);
    }
    /** The module and its output trim (ramped across the period); t0 = position of this call in the period, period = its length. */
    void process (float* l, float* r, int n, int t0, int period) noexcept
    {
        t0Now = t0;
        periodNow = period;
        run (l, r, n);
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

private:
    int levelIdx;
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
