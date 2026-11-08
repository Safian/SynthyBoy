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
#include <set>
#include <string>

#if defined (__SSE2__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
 #include <xmmintrin.h>
#endif

#include "modulation_connection_processor.h"
#include "sound_engine.h"
#include "synth_oscillator.h"
#include "synth_constants.h"
#include "synth_parameters.h"
#include "tuning.h"
#include "utils.h"
#include "value_switch.h"
#include "vital_bindings.h"
#include "vital_tables.h"
#include "synthyboy/modulation.h"

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
constexpr int kNumLfo = 8;

/** Names of the engine's modulation sources, in the order of core/src/modulation.cpp (modSourceTable). */
std::string vitalSourceName (int index)
{
    if (index < 6)  return "env_" + std::to_string (index + 1);
    if (index < 14) return "lfo_" + std::to_string (index - 5);
    if (index < 18) return "random_" + std::to_string (index - 13);
    if (index < 22) return "macro_control_" + std::to_string (index - 17);
    static const char* const performance[] = { "velocity", "note", "aftertouch", "pitch_wheel", "mod_wheel", "random" };
    return performance[index - 22];
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
            engine->setZonedPitchWheel (0.0f, channel, channel);
            engine->setModWheel (0.0f, channel);
        }
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

    int activeVoices() const noexcept override
    {
        return activeCount.load (std::memory_order_relaxed);
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
    };

    struct Destination
    {
        bool valid = false;
        sbvital::Processor* mono = nullptr;
        sbvital::Processor* poly = nullptr;
        sbvital::ValueSwitch* monoSwitch = nullptr;
        sbvital::ValueSwitch* polySwitch = nullptr;
        float scale = 1.0f;
    };

    struct Slot
    {
        int source = 0;       // choice value applied
        int destination = 0;
        bool connected = false;
        uint32_t triedGeneration = 0;   // matrixGeneration at the last attempt to wire the slot
        sbvital::ModulationConnectionProcessor* processor = nullptr;
        int sourceParam = -1, destinationParam = -1;
    };

    // ---- quality hand-off (any thread -> audio thread) -------------------------------------------------------------
    void setQualityValues (const Quality& q) noexcept
    {
        qVoices.store (std::clamp (q.maxVoices, 1, kMaxVoices), std::memory_order_relaxed);
        qUnison.store (std::clamp (q.maxUnison, 1, kMaxUnisonVoices), std::memory_order_relaxed);
        qOversampling.store (q.oversampling >= 2 ? 2 : 1, std::memory_order_relaxed);
        qInterpolation.store (q.interpolation == Interpolation::spectral ? 1 : 0, std::memory_order_relaxed);
        qFxMask.store (q.fxMask & kFxAll, std::memory_order_relaxed);
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
        appliedShape.fill (-1);
        appliedRouting = appliedFxOrder = -1.0f;
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
            if (sbvital::Wavetable* wavetable = engine->getWavetable (i))
                wavetable->useExternalData (tables->data (kWavetableSaw));

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
        vFilter1Input = control ("filter_1_filter_input");
        vFilter2Input = control ("filter_2_filter_input");
        vFxOrder = control ("effect_chain_order");
        resolveBindings();
        resolveModulation();
        for (int i = 0; i < kNumLfo; ++i)
            lfoLines[static_cast<size_t> (i)] = engine->getLfoSource (i);

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
                if (number (9) <= kNumLegacyFxSlots)
                    fxOrderParam[static_cast<size_t> (number (9) - 1)] = static_cast<int> (i);
            }
            else if (key == "master.bend_range")
                bendRangeParam = static_cast<int> (i);
        }
    }

    /** The tables of the modulation matrix: sources, destinations, the 64 connection processors. Everything the audio thread needs
        to wire a slot is looked up here (the lookups take std::string and walk maps: allocation, not for process()). */
    void resolveModulation()
    {
        for (size_t k = 0; k < static_cast<size_t> (kNumModSourcesV2); ++k) // the sources 29 .. 38 have no output here: slots using them stay unwired
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

        for (const Resolved& r : resolved)
        {
            const Binding& b = *r.binding;
            const float plain = params[b.param].load (std::memory_order_relaxed);
            const bool capped = b.cap != Cap::none;
            if (! force && plain == applied[b.param] && ! (capped && qualityChanged))
                continue;
            applied[b.param] = plain;

            double value = convertParam (b, static_cast<double> (plain), sampleRate);
            switch (b.cap)
            {
                case Cap::none:   break;
                case Cap::voices: value = std::min (value, static_cast<double> (quality.maxVoices)); break;
                case Cap::unison: value = std::min (value, static_cast<double> (quality.maxUnison)); break;
                case Cap::fx:     value = (value > 0.5 && (quality.fxMask & (1u << b.capArg)) != 0) ? 1.0 : 0.0; break;
            }
            r.control->set (static_cast<float> (std::clamp (value, r.lo, r.hi)));
        }

        applySpecials();
        for (int k = 0; k < kNumSlots; ++k)
            updateSlot (k);
    }

    void applySpecials() noexcept
    {
        for (int i = 0; i < kNumOsc; ++i)
        {
            const int p = wavetableParam[static_cast<size_t> (i)];
            if (p < 0)
                continue;
            int index = static_cast<int> (params[static_cast<size_t> (p)].load (std::memory_order_relaxed) + 0.5f);
            if (index >= kNumBuiltinWavetables)
                index = kWavetableSaw; // "Preset table": no content store yet, the documented fallback is the saw
            if (index != appliedWavetable[static_cast<size_t> (i)])
            {
                appliedWavetable[static_cast<size_t> (i)] = index;
                if (auto* wavetable = engine->getWavetable (i))
                    wavetable->useExternalData (tables->data (index));
            }
        }

        for (int i = 0; i < kNumLfo; ++i)
        {
            const int p = shapeParam[static_cast<size_t> (i)];
            if (p < 0 || lfoLines[static_cast<size_t> (i)] == nullptr)
                continue;
            int shape = static_cast<int> (params[static_cast<size_t> (p)].load (std::memory_order_relaxed) + 0.5f);
            if (shape >= kNumLfoShapes)
                shape = 0; // "Custom": no custom point lists yet, the documented fallback is the sine
            if (shape != appliedShape[static_cast<size_t> (i)])
            {
                appliedShape[static_cast<size_t> (i)] = shape;
                vitaltables::applyLfoShape (*lfoLines[static_cast<size_t> (i)], shape);
            }
        }

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

        float choices[kNumLegacyFxSlots];
        bool same = true;
        for (int i = 0; i < kNumLegacyFxSlots; ++i)
        {
            const int p = fxOrderParam[static_cast<size_t> (i)];
            choices[i] = p >= 0 ? params[static_cast<size_t> (p)].load (std::memory_order_relaxed) : static_cast<float> (i);
            same = same && choices[i] == appliedChoices[static_cast<size_t> (i)];
        }
        if (! same || appliedFxOrder < 0.0f)
        {
            for (int i = 0; i < kNumLegacyFxSlots; ++i)
                appliedChoices[static_cast<size_t> (i)] = choices[i];
            int order[kNumLegacyFxSlots];
            resolveFxOrder (choices, order);
            appliedFxOrder = sbvital::utils::encodeOrderToFloat (order, kNumLegacyFxSlots);
            if (vFxOrder != nullptr)
                vFxOrder->set (appliedFxOrder);
        }
    }

    // ---- the modulation matrix --------------------------------------------------------------------------------------
    void updateSlot (int k) noexcept
    {
        Slot& slot = slots[static_cast<size_t> (k)];
        if (slot.sourceParam < 0 || slot.destinationParam < 0)
            return;
        const int source = static_cast<int> (params[static_cast<size_t> (slot.sourceParam)].load (std::memory_order_relaxed) + 0.5f);
        const int destination = static_cast<int> (params[static_cast<size_t> (slot.destinationParam)].load (std::memory_order_relaxed) + 0.5f);
        if (source == slot.source && destination == slot.destination)
        {
            // a slot that was left unwired because it would have closed a loop gets another chance when the matrix has changed
            if (slot.source > 0 && slot.destination > 0 && ! slot.connected && slot.triedGeneration != matrixGeneration
                && static_cast<size_t> (destination) < destinations.size() && destinations[static_cast<size_t> (destination)].valid
                && sourceOutput[static_cast<size_t> (source - 1)] != nullptr)
            {
                slot.triedGeneration = matrixGeneration;
                connectSlot (k);
            }
            return;
        }

        if (slot.connected)
            disconnectSlot (k);
        slot.source = source;
        slot.destination = destination;
        slot.triedGeneration = matrixGeneration;
        if (source > 0 && destination > 0 && static_cast<size_t> (destination) < destinations.size() && destinations[static_cast<size_t> (destination)].valid
            && sourceOutput[static_cast<size_t> (source - 1)] != nullptr)
            connectSlot (k);
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
                // the per-channel wheel of the voices moves the pitch (a value of 1 is kVitalLocalBendRange semitones); the zoned
                // wheel only feeds the "Pitch Wheel" modulation source (its range control is 0, see applyFixedControls)
                const float bend = std::isnan (e.value) ? 0.0f : clampf (e.value, -1.0f, 1.0f);
                const float range = bendRangeParam >= 0 ? params[static_cast<size_t> (bendRangeParam)].load (std::memory_order_relaxed) : 2.0f;
                engine->setPitchWheel (bend * range / kVitalLocalBendRange, channel);
                engine->setZonedPitchWheel (bend, channel, channel);
                break;
            }
            case Event::ModWheel:
                engine->setModWheel (std::isnan (e.value) ? 0.0f : clampf (e.value, 0.0f, 1.0f), channel);
                break;
            case Event::Aftertouch:
                engine->setChannelAftertouch (channel, std::isnan (e.value) ? 0.0f : clampf (e.value, 0.0f, 1.0f), 0);
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

    // ---- state ----------------------------------------------------------------------------------------------------
    std::array<std::atomic<float>, kMaxParams> params;
    std::atomic<int> activeCount { 0 };

    std::atomic<int> qVoices { 8 }, qUnison { 16 }, qOversampling { 2 }, qInterpolation { 1 };
    std::atomic<uint32_t> qFxMask { kFxAll };
    std::atomic<uint32_t> qGeneration { 0 };

    // audio thread (and prepare) only
    Quality quality;
    uint32_t appliedQualityGeneration = ~0u;
    std::array<float, kMaxParams> applied {};
    std::array<int, kNumOsc> appliedWavetable {};
    std::array<int, kNumLfo> appliedShape {};
    std::array<float, kNumLegacyFxSlots> appliedChoices {};
    float appliedRouting = -1.0f, appliedFxOrder = -1.0f;
    std::array<int, kNumOsc> wavetableParam { { -1, -1, -1 } };
    std::array<int, kNumLfo> shapeParam { { -1, -1, -1, -1, -1, -1, -1, -1 } };
    std::array<int, kNumLegacyFxSlots> fxOrderParam { { -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
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
