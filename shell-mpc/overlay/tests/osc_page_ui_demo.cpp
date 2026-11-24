// SynthyBoy - MPC overlay: the DEMONSTRATION of the pages of the OSC tab (a host tool built by shell-mpc/accept.sh): renders Osc A, B, C, Sub and Noise (osc_page.h) through the CPU reference renderer into
// PNG files in the directory given as the first argument, one sub directory per look.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <string>

#include <sys/stat.h>

#include "../osc_page.h"
#include "demo_common.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
using ORig = demo::Rig<OscPageUi>;

void renderAll (const std::string& dir)
{
    {
        ORig r (2);   // Osc A: a wavetable with unison
        r.inst.set ("osc1.wavetable", 3.0f); r.inst.set ("osc1.frame", 35.0f); r.inst.set ("osc1.unison.voices", 5.0f); r.inst.set ("osc1.unison.detune", 40.0f);
        r.refresh (3);
        r.picture (dir, "osc_a.png");
        r.stack.openDetail (OscUi::kPageUnison);
        r.refresh (2);
        r.picture (dir, "osc_a_unison.png");
    }
    {
        ORig r (3);   // Osc B: the granular type
        r.inst.set ("osc2.type", 2.0f);
        r.refresh (3);
        r.picture (dir, "osc_b_granular.png");
    }
    {
        ORig r (4);   // Osc C: off
        r.inst.set ("osc3.on", 0.0f);
        r.refresh (3);
        r.picture (dir, "osc_c_off.png");
    }
    {
        ORig r (1);   // Sub
        r.inst.haveLive = true;
        r.refresh (3);
        r.picture (dir, "osc_sub.png");
    }
    {
        ORig r (5);   // Noise (generated)
        r.refresh (3);
        r.picture (dir, "osc_noise.png");
        r.inst.set ("noise.type", 1.0f);   // Noise (sample)
        r.refresh (3);
        r.picture (dir, "osc_noise_sample.png");
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
    {
        ui::theme::setLook (1);
        ui::theme::setDetail (2);
        const std::string dir = base + "/look-1-reduced";
        mkdir (dir.c_str(), 0755);
        renderAll (dir);
    }
    std::printf ("%s: %d images, %d failed\n", demo::g_fails ? "FAIL" : "PASS", demo::g_images, demo::g_fails);
    return demo::g_fails ? 1 : 0;
}
