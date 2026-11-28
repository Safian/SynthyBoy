// SynthyBoy - MPC overlay: the GEOMETRY of the LFO pages (owner decision 2026-11-27: the C++ layer has no UI elements). The controls of the compact view of the Play page and of the LFO tab are native
// cells of the host skin; the plugin only has to know the rectangles the skin puts them in (so that it leaves them open in its ground, does not swallow a touch on one and selects the Q group of the one that was
// touched) and the rectangles of its own displays. Pure functions of the sizes: no state.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <vector>

#include "../ui/core.h"
#include "../ui/play_rows.h"

namespace synthyboy_mpc { namespace overlay { namespace lfolayout {

constexpr int kRowH = 56, kRateH = 72, kGap = 4, kLedRowH = 56, kMargin = 6;
constexpr int kQuadRandomH = 280;   // Play phase 2: the Q lane (16) and four 60 px rows with three 8 px gaps (RND: STYLE, RATE, SYNC, STEREO)
constexpr int kModeW = 208, kModeH = 56;   // the HZ | BPM | KEY radio grid: three 64 px faces and two 8 px gaps, in a 56 px touch row

/** The compact view (the right third of the Play page): the rectangle `third`. */
struct Compact
{
    ui::Rect third, instance, shape, style, sync, rate, rateKnob, division, modeCells, rise, smoothTime, stereo, display;
};
inline Compact compact (const ui::Rect& third, bool random) noexcept
{
    // Play phase 2 (docs/review/PLAY-PHASE2-PLAN-2026-11-27.md): the selector stands in the header band of the third, the display has no band, the controls are 60 px rows with 8 px gaps
    namespace pr = ui::playrows;
    Compact c;
    c.third = third;
    const int m = kMargin, W = third.w - 2 * m, rowH = pr::kCompactRowH, g = pr::kControlGap;
    c.instance = pr::headerSelectorRect (third, false, false);
    const int dy = third.y + m + pr::kSelectorH + pr::kSelectorGap;
    int qy = third.bottom() - m - kQuadRandomH;
    if (random)
        c.display = { third.x + m, dy, W, std::max (0, qy - 4 - dy) };
    else
    {
        c.display = pr::displayRect (third);
        qy = c.display.bottom() + 4;
    }
    int y = qy + ui::kQLane;
    auto rateCell = [&] (const ui::Rect& r)
    {
        c.rate = r;
        const int h = std::min (r.h, kModeH);
        c.modeCells = { r.right() - kModeW, r.y + (r.h - h) / 2, kModeW, h };
        c.rateKnob = { r.x, r.y, std::max (0, r.w - kModeW - g), r.h };
        const int dh = std::min (c.rateKnob.h, rowH);
        c.division = { c.rateKnob.x, c.rateKnob.y + (c.rateKnob.h - dh) / 2, c.rateKnob.w, dh };
    };
    if (random)
    {
        c.style = { third.x + m, y, W, rowH };
        y += rowH + g;
        rateCell ({ third.x + m, y, W, rowH });
        y += rowH + g;
        c.sync = { third.x + m, y, W, rowH };
        y += rowH + g;
        c.stereo = { third.x + m, y, W, rowH };
        return c;
    }
    c.shape = { third.x + m, y, W, rowH };
    y += rowH + g;
    rateCell ({ third.x + m, y, W, rowH });
    y += rowH + g;
    const int h = std::max (0, third.bottom() - m - y);
    const int cw = (W - g) / 2;
    c.rise = { third.x + m, y, cw, h };
    c.smoothTime = { third.x + m + cw + g, y, W - cw - g, h };
    return c;
}

/** The page of an LFO tab. */
struct Full
{
    static constexpr int kBandH = 290, kBandRandomH = 196, kMaxBlocks = 3;
    ui::Rect tab, targets, namedPicker, picker, display, edit;
    ui::Rect blocks[kMaxBlocks];
    int nBlocks = 0;
    ui::Rect rate, rise, smoothTime, smoothSwitch, smoothArea, syncMode, mode, modeList, phase, delay, host, direction, swing, stereo, mono, stereoToggle, keyTranspose, keyTune;
    int blockOfRate = 0;
};
inline Full full (const ui::Rect& tab, bool random, bool keyActive, bool editShown) noexcept
{
    Full f;
    f.tab = tab;
    const int W = tab.w, m = 8, g = kGap;
    const int bandH = random ? Full::kBandRandomH : Full::kBandH;
    const int bandY = tab.bottom() - m - bandH;
    int midY;
    // The page of an LFO sub-tab: the readable named waveform / style selector at the left of the top row, the TARGETS action button (200 x 56) at its right.
    const int topY = tab.y + m, targetsW = 200;
    midY = topY + 56 + m;
    f.targets = { tab.right() - m - targetsW, topY, targetsW, 56 };
    const int pickerX = tab.x + m;
    f.namedPicker = { pickerX, topY, std::max (0, (tab.right() - m - targetsW - 8) - pickerX), 56 };
    f.display = { tab.x + m, midY, W - 2 * m, std::max (0, bandY - m - midY) };
    f.edit = ! random && editShown ? ui::Rect { f.display.right() - 4 - 64, f.display.y + 4 + (64 - 56) / 2, 64, 56 } : ui::Rect {};
    // the blocks of the bottom band
    const int n = random ? 1 : 3;   // the key follow has no block of its own: its two numbers stand in the RATE block
    f.nBlocks = n;
    static const int wShape[3] = { 3, 4, 3 }, wRandom[1] = { 1 };
    const int* wt = random ? wRandom : wShape;
    int sum = 0;
    for (int i = 0; i < n; ++i)
        sum += wt[i];
    const int totalW = W - 2 * m - (n - 1) * m;
    int x = tab.x + m, used = 0;
    for (int i = 0; i < n; ++i)
    {
        used += wt[i];
        const int right = tab.x + m + totalW * used / sum + i * m;
        f.blocks[i] = { x, bandY, right - x, bandH };
        x = right + m;
    }
    const int pad = 6, titleH = 22, th = kLedRowH;
    auto inner = [&] (int i) { return ui::Rect { f.blocks[i].x + pad, f.blocks[i].y + titleH + ui::kQLane, f.blocks[i].w - 2 * pad, f.blocks[i].h - titleH - ui::kQLane - pad }; };
    auto stack2 = [&] (const ui::Rect& cell, ui::Rect& a, ui::Rect& b)   // two fields one above the other, as tall as the block lets them be (at most 96)
    {
        const int h = std::min (96, (cell.h - ui::kQLane) / 2);
        a = { cell.x, cell.y, cell.w, h };
        b = { cell.x, cell.y + h + ui::kQLane, cell.w, h };
    };
    if (! random)
    {
        const int kKnobH = 114;
        const ui::Rect in0 = inner (0);
        const int rw = std::min (in0.w * 45 / 100, in0.w - 172 - kGap);
        f.rate = { in0.x, in0.y, rw, in0.h };
        if (keyActive)
            stack2 ({ in0.x, in0.y, rw, in0.h }, f.keyTranspose, f.keyTune);
        const int sx = in0.x + rw + kGap, sw = in0.w - rw - kGap;
        f.rise = { sx, in0.y, sw, kKnobH };
        const ui::Rect smooth { sx, in0.bottom() - kKnobH, sw, kKnobH };
        constexpr int buttonW = 72;
        f.smoothArea = { smooth.x, smooth.y + 2, smooth.w, smooth.h - 2 };
        f.smoothSwitch = { smooth.x, smooth.y, buttonW, smooth.h };
        f.smoothTime = { smooth.x + buttonW, smooth.y, smooth.w - buttonW, smooth.h };
        const ui::Rect in = inner (1);
        const int lw = 150, kx = in.x + lw + g, kw = in.w - lw - g, topH = 3 * th, kwHalf = (kw - g) / 2;
        f.syncMode = { in.x, in.y, lw, topH };
        f.mode = { in.x, in.y + topH + ui::kQLane, lw, in.h - topH - ui::kQLane };
        f.phase = { kx, in.y, kwHalf, topH };
        f.delay = { kx + kw - kwHalf, in.y, kwHalf, topH };
        f.host = { kx, in.y + topH + ui::kQLane + std::max (0, (in.h - topH - ui::kQLane - th) / 2), kw, th };
        const ui::Rect im = inner (2);
        const int dlw = 120, dkx = im.x + dlw + g, dkw = im.w - dlw - g;
        f.direction = { im.x, im.y, dlw, 3 * th };
        f.swing = { dkx, im.y, dkw, kKnobH };
        f.stereo = { dkx, im.bottom() - kKnobH, dkw, kKnobH };
        f.mono = { im.x, im.y + 3 * th + ui::kQLane, dlw, im.h - 3 * th - ui::kQLane };
    }
    else
    {
        const ui::Rect in = inner (0);
        const int rw = std::min (220, in.w * 28 / 100), lh = 2 * th - 8;
        f.rate = { in.x, in.y, rw, in.h };
        if (keyActive)
            stack2 ({ in.x, in.y, rw, in.h }, f.keyTranspose, f.keyTune);
        const int x0 = in.x + rw + kGap, rest = in.right() - x0 - 2 * kGap;
        const int syncW = std::max (3 * 84, rest * 40 / 100), stereoW = std::max (112, rest * 18 / 100);
        f.syncMode = { x0, in.y, syncW, lh };
        f.stereoToggle = { x0 + syncW + kGap, in.y, stereoW, lh };
        f.modeList = { x0 + syncW + stereoW + 2 * kGap, in.y, in.right() - (x0 + syncW + stereoW + 2 * kGap), lh };
    }
    return f;
}

/** The list menu of the editor (the shapes, the actions) and of the targets (the destination): a header bar over at most eight rows, the same rows the host menu cells of nativeui::lfodetail take. */
struct Menu
{
    static constexpr int kRowH = 56, kPad = 4, kZoneW = 44, kBarW = 10, kBoxX = 340, kBoxW = 600, kMaxRows = 9;
    const char* header = nullptr;
    bool headerBack = false;
    std::vector<const char*> items;
    int first = 0;
    bool reserveZone = true;
    ui::Rect parent;   // the dimmed ground (the tab)

    int count() const noexcept { return static_cast<int> (items.size()); }
    int headerH() const noexcept { return header != nullptr ? kRowH : 0; }
    int contentHeight() const noexcept { return headerH() + std::max (1, count()) * kRowH + 2 * kPad; }
    /** THE rectangle of the box: the room of nine rows, as tall as its rows need, inside the dimmed tab. */
    ui::Rect box() const noexcept
    {
        ui::Rect r { kBoxX, parent.y + 20, kBoxW, kMaxRows * kRowH + 2 * kPad };
        r.h = std::max (kRowH + 2 * kPad + headerH(), std::min (r.h, contentHeight()));
        r.w = std::min (r.w, 1280);
        r.h = std::min (r.h, 800);
        r.x = std::clamp (r.x, 0, 1280 - r.w);
        r.y = std::clamp (r.y, 0, 800 - r.h);
        if (parent.w >= r.w && parent.h >= r.h)
        {
            r.x = std::clamp (r.x, parent.x, parent.right() - r.w);
            r.y = std::clamp (r.y, parent.y, parent.bottom() - r.h);
        }
        return r;
    }
    ui::Rect headerRect() const noexcept { const ui::Rect b = box(); return { b.x + kPad, b.y + kPad, b.w - 2 * kPad, headerH() }; }
    int visibleRows() const noexcept { return std::max (1, (box().h - 2 * kPad - headerH()) / kRowH); }
    int firstRow() const noexcept { return std::clamp (first, 0, std::max (0, count() - visibleRows())); }
    bool scrolls() const noexcept { return count() > visibleRows(); }
    ui::Rect rowRect (int row) const noexcept
    {
        const ui::Rect b = box();
        return { b.x + kPad, b.y + kPad + headerH() + (row - firstRow()) * kRowH, b.w - 2 * kPad - (scrolls() || reserveZone ? kZoneW : 0), kRowH };
    }
    ui::Rect zoneRect() const noexcept { const ui::Rect b = box(); return { b.right() - kPad - kZoneW, b.y + kPad + headerH(), kZoneW, b.h - 2 * kPad - headerH() }; }
    ui::Rect thumbRect() const noexcept
    {
        const ui::Rect b = box();
        const int n = std::max (1, count()), trackH = b.h - 2 * kPad - headerH(), thumbH = std::max (28, trackH * visibleRows() / n);
        const int thumbY = (trackH - thumbH) * firstRow() / std::max (1, n - visibleRows());
        return { b.right() - kPad - kZoneW / 2 - kBarW / 2, b.y + kPad + headerH() + thumbY, kBarW, thumbH };
    }
    /** The rows of the window that are native cells (at most `cap`). */
    int nativeRows (int cap) const noexcept { return std::clamp (count() - firstRow(), 0, std::min (visibleRows(), cap)); }
    void scroll (int rows) noexcept { first = std::clamp (firstRow() + rows, 0, std::max (0, count() - visibleRows())); }
    /** A drag in the scroll zone: the thumb follows the finger. */
    void dragThumb (int y) noexcept
    {
        const ui::Rect b = box();
        const int trackH = b.h - 2 * kPad - headerH(), thumbH = thumbRect().h, room = std::max (1, trackH - thumbH), rows = std::max (0, count() - visibleRows());
        const int ty = std::clamp (y - thumbH / 2 - (b.y + kPad + headerH()), 0, room);
        first = std::clamp ((ty * rows + room / 2) / room, 0, rows);
    }
};

}}} // namespace synthyboy_mpc::overlay::lfolayout
