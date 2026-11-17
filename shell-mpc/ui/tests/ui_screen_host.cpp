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
/** How many leaves lie under the index chips of a group (the chip of a member sits on the line above its top left corner: nothing may be there). */
int chipsCovered (const QGroup& g, Component* const* all, int n)
{
    int bad = 0;
    for (int i = 0; i < QGroup::kSlots; ++i)
    {
        const Component* m = g.member (i);
        if (m == nullptr)
            continue;
        const Rect chip { m->rect().x + 4, std::max (0, m->rect().y - 15), 18, std::min (15, m->rect().y) };
        for (int k = 0; k < n; ++k)
            bad += all[k]->rect().overlaps (chip);
    }
    return bad;
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
            if (all[i]->rect().empty())
                continue;   // not shown (the sub oscillator has no route button in the header: its routing is the ROUTING block)
            inside = inside && area.contains (all[i]->rect()) && all[i]->sizeOk();
            for (int j = i + 1; j < n; ++j)
                apart = apart && ! all[i]->rect().overlaps (all[j]->rect());
        }
        check (inside, nm + ": every leaf inside the screen and at least its minimum size");
        check (apart, nm + ": no two leaves overlap");
        // what is drawn is what is touched: a tap on the middle of every enabled leaf reaches that leaf (no other control, no dead spot under the drawing)
        {
            int missed = 0;
            std::string which;
            for (int i = 0; i < n; ++i)
            {
                if (! all[i]->enabled() || all[i]->rect().empty())
                    continue;
                const int px = all[i]->rect().x + all[i]->rect().w / 2, py = all[i]->rect().y + all[i]->rect().h / 2;
                s.touchDown (px, py, 0.0);
                if (! all[i]->capturing())
                {
                    ++missed;
                    which += " (" + std::to_string (px) + "," + std::to_string (py) + ")";
                }
                s.touchUp (px, py);
            }
            s.setActiveGroup (0);
            check (missed == 0, nm + ": a tap on the middle of each of the " + std::to_string (n) + " leaves reaches it" + which);
        }
        check (! s.displayRect().empty() && area.contains (s.displayRect()) && ! s.displayRect().overlaps (s.bar.rect()) && ! s.displayRect().overlaps (s.panel.rect()) && ! s.displayRect().overlaps (s.tune.rect()), nm + ": the display area is reserved, and nothing else is on it");
        const bool isSub = var.inst == OscInstance::Sub;
        if (var.inst == OscInstance::Noise)
        {
            const Stepper* kind = dynamic_cast<const Stepper*> (s.panel.part (PanelSlot::NoiseType));
            check (std::string (s.basic.noiseType.state.caption) == "SOUND" && kind != nullptr && std::string (kind->state.caption) == "KIND", nm + ": the two noise steppers say what they select (SOUND in the header, KIND in the block)");
        }
        if (var.inst == OscInstance::A)
        {
            const WarpPair* w = s.panel.warp();
            check (w != nullptr && std::string (w->first.amount.state.label) == "WARP 1" && std::string (w->second.amount.state.label) == "WARP 2", nm + ": the two warp knobs are named WARP 1 and WARP 2");
        }
        check (isSub ? s.displayRect().w * 100 / 1264 >= 99 : (s.displayRect().w * 100 / 1264 >= 76 && s.displayRect().w * 100 / 1264 <= 80 && (var.inst == OscInstance::Noise || s.barBlock().w == s.displayRect().w)), nm + ": the display is about 78 percent of the width (the sub oscillator's: all of it)");
        check (s.displayRect().h >= 240, nm + ": the display is at least 240 px tall (" + std::to_string (s.displayRect().h) + " px)");
        if (! isSub && var.inst != OscInstance::Noise)
            check (s.displayRect().y == s.barBlock().bottom() + kGap && s.barBlock().x == s.displayRect().x && s.barBlock().contains (s.bar.rect()), nm + ": the type bar (in its titled block) directly under the header and over the display");
        check ((isSub ? true : s.tune.rect().x > s.displayRect().right()) && s.panel.rect().y >= s.displayRect().bottom() && s.close.rect().x < s.power.rect().x && (isSub ? s.route.rect().empty() : s.route.rect().x > s.basic.rect().right() - 1), nm + ": the tuning row at the right, the bottom band below, the close button at the left, the route button at the right end");
        check (s.numGroups() >= 2 && s.activeGroup() == 0 && std::string (s.groupName (0)) == "Basic" && s.groups[0].active(), nm + ": the first group is Basic and active (" + std::to_string (s.numGroups()) + " groups)");
        for (int g = 0; g < s.numGroups(); ++g)
        {
            const Component* off = nullptr;
            const int bad = s.groups[g].check (all, n, &off);
            check (bad == 0 && s.groups[g].numMembers() >= 1, nm + ": group " + s.groupName (g) + ": all members adjacent (" + std::to_string (s.groups[g].numMembers()) + " members, offenders " + std::to_string (bad) + ")");
            check (chipsCovered (s.groups[g], all, n) == 0, nm + ": group " + s.groupName (g) + ": the index chips lie on free lanes above their members (nothing under them)");
        }
        const Rect top[4] = { s.close.rect(), s.power.rect(), s.basic.rect(), s.route.rect() };
        if (v == 0)
            check (top[0] == s.close.rect() && s.route.rect().empty(), nm + ": the sub oscillator has no route button in the header (its routing is the ROUTING block)");
        else if (v == 1)
            for (int i = 0; i < 4; ++i) refTop[i] = top[i];
        else
        {
            bool same = true;
            for (int i = 0; i < 4; ++i) same = same && top[i] == refTop[i];
            check (same, nm + ": the top band is where it was for the first oscillator");
        }
        if (v == 1)
        {
            refDisplay = s.displayRect(); refBar = s.bar.rect(); refTune = s.tune.rect(); refPanel = s.panel.rect();
        }
        else if (v > 1 && var.inst != OscInstance::Noise)
            check (s.displayRect() == refDisplay && s.bar.rect() == refBar && s.tune.rect() == refTune && s.panel.rect() == refPanel, nm + ": the display, the type bar, the tuning row and the bottom band have the same rectangles in every variant (the sub and noise oscillators have their own)");
        // the Basic group of the instance
        const QGroup& bg = s.basic.group;
        if (var.inst == OscInstance::Sub)
            check (bg.member (0) == &s.basic.selector && bg.member (1) == &s.basic.oct && s.basic.selector.state.count == 6, nm + ": Basic = waveform (six pictograms), OCT, PAN, LEVEL");
        else if (var.inst == OscInstance::Noise)
            check (bg.member (0) == &s.basic.noiseType && bg.member (1) == &s.basic.filter, nm + ": Basic = noise type, FILTER, PAN, LEVEL");
        else
            check (bg.member (0) == &s.basic.selector && bg.member (1) == &s.basic.source && s.basic.selector.state.count == 5, nm + ": Basic = type (five pictograms), source, PAN, LEVEL");
        check (bg.member (2) == &s.basic.pan && bg.member (3) == &s.basic.level, nm + ": Basic: the third and fourth are PAN and LEVEL");
        // the type specific parts
        const bool noise = var.inst == OscInstance::Noise, sub = var.inst == OscInstance::Sub;
        const int slots = sub ? 0 : noise ? 0 : var.kind == OscKind::Wavetable ? 4 : var.kind == OscKind::Multisample ? 3 : 4;
        check (s.bar.numSlots() == slots, nm + ": " + std::to_string (slots) + " type bar slots");
        Component* tl[8];
        const int tuneLeaves = s.tune.collectLeaves (tl, 0, 8);
        int tuneOnScreen = 0;
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < tuneLeaves; ++k)
                tuneOnScreen += all[i] == tl[k];
        check (sub ? ! s.tuneShown() : (s.tuneShown() && tuneOnScreen == (noise ? 3 : 6)), nm + ": the tuning row: " + (noise ? "PITCH, FINE and KEY TRACK" : sub ? "none" : "OCT, SEM, FIN, CRS, the toggle and MORE"));
        if (sub)
            check (s.panel.numParts() == 9, nm + ": the sub oscillator has its 9 parameters in its bottom panel");
        if (noise)
            check (s.panel.numParts() >= 3, nm + ": the noise panel has its parts");
        if (var.inst == OscInstance::A || var.inst == OscInstance::B || var.inst == OscInstance::C)
        {
            if (var.kind == OscKind::Granular)
                check (s.numGroups() == 6 && std::string (s.groupName (3)) == "Grain" && std::string (s.groupName (4)) == "Playback" && std::string (s.groupName (5)) == "Random", nm + ": the groups Grain, Playback, Random");
            else
                check (s.numGroups() == 6 && std::string (s.groupName (3)) == "Unison" && std::string (s.groupName (4)) == "Warp 1" && std::string (s.groupName (5)) == "Warp 2", nm + ": the groups Unison, Warp 1 and Warp 2");
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
        // every Q group has a visible outline with a title: the blocks of the panel, the type bar, the tuning column
        auto hasText = [&] (const char* want)
        {
            for (uint32_t k = 0; k < sc.data->numOps; ++k)
                if (sc.data->ops[k].op == gpu::kOpText)
                {
                    if (gpu::textString (*sc.data, sc.data->ops[k]) == want)
                        return true;
                }
            return false;
        };
        auto edgeDrawn = [&] (const Rect& r) { return r.w > 4 && im.at (r.x + r.w / 2, r.y) != backgroundPixel() && im.at (r.x, r.y + r.h / 2) != backgroundPixel() && im.at (r.right() - 1, r.y + r.h / 2) != backgroundPixel() && im.at (r.x + r.w / 2, r.bottom() - 1) != backgroundPixel(); };
        check ((sub || noise) ? true : (edgeDrawn (s.barBlock()) && hasText (var.kind == OscKind::Wavetable ? "WAVETABLE" : var.kind == OscKind::Multisample ? "MULTISAMPLE" : "GRANULAR")), nm + ": the type bar is a bordered block with a title");
        check (sub ? true : (edgeDrawn (s.tuneBlock()) && hasText ("TUNE") && s.tuneBlock().contains (s.tune.rect())), nm + ": the tuning column is a bordered block titled TUNE");
        check (edgeDrawn (s.basic.rect()) || s.groups[0].active(), nm + ": Basic has an outline (the coloured frame when active)");
        const int wantBlocks = sub ? 3 : noise ? 1 : var.kind == OscKind::Granular ? 3 : 2;
        check (s.panel.numBlocks() == wantBlocks, nm + ": the bottom band has " + std::to_string (wantBlocks) + " blocks");
        bool blocksOk = true;
        for (int b = 0; b < s.panel.numBlocks(); ++b)
        {
            blocksOk = blocksOk && edgeDrawn (s.panel.blockRect (b)) && hasText (s.panel.blockTitle (b)) && s.panel.rect().contains (s.panel.blockRect (b));
            if (b > 0)
                blocksOk = blocksOk && s.panel.blockRect (b - 1).right() < s.panel.blockRect (b).x && s.panel.blockRect (b - 1).y == s.panel.blockRect (b).y;   // side by side, with a gap
        }
        check (blocksOk, nm + ": each panel block is a bordered block with its title, side by side with a gap");
        // every group of the panel lies inside one block
        bool inBlock = true;
        for (int g = 3; g < s.numGroups(); ++g)
        {
            if (std::string (s.groupName (g)) == "Warp" || s.panel.numBlocks() == 0)
                continue;
            bool found = false;
            for (int b = 0; b < s.panel.numBlocks(); ++b)
                found = found || s.panel.blockRect (b).contains (s.groups[g].rect());
            inBlock = inBlock && found;
        }
        check (inBlock, nm + ": every Q group of the panel is inside one titled block");
        if (var.inst == OscInstance::A || var.inst == OscInstance::B || var.inst == OscInstance::C)
        {
            if (var.kind != OscKind::Granular)
            {
                const Rect u = s.panel.blockRect (0), w = s.panel.blockRect (1);
                check (std::string (s.panel.blockTitle (0)) == "UNISON" && std::string (s.panel.blockTitle (1)) == "WARP" && u.right() <= w.x && u.w == w.w, nm + ": UNISON block left, WARP block right, side by side, equal halves");
                const Component* k0 = s.panel.part (var.kind == OscKind::Wavetable ? PanelSlot::WtPos : PanelSlot::Timbre);
                const Rect r0 = k0->rect(), r1 = s.panel.part (PanelSlot::Unison)->rect(), r2 = s.panel.part (PanelSlot::Detune)->rect(), r3 = s.panel.part (PanelSlot::Blend)->rect();
                check (r0.y == r1.y && r1.y == r2.y && r2.y == r3.y && r0.h == r1.h && r1.h == r2.h && r2.h == r3.h && r1.x - r0.x == r2.x - r1.x && r2.x - r1.x == r3.x - r2.x && u.contains (r0) && u.contains (r3), nm + ": the four of the UNISON block on one evenly spaced row inside the block");
                WarpPair& wp = *s.panel.warp();
                check (wp.stacked() && w.contains (wp.rect()) && wp.first.rect().bottom() <= wp.second.rect().y && wp.first.rect().x == wp.second.rect().x && wp.first.amount.rect().x < wp.first.type.rect().x && wp.second.amount.rect().x < wp.second.type.rect().x, nm + ": the two warp slots stacked inside the WARP block, knobs on the same side");
            }
            else
                check (std::string (s.panel.blockTitle (0)) == "GRAIN" && std::string (s.panel.blockTitle (1)) == "PLAYBACK" && std::string (s.panel.blockTitle (2)) == "RANDOM", nm + ": the blocks GRAIN, PLAYBACK, RANDOM");
        }
        if (tuneLeaves > 0 && ! sub && ! noise)
            check (std::string (s.tune.track.state.label) == "TRACK" && hasText ("TRACK") && s.tune.track.sizeOk(), nm + ": the pitch tracking toggle has the label TRACK");
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
        check (chipsCovered (c.basic.group, all, n) == 0, nm + ": the index chips of the Basic group lie on free lanes above their members");
        bool inside = true, apart = true;
        const bool subHeader = var.inst == OscInstance::Sub;
        for (int i = 0; i < n; ++i)
        {
            if (all[i]->rect().empty() && all[i] == &c.route && subHeader)
                continue;   // not shown: the sub oscillator has no route button in the header (its routing is the ROUTING block of its page), the same on the Play page as on the page
            inside = inside && c.rect().contains (all[i]->rect()) && all[i]->sizeOk();
            for (int j = i + 1; j < n; ++j)
                apart = apart && ! all[i]->rect().overlaps (all[j]->rect());
        }
        check (inside && apart, nm + ": every leaf inside, at least its minimum size, no overlap");
        check (c.route.rect().empty() == subHeader, nm + ": the route button is in the header of every instance but the sub oscillator");
        check (! c.displayRect().empty() && c.rect().contains (c.displayRect()) && ! c.displayRect().overlaps (c.full.rect()) && c.full.rect().bottom() <= c.displayRect().y && (subHeader || c.full.rect().right() <= c.route.rect().x) && c.full.rect().right() <= c.rect().right() && ! c.full.rect().overlaps (c.power.rect()), nm + ": the display area is reserved, the full screen button stands in the header row (it does not cover the curve)");
        check (c.displayRect().bottom() <= c.basic.rect().y && c.instances.rect().bottom() <= c.power.rect().y && ! c.displayRect().overlaps (c.basic.rect()), nm + ": instance selector, header, display, Basic: top to bottom");
        Scene sc (420, 700, kBackgroundRgba);
        c.draw (sc.builder());
        {
            const Image img = render (sc);
            const uint32_t panelPx = ov::sceneColour (colour::rgba (colour::panel(), 255));
            int on = 0;
            for (int y = c.full.rect().y; y < c.full.rect().bottom(); ++y)
                for (int x = c.full.rect().x; x < c.full.rect().right(); ++x)
                    on += img.at (x, y) != panelPx && img.at (x, y) != backgroundPixel();
            check (! c.displayRect().overlaps (c.full.rect()) && on > 60 && colourNear (img, static_cast<float> (cx (c.full) - 10), static_cast<float> (cy (c.full) - 10), 2, colour::accent(), 20) && colourNear (img, static_cast<float> (cx (c.full) + 10), static_cast<float> (cy (c.full) + 10), 2, colour::accent(), 20), nm + ": the full screen button is drawn in the header row, clear of the display (" + std::to_string (on) + " button pixels in its rectangle: frame and the expand corners)");
        }
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
        check (std::string (s.groupName (s.activeGroup())) == "Warp 2", "full screen: a tap in the second warp slot: the group Warp 2");
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
        s.setActiveGroup (2);
        s.setInstance (OscInstance::Noise, OscKind::Wavetable);
        check (s.activeGroup() == 0 && s.numGroups() >= 3, "full screen: switching the instance: the Q frame is back at Basic");
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
    // ---- LED switches (frameless: a dot and the name, the state is the dot): the toggles of the sub and the noise page, the TRACK of the tune row; named by the function only, at least 56 px, and on the baseline of the controls of their row
    {
        auto cy = [] (const Component& c) { return c.rect().y + c.rect().h / 2; };
        auto isLed = [] (const Component* c, const char* name)
        {
            const LedToggle* t = dynamic_cast<const LedToggle*> (c);
            return t != nullptr && std::strcmp (t->state.label, name) == 0 && t->rect().h >= 56 && t->rect().w >= LedToggle::widthFor (name);
        };
        OscillatorFullScreen sub;
        sub.place (area);
        sub.setInstance (OscInstance::Sub, OscKind::Wavetable);
        check (isLed (sub.panel.part (PanelSlot::SubEnv1), "ENV 1") && isLed (sub.panel.part (PanelSlot::SubMono), "MONO") && isLed (sub.panel.part (PanelSlot::SubContig), "FREE RUN"), "sub: ENV 1, MONO and FREE RUN are LED toggles named by the function only (no ON / OFF in the label), 56 px high");
        const Component *b1 = sub.panel.part (PanelSlot::SubBus1), *b2 = sub.panel.part (PanelSlot::SubBus2);
        check (b1 != nullptr && b2 != nullptr && std::abs (cy (*sub.panel.part (PanelSlot::SubEnv1)) - cy (*b1)) <= 2 && std::abs (cy (*sub.panel.part (PanelSlot::SubMono)) - cy (*b1)) <= 2 && std::abs (cy (*sub.panel.part (PanelSlot::SubContig)) - cy (*b2)) <= 2,
               "sub: the switches of the TUNE and PHASE blocks stand on the baseline of the BUS knobs of the ROUTING block");
        OscillatorFullScreen noise;
        noise.place (area);
        noise.setInstance (OscInstance::Noise, OscKind::Wavetable);
        check (isLed (noise.panel.part (PanelSlot::NoiseEnv1), "ENV 1") && isLed (&noise.tune.track, "KEY TRACK"), "noise: ENV 1 and KEY TRACK are LED toggles");
        check (noise.panel.part (PanelSlot::NoiseType) != nullptr && noise.panel.part (PanelSlot::NoiseStereo) != nullptr &&
               std::abs (cy (*noise.panel.part (PanelSlot::NoiseType)) - cy (*noise.panel.part (PanelSlot::NoiseStereo))) <= 2 &&
               cy (*noise.panel.part (PanelSlot::NoiseEnv1)) > cy (*noise.panel.part (PanelSlot::NoiseType)),
               "noise: KIND and STEREO share baseline in row 0, ENV 1 is in row 1");
        OscillatorFullScreen wt;
        wt.place (area);
        wt.setInstance (OscInstance::A, OscKind::Wavetable);
        check (isLed (&wt.tune.track, "TRACK"), "oscillator A, B, C: TRACK is a LED toggle");
        // the five source tabs of the Play page: every cell the same width
        OscillatorCompact c;
        c.place ({ 0, 0, 420, 700 });
        int w0 = c.instances.cell (0).w;
        bool same = true;
        for (int i = 1; i < 5; ++i)
            same = same && std::abs (c.instances.cell (i).w - w0) <= 1;
        check (same, "the five source tabs SUB A B C NOISE have the same width");
    }
    // ---- what is drawn is what is touched, for the compact view of every instance and every detail page: a tap on the middle of each enabled leaf reaches it
    {
        auto everyLeaf = [&] (Component& page, const std::string& nm)
        {
            Component* l[128];
            const int nl = page.collectLeaves (l, 0, 128);
            int missed = 0;
            std::string which;
            for (int i = 0; i < nl; ++i)
            {
                if (! l[i]->enabled() || l[i]->rect().empty())
                    continue;
                const int px = l[i]->rect().x + l[i]->rect().w / 2, py = l[i]->rect().y + l[i]->rect().h / 2;
                page.touchDown (px, py, 0.0);
                if (! l[i]->capturing())
                {
                    ++missed;
                    which += " (" + std::to_string (px) + "," + std::to_string (py) + ")";
                }
                page.touchUp (px, py);
            }
            check (nl > 3 && missed == 0, nm + ": a tap on the middle of each of the " + std::to_string (nl) + " leaves reaches it" + which);
        };
        const OscInstance insts[5] = { OscInstance::Sub, OscInstance::A, OscInstance::B, OscInstance::C, OscInstance::Noise };
        for (OscInstance i : insts)
        {
            static OscillatorCompact oc;
            oc.place ({ 8, 114, 420, 620 });
            oc.setInstance (i, OscKind::Wavetable);
            everyLeaf (oc, "compact view");
        }
        const PanelPreset* pres[5] = { &presets::kUnisonDetail, &presets::kUnisonDetailMultisample, &presets::kUnisonDetailGranular, &presets::kGrainDetail, &presets::kEnvelopeDetail };
        for (const PanelPreset* pr : pres)
        {
            static DetailPage pg;
            pg.load (*pr, "DETAIL");
            pg.place ({ 0, 110, 1280, 628 });
            everyLeaf (pg, std::string ("detail page ") + pr->name);
        }
        {
            ModInput m;
            m.add (7, "LFO 2", 0.35);
            m.add (8, "ENV 1", -0.5, true);
            static ModulationsPage mp;
            mp.load (m, "DETUNE");
            mp.place ({ 0, 110, 1280, 628 });
            everyLeaf (mp, "modulations page");
        }
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
