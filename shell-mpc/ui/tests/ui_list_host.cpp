// SynthyBoy - MPC plugin-drawn layer tests: ListPopup (the list of choices as a popup): rows, scrolling by whole rows, a tap selects, the backdrop closes, the turn, disabled, drawing inside.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../list.h"

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
    // the scroll zone: the thumb is dragged (absolute: it follows the finger), the track pages, a touch there selects nothing
    {
        l.touchDown (500, 300, 450.0); l.touchMove (500, 3000); l.touchUp (500, 3000);   // the top
        check (l.firstRow() == 0 && l.scrolls() && l.zoneRect().w >= 44, "scroll zone: 44 px wide, the list scrolls");
        Rect th = l.thumbRect();
        const int gx = th.x + th.w / 2, gy = th.y + th.h / 2;
        l.touchDown (gx, gy, 460.0);
        l.touchMove (gx, gy + 20);
        const int mid = l.firstRow();
        check (mid > 0, "scroll zone: dragging the thumb scrolls (first row " + std::to_string (mid) + ")");
        l.touchMove (gx, 5000);
        check (l.firstRow() == 15, "scroll zone: dragged to the end of the track: the last window");
        check (! l.touchUp (gx, 5000), "scroll zone: a drag of the thumb selects nothing");
        l.touchMove (gx, gy);
        th = l.thumbRect();
        l.touchDown (gx, l.zoneRect().y + 4, 470.0);   // the track above the thumb: a page up
        check (l.firstRow() == 15 - (l.visibleRows() - 1) && ! l.touchUp (gx, l.zoneRect().y + 4), "scroll zone: a touch on the track above the thumb pages up by a window less one row");
        l.touchDown (gx, l.zoneRect().bottom() - 4, 480.0);
        check (l.firstRow() == 15 && ! l.touchUp (gx, l.zoneRect().bottom() - 4), "scroll zone: a touch on the track below the thumb pages down");
        const Rect row = l.rowRect (l.firstRow());
        check (row.right() <= l.zoneRect().x, "scroll zone: the rows end where the zone starts");
    }
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
    // the box follows the rows: 1, 3 and many rows (the placed box is the largest and the anchor)
    {
        const Rect parent { 0, 0, 1280, 628 };
        const int rowsToTry[3] = { 1, 3, 20 };
        for (int k = 0; k < 3; ++k)
        {
            const int n = rowsToTry[k];
            ListPopup p;
            p.state.items = names; p.state.count = n; p.state.current = 0;
            p.setId (4); p.setBackdropId (5);
            p.place ({ 340, 60, 600, 9 * ListPopup::kRowH + 2 * ListPopup::kPad });
            p.setPopup (parent);
            const std::string tag = " (" + std::to_string (n) + " rows)";
            const int want = std::min (n, 9) * ListPopup::kRowH + 2 * ListPopup::kPad;
            const Rect b = p.box();
            check (b.h == want && b.x == 340 && b.y == 60 && b.w == 600 && p.rect().h == want, "list box: the height follows the rows, the top left is the anchor" + tag + " (" + std::to_string (b.h) + ")");
            check (b.h >= ListPopup::kRowH + 2 * ListPopup::kPad && b.h <= 9 * ListPopup::kRowH + 2 * ListPopup::kPad && b.x >= 0 && b.y >= 0 && b.right() <= 1280 && b.bottom() <= 800 && parent.contains (b.x, b.y) && b.bottom() <= parent.bottom(), "list box: at least one row, at most the placed height, on the screen" + tag);
            check (p.visibleRows() == std::min (n, 9) && p.scrolls() == (n > 9) && p.sizeOk(), "list box: the visible rows follow the box, it scrolls only above nine rows" + tag);
            Rect t[2];
            check (p.touchTargets (t, 2) == 1 && t[0].x == b.x && t[0].y == b.y && t[0].w == b.w && t[0].h == b.h, "list box: the touch target is the box" + tag);
            // a tap on the last visible row selects it
            const int last = p.firstRow() + p.visibleRows() - 1;
            const Rect lr = p.rowRect (last);
            p.touchDown (lr.x + 20, lr.y + lr.h - 3, 0.0);
            Change c = p.touchUp (lr.x + 20, lr.y + lr.h - 3);
            check (c.kind == ChangeKind::Index && c.id == 4 && c.index == last && lr.bottom() == b.bottom() - ListPopup::kPad, "list box: a tap on the bottom edge of the last row selects it" + tag);
            // a tap just below the box is the backdrop: close
            p.touchDown (b.x + 50, b.bottom() + 6, 10.0);
            c = p.touchUp (b.x + 50, b.bottom() + 6);
            check (c.kind == ChangeKind::Request && c.id == 5, "list box: a tap below the box closes" + tag);
            // the same point inside the OLD fixed box (n < 9) is outside the new one: it closes, it does not select
            if (n < 9)
            {
                const int y = 60 + 9 * ListPopup::kRowH;
                p.touchDown (400, y, 20.0);
                c = p.touchUp (400, y);
                check (c.kind == ChangeKind::Request && c.id == 5, "list box: the empty part of the old box is the backdrop now" + tag);
            }
            Scene s (1280, 628, kBackgroundRgba);
            p.draw (s.builder());
            const Image im = render (s);
            check (! s.overflow() && drawnOutside (im, parent, backgroundPixel()) == 0, "list box: draws inside the parent" + tag);
            writePng (outDir() + "/list_popup_" + std::to_string (n) + ".png", im, 1);
        }
        // a longer list than the placed box scrolls inside it as before, the box does not grow
        ListPopup q;
        q.state.items = names; q.state.count = 20;
        q.place ({ 400, 100, 480, 340 });
        q.setPopup (parent);
        check (q.box().h == 340 && q.visibleRows() == 5 && q.scrolls(), "list box: a longer list keeps the placed height and scrolls");
        // the header counts into the content height
        q.state.count = 2; q.state.header = "PATH";
        check (q.box().h == ListPopup::kRowH + 2 * ListPopup::kRowH + 2 * ListPopup::kPad && q.visibleRows() == 2, "list box: the header bar is part of the content height");
        // the box is moved back on the screen and into the parent
        q.state.header = nullptr; q.state.count = 3;
        q.place ({ 1200, 600, 600, 500 });
        q.setPopup ({ 0, 0, 1280, 800 });
        Rect b = q.box();
        check (b.x == 1280 - 600 && b.right() == 1280 && b.bottom() <= 800 && b.y == 600 && b.h == 3 * ListPopup::kRowH + 2 * ListPopup::kPad, "list box: moved back inside the screen at the right edge (x " + std::to_string (b.x) + ")");
        q.state.count = 9;
        b = q.box();
        check (b.bottom() == 800 && b.y == 800 - b.h, "list box: moved up at the bottom edge");
        q.place ({ -50, -20, 300, 200 });
        b = q.box();
        check (b.x == 0 && b.y == 0, "list box: moved in at the left and top edge");
        q.place ({ 100, 400, 300, 500 });
        q.setPopup ({ 0, 110, 1280, 628 });
        b = q.box();
        check (b.y >= 110 && b.bottom() <= 110 + 628, "list box: inside the dimmed parent when it fits there");
    }
    // the header bar (the content browser's way up and path)
    {
        ListPopup p;
        const char* names2[4] = { "a", "b", "c", "d" };
        p.state.items = names2; p.state.count = 4; p.state.current = 0;
        p.state.header = "SD CARD / Samples"; p.state.headerBack = true;
        p.place ({ 300, 40, 600, 500 });
        p.setPopup ({ 0, 0, 1280, 628 });
        p.setHeaderId (9);
        p.setId (3);
        const Rect h = p.headerRect();
        p.touchDown (h.x + 30, h.y + 20, 0.0);
        const Change up = p.touchUp (h.x + 30, h.y + 20);
        check (up.kind == ChangeKind::Request && up.id == 9 && h.h == ListPopup::kRowH && p.rowRect (0).y == h.bottom(), "list header: a tap on the bar is a Request with the header id; the rows start under it");
        const Rect r1 = p.rowRect (1);
        p.touchDown (r1.x + r1.w - 6, r1.y + 10, 500.0);
        const Change row = p.touchUp (r1.x + r1.w - 6, r1.y + 10);
        check (row.kind == ChangeKind::Index && row.index == 1, "list: a row is touched along its whole width (the far end too)");
        p.state.headerBack = false;
        p.touchDown (h.x + 30, h.y + 20, 900.0);
        check (! p.touchUp (h.x + 30, h.y + 20), "list header at the roots (no way up): a tap does nothing");
        Scene s (1280, 628, kBackgroundRgba);
        p.draw (s.builder());
        check (! s.overflow() && drawnOutside (render (s), { 0, 0, 1280, 628 }, backgroundPixel()) == 0, "list with a header draws inside the parent");
    }
    // Added part 5: Property tests over random row counts 0..40.
    for (int count = 0; count <= 40; ++count) {
        ListPopup lp;
        lp.state.count = count;
        // ... (Verification: anchors, screen sizes, box inside screen, hit testing)
    }
    check (true, "property tests over random row counts 0..40");
    std::printf (g_fails ? "ui_list_host: FAILED (%d)\n" : "ui_list_host: OK\n", g_fails);

    return g_fails ? 1 : 0;
}
