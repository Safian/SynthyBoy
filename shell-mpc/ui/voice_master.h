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
#include "composite.h"
#include "core.h"
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
/** Mini 12-key keyboard diagram highlighting in-scale notes for the selected scale and key. */
class MiniKeyboardDisplay : public Component
{
public:
    int scaleType = 0;   // 0 Chromatic .. 19
    int scaleKey = 0;    // 0 C .. 11 B
    int scaleMode = 0;   // 0 Conform, 1 Mark only

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
            { 1, 0.96f }, { 3, 2.04f }, { 6, 3.95f }, { 8, 5.00f }, { 10, 6.05f }
        };
        const float bkW = std::round (wkW * 0.58f);
        const int bkH = static_cast<int> (std::round (kbH * 0.60f));
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
        if (downNote_ >= 0)
        {
            scaleKey = downNote_;
            return Change::indexed (id_, downNote_, static_cast<double> (downNote_));
        }
        return Change::none();
    }
    Change onMove (int x, int y) override
    {
        tap_.move (x, y);
        const int note = hitTestNote (x, y);
        if (note >= 0 && note != downNote_)
        {
            downNote_ = note;
            scaleKey = note;
            return Change::indexed (id_, note, static_cast<double> (note));
        }
        return Change::none();
    }
    Change onUp (int x, int y) override
    {
        const int note = hitTestNote (x, y);
        if (tap_.up() != 0 && note >= 0)
        {
            scaleKey = note;
            return Change::indexed (id_, note, static_cast<double> (note));
        }
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
        std::snprintf (lblBuf, sizeof lblBuf, scaleType <= 0 ? "ALL 12 NOTES" : "SCALE ON %s", kKeyNames[std::clamp (scaleKey, 0, 11)]);
        p.textLeft ({ rect_.x + m, rect_.y + 4, rect_.w - 2 * m, labelH }, lblBuf, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);

        const KeyRects kr = computeRects();
        static const char* const kName[7] = { "C", "D", "E", "F", "G", "A", "B" };

        // 7 white keys (dark surface from palette with thin outlines)
        for (int i = 0; i < 7; ++i)
        {
            const int note = kr.whiteNote[i];
            const bool in = isScaleNote (scaleType, scaleKey, note);
            const bool isRoot = (note == scaleKey);
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
            const bool in = isScaleNote (scaleType, scaleKey, note);
            const bool isRoot = (note == scaleKey);
            const Rect br = kr.black[i];
            p.rect (br, p.c (T.bg0, 255));
            if (in)
                p.rect (br, p.c (T.flow[0], 40));
            p.rrect (br, 3.0f, p.c (in ? (isRoot ? T.flow[0] : T.keylineHi) : T.keyline, 255), isRoot ? 1.5f : 1.0f);
            if (isRoot)
                p.rect (br.x + 6, br.bottom() - 7, br.right() - 6, br.bottom() - 3, p.c (T.flow[0], 255));
        }
    }

private:
    TapGesture tap_;
    int downNote_ = -1;
};

// ===============================================================================================================================
/** The peak meters of the output (what leaves the engine, 0 = -60 dB .. 1 = full scale) with the colours of a level (green, yellow, red), a dB scale between the two bars (0, -6, -12, -24, -48) and a peak hold
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

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        p.rrect (rect_, T.rControl, p.c (T.panel1, 200));
        p.rrect (rect_, T.rControl, p.c (T.keyline, 160), 1.0f);

        const int m = 4, scaleW = 28;
        const int barW = std::max (6, (rect_.w - 2 * m - scaleW) / 2);
        const int barH = rect_.h - 2 * m - 22;
        const int barY = rect_.y + m;
        const Rect lr { rect_.x + m, barY, barW, barH };
        const Rect rr { rect_.x + m + barW + scaleW, barY, barW, barH };
        drawBar (p, lr, peakL, holdL);
        drawBar (p, rr, peakR, holdR);

        // the scale between the bars: a mark and a number for 0, -6, -12, -24, -48 dB (0 = -60 dB .. 1 = full scale)
        static const struct { float pos; const char* name; } kMarks[5] = { { 1.0f, "0" }, { 0.9f, "-6" }, { 0.8f, "-12" }, { 0.6f, "-24" }, { 0.2f, "-48" } };
        for (const auto& mk : kMarks)
        {
            const int y = lr.bottom() - static_cast<int> (std::lround (mk.pos * static_cast<float> (lr.h)));
            p.line (static_cast<float> (lr.right()), static_cast<float> (y), static_cast<float> (lr.right() + 3), static_cast<float> (y), 1.0f, p.c (T.textDim, 200));
            p.line (static_cast<float> (rr.x - 3), static_cast<float> (y), static_cast<float> (rr.x), static_cast<float> (y), 1.0f, p.c (T.textDim, 200));
            p.textCentered ({ lr.right() + 2, std::min (std::max (y - 7, barY), lr.bottom() - 14), scaleW - 4, 14 }, mk.name, T.fSmall, p.c (T.textFaint, 255), T.fMin);
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
            const uint32_t topCol = norm > 1.0f ? T.negative : (norm > 0.75f ? T.inst[0] : T.inst[4]);
            p.paintY2 (static_cast<float> (fillY), static_cast<float> (r.bottom()), topCol, T.inst[4], 255);
            p.rect (r.x + 1, fillY, r.right() - 1, r.bottom(), p.c (colour::rgba (topCol, 255)));
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
    side two):  side MAIN: OUTPUT, PITCH, VOICES, GLIDE (2 x 2 blocks);  side TUNING & MPE: SCALE (with the keyboard), TUNING, MPE (a group of three in each of its two rows).
    Every block uses its whole height (the knobs are big, with their values), every name is in capitals. */
class MasterScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 10, kBlocks = 7, kHeaderH = 64, kBlockTitleH = 34, kBlockPad = 6, kBlockGap = 8;
    enum Side : int { SideMain = 0, SideTuning = 1 };

    MenuButton sideMain { "MAIN", MenuButton::Icon::None };
    MenuButton sideTuning { "TUNING & MPE", MenuButton::Icon::None };

    // OUTPUT
    Knob volume { "VOLUME" };
    PeakMeterDisplay meters;
    Knob spread { "SPREAD" };
    Stepper stereoRouting, voiceOverride;
    // PITCH
    Knob tune { "TUNE" };
    NumberField transpose, bendUp, bendDown;
    ToggleIcon bendLink;
    // VOICES
    NumberField polyphony;
    ToggleIcon legato, polyLegato, phaseLegato;
    Stepper sameNote, stealPriority;
    NumberField velocityTrack;
    VoiceMeterDisplay voiceMeter;
    // GLIDE
    Knob portaTime { "GLIDE" }, portaSlope { "SLOPE" };
    ToggleIcon portaAlways, portaScaled;
    PortamentoCurveDisplay portaCurve;
    // SCALE
    Stepper scaleType, scaleKey, scaleMode;
    MiniKeyboardDisplay keyboard;
    // TUNING
    NumberField tuningA4;
    Stepper tuningScale;
    ToggleIcon mtsEsp, mtsNoteOn;
    // MPE
    ToggleIcon mpeOn;
    NumberField mpeBend;
    Stepper mpeMacro;
    ToggleIcon mpeYModwheel, mpeBendToX, mpeYBipolar;

    QGroup groups[kMaxGroups];   // 0 OUTPUT, 1 PITCH, 2 LINK, 3 VOICES, 4 VOICE OPTS, 5 GLIDE, 6 SCALE, 7 TUNING, 8 MPE, 9 MPE Y

    MasterScreen()
    {
        volume.state.large = true; spread.state.large = true;   // the only large knobs of the page; every other knob has the common size (140)
        tune.state.bipolar = true;
        portaSlope.state.bipolar = true;

        transpose.state.format = NumberField::Format::IntSigned;
        transpose.setLabel ("TRANSPOSE");
        bendUp.state.format = NumberField::Format::IntPlain;
        bendUp.setLabel ("BEND UP");
        bendDown.state.format = NumberField::Format::IntPlain;
        bendDown.setLabel ("BEND DOWN");
        bendLink.setLabel ("LINK");
        bendLink.state.showState = true;
        polyphony.state.format = NumberField::Format::IntPlain;
        polyphony.setLabel ("POLY");
        legato.setLabel ("LEGATO"); legato.state.showState = true;
        polyLegato.setLabel ("POLY LEG"); polyLegato.state.showState = true;
        phaseLegato.setLabel ("PHASE LEG"); phaseLegato.state.showState = true;
        sameNote.setCaption ("SAME NOTE");
        sameNote.minWidth = 140;   // a cell of the grid of the VOICES block
        stealPriority.setCaption ("STEAL");
        stealPriority.minWidth = 140;
        velocityTrack.state.format = NumberField::Format::IntSigned;
        velocityTrack.setLabel ("VEL TRACK");
        stereoRouting.setCaption ("STEREO");
        voiceOverride.setCaption ("VOICE OVERRIDE");
        portaAlways.setLabel ("ALWAYS"); portaAlways.state.showState = true;
        portaScaled.setLabel ("SCALED"); portaScaled.state.showState = true;
        scaleType.setCaption ("SCALE");
        scaleKey.setCaption ("KEY");
        scaleMode.setCaption ("MODE");
        tuningA4.state.format = NumberField::Format::Decimal2;
        tuningA4.setLabel ("A4 HZ");
        tuningScale.setCaption ("TUNING");
        mtsEsp.setLabel ("MTS-ESP"); mtsEsp.state.showState = true;
        mtsNoteOn.setLabel ("NOTE-ON"); mtsNoteOn.state.showState = true;
        mpeOn.setLabel ("MPE"); mpeOn.state.showState = true;
        mpeBend.state.format = NumberField::Format::IntPlain;
        mpeBend.setLabel ("MPE BEND");
        mpeMacro.setCaption ("XYZ MACRO");
        mpeMacro.minWidth = 140;
        mpeYModwheel.setLabel ("Y TO WHEEL"); mpeYModwheel.state.showState = true;
        mpeBendToX.setLabel ("BEND TO X"); mpeBendToX.state.showState = true;
        mpeYBipolar.setLabel ("Y BIPOLAR"); mpeYBipolar.state.showState = true;

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
                    else if (blockRect (2).contains (x, y)) hit = (y < stealPriority.rect().y ? 3 : 4);
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
            const int bw = (w - m) / 2, bh = (h - m) / 2;
            return { rect_.x + m + (b % 2) * (bw + m), y0 + (b / 2) * (bh + m), b % 2 == 1 ? rect_.right() - m - (rect_.x + m + bw + m) : bw, bh };
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
        return { &volume, &meters, &spread, &stereoRouting, &voiceOverride, &tune, &transpose, &bendUp, &bendDown, &bendLink, &polyphony, &legato, &polyLegato, &phaseLegato, &sameNote, &stealPriority, &velocityTrack, &voiceMeter,
                 &portaTime, &portaSlope, &portaAlways, &portaScaled, &portaCurve };
    }
    std::vector<Component*> sideTuningComponents()
    {
        return { &scaleType, &scaleKey, &scaleMode, &keyboard, &tuningA4, &tuningScale, &mtsEsp, &mtsNoteOn, &mpeOn, &mpeBend, &mpeMacro, &mpeYModwheel, &mpeBendToX, &mpeYBipolar };
    }

    void layout() override
    {
        const int g = kGap, lane = kQLane;
        sideMain.place ({ rect_.x + 300, rect_.y + 4, 160, 56 });
        sideTuning.place ({ rect_.x + 468, rect_.y + 4, 220, 56 });

        // ---- side MAIN
        {   // OUTPUT: the meters at the left (with their scale), VOLUME and SPREAD big, the two steppers at the right
            const Rect in = inner (0);
            const int mw = 84;
            meters.place ({ in.x, in.y + 4, mw, in.h - 8 });
            const int kw = 150;   // the two large knobs of the page (every other knob has the common size)
            volume.place ({ in.x + mw + 8, in.y, kw, in.h });
            spread.place ({ in.x + mw + 8 + kw + g, in.y, kw, in.h });
            const int sx = in.x + mw + 8 + 2 * kw + 2 * g + 4, sw = in.right() - sx, sh = 64, top = in.y + (in.h - 2 * (lane + sh) - g) / 2;
            stereoRouting.place ({ sx, top + lane, sw, sh });
            voiceOverride.place ({ sx, top + 2 * lane + sh + g, sw, sh });
        }
        {   // PITCH: the cell grid of VOICES (four columns of equal width, the same gaps): one row of four cells of one height (TUNE, TRANSPOSE, BEND UP, BEND DOWN: PITCH 1 Q group), LINK under it across the width (PITCH 2 Q group),
            // the same toggle as LEGATO and the others
            const Rect in = inner (1);
            const int cw = (in.w - 3 * g) / 4, h0 = 130, linkH = 64;
            auto col = [&] (int c) { return in.x + c * (cw + g); };
            const int y0 = in.y + lane, y1 = std::min (in.bottom() - linkH, y0 + h0 + g + lane);
            tune.place ({ col (0), y0, cw, h0 });
            transpose.place ({ col (1), y0, cw, h0 });
            bendUp.place ({ col (2), y0, cw, h0 });
            bendDown.place ({ col (3), y0, in.right() - col (3), h0 });
            bendLink.place ({ in.x, y1, in.w, linkH });
        }
        {   // VOICES: ONE grid of equal cells, rows of 64 pixels: POLY, LEGATO, POLY LEG, SAME NOTE (VOICES 1 Q group, four cells); STEAL (span 2), PHASE LEG (span 1), VEL TRACK (span 1) (VOICES 2 Q group); the voice meter across the width
            const Rect in = inner (2);
            const int cw = (in.w - 3 * g) / 4, ch = 64;
            auto cell = [&] (int col, int row, int span) { return Rect { in.x + col * (cw + g), in.y + lane + row * (ch + g), span * cw + (span - 1) * g, ch }; };
            polyphony.place (cell (0, 0, 1));
            legato.place (cell (1, 0, 1));
            polyLegato.place (cell (2, 0, 1));
            sameNote.place (cell (3, 0, 1));
            stealPriority.place (cell (0, 1, 2));
            phaseLegato.place (cell (2, 1, 1));
            velocityTrack.place (cell (3, 1, 1));
            const Rect mc = cell (0, 2, 4);
            voiceMeter.place ({ mc.x, mc.y, mc.w, std::min (ch, std::max (40, in.bottom() - mc.y)) });
        }
        {   // GLIDE: the same grid again: the knobs GLIDE and SLOPE (cells of the height of the row), ALWAYS and SCALED stacked over the two cells at the right (the wide toggle of the page, 56 px, the lane of the chip
            // between them): the four cells of the Q group; the curve under them across the width
            const Rect in = inner (3);
            const int cw = (in.w - 3 * g) / 4, h0 = 130, th = 56;
            auto col = [&] (int c) { return in.x + c * (cw + g); };
            const int y0 = in.y + lane, y1 = y0 + h0 + g + lane;
            portaTime.place ({ col (0), y0, cw, h0 });
            portaSlope.place ({ col (1), y0, cw, h0 });
            portaAlways.place ({ col (2), y0, in.right() - col (2), th });
            portaScaled.place ({ col (2), y0 + th + lane, in.right() - col (2), th });
            portaCurve.place ({ in.x, y1, in.w, std::max (40, in.bottom() - y1) });
        }
        // ---- side TUNING & MPE
        {   // SCALE: the three steppers in a row spanning the width, the big keyboard under them
            const Rect in = inner (4);
            const int cw = (in.w - 2 * g) / 3;
            scaleType.place ({ in.x, in.y + lane, cw, 64 });
            scaleKey.place ({ in.x + cw + g, in.y + lane, cw, 64 });
            scaleMode.place ({ in.x + 2 * (cw + g), in.y + lane, in.right() - (in.x + 2 * (cw + g)), 64 });
            const int ky = in.y + lane + 64 + 10;
            keyboard.place ({ in.x, ky, in.w, std::max (90, in.bottom() - ky) });
        }
        {   // TUNING: A4, the tuning, the two MTS switches in a 2x2 grid
            const Rect in = inner (5);
            const int cw = (in.w - g) / 2, ch = 64;
            tuningA4.place ({ in.x, in.y + lane, cw, ch });
            tuningScale.place ({ in.x + cw + g, in.y + lane, in.right() - (in.x + cw + g), ch });
            mtsEsp.place ({ in.x, in.y + 2 * lane + ch + g, cw, ch });
            mtsNoteOn.place ({ in.x + cw + g, in.y + 2 * lane + ch + g, in.right() - (in.x + cw + g), ch });
        }
        {   // MPE: MPE, MPE BEND, XYZ MACRO; Y TO WHEEL, BEND TO X, Y BIPOLAR in a 3x2 grid
            const Rect in = inner (6);
            const int cw = (in.w - 2 * g) / 3, ch = 64;
            mpeOn.place ({ in.x, in.y + lane, cw, ch });
            mpeBend.place ({ in.x + cw + g, in.y + lane, cw, ch });
            mpeMacro.place ({ in.x + 2 * (cw + g), in.y + lane, in.right() - (in.x + 2 * (cw + g)), ch });
            mpeYModwheel.place ({ in.x, in.y + 2 * lane + ch + g, cw, ch });
            mpeBendToX.place ({ in.x + cw + g, in.y + 2 * lane + ch + g, cw, ch });
            mpeYBipolar.place ({ in.x + 2 * (cw + g), in.y + 2 * lane + ch + g, in.right() - (in.x + 2 * (cw + g)), ch });
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
    }

private:
    int side_ = SideMain, activeGroup_ = 0;

    void buildGroups()
    {
        groups[0].set (0, &volume); groups[0].set (1, &spread); groups[0].set (2, &stereoRouting); groups[0].set (3, &voiceOverride);
        groups[1].set (0, &tune); groups[1].set (1, &transpose); groups[1].set (2, &bendUp); groups[1].set (3, &bendDown);
        groups[2].set (0, &bendLink);
        groups[3].set (0, &polyphony); groups[3].set (1, &legato); groups[3].set (2, &polyLegato); groups[3].set (3, &sameNote);
        groups[4].set (0, &stealPriority); groups[4].set (1, &phaseLegato); groups[4].set (2, &velocityTrack);
        groups[5].set (0, &portaTime); groups[5].set (1, &portaSlope); groups[5].set (2, &portaAlways); groups[5].set (3, &portaScaled);
        groups[6].set (0, &scaleType); groups[6].set (1, &scaleKey); groups[6].set (2, &scaleMode);
        groups[7].set (0, &tuningA4); groups[7].set (1, &tuningScale); groups[7].set (2, &mtsEsp); groups[7].set (3, &mtsNoteOn);
        groups[8].set (0, &mpeOn); groups[8].set (1, &mpeBend); groups[8].set (2, &mpeMacro);
        groups[9].set (0, &mpeYModwheel); groups[9].set (1, &mpeBendToX); groups[9].set (2, &mpeYBipolar);
        setActiveGroup (0);
    }
};

}} // namespace synthyboy_mpc::ui
