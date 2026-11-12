// SynthyBoy - param-audit: the harness library of the parameter effect audit (docs/ui-design/PARAM-AUDIT-REPORT.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One engine ("rig"), a reference patch, a script of notes, deterministic offline renders and the metrics measured on them. Stage 1 (tools/param-audit/param_audit.cpp) sweeps
// every key through its context; stage 2 (tools/param-audit/test_param_semantics.cpp) asserts the expected curves of the parameter families. No host, no JUCE, no files.
// Fault injection for the self test: the environment variable SYNTHYBOY_AUDIT_FAULT names a fault that changes the value the harness hands to the engine (see faultFor ()).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"

namespace paudit
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;
constexpr int kBands = 24;
/** Changes of the audio below this level (peak of the difference against the peak of the render) are no change: the history residual of the engine (about -70 dB). */
constexpr double kNoiseFloorDb = -60.0;

/** A parameter override by key. */
struct Ov
{
    std::string key;
    float value = 0.0f;
};
using Ovs = std::vector<Ov>;

/** What is played: sorted events at absolute frames, the length of the render and the windows the metrics are taken in. */
struct Script
{
    std::string name;
    std::vector<synthyboy::Event> events;
    int frames = 0;
    int steadyFrom = 0, steadyTo = 0; // the steady part of the held notes (spectral metrics are measured here)
    int offFrame = 0;                 // the note off (envelope metrics)
    double bpm = 120.0;
};
/** "default" (C3 + G3, velocity 100, 0.6 s held, 1.0 s long), "long" (the same, 2 s: delays, reverbs), "single" (C3 alone), "chord8" (eight notes), "legato" (two overlapping notes), "seq" (two notes in a row),
    "bend" (C3 and a pitch bend of +1 at 0.3 s), "same" (the same key twice), "fast" (two quick notes of 0.1 s), "mpe" (member channel notes with expressions), "scale" (notes between the scale degrees). */
Script scriptByName (const std::string& name);

struct Audio
{
    std::vector<float> l, r;
};

struct Metrics
{
    bool finite = true;
    double rmsDb = -180, peakDb = -180, centroid = 0, balanceDb = 0, f0 = 0, attackMs = 0, decayDb = 0, releaseMs = 0, zcRate = 0, jump = 0, envStdDb = 0, envHz = 0;
    double band[kBands] = {};
    double tailDb = -180; // RMS of the last 100 ms of the render
    uint64_t hash = 0;
};
Metrics analyse (const Audio& audio, const Script& script);
const char* metricName (int index);
constexpr int kNumMetrics = 12 + kBands;
double metricValue (const Metrics& m, int index);
/** Smallest change of a metric that counts as "moved" (see PARAM-AUDIT-REPORT.md 2.3). */
double metricThreshold (int index);

/** The content fixtures every render has available (wavetable "Preset table", samples, impulse, instrument, custom LFO shapes ...). */
struct Fixtures
{
    synthyboy::MemoryContentStore store;
    synthyboy::StateContent content;
    Fixtures();
    /** The process wide instance. */
    static Fixtures& shared();
};

/** One engine instance with the fixtures, prepared at the offline quality; every sweep starts from the defaults of the table. */
class Rig
{
public:
    explicit Rig (synthyboy::EngineKind kind = synthyboy::EngineKind::vital, double sampleRate = kSr, synthyboy::Tier tier = synthyboy::Tier::high);
    synthyboy::IEngine& engine() { return *engine_; }
    bool ok() const { return engine_ != nullptr; }
    /** All keys back to the defaults of the table, then `ov`. */
    void begin (const Ovs& ov);
    void set (const std::string& key, float value);
    float get (const std::string& key) const;
    /** reset (), then plays the script. */
    Audio render (const Script& script);
    /** begin + render in one go. */
    Audio renderWith (const Ovs& ov, const Script& script);
    bool supports (const std::string& key) const;
    double sampleRate() const { return sr_; }
    /** Renders made by all rigs of the process so far (the cost measure of the audit: a render is a second of audio). */
    static long renderCount();

private:
    std::unique_ptr<synthyboy::IEngine> engine_;
    Fixtures& fixtures_; // one set for the process (built once, read only: every key of the audit builds its own Rig)
    double sr_;
    synthyboy::Tier tier_;
    void prepareIfNeeded();
    Audio renderOnce (const Script& script);
};

/** The value the harness really sends for (key, value): identity unless a fault is injected. */
float faultFor (const std::string& key, float value);
const char* activeFault();

// ---- sweeps and contexts -------------------------------------------------------------------------------------------------------------------------------------------------------------------

enum class Density { quick, full };
/** The values to render for a key: every step of a stepped key, a dense skew-aware sweep of a continuous one (+ default, neighbours, edges). `outside` receives the two values just outside the range. */
std::vector<float> sweepValues (const synthyboy::ParamInfo& info, Density density, std::vector<float>* outside = nullptr);

/** One way to put a key into a state where it can be heard. */
struct Context
{
    std::string name;
    Ovs ov;
    std::string script = "default";
};
/** The ladder of contexts for a key, most likely first. The first one in which the key is alive is used. */
std::vector<Context> contextsFor (const synthyboy::ParamInfo& info);
/** A modulation routing of source `source` (choice value) to `destKey` through slot `slot` (1..64). */
Ovs routeOverrides (int slot, int source, const std::string& destKey, float amountPercent, bool bipolar = false);
/** The choice value of a modulation destination key (0 when it is no destination). */
int destinationChoice (const std::string& key);
/** Keys that are no sound parameter by design (UI / host / content mirrors), with the reason; nullptr when the key is not exempt. */
const char* exemptionReason (const synthyboy::ParamInfo& info);
/** Keys whose effect is abrupt by design (a click bound does not apply). */
bool abruptByDesign (const synthyboy::ParamInfo& info);

/** Welch power spectrum (Hann, 50 % overlap, `nfft` a power of two) of the mono mix of x[from, to): nfft / 2 + 1 bins of power, bin k at k * sampleRate / nfft. */
std::vector<double> welch (const Audio& audio, size_t from, size_t to, size_t nfft, int channel = -1);
/** Mean power in dB over [hz / 2^(octaves / 2), hz * 2^(octaves / 2)] of a spectrum made by welch (). */
double bandDb (const std::vector<double>& spectrum, size_t nfft, double hz, double octaves = 1.0 / 3.0);

std::string fmtG (double v);
uint64_t fnv (const float* p, size_t n, uint64_t seed = 1469598103934665603ull);
} // namespace paudit
