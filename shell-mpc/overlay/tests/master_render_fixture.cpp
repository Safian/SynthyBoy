// Authentic MasterUi scene + matching parameter state for offline native rendering
// (scripts/render_ui_screen_master_preview.py). A synthetic fixture, never a device capture.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#include "../master_ui.h"
#include "demo_common.h"
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

int main (int argc, char** argv)
{
    if (argc != 4) return 2;   // side (0 MAIN, 1 TUNING & MPE), raw RGBA out, state JSON out
    const int side = std::atoi (argv[1]);
    ui::QGroup::setWideDevice (SYNTHYBOY_MPC_UI_GEN == 2);
    demo::Rig<MasterUi> r;
    const std::pair<const char*, float> values[] {
        { "master.volume", -6.0f }, { "master.stereo_spread", 80.0f }, { "master.stereo_routing", 100.0f },
        { "master.tune", 0.0f }, { "master.transpose", -12.0f }, { "master.bend_range", 2.0f }, { "master.bend_down_range", 12.0f },
        { "master.polyphony", 16.0f }, { "master.legato", 0.0f }, { "master.poly_legato", 1.0f }, { "master.velocity_track", 60.0f },
        { "master.portamento.time", 120.0f }, { "master.portamento.slope", 2.5f }, { "master.portamento.scaled", 1.0f },
        { "master.scale.type", 1.0f }, { "master.scale.key", 9.0f }, { "master.mpe.on", 1.0f }, { "master.mpe.bend_range", 48.0f },
        { "master.tuning.mts_esp", 1.0f },
    };
    for (const auto& [key, value] : values)
        r.inst.set (key, value);
    r.inst.padOn = true;
    r.inst.padPercent = 35.0f;
    r.inst.limiterOn = true;
    r.inst.haveLive = true;
    r.inst.liveValues.activeVoices = 4;
    r.inst.liveValues.masterPeakL = 0.78f;
    r.inst.liveValues.masterPeakR = 0.70f;
    r.ui.screen().setSide (side == 1 ? ui::MasterScreen::SideTuning : ui::MasterScreen::SideMain);
    r.refresh (2);
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    if (sb.overflow) return 3;
    std::vector<uint32_t> pixels;
    renderScene (*scene, pixels);
    std::ofstream raw (argv[2], std::ios::binary);
    for (int y = MasterUi::kTabY; y < MasterUi::kTabY + MasterUi::kTabH; ++y)
        for (int x = 0; x < 1280; ++x)
        {
            const auto p = pixels[y * 1280 + x];
            const char c[] { static_cast<char> (p >> 16), static_cast<char> (p >> 8), static_cast<char> (p), static_cast<char> (p >> 24) };
            raw.write (c, 4);
        }
    std::ofstream js (argv[3]);
    js << std::setprecision (9);
    js << "{\"synthetic_fixture\":true,\"device_capture\":false,\"tab_name\":\"Master\",\"side\":" << side
       << ",\"param_count\":" << synthyboy::paramCount() << ",\"params\":{";
    bool first = true;
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        const auto& p = synthyboy::paramTable()[i];
        if (std::strncmp (p.key, "master.", 7) != 0 && std::strncmp (p.key, "quality.", 8) != 0 && std::strcmp (p.key, "voice.phase_legato") != 0)
            continue;
        const float value = r.inst.param (static_cast<int> (i));
        char text[80] {};
        if (p.numChoices > 0)
            std::snprintf (text, sizeof text, "%s", p.choices[std::clamp (static_cast<int> (std::lround (value - p.min)), 0, p.numChoices - 1)]);
        else
            uibind::formatValue (p, value, text, sizeof text);
        if (! first) js << ',';
        first = false;
        js << '"' << i << "\":{\"key\":\"" << p.key << "\",\"norm\":" << synthyboy::toNormalised (p, value) << ",\"display\":\"" << text << "\"}";
    }
    js << "}}";
    return raw && js ? 0 : 4;
}
