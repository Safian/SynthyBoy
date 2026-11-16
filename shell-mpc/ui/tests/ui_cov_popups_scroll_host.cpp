// SynthyBoy - MPC UI library test coverage: popups and scroll areas
// Tests untested public behaviors of ListPopup, FxGallery, ModSourceBoard,
// and ShapeCanvas.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui_test.h"
#include "../list.h"
#include "../fx_gallery.h"
#include "../mod_board.h"
#include "../shape_editor.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

static void test_list_popup_selection_and_turn()
{
    static const char* const items[] = {
        "Osc 1", "Osc 2", "Filter", "LFO 1", "LFO 2", "Chorus", "Reverb", "Delay"
    };

    ListPopup lp;
    lp.setId (301);
    lp.place ({ 100, 100, 260, 250 });
    lp.setPopup ({ 0, 0, 800, 600 });
    lp.setBackdropId (999);
    lp.setHeaderId (888);
    lp.state.items = items;
    lp.state.count = 8;
    lp.state.current = 0;

    check (lp.minSize().w == 240 && lp.minSize().h == 3 * ListPopup::kRowH + 2 * ListPopup::kPad, "ListPopup minSize");
    check (lp.visibleRows() == (250 - 2 * ListPopup::kPad) / ListPopup::kRowH, "ListPopup visibleRows calculation");
    check (lp.scrolls(), "ListPopup scrolls when count > visibleRows");

    // Tap on row 1
    Rect r1 = lp.rowRect (1);
    lp.touchDown (r1.x + 20, r1.y + 20, 1.0);
    Change c = lp.touchUp (r1.x + 20, r1.y + 20);
    check (c.kind == ChangeKind::Index && c.index == 1, "ListPopup tap row 1 selects item 1");

    // Turn moves current item
    c = lp.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "ListPopup turn +1 advances to 1");
    lp.state.current = 1;

    c = lp.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 0, "ListPopup turn -1 retreats to 0");
    lp.state.current = 0;

    // Clamped at 0
    c = lp.turn (-1.0);
    check (c.kind == ChangeKind::None, "ListPopup turn -1 at top does nothing");

    // Clamped at end
    c = lp.turn (20.0);
    check (c.kind == ChangeKind::Index && c.index == 7, "ListPopup turn +20 clamps to end (7)");
    lp.state.current = 7;

    c = lp.turn (1.0);
    check (c.kind == ChangeKind::None, "ListPopup turn +1 at bottom does nothing");

    // showCurrent centers item
    lp.showCurrent();
    check (lp.firstRow() > 0, "showCurrent scrolled down to reveal current item");

    Rect parent { 0, 0, 800, 600 };
    Image im = drawComponent (lp, 800, 600);
    check (drawnOutside (im, parent, backgroundPixel()) == 0, "ListPopup with backdrop stays within parent rect");
}

static void test_list_popup_backdrop_and_header_back()
{
    static const char* const items[] = { "Item A", "Item B" };

    ListPopup lp;
    lp.setId (301);
    lp.place ({ 100, 100, 260, 200 });
    lp.setPopup ({ 50, 50, 700, 500 });
    lp.setBackdropId (999);
    lp.setHeaderId (888);
    lp.state.items = items;
    lp.state.count = 2;
    lp.state.header = "BROWSER";
    lp.state.headerBack = true;

    check (lp.headerH() == ListPopup::kRowH, "Header height is kRowH when header is present");
    Rect hr = lp.headerRect();
    check (hr.y == lp.rect().y + ListPopup::kPad && hr.h == ListPopup::kRowH, "Header rect matches top row");

    // Tap in header triggers back Request
    lp.touchDown (hr.x + 20, hr.y + 20, 1.0);
    Change c = lp.touchUp (hr.x + 20, hr.y + 20);
    check (c.kind == ChangeKind::Request && c.id == 888, "Header back tap emits headerId Request");

    // When headerBack is false, tap does not emit Request
    lp.state.headerBack = false;
    lp.touchDown (hr.x + 20, hr.y + 20, 1.0);
    c = lp.touchUp (hr.x + 20, hr.y + 20);
    check (c.kind == ChangeKind::None, "Header tap without headerBack does nothing");

    // Backdrop dismissal: tap outside lp.rect() but inside parent rect
    lp.touchDown (60, 60, 1.0);
    c = lp.touchUp (60, 60);
    check (c.kind == ChangeKind::Request && c.id == 999, "Backdrop tap emits backdropId Request");

    // Backdrop drag away cancels dismissal
    lp.touchDown (60, 60, 1.0);
    lp.touchMove (300, 60);
    c = lp.touchUp (300, 60);
    check (c.kind == ChangeKind::None, "Backdrop drag away cancels dismissal");

    // Tap outside parent entirely is ignored
    c = lp.touchDown (10, 10, 1.0);
    check (c.kind == ChangeKind::None, "Tap outside parent is ignored");
}

static void test_list_popup_scroll_zone_and_paging()
{
    static const char* const items[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"
    };

    ListPopup lp;
    lp.setId (301);
    lp.place ({ 100, 100, 260, 240 });
    lp.setPopup ({ 0, 0, 800, 600 });
    lp.state.items = items;
    lp.state.count = 10;
    lp.state.current = 0;

    check (lp.scrolls(), "10 items in 240px scrolls");
    Rect zr = lp.zoneRect();
    check (zr.w == ListPopup::kZoneW, "Zone width is kZoneW (44px)");
    Rect tr = lp.thumbRect();
    check (zr.contains (tr), "Thumb rect is within scroll zone");

    // Tap below thumb -> page down
    int oldFirst = lp.firstRow();
    lp.touchDown (zr.x + 10, zr.bottom() - 10, 1.0);
    lp.touchUp (zr.x + 10, zr.bottom() - 10);
    check (lp.firstRow() > oldFirst, "Paging down in scroll track advances firstRow");

    // Tap above thumb -> page up
    oldFirst = lp.firstRow();
    lp.touchDown (zr.x + 10, zr.y + 5, 1.0);
    lp.touchUp (zr.x + 10, zr.y + 5);
    check (lp.firstRow() < oldFirst, "Paging up in scroll track retreats firstRow");
}

static void test_fx_gallery_layout_and_selection()
{
    FxGallery fg;
    fg.setId (302);
    fg.place ({ 50, 50, 720, 320 });

    const int types[6] = { 1, 2, 3, 4, 5, 6 };
    const char* const names[6] = { "Chorus", "Flanger", "Delay", "Reverb", "Drive", "Filter" };
    fg.setTypes (types, names, 6);

    check (fg.numTypes() == 6, "FxGallery has 6 types");
    check (fg.numRows() == 2, "FxGallery has 2 rows");
    check (fg.minSize().w == 700, "FxGallery minSize width is 700");

    Rect targets[8];
    int nt = fg.touchTargets (targets, 8);
    check (nt == 6, "FxGallery reports 6 card touch targets");

    // Tap card 2 (Delay, type 3)
    Rect c2 = fg.cardRect (2);
    fg.touchDown (c2.x + 10, c2.y + 10, 1.0);
    Change c = fg.touchUp (c2.x + 10, c2.y + 10);
    check (c.kind == ChangeKind::Index && c.index == 3, "FxGallery tap card 2 selects type 3");

    // Card rect of type
    check (fg.cardRectOfType (3).x == c2.x && fg.cardRectOfType (3).y == c2.y, "cardRectOfType matches cardRect");
    check (fg.cardRectOfType (99).empty(), "cardRectOfType for non-existent type is empty");

    // Picture rect inside card
    Rect pic = fg.pictureRect (0);
    check (fg.cardRect (0).contains (pic), "pictureRect is strictly inside cardRect");

    // Names converted to uppercase
    check (std::strcmp (fg.nameAt (0), "CHORUS") == 0, "FxGallery names are uppercase");

    Image im = drawComponent (fg, 820, 420);
    check (drawnOutside (im, fg.rect(), backgroundPixel()) == 0, "FxGallery draws strictly inside rect");
}

static void test_mod_source_board_chips_and_groups()
{
    ModSourceBoard mb;
    mb.setId (303);
    mb.place ({ 50, 50, 920, 300 });

    mb.setGroupName (0, "MACROS");
    mb.setGroupName (1, "ENVELOPES");
    mb.add (10, "M1", "macro", 0);
    mb.add (11, "M2", "macro", 0);
    mb.add (20, "ENV1", "env", 1);
    mb.add (21, "ENV2", "env", 1);

    check (mb.numSources() == 4, "ModSourceBoard has 4 sources");
    check (mb.numGroups() == 2, "ModSourceBoard has 2 populated groups");

    Rect targets[8];
    int nt = mb.touchTargets (targets, 8);
    check (nt == 4, "ModSourceBoard reports 4 chip touch targets");

    // Tap chip 2 (ENV1, id 20)
    Rect c2 = mb.chipRect (2);
    mb.touchDown (c2.x + 10, c2.y + 10, 1.0);
    Change c = mb.touchUp (c2.x + 10, c2.y + 10);
    check (c.kind == ChangeKind::Index && c.index == 20, "ModSourceBoard tap chip 2 emits source id 20");

    // chipRectOfId
    check (mb.chipRectOfId (20).x == c2.x && mb.chipRectOfId (20).y == c2.y, "chipRectOfId matches chipRect");
    check (mb.chipRectOfId (999).empty(), "chipRectOfId non-existent is empty");

    check (mb.fits(), "ModSourceBoard fits in allocated rectangle");

    // Clear
    mb.clear();
    check (mb.numSources() == 0, "ModSourceBoard clear resets sources to 0");
    check (mb.numGroups() == 0, "ModSourceBoard clear resets groups to 0");

    // Re-add and draw
    mb.setGroupName (0, "MACROS");
    mb.add (10, "M1", "macro", 0);
    Image im = drawComponent (mb, 1020, 400);
    check (drawnOutside (im, mb.rect(), backgroundPixel()) == 0, "ModSourceBoard draws strictly inside rect");
}

static void test_shape_canvas_point_editing_and_undo()
{
    ShapeCanvas sc;
    sc.setId (304);
    sc.place ({ 50, 50, 500, 300 });

    lfo::ShapeModel shape = lfo::ShapeModel::fromBuiltin (0);
    lfo::Undo undo;

    sc.state.shape = &shape;
    sc.state.undo = &undo;
    sc.state.snap = false;

    check (sc.minSize().w == 400 && sc.minSize().h == 240, "ShapeCanvas minSize is 400x240");
    check (sc.rect().contains (sc.plot()), "ShapeCanvas plot is inside rect");

    // Select existing point 0
    int px = static_cast<int> (std::lround (sc.xOf (shape.p[0].x)));
    int py = static_cast<int> (std::lround (sc.yOf (shape.p[0].y)));
    Change c = sc.touchDown (px, py, 100.0);
    check (c.kind == ChangeKind::Index && c.index == ShapeCanvas::Selected, "ShapeCanvas touch on existing point selects it");
    sc.touchUp (px, py);

    // Insert an interior point
    const int oldN = shape.n;
    shape.insert (0.5f, 0.75f);
    check (shape.n == oldN + 1, "Point inserted into ShapeModel");

    // Find the inserted point (at x = 0.5f)
    int idx = -1;
    for (int i = 0; i < shape.n; ++i)
        if (std::fabs (shape.p[i].x - 0.5f) < 0.01f)
            idx = i;
    check (idx > 0 && idx < shape.n - 1, "Inserted point is interior");

    // Long press on interior point removes it
    int ix = static_cast<int> (std::lround (sc.xOf (shape.p[idx].x)));
    int iy = static_cast<int> (std::lround (sc.yOf (shape.p[idx].y)));
    sc.touchDown (ix, iy, 100.0);
    c = sc.tick (100.0 + 700.0); // 700ms > 600ms long press threshold
    check (c.kind == ChangeKind::Index && c.index == ShapeCanvas::Edited, "ShapeCanvas long press removes point and reports Edited");
    sc.touchUp (ix, iy);
    check (shape.n == oldN, "Point removed by long press");
    check (undo.depth() > 0, "Long press pushed to undo stack");

    // Undo restores removed point
    undo.pop (shape);
    check (shape.n == oldN + 1, "Undo restores point");

    Image im = drawComponent (sc, 600, 400);
    check (drawnOutside (im, sc.rect(), backgroundPixel()) == 0, "ShapeCanvas draws strictly inside rect");
}

int main()
{
    test_list_popup_selection_and_turn();
    test_list_popup_backdrop_and_header_back();
    test_list_popup_scroll_zone_and_paging();
    test_fx_gallery_layout_and_selection();
    test_mod_source_board_chips_and_groups();
    test_shape_canvas_point_editing_and_undo();

    std::printf ("ui_cov_popups_scroll_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
