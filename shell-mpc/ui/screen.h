// SynthyBoy - MPC UI component library: the two views of an oscillator INSTANCE (owner approved; drawn for 1280 x 800, other sizes scale in proportion). The synth has five instances: Sub, A, B,
// C and Noise; Sub and Noise have a fixed panel, A, B and C have a type (OscKind: wavetable, multisample, granular). Both views are layout presets built only from library components, and
// both contain the Q group "Basic" (BasicControls: the same members and, with assignIds, the same ids).
//  - OscillatorCompact: the third of the width on the main page (about 420 x 700): the header row (the source selector Sub / Osc A / Osc B / Osc C / Noise: a carousel with a list), the display
//    area (the same rectangle as the envelope's and the LFO's, play_rows.h) with its header band (the power button, the route button, the "full screen" button) and the plot area under the band, and the Basic group in one frame at the bottom: the type cells and ONE row (table stepper / OCT, PAN, LEVEL). No tuning row and no type bar.
//  - OscillatorFullScreen: the top band (close, on / off, Basic, route at the right end), below left the display area with the type bar under it, below right the tuning row (vertical),
//    the bottom band = the knob panel of the type; the full screen has no instance selector. `setInstance` swaps only the type bar, the tuning row, the bottom band and the groups.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "carousel.h"
#include "panel.h"
#include "play_rows.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
class OscillatorCompact : public Composite
{
public:
    Carousel instances;   // the source: Sub, Osc A, Osc B, Osc C, Noise: the ONE selector of the three thirds (a Carousel: the neighbours step, a tap on the name opens the list of the five)
    ToggleIcon power;
    RouteButton route { "M" };
    MenuButton full { "", MenuButton::Icon::Expand };   // the "full screen" button: four corner brackets
    BasicControls basic;

    /** Outputs: the new instance index (an Index change of `instances`; a Request when its name is tapped: the owner opens the list of `instanceName (i)`), "full screen requested" (the Request of `full`), the changes of the Basic group's parts, the route button's Request. */
    OscillatorCompact()
    {
        instances.state.count = 5;
        instances.state.wrap = true;
        instances.state.names = sourceNames();
        instances.listable = true;
        power.state.icon = ToggleIcon::Icon::Power;
        power.state.negativeOff = true;
        basic.setStacked (true);
        addKid (instances); addKid (power); addKid (route); addKid (basic); addKid (full);
        setInstance (OscInstance::A, OscKind::Wavetable);
    }
    /** The names of the five entries of the selector and of its list (the wording of the owner). */
    static const char* const* sourceNames() noexcept { static const char* const n[5] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" }; return n; }
    static const char* sourceName (OscInstance i) noexcept { return sourceNames()[static_cast<int> (i)]; }
    static const char* sourceName (int i) noexcept { return sourceName (static_cast<OscInstance> (std::clamp (i, 0, 4))); }
    void setInstance (OscInstance i, OscKind k)
    {
        inst_ = i;
        instances.state.index = static_cast<int> (i);
        instances.setItem (sourceName (i));
        basic.configure (i, k);
        basic.group.setActive (true);
        layout();
    }
    Rect displayRect() const noexcept { return display_; }
    /** The plot area of the picture (below the header band, playrows::plotRect): where the wave is drawn. */
    Rect plotRect() const noexcept { return playrows::plotRect (display_); }
    /** The room of the label text in the band (between the power button and the route button or, for the sub oscillator, the full screen button). */
    Rect labelRect() const noexcept { return playrows::labelRect (display_, true, inst_ != OscInstance::Sub); }
    QGroup& group() noexcept { return basic.group; }
    Size minSize() const override { return { 420, 700 }; }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        basic.group.draw (sb);
    }

protected:
    // top to bottom (play_rows.h): the top row (the source selector), the display (the rectangle of the three thirds) with its header band at the top (the power button at the left, the route button and the full screen button at the right end,
    // the full screen button placed exactly as the envelope graph's and the LFO display's; the label text of the display lies between them) and the plot area under the band (the wave never runs under a button), and from the controls line the
    // Basic group in one frame
    void layout() override
    {
        const int m = playrows::kMargin, W = rect_.w - 2 * m, rowH = playrows::kSelectorH;
        const bool sub = inst_ == OscInstance::Sub;   // the sub oscillator's routing is the ROUTING block of its page: no route button (as on the page)
        const int hy = rect_.y + m;
        display_ = playrows::displayRect (rect_);
        power.place (playrows::powerRect (display_));
        full.place (playrows::fullRect (display_));
        route.place (sub ? Rect {} : playrows::routeRect (display_));   // the route button (F1 ...): 64 x 64 (RouteButton::minSize), left of the full screen button
        instances.place ({ rect_.x + m, hy, W, rowH });
        const int by = playrows::controlsLine (rect_);
        basic.place ({ rect_.x + m, by, W, std::max (0, rect_.bottom() - m - by) });
    }
    void drawUnder (Pen& p) const override   // the display area under the buttons that sit on it
    {
        p.block (display_);
    }
    Rect display_;
    OscInstance inst_ = OscInstance::A;
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
        power.state.negativeOff = true;
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
                bar.load (kind == OscKind::Wavetable ? barpresets::kWavetable : kind == OscKind::Multisample ? barpresets::kMultisample : kind == OscKind::Sample ? barpresets::kSample : kind == OscKind::Spectral ? barpresets::kSpectral : barpresets::kGranular);
                panel.load (kind == OscKind::Wavetable ? presets::kWavetableFull : kind == OscKind::Multisample ? presets::kMultisampleFull : kind == OscKind::Sample ? presets::kSampleFull : kind == OscKind::Spectral ? presets::kSpectralFull : presets::kGranularFull);
                break;
        }
        tune.setShort (false);
        tune.setCrsOnly (false);
        tune.setNoise (inst == OscInstance::Noise);
        layout();
        buildGroups();
        setActiveGroup (0);
    }
    /** (kept for the tests of old pages: every type has a full screen now) An oscillator type the library has no full screen for (the engine's Sample and Spectral types, only on the host's own pages): the type bar and the bottom band are empty (the owner draws the
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
        const int topH = isTab ? 78 : 88;   // 78: the dial of the Basic knobs is as big as the dial of the warp slots (70 px, one size on the page)
        const int bottomH = isTab ? 216 : kBottomH;
        const int lane = isTab ? 22 : (m + kQLane + 2);
        const Rect top { rect_.x + m, rect_.y + lane, W - 2 * m, topH };
        Rect r[4];
        const int fixed[4] = { 84, 64, 0, sub ? 0 : 72 }, weight[4] = { 0, 0, 1, 0 };   // the sub oscillator has its routing in the ROUTING block: no second route button in the header
        layoutRow (top, fixed, weight, 4, kGap, r);
        close.place ({ r[0].x, top.y + (topH - 56) / 2, r[0].w, 56 });
        power.place ({ r[1].x, top.y + (topH - 56) / 2, r[1].w, 56 });
        basic.place (r[2]);
        route.place (sub ? Rect {} : Rect { r[3].x, top.y + (topH - 64) / 2, r[3].w, 64 });
        const int gap = kGap;
        const int panelY = rect_.bottom() - (isTab ? 0 : m) - bottomH;
        Rect panelRect { rect_.x + m, panelY, W - 2 * m, bottomH };
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
        else if (noise && panel.preset() != nullptr && panel.preset()->items == presets::kNoiseGenItems)
        {
            // the generated noise: ONE clean grid in the bottom band, two blocks of the same height side by side, the same two rows in both: TUNE (PITCH, FINE / KEY TRACK) at the left and NOISE (KIND, STEREO / ENV 1) at the right, the
            // rows of the two blocks are the same rows (same top, same title strip, same row heights, the lane of the chips between them); the display has the whole width above them, as on the sub oscillator's page
            const int midY = top.bottom() + gap;
            display_ = { rect_.x + m, midY, W - 2 * m, std::max (0, panelY - gap - midY) };
            barBlock_ = {};
            bar.place ({});
            const int halfW = (W - 3 * m) / 2;
            tuneBlock_ = { rect_.x + m, panelY, halfW, bottomH };
            tune.place ({ tuneBlock_.x + kPad, tuneBlock_.y + kTitleH, std::max (0, tuneBlock_.w - 2 * kPad), std::max (0, tuneBlock_.h - kTitleH - kPad) });
            panelRect = { tuneBlock_.right() + m, panelY, W - 2 * m - halfW - m, bottomH };
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
        panel.place (panelRect);
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
            QGroup& g = next ("Warp 1");
            g.set (0, &w.first.type); g.set (1, &w.first.amount); g.set (2, &w.first.var);
            QGroup& g2 = next ("Warp 2");
            g2.set (0, &w.second.type); g2.set (1, &w.second.amount); g2.set (2, &w.second.var);
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
        return kind_ == OscKind::Wavetable ? "WAVETABLE" : kind_ == OscKind::Multisample ? "MULTISAMPLE" : kind_ == OscKind::Sample ? "SAMPLE" : kind_ == OscKind::Spectral ? "SPECTRAL" : "GRANULAR";
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
