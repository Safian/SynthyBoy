// SynthyBoy - MPC UI component library: VoiceScreen, the page of the Voice tab (1280 x 628): the header (VOICE CONTROL, the Q group of the step: the row, the step, the length), the step grid (8 steps, the
// six rows PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2: draw with a finger, the step a voice plays is lit), a strip with ALL the values of the selected step, and three blocks that use their height: RANDOM (the four
// spreads in a row of large knobs and the 10x DETUNE LED toggle under them), SCALING (the time scale of the envelopes and the LFOs and its mode as a LED list), TARGETS (which sources the voice control acts on: five LED
// toggles of the same width, two columns). Four Q groups: STEP, RANDOM, [10x DETUNE + SCALING] and TARGETS (the fifth target, NOISE, is touch only: beside the group, no Q slot). The ROW is a LED list of the six rows (rows of 56 px, two columns of three) at the left of the grid:
// the member Q2 of the header group, which lies apart in the layout (the group has no frame, its chips mark it).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "voice_native_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "led_list.h"
#include "led_toggle.h"
#include "qgroup.h"
#include "theme.h"
#include "voice_grid.h"

namespace synthyboy_mpc { namespace ui {

class VoiceScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 4, kHeaderH = 80, kGridH = 236, kStripH = 40, kBlockTitleH = 34, kBlockPad = 6, kBlockGap = 8, kRowBlockW = 252;

    // Header
    NumberField length;       // voicectl.length (1..8)
    Stepper qStepSelect;      // Q-Link step selector (1..8)
    LedList rowList;          // the row (PAN, DETUNE, CUTOFF, ENV, MOD 1, MOD 2): all six visible, in the block at the left of the grid; Q2 of the header group
    NumberField stepValue;    // the value of the selected step in the selected row (-100 .. +100): the fourth Q-Link of the group STEP

    // Step Grid
    VoiceGrid grid;

    // Block 0: RANDOM (voicectl.rand.*)
    Knob randPan { "PAN" };
    Knob randDetune { "DETUNE" };
    Knob randCutoff { "CUTOFF" };
    Knob randEnv { "ENV" };
    LedToggle randDetune10x;

    // Block 1: SCALING (voicectl.scale.*)
    Knob scaleEnv { "ENV TIME" };
    Knob scaleLfo { "LFO TIME" };
    LedList scaleLfoMode;   // PERCENT / RATE: both visible

    // Block 2: TARGETS (voicectl.osc.*)
    LedToggle oscA, oscB, oscC, oscSub, oscNoise;

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
        qStepSelect.setCaption ("STEP");
        {
            const char* names[VoiceGrid::kNumRows];
            for (int r = 0; r < VoiceGrid::kNumRows; ++r)
                names[r] = VoiceGrid::rowName (r);
            rowList.setOptions (names, VoiceGrid::kNumRows);
            rowList.setTitle ("ROW");
            rowList.state.titleInLane = true;
        }
        scaleLfoMode.setTitle ("LFO MODE");

        randDetune10x.setLabel ("10x DETUNE");
        oscA.setLabel ("OSC A");
        oscB.setLabel ("OSC B");
        oscC.setLabel ("OSC C");
        oscSub.setLabel ("SUB");
        oscNoise.setLabel ("NOISE");
        for (Knob* k : { &randPan, &randDetune, &randCutoff, &randEnv, &scaleEnv, &scaleLfo })
            k->state.large = true;

        addKid (length);
        addKid (qStepSelect);
        addKid (stepValue);
        addKid (grid);
        addKid (rowList);
        addKid (randPan); addKid (randDetune); addKid (randCutoff); addKid (randEnv); addKid (randDetune10x);
        addKid (scaleEnv); addKid (scaleLfo); addKid (scaleLfoMode);
        addKid (oscA); addKid (oscB); addKid (oscC); addKid (oscSub); addKid (oscNoise);

        buildGroups();
    }

    bool nativeControls_ = false;

    /** Configures host-native Voice controls: suppresses overlay dials, switches, and length field,
        leaving them to be drawn and handled natively by the MPC host. */
    void setNativeControls (bool native = true)
    {
        nativeControls_ = native;
        show (length, ! native);
        length.setEnabled (! native);

        show (randPan, ! native);
        show (randDetune, ! native);
        show (randCutoff, ! native);
        show (randEnv, ! native);
        randPan.setEnabled (! native);
        randDetune.setEnabled (! native);
        randCutoff.setEnabled (! native);
        randEnv.setEnabled (! native);

        show (randDetune10x, ! native);
        randDetune10x.setEnabled (! native);

        show (scaleEnv, ! native);
        show (scaleLfo, ! native);
        show (scaleLfoMode, ! native);
        scaleEnv.setEnabled (! native);
        scaleLfo.setEnabled (! native);
        scaleLfoMode.setEnabled (! native);

        show (oscA, ! native);
        show (oscB, ! native);
        show (oscC, ! native);
        show (oscSub, ! native);
        show (oscNoise, ! native);
        oscA.setEnabled (! native);
        oscB.setEnabled (! native);
        oscC.setEnabled (! native);
        oscSub.setEnabled (! native);
        oscNoise.setEnabled (! native);
    }

    bool nativeStepKnobs_ = false;

    void setNativeStepKnobs (bool native = true) noexcept
    {
        nativeStepKnobs_ = native;
    }
    bool nativeStepKnobs() const noexcept { return nativeStepKnobs_; }

    void setNativeSequenceControls (bool native)
    {
        if (nativeSequence_ == native)
            return;
        nativeSequence_ = native;
        setNativeStepKnobs (native);
        Component* const controls[] { &rowList, &qStepSelect, &stepValue };
        for (Component* control : controls)
            show (*control, ! native);
    }
    bool nativeSequenceControls() const noexcept { return nativeSequence_; }

    bool nativeControls() const noexcept { return nativeControls_; }

    /** Returns true if kid component is shown in the composite. */
    bool isShown (const Component& c) const noexcept
    {
        for (int i = 0; i < n_; ++i)
            if (kids_[i] == &c)
                return shown_[i];
        return false;
    }

    /** Returns true if the coordinate falls within any of the converted host-native controls
        so the touch event can be passed through to the MPC host UI without overlay interference. */
    bool isNativeControl (int x, int y) const noexcept
    {
        if (! nativeControls_)
            return false;
        if (nativeSequence_ && (rowList.rect().contains (x, y) || qStepSelect.rect().contains (x, y) || stepValue.rect().contains (x, y)))
            return true;
        if (nativeStepKnobs_)
            for (int step = 0; step < VoiceGrid::kNumSteps; ++step)
            {
                if (grid.knobRect (step).contains (x,y))
                    return true;
            }
        const Component* const widgets[] = { &length, &randPan, &randDetune, &randCutoff, &randEnv, &randDetune10x,
                                             &scaleEnv, &scaleLfo, &scaleLfoMode, &oscA, &oscB, &oscC, &oscSub, &oscNoise };
        for (const Component* widget : widgets)
            if (widget->rect().contains (x, y))
                return true;
        return false;
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
        if (nativeControls_ && isNativeControl (x, y))
            return Change::none();
        const Change c = Composite::touchDown (x, y, nowMs);
        if (grid.rect().contains (x, y) || qStepSelect.rect().contains (x, y) || rowList.rect().contains (x, y) || rowBlockRect().contains (x, y) || length.rect().contains (x, y) || stepValue.rect().contains (x, y) || stripRect().contains (x, y))
        {
            setActiveGroup (0);
        }
        else
        {
            bool hit = false;
            for (int g = 1; g < kMaxGroups; ++g)
            {
                for (int s = 0; s < QGroup::kSlots; ++s)
                {
                    if (const Component* m = groups[g].member (s))
                    {
                        if (m->rect().contains (x, y))
                        {
                            setActiveGroup (g);
                            hit = true;
                            break;
                        }
                    }
                }
                if (hit) break;
            }
            if (! hit)
            {
                if (blockRect (0).contains (x, y))
                    setActiveGroup (x >= randDetune10x.rect().x - 6 ? 2 : 1);
                else if (blockRect (1).contains (x, y))
                    setActiveGroup (2);
                else if (blockRect (2).contains (x, y))
                    setActiveGroup (3);   // TARGETS (NOISE beside it is touch only: it has no Q slot, a touch on it keeps the group of its block)
            }
        }
        return c;
    }

    void drawStaticFurniture (gpu::SceneBuilder& sb) const
    {
        Pen p (sb, enabled_, inactive_);
        Cell cell (sb, rect_);
        drawUnder (p);
        drawFixedLabels (p);
    }

    void draw (gpu::SceneBuilder& sb) const override { drawPage (sb, false); }

    void drawPage (gpu::SceneBuilder& sb, bool nativeGround) const
    {
        if (! nativeGround)
            drawStaticFurniture (sb);
        drawChildren (sb);
        {
            Pen p (sb, enabled_, inactive_);
            Cell cell (sb, rect_);
            drawLiveLabels (p);
        }
        groups[activeGroup_].draw (sb);
        for (int i = 0; i < kMaxGroups; ++i)
            if (i != activeGroup_)
            {
                if (nativeControls_ && i > 0)
                    continue;
                groups[i].drawIdle (sb);
            }
    }

    /** The block at the left of the grid with the list of the rows. */
    Rect rowBlockRect() const noexcept { return { rect_.x + 8, rect_.y + kHeaderH + 4, kRowBlockW, kGridH }; }
    Rect stripRect() const noexcept { return { rect_.x + 8, grid.rect().bottom() + 6, rect_.w - 16, kStripH }; }
    Rect blockRect (int b) const noexcept
    {
        const int m = kBlockGap, y0 = stripRect().bottom() + 8, h = rect_.bottom() - 8 - y0, totalW = rect_.w - 2 * m;
        const int w0 = 520, w1 = 380, w2 = totalW - w0 - w1 - 2 * m;
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
        stepValue.place ({ rect_.right() - m - 200, hy, 200, 56 });   // the ROW left the header: LENGTH and VALUE have its room
        length.place ({ stepValue.rect().x - m - 180, hy, 180, 56 });
        qStepSelect.place ({ length.rect().x - m - Stepper::kMinW, hy, Stepper::kMinW, 56 });
        {   // the row list in a block of its own at the left of the grid (a lane above it for its title and the index chip), the grid at its right
            const Rect rb = rowBlockRect();
            rowList.place ({ rb.x + kBlockPad, rb.y + kBlockPad + lane, rb.w - 2 * kBlockPad, rb.h - 2 * kBlockPad - lane });
            grid.place ({ rb.right() + m, rb.y, rect_.right() - m - (rb.right() + m), kGridH });
        }

        {   // RANDOM: the four spreads as ONE row of large knobs (dial, name and value under it), 10x DETUNE as a LED toggle row across the block under them. The first row starts at the top of the block: the index chips sit in its title strip
            const Rect in = inner (0);
            const int th = 56, kh = std::max (72, in.h - lane - th - 4), kw = (in.w - 3 * g) / 4;
            Knob* ks[4] = { &randPan, &randDetune, &randCutoff, &randEnv };
            for (int i = 0; i < 4; ++i)
            {
                ks[i]->state.side = false; ks[i]->state.large = true;
                const int x = in.x + i * (kw + g);
                ks[i]->place ({ x, in.y, i == 3 ? in.right() - x : kw, kh });
            }
            randDetune10x.place ({ in.x, in.bottom() - th, 172, th });   // matches the standalone skin switch
        }
        {   // SCALING: the two time knobs, the mode (a LED list of two, side by side, with its title) under them
            const Rect in = inner (1);
            const int lh = LedList::kTitleH + led::kRowH, kw = (in.w - g) / 2, kh = std::max (72, in.h - lane - lh - 4);
            for (Knob* k : { &scaleEnv, &scaleLfo })
            {
                k->state.side = true; k->state.large = false;   // the medium size: the dial at the left, the name and the value beside it
            }
            scaleEnv.place ({ in.x, in.y, kw, kh });
            scaleLfo.place ({ in.x + kw + g, in.y, in.right() - (in.x + kw + g), kh });
            scaleLfoMode.place ({ in.x, in.bottom() - lh, in.w, lh });
        }
        {   // TARGETS: five LED toggles of the same width in one grid of two columns (OSC A, OSC B / OSC C, SUB / NOISE); a lane above the rows below the first
            const Rect in = inner (2);
            const int rows = 3, th = std::max (led::kRowH, (in.h - 2 * lane) / rows), cw = (in.w - g) / 2;
            LedToggle* ts[5] = { &oscA, &oscB, &oscC, &oscSub, &oscNoise };
            for (int i = 0; i < 5; ++i)
                ts[i]->place ({ in.x + (i % 2) * (cw + g), in.y + (i / 2) * (th + lane), cw, th });
        }
        auto nativePlace = [&] (Component& c, Rect r) { r.x += rect_.x; r.y += rect_.y; c.place (r); };
        nativePlace (length, voicelayout::kLength);
        nativePlace (randPan, voicelayout::kRandPan);
        nativePlace (randDetune, voicelayout::kRandDetune);
        nativePlace (randCutoff, voicelayout::kRandCutoff);
        nativePlace (randEnv, voicelayout::kRandEnv);
        nativePlace (randDetune10x, voicelayout::kRandDetune10x);
        nativePlace (scaleEnv, voicelayout::kScaleEnv);
        nativePlace (scaleLfo, voicelayout::kScaleLFO);
        nativePlace (scaleLfoMode, voicelayout::kScaleLFOMode);
        nativePlace (oscA, voicelayout::kOscA);
        nativePlace (oscB, voicelayout::kOscB);
        nativePlace (oscC, voicelayout::kOscC);
        nativePlace (oscSub, voicelayout::kOscSub);
        nativePlace (oscNoise, voicelayout::kOscNoise);
        nativePlace (rowList, voicelayout::kRowList);
        nativePlace (qStepSelect, voicelayout::kStepSelect);
        nativePlace (stepValue, voicelayout::kStepValue);
    }

    void drawUnder (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rowBlockRect(), false);
        if (! nativeControls_)
        {
            for (int b = 0; b < 3; ++b)
                p.block (blockRect (b), false);
        }
        // the strip with every value of the selected step
        const Rect sr = stripRect();
        p.control (sr, false, false);
        (void) T;
    }

    void drawExtra (Pen& p) const override
    {
        drawFixedLabels (p);
        drawLiveLabels (p);
    }

    void drawFixedLabels (Pen& p) const
    {
        const theme::Look& T = theme::L();
        static const char* const kBlockTitles[3] = { "RANDOM", "SCALING", "TARGETS" };
        for (int b = 0; b < 3; ++b)
        {
            const Rect r = blockRect (b);
            p.textLeft ({ r.x + kBlockPad + 6, r.y + 3, std::max (0, r.w - 20), 16 }, kBlockTitles[b], T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        }
    }

    void drawLiveLabels (Pen& p) const
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + 14, rect_.y + 4, std::max (0, qStepSelect.rect().x - rect_.x - 20), 56 }, "VOICE CONTROL", T.fTitle, p.c (T.text, 255), T.fMin);
        // the selected step: every row's value
        const Rect sr = stripRect();
        char t[40];
        std::snprintf (t, sizeof t, "STEP %d", grid.state.selectedStep + 1);
        p.textLeft ({ sr.x + 12, sr.y, 96, sr.h }, t, T.fLabel, p.c (T.q, 255), T.fMin);
        const int cw = (sr.w - 120) / VoiceGrid::kNumRows;
        for (int r = 0; r < VoiceGrid::kNumRows; ++r)
        {
            char v[24];
            VoiceGrid::formatValue (r, grid.state.values[r][std::clamp (grid.state.selectedStep, 0, VoiceGrid::kNumSteps - 1)], v, sizeof v);
            std::snprintf (t, sizeof t, "%s  %s", VoiceGrid::rowName (r), v);
            p.textLeft ({ sr.x + 112 + r * cw, sr.y, cw - 6, sr.h }, t, T.fLabel - 1.0f, p.c (r == grid.state.activeRow ? T.text : T.textDim, 255), T.fMin);
        }
    }

private:
    bool nativeSequence_ = false;
    int activeGroup_ = 0;

    void buildGroups()
    {
        groups[0].set (0, &qStepSelect);
        groups[0].set (1, &rowList);
        groups[0].set (2, &length);
        groups[0].set (3, &stepValue);
        groups[0].setFrameless (true);   // the members lie apart (the header and the list at the left of the grid): no frame around everything between them, the four chips and the Q colour mark the quad

        groups[1].set (0, &randPan);
        groups[1].set (1, &randDetune);
        groups[1].set (2, &randCutoff);
        groups[1].set (3, &randEnv);

        // 4 groups = the 4 banks of the Key 61 and the 16 knobs of the XL at once: [10x DETUNE, ENV TIME, LFO TIME, LFO MODE] (the RAND 10x switch and the SCALING block); NOISE (the 17th value) is touch only
        groups[2].set (0, &randDetune10x);
        groups[2].set (1, &scaleEnv);
        groups[2].set (2, &scaleLfo);
        groups[2].set (3, &scaleLfoMode);
        groups[2].setFrameless (true);   // its members lie in two blocks (the 10x switch of RANDOM, the knobs of SCALING): no frame around everything between them, the chips mark it

        groups[3].set (0, &oscA);
        groups[3].set (1, &oscB);
        groups[3].set (2, &oscC);
        groups[3].set (3, &oscSub);

        setActiveGroup (0);
    }
};

}} // namespace synthyboy_mpc::ui
