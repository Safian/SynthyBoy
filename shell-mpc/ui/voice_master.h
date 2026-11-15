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

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.rrect (rect_, T.rControl, p.c (T.panel1, 200));
        p.rrect (rect_, T.rControl, p.c (T.keyline, 160), 1.0f);

        const int m = 8, labelH = 22;
        const int kbX = rect_.x + m, kbY = rect_.y + m + labelH, kbW = rect_.w - 2 * m, kbH = std::max (24, rect_.h - 2 * m - labelH);
        char lblBuf[48];
        static const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        std::snprintf (lblBuf, sizeof lblBuf, scaleType <= 0 ? "ALL 12 NOTES" : "SCALE ON %s", kKeyNames[std::clamp (scaleKey, 0, 11)]);
        p.textLeft ({ rect_.x + m, rect_.y + 4, rect_.w - 2 * m, labelH }, lblBuf, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);

        // the 7 white keys, then the 5 black keys over them: the notes of the scale are lit in the colour of the signal, the others are dark; the root has a bar under it
        static const int kWhites[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const char* const kName[7] = { "C", "D", "E", "F", "G", "A", "B" };
        const float wkW = static_cast<float> (kbW) / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            const int note = kWhites[i];
            const bool in = isScaleNote (scaleType, scaleKey, note);
            const Rect wr { kbX + static_cast<int> (std::round (i * wkW)), kbY, static_cast<int> (std::round ((i + 1) * wkW)) - static_cast<int> (std::round (i * wkW)), kbH };
            p.rect (wr.x + 1, wr.y, wr.right() - 1, wr.bottom(), p.c (in ? T.text : T.track, in ? 235 : 255));
            p.rrect ({ wr.x + 1, wr.y, wr.w - 2, wr.h }, 3.0f, p.c (in ? T.flow[0] : T.keyline, 255), 1.2f);
            if (in && note == scaleKey)
                p.rect (wr.x + 6, wr.bottom() - 10, wr.right() - 6, wr.bottom() - 4, p.c (T.flow[3], 255));
            p.textCentered ({ wr.x, wr.bottom() - 36, wr.w, 22 }, kName[i], T.fLabel - 2.0f, p.c (in ? T.onBright : T.textDim, 255), T.fMin);
        }
        static const struct { int note; float pos; } kBlacks[5] = { { 1, 0.65f }, { 3, 1.65f }, { 6, 3.65f }, { 8, 4.65f }, { 10, 5.65f } };
        const float bkW = wkW * 0.7f;
        const int bkH = static_cast<int> (std::round (kbH * 0.6f));
        for (int i = 0; i < 5; ++i)
        {
            const bool in = isScaleNote (scaleType, scaleKey, kBlacks[i].note);
            const Rect br { kbX + static_cast<int> (std::round (kBlacks[i].pos * wkW)), kbY, static_cast<int> (std::round (bkW)), bkH };
            p.rect (br, p.c (in ? T.flow[1] : T.bg1, 255));
            p.rrect (br, 3.0f, p.c (in ? T.flow[0] : T.keylineHi, 255), 1.2f);
            if (in && kBlacks[i].note == scaleKey)
                p.rect (br.x + 5, br.bottom() - 9, br.right() - 5, br.bottom() - 4, p.c (T.flow[3], 255));
        }
    }
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

        const int maxVoices = std::max (1, polyphony);
        const float frac = std::clamp (static_cast<float> (activeVoices) / static_cast<float> (maxVoices), 0.0f, 1.0f);
        if (frac > 0.005f)
        {
            const int barW = static_cast<int> (std::lround (frac * (rect_.w - 4)));
            Rect fr { rect_.x + 2, rect_.y + 2, barW, rect_.h - 4 };
            p.paintX2 (static_cast<float> (fr.x), static_cast<float> (fr.right()), T.flow[0], T.flow[1], 255);
            p.rrect (fr, T.rChip - 1.0f, p.c (colour::rgba (T.flow[0], 255)));
            p.plain();
        }

        char buf[32];
        std::snprintf (buf, sizeof buf, "%d / %d VOICES", activeVoices, polyphony);
        p.textCentered (rect_, buf, T.fLabel, p.c (T.text, 255), T.fMin);
    }
};

// ===============================================================================================================================
/** MasterScreen: the page of the Master tab (1280 x 628): a header (MASTER, and the two sides MAIN and TUNING & MPE) and the blocks of the side that is on, each ONE Q group (the last two blocks of the second
    side two):  side MAIN: OUTPUT, PITCH, VOICES, GLIDE (2 x 2 blocks);  side TUNING & MPE: SCALE (with the keyboard), TUNING, MPE (a group of three in each of its two rows).
    Every block uses its whole height (the knobs are big, with their values), every name is in capitals. */
class MasterScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 8, kBlocks = 7, kHeaderH = 64, kBlockTitleH = 34, kBlockPad = 6, kBlockGap = 8;
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

    QGroup groups[kMaxGroups];   // 0 OUTPUT, 1 PITCH, 2 VOICES, 3 GLIDE, 4 SCALE, 5 TUNING, 6 MPE (the switches of the notes), 7 MPE (the options of Y)

    MasterScreen()
    {
        volume.state.large = true; spread.state.large = true; tune.state.large = true; portaTime.state.large = true; portaSlope.state.large = true;
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
            setActiveGroup (side_ == SideMain ? 0 : 4);
        else
            setActiveGroup (activeGroup_);
    }
    static int sideOfGroup (int g) noexcept { return g < 4 ? SideMain : SideTuning; }

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
        static const char* const kNames[kMaxGroups] = { "OUTPUT", "PITCH", "VOICES", "GLIDE", "SCALE", "TUNING", "MPE", "MPE Y" };
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
            // a control of a group makes that group the active one (the blocks MPE have two groups: the rows)
            int hit = -1;
            for (int i = 0; i < kMaxGroups && hit < 0; ++i)
                if (sideOfGroup (i) == side_)
                    for (int k = 0; k < QGroup::kSlots; ++k)
                        if (groups[i].member (k) == cap_)
                            hit = i;
            if (hit < 0)   // not a member: the block it is in
                for (int b = 0; b < kBlocks && hit < 0; ++b)
                    if (blockVisible (b) && blockRect (b).contains (x, y))
                        hit = b == 6 ? (y < blockRect (6).y + blockRect (6).h / 2 + 8 ? 6 : 7) : b;
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
        groups[activeGroup_].draw (sb);
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
        return { &volume, &meters, &spread, &stereoRouting, &voiceOverride, &tune, &transpose, &bendUp, &bendDown, &bendLink, &polyphony, &legato, &polyLegato, &phaseLegato, &sameNote, &stealPriority, &voiceMeter,
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
            const int kw = 150;
            volume.place ({ in.x + mw + 8, in.y, kw, in.h });
            spread.place ({ in.x + mw + 8 + kw + g, in.y, kw, in.h });
            const int sx = in.x + mw + 8 + 2 * kw + 2 * g + 4, sw = in.right() - sx, sh = 64, top = in.y + (in.h - 2 * (lane + sh) - g) / 2;
            stereoRouting.place ({ sx, top + lane, sw, sh });
            voiceOverride.place ({ sx, top + 2 * lane + sh + g, sw, sh });
        }
        {   // PITCH: TUNE big, the three number fields in a column, LINK at the right in the size of a number field
            const Rect in = inner (1);
            tune.place ({ in.x, in.y, 150, in.h });
            const int nx = in.x + 150 + g, nw = 290, rowH = (in.h - 3 * lane - 2 * g) / 3;
            transpose.place ({ nx, in.y + lane, nw, rowH });
            bendUp.place ({ nx, in.y + 2 * lane + rowH + g, nw, rowH });
            bendDown.place ({ nx, in.y + 3 * lane + 2 * rowH + 2 * g, nw, rowH });
            const int lx = nx + nw + g * 2;
            bendLink.place ({ lx, in.y + 2 * lane + rowH + g, in.right() - lx, rowH });
        }
        {   // VOICES: ONE grid of equal cells, rows of 64 pixels: POLY, LEGATO, POLY LEG, SAME NOTE (the Q group, four cells); STEAL and PHASE LEG (two cells each); the voice meter across the width
            const Rect in = inner (2);
            const int cw = (in.w - 3 * g) / 4, ch = 64;
            auto cell = [&] (int col, int row, int span) { return Rect { in.x + col * (cw + g), in.y + lane + row * (ch + g), span * cw + (span - 1) * g, ch }; };
            polyphony.place (cell (0, 0, 1));
            legato.place (cell (1, 0, 1));
            polyLegato.place (cell (2, 0, 1));
            sameNote.place (cell (3, 0, 1));
            stealPriority.place (cell (0, 1, 2));
            phaseLegato.place (cell (2, 1, 2));
            const Rect mc = cell (0, 2, 4);
            voiceMeter.place ({ mc.x, mc.y, mc.w, std::min (ch, std::max (40, in.bottom() - mc.y)) });
        }
        {   // GLIDE: the two knobs, ALWAYS / SCALED stacked (the height of the other switches), the curve at the right
            const Rect in = inner (3);
            portaTime.place ({ in.x, in.y, 150, in.h });
            portaSlope.place ({ in.x + 150 + g, in.y, 150, in.h });
            const int tx = in.x + 300 + 2 * g, tw = 130, th = 64, free = in.h - 2 * th - 2 * lane, y1 = in.y + lane + free / 3;
            portaAlways.place ({ tx, y1, tw, th });
            portaScaled.place ({ tx, y1 + th + lane + free / 3, tw, th });
            const int cx = tx + tw + 2 * g;
            portaCurve.place ({ cx, in.y + 4, in.right() - cx, in.h - 8 });
        }
        // ---- side TUNING & MPE
        {   // SCALE: the three steppers in a row, the big keyboard under them
            const Rect in = inner (4);
            const int sw[3] = { 340, 240, 340 };
            scaleType.place ({ in.x, in.y + lane, sw[0], 64 });
            scaleKey.place ({ in.x + sw[0] + g, in.y + lane, sw[1], 64 });
            scaleMode.place ({ in.x + sw[0] + sw[1] + 2 * g, in.y + lane, sw[2], 64 });
            const int ky = in.y + lane + 64 + 10;
            keyboard.place ({ in.x, ky, in.w, std::max (90, in.bottom() - ky) });
        }
        {   // TUNING: A4, the tuning, the two MTS switches in a grid
            const Rect in = inner (5);
            const int cw = (in.w - g) / 2, rowH = (in.h - 2 * lane - g) / 2;
            tuningA4.place ({ in.x, in.y + lane, cw, rowH });
            tuningScale.place ({ in.x + cw + g, in.y + lane + (rowH - 64) / 2, in.right() - (in.x + cw + g), 64 });
            mtsEsp.place ({ in.x, in.y + 2 * lane + rowH + g, cw, rowH });
            mtsNoteOn.place ({ in.x + cw + g, in.y + 2 * lane + rowH + g, in.right() - (in.x + cw + g), rowH });
        }
        {   // MPE: MPE, MPE BEND, XYZ MACRO; Y TO WHEEL, BEND TO X, Y BIPOLAR
            const Rect in = inner (6);
            const int rowH = (in.h - 2 * lane - g) / 2;
            const int w1 = 150, w2 = 190, w3 = in.w - w1 - w2 - 2 * g;
            mpeOn.place ({ in.x, in.y + lane, w1, rowH });
            mpeBend.place ({ in.x + w1 + g, in.y + lane, w2, rowH });
            mpeMacro.place ({ in.x + w1 + w2 + 2 * g, in.y + lane + (rowH - 64) / 2, w3, 64 });
            const int cw = (in.w - 2 * g) / 3;
            mpeYModwheel.place ({ in.x, in.y + 2 * lane + rowH + g, cw, rowH });
            mpeBendToX.place ({ in.x + cw + g, in.y + 2 * lane + rowH + g, cw, rowH });
            mpeYBipolar.place ({ in.x + 2 * (cw + g), in.y + 2 * lane + rowH + g, in.right() - (in.x + 2 * (cw + g)), rowH });
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
        groups[2].set (0, &polyphony); groups[2].set (1, &legato); groups[2].set (2, &polyLegato); groups[2].set (3, &sameNote);
        groups[3].set (0, &portaTime); groups[3].set (1, &portaSlope); groups[3].set (2, &portaAlways); groups[3].set (3, &portaScaled);
        groups[4].set (0, &scaleType); groups[4].set (1, &scaleKey); groups[4].set (2, &scaleMode);
        groups[5].set (0, &tuningA4); groups[5].set (1, &tuningScale); groups[5].set (2, &mtsEsp); groups[5].set (3, &mtsNoteOn);
        groups[6].set (0, &mpeOn); groups[6].set (1, &mpeBend); groups[6].set (2, &mpeMacro);
        groups[7].set (0, &mpeYModwheel); groups[7].set (1, &mpeBendToX); groups[7].set (2, &mpeYBipolar);
        setActiveGroup (0);
    }
};

}} // namespace synthyboy_mpc::ui
