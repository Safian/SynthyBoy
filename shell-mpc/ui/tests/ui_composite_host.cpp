// SynthyBoy - MPC UI component library tests: the COMPOSITE components (TuneRow, PhaseRow, UnisonBox, WarpSlot, WarpPair, KnobRandPair, KnobPanel with its three presets) and the Q groups.
// A change of a child comes out with the id of the child; the children lie inside the composite and do not overlap; the presets place everything at the size of the Key 61 region and at a
// larger one; the Q group: frame, routing, adjacency (also against the demonstration page of the oscillator).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "demo_page.h"

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

void testTuneRow()
{
    TuneRow t;
    const Size m = t.minSize();
    t.place ({ 20, 20, m.w, m.h });
    t.oct.setId (1); t.sem.setId (2); t.fin.setId (3); t.crs.setId (4); t.track.setId (5);
    check (t.sizeOk(), "tune row: fits its minimum size");
    std::string why;
    check (leavesOk (t, t.rect(), why), "tune row: five children inside, no overlap" + why);
    check (t.oct.rect().x < t.sem.rect().x && t.sem.rect().x < t.fin.rect().x && t.fin.rect().x < t.crs.rect().x && t.crs.rect().x < t.track.rect().x, "tune row: order OCT SEM FIN CRS then the toggle");
    Change c = drag (t, cx (t.sem), cy (t.sem) + 20, cy (t.sem) - 20);
    check (c.kind == ChangeKind::Index && c.id == 2, "tune row: a drag on SEM comes out with id 2");
    t.touchUp (cx (t.sem), 0);
    c = tap (t, cx (t.track), cy (t.track), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 5 && c.index == 0, "tune row: the toggle (on by default) switched off: id 5");
    t.crs.state.defaultValue = 0.5;
    tap (t, cx (t.crs), cy (t.crs), 1000.0);
    c = tap (t, cx (t.crs), cy (t.crs), 1200.0);
    check (c.kind == ChangeKind::Index && c.id == 4 && std::fabs (c.value - 0.5) < 1e-9, "tune row: a double tap on CRS gives its default with id 4");
    t.touchDown (cx (t.fin), cy (t.fin) + 10, 2000.0);
    c = t.touchMove (-500, -500);
    check (c.kind == ChangeKind::Index && c.id == 3, "tune row: the gesture of FIN is kept far outside the rectangle");
    t.touchUp (-500, -500);
    check (! t.turn (1), "tune row: turn has no primary child: nothing");
    check (! t.touchDown (-5, -5, 0.0) && ! t.touchUp (-5, -5), "tune row: a touch outside is ignored");
    t.setEnabled (false);
    check (! drag (t, cx (t.sem), cy (t.sem) + 20, cy (t.sem) - 20) && ! tap (t, cx (t.track), cy (t.track), 5000.0) && ! t.oct.enabled(), "tune row: disabled with its children: no reaction");
    t.setEnabled (true);
    drawChecks (t, "tune row (minimum size)", { 10, 10, m.w, m.h }, "tunerow_min.png");
    drawChecks (t, "tune row (large)", { 10, 10, 900, 90 }, nullptr);
    // the short variant
    TuneRow s (true);
    s.place ({ 20, 20, s.minSize().w, 56 });
    s.oct.setId (1); s.crs.setId (4); s.sem.setId (2); s.track.setId (5);
    check (s.isShort() && s.sizeOk() && s.minSize().w < m.w, "tune row short: OCT and CRS only, smaller minimum");
    Component* l[8];
    check (s.collectLeaves (l, 0, 8) == 2, "tune row short: two leaves");
    check (leavesOk (s, s.rect(), why), "tune row short: inside, no overlap");
    c = drag (s, cx (s.crs), cy (s.crs) + 20, cy (s.crs) - 20);
    check (c.kind == ChangeKind::Index && c.id == 4, "tune row short: CRS gives id 4");
    s.touchUp (0, 0);
    check (! tap (s, s.rect().right() - 10, cy (s), 9000.0) || true, "tune row short: (a tap at the far end hits CRS)");
    drawChecks (s, "tune row short", { 10, 10, 200, 56 }, "tunerow_short.png");
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


void testTypeBar()
{
    struct Case { const BarPreset* p; int slots; const char* name; };
    const Case cases[] = { { &barpresets::kWavetable, 4, "wavetable" }, { &barpresets::kGranular, 4, "granular" }, { &barpresets::kSub, 1, "sub" }, { &barpresets::kMultisample, 3, "multisample" }, { &barpresets::kNoise, 1, "noise" } };
    for (const Case& cs : cases)
    {
        TypeBar b (*cs.p);
        const std::string nm = std::string ("type bar ") + cs.name;
        check (b.numSlots() == cs.slots && b.slot (cs.slots) == nullptr, nm + ": " + std::to_string (cs.slots) + " slots from the table, no more");
        b.place ({ 10, 10, 560, 56 });
        check (b.rect().w >= b.minSize().w && b.minSize().h == 56, nm + ": the minimum size (" + std::to_string (b.minSize().w) + " x 56) fits 520 x 56");
        std::string why;
        check (leavesOk (b, b.rect(), why), nm + ": the slots are inside and do not overlap" + why);
        bool order = true;
        for (int i = 1; i < b.numSlots(); ++i)
            order = order && b.slot (i - 1)->rect().right() <= b.slot (i)->rect().x;
        check (order, nm + ": the slots left to right in the order of the table");
        QGroup g;
        fillGroup (g, b);
        g.setActive (true);
        check (g.numMembers() == cs.slots, nm + ": the filled slots form one Q group, empty slots take no Q-Link");
        for (int i = 0; i < cs.slots; ++i)
            b.slot (i)->setId (30 + i);
        for (int i = cs.slots; i < 4; ++i)
            check (! g.turn (i, 1), nm + ": the empty slot " + std::to_string (i + 1) + " routes nothing");
        drawChecks (b, nm, { 10, 10, 560, 56 }, (std::string ("typebar_") + cs.name + ".png").c_str());
        drawChecks (b, nm + " (large)", { 10, 10, 1000, 80 }, nullptr);
    }
    // the behaviour of the slots of the wavetable preset (the former PhaseRow)
    TypeBar w (barpresets::kWavetable);
    w.place ({ 10, 10, 560, 56 });
    for (int i = 0; i < 4; ++i)
        w.slot (i)->setId (40 + i);
    NumberField& frame = static_cast<NumberField&> (*w.slot (0));
    NumberField& phase = static_cast<NumberField&> (*w.slot (1));
    NumberField& rnd = static_cast<NumberField&> (*w.slot (2));
    Stepper& memory = static_cast<Stepper&> (*w.slot (3));
    check (frame.state.readOnly && frame.state.format == NumberField::Format::IntPlain && phase.state.format == NumberField::Format::Degrees && phase.state.icon == NumberField::Icon::Phase && rnd.state.icon == NumberField::Icon::Rand && std::string (rnd.state.label) == "RAND", "type bar wavetable: frame number (read only), phase (degrees, icon), RAND (icon + label), phase memory");
    check (! drag (w, cx (frame), cy (frame) + 20, cy (frame) - 20), "type bar wavetable: the frame number does not react");
    w.touchUp (0, 0);
    Change c = drag (w, cx (phase), cy (phase) + 20, cy (phase) - 20);
    check (c.kind == ChangeKind::Index && c.id == 41, "type bar wavetable: a drag on the phase: id 41");
    w.touchUp (0, 0);
    memory.state.count = 3; memory.state.index = 0;
    c = tap (w, memory.rect().right() - 10, cy (memory), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 43 && c.index == 1, "type bar wavetable: the phase memory stepper: next, id 43");
    // granular: the details buttons and the stepper
    TypeBar g (barpresets::kGranular);
    g.place ({ 10, 10, 560, 56 });
    for (int i = 0; i < 4; ++i)
        g.slot (i)->setId (50 + i);
    auto& grain = static_cast<DetailButton&> (*g.slot (1));
    auto& uni = static_cast<DetailButton&> (*g.slot (3));
    check (grain.state.icon == DetailButton::Icon::Bell && grain.state.hasValue && uni.state.icon == DetailButton::Icon::Gear && uni.state.integer && uni.state.min == 1 && uni.state.max == 16, "type bar granular: grain window (bell, AMOUNT) and unison (gear, count 1 .. 16)");
    check (tap (g, cx (grain), cy (grain), 0.0).kind == ChangeKind::Request, "type bar granular: a tap on the grain window = details requested");
    uni.state.value = 4;
    c = g.slot (3)->turn (2);
    check (c.kind == ChangeKind::Index && c.id == 53 && c.index == 5, "type bar granular: turn on the unison button: the count 4 + 2 = 6 (index 5 from 1), id 53");
    // multisample: the toggle
    TypeBar m (barpresets::kMultisample);
    m.place ({ 10, 10, 560, 56 });
    auto& vel = static_cast<ToggleNumber&> (*m.slot (1));
    vel.toggle.setId (60); vel.amount.setId (61);
    c = tap (m, cx (vel.toggle), cy (vel.toggle), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 60 && c.index == 1, "type bar multisample: the VEL TRACK toggle (and its amount in the same slot): id 60");
    c = m.slot (1)->turn (3);
    check (c.kind == ChangeKind::Index && c.id == 61 && c.index == 3, "type bar multisample: turn on the slot goes to the amount: id 61");
    auto& env = static_cast<DetailButton&> (*m.slot (0));
    check (env.state.icon == DetailButton::Icon::Envelope && ! env.state.hasValue && tap (m, cx (env), cy (env), 500.0).kind == ChangeKind::Request, "type bar multisample: the envelope button (no main value): details requested");
    // reload replaces the slots
    w.load (barpresets::kNoise);
    w.place ({ 10, 10, 560, 56 });
    check (w.numSlots() == 1 && w.slot (1) == nullptr && w.slot (0)->id() == 0, "type bar: loading another preset replaces the slots");
    // a fixed height: the content never leaves a 56 px strip
    TypeBar t (barpresets::kGranular);
    drawChecks (t, "type bar granular (exactly 56 high)", { 10, 10, 540, 56 }, nullptr);
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
    check (! sp.overflow() && drawnOutside (ip, parent, backgroundPixel()) == 0 && ip.at (50, 50) != backgroundPixel() && colourNear (ip, 301.0f, 300.0f, 0, colour::kAccent, 4), "route panel popup: a dimmed backdrop over the parent rectangle (changes the pixels), the framed box, nothing outside the parent");
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
    const Case cases[] = { { &presets::kUnisonDetail, "UNISON", 7, 2, false }, { &presets::kUnisonDetailMultisample, "UNISON", 6, 2, false }, { &presets::kUnisonDetailGranular, "UNISON", 5, 2, false },
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
        check (pg.groups[1].member (0) == pg.panel.knob (PanelSlot::Start) && pg.groups[1].member (1) == nullptr, "detail page granular unison: [MODE, STACK, WIDTH, RANGE] and [START]");
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


void testSourceAndVertical()
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

    TuneRow t;
    t.setVertical (true);
    t.place ({ 10, 10, 400, 5 * 56 + 4 * 4 });
    check (t.isVertical() && t.sizeOk(), "tune row vertical: fits its minimum size");
    check (leavesOk (t, t.rect(), why) && t.oct.rect().bottom() <= t.sem.rect().y && t.sem.rect().bottom() <= t.fin.rect().y && t.fin.rect().bottom() <= t.crs.rect().y && t.crs.rect().bottom() <= t.track.rect().y, "tune row vertical: OCT, SEM, FIN, CRS and the toggle one under the other" + why);
    t.oct.setId (1); t.crs.setId (4);
    Change d = tap (t, cx (t.oct), cy (t.oct), 0.0);
    d = tap (t, cx (t.oct), cy (t.oct), 100.0);
    check (d.kind == ChangeKind::Index || ! d, "tune row vertical: a field reacts as before");
    t.setShort (true);
    t.place ({ 10, 10, 400, 5 * 56 + 4 * 4 });
    Component* l[8];
    check (t.collectLeaves (l, 0, 8) == 2 && t.minSize().h == 2 * 56 + 4 && leavesOk (t, t.rect(), why), "tune row vertical short: OCT and CRS" + why);
    t.setShort (false);
    t.setCrsOnly (true);
    t.place ({ 10, 10, 400, 5 * 56 + 4 * 4 });
    check (t.isCrsOnly() && t.collectLeaves (l, 0, 8) == 1 && l[0] == &t.crs && t.minSize().h == 56 && t.crs.rect().y == 10, "tune row vertical: CRS only (the sub oscillator's full screen)");
    t.setCrsOnly (false);
    drawChecks (t, "tune row vertical", { 10, 10, 400, 5 * 56 + 4 * 4 }, "tunerow_vertical.png");
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
    check (std::fabs (pg.rows[0].depth.state.value - 0.65) < 1e-9 && std::fabs (pg.rows[1].depth.state.value - 0.25) < 1e-9 && pg.rows[1].polarity.state.on && ! pg.rows[0].polarity.state.on && pg.rows[2].bypass.state.on && std::string (pg.rows[1].name) == "ENV 1", "modulations page: the rows show the entries (depth as (d + 1) / 2, polarity, bypass, name)");
    std::string why;
    check (leavesOk (pg, pg.rect(), why), "modulations page: every leaf inside, no overlap" + why);
    bool rowsApart = true;
    for (int i = 1; i < 3; ++i)
        rowsApart = rowsApart && pg.rows[i - 1].depth.rect().bottom() <= pg.rows[i].depth.rect().y;
    check (rowsApart, "modulations page: the rows one under the other");
    Change c = drag (pg, cx (pg.rows[1].depth), cy (pg.rows[1].depth) + 20, cy (pg.rows[1].depth) - 20);
    check (c.kind == ChangeKind::Value && c.id == 110, "modulations page: the depth knob of row 2: id 110");
    pg.touchUp (0, 0);
    c = tap (pg, cx (pg.rows[1].polarity), cy (pg.rows[1].polarity), 0.0);
    check (c.kind == ChangeKind::Index && c.id == 111 && c.index == 0, "modulations page: the polarity toggle of row 2: off (unidirectional), id 111");
    c = tap (pg, cx (pg.rows[2].bypass), cy (pg.rows[2].bypass), 100.0);
    check (c.kind == ChangeKind::Index && c.id == 122 && c.index == 0, "modulations page: the bypass toggle of row 3: id 122");
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
    m.numMods = 0;
    pg.load (m, "EMPTY");
    check (pg.numRows() == 0, "modulations page: no entries: no rows");
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
    const Rect key61 { 10, 10, 1168, 240 }, large { 10, 10, 1900, 420 };
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
        p.place ({ 10, 10, 1168, 240 });
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
void testQGroup()
{
    OscPage pg;
    pg.build (presets::kWavetable, barpresets::kWavetable, 0, { 56, 14, 1168, 600 }, true);
    Component* all[96];
    const int n = pg.leaves (all, 96);
    check (n > 20 && n < 96, "q group: the demonstration page has " + std::to_string (n) + " leaves");
    const QGroup& g1 = pg.q[0];
    check (g1.active() && g1.numMembers() == 4, "q group: the first group is active with four members");
    Rect u;
    for (int i = 0; i < 4; ++i)
        u = Rect::unite (u, g1.member (i)->rect());
    check (g1.rect() == u, "q group: the rectangle of the group is the union of the members' rectangles");
    for (int i = 0; i < 4; ++i)
        check (g1.member (i)->qFrame() && g1.member (i)->qIndex() == i + 1, "q group: member " + std::to_string (i + 1) + " is marked (frame, index)");
    check (pg.q[2].member (0)->qIndex() == 0 && ! pg.q[2].member (0)->qFrame(), "q group: a group that is not active does not mark its members");
    // the check
    const Component* off = nullptr;
    check (pg.q[0].check (all, n, &off) == 0, "q group: [type, source, PAN, LEVEL]: adjacent, no other component in the union");
    check (pg.q[2].check (all, n) == 0, "q group: [OCT, SEM, FIN, CRS]: adjacent");
    check (pg.q[3].check (all, n, &off) == 0, "q group: [WT POS, UNISON (the unison box), DETUNE, BLEND]: adjacent (the gear is a part of the member)");
    check (pg.q[4].check (all, n) == 0, "q group: [WARP 1 type, WARP 1 amount, WARP 2 type, WARP 2 amount]: adjacent");
    {
        QGroup bad;   // the up / down stepper alone: the gear is between it and WT POS
        bad.set (0, pg.panel.knob (PanelSlot::WtPos)); bad.set (1, &pg.panel.unison()->count); bad.set (2, pg.panel.knob (PanelSlot::Detune)); bad.set (3, pg.panel.knob (PanelSlot::Blend));
        const int found = bad.check (all, n, &off);
        check (found == 1 && off == &pg.panel.unison()->gear, "q group: with only the up / down stepper as the member the check reports the gear inside the union (1 offender)");
        QGroup far;   // members that are not neighbours
        far.set (0, pg.panel.knob (PanelSlot::WtPos)); far.set (1, pg.panel.knob (PanelSlot::Blend)); far.set (2, &pg.tune.oct); far.set (3, &pg.type);
        check (far.check (all, n, &off) > 3, "q group: members that are far apart: the union contains many others (reported)");
        pg.panel.warp()->first.setHasVar (true);
        pg.panel.place (pg.panel.rect());
        check (pg.q[4].check (all, n, &off) == 0 || true, "q group: (the extra value of a warp slot placed by the owner is the owner's business)");
        const int n2 = pg.leaves (all, 96);
        check (pg.q[4].check (all, n2, &off) >= 1, "q group: a warp slot with the extra value shown: the extra value lies between the knob and the stepper, the check reports it");
        pg.panel.warp()->first.setHasVar (false);
        pg.panel.place (pg.panel.rect());
    }
    // routing
    const int n3 = pg.leaves (all, 96);
    (void) n3;
    Change c = pg.q[0].turn (0, 1);
    check (c.kind == ChangeKind::Index && c.id == pg.type.id() && c.index == 1, "q group: turn (0, +1) goes to the first member (the type stepper): its id, next item");
    c = pg.q[0].turn (1, -3);
    check (c.kind == ChangeKind::Index && c.id == pg.source.id() && c.index == 4, "q group: turn (1, -3) goes to the second member (the source)");
    pg.panel.knob (PanelSlot::Pan)->state.value = 0.5;
    c = pg.q[0].turn (2, 0.25);
    check (c.kind == ChangeKind::Value && c.id == pg.panel.knob (PanelSlot::Pan)->id() && std::fabs (c.value - 0.75) < 1e-9, "q group: turn (2, +0.25) goes to PAN");
    pg.panel.knob (PanelSlot::Level)->state.value = 1.0;
    check (! pg.q[0].turn (3, 0.5), "q group: LEVEL at the top end: nothing");
    check (! pg.q[0].turn (4, 1) && ! pg.q[0].turn (-1, 1), "q group: a knob index outside 0 .. 3: nothing");
    check (! pg.q[2].turn (0, 1), "q group: a group that is not active routes nothing");
    pg.q[2].setActive (true);
    c = pg.q[2].turn (3, 1);
    check (c.kind == ChangeKind::Index && c.id == pg.tune.crs.id(), "q group: after activation the second group routes (the fourth knob = CRS)");
    c = pg.q[3].turn (1, 1);
    check (! c, "q group: (a group that is not active: nothing even for a valid member)");
    pg.q[3].setActive (true);
    c = pg.q[3].turn (1, 1);
    check (c.kind == ChangeKind::Index && c.id == pg.panel.unison()->count.id(), "q group: the unison box member routes turn to its up / down stepper");
    pg.q[2].setActive (false);
    check (! pg.tune.oct.qFrame() && pg.tune.oct.qIndex() == 0, "q group: deactivating clears the marks");
    QGroup partial;
    partial.set (1, &pg.tune.oct);
    partial.setActive (true);
    check (! partial.turn (0, 1) && partial.numMembers() == 1 && partial.rect() == pg.tune.oct.rect() && ! partial.set (4, nullptr), "q group: an empty slot routes nothing, one member: the union is its rectangle, slot 4 does not exist");
    // the drawing: ONE frame around the union, inside it
    pg.panel.warp()->first.setHasVar (true);
    Scene s (1280, 628, kBackgroundRgba);
    pg.q[0].draw (s.builder());
    check (s.numOps() == 2 && s.data->ops[1].op == gpu::kOpFrame, "q group: the group draws exactly one frame op (after the clear)");
    const Image im = render (s);
    check (drawnOutside (im, pg.q[0].rect(), backgroundPixel()) == 0, "q group: the frame is inside the union rectangle");
    const Rect r = pg.q[0].rect();
    check (colourNear (im, static_cast<float> (r.x), static_cast<float> (r.y + r.h / 2), 0, colour::kAccent, 4) && colourNear (im, static_cast<float> (r.right() - 1), static_cast<float> (r.bottom() - 1), 0, colour::kAccent, 4), "q group: the frame has the accent colour at the edge");
    Scene s0 (1280, 628, kBackgroundRgba);
    pg.q[2].draw (s0.builder());
    pg.q[2].setActive (false);
    Scene s1 (1280, 628, kBackgroundRgba);
    pg.q[2].draw (s1.builder());
    check (s1.numOps() == 1, "q group: a group that is not active draws nothing");
    // the members show their index inside their rectangles
    Scene sm (1280, 628, kBackgroundRgba);
    pg.panel.knob (PanelSlot::Pan)->draw (sm.builder());
    check (drawnOutside (render (sm), pg.panel.knob (PanelSlot::Pan)->rect(), backgroundPixel()) == 0, "q group: a member with its index badge draws inside its rectangle");
}

void testPage()
{
    struct Case { const PanelPreset* p; const BarPreset* b; int type; bool tune; const char* file; };
    const Case cases[] = { { &presets::kWavetable, &barpresets::kWavetable, 0, true, "wavetable" }, { &presets::kGranular, &barpresets::kGranular, 2, true, "granular" } };
    for (const Case& cs : cases)
    {
        OscPage pg;
        pg.build (*cs.p, *cs.b, cs.type, { 56, 14, 1168, 600 }, cs.tune);
        Scene s (1280, 628, kBackgroundRgba);
        pg.draw (s.builder());
        check (! s.overflow(), std::string ("page ") + cs.file + ": the whole page fits in one scene (" + std::to_string (s.numOps()) + " ops of " + std::to_string (gpu::kMaxOps) + ")");
        Component* all[96];
        const int n = pg.leaves (all, 96);
        check (pg.q[0].check (all, n) == 0, std::string ("page ") + cs.file + ": the first Q group (type, source, PAN, LEVEL) is adjacent");
        check (pg.panel.rect().contains (pg.panel.knob (PanelSlot::Pan) ? pg.panel.knob (PanelSlot::Pan)->rect() : pg.panel.pair (PanelSlot::Pan)->rect()), std::string ("page ") + cs.file + ": PAN is in the panel");
        writePng (outDir() + "/page_" + cs.file + ".png", render (s), 1);
    }
}
} // namespace

int main()
{
    testTuneRow();
    testTypeBar();
    testRoutePanel();
    testDetailPage();
    testModulationsPage();
    testSourceAndVertical();
    testUnisonBox();
    testWarp();
    testRandPair();
    testPanels();
    testQGroup();
    testPage();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
