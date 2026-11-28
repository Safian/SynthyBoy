// SynthyBoy - MPC plugin-drawn layer tests: TextEntry (the keyboard page): the keys and their size, typing, shift / caps, the layers, backspace, CLEAR, the limit (characters, not bytes), OK / CANCEL as requests,
// a finger that slides off a key, a disabled page, the drawing inside the tab.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"

using namespace uit;

namespace
{
const Rect kTab { 0, 110, 1280, 628 };

void tapKey (TextEntry& e, int k, Change* out = nullptr)
{
    const Rect r = e.key (k).r;
    e.touchDown (r.x + r.w / 2, r.y + r.h / 2, 0.0);
    const Change c = e.touchUp (r.x + r.w / 2, r.y + r.h / 2);
    if (out != nullptr)
        *out = c;
}
}

int main()
{
    TextEntry e;
    e.place (kTab);
    e.begin ("NAME OF MACRO 3", "", 16, 7, 8);
    check (e.sizeOk() && e.numKeys() >= 30, "the page fits the tab (minimum " + std::to_string (e.minSize().w) + " x " + std::to_string (e.minSize().h) + "), " + std::to_string (e.numKeys()) + " keys");
    // the keys: every one at least 56 x 56 inside the tab, none over another, the rows use the whole width, on both layers
    for (int layer = 0; layer < 2; ++layer)
    {
        int minX = 100000, maxX = 0, bad = 0, over = 0;
        for (int i = 0; i < e.numKeys(); ++i)
        {
            const Rect r = e.key (i).r;
            bad += r.w < 56 || r.h < 56 || ! kTab.contains (r);
            minX = std::min (minX, r.x);
            maxX = std::max (maxX, r.right());
            for (int j = i + 1; j < e.numKeys(); ++j)
                over += r.overlaps (e.key (j).r);
        }
        check (bad == 0 && over == 0 && minX <= 24 && maxX >= 1256, std::string (layer ? "symbols" : "letters") + ": keys of at least 56 x 56 inside the tab, no overlap, the whole width (" + std::to_string (minX) + " .. " + std::to_string (maxX) + ")");
        tapKey (e, e.keyOfKind (TextEntry::LayerKey));
    }
    check (e.layer() == TextEntry::Layer::Letters, "two taps on the layer key: back on the letters");
    // typing and shift
    check (e.shift() == TextEntry::Shift::Once && e.text().empty(), "begin: empty, shift once");
    tapKey (e, e.keyOf ('f'));
    tapKey (e, e.keyOf ('o'));
    check (e.text() == "Fo" && e.shift() == TextEntry::Shift::Off, "the first letter is a capital, the next one is not");
    tapKey (e, e.keyOfKind (TextEntry::ShiftKey));
    tapKey (e, e.keyOfKind (TextEntry::ShiftKey));
    tapKey (e, e.keyOf ('o'));
    tapKey (e, e.keyOf ('b'));
    check (e.text() == "FoOB" && e.shift() == TextEntry::Shift::Caps, "shift twice: caps lock holds");
    tapKey (e, e.keyOfKind (TextEntry::ShiftKey));
    check (e.shift() == TextEntry::Shift::Off, "a third tap: off");
    tapKey (e, e.keyOfKind (TextEntry::Space));
    check (e.text() == "FoOB " && e.chars() == 5, "space");
    tapKey (e, e.keyOfKind (TextEntry::Backspace));
    tapKey (e, e.keyOfKind (TextEntry::Backspace));
    check (e.text() == "FoO" && e.chars() == 3, "backspace takes the last character");
    tapKey (e, e.keyOfKind (TextEntry::Clear));
    tapKey (e, e.keyOfKind (TextEntry::Backspace));
    check (e.text().empty() && e.chars() == 0 && e.shift() == TextEntry::Shift::Once, "CLEAR empties; backspace at the start does nothing");
    // the symbol layer, no letters on it
    tapKey (e, e.keyOfKind (TextEntry::LayerKey));
    const char* const symbols = "1234567890-+/&'()#%_.,!?:*=";
    bool all = true;
    for (const char* c = symbols; *c != '\0'; ++c)
        all = all && e.keyOf (*c) >= 0;
    check (all && e.keyOf ('a') < 0, "the second layer: digits and the symbols, no letters");
    tapKey (e, e.keyOf ('7'));
    tapKey (e, e.keyOf ('&'));
    check (e.text() == "7&", "symbols are typed as they are (shift does not touch them)");
    // the limit counts characters
    tapKey (e, e.keyOfKind (TextEntry::LayerKey));
    tapKey (e, e.keyOfKind (TextEntry::Clear));
    for (int i = 0; i < 30; ++i)
        tapKey (e, e.keyOf ('x'));
    check (e.chars() == 16 && e.text().size() == 16, "the limit: 16 characters");
    tapKey (e, e.keyOfKind (TextEntry::Space));
    check (e.chars() == 16, "space at the limit does nothing");
    e.begin ("T", "\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac", 4, 7, 8);
    check (e.chars() == 3 && e.text().size() == 9, "three characters of three bytes: the limit is in characters");
    tapKey (e, e.keyOf ('a'));
    tapKey (e, e.keyOf ('b'));
    check (e.chars() == 4 && e.text().size() == 10, "one more fits, then the limit");
    tapKey (e, e.keyOfKind (TextEntry::Backspace));
    tapKey (e, e.keyOfKind (TextEntry::Backspace));
    check (e.text() == "\xe2\x82\xac\xe2\x82\xac" && e.chars() == 2, "backspace removes a whole character");
    // OK and CANCEL
    Change c;
    tapKey (e, e.keyOfKind (TextEntry::Ok), &c);
    check (c.kind == ChangeKind::Request && c.id == 7, "OK is a Request with the id of OK");
    tapKey (e, e.keyOfKind (TextEntry::Cancel), &c);
    check (c.kind == ChangeKind::Request && c.id == 8, "CANCEL is a Request with the id of CANCEL");
    // a finger that slides off a key does nothing
    e.begin ("T", "ab", 16, 7, 8);
    {
        const Rect r = e.key (e.keyOf ('q')).r;
        e.touchDown (r.x + 10, r.y + 10, 0.0);
        e.touchMove (r.x + 10, r.y - 80);
        check (! e.touchUp (r.x + 10, r.y - 80) && e.text() == "ab", "a touch that left the key does nothing");
        e.touchDown (r.x + 10, r.y + 10, 0.0);
        e.touchMove (r.x + 20, r.y + 20);
        e.touchUp (r.x + 20, r.y + 20);
        check (e.text() == "abq", "a small move inside the key is still a tap");
        e.touchDown (5, 5, 0.0);
        check (! e.touchUp (5, 5) && e.text() == "abq", "a touch beside the keys does nothing");
    }
    // disabled
    e.setEnabled (false);
    {
        const Rect r = e.key (e.keyOf ('q')).r;
        e.touchDown (r.x + 10, r.y + 10, 0.0);
        e.touchUp (r.x + 10, r.y + 10);
        check (e.text() == "abq", "disabled: no reaction");
    }
    e.setEnabled (true);
    // drawing: inside the tab, one scene, each state
    for (int state = 0; state < 4; ++state)
    {
        TextEntry p;
        p.place (kTab);
        p.begin ("NAME OF MACRO 3", state == 0 ? "" : (state == 2 ? "WWWWWWWWWWWWWWWW" : "Filter Sweep"), 16, 7, 8);
        if (state == 1)
            tapKey (p, p.keyOfKind (TextEntry::LayerKey));
        if (state == 3)
        {
            tapKey (p, p.keyOfKind (TextEntry::ShiftKey));
            tapKey (p, p.keyOfKind (TextEntry::ShiftKey));
        }
        Scene s (1280, 800, kBackgroundRgba);
        p.draw (s.builder());
        const Image im = render (s);
        check (! s.overflow() && drawnOutside (im, kTab, backgroundPixel()) == 0, "the page draws inside the tab, state " + std::to_string (state) + " (" + std::to_string (s.numOps()) + " ops)");
        static const char* const names[4] = { "empty", "symbols", "full", "caps" };
        writePng (outDir() + "/text_entry_" + names[state] + ".png", im, 1);
    }
    std::printf (g_fails ? "ui_text_entry_host: FAILED (%d)\n" : "ui_text_entry_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
