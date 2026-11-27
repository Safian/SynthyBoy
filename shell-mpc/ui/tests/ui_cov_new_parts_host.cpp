// SynthyBoy - MPC UI library test coverage: new parts host tests
// Tests NoteMask (12-note keyboard mask editor), FxBandBoard (splitter band lanes),
// Knob LED-on-label feature (led / ledOn / ledHit) and dialMax,
// NumberField maxBox and State::text, and LedList with two-digit Q chips.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "ui_test.h"
#include "../base.h"
#include "../fx_band_board.h"
#include "../led_list.h"
#include "../note_mask.h"
#include "../qgroup.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

namespace {

bool near(double a, double b, double eps = 1.0e-5)
{
    return std::fabs(a - b) <= eps;
}

// =================================================================================================
// 1. NoteMask (shell-mpc/ui/note_mask.h): 12-note keyboard mask editor
// =================================================================================================
void test_note_mask()
{
    NoteMask nm;
    nm.setId(101);
    nm.setLabel("TRANSPOSE");
    check(std::strcmp(nm.state.label, "TRANSPOSE") == 0, "note_mask: label set correctly");

    // Minimum size: 7 * 62 = 434, 2 * 56 + 8 = 120
    const Size ms = nm.minSize();
    check(ms.w == 434 && ms.h == 120, "note_mask: minSize is 434x120 (>= 56px rows + 8px gap)");

    const Rect bounds { 50, 50, ms.w, ms.h };
    nm.place(bounds);
    check(nm.rect() == bounds, "note_mask: placed at minSize rectangle");

    // Touch targets >= 56 px and all inside rectangle
    Rect targets[16];
    const int targetCount = nm.touchTargets(targets, 16);
    check(targetCount == NoteMask::kNotes, "note_mask: reports 12 touch targets");

    bool allTargetsInside = true;
    bool allTargetsMinSize = true;
    for (int i = 0; i < targetCount; ++i)
    {
        if (!bounds.contains(targets[i]))
            allTargetsInside = false;
        if (targets[i].w < 56 || targets[i].h < 56)
            allTargetsMinSize = false;
    }
    check(allTargetsInside, "note_mask: all 12 note touch zones are strictly inside rect");
    check(allTargetsMinSize, "note_mask: all 12 note touch targets have width >= 56 and height >= 56");

    // Key layout validation: white vs black
    for (int i = 0; i < NoteMask::kNotes; ++i)
    {
        const bool black = NoteMask::isBlack(i);
        const Rect cell = nm.cellRect(i);
        if (black)
            check(cell.y == bounds.y, "note_mask: black key is in upper row");
        else
            check(cell.bottom() == bounds.bottom(), "note_mask: white key is in lower row");
    }

    // Touch down/up sequence: tap toggles note bit
    nm.state.mask = 0;
    const Rect cCell = nm.cellRect(0); // Note 0: C (white key)
    const int cx = cCell.x + cCell.w / 2;
    const int cy = cCell.y + cCell.h / 2;

    Change ch = nm.touchDown(cx, cy, 100.0);
    check(!ch, "note_mask: touchDown alone emits none");
    ch = nm.touchUp(cx, cy);
    check(ch.kind == ChangeKind::Index && ch.id == 101 && ch.index == 1 && near(ch.value, 1.0),
          "note_mask: tap on C flips bit 0 (mask becomes 1)");
    nm.state.mask = ch.index;

    // Tap on black key: Note 1 (C#)
    const Rect cSharpCell = nm.cellRect(1);
    const int csx = cSharpCell.x + cSharpCell.w / 2;
    const int csy = cSharpCell.y + cSharpCell.h / 2;
    nm.touchDown(csx, csy, 200.0);
    ch = nm.touchUp(csx, csy);
    check(ch.kind == ChangeKind::Index && ch.index == 0b11,
          "note_mask: tap on C# flips bit 1 (mask becomes 0b11 = 3)");
    nm.state.mask = ch.index;

    // Tap on already set note toggles it off
    nm.touchDown(cx, cy, 300.0);
    ch = nm.touchUp(cx, cy);
    check(ch.kind == ChangeKind::Index && ch.index == 2,
          "note_mask: tap on active C clears bit 0 (mask becomes 2)");
    nm.state.mask = ch.index;

    // Drag sequence: drag cancels tap toggle
    nm.touchDown(csx, csy, 400.0);
    nm.touchMove(csx + 15, csy); // moved > kSlop (6 px)
    ch = nm.touchUp(csx + 15, csy);
    check(!ch, "note_mask: horizontal drag cancels tap, emits none on touchUp");

    // Touch in row gap (between black key row and white key row)
    const int gapY = bounds.y + (bounds.h - 8) / 2 + 2; // in the 8 px gap
    ch = nm.touchDown(cx, gapY, 500.0);
    check(!ch, "note_mask: touchDown in inter-row gap emits none");
    ch = nm.touchUp(cx, gapY);
    check(!ch, "note_mask: touchUp in inter-row gap emits none");

    // Touch outside bounds
    ch = nm.touchDown(bounds.x - 10, bounds.y - 10, 600.0);
    check(!ch, "note_mask: touchDown outside rectangle emits none");

    // Values at ends of range and Q turn behavior
    nm.state.mask = 0;
    ch = nm.turn(-1.0);
    check(!ch, "note_mask: turn(-1) at mask 0 clamps and emits none");

    ch = nm.turn(1.0);
    check(ch.kind == ChangeKind::Index && ch.index == 1,
          "note_mask: turn(+1) from 0 steps mask to 1");
    nm.state.mask = 1;

    ch = nm.turn(5.0);
    check(ch.kind == ChangeKind::Index && ch.index == 6,
          "note_mask: turn(+5) advances mask by 5 whole steps");

    // Upper limit clamp at 4095 ((1 << 12) - 1)
    nm.state.mask = 4095;
    ch = nm.turn(1.0);
    check(!ch, "note_mask: turn(+1) at mask 4095 clamps and emits none");

    ch = nm.turn(-1.0);
    check(ch.kind == ChangeKind::Index && ch.index == 4094,
          "note_mask: turn(-1) from 4095 steps mask down to 4094");

    // Wheel behavior
    check(near(nm.wheelUnit(), 1.0), "note_mask: wheelUnit is 1.0");
    nm.state.mask = 10;
    ch = nm.wheel(1);
    check(ch.kind == ChangeKind::Index && ch.index == 11,
          "note_mask: wheel(+1) advances mask by 1");
    nm.state.mask = 10;
    ch = nm.wheel(-2);
    check(ch.kind == ChangeKind::Index && ch.index == 8,
          "note_mask: wheel(-2) steps mask down by 2");

    // Disabled state takes no touch, turn, or wheel
    nm.setEnabled(false);
    check(!nm.touchDown(cx, cy, 700.0), "note_mask: disabled touchDown returns none");
    check(!nm.turn(1.0), "note_mask: disabled turn returns none");
    check(!nm.wheel(1), "note_mask: disabled wheel returns none");
    Change outCh;
    check(!nm.wheelOver(cx, cy, 1, outCh), "note_mask: disabled wheelOver returns false");
    nm.setEnabled(true);

    // QGroup routing and styling
    QGroup qg;
    qg.set(0, &nm);
    qg.setActive(true);
    check(nm.qFrame() && nm.qIndex() == 1, "note_mask: active QGroup assigns qFrame and index 1");

    nm.state.mask = 100;
    ch = qg.turn(0, 2.0);
    check(ch.kind == ChangeKind::Index && ch.index == 102,
          "note_mask: QGroup turn routes to NoteMask");
    qg.setActive(false);
    check(!nm.qFrame() && nm.qIndex() == 0, "note_mask: deactivating QGroup clears Q frame");

    // Text fitting and drawing containment
    Scene scAll(600, 300, kBackgroundRgba);
    nm.state.mask = 0b111111111111; // all notes lit
    nm.draw(scAll.builder());
    Image imAll = render(scAll);
    check(drawnOutside(imAll, bounds, backgroundPixel()) == 0,
          "note_mask: all notes lit draws strictly inside rect");

    Scene scNone(600, 300, kBackgroundRgba);
    nm.state.mask = 0; // all notes unlit
    nm.draw(scNone.builder());
    Image imNone = render(scNone);
    check(drawnOutside(imNone, bounds, backgroundPixel()) == 0,
          "note_mask: all notes unlit draws strictly inside rect");
}

// =================================================================================================
// 2. FxBandBoard (shell-mpc/ui/fx_band_board.h): splitter band lanes
// =================================================================================================
void test_fx_band_board()
{
    FxBandBoard board;
    board.assignIds(100);
    check(board.baseId() == 100, "fx_band_board: baseId is 100");

    // Shape: 3 bands (Low, Mid, High), 2 cards in lane 0, 1 in lane 1, 0 in lane 2
    const int cardsPerLane[3] = { 2, 1, 0 };
    board.setShape(3, cardsPerLane);
    check(board.numBands() == 3, "fx_band_board: numBands is 3");
    check(board.numCards(0) == 2, "fx_band_board: lane 0 has 2 cards");
    check(board.numCards(1) == 1, "fx_band_board: lane 1 has 1 card");
    check(board.numCards(2) == 0, "fx_band_board: lane 2 has 0 cards");

    // ID decoding: lane 0..2 cards, tile, add
    int band = -1, what = -1;
    check(board.decode(100, band, what) && band == 0 && what == 0, "fx_band_board: decode card 0 lane 0");
    check(board.decode(114, band, what) && band == 0 && what == FxBandBoard::kOpenId, "fx_band_board: decode tile lane 0");
    check(board.decode(115, band, what) && band == 0 && what == FxBandBoard::kAddId, "fx_band_board: decode add lane 0");

    check(board.decode(100 + 16 + 14, band, what) && band == 1 && what == FxBandBoard::kOpenId, "fx_band_board: decode tile lane 1");
    check(board.decode(100 + 32 + 15, band, what) && band == 2 && what == FxBandBoard::kAddId, "fx_band_board: decode add lane 2");

    // Out-of-range decode checks
    check(!board.decode(99, band, what), "fx_band_board: decode below baseId returns false");
    check(!board.decode(100 + 3 * 16, band, what), "fx_band_board: decode above range returns false");

    // Place at a safe height (180 px: 3 lanes * 56 px + 2 * 6 px gap)
    const Rect safeBounds { 40, 40, 650, 180 };
    board.place(safeBounds);

    // Touch targets >= 56 px and inside rect at safe height
    bool allInsideSafe = true;
    bool allTargetsMinSafe = true;
    for (int b = 0; b < 3; ++b)
    {
        const Rect tr = board.lane[b].tile.rect();
        if (!safeBounds.contains(tr))
            allInsideSafe = false;
        if (tr.w < 56 || tr.h < 56)
            allTargetsMinSafe = false;

        for (int c = 0; c < board.numCards(b); ++c)
        {
            const Rect cr = board.lane[b].cards[c].rect();
            if (!safeBounds.contains(cr))
                allInsideSafe = false;
            if (cr.w < 56 || cr.h < 56)
                allTargetsMinSafe = false;
        }

        const Rect ar = board.lane[b].add.rect();
        if (!safeBounds.contains(ar))
            allInsideSafe = false;
        if (ar.w < 56 || ar.h < 56)
            allTargetsMinSafe = false;
    }
    check(allInsideSafe, "fx_band_board: all tiles, cards, and add buttons inside rect at 180px height");
    check(allTargetsMinSafe, "fx_band_board: all targets have width and height >= 56px at 180px height");

    // FxBandTile gestures: tap emits kOpen, drag cancels
    FxBandTile& tile0 = board.lane[0].tile;
    const Rect t0r = tile0.rect();
    const int tx = t0r.x + 20, ty = t0r.y + 20;

    Change ch = board.touchDown(tx, ty, 100.0);
    check(!ch, "fx_band_board: touchDown on tile emits none");
    ch = board.touchUp(tx, ty);
    check(ch.kind == ChangeKind::Index && ch.id == 114 && ch.index == FxBandTile::Action::kOpen,
          "fx_band_board: tap on tile emits kOpen (id 114)");

    // Drag on tile cancels open
    board.touchDown(tx, ty, 200.0);
    board.touchMove(tx, ty + 20); // vertical drag > kSlop
    ch = board.touchUp(tx, ty + 20);
    check(!ch, "fx_band_board: drag on tile cancels tap, emits none on touchUp");

    // Add button tap emits kAdd
    FxSlotCard& add0 = board.lane[0].add;
    const Rect a0r = add0.rect();
    const int ax = a0r.x + a0r.w / 2, ay = a0r.y + a0r.h / 2;
    board.touchDown(ax, ay, 300.0);
    ch = board.touchUp(ax, ay);
    check(ch.kind == ChangeKind::Index && ch.id == 115 && ch.index == FxSlotCard::Action::kAdd,
          "fx_band_board: tap on + ADD card emits kAdd (id 115)");

    // Card power toggle: touchDown on powerRect emits kTogglePower (Action 2) immediately
    FxSlotCard& card0 = board.lane[0].cards[0];
    const Rect pwrRect = card0.powerRect();
    check(!pwrRect.empty() && pwrRect.w >= 56 && pwrRect.h >= 56,
          "fx_band_board: card power target is >= 56px");
    const int px = pwrRect.x + pwrRect.w / 2, py = pwrRect.y + pwrRect.h / 2;
    ch = board.touchDown(px, py, 400.0);
    check(ch.kind == ChangeKind::Index && ch.id == 100 && ch.index == FxSlotCard::Action::kTogglePower,
          "fx_band_board: touchDown on card power button emits kTogglePower");
    board.touchUp(px, py);

    // Card body tap emits kSelect (Action 1) on touchUp
    const int bx = card0.rect().x + 10, by = card0.rect().y + 10;
    board.touchDown(bx, by, 500.0);
    ch = board.touchUp(bx, by);
    check(ch.kind == ChangeKind::Index && ch.id == 100 && ch.index == FxSlotCard::Action::kSelect,
          "fx_band_board: tap on card body emits kSelect");

    // Card horizontal drag emits kMove (Action 3) on touchUp
    board.touchDown(bx, by, 600.0);
    board.touchMove(bx + 40, by); // move > 24px threshold
    ch = board.touchUp(bx + 40, by);
    check(ch.kind == ChangeKind::Index && ch.id == 100 && ch.index == FxSlotCard::Action::kMove,
          "fx_band_board: horizontal drag on card emits kMove on touchUp");

    // Disabled board takes no touch
    board.setEnabled(false);
    check(!board.touchDown(tx, ty, 700.0), "fx_band_board: disabled touchDown on tile returns none");
    check(!board.touchDown(ax, ay, 710.0), "fx_band_board: disabled touchDown on add returns none");
    check(!board.touchDown(bx, by, 720.0), "fx_band_board: disabled touchDown on card returns none");
    board.setEnabled(true);

    // Disabled individual tile
    tile0.setEnabled(false);
    check(!board.touchDown(tx, ty, 730.0), "fx_band_board: disabled tile takes no touch");
    tile0.setEnabled(true);

    // Dim/bypassed/idle states
    tile0.state.bypassed = true;
    tile0.state.idle = true;
    Scene scDim(800, 300, kBackgroundRgba);
    board.draw(scDim.builder());
    check(scDim.numOps() > 0, "fx_band_board: bypassed/idle tile generates draw ops");
    tile0.state.bypassed = false;
    tile0.state.idle = false;

    // Drawing containment at safe height
    Scene scSafe(800, 300, kBackgroundRgba);
    board.draw(scSafe.builder());
    Image imSafe = render(scSafe);
    check(drawnOutside(imSafe, safeBounds, backgroundPixel()) == 0,
          "fx_band_board: draws strictly inside rect at safe 180px height");
}

// Defect reproduction: FxBandBoard::minSize() vs 3-lane layout overflow
void finding_fx_band_board_min_size_overflow()
{
    FxBandBoard board;
    const Size ms = board.minSize();
    check(ms.w == 600 && ms.h == 168, "finding: FxBandBoard minSize reports 600x168 (3 * 56)");

    const Rect r { 50, 50, ms.w, ms.h };
    board.place(r);
    const int cards[3] = { 1, 1, 1 };
    board.setShape(3, cards);

    // Lane 2 tile bottom calculation:
    // With n=3, gap=6: (168 - 12)/3 = 52px, clamped to min 56px.
    // Lane 0: y=50, bottom=106
    // Lane 1: y=112, bottom=168
    // Lane 2: y=174, bottom=230, exceeding board bottom (218) by 12px!
    const Rect lane2Tile = board.lane[2].tile.rect();
    const bool lane2TileEscapes = lane2Tile.bottom() > r.bottom();
    check(lane2TileEscapes && lane2Tile.bottom() == r.bottom() + 12,
          "finding_fx_band_board_min_size_overflow: lane 2 bottom (230) exceeds board bottom (218) by 12px");

    // Rendering at minSize causes drawing outside the component's rectangle
    Scene sc(800, 400, kBackgroundRgba);
    board.draw(sc.builder());
    Image im = render(sc);
    const int outsidePixels = drawnOutside(im, r, backgroundPixel());
    check(outsidePixels > 0,
          "finding_fx_band_board_min_size_overflow: pixels drawn outside board rect at minSize (" +
          std::to_string(outsidePixels) + " px)");
}

// =================================================================================================
// 3. Knob LED-on-label feature and dialMax
// =================================================================================================
void test_knob_led_and_dial_max()
{
    // A. dialMax state metadata
    {
        Knob sideK("WARP", false);
        sideK.state.side = true;
        sideK.state.dialMax = 52;
        sideK.place({ 50, 50, 200, 80 });
        check(sideK.state.dialMax == 52 && sideK.minSize().w == 172 && sideK.minSize().h == 72 && sideK.sizeOk(),
              "knob: side variant retains dialMax=52 metadata and minSize 172x72");

        Knob standardK("DRIVE", false);
        standardK.state.side = false;
        standardK.state.dialMax = 40;
        standardK.place({ 50, 50, 120, 120 });
        check(standardK.minSize().w == 100 && standardK.minSize().h == 72 && standardK.sizeOk(),
              "knob: standard variant retains minSize 100x72");
    }

    // B. Knob LED-on-label gating in model contract (side and standard variants)
    for (const bool side : { true, false })
    {
        const std::string tag = std::string("knob led (") + (side ? "side" : "standard") + ")";
        Knob k("SMOOTH");
        k.setId(201);
        k.state.side = side;
        setText(k.state.valueText, sizeof(k.state.valueText), "50 ms");
        const Rect kr = side ? Rect { 40, 40, 200, 72 } : Rect { 40, 40, 100, 80 };
        k.place(kr);
        check(k.rect() == kr && k.sizeOk(), tag + ": place stores rect and satisfies minSize");

        // Enable LED feature
        k.state.led = true;
        k.state.ledOn = true;
        k.state.value = 0.5;
        k.state.defaultValue = 0.5;

        // Q turn and wheel while ledOn == true
        Change ch = k.turn(0.1);
        check(ch.kind == ChangeKind::Value && ch.id == 201 && near(ch.value, 0.6),
              tag + ": turn(+0.1) while lit advances value to 0.6");
        k.state.value = ch.value;

        ch = k.wheel(1);
        check(ch.kind == ChangeKind::Value && ch.id == 201 && near(ch.value, 0.6 + 1.0 / 128.0),
              tag + ": wheel(+1) while lit steps value by 1/128");

        // Values at ends of range while ledOn == true
        k.state.value = 1.0;
        check(!k.turn(0.1), tag + ": turn(+0.1) at max clamps and emits none");
        check(!k.wheel(1), tag + ": wheel(+1) at max clamps and emits none");

        k.state.value = 0.0;
        check(!k.turn(-0.1), tag + ": turn(-0.1) at min clamps and emits none");
        check(!k.wheel(-1), tag + ": wheel(-1) at min clamps and emits none");

        // Behavior when ledOn == false: takes NO turn, NO wheel, wheelOver absorbs inside rect
        k.state.ledOn = false;
        k.state.value = 0.5;
        check(!k.turn(0.1), tag + ": turn while unlit returns none");
        check(!k.wheel(1), tag + ": wheel while unlit returns none");
        Change outWh;
        check(k.wheelOver(kr.x + kr.w / 2, kr.y + kr.h / 2, 1, outWh) && !outWh,
              tag + ": wheelOver while unlit absorbs event and produces no change");

        // Re-lighting resumes turn/wheel
        k.state.ledOn = true;
        check(k.turn(0.1).kind == ChangeKind::Value, tag + ": turn works again after ledOn=true");

        // Disabled knob ignores turn/wheel even when lit
        k.setEnabled(false);
        check(!k.turn(0.1), tag + ": disabled turn returns none");
        check(!k.wheel(1), tag + ": disabled wheel returns none");
        k.setEnabled(true);
    }
}

// =================================================================================================
// 4. NumberField maxBox and State::text
// =================================================================================================
void test_number_field_max_box_and_text()
{
    NumberField nf("VOICES", NumberField::Format::IntPlain);
    nf.setId(301);
    nf.state.min = 1.0;
    nf.state.max = 16.0;
    nf.state.step = 1.0;
    nf.state.value = 4.0;
    nf.state.defaultValue = 4.0;

    // A. maxBox and placed rect behavior
    const Rect fieldRect { 50, 50, 160, 70 };
    nf.place(fieldRect);
    nf.maxBox = { 70, 50 };
    check(nf.rect() == fieldRect && nf.maxBox.w == 70 && nf.maxBox.h == 50 && nf.sizeOk(),
          "number_field: placed rectangle and maxBox preserved");
    check(fieldRect.w >= 56 && fieldRect.h >= 56,
          "number_field: placed rectangle meets >= 56px requirement");

    Change whOut;
    check(nf.wheelOver(fieldRect.right() - 10, fieldRect.y + fieldRect.h / 2, 2, whOut)
          && whOut.kind == ChangeKind::Index && whOut.id == 301 && near(whOut.value, 6.0),
          "number_field: wheelOver inside full cell rect steps value (4 -> 6)");

    // Values at ends of range and clamping
    nf.state.value = 16.0;
    check(!nf.turn(1.0), "number_field: turn(+1) at max clamps and emits none");
    check(!nf.wheel(1), "number_field: wheel(+1) at max clamps and emits none");

    nf.state.value = 1.0;
    check(!nf.turn(-1.0), "number_field: turn(-1) at min clamps and emits none");
    check(!nf.wheel(-1), "number_field: wheel(-1) at min clamps and emits none");

    Change ch = nf.turn(1.0);
    check(ch.kind == ChangeKind::Index && near(ch.value, 2.0),
          "number_field: turn(+1) from min steps value to 2.0");

    // Disabled and readOnly state
    nf.setEnabled(false);
    check(!nf.turn(1.0), "number_field: disabled turn returns none");
    check(!nf.wheel(1), "number_field: disabled wheel returns none");
    nf.setEnabled(true);

    nf.state.readOnly = true;
    check(!nf.turn(1.0), "number_field: readOnly turn returns none");
    check(!nf.wheel(1), "number_field: readOnly wheel returns none");
    nf.state.readOnly = false;

    // B. State::text override vs formatValue
    char buf[16];

    // Default format IntPlain
    nf.state.value = 7.0;
    NumberField::formatValue(nf.state, buf);
    check(std::strcmp(buf, "7") == 0, "number_field: formatValue with IntPlain formats as '7'");

    // Custom text set in state.text
    setText(nf.state.text, sizeof(nf.state.text), "MID");
    NumberField::formatValue(nf.state, buf);
    check(std::strcmp(buf, "MID") == 0, "number_field: formatValue uses custom state.text ('MID')");

    setText(nf.state.text, sizeof(nf.state.text), "0 c");
    NumberField::formatValue(nf.state, buf);
    check(std::strcmp(buf, "0 c") == 0, "number_field: formatValue uses custom state.text ('0 c')");

    // notSet takes precedence over custom text
    nf.state.notSet = true;
    NumberField::formatValue(nf.state, buf);
    check(std::strcmp(buf, "-") == 0, "number_field: notSet formats as '-' even with custom text");
    nf.state.notSet = false;

    // QGroup routing
    QGroup qg;
    qg.set(1, &nf);
    qg.setActive(true);
    nf.state.value = 5.0;
    ch = qg.turn(1, 1.0);
    check(ch.kind == ChangeKind::Index && near(ch.value, 6.0),
          "number_field: QGroup turn routes to number field");
    qg.setActive(false);
}

// =================================================================================================
// 5. LedList with two-digit Q chips
// =================================================================================================
void test_led_list_two_digit_q_chips()
{
    LedList ll;
    ll.setId(401);
    ll.setTitle("UNISON");
    const char* opts[] = { "OFF", "2 VOICES", "3 VOICES", "4 VOICES" };
    ll.setOptions(opts, 4);

    const Size ms = ll.minSize();
    check(ms.w >= 56 && ms.h >= 56, "led_list: minSize is >= 56px");

    const Rect listRect { 50, 50, 240, 4 * 56 + LedList::kTitleH };
    ll.place(listRect);

    // Touch targets >= 56 px and inside rect
    Rect llTargets[8];
    const int numTgt = ll.touchTargets(llTargets, 8);
    check(numTgt == 4, "led_list: reports 4 row touch targets");

    bool allTargetsInside = true;
    bool allTargetsMinSize = true;
    for (int i = 0; i < numTgt; ++i)
    {
        if (!listRect.contains(llTargets[i]))
            allTargetsInside = false;
        if (llTargets[i].w < 56 || llTargets[i].h < 56)
            allTargetsMinSize = false;
    }
    check(allTargetsInside, "led_list: all row targets are strictly inside rect");
    check(allTargetsMinSize, "led_list: all row targets have width and height >= 56px");

    // Touch down/up: tap on enabled row selects it
    ll.state.index = 0;
    const Rect r1 = ll.rowRect(1);
    const int r1x = r1.x + r1.w / 2, r1y = r1.y + r1.h / 2;

    Change ch = ll.touchDown(r1x, r1y, 100.0);
    check(!ch, "led_list: touchDown emits none");
    ch = ll.touchUp(r1x, r1y);
    check(ch.kind == ChangeKind::Index && ch.id == 401 && ch.index == 1,
          "led_list: tap on row 1 selects index 1");
    ll.state.index = 1;

    // Tap on already selected row emits none
    ll.touchDown(r1x, r1y, 200.0);
    ch = ll.touchUp(r1x, r1y);
    check(!ch, "led_list: tap on current index emits none");

    // Tap on disabled row emits none
    ll.state.disabledMask = (1u << 2); // row 2 disabled
    const Rect r2 = ll.rowRect(2);
    ll.touchDown(r2.x + r2.w / 2, r2.y + r2.h / 2, 300.0);
    ch = ll.touchUp(r2.x + r2.w / 2, r2.y + r2.h / 2);
    check(!ch, "led_list: tap on disabled row emits none");

    // Drag cancels tap
    ll.touchDown(r1x, r1y, 400.0);
    ll.touchMove(r1x + 20, r1y);
    ch = ll.touchUp(r1x + 20, r1y);
    check(!ch, "led_list: drag cancels row tap");

    // Q turn behavior without wrapping, skipping disabled
    ll.state.index = 1;
    // Step forward: row 2 is disabled, so should advance to row 3
    ch = ll.turn(1.0);
    check(ch.kind == ChangeKind::Index && ch.index == 3,
          "led_list: turn(+1) skips disabled row 2 and selects row 3");
    ll.state.index = 3;

    // Range end: at row 3 (last), turn(+1) clamps without wrap
    ch = ll.turn(1.0);
    check(!ch, "led_list: turn(+1) at end clamps without wrapping");

    // Step backward: row 2 is disabled, steps to row 1
    ch = ll.turn(-1.0);
    check(ch.kind == ChangeKind::Index && ch.index == 1,
          "led_list: turn(-1) skips disabled row 2 and selects row 1");
    ll.state.index = 1;

    ch = ll.turn(-1.0);
    check(ch.kind == ChangeKind::Index && ch.index == 0,
          "led_list: turn(-1) selects row 0");
    ll.state.index = 0;

    ch = ll.turn(-1.0);
    check(!ch, "led_list: turn(-1) at top clamps without wrapping");

    // Wheel unit is -1.0 (wheel up goes up the list)
    check(near(ll.wheelUnit(), -1.0), "led_list: wheelUnit is -1.0 (wheel up goes up)");
    ll.state.index = 1;
    ch = ll.wheel(1); // wheel up -> delta = -1.0 -> step to row 0
    check(ch.kind == ChangeKind::Index && ch.index == 0,
          "led_list: wheel(+1) moves up the list to row 0");

    // Disabled list takes no touch, turn, or wheel
    ll.setEnabled(false);
    check(!ll.touchDown(r1x, r1y, 500.0), "led_list: disabled touchDown returns none");
    check(!ll.turn(1.0), "led_list: disabled turn returns none");
    check(!ll.wheel(1), "led_list: disabled wheel returns none");
    ll.setEnabled(true);

    // Two-digit Q chips integration on wide device (MPC XL)
    QGroup::setWideDevice(true);
    check(QGroup::wideDevice(), "led_list: wide device flag active");

    QGroup qg;
    qg.setBlock(2); // block 2 -> hardware knobs 9..12
    qg.set(1, &ll); // slot 1 -> knob 10 (two-digit chip!)

    // Validate two-digit chip geometry: width 26px, ends at x + 30
    const Rect chip10 = QGroup::chipRect(listRect, 10);
    check(chip10.w == 26 && chip10.h == 17, "led_list: two-digit chip width is 26px (vs 18px for single)");
    check(chip10.x == listRect.x + 4, "led_list: chip starts at list.x + 4");
    check(chip10.right() == listRect.x + 30, "led_list: two-digit chip right edge is at list.x + 30");

    // Validate single-digit chip for comparison
    const Rect chip4 = QGroup::chipRect(listRect, 4);
    check(chip4.w == 18, "led_list: single-digit chip width is 18px");

    // Title in lane: title starts at list.x + 34, leaving 4px clear of two-digit chip
    ll.state.titleInLane = true;
    check(listRect.x + 34 >= chip10.right() + 4,
          "led_list: title in lane starts at x+34, leaving >= 4px margin after two-digit chip");

    // Routing Q turn via QGroup with two-digit knob slot
    qg.setActive(true);
    ll.state.index = 0;
    ll.state.disabledMask = 0;
    ch = qg.turn(1, 1.0); // turn slot 1
    check(ch.kind == ChangeKind::Index && ch.index == 1,
          "led_list: QGroup turn on slot 1 routes to LedList");
    qg.setActive(false);

    // Drawing containment:
    // With titleInLane == false: draws strictly inside rect
    ll.state.titleInLane = false;
    Scene scNormal(400, 400, kBackgroundRgba);
    ll.draw(scNormal.builder());
    Image imNormal = render(scNormal);
    check(drawnOutside(imNormal, listRect, backgroundPixel()) == 0,
          "led_list: titleInLane=false draws strictly inside rect");

    // With titleInLane == true: draws within rect extended into the 22px Q-lane above
    ll.state.titleInLane = true;
    Scene scLane(400, 400, kBackgroundRgba);
    ll.draw(scLane.builder());
    qg.drawIdle(scLane.builder());
    Image imLane = render(scLane);
    const Rect qLaneBounds { listRect.x, listRect.y - 22, listRect.w, listRect.h + 22 };
    check(drawnOutside(imLane, qLaneBounds, backgroundPixel()) == 0,
          "led_list: titleInLane=true and two-digit chip draw strictly inside Q-lane bounds");

    QGroup::setWideDevice(false);
}

// Defect reproduction: LedList touch target width < 56px with single-character option names
void finding_led_list_short_name_min_width()
{
    LedList ll;
    const char* shortOpts[] = { "A", "B", "C", "D" };
    ll.setOptions(shortOpts, 4);

    // columnNeed() computes max(w, 2 * kPadR + 40) = max(w, 52).
    // For single-character names, widthFor("A") is ~44px, so columnNeed() returns 52px.
    const int need = ll.columnNeed();
    check(need == 52, "finding: LedList columnNeed returns 52px for short names (due to 2*kPadR + 40 floor)");

    const Size ms = ll.minSize();
    check(ms.w == 52, "finding: LedList minSize width is 52px (< 56px)");

    ll.place({ 50, 50, ms.w, ms.h });
    const Rect r0 = ll.rowRect(0);
    check(r0.w < 56 && r0.w == 52,
          "finding_led_list_short_name_min_width: row touch target width (52px) is below mandatory 56px minimum");
}

} // namespace

int main()
{
    std::printf("=== UI Coverage: New Parts Host Tests ===\n");
    test_note_mask();
    test_fx_band_board();
    finding_fx_band_board_min_size_overflow();
    test_knob_led_and_dial_max();
    test_number_field_max_box_and_text();
    test_led_list_two_digit_q_chips();
    finding_led_list_short_name_min_width();

    std::printf(g_fails ? "\nui_cov_new_parts_host: FAILED (%d failure(s))\n" : "\nui_cov_new_parts_host: ALL PASSED (0 failures)\n", g_fails);
    return g_fails ? 1 : 0;
}
