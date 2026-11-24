// SynthyBoy - actual VST metadata for the native functional FX panel handle.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#define SYNTHYBOY_MPC_OVERLAY 1
#define SYNTHYBOY_MPC_LFO_LIVE 1
#define SYNTHYBOY_MPC_SPECTRUM 1
#define SYNTHYBOY_MPC_PAD_PRESSURE 1
#include "../synthyboy_mpc.cpp"
int main()
{
    namespace h = synthyboy_mpc::nativeui::fxheader;
    PluginInstance plugin;
    AEffect effect {};
    effect.object = &plugin;
    effect.numParams = PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kCount;
    int fails = 0;
    for (int item = 0; item < h::kItems; ++item)
    {
        const int index = PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kFxHeader + 2 + item*h::kFields + h::Page;
        VstParameterProperties properties {};
        const bool handled = pluginDispatcher (&effect, effGetParameterProperties, index, 0, &properties, 0) != 0;
        if (! handled || properties.maxInteger != 2 || properties.minInteger != 0
            || properties.stepInteger != 1 || properties.stepFloat != .5f
            || (properties.flags & kVstParameterIsSwitch) != 0)
        {
            std::fprintf (stderr, "FAIL source%d: panel proxy was quantized as a boolean\n", item);
            ++fails;
        }
        for (int tab = 0; tab < 3; ++tab)
        {
            plugin.nativeFxHeader.publish ({ item, synthyboy::kFxTypeReverb, h::layoutFor (synthyboy::kFxTypeReverb, true), 0,
                                            -1, h::NoBand, tab, 3 }, 0);
            const float value = pluginGetParameter (&effect, index);
            char name[32] {};
            pluginDispatcher (&effect, effGetParamDisplay, index, 0, name, 0);
            const auto panels = synthyboy_mpc::ui::fxl::panelsOf (synthyboy::kFxTypeReverb);
            if (value != tab / 2.0f || std::strcmp (name, panels.names[tab]) != 0)
            {
                std::fprintf (stderr, "FAIL source%d tab%d: value=%g label=%s\n", item, tab, value, name);
                ++fails;
            }
        }
    }
    std::printf ("FX panel VST properties: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
