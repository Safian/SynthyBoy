// SynthyBoy - MPC UI component library: VoiceScreen, the page of the Voice tab (1280 x 628): the header (VOICE CONTROL, the Q group of the step: the row, the step, the length), the step grid (8 steps, the
// six rows PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2: draw with a finger, the step a voice plays is lit), a strip with ALL the values of the selected step, and three blocks that use their height: RANDOM (the four
// spreads and 10x DETUNE), SCALING (the time scale of the envelopes and the LFOs and its mode), TARGETS (which sources the voice control acts on). One Q group each (the Targets of the fifth, NOISE, is beside the group).
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
#include "voice_grid.h"

namespace synthyboy_mpc { namespace ui {

class VoiceScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 4, kHeaderH = 80, kGridH = 236, kStripH = 40, kBlockTitleH = 34, kBlockPad = 6, kBlockGap = 8;

    // Header
    NumberField length;       // voicectl.length (1..8)
    Stepper qStepSelect;      // Q-Link step selector (1..8)
    Stepper qRowSelect;       // Q-Link row selector (PAN, DETUNE, CUTOFF, ENV, MOD1, MOD2)
    NumberField stepValue;    // the value of the selected step in the selected row (-100 .. +100): the fourth Q-Link of the group STEP

    // Step Grid
    VoiceGrid grid;

    // Block 0: RANDOM (voicectl.rand.*)
    Knob randPan { "PAN" };
    Knob randDetune { "DETUNE" };
    Knob randCutoff { "CUTOFF" };
    Knob randEnv { "ENV" };
    ToggleIcon randDetune10x;

    // Block 1: SCALING (voicectl.scale.*)
    Knob scaleEnv { "ENV TIME" };
    Knob scaleLfo { "LFO TIME" };
    Stepper scaleLfoMode;

    // Block 2: TARGETS (voicectl.osc.*)
    ToggleIcon oscA, oscB, oscC, oscSub, oscNoise;

    // QGroups
    QGroup groups[kMaxGroups];

    VoiceScreen()
    {
        length.state.format = NumberField::Format::IntPlain;
        length.setLabel ("LENGTH");
        stepValue.state.format = NumberField::Format::IntSigned;
        stepValue.state.min = -100; stepValue.state.max = 100; stepValue.state.step = 1; stepValue.state.pxPerStep = 1;
        stepValue.setLabel ("VALUE");
        qStepSelect.setItem ("STEP 1");
        qRowSelect.setItem ("PAN");
        qStepSelect.setCaption ("STEP");
        qRowSelect.setCaption ("ROW");
        scaleLfoMode.setCaption ("LFO TIME MODE");

        randDetune10x.setLabel ("10x DETUNE");
        randDetune10x.state.showState = true;
        for (ToggleIcon* t : { &oscA, &oscB, &oscC, &oscSub, &oscNoise })
            t->state.showState = false;
        oscA.setLabel ("OSC A");
        oscB.setLabel ("OSC B");
        oscC.setLabel ("OSC C");
        oscSub.setLabel ("SUB");
        oscNoise.setLabel ("NOISE");
        for (Knob* k : { &randPan, &randDetune, &randCutoff, &randEnv, &scaleEnv, &scaleLfo })
            k->state.large = true;

        addKid (length);
        addKid (qStepSelect);
        addKid (qRowSelect);
        addKid (stepValue);
        addKid (grid);
        addKid (randPan); addKid (randDetune); addKid (randCutoff); addKid (randEnv); addKid (randDetune10x);
        addKid (scaleEnv); addKid (scaleLfo); addKid (scaleLfoMode);
        addKid (oscA); addKid (oscB); addKid (oscC); addKid (oscSub); addKid (oscNoise);

        buildGroups();
    }

    Size minSize() const override { return { 1280, 628 }; }

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
        static const char* const kNames[kMaxGroups] = { "STEP", "RANDOM", "SCALING", "TARGETS" };
        return g >= 0 && g < kMaxGroups ? kNames[g] : "";
    }

    Change turnQ (int slot, double delta) { return groups[activeGroup_].turn (slot, delta); }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (grid.rect().contains (x, y) || qStepSelect.rect().contains (x, y) || qRowSelect.rect().contains (x, y) || length.rect().contains (x, y) || stepValue.rect().contains (x, y) || stripRect().contains (x, y))
            setActiveGroup (0);
        else if (blockRect (0).contains (x, y))
            setActiveGroup (1);
        else if (blockRect (1).contains (x, y))
            setActiveGroup (2);
        else if (blockRect (2).contains (x, y))
            setActiveGroup (3);
        return c;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        groups[activeGroup_].draw (sb);
    }

    Rect stripRect() const noexcept { return { rect_.x + 8, grid.rect().bottom() + 6, rect_.w - 16, kStripH }; }
    Rect blockRect (int b) const noexcept
    {
        const int m = kBlockGap, y0 = stripRect().bottom() + 8, h = rect_.bottom() - 8 - y0, totalW = rect_.w - 2 * m;
        const int w0 = 520, w1 = 330, w2 = totalW - w0 - w1 - 2 * m;
        if (b == 0) return { rect_.x + m, y0, w0, h };
        if (b == 1) return { rect_.x + m + w0 + m, y0, w1, h };
        return { rect_.x + m + w0 + m + w1 + m, y0, w2, h };
    }
    Rect inner (int b) const noexcept
    {
        const Rect r = blockRect (b);
        return { r.x + kBlockPad, r.y + kBlockTitleH, std::max (0, r.w - 2 * kBlockPad), std::max (0, r.h - kBlockTitleH - kBlockPad) };
    }

protected:
    void layout() override
    {
        const int m = 8, g = kGap, lane = kQLane;
        // the header: the Q group of the step at the right
        const int hy = rect_.y + 20;   // a lane above for the Q index chips
        stepValue.place ({ rect_.right() - m - 150, hy, 150, 56 });
        length.place ({ stepValue.rect().x - m - 130, hy, 130, 56 });
        qRowSelect.place ({ length.rect().x - m - Stepper::kMinW, hy, Stepper::kMinW, 56 });
        qStepSelect.place ({ qRowSelect.rect().x - m - Stepper::kMinW, hy, Stepper::kMinW, 56 });
        grid.place ({ rect_.x + m, rect_.y + kHeaderH + 4, rect_.w - 2 * m, kGridH });

        {   // RANDOM: the four spreads as medium knobs (the dial at the left, name and value beside it) in a 2 x 2 grid, 10x DETUNE in a column of its own at the right
            const Rect in = inner (0);
            const int tw = 140, kw = (in.w - tw - 2 * g) / 2, kh = (in.h - 2 * lane - g) / 2;
            Knob* ks[4] = { &randPan, &randDetune, &randCutoff, &randEnv };
            for (int i = 0; i < 4; ++i)
            {
                ks[i]->state.side = true; ks[i]->state.large = false;
                ks[i]->place ({ in.x + (i % 2) * (kw + g), in.y + lane + (i / 2) * (kh + lane + g), kw, std::max (72, kh) });
            }
            randDetune10x.place ({ in.right() - tw, in.y + (in.h - 64) / 2, tw, 64 });
        }
        {   // SCALING: the two time knobs, the mode under them
            const Rect in = inner (1);
            const int kw = (in.w - g) / 2, kh = in.h - lane - 64 - lane - g;
            scaleEnv.place ({ in.x, in.y + lane, kw, std::max (72, kh) });
            scaleLfo.place ({ in.x + kw + g, in.y + lane, in.right() - (in.x + kw + g), std::max (72, kh) });
            scaleLfoMode.place ({ in.x, in.bottom() - 64, std::max (Stepper::kMinW, std::min (in.w, 260)), 64 });
        }
        {   // TARGETS: from the top of the block in a grid of equal cells: A, B, C, SUB (the Q group) in a row, NOISE (beside the group) in the row under it
            const Rect in = inner (2);
            const int tw = (in.w - 3 * g) / 4, th = 64;
            ToggleIcon* ts[4] = { &oscA, &oscB, &oscC, &oscSub };
            for (int i = 0; i < 4; ++i)
                ts[i]->place ({ in.x + i * (tw + g), in.y + lane, i == 3 ? in.right() - (in.x + 3 * (tw + g)) : tw, th });
            oscNoise.place ({ in.x, in.y + lane + th + g, in.w, th });
        }
    }

    void drawUnder (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        for (int b = 0; b < 3; ++b)
            p.block (blockRect (b), false);
        // the strip with every value of the selected step
        const Rect sr = stripRect();
        p.control (sr, false, false);
        (void) T;
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + 14, rect_.y + 4, std::max (0, qStepSelect.rect().x - rect_.x - 20), 56 }, "VOICE CONTROL", T.fTitle, p.c (T.text, 255), T.fMin);
        for (int b = 0; b < 3; ++b)
        {
            const Rect r = blockRect (b);
            p.textLeft ({ r.x + kBlockPad + 6, r.y + 3, std::max (0, r.w - 20), 16 }, groupName (b + 1), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        }
        // the selected step: every row's value
        const Rect sr = stripRect();
        char t[40];
        std::snprintf (t, sizeof t, "STEP %d", grid.selectedStep + 1);
        p.textLeft ({ sr.x + 12, sr.y, 96, sr.h }, t, T.fLabel, p.c (T.q, 255), T.fMin);
        const int cw = (sr.w - 120) / VoiceGrid::kNumRows;
        for (int r = 0; r < VoiceGrid::kNumRows; ++r)
        {
            char v[24];
            VoiceGrid::formatValue (r, grid.values[r][std::clamp (grid.selectedStep, 0, VoiceGrid::kNumSteps - 1)], v, sizeof v);
            std::snprintf (t, sizeof t, "%s  %s", VoiceGrid::rowName (r), v);
            p.textLeft ({ sr.x + 112 + r * cw, sr.y, cw - 6, sr.h }, t, T.fLabel - 1.0f, p.c (r == grid.activeRow ? T.text : T.textDim, 255), T.fMin);
        }
    }

private:
    int activeGroup_ = 0;

    void buildGroups()
    {
        groups[0].set (0, &qStepSelect);
        groups[0].set (1, &qRowSelect);
        groups[0].set (2, &length);
        groups[0].set (3, &stepValue);
        groups[1].set (0, &randPan); groups[1].set (1, &randDetune); groups[1].set (2, &randCutoff); groups[1].set (3, &randEnv);
        groups[2].set (0, &scaleEnv); groups[2].set (1, &scaleLfo); groups[2].set (2, &scaleLfoMode);
        groups[3].set (0, &oscA); groups[3].set (1, &oscB); groups[3].set (2, &oscC); groups[3].set (3, &oscSub);
        setActiveGroup (0);
    }
};

}} // namespace synthyboy_mpc::ui
