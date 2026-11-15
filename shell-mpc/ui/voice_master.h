// SynthyBoy - MPC UI component library: Master view and displays (docs/MPC.md, ui-elem-mastervoice.md).
// Full screen layout with Output, Pitch, Voices, Glide, Tuning/Scale, MPE blocks, Q-Link groups, and displays.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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
        Rect lbl { rect_.x + 4, rect_.y + 2, rect_.w - 8, 16 };
        p.textCentered (lbl, "GLIDE CURVE", 10.0f, p.c (T.textDim, 200));
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
    Size minSize() const override { return { 140, 50 }; }

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

        const int m = 4;
        const int kbX = rect_.x + m;
        const int kbY = rect_.y + m + 14;
        const int kbW = rect_.w - 2 * m;
        const int kbH = std::max (16, rect_.h - 2 * m - 16);

        // 7 white keys: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
        static const int kWhites[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const float wkW = static_cast<float> (kbW) / 7.0f;

        for (int i = 0; i < 7; ++i)
        {
            const int note = kWhites[i];
            const bool in = isScaleNote (scaleType, scaleKey, note);
            const Rect wr { kbX + static_cast<int> (std::round (i * wkW)), kbY,
                            static_cast<int> (std::round ((i + 1) * wkW)) - static_cast<int> (std::round (i * wkW)), kbH };
            const uint32_t fillCol = in ? T.onBright : T.track;
            p.rect (wr, p.c (fillCol, in ? 230 : 80));
            p.rrect (wr, 2.0f, p.c (T.keyline, 180), 1.0f);
            if (in && note == scaleKey) // Root note indicator
                p.rect (wr.x + 2, wr.bottom() - 6, wr.right() - 2, wr.bottom() - 2, p.c (T.flow[0], 255));
        }

        // 5 black keys: C#(1), D#(3), F#(6), G#(8), A#(10)
        static const struct { int note; float pos; } kBlacks[5] = {
            { 1, 0.7f }, { 3, 1.7f }, { 6, 3.7f }, { 8, 4.7f }, { 10, 5.7f }
        };
        const float bkW = wkW * 0.6f;
        const int bkH = static_cast<int> (std::round (kbH * 0.62f));

        for (int i = 0; i < 5; ++i)
        {
            const int note = kBlacks[i].note;
            const bool in = isScaleNote (scaleType, scaleKey, note);
            const int bx = kbX + static_cast<int> (std::round (kBlacks[i].pos * wkW));
            const Rect br { bx, kbY, static_cast<int> (std::round (bkW)), bkH };
            const uint32_t fillCol = in ? T.flow[1] : T.bg1;
            p.rect (br, p.c (fillCol, in ? 240 : 200));
            p.rrect (br, 1.5f, p.c (in ? T.flow[0] : T.keylineHi, 255), 1.0f);
            if (in && note == scaleKey) // Root note indicator
                p.rect (br.x + 1, br.bottom() - 4, br.right() - 1, br.bottom() - 1, p.c (T.flow[0], 255));
        }

        // Scale label header
        char lblBuf[32];
        static const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        std::snprintf (lblBuf, sizeof lblBuf, "SCALE: %s", kKeyNames[std::clamp (scaleKey, 0, 11)]);
        Rect topLbl { rect_.x + 4, rect_.y + 2, rect_.w - 8, 14 };
        p.textCentered (topLbl, lblBuf, 10.0f, p.c (T.textDim, 220));
    }
};

// ===============================================================================================================================
/** Live stereo peak output meters (L / R) with peak hold line and level gradients. */
class PeakMeterDisplay : public Component
{
public:
    float peakL = 0.0f; // 0 .. 1.5
    float peakR = 0.0f;

    PeakMeterDisplay() = default;
    Size minSize() const override { return { 36, 120 }; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        p.rrect (rect_, T.rControl, p.c (T.panel1, 200));
        p.rrect (rect_, T.rControl, p.c (T.keyline, 160), 1.0f);

        const int m = 4;
        const int barW = std::max (6, (rect_.w - 2 * m - 4) / 2);
        const int barH = rect_.h - 2 * m - 16;
        const int barY = rect_.y + m + 14;

        // Left meter
        const Rect lr { rect_.x + m, barY, barW, barH };
        drawBar (p, lr, peakL);

        // Right meter
        const Rect rr { rect_.x + m + barW + 4, barY, barW, barH };
        drawBar (p, rr, peakR);

        // Label
        Rect lbl { rect_.x + 2, rect_.y + 2, rect_.w - 4, 14 };
        p.textCentered (lbl, "PEAK L/R", 9.0f, p.c (T.textDim, 200));
    }

private:
    void drawBar (Pen& p, const Rect& r, float level) const noexcept
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

        // -6dB line
        const float y6dB = static_cast<float> (r.bottom() - static_cast<int> (0.5f * r.h));
        p.line (static_cast<float> (r.x), y6dB, static_cast<float> (r.right()), y6dB, 0.8f, p.c (T.keylineHi, 140));
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
        std::snprintf (buf, sizeof buf, "VOICES: %d / %d", activeVoices, polyphony);
        p.textCentered (rect_, buf, 11.0f, p.c (T.text, 255));
    }
};

// ===============================================================================================================================
/** MasterScreen: the full-screen composite of the Master element. */
class MasterScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 6;

    // Header
    MenuButton close { "CLOSE", MenuButton::Icon::None };

    // Block 1: OUTPUT
    Knob volume { "VOLUME" };
    PeakMeterDisplay meters;
    Knob spread { "SPREAD" };
    Stepper stereoRouting;
    Stepper voiceOverride;

    // Block 2: PITCH
    Knob tune { "TUNE" };
    NumberField transpose;
    NumberField bendUp;
    NumberField bendDown;
    ToggleIcon bendLink;

    // Block 3: VOICES
    NumberField polyphony;
    ToggleIcon legato;
    ToggleIcon polyLegato;
    ToggleIcon phaseLegato;
    Stepper sameNote;
    Stepper stealPriority;
    VoiceMeterDisplay voiceMeter;

    // Block 4: GLIDE
    Knob portaTime { "GLIDE" };
    Knob portaSlope { "SLOPE" };
    ToggleIcon portaAlways;
    ToggleIcon portaScaled;
    PortamentoCurveDisplay portaCurve;

    // Block 5: TUNING / SCALE
    Stepper scaleType;
    Stepper scaleKey;
    Stepper scaleMode;
    NumberField tuningA4;
    Stepper tuningScale;
    ToggleIcon mtsEsp;
    ToggleIcon mtsNoteOn;
    MiniKeyboardDisplay keyboard;

    // Block 6: MPE
    ToggleIcon mpeOn;
    NumberField mpeBend;
    Stepper mpeMacro;
    ToggleIcon mpeYModwheel;
    ToggleIcon mpeBendToX;
    ToggleIcon mpeYBipolar;

    // QGroups
    QGroup groups[kMaxGroups];

    MasterScreen()
    {
        // Initial control configurations
        volume.state.bipolar = false;
        spread.state.bipolar = false;
        tune.state.bipolar = true;
        portaSlope.state.bipolar = true;

        transpose.state.format = NumberField::Format::IntSigned;
        transpose.setLabel ("TRANS");
        bendUp.state.format = NumberField::Format::IntPlain;
        bendUp.setLabel ("BEND UP");
        bendDown.state.format = NumberField::Format::IntPlain;
        bendDown.setLabel ("BEND DN");
        bendLink.setLabel ("LINK");

        polyphony.state.format = NumberField::Format::IntPlain;
        polyphony.setLabel ("POLY");
        legato.setLabel ("LEGATO");
        polyLegato.setLabel ("POLY LEG");
        phaseLegato.setLabel ("PHASE LEG");

        portaAlways.setLabel ("ALWAYS");
        portaScaled.setLabel ("SCALED");

        tuningA4.state.format = NumberField::Format::Decimal2;
        tuningA4.setLabel ("A4 (HZ)");
        mtsEsp.setLabel ("MTS-ESP");
        mtsNoteOn.setLabel ("NOTE-ON");

        mpeOn.setLabel ("MPE");
        mpeBend.state.format = NumberField::Format::IntPlain;
        mpeBend.setLabel ("MPE BEND");
        mpeYModwheel.setLabel ("Y->WHEEL");
        mpeBendToX.setLabel ("BEND->X");
        mpeYBipolar.setLabel ("Y BIPOL");

        // Add children to composite
        addKid (close);
        addKid (volume); addKid (meters); addKid (spread); addKid (stereoRouting); addKid (voiceOverride);
        addKid (tune); addKid (transpose); addKid (bendUp); addKid (bendDown); addKid (bendLink);
        addKid (polyphony); addKid (legato); addKid (polyLegato); addKid (phaseLegato); addKid (sameNote); addKid (stealPriority); addKid (voiceMeter);
        addKid (portaTime); addKid (portaSlope); addKid (portaAlways); addKid (portaScaled); addKid (portaCurve);
        addKid (scaleType); addKid (scaleKey); addKid (scaleMode); addKid (tuningA4); addKid (tuningScale); addKid (mtsEsp); addKid (mtsNoteOn); addKid (keyboard);
        addKid (mpeOn); addKid (mpeBend); addKid (mpeMacro); addKid (mpeYModwheel); addKid (mpeBendToX); addKid (mpeYBipolar);

        buildGroups();
    }

    Size minSize() const override { return { 1280, 800 }; }

    int numGroups() const noexcept { return kMaxGroups; }
    int activeGroup() const noexcept { return activeGroup_; }
    void setActiveGroup (int g) noexcept
    {
        activeGroup_ = std::clamp (g, 0, kMaxGroups - 1);
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setActive (i == activeGroup_);
    }

    const char* groupName (int g) const noexcept
    {
        static const char* const kNames[kMaxGroups] = { "Output", "Pitch", "Voices", "Glide", "Tuning", "MPE" };
        return g >= 0 && g < kMaxGroups ? kNames[g] : "";
    }

    Change turnQ (int slot, double delta) { return groups[activeGroup_].turn (slot, delta); }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        // Switch active Q group if touch was in a block
        for (int i = 0; i < kMaxGroups; ++i)
        {
            if (blockRect (i).contains (x, y))
            {
                setActiveGroup (i);
                break;
            }
        }
        return c;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Pen p (sb, enabled_);
        Cell cell (sb, rect_);
        const theme::Look& T = theme::L();

        // 1. Draw 6 block panels
        for (int i = 0; i < kMaxGroups; ++i)
        {
            const Rect br = blockRect (i);
            const bool isActive = (i == activeGroup_);
            p.block (br, isActive);
            // Title header
            Rect titleBox { br.x + 8, br.y + 4, br.w - 16, 20 };
            p.textLeft (titleBox, groupName (i), 12.0f, p.c (isActive ? T.q : T.textDim, 255));
        }

        // Header label
        Rect hdr { rect_.x + 104, rect_.y + 8, 200, 56 };
        p.textLeft (hdr, "MASTER", 24.0f, p.c (T.text, 255));

        // Draw children
        Composite::draw (sb);

        // Draw active QGroup frame
        groups[activeGroup_].draw (sb);
    }

    Rect blockRect (int b) const noexcept
    {
        const int m = 8;
        const int topY = rect_.y + 70;
        const int totalH = rect_.bottom() - m - topY;
        const int blockH = (totalH - m) / 2;
        const int totalW = rect_.w - 2 * m;
        const int blockW = (totalW - 2 * m) / 3;

        const int col = b % 3;
        const int row = b / 3;
        const int bx = rect_.x + m + col * (blockW + m);
        const int by = topY + row * (blockH + m);
        return { bx, by, blockW, blockH };
    }

protected:
    void layout() override
    {
        // Close button at top-left
        close.place ({ rect_.x + 8, rect_.y + 8, 84, 56 });

        // Block 0: OUTPUT
        {
            const Rect br = blockRect (0);
            const int inY = br.y + 26;
            volume.place ({ br.x + 8, inY + 8, 116, 120 });
            meters.place ({ br.x + 130, inY + 8, 40, 120 });
            spread.place ({ br.x + 176, inY + 8, 108, 116 });
            stereoRouting.place ({ br.x + 290, inY + 8, 116, 56 });
            voiceOverride.place ({ br.x + 290, inY + 70, 116, 56 });
        }

        // Block 1: PITCH
        {
            const Rect br = blockRect (1);
            const int inY = br.y + 26;
            tune.place ({ br.x + 10, inY + 8, 116, 116 });
            transpose.place ({ br.x + 134, inY + 8, 128, 56 });
            bendUp.place ({ br.x + 270, inY + 8, 134, 56 });
            bendDown.place ({ br.x + 134, inY + 72, 128, 56 });
            bendLink.place ({ br.x + 270, inY + 72, 134, 56 });
        }

        // Block 2: VOICES
        {
            const Rect br = blockRect (2);
            const int inY = br.y + 26;
            polyphony.place ({ br.x + 8, inY + 8, 100, 56 });
            voiceMeter.place ({ br.x + 114, inY + 12, 180, 48 });
            sameNote.place ({ br.x + 300, inY + 8, 106, 56 });

            legato.place ({ br.x + 8, inY + 70, 94, 56 });
            polyLegato.place ({ br.x + 108, inY + 70, 94, 56 });
            phaseLegato.place ({ br.x + 208, inY + 70, 94, 56 });
            stealPriority.place ({ br.x + 308, inY + 70, 98, 56 });
        }

        // Block 3: GLIDE
        {
            const Rect br = blockRect (3);
            const int inY = br.y + 26;
            portaTime.place ({ br.x + 8, inY + 8, 116, 116 });
            portaSlope.place ({ br.x + 130, inY + 8, 116, 116 });
            portaCurve.place ({ br.x + 252, inY + 8, 154, 76 });
            portaAlways.place ({ br.x + 252, inY + 90, 74, 56 });
            portaScaled.place ({ br.x + 332, inY + 90, 74, 56 });
        }

        // Block 4: TUNING / SCALE
        {
            const Rect br = blockRect (4);
            const int inY = br.y + 26;
            scaleType.place ({ br.x + 8, inY + 8, 160, 56 });
            scaleKey.place ({ br.x + 174, inY + 8, 70, 56 });
            scaleMode.place ({ br.x + 250, inY + 8, 156, 56 });

            tuningA4.place ({ br.x + 8, inY + 68, 100, 56 });
            tuningScale.place ({ br.x + 114, inY + 68, 130, 56 });
            mtsEsp.place ({ br.x + 250, inY + 68, 74, 56 });
            mtsNoteOn.place ({ br.x + 330, inY + 68, 76, 56 });

            keyboard.place ({ br.x + 8, inY + 130, br.w - 16, 68 });
        }

        // Block 5: MPE
        {
            const Rect br = blockRect (5);
            const int inY = br.y + 26;
            mpeOn.place ({ br.x + 8, inY + 8, 80, 56 });
            mpeBend.place ({ br.x + 94, inY + 8, 120, 56 });
            mpeMacro.place ({ br.x + 220, inY + 8, 186, 56 });

            mpeYModwheel.place ({ br.x + 8, inY + 70, 94, 56 });
            mpeBendToX.place ({ br.x + 108, inY + 70, 94, 56 });
            mpeYBipolar.place ({ br.x + 208, inY + 70, 94, 56 });
        }
    }

private:
    int activeGroup_ = 0;

    void buildGroups()
    {
        // Group 0: Output [Volume, Spread, Stereo Routing, Voice Override]
        groups[0].set (0, &volume);
        groups[0].set (1, &spread);
        groups[0].set (2, &stereoRouting);
        groups[0].set (3, &voiceOverride);

        // Group 1: Pitch [Tune, Transpose, Bend Up, Bend Down]
        groups[1].set (0, &tune);
        groups[1].set (1, &transpose);
        groups[1].set (2, &bendUp);
        groups[1].set (3, &bendDown);

        // Group 2: Voices [Polyphony, Legato, Poly Legato, Same Note]
        groups[2].set (0, &polyphony);
        groups[2].set (1, &legato);
        groups[2].set (2, &polyLegato);
        groups[2].set (3, &sameNote);

        // Group 3: Glide [Porta Time, Slope, Always, Scaled]
        groups[3].set (0, &portaTime);
        groups[3].set (1, &portaSlope);
        groups[3].set (2, &portaAlways);
        groups[3].set (3, &portaScaled);

        // Group 4: Tuning [Scale Type, Scale Key, Scale Mode, Tuning A4]
        groups[4].set (0, &scaleType);
        groups[4].set (1, &scaleKey);
        groups[4].set (2, &scaleMode);
        groups[4].set (3, &tuningA4);

        // Group 5: MPE [MPE On, MPE Bend, XYZ Macro, Y Modwheel]
        groups[5].set (0, &mpeOn);
        groups[5].set (1, &mpeBend);
        groups[5].set (2, &mpeMacro);
        groups[5].set (3, &mpeYModwheel);

        setActiveGroup (0);
    }
};

}} // namespace synthyboy_mpc::ui
