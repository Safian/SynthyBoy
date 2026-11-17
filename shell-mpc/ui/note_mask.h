// SynthyBoy - MPC UI component library: NoteMask, the twelve notes of an octave as a row of LED cells in keyboard order (C C# D D# E F F# G G# A A# B): a lit cell is a note that is in the set. The black keys are
// recognisable (a darker cell with the sharp in its name, in the upper row), the white ones are light. Same contract as every component (core.h).
// Laid out as a piano: the seven white notes in the lower row, the five black ones in the upper row over the gaps between the whites (C# D# F# G# A#). In: the mask (bit 0 = C .. bit 11 = B), a label. Out: an Index change { id, index = the new mask, value = the new mask } on a tap on a cell (made on the touch up); turn (delta) steps the mask as a number by whole
// steps (what a Q-Link knob that is mapped to the parameter of the mask does), the wheel the same. Every cell is at least 56 px wide and high.
// Look: the palette and fonts of the library; the same flat dot as the other LED controls; no glow.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "led_toggle.h"

namespace synthyboy_mpc { namespace ui {

class NoteMask : public Component
{
public:
    static constexpr int kNotes = 12;
    struct State
    {
        int mask = 0;           // bit i set: the note i of the octave (0 = C) is in the set
        char label[24] = {};
    };
    State state;

    void setLabel (const char* s) { setText (state.label, sizeof state.label, s); }
    static bool isBlack (int note) noexcept { return note == 1 || note == 3 || note == 6 || note == 8 || note == 10; }
    static const char* noteName (int note) noexcept
    {
        static const char* const n[kNotes] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return n[note % kNotes];
    }
    Size minSize() const override { return { 7 * 62, 2 * 56 + 8 }; }
    /** The index of a white note (C D E F G A B = 0 .. 6) for the note, -1 for a black one. */
    static int whiteIndex (int note) noexcept { static const int w[kNotes] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 }; return w[note % kNotes]; }
    /** The cell of a note: a white one in the lower row under its place, a black one in the upper row centred on the border between the two whites around it. */
    Rect cellRect (int i) const noexcept
    {
        const int rowH = std::max (0, (rect_.h - 8) / 2), ww = rect_.w / 7;
        const int wi = whiteIndex (i);
        if (wi >= 0)
            return { rect_.x + wi * rect_.w / 7, rect_.bottom() - rowH, (wi + 1) * rect_.w / 7 - wi * rect_.w / 7, rowH };
        const int left = whiteIndex (i - 1);   // the white note below it
        const int cx = rect_.x + (left + 1) * rect_.w / 7, bw = ww;
        return { cx - bw / 2, rect_.y, bw, rowH };
    }
    int touchTargets (Rect* out, int max) const override
    {
        int n = 0;
        for (int i = 0; i < kNotes && n < max; ++i)
            out[n++] = cellRect (i);
        return n;
    }
    int cellAt (int x, int y) const noexcept
    {
        for (int i = 0; i < kNotes; ++i)
            if (cellRect (i).contains (x, y))
                return i;
        return -1;
    }
    double wheelUnit() const noexcept override { return 1.0; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        for (int i = 0; i < kNotes; ++i)
        {
            const Rect c = cellRect (i).inset (2);
            const bool lit = ((state.mask >> i) & 1) != 0, black = isBlack (i);
            // a white key is a light cell, a black key a darker one; the lit dot says the note is in the set
            p.rrect (c, T.rControl, p.c (black ? T.panel0 : T.panel1, 255));
            p.rrect (c, T.rControl, p.c (lit ? (qFrame_ ? T.q : T.flow[0]) : T.keyline, 255), lit ? 1.6f : T.frame);
            const float cx = static_cast<float> (c.x) + static_cast<float> (c.w) * 0.5f;
            led::dot (p, cx, static_cast<float> (c.y) + static_cast<float> (c.h) * 0.36f, lit, 255, qFrame_);
            p.textCentered ({ c.x, c.y + c.h * 58 / 100, c.w, c.h * 38 / 100 }, noteName (i), led::nameSize(), lit ? p.c (T.text, 255) : p.c (T.textDim, black ? 170 : 230), T.fMin);
        }
    }
    Change onDown (int x, int y, double now) override { tap_.down (x, y, now); downCell_ = cellAt (x, y); return Change::none(); }
    Change onMove (int x, int y) override { tap_.move (x, y); return Change::none(); }
    Change onUp (int, int) override
    {
        if (tap_.up() == 0 || downCell_ < 0)
            return Change::none();
        const int m = state.mask ^ (1 << downCell_);
        return Change::indexed (id_, m, m);
    }
    Change onTurn (double delta) override
    {
        const long d = std::lround (delta);
        if (d == 0)
            return Change::none();
        const int m = std::clamp (state.mask + static_cast<int> (std::clamp<long> (d, -4096, 4096)), 0, (1 << kNotes) - 1);
        return m == state.mask ? Change::none() : Change::indexed (id_, m, m);
    }

private:
    TapGesture tap_;
    int downCell_ = -1;
};

}} // namespace synthyboy_mpc::ui
