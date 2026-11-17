// SynthyBoy - MPC overlay: the DEMONSTRATION of the master and voice UI (a host tool built by shell-mpc/accept.sh): renders the pages of MasterUi and VoiceUi, driven by real engine parameters, through the CPU reference
// renderer into PNG files in the directory given as the first argument: both sides of the Master page (the Q groups), a list over it, the Voice page with a drawn pattern, the lit steps and every value of the selected step.
// One sub directory per look.
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o mastervoice_ui_demo shell-mpc/overlay/tests/mastervoice_ui_demo.cpp <libsynthyboy_core.a ...> -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <string>

#include <sys/stat.h>

#include "../master_ui.h"
#include "../voice_ui.h"
#include "demo_common.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
using MRig = demo::Rig<MasterUi>;
using VRig = demo::Rig<VoiceUi>;

void renderAll (const std::string& dir)
{
    {
        MRig r;
        r.inst.set ("master.volume", -6.0f); r.inst.set ("master.stereo_spread", 80.0f); r.inst.set ("master.voice_override", 1.0f); r.inst.set ("master.tune", 8.0f); r.inst.set ("master.transpose", 2.0f);
        r.inst.set ("master.bend_range", 7.0f); r.inst.set ("master.polyphony", 12.0f); r.inst.set ("master.legato", 1.0f); r.inst.set ("master.portamento.time", 180.0f); r.inst.set ("master.portamento.slope", 2.5f);
        r.inst.set ("master.portamento.scaled", 1.0f);
        r.inst.haveLive = true;
        r.inst.liveValues.stripPeak[9] = 0.78f;
        r.inst.liveValues.activeVoices = 5;
        r.refresh (2);
        r.picture (dir, "master_main.png");
        r.tap (r.ui.screen().tune);
        r.picture (dir, "master_pitch_group.png");
        r.tap (r.ui.screen().voiceOverride);
        r.picture (dir, "master_stepper_list.png");
        r.stack.back();
        r.refresh();
        r.inst.set ("master.scale.type", 1.0f); r.inst.set ("master.scale.key", 2.0f); r.inst.set ("master.tuning.a4", 442.0f); r.inst.set ("master.mpe.on", 1.0f); r.inst.set ("master.mpe.bend_range", 24.0f);
        r.tap (r.ui.screen().sideTuning);
        r.refresh (2);
        r.picture (dir, "master_tuning_mpe.png");
        r.inst.set ("master.scale.type", 10.0f);
        r.refresh (2);
        r.picture (dir, "master_tuning_pentatonic.png");
    }
    {
        VRig r;
        r.inst.set ("voicectl.length", 6.0f);
        const float pan[8] = { -70, -30, 0, 40, 80, 50, -20, -60 };
        const float det[8] = { 10, -20, 30, -40, 20, 0, 15, -5 };
        const float cut[8] = { 0, 30, 60, 90, 60, 30, 0, -30 };
        for (int s = 0; s < 8; ++s)
        {
            r.inst.set ("voicectl.s" + std::to_string (s + 1) + ".pan", pan[s]);
            r.inst.set ("voicectl.s" + std::to_string (s + 1) + ".detune", det[s]);
            r.inst.set ("voicectl.s" + std::to_string (s + 1) + ".cutoff", cut[s]);
        }
        r.inst.set ("voicectl.rand.pan", 25.0f); r.inst.set ("voicectl.rand.detune", 40.0f); r.inst.set ("voicectl.rand.cutoff", 15.0f); r.inst.set ("voicectl.rand.env", 10.0f);
        r.inst.set ("voicectl.scale.env", 100.0f); r.inst.set ("voicectl.scale.lfo", 100.0f);
        r.inst.haveLive = true;
        for (float& s : r.inst.liveValues.voiceStep)
            s = -1.0f;
        r.inst.liveValues.voiceStep[0] = 3.0f;
        r.inst.liveValues.voiceStep[1] = 2.0f;
        r.refresh (2);
        r.picture (dir, "voice_pan.png");
        r.tapAt (r.ui.screen().grid.columnRect (4).x + 20, r.ui.screen().grid.columnRect (4).y + 100);
        r.tapAt (r.ui.screen().rowList.rowRect (2).x + 10, r.ui.screen().rowList.rowRect (2).y + 10);
        r.picture (dir, "voice_cutoff_step5.png");
        r.tap (r.ui.screen().randPan);
        r.picture (dir, "voice_random_group.png");
        r.tap (r.ui.screen().scaleLfoMode);
        r.picture (dir, "voice_mode_list.png");
    }
}
} // namespace

int main (int argc, char** argv)
{
    const std::string base = argc > 1 ? argv[1] : ".";
    mkdir (base.c_str(), 0755);
    for (int look = 1; look <= 3; ++look)
    {
        ui::theme::setLook (look);
        const std::string dir = base + "/look-" + std::to_string (look);
        mkdir (dir.c_str(), 0755);
        renderAll (dir);
    }
    {   // the reduced level of detail (the Key 61)
        ui::theme::setLook (1);
        ui::theme::setDetail (2);
        const std::string dir = base + "/look-1-reduced";
        mkdir (dir.c_str(), 0755);
        MRig r;
        r.refresh (2);
        r.picture (dir, "master_main.png");
        ui::theme::setDetail (1);
    }
    ui::theme::setLook (1);
    std::printf ("%s: %d images, %d failed\n", demo::g_fails ? "FAIL" : "PASS", demo::g_images, demo::g_fails);
    return demo::g_fails ? 1 : 0;
}
