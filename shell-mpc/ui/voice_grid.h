// SynthyBoy - MPC UI component library: VoiceGrid, the 8-step sequence and per-voice control grid (docs/MPC.md, ui-elem-mastervoice.md).
// Rows: 6 (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2); Columns: 8 steps.
// Drag-drawable bipolar vertical bars, row tabs on the left, live active voice step highlighting.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

class VoiceGrid : public Component
{
public:
    static constexpr int kNumRows = 6;
    static constexpr int kNumSteps = 8;
    static constexpr int kTabW = 96;
    static constexpr int kPad = 4;
    static constexpr int kColGap = 6;

    int length = 8;               // voicectl.length (1 .. 8)
    int activeRow = 0;            // 0..5 (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2)
    int selectedStep = 0;         // 0..7
    float values[kNumRows][kNumSteps] = {};     // -1.0 .. +1.0
    float modValues[kNumRows][kNumSteps] = {};
    uint8_t voiceStepMask = 0;    // bit s is 1 if step s has an active sounding voice
    int baseId = 100;             // baseId + row * 8 + col
    int rowTabBaseId = 150;       // rowTabBaseId + row

    static const char* rowName (int r) noexcept
    {
        static const char* const kNames[kNumRows] = { "PAN", "DETUNE", "CUTOFF", "ENV", "MOD 1", "MOD 2" };
        return r >= 0 && r < kNumRows ? kNames[r] : "";
    }

    VoiceGrid() = default;

    Size minSize() const override { return { 800, 260 }; }

    int stepId (int row, int step) const noexcept { return baseId + row * kNumSteps + step; }

    Rect tabRect (int r) const noexcept
    {
        if (r < 0 || r >= kNumRows) return {};
        const int tabAreaH = rect_.h - 2 * kPad;
        const int tabH = tabAreaH / kNumRows;
        return { rect_.x + kPad, rect_.y + kPad + r * tabH, kTabW - kPad, std::max (28, tabH - 3) };
    }

    Rect gridArea() const noexcept
    {
        const int gx = rect_.x + kTabW + kColGap;
        return { gx, rect_.y + kPad, std::max (0, rect_.right() - kPad - gx), std::max (0, rect_.h - 2 * kPad) };
    }

    Rect columnRect (int s) const noexcept
    {
        if (s < 0 || s >= kNumSteps) return {};
        const Rect g = gridArea();
        const int totalGap = (kNumSteps - 1) * kColGap;
        const int colW = std::max (10, (g.w - totalGap) / kNumSteps);
        return { g.x + s * (colW + kColGap), g.y, colW, g.h };
    }

    void setValue (int row, int step, float v) noexcept
    {
        if (row >= 0 && row < kNumRows && step >= 0 && step < kNumSteps)
            values[row][step] = std::clamp (v, -1.0f, 1.0f);
    }

    float value (int row, int step) const noexcept
    {
        return (row >= 0 && row < kNumRows && step >= 0 && step < kNumSteps) ? values[row][step] : 0.0f;
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        (void) nowMs;
        dragging_ = false;
        lastCol_ = -1;
        if (! enabled_ || ! rect_.contains (x, y))
            return Change::none();

        // 1. Check row tabs on left
        for (int r = 0; r < kNumRows; ++r)
        {
            if (tabRect (r).contains (x, y))
            {
                activeRow = r;
                return Change::indexed (rowTabBaseId + r, r, r);
            }
        }

        // 2. Check grid columns
        const Rect g = gridArea();
        if (g.contains (x, y))
        {
            const int s = findColumn (x);
            if (s >= 0 && s < kNumSteps)
            {
                selectedStep = s;
                dragging_ = true;
                lastCol_ = s;
                const float val = valueFromY (s, y);
                values[activeRow][s] = val;
                return Change::normalised (stepId (activeRow, s), val);
            }
        }
        return Change::none();
    }

    Change touchMove (int x, int y) override
    {
        if (! dragging_)
            return Change::none();

        const int s = findColumn (x);
        if (s >= 0 && s < kNumSteps)
        {
            selectedStep = s;
            const float val = valueFromY (s, y);
            if (s != lastCol_ || std::fabs (val - values[activeRow][s]) > 0.01f)
            {
                // a fast stroke skips columns: the columns between the last one and this one get the values on the line between them (drawn through, not left out); the owner takes them with takePending ()
                numPending_ = 0;
                if (lastCol_ >= 0 && std::abs (s - lastCol_) > 1)
                {
                    const int dir = s > lastCol_ ? 1 : -1;
                    const float v0 = values[activeRow][lastCol_];
                    for (int c = lastCol_ + dir; c != s && numPending_ < kNumSteps; c += dir)
                    {
                        const float t = static_cast<float> (c - lastCol_) / static_cast<float> (s - lastCol_);
                        float v = v0 + (val - v0) * t;
                        if (std::fabs (v) < 0.035f)
                            v = 0.0f;
                        values[activeRow][c] = v;
                        pending_[numPending_++] = Change::normalised (stepId (activeRow, c), v);
                    }
                }
                values[activeRow][s] = val;
                lastCol_ = s;
                return Change::normalised (stepId (activeRow, s), val);
            }
        }
        return Change::none();
    }
    /** The changes of the columns that a fast stroke passed over (after a touchMove): false when there are no more. */
    bool takePending (Change& c) noexcept
    {
        if (numPending_ <= 0)
            return false;
        c = pending_[--numPending_];
        return true;
    }

    Change touchUp (int x, int y) override
    {
        (void) x; (void) y;
        dragging_ = false;
        lastCol_ = -1;
        return Change::none();
    }

    Change turn (double delta) override
    {
        if (selectedStep < 0 || selectedStep >= kNumSteps)
            return Change::none();
        float val = std::clamp (values[activeRow][selectedStep] + static_cast<float> (delta * 0.05), -1.0f, 1.0f);
        if (std::fabs (val) < 0.02f)
            val = 0.0f;
        values[activeRow][selectedStep] = val;
        return Change::normalised (stepId (activeRow, selectedStep), val);
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen p (sb, enabled_);
        Cell cell (sb, rect_);
        drawBody (p);
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        // 1. Draw outer block
        p.block (rect_, false);

        // 2. Draw row tabs on the left
        for (int r = 0; r < kNumRows; ++r)
        {
            const Rect tr = tabRect (r);
            const bool active = (r == activeRow);
            if (active)
            {
                p.paintY2 (static_cast<float> (tr.y), static_cast<float> (tr.bottom()), T.flow[0], T.flow[1], 255);
                p.rrect (tr, T.rChip, p.c (colour::rgba (T.flow[0], 255)));
                p.plain();
                p.textCentered (tr, rowName (r), T.fLabel, p.c (T.onBright, 255), T.fMin);
            }
            else
            {
                p.rrect (tr, T.rChip, p.c (T.panel1, 200));
                p.rrect (tr, T.rChip, p.c (T.keyline, 160), 1.0f);
                p.textCentered (tr, rowName (r), T.fLabel - 1.0f, p.c (T.textDim, 255), T.fMin);
            }
        }

        // 3. Draw step columns
        for (int s = 0; s < kNumSteps; ++s)
        {
            const Rect cr = columnRect (s);
            const bool activeStep = (s < length);
            const bool isSelected = (s == selectedStep);
            const bool isSounding = (voiceStepMask & (1 << s)) != 0;

            // Column background box
            p.rrect (cr, T.rControl, p.c (activeStep ? T.panel0 : T.bg1, activeStep ? 180 : 120));
            if (isSounding)   // the step a voice plays: the whole column carries a tint of the signal colour, besides the lit frame and the dot
                p.rrect (cr, T.rControl, p.c (T.flow[0], 34));
            uint32_t borderCol = isSelected ? T.q : (isSounding ? T.flow[0] : (activeStep ? T.keyline : T.track));
            float borderThick = isSelected ? 1.8f : (isSounding ? 2.2f : 1.0f);
            p.rrect (cr, T.rControl, p.c (borderCol, 255), borderThick);

            // Step header area
            const Rect hdr { cr.x, cr.y + 4, cr.w, 24 };
            char numBuf[8];
            std::snprintf (numBuf, sizeof numBuf, "%d", s + 1);
            p.textCentered (hdr, numBuf, T.fLabel, p.c (activeStep ? T.text : T.textDim, 255), T.fMin);

            // the step a voice plays now: its frame is lit, with a dot at the top
            if (isSounding)
                p.disc (static_cast<float> (cr.right() - 12), static_cast<float> (cr.y + 14), 4.5f, p.c (T.flow[0], 255));

            // Bar drawing area
            const int barPadX = 6;
            const int barTop = cr.y + 32;
            const int barBottom = cr.bottom() - 30;
            const int barH = std::max (10, barBottom - barTop);
            const int barW = std::max (6, cr.w - 2 * barPadX);
            const int barX = cr.x + barPadX;
            const int midY = barTop + barH / 2;

            // Track background
            p.rect (barX, barTop, barX + barW, barBottom, p.c (T.track, activeStep ? 120 : 60));

            // Zero baseline
            p.line (static_cast<float> (barX), static_cast<float> (midY),
                    static_cast<float> (barX + barW), static_cast<float> (midY),
                    1.2f, p.c (T.keylineHi, activeStep ? 220 : 120));

            // Bar fill
            const float val = values[activeRow][s];
            if (std::fabs (val) > 0.005f)
            {
                const int extent = static_cast<int> (std::lround (std::fabs (val) * (barH * 0.47f)));
                if (val > 0.0f)
                {
                    const int topY = std::max (barTop, midY - extent);
                    p.paintY2 (static_cast<float> (topY), static_cast<float> (midY), T.flow[0], T.flow[1], 255);
                    p.rect (barX + 1, topY, barX + barW - 1, midY, p.c (colour::rgba (T.flow[0], 255)));
                    p.plain();
                    // Crisp cap line
                    p.line (static_cast<float> (barX + 1), static_cast<float> (topY),
                            static_cast<float> (barX + barW - 1), static_cast<float> (topY),
                            1.5f, p.c (T.text, 255));
                }
                else
                {
                    const int botY = std::min (barBottom, midY + extent);
                    p.paintY2 (static_cast<float> (midY), static_cast<float> (botY), T.negative, T.flow[3], 255);
                    p.rect (barX + 1, midY, barX + barW - 1, botY, p.c (colour::rgba (T.negative, 255)));
                    p.plain();
                    // Crisp cap line
                    p.line (static_cast<float> (barX + 1), static_cast<float> (botY),
                            static_cast<float> (barX + barW - 1), static_cast<float> (botY),
                            1.5f, p.c (T.text, 255));
                }
            }

            // The handle at the value, ALSO at zero (on the middle line): every step of the length can be grasped, whatever its value
            if (activeStep)
            {
                const int extentV = static_cast<int> (std::lround (std::fabs (val) * (barH * 0.47f)));
                const float hy = static_cast<float> (val > 0.0f ? std::max (barTop, midY - extentV) : (val < 0.0f ? std::min (barBottom, midY + extentV) : midY));
                const float hx = static_cast<float> (barX) + static_cast<float> (barW) * 0.5f;
                p.disc (hx, hy, 6.0f, p.c (T.text, 255));
                if (val == 0.0f)
                    p.arc (hx, hy, 9.5f, 0.0f, 2.0f * kPi, 1.2f, p.c (T.textDim, 200));
            }

            // Value text area below bar
            const Rect valBox { cr.x + 2, barBottom + 4, cr.w - 4, 22 };
            char valStr[24];
            formatValue (activeRow, val, valStr, sizeof valStr);
            p.textCentered (valBox, valStr, T.fLabel, p.c (activeStep ? (isSelected ? T.q : T.text) : T.textDim, 255), T.fMin);
        }
    }

private:
    bool dragging_ = false;
    int lastCol_ = -1;
    Change pending_[kNumSteps];
    int numPending_ = 0;

    int findColumn (int x) const noexcept
    {
        for (int s = 0; s < kNumSteps; ++s)
            if (columnRect (s).x <= x && x < columnRect (s).right())
                return s;
        return -1;
    }

    float valueFromY (int s, int y) const noexcept
    {
        const Rect cr = columnRect (s);
        const int barTop = cr.y + 32;
        const int barBottom = cr.bottom() - 30;
        const int barH = std::max (10, barBottom - barTop);
        const float midY = static_cast<float> (barTop + barH / 2);
        const float halfH = static_cast<float> (barH) * 0.47f;
        float v = (midY - static_cast<float> (y)) / halfH;
        v = std::clamp (v, -1.0f, 1.0f);
        if (std::fabs (v) < 0.035f)
            v = 0.0f; // Deadzone snap to 0
        return v;
    }

public:
    static void formatValue (int row, float val, char* out, size_t maxLen) noexcept
    {
        if (std::fabs (val) < 0.005f)
        {
            if (row == 0) std::snprintf (out, maxLen, "MID");
            else if (row == 1) std::snprintf (out, maxLen, "0 c");
            else std::snprintf (out, maxLen, "0%%");
            return;
        }
        if (row == 0) // PAN: %
        {
            int p = static_cast<int> (std::lround (val * 100.0f));
            if (p > 0) std::snprintf (out, maxLen, "%dR", p);
            else std::snprintf (out, maxLen, "%dL", -p);
        }
        else if (row == 1) // DETUNE: cents
        {
            int c = static_cast<int> (std::lround (val * 100.0f));
            std::snprintf (out, maxLen, "%+d c", c);
        }
        else // CUTOFF, ENV, MOD1, MOD2: %
        {
            int p = static_cast<int> (std::lround (val * 100.0f));
            std::snprintf (out, maxLen, "%+d%%", p);
        }
    }
};

}} // namespace synthyboy_mpc::ui
