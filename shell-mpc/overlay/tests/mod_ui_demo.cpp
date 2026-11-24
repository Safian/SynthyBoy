// SynthyBoy - MPC overlay: the DEMONSTRATION of the modulation matrix and the macros UI (a host tool built by shell-mpc/accept.sh): renders the pages of ModUi and MacroUi, driven by real engine parameters, through the
// CPU reference renderer into PNG files in the directory given as the first argument: the matrix (all, by source, by destination), the page of a slot (the three Q groups), the grouped lists of the sources and the
// targets, the macros with their chips and the live spectrum, the targets page of a macro. One sub directory per look.
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o mod_ui_demo shell-mpc/overlay/tests/mod_ui_demo.cpp <libsynthyboy_core.a ...> -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "../macro_ui.h"
#include "../mod_ui.h"
#include "demo_common.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
using ModRig = demo::Rig<ModUi>;
struct NativeMacroInstance : demo::Inst
{
    int pendingMacroTarget = -1;
    bool macroTargetsPublished = false;
    bool nativeMacroTargetsSupported() const noexcept override { return true; }
    void publishNativeMacroTargets (bool on) noexcept override { macroTargetsPublished = on; }
    int takeNativeMacroTarget() noexcept override
    {
        const int target = macroTargetsPublished ? pendingMacroTarget : -1;
        pendingMacroTarget = -1;
        return target;
    }
};
using MacroRig = demo::Rig<MacroUi, NativeMacroInstance>;

void fillMatrix (demo::Inst& i)
{
    i.mod (1, 7, "filter1.cutoff", 65.0f, true);
    i.mod (2, 1, "osc1.tune", -30.0f, true);
    i.mod (3, 19, "osc1.frame", 85.0f);
    i.mod (4, 27, "lfo1.rate", 50.0f);
    i.mod (5, 23, "master.volume", 40.0f);
    i.mod (6, 8, "osc2.pan", 25.0f, true);
    i.mod (7, 25, "filter1.resonance", 70.0f, false, true);
    i.mod (8, 9, "filter1.drive", -45.0f, true);
    i.mod (9, 3, "osc2.level", 55.0f);
    i.mod (10, 20, "filter2.cutoff", 35.0f, true);
    i.mod (11, 1, "filter2.resonance", 20.0f);
    i.mod (12, 10, "delay.mix", 30.0f);
    i.set ("mod01.aux_source", 27.0f); i.set ("mod01.aux_output", 140.0f); i.set ("mod01.aux_power", 3.5f); i.set ("mod01.power", -2.0f); i.set ("mod01.rise", 120.0f); i.set ("mod01.fall", 800.0f);
    i.set ("mod01.delay", 250.0f); i.set ("mod01.stereo", 1.0f);
    i.haveLive = true;
    i.liveValues.source[6] = 0.6f; i.liveValues.source[0] = 0.7f; i.liveValues.source[7] = -0.3f; i.liveValues.source[2] = 0.4f;
}

void renderAll (const std::string& dir)
{
    {   // the matrix: all slots, the selected row with its Q group, the lists
        ModRig r;
        fillMatrix (r.inst);
        r.refresh (2);
        r.picture (dir, "matrix_all.png");
        r.tap (r.ui.matrix().windowRows[2].number);
        r.picture (dir, "matrix_row3_selected.png");
        r.drag (600, r.ui.matrix().listRect().y + 300, 600, r.ui.matrix().listRect().y + 40, true);
        r.picture (dir, "matrix_scrolled.png");
        r.tap (r.ui.matrix().tabSource);
        r.tap (r.ui.matrix().sourceFilter);
        r.picture (dir, "matrix_by_source.png");
        r.tap (r.ui.matrix().tabDest);
        r.tap (r.ui.matrix().destFilter);
        r.tap (r.ui.matrix().destFilter);
        r.tap (r.ui.matrix().destFilter);
        r.tap (r.ui.matrix().destFilter);
        r.picture (dir, "matrix_by_destination.png");
    }
    {
        ModRig r;
        fillMatrix (r.inst);
        r.refresh (2);
        r.tap (r.ui.matrix().windowRows[0].sourceBtn);
        r.picture (dir, "source_list_groups.png");
        const ui::Rect g = r.ui.list().rowRect (2);
        r.tapAt (g.x + g.w / 2, g.y + g.h / 2);
        r.picture (dir, "source_list_lfos.png");
        r.stack.back();
        r.refresh();
        r.tap (r.ui.matrix().windowRows[0].destBtn);
        const ui::Rect g1 = r.ui.list().rowRect (2);
        r.tapAt (g1.x + g1.w / 2, g1.y + g1.h / 2);
        r.picture (dir, "target_list_group.png");
    }
    {   // the empty matrix
        ModRig r;
        r.refresh (2);
        r.picture (dir, "matrix_empty.png");
    }
    {   // the page of a slot: AUX, SHAPE and TIME, each block a Q group
        ModRig r;
        fillMatrix (r.inst);
        r.refresh (2);
        r.tap (r.ui.matrix().windowRows[0].detailBtn);
        r.picture (dir, "slot_page_aux.png");
        r.tap (r.ui.detail().stereo);
        r.picture (dir, "slot_page_shape.png");
        r.tap (r.ui.detail().rise);
        r.picture (dir, "slot_page_time.png");
        r.tap (r.ui.detail().delaySync);
        r.picture (dir, "slot_page_time_beats.png");
    }
    {   // the macros: values, chips, the spectrum of a chord
        MacroRig r;
        r.inst.set ("macro.1", 35.0f); r.inst.set ("macro.2", 70.0f); r.inst.set ("macro.3", 12.0f); r.inst.set ("macro.4", 95.0f);
        r.inst.set ("macro.5", 50.0f); r.inst.set ("macro.6", 0.0f); r.inst.set ("macro.7", 80.0f); r.inst.set ("macro.8", 20.0f);
        const int m[8] = { uibind::macroSource (1), uibind::macroSource (2), uibind::macroSource (3), uibind::macroSource (4), uibind::macroSource (5), uibind::macroSource (6), uibind::macroSource (7), uibind::macroSource (8) };
        r.inst.mod (1, m[0], "filter1.cutoff", 60.0f);
        r.inst.mod (2, m[0], "filter1.drive", 30.0f);
        r.inst.mod (3, m[0], "osc1.frame", 45.0f);
        r.inst.mod (4, m[1], "delay.mix", 50.0f);
        r.inst.mod (5, m[1], "reverb.mix", 40.0f);
        r.inst.mod (6, m[2], "osc1.unison.detune", 35.0f);
        r.inst.mod (7, m[3], "lfo1.rate", 50.0f);
        r.inst.mod (8, m[3], "chorus.depth", 25.0f);
        r.inst.mod (9, m[3], "osc2.pan", 25.0f, true);
        r.inst.mod (10, m[3], "filter2.cutoff", 25.0f);
        r.inst.mod (11, m[3], "master.volume", 25.0f);
        r.inst.mod (12, m[4], "env1.attack", 40.0f);
        r.inst.mod (13, m[6], "noise.level", 40.0f);
        r.inst.mod (14, m[6], "distortion.drive", 40.0f);
        static SpectrumRing ring;
        r.inst.ring = &ring;
        std::vector<float> buf (512);
        double ph[4] = {};
        const double f[4] = { 220.0, 440.0, 880.0, 3500.0 };
        for (int blk = 0; blk < 40; ++blk)
        {
            for (float& s : buf)
            {
                s = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    ph[k] += 2.0 * 3.14159265 * f[k] / 48000.0;
                    s += 0.12f * static_cast<float> (std::sin (ph[k])) / static_cast<float> (1 + k);
                }
            }
            ring.push (buf.data(), nullptr, static_cast<int> (buf.size()));
            r.refresh();
        }
        r.picture (dir, "macros_bank1.png");
        r.tap (r.ui.view().cards[5].knob);
        r.picture (dir, "macros_bank2.png");
        r.inst.pendingMacroTarget = 0;
        r.refresh();
        r.picture (dir, "macro_targets.png");
        r.tap (r.ui.targetsPage().add);
        r.picture (dir, "macro_add_target.png");
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
        ModRig r;
        fillMatrix (r.inst);
        r.refresh (2);
        r.picture (dir, "matrix_all.png");
        ui::theme::setDetail (1);
    }
    ui::theme::setLook (1);
    std::printf ("%s: %d images, %d failed\n", demo::g_fails ? "FAIL" : "PASS", demo::g_images, demo::g_fails);
    return demo::g_fails ? 1 : 0;
}
