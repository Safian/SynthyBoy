// SynthyBoy - MPC UI: Modulation Slot Detail Panel
// Slide-in panel from the right over the matrix list (x=600..1280, w=680, h=628).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "base.h"
#include "composite.h"
#include "core.h"
#include "qgroup.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** Slot Details Slide-in Panel:
    AUX block: AUX SOURCE, AUX OUTPUT (scale), AUX INV, AUX POWER.
    SHAPE block: POWER, STEREO.
    TIME block: RISE, FALL, DELAY (ms / DELAY SYNC toggle: DELAY BEATS).
    Delete button with confirmation.
    Three Q-groups: AUX, SHAPE, TIME. */
class ModSlotDetail : public Composite
{
public:
    static constexpr int kBarH = 56;
    enum Block : uint8_t { BlockAux = 0, BlockShape, BlockTime };

    int slotIndex = 0; // 0 .. 63

    MenuButton back { "BACK", MenuButton::Icon::None };
    MenuButton deleteBtn { "DELETE", MenuButton::Icon::None };

    // Block selector buttons
    MenuButton tabAux { "AUX", MenuButton::Icon::None };
    MenuButton tabShape { "SHAPE", MenuButton::Icon::None };
    MenuButton tabTime { "TIME", MenuButton::Icon::None };

    // AUX Block
    MenuButton auxSource { "NONE", MenuButton::Icon::Down };
    Knob auxOutput { "SCALE" };
    MenuButton auxInv { "INV", MenuButton::Icon::None };
    Knob auxPower { "CURVE" };

    // SHAPE Block
    Knob power { "CURVE" };
    MenuButton stereo { "STEREO", MenuButton::Icon::None };

    // TIME Block
    Knob rise { "RISE" };
    Knob fall { "FALL" };
    Knob delay { "DELAY" };
    MenuButton delaySync { "SYNC", MenuButton::Icon::None };
    Knob delayBeats { "BEATS" };

    QGroup qgroup;

    ModSlotDetail()
    {
        addKid (back);
        addKid (deleteBtn);
        addKid (tabAux);
        addKid (tabShape);
        addKid (tabTime);

        // AUX block setup
        auxOutput.state.value = auxOutput.state.defaultValue = 0.5; // 100%
        auxPower.state.bipolar = true;
        auxPower.state.defaultValue = 0.5; // 0.0
        addKid (auxSource);
        addKid (auxOutput);
        addKid (auxInv);
        addKid (auxPower);

        // SHAPE block setup
        power.state.bipolar = true;
        power.state.defaultValue = 0.5; // 0.0
        addKid (power);
        addKid (stereo);

        // TIME block setup
        rise.state.defaultValue = 0.0;
        fall.state.defaultValue = 0.0;
        delay.state.defaultValue = 0.0;
        delayBeats.state.defaultValue = 0.0;
        addKid (rise);
        addKid (fall);
        addKid (delay);
        addKid (delaySync);
        addKid (delayBeats);

        setActiveBlock (BlockAux);
    }

    Size minSize() const override { return { 500, 400 }; }

    void setSlot (int idx, const char* srcName, const char* dstName)
    {
        if (slotIndex != idx)
        {
            slotIndex = idx;
            confirmDelete_ = false;
            deleteBtn.setLabel ("DELETE");
        }
        std::snprintf (title_, sizeof title_, "SLOT %02d: %s -> %s", idx + 1,
                       srcName && srcName[0] ? srcName : "---",
                       dstName && dstName[0] ? dstName : "---");
    }

    void setActiveBlock (Block b)
    {
        activeBlock_ = b;
        updateQGroup();
    }

    Block activeBlock() const noexcept { return activeBlock_; }

    void updateQGroup()
    {
        for (int k = 0; k < 4; ++k)
            qgroup.set (k, nullptr);

        if (activeBlock_ == BlockAux)
        {
            qgroup.set (0, &auxSource);
            qgroup.set (1, &auxOutput);
            qgroup.set (2, &auxInv);
            qgroup.set (3, &auxPower);
        }
        else if (activeBlock_ == BlockShape)
        {
            qgroup.set (0, &power);
            qgroup.set (1, &stereo);
        }
        else if (activeBlock_ == BlockTime)
        {
            qgroup.set (0, &rise);
            qgroup.set (1, &fall);
            qgroup.set (2, delaySyncOn_ ? &delayBeats : &delay);
            qgroup.set (3, &delaySync);
        }
        qgroup.setActive (true);
    }

    void setAuxData (const char* src, double output, bool inv, double curve)
    {
        auxSource.setLabel (src && src[0] ? src : "NONE");
        auxOutput.state.value = std::clamp (output / 2.0, 0.0, 1.0); // 0 .. 200%
        std::snprintf (auxOutput.state.valueText, sizeof auxOutput.state.valueText, "%d%%", static_cast<int> (std::lround (output * 100.0)));
        auxInv.setLabel (inv ? "INV ON" : "INV OFF");
        auxInvOn_ = inv;
        auxPower.state.value = std::clamp ((curve + 10.0) / 20.0, 0.0, 1.0);
        std::snprintf (auxPower.state.valueText, sizeof auxPower.state.valueText, "%.1f", curve);
    }

    void setShapeData (double crv, bool st)
    {
        power.state.value = std::clamp ((crv + 10.0) / 20.0, 0.0, 1.0);
        std::snprintf (power.state.valueText, sizeof power.state.valueText, "%.1f", crv);
        stereo.setLabel (st ? "STEREO ON" : "STEREO OFF");
        stereoOn_ = st;
    }

    void setTimeData (double rMs, double fMs, double dMs, bool sync, double beats)
    {
        rise.state.value = std::clamp (rMs / 5000.0, 0.0, 1.0);
        std::snprintf (rise.state.valueText, sizeof rise.state.valueText, "%d ms", static_cast<int> (std::lround (rMs)));

        fall.state.value = std::clamp (fMs / 5000.0, 0.0, 1.0);
        std::snprintf (fall.state.valueText, sizeof fall.state.valueText, "%d ms", static_cast<int> (std::lround (fMs)));

        delay.state.value = std::clamp (dMs / 4000.0, 0.0, 1.0);
        std::snprintf (delay.state.valueText, sizeof delay.state.valueText, "%d ms", static_cast<int> (std::lround (dMs)));

        delaySyncOn_ = sync;
        delaySync.setLabel (sync ? "SYNC: BEAT" : "SYNC: MS");
        show (delay, ! sync);
        show (delayBeats, sync);

        delayBeats.state.value = std::clamp (beats / 8.0, 0.0, 1.0);
        std::snprintf (delayBeats.state.valueText, sizeof delayBeats.state.valueText, "%.2f", beats);
        updateQGroup();
        layout();
    }

    bool isDeleteConfirmed() const noexcept { return confirmDelete_; }

    Change touchDown (int x, int y, double nowMs) override
    {
        return Composite::touchDown (x, y, nowMs);
    }

    Change touchUp (int x, int y) override
    {
        const bool onDelete = (cap_ == &deleteBtn);
        const bool onTabAux = (cap_ == &tabAux);
        const bool onTabShape = (cap_ == &tabShape);
        const bool onTabTime = (cap_ == &tabTime);

        const Change c = Composite::touchUp (x, y);

        if (onTabAux) { setActiveBlock (BlockAux); return Change::none(); }
        if (onTabShape) { setActiveBlock (BlockShape); return Change::none(); }
        if (onTabTime) { setActiveBlock (BlockTime); return Change::none(); }

        if (onDelete && c.kind == ChangeKind::Request)
        {
            if (! confirmDelete_)
            {
                confirmDelete_ = true;
                deleteBtn.setLabel ("CONFIRM?");
                return Change::none();
            }
            else
            {
                confirmDelete_ = false;
                deleteBtn.setLabel ("DELETE");
                return Change::request (deleteBtn.id());
            }
        }
        else if (confirmDelete_ && ! onDelete)
        {
            confirmDelete_ = false;
            deleteBtn.setLabel ("DELETE");
        }

        return c;
    }

    void draw (gpu::SceneBuilder& sb) const override
    {
        // Solid panel backdrop
        Pen p (sb, true);
        const theme::Look& T = theme::L();
        p.rect (rect_, colour::rgba (T.bg1, 250));
        p.line (static_cast<float> (rect_.x), static_cast<float> (rect_.y),
                static_cast<float> (rect_.x), static_cast<float> (rect_.bottom()),
                2.0f, p.c (T.keylineHi, 255));

        Composite::draw (sb);
        qgroup.draw (sb);
    }

    Change turnQ (int knobIndex, double delta)
    {
        return qgroup.turn (knobIndex, delta);
    }

protected:
    void layout() override
    {
        // Header
        back.place ({ rect_.x + 12, rect_.y + 4, 90, kBarH - 8 });
        tabAux.place ({ rect_.x + 360, rect_.y + 4, 60, kBarH - 8 });
        tabShape.place ({ rect_.x + 426, rect_.y + 4, 76, kBarH - 8 });
        tabTime.place ({ rect_.x + 508, rect_.y + 4, 64, kBarH - 8 });
        deleteBtn.place ({ rect_.right() - 100, rect_.y + 4, 90, kBarH - 8 });

        const int x0 = rect_.x + 24;

        // AUX block (y = rect_.y + 64, h = 160)
        int y = rect_.y + 64;
        auxSource.place ({ x0, y + 26, 170, 56 });
        auxOutput.place ({ x0 + 190, y + 20, 110, 120 });
        auxInv.place ({ x0 + 320, y + 26, 80, 56 });
        auxPower.place ({ x0 + 420, y + 20, 110, 120 });

        // SHAPE block (y = rect_.y + 244, h = 150)
        y = rect_.y + 244;
        power.place ({ x0, y + 20, 120, 120 });
        stereo.place ({ x0 + 150, y + 26, 110, 56 });

        // TIME block (y = rect_.y + 414, h = 180)
        y = rect_.y + 414;
        rise.place ({ x0, y + 20, 100, 130 });
        fall.place ({ x0 + 110, y + 20, 100, 130 });
        delay.place ({ x0 + 220, y + 20, 100, 130 });
        delayBeats.place ({ x0 + 220, y + 20, 100, 130 });
        delaySync.place ({ x0 + 330, y + 26, 96, 56 });
    }

    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();

        // Title in header
        p.textLeft ({ rect_.x + 112, rect_.y + 4, 240, kBarH - 8 }, title_, T.fLabel, p.c (T.text, 255), T.fMin);

        // Header separator
        p.line (static_cast<float> (rect_.x + 8), static_cast<float> (rect_.y + kBarH - 1),
                static_cast<float> (rect_.right() - 8), static_cast<float> (rect_.y + kBarH - 1),
                1.5f, p.c (T.keyline, 255));

        // Highlight active block tab
        const MenuButton* tab = (activeBlock_ == BlockAux ? &tabAux : (activeBlock_ == BlockShape ? &tabShape : &tabTime));
        p.rrect (tab->rect(), T.rControl, p.c (T.flow[0], 255), 1.6f);

        // Block Titles and frames
        const int x0 = rect_.x + 16;
        const int w = rect_.w - 32;

        // AUX block outline
        p.block ({ x0, rect_.y + 60, w, 160 });
        p.textLeft ({ x0 + 12, rect_.y + 64, 200, 20 }, "AUXILIARY SOURCE", T.fMin + 2.0f, p.c (T.textDim, 255), T.fMin);

        // SHAPE block outline
        p.block ({ x0, rect_.y + 236, w, 150 });
        p.textLeft ({ x0 + 12, rect_.y + 240, 200, 20 }, "SHAPE & STEREO", T.fMin + 2.0f, p.c (T.textDim, 255), T.fMin);

        // TIME block outline
        p.block ({ x0, rect_.y + 400, w, 180 });
        p.textLeft ({ x0 + 12, rect_.y + 404, 200, 20 }, "TIME & SLEW", T.fMin + 2.0f, p.c (T.textDim, 255), T.fMin);
    }

private:
    char title_[64] = {};
    Block activeBlock_ = BlockAux;
    bool confirmDelete_ = false;
    bool auxInvOn_ = false;
    bool stereoOn_ = false;
    bool delaySyncOn_ = false;
};

}} // namespace synthyboy_mpc::ui
