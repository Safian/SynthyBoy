// SynthyBoy - tests: native JUCE-free CLAP shell and static entry point (docs/ARCHITECTURE.md 4).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include <clap/ext/remote-controls.h>
#include <clap/ext/render.h>

#include "synthyboy/engine.h"
#include "synthyboy/kick_pages.h"
#include "synthyboy/params.h"
#include "synthyboy/preferences.h"
#include "synthyboy/shell_features.h"
#include "synthyboy/state.h"
#include "synthyboy_clap.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
const clap_host_t s_mockHost = {
    CLAP_VERSION,
    nullptr,
    "synthyboy-test-host",
    "Safian",
    "https://github.com/Safian/SynthyBoy",
    "1.0.0",
    [] (const clap_host_t*, const char*) -> const void* { return nullptr; },
    [] (const clap_host_t*) {},
    [] (const clap_host_t*) {},
    [] (const clap_host_t*) {}
};

struct MemOStream
{
    clap_ostream_t stream {};
    std::vector<uint8_t> bytes;

    MemOStream()
    {
        stream.ctx = this;
        stream.write = [] (const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t {
            auto* self = static_cast<MemOStream*> (s->ctx);
            const auto* p = static_cast<const uint8_t*> (buffer);
            self->bytes.insert (self->bytes.end(), p, p + size);
            return static_cast<int64_t> (size);
        };
    }
};

struct MemIStream
{
    clap_istream_t stream {};
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    MemIStream (const void* d, size_t sz) : data (static_cast<const uint8_t*> (d)), size (sz), pos (0)
    {
        stream.ctx = this;
        stream.read = [] (const clap_istream_t* s, void* buffer, uint64_t count) -> int64_t {
            auto* self = static_cast<MemIStream*> (s->ctx);
            if (self->pos >= self->size)
                return 0;
            const size_t available = self->size - self->pos;
            const size_t toRead = std::min (available, static_cast<size_t> (count));
            std::memcpy (buffer, self->data + self->pos, toRead);
            self->pos += toRead;
            return static_cast<int64_t> (toRead);
        };
    }
};

struct SimpleEvents
{
    std::vector<clap_event_header_t*> events;

    clap_input_events_t inEvents()
    {
        clap_input_events_t in {};
        in.ctx = this;
        in.size = [] (const clap_input_events_t* l) -> uint32_t {
            return static_cast<uint32_t> (static_cast<SimpleEvents*> (l->ctx)->events.size());
        };
        in.get = [] (const clap_input_events_t* l, uint32_t index) -> const clap_event_header_t* {
            return static_cast<SimpleEvents*> (l->ctx)->events[index];
        };
        return in;
    }
};
} // namespace

TEST (native_clap_entry_and_descriptor)
{
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    CHECK (entry != nullptr);
    CHECK_EQ (entry->clap_version.major, CLAP_VERSION.major);
    CHECK_EQ (entry->clap_version.minor, CLAP_VERSION.minor);
    CHECK (entry->init ("synthyboy_static"));

    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    CHECK (factory != nullptr);
    CHECK_EQ (factory->get_plugin_count (factory), 1u);

    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor (factory, 0);
    CHECK (desc != nullptr);
    CHECK_EQ (std::string (desc->id), "com.safian.synthyboy");
    CHECK_EQ (std::string (desc->name), "SynthyBoy");
    CHECK_EQ (std::string (desc->version), "0.1.0");

    entry->deinit();
}

TEST (native_clap_extensions_and_parameter_parity)
{
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    CHECK (entry != nullptr);
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    CHECK (factory != nullptr);

    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
    CHECK (plugin != nullptr);
    CHECK (plugin->init (plugin));

    // 1. Audio ports
    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
    CHECK (audioPorts != nullptr);
    CHECK_EQ (audioPorts->count (plugin, true), 0u);
    CHECK_EQ (audioPorts->count (plugin, false), 1u);
    clap_audio_port_info_t ainfo {};
    CHECK (audioPorts->get (plugin, 0, false, &ainfo));
    CHECK_EQ (ainfo.channel_count, 2u);
    CHECK_EQ (ainfo.flags & CLAP_AUDIO_PORT_IS_MAIN, CLAP_AUDIO_PORT_IS_MAIN);

    // 2. Note ports
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS));
    CHECK (notePorts != nullptr);
    CHECK_EQ (notePorts->count (plugin, true), 1u);
    CHECK_EQ (notePorts->count (plugin, false), 0u);
    clap_note_port_info_t ninfo {};
    CHECK (notePorts->get (plugin, 0, true, &ninfo));
    CHECK (ninfo.supported_dialects & CLAP_NOTE_DIALECT_CLAP);

    // 3. Parameters parity with paramTable()
    const auto* params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    CHECK (params != nullptr);
    CHECK_EQ (params->count (plugin), static_cast<uint32_t> (paramCount()));

    for (uint32_t i = 0; i < params->count (plugin); ++i)
    {
        clap_param_info_t pinfo {};
        CHECK (params->get_info (plugin, i, &pinfo));
        const auto& tableInfo = paramTable()[i];
        CHECK_EQ (pinfo.id, tableInfo.id);
        CHECK_EQ (std::string (pinfo.name), tableInfo.name);
        CHECK_EQ (std::string (pinfo.module), tableInfo.group);
        CHECK_NEAR (pinfo.min_value, tableInfo.min, 0.0001);
        CHECK_NEAR (pinfo.max_value, tableInfo.max, 0.0001);
        CHECK_NEAR (pinfo.default_value, tableInfo.def, 0.0001);

        double curVal = 0.0;
        CHECK (params->get_value (plugin, pinfo.id, &curVal));
        CHECK_NEAR (curVal, tableInfo.def, 0.0001);

        char textBuf[64] = {};
        CHECK (params->value_to_text (plugin, pinfo.id, curVal, textBuf, sizeof textBuf));
        CHECK (textBuf[0] != '\0');

        double parsedVal = 0.0;
        CHECK (params->text_to_value (plugin, pinfo.id, textBuf, &parsedVal));
        CHECK_NEAR (parsedVal, curVal, 0.1);
    }

    // 4. Voice info
    const auto* voiceInfo = static_cast<const clap_plugin_voice_info_t*> (plugin->get_extension (plugin, CLAP_EXT_VOICE_INFO));
    CHECK (voiceInfo != nullptr);
    clap_voice_info_t vinfo {};
    CHECK (voiceInfo->get (plugin, &vinfo));
    CHECK (vinfo.voice_capacity > 0);

    // 5. Remote controls extension
    const auto* remoteControls = static_cast<const clap_plugin_remote_controls_t*> (
        plugin->get_extension (plugin, CLAP_EXT_REMOTE_CONTROLS));
    CHECK (remoteControls != nullptr);
    CHECK_EQ (remoteControls->count (plugin), 160u);

    clap_remote_controls_page_t firstPage {};
    CHECK (remoteControls->get (plugin, 0, &firstPage));
    CHECK_EQ (std::string (firstPage.section_name), "Perf");
    CHECK_EQ (std::string (firstPage.page_name), "Macros & Performance");
    CHECK_EQ (firstPage.page_id, 0u);
    CHECK (! firstPage.is_for_preset);
    const ParamInfo* macro1Param = findParamByKey ("macro.1");
    CHECK (macro1Param != nullptr);
    CHECK_EQ (firstPage.param_ids[0], macro1Param->id);

    plugin->destroy (plugin);
}

TEST (kick_remote_control_pages_and_reachability)
{
    CHECK_EQ (kickPageCount(), 160u);
    const Page* pages = kickPageTable();
    CHECK (pages != nullptr);

    std::unordered_set<std::string> allReachableKeys;

    for (size_t i = 0; i < kickPageCount(); ++i)
    {
        const Page& page = pages[i];
        CHECK (page.id != nullptr && page.id[0] != '\0');
        CHECK (page.title != nullptr && page.title[0] != '\0');
        CHECK (page.section != nullptr && page.section[0] != '\0');
        CHECK (page.count >= 1 && page.count <= 8);

        for (size_t k = 0; k < page.count; ++k)
        {
            if (page.keys[k] == nullptr)
                continue;
            const ParamInfo* info = findParamByKey (page.keys[k]);
            CHECK (info != nullptr);
            allReachableKeys.insert (page.keys[k]);
        }

        if (page.moreKeys != nullptr)
        {
            for (size_t m = 0; m < page.moreCount; ++m)
            {
                CHECK (page.moreKeys[m] != nullptr);
                const ParamInfo* info = findParamByKey (page.moreKeys[m]);
                CHECK (info != nullptr);
                allReachableKeys.insert (page.moreKeys[m]);
            }
        }
    }

    // Asserts all 2637 keys in paramTable() are reachable
    CHECK_EQ (allReachableKeys.size(), 2637u);
    CHECK_EQ (allReachableKeys.size(), paramCount());

    // Macro defaults check
    CHECK_EQ (categoryMacroDefaultCount(), 7u);
    for (size_t c = 0; c < categoryMacroDefaultCount(); ++c)
    {
        const auto& cat = categoryMacroDefaults()[c];
        CHECK (cat.category != nullptr && cat.category[0] != '\0');
        for (size_t m = 0; m < 4; ++m)
        {
            const auto& mac = cat.macros[m];
            CHECK (mac.macroKey != nullptr);
            CHECK (findParamByKey (mac.macroKey) != nullptr);
            CHECK (mac.label != nullptr && std::strlen (mac.label) <= 12);
            CHECK (mac.targetCount >= 1);
            for (size_t t = 0; t < mac.targetCount; ++t)
            {
                CHECK (mac.targets[t].destination != nullptr);
                CHECK (findParamByKey (mac.targets[t].destination) != nullptr);
            }
        }
    }
}

TEST (native_clap_audio_render_and_state)
{
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    CHECK (entry != nullptr);
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    CHECK (factory != nullptr);

    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
    CHECK (plugin != nullptr);
    CHECK (plugin->init (plugin));
    CHECK (plugin->activate (plugin, 48000.0, 32, 512));
    CHECK (plugin->start_processing (plugin));

    const auto* stateExt = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
    const auto* paramsExt = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    CHECK (stateExt != nullptr && paramsExt != nullptr);

    // Allocate audio buffers
    std::vector<float> bufL (512, 0.0f);
    std::vector<float> bufR (512, 0.0f);
    float* channelData[2] = { bufL.data(), bufR.data() };
    clap_audio_buffer_t audioOut {};
    audioOut.data32 = channelData;
    audioOut.channel_count = 2;
    audioOut.constant_mask = 0;
    audioOut.latency = 0;

    clap_process_t proc {};
    proc.steady_time = 0;
    proc.frames_count = 512;
    proc.transport = nullptr;
    proc.audio_inputs = nullptr;
    proc.audio_inputs_count = 0;
    proc.audio_outputs = &audioOut;
    proc.audio_outputs_count = 1;
    proc.in_events = nullptr;
    proc.out_events = nullptr;

    // 1. Process without notes: should be silent
    proc.frames_count = 512;
    plugin->process (plugin, &proc);
    float silentPeak = 0.0f;
    for (int i = 0; i < 512; ++i)
        silentPeak = std::max ({ silentPeak, std::abs (bufL[i]), std::abs (bufR[i]) });
    CHECK_EQ (silentPeak, 0.0f);

    // 2. Note-On event: A4 (key 69)
    clap_event_note_t noteOnEv {};
    noteOnEv.header.size = sizeof noteOnEv;
    noteOnEv.header.time = 0;
    noteOnEv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    noteOnEv.header.type = CLAP_EVENT_NOTE_ON;
    noteOnEv.header.flags = 0;
    noteOnEv.note_id = 1;
    noteOnEv.port_index = 0;
    noteOnEv.channel = 0;
    noteOnEv.key = 69;
    noteOnEv.velocity = 0.8;

    SimpleEvents evs;
    evs.events.push_back (&noteOnEv.header);
    clap_input_events_t inEvs = evs.inEvents();
    proc.in_events = &inEvs;

    plugin->process (plugin, &proc);

    float activePeak = 0.0f;
    for (int i = 0; i < 512; ++i)
    {
        activePeak = std::max ({ activePeak, std::abs (bufL[i]), std::abs (bufR[i]) });
        CHECK (std::isfinite (bufL[i]) && std::isfinite (bufR[i]));
    }
    CHECK (activePeak > 0.01f);

    // 3. State save and restore
    MemOStream outStream;
    CHECK (stateExt->save (plugin, &outStream.stream));
    CHECK (! outStream.bytes.empty());

    // Modify a parameter via event
    clap_event_param_value_t paramEv {};
    paramEv.header.size = sizeof paramEv;
    paramEv.header.time = 0;
    paramEv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    paramEv.header.type = CLAP_EVENT_PARAM_VALUE;
    paramEv.param_id = kParamMasterVolume;
    paramEv.value = -3.5;
    SimpleEvents modEv;
    modEv.events.push_back (&paramEv.header);
    clap_input_events_t modIn = modEv.inEvents();
    paramsExt->flush (plugin, &modIn, nullptr);

    double valAfterMod = 0.0;
    CHECK (paramsExt->get_value (plugin, kParamMasterVolume, &valAfterMod));
    CHECK_NEAR (valAfterMod, -3.5, 0.01);

    // Restore state from stream
    MemIStream inStream (outStream.bytes.data(), outStream.bytes.size());
    CHECK (stateExt->load (plugin, &inStream.stream));

    double valAfterRestore = 0.0;
    CHECK (paramsExt->get_value (plugin, kParamMasterVolume, &valAfterRestore));
    CHECK_NEAR (valAfterRestore, -12.0, 0.01);

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
}

// WP1 of docs/ui-design/13-new-keys.md: the shell turns CLAP_EVENT_MIDI (CC 64), the note-off velocity, CLAP note expressions and the
// transport into engine events. Audible proof: with the pedal down a released note keeps sounding until the pedal is lifted.
TEST (native_clap_sustain_pedal_note_expression_and_transport)
{
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
    CHECK (plugin != nullptr && plugin->init (plugin));
    CHECK (plugin->activate (plugin, 48000.0, 32, 512));
    CHECK (plugin->start_processing (plugin));

    std::vector<float> bufL (512, 0.0f), bufR (512, 0.0f);
    float* channelData[2] = { bufL.data(), bufR.data() };
    clap_audio_buffer_t audioOut {};
    audioOut.data32 = channelData;
    audioOut.channel_count = 2;
    clap_event_transport_t transport {};
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    transport.tempo = 90.0;
    clap_process_t proc {};
    proc.frames_count = 512;
    proc.transport = &transport;
    proc.audio_outputs = &audioOut;
    proc.audio_outputs_count = 1;

    const auto run = [&] (const std::vector<clap_event_header_t*>& events, int blocks) -> float
    {
        SimpleEvents list;
        list.events = events;
        clap_input_events_t in = list.inEvents();
        double sum = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            proc.in_events = b == 0 ? &in : nullptr;
            plugin->process (plugin, &proc);
            for (size_t i = 0; i < 512; ++i)
                sum += static_cast<double> (bufL[i]) * bufL[i];
        }
        return static_cast<float> (std::sqrt (sum / (512.0 * blocks)));
    };

    clap_event_midi_t pedal {};
    pedal.header.size = sizeof pedal;
    pedal.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    pedal.header.type = CLAP_EVENT_MIDI;
    pedal.data[0] = 0xB0;
    pedal.data[1] = 64;
    pedal.data[2] = 127;

    clap_event_note_t on {};
    on.header.size = sizeof on;
    on.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    on.header.type = CLAP_EVENT_NOTE_ON;
    on.note_id = 5;
    on.key = 69;
    on.velocity = 0.9;
    clap_event_note_t off = on;
    off.header.type = CLAP_EVENT_NOTE_OFF;
    off.velocity = 0.7;
    clap_event_note_t offLater = off;
    offLater.header.time = 100;

    run ({ &pedal.header, &on.header, &offLater.header }, 1);
    const float held = run ({}, 400); // about 4 s later (the default patch releases in well under a second)
    CHECK (held > 0.005f);

    clap_event_note_expression_t bend {};
    bend.header.size = sizeof bend;
    bend.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    bend.header.type = CLAP_EVENT_NOTE_EXPRESSION;
    bend.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
    bend.note_id = 5;
    bend.key = -1;
    bend.channel = -1;
    bend.port_index = -1;
    bend.value = 12.0;
    run ({ &bend.header }, 1);
    CHECK (run ({}, 40) > 0.005f); // still sounding, now an octave up

    pedal.data[2] = 0; // pedal up: the held note is released
    run ({ &pedal.header }, 1);
    run ({}, 100); // the release tail
    CHECK (run ({}, 100) < 1e-4f);

    // the same without a pedal: the note-off ends the note
    run ({ &on.header, &off.header }, 1);
    run ({}, 100);
    CHECK (run ({}, 100) < 1e-4f);

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
}

// G07 / G02 / G08 (docs/ui-design/15-shell-features.md): the native shell reads the MIDI map of the `shell` state section, applies mapped
// controllers and reports them to the host; a new instance starts from the user default; locks survive a preset-like state load.
namespace
{
struct OutCapture
{
    std::vector<clap_event_param_value_t> events;
    clap_output_events_t list {};
    OutCapture()
    {
        list.ctx = this;
        list.try_push = [] (const clap_output_events_t* l, const clap_event_header_t* h) -> bool {
            if (h->type != CLAP_EVENT_PARAM_VALUE)
                return false;
            static_cast<OutCapture*> (l->ctx)->events.push_back (*reinterpret_cast<const clap_event_param_value_t*> (h));
            return true;
        };
    }
};

void setUserDir (const char* value)
{
   #if defined(_WIN32)
    _putenv_s ("SYNTHYBOY_USER_DIR", value);
   #else
    if (value[0] != '\0')
        sbtest::setEnv ("SYNTHYBOY_USER_DIR", value);
    else
        sbtest::unsetEnv ("SYNTHYBOY_USER_DIR");
   #endif
}

bool loadStateText (const clap_plugin_t* plugin, const std::string& text)
{
    const auto* stateExt = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
    MemIStream in (text.data(), text.size());
    return stateExt->load (plugin, &in.stream);
}

double paramValue (const clap_plugin_t* plugin, uint32_t id)
{
    const auto* paramsExt = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    double v = -9999.0;
    paramsExt->get_value (plugin, id, &v);
    return v;
}

void processEvents (const clap_plugin_t* plugin, const std::vector<clap_event_header_t*>& events, clap_output_events_t* out)
{
    std::vector<float> bufL (64, 0.0f), bufR (64, 0.0f);
    float* channelData[2] = { bufL.data(), bufR.data() };
    clap_audio_buffer_t audioOut {};
    audioOut.data32 = channelData;
    audioOut.channel_count = 2;
    clap_process_t proc {};
    proc.frames_count = 64;
    proc.audio_outputs = &audioOut;
    proc.audio_outputs_count = 1;
    SimpleEvents list;
    list.events = events;
    clap_input_events_t in = list.inEvents();
    proc.in_events = &in;
    proc.out_events = out;
    plugin->process (plugin, &proc);
}

clap_event_midi_t ccEvent (int cc, int value)
{
    clap_event_midi_t m {};
    m.header.size = sizeof m;
    m.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    m.header.type = CLAP_EVENT_MIDI;
    m.data[0] = 0xB0;
    m.data[1] = static_cast<uint8_t> (cc);
    m.data[2] = static_cast<uint8_t> (value);
    return m;
}

int indexOfKey (const char* key)
{
    return findParamIndex (findParamByKey (key)->id);
}
} // namespace

TEST (native_clap_midi_map_in_state_drives_parameters)
{
    setUserDir ((std::filesystem::temp_directory_path() / "sb_clap_nodefault").string().c_str());
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
    CHECK (plugin != nullptr && plugin->init (plugin));
    CHECK (plugin->activate (plugin, 48000.0, 32, 512));
    CHECK (plugin->start_processing (plugin));

    ShellState shell;
    shell.midiCc = { { 21, "master.volume" } };
    shell.locks = { "osc1" };
    CHECK (loadStateText (plugin, withShellSection (saveState (ParamValues::defaults()), shell)));

    const ParamInfo* volume = findParamByKey ("master.volume");
    CHECK (volume != nullptr);
    if (volume == nullptr)
        return;

    OutCapture out;
    auto hi = ccEvent (21, 127);
    processEvents (plugin, { &hi.header }, &out.list);
    CHECK_NEAR (paramValue (plugin, volume->id), volume->max, 0.01);
    CHECK_EQ (out.events.size(), static_cast<size_t> (1));
    if (! out.events.empty())
    {
        CHECK_EQ (out.events[0].param_id, volume->id);
        CHECK_NEAR (out.events[0].value, volume->max, 0.01);
    }

    auto lo = ccEvent (21, 0);
    processEvents (plugin, { &lo.header }, &out.list);
    CHECK_NEAR (paramValue (plugin, volume->id), volume->min, 0.01);

    // an unmapped controller and a null out list do nothing / do not crash
    auto other = ccEvent (22, 100);
    processEvents (plugin, { &other.header }, nullptr);
    auto again = ccEvent (21, 127);
    processEvents (plugin, { &again.header }, nullptr);
    CHECK_NEAR (paramValue (plugin, volume->id), volume->max, 0.01);

    // the map and the locks are saved with the state
    const auto* stateExt = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
    MemOStream saved;
    CHECK (stateExt->save (plugin, &saved.stream));
    const std::string text (saved.bytes.begin(), saved.bytes.end());
    ShellState back;
    CHECK (loadShellSection (text.data(), text.size(), back) == ShellStatus::ok);
    CHECK (back == shell);

    // a document without the section is a preset change: the locked module osc1 keeps its values
    auto current = ParamValues::defaults();
    current.plain[indexOfKey ("osc1.level")] = 0.123f;
    CHECK (loadStateText (plugin, withShellSection (saveState (current), shell)));
    auto preset = ParamValues::defaults();
    preset.plain[indexOfKey ("osc1.level")] = 0.9f;
    preset.plain[indexOfKey ("osc2.level")] = 0.4f;
    CHECK (loadStateText (plugin, saveState (preset)));
    CHECK_NEAR (paramValue (plugin, findParamByKey ("osc1.level")->id), 0.123, 1.0e-4);
    CHECK_NEAR (paramValue (plugin, findParamByKey ("osc2.level")->id), 0.4, 1.0e-4);

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    setUserDir ("");
}

TEST (native_clap_new_instance_starts_from_the_user_default)
{
    const auto dir = std::filesystem::temp_directory_path() / "sb_clap_default";
    std::filesystem::remove_all (dir);
    setUserDir (dir.string().c_str());

    auto values = ParamValues::defaults();
    values.plain[indexOfKey ("master.volume")] = -5.0f;
    ShellState shell;
    shell.midiCc = { { 30, "filter1.cutoff" } };
    CHECK (saveUserDefault (dir.string(), withShellSection (saveState (values), shell)));

    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
    CHECK (plugin != nullptr && plugin->init (plugin));
    CHECK_NEAR (paramValue (plugin, findParamByKey ("master.volume")->id), -5.0, 1.0e-4);

    CHECK (plugin->activate (plugin, 48000.0, 32, 512));
    CHECK (plugin->start_processing (plugin));
    auto cc = ccEvent (30, 127);
    processEvents (plugin, { &cc.header }, nullptr);
    const ParamInfo* cutoff = findParamByKey ("filter1.cutoff");
    CHECK_NEAR (paramValue (plugin, cutoff->id), cutoff->max, 0.5);
    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);

    CHECK (clearUserDefault (dir.string()));
    const clap_plugin_t* fresh = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
    CHECK (fresh != nullptr && fresh->init (fresh));
    CHECK_NEAR (paramValue (fresh, findParamByKey ("master.volume")->id), -12.0, 1.0e-4);
    fresh->destroy (fresh);
    std::filesystem::remove_all (dir);
    setUserDir ("");
}

// GROUPA2 (WP-02): the native shell reads the preferences (MPE defaults of a new instance, silence on stop, ultra render) and the default MIDI map
namespace
{
/** Renders `blocks` blocks of 256 frames with the transport running or stopped; returns the peak of the left channel of the last block. */
float runBlocks (const clap_plugin_t* plugin, const std::vector<clap_event_header_t*>& events, int blocks, bool transportKnown, bool playing, float* rmsOut = nullptr)
{
    std::vector<float> bufL (256, 0.0f), bufR (256, 0.0f);
    float* channelData[2] = { bufL.data(), bufR.data() };
    clap_audio_buffer_t audioOut {};
    audioOut.data32 = channelData;
    audioOut.channel_count = 2;
    clap_event_transport_t transport {};
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0u);
    transport.tempo = 120.0;
    clap_process_t proc {};
    proc.frames_count = 256;
    proc.transport = transportKnown ? &transport : nullptr;
    proc.audio_outputs = &audioOut;
    proc.audio_outputs_count = 1;
    SimpleEvents list;
    list.events = events;
    clap_input_events_t in = list.inEvents();
    float peak = 0.0f;
    double sum = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        proc.in_events = b == 0 ? &in : nullptr;
        plugin->process (plugin, &proc);
        peak = 0.0f;
        for (float v : bufL)
        {
            peak = std::max (peak, std::fabs (v));
            sum += static_cast<double> (v) * v;
        }
    }
    if (rmsOut != nullptr)
        *rmsOut = static_cast<float> (std::sqrt (sum / (256.0 * blocks)));
    return peak;
}

const clap_plugin_t* createPlugin()
{
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
    if (plugin != nullptr)
        plugin->init (plugin);
    return plugin;
}
} // namespace

TEST (native_clap_preferences_mpe_defaults_default_midi_map_and_preset_changes)
{
    const auto dir = std::filesystem::temp_directory_path() / "sb_clap_prefs";
    std::filesystem::remove_all (dir);
    setUserDir (dir.string().c_str());

    Preferences prefs;
    prefs.mpeByDefault = true;
    prefs.mpeBendToX = true;
    CHECK (savePreferences (dir.string(), prefs));
    CHECK (saveDefaultMidiMap (dir.string(), { { 30, "filter1.cutoff" } }));

    const clap_plugin_t* plugin = createPlugin();
    CHECK (plugin != nullptr);
    if (plugin == nullptr)
        return;
    // a new instance: MPE on and pitch bend also to X from the preferences, Y stays unipolar
    CHECK_NEAR (paramValue (plugin, findParamByKey ("master.mpe.on")->id), 1.0, 1.0e-6);
    CHECK_NEAR (paramValue (plugin, findParamByKey ("master.mpe.bend_to_x")->id), 1.0, 1.0e-6);
    CHECK_NEAR (paramValue (plugin, findParamByKey ("master.mpe.y_bipolar")->id), 0.0, 1.0e-6);
    // and the default MIDI map of the new instance
    CHECK (plugin->activate (plugin, 48000.0, 32, 512));
    CHECK (plugin->start_processing (plugin));
    auto cc = ccEvent (30, 127);
    processEvents (plugin, { &cc.header }, nullptr);
    const ParamInfo* cutoff = findParamByKey ("filter1.cutoff");
    CHECK_NEAR (paramValue (plugin, cutoff->id), cutoff->max, 0.5);

    // a session brings its own map and keeps it; a preset change (no shell section) loads the default map again
    ShellState shell;
    shell.midiCc = { { 21, "master.volume" } };
    CHECK (loadStateText (plugin, withShellSection (saveState (ParamValues::defaults()), shell)));
    auto session = ccEvent (21, 127);
    processEvents (plugin, { &session.header }, nullptr);
    CHECK_NEAR (paramValue (plugin, findParamByKey ("master.volume")->id), findParamByKey ("master.volume")->max, 0.01);
    CHECK (loadStateText (plugin, saveState (ParamValues::defaults())));
    auto lo = ccEvent (30, 0);
    processEvents (plugin, { &lo.header }, nullptr);
    CHECK_NEAR (paramValue (plugin, cutoff->id), cutoff->min, 0.5);
    auto oldMap = ccEvent (21, 127);
    const double volumeBefore = paramValue (plugin, findParamByKey ("master.volume")->id);
    processEvents (plugin, { &oldMap.header }, nullptr);
    CHECK_NEAR (paramValue (plugin, findParamByKey ("master.volume")->id), volumeBefore, 1.0e-6);   // CC 21 is no longer mapped
    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);

    // a default patch the user saved wins over the MPE preferences of a new instance
    auto values = ParamValues::defaults();
    CHECK (saveUserDefault (dir.string(), withShellSection (saveState (values), ShellState {})));
    const clap_plugin_t* withDefault = createPlugin();
    CHECK_NEAR (paramValue (withDefault, findParamByKey ("master.mpe.on")->id), 0.0, 1.0e-6);
    withDefault->destroy (withDefault);
    std::filesystem::remove_all (dir);
    setUserDir ("");
}

TEST (native_clap_silences_notes_and_tails_when_the_host_stops)
{
    for (const bool silence : { true, false })
    {
        const auto dir = std::filesystem::temp_directory_path() / (silence ? "sb_clap_stop_on" : "sb_clap_stop_off");
        std::filesystem::remove_all (dir);
        setUserDir (dir.string().c_str());
        Preferences prefs;
        prefs.silenceOnStop = silence;
        CHECK (savePreferences (dir.string(), prefs));

        const clap_plugin_t* plugin = createPlugin();
        CHECK (plugin != nullptr && plugin->activate (plugin, 48000.0, 32, 512) && plugin->start_processing (plugin));
        auto patch = ParamValues::defaults();
        patch.plain[indexOfKey ("reverb.on")] = 1.0f;
        patch.plain[indexOfKey ("reverb.mix")] = 100.0f;
        patch.plain[indexOfKey ("env1.release")] = 4000.0f;
        CHECK (loadStateText (plugin, saveState (patch)));

        clap_event_note_t on {};
        on.header.size = sizeof on;
        on.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        on.header.type = CLAP_EVENT_NOTE_ON;
        on.note_id = 1;
        on.key = 60;
        on.velocity = 0.9;
        float rms = 0.0f;
        runBlocks (plugin, { &on.header }, 1, true, true);
        runBlocks (plugin, {}, 60, true, true, &rms);         // the transport runs, the note is held
        CHECK (rms > 0.01f);
        // the transport stops: the note ends and the tail is cleared (only with the preference), a block later it is quiet
        runBlocks (plugin, {}, 1, true, false);
        const float after = runBlocks (plugin, {}, 4, true, false, &rms);
        if (silence)
            CHECK (rms < 1.0e-4f && after < 1.0e-3f);
        else
            CHECK (rms > 0.01f);
        plugin->stop_processing (plugin);
        plugin->deactivate (plugin);
        plugin->destroy (plugin);
        std::filesystem::remove_all (dir);
    }
    // a host that gives no transport at all never stops
    const auto dir = std::filesystem::temp_directory_path() / "sb_clap_stop_none";
    std::filesystem::remove_all (dir);
    setUserDir (dir.string().c_str());
    const clap_plugin_t* plugin = createPlugin();
    CHECK (plugin != nullptr && plugin->activate (plugin, 48000.0, 32, 512) && plugin->start_processing (plugin));
    clap_event_note_t on {};
    on.header.size = sizeof on;
    on.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    on.header.type = CLAP_EVENT_NOTE_ON;
    on.note_id = 1;
    on.key = 60;
    on.velocity = 0.9;
    float rms = 0.0f;
    runBlocks (plugin, { &on.header }, 1, false, false);
    runBlocks (plugin, {}, 20, false, false, &rms);
    CHECK (rms > 0.01f);
    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    std::filesystem::remove_all (dir);
    setUserDir ("");
}

TEST (native_clap_offline_render_runs_at_ultra_quality_when_the_preference_is_on)
{
    const auto render = [] (bool ultraPreference, bool offline)
    {
        const auto dir = std::filesystem::temp_directory_path() / "sb_clap_ultra";
        std::filesystem::remove_all (dir);
        setUserDir (dir.string().c_str());
        Preferences prefs;
        prefs.ultraRender = ultraPreference;
        savePreferences (dir.string(), prefs);
        const clap_plugin_t* plugin = createPlugin();
        const auto* ext = static_cast<const clap_plugin_render_t*> (plugin->get_extension (plugin, CLAP_EXT_RENDER));
        CHECK (ext != nullptr);
        if (ext != nullptr)
        {
            CHECK (! ext->has_hard_realtime_requirement (plugin));
            CHECK (ext->set (plugin, offline ? CLAP_RENDER_OFFLINE : CLAP_RENDER_REALTIME));
        }
        plugin->activate (plugin, 48000.0, 32, 512);
        plugin->start_processing (plugin);
        clap_event_note_t on {};
        on.header.size = sizeof on;
        on.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        on.header.type = CLAP_EVENT_NOTE_ON;
        on.note_id = 1;
        on.key = 100;
        on.velocity = 1.0;
        std::vector<float> left;
        std::vector<float> bufL (256, 0.0f), bufR (256, 0.0f);
        float* channelData[2] = { bufL.data(), bufR.data() };
        clap_audio_buffer_t audioOut {};
        audioOut.data32 = channelData;
        audioOut.channel_count = 2;
        clap_process_t proc {};
        proc.frames_count = 256;
        proc.audio_outputs = &audioOut;
        proc.audio_outputs_count = 1;
        SimpleEvents list;
        list.events = { &on.header };
        clap_input_events_t in = list.inEvents();
        for (int b = 0; b < 40; ++b)
        {
            proc.in_events = b == 0 ? &in : nullptr;
            plugin->process (plugin, &proc);
            left.insert (left.end(), bufL.begin(), bufL.end());
        }
        plugin->stop_processing (plugin);
        plugin->deactivate (plugin);
        plugin->destroy (plugin);
        std::filesystem::remove_all (dir);
        setUserDir ("");
        return left;
    };
    const auto live = render (true, false);        // the preference is on, the host plays live: normal quality
    const auto plain = render (false, false);
    const auto bounce = render (true, true);       // the host renders offline: ultra
    const auto offlineOnly = render (false, true); // offline without the preference: normal quality
    CHECK (live == plain);
    CHECK (offlineOnly == plain);
    CHECK (bounce != plain);
}

// Rank 13 verification: shell tail continuation on voice end and mono output downmixing.
// Reuses helper types, includes, set-up calls and check macros from native_clap_audio_render_and_state
// and native_clap_silences_notes_and_tails_when_the_host_stops.
TEST (native_clap_tail_continuation_and_mono_downmix)
{
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    CHECK (entry != nullptr);
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    CHECK (factory != nullptr);

    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
    CHECK (plugin != nullptr);
    CHECK (plugin->init (plugin));
    CHECK (plugin->activate (plugin, 48000.0, 32, 512));
    CHECK (plugin->start_processing (plugin));

    // Part 1: Tail continuation when activeVoices() drops to 0 while FX tail is sounding
    {
        const auto* paramsExt = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        CHECK (paramsExt != nullptr);
        const auto* voiceExt = static_cast<const clap_plugin_voice_info_t*> (plugin->get_extension (plugin, CLAP_EXT_VOICE_INFO));
        CHECK (voiceExt != nullptr);

        auto setParamClap = [&] (const char* key, double val) {
            const ParamInfo* info = findParamByKey (key);
            CHECK (info != nullptr);
            clap_event_param_value_t ev {};
            ev.header.size = sizeof ev;
            ev.header.time = 0;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type = CLAP_EVENT_PARAM_VALUE;
            ev.param_id = info->id;
            ev.value = val;
            SimpleEvents se;
            se.events.push_back (&ev.header);
            clap_input_events_t inEvs = se.inEvents();
            paramsExt->flush (plugin, &inEvs, nullptr);
        };

        // Switch on reverb with decay >= 2s and wet mix >= 50% via CLAP params interface BEFORE the note
        setParamClap ("reverb.on", 1.0);
        setParamClap ("reverb.mix", 100.0);
        setParamClap ("reverb.decay", 3000.0);

        std::vector<float> bufL (512, 0.0f);
        std::vector<float> bufR (512, 0.0f);
        float* channelData[2] = { bufL.data(), bufR.data() };
        clap_audio_buffer_t audioOut {};
        audioOut.data32 = channelData;
        audioOut.channel_count = 2;

        clap_process_t proc {};
        proc.frames_count = 512;
        proc.audio_outputs = &audioOut;
        proc.audio_outputs_count = 1;

        // Play a note of at least 0.1 s at velocity 0.9 (10 blocks of 512 frames @ 48 kHz = 5120 frames = ~0.1067 s)
        clap_event_note_t noteOnEv {};
        noteOnEv.header.size = sizeof noteOnEv;
        noteOnEv.header.time = 0;
        noteOnEv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        noteOnEv.header.type = CLAP_EVENT_NOTE_ON;
        noteOnEv.note_id = 1;
        noteOnEv.key = 60;
        noteOnEv.velocity = 0.9;

        SimpleEvents onEvs;
        onEvs.events.push_back (&noteOnEv.header);
        clap_input_events_t inOn = onEvs.inEvents();
        proc.in_events = &inOn;
        plugin->process (plugin, &proc);

        proc.in_events = nullptr;
        for (int b = 1; b < 10; ++b)
            plugin->process (plugin, &proc);

        // Note Off
        clap_event_note_t noteOffEv = noteOnEv;
        noteOffEv.header.time = 0;
        noteOffEv.header.type = CLAP_EVENT_NOTE_OFF;
        noteOffEv.velocity = 0.0;

        SimpleEvents offEvs;
        offEvs.events.push_back (&noteOffEv.header);
        clap_input_events_t inOff = offEvs.inEvents();
        proc.in_events = &inOff;
        plugin->process (plugin, &proc);
        proc.in_events = nullptr;

        // Process silence blocks with no events until activeVoices is 0
        clap_voice_info_t vinfo {};
        CHECK (voiceExt->get (plugin, &vinfo));
        while (vinfo.voice_count > 0)
        {
            plugin->process (plugin, &proc);
            CHECK (voiceExt->get (plugin, &vinfo));
        }
        CHECK_EQ (vinfo.voice_count, 0u);

        // (a) One block after the voice has ended (activeVoices is 0), output peak is above 0.001
        plugin->process (plugin, &proc);
        float tailPeak = 0.0f;
        for (int i = 0; i < 512; ++i)
            tailPeak = std::max ({ tailPeak, std::abs (bufL[i]), std::abs (bufR[i]) });
        std::printf ("    measured tail peak: %f\n", tailPeak);
        CHECK (tailPeak > 0.001f);

        // (b) Every process call during the next 1 second returns CLAP_PROCESS_CONTINUE and never CLAP_PROCESS_SLEEP
        const int blocksIn1Sec = static_cast<int> (std::ceil (1.0 * 48000.0 / 512.0)); // 94 blocks
        for (int b = 0; b < blocksIn1Sec; ++b)
        {
            const int32_t status = plugin->process (plugin, &proc);
            CHECK_EQ (status, static_cast<int32_t> (CLAP_PROCESS_CONTINUE));
            CHECK (status != static_cast<int32_t> (CLAP_PROCESS_SLEEP));
        }

        // (c) With the effect switched OFF (a second run, dry patch), after the voice ended
        // and the hold time the fix uses (2.0s = 96000 frames) has passed in silence,
        // the status becomes CLAP_PROCESS_SLEEP (process that long plus one block)
        setParamClap ("reverb.on", 0.0);
        plugin->reset (plugin);

        // Play note for 10 blocks (>= 0.1s)
        proc.in_events = &inOn;
        plugin->process (plugin, &proc);
        proc.in_events = nullptr;
        for (int b = 1; b < 10; ++b)
            plugin->process (plugin, &proc);

        // Note off
        proc.in_events = &inOff;
        plugin->process (plugin, &proc);
        proc.in_events = nullptr;

        // Process silence until voice ended
        CHECK (voiceExt->get (plugin, &vinfo));
        while (vinfo.voice_count > 0)
        {
            plugin->process (plugin, &proc);
            CHECK (voiceExt->get (plugin, &vinfo));
        }
        CHECK_EQ (vinfo.voice_count, 0u);

        // Hold time the fix uses: 2.0 seconds (96000 frames @ 48 kHz).
        // In 512-frame blocks: 96000 / 512 = 187.5 -> 188 blocks.
        // During the first second of silence (~94 blocks), status remains CLAP_PROCESS_CONTINUE.
        // After the hold time has passed in silence (process that long plus one block = 189 blocks),
        // the status becomes CLAP_PROCESS_SLEEP.
        const int holdBlocks = static_cast<int> (std::ceil (2.0 * 48000.0 / 512.0)); // 188 blocks
        int32_t midStatus = CLAP_PROCESS_SLEEP;
        for (int b = 0; b < blocksIn1Sec; ++b)
            midStatus = plugin->process (plugin, &proc);
        CHECK_EQ (midStatus, static_cast<int32_t> (CLAP_PROCESS_CONTINUE));

        int32_t sleepStatus = CLAP_PROCESS_CONTINUE;
        for (int b = blocksIn1Sec; b <= holdBlocks; ++b)
            sleepStatus = plugin->process (plugin, &proc);
        CHECK_EQ (sleepStatus, static_cast<int32_t> (CLAP_PROCESS_SLEEP));
    }

    // Part 2: Mono output buffer downmix (channel_count == 1) with left-panned signal
    {
        plugin->reset (plugin);

        auto monoPatch = ParamValues::defaults();
        monoPatch.plain[indexOfKey ("osc1.pan")] = -100.0f; // 100% hard left
        CHECK (loadStateText (plugin, saveState (monoPatch)));

        std::vector<float> bufMono (512, 0.0f);
        float* channelData[1] = { bufMono.data() };
        clap_audio_buffer_t audioOut {};
        audioOut.data32 = channelData;
        audioOut.channel_count = 1;

        clap_process_t proc {};
        proc.frames_count = 512;
        proc.audio_outputs = &audioOut;
        proc.audio_outputs_count = 1;

        clap_event_note_t noteOnEv {};
        noteOnEv.header.size = sizeof noteOnEv;
        noteOnEv.header.time = 0;
        noteOnEv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        noteOnEv.header.type = CLAP_EVENT_NOTE_ON;
        noteOnEv.note_id = 2;
        noteOnEv.key = 60;
        noteOnEv.velocity = 0.9;

        SimpleEvents evs;
        evs.events.push_back (&noteOnEv.header);
        clap_input_events_t inEvs = evs.inEvents();
        proc.in_events = &inEvs;

        plugin->process (plugin, &proc);

        // On defect code: outR aliases outL, right channel (0.0f) overwrites left channel -> monoPeak == 0.0f
        // On fixed code: mono receives 0.5f * (L + R) -> monoPeak > 0.02f
        float monoPeak = 0.0f;
        for (int i = 0; i < 512; ++i)
            monoPeak = std::max (monoPeak, std::abs (bufMono[i]));
        CHECK (monoPeak > 0.02f);
    }

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
}

TEST (native_clap_host_lifecycle)
{
    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    CHECK (entry != nullptr);
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    CHECK (factory != nullptr);

    // 1. Host creates plugin and destroys it immediately without init
    {
        const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
        CHECK (plugin != nullptr);
        plugin->destroy (plugin);
    }

    // 2. Host creates plugin, inits it, then destroys it without activate
    {
        const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
        CHECK (plugin != nullptr);
        CHECK (plugin->init (plugin));
        plugin->destroy (plugin);
    }

    // 3. Host creates plugin, inits, activates, deactivates, destroys without processing
    {
        const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
        CHECK (plugin != nullptr);
        CHECK (plugin->init (plugin));
        CHECK (plugin->activate (plugin, 44100.0, 32, 256));
        plugin->deactivate (plugin);
        plugin->destroy (plugin);
    }

    // 4. Multiple activate / deactivate cycles with sample rate and buffer size changes
    {
        const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
        CHECK (plugin != nullptr);
        CHECK (plugin->init (plugin));

        for (const double sr : { 44100.0, 48000.0, 96000.0, 88200.0 })
        {
            for (const uint32_t bs : { 64u, 256u, 512u, 1024u })
            {
                CHECK (plugin->activate (plugin, sr, 32, bs));
                CHECK (plugin->start_processing (plugin));
                plugin->reset (plugin);
                plugin->stop_processing (plugin);
                plugin->deactivate (plugin);
            }
        }
        plugin->destroy (plugin);
    }

    // 5. Full lifecycle with processing, stop, start, reset, audio buffer edge cases
    {
        const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
        CHECK (plugin != nullptr);
        CHECK (plugin->init (plugin));
        CHECK (plugin->activate (plugin, 48000.0, 32, 512));
        CHECK (plugin->start_processing (plugin));

        // Process with 0 audio outputs (e.g. MIDI-only track / event flushing)
        clap_process_t proc0 {};
        proc0.frames_count = 64;
        proc0.audio_outputs_count = 0;
        proc0.audio_outputs = nullptr;
        CHECK (plugin->process (plugin, &proc0) != CLAP_PROCESS_ERROR);

        // Process with null events and null transport
        std::vector<float> bufL (64, 0.0f), bufR (64, 0.0f);
        float* channels[2] = { bufL.data(), bufR.data() };
        clap_audio_buffer_t audioOut {};
        audioOut.data32 = channels;
        audioOut.channel_count = 2;
        clap_process_t proc1 {};
        proc1.frames_count = 64;
        proc1.audio_outputs = &audioOut;
        proc1.audio_outputs_count = 1;
        CHECK (plugin->process (plugin, &proc1) != CLAP_PROCESS_ERROR);

        // Stop processing, reset, restart processing
        plugin->stop_processing (plugin);
        plugin->reset (plugin);
        CHECK (plugin->start_processing (plugin));
        CHECK (plugin->process (plugin, &proc1) != CLAP_PROCESS_ERROR);

        plugin->stop_processing (plugin);
        plugin->deactivate (plugin);
        plugin->destroy (plugin);
    }

    // 6. Null pointer safety across plugin lifecycle entry points
    {
        const clap_plugin_t* plugin = factory->create_plugin (factory, &s_mockHost, "com.safian.synthyboy");
        CHECK (plugin != nullptr);
        CHECK (! plugin->init (nullptr));
        plugin->destroy (nullptr);
        CHECK (! plugin->activate (nullptr, 48000.0, 32, 512));
        plugin->deactivate (nullptr);
        CHECK (! plugin->start_processing (nullptr));
        plugin->stop_processing (nullptr);
        plugin->reset (nullptr);
        CHECK_EQ (plugin->process (nullptr, nullptr), static_cast<int32_t> (CLAP_PROCESS_ERROR));
        CHECK_EQ (plugin->process (plugin, nullptr), static_cast<int32_t> (CLAP_PROCESS_ERROR));
        plugin->destroy (plugin);
    }
}


