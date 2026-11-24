// SynthyBoy - model-specific passive Filter STYLE names through the actual VST dispatcher.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#define SYNTHYBOY_MPC_OVERLAY 1
#define SYNTHYBOY_MPC_LFO_LIVE 1
#define SYNTHYBOY_MPC_SPECTRUM 1
#define SYNTHYBOY_MPC_PAD_PRESSURE 1
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
    std::printf ("Filter raw STYLE display: %s (%d cases, %d failures)\n", failures ? "FAIL" : "PASS", checked, failures);
    return failures ? 1 : 0;
}
