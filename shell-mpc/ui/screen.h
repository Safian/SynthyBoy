// SynthyBoy - MPC UI component library: the two views of an oscillator INSTANCE (owner approved; drawn for 1280 x 800, other sizes scale in proportion). The synth has five instances: Sub, A, B,
// C and Noise; Sub and Noise have a fixed panel, A, B and C have a type (OscKind: wavetable, multisample, granular). Both views are layout presets built only from library components, and
// both contain the Q group "Basic" (BasicControls: the same members and, with assignIds, the same ids).
//  - OscillatorCompact: the third of the width on the main page (about 420 x 700): the instance selector, the header (on / off, name, route button), the display area with the "full screen"
//    button in its top right corner, and the Basic group in one frame at the bottom. No tuning row and no type bar.
//  - OscillatorFullScreen: the top band (close, on / off, Basic, route at the right end), below left the display area with the type bar under it, below right the tuning row (vertical),
//    the bottom band = the knob panel of the type; the full screen has no instance selector. `setInstance` swaps only the type bar, the tuning row, the bottom band and the groups.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "panel.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
class OscillatorCompact : public Composite
{
public:
    PictogramSelector instances;
    ToggleIcon power;
    char name[9] = {};
    RouteButton route { "M" };
    MenuButton full { "FULL", MenuButton::Icon::None };
    BasicControls basic;

    /** Outputs: the new instance index (an Index change of `instances`), "full screen requested" (the Request of `full`), the changes of the Basic group's parts, the route button's Request. */
    OscillatorCompact()
    {
        for (int i = 0; i < 5; ++i)
            instances.setItem (i, -1, instanceName (static_cast<OscInstance> (i)));   // text cells
        power.state.icon = ToggleIcon::Icon::Power;
        basic.setStacked (true);
        addKid (instances); addKid (power); addKid (route); addKid (basic); addKid (full);
        setInstance (OscInstance::A, OscKind::Wavetable);
    }
    void setInstance (OscInstance i, OscKind k)
    {
        instances.state.index = static_cast<int> (i);
        setText (name, sizeof name, instanceName (i));
        basic.configure (i, k);
        basic.group.setActive (true);
        layout();
    }
    Rect displayRect() const noexcept { return display_; }
    QGroup& group() noexcept { return basic.group; }
    Size minSize() const override { return { 420, 700 }; }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        basic.group.draw (sb);
    }

protected:
    void layout() override
    {
        const int m = 6, W = rect_.w - 2 * m;
        const int basicH = basic.minSize().h + 16;
        instances.place ({ rect_.x + m, rect_.y + m, W, 56 });
        const int hy = rect_.y + m + 56 + 8;
        power.place ({ rect_.x + m, hy, 64, 64 });
        route.place ({ rect_.right() - m - 64, hy, 64, 64 });
        nameBox_ = { rect_.x + m + 64 + 12, hy, std::max (0, W - 2 * 64 - 24), 64 };
        const int dy = hy + 64 + 8;
        const int by = rect_.bottom() - m - basicH;
        display_ = { rect_.x + m, dy, W, std::max (0, by - 8 - dy) };
        full.place ({ display_.right() - 100 - 4, display_.y + 4, 100, 56 });
        basic.place ({ rect_.x + m, by, W, basicH });
    }
    void drawExtra (Pen& p) const override
    {
        p.textLeft (nameBox_, name, 4, p.c (colour::kWhite, 255));
        p.rect (display_, p.c (colour::kPanel, 255));
        p.frame (display_, 1, p.c (colour::kRest, 255));
    }
    Rect display_, nameBox_;
};

// ===============================================================================================================================================================
/** Outputs of the full screen (the changes of the children come out with the ids the owner gave them): close requested = the Request of `close`; details requested = the Request of a
    DetailButton, of the route button, the gear or the dropdown; modsRequested = ModsRequest of a knob or a number field. A touch that goes down on a control of a Q group makes that group the
    active one (the Q frame moves to it); `turnQ (knobIndex, delta)` routes the hardware knob to the active group. The groups are in `groups` (`groupName`); the first is Basic. */
class OscillatorFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 8;
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    ToggleIcon power;
    BasicControls basic;
    RouteButton route { "M" };
    TypeBar bar;
    TuneRow tune;
    KnobPanel panel;
    QGroup groups[kMaxGroups];

    OscillatorFullScreen()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        tune.setVertical (true);
        addKid (close); addKid (power); addKid (basic); addKid (route); addKid (bar); addKid (tune); addKid (panel);
        setInstance (OscInstance::A, OscKind::Wavetable);
    }

    /** Switches the instance (and the type of A, B, C): the Basic group, the type bar, the tuning row, the bottom band and the Q groups change; the active group goes back to Basic. */
    void setInstance (OscInstance inst, OscKind kind)
    {
        inst_ = inst;
        kind_ = kind;
        basic.configure (inst, kind);
        switch (inst)
        {
            case OscInstance::Sub: bar.load (barpresets::kSub); panel.load (presets::kEmptyFull); break;
            case OscInstance::Noise: bar.load (barpresets::kNoise); panel.load (presets::kEmptyFull); break;
            default:
                bar.load (kind == OscKind::Wavetable ? barpresets::kWavetable : kind == OscKind::Multisample ? barpresets::kMultisample : barpresets::kGranular);
                panel.load (kind == OscKind::Wavetable ? presets::kWavetableFull : kind == OscKind::Multisample ? presets::kMultisampleFull : presets::kGranularFull);
                break;
        }
        tune.setShort (false);
        tune.setCrsOnly (inst == OscInstance::Sub);
        tune_shown_ = inst != OscInstance::Noise;
        show (tune, tune_shown_);
        layout();
        buildGroups();
    }
    OscInstance instance() const noexcept { return inst_; }
    OscKind kind() const noexcept { return kind_; }
    /** The rectangle reserved for the display (the picture is drawn by the overlay views). */
    Rect displayRect() const noexcept { return display_; }
    int numGroups() const noexcept { return nGroups_; }
    const char* groupName (int i) const noexcept { return i >= 0 && i < nGroups_ ? names_[i] : ""; }
    int activeGroup() const noexcept { return active_; }
    void setActiveGroup (int i)
    {
        for (int k = 0; k < nGroups_; ++k)
            groups[k].setActive (k == i);
        active_ = i >= 0 && i < nGroups_ ? i : -1;
    }
    Change turnQ (int knobIndex, double delta) { return active_ >= 0 ? groups[active_].turn (knobIndex, delta) : Change::none(); }
    bool tuneShown() const noexcept { return tune_shown_; }
    Size minSize() const override { return { 1280, 800 }; }

    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)
            for (int i = 0; i < nGroups_; ++i)
                if (groups[i].rect().contains (x, y))
                {
                    setActiveGroup (i);
                    break;
                }
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        for (int i = 0; i < nGroups_; ++i)
            groups[i].draw (sb);
    }
    /** All the leaves that are on the screen now (what the adjacency check looks at). */
    int leaves (Component** out, int max) { return collectLeaves (out, 0, max); }

protected:
    void layout() override
    {
        const int W = rect_.w, H = rect_.h, m = 8;
        const int topH = H * 12 / 100, bottomH = H * 31 / 100;
        const Rect top { rect_.x + m, rect_.y + m, W - 2 * m, topH };
        Rect r[4];
        const int fixed[4] = { 84, 64, 0, 72 }, weight[4] = { 0, 0, 1, 0 };
        layoutRow (top, fixed, weight, 4, kGap, r);
        close.place ({ r[0].x, r[0].y + (topH - 56) / 2, r[0].w, 56 });
        power.place ({ r[1].x, r[1].y + (topH - 56) / 2, r[1].w, 56 });
        basic.place (r[2]);
        route.place ({ r[3].x, r[3].y + (topH - 64) / 2, r[3].w, 64 });
        const int midY = top.bottom() + m, midH = rect_.bottom() - m - bottomH - m - midY;
        const int leftW = (W - 2 * m) * 64 / 100, rightX = rect_.x + m + leftW + m;
        const int barH = 56;
        display_ = { rect_.x + m, midY, leftW, std::max (0, midH - barH - kGap) };
        bar.place ({ rect_.x + m, display_.bottom() + kGap, leftW, barH });
        tune.place ({ rightX, midY, rect_.right() - m - rightX, std::max (0, midH) });
        panel.place ({ rect_.x + m, rect_.bottom() - m - bottomH, W - 2 * m, bottomH });
    }
    void buildGroups()
    {
        for (QGroup& g : groups)
            for (int i = 0; i < 4; ++i)
                g.set (i, nullptr);
        nGroups_ = 0;
        auto next = [&] (const char* name) -> QGroup& { setText (names_[nGroups_], sizeof names_[0], name); return groups[nGroups_++]; };
        QGroup& b = next ("Basic");
        for (int i = 0; i < 4; ++i)
            b.set (i, basic.group.member (i));
        fillGroup (next ("Type"), bar);
        if (tune_shown_)
        {
            QGroup& g = next ("Tune");
            if (inst_ == OscInstance::Sub) g.set (0, &tune.crs);
            else { g.set (0, &tune.oct); g.set (1, &tune.sem); g.set (2, &tune.fin); g.set (3, &tune.crs); }
        }
        const PanelPreset* pp = panel.preset();
        static const char* const kGranularNames[] = { "Grain", "Playback", "Random" };
        for (int i = 0; pp && i < pp->numQ && nGroups_ < kMaxGroups; ++i)
        {
            QGroup& g = next (kind_ == OscKind::Granular ? kGranularNames[i] : "Unison");
            for (int k = 0; k < 4; ++k)
                g.set (k, pp->q[i].slot[k] == PanelSlot::Count ? nullptr : panel.part (pp->q[i].slot[k]));
        }
        if (panel.warp() && nGroups_ < kMaxGroups)
        {
            WarpPair& w = *panel.warp();
            QGroup& g = next ("Warp");
            g.set (0, &w.first.type); g.set (1, &w.first.amount); g.set (2, &w.second.type); g.set (3, &w.second.amount);
        }
        active_ = -1;
        setActiveGroup (0);
    }
    Rect display_;
    OscInstance inst_ = OscInstance::A;
    OscKind kind_ = OscKind::Wavetable;
    bool tune_shown_ = true;
    int nGroups_ = 0, active_ = -1;
    char names_[kMaxGroups][10] = {};
};

}} // namespace synthyboy_mpc::ui
