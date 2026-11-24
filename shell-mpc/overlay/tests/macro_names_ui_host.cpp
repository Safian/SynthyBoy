// SynthyBoy - MPC overlay tests: the names of the macros on the plugin-drawn pages (docs/ui-design/macro-names.md): the short form, the card and the chips, the keyboard page (typing, shift, backspace, the limit,
// OK / CANCEL / BACK, the layers, the size of the keys), the name arriving in the state and on the card, the Mod page's lists and the macro chips of a knob.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "fakes.h"
#include "demo_common.h"
#include "../gpu/cell_plan.h"
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
    if (! ok || std::getenv ("MACRO_NAMES_UI_HOST_VERBOSE"))
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

struct NativeMacroInstance : FakeInstance
{
    int pendingMacroTarget = -1;
    bool macroTargetsPublished = false;
    bool nativeMacroTargetsSupported() const noexcept override { return true; }
    void publishNativeMacroTargets (bool on) noexcept override { macroTargetsPublished = on; }
    int takeNativeMacroTarget() noexcept override
    {
        const int target = macroTargetsPublished ? pendingMacroTarget : -1;
        pendingMacroTarget = -1;
        return target;
    }
};

struct Rig
{
    NativeMacroInstance inst;
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
    void target (int m) { inst.pendingMacroTarget = m; refresh(); }
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
std::string label (const ui::MacroCard& c) { return c.macroName(); }

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
    check (label (r.ui.view().cards[0]) == "MACRO 1" && label (r.ui.view().cards[7]) == "MACRO 8", "default names on the cards: MACRO n");
    r.inst.setContent (named ({ { 0, "Filter Sweep" }, { 5, "Drive + Warp" } }));
    r.refresh();
    check (label (r.ui.view().cards[0]) == "FILTER SWEEP" && label (r.ui.view().cards[5]) == "DRIVE + WARP" && label (r.ui.view().cards[1]) == "MACRO 2", "the card title is the name (capitals), MACRO n for the others");
    // a long name, 16 wide characters: the label is the card's, fitted/ellipsised into nameLabelRect, never wider than the card
    r.inst.setContent (named ({ { 2, "WWWWWWWWWWWWWWWW" } }));
    r.refresh();
    check (label (r.ui.view().cards[2]) == "WWWWWWWWWWWWWWWW", "a sixteen character name is on the card whole");
    const ui::Rect nb = r.ui.view().cards[2].nameLabelRect();
    const auto& T = ui::theme::L();
    const float fitSz = ui::Pen::fitSize ("WWWWWWWWWWWWWWWW", static_cast<float> (nb.w), static_cast<float> (nb.h), T.fLabel, T.fMin);
    char fittedBuf[96] = {};
    const float fittedW = ui::sdf::fitText ("WWWWWWWWWWWWWWWW", fitSz, static_cast<float> (nb.w), fittedBuf, static_cast<int> (sizeof fittedBuf));
    check (fitSz > 0.0f && fittedW <= static_cast<float> (nb.w) + 0.5f, "the widest name fits inside nameLabelRect via fit/ellipsis");
    // 23 bytes is all the label holds: a name of sixteen three byte characters is cut at a character
    r.inst.setContent (named ({ { 3, "\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac" } }));
    r.refresh();
    const std::string cut = label (r.ui.view().cards[3]);
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
    // Owners use slotOf to prevent adding the same source twice, including routes beyond the visible rows.
    for (int slot = 0; slot < 9; ++slot)
    {
        const auto route = synthyboy::modSlotParams (slot);
        r.inst.set (synthyboy::paramTable()[route.source].key, static_cast<float> (slot + 1));
        r.inst.set (synthyboy::paramTable()[route.destination].key, r.inst.param (sp.destination));
    }
    check (uibind::slotOf (r.inst, cutIdx, 9) == 8, "a ninth modulation route is found without duplicating it");
    check (uibind::slotOf (r.inst, cutIdx, 10) < 0, "unassigned source remains available");

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
    check (! r.keyboardOpen() && r.stack.depth() == 1 && r.inst.macroRenames.empty() && label (r.ui.view().cards[0]) == "OLD", "CANCEL leaves the name and closes the page");
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
    check (r.inst.content() != nullptr && r.inst.content()->macroNames[4] == "Cutoff" && label (r.ui.view().cards[4]) == "CUTOFF", "the name is in the state's content and on the card");
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
    check (r.inst.macroRenames.back() == std::make_pair (4, std::string()) && label (r.ui.view().cards[4]) == "MACRO 5", "an empty name is stored as the default and the card says MACRO 5 again");
    // the targets page of a named macro carries its name
    r.inst.setContent (named ({ { 3, "Wobble" } }));
    r.refresh();
    r.target (3);
    check (r.ui.targetsOf() == 3, "native TARGETS opens the named macro targets page");
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
    NativeMacroInstance inst;
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
    check (label (view.ui().view().cards[1]) == "DRIVE", "the macros page view shows the name");
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

void testStandaloneMacrosNativeControls()
{
    Rig rig;
    check (rig.ui.nativeControls(), "Macros native controls active by default");
    check (! rig.ui.hostBackground(), "native dials do not imply a verified host background");
    check (rig.ui.view().cards[0].numChildren() == 0, "native knob is not a GPU child");

    rig.ui.view().setInactive (true);
    bool inactive = true;
    for (const auto& card : rig.ui.view().cards)
        inactive = inactive && card.inactive() && card.knob.inactive();
    check (inactive, "native card state/Q adapters inherit the inactive view state");
    rig.ui.view().setInactive (false);

    // Geometry & bounds of the 8 native dials in one bottom row across 1280px
    for (int m = 0; m < 8; ++m)
    {
        check (rig.ui.view().cards[m].numChildren() == 0, "card has no GPU dial child");
        check (rig.ui.view().cards[m].knob.enabled(), "native dial state adapter retains Q routing");

        const int cardX = ui::macrocard::kCardPositions[m].x;
        const int cardY = 110 + ui::macrocard::kCardPositions[m].y;
        const int dialX = cardX + ui::macrocard::kDialRect.x;
        const int dialY = cardY + ui::macrocard::kDialRect.y;
        const int dialW = ui::macrocard::kDialRect.w, dialH = ui::macrocard::kDialRect.h;

        const ui::Rect dialRect = rig.ui.view().dialArea (m);
        check (dialRect.x == dialX && dialRect.y == dialY && dialRect.w == dialW && dialRect.h == dialH,
               "Macro " + std::to_string (m + 1) + " dial geometry matches shared MacroCard contract");
        check (rig.ui.view().cards[m].rect().y == 110 + 382,
               "Macro " + std::to_string (m + 1) + " sits in single bottom row");

        const ui::Rect knobR = rig.ui.view().cards[m].nativeKnobArea();
        const ui::Rect nameR = rig.ui.view().cards[m].nameRect();
        const ui::Rect nameLabelR = rig.ui.view().cards[m].nameLabelRect();
        const ui::Rect valueR { cardX + ui::macrocard::kValueRect.x, cardY + ui::macrocard::kValueRect.y,
                                ui::macrocard::kValueRect.w, ui::macrocard::kValueRect.h };
        const ui::Rect targetR = rig.ui.view().cards[m].nativeTargetArea();

        // All interactive hitboxes >= 56 px in smaller dimension, strictly non-overlapping
        check (knobR.w >= 56 && knobR.h >= 56 && nameR.w >= 56 && nameR.h >= 56 && targetR.w >= 56 && targetR.h >= 56,
               "card " + std::to_string (m + 1) + " interactive hitboxes meet 56 px minimum");
        check (! knobR.overlaps (nameR) && ! knobR.overlaps (targetR) && ! nameR.overlaps (knobR) && ! nameR.overlaps (valueR) && ! nameR.overlaps (targetR),
               "card " + std::to_string (m + 1) + " hitboxes and value do not overlap");
        check (nameLabelR.y - valueR.bottom() >= 12,
               "card " + std::to_string (m + 1) + " maintains clear vertical gap between value and name");

        // Dial rotary area is a native control
        check (rig.ui.view().isNativeDial (knobR.x + knobR.w / 2, knobR.y + knobR.h / 2),
               "isNativeDial matches inside rotary dial " + std::to_string (m + 1));
        check (rig.ui.view().isNativeControl (knobR.x + knobR.w / 2, knobR.y + knobR.h / 2),
               "isNativeControl matches inside rotary dial " + std::to_string (m + 1));

        // Macro name button inside the card is NOT a native control: owned by overlay for keyboard entry
        check (! rig.ui.view().isNativeDial (nameR.x + 10, nameR.y + 10),
               "name button is not native dial on card " + std::to_string (m + 1));
        check (! rig.ui.view().isNativeControl (nameR.x + 10, nameR.y + 10),
               "name button is not native control on card " + std::to_string (m + 1));
        check (rig.ui.covers (nameR.x + 10, nameR.y + 10),
               "covers returns true over name button on card " + std::to_string (m + 1));

        // Target / chips area is not native dial
        check (! rig.ui.view().isNativeDial (targetR.x + 10, targetR.y + 10),
               "target area is not native dial");
    }

    // Touch passthrough verification on main page
    check (rig.ui.topPage() == 0, "on main macros page");
    const ui::Rect knob0 = rig.ui.view().cards[0].nativeKnobArea();
    const int dialTapX = knob0.x + knob0.w / 2, dialTapY = knob0.y + knob0.h / 2;
    // 1. Touch on native dial rotary area passes through to host
    check (! rig.ui.covers (dialTapX, dialTapY), "covers returns false over native Macro 1 dial");
    bool touchDownHandled = rig.ui.touch (ev (TouchEvent::Down, dialTapX, dialTapY), rig.stack, rig.inst, rig.now);
    check (! touchDownHandled, "touchDown on native dial passes through (returns false)");

    // 2. Card / dial touch focuses bank (0/1) for Gen1 frame without changing fixed Q-Link target slots
    rig.ui.setBank (0);
    rig.refresh();
    check (rig.ui.bank() == 0, "starts on bank 0 focus");
    const ui::Rect knob4 = rig.ui.view().cards[4].nativeKnobArea();
    rig.ui.touch (ev (TouchEvent::Down, knob4.x + knob4.w / 2, knob4.y + knob4.h / 2), rig.stack, rig.inst, rig.now);
    check (rig.ui.bank() == 1, "touching Bank 2 dial updates visual bank focus to 1");
    const ui::Rect name0 = rig.ui.view().cards[0].nameRect();
    check (rig.ui.covers (name0.x + 10, name0.y + 10), "covers returns true over Macro 1 name button");
    rig.tap (name0.x + 10, name0.y + 10);
    check (rig.keyboardOpen(), "tapping Macro 1 name button opens keyboard page in native mode");
    check (rig.ui.editedMacro() == 0, "keyboard edits Macro 1");
    // Cancel keyboard to return to main page
    rig.tapKind (ui::TextEntry::Cancel);
    check (! rig.keyboardOpen(), "cancelling keyboard returns to main page");
    check (rig.ui.topPage() == 0, "back on main macros page");
    check (rig.ui.bank() == 0, "tapping card 0 returned visual bank focus to 0");

    // The host's TARGETS action opens the same targets page.
    const ui::Rect target0 = rig.ui.view().cards[0].nativeTargetArea();
    check (! rig.ui.covers (target0.x + 20, target0.y + 20), "native TARGETS passes through");
    rig.target (0);
    check (rig.ui.topPage() == MacroUi::kPageTargets, "native TARGETS opens modal page");

    // 5. While targets modal is open, overlay covers entire tab (no touch passthrough)
    check (rig.ui.covers (dialTapX, dialTapY), "covers returns true over dials while modal is open");
    // Tap BACK to return to main page
    rig.tapRect (rig.ui.targetsPage().back.rect());
    check (rig.ui.topPage() == 0, "tapping back returns to main page");

    // 6. Disabled state check: disabled view reports no active native controls
    rig.ui.view().setEnabled (false);
    check (! rig.ui.view().isNativeControl (dialTapX, dialTapY), "disabled view does not report active native controls");
    rig.ui.view().setEnabled (true);

    // Q-Link Routing verification: fixed Bank 1 (Q 1..4 = macro.1..4) and Bank 2 (Q 5..8 = macro.5..8)
    // Never swapped regardless of bank focus or 4-knob / 16-knob hardware mode.
    for (const bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        for (int bankFocus : { 0, 1 })
        {
            rig.ui.setBank (bankFocus);
            rig.refresh();
            const int* q = rig.ui.qTargets();
            for (int k = 0; k < 8; ++k)
                check (q[k] == rig.ui.macroParam (k),
                       std::string (wide ? "XL" : "Key61") + " bankFocus=" + std::to_string (bankFocus) + " Q " + std::to_string (k + 1) + " is fixed Macro " + std::to_string (k + 1));
        }
    }
    ui::QGroup::setWideDevice (false);
    rig.ui.setBank (0);
    rig.refresh();

    // Hardware Q-Link turn on 4-knob device moves visual bank focus to the turned bank without swapping Q targets
    rig.inst.qWrites.fetch_add (1);
    rig.inst.set ("macro.5", 42.0f);
    rig.refresh();
    check (rig.ui.bank() == 1, "hardware Q-Link turn in Bank 2 moves Gen1 visual focus to Bank 2");
    check (rig.ui.qTargets()[0] == rig.ui.macroParam (0) && rig.ui.qTargets()[4] == rig.ui.macroParam (4),
           "Bank 1 and Bank 2 Q targets stay fixed after hardware turn in Bank 2");
    rig.inst.qWrites.fetch_add (1);
    rig.inst.set ("macro.1", 61.0f);
    rig.refresh();
    check (rig.ui.bank() == 0, "hardware Q-Link turn in Bank 1 returns Gen1 visual focus to Bank 1");

    // Live spectrum curve occupies full-width upper studio display:
    const ui::Rect specRect = rig.ui.view().spectrum.rect();
    check (specRect.x == ui::macrocard::kSpectrumRect.x && specRect.y == 110 + ui::macrocard::kSpectrumRect.y
           && specRect.w == ui::macrocard::kSpectrumRect.w && specRect.h == ui::macrocard::kSpectrumRect.h,
           "Live spectrum curve occupies full-width upper studio area");
    for (int m = 0; m < 8; ++m)
        check (! specRect.overlaps (rig.ui.view().cards[m].rect()), "spectrum does not overlap card " + std::to_string (m + 1));
}

void testStandaloneMacrosDifferingStates()
{
    Rig r;
    // State 1: Default instance state (fixed titles and default MACRO n names)
    check (r.ui.nativeControls(), "nativeControls defaults to true");
    check (r.ui.cardName (0) == "MACRO 1" && r.ui.cardName (7) == "MACRO 8",
           "State 1: default instance has fixed titles MACRO 1 .. MACRO 8");
    check (label (r.ui.view().cards[0]) == "MACRO 1" && label (r.ui.view().cards[7]) == "MACRO 8",
           "State 1: card macro name matches default fixed title");
    check (! r.ui.view().cards[0].knob.state.side, "MacroCard knob configured in vertical stacked mode");
    check (! r.ui.view().cards[0].knob.state.small, "MacroCard uses the full 90px dial in its native card");

    // Rotary dial touch passthrough in State 1
    const ui::Rect dial0 = r.ui.view().dialArea (0);
    const ui::Rect knob0 = r.ui.view().cards[0].nativeKnobArea();
    check (dial0.w == ui::macrocard::kDialRect.w && dial0.h == ui::macrocard::kDialRect.h,
           "Macro 1 dialArea matches shared contract");
    check (! r.ui.covers (knob0.x + 40, knob0.y + 40), "covers is false over rotary knob in State 1");
    check (! r.ui.touch (ev (TouchEvent::Down, knob0.x + 40, knob0.y + 40), r.stack, r.inst, r.now),
           "rotary knob touch passes through in State 1");

    // Name button interception in State 1
    const ui::Rect name0 = r.ui.view().cards[0].nameRect();
    check (r.ui.covers (name0.x + 10, name0.y + 10), "covers is true over name button in State 1");

    // State 2: Differing non-default instance state (custom instance-specific names)
    r.inst.setContent (named ({ { 0, "Cutoff" }, { 1, "Drive + Warp" }, { 5, "Space Verb" } }));
    r.refresh();

    check (r.ui.cardName (0) == "CUTOFF", "State 2: Card 0 reflects instance-specific name CUTOFF");
    check (r.ui.cardName (1) == "DRIVE + WARP", "State 2: Card 1 reflects instance-specific name DRIVE + WARP");
    check (r.ui.cardName (5) == "SPACE VERB", "State 2: Card 6 reflects instance-specific name SPACE VERB");
    check (r.ui.cardName (2) == "MACRO 3", "State 2: Unnamed Card 2 retains default fixed title MACRO 3");
    check (label (r.ui.view().cards[0]) == "CUTOFF", "State 2: card name matches instance-specific CUTOFF");
    check (label (r.ui.view().cards[1]) == "DRIVE + WARP", "State 2: card name matches instance-specific DRIVE + WARP");
    check (label (r.ui.view().cards[2]) == "MACRO 3", "State 2: card name matches default fixed title MACRO 3");

    // Rotary dial touch passthrough remains active in State 2
    check (! r.ui.covers (knob0.x + 40, knob0.y + 40), "covers is false over rotary knob in State 2");
    check (! r.ui.touch (ev (TouchEvent::Down, knob0.x + 40, knob0.y + 40), r.stack, r.inst, r.now),
           "rotary knob touch passes through in State 2");

    // Keyboard modal flow on instance-specific name button
    r.tap (name0.x + 10, name0.y + 10);
    check (r.keyboardOpen(), "tapping name button opens keyboard modal in State 2");
    check (r.ui.editedMacro() == 0, "keyboard edits Macro 1");
    // Cancel keyboard leaves instance name unchanged
    r.tapKind (ui::TextEntry::Cancel);
    check (! r.keyboardOpen(), "cancelling returns to main page");
    check (r.ui.cardName (0) == "CUTOFF", "name remains CUTOFF after cancel");

    // Type a new instance-specific name through keyboard
    r.tap (name0.x + 10, name0.y + 10);
    check (r.keyboardOpen(), "reopen keyboard");
    r.tapKind (ui::TextEntry::Clear);
    r.typeText ("Acid");
    r.tapKind (ui::TextEntry::Ok);
    check (! r.keyboardOpen(), "pressing OK closes keyboard");
    check (r.ui.cardName (0) == "ACID", "instance-specific name updated to ACID after rename");
    check (label (r.ui.view().cards[0]) == "ACID", "card name updated to ACID");

    // Host availability changes never restore the retired GPU controls
    r.ui.setNativeControls (false);
    r.refresh();
    check (! r.ui.nativeControls(), "nativeControls toggled to false");
    check (r.ui.covers (knob0.x + 40, knob0.y + 40), "covers returns true over knob when nativeControls is false");
    check (r.ui.cardName (0) == "ACID", "instance-specific name ACID preserved with nativeControls false");
    check (r.ui.cardName (1) == "DRIVE + WARP", "instance-specific name DRIVE + WARP preserved");
    check (r.ui.cardName (2) == "MACRO 3", "fixed title MACRO 3 preserved");

    const int writes = r.inst.uiWrites.load();
    r.tapRect (knob0);
    check (r.inst.uiWrites.load() == writes, "unavailable host never enables GPU knob input");
    for (bool available : { false, true })
        for (bool enabled : { false, true })
            for (bool hostGround : { false, true })
            {
                r.ui.setNativeControls (available);
                r.ui.view().setEnabled (enabled);
                r.ui.setHostBackground (hostGround);
                r.refresh();
                auto data = std::make_unique<gpu::SceneData>();
                gpu::SceneBuilder sb (*data);
                sb.begin (gpu::kKindPlay, 0, 1280, 800);
                r.ui.draw (sb);
                bool obsolete = false;
                for (uint32_t op = 0; op < data->numOps; ++op)
                {
                    const auto& draw = data->ops[op];
                    obsolete |= draw.op == gpu::kOpArc || draw.op == gpu::kOpDisc;
                    if (draw.op == gpu::kOpText)
                    {
                        const auto text = gpu::textString (*data, draw);
                        obsolete |= text == "NO TARGET" || text == "NO TARGET - TAP TO ADD";
                    }
                }
                check (! obsolete, "all host/pass/enabled states omit GPU macro dials and target chips");
            }
    r.ui.view().setEnabled (true);
    r.ui.setHostBackground (false);

    // Toggling native controls back to true
    r.ui.setNativeControls (true);
    r.refresh();
    check (r.ui.nativeControls(), "nativeControls toggled back to true");
    check (! r.ui.covers (knob0.x + 40, knob0.y + 40), "rotary passthrough restored");
    check (r.ui.cardName (0) == "ACID", "instance-specific name ACID intact");
}
void testNativeMacrosPageLifecycle()
{
    struct Touch final : ITouchInput
    {
        std::vector<TouchEvent> pending;
        void configure (int, int, int) override {}
        int poll (const std::function<void (const TouchEvent&)>& send) override
        {
            const int count = static_cast<int> (pending.size());
            for (const auto& event : pending)
                send (event);
            pending.clear();
            return count;
        }
    } touch;
    struct Instance : demo::Inst
    {
        int renames = 0;
        void setMacroName (int, const std::string&) override { ++renames; }
    } first, second;
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    Timing timing;
    timing.nativeTabTouch = true;
    SynthyBoyController controller (plane, frame, renderer, dir.paths(), timing, 10.0, nullptr, &touch);
    const int firstSlot = controller.attach (&first), secondSlot = controller.attach (&second);
    auto index = [] (const char* name)
    {
        for (int i = 0; i < layout::kNumViews; ++i)
            if (std::strcmp (layout::kViews[i].name, name) == 0)
                return i;
        return -1;
    };
    const int play = index ("play1"), macros = index ("macros");
    if (play < 0 || macros < 0 || ! hasHostBackground ("play1") || ! hasHostBackground ("macros"))
    {
        check (false, "the release registers Play and Macros with their own native backgrounds");
        return;
    }
    first.contentV = named ({{ 0, "Cutoff" }, { 7, "First tail" }});
    second.contentV = named ({{ 0, "Drive" }, { 7, "Second tail" }});
    for (int i = 0; i < 8; ++i)
    {
        const std::string key = "macro." + std::to_string (i + 1);
        first.set (key, 10.0f * (i + 1));
        second.set (key, 10.0f * (8 - i));
    }
    SpectrumRing ring;
    first.ring = &ring;
    std::vector<float> signal (4096);
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = 0.5f * static_cast<float> (std::sin (2.0 * 3.14159265 * 1000.0 * i / 48000.0));
    ring.push (signal.data(), nullptr, static_cast<int> (signal.size()));
    double now = 0.0;
    auto run = [&] (double ms)
    {
        const double until = now + ms;
        while (now < until)
            controller.tick (now += 10.0);
    };
    auto mark = [&] (int view, int slot, bool show)
    {
        frame.setView (view, show, 270);
        frame.setViewInstance (view, slot, show, 270);
        const auto* bg = backgroundOf (layout::kViews[view].name);
        frame.setUiRect (bg->sentinelX, page::kTabY + bg->sentinelY, 5, 5, bg->sentinelRgb, show, 270);
    };
    auto finger = [&] (TouchEvent::Kind kind)
    {
        touch.pending.push_back (ev (kind, 107, 770));
        run (20);
    };
    auto tap = [&] (ui::Rect r)
    {
        controller.touchEvent (ev (TouchEvent::Down, r.x + r.w / 2, r.y + r.h / 2), now);
        controller.touchEvent (ev (TouchEvent::Up, r.x + r.w / 2, r.y + r.h / 2), now + 5.0);
        run (160);
    };
    auto activeMacros = [&] { return dynamic_cast<const pages::MacrosPage*> (controller.currentView()); };
    auto pixels = std::vector<uint32_t> (1280u * 800u);
    auto paint = [&]
    {
        auto* view = const_cast<IView*> (controller.currentView());
        if (view != nullptr)
            view->render (pixels.data(), 1280, 800);
    };
    auto alpha = [&] (int x, int y) { return pixels[static_cast<size_t> (y) * 1280 + x] >> 24; };

    mark (play, firstSlot, true);
    run (300);
    finger (TouchEvent::Down);
    check (! plane.planeOn && controller.currentIndex() == play, "Play key down withdraws the outgoing overlay");
    finger (TouchEvent::Up);
    mark (play, firstSlot, false);
    mark (macros, firstSlot, true);
    run (300);
    ring.push (signal.data(), nullptr, static_cast<int> (signal.size()));
    run (40);
    const auto* root = activeMacros();
    if (root == nullptr)
    {
        check (false, "the Play key reaches the ordinary Macros PageView");
        return;
    }
    check (root->ui().hostBackground() && ! first.fullOpen && controller.activeSlot() == firstSlot,
           "the Macros root keeps its instance marker and native controls visible");
    check (root->ui().cardName (0) == "CUTOFF" && root->ui().cardName (7) == "FIRST TAIL", "the first page shows its own editable names");
    bool valuesMatch = true;
    for (int m = 0; m < 8; ++m)
    {
        const int p = root->ui().macroParam (m);
        valuesMatch &= std::fabs (root->ui().view().cards[m].knob.state.value - uibind::normOf (p, first.param (p))) < 1e-6;
    }
    check (valuesMatch, "all eight cards read the same real macro values as their native controls");
    paint();
    bool clear = true;
    for (const auto& card : root->ui().view().cards)
    {
        const auto r = card.nativeKnobArea();
        for (int y = r.y; y < r.bottom(); ++y)
            for (int x = r.x; x < r.right(); ++x)
                clear &= alpha (x, y) == 0;
    }
    const auto* spectrum = root->ui().view().spectrum.levels;
    check (clear, "the regular PageView is clear over every native rotary rectangle");
    check (alpha (163, 560) == 0, "the verified host background is visible between cards");
    check (spectrum != nullptr && *std::max_element (spectrum, spectrum + ui::SpectrumStrip::kPoints) > 0.3f,
           "the ordinary PageView samples its instance's output spectrum");

    const auto* macrosBg = backgroundOf ("macros");
    frame.setUiRect (macrosBg->sentinelX, page::kTabY + macrosBg->sentinelY, 5, 5, macrosBg->sentinelRgb, false, 270);
    run (200);
    paint();
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder builder (*scene);
    gpu::CellPlan plan;
    check (! root->ui().hostBackground() && alpha (163, 560) == 255 && alpha (84, 541) == 0
           && root->describe (builder, macros, 1280, 800) && gpu::planCells (*scene, plan),
           "without the sentinel the fallback fills the tab around its native apertures and remains cacheable");
    mark (macros, firstSlot, true);
    run (160);

    tap (root->ui().view().cards[7].nativeKnobArea());
    check (first.writes == 0 && root->ui().bank() == 1 && first.q[0] == root->ui().macroParam (0) && first.q[4] == root->ui().macroParam (4),
           "a native dial changes visual bank focus without swapping fixed Bank 1/2 Q targets or writing parameters");
    tap (root->ui().view().cards[0].nameRect());
    paint();
    check (first.fullOpen && root->ui().topPage() == MacroUi::kPageKeyboard && ! root->ui().hostBackground() && alpha (40, 250) == 255,
           "the name editor closes native widgets and paints an opaque modal through PageView");
    first.contentV = named ({{ 0, "Preset name" }});
    ++first.generation;
    run (200);
    check (! first.fullOpen && root->ui().topPage() == 0 && root->ui().cardName (0) == "PRESET NAME" && first.renames == 0,
           "preset recall cancels the old rename and restores the live native root");

    frame.setViewInstance (macros, secondSlot, true, 270);
    run (260);
    root = activeMacros();
    check (root != nullptr && controller.activeSlot() == secondSlot && root->ui().cardName (0) == "DRIVE"
           && root->ui().cardName (7) == "SECOND TAIL" && root->ui().view().spectrum.levels == nullptr && ! second.fullOpen,
           "switching instances refreshes names and clears the previous instance's spectrum");
    finger (TouchEvent::Down);
    check (! plane.planeOn && controller.currentIndex() == macros, "Macros key down withdraws its overlay before returning to Play");
    finger (TouchEvent::Up);
    mark (macros, secondSlot, false);
    mark (play, secondSlot, true);
    run (300);
    check (plane.planeOn && controller.currentIndex() == play && ! second.fullOpen && second.writes == 0,
           "the return to Play restores its main stack without a stray write");
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
    testStandaloneMacrosNativeControls();
    testStandaloneMacrosDifferingStates();
    testNativeMacrosPageLifecycle();
    std::printf (failures == 0 ? "macro_names_ui_host: OK\n" : "macro_names_ui_host: FAILED (%d failures)\n", failures);
    return failures == 0 ? 0 : 1;
}
