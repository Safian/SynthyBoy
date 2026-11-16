// SynthyBoy - MPC UI component library: the two views of the LFO ELEMENT (owner approved plan; drawn for the Play tab of 1280 x 628, other sizes scale in proportion): LfoCompact (the third of the width on
// the main page, about 408 x 620) and LfoFullScreen (the whole tab). The element has two types: an LFO (LFO 1 .. 10: a shape of one cycle) and a RANDOM LFO (RND 1 .. 4: a style); `configure (random)`
// swaps the parts and the Q groups, the places stay.
//  - LfoCompact: the instance stepper (the 14 LFOs, a tap on its name opens the list) with the full screen button on the display, the display, and under it the Q group RATE: [SHAPE, RATE or DIVISION, RISE,
//    SMOOTH time] (a random LFO: [STYLE, RATE or DIVISION, SYNC, STEREO]), one frame around the four.
//  - LfoFullScreen: the top bar (close, the instance cells, HZ / BPM / KEY with TRIP and DOT, MONO, the targets button), in the middle the display at the left and the picker of the shapes (the free cells of
//    last row hold SMOOTH and HOST; EDIT is on the corner of the display) or of the styles at the right, and the bottom band of titled blocks, one Q group each: RATE, TIMING, MOTION, KEY (the KEY block works in the key follow sync
//    only); a random LFO: RANDOM, TRIGGER, KEY. A touch that goes down on a control of a Q group makes that group the active one; `turnQ (knob, delta)` routes the hardware knob to it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "lfo_parts.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
class LfoCompact : public Composite
{
public:
    Stepper instance;                                       // LFO 1 .. 10, RND 1 .. 4
    MenuButton full { "", MenuButton::Icon::Expand };      // the full screen button, on the display
    Stepper shape, style, sync;
    RateControl rate;
    Knob rise { "RISE" }, smoothTime { "SMOOTH" };
    ToggleIcon stereo;
    QGroup group;

    LfoCompact()
    {
        instance.state.wrap = true; instance.listable = true;
        shape.setCaption ("SHAPE"); shape.listable = true; shape.state.wrap = true;
        style.setCaption ("STYLE"); style.listable = true; style.state.wrap = true;
        sync.setCaption ("SYNC"); sync.listable = true; sync.state.wrap = true;
        stereo.state.icon = ToggleIcon::Icon::Generic; stereo.setLabel ("STEREO"); stereo.state.showState = true;
        rise.state.side = smoothTime.state.side = false;
        rate.setShowMode (true);
        addKid (instance); addKid (shape); addKid (style, false); addKid (sync, false); addKid (rate); addKid (rise); addKid (smoothTime); addKid (stereo, false); addKid (full);
        configure (false);
    }
    void configure (bool random)
    {
        random_ = random;
        show (shape, ! random); show (rise, ! random); show (smoothTime, ! random);
        show (style, random); show (sync, random); show (stereo, random);
        for (int i = 0; i < 4; ++i)
            group.set (i, nullptr);
        group.set (0, random ? static_cast<Component*> (&style) : &shape);
        group.set (1, &rate);
        group.set (2, random ? static_cast<Component*> (&sync) : &rise);
        group.set (3, random ? static_cast<Component*> (&stereo) : &smoothTime);
        group.setActive (true);
        layout();
    }
    bool random() const noexcept { return random_; }
    Rect displayRect() const noexcept { return display_; }
    QGroup& qGroup() noexcept { return group; }
    static constexpr int kQuadH = 304, kRowH = 56, kRateH = 72;
    Size minSize() const override { return { 400, 560 }; }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        group.draw (sb);
    }

protected:
    void layout() override
    {
        const int m = 6, W = rect_.w - 2 * m;
        instance.place ({ rect_.x + m, rect_.y + m, W, kRowH });
        const int dy = rect_.y + m + kRowH + 8, qy = rect_.bottom() - m - kQuadH;
        display_ = { rect_.x + m, dy, W, std::max (0, qy - 8 - dy) };
        full.place ({ display_.right() - 64 - 4, display_.y + 4, 64, 56 });
        int y = qy + kQLane;
        if (random_)
        {
            style.place ({ rect_.x + m, y, W, kRowH });
            y += kRowH + kQLane;
            rate.place ({ rect_.x + m, y, W, kRateH });
            y += kRateH + kQLane;
            sync.place ({ rect_.x + m, y, W, kRowH });
            y += kRowH + kQLane;
            stereo.place ({ rect_.x + m, y, W, kRowH });
            return;
        }
        shape.place ({ rect_.x + m, y, W, kRowH });
        y += kRowH + kQLane;
        rate.place ({ rect_.x + m, y, W, kRateH });
        y += kRateH + kQLane;
        const int h = std::max (0, rect_.bottom() - m - y);
        const int cw = (W - kGap) / 2;
        rise.place ({ rect_.x + m, y, cw, h });
        smoothTime.place ({ rect_.x + m + cw + kGap, y, W - cw - kGap, h });
    }
    void drawUnder (Pen& p) const override { p.block (display_); }
    Rect display_;
    bool random_ = false;
};

// ===============================================================================================================================================================
class LfoFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 4, kBandH = 308, kBandRandomH = 196, kTopH = 64, kPickerCols = 5;   // kBandRandomH: the bottom band of a random LFO is lower (the display takes the room)
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    InstanceBar bar;
    SyncSwitch syncSwitch;
    ToggleIcon monoTop;
    DetailButton targets { DetailButton::Icon::Route, "TARGETS" };
    ShapePicker picker;                                     // the shapes (13) or the styles (5)
    ToggleIcon smoothPick, hostPick;                        // in the free cells of the picker's last row
    MenuButton edit { "EDIT", MenuButton::Icon::None };
    // Q group RATE / RANDOM
    Stepper shape, style;
    RateControl rate;
    Knob rise { "RISE" }, smoothTime { "SMOOTH" };
    // Q group TIMING / TRIGGER
    Stepper sync, mode;
    Knob phase { "PHASE" }, delay { "DELAY" };
    // Q group MOTION
    Stepper direction;
    Knob swing { "SWING" }, stereo { "STEREO" };
    ToggleIcon monoQ, stereoToggle;
    // Q group KEY
    NumberField keyTranspose { "TRANSPOSE", NumberField::Format::IntSigned }, keyTune { "TUNE", NumberField::Format::IntSigned };
    QGroup groups[kMaxGroups];

    LfoFullScreen()
    {
        rate.rate.state.large = true;
        monoTop.state.icon = ToggleIcon::Icon::Generic; monoTop.setLabel ("MONO");
        monoQ.state.icon = ToggleIcon::Icon::Generic; monoQ.setLabel ("MONO"); monoQ.state.showState = true;
        stereoToggle.state.icon = ToggleIcon::Icon::Generic; stereoToggle.setLabel ("STEREO"); stereoToggle.state.showState = true;
        smoothPick.state.icon = ToggleIcon::Icon::Generic; smoothPick.setLabel ("SMOOTH");
        hostPick.state.icon = ToggleIcon::Icon::Generic; hostPick.setLabel ("HOST");
        shape.setCaption ("SHAPE"); shape.listable = true; shape.state.wrap = true;
        style.setCaption ("STYLE"); style.listable = true; style.state.wrap = true;
        sync.setCaption ("SYNC"); sync.listable = true; sync.state.wrap = true;
        mode.setCaption ("MODE"); mode.listable = true; mode.state.wrap = true;
        direction.setCaption ("DIRECTION"); direction.listable = true; direction.state.wrap = true;
        keyTranspose.state.min = -60; keyTranspose.state.max = 36; keyTranspose.state.pxPerStep = 8; keyTranspose.state.defaultValue = -12;
        keyTune.state.min = -100; keyTune.state.max = 100; keyTune.state.pxPerStep = 3;
        picker.state.cols = kPickerCols;
        addKid (close); addKid (bar); addKid (syncSwitch); addKid (monoTop); addKid (targets);
        addKid (picker); addKid (smoothPick); addKid (hostPick); addKid (edit);
        addKid (style, false); addKid (rate); addKid (rise); addKid (smoothTime);
        addKid (sync, false); addKid (mode); addKid (phase); addKid (delay);
        addKid (direction); addKid (swing); addKid (stereo); addKid (stereoToggle, false);
        addKid (keyTranspose); addKid (keyTune);
        configure (false);
        setKeyActive (false);
    }
    /** An LFO (shape) or a random LFO (style): the parts that belong to the type are shown, the Q groups are built, the first one is active. */
    void configure (bool random)
    {
        random_ = random;
        picker.state.styles = random;
        picker.state.cols = random ? 2 : kPickerCols;
        show (smoothPick, ! random); show (hostPick, ! random); show (edit, ! random); show (monoTop, ! random);
        show (rise, ! random); show (smoothTime, ! random); show (style, random);
        show (phase, ! random); show (delay, ! random); show (direction, ! random); show (swing, ! random); show (stereo, ! random); show (stereoToggle, random);
        show (mode, true);
        show (shape, false); show (sync, random); show (monoQ, false);
        for (QGroup& g : groups)
        {
            g.setFrameRect ({});
            for (int i = 0; i < 4; ++i)
                g.set (i, nullptr);
        }
        nGroups_ = 0;
        auto next = [&] (const char* name) -> QGroup& { setText (names_[nGroups_], sizeof names_[0], name); return groups[nGroups_++]; };
        if (! random)
        {
            QGroup& r = next ("Rate");
            r.set (0, &picker); r.set (1, &rate); r.set (2, &rise); r.set (3, &smoothTime);
            QGroup& t = next ("Timing");
            t.set (0, &mode); t.set (1, &phase); t.set (2, &delay);
            QGroup& mo = next ("Motion");
            mo.set (0, &direction); mo.set (1, &swing); mo.set (2, &stereo);
            QGroup& k = next ("Key");
            k.set (0, &keyTranspose); k.set (1, &keyTune);
        }
        else
        {
            QGroup& r = next ("Random");
            r.set (0, &style); r.set (1, &rate); r.set (2, &sync); r.set (3, &stereoToggle);
            QGroup& t = next ("Trigger");
            t.set (0, &mode);
            QGroup& k = next ("Key");
            k.set (0, &keyTranspose); k.set (1, &keyTune);
        }
        layout();
        active_ = -1;
        setActiveGroup (0);
    }
    bool random() const noexcept { return random_; }
    /** The key follow sync: only then the KEY block is there (its place goes to the other blocks otherwise: nothing dim and idle on the page). */
    void setKeyActive (bool on)
    {
        const bool changed = on != keyActive_;
        keyActive_ = on;
        show (keyTranspose, on); show (keyTune, on);
        keyTranspose.setEnabled (enabled_ && on); keyTune.setEnabled (enabled_ && on);
        if (! on && active_ == keyGroup())
            setActiveGroup (0);
        if (changed && rect_.w > 0)
            layout();
    }
    bool keyActive() const noexcept { return keyActive_; }
    int keyGroup() const noexcept { return nGroups_ - 1; }
    Rect displayRect() const noexcept { return display_; }
    Rect block (int i) const noexcept { return i >= 0 && i < nBlocks_ ? blocks_[i] : Rect {}; }
    int numBlocks() const noexcept { return nBlocks_; }
    int numGroups() const noexcept { return nGroups_; }
    const char* groupName (int i) const noexcept { return i >= 0 && i < nGroups_ ? names_[i] : ""; }
    int activeGroup() const noexcept { return active_; }
    void setActiveGroup (int i)
    {
        if (i >= 0 && i < nGroups_ && i == keyGroup() && ! keyActive_)
            i = 0;
        for (int k = 0; k < nGroups_; ++k)
            groups[k].setActive (k == i);
        active_ = i >= 0 && i < nGroups_ ? i : -1;
    }
    Change turnQ (int knobIndex, double delta) { return active_ >= 0 ? groups[active_].turn (knobIndex, delta) : Change::none(); }
    Size minSize() const override { return { 1200, 600 }; }
    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)
        {
            if (picker.rect().contains (x, y))
                setActiveGroup (0);
            else
                for (int i = 0; i < nBlocks_; ++i)
                    if (blocks_[i].contains (x, y) && ! (i == keyGroup() && ! keyActive_))
                    {
                        setActiveGroup (i);
                        break;
                    }
        }
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        for (int i = 0; i < nGroups_; ++i)
            if (i != keyGroup() || keyActive_)
                groups[i].draw (sb);
    }
    /** The page of its own tab (the plugin draws the whole tab): there is nothing to close. */
    void setPageMode (bool page)
    {
        pageMode_ = page;
        show (close, ! page);
        if (rect_.w > 0)
            layout();
    }
    int leaves (Component** out, int max) { return collectLeaves (out, 0, max); }

protected:
    void syncKids() override
    {
        Composite::syncKids();
        keyTranspose.setEnabled (enabled_ && keyActive_); keyTune.setEnabled (enabled_ && keyActive_);
    }
    void layout() override
    {
        const int W = rect_.w, m = 8;
        const Rect top { rect_.x + m, rect_.y + 4, W - 2 * m, 56 };
        Rect r[5];
        const int fixed[5] = { pageMode_ ? 0 : 84, (InstanceBar::kLfoCells + InstanceBar::kRndCells) * InstanceBar::kCellW + kGap, 3 * 56 + 2 * 72 + 2 * kGap, 72, 0 }, weight[5] = { 0, 0, 0, 0, 1 };
        layoutRow (top, fixed, weight, 5, kGap, r);
        close.place ({ r[0].x, top.y, r[0].w, 56 });
        bar.place ({ r[1].x, top.y, r[1].w, 56 });
        syncSwitch.place ({ r[2].x, top.y, r[2].w, 56 });
        monoTop.place ({ r[3].x, top.y, r[3].w, 56 });
        targets.place ({ r[4].x, top.y, std::min (r[4].w, 200), 56 });
        const int bandH = random_ ? kBandRandomH : kBandH;
        const int midY = top.bottom() + kQLane, bandY = rect_.bottom() - m - bandH, midH = std::max (0, bandY - m - midY);
        const int colW = std::min (random_ ? 232 : 304, W / 4);
        display_ = { rect_.x + m, midY, W - 3 * m - colW, midH };
        // the picker: three rows of cells (the 13 shapes in five columns; the five styles in two columns fill the height); under it, one row of SMOOTH and HOST; EDIT on the corner of the display
        const int rowsP = random_ ? 1 : 3, pickerH = random_ ? midH : std::min (midH - 56, rowsP * std::max (56, (midH - 56) / rowsP));
        picker.place ({ rect_.right() - m - colW, midY, colW, pickerH });
        {
            const int by = midY + pickerH, bh = std::min (56, std::max (0, midH - pickerH)), hw = (colW - kGap) / 2;
            smoothPick.place ({ picker.rect().x, by, hw, bh });
            hostPick.place ({ picker.rect().x + hw + kGap, by, colW - hw - kGap, bh });
        }
        edit.place ({ display_.right() - 4 - 104, display_.y + 4, 104, 56 });
        // the blocks of the bottom band
        const int n = (random_ ? 3 : 4) - (keyActive_ ? 0 : 1);
        nBlocks_ = n;
        static const int wShape[4] = { 3, 3, 3, 2 }, wRandom[3] = { 4, 2, 2 };   // without the KEY block the others take its place
        const int* wt = random_ ? wRandom : wShape;
        int sum = 0;
        for (int i = 0; i < n; ++i) sum += wt[i];
        const int totalW = W - 2 * m - (n - 1) * m;
        int x = rect_.x + m, used = 0;
        for (int i = 0; i < n; ++i)
        {
            used += wt[i];
            const int right = rect_.x + m + totalW * used / sum + i * m;
            blocks_[i] = { x, bandY, right - x, bandH };
            x = right + m;
        }
        const int pad = 6, titleH = 22, rowH = 56;
        auto inner = [&] (int i) { return Rect { blocks_[i].x + pad, blocks_[i].y + titleH + kQLane, blocks_[i].w - 2 * pad, blocks_[i].h - titleH - kQLane - pad }; };
        auto pair = [&] (const Rect& cell, Component& a, Component& b) { const int cw = (cell.w - kGap) / 2; a.place ({ cell.x, cell.y, cw, cell.h }); b.place ({ cell.x + cw + kGap, cell.y, cell.w - cw - kGap, cell.h }); };
        auto stack2 = [&] (const Rect& cell, Component& a, Component& b)   // two fields one above the other, as tall as the block lets them be (at most 96)
        {
            const int h = std::min (96, (cell.h - kQLane) / 2);
            a.place ({ cell.x, cell.y, cell.w, h });
            b.place ({ cell.x, cell.y + h + kQLane, cell.w, h });
        };
        if (! random_)
        {
            const int topH = (inner (0).h - kQLane) / 2;
            const int botY = inner (0).y + topH + kQLane;
            const int botH = inner (0).bottom() - botY;

            // RATE: the RATE knob is large (the whole height of the block, with its value) on the left; RISE and SMOOTH are the medium knobs of the page (the size of PHASE, DELAY, SWING, STEREO) one above the other on the right
            Rect in0 = inner (0);
            const int rw = in0.w * 11 / 20;
            rate.place ({ in0.x, in0.y, rw, in0.h });
            rise.place ({ in0.x + rw + kGap, in0.y, in0.w - rw - kGap, topH });
            smoothTime.place ({ in0.x + rw + kGap, botY, in0.w - rw - kGap, botH });

            // TIMING: MODE stepper + PHASE + DELAY
            Rect in1 = inner (1);
            mode.place ({ in1.x, in1.y + (topH - rowH) / 2, in1.w, rowH });
            pair ({ in1.x, botY, in1.w, botH }, phase, delay);

            // MOTION: DIRECTION + SWING + STEREO
            Rect in2 = inner (2);
            direction.place ({ in2.x, in2.y + (topH - rowH) / 2, in2.w, rowH });
            pair ({ in2.x, botY, in2.w, botH }, swing, stereo);

            if (keyActive_)
                stack2 (inner (3), keyTranspose, keyTune);
        }
        else
        {
            const int rateH = 72;
            Rect in = inner (0);
            pair ({ in.x, in.y, in.w, rowH }, style, sync);
            pair ({ in.x, in.y + rowH + kQLane, in.w, rateH }, rate, stereoToggle);
            in = inner (1);
            mode.place ({ in.x, in.y + (in.h - rowH) / 2, in.w, rowH });   // the only control of the block: in the middle of it
            if (keyActive_)
                stack2 (inner (2), keyTranspose, keyTune);
        }
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setFrameRect (i < nBlocks_ ? blocks_[i] : Rect {});
    }
    void drawUnder (Pen& p) const override
    {
        p.block (display_);
        for (int i = 0; i < nBlocks_; ++i)
            p.block (blocks_[i], i == active_);
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        static const char* const kShape[4] = { "RATE", "TIMING", "MOTION", "KEY FOLLOW" }, * const kRandom[3] = { "RANDOM", "TRIGGER", "KEY FOLLOW" };
        for (int i = 0; i < nBlocks_; ++i)
        {
            const bool dim = false;
            p.textLeft ({ blocks_[i].x + 10, blocks_[i].y + 4, std::max (0, blocks_[i].w - 20), 18 }, random_ ? kRandom[i] : kShape[i], T.fLabel - 2.0f, p.c (T.textDim, dim ? 110 : 255), T.fMin);
        }
    }
    Rect display_, blocks_[4];
    int nBlocks_ = 4, nGroups_ = 0, active_ = -1;
    bool random_ = false, keyActive_ = false, pageMode_ = false;
    char names_[kMaxGroups][16] = {};
};

}} // namespace synthyboy_mpc::ui
