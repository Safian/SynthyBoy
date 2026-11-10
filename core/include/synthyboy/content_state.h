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
constexpr int kNoiseSampleOsc = 4;               // content.samples[].osc of the noise source (noise.type = Sample, GROUPA1 WP-04)
constexpr int kMaxContentSamples = 4;            // one per oscillator (oscN.type = Sample) and one for the noise source (osc 4)
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
constexpr int kMaxContentSpectralFilters = 3;    // one drawn spectral filter curve per oscillator (oscN.type = Spectral, GROUPC WP-12)
constexpr int kMaxContentInstruments = 3;        // one multisample instrument per oscillator (oscN.type = Multisample, GROUPC WP-13)
constexpr int kMaxContentModCurves = 64;         // one per modulation matrix slot (modNN, G2)
constexpr float kMaxModSlewMs = 5000.0f;         // RISE / FALL of a modulation connection (GROUPB WP-08)
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
    int osc = 0; // 1..3 = the oscillator, 4 = the noise source (kNoiseSampleOsc)
    ContentFileRef ref;
    int sampleRate = 0, frames = 0, channels = 0;
    std::vector<float> slices; // 0..1, ascending
};

/** A multisample instrument of an oscillator (GROUPC WP-13, 13-new-keys.md 4.39): `ref` is the SFZ file of a content pack (or of the user library); the samples its regions name
    are files of the same pack, relative to the folder of the SFZ file. `zones` is the number of regions the SFZ has (display only, 0 = unknown). */
struct ContentInstrument
{
    int osc = 0; // 1..3
    ContentFileRef ref;
    int zones = 0;
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
    ContentModRef modCurve;   // REMAINDER item 4 (S13): the curve of the segment after the point; amount 1 = 10 power units (A457)

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
    std::vector<ContentPoint> points; // 0 or 2 .. kMaxModCurvePoints, ascending x in 0..1 (no points: only the slew below)
    /** LEGACY (GROUPB WP-08, 13-new-keys.md 4.35): the RISE / FALL of the connection used to be stored here (ms). They are the keys modNN.rise / modNN.fall since REMAINDER
        item 1 (4.41); the state loader moves a document's values into the keys and clears them, the writer never writes them. Kept so that old documents parse. */
    float riseMs = 0.0f;
    float fallMs = 0.0f;
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
    /** The drawn mask of the spectral filter of oscillator `osc` (GROUPC WP-12): x = position on the frequency axis (log, 20 Hz .. 20 kHz) 0..1, y = 0..1 with 0.5 = unity gain; `slot` is 1. */
    std::vector<ContentRemap> spectralFilters;
    std::vector<ContentInstrument> instruments;
    std::vector<ContentModCurve> modCurves;

    bool empty() const noexcept
    {
        return wavetables.empty() && samples.empty() && lfoShapes.empty() && velocityCurve.empty() && noteCurve.empty() && shaperA.empty() && shaperB.empty()
            && ! tuning.present && impulses.empty() && filterCurves.empty() && remaps.empty() && spectralFilters.empty() && instruments.empty() && modCurves.empty();
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
