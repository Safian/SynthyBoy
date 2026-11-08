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

#include "synthyboy/kick_pages.h"
#include "synthyboy/params.h"
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
    CHECK_EQ (remoteControls->count (plugin), 118u);

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
    CHECK_EQ (kickPageCount(), 118u);
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
            CHECK (page.keys[k] != nullptr);
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

    // Asserts all 1596 keys in paramTable() are reachable
    CHECK_EQ (allReachableKeys.size(), 1596u);
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
        setenv ("SYNTHYBOY_USER_DIR", value, 1);
    else
        unsetenv ("SYNTHYBOY_USER_DIR");
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
