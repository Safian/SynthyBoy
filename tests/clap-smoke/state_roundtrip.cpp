// SynthyBoyVST - CLAP smoke host: parameter text round trip + state reproducibility.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Mimics the clap-validator checks `param-conversions` and `state-reproducibility-*` without cargo:
//   state_roundtrip <path/to/Plugin.clap>
// Exit code 0 = all checks passed.

#include <clap/clap.h>

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
bool rescanValues = false;     // plugin asked for params.rescan(VALUES)
bool callbackRequested = false;

const clap_host_params_t hostParams = {
    [] (const clap_host_t*, clap_param_rescan_flags flags) { if (flags & CLAP_PARAM_RESCAN_VALUES) rescanValues = true; },
    [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
    [] (const clap_host_t*) {}
};

const clap_host_t host = {
    CLAP_VERSION, nullptr, "clap-smoke", "SynthyBoy", nullptr, "0",
    [] (const clap_host_t*, const char* id) -> const void* { return std::strcmp (id, CLAP_EXT_PARAMS) == 0 ? &hostParams : nullptr; },
    [] (const clap_host_t*) {}, [] (const clap_host_t*) {}, [] (const clap_host_t*) { callbackRequested = true; }
};

struct Param { clap_param_info_t info; };

struct Instance
{
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    std::vector<Param> list;
    bool active = false;
};

bool activateInstance (Instance& inst)
{
    inst.active = inst.plugin->activate (inst.plugin, 48000.0, 32, 512) && inst.plugin->start_processing (inst.plugin);
    return inst.active;
}

bool createInstance (const clap_plugin_factory_t* f, const char* id, Instance& inst, bool activate = true)
{
    inst.plugin = f->create_plugin (f, &host, id);
    if (! inst.plugin || ! inst.plugin->init (inst.plugin)) return false;
    inst.params = static_cast<const clap_plugin_params_t*> (inst.plugin->get_extension (inst.plugin, CLAP_EXT_PARAMS));
    inst.state  = static_cast<const clap_plugin_state_t*>  (inst.plugin->get_extension (inst.plugin, CLAP_EXT_STATE));
    if (! inst.params || ! inst.state) return false;
    for (uint32_t i = 0; i < inst.params->count (inst.plugin); ++i)
    {
        Param p {};
        inst.params->get_info (inst.plugin, i, &p.info);
        inst.list.push_back (p);
    }
    return ! activate || activateInstance (inst);
}

void destroyInstance (Instance& inst)
{
    if (inst.active)
    {
        inst.plugin->stop_processing (inst.plugin);
        inst.plugin->deactivate (inst.plugin);
    }
    inst.plugin->destroy (inst.plugin);
}

// pushes the values through process(), like the validator does
void setValues (Instance& inst, const std::vector<double>& values)
{
    struct Events { std::vector<clap_event_param_value_t> ev; } events;
    for (size_t i = 0; i < values.size(); ++i)
    {
        clap_event_param_value_t e {};
        e.header = { sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
        e.param_id = inst.list[i].info.id;
        e.cookie = inst.list[i].info.cookie;
        e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
        e.value = values[i];
        events.ev.push_back (e);
    }
    clap_input_events_t in { &events,
        [] (const clap_input_events_t* l) { return (uint32_t) static_cast<Events*> (l->ctx)->ev.size(); },
        [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* { return &static_cast<Events*> (l->ctx)->ev[i].header; } };
    clap_output_events_t out { nullptr, [] (const clap_output_events_t*, const clap_event_header_t*) { return true; } };

    std::vector<float> l (512, 0.0f), r (512, 0.0f);
    float* ch[2] = { l.data(), r.data() };
    clap_audio_buffer_t ab {}; ab.data32 = ch; ab.channel_count = 2;
    clap_process_t p {};
    p.frames_count = 512; p.audio_outputs = &ab; p.audio_outputs_count = 1;
    p.in_events = &in; p.out_events = &out; p.steady_time = -1;
    inst.plugin->process (inst.plugin, &p);
}

std::vector<double> getValues (Instance& inst)
{
    std::vector<double> v;
    for (auto& p : inst.list) { double x = 0; inst.params->get_value (inst.plugin, p.info.id, &x); v.push_back (x); }
    return v;
}

std::vector<char> saveState (Instance& inst)
{
    std::vector<char> data;
    clap_ostream_t os { &data, [] (const clap_ostream_t* s, const void* b, uint64_t n) -> int64_t
        { auto* d = static_cast<std::vector<char>*> (s->ctx); d->insert (d->end(), (const char*) b, (const char*) b + n); return (int64_t) n; } };
    if (! inst.state->save (inst.plugin, &os)) data.clear();
    return data;
}

bool loadState (Instance& inst, const std::vector<char>& data)
{
    struct S { const std::vector<char>* d; size_t pos; } s { &data, 0 };
    clap_istream_t is { &s, [] (const clap_istream_t* st, void* b, uint64_t n) -> int64_t
        { auto* x = static_cast<S*> (st->ctx); auto k = std::min<uint64_t> (n, x->d->size() - x->pos);
          std::memcpy (b, x->d->data() + x->pos, k); x->pos += k; return (int64_t) k; } };
    return inst.state->load (inst.plugin, &is);
}

std::string toText (Instance& inst, clap_id id, double v)
{
    char buf[256] = {};
    return inst.params->value_to_text (inst.plugin, id, v, buf, sizeof buf) ? buf : "<no text>";
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: %s Plugin.clap\n", argv[0]); return 2; }

    std::string path = argv[1];
#if defined (__APPLE__)
    path += "/Contents/MacOS/" + path.substr (path.find_last_of ('/') + 1, path.size() - path.find_last_of ('/') - 6);
#endif
    void* lib = dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::fprintf (stderr, "dlopen failed: %s\n", dlerror()); return 2; }
    auto* entry = static_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry"));
    if (! entry || ! entry->init (argv[1])) { std::fprintf (stderr, "clap_entry failed\n"); return 2; }
    auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const char* id = factory->get_plugin_descriptor (factory, 0)->id;

    int failures = 0;
    Instance a;
    if (! createInstance (factory, id, a)) { std::fprintf (stderr, "cannot create instance\n"); return 2; }

    // 1. value <-> text round trip (validator: param-conversions)
    for (auto& p : a.list)
        for (double t : { 0.0, 0.1616161616, 0.5, 0.9 })
        {
            const double v = p.info.min_value + t * (p.info.max_value - p.info.min_value);
            const auto s1 = toText (a, p.info.id, v);
            double v2 = 0;
            char tmp[256]; std::snprintf (tmp, sizeof tmp, "%s", s1.c_str());
            if (! a.params->text_to_value (a.plugin, p.info.id, tmp, &v2)) { std::printf ("FAIL text_to_value '%s' (%s)\n", s1.c_str(), p.info.name); ++failures; continue; }
            const auto s2 = toText (a, p.info.id, v2);
            if (s1 != s2) { std::printf ("FAIL param-conversions %s: '%s' -> %.9g -> '%s'\n", p.info.name, s1.c_str(), v2, s2.c_str()); ++failures; }
        }

    // 2. state reproducibility: set values via process(), save, new instance, load, compare
    std::vector<double> wanted;
    for (size_t i = 0; i < a.list.size(); ++i)
    {
        const auto& info = a.list[i].info;
        const double t = (i % 2 == 0) ? 0.2 : 0.7;
        double v = info.min_value + t * (info.max_value - info.min_value);
        if (info.flags & CLAP_PARAM_IS_STEPPED) v = std::round (v);
        wanted.push_back (v);
    }
    setValues (a, wanted);
    const auto set = getValues (a);
    const auto blob1 = saveState (a);
    destroyInstance (a);

    for (const bool activateBeforeLoad : { true, false })
    {
        Instance b;
        if (! createInstance (factory, id, b, activateBeforeLoad) || blob1.empty())
        { std::printf ("FAIL: cannot create second instance / empty state (%zu bytes)\n", blob1.size()); return 1; }
        const auto beforeLoad = getValues (b);
        rescanValues = false; callbackRequested = false;
        if (! loadState (b, blob1)) { std::printf ("FAIL: state load returned false\n"); return 1; }
        if (callbackRequested) b.plugin->on_main_thread (b.plugin);
        if (! activateBeforeLoad && ! activateInstance (b)) { std::printf ("FAIL: activate after load\n"); return 1; }
        const auto got = getValues (b);
        bool changed = false;
        for (size_t i = 0; i < got.size(); ++i) changed = changed || got[i] != beforeLoad[i];
        if (changed && ! rescanValues) { std::printf ("FAIL state-reproducibility (%s): values changed on load without params.rescan(VALUES)\n", activateBeforeLoad ? "activate, load" : "load, activate"); ++failures; }
        for (size_t i = 0; i < got.size(); ++i)
            if (std::fabs (got[i] - set[i]) > 1e-4 * std::max (1.0, std::fabs (set[i])))
            { std::printf ("FAIL state-reproducibility (%s) %s: set %.7g, after reload %.7g\n", activateBeforeLoad ? "activate, load" : "load, activate", b.list[i].info.name, set[i], got[i]); ++failures; }
        const auto blob2 = saveState (b);
        if (blob1 != blob2) { std::printf ("FAIL state-reproducibility-binary: state differs byte for byte (%zu vs %zu bytes)\n", blob1.size(), blob2.size()); ++failures; }
        destroyInstance (b);
    }

    std::printf (failures ? "%d check(s) FAILED\n" : "all checks passed\n", failures);
    entry->deinit();
    return failures ? 1 : 0;
}
