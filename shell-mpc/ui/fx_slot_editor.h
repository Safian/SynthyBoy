// SynthyBoy - MPC UI component library: the FX SLOT EDITOR component (FxSlotEditor).
// Renders the parameters of the selected effect slot dynamically from the engine's FX type descriptors
// (fxTypeDescriptor), large knobs with value readouts, switches, choices, and the type's visual display.
// Q-Groups: parameters mapped 4 per row, Q-bank pages through the rows.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <vector>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "fx_display.h"
#include "qgroup.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace ui {

class FxSlotEditor : public Composite
{
public:
    static constexpr int kMaxParams = synthyboy::kFxInstanceParams; // 24
    static constexpr int kMaxRows = 6;                              // 6 rows of 4 = 24 params

    FxDisplay display;
    Knob knobs[kMaxParams];
    QGroup groups[kMaxRows];

    FxSlotEditor()
    {
        addKid (display);
        for (int i = 0; i < kMaxParams; ++i)
        {
            knobs[i].state.bipolar = false;
            knobs[i].state.small = false;
            addKid (knobs[i]);
        }
        setupGroups();
        setType (synthyboy::kFxTypeChorus);
        setActiveRow (0);
    }

    Size minSize() const override { return { 500, 360 }; }

    int currentType() const noexcept { return currentType_; }

    void setType (int type)
    {
        currentType_ = std::clamp (type, 0, static_cast<int> (synthyboy::kNumFxInstanceTypes - 1));
        const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (currentType_);
        display.setKindFromType (currentType_);

        numParams_ = desc.count;
        for (int i = 0; i < kMaxParams; ++i)
        {
            if (i < desc.count)
            {
                const int srcIdx = desc.source[i];
                if (srcIdx >= 0)
                {
                    const synthyboy::ParamInfo& pi = synthyboy::paramTable()[srcIdx];
                    knobs[i].setLabel (pi.name);
                    knobs[i].state.bipolar = (pi.min < 0.0f && pi.max > 0.0f);
                    knobs[i].setEnabled (true);
                    show (knobs[i], true);
                }
                else
                {
                    knobs[i].setLabel ("-");
                    knobs[i].setEnabled (false);
                    show (knobs[i], false);
                }
            }
            else
            {
                knobs[i].setLabel ("-");
                knobs[i].setEnabled (false);
                show (knobs[i], false);
            }
        }
        numActiveRows_ = std::max (1, (numParams_ + 3) / 4);
        setActiveRow (std::min (activeRow_, numActiveRows_ - 1));
    }

    int activeRow() const noexcept { return activeRow_; }
    void setActiveRow (int r)
    {
        if (r < 0 || r >= kMaxRows)
            return;
        activeRow_ = r;
        for (int i = 0; i < kMaxRows; ++i)
            groups[i].setActive (i == activeRow_);
    }

    int numActiveRows() const noexcept { return numActiveRows_; }

    void assignIds (int baseId)
    {
        for (int i = 0; i < kMaxParams; ++i)
            knobs[i].setId (baseId + i);
    }

    Change touchDown (int x, int y, double nowMs) override
    {
        // Touch on any knob row activates its Q-Group row
        for (int r = 0; r < numActiveRows_; ++r)
        {
            for (int col = 0; col < 4; ++col)
            {
                const int idx = r * 4 + col;
                if (idx < numParams_ && knobs[idx].rect().contains (x, y))
                {
                    if (activeRow_ != r)
                        setActiveRow (r);
                    break;
                }
            }
        }
        return Composite::touchDown (x, y, nowMs);
    }

    Change turnQ (int knobIndex, double delta)
    {
        return groups[activeRow_].turn (knobIndex, delta);
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        if (activeRow_ >= 0 && activeRow_ < numActiveRows_)
            groups[activeRow_].draw (sb);
    }

protected:
    void layout() override
    {
        const int pad = 6;
        const int w = rect_.w - 2 * pad;

        // Top section: FxDisplay across the top
        const int dispH = 150;
        display.place ({ rect_.x + pad, rect_.y + pad, w, dispH });

        // Bottom section: Parameter knobs in rows of 4
        const int topY = rect_.y + pad + dispH + 12;
        const int remainingH = rect_.bottom() - topY - pad;
        const int rows = std::max (1, numActiveRows_);
        const int rowH = std::min (92, remainingH / rows);
        const int gapX = 8;
        const int knobW = (w - 3 * gapX) / 4;

        for (int r = 0; r < rows; ++r)
        {
            const int ry = topY + r * rowH;
            for (int col = 0; col < 4; ++col)
            {
                const int idx = r * 4 + col;
                const int kx = rect_.x + pad + col * (knobW + gapX);
                knobs[idx].place ({ kx, ry, knobW, rowH - 4 });
            }
        }
    }

    void drawUnder (Pen& p) const override
    {
        p.block (rect_, false);
    }

private:
    int currentType_ = synthyboy::kFxTypeChorus;
    int numParams_ = 0;
    int numActiveRows_ = 1;
    int activeRow_ = 0;

    void setupGroups()
    {
        for (int r = 0; r < kMaxRows; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                const int idx = r * 4 + c;
                groups[r].set (c, &knobs[idx]);
            }
        }
    }
};

}} // namespace synthyboy_mpc::ui
