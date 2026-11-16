// SynthyBoy - MPC overlay tests: the names of the macros on the plugin-drawn pages (docs/ui-design/macro-names.md): the short form, the card and the chips, the keyboard page (typing, shift, backspace, the limit,
// OK / CANCEL / BACK, the layers, the size of the keys), the name arriving in the state and on the card, the Mod page's lists and the macro chips of a knob.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "fakes.h"
#include "../macro_names.h"
#include "../macro_ui.h"
#include "../mod_ui.h"
#include "../page_view.h"
#include "../scene_cpu.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;
using namespace ovt;

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    if (! ok || std::getenv ("MACRO_NAMES_VERBOSE"))
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

struct Rig
{
    FakeInstance inst;
    UiStack stack;
    MacroUi ui;
    double now = 1000.0;
    Rig()
    {
        stack.openFull (MacroUi::kElementMacros);
        refresh();
    }
    void refresh()
    {
        now += 120.0;
        ui.update (inst, stack, now, page::Input {});
    }
    void tap (int x, int y)
    {
        ui.touch (ev (TouchEvent::Down, x, y), stack, inst, now);
        now += 40.0;
        ui.tick (now, stack, inst);
        ui.touch (ev (TouchEvent::Up, x, y), stack, inst, now);
        now += 50.0;
        refresh();
    }
    void tapRect (const ui::Rect& r) { tap (r.x + r.w / 2, r.y + r.h / 2); }
    void tapName (int m) { tapRect (ui.view().cards[m].nameRect()); }
    /** Taps the key of a character (on the current layer). */
    bool type (char c)
    {
        const int k = ui.keyboard().keyOf (c);
        if (k < 0)
            return false;
        tapRect (ui.keyboard().key (k).r);
        return true;
    }
    void typeText (const std::string& s)
    {
        for (const char c : s)
        {
            if (c == ' ')
                tapRect (ui.keyboard().key (ui.keyboard().keyOfKind (ui::TextEntry::Space)).r);
            else
                type (c);
        }
    }
    void tapKind (ui::TextEntry::KeyKind k) { tapRect (ui.keyboard().key (ui.keyboard().keyOfKind (k)).r); }
    bool keyboardOpen() const { return ui.topPage() == MacroUi::kPageKeyboard; }
};

std::shared_ptr<const synthyboy::StateContent> named (std::initializer_list<std::pair<int, const char*>> names)
{
    auto c = std::make_shared<synthyboy::StateContent>();
    for (const auto& n : names)
        c->macroNames[n.first] = n.second;
    return c;
}
std::string label (const ui::Knob& k) { return k.state.label; }

void testShortForms()
{
    char s[7];
    auto shortOf = [&] (const char* name, int m = 0) { MacroNames::shortForm (name, m, s); return std::string (s); };
    check (shortOf ("", 2) == "MAC 3", "no name: MAC n");
    check (shortOf ("Cutoff") == "Cutoff", "six letters fit as they are");
    check (shortOf ("Filter Sweep") == "Filter", "Filter Sweep -> Filter (the cut falls at the end of a word)");
    check (shortOf ("Big Wobble") == "Big", "Big Wobble -> Big (the cut would fall inside a word)");
    check (shortOf ("Resonance") == "Resona", "one long word: the first letters that fit");
    check (shortOf ("Drive + Warp") == "Drive", "Drive + Warp -> Drive (no dangling sign)");
    check (shortOf ("A + B") == "A + B", "A + B fits");
    check (shortOf ("LFO Rate") == "LFO", "LFO Rate -> LFO");
    check (shortOf ("Hi Cut Rate") == "Hi Cut", "Hi Cut Rate -> Hi Cut");
    check (shortOf ("\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4\xc3\xa4") == "\xc3\xa4\xc3\xa4\xc3\xa4", "non-ASCII: whole characters only, at most six bytes");
    check (shortOf ("\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac") == "\xe2\x82\xac\xe2\x82\xac", "a three byte character: two of them are six bytes");
    bool six = true;
    for (const char* n : { "x", "xxxxxxxxxxxxxxxx", "Wide Wide Wide Wide", "- - - - - - - -", "+", "a b c d e f g" })
        six = six && shortOf (n).size() <= 6 && ! shortOf (n).empty();
    check (six, "never longer than six bytes, never empty");
}

void testMacroSourceMapping()
{
    bool all = true;
    for (int m = 0; m < 8; ++m)
        all = all && macroOfSource (uibind::macroSource (m + 1)) == m;
    check (all, "the eight macro sources map to the macros 0 .. 7");
    check (macroOfSource (1) == -1 && macroOfSource (0) == -1 && macroOfSource (999) == -1 && ! uibind::isMacroSource (7), "no other source is a macro");
    MacroNames n;
    n.load (named ({ { 2, "Filter Sweep" } }).get());
    {
        const NamesScope scope (n);
        check (std::string (uibind::sourceName (uibind::macroSource (3))) == "Filter Sweep" && std::string (uibind::sourceName (uibind::macroSource (4))) == "Macro 4", "sourceName answers with the name of the macro, the default for the others");
        char sn[7];
        uibind::shortName (uibind::macroSource (3), sn);
        check (std::string (sn) == "Filter", "shortName: the short form");
        uibind::shortName (uibind::macroSource (4), sn);
        check (std::string (sn) == "MAC 4", "shortName: MAC n without a name");
    }
    check (std::string (uibind::sourceName (uibind::macroSource (3))) == "Macro 3", "outside a scope the default names answer");
    check (std::string (uibind::sourceName (7)) == synthyboy::modSourceTable()[6].name, "another source is the table's");
}

void testCardsAndChips()
{
    Rig r;
    check (label (r.ui.view().cards[0].knob) == "MACRO 1" && label (r.ui.view().cards[7].knob) == "MACRO 8", "default names on the cards: MACRO n");
    r.inst.setContent (named ({ { 0, "Filter Sweep" }, { 5, "Drive + Warp" } }));
    r.refresh();
    check (label (r.ui.view().cards[0].knob) == "FILTER SWEEP" && label (r.ui.view().cards[5].knob) == "DRIVE + WARP" && label (r.ui.view().cards[1].knob) == "MACRO 2", "the card title is the name (capitals), MACRO n for the others");
    // a long name, 16 wide characters: the label is the card's, cut by the drawing at the width of the text column, never wider than the card
    r.inst.setContent (named ({ { 2, "WWWWWWWWWWWWWWWW" } }));
    r.refresh();
    check (label (r.ui.view().cards[2].knob) == "WWWWWWWWWWWWWWWW", "a sixteen character name is on the card whole");
    const ui::Knob::Geom g = r.ui.view().cards[2].knob.geom();
    check (g.labelSize > 0.0f && ui::Pen::textWidth ("WWWWWWWWWWWWWWWW", g.labelSize) <= static_cast<float> (g.labelBox.w) + 0.5f, "the widest name is fitted into the text column by its size");
    // 23 bytes is all the label holds: a name of sixteen three byte characters is cut at a character
    r.inst.setContent (named ({ { 3, "\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac" } }));
    r.refresh();
    const std::string cut = label (r.ui.view().cards[3].knob);
    check (cut.size() == 21 && cut.compare (0, 3, "\xe2\x82\xac") == 0, "a name longer than the label buffer is cut at a character (21 of 23 bytes: seven characters)");

    // a knob of another page: the chip of a macro that modulates it
    synthyboy::ParamInfo const* cutoff = synthyboy::findParamByKey ("filter1.cutoff");
    const int cutIdx = static_cast<int> (cutoff - synthyboy::paramTable());
    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (0);
    r.inst.set ("mod01.source", static_cast<float> (uibind::macroSource (3)));
    r.inst.set ("mod01.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (cutIdx))));
    r.inst.set ("mod01.amount", 40.0f);
    (void) sp;
    r.inst.setContent (named ({ { 2, "Filter Sweep" } }));
    MacroNames n;
    n.load (r.inst.content().get());
    {
        const NamesScope scope (n);
        const ui::ModInput mi = uibind::modsOf (r.inst, cutIdx, ui::kNoSource, nullptr);
        check (mi.numMods == 1 && std::string (mi.mods[0].name) == "Filter" && mi.mods[0].macro, "the chip of a knob a named macro modulates: the short name, flagged as a macro");
        check (mi.mods[0].colour() == ui::theme::sourceColour ("MAC"), "the chip keeps the colour of the macros whatever the name is");
        ui::ModulationsPage page;
        page.load (mi, "FILTER 1 CUTOFF");
        check (std::string (page.rows[0].name) == "Filter" && page.rows[0].macro, "the row of the modulations page: the short name, the macro colour");
    }
    n.load (nullptr);
    {
        const NamesScope scope (n);
        const ui::ModInput mi = uibind::modsOf (r.inst, cutIdx, ui::kNoSource, nullptr);
        check (std::string (mi.mods[0].name) == "MAC 3", "without a name: MAC 3");
    }
}

void testKeyboardFlow()
{
    Rig r;
    r.inst.setContent (named ({ { 0, "Old" } }));
    r.refresh();
    // a tap on the name opens the keyboard page of the stack
    r.tapName (0);
    check (r.keyboardOpen() && r.stack.depth() == 2 && r.stack.top() == MacroUi::kPageKeyboard && r.ui.editedMacro() == 0, "a tap on the name of macro 1 opens the keyboard page");
    check (r.ui.keyboard().text() == "Old" && r.ui.keyboard().chars() == 3, "the field starts with the name the macro has");
    bool noGroup = r.inst.qGroupCalls > 0;
    for (int i = 0; i < 16; ++i)
        noGroup = noGroup && r.inst.qGroup[i] == -1;
    check (noGroup, "no Q group on the keyboard page: all sixteen slots are without a target");
    r.tapKind (ui::TextEntry::Cancel);
    check (! r.keyboardOpen() && r.stack.depth() == 1 && r.inst.macroRenames.empty() && label (r.ui.view().cards[0].knob) == "OLD", "CANCEL leaves the name and closes the page");
    // BACK (the stack closes the page from outside) is a cancel too
    r.tapName (0);
    r.type ('x');
    r.stack.back();
    r.refresh();
    check (! r.keyboardOpen() && r.inst.macroRenames.empty(), "BACK behaves like CANCEL: nothing is stored");
    // a macro without a name starts empty with shift on; type Cutoff, OK
    r.tapName (4);
    check (r.keyboardOpen() && r.ui.keyboard().text().empty() && r.ui.keyboard().shift() == ui::TextEntry::Shift::Once && r.ui.editedMacro() == 4, "an unnamed macro starts with an empty field and shift on");
    r.typeText ("Cutoff");
    check (r.ui.keyboard().text() == "Cutoff" && r.ui.keyboard().shift() == ui::TextEntry::Shift::Off, "typing: the first letter a capital (shift once), the others lower case");
    r.tapKind (ui::TextEntry::Ok);
    check (! r.keyboardOpen() && r.inst.macroRenames.size() == 1 && r.inst.macroRenames[0].first == 4 && r.inst.macroRenames[0].second == "Cutoff", "OK stores the name through setMacroName");
    check (r.inst.content() != nullptr && r.inst.content()->macroNames[4] == "Cutoff" && label (r.ui.view().cards[4].knob) == "CUTOFF", "the name is in the state's content and on the card");
    // the trimmed name
    r.tapName (1);
    r.tapKind (ui::TextEntry::Space);
    r.tapKind (ui::TextEntry::Space);
    r.type ('a');
    r.tapKind (ui::TextEntry::Space);
    r.tapKind (ui::TextEntry::Ok);
    check (r.inst.macroRenames.back().first == 1 && r.inst.macroRenames.back().second == "a", "OK stores the trimmed name");
    // an empty name is the default
    r.tapName (4);
    r.tapKind (ui::TextEntry::Clear);
    r.tapKind (ui::TextEntry::Ok);
    check (r.inst.macroRenames.back() == std::make_pair (4, std::string()) && label (r.ui.view().cards[4].knob) == "MACRO 5", "an empty name is stored as the default and the card says MACRO 5 again");
    // the targets page of a named macro carries its name
    r.inst.setContent (named ({ { 3, "Wobble" } }));
    r.refresh();
    r.tapRect (r.ui.view().cards[3].chipsRect());
    check (r.ui.targetsOf() == 3, "a tap on the chips still opens the targets page");
}

void testKeyboardKeys()
{
    Rig r;
    r.tapName (2);
    ui::TextEntry& k = r.ui.keyboard();
    // backspace at the start does nothing
    r.tapKind (ui::TextEntry::Backspace);
    check (k.text().empty() && k.chars() == 0, "backspace at the start does nothing");
    // shift: tap = once, again = caps, again = off
    check (k.shift() == ui::TextEntry::Shift::Once, "shift on at the start of an empty text");
    r.tapKind (ui::TextEntry::ShiftKey);
    check (k.shift() == ui::TextEntry::Shift::Caps, "a second tap on shift: caps lock");
    r.typeText ("ab");
    check (k.text() == "AB" && k.shift() == ui::TextEntry::Shift::Caps, "caps lock: capitals until it is switched off");
    r.tapKind (ui::TextEntry::ShiftKey);
    check (k.shift() == ui::TextEntry::Shift::Off, "a third tap on shift: off");
    r.typeText ("cd");
    check (k.text() == "ABcd", "shift off: lower case");
    r.tapKind (ui::TextEntry::ShiftKey);
    r.typeText ("ef");
    check (k.text() == "ABcdEf" && k.shift() == ui::TextEntry::Shift::Off, "shift once: one capital");
    r.tapKind (ui::TextEntry::Backspace);
    check (k.text() == "ABcdE" && k.chars() == 5, "backspace removes the last character");
    // the symbol layer
    r.tapKind (ui::TextEntry::LayerKey);
    check (k.layer() == ui::TextEntry::Layer::Symbols && k.keyOf ('1') >= 0 && k.keyOf ('a') < 0 && k.keyOf ('&') >= 0 && k.keyOf ('+') >= 0, "the second layer has the digits and the symbols, no letters");
    r.typeText ("7+&");
    check (k.text() == "ABcdE7+&", "digits and symbols are typed as they are");
    r.tapKind (ui::TextEntry::LayerKey);
    check (k.layer() == ui::TextEntry::Layer::Letters && k.keyOf ('a') >= 0, "ABC goes back to the letters");
    // space
    r.tapKind (ui::TextEntry::Space);
    check (k.text() == "ABcdE7+& ", "space");
    // the limit: 16 characters, the keys after it do nothing
    r.tapKind (ui::TextEntry::Clear);
    check (k.text().empty() && k.shift() == ui::TextEntry::Shift::Once, "CLEAR empties the field (and a new text starts with a capital)");
    r.typeText ("abcdefghijklmnopqrstuvwxyz");
    check (k.chars() == 16 && k.text().size() == 16 && k.text() == "Abcdefghijklmnop", "the limit is 16 characters");
    r.tapKind (ui::TextEntry::Space);
    r.tapKind (ui::TextEntry::Backspace);
    r.type ('z');
    check (k.text() == "Abcdefghijklmno" "z", "after the limit a key does nothing; a backspace makes room for one more");
    // a finger that slides off the key does nothing
    const int before = k.chars();
    const ui::Rect kr = k.key (k.keyOf ('q')).r;
    r.ui.touch (ev (TouchEvent::Down, kr.x + 10, kr.y + 10), r.stack, r.inst, r.now);
    r.ui.touch (ev (TouchEvent::Move, kr.x + 10, kr.y - 40), r.stack, r.inst, r.now + 20.0);
    r.ui.touch (ev (TouchEvent::Up, kr.x + 10, kr.y - 40), r.stack, r.inst, r.now + 40.0);
    check (k.chars() == before, "a touch that left the key does nothing");
    // multi byte text in the field: backspace removes a whole character
    r.ui.keyboard().begin ("T", "a\xe2\x82\xac", 16, MacroUi::kKeyboardOk, MacroUi::kKeyboardCancel);
    r.tapKind (ui::TextEntry::Backspace);
    check (k.text() == "a" && k.chars() == 1, "backspace takes a whole UTF-8 character");
    r.ui.keyboard().begin ("T", "0123456789ABCDEFGHI", 16, MacroUi::kKeyboardOk, MacroUi::kKeyboardCancel);
    check (k.chars() == 16 && k.text() == "0123456789ABCDEF", "an initial text longer than the limit is cut");
}

void testKeyboardGeometry()
{
    Rig r;
    r.tapName (0);
    ui::TextEntry& k = r.ui.keyboard();
    for (int layer = 0; layer < 2; ++layer)
    {
        int minX = 100000, maxX = 0, minSide = 100000, outside = 0, overlaps = 0;
        for (int i = 0; i < k.numKeys(); ++i)
        {
            const ui::Rect a = k.key (i).r;
            minX = std::min (minX, a.x);
            maxX = std::max (maxX, a.right());
            minSide = std::min (minSide, std::min (a.w, a.h));
            outside += ! k.rect().contains (a);
            for (int j = i + 1; j < k.numKeys(); ++j)
                overlaps += a.overlaps (k.key (j).r);
        }
        const std::string what = layer == 0 ? "letters" : "symbols";
        check (minSide >= 56, what + ": every key is at least 56 x 56 (" + std::to_string (minSide) + ")");
        check (minX <= 24 && maxX >= 1280 - 24, what + ": the rows use the whole width (" + std::to_string (minX) + " .. " + std::to_string (maxX) + ")");
        check (outside == 0 && overlaps == 0, what + ": the keys lie inside the tab and none overlaps another");
        check (k.keyOfKind (ui::TextEntry::Ok) >= 0 && k.keyOfKind (ui::TextEntry::Cancel) >= 0 && k.keyOfKind (ui::TextEntry::Clear) >= 0 && k.keyOfKind (ui::TextEntry::Backspace) >= 0 && k.keyOfKind (ui::TextEntry::Space) >= 0 && k.keyOfKind (ui::TextEntry::LayerKey) >= 0, what + ": SPACE, backspace, CLEAR, CANCEL, OK and the layer key are there");
        r.tapKind (ui::TextEntry::LayerKey);
    }
    check (k.minSize().h <= ui::Rect { 0, 0, 1280, 628 }.h, "the page fits the 628 pixel tab");
    // a picture: the page draws inside the tab
    auto data = std::make_unique<gpu::SceneData>();
    std::memset (data.get(), 0, sizeof (gpu::SceneData));
    gpu::SceneBuilder sb (*data);
    sb.begin (gpu::kKindPlay, 0, 1280, 800);
    r.ui.draw (sb);
    check (! sb.overflow && data->numOps > 30, "the keyboard page fits the scene (" + std::to_string (data->numOps) + " ops)");
    std::vector<uint32_t> cv;
    renderScene (*data, cv);
    check (cv.size() == 1280u * 800u, "the CPU renderer draws the keyboard page");
}

void testPageViewScopeAndModPage()
{
    // the page view loads the names of its instance for everything it does
    FakeInstance inst;
    PageView<MacroUi> view ("macros", 1);
    UiStack stack;
    stack.openFull (MacroUi::kElementMacros);
    inst.setContent (named ({ { 1, "Drive" } }));
    ViewInput in;
    in.io = &inst;
    in.ui = &stack;
    in.instance = &inst;
    in.nowMs = 1000.0;
    view.update (in);
    check (label (view.ui().view().cards[1].knob) == "DRIVE", "the macros page view shows the name");
    // the Mod page: the board, the rows
    PageView<ModUi> mod ("mod", 1);
    inst.set ("mod01.source", static_cast<float> (uibind::macroSource (2)));
    inst.set ("mod01.destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (synthyboy::findParamByKey ("filter1.cutoff") - synthyboy::paramTable()))));
    inst.set ("mod01.amount", 30.0f);
    in.nowMs = 1100.0;
    mod.update (in);
    bool boardNamed = false, boardDefault = false;
    const ui::ModSourceBoard& b = mod.ui().matrix().board;
    for (int i = 0; i < b.numSources(); ++i)
    {
        boardNamed = boardNamed || std::string (b.sourceLabel (i)) == "DRIVE";
        boardDefault = boardDefault || std::string (b.sourceLabel (i)) == "MACRO 2";
    }
    check (boardNamed && ! boardDefault, "the source board of the Mod page lists the macro by its name");
    check (std::string (mod.ui().matrix().slotData (0).src) == "Drive", "the matrix row names the macro");
    inst.setContent (nullptr);
    in.nowMs = 1200.0;
    mod.update (in);
    bool back = false;
    for (int i = 0; i < b.numSources(); ++i)
        back = back || std::string (b.sourceLabel (i)) == "MACRO 2";
    check (back && std::string (mod.ui().matrix().slotData (0).src) == "Macro 2", "a preset without names: the default names come back on the Mod page");
}
} // namespace

int main()
{
    testShortForms();
    testMacroSourceMapping();
    testCardsAndChips();
    testKeyboardFlow();
    testKeyboardKeys();
    testKeyboardGeometry();
    testPageViewScopeAndModPage();
    std::printf (failures == 0 ? "macro_names_host: OK\n" : "macro_names_host: FAILED (%d failures)\n", failures);
    return failures == 0 ? 0 : 1;
}
