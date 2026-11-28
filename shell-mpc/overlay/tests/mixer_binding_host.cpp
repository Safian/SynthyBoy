// SynthyBoy - MPC overlay tests: the MIXER page as DATA (mixer_ui.h, ui/binding.h): the parameter rows resolve in the engine, the Q-Link banks (a table of row indices) publish the right levels on the Key 61 and on the
// sixteen-knob XL, a hardware turn and a native cell write reach the engine parameter (a switched-off strip takes none), the native cells lie inside their strip. The page holds no control (owner decision 2026-11-27).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>

#include "../mixer_ui.h"
#include "demo_common.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
int g_fails = 0;
void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_fails;
}
std::string keyOf (int code)
{
    if (code < 0)
        return "";
    return synthyboy::paramTable()[QRouter::indexOf (code)].key;
}

void testRows()
{
    bool keys = true, ids = true;
    int params = 0;
    for (int s = 0; s < MixerUi::kStrips; ++s)
        for (int t = 0; t < MixerUi::kTypes; ++t)
        {
            const binding::Row& row = MixerUi::kRows[MixerUi::rowOf (s, t)];
            ids = ids && row.id == MixerUi::kStripBase + 10 * s + t;
            if (row.hasParam())
            {
                ++params;
                keys = keys && uibind::indexOfKey (row.key) >= 0;
            }
        }
    check (ids, "Mixer rows: the id of a row is kStripBase + 10 * strip + type");
    check (keys, "Mixer rows: every row key is an engine parameter");
    check (params > 0, "Mixer rows: the keyed rows were counted");
    check (MixerUi::kRows[MixerUi::rowOf (7, MixerUi::kPower)].inverted && ! MixerUi::kRows[MixerUi::rowOf (0, MixerUi::kPower)].inverted, "Mixer rows: the bus FX bypass is an inverted switch, a source switch is not");
    check (MixerUi::kRows[MixerUi::rowOf (2, MixerUi::kPan)].format == binding::Format::PanLR, "Mixer rows: the pan is written C / L / R");
}

void testQ()
{
    demo::Rig<MixerUi> r;
    for (const char* key : { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on", "filter1.on", "filter2.on" })
        r.inst.set (key, 1.0f);
    ui::QGroup::setWideDevice (false);
    const char* const key61[3][4] = { { "sub.level", "osc1.level", "osc2.level", "osc3.level" }, { "noise.level", "filter1.level", "filter2.level", "master.volume" }, { "bus1.level", "bus2.level", "", "" } };
    for (int g = 0; g < 3; ++g)
    {
        r.ui.setActiveGroup (g);
        r.refresh();
        bool ok = r.inst.qN == 16;
        for (int k = 0; k < 4; ++k)
            ok = ok && keyOf (r.inst.q[k]) == key61[g][k];
        check (ok, "Mixer, Key 61: group " + std::to_string (g) + " active: Q 1 .. 4 are its levels, sixteen slots published");
        // the next group follows on 5 .. 8 (wrapping)
        const int next = (g + 1) % 3;
        bool follows = true;
        for (int k = 0; k < 4; ++k)
            follows = follows && keyOf (r.inst.q[4 + k]) == key61[next][k];
        check (follows, "Mixer, Key 61: the next group (wrapping) is on Q 5 .. 8");
    }
    ui::QGroup::setWideDevice (true);
    r.ui.selectStrip (6);
    r.refresh();
    const char* order[] = { "sub.level", "osc1.level", "osc2.level", "osc3.level", "noise.level", "filter1.level", "filter2.level", "bus1.level", "bus2.level", "master.volume" };
    bool xl = true;
    for (int q = 0; q < 10; ++q)
        xl = xl && keyOf (r.inst.q[q]) == order[q];
    check (xl, "Mixer, XL: the ten levels are Q 1 .. 10 in strip order");
    r.ui.selectStrip (9);
    r.refresh();
    check (r.ui.activeGroup() == 2, "Mixer, XL: MAIN is in the third bank");
    ui::QGroup::setWideDevice (false);
    r.refresh();
    check (r.ui.activeGroup() == 1, "Mixer, Key 61: MAIN is in the second bank again after the device rule changed");
    bool noPanOrSend = true;
    for (int i = 0; i < 16; ++i)
    {
        const std::string k = keyOf (r.inst.q[i]);
        noPanOrSend = noPanOrSend && k.find (".pan") == std::string::npos && k.find ("send") == std::string::npos;
    }
    check (noPanOrSend, "Mixer: no PAN and no bus send is a Q target (touch only)");
}

void testWrites()
{
    demo::Rig<MixerUi> r;
    ui::QGroup::setWideDevice (false);
    r.inst.set ("osc1.on", 1.0f);
    r.inst.set ("osc1.level", -20.0f);
    r.refresh();
    r.ui.setActiveGroup (0);
    r.refresh();
    UiStack& st = r.stack;
    const float before = r.inst.get ("osc1.level");
    r.ui.turnQ (1, 0.05, st, r.inst);
    check (r.inst.get ("osc1.level") > before, "Mixer: a hardware turn of knob 2 raises the level of A");
    r.inst.set ("osc1.on", 0.0f);
    r.refresh();
    const float off = r.inst.get ("osc1.level");
    r.ui.turnQ (1, 0.05, st, r.inst);
    check (r.inst.get ("osc1.level") == off, "Mixer: a switched-off strip takes no Q-Link turn");
}

} // namespace

int main()
{
    testRows();
    testQ();
    testWrites();
    std::printf ("%s\n", g_fails == 0 ? "ALL PASS" : "FAILURES");
    return g_fails == 0 ? 0 : 1;
}
