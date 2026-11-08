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
#include "synthyboy/preset.h"
#include "synthyboy/quality.h"
#include "synthyboy/shell_features.h"
#include "synthyboy/state.h"
#include <clap/ext/remote-controls.h>

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
    ParamValues values;
    StateContent content; // the `content` section of the state (not parameters): custom LFO shapes, curves and the tuning go to the engine (setContent), main thread only
    MidiCcMap midiMap;                // MIDI learn table (G07): CC -> parameter, read on the audio thread; saved in the `shell` section
    std::vector<std::string> locks;   // module locks (G08), main thread; kept for the round trip with the editor shell
    Quality quality;
    std::vector<Event> events;
    double sampleRate = 48000.0;
    uint32_t maxBlockSize = 512;
    bool active = false;
    std::atomic<int> reportedLatency { 0 };   // the latency the host was told (ext_latency_get)
    bool processing = false;

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
    if (self->engine)
        self->engine->prepare (self->sampleRate, static_cast<int> (self->maxBlockSize), self->quality);
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
                    break;
                }
                case CLAP_EVENT_NOTE_EXPRESSION:
                {
                    // per-note expressions (MPE through CLAP, docs/ui-design/13-new-keys.md 4.7): TUNING (semitones), PRESSURE, BRIGHTNESS
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
                    else
                        break; // volume, pan, vibrato, expression: not used
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
                transport.bpm = t->tempo;
            if ((t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0)
                transport.songPosBeats = static_cast<double> (t->song_pos_beats) / static_cast<double> (CLAP_BEATTIME_FACTOR);
            transport.playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        }
        self->engine->setTransport (transport);
    }

    // 2. Render audio
    if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32 != nullptr)
    {
        float* outL = process->audio_outputs[0].data32[0];
        float* outR = (process->audio_outputs[0].channel_count > 1) ? process->audio_outputs[0].data32[1] : outL;

        self->engine->process (self->events.data(), static_cast<int> (self->events.size()),
                               outL, outR, static_cast<int> (process->frames_count));
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
        else if (! self->locks.empty())
        {
            keepLockedValues (self->values, before, self->locks);
            StateContent merged = self->content;
            keepLockedContent (merged, contentBefore, self->locks);
            if (validateContent (merged))
                self->content = std::move (merged);
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
