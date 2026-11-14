// SynthyBoy - MPC UI component library tests: the two views of an oscillator instance (OscillatorCompact, OscillatorFullScreen) for the five instances Sub, A, B, C, Noise and the three types of A, B, C.
// Every Q group of every variant is adjacent (the adjacency check passes), nothing overlaps, the areas are identical across the variants, the Basic group is the same in the compact view and in the
// full screen (members and ids), the Q frame moves to a tapped group, the outputs come with the ids the owner gave, the pictures stay inside the screen and fit one scene.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <typeinfo>

#include "ui_test.h"

using namespace uit;

namespace
{
Change tap (Component& c, int x, int y, double t)
{
    const Change d = c.touchDown (x, y, t);
    const Change u = c.touchUp (x, y);
    return u ? u : d;
}
int cx (const Component& c) { return c.rect().x + c.rect().w / 2; }
int cy (const Component& c) { return c.rect().y + c.rect().h / 2; }

void giveIds (OscillatorFullScreen& s)
{
    s.close.setId (1); s.power.setId (2); s.route.setId (8);
    s.basic.assignIds (300);
    s.tune.oct.setId (10); s.tune.sem.setId (11); s.tune.fin.setId (12); s.tune.crs.setId (13); s.tune.track.setId (14);
    for (int i = 0; i < s.bar.numSlots(); ++i)
        s.bar.slot (i)->setId (20 + i);
    Component* l[96];
    const int n = s.panel.collectLeaves (l, 0, 96);
    for (int i = 0; i < n; ++i)
        l[i]->setId (100 + i);
}
struct Variant { OscInstance inst; OscKind kind; const char* name; };
const Variant kVariants[] = { { OscInstance::Sub, OscKind::Wavetable, "sub" }, { OscInstance::A, OscKind::Wavetable, "A wavetable" }, { OscInstance::B, OscKind::Multisample, "B multisample" },
                              { OscInstance::C, OscKind::Granular, "C granular" }, { OscInstance::Noise, OscKind::Wavetable, "noise" } };
}

int main()
{
    const Rect area { 0, 0, 1280, 800 };
    Rect refTop[4], refDisplay, refBar, refTune, refPanel;
    for (int v = 0; v < 5; ++v)
    {
        const Variant& var = kVariants[v];
        OscillatorFullScreen s;
        s.place (area);
        s.setInstance (var.inst, var.kind);
        giveIds (s);
        const std::string nm = std::string ("full screen ") + var.name;
        Component* all[96];
        const int n = s.leaves (all, 96);
        check (n > 5 && n < 96, nm + ": " + std::to_string (n) + " leaves on the screen");
        bool inside = true, apart = true;
        for (int i = 0; i < n; ++i)
        {
            inside = inside && area.contains (all[i]->rect()) && all[i]->sizeOk();
            for (int j = i + 1; j < n; ++j)
                apart = apart && ! all[i]->rect().overlaps (all[j]->rect());
        }
        check (inside, nm + ": every leaf inside the screen and at least its minimum size");
        check (apart, nm + ": no two leaves overlap");
        check (! s.displayRect().empty() && area.contains (s.displayRect()) && ! s.displayRect().overlaps (s.bar.rect()) && ! s.displayRect().overlaps (s.panel.rect()) && ! s.displayRect().overlaps (s.tune.rect()), nm + ": the display area is reserved, and nothing else is on it");
        check (s.displayRect().w * 100 / 1264 >= 62 && s.displayRect().w * 100 / 1264 <= 66 && s.bar.rect().y == s.displayRect().bottom() + kGap && s.bar.rect().x == s.displayRect().x && s.bar.rect().w == s.displayRect().w, nm + ": the display is about 64 percent of the width, the type bar directly under it");
        check (s.tune.rect().x > s.displayRect().right() && s.panel.rect().y >= s.bar.rect().bottom() && s.close.rect().x < s.power.rect().x && s.route.rect().x > s.basic.rect().right() - 1, nm + ": the tuning row at the right, the bottom band below, the close button at the left, the route button at the right end");
        check (s.numGroups() >= 2 && s.activeGroup() == 0 && std::string (s.groupName (0)) == "Basic" && s.groups[0].active(), nm + ": the first group is Basic and active (" + std::to_string (s.numGroups()) + " groups)");
        for (int g = 0; g < s.numGroups(); ++g)
        {
            const Component* off = nullptr;
            const int bad = s.groups[g].check (all, n, &off);
            check (bad == 0 && s.groups[g].numMembers() >= 1, nm + ": group " + s.groupName (g) + ": all members adjacent (" + std::to_string (s.groups[g].numMembers()) + " members, offenders " + std::to_string (bad) + ")");
        }
        const Rect top[4] = { s.close.rect(), s.power.rect(), s.basic.rect(), s.route.rect() };
        if (v == 0)
        {
            for (int i = 0; i < 4; ++i) refTop[i] = top[i];
            refDisplay = s.displayRect(); refBar = s.bar.rect(); refTune = s.tune.rect(); refPanel = s.panel.rect();
        }
        else
        {
            bool same = true;
            for (int i = 0; i < 4; ++i) same = same && top[i] == refTop[i];
            check (same, nm + ": the top band is where it was for the sub oscillator");
            check (s.displayRect() == refDisplay && s.bar.rect() == refBar && s.tune.rect() == refTune && s.panel.rect() == refPanel, nm + ": the display, the type bar, the tuning row and the bottom band have the same rectangles in every variant");
        }
        // the Basic group of the instance
        const QGroup& bg = s.basic.group;
        if (var.inst == OscInstance::Sub)
            check (bg.member (0) == &s.basic.selector && bg.member (1) == &s.basic.oct && s.basic.selector.state.count == 6, nm + ": Basic = waveform (six pictograms), OCT, PAN, LEVEL");
        else if (var.inst == OscInstance::Noise)
            check (bg.member (0) == &s.basic.noiseType && bg.member (1) == &s.basic.filter, nm + ": Basic = noise type, FILTER, PAN, LEVEL");
        else
            check (bg.member (0) == &s.basic.selector && bg.member (1) == &s.basic.source && s.basic.selector.state.count == 3, nm + ": Basic = type (three pictograms), source, PAN, LEVEL");
        check (bg.member (2) == &s.basic.pan && bg.member (3) == &s.basic.level, nm + ": Basic: the third and fourth are PAN and LEVEL");
        // the type specific parts
        const bool noise = var.inst == OscInstance::Noise, sub = var.inst == OscInstance::Sub;
        const int slots = sub ? 1 : noise ? 1 : var.kind == OscKind::Wavetable ? 4 : var.kind == OscKind::Multisample ? 3 : 4;
        check (s.bar.numSlots() == slots, nm + ": " + std::to_string (slots) + " type bar slots");
        Component* tl[8];
        const int tuneLeaves = s.tune.collectLeaves (tl, 0, 8);
        int tuneOnScreen = 0;
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < tuneLeaves; ++k)
                tuneOnScreen += all[i] == tl[k];
        check (s.tuneShown() == ! noise && tuneOnScreen == (noise ? 0 : sub ? 1 : 5), nm + ": the tuning row: " + (noise ? "absent" : sub ? "CRS only" : "OCT, SEM, FIN, CRS and the toggle"));
        if (sub || noise)
            check (s.panel.numParts() == 0 && s.panel.rect() == refPanel, nm + ": the bottom band is empty but keeps its rectangle");
        if (var.inst == OscInstance::A || var.inst == OscInstance::B || var.inst == OscInstance::C)
        {
            if (var.kind == OscKind::Granular)
                check (s.numGroups() == 6 && std::string (s.groupName (3)) == "Grain" && std::string (s.groupName (4)) == "Playback" && std::string (s.groupName (5)) == "Random", nm + ": the groups Grain, Playback, Random");
            else
                check (s.numGroups() == 5 && std::string (s.groupName (3)) == "Unison" && std::string (s.groupName (4)) == "Warp", nm + ": the groups Unison and Warp");
            if (var.kind == OscKind::Multisample)
                check (s.panel.knob (PanelSlot::Timbre) && ! s.panel.knob (PanelSlot::WtPos) && s.groups[3].member (0) == s.panel.knob (PanelSlot::Timbre), nm + ": the Unison group starts with TIMBRE");
        }
        // the drawing
        Scene sc (1280, 800, kBackgroundRgba);
        s.draw (sc.builder());
        check (! sc.overflow(), nm + ": the whole screen fits in one scene (" + std::to_string (sc.numOps()) + " ops of " + std::to_string (gpu::kMaxOps) + ")");
        const Image im = render (sc);
        int overDisplay = 0;
        for (int y = s.displayRect().y; y < s.displayRect().bottom(); ++y)
            for (int x = s.displayRect().x; x < s.displayRect().right(); ++x)
                overDisplay += im.at (x, y) != backgroundPixel();
        check (overDisplay == 0, nm + ": nothing is drawn over the display area");
        writePng (outDir() + "/screen_" + std::to_string (v) + ".png", im, 1);
    }
    // the compact view: Basic is the same as in the full screen, members and ids
    for (int v = 0; v < 5; ++v)
    {
        const Variant& var = kVariants[v];
        OscillatorCompact c;
        c.place ({ 0, 0, 420, 700 });
        c.setInstance (var.inst, var.kind);
        c.instances.setId (50); c.power.setId (51); c.route.setId (52); c.full.setId (53);
        c.basic.assignIds (300);
        OscillatorFullScreen s;
        s.place (area);
        s.setInstance (var.inst, var.kind);
        s.basic.assignIds (300);
        const std::string nm = std::string ("compact ") + var.name;
        bool same = true;
        for (int i = 0; i < 4; ++i)
        {
            const Component* a = c.basic.group.member (i);
            const Component* b = s.basic.group.member (i);
            same = same && a != nullptr && b != nullptr && a->id() == b->id() && typeid (*a) == typeid (*b);
        }
        check (same, nm + ": the Basic group has the same four members (types) and ids as in the full screen");
        Component* all[64];
        const int n = c.collectLeaves (all, 0, 64);
        check (c.basic.group.active() && c.basic.group.check (all, n) == 0 && c.basic.group.numMembers() == 4, nm + ": the Basic group is one active frame and adjacent");
        bool inside = true, apart = true;
        for (int i = 0; i < n; ++i)
        {
            inside = inside && c.rect().contains (all[i]->rect()) && all[i]->sizeOk();
            for (int j = i + 1; j < n; ++j)
                apart = apart && ! all[i]->rect().overlaps (all[j]->rect());
        }
        check (inside && apart, nm + ": every leaf inside, at least its minimum size, no overlap");
        check (! c.displayRect().empty() && c.rect().contains (c.displayRect()) && c.displayRect().contains (c.full.rect()) && c.full.rect().right() <= c.displayRect().right() && c.full.rect().y - c.displayRect().y <= 8, nm + ": the display area is reserved, the full screen button in its top right corner");
        check (c.displayRect().bottom() <= c.basic.rect().y && c.instances.rect().bottom() <= c.power.rect().y && ! c.displayRect().overlaps (c.basic.rect()), nm + ": instance selector, header, display, Basic: top to bottom");
        Scene sc (420, 700, kBackgroundRgba);
        c.draw (sc.builder());
        check (! sc.overflow() && drawnOutside (render (sc), c.rect(), backgroundPixel()) == 0 && c.sizeOk(), nm + ": draws inside, fits one scene (" + std::to_string (sc.numOps()) + " ops)");
        writePng (outDir() + "/compact_" + std::to_string (v) + ".png", render (sc), 1);
        if (v == 1)
        {
            Change ch = tap (c, cx (c.instances), cy (c.instances), 0.0);   // a tap on the middle cell: instance B (index 2)
            check (ch.kind == ChangeKind::Index && ch.id == 50 && ch.index == 2, nm + ": a tap on the instance selector: { id 50, the new instance index }");
            ch = tap (c, cx (c.full), cy (c.full), 100.0);
            check (ch.kind == ChangeKind::Request && ch.id == 53, nm + ": full screen requested: id 53");
            ch = tap (c, cx (c.route), cy (c.route), 200.0);
            check (ch.kind == ChangeKind::Request && ch.id == 52, nm + ": the route button: id 52");
            ch = c.basic.group.turn (3, 0.25);
            check (ch.kind == ChangeKind::Value && ch.id == 307, nm + ": the Basic group's fourth Q-Link is LEVEL: id 307");
            check (c.instances.state.count == 5 && c.instances.state.index == 1 && std::string (c.name) == "A", nm + ": five instance cells Sub, A, B, C, Noise, A selected");
        }
    }
    // the Q frame moves to the tapped group
    {
        OscillatorFullScreen s;
        s.place (area);
        s.setInstance (OscInstance::A, OscKind::Wavetable);
        giveIds (s);
        Change c = tap (s, cx (s.tune.fin), cy (s.tune.fin), 0.0);
        check (s.activeGroup() == 2 && std::string (s.groupName (2)) == "Tune" && s.groups[2].active() && ! s.groups[0].active(), "full screen: a tap on FIN moves the Q frame to the group Tune");
        check (! c, "full screen: (a plain tap on a number field gives no change)");
        tap (s, cx (*s.panel.knob (PanelSlot::Detune)), cy (*s.panel.knob (PanelSlot::Detune)), 1000.0);
        check (std::string (s.groupName (s.activeGroup())) == "Unison", "full screen: a tap on DETUNE: the group Unison");
        tap (s, cx (s.panel.warp()->second.type), cy (s.panel.warp()->second.type), 2000.0);
        check (std::string (s.groupName (s.activeGroup())) == "Warp", "full screen: a tap in the warp pair: the group Warp");
        tap (s, cx (s.basic.level), cy (s.basic.level), 3000.0);
        check (s.activeGroup() == 0, "full screen: a tap on LEVEL: back to Basic");
        c = s.turnQ (3, 0.25);
        check (c.kind == ChangeKind::Value && c.id == 307 && std::fabs (c.value - 0.25) < 1e-9, "full screen: the fourth Q-Link of Basic is LEVEL: { id 307, 0.25 }");
        c = s.turnQ (0, 1);
        check (c.kind == ChangeKind::Index && c.id == 300 && c.index == 1, "full screen: the first Q-Link of Basic is the type: next pictogram");
        tap (s, 1279 - 10, 5, 4000.0);
        check (s.activeGroup() == 0, "full screen: a tap on nothing does not move the frame");
        c = tap (s, cx (s.close), cy (s.close), 5000.0);
        check (c.kind == ChangeKind::Request && c.id == 1, "full screen: close requested: { request, id 1 }");
        c = tap (s, cx (s.route), cy (s.route), 6000.0);
        check (c.kind == ChangeKind::Request && c.id == 8, "full screen: details of the route requested: id 8");
        c = tap (s, cx (s.basic.source.dropdown), cy (s.basic.source.dropdown), 7000.0);
        check (c.kind == ChangeKind::Request && c.id == 302, "full screen: the source dropdown: id 302");
        c = tap (s, cx (s.power), cy (s.power), 8000.0);
        check (c.kind == ChangeKind::Index && c.id == 2, "full screen: the on / off toggle: id 2");
        check (s.instance() == OscInstance::A && s.kind() == OscKind::Wavetable, "full screen: there is no instance selector: the instance is the owner's (setInstance)");
        s.setInstance (OscInstance::C, OscKind::Granular);
        giveIds (s);
        Component* bar1 = s.bar.slot (1);
        c = tap (s, cx (*bar1), cy (*bar1), 10200.0);
        check (c.kind == ChangeKind::Request && c.id == 21, "full screen granular: the grain window button of the type bar: details requested, id 21");
        Knob* len = s.panel.knob (PanelSlot::Length);
        len->state.add (5, "LFO 1", 0.3);
        s.touchDown (cx (*len), cy (*len), 20000.0);
        c = s.tick (20700.0);
        check (c.kind == ChangeKind::ModsRequest && c.id == len->id(), "full screen: a long press on a knob: modsRequested with the knob's id");
        s.touchUp (cx (*len), cy (*len));
        s.setActiveGroup (4);
        s.setInstance (OscInstance::Noise, OscKind::Wavetable);
        check (s.activeGroup() == 0 && s.numGroups() == 2, "full screen: switching the instance: the Q frame is back at Basic");
        // sub: the waveform selector, OCT in the top band, CRS in the right column
        s.setInstance (OscInstance::Sub, OscKind::Wavetable);
        giveIds (s);
        s.basic.oct.state.value = 0;
        c = s.turnQ (1, 1);
        check (c.kind == ChangeKind::Index && c.id == 303 && c.index == 5, "full screen sub: the second Q-Link of Basic is OCT: id 303");
        s.basic.selector.state.index = 2;
        c = tap (s, s.basic.selector.cell (4).x + 5, s.basic.selector.cell (4).y + 5, 30000.0);
        check (c.kind == ChangeKind::Index && c.id == 300 && c.index == 4, "full screen sub: a tap on the fifth waveform (square): { id 300, 4 }");
        // noise
        s.setInstance (OscInstance::Noise, OscKind::Wavetable);
        giveIds (s);
        s.basic.noiseType.state.count = 4; s.basic.noiseType.state.index = 0;
        c = s.turnQ (0, 1);
        check (c.kind == ChangeKind::Index && c.id == 305 && c.index == 1, "full screen noise: the first Q-Link of Basic is the noise type stepper: id 305");
        s.basic.filter.state.value = 0.5;
        c = s.turnQ (1, 0.1);
        check (c.kind == ChangeKind::Value && c.id == 304, "full screen noise: the second is FILTER: id 304");
    }
    // other sizes: proportional
    {
        OscillatorFullScreen s;
        s.place ({ 0, 0, 1920, 1200 });
        s.setInstance (OscInstance::A, OscKind::Granular);
        Component* all[96];
        const int n = s.leaves (all, 96);
        bool ok = true;
        for (int g = 0; g < s.numGroups(); ++g)
            ok = ok && s.groups[g].check (all, n) == 0;
        check (ok && s.close.rect().x < s.route.rect().x && Rect { 0, 0, 1920, 1200 }.contains (s.panel.rect()), "full screen: 1920 x 1200: groups adjacent, the same arrangement scaled");
        Scene sc (1920, 1200, kBackgroundRgba);
        s.draw (sc.builder());
        check (! sc.overflow() && drawnOutside (render (sc), { 0, 0, 1920, 1200 }, backgroundPixel()) == 0, "full screen: 1920 x 1200: draws (nothing outside the screen)");
        OscillatorCompact c;
        c.place ({ 0, 0, 630, 1050 });
        Scene sc2 (630, 1050, kBackgroundRgba);
        c.draw (sc2.builder());
        check (! sc2.overflow() && drawnOutside (render (sc2), c.rect(), backgroundPixel()) == 0, "compact: 630 x 1050: draws inside");
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
