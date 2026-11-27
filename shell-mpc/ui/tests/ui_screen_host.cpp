// SynthyBoy - MPC UI component library tests: the two views of an oscillator instance (OscillatorCompact, OscillatorFullScreen) for the five instances Sub, A, B, C, Noise and the three types of A, B, C.
// Every Q group of every variant is adjacent (the adjacency check passes), nothing overlaps, the areas are identical across the variants, the Basic group is the same in the compact view and in the
// full screen (members and ids), the Q frame moves to a tapped group, the outputs come with the ids the owner gave, the pictures stay inside the screen and fit one scene.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <typeinfo>

#include "ui_test.h"
#include "../env_screen.h"
#include "../lfo_display.h"
#include "../lfo_screen.h"
#include "../play_rows.h"

using namespace uit;

namespace
{
Change tap (Component& c, int x, int y, double t)
{
    const Change d = c.touchDown (x, y, t);
    const Change u = c.touchUp (x, y);
    return u ? u : d;
}
int cx (const ControlModel& c) { return c.rect().x + c.rect().w / 2; }
int cy (const ControlModel& c) { return c.rect().y + c.rect().h / 2; }

void giveIds (OscillatorFullScreen& s)
{
    s.close.setId (1); s.power.setId (2); s.more.setId (9);
    s.top.assignIds (300);
    s.top.pos.setId (310); s.top.crs.setId (311); s.top.phase.setId (312); s.top.stereo.setId (313); s.top.kind.setId (314);
    ControlModel* l[96];
    const int n = s.panel.collectModelLeaves (l, 0, 96);
    for (int i = 0; i < n; ++i)
        l[i]->setId (100 + i);
}

/** How many leaves lie under the index chips of a group (the chip of a member sits on the line above its top left corner: nothing may be there). */
int chipsCovered (const QGroup& g, ControlModel* const* all, int n)
{
    int bad = 0;
    for (int i = 0; i < QGroup::kSlots; ++i)
    {
        const ControlModel* m = g.member (i);
        if (m == nullptr)
            continue;
        const Rect chip { m->rect().x + 4, std::max (0, m->rect().y - 15), 18, std::min (15, m->rect().y) };
        for (int k = 0; k < n; ++k)
            bad += all[k]->rect().overlaps (chip);
    }
    return bad;
}
struct Variant { OscInstance inst; OscKind kind; bool noiseSample; const char* name; int controls; int groups; };
// the controls of the default view (the bold cut: docs/ui-map/osc-play-bold-cut/README.md): POWER + the top row + the bottom band; the Q groups
const Variant kVariants[] = { { OscInstance::Sub, OscKind::Wavetable, false, "sub", 8, 2 }, { OscInstance::A, OscKind::Wavetable, false, "A wavetable", 12, 3 }, { OscInstance::B, OscKind::Multisample, false, "B multisample", 10, 3 },
                              { OscInstance::C, OscKind::Granular, false, "C granular", 12, 3 }, { OscInstance::Noise, OscKind::Wavetable, false, "noise generated", 9, 2 }, { OscInstance::Noise, OscKind::Wavetable, true, "noise sample", 12, 3 },
                              { OscInstance::A, OscKind::Sample, false, "A sample", 10, 3 }, { OscInstance::A, OscKind::Spectral, false, "A spectral", 9, 3 } };
/** The controls of the default view as the user counts them: POWER, the live members of the top row, every part of the bottom band that is shown (the unison box is ONE, a warp slot is its type and, while it applies, its amount). MORE is not counted. */
int countControls (OscillatorFullScreen& s)
{
    int n = 1 + s.top.numRow();
    const PanelPreset* pp = s.panel.preset();
    for (int i = 0; pp != nullptr && i < pp->numItems; ++i)
    {
        const PanelItem& it = pp->items[i];
        if (it.kind == PartKind::WarpPair)
        {
            const WarpPair& w = *s.panel.warp();
            n += 1 + (s.warpApplies (0) ? 1 : 0);
            if (! w.single())
                n += 1 + (s.warpApplies (1) ? 1 : 0);
        }
        else if (s.panel.shown (it.slot))
            ++n;
    }
    return n;
}
}


int main()
{
    const Rect area { 0, 0, 1280, 800 };
    Rect refTop[4];
    for (int v = 0; v < 8; ++v)
    {
        const Variant& var = kVariants[v];
        OscillatorFullScreen s;
        s.place (area);
        s.setInstance (var.inst, var.kind, var.noiseSample);
        giveIds (s);
        const std::string nm = std::string ("full screen ") + var.name;
        ControlModel* all[96];
        const int n = s.collectModelLeaves (all, 0, 96);
        check (n > 5 && n < 96, nm + ": " + std::to_string (n) + " leaves on the screen");
        bool inside = true, apart = true;
        for (int i = 0; i < n; ++i)
        {
            if (all[i]->rect().empty())
                continue;
            inside = inside && area.contains (all[i]->rect()) && all[i]->sizeOk();
            for (int j = i + 1; j < n; ++j)
                apart = apart && ! all[i]->rect().overlaps (all[j]->rect());
        }
        if (! inside)
            for (int i = 0; i < n; ++i)
                if (! all[i]->rect().empty() && ! (area.contains (all[i]->rect()) && all[i]->sizeOk()))
                    std::printf ("  (%s: leaf %d at %d,%d %dx%d, minimum %dx%d)\n", nm.c_str(), i, all[i]->rect().x, all[i]->rect().y, all[i]->rect().w, all[i]->rect().h, all[i]->minSize().w, all[i]->minSize().h);
        check (inside, nm + ": every leaf inside the screen and at least its minimum size");
        check (apart, nm + ": no two leaves overlap");
        // what is overlay-drawn is what is overlay-touched: a tap on the middle of every enabled widget leaf reaches that leaf
        {
            int missed = 0;
            std::string which;
            for (int i = 0; i < n; ++i)
            {
                Component* w = dynamic_cast<Component*> (all[i]);
                if (! w || ! w->enabled() || w->rect().empty() || w == &s.mix)
                    continue;   // (pure models and the indicator take no overlay touch)
                const int px = w->rect().x + w->rect().w / 2, py = w->rect().y + w->rect().h / 2;
                s.touchDown (px, py, 0.0);
                if (! w->capturing())
                {
                    ++missed;
                    which += " (" + std::to_string (px) + "," + std::to_string (py) + ")";
                }
                s.touchUp (px, py);
            }
            s.setActiveGroup (0);
            check (missed == 0, nm + ": a tap on the middle of each widget leaf reaches it" + which);
        }
        check (! s.displayRect().empty() && area.contains (s.displayRect()) && ! s.displayRect().overlaps (s.top.rect()) && ! s.displayRect().overlaps (s.panel.rect()) && ! s.displayRect().overlaps (s.more.rect()), nm + ": the display area is reserved, and nothing else is on it");
        check (s.displayRect().w * 100 / 1264 >= 99 && s.displayRect().h >= 240, nm + ": the display has the whole width and is at least 240 px tall (" + std::to_string (s.displayRect().h) + " px)");
        check (countControls (s) == var.controls, nm + ": the default view has " + std::to_string (var.controls) + " controls (counted " + std::to_string (countControls (s)) + ")");
        check (s.panel.rect().y >= s.displayRect().bottom() && s.close.rect().x < s.power.rect().x && s.mix.rect().x > s.top.rect().right() - 1 && s.more.rect().x >= s.panel.rect().right(), nm + ": the bottom band below the display, the close button at the left, the read-only mix indicator at the right end of the top band, MORE right of the bottom band");
        check (s.mix.touchTargets (nullptr, 0) == 0 && s.mix.rect().w >= 120, nm + ": the level / pan / destination indicator has no touch target (read-only)");
        check (s.numGroups() == var.groups && s.activeGroup() == 0 && std::string (s.groupName (0)) == "Basic" && s.groups[0].active(), nm + ": " + std::to_string (var.groups) + " groups, the first is Basic and active (counted " + std::to_string (s.numGroups()) + ")");
        for (int g = 0; g < s.numGroups(); ++g)
        {
            const ControlModel* off = nullptr;
            const int bad = s.groups[g].check (all, n, &off);
            check (bad == 0 && s.groups[g].numMembers() >= 1 && s.groups[g].numMembers() <= 4, nm + ": group " + s.groupName (g) + ": all members adjacent (" + std::to_string (s.groups[g].numMembers()) + " members, offenders " + std::to_string (bad) + ")");
            check (chipsCovered (s.groups[g], all, n) == 0, nm + ": group " + s.groupName (g) + ": the index chips lie on free lanes above their members (nothing under them)");
            // a member is shown: a hidden control is no member
            bool live = true;
            for (int k = 0; k < 4; ++k)
                if (const ControlModel* m = s.groups[g].member (k))
                {
                    bool found = false;
                    for (int i = 0; i < n; ++i) found = found || all[i] == m || all[i] == m->primaryModel();
                    live = live && (found || m->primaryModel() != nullptr);
                }
            check (live, nm + ": group " + s.groupName (g) + ": every member is on the screen (nothing hidden is a member)");
        }
        // the top row and the first group, by instance
        const QGroup& bg = s.groups[0];
        if (var.inst == OscInstance::Sub)
            check (bg.member (0) == &s.top.selector && bg.member (1) == &s.top.oct && bg.member (2) == &s.top.crs && bg.member (3) == &s.top.phase && s.top.selector.state.count == 6, nm + ": Basic = waveform (six pictograms), OCT, CRS, PHASE");
        else if (var.inst == OscInstance::Noise)
            check (bg.member (0) == &s.top.kind && (var.noiseSample ? (bg.member (1) == &s.top.filter && bg.member (2) == &s.top.stereo && bg.member (3) == nullptr) : (bg.member (1) == &s.top.sound && bg.member (2) == &s.top.filter && bg.member (3) == &s.top.stereo)), nm + ": Basic = KIND, " + (var.noiseSample ? "FILTER, STEREO (SOUND does not apply to a sample: hidden, the group closes up)" : "SOUND, FILTER, STEREO"));
        else
            check (bg.member (0) == &s.top.selector && bg.member (1) == &s.top.source && bg.member (2) == &s.top.pos && bg.member (3) == &s.top.oct && s.top.selector.state.count == 5, nm + ": Basic = type (five pictograms), source, the position knob, OCT");
        // level, pan and destination are not controls of this view (the Mixer is their home)
        {
            bool noPanLevel = true;
            for (int i = 0; i < n; ++i)
            {
                const Knob* k = dynamic_cast<const Knob*> (all[i]);
                noPanLevel = noPanLevel && (k == nullptr || (std::string (k->state.label) != "PAN" && std::string (k->state.label) != "LEVEL"));
            }
            check (noPanLevel, nm + ": no PAN and no LEVEL knob (read-only indicator only)");
        }
        // the top band is where it was for the first oscillator
        const Rect top[4] = { s.close.rect(), s.power.rect(), s.top.rect(), s.mix.rect() };
        if (v == 1)
            for (int i = 0; i < 4; ++i) refTop[i] = top[i];
        else if (v > 1)
        {
            bool same = true;
            for (int i = 0; i < 4; ++i) same = same && top[i] == refTop[i];
            check (same, nm + ": the top band is where it was for the first oscillator");
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
        auto hasText = [&] (const char* want)
        {
            for (uint32_t k = 0; k < sc.data->numOps; ++k)
                if (sc.data->ops[k].op == gpu::kOpText && gpu::textString (*sc.data, sc.data->ops[k]) == want)
                    return true;
            return false;
        };
        auto edgeDrawn = [&] (const Rect& r) { return r.w > 4 && im.at (r.x + r.w / 2, r.y) != backgroundPixel() && im.at (r.x, r.y + r.h / 2) != backgroundPixel() && im.at (r.right() - 1, r.y + r.h / 2) != backgroundPixel() && im.at (r.x + r.w / 2, r.bottom() - 1) != backgroundPixel(); };
        check (edgeDrawn (s.top.rect()) || s.groups[0].active(), nm + ": Basic has an outline (the coloured frame when active)");
        bool blocksOk = s.panel.numBlocks() >= 1;
        for (int b = 0; b < s.panel.numBlocks(); ++b)
        {
            blocksOk = blocksOk && edgeDrawn (s.panel.blockRect (b)) && hasText (s.panel.blockTitle (b)) && s.panel.rect().contains (s.panel.blockRect (b));
            if (b > 0)
                blocksOk = blocksOk && s.panel.blockRect (b - 1).right() < s.panel.blockRect (b).x && s.panel.blockRect (b - 1).y == s.panel.blockRect (b).y;   // side by side, with a gap
        }
        check (blocksOk, nm + ": each panel block is a bordered block with its title, side by side with a gap");
        check (hasText ("MORE") && s.more.sizeOk(), nm + ": the ONE MORE button is drawn");
        // every group of the panel lies inside one block (the warp group is the second slot pair of the WARP block)
        bool inBlock = true;
        for (int g = 1; g < s.numGroups(); ++g)
        {
            bool found = false;
            for (int b = 0; b < s.panel.numBlocks(); ++b)
                found = found || s.panel.blockRect (b).contains (s.groups[g].rect());
            inBlock = inBlock && found;
        }
        check (inBlock, nm + ": every Q group of the panel is inside one titled block");
        writePng (outDir() + "/screen_" + std::to_string (v) + ".png", im, 1);
    }
    // the default view per type: the blocks and the members of the groups
    {
        auto make = [&] (OscInstance i, OscKind k, bool ns, OscillatorFullScreen& s) { s.place (area); s.setInstance (i, k, ns); giveIds (s); };
        OscillatorFullScreen wt;
        make (OscInstance::A, OscKind::Wavetable, false, wt);
        const WarpPair* w = wt.panel.warp();
        check (w != nullptr && ! w->single() && std::string (w->first.amount.state.label) == "WARP 1" && std::string (w->second.amount.state.label) == "WARP 2" && ! w->first.hasVar() , "wavetable: the two warp knobs are named WARP 1 and WARP 2 (the PHASE knobs are behind MORE)");
        check (std::string (wt.groupName (1)) == "Unison" && wt.groups[1].member (0) == wt.panel.part (PanelSlot::Unison) && wt.groups[1].member (1) == wt.panel.part (PanelSlot::Detune) && wt.groups[1].member (2) == wt.panel.part (PanelSlot::Fin) && wt.groups[1].member (3) == nullptr, "wavetable: Unison = [UNISON, DETUNE, FIN]");
        check (std::string (wt.groupName (2)) == "Warp" && wt.groups[2].member (0) == &w->first.type && wt.groups[2].member (1) == &w->first.amount && wt.groups[2].member (2) == &w->second.type && wt.groups[2].member (3) == &w->second.amount, "wavetable: Warp = [WARP 1 TYPE, WARP 1 AMOUNT, WARP 2 TYPE, WARP 2 AMOUNT] (the Q-Link warp group of the Q mapping is kept)");
        check (wt.panel.part (PanelSlot::Blend) == nullptr && std::string (wt.panel.blockTitle (0)) == "UNISON" && std::string (wt.panel.blockTitle (1)) == "WARP" && wt.panel.blockRect (0).right() <= wt.panel.blockRect (1).x && wt.panel.blockRect (0).w == wt.panel.blockRect (1).w, "wavetable: BLEND is behind MORE; UNISON block left, WARP block right, equal halves");
        check (w->stacked() && wt.panel.blockRect (1).contains (w->rect()) && w->first.rect().bottom() <= w->second.rect().y && w->first.rect().x == w->second.rect().x, "wavetable: the two warp slots stacked inside the WARP block");
        // hide what does not apply: a warp that is Off has no amount
        wt.setWarpApplies (0, false);
        check (! wt.warpApplies (0) && wt.groups[2].member (0) == &w->first.type && wt.groups[2].member (1) == &w->second.type && wt.groups[2].member (2) == &w->second.amount && wt.groups[2].member (3) == nullptr, "wavetable: WARP 1 Off: its amount is hidden and leaves the Q group (the group closes up)");
        ControlModel* leaves[96];
        int nl = wt.collectModelLeaves (leaves, 0, 96);
        bool amountListed = false;
        for (int i = 0; i < nl; ++i) amountListed = amountListed || leaves[i] == &w->first.amount;
        check (! amountListed && countControls (wt) == 11, "wavetable: the hidden amount is not a leaf of the screen (not dimmed: not there), 11 controls");
        wt.setWarpApplies (0, true);
        wt.setWarpApplies (1, false);
        check (wt.groups[2].member (1) == &w->first.amount && wt.groups[2].member (2) == &w->second.type && wt.groups[2].member (3) == nullptr, "wavetable: WARP 2 Off: [WARP 1 TYPE, WARP 1 AMOUNT, WARP 2 TYPE]");
        wt.setWarpApplies (0, false);
        check (wt.groups[2].member (0) == &w->first.type && wt.groups[2].member (1) == &w->second.type && wt.groups[2].member (2) == nullptr, "wavetable: both warps Off: only the two types are members");
        // the other types
        for (OscKind k : { OscKind::Multisample, OscKind::Sample, OscKind::Spectral })
        {
            OscillatorFullScreen o;
            make (OscInstance::A, k, false, o);
            const WarpPair* ow = o.panel.warp();
            check (ow != nullptr && ow->single() && o.numGroups() == 3 && std::string (o.groupName (2)) == "Warp" && o.groups[2].member (0) == &ow->first.type && o.groups[2].member (1) == &ow->first.amount && o.groups[2].member (2) == nullptr, "warp 1 only in the default view of multisample, sample and spectral (warp 2 is behind MORE)");
            check ((k == OscKind::Spectral) == (o.panel.part (PanelSlot::Fin) == nullptr) && o.top.pos.state.label[0] != '\0' && std::string (o.top.pos.state.label) == (k == OscKind::Multisample ? "TIMBRE" : k == OscKind::Sample ? "SCAN" : "POSITION"), "the position knob is TIMBRE / SCAN / POSITION; FIN is in the default view of all but the spectral type");
        }
        OscillatorFullScreen g;
        make (OscInstance::C, OscKind::Granular, false, g);
        check (g.panel.warp() == nullptr && std::string (g.groupName (1)) == "Grain" && g.groups[1].member (0) == g.panel.part (PanelSlot::Dens) && g.groups[1].member (1) == g.panel.part (PanelSlot::Length) && g.groups[1].member (2) == g.panel.part (PanelSlot::WarpMenu)
                   && std::string (g.groupName (2)) == "Playback" && g.groups[2].member (0) == g.panel.part (PanelSlot::Offset) && g.groups[2].member (1) == g.panel.part (PanelSlot::Dir) && g.groups[2].member (2) == g.panel.part (PanelSlot::Pitch) && g.groups[2].member (3) == g.panel.part (PanelSlot::Fin),
               "granular: Basic [TYPE, SOURCE, SCAN, OCT], Grain [DENS, LENGTH, WARP], Playback [OFFSET, DIR, PITCH, FIN]");
        OscillatorFullScreen sub;
        make (OscInstance::Sub, OscKind::Wavetable, false, sub);
        check (std::string (sub.groupName (1)) == "Behaviour" && sub.groups[1].member (0) == sub.panel.part (PanelSlot::SubEnv1) && sub.groups[1].member (1) == sub.panel.part (PanelSlot::SubMono) && sub.groups[1].member (2) == sub.panel.part (PanelSlot::SubContig) && sub.panel.numParts() == 3 && sub.panel.part (PanelSlot::SubBus1) == nullptr && sub.panel.part (PanelSlot::SubDest) == nullptr,
               "sub: Behaviour [ENV 1, MONO, FREE RUN]; the destination, the sends and the balance are not in the default view");
        // the noise: BOUNCE only while LOOP is on
        OscillatorFullScreen ns;
        make (OscInstance::Noise, OscKind::Wavetable, true, ns);
        check (ns.panel.shown (PanelSlot::NoiseBounce) && std::string (ns.groupName (2)) == "Playback" && ns.groups[2].member (3) == ns.panel.part (PanelSlot::NoiseBounce), "noise sample: PLAYBACK [START, RANDOM, LOOP, BOUNCE]");
        ns.setPartShown (PanelSlot::NoiseBounce, false);
        check (! ns.panel.shown (PanelSlot::NoiseBounce) && ns.groups[2].member (2) == ns.panel.part (PanelSlot::NoiseLoop) && ns.groups[2].member (3) == nullptr && countControls (ns) == 11, "noise sample: explicitly hiding BOUNCE removes it and the group closes up: 11 controls");
        OscillatorFullScreen ng;
        make (OscInstance::Noise, OscKind::Wavetable, false, ng);
        check (ng.panel.part (PanelSlot::NoiseStart) == nullptr && ng.panel.part (PanelSlot::NoiseLoop) == nullptr && ng.panel.part (PanelSlot::NoiseBounce) == nullptr && ng.panel.part (PanelSlot::NoiseRand) == nullptr, "noise generated: START, RANDOM, LOOP, BOUNCE are not there (sample only)");
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
        s.top.assignIds (300);
        const std::string nm = std::string ("compact ") + var.name;
        // the compact view keeps PAN and LEVEL (the intentional shortcuts of the Play page); the full screen's first group has the position knob and OCT instead (level, pan and destination are the Mixer's): the first member (the type) is the same
        const ControlModel* a0 = c.basic.group.member (0);
        const ControlModel* b0 = s.groups[0].member (0);
        check (a0 != nullptr && b0 != nullptr && (var.inst == OscInstance::Noise || (a0->id() == b0->id() && typeid (*a0) == typeid (*b0))) && c.basic.group.member (2) == &c.basic.pan && c.basic.group.member (3) == &c.basic.level, nm + ": the compact Basic group is [type, source, PAN, LEVEL]; its type is the full screen's first member (same id and kind)");
        ControlModel* all[64];
        const int n = c.collectModelLeaves (all, 0, 64);
        check (c.basic.group.active() && c.basic.group.check (all, n) == 0 && c.basic.group.numMembers() == 4, nm + ": the Basic group is one active frame and adjacent");
        check (chipsCovered (c.basic.group, all, n) == 0, nm + ": the index chips of the Basic group lie on free lanes above their members");
        bool inside = true, apart = true;
        std::string why;
        const bool subHeader = var.inst == OscInstance::Sub;
        for (int i = 0; i < n; ++i)
        {
            if (all[i]->rect().empty() && all[i] == &c.route && subHeader)
                continue;   // not shown: the sub oscillator has no route button in the header (its routing is the ROUTING block of its page), the same on the Play page as on the page
            const bool ok1 = c.rect().contains (all[i]->rect()) && all[i]->sizeOk();
            if (! ok1) why += " [leaf " + std::to_string (i) + " " + std::to_string (all[i]->rect().x) + "," + std::to_string (all[i]->rect().y) + " " + std::to_string (all[i]->rect().w) + "x" + std::to_string (all[i]->rect().h) + " min " + std::to_string (all[i]->minSize().w) + "x" + std::to_string (all[i]->minSize().h) + "]";
            inside = inside && ok1;
            for (int j = i + 1; j < n; ++j)
            {
                const bool ov = all[i]->rect().overlaps (all[j]->rect());
                if (ov) why += " [leaves " + std::to_string (i) + " and " + std::to_string (j) + " overlap]";
                apart = apart && ! ov;
            }
        }
        check (inside && apart, nm + ": every leaf inside, at least its minimum size, no overlap" + why);
        check (c.route.rect().empty() == subHeader, nm + ": the route button is in the header of every instance but the sub oscillator");
        {   // the buttons of the display stand in its header band (playrows.h): the power button at the left, the route button and the full screen button at the right end; the full screen button exactly where the envelope graph's and the LFO display's is
            const Rect d = c.displayRect(), f = c.full.rect(), w = c.power.rect(), band = playrows::bandRect (d);
            EnvCompact ec;
            ec.place ({ 440, 114, 400, 620 });
            const Rect ef = ec.full.rect(), eg = ec.graphRect();
            check (! d.empty() && c.rect().contains (d) && band.contains (f) && band.contains (w) && f == playrows::fullRect (d) && w == playrows::powerRect (d) && f.right() == d.right() - 4 && f.w == ef.w && f.h == ef.h && ef == playrows::fullRect (eg) && ! f.overlaps (w) && w.w >= 56 && w.h >= 56 && f.h >= 56,
                   nm + ": the full screen button (right end) and the power button (left end) stand in the header band of the display, the full screen button like the envelope graph's");
        }
        {
            const Rect hdr = c.instances.rect();
            const bool row = c.instances.rect().y == c.rect().y + playrows::kMargin && c.instances.rect().right() == c.rect().right() - playrows::kMargin;
            check (hdr.h == playrows::kSelectorH && row && (subHeader || (c.route.rect().h >= 56 && c.route.rect() == playrows::routeRect (c.displayRect()) && playrows::bandRect (c.displayRect()).contains (c.route.rect()) && c.route.rect().right() <= c.full.rect().x && ! c.route.rect().overlaps (c.full.rect()) && ! c.route.rect().overlaps (c.power.rect()))) && c.power.state.negativeOff, nm + ": the top row holds the source selector only (the route button is in the header band of the display, left of the full screen button), 56 px, like the other two thirds; the power button is red while off");
            check (c.plotRect() == playrows::plotRect (c.displayRect()) && c.plotRect().y >= playrows::bandRect (c.displayRect()).bottom() && c.displayRect().contains (c.plotRect()) && (subHeader ? c.labelRect().right() <= c.full.rect().x : c.labelRect().right() <= c.route.rect().x) && c.labelRect().x >= c.power.rect().right(), nm + ": the plot area is under the band and the label's room lies between the power button and the route / full screen button");
            check (c.instances.rect().bottom() <= c.displayRect().y && c.displayRect().bottom() <= c.basic.rect().y && ! c.displayRect().overlaps (c.basic.rect()) && c.basic.rect().y == playrows::controlsLine (c.rect()), nm + ": top row, display, Basic: top to bottom, Basic at the common controls line");
            check (c.displayRect().x == playrows::displayRect (c.rect()).x && c.displayRect().y == playrows::displayRect (c.rect()).y && c.displayRect().h == playrows::displayRect (c.rect()).h, nm + ": the display is the rectangle shared by the three thirds");
            check (c.instances.state.icon == MenuButton::Icon::Down && std::string (c.instances.state.label) == OscillatorCompact::sourceName (var.inst) && c.instances.rect().w >= 120, nm + ": the source selector shows the current source (" + c.instances.state.label + ") and opens a list of five");
            Rect cells[8]; const int nt = c.instances.touchTargets (cells, 8);
            bool big = nt == 1;
            for (int t = 0; t < nt; ++t)
                big = big && cells[t].h >= 56 && cells[t].w >= 56;
            check (big, nm + ": the touch targets of the source selector are at least 56 px");
            if (var.inst != OscInstance::Noise)
            {
                // two rows like the envelope third's knobs: row 1 the type drop-down and the table / OCT selector side by side (at the 1/3 width the table stepper keeps 200 px, 140 px of text, so the longest built-in
                // table name is written whole at a legible size and the two selectors are NOT stacked), row 2 PAN and LEVEL as the side knobs of the envelope third
                EnvCompact ec;
                ec.place ({ 440, 114, 400, 620 });
                OscillatorCompact co;   // the same rectangle as the envelope third: the rows are compared where the Play page puts the two thirds
                co.place ({ 0, 114, 400, 620 });
                co.setInstance (var.inst, var.kind);
                co.basic.assignIds (300);
                const ControlModel* sec = var.inst == OscInstance::Sub ? static_cast<const ControlModel*> (&co.basic.oct) : static_cast<const ControlModel*> (&co.basic.source);
                const Rect tr = co.basic.selector.rect(), sr = sec->rect();
                const bool rows = co.basic.selector.dropdown && tr.y == sr.y && tr.h == sr.h && tr.right() <= sr.x && sr.right() <= co.basic.rect().right() && tr.h >= 56 && sr.h >= 56 && tr.bottom() <= co.basic.pan.rect().y && sr.bottom() <= co.basic.level.rect().y;
                const bool knobs = co.basic.pan.state.side && co.basic.level.state.side && co.basic.pan.rect().y == ec.sustain.rect().y && co.basic.level.rect().y == ec.release.rect().y && co.basic.pan.rect().h == ec.sustain.rect().h
                                   && co.basic.pan.state.dialMax == ec.sustain.state.dialMax && co.basic.pan.rect().w >= 172 && co.basic.level.rect().w >= 172 && co.basic.pan.rect().y == co.basic.level.rect().y;
                check (rows && knobs, nm + ": row 1 = the type drop-down and the " + (var.inst == OscInstance::Sub ? "OCT field" : "table stepper") + " side by side, row 2 = PAN and LEVEL: the side knobs of the envelope third, same rows and size");
                if (var.inst != OscInstance::Sub)
                {
                    const float size = Pen::fitSize ("Harmonic Series", static_cast<float> (sr.w - 2 * 28 - 4), static_cast<float> (sr.h - 4), theme::L().fValue, theme::L().fMin);
                    check (c.basic.source.compact() && c.basic.source.dropdown.rect().empty() && sr.w >= 190 && size >= 12.0f && sdf::textWidth ("Harmonic Series", size) <= static_cast<float> (sr.w - 2 * 28 - 4) + 0.5f,
                           nm + ": the table stepper (" + std::to_string (sr.w) + " px) writes 'Harmonic Series' whole at " + std::to_string (static_cast<int> (size)) + " px");
                }
                Rect cells[4];
                check (c.basic.selector.touchTargets (cells, 4) == 1 && cells[0].h >= 56 && cells[0].w >= 56, nm + ": the type drop-down is one touch target of at least 56 px");
                const Change tc = tap (c, cx (c.basic.selector), cy (c.basic.selector), 0.0);
                check (tc.kind == ChangeKind::Request && tc.id == c.basic.selector.id(), nm + ": a tap on the type drop-down is a Request (the owner opens the list of types)");
                const int before = c.basic.selector.state.index, countN = c.basic.selector.state.count;
                const Change turned = c.basic.selector.turn (before + 1 < countN ? 1.0 : -1.0);
                check (turned.kind == ChangeKind::Index && turned.id == c.basic.selector.id() && turned.index == (before + 1 < countN ? before + 1 : before - 1), nm + ": a Q turn steps the type through the list as the cells did");
            }
        }
        Scene sc (420, 700, kBackgroundRgba);
        c.draw (sc.builder());
        {
            const Image img = render (sc);
            const uint32_t panelPx = ov::sceneColour (colour::rgba (colour::panel(), 255));
            int on = 0;
            for (int y = c.full.rect().y; y < c.full.rect().bottom(); ++y)
                for (int x = c.full.rect().x; x < c.full.rect().right(); ++x)
                    on += img.at (x, y) != panelPx && img.at (x, y) != backgroundPixel();
            check (c.displayRect().contains (c.full.rect()) && on > 60 && colourNear (img, static_cast<float> (cx (c.full) - 10), static_cast<float> (cy (c.full) - 10), 2, colour::accent(), 20) && colourNear (img, static_cast<float> (cx (c.full) + 10), static_cast<float> (cy (c.full) + 10), 2, colour::accent(), 20), nm + ": the full screen button is drawn in the top right corner of the display (" + std::to_string (on) + " button pixels in its rectangle: frame and the expand corners)");
        }
        check (! sc.overflow() && drawnOutside (render (sc), c.rect(), backgroundPixel()) == 0 && c.sizeOk(), nm + ": draws inside, fits one scene (" + std::to_string (sc.numOps()) + " ops)");
        writePng (outDir() + "/compact_" + std::to_string (v) + ".png", render (sc), 1);
        if (v == 1)
        {
            Change ch = tap (c, c.instances.rect().right() - 14, cy (c.instances), 0.0);   // a tap on the right arrow: the next source, B (index 2)
            check (ch.kind == ChangeKind::Request && ch.id == 50, nm + ": the dropdown arrow opens the instance list");
            ch = tap (c, cx (c.instances), cy (c.instances), 50.0);   // a tap on the name: the list is asked for
            check (ch.kind == ChangeKind::Request && ch.id == 50, nm + ": a tap on the name of the source selector: Request { id 50 } (the owner opens the list)");
            ch = tap (c, cx (c.full), cy (c.full), 100.0);
            check (ch.kind == ChangeKind::Request && ch.id == 53, nm + ": full screen requested: id 53");
            ch = tap (c, cx (c.route), cy (c.route), 200.0);
            check (ch.kind == ChangeKind::Request && ch.id == 52, nm + ": the route button: id 52");
            ch = c.basic.group.turn (3, 0.25);
            check (ch.kind == ChangeKind::Value && ch.id == 307, nm + ": the Basic group's fourth Q-Link is LEVEL: id 307");
            check (std::string (c.instances.state.label) == "Osc A", nm + ": the selector has the five sources Sub, Osc A, Osc B, Osc C, Noise, Osc A selected");
        }
    }
    // the Q frame moves to the tapped group
    {
        OscillatorFullScreen s;
        s.place (area);
        s.setInstance (OscInstance::A, OscKind::Wavetable);
        giveIds (s);
        Change c = tap (s, cx (*s.panel.part (PanelSlot::Fin)), cy (*s.panel.part (PanelSlot::Fin)), 0.0);
        check (s.activeGroup() == 1 && std::string (s.groupName (1)) == "Unison" && s.groups[1].active() && ! s.groups[0].active(), "full screen: a tap on FIN moves the Q frame to the group Unison");
        check (! c, "full screen: (a plain tap on a number field gives no change)");
        tap (s, cx (s.panel.warp()->second.type), cy (s.panel.warp()->second.type), 2000.0);
        check (std::string (s.groupName (s.activeGroup())) == "Warp" && s.activeGroup() == 2, "full screen: a tap in the second warp slot: the group Warp (WARP 1 and 2 type and amount)");
        s.setActiveGroup (0);
        check (s.activeGroup() == 0, "full screen: active group can return to Basic");
        c = s.turnQ (3, 1);
        check (c.kind == ChangeKind::Index && c.id == 303, "full screen: the fourth Q-Link of Basic is OCT: id 303");
        c = s.turnQ (0, 1);
        check (c.kind == ChangeKind::Index && c.id == 300 && c.index == 1, "full screen: the first Q-Link of Basic is the type: next pictogram");
        tap (s, 1279 - 10, 5, 4000.0);
        check (s.activeGroup() == 0, "full screen: a tap on nothing does not move the frame");
        c = tap (s, cx (s.close), cy (s.close), 5000.0);
        check (c.kind == ChangeKind::Request && c.id == 1, "full screen: close requested: { request, id 1 }");
        c = tap (s, cx (s.more), cy (s.more), 6000.0);
        check (c.kind == ChangeKind::Request && c.id == 9, "full screen: the MORE button: { request, id 9 }");
        c = tap (s, cx (s.top.source.dropdown), cy (s.top.source.dropdown), 7000.0);
        check (c.kind == ChangeKind::Request && c.id == 302, "full screen: the source dropdown: id 302");
        c = tap (s, cx (s.power), cy (s.power), 8000.0);
        check (c.kind == ChangeKind::Index && c.id == 2, "full screen: the on / off toggle: id 2");
        check (s.instance() == OscInstance::A && s.kind() == OscKind::Wavetable, "full screen: there is no instance selector: the instance is the owner's (setInstance)");
        s.setInstance (OscInstance::C, OscKind::Granular);
        giveIds (s);
        Knob* len = s.panel.knob (PanelSlot::Length);
        len->state.add (5, "LFO 1", 0.3);
        check (len->id() == 103 || len->id() > 0, "full screen: Length knob model receives an assigned id");
        check (len->turn (0.1).id == len->id(), "full screen: turning Length knob model emits its assigned id");
        s.setActiveGroup (2);
        s.setInstance (OscInstance::Noise, OscKind::Wavetable);
        check (s.activeGroup() == 0 && s.numGroups() >= 2, "full screen: switching the instance: the Q frame is back at Basic");
        // sub: the waveform selector, OCT, CRS and PHASE in the first group
        s.setInstance (OscInstance::Sub, OscKind::Wavetable);
        giveIds (s);
        s.top.oct.state.value = 0;
        c = s.turnQ (1, 1);
        check (c.kind == ChangeKind::Index && c.id == 303 && c.index == 5, "full screen sub: the second Q-Link of Basic is OCT: id 303");
        s.top.selector.state.index = 2;
        c = tap (s, s.top.selector.cell (4).x + 5, s.top.selector.cell (4).y + 5, 30000.0);
        check (c.kind == ChangeKind::Index && c.id == 300 && c.index == 4, "full screen sub: a tap on the fifth waveform (square): { id 300, 4 }");
        // noise
        s.setInstance (OscInstance::Noise, OscKind::Wavetable);
        giveIds (s);
        s.top.kind.state.count = 2; s.top.kind.state.index = 0;
        c = s.turnQ (0, 1);
        check (c.kind == ChangeKind::Index && c.id == 314 && c.index == 1, "full screen noise: the first Q-Link of Basic is KIND: id 314");
        s.top.filter.state.value = 0.5;
        c = s.turnQ (2, 0.1);
        check (c.kind == ChangeKind::Value && c.id == 304, "full screen noise: the third is FILTER (SOUND is the second): id 304");
    }

    // other sizes: proportional
    {
        OscillatorFullScreen s;
        s.place ({ 0, 0, 1920, 1200 });
        s.setInstance (OscInstance::A, OscKind::Granular);
        ControlModel* all[96];
        const int n = s.collectModelLeaves (all, 0, 96);
        bool ok = true;
        for (int g = 0; g < s.numGroups(); ++g)
            ok = ok && s.groups[g].check (all, n) == 0;
        check (ok && s.close.rect().x < s.mix.rect().x && Rect { 0, 0, 1920, 1200 }.contains (s.panel.rect()), "full screen: 1920 x 1200: groups adjacent, the same arrangement scaled");
        Scene sc (1920, 1200, kBackgroundRgba);
        s.draw (sc.builder());
        check (! sc.overflow() && drawnOutside (render (sc), { 0, 0, 1920, 1200 }, backgroundPixel()) == 0, "full screen: 1920 x 1200: draws (nothing outside the screen)");
        OscillatorCompact c;
        c.place ({ 0, 0, 630, 1050 });
        Scene sc2 (630, 1050, kBackgroundRgba);
        c.draw (sc2.builder());
        check (! sc2.overflow() && drawnOutside (render (sc2), c.rect(), backgroundPixel()) == 0, "compact: 630 x 1050: draws inside");
    }
    // ---- LED switches (frameless: a dot and the name, the state is the dot): the toggles of the sub and the noise page; named by the function only, at least 56 px, and on the baseline of the controls of their row
    {
        auto cy = [] (const ControlModel& c) { return c.rect().y + c.rect().h / 2; };
        auto isLed = [] (const ControlModel* c, const char* name)
        {
            const LedToggle* t = dynamic_cast<const LedToggle*> (c);
            return t != nullptr && std::strcmp (t->state.label, name) == 0 && t->rect().h >= 56 && t->rect().w >= LedToggle::widthFor (name);
        };
        OscillatorFullScreen sub;
        sub.place (area);
        sub.setInstance (OscInstance::Sub, OscKind::Wavetable);
        check (isLed (sub.panel.part (PanelSlot::SubEnv1), "ENV 1") && isLed (sub.panel.part (PanelSlot::SubMono), "MONO") && isLed (sub.panel.part (PanelSlot::SubContig), "FREE RUN"), "sub: ENV 1, MONO and FREE RUN are LED toggles named by the function only (no ON / OFF in the label), 56 px high");
        check (std::abs (cy (*sub.panel.part (PanelSlot::SubEnv1)) - cy (*sub.panel.part (PanelSlot::SubMono))) <= 2 && std::abs (cy (*sub.panel.part (PanelSlot::SubMono)) - cy (*sub.panel.part (PanelSlot::SubContig))) <= 2, "sub: the three switches stand on one baseline");
        OscillatorFullScreen noise;
        noise.place (area);
        noise.setInstance (OscInstance::Noise, OscKind::Wavetable);
        check (isLed (noise.panel.part (PanelSlot::NoiseEnv1), "ENV 1") && isLed (noise.panel.part (PanelSlot::NoiseKey), "KEY TRACK"), "noise: ENV 1 and KEY TRACK are LED toggles");
        check (noise.panel.part (PanelSlot::NoisePitch) != nullptr && noise.panel.part (PanelSlot::NoiseFine) != nullptr &&
               std::abs (cy (*noise.panel.part (PanelSlot::NoisePitch)) - cy (*noise.panel.part (PanelSlot::NoiseFine))) <= 2 &&
               std::abs (cy (*noise.panel.part (PanelSlot::NoiseEnv1)) - cy (*noise.panel.part (PanelSlot::NoisePitch))) <= 2 &&
               cx (*noise.panel.part (PanelSlot::NoisePitch)) < cx (*noise.panel.part (PanelSlot::NoiseFine)) && cx (*noise.panel.part (PanelSlot::NoiseFine)) < cx (*noise.panel.part (PanelSlot::NoiseKey)) && cx (*noise.panel.part (PanelSlot::NoiseKey)) < cx (*noise.panel.part (PanelSlot::NoiseEnv1)),
               "noise: PITCH, FINE, KEY TRACK and ENV 1 share one row, left to right");

        // the source selector of the Play page (replaces the five tabs SUB A B C NOISE): the closed selector says each source in the owner's wording, the list entries are those five names
        OscillatorCompact c;
        c.place ({ 0, 0, 420, 700 });
        const char* const want[5] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" };
        bool names = true;
        for (int i = 0; i < 5; ++i)
        {
            c.setInstance (static_cast<OscInstance> (i), OscKind::Wavetable);
            names = names && std::string (c.instances.state.label) == want[i] && std::string (OscillatorCompact::sourceName (i)) == want[i];
        }
        check (names, "the source selector: Sub, Osc A, Osc B, Osc C, Noise, the closed selector shows the current one");
        ListPopup lp;
        lp.place ({ 6, 62, 408, 5 * ListPopup::kRowH + 2 * ListPopup::kPad });
        static const char* const items[5] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" };
        lp.state.items = items; lp.state.count = 5; lp.state.current = 1;
        bool rows = true;
        for (int i = 0; i < 5; ++i)
            rows = rows && lp.rowRect (i).h >= 56;
        check (rows && ! lp.scrolls(), "the list of the source selector: five rows of at least 56 px, no scrolling");
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
            check (nl >= 1 && missed == 0, nm + ": a tap on the middle of each of the " + std::to_string (nl) + " leaves reaches it" + which);
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
    // ---- the three displays of the Play pages (oscillator wave, envelope graph, LFO curve): the same top and the same height for each of the five oscillator sources, the controls below start on one line, the top rows are 56 px
    {
        const OscInstance insts[5] = { OscInstance::Sub, OscInstance::A, OscInstance::B, OscInstance::C, OscInstance::Noise };
        for (OscInstance i : insts)
            for (int page = 1; page <= 3; ++page)   // Play 1 / 2 / 3: the same thirds, the Q frame on the oscillator, the envelope or the LFO third
            {
                OscillatorCompact oc;
                EnvCompact ec;
                LfoCompact lc;
                oc.place ({ 8, 114, 420, 620 });
                ec.place ({ 440, 114, 400, 620 });
                lc.place ({ 858, 114, 408, 620 });
                oc.setInstance (i, OscKind::Wavetable);
                oc.basic.group.setActive (page == 1);
                ec.group.setActive (page == 2);
                lc.group.setActive (page == 3);
                const std::string nm = std::string ("Play ") + std::to_string (page) + ", source " + OscillatorCompact::sourceName (i);
                const Rect w = oc.displayRect(), e = ec.graphRect(), l = lc.displayRect();
                check (w.y == e.y && e.y == l.y && w.h == e.h && e.h == l.h && w.h > 0, nm + ": the wave display, the envelope graph and the LFO curve have the same top y (" + std::to_string (w.y) + ") and the same height (" + std::to_string (w.h) + ")");
                check (w.bottom() == e.bottom() && e.bottom() == l.bottom(), nm + ": the three displays end on the same line");
                check (oc.instances.rect().y == ec.instances.rect().y && ec.instances.rect().y == lc.instance.rect().y && oc.instances.rect().h == ec.instances.rect().h && ec.instances.rect().h == lc.instance.rect().h && oc.instances.rect().h == 56, nm + ": the selector of the three thirds is in the same top row, 56 px high");
                check (oc.basic.rect().y == ec.attack.rect().y && ec.attack.rect().y == lc.shape.rect().y, nm + ": the controls under the displays start on one line (Basic, ATTACK, SHAPE)");
                // (a) the plot areas: the same top, bottom and height (and the same x range) in the three thirds
                lfodisplay::Data ld;
                ld.kind = lfodisplay::Data::Kind::Shape;
                ld.area = playrows::plotRect (lc.displayRect());   // the owner of the compact view names the plot area
                const Rect pw = oc.plotRect(), pe = ec.graph.plot(), pl = lfodisplay::plotRect (lc.displayRect(), ld);
                check (pw.y == pe.y && pe.y == pl.y && pw.h == pe.h && pe.h == pl.h && pw.bottom() == pe.bottom() && pe.bottom() == pl.bottom() && pw.h > 100 && pw == playrows::plotRect (w) && pe == playrows::plotRect (e) && pl == playrows::plotRect (l),
                       nm + ": the wave, the envelope curve and the LFO curve have the same plot area top (" + std::to_string (pw.y) + "), bottom and height (" + std::to_string (pw.h) + "), below the header band");
                // (c) the touch targets of the buttons are 56 px at least and inside the display; (d) the full screen button is at the same place in the three thirds, the rightmost of its band
                const Rect fo = oc.full.rect(), fe = ec.full.rect(), fl = lc.full.rect();
                check (fo.w >= 56 && fo.h >= 56 && fe.w >= 56 && fe.h >= 56 && fl.w >= 56 && fl.h >= 56 && w.contains (fo) && e.contains (fe) && l.contains (fl) && w.contains (oc.power.rect()) && (i == OscInstance::Sub || (w.contains (oc.route.rect()) && oc.route.rect().h >= 56 && oc.route.rect().w >= 56)),
                       nm + ": the touch targets of the buttons are 56 px at least and inside their display");
                check (w.right() - fo.right() == e.right() - fe.right() && e.right() - fe.right() == l.right() - fl.right() && fo.y - w.y == fe.y - e.y && fe.y - e.y == fl.y - l.y && fo.right() == w.right() - 4 && fe.right() == e.right() - 4 && fl.right() == l.right() - 4 && (i == OscInstance::Sub || (oc.route.rect().right() <= fo.x && oc.power.rect().right() <= oc.route.rect().x)),
                       nm + ": the full screen button is at the same place in the three displays (rightmost); the route button is left of it, the power button at the left end");
            }
    }
    // ---- standalone native instance selector aperture on OscillatorFullScreen
    {
        OscillatorFullScreen s;
        s.place ({ 0, 110, 1280, 628 });
        check (! s.hasNativeInstanceSelector(), "initially nativeSelector is false");
        check (s.selectorArea().w == 84, "default width without native selector is 84");

        s.setNativeInstanceSelector (true);
        check (s.hasNativeInstanceSelector(), "setNativeInstanceSelector(true) enables native selector");
        const Rect apt = s.selectorArea();
        check (apt.w == 1280 && apt.h == 48,
               "source tabs span the full width and are 48 px tall");
        check (apt.x == 0 && apt.y == 690, "source tabs placed at (0, 690)");
        check (s.isNativeSelector (apt.x + 10, apt.y + 10), "point in selectorArea recognized as native selector");
        check (! s.isNativeSelector (s.top.rect().x + 10, s.top.rect().y + 10), "point in top row is not native selector");

        // Touch pass-through
        const Change c = s.touchDown (apt.x + 10, apt.y + 10, 100.0);
        check (! c, "touchDown on native selector area yields Change::none()");

        // Standalone native routing aperture on the right side of the top band
        check (! s.hasNativeRouting(), "initially nativeRouting is false");
        check (! s.isNativeRoutingActive(), "initially isNativeRoutingActive is false");
        const Rect rapt = s.routingArea();
        check (rapt.x == 1122 && rapt.y == 143 && rapt.w == 150 && rapt.h == 56,
               "routingArea placed at (1122, 143, 150, 56), strictly 3 px below right marker (1241, 134, 6, 6)");
        check (! apt.overlaps (rapt) && ! s.top.rect().overlaps (rapt) && ! s.displayRect().overlaps (rapt),
               "routingArea does not overlap selectorArea, top row, or display");

        s.setNativeRouting (true);
        check (s.hasNativeRouting() && s.isNativeRoutingActive(), "setNativeRouting(true) enables active native routing while ON");
        check (s.isNativeRouting (1150, 160), "point inside routingArea recognized as native routing");
        check (! s.isNativeRouting (s.top.rect().x + 10, s.top.rect().y + 10), "point in top row is not native routing");
        check (! s.touchDown (1150, 160, 110.0), "touchDown on native routing area yields Change::none()");

        Component* lv[96];
        int nl = s.leaves (lv, 96);
        bool hasMix = false;
        for (int i = 0; i < nl; ++i)
            hasMix = hasMix || lv[i] == &s.mix;
        check (! hasMix, "mix indicator is hidden while native routing aperture is active");

        // OFF state closes native routing aperture and restores dimmed read-only mix indicator while keeping selector open
        s.setInactive (true);
        check (s.hasNativeRouting() && ! s.isNativeRoutingActive(), "inactive (OFF) oscillator closes native routing aperture");
        check (! s.isNativeRouting (1150, 160), "point inside routingArea is not native routing when OFF");
        check (s.isNativeSelector (apt.x + 10, apt.y + 10), "native selector remains active when oscillator is OFF");
        nl = s.leaves (lv, 96);
        hasMix = false;
        for (int i = 0; i < nl; ++i)
            hasMix = hasMix || lv[i] == &s.mix;
        check (hasMix && s.mix.inactive(), "read-only mix indicator returns in inactive look when oscillator is OFF");

        s.setInactive (false);
        check (s.isNativeRoutingActive(), "powering ON re-opens native routing aperture");
        nl = s.leaves (lv, 96);
        hasMix = false;
        for (int i = 0; i < nl; ++i)
            hasMix = hasMix || lv[i] == &s.mix;
        check (! hasMix, "mix indicator is hidden again when oscillator powers back ON");
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
