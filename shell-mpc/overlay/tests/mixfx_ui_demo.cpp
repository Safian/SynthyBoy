// SynthyBoy - MPC overlay: the DEMONSTRATION of the mixer and the FX UI (a host tool built by shell-mpc/accept.sh): renders the pages of MixerUi and FxUi, driven by real engine parameters, through the CPU reference
// renderer into PNG files in the directory given as the first argument: the mixer with its meters, a selected strip, the destination list, the chain of the effects with the EQ, the compressor (two pages), the delay,
// the distortion, the FX filter, the chorus and the reverb (no display: the knobs take the room), the list of the effect types. One sub directory per look.
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o mixfx_ui_demo shell-mpc/overlay/tests/mixfx_ui_demo.cpp <libsynthyboy_core.a ...> -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <string>

#include <sys/stat.h>

#include "../fx_ui.h"
#include "../mixer_ui.h"
#include "demo_common.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
using MixRig = demo::Rig<MixerUi>;
struct FxRig : demo::Rig<FxUi>
{
    FxRig() { ui.setNativeControls (false); }
};

void addEffect (FxRig& r, int type)
{
    r.tap (r.ui.screen().chainBar.plusCard);
    r.ui.list().state.current = type - 1;
    r.ui.list().showCurrent();
    const ui::Rect row = r.ui.list().rowRect (type - 1);
    r.tapAt (row.x + row.w / 2, row.y + row.h / 2);
}
void setPool (FxRig& r, int type, const char* key, float plain)
{
    const int slot = r.ui.cardItem (r.ui.selectedSlot()).index, idx = demo::Inst::idx (key);
    const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (type);
    for (int p = 0; p < d.count; ++p)
        if (d.source[p] == idx)
            r.inst.setAt (synthyboy::fxInstanceKeys (slot).p[p], synthyboy::fxInstanceNormalise (type, p, plain));
}

void renderAll (const std::string& dir)
{
    {   // the mixer: levels in % and in dB, pans, sends, meters
        MixRig r;
        r.inst.set ("sub.level", 70.0f); r.inst.set ("osc1.level", 50.0f); r.inst.set ("osc2.level", 62.0f); r.inst.set ("osc3.level", 35.0f); r.inst.set ("noise.level", 25.0f);
        r.inst.set ("filter1.level", -3.0f); r.inst.set ("filter2.level", -9.0f); r.inst.set ("bus1.level", -6.0f); r.inst.set ("bus2.level", 0.0f); r.inst.set ("master.volume", -4.0f);
        r.inst.set ("osc1.pan", -35.0f); r.inst.set ("osc2.pan", 35.0f); r.inst.set ("osc1.bus1_send", 40.0f); r.inst.set ("osc2.bus2_send", 65.0f); r.inst.set ("filter1.bus1_send", 20.0f);
        r.inst.set ("osc3.on", 0.0f); r.inst.set ("noise.on", 0.0f); r.inst.set ("sub.destination", 3.0f); r.inst.set ("osc1.destination", 0.0f); r.inst.set ("osc2.destination", 1.0f);
        r.inst.haveLive = true;
        const float peaks[10] = { 0.45f, 0.72f, 0.64f, 0.0f, 0.0f, 0.83f, 0.5f, 0.3f, 0.12f, 0.9f };
        for (int i = 0; i < 10; ++i)
            r.inst.liveValues.stripPeak[i] = peaks[i];
        r.refresh (3);
        r.picture (dir, "mixer.png");
        r.tap (r.ui.screen().strips[1].send1);
        r.picture (dir, "mixer_strip_selected.png");
        r.tap (r.ui.screen().strips[2].destButton);
        r.picture (dir, "mixer_destination_list.png");
    }
    {   // the chain with an EQ and the compressor (pages), the delay, the distortion, the FX filter
        FxRig r;
        r.refresh();
        addEffect (r, synthyboy::kFxTypeEq);
        setPool (r, synthyboy::kFxTypeEq, "eq.low.cutoff", 90.0f); setPool (r, synthyboy::kFxTypeEq, "eq.low.gain", 4.0f); setPool (r, synthyboy::kFxTypeEq, "eq.mid.cutoff", 1800.0f);
        setPool (r, synthyboy::kFxTypeEq, "eq.mid.gain", -7.0f); setPool (r, synthyboy::kFxTypeEq, "eq.mid.resonance", 55.0f); setPool (r, synthyboy::kFxTypeEq, "eq.high.cutoff", 7500.0f);
        setPool (r, synthyboy::kFxTypeEq, "eq.high.gain", 6.0f); setPool (r, synthyboy::kFxTypeEq, "eq.mid2.mode", 1.0f); setPool (r, synthyboy::kFxTypeEq, "eq.mid2.cutoff", 4200.0f); setPool (r, synthyboy::kFxTypeEq, "eq.mid2.gain", 5.0f);
        r.refresh (2);
        r.picture (dir, "fx_eq.png");
        addEffect (r, synthyboy::kFxTypeCompressor);
        setPool (r, synthyboy::kFxTypeCompressor, "compressor.bands", 0.0f);
        setPool (r, synthyboy::kFxTypeCompressor, "compressor.low.upper_threshold", -28.0f); setPool (r, synthyboy::kFxTypeCompressor, "compressor.low.upper_ratio", 70.0f);
        setPool (r, synthyboy::kFxTypeCompressor, "compressor.mid.upper_threshold", -20.0f); setPool (r, synthyboy::kFxTypeCompressor, "compressor.mid.upper_ratio", 40.0f); setPool (r, synthyboy::kFxTypeCompressor, "compressor.mid.lower_ratio", 25.0f);
        setPool (r, synthyboy::kFxTypeCompressor, "compressor.high.upper_threshold", -14.0f); setPool (r, synthyboy::kFxTypeCompressor, "compressor.high.upper_ratio", 85.0f); setPool (r, synthyboy::kFxTypeCompressor, "compressor.high.gain", 3.0f);
        r.refresh (2);
        r.picture (dir, "fx_compressor_page1.png");
        r.ui.screen().slotEditor.setPage (1); r.refresh();
        r.picture (dir, "fx_compressor_page2.png");
        addEffect (r, synthyboy::kFxTypeDelay);
        setPool (r, synthyboy::kFxTypeDelay, "delay.sync", 1.0f); setPool (r, synthyboy::kFxTypeDelay, "delay.division", 5.0f); setPool (r, synthyboy::kFxTypeDelay, "delay.division2", 4.0f);
        setPool (r, synthyboy::kFxTypeDelay, "delay.feedback", 62.0f); setPool (r, synthyboy::kFxTypeDelay, "delay.style", 1.0f);
        r.refresh (2);
        r.picture (dir, "fx_delay.png");
        setPool (r, synthyboy::kFxTypeDelay, "delay.style", 2.0f);
        r.refresh (2);
        r.picture (dir, "fx_delay_pingpong.png");
        addEffect (r, synthyboy::kFxTypeDistortion);
        setPool (r, synthyboy::kFxTypeDistortion, "distortion.type", 3.0f); setPool (r, synthyboy::kFxTypeDistortion, "distortion.drive", 12.0f);
        r.refresh (2);
        r.picture (dir, "fx_distortion.png");
        addEffect (r, synthyboy::kFxTypeFilter);
        setPool (r, synthyboy::kFxTypeFilter, "fxfilter.model", 2.0f); setPool (r, synthyboy::kFxTypeFilter, "fxfilter.cutoff", 1500.0f); setPool (r, synthyboy::kFxTypeFilter, "fxfilter.resonance", 60.0f);
        r.refresh (2);
        r.picture (dir, "fx_filter.png");
    }
    {   // the effects without a display: the knobs have the room
        FxRig r;
        r.refresh();
        addEffect (r, synthyboy::kFxTypeChorus);
        r.refresh (2);
        r.picture (dir, "fx_chorus.png");
        addEffect (r, synthyboy::kFxTypeReverb);
        r.refresh (2);
        r.picture (dir, "fx_reverb.png");
        r.ui.screen().slotEditor.setPage (1); r.refresh();
        r.picture (dir, "fx_reverb_page2.png");
        r.tap (r.ui.screen().chainBar.plusCard);
        r.picture (dir, "fx_add_list.png");
    }
    {   // the empty chain
        FxRig r;
        r.refresh (2);
        r.picture (dir, "fx_empty.png");
    }
    if (&ui::theme::L() == &ui::theme::kLook1)
    {
        // The generated skin supplies the card faces and labels over this preview layer.
        demo::Rig<FxUi> native;
        native.refresh();
        native.picture (dir, "fx_native_gallery_foreground.png");
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
        MixRig r;
        r.refresh (2);
        r.picture (dir, "mixer.png");
        ui::theme::setDetail (1);
    }
    ui::theme::setLook (1);
    std::printf ("%s: %d images, %d failed\n", demo::g_fails ? "FAIL" : "PASS", demo::g_images, demo::g_fails);
    return demo::g_fails ? 1 : 0;
}
