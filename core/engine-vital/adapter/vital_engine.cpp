// SynthyBoy - engine-vital adapter: IEngine over the Vital-derived DSP (sbvital::SoundEngine).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// prepare() builds a complete engine (the only call that allocates): the engine, the built-in wavetables, the tables of the
// modulation matrix. At the start of every block process() copies the parameters that changed into the engine's controls
// (vital_bindings.cpp: one converter per kind), wires the modulation slots whose source or destination changed, then splits the
// host block into chunks of at most kMaxBufferSize frames and at every event frame, applies the events between the chunks and
// renders into left/right. Nothing in process() allocates (tested with a counting operator new, core/tests/test_rt_alloc.cpp
// and core/engine-vital/tests).

#include "vital_engine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#if defined (__SSE2__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
 #include <xmmintrin.h>
#endif

#include "distortion.h"
#include "distortion_module.h"
#include "modulation_connection_processor.h"
#include "sample_oscillator.h"
#include "serum_filter_bank.h"
#include "serum_fx_modules.h"
#include "sound_engine.h"
#include "synth_lfo.h"
#include "synth_oscillator.h"
#include "synth_constants.h"
#include "synth_parameters.h"
#include "tuning.h"
#include "utils.h"
#include "value_switch.h"
#include "vital_bindings.h"
#include "vital_point_mod.h"
#include "vital_tables.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/sampleosc/wav_reader.h"
#include "synthyboy/modulation.h"
#include "synthyboy/shapes/shapes.h"
#include "synthyboy/tuning.h"

namespace synthyboy
{
static_assert (std::atomic<float>::is_always_lock_free, "parameter atomics must be lock free (audio thread)");

namespace
{
constexpr int kChunk = sbvital::kMaxBufferSize;      // largest block Vital's engine accepts
constexpr float kVitalLocalBendRange = 48.0f;        // VoiceHandler::kLocalPitchBendRange: setPitchWheel (v, ch) scales v by this
constexpr int kSettleChunks = 100;                   // silent chunks rendered by reset(): the smoothers of the engine have settled on their targets

/** Flushes denormals to zero while process() runs (and restores the caller's mode): the engine itself no longer
    touches the thread's floating point control register (docs/UPSTREAM.md). */
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if defined (__SSE2__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u); // FTZ | DAZ
#elif defined (__aarch64__) && (defined (__GNUC__) || defined (__clang__))
        unsigned long long fpcr;
        __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (fpcr));
        saved = fpcr;
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (fpcr | (1ull << 24)));
#elif defined (__arm__) && defined (__VFP_FP__) && (defined (__GNUC__) || defined (__clang__))
        unsigned int fpscr;
        __asm__ __volatile__ ("vmrs %0, fpscr" : "=r" (fpscr));
        saved = fpscr;
        __asm__ __volatile__ ("vmsr fpscr, %0" : : "r" (fpscr | (1u << 24)));
#endif
    }

    ~ScopedFlushDenormals()
    {
#if defined (__SSE2__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
        _mm_setcsr (saved);
#elif defined (__aarch64__) && (defined (__GNUC__) || defined (__clang__))
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (saved));
#elif defined (__arm__) && defined (__VFP_FP__) && (defined (__GNUC__) || defined (__clang__))
        __asm__ __volatile__ ("vmsr fpscr, %0" : : "r" (saved));
#endif
    }

private:
#if defined (__SSE2__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
    unsigned int saved = 0;
#elif defined (__aarch64__)
    unsigned long long saved = 0;
#elif defined (__arm__)
    unsigned int saved = 0;
#endif
};

bool endsWith (const std::string& s, const char* suffix)
{
    const size_t n = std::strlen (suffix);
    return s.size() >= n && s.compare (s.size() - n, n, suffix) == 0;
}

float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

constexpr int kWorkQueueCapacity = 4096;            // entries of the dependency search queues of a router (a pointer each)
constexpr int kNumOsc = 3;
constexpr int kNumLfo = 10;
constexpr int kNumMacro = 8;
constexpr int kBakedPoints = 2048; // LineGenerator::kDefaultResolution

/** Names of the engine's modulation sources, in the order of core/src/modulation.cpp (modSourceTable). */
std::string vitalSourceName (int index)
{
    if (index < 6)  return "env_" + std::to_string (index + 1);
    if (index < 14) return "lfo_" + std::to_string (index - 5);
    if (index < 18) return "random_" + std::to_string (index - 13);
    if (index < 22) return "macro_control_" + std::to_string (index - 17);
    if (index < 28)
    {
        static const char* const performance[] = { "velocity", "note", "aftertouch", "pitch_wheel", "mod_wheel", "random" };
        return performance[index - 22];
    }
    // state version 3 (modulation.cpp): macro 5 .. 8, LFO 9 and 10, lift, slide, oct note, poly aftertouch
    if (index < 32) return "macro_control_" + std::to_string (index - 28 + 5);
    if (index < 34) return "lfo_" + std::to_string (index - 32 + 9);
    static const char* const extra[] = { "lift", "slide", "note_in_octave", "poly_aftertouch" };
    return extra[index - 34];
}

/** MIDI note (fractional) of a frequency, 69 = 440 Hz. */
float noteFromHz (double hz) noexcept
{
    return static_cast<float> (69.0 + 12.0 * std::log2 (hz / 440.0));
}

/** The notes of the scales of master.scale.type (semitones above the key); index 0 (chromatic) is not in the table. */
struct ScaleDef
{
    int count;
    int steps[8];
};
constexpr ScaleDef kScales[] = {
    { 12, { 0 } },                          // chromatic (filled in by inScale ())
    { 7, { 0, 2, 4, 5, 7, 9, 11 } },        // Major
    { 7, { 0, 2, 3, 5, 7, 8, 10 } },        // Natural Minor
    { 7, { 0, 2, 3, 5, 7, 8, 11 } },        // Harmonic Minor
    { 7, { 0, 2, 3, 5, 7, 9, 11 } },        // Melodic Minor
    { 7, { 0, 2, 3, 5, 7, 9, 10 } },        // Dorian
    { 7, { 0, 1, 3, 5, 7, 8, 10 } },        // Phrygian
    { 7, { 0, 2, 4, 6, 7, 9, 11 } },        // Lydian
    { 7, { 0, 2, 4, 5, 7, 9, 10 } },        // Mixolydian
    { 7, { 0, 1, 3, 5, 6, 8, 10 } },        // Locrian
    { 5, { 0, 2, 4, 7, 9 } },               // Major Pentatonic
    { 5, { 0, 3, 5, 7, 10 } },              // Minor Pentatonic
    { 6, { 0, 3, 5, 6, 7, 10 } },           // Blues
    { 6, { 0, 2, 4, 6, 8, 10 } },           // Whole Tone
    { 8, { 0, 1, 3, 4, 6, 7, 9, 10 } },     // Diminished (H-W)
    { 8, { 0, 2, 3, 5, 6, 8, 9, 11 } },     // Diminished (W-H)
    { 7, { 0, 1, 4, 5, 7, 8, 10 } },        // Phrygian Dominant
    { 7, { 0, 2, 3, 6, 7, 8, 11 } },        // Hungarian Minor
    { 7, { 0, 1, 4, 5, 7, 8, 11 } },        // Double Harmonic
    { 5, { 0, 2, 3, 7, 8 } },               // Hirajoshi
};
constexpr int kNumScaleTypes = static_cast<int> (sizeof (kScales) / sizeof (kScales[0]));

bool inScale (int type, int pitchClass) noexcept
{
    if (type <= 0 || type >= kNumScaleTypes)
        return true;
    for (int i = 0; i < kScales[type].count; ++i)
        if (kScales[type].steps[i] == pitchClass)
            return true;
    return false;
}

/** The scale-conformed note: a note outside the scale moves to the nearest note inside it, a tie goes down (ASSUMPTION A7). */
int conformedNote (int note, int key, int type) noexcept
{
    if (type <= 0 || type >= kNumScaleTypes)
        return note;
    for (int d = 0; d < 12; ++d)
    {
        const int down = note - d, up = note + d;
        if (down >= 0 && inScale (type, (((down - key) % 12) + 12) % 12))
            return down;
        if (up <= 127 && inScale (type, (((up - key) % 12) + 12) % 12))
            return up;
    }
    return note;
}

// ---- content (setContent): everything the audio thread needs from the `content` section, built on the message thread ---------------
struct LfoCustom
{
    bool present = false;
    shapes::ShapeData shape;
    std::vector<PointMod> mods;
    std::unique_ptr<CustomShapeModulation> point; // null without a point modulation
};

/** A sample of the content (oscN.type = Sample), decoded on the message thread. */
struct SampleEntry
{
    sampleosc::SampleData data;
    sampleosc::SampleView view;
    std::vector<sampleosc::SampleData::Onset> onsets;
    std::vector<float> manual;
    bool present = false;
};

struct Snapshot
{
    std::array<LfoCustom, kNumLfo> lfo;
    std::array<std::unique_ptr<sbvital::Wavetable>, 3> userTable; // oscN.wavetable = Preset table (content.wavetables)
    std::array<int, 3> userFrames {};
    std::array<SampleEntry, 3> sample;                            // oscN.type = Sample (content.samples)
    std::array<sbvital::FilterCurveShared, 3> filterCurve;        // PZ SVF curves of filter 1, filter 2 and the FX filter (content.filter_curves)
    bool hasVelocity = false, hasNote = false;
    std::array<float, kBakedPoints> velocityTable {}, noteTable {};
    std::array<std::array<float, sbvital::Distortion::kShaperSize>, 2> shaperTable {}; // X-Shaper curves A and B (content.curves.shaper_a / shaper_b)
    std::array<bool, 2> hasShaper { { false, false } };
    std::array<std::array<float, 128>, 4> scaleNotes {}; // tuned MIDI note of every key per master.tuning.scale at A4 = 440 (NaN: unmapped)
    std::atomic<bool> retired { false };                 // the audio thread has replaced it
};

bool buildShape (const std::vector<ContentPoint>& points, bool smooth, shapes::ShapeData& out) noexcept
{
    if (points.size() < 2)
        return false;
    out = shapes::ShapeData {};
    out.numPoints = std::min<size_t> (points.size(), shapes::ShapeData::kMaxPoints);
    for (size_t i = 0; i < out.numPoints; ++i)
    {
        out.points[i].x = points[i].x;
        out.points[i].y = points[i].y;
        out.points[i].curve = points[i].power;
        out.points[i].smooth = smooth;
    }
    out.sanitize();
    return true;
}

void identityNotes (std::array<float, 128>& out) noexcept
{
    for (size_t k = 0; k < out.size(); ++k)
        out[k] = static_cast<float> (k);
}

void notesOf (const tuning::Tuning& t, std::array<float, 128>& out) noexcept
{
    for (size_t k = 0; k < out.size(); ++k)
    {
        const double f = t.table()[k];
        out[k] = f > 0.0 ? noteFromHz (f) : std::numeric_limits<float>::quiet_NaN();
    }
}

std::atomic<int> gTableVersion { 2000 };

/** The impulse response of content.impulses[0] (convolve.ir = Preset IR); null if there is none or it cannot be read. */
std::shared_ptr<const sbvital::ImpulseData> decodeImpulse (const StateContent& content, const IContentStore* store)
{
    std::vector<uint8_t> bytes;
    if (store == nullptr || content.impulses.empty() || ! store->readFile (content.impulses[0], bytes))
        return nullptr;
    std::vector<float> left, right;
    sampleosc::WavInfo info;
    if (! sampleosc::readWav (bytes.data(), bytes.size(), left, right, info) || left.empty())
        return nullptr;
    auto ir = std::make_shared<sbvital::ImpulseData>();
    ir->frames = static_cast<int> (left.size());
    ir->channels = info.numChannels >= 2 ? 2 : 1;
    ir->sample_rate = info.sampleRate > 0 ? info.sampleRate : 48000.0;
    if (ir->channels == 1)
        ir->samples = left;
    else
    {
        ir->samples.resize (left.size() * 2);
        for (size_t i = 0; i < left.size(); ++i)
        {
            ir->samples[2 * i] = left[i];
            ir->samples[2 * i + 1] = right[i];
        }
    }
    return ir;
}

/** The frames of a user wavetable file: a WAV whose length is a multiple of 2048 is that many frames; anything else is one cycle that
    is resampled to 2048 values. At most 256 frames. */
bool decodeWavetable (const std::vector<uint8_t>& bytes, std::vector<float>& frames, int& numFrames)
{
    std::vector<float> left, right;
    sampleosc::WavInfo info;
    if (! sampleosc::readWav (bytes.data(), bytes.size(), left, right, info) || left.empty())
        return false;
    const size_t n = left.size();
    if (n >= 2048 && n % 2048 == 0)
    {
        numFrames = static_cast<int> (std::min<size_t> (256, n / 2048));
        frames.assign (left.begin(), left.begin() + static_cast<std::ptrdiff_t> (numFrames) * 2048);
    }
    else
    {
        numFrames = 1;
        frames.resize (2048);
        for (size_t i = 0; i < 2048; ++i)
        {
            const double pos = static_cast<double> (i) * static_cast<double> (n) / 2048.0;
            const size_t i0 = std::min (n - 1, static_cast<size_t> (pos));
            const size_t i1 = std::min (n - 1, i0 + 1);
            const float f = static_cast<float> (pos - std::floor (pos));
            frames[i] = left[i0] * (1.0f - f) + left[i1] * f;
        }
    }
    return true;
}

std::unique_ptr<Snapshot> buildSnapshot (const StateContent& content, const IContentStore* store)
{
    auto s = std::make_unique<Snapshot>();

    if (store != nullptr)
    {
        std::vector<uint8_t> bytes;
        for (const ContentWavetable& w : content.wavetables)
        {
            std::vector<float> frames;
            int numFrames = 0;
            if (w.osc < 1 || w.osc > kNumOsc || ! store->readFile (w.ref, bytes) || ! decodeWavetable (bytes, frames, numFrames))
                continue;
            s->userTable[static_cast<size_t> (w.osc - 1)] = vitaltables::buildUserWavetable (frames.data(), numFrames, gTableVersion.fetch_add (1));
            s->userFrames[static_cast<size_t> (w.osc - 1)] = numFrames;
        }
        for (const ContentSample& c : content.samples)
        {
            if (c.osc < 1 || c.osc > kNumOsc || ! store->readFile (c.ref, bytes))
                continue;
            SampleEntry& e = s->sample[static_cast<size_t> (c.osc - 1)];
            if (! e.data.loadFromMemory (bytes.data(), bytes.size(), c.ref.file.c_str()) || ! e.data.isValid())
                continue;
            e.view = e.data.view();
            e.onsets = e.data.detectOnsets();
            e.manual = c.slices;
            e.present = true;
        }
    }

    for (const ContentLfoShape& c : content.lfoShapes)
    {
        if (c.lfo < 1 || c.lfo > kNumLfo)
            continue;
        LfoCustom& l = s->lfo[static_cast<size_t> (c.lfo - 1)];
        if (! buildShape (c.points, c.smooth, l.shape))
            continue;
        l.present = true;
        for (size_t i = 0; i < c.points.size() && i < l.shape.numPoints; ++i)
        {
            if (c.points[i].modX.present && i > 0 && i + 1 < l.shape.numPoints) // the end points stay at 0 and 1
                l.mods.push_back ({ static_cast<int> (i), true, sourceIndexOf (c.points[i].modX.source), c.points[i].modX.amount });
            if (c.points[i].modY.present)
                l.mods.push_back ({ static_cast<int> (i), false, sourceIndexOf (c.points[i].modY.source), c.points[i].modY.amount });
        }
        if (! l.mods.empty())
            l.point = std::make_unique<CustomShapeModulation> (l.shape, l.mods);
    }

    for (const ContentFilterCurve& c : content.filterCurves)
    {
        const int slot = c.filter == "filter1" ? 0 : (c.filter == "filter2" ? 1 : (c.filter == "fxfilter" ? 2 : -1));
        if (slot < 0 || c.points.empty())
            continue;
        float x[kMaxFilterCurvePoints], y[kMaxFilterCurvePoints];
        const int n = std::min (static_cast<int> (c.points.size()), kMaxFilterCurvePoints);
        for (int i = 0; i < n; ++i)
        {
            x[i] = c.points[static_cast<size_t> (i)].x;
            y[i] = c.points[static_cast<size_t> (i)].y;
        }
        s->filterCurve[static_cast<size_t> (slot)].fit (x, y, n);
    }

    shapes::ShapeData curve;
    if (buildShape (content.velocityCurve, false, curve))
    {
        curve.bake (s->velocityTable.data(), kBakedPoints);
        s->hasVelocity = true;
    }
    if (buildShape (content.noteCurve, false, curve))
    {
        curve.bake (s->noteTable.data(), kBakedPoints);
        s->hasNote = true;
    }
    for (int which = 0; which < 2; ++which)
        if (buildShape (which == 0 ? content.shaperA : content.shaperB, false, curve))
        {
            curve.bake (s->shaperTable[static_cast<size_t> (which)].data(), sbvital::Distortion::kShaperSize);
            s->hasShaper[static_cast<size_t> (which)] = true;
        }

    // the four tuning scales (master.tuning.scale): 12-TET, Pythagorean, just intonation (5-limit), the user file
    identityNotes (s->scaleNotes[0]);
    const char* const pythagorean = "! pythagorean.scl\nPythagorean 12-tone\n 12\n 256/243\n 9/8\n 32/27\n 81/64\n 4/3\n 729/512\n 3/2\n 128/81\n 27/16\n 16/9\n 243/128\n 2/1\n";
    const char* const just = "! just.scl\n5-limit just intonation\n 12\n 16/15\n 9/8\n 6/5\n 5/4\n 4/3\n 45/32\n 3/2\n 8/5\n 5/3\n 9/5\n 15/8\n 2/1\n";
    for (int which = 1; which <= 2; ++which)
    {
        tuning::Tuning t;
        const std::string text = which == 1 ? pythagorean : just;
        if (t.loadScl (text).ok())
            notesOf (t, s->scaleNotes[static_cast<size_t> (which)]);
        else
            identityNotes (s->scaleNotes[static_cast<size_t> (which)]);
    }
    identityNotes (s->scaleNotes[3]);
    if (content.tuning.present)
    {
        tuning::Tuning t;
        bool ok = t.loadScl (content.tuning.scl).ok();
        if (ok && content.tuning.hasKbm)
            ok = t.loadKbm (content.tuning.kbm).ok();
        if (ok)
            notesOf (t, s->scaleNotes[3]);
    }
    return s;
}

class VitalEngine final : public IEngine
{
public:
    VitalEngine()
    {
        const auto* table = paramTable();
        for (size_t i = 0; i < kMaxParams; ++i)
            params[i].store (i < paramCount() ? table[i].def : 0.0f, std::memory_order_relaxed);
        findParamIndex (0); // builds the id lookup now, not on the first setParam
        setQualityValues (Quality {});
        identityNotes (noteTable);
        for (auto& channelKeys : activeKey)
            channelKeys.fill (-1);
        for (int i = 0; i < kNumConform; ++i)
            conform[static_cast<size_t> (i)] = static_cast<int8_t> (i);
        setContent (StateContent {}); // the built-in tuning scales
    }

    void prepare (double newSampleRate, int /*maxBlockFrames*/, const Quality& q) override
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        setQualityValues (q);
        build();
        reset();
    }

    void setQuality (const Quality& q) noexcept override
    {
        setQualityValues (q);
    }

    void reset() override
    {
        if (engine == nullptr)
            return;

        ScopedFlushDenormals denormals;
        engine->allSoundsOff();
        for (int channel = 0; channel < numChannels; ++channel)
        {
            engine->setPitchWheel (0.0f, channel); // bends survive allSoundsOff() inside the voice handler
            engine->setChannelPitchBend (channel, 0.0f);
            engine->setZonedPitchWheel (0.0f, channel, channel);
            engine->setModWheel (0.0f, channel);
            rawBend[static_cast<size_t> (channel)] = 0.0f;
        }
        for (auto& channelKeys : activeKey)
            channelKeys.fill (-1);
        pedalChannels = 0;
        noteGroup->held = false;
        ++noteGroup->group;
        noteIds.fill (NoteIdSlot {});
        clockSeconds = 0.0;
        lastModWheel = lastAftertouch = 0.0f;
        lastBend = 0.0f;
        takeSnapshot();
        applyParams (true);
        // let every smoother in the engine settle on the (possibly changed) targets while nothing sounds
        for (int i = 0; i < kSettleChunks; ++i)
            runEngine (kChunk);
        engine->allSoundsOff();
        activeCount.store (0, std::memory_order_relaxed);
        latencyCache.store (engine->latencyFrames(), std::memory_order_relaxed);
    }

    void setParam (uint32_t id, float plain) noexcept override
    {
        const int i = findParamIndex (id);
        if (i < 0 || std::isnan (plain))
            return;
        params[static_cast<size_t> (i)].store (constrainValue (paramTable()[i], plain), std::memory_order_relaxed);
    }

    float getParam (uint32_t id) const noexcept override
    {
        const int i = findParamIndex (id);
        return i >= 0 ? params[static_cast<size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }

    bool supportsParam (uint32_t id) const noexcept override
    {
        const int i = findParamIndex (id);
        return i >= 0 && capsCover (capabilities(), requiredCaps (static_cast<size_t> (i))); // the keys of state version 3 need their capability bits
    }

    void process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept override
    {
        if (left == nullptr || numFrames <= 0)
            return;

        if (engine == nullptr)
        {
            std::fill (left, left + numFrames, 0.0f);
            if (right != nullptr)
                std::fill (right, right + numFrames, 0.0f);
            return;
        }

        ScopedFlushDenormals denormals;
        takeSnapshot();
        if (transport.bpm != engineBpm)
        {
            engine->setBpm (static_cast<float> (transport.bpm));
            engineBpm = transport.bpm;
        }
        applyParams (false);

        // Vital's note triggers are only sample accurate at the start of a processed chunk (the "sample" offset of
        // noteOn/noteOff does not delay the voice's audio, measured), so the block is cut at every event frame: events
        // are always delivered at offset 0 of a chunk, and chunks are at most kMaxBufferSize long.
        int pos = 0;
        int next = 0; // first event not yet delivered
        while (pos < numFrames)
        {
            while (next < numEvents && std::clamp (events[next].frame, pos, numFrames) <= pos)
                handleEvent (events[next++]);

            int end = std::min (pos + kChunk, numFrames);
            if (next < numEvents)
                end = std::min (end, std::clamp (events[next].frame, pos, numFrames));

            updateClocks (pos);
            slewTune (end - pos);
            runEngine (end - pos);
            clockSeconds += static_cast<double> (end - pos) / sampleRate;
            writeOutput (left + pos, right != nullptr ? right + pos : nullptr, end - pos);
            pos = end;
        }

        // events at or beyond the end of the block (out of range input) take effect at the start of the next block
        while (next < numEvents)
            handleEvent (events[next++]);

        activeCount.store (engine->getNumActiveVoices(), std::memory_order_relaxed);
        latencyCache.store (engine->latencyFrames(), std::memory_order_relaxed);
    }

    EngineCapsMask capabilities() const noexcept override
    {
        // a bit is set only for an area that has a test rendering the key on and off (core/engine-vital/tests/test_wp123.cpp,
        // core/tests/test_events.cpp); docs/ui-design/WP123-REPORT.md lists what each bit covers and what it does not
        return kCapEventContract | kCapMacros58 | kCapLfo910 | kCapModSourcesExtra | kCapAuxSource | kCapLfoOptions | kCapEnvBeats | kCapCurves
             | kCapCustomLfoShape | kCapKeyScale | kCapTuning | kCapMpe | kCapOscMapping | kCapSampleOsc | kCapUserWavetable | kCapWarp2 | kCapMixerGraph | kCapFxRacks | kCapFilterExtras | kCapFilterModels | kCapReverbForce
             | kCapFxUtility | kCapFxHyperDimension | kCapFxBode | kCapFxConvolve | kCapFxSplitters
             | kCapFxExtrasAll; // block B17: proven by core/engine-vital/tests/test_fxextra.cpp
    }

    void setTransport (const TransportInfo& t) noexcept override
    {
        transport.bpm = std::isfinite (t.bpm) && t.bpm >= 1.0 && t.bpm <= 999.0 ? t.bpm : 120.0;
        transport.songPosBeats = std::isfinite (t.songPosBeats) ? t.songPosBeats : 0.0;
        transport.playing = t.playing;
    }

    void setTuningSource (const tuning::ITuningSource* source) noexcept override
    {
        tuningSource.store (source, std::memory_order_release);
    }

    void setContent (const StateContent& content) override
    {
        {
            const std::lock_guard<std::mutex> lock (snapshotMutex);
            lastContent = content;
        }
        publishContent (content);
    }

    void setContentStore (const IContentStore* store) override
    {
        StateContent content;
        {
            const std::lock_guard<std::mutex> lock (snapshotMutex);
            contentStore = store;
            content = lastContent;
        }
        publishContent (content);
    }

    void publishContent (const StateContent& content)
    {
        const IContentStore* store;
        {
            const std::lock_guard<std::mutex> lock (snapshotMutex);
            store = contentStore;
        }
        auto snapshot = buildSnapshot (content, store);
        auto impulseData = decodeImpulse (content, store);
        const std::lock_guard<std::mutex> lock (snapshotMutex);
        impulse = impulseData;
        if (engine != nullptr)
            sbvital::setConvolveImpulse (engine->getEffectModule (sbvital::ReorderableEffectChain::kConvolve), impulse); // atomics only
        // snapshots the audio thread has replaced are done with: free them. The one it is using stays until it is replaced.
        for (auto it = snapshots.begin(); it != snapshots.end();)
            it = (*it)->retired.load (std::memory_order_acquire) ? snapshots.erase (it) : it + 1;
        Snapshot* raw = snapshot.get();
        snapshots.push_back (std::move (snapshot));
        Snapshot* previous = pendingSnapshot.exchange (raw, std::memory_order_acq_rel);
        if (previous != nullptr) // never picked up by the audio thread: nobody holds it
            for (auto it = snapshots.begin(); it != snapshots.end(); ++it)
                if (it->get() == previous)
                {
                    snapshots.erase (it);
                    break;
                }
    }

    int activeVoices() const noexcept override
    {
        return activeCount.load (std::memory_order_relaxed);
    }

    int latencySamples() const noexcept override
    {
        return latencyCache.load (std::memory_order_relaxed);
    }

    bool configured() const noexcept { return engine != nullptr && controlsComplete; }

    double controlValue (const char* name) const noexcept
    {
        const auto it = controlMap.find (name);
        return it != controlMap.end() && it->second != nullptr ? static_cast<double> (it->second->value()) : std::nan ("");
    }

    bool hasModulationDestination (size_t paramIndex) const noexcept
    {
        const size_t choice = modDestinationChoiceOf (paramIndex);
        return choice < destinations.size() && destinations[choice].valid;
    }

    int connectedSlots() const noexcept
    {
        int n = 0;
        for (const auto& s : slots)
            n += s.connected ? 1 : 0;
        return n;
    }

private:
    static constexpr int numChannels = 16;
    static constexpr int kNumSlots = kNumModSlots;

    struct Resolved
    {
        const Binding* binding = nullptr;
        sbvital::Value* control = nullptr;
        double lo = 0.0, hi = 0.0;
        bool wasActive = true;  // gated bindings (envelope time units): active in the last block
        bool isTune = false;    // master.tune: the concert pitch offset is added to this control
    };

    struct Destination
    {
        bool valid = false;
        sbvital::Processor* mono = nullptr;
        sbvital::Processor* poly = nullptr;
        sbvital::ValueSwitch* monoSwitch = nullptr;
        sbvital::ValueSwitch* polySwitch = nullptr;
        float scale = 1.0f;
        int gateParam = -1, gateValue = 0; // an envelope time: live only while envN.units has this value
    };

    struct Slot
    {
        int source = 0;       // choice value applied
        int destination = 0;
        bool connected = false;
        uint32_t triedGeneration = 0;   // matrixGeneration at the last attempt to wire the slot
        sbvital::ModulationConnectionProcessor* processor = nullptr;
        int sourceParam = -1, destinationParam = -1;
        bool eligible = true;          // the destination is live (an envelope time of the selected unit)
        int auxSourceParam = -1, auxInvParam = -1, auxPowerParam = -1, auxOutputParam = -1;
        int auxPlugged = 0;            // aux source choice that is plugged into the processor
        int curveKind = 0;             // 0: linear remap, 1: velocity curve, 2: note curve
        uint32_t curveId = 0;          // snapshot the curve comes from
    };

    // ---- quality hand-off (any thread -> audio thread) -------------------------------------------------------------
    void setQualityValues (const Quality& q) noexcept
    {
        qVoices.store (std::clamp (q.maxVoices, 1, kMaxVoices), std::memory_order_relaxed);
        qUnison.store (std::clamp (q.maxUnison, 1, kMaxUnisonVoices), std::memory_order_relaxed);
        qOversampling.store (q.oversampling >= 2 ? 2 : 1, std::memory_order_relaxed);
        qInterpolation.store (q.interpolation == Interpolation::spectral ? 1 : 0, std::memory_order_relaxed);
        qFxMask.store (q.fxMask & kFxAll, std::memory_order_relaxed);
        qRealtime.store (q.realtime, std::memory_order_relaxed);
        if (engine != nullptr) // an atomic flag of the module; takes effect at once
            sbvital::setConvolveRealtime (engine->getEffectModule (sbvital::ReorderableEffectChain::kConvolve), q.realtime);
        qGeneration.fetch_add (1, std::memory_order_release);
    }

    sbvital::Value* control (const std::string& name)
    {
        const auto it = controlMap.find (name);
        if (it == controlMap.end() || it->second == nullptr)
        {
            controlsComplete = false;
            return nullptr;
        }
        return it->second;
    }

    void setControl (const std::string& name, float value)
    {
        if (auto* c = control (name))
            c->set (value);
    }

    void build()
    {
        controlsComplete = true;
        resolved.clear();
        destinations.clear();
        slots.fill (Slot {});
        for (auto& v : applied)
            v = std::numeric_limits<float>::quiet_NaN();
        appliedWavetable.fill (-1);
        userFramesNow.fill (0);
        appliedSliceKey.fill (~0ull);
        retireAfterApply = nullptr;
        appliedShape.fill (-1);
        appliedCustomId.fill (0);
        appliedMap.fill ({ -1, -1, -1, -1, -1, -1 });
        appliedRouting = appliedFxOrder = -1.0f;
        engineBpm = -1.0;
        appliedBpm = -1.0;
        clockSeconds = 0.0;
        snapshotId = 0;
        curveId = 0;
        tuningStateId = ~0u;
        wasMts = false;
        mpeApplied = -1;
        conformKey = conformType = -1;
        for (auto& v : appliedModValues)
            v.fill (0.0f);
        appliedQualityGeneration = ~0u;
        engine.reset();
        tuning.reset();
        if (tables == nullptr)
            tables = std::make_unique<vitaltables::WavetableSet>(); // the built-in wavetables: built once per engine object

        // Every random generator of the engine (random LFOs, the noise sample, ...) is seeded from a counter: start it at 0 for this
        // engine, after the one process-wide table that also takes a number, so that two engines of the same patch sound the same.
        sbvital::RandomValues::instance();
        sbvital::utils::RandomGenerator::next_seed_ = 0;
        engine = std::make_unique<sbvital::SoundEngine>();
        tuning = std::make_unique<Tuning>(); // default 12-tone equal temperament
        engine->setTuning (tuning.get());

        for (int i = 0; i < kNumOsc; ++i)
        {
            if (sbvital::Wavetable* wavetable = engine->getWavetable (i))
                wavetable->useExternalData (tables->data (kWavetableSaw));
            sampleShared[static_cast<size_t> (i)] = engine->getSampleShared (i);
        }
        for (int i = 0; i < 3; ++i)
        {
            curveShared[static_cast<size_t> (i)] = engine->getFilterCurve (i);
            appliedCurveId[static_cast<size_t> (i)] = ~0u;
        }
        distortionModule = dynamic_cast<sbvital::DistortionModule*> (engine->getEffectModule (3)); // constants::kDistortion
        appliedShaperId = ~0u;

        {
            const std::lock_guard<std::mutex> lock (snapshotMutex);
            sbvital::setConvolveImpulse (engine->getEffectModule (sbvital::ReorderableEffectChain::kConvolve), impulse);
        }
        sbvital::setConvolveRealtime (engine->getEffectModule (sbvital::ReorderableEffectChain::kConvolve), qRealtime.load (std::memory_order_relaxed));
        engine->setSampleRate (static_cast<int> (std::lround (sampleRate)));
        engine->setBpm (120.0f);
        controlMap = engine->getControls();
        // the oversampling of the tier is fixed for the life of the buffers (IEngine::setQuality)
        setControl ("oversampling", qOversampling.load() >= 2 ? 1.0f : 0.0f);
        engine->checkOversampling();
        engine->updateAllModulationSwitches();
        controlMap = engine->getControls();

        applyFixedControls();
        for (int i = 0; i < kNumOsc; ++i)
            vSmooth[static_cast<size_t> (i)] = control ("osc_" + std::to_string (i + 1) + "_smooth_interpolation");
        for (int i = 0; i < kNumLfo; ++i)
        {
            vLfoSyncType[static_cast<size_t> (i)] = control ("lfo_" + std::to_string (i + 1) + "_sync_type");
            vLfoHostLock[static_cast<size_t> (i)] = control ("lfo_" + std::to_string (i + 1) + "_host_lock");
        }
        for (int i = 0; i < kNumOsc; ++i)
            vOscMapWarp[static_cast<size_t> (i)] = control ("osc_" + std::to_string (i + 1) + "_map_warp");
        engine->setNoteTable (noteTable.data());
        vFilter1Input = control ("filter_1_filter_input");
        vFilter2Input = control ("filter_2_filter_input");
        vFxOrder = control ("effect_chain_order");
        for (int i = 0; i < static_cast<int> (kNumFxSlots); ++i)
            vFxOrderN[static_cast<size_t> (i)] = control ("fx_order_" + std::to_string (i + 1));
        resolveBindings();
        resolveModulation();
        for (int i = 0; i < kNumLfo; ++i)
        {
            lfoLines[static_cast<size_t> (i)] = engine->getLfoSource (i);
            lfoProcessors[static_cast<size_t> (i)] = engine->getLfo (i);
            lfoStates[static_cast<size_t> (i)] = lfoProcessors[static_cast<size_t> (i)]->pointState();
            lfoStates[static_cast<size_t> (i)]->budget = pointBudget;
            lfoStates[static_cast<size_t> (i)]->note_group = noteGroup;
        }

        takeSnapshot();
        applyParams (true);

        // Warm-up: the first notes an engine plays differ slightly from all later ones (measured: the very first note
        // has a different attack, one more note makes the engine behave identically from then on), and lazily built
        // tables inside the DSP (FFT plans, lookups, ...) must not be built on the audio thread. Two short notes are
        // rendered here; reset() then silences everything again.
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            engine->noteOn (60, 1.0f, 0, 0);
            for (int i = 0; i < 8; ++i)
                runEngine (kChunk);
            engine->noteOff (60, 0.0f, 0, 0);
            for (int i = 0; i < 8; ++i)
                runEngine (kChunk);
            engine->allSoundsOff();
            runEngine (kChunk);
        }
    }

    /** Engine controls that no parameter drives: fixed for every patch. */
    void applyFixedControls()
    {
        setControl ("pitch_bend_range", 0.0f); // bends go through the per-channel path (handleEvent); this one is the zoned wheel
        setControl ("mpe_enabled", 0.0f);
        setControl ("bypass", 0.0f);
        setControl ("stereo_routing", 1.0f);
        setControl ("stereo_mode", 0.0f);
        setControl ("voice_amplitude", 1.0f);
        for (int i = 1; i <= kNumOsc; ++i)
        {
            const std::string c = "osc_" + std::to_string (i) + "_";
            setControl (c + "midi_track", 1.0f);
            setControl (c + "spectral_unison", 1.0f);
            setControl (c + "transpose_quantize", 0.0f);
        }
        setControl ("sample_random_phase", 0.0f);
        setControl ("sample_loop", 1.0f);
        setControl ("sample_bounce", 0.0f);
        setControl ("sample_keytrack", 0.0f);
        setControl ("sample_transpose", 0.0f);
        setControl ("sample_tune", 0.0f);
        setControl ("sample_transpose_quantize", 0.0f);
    }

    void resolveBindings()
    {
        for (const auto& binding : vitalBindings())
        {
            Resolved r;
            r.binding = &binding;
            r.control = control (binding.control);
            const ControlRange range = vitalControlRange (binding.control);
            if (r.control == nullptr || ! range.exists || binding.param >= paramCount())
            {
                controlsComplete = false;
                continue;
            }
            r.lo = range.min;
            r.hi = range.max;
            r.isTune = binding.control == "voice_transpose"; // the concert pitch (master.tuning.a4) is added to the transposition
            resolved.push_back (r);
        }

        for (size_t i = 0; i < paramCount(); ++i)
        {
            const std::string key = paramTable()[i].key;
            const auto number = [&] (size_t from) { return std::atoi (key.c_str() + from); };
            // the engine implements 3 oscillators, 8 LFOs and the 9 effect slots of state version 2; the keys of osc/LFO/slot numbers
            // beyond (lfo9, lfo10, fx.order.10 ..) are stored and ignored until a later work package gives them a meaning
            if (endsWith (key, ".wavetable"))
                wavetableParam[static_cast<size_t> (number (3) - 1)] = static_cast<int> (i); // "osc1.wavetable": the digit at 3
            else if (endsWith (key, ".shape"))
            {
                if (number (3) <= kNumLfo)
                    shapeParam[static_cast<size_t> (number (3) - 1)] = static_cast<int> (i);    // "lfo3.shape"
            }
            else if (key == "filter.routing")
                routingParam = static_cast<int> (i);
            else if (key.compare (0, 9, "fx.order.") == 0)
            {
                if (number (9) >= 1 && number (9) <= static_cast<int> (kNumFxSlots))
                    fxOrderParam[static_cast<size_t> (number (9) - 1)] = static_cast<int> (i);
            }
            else if (key == "master.bend_range")
                bendRangeParam = static_cast<int> (i);
        }

        // state version 3 parameters the adapter reads itself
        const auto find = [] (const std::string& key) { const ParamInfo* info = findParamByKey (key.c_str()); return info != nullptr ? findParamIndex (info->id) : -1; };
        for (int n = 1; n <= kNumLfo; ++n)
        {
            const std::string k = "lfo" + std::to_string (n) + ".";
            lfoMonoParam[static_cast<size_t> (n - 1)] = find (k + "mono");
            lfoHostParam[static_cast<size_t> (n - 1)] = find (k + "host");
            lfoSyncParam[static_cast<size_t> (n - 1)] = find (k + "sync_type");
        }
        for (int n = 1; n <= kNumMacro; ++n)
            macroParam[static_cast<size_t> (n - 1)] = find ("macro." + std::to_string (n));
        for (int n = 1; n <= kNumOsc; ++n)
        {
            const std::string k = "osc" + std::to_string (n) + ".map.";
            const char* const names[6] = { "key_low", "key_high", "vel_low", "vel_high", "key_fold", "key_warp" };
            for (int m = 0; m < 6; ++m)
                mapParam[static_cast<size_t> (n - 1)][static_cast<size_t> (m)] = find (k + names[m]);
        }
        {
            static const char* const names[kSpCount] = { "type", "sample.start", "sample.end", "sample.root", "sample.keytrack", "sample.loop_mode",
                "sample.loop_start", "sample.loop_end", "sample.loop_xfade", "sample.loop_relative", "sample.loop_exit", "sample.fade_edges",
                "sample.normalize", "sample.reverse", "sample.slice_mode", "sample.slice_threshold", "sample.slice_play", "sample.slice_root",
                "sample.scan_range", "sample.scan_reverse", "sample.scan_lock_tempo", "sample.scan_to_bpm", "sample.length_beats" };
            for (int n = 1; n <= kNumOsc; ++n)
            {
                const std::string k = "osc" + std::to_string (n) + ".";
                for (int w = 0; w < kSpCount; ++w)
                    sampleParams[static_cast<size_t> (n - 1)][static_cast<size_t> (w)] = find (k + names[w]);
                frameParams[static_cast<size_t> (n - 1)][0] = find (k + "frame");
                frameParams[static_cast<size_t> (n - 1)][1] = find (k + "frame_spread");
            }
        }
        curveVelocityLegatoParam = find ("curve.velocity.legato");
        curveNoteLegatoParam = find ("curve.note.legato");
        portamentoParam = find ("master.portamento.time");
        scaleKeyParam = find ("master.scale.key");
        scaleTypeParam = find ("master.scale.type");
        scaleModeParam = find ("master.scale.mode");
        a4Param = find ("master.tuning.a4");
        tuningScaleParam = find ("master.tuning.scale");
        mtsParam = find ("master.tuning.mts_esp");
        mtsNoteOnParam = find ("master.tuning.mts_note_on_only");
        mpeOnParam = find ("master.mpe.on");
        mpeRangeParam = find ("master.mpe.bend_range");
        reverbForceParam = find ("reverb.force_enable");
    }

    /** The tables of the modulation matrix: sources, destinations, the 64 connection processors. Everything the audio thread needs
        to wire a slot is looked up here (the lookups take std::string and walk maps: allocation, not for process()). */
    void resolveModulation()
    {
        for (size_t k = 0; k < static_cast<size_t> (kNumModSources); ++k)
            sourceOutput[k] = engine->getModulationSource (vitalSourceName (static_cast<int> (k)));

        destinations.assign (modDestinationCount() + 1, Destination {});
        for (size_t choice = 1; choice <= modDestinationCount(); ++choice)
        {
            const auto paramIndex = static_cast<size_t> (modDestinationParamIndex (choice));
            const std::string name = vitalControlOf (paramIndex);
            Destination& d = destinations[choice];
            d.mono = name.empty() ? nullptr : engine->getMonoModulationDestination (name);
            d.poly = name.empty() ? nullptr : engine->getPolyModulationDestination (name);
            d.monoSwitch = name.empty() ? nullptr : engine->getMonoModulationSwitch (name);
            d.polySwitch = name.empty() ? nullptr : engine->getPolyModulationSwitch (name);
            d.valid = d.mono != nullptr && d.monoSwitch != nullptr && sbvital::Parameters::isParameter (name);
            for (const Binding& b : vitalBindings())
                if (b.param == paramIndex && b.gateParam >= 0)
                {
                    d.gateParam = b.gateParam;
                    d.gateValue = b.gateValue;
                    break;
                }
            if (d.valid)
            {
                // room for every slot to be wired to this destination at the same time: wiring a slot then reuses a spare input
                // and allocates nothing (Processor::plugNext allocates when it finds no free input)
                d.mono->reserveInputs (kNumSlots);
                if (d.poly != nullptr)
                    d.poly->reserveInputs (kNumSlots);
                // a full sweep of the destination's own range (the frame positions of our built-in tables span 64 frames)
                d.scale = sbvital::Parameters::getParameterRange (name);
                if (name.size() > 10 && name.compare (name.size() - 10, 10, "wave_frame") == 0)
                    d.scale = static_cast<float> (vitaltables::kMorphFrames - 1);
                else if (name.size() > 12 && name.compare (name.size() - 12, 12, "frame_spread") == 0)
                    d.scale = 2.0f * static_cast<float> (vitaltables::kMorphFrames - 1);
            }
        }

        // the search for dependencies of a connection uses work queues that grow on demand: grow them now, on every router that a
        // destination sits in or below
        std::set<sbvital::ProcessorRouter*> routers;
        for (const Destination& d : destinations)
            for (sbvital::Processor* p : { d.mono, d.poly })
                for (sbvital::ProcessorRouter* r = p != nullptr ? p->router() : nullptr; r != nullptr; r = r->router())
                    routers.insert (r);
        for (sbvital::ProcessorRouter* r : routers)
            r->reserveWorkspace (kWorkQueueCapacity);

        auto& bank = engine->getModulationBank();
        for (int k = 0; k < kNumSlots; ++k)
        {
            const ModSlotParams p = modSlotParams (k);
            Slot& s = slots[static_cast<size_t> (k)];
            s.processor = bank.atIndex (k)->modulation_processor.get();
            s.sourceParam = p.source;
            s.destinationParam = p.destination;
            s.auxSourceParam = p.auxSource;
            s.auxInvParam = p.auxInv;
            s.auxPowerParam = p.auxPower;
            s.auxOutputParam = p.auxOutput;
        }
    }

    // ---- parameters -> controls ------------------------------------------------------------------------------------
    void applyParams (bool force) noexcept
    {
        const uint32_t generation = qGeneration.load (std::memory_order_acquire);
        const bool qualityChanged = generation != appliedQualityGeneration;
        if (qualityChanged)
        {
            quality.maxVoices = qVoices.load (std::memory_order_relaxed);
            quality.maxUnison = qUnison.load (std::memory_order_relaxed);
            quality.interpolation = qInterpolation.load (std::memory_order_relaxed) != 0 ? Interpolation::spectral : Interpolation::linear;
            quality.fxMask = qFxMask.load (std::memory_order_relaxed);
            for (sbvital::Value* v : vSmooth)
                if (v != nullptr)
                    v->set (quality.interpolation == Interpolation::spectral ? 1.0f : 0.0f);
            appliedQualityGeneration = generation;
        }

        applyScaleAndTuning (force); // first: the concert pitch it computes is added to the transposition below
        applyWavetables();           // before the bindings: the frame scale of a user table

        const bool bpmChanged = appliedBpm != transport.bpm;
        // Q27 / WP8: "reverb.force_enable" keeps the reverb running although the tier masks it out (the user trades polyphony for it)
        const bool reverbForce = reverbForceParam >= 0 && params[static_cast<size_t> (reverbForceParam)].load (std::memory_order_relaxed) > 0.5f;
        const bool forceChanged = reverbForce != appliedReverbForce;
        appliedReverbForce = reverbForce;
        const uint32_t fxMask = effectiveFxMask (quality.fxMask, reverbForce);
        for (Resolved& r : resolved)
        {
            const Binding& b = *r.binding;
            bool active = true;
            if (b.gateParam >= 0) // an envelope time of the unit that envN.units has not selected writes nothing
                active = (params[static_cast<size_t> (b.gateParam)].load (std::memory_order_relaxed) > 0.5f) == (b.gateValue > 0);
            const bool activated = active && ! r.wasActive;
            r.wasActive = active;
            if (! active)
                continue;

            const float plain = params[b.param].load (std::memory_order_relaxed);
            const bool capped = b.cap != Cap::none;
            const bool tempoDependent = b.conv == Conv::quarticBeats && bpmChanged;
            if (! force && plain == applied[b.param] && ! (capped && (qualityChanged || forceChanged)) && ! activated && ! tempoDependent)
                continue;
            applied[b.param] = plain;

            double value = convertParam (b, static_cast<double> (plain), sampleRate, transport.bpm);
            if (b.frameOsc > 0 && userFramesNow[static_cast<size_t> (b.frameOsc - 1)] > 1) // the frame position runs over the frames of the user table
                value *= static_cast<double> (userFramesNow[static_cast<size_t> (b.frameOsc - 1)] - 1) / static_cast<double> (vitaltables::kMorphFrames - 1);
            switch (b.cap)
            {
                case Cap::none:   break;
                case Cap::voices: value = std::min (value, static_cast<double> (quality.maxVoices)); break;
                case Cap::unison: value = std::min (value, static_cast<double> (quality.maxUnison)); break;
                case Cap::fx:     value = (value > 0.5 && (fxMask & (1u << b.capArg)) != 0) ? 1.0 : 0.0; break;
            }
            const double clamped = std::clamp (value, r.lo, r.hi);
            if (r.isTune)
            {
                tuneBase = clamped;
                r.control->set (static_cast<float> (clamped + a4Current));
            }
            else
                r.control->set (static_cast<float> (clamped));
        }
        appliedBpm = transport.bpm;

        applySpecials();
        applySamples();
        applyFilterCurves();
        applyShaperCurves();
        if (retireAfterApply != nullptr) // nothing references the replaced snapshot any more
        {
            retireAfterApply->retired.store (true, std::memory_order_release);
            retireAfterApply = nullptr;
        }
        for (int k = 0; k < kNumSlots; ++k)
            updateSlot (k);
    }

    /** oscN.wavetable: a built-in table, or the user table of the content (Preset table; the saw when the content has none). */
    void applyWavetables() noexcept
    {
        for (int i = 0; i < kNumOsc; ++i)
        {
            const int p = wavetableParam[static_cast<size_t> (i)];
            if (p < 0)
                continue;
            int index = static_cast<int> (params[static_cast<size_t> (p)].load (std::memory_order_relaxed) + 0.5f);
            sbvital::Wavetable* user = nullptr;
            int frames = 0;
            if (index >= kNumBuiltinWavetables)
            {
                if (activeSnapshot != nullptr && activeSnapshot->userTable[static_cast<size_t> (i)] != nullptr)
                {
                    user = activeSnapshot->userTable[static_cast<size_t> (i)].get();
                    frames = activeSnapshot->userFrames[static_cast<size_t> (i)];
                }
                else
                    index = kWavetableSaw; // "Preset table" without a table in the content: the documented fallback is the saw
            }
            const int key = user != nullptr ? 100000 + static_cast<int> (snapshotId) : index;
            if (key != appliedWavetable[static_cast<size_t> (i)])
            {
                appliedWavetable[static_cast<size_t> (i)] = key;
                if (auto* wavetable = engine->getWavetable (i))
                    wavetable->useExternalData (user != nullptr ? const_cast<sbvital::Wavetable::WavetableData*> (user->getAllData()) : tables->data (index));
            }
            if (frames != userFramesNow[static_cast<size_t> (i)])
            {
                userFramesNow[static_cast<size_t> (i)] = frames;
                for (int idx : frameParams[static_cast<size_t> (i)])
                    if (idx >= 0)
                        applied[static_cast<size_t> (idx)] = std::numeric_limits<float>::quiet_NaN(); // the frame scale changed: convert again
            }
        }
    }

    /** The drawn curves of the PZ SVF of the content, handed to the filters (a copy of a few hundred bytes when the content changed). */
    void applyFilterCurves() noexcept
    {
        for (int i = 0; i < 3; ++i)
        {
            sbvital::FilterCurveShared* sh = curveShared[static_cast<size_t> (i)];
            if (sh == nullptr || appliedCurveId[static_cast<size_t> (i)] == snapshotId)
                continue;
            appliedCurveId[static_cast<size_t> (i)] = snapshotId;
            if (activeSnapshot != nullptr)
                *sh = activeSnapshot->filterCurve[static_cast<size_t> (i)];
            else
                sh->num_bells = 0;
        }
    }

    /** The X-Shaper curves of the content, handed to the distortion (a copy of two small tables when the content changed). */
    void applyShaperCurves() noexcept
    {
        if (distortionModule == nullptr || appliedShaperId == snapshotId)
            return;
        appliedShaperId = snapshotId;
        const bool a = activeSnapshot != nullptr && activeSnapshot->hasShaper[0], b = activeSnapshot != nullptr && activeSnapshot->hasShaper[1];
        distortionModule->setShaperCurves (a ? activeSnapshot->shaperTable[0].data() : nullptr, b ? activeSnapshot->shaperTable[1].data() : nullptr);
    }

    /** The settings and the sample of every oscillator slot, handed to the sample oscillators (they read them on this thread). */
    void applySamples() noexcept
    {
        static constexpr float kFadeMs[9] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f };
        for (int i = 0; i < kNumOsc; ++i)
        {
            sbvital::SampleOscShared* sh = sampleShared[static_cast<size_t> (i)];
            if (sh == nullptr)
                continue;
            const auto& ix = sampleParams[static_cast<size_t> (i)];
            const auto P = [&] (int what) { const int idx = ix[static_cast<size_t> (what)]; return idx >= 0 ? params[static_cast<size_t> (idx)].load (std::memory_order_relaxed) : 0.0f; };
            const SampleEntry* entry = activeSnapshot != nullptr && activeSnapshot->sample[static_cast<size_t> (i)].present ? &activeSnapshot->sample[static_cast<size_t> (i)] : nullptr;
            // The view stays set while the type is Wavetable: the sample oscillator keeps playing during the 10 ms crossfade of a type change
            // (OscillatorModule); Sample without a sample in the content: a silent oscillator
            sh->view = entry != nullptr ? &entry->view : nullptr;
            sh->normalize_gain = entry != nullptr ? entry->data.normalizeGain() : 1.0f;
            sh->start = P (kSpStart) * 0.01f;
            sh->end = P (kSpEnd) * 0.01f;
            sh->root = static_cast<int> (P (kSpRoot) + 0.5f);
            sh->keytrack = P (kSpKeytrack) > 0.5f;
            sh->loop_mode = static_cast<int> (P (kSpLoopMode) + 0.5f);
            sh->loop_start = P (kSpLoopStart) * 0.01f;
            sh->loop_end = P (kSpLoopEnd) * 0.01f;
            sh->loop_xfade_ms = P (kSpLoopXfade);
            sh->loop_relative = P (kSpLoopRelative) > 0.5f;
            sh->loop_exit = P (kSpLoopExit) > 0.5f;
            sh->fade_ms = kFadeMs[std::max (0, std::min (8, static_cast<int> (P (kSpFadeEdges) + 0.5f)))];
            sh->normalize = P (kSpNormalize) > 0.5f;
            sh->reverse = P (kSpReverse) > 0.5f;
            sh->slice_mode = static_cast<int> (P (kSpSliceMode) + 0.5f);
            sh->slice_play = static_cast<int> (P (kSpSlicePlay) + 0.5f);
            sh->slice_root = static_cast<int> (P (kSpSliceRoot) + 0.5f);
            sh->scan_range = static_cast<int> (P (kSpScanRange) + 0.5f);
            sh->scan_reverse = P (kSpScanReverse) > 0.5f;
            sh->scan_lock_tempo = P (kSpScanLock) > 0.5f;
            sh->scan_to_bpm = P (kSpScanToBpm) > 0.5f;
            sh->length_beats = P (kSpLengthBeats);
            sh->bpm = transport.bpm;
            sh->quality = qOversampling.load (std::memory_order_relaxed) < 2 ? 0 : (quality.interpolation == Interpolation::spectral ? 2 : 1);
            sh->max_unison = quality.maxUnison;

            // the slice list: auto = the onsets of the file stronger than the threshold, manual = the positions of the content
            const float threshold = P (kSpSliceThreshold);
            const uint64_t key = (static_cast<uint64_t> (snapshotId) << 32) | (static_cast<uint64_t> (sh->slice_mode) << 24)
                               | static_cast<uint64_t> (std::lround (threshold * 10.0f) + 1) | (entry != nullptr ? 1ull << 40 : 0ull);
            if (key != appliedSliceKey[static_cast<size_t> (i)])
            {
                appliedSliceKey[static_cast<size_t> (i)] = key;
                int n = 0;
                if (entry != nullptr && sh->slice_mode != 0)
                {
                    sh->slices[n++] = 0.0f;
                    if (sh->slice_mode == 1)
                    {
                        for (const auto& onset : entry->onsets)
                            if (onset.strength >= 1.0f - threshold * 0.01f && n < sbvital::SampleOscShared::kMaxSlices)
                                sh->slices[n++] = onset.position;
                    }
                    else
                    {
                        for (float position : entry->manual)
                            if (position > sh->slices[n - 1] && position < 1.0f && n < sbvital::SampleOscShared::kMaxSlices)
                                sh->slices[n++] = position;
                    }
                }
                sh->num_slices = n;
            }
        }
    }

    void applySpecials() noexcept
    {
        for (int i = 0; i < kNumLfo; ++i)
        {
            const int p = shapeParam[static_cast<size_t> (i)];
            if (p < 0 || lfoLines[static_cast<size_t> (i)] == nullptr)
                continue;
            int shape = static_cast<int> (params[static_cast<size_t> (p)].load (std::memory_order_relaxed) + 0.5f);
            const LfoCustom* custom = nullptr;
            if (shape == kLfoShapeCustom && activeSnapshot != nullptr && activeSnapshot->lfo[static_cast<size_t> (i)].present)
                custom = &activeSnapshot->lfo[static_cast<size_t> (i)];
            if (shape >= kNumLfoShapes && custom == nullptr)
                shape = 0; // "Custom" without a point list in the content: the documented fallback is the sine
            if (custom != nullptr)
            {
                // the point list of content.lfo_shapes, baked into the LFO's shared table with the global sources (macros, mod wheel, pitch
                // wheel, channel pressure; the per-voice sources count as 0 there); re-baked when one of them moved
                bool rebake = appliedShape[static_cast<size_t> (i)] != kLfoShapeCustom || appliedCustomId[static_cast<size_t> (i)] != snapshotId;
                for (size_t m = 0; m < custom->mods.size() && m < kMaxPointMods; ++m)
                {
                    const float v = modValue (custom->mods[m].source);
                    if (std::fabs (v - appliedModValues[static_cast<size_t> (i)][m]) > 1.0e-4f)
                        rebake = true;
                }
                if (rebake)
                {
                    for (size_t m = 0; m < custom->mods.size() && m < kMaxPointMods; ++m)
                        appliedModValues[static_cast<size_t> (i)][m] = modValue (custom->mods[m].source);
                    bakeCustomShape (i, *custom);
                }
                appliedShape[static_cast<size_t> (i)] = kLfoShapeCustom;
                appliedCustomId[static_cast<size_t> (i)] = snapshotId;
                applyPointModulation (i, custom->point.get());
            }
            else
            {
                if (shape != appliedShape[static_cast<size_t> (i)])
                {
                    appliedShape[static_cast<size_t> (i)] = shape;
                    vitaltables::applyLfoShape (*lfoLines[static_cast<size_t> (i)], shape);
                }
                applyPointModulation (i, nullptr);
            }
        }

        applyLfoOptions();
        applyOscMapping();
        applyMpe();

        if (routingParam >= 0)
        {
            const float routing = params[static_cast<size_t> (routingParam)].load (std::memory_order_relaxed);
            if (routing != appliedRouting)
            {
                appliedRouting = routing;
                const int mode = static_cast<int> (routing + 0.5f); // 0 parallel, 1 series 1 -> 2, 2 series 2 -> 1
                if (vFilter1Input != nullptr)
                    vFilter1Input->set (mode == 2 ? 1.0f : 0.0f);
                if (vFilter2Input != nullptr)
                    vFilter2Input->set (mode == 1 ? 1.0f : 0.0f);
            }
        }

        // the order of the 16 modules: fx.order.1 .. 16 repaired to a permutation (duplicates: a later position takes the lowest module not yet placed)
        float choices[kNumFxSlots];
        bool same = true;
        for (int i = 0; i < static_cast<int> (kNumFxSlots); ++i)
        {
            const int p = fxOrderParam[static_cast<size_t> (i)];
            choices[i] = p >= 0 ? params[static_cast<size_t> (p)].load (std::memory_order_relaxed) : static_cast<float> (i);
            same = same && choices[i] == appliedChoices[static_cast<size_t> (i)];
        }
        if (! same || appliedFxOrder < 0.0f)
        {
            for (int i = 0; i < static_cast<int> (kNumFxSlots); ++i)
                appliedChoices[static_cast<size_t> (i)] = choices[i];
            int order[kNumFxSlots];
            resolveFxOrder (choices, order, static_cast<int> (kNumFxSlots));
            appliedFxOrder = 1.0f;
            for (int i = 0; i < static_cast<int> (kNumFxSlots); ++i)
                if (vFxOrderN[static_cast<size_t> (i)] != nullptr)
                    vFxOrderN[static_cast<size_t> (i)]->set (static_cast<float> (order[i]));
        }
    }

    // ---- content, clocks, tuning ----------------------------------------------------------------------------------------
    /** The audio thread takes over the snapshot setContent () built (lock free). */
    void takeSnapshot() noexcept
    {
        Snapshot* next = pendingSnapshot.exchange (nullptr, std::memory_order_acq_rel);
        if (next == nullptr)
            return;
        // the previous snapshot is retired (may be freed by the message thread) only after applyParams () has pointed the wavetables and
        // the sample oscillators at the new one
        if (retireAfterApply != nullptr)
            retireAfterApply->retired.store (true, std::memory_order_release);
        retireAfterApply = activeSnapshot;
        activeSnapshot = next;
        ++snapshotId;
        tuningStateId = ~0u; // the scale tables changed
        ++matrixGeneration;  // the curves: every velocity / note slot takes its table again
    }

    /** The value of a source that is the same for every voice (globalKindOf () != 0), 0 for a source that differs per voice or is unknown. */
    float modValue (int source) const noexcept
    {
        const int kind = source >= 0 ? globalKindOf (source) : 0;
        if (kind >= 1 && kind <= kNumMacro)
        {
            // the macro as the matrix sees it (the knob plus its modulation, as of the last block), else the knob
            const sbvital::Output* out = sourceOutput[static_cast<size_t> (kind <= 4 ? kSourceMacro1 + kind - 1 : kSourceMacro5 + kind - 5)];
            if (out != nullptr)
                return out->buffer[0][0];
            const int p = macroParam[static_cast<size_t> (kind - 1)];
            return p >= 0 ? params[static_cast<size_t> (p)].load (std::memory_order_relaxed) * 0.01f : 0.0f;
        }
        switch (kind)
        {
            case 9:  return lastModWheel;
            case 10: return lastBend;
            case 11: return lastAftertouch;
            default: return 0.0f;
        }
    }

    /** The custom shape with its point modulation applied (the moved positions keep their order, no sort needed), baked into the LFO's table. */
    void bakeCustomShape (int i, const LfoCustom& custom) noexcept
    {
        shapes::ShapeData shape = custom.shape;
        float values[kPointSources] = {};
        for (int s = 0; s < kNumModSources; ++s)
            values[s] = modValue (s);
        if (custom.point != nullptr)
            custom.point->apply (values, shape);
        LineGenerator& line = *lfoLines[static_cast<size_t> (i)];
        shape.bake (line.getBuffer(), kBakedPoints);
        float* full = line.getCubicInterpolationBuffer(); // [0] and the two values behind the table, as LineGenerator::render () leaves them
        full[0] = full[1];
        full[kBakedPoints + 1] = full[kBakedPoints];
        full[kBakedPoints + 2] = full[kBakedPoints];
    }

    /** One chunk of the engine: the budget of the LFOs' point tables and the chunk counter of the mono LFOs are renewed first. */
    void runEngine (int numFrames) noexcept
    {
        *pointBudget = pointTablesPerChunk();
        ++noteGroup->chunk;
        engine->process (numFrames);
    }

    /** The keys held down, for the mono LFOs (lfoN.mono in the envelope and loop modes): a note on while no key is held and no pedal is down
        starts a new group, the LFO is held while a key or the pedal is down. */
    bool keysHeld() const noexcept
    {
        if (pedalChannels != 0)
            return true;
        for (const auto& channelKeys : activeKey)
            for (const int8_t k : channelKeys)
                if (k >= 0)
                    return true;
        return false;
    }

    /** Tables of the per-voice point modulation that the LFOs may bake in one chunk (the tier): the cost of a chunk is bounded, a table that
        has to wait is baked after `force_after` chunks at the latest. */
    int pointTablesPerChunk() const noexcept
    {
        return quality.maxVoices <= 8 ? 4 : (quality.maxVoices <= 16 ? 8 : 16);
    }

    /** The per-voice point modulation of LFO i (null: none). The LFO bakes a table per voice from the sources it reads; this plugs those
        sources into the LFO (a source that depends on the LFO would close a loop: it stays unplugged, as for a matrix slot), hands over the
        global ones (macros, wheels) and sets the resolution / budget of the tier. */
    void applyPointModulation (int i, const CustomShapeModulation* point) noexcept
    {
        const size_t k = static_cast<size_t> (i);
        sbvital::LfoPointState* state = lfoStates[k];
        sbvital::SynthLfo* lfo = lfoProcessors[k];
        if (state == nullptr || lfo == nullptr)
            return;
        const bool voices = point != nullptr && point->voiceSources() != 0;
        const CustomShapeModulation* mod = voices ? point : nullptr;
        // the tier: 8 voices or fewer (low) bake 512 points and 4 tables per chunk, up to 16 voices 2048 and 8, more 2048 and 16
        const int resolution = quality.maxVoices <= 8 ? CustomShapeModulation::kLowResolution : CustomShapeModulation::kHighResolution;
        if (state->modulation != mod || state->resolution != resolution || appliedPointSnapshot[k] != snapshotId)
        {
            state->modulation = mod;
            state->resolution = resolution;
            ++state->generation;
            appliedPointSnapshot[k] = snapshotId;
        }
        if (mod != nullptr)
        {
            state->global_mask = mod->globalSources();
            for (int s = 0; s < kPointSources; ++s)
                if (((state->global_mask >> s) & 1) != 0)
                    state->global_values[s] = modValue (s);
        }

        const uint64_t wanted = mod != nullptr ? mod->voiceSources() : 0;
        if (wanted == pointPlugged[k] || (wanted == pointWanted[k] && pointTriedGeneration[k] == matrixGeneration))
            return; // done, or a source was left unplugged (loop) and nothing has changed since
        pointWanted[k] = wanted;
        pointTriedGeneration[k] = matrixGeneration;
        for (int s = 0; s < kNumModSources; ++s)
        {
            const uint64_t bit = 1ull << s;
            const sbvital::Output* out = sourceOutput[static_cast<size_t> (s)];
            if (out == nullptr)
                continue;
            if ((pointPlugged[k] & bit) != 0 && (wanted & bit) == 0)
            {
                lfo->unplug (out);
                pointPlugged[k] &= ~bit;
            }
            else if ((pointPlugged[k] & bit) == 0 && (wanted & bit) != 0)
            {
                // a source that depends on this LFO would close a loop (the engine would allocate a feedback node)
                const sbvital::ProcessorRouter* router = lfo->router();
                if (router != nullptr && router->isDownstream (lfo, out->owner))
                    continue;
                lfo->plug (out, sbvital::SynthLfo::kPointSource0 + s);
                out->owner->enable (true);
                pointPlugged[k] |= bit;
            }
        }
    }

    /** mono / host switches of the LFOs: both are the engine's "Sync" mode (the phase comes from a clock shared by all voices); host locks
        that clock to the transport. Other sync types (envelope and loop modes) are not changed by mono. */
    void applyLfoOptions() noexcept
    {
        for (size_t i = 0; i < static_cast<size_t> (kNumLfo); ++i)
        {
            if (vLfoSyncType[i] == nullptr || vLfoHostLock[i] == nullptr || lfoSyncParam[i] < 0)
                continue;
            const int sync = static_cast<int> (params[static_cast<size_t> (lfoSyncParam[i])].load (std::memory_order_relaxed) + 0.5f);
            const bool mono = lfoMonoParam[i] >= 0 && params[static_cast<size_t> (lfoMonoParam[i])].load (std::memory_order_relaxed) > 0.5f;
            const bool host = lfoHostParam[i] >= 0 && params[static_cast<size_t> (lfoHostParam[i])].load (std::memory_order_relaxed) > 0.5f;
            const int effective = (sync == sbvital::SynthLfo::kTrigger && (mono || host)) ? static_cast<int> (sbvital::SynthLfo::kSync) : sync;
            // the envelope and loop modes: the LFO itself serves all voices from one instance (A45)
            if (lfoStates[i] != nullptr)
                lfoStates[i]->mono = mono;
            vLfoSyncType[i]->set (static_cast<float> (effective));
            const bool lock = host && effective == static_cast<int> (sbvital::SynthLfo::kSync);
            vLfoHostLock[i]->set (lock ? 1.0f : 0.0f);
            // 0: the clock stays at 0 as in version 2 (a "Sync" LFO restarts at phase 0), 1: the engine's running time, 2: the host's song position
            lfoClock[i] = lock ? 2 : (mono ? 1 : 0);
        }
    }

    /** The clock of every LFO before a chunk: the engine's own running time, or the host's song position for a host-locked LFO. */
    void updateClocks (int pos) noexcept
    {
        const double hostSeconds = transport.playing ? transport.songPosBeats * 60.0 / transport.bpm + static_cast<double> (pos) / sampleRate : clockSeconds;
        for (int i = 0; i < kNumLfo; ++i)
        {
            const int kind = lfoClock[static_cast<size_t> (i)];
            engine->correctLfoToTime (i, kind == 2 ? hostSeconds : (kind == 1 ? clockSeconds : 0.0));
        }
    }

    /** Moves the concert pitch offset of the sounding notes to its target within about 20 ms (master.tuning.a4). */
    void slewTune (int frames) noexcept
    {
        if (a4Current == a4Target)
            return;
        a4Current += (a4Target - a4Current) * std::min (1.0, static_cast<double> (frames) / (0.02 * sampleRate));
        if (std::fabs (a4Target - a4Current) < 1.0e-4)
            a4Current = a4Target;
        for (const Resolved& r : resolved)
            if (r.isTune)
                r.control->set (static_cast<float> (std::clamp (tuneBase + a4Current, r.lo, r.hi)));
    }

    /** master.scale.* (the note conforming table) and master.tuning.* (the tuned note of every key, concert pitch, MTS-ESP). */
    void applyScaleAndTuning (bool force) noexcept
    {
        const auto value = [this] (int p, float fallback) { return p >= 0 ? params[static_cast<size_t> (p)].load (std::memory_order_relaxed) : fallback; };

        const int key = static_cast<int> (value (scaleKeyParam, 0.0f) + 0.5f);
        const int mode = static_cast<int> (value (scaleModeParam, 0.0f) + 0.5f);
        const int type = mode == 0 ? static_cast<int> (value (scaleTypeParam, 0.0f) + 0.5f) : 0; // "Mark only" changes nothing audible
        if (force || key != conformKey || type != conformType)
        {
            conformKey = key;
            conformType = type;
            for (int n = 0; n < kNumConform; ++n)
                conform[static_cast<size_t> (n)] = static_cast<int8_t> (conformedNote (n, key, type));
        }

        const tuning::ITuningSource* source = tuningSource.load (std::memory_order_acquire);
        const bool wantMts = value (mtsParam, 0.0f) > 0.5f;
        mtsActive = wantMts && source != nullptr && source->isConnected();
        mtsNoteOnOnly = value (mtsNoteOnParam, 0.0f) > 0.5f;

        const double a4 = std::clamp (static_cast<double> (value (a4Param, 440.0f)), 400.0, 480.0);
        a4Target = mtsActive ? 0.0 : 12.0 * std::log2 (a4 / 440.0); // an MTS master overrides the scale and the concert pitch
        if (force)
            a4Current = a4Target;

        if (mtsActive)
        {
            if (! mtsNoteOnOnly)
            {
                std::array<double, 128> frequencies {};
                if (source->query128Keys (frequencies))
                {
                    for (size_t k = 0; k < 128; ++k)
                        noteTable[k] = frequencies[k] > 0.0 ? noteFromHz (frequencies[k]) : std::numeric_limits<float>::quiet_NaN();
                    engine->retuneVoices (noteTable.data());
                }
            }
            wasMts = true;
            tuningStateId = ~0u; // back to the scale when the master goes away
        }
        else
        {
            const int scale = std::clamp (static_cast<int> (value (tuningScaleParam, 0.0f) + 0.5f), 0, 3);
            const uint32_t state = static_cast<uint32_t> (scale) | (snapshotId << 2);
            if (force || wasMts || state != tuningStateId)
            {
                tuningStateId = state;
                wasMts = false;
                if (activeSnapshot != nullptr)
                    noteTable = activeSnapshot->scaleNotes[static_cast<size_t> (scale)];
                else
                    identityNotes (noteTable);
            }
        }
    }

    void applyOscMapping() noexcept
    {
        for (size_t i = 0; i < static_cast<size_t> (kNumOsc); ++i)
        {
            std::array<int, 6> now {};
            for (size_t m = 0; m < 6; ++m)
                now[m] = mapParam[i][m] >= 0 ? static_cast<int> (params[static_cast<size_t> (mapParam[i][m])].load (std::memory_order_relaxed) + 0.5f) : 0;
            if (now == appliedMap[i])
                continue;
            appliedMap[i] = now;
            engine->setOscMapping (static_cast<int> (i), now[0], now[1], now[2], now[3], now[4] != 0);
            if (vOscMapWarp[i] != nullptr)
                vOscMapWarp[i]->set (now[5] != 0 ? 1.0f : 0.0f);
        }
    }

    /** MPE on/off or a new range: every channel's pitch is recomputed. */
    void applyMpe() noexcept
    {
        const bool on = mpeOn();
        const float range = mpeRangeParam >= 0 ? params[static_cast<size_t> (mpeRangeParam)].load (std::memory_order_relaxed) : 48.0f;
        const int state = (on ? 1 : 0) + 2 * static_cast<int> (range + 0.5f);
        if (state == mpeApplied)
            return;
        const bool changedMode = (mpeApplied < 0 ? 0 : (mpeApplied & 1)) != (on ? 1 : 0);
        mpeApplied = state;
        if (on)
            applyMpeBends();
        else if (changedMode)
            for (int c = 0; c < numChannels; ++c) // back to the plain per-channel wheel
                engine->setPitchWheel (rawBend[static_cast<size_t> (c)] * masterBendRange() / kVitalLocalBendRange, c);
    }

    bool mpeOn() const noexcept { return mpeOnParam >= 0 && params[static_cast<size_t> (mpeOnParam)].load (std::memory_order_relaxed) > 0.5f; }

    float masterBendRange() const noexcept
    {
        return bendRangeParam >= 0 ? params[static_cast<size_t> (bendRangeParam)].load (std::memory_order_relaxed) : 2.0f;
    }

    /** MPE: the manager channel's wheel (master.bend_range) moves every note, a member channel's wheel (master.mpe.bend_range) its own notes. */
    void applyMpeBends() noexcept
    {
        const float memberRange = mpeRangeParam >= 0 ? params[static_cast<size_t> (mpeRangeParam)].load (std::memory_order_relaxed) : 48.0f;
        const float managerBend = rawBend[0] * masterBendRange();
        for (int c = 0; c < numChannels; ++c)
            engine->setChannelPitchBend (c, managerBend + (c == 0 ? 0.0f : rawBend[static_cast<size_t> (c)] * memberRange));
    }

    // ---- the modulation matrix --------------------------------------------------------------------------------------
    bool destinationEligible (int destination) const noexcept
    {
        if (destination <= 0 || static_cast<size_t> (destination) >= destinations.size())
            return true;
        const Destination& d = destinations[static_cast<size_t> (destination)];
        return d.gateParam < 0 || (params[static_cast<size_t> (d.gateParam)].load (std::memory_order_relaxed) > 0.5f) == (d.gateValue > 0);
    }

    /** The aux source of a connected slot (src x amount x aux x output): plug / unplug it and hand the settings to the connection. */
    void updateAux (int k) noexcept
    {
        Slot& slot = slots[static_cast<size_t> (k)];
        if (slot.auxSourceParam < 0)
            return;
        int aux = slot.connected ? static_cast<int> (params[static_cast<size_t> (slot.auxSourceParam)].load (std::memory_order_relaxed) + 0.5f) : 0;
        if (aux < 0 || aux > kNumModSources || (aux > 0 && sourceOutput[static_cast<size_t> (aux - 1)] == nullptr))
            aux = 0;

        if (aux != slot.auxPlugged)
        {
            if (slot.auxPlugged > 0)
                slot.processor->unplug (sourceOutput[static_cast<size_t> (slot.auxPlugged - 1)]);
            slot.auxPlugged = 0;
            if (aux > 0)
            {
                // like the main source: a source that depends on the destination would close a loop (the engine would allocate a
                // feedback node), such an aux source is left unplugged
                const sbvital::Output* out = sourceOutput[static_cast<size_t> (aux - 1)];
                const Destination& d = destinations[static_cast<size_t> (slot.destination)];
                bool loop = false;
                for (const sbvital::Processor* target : { d.mono, d.poly })
                    if (target != nullptr && target->router() != nullptr && target->router()->isDownstream (target, out->owner))
                        loop = true;
                if (! loop)
                {
                    slot.processor->plug (out, sbvital::ModulationConnectionProcessor::kAuxInput);
                    out->owner->enable (true);
                    slot.auxPlugged = aux;
                }
            }
        }

        // the engine's sources are 0..1 at the output (the bipolar display is the slot's own flag), so the mapping to 0..1 is the identity
        slot.processor->setAux (slot.auxPlugged > 0, false, params[static_cast<size_t> (slot.auxInvParam)].load (std::memory_order_relaxed) > 0.5f,
                                params[static_cast<size_t> (slot.auxPowerParam)].load (std::memory_order_relaxed),
                                params[static_cast<size_t> (slot.auxOutputParam)].load (std::memory_order_relaxed) * 0.01f);
    }

    /** The velocity and note curve of content.curves are the remap table of the slots that read those sources; "legato" glides them. */
    void updateCurve (int k, int source) noexcept
    {
        Slot& slot = slots[static_cast<size_t> (k)];
        const int kind = (source == kSourceVelocity) ? 1 : (source == kSourceNote ? 2 : 0);
        const bool have = activeSnapshot != nullptr && ((kind == 1 && activeSnapshot->hasVelocity) || (kind == 2 && activeSnapshot->hasNote));
        const int want = have ? kind : 0;
        if (want != slot.curveKind || (want != 0 && slot.curveId != snapshotId))
        {
            LineGenerator& line = *slot.processor->lineMapGenerator();
            if (want == 0)
                line.initLinear();
            else
            {
                // any non-linear point set marks the remap as active; the table is then written over what render () made of it
                line.setSmooth (false);
                line.setNumPoints (3);
                line.setPoint (0, { 0.0f, 1.0f });
                line.setPoint (1, { 0.5f, 0.5f });
                line.setPoint (2, { 1.0f, 0.0f });
                line.render();
                const auto& table = want == 1 ? activeSnapshot->velocityTable : activeSnapshot->noteTable;
                float* data = line.getBuffer();
                for (int i = 0; i < kBakedPoints; ++i)
                    data[i] = std::clamp (table[static_cast<size_t> (i)], 0.0f, 1.0f);
                float* full = line.getCubicInterpolationBuffer();
                full[0] = full[1];
                full[kBakedPoints + 1] = full[kBakedPoints];
                full[kBakedPoints + 2] = full[kBakedPoints];
            }
            slot.curveKind = want;
            slot.curveId = snapshotId;
        }

        const int legatoParam = kind == 1 ? curveVelocityLegatoParam : (kind == 2 ? curveNoteLegatoParam : -1);
        const bool glide = legatoParam >= 0 && params[static_cast<size_t> (legatoParam)].load (std::memory_order_relaxed) > 0.5f;
        const float seconds = portamentoParam >= 0 ? params[static_cast<size_t> (portamentoParam)].load (std::memory_order_relaxed) * 0.001f : 0.0f;
        slot.processor->setGlide (glide, seconds);
    }

    void updateSlot (int k) noexcept
    {
        Slot& slot = slots[static_cast<size_t> (k)];
        if (slot.sourceParam < 0 || slot.destinationParam < 0)
            return;
        const int source = static_cast<int> (params[static_cast<size_t> (slot.sourceParam)].load (std::memory_order_relaxed) + 0.5f);
        const int destination = static_cast<int> (params[static_cast<size_t> (slot.destinationParam)].load (std::memory_order_relaxed) + 0.5f);
        const bool eligible = destinationEligible (destination);
        if (source == slot.source && destination == slot.destination && eligible == slot.eligible)
        {
            // a slot that was left unwired because it would have closed a loop gets another chance when the matrix has changed
            if (slot.source > 0 && slot.destination > 0 && ! slot.connected && slot.triedGeneration != matrixGeneration && eligible
                && static_cast<size_t> (destination) < destinations.size() && destinations[static_cast<size_t> (destination)].valid
                && sourceOutput[static_cast<size_t> (source - 1)] != nullptr)
            {
                slot.triedGeneration = matrixGeneration;
                connectSlot (k);
            }
            updateAux (k);
            updateCurve (k, slot.source);
            return;
        }

        if (slot.connected)
            disconnectSlot (k);
        slot.source = source;
        slot.destination = destination;
        slot.eligible = eligible;
        slot.triedGeneration = matrixGeneration;
        if (source > 0 && destination > 0 && static_cast<size_t> (destination) < destinations.size() && destinations[static_cast<size_t> (destination)].valid
            && sourceOutput[static_cast<size_t> (source - 1)] != nullptr && eligible)
            connectSlot (k);
        updateAux (k);
        updateCurve (k, slot.source);
    }

    sbvital::modulation_change changeFor (int k, bool disconnecting) const noexcept
    {
        const Slot& slot = slots[static_cast<size_t> (k)];
        const Destination& d = destinations[static_cast<size_t> (slot.destination)];
        sbvital::modulation_change change {};
        change.source = sourceOutput[static_cast<size_t> (slot.source - 1)];
        change.mono_destination = d.mono;
        change.poly_destination = d.poly;
        change.destination_scale = d.scale;
        change.mono_modulation_switch = d.monoSwitch;
        change.poly_modulation_switch = d.polySwitch;
        change.modulation_processor = slot.processor;
        change.disconnecting = disconnecting;
        change.num_audio_rate = 0;
        if (disconnecting)
        {
            // other connections of the same source that run at audio rate keep the source at audio rate
            for (int j = 0; j < kNumSlots; ++j)
            {
                const Slot& other = slots[static_cast<size_t> (j)];
                if (j == k || ! other.connected || other.source != slot.source)
                    continue;
                const Destination& od = destinations[static_cast<size_t> (other.destination)];
                const sbvital::Processor* target = (change.source->owner->isPolyphonic() && od.poly != nullptr) ? od.poly : od.mono;
                if (target != nullptr && ! target->isControlRate())
                    ++change.num_audio_rate;
            }
        }
        return change;
    }

    /** True if wiring the slot would make the engine insert a feedback node (a loop in the graph: the source depends on the
        destination), which allocates; such a slot stays unwired (docs/PARAMETERS.md). The decision is the engine's own: the
        check that ProcessorRouter::connect makes for the connection, made first. */
    bool wouldCloseLoop (const sbvital::modulation_change& change) const noexcept
    {
        change.modulation_processor->plug (change.source, sbvital::ModulationConnectionProcessor::kModulationInput); // as connectModulation does
        const bool polyphonic = change.source->owner->isPolyphonic() && change.poly_destination != nullptr;
        const sbvital::Processor* destination = polyphonic ? change.poly_destination : change.mono_destination;
        const sbvital::ProcessorRouter* router = destination->router();
        return router != nullptr && router->isDownstream (destination, change.modulation_processor);
    }

    void connectSlot (int k) noexcept
    {
        Slot& slot = slots[static_cast<size_t> (k)];
        const sbvital::modulation_change change = changeFor (k, false);
        if (wouldCloseLoop (change))
            return;
        engine->connectModulation (change);
        slot.connected = true;
        ++matrixGeneration;
    }

    void disconnectSlot (int k) noexcept
    {
        Slot& slot = slots[static_cast<size_t> (k)];
        engine->disconnectModulation (changeFor (k, true));
        slot.connected = false;
        ++matrixGeneration;
    }

    // ---- events -----------------------------------------------------------------------------------------------------
    void handleEvent (const Event& e) noexcept
    {
        const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);

        switch (e.type)
        {
            case Event::NoteOn:
            {
                if (e.key < 0 || e.key > 127)
                    return;
                const float velocity = std::isnan (e.value) ? 0.0f : clampf (e.value, 0.0f, 1.0f);
                // master.scale.*: the key the voice allocator sees (a note outside the scale moves into it)
                const int key = conform[static_cast<size_t> (e.key)];
                if (mtsActive)
                {
                    // an MTS master: its frequency for the key (all keys again every block unless "note on only")
                    const tuning::ITuningSource* source = tuningSource.load (std::memory_order_acquire);
                    const double hz = source != nullptr ? source->frequencyForKey (key) : 0.0;
                    if (hz > 0.0)
                        noteTable[static_cast<size_t> (key)] = noteFromHz (hz);
                }
                if (std::isnan (noteTable[static_cast<size_t> (key)]))
                    return; // a key the tuning does not map (a .kbm "x" entry) does not sound
                if (! keysHeld())
                    ++noteGroup->group; // the first note of a new group restarts the mono LFOs
                activeKey[static_cast<size_t> (channel)][static_cast<size_t> (e.key)] = static_cast<int8_t> (key);
                noteGroup->held = true;
                // hitting a key that still rings lets the old note fade out first (as the own engine does)
                engine->noteOff (key, 0.0f, 0, channel);
                engine->noteOn (key, velocity, 0, channel);
                rememberNoteId (e.noteId, key, channel);
                break;
            }
            case Event::NoteOff:
                if (e.key >= 0 && e.key <= 127)
                {
                    // the note that was started (the scale may have changed since); the release velocity is the "Lift" source (0 = unknown)
                    int8_t& started = activeKey[static_cast<size_t> (channel)][static_cast<size_t> (e.key)];
                    const int key = started >= 0 ? started : conform[static_cast<size_t> (e.key)];
                    started = -1;
                    engine->noteOff (key, std::isnan (e.value) ? 0.0f : clampf (e.value, 0.0f, 1.0f), 0, channel);
                    noteGroup->held = keysHeld();
                }
                break;
            case Event::SustainPedal:
                // pedal down keeps released notes sounding until it is lifted (the engine's own sustain handling)
                if (e.channel < 0)
                {
                    pedalChannels = e.value >= 0.5f ? 0xffffu : 0u;
                    if (e.value >= 0.5f)
                        engine->sustainOnRange (0, numChannels - 1);
                    else
                        engine->sustainOffRange (0, 0, numChannels - 1);
                }
                else if (e.value >= 0.5f)
                {
                    pedalChannels |= 1u << (channel & 15);
                    engine->sustainOn (channel);
                }
                else
                {
                    pedalChannels &= ~(1u << (channel & 15));
                    engine->sustainOff (0, channel);
                }
                noteGroup->held = keysHeld();
                break;
            case Event::NoteExpression:
                handleExpression (e, channel);
                break;
            case Event::PitchBend:
            {
                // the per-channel wheel of the voices moves the pitch (a value of 1 is kVitalLocalBendRange semitones); the zoned
                // wheel only feeds the "Pitch Wheel" modulation source (its range control is 0, see applyFixedControls)
                const float bend = std::isnan (e.value) ? 0.0f : clampf (e.value, -1.0f, 1.0f);
                rawBend[static_cast<size_t> (channel)] = bend;
                lastBend = bend;
                if (mpeOn())
                    applyMpeBends(); // master.mpe.on: the member channel's bend range, the manager channel moves every note
                else
                    engine->setPitchWheel (bend * masterBendRange() / kVitalLocalBendRange, channel);
                engine->setZonedPitchWheel (bend, channel, channel);
                break;
            }
            case Event::ModWheel:
                lastModWheel = std::isnan (e.value) ? 0.0f : clampf (e.value, 0.0f, 1.0f);
                engine->setModWheel (lastModWheel, channel);
                break;
            case Event::Aftertouch:
            {
                const float pressure = std::isnan (e.value) ? 0.0f : clampf (e.value, 0.0f, 1.0f);
                lastAftertouch = pressure;
                engine->setChannelAftertouch (channel, pressure, 0);
                if (mpeOn() && channel > 0)
                    engine->setNotePressure (-1, channel, pressure); // an MPE member channel carries one note: its pressure is that note's
                break;
            }
            case Event::AllNotesOff:
                if (e.channel < 0)
                {
                    engine->allNotesOff (0);
                    for (auto& channelKeys : activeKey)
                        channelKeys.fill (-1);
                }
                else
                {
                    engine->allNotesOff (0, channel);
                    activeKey[static_cast<size_t> (channel)].fill (-1);
                }
                noteGroup->held = keysHeld();
                break;
        }
    }

    /** Host note ids -> (key, channel): CLAP addresses a note expression by id (the key may be a wildcard). A small ring of the last notes. */
    void rememberNoteId (int32_t id, int key, int channel) noexcept
    {
        if (id < 0)
            return;
        noteIds[static_cast<size_t> (noteIdNext)] = { id, key, channel };
        noteIdNext = (noteIdNext + 1) % kNoteIdSlots;
    }

    void handleExpression (const Event& e, int channel) noexcept
    {
        int key = e.key < 0 ? -1 : std::min<int> (e.key, 127);
        if (key >= 0)
        {
            const int8_t started = activeKey[static_cast<size_t> (channel)][static_cast<size_t> (key)];
            key = started >= 0 ? started : conform[static_cast<size_t> (key)];
        }
        if (e.noteId >= 0)
            for (const NoteIdSlot& slot : noteIds)
                if (slot.id == e.noteId)
                {
                    key = slot.key;
                    channel = slot.channel;
                    break;
                }
        if (! std::isfinite (e.value))
            return;
        switch (e.expression)
        {
            case Event::Tuning:     engine->setNotePitchBend (key, channel, clampf (e.value, -96.0f, 96.0f)); break;
            case Event::Pressure:   engine->setNotePressure (key, channel, clampf (e.value, 0.0f, 1.0f)); break;
            case Event::Brightness: engine->setNoteSlide (key, channel, clampf (e.value, 0.0f, 1.0f)); break;
        }
    }

    /** Vital's output is interleaved poly_float: lanes 0 and 1 are left and right. */
    void writeOutput (float* left, float* right, int n) const noexcept
    {
        const auto* out = reinterpret_cast<const sbvital::mono_float*> (engine->output (0)->buffer);
        constexpr int stride = static_cast<int> (sbvital::poly_float::kSize);
        if (right != nullptr)
        {
            for (int i = 0; i < n; ++i)
            {
                left[i] = out[stride * i];
                right[i] = out[stride * i + 1];
            }
        }
        else
        {
            for (int i = 0; i < n; ++i)
                left[i] = 0.5f * (out[stride * i] + out[stride * i + 1]);
        }
    }

    // ---- state ----------------------------------------------------------------------------------------------------
    std::array<std::atomic<float>, kMaxParams> params;
    std::atomic<int> activeCount { 0 };
    std::atomic<int> latencyCache { 0 };

    struct NoteIdSlot
    {
        int32_t id = -1;
        int key = -1, channel = 0;
    };
    static constexpr int kNoteIdSlots = 64;
    std::array<NoteIdSlot, kNoteIdSlots> noteIds {};
    int noteIdNext = 0;

    std::atomic<int> qVoices { 8 }, qUnison { 16 }, qOversampling { 2 }, qInterpolation { 1 };
    std::atomic<uint32_t> qFxMask { kFxAll };
    std::atomic<bool> qRealtime { false };
    std::atomic<uint32_t> qGeneration { 0 };

    // audio thread (and prepare) only
    Quality quality;
    uint32_t appliedQualityGeneration = ~0u;
    std::array<float, kMaxParams> applied {};
    std::array<int, kNumOsc> appliedWavetable {};
    std::array<int, kNumOsc> userFramesNow {};                 // frames of the user table an oscillator plays (0: a built-in table)
    std::array<std::array<int, 2>, kNumOsc> frameParams { { { { -1, -1 } }, { { -1, -1 } }, { { -1, -1 } } } };
    enum { kSpType, kSpStart, kSpEnd, kSpRoot, kSpKeytrack, kSpLoopMode, kSpLoopStart, kSpLoopEnd, kSpLoopXfade, kSpLoopRelative, kSpLoopExit,
           kSpFadeEdges, kSpNormalize, kSpReverse, kSpSliceMode, kSpSliceThreshold, kSpSlicePlay, kSpSliceRoot, kSpScanRange, kSpScanReverse,
           kSpScanLock, kSpScanToBpm, kSpLengthBeats, kSpCount };
    std::array<std::array<int, kSpCount>, kNumOsc> sampleParams {};
    std::array<sbvital::SampleOscShared*, kNumOsc> sampleShared {};
    std::array<sbvital::FilterCurveShared*, 3> curveShared {};
    std::array<uint32_t, 3> appliedCurveId { { ~0u, ~0u, ~0u } };
    sbvital::DistortionModule* distortionModule = nullptr;
    uint32_t appliedShaperId = ~0u;
    std::array<uint64_t, kNumOsc> appliedSliceKey {};
    std::array<int, kNumLfo> appliedShape {};
    static constexpr size_t kMaxPointMods = 256;
    std::array<uint32_t, kNumLfo> appliedCustomId {};
    std::array<sbvital::LfoPointState*, kNumLfo> lfoStates {};   // the point modulation state of every LFO (shared with its voice clones)
    std::array<sbvital::SynthLfo*, kNumLfo> lfoProcessors {};
    std::array<uint64_t, kNumLfo> pointPlugged {};               // sources plugged into the LFO's point modulation inputs
    std::array<uint32_t, kNumLfo> pointTriedGeneration {};
    std::array<uint64_t, kNumLfo> pointWanted {};
    std::array<uint32_t, kNumLfo> appliedPointSnapshot {};
    std::shared_ptr<int> pointBudget = std::make_shared<int> (0); // tables the voices' LFOs may still bake in this chunk
    std::shared_ptr<sbvital::LfoNoteGroup> noteGroup = std::make_shared<sbvital::LfoNoteGroup>(); // the keys held, for the mono LFOs
    uint32_t pedalChannels = 0;                                   // bit c: the sustain pedal of channel c is down
    std::array<std::array<float, kMaxPointMods>, kNumLfo> appliedModValues {};
    std::array<std::array<int, 6>, kNumOsc> appliedMap {};
    std::array<float, kNumFxSlots> appliedChoices {};
    std::array<sbvital::Value*, kNumFxSlots> vFxOrderN {};
    float appliedRouting = -1.0f, appliedFxOrder = -1.0f;
    std::array<int, kNumOsc> wavetableParam { { -1, -1, -1 } };
    std::array<int, kNumLfo> shapeParam { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
    std::array<int, kNumLfo> lfoMonoParam { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
    std::array<int, kNumLfo> lfoHostParam { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
    std::array<int, kNumLfo> lfoSyncParam { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
    std::array<int, kNumMacro> macroParam { { -1, -1, -1, -1, -1, -1, -1, -1 } };
    std::array<std::array<int, 6>, kNumOsc> mapParam { { { { -1, -1, -1, -1, -1, -1 } }, { { -1, -1, -1, -1, -1, -1 } }, { { -1, -1, -1, -1, -1, -1 } } } };
    int curveVelocityLegatoParam = -1, curveNoteLegatoParam = -1, portamentoParam = -1;
    int scaleKeyParam = -1, scaleTypeParam = -1, scaleModeParam = -1, a4Param = -1, tuningScaleParam = -1, mtsParam = -1, mtsNoteOnParam = -1;
    int mpeOnParam = -1, mpeRangeParam = -1, reverbForceParam = -1;
    bool appliedReverbForce = false;
    std::array<sbvital::Value*, kNumLfo> vLfoSyncType {}, vLfoHostLock {};
    std::array<sbvital::Value*, kNumOsc> vOscMapWarp {};
    std::array<int, kNumLfo> lfoClock {};
    std::array<int, kNumFxSlots> fxOrderParam { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
    int routingParam = -1, bendRangeParam = -1;
    std::array<sbvital::Value*, kNumOsc> vSmooth {};
    sbvital::Value* vFilter1Input = nullptr;
    sbvital::Value* vFilter2Input = nullptr;
    sbvital::Value* vFxOrder = nullptr;

    std::unique_ptr<vitaltables::WavetableSet> tables; // before the engine: the engine's wavetables point into it
    std::unique_ptr<sbvital::SoundEngine> engine;
    std::unique_ptr<Tuning> tuning;
    std::map<std::string, sbvital::Value*> controlMap;
    std::vector<Resolved> resolved;
    std::vector<Destination> destinations;
    std::array<sbvital::Output*, kNumModSources> sourceOutput {};
    std::array<Slot, kNumSlots> slots;
    uint32_t matrixGeneration = 0;
    std::array<LineGenerator*, kNumLfo> lfoLines {};
    double sampleRate = 44100.0;
    bool controlsComplete = false;

    // host transport, the clock of the LFOs, the concert pitch offset (audio thread)
    TransportInfo transport;
    double engineBpm = -1.0, appliedBpm = -1.0, clockSeconds = 0.0;
    double a4Current = 0.0, a4Target = 0.0, tuneBase = 0.0;
    float lastModWheel = 0.0f, lastAftertouch = 0.0f, lastBend = 0.0f;
    std::array<float, 16> rawBend {};
    int mpeApplied = -1;

    // scale, tuning (audio thread); the note table is the one the voice handler reads
    static constexpr int kNumConform = 128;
    static constexpr int kSourceVelocity = 23, kSourceNote = 24; // modulation source choices
    static constexpr int kSourceMacro1 = 18;                     // index of "macro1" in sourceOutput
    static constexpr int kSourceMacro5 = 28;                     // index of "macro5"
    std::array<int8_t, kNumConform> conform {};
    int conformKey = -1, conformType = -1;
    std::array<float, 128> noteTable {};
    std::array<std::array<int8_t, 128>, 16> activeKey {}; // the key a note was started on, per host key and channel (-1: none)
    uint32_t tuningStateId = ~0u;
    bool mtsActive = false, mtsNoteOnOnly = false, wasMts = false;
    std::atomic<const tuning::ITuningSource*> tuningSource { nullptr };
    uint32_t curveId = 0;

    // content: built on the message thread (setContent), taken over by the audio thread without a lock
    std::mutex snapshotMutex;
    StateContent lastContent;
    std::shared_ptr<const sbvital::ImpulseData> impulse;   // the Convolve module's impulse response (message thread)
    const IContentStore* contentStore = nullptr;
    Snapshot* retireAfterApply = nullptr;
    std::vector<std::unique_ptr<Snapshot>> snapshots;
    std::atomic<Snapshot*> pendingSnapshot { nullptr };
    Snapshot* activeSnapshot = nullptr;
    uint32_t snapshotId = 0;
};
} // namespace

std::unique_ptr<IEngine> createVitalEngine()
{
    return std::make_unique<VitalEngine>();
}

bool isVitalEngineConfigured (const IEngine& engine) noexcept
{
    const auto* vital = dynamic_cast<const VitalEngine*> (&engine);
    return vital != nullptr && vital->configured();
}

double vitalControlValue (const IEngine& engine, const char* control) noexcept
{
    const auto* vital = dynamic_cast<const VitalEngine*> (&engine);
    return vital != nullptr ? vital->controlValue (control) : std::nan ("");
}

bool vitalHasModulationDestination (const IEngine& engine, size_t paramIndex) noexcept
{
    const auto* vital = dynamic_cast<const VitalEngine*> (&engine);
    return vital != nullptr && vital->hasModulationDestination (paramIndex);
}

int vitalConnectedSlots (const IEngine& engine) noexcept
{
    const auto* vital = dynamic_cast<const VitalEngine*> (&engine);
    return vital != nullptr ? vital->connectedSlots() : -1;
}

int vitalEffectiveVoiceLimit (const IEngine& engine) noexcept
{
    return static_cast<int> (vitalControlValue (engine, "polyphony"));
}
} // namespace synthyboy
