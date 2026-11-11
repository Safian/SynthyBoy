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
#include "line_generator.h"
#include "modulation_connection_processor.h"
#include "sample_oscillator.h"
#include "sample_source.h"
#include "serum_filter_bank.h"
#include "serum_fx_modules.h"
#include "smooth_value.h"
#include "sound_engine.h"
#include "synth_lfo.h"
#include "synth_oscillator.h"
#include "synth_constants.h"
#include "synth_parameters.h"
#include "tuning.h"
#include "utils.h"
#include "value_switch.h"
#include "vital_bindings.h"
#include "vital_fx_pool.h"
#include "vital_point_mod.h"
#include "vital_tables.h"
#include "synthyboy/content_state.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/content_store.h"
#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/sampleosc/wav_reader.h"
#include "synthyboy/multisample/multisample.h"
#include "synthyboy/spectral/spectral.h"
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
constexpr int kNoiseSampleSlot = 4; // content.samples[].osc of the noise source (GROUPA1, WP-04)
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
    if (index < 38) return extra[index - 34];
    if (index == 38) return "stereo";
    static const char* const voice[] = { "voice_index", "voice_mod1", "voice_mod2" }; // block B26 (GROUPA2, WP-07)
    if (index >= 39 && index < 42) return voice[index - 39];
    // GROUPB WP-06 (choice values 43 .. 57): the outputs of the sound sources and the sources of the note counter
    static const char* const groupb[] = { "osc_1_out", "osc_2_out", "osc_3_out", "noise_out", "sub_out", "filter_1_out", "filter_2_out", "active_voices", "note_on_alt1",
                                          "note_on_alt2", "note_on_rand2", "note_on_discrete_1", "release_velocity", "expression_x", "fixed" };
    if (index >= 42 && index < 57) return groupb[index - 42];
    return "";
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
    spectral::Analysis analysis;                                  // GROUPC WP-12: the STFT analysis of the sample (the spectral oscillator), built with the content
    std::vector<sampleosc::SampleData::Onset> onsets;
    std::vector<float> manual;
    bool present = false;
};

struct Snapshot
{
    std::array<LfoCustom, kNumLfo> lfo;
    struct SpectralFilterEntry
    {
        bool present = false;
        std::array<float, 257> table {};
        uint32_t id = 0;
    };
    std::array<std::unique_ptr<multisample::Instrument>, 3> instrument;  // the SFZ instruments of the content (oscN.type = Multisample, GROUPC WP-13)
    std::array<SpectralFilterEntry, 3> spectralFilter;            // the drawn spectral filter masks (content.spectral_filters, GROUPC WP-12)
    std::array<std::unique_ptr<sbvital::Wavetable>, 3> userTable; // oscN.wavetable = Preset table (content.wavetables)
    std::array<int, 3> userFrames {};
    std::array<std::vector<float>, 3> tableBank;                  // the filter curves of the frames of the user table (the spectral filter from a wavetable, REMAINDER item 2)
    std::array<int, 3> tableBankFrames {};
    std::array<SampleEntry, 3> sample;                            // oscN.type = Sample (content.samples)
    std::unique_ptr<sbvital::Sample::SampleData> noiseSample;     // noise.type = Sample: content.samples[osc 4] as the band limited buffers of the noise source
    std::array<sbvital::FilterCurveShared, 3> filterCurve;        // PZ SVF curves of filter 1, filter 2 and the FX filter (content.filter_curves)
    struct RemapEntry
    {
        bool present = false;
        std::array<float, sbvital::FrameRemap::kSize + 1> table {};
    };
    std::array<std::array<RemapEntry, kNumRemapSlots>, 3> remap;  // the drawn wavetable position remaps (content.remaps): [osc][slot]
    struct ModCurveEntry
    {
        bool present = false;                 // a drawn curve
        std::vector<ContentPoint> points;
    };
    std::array<ModCurveEntry, kNumModSlots> modCurves;            // the per-slot modulation curves (content.modCurves, G2)
    bool hasVelocity = false, hasNote = false;
    std::array<float, kBakedPoints> velocityTable {}, noteTable {};
    std::array<std::array<float, sbvital::Distortion::kShaperSize>, 2> shaperTable {}; // X-Shaper curves A and B (content.curves.shaper_a / shaper_b)
    std::array<bool, 2> hasShaper { { false, false } };
    std::array<std::array<float, 128>, 4> scaleNotes {}; // tuned MIDI note of every key per master.tuning.scale at A4 = 440 (NaN: unmapped)
    std::shared_ptr<const sbvital::ImpulseData> impulse;  // content.impulses[0], for the Convolve instances of the effect pool
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
    is resampled to 2048 values. At most 257 frames (kNumOscillatorWaveFrames: the keyframe positions of a Vital table are 0 .. 256). */
bool decodeWavetable (const std::vector<uint8_t>& bytes, std::vector<float>& frames, int& numFrames)
{
    std::vector<float> left, right;
    sampleosc::WavInfo info;
    if (! sampleosc::readWav (bytes.data(), bytes.size(), left, right, info) || left.empty())
        return false;
    const size_t n = left.size();
    if (n >= 2048 && n % 2048 == 0)
    {
        numFrames = static_cast<int> (std::min<size_t> (static_cast<size_t> (sbvital::kNumOscillatorWaveFrames), n / 2048));
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
            auto& bank = s->tableBank[static_cast<size_t> (w.osc - 1)];
            bank.resize (static_cast<size_t> (numFrames) * vitaltables::kFilterCurvePoints);
            for (int f = 0; f < numFrames; ++f)
                vitaltables::frameToFilterCurve (frames.data() + static_cast<size_t> (f) * 2048, bank.data() + static_cast<size_t> (f) * vitaltables::kFilterCurvePoints);
            s->tableBankFrames[static_cast<size_t> (w.osc - 1)] = numFrames;
        }
        for (const ContentSample& c : content.samples)
        {
            if (c.osc == kNoiseSampleSlot && store->readFile (c.ref, bytes))
            {
                // GROUPA1 (WP-04): the sample of the noise source (osc 4). Decoded here, on the message thread: the audio thread only swaps a pointer
                sampleosc::SampleData decoded;
                if (decoded.loadFromMemory (bytes.data(), bytes.size(), c.ref.file.c_str()) && decoded.isValid())
                {
                    const sampleosc::SampleView v = decoded.view();
                    const int frames = static_cast<int> (std::min<int64_t> (v.numFrames, 1764000));
                    s->noiseSample = sbvital::Sample::makeUserData (v.channelData[0], v.numChannels > 1 ? v.channelData[1] : nullptr, frames,
                                                                     static_cast<int> (std::lround (v.sampleRate)));
                }
                continue;
            }
            if (c.osc < 1 || c.osc > kNumOsc || ! store->readFile (c.ref, bytes))
                continue;
            SampleEntry& e = s->sample[static_cast<size_t> (c.osc - 1)];
            if (! e.data.loadFromMemory (bytes.data(), bytes.size(), c.ref.file.c_str()) || ! e.data.isValid())
                continue;
            e.view = e.data.view();
            e.analysis.build (e.view); // GROUPC WP-12: for the spectral oscillator (about 4 ms per second of audio)
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
            if (c.points[i].modCurve.present && i + 1 < l.shape.numPoints) // the curve of the segment after the point
                l.mods.push_back ({ static_cast<int> (i), false, sourceIndexOf (c.points[i].modCurve.source), c.points[i].modCurve.amount, true });
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

    for (const ContentRemap& r : content.remaps)
    {
        shapes::ShapeData shape;
        if (r.osc < 1 || r.osc > 3 || r.slot < 1 || r.slot > kNumRemapSlots || ! buildShape (r.points, false, shape))
            continue;
        auto& e = s->remap[static_cast<size_t> (r.osc - 1)][static_cast<size_t> (r.slot - 1)];
        shape.bake (e.table.data(), e.table.size());
        e.present = true;
    }

    if (store != nullptr)
    {
        // GROUPC WP-13: the instruments: the SFZ text and the samples its regions name (files of the same pack, relative to the folder of the SFZ file); decoded here, off the audio thread
        for (const ContentInstrument& in : content.instruments)
        {
            std::vector<uint8_t> bytes;
            if (in.osc < 1 || in.osc > 3 || ! store->readFile (in.ref, bytes))
                continue;
            const multisample::SfzInstrument sfz = multisample::parseSfz (std::string (bytes.begin(), bytes.end()));
            const size_t slash = in.ref.file.rfind ('/');
            const std::string folder = slash == std::string::npos ? std::string() : in.ref.file.substr (0, slash + 1);
            auto instrument = std::make_unique<multisample::Instrument>();
            const IContentStore* source = store;
            const std::string pack = in.ref.pack;
            instrument->build (sfz, [source, pack, folder] (const std::string& relative, std::vector<uint8_t>& out)
                               {
                                   ContentFileRef ref;
                                   ref.pack = pack;
                                   ref.file = folder + relative;
                                   return source->readFile (ref, out);
                               });
            s->instrument[static_cast<size_t> (in.osc - 1)] = std::move (instrument);
        }
    }

    static std::atomic<uint32_t> spectralFilterIds { 1 };
    for (const ContentRemap& r : content.spectralFilters)
    {
        shapes::ShapeData shape;
        if (r.osc < 1 || r.osc > 3 || ! buildShape (r.points, false, shape))
            continue;
        auto& e = s->spectralFilter[static_cast<size_t> (r.osc - 1)];
        shape.bake (e.table.data(), e.table.size());
        e.present = true;
        e.id = spectralFilterIds.fetch_add (1);
    }

    for (const ContentModCurve& mc : content.modCurves)
    {
        if (mc.slot >= 1 && mc.slot <= kNumModSlots)
        {
            auto& e = s->modCurves[static_cast<size_t> (mc.slot - 1)];
            if (mc.points.size() >= 2)
            {
                e.present = true;
                e.points = mc.points;
            }
        }
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

    ~VitalEngine() override
    {
        resetPool(); // the pool and its packages go before the engine whose chain they were running in
    }

    void prepare (double newSampleRate, int /*maxBlockFrames*/, const Quality& q) override
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        setQualityValues (q);
        build();
        reset();
    }

    bool needsPrepare() const noexcept override { return oversample4xOn() != preparedOversample4x; }

    bool oversample4xOn() const noexcept
    {
        return ultraRender.load (std::memory_order_relaxed)
               || (oversample4xParam >= 0 && params[static_cast<size_t> (oversample4xParam)].load (std::memory_order_relaxed) > 0.5f);
    }

    void setUltraRender (bool on) noexcept override { ultraRender.store (on, std::memory_order_relaxed); }

    /** The host stopped: every note ends now and the tails of the effects are cleared (A381). The controllers (bend, mod wheel, pedal position of the
        wheels) and the parameters stay; the note book keeping of the adapter starts empty. */
    void silence() noexcept override
    {
        if (engine == nullptr)
            return;
        engine->allSoundsOff(); // the voices, the effect chain (hardReset), the decimator
        for (auto& channelKeys : activeKey)
            channelKeys.fill (-1);
        pedalChannels = 0;
        noteGroup->held = false;
        ++noteGroup->group;
        noteIds.fill (NoteIdSlot {});
        activeCount.store (0, std::memory_order_relaxed);
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
        appliedNoiseSample = 0;
        appliedNoiseUser = nullptr;
        if (noiseSample_ != nullptr)
            noiseSample_->setNoiseType (0);
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

    void restartEffectPhases() noexcept override
    {
        if (engine != nullptr)
            engine->correctToTime (0.0);
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
        return i >= 0 && capsCover (capabilities(), requiredCaps (static_cast<size_t> (i))) && capsCover2 (capabilities2(), requiredCaps2 (static_cast<size_t> (i))); // the keys of state version 3 need their capability bits
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
        updateReadout();
    }

    EngineCaps2Mask capabilities2() const noexcept override
    {
        return kCap2Granular   // GROUPC WP-11 (B28): proven by core/engine-vital/tests/test_granular_osc.cpp
             | kCap2Spectral | kCap2SpectralWarps // GROUPC WP-12 (B29): proven by core/engine-vital/tests/test_spectral_osc.cpp
             | kCap2Multisample // GROUPC WP-13 (B30): proven by core/engine-vital/tests/test_multisample_osc.cpp
             | kCap2ModSlew | kCap2ModDelay // REMAINDER item 1 (B31): proven by core/engine-vital/tests/test_modslew.cpp
             | kCap2SpectralFilterTable; // REMAINDER item 2 (B32): proven by core/engine-vital/tests/test_spectral_filter_table.cpp
    }

    EngineCapsMask capabilities() const noexcept override
    {
        // a bit is set only for an area that has a test rendering the key on and off (core/engine-vital/tests/test_wp123.cpp,
        // core/tests/test_events.cpp); docs/ui-design/WP123-REPORT.md lists what each bit covers and what it does not
        return kCapEventContract | kCapMacros58 | kCapLfo910 | kCapModSourcesExtra | kCapAuxSource | kCapLfoOptions | kCapEnvBeats | kCapCurves
             | kCapCustomLfoShape | kCapKeyScale | kCapTuning | kCapMpe | kCapOscMapping | kCapSampleOsc | kCapUserWavetable | kCapWarp2 | kCapMixerGraph | kCapFxRacks | kCapFilterExtras | kCapFilterModels | kCapFilterModels2 | kCapReverbForce
             | kCapFxUtility | kCapFxHyperDimension | kCapFxBode | kCapFxConvolve | kCapFxSplitters
             | kCapWtRemap | kCapFxLevel | kCapCleanMode // VERIFY: the three B16 bits stay only if the on / off renders of test_wtremap.cpp differ
             | kCapPitchUnison
             | kCapFxExtrasAll // block B17: proven by core/engine-vital/tests/test_fxextra.cpp
             | kCapSubOsc | kCapNoiseExtras // block B18: proven by core/engine-vital/tests/test_oscextra.cpp
             | kCapVoiceSteal | kCapPolyLegato | kCapBendDownRange | kCapStereoSpread | kCapLfoSwing | kCapQualityOptions
             | kCapModAmount // block B20: proven by core/engine-vital/tests/test_modamt.cpp
             | kCapWarpAll | kCapEnv1Toggle // block B21: proven by core/engine-vital/tests/test_warpenv.cpp
             | kCapFxInstances // block B22: proven by core/engine-vital/tests/test_fxinstances.cpp (a slot of every type renders what instance 0 renders)
             | kCapSubPhase | kCapNoiseColour | kCapNoiseSample // block B23 (GROUPA1 WP-04): proven by core/engine-vital/tests/test_noisesample.cpp
             | kCapMpeRouting // block B24 (GROUPA1 WP-09): proven by core/engine-vital/tests/test_mpe.cpp
             | kCapFxDelayOffsetLeft | kCapReverbFeedback // block B25 (GROUPA1 WP-10): proven by core/engine-vital/tests/test_fxextra.cpp
             | kCapVoiceControl | kCapVoiceSources // block B26 (GROUPA2 WP-07): proven by core/engine-vital/tests/test_voicecontrol.cpp
             | kCapOscExtras2 // block B27 (GROUPB WP-05): proven by core/engine-vital/tests/test_oscextra2.cpp
             | kCapModSources2 | kCapWarpSources // GROUPB WP-06: proven by core/engine-vital/tests/test_modsources.cpp
             | kCapWavetableSawDown; // GROUPB WP-18: proven by core/engine-vital/tests/test_modsources.cpp (saw_down_*)
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

    /** Offline render (offline_render.h, GROUPD WP-15): a private instance of this engine with a copy of the parameters, the content and the store. This
        engine is only read (atomic parameter loads, the content copy under the mutex the message thread already uses), so the audio thread is not
        disturbed and no state of this engine changes. */
    bool renderOffline (const OfflineRequest& request, OfflineAudio& out) override
    {
        VitalEngine twin;
        for (size_t i = 0; i < kMaxParams; ++i)
            twin.params[i].store (params[i].load (std::memory_order_relaxed), std::memory_order_relaxed);
        twin.tuningSource.store (tuningSource.load (std::memory_order_acquire), std::memory_order_release);
        StateContent content;
        const IContentStore* store;
        {
            const std::lock_guard<std::mutex> lock (snapshotMutex);
            content = lastContent;
            store = contentStore;
        }
        return runOfflineRequest (twin, request, out, [&content, store] (IEngine& e)
        {
            e.setContent (content); // before the store: nothing is decoded twice
            e.setContentStore (store);
        });
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
        snapshot->impulse = impulseData;
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

    bool hasPoolModule (int slot) const noexcept
    {
        return pool != nullptr && slot >= 0 && slot < kNumFxInstances && poolRt[static_cast<size_t> (slot)].active != nullptr;
    }

    int poolBuilds() const noexcept { return pool != nullptr ? pool->pendingBuilds() : 0; }

    bool poolDestinationValid (int slot, int number) const noexcept
    {
        if (slot < 0 || slot >= kNumFxInstances || number < 0 || number >= kFxInstanceParams)
            return false;
        const int choice = poolChoice[static_cast<size_t> (slot)][static_cast<size_t> (number)];
        return choice > 0 && static_cast<size_t> (choice) < destinations.size() && destinations[static_cast<size_t> (choice)].valid;
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
        int sourceParam = -1, destinationParam = -1, amountParam = -1;
        bool eligible = true;          // the destination is live (an envelope time of the selected unit)
        int auxSourceParam = -1, auxInvParam = -1, auxPowerParam = -1, auxOutputParam = -1;
        int delaySyncParam = -1;   // REMAINDER item 1: modNN.delay_sync (rise, fall, delay and delay_beats are bound controls)
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
        resetPool();
        engine.reset();
        tuning.reset();
        if (tables == nullptr)
            tables = std::make_unique<vitaltables::WavetableSet>(); // the built-in wavetables: built once per engine object

        // Every random generator of the engine (random LFOs, the noise sample, ...) is seeded from a counter: start it at 0 for this
        // engine, after the one process-wide table that also takes a number, so that two engines of the same patch sound the same.
        sbvital::RandomValues::instance();
        sbvital::utils::RandomGenerator::next_seed_ = 0;
        engine = std::make_unique<sbvital::SoundEngine>();
        pool = std::make_unique<FxPool> (*engine);
        poolRt.fill (PoolRt {});
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
        noiseSample_ = engine->getSample();

        {
            const std::lock_guard<std::mutex> lock (snapshotMutex);
            sbvital::setConvolveImpulse (engine->getEffectModule (sbvital::ReorderableEffectChain::kConvolve), impulse);
        }
        sbvital::setConvolveRealtime (engine->getEffectModule (sbvital::ReorderableEffectChain::kConvolve), qRealtime.load (std::memory_order_relaxed));
        engine->setSampleRate (static_cast<int> (std::lround (sampleRate)));
        engine->setBpm (120.0f);
        controlMap = engine->getControls();
        // the oversampling of the tier is fixed for the life of the buffers (IEngine::setQuality)
        // quality.oversample_4x is part of that: it is read here and applied by the next prepare () (needsPrepare ())
        preparedOversample4x = oversample4xOn();
        setControl ("oversampling", preparedOversample4x ? 2.0f : (qOversampling.load() >= 2 ? 1.0f : 0.0f));
        engine->checkOversampling();
        engine->updateAllModulationSwitches();
        controlMap = engine->getControls();

        applyFixedControls();
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
        setControl ("stereo_mode", 0.0f);
        setControl ("voice_amplitude", 1.0f);
        for (int i = 1; i <= kNumOsc; ++i)
        {
            const std::string c = "osc_" + std::to_string (i) + "_";
            setControl (c + "spectral_unison", 1.0f);
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
            if (binding.control.compare (0, 14, "macro_control_") == 0 && binding.control.size() == 15 && binding.control[14] >= '1' && binding.control[14] <= '8')
                macroResolved[static_cast<size_t> (binding.control[14] - '1')] = resolved.size() - 1;   // GROUPA1 (WP-09): the MPE axes write these controls
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
            else if (key == "noise.sample")
                noiseSampleParam = static_cast<int> (i);
            else if (key == "noise.type")
                noiseTypeParam = static_cast<int> (i);
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
                "sample.scan_range", "sample.scan_reverse", "sample.scan_lock_tempo", "sample.scan_to_bpm", "sample.length_beats",
                "gran.window_shape", "gran.density_mode", "gran.density_sync", "gran.density_grains", "gran.length_mode", "gran.length_sync", "gran.jump_start",
                "gran.max_grains", "gran.loop_grains", "gran.manual", "gran.scan_keytrack", "gran.reverse_grains", "gran.spawn_pattern", "gran.y_axis",
                "spec.lohi_smooth", "spec.lohi_post_warp", "spec.manual", "spec.scan_keytrack", "spec.phase_lock", "spec.transients", "spec.y_axis",
                "spec.flt_use", "spec.flt_table",
                "multi.override", "multi.delay", "multi.attack", "multi.hold", "multi.decay", "multi.sustain", "multi.release", "multi.vel_track", "multi.vel_override" };
            for (int n = 1; n <= kNumOsc; ++n)
            {
                const std::string k = "osc" + std::to_string (n) + ".";
                for (int w = 0; w < kSpCount; ++w)
                    sampleParams[static_cast<size_t> (n - 1)][static_cast<size_t> (w)] = find (k + names[w]);
                frameParams[static_cast<size_t> (n - 1)][0] = find (k + "frame");
                frameParams[static_cast<size_t> (n - 1)][1] = find (k + "frame_spread");
            }
        }
        for (int n = 1; n <= kNumOsc; ++n)
        {
            const char* const names[4] = { "on", "slot", "rise", "fall" };
            for (int m = 0; m < 4; ++m)
                remapParam[static_cast<size_t> (n - 1)][static_cast<size_t> (m)] = find ("osc" + std::to_string (n) + ".remap." + names[m]);
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
        // block B26 (GROUPA2, WP-07): the voice control
        {
            vcLengthParam = find ("voicectl.length");
            static const char* const kRows[6] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
            for (int n = 1; n <= 8; ++n)
                for (int r = 0; r < 6; ++r)
                    vcStepParam[static_cast<size_t> ((n - 1) * 6 + r)] = find ("voicectl.s" + std::to_string (n) + "." + kRows[r]);
            static const char* const kRand[5] = { "pan", "detune", "cutoff", "env", "detune10x" };
            for (int r = 0; r < 5; ++r)
                vcRandParam[static_cast<size_t> (r)] = find (std::string ("voicectl.rand.") + kRand[r]);
            vcScaleEnvParam = find ("voicectl.scale.env");
            vcScaleLfoParam = find ("voicectl.scale.lfo");
            vcScaleLfoModeParam = find ("voicectl.scale.lfo_mode");
            static const char* const kTargets[5] = { "a", "b", "c", "noise", "sub" };
            for (int t = 0; t < 5; ++t)
                vcTargetParam[static_cast<size_t> (t)] = find (std::string ("voicectl.osc.") + kTargets[t]);
        }
        mpeOnParam = find ("master.mpe.on");
        mpeRangeParam = find ("master.mpe.bend_range");
        mpeXyzMacroParam = find ("master.mpe.xyz_macro");
        mpeYModWheelParam = find ("master.mpe.y_modwheel");
        mpeBendToXParam = find ("master.mpe.bend_to_x");
        mpeYBipolarParam = find ("master.mpe.y_bipolar");
        reverbForceParam = find ("reverb.force_enable");
        sameNoteParam = find ("master.voice.same_note");
        bendDownRangeParam = find ("master.bend_down_range");
        bendDownLinkParam = find ("master.bend_down_link");
        oversample4xParam = find ("quality.oversample_4x");
        noSmoothingParam = find ("quality.no_smoothing");
    }

    /** The tables of the modulation matrix: sources, destinations, the 64 connection processors. Everything the audio thread needs
        to wire a slot is looked up here (the lookups take std::string and walk maps: allocation, not for process()). */
    void resolveModulation()
    {
        for (size_t k = 0; k < static_cast<size_t> (kNumModSources); ++k)
            sourceOutput[k] = engine->getModulationSource (vitalSourceName (static_cast<int> (k)));
        for (size_t k = 0; k < discreteOutput.size(); ++k) // GROUPB WP-06: Note-On Rand (Discrete) gives every slot its own number
            discreteOutput[k] = engine->getModulationSource ("note_on_discrete_" + std::to_string (k + 1));

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
            d.valid = (d.mono != nullptr || d.poly != nullptr) && (d.monoSwitch != nullptr || d.polySwitch != nullptr) && sbvital::Parameters::isParameter (name);
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
                if (d.mono != nullptr)
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

        // the destinations of the pool slots (block B22) are filled in when a module is handed over (handoverPool ())
        for (int s = 0; s < kNumFxInstances; ++s)
            for (int k = 0; k < kFxInstanceParams; ++k)
                poolChoice[static_cast<size_t> (s)][static_cast<size_t> (k)] = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (fxInstanceKeys (s).p[k])));

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
            s.amountParam = p.amount;
            s.auxSourceParam = p.auxSource;
            s.auxInvParam = p.auxInv;
            s.auxPowerParam = p.auxPower;
            s.auxOutputParam = p.auxOutput;
            s.delaySyncParam = p.delaySync;
        }

        resolveReadout();
    }

    /** GROUPA2 (WP-17): the pointers the audio thread reads for the display, looked up here once (the lookups walk maps). */
    void resolveReadout()
    {
        destReadout.assign (paramCount(), DestReadout {});
        for (size_t i = 0; i < paramCount(); ++i)
        {
            const Binding* b = vitalBindingOf (i);
            if (b == nullptr || b->frameOsc != 0 || b->conv == Conv::index || b->conv == Conv::indexMap || ! sbvital::Parameters::isParameter (b->control))
                continue;
            destReadout[i].binding = b;
        }
        // what a connection adds to its destination, as the Vital display reads it: "modulation_amount_N" is amount x source in units of the destination's range
        for (int k = 0; k < kNumSlots; ++k)
            amountStatus[static_cast<size_t> (k)] = engine->getStatusOutput ("modulation_amount_" + std::to_string (k + 1));
        for (int k = 0; k < kNumModSources; ++k)
            sourceStatus[static_cast<size_t> (k)] = engine->getStatusOutput (vitalSourceName (k));
        for (int n = 0; n < kNumMacro; ++n)
            macroValue[static_cast<size_t> (n)] = control ("macro_control_" + std::to_string (n + 1));
        for (int n = 0; n < kNumLfo; ++n)
            lfoPhaseStatus[static_cast<size_t> (n)] = engine->getStatusOutput ("lfo_" + std::to_string (n + 1) + "_phase");
        for (int n = 0; n < 6; ++n)
            envPhaseStatus[static_cast<size_t> (n)] = engine->getStatusOutput ("env_" + std::to_string (n + 1) + "_phase");
    }

    void setReadoutTargets (const int* paramIndices, int count) noexcept override
    {
        const int n = paramIndices == nullptr ? 0 : std::clamp (count, 0, LiveValues::kMaxTargets);
        for (int i = 0; i < n; ++i)
            readoutTarget[static_cast<size_t> (i)].store (paramIndices[i], std::memory_order_relaxed);
        readoutCount.store (n, std::memory_order_release);
    }

    bool copyReadout (LiveValues& out) const noexcept override
    {
        out.block = readoutBlock.load (std::memory_order_acquire);
        if (out.block == 0)
            return false;
        out.activeVoices = readoutVoices.load (std::memory_order_relaxed);
        for (int k = 0; k < LiveValues::kMaxSources; ++k)
            out.source[k] = readoutSource[static_cast<size_t> (k)].load (std::memory_order_relaxed);
        for (int n = 0; n < LiveValues::kNumLfos; ++n)
            out.lfoPhase[n] = readoutLfoPhase[static_cast<size_t> (n)].load (std::memory_order_relaxed);
        for (int n = 0; n < LiveValues::kNumEnvs; ++n)
        {
            out.envStage[n] = readoutEnvStage[static_cast<size_t> (n)].load (std::memory_order_relaxed);
            out.envPosition[n] = readoutEnvPosition[static_cast<size_t> (n)].load (std::memory_order_relaxed);
        }
        for (int n = 0; n < kNumOsc; ++n)
        {
            out.multisampleZone[n] = readoutMultiZone[static_cast<size_t> (n)].load (std::memory_order_relaxed);
            out.multisampleKey[n] = readoutMultiKey[static_cast<size_t> (n)].load (std::memory_order_relaxed);
        }
        out.numTargets = std::min (readoutShown.load (std::memory_order_acquire), LiveValues::kMaxTargets);
        for (int i = 0; i < out.numTargets; ++i)
            out.target[i] = readoutValue[static_cast<size_t> (i)].load (std::memory_order_relaxed);
        return true;
    }

    /** End of every block (audio thread): the sources, the phases and the modulated value of the subscribed knobs. */
    void updateReadout() noexcept
    {
        const int voices = engine->getNumActiveVoices();
        readoutVoices.store (voices, std::memory_order_relaxed);
        for (int n = 0; n < kNumOsc; ++n)
            if (sampleShared[static_cast<size_t> (n)] != nullptr)
            {
                readoutMultiZone[static_cast<size_t> (n)].store (sampleShared[static_cast<size_t> (n)]->multi.last_zone, std::memory_order_relaxed);
                readoutMultiKey[static_cast<size_t> (n)].store (sampleShared[static_cast<size_t> (n)]->multi.last_key, std::memory_order_relaxed);
            }
        const auto clean = [] (float v) noexcept { return std::isfinite (v) && std::fabs (v) < 1.0e6f ? v : 0.0f; };
        for (int k = 0; k < kNumModSources; ++k)
        {
            const sbvital::StatusOutput* s = sourceStatus[static_cast<size_t> (k)];
            float v = s != nullptr ? clean (s->value()[0]) : 0.0f;
            // the macros are global: their control is the value, also while no voice sounds (the status output only follows the voices)
            const int macro = k >= kSourceMacro1 && k < kSourceMacro1 + 4 ? k - kSourceMacro1 : (k >= kSourceMacro5 && k < kSourceMacro5 + 4 ? k - kSourceMacro5 + 4 : -1);
            if (macro >= 0 && macroValue[static_cast<size_t> (macro)] != nullptr)
                v = clean (macroValue[static_cast<size_t> (macro)]->value());
            readoutSource[static_cast<size_t> (k)].store (v, std::memory_order_relaxed);
        }
        for (int n = 0; n < kNumLfo; ++n)
        {
            const sbvital::StatusOutput* s = lfoPhaseStatus[static_cast<size_t> (n)];
            float phase = -1.0f;
            if (s != nullptr && voices > 0)
            {
                const float encoded = clean (s->value()[0]);
                phase = sbvital::utils::decodePhaseAndVoice (sbvital::poly_float (encoded)).first[0];
            }
            readoutLfoPhase[static_cast<size_t> (n)].store (phase, std::memory_order_relaxed);
        }
        for (int n = 0; n < 6; ++n)
        {
            const sbvital::StatusOutput* s = envPhaseStatus[static_cast<size_t> (n)];
            float stage = 0.0f, position = 0.0f;
            if (s != nullptr && voices > 0)
            {
                const float raw = clean (s->value()[0]);
                const float state = std::floor (raw);
                position = std::clamp (raw - state, 0.0f, 1.0f);
                // VoiceEvent: 2 on (attack), 3 hold, 4 decay, 5 off (release), 6 kill; 0 / 1 invalid / idle
                stage = state >= 2.0f ? std::min (state - 1.0f, 5.0f) : 0.0f;
            }
            readoutEnvStage[static_cast<size_t> (n)].store (stage, std::memory_order_relaxed);
            readoutEnvPosition[static_cast<size_t> (n)].store (position, std::memory_order_relaxed);
        }

        const int count = readoutCount.load (std::memory_order_acquire);
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (int i = 0; i < count; ++i)
        {
            const int index = readoutTarget[static_cast<size_t> (i)].load (std::memory_order_relaxed);
            float live = 0.0f;
            if (index >= 0 && static_cast<size_t> (index) < destReadout.size())
            {
                const float plain = params[static_cast<size_t> (index)].load (std::memory_order_relaxed);
                live = plain;
                const DestReadout& r = destReadout[static_cast<size_t> (index)];
                if (r.binding != nullptr)
                {
                    // the sum of the connections that end here (the slots are wired by this thread: their fields are read without a lock)
                    const int choice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (index)));
                    double delta = 0.0;
                    if (choice > 0 && static_cast<size_t> (choice) < destinations.size())
                    {
                        const double scale = destinations[static_cast<size_t> (choice)].scale;
                        for (int k = 0; k < kNumSlots; ++k)
                        {
                            const Slot& slot = slots[static_cast<size_t> (k)];
                            const sbvital::StatusOutput* amount = amountStatus[static_cast<size_t> (k)];
                            if (slot.connected && slot.destination == choice && amount != nullptr)
                                delta += static_cast<double> (clean (amount->value()[0])) * scale;
                        }
                    }
                    if (delta != 0.0 && std::isfinite (delta))
                    {
                        const double raw = convertParam (*r.binding, static_cast<double> (plain), sr, transport.bpm) + delta;
                        const double inverted = invertParam (*r.binding, raw, sr, transport.bpm);
                        if (std::isfinite (inverted))
                        {
                            const ParamInfo& info = paramTable()[static_cast<size_t> (index)];
                            live = static_cast<float> (std::clamp (inverted, static_cast<double> (info.min), static_cast<double> (info.max)));
                        }
                    }
                }
            }
            readoutValue[static_cast<size_t> (i)].store (live, std::memory_order_relaxed);
        }
        readoutShown.store (count, std::memory_order_release);
        readoutBlock.store (readoutBlock.load (std::memory_order_relaxed) + 1u, std::memory_order_release);
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
            appliedQualityGeneration = generation;
        }

        applyMpeMacroMap();          // before the bindings: the macros that were driven by an MPE axis take their knobs back in this pass
        applyScaleAndTuning (force); // first: the concert pitch it computes is added to the transposition below
        applyWavetables();           // before the bindings: the frame scale of a user table

        // quality.no_smoothing
        const bool noSmoothing = noSmoothingParam >= 0 && params[static_cast<size_t> (noSmoothingParam)].load (std::memory_order_relaxed) > 0.5f;
        sbvital::SmoothValue::sBypassSmoothing = noSmoothing;

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
        applyPool (force, fxMask, qualityChanged);
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

    // ---- the effect instance pool (block B22, docs/ui-design/18-FX-INSTANCES.md) -----------------------------------------------------------
    struct PoolRt
    {
        FxBuilt* active = nullptr;   // the instance the chain has (null: the slot is empty or its module is not ready)
        int wanted = 0;              // the type that was asked for last
        uint32_t generation = 0;     // pool request number of `wanted`
        bool requested = false;      // a request for `wanted` has been posted and is not yet taken
    };

    /** Frees the packages and the pool (message / prepare thread; the audio thread is not running). */
    void resetPool()
    {
        if (pool == nullptr)
            return;
        pool.reset(); // deletes every module, the live ones in the chain included
        for (auto& rt : poolRt)
        {
            delete rt.active;
            rt = PoolRt {};
        }
    }

    /** The modulation slots wired to a destination of a pool slot are unwired (before the module they point into goes away). */
    void disconnectPoolSlots (int s) noexcept
    {
        for (int k = 0; k < kNumSlots; ++k)
        {
            Slot& slot = slots[static_cast<size_t> (k)];
            if (! slot.connected)
                continue;
            for (int choice : poolChoice[static_cast<size_t> (s)])
                if (choice > 0 && choice == slot.destination)
                {
                    disconnectSlot (k);
                    break;
                }
        }
    }

    /** Takes a built instance over (or null: empties the slot): unwires the modulation, gives the module to the chain, publishes the
        destinations. Audio thread. */
    void handoverPool (int s, FxBuilt* next) noexcept
    {
        PoolRt& rt = poolRt[static_cast<size_t> (s)];
        disconnectPoolSlots (s);
        FxBuilt* previous = rt.active;
        auto* chain = engine->effectChain();
        chain->setPoolModule (s, next != nullptr ? next->module : nullptr);
        if (next != nullptr)
            next->handedOver = true;
        rt.active = next;
        for (int k = 0; k < kFxInstanceParams; ++k)
        {
            const int choice = poolChoice[static_cast<size_t> (s)][static_cast<size_t> (k)];
            if (choice <= 0 || static_cast<size_t> (choice) >= destinations.size())
                continue;
            Destination d {};
            if (next != nullptr && k < next->count)
            {
                const FxBuilt::Param& p = next->params[static_cast<size_t> (k)];
                d.mono = p.mono;
                d.monoSwitch = p.monoSwitch;
                d.scale = p.scale;
                d.valid = p.mono != nullptr && p.monoSwitch != nullptr;
            }
            destinations[static_cast<size_t> (choice)] = d;
        }
        ++matrixGeneration; // the slots that were unwired (or waited for this destination) try again
        pool->release (previous);
        pool->collectNow();
    }

    /** Parameters, content and routing of the pool slots; builds and hands over modules. Audio thread (force: prepare / reset, may allocate). */
    void applyPool (bool force, uint32_t fxMask, bool qualityChanged) noexcept
    {
        if (pool == nullptr)
            return;
        const bool synchronous = force || ! qRealtime.load (std::memory_order_relaxed);
        for (int s = 0; s < kNumFxInstances; ++s)
        {
            const FxInstanceKeys& keys = fxInstanceKeys (s);
            PoolRt& rt = poolRt[static_cast<size_t> (s)];
            auto value = [&] (int index) { return params[static_cast<size_t> (index)].load (std::memory_order_relaxed); };
            int type = static_cast<int> (value (keys.type) + 0.5f);
            if (type < 0 || type >= kNumFxInstanceTypes)
                type = 0;

            if (type != rt.wanted || (type != 0 && ! rt.requested && rt.active == nullptr))
            {
                rt.wanted = type;
                rt.requested = false;
                if (type == 0)
                {
                    pool->cancel (s); // supersedes a build in progress; type 0 builds nothing
                    handoverPool (s, nullptr);
                }
                else
                {
                    float normalised[kFxInstanceParams];
                    for (int k = 0; k < kFxInstanceParams; ++k)
                        normalised[k] = value (keys.p[k]);
                    if (! synchronous && rt.active != nullptr)
                        handoverPool (s, nullptr); // dry (10 ms crossfade) until the new module is ready
                    rt.requested = pool->request (s, type, normalised, synchronous);
                    rt.generation = pool->generation (s);
                }
            }

            if (rt.requested)
                if (FxBuilt* built = pool->takeReady (s, rt.generation))
                {
                    if (built->type == rt.wanted)
                    {
                        handoverPool (s, built);
                        rt.requested = false;
                    }
                    else
                        pool->release (built);
                }

            FxBuilt* b = rt.active;
            if (b != nullptr)
            {
                if (qualityChanged)
                    sbvital::setConvolveRealtime (b->module->effect(), qRealtime.load (std::memory_order_relaxed));
                if (b->contentId != snapshotId)
                {
                    b->contentId = snapshotId;
                    if (auto* curve = b->module->filterCurve())
                    {
                        if (activeSnapshot != nullptr)
                            *curve = activeSnapshot->filterCurve[2];
                        else
                            curve->num_bells = 0;
                    }
                    if (auto* distortion = b->module->distortion())
                    {
                        const bool a = activeSnapshot != nullptr && activeSnapshot->hasShaper[0], c = activeSnapshot != nullptr && activeSnapshot->hasShaper[1];
                        distortion->setShaperCurves (a ? activeSnapshot->shaperTable[0].data() : nullptr, c ? activeSnapshot->shaperTable[1].data() : nullptr);
                    }
                    if (b->type == kFxTypeConvolve)
                        sbvital::setConvolveImpulse (b->module->effect(), activeSnapshot != nullptr ? activeSnapshot->impulse : nullptr);
                }
                for (int k = 0; k < b->count; ++k)
                {
                    FxBuilt::Param& p = b->params[static_cast<size_t> (k)];
                    const float norm = value (keys.p[k]);
                    if (p.control == nullptr || (! force && norm == b->appliedNorm[static_cast<size_t> (k)]))
                        continue;
                    b->appliedNorm[static_cast<size_t> (k)] = norm;
                    const double plain = fromNormalised (*p.source, norm);
                    const double converted = convertParam (*p.binding, plain, sampleRate, transport.bpm);
                    p.control->set (static_cast<float> (std::clamp (converted, p.lo, p.hi)));
                }
            }

            const int module = fxTypeDescriptor (type).module;
            const bool tierAllows = module >= 0 && (fxMask & (1u << module)) != 0;
            engine->effectChain()->setPoolConfig (s, b != nullptr && value (keys.on) > 0.5f && tierAllows, static_cast<int> (value (keys.rack) + 0.5f),
                                                  static_cast<int> (value (keys.order) + 0.5f));
        }
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
            if (index == kWavetableSawDown)
                index = vitaltables::kSawDownSlot; // GROUPB WP-18: the built-in falling saw sits in the slot behind the 13 tables
            else if (index >= kNumBuiltinWavetables)
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

    /** oscN.remap.*: the position remap of each oscillator (on, the drawn curve of the slot from the content, RISE / FALL) handed to the
        FrameRemap of its wavetable. Cheap every block; the 257 table values are copied when the slot or the content changed. */
    void applyRemaps() noexcept
    {
        for (size_t i = 0; i < static_cast<size_t> (kNumOsc); ++i)
        {
            if (remapParam[i][0] < 0)
                continue;
            sbvital::Wavetable* wavetable = engine->getWavetable (static_cast<int> (i));
            if (wavetable == nullptr)
                continue;
            sbvital::FrameRemap* remap = wavetable->frameRemap();
            const auto value = [&] (size_t m) { return params[static_cast<size_t> (remapParam[i][m])].load (std::memory_order_relaxed); };
            const int slot = std::clamp (static_cast<int> (value (1) + 0.5f), 0, kNumRemapSlots - 1);
            const uint64_t key = (static_cast<uint64_t> (snapshotId) << 8) | static_cast<uint64_t> (slot);
            if (key != appliedRemapKey[i])
            {
                appliedRemapKey[i] = key;
                const Snapshot::RemapEntry* entry = activeSnapshot != nullptr ? &activeSnapshot->remap[i][static_cast<size_t> (slot)] : nullptr;
                if (entry != nullptr && entry->present)
                    std::copy (entry->table.begin(), entry->table.end(), remap->table);
                else
                    remap->setIdentity(); // a slot nobody drew: the identity
            }
            remap->rise_ms = value (2);
            remap->fall_ms = value (3);
            remap->max_frame = userFramesNow[i] > 1 ? static_cast<float> (userFramesNow[i] - 1) : static_cast<float> (vitaltables::kMorphFrames - 1);
            remap->on = value (0) > 0.5f;
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
            // a Multisample oscillator plays its instrument, not a sample: the view stays valid (it gates the processor), an instrument without a sample is a silent oscillator
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
            {
                auto& g = sh->gran;
                const auto I = [&] (int what) { return static_cast<int> (P (what) + 0.5f); };
                g.window_shape = I (kSpGranWindowShape);
                g.density_mode = I (kSpGranDensityMode);
                g.density_sync = I (kSpGranDensitySync);
                g.density_grains = I (kSpGranDensityGrains);
                g.length_mode = I (kSpGranLengthMode);
                g.length_sync = I (kSpGranLengthSync);
                g.jump_start = P (kSpGranJumpStart) > 0.5f;
                g.max_grains = I (kSpGranMaxGrains);
                g.loop_grains = P (kSpGranLoopGrains) > 0.5f;
                g.manual = P (kSpGranManual) > 0.5f;
                g.scan_keytrack = P (kSpGranScanKeytrack) > 0.5f;
                g.reverse_grains = P (kSpGranReverseGrains) > 0.5f;
                g.spawn_pattern = I (kSpGranSpawnPattern);
                g.y_axis = I (kSpGranYAxis);
            }
            {
                auto& g = sh->spec;
                g.analysis = entry != nullptr && entry->analysis.valid() ? &entry->analysis : nullptr;
                g.lohi_smooth = P (kSpSpecLohiSmooth) > 0.5f;
                g.lohi_post_warp = P (kSpSpecLohiPostWarp) > 0.5f;
                g.manual = P (kSpSpecManual) > 0.5f;
                g.scan_keytrack = P (kSpSpecScanKeytrack) > 0.5f;
                g.phase_lock = P (kSpSpecPhaseLock) > 0.5f;
                g.transients = P (kSpSpecTransients) > 0.5f;
                g.y_axis = static_cast<int> (P (kSpSpecYAxis) + 0.5f);
                auto& m = sh->multi;
                m.instrument = activeSnapshot != nullptr && activeSnapshot->instrument[static_cast<size_t> (i)] != nullptr && activeSnapshot->instrument[static_cast<size_t> (i)]->numPlayable() > 0
                                   ? activeSnapshot->instrument[static_cast<size_t> (i)].get() : nullptr;
                m.envelope_override = P (kSpMultiOverride) > 0.5f;
                m.envelope.delay = P (kSpMultiDelay) * 0.001f;
                m.envelope.attack = P (kSpMultiAttack) * 0.001f;
                m.envelope.hold = P (kSpMultiHold) * 0.001f;
                m.envelope.decay = P (kSpMultiDecay) * 0.001f;
                m.envelope.sustain = P (kSpMultiSustain) * 0.01f;
                m.envelope.release = P (kSpMultiRelease) * 0.001f;
                m.vel_track = P (kSpMultiVelTrack);
                m.vel_override = P (kSpMultiVelOverride) > 0.5f;
                const Snapshot::SpectralFilterEntry* filter = activeSnapshot != nullptr && activeSnapshot->spectralFilter[static_cast<size_t> (i)].present
                                                                  ? &activeSnapshot->spectralFilter[static_cast<size_t> (i)] : nullptr;
                g.has_filter = filter != nullptr;
                if (filter != nullptr && g.filter_id != filter->id)
                {
                    g.filter_id = filter->id;
                    std::copy (filter->table.begin(), filter->table.end(), g.filter_table);
                }
                // a wavetable as the filter (REMAINDER item 2): the filter curves of the frames of the chosen table; the drawn curve answers while the table has none
                g.flt_bank = nullptr;
                g.flt_frames = 0;
                if (P (kSpSpecFltUse) > 0.5f)
                {
                    const int choice = static_cast<int> (P (kSpSpecFltTable) + 0.5f);
                    if (choice == kWavetablePresetTable)
                    {
                        if (activeSnapshot != nullptr && activeSnapshot->tableBankFrames[static_cast<size_t> (i)] > 0)
                        {
                            g.flt_bank = activeSnapshot->tableBank[static_cast<size_t> (i)].data();
                            g.flt_frames = activeSnapshot->tableBankFrames[static_cast<size_t> (i)];
                            g.flt_bank_id = 0x80000000u + snapshotId * 4u + static_cast<uint32_t> (i);
                        }
                    }
                    else if (tables != nullptr)
                    {
                        const int slot = choice == kWavetableSawDown ? vitaltables::kSawDownSlot : std::clamp (choice, 0, kNumBuiltinWavetables - 1);
                        g.flt_bank = tables->filterBank (slot, g.flt_frames);
                        g.flt_bank_id = 3000u + static_cast<uint32_t> (slot);
                    }
                }
            }

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
        applyRemaps();
        applyOscMapping();
        applyMpe();
        applyVoiceControl();

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

        if (noiseSampleParam >= 0 && noiseSample_ != nullptr)
        {
            // noise.type = Sample plays the content sample (osc 4); without one (nothing loaded, file missing) the generator of noise.sample plays
            sbvital::Sample::SampleData* user = nullptr;
            if (noiseTypeParam >= 0 && params[static_cast<size_t> (noiseTypeParam)].load (std::memory_order_relaxed) > 0.5f && activeSnapshot != nullptr)
                user = activeSnapshot->noiseSample.get();
            const int sampleChoice = static_cast<int> (params[static_cast<size_t> (noiseSampleParam)].load (std::memory_order_relaxed) + 0.5f);
            if (user != appliedNoiseUser)
            {
                appliedNoiseUser = user;
                appliedNoiseSample = sampleChoice;
                noiseSample_->setNoiseType (sampleChoice);
                noiseSample_->useUserData (user);
            }
            else if (sampleChoice != appliedNoiseSample)
            {
                appliedNoiseSample = sampleChoice;
                noiseSample_->setNoiseType (sampleChoice);   // (a user sample keeps playing: the choice is the generator behind it)
                if (user != nullptr)
                    noiseSample_->useUserData (user);
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

    /** Voice control (GROUPA2, WP-07): the sequence, the random amounts, the scaling and the targets go to the voice handler, which picks the step of
        every note by its note counter. Nothing is cached: the 62 values are read and handed over in every block (a copy of a few hundred bytes). */
    void applyVoiceControl() noexcept
    {
        if (vcLengthParam < 0)
            return;
        const auto get = [this] (int index, float fallback) noexcept
        { return index >= 0 ? params[static_cast<size_t> (index)].load (std::memory_order_relaxed) : fallback; };
        sbvital::VoiceHandler::VoiceControl c;
        c.length = std::clamp (static_cast<int> (get (vcLengthParam, 8.0f) + 0.5f), 1, 8);
        for (size_t n = 0; n < 8; ++n)
        {
            c.pan[n] = get (vcStepParam[n * 6 + 0], 0.0f);
            c.detune[n] = get (vcStepParam[n * 6 + 1], 0.0f);
            c.cutoff[n] = get (vcStepParam[n * 6 + 2], 0.0f);
            c.env[n] = get (vcStepParam[n * 6 + 3], 0.0f);
            c.mod1[n] = get (vcStepParam[n * 6 + 4], 0.0f);
            c.mod2[n] = get (vcStepParam[n * 6 + 5], 0.0f);
        }
        c.rand_pan = get (vcRandParam[0], 0.0f);
        c.rand_detune = get (vcRandParam[1], 0.0f) * (get (vcRandParam[4], 0.0f) > 0.5f ? 10.0f : 1.0f);
        c.rand_cutoff = get (vcRandParam[2], 0.0f);
        c.rand_env = get (vcRandParam[3], 0.0f);
        c.env_scale = std::clamp (get (vcScaleEnvParam, 100.0f), 25.0f, 400.0f) * 0.01f;
        for (size_t t = 0; t < 5; ++t)
            c.target[t] = get (vcTargetParam[t], 1.0f) > 0.5f;
        engine->setVoiceControl (c);
        // the LFO scaling: a time (Percent: 200 % = half the rate) or a rate (Rate: 200 % = twice the rate), A374
        const float lfo = std::clamp (get (vcScaleLfoParam, 100.0f), 25.0f, 400.0f);
        engine->setLfoRateScale (get (vcScaleLfoModeParam, 0.0f) > 0.5f ? lfo * 0.01f : 100.0f / lfo);
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
                engine->setPitchWheel (rawBend[static_cast<size_t> (c)] * masterBendRange (rawBend[static_cast<size_t> (c)]) / kVitalLocalBendRange, c);
    }

    bool mpeOn() const noexcept { return mpeOnParam >= 0 && params[static_cast<size_t> (mpeOnParam)].load (std::memory_order_relaxed) > 0.5f; }

    float masterBendRange (float bend = 1.0f) const noexcept
    {
        // master.bend_down_link On (the default): the wheel has one range, both ways, as in version 2
        if (bend < 0.0f && bendDownRangeParam >= 0 && bendDownLinkParam >= 0
            && params[static_cast<size_t> (bendDownLinkParam)].load (std::memory_order_relaxed) < 0.5f)
            return params[static_cast<size_t> (bendDownRangeParam)].load (std::memory_order_relaxed);
        return bendRangeParam >= 0 ? params[static_cast<size_t> (bendRangeParam)].load (std::memory_order_relaxed) : 2.0f;
    }

    /** MPE: the manager channel's wheel (master.bend_range) moves every note, a member channel's wheel (master.mpe.bend_range) its own notes. */
    void applyMpeBends() noexcept
    {
        const float memberRange = mpeRangeParam >= 0 ? params[static_cast<size_t> (mpeRangeParam)].load (std::memory_order_relaxed) : 48.0f;
        const float managerBend = rawBend[0] * masterBendRange (rawBend[0]);
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
                slot.processor->unplug (sourceFor (k, slot.auxPlugged));
            slot.auxPlugged = 0;
            if (aux > 0)
            {
                // like the main source: a source that depends on the destination would close a loop (the engine would allocate a
                // feedback node), such an aux source is left unplugged
                const sbvital::Output* out = sourceFor (k, aux);
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
        const bool hasModCurve = activeSnapshot != nullptr && activeSnapshot->modCurves[static_cast<size_t> (k)].present;
        const int kind = hasModCurve ? 3 : ((source == kSourceVelocity) ? 1 : (source == kSourceNote ? 2 : 0));
        const bool have = hasModCurve || (activeSnapshot != nullptr && ((kind == 1 && activeSnapshot->hasVelocity) || (kind == 2 && activeSnapshot->hasNote)));
        const int want = have ? kind : 0;
        if (want != slot.curveKind || (want != 0 && slot.curveId != snapshotId))
        {
            LineGenerator& line = *slot.processor->lineMapGenerator();
            if (want == 0)
                line.initLinear();
            else if (want == 3)
            {
                const auto& pts = activeSnapshot->modCurves[static_cast<size_t> (k)].points;
                const int n = static_cast<int> (std::min<size_t> (pts.size(), LineGenerator::kMaxPoints));
                line.setSmooth (false);
                line.setNumPoints (n);
                for (int i = 0; i < n; ++i)
                {
                    line.setPoint (i, { pts[static_cast<size_t> (i)].x, 1.0f - pts[static_cast<size_t> (i)].y });
                    line.setPower (i, pts[static_cast<size_t> (i)].power);
                }
                line.render();
            }
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
        // RISE / FALL / DELAY (REMAINDER item 1): the times are bound controls (modNN.rise / fall / delay / delay_beats); the delay unit is the adapter's
        slot.processor->setDelaySync (slot.delaySyncParam >= 0 && params[static_cast<size_t> (slot.delaySyncParam)].load (std::memory_order_relaxed) > 0.5f);
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

    /** The output a slot reads for a source choice: its own number for Note-On Rand (Discrete), the shared output of the source for the others. */
    sbvital::Output* sourceFor (int k, int choice) const noexcept
    {
        if (choice == kSourceRandDiscrete && discreteOutput[static_cast<size_t> (k)] != nullptr)
            return discreteOutput[static_cast<size_t> (k)];
        return sourceOutput[static_cast<size_t> (choice - 1)];
    }

    sbvital::modulation_change changeFor (int k, bool disconnecting) const noexcept
    {
        const Slot& slot = slots[static_cast<size_t> (k)];
        const Destination& d = destinations[static_cast<size_t> (slot.destination)];
        sbvital::modulation_change change {};
        change.source = sourceFor (k, slot.source);
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

    int slotOfAmountChoice (int destinationChoice) const noexcept
    {
        if (destinationChoice <= 0)
            return -1;
        const int destParam = modDestinationParamIndex (static_cast<size_t> (destinationChoice));
        if (destParam < 0)
            return -1;
        for (int s = 0; s < kNumSlots; ++s)
            if (slots[static_cast<size_t> (s)].amountParam == destParam)
                return s;
        return -1;
    }

    bool wouldCloseSlotLoop (int k, int destinationChoice) const noexcept
    {
        int target = slotOfAmountChoice (destinationChoice);
        if (target < 0)
            return false;
        if (target == k)
            return true; // direct self-modulation
        // Follow chain of slot modulations
        int visited = 0;
        while (target >= 0 && visited < kNumSlots)
        {
            if (target == k)
                return true; // cycle
            if (! slots[static_cast<size_t> (target)].connected)
                break;
            const int nextDestChoice = slots[static_cast<size_t> (target)].destination;
            target = slotOfAmountChoice (nextDestChoice);
            ++visited;
        }
        return false;
    }

    /** True if wiring the slot would make the engine insert a feedback node (a loop in the graph: the source depends on the
        destination), which allocates; such a slot stays unwired (docs/PARAMETERS.md). The decision is the engine's own: the
        check that ProcessorRouter::connect makes for the connection, made first. */
    bool wouldCloseLoop (int k, const sbvital::modulation_change& change) const noexcept
    {
        const Slot& slot = slots[static_cast<size_t> (k)];
        if (wouldCloseSlotLoop (k, slot.destination))
            return true;
        change.modulation_processor->plug (change.source, sbvital::ModulationConnectionProcessor::kModulationInput); // as connectModulation does
        const bool polyphonic = change.source->owner->isPolyphonic() && change.poly_destination != nullptr;
        const sbvital::Processor* destination = (polyphonic && change.poly_destination != nullptr) ? change.poly_destination : (change.mono_destination != nullptr ? change.mono_destination : change.poly_destination);
        if (destination == nullptr)
            return true;
        const sbvital::ProcessorRouter* router = destination->router();
        return router != nullptr && router->isDownstream (destination, change.modulation_processor);
    }

    void connectSlot (int k) noexcept
    {
        Slot& slot = slots[static_cast<size_t> (k)];
        const sbvital::modulation_change change = changeFor (k, false);
        if (wouldCloseLoop (k, change))
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

                const int sameNoteMode = sameNoteParam >= 0 ? static_cast<int> (params[static_cast<size_t> (sameNoteParam)].load (std::memory_order_relaxed)) : 0;
                const bool alreadyPlaying = activeKey[static_cast<size_t> (channel)][static_cast<size_t> (e.key)] >= 0;
                if (sameNoteMode == 2 && alreadyPlaying)
                    return; // Ignore mode: do not retrigger if key is already active

                if (! keysHeld())
                    ++noteGroup->group; // the first note of a new group restarts the mono LFOs
                activeKey[static_cast<size_t> (channel)][static_cast<size_t> (e.key)] = static_cast<int8_t> (key);
                noteGroup->held = true;

                if (sameNoteMode == 0) // Steal
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
                {
                    applyMpeBends(); // master.mpe.on: the member channel's bend range, the manager channel moves every note
                    // Serum: "MPE Pitch Bend (also) maps to Expr X": the member channel's wheel is the X axis as well
                    if (channel > 0 && mpeBendToXParam >= 0 && params[static_cast<size_t> (mpeBendToXParam)].load (std::memory_order_relaxed) > 0.5f)
                        mpeAxis (0, 0.5f + 0.5f * bend, channel);
                }
                else
                    engine->setPitchWheel (bend * masterBendRange (bend) / kVitalLocalBendRange, channel);
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
                {
                    engine->setNotePressure (-1, channel, pressure); // an MPE member channel carries one note: its pressure is that note's
                    mpeAxis (2, pressure, channel);                   // and the Z axis
                }
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
            case Event::Brightness: engine->setNoteSlide (key, channel, clampf (e.value, 0.0f, 1.0f)); mpeAxis (1, clampf (e.value, 0.0f, 1.0f), channel); break;
            case Event::Pan:        mpeAxis (0, clampf (e.value, 0.0f, 1.0f), channel); break;
        }
        if (e.expression == Event::Pressure)
            mpeAxis (2, clampf (e.value, 0.0f, 1.0f), channel);
    }

    /** GROUPA1 (WP-09, 13-new-keys.md 4.28): the MPE axes X (0), Y (1), Z (2), each 0..1, drive the macros (master.mpe.xyz_macro: XYZ -> macros 1, 2, 3, or
        YZ -> macros 1, 2) and Y the mod wheel (master.mpe.y_modwheel). They act on the note expressions of the host as well, with MPE off (manual p.325). The
        macros are global: the last axis value that arrived wins. y_bipolar: Y is centred on 0.5 and the macro / mod wheel get the size of the deflection. */
    void mpeAxis (int axis, float value, int channel) noexcept
    {
        const int map = mpeXyzMacroParam >= 0 ? static_cast<int> (params[static_cast<size_t> (mpeXyzMacroParam)].load (std::memory_order_relaxed) + 0.5f) : 0;
        const bool yToWheel = mpeYModWheelParam >= 0 && params[static_cast<size_t> (mpeYModWheelParam)].load (std::memory_order_relaxed) > 0.5f;
        if (map == 0 && ! yToWheel)
            return;
        float v = value;
        if (axis == 1 && mpeYBipolarParam >= 0 && params[static_cast<size_t> (mpeYBipolarParam)].load (std::memory_order_relaxed) > 0.5f)
            v = std::fabs (2.0f * value - 1.0f);
        if (map == 1 || (map == 2 && axis >= 1))
        {
            const int macro = map == 1 ? axis : axis - 1;   // XYZ: 0, 1, 2; YZ: Y -> 0, Z -> 1
            if (macro >= 0 && macro < 3)
                setMacroFromMpe (macro, v);
        }
        if (axis == 1 && yToWheel)
        {
            lastModWheel = v;
            engine->setModWheel (v, channel);
        }
    }

    /** A macro takes the value of an MPE axis (the control, not the knob); applyMpeMacroMap () gives it back when the mapping is switched off. */
    void setMacroFromMpe (int macro, float value) noexcept
    {
        const size_t index = macroResolved[static_cast<size_t> (macro)];
        if (index == SIZE_MAX)
            return;
        const Resolved& r = resolved[index];
        if (r.control != nullptr)
            r.control->set (clampf (value, 0.0f, 1.0f));
    }

    /** The mapping was switched off or changed: the macros 1 .. 3 are written from their knobs again. */
    void applyMpeMacroMap() noexcept
    {
        const int map = mpeXyzMacroParam >= 0 ? static_cast<int> (params[static_cast<size_t> (mpeXyzMacroParam)].load (std::memory_order_relaxed) + 0.5f) : 0;
        if (map == appliedMpeMacroMap)
            return;
        appliedMpeMacroMap = map;
        for (int m = 0; m < 3; ++m)
        {
            const size_t index = macroResolved[static_cast<size_t> (m)];
            if (index != SIZE_MAX)
                applied[resolved[index].binding->param] = std::numeric_limits<float>::quiet_NaN();
        }
    }

    /** Vital's output is interleaved poly_float: lanes 0 and 1 are left and right. */
    void writeOutput (float* left, float* right, int n) const noexcept
    {
        const auto* out = reinterpret_cast<const sbvital::mono_float*> (engine->output (0)->buffer);
        constexpr int stride = static_cast<int> (sbvital::poly_float::kSize);
        // master.stereo_spread (with its modulation): mid / side width of the finished mix, 1 = the version 2 sound
        const float spread = clampf (engine->masterStereoSpread()->buffer[0][0], 0.0f, 1.0f);

        if (right != nullptr)
        {
            if (spread >= 0.9999f)
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
                {
                    const float l = out[stride * i];
                    const float r = out[stride * i + 1];
                    const float mid = 0.5f * (l + r);
                    const float side = 0.5f * (l - r);
                    left[i] = mid + spread * side;
                    right[i] = mid - spread * side;
                }
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
    std::array<std::array<int, 4>, kNumOsc> remapParam { { { { -1, -1, -1, -1 } }, { { -1, -1, -1, -1 } }, { { -1, -1, -1, -1 } } } }; // on, slot, rise, fall
    std::array<uint64_t, kNumOsc> appliedRemapKey { { ~0ull, ~0ull, ~0ull } };
    std::array<int, kNumOsc> userFramesNow {};                 // frames of the user table an oscillator plays (0: a built-in table)
    std::array<std::array<int, 2>, kNumOsc> frameParams { { { { -1, -1 } }, { { -1, -1 } }, { { -1, -1 } } } };
    enum { kSpType, kSpStart, kSpEnd, kSpRoot, kSpKeytrack, kSpLoopMode, kSpLoopStart, kSpLoopEnd, kSpLoopXfade, kSpLoopRelative, kSpLoopExit,
           kSpFadeEdges, kSpNormalize, kSpReverse, kSpSliceMode, kSpSliceThreshold, kSpSlicePlay, kSpSliceRoot, kSpScanRange, kSpScanReverse,
           kSpScanLock, kSpScanToBpm, kSpLengthBeats,
           // GROUPC WP-11: the static keys of the granular oscillator
           kSpGranWindowShape, kSpGranDensityMode, kSpGranDensitySync, kSpGranDensityGrains, kSpGranLengthMode, kSpGranLengthSync, kSpGranJumpStart,
           kSpGranMaxGrains, kSpGranLoopGrains, kSpGranManual, kSpGranScanKeytrack, kSpGranReverseGrains, kSpGranSpawnPattern, kSpGranYAxis,
           // GROUPC WP-12: the static keys of the spectral oscillator
           kSpSpecLohiSmooth, kSpSpecLohiPostWarp, kSpSpecManual, kSpSpecScanKeytrack, kSpSpecPhaseLock, kSpSpecTransients, kSpSpecYAxis,
           kSpSpecFltUse, kSpSpecFltTable,   // REMAINDER item 2: a wavetable as the spectral filter
           // GROUPC WP-13: the static keys of the multisample oscillator
           kSpMultiOverride, kSpMultiDelay, kSpMultiAttack, kSpMultiHold, kSpMultiDecay, kSpMultiSustain, kSpMultiRelease, kSpMultiVelTrack, kSpMultiVelOverride, kSpCount };
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
    int vcLengthParam = -1, vcScaleEnvParam = -1, vcScaleLfoParam = -1, vcScaleLfoModeParam = -1; // block B26: the voice control
    std::array<int, 48> vcStepParam {};
    std::array<int, 5> vcRandParam {}, vcTargetParam {};
    int mpeXyzMacroParam = -1, mpeYModWheelParam = -1, mpeBendToXParam = -1, mpeYBipolarParam = -1;
    std::array<size_t, 8> macroResolved { SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX }; // index into `resolved` of macro 1 .. 8
    int appliedMpeMacroMap = 0;       // master.mpe.xyz_macro as of the last block: a change gives the macros back to their knobs
    bool appliedReverbForce = false;
    std::array<sbvital::Value*, kNumLfo> vLfoSyncType {}, vLfoHostLock {};
    std::array<sbvital::Value*, kNumOsc> vOscMapWarp {};
    std::array<int, kNumLfo> lfoClock {};
    std::array<int, kNumFxSlots> fxOrderParam { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
    int routingParam = -1, bendRangeParam = -1;
    int noiseSampleParam = -1;
    int noiseTypeParam = -1;
    int appliedNoiseSample = 0;
    sbvital::Sample::SampleData* appliedNoiseUser = nullptr;
    sbvital::Sample* noiseSample_ = nullptr;
    int sameNoteParam = -1, bendDownRangeParam = -1, bendDownLinkParam = -1;
    int oversample4xParam = -1, noSmoothingParam = -1;
    bool preparedOversample4x = false;
    std::atomic<bool> ultraRender { false };
    // GROUPA2 (WP-17): the live read-out for the display (written at the end of every block, read by copyReadout ())
    struct DestReadout
    {
        const Binding* binding = nullptr;
    };
    std::vector<DestReadout> destReadout;
    std::array<const sbvital::StatusOutput*, kNumSlots> amountStatus {};
    std::array<const sbvital::StatusOutput*, 64> sourceStatus {};
    std::array<sbvital::Value*, 8> macroValue {};
    std::array<const sbvital::StatusOutput*, 10> lfoPhaseStatus {};
    std::array<const sbvital::StatusOutput*, 6> envPhaseStatus {};
    std::array<std::atomic<int>, LiveValues::kMaxTargets> readoutTarget {};
    std::atomic<int> readoutCount { 0 }, readoutShown { 0 }, readoutVoices { 0 };
    std::array<std::atomic<float>, LiveValues::kMaxTargets> readoutValue {};
    std::array<std::atomic<float>, LiveValues::kMaxSources> readoutSource {};
    std::array<std::atomic<float>, LiveValues::kNumLfos> readoutLfoPhase {};
    std::array<std::atomic<int>, kNumOsc> readoutMultiZone { { -1, -1, -1 } }, readoutMultiKey { { -1, -1, -1 } }; // GROUPC WP-13: the last zone / key of a multisample oscillator
    std::array<std::atomic<float>, LiveValues::kNumEnvs> readoutEnvStage {}, readoutEnvPosition {};
    std::atomic<uint32_t> readoutBlock { 0 };
    sbvital::Value* vFilter1Input = nullptr;
    sbvital::Value* vFilter2Input = nullptr;
    sbvital::Value* vFxOrder = nullptr;

    std::unique_ptr<vitaltables::WavetableSet> tables; // before the engine: the engine's wavetables point into it
    std::unique_ptr<sbvital::SoundEngine> engine;
    std::unique_ptr<FxPool> pool;                 // after the engine: it dies first
    std::array<PoolRt, kNumFxInstances> poolRt {};
    std::array<std::array<int, kFxInstanceParams>, kNumFxInstances> poolChoice {}; // modulation destination choice of each pool parameter
    std::unique_ptr<Tuning> tuning;
    std::map<std::string, sbvital::Value*> controlMap;
    std::vector<Resolved> resolved;
    std::vector<Destination> destinations;
    std::array<sbvital::Output*, kNumModSources> sourceOutput {};
    std::array<sbvital::Output*, 64> discreteOutput {};
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
    static constexpr int kSourceRandDiscrete = 54;               // GROUPB WP-06: Note-On Rand (Discrete)
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

bool vitalHasPoolModule (const IEngine& engine, int slot) noexcept
{
    const auto* vital = dynamic_cast<const VitalEngine*> (&engine);
    return vital != nullptr && vital->hasPoolModule (slot);
}

int vitalPoolBuilds (const IEngine& engine) noexcept
{
    const auto* vital = dynamic_cast<const VitalEngine*> (&engine);
    return vital != nullptr ? vital->poolBuilds() : 0;
}

bool vitalPoolDestinationValid (const IEngine& engine, int slot, int number) noexcept
{
    const auto* vital = dynamic_cast<const VitalEngine*> (&engine);
    return vital != nullptr && vital->poolDestinationValid (slot, number);
}
} // namespace synthyboy
