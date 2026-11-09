// SynthyBoy - native JUCE-free CLAP entry and wrapper (docs/ARCHITECTURE.md 4).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy_clap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/events.h"
#include "synthyboy/kick_pages.h"
#include "synthyboy/midi_events.h"
#include "synthyboy/params.h"
#include "synthyboy/preferences.h"
#include "synthyboy/preset.h"
#include "synthyboy/quality.h"
#include "synthyboy/shell_features.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"
#include "synthyboy/vital_plugin_import.h"
#include "synthyboy_content_ext.h"
#include <clap/ext/remote-controls.h>
#include <clap/ext/render.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace synthyboy
{
namespace
{
constexpr char kPluginId[] = "com.safian.synthyboy";
constexpr char kPluginName[] = "SynthyBoy";
constexpr char kPluginVendor[] = "Safian";
constexpr char kPluginUrl[] = "https://github.com/Safian/SynthyBoy";
constexpr char kPluginVersion[] = "0.1.0";
constexpr char kPluginDescription[] = "Synthesizer plugin (VST3 + CLAP)";

const char* const kPluginFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    nullptr
};

const clap_plugin_descriptor_t s_descriptor = {
    CLAP_VERSION,
    kPluginId,
    kPluginName,
    kPluginVendor,
    kPluginUrl,
    "", // manual_url
    "", // support_url
    kPluginVersion,
    kPluginDescription,
    kPluginFeatures
};

struct SynthyBoyClap
{
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    std::unique_ptr<IEngine> engine;
    UserContentStore contentStore { userDataDirectory() }; // the user content library (WP-01): the engine resolves the references of `content` through it
    ParamValues values;
    StateContent content; // the `content` section of the state (not parameters): custom LFO shapes, curves and the tuning go to the engine (setContent), main thread only
    MidiCcMap midiMap;                // MIDI learn table (G07): CC -> parameter, read on the audio thread; saved in the `shell` section
    std::vector<std::string> locks;   // module locks (G08), main thread; kept for the round trip with the editor shell
    Preferences prefs;                // the global preferences (GROUPA2 WP-02): read once when the plug-in is created (the editor shell writes the file)
    TransportStopWatch stopWatch;     // audio thread: the host transport stops -> silence () when prefs.silenceOnStop
    bool offlineRender = false;       // clap_plugin_render: the host renders offline
    // the last note (audio thread writes, the main thread reads: Resample, the audio of the last note) and the host tempo
    std::atomic<int> lastKey { -1 };
    std::atomic<float> lastVelocity { 0.8f };
    std::atomic<int64_t> lastOnSample { 0 }, lastOffSample { -1 }, samplesDone { 0 };
    std::atomic<double> tempo { 120.0 };
    Quality quality;
    std::vector<Event> events;
    double sampleRate = 48000.0;
    uint32_t maxBlockSize = 512;
    bool active = false;
    std::atomic<int> reportedLatency { 0 };   // the latency the host was told (ext_latency_get)
    bool processing = false;

    ~SynthyBoyClap()
    {
        if (engine)
            engine->setContentStore (nullptr); // the store is destroyed before the engine (member order)
    }

    SynthyBoyClap (const clap_host_t* h)
        : host (h), values (ParamValues::defaults()), quality (qualityForTier (Tier::high))
    {
        events.reserve (256);
        quality.realtime = true; // live: the audio thread never waits for the engine's helper thread (Quality::realtime)
#if SYNTHYBOY_HAS_VITAL
        engine = createEngine (EngineKind::vital);
#else
        engine = createEngine (EngineKind::own);
#endif

        contentStore.addPackRoot ((std::filesystem::path (userDataDirectory()) / "packs").string()); // installed content packs
        if (engine)
            engine->setContentStore (&contentStore); // before the first setContent: the references of the content decode against the library

        prefs = loadPreferences (userDataDirectory());

        // Save as Default (G02): a new instance starts from the user's default patch (written by the editor shell), if there is one
        ShellState defaultShell;
        if (loadUserDefault (userDataDirectory(), values, content, &defaultShell))
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
                    if (engine) engine->setParam (info->id, 1.0f);
                }
            };
            set ("master.mpe.on", prefs.mpeByDefault);
            set ("master.mpe.bend_to_x", prefs.mpeBendToX);
            set ("master.mpe.y_bipolar", prefs.mpeYBipolar);
        }
        applyDefaultMidiMap (false);
    }

    /** The default MIDI CC map file (S p.325): when `force`, it replaces the table; else only an empty table takes it (a new instance). */
    void applyDefaultMidiMap (bool force)
    {
        std::vector<std::pair<int, std::string>> bindings;
        if ((force || midiMap.numMappings() == 0) && loadDefaultMidiMap (userDataDirectory(), bindings))
            midiMap.setBindings (bindings);
    }
};

inline SynthyBoyClap* toClap (const clap_plugin_t* p) noexcept
{
    return static_cast<SynthyBoyClap*> (p->plugin_data);
}

// -----------------------------------------------------------------------------
// Plugin Lifecycle
// -----------------------------------------------------------------------------

bool clap_init (const clap_plugin_t*) noexcept
{
    return true;
}

void clap_destroy (const clap_plugin_t* p) noexcept
{
    delete toClap (p);
}

bool clap_activate (const clap_plugin_t* p, double sample_rate, uint32_t, uint32_t max_frames_count) noexcept
{
    auto* self = toClap (p);
    self->sampleRate = sample_rate > 0.0 ? sample_rate : 48000.0;
    self->maxBlockSize = std::max (max_frames_count, 32u);
    self->stopWatch.reset();
    if (self->engine)
    {
        self->engine->setUltraRender (self->prefs.ultraRender && self->offlineRender);
        self->engine->prepare (self->sampleRate, static_cast<int> (self->maxBlockSize), self->quality);
    }
    self->active = true;
    return true;
}

void clap_deactivate (const clap_plugin_t* p) noexcept
{
    toClap (p)->active = false;
}

bool clap_start_processing (const clap_plugin_t* p) noexcept
{
    toClap (p)->processing = true;
    return true;
}

void clap_stop_processing (const clap_plugin_t* p) noexcept
{
    toClap (p)->processing = false;
}

void clap_reset (const clap_plugin_t* p) noexcept
{
    auto* self = toClap (p);
    if (self->engine)
        self->engine->reset();
}

// -----------------------------------------------------------------------------
// Audio Processing & Event Handling
// -----------------------------------------------------------------------------

clap_process_status clap_process (const clap_plugin_t* p, const clap_process_t* process) noexcept
{
    auto* self = toClap (p);
    if (! self->engine)
        return CLAP_PROCESS_ERROR;

    self->events.clear();

    // 1. Process incoming events
    if (process->in_events != nullptr)
    {
        const uint32_t numEvents = process->in_events->size (process->in_events);
        for (uint32_t i = 0; i < numEvents; ++i)
        {
            const auto* hdr = process->in_events->get (process->in_events, i);
            if (hdr == nullptr || hdr->space_id != CLAP_CORE_EVENT_SPACE_ID)
                continue;

            switch (hdr->type)
            {
                case CLAP_EVENT_NOTE_ON:
                {
                    const auto* ev = reinterpret_cast<const clap_event_note_t*> (hdr);
                    Event e {};
                    e.frame = static_cast<int32_t> (ev->header.time);
                    e.type = Event::NoteOn;
                    e.channel = static_cast<int16_t> (ev->channel);
                    e.key = static_cast<int16_t> (ev->key);
                    e.value = static_cast<float> (ev->velocity);
                    e.noteId = ev->note_id;
                    self->events.push_back (e);
                    self->lastKey.store (e.key, std::memory_order_relaxed);
                    self->lastVelocity.store (e.value, std::memory_order_relaxed);
                    self->lastOnSample.store (self->samplesDone.load (std::memory_order_relaxed) + e.frame, std::memory_order_relaxed);
                    self->lastOffSample.store (-1, std::memory_order_relaxed);
                    break;
                }
                case CLAP_EVENT_NOTE_OFF:
                case CLAP_EVENT_NOTE_CHOKE:
                {
                    const auto* ev = reinterpret_cast<const clap_event_note_t*> (hdr);
                    Event e {};
                    e.frame = static_cast<int32_t> (ev->header.time);
                    e.type = Event::NoteOff;
                    e.channel = static_cast<int16_t> (ev->channel);
                    e.key = static_cast<int16_t> (ev->key);
                    e.value = static_cast<float> (ev->velocity);
                    e.noteId = ev->note_id;
                    self->events.push_back (e);
                    if (e.key == self->lastKey.load (std::memory_order_relaxed))
                        self->lastOffSample.store (self->samplesDone.load (std::memory_order_relaxed) + e.frame, std::memory_order_relaxed);
                    break;
                }
                case CLAP_EVENT_NOTE_EXPRESSION:
                {
                    // per-note expressions (MPE through CLAP, docs/ui-design/13-new-keys.md 4.7): TUNING (semitones), PRESSURE, BRIGHTNESS, PAN
                    const auto* ev = reinterpret_cast<const clap_event_note_expression_t*> (hdr);
                    Event e {};
                    e.frame = static_cast<int32_t> (ev->header.time);
                    e.type = Event::NoteExpression;
                    e.channel = static_cast<int16_t> (std::clamp<int> (ev->channel, 0, 15));
                    e.key = static_cast<int16_t> (ev->key);
                    e.noteId = ev->note_id;
                    e.value = static_cast<float> (ev->value);
                    if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                        e.expression = Event::Tuning;
                    else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE)
                        e.expression = Event::Pressure;
                    else if (ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
                        e.expression = Event::Brightness;
                    else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PAN)
                        e.expression = Event::Pan; // the X axis of the MPE mappings
                    else
                        break; // volume, vibrato, expression: not used
                    self->events.push_back (e);
                    break;
                }
                case CLAP_EVENT_MIDI:
                {
                    // the MIDI dialect of the note port: CC 64 sustain pedal, CC 1, CC 74, pressure, bend, note on/off
                    const auto* ev = reinterpret_cast<const clap_event_midi_t*> (hdr);
                    // a controller that is in the MIDI learn table sets its parameter (G07) and is reported to the host
                    if ((ev->data[0] & 0xF0) == 0xB0)
                    {
                        const auto hit = self->midiMap.handleCc (ev->data[1] & 0x7F, ev->data[2] & 0x7F);
                        if (hit.apply)
                        {
                            const auto& info = paramTable()[hit.paramIndex];
                            self->values.plain[hit.paramIndex] = hit.plain;
                            self->engine->setParam (info.id, hit.plain);
                            if (process->out_events != nullptr)
                            {
                                clap_event_param_value_t out {};
                                out.header.size = sizeof out;
                                out.header.time = ev->header.time;
                                out.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                                out.header.type = CLAP_EVENT_PARAM_VALUE;
                                out.header.flags = 0;
                                out.param_id = info.id;
                                out.cookie = nullptr;
                                out.note_id = -1;
                                out.port_index = -1;
                                out.channel = -1;
                                out.key = -1;
                                out.value = static_cast<double> (hit.plain);
                                process->out_events->try_push (process->out_events, &out.header);
                            }
                        }
                    }
                    Event e {};
                    if (translateMidiMessage (ev->data, 3, static_cast<int32_t> (ev->header.time), e))
                        self->events.push_back (e);
                    break;
                }
                case CLAP_EVENT_PARAM_VALUE:
                {
                    const auto* ev = reinterpret_cast<const clap_event_param_value_t*> (hdr);
                    const int idx = findParamIndex (ev->param_id);
                    if (idx >= 0)
                    {
                        const auto& info = paramTable()[idx];
                        const float val = constrainValue (info, static_cast<float> (ev->value));
                        self->values.plain[idx] = val;
                        self->engine->setParam (ev->param_id, val);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    // host transport (tempo sync, beat units, LFO HOST mode)
    {
        TransportInfo transport;
        if (process->transport != nullptr)
        {
            const clap_event_transport_t* t = process->transport;
            if ((t->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0 && t->tempo > 0.0)
            {
                transport.bpm = t->tempo;
                self->tempo.store (t->tempo, std::memory_order_relaxed);
            }
            if ((t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0)
                transport.songPosBeats = static_cast<double> (t->song_pos_beats) / static_cast<double> (CLAP_BEATTIME_FACTOR);
            transport.playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        }
        // the host stopped: notes end and the effect tails are cleared (S p.315, A381)
        if (self->stopWatch.update (process->transport != nullptr, transport.playing) && self->prefs.silenceOnStop)
            self->engine->silence();
        self->engine->setTransport (transport);
    }

    // 2. Render audio
    if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32 != nullptr)
    {
        float* outL = process->audio_outputs[0].data32[0];
        float* outR = (process->audio_outputs[0].channel_count > 1) ? process->audio_outputs[0].data32[1] : outL;

        self->engine->process (self->events.data(), static_cast<int> (self->events.size()),
                               outL, outR, static_cast<int> (process->frames_count));
        self->samplesDone.fetch_add (process->frames_count, std::memory_order_relaxed);
        // quality.oversample_4x changed: only prepare () can apply it (activate () calls it again after the restart)
        if (self->engine->needsPrepare() && self->host != nullptr && self->host->request_restart != nullptr)
            self->host->request_restart (self->host);
        // Bode's latency changed (the effect was switched on / off or moved): the host reads the new value after the restart
        const int latency = self->engine->latencySamples();
        if (latency != self->reportedLatency.load (std::memory_order_relaxed))
        {
            self->reportedLatency.store (latency, std::memory_order_relaxed);
            if (self->host != nullptr && self->host->request_restart != nullptr)
                self->host->request_restart (self->host);
        }
    }

    return (self->engine->activeVoices() > 0) ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

// -----------------------------------------------------------------------------
// CLAP Extensions
// -----------------------------------------------------------------------------

// audio-ports
uint32_t ext_audio_ports_count (const clap_plugin_t*, bool is_input) noexcept
{
    return is_input ? 0 : 1;
}

bool ext_audio_ports_get (const clap_plugin_t*, uint32_t index, bool is_input, clap_audio_port_info_t* info) noexcept
{
    if (is_input || index != 0 || info == nullptr)
        return false;

    info->id = 0;
    std::strncpy (info->name, "Stereo Output", sizeof (info->name) - 1);
    info->name[sizeof (info->name) - 1] = '\0';
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t s_audio_ports = {
    ext_audio_ports_count,
    ext_audio_ports_get
};

// note-ports
uint32_t ext_note_ports_count (const clap_plugin_t*, bool is_input) noexcept
{
    return is_input ? 1 : 0;
}

bool ext_note_ports_get (const clap_plugin_t*, uint32_t index, bool is_input, clap_note_port_info_t* info) noexcept
{
    if (! is_input || index != 0 || info == nullptr)
        return false;

    info->id = 0;
    std::strncpy (info->name, "Note Input", sizeof (info->name) - 1);
    info->name[sizeof (info->name) - 1] = '\0';
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    return true;
}

const clap_plugin_note_ports_t s_note_ports = {
    ext_note_ports_count,
    ext_note_ports_get
};

// params
uint32_t ext_params_count (const clap_plugin_t*) noexcept
{
    return static_cast<uint32_t> (paramCount());
}

bool ext_params_get_info (const clap_plugin_t*, uint32_t index, clap_param_info_t* info) noexcept
{
    if (index >= paramCount() || info == nullptr)
        return false;

    const auto& p = paramTable()[index];
    info->id = p.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
    if (p.flags & kParamStepped)
        info->flags |= CLAP_PARAM_IS_STEPPED;

    info->cookie = nullptr;

    std::strncpy (info->name, p.name, sizeof (info->name) - 1);
    info->name[sizeof (info->name) - 1] = '\0';

    std::strncpy (info->module, p.group, sizeof (info->module) - 1);
    info->module[sizeof (info->module) - 1] = '\0';

    info->min_value = p.min;
    info->max_value = p.max;
    info->default_value = p.def;
    return true;
}

bool ext_params_get_value (const clap_plugin_t* p, clap_id param_id, double* out_value) noexcept
{
    if (out_value == nullptr)
        return false;

    const int idx = findParamIndex (param_id);
    if (idx < 0)
        return false;

    *out_value = toClap (p)->values.plain[idx];
    return true;
}

bool ext_params_value_to_text (const clap_plugin_t*, clap_id param_id, double value, char* out_buffer, uint32_t out_buffer_capacity) noexcept
{
    if (out_buffer == nullptr || out_buffer_capacity == 0)
        return false;

    const int idx = findParamIndex (param_id);
    if (idx < 0)
        return false;

    valueToText (paramTable()[idx], static_cast<float> (value), out_buffer, out_buffer_capacity);
    return true;
}

bool ext_params_text_to_value (const clap_plugin_t*, clap_id param_id, const char* param_value_text, double* out_value) noexcept
{
    if (param_value_text == nullptr || out_value == nullptr)
        return false;

    const int idx = findParamIndex (param_id);
    if (idx < 0)
        return false;

    float val = 0.0f;
    if (textToValue (paramTable()[idx], param_value_text, val))
    {
        *out_value = val;
        return true;
    }
    return false;
}

void ext_params_flush (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) noexcept
{
    if (in == nullptr)
        return;

    auto* self = toClap (p);
    const uint32_t numEvents = in->size (in);
    for (uint32_t i = 0; i < numEvents; ++i)
    {
        const auto* hdr = in->get (in, i);
        if (hdr != nullptr && hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* ev = reinterpret_cast<const clap_event_param_value_t*> (hdr);
            const int idx = findParamIndex (ev->param_id);
            if (idx >= 0)
            {
                const auto& info = paramTable()[idx];
                const float val = constrainValue (info, static_cast<float> (ev->value));
                self->values.plain[idx] = val;
                if (self->engine)
                    self->engine->setParam (ev->param_id, val);
            }
        }
    }
}

const clap_plugin_params_t s_params = {
    ext_params_count,
    ext_params_get_info,
    ext_params_get_value,
    ext_params_value_to_text,
    ext_params_text_to_value,
    ext_params_flush
};

// state
bool ext_state_save (const clap_plugin_t* p, const clap_ostream_t* stream) noexcept
{
    if (stream == nullptr)
        return false;

    auto* self = toClap (p);
    ShellState shell;
    shell.midiCc = self->midiMap.bindings();
    shell.locks = self->locks;
    const std::string s = withShellSection (saveState (self->values, self->content), shell);
    const int64_t written = stream->write (stream, s.data(), s.size());
    return (written == static_cast<int64_t> (s.size()));
}

bool ext_state_load (const clap_plugin_t* p, const clap_istream_t* stream) noexcept
{
    if (stream == nullptr)
        return false;

    auto* self = toClap (p);
    std::vector<char> buffer;
    char chunk[4096];
    int64_t n = 0;
    while ((n = stream->read (stream, chunk, sizeof chunk)) > 0)
    {
        buffer.insert (buffer.end(), chunk, chunk + n);
        if (buffer.size() > kMaxStateBytes)
            return false;
    }

    if (buffer.empty())
        return false;

    const ParamValues before = self->values;
    const StateContent contentBefore = self->content;
    const LoadResult res = loadStateWithContent (buffer.data(), buffer.size(), self->values, self->content);
    if (res.status == LoadStatus::ok || res.status == LoadStatus::newer_version)
    {
        // a session of ours carries the `shell` section (MIDI map, locks) and is restored as it is; a document without it is a preset
        // change and the locked modules of this instance survive it (G08)
        ShellState shell;
        const ShellStatus shellStatus = loadShellSection (buffer.data(), buffer.size(), shell);
        if (shellStatus == ShellStatus::ok || shellStatus == ShellStatus::rejected)
        {
            self->midiMap.setBindings (shell.midiCc);
            self->locks = shell.locks;
        }
        else
        {
            self->applyDefaultMidiMap (true); // a preset change loads the default MIDI map (S p.315)
            if (! self->locks.empty())
            {
                keepLockedValues (self->values, before, self->locks);
                StateContent merged = self->content;
                keepLockedContent (merged, contentBefore, self->locks);
                if (validateContent (merged))
                    self->content = std::move (merged);
            }
        }
        if (self->engine)
        {
            for (size_t i = 0; i < paramCount(); ++i)
                self->engine->setParam (paramTable()[i].id, self->values.plain[i]);
            self->engine->setContent (self->content); // main thread: allocates, handed to the audio thread lock free
        }
        return true;
    }

    return false;
}

const clap_plugin_state_t s_state = {
    ext_state_save,
    ext_state_load
};

// voice-info
bool ext_voice_info_get (const clap_plugin_t* p, clap_voice_info_t* info) noexcept
{
    if (info == nullptr)
        return false;

    auto* self = toClap (p);
    info->voice_count = self->engine ? static_cast<uint32_t> (self->engine->activeVoices()) : 0;
    info->voice_capacity = static_cast<uint32_t> (self->quality.maxVoices);
    info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
    return true;
}

const clap_plugin_voice_info_t s_voice_info = {
    ext_voice_info_get
};

// latency & tail
uint32_t ext_latency_get (const clap_plugin_t* plugin) noexcept
{
    auto* self = static_cast<SynthyBoyClap*> (plugin->plugin_data);
    const int latency = self->engine ? self->engine->latencySamples() : 0;
    self->reportedLatency.store (latency, std::memory_order_relaxed);
    return static_cast<uint32_t> (latency);
}

const clap_plugin_latency_t s_latency = {
    ext_latency_get
};

uint32_t ext_tail_get (const clap_plugin_t*) noexcept
{
    return 0;
}

const clap_plugin_tail_t s_tail = {
    ext_tail_get
};

// remote-controls
uint32_t ext_remote_controls_count (const clap_plugin_t*) noexcept
{
    return static_cast<uint32_t> (kickPageCount());
}

bool ext_remote_controls_get (const clap_plugin_t*, uint32_t page_index, clap_remote_controls_page_t* page) noexcept
{
    if (page == nullptr || page_index >= kickPageCount())
        return false;

    const Page& src = kickPageTable()[page_index];

    std::memset (page, 0, sizeof (*page));

    std::strncpy (page->section_name, src.section, sizeof (page->section_name) - 1);
    page->page_id = page_index;
    std::strncpy (page->page_name, src.title, sizeof (page->page_name) - 1);

    for (size_t i = 0; i < CLAP_REMOTE_CONTROLS_COUNT; ++i)
    {
        if (i < src.count && src.keys[i] != nullptr)
        {
            const ParamInfo* p = findParamByKey (src.keys[i]);
            page->param_ids[i] = (p != nullptr) ? p->id : CLAP_INVALID_ID;
        }
        else
        {
            page->param_ids[i] = CLAP_INVALID_ID;
        }
    }

    page->is_for_preset = false;
    return true;
}

const clap_plugin_remote_controls_t s_remote_controls = {
    ext_remote_controls_count,
    ext_remote_controls_get
};

// synthyboy.content (shell-clap/synthyboy_content_ext.h): the user content library for a host without a file dialog
bool readWholeFile (const char* path, std::vector<uint8_t>& bytes)
{
    if (path == nullptr || path[0] == '\0')
        return false;
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return false;
    bytes.assign ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    return ! bytes.empty() && bytes.size() <= kMaxContentFileBytes;
}

void copyText (char* dest, size_t capacity, const std::string& text)
{
    std::strncpy (dest, text.c_str(), capacity - 1);
    dest[capacity - 1] = '\0';
}

void fillItem (synthyboy_content_item_t& out, const UserContentItem& item)
{
    out = synthyboy_content_item_t {};
    out.kind = static_cast<uint32_t> (item.kind);
    copyText (out.name, sizeof out.name, item.ref.name);
    copyText (out.pack, sizeof out.pack, item.ref.pack);
    copyText (out.file, sizeof out.file, item.ref.file);
    copyText (out.sha256, sizeof out.sha256, item.ref.sha256);
    out.size_bytes = item.size;
    out.embedded = item.embedded ? 1u : 0u;
}

bool validKind (uint32_t kind) noexcept { return kind <= SYNTHYBOY_CONTENT_IMPULSE; }

/** The state changed outside the audio path: the engine gets the content, the host is told to save. */
void contentChanged (SynthyBoyClap* self)
{
    if (self->engine)
        self->engine->setContent (self->content);
    if (self->host != nullptr && self->host->get_extension != nullptr)
        if (const auto* state = static_cast<const clap_host_state_t*> (self->host->get_extension (self->host, CLAP_EXT_STATE)))
            if (state->mark_dirty != nullptr)
                state->mark_dirty (self->host);
}

uint32_t ext_content_count (const clap_plugin_t* p, uint32_t kind) noexcept
{
    return validKind (kind) ? static_cast<uint32_t> (toClap (p)->contentStore.list (static_cast<ContentKind> (kind)).size()) : 0u;
}

int32_t ext_content_get (const clap_plugin_t* p, uint32_t kind, uint32_t index, synthyboy_content_item_t* item) noexcept
{
    if (item == nullptr || ! validKind (kind))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const auto items = toClap (p)->contentStore.list (static_cast<ContentKind> (kind));
    if (index >= items.size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    fillItem (*item, items[index]);
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_import_file (const clap_plugin_t* p, uint32_t kind, const char* path, synthyboy_content_item_t* item) noexcept
{
    if (path == nullptr || ! validKind (kind))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    auto* self = toClap (p);
    std::vector<uint8_t> bytes;
    if (! readWholeFile (path, bytes))
        return SYNTHYBOY_CONTENT_ERR_READ;
    const std::string name = fileStemOf (path);
    ContentFileRef ref;
    UserContentItem result;
    result.kind = static_cast<ContentKind> (kind);
    if (kind == SYNTHYBOY_CONTENT_WAVETABLE && vitalFileKindFromName (path) == VitalFileKind::wavetable)
    {
        const VitalPluginImport imported = importVitalFileForPlugin (path, bytes, self->contentStore, userDataDirectory(), self->engine ? self->engine->capabilities() : kCapNone);
        if (! imported.ok)
            return SYNTHYBOY_CONTENT_ERR_FORMAT;
        ref = imported.tableRef;
    }
    else
    {
        SampleFacts facts;
        if (! probeSampleWav (bytes, path, facts))
            return SYNTHYBOY_CONTENT_ERR_FORMAT;
        if (! self->contentStore.addFile (static_cast<ContentKind> (kind), name, bytes, ref))
            return SYNTHYBOY_CONTENT_ERR_STORE;
    }
    result.ref = ref;
    result.size = bytes.size();
    if (item != nullptr)
        fillItem (*item, result);
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_attach (const clap_plugin_t* p, uint32_t kind, uint32_t index, uint32_t osc) noexcept
{
    if (! validKind (kind))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    auto* self = toClap (p);
    const auto items = self->contentStore.list (static_cast<ContentKind> (kind));
    if (index >= items.size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const ContentFileRef& ref = items[index].ref;
    switch (kind)
    {
        case SYNTHYBOY_CONTENT_WAVETABLE:
            if (osc < 1 || osc > 3 || ! attachStoredWavetable (self->content, static_cast<int> (osc), ref, self->contentStore))
                return osc < 1 || osc > 3 ? SYNTHYBOY_CONTENT_ERR_ARGUMENT : SYNTHYBOY_CONTENT_ERR_FORMAT;
            break;
        case SYNTHYBOY_CONTENT_SAMPLE:
            if (osc < 1 || osc > 3 || ! attachStoredSample (self->content, &self->values, static_cast<int> (osc), ref, self->contentStore))
                return osc < 1 || osc > 3 ? SYNTHYBOY_CONTENT_ERR_ARGUMENT : SYNTHYBOY_CONTENT_ERR_FORMAT;
            if (self->engine)
                for (size_t i = 0; i < paramCount(); ++i)
                    self->engine->setParam (paramTable()[i].id, self->values.plain[i]);
            break;
        default:
            attachImpulse (self->content, ref);
            break;
    }
    if (! validateContent (self->content))
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    contentChanged (self);
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_remove (const clap_plugin_t* p, uint32_t kind, uint32_t index) noexcept
{
    if (! validKind (kind))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    auto* self = toClap (p);
    const auto items = self->contentStore.list (static_cast<ContentKind> (kind));
    if (index >= items.size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    if (! self->contentStore.remove (items[index].ref))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    if (self->engine) // a reference to the removed file is now a missing file: the engine falls back
        self->engine->setContent (self->content);
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_import_vital (const clap_plugin_t* p, const char* path, char* summary, uint32_t capacity) noexcept
{
    if (path == nullptr)
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    auto* self = toClap (p);
    std::vector<uint8_t> bytes;
    if (! readWholeFile (path, bytes))
        return SYNTHYBOY_CONTENT_ERR_READ;
    const VitalPluginImport imported = importVitalFileForPlugin (path, bytes, self->contentStore, userDataDirectory(), self->engine ? self->engine->capabilities() : kCapNone);
    const auto say = [&] (const std::string& text) { if (summary != nullptr && capacity > 0) copyText (summary, capacity, text); };
    if (! imported.ok)
    {
        say (imported.error);
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    }
    if (imported.bank)
    {
        say ("bank " + imported.name + ": " + std::to_string (imported.summary.written) + " written, " + std::to_string (imported.summary.skippedSame) + " already there, "
             + std::to_string (imported.summary.failed) + " failed");
        return SYNTHYBOY_CONTENT_OK;
    }
    if (imported.kind != VitalFileKind::preset)
    {
        say (imported.name + ": stored in the library"); // a table: use import_file / attach, a shape needs the editor
        return imported.kind == VitalFileKind::wavetable ? SYNTHYBOY_CONTENT_OK : SYNTHYBOY_CONTENT_ERR_FORMAT;
    }
    self->values = imported.values;
    self->content = imported.content;
    if (self->engine)
        for (size_t i = 0; i < paramCount(); ++i)
            self->engine->setParam (paramTable()[i].id, self->values.plain[i]);
    contentChanged (self);
    if (self->host != nullptr && self->host->get_extension != nullptr)
        if (const auto* params = static_cast<const clap_host_params_t*> (self->host->get_extension (self->host, CLAP_EXT_PARAMS)))
            if (params->rescan != nullptr)
                params->rescan (self->host, CLAP_PARAM_RESCAN_VALUES);
    say (imported.name + ": " + std::to_string (imported.report.numImported) + " imported, " + std::to_string (imported.report.numApproximated) + " approximated, "
         + std::to_string (imported.report.numDropped) + " dropped");
    return SYNTHYBOY_CONTENT_OK;
}

// ---- GROUPD (WP-14 / 15 / 16): the conversions and offline renders of core/wave_convert.h through the library of this shell ----------------------

/** Sets a parameter of the stored values and of the engine; the host re-reads the values after the action (rescan). */
void setValue (SynthyBoyClap* self, const std::string& key, float plain)
{
    const ParamInfo* info = findParamByKey (key.c_str());
    if (info == nullptr)
        return;
    const float v = constrainValue (*info, plain);
    self->values.plain[static_cast<size_t> (findParamIndex (info->id))] = v;
    if (self->engine)
        self->engine->setParam (info->id, v);
}

float valueOf (const SynthyBoyClap* self, const std::string& key, float fallback = 0.0f)
{
    const ParamInfo* info = findParamByKey (key.c_str());
    return info != nullptr ? self->values.plain[static_cast<size_t> (findParamIndex (info->id))] : fallback;
}

void say (char* summary, uint32_t capacity, const std::string& text)
{
    if (summary != nullptr && capacity > 0)
        copyText (summary, capacity, text);
}

convert::OfflineRunner offlineRunner (SynthyBoyClap* self)
{
    return [self] (const OfflineRequest& request, OfflineAudio& audio)
    {
        if (! self->engine)
            return false;
        OfflineRequest copy = request;
        copy.ultra = request.ultra || self->prefs.ultraRender; // "Use Ultra quality when rendering" (S p.315)
        if (copy.values == nullptr)
            copy.values = &self->values;
        const EngineKind kind =
#if SYNTHYBOY_HAS_VITAL
            EngineKind::vital;
#else
            EngineKind::own;
#endif
        return renderOfflineAny (*self->engine, kind, &self->content, &self->contentStore, copy, audio);
    };
}

/** A new table for oscillator `osc`: the library, the state reference, oscN.type = Wavetable, oscN.wavetable = Preset table. */
bool installTable (SynthyBoyClap* self, int osc, const wtedit::Wavetable& table)
{
    const std::vector<uint8_t> wav = wavetableToWav (table);
    ContentFileRef ref;
    if (wav.empty() || ! self->contentStore.addFile (ContentKind::wavetable, table.name().empty() ? std::string ("Table") : table.name(), wav, ref))
        return false;
    attachWavetable (self->content, osc, ref, table.author(), static_cast<int> (table.numFrames()));
    const std::string o = "osc" + std::to_string (osc) + ".";
    setValue (self, o + "type", static_cast<float> (kOscTypeWavetable));
    setValue (self, o + "wavetable", static_cast<float> (kWavetablePresetTable));
    return true;
}

/** The action is done: the engine gets the content, the host the new values and the dirty flag. */
void actionDone (SynthyBoyClap* self)
{
    contentChanged (self);
    if (self->host != nullptr && self->host->get_extension != nullptr)
        if (const auto* params = static_cast<const clap_host_params_t*> (self->host->get_extension (self->host, CLAP_EXT_PARAMS)))
            if (params->rescan != nullptr)
                params->rescan (self->host, CLAP_PARAM_RESCAN_VALUES);
}

wtedit::ImportOptions importOptionsOf (uint32_t mode)
{
    wtedit::ImportOptions options;
    const auto& choices = convert::importChoices();
    options.mode = choices[std::min<size_t> (mode, choices.size() - 1)].mode;
    return options;
}

int32_t ext_content_switch_to_wavetable (const clap_plugin_t* p, uint32_t osc, uint32_t importMode, char* summary, uint32_t capacity) noexcept
{
    auto* self = toClap (p);
    if (osc < 1 || osc > 3 || importMode >= convert::importChoices().size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const std::string o = "osc" + std::to_string (osc) + ".";
    const int type = static_cast<int> (std::lround (valueOf (self, o + "type")));
    if (type != kOscTypeSample && type != kOscTypeGranular && type != kOscTypeSpectral)
    {
        say (summary, capacity, "needs a Sample, Granular or Spectral oscillator");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    const ContentSample* sample = nullptr;
    for (const auto& s : self->content.samples)
        if (s.osc == static_cast<int> (osc))
            sample = &s;
    std::vector<uint8_t> bytes;
    if (sample == nullptr || ! self->contentStore.readFile (sample->ref, bytes))
    {
        say (summary, capacity, "no sample is loaded in this oscillator");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    const auto preview = SamplePreview::create (bytes, sample->ref.name);
    if (preview == nullptr || preview->frames() <= 0)
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    wtedit::Wavetable table;
    std::string error;
    if (! convert::sampleRegionToWavetable (preview->mono().data(), preview->mono().size(), static_cast<double> (preview->sampleRate()), valueOf (self, o + "sample.start") * 0.01f,
                                            valueOf (self, o + "sample.end", 100.0f) * 0.01f, importOptionsOf (importMode), (sample->ref.name.empty() ? std::string ("Sample") : sample->ref.name) + " wavetable", table, &error))
    {
        say (summary, capacity, error);
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    }
    if (! installTable (self, static_cast<int> (osc), table))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    setValue (self, o + "frame", 0.0f);
    actionDone (self);
    say (summary, capacity, "wavetable of " + std::to_string (table.numFrames()) + " frames");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_render_osc_warp (const clap_plugin_t* p, uint32_t osc, char* summary, uint32_t capacity) noexcept
{
    auto* self = toClap (p);
    if (osc < 1 || osc > 3)
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    convert::WarpRenderOptions options;
    options.osc = static_cast<int> (osc);
    wtedit::Wavetable table;
    std::string error;
    float scale = 1.0f;
    const std::string o = "osc" + std::to_string (osc) + ".";
    if (static_cast<int> (std::lround (valueOf (self, o + "type"))) != kOscTypeWavetable)
    {
        say (summary, capacity, "Render OSC Warp needs a wavetable oscillator");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    if (! convert::renderOscWarp (offlineRunner (self), &self->values, options, "Osc " + std::string (osc == 1 ? "A" : (osc == 2 ? "B" : "C")) + " warp", table, &error, &scale))
    {
        say (summary, capacity, error);
        return error.empty() ? SYNTHYBOY_CONTENT_ERR_RENDER : SYNTHYBOY_CONTENT_ERR_STATE;
    }
    const float amount = valueOf (self, o + "warp.amount"), level = valueOf (self, o + "level", 50.0f);
    if (! installTable (self, static_cast<int> (osc), table))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    for (const auto& change : convert::warpBakeParameterChanges (static_cast<int> (osc), amount, scale, level))
        setValue (self, change.first, change.second);
    actionDone (self);
    say (summary, capacity, std::to_string (table.numFrames()) + " frames across WARP");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_resample (const clap_plugin_t* p, uint32_t target, uint32_t importMode, char* summary, uint32_t capacity) noexcept
{
    auto* self = toClap (p);
    if (target > 3 || importMode >= convert::importChoices().size())
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    convert::ResampleOptions options;
    const int key = self->lastKey.load (std::memory_order_relaxed);
    options.key = key >= 0 ? key : 60;
    options.velocity = key >= 0 ? self->lastVelocity.load (std::memory_order_relaxed) : 0.8f;
    options.bpm = self->tempo.load (std::memory_order_relaxed);
    OfflineAudio audio;
    if (! convert::renderResample (offlineRunner (self), options, audio))
        return SYNTHYBOY_CONTENT_ERR_RENDER;
    wtedit::Wavetable left, right;
    std::string error;
    if (! convert::audioToWavetables (audio, importOptionsOf (importMode), "Resample", left, target == 0 ? &right : nullptr, &error))
    {
        say (summary, capacity, error);
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    left.setName (target == 0 ? "Resample Osc A" : "Resample Osc " + std::string (target == 1 ? "A" : (target == 2 ? "B" : "C")));
    if (! installTable (self, target == 0 ? 1 : static_cast<int> (target), left))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    if (target == 0)
    {
        right.setName ("Resample Osc B");
        if (! installTable (self, 2, right))
            return SYNTHYBOY_CONTENT_ERR_STORE;
    }
    actionDone (self);
    say (summary, capacity, "one bar, " + std::to_string (left.numFrames()) + " frames");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_render_last_note (const clap_plugin_t* p, const char* path, char* summary, uint32_t capacity) noexcept
{
    auto* self = toClap (p);
    if (path == nullptr || path[0] == '\0')
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const int key = self->lastKey.load (std::memory_order_relaxed);
    if (key < 0)
    {
        say (summary, capacity, "play a note first");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    const int64_t on = self->lastOnSample.load (std::memory_order_relaxed), off = self->lastOffSample.load (std::memory_order_relaxed);
    const int64_t end = off >= 0 ? off : self->samplesDone.load (std::memory_order_relaxed);
    convert::NoteRender note;
    note.key = key;
    note.velocity = self->lastVelocity.load (std::memory_order_relaxed);
    note.heldSeconds = std::clamp (static_cast<double> (end - on) / std::max (self->sampleRate, 1.0), 0.25, 20.0);
    OfflineAudio audio;
    if (! convert::renderNote (offlineRunner (self), note, audio))
        return SYNTHYBOY_CONTENT_ERR_RENDER;
    const std::vector<uint8_t> wav = convert::audioToWav (audio, 24);
    std::error_code ec;
    std::filesystem::create_directories (std::filesystem::path (path).parent_path(), ec);
    std::ofstream out (path, std::ios::binary);
    if (wav.empty() || ! out)
        return SYNTHYBOY_CONTENT_ERR_STORE;
    out.write (reinterpret_cast<const char*> (wav.data()), static_cast<std::streamsize> (wav.size()));
    if (! out)
        return SYNTHYBOY_CONTENT_ERR_STORE;
    say (summary, capacity, std::to_string (audio.frames()) + " frames at " + std::to_string (static_cast<int> (audio.sampleRate)) + " Hz");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_wavetable_frame_to_lfo (const clap_plugin_t* p, uint32_t osc, uint32_t lfo, char* summary, uint32_t capacity) noexcept
{
    auto* self = toClap (p);
    if (osc < 1 || osc > 3 || lfo < 1 || lfo > static_cast<uint32_t> (kMaxContentLfoShapes))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    convert::WarpRenderOptions options;
    options.osc = static_cast<int> (osc);
    options.frames = 2;
    options.rawFrame = true;
    wtedit::Wavetable one;
    std::string error;
    if (! convert::renderOscWarp (offlineRunner (self), &self->values, options, "frame", one, &error) || one.numFrames() == 0)
    {
        say (summary, capacity, error);
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    ContentLfoShape shape;
    float reached = 0.0f;
    if (! convert::frameToLfoShape (one.getFrame (0), static_cast<int> (lfo), shape, 0.002f, kMaxLfoShapePoints, &reached))
        return SYNTHYBOY_CONTENT_ERR_FORMAT;
    shape.name = "Osc " + std::string (osc == 1 ? "A" : (osc == 2 ? "B" : "C")) + " frame";
    auto& shapes = self->content.lfoShapes;
    shapes.erase (std::remove_if (shapes.begin(), shapes.end(), [lfo] (const ContentLfoShape& s) { return s.lfo == static_cast<int> (lfo); }), shapes.end());
    shapes.push_back (shape);
    std::sort (shapes.begin(), shapes.end(), [] (const ContentLfoShape& a, const ContentLfoShape& b) { return a.lfo < b.lfo; });
    setValue (self, "lfo" + std::to_string (lfo) + ".shape", static_cast<float> (kLfoShapeCustom));
    actionDone (self);
    say (summary, capacity, std::to_string (shape.points.size()) + " points");
    return SYNTHYBOY_CONTENT_OK;
}

int32_t ext_content_lfo_shape_to_wavetable (const clap_plugin_t* p, uint32_t lfo, uint32_t osc, char* summary, uint32_t capacity) noexcept
{
    auto* self = toClap (p);
    if (osc < 1 || osc > 3 || lfo < 1 || lfo > static_cast<uint32_t> (kMaxContentLfoShapes))
        return SYNTHYBOY_CONTENT_ERR_ARGUMENT;
    const std::string l = "lfo" + std::to_string (lfo) + ".shape";
    const int builtIn = static_cast<int> (std::lround (valueOf (self, l)));
    const auto it = std::find_if (self->content.lfoShapes.begin(), self->content.lfoShapes.end(), [lfo] (const ContentLfoShape& s) { return s.lfo == static_cast<int> (lfo); });
    ContentLfoShape shape;
    if (builtIn == kLfoShapeCustom && it != self->content.lfoShapes.end() && it->points.size() >= 2)
        shape = *it;
    else if (builtIn >= 0 && builtIn < kNumLfoShapes)
        shape = convert::builtInLfoShape (static_cast<int> (lfo), builtIn);
    else
    {
        say (summary, capacity, "this LFO has no shape to copy");
        return SYNTHYBOY_CONTENT_ERR_STATE;
    }
    wtedit::Frame frame;
    convert::lfoShapeToFrame (shape, frame);
    wtedit::Wavetable table;
    const std::string o = "osc" + std::to_string (osc) + ".";
    bool replaced = false;
    const ContentWavetable* current = nullptr;
    for (const auto& w : self->content.wavetables)
        if (w.osc == static_cast<int> (osc))
            current = &w;
    std::vector<uint8_t> bytes;
    if (current != nullptr && static_cast<int> (std::lround (valueOf (self, o + "wavetable"))) == kWavetablePresetTable && self->contentStore.readFile (current->ref, bytes)
        && wavetableFromWav (bytes, current->ref.name, table) && table.numFrames() > 0)
    {
        const float position = std::clamp (valueOf (self, o + "frame") * 0.01f, 0.0f, 1.0f);
        table.setFrame (static_cast<size_t> (std::lround (position * static_cast<float> (table.numFrames() - 1))), frame);
        replaced = true;
    }
    else
    {
        table = wtedit::Wavetable ("LFO " + std::to_string (lfo) + " shape");
        table.setFrame (0, frame);
    }
    if (! installTable (self, static_cast<int> (osc), table))
        return SYNTHYBOY_CONTENT_ERR_STORE;
    actionDone (self);
    say (summary, capacity, replaced ? "the current frame was replaced" : "a new one-frame table");
    return SYNTHYBOY_CONTENT_OK;
}

const synthyboy_plugin_content_t s_content = {
    ext_content_count,
    ext_content_get,
    ext_content_import_file,
    ext_content_attach,
    ext_content_remove,
    ext_content_import_vital,
    ext_content_switch_to_wavetable,
    ext_content_render_osc_warp,
    ext_content_resample,
    ext_content_render_last_note,
    ext_content_wavetable_frame_to_lfo,
    ext_content_lfo_shape_to_wavetable
};

// render (offline bounce): "Use Ultra quality when rendering" (S p.315, A385)
bool ext_render_has_hard_realtime_requirement (const clap_plugin_t*) noexcept { return false; }

bool ext_render_set (const clap_plugin_t* p, clap_plugin_render_mode mode) noexcept
{
    auto* self = toClap (p);
    self->offlineRender = mode == CLAP_RENDER_OFFLINE;
    if (self->engine)
    {
        self->engine->setUltraRender (self->prefs.ultraRender && self->offlineRender); // the process call asks the host for a restart (needsPrepare)
        self->quality.realtime = ! self->offlineRender;
        self->engine->setQuality (self->quality);
    }
    return true;
}

const clap_plugin_render_t s_render = {
    ext_render_has_hard_realtime_requirement,
    ext_render_set
};

// Extension query
const void* clap_get_extension (const clap_plugin_t*, const char* id) noexcept
{
    if (id == nullptr)
        return nullptr;

    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0) return &s_note_ports;
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &s_params;
    if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &s_state;
    if (std::strcmp (id, CLAP_EXT_VOICE_INFO) == 0) return &s_voice_info;
    if (std::strcmp (id, CLAP_EXT_LATENCY) == 0) return &s_latency;
    if (std::strcmp (id, CLAP_EXT_TAIL) == 0) return &s_tail;
    if (std::strcmp (id, CLAP_EXT_RENDER) == 0) return &s_render;
    if (std::strcmp (id, SYNTHYBOY_EXT_CONTENT) == 0) return &s_content;
    if (std::strcmp (id, CLAP_EXT_REMOTE_CONTROLS) == 0 ||
        std::strcmp (id, CLAP_EXT_REMOTE_CONTROLS_COMPAT) == 0 ||
        std::strcmp (id, "clap.remote-controls") == 0) return &s_remote_controls;

    return nullptr;
}

void clap_on_main_thread (const clap_plugin_t*) noexcept {}

// -----------------------------------------------------------------------------
// Plugin Factory & Entry Point
// -----------------------------------------------------------------------------

uint32_t factory_get_plugin_count (const clap_plugin_factory_t*) noexcept
{
    return 1;
}

const clap_plugin_descriptor_t* factory_get_plugin_descriptor (const clap_plugin_factory_t*, uint32_t index) noexcept
{
    return (index == 0) ? &s_descriptor : nullptr;
}

const clap_plugin_t* factory_create_plugin (const clap_plugin_factory_t*, const clap_host_t* host, const char* plugin_id) noexcept
{
    if (plugin_id == nullptr || std::strcmp (plugin_id, kPluginId) != 0 || host == nullptr)
        return nullptr;

    auto* self = new SynthyBoyClap (host);
    self->plugin.desc = &s_descriptor;
    self->plugin.plugin_data = self;
    self->plugin.init = clap_init;
    self->plugin.destroy = clap_destroy;
    self->plugin.activate = clap_activate;
    self->plugin.deactivate = clap_deactivate;
    self->plugin.start_processing = clap_start_processing;
    self->plugin.stop_processing = clap_stop_processing;
    self->plugin.reset = clap_reset;
    self->plugin.process = clap_process;
    self->plugin.get_extension = clap_get_extension;
    self->plugin.on_main_thread = clap_on_main_thread;

    return &self->plugin;
}

const clap_plugin_factory_t s_factory = {
    factory_get_plugin_count,
    factory_get_plugin_descriptor,
    factory_create_plugin
};

bool entry_init (const char*) noexcept
{
    return true;
}

void entry_deinit (void) noexcept {}

const void* entry_get_factory (const char* factory_id) noexcept
{
    if (factory_id != nullptr && std::strcmp (factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &s_factory;
    return nullptr;
}

const clap_plugin_entry_t s_entry = {
    CLAP_VERSION,
    entry_init,
    entry_deinit,
    entry_get_factory
};
} // namespace
} // namespace synthyboy

extern "C" {

SYNTHYBOY_CLAP_API const clap_plugin_entry_t* synthyboy_clap_get_entry (void)
{
    return &synthyboy::s_entry;
}

#if ! defined (SYNTHYBOY_STATIC_ONLY)
SYNTHYBOY_CLAP_API const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    synthyboy::entry_init,
    synthyboy::entry_deinit,
    synthyboy::entry_get_factory
};
#endif

} // extern "C"
