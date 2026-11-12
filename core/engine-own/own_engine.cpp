// SynthyBoy - own engine: engine glue (events, voice allocation, control grid, master).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "own_engine.h"

#include <algorithm>
#include <cmath>

#include "own_support.h"
#include "osc/wavetable_bank.h"

namespace synthyboy
{
static_assert (std::atomic<float>::is_always_lock_free, "parameter atomics must be lock free (audio thread)");

std::unique_ptr<IEngine> createOwnEngine()
{
    return std::make_unique<own::OwnEngine>();
}

namespace own
{
namespace
{
/** The output safety stage: the identity up to +-1, above it a smooth knee that approaches +-2 (any patch stays bounded, a normal patch never reaches it). */
inline float outputLimit (float y) noexcept
{
    const float a = std::fabs (y);
    if (!(a > 1.0f))
        return y == y ? y : 0.0f;
    return std::copysign (1.0f + std::tanh (a - 1.0f), y);
}
} // namespace

OwnEngine::OwnEngine() : fxReverbForceIndex (findParamIndex (hashKey ("reverb.force_enable"))), fxOversampleIndex (findParamIndex (hashKey ("quality.oversample_4x")))
{
    const auto* table = paramTable();
    for (size_t i = 0; i < kMaxParams; ++i)
        params[i].store (i < paramCount() ? table[i].def : 0.0f, std::memory_order_relaxed);
    for (auto& k : switchKeyAtomic)
        k.store (-1, std::memory_order_relaxed);
    for (int o = 0; o < 3; ++o)
    {
        multiZone[o].store (-1, std::memory_order_relaxed);
        multiKey[o].store (-1, std::memory_order_relaxed);
    }
    spectralScratch = std::make_unique<spectral::Scratch>();
    publishContent (ContentSet::build (StateContent {}, nullptr));
}

OwnEngine::~OwnEngine() = default;

void OwnEngine::publishContent (std::unique_ptr<ContentSet> set)
{
    std::lock_guard<std::mutex> lock (contentMutex);
    const ContentSet* raw = set.get();
    contentOwned.push_back (std::move (set));
    contentCurrent.store (raw); // seq_cst: the audio thread re-validates the pointer it announced in contentInUse
    const ContentSet* inUse = contentInUse.load();
    for (size_t i = 0; i < contentOwned.size();)
    {
        const ContentSet* p = contentOwned[i].get();
        if (p != raw && p != inUse)
            contentOwned.erase (contentOwned.begin() + static_cast<std::ptrdiff_t> (i));
        else
            ++i;
    }
    for (auto& k : switchKeyAtomic)
        k.store (-1, std::memory_order_relaxed);
}

void OwnEngine::rebuildContent()
{
    StateContent copy;
    const IContentStore* st;
    {
        std::lock_guard<std::mutex> lock (contentMutex);
        copy = contentState;
        st = contentStore;
    }
    publishContent (ContentSet::build (copy, st));
}

void OwnEngine::setContent (const StateContent& content)
{
    {
        std::lock_guard<std::mutex> lock (contentMutex);
        contentState = content;
    }
    rebuildContent();
}

void OwnEngine::setContentStore (const IContentStore* store)
{
    {
        std::lock_guard<std::mutex> lock (contentMutex);
        contentStore = store;
    }
    rebuildContent();
}

float OwnEngine::readParam (uint32_t id) const noexcept
{
    const int i = findParamIndex (id);
    return i >= 0 ? params[static_cast<size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
}

void OwnEngine::setParam (uint32_t id, float plain) noexcept
{
    const int i = findParamIndex (id);
    if (i < 0 || std::isnan (plain))
        return;
    params[static_cast<size_t> (i)].store (constrainValue (paramTable()[i], plain), std::memory_order_relaxed);
}

float OwnEngine::getParam (uint32_t id) const noexcept
{
    return readParam (id);
}

bool OwnEngine::supportsParam (uint32_t id) const noexcept
{
    const int i = findParamIndex (id);
    if (i < 0)
        return false;
    return isImplemented (static_cast<size_t> (i)) && capsCover (capabilities(), requiredCaps (static_cast<size_t> (i)))
        && capsCover2 (capabilities2(), requiredCaps2 (static_cast<size_t> (i)));
}

EngineCapsMask OwnEngine::capabilities() const noexcept
{
    return ownCapabilities();
}

EngineCaps2Mask OwnEngine::capabilities2() const noexcept
{
    return ownCapabilities2();
}

void OwnEngine::setQuality (const Quality& q) noexcept
{
    voiceCap.store (std::clamp (q.maxVoices, 1, kMaxVoices), std::memory_order_relaxed);
    unisonCap.store (std::clamp (q.maxUnison, 1, kMaxUnisonVoices), std::memory_order_relaxed);
    qualityTier.store (q.oversampling <= 1 ? 0 : (q.interpolation == Interpolation::spectral ? 2 : 1), std::memory_order_relaxed);
    fxMask.store (q.fxMask, std::memory_order_relaxed);
}

void OwnEngine::setTransport (const TransportInfo& t) noexcept
{
    tpBpm.store (t.bpm > 0.0 ? t.bpm : 120.0, std::memory_order_relaxed);
    tpPos.store (t.songPosBeats, std::memory_order_relaxed);
    tpPlaying.store (t.playing, std::memory_order_relaxed);
}

void OwnEngine::prepare (double newSampleRate, int maxBlockFrames, const Quality& quality)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    setQuality (quality);
    scratchRight.assign (static_cast<size_t> (std::max (maxBlockFrames, 4096)), 0.0f);
    for (int c = 0; c <= kNumWavetableChoices; ++c)
        builtinWavetable (c); // builds the shared bank (allocates): not on the audio thread
    // the lazily built tables of the keys (they allocate on first use): build them here
    (void) paramIndex();
    (void) fxIndex();
    (void) destLawOf (0);
    (void) WtOsc::decimatorTaps();

    voices.clear();
    for (int i = 0; i < kMaxVoices; ++i)
    {
        voices.push_back (std::make_unique<Voice>());
        voices.back()->prepare (sampleRate);
    }
    sustainPending.assign (static_cast<size_t> (kMaxVoices), static_cast<uint8_t> (0));
    base.assign (paramCount(), 0.0f);
    gdelta.assign (paramCount(), 0.0f);
    gtouched.assign (static_cast<size_t> (kNumModSlots), 0);
    fx.prepare (sampleRate, maxBlockFrames);
    smoothedGain.reset (sampleRate, 0.02);
    prepared = true;
    reset();
}

void OwnEngine::silence() noexcept
{
    // the host transport stopped: notes and effect tails end at once, parameters, controllers and content stay
    for (auto& v : voices)
        v->kill();
    fx.reset();
    std::fill (sustainPending.begin(), sustainPending.end(), static_cast<uint8_t> (0));
    heldKeys = 0;
}

void OwnEngine::reset()
{
    for (auto& v : voices)
        v->kill();
    fx.reset();
    for (int c = 0; c < numChannels; ++c)
    {
        bendSemis[c] = modWheel[c] = aftertouch[c] = 0.0f;
        sustainDown[c] = false;
    }
    std::fill (sustainPending.begin(), sustainPending.end(), static_cast<uint8_t> (0));
    noteCounter = 0;
    controlPos = 0;
    heldKeys = 0;
    haveLastNote = false;
    for (auto& p : lfoPhase)
        p = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        rlfo[i].seed (static_cast<uint32_t> (i) * 4093u + 1u);
        rlfoValue[i] = 0.0f;
    }
    for (int i = 0; i < gnumTouched; ++i)
        gdelta[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
    gnumTouched = 0;
    smoothedGain.setCurrentAndTarget (dbToGain (readParam (kParamMasterVolume)) * (readParam (kParamMasterVolume) > -60.0f ? 1.0f : 0.0f));
    activeCount.store (0, std::memory_order_relaxed);
}

Voice* OwnEngine::allocateVoice (int key, int channel, float velocity) noexcept
{
    const MasterIdx& mi = paramIndex().master;
    const int cap = std::clamp (std::min (voiceCap.load (std::memory_order_relaxed), static_cast<int> (base[static_cast<size_t> (mi.polyphony)] + 0.5f)), 1, kMaxVoices);
    for (int i = 0; i < cap; ++i)
        if (!voices[static_cast<size_t> (i)]->isActive())
            return voices[static_cast<size_t> (i)].get();
    // out of voices: steal. Released voices go first (the quietest, then the oldest), then by the priority of the parameter.
    const int priority = static_cast<int> (base[static_cast<size_t> (mi.stealPriority)] + 0.5f);
    Voice* best = nullptr;
    auto worse = [&] (const Voice& a, const Voice& b) // true if a is a better victim than b
    {
        if (a.isReleased() != b.isReleased())
            return a.isReleased();
        if (a.isReleased())
            return a.level() < b.level();
        switch (priority)
        {
            case 1: return a.serial() > b.serial();      // Oldest priority: keep the oldest, steal the newest
            case 2: return a.key() < b.key();            // Highest: steal the lowest
            case 3: return a.key() > b.key();            // Lowest: steal the highest
            case 4: return a.velocity() < b.velocity();  // Velocity: steal the softest
            default: return a.serial() < b.serial();     // Newest: steal the oldest
        }
    };
    for (int i = 0; i < cap; ++i)
    {
        Voice* v = voices[static_cast<size_t> (i)].get();
        if (best == nullptr || worse (*v, *best))
            best = v;
    }
    // a lowered cap leaves voices above it sounding until they end; they do not count
    (void) key;
    (void) channel;
    (void) velocity;
    return best;
}

void OwnEngine::noteOn (const Event& e) noexcept
{
    if (e.key < 0 || e.key > 127)
        return;
    const MasterIdx& mi = paramIndex().master;
    const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
    const float velocity = std::isnan (e.value) ? 0.0f : std::clamp (e.value, 0.0f, 1.0f);
    const bool legato = base[static_cast<size_t> (mi.legato)] >= 0.5f;
    const int sameNote = static_cast<int> (base[static_cast<size_t> (mi.sameNote)] + 0.5f);

    NoteStart ns;
    ns.key = e.key;
    ns.channel = channel;
    ns.noteId = e.noteId;
    ns.velocity = velocity;
    ns.serial = noteCounter++;
    // a key switch of a multisample instrument selects the articulation of the zones that follow
    if (globals.content != nullptr)
        for (int o = 0; o < 3; ++o)
            if (globals.content->instrument[o] != nullptr && globals.content->instrument[o]->isKeySwitchKey (e.key))
            {
                switchKeyAtomic[o].store (e.key, std::memory_order_relaxed);
                switchKeySnap[o] = e.key;
            }
    const bool glideAlways = base[static_cast<size_t> (mi.portAlways)] >= 0.5f;

    // a key that is already held
    for (auto& v : voices)
        if (v->isActive() && !v->isReleased() && v->key() == e.key && v->channel() == channel)
        {
            if (sameNote == 2) // Ignore
                return;
            if (sameNote == 0) // Steal: the old note fades out before the new one starts
                v->release (0.0f);
        }

    if (legato)
    {
        // legato: a held note is taken over without retriggering the envelopes (pitch glides if the portamento time is set)
        Voice* held = nullptr;
        for (auto& v : voices)
            if (v->isActive() && !v->isReleased() && (held == nullptr || v->serial() > held->serial()))
                held = v.get();
        if (held != nullptr)
        {
            ns.glide = true;
            held->legatoTo (ns);
            lastNoteSemis = static_cast<float> (e.key);
            haveLastNote = true;
            ++heldKeys;
            lastVelocity = velocity;
            return;
        }
    }
    ns.glide = haveLastNote && (glideAlways || heldKeys > 0);
    ns.glideFromSemis = lastNoteSemis;
    Voice* chosen = allocateVoice (e.key, channel, velocity);
    if (chosen == nullptr)
        return;
    chosen->start (ns, chosen->lastL(), chosen->lastR());
    chosen->tick (globals, static_cast<int> (std::find_if (voices.begin(), voices.end(), [&] (const auto& p) { return p.get() == chosen; }) - voices.begin()));
    for (int o = 0; o < 3; ++o)
        if (chosen->multisampleZone (o) >= 0)
        {
            multiZone[o].store (chosen->multisampleZone (o), std::memory_order_relaxed);
            multiKey[o].store (chosen->multisampleKey (o), std::memory_order_relaxed);
        }
    lastNoteSemis = static_cast<float> (e.key);
    haveLastNote = true;
    ++heldKeys;
    lastVelocity = velocity;
}

void OwnEngine::noteOff (const Event& e) noexcept
{
    const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
    if (heldKeys > 0)
        --heldKeys;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        Voice& v = *voices[i];
        if (!v.isActive() || v.isReleased() || v.key() != e.key || v.channel() != channel)
            continue;
        if (e.noteId >= 0 && v.noteId() >= 0 && v.noteId() != e.noteId)
            continue;
        if (sustainDown[channel])
            sustainPending[i] = 1;
        else
            v.release (std::isnan (e.value) ? 0.0f : std::clamp (e.value, 0.0f, 1.0f));
        break;
    }
}

void OwnEngine::setSustain (int channel, bool down) noexcept
{
    const int first = channel < 0 ? 0 : channel;
    const int last = channel < 0 ? numChannels - 1 : channel;
    for (int c = first; c <= last; ++c)
        sustainDown[c] = down;
    if (down)
        return;
    for (size_t i = 0; i < voices.size(); ++i)
        if (sustainPending[i] != 0 && (channel < 0 || voices[i]->channel() == channel))
        {
            sustainPending[i] = 0;
            voices[i]->release (0.0f);
        }
}

void OwnEngine::handleEvent (const Event& e) noexcept
{
    const MasterIdx& mi = paramIndex().master;
    switch (e.type)
    {
        case Event::NoteOn:
            noteOn (e);
            break;
        case Event::NoteOff:
            noteOff (e);
            break;
        case Event::PitchBend:
        {
            const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
            const float v = std::isnan (e.value) ? 0.0f : std::clamp (e.value, -1.0f, 1.0f);
            const float up = base[static_cast<size_t> (mi.bendRange)];
            const float down = base[static_cast<size_t> (mi.bendDownLink)] >= 0.5f ? up : base[static_cast<size_t> (mi.bendDownRange)];
            bendSemis[channel] = v * (v >= 0.0f ? up : down);
            break;
        }
        case Event::AllNotesOff:
            for (size_t i = 0; i < voices.size(); ++i)
                if (e.channel < 0 || voices[i]->channel() == e.channel)
                    voices[i]->release (0.0f);
            std::fill (sustainPending.begin(), sustainPending.end(), static_cast<uint8_t> (0));
            heldKeys = 0;
            break;
        case Event::SustainPedal:
            setSustain (e.channel < 0 ? -1 : std::clamp<int> (e.channel, 0, numChannels - 1), e.value >= 0.5f);
            break;
        case Event::NoteExpression:
            if (std::isfinite (e.value))
            {
                const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
                for (auto& v : voices)
                    if (v->isActive() && v->channel() == channel && (e.key < 0 || v->key() == e.key))
                        switch (e.expression)
                        {
                            case Event::Tuning: v->setTuningExpression (std::clamp (e.value, -96.0f, 96.0f)); break;
                            case Event::Pressure: v->setPressure (clamp01 (e.value)); break;
                            case Event::Brightness: v->setSlide (clamp01 (e.value)); break;
                            case Event::Pan: v->setExpressionX (std::clamp (e.value * 2.0f - 1.0f, -1.0f, 1.0f)); break;
                        }
            }
            break;
        case Event::ModWheel:
            modWheel[std::clamp<int> (e.channel, 0, numChannels - 1)] = std::isfinite (e.value) ? clamp01 (e.value) : 0.0f;
            break;
        case Event::Aftertouch:
            aftertouch[std::clamp<int> (e.channel, 0, numChannels - 1)] = std::isfinite (e.value) ? clamp01 (e.value) : 0.0f;
            break;
    }
}

void OwnEngine::controlTick() noexcept
{
    const double dt = kControlPeriod / sampleRate;
    const float fdt = static_cast<float> (dt);
    const ParamIndex& pi = paramIndex();
    globals.songBeats += globals.playing ? dt * globals.bpm / 60.0 : 0.0;
    // global LFO phases (free running; the voices read them for the free-run / mono LFOs)
    for (int l = 0; l < 10; ++l)
    {
        const LfoIdx& li = pi.lfo[l];
        const int sync = static_cast<int> (base[static_cast<size_t> (li.sync)] + 0.5f);
        float rate = base[static_cast<size_t> (li.rate)];
        if (sync >= 1 && sync <= 3)
        {
            static const float beats[13] = { 0.0f, 128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
            const float b = beats[std::clamp (static_cast<int> (base[static_cast<size_t> (li.division)] + 0.5f), 0, 12)];
            rate = b > 0.0f ? static_cast<float> (globals.bpm / 60.0) / b * (sync == 2 ? 1.0f / 1.5f : (sync == 3 ? 1.5f : 1.0f)) : 0.0f;
        }
        lfoPhase[l] += rate * fdt;
        lfoPhase[l] -= std::floor (lfoPhase[l]);
    }
    for (int r = 0; r < 4; ++r)
    {
        const RlfoIdx& ri = pi.rlfo[r];
        float rate = base[static_cast<size_t> (ri.rate)];
        const int sync = static_cast<int> (base[static_cast<size_t> (ri.sync)] + 0.5f);
        if (sync >= 1 && sync <= 3)
        {
            static const float beats[13] = { 0.0f, 128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
            const float b = beats[std::clamp (static_cast<int> (base[static_cast<size_t> (ri.division)] + 0.5f), 0, 12)];
            rate = b > 0.0f ? static_cast<float> (globals.bpm / 60.0) / b * (sync == 2 ? 1.0f / 1.5f : (sync == 3 ? 1.5f : 1.0f)) : 0.0f;
        }
        rlfoValue[r] = rlfo[r].advance (static_cast<int> (base[static_cast<size_t> (ri.style)] + 0.5f), rate * fdt);
    }
    globals.activeVoices = activeCount.load (std::memory_order_relaxed);
    for (size_t i = 0; i < voices.size(); ++i)
        if (voices[i]->isActive())
            voices[i]->tick (globals, static_cast<int> (i));
    // the global modulation (sources that do not belong to a voice) of the global parameters
    for (int i = 0; i < gnumTouched; ++i)
        gdelta[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
    gnumTouched = 0;
    float gsrc[kNumSourceIds] = {};
    const Voice* newest = nullptr; // a voice source on a global destination reads the most recently started active voice (document 04, 3.1)
    for (const auto& v : voices)
        if (v->isActive() && (newest == nullptr || v->serial() > newest->serial()))
            newest = v.get();
    for (int m = 0; m < 8; ++m)
        gsrc[srcMacro (m)] = globals.macro[m];
    for (int l = 0; l < 10; ++l)
    {
        const LfoIdx& li = pi.lfo[l];
        gsrc[srcLfo (l)] = 0.5f + 0.5f * lfoShape (static_cast<int> (base[static_cast<size_t> (li.shape)] + 0.5f), lfoPhase[l], 1u);
    }
    for (int r = 0; r < 4; ++r)
        gsrc[srcRlfo (r)] = 0.5f + 0.5f * rlfoValue[r];
    float mw = 0.0f, at = 0.0f, pb = 0.0f;
    for (int c = 0; c < numChannels; ++c)
    {
        mw = std::max (mw, modWheel[c]);
        at = std::max (at, aftertouch[c]);
        if (std::fabs (bendSemis[c]) > std::fabs (pb))
            pb = bendSemis[c];
    }
    const float bendRange = base[static_cast<size_t> (pi.master.bendRange)];
    gsrc[kSrcModWheel] = mw;
    gsrc[kSrcAftertouch] = at;
    gsrc[kSrcPitchWheel] = bendRange > 0.0f ? std::clamp (pb / bendRange, -1.0f, 1.0f) : 0.0f;
    gsrc[kSrcVelocity] = lastVelocity;
    gsrc[kSrcNote] = lastNoteSemis * (1.0f / 127.0f);
    gsrc[kSrcActiveVoices] = static_cast<float> (activeCount.load (std::memory_order_relaxed)) * (1.0f / 32.0f);
    gsrc[kSrcFixed] = 1.0f;
    matrix.evaluate (newest != nullptr ? newest->sourceValues() : gsrc, true, gdelta.data(), gtouched.data(), gnumTouched, kNumModSlots);

}

void OwnEngine::process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept
{
    if (left == nullptr || numFrames <= 0)
        return;
    std::fill (left, left + numFrames, 0.0f);
    float* rightOut = right;
    if (!prepared)
    {
        if (right != nullptr)
            std::fill (right, right + numFrames, 0.0f);
        return;
    }
    // right may be null (mono output): render into a scratch right channel that is dropped. The scratch is a member to avoid allocation.
    if (rightOut == nullptr)
    {
        if (static_cast<int> (scratchRight.size()) < numFrames)
            scratchRight.assign (static_cast<size_t> (numFrames), 0.0f); // a block larger than prepare () announced: grows once
        rightOut = scratchRight.data();
    }
    std::fill (rightOut, rightOut + numFrames, 0.0f);

    // ---- block snapshot
    for (size_t i = 0; i < base.size(); ++i)
        base[i] = params[i].load (std::memory_order_relaxed);
    matrix.compile (base.data());
    const ParamIndex& pi = paramIndex();
    globals.base = base.data();
    globals.matrix = &matrix;
    globals.sampleRate = sampleRate;
    globals.bpm = tpBpm.load (std::memory_order_relaxed);
    if (tpPlaying.load (std::memory_order_relaxed))
    {
        globals.playing = true;
        globals.songBeats = tpPos.load (std::memory_order_relaxed);
    }
    else
        globals.playing = false;
    for (int m = 0; m < 8; ++m)
        globals.macro[m] = pi.macro[m] >= 0 ? clamp01 (base[static_cast<size_t> (pi.macro[m])] * 0.01f) : 0.0f;
    globals.channelBendSemis = bendSemis;
    globals.channelModWheel = modWheel;
    globals.channelAftertouch = aftertouch;
    globals.lfoPhase = lfoPhase;
    globals.rlfoValue = rlfoValue;
    globals.maxUnison = unisonCap.load (std::memory_order_relaxed);
    // announce the content set this block reads (hazard pointer: the message thread frees a set only when it is neither current nor announced)
    {
        const ContentSet* cs;
        do
        {
            cs = contentCurrent.load();
            contentInUse.store (cs);
        } while (cs != contentCurrent.load());
        globals.content = cs;
    }
    for (int o = 0; o < 3; ++o)
        switchKeySnap[o] = switchKeyAtomic[o].load (std::memory_order_relaxed);
    globals.switchKey = switchKeySnap;
    globals.quality = qualityTier.load (std::memory_order_relaxed);
    globals.oversample = ultraRender.load (std::memory_order_relaxed) || (fxOversampleIndex >= 0 && base[static_cast<size_t> (fxOversampleIndex)] >= 0.5f);
    globals.spectralScratch = spectralScratch.get();

    if (voiceCap.load (std::memory_order_relaxed) < kMaxVoices)
    {
        // a lowered cap: voices above it are released (they fade out), they are not cut
        const size_t cap = static_cast<size_t> (std::clamp (voiceCap.load (std::memory_order_relaxed), 1, kMaxVoices));
        for (size_t i = cap; i < voices.size(); ++i)
            voices[i]->release (0.0f);
    }

    const MasterIdx& mi = pi.master;
    const int fxReverbForce = fxReverbForceIndex;
    int pos = 0, ei = 0;
    while (pos < numFrames)
    {
        if (controlPos == 0)
        {
            controlTick();
            // master volume with the global modulation
            const size_t vi = static_cast<size_t> (mi.volume);
            float db = base[vi];
            if (gdelta[vi] != 0.0f)
                db = modulatedValue (paramTable()[vi], destLawOf (vi), db, gdelta[vi]);
            smoothedGain.setTarget (db > -60.0f ? dbToGain (db) : 0.0f);
            fx.update (base.data(), gdelta.data(), globals.bpm, effectiveFxMask (fxMask.load (std::memory_order_relaxed), base[fxReverbForce] >= 0.5f), kControlPeriod);
        }
        while (ei < numEvents && events[ei].frame <= pos)
            handleEvent (events[ei++]);
        int end = std::min (numFrames, pos + (kControlPeriod - controlPos));
        if (ei < numEvents)
            end = std::min (end, std::max (events[ei].frame, pos + 1));
        const int n = end - pos;
        for (auto& v : voices)
            if (v->isActive())
                v->render (left + pos, rightOut + pos, n, controlPos);
        fx.process (left + pos, rightOut + pos, n);
        for (int i = pos; i < end; ++i)
        {
            const float g = smoothedGain.next();
            left[i] = outputLimit (left[i] * g);
            rightOut[i] = outputLimit (rightOut[i] * g);
        }
        controlPos = (controlPos + n) % kControlPeriod;
        pos = end;
    }
    while (ei < numEvents)
        handleEvent (events[ei++]);

    if (right == nullptr)
        for (int i = 0; i < numFrames; ++i)
            left[i] = 0.5f * (left[i] + rightOut[i]);

    int count = 0;
    for (const auto& v : voices)
        count += v->isActive() ? 1 : 0;
    activeCount.store (count, std::memory_order_relaxed);
    blockCounter.fetch_add (1, std::memory_order_relaxed);
}

bool OwnEngine::copyReadout (LiveValues& out) const noexcept
{
    // the part of the read-out that belongs to the sample-type oscillators (zone and key of the last multisample note); the sources, LFO phases and envelope stages are filled by the live bus of the engine
    out.block = blockCounter.load (std::memory_order_relaxed);
    out.activeVoices = activeCount.load (std::memory_order_relaxed);
    for (int o = 0; o < 3; ++o)
    {
        out.multisampleZone[o] = multiZone[o].load (std::memory_order_relaxed);
        out.multisampleKey[o] = multiKey[o].load (std::memory_order_relaxed);
    }
    return true;
}
} // namespace own
} // namespace synthyboy
