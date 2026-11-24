// SynthyBoy - MPC overlay tests: the selector of the merged Env / LFO tabs (ui::UsedSelector, uibind::usedSourcesOf / selectorCandidates / scanUsed, the view state helpers, the chunk member "mpcTabView").
// Step 1 of docs/ui-map/env-filter-lfo-plan.md. Host only; pictures of the selector alone go to UI_OUT (used_selector_*.png).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "demo_common.h"
#include "../ui_bind.h"
#include "../../tab_view.h"

using namespace uit;
namespace ui = synthyboy_mpc::ui;
namespace ub = synthyboy_mpc::overlay::uibind;
using synthyboy_mpc::overlay::IInstanceView;

namespace
{
std::string cellsText (const ui::UsedSelector& s)
{
    int c[ui::UsedSelector::kMaxCells];
    const int n = s.cells (c, ui::UsedSelector::kMaxCells);
    std::string t;
    for (int k = 0; k < n; ++k)
        t += std::string (k ? " " : "") + (c[k] == ui::UsedSelector::kPlus ? "+" : "[" + std::string (s.labelOf (c[k])) + "]");
    return t;
}
std::string choicesText (const ui::UsedSelector& s)
{
    int c[ui::UsedSelector::kMaxChoices];
    const int n = s.plusChoices (c, ui::UsedSelector::kMaxChoices);
    std::string t;
    for (int k = 0; k < n; ++k)
        t += std::string (k ? " " : "") + s.labelOf (c[k]);
    return t;
}
void fill (ui::UsedSelector& sel, ub::SelKind kind, const demo::Inst& inst) { ub::fillSelector (kind, ub::scanUsed (inst), inst, sel); }
Change tap (ui::Component& c, const Rect& r, double t = 0.0)
{
    const Change d = c.touchDown (r.x + r.w / 2, r.y + r.h / 2, t);
    const Change u = c.touchUp (r.x + r.w / 2, r.y + r.h / 2);
    return u ? u : d;
}
void picture (ui::UsedSelector& s, const char* name)
{
    s.place ({ 8, 8, std::max (1, s.numCells()) * 56, 56 });
    Scene sc (s.rect().w + 16, 72, kBackgroundRgba);
    s.draw (sc.builder());
    const Image im = render (sc);
    check (! sc.overflow() && drawnOutside (im, { 4, 4, s.rect().w + 8, 64 }, backgroundPixel()) == 0, std::string (name) + ": drawn inside its rectangle (" + std::to_string (sc.numOps()) + " ops)");
    writePng (outDir() + "/" + name, im, 1);
}
constexpr int kEnvS = 0, kLfoS = 6;   // source id = kEnvS + n for ENV n; LFO n = 6 + n (1 .. 8)
}

int main()
{
    // ---- an empty preset: ENV: [1] +, LFO: [1] +
    {
        demo::Inst inst;
        ui::UsedSelector env, lfo;
        fill (env, ub::SelKind::Env, inst);
        fill (lfo, ub::SelKind::Lfo, inst);
        check (cellsText (env) == "[1] +", "empty preset, Env: ENV 1 and '+' (" + cellsText (env) + ")");
        check (cellsText (lfo) == "[1] +", "empty preset, LFO: 1 and '+' (" + cellsText (lfo) + ")");
        check (choicesText (env) == "2", "Env '+' offers the lowest free envelope: 2 (" + choicesText (env) + ")");
        check (choicesText (lfo) == "2 R1", "LFO '+' offers the lowest free LFO and the lowest free random LFO: 2 and R1 (" + choicesText (lfo) + ")");
        picture (lfo, "used_selector_empty.png");
        // ENV 1 is always shown, even when another envelope is selected and ENV 1 is not routed
        ub::chooseEnv (inst, 3);
        fill (env, ub::SelKind::Env, inst);
        check (cellsText (env) == "[1] [3] +", "Env 3 selected and unused: shown next to the always shown ENV 1 (" + cellsText (env) + ")");
    }
    // ---- partly used: a slot's source or the aux of a slot that has a source and a destination
    {
        demo::Inst inst;
        inst.mod (1, kEnvS + 2, "master.volume", 20.0f);
        inst.mod (2, kEnvS + 5, "master.volume", 20.0f, false, true);   // bypassed: still assigned
        inst.mod (3, kLfoS + 1, "master.volume", 20.0f);
        inst.mod (4, kLfoS + 2, "master.volume", 20.0f);
        inst.mod (5, kLfoS + 5, "master.volume", 20.0f);
        ui::UsedSelector env, lfo;
        ub::chooseEnv (inst, 2);
        fill (env, ub::SelKind::Env, inst);
        fill (lfo, ub::SelKind::Lfo, inst);
        check (cellsText (env) == "[1] [2] [5] +", "partly used Env {2, 5} (5 bypassed), selected 2: [1] [2] [5] + (" + cellsText (env) + ")");
        check (cellsText (lfo) == "[1] [2] [5] +", "partly used LFO {1, 2, 5}: [1] [2] [5] + (" + cellsText (lfo) + ")");
        check (choicesText (lfo) == "3 R1", "LFO '+' offers 3 and R1 (" + choicesText (lfo) + ")");
        picture (lfo, "used_selector_partly.png");
        ub::chooseEnv (inst, 3);
        fill (env, ub::SelKind::Env, inst);
        check (cellsText (env) == "[1] [2] [3] [5] +", "the selected unused Env 3 is shown in number order: [1] [2] [3] [5] + (" + cellsText (env) + ")");
        ub::chooseEnv (inst, 5);
        fill (env, ub::SelKind::Env, inst);
        check (cellsText (env) == "[1] [2] [5] +", "selecting another one makes the unused one disappear again (" + cellsText (env) + ")");
    }
    // ---- the aux source counts only for a slot that has a source and a destination
    {
        demo::Inst inst;
        inst.set ("mod01.source", 0.0f);                       // no source: its aux does not count (the slot is free)
        inst.set ("mod01.aux_source", static_cast<float> (kLfoS + 3));
        inst.set ("mod02.source", static_cast<float> (kLfoS + 4)); inst.set ("mod02.destination", 0.0f);   // no destination: not assigned
        inst.set ("mod02.aux_source", static_cast<float> (kLfoS + 5));
        inst.mod (3, kEnvS + 2, "master.volume", 10.0f);
        inst.set ("mod03.aux_source", static_cast<float> (kLfoS + 6));   // a real slot: its source and its aux count
        ui::UsedSelector lfo, env;
        fill (lfo, ub::SelKind::Lfo, inst);
        fill (env, ub::SelKind::Env, inst);
        check (cellsText (lfo) == "[1] [6] +", "aux of a slot with source and destination counts (LFO 6); the aux of an empty slot and a slot without destination do not (" + cellsText (lfo) + ")");
        check (cellsText (env) == "[1] [2] +", "the source of that slot counts (ENV 2) (" + cellsText (env) + ")");
    }
    // ---- all used: no '+'
    {
        demo::Inst inst;
        int n = 1;
        for (int e = 1; e <= 6; ++e) inst.mod (n++, kEnvS + e, "master.volume", 10.0f);
        for (int l = 1; l <= 8; ++l) inst.mod (n++, kLfoS + l, "master.volume", 10.0f);
        inst.mod (n++, 33, "master.volume", 10.0f);
        inst.mod (n++, 34, "master.volume", 10.0f);
        for (int r = 15; r <= 18; ++r) inst.mod (n++, r, "master.volume", 10.0f);
        ui::UsedSelector env, lfo;
        fill (env, ub::SelKind::Env, inst);
        fill (lfo, ub::SelKind::Lfo, inst);
        check (cellsText (env) == "[1] [2] [3] [4] [5] [6]" && ! env.hasPlus(), "all envelopes used: six cells, no '+' (" + cellsText (env) + ")");
        check (cellsText (lfo) == "[1] [2] [3] [4] [5] [6] [7] [8] [9] [10] [R1] [R2] [R3] [R4]" && ! lfo.hasPlus() && lfo.numCells() == 14, "all LFOs and random LFOs used: 14 cells, no '+' (" + cellsText (lfo) + ")");
        check (lfo.minSize().w == 14 * 56 && lfo.minSize().h == 56, "the worst case needs 14 cells of 56 px (15 with '+') = " + std::to_string (lfo.minSize().w));
        picture (lfo, "used_selector_all.png");
    }
    // ---- LFO 7 appears after 6 is assigned; R1 .. R4 appear when used or chosen from '+'
    {
        demo::Inst inst;
        for (int l = 1; l <= 6; ++l) inst.mod (l, kLfoS + l, "master.volume", 10.0f);
        ui::UsedSelector lfo;
        fill (lfo, ub::SelKind::Lfo, inst);
        check (cellsText (lfo) == "[1] [2] [3] [4] [5] [6] +" && choicesText (lfo) == "7 R1", "LFO 1 .. 6 assigned: '+' offers LFO 7 (" + cellsText (lfo) + " / " + choicesText (lfo) + ")");
        int ch[4];
        lfo.plusChoices (ch, 4);
        const int id7 = lfo.state.items[ch[0]].id, idR1 = lfo.state.items[ch[1]].id;
        check (id7 == 7 && idR1 == ub::kRndBase + 1, "the ids of the two choices are 7 and 11 (R1)");
        inst.mod (7, kLfoS + 7, "master.volume", 10.0f);
        fill (lfo, ub::SelKind::Lfo, inst);
        check (cellsText (lfo) == "[1] [2] [3] [4] [5] [6] [7] +" && choicesText (lfo) == "8 R1", "LFO 7 assigned: it is shown, '+' offers 8 (" + cellsText (lfo) + ")");
        const int writes0 = inst.writes;
        ub::chooseLfo (inst, ub::kRndBase + 3);   // R3 chosen from '+'
        fill (lfo, ub::SelKind::Lfo, inst);
        check (inst.tabView (IInstanceView::kTabViewRnd) == 3 && inst.playSelection (2) == 1 && ub::selectedLfo (inst) == ub::kRndBase + 3, "choosing R3: mpcTabView.rnd = 3, Play LFO keeps its last standard number");
        check (cellsText (lfo) == "[1] [2] [3] [4] [5] [6] [7] [R3] +" && choicesText (lfo) == "8 R1", "the chosen random LFO is shown as selected (" + cellsText (lfo) + ")");
        check (inst.writes == writes0, "choosing wrote no engine parameter");
        ub::chooseLfo (inst, 2);
        check (inst.tabView (IInstanceView::kTabViewRnd) == 0 && inst.playSelection (2) == 2, "choosing a standard LFO clears the random one and sets Play LFO");
        fill (lfo, ub::SelKind::Lfo, inst);
        check (cellsText (lfo) == "[1] [2] [3] [4] [5] [6] [7] +", "R3 was neither used nor edited: it is gone from the row (" + cellsText (lfo) + ")");
        inst.mod (8, 17, "master.volume", 10.0f);   // R3 routed
        fill (lfo, ub::SelKind::Lfo, inst);
        check (cellsText (lfo) == "[1] [2] [3] [4] [5] [6] [7] [R3] +", "R3 used (source 17): shown (" + cellsText (lfo) + ")");
    }
    // ---- hidden items keep their parameters; edited items count as used; nothing is written by the selector
    {
        demo::Inst inst;
        inst.set ("lfo4.rate", 3.7f);        // non-default parameters, no routing
        inst.set ("rlfo2.rate", 1.3f);
        inst.set ("env3.attack", 123.0f);
        auto content = std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentLfoShape sh;
        sh.lfo = 6;
        sh.points.resize (2);
        content->lfoShapes.push_back (sh);
        inst.contentV = content;
        ui::UsedSelector lfo, env;
        fill (lfo, ub::SelKind::Lfo, inst);
        fill (env, ub::SelKind::Env, inst);
        check (cellsText (lfo) == "[1] [4] [6] [R2] +", "LFO with changed parameters (4), with a drawn shape (6) and a random LFO with changed parameters (R2) count as used (" + cellsText (lfo) + ")");
        check (cellsText (env) == "[1] [3] +", "an envelope with changed parameters (3) counts as used (" + cellsText (env) + ")");
        // a snapshot of every parameter, then every selector operation, then compare
        std::vector<float> before;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i) before.push_back (inst.param (static_cast<int>(i)));
        for (int id : { 2, 9, 4, 1 }) { ub::chooseLfo (inst, id); fill (lfo, ub::SelKind::Lfo, inst); }
        for (int id : { 12, 14, 11 }) { ub::chooseLfo (inst, id); fill (lfo, ub::SelKind::Lfo, inst); }
        for (int id : { 6, 2, 5 }) { ub::chooseEnv (inst, id); fill (env, ub::SelKind::Env, inst); }
        bool same = inst.writes == 0;
        for (size_t i = 0; i < synthyboy::paramCount(); ++i) same = same && inst.param (static_cast<int>(i)) == before[i];
        check (same, "after select-away and select-again no parameter was written or changed (hidden items keep their values)");
        check (inst.param (demo::Inst::idx ("lfo4.rate")) == 3.7f && inst.param (demo::Inst::idx ("env3.attack")) == 123.0f, "the hidden-or-shown items still hold their edited values");
    }
    // ---- the shared selection with the Play page: one state
    {
        demo::Inst inst;
        inst.setPlaySelection (1, 4);   // the Play page chose Env 4
        inst.setPlaySelection (2, 7);
        check (ub::selectedEnv (inst) == 4 && ub::selectedLfo (inst) == 7, "the tab reads what the Play page selected (Env 4, LFO 7)");
        ub::chooseEnv (inst, 2);
        ub::chooseLfo (inst, 9);
        check (inst.playSelection (1) == 2 && inst.playSelection (2) == 9, "the tab's choice is the Play page's (Play Env 2, Play LFO 9)");
        ub::chooseLfo (inst, ub::kRndBase + 2);
        check (ub::selectedLfo (inst) == ub::kRndBase + 2 && inst.playSelection (2) == 9, "a random LFO: the Play LFO selector keeps 9");
        ub::chooseLfo (inst, 99);
        ub::chooseEnv (inst, 0);
        check (ub::selectedLfo (inst) == ub::kRndBase + 2 && ub::selectedEnv (inst) == 2, "out of range ids are ignored");
    }
    // ---- the component: touches, targets, turn, drawing
    {
        demo::Inst inst;
        inst.mod (1, kEnvS + 2, "master.volume", 10.0f);
        inst.mod (2, kEnvS + 4, "master.volume", 10.0f);
        ui::UsedSelector env, lfo;
        env.setId (77); lfo.setId (78);
        fill (env, ub::SelKind::Env, inst);   // [1] [2] [4] +, selected 1
        env.place ({ 8, 8, 4 * 56, 56 });
        check (env.sizeOk() && env.minSize().w == 4 * 56, "four cells: the minimum size is 4 x 56");
        Rect t[8];
        const int nt = env.touchTargets (t, 8);
        bool big = nt == 4;
        for (int i = 0; i < nt; ++i) big = big && t[i].w >= 56 && t[i].h >= 56;
        check (big, "four touch targets of at least 56 px");
        Change c = tap (env, t[2]);
        check (c.kind == ChangeKind::Index && c.id == 77 && c.index == 4, "a tap on [4]: Index { 77, 4 }");
        c = tap (env, t[0], 500.0);
        check (! c, "a tap on the selected cell reports nothing");
        c = tap (env, t[3], 1000.0);
        check (c.kind == ChangeKind::Index && c.index == 3, "a tap on '+' with one choice selects the lowest free envelope (3) at once");
        fill (lfo, ub::SelKind::Lfo, inst);
        lfo.place ({ 8, 8, 2 * 56, 56 });
        const int n2 = lfo.touchTargets (t, 8);
        c = tap (lfo, t[n2 - 1], 1500.0);
        check (c.kind == ChangeKind::Request && c.id == 78, "a tap on '+' with two choices is a Request (the owner opens the list of plusChoices)");
        // touch down on one cell and up on another: nothing
        c = env.touchDown (t[0].x + 5, 20, 2000.0);
        c = env.touchUp (t[0].x + 5 + 120, 20);
        check (! c, "a gesture that ends on another cell is no tap");
        // turn
        env.setSelected (2);
        c = env.turn (1.0);
        check (c.kind == ChangeKind::Index && c.index == 4, "a turn up steps to the next shown candidate (not '+')");
        env.setSelected (4);
        check (! env.turn (1.0), "no step beyond the last shown candidate");
        c = env.turn (-5.0);
        check (c.kind == ChangeKind::Index && c.index == 1, "a turn down is clamped at the first");
        env.setEnabled (false);
        check (! tap (env, t[2], 3000.0), "disabled: no reaction");
    }
    // ---- the chunk member
    {
        namespace tv = synthyboy_mpc::tabview;
        const std::string base = "{\"mpcProgram\":3,\"params\":{\"a\":1},\"mpcPlayOsc5\":2,\"mpcPlayEnv\":3,\"mpcPlayLfo\":4}";
        const int v[2] = { 3, 2 };
        const std::string c = tv::chunkWithTabView (base, v);
        check (c.find (",\"mpcTabView\":{\"rnd\":3,\"filter\":2}}") != std::string::npos && c.rfind ('}') == c.size() - 1, "the chunk member is written before the last closing brace");
        int r[2] = { 9, 9 };
        check (tv::tabViewFromChunk (c.data(), c.size(), r) && r[0] == 3 && r[1] == 2, "the field round trips");
        int d[2] = { 1, 1 };
        check (! tv::tabViewFromChunk (base.data(), base.size(), d) && d[0] == 1 && d[1] == 1, "an old chunk without it: not found (the plugin then loads the defaults)");
        std::string bad = c;
        bad.replace (bad.find ("\"rnd\":3"), 7, "\"rnd\":9");
        int b[2] = { 1, 1 };
        check (! tv::tabViewFromChunk (bad.data(), bad.size(), b) && b[0] == 1, "a value out of range is refused (the field is not read)");
        std::string trunc = c.substr (0, c.size() - 4);
        check (! tv::tabViewFromChunk (trunc.data(), trunc.size(), b), "a truncated member is refused");
        check (! tv::tabViewFromChunk (nullptr, 100, b) && ! tv::tabViewFromChunk (c.data(), 10, b), "no data, or too short: false, nothing read outside");
        const int big[2] = { 99, -4 };
        const std::string cl = tv::chunkWithTabView (base, big);
        int rr[2];
        check (tv::tabViewFromChunk (cl.data(), cl.size(), rr) && rr[0] == 4 && rr[1] == 0, "values are clamped when written (4, 0)");
        // the members after it (the limiter and the pad pressure ones) do not hide it
        const std::string full = c.substr (0, c.size() - 1) + ",\"mpcOutputLimiter\":0,\"mpcPadPressure\":0,\"mpcPadPressureAmount\":35.00}";
        check (tv::tabViewFromChunk (full.data(), full.size(), rr) && rr[0] == 3 && rr[1] == 2, "found in front of the limiter and the pad pressure members");
    }
    std::printf ("ui_used_selector_host: %s (%d failures)\n", g_fails == 0 ? "OK" : "FAILED", g_fails);
    return g_fails == 0 ? 0 : 1;
}
