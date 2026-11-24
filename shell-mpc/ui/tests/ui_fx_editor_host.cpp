// SynthyBoy - MPC UI component library tests: the FX SLOT EDITOR driven by the TABLES of the effects (fx_layout.h): every key of every table is an engine parameter, every parameter of a type of the pool has its control
// once, the three splitters have pages, the one rule for what does nothing in the state of the effect (a pair of controls in one cell shows the live one; a control that is not live is hidden when no live control follows
// it in its row, else dim; a row with nothing live is gone and the rows below move up), the pages made of the rows that are left, the Q groups (a member exactly where a control is live), and the NUMBER FIELD kind
// (VOICES, UNISON: whole numbers) with the NumberField's `maxBox` (the drawn field is limited, the touch zone is the cell).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

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

int shownOf (FxSlotEditor& ed, const char* key)   // 1 live on the views, 0 not on them (a control that is not live is hidden, never dim)
{
    const int p = ed.controlOfKey (key);
    return p >= 0 && ed.rowOfControl (p) >= 0 ? 1 : 0;
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
                    // an engine key (the BAND selector of the compressor is a view-local cell: its key and the condition on it name no parameter)
                    engineKeys = engineKeys && (fxl::isView (a.key) || synthyboy::findParamByKey (a.key) != nullptr) && (a.live.always() || fxl::isView (a.live.key) || synthyboy::findParamByKey (a.live.key) != nullptr);
                    once = once && seen.insert (a.key).second;
                }
        check (engineKeys && once && keys > 0, name + ": every key of the table is an engine parameter and stands once (" + std::to_string (keys) + ")");
        // a pair excludes itself: the first live one wins, and the two are never live in the same state
        bool pairs = true;
        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
            {
                const fxl::Cell& cell = lay.row[r].cell[c];
                for (int i = 0; i < fxl::kAlts; ++i)
                    for (int j = i + 1; j < fxl::kAlts; ++j)
                    {
                        if (cell.alt[i].key == nullptr || cell.alt[j].key == nullptr)
                            continue;
                        const char* ki = cell.alt[i].live.key;
                        pairs = pairs && ki != nullptr && std::string (ki) == cell.alt[j].live.key;
                        const synthyboy::ParamInfo* cond = fxl::isView (ki) || ki == nullptr ? nullptr : synthyboy::findParamByKey (ki);
                        const int values = fxl::isView (ki) ? 3 : (cond != nullptr ? cond->numChoices : 0);
                        pairs = pairs && (cond != nullptr || fxl::isView (ki));
                        for (int m = 0; m < values; ++m)   // in no state of the condition are two of them live
                            pairs = pairs && ! (cell.alt[i].live.holds (static_cast<float> (m)) && cell.alt[j].live.holds (static_cast<float> (m)));
                    }
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
    {   // a control that is not live is hidden, a whole row closes up and the rows below move up
        FxSlotEditor ed;
        ed.place ({ 0, 100, 1280, 524 });
        ed.setType (synthyboy::kFxTypeDistortion, true);
        State s;
        ed.setState (s);
        check (ed.numRows() == 2 && shownOf (ed, "distortion.filter_order") == 1 && shownOf (ed, "distortion.filter_cutoff") == 0 && shownOf (ed, "distortion.filter_keytrack") == 0, "closing up: without a filter the filter values are hidden and the shape row is gone (2 rows)");
        check (ed.groups[1].numMembers() == 1 && ed.groups[2].numMembers() == 0 && ed.numPages() == 1, "closing up: the Q group of the row has the one member, the row that is gone has none, nothing is behind MORE");
        s.v["distortion.filter_order"] = 2.0f;
        ed.setState (s);
        check (ed.numRows() == 3 && ed.numDefaultRows() == 3 && ed.groups[1].numMembers() == 3 && ed.groups[2].numMembers() == 2 && std::string (ed.rowTitle (2)) == "FILTER SHAPE", "closing up: with a filter the values are back, the blend and the key track are behind MORE (3 rows)");
        check (ed.numPages() == 1, "Distortion keeps all filter controls on the same compact panel");
        const int blend = ed.controlOfKey ("distortion.filter_blend");
        check (ed.memberOf (blend)->enabled() && ed.rowOfControl (blend) == 2, "a live control is enabled");
    }
    {   // hidden, never dim, and the BAND selector of the compressor: the default view shows ONE band's upper threshold and ratio
        FxSlotEditor ed;
        ed.place ({ 0, 100, 1280, 524 });
        ed.setType (synthyboy::kFxTypeCompressor, true);
        State s;
        ed.setState (s);
        const int lat = ed.controlOfKey ("compressor.limit_latency_comp");
        check (shownOf (ed, "compressor.limit_latency_comp") == 0 && ! ed.isLive (lat), "hidden: the latency compensation (limit mode 1 only) is not on the pages");
        // Multiband, the middle band selected (the default): MODE, THRESHOLD, RATIO and the selector are the second row; the selector is no Q member
        check (ed.hasBandSelector() && ed.effectiveBand() == 1 && ed.numDefaultRows() == 2 && ed.groups[1].numMembers() == 3, "band: Multiband, the middle band: the second row has MODE, THRESHOLD, RATIO as Q members (the selector is a cell without a Q slot)");
        check (shownOf (ed, "compressor.mid.upper_threshold") == 1 && shownOf (ed, "compressor.low.upper_threshold") == 0 && shownOf (ed, "compressor.high.upper_ratio") == 0 && ed.controlInCell (1, 3) == FxSlotEditor::kSel, "band: only the selected band's upper pair is on the default view, the selector is the last cell");
        ed.setBand (0);
        check (ed.effectiveBand() == 0 && shownOf (ed, "compressor.low.upper_threshold") == 1 && shownOf (ed, "compressor.mid.upper_threshold") == 0 && ed.memberControl (1, 1) == ed.controlOfKey ("compressor.low.upper_threshold") && ed.memberControl (1, 2) == ed.controlOfKey ("compressor.low.upper_ratio"),
               "band: LOW selected: THRESHOLD and RATIO are the low band's, on Q slots 2 and 3 of the row");
        ed.setBand (2);
        check (ed.memberControl (1, 1) == ed.controlOfKey ("compressor.high.upper_threshold") && ed.memberOf (ed.controlOfKey ("compressor.high.upper_ratio"))->enabled(), "band: HIGH selected: the high band's pair");
        // the mode of the bands clamps the selection; Single band has no selector
        s.v["compressor.bands"] = 1.0f;   // Low band: low + mid
        ed.setState (s);
        check (ed.effectiveBand() == 1 && ed.bandSel.state.index == 1 && (ed.bandSel.state.disabledMask & 4) != 0, "band: in the Low band mode HIGH is not selectable and the selection falls back to MID");
        s.v["compressor.bands"] = 3.0f;   // single band: only the middle band
        ed.setState (s);
        check (! (ed.controlInCell (1, 3) == FxSlotEditor::kSel) && ed.effectiveBand() == 1 && shownOf (ed, "compressor.mid.upper_threshold") == 1 && ed.groups[1].numMembers() == 3,
               "band: in Single band there is no selector, the one band's pair is shown");
        check (shownOf (ed, "compressor.low.lower_threshold") == 0 && shownOf (ed, "compressor.high.gain") == 0 && shownOf (ed, "compressor.crossover_low") == 0 && shownOf (ed, "compressor.low.gain") == 0, "bands: in the single band mode the other bands' values and the crossovers are gone (not dim)");
        // Limit mode: the upper ratio is not used by the engine (the limiter replaces the downward stage): hidden
        s.v["compressor.bands"] = 0.0f;
        s.v["compressor.limit"] = 1.0f;
        ed.setBand (1);
        ed.setState (s);
        check (shownOf (ed, "compressor.limit_latency_comp") == 1 && ed.groups[1].numMembers() == 2 && ! ed.isLive (ed.controlOfKey ("compressor.mid.upper_ratio")), "limit: the ratio is hidden (the threshold is the ceiling), the latency compensation is live behind MORE");
        // no cell of any view shows a dead control, and every shown cell is a live one
        bool onlyLive = true;
        for (int pg = 0; pg < ed.numPages(); ++pg)
        {
            ed.setPage (pg);
            for (int rr = 0; rr < ed.rowsOnPage(); ++rr)
                for (int c = 0; c < 4; ++c)
                {
                    const int p = ed.controlInCell (ed.firstRowOfPage() + rr, c);
                    onlyLive = onlyLive && (p < 0 || p == FxSlotEditor::kSel || ed.isLive (p));
                }
        }
        check (onlyLive, "hidden: every shown cell of both views is live");
    }
    {   // Functional panels preserve one selected editor and explicit navigation.
        FxSlotEditor ed;
        ed.place ({ 0, 100, 1280, 524 });
        ed.setType (synthyboy::kFxTypeCompressor, true);
        State s;
        ed.setState (s);
        check (ed.numRows() == 6 && ed.numDefaultRows() == 2 && ed.numPages() == 3 && ed.hasDisplay(), "Compressor has MAIN, BANDS and UPWARD panels");
        check (ed.pageIndex() == 0 && ed.rowsOnPage() == 2 && std::string (ed.panelName (0)) == "MAIN", "MAIN shows global and selected-band compression");
        ed.setPage (1);
        check (ed.pageIndex() == 1 && ed.rowsOnPage() == 2 && ed.activeRow() == 2 && std::string (ed.panelName (1)) == "BANDS", "BANDS owns output, crossovers and gains");
        ed.setPage (2);
        check (ed.pageIndex() == 2 && ed.rowsOnPage() == 2 && ed.activeRow() == 4 && std::string (ed.panelName (2)) == "UPWARD", "third panel owns the upward pairs");
        s.v["compressor.bands"] = 3.0f;
        ed.setState (s);
        check (ed.numPages() == 3 && ed.rowsOnPage() == 1, "single-band UPWARD collapses its hidden high row");
        ed.setPage (0);
        check (ed.pageIndex() == 0 && ed.rowsOnPage() == 2, "MAIN can be selected explicitly again");
    }
}

/** THE BOLD CUT, per effect (owner 2026-11-21): over every state of the conditions of its table the default view has at most 16 cells in at most 4 Q groups of 4 (one Q group each), MORE shows every other live control (the
    union of the two views is the table: the old full set, minus what is not live in that state), nothing that is not live is on either view (hidden, not dim), the Q groups are the rows of the view (at most 4 members, all live). */
void testViews()
{
    // the values of each effect before the cut (the old tables: every key stood once)
    static const int kOldValues[synthyboy::kNumFxInstanceTypes] = { 0, 12, 24, 14, 9, 18, 18, 9, 11, 21, 8, 10, 14, 14 };
    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;
        // the keys the conditions of the table name, and every combination of their values (a handful of states)
        std::vector<std::string> condKeys;
        auto addCond = [&] (const char* k) { if (k != nullptr && std::find (condKeys.begin(), condKeys.end(), k) == condKeys.end()) condKeys.push_back (k); };
        int keys = 0;
        bool viewSel = false;   // the table has the BAND selector: every selection is a state of its own
        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (const fxl::Alt& a : lay.row[r].cell[c].alt)
                    if (a.key != nullptr)
                    {
                        keys += ! fxl::isView (a.key);
                        viewSel = viewSel || fxl::isView (a.key);
                        if (! fxl::isView (a.live.key))
                            addCond (a.live.key);
                        if (! fxl::isView (a.live.key2))
                            addCond (a.live.key2);
                    }
        if (kind < synthyboy::kNumFxInstanceTypes)
            check (keys == kOldValues[kind], name + ": the table holds the old full set of " + std::to_string (kOldValues[kind]) + " controls (" + std::to_string (keys) + ")");
        check (lay.defaultRows >= 1 && lay.defaultRows <= fxl::kMaxPageRows && lay.defaultRows <= lay.numRows, name + ": the MAIN panel fits at most4 Q groups");
        std::vector<int> radix;
        long states = 1;
        for (const std::string& k : condKeys)
        {
            const synthyboy::ParamInfo* pi = synthyboy::findParamByKey (k.c_str());
            radix.push_back (pi != nullptr ? std::max (1, std::min (static_cast<int> (pi->numChoices), 12)) : 1);
            states *= radix.back();
        }
        const long engineStates = states;
        if (viewSel)
            states *= 3;
        std::set<std::string> everShown;
        bool defaultOk = true, rowsOk = true, hiddenOk = true, moreOk = true, qOk = true, liveAllShown = true, disjoint = true;
        int maxDefault = 0;
        std::string why;
        for (long st = 0; st < states; ++st)
        {
            State s;
            long rest = st % engineStates;
            const int sel = static_cast<int> (st / engineStates);   // the BAND selector of the compressor (0 low, 1 mid, 2 high)
            for (size_t i = 0; i < condKeys.size(); ++i)
            {
                s.v[condKeys[i]] = static_cast<float> (rest % radix[i]);
                rest /= radix[i];
            }
            FxSlotEditor ed;
            ed.place ({ 0, 100, 1280, 524 });
            ed.setType (kind, kind < synthyboy::kNumFxInstanceTypes);
            ed.setBand (sel);
            ed.setState (s);
            s.v["@band"] = static_cast<float> (ed.effectiveBand());   // the condition on the selector reads the band the mode allows
            // the live controls of the table in this state: the first live one of every cell
            std::set<std::string> live;
            for (int r = 0; r < lay.numRows; ++r)
                for (int c = 0; c < 4; ++c)
                    for (const fxl::Alt& a : lay.row[r].cell[c].alt)
                        if (a.key != nullptr && a.live.liveIn (s))
                        {
                            live.insert (a.key);
                            break;
                        }
            std::set<std::string> shown;
            int defaultCells = 0;
            for (int pg = 0; pg < ed.numPages(); ++pg)
            {
                ed.setPage (pg);
                for (int rr = 0; rr < ed.rowsOnPage(); ++rr)
                {
                    int cells = 0, members = 0, last = -1;
                    for (int c = 0; c < 4; ++c)
                    {
                        const int p = ed.controlInCell (ed.firstRowOfPage() + rr, c);
                        if (p < 0)
                            continue;
                        const bool selector = p == FxSlotEditor::kSel;   // the view-local BAND selector: a cell, no Q member, last in its row
                        const char* k = selector ? fxl::kViewBand : ed.infoOf (p)->key;
                        hiddenOk = hiddenOk && live.count (k) == 1 && (selector || (ed.isLive (p) && ed.memberOf (p)->enabled()));
                        disjoint = disjoint && shown.insert (k).second;
                        everShown.insert (k);
                        defaultCells += pg == 0;
                        ++cells;
                        last = c;
                        if (! selector)
                        {
                            ++members;
                            qOk = qOk && ed.groups[ed.firstRowOfPage() + rr].member (c) == ed.memberOf (p);
                        }
                        else
                            qOk = qOk && c == cells - 1 && ed.memberControl (ed.firstRowOfPage() + rr, c) < 0;
                    }
                    rowsOk = rowsOk && cells >= 1 && cells <= 4 && members >= 1 && last == cells - 1;   // the cells of a row stand together from the left
                    qOk = qOk && ed.groups[ed.firstRowOfPage() + rr].numMembers() == members;
                }
                if (pg == 0)
                    defaultOk = defaultOk && ed.rowsOnPage() <= fxl::kMaxPageRows && defaultCells <= 16;
                if (pg == 1)
                    moreOk = moreOk && ed.rowsOnPage() >= 1 && ed.rowsOnPage() <= fxl::kMaxPageRows;
            }
            maxDefault = std::max (maxDefault, defaultCells);
            liveAllShown = liveAllShown && shown == live;   // default + MORE = every live control of the table, each once
            if (ed.numPages() > 1)
                moreOk = moreOk && ed.numRows() > ed.numDefaultRows();
            else
                moreOk = moreOk && ed.numRows() == ed.numDefaultRows() && lay.numRows >= 1;
            if (! (defaultOk && rowsOk && hiddenOk && moreOk && qOk && liveAllShown && disjoint) && why.empty())
                why = " (state " + std::to_string (st) + ")";
        }
        check (defaultOk, name + ": the default view has at most 16 cells in at most 4 Q groups in every state (max " + std::to_string (maxDefault) + ")" + why);
        check (liveAllShown && disjoint, name + ": the default view and MORE together show every live control of the table once" + why);
        // every key of the table is reached in some state (nothing is lost behind MORE)
        std::set<std::string> all;
        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (const fxl::Alt& a : lay.row[r].cell[c].alt)
                    if (a.key != nullptr)
                        all.insert (a.key);
        check (everShown == all, name + ": every control of the old table is reachable (" + std::to_string (everShown.size()) + " of " + std::to_string (all.size()) + ")");
        check (hiddenOk, name + ": hidden, not dim: every control on either view is live and takes touch" + why);
        check (rowsOk && qOk, name + ": the Q groups are the rows of the view, at most 4 live members from the left" + why);
        check (moreOk, name + ": MORE is there exactly when something is behind it, and fits 4 rows" + why);
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

/** The band board of a splitter (fx_band_board.h): the lanes, the cards, the ids, the touch sizes, the changes; the back card of the chain bar. */
void testBandBoard()
{
    FxBandBoard bd;
    bd.assignIds (3100);
    bd.place ({ 8, 200, 1264, 240 });
    const int cards[3] = { 0, 2, 13 };
    bd.setShape (3, cards);
    check (bd.numBands() == 3 && bd.numCards (0) == 0 && bd.numCards (1) == 2 && bd.numCards (2) == 13, "the board has three lanes with 0, 2 and 13 cards");
    bool sizes = true, inside = true, apart = true;
    for (int b = 0; b < 3; ++b)
    {
        const FxBandBoard::Lane& ln = bd.lane[b];
        sizes = sizes && ln.tile.rect().h >= 56 && ln.tile.rect().w >= 120;
        inside = inside && bd.rect().contains (ln.tile.rect());
        for (int i = 0; i < bd.numCards (b); ++i)
        {
            const Rect r = ln.cards[i].rect();
            sizes = sizes && r.h >= 56 && r.w >= 64;
            inside = inside && bd.rect().contains (r);
            if (i > 0)
                apart = apart && ! r.overlaps (ln.cards[i - 1].rect());
        }
        if (bd.numCards (b) < FxBandBoard::kMaxCards)
        {
            sizes = sizes && ln.add.rect().h >= 56 && ln.add.rect().w >= 64;
            inside = inside && bd.rect().contains (ln.add.rect());
        }
        if (b > 0)
            apart = apart && ! ln.tile.rect().overlaps (bd.lane[b - 1].tile.rect());
    }
    check (sizes && inside && apart, "every tile, card and + ADD of the board is a touch target of 56 px, inside the board, none over another");
    check (bd.lane[2].add.rect().w == 0 || ! bd.lane[2].add.rect().overlaps (bd.lane[2].cards[12].rect()), "13 cards fill a lane: no room for + ADD, nothing overlaps");
    int band = -1, what = -1;
    check (bd.decode (3100 + 16 * 1 + 1, band, what) && band == 1 && what == 1 && bd.decode (3100 + 16 * 2 + FxBandBoard::kAddId, band, what) && band == 2 && what == FxBandBoard::kAddId && ! bd.decode (3100 + 16 * 3, band, what) && ! bd.decode (3099, band, what), "the ids decode to the lane and the card");

    // the changes: a tap on a tile opens, a tap on + ADD adds, a tap on a card selects, the power switches, a drag moves
    FxBandBoard::Lane& l1 = bd.lane[1];
    Rect t = l1.tile.rect();
    (void) bd.touchDown (t.x + 20, t.y + 20, 0.0);
    Change ch = bd.touchUp (t.x + 20, t.y + 20);
    check (ch.kind == ChangeKind::Index && ch.id == 3100 + 16 + FxBandBoard::kOpenId && ch.index == FxBandTile::kOpen, "a tap on the tile of a band asks to open it");
    Rect a = l1.add.rect();
    (void) bd.touchDown (a.x + 20, a.y + 20, 0.0);
    ch = bd.touchUp (a.x + 20, a.y + 20);
    check (ch.kind == ChangeKind::Index && ch.id == 3100 + 16 + FxBandBoard::kAddId && ch.index == FxSlotCard::kAdd, "a tap on + ADD of a lane asks to add");
    Rect c0 = l1.cards[0].rect();
    (void) bd.touchDown (c0.x + 6, c0.y + c0.h - 8, 0.0);
    ch = bd.touchUp (c0.x + 6, c0.y + c0.h - 8);
    check (ch.kind == ChangeKind::Index && ch.id == 3100 + 16 && ch.index == FxSlotCard::kSelect, "a tap on a card selects it");
    Rect pr = l1.cards[0].powerRect();
    ch = bd.touchDown (pr.x + pr.w / 2, pr.y + pr.h / 2, 0.0);
    const Change up = bd.touchUp (pr.x + pr.w / 2, pr.y + pr.h / 2);
    check (ch.kind == ChangeKind::Index && ch.index == FxSlotCard::kTogglePower && ! up, "the power zone of a lane card switches the power and does not select it");
    Rect c1 = l1.cards[1].rect();
    (void) bd.touchDown (c1.x + 6, c1.y + c1.h - 8, 0.0);
    (void) bd.touchMove (c1.x - 50, c1.y + c1.h - 8);
    ch = bd.touchUp (c0.x + 6, c1.y + c1.h - 8);
    check (ch.kind == ChangeKind::Index && ch.index == FxSlotCard::kMove && static_cast<int> (ch.value) == c0.x + 6, "a drag of a card gives a move with the x where it was dropped");

    // the picture of the board is drawn: the tile in its colour, the cards
    l1.tile.state = {};
    std::snprintf (l1.tile.state.name, sizeof l1.tile.state.name, "MID");
    std::snprintf (l1.tile.state.range, sizeof l1.tile.state.range, "200 Hz to 3.0 kHz");
    Scene sc (1280, 480, kBackgroundRgba);
    bd.draw (sc.builder());
    const Image im = render (sc);
    check (drawnInside (im, l1.tile.rect(), backgroundPixel()) > 2000 && drawnInside (im, bd.lane[0].add.rect(), backgroundPixel()) > 20, "the lanes are drawn");

    // the chain bar with a back card: it stands first, the cards follow, the way back is a touch target and gives its own change
    FxChainBar bar;
    bar.assignIds (3000);
    bar.place ({ 8, 120, 1264, 88 });
    bar.setNumActiveSlots (3);
    const int firstX = bar.cards[0].rect().x;
    bar.setBackShown (true);
    check (bar.backShown() && bar.backCard.rect().x < bar.cards[0].rect().x && bar.cards[0].rect().x > firstX && bar.backCard.rect().w >= 64 && bar.backCard.rect().h >= 56 && bar.backCard.id() == 3000 + FxChainBar::kMaxSlots + 1, "the back card stands before the cards and is a touch target");
    bar.setBackShown (false);
    check (! bar.backShown() && bar.cards[0].rect().x == firstX, "without it the cards stand where they stood");
    bar.setBackShown (true);
    const Rect br = bar.backCard.rect();
    (void) bar.touchDown (br.x + br.w / 2, br.y + br.h / 2, 0.0);
    ch = bar.touchUp (br.x + br.w / 2, br.y + br.h / 2);
    check (ch.kind == ChangeKind::Index && ch.index == FxSlotCard::kBack && ch.id == 3000 + FxChainBar::kMaxSlots + 1, "a tap on the back card gives kBack");
    std::snprintf (bar.backCard.state.name, sizeof bar.backCard.state.name, "SPLITTER L/M/H");
    std::snprintf (bar.backCard.state.tag, sizeof bar.backCard.state.tag, "MID BAND");
    Scene sc2 (1280, 240, kBackgroundRgba);
    bar.draw (sc2.builder());
    const Image im2 = render (sc2);
    check (drawnInside (im2, bar.backCard.rect(), backgroundPixel()) > 500, "the back card is drawn (chevron, place, band)");

    // the page of a splitter has the board in the display area, other effects have their display
    FxSlotEditor ed;
    ed.place ({ 0, 100, 1280, 524 });
    ed.setType (fxl::kKindSplitterLmh, true);
    check (ed.hasBands() && ed.hasDisplay() && ed.displayRect().h == fxl::layoutOf (fxl::kKindSplitterLmh).displayH && ed.displayRect() == ed.bands.rect() && ed.display.kind == FxDisplay::Kind::None, "the page of a splitter has the band board where the display would be");
    ed.setType (synthyboy::kFxTypeEq, true);
    check (! ed.hasBands() && ed.hasDisplay() && ed.display.kind == FxDisplay::Kind::Eq, "the page of the EQ has its display, no board");
    ed.setPath ("MAIN CHAIN  >  SPLITTER L/H  >  LOW BAND");
    check (std::string (ed.path()) == "MAIN CHAIN  >  SPLITTER L/H  >  LOW BAND", "the place stands over the name");
    // the band racks of the engine: 7 racks, 2 + 3 + 2 bands, names, and the way back from a rack
    bool maps = true;
    for (int rack = fxl::kFirstBandRack; rack < fxl::kNumRacks; ++rack)
    {
        int k = 0, bn = 0;
        maps = maps && fxl::bandOfRack (rack, k, bn) && fxl::bandRack (k, bn) == rack && std::string (fxl::bandName (k, bn)).size() >= 3;
    }
    int k0 = 0, b0 = 0;
    check (maps && ! fxl::bandOfRack (2, k0, b0) && fxl::bandsOfSplitter (fxl::kKindSplitterLh) == 2 && fxl::bandsOfSplitter (fxl::kKindSplitterLmh) == 3 && fxl::bandsOfSplitter (fxl::kKindSplitterMs) == 2 && fxl::bandsOfSplitter (synthyboy::kFxTypeEq) == 0, "the seven band racks map to their splitter and band and back");
}
/** FxSlotCard stores nothing: a touch on the power zone reports the value it asks for and leaves `state` and the picture alone; the owner writes the effect and sets `state.on`, and the next draw shows it. */
void testSlotCardStoresNoTruth()
{
    FxSlotCard card;
    card.setId (21);
    card.place ({ 0, 0, 124, 56 });
    card.state.type = synthyboy::kFxTypeEq;
    card.state.on = true;
    const Rect pr = card.powerRect();
    const Image before = drawComponent (card, 200, 100);
    const int px = pr.x + pr.w / 2, py = pr.y + pr.h / 2;
    Change c = card.touchDown (px, py, 0.0);
    check (c.kind == ChangeKind::Index && c.id == 21 && c.index == FxSlotCard::kTogglePower && c.value == 0.0, "FxSlotCard: a touch on the power zone of a card that is on asks for off (0.0)");
    card.touchUp (px, py);
    check (card.state.on && drawComponent (card, 200, 100).px == before.px, "FxSlotCard: the card still shows ON and its picture is the same (the owner has not written back)");
    c = card.touchDown (px, py, 500.0);
    check (c.kind == ChangeKind::Index && c.index == FxSlotCard::kTogglePower && c.value == 0.0, "FxSlotCard: with no write back the same touch asks for the same value (no toggle is kept in the card)");
    card.touchUp (px, py);
    card.state.on = false;   // the owner wrote the effect and sets the state
    check (drawComponent (card, 200, 100).px != before.px, "FxSlotCard: with the owner writing back, the next draw shows OFF");
    c = card.touchDown (px, py, 1000.0);
    check (c.kind == ChangeKind::Index && c.index == FxSlotCard::kTogglePower && c.value == 1.0, "FxSlotCard: a card that is off asks for on (1.0)");
    card.touchUp (px, py);
    // a card of a band's lane (selectOnPower false): the touch up after the power touch selects nothing
    card.state.selectOnPower = false;
    c = card.touchDown (px, py, 2000.0);
    check (c.kind == ChangeKind::Index && card.touchUp (px, py).kind == ChangeKind::None, "FxSlotCard: on a lane card the power touch does not select on lifting");
}

void testNativeControlsLayoutAndHitTesting()
{
    namespace native = synthyboy_mpc::nativeui;
    FxFullScreen fs;
    fs.place ({ 0, 110, 1280, 628 });  // the release FX tab, including its native chain bar
    fs.assignIds (1000, 2000);
    fs.setEditorShown (true);
    auto& ed = fs.slotEditor;
    ed.setType (synthyboy::kFxTypeChorus, true);
    check (! ed.nativeControls(), "the opaque fallback initially owns the editor");
    fs.setNativeControls (true);
    check (ed.nativeControls() && ed.nativeLayout() > 0, "the native chain reserves room for the skin's Chorus cells");

    const auto centerX = [](Rect r) { return r.x + r.w / 2; };
    const auto centerY = [](Rect r) { return r.y + r.h / 2; };
    const auto skinOwns = [&](Rect r)
    {
        const int x = centerX (r), y = centerY (r);
        const Change down = ed.touchDown (x, y, 0.0);
        const Change move = ed.touchMove (x + 6, y + 6);
        const Change up = ed.touchUp (x, y);
        return ! r.empty() && ed.isNativeControl (x, y) && ! down && ! move && ! up;
    };
    // The packaged fxNativeHeader gates show POWER/RACK/MORE/REMOVE for a powered two-page slot.
    check (skinOwns (ed.power.rect()) && skinOwns (ed.rack.rect()) && ed.numPages() == 1 && skinOwns (ed.remove.rect()),
           "the powered pooled header yields all four actions to the skin");
    const auto rate = ed.cellRect (0, 0);
    const auto empty = ed.cellRect (2, 3);
    check (skinOwns (rate) && ! ed.isNativeControl (centerX (empty), centerY (empty)),
           "the Chorus RATE cell yields touch and its empty cell remains overlay-owned");
    const int voices = ed.controlOfKey ("chorus.voices");
    check (voices >= 0 && ed.controlInCell (1, 0) == voices
               && ed.nativeKindOf (voices) == native::kFxFirstDomain + native::kFxVoices
               && skinOwns (ed.cellRect (1, 0)) && ! ed.numbers[voices].enabled(),
           "Chorus VOICES uses the skin's numeric drawer domain and disables the overlay NumberField");

    fs.setEditorShown (false);
    check (! fs.isNativeControl (centerX (rate), centerY (rate)), "the empty FX chain does not expose the saved editor hitbox");
    fs.setEditorShown (true);
    check (fs.isNativeControl (centerX (rate), centerY (rate)), "the editor restores the native RATE hitbox when shown");

    ed.setModuleEnabled (false);
    check (skinOwns (ed.power.rect()) && ! ed.isNativeControl (centerX (rate), centerY (rate))
               && ! ed.isNativeControl (centerX (ed.rack.rect()), centerY (ed.rack.rect()))
               && ! ed.isNativeControl (centerX (ed.remove.rect()), centerY (ed.remove.rect()))
               && ! ed.isNativeControl (centerX (ed.tabs.rect()), centerY (ed.tabs.rect())),
           "a bypassed main view gives the skin only POWER");
    ed.setPage (1);
    check (ed.numPages() == 1, "Chorus has no hidden panel after bypass");

    ed.setPage (0);
    ed.setModuleEnabled (true);
    fs.setNativeControls (false);
    const Rect power = ed.power.rect();
    ed.power.state.on = true;
    ed.touchDown (centerX (power), centerY (power), 10.0);
    const Change fallback = ed.touchUp (centerX (power), centerY (power));
    check (! ed.isNativeControl (centerX (power), centerY (power)) && fallback.kind == ChangeKind::Index
               && fallback.id == ed.power.id() && fallback.index == 0 && voices >= 0 && ed.numbers[voices].enabled(),
           "the opaque fallback handles power and enables the Chorus number field itself");
    fs.setNativeControls (true);

    // Preservation of graphs: EQ display
    ed.setType (synthyboy::kFxTypeEq, true);
    check (ed.hasDisplay() && ed.display.kind == FxDisplay::Kind::Eq, "EQ display is active");
    const Rect disp = ed.displayRect();
    check (! disp.empty(), "EQ display rect is non-empty");
    check (! ed.isNativeControl (disp.x + disp.w / 2, disp.y + disp.h / 2), "EQ graph display is preserved (not native control)");

    // Preservation of graphs: Splitter band lanes
    ed.setType (fxl::kKindSplitterLmh, true);
    check (ed.hasBands(), "splitter bands active");
    const Rect bandsR = ed.displayRect();
    check (! bandsR.empty(), "splitter bands rect is non-empty");
    check (! ed.isNativeControl (bandsR.x + bandsR.w / 2, bandsR.y + bandsR.h / 2), "splitter band board is preserved (not native control)");

    ed.setType (synthyboy::kFxTypeCompressor, true);
    check (ed.nativeBandShown() && ! ed.bandSel.enabled() && ed.isNativeControl (centerX (ed.nativeBandRect()), centerY (ed.nativeBandRect())),
           "the compressor BAND selector belongs to the same native MAIN panel");
    check (! fs.isNativeControl (fs.chainBar.rect().x + 20, fs.chainBar.rect().y + 20),
           "the editor hit test leaves the chain's separate ACTION and MOVE zones to its owner");

    // Main and buses retention with existing editor
    ed.setPath ("MAIN CHAIN");
    check (ed.nativeControls(), "Main chain retains native controls");
    ed.setPath ("BUS 1 CHAIN");
    check (ed.nativeControls(), "Bus 1 chain retains native controls");
    ed.setPath ("BUS 2 CHAIN");
    check (ed.nativeControls(), "Bus 2 chain retains native controls");
}
} // namespace

int main()
{
    testTables();
    testRule();
    testViews();
    testNumberKind();
    testBandBoard();
    testSlotCardStoresNoTruth();
    testNativeControlsLayoutAndHitTesting();
    std::printf ("%s\n", g_fails == 0 ? "ALL PASSED" : "FAILED");
    return g_fails == 0 ? 0 : 1;
}
