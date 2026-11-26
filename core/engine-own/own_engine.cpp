// SynthyBoy - own engine: engine glue (events, voice allocation, control grid, master).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include "own_engine.h"
#include "synthyboy/state.h"
#include "osc/lib_osc.h"
#include "synthyboy/spectral/start_cache.h"

#include <chrono>
#include "dsp/section_timing.h"
#include "dsp/peak.h"
#include "dsp/master_stage.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

bool setOwnVoiceCountAlpha (IEngine& engine, float alpha) noexcept
{
    auto* own = dynamic_cast<own::OwnEngine*> (&engine);
    if (own == nullptr)
        return false;
    own->setVoiceCountAlpha (alpha);
    return true;
}

float ownVoiceCountGain (const IEngine& engine) noexcept
{
    const auto* own = dynamic_cast<const own::OwnEngine*> (&engine);
    if (own == nullptr)
        return 1.0f;
    return own->voiceCountGain();
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
/** The consumers of the block snapshot `base` that a changed parameter can reach. A bit is set for a parameter that may change what the consumer derives from it; a consumer is rebuilt only when
    one of its bits changed (OwnEngine::process, "block snapshot"). The classes only ever over-approximate: when in doubt a parameter carries the bit. */
enum ParamClassBit : uint8_t
{
    kParamSlot = 1,   // ModMatrix::compile () reads it (every field of ModSlotParams of every slot)
    kParamTuning = 2, // NoteTuning::compute () reads it (the scale, A4 and the MTS-ESP switch)
    kParamVoice = 4,  // a voice (or the voice side of the engine) may read it: everything except the effect parameters
};

bool hasPrefix (const char* key, const char* prefix) noexcept
{
    return std::strncmp (key, prefix, std::strlen (prefix)) == 0;
}

/** Keys that only the effect racks read (fx/fx_chain.cpp, fx_modules.cpp, fx_budget.cpp) and never a voice: the effect instances, the modules and the order of the racks. Everything else
    (the oscillators, filters, envelopes, LFOs, matrix, master, buses, quality ...) is voice visible. Pinned by the bit identity test (test_own_param_snapshot.cpp), which moves every
    parameter against the full rebuild. */
bool isEffectOnlyKey (const char* key) noexcept
{
    static const char* const prefixes[] = { "fxi", "fx.", "fxfilter.", "compressor.", "reverb.", "eq.", "delay.", "bode.", "convolve.", "chorus.", "phaser.", "utility.", "distortion.",
                                            "hyperdim.", "flanger.", "splitter_lmh.", "splitter_lh.", "splitter_ms." };
    for (const char* p : prefixes)
        if (hasPrefix (key, p))
            return true;
    return false;
}

void classifyParams (std::vector<uint8_t>& out)
{
    const size_t n = paramCount();
    out.assign (n, static_cast<uint8_t> (0));
    const ParamInfo* table = paramTable();
    for (size_t i = 0; i < n; ++i)
        if (! isEffectOnlyKey (table[i].key))
            out[i] |= kParamVoice;
    const ParamIndex& pi = paramIndex();
    auto mark = [&] (int idx, uint8_t bit)
    {
        if (idx >= 0 && static_cast<size_t> (idx) < n)
            out[static_cast<size_t> (idx)] |= bit;
    };
    for (const ModSlotParams& sp : pi.slot)
        for (const int idx : { sp.source, sp.destination, sp.amount, sp.power, sp.bipolar, sp.stereo, sp.bypass, sp.auxSource, sp.auxInv, sp.auxPower, sp.auxOutput, sp.rise, sp.fall, sp.delay,
                               sp.delayBeats, sp.delaySync })
            mark (idx, kParamSlot);
    mark (pi.master.tuningScale, kParamTuning);
    mark (pi.master.tuningA4, kParamTuning);
    mark (pi.master.tuningMts, kParamTuning);
}
} // namespace

OwnEngine::OwnEngine() : fxReverbForceIndex (findParamIndex (hashKey ("reverb.force_enable"))), fxOversampleIndex (findParamIndex (hashKey ("quality.oversample_4x"))), noSmoothingIndex (findParamIndex (hashKey ("quality.no_smoothing"))), convolveOnIndex (findParamIndex (hashKey ("convolve.on")))
{
    for (int k = 0; k < 128; ++k)
        tunedNotes[k] = static_cast<float> (k);
    const auto* table = paramTable();
    for (size_t i = 0; i < kMaxParams; ++i)
        params[i].store (i < paramCount() ? table[i].def : 0.0f, std::memory_order_relaxed);
    for (int i = 0; i < kNumFxInstances; ++i)
        fxiTypeIndex[i] = fxInstanceKeys (i).type;
    {
        static const char* const kWtKeys[6] = { "osc1.wavetable", "osc2.wavetable", "osc3.wavetable", "osc1.spec.flt_table", "osc2.spec.flt_table", "osc3.spec.flt_table" };
        for (int i = 0; i < 6; ++i)
        {
            const ParamInfo* info = findParamByKey (kWtKeys[i]);
            wtParamIdx[i] = info != nullptr ? findParamIndex (info->id) : -1;
        }
    }
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
    {
        liveStripPeak[s].store (0.0f, std::memory_order_relaxed);
        for (int field = 0; field < 6; ++field) liveStereoMeter[field][s].store (0.0f, std::memory_order_relaxed);
    }
    spectralScratch = std::make_unique<spectral::Scratch>();
    publishContent (ContentSet::build (StateContent {}, nullptr));
    contentHeld = contentCurrent.load();   // the first set is pinned from the start: the audio thread always has a valid set to fall back to
    contentHeldSlot = 0;
    contentHold[0].store (contentHeld);
}

OwnEngine::~OwnEngine()
{
    stopFxWorker();
    if (lazyBank)
        stopBuiltinBankBackground();   // gives the share back; the last one stops and joins the builder thread (bound: one frame of a table and its 10 ms sleep). The thread holds no pointer to this engine.
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
        runHelperThreadStartHook ("sb-fx-build");   // the shell's scheduling class and cores, not those of the thread that started this one (dsp/helper_thread.h)
        for (;;)
        {
            fxWake.wait();   // no timeout: a set type key notifies (OwnEngine::setParam), stopFxWorker notifies
            if (fxWorkerStop.load (std::memory_order_acquire))
                break;
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
    fxWake.notify();
    fxWorker.join();
}

void OwnEngine::publishContent (std::shared_ptr<const ContentSet> set)
{
    std::lock_guard<std::mutex> lock (contentMutex);
    const ContentSet* raw = set.get();
    contentOwned.push_back (std::move (set));
    contentCurrent.store (raw); // seq_cst: the audio thread re-validates the pointer it pinned in contentHold after reading it (announceContent)
    const ContentSet* const pinned0 = contentHold[0].load(); // seq_cst, after the store above: a set the audio thread validated against the previous current is seen here
    const ContentSet* const pinned1 = contentHold[1].load();
    for (size_t i = 0; i < contentOwned.size();)
    {
        const ContentSet* p = contentOwned[i].get();
        if (p != raw && p != pinned0 && p != pinned1)
            contentOwned.erase (contentOwned.begin() + static_cast<std::ptrdiff_t> (i));
        else
            ++i;
    }
    for (auto& k : switchKeyAtomic)
        k.store (-1, std::memory_order_relaxed);
}

/** The audio thread's hand over (a fixed number of operations, no loop that a publisher can keep running, no free): pins the newest set and returns the set this block reads.
    The set read last stays pinned in contentHold[contentHeldSlot]; an attempt reads current, pins it in the other slot and reads current again (the publisher stores current and then reads both slots,
    so a set that is still current after it was pinned cannot be freed). An attempt that finds current changed again is repeated, at most kContentAnnounceTries times; if all of them lose, the
    block keeps the set it already pinned, which no publication can free (it is a valid, if one publication old, state: the next block tries again). */
const ContentSet* OwnEngine::announceContent() noexcept
{
    uint32_t tries = 0;
    bool stored = false, settled = false;
    void (*const hook) (void*) = announceHook.load (std::memory_order_acquire);
    for (int attempt = 0; attempt < kContentAnnounceTries && ! settled; ++attempt)
    {
        ++tries;
        const ContentSet* const cand = contentCurrent.load();
        if (cand == contentHeld)
        {
            settled = true;                          // the pinned set is the current one
            break;
        }
        contentHold[contentHeldSlot ^ 1].store (cand);
        stored = true;
        if (hook != nullptr)
            hook (announceHookArg.load (std::memory_order_relaxed));   // tests only
        if (cand == contentCurrent.load())
        {
            contentHeld = cand;
            contentHeldSlot ^= 1;
            statContentAdoptions.store (statContentAdoptions.load (std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
            settled = true;
        }
    }
    if (! settled)
    {
        // every attempt lost against a publication: keep the set that is pinned, and do not leave the last candidate pinned
        if (stored)
            contentHold[contentHeldSlot ^ 1].store (nullptr);
        statContentFallbacks.store (statContentFallbacks.load (std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
    }
    if (tries > statContentMaxTries.load (std::memory_order_relaxed))
        statContentMaxTries.store (tries, std::memory_order_relaxed);
    return contentHeld;
}

std::shared_ptr<const ContentSet> OwnEngine::preparedOscContent()
{
    std::lock_guard<std::mutex> lock (contentMutex);
    return contentOwned.empty() ? nullptr : contentOwned.back();
}

void OwnEngine::rebuildContent()
{
    publishContent (ContentSet::build (contentState, contentStore));
}

void OwnEngine::setContent (const StateContent& content)
{
    std::lock_guard<std::mutex> lock (contentWriterMutex);
    noteTuning.setContent (content.tuning);
    ownContent.publish (content);
    contentState = content;
    rebuildContent();
}

void OwnEngine::setPreparedContent (const StateContent& state, std::shared_ptr<const ContentSet> decoded)
{
    std::lock_guard<std::mutex> lock (contentWriterMutex);
    noteTuning.setContent (state.tuning);
    ownContent.publish (state);
    contentState = state;
    publishContent (std::move (decoded));
}

void OwnEngine::setContentStore (const IContentStore* store)
{
    std::lock_guard<std::mutex> lock (contentWriterMutex);
    contentStore = store;
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
    const bool changed = storeParamAt (i, plain);
    // a write of the value the parameter already holds (the host and the display repeat values) needs no new snapshot: the generation moves only for a changed bit pattern
    // (kOwnSavingParamDiff off: as before, every write moves it)
    if (changed || (savingsOff.load (std::memory_order_relaxed) & kOwnSavingParamDiff) != 0)
        paramGen.fetch_add (1u, std::memory_order_release);
}

void OwnEngine::setPresetParams (const ParamValues& values) noexcept
{
    const int count = static_cast<int> (paramCount());
    for (int i = 0; i < count; ++i)
        if (! std::isnan (values.plain[i]))
            storeParamAt (i, values.plain[i]);
    paramGen.fetch_add (1u, std::memory_order_release);
}

bool OwnEngine::storeParamAt (int i, float plain) noexcept
{
    const float v = constrainValue (paramTable()[i], plain);
    const float held = params[static_cast<size_t> (i)].load (std::memory_order_relaxed);
    uint32_t newBits, heldBits;
    std::memcpy (&newBits, &v, sizeof newBits);
    std::memcpy (&heldBits, &held, sizeof heldBits);
    const bool changed = newBits != heldBits; // the bit pattern, as the snapshot compares: -0 and +0 are different values
    params[static_cast<size_t> (i)].store (v, std::memory_order_relaxed);
    for (int k = 0; k < 6; ++k)
        if (wtParamIdx[k] == i)
        {
            requestBuiltinWavetable (k, std::clamp (static_cast<int> (v + 0.5f), 0, kNumWavetableChoices - 1));   // lock free: the background builder takes the latest request of each slot; until the table is built the oscillator keeps playing the one it had
            break;
        }
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
                    fxWake.notify();   // lock free (dsp/helper_thread.h)
                }
                else
                    fxPool.typeChanged (s, static_cast<int> (v + 0.5f)); // offline: at once
            }
            catch (...)
            {
            }
            break;
        }
    return changed;
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
    spectralStartOn.store (spectralStartOf (q), std::memory_order_relaxed);
    spectralStartBudget.store (q.spectralStartBudgetUs, std::memory_order_relaxed);
    if (spectralStartCacheOf (q))
        spectral::StartCache::instance().prepare();   // allocates the table once, here and not on the audio thread
    spectralStartCacheOn.store (spectralStartCacheOf (q), std::memory_order_relaxed);
    tierLowCap.store (q.economy < 0 && q.oversampling <= 1, std::memory_order_relaxed);
    fxMask.store (q.fxMask, std::memory_order_relaxed);
    fxMakeup.store (q.fxMakeup, std::memory_order_relaxed);
    fxBudgetPct.store (std::max (q.fxBudgetPct, 0), std::memory_order_relaxed);
}

bool OwnEngine::fxBudgetStatus (FxBudgetStatus& out) const noexcept
{
    out.budgetCenti = fxBudgetPct.load (std::memory_order_relaxed) * 100;
    out.admittedCenti = budgetStatus[0].load (std::memory_order_relaxed);
    out.requestedCenti = budgetStatus[1].load (std::memory_order_relaxed);
    out.overModules = static_cast<uint32_t> (budgetStatus[2].load (std::memory_order_relaxed));
    out.overSlots = static_cast<uint32_t> (budgetStatus[3].load (std::memory_order_relaxed));
    return true;
}

/** The gain of the bypassed effects of bus `b` for this block (`prev` -> `now`, 1 when the bus rack bypasses nothing that has a model). With nothing shared between Main and the bus it is the product
    of the gains of the effects, as inside any rack. A bus whose input is a copy of what Main carries (a source with a send to the bus and its own way to Main) adds up with Main in amplitude when
    its effects are bypassed, but not (or less) when they are active: the gain G then solves  Pm + G^2 Pb + 2 G C = Pm + Pb Pmod + 2 A C  for the powers of Main (Pm), the bus input (Pb), their
    cross term (C, all measured before the racks, smoothed over 100 ms) and the power gain Pmod and coherent amplitude A of the bypassed effects. */
void OwnEngine::makeupOfBus (int b, int n, float& prev, float& now) noexcept
{
    float pw, co, amp;
    prev = now = 1.0f;
    if (!fx[b].busMakeup (pw, co, amp))
    {
        busMkSeen[b] = false;
        busMkPrev[b] = busMkNow[b] = 1.0f;
        return;
    }
    double sm = 0.0, sb = 0.0, sc = 0.0;
    for (int i = 0; i < n; ++i)
    {
        sm += static_cast<double> (mixL[0][i]) * mixL[0][i] + static_cast<double> (mixR[0][i]) * mixR[0][i];
        sb += static_cast<double> (mixL[b][i]) * mixL[b][i] + static_cast<double> (mixR[b][i]) * mixR[b][i];
        sc += static_cast<double> (mixL[0][i]) * mixL[b][i] + static_cast<double> (mixR[0][i]) * mixR[b][i];
    }
    double* st = busStat[b];
    if (sm + sb > 1.0e-12 * static_cast<double> (n))
    {
        const double k = busMkSeen[b] ? 1.0 - std::exp (-static_cast<double> (n) / (0.1 * sampleRate)) : 1.0;
        st[0] += (sm / n - st[0]) * k;
        st[1] += (sb / n - st[1]) * k;
        st[2] += (sc / n - st[2]) * k;
    }
    float target = amp;
    const double pm = st[0], pb = st[1], c = st[2];
    if (fxMakeup.load (std::memory_order_relaxed) && pb > 1.0e-12 && pm > 1.0e-12)
    {
        const double disc = c * c + pb * (pb * pw + 2.0 * co * c);
        if (disc > 0.0)
        {
            const double g = (-c + std::sqrt (disc)) / pb;
            if (g > 0.0)
                target = static_cast<float> (g);
        }
    }
    target = std::clamp (target, 0.03f, 16.0f);
    if (!busMkSeen[b])
    {
        busMkPrev[b] = 1.0f;
        busMkNow[b] = target;
        busMkSeen[b] = true;
    }
    else
    {
        busMkPrev[b] = busMkNow[b];
        busMkNow[b] += (target - busMkNow[b]) * (1.0f - std::exp (-static_cast<float> (n) / (0.02f * static_cast<float> (sampleRate))));
    }
    prev = busMkPrev[b];
    now = busMkNow[b];
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
    // The built-in tables (allocates, FFTs: not on the audio thread). Offline (tests, renders) and every shell that does not ask for lazyTables: all of them here, as always. Lazy (the MPC shell): the one cycle
    // tables (a few ms) and the tables the current parameters name are built here; any other table is built when it is first chosen, by a low priority thread that takes the latest request of each
    // oscillator (docs/MPC.md "Start-up time"). Until it is ready the oscillator keeps playing the table it had (see wtHeld, below).
    if (quality.realtime && quality.lazyTables)
    {
        ensureBuiltinSingleCycleTables();
        ensureBuiltinWavetables (wavetableMaskOfParams());
        if (! lazyBank)
        {
            startBuiltinBankBackground();
            lazyBank = true;
        }
    }
    else
        ensureAllBuiltinWavetables();
    for (int o = 0; o < 3; ++o)   // the tables the oscillators play when the first block comes: the ones the parameters name (built just now)
    {
        const int idx = wtParamIdx[o];
        const int choice = idx >= 0 ? std::clamp (static_cast<int> (params[static_cast<size_t> (idx)].load (std::memory_order_relaxed) + 0.5f), 0, kNumWavetableChoices - 1) : kWavetableSaw;
        wtHeld[o] = choice == kWavetablePresetTable ? nullptr : builtinWavetableIfReady (choice);
    }
    // the lazily built tables of the keys (they allocate on first use): build them here
    (void) paramIndex();
    (void) fxIndex();
    (void) libKeys (0);   // the library oscillator key table (snprintf, hashing on its first call): not on the audio thread
    (void) destLawOf (0);
    (void) WtOsc::decimatorTaps();

    const auto tVoices0 = std::chrono::steady_clock::now();
    voices.clear();
    activeMask = 0;
    heldSrcValid = false;
    // Economy control retires stolen voices immediately and carries their fades in ghosts[]. Full control keeps
    // the original pool so bursts can use every free slot for the 50 ms tails. Live-switchable callers also need all slots.
    const bool fixedGhostPool = quality.fixedVoiceProfile && (economyOf (quality) & kEconomyControl) != 0;
    const int preparedCap = fixedGhostPool ? std::clamp (quality.maxVoices, 1, kMaxVoices) : kMaxVoices;
    const int poolSize = preparedCap + kSpareVoices;
    for (int i = 0; i < poolSize; ++i)
    {
        voices.push_back (std::make_unique<Voice>());
        voices.back()->prepare (sampleRate);
        voices.back()->attachPool (&activeMask, i);
    }
    sustainPending.assign (voices.size(), static_cast<uint8_t> (0));
    base.assign (paramCount(), 0.0f);
    classifyParams (paramClass);
    gdelta.assign (paramCount(), 0.0f);
    gdeltaR.assign (paramCount(), 0.0f);
    gtouched.assign (static_cast<size_t> (kNumModSlots), 0);
    pdelta.assign (paramCount(), 0.0f);
    ptouched.assign (static_cast<size_t> (kNumModSlots), 0);
    pnumTouched = 0;
    const auto tFx0 = std::chrono::steady_clock::now();
    prepareVoicesMs.store (std::chrono::duration<double, std::milli> (tFx0 - tVoices0).count(), std::memory_order_relaxed);
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
    budgetEpoch = 0xFFFFFFFFu; // the FX budget is admitted again with the first block
    lastFxBudgetPct = -1;
    fxBudget = own::FxBudgetResult {};
    for (int s = 0; s < 4; ++s)
        budgetStatus[s].store (0, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
    {
        fx[i].setOverBudget (0u, 0u);
        fx[i].setRack (i);
        fx[i].setPool (&fxPool);
        fx[i].setBusDeferred (i != 0);
        fx[i].prepare (sampleRate, maxBlockFrames);
    }
    prepareFxMs.store (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - tFx0).count(), std::memory_order_relaxed);
    smoothedGain.reset (sampleRate, 0.02);
    morphCache.prepare();
    snapValid = false;
    mixCached = latCached = false;
    prepared = true;
    reset();
}

unsigned OwnEngine::wavetableMaskOfParams() const noexcept
{
    unsigned mask = 0;
    for (int k = 0; k < 6; ++k)
        if (wtParamIdx[k] >= 0)
            mask |= 1u << std::clamp (static_cast<int> (params[static_cast<size_t> (wtParamIdx[k])].load (std::memory_order_relaxed) + 0.5f), 0, kNumWavetableChoices - 1);
    return mask;
}

void OwnEngine::readyContentFor (const float* plain, size_t count) noexcept
{
    if (plain == nullptr)
        return;
    unsigned mask = 0;
    for (int k = 0; k < 6; ++k)
        if (wtParamIdx[k] >= 0 && static_cast<size_t> (wtParamIdx[k]) < count)
            mask |= 1u << std::clamp (static_cast<int> (plain[wtParamIdx[k]] + 0.5f), 0, kNumWavetableChoices - 1);
    ensureBuiltinWavetables (mask);
}

bool OwnEngine::contentPending() const noexcept
{
    for (int k = 0; k < 6; ++k)
        if (wtParamIdx[k] >= 0 && ! builtinWavetableSettled (std::clamp (static_cast<int> (params[static_cast<size_t> (wtParamIdx[k])].load (std::memory_order_relaxed) + 0.5f), 0, kNumWavetableChoices - 1)))
            return true;
    return false;
}

void OwnEngine::readyContent() noexcept
{
    ensureBuiltinWavetables (wavetableMaskOfParams());
}

double OwnEngine::wavetableBuildMs() const noexcept
{
    return builtinBankBuildMs();
}

void OwnEngine::silence() noexcept
{
    voiceCountGainCur = voiceCountGainPrev = 1.0f;
    voiceCountGainDb = 0.0f;
    vcgRampStart = 0;
    vcgRampLen = kControlPeriod;
    lastVoiceCountGain.store (1.0f, std::memory_order_relaxed);
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
    readoutDemand.store (kReadoutDemandBlocks, std::memory_order_relaxed);
    readoutIdle = false;
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < LiveValues::kMaxStrips; ++s)
        {
            stereoBlock[ch][s] = stereoShown[ch][s] = stereoHold[ch][s] = 0.0f;
            stereoHoldFrames[ch][s] = stereoClipFrames[ch][s] = 0;
            for (int field = 0; field < 6; ++field) liveStereoMeter[field][s].store (0.0f, std::memory_order_relaxed);
        }
    voiceCountGainCur = voiceCountGainPrev = 1.0f;
    voiceCountGainDb = 0.0f;
    vcgRampStart = 0;
    vcgRampLen = kControlPeriod;
    lastVoiceCountGain.store (1.0f, std::memory_order_relaxed);
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
    spectralScratch->start.reset();
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

namespace
{
inline int lowestSetBit (uint64_t m) noexcept
{
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanForward64 (&i, m);
    return static_cast<int> (i);
#else
    return __builtin_ctzll (m);
#endif
}
} // namespace

template <class F> void OwnEngine::forEachActive (F&& f) noexcept
{
    if (globals.referenceControl)
    {
        for (size_t i = 0; i < voices.size(); ++i)
            if (voices[i]->isActive())
                f (static_cast<int> (i));
        return;
    }
    // a snapshot of the set: what f (i) does to voice i (a tick that ends it) does not change the other voices, so the set is the one the loop over the pool visits
    for (uint64_t m = activeMask; m != 0; m &= m - 1)
        f (lowestSetBit (m));
}

int OwnEngine::polyphonyLimit() const noexcept
{
    const MasterIdx& mi = paramIndex().master;
    const int preparedCap = static_cast<int> (voices.size()) - kSpareVoices;
    return std::clamp (std::min (voiceCap.load (std::memory_order_relaxed), static_cast<int> (base[static_cast<size_t> (mi.polyphony)] + 0.5f)), 1, preparedCap);
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
    if (free != nullptr && killMode && n >= P)
    {
        // Kill mode, the limit is reached: the voice killExcess () would pick (the same rule) is given up now, at this note-on, so that no more than the limit sound from the first sample of
        // the block (a strike of many notes used to start and render all of them for one control period before killExcess () ran at the next tick). killExcess () stays as the safety net.
        if (Voice* victim = pickVictim (false))
        {
            ++cntStolen;
            killOverflowVoice (*victim);
        }
        return free;
    }
    if (free != nullptr && (killMode || n < P))
        return free;
    Voice* victim = pickVictim (false);
    if (victim != nullptr)
    {
        ++cntStolen;
        spectralScratch->start.cancelSerial (victim->serial());   // the stolen voice starts a new note: its old note's waiting Spectral voices are gone
    }
    return victim != nullptr ? victim : (free != nullptr ? free : pickVictim (true));
}

void OwnEngine::ghostKill (Voice& v) noexcept
{
    spectralScratch->start.cancelSerial (v.serial());   // a Spectral voice of the note that has not started yet never will; the work booked for the ones that did is given back
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
        // the gain of each sample is the same for the four buses: made once (the same expression of the same values), not once per bus (the reference, kOwnSavingVoiceControl, makes it per bus)
        float gains[kControlPeriod];
        bool gainsMade = false;
        for (int t = 0; t < 4; ++t)
        {
            const float l = g.v[t][0], r = g.v[t][1];
            if (l == 0.0f && r == 0.0f)
                continue;
            if (globals.referenceControl)
            {
                for (int k = 0; k < n; ++k)
                {
                    const float gain = std::max (0.0f, 1.0f - static_cast<float> (g.age + k + 1) * inv);
                    mixL[t][k] += l * gain;
                    mixR[t][k] += r * gain;
                }
                continue;
            }
            if (!gainsMade)
            {
                for (int k = 0; k < n; ++k)
                    gains[k] = std::max (0.0f, 1.0f - static_cast<float> (g.age + k + 1) * inv);
                gainsMade = true;
            }
            for (int k = 0; k < n; ++k)
            {
                mixL[t][k] += l * gains[k];
                mixR[t][k] += r * gains[k];
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
    forEachActive ([&] (int i)
    {
        const Voice& v = *voices[static_cast<size_t> (i)];
        if (!v.killing())
            lowest = std::min (lowest, v.tunedKeyNote());
    });
    globals.lowestNote = lowest;
}

void OwnEngine::killExcess() noexcept
{
    const int P = polyphonyLimit();
    int n = 0;
    forEachActive ([&] (int i) { n += voices[static_cast<size_t> (i)]->killing() ? 0 : 1; });
    while (n > P)
    {
        Voice* v = pickVictim (false);
        if (v == nullptr)
            break;
        ++cntStolen;
        killOverflowVoice (*v);
        --n;
    }
}

void OwnEngine::killOverflowVoice (Voice& v) noexcept
{
    spectralScratch->start.cancelSerial (v.serial());
    if ((economyMask.load (std::memory_order_relaxed) & kEconomyControl) != 0)
        ghostKill (v); // tier low: the voice stops, a ramp of its last output fades on the buses
    else
        v.startKill(); // the envelopes fade to zero in 50 ms
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
    for (size_t i = 0; i < voices.size(); ++i)
        if (voices[i].get() == chosen)
            sustainPending[i] = 0; // a pedal hold that belonged to the voice's previous note does not carry over to the new one
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
        if (v->isActive() && !v->isReleased() && !v->killing() && v->playedKey() == e.key && v->channel() == n.channel)
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
    globals.songSeconds = globals.songBeats * 60.0 / std::max (globals.bpm, 1.0);
    // global LFO phases (free running; the voices read them for the free-run LFOs) and the shared instance of the mono LFOs
    // Voice scaling belongs to the owning shared clock, once; local/host
    // clocks apply the same factor in Voice::lfoValue.
    const float voiceLfoScale = base[static_cast<size_t> (pi.vctl.scaleLfo)] * 0.01f;
    const float voiceLfoFactor = base[static_cast<size_t> (pi.vctl.scaleLfoMode)] >= 0.5f
                                  ? voiceLfoScale : 1.0f / std::max (voiceLfoScale, 0.01f);
    bool anyDown = numPressed > 0;
    for (int c = 0; c < numChannels && !anyDown; ++c)
        anyDown = sustainDown[c];
    for (int l = 0; l < 10; ++l)
    {
        const LfoIdx& li = pi.lfo[l];
        const int sync = static_cast<int> (base[static_cast<size_t> (li.sync)] + 0.5f);
        const float rate = voiceLfoFactor * lfoRateHz (sync, base[static_cast<size_t> (li.rate)], static_cast<int> (base[static_cast<size_t> (li.division)] + 0.5f), globals.bpm, 440.0f, lastNoteSemis,
                                      base[static_cast<size_t> (li.keyTranspose)], base[static_cast<size_t> (li.keyTune)]);
        lfoPhase[l] += rate * fdt;
        lfoPhase[l] -= ffloor (lfoPhase[l]);   // (ffloor: std::floor for every float, without the libm call; CPU-REVIEW-VOICE-MOD.md V12)
        const int syncType = static_cast<int> (base[static_cast<size_t> (li.syncType)] + 0.5f);
        if (syncType >= 2 && li.mono >= 0 && base[static_cast<size_t> (li.mono)] >= 0.5f)
            monoCore[l].advance (syncType, base[static_cast<size_t> (li.phase)] * (1.0f / 360.0f), rate, base[static_cast<size_t> (li.delay)] * 0.001f, fdt, anyDown);
    }
    for (int r = 0; r < 4; ++r)
    {
        const RlfoIdx& ri = pi.rlfo[r];
        const int sync = static_cast<int> (base[static_cast<size_t> (ri.sync)] + 0.5f);
        const float rate = voiceLfoFactor * lfoRateHz (sync, base[static_cast<size_t> (ri.rate)], static_cast<int> (base[static_cast<size_t> (ri.division)] + 0.5f), globals.bpm, 440.0f, lastNoteSemis,
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
    for (size_t i = 0; i < voices.size(); ++i)
        if (sustainPending[i] != 0 && (!voices[i]->isActive() || voices[i]->killing()))
            sustainPending[i] = 0; // a voice that was stolen or killed keeps no pending pedal release
    killExcess();
    {
        const bool mpe = base[static_cast<size_t> (pi.master.mpeOn)] >= 0.5f;
        for (int c = 0; c < numChannels; ++c)
            effBend[c] = bendSemis[c] + (mpe && c > 0 ? bendSemis[0] : 0.0f);
    }
    globals.activeVoices = activeCount.load (std::memory_order_relaxed);
    updateLowestNote();
    {
        // the voice the display reads (the newest, as writeReadout and the global modulation find it): the other voices skip what only the display of an unused LFO needs
        globals.lfoDisplaySkip = !globals.referenceControl && (savingsOff.load (std::memory_order_relaxed) & kOwnSavingLfoDisplay) == 0;
        uint32_t newestSerial = 0;
        forEachActive ([&] (int i) { newestSerial = std::max (newestSerial, voices[static_cast<size_t> (i)]->serial()); });
        globals.newestSerial = newestSerial;
    }
    forEachActive ([&] (int i) { voices[static_cast<size_t> (i)]->tick (globals, i); });
    // the global modulation (sources that do not belong to a voice) of the global parameters
    for (int i = 0; i < gnumTouched; ++i)
    {
        gdelta[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
        gdeltaR[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
    }
    gnumTouched = 0;
    float gsrc[kNumSourceIds] = {};
    const Voice* newest = nullptr; // a voice source on a global destination reads the most recently started active voice (document 04, 3.1)
    forEachActive ([&] (int i)
    {
        const Voice* v = voices[static_cast<size_t> (i)].get();
        if (newest == nullptr || v->serial() > newest->serial())
            newest = v;
    });
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
        ctx.reference = globals.referenceControl;
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
        pc.reference = globals.referenceControl;
        matrix.evaluate (pc);
    }
}

static inline uint32_t computeEffectiveSoloMask (uint32_t rawSoloMask, const float* base, const ParamIndex& pi) noexcept
{
    if (rawSoloMask == 0 || base == nullptr)
        return 0;
    uint32_t eff = 0;
    // Strip 0: Sub (generator)
    if ((rawSoloMask & (1u << 0)) && (pi.sub.on < 0 || base[static_cast<size_t> (pi.sub.on)] >= 0.5f))
        eff |= (1u << 0);
    // Strips 1..3: Osc 1..3 (generators)
    for (int o = 0; o < 3; ++o)
        if ((rawSoloMask & (1u << (1 + o))) && (pi.osc[o].on < 0 || base[static_cast<size_t> (pi.osc[o].on)] >= 0.5f))
            eff |= (1u << (1 + o));
    // Strip 4: Noise (generator)
    if ((rawSoloMask & (1u << 4)) && (pi.noise.on < 0 || base[static_cast<size_t> (pi.noise.on)] >= 0.5f))
        eff |= (1u << 4);
    // Strips 5..6: Filter 1 and Filter 2 (always valid routing nodes/strips, filter.on is bypass not power)
    if (rawSoloMask & (1u << 5)) eff |= (1u << 5);
    if (rawSoloMask & (1u << 6)) eff |= (1u << 6);
    // Strips 7..8: Bus 1 and Bus 2 (always valid strips)
    if (rawSoloMask & (1u << 7)) eff |= (1u << 7);
    if (rawSoloMask & (1u << 8)) eff |= (1u << 8);
    return eff;
}

void OwnEngine::process (const Event* events, int numEvents, float* left, float* right, int numFrames) noexcept
{
    SB_SECTION (sections::engineRest);
    if (left == nullptr || numFrames <= 0)
        return;
    std::fill (left, left + numFrames, 0.0f);
    if (!prepared)
    {
        if (right != nullptr)
            std::fill (right, right + numFrames, 0.0f);
        return;
    }
    // right may be null (mono output): every 32 frame chunk below renders its right channel into a small stack buffer and folds it into left (no allocation, any block size). A given right is written chunk by chunk.
    std::fill (std::begin (stripBlockPeak), std::end (stripBlockPeak), 0.0f);   // the meters of this block start from nothing
    masterBlockPeakL = masterBlockPeakR = 0.0f;
    for (auto& channel : stereoBlock) std::fill (std::begin (channel), std::end (channel), 0.0f);
    lastBlockFrames = numFrames;
    {
        int h = meterHold.load (std::memory_order_relaxed);
        sourceMeters = h > 0;
        if (sourceMeters)
            meterHold.compare_exchange_strong (h, h - 1, std::memory_order_relaxed);   // (a request made since the load stays whole)
    }

    // ---- block snapshot
    const uint32_t gen = paramGen.load (std::memory_order_acquire);
    const auto content = ownContent.acquire();
    const bool contentChanged = content.generation != snapContentGen;
    blockDirty = !snapValid || gen != snapGen || contentChanged;
    // What the changed parameters can reach (kParamSlot / kParamTuning / kParamVoice, see classifyParams): the matrix is compiled again only when a slot parameter or the content changed,
    // the tuning table only when a tuning parameter or the content changed, the voices drop what they derived from the parameters only when a parameter they can read changed.
    // kOwnSavingParamDiff off (measurements, the bit identity test): everything is rebuilt on every change, as it was before.
    bool matrixDirty = true, tuningDirty = true, voicesDirty = true, noopGeneration = false;
    if (blockDirty)
    {
        const bool full = !snapValid || (savingsOff.load (std::memory_order_relaxed) & kOwnSavingParamDiff) != 0 || paramClass.size() != base.size();
        if (full)
        {
            for (size_t i = 0; i < base.size(); ++i)
                base[i] = params[i].load (std::memory_order_relaxed);
        }
        else
        {
            uint8_t changed = 0;
            bool anyChanged = false;
            for (size_t i = 0; i < base.size(); ++i)
            {
                const float v = params[i].load (std::memory_order_relaxed);
                uint32_t a, b;
                std::memcpy (&a, &v, sizeof a);
                std::memcpy (&b, &base[i], sizeof b);
                if (a != b) // bit pattern: a value that is only equal as a float (-0 / +0) counts as changed
                {
                    base[i] = v;
                    changed |= paramClass[i];
                    anyChanged = true;
                }
            }
            matrixDirty = contentChanged || content.content != snapContentPtr || (changed & kParamSlot) != 0;
            tuningDirty = contentChanged || (changed & kParamTuning) != 0;
            voicesDirty = contentChanged || (changed & kParamVoice) != 0;
            // a generation that moved without a value or the content changing (a write of the value a parameter already had, a tuning source that was set again): the snapshot is the one
            // of the block before, so nothing it feeds is derived again (the FX budget, the idle effect racks, the master gains and the latency plan keep their inputs)
            noopGeneration = !anyChanged && !contentChanged && content.content == snapContentPtr;
        }
        if (noopGeneration)
        {
            snapGen = gen;
            blockDirty = false;
            matrixDirty = tuningDirty = voicesDirty = false;
        }
        else
        {
            matrix.setContent (content.content);
            statSnapshots.fetch_add (1u, std::memory_order_relaxed);
            if (matrixDirty)
            {
                matrix.compile (base.data());
                statMatrixCompiles.fetch_add (1u, std::memory_order_relaxed);
            }
            if (contentChanged)
                morphCache.clear(); // setContent can also replace the user wavetables
            snapContentPtr = content.content;
            snapGen = gen;
            snapContentGen = content.generation;
            snapValid = true;
            ++snapEpoch;
            if (voicesDirty)
            {
                ++baseEpoch;
                statVoiceEpochs.fetch_add (1u, std::memory_order_relaxed);
            }
        }
    }
    const ParamIndex& pi = paramIndex();
    globals.base = base.data();
    globals.matrix = &matrix;
    globals.lfoContent = content.content;
    globals.monoCore = monoCore;
    globals.sampleRate = sampleRate;
    globals.bpm = tpBpm.load (std::memory_order_relaxed);
    if (tpPlaying.load (std::memory_order_relaxed))
    {
        globals.playing = true;
        globals.songBeats = tpPos.load (std::memory_order_relaxed);
    }
    else
        globals.playing = false;
    globals.songSeconds = globals.songBeats * 60.0 / std::max (globals.bpm, 1.0);
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
    if ((blockDirty && tuningDirty) || base[static_cast<size_t> (pi.master.tuningMts)] >= 0.5f) // a live MTS-ESP source is asked every block
    {
        statTuningComputes.fetch_add (1u, std::memory_order_relaxed);
        noteTuning.compute (tunedNotes, static_cast<int> (base[static_cast<size_t> (pi.master.tuningScale)] + 0.5f), base[static_cast<size_t> (pi.master.tuningA4)],
                            base[static_cast<size_t> (pi.master.tuningMts)] >= 0.5f);
    }
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
    // pin the content set this block reads (hazard slots: the message thread frees a set only when it is neither current nor pinned; a fixed number of steps, see announceContent)
    globals.content = announceContent();
    for (int o = 0; o < 3; ++o)
        switchKeySnap[o] = switchKeyAtomic[o].load (std::memory_order_relaxed);
    globals.switchKey = switchKeySnap;
    // The built-in table of each oscillator for this block, from the same snapshot of the parameters the voices read: the table the choice names when it is built, else the one the oscillator had
    // (never a substitute); the finished table is handed over by the bank's atomic pointer, so a voice sees an index and a table that belong together or the previous pair.
    for (int o = 0; o < 3; ++o)
    {
        const int idx = wtParamIdx[o];
        if (idx < 0)
            continue;
        const int choice = std::clamp (static_cast<int> (base[static_cast<size_t> (idx)] + 0.5f), 0, kNumWavetableChoices - 1);
        if (choice == kWavetablePresetTable)
            continue;   // the user table of the content: the voice reads it itself
        if (const Wavetable* w = builtinWavetableIfReady (choice))
            wtHeld[o] = w;
    }
    globals.wtResolved = wtHeld;
    globals.quality = qualityTier.load (std::memory_order_relaxed);
    globals.economy = economyMask.load (std::memory_order_relaxed);
    globals.scalarFilters = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingVectorFilters) != 0;
    globals.scalarWarp = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingVectorWarp) != 0;
    globals.scalarContent = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingContentKernels) != 0;
    globals.referenceControl = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingVoiceControl) != 0;
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
    bool blockClipped = false;
    const bool eventGrid = (globals.economy & kEconomyControl) != 0;
    while (pos < numFrames)
    {
        if (controlPos == 0)
        {
            soundingAtPeriodStart = 0;
            forEachActive ([&] (int i)
            {
                const Voice* v = voices[static_cast<size_t> (i)].get();
                if (!v->killing() && v->countLevel() > 0.0f)
                    ++soundingAtPeriodStart;
            });
            // the start ledger: configured from the quality here (it changes only when the quality does), one tick per control period before the voices tick
            {
                const int budget = spectralStartBudget.load (std::memory_order_relaxed);
                spectralScratch->start.configure (spectralStartOn.load (std::memory_order_relaxed), budget > 0 ? budget : spectral::kStartBudgetUs);
            }
            spectralScratch->startCache = spectralStartCacheOn.load (std::memory_order_relaxed);
            spectralScratch->start.beginTick();
            controlTick();
            // the voices rebound to the set of this block in the tick: the set pinned before it is no longer referenced (it stayed pinned for the voices that were bound to it until now)
            contentHold[contentHeldSlot ^ 1].store (nullptr, std::memory_order_release);
            // everything below that reads only parameters (and the global modulation) is redone only when one of them changed since the last period
            const uint32_t mask = effectiveFxMask (fxMask.load (std::memory_order_relaxed), base[fxReverbForce] >= 0.5f);
            // the FX budget: admitted again only when the snapshot of the parameters (the effect configuration), the mask or the budget changed; the chains see a changed result like a changed parameter
            const int budgetPct = fxBudgetPct.load (std::memory_order_relaxed);
            bool budgetChanged = false;
            if (snapEpoch != budgetEpoch || mask != lastFxMask || budgetPct != lastFxBudgetPct)
            {
                budgetEpoch = snapEpoch;
                lastFxBudgetPct = budgetPct;
                const own::FxBudgetResult now = own::FxBudget::evaluate (base.data(), mask, budgetPct);
                budgetChanged = now != fxBudget;
                fxBudget = now;
                if (budgetChanged)
                {
                    budgetStatus[0].store (now.admittedCenti, std::memory_order_relaxed);
                    budgetStatus[1].store (now.requestedCenti, std::memory_order_relaxed);
                    budgetStatus[2].store (static_cast<int> (now.overModules), std::memory_order_relaxed);
                    budgetStatus[3].store (static_cast<int> (now.overSlots), std::memory_order_relaxed);
                    for (auto& f : fx)
                        f.setOverBudget (now.overModules, now.overSlots);
                }
            }
            const bool inputsSame = !blockDirty && gnumTouched == 0 && mask == lastFxMask && !budgetChanged;
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
            frame.makeup = fxMakeup.load (std::memory_order_relaxed);
            frame.lowCost = (globals.economy & kEconomyMath) != 0;
            frame.distOs2 = (globals.economy & kEconomyWarp) == 0;
            frame.scalarLoops = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingVectorFilters) != 0;
            frame.muteSkip = (globals.economy & kEconomyControl) != 0 && (savingsOff.load (std::memory_order_relaxed) & kOwnSavingFxMute) == 0;
            const bool latencyKey = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingLatencyKey) == 0;
            for (auto& f : fx)
            {
                f.setLatencyKey (latencyKey);
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
            // the latency of the racks and the delays that line the paths up (document 06, 11): it changes only with the mixer settings, a chain that was configured in this period, or a latency
            // that the modules of a settled chain report differently (FxChain::configuredNow compares the reported latencies of the rack)
            if (!(mixSame && latCached && !fx[0].configuredNow() && !fx[1].configuredNow() && !fx[2].configuredNow()))
            {
                statLatencyPlans.store (statLatencyPlans.load (std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
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
        auto countSounding = [&] () -> int
        {
            int cnt = 0;
            forEachActive ([&] (int i)
            {
                const Voice* v = voices[static_cast<size_t> (i)].get();
                if (!v->killing() && v->countLevel() > 0.0f)
                    ++cnt;
            });
            return cnt;
        };
        int soundingBeforeMid = 0;
        bool midNoteOn = false;
        if (eventGrid)
        {
            // tier low: an event takes effect at the start of the control period that contains its frame (up to 0.73 ms early), so a block is never cut by events
            while (ei < numEvents && events[ei].frame < end)
                handleEvent (events[ei++]);
        }
        else
        {
            if (controlPos != 0 && ei < numEvents && events[ei].frame <= pos)
                soundingBeforeMid = countSounding();
            while (ei < numEvents && events[ei].frame <= pos)
            {
                if (controlPos != 0 && events[ei].type == Event::NoteOn)
                    midNoteOn = true;
                handleEvent (events[ei++]);
            }
            if (ei < numEvents)
                end = std::min (end, std::max (events[ei].frame, pos + 1));
        }
        const int n = end - pos;
        auto updateVcg = [&] (int soundingBefore, int stepSamples)
        {
            voiceCountGainPrev = voiceCountGainCur;
            const float alpha = voiceCountAlpha.load (std::memory_order_relaxed);
            if (alpha <= 0.0f)
            {
                voiceCountGainCur = 1.0f;
                voiceCountGainDb = 0.0f;
            }
            else
            {
                const MasterIdx& mIdx = paramIndex().master;
                const bool monoOrLegato = (base[static_cast<size_t> (mIdx.polyphony)] <= 1.0f) || (base[static_cast<size_t> (mIdx.legato)] >= 0.5f);
                float s1 = 0.0f, s2 = 0.0f;
                int counted = 0;
                forEachActive ([&] (int i)
                {
                    const Voice* v = voices[static_cast<size_t> (i)].get();
                    if (v->killing())
                        return;
                    const float a = v->countLevel();
                    if (a > 0.0f)
                    {
                        ++counted;
                        s1 += a;
                        s2 += a * a;
                    }
                });
                float n_eff = 1.0f;
                if (!monoOrLegato && counted > 1 && s2 >= 1e-12f)
                {
                    n_eff = (s1 * s1) / s2;
                    if (n_eff < 1.0f)
                        n_eff = 1.0f;
                }
                float T = 1.0f;
                float targetDb = 0.0f;
                const bool vcgCache = (savingsOff.load (std::memory_order_relaxed) & kOwnSavingVcgCache) == 0;
                if (n_eff <= 1.0f)
                {
                    T = 1.0f;
                    targetDb = 0.0f;
                }
                else
                {
                    // T and targetDb are functions of n_eff and alpha alone, and those are the same period after period while the voices sound: the two libm calls are made when either changed
                    uint32_t nBits, aBits;
                    std::memcpy (&nBits, &n_eff, sizeof nBits);
                    std::memcpy (&aBits, &alpha, sizeof aBits);
                    if (vcgCache && vcgTargetOk && vcgKeyNeff == nBits && vcgKeyAlpha == aBits)
                    {
                        T = vcgT;
                        targetDb = vcgTargetDb;
                    }
                    else
                    {
                        T = std::pow (n_eff, -alpha);
                        targetDb = -alpha * (20.0f * std::log10 (n_eff));
                        vcgKeyNeff = nBits;
                        vcgKeyAlpha = aBits;
                        vcgT = T;
                        vcgTargetDb = targetDb;
                        vcgTargetOk = true;
                    }
                }
                if (soundingBefore == 0)
                {
                    voiceCountGainCur = T;
                    voiceCountGainPrev = T;
                    voiceCountGainDb = targetDb;
                }
                else
                {
                    const float dtMs = (static_cast<float> (stepSamples) / static_cast<float> (sampleRate)) * 1000.0f;
                    const float maxDownDb = 3.0f * dtMs;
                    const float maxUpDb = 0.15f * dtMs;
                    if (targetDb < voiceCountGainDb)
                        voiceCountGainDb = std::max (voiceCountGainDb - maxDownDb, targetDb);
                    else if (targetDb > voiceCountGainDb)
                        voiceCountGainDb = std::min (voiceCountGainDb + maxUpDb, targetDb);
                    // the third call: the gain of the stepped level, the same value once the level has reached its target
                    uint32_t dBits;
                    std::memcpy (&dBits, &voiceCountGainDb, sizeof dBits);
                    if (vcgCache && vcgGainOk && vcgKeyDb == dBits)
                        voiceCountGainCur = vcgDbGain;
                    else
                    {
                        voiceCountGainCur = std::pow (10.0f, voiceCountGainDb * 0.05f);
                        vcgKeyDb = dBits;
                        vcgDbGain = voiceCountGainCur;
                        vcgGainOk = true;
                    }
                }
                if (std::fabs (voiceCountGainCur - T) < 1e-6f)
                {
                    voiceCountGainCur = T;
                    voiceCountGainDb = targetDb;
                }
            }
            lastVoiceCountGain.store (voiceCountGainCur, std::memory_order_relaxed);
        };
        if (controlPos == 0)
        {
            updateVcg (soundingAtPeriodStart, kControlPeriod);
            vcgRampStart = 0;
            vcgRampLen = kControlPeriod;
        }
        else if (midNoteOn)
        {
            const float curFrac = clamp01 (static_cast<float> (controlPos - vcgRampStart) / static_cast<float> (std::max (1, vcgRampLen)));
            const float curG = lerp (voiceCountGainPrev, voiceCountGainCur, curFrac);
            voiceCountGainCur = voiceCountGainPrev = curG;
            voiceCountGainDb = 20.0f * std::log10 (std::max (curG, 1.0e-6f));
            // the step and the ramp run to the end of the control period, not to the end of this segment: the segment ends at the next event or at the end of the host block, and the gain
            // must not depend on the host block size
            const int toPeriodEnd = std::max (1, kControlPeriod - controlPos);
            updateVcg (soundingBeforeMid, toPeriodEnd);
            vcgRampStart = controlPos;
            vcgRampLen = toPeriodEnd;
        }

        globals.mixerSoloMask = computeEffectiveSoloMask (mixerSoloMask.load (std::memory_order_relaxed), base.data(), pi);
        globals.mixerMuteMask = mixerMuteMask.load (std::memory_order_relaxed);
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
        if (sourceMeters)
        {
            for (int s = 0; s < 7; ++s)
            {
                std::fill (stripMixL[s], stripMixL[s] + n, 0.0f);
                std::fill (stripMixR[s], stripMixR[s] + n, 0.0f);
            }
            mo.meterL = stripMixL;
            mo.meterR = stripMixR;
        }
        forEachActive ([&] (int i) { voices[static_cast<size_t> (i)]->render (mo, n, controlPos); });
        addGhosts (n);
        if (voiceCountGainPrev != 1.0f || voiceCountGainCur != 1.0f)
        {
            float gRamp[kControlPeriod];
            if (vcgRampStart == 0 && vcgRampLen == kControlPeriod)
            {
                const float invPeriod = 1.0f / static_cast<float> (kControlPeriod);
                for (int i = 0; i < n; ++i)
                    gRamp[i] = lerp (voiceCountGainPrev, voiceCountGainCur, static_cast<float> (controlPos + i + 1) * invPeriod);
            }
            else
            {
                const float invLen = 1.0f / static_cast<float> (vcgRampLen);
                for (int i = 0; i < n; ++i)
                    gRamp[i] = lerp (voiceCountGainPrev, voiceCountGainCur, clamp01 (static_cast<float> (controlPos - vcgRampStart + i + 1) * invLen));
            }
            for (int t = 0; t < 4; ++t)
            {
                simd::mulRamp (mixL[t], gRamp, n);
                simd::mulRamp (mixR[t], gRamp, n);
            }
        }
        if (sourceMeters)
            for (int s = 0; s < 7; ++s)
            {
                const auto peaks = peak::stereoPeaks (stripMixL[s], stripMixR[s], n);
                stereoBlock[0][s] = std::max (stereoBlock[0][s], peaks.left);
                stereoBlock[1][s] = std::max (stereoBlock[1][s], peaks.right);
                stripBlockPeak[s] = std::max (stripBlockPeak[s], std::max (peaks.left, peaks.right));
            }
        for (int b = 1; b <= 2; ++b)   // Bus 1, Bus 2: what the voices send into them, before the rack of the bus
        {
            const float pk = peak::peakAbsPair (mixL[b], mixR[b], n);
            stripBlockPeak[6 + b] = std::max (stripBlockPeak[6 + b], pk);
        }
        dlVoices.process (mixL[0], mixR[0], n);
        dlDirect.process (mixL[3], mixR[3], n);
        dlBusIn[0].process (mixL[1], mixR[1], n); // a bus that is fed by the other waits for what the first sends into it
        dlBusIn[1].process (mixL[2], mixR[2], n);
        auto runBus = [&] (int b) // b = 1, 2
        {
            const int busStrip = 6 + b; // b=1 -> strip 7, b=2 -> strip 8
            const bool busMuted = (globals.mixerMuteMask & (1u << busStrip)) != 0;
            const bool busSolo = (globals.mixerSoloMask & (1u << busStrip)) != 0;
            const bool anySolo = (globals.mixerSoloMask & 0x1ffu) != 0;

            float busSoloGain = 1.0f;
            if (busMuted)
            {
                busSoloGain = 0.0f;
            }
            else if (anySolo)
            {
                bool busHasActiveSolo = busSolo;
                if (!busHasActiveSolo)
                {
                    const uint32_t solo = globals.mixerSoloMask;
                    const uint32_t mute = globals.mixerMuteMask;

                    const auto sendVal = [&] (int p) { return (p >= 0 && base[static_cast<size_t> (p)] > 0.0f); };
                    const auto destVal = [&] (int p) { return p >= 0 ? static_cast<int> (base[static_cast<size_t> (p)] + 0.5f) : 0; };

                    // Mute flags
                    const bool f0Muted = (mute & (1u << 5)) != 0;
                    const bool f1Muted = (mute & (1u << 6)) != 0;
                    const bool b1Muted = (mute & (1u << 7)) != 0;
                    const bool b2Muted = (mute & (1u << 8)) != 0;

                    // Which sources are soloed?
                    const bool subSolo = (solo & (1u << 0)) != 0;
                    const bool oscSolo[3] = { (solo & (1u << 1)) != 0, (solo & (1u << 2)) != 0, (solo & (1u << 3)) != 0 };
                    const bool noiseSolo = (solo & (1u << 4)) != 0;

                    auto feedsFilter = [] (int dest, int f) { return dest == f || dest == 2 || dest == 5; };

                    bool f1Reached = (solo & (1u << 5)) != 0;
                    bool f2Reached = (solo & (1u << 6)) != 0;

                    if (subSolo)
                    {
                        const int d = destVal (pi.sub.destination);
                        if (feedsFilter (d, 0)) f1Reached = true;
                        if (feedsFilter (d, 1)) f2Reached = true;
                    }
                    for (int o = 0; o < 3; ++o)
                    {
                        if (oscSolo[o])
                        {
                            const int d = destVal (pi.osc[o].destination);
                            if (feedsFilter (d, 0)) f1Reached = true;
                            if (feedsFilter (d, 1)) f2Reached = true;
                        }
                    }
                    if (noiseSolo)
                    {
                        const int d = destVal (pi.noise.destination);
                        if (feedsFilter (d, 0)) f1Reached = true;
                        if (feedsFilter (d, 1)) f2Reached = true;
                    }

                    // Series filter routing propagation
                    const int fRouting = destVal (pi.filterRouting);
                    if (fRouting == 1 && f1Reached && !f0Muted)
                        f2Reached = true;
                    else if (fRouting == 2 && f2Reached && !f1Muted)
                        f1Reached = true;

                    if (b == 1)
                    {
                        // Direct source sends to Bus 1
                        if (subSolo && sendVal (pi.sub.bus1Send)) busHasActiveSolo = true;
                        for (int o = 0; o < 3 && !busHasActiveSolo; ++o)
                            if (oscSolo[o] && sendVal (pi.osc[o].bus1Send)) busHasActiveSolo = true;
                        if (!busHasActiveSolo && noiseSolo && sendVal (pi.noise.bus1Send)) busHasActiveSolo = true;

                        // Filter sends to Bus 1
                        if (!busHasActiveSolo && f1Reached && !f0Muted && sendVal (pi.filter[0].bus1Send)) busHasActiveSolo = true;
                        if (!busHasActiveSolo && f2Reached && !f1Muted && sendVal (pi.filter[1].bus1Send)) busHasActiveSolo = true;

                        // Other bus: Bus 2 feeds Bus 1
                        if (!busHasActiveSolo && busDest[1] == 2 && busDest[0] != 2 && !b2Muted)
                        {
                            if (solo & (1u << 8)) busHasActiveSolo = true;
                            if (!busHasActiveSolo && subSolo && sendVal (pi.sub.bus2Send)) busHasActiveSolo = true;
                            for (int o = 0; o < 3 && !busHasActiveSolo; ++o)
                                if (oscSolo[o] && sendVal (pi.osc[o].bus2Send)) busHasActiveSolo = true;
                            if (!busHasActiveSolo && noiseSolo && sendVal (pi.noise.bus2Send)) busHasActiveSolo = true;
                            if (!busHasActiveSolo && f1Reached && !f0Muted && sendVal (pi.filter[0].bus2Send)) busHasActiveSolo = true;
                            if (!busHasActiveSolo && f2Reached && !f1Muted && sendVal (pi.filter[1].bus2Send)) busHasActiveSolo = true;
                        }

                        // Upstream: Bus 1 feeds Bus 2 and Bus 2 is soloed
                        if (!busHasActiveSolo && busDest[0] == 2 && (solo & (1u << 8)))
                            busHasActiveSolo = true;
                    }
                    else
                    {
                        // Direct source sends to Bus 2
                        if (subSolo && sendVal (pi.sub.bus2Send)) busHasActiveSolo = true;
                        for (int o = 0; o < 3 && !busHasActiveSolo; ++o)
                            if (oscSolo[o] && sendVal (pi.osc[o].bus2Send)) busHasActiveSolo = true;
                        if (!busHasActiveSolo && noiseSolo && sendVal (pi.noise.bus2Send)) busHasActiveSolo = true;

                        // Filter sends to Bus 2
                        if (!busHasActiveSolo && f1Reached && !f0Muted && sendVal (pi.filter[0].bus2Send)) busHasActiveSolo = true;
                        if (!busHasActiveSolo && f2Reached && !f1Muted && sendVal (pi.filter[1].bus2Send)) busHasActiveSolo = true;

                        // Other bus: Bus 1 feeds Bus 2
                        if (!busHasActiveSolo && busDest[0] == 2 && !b1Muted)
                        {
                            if (solo & (1u << 7)) busHasActiveSolo = true;
                            if (!busHasActiveSolo && subSolo && sendVal (pi.sub.bus1Send)) busHasActiveSolo = true;
                            for (int o = 0; o < 3 && !busHasActiveSolo; ++o)
                                if (oscSolo[o] && sendVal (pi.osc[o].bus1Send)) busHasActiveSolo = true;
                            if (!busHasActiveSolo && noiseSolo && sendVal (pi.noise.bus1Send)) busHasActiveSolo = true;
                            if (!busHasActiveSolo && f1Reached && !f0Muted && sendVal (pi.filter[0].bus1Send)) busHasActiveSolo = true;
                            if (!busHasActiveSolo && f2Reached && !f1Muted && sendVal (pi.filter[1].bus1Send)) busHasActiveSolo = true;
                        }

                        // Upstream: Bus 2 feeds Bus 1 and Bus 1 is soloed
                        if (!busHasActiveSolo && busDest[1] == 2 && busDest[0] != 2 && (solo & (1u << 7)))
                            busHasActiveSolo = true;
                    }
                }
                busSoloGain = busHasActiveSolo ? 1.0f : 0.0f;
            }

            // the makeup of the bypassed effects of this bus (Quality::fxMakeup): the gain that makes what the bus adds to Main as loud as the active effects would have, given how much
            // of the bus input is a copy of what Main carries (a send of a source that also plays in Main): see makeupOfBus
            float mkGain = 1.0f, mkPrev = 1.0f;
            if (!busBypass[b - 1])
            {
                makeupOfBus (b, n, mkPrev, mkGain);
                fx[b].process (mixL[b], mixR[b], n, controlPos);
            }
            float outL[kControlPeriod], outR[kControlPeriod];
            for (int i = 0; i < n; ++i)
            {
                const float g = lerp (busGainPrev[b], busGain[b], static_cast<float> (controlPos + i + 1) * (1.0f / static_cast<float> (kControlPeriod)));
                const float mk = mkGain == 1.0f && mkPrev == 1.0f ? 1.0f : lerp (mkPrev, mkGain, static_cast<float> (i + 1) / static_cast<float> (n));
                outL[i] = mixL[b][i] * g * mk * busSoloGain;
                outR[i] = mixR[b][i] * g * mk * busSoloGain;
            }
            const auto busPeaks = peak::stereoPeaks (outL, outR, n);
            stereoBlock[0][busStrip] = std::max (stereoBlock[0][busStrip], busPeaks.left);
            stereoBlock[1][busStrip] = std::max (stereoBlock[1][busStrip], busPeaks.right);
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
        float monoRight[kControlPeriod];   // n <= kControlPeriod: the right channel of a mono output, dropped after the fold below
        float* const chunkRight = right != nullptr ? right + pos : monoRight;
        std::copy (mixL[0], mixL[0] + n, left + pos);
        std::copy (mixR[0], mixR[0] + n, chunkRight);
        {
            // master volume (smoothed), stereo routing, Direct (past every rack and the routing, scaled by its own level), the master volume on everything that reaches the output, the Direct sum included
            // (it did not before: a Direct source ignored master.volume), and the safety stage: dsp/master_stage.h (the same samples as the per sample loop it replaced)
            master::Params mp;
            mp.routeCos = routeCos;
            mp.routeSin = routeSin;
            mp.directPrev = directGainPrev;
            mp.directNow = directGain;
            mp.controlPos = controlPos;
            mp.clipped = &blockClipped;
            master::stage (left + pos, chunkRight, mixL[3], mixR[3], end - pos, mp, smoothedGain);
        }
        const bool masterMuted = (globals.mixerMuteMask & (1u << 9)) != 0;
        if (masterMuted)
        {
            std::fill (left + pos, left + end, 0.0f);
            std::fill (chunkRight, chunkRight + (end - pos), 0.0f);
        }
        // Master L/R in one pass over the output, before any mono fold.
        const auto outputPeak = peak::stereoPeaks (left + pos, chunkRight, n);
        masterBlockPeakL = std::max (masterBlockPeakL, outputPeak.left);
        masterBlockPeakR = std::max (masterBlockPeakR, outputPeak.right);
        stereoBlock[0][LiveValues::kMasterStrip] = masterBlockPeakL;
        stereoBlock[1][LiveValues::kMasterStrip] = masterBlockPeakR;
        if (right == nullptr)
            for (int i = 0; i < n; ++i)
                left[pos + i] = 0.5f * (left[pos + i] + monoRight[i]);
        controlPos = (controlPos + n) % kControlPeriod;
        pos = end;
    }
    if (blockClipped)
        clipFlag.store (true, std::memory_order_relaxed);
    while (ei < numEvents)
        handleEvent (events[ei++]);

    killExcess(); // the overflow of the Kill mode is resolved before the count is reported
    int count = 0;
    forEachActive ([&] (int i) { count += voices[static_cast<size_t> (i)]->killing() ? 0 : 1; }); // a voice in its 50 ms kill fade is on its way out: the count stays within the polyphony
    activeCount.store (count, std::memory_order_relaxed);
    bool writeNow = true;
    if ((savingsOff.load (std::memory_order_relaxed) & kOwnSavingReadoutCadence) == 0)
    {
        int d = readoutDemand.load (std::memory_order_relaxed);
        if (d > 0)
            readoutDemand.compare_exchange_strong (d, d - 1, std::memory_order_relaxed); // (a poll since the load stays whole)
        else
            writeNow = false;
    }
    if (writeNow)
    {
        if (readoutIdle)
        {
            // blocks passed without a readout: the meters start again from nothing (their memory would show a peak from before the pause)
            std::fill (std::begin (stripShown), std::end (stripShown), 0.0f);
            masterShownL = masterShownR = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < LiveValues::kMaxStrips; ++s)
                {
                    stereoShown[ch][s] = stereoHold[ch][s] = 0.0f;
                    stereoHoldFrames[ch][s] = stereoClipFrames[ch][s] = 0;
                }
        }
        readoutIdle = false;
        publishStats();
        writeReadout();
    }
    else
    {
        readoutIdle = true;
        publishMissing();
    }
    blockCounter.fetch_add (1, std::memory_order_relaxed);
}

/** The content that no voice needs yet but the patch asks for, as the bits of missingContent (): a user table that is not there, the impulse of Convolve, the sample of the noise source. */
unsigned OwnEngine::contentAskedFor() const noexcept
{
    unsigned miss = 0;
    const ParamIndex& pi = paramIndex();
    const ContentSet* cs = globals.content;
    for (int o = 0; o < 3; ++o)
        if (base[static_cast<size_t> (pi.osc[o].on)] >= 0.5f && base[static_cast<size_t> (pi.osc[o].type)] < 0.5f
            && static_cast<int> (base[static_cast<size_t> (pi.osc[o].wavetable)] + 0.5f) == kWavetablePresetTable && (cs == nullptr || cs->userTable[o] == nullptr))
            miss |= 8u << o;
    if (convolveOnIndex >= 0 && base[static_cast<size_t> (convolveOnIndex)] >= 0.5f && (cs == nullptr || cs->impulse == nullptr))
        miss |= 64u;
    return miss;
}

/** missingContent () for the blocks that do not publish the statistics: it is a state of the engine that any caller may read at any time, not a display value. */
void OwnEngine::publishMissing() noexcept
{
    unsigned miss = contentAskedFor();
    forEachActive ([&] (int i) { miss |= voices[static_cast<size_t> (i)]->missingMask(); });
    missingMask.store (miss, std::memory_order_relaxed);
}

/** The snapshot of voiceStats (): the voices, the keys the events say are held, and the bug state: a voice that is not released while its key is not held and the pedal is up. */
void OwnEngine::publishStats() noexcept
{
    int active = 0, released = 0, sustaining = 0, orphans = 0;
    unsigned miss = 0;
    forEachActive ([&] (int i)
    {
        const Voice* v = voices[static_cast<size_t> (i)].get();
        miss |= v->missingMask();
        ++active;
        if (v->isReleased() || v->killing())
        {
            ++released;
            return;
        }
        ++sustaining;
        bool held = sustainDown[std::clamp (v->channel(), 0, numChannels - 1)];
        for (int k = 0; k < numPressed && !held; ++k)
            held = pressed[k].playedKey == v->playedKey() && pressed[k].channel == v->channel();
        if (!held)
            ++orphans;
    });
    miss |= contentAskedFor();
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
    readoutDemand.store (kReadoutDemandBlocks, std::memory_order_relaxed);
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
    readoutDemand.store (kReadoutDemandBlocks, std::memory_order_relaxed);
}

bool OwnEngine::copyReadout (LiveValues& out) const noexcept
{
    readoutDemand.store (kReadoutDemandBlocks, std::memory_order_relaxed);
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
    for (int s = 0; s < LiveValues::kMaxStrips; ++s)
    {
        out.stripPeakL[s] = liveStereoMeter[0][s].load (std::memory_order_relaxed);
        out.stripPeakR[s] = liveStereoMeter[1][s].load (std::memory_order_relaxed);
        out.stripHoldL[s] = liveStereoMeter[2][s].load (std::memory_order_relaxed);
        out.stripHoldR[s] = liveStereoMeter[3][s].load (std::memory_order_relaxed);
        out.stripClipL[s] = liveStereoMeter[4][s].load (std::memory_order_relaxed);
        out.stripClipR[s] = liveStereoMeter[5][s].load (std::memory_order_relaxed);
    }
    out.masterPeakL = liveMasterPeakL.load (std::memory_order_relaxed);
    out.masterPeakR = liveMasterPeakR.load (std::memory_order_relaxed);
    return true;
}

/** End of a block: the display gets the sources of the voice played last (the global sources when none sounds), the LFO cycle positions, the envelope stages and the modulated knobs. */
void OwnEngine::writeReadout() noexcept
{
    const ParamIndex& pi = paramIndex();
    // the meters of the mixer: the peak of the block on a scale of 60 dB (0 = -60 dBFS, 1 = full scale), a fast attack and a release of about 300 ms; published to the display thread through atomics (no lock)
    {
        const float release = std::exp (-static_cast<float> (std::max (lastBlockFrames, 1)) / static_cast<float> (std::max (sampleRate, 1.0) * 0.3));
        auto shownPeak = [release] (float pk, float previous)
        {
            const float level = pk > 1.0e-3f ? std::clamp (1.0f + 20.0f * std::log10 (pk) / 60.0f, 0.0f, 1.0f) : 0.0f;
            const float shown = std::max (level, previous * release);
            return shown < 0.002f ? 0.0f : shown;
        };
        for (int s = 0; s < LiveValues::kMasterStrip; ++s)
        {
            if (s < 7 && ! sourceMeters)   // not measured this block: nothing to show (the memory of the release starts again at the next measured block)
            {
                stripShown[s] = 0.0f;
                liveStripPeak[s].store (0.0f, std::memory_order_relaxed);
                continue;
            }
            stripShown[s] = shownPeak (stripBlockPeak[s], stripShown[s]);
            liveStripPeak[s].store (stripShown[s], std::memory_order_relaxed);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < LiveValues::kMaxStrips; ++s)
            {
                const float raw = s < 7 && ! sourceMeters ? 0.0f : stereoBlock[ch][s];
                stereoShown[ch][s] = s < 7 && ! sourceMeters ? 0.0f : shownPeak (raw, stereoShown[ch][s]);
                const float norm = raw > 1.0e-3f ? std::clamp (1.0f + 20.0f * std::log10 (raw) / 60.0f, 0.0f, 1.0f) : 0.0f;
                if (norm >= stereoHold[ch][s])
                {
                    stereoHold[ch][s] = norm;
                    stereoHoldFrames[ch][s] = static_cast<int> (sampleRate);
                }
                else
                {
                    stereoHoldFrames[ch][s] = std::max (0, stereoHoldFrames[ch][s] - lastBlockFrames);
                    if (stereoHoldFrames[ch][s] == 0) stereoHold[ch][s] = shownPeak (raw, stereoHold[ch][s]);
                }
                if (raw >= 1.0f) stereoClipFrames[ch][s] = static_cast<int> (2.0 * sampleRate);
                else stereoClipFrames[ch][s] = std::max (0, stereoClipFrames[ch][s] - lastBlockFrames);
                liveStereoMeter[ch][s].store (stereoShown[ch][s], std::memory_order_relaxed);
                liveStereoMeter[2 + ch][s].store (stereoHold[ch][s], std::memory_order_relaxed);
                liveStereoMeter[4 + ch][s].store (stereoClipFrames[ch][s] > 0 ? 1.0f : 0.0f, std::memory_order_relaxed);
            }
        masterShownL = shownPeak (masterBlockPeakL, masterShownL);
        masterShownR = shownPeak (masterBlockPeakR, masterShownR);
        liveMasterPeakL.store (masterShownL, std::memory_order_relaxed);
        liveMasterPeakR.store (masterShownR, std::memory_order_relaxed);
        liveStripPeak[LiveValues::kMasterStrip].store (std::max (masterShownL, masterShownR), std::memory_order_relaxed);
    }
    const Voice* newest = nullptr;
    forEachActive ([&] (int i)
    {
        const Voice* v = voices[static_cast<size_t> (i)].get();
        if (newest == nullptr || v->serial() > newest->serial())
            newest = v;
    });
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
        // the newest kReadVoices serials, newest first: one pass over the active voices. A serial the voices share is taken once, by the first voice of the pool that has it (what the passes
        // for the largest serial below the last one gave).
        const Voice* order[LiveValues::kReadVoices] = {};
        int found = 0;
        if ((savingsOff.load (std::memory_order_relaxed) & kOwnSavingReadoutCadence) != 0)
        {
            // the reference (the test of the one pass): one pass over the voices for each of the voices, the largest serial below the last one
            uint32_t bound = 0xffffffffu;
            for (; found < LiveValues::kReadVoices; ++found)
            {
                const Voice* best = nullptr;
                forEachActive ([&] (int i)
                {
                    const Voice* v = voices[static_cast<size_t> (i)].get();
                    if (v->serial() < bound && (best == nullptr || v->serial() > best->serial()))
                        best = v;
                });
                if (best == nullptr)
                    break;
                order[found] = best;
                bound = best->serial();
            }
        }
        else
        {
        forEachActive ([&] (int i)
        {
            const Voice* v = voices[static_cast<size_t> (i)].get();
            const uint32_t sv = v->serial();
            if (sv >= 0xffffffffu)
                return;
            int at = 0;
            while (at < found && order[at]->serial() > sv)
                ++at;
            if (at < found && order[at]->serial() == sv)
                return; // the serial is taken
            if (at >= LiveValues::kReadVoices)
                return;
            const int last = std::min (found, LiveValues::kReadVoices - 1);
            for (int k = last; k > at; --k)
                order[k] = order[k - 1];
            order[at] = v;
            found = std::min (found + 1, static_cast<int> (LiveValues::kReadVoices));
        });
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
