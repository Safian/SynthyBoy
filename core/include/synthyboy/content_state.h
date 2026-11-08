// SynthyBoy - core: the additive `content` section of the state / preset document (state version 3), docs/ui-design/13-new-keys.md 5.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Everything that is not a parameter key: references to wavetables, samples and impulses (never their data), custom LFO shapes,
// velocity / note curves, a tuning file, drawable filter curves. The section is part of the state document (the host state and
// the .sbpreset share one schema, ARCHITECTURE 5.2), bounded on load (the caps below) and written in a fixed order.
#pragma once

#include <cstddef>
#include <string>
#include <tuple>
#include <vector>

namespace synthyboy
{
constexpr size_t kMaxContentStringBytes = 255;   // name, author, pack, file, filter, source
constexpr int kMaxContentWavetables = 3;         // one per oscillator (oscN.wavetable = Preset table)
constexpr int kMaxContentSamples = 3;            // one per oscillator (oscN.type = Sample)
constexpr int kMaxSampleSlices = 128;
constexpr int kMaxContentLfoShapes = 10;         // one per LFO (lfoN.shape = Custom)
constexpr int kMaxLfoShapePoints = 128;
constexpr int kMaxCurvePoints = 64;              // per velocity / note curve
constexpr int kMaxContentImpulses = 1;           // convolve.ir = Preset IR
constexpr int kMaxContentFilterCurves = 3;       // filter1, filter2, fxfilter (filterN.model = PZ SVF)
constexpr int kMaxFilterCurvePoints = 16;
constexpr int kMaxContentRemaps = 12;            // 3 oscillators x 4 slots (oscN.remap.slot)
constexpr int kNumRemapSlots = 4;
constexpr int kMaxRemapPoints = 64;
constexpr int kMaxContentModCurves = 64;         // one per modulation matrix slot (modNN, G2)
constexpr int kMaxModCurvePoints = 100;
constexpr size_t kMaxTuningSclBytes = 64 * 1024;
constexpr size_t kMaxTuningKbmBytes = 4 * 1024;
constexpr double kMaxPointModAmount = 1.0;       // |amount| of the point modulation of a custom LFO point
constexpr double kMaxPointPower = 20.0;          // |power| of a curve segment

/** Reference to a file of a ContentPack: pack id, relative path, SHA-256 (64 lower case hex digits, or empty = unknown). */
struct ContentFileRef
{
    std::string pack, file, sha256, name;
    auto tie() const { return std::tie (pack, file, sha256, name); }
};

struct ContentWavetable
{
    int osc = 0; // 1..3
    ContentFileRef ref;
    std::string author;
    int frames = 0;
};

struct ContentSample
{
    int osc = 0; // 1..3
    ContentFileRef ref;
    int sampleRate = 0, frames = 0, channels = 0;
    std::vector<float> slices; // 0..1, ascending
};

/** Point modulation of a custom LFO point (G65): a modulation source key of docs/ui-design/13-new-keys.md section 2 and an amount. */
struct ContentModRef
{
    bool present = false;
    std::string source;
    float amount = 0.0f;
};

struct ContentPoint
{
    float x = 0.0f, y = 0.0f, power = 0.0f;
    ContentModRef modX, modY; // only on LFO shape points

    ContentPoint() = default;
    ContentPoint (float xIn, float yIn, float powerIn = 0.0f) : x (xIn), y (yIn), power (powerIn) {}
};

struct ContentLfoShape
{
    int lfo = 0; // 1..10
    std::string name;
    bool smooth = false;
    int loopPoint = -1; // -1: none
    std::vector<ContentPoint> points;
};

/** A drawn remap curve of the wavetable position of one oscillator (G69, 13-new-keys.md 4.14): input position 0..1 (frame / last frame) to
    output position 0..1. `slot` is the Remap 1..4 preset of oscN.remap.slot; a slot without an entry is the identity. */
struct ContentRemap
{
    int osc = 0;  // 1..3
    int slot = 0; // 1..4
    std::string name;
    std::vector<ContentPoint> points; // 2 .. kMaxRemapPoints, ascending x in 0..1 (the engine clamps the ends)
};

/** A drawn per-connection modulation curve (G2, 13-new-keys.md B20): remaps source value before applying amount.
    `slot` is the modulation slot index (1..64). */
struct ContentModCurve
{
    int slot = 0; // 1..64
    std::vector<ContentPoint> points; // 2 .. kMaxModCurvePoints, ascending x in 0..1
};

struct ContentFilterCurvePoint
{
    float x = 0.0f; // log frequency 0..1
    float y = 0.0f; // dB
};

struct ContentFilterCurve
{
    std::string filter; // "filter1" / "filter2" / "fxfilter"
    std::vector<ContentFilterCurvePoint> points;
};

struct ContentTuning
{
    bool present = false;
    std::string scl;
    bool hasKbm = false;
    std::string kbm;
};

struct StateContent
{
    std::vector<ContentWavetable> wavetables;
    std::vector<ContentSample> samples;
    std::vector<ContentLfoShape> lfoShapes;
    std::vector<ContentPoint> velocityCurve, noteCurve; // empty = the identity [(0,0),(1,1)]
    /** The two drawn transfer curves of distortion.type X-Shaper / X-Shaper (Asym) (13-new-keys.md 4.17, G75): input magnitude (x) against
        output magnitude (y), both 0..1, the curve shape of the velocity curve. Empty = the identity [(0,0),(1,1)]. */
    std::vector<ContentPoint> shaperA, shaperB;
    ContentTuning tuning;
    std::vector<ContentFileRef> impulses;
    std::vector<ContentFilterCurve> filterCurves;
    std::vector<ContentRemap> remaps;
    std::vector<ContentModCurve> modCurves;

    bool empty() const noexcept
    {
        return wavetables.empty() && samples.empty() && lfoShapes.empty() && velocityCurve.empty() && noteCurve.empty() && shaperA.empty() && shaperB.empty()
            && ! tuning.present && impulses.empty() && filterCurves.empty() && remaps.empty() && modCurves.empty();
    }
    void clear() noexcept { *this = StateContent {}; }
};

bool operator== (const ContentModRef& a, const ContentModRef& b) noexcept;
bool operator== (const ContentPoint& a, const ContentPoint& b) noexcept;
bool operator== (const StateContent& a, const StateContent& b) noexcept;
inline bool operator!= (const StateContent& a, const StateContent& b) noexcept { return ! (a == b); }

/** True if every entry is inside the caps and ranges above (counts, string lengths, path safety, ascending x, finite numbers, known
    mod sources). loadStateWithContent rejects a section that fails this; saveState omits it. */
bool validateContent (const StateContent& content) noexcept;
} // namespace synthyboy
