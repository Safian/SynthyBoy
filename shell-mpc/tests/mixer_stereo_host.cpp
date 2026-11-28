// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
// Focused native stereo meter acceptance through the actual VST dispatcher.
#define SYNTHYBOY_MPC_LFO_LIVE 1
#define SYNTHYBOY_MPC_SPECTRUM 1
#define SYNTHYBOY_MPC_PAD_PRESSURE 1
#include "../synthyboy_mpc.cpp"
namespace
{
int failures = 0;
void check (bool ok, const char* message)
{
    if (! ok) { std::fprintf (stderr, "FAIL: %s\n", message); ++failures; }
}
VstIntPtr callback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}
struct Rig
{
    PluginInstance plugin;
    AEffect effect {};
    Rig()
    {
        effect.object = &plugin;
        effect.setParameter = pluginSetParameter;
        effect.getParameter = pluginGetParameter;
        effect.dispatcher = pluginDispatcher;
        effect.numParams = PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kCount;
        plugin.audioMaster = callback;
        plugin.hostKnows.store (true, std::memory_order_release);
    }
};
}
#include "mixer_stereo_checks.h"
int main()
{
    testMixerStereoSignalAndReadonlyBridge();
    return failures != 0;
}
