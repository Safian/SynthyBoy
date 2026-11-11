// SynthyBoy - tests of the CLAP extension "synthyboy.content" (WP-01 g, GROUPD): import by path, list, attach, remove, and the audible result.
// The tests run against the native shell (linked in) and, when SYNTHYBOY_CLAP_UNDER_TEST names a .clap bundle, against that module: the JUCE-wrapped CLAP build (REMAINDER item 3,
// ctest synthyboy_test_clap_content_juce) answers the same extension with the same functions (shell-clap/synthyboy_content_service.cpp) through the patched wrapper.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
 #include <process.h>
 #define NOMINMAX
 #include <windows.h>
#else
 #include <dlfcn.h>
 #include <unistd.h>
#endif

#include "groupd_util.h"
#include "synthyboy/engine.h"
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

/** The output events: a wrapper (the JUCE one) writes parameter changes made by the plug-in itself; the test ignores them. */
const clap_output_events_t s_outEvents = { nullptr, [] (const clap_output_events_t*, const clap_event_header_t*) -> bool { return true; } };

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

/** The JUCE-wrapped build reports every parameter as 0 .. 1 (clap-juce-extensions with "Use JUCE parameter ranges" OFF): its events carry the normalised value of the JUCE range
    (start, end, skew), the native shell takes plain values. */
bool s_normalisedEvents = false;

double eventValueOf (const ParamInfo& info, double plain)
{
    if (! s_normalisedEvents)
        return plain;
    const double span = static_cast<double> (info.max) - static_cast<double> (info.min);
    double proportion = span > 0.0 ? std::clamp ((plain - static_cast<double> (info.min)) / span, 0.0, 1.0) : 0.0;
    if (info.scale == Scale::exp_skew && info.skew != 1.0f)
        proportion = std::pow (proportion, static_cast<double> (info.skew));
    return proportion;
}

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
    e.value = eventValueOf (*findParamByKey (key), value);
    return e;
}

long currentProcessId()
{
#ifdef _WIN32
    return static_cast<long> (_getpid());
#else
    return static_cast<long> (getpid());
#endif
}

/** The plug-in factory under test: the module named by SYNTHYBOY_CLAP_UNDER_TEST (a .clap bundle, loaded once) or the native shell. */
const clap_plugin_factory_t* openFactory (bool& wrapped)
{
    static const clap_plugin_entry_t* entry = nullptr;
    static bool isWrapped = false;
    if (entry == nullptr)
    {
        if (const char* bundle = std::getenv ("SYNTHYBOY_CLAP_UNDER_TEST"); bundle != nullptr && bundle[0] != '\0')
        {
            std::string path = bundle;
            const std::string name = fs::path (path).stem().string();
            if (fs::is_directory (path))                    // macOS: a bundle; elsewhere the module is the file itself
                path += "/Contents/MacOS/" + name;
            const auto loadEntry = [] (const std::string& modulePath) -> const clap_plugin_entry_t*
            {
#ifdef _WIN32
                HMODULE lib = LoadLibraryA (modulePath.c_str());
                if (lib == nullptr)
                    std::printf ("    LoadLibrary %s: error %lu\n", modulePath.c_str(), static_cast<unsigned long> (GetLastError()));
                return lib != nullptr ? reinterpret_cast<const clap_plugin_entry_t*> (GetProcAddress (lib, "clap_entry")) : nullptr;
#else
                void* lib = dlopen (modulePath.c_str(), RTLD_NOW | RTLD_LOCAL);
                if (lib == nullptr)
                    std::printf ("    dlopen %s: %s\n", modulePath.c_str(), dlerror());
                return lib != nullptr ? static_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry")) : nullptr;
#endif
            };
            entry = loadEntry (path);
            isWrapped = entry != nullptr;
            if (entry != nullptr)
                entry->init (bundle);
        }
        else
        {
            entry = synthyboy_clap_get_entry();
            entry->init ("synthyboy_static");
        }
    }
    wrapped = isWrapped;
    s_normalisedEvents = isWrapped;
    return entry != nullptr ? static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID)) : nullptr;
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
        proc.out_events = &s_outEvents;
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
    const fs::path dir = fs::temp_directory_path() / ("sb_clap_content_" + std::to_string (currentProcessId()));
    fs::remove_all (dir);
    sbtest::setEnv ("SYNTHYBOY_USER_DIR", (dir / "user").string().c_str());

    std::vector<float> tone (48000);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.8f * static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * 440.0 * static_cast<double> (i) / 48000.0));
    const fs::path file = dir / "in" / "Tone A3.wav";
    writeFile (file, encodeWav (tone.data(), nullptr, tone.size(), 48000, 16));

    bool wrapped = false;
    const auto* factory = openFactory (wrapped);
    CHECK (factory != nullptr);
    if (factory == nullptr)
        return;
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
    const bool sampleOsc = isEngineAvailable (EngineKind::vital); // the own engine (builds without Vital) has no Sample oscillator
    if (sampleOsc)
        CHECK (rmsOf (play (plugin, 69, 0.8, base()), 0.3, 0.7) < 1.0e-4);
    plugin->reset (plugin);
    CHECK_EQ (ext->attach (plugin, SYNTHYBOY_CONTENT_SAMPLE, 0, 4), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->attach (plugin, SYNTHYBOY_CONTENT_SAMPLE, 0, 1), SYNTHYBOY_CONTENT_OK);
    if (sampleOsc)
    {
        const auto a4 = play (plugin, 69, 1.0);
        CHECK (rmsOf (a4, 0.3, 0.9) > 0.05);
        CHECK_NEAR (crossingHz (a4, 0.3, 0.9), 440.0, 6.0);
        plugin->reset (plugin);
        CHECK_NEAR (crossingHz (play (plugin, 81, 1.0), 0.3, 0.9), 880.0, 12.0);
    }

    // the reference is in the state, so a saved project finds the file again; removing the file makes it a missing one (silence)
    CHECK_EQ (ext->remove (plugin, SYNTHYBOY_CONTENT_SAMPLE, 3), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->remove (plugin, SYNTHYBOY_CONTENT_SAMPLE, 0), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (ext->count (plugin, SYNTHYBOY_CONTENT_SAMPLE), 0u);
    plugin->reset (plugin);
    if (sampleOsc)
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
    sbtest::unsetEnv ("SYNTHYBOY_USER_DIR");
}

/** One block with parameter events only (no note): the shell's stored values and the engine take them. */
void setParams (const clap_plugin_t* plugin, std::vector<clap_event_param_value_t> params)
{
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    float* data[2] = { l.data(), r.data() };
    clap_audio_buffer_t audio {};
    audio.data32 = data;
    audio.channel_count = 2;
    Events events;
    for (auto& p : params)
        events.list.push_back (&p.header);
    clap_process_t proc {};
    proc.frames_count = 256;
    proc.audio_outputs = &audio;
    proc.audio_outputs_count = 1;
    proc.in_events = &events.in;
    proc.out_events = &s_outEvents;
    plugin->process (plugin, &proc);
}

// GROUPD (WP-14 / 15 / 16) through the CLAP extension of the native shell (the mobile / Kick path): Switch to wavetable, the audio of the last note, Resample, Render OSC Warp,
// wavetable frame <-> LFO shape
TEST (clap_content_extension_switches_to_wavetable_renders_resamples_and_copies_lfo_shapes)
{
    const fs::path dir = fs::temp_directory_path() / ("sb_clap_groupd_" + std::to_string (currentProcessId()));
    fs::remove_all (dir);
    sbtest::setEnv ("SYNTHYBOY_USER_DIR", (dir / "user").string().c_str());

    const double f0 = 220.0;
    const auto tone = groupd::sawTone (f0, 1.5, 48000.0, 16);
    writeFile (dir / "in" / "Saw A3.wav", encodeWav (tone.data(), nullptr, tone.size(), 48000, 16));

    bool wrapped = false;
    const auto* factory = openFactory (wrapped);
    CHECK (factory != nullptr);
    if (factory == nullptr)
        return;
    const clap_plugin_t* plugin = factory->create_plugin (factory, &s_host, "com.safian.synthyboy");
    CHECK (plugin != nullptr && plugin->init (plugin));
    const auto* ext = static_cast<const synthyboy_plugin_content_t*> (plugin->get_extension (plugin, SYNTHYBOY_EXT_CONTENT));
    CHECK (ext != nullptr);
    if (ext == nullptr)
        return;
    CHECK (ext->switch_to_wavetable != nullptr && ext->render_osc_warp != nullptr && ext->resample != nullptr && ext->render_last_note != nullptr
           && ext->wavetable_frame_to_lfo != nullptr && ext->lfo_shape_to_wavetable != nullptr);
    CHECK (plugin->activate (plugin, 48000.0, 32, 256));
    CHECK (plugin->start_processing (plugin));

    char summary[256] = {};
    synthyboy_content_item_t item {};
    CHECK_EQ (ext->import_file (plugin, SYNTHYBOY_CONTENT_SAMPLE, (dir / "in" / "Saw A3.wav").string().c_str(), &item), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (ext->attach (plugin, SYNTHYBOY_CONTENT_SAMPLE, 0, 1), SYNTHYBOY_CONTENT_OK);
    const auto plain = [] (int osc) { return std::vector<clap_event_param_value_t> {
        paramEvent (("osc" + std::to_string (osc) + ".unison.voices").c_str(), 1.0), paramEvent (("osc" + std::to_string (osc) + ".random_phase").c_str(), 0.0),
        paramEvent (("osc" + std::to_string (osc) + ".phase").c_str(), 0.0), paramEvent (("osc" + std::to_string (osc) + ".level").c_str(), 100.0) }; };
    auto params = plain (1);
    params.push_back (paramEvent ("osc1.type", 1.0));
    params.push_back (paramEvent ("osc1.sample.loop_mode", 1.0));
    params.push_back (paramEvent ("filter1.on", 0.0));
    params.push_back (paramEvent ("env1.attack", 0.0));
    params.push_back (paramEvent ("env1.sustain", 100.0));
    params.push_back (paramEvent ("master.volume", -6.0));
    setParams (plugin, params);

    // errors: an unknown mode / oscillator, a wavetable oscillator (osc 2), no note played yet
    CHECK_EQ (ext->switch_to_wavetable (plugin, 1, 99, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->switch_to_wavetable (plugin, 4, 0, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->switch_to_wavetable (plugin, 2, 0, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_STATE);
    const fs::path out = dir / "out" / "last.wav";
    CHECK_EQ (ext->render_last_note (plugin, out.string().c_str(), summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_STATE);
    CHECK_EQ (ext->render_last_note (plugin, nullptr, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_ARGUMENT);

    // Switch to wavetable: the sample (a 220 Hz saw) becomes the table; played at A3 it has the harmonics of the source within 0.5 dB
    CHECK_EQ (ext->switch_to_wavetable (plugin, 1, 0, summary, sizeof summary), SYNTHYBOY_CONTENT_OK);
    std::printf ("    switch_to_wavetable: %s\n", summary);
    CHECK_EQ (ext->count (plugin, SYNTHYBOY_CONTENT_WAVETABLE), 1u);
    const auto played = play (plugin, 57, 1.5);
    const size_t from = 24000, n = 36000;
    const auto source = groupd::harmonicDb (tone, 24000, n, f0, 48000.0, 12);
    const auto heard = groupd::harmonicDb (played, from, n, f0, 48000.0, 12);
    std::vector<double> a, b;
    for (size_t i = 0; i < source.size(); ++i)
    {
        a.push_back (source[i] - source[0]);
        b.push_back (heard[i] - heard[0]);
    }
    const double worst = groupd::worstHarmonicDifference (a, b, 40.0);
    std::printf ("    CLAP shell: sample -> wavetable, harmonics 1..12 worst difference %.3f dB\n", worst);
    CHECK (worst < 0.5);

    // the audio of the last note (A3, held 1.5 s so far) as a WAV file
    CHECK_EQ (ext->render_last_note (plugin, out.string().c_str(), summary, sizeof summary), SYNTHYBOY_CONTENT_OK);
    std::ifstream wav (out, std::ios::binary);
    std::string head (12, '\0');
    wav.read (head.data(), 12);
    CHECK (head.compare (0, 4, "RIFF") == 0 && head.compare (8, 4, "WAVE") == 0);
    CHECK (fs::file_size (out) > 48000u * 6u);

    // Resample: one bar of that note becomes the table of osc 2 (the plain wavetable oscillator turns into the captured sound), A + B takes both
    plugin->reset (plugin);
    CHECK_EQ (ext->resample (plugin, 5, 0, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->resample (plugin, 2, 0, summary, sizeof summary), SYNTHYBOY_CONTENT_OK);
    const uint32_t afterOsc2 = ext->count (plugin, SYNTHYBOY_CONTENT_WAVETABLE);
    CHECK (afterOsc2 >= 2u);   // the files are content addressed: identical channels are stored once
    CHECK_EQ (ext->resample (plugin, 0, 0, summary, sizeof summary), SYNTHYBOY_CONTENT_OK);
    const uint32_t afterAB = ext->count (plugin, SYNTHYBOY_CONTENT_WAVETABLE);
    CHECK (afterAB >= afterOsc2);

    // Render OSC Warp on osc 1 (a wavetable oscillator since the switch) and the refusal on a sample oscillator
    auto warp = plain (1);
    warp.push_back (paramEvent ("osc1.warp.type", 1.0)); // Sync
    warp.push_back (paramEvent ("osc1.warp.amount", 30.0));
    warp.push_back (paramEvent ("osc3.type", 1.0));
    setParams (plugin, warp);
    CHECK_EQ (ext->render_osc_warp (plugin, 1, summary, sizeof summary), SYNTHYBOY_CONTENT_OK);
    std::printf ("    render_osc_warp: %s\n", summary);
    CHECK (ext->count (plugin, SYNTHYBOY_CONTENT_WAVETABLE) > afterAB);
    CHECK_EQ (ext->render_osc_warp (plugin, 3, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_STATE);
    CHECK_EQ (ext->render_osc_warp (plugin, 7, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    plugin->reset (plugin);
    CHECK (rmsOf (play (plugin, 57, 1.0), 0.3, 0.9) > 0.02);   // the baked oscillator sounds

    // wavetable frame -> LFO 1 (custom shape), LFO shape -> wavetable of osc 3 (one-frame table: osc 3 is a sample oscillator with no table)
    CHECK_EQ (ext->wavetable_frame_to_lfo (plugin, 1, 1, summary, sizeof summary), SYNTHYBOY_CONTENT_OK);
    std::printf ("    wavetable_frame_to_lfo: %s\n", summary);
    CHECK_EQ (ext->wavetable_frame_to_lfo (plugin, 1, 11, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_ARGUMENT);
    CHECK_EQ (ext->lfo_shape_to_wavetable (plugin, 1, 2, summary, sizeof summary), SYNTHYBOY_CONTENT_OK);
    CHECK_EQ (ext->lfo_shape_to_wavetable (plugin, 11, 2, summary, sizeof summary), SYNTHYBOY_CONTENT_ERR_ARGUMENT);

    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    fs::remove_all (dir);
    sbtest::unsetEnv ("SYNTHYBOY_USER_DIR");
}
