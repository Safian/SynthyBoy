// SynthyBoy - MPC UI component library tests: the FX SLOT EDITOR driven by the TABLES of the effects (fx_layout.h): every key of every table is an engine parameter, every parameter of a type of the pool has its control
// once, the three splitters have pages, the one rule for what does nothing in the state of the effect (a pair of controls in one cell shows the live one; a control that is not live is hidden when no live control follows
// it in its row, else dim; a row with nothing live is gone and the rows below move up), the pages made of the rows that are left, the Q groups (a member exactly where a control is live), and the NUMBER FIELD kind
// (VOICES, UNISON: whole numbers) with the NumberField's `maxBox` (the drawn field is limited, the touch zone is the cell).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <map>
#include <set>
#include <string>

#include "ui_test.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

namespace
{
/** The values of the keys the editor asks for: the engine's defaults, with the overrides of the test. */
struct State
{
    std::map<std::string, float> v;
    float operator() (const char* key) const
    {
        const auto it = v.find (key);
        if (it != v.end())
            return it->second;
        const synthyboy::ParamInfo* pi = synthyboy::findParamByKey (key);
        return pi != nullptr ? pi->def : 0.0f;
    }
};

int shownOf (FxSlotEditor& ed, const char* key)   // 1 live on the pages, 2 dim on the pages, 0 not on the pages
{
    const int p = ed.controlOfKey (key);
    return p >= 0 && ed.rowOfControl (p) >= 0 ? (ed.isLive (p) ? 1 : 2) : 0;
}

void testTables()
{
    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        FxSlotEditor ed;
        ed.place ({ 0, 100, 1280, 524 });
        ed.setType (kind, kind < synthyboy::kNumFxInstanceTypes);
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;
        std::set<std::string> seen;
        bool engineKeys = true, once = true;
        int keys = 0;
        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (const fxl::Alt& a : lay.row[r].cell[c].alt)
                {
                    if (a.key == nullptr)
                        continue;
                    ++keys;
                    engineKeys = engineKeys && synthyboy::findParamByKey (a.key) != nullptr && (a.live.always() || synthyboy::findParamByKey (a.live.key) != nullptr);
                    once = once && seen.insert (a.key).second;
                }
        check (engineKeys && once && keys > 0, name + ": every key of the table is an engine parameter and stands once (" + std::to_string (keys) + ")");
        // a pair excludes itself: the first live one wins, and the two are never live in the same state
        bool pairs = true;
        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
            {
                const fxl::Cell& cell = lay.row[r].cell[c];
                if (cell.alt[1].key == nullptr)
                    continue;
                const synthyboy::ParamInfo* cond = synthyboy::findParamByKey (cell.alt[0].live.key);
                pairs = pairs && cond != nullptr && std::string (cell.alt[0].live.key) == cell.alt[1].live.key;
                for (int m = 0; cond != nullptr && m < cond->numChoices; ++m)   // in no state of the condition are both live
                    pairs = pairs && ! (cell.alt[0].live.holds (static_cast<float> (m)) && cell.alt[1].live.holds (static_cast<float> (m)));
            }
        check (pairs, name + ": the two controls of a cell are live in different states");
        if (kind < synthyboy::kNumFxInstanceTypes)
        {
            const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (kind);
            bool all = true;
            for (int p = 0; p < d.count; ++p)
                all = all && ed.infoOf (p) == &synthyboy::paramTable()[d.source[p]];
            check (all, name + ": control p is parameter p of the type (every parameter of the type has its control)");
        }
        check (lay.rowsPerPage >= 1 && lay.rowsPerPage <= fxl::kMaxPageRows && lay.numRows <= fxl::kMaxRows, name + ": rows per page and rows of the table are in range");
    }
}

void testRule()
{
    {   // RATE / DIVISION: one cell
        FxSlotEditor ed;
        ed.place ({ 0, 100, 1280, 524 });
        ed.setType (synthyboy::kFxTypeChorus, true);
        State s;
        ed.setState (s);
        const int rate = ed.controlOfKey ("chorus.rate"), div = ed.controlOfKey ("chorus.division");
        check (shownOf (ed, "chorus.rate") == 1 && shownOf (ed, "chorus.division") == 0 && ed.groups[0].member (0) == ed.memberOf (rate), "pair: in Free (Hz) the cell shows the RATE, the DIVISION is not on the pages");
        check (ed.memberOf (rate)->rect() == ed.memberOf (div)->rect() && ed.memberOf (rate)->rect().h >= 56, "pair: both controls lie in the same rectangle (one cell)");
        s.v["chorus.sync"] = 1.0f;
        ed.setState (s);
        check (shownOf (ed, "chorus.rate") == 0 && shownOf (ed, "chorus.division") == 1 && ed.groups[0].member (0) == ed.memberOf (div), "pair: in a tempo sync the cell shows the DIVISION, the first member of its Q group");
        s.v["chorus.sync"] = 3.0f;
        ed.setState (s);
        check (shownOf (ed, "chorus.division") == 1, "pair: triplets are tempo too");
    }
    {   // the end of a row is hidden, a whole row closes up and the rows below move up
        FxSlotEditor ed;
        ed.place ({ 0, 100, 1280, 524 });
        ed.setType (synthyboy::kFxTypeDistortion, true);
        State s;
        ed.setState (s);
        check (ed.numRows() == 2 && shownOf (ed, "distortion.filter_order") == 1 && shownOf (ed, "distortion.filter_cutoff") == 0 && shownOf (ed, "distortion.filter_keytrack") == 0, "closing up: without a filter the filter values are hidden and the key track row is gone (2 rows)");
        check (ed.groups[1].numMembers() == 1 && ed.groups[2].numMembers() == 0, "closing up: the Q group of the row has the one member, the row that is gone has none");
        s.v["distortion.filter_order"] = 2.0f;
        ed.setState (s);
        check (ed.numRows() == 3 && ed.groups[1].numMembers() == 4 && ed.groups[2].numMembers() == 1 && std::string (ed.rowTitle (2)) == "FILTER KEY", "closing up: with a filter the values and the row are back (3 rows)");
        const int blend = ed.controlOfKey ("distortion.filter_blend");
        check (ed.memberOf (blend)->enabled() && ed.rowOfControl (blend) == 1, "a live control is enabled");
    }
    {   // dim in place: a dead control with a live one after it
        FxSlotEditor ed;
        ed.place ({ 0, 100, 1280, 524 });
        ed.setType (synthyboy::kFxTypeCompressor, true);
        State s;
        ed.setState (s);
        const int lat = ed.controlOfKey ("compressor.limit_latency_comp");
        check (shownOf (ed, "compressor.limit_latency_comp") == 2 && ! ed.memberOf (lat)->enabled() && ed.memberControl (ed.rowOfControl (lat), 1) == -1 && ed.groups[ed.rowOfControl (lat)].numMembers() == 3, "dim: a control that does nothing with a live one behind it stays in its place, takes no touch, has no Q member");
        Change ch = ed.toggles[lat].touchDown (ed.toggles[lat].rect().x + 5, ed.toggles[lat].rect().y + 5, 0.0);
        ch = ed.toggles[lat].touchUp (ed.toggles[lat].rect().x + 5, ed.toggles[lat].rect().y + 5);
        check (! ch, "dim: a tap on it makes no change");
        s.v["compressor.bands"] = 3.0f;   // single band: only the middle band
        ed.setState (s);
        check (shownOf (ed, "compressor.low.lower_threshold") == 0 && shownOf (ed, "compressor.high.upper_ratio") == 0 && shownOf (ed, "compressor.mid.lower_threshold") == 1 && shownOf (ed, "compressor.low.gain") == 2 && shownOf (ed, "compressor.crossover_low") == 0, "bands: in the single band mode the other bands' rows are gone, their gains dim, the crossovers hidden");
        check (ed.numRows() == 4, "bands: four rows are left of the six (" + std::to_string (ed.numRows()) + ")");
    }
    {   // pages are made of the rows that are left
        FxSlotEditor ed;
        ed.place ({ 0, 100, 1280, 524 });
        ed.setType (synthyboy::kFxTypeCompressor, true);
        State s;
        ed.setState (s);
        check (ed.numRows() == 6 && ed.rowsPerPage() == 2 && ed.numPages() == 3 && ed.hasDisplay() && ed.displayRect().h == 220, "pages: six rows, two a page, three pages, the display has the height of its table");
        s.v["compressor.bands"] = 3.0f;
        ed.setState (s);
        check (ed.numPages() == 2, "pages: the pages follow the rows that are left (2)");
        ed.setPage (1);
        check (ed.pageIndex() == 1 && ed.activeRow() == 2, "pages: the page stepper selects the first row of the page");
    }
}

void testNumberKind()
{
    FxSlotEditor ed;
    ed.place ({ 0, 100, 1280, 524 });
    ed.setType (synthyboy::kFxTypeChorus, true);
    State s;
    ed.setState (s);
    const int v = ed.controlOfKey ("chorus.voices");
    check (v >= 0 && ed.kindOf (v) == FxSlotEditor::Kind::Number, "a whole number (VOICES) is a number field");
    bool noneElse = true;
    for (int p = 0; p < ed.numParams(); ++p)
        if (ed.infoOf (p) != nullptr && p != v && ed.infoOf (p)->numChoices == 0 && ed.infoOf (p)->interval != 1.0f)
            noneElse = noneElse && ed.kindOf (p) == FxSlotEditor::Kind::Knob;
    check (noneElse, "a continuous value stays a knob");
    NumberField& n = ed.numbers[v];
    n.state.min = 1.0; n.state.max = 4.0; n.state.step = 1.0; n.state.value = 3.0;
    check (n.maxBox.w == 200 && n.maxBox.h == 84 && n.rect().w > 200, "the number field of the cell has a limited box, the cell is wider");

    // the drawn box is limited, at the left of the cell; the touch zone is the cell
    NumberField f ("VOICES", NumberField::Format::IntPlain);
    f.state.min = 1.0; f.state.max = 4.0; f.state.value = 3.0;
    f.maxBox = { 160, 60 };
    f.place ({ 20, 20, 400, 150 });
    Scene sc (500, 200, kBackgroundRgba);
    f.draw (sc.builder());
    const Image im = render (sc);
    check (drawnInside (im, { 20, 20, 160, 150 }, backgroundPixel()) > 0 && drawnInside (im, { 183, 0, 317, 200 }, backgroundPixel()) == 0, "the box is drawn at the left, nothing at the right of it");
    check (drawnInside (im, { 0, 0, 500, 20 }, backgroundPixel()) == 0 && drawnInside (im, { 20, 20, 160, 44 }, backgroundPixel()) == 0 && drawnInside (im, { 20, 126, 160, 44 }, backgroundPixel()) == 0, "and it is centred in the height of the cell");
    (void) f.touchDown (390, 160, 0.0);   // in the cell, outside the drawn box
    const Change moved = f.touchMove (390, 160 + 8);
    check (moved.kind == ChangeKind::Index && moved.index == 1 && moved.value == 2.0, "a drag of 8 px down in the cell (outside the drawn box) steps the number down by one: 3 -> 2 (the touch zone is the cell)");
    (void) f.touchUp (390, 168);
}
} // namespace

int main()
{
    testTables();
    testRule();
    testNumberKind();
    std::printf ("%s\n", g_fails == 0 ? "ALL PASSED" : "FAILED");
    return g_fails == 0 ? 0 : 1;
}
