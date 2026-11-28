// SynthyBoy - model-specific passive Filter STYLE names through the actual VST dispatcher.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#define SYNTHYBOY_MPC_LFO_LIVE 1
#define SYNTHYBOY_MPC_SPECTRUM 1
#define SYNTHYBOY_MPC_PAD_PRESSURE 1
#include <tuple>
#include "../synthyboy_mpc.cpp"

int main()
{
    PluginInstance plugin;
    AEffect effect {};
    effect.object = &plugin;
    int failures = 0, checked = 0;
    const auto index = [] (const std::string& key) {
        return static_cast<int> (synthyboy::findParamByKey (key.c_str()) - synthyboy::paramTable());
    };
    for (int filter : { 1, 2 })
    {
        const std::string prefix = "filter" + std::to_string (filter) + ".";
        const int power = index (prefix + "on"), model = index (prefix + "model"), style = index (prefix + "style");
        // A passive display must work without publishing any native edit proxy.
        plugin.nativeFilter.publish (0, nullptr);
        for (bool on : { false, true })
            for (int type = 0; type < 27; ++type)
            {
                const int count = synthyboy::filters::filterModelStyleCount (type);
                const auto* names = synthyboy_mpc::ui::shownFilterStyleNames (type, synthyboy::filters::filterModelStyleNames (type));
                for (int choice = 0; choice < count; ++choice)
                {
                    plugin.paramMirror[power].store (on ? 1.0f : 0.0f);
                    plugin.paramMirror[model].store (static_cast<float> (type));
                    plugin.paramMirror[style].store (static_cast<float> (choice));
                    char text[256] {};
                    pluginDispatcher (&effect, effGetParamDisplay, style, 0, text, 0);
                    const bool correct = names != nullptr && std::strcmp (text, names[choice]) == 0
                        && plugin.paramMirror[style].load() == static_cast<float> (choice)
                        && plugin.paramMirror[power].load() == (on ? 1.0f : 0.0f);
                    if (! correct)
                    {
                        std::fprintf (stderr, "FAIL F%d %s model%d style%d: '%s', expected '%s'\n", filter, on ? "ON" : "OFF", type, choice, text, names != nullptr ? names[choice] : "");
                        ++failures;
                    }
                    ++checked;
                }
            }
    }
    // The passive dials read the raw value handles: the text carries the unit (the same text as the active native dials) and the host label is empty
    for (int filter : { 1, 2 })
        for (const auto& [name, plain, expected] : { std::tuple<const char*, float, const char*> { "cutoff", 12000.0f, "12.00 kHz" }, { "cutoff", 57.9f, "57.9 Hz" },
                                                     { "drive", 3.3f, "+3.3 dB" }, { "mix", 100.0f, "100.0%" } })
        {
            const int param = index ("filter" + std::to_string (filter) + "." + name);
            plugin.paramMirror[param].store (plain);
            char text[256] {}, label[256] { 'x' };
            pluginDispatcher (&effect, effGetParamDisplay, param, 0, text, 0);
            pluginDispatcher (&effect, effGetParamLabel, param, 0, label, 0);
            if (std::strcmp (text, expected) != 0 || label[0] != '\0')
            {
                std::fprintf (stderr, "FAIL F%d %s %g: display '%s' (expected '%s'), label '%s' (expected empty)\n", filter, name, static_cast<double> (plain), text, expected, label);
                ++failures;
            }
            ++checked;
        }
    std::printf ("Filter raw STYLE display: %s (%d cases, %d failures)\n", failures ? "FAIL" : "PASS", checked, failures);
    return failures ? 1 : 0;
}
