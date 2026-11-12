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

OwnEngine::OwnEngine() : fxReverbForceIndex (findParamIndex (hashKey ("reverb.force_enable")))
{
    for (int k = 0; k < 128; ++k)
        tunedNotes[k] = static_cast<float> (k);
    const auto* table = paramTable();
    for (size_t i = 0; i < kMaxParams; ++i)
        params[i].store (i < paramCount() ? table[i].def : 0.0f, std::memory_order_relaxed);
}

OwnEngine::~OwnEngine() = default;

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
    for (int i = 0; i < kPoolVoices; ++i)
    {
        voices.push_back (std::make_unique<Voice>());
        voices.back()->prepare (sampleRate);
    }
    sustainPending.assign (static_cast<size_t> (kPoolVoices), static_cast<uint8_t> (0));
    base.assign (paramCount(), 0.0f);
    gdelta.assign (paramCount(), 0.0f);
    gtouched.assign (static_cast<size_t> (kNumModSlots), 0);
    fx.prepare (sampleRate, maxBlockFrames);
    smoothedGain.reset (sampleRate, 0.02);
    prepared = true;
    reset();
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
        gdelta[static_cast<size_t> (gtouched[static_cast<size_t> (i)])] = 0.0f;
    gnumTouched = 0;
    smoothedGain.setCurrentAndTarget (dbToGain (readParam (kParamMasterVolume)) * (readParam (kParamMasterVolume) > -60.0f ? 1.0f : 0.0f));
    activeCount.store (0, std::memory_order_relaxed);
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
    return victim != nullptr ? victim : (free != nullptr ? free : pickVictim (true));
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
    chosen->start (ns, chosen->lastL(), chosen->lastR());
    if (heldVoice != nullptr)
        chosen->inheritEnvelopes (*heldVoice);
    chosen->tick (globals, static_cast<int> (std::find_if (voices.begin(), voices.end(), [&] (const auto& p) { return p.get() == chosen; }) - voices.begin()));
    lastNoteSemis = ns.tunedNote;
    haveLastNote = true;
    lastVelocity = n.velocity;
}

void OwnEngine::noteOn (const Event& e) noexcept
{
    if (e.key < 0 || e.key > 127)
        return;
    const MasterIdx& mi = paramIndex().master;
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
                    v.startKill(); // Kill mode: the released voice fades out, a fresh voice plays the recalled note
                recalled = true;
                continue;
            }
        }
        v.release (relVel);
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
                    voices[i]->release (0.0f);
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
            if (mpe && channel > 0) // the pressure of a member channel belongs to its notes (the Poly Aftertouch source) and is the Z axis
            {
                for (auto& v : voices)
                    if (v->isActive() && v->channel() == channel)
                        v->setPressure (aftertouch[channel]);
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
    killExcess();
    {
        const bool mpe = base[static_cast<size_t> (pi.master.mpeOn)] >= 0.5f;
        for (int c = 0; c < numChannels; ++c)
            effBend[c] = bendSemis[c] + (mpe && c > 0 ? bendSemis[0] : 0.0f);
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
    noteTuning.compute (tunedNotes, static_cast<int> (base[static_cast<size_t> (pi.master.tuningScale)] + 0.5f), base[static_cast<size_t> (pi.master.tuningA4)],
                        base[static_cast<size_t> (pi.master.tuningMts)] >= 0.5f);
    globals.tunedNotes = tunedNotes;
    globals.channelBendSemis = effBend;
    globals.channelModWheel = modWheel;
    globals.channelAftertouch = aftertouch;
    globals.lfoPhase = lfoPhase;
    globals.rlfoValue = rlfoValue;
    globals.maxUnison = unisonCap.load (std::memory_order_relaxed);

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
            if (routeSin != 0.0f)
            {
                const float l0 = left[i], r0 = rightOut[i];
                left[i] = l0 * routeCos + r0 * routeSin;
                rightOut[i] = r0 * routeCos + l0 * routeSin;
            }
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
}
} // namespace own
} // namespace synthyboy
