// Authentic VoiceUi scene + matching parameter state for offline native rendering.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <fstream>
#include <iomanip>
#include "../voice_ui.h"
#include "demo_common.h"
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;
namespace {
struct Instance : demo::Inst
{
    nativeui::voice::ViewState state;
    bool nativeVoiceSequenceControls() const noexcept override { return true; }
    void setNativeVoiceState (const nativeui::voice::ViewState& s) noexcept override { state = s; }
};
}
int main (int argc, char** argv)
{
    if (argc != 5) return 2;
    const bool rate = std::atoi (argv[1]) != 0, active = std::atoi (argv[2]) != 0;
    ui::QGroup::setWideDevice (SYNTHYBOY_MPC_UI_GEN == 2);
    demo::Rig<VoiceUi, Instance> r;
    r.ui.setHostBackground (true);
    r.inst.set ("voicectl.rand.pan", 25); r.inst.set ("voicectl.rand.detune", 14);
    r.inst.set ("voicectl.rand.cutoff", 15); r.inst.set ("voicectl.rand.env", 10);
    r.inst.set ("voicectl.scale.env", 125); r.inst.set ("voicectl.scale.lfo", 150);
    r.inst.set ("voicectl.scale.lfo_mode", rate ? 1.0f : 0.0f);
    r.inst.set ("voicectl.rand.detune10x", active ? 1.0f : 0.0f);
    for (const char* key : { "a", "b", "c", "sub", "noise" })
        r.inst.set (std::string ("voicectl.osc.") + key, active ? 1.0f : 0.0f);
    const int amounts[8] { -40, -15, 20, 65, 40, 0, -30, -55 };
    for (int i = 1; i <= 8; ++i)
        r.inst.set ("voicectl.s" + std::to_string (i) + ".pan", static_cast<float> (amounts[i-1]));
    r.inst.haveLive = true;
    for (float& step : r.inst.liveValues.voiceStep) step = -1;
    r.inst.liveValues.voiceStep[0] = 0; r.inst.liveValues.voiceStep[1] = 3;
    r.refresh (2);
    r.ui.screen().setActiveGroup (1);
    r.refresh (2);
    auto scene = std::make_unique<gpu::SceneData>(); gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800); r.ui.draw (sb);
    if (sb.overflow) return 3;
    std::vector<uint32_t> pixels; renderScene (*scene, pixels);
    std::ofstream raw (argv[3], std::ios::binary);
    for (int y = 110; y < 738; ++y) for (int x = 0; x < 1280; ++x)
    {
        const auto p = pixels[y*1280+x];
        const char c[] { static_cast<char> (p>>16), static_cast<char> (p>>8), static_cast<char> (p), static_cast<char> (p>>24) };
        raw.write (c, 4);
    }
    const int base = static_cast<int> (synthyboy::paramCount()) + 82;
    std::ofstream js (argv[4]); js << std::setprecision (9);
    js << "{\"synthetic_fixture\":true,\"device_test\":false,\"tab_name\":\"Voice\",\"base\":" << base << ",\"params\":{";
    bool first = true;
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const auto& p = synthyboy::paramTable()[i];
        if (std::strncmp (p.key,"voicectl.",9)!=0) continue;
        char text[80] {};
        if (p.numChoices > 0)
            std::snprintf (text,sizeof text,"%s",p.choices[std::clamp (static_cast<int> (std::lround (r.inst.param (static_cast<int> (i))-p.min)),0,p.numChoices-1)]);
        else
            uibind::formatValue (p, r.inst.param (static_cast<int> (i)), text, sizeof text);
        if (!first) js << ',';
        first = false;
        js << '\"' << i << "\":{\"key\":\"" << p.key << "\",\"norm\":" << synthyboy::toNormalised (p, r.inst.param (static_cast<int> (i))) << ",\"display\":\"" << text << "\"}";
    }
    char value[32] {}; ui::VoiceGrid::formatValue (0, -.4f, value, sizeof value);
    const int seq = base + nativeui::kVoiceSequence;
    js << "},\"virtual\":{\"Parameter " << seq << "\":{\"norm\":0,\"display\":\"PAN\"},\"Parameter " << seq+1 << "\":{\"norm\":0,\"display\":\"STEP 1\"},\"Parameter " << seq+2 << "\":{\"norm\":0.3,\"display\":\"" << value << "\"}}}";
    return raw && js ? 0 : 4;
}
