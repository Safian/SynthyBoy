// SynthyBoy - MPC UI component library tests: the COMPOSITE components (BasicControls, UnisonBox, WarpSlot, WarpPair, KnobRandPair, KnobPanel with its three presets) and the Q groups.
// A change of a child comes out with the id of the child; the children lie inside the composite and do not overlap; the presets place everything at the size of the Key 61 region and at a
// larger one; the Q group: frame, routing, adjacency.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../../overlay/gpu/cell_plan.h"

using namespace uit;

namespace
{
Change tap (Component& c, int x, int y, double t)
{
    const Change d = c.touchDown (x, y, t);
    const Change u = c.touchUp (x, y);
    return u ? u : d;
}
Change drag (Component& c, int x, int y0, int y1, double t = 0.0)
{
    Change last = c.touchDown (x, y0, t);
    const int dir = y1 > y0 ? 1 : -1;
    for (int y = y0 + dir; dir > 0 ? y <= y1 : y >= y1; y += dir * 4)
        if (const Change m = c.touchMove (x, y))
            last = m;
    if (const Change m = c.touchMove (x, y1))
        last = m;
    return last;
}
Change hdrag (Component& c, int y, int x0, int x1, double t = 0.0)
{
    Change last = c.touchDown (x0, y, t);
    const int dir = x1 > x0 ? 1 : -1;
    for (int x = x0 + dir; dir > 0 ? x <= x1 : x >= x1; x += dir * 4)
        if (const Change m = c.touchMove (x, y))
            last = m;
    if (const Change m = c.touchMove (x1, y))
        last = m;
    return last;
}
int cx (const Component& c) { return c.rect().x + c.rect().w / 2; }
int cy (const Component& c) { return c.rect().y + c.rect().h / 2; }
/** All leaves of a component do not overlap each other and lie inside `r`. */
bool leavesOk (Component& c, const Rect& r, std::string& why)
{
    Component* l[64];
    const int n = c.collectLeaves (l, 0, 64);
    for (int i = 0; i < n; ++i)
    {
        if (! r.contains (l[i]->rect())) { why = "leaf " + std::to_string (i) + " outside"; return false; }
        for (int j = i + 1; j < n; ++j)
            if (l[i]->rect().overlaps (l[j]->rect())) { why = "leaves " + std::to_string (i) + " and " + std::to_string (j) + " overlap"; return false; }
    }
    return n > 0;
}
void drawChecks (Component& c, const std::string& name, const Rect& r, const char* file)
{
    c.place (r);
    Scene s (r.right() + 24, r.bottom() + 24, kBackgroundRgba);
    c.draw (s.builder());
    check (! s.overflow(), name + ": the scene fits (" + std::to_string (s.numOps()) + " ops)");
    const Image im = render (s);
    check (drawnOutside (im, r, backgroundPixel()) == 0, name + ": nothing drawn outside the rectangle");
    check (drawnInside (im, r, backgroundPixel()) > 100, name + ": something is drawn");
    int smallText = 0;
    for (uint32_t i = 0; i < s.data->numOps; ++i)
        smallText += s.data->ops[i].op == gpu::kOpText && s.data->ops[i].f[2] < 2.0f;
    check (smallText == 0, name + ": every text is legible (>= 2 pixels per font pixel)");
    Rect z[64];
    const int n = c.touchTargets (z, 64);
    bool ok = n > 0;
    for (int i = 0; i < n; ++i)
        ok = ok && r.contains (z[i]) && std::min (z[i].w, z[i].h) >= std::min (56, std::min (r.w, r.h));
    check (ok, name + ": the touch targets are inside and at least 56 px in the smaller direction where the rectangle allows");
    if (file)
        writePng (outDir() + "/" + file, im, 1);
    c.setEnabled (false);
    Scene sd (r.right() + 24, r.bottom() + 24, kBackgroundRgba);
    c.draw (sd.builder());
    check (drawnOutside (render (sd), r, backgroundPixel()) == 0 && render (sd).px != im.px, name + ": disabled: dim, nothing outside");
    c.setEnabled (true);
}

void testUnisonBox()
{
    UnisonBox u;
    u.place ({ 20, 20, u.minSize().w, u.minSize().h });
    u.gear.setId (1); u.count.setId (2);
    u.count.state.min = 1; u.count.state.max = 16; u.count.state.value = 4;
    std::string why;
    check (u.sizeOk() && leavesOk (u, u.rect(), why), "unison box: fits, inside, no overlap" + why);
    Change c = tap (u, cx (u.gear), cy (u.gear), 0.0);
    check (c.kind == ChangeKind::Request && c.id == 1, "unison box: the gear asks for the settings: id 1");
    c = tap (u, cx (u.count), u.count.rect().y + 5, 100.0);
    check (c.kind == ChangeKind::Index && c.id == 2 && c.index == 5, "unison box: up on the stepper: { id 2, 5 }");
    c = u.turn (-1);
    check (c.kind == ChangeKind::Index && c.id == 2 && c.index == 3, "unison box: turn goes to the stepper");
    drawChecks (u, "unison box", { 10, 10, u.minSize().w, u.minSize().h }, "unison_min.png");
    drawChecks (u, "unison box (large)", { 10, 10, 300, 200 }, nullptr);
}

void testWarp()
{
    WarpSlot w;
    w.place ({ 20, 20, w.minSize().w, w.minSize().h });
    w.amount.setId (1); w.type.setId (2); w.var.setId (3);
    w.type.state.count = 12; w.type.state.index = 2; w.type.setItem ("SYNC");
    w.amount.state.value = 0.3;
    std::string why;
    check (w.sizeOk() && leavesOk (w, w.rect(), why) && w.amount.rect().x < w.type.rect().x, "warp slot: fits, inside, no overlap, the knob on the left" + why);
    Component* l[8];
    check (w.collectLeaves (l, 0, 8) == 2 && ! w.hasVar(), "warp slot: without the extra value: knob and stepper only");
    Change c = drag (w, cx (w.amount), cy (w.amount) + 20, cy (w.amount) - 20);
    check (c.kind == ChangeKind::Value && c.id == 1, "warp slot: the knob: id 1");
    w.touchUp (0, 0);
    c = tap (w, w.type.rect().right() - 10, cy (w.type), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 2 && c.index == 3, "warp slot: the stepper: next type, id 2");
    c = w.turn (0.1);
    check (c.kind == ChangeKind::Value && c.id == 1, "warp slot: turn goes to the amount knob");
    // OFF: dim, the knob does not react, the type can still be chosen
    w.setTypeOff (true);
    check (! w.amount.enabled() && ! drag (w, cx (w.amount), cy (w.amount) + 20, cy (w.amount) - 20) && ! w.turn (0.1), "warp slot: the type OFF: the knob is dim and without reaction");
    w.touchUp (0, 0);
    c = tap (w, w.type.rect().right() - 10, cy (w.type), 1000.0);
    check (c.kind == ChangeKind::Index && c.id == 2, "warp slot: the type OFF: the stepper still works");
    drawChecks (w, "warp slot (type OFF)", { 10, 10, w.minSize().w, 72 }, "warpslot_off.png");
    w.setTypeOff (false);
    // the extra value
    w.setHasVar (true);
    w.place ({ 20, 20, w.minSize().w, 72 });
    check (w.hasVar() && w.collectLeaves (l, 0, 8) == 3 && leavesOk (w, w.rect(), why) && w.sizeOk(), "warp slot: with the extra value: three children, inside, no overlap, fits the larger minimum" + why);
    c = drag (w, cx (w.var), cy (w.var) + 20, cy (w.var) - 20);
    check (c.kind == ChangeKind::Value && c.id == 3, "warp slot: the extra value (WARP Var): id 3");
    w.touchUp (0, 0);
    drawChecks (w, "warp slot (with the extra value)", { 10, 10, w.minSize().w, 72 }, "warpslot_var.png");
    drawChecks (w, "warp slot (large)", { 10, 10, 560, 116 }, nullptr);
    // mirrored
    WarpSlot m (true);
    m.place ({ 20, 20, 400, 100 });
    check (m.mirrored() && m.amount.rect().x > m.type.rect().x && leavesOk (m, m.rect(), why), "warp slot mirrored: the knob on the right" + why);
    m.setHasVar (true);
    check (m.var.rect().right() <= m.type.rect().x && leavesOk (m, m.rect(), why), "warp slot mirrored with the extra value: it sits left of the stepper" + why);
    // the stacked variant (inside a titled block of the full screen)
    {
        WarpPair sp;
        sp.setStacked (true);
        sp.place ({ 10, 10, 560, 160 });
        check (sp.stacked() && sp.first.rect().bottom() <= sp.second.rect().y && sp.first.rect().x == sp.second.rect().x && ! sp.second.mirrored() && leavesOk (sp, sp.rect(), why) && sp.minSize().h == 2 * 72 + kQLane && sp.second.rect().y - sp.first.rect().bottom() >= kQLane && sp.first.amount.state.side, "warp pair stacked: the first slot over the second (the lane of the Q chips between them), both with the medium knob on the left" + why);
        drawChecks (sp, "warp pair stacked", { 10, 10, 560, 160 }, nullptr);
    }
    // the pitch tracking toggle has a label
    {
        ToggleIcon tg;
        tg.setLabel ("TRACK");
        tg.place ({ 10, 10, 72, 56 });
        check (tg.sizeOk() && tg.minSize().w == 72, "toggle with a label: 72 x 56 at least");
        tg.state.icon = ToggleIcon::Icon::PitchTrack;
        drawChecks (tg, "toggle with the label TRACK", { 10, 10, 72, 56 }, "toggle_track.png");
    }
    // the pair
    WarpPair p;
    p.place ({ 20, 20, p.minSize().w, 72 });
    p.first.amount.setId (1); p.second.amount.setId (2); p.first.type.setId (3); p.second.type.setId (4);
    check (p.sizeOk() && leavesOk (p, p.rect(), why) && p.first.rect().right() <= p.captionBox().x && p.captionBox().right() <= p.second.rect().x, "warp pair: fits, inside, the caption between the slots" + why);
    check (p.first.amount.rect().x < p.first.type.rect().x && p.second.amount.rect().x > p.second.type.rect().x, "warp pair: the knobs on the outside, mirrored");
    c = drag (p, cx (p.second.amount), cy (p.second.amount) + 20, cy (p.second.amount) - 20);
    check (c.kind == ChangeKind::Value && c.id == 2, "warp pair: the right knob: id 2");
    p.touchUp (0, 0);
    p.first.type.state.count = 5; p.first.type.state.index = 2;
    c = tap (p, p.first.type.rect().x + 5, cy (p.first.type), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 3, "warp pair: the left stepper: id 3");
    drawChecks (p, "warp pair", { 10, 10, p.minSize().w, 72 }, "warppair_min.png");
    drawChecks (p, "warp pair (large)", { 10, 10, 900, 116 }, nullptr);
}

void testRandPair()
{
    KnobRandPair r ("LENGTH");
    r.place ({ 20, 20, r.minSize().w, r.minSize().h });
    r.main.setId (1); r.rand.setId (2);
    std::string why;
    check (r.sizeOk() && leavesOk (r, r.rect(), why) && r.rand.rect().y >= r.main.rect().bottom() && r.rand.state.small, "knob + rand pair: fits, the small knob below the main one" + why);
    Change c = drag (r, cx (r.main), cy (r.main) + 20, cy (r.main) - 20);
    check (c.kind == ChangeKind::Value && c.id == 1, "knob + rand pair: the main knob: id 1");
    r.touchUp (0, 0);
    c = drag (r, cx (r.rand), cy (r.rand) + 20, cy (r.rand) - 20);
    check (c.kind == ChangeKind::Value && c.id == 2, "knob + rand pair: the RAND knob: id 2");
    r.touchUp (0, 0);
    c = r.turn (0.2);
    check (c.kind == ChangeKind::Value && c.id == 1, "knob + rand pair: turn goes to the main knob");
    drawChecks (r, "knob + rand pair", { 10, 10, r.minSize().w, r.minSize().h }, "randpair_min.png");
    drawChecks (r, "knob + rand pair (large)", { 10, 10, 200, 300 }, nullptr);
}


void testRoutePanel()
{
    RoutePanel r;
    r.place ({ 20, 20, r.minSize().w, r.minSize().h });
    r.target.setId (1); r.balance.setId (2); r.bus1.setId (3); r.bus2.setId (4); r.close.setId (5);
    r.target.state.count = 4; r.target.state.index = 0; r.target.setItem ("FILTER");
    std::string why;
    check (r.sizeOk() && leavesOk (r, r.rect(), why), "route panel: fits its minimum size, five children inside, no overlap" + why);
    Change c = tap (r, r.target.rect().right() - 10, cy (r.target), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 1 && c.index == 1, "route panel: the target stepper: next, id 1");
    check (r.targetIsFilter() && r.balance.enabled() && r.balance.state.bipolar, "route panel: the balance knob is bipolar and enabled while the target is the filter");
    c = drag (r, cx (r.balance), cy (r.balance) + 20, cy (r.balance) - 20);
    check (c.kind == ChangeKind::Value && c.id == 2, "route panel: the balance knob: id 2");
    r.touchUp (0, 0);
    r.setTargetIsFilter (false);
    check (! r.balance.enabled() && ! drag (r, cx (r.balance), cy (r.balance) + 20, cy (r.balance) - 20), "route panel: the target is not the filter: the balance knob is dim and without reaction");
    r.touchUp (0, 0);
    c = drag (r, cx (r.bus1), cy (r.bus1) + 20, cy (r.bus1) - 20);
    check (c.kind == ChangeKind::Value && c.id == 3, "route panel: BUS 1 send: id 3");
    r.touchUp (0, 0);
    c = drag (r, cx (r.bus2), cy (r.bus2) + 20, cy (r.bus2) - 20);
    check (c.kind == ChangeKind::Value && c.id == 4, "route panel: BUS 2 send: id 4");
    r.touchUp (0, 0);
    c = tap (r, cx (r.close), cy (r.close), 100.0);
    check (c.kind == ChangeKind::Request && c.id == 5, "route panel: the close button: { request, id 5 }");
    c = r.turn (1);
    check (c.kind == ChangeKind::Index && c.id == 1, "route panel: turn goes to the target stepper");
    r.setTargetIsFilter (true);
    r.setEnabled (false);
    check (! r.turn (1) && ! r.balance.enabled(), "route panel: disabled with everything");
    r.setEnabled (true);
    check (r.balance.enabled(), "route panel: enabled again, filter target: the balance knob is back");
    drawChecks (r, "route panel", { 10, 10, r.minSize().w, r.minSize().h }, "routepanel_min.png");
    drawChecks (r, "route panel (large)", { 10, 10, 700, 240 }, "routepanel.png");
    // the popup variant
    RoutePanel pp;
    const Rect parent { 0, 0, 1280, 800 };
    pp.place ({ 300, 250, 560, 200 });
    pp.setPopup (true, parent);
    pp.setBackdropId (77);
    pp.target.setId (1); pp.close.setId (5);
    check (pp.popup() && pp.group.numMembers() == 4 && pp.group.member (0) == &pp.target && pp.group.member (1) == &pp.balance && pp.group.member (2) == &pp.bus1 && pp.group.member (3) == &pp.bus2, "route panel popup: exactly four values = one Q group (target, balance, BUS 1, BUS 2)");
    pp.group.setActive (true);
    Component* pl[8];
    const int np = pp.collectLeaves (pl, 0, 8);
    check (pp.group.check (pl, np) == 0, "route panel popup: the Q group is adjacent (the close button is in a row of its own, outside the union)");
    Scene sp (1280, 800, kBackgroundRgba);
    pp.draw (sp.builder());
    const Image ip = render (sp);
    check (! sp.overflow() && drawnOutside (ip, parent, backgroundPixel()) == 0 && ip.at (50, 50) != backgroundPixel() && colourNear (ip, 300.0f, 300.0f, 0, colour::accent(), 4), "route panel popup: a dimmed backdrop over the parent rectangle (changes the pixels), the framed box, nothing outside the parent");
    c = tap (pp, 50, 50, 0.0);
    check (c.kind == ChangeKind::Request && c.id == 77, "route panel popup: a tap outside the box (on the backdrop) = close requested: id 77");
    pp.touchDown (50, 50, 100.0);
    pp.touchMove (50, 300);
    check (! pp.touchUp (50, 300), "route panel popup: a drag on the backdrop is not a tap");
    check (! tap (pp, -5, -5, 200.0), "route panel popup: a touch outside the parent is ignored");
    pp.target.state.count = 4;
    c = tap (pp, pp.target.rect().right() - 10, cy (pp.target), 300.0);
    check (c.kind == ChangeKind::Index && c.id == 1, "route panel popup: a tap inside the box reaches the children");
    c = tap (pp, cx (pp.close), cy (pp.close), 400.0);
    check (c.kind == ChangeKind::Request && c.id == 5, "route panel popup: the close button inside: id 5");
    pp.setEnabled (false);
    check (! tap (pp, 50, 50, 500.0), "route panel popup: disabled: no reaction");
    writePng (outDir() + "/routepanel_popup.png", ip, 1);
}

void testDetailPage()
{
    struct Case { const PanelPreset* p; const char* title; int parts, groups; bool display; };
    const Case cases[] = { { &presets::kUnisonDetail, "UNISON", 7, 2, true }, { &presets::kUnisonDetailMultisample, "UNISON", 6, 2, true }, { &presets::kUnisonDetailGranular, "UNISON", 6, 2, true },
                           { &presets::kGrainDetail, "GRAIN WINDOW", 5, 2, true }, { &presets::kEnvelopeDetail, "ENVELOPE", 7, 2, true } };
    int caseNo = 0;
    for (const Case& cs : cases)
    {
        ++caseNo;
        DetailPage pg;
        pg.load (*cs.p, cs.title);
        const Rect area { 56, 14, 1168, 600 };
        pg.place (area);
        const std::string nm = std::string ("detail page ") + cs.p->name;
        check (pg.panel.numParts() == cs.parts && pg.numGroups() == cs.groups && pg.activeGroup() == 0, nm + ": parts and Q groups from the table, the first group active");
        check (pg.sizeOk(), nm + ": fits its minimum size");
        check (cs.display == ! pg.displayRect().empty() && (pg.displayRect().empty() || (area.contains (pg.displayRect()) && ! pg.displayRect().overlaps (pg.panel.rect()))), nm + ": the display area only where the preset has one, not under the panel");
        check (! pg.back.rect().overlaps (pg.panel.rect()) && area.contains (pg.back.rect()), nm + ": the back button in the top bar, not over the grid");
        {
            Component* lv[64];
            const int nl = pg.collectLeaves (lv, 0, 64);
            int covered = 0, tall = 0, noValue = 0;
            for (int g = 0; g < pg.numGroups(); ++g)
                for (int i = 0; i < 4; ++i)
                    if (const Component* m = pg.groups[g].member (i))
                    {
                        const Rect chip { m->rect().x + 4, std::max (0, m->rect().y - 15), 18, std::min (15, m->rect().y) };
                        for (int k = 0; k < nl; ++k)
                            covered += lv[k]->rect().overlaps (chip);
                    }
            for (int k = 0; k < nl; ++k)
                if (dynamic_cast<Stepper*> (lv[k]) != nullptr)
                    tall += lv[k]->rect().h > 72;   // no stepper is taller than a row of controls
            check (covered == 0 && tall == 0, nm + ": the index chips lie on free lanes above their members, no stepper taller than 72 px" + (covered ? " (covered " + std::to_string (covered) + ")" : std::string()));
            (void) noValue;
        }
        std::string why;
        check (leavesOk (pg, area, why), nm + ": every leaf inside, no overlap" + why);
        Component* all[64];
        const int n = pg.collectLeaves (all, 0, 64);
        for (int g = 0; g < pg.numGroups(); ++g)
            check (pg.groups[g].check (all, n) == 0 && pg.groups[g].numMembers() >= 1, nm + ": Q group " + std::to_string (g + 1) + " is adjacent (no other control in its union)");
        pg.back.setId (1);
        Change c = tap (pg, cx (pg.back), cy (pg.back), 0.0);
        check (c.kind == ChangeKind::Request && c.id == 1, nm + ": the back button: { request, id 1 }");
        if (Stepper* st = pg.panel.stepper (PanelSlot::Mode)) { st->state.count = 5; st->state.index = 0; }
        Component* first = pg.groups[0].member (0);
        first->setId (77);
        c = pg.turnQ (0, 1);
        check ((c.kind == ChangeKind::Index || c.kind == ChangeKind::Value) && c.id == 77, nm + ": turn of the first Q-Link goes to the first member of the active group");
        pg.setActiveGroup (1);
        check (pg.groups[1].active() && ! pg.groups[0].active(), nm + ": the second group can be activated, the first is off");
        check (! pg.turnQ (3, 1) || pg.groups[1].member (3) != nullptr, nm + ": an empty place of a group routes nothing");
        drawChecks (pg, nm, area, (std::string ("detail_") + std::to_string (caseNo) + ".png").c_str());
        drawChecks (pg, nm + " (large)", { 10, 10, 1900, 1000 }, nullptr);
    }
    {
        DetailPage pg;
        pg.load (presets::kUnisonDetail, "UNISON");
        pg.place ({ 56, 14, 1168, 600 });
        Stepper* mode = pg.panel.stepper (PanelSlot::Mode);
        mode->state.count = 5; mode->state.index = 0; mode->setItem ("LINEAR");
        mode->setId (9);
        Change c = tap (pg, mode->rect().right() - 10, cy (*mode), 0.0);
        check (c.kind == ChangeKind::Index && c.id == 9 && c.index == 1, "detail page unison: the MODE stepper (Linear / Super / Exp / Inv / Random): next, id 9");
        check (pg.panel.knob (PanelSlot::Width) && pg.panel.knob (PanelSlot::Range) && pg.panel.knob (PanelSlot::WtSpread) && pg.panel.knob (PanelSlot::Warp1Spread) && pg.panel.knob (PanelSlot::Warp2Spread) && pg.panel.stepper (PanelSlot::Stack), "detail page unison: STACK stepper and the WIDTH, RANGE, WT POS, WARP 1, WARP 2 knobs");
    }
    {
        DetailPage pg;
        pg.load (presets::kUnisonDetailMultisample, "UNISON");
        check (pg.groups[0].member (3) == pg.panel.knob (PanelSlot::Range) && pg.groups[1].member (0) == pg.panel.knob (PanelSlot::Warp1Spread) && pg.groups[1].member (1) == pg.panel.knob (PanelSlot::Warp2Spread) && pg.groups[1].member (2) == nullptr && ! pg.panel.knob (PanelSlot::WtSpread), "detail page multisample unison: [MODE, STACK, WIDTH, RANGE] and [WARP 1 spread, WARP 2 spread]");
        pg.load (presets::kUnisonDetailGranular, "UNISON");
        check (pg.groups[1].member (0) == pg.panel.knob (PanelSlot::Start) && pg.groups[1].member (1) == pg.panel.knob (PanelSlot::Span) && pg.groups[1].member (2) == nullptr, "detail page granular unison: [MODE, STACK, WIDTH, RANGE] and [START, SPAN]");
        pg.load (presets::kUnisonDetail, "UNISON");
        check (pg.groups[1].member (0) == pg.panel.knob (PanelSlot::WtSpread) && pg.groups[1].member (2) == pg.panel.knob (PanelSlot::Warp2Spread), "detail page wavetable unison: [MODE, STACK, WIDTH, RANGE] and [WT POS, WARP 1, WARP 2 spread]");
        pg.load (presets::kEnvelopeDetail, "ENVELOPE");
        pg.place ({ 56, 14, 1168, 600 });
        check (pg.groups[0].member (0) == pg.panel.knob (PanelSlot::Delay) && pg.groups[0].member (1) == pg.panel.knob (PanelSlot::Attack) && pg.groups[0].member (2) == pg.panel.knob (PanelSlot::Hold) && pg.groups[0].member (3) == pg.panel.knob (PanelSlot::Decay)
               && pg.groups[1].member (0) == pg.panel.knob (PanelSlot::Sustain) && pg.groups[1].member (1) == pg.panel.knob (PanelSlot::Release) && pg.groups[1].member (2) == pg.panel.toggle (PanelSlot::Override) && ! pg.displayRect().empty(), "detail page envelope: [DELAY, A, H, D] and [S, R, OVERRIDE], the display area reserved above");
        pg.panel.toggle (PanelSlot::Override)->setId (90);
        Change c = tap (pg, cx (*pg.panel.toggle (PanelSlot::Override)), cy (*pg.panel.toggle (PanelSlot::Override)), 0.0);
        check (c.kind == ChangeKind::Index && c.id == 90 && c.index == 1, "detail page envelope: the OVERRIDE toggle: id 90");
        pg.load (presets::kGrainDetail, "GRAIN WINDOW");
        check (pg.panel.knob (PanelSlot::Amount) && pg.panel.knob (PanelSlot::AmountRand) && pg.panel.knob (PanelSlot::Skew) && pg.panel.knob (PanelSlot::SkewRand) && pg.panel.stepper (PanelSlot::Shape), "detail page grain window: AMOUNT, AMOUNT rand, SKEW, SKEW rand knobs and the SHAPE stepper");
    }
}


void testSourcePicker()
{
    SourcePicker sp;
    sp.place ({ 10, 10, sp.minSize().w, 56 });
    sp.stepper.setId (1); sp.dropdown.setId (2);
    sp.stepper.state.count = 10; sp.stepper.state.index = 3; sp.stepper.setItem ("SAW");
    std::string why;
    check (sp.sizeOk() && leavesOk (sp, sp.rect(), why), "source picker: fits, stepper and dropdown inside, no overlap" + why);
    Change c = tap (sp, sp.stepper.rect().right() - 10, cy (sp.stepper), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 1 && c.index == 4, "source picker: the stepper: next, id 1");
    c = tap (sp, cx (sp.dropdown), cy (sp.dropdown), 100.0);
    check (c.kind == ChangeKind::Request && c.id == 2, "source picker: the dropdown: request, id 2");
    c = sp.turn (-1);
    check (c.kind == ChangeKind::Index && c.id == 1 && c.index == 2, "source picker: turn goes to the stepper");
    drawChecks (sp, "source picker", { 10, 10, sp.minSize().w, 56 }, nullptr);
}

void testModulationsPage()
{
    ModInput m;
    m.add (7, "LFO 2", 0.3);
    m.add (8, "ENV 1", -0.5, true);
    m.add (9, "MACRO1", 0.0, false, true);
    ModulationsPage pg;
    pg.load (m, "DETUNE");
    pg.place ({ 56, 14, 1168, 600 });
    for (int i = 0; i < 3; ++i)
        pg.setRowIds (i, 100 + 10 * i, 101 + 10 * i, 102 + 10 * i, 103 + 10 * i);
    pg.back.setId (1);
    check (pg.numRows() == 3 && pg.sizeOk(), "modulations page: one row per entry (3), fits");
    check (std::fabs (pg.rows[0].depth.state.value - 0.65) < 1e-9 && std::fabs (pg.rows[1].depth.state.value - 0.25) < 1e-9 && pg.rows[1].polarity.state.on && ! pg.rows[0].polarity.state.on && ! pg.rows[2].bypass.state.on && pg.rows[0].bypass.state.on && std::string (pg.rows[1].polarity.state.label) == "BI" && std::string (pg.rows[0].polarity.state.label) == "UNI" && std::string (pg.rows[2].bypass.state.label) == "OFF" && std::string (pg.rows[0].bypass.state.label) == "ON" && std::string (pg.rows[1].name) == "ENV 1", "modulations page: the rows show the entries (depth as (d + 1) / 2, polarity UNI / BI, ON / OFF, name)");
    std::string why;
    check (leavesOk (pg, pg.rect(), why), "modulations page: every leaf inside, no overlap" + why);
    bool rowsApart = true;
    for (int i = 1; i < 3; ++i)
        rowsApart = rowsApart && pg.rows[i - 1].depth.rect().bottom() <= pg.rows[i].depth.rect().y;
    check (rowsApart, "modulations page: the rows one under the other");
    Change c = hdrag (pg, cy (pg.rows[1].depth), cx (pg.rows[1].depth) - 40, cx (pg.rows[1].depth) + 40);
    check (c.kind == ChangeKind::Value && c.id == 110 && c.value > pg.rows[1].depth.state.value, "modulations page: the depth bar of row 2 (a horizontal drag to the right raises it): id 110");
    pg.touchUp (0, 0);
    c = tap (pg, cx (pg.rows[1].polarity), cy (pg.rows[1].polarity), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 111 && c.index == 0, "modulations page: the polarity toggle of row 2: off (unidirectional), id 111");
    c = tap (pg, cx (pg.rows[2].bypass), cy (pg.rows[2].bypass), 100.0);
    check (c.kind == ChangeKind::Index && c.id == 122 && c.index == 1, "modulations page: the on / off toggle of row 3 (it is off now): on, id 122");
    c = tap (pg, cx (pg.rows[0].remove), cy (pg.rows[0].remove), 200.0);
    check (c.kind == ChangeKind::Request && c.id == 103, "modulations page: the remove button of row 1: { request, id 103 }");
    c = tap (pg, cx (pg.back), cy (pg.back), 300.0);
    check (c.kind == ChangeKind::Request && c.id == 1, "modulations page: back");
    check (! tap (pg, pg.rect().x + 600, pg.rect().y + 400, 400.0), "modulations page: below the rows: nothing");
    drawChecks (pg, "modulations page", { 56, 14, 1168, 600 }, "modulations.png");
    // the last row: add a source
    pg.addSource.setId (200); pg.add.setId (201);
    pg.addSource.state.count = 3; pg.addSource.state.index = 1; pg.addSource.setItem ("LFO 3");
    pg.sourceIds[0] = 31; pg.sourceIds[1] = 32; pg.sourceIds[2] = 33;
    check (pg.addRowRect().y == pg.rows[2].depth.rect().bottom() + 0 + 0 || pg.addRowRect().y >= pg.rows[2].depth.rect().bottom(), "modulations page: the add row is under the last entry");
    c = tap (pg, cx (pg.add), cy (pg.add), 500.0);
    check (c.kind == ChangeKind::AddRequest && c.id == 201 && c.index == 32, "modulations page: the add button: { add requested, source id 32 (the item the stepper shows) }");
    c = tap (pg, pg.addSource.rect().right() - 10, cy (pg.addSource), 600.0);
    check (c.kind == ChangeKind::Index && c.id == 200 && c.index == 2, "modulations page: the add stepper over the sources of the owner: id 200");
    drawChecks (pg, "modulations page with the add row", { 56, 14, 1168, 600 }, nullptr);
    // the Q group of the row touched last: [depth, polarity, bypass, -]
    pg.load (m, "DETUNE");
    m.numMods = 3;
    pg.load (m, "DETUNE");
    pg.place ({ 56, 14, 1168, 600 });
    for (int i = 0; i < 3; ++i)
        pg.setRowIds (i, 100 + 10 * i, 101 + 10 * i, 102 + 10 * i, 103 + 10 * i);
    check (pg.activeRow() == 0 && pg.group.active() && pg.group.member (0) == &pg.rows[0].depth && pg.group.member (3) == nullptr, "modulations page: after the load the first row is the active Q group [depth, polarity, bypass, -]");
    tap (pg, cx (pg.rows[2].polarity), cy (pg.rows[2].polarity), 1000.0);
    check (pg.activeRow() == 2 && pg.group.member (0) == &pg.rows[2].depth && pg.group.member (1) == &pg.rows[2].polarity && pg.group.member (2) == &pg.rows[2].bypass && ! pg.rows[0].depth.qFrame() && pg.rows[2].depth.qIndex() == 1, "modulations page: a touch on a row makes it the active group (the previous row is unmarked)");
    pg.rows[2].depth.state.value = 0.5;
    c = pg.turnQ (0, 0.1);
    check (c.kind == ChangeKind::Value && c.id == 120 && std::fabs (c.value - 0.6) < 1e-9, "modulations page: the first Q-Link turns the depth of the active row: id 120");
    pg.rows[2].polarity.state.on = false;
    c = pg.turnQ (1, 1);
    check (c.kind == ChangeKind::Index && c.id == 121 && c.index == 1, "modulations page: the second Q-Link switches the polarity of the active row: id 121");
    check (! pg.turnQ (3, 1), "modulations page: the fourth place of the group is empty");
    Component* allLeaves[64];
    const int nl = pg.collectLeaves (allLeaves, 0, 64);
    check (pg.group.check (allLeaves, nl) == 0, "modulations page: the row's group is adjacent (the remove button and the other rows lie outside the union)");
    Scene sgp (1280, 628, kBackgroundRgba);
    pg.group.draw (sgp.builder());
    check (sgp.numOps() >= 2 && sgp.data->ops[1].op == gpu::kOpCellBegin && sgp.data->ops[2].op == gpu::kOpRRect && pg.group.rect().y == pg.rows[2].depth.rect().y, "modulations page: the group frame is drawn around the three controls of the active row");
    m.numMods = 0;
    pg.load (m, "EMPTY");
    check (pg.numRows() == 0, "modulations page: no entries: no rows");

    const Rect tab { 0, 110, 1280, 628 };
    ModInput first;
    for (int i = 0; i < ModulationsPage::kPerPage; ++i)
        first.add (i + 1, "ENV", 0.15);
    pg.load (first, "CUTOFF", 10, 0);
    pg.place (tab);
    pg.page.setId (202);
    const auto& last = pg.rows[ModulationsPage::kPerPage - 1];
    check (pg.numRows() == 6 && pg.sizeOk() && tab.contains (pg.add.rect())
           && last.remove.rect().h == 56 && last.depth.rect().y == pg.rowY (5) + 16,
           "modulations page: six full rows retain the Q lane and a usable ADD below them on the 628px tab");
    c = tap (pg, pg.page.rect().right() - 10, cy (pg.page), 1100.0);
    check (c.kind == ChangeKind::Index && c.id == 202 && c.index == 1,
           "modulations page: stock fallback pager requests the remaining routes");
    ModInput second;
    for (int i = 0; i < 4; ++i)
        second.add (7 + i, "LFO", 0.25);
    pg.load (second, "CUTOFF", 10, 1);
    check (pg.page.state.index == 1 && pg.numRows() == 4 && pg.activeRow() == 0 && pg.rows[3].source == 10,
           "modulations page: second page presents routes seven through ten and resets the active row");

    pg.setNativeControls (true);
    int row = -1;
    check (pg.nativeHit (cx (pg.rows[3].depth), cy (pg.rows[3].depth), &row) && row == 3
           && ! pg.nativeHit (cx (last.depth), cy (last.depth))
           && pg.nativeHit (cx (pg.add), cy (pg.add)) && tab.contains (pg.add.rect())
           && pg.add.rect().y == pg.back.rect().y,
           "modulations page: only visible rows and the native header expose host touch cells");
    check (! tap (pg, cx (pg.rows[3].depth), cy (pg.rows[3].depth), 1200.0),
           "modulations page: native depth has no overlapping fallback handler");
    Scene native (1280, 800, kBackgroundRgba);
    pg.draw (native.builder());
    const auto nativeImage = render (native);
    check ((nativeImage.at (cx (pg.rows[3].depth), cy (pg.rows[3].depth)) >> 24) == 0
           && (nativeImage.at (cx (pg.add), cy (pg.add)) >> 24) == 0
           && (nativeImage.at (cx (last.depth), cy (last.depth)) >> 24) != 0,
           "modulations page: the composited scene clears exactly the active native row and header cells");
    pg.setNativeControls (false);
    c = tap (pg, cx (pg.rows[3].remove), cy (pg.rows[3].remove), 1300.0);
    check (c.kind == ChangeKind::Request && c.id == pg.rows[3].remove.id(),
           "modulations page: fallback owns REMOVE again after native ownership ends");
    pg.load (first, "CUTOFF", 6, 1);
    check (pg.page.state.index == 0 && pg.page.state.count == 1 && pg.numRows() == 6,
           "modulations page: removing the last route on a later page clamps back to the last valid page");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
void testPanel (const PanelPreset& pr, int expectParts, const Rect& area, const std::string& where, bool snapshot)
{
    KnobPanel p;
    p.load (pr);
    p.place (area);
    const std::string nm = std::string ("panel ") + pr.name + " (" + where + ")";
    check (p.numParts() == expectParts, nm + ": " + std::to_string (expectParts) + " parts from the table");
    const Size m = p.minSize();
    check (area.w >= m.w && area.h >= m.h, nm + ": the size is at least the minimum of the preset (" + std::to_string (m.w) + " x " + std::to_string (m.h) + ")");
    bool inside = true, apart = true, fit = true;
    for (int i = 0; i < p.numChildren(); ++i)
    {
        const Component* a = p.child (i);
        inside = inside && p.rect().contains (a->rect());
        fit = fit && a->sizeOk();
        for (int j = i + 1; j < p.numChildren(); ++j)
            apart = apart && ! a->rect().overlaps (p.child (j)->rect());
    }
    check (inside, nm + ": every part inside the panel");
    check (apart, nm + ": no two parts overlap");
    check (fit, nm + ": every part has at least its own minimum size");
    std::string why;
    check (leavesOk (p, p.rect(), why), nm + ": the leaves are inside and do not overlap" + why);
    drawChecks (p, nm, area, snapshot ? (std::string ("panel_") + pr.name + ".png").c_str() : nullptr);
}

void testPanels()
{
    const Rect key61 { 10, 10, 1168, 380 }, large { 10, 10, 1900, 420 };
    testPanel (presets::kWavetable, 7, key61, "Key 61 region", true);
    testPanel (presets::kWavetable, 7, large, "large", false);
    testPanel (presets::kGranular, 9, key61, "Key 61 region", true);
    testPanel (presets::kGranular, 9, large, "large", false);
    testPanel (presets::kNoise, 3, key61, "Key 61 region", true);
    testPanel (presets::kNoise, 3, large, "large", false);
    // the layout of each preset is what the table says
    {
        KnobPanel p (presets::kWavetable);
        p.place (key61);
        const Rect pan = p.cellRect (PanelSlot::Pan), lvl = p.cellRect (PanelSlot::Level), blend = p.cellRect (PanelSlot::Blend), wt = p.cellRect (PanelSlot::WtPos), un = p.cellRect (PanelSlot::Unison), dt = p.cellRect (PanelSlot::Detune), wp = p.cellRect (PanelSlot::Warp);
        check (wt.y == un.y && un.y == dt.y && dt.y == blend.y && wt.x < un.x && un.x < dt.x && dt.x < blend.x, "wavetable: WT POS, the unison box, DETUNE and BLEND in one row");
        check (wp.y >= wt.bottom() && wp.x == wt.x && wp.right() >= blend.right(), "wavetable: the warp pair below, across the row");
        check (pan.x == lvl.x && pan.y < lvl.y && pan.x - blend.right() > KnobPanel::kSeparatorGap - 1 && p.separatorX() > blend.right() && p.separatorX() < pan.x, "wavetable: PAN over LEVEL in a separated right column (a line between)");
        check (p.knob (PanelSlot::Pan) && p.knob (PanelSlot::Pan)->state.bipolar && ! p.knob (PanelSlot::Level)->state.bipolar, "wavetable: PAN is bipolar, LEVEL is not");
        check (p.unison() && p.warp() && ! p.menu() && ! p.pair (PanelSlot::Length), "wavetable: the parts of the other presets are not there");
    }
    {
        KnobPanel p (presets::kGranular);
        p.place (key61);
        const Rect w = p.cellRect (PanelSlot::WarpMenu), sc = p.cellRect (PanelSlot::Scan), de = p.cellRect (PanelSlot::Dens), le = p.cellRect (PanelSlot::Length), pa = p.cellRect (PanelSlot::Pan), lv = p.cellRect (PanelSlot::Level), of = p.cellRect (PanelSlot::Offset), di = p.cellRect (PanelSlot::Dir), pi = p.cellRect (PanelSlot::Pitch);
        check (w.y == sc.y && sc.y == de.y && de.y == le.y && le.y == pa.y && pa.y == lv.y && w.x < sc.x && sc.x < de.x && de.x < le.x && le.x < pa.x && pa.x < lv.x, "granular: WARP, SCAN, DENS, LENGTH, PAN, LEVEL in the first row");
        check (of.y == di.y && di.y == pi.y && of.y > w.y && of.x == w.x && di.x == sc.x && pi.x == de.x, "granular: OFFSET, DIR, PITCH in the second row under WARP, SCAN, DENS");
        const KnobRandPair* lp = p.pair (PanelSlot::Length);
        check (lp && lp->rand.rect().y > lp->main.rect().y && lp->rand.rect().x >= le.x && lp->rand.rect().right() <= le.right() && le.bottom() > of.bottom() - 1, "granular: the RAND knob under LENGTH (a pair over both rows)");
        check (p.menu() && ! p.unison() && ! p.warp() && p.pair (PanelSlot::Pan)->main.state.bipolar, "granular: the WARP menu button, three pairs, PAN bipolar");
    }
    {
        KnobPanel p (presets::kNoise);
        p.place (key61);
        const Rect f = p.cellRect (PanelSlot::Filter), pa = p.cellRect (PanelSlot::Pan), lv = p.cellRect (PanelSlot::Level);
        check (f.y == pa.y && pa.y == lv.y && f.x < pa.x && pa.x < lv.x && p.numChildren() == 3 && p.separatorX() < 0, "noise: FILTER, PAN, LEVEL in a row, no separator");
    }
    // ids, routing, turn through a panel
    {
        KnobPanel p (presets::kWavetable);
        p.place ({ 10, 10, 1168, 380 });
        p.knob (PanelSlot::Pan)->setId (77);
        Knob* pan = p.knob (PanelSlot::Pan);
        pan->state.defaultValue = 0.5; pan->state.value = 0.9;
        tap (p, cx (*pan), cy (*pan), 0.0);
        Change c = tap (p, cx (*pan), cy (*pan), 150.0);
        check (c.kind == ChangeKind::Value && c.id == 77 && std::fabs (c.value - 0.5) < 1e-9, "panel: a double tap on PAN gives its default with the id the owner gave");
        p.unison()->count.setId (78);
        c = tap (p, cx (p.unison()->count), p.unison()->count.rect().y + 5, 1000.0);
        check (c.kind == ChangeKind::Index && c.id == 78, "panel: the unison stepper's change has its id");
        p.touchDown (cx (*pan), cy (*pan) + 30, 3000.0);
        c = p.touchMove (-100, -100);
        check (c.kind == ChangeKind::Value && c.id == 77, "panel: the gesture of a knob goes on outside the panel");
        p.touchUp (-100, -100);
        check (! p.touchDown (5, 5, 0.0) && ! p.turn (0.1), "panel: a touch outside is ignored, turn has no primary child");
        // reload: another preset, the old parts are gone
        p.load (presets::kNoise);
        p.place ({ 10, 10, 800, 240 });
        check (p.numParts() == 3 && ! p.unison() && p.knob (PanelSlot::Filter) && p.knob (PanelSlot::Pan)->id() == 0, "panel: loading another preset replaces the parts (ids back to 0)");
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The Q chips and the frame in cache cells: the helper repaints a cell by its rectangle, so every op of a chip and of the frame must lie inside a cell that covers it (one cell per chip, one for the frame).
struct CellInfo { float x0, y0, x1, y1; uint32_t hash; };
bool sameRectCell (const CellInfo& a, float x0, float y0, float x1, float y1) { return std::fabs (a.x0 - x0) < 0.01f && std::fabs (a.y0 - y0) < 0.01f && std::fabs (a.x1 - x1) < 0.01f && std::fabs (a.y1 - y1) < 0.01f; }

/** The cells of a scene; `bad` counts the drawing ops that are outside a cell or leave the cell they are in. */
std::vector<CellInfo> sceneCells (const gpu::SceneData& d, int& bad, int& ops)
{
    std::vector<CellInfo> cells;
    bool in = false;
    CellInfo cur {};
    bad = 0;
    ops = 0;
    for (uint32_t i = 0; i < d.numOps; ++i)
    {
        const gpu::SceneOp& o = d.ops[i];
        if (o.op == gpu::kOpCellBegin) { in = true; cur = { o.f[0], o.f[1], o.f[2], o.f[3], o.rgba }; cells.push_back (cur); continue; }
        if (o.op == gpu::kOpCellEnd) { in = false; continue; }
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        switch (o.op)
        {
            case gpu::kOpRect: case gpu::kOpRRect: case gpu::kOpFrame: x0 = o.f[0]; y0 = o.f[1]; x1 = o.f[2]; y1 = o.f[3]; break;
            case gpu::kOpText:
            {
                const size_t off = static_cast<size_t> (o.f[3]), len = static_cast<size_t> (o.f[4]);
                const float w = sdf::textWidth (d.text + off, static_cast<int> (len), o.f[2], gpu::bitsFloat (o.rgba2));
                x0 = o.f[0]; x1 = o.f[0] + w; y0 = o.f[1] - sdf::capHeight (o.f[2]); y1 = o.f[1] + 0.2f * o.f[2];
                break;
            }
            case gpu::kOpStyle: case gpu::kOpPaint: case gpu::kOpNone: continue;
            default: ++bad; continue;   // an op this test does not know
        }
        ++ops;
        if (! in || x0 < cur.x0 || y0 < cur.y0 || x1 > cur.x1 || y1 > cur.y1)
            ++bad;
    }
    return cells;
}

struct GroupRig
{
    Knob k[4];
    QGroup g;
    Rect m[4];
    Rect frame;
    GroupRig (const Rect r[4], bool frameless)
    {
        for (int i = 0; i < 4; ++i)
        {
            m[i] = r[i];
            k[i].place (r[i]);
            g.set (i, &k[i]);
        }
        g.setFrameless (frameless);
        frame = g.rect();
    }
    GroupRig (const GroupRig&) = delete;
    GroupRig& operator= (const GroupRig&) = delete;
};

void drawGroups (Scene& sc, const GroupRig* const* rigs, int n)
{
    for (int i = 0; i < n; ++i)
    {
        if (rigs[i]->g.active())
            rigs[i]->g.draw (sc.builder());
        else
            rigs[i]->g.drawIdle (sc.builder());
    }
}

void testQGroupCells()
{
    struct Shape { const char* name; Rect r[4]; bool frameless; };
    const Shape shapes[] = {
        { "row", { { 20, 60, 140, 100 }, { 170, 60, 140, 100 }, { 320, 60, 140, 100 }, { 470, 60, 140, 100 } }, false },
        { "column", { { 20, 40, 140, 80 }, { 20, 140, 140, 80 }, { 20, 240, 140, 80 }, { 20, 340, 140, 80 } }, false },
        { "2x2", { { 20, 40, 140, 100 }, { 170, 40, 140, 100 }, { 20, 160, 140, 100 }, { 170, 160, 140, 100 } }, false },
        { "split frameless", { { 20, 40, 140, 100 }, { 260, 40, 140, 100 }, { 20, 330, 140, 100 }, { 260, 330, 140, 100 } }, true },
    };
    auto focusCell = [] (const Rect& frame)
    {
        return Rect::unite (frame, Pen::qMarkerRect (frame)).inset (-2);
    };
    for (const Shape& sh : shapes)
    {
        const std::string nm = std::string ("q group cells (") + sh.name + "): ";
        Rect rb[4];
        for (int i = 0; i < 4; ++i)
            rb[i] = { sh.r[i].x + 640, sh.r[i].y, sh.r[i].w, sh.r[i].h };
        GroupRig a (sh.r, sh.frameless), b (rb, sh.frameless);
        const GroupRig* both[2] = { &a, &b };

        // idle: zero stray chips/ops per owner visual rule
        Scene s0 (1280, 720);
        drawGroups (s0, both, 2);
        int bad = 0, ops = 0;
        const std::vector<CellInfo> idle = sceneCells (*s0.data, bad, ops);
        check (bad == 0, nm + "idle: zero ops outside cell (" + std::to_string (bad) + " outside)");
        check (idle.empty(), nm + "idle: zero numbered chips per owner visual rule");

        // A active: the frame and unnumbered Q marker in a cell of its own
        a.g.setActive (true);
        Scene s1 (1280, 720);
        drawGroups (s1, both, 2);
        const std::vector<CellInfo> actA = sceneCells (*s1.data, bad, ops);
        check (bad == 0 && (sh.frameless ? ops == 0 : ops > 0), nm + "active: every op of the frame lies inside its cell (" + std::to_string (bad) + " outside)");
        const int wantA = sh.frameless ? 0 : 1;
        check (static_cast<int> (actA.size()) == wantA, nm + "active: " + std::to_string (wantA) + " cell (frame with unnumbered Q marker), got " + std::to_string (actA.size()));
        bool frameCell = false;
        const Rect expected = focusCell (a.frame);
        for (const CellInfo& c : actA)
            frameCell = frameCell || sameRectCell (c, static_cast<float> (expected.x), static_cast<float> (expected.y),
                                                 static_cast<float> (expected.right()), static_cast<float> (expected.bottom()));
        check (frameCell == ! sh.frameless, nm + "a single cell contains the frame and marker");

        // the cells that changed between two scenes (rectangle + hash), as rectangles
        auto changed = [] (const std::vector<CellInfo>& p, const std::vector<CellInfo>& q)
        {
            // what layerDamage (cell_plan.h) takes as damage, before it merges the overlapping boxes: the cells of one scene that are not in the other (rectangle and hash), as the set of their rectangles
            std::vector<ov::gpu::CellKey> kp, kq;
            for (const CellInfo& c : p) kp.push_back ({ static_cast<int> (c.x0), static_cast<int> (c.y0), static_cast<int> (c.x1), static_cast<int> (c.y1), c.hash });
            for (const CellInfo& c : q) kq.push_back ({ static_cast<int> (c.x0), static_cast<int> (c.y0), static_cast<int> (c.x1), static_cast<int> (c.y1), c.hash });
            std::vector<Rect> out;
            auto add = [&out] (const ov::gpu::CellKey& k)
            {
                const Rect r { k.x0, k.y0, k.x1 - k.x0, k.y1 - k.y0 };
                for (const Rect& o : out)
                    if (o == r)
                        return;
                out.push_back (r);
            };
            for (const auto& k : kq) { bool f = false; for (const auto& o : kp) f = f || o == k; if (! f) add (k); }
            for (const auto& k : kp) { bool f = false; for (const auto& o : kq) f = f || o == k; if (! f) add (k); }
            std::vector<ov::gpu::DamageRect> dmg;   // and the real function agrees that something is damaged
            ov::gpu::layerDamage (kp, kq, dmg, 1000);
            if (out.empty() != dmg.empty())
                out.push_back ({ -1, -1, 0, 0 });
            return out;
        };
        auto expectChanged = [&] (const std::vector<Rect>& got, const GroupRig* const* rigs, int n, const std::string& what)
        {
            std::vector<Rect> want;
            for (int r = 0; r < n; ++r)
            {
                if (! sh.frameless)
                    want.push_back (focusCell (rigs[r]->frame));
            }
            bool same = got.size() == want.size();
            for (const Rect& w : want)
            {
                bool in = false;
                for (const Rect& g : got)
                    in = in || g == w;
                same = same && in;
            }
            check (same, nm + what + ": exactly the cells of the frame of the group(s) concerned changed (" + std::to_string (got.size()) + " cells, expected " + std::to_string (want.size()) + ")");
        };
        const GroupRig* justA[1] = { &a };
        expectChanged (changed (idle, actA), justA, 1, "activating a group");

        // B instead of A
        a.g.setActive (false);
        b.g.setActive (true);
        Scene s2 (1280, 720);
        drawGroups (s2, both, 2);
        const std::vector<CellInfo> actB = sceneCells (*s2.data, bad, ops);
        check (bad == 0, nm + "the other group active: every op inside its cell");
        expectChanged (changed (actA, actB), both, 2, "moving the active group to the other quad");

        // deactivating
        b.g.setActive (false);
        Scene s3 (1280, 720);
        drawGroups (s3, both, 2);
        const std::vector<CellInfo> idle2 = sceneCells (*s3.data, bad, ops);
        const GroupRig* justB[1] = { &b };
        expectChanged (changed (actB, idle2), justB, 1, "deactivating a group");
        check (idle2.size() == idle.size() && changed (idle, idle2).empty(), nm + "after deactivating, the scene is the idle scene again (same cells)");
    }
    // mapped row members (zero numbered chips per owner visual rule)
    {
        Knob k[3];
        const Rect r[3] = { { 20, 100, 100, 60 }, { 20, 200, 100, 60 }, { 400, 100, 100, 60 } };
        const Component* mm[3];
        for (int i = 0; i < 3; ++i) { k[i].place (r[i]); mm[i] = &k[i]; }
        Scene s (1280, 720);
        QGroup::drawMappedMembers (s.builder(), 1, mm, 3);
        int bad = 0, ops = 0;
        const std::vector<CellInfo> c = sceneCells (*s.data, bad, ops);
        check (bad == 0 && c.empty(), "q group cells (mapped row members): zero numbered chips per owner rule (" + std::to_string (c.size()) + " cells, " + std::to_string (bad) + " outside)");
    }
}

void testNativePanLevel()
{
    for (const auto inst : { OscInstance::Sub, OscInstance::A, OscInstance::B, OscInstance::C, OscInstance::Noise })
        for (bool enabled : { false, true })
        {
            BasicControls basic;
            basic.assignIds (300);
            basic.setStacked (true);
            basic.configure (inst, OscKind::Wavetable);
            basic.place ({ 8, 350, 404, 200 });
            basic.setEnabled (enabled);
            basic.group.setActive (true);
            Component* leaves[32];
            const int n = basic.collectLeaves (leaves, 0, 32);
            bool oldControl = false;
            for (int i = 0; i < n; ++i)
                oldControl |= leaves[i] == &basic.pan || leaves[i] == &basic.level;
            check (! oldControl, "native Pan/Level are absent from overlay leaves");
            for (const auto* control : { &basic.pan, &basic.level })
            {
                const Rect r = control->rect();
                check (r.w >= 56 && r.h >= 56, "native Pan/Level geometry retained");
                check (! drag (basic, r.x + r.w / 2, r.y + r.h / 2, r.y + 4), "native Pan/Level have no overlay drag");
                basic.touchUp (r.x + 4, r.y + 4);
                Change change;
                check (! basic.wheelOver (r.x + 4, r.y + 4, 1, change), "native Pan/Level have no overlay wheel");
            }
            check (basic.group.member (2) == &basic.pan && basic.group.member (3) == &basic.level,
                   "native Pan/Level preserve physical Q slots");
            check (static_cast<bool> (basic.group.turn (2, 1.0)) == enabled,
                   "native PAN Q adapter follows module enable state");
        }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
void testQGroup()
{
    BasicControls basic;
    basic.assignIds (300);
    basic.place ({ 56, 50, 1168, 100 });
    basic.source.stepper.state.count = 10;
    basic.source.stepper.state.index = 7;
    QGroup& group = basic.group;
    group.setActive (true);
    check (group.active() && group.numMembers() == 4, "q group: Basic is active with four members");
    Component* const expected[] = { &basic.selector, &basic.source, &basic.pan, &basic.level };
    Rect bounds;
    for (int i = 0; i < 4; ++i)
    {
        bounds = Rect::unite (bounds, expected[i]->rect());
        check (group.member (i) == expected[i] && expected[i]->qFrame() && expected[i]->qIndex() == i + 1,
               "q group: Basic member " + std::to_string (i + 1) + " has the expected target and marks");
    }
    check (group.rect() == bounds, "q group: the rectangle of the group is the union of its members");

    KnobPanel panel;
    panel.load (presets::kWavetable);
    panel.place ({ 56, 350, 1168, 260 });
    UnisonBox& unison = *panel.unison();
    unison.count.setId (400);
    WarpPair& warp = *panel.warp();
    QGroup voice;
    voice.set (0, panel.knob (PanelSlot::WtPos));
    voice.set (1, &unison);
    voice.set (2, panel.knob (PanelSlot::Detune));
    voice.set (3, panel.knob (PanelSlot::Blend));
    QGroup warps;
    warps.set (0, &warp.first.type);
    warps.set (1, &warp.first.amount);
    warps.set (2, &warp.second.type);
    warps.set (3, &warp.second.amount);
    Component* all[96];
    const int basicLeaves = basic.collectLeaves (all, 0, 96);
    int n = panel.collectLeaves (all, basicLeaves, 96);
    const Component* offender = nullptr;
    check (group.check (all, n) == 0, "q group: [type, source, PAN, LEVEL] contains only its members");
    check (voice.check (all, n) == 0, "q group: the unison box includes the gear");
    check (warps.check (all, n) == 0, "q group: the four warp controls are adjacent");
    voice.set (1, &unison.count);
    check (voice.check (all, n, &offender) == 1 && offender == &unison.gear,
           "q group: using only the unison count leaves the gear as an offender");
    voice.set (1, &unison);
    QGroup far;
    far.set (0, &basic.selector);
    far.set (1, panel.knob (PanelSlot::Blend));
    check (far.check (all, n) > 3, "q group: the span between distant members contains other controls");

    warp.first.setHasVar (true);
    panel.place (panel.rect());
    n = panel.collectLeaves (all, basicLeaves, 96);
    check (warps.check (all, n, &offender) == 1 && offender == &warp.first.var,
           "q group: the warp variant between the knob and stepper is reported");

    Change c = group.turn (0, 1);
    check (c.kind == ChangeKind::Index && c.id == basic.selector.id() && c.index == 1, "q group: slot 1 selects the next oscillator type");
    c = group.turn (1, -3);
    check (c.kind == ChangeKind::Index && c.id == basic.source.stepper.id() && c.index == 4, "q group: slot 2 routes to the source stepper");
    c = group.turn (2, 0.25);
    check (c.kind == ChangeKind::Value && c.id == basic.pan.id() && std::fabs (c.value - 0.75) < 1e-9, "q group: slot 3 increases PAN");
    basic.level.state.value = 1.0;
    check (! group.turn (3, 0.5), "q group: LEVEL at maximum is unchanged");
    check (! group.turn (4, 1) && ! group.turn (-1, 1), "q group: indices outside the four slots are ignored");
    check (! voice.turn (1, 1) && unison.qIndex() == 0 && ! unison.qFrame(), "q group: an inactive group skips input and marks");
    voice.setActive (true);
    c = voice.turn (1, 1);
    check (c.kind == ChangeKind::Index && c.id == unison.count.id(), "q group: the unison box routes turn to its count stepper");
    voice.setActive (false);
    check (! unison.qFrame() && unison.qIndex() == 0, "q group: deactivating clears the marks");
    QGroup partial;
    partial.set (1, &unison);
    partial.setActive (true);
    check (! partial.turn (0, 1) && partial.numMembers() == 1 && partial.rect() == unison.rect() && ! partial.set (4, nullptr),
           "q group: an empty slot skips input and a sole member sets the rectangle");
    partial.setActive (false);

    // the drawing: ONE frame around the union, inside it
    Scene s (1280, 628, kBackgroundRgba);
    group.draw (s.builder());
    check (s.numOps() >= 2 && s.data->ops[1].op == gpu::kOpCellBegin && s.data->ops[2].op == gpu::kOpRRect, "q group: the group draws one rounded outline op (after the clear), then the index marks of its members");
    const Image im = render (s);
    check (drawnOutside (im, { group.rect().x, group.rect().y - 18, group.rect().w, group.rect().h + 18 }, backgroundPixel()) == 0, "q group: the frame is inside the union rectangle; the index marks sit on its top edge (at most 18 px above it) or inside a member");
    const Rect r = group.rect();
    check (colourNear (im, static_cast<float> (r.x), static_cast<float> (r.y + r.h / 2), 0, colour::q(), 4) && colourNear (im, static_cast<float> (r.x + r.w / 2), static_cast<float> (r.bottom() - 1), 0, colour::q(), 4), "q group: the frame has the colour of the Q-Link focus at the edge");
    Scene s1 (1280, 628, kBackgroundRgba);
    voice.draw (s1.builder());
    check (s1.numOps() == 1, "q group: a group that is not active draws nothing");
    // the members show their index inside their rectangles
    Scene sm (1280, 628, kBackgroundRgba);
    basic.pan.draw (sm.builder());
    check (drawnOutside (render (sm), basic.pan.rect(), backgroundPixel()) == 0, "q group: a member with its index badge draws inside its rectangle");
}

/** The panel's switches are LED toggles (frameless: a dot and the name, no ON / OFF in the label), the toggle of a panel takes the whole cell, the mixer's sliders and destination dropdown have no box. */
void testLedParts()
{
    KnobPanel p;
    p.load (presets::kEnvelopeDetail);
    const ToggleIcon* o = p.toggle (PanelSlot::Override);
    check (o != nullptr && dynamic_cast<const LedToggle*> (o) != nullptr && ! o->state.showState && std::strcmp (o->state.label, "OVERRIDE") == 0, "a panel toggle is a LedToggle named by its function (no ON / OFF written into the label)");
    p.load (presets::kNoiseSampleFull);
    check (p.toggle (PanelSlot::NoiseRand) != nullptr && dynamic_cast<const LedToggle*> (p.toggle (PanelSlot::NoiseRand)) != nullptr && dynamic_cast<const LedToggle*> (p.toggle (PanelSlot::NoiseEnv1)) != nullptr, "the noise sample page: RANDOM, LOOP, BOUNCE and ENV 1 are LED toggles");
    // frameless: nothing is drawn along the left edge of a slider and of the destination dropdown (no box), the dropdown has its hairline
    SliderBar sl ("PAN", true);
    sl.place ({ 10, 10, 126, 56 });
    const Image a = drawComponent (sl, 160, 80);
    RouteDropdown rd ("Filter 1", MenuButton::Icon::Down);
    rd.place ({ 10, 10, 126, 56 });
    const Image b = drawComponent (rd, 160, 80);
    const uint32_t bg = a.at (2, 2);
    bool edgesClear = true;
    for (int y = 16; y < 60; ++y)
        for (int x = 11; x <= 14; ++x)
            edgesClear = edgesClear && a.at (x, y) == bg && b.at (x, y) == bg;
    bool hairline = false;
    for (int x = 30; x < 110; ++x)
        hairline = hairline || b.at (x, 54) != bg;
    check (edgesClear, "the PAN slider and the destination dropdown of a strip have no box");
    check (hairline, "the destination dropdown has its hairline under the name");
}
} // namespace

int main()
{
    testLedParts();
    testRoutePanel();
    testDetailPage();
    testModulationsPage();
    testSourcePicker();
    testUnisonBox();
    testWarp();
    testRandPair();
    testPanels();
    testNativePanLevel();
    testQGroup();
    testQGroupCells();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
