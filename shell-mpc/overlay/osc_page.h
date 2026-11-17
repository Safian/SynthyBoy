// SynthyBoy - MPC overlay: a page of the OSC tab, the element PageView (page_view.h) shows for the views Osc A, Osc B, Osc C, Sub and Noise (overlay_layout.json, kind "oscui"; the view's index is the
// oscillator selector's number: 1 Sub, 2 A, 3 B, 4 C, 5 Noise). It is the full screen of the oscillator UI (osc_ui.h: the same class the Play page opens with its full screen button, in its page mode) with
// the display of the oscillator (osc_display.h: the same class the Play view draws its picture with): the Basic group, the type bar, the tuning column, the knob panel with its Q groups, the detail
// pages (unison, grain window, envelope, modulations), the route popup, the lists and the content browser, and the live read-outs. The tab names the oscillator, so nothing here writes the Play selector.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "osc_display.h"
#include "osc_marks.h"
#include "osc_ui.h"
#include "page_view.h"

namespace synthyboy_mpc { namespace overlay {

class OscPageUi
{
public:
    static constexpr int kElementPage = OscUi::kElementOsc;

    /** The table indices a page of the oscillator `sel` (1 Sub .. 5 Noise) reads live: the frame, the two warp amounts and the morph amount of A, B or C; none for the Sub and the Noise. */
    static std::vector<int> targetsOf (int sel)
    {
        std::vector<int> out;
        const int osc = playosc::oscNumber (sel);
        if (osc >= 1)
        {
            int idx[4];
            synthyboy::OscCycle::readoutIndices (osc, idx);
            out.assign (idx, idx + 4);
        }
        return out;
    }

    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        if (in.instance != sel_)
        {
            sel_ = in.instance;
            osc_.setPage (sel_);
        }
        osc_.update (inst, 1, stack, nowMs, std::clamp (inst.playSelection (1), 1, 6), std::clamp (inst.playSelection (2), 1, 10));
        // the page lists the readout targets of its own oscillator only (4 values, targets ()); the display reads them where the Play view's twelve are
        const int osc = playosc::oscNumber (sel_);
        float t[OscDisplay::kOscs * 4];
        std::fill (t, t + OscDisplay::kOscs * 4, -1.0f);
        if (in.targets != nullptr && osc >= 1)
            for (int i = 0; i < 4; ++i)
                t[(osc - 1) * 4 + i] = in.targets[i];
        marks_ = OscMarks::of (inst, sel_, osc_.topPage());
        display_.update (inst, sel_, osc >= 1 && in.targets != nullptr ? t : nullptr, in.live, nowMs, counters_);
    }

    /** The picture: the page, the oscillator's display in its rectangle (the data under the live read-outs), the popups over it. */
    void draw (gpu::SceneBuilder& sb) const
    {
        osc_.drawPage (sb);
        const ui::Rect r = osc_.displayRect();
        if (! r.empty() && display_.valid())
        {
            display_.draw (sb, r, false);
            display_.draw (sb, r, true);
            if (marks_.any())
                marks_.draw (sb, r);
        }
        osc_.drawOnDisplay (sb);
        osc_.drawPopup (sb);
    }
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs) { return osc_.touch (ev, stack, inst, nowMs); }
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs) { return osc_.wheel (x, y, notches, stack, inst, nowMs); }
    void tick (double nowMs, UiStack& stack, IInstanceView& inst) { osc_.tick (nowMs, stack, inst); }
    bool fingerDown() const noexcept { return osc_.fingerDown(); }
    void dropGesture() noexcept { osc_.dropGesture(); }
    void forgetQGroup() noexcept { osc_.forgetQGroup(); }
    int topPage() const noexcept { return osc_.topPage(); }
    const int* qTargets() const noexcept { return osc_.qTargets(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }

    // ---- access for the tests
    OscUi& ui() noexcept { return osc_; }
    const OscUi& ui() const noexcept { return osc_; }
    const OscDisplay& display() const noexcept { return display_; }
    int selection() const noexcept { return sel_; }

private:
    OscUi osc_;
    OscDisplay display_;
    OscMarks marks_;
    ViewCounters counters_;
    int sel_ = 0;
};

}} // namespace synthyboy_mpc::overlay
