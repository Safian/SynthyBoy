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
    MenuButton full { "", MenuButton::Icon::Expand };   // the "full screen" button: four corner brackets
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
    static constexpr int kKnobH = 124;
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
        const int basicH = basic.minSize().h - basic.pan.minSize().h + kKnobH;   // the two knobs get the room a third of the width allows (kKnobH), with their value under them
        instances.place ({ rect_.x + m, rect_.y + m, W, 56 });
        const int hy = rect_.y + m + 56 + 8;
        power.place ({ rect_.x + m, hy, 64, 64 });
        route.place ({ rect_.right() - m - 64, hy, 64, 64 });
        nameBox_ = { rect_.x + m + 64 + 12, hy, std::max (0, W - 2 * 64 - 24), 64 };
        const int dy = hy + 64 + 8;
        const int by = rect_.bottom() - m - basicH;
        display_ = { rect_.x + m, dy, W, std::max (0, by - kQLane - 4 - dy) };   // kQLane: the index chip of the first member of the Q frame sits on its top edge
        full.place ({ display_.right() - 64 - 4, display_.y + 4, 64, 56 });
        basic.place ({ rect_.x + m, by, W, basicH });
    }
    void drawUnder (Pen& p) const override   // the display area under the buttons that sit on it
    {
        p.block (display_);
    }
    void drawExtra (Pen& p) const override { p.textLeft (nameBox_, name, theme::L().fTitle, p.c (theme::L().text, 255), theme::L().fMin); }
    Rect display_, nameBox_;
};

// ===============================================================================================================================================================
/** Outputs of the full screen (the changes of the children come out with the ids the owner gave them): close requested = the Request of `close`; details requested = the Request of a
    DetailButton, of the route button, the gear or the dropdown; modsRequested = ModsRequest of a knob or a number field. A touch that goes down on a control of a Q group makes that group the
    active one (the Q frame moves to it); `turnQ (knobIndex, delta)` routes the hardware knob to the active group. The groups are in `groups` (`groupName`); the first is Basic. */
class OscillatorFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 8, kBottomH = 220;   // kBottomH: the blocks of the knob panel
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
        bar_shown_ = inst != OscInstance::Sub && inst != OscInstance::Noise;
        tune_shown_ = inst != OscInstance::Sub;
        show (bar, bar_shown_);
        show (tune, tune_shown_);
        static const BarPreset kEmptyBar { "none", nullptr, 0 };
        switch (inst)
        {
            case OscInstance::Sub: bar.load (kEmptyBar); panel.load (presets::kSubFull); break;
            case OscInstance::Noise: bar.load (kEmptyBar); panel.load (presets::kNoiseGenFull); break;
            default:
                bar.load (kind == OscKind::Wavetable ? barpresets::kWavetable : kind == OscKind::Multisample ? barpresets::kMultisample : barpresets::kGranular);
                panel.load (kind == OscKind::Wavetable ? presets::kWavetableFull : kind == OscKind::Multisample ? presets::kMultisampleFull : presets::kGranularFull);
                break;
        }
        tune.setShort (false);
        tune.setCrsOnly (false);
        tune.setNoise (inst == OscInstance::Noise);
        layout();
        buildGroups();
        setActiveGroup (0);
    }
    /** An oscillator type the library has no full screen for (the engine's Sample and Spectral types, only on the host's own pages): the type bar and the bottom band are empty (the owner draws the
        type name), the other parts stay. Call after `setInstance`. */
    void setUnsupportedType()
    {
        static const BarPreset kNoBar { "none", nullptr, 0 };
        bar.load (kNoBar);
        panel.load (presets::kEmptyFull);
        layout();
        buildGroups();
    }
    OscInstance instance() const noexcept { return inst_; }
    OscKind kind() const noexcept { return kind_; }
    /** The screen is a PAGE of a tab (the OSC tab: the tab names the oscillator and nothing closes it): the CLOSE button is not there, its place carries the name of the oscillator ("" = a full screen with a close button). */
    void setPageTitle (const char* title)
    {
        setText (pageTitle_, sizeof pageTitle_, title != nullptr ? title : "");
        show (close, pageTitle_[0] == '\0');
    }
    bool isPage() const noexcept { return pageTitle_[0] != '\0'; }
    /** The rectangle reserved for the display (the picture is drawn by the overlay views). */
    Rect displayRect() const noexcept { return display_; }
    /** The titled outlines (blocks) of the type bar and of the tuning column (empty when the tuning row is absent). The panel's own blocks are `panel.blockRect (i)`. */
    Rect barBlock() const noexcept { return barBlock_; }
    Rect tuneBlock() const noexcept { return tune_shown_ ? tuneBlock_ : Rect {}; }
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
    void rebuildGroups() { layout(); buildGroups(); }
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
        for (int i = 0; i < nGroups_; ++i)
            if (i != active_)
                groups[i].drawMapped (sb);
    }
    /** All the leaves that are on the screen now (what the adjacency check looks at). */
    int leaves (Component** out, int max) { return collectLeaves (out, 0, max); }

protected:
    void layout() override
    {
        // top: the lane of the Q index chips of the band, the band (the Basic group: the knobs are the medium size, the dial at the left); middle: the type bar under the header with the display below it at the left (at least 240 px tall), the tuning block at the
        // right; bottom: the blocks of the type's knob panel (the sub oscillator has none: the middle takes its room)
        const int W = rect_.w, H = rect_.h, m = 8;
        const bool sub = inst_ == OscInstance::Sub;
        const bool noise = inst_ == OscInstance::Noise;
        const bool isTab = H <= 650;
        const int topH = isTab ? 72 : 88;
        const int bottomH = sub ? 245 : (isTab ? 212 : kBottomH);
        const int lane = isTab ? 22 : (m + kQLane + 2);
        const Rect top { rect_.x + m, rect_.y + lane, W - 2 * m, topH };
        Rect r[4];
        const int fixed[4] = { 84, 64, 0, 72 }, weight[4] = { 0, 0, 1, 0 };
        layoutRow (top, fixed, weight, 4, kGap, r);
        close.place ({ r[0].x, top.y + (topH - 56) / 2, r[0].w, 56 });
        power.place ({ r[1].x, top.y + (topH - 56) / 2, r[1].w, 56 });
        basic.place (r[2]);
        route.place ({ r[3].x, top.y + (topH - 64) / 2, r[3].w, 64 });
        const int gap = kGap;
        const int panelY = rect_.bottom() - (isTab ? 0 : m) - bottomH;
        const int leftW = (W - 2 * m) * 78 / 100, rightX = rect_.x + m + leftW + m;
        constexpr int kTitleH = KnobPanel::kBlockTitleH, kPad = KnobPanel::kBlockPad;
        const int barH = 56, barBlockH = isTab ? 70 : (kTitleH + barH + kPad);
        const int titleH = isTab ? 14 : kTitleH;

        if (sub)
        {
            const int midY = top.bottom() + gap;
            display_ = { rect_.x + m, midY, W - 2 * m, std::max (0, panelY - gap - midY) };
            barBlock_ = {};
            bar.place ({});
            tuneBlock_ = {};
            tune.place ({});
        }
        else if (noise)
        {
            const int midY = top.bottom() + gap;
            const int midH = std::max (0, panelY - gap - midY);
            display_ = { rect_.x + m, midY, leftW, midH };
            barBlock_ = {};
            bar.place ({});
            tuneBlock_ = { rightX, midY, rect_.right() - m - rightX, midH };
            tune.place ({ tuneBlock_.x + kPad, tuneBlock_.y + kTitleH, std::max (0, tuneBlock_.w - 2 * kPad), std::max (0, tuneBlock_.h - kTitleH - kPad) });
        }
        else
        {
            barBlock_ = { rect_.x + m, top.bottom() + gap, leftW, barBlockH };
            bar.place ({ barBlock_.x + kPad, barBlock_.y + titleH, barBlock_.w - 2 * kPad, barH });
            const int dispY = barBlock_.bottom() + gap;
            display_ = { rect_.x + m, dispY, leftW, std::max (0, panelY - gap - dispY) };
            tuneBlock_ = Rect { rightX, barBlock_.y, rect_.right() - m - rightX, std::max (0, display_.bottom() - barBlock_.y) };
            tune.place ({ tuneBlock_.x + kPad, tuneBlock_.y + kTitleH, std::max (0, tuneBlock_.w - 2 * kPad), std::max (0, tuneBlock_.h - kTitleH - kPad) });
        }
        panel.place ({ rect_.x + m, panelY, W - 2 * m, bottomH });
    }
    bool hasPanel() const noexcept { return true; }
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
        if (bar_shown_)
            fillGroup (next ("Type"), bar);
        if (tune_shown_)
        {
            QGroup& g = next ("Tune");
            if (inst_ == OscInstance::Noise) { g.set (0, &tune.crs); g.set (1, &tune.fin); g.set (2, &tune.track); }
            else { g.set (0, &tune.oct); g.set (1, &tune.sem); g.set (2, &tune.fin); g.set (3, &tune.crs); }
        }
        const PanelPreset* pp = panel.preset();
        static const char* const kGranularNames[] = { "Grain", "Playback", "Random" };
        static const char* const kNoiseNames[] = { "Source", "Playback" };
        static const char* const kSubNames[] = { "Tune", "Phase", "Routing" };
        for (int i = 0; pp && i < pp->numQ && nGroups_ < kMaxGroups; ++i)
        {
            const char* gname = "Unison";
            if (inst_ == OscInstance::Sub)
                gname = i < 3 ? kSubNames[i] : "Sub";
            else if (inst_ == OscInstance::Noise)
                gname = pp->numQ == 1 ? "Noise" : (i < 2 ? kNoiseNames[i] : "Noise");
            else if (kind_ == OscKind::Granular)
                gname = kGranularNames[i];
            QGroup& g = next (gname);
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
    void drawUnder (Pen& p) const override
    {
        // every Q group has a block of its own (the active one also has the coloured frame of its QGroup): Basic, the type bar, the tuning column; the panel draws its own blocks
        p.block (basic.rect());
        if (bar_shown_)
            p.block (barBlock_);
        if (tune_shown_)
            p.block (tuneBlock_);
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (pageTitle_[0] != '\0')
            p.textCentered (close.rect(), pageTitle_, T.fTitle, p.c (T.text, 255), T.fMin);
        if (bar_shown_)
        {
            // the title row of a low block is the row of the Q chips of its members: the title goes to the right end, the chips keep the left (a title under a chip lost its first letter)
            const Rect tr { barBlock_.x + KnobPanel::kBlockPad + 4, barBlock_.y + 3, std::max (0, barBlock_.w - 2 * KnobPanel::kBlockPad - 8), 16 };
            if (barBlock_.h < 80)
                p.textRight (tr, barTitle(), T.fLabel - 2.0f, p.c (T.textDim, 255));
            else
                p.textLeft (tr, barTitle(), T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        }
        if (tune_shown_)
        {
            p.textLeft ({ tuneBlock_.x + KnobPanel::kBlockPad + 4, tuneBlock_.y + 3, std::max (0, tuneBlock_.w - 16), 16 }, "TUNE", T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        }
    }
    const char* barTitle() const noexcept
    {
        if (inst_ == OscInstance::Sub) return "SUB";
        if (inst_ == OscInstance::Noise) return "NOISE";
        return kind_ == OscKind::Wavetable ? "WAVETABLE" : kind_ == OscKind::Multisample ? "MULTISAMPLE" : "GRANULAR";
    }
    Rect display_, barBlock_, tuneBlock_;
    OscInstance inst_ = OscInstance::A;
    OscKind kind_ = OscKind::Wavetable;
    bool tune_shown_ = true, bar_shown_ = true;
    int nGroups_ = 0, active_ = -1;
    char names_[kMaxGroups][20] = {};
    char pageTitle_[16] = {};
};

}} // namespace synthyboy_mpc::ui
