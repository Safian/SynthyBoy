// SynthyBoy - engine-vital adapter: IEngine over the Vital-derived DSP (sbvital::SoundEngine).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// prepare() builds a complete engine (the only call that allocates), maps the three SynthyBoy parameters onto Vital's
// controls and sets up a fixed "init patch" (see README.md). process() splits the host block into chunks of at most
// kMaxBufferSize frames and at every event frame, applies the events between the chunks and renders into left/right.

#include "vital_engine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>

#if defined (__SSE2__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
 #include <xmmintrin.h>
#endif

#include "sound_engine.h"
#include "synth_constants.h"
#include "synth_parameters.h"
#include "tuning.h"
#include "wave_frame.h"
#include "wave_source.h"
#include "wavetable_creator.h"
#include "wavetable_group.h"

namespace synthyboy
{
static_assert (std::atomic<float>::is_always_lock_free, "parameter atomics must be lock free (audio thread)");

namespace
{
constexpr int kChunk = sbvital::kMaxBufferSize;      // largest block Vital's engine accepts
constexpr float kBendSemitones = 2.0f;               // full pitch bend range of the Event contract
constexpr float kVitalLocalBendRange = 48.0f;        // VoiceHandler::kLocalPitchBendRange: setPitchWheel (v, ch) scales v by this
constexpr float kMinGainDb = -60.0f;                 // gain <= this is silence (as in the own engine)
constexpr float kVitalVolumeFloorDb = -80.0f;        // SmoothVolume::kMinDb: offset of the "volume" square-root scale
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

float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** gain in dB -> Vital's "volume" control value. Vital shows dB = sqrt (value) - 80 (ValueDetails kSquareRoot,
    post_offset -80), so value = (dB + 80)^2; at or below -60 dB the output is muted (-80 dB), as in the own engine. */
float volumeControlFromDb (float db) noexcept
{
    const float shown = db <= kMinGainDb ? kVitalVolumeFloorDb : clampf (db, kVitalVolumeFloorDb, 6.0f);
    const float root = shown - kVitalVolumeFloorDb;
    return root * root;
}

/** Hz -> Vital's filter cutoff control, a MIDI note number (frequency = kMidi0Frequency * 2^(note / 12)). */
float cutoffControlFromHz (float hz, double sampleRate) noexcept
{
    const double limited = std::clamp (static_cast<double> (hz), 20.0, std::max (20.0, 0.45 * sampleRate));
    const double note = 12.0 * std::log2 (limited / static_cast<double> (sbvital::kMidi0Frequency));
    return clampf (static_cast<float> (note), 8.0f, 136.0f);
}

class VitalEngine final : public IEngine
{
public:
    VitalEngine()
    {
        const auto* table = paramTable();
        for (size_t i = 0; i < kMaxParams; ++i)
            params[i].store (i < paramCount() ? table[i].def : 0.0f, std::memory_order_relaxed);
    }

    void prepare (double newSampleRate, int /*maxBlockFrames*/, const Quality& quality) override
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        maxVoices = std::clamp (quality.maxVoices, 1, kMaxVoices);
        build();
        reset();
    }

    void reset() override
    {
        if (engine == nullptr)
            return;

        ScopedFlushDenormals denormals;
        engine->allSoundsOff();
        for (int channel = 0; channel < numChannels; ++channel)
            engine->setPitchWheel (0.0f, channel); // bends survive allSoundsOff() inside the voice handler
        applyParams (true);
        // let every smoother in the engine settle on the (possibly changed) targets while nothing sounds
        for (int i = 0; i < kSettleChunks; ++i)
            engine->process (kChunk);
        engine->allSoundsOff();
        activeCount.store (0, std::memory_order_relaxed);
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
        return readParam (id);
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

            engine->process (end - pos);
            writeOutput (left + pos, right != nullptr ? right + pos : nullptr, end - pos);
            pos = end;
        }

        // events at or beyond the end of the block (out of range input) take effect at the start of the next block
        while (next < numEvents)
            handleEvent (events[next++]);

        activeCount.store (engine->getNumActiveVoices(), std::memory_order_relaxed);
    }

    bool supportsParam (uint32_t id) const noexcept override
    {
        return id == kParamMasterVolume || id == kParamFilter1Cutoff || id == kParamOsc1Wavetable;
    }

    void setQuality (const Quality&) noexcept override {}

    int activeVoices() const noexcept override
    {
        return activeCount.load (std::memory_order_relaxed);
    }

    bool configured() const noexcept { return engine != nullptr && controlsComplete; }

private:
    static constexpr int numChannels = 16;

    struct Controls
    {
        sbvital::Value* volume = nullptr;
        sbvital::Value* cutoff = nullptr;
        sbvital::Value* waveFrame = nullptr;
        sbvital::Value* polyphony = nullptr;
    };

    float readParam (uint32_t id) const noexcept
    {
        const int i = findParamIndex (id);
        return i >= 0 ? params[static_cast<size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }

    sbvital::Value* control (const char* name)
    {
        const auto it = controlMap.find (name);
        if (it == controlMap.end() || it->second == nullptr)
        {
            controlsComplete = false;
            return nullptr;
        }
        return it->second;
    }

    void setControl (const char* name, float value)
    {
        if (auto* c = control (name))
            c->set (value);
    }

    /** The wavetable of oscillator 1: frame 0 = sine, frame 1 = saw (SynthyBoy's "waveform" choice), built with the
        wavetable model (WaveSource component, Vital's predefined shapes, as WavetableCreator::initPredefinedWaves does for
        six shapes). No file, no preset. */
    class WaveformTableCreator : public WavetableCreator
    {
    public:
        explicit WaveformTableCreator (sbvital::Wavetable* wavetable) : WavetableCreator (wavetable) {}

        void build()
        {
            clear();
            setName ("SynthyBoy Sine-Saw");

            auto* source = new WaveSource();
            source->insertNewKeyframe (0);
            source->insertNewKeyframe (1);
            source->getKeyframe (0)->wave_frame()->copy (sbvital::PredefinedWaveFrames::getWaveFrame (sbvital::PredefinedWaveFrames::kSin));
            source->getKeyframe (1)->wave_frame()->copy (sbvital::PredefinedWaveFrames::getWaveFrame (sbvital::PredefinedWaveFrames::kSaw));
            source->setInterpolationStyle (WaveSource::kNone);

            auto* group = new WavetableGroup();
            group->addComponent (source);
            addGroup (group);
            full_normalize_ = false; // both shapes are already unit amplitude and DC free
            remove_all_dc_ = false;
            render();
        }
    };

    void build()
    {
        controlsComplete = true;
        controls = Controls {};
        engine.reset();
        tuning.reset();

        engine = std::make_unique<sbvital::SoundEngine>();
        tuning = std::make_unique<Tuning>(); // default 12-tone equal temperament
        engine->setTuning (tuning.get());

        for (int i = 0; i < sbvital::kNumOscillators; ++i)
        {
            if (sbvital::Wavetable* wavetable = engine->getWavetable (i))
            {
                if (i == 0)
                {
                    WaveformTableCreator creator (wavetable);
                    creator.build();
                }
                else
                {
                    WavetableCreator creator (wavetable);
                    creator.init();
                }
            }
        }

        engine->setSampleRate (static_cast<int> (std::lround (sampleRate)));
        engine->setBpm (120.0f);
        engine->checkOversampling(); // oversampling stays at Vital's default (2x)
        engine->updateAllModulationSwitches();

        controlMap = engine->getControls();
        controls.volume = control ("volume");
        controls.cutoff = control ("filter_1_cutoff");
        controls.waveFrame = control ("osc_1_wave_frame");
        controls.polyphony = control ("polyphony");

        applyInitPatch();
        applyParams (true);

        // Warm-up: the first notes an engine plays differ slightly from all later ones (measured: the very first note
        // has a different attack, one more note makes the engine behave identically from then on), and lazily built
        // tables inside the DSP (FFT plans, lookups, ...) must not be built on the audio thread. Two short notes are
        // rendered here; reset() then silences everything again.
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            engine->noteOn (60, 1.0f, 0, 0);
            for (int i = 0; i < 8; ++i)
                engine->process (kChunk);
            engine->noteOff (60, 0.0f, 0, 0);
            for (int i = 0; i < 8; ++i)
                engine->process (kChunk);
            engine->allSoundsOff();
            engine->process (kChunk);
        }
    }

    /** Everything the three SynthyBoy parameters do not touch: one oscillator through one low-pass filter into the
        master volume, amplitude envelope 1 at Vital's defaults, no effects, no modulation. */
    void applyInitPatch()
    {
        // sound path
        setControl ("osc_1_on", 1.0f);
        setControl ("osc_1_level", 0.70710678f); // Vital's default: amplitude 0.5 (the control is squared); keeps the filter below its saturation
        setControl ("osc_1_transpose", 0.0f);
        setControl ("osc_1_tune", 0.0f);
        setControl ("osc_1_pan", 0.0f);
        setControl ("osc_1_unison_voices", 1.0f);
        setControl ("osc_1_random_phase", 0.0f); // deterministic start phase
        setControl ("osc_1_destination", 0.0f);  // 0 = filter 1 input chain (the default)
        setControl ("osc_1_distortion_type", 0.0f);
        setControl ("osc_1_spectral_morph_type", 0.0f);
        setControl ("osc_2_on", 0.0f);
        setControl ("osc_3_on", 0.0f);
        setControl ("sample_on", 0.0f);

        setControl ("filter_1_on", 1.0f);
        setControl ("filter_1_model", 3.0f);     // Digital (state variable): linear, no saturation, so a sine stays a sine
        setControl ("filter_1_style", 0.0f);
        setControl ("filter_1_blend", 0.0f);     // low-pass
        setControl ("filter_1_mix", 1.0f);
        setControl ("filter_1_drive", 0.0f);
        setControl ("filter_1_resonance", 0.2f);
        setControl ("filter_1_keytrack", 0.0f);
        setControl ("filter_1_filter_input", 0.0f);
        setControl ("filter_2_on", 0.0f);
        setControl ("filter_fx_on", 0.0f);

        // effects off
        for (const char* fx : { "chorus_on", "compressor_on", "delay_on", "distortion_on", "eq_on", "flanger_on", "phaser_on", "reverb_on" })
            setControl (fx, 0.0f);

        // voices
        setControl ("velocity_track", 1.0f);     // velocity scales the level, as in the own engine
        setControl ("legato", 0.0f);
        setControl ("pitch_bend_range", kBendSemitones);
        setControl ("pitch_wheel", 0.0f);
        setControl ("mod_wheel", 0.0f);
        setControl ("voice_tune", 0.0f);
        setControl ("voice_transpose", 0.0f);
        setControl ("voice_amplitude", 1.0f);
        setControl ("portamento_time", -10.0f);  // Vital's "off" value
        setControl ("volume", volumeControlFromDb (kMinGainDb)); // replaced by applyParams
    }

    void applyParams (bool force) noexcept
    {
        const float gain = readParam (kParamMasterVolume);
        const float cutoff = readParam (kParamFilter1Cutoff);
        const float waveform = readParam (kParamOsc1Wavetable);

        if (force || gain != applied.gain)
        {
            if (controls.volume != nullptr)
                controls.volume->set (volumeControlFromDb (gain));
            applied.gain = gain;
        }
        if (force || cutoff != applied.cutoff)
        {
            if (controls.cutoff != nullptr)
                controls.cutoff->set (cutoffControlFromHz (cutoff, sampleRate));
            applied.cutoff = cutoff;
        }
        if (force || waveform != applied.waveform)
        {
            if (controls.waveFrame != nullptr)
                controls.waveFrame->set (waveform < 0.5f ? 0.0f : 1.0f);
            applied.waveform = waveform;
        }
        if (force || maxVoices != applied.voices)
        {
            if (controls.polyphony != nullptr)
                controls.polyphony->set (static_cast<float> (maxVoices));
            applied.voices = maxVoices;
        }
    }

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
                // hitting a key that still rings lets the old note fade out first (as the own engine does)
                engine->noteOff (e.key, 0.0f, 0, channel);
                engine->noteOn (e.key, velocity, 0, channel);
                break;
            }
            case Event::NoteOff:
                if (e.key >= 0 && e.key <= 127)
                    engine->noteOff (e.key, 0.0f, 0, channel);
                break;
            case Event::PitchBend:
            {
                const float bend = std::isnan (e.value) ? 0.0f : clampf (e.value, -1.0f, 1.0f);
                engine->setPitchWheel (bend * kBendSemitones / kVitalLocalBendRange, channel);
                break;
            }
            case Event::ModWheel:
            case Event::Aftertouch:
                break;
            case Event::AllNotesOff:
                if (e.channel < 0)
                    engine->allNotesOff (0);
                else
                    engine->allNotesOff (0, channel);
                break;
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

    struct Applied
    {
        float gain = std::numeric_limits<float>::quiet_NaN();
        float cutoff = std::numeric_limits<float>::quiet_NaN();
        float waveform = std::numeric_limits<float>::quiet_NaN();
        int voices = -1;
    };

    std::array<std::atomic<float>, kMaxParams> params;
    std::atomic<int> activeCount { 0 };

    std::unique_ptr<sbvital::SoundEngine> engine;
    std::unique_ptr<Tuning> tuning;
    std::map<std::string, sbvital::Value*> controlMap;
    Controls controls;
    Applied applied;
    double sampleRate = 44100.0;
    int maxVoices = 8;
    bool controlsComplete = false;
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
} // namespace synthyboy
