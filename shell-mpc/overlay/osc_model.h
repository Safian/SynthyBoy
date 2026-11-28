// SynthyBoy - MPC overlay: the PLAIN DATA of the oscillator pages (owner decision 2026-11-27: the C++ layer has no UI elements). The controls of the oscillator pages are native cells of the host skin; this header holds what the plugin
// still has to know about them: the state of a cell (`Cell`: its rectangle, whether it is shown / enabled, the value and the text the owner reads from the parameter), the rectangles of the native cells of the compact view, of the full
// screen (the top row, the bottom band with its titled blocks, the unison box, the warp slots), of the detail pages (the grid of a preset under the display) and the detail pages of the types (`SpecPage`: which controls apply, the Q groups).
// Pure functions of rectangles and flags: no drawing, no touch, no virtuals. The skin's native cells are placed into these rectangles.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../ui/core.h"
#include "../ui/play_rows.h"
#include "mod_model.h"
#include "osc_presets.h"

namespace synthyboy_mpc { namespace overlay { namespace oscmodel {

using ui::Rect;
using modmodel::layoutRow;
constexpr int kGap = modmodel::kGap, kQLane = ui::kQLane;

enum class Instance : uint8_t { Sub, A, B, C, Noise };
enum class Kind : uint8_t { Wavetable, Multisample, Granular, Sample, Spectral };   // the five types of A / B / C (engine oscN.type 0, 4, 2, 1, 3)
inline const char* instanceName (Instance i) noexcept { static const char* const n[5] = { "SUB", "A", "B", "C", "NOISE" }; return n[static_cast<int> (i)]; }
inline const char* const* sourceNames() noexcept { static const char* const n[5] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" }; return n; }

/** The number format of a number cell. */
enum class NumberFormat : uint8_t { IntSigned, Decimal2, Degrees, IntPlain };

/** The state of one native cell as the page reads it from the parameter: the rectangle the host's control stands in, whether it applies (`shown`) and takes edits (`enabled`), the value and the text. Plain data: the owner writes it
    on every update, nothing in it is the truth. */
struct Cell
{
    Rect rect;
    bool shown = true, enabled = true;
    char label[24] = {};           // the name of the cell
    char text[48] = {};            // the text it shows (a stepper's item, a knob's value, a source's file name)
    double value = 0.0;            // a knob: normalised 0 .. 1; a number: its plain value; an up / down count: the count; a toggle: 0 / 1
    double min = 0.0, max = 1.0, step = 1.0, def = 0.0;
    int index = 0, count = 0;      // a stepper / pictogram selector: the position and the number of items
    bool on = false, wrap = false, bipolar = false;
    NumberFormat format = NumberFormat::IntSigned;
};
inline void setLabel (Cell& c, const char* s) noexcept { ui::setText (c.label, sizeof c.label, s != nullptr ? s : ""); }
inline void setText (Cell& c, const char* s) noexcept { ui::setText (c.text, sizeof c.text, s != nullptr ? s : ""); }

// =============================================================================================================================================================
// the bottom band of a full screen / the grid of a detail page : blocks, cells, the composites' parts
constexpr int kBlockTitleH = 38, kBlockPad = 6, kBlockGap = 8, kMaxStepperH = 72, kOneRowH = 128, kMaxBlocks = 4, kMaxItems = 12, kSeparatorGap = 16, kPanelMargin = 4;
constexpr int kUnisonGearH = 56, kWarpAmountW = 184, kWarpStepperMaxW = 300;

struct PanelCells
{
    Rect item[kMaxItems];            // the rectangle of every item of the preset (a composite: its whole rectangle)
    Rect block[kMaxBlocks];
    int sepX = -1;
    Rect unisonGear, unisonCount;
    Rect warpType[2], warpAmount[2];
};

/** The warp slot (amount knob, type stepper) in a rectangle: with the amount hidden the type has the room (at most 120 px tall); `tall`: the slot has a whole block to itself. */
inline void layoutWarpSlot (const Rect& rect, bool applies, bool tall, Rect& amount, Rect& type) noexcept
{
    if (! applies)
    {
        const int sw = rect.w, sh = std::min (rect.h, 120);
        amount = {};
        type = { rect.x + (rect.w - sw) / 2, rect.y + (rect.h - sh) / 2, sw, sh };
        return;
    }
    const int contentW = kWarpAmountW + kGap + kWarpStepperMaxW;
    const Rect area = rect.w > contentW ? Rect { rect.x + (rect.w - contentW) / 2, rect.y, contentW, rect.h } : rect;
    Rect r[2];
    const int fixed[2] = { kWarpAmountW, 0 }, weight[2] = { 0, 1 };
    layoutRow (area, fixed, weight, 2, kGap, r);
    const int sh = std::min (r[1].h, tall ? 120 : 64);
    amount = r[0];
    type = { r[1].x, r[1].y + (r[1].h - sh) / 2, r[1].w, sh };
}

/** The rows of block `bl` inside `in`: the first row fixed when the block says so, the rows share what is left; kQLane between the rows. */
inline void blockRows (const PanelBlock& bl, const Rect& in, int* y, int* h) noexcept
{
    const int n = std::max (1, static_cast<int> (bl.innerRows)), gap = n > 1 ? kQLane : kGap;
    const int fixed = bl.firstRowH > 0 && n > 1 ? bl.firstRowH : 0, rest = n - (fixed > 0 ? 1 : 0);
    const int free = std::max (0, in.h - gap * (n - 1) - fixed);
    int yy = in.y;
    for (int r = 0; r < n; ++r)
    {
        h[r] = r == 0 && fixed > 0 ? fixed : (rest > 0 ? free / rest : 0);
        y[r] = yy;
        yy += h[r] + gap;
    }
}
inline bool blockHasCapped (const PanelPreset& p, int b) noexcept
{
    for (int i = 0; i < p.numItems && i < kMaxItems; ++i)
        if (p.items[i].block == b && (p.items[i].kind == PartKind::Choice || p.items[i].kind == PartKind::Menu || p.items[i].kind == PartKind::Toggle || p.items[i].kind == PartKind::Number))
            return true;
    return false;
}

/** Lays the items of a preset out in `rect` (the grid, or the titled blocks), and the parts of the unison box and of the warp pair. */
inline PanelCells layoutPanel (const PanelPreset& p, const Rect& rect, bool warpSingle, const bool* warpApplies) noexcept
{
    PanelCells c;
    if (p.numBlocks > 0)
    {
        const int nb = std::min (p.numBlocks, kMaxBlocks);
        const int cw = std::max (0, (rect.w - kBlockGap * (p.cols - 1)) / p.cols), ch = std::max (0, (rect.h - kBlockGap * (p.rows - 1)) / p.rows);
        for (int b = 0; b < nb; ++b)
        {
            const PanelBlock& bl = p.blocks[b];
            const int x0 = rect.x + bl.col * (cw + kBlockGap), y0 = rect.y + bl.row * (ch + kBlockGap);
            const int x1 = rect.x + (bl.col + bl.colSpan - 1) * (cw + kBlockGap) + cw, y1 = rect.y + (bl.row + bl.rowSpan - 1) * (ch + kBlockGap) + ch;
            c.block[b] = { x0, y0, x1 - x0, y1 - y0 };
            const Rect in { x0 + kBlockPad, y0 + kBlockTitleH, std::max (0, x1 - x0 - 2 * kBlockPad), std::max (0, y1 - y0 - kBlockTitleH - kBlockPad) };
            const int icw = std::max (0, (in.w - kGap * (bl.innerCols - 1)) / bl.innerCols);
            int ry[4], rh[4];
            blockRows (bl, in, ry, rh);
            for (int i = 0; i < p.numItems && i < kMaxItems; ++i)
            {
                const PanelItem& it = p.items[i];
                if (it.block != b)
                    continue;
                const int r0 = std::min<int> (it.row, 3), r1 = std::min<int> (it.row + it.rowSpan - 1, 3);
                const int ix0 = in.x + it.col * (icw + kGap), ix1 = in.x + (it.col + it.colSpan - 1) * (icw + kGap) + icw;
                int iy0 = ry[r0], iy1 = ry[r1] + rh[r1];
                const bool oneRow = bl.innerRows == 1 && blockHasCapped (p, b);
                const int cap = oneRow ? kOneRowH : (it.kind == PartKind::Choice || it.kind == PartKind::Menu ? kMaxStepperH : 0);
                if (cap > 0 && iy1 - iy0 > cap)
                {
                    const int mid = (iy0 + iy1) / 2;
                    iy0 = mid - cap / 2;
                    iy1 = iy0 + cap;
                }
                c.item[i] = { ix0, iy0, ix1 - ix0, iy1 - iy0 };
            }
        }
    }
    else
    {
        const Rect in = rect.inset (kPanelMargin);
        const int extra = p.separatorBefore >= 0 ? kSeparatorGap - kGap : 0;
        const int cw = std::max (0, (in.w - kGap * (p.cols - 1) - extra) / p.cols), ch = std::max (0, (in.h - kGap * (p.rows - 1)) / p.rows);
        auto colX = [&] (int col) { return in.x + col * (cw + kGap) + (p.separatorBefore >= 0 && col >= p.separatorBefore ? extra : 0); };
        auto rowY = [&] (int r) { return in.y + r * (ch + kGap); };
        for (int i = 0; i < p.numItems && i < kMaxItems; ++i)
        {
            const PanelItem& it = p.items[i];
            const int x0 = colX (it.col), y0 = rowY (it.row), x1 = colX (it.col + it.colSpan - 1) + cw, y1 = rowY (it.row + it.rowSpan - 1) + ch;
            c.item[i] = { x0, y0, x1 - x0, y1 - y0 };
        }
        if (p.separatorBefore >= 0)
            c.sepX = colX (p.separatorBefore) - (kGap + extra) / 2;
    }
    const bool stacked = p.numBlocks > 0;
    for (int i = 0; i < p.numItems && i < kMaxItems; ++i)
    {
        const Rect r = c.item[i];
        if (p.items[i].kind == PartKind::Unison)
        {
            c.unisonGear = { r.x, r.bottom() - kUnisonGearH, r.w, kUnisonGearH };
            c.unisonCount = { r.x, r.y, r.w, std::max (0, r.h - kGap - kUnisonGearH) };
        }
        else if (p.items[i].kind == PartKind::WarpPair && stacked)
        {
            Rect first = r, second {};
            if (! warpSingle)
            {
                const int h = std::max (0, (r.h - kQLane) / 2);
                first = { r.x, r.y, r.w, h };
                second = { r.x, r.y + h + kQLane, r.w, std::max (0, r.h - h - kQLane) };
            }
            layoutWarpSlot (first, warpApplies == nullptr || warpApplies[0], warpSingle, c.warpAmount[0], c.warpType[0]);
            if (! warpSingle)
                layoutWarpSlot (second, warpApplies == nullptr || warpApplies[1], false, c.warpAmount[1], c.warpType[1]);
        }
    }
    return c;
}

/** The least height of the preset's blocks (blockMinSize): what the part kinds need; the display of a detail page takes the room the blocks do not need. */
inline int partMinHeight (const PanelItem& it, bool warpSingle) noexcept
{
    switch (it.kind)
    {
        case PartKind::Dial: return 72;
        case PartKind::RandPair: return 72 + 8 + 56;
        case PartKind::Unison: return 112 + kGap + kUnisonGearH;
        case PartKind::WarpPair: return warpSingle ? 72 : 72 + 72 + kQLane;
        default: return 56;
    }
}
inline int panelMinHeight (const PanelPreset& p, bool warpSingle) noexcept
{
    if (p.numBlocks == 0)
    {
        int rowH = 0;
        for (int i = 0; i < p.numItems && i < kMaxItems; ++i)
            rowH = std::max (rowH, (partMinHeight (p.items[i], warpSingle) - (p.items[i].rowSpan - 1) * kGap + p.items[i].rowSpan - 1) / p.items[i].rowSpan);
        return rowH * p.rows + kGap * (p.rows - 1) + 2 * kPanelMargin;
    }
    const int nb = std::min (p.numBlocks, kMaxBlocks);
    int rowH = 0;
    for (int b = 0; b < nb; ++b)
    {
        const PanelBlock& bl = p.blocks[b];
        int ich = 0;
        for (int i = 0; i < p.numItems && i < kMaxItems; ++i)
        {
            const PanelItem& it = p.items[i];
            if (it.block != b)
                continue;
            if (! (bl.firstRowH > 0 && it.row == 0))
                ich = std::max (ich, (partMinHeight (it, warpSingle) - (it.rowSpan - 1) * kQLane + it.rowSpan - 1) / it.rowSpan);
        }
        const int rowsH = (bl.firstRowH > 0 ? bl.firstRowH + std::max (1, bl.innerRows - 1) * ich : bl.innerRows * ich) + (bl.innerRows - 1) * (bl.innerRows > 1 ? kQLane : kGap);
        const int bh = rowsH + kBlockTitleH + kBlockPad;
        rowH = std::max (rowH, (bh - (bl.rowSpan - 1) * kBlockGap + bl.rowSpan - 1) / bl.rowSpan);
    }
    return rowH * p.rows + kBlockGap * (p.rows - 1);
}

// =============================================================================================================================================================
// the detail page of a preset (unison settings, grain window, multisample envelope): BACK, the display, the grid
struct DetailLayout
{
    static constexpr int kBarH = 64;
    Rect back, display, panel;
    PanelCells cells;
};
inline DetailLayout layoutDetail (const PanelPreset& p, const Rect& rect, bool warpSingle = false) noexcept
{
    DetailLayout d;
    d.back = { rect.x + 4, rect.y + 4, 120, DetailLayout::kBarH - 8 };
    int dh = p.displayH;
    if (dh > 0)   // the picture takes the room the blocks do not need (their minimum and a little air), never less than the preset says
        dh = std::max (dh, rect.h - DetailLayout::kBarH - 8 - panelMinHeight (p, warpSingle) - 24);
    d.display = dh > 0 ? Rect { rect.x + 4, rect.y + DetailLayout::kBarH + 4, rect.w - 8, dh } : Rect {};
    const int top = rect.y + DetailLayout::kBarH + (dh > 0 ? dh + 8 : 0);
    d.panel = { rect.x, top, rect.w, std::max (0, rect.bottom() - top) };
    bool applies[2] = { true, true };
    d.cells = layoutPanel (p, d.panel, warpSingle, applies);
    return d;
}

// =============================================================================================================================================================
// the compact view (the oscillator third of the Play page)
struct CompactLayout
{
    Rect third, display, power, route, instances, basic, first, second, pan, level;
    Rect plot() const noexcept { return ui::playrows::compactPlotRect (display); }   // Play phase 2: no band, the buttons stand in the header band above the display
};
inline CompactLayout layoutCompact (const Rect& third, Instance inst) noexcept
{
    namespace pr = ui::playrows;
    CompactLayout c;
    c.third = third;
    const int m = pr::kMargin, W = third.w - 2 * m, g = pr::kControlGap;
    const bool sub = inst == Instance::Sub;
    c.display = pr::displayRect (third);
    c.power = pr::headerPowerRect (third);
    c.route = sub ? Rect {} : pr::headerRouteRect (third);
    c.instances = pr::headerSelectorRect (third, true, ! sub);
    const int by = pr::controlsLine (third);
    c.basic = { third.x + m, by, W, std::max (0, third.bottom() - m - by) };
    // the stacked arrangement: a 60 px row of titled dropdowns (the Sub: WAVE and the 56 px OCT box) over a row of two 56 px dials (PAN, LEVEL), 8 px gaps
    const int bRowH = std::max (0, (c.basic.h - g) / 2), row2Y = c.basic.y + bRowH + g, cw = (c.basic.w - g) / 2;
    const int h1 = std::min (bRowH, pr::kCompactRowH), y1 = c.basic.y + (bRowH - h1) / 2;
    if (sub)
    {
        // WAVE: a titled dropdown (20 px title, 40 px box); OCT: a 40 px box in a 56 px touch slot whose box lines up with the WAVE box (title 20 - pad 8 = 12 px below the top of the row)
        const int octW = 116, octH = 56, titleH = 20, boxH = 40, waveW = c.basic.w - octW - g;   // 284 + 8 + 116 = 408
        c.first = { c.basic.x, y1, waveW, h1 };
        c.second = { c.basic.x + waveW + g, y1 + titleH - (octH - boxH) / 2, octW, octH };
    }
    else
    {
        c.first = { c.basic.x, y1, cw, h1 };
        c.second = { c.basic.x + cw + g, y1, c.basic.w - cw - g, h1 };
    }
    c.pan = { c.basic.x, row2Y, cw, bRowH };
    c.level = { c.basic.x + cw + g, row2Y, c.basic.w - cw - g, bRowH };
    return c;
}
/** The line of text over the picture of the compact view (the warning of a missing content): the top strip of the display, over the plot (the band of the buttons is the header band above the display since phase 2). */
inline Rect compactLabelRect (const CompactLayout& c, Instance) noexcept { return { c.display.x + 14, c.display.y + 4, std::max (0, c.display.w - 28), 32 }; }

// =============================================================================================================================================================
// the full screen
struct FullState
{
    Instance inst = Instance::A;
    Kind kind = Kind::Wavetable;
    bool noiseSample = false;
    bool pageTab = false;          // the screen is a PAGE of a tab (the tab names the oscillator)
    bool nativeSelector = false, nativeRouting = false, nativePower = false;
};

/** The text width of the cell of a pictogram selector with these labels (the width of the longest at the label size, at least 56). */
inline int pictogramCellW (int longestLabel) noexcept
{
    return std::max (56, static_cast<int> (std::ceil (ui::sdf::textWidth ("MMMMMMMMMMMMMMMM", ui::theme::L().fLabel - 2.0f) * static_cast<float> (longestLabel) / 16.0f)) + 10);
}

constexpr int kMaxGroups = 8, kBottomH = 220, kMoreW = 120, kNativeSelectorW = 200;
constexpr size_t kNativePanelCells = 24;
using NativeCells = std::array<Rect, kNativePanelCells>;
constexpr Rect tuneAreaAt (int x, int y) noexcept { return { x + 994, y + 202, 278, 154 }; }

struct FullLayout
{
    Rect selectorArea, routingArea, display, power, mix, top, panel, more, subBlend;
    // the top row (the first Q group): the cells of the members that exist for the instance
    Rect tSelector, tSource, tSourceStepper, tSourceDropdown, tKind, tSound, tPos, tOct, tStartPhase, tRandomPhase, tWaveMap;
    PanelCells cells;
    const PanelPreset* preset = nullptr;
    bool warpSingle = false;
};

inline bool hasNativeTune (const FullState& s) noexcept { return s.nativeRouting && s.inst != Instance::Sub && s.inst != Instance::Noise; }

/** The preset of the bottom band of the screen of an instance. */
inline const PanelPreset& fullPreset (Instance inst, Kind kind, bool noiseSample) noexcept
{
    switch (inst)
    {
        case Instance::Sub: return presets::kSubFull;
        case Instance::Noise: return noiseSample ? presets::kNoiseSampleFull : presets::kNoiseGenFull;
        default:
            return kind == Kind::Wavetable ? presets::kWavetableFull : kind == Kind::Multisample ? presets::kMultisampleFull : kind == Kind::Sample ? presets::kSampleFull
                 : kind == Kind::Spectral ? presets::kSpectralFull : presets::kGranularFull;
    }
}

/** The members of the top row (the first Q group), in order, by what they are: 0 selector, 1 source, 2 position knob, 3 OCT, 4 kind, 5 sound. */
enum TopMember : int { kTopSelector = 0, kTopSource, kTopPos, kTopOct, kTopKind, kTopSound };
inline int topRowMembers (const FullState& s, int* out) noexcept
{
    const bool sub = s.inst == Instance::Sub, noise = s.inst == Instance::Noise, abc = ! sub && ! noise;
    int n = 0;
    if (abc) { out[n++] = kTopSelector; out[n++] = kTopSource; out[n++] = kTopPos; out[n++] = kTopOct; }
    else if (sub) { out[n++] = kTopSelector; out[n++] = kTopOct; }
    else { out[n++] = kTopKind; if (! s.noiseSample) out[n++] = kTopSound; }
    return n;
}

inline FullLayout layoutFull (const Rect& rect, const FullState& s, const bool* warpApplies = nullptr) noexcept
{
    FullLayout L;
    const int W = rect.w, H = rect.h, m = 8;
    const bool sub = s.inst == Instance::Sub, noise = s.inst == Instance::Noise, abc = ! sub && ! noise;
    const bool oneRow = sub || noise;
    const bool isTab = H <= 650;
    const int topH = isTab ? 78 : 88;
    const bool tune = hasNativeTune (s);
    const int bottomH = oneRow ? kBlockTitleH + kOneRowH + kBlockPad : (s.nativeSelector && tune ? 176 : isTab ? 216 : kBottomH);
    const int lane = isTab ? 22 : (m + kQLane + 2);
    const Rect topR { rect.x + m, rect.y + lane, W - 2 * m, topH };
    Rect r[4];
    const int fixed[4] = { s.nativeSelector ? kNativeSelectorW : 84, 64, 0, 150 }, weight[4] = { 0, 0, 1, 0 };
    layoutRow (topR, fixed, weight, 4, kGap, r);
    L.selectorArea = { r[0].x, topR.y + (topH - 56) / 2, r[0].w, 56 };
    if (s.nativeSelector)
        L.selectorArea = {};   // a page of a tab has no strip: the hardware sub-tab names the oscillator
    const bool column = s.nativeSelector && tune;
    L.top = column ? Rect { rect.x, rect.y, W, 194 } : r[2];
    // ---- the top row
    const bool waveTools = column && s.kind == Kind::Wavetable && abc;
    if (column)
    {
        const int x = L.top.x, y = L.top.y, right = L.top.right() - 286;
        L.tSelector = { x + 122, y + 4, 256, 56 };
        L.tSource = { x + 386, y + 4, right - x - 394, 56 };
        L.tSourceStepper = { L.tSource.x, L.tSource.y, L.tSource.w - 116, 56 };
        L.tSourceDropdown = { L.tSource.right() - 112, L.tSource.y, 112, 56 };
        L.tPos = { right, y + 92, waveTools ? 146 : 278, 48 };
        L.tWaveMap = { right + 150, y + 92, 128, 48 };
        L.tStartPhase = { right, y + 146, 136, 48 };
        L.tRandomPhase = { right + 140, y + 146, 138, 48 };
        L.tOct = { right, y + 204, 64, 48 };
    }
    else
    {
        int members[4];
        const int n = topRowMembers (s, members);
        if (n > 0)
        {
            // the cells of the row share its width in proportion to their least widths; a knob has the whole height, the others at most 72 px in the middle of it
            const int selW = (abc ? 5 : 6) * pictogramCellW (abc ? 6 : 5);
            int fx[4] = {}, wt[4] = {};
            for (int i = 0; i < n; ++i)
            {
                switch (members[i])
                {
                    case kTopSelector: wt[i] = selW; break;
                    case kTopSource: wt[i] = 208 + kGap + 56; break;
                    case kTopPos: wt[i] = 172; break;
                    case kTopOct: wt[i] = 80; break;
                    default: wt[i] = 208; break;
                }
            }
            Rect cells[4];
            layoutRow (L.top, fx, wt, n, kGap, cells);
            for (int i = 0; i < n; ++i)
            {
                const bool knob = members[i] == kTopPos;
                const int minH = 56;
                const int h = knob ? cells[i].h : std::min (cells[i].h, std::max (minH, 72));
                const Rect cell { cells[i].x, cells[i].y + (cells[i].h - h) / 2, cells[i].w, h };
                switch (members[i])
                {
                    case kTopSelector: L.tSelector = cell; break;
                    case kTopSource:
                        L.tSource = cell;
                        L.tSourceStepper = { cell.x, cell.y, std::max (0, cell.w - kGap - 56), cell.h };
                        L.tSourceDropdown = { cell.right() - 56, cell.y, 56, cell.h };
                        break;
                    case kTopPos: L.tPos = cell; break;
                    case kTopOct: L.tOct = cell; break;
                    case kTopKind: L.tKind = cell; break;
                    default: L.tSound = cell; break;
                }
            }
        }
    }
    const int routeH = s.nativeRouting ? 48 : 56;
    L.routingArea = { r[3].x, topR.y + (topH - routeH) / 2, r[3].w, routeH };
    L.mix = { r[3].x, topR.y + 7, r[3].w, topH - 14 };
    const int gap = kGap + 4;
    const int panelY = rect.bottom() - (s.nativeSelector ? 48 : isTab ? 0 : m) - bottomH;
    const int midY = column ? rect.y + 64 : L.top.bottom() + gap;
    L.display = { rect.x + m, midY, W - 2 * m, std::max (0, panelY - gap - midY) };
    if (tune)
        L.display.w = tuneAreaAt (rect.x, rect.y).x - m - L.display.x;
    L.power = ui::playrows::powerRect (column ? Rect { rect.x + 50, rect.y, L.display.w, 64 } : L.display);
    L.panel = { rect.x + m, panelY, W - 2 * m - (noise ? 0 : kMoreW + m), bottomH };
    const Rect card = tuneAreaAt (rect.x, rect.y);
    L.more = tune ? Rect { card.x + 143, card.y + 98, 125, 48 } : Rect { rect.right() - m - kMoreW, panelY + kBlockTitleH, kMoreW, 72 };
    L.subBlend = { rect.right() - m - kMoreW, panelY + kBlockTitleH, kMoreW, bottomH - kBlockTitleH - kBlockPad };
    L.preset = &fullPreset (s.inst, s.kind, s.noiseSample);
    L.warpSingle = abc && s.kind != Kind::Wavetable;   // the default view of the other types has warp 1 only (warp 2 is behind MORE)
    L.cells = layoutPanel (*L.preset, L.panel, L.warpSingle, warpApplies);
    return L;
}


}}} // namespace synthyboy_mpc::overlay::oscmodel
