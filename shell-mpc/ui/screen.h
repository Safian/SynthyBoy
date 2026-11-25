// SynthyBoy - MPC UI component library: the two views of an oscillator INSTANCE (owner approved; drawn for 1280 x 800, other sizes scale in proportion). The synth has five instances: Sub, A, B,
// C and Noise. Sub and Noise have a fixed panel; A, B and C have an OscKind. Both views are built from library components and start with a Basic Q-Link group.
//  - OscillatorCompact: the third of the width on the main page (about 420 x 700): the header row (the source selector Sub / Osc A / Osc B / Osc C / Noise: a dropdown), the display
//    area (the same rectangle as the envelope's and the LFO's, play_rows.h) with its header band (the power button, the route button, the "full screen" button) and the plot area under the band, and the Basic group in one frame at the bottom.
//  - OscillatorFullScreen: the top band (close, power, the first Q group and mix readout), the display and the bottom knob panel with MORE.
//    `setInstance` updates the top row, bottom panel and groups.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cctype>
#include <cstdio>

#include "carousel.h"
#include "panel.h"
#include "play_rows.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
class OscillatorCompact : public Composite
{
public:
    using NativeHeader = std::array<Rect, 3>;
    MenuButton instances;
    ToggleIcon power;
    RouteButton route { "M" };
    MenuButton full { "", MenuButton::Icon::Expand };   // the "full screen" button: four corner brackets
    BasicControls basic;

    /** Outputs: the new instance index (a Request from `instances` opens the list of five sources), "full screen requested" (the Request of `full`), the changes of the Basic group's parts, the route button's Request. */
    OscillatorCompact()
    {
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
        instances.setLabel (sourceName (i));
        basic.configure (i, k);
        basic.group.setActive (true);
        layout();
    }
    /** The Play skin supplies the instance selector as a host-native component; keep the plugin's old compact carousel out of that pass. */
    void setNativeInstanceSelector (bool shown)
    {
        show (instances, shown);
        instances.setEnabled (shown);
    }
    void setNativeHeader (const NativeHeader& header) noexcept { nativeHeader_ = header; }
    const NativeHeader& nativeHeader() const noexcept { return nativeHeader_; }
    Rect displayRect() const noexcept { return display_; }
    /** The plot area of the picture (below the header band, playrows::plotRect): where the wave is drawn. */
    Rect plotRect() const noexcept { return playrows::plotRect (display_); }
    /** The room of the label text in the band (between the power button and the route button or, for the sub oscillator, the full screen button). */
    Rect labelRect() const noexcept { return playrows::labelRect (display_, true, inst_ != OscInstance::Sub); }
    QGroup& group() noexcept { return basic.group; }
    Size minSize() const override { return { 420, 700 }; }
    void draw (gpu::SceneBuilder& sb) const override
    {
        if (nativeHeader_[0].empty())
            Composite::draw (sb);
        else
        {
            // Native compact controls and their transparent margins share the
            // host background; repainting around rectangular holes makes seams.
            drawChildren (sb);
        }
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
    NativeHeader nativeHeader_ {};
    OscInstance inst_ = OscInstance::A;
};

// ===============================================================================================================================================================
/** The read-only indicator of the full screen's top band (bold cut): where the oscillator goes and how loud it is. LEVEL, PAN and the DESTINATION of a module are edited on the Mixer (their one home); here they are only shown, so
    the owner of the page sees them without a second place to set them. No touch target, no Q slot. The owner writes `state`. */
class OscMixIndicator : public Component
{
public:
    struct State { char dest[8] = {}; bool segment[2] = { false, false }; char info[28] = {}; };
    State state;
    void setDest (const char* t) { setText (state.dest, sizeof state.dest, t); }
    void setInfo (const char* t) { setText (state.info, sizeof state.info, t); }
    Size minSize() const override { return { 120, 56 }; }
    int touchTargets (Rect*, int) const override { return 0; }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.block (rect_);
        const Rect strip { rect_.x + 10, rect_.y + 7, rect_.w - 20, 6 };
        const int half = strip.w / 2;
        p.rrect ({ strip.x, strip.y, half - 2, strip.h }, 3.0f, p.c (state.segment[0] ? T.flow[0] : T.track, 255));
        p.rrect ({ strip.x + half + 2, strip.y, strip.w - half - 2, strip.h }, 3.0f, p.c (state.segment[1] ? T.flow[3] : T.track, 255));
        p.textCentered ({ rect_.x + 6, rect_.y + 16, rect_.w - 12, std::max (0, (rect_.h - 20) * 11 / 20) }, state.dest, T.fValue, p.c (T.text, 255), T.fMin);
        p.textCentered ({ rect_.x + 6, rect_.y + 16 + std::max (0, (rect_.h - 20) * 11 / 20), rect_.w - 12, std::max (0, (rect_.h - 20) * 9 / 20) }, state.info, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
    }
};

// ===============================================================================================================================================================
/** OscTopRow: the first Q group ("Basic") of the full screen, ONE row (bold cut). A, B, C: [TYPE, SOURCE / TABLE, the position knob of the type (WT POS, TIMBRE, SCAN, POSITION, SCAN), OCT]; Sub: [WAVEFORM, OCT, CRS, PHASE];
    Noise generated: [KIND, SOUND, FILTER, STEREO]; Noise sample: [KIND, FILTER, STEREO] (SOUND does not apply to a sample: hidden, the group closes up). Level, pan and destination are NOT here (the Mixer is their home). */
class OscTopRow : public Composite
{
public:
    TypeSelector selector;
    Stepper kind, sound;
    SourcePicker source;
    NumberField oct { "OCT", NumberField::Format::IntSigned };
    Knob pos { "POS" }, crs { "CRS" }, phase { "PHASE" }, filter { "FILTER" }, stereo { "STEREO" };
    QGroup group;

    OscTopRow()
    {
        oct.state.min = -4; oct.state.max = 4;
        crs.state.bipolar = true; crs.state.defaultValue = 0.5; crs.state.value = crs.state.modValue = 0.5;
        for (Knob* k : { &pos, &crs, &phase, &filter, &stereo })
            k->state.side = true;
        kind.setCaption ("KIND");
        sound.setCaption ("SOUND");
        addKid (selector); addKid (kind, false); addKid (sound, false); addKid (source); addKid (oct); addKid (pos); addKid (crs, false); addKid (phase, false); addKid (filter, false); addKid (stereo, false);
        configure (OscInstance::A, OscKind::Wavetable, false);
    }
    /** The members for an instance, the type of A, B, C and (Noise) the mode: the pictograms, the visible parts, the Q group. */
    void configure (OscInstance inst, OscKind k, bool noiseSample)
    {
        inst_ = inst;
        kind_ = k;
        noiseSample_ = noiseSample;
        const bool sub = inst == OscInstance::Sub, noise = inst == OscInstance::Noise, abc = ! sub && ! noise;
        selector.state.count = 0;
        if (sub)
        {
            static const int ids[6] = { pictogram::Sine, pictogram::HalfSine, pictogram::Triangle, pictogram::Saw, pictogram::Square, pictogram::Pulse };
            static const char* const nm[6] = { "SINE", "HALF", "TRI", "SAW", "SQR", "PULSE" };
            for (int i = 0; i < 6; ++i)
                selector.setItem (i, ids[i], nm[i]);
        }
        else if (abc)
        {
            selector.setItem (0, pictogram::Wavetable, "WAVE");
            selector.setItem (1, pictogram::Multisample, "MULTI");
            selector.setItem (2, pictogram::Sample, "SAMPLE");
            selector.setItem (3, pictogram::Granular, "GRAIN");
            selector.setItem (4, pictogram::Spectral, "SPEC");
            selector.state.index = k == OscKind::Wavetable ? 0 : k == OscKind::Multisample ? 1 : k == OscKind::Sample ? 2 : k == OscKind::Granular ? 3 : 4;
        }
        pos.setLabel (k == OscKind::Wavetable ? "WT POS" : k == OscKind::Multisample ? "TIMBRE" : k == OscKind::Spectral ? "POSITION" : "SCAN");
        pos.state.bipolar = false;
        show (selector, ! noise); show (source, abc); show (oct, ! noise); show (pos, abc); show (crs, false); show (phase, false);
        show (kind, noise); show (sound, noise && ! noiseSample); show (filter, false); show (stereo, false);
        n_row_ = 0;
        auto add = [&] (Component* c) { if (n_row_ < 4) row_[n_row_++] = c; };
        if (abc) { add (&selector); add (&source); add (&pos); add (&oct); }
        else if (sub) { add (&selector); add (&oct); }
        else { add (&kind); if (! noiseSample) add (&sound); }
        for (int i = 0; i < 4; ++i)
            group.set (i, i < n_row_ ? row_[i] : nullptr);
        layout();
    }
    void setCardOctave (bool on)
    {
        if (inst_ != OscInstance::Sub && inst_ != OscInstance::Noise)
        {
            show (oct, ! on);
            group.set (3, on ? nullptr : &oct);
        }
    }
    OscInstance instance() const noexcept { return inst_; }
    int numRow() const noexcept { return n_row_; }
    Component* rowMember (int i) const noexcept { return i >= 0 && i < n_row_ ? row_[i] : nullptr; }
    /** The ids of the parts (the owner overrides the ones that differ): selector base, source stepper +1, dropdown +2, OCT +3, FILTER +4, SOUND +5. */
    void assignIds (int base)
    {
        selector.setId (base); source.stepper.setId (base + 1); source.dropdown.setId (base + 2); oct.setId (base + 3); filter.setId (base + 4); sound.setId (base + 5);
    }
    Size minSize() const override
    {
        int w = 0;
        for (int i = 0; i < n_row_; ++i)
            w += row_[i]->minSize().w + (i ? kGap : 0);
        return { w, 72 };
    }

protected:
    void layout() override
    {
        if (n_row_ == 0)
            return;
        int fixed[4] = {}, weight[4] = {};
        Rect r[4];
        for (int i = 0; i < n_row_; ++i)
            weight[i] = row_[i]->minSize().w;
        layoutRow (rect_, fixed, weight, n_row_, kGap, r);
        for (int i = 0; i < n_row_; ++i)
        {
            const bool knob = row_[i] == &pos || row_[i] == &crs || row_[i] == &phase || row_[i] == &filter || row_[i] == &stereo;
            const int h = knob ? r[i].h : std::min (r[i].h, std::max (row_[i]->minSize().h, 72));   // the cells fill the band's height as far as 72 px; the knobs use all of it
            row_[i]->place ({ r[i].x, r[i].y + (r[i].h - h) / 2, r[i].w, h });
        }
    }
    OscInstance inst_ = OscInstance::A;
    OscKind kind_ = OscKind::Wavetable;
    bool noiseSample_ = false;
    Component* row_[4] = {};
    int n_row_ = 0;
};

// ===============================================================================================================================================================
/** Outputs of the full screen (the changes of the children come out with the ids the owner gave them): close requested = the Request of `close`; details requested = the Request of a
    DetailButton, of the gear or of the dropdown; modsRequested = ModsRequest of a knob or a number field. A touch that goes down on a control of a Q group makes that group the
    active one (the Q frame moves to it); `turnQ (knobIndex, delta)` routes the hardware knob to the active group. The groups are in `groups` (`groupName`); the first is Basic.
    THE DEFAULT VIEW (bold cut, docs/ui-map/osc-play-bold-cut/README.md): the top band (close or title, POWER, `top` = the first Q group, `mix` = the read-only level / pan / destination), the display over the whole width, the
    bottom band (`panel`: the blocks of the other Q groups) and the ONE `more` button (everything else of the oscillator: the detail page of the type). A control that does not apply (the amount of a warp that is Off, a sample's
    loop control in generated noise ...) is HIDDEN, not dimmed, and leaves its Q group. */
inline void drawOscillatorPageGround (gpu::SceneBuilder& sb, Rect tab, Rect area, bool off = false)
{
    if (area.empty())
        return;
    Cell cell (sb, area, 0);
    Pen pen (sb, true, off);
    const auto& look = theme::L();
    pen.paintY2 (static_cast<float> (tab.y), static_cast<float> (tab.bottom()), look.bg0, look.bg1, 255);
    pen.rect (area, pen.c (look.bg1, 255));
    pen.paintOff();
}

class OscillatorFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 8, kBottomH = 220, kMoreW = 120;   // kBottomH: the blocks of the knob panel
    static constexpr int kNativeSelectorW = 200;
    static constexpr size_t kNativePanelCells = 24;
    using NativeCells = std::array<Rect, kNativePanelCells>;
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    ToggleIcon power;
    OscTopRow top;
    OscMixIndicator mix;
    DetailButton more { DetailButton::Icon::Gear, "MORE" };
    Knob subBlend { "BLEND" };
    KnobPanel panel;
    QGroup groups[kMaxGroups];

    OscillatorFullScreen()
    {
        power.state.icon = ToggleIcon::Icon::Power;
        power.state.negativeOff = true;
        addKid (close); addKid (power); addKid (top); addKid (mix); addKid (more); addKid (subBlend, false); addKid (panel);
        setInstance (OscInstance::A, OscKind::Wavetable);
    }

    /** Switches the instance (and the type of A, B, C; the noise mode): the top row, the bottom band and the Q groups change; the active group goes back to Basic. */
    void setInstance (OscInstance inst, OscKind kind, bool noiseSample = false)
    {
        inst_ = inst;
        kind_ = kind;
        noiseSample_ = inst == OscInstance::Noise && noiseSample;
        top.configure (inst, kind, noiseSample_);
        show (more, inst != OscInstance::Sub && inst != OscInstance::Noise);
        show (subBlend, inst == OscInstance::Sub);
        switch (inst)
        {
            case OscInstance::Sub: panel.load (presets::kSubFull); break;
            case OscInstance::Noise: panel.load (noiseSample_ ? presets::kNoiseSampleFull : presets::kNoiseGenFull); break;
            default:
                panel.load (kind == OscKind::Wavetable ? presets::kWavetableFull : kind == OscKind::Multisample ? presets::kMultisampleFull : kind == OscKind::Sample ? presets::kSampleFull : kind == OscKind::Spectral ? presets::kSpectralFull : presets::kGranularFull);
                if (WarpPair* w = panel.warp())
                    w->setSingle (kind != OscKind::Wavetable);   // the default view of the other types has warp 1 only (warp 2 is behind MORE)
                if (UnisonBox* u = panel.unison())
                    u->gear.setLabel ("DETAIL");   // (the page of the unison settings: the one MORE of the screen is the other button)
                break;
        }
        warpApplies_[0] = warpApplies_[1] = true;
        if (WarpPair* w = panel.warp())
        {
            w->first.setApplies (true);
            w->second.setApplies (true);
        }
        layout();
        buildGroups();
        setActiveGroup (0);
    }
    OscInstance instance() const noexcept { return inst_; }
    OscKind kind() const noexcept { return kind_; }
    bool noiseSample() const noexcept { return noiseSample_; }
    /** The screen is a PAGE of a tab (the OSC tab: the tab names the oscillator and nothing closes it): the CLOSE button is not there, its place carries the name of the oscillator ("" = a full screen with a close button). */
    void setPageTitle (const char* title)
    {
        setText (pageTitle_, sizeof pageTitle_, title != nullptr ? title : "");
        show (close, pageTitle_[0] == '\0' && ! nativeSelector_);
    }
    bool isPage() const noexcept { return pageTitle_[0] != '\0'; }
    const char* pageTitle() const noexcept { return pageTitle_; }
    /** The standalone tab can host a native instance selector; its aperture remains transparent and operable. */
    void setNativeInstanceSelector (bool shown)
    {
        if (nativeSelector_ == shown)
            return;
        nativeSelector_ = shown;
        show (close, ! shown && pageTitle_[0] == '\0');
        close.setEnabled (! shown);
        if (rect_.w > 0)
            layout();
    }
    bool hasNativeInstanceSelector() const noexcept { return nativeSelector_; }
    Rect selectorArea() const noexcept { return selectorArea_; }
    bool isNativeSelector (int x, int y) const noexcept { return nativeSelector_ && selectorArea_.contains (x, y); }
    /** The standalone tab can host a native destination routing menu in the right top-band slot while the oscillator is ON. */
    void setNativeRouting (bool shown)
    {
        if (nativeRouting_ == shown)
            return;
        nativeRouting_ = shown;
        show (mix, ! nativeRouting_);
        if (rect_.w > 0)
            layout();
    }
    void setNativePower (bool on) noexcept
    {
        nativePower_ = on;
        show (power, ! on);
    }
    bool hasNativePower() const noexcept { return nativePower_; }
    bool hasNativeRouting() const noexcept { return nativeRouting_; }
    bool isNativeRoutingActive() const noexcept { return nativeRouting_ && ! inactive(); }
    bool hasNativeTune() const noexcept { return nativeRouting_ && inst_ != OscInstance::Sub && inst_ != OscInstance::Noise; }
    static constexpr Rect tuneAreaAt (int x, int y) noexcept { return { x + 10, y + 114, 278, 154 }; }
    Rect tuneArea() const noexcept { return tuneAreaAt (rect_.x, rect_.y); }
    Rect routingArea() const noexcept { return routingArea_; }
    Rect panArea() const noexcept { return { rect_.right() - 416, rect_.bottom() - 96, 202, 48 }; }
    Rect levelArea() const noexcept { return { rect_.right() - 210, rect_.bottom() - 96, 202, 48 }; }
    bool isNativeRouting (int x, int y) const noexcept
    {
        return isNativeRoutingActive() && (panArea().contains (x, y) || levelArea().contains (x, y) || routingArea_.contains (x, y));
    }
    /** Hide what does not apply: the amount and the PHASE knob of warp `slot` (0, 1) while its type is Off; the Q groups close up. */
    void setWarpApplies (int slot, bool applies)
    {
        if (slot < 0 || slot > 1 || warpApplies_[slot] == applies)
            return;
        warpApplies_[slot] = applies;
        if (WarpPair* w = panel.warp())
            (slot == 0 ? w->first : w->second).setApplies (applies);
        refreshGroups();
    }
    bool warpApplies (int slot) const noexcept { return slot >= 0 && slot <= 1 && warpApplies_[slot]; }
    /** Hide or show a part of the bottom band (the BOUNCE of a noise sample while LOOP is off); the Q groups close up. */
    void setPartShown (PanelSlot s, bool on)
    {
        if (panel.shown (s) == on)
            return;
        panel.setShown (s, on);
        refreshGroups();
    }
    /** The groups again from what is shown now, the active group kept (when it still exists). */
    void refreshGroups()
    {
        const int a = active_;
        buildGroups();
        setActiveGroup (a >= 0 && a < nGroups_ ? a : 0);
    }
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
    void rebuildGroups() { layout(); buildGroups(); setActiveGroup (0); }
    Size minSize() const override { return { 1280, 800 }; }

    Change touchDown (int x, int y, double nowMs) override
    {
        if (isNativeSelector (x, y) || isNativeRouting (x, y) || (nativePower_ && power.rect().contains (x, y)))
            return Change::none();
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
    void setNativeCells (const NativeCells* cells) noexcept { nativeCells_ = cells; }

    void draw (gpu::SceneBuilder& sb) const override
    {
        if (nativeCells_ == nullptr)
            Composite::draw (sb);
        else
        {
            Pen pen (sb, enabled_, inactive_);
            if (! nativeRouting_)
                paintAroundNativeCells (sb, top.rect(), *nativeCells_, [&] { pen.block (top.rect()); });
            for (int i = 0; i < n_; ++i)
                if (shown_[i])
                {
                    if (kids_[i] == &panel)
                        panel.drawNativePanel (sb);
                    else
                        kids_[i]->draw (sb);
                }
            Cell cell (sb, rect_);
            drawExtra (pen);
        }
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
        // top: the lane of the Q index chips of the band, the band (close or the name, POWER, the Basic group, the read-only mix indicator); middle: the display over the whole width; bottom: the blocks of the other Q groups and MORE
        const int W = rect_.w, H = rect_.h, m = 8;
        const bool oneRow = inst_ == OscInstance::Sub || inst_ == OscInstance::Noise;
        const bool isTab = H <= 650;
        const int topH = isTab ? 78 : 88;   // 78: the dial of the position knob is as big as the dial of the warp slots (70 px, one size on the page)
        const int bottomH = oneRow ? KnobPanel::kBlockTitleH + KnobPanel::kOneRowH + KnobPanel::kBlockPad : (isTab ? 216 : kBottomH);   // the one-row blocks are at their maximum height; the room that frees goes to the display
        const int lane = isTab ? 22 : (m + kQLane + 2);
        const Rect topR { rect_.x + m, rect_.y + lane, W - 2 * m, topH };
        Rect r[4];
        const int fixed[4] = { nativeSelector_ ? kNativeSelectorW : 84, 64, 0, 150 }, weight[4] = { 0, 0, 1, 0 };
        layoutRow (topR, fixed, weight, 4, kGap, r);
        selectorArea_ = { r[0].x, topR.y + (topH - 56) / 2, r[0].w, 56 };
        if (nativeSelector_)
            selectorArea_ = { rect_.x, rect_.bottom() - 48, W, 48 };
        close.place (selectorArea_);
        top.place (r[2]);
        top.setCardOctave (hasNativeTune());
        panel.setShown (PanelSlot::Fin, ! hasNativeTune());
        const int routeH = nativeRouting_ ? 48 : 56;
        routingArea_ = { r[3].x, topR.y + (topH - routeH) / 2, r[3].w, routeH };
        mix.place ({ r[3].x, topR.y + 7, r[3].w, topH - 14 });
        show (mix, ! nativeRouting_);
        const int gap = kGap + 4;
        const int panelY = rect_.bottom() - (nativeSelector_ ? 96 : isTab ? 0 : m) - bottomH;
        const int midY = top.rect().bottom() + gap;
        display_ = { rect_.x + m, midY, W - 2 * m, std::max (0, panelY - gap - midY) };
        if (hasNativeTune())
        {
            const int left = tuneArea().right() + m;
            display_.w -= left - display_.x;
            display_.x = left;
        }
        power.place (playrows::powerRect (display_));
        panel.place ({ rect_.x + m, panelY, W - 2 * m - (inst_ == OscInstance::Noise ? 0 : kMoreW + m), bottomH });
        const Rect card = tuneArea();
        more.place (hasNativeTune() ? Rect { card.x + 143, card.y + 98, 125, 48 }
                                   : Rect { rect_.right() - m - kMoreW, panelY + KnobPanel::kBlockTitleH, kMoreW, 72 });
        subBlend.place ({ rect_.right() - m - kMoreW, panelY + KnobPanel::kBlockTitleH, kMoreW, bottomH - KnobPanel::kBlockTitleH - KnobPanel::kBlockPad });
    }
    /** The live members of a Q group in slot order, closed up from slot 1; a group with no live member is not made. */
    QGroup* addGroup (const char* name, Component* const* members)
    {
        Component* live[4] = {};
        int n = 0;
        for (int i = 0; i < 4; ++i)
            if (members[i] != nullptr)
                live[n++] = members[i];
        if (n == 0 || nGroups_ >= kMaxGroups)
            return nullptr;
        setText (names_[nGroups_], sizeof names_[0], name);
        QGroup& g = groups[nGroups_++];
        for (int i = 0; i < 4; ++i)
            g.set (i, live[i]);
        return &g;
    }
    void buildGroups()
    {
        for (QGroup& g : groups)
            for (int i = 0; i < 4; ++i)
                g.set (i, nullptr);
        nGroups_ = 0;
        Component* m[4] = {};
        for (int i = 0; i < 4; ++i)
            m[i] = top.group.member (i);
        addGroup ("Basic", m);
        const PanelPreset* pp = panel.preset();
        static const char* const kTitles[] = { "Unison", "Grain", "Playback", "Tune", "Behaviour" };
        (void) kTitles;
        for (int i = 0; pp != nullptr && i < pp->numQ; ++i)
        {
            for (int k = 0; k < 4; ++k)
                m[k] = pp->q[i].slot[k] == PanelSlot::Count || ! panel.shown (pp->q[i].slot[k]) ? nullptr : panel.part (pp->q[i].slot[k]);
            if (inst_ == OscInstance::Sub && i == 1)
                m[3] = &subBlend;
            char name[20];
            const char* t = i < pp->numBlocks ? pp->blocks[i].title : "Group";
            std::snprintf (name, sizeof name, "%c%s", t[0], t + 1);
            for (char* c = name + 1; *c; ++c)
                *c = static_cast<char> (std::tolower (static_cast<unsigned char> (*c)));
            addGroup (name, m);
            if (i == 0 && panel.warp() != nullptr && inst_ != OscInstance::Sub && inst_ != OscInstance::Noise)
            {
                // the WARP group right after the unison group: [WARP 1 TYPE, WARP 1 AMOUNT, WARP 2 TYPE, WARP 2 AMOUNT]; the warp 2 members only when the screen shows the second slot, an amount only while its type is not Off
                WarpPair& w = *panel.warp();
                Component* wm[4] = { &w.first.type, warpApplies_[0] ? static_cast<Component*> (&w.first.amount) : nullptr, nullptr, nullptr };
                if (! w.single())
                {
                    wm[2] = &w.second.type;
                    wm[3] = warpApplies_[1] ? static_cast<Component*> (&w.second.amount) : nullptr;
                }
                addGroup ("Warp", wm);
            }
        }
        active_ = -1;
        setActiveGroup (0);
    }
    void drawUnder (Pen& p) const override
    {
        // every Q group has a block of its own (the active one also has the coloured frame of its QGroup): the top row; the panel draws its own blocks
        if (! nativeRouting_)
            p.block (top.rect());
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        if (pageTitle_[0] != '\0' && ! nativeSelector_)
            p.textCentered (close.rect(), pageTitle_, T.fTitle, p.c (T.text, 255), T.fMin);
    }
    Rect display_;
    Rect selectorArea_;
    Rect routingArea_;
    OscInstance inst_ = OscInstance::A;
    OscKind kind_ = OscKind::Wavetable;
    bool noiseSample_ = false, warpApplies_[2] = { true, true };
    bool nativeSelector_ = false, nativeRouting_ = false, nativePower_ = false;
    const NativeCells* nativeCells_ = nullptr;
    int nGroups_ = 0, active_ = -1;
    char names_[kMaxGroups][20] = {};
    char pageTitle_[16] = {};
};

}} // namespace synthyboy_mpc::ui
