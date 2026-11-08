// SynthyBoy - tests: native JUCE-free CLAP shell and static entry point (docs/ARCHITECTURE.md 4).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

#include <clap/ext/remote-controls.h>

#include "synthyboy/kick_pages.h"
#include "synthyboy/params.h"
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
    CHECK_EQ (remoteControls->count (plugin), 61u);

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
    CHECK_EQ (kickPageCount(), 61u);
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

    // Asserts all 880 keys in paramTable() are reachable
    CHECK_EQ (allReachableKeys.size(), 880u);
    // the 604 keys of state v3 get their pages in the 103-page table (13-new-keys.md section 9); until then only the v2 keys are reachable
    CHECK_EQ (allReachableKeys.size(), kNumKeysV2);

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
