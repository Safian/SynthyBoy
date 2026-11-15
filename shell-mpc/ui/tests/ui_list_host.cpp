// SynthyBoy - MPC UI component library tests: ListPopup (the list of choices as a popup): rows, scrolling by whole rows, a tap selects, the backdrop closes, the turn, disabled, drawing inside.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"

using namespace uit;

int main()
{
    static const char* const names[20] = { "Sine", "Saw", "Square", "Triangle", "Pulse", "Noise", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "A very long name indeed" };
    ListPopup l;
    l.state.items = names; l.state.count = 20; l.state.current = 7;
    l.setId (9); l.setBackdropId (10);
    l.place ({ 400, 100, 480, 340 });
    l.setPopup ({ 0, 0, 1280, 628 });
    check (l.sizeOk() && l.visibleRows() == 5, "the box holds five whole rows (" + std::to_string (l.visibleRows()) + ")");
    l.showCurrent();
    check (l.firstRow() <= 7 && l.firstRow() + l.visibleRows() > 7, "showCurrent brings the current item into the window");
    l.state.current = 0; l.showCurrent();
    check (l.firstRow() == 0, "item 0: the window starts at the top");
    // a tap on the third row selects item 2
    Rect r2 = l.rowRect (2);
    Change c = l.touchDown (r2.x + 20, r2.y + 20, 0.0);
    check (! c, "touch down gives nothing yet");
    c = l.touchUp (r2.x + 20, r2.y + 20);
    check (c.kind == ChangeKind::Index && c.id == 9 && c.index == 2, "a tap on row 2: Index { id 9, index 2 }");
    // a drag up by 2.2 rows scrolls two rows and selects nothing
    l.touchDown (500, 300, 100.0);
    l.touchMove (500, 300 - static_cast<int> (2.2 * ListPopup::kRowH));
    c = l.touchUp (500, 300 - static_cast<int> (2.2 * ListPopup::kRowH));
    check (! c && l.firstRow() == 2, "a drag scrolls whole rows and is not a tap (first row " + std::to_string (l.firstRow()) + ")");
    l.touchDown (500, 300, 200.0); l.touchMove (500, 3000); l.touchUp (500, 3000);
    check (l.firstRow() == 0, "dragged down far: the top");
    l.touchDown (500, 300, 300.0); l.touchMove (500, -3000); l.touchUp (500, -3000);
    check (l.firstRow() == 15, "dragged up far: the last window (first row 15 of 20, five visible)");
    r2 = l.rowRect (17);
    l.touchDown (r2.x + 20, r2.y + 20, 400.0);
    c = l.touchUp (r2.x + 20, r2.y + 20);
    check (c.kind == ChangeKind::Index && c.index == 17, "after scrolling the tap selects the row under the finger (17)");
    // the backdrop
    l.touchDown (50, 50, 500.0);
    c = l.touchUp (50, 50);
    check (c.kind == ChangeKind::Request && c.id == 10, "a tap outside the box closes: Request with the backdrop id");
    l.touchDown (50, 50, 600.0); l.touchMove (400, 400);
    check (! l.touchUp (400, 400), "a drag that started on the backdrop does not close");
    // the turn
    l.state.current = 5;
    c = l.turn (2);
    check (c.kind == ChangeKind::Index && c.index == 7 && l.firstRow() + l.visibleRows() > 7, "turn +2: item 7, the window follows");
    l.state.current = 19;
    check (! l.turn (3), "turn at the end: nothing");
    // disabled
    l.setEnabled (false);
    l.touchDown (r2.x + 20, r2.y + 20, 700.0);
    check (! l.touchUp (r2.x + 20, r2.y + 20) && ! l.turn (1), "disabled: no reaction");
    l.setEnabled (true);
    // drawing: inside the parent, the box opaque, at the minimum size too
    for (int big = 0; big < 2; ++big)
    {
        ListPopup p;
        p.state.items = names; p.state.count = 20; p.state.current = 3;
        const Size m = p.minSize();
        p.place (big ? Rect { 300, 40, 600, 500 } : Rect { 100, 50, m.w, m.h });
        p.setPopup ({ 0, 0, 1280, 628 });
        Scene s (1280, 628, kBackgroundRgba);
        p.draw (s.builder());
        const Image im = render (s);
        check (! s.overflow() && p.sizeOk() && drawnOutside (im, { 0, 0, 1280, 628 }, backgroundPixel()) == 0, std::string ("draws inside the parent, one scene (") + (big ? "large" : "minimum size") + ", " + std::to_string (s.numOps()) + " ops)");
        writePng (outDir() + (big ? "/list_popup.png" : "/list_popup_min.png"), im, 1);
    }
    std::printf (g_fails ? "ui_list_host: FAILED (%d)\n" : "ui_list_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
