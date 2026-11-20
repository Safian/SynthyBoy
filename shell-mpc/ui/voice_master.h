// SynthyBoy - MPC UI component library: Master view and displays (docs/MPC.md, ui-elem-mastervoice.md).
// Full screen layout with Output, Pitch, Voices, Glide, Tuning/Scale, MPE blocks, Q-Link groups, and displays.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "base.h"
#include "clip_label.h"
#include "composite.h"
#include "core.h"
#include "led_list.h"
#include "led_toggle.h"
#include "qgroup.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================
/** Mini display of the portamento glide slope curve (convex / linear / concave according to master.portamento.slope -8..+8). */
class PortamentoCurveDisplay : public Component
{
public:
    float slope = 0.0f;

    PortamentoCurveDisplay() = default;
    Size minSize() const override { return { 100, 60 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        p.rrect (rect_, T.rControl, p.c (T.panel1, 200));
        p.rrect (rect_, T.rControl, p.c (T.keyline, 160), 1.0f);

        const int m = 6;
        const float x0 = static_cast<float> (rect_.x + m);
        const float y0 = static_cast<float> (rect_.bottom() - m);
        const float w = static_cast<float> (rect_.w - 2 * m);
        const float h = static_cast<float> (rect_.h - 2 * m);

        // Grid lines
        p.line (x0, y0 - h * 0.5f, x0 + w, y0 - h * 0.5f, 0.8f, p.c (T.track, 100));
        p.line (x0 + w * 0.5f, y0, x0 + w * 0.5f, y0 - h, 0.8f, p.c (T.track, 100));

        // Draw curve from (x0, y0) to (x0 + w, y0 - h)
        constexpr int kPoints = 32;
        float prevX = x0, prevY = y0;
        const float k = std::clamp (slope, -8.0f, 8.0f);
        const bool isLinear = std::fabs (k) < 0.05f;
        const float denom = isLinear ? 1.0f : (std::exp (k) - 1.0f);

        for (int i = 1; i <= kPoints; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (kPoints);
            const float yNorm = isLinear ? t : (std::exp (k * t) - 1.0f) / denom;
            const float px = x0 + t * w;
            const float py = y0 - yNorm * h;
            p.line (prevX, prevY, px, py, 1.8f, p.c (T.flow[0], 255));
            prevX = px; prevY = py;
        }

        // Label
        Rect lbl { rect_.x + 4, rect_.y + 2, rect_.w - 8, 18 };
        p.textCentered (lbl, "GLIDE CURVE", T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
    }
};

// ===============================================================================================================================
/** Mini 12-key keyboard diagram highlighting in-scale notes for the selected scale and key. It stores nothing of the scale: it draws `state` (the owner sets it on every update) and a touch on a key reports the key (`Index`,
    `index` = the note 0 .. 11); the owner writes the parameter and the next update shows it. A key that is already the root, or was already reported in this gesture, is not reported again. */
class MiniKeyboardDisplay : public Component
{
public:
    struct State
    {
        int scaleType = 0;   // 0 Chromatic .. 19
        int scaleKey = 0;    // 0 C .. 11 B
        int scaleMode = 0;   // 0 Conform, 1 Mark only
    };
    State state;

    MiniKeyboardDisplay() = default;
    Size minSize() const override { return { 240, 90 }; }

    static bool isScaleNote (int type, int key, int note) noexcept
    {
        if (type <= 0) return true; // Chromatic (all notes)
        struct ScaleDef { int count; int steps[8]; };
        static constexpr ScaleDef kDefs[20] = {
            { 12, { 0 } },
            { 7, { 0, 2, 4, 5, 7, 9, 11 } },        // Major
            { 7, { 0, 2, 3, 5, 7, 8, 10 } },        // Natural Minor
            { 7, { 0, 2, 3, 5, 7, 8, 11 } },        // Harmonic Minor
            { 7, { 0, 2, 3, 5, 7, 9, 11 } },        // Melodic Minor
            { 7, { 0, 2, 3, 5, 7, 9, 10 } },        // Dorian
            { 7, { 0, 1, 3, 5, 7, 8, 10 } },        // Phrygian
            { 7, { 0, 2, 4, 6, 7, 9, 11 } },        // Lydian
            { 7, { 0, 2, 4, 5, 7, 9, 10 } },        // Mixolydian
            { 7, { 0, 1, 3, 5, 6, 8, 10 } },        // Locrian
            { 5, { 0, 2, 4, 7, 9 } },               // Major Pentatonic
            { 5, { 0, 3, 5, 7, 10 } },              // Minor Pentatonic
            { 6, { 0, 3, 5, 6, 7, 10 } },           // Blues
            { 6, { 0, 2, 4, 6, 8, 10 } },           // Whole Tone
            { 8, { 0, 1, 3, 4, 6, 7, 9, 10 } },     // Diminished (H-W)
            { 8, { 0, 2, 3, 5, 6, 8, 9, 11 } },     // Diminished (W-H)
            { 7, { 0, 1, 4, 5, 7, 8, 10 } },        // Phrygian Dominant
            { 7, { 0, 2, 3, 6, 7, 8, 11 } },        // Hungarian Minor
            { 7, { 0, 1, 4, 5, 7, 8, 11 } },        // Double Harmonic
            { 5, { 0, 2, 3, 7, 8 } },               // Hirajoshi
        };
        if (type >= 20) return true;
        const int pc = (note - key + 12) % 12;
        for (int i = 0; i < kDefs[type].count; ++i)
            if (kDefs[type].steps[i] == pc)
                return true;
        return false;
    }

    struct KeyRects
    {
        Rect white[7];
        int whiteNote[7];
        Rect black[5];
        int blackNote[5];
    };

    KeyRects computeRects() const noexcept
    {
        KeyRects kr;
        const int m = 8, labelH = 22;
        const int kbX = rect_.x + m, kbY = rect_.y + m + labelH, kbW = rect_.w - 2 * m, kbH = std::max (24, rect_.h - 2 * m - labelH);
        const float wkW = static_cast<float> (kbW) / 7.0f;
        static const int kWhites[7] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int i = 0; i < 7; ++i)
        {
            kr.whiteNote[i] = kWhites[i];
            const int x0 = kbX + static_cast<int> (std::round (i * wkW));
            const int x1 = kbX + static_cast<int> (std::round ((i + 1) * wkW));
            kr.white[i] = { x0, kbY, x1 - x0, kbH };
        }
        static const struct { int note; float pos; } kBlacks[5] = {
            { 1, 0.877f }, { 3, 2.040f }, { 6, 3.790f }, { 8, 4.950f }, { 10, 6.120f }   // the centres of a piano's black keys in white key widths from the left of the C (23.5 mm whites, 13.7 mm blacks)
        };
        const float bkW = std::round (wkW * 0.40f);   // narrow: a black key is under 0.6 of a white one on a piano, and these white keys are wide and short, so it is narrower still to read as a key
        const int bkH = static_cast<int> (std::round (kbH * 0.62f));
        for (int i = 0; i < 5; ++i)
        {
            kr.blackNote[i] = kBlacks[i].note;
            const int cx = kbX + static_cast<int> (std::round (kBlacks[i].pos * wkW));
            const int x0 = cx - static_cast<int> (bkW * 0.5f);
            kr.black[i] = { x0, kbY, static_cast<int> (bkW), bkH };
        }
        return kr;
    }

    int hitTestNote (int x, int y) const noexcept
    {
        const KeyRects kr = computeRects();
        for (int i = 0; i < 5; ++i)
            if (kr.black[i].contains (x, y))
                return kr.blackNote[i];
        for (int i = 0; i < 7; ++i)
            if (kr.white[i].contains (x, y))
                return kr.whiteNote[i];
        return -1;
    }

    Change onDown (int x, int y, double now) override
    {
        tap_.down (x, y, now);
        downNote_ = hitTestNote (x, y);
        reported_ = -1;
        return report (downNote_);
    }
    Change onMove (int x, int y) override
    {
        tap_.move (x, y);
        const int note = hitTestNote (x, y);
        if (note >= 0 && note != downNote_)
        {
            downNote_ = note;
            return report (note);
        }
        return Change::none();
    }
    Change onUp (int x, int y) override
    {
        const int note = hitTestNote (x, y);
        if (tap_.up() != 0 && note >= 0)
            return report (note);
        return Change::none();
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.rrect (rect_, T.rControl, p.c (T.panel1, 200));
        p.rrect (rect_, T.rControl, p.c (T.keyline, 160), 1.0f);

        const int m = 8, labelH = 22;
        char lblBuf[48];
        static const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        std::snprintf (lblBuf, sizeof lblBuf, state.scaleType <= 0 ? "ALL 12 NOTES" : "SCALE ON %s", kKeyNames[std::clamp (state.scaleKey, 0, 11)]);
        p.textLeft ({ rect_.x + m, rect_.y + 4, rect_.w - 2 * m, labelH }, lblBuf, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);

        const KeyRects kr = computeRects();
        static const char* const kName[7] = { "C", "D", "E", "F", "G", "A", "B" };

        // 7 white keys (dark surface from palette with thin outlines)
        for (int i = 0; i < 7; ++i)
        {
            const int note = kr.whiteNote[i];
            const bool in = isScaleNote (state.scaleType, state.scaleKey, note);
            const bool isRoot = (note == state.scaleKey);
            const Rect wr = kr.white[i];
            p.rect (wr.x + 1, wr.y, wr.right() - 1, wr.bottom(), p.c (in ? T.panel0 : T.bg1, 255));
            if (in)
                p.rect (wr.x + 1, wr.y, wr.right() - 1, wr.bottom(), p.c (T.flow[0], 28));
            p.rrect ({ wr.x + 1, wr.y, wr.w - 2, wr.h }, 3.0f, p.c (in ? (isRoot ? T.flow[0] : T.keylineHi) : T.keyline, 255), isRoot ? 1.5f : 1.0f);
            if (isRoot)
                p.rect (wr.x + 8, wr.bottom() - 6, wr.right() - 8, wr.bottom() - 2, p.c (T.flow[0], 255));
            p.textCentered ({ wr.x, wr.bottom() - 30, wr.w, 22 }, kName[i], T.fLabel, p.c (in ? (isRoot ? T.flow[0] : T.text) : T.textDim, 255), T.fMin);
        }

        // 5 black keys (darker surface from palette, narrower and shorter, drawn above white keys)
        for (int i = 0; i < 5; ++i)
        {
            const int note = kr.blackNote[i];
            const bool in = isScaleNote (state.scaleType, state.scaleKey, note);
            const bool isRoot = (note == state.scaleKey);
            const Rect br = kr.black[i];
            p.rect (br, p.c (T.bg0, 255));   // dark, thin outline: the notes of the scale are the outline (accent for the root) and a small bar, not a coloured block
            p.rrect (br, 3.0f, p.c (in ? (isRoot ? T.flow[0] : T.keylineHi) : T.keyline, 255), isRoot ? 1.5f : 1.0f);
            if (in)
                p.rect (br.x + 5, br.bottom() - 7, br.right() - 5, br.bottom() - 4, p.c (isRoot ? T.flow[0] : T.keylineHi, isRoot ? 255 : 200));
        }
    }

private:
    TapGesture tap_;
    int downNote_ = -1;
    int reported_ = -1;   // the note of the last report of this gesture (gesture data, not the value: the root is `state.scaleKey`)

    /** The change for a touch on `note`: none for no key, for the root that is shown already, and for the key this gesture reported last. */
    Change report (int note) noexcept
    {
        if (note < 0 || note == state.scaleKey || note == reported_)
            return Change::none();
        reported_ = note;
        return Change::indexed (id_, note, static_cast<double> (note));
    }
};

// ===============================================================================================================================
/** The peak meters of the output (what leaves the engine, 0 = -60 dB .. 1 = full scale) in the ramp of the library (cyan to magenta, red when clipping), a dB scale in a column of its own at the left (0, -6, -12, -24, -48) and a peak hold
    line that the owner lets fall slowly. The owner gives each bar its level (the engine measures the larger of the two channels: both bars show it). Without a signal: a faint base line and the scale. */
class PeakMeterDisplay : public Component
{
public:
    float peakL = 0.0f; // 0 .. 1
    float peakR = 0.0f;
    float holdL = 0.0f, holdR = 0.0f;   // the peak hold lines (0 .. 1)

    PeakMeterDisplay() = default;
    Size minSize() const override { return { 76, 120 }; }

    /** The owner calls it once per picture with the time since the last one: the hold lines rise with the peaks and fall by 0.5 (30 dB) a second. */
    void updateHold (float dtSeconds) noexcept
    {
        const float fall = 0.5f * std::clamp (dtSeconds, 0.0f, 0.2f);
        holdL = std::max (peakL, holdL - fall);
        holdR = std::max (peakR, holdR - fall);
    }

    static constexpr int kNumMarks = 5, kLabelH = 14;
    /** The boxes of the numbers of the scale (dB 0, -6, -12, -24, -48) and whether each is written: a number whose box would run into the one above it (a short meter: 0 and -6 stand 15 px apart, the box of 0 is held down
        at the top edge) is left out, its mark stays. */
    void scaleLabels (Rect (&box)[kNumMarks], bool (&shown)[kNumMarks]) const noexcept
    {
        const int m = 4, scaleW = 34;
        const int barH = rect_.h - 2 * m - 22, barY = rect_.y + m, bottom = barY + barH;
        int lastBottom = -1000;
        for (int i = 0; i < kNumMarks; ++i)
        {
            const int y = bottom - static_cast<int> (std::lround (kMarks[i].pos * static_cast<float> (barH)));
            box[i] = { rect_.x + m, std::min (std::max (y - 7, barY), bottom - kLabelH), scaleW - 7, kLabelH };
            shown[i] = box[i].y >= lastBottom;
            if (shown[i])
                lastBottom = box[i].bottom();
        }
    }

protected:
    static constexpr struct Mark { float pos; const char* name; } kMarks[kNumMarks] = { { 1.0f, "0" }, { 0.9f, "-6" }, { 0.8f, "-12" }, { 0.6f, "-24" }, { 0.2f, "-48" } };
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        p.rrect (rect_, T.rControl, p.c (T.panel1, 200));
        p.rrect (rect_, T.rControl, p.c (T.keyline, 160), 1.0f);

        // the scale has a column of its own at the left (the numbers right aligned, the ticks touching the bar edge); the two bars stand side by side to the right of it
        const int m = 4, scaleW = 34, barGap = 6;
        const int barW = std::max (6, (rect_.w - 2 * m - scaleW - barGap) / 2);
        const int barH = rect_.h - 2 * m - 22;
        const int barY = rect_.y + m;
        const Rect lr { rect_.x + m + scaleW, barY, barW, barH };
        const Rect rr { lr.right() + barGap, barY, barW, barH };
        drawBar (p, lr, peakL, holdL);
        drawBar (p, rr, peakR, holdR);

        // the scale: a mark for 0, -6, -12, -24, -48 dB (0 = -60 dB .. 1 = full scale) and, where the numbers do not run into each other, the number
        Rect box[kNumMarks];
        bool shown[kNumMarks];
        scaleLabels (box, shown);
        for (int i = 0; i < kNumMarks; ++i)
        {
            const int y = lr.bottom() - static_cast<int> (std::lround (kMarks[i].pos * static_cast<float> (lr.h)));
            p.line (static_cast<float> (lr.x - 4), static_cast<float> (y), static_cast<float> (lr.x), static_cast<float> (y), 1.0f, p.c (T.textDim, 200));
            if (shown[i])
                p.textRight (box[i], kMarks[i].name, T.fSmall, p.c (T.textFaint, 255), T.fMin);
        }

        // the names of the bars under them
        p.textCentered ({ lr.x - 2, rect_.bottom() - 22, barW + 4, 18 }, "L", T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
        p.textCentered ({ rr.x - 2, rect_.bottom() - 22, barW + 4, 18 }, "R", T.fSmall + 1.0f, p.c (T.textDim, 255), T.fMin);
    }

private:
    void drawBar (Pen& p, const Rect& r, float level, float hold) const noexcept
    {
        const theme::Look& T = theme::L();
        p.rect (r, p.c (T.track, 100));

        const float norm = std::clamp (level, 0.0f, 1.2f);
        if (norm > 0.01f)
        {
            const int fillH = static_cast<int> (std::lround (std::min (1.0f, norm) * r.h));
            const int fillY = r.bottom() - fillH;
            // the ramp of the library (cyan at the bottom to magenta at the top, as the mixer's meters); red only when it clips (the yellow is the Q colour and never means "hot")
            const uint32_t topCol = norm > 1.0f ? T.negative : T.flow[3];
            const uint32_t ramp[4] = { topCol, T.flow[2], T.flow[1], T.flow[0] };
            p.paintY (static_cast<float> (r.y), static_cast<float> (r.bottom()), ramp, 240);
            p.line (static_cast<float> (r.x) + static_cast<float> (r.w) * 0.5f, static_cast<float> (fillY), static_cast<float> (r.x) + static_cast<float> (r.w) * 0.5f, static_cast<float> (r.bottom()), static_cast<float> (r.w - 2), p.c (topCol, 240));   // a thick line takes the paint (a rect does not, in either renderer)
            p.plain();

            // Crisp top peak line
            p.line (static_cast<float> (r.x), static_cast<float> (fillY),
                    static_cast<float> (r.right()), static_cast<float> (fillY),
                    1.5f, p.c (T.text, 255));
        }
        else
            p.rect (r.x + 1, r.bottom() - 3, r.right() - 1, r.bottom() - 1, p.c (T.textFaint, 150));   // at rest: the faint base line

        // the peak hold: a line that stays where the highest peak was and falls slowly
        if (hold > 0.02f)
        {
            const float hy = static_cast<float> (r.bottom()) - std::min (1.0f, hold) * static_cast<float> (r.h);
            p.line (static_cast<float> (r.x), hy, static_cast<float> (r.right()), hy, 2.0f, p.c (hold > 1.0f ? T.negative : T.flow[0], 255));
        }
    }
};

// ===============================================================================================================================
/** Horizontal live voice count bar: sounding voices / polyphony. */
class VoiceMeterDisplay : public Component
{
public:
    int activeVoices = 0;
    int polyphony = 16;

    VoiceMeterDisplay() = default;
    Size minSize() const override { return { 120, 24 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        p.rrect (rect_, T.rChip, p.c (T.panel1, 200));
        p.rrect (rect_, T.rChip, p.c (T.keyline, 160), 1.0f);

        const int totalCells = std::clamp (polyphony, 1, 32);
        const int m = 6;
        const int gap = 3;
        const int textW = 140;
        const int cellsW = std::max (10, rect_.w - 2 * m - textW - 8);
        const int cellW = std::max (4, (cellsW - (totalCells - 1) * gap) / totalCells);
        const int cellH = std::max (12, rect_.h - 2 * m);
        const int cellY = rect_.y + (rect_.h - cellH) / 2;

        for (int i = 0; i < totalCells; ++i)
        {
            const int cx = rect_.x + m + i * (cellW + gap);
            const Rect cr { cx, cellY, cellW, cellH };
            if (i < activeVoices)
            {
                p.rrect (cr, 2.0f, p.c (T.flow[0], 255));
            }
            else
            {
                p.rrect (cr, 2.0f, p.c (T.track, 120));
                p.rrect (cr, 2.0f, p.c (T.keyline, 80), 1.0f);
            }
        }

        char buf[32];
        std::snprintf (buf, sizeof buf, "%d / %d VOICES", activeVoices, polyphony);
        p.textCentered ({ rect_.right() - textW - 4, rect_.y, textW, rect_.h }, buf, T.fLabel, p.c (T.text, 255), T.fMin);
    }
};

// ===============================================================================================================================
/** MasterScreen: the page of the Master tab (1280 x 628): a header (MASTER, and the two sides MAIN and TUNING & MPE) and the blocks of the side that is on, each ONE Q group (the last two blocks of the second
    side two):  side MAIN: OUTPUT, PITCH (top row), VOICES, GLIDE (bottom row);  side TUNING & MPE: SCALE (with the keyboard), TUNING, MPE (a group of three in each of its two rows).
    The switches and the short lists are LED controls (led_toggle.h, led_list.h: a dot and a name, no box, every option of a list visible); the numbers, the knobs and the long lists (SCALE, KEY) keep their cells.
    The room the LED controls free goes to the knobs, the glide curve and the keyboard. */
class MasterScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 10, kBlocks = 7, kHeaderH = 64, kBlockTitleH = 38, kBlockPad = 6, kBlockGap = 8;   // the title strip: the name of the block, then the lane of the index chips with a gap to the name
    static constexpr int kTopBlockH = 228, kTopLeftW = 640, kBottomLeftW = 780;   // the first row of MAIN: OUTPUT (the knobs) and PITCH; the second: VOICES (the widest) and GLIDE
    enum Side : int { SideMain = 0, SideTuning = 1 };

    MenuButton sideMain { "MAIN", MenuButton::Icon::None };
    MenuButton sideTuning { "TUNING & MPE", MenuButton::Icon::None };

    // OUTPUT
    Knob volume { "VOLUME" };
    ClipLabel clip;
    ClipHold& clipHold = clip.hold;
    PeakMeterDisplay meters;
    Knob spread { "SPREAD" };
    Knob stereoRouting { "STEREO" };   // master.stereo_routing is a continuous percentage: a knob, not a list
    LedList voiceOverride;
    LedToggle outputLimiter;   // the output limiter of the plugin (a shell setting, not an engine parameter: no Q group); in the strip under the knobs, outside the Q frame
    LedToggle noSmoothing, oversample4x;   // QUALITY: quality.no_smoothing and quality.oversample_4x (engine parameters, touch only: every Q slot of the page is taken), in the same strip
    // PITCH
    Knob tune { "TUNE" };
    NumberField transpose, bendUp, bendDown;
    LedToggle bendLink;
    // VOICES
    NumberField polyphony;
    LedToggle legato, polyLegato, phaseLegato;
    LedList sameNote, stealPriority;
    NumberField velocityTrack;
    VoiceMeterDisplay voiceMeter;
    LedToggle padPressure;   // the plugin-wide pad pressure layer (shell parameters, not engine ones: no Q group)
    NumberField padAmount;
    // GLIDE
    Knob portaTime { "GLIDE" }, portaSlope { "SLOPE" };
    LedToggle portaAlways, portaScaled;
    PortamentoCurveDisplay portaCurve;
    // SCALE
    Stepper scaleType, scaleKey;   // 20 scales and 12 keys: long lists, they keep the stepper with its list
    LedList scaleMode;
    MiniKeyboardDisplay keyboard;
    // TUNING
    NumberField tuningA4;
    LedList tuningScale;
    LedToggle mtsEsp, mtsNoteOn;
    // MPE
    LedToggle mpeOn;
    NumberField mpeBend;
    LedList mpeMacro;
    LedToggle mpeYModwheel, mpeBendToX, mpeYBipolar;

    QGroup groups[kMaxGroups];   // 0 OUTPUT, 1 PITCH, 2 LINK, 3 VOICES, 4 VOICE OPTS, 5 GLIDE, 6 SCALE, 7 TUNING, 8 MPE, 9 MPE Y

    MasterScreen()
    {
        volume.state.large = true; spread.state.large = true; stereoRouting.state.large = true;   // the three knobs of OUTPUT are one size; every other knob has the common size
        tune.state.bipolar = true;
        tune.state.side = true;   // the medium variant: the dial at the left, the name and the value beside it
        portaSlope.state.bipolar = true;

        transpose.state.format = NumberField::Format::IntSigned;
        transpose.setLabel ("TRANSPOSE");
        bendUp.state.format = NumberField::Format::IntPlain;
        bendUp.setLabel ("BEND UP");
        bendDown.state.format = NumberField::Format::IntPlain;
        bendDown.setLabel ("BEND DOWN");
        bendLink.setLabel ("LINK");
        polyphony.state.format = NumberField::Format::IntPlain;
        polyphony.setLabel ("POLY");
        legato.setLabel ("LEGATO");
        polyLegato.setLabel ("POLY LEG");
        phaseLegato.setLabel ("PHASE LEG");
        sameNote.setTitle ("SAME NOTE"); sameNote.state.titleInLane = true;
        stealPriority.setTitle ("STEAL"); stealPriority.state.titleInLane = true;
        velocityTrack.state.format = NumberField::Format::IntSigned;
        velocityTrack.setLabel ("VEL TRACK");
        outputLimiter.setLabel ("LIMIT");
        noSmoothing.setLabel ("NO SMOOTHING");
        oversample4x.setLabel ("OVERSAMPLE 4X");
        padPressure.setLabel ("PAD PRESS");
        padAmount.state.format = NumberField::Format::IntPlain;
        padAmount.setLabel ("PAD %");
        voiceOverride.setTitle ("VOICE OVERRIDE"); voiceOverride.state.titleInLane = true;
        portaAlways.setLabel ("ALWAYS");
        portaScaled.setLabel ("SCALED");
        scaleType.setCaption ("SCALE");
        scaleKey.setCaption ("KEY");
        scaleMode.setTitle ("MODE"); scaleMode.state.titleInLane = true;
        tuningA4.state.format = NumberField::Format::Decimal2;
        tuningA4.setLabel ("A4 HZ");
        tuningScale.setTitle ("TUNING"); tuningScale.state.titleInLane = true;
        mtsEsp.setLabel ("MTS-ESP");
        mtsNoteOn.setLabel ("NOTE-ON");
        mpeOn.setLabel ("MPE");
        mpeBend.state.format = NumberField::Format::IntPlain;
        mpeBend.setLabel ("MPE BEND");
        mpeMacro.setTitle ("XYZ TO MACROS"); mpeMacro.state.titleInLane = true;
        mpeYModwheel.setLabel ("Y TO WHEEL");
        mpeBendToX.setLabel ("BEND TO X");
        mpeYBipolar.setLabel ("Y BIPOLAR");

        addKid (sideMain); addKid (sideTuning);
        for (Component* c : sideMainComponents())
            addKid (*c);
        for (Component* c : sideTuningComponents())
            addKid (*c, false);
        buildGroups();
        setSide (SideMain);
    }

    Size minSize() const override { return { 1280, 628 }; }

    int side() const noexcept { return side_; }
    void setSide (int s)
    {
        side_ = s == SideTuning ? SideTuning : SideMain;
        for (Component* c : sideMainComponents())
            show (*c, side_ == SideMain);
        for (Component* c : sideTuningComponents())
            show (*c, side_ == SideTuning);
        if (activeGroup_ >= 0 && sideOfGroup (activeGroup_) != side_)
            setActiveGroup (side_ == SideMain ? 0 : 6);
        else
            setActiveGroup (activeGroup_);
    }
    static int sideOfGroup (int g) noexcept { return g < 6 ? SideMain : SideTuning; }

    int numGroups() const noexcept { return kMaxGroups; }
    int activeGroup() const noexcept { return activeGroup_; }
    void setActiveGroup (int g) noexcept
    {
        activeGroup_ = std::clamp (g, 0, kMaxGroups - 1);
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setActive (i == activeGroup_);
    }
    static const char* groupName (int g) noexcept
    {
        static const char* const kNames[kMaxGroups] = { "OUTPUT", "PITCH", "LINK", "VOICES", "VOICE OPTS", "GLIDE", "SCALE", "TUNING", "MPE", "MPE Y" };
        return g >= 0 && g < kMaxGroups ? kNames[g] : "";
    }
    static const char* blockName (int b) noexcept
    {
        static const char* const kNames[kBlocks] = { "OUTPUT", "PITCH", "VOICES", "GLIDE", "SCALE", "TUNING", "MPE" };
        return b >= 0 && b < kBlocks ? kNames[b] : "";
    }

    Change turnQ (int slot, double delta) { return groups[activeGroup_].turn (slot, delta); }

    Change touchDown (int x, int y, double nowMs) override
    {
        Change c = Composite::touchDown (x, y, nowMs);
        if (cap_ && cap_ != &sideMain && cap_ != &sideTuning)
        {
            int hit = -1;
            for (int i = 0; i < kMaxGroups && hit < 0; ++i)
                if (sideOfGroup (i) == side_)
                    for (int k = 0; k < QGroup::kSlots; ++k)
                        if (groups[i].member (k) == cap_)
                            hit = i;
            if (hit < 0)   // not a member: the block it is in
            {
                if (side_ == SideMain)
                {
                    if (blockRect (0).contains (x, y)) hit = 0;
                    else if (blockRect (1).contains (x, y)) hit = (y < bendLink.rect().y ? 1 : 2);
                    else if (blockRect (2).contains (x, y)) hit = (y < polyphony.rect().bottom() + kQLane ? 3 : 4);
                    else if (blockRect (3).contains (x, y)) hit = 5;
                }
                else
                {
                    if (blockRect (4).contains (x, y)) hit = 6;
                    else if (blockRect (5).contains (x, y)) hit = 7;
                    else if (blockRect (6).contains (x, y)) hit = (y < mpeYModwheel.rect().y ? 8 : 9);
                }
            }
            if (hit >= 0)
                setActiveGroup (hit);
        }
        return c;
    }
    Change touchUp (int x, int y) override
    {
        const bool onMain = cap_ == &sideMain, onTuning = cap_ == &sideTuning;
        const Change c = Composite::touchUp (x, y);
        if (onMain) { setSide (SideMain); return Change::none(); }
        if (onTuning) { setSide (SideTuning); return Change::none(); }
        return c;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        if (activeGroup_ >= 0 && activeGroup_ < kMaxGroups)
            groups[activeGroup_].draw (sb);
        for (int i = 0; i < kMaxGroups; ++i)
            if (i != activeGroup_ && sideOfGroup (i) == side_)
                groups[i].drawIdle (sb);
    }

    bool blockVisible (int b) const noexcept { return side_ == SideMain ? b < 4 : b >= 4; }
    /** The rectangle of block b (0 .. 6) on its side. */
    Rect blockRect (int b) const noexcept
    {
        const int m = kBlockGap, y0 = rect_.y + kHeaderH + 8, h = rect_.bottom() - 8 - y0, w = rect_.w - 2 * m;
        if (b < 4)
        {
            const int topH = kTopBlockH, botH = h - m - topH, lw = b < 2 ? kTopLeftW : kBottomLeftW;
            const int y = b < 2 ? y0 : y0 + topH + m, bh = b < 2 ? topH : botH;
            return b % 2 == 0 ? Rect { rect_.x + m, y, lw, bh } : Rect { rect_.x + m + lw + m, y, w - lw - m, bh };
        }
        const int topH = (h - m) * 52 / 100, botH = h - m - topH;
        if (b == 4)
            return { rect_.x + m, y0, w, topH };
        const int bw = (w - m) / 2;
        return b == 5 ? Rect { rect_.x + m, y0 + topH + m, bw, botH } : Rect { rect_.x + m + bw + m, y0 + topH + m, rect_.right() - m - (rect_.x + m + bw + m), botH };
    }
    Rect inner (int b) const noexcept
    {
        const Rect r = blockRect (b);
        return { r.x + kBlockPad, r.y + kBlockTitleH, std::max (0, r.w - 2 * kBlockPad), std::max (0, r.h - kBlockTitleH - kBlockPad) };
    }

protected:
    std::vector<Component*> sideMainComponents()
    {
        return { &volume, &clip, &meters, &spread, &stereoRouting, &voiceOverride, &outputLimiter, &noSmoothing, &oversample4x, &tune, &transpose, &bendUp, &bendDown, &bendLink, &polyphony, &legato, &polyLegato, &phaseLegato, &sameNote, &stealPriority, &velocityTrack, &voiceMeter, &padPressure, &padAmount,
                 &portaTime, &portaSlope, &portaAlways, &portaScaled, &portaCurve };
    }
    std::vector<Component*> sideTuningComponents()
    {
        return { &scaleType, &scaleKey, &scaleMode, &keyboard, &tuningA4, &tuningScale, &mtsEsp, &mtsNoteOn, &mpeOn, &mpeBend, &mpeMacro, &mpeYModwheel, &mpeBendToX, &mpeYBipolar };
    }

    void layout() override
    {
        const int g = kGap, lane = kQLane, th = led::kRowH;
        sideMain.place ({ rect_.x + 300, rect_.y + 4, 160, 56 });
        sideTuning.place ({ rect_.x + 468, rect_.y + 4, 220, 56 });

        // ---- side MAIN
        {   // OUTPUT: the meters (the scale in a column of its own) at the left; at the right the Q group of four on one line (the three knobs VOLUME, SPREAD, STEREO, one size, and the voice override as a LED list) over a strip of three LED switches
            // that are outside the Q frame (touch only): LIMIT (the plugin's output limiter) and the QUALITY pair NO SMOOTHING, OVERSAMPLE 4X (quality.* of the engine; the caption QUALITY stands in front of the pair)
            const Rect in = inner (0);
            const int mw = 100, stripH = th, upperH = std::max (th, in.h - stripH - 4);
            const int clipH = 56;
            clip.place ({ in.x, in.y, mw, clipH });
            meters.place ({ in.x, in.y + clipH, mw, in.h - clipH });
            const int lw = 150, kx = in.x + mw + 8, kw = (in.right() - lw - g - kx - 2 * g) / 3;
            volume.place ({ kx, in.y, kw, upperH });
            spread.place ({ kx + kw + g, in.y, kw, upperH });
            stereoRouting.place ({ kx + 2 * (kw + g), in.y, kw, upperH });
            voiceOverride.place ({ in.right() - lw, in.y, lw, std::min (upperH, 2 * 64) });
            const int sy = in.bottom() - stripH;
            const int w0 = std::max (LedToggle::widthFor (outputLimiter.state.label), 96), w1 = LedToggle::widthFor (noSmoothing.state.label), w2 = LedToggle::widthFor (oversample4x.state.label);
            outputLimiter.place ({ kx, sy, w0, stripH });
            qualityCaption_ = { kx + w0 + 3 * g, sy, 70, stripH };
            noSmoothing.place ({ qualityCaption_.right(), sy, w1, stripH });
            oversample4x.place ({ qualityCaption_.right() + w1 + g, sy, std::min (w2, std::max (0, in.right() - (qualityCaption_.right() + w1 + g))), stripH });
        }
        {   // PITCH: one row of four cells at one height (TUNE as a medium knob with its text beside it, TRANSPOSE, BEND UP, BEND DOWN: PITCH 1 Q group), LINK as a LED toggle under the two bend fields (PITCH 2 Q group)
            const Rect in = inner (1);
            const int tw = 190, cw = (in.w - tw - 3 * g) / 3, rowH = std::max (th, in.h - lane - th);
            tune.place ({ in.x, in.y, tw, rowH });
            transpose.place ({ in.x + tw + g, in.y, cw, rowH });
            bendUp.place ({ in.x + tw + g + cw + g, in.y, cw, rowH });
            bendDown.place ({ in.x + tw + g + 2 * (cw + g), in.y, in.right() - (in.x + tw + g + 2 * (cw + g)), rowH });
            bendLink.place ({ bendUp.rect().x, in.y + rowH + lane, LedToggle::widthFor (bendLink.state.label), th });
        }
        {   // VOICES: row 1 four cells (POLY, LEGATO, POLY LEG, PHASE LEG: VOICES 1 Q group); row 2 VEL TRACK, SAME NOTE, STEAL (VOICES 2): the two lists each as wide as their options need (rows of 56 px under the title in the chip lane);
            // row 3 the pad pressure layer (PAD PRESS, PAD %) and the voice meter across the rest
            const Rect in = inner (2);
            const int cw = (in.w - 3 * g) / 4, r2H = 2 * th;
            auto col = [&] (int c) { return in.x + c * (cw + g); };
            const int y1 = in.y, y2 = y1 + th + lane, y3 = y2 + r2H + lane;
            polyphony.place ({ col (0), y1, cw, th });
            legato.place ({ col (1), y1, cw, th });
            polyLegato.place ({ col (2), y1, cw, th });
            phaseLegato.place ({ col (3), y1, in.right() - col (3), th });
            const int stealW = 345, sameW = in.w - cw - stealW - 2 * g;   // STEAL: five options in three columns of two rows; SAME NOTE: three in two columns
            velocityTrack.place ({ in.x, y2, cw, r2H });
            sameNote.place ({ in.x + cw + g, y2, sameW, r2H });
            stealPriority.place ({ in.right() - stealW, y2, stealW, r2H });
            padPressure.place ({ col (0), y3, cw, th });
            padAmount.place ({ col (1), y3, cw, th });
            voiceMeter.place ({ col (2), y3 + (th - 44) / 2, in.right() - col (2), 44 });
        }
        {   // GLIDE: the two knobs over ALWAYS and SCALED (the four cells of the Q group in the left half), the curve display tall at the right
            const Rect in = inner (3);
            const int lw = (in.w - g) / 2 - 16, cw = (lw - g) / 2, rowH = std::max (th, in.h - lane - th);
            portaTime.place ({ in.x, in.y, cw, rowH });
            portaSlope.place ({ in.x + cw + g, in.y, lw - cw - g, rowH });
            portaAlways.place ({ in.x, in.y + rowH + lane, cw, th });
            portaScaled.place ({ in.x + cw + g, in.y + rowH + lane, lw - cw - g, th });
            portaCurve.place ({ in.x + lw + g, in.y, in.w - lw - g, in.h });
        }
        // ---- side TUNING & MPE
        {   // SCALE: the two long lists as steppers and MODE as a LED list in a row spanning the width, the big keyboard under them
            const Rect in = inner (4);
            const int rowH = 64, typeW = in.w * 36 / 100, keyW = in.w * 26 / 100;
            scaleType.place ({ in.x, in.y, typeW, rowH });
            scaleKey.place ({ in.x + typeW + g, in.y, keyW, rowH });
            scaleMode.place ({ in.x + typeW + keyW + 2 * g, in.y, in.right() - (in.x + typeW + keyW + 2 * g), rowH });
            const int ky = in.y + rowH + 10;
            keyboard.place ({ in.x, ky, in.w, std::max (90, in.bottom() - ky) });
        }
        {   // TUNING: A4 and the tuning list (four options, all visible) in the first row, the two MTS switches under them
            const Rect in = inner (5);
            const int rowH = std::max (2 * th, in.h - lane - th), a4W = 200;
            tuningA4.place ({ in.x, in.y, a4W, rowH });
            tuningScale.place ({ in.x + a4W + g, in.y, in.w - a4W - g, rowH });
            const int cw = (in.w - g) / 2;
            mtsEsp.place ({ in.x, in.y + rowH + lane, cw, th });
            mtsNoteOn.place ({ in.x + cw + g, in.y + rowH + lane, in.right() - (in.x + cw + g), th });
        }
        {   // MPE: MPE, MPE BEND, XYZ TO MACROS (three options side by side); Y TO WHEEL, BEND TO X, Y BIPOLAR under them
            const Rect in = inner (6);
            const int rowH = std::max (th, in.h - lane - th), cw = (in.w - 2 * g) / 3;   // the first row takes the room, the three switches stand at the bottom (no empty strip under them)
            mpeOn.place ({ in.x, in.y, cw, rowH });
            mpeBend.place ({ in.x + cw + g, in.y, cw, rowH });
            mpeMacro.place ({ in.x + 2 * (cw + g), in.y, in.right() - (in.x + 2 * (cw + g)), std::min (rowH, 2 * th - 8) });   // three options in ONE row (a rectangle under two rows high keeps them side by side)
            const int y2 = in.y + rowH + lane;
            mpeYModwheel.place ({ in.x, y2, cw, th });
            mpeBendToX.place ({ in.x + cw + g, y2, cw, th });
            mpeYBipolar.place ({ in.x + 2 * (cw + g), y2, in.right() - (in.x + 2 * (cw + g)), th });
        }
    }

    void drawUnder (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + kHeaderH - 2), static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.y + kHeaderH - 2), 1.5f, p.c (T.keyline, 255));
        for (int b = 0; b < kBlocks; ++b)
            if (blockVisible (b))
                p.block (blockRect (b), false);
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + 14, rect_.y + 4, 270, 56 }, "MASTER", T.fTitle, p.c (T.text, 255), T.fMin);
        p.rrect ((side_ == SideMain ? sideMain : sideTuning).rect(), T.rControl, p.c (T.flow[0], 255), 1.6f);
        for (int b = 0; b < kBlocks; ++b)
            if (blockVisible (b))
            {
                const Rect r = blockRect (b);
                p.textLeft ({ r.x + kBlockPad + 6, r.y + 3, std::max (0, r.w - 20), 16 }, blockName (b), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
            }
        if (side_ == SideMain && ! qualityCaption_.empty())
            p.textLeft (qualityCaption_, "QUALITY", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
    }

private:
    int side_ = SideMain, activeGroup_ = 0;
    Rect qualityCaption_;   // the small caption of the QUALITY pair in the OUTPUT block

    void buildGroups()
    {
        groups[0].set (0, &volume); groups[0].set (1, &spread); groups[0].set (2, &stereoRouting); groups[0].set (3, &voiceOverride);
        groups[1].set (0, &tune); groups[1].set (1, &transpose); groups[1].set (2, &bendUp); groups[1].set (3, &bendDown);
        groups[2].set (0, &bendLink);
        groups[3].set (0, &polyphony); groups[3].set (1, &legato); groups[3].set (2, &polyLegato); groups[3].set (3, &phaseLegato);
        groups[4].set (0, &velocityTrack); groups[4].set (1, &sameNote); groups[4].set (2, &stealPriority);
        groups[5].set (0, &portaTime); groups[5].set (1, &portaSlope); groups[5].set (2, &portaAlways); groups[5].set (3, &portaScaled);
        groups[6].set (0, &scaleType); groups[6].set (1, &scaleKey); groups[6].set (2, &scaleMode);
        groups[7].set (0, &tuningA4); groups[7].set (1, &tuningScale); groups[7].set (2, &mtsEsp); groups[7].set (3, &mtsNoteOn);
        groups[8].set (0, &mpeOn); groups[8].set (1, &mpeBend); groups[8].set (2, &mpeMacro);
        groups[9].set (0, &mpeYModwheel); groups[9].set (1, &mpeBendToX); groups[9].set (2, &mpeYBipolar);
        setActiveGroup (0);
    }
};

}} // namespace synthyboy_mpc::ui
