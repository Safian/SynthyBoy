// SynthyBoy Voice tab. Native controls own all input and paint. The overlay
// keeps parameter geometry for Q-Link routing and draws the live indicators.
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
    static constexpr int kMaxGroups = 4;   // geometry: scripts/native_voice/voice_layout.json through voice_native_layout.h (redesign 2026-11-26)

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

        addKid (grid);
        buildGroups();
    }

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
        if (grid.rect().contains (x, y) || rowList.rect().contains (x, y)
            || qStepSelect.rect().contains (x, y) || stepValue.rect().contains (x, y))
            return true;
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

    Change turnQ (int slot, double delta)
    {
        return enabled_ ? groups[activeGroup_].turn (slot, delta) : Change::none();
    }

    Change touchDown (int x, int y, double) override
    {
        if (!enabled_ || !rect_.contains (x, y) || isNativeControl (x, y))
            return Change::none();
        if (sequenceRect().contains (x, y))
            setActiveGroup (0);
        else if (blockRect (0).contains (x, y))
            setActiveGroup (y >= randDetune10x.rect().y - 4 ? 2 : 1);   // the 10x switch under the knobs is the first member of SCALING's group
        else if (blockRect (1).contains (x, y))
            setActiveGroup (2);
        else if (blockRect (2).contains (x, y))
            setActiveGroup (3);
        return Change::none();
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        drawChildren (sb);
        groups[activeGroup_].draw (sb);
    }

    /** A rectangle of voice_native_layout.h (tab coordinates) on the page. */
    Rect at (Rect r) const noexcept { return { rect_.x + r.x, rect_.y + r.y, r.w, r.h }; }
    /** The SEQUENCE panel (the list of the rows, the step grid, the strip of the selected step). */
    Rect sequenceRect() const noexcept { return at (voicelayout::kBlockSequence); }
    /** The part of the SEQUENCE panel at the left of the grid, with the list of the rows. */
    Rect rowBlockRect() const noexcept
    {
        const Rect s = sequenceRect();
        return { s.x, s.y, grid.rect().x - s.x, stripRect().y - s.y };
    }
    Rect stripRect() const noexcept { return at (voicelayout::kStrip); }
    /** The titled panels under the sequence: 0 RANDOM, 1 SCALING, 2 TARGETS (what the skin draws). */
    Rect blockRect (int b) const noexcept
    {
        return at (b == 0 ? voicelayout::kBlockRandom : b == 1 ? voicelayout::kBlockScaling : voicelayout::kBlockTargets);
    }
    Rect inner (int b) const noexcept
    {
        const Rect r = blockRect (b);
        constexpr int title = 32, pad = 8;   // the title strip of the native panel, its inner margin
        return { r.x + pad, r.y + title, std::max (0, r.w - 2 * pad), std::max (0, r.h - title - pad) };
    }

protected:
    void layout() override
    {
        auto nativePlace = [&] (Component& c, Rect r) { r.x += rect_.x; r.y += rect_.y; c.place (r); };
        nativePlace (grid, voicelayout::kSequenceGrid);
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

private:
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
