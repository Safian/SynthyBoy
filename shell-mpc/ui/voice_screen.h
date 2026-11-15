// SynthyBoy - MPC UI component library: VoiceScreen, the full-screen composite of the Voice view (docs/MPC.md, ui-elem-mastervoice.md).
// VoiceGrid step sequencer, Scale block, Random block, Osc targets block, Q-Link groups.
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
    static constexpr int kMaxGroups = 4;

    // Header
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    NumberField length;       // voicectl.length (1..8)
    Stepper qStepSelect;      // Q-Link step selector (1..8)
    Stepper qRowSelect;       // Q-Link row selector (PAN, DETUNE, CUTOFF, ENV, MOD1, MOD2)

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

    // Block 2: OSC TARGETS (voicectl.osc.*)
    ToggleIcon oscA;
    ToggleIcon oscB;
    ToggleIcon oscC;
    ToggleIcon oscSub;
    ToggleIcon oscNoise;

    // QGroups
    QGroup groups[kMaxGroups];

    VoiceScreen()
    {
        length.state.format = NumberField::Format::IntPlain;
        length.setLabel ("LENGTH");

        qStepSelect.setItem ("STEP 1");
        qRowSelect.setItem ("PAN");

        randPan.state.bipolar = false;
        randDetune.state.bipolar = false;
        randCutoff.state.bipolar = false;
        randEnv.state.bipolar = false;
        randDetune10x.setLabel ("10x DETUNE");

        scaleEnv.state.bipolar = false;
        scaleLfo.state.bipolar = false;

        oscA.setLabel ("OSC A");
        oscB.setLabel ("OSC B");
        oscC.setLabel ("OSC C");
        oscSub.setLabel ("SUB");
        oscNoise.setLabel ("NOISE");

        // Add children to composite
        addKid (close);
        addKid (length);
        addKid (qStepSelect);
        addKid (qRowSelect);
        addKid (grid);
        addKid (randPan); addKid (randDetune); addKid (randCutoff); addKid (randEnv); addKid (randDetune10x);
        addKid (scaleEnv); addKid (scaleLfo); addKid (scaleLfoMode);
        addKid (oscA); addKid (oscB); addKid (oscC); addKid (oscSub); addKid (oscNoise);

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
        static const char* const kNames[kMaxGroups] = { "Step", "Random", "Scaling", "Targets" };
        return g >= 0 && g < kMaxGroups ? kNames[g] : "";
    }

    Change turnQ (int slot, double delta)
    {
        if (activeGroup_ == 0 && slot == 0)
            return grid.turn (delta);
        return groups[activeGroup_].turn (slot, delta);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        // Switch Q group based on touched region
        if (grid.rect().contains (x, y) || qStepSelect.rect().contains (x, y) || qRowSelect.rect().contains (x, y) || length.rect().contains (x, y))
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
        Pen p (sb, enabled_);
        Cell cell (sb, rect_);
        const theme::Look& T = theme::L();

        // Header label
        Rect hdr { rect_.x + 104, rect_.y + 8, 240, 56 };
        p.textLeft (hdr, "VOICE CONTROL", 24.0f, p.c (T.text, 255));

        // Draw 3 bottom block panels
        for (int i = 0; i < 3; ++i)
        {
            const Rect br = blockRect (i);
            const bool isActive = (i + 1 == activeGroup_);
            p.block (br, isActive);
            // Title header
            Rect titleBox { br.x + 8, br.y + 4, br.w - 16, 20 };
            p.textLeft (titleBox, groupName (i + 1), 12.0f, p.c (isActive ? T.q : T.textDim, 255));
        }

        // Draw children
        Composite::draw (sb);

        // Draw active QGroup frame
        groups[activeGroup_].draw (sb);
    }

    Rect blockRect (int b) const noexcept
    {
        const int m = 8;
        const int gridBottom = rect_.y + 70 + 310;
        const int inY = gridBottom + m;
        const int totalH = rect_.bottom() - m - inY;
        const int totalW = rect_.w - 2 * m;

        // Block 0: RANDOM (w = 460), Block 1: SCALING (w = 380), Block 2: OSC TARGETS (remainder)
        const int w0 = 460;
        const int w1 = 380;
        const int w2 = totalW - w0 - w1 - 2 * m;

        if (b == 0) return { rect_.x + m, inY, w0, totalH };
        if (b == 1) return { rect_.x + m + w0 + m, inY, w1, totalH };
        return { rect_.x + m + w0 + m + w1 + m, inY, w2, totalH };
    }

protected:
    void layout() override
    {
        const int m = 8;

        // Top bar
        close.place ({ rect_.x + m, rect_.y + m, 84, 56 });
        length.place ({ rect_.right() - m - 120, rect_.y + m, 120, 56 });
        qStepSelect.place ({ length.rect().x - m - 110, rect_.y + m, 110, 56 });
        qRowSelect.place ({ qStepSelect.rect().x - m - 130, rect_.y + m, 130, 56 });

        // Step Grid
        const int gridY = rect_.y + 70;
        const int gridH = 310;
        grid.place ({ rect_.x + m, gridY, rect_.w - 2 * m, gridH });

        // Block 0: RANDOM (voicectl.rand.*)
        {
            const Rect br = blockRect (0);
            const int inY = br.y + 26;
            const int kw = (br.w - 5 * m) / 4;
            randPan.place ({ br.x + m, inY + 8, kw, 116 });
            randDetune.place ({ br.x + m + kw + m, inY + 8, kw, 116 });
            randCutoff.place ({ br.x + m + 2 * (kw + m), inY + 8, kw, 116 });
            randEnv.place ({ br.x + m + 3 * (kw + m), inY + 8, kw, 116 });
            randDetune10x.place ({ br.x + m, inY + 128, 140, 56 });
        }

        // Block 1: SCALING (voicectl.scale.*)
        {
            const Rect br = blockRect (1);
            const int inY = br.y + 26;
            scaleEnv.place ({ br.x + m, inY + 8, 116, 116 });
            scaleLfo.place ({ br.x + m + 120, inY + 8, 116, 116 });
            scaleLfoMode.place ({ br.x + m + 240, inY + 36, 120, 56 });
        }

        // Block 2: OSC TARGETS (voicectl.osc.*)
        {
            const Rect br = blockRect (2);
            const int inY = br.y + 26;
            const int tw = (br.w - 6 * m) / 5;
            oscA.place ({ br.x + m, inY + 36, tw, 56 });
            oscB.place ({ br.x + m + tw + m, inY + 36, tw, 56 });
            oscC.place ({ br.x + m + 2 * (tw + m), inY + 36, tw, 56 });
            oscSub.place ({ br.x + m + 3 * (tw + m), inY + 36, tw, 56 });
            oscNoise.place ({ br.x + m + 4 * (tw + m), inY + 36, tw, 56 });
        }
    }

private:
    int activeGroup_ = 0;

    void buildGroups()
    {
        // Group 0: Step [Selected Step Value, Step select, Length, Row select]
        groups[0].set (0, &qStepSelect);
        groups[0].set (1, &qRowSelect);
        groups[0].set (2, &length);
        groups[0].set (3, nullptr);

        // Group 1: Random [Rand Pan, Rand Detune, Rand Cutoff, Rand Env]
        groups[1].set (0, &randPan);
        groups[1].set (1, &randDetune);
        groups[1].set (2, &randCutoff);
        groups[1].set (3, &randEnv);

        // Group 2: Scaling [Scale Env, Scale Lfo, Scale Mode, Detune 10x]
        groups[2].set (0, &scaleEnv);
        groups[2].set (1, &scaleLfo);
        groups[2].set (2, &scaleLfoMode);
        groups[2].set (3, &randDetune10x);

        // Group 3: Targets [Osc A, Osc B, Osc C, Osc Sub]
        groups[3].set (0, &oscA);
        groups[3].set (1, &oscB);
        groups[3].set (2, &oscC);
        groups[3].set (3, &oscSub);

        setActiveGroup (0);
    }
};

}} // namespace synthyboy_mpc::ui
