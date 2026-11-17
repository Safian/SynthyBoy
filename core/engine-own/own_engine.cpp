// SynthyBoy - own engine: engine glue (events, voice allocation, control grid, master).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include "own_engine.h"

#include <chrono>
#include "dsp/section_timing.h"

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

bool setOwnLowTierKillFade (IEngine& engine, float seconds) noexcept
{
    auto* own = dynamic_cast<own::OwnEngine*> (&engine);
    if (own == nullptr)
        return false;
    own->setLowTierKillFade (seconds);
    return true;
}

bool setOwnSavingsOff (IEngine& engine, uint32_t off) noexcept
{
    auto* own = dynamic_cast<own::OwnEngine*> (&engine);
    if (own == nullptr)
        return false;
    own->setSavingsOff (off);
    return true;
}

bool setOwnLowTierMorphSpread (IEngine& engine, bool keep) noexcept
{
    auto* own = dynamic_cast<own::OwnEngine*> (&engine);
    if (own == nullptr)
        return false;
    own->setLowTierMorphSpread (keep);
    return true;
}

bool setOwnLowTierUnisonCap (IEngine& engine, int voices) noexcept
{
    auto* own = dynamic_cast<own::OwnEngine*> (&engine);
    if (own == nullptr)
        return false;
    own->setLowTierUnisonCap (voices);
    return true;
}

bool setOwnPadPressureToCutoff (IEngine& engine, float amount01) noexcept
{
    auto* own = dynamic_cast<own::OwnEngine*> (&engine);
    if (own == nullptr)
        return false;
    own->setPadPressureToCutoff (amount01);
    return true;
}

namespace own
{
namespace
{
/** The output safety stage: the identity up to +-1.8 (a loud patch with a boosting warp or filter passes unchanged, like the headroom of engine-vital), above it a smooth knee that
    approaches +-2 (any patch stays bounded). A knee that starts at 1 distorts a signal with peaks of 1.5 and aliases the distortion (a high-pass warp measured -58 dB). */
inline float outputLimit (float y) noexcept
{
    const float a = std::fabs (y);
    if (!(a > 1.8f))
        return y == y ? y : 0.0f;
    return std::copysign (1.8f + 0.2f * std::tanh ((a - 1.8f) * 5.0f), y);
}
} // namespace

OwnEngine::OwnEngine() : fxReverbForceIndex (findParamIndex (hashKey ("reverb.force_enable"))), fxOversampleIndex (findParamIndex (hashKey ("quality.oversample_4x"))), noSmoothingIndex (findParamIndex (hashKey ("quality.no_smoothing")))
{
    for (int k = 0; k < 128; ++k)
        tunedNotes[k] = static_cast<float> (k);
    const auto* table = paramTable();
    for (size_t i = 0; i < kMaxParams; ++i)
        params[i].store (i < paramCount() ? table[i].def : 0.0f, std::memory_order_relaxed);
    for (int i = 0; i < kNumFxInstances; ++i)
        fxiTypeIndex[i] = fxInstanceKeys (i).type;
    for (auto& k : switchKeyAtomic)
        k.store (-1, std::memory_order_relaxed);
    for (int o = 0; o < 3; ++o)
    {
        multiZone[o].store (-1, std::memory_order_relaxed);
        liveSampleHead[o].store (-1.0f, std::memory_order_relaxed);
        liveNumGrains[o].store (0, std::memory_order_relaxed);
        for (int v = 0; v < LiveValues::kReadVoices; ++v)
            liveVoicePos[o][v].store (-1.0f, std::memory_order_relaxed);
        for (int g = 0; g < LiveValues::kReadGrains; ++g)
        {
            liveGrainPos[o][g].store (0.0f, std::memory_order_relaxed);
            liveGrainLife[o][g].store (0.0f, std::memory_order_relaxed);
        }
        multiKey[o].store (-1, std::memory_order_relaxed);
    }
    for (int v = 0; v < LiveValues::kReadVoices; ++v)
    {
        liveSubPhase[v].store (-1.0f, std::memory_order_relaxed);
        liveVoiceStep[v].store (-1.0f, std::memory_order_relaxed);
    }
    for (int e = 0; e < LiveValues::kNumEnvs; ++e)
        for (int v = 0; v < LiveValues::kReadVoices; ++v)
        {
            liveEnvVoiceStage[e][v].store (0.0f, std::memory_order_relaxed);
            liveEnvVoiceFrac[e][v].store (0.0f, std::memory_order_relaxed);
        }
    for (int l = 0; l < LiveValues::kNumLfos; ++l)
        for (int v = 0; v < LiveValues::kReadVoices; ++v)
            liveLfoVoicePhase[l][v].store (-1.0f, std::memory_order_relaxed);
    liveNoisePos.store (-1.0f, std::memory_order_relaxed);
    for (int s = 0; s < LiveValues::kMaxStrips; ++s)
        liveStripPeak[s].store (0.0f, std::memory_order_relaxed);
    spectralScratch = std::make_unique<spectral::Scratch>();
    publishContent (ContentSet::build (StateContent {}, nullptr));
}

OwnEngine::~OwnEngine()
{
    stopFxWorker();
}

void OwnEngine::startFxWorker()
{
    if (fxWorker.joinable())
        return;
    for (auto& w : fxWant)
        w.store (-1, std::memory_order_relaxed);
    fxWorkDue.store (false, std::memory_order_relaxed);
    fxWorkerStop.store (false, std::memory_order_relaxed);
    fxWorker = std::thread ([this]
    {
        while (! fxWorkerStop.load (std::memory_order_acquire))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
            if (! fxWorkDue.exchange (false, std::memory_order_acq_rel))
                continue;
            for (int s = 0; s < kNumFxInstances; ++s)
            {
                const int want = fxWant[s].exchange (-1, std::memory_order_acq_rel);
                if (want < 0)
                    continue;
                try { fxPool.typeChanged (s, want); }
                catch (...) {}
            }
        }
    });
}

void OwnEngine::stopFxWorker() noexcept
{
    if (! fxWorker.joinable())
        return;
    fxWorkerStop.store (true, std::memory_order_release);
    fxWorker.join();
}

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
    noteTuning.setContent (content.tuning);
    ownContent.publish (content);
    paramGen.fetch_add (1u, std::memory_order_release);
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
    const float v = constrainValue (paramTable()[i], plain);
    params[static_cast<size_t> (i)].store (v, std::memory_order_relaxed);
    paramGen.fetch_add (1u, std::memory_order_release);
    for (int s = 0; s < kNumFxInstances; ++s)
        if (fxiTypeIndex[s] == i)
        {
            try
            {
                if (fxWorker.joinable())
                {
                    // live run: no allocation on this thread (it may be the host's audio thread); the worker builds the module
                    fxWant[s].store (static_cast<int> (v + 0.5f), std::memory_order_release);
                    fxWorkDue.store (true, std::memory_order_release);
                }
                else
                    fxPool.typeChanged (s, static_cast<int> (v + 0.5f)); // offline: at once
            }
            catch (...)
            {
            }
            break;
        }
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
    economyMask.store (economyOf (q), std::memory_order_relaxed);
    tierLowCap.store (q.economy < 0 && q.oversampling <= 1, std::memory_order_relaxed);
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
    heldSrcValid = false;
    for (int i = 0; i < kPoolVoices; ++i)
    {
        voices.push_back (std::make_unique<Voice>());
        voices.back()->prepare (sampleRate);
    }
    sustainPending.assign (static_cast<size_t> (kPoolVoices), static_cast<uint8_t> (0));
    base.assign (paramCount(), 0.0f);
    gdelta.assign (paramCount(), 0.0f);
    gdeltaR.assign (paramCount(), 0.0f);
    gtouched.assign (static_cast<size_t> (kNumModSlots), 0);
    pdelta.assign (paramCount(), 0.0f);
    ptouched.assign (static_cast<size_t> (kNumModSlots), 0);
    pnumTouched = 0;
    stopFxWorker();   // no module is built while the pool is prepared again
    realtimeFlag = quality.realtime;
    {
        int types[kNumFxInstances];
        for (int s = 0; s < kNumFxInstances; ++s) // the types set before prepare ()
            types[s] = static_cast<int> (params[static_cast<size_t> (fxiTypeIndex[s])].load (std::memory_order_relaxed) + 0.5f);
        fxPool.prepare (sampleRate, maxBlockFrames, types);
    }
    if (realtimeFlag)
        startFxWorker();
    for (int i = 0; i < 3; ++i)
    {
        fx[i].setRack (i);
        fx[i].setPool (&fxPool);
        fx[i].prepare (sampleRate, maxBlockFrames);
    }
    smoothedGain.reset (sampleRate, 0.02);
    morphCache.prepare();
    snapValid = false;
    mixCached = latCached = false;
    prepared = true;
    reset();
}

void OwnEngine::silence() noexcept
{
    for (auto& g : ghosts)
        g.used = false;
    // the host transport stopped: notes and effect tails end at once, parameters, controllers and content stay
    for (auto& v : voices)
        v->kill();
    for (auto& f : fx)
        f.reset();
    clearLatencyDelays();
    std::fill (sustainPending.begin(), sustainPending.end(), static_cast<uint8_t> (0));
    numPressed = 0;
}

void OwnEngine::clearLatencyDelays() noexcept
{
    dlVoices.clear();
    dlMainOut.clear();
    dlDirect.clear();
    for (int b = 0; b < 2; ++b)
    {
        dlBusOut[b].clear();
        dlBusIn[b].clear();
    }
}

void OwnEngine::reset()
{
    for (auto& g : ghosts)
        g.used = false;
    for (auto& v : voices)
        v->kill();
    for (auto& f : fx)
        f.reset();
    clearLatencyDelays();
    mixCached = latCached = false;
    heldSrcValid = false;
    for (int c = 0; c < numChannels; ++c)
    {
        bendSemis[c] = modWheel[c] = aftertouch[c] = 0.0f;
        sustainDown[c] = false;
    }
    std::fill (sustainPending.begin(), sustainPending.end(), static_cast<uint8_t> (0));
    noteCounter = 0;
    voiceCtlCounter = 0;
    for (auto& row : chokeLatest)
        for (auto& x : row)
            x = 0;
    for (auto& x : chokeCounter)
        x = 0;
    controlPos = 0;
    numPressed = 0;
    haveLastNote = false;
    for (int m = 0; m < 8; ++m)
        macroOverridden[m] = false;
    for (auto& p : lfoPhase)
        p = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        rlfo[i].seed (static_cast<uint32_t> (i) * 4093u + 1u);
        rlfoValue[i] = 0.0f;
    }
    for (int i = 0; i < gnumTouched; ++i)
    {
        gdelta[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
        gdeltaR[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
    }
    gnumTouched = 0;
    for (int i = 0; i < pnumTouched; ++i)
        pdelta[static_cast<size_t> (ptouched[static_cast<size_t> (i)])] = 0.0f;
    pnumTouched = 0;
    smoothedGain.setCurrentAndTarget (dbToGain (readParam (kParamMasterVolume)) * (readParam (kParamMasterVolume) > -60.0f ? 1.0f : 0.0f));
    activeCount.store (0, std::memory_order_relaxed);
    cntNoteOns = cntNoteOffs = cntUnmatched = cntStolen = 0;
}

int OwnEngine::polyphonyLimit() const noexcept
{
    const MasterIdx& mi = paramIndex().master;
    return std::clamp (std::min (voiceCap.load (std::memory_order_relaxed), static_cast<int> (base[static_cast<size_t> (mi.polyphony)] + 0.5f)), 1, kMaxVoices);
}

Voice* OwnEngine::freeVoice() noexcept
{
    for (auto& v : voices)
        if (!v->isActive())
            return v.get();
    return nullptr;
}

/** The voice to give up: released voices first (the quietest, then the oldest), then the sustained ones, then by the priority of the parameter (document 01, 3.1 - 3.3). */
Voice* OwnEngine::pickVictim (bool includeKilling) noexcept
{
    const MasterIdx& mi = paramIndex().master;
    const int priority = static_cast<int> (base[static_cast<size_t> (mi.stealPriority)] + 0.5f);
    Voice* best = nullptr;
    int bestRank = 0;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        Voice* v = voices[i].get();
        if (!v->isActive() || (v->killing() && !includeKilling))
            continue;
        const int rank = v->killing() ? 0 : (v->isReleased() ? 1 : (sustainPending[i] != 0 ? 2 : 3));
        bool better = best == nullptr || rank < bestRank;
        if (!better && rank == bestRank)
        {
            if (rank <= 1)
                better = v->level() < best->level() || (v->level() == best->level() && v->serial() < best->serial());
            else
                switch (priority)
                {
                    case 1: better = v->serial() > best->serial(); break;      // Oldest: the newest goes
                    case 2: better = v->key() < best->key(); break;            // Highest: the lowest goes
                    case 3: better = v->key() > best->key(); break;            // Lowest: the highest goes
                    case 4: better = v->velocity() < best->velocity(); break;  // Velocity: the softest goes
                    default: better = v->serial() < best->serial(); break;     // Newest: the earliest goes
                }
        }
        if (better)
        {
            best = v;
            bestRank = rank;
        }
    }
    return best;
}

Voice* OwnEngine::allocateVoice (bool legato) noexcept
{
    const MasterIdx& mi = paramIndex().master;
    const int P = polyphonyLimit();
    int n = 0;
    for (const auto& v : voices)
        n += (v->isActive() && !v->killing()) ? 1 : 0;
    const bool killMode = base[static_cast<size_t> (mi.voiceOverride)] < 0.5f && !legato;
    Voice* free = freeVoice();
    if (free != nullptr && (killMode || n < P))
        return free; // Kill mode: the overflow is resolved at the next control tick (killExcess)
    Voice* victim = pickVictim (false);
    if (victim != nullptr)
        ++cntStolen;
    return victim != nullptr ? victim : (free != nullptr ? free : pickVictim (true));
}

void OwnEngine::ghostKill (Voice& v) noexcept
{
    Ghost* slot = nullptr;
    for (auto& g : ghosts)
        if (!g.used)
        {
            slot = &g;
            break;
        }
    if (slot == nullptr)
        slot = &ghosts[0]; // all busy (more than 16 steals within a few ms): the oldest ramp is replaced
    for (int t = 0; t < 4; ++t)
    {
        slot->v[t][0] = v.lastBusLeft (t);
        slot->v[t][1] = v.lastBusRight (t);
    }
    slot->age = 0;
    slot->total = std::max (1, static_cast<int> (lowKillSeconds.load (std::memory_order_relaxed) * static_cast<float> (sampleRate)));
    slot->used = true;
    v.kill();
}

/** Adds the declick ramps of the stolen voices to the buses for n samples of the period; the ramp is linear in the sample count, so it does not depend on the block size. */
void OwnEngine::addGhosts (int n) noexcept
{
    for (auto& g : ghosts)
    {
        if (!g.used)
            continue;
        const float inv = 1.0f / static_cast<float> (g.total);
        for (int t = 0; t < 4; ++t)
        {
            const float l = g.v[t][0], r = g.v[t][1];
            if (l == 0.0f && r == 0.0f)
                continue;
            for (int k = 0; k < n; ++k)
            {
                const float gain = std::max (0.0f, 1.0f - static_cast<float> (g.age + k + 1) * inv);
                mixL[t][k] += l * gain;
                mixR[t][k] += r * gain;
            }
        }
        g.age += n;
        if (g.age >= g.total)
            g.used = false;
    }
}

void OwnEngine::updateLowestNote() noexcept
{
    float lowest = 1000.0f;
    for (const auto& v : voices)
        if (v->isActive() && !v->killing())
            lowest = std::min (lowest, v->tunedKeyNote());
    globals.lowestNote = lowest;
}

void OwnEngine::killExcess() noexcept
{
    const int P = polyphonyLimit();
    int n = 0;
    for (const auto& v : voices)
        n += (v->isActive() && !v->killing()) ? 1 : 0;
    while (n > P)
    {
        Voice* v = pickVictim (false);
        if (v == nullptr)
            break;
        ++cntStolen;
        if ((economyMask.load (std::memory_order_relaxed) & kEconomyControl) != 0)
            ghostKill (*v); // tier low: the voice stops, a ramp of its last output fades on the buses
        else
            v->startKill(); // the envelopes fade to zero in 50 ms
        --n;
    }
}

void OwnEngine::removePressed (int playedKey, int channel) noexcept
{
    for (int i = 0; i < numPressed; ++i)
        if (pressed[i].playedKey == playedKey && pressed[i].channel == channel)
        {
            for (int j = i; j + 1 < numPressed; ++j)
                pressed[j] = pressed[j + 1];
            --numPressed;
            return;
        }
}

bool OwnEngine::hasVoice (const Pressed& n) const noexcept
{
    for (const auto& v : voices)
        if (v->isActive() && !v->isReleased() && !v->killing() && v->playedKey() == n.playedKey && v->channel() == n.channel)
            return true;
    return false;
}

void OwnEngine::startNote (const Pressed& n, bool fromRecall, const Voice* releasing) noexcept
{
    const MasterIdx& mi = paramIndex().master;
    const bool legato = base[static_cast<size_t> (mi.legato)] >= 0.5f;
    const bool glideAlways = base[static_cast<size_t> (mi.portAlways)] >= 0.5f;
    NoteStart ns;
    ns.key = n.key;
    ns.playedKey = n.playedKey;
    ns.channel = n.channel;
    ns.noteId = n.noteId;
    ns.velocity = n.velocity;
    ns.tunedNote = tunedNotes[n.key & 127];
    ns.serial = noteCounter++;
    ns.sequence = ++voiceCtlCounter;
    const int others = fromRecall ? 1 : std::max (0, numPressed - 1); // the pressed notes besides this one
    if (legato)
    {
        // legato: a held note is taken over without retriggering the envelopes (pitch glides if the portamento time is set)
        Voice* held = nullptr;
        for (auto& v : voices)
            if (v->isActive() && !v->isReleased() && !v->killing() && (held == nullptr || v->serial() > held->serial()))
                held = v.get();
        if (releasing != nullptr)
            held = const_cast<Voice*> (releasing);
        if (held != nullptr)
        {
            ns.glide = true;
            held->legatoTo (ns);
            lastNoteSemis = ns.tunedNote;
            haveLastNote = true;
            lastVelocity = n.velocity;
            return;
        }
    }
    ns.glide = haveLastNote && (glideAlways || others > 0);
    ns.glideFromSemis = releasing != nullptr ? releasing->currentSemitones() : lastNoteSemis;
    Voice* chosen = fromRecall && releasing != nullptr && base[static_cast<size_t> (mi.voiceOverride)] >= 0.5f
                        ? const_cast<Voice*> (releasing) // Steal mode: the same voice is retaken
                        : allocateVoice (legato);
    if (chosen == nullptr)
        return;
    // poly legato: a note played while another is held continues that voice's envelopes instead of attacking again
    const Voice* heldVoice = nullptr;
    if (base[static_cast<size_t> (mi.polyLegato)] >= 0.5f && !legato)
        for (const auto& v : voices)
            if (v.get() != chosen && v->envelopeHeld() && !v->killing())
            {
                heldVoice = v.get();
                break;
            }
    // the phases the previous note (the voice that started last, the voice that is retaken included) has now, and whether its key is still held: the phase memory and the phase legato of the wavetable oscillators carry them over
    uint32_t prevPhase[3][Voice::kPhaseCount];
    bool havePrev = false, prevHeld = false;
    float prevSubPhase = 0.0f;
    {
        const Voice* prev = nullptr;
        for (const auto& v : voices)
            if (v->isActive() && !v->killing() && (prev == nullptr || v->serial() > prev->serial()))
                prev = v.get();
        havePrev = prev != nullptr;
        prevHeld = havePrev && !prev->isReleased();
        if (havePrev)
            prevSubPhase = prev->subPhase();
        for (int o = 0; havePrev && o < 3; ++o)
            prev->oscPhases (o, prevPhase[o]);
    }
    chosen->start (ns, chosen->lastL(), chosen->lastR());
    if (heldVoice != nullptr)
        chosen->inheritEnvelopes (*heldVoice);
    updateLowestNote();
    chosen->tick (globals, static_cast<int> (std::find_if (voices.begin(), voices.end(), [&] (const auto& p) { return p.get() == chosen; }) - voices.begin()));
    applyPhaseOptions (*chosen, havePrev ? prevPhase : nullptr, prevHeld);
    if (havePrev && paramIndex().sub.phaseContiguous >= 0 && base[static_cast<size_t> (paramIndex().sub.phaseContiguous)] >= 0.5f)
        chosen->setSubPhase (prevSubPhase); // sub.phase_contiguous: the new note continues with the phase the previous one has (docs/engine-spec/02-oscillator.md section 9)
    for (int o = 0; o < 3; ++o)
        if (chosen->multisampleZone (o) >= 0)
        {
            multiZone[o].store (chosen->multisampleZone (o), std::memory_order_relaxed);
            multiKey[o].store (chosen->multisampleKey (o), std::memory_order_relaxed);
        }
    lastNoteSemis = ns.tunedNote;
    haveLastNote = true;
    lastVelocity = n.velocity;
}

/** Phase Memory and Phase Legato of the wavetable oscillators, after the note started (docs/MPC.md "Oscillator UI", Serum 2 manual p.43 and 44). Memory All Voices: the note starts with PHASE and RAND (nothing here). Contiguous: it
    continues with the phase the previous note has now. Per Voice: it starts with the same phase every time, the one captured from the previous note when this mode was entered (the first note after it, with no previous
    note, captures its own). Phase Legato (the oscillator's switch or the global voice.phase_legato, memory on All Voices): a note that starts while the key of the previous note is still held takes that note's phases instead of PHASE and RAND (docs/ui-design/13-new-keys.md A86). Arrays: [oscillator][unison voice] or null. */
void OwnEngine::applyPhaseOptions (Voice& v, const uint32_t (*prev)[Voice::kPhaseCount], bool prevHeld) noexcept
{
    for (int o = 0; o < 3; ++o)
    {
        const OscIdx& oi = paramIndex().osc[o];
        const int mode = static_cast<int> (base[static_cast<size_t> (oi.phaseMemory)] + 0.5f);
        const bool wavetable = base[static_cast<size_t> (oi.type)] < 0.5f;
        if (mode != 2)
            memValid[o] = false;   // leaving Per Voice forgets the captured phases: entering it captures again
        if (!wavetable)
            continue;
        if (mode == 1 && prev != nullptr)
            v.setOscPhases (o, prev[o]);
        else if (mode == 2)
        {
            if (!memValid[o])
            {
                if (prev != nullptr)
                    std::copy (prev[o], prev[o] + Voice::kPhaseCount, memPhase[o]);
                else
                    v.oscPhases (o, memPhase[o]);
                memValid[o] = true;
            }
            v.setOscPhases (o, memPhase[o]);
        }
        else if (mode == 0 && prev != nullptr && prevHeld && (base[static_cast<size_t> (oi.phaseLegato)] >= 0.5f || base[static_cast<size_t> (paramIndex().master.voicePhaseLegato)] >= 0.5f))
            v.setOscPhases (o, prev[o]);
    }
}

void OwnEngine::noteOn (const Event& e) noexcept
{
    if (e.key < 0 || e.key > 127)
        return;
    const MasterIdx& mi = paramIndex().master;
    ++cntNoteOns;
    Pressed n;
    n.channel = std::clamp<int> (e.channel, 0, numChannels - 1);
    n.velocity = std::isnan (e.value) ? 0.0f : std::clamp (e.value, 0.0f, 1.0f);
    n.noteId = e.noteId;
    n.playedKey = e.key;
    const int scaleType = static_cast<int> (base[static_cast<size_t> (mi.scaleType)] + 0.5f);
    n.key = base[static_cast<size_t> (mi.scaleMode)] < 0.5f ? conformKey (e.key, static_cast<int> (base[static_cast<size_t> (mi.scaleKey)] + 0.5f), scaleType) : e.key;
    if (!(tunedNotes[n.key] == tunedNotes[n.key]))
        return; // the tuning does not map this key
    const int sameNote = static_cast<int> (base[static_cast<size_t> (mi.sameNote)] + 0.5f);
    {
        // a note on while no key and no pedal is down starts a new group: the mono LFOs restart (document 03, 3.7)
        bool anyDown = numPressed > 0;
        for (int c = 0; c < numChannels && !anyDown; ++c)
            anyDown = sustainDown[c];
        if (!anyDown)
            for (int l = 0; l < 10; ++l)
                monoCore[l].restart();
    }
    // a key switch of a multisample instrument selects the articulation of the zones that follow
    if (globals.content != nullptr)
        for (int o = 0; o < 3; ++o)
            if (globals.content->instrument[o] != nullptr && globals.content->instrument[o]->isKeySwitchKey (e.key))
            {
                switchKeyAtomic[o].store (e.key, std::memory_order_relaxed);
                switchKeySnap[o] = e.key;
            }

    // a key that is already held
    for (auto& v : voices)
        if (v->isActive() && !v->isReleased() && v->playedKey() == e.key && v->channel() == n.channel)
        {
            if (sameNote == 2) // Ignore
                return;
            if (sameNote == 0) // Steal: the old note fades out before the new one starts
                v->release (0.0f);
        }
    removePressed (e.key, n.channel);
    n.order = noteCounter;
    if (numPressed < 64)
        pressed[numPressed++] = n;
    else
    {
        for (int j = 0; j + 1 < 64; ++j)
            pressed[j] = pressed[j + 1];
        pressed[63] = n;
    }
    startNote (n, false, nullptr);
}

void OwnEngine::noteOff (const Event& e) noexcept
{
    const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
    ++cntNoteOffs;
    bool matched = false;
    removePressed (e.key, channel);
    const float relVel = std::isnan (e.value) ? 0.0f : std::clamp (e.value, 0.0f, 1.0f);
    bool recalled = false;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        Voice& v = *voices[i];
        if (!v.isActive() || v.isReleased() || v.killing() || v.playedKey() != e.key || v.channel() != channel)
            continue;
        if (e.noteId >= 0 && v.noteId() >= 0 && v.noteId() != e.noteId)
            continue;
        matched = true;
        if (sustainDown[channel])
        {
            sustainPending[i] = 1;
            continue;
        }
        // all voices were busy and a pressed note has none: the releasing voice plays it (mono behaviour with polyphony 1, document 01, 3.4)
        if (!recalled && numPressed >= polyphonyLimit())
        {
            const MasterIdx& mi = paramIndex().master;
            const int priority = static_cast<int> (base[static_cast<size_t> (mi.stealPriority)] + 0.5f);
            int pick = -1;
            for (int k = 0; k < numPressed; ++k)
            {
                if (hasVoice (pressed[k]))
                    continue;
                if (pick < 0)
                    pick = k;
                else if (priority == 0 ? pressed[k].order > pressed[pick].order
                         : priority == 2 ? pressed[k].key > pressed[pick].key
                         : priority == 3 ? pressed[k].key < pressed[pick].key
                         : false)
                    pick = k;
            }
            if (pick >= 0)
            {
                Pressed n = pressed[pick];
                n.velocity = v.velocity();
                startNote (n, true, &v);
                if (base[static_cast<size_t> (mi.voiceOverride)] < 0.5f && base[static_cast<size_t> (mi.legato)] < 0.5f)
                {
                    if ((economyMask.load (std::memory_order_relaxed) & kEconomyControl) != 0)
                        ghostKill (v);
                    else
                        v.startKill(); // Kill mode: the released voice fades out, a fresh voice plays the recalled note
                }
                recalled = true;
                continue;
            }
        }
        v.release (relVel);
    }
    if (!matched)
        ++cntUnmatched;
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

/** The MPE axes (13-new-keys.md 4.28): axis 0 = X (pan), 1 = Y (slide), 2 = Z (pressure); value 0 .. 1. */
void OwnEngine::applyAxes (int channel, int axis, float value) noexcept
{
    const MasterIdx& mi = paramIndex().master;
    const int mode = static_cast<int> (base[static_cast<size_t> (mi.mpeXyz)] + 0.5f);
    const bool bipolarY = base[static_cast<size_t> (mi.mpeYBipolar)] >= 0.5f;
    const float y = bipolarY ? std::fabs (2.0f * value - 1.0f) : value;
    if (mode != 0)
    {
        int macro = -1;
        if (mode == 1)
            macro = axis; // X, Y, Z -> macro 1, 2, 3
        else if (axis >= 1)
            macro = axis - 1; // Y, Z -> macro 1, 2
        if (macro >= 0)
        {
            macroOverride[macro] = axis == 1 ? y : value;
            macroOverridden[macro] = true;
        }
    }
    if (axis == 1 && base[static_cast<size_t> (mi.mpeYModwheel)] >= 0.5f)
        modWheel[channel] = y;
}

void OwnEngine::handleEvent (const Event& e) noexcept
{
    const MasterIdx& mi = paramIndex().master;
    const bool mpe = base[static_cast<size_t> (mi.mpeOn)] >= 0.5f;
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
            const float up = mpe && channel > 0 ? base[static_cast<size_t> (mi.mpeBendRange)] : base[static_cast<size_t> (mi.bendRange)];
            const float down = mpe && channel > 0 ? up : (base[static_cast<size_t> (mi.bendDownLink)] >= 0.5f ? up : base[static_cast<size_t> (mi.bendDownRange)]);
            bendSemis[channel] = v * (v >= 0.0f ? up : down);
            if (mpe && channel > 0 && base[static_cast<size_t> (mi.mpeBendToX)] >= 0.5f)
            {
                for (auto& vc : voices)
                    if (vc->isActive() && vc->channel() == channel)
                        vc->setExpressionXRaw (v);
                applyAxes (channel, 0, 0.5f + 0.5f * v);
            }
            break;
        }
        case Event::AllNotesOff:
            for (size_t i = 0; i < voices.size(); ++i)
                if (e.channel < 0 || voices[i]->channel() == e.channel)
                {
                    if (e.value >= 0.5f && voices[i]->isActive() && !voices[i]->killing())
                    {
                        // All Sound Off (CC 120, the shell's stop and mains off): the voices stop at once with the short ramp, not through their release stages
                        if ((economyMask.load (std::memory_order_relaxed) & kEconomyControl) != 0)
                            ghostKill (*voices[i]);
                        else
                            voices[i]->startKill();
                    }
                    else
                        voices[i]->release (0.0f);
                }
            if (e.value >= 0.5f && e.channel < 0)
                std::fill (std::begin (sustainDown), std::end (sustainDown), false);
            std::fill (sustainPending.begin(), sustainPending.end(), static_cast<uint8_t> (0));
            numPressed = 0;
            break;
        case Event::SustainPedal:
            setSustain (e.channel < 0 ? -1 : std::clamp<int> (e.channel, 0, numChannels - 1), e.value >= 0.5f);
            break;
        case Event::NoteExpression:
            if (std::isfinite (e.value))
            {
                const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
                for (auto& v : voices)
                    if (v->isActive() && v->channel() == channel && (e.key < 0 || v->playedKey() == e.key))
                        switch (e.expression)
                        {
                            case Event::Tuning: v->setTuningExpression (std::clamp (e.value, -96.0f, 96.0f)); break;
                            case Event::Pressure: v->setPressure (clamp01 (e.value)); break;
                            case Event::Brightness: v->setSlide (clamp01 (e.value)); break;
                            case Event::Pan: v->setExpressionX (std::clamp (e.value * 2.0f - 1.0f, -1.0f, 1.0f)); break;
                        }
                if (e.expression == Event::Pan)
                    applyAxes (channel, 0, clamp01 (e.value));
                else if (e.expression == Event::Brightness)
                    applyAxes (channel, 1, clamp01 (e.value));
                else if (e.expression == Event::Pressure)
                    applyAxes (channel, 2, clamp01 (e.value));
            }
            break;
        case Event::ModWheel:
            modWheel[std::clamp<int> (e.channel, 0, numChannels - 1)] = std::isfinite (e.value) ? clamp01 (e.value) : 0.0f;
            break;
        case Event::Aftertouch:
        {
            const int channel = std::clamp<int> (e.channel, 0, numChannels - 1);
            aftertouch[channel] = std::isfinite (e.value) ? clamp01 (e.value) : 0.0f;
            if (mpe && channel > 0) // the pressure of a member channel is the Z axis; the pad pressure layer reacts strictly to poly aftertouch (0xA0)
            {
                applyAxes (channel, 2, aftertouch[channel]);
            }
            break;
        }
    }
}

void OwnEngine::controlTick() noexcept
{
    const double dt = kControlPeriod / sampleRate;
    const float fdt = static_cast<float> (dt);
    const ParamIndex& pi = paramIndex();
    globals.songBeats += globals.playing ? dt * globals.bpm / 60.0 : 0.0;
    // global LFO phases (free running; the voices read them for the free-run LFOs) and the shared instance of the mono LFOs
    bool anyDown = numPressed > 0;
    for (int c = 0; c < numChannels && !anyDown; ++c)
        anyDown = sustainDown[c];
    for (int l = 0; l < 10; ++l)
    {
        const LfoIdx& li = pi.lfo[l];
        const int sync = static_cast<int> (base[static_cast<size_t> (li.sync)] + 0.5f);
        const float rate = lfoRateHz (sync, base[static_cast<size_t> (li.rate)], static_cast<int> (base[static_cast<size_t> (li.division)] + 0.5f), globals.bpm, 440.0f, lastNoteSemis,
                                      base[static_cast<size_t> (li.keyTranspose)], base[static_cast<size_t> (li.keyTune)]);
        lfoPhase[l] += rate * fdt;
        lfoPhase[l] -= std::floor (lfoPhase[l]);
        const int syncType = static_cast<int> (base[static_cast<size_t> (li.syncType)] + 0.5f);
        if (syncType >= 2 && li.mono >= 0 && base[static_cast<size_t> (li.mono)] >= 0.5f)
            monoCore[l].advance (syncType, base[static_cast<size_t> (li.phase)] * (1.0f / 360.0f), rate, base[static_cast<size_t> (li.delay)] * 0.001f, fdt, anyDown);
    }
    for (int r = 0; r < 4; ++r)
    {
        const RlfoIdx& ri = pi.rlfo[r];
        const int sync = static_cast<int> (base[static_cast<size_t> (ri.sync)] + 0.5f);
        const float rate = lfoRateHz (sync, base[static_cast<size_t> (ri.rate)], static_cast<int> (base[static_cast<size_t> (ri.division)] + 0.5f), globals.bpm, 440.0f, lastNoteSemis,
                                      base[static_cast<size_t> (ri.keyTranspose)], base[static_cast<size_t> (ri.keyTune)]);
        rlfoValue[r] = rlfo[r].advance (static_cast<int> (base[static_cast<size_t> (ri.style)] + 0.5f), rate * fdt);
    }
    // the macros are destinations too: the modulation of the last period moves their control (an MPE axis that drives a macro wins)
    for (int m = 0; m < 8; ++m)
    {
        const int idx = pi.macro[m];
        if (idx < 0 || macroOverridden[m])
            continue;
        const float knob = clamp01 (base[static_cast<size_t> (idx)] * 0.01f);
        globals.macro[m] = gdelta[static_cast<size_t> (idx)] != 0.0f
            ? clamp01 (modulatedValue (paramTable()[static_cast<size_t> (idx)], destLawOf (static_cast<size_t> (idx)), base[static_cast<size_t> (idx)], gdelta[static_cast<size_t> (idx)]) * 0.01f)
            : knob;
    }
    killExcess();
    {
        const bool mpe = base[static_cast<size_t> (pi.master.mpeOn)] >= 0.5f;
        for (int c = 0; c < numChannels; ++c)
            effBend[c] = bendSemis[c] + (mpe && c > 0 ? bendSemis[0] : 0.0f);
    }
    globals.activeVoices = activeCount.load (std::memory_order_relaxed);
    updateLowestNote();
    for (size_t i = 0; i < voices.size(); ++i)
        if (voices[i]->isActive())
            voices[i]->tick (globals, static_cast<int> (i));
    // the global modulation (sources that do not belong to a voice) of the global parameters
    for (int i = 0; i < gnumTouched; ++i)
    {
        gdelta[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
        gdeltaR[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
    }
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
        if (((matrix.globalSourceMask() >> srcLfo (l)) & 1u) == 0) // only a global slot reads these values
            continue;
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
    {
        EvalContext ctx;
        // A voice source on a global destination reads the most recently started active voice (a released voice that still sounds counts); with no active voice the voice
        // sources keep the values of the last voice (document 04, 3.1: the connection keeps its last value until the next note), while the sources that have a value without a
        // voice (macros, wheels, aftertouch, velocity, note, active voices, fixed, the random LFOs and the free running LFOs a global slot reads) are the current ones.
        float held[kNumSourceIds];
        if (newest != nullptr)
        {
            std::copy (newest->sourceValues(), newest->sourceValues() + kNumSourceIds, heldSrc);
            heldSrcValid = true;
            ctx.src = newest->sourceValues();
        }
        else if (heldSrcValid)
        {
            std::copy (heldSrc, heldSrc + kNumSourceIds, held);
            for (int m = 0; m < 8; ++m)
                held[srcMacro (m)] = gsrc[srcMacro (m)];
            for (int l = 0; l < 10; ++l)
                if (((matrix.globalSourceMask() >> srcLfo (l)) & 1u) != 0)
                    held[srcLfo (l)] = gsrc[srcLfo (l)];
            for (int r = 0; r < 4; ++r)
                held[srcRlfo (r)] = gsrc[srcRlfo (r)];
            for (int id : { static_cast<int> (kSrcModWheel), static_cast<int> (kSrcAftertouch), static_cast<int> (kSrcPitchWheel), static_cast<int> (kSrcVelocity), static_cast<int> (kSrcNote),
                            static_cast<int> (kSrcActiveVoices), static_cast<int> (kSrcFixed) })
                held[id] = gsrc[id];
            ctx.src = held;
        }
        else
            ctx.src = gsrc;
        ctx.global = true;
        ctx.delta = gdelta.data();
        ctx.deltaRight = gdeltaR.data();
        ctx.touched = gtouched.data();
        ctx.numTouched = &gnumTouched;
        ctx.maxTouched = kNumModSlots;
        ctx.dt = fdt;
        ctx.bpm = globals.bpm;
        matrix.evaluate (ctx);
    }
    // The readout without a sounding voice: the voice parameters (filter, oscillators ...) that the sources an idle engine has (macros, mod wheel, aftertouch, pitch
    // wheel, the last velocity / note, fixed) modulate, evaluated once per block like a voice would (audio output unchanged: only writeReadout reads pdelta).
    for (int i = 0; i < pnumTouched; ++i)
        pdelta[static_cast<size_t> (ptouched[static_cast<size_t> (i)])] = 0.0f;
    pnumTouched = 0;
    if (newest == nullptr && activeTargetCount > 0 && matrix.size() > 0)
    {
        float psrc[kNumSourceIds] = {};
        for (int m = 0; m < 8; ++m)
            psrc[srcMacro (m)] = globals.macro[m];
        psrc[kSrcVelocity] = lastVelocity;
        psrc[kSrcNote] = lastNoteSemis * (1.0f / 127.0f);
        psrc[kSrcAftertouch] = at;
        psrc[kSrcPitchWheel] = 0.5f + 0.5f * gsrc[kSrcPitchWheel];
        psrc[kSrcModWheel] = mw;
        psrc[kSrcFixed] = 1.0f;
        uint64_t mask = (uint64_t (1) << kSrcVelocity) | (uint64_t (1) << kSrcNote) | (uint64_t (1) << kSrcAftertouch) | (uint64_t (1) << kSrcPitchWheel)
                        | (uint64_t (1) << kSrcModWheel) | (uint64_t (1) << kSrcFixed);
        for (int m = 0; m < 8; ++m)
            mask |= uint64_t (1) << srcMacro (m);
        EvalContext pc;
        pc.src = psrc;
        pc.global = false;
        pc.delta = pdelta.data();
        pc.touched = ptouched.data();
        pc.numTouched = &pnumTouched;
        pc.maxTouched = kNumModSlots;
        pc.dt = fdt;
        pc.bpm = globals.bpm;
        pc.sourceMask = mask;
        matrix.evaluate (pc);
    }
}

void OwnEngine::process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept
{
    SB_SECTION (sections::engineRest);
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
    std::fill (std::begin (stripBlockPeak), std::end (stripBlockPeak), 0.0f);   // the meters of this block start from nothing
    lastBlockFrames = numFrames;

    // ---- block snapshot
    const uint32_t gen = paramGen.load (std::memory_order_acquire);
    contentNow = ownContent.get();
    blockDirty = !snapValid || gen != snapGen || contentNow != snapContent;
    if (blockDirty)
    {
        for (size_t i = 0; i < base.size(); ++i)
            base[i] = params[i].load (std::memory_order_relaxed);
        matrix.setContent (contentNow);
        matrix.compile (base.data());
        if (contentNow != snapContent)
            morphCache.clear(); // a changed content set can bring another wavetable at the address of an old one
        snapGen = gen;
        snapContent = contentNow;
        snapValid = true;
        ++baseEpoch;
    }
    const ParamIndex& pi = paramIndex();
    globals.base = base.data();
    globals.matrix = &matrix;
    globals.lfoContent = contentNow;
    globals.monoCore = monoCore;
    globals.songSeconds = globals.songBeats * 60.0 / std::max (globals.bpm, 1.0);
    globals.sampleRate = sampleRate;
    globals.bpm = tpBpm.load (std::memory_order_relaxed);
    if (tpPlaying.load (std::memory_order_relaxed))
    {
        globals.playing = true;
        globals.songBeats = tpPos.load (std::memory_order_relaxed);
    }
    else
        globals.playing = false;
    {
        const int xyz = static_cast<int> (base[static_cast<size_t> (pi.master.mpeXyz)] + 0.5f);
        if (xyz != lastXyzMode) // a changed mapping gives the macros back to their knobs
        {
            for (int m = 0; m < 8; ++m)
                macroOverridden[m] = false;
            lastXyzMode = xyz;
        }
    }
    for (int m = 0; m < 8; ++m)
        globals.macro[m] = macroOverridden[m] ? macroOverride[m] : (pi.macro[m] >= 0 ? clamp01 (base[static_cast<size_t> (pi.macro[m])] * 0.01f) : 0.0f);
    if (blockDirty || base[static_cast<size_t> (pi.master.tuningMts)] >= 0.5f) // a live MTS-ESP source is asked every block
        noteTuning.compute (tunedNotes, static_cast<int> (base[static_cast<size_t> (pi.master.tuningScale)] + 0.5f), base[static_cast<size_t> (pi.master.tuningA4)],
                            base[static_cast<size_t> (pi.master.tuningMts)] >= 0.5f);
    globals.noSmoothing = noSmoothingIndex >= 0 && base[static_cast<size_t> (noSmoothingIndex)] >= 0.5f;
    globals.tunedNotes = tunedNotes;
    globals.tuningNoteOnOnly = base[static_cast<size_t> (pi.master.tuningMts)] >= 0.5f && pi.master.tuningMtsNoteOnOnly >= 0 && base[static_cast<size_t> (pi.master.tuningMtsNoteOnOnly)] >= 0.5f
                               && noteTuning.mtsConnected();
    globals.channelBendSemis = effBend;
    globals.channelModWheel = modWheel;
    globals.channelAftertouch = aftertouch;
    globals.lfoPhase = lfoPhase;
    globals.rlfoValue = rlfoValue;
    {
        int mu = unisonCap.load (std::memory_order_relaxed);
        const int lowCap = lowUnisonCap.load (std::memory_order_relaxed);
        if (tierLowCap.load (std::memory_order_relaxed) && lowCap > 0)
            mu = std::min (mu, lowCap);
        globals.maxUnison = mu;
    }
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
    globals.economy = economyMask.load (std::memory_order_relaxed);
    globals.scalarFilters = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingVectorFilters) != 0;
    globals.scalarWarp = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingVectorWarp) != 0;
    globals.usedSources = matrix.sourceMask();
    globals.morphCache = &morphCache;
    globals.killSeconds = (globals.economy & kEconomyControl) != 0 ? lowKillSeconds.load (std::memory_order_relaxed) : 0.05f;
    globals.lowShareMorph = !lowKeepMorphSpread.load (std::memory_order_relaxed);
    globals.baseEpoch = baseEpoch;
    globals.oversample = ultraRender.load (std::memory_order_relaxed) || (fxOversampleIndex >= 0 && base[static_cast<size_t> (fxOversampleIndex)] >= 0.5f);
    globals.spectralScratch = spectralScratch.get();
    globals.chokeLatest = &chokeLatest[0][0];
    globals.chokeCounter = chokeCounter;
    globals.padPressureToCutoff = padPressureToCutoff.load (std::memory_order_relaxed);

    const MasterIdx& mi = pi.master;
    const int fxReverbForce = fxReverbForceIndex;
    int pos = 0, ei = 0;
    const bool eventGrid = (globals.economy & kEconomyControl) != 0;
    while (pos < numFrames)
    {
        if (controlPos == 0)
        {
            controlTick();
            // everything below that reads only parameters (and the global modulation) is redone only when one of them changed since the last period
            const uint32_t mask = effectiveFxMask (fxMask.load (std::memory_order_relaxed), base[fxReverbForce] >= 0.5f);
            const bool inputsSame = !blockDirty && gnumTouched == 0 && mask == lastFxMask;
            const bool mixSame = inputsSame && mixCached;
            lastFxMask = mask;
            // master volume with the global modulation
            if (!mixSame)
            {
                const size_t vi = static_cast<size_t> (mi.volume);
                float db = base[vi];
                if (gdelta[vi] != 0.0f)
                    db = modulatedValue (paramTable()[vi], destLawOf (vi), db, gdelta[vi]);
                if (globals.noSmoothing)
                    smoothedGain.setCurrentAndTarget (db > -60.0f ? dbToGain (db) : 0.0f);
                else
                    smoothedGain.setTarget (db > -60.0f ? dbToGain (db) : 0.0f);
                {
                    // master.stereo_routing (document 01, 8): 100 % leaves the channels alone, 0 % sums them to mono at -3 dB
                    const size_t ri = static_cast<size_t> (mi.stereoRouting);
                    float r = base[ri] * 0.01f;
                    if (gdelta[ri] != 0.0f)
                        r = modulatedValue (paramTable()[ri], destLawOf (ri), base[ri], gdelta[ri]) * 0.01f;
                    const float phi = (1.0f - clamp01 (r)) * 0.78539816f;
                    routeCos = std::cos (phi);
                    routeSin = std::sin (phi);
                }
            }
            FxFrame frame;
            frame.bpm = globals.bpm;
            frame.period = kControlPeriod;
            frame.lastNote = lastNoteSemis;
            frame.noteOns = noteCounter;
            frame.noSmoothing = globals.noSmoothing;
            frame.songBeats = globals.songBeats;
            frame.content = globals.content;
            frame.realtime = realtimeFlag;
            frame.lowCost = (globals.economy & kEconomyMath) != 0;
            frame.muteSkip = (globals.economy & kEconomyControl) != 0 && (savingsOff.load (std::memory_order_relaxed) & kOwnSavingFxMute) == 0;
            for (auto& f : fx)
            {
                f.setLowCost ((globals.economy & kEconomyControl) != 0);
                f.update (base.data(), gdelta.data(), frame, mask, inputsSame);
            }
            // the mixer: the levels of the buses and of the Direct output, their destinations and the rack bypasses
            auto levelOf = [&] (int idx) -> float
            {
                if (idx < 0)
                    return 1.0f;
                float db = base[static_cast<size_t> (idx)];
                if (gdelta[static_cast<size_t> (idx)] != 0.0f)
                    db += gdelta[static_cast<size_t> (idx)];
                return db > -59.99f ? dbToGain (std::min (db, 6.0f)) : 0.0f;
            };
            busGainPrev[1] = busGain[1];
            busGainPrev[2] = busGain[2];
            directGainPrev = directGain;
            if (!mixSame)
            {
                busGain[1] = levelOf (pi.bus1Level);
                busGain[2] = levelOf (pi.bus2Level);
                directGain = levelOf (pi.directLevel);
                busDest[0] = pi.bus1Dest >= 0 ? static_cast<int> (base[static_cast<size_t> (pi.bus1Dest)] + 0.5f) : 0;
                busDest[1] = pi.bus2Dest >= 0 ? static_cast<int> (base[static_cast<size_t> (pi.bus2Dest)] + 0.5f) : 0;
                busBypass[0] = pi.bus1Bypass >= 0 && base[static_cast<size_t> (pi.bus1Bypass)] >= 0.5f;
                busBypass[1] = pi.bus2Bypass >= 0 && base[static_cast<size_t> (pi.bus2Bypass)] >= 0.5f;
                mainBypass = pi.mainBypass >= 0 && base[static_cast<size_t> (pi.mainBypass)] >= 0.5f;
                if (globals.noSmoothing)
                {
                    busGainPrev[1] = busGain[1];
                    busGainPrev[2] = busGain[2];
                    directGainPrev = directGain;
                }
                mixCached = gnumTouched == 0;
            }
            // the latency of the racks (configuration only) and the delays that line the paths up (document 06, 11): it changes only with the mixer settings or a chain
            // that was configured in this period
            if (!(mixSame && latCached && !fx[0].configuredNow() && !fx[1].configuredNow() && !fx[2].configuredNow()))
            {
                const int dest[2] = { busDest[0], busDest[0] == 2 && busDest[1] == 2 ? 0 : busDest[1] };
                const int lat[2] = { busBypass[0] ? 0 : fx[1].rackLatency (1, false), busBypass[1] ? 0 : fx[2].rackLatency (2, false) };
                const int lat0 = mainBypass ? 0 : fx[0].rackLatency (0, false);
                const LatencyPlan plan = LatencyPlan::compute (lat0, lat, dest);
                dlVoices.set (plan.voicesToMain);
                for (int b = 0; b < 2; ++b)
                {
                    dlBusOut[b].set (plan.busOut[b]);
                    dlBusIn[b].set (plan.busIn[b]);
                }
                dlMainOut.set (plan.mainOut);
                dlDirect.set (plan.direct);
                const int latR[2] = { busBypass[0] ? 0 : fx[1].rackLatency (1, true), busBypass[1] ? 0 : fx[2].rackLatency (2, true) };
                const LatencyPlan rep = LatencyPlan::compute (mainBypass ? 0 : fx[0].rackLatency (0, true), latR, dest);
                reportedLatency.store (rep.total, std::memory_order_relaxed);
                latCached = true;
            }
        }
        int end = std::min (numFrames, pos + (kControlPeriod - controlPos));
        if (eventGrid)
        {
            // tier low: an event takes effect at the start of the control period that contains its frame (up to 0.73 ms early), so a block is never cut by events
            while (ei < numEvents && events[ei].frame < end)
                handleEvent (events[ei++]);
        }
        else
        {
            while (ei < numEvents && events[ei].frame <= pos)
                handleEvent (events[ei++]);
            if (ei < numEvents)
                end = std::min (end, std::max (events[ei].frame, pos + 1));
        }
        const int n = end - pos;
        // the voices add to Main, Bus 1, Bus 2 and Direct; the buses run through their racks and go to their destination, Main through the Main rack
        for (int t = 0; t < 4; ++t)
        {
            std::fill (mixL[t], mixL[t] + n, 0.0f);
            std::fill (mixR[t], mixR[t] + n, 0.0f);
        }
        MixOut mo;
        for (int t = 0; t < 4; ++t)
        {
            mo.l[t] = mixL[t];
            mo.r[t] = mixR[t];
        }
        std::fill (std::begin (stripSq), std::end (stripSq), 0.0f);
        mo.srcSq = stripSq;
        for (auto& v : voices)
            if (v->isActive())
                v->render (mo, n, controlPos);
        addGhosts (n);
        for (int s = 0; s < 7; ++s)   // Sub, A, B, C, Noise, Filter 1, Filter 2: the power sum of the voices of this period
            stripBlockPeak[s] = std::max (stripBlockPeak[s], std::sqrt (stripSq[s]));
        for (int b = 1; b <= 2; ++b)   // Bus 1, Bus 2: what the voices send into them, before the rack of the bus
        {
            float pk = 0.0f;
            for (int i = 0; i < n; ++i)
                pk = std::max (pk, std::max (std::fabs (mixL[b][i]), std::fabs (mixR[b][i])));
            stripBlockPeak[6 + b] = std::max (stripBlockPeak[6 + b], pk);
        }
        dlVoices.process (mixL[0], mixR[0], n);
        dlDirect.process (mixL[3], mixR[3], n);
        dlBusIn[0].process (mixL[1], mixR[1], n); // a bus that is fed by the other waits for what the first sends into it
        dlBusIn[1].process (mixL[2], mixR[2], n);
        auto runBus = [&] (int b) // b = 1, 2
        {
            if (!busBypass[b - 1])
                fx[b].process (mixL[b], mixR[b], n, controlPos);
            float outL[kControlPeriod], outR[kControlPeriod];
            for (int i = 0; i < n; ++i)
            {
                const float g = lerp (busGainPrev[b], busGain[b], static_cast<float> (controlPos + i + 1) * (1.0f / static_cast<float> (kControlPeriod)));
                outL[i] = mixL[b][i] * g;
                outR[i] = mixR[b][i] * g;
            }
            dlBusOut[b - 1].process (outL, outR, n);
            const int dest = busDest[b - 1] == 2 && b == 2 && busDest[0] == 2 ? 0 : busDest[b - 1]; // both choose Other bus: bus 2 acts as Main (no feedback)
            const int to = dest == 1 ? 3 : (dest == 2 ? 3 - b : 0);
            for (int i = 0; i < n; ++i)
            {
                mixL[to][i] += outL[i];
                mixR[to][i] += outR[i];
            }
        };
        if (busDest[1] == 2 && busDest[0] != 2) // bus 2 feeds bus 1: it runs first
        {
            runBus (2);
            runBus (1);
        }
        else
        {
            runBus (1);
            runBus (2);
        }
        if (!mainBypass)
            fx[0].process (mixL[0], mixR[0], n, controlPos);
        dlMainOut.process (mixL[0], mixR[0], n);
        std::copy (mixL[0], mixL[0] + n, left + pos);
        std::copy (mixR[0], mixR[0] + n, rightOut + pos);
        for (int i = pos; i < end; ++i)
        {
            const float g = smoothedGain.next();
            if (routeSin != 0.0f)
            {
                const float l0 = left[i], r0 = rightOut[i];
                left[i] = l0 * routeCos + r0 * routeSin;
                rightOut[i] = r0 * routeCos + l0 * routeSin;
            }
            // Direct: past every rack (and the stereo routing), scaled by its own level; the master volume applies to everything that reaches the output, the Direct sum
            // included (it did not before: a Direct source ignored master.volume); the sum goes through the safety stage
            const float dg = lerp (directGainPrev, directGain, static_cast<float> (controlPos + (i - pos) + 1) * (1.0f / static_cast<float> (kControlPeriod)));
            left[i] = outputLimit ((left[i] + mixL[3][i - pos] * dg) * g);
            rightOut[i] = outputLimit ((rightOut[i] + mixR[3][i - pos] * dg) * g);
            stripBlockPeak[9] = std::max (stripBlockPeak[9], std::max (std::fabs (left[i]), std::fabs (rightOut[i])));   // Master: what leaves the engine
        }
        controlPos = (controlPos + n) % kControlPeriod;
        pos = end;
    }
    while (ei < numEvents)
        handleEvent (events[ei++]);

    if (right == nullptr)
        for (int i = 0; i < numFrames; ++i)
            left[i] = 0.5f * (left[i] + rightOut[i]);

    killExcess(); // the overflow of the Kill mode is resolved before the count is reported
    int count = 0;
    for (const auto& v : voices)
        count += (v->isActive() && !v->killing()) ? 1 : 0; // a voice in its 50 ms kill fade is on its way out: the count stays within the polyphony
    activeCount.store (count, std::memory_order_relaxed);
    publishStats();
    writeReadout();
    blockCounter.fetch_add (1, std::memory_order_relaxed);
}

/** The snapshot of voiceStats (): the voices, the keys the events say are held, and the bug state: a voice that is not released while its key is not held and the pedal is up. */
void OwnEngine::publishStats() noexcept
{
    int active = 0, released = 0, sustaining = 0, orphans = 0;
    unsigned miss = 0;
    for (const auto& v : voices)
    {
        if (!v->isActive())
            continue;
        miss |= v->missingMask();
        ++active;
        if (v->isReleased() || v->killing())
        {
            ++released;
            continue;
        }
        ++sustaining;
        bool held = sustainDown[std::clamp (v->channel(), 0, numChannels - 1)];
        for (int i = 0; i < numPressed && !held; ++i)
            held = pressed[i].playedKey == v->playedKey() && pressed[i].channel == v->channel();
        if (!held)
            ++orphans;
    }
    {
        // the content that no voice needs yet but the patch asks for: a user table that is not there, the impulse of Convolve, the sample of the noise source
        const ParamIndex& pi = paramIndex();
        const ContentSet* cs = globals.content;
        for (int o = 0; o < 3; ++o)
            if (base[static_cast<size_t> (pi.osc[o].on)] >= 0.5f && base[static_cast<size_t> (pi.osc[o].type)] < 0.5f
                && static_cast<int> (base[static_cast<size_t> (pi.osc[o].wavetable)] + 0.5f) == kWavetablePresetTable && (cs == nullptr || cs->userTable[o] == nullptr))
                miss |= 8u << o;
        static const int convolveOn = findParamIndex (hashKey ("convolve.on"));
        if (convolveOn >= 0 && base[static_cast<size_t> (convolveOn)] >= 0.5f && (cs == nullptr || cs->impulse == nullptr))
            miss |= 64u;
    }
    missingMask.store (miss, std::memory_order_relaxed);
    int lo = -1, hi = -1;
    for (int i = 0; i < numPressed; ++i)
    {
        lo = lo < 0 ? pressed[i].playedKey : std::min (lo, pressed[i].playedKey);
        hi = std::max (hi, pressed[i].playedKey);
    }
    bool pedal = false;
    for (int c = 0; c < numChannels; ++c)
        pedal = pedal || sustainDown[c];
    const int ints[8] = { active, released, sustaining, numPressed, lo, hi, orphans, pedal ? 1 : 0 };
    for (int i = 0; i < 8; ++i)
        stIntegers[i].store (ints[i], std::memory_order_relaxed);
    stCounters[0].store (cntNoteOns, std::memory_order_relaxed);
    stCounters[1].store (cntNoteOffs, std::memory_order_relaxed);
    stCounters[2].store (cntUnmatched, std::memory_order_relaxed);
    stCounters[3].store (cntStolen, std::memory_order_relaxed);
}

bool OwnEngine::voiceStats (VoiceStats& out) const noexcept
{
    out.active = stIntegers[0].load (std::memory_order_relaxed);
    out.released = stIntegers[1].load (std::memory_order_relaxed);
    out.sustaining = stIntegers[2].load (std::memory_order_relaxed);
    out.heldKeys = stIntegers[3].load (std::memory_order_relaxed);
    out.lowestHeld = stIntegers[4].load (std::memory_order_relaxed);
    out.highestHeld = stIntegers[5].load (std::memory_order_relaxed);
    out.orphans = stIntegers[6].load (std::memory_order_relaxed);
    out.sustainPedal = stIntegers[7].load (std::memory_order_relaxed) != 0;
    out.noteOns = stCounters[0].load (std::memory_order_relaxed);
    out.noteOffs = stCounters[1].load (std::memory_order_relaxed);
    out.unmatchedNoteOffs = stCounters[2].load (std::memory_order_relaxed);
    out.stolen = stCounters[3].load (std::memory_order_relaxed);
    return true;
}

void OwnEngine::setReadoutTargets (const int* idx, int count) noexcept
{
    const int n = std::clamp (count, 0, LiveValues::kMaxTargets);
    for (int i = 0; i < n; ++i)
        pendingTargets[i].store (idx[i], std::memory_order_relaxed);
    pendingCount.store (n, std::memory_order_release);
}

bool OwnEngine::copyReadout (LiveValues& out) const noexcept
{
    out.block = blockCounter.load (std::memory_order_relaxed);
    out.activeVoices = activeCount.load (std::memory_order_relaxed);
    for (int i = 0; i < LiveValues::kMaxSources; ++i)
        out.source[i] = liveSource[i].load (std::memory_order_relaxed);
    for (int i = 0; i < LiveValues::kNumLfos; ++i)
        out.lfoPhase[i] = liveLfo[i].load (std::memory_order_relaxed);
    for (int i = 0; i < LiveValues::kNumEnvs; ++i)
    {
        out.envStage[i] = liveEnvStage[i].load (std::memory_order_relaxed);
        out.envPosition[i] = liveEnvPos[i].load (std::memory_order_relaxed);
        out.envFineStage[i] = liveEnvFine[i].load (std::memory_order_relaxed);
        out.envFineFraction[i] = liveEnvFineFrac[i].load (std::memory_order_relaxed);
    }
    out.numTargets = liveNumTargets.load (std::memory_order_relaxed);
    for (int i = 0; i < out.numTargets; ++i)
        out.target[i] = liveTarget[i].load (std::memory_order_relaxed);
    for (int v = 0; v < LiveValues::kReadVoices; ++v)
    {
        out.subPhase[v] = liveSubPhase[v].load (std::memory_order_relaxed);
        out.voiceStep[v] = liveVoiceStep[v].load (std::memory_order_relaxed);
    }
    out.noisePos = liveNoisePos.load (std::memory_order_relaxed);
    out.numLfoVoices = std::clamp (liveNumLfoVoices.load (std::memory_order_relaxed), 0, static_cast<int> (LiveValues::kReadVoices));
    for (int l = 0; l < LiveValues::kNumLfos; ++l)
        for (int v = 0; v < LiveValues::kReadVoices; ++v)
            out.lfoVoicePhase[l][v] = liveLfoVoicePhase[l][v].load (std::memory_order_relaxed);
    for (int e = 0; e < LiveValues::kNumEnvs; ++e)
        for (int v = 0; v < LiveValues::kReadVoices; ++v)
        {
            out.envVoiceStage[e][v] = liveEnvVoiceStage[e][v].load (std::memory_order_relaxed);
            out.envVoiceFraction[e][v] = liveEnvVoiceFrac[e][v].load (std::memory_order_relaxed);
        }
    for (int o = 0; o < 3; ++o)
    {
        out.sampleHead[o] = liveSampleHead[o].load (std::memory_order_relaxed);
        for (int v = 0; v < LiveValues::kReadVoices; ++v)
            out.voicePos[o][v] = liveVoicePos[o][v].load (std::memory_order_relaxed);
        out.numGrains[o] = std::clamp (liveNumGrains[o].load (std::memory_order_relaxed), 0, static_cast<int> (LiveValues::kReadGrains));
        for (int g = 0; g < out.numGrains[o]; ++g)
        {
            out.grainPos[o][g] = liveGrainPos[o][g].load (std::memory_order_relaxed);
            out.grainLife[o][g] = liveGrainLife[o][g].load (std::memory_order_relaxed);
        }
        out.multisampleZone[o] = multiZone[o].load (std::memory_order_relaxed);
        out.multisampleKey[o] = multiKey[o].load (std::memory_order_relaxed);
    }
    for (int s = 0; s < LiveValues::kMaxStrips; ++s)
        out.stripPeak[s] = liveStripPeak[s].load (std::memory_order_relaxed);
    return true;
}

/** End of a block: the display gets the sources of the voice played last (the global sources when none sounds), the LFO cycle positions, the envelope stages and the modulated knobs. */
void OwnEngine::writeReadout() noexcept
{
    const ParamIndex& pi = paramIndex();
    // the meters of the mixer: the peak of the block on a scale of 60 dB (0 = -60 dBFS, 1 = full scale), a fast attack and a release of about 300 ms; published to the display thread through atomics (no lock)
    {
        const float release = std::exp (-static_cast<float> (std::max (lastBlockFrames, 1)) / static_cast<float> (std::max (sampleRate, 1.0) * 0.3));
        for (int s = 0; s < LiveValues::kMaxStrips; ++s)
        {
            const float pk = stripBlockPeak[s];
            const float level = pk > 1.0e-3f ? std::clamp (1.0f + 20.0f * std::log10 (pk) / 60.0f, 0.0f, 1.0f) : 0.0f;
            stripShown[s] = std::max (level, stripShown[s] * release);
            if (stripShown[s] < 0.002f)
                stripShown[s] = 0.0f;
            liveStripPeak[s].store (stripShown[s], std::memory_order_relaxed);
        }
    }
    const Voice* newest = nullptr;
    for (const auto& v : voices)
        if (v->isActive() && (newest == nullptr || v->serial() > newest->serial()))
            newest = v.get();
    const int tc = pendingCount.load (std::memory_order_acquire);
    if (tc != activeTargetCount)
        activeTargetCount = tc;
    for (int i = 0; i < activeTargetCount; ++i)
        activeTargets[i] = pendingTargets[i].load (std::memory_order_relaxed);
    for (int i = 0; i < LiveValues::kMaxSources; ++i)
    {
        const int id = i + 1; // index k = choice value k + 1
        float v = 0.0f;
        if (newest != nullptr && id < kNumSourceIds)
            v = newest->sourceValues()[id];
        else if (id >= kSrcMacro1 && id < kSrcMacro1 + 4)
            v = globals.macro[id - kSrcMacro1];
        else if (id >= kSrcMacro5 && id < kSrcMacro5 + 4)
            v = globals.macro[4 + id - kSrcMacro5];
        else if (id == kSrcPitchWheel)
            v = 0.5f;
        const bool lfo = (id >= kSrcLfo1 && id < kSrcLfo1 + 8) || id == kSrcLfo9 || id == kSrcLfo10;
        liveSource[i].store (lfo ? 2.0f * v - 1.0f : v, std::memory_order_relaxed);
    }
    for (int o = 0; o < 3; ++o)
        liveSampleHead[o].store (newest != nullptr ? newest->sampleHead (o) : -1.0f, std::memory_order_relaxed);
    // the newest active voices (newest first, at most kReadVoices): the read positions of the sample based oscillators, the phase of the sub; the grains of the voice that is heard
    {
        const Voice* order[LiveValues::kReadVoices] = {};
        uint32_t bound = 0xffffffffu;
        int found = 0;
        for (; found < LiveValues::kReadVoices; ++found)
        {
            const Voice* best = nullptr;
            for (const auto& v : voices)
                if (v->isActive() && v->serial() < bound && (best == nullptr || v->serial() > best->serial()))
                    best = v.get();
            if (best == nullptr)
                break;
            order[found] = best;
            bound = best->serial();
        }
        for (int i = 0; i < LiveValues::kReadVoices; ++i)
        {
            for (int o = 0; o < 3; ++o)
                liveVoicePos[o][i].store (i < found ? order[i]->readPosition (o) : -1.0f, std::memory_order_relaxed);
            liveSubPhase[i].store (i < found ? order[i]->subPhase() : -1.0f, std::memory_order_relaxed);
            liveVoiceStep[i].store (i < found ? static_cast<float> (order[i]->voiceControlStep()) : -1.0f, std::memory_order_relaxed);
            for (int l = 0; l < LiveValues::kNumLfos; ++l)
                liveLfoVoicePhase[l][i].store (i < found ? order[i]->lfoReadout (l) : -1.0f, std::memory_order_relaxed);
            for (int e = 0; e < LiveValues::kNumEnvs; ++e)
            {
                float fs = 0.0f, ff = 0.0f;
                if (i < found)
                    order[i]->envReadoutFine (e, fs, ff);
                liveEnvVoiceStage[e][i].store (fs, std::memory_order_relaxed);
                liveEnvVoiceFrac[e][i].store (ff, std::memory_order_relaxed);
            }
        }
        liveNumLfoVoices.store (found, std::memory_order_relaxed);
        float gp[LiveValues::kReadGrains], gl[LiveValues::kReadGrains];
        for (int o = 0; o < 3; ++o)
        {
            const int n = newest != nullptr ? newest->readGrains (o, gp, gl, LiveValues::kReadGrains) : 0;
            for (int g = 0; g < n; ++g)
            {
                liveGrainPos[o][g].store (gp[g], std::memory_order_relaxed);
                liveGrainLife[o][g].store (gl[g], std::memory_order_relaxed);
            }
            liveNumGrains[o].store (n, std::memory_order_relaxed);
        }
        liveNoisePos.store (newest != nullptr ? newest->noisePosition() : -1.0f, std::memory_order_relaxed);
    }
    for (int l = 0; l < LiveValues::kNumLfos; ++l)
        liveLfo[l].store (newest != nullptr ? newest->lfoReadout (l) : -1.0f, std::memory_order_relaxed);
    for (int e = 0; e < LiveValues::kNumEnvs; ++e)
    {
        float st = 0.0f, ps = 0.0f;
        if (newest != nullptr)
            newest->envReadout (e, st, ps);
        liveEnvStage[e].store (st, std::memory_order_relaxed);
        liveEnvPos[e].store (ps, std::memory_order_relaxed);
        float fs = 0.0f, ff = 0.0f;
        if (newest != nullptr)
            newest->envReadoutFine (e, fs, ff);
        liveEnvFine[e].store (fs, std::memory_order_relaxed);
        liveEnvFineFrac[e].store (ff, std::memory_order_relaxed);
    }
    for (int i = 0; i < activeTargetCount; ++i)
    {
        const int idx = activeTargets[i];
        float v = idx >= 0 && static_cast<size_t> (idx) < base.size() ? base[static_cast<size_t> (idx)] : 0.0f;
        if (idx >= 0 && static_cast<size_t> (idx) < base.size())
        {
            if (gdelta[static_cast<size_t> (idx)] != 0.0f)
                v = modulatedValue (paramTable()[idx], destLawOf (static_cast<size_t> (idx)), v, gdelta[static_cast<size_t> (idx)]);
            else if (newest != nullptr)
                v = newest->modulatedParam (idx);
            else if (pdelta[static_cast<size_t> (idx)] != 0.0f)
                v = modulatedValue (paramTable()[idx], destLawOf (static_cast<size_t> (idx)), v, pdelta[static_cast<size_t> (idx)]);
        }
        liveTarget[i].store (v, std::memory_order_relaxed);
    }
    liveNumTargets.store (activeTargetCount, std::memory_order_relaxed);
    (void) pi;
}
} // namespace own
} // namespace synthyboy
