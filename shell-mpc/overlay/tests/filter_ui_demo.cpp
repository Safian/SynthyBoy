// SynthyBoy - MPC overlay: the DEMONSTRATION of the filter UI (a host tool built by shell-mpc/accept.sh): renders the page of FilterUi, driven by real engine parameters and the real response curves of the engine's filter
// (curve_source.h), through the CPU reference renderer into PNG files in the directory given as the first argument: F1, F2 and F1+F2, a Formant filter with its XY pad, the dragged point with its values, the OUTPUT popup,
// the model list, the style list and the modulations of the cutoff. One sub directory per look.
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o filter_ui_demo shell-mpc/overlay/tests/filter_ui_demo.cpp <libsynthyboy_core.a ...> -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <string>

#include <sys/stat.h>

#include "../filter_ui.h"
#include "demo_common.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
using Rig = demo::Rig<FilterUi>;

void renderAll (const std::string& dir)
{
    {   // F1: a resonant 24 dB low pass with a keytrack and a modulated cutoff
        Rig r;
        r.inst.set ("filter1.model", 2.0f); r.inst.set ("filter1.cutoff", 1800.0f); r.inst.set ("filter1.resonance", 55.0f); r.inst.set ("filter1.drive", 6.0f);
        r.inst.set ("filter1.mix", 100.0f); r.inst.set ("filter1.keytrack", 35.0f); r.inst.set ("filter1.level", -3.0f); r.inst.set ("filter1.style", 1.0f);
        r.inst.set ("filter1.blend", 40.0f); r.inst.set ("filter1.pan", -20.0f); r.inst.set ("filter1.var", 60.0f);
        r.inst.mod (1, 7, "filter1.cutoff", 45.0f);
        static float live[4] = { 2600.0f, 55.0f, 12000.0f, 20.0f };
        r.in.targets = live;
        r.refresh (2);
        r.picture (dir, "filter1_main.png");
        r.tap (r.ui.full().keytrack);
        r.picture (dir, "filter1_track_group.png");
        r.tap (r.ui.full().blend);
        r.picture (dir, "filter1_out_group.png");
        const ui::Rect d = r.ui.displayRect();
        r.drag (d.x + d.w / 3, d.y + d.h / 2, d.x + d.w / 2, d.y + d.h / 3, false);
        r.picture (dir, "filter1_dragging_point.png");
        r.ui.dropGesture();
        r.refresh();
    }
    {   // F2 a band pass and the two together in parallel and in series
        Rig r;
        r.inst.set ("filter1.on", 1.0f);
        r.inst.set ("filter2.on", 1.0f);
        r.inst.set ("filter2.model", 8.0f); r.inst.set ("filter2.cutoff", 900.0f); r.inst.set ("filter2.resonance", 35.0f); r.inst.set ("filter2.style", 3.0f);
        r.inst.set ("filter1.model", 2.0f); r.inst.set ("filter1.cutoff", 4200.0f); r.inst.set ("filter1.resonance", 30.0f);
        r.refresh (2);
        const ui::Rect s = r.ui.full().view.cell (1, 4);
        r.tapAt (s.x + s.w / 2, s.y + s.h / 2);   // the second cell is F2
        r.picture (dir, "filter2_main.png");
        r.tap (r.ui.full().both);   // the F1+F2 view toggle
        r.picture (dir, "dual_parallel.png");
        r.inst.set ("filter.routing", 1.0f);
        r.refresh (2);
        r.picture (dir, "dual_series.png");
    }
    {   // the Formant filter: the XY pad, its block, the Q group FORMANT
        Rig r;
        r.inst.set ("filter1.model", 5.0f); r.inst.set ("filter1.formant.x", 62.0f); r.inst.set ("filter1.formant.y", 38.0f); r.inst.set ("filter1.formant.resonance", 80.0f);
        r.inst.set ("filter1.formant.spread", 30.0f); r.inst.set ("filter1.formant.transpose", -3.0f);
        r.refresh (2);
        r.tap (r.ui.full().formantTrans);
        r.picture (dir, "formant.png");
    }
    {   // the lists: the models and the styles
        Rig r;
        r.inst.set ("filter1.model", 6.0f);
        r.refresh (2);
        r.tap (r.ui.full().model);
        r.picture (dir, "model_list.png");
        r.stack.back();
        r.refresh();
        r.tap (r.ui.full().style);
        r.picture (dir, "style_list.png");
    }
    {   // the modulations of the cutoff: a long press on the knob
        Rig r;
        r.inst.mod (1, 7, "filter1.cutoff", 45.0f);
        r.inst.mod (2, 1, "filter1.cutoff", -30.0f);
        r.refresh (2);
        r.tap (r.ui.full().cutoff, 700);
        r.picture (dir, "modulations_page.png");
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
        Rig r;
        r.inst.set ("filter1.cutoff", 1800.0f); r.inst.set ("filter1.resonance", 55.0f);
        r.refresh (2);
        r.picture (dir, "filter1_main.png");
        ui::theme::setDetail (1);
    }
    ui::theme::setLook (1);
    std::printf ("%s: %d images, %d failed\n", demo::g_fails ? "FAIL" : "PASS", demo::g_images, demo::g_fails);
    return demo::g_fails ? 1 : 0;
}
