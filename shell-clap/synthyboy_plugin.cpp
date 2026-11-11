// SynthyBoy - native CLAP shell: the state of one plug-in instance (see synthyboy_plugin.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy_plugin.h"

#include <algorithm>
#include <filesystem>

#include "synthyboy/midi_events.h"
#include "synthyboy/preferences.h"
#include <clap/ext/params.h>
#include <clap/ext/state.h>

namespace synthyboy
{
SynthyBoyClap::SynthyBoyClap (const clap_host_t* h)
    : host (h), values (ParamValues::defaults()), quality (qualityForTier (Tier::high))
{
    events.reserve (256);
    quality.realtime = true; // live: the audio thread never waits for the engine's helper thread (Quality::realtime)
#if SYNTHYBOY_HAS_VITAL
    engine = createEngine (EngineKind::vital);
#else
    engine = createEngine (EngineKind::own);
#endif

    contentStore.addPackRoot ((std::filesystem::path (userDir) / "packs").string()); // installed content packs
    itemLibrary = std::make_unique<UserLibrary> (userDir);
    if (engine)
        engine->setContentStore (&contentStore); // before the first setContent: the references of the content decode against the library

    prefs = loadPreferences (userDir);
    prefSilenceOnStop.store (prefs.silenceOnStop);

    // Save as Default (G02): a new instance starts from the user's default patch (written by the editor shell), if there is one
    ShellState defaultShell;
    if (loadUserDefault (userDir, values, content, &defaultShell))
    {
        midiMap.setBindings (defaultShell.midiCc);
        locks = defaultShell.locks;
        if (engine)
        {
            for (size_t i = 0; i < paramCount(); ++i)
                engine->setParam (paramTable()[i].id, values.plain[i]);
            engine->setContent (content);
        }
    }
    else
    {
        // the preferences of a new instance (S p.314, A383): MPE on, pitch bend also to X, Y bi-directional
        const auto set = [this] (const char* key, bool on)
        {
            if (const ParamInfo* info = findParamByKey (key); info != nullptr && on)
            {
                values.plain[static_cast<size_t> (findParamIndex (info->id))] = 1.0f;
                if (engine)
                    engine->setParam (info->id, 1.0f);
            }
        };
        set ("master.mpe.on", prefs.mpeByDefault);
        set ("master.mpe.bend_to_x", prefs.mpeBendToX);
        set ("master.mpe.y_bipolar", prefs.mpeYBipolar);
    }
    applyDefaultMidiMap (false);
    for (size_t i = 0; i < paramCount(); ++i)
        ui.publishQuiet (i, values.plain[i]);
    rememberSaved();
}

SynthyBoyClap::~SynthyBoyClap()
{
    bitmapGui.reset(); // the GUI first: it reads the engine and the library
    if (engine)
        engine->setContentStore (nullptr); // the store is destroyed before the engine (member order)
}

void SynthyBoyClap::applyDefaultMidiMap (bool force)
{
    std::vector<std::pair<int, std::string>> bindings;
    if ((force || midiMap.numMappings() == 0) && loadDefaultMidiMap (userDir, bindings))
        midiMap.setBindings (bindings);
}

// ---- audio thread -------------------------------------------------------------------------------------------------------------------------

void SynthyBoyClap::applyHostValue (int index, float constrained) noexcept
{
    if (engine)
        engine->setParam (paramTable()[index].id, constrained);
    ui.publish (static_cast<size_t> (index), constrained);
}

namespace
{
bool pushParamValue (const clap_output_events_t* out, uint32_t time, clap_id id, double value, uint32_t flags) noexcept
{
    if (out == nullptr || out->try_push == nullptr)
        return true;
    clap_event_param_value_t e {};
    e.header.size = sizeof e;
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_PARAM_VALUE;
    e.header.flags = flags;
    e.param_id = id;
    e.cookie = nullptr;
    e.note_id = -1;
    e.port_index = -1;
    e.channel = -1;
    e.key = -1;
    e.value = value;
    return out->try_push (out, &e.header);
}

bool pushGesture (const clap_output_events_t* out, uint32_t time, clap_id id, bool begin) noexcept
{
    if (out == nullptr || out->try_push == nullptr)
        return true;
    clap_event_param_gesture_t e {};
    e.header.size = sizeof e;
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    e.header.flags = CLAP_EVENT_IS_LIVE;
    e.param_id = id;
    return out->try_push (out, &e.header);
}
} // namespace

void SynthyBoyClap::drainUiEdits (const clap_output_events_t* out, uint32_t time) noexcept
{
    if (! ui.hasPendingEdits())
        return;
    ui.drainEdits (
        [&] (size_t i) { return i >= paramCount() || pushGesture (out, time, paramTable()[i].id, true); },
        [&] (size_t i, float plain)
        {
            if (i >= paramCount())
                return true;
            const auto& info = paramTable()[i];
            const float constrained = constrainValue (info, plain);
            if (engine)
                engine->setParam (info.id, constrained);
            if (constrained < plain || constrained > plain)
                ui.publish (i, constrained); // the GUI shows the value the engine really has (a stepped parameter rounds)
            return pushParamValue (out, time, info.id, static_cast<double> (constrained), CLAP_EVENT_IS_LIVE);
        },
        [&] (size_t i) { return i >= paramCount() || pushGesture (out, time, paramTable()[i].id, false); });
}

void SynthyBoyClap::handleMidi (const uint8_t* data, uint32_t time, const clap_output_events_t* out) noexcept
{
    // a controller that is in the MIDI learn table sets its parameter (G07) and is reported to the host
    if ((data[0] & 0xF0) == 0xB0)
    {
        const auto hit = midiMap.handleCc (data[1] & 0x7F, data[2] & 0x7F);
        if (hit.apply)
        {
            const auto& info = paramTable()[hit.paramIndex];
            applyHostValue (hit.paramIndex, hit.plain);
            pushParamValue (out, time, info.id, static_cast<double> (hit.plain), 0);
        }
    }
    Event e {};
    if (! translateMidiMessage (data, 3, static_cast<int32_t> (time), e))
        return;
    if (e.type == Event::ModWheel)
        modWheelPosition.store (e.value, std::memory_order_relaxed);
    if (e.type == Event::NoteOn)
    {
        lastKey.store (e.key, std::memory_order_relaxed);
        lastVelocity.store (e.value, std::memory_order_relaxed);
        lastOnSample.store (samplesDone.load (std::memory_order_relaxed) + e.frame, std::memory_order_relaxed);
        lastOffSample.store (-1, std::memory_order_relaxed);
    }
    else if (e.type == Event::NoteOff && e.key == lastKey.load (std::memory_order_relaxed))
        lastOffSample.store (samplesDone.load (std::memory_order_relaxed) + e.frame, std::memory_order_relaxed);
    events.push_back (e);
}

// ---- main thread --------------------------------------------------------------------------------------------------------------------------

ParamValues SynthyBoyClap::currentValues() const
{
    auto all = ParamValues::defaults();
    for (size_t i = 0; i < paramCount(); ++i)
        all.plain[i] = ui.value (i);
    return all;
}

void SynthyBoyClap::setAllValues (const ParamValues& all)
{
    for (size_t i = 0; i < paramCount(); ++i)
    {
        values.plain[i] = all.plain[i];
        if (engine)
            engine->setParam (paramTable()[i].id, all.plain[i]);
        ui.publish (i, all.plain[i]);
    }
}

void SynthyBoyClap::applyPatch (ParamValues newValues, StateContent newContent, bool respectLocks)
{
    installPatch (std::move (newValues), std::move (newContent), respectLocks);
    rescanValues();
    markStateDirty();
}

void SynthyBoyClap::installPatch (ParamValues newValues, StateContent newContent, bool respectLocks)
{
    if (respectLocks && ! locks.empty())
    {
        keepLockedValues (newValues, currentValues(), locks);
        StateContent merged = newContent;
        keepLockedContent (merged, content, locks);
        if (validateContent (merged))
            newContent = std::move (merged);
    }
    setAllValues (newValues);
    content = std::move (newContent); // a loaded document is a whole patch (the content of the previous one must not leak into it)
    if (engine)
        engine->setContent (content); // main thread: allocates, handed to the audio thread lock free
    contentRev.fetch_add (1, std::memory_order_relaxed);
}

bool SynthyBoyClap::setContentFromGui (const StateContent& newContent)
{
    if (! validateContent (newContent))
        return false;
    if (newContent == content)
        return true;
    content = newContent;
    if (engine)
        engine->setContent (content);
    markStateDirty();
    return true;
}

ShellState SynthyBoyClap::shellState() const
{
    ShellState shell;
    shell.midiCc = midiMap.bindings();
    shell.locks = locks;
    return shell;
}

void SynthyBoyClap::applyShell (const ShellState& shell)
{
    midiMap.setBindings (shell.midiCc);
    locks = shell.locks;
}

std::string SynthyBoyClap::stateText() const
{
    return withShellSection (saveState (currentValues(), content), shellState());
}

void SynthyBoyClap::rememberSaved()
{
    savedValues = currentValues();
    savedContent = content;
}

void SynthyBoyClap::markStateDirty() const
{
    if (host != nullptr && host->get_extension != nullptr)
        if (const auto* state = static_cast<const clap_host_state_t*> (host->get_extension (host, CLAP_EXT_STATE)))
            if (state->mark_dirty != nullptr)
                state->mark_dirty (host);
}

void SynthyBoyClap::rescanValues() const
{
    if (host != nullptr && host->get_extension != nullptr)
        if (const auto* params = static_cast<const clap_host_params_t*> (host->get_extension (host, CLAP_EXT_PARAMS)))
            if (params->rescan != nullptr)
                params->rescan (host, CLAP_PARAM_RESCAN_VALUES);
}

void SynthyBoyClap::requestFlush() const
{
    if (host != nullptr && host->get_extension != nullptr)
        if (const auto* params = static_cast<const clap_host_params_t*> (host->get_extension (host, CLAP_EXT_PARAMS)))
            if (params->request_flush != nullptr)
                params->request_flush (host);
}

void SynthyBoyClap::requestRestart() const
{
    if (host != nullptr && host->request_restart != nullptr)
        host->request_restart (host);
}
} // namespace synthyboy
