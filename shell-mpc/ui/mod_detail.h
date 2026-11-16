// SynthyBoy - MPC UI: the page of one modulation slot (the details of a row of the matrix), 1280 x 628.
// From the top: BACK, the title, DELETE; the slot's own row (SOURCE -> TARGET, AMOUNT, UNI / BI, ON / OFF: the same controls as in the matrix, so the whole slot is edited here); three blocks side by side,
// ONE Q group each: AUXILIARY SOURCE (the second source that multiplies the first: SOURCE, SCALE, INVERT, CURVE), SHAPE (CURVE, STEREO) and TIME (RISE, FALL, DELAY or BEATS, SYNC). A tap on a member makes its
// block the Q group. Every knob shows its value; the blocks are laid out over their whole height.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "led_toggle.h"
#include "mod_matrix.h"
#include "qgroup.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
class ModSlotDetail : public Composite
{
public:
    static constexpr int kBarH = 64, kBlockTitleH = 34, kBlockPad = 6, kBlockGap = 8;
    enum Block : uint8_t { BlockAux = 0, BlockShape, BlockTime };

    int slotIndex = 0; // 0 .. 63

    MenuButton back { "BACK", MenuButton::Icon::None };
    MenuButton deleteBtn { "DELETE", MenuButton::Icon::None };
    ModRow summary;   // the slot's own row (the MORE button is not used here)

    // AUX block
    MenuButton auxSource { "SOURCE", MenuButton::Icon::Down };
    Knob auxOutput { "SCALE" };
    LedToggle auxInv;
    Knob auxPower { "CURVE" };

    // SHAPE block
    Knob power { "CURVE" };
    LedToggle stereo;

    // TIME block
    Knob rise { "RISE" };
    Knob fall { "FALL" };
    Knob delay { "DELAY" };
    LedToggle delaySync;
    Knob delayBeats { "BEATS" };

    QGroup groups[3];

    ModSlotDetail()
    {
        addKid (back);
        addKid (deleteBtn);
        addKid (summary);
        summary.showDetailButton (false);

        auxOutput.state.defaultValue = 0.5;   // 100 %
        auxPower.state.bipolar = true;
        auxPower.state.defaultValue = 0.5;
        auxInv.setLabel ("INVERT");
        addKid (auxSource);
        addKid (auxOutput);
        addKid (auxInv);
        addKid (auxPower);

        power.state.bipolar = true;
        power.state.defaultValue = 0.5;
        stereo.setLabel ("STEREO");
        addKid (power);
        addKid (stereo);

        delaySync.setLabel ("SYNC");
        addKid (rise);
        addKid (fall);
        addKid (delay);
        addKid (delayBeats, false);
        addKid (delaySync);

        for (Knob* k : { &auxOutput, &auxPower, &power, &rise, &fall, &delay, &delayBeats })
            k->state.large = true;
        auxSource.state.label[0] = '\0';
        setText (auxSource.state.label, sizeof auxSource.state.label, "SOURCE");
        buildGroups();
        setActiveBlock (BlockAux);
    }

    Size minSize() const override { return { 1280, 628 }; }

    void setTitle (int idx, const char* srcName, const char* dstName)
    {
        if (slotIndex != idx)
        {
            slotIndex = idx;
            confirmDelete_ = false;
            deleteBtn.setLabel ("DELETE");
        }
        std::snprintf (title_, sizeof title_, "SLOT %02d   %s  ->  %s", idx + 1, srcName && srcName[0] ? srcName : "---", dstName && dstName[0] ? dstName : "---");
    }

    /** The DELAY is in milliseconds or in beats (the SYNC switch): the member of the TIME group follows it. */
    void setDelaySync (bool sync)
    {
        if (sync == delaySyncOn_)
            return;
        delaySyncOn_ = sync;
        show (delay, ! sync);
        show (delayBeats, sync);
        groups[BlockTime].set (2, sync ? &delayBeats : &delay);
        if (activeBlock_ == BlockTime)
            groups[BlockTime].setActive (true);
    }
    bool delaySyncOn() const noexcept { return delaySyncOn_; }

    void setActiveBlock (Block b)
    {
        activeBlock_ = b;
        for (int k = 0; k < 3; ++k)
            groups[k].setActive (k == b);
    }
    Block activeBlock() const noexcept { return activeBlock_; }
    static const char* blockName (int b) noexcept { static const char* const n[3] = { "AUXILIARY SOURCE", "SHAPE", "TIME" }; return b >= 0 && b < 3 ? n[b] : ""; }
    Rect block (int b) const noexcept { return b >= 0 && b < 3 ? blocks_[b] : Rect {}; }

    bool isDeleteConfirmed() const noexcept { return confirmDelete_; }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)
            for (int b = 0; b < 3; ++b)
                if (blocks_[b].contains (x, y))
                {
                    setActiveBlock (static_cast<Block> (b));
                    break;
                }
        return c;
    }

    Change touchUp (int x, int y) override
    {
        const bool onDelete = (cap_ == &deleteBtn);
        const Change c = Composite::touchUp (x, y);
        if (onDelete && c.kind == ChangeKind::Request)
        {
            if (! confirmDelete_)
            {
                confirmDelete_ = true;
                deleteBtn.setLabel ("CONFIRM?");
                return Change::none();
            }
            confirmDelete_ = false;
            deleteBtn.setLabel ("DELETE");
            return Change::request (deleteBtn.id());
        }
        if (confirmDelete_ && ! onDelete)
        {
            confirmDelete_ = false;
            deleteBtn.setLabel ("DELETE");
        }
        return c;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        for (const QGroup& g : groups)
            g.draw (sb);
        for (int k = 0; k < 3; ++k)
            if (k != activeBlock_)
                groups[k].drawMapped (sb);
    }

    Change turnQ (int knobIndex, double delta) { return groups[activeBlock_].turn (knobIndex, delta); }

protected:
    void buildGroups()
    {
        groups[BlockAux].set (0, &auxSource);
        groups[BlockAux].set (1, &auxOutput);
        groups[BlockAux].set (2, &auxInv);
        groups[BlockAux].set (3, &auxPower);
        groups[BlockShape].set (0, &power);
        groups[BlockShape].set (1, &stereo);
        groups[BlockTime].set (0, &rise);
        groups[BlockTime].set (1, &fall);
        groups[BlockTime].set (2, &delay);
        groups[BlockTime].set (3, &delaySync);
    }

    void layout() override
    {
        const int m = 8;
        // the bar: BACK, the title, DELETE
        back.place ({ rect_.x + m, rect_.y + 4, 100, 56 });
        deleteBtn.place ({ rect_.right() - m - 120, rect_.y + 4, 120, 56 });
        // the slot's own row
        summary.place ({ rect_.x, rect_.y + kBarH, rect_.w, ModRow::kRowH });

        // the three blocks over the rest of the height
        const int y0 = rect_.y + kBarH + ModRow::kRowH + 6, h = rect_.bottom() - m - y0;
        const int avail = rect_.w - 2 * m - 2 * kBlockGap;
        const int wAux = avail * 36 / 100, wShape = avail * 26 / 100, wTime = avail - wAux - wShape;
        blocks_[BlockAux] = { rect_.x + m, y0, wAux, h };
        blocks_[BlockShape] = { blocks_[BlockAux].right() + kBlockGap, y0, wShape, h };
        blocks_[BlockTime] = { blocks_[BlockShape].right() + kBlockGap, y0, wTime, h };
        auto inner = [&] (const Rect& b) { return Rect { b.x + kBlockPad, b.y + kBlockTitleH, std::max (0, b.w - 2 * kBlockPad), std::max (0, b.h - kBlockTitleH - kBlockPad) }; };
        // a 2 x 2 grid: the cells of the second row start a lane lower (the Q index chips of the members lie above them)
        auto grid = [&] (const Rect& b, Component* c0, Component* c1, Component* c2, Component* c3)
        {
            const Rect in = inner (b);
            const int cw = (in.w - kGap) / 2, rh = (in.h - kQLane - kGap) / 2;
            Component* cs[4] = { c0, c1, c2, c3 };
            for (int i = 0; i < 4; ++i)
            {
                const int col = i % 2, row = i / 2;
                Rect cell { in.x + col * (cw + kGap), in.y + row * (rh + kGap + kQLane), col == 1 ? in.right() - (in.x + cw + kGap) : cw, rh };
                if (cs[i] == &auxSource)
                    cell = { cell.x, cell.y + (cell.h - std::min (cell.h, 110)) / 2, cell.w, std::min (cell.h, 110) };
                else if (cs[i] == &auxInv || cs[i] == &delaySync)   // a LED toggle: one row of 56 px, in the middle of its cell
                    cell = { cell.x, cell.y + (cell.h - std::min (cell.h, led::kRowH)) / 2, cell.w, std::min (cell.h, led::kRowH) };
                cs[i]->place (cell);
            }
        };
        grid (blocks_[BlockAux], &auxSource, &auxOutput, &auxInv, &auxPower);
        {
            const Rect in = inner (blocks_[BlockShape]);
            const int sh = led::kRowH, rh = in.h - kQLane - kGap - sh;   // STEREO is one LED row; the room it leaves goes to the CURVE knob
            power.place ({ in.x, in.y, in.w, rh });
            stereo.place ({ in.x, in.y + rh + kGap + kQLane, in.w, sh });
        }
        grid (blocks_[BlockTime], &rise, &fall, &delay, &delaySync);
        delayBeats.place (delay.rect());
    }

    void drawUnder (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        for (int b = 0; b < 3; ++b)
            p.block (blocks_[b]);
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + kBarH - 2), static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.y + kBarH - 2), 1.5f, p.c (T.keyline, 255));
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + 124, rect_.y + 4, rect_.w - 124 - 140, 56 }, title_, T.fTitle - 4.0f, p.c (T.text, 255), T.fMin);
        for (int b = 0; b < 3; ++b)
            p.textLeft ({ blocks_[b].x + kBlockPad + 4, blocks_[b].y + 3, std::max (0, blocks_[b].w - 2 * kBlockPad - 4), 16 }, blockName (b), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
    }

private:
    char title_[96] = {};
    Rect blocks_[3];
    Block activeBlock_ = BlockAux;
    bool confirmDelete_ = false;
    bool delaySyncOn_ = false;
};

}} // namespace synthyboy_mpc::ui
