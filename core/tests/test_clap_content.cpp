// SynthyBoy - tests of the CLAP extension "synthyboy.content" of the native shell (WP-01 g): import by path, list, attach, remove, and the audible result.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "synthyboy/params.h"
#include "synthyboy/user_content.h"
#include "synthyboy_clap.h"
#include "synthyboy_content_ext.h"
#include "test_runner.h"

using namespace synthyboy;
namespace fs = std::filesystem;

namespace
{
const clap_host_t s_host = { CLAP_VERSION, nullptr, "content-test-host", "Safian", "", "1.0.0",
                             [] (const clap_host_t*, const char*) -> const void* { return nullptr; }, [] (const clap_host_t*) {}, [] (const clap_host_t*) {},
                             [] (const clap_host_t*) {} };

struct Events
{
    std::vector<const clap_event_header_t*> list;
    clap_input_events_t in {};
    Events()
    {
        in.ctx = this;
        in.size = [] (const clap_input_events_t* l) -> uint32_t { return static_cast<uint32_t> (static_cast<Events*> (l->ctx)->list.size()); };
        in.get = [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* { return static_cast<Events*> (l->ctx)->list[i]; };
    }
};

clap_event_param_value_t paramEvent (const char* key, double value)
{
    clap_event_param_value_t e {};
    e.header.size = sizeof e;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_PARAM_VALUE;
    e.param_id = findParamByKey (key)->id;
    e.cookie = nullptr;
    e.note_id = -1;
    e.port_index = -1;
    e.channel = -1;
    e.key = -1;
    e.value = value;
    return e;
}

/** Renders `seconds` of a held note and returns the left channel. The parameter events go in with the first block. */
std::vector<float> play (const clap_plugin_t* plugin, int key, double seconds, std::vector<clap_event_param_value_t> params = {})
{
    const int block = 256;
    const int blocks = static_cast<int> (seconds * 48000.0) / block;
    std::vector<float> out;
    clap_event_note_t on {};
    on.header.size = sizeof on;
    on.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    on.header.type = CLAP_EVENT_NOTE_ON;
    on.note_id = 1;
    on.key = static_cast<int16_t> (key);
    on.velocity = 1.0;
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<float> l (block, 0.0f), r (block, 0.0f);
        float* data[2] = { l.data(), r.data() };
        clap_audio_buffer_t audio {};
        audio.data32 = data;
        audio.channel_count = 2;
        Events events;
        if (b == 0)
        {
            for (auto& p : params)
                events.list.push_back (&p.header);
            events.list.push_back (&on.header);
        }
        clap_process_t proc {};
        proc.frames_count = block;
        proc.audio_outputs = &audio;
        proc.audio_outputs_count = 1;
        proc.in_events = &events.in;
        plugin->process (plugin, &proc);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}

double crossingHz (const std::vector<float>& v, double t0, double t1)
{
    int crossings = 0;
    for (size_t i = static_cast<size_t> (t0 * 48000.0) + 1; i < static_cast<size_t> (t1 * 48000.0) && i < v.size(); ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
            ++crossings;
    return crossings / (t1 - t0);
}

double rmsOf (const std::vector<float>& v, double t0, double t1)
{
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = static_cast<size_t> (t0 * 48000.0); i < static_cast<size_t> (t1 * 48000.0) && i < v.size(); ++i, ++n)
        sum += static_cast<double> (v[i]) * static_cast<double> (v[i]);
    return n > 0 ? std::sqrt (sum / static_cast<double> (n)) : 0.0;
}

void writeFile (const fs::path& path, const std::vector<uint8_t>& bytes)
{
    fs::create_directories (path.parent_path());
    std::ofstream out (path, std::ios::binary);
    out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
}
} // namespace

TEST (clap_content_extension_imports_lists_attaches_and_removes)
{
    const fs::path dir = fs::temp_directory_path() / "sb_clap_content";
    fs::remove_all (dir);
    setenv ("SYNTHYBOY_USER_DIR", (dir / "user").string().c_str(), 1);

    std::vector<float> tone (48000);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.8f * static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * 440.0 * static_cast<double> (i) / 48000.0));
    const fs::path file = dir / "in" / "Tone A3.wav";
    writeFile (file, encodeWav (tone.data(), nullptr, tone.size(), 48000, 16));

    const clap_plugin_entry_t* entry = synthyboy_clap_get_entry();
    CHECK (entry->init ("synthyboy_static"));
    const auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_host, "com.safian.synthyboy");
    CHECK (plugin != nullptr && plugin->init (plugin));
    const auto* ext = static_cast<const synthyboy_plugin_content_t*> (plugin->get_extension (plugin, SYNTHYBOY_EXT_CONTENT));
    CHECK (ext != nullptr);
    if (ext == nullptr)
        return;
    CHECK (plugin->activate (plugin, 48000.0, 32, 256));
    CHECK (plugin->start_processing (plugin));

    CHECK_EQ (ext->count (plugin, SYNTHYBOY_CONTENT_SAMPLE), 0u);
    synthyboy_content_item_t item {};
    CHECK_EQ (ext->import_file (plugin, SYNTHYBOY_CONTENT_SAMPLE, (dir / "none.wav").string().c_str(), &item), SYNTHYBOY_CONTENT_ERR_READ);
    writeFile (dir / "in" / "notes.txt", { 'h', 'i' });
    CHECK_EQ (ext->import_file (plugin, SYNTHYBOY_CONTENT_SAMPLE, (dir / "in" / "notes.txt").string().c_str(), &item), SYNTHYBOY_CONTENT_ERR_FORMAT);
    CHECK_EQ (ext->import_file (plugin, 9, file.string().c_str(), &item), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->import_file (plugin, SYNTHYBOY_CONTENT_SAMPLE, file.string().c_str(), &item), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (std::string (item.name), std::string ("Tone A3"));
    CHECK_EQ (std::string (item.pack), std::string (kPackSamples));
    CHECK_EQ (std::string (item.sha256).size(), static_cast<size_t> (64));
    CHECK_EQ (ext->count (plugin, SYNTHYBOY_CONTENT_SAMPLE), 1u);
    CHECK_EQ (ext->count (plugin, SYNTHYBOY_CONTENT_WAVETABLE), 0u);
    synthyboy_content_item_t listed {};
    CHECK_EQ (ext->get (plugin, SYNTHYBOY_CONTENT_SAMPLE, 0, &listed), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (std::string (listed.sha256), std::string (item.sha256));
    CHECK_EQ (ext->get (plugin, SYNTHYBOY_CONTENT_SAMPLE, 1, &listed), SYNTHYBOY_CONTENT_ERR_ARGUMENT);

    // not attached: the Sample oscillator is silent; attached: it plays the file at the key pitch, 880 Hz an octave up
    const auto base = [] { return std::vector<clap_event_param_value_t> {
        paramEvent ("osc1.type", 1.0), paramEvent ("osc1.unison.voices", 1.0), paramEvent ("osc1.random_phase", 0.0), paramEvent ("osc1.sample.loop_mode", 1.0),
        paramEvent ("filter1.on", 0.0), paramEvent ("osc1.level", 100.0) }; };
    CHECK (rmsOf (play (plugin, 69, 0.8, base()), 0.3, 0.7) < 1.0e-4);
    plugin->reset (plugin);
    CHECK_EQ (ext->attach (plugin, SYNTHYBOY_CONTENT_SAMPLE, 0, 4), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->attach (plugin, SYNTHYBOY_CONTENT_SAMPLE, 0, 1), SYNTHYBOY_CONTENT_OK);
    const auto a4 = play (plugin, 69, 1.0);
    CHECK (rmsOf (a4, 0.3, 0.9) > 0.05);
    CHECK_NEAR (crossingHz (a4, 0.3, 0.9), 440.0, 6.0);
    plugin->reset (plugin);
    CHECK_NEAR (crossingHz (play (plugin, 81, 1.0), 0.3, 0.9), 880.0, 12.0);

    // the reference is in the state, so a saved project finds the file again; removing the file makes it a missing one (silence)
    CHECK_EQ (ext->remove (plugin, SYNTHYBOY_CONTENT_SAMPLE, 3), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->remove (plugin, SYNTHYBOY_CONTENT_SAMPLE, 0), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (ext->count (plugin, SYNTHYBOY_CONTENT_SAMPLE), 0u);
    plugin->reset (plugin);
    CHECK (rmsOf (play (plugin, 69, 0.8), 0.3, 0.7) < 1.0e-4);

    // a wavetable by path (a WAV of 2048 sample cycles) and an impulse response
    std::vector<float> frames (2048 * 2);
    for (size_t i = 0; i < frames.size(); ++i)
        frames[i] = 0.7f * static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * 3.0 * static_cast<double> (i % 2048) / 2048.0));
    writeFile (dir / "in" / "Triple.wav", encodeWav (frames.data(), nullptr, frames.size(), 44100, 16));
    CHECK_EQ (ext->import_file (plugin, SYNTHYBOY_CONTENT_WAVETABLE, (dir / "in" / "Triple.wav").string().c_str(), &item), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (std::string (item.pack), std::string (kPackWavetables));
    CHECK_EQ (ext->attach (plugin, SYNTHYBOY_CONTENT_WAVETABLE, 0, 2), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (ext->import_file (plugin, SYNTHYBOY_CONTENT_IMPULSE, file.string().c_str(), &item), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (ext->attach (plugin, SYNTHYBOY_CONTENT_IMPULSE, 0, 0), SYNTHYBOY_CONTENT_OK);

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    fs::remove_all (dir);
    unsetenv ("SYNTHYBOY_USER_DIR");
}
