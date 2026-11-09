/*
 * SynthyBoy - minimal 8-voice synthesizer (VST3 + CLAP): thin JUCE shell around synthyboy_core
 * Copyright (C) 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "PluginProcessor.h"

#include "synthyboy/midi_events.h"
#include "ui/Editor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
/** The engine the shell was configured with (CMake SYNTHYBOY_SHELL_ENGINE: the Vital-derived engine whenever it is part of the
    build, the own engine otherwise or on request). */
std::unique_ptr<synthyboy::IEngine> createShellEngine()
{
   #if SYNTHYBOY_SHELL_ENGINE_VITAL
    if (auto vital = synthyboy::createEngine (synthyboy::EngineKind::vital))
        return vital;
   #endif
    return synthyboy::createOwnEngine();
}
} // namespace

SynthyBoyProcessor::SynthyBoyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SynthyBoyState", createParameterLayout()),
      engine (createShellEngine())
{
    caps = engine->capabilities();
    caps2 = engine->capabilities2();
    startTimerHz (20);
    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        rawParams[i] = apvts.getRawParameterValue (table[i].key);
        paramPtrs[i] = apvts.getParameter (table[i].key);
        engine->setParam (table[i].id, table[i].def);
        sentToEngine[i] = std::numeric_limits<float>::quiet_NaN(); // the first block sends everything
    }

    // Save as Default (G02): a new instance starts from the user's default patch when there is one (the host's state, if any, replaces it)
    userDir = synthyboy::userDataDirectory();
    prefs = synthyboy::loadPreferences (userDir);
    prefSilenceOnStop.store (prefs.silenceOnStop);

    // the content pipeline (WP-01): the store is set before any content reaches the engine; every document load (applyPatch) then decodes its
    // references against it
    contentStore = std::make_unique<synthyboy::UserContentStore> (userDir);
    itemLibrary = std::make_unique<synthyboy::UserLibrary> (userDir);
    if (! userDir.empty())
        contentStore->addPackRoot ((std::filesystem::path (userDir) / "packs").string()); // installed content packs: <userDir>/packs/<pack>/<file>
    engine->setContentStore (contentStore.get());
    auto values = synthyboy::ParamValues::defaults();
    synthyboy::StateContent defaultContent;
    synthyboy::ShellState defaultShell;
    if (synthyboy::loadUserDefault (userDir, values, defaultContent, &defaultShell))
    {
        applyShell (defaultShell);
        applyPatch (values, std::move (defaultContent), false);
        const std::lock_guard<std::mutex> lock (contentMutex);
        savedValues = currentValues();
        savedContent = patchContent;
    }
    else
    {
        // the preferences of a new instance (S p.314): MPE on, pitch bend also to X, Y bi-directional; a patch the user saved as default wins over them (A383)
        const auto set = [&] (const char* key, bool on) { if (on) values.plain[static_cast<size_t> (synthyboy::findParamIndex (synthyboy::findParamByKey (key)->id))] = 1.0f; };
        set ("master.mpe.on", prefs.mpeByDefault);
        set ("master.mpe.bend_to_x", prefs.mpeBendToX);
        set ("master.mpe.y_bipolar", prefs.mpeYBipolar);
        if (prefs.mpeByDefault || prefs.mpeBendToX || prefs.mpeYBipolar)
        {
            applyValues (values);
            const std::lock_guard<std::mutex> lock (contentMutex);
            savedValues = currentValues();
        }
    }
    // the default MIDI CC map (S p.325) in a new instance, unless the default patch brought a map of its own
    if (midiMap.numMappings() == 0)
        applyMapOfPresetChange (nullptr);
}

void SynthyBoyProcessor::applyMapOfPresetChange (const synthyboy::ui::MidiMapBindings* presetMidiMap)
{
    if (prefs.loadMidiMapFromPresets && presetMidiMap != nullptr && ! presetMidiMap->empty())
    {
        midiMap.setBindings (*presetMidiMap);
        return;
    }
    std::vector<std::pair<int, std::string>> bindings;
    if (synthyboy::loadDefaultMidiMap (userDir, bindings))
        midiMap.setBindings (bindings);
}

void SynthyBoyProcessor::setPreferences (const synthyboy::Preferences& newPrefs)
{
    if (newPrefs == prefs)
        return;
    prefs = newPrefs;
    prefSilenceOnStop.store (prefs.silenceOnStop);
    synthyboy::savePreferences (userDir, prefs);
    prefsRev.fetch_add (1, std::memory_order_relaxed);
}

int SynthyBoyProcessor::setMidiMapBindings (const synthyboy::ui::MidiMapBindings& bindings)
{
    const int taken = midiMap.setBindings (bindings);
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return taken;
}

bool SynthyBoyProcessor::saveDefaultMidiMap()
{
    return synthyboy::saveDefaultMidiMap (userDir, midiMap.bindings());
}

juce::String SynthyBoyProcessor::aboutText() const
{
   #ifdef SYNTHYBOY_VERSION_STRING
    const juce::String version (SYNTHYBOY_VERSION_STRING);
   #else
    const juce::String version ("dev");
   #endif
    const juce::String engineName = (caps & synthyboy::kCapVoiceControl) != 0 ? "vital DSP (AGPL-3.0)" : "own engine";
    int packs = 0; // installed content packs: the directories below <userDir>/packs
    std::error_code ec;
    if (! userDir.empty())
        for (std::filesystem::directory_iterator it ((std::filesystem::path (userDir) / "packs"), ec), end; ! ec && it != end; it.increment (ec))
            packs += it->is_directory (ec) ? 1 : 0;
    return "SynthyBoy " + version + " | built " + juce::String (__DATE__) + " | Engine: " + engineName + " | " + juce::String (synthyboy::paramCount()) + " parameters | content packs: "
           + juce::String (packs);
}

SynthyBoyProcessor::~SynthyBoyProcessor()
{
    stopTimer();
    if (engine != nullptr)
        engine->setContentStore (nullptr);
}

namespace
{
/** A node of the group tree built from the " / " hierarchy of ParamInfo::group. */
struct GroupNode
{
    std::string name;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    std::vector<std::unique_ptr<GroupNode>> children;

    GroupNode* child (const std::string& childName)
    {
        for (auto& c : children)
            if (c->name == childName)
                return c.get();
        children.push_back (std::make_unique<GroupNode>());
        children.back()->name = childName;
        return children.back().get();
    }
};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SynthyBoyProcessor::createParameterLayout()
{
    using namespace juce;

    const auto* table = synthyboy::paramTable();

    // Parameters are collected per group first (the groups are the CLAP "module" path and the sections of the generic editor).
    // Every parameter stays visible: the generic editor shows the 64 modulation slots as 64 small sections.
    GroupNode root;

    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const synthyboy::ParamInfo& info = table[i];
        const ParameterID id { info.key, 1 };

        std::unique_ptr<RangedAudioParameter> parameter;
        if (info.scale == synthyboy::Scale::stepped)
        {
            StringArray choices;
            for (size_t k = 0; k < synthyboy::choiceCount (info); ++k)
                choices.add (synthyboy::choiceName (info, k));

            parameter = std::make_unique<AudioParameterChoice> (id, info.name, choices, static_cast<int> (info.def));
        }
        else
        {
            // display text comes from the core, so a hardware knob and the plugin GUI show the same string
            // (fixed precision: value -> text -> value -> text is stable, clap-validator param-conversions)
            parameter = std::make_unique<AudioParameterFloat> (
                id, info.name,
                NormalisableRange<float> (info.min, info.max, info.interval, info.skew), info.def,
                AudioParameterFloatAttributes().withLabel (synthyboy::unitLabel (info.unit))
                    .withStringFromValueFunction ([&info] (float v, int)
                    {
                        char text[64];
                        synthyboy::valueToText (info, v, text, sizeof text);
                        return String (text);
                    })
                    .withValueFromStringFunction ([&info] (const String& t)
                    {
                        float v = info.def;
                        synthyboy::textToValue (info, t.toRawUTF8(), v);
                        return v;
                    }));
        }

        // the group path "Osc 1 / Unison" -> node Osc 1 -> node Unison
        GroupNode* node = &root;
        const std::string path = info.group;
        size_t pos = 0;
        while (true)
        {
            const size_t next = path.find (" / ", pos);
            node = node->child (path.substr (pos, next == std::string::npos ? std::string::npos : next - pos));
            if (next == std::string::npos)
                break;
            pos = next + 3;
        }
        node->parameters.push_back (std::move (parameter));
    }

    // JUCE wants the groups as finished objects: convert the tree bottom up (parameters before sub groups, as listed)
    struct Builder
    {
        static std::unique_ptr<AudioProcessorParameterGroup> build (GroupNode& node, const std::string& path)
        {
            auto group = std::make_unique<AudioProcessorParameterGroup> (path, node.name, " | ");
            for (auto& p : node.parameters)
                group->addChild (std::move (p));
            for (auto& c : node.children)
                group->addChild (build (*c, path + "/" + c->name));
            return group;
        }
    };

    AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& top : root.children)
        layout.add (Builder::build (*top, top->name));
    return layout;
}

void SynthyBoyProcessor::timerCallback()
{
    if (! engine->needsPrepare())
        return;
    // an option that only a prepare () can apply (4x oversampling resizes buffers): sounding notes end, the rest of the sound is kept
    const juce::ScopedLock lock (getCallbackLock());
    prepareToPlay (preparedSampleRate, preparedBlockSize);
}

void SynthyBoyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;
    events.resize (static_cast<size_t> (maxEventsPerBlock));

    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const float v = rawParams[i]->load();
        engine->setParam (table[i].id, v);
        sentToEngine[i] = v;
    }

    // the quality tier of this build (CMake SYNTHYBOY_DEFAULT_TIER, high on desktop); a tier only caps, it never changes parameters
    // Live playback never waits for a helper thread of the engine; an offline bounce (isNonRealtime) is deterministic (Quality::realtime).
    auto quality = synthyboy::qualityForTier (synthyboy::kDefaultTier);
    quality.realtime = ! isNonRealtime();
    engine->setUltraRender (prefs.ultraRender && isNonRealtime()); // a bounce at 4x oversampling (S p.315, A385)
    stopWatch.reset();
    engine->prepare (sampleRate, samplesPerBlock, quality);
    setLatencySamples (engine->latencySamples());
}

void SynthyBoyProcessor::setNonRealtime (bool isNonRealtime) noexcept
{
    juce::AudioProcessor::setNonRealtime (isNonRealtime);
    auto quality = synthyboy::qualityForTier (synthyboy::kDefaultTier);
    quality.realtime = ! isNonRealtime;
    engine->setQuality (quality);
    engine->setUltraRender (prefs.ultraRender && isNonRealtime); // reported by needsPrepare (): the 20 Hz timer prepares the engine again

}

bool SynthyBoyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return layouts.inputBuses.isEmpty()
        && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void SynthyBoyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // parameters: one atomic load and one compare per parameter and block; the engine is told only about the ones that changed
    // (no allocation, no lock: rawParams are the atomics of the JUCE parameters)
    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const float v = rawParams[i]->load (std::memory_order_relaxed);
        if (! std::equal_to<float>() (v, sentToEngine[i])) // exact comparison on purpose; the NaN of the first block never equals
        {
            engine->setParam (table[i].id, v);
            sentToEngine[i] = v;
        }
    }

    // notes played on the on-screen keyboard join the host's MIDI input (MidiKeyboardState takes its own lock for a few instructions)
    keyboard.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    // UI injected MIDI messages (pitch wheel, mod wheel, CC 64 sustain, etc.)
    {
        std::lock_guard<std::mutex> lock (uiMidiMutex);
        if (! uiMidiBuffer.isEmpty())
        {
            midiMessages.addEvents (uiMidiBuffer, 0, numSamples, 0);
            uiMidiBuffer.clear();
        }
    }

    // MIDI -> sample accurate events (fixed capacity, overflow is dropped and counted)
    const int capacity = static_cast<int> (events.size());
    int numEvents = 0;

    for (const auto metadata : midiMessages)
    {
        if (metadata.numBytes > 3) // only channel messages matter, and short ones do not allocate
            continue;

        const auto message = metadata.getMessage();

        // MIDI learn / mapped controllers (G07): a CC that is in the table sets its parameter (host notified, the engine told right here)
        if (metadata.numBytes == 3 && (message.getRawData()[0] & 0xF0) == 0xB0)
        {
            const auto hit = midiMap.handleCc (message.getRawData()[1] & 0x7F, message.getRawData()[2] & 0x7F);
            if (hit.apply)
            {
                const size_t index = static_cast<size_t> (hit.paramIndex);
                if (auto* parameter = paramPtrs[index])
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (hit.plain));
                rawParams[index]->store (hit.plain); // exact value, not the normalised round trip
                engine->setParam (table[index].id, hit.plain);
                sentToEngine[index] = hit.plain;
            }
        }

        synthyboy::Event e;
        // note on/off (with release velocity), pitch bend, mod wheel, CC 64 sustain pedal, CC 74 slide, channel and poly pressure,
        // all notes off: core/include/synthyboy/midi_events.h (shared with the CLAP shell)
        if (! synthyboy::translateMidiMessage (message.getRawData(), message.getRawDataSize(),
                                               std::clamp (metadata.samplePosition, 0, numSamples - 1), e))
            continue;

        if (e.type == synthyboy::Event::ModWheel)
            modWheelPosition.store (e.value, std::memory_order_relaxed);
        // the last note (share / drag-out of the last note, Resample): key, velocity, when it started and ended (running sample count)
        if (e.type == synthyboy::Event::NoteOn)
        {
            lastKey.store (e.key, std::memory_order_relaxed);
            lastVelocity.store (e.value, std::memory_order_relaxed);
            lastOnSample.store (samplesDone.load (std::memory_order_relaxed) + e.frame, std::memory_order_relaxed);
            lastOffSample.store (-1, std::memory_order_relaxed);
        }
        else if (e.type == synthyboy::Event::NoteOff && e.key == lastKey.load (std::memory_order_relaxed))
            lastOffSample.store (samplesDone.load (std::memory_order_relaxed) + e.frame, std::memory_order_relaxed);
        if (numEvents < capacity)
            events[static_cast<size_t> (numEvents++)] = e;
        else
            droppedEvents.fetch_add (1);
    }

    {
        // host transport (tempo sync, beat units, LFO HOST mode)
        synthyboy::TransportInfo transport;
        bool transportKnown = false;
        if (auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
            {
                if (const auto bpm = position->getBpm())
                    transport.bpm = *bpm > 0.0 ? *bpm : 120.0;
                if (const auto ppq = position->getPpqPosition())
                    transport.songPosBeats = *ppq;
                transport.playing = position->getIsPlaying();
                transportKnown = true;
                tempo.store (transport.bpm, std::memory_order_relaxed);
            }
        // the host stopped (running -> stopped): notes end and the effect tails are cleared (S p.315, A381). Before the block, so the events of
        // this block (a note played with the transport stopped) start a clean engine.
        if (stopWatch.update (transportKnown, transport.playing) && prefSilenceOnStop.load (std::memory_order_relaxed))
            engine->silence();
        engine->setTransport (transport);
    }

    const auto processStart = juce::Time::getHighResolutionTicks();
    engine->process (events.data(), numEvents, buffer.getWritePointer (0),
                     numChannels > 1 ? buffer.getWritePointer (1) : nullptr, numSamples);
    {
        // CPU: the time the engine needed for this block against the time the block lasts, smoothed over a few blocks
        const double spent = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - processStart);
        const double rate = getSampleRate();
        const double budget = rate > 0.0 ? static_cast<double> (numSamples) / rate : 0.0;
        if (budget > 0.0)
        {
            const float load = static_cast<float> (spent / budget);
            cpu.store (cpu.load (std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
        }
        voiceCount.store (engine->activeVoices(), std::memory_order_relaxed);

        // the scope ring: the mono mix of the block
        uint32_t w = scopeWrite.load (std::memory_order_relaxed);
        const float* l = buffer.getReadPointer (0);
        const float* r = numChannels > 1 ? buffer.getReadPointer (1) : l;
        for (int i = 0; i < numSamples; ++i)
        {
            scopeRing[w % synthyboy::ui::UiHost::kScopeSamples].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);
            ++w;
        }
        scopeWrite.store (w, std::memory_order_release);
    }

    samplesDone.fetch_add (numSamples, std::memory_order_relaxed);

    const float magL = buffer.getMagnitude (0, 0, numSamples);
    const float magR = numChannels > 1 ? buffer.getMagnitude (1, 0, numSamples) : magL;
    peakL.store (magL, std::memory_order_relaxed);
    peakR.store (magR, std::memory_order_relaxed);

    // Bode's latency (the engine delays every other path to match): tell the host when it changes
    const int latency = engine->latencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

// ---- offline render, last note, share hook (GROUPD WP-15) ----------------------------------------------------------------------------------

bool SynthyBoyProcessor::renderOffline (const synthyboy::OfflineRequest& request, synthyboy::OfflineAudio& out)
{
    if (engine == nullptr)
        return false;
    // the values of the parameter tree: the engine only gets them with the next audio block, a render right after a gesture must hear them
    auto values = std::make_unique<synthyboy::ParamValues> (currentValues());
    synthyboy::OfflineRequest copy = request;
    if (copy.values == nullptr)
        copy.values = values.get();
    synthyboy::StateContent content;
    {
        const std::lock_guard<std::mutex> lock (contentMutex);
        content = patchContent;
    }
    synthyboy::EngineKind kind = synthyboy::EngineKind::own;
#if SYNTHYBOY_SHELL_ENGINE_VITAL
    kind = synthyboy::EngineKind::vital;
#endif
    return synthyboy::renderOfflineAny (*engine, kind, &content, contentStore.get(), copy, out);
}

synthyboy::ui::UiHost::LastNote SynthyBoyProcessor::lastNote() const
{
    LastNote note;
    const int key = lastKey.load (std::memory_order_relaxed);
    if (key < 0)
        return note;
    note.valid = true;
    note.key = key;
    note.velocity = lastVelocity.load (std::memory_order_relaxed);
    const int64_t on = lastOnSample.load (std::memory_order_relaxed), off = lastOffSample.load (std::memory_order_relaxed);
    const int64_t end = off >= 0 ? off : samplesDone.load (std::memory_order_relaxed);
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    note.heldSeconds = std::max (0.05, static_cast<double> (end - on) / rate);
    return note;
}

bool SynthyBoyProcessor::shareFile (const juce::File& file, const juce::String& title, std::function<void (bool)> done)
{
    if (! file.existsAsFile())
        return false;
    if (shareHandler)
    {
        const bool ok = shareHandler (file, title);
        if (done)
            done (ok);
        return ok;
    }
#if JUCE_IOS || JUCE_ANDROID
    // the platform share sheet (S01-09 on mobile: the rendered note goes to Files, Messages, a DAW ...)
    juce::ContentSharer::getInstance()->shareFiles (juce::Array<juce::URL> { juce::URL (file) }, [done] (bool success, const juce::String&) { if (done) done (success); });
    return true;
#else
    file.revealToUser(); // desktop: the file in the file manager, from where it can be dragged
    if (done)
        done (true);
    return true;
#endif
}

double SynthyBoyProcessor::getTailLengthSeconds() const
{
    // the longest release of the envelopes plus the tails of the effects that are on (from the current parameter values)
    const auto value = [this] (const char* key)
    {
        const auto* info = synthyboy::findParamByKey (key);
        const auto index = info != nullptr ? synthyboy::findParamIndex (info->id) : -1;
        return index >= 0 && rawParams[static_cast<size_t> (index)] != nullptr ? static_cast<double> (rawParams[static_cast<size_t> (index)]->load()) : 0.0;
    };
    double release = 0.0;
    for (int n = 1; n <= 6; ++n)
        release = std::max (release, value (("env" + std::to_string (n) + ".release").c_str()) * 0.001);
    double effects = 0.0;
    if (value ("reverb.on") > 0.5)
        effects = std::max (effects, value ("reverb.decay") * 0.001 + value ("reverb.predelay") * 0.001);
    if (value ("delay.on") > 0.5)
        effects = std::max (effects, 4.0 + 8.0 * std::abs (value ("delay.feedback")) * 0.01 * 4.0); // repeats fade with the feedback
    return release + effects;
}

juce::AudioProcessorEditor* SynthyBoyProcessor::createEditor()
{
    // SYNTHYBOY_GENERIC_UI=1: the generic parameter editor (one section per parameter group) stays reachable for debugging
    if (juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_GENERIC_UI", {}).trim() == "1")
        return new juce::GenericAudioProcessorEditor (*this);
    return new synthyboy::ui::SynthyBoyEditor (*this, *this);
}

synthyboy::ParamValues SynthyBoyProcessor::currentValues() const
{
    auto values = synthyboy::ParamValues::defaults();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        if (rawParams[i] != nullptr)
            values.plain[i] = rawParams[i]->load();
    return values;
}

void SynthyBoyProcessor::applyValues (const synthyboy::ParamValues& values)
{
    const auto* table = synthyboy::paramTable();
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        if (auto* parameter = apvts.getParameter (table[i].key))
        {
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (values.plain[i]));

            // The normalised round trip (plain -> 0..1 -> plain) drifts in the last float bits (-12 -> -11.9999981),
            // which makes the saved state differ from the loaded one. Store the exact value in the raw atomic; skipped when
            // the round trip landed somewhere genuinely different (e.g. stepped or clamped parameters).
            if (rawParams[i] != nullptr)
            {
                const float exact = values.plain[i];
                const float stored = rawParams[i]->load();
                if (std::abs (stored - exact) <= 1.0e-4f * std::max (1.0f, std::abs (exact)))
                    rawParams[i]->store (exact);
            }
        }
}

bool SynthyBoyProcessor::loadLegacyXml (const void* data, int sizeInBytes, synthyboy::ParamValues& values) const
{
    // the spike's state: <SynthyBoyState><PARAM id="gain" value="-12.0"/>...</SynthyBoyState> in JUCE's binary XML wrapper
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    for (auto* child : xml->getChildIterator())
    {
        if (! child->hasTagName ("PARAM") || ! child->hasAttribute ("value"))
            continue;

        // the three parameters of the spike (gain, waveform, cutoff) go through the migration of state format version 1
        synthyboy::migrateLegacyParam (child->getStringAttribute ("id").toRawUTF8(), child->getDoubleAttribute ("value"), values);
    }
    return true;
}

synthyboy::ShellState SynthyBoyProcessor::shellState() const
{
    synthyboy::ShellState shell;
    shell.midiCc = midiMap.bindings();
    const std::lock_guard<std::mutex> lock (contentMutex);
    shell.locks = locks;
    return shell;
}

std::string SynthyBoyProcessor::stateText()
{
    std::string text;
    {
        const std::lock_guard<std::mutex> lock (contentMutex);
        text = synthyboy::saveState (currentValues(), patchContent);
    }
    return synthyboy::withShellSection (text, shellState());
}

void SynthyBoyProcessor::applyShell (const synthyboy::ShellState& shell)
{
    midiMap.setBindings (shell.midiCc);
    const std::lock_guard<std::mutex> lock (contentMutex);
    locks = shell.locks;
}

void SynthyBoyProcessor::applyPatch (synthyboy::ParamValues values, synthyboy::StateContent newContent, bool respectLocks)
{
    if (respectLocks)
    {
        std::vector<std::string> keep;
        synthyboy::StateContent currentContent;
        {
            const std::lock_guard<std::mutex> lock (contentMutex);
            keep = locks;
            currentContent = patchContent;
        }
        if (! keep.empty())
        {
            synthyboy::keepLockedValues (values, currentValues(), keep);
            synthyboy::StateContent merged = newContent;
            synthyboy::keepLockedContent (merged, currentContent, keep);
            if (synthyboy::validateContent (merged))
                newContent = std::move (merged);
        }
    }

    applyValues (values);
    {
        // a loaded document is a whole patch (the content of the previous one must not leak into it)
        const std::lock_guard<std::mutex> lock (contentMutex);
        patchContent = std::move (newContent);
        if (engine != nullptr)
            engine->setContent (patchContent); // message thread: allocates, handed to the audio thread lock free
    }
    contentRev.fetch_add (1, std::memory_order_relaxed); // a whole document arrived: the editor checks it for missing files
}

void SynthyBoyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const std::string text = stateText();
    destData.replaceAll (text.data(), text.size());
}

void SynthyBoyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto values = currentValues();
    synthyboy::StateContent loadedContent;
    const auto result = synthyboy::loadStateWithContent (data, static_cast<size_t> (sizeInBytes), values, loadedContent);

    bool apply = result.status == synthyboy::LoadStatus::ok || result.status == synthyboy::LoadStatus::newer_version;
    const bool document = apply;
    if (result.status == synthyboy::LoadStatus::invalid)
        apply = loadLegacyXml (data, sizeInBytes, values);

    if (! apply)
        return;

    // A session of ours carries the `shell` section (MIDI map, locks): it is restored exactly, locks included. A document without it is a
    // preset or a state of an older version: that is a preset change, the locked modules of this instance survive it (G08).
    synthyboy::ShellState shell;
    const auto shellStatus = document ? synthyboy::loadShellSection (data, static_cast<size_t> (sizeInBytes), shell) : synthyboy::ShellStatus::absent;
    const bool session = shellStatus == synthyboy::ShellStatus::ok || shellStatus == synthyboy::ShellStatus::rejected;
    if (session)
        applyShell (shell);

    // the legacy XML of the spike has no content (loadedContent is empty then)
    applyPatch (values, std::move (loadedContent), ! session);
    if (! session)
        applyMapOfPresetChange (nullptr); // a preset change: the default MIDI map (S p.315)

    {
        const std::lock_guard<std::mutex> lock (contentMutex);
        savedValues = currentValues();
        savedContent = patchContent;
    }

    // Values changed behind the host's back: the CLAP wrapper turns "program changed" into
    // params.rescan(VALUES), VST3 into a restartComponent (clap-validator state-reproducibility).
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

bool SynthyBoyProcessor::revertToSaved()
{
    synthyboy::ParamValues values;
    synthyboy::StateContent content;
    {
        const std::lock_guard<std::mutex> lock (contentMutex);
        values = savedValues;
        content = savedContent;
    }
    applyValues (values);
    setContent (content);
    return true;
}

int SynthyBoyProcessor::copyScope (float* dest, int maxSamples) const
{
    const uint32_t w = scopeWrite.load (std::memory_order_acquire);
    const int available = static_cast<int> (std::min<uint32_t> (w, static_cast<uint32_t> (synthyboy::ui::UiHost::kScopeSamples)));
    const int n = std::min (maxSamples, available);
    for (int i = 0; i < n; ++i)
        dest[i] = scopeRing[(w - static_cast<uint32_t> (n) + static_cast<uint32_t> (i)) % synthyboy::ui::UiHost::kScopeSamples].load (std::memory_order_relaxed);
    return n;
}

synthyboy::StateContent SynthyBoyProcessor::content() const
{
    const std::lock_guard<std::mutex> lock (contentMutex);
    return patchContent;
}

void SynthyBoyProcessor::reloadContent()
{
    const std::lock_guard<std::mutex> lock (contentMutex);
    if (engine != nullptr)
        engine->setContent (patchContent);
}

bool SynthyBoyProcessor::setContent (const synthyboy::StateContent& newContent)
{
    if (! synthyboy::validateContent (newContent))
        return false;
    {
        const std::lock_guard<std::mutex> lock (contentMutex);
        if (newContent == patchContent)
            return true;
        patchContent = newContent;
        if (engine != nullptr)
            engine->setContent (patchContent); // message thread: allocates, handed to the audio thread lock free
    }
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

// ---- MIDI learn, Save as Default, locks (docs/ui-design/15-shell-features.md) ------------------------------------------------------------

void SynthyBoyProcessor::startMidiLearn (const std::string& key)
{
    const auto* info = synthyboy::findParamByKey (key.c_str());
    midiMap.armLearn (info != nullptr ? synthyboy::findParamIndex (info->id) : -1);
}

void SynthyBoyProcessor::cancelMidiLearn()
{
    midiMap.disarm();
}

std::string SynthyBoyProcessor::midiLearnKey() const
{
    const int armed = midiMap.armedParam();
    return armed >= 0 && static_cast<size_t> (armed) < synthyboy::paramCount() ? std::string (synthyboy::paramTable()[armed].key) : std::string();
}

int SynthyBoyProcessor::midiCcForKey (const std::string& key) const
{
    const auto* info = synthyboy::findParamByKey (key.c_str());
    return info != nullptr ? midiMap.ccForParam (synthyboy::findParamIndex (info->id)) : -1;
}

void SynthyBoyProcessor::clearMidiCc (const std::string& key)
{
    if (const auto* info = synthyboy::findParamByKey (key.c_str()))
    {
        midiMap.clearParam (synthyboy::findParamIndex (info->id));
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    }
}

bool SynthyBoyProcessor::saveAsDefault()
{
    return synthyboy::saveUserDefault (userDir, stateText());
}

bool SynthyBoyProcessor::clearUserDefault()
{
    return synthyboy::clearUserDefault (userDir);
}

bool SynthyBoyProcessor::initPatch()
{
    auto values = synthyboy::ParamValues::defaults();
    synthyboy::StateContent initContent;
    synthyboy::loadUserDefault (userDir, values, initContent); // untouched (the factory init patch) when there is no default
    applyPatch (values, std::move (initContent), true);
    applyMapOfPresetChange (nullptr); // Init Preset loads the default MIDI map (S p.315)
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return true;
}

bool SynthyBoyProcessor::isModuleLocked (const std::string& prefix) const
{
    const std::lock_guard<std::mutex> lock (contentMutex);
    return synthyboy::isLocked (locks, prefix);
}

void SynthyBoyProcessor::setModuleLocked (const std::string& prefix, bool locked)
{
    bool changed = false;
    {
        const std::lock_guard<std::mutex> lock (contentMutex);
        const bool before = synthyboy::isLocked (locks, prefix);
        changed = synthyboy::setLocked (locks, prefix, locked) && before != synthyboy::isLocked (locks, prefix);
    }
    if (changed)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

bool SynthyBoyProcessor::loadPatch (const synthyboy::ParamValues& values, const synthyboy::StateContent& newContent, const synthyboy::ui::MidiMapBindings* presetMidiMap)
{
    if (! synthyboy::validateContent (newContent))
        return false;
    applyPatch (values, newContent, true);
    applyMapOfPresetChange (presetMidiMap);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return true;
}

bool SynthyBoyProcessor::requestFile (const synthyboy::ui::FileRequest& request, std::function<void (const juce::File&)> done)
{
    if (fileRequestHandler)
        return fileRequestHandler (request, std::move (done));
    if (! done)
        return false;
    // the last folder per purpose is remembered for the session
    static std::map<juce::String, juce::File> lastFolder;
    const bool save = request.mode == synthyboy::ui::FileRequest::Mode::save;
    juce::File start = lastFolder.count (request.purpose) != 0 ? lastFolder[request.purpose] : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    if (save && request.suggestedName.isNotEmpty())
        start = start.getChildFile (request.suggestedName);
    fileChooser = std::make_unique<juce::FileChooser> (request.title, start, request.wildcards);
    const auto flags = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting
                            : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    const juce::String purpose = request.purpose;
    fileChooser->launchAsync (flags, [purpose, save, callback = std::move (done)] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (file == juce::File() || (! save && ! file.existsAsFile()))
            return;
        lastFolder[purpose] = file.getParentDirectory();
        callback (file);
    });
    return true;
}

void SynthyBoyProcessor::injectMidiMessage (const juce::MidiMessage& msg)
{
    std::lock_guard<std::mutex> lock (uiMidiMutex);
    uiMidiBuffer.addEvent (msg, 0);
}


// This creates new instances of the plugin (entry point used by the VST3 and CLAP wrappers).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthyBoyProcessor();
}
