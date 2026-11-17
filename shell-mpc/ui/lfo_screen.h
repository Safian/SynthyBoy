// SynthyBoy - MPC UI component library: the two views of the LFO ELEMENT (owner approved plan; drawn for the Play tab of 1280 x 628, other sizes scale in proportion): LfoCompact (the third of the width on
// the main page, about 408 x 620) and LfoFullScreen (the whole tab). The element has two types: an LFO (LFO 1 .. 10: a shape of one cycle) and a RANDOM LFO (RND 1 .. 4: a style); `configure (random)`
// swaps the parts and the Q groups, the places stay.
//  - LfoCompact: the instance stepper (the 14 LFOs, a tap on its name opens the list; the same selector as the envelope's and the oscillator's, top row of the third) with the full screen button on the display, the display, and under it the Q group RATE: [SHAPE, RATE or DIVISION, RISE,
//    SMOOTH time] (a random LFO: [STYLE, RATE or DIVISION, SYNC, STEREO]), one frame around the four.
//  - LfoFullScreen: the top bar (close, the instance cells, HZ / BPM / KEY with TRIP and DOT, MONO, the targets button), in the middle the display at the left and the picker of the shapes (the free cells of
//    last row hold SMOOTH and HOST; EDIT is on the corner of the display) or of the styles at the right, and the bottom band of titled blocks, one Q group each: RATE, TIMING, MOTION, KEY (the KEY block works in the key follow sync
//    only); a random LFO: RANDOM, TRIGGER, KEY. A touch that goes down on a control of a Q group makes that group the active one; `turnQ (knob, delta)` routes the hardware knob to it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "carousel.h"
#include "lfo_parts.h"
#include "play_rows.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
class LfoCompact : public Composite
{
public:
    Carousel instance;                                      // LFO 1 .. 10, RND 1 .. 4: the selector of the three thirds (neighbours step, the name opens the list)
    MenuButton full { "", MenuButton::Icon::Expand };      // the full screen button, on the display
    Stepper shape, style, sync;
    RateControl rate;
    Knob rise { "RISE" }, smoothTime { "SMOOTH" };
    LedToggle stereo;
    QGroup group;

    LfoCompact()
    {
        instance.state.wrap = true; instance.listable = true;
        shape.setCaption ("SHAPE"); shape.listable = true; shape.state.wrap = true;
        style.setCaption ("STYLE"); style.listable = true; style.state.wrap = true;
        sync.setCaption ("SYNC"); sync.listable = true; sync.state.wrap = true;
        stereo.setLabel ("STEREO");
        smoothTime.state.led = true;   // the switch of the smoothing is the LED on the label of its time
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
    static constexpr int kQuadH = 248, kQuadRandomH = 304, kRowH = 56, kRateH = 72;   // an LFO: SHAPE, RATE, RISE and SMOOTH (the last two as tall as the rate, the same dial size); a random LFO has two more rows
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
        // an LFO: the display is the rectangle every third of the Play page shares (play_rows.h) and the block starts one chip lane + 4 px under it (the first member, SHAPE, at the common controls line); a random LFO (not on the Play page)
        // has two more rows and takes its room from the display
        const int dy = rect_.y + m + kRowH + 8;
        int qy = rect_.bottom() - m - kQuadRandomH;
        if (random_)
            display_ = { rect_.x + m, dy, W, std::max (0, qy - 8 - dy) };
        else
        {
            display_ = playrows::displayRect (rect_);
            qy = display_.bottom() + 4;
        }
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
        rise.state.side = smoothTime.state.side = true;   // the dial at the left with its name and value beside it: the same dial as the rate's above
        rise.place ({ rect_.x + m, y, cw, h });
        smoothTime.place ({ rect_.x + m + cw + kGap, y, W - cw - kGap, h });
    }
    void drawUnder (Pen& p) const override { p.block (display_); }
    Rect display_;
    bool random_ = false;
};

// ===============================================================================================================================================================
/** LedSwitchProxy: the Q member of the LED on the label of a knob (Knob::state.led): a LED toggle that is not drawn and takes no touch (the knob's own label zone is the touch), placed at the LED so that its Q chip stands there; a Q-Link
    step on it switches (up on, down off) and the owner writes the switch's parameter. It is no child of any composite. */
class LedSwitchProxy : public LedToggle
{
public:
    void draw (gpu::SceneBuilder&) const override {}
    Change touchDown (int, int, double) override { return Change::none(); }
    int touchTargets (Rect*, int) const override { return 0; }
    Size minSize() const override { return { 1, 1 }; }
    bool sizeOk() const noexcept override { return true; }
    /** Places the proxy beside the top left corner of the knob (inside its rectangle): its Q chip stands in the lane above the knob, right of the knob's own chip. */
    void follow (const Knob& k) { place ({ k.rect().x + 24, k.rect().y, 20, 16 }); }
};

// ===============================================================================================================================================================
class LfoFullScreen : public Composite
{
public:
    static constexpr int kMaxGroups = 6, kBandH = 290, kBandRandomH = 196, kTopH = 64, kPickerCols = 5;   // kBandRandomH: the bottom band of a random LFO is lower (the display takes the room)
    MenuButton close { "CLOSE", MenuButton::Icon::None };
    InstanceBar bar;
    SyncSwitch syncSwitch;                                  // the logic of HZ / BPM / KEY (its list `syncSwitch.mode` is the first member of TIMING / RANDOM); TRIP and DOT are `rate.trip` and `rate.dot`
    ToggleIcon monoTop;
    DetailButton targets { DetailButton::Icon::Route, "TARGETS" };
    ShapePicker picker;                                     // the shapes (13) or the styles (5)
    LedToggle hostPick;                                     // under the picker: HOST (a LED toggle); the SMOOTH switch is the LED on the label of the SMOOTH knob (smoothTime), its Q member `smoothSwitch` (group SHAPE)
    LedSwitchProxy smoothSwitch;
    MenuButton edit { "EDIT", MenuButton::Icon::None };
    // Q group RATE / RANDOM
    Stepper shape, style;
    RateControl rate;
    Knob rise { "RISE" }, smoothTime { "SMOOTH" };
    // Q group TIMING / TRIGGER
    Stepper sync, mode;                                     // `mode` (the trigger: six long names): a stepper with its list
    LedList modeList;                                       // the trigger of a random LFO (two options): all visible
    Knob phase { "PHASE" }, delay { "DELAY" };
    // Q group MOTION
    LedList direction;                                      // Forward / Reverse / Ping pong
    Knob swing { "SWING" }, stereo { "STEREO" };
    LedToggle monoQ, stereoToggle;
    // Q group KEY
    NumberField keyTranspose { "TRANSPOSE", NumberField::Format::IntSigned }, keyTune { "TUNE", NumberField::Format::IntSigned };
    QGroup groups[kMaxGroups];

    LfoFullScreen()
    {
        rate.rate.state.large = true;
        rate.setTripDot (true);
        monoTop.state.icon = ToggleIcon::Icon::Generic; monoTop.setLabel ("MONO");
        monoQ.setLabel ("MONO");
        stereoToggle.setLabel ("STEREO");
        smoothTime.state.led = true;   // one control: the smoothing time with the switch of the smoothing on its label
        hostPick.setLabel ("HOST");
        shape.setCaption ("SHAPE"); shape.listable = true; shape.state.wrap = true;
        style.setCaption ("STYLE"); style.listable = true; style.state.wrap = true;
        sync.setCaption ("SYNC"); sync.listable = true; sync.state.wrap = true; sync.minWidth = 140;
        mode.setCaption ("MODE"); mode.listable = true; mode.state.wrap = true; mode.minWidth = 140;
        static const char* const dirNames[3] = { "Forward", "Reverse", "Ping pong" };
        direction.setOptions (dirNames, 3); direction.setTitle ("DIRECTION"); direction.state.titleInLane = true;
        modeList.setTitle ("MODE"); modeList.state.titleInLane = true;
        syncSwitch.mode.setTitle ("SYNC"); syncSwitch.mode.state.titleInLane = true;
        keyTranspose.state.min = -60; keyTranspose.state.max = 36; keyTranspose.state.pxPerStep = 8; keyTranspose.state.defaultValue = -12;
        keyTune.state.min = -100; keyTune.state.max = 100; keyTune.state.pxPerStep = 3;
        picker.state.cols = kPickerCols;
        addKid (close); addKid (bar); addKid (syncSwitch.mode); addKid (monoTop); addKid (targets);
        addKid (picker); addKid (hostPick); addKid (edit);
        addKid (style, false); addKid (rate); addKid (rise); addKid (smoothTime);
        addKid (sync, false); addKid (mode); addKid (modeList, false); addKid (phase); addKid (delay);
        addKid (direction); addKid (swing); addKid (stereo); addKid (monoQ, false); addKid (stereoToggle, false);
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
        show (hostPick, ! random); show (edit, ! random); show (monoTop, false);
        show (rise, ! random); show (smoothTime, ! random); show (style, false);   // the style grid IS the style control (a stepper beside it was the same control twice)
        show (phase, ! random); show (delay, ! random); show (direction, ! random); show (swing, ! random); show (stereo, ! random); show (stereoToggle, random);
        show (mode, ! random); show (modeList, random);
        show (shape, false); show (sync, false); show (monoQ, ! random);   // the HZ / BPM / KEY list is the sync control (and the Q member) of TIMING / RANDOM
        buildGroups();
        layout();
        active_ = -1;
        setActiveGroup (0);
    }
    bool random() const noexcept { return random_; }
    /** The sync value of the parameter: the HZ / BPM / KEY list and the TRIP / DOT toggles show it. */
    void setSync (int sync)
    {
        syncSwitch.setSync (sync);
        rate.setSync (sync);
    }
    /** The key follow sync: the two numbers TRANSPOSE and TUNE (they set the rate) take the place of the RATE knob in the first block (the knob has nothing to say: it is not on the page, no dead control), as their own Q
        group right after the first one; the Q groups are built again, the group that was active stays the active one (by its name). */
    void setKeyActive (bool on)
    {
        const bool changed = on != keyActive_;
        keyActive_ = on;
        show (keyTranspose, on); show (keyTune, on);
        show (rate, ! on);
        keyTranspose.setEnabled (enabled_ && on); keyTune.setEnabled (enabled_ && on);
        if (! changed)
            return;
        char was[sizeof names_[0]] = {};
        if (active_ >= 0 && active_ < nGroups_)
            setText (was, sizeof was, names_[active_]);
        buildGroups();
        int again = 0;
        for (int i = 0; i < nGroups_; ++i)
            if (std::strcmp (names_[i], was) == 0)
                again = i;
        setActiveGroup (again);
        if (rect_.w > 0)
            layout();
    }
    bool keyActive() const noexcept { return keyActive_; }
    int keyGroup() const noexcept { return keyGroup_; }
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
        int hit = -1;
        for (int i = 0; i < nGroups_ && hit < 0; ++i)   // a member of a group: that group
            for (int k = 0; k < QGroup::kSlots; ++k)
                if (cap_ != nullptr && groups[i].member (k) == cap_)
                    hit = i;
        if (hit < 0)   // not a member: the block it is in
            for (int g = 0; g < nGroups_ && hit < 0; ++g)
                if (blockOfGroup_[g] >= 0 && blocks_[blockOfGroup_[g]].contains (x, y) && ! (g == keyGroup() && ! keyActive_))
                    hit = g;
        if (hit >= 0)
            setActiveGroup (hit);
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        for (int i = 0; i < nGroups_; ++i)
        {
            if (i == keyGroup() && ! keyActive_)
                continue;
            if (i == active_)
                groups[i].draw (sb);
            else
                groups[i].drawIdle (sb);
        }
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
    /** The Q groups of the page in reading order (the blocks left to right, the members of a block top to bottom): the shape page RATE, [KEY], TIMING, MOTION, SHAPE; a random LFO RANDOM, [KEY], TRIGGER. The KEY group
        (TRANSPOSE and TUNE, in the first block where the rate knob is not) is there in the key follow sync only. */
    void buildGroups()
    {
        for (QGroup& g : groups)
        {
            g.setFrameRect ({});
            g.setFrameless (false);
            for (int i = 0; i < 4; ++i)
                g.set (i, nullptr);
        }
        for (int& b : blockOfGroup_)
            b = -1;
        nGroups_ = 0;
        keyGroup_ = -1;
        auto next = [&] (const char* name, int block) -> QGroup& { setText (names_[nGroups_], sizeof names_[0], name); blockOfGroup_[nGroups_] = block; return groups[nGroups_++]; };
        auto key = [&] (int block)
        {
            if (! keyActive_)
                return;
            keyGroup_ = nGroups_;
            QGroup& k = next ("Key", block);
            k.set (0, &keyTranspose); k.set (1, &keyTune);
            k.setFrameless (true);   // two groups in one block: the chips mark them
        };
        if (! random_)
        {
            QGroup& r = next ("Rate", 0);
            r.set (0, &picker); r.set (1, keyActive_ ? nullptr : static_cast<Component*> (&rate)); r.set (2, &rise); r.set (3, &smoothTime);
            r.setFrameless (true);   // the shape grid (Q 1) and the RATE block lie apart: one quad is marked by its four chips and the Q colour, not by a frame around everything between them
            key (0);
            QGroup& t = next ("Timing", 1);
            t.set (0, &syncSwitch.mode); t.set (1, &phase); t.set (2, &delay); t.set (3, &mode);   // reading order of the block: the list and the knob over it, the stepper under
            QGroup& mo = next ("Motion", 2);
            mo.set (0, &direction); mo.set (1, &swing); mo.set (2, &stereo); mo.set (3, &monoQ);
            QGroup& sh = next ("Shape", -1);
            sh.set (0, &hostPick); sh.set (1, &smoothSwitch);
            sh.setFrameless (true);
        }
        else
        {
            QGroup& r = next ("Random", 0);
            r.set (0, &picker); r.set (1, keyActive_ ? nullptr : static_cast<Component*> (&rate)); r.set (2, &syncSwitch.mode); r.set (3, &stereoToggle);
            r.setFrameless (true);   // the style grid (Q 1) lies apart from the block: marked by its chips
            key (0);
            QGroup& t = next ("Trigger", 0);
            t.set (0, &modeList);
            t.setFrameless (true);   // two groups in one block: the chips mark them
        }
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setActive (false);
        if (rect_.w > 0)
            layout();
    }
    void syncKids() override
    {
        Composite::syncKids();
        keyTranspose.setEnabled (enabled_ && keyActive_); keyTune.setEnabled (enabled_ && keyActive_);
    }
    void layout() override
    {
        const int W = rect_.w, m = 8, g = kGap;
        const Rect top { rect_.x + m, rect_.y + 2 + kQLane, W - 2 * m, 56 };   // the lane above holds nothing now (HZ / BPM / KEY moved into the TIMING / RANDOM block); the bar keeps the place
        const int closeW = pageMode_ ? 0 : 84, barW = (InstanceBar::kLfoCells + InstanceBar::kRndCells) * InstanceBar::kCellW + g;
        close.place ({ top.x, top.y, closeW, 56 });
        bar.place ({ top.x + (closeW > 0 ? closeW + g : 0), top.y, barW, 56 });
        syncSwitch.mode.place ({});   // placed by the block (below)
        monoTop.place ({});
        sync.place ({});   // gone from the page (see configure)
        // the open-the-editor button at the right end of the top bar, over the shape picker, for every shape; the TARGETS button at the very end
        const int editW = random_ ? 0 : 104, targetsW = 200;
        targets.place ({ top.right() - targetsW, top.y, targetsW, 56 });
        edit.place (random_ ? Rect {} : Rect { top.right() - targetsW - g - editW, top.y, editW, 56 });
        const int bandH = random_ ? kBandRandomH : kBandH;
        const int midY = top.bottom() + 15, bandY = rect_.bottom() - m - bandH, midH = std::max (0, bandY - m - midY);
        const int colW = std::min (random_ ? 232 : 304, W / 4);
        display_ = { rect_.x + m, midY, W - 3 * m - colW, midH };
        // the picker: three rows of cells (the 13 shapes in five columns; the five styles in two columns fill the height); under it, HOST
        const int rowsP = random_ ? 1 : 3, pickerH = random_ ? midH : rowsP * 56;
        picker.place ({ rect_.right() - m - colW, midY, colW, pickerH });
        {
            const int by = midY + pickerH + 15, bh = 56;
            hostPick.place ({ picker.rect().x, by, colW, bh });
        }
        // the blocks of the bottom band
        const int n = random_ ? 1 : 3;   // the key follow has no block of its own: its two numbers stand in the RATE block
        nBlocks_ = n;
        static const int wShape[3] = { 3, 4, 3 }, wRandom[1] = { 1 };
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
        const int pad = 6, titleH = 22, th = led::kRowH;
        auto inner = [&] (int i) { return Rect { blocks_[i].x + pad, blocks_[i].y + titleH + kQLane, blocks_[i].w - 2 * pad, blocks_[i].h - titleH - kQLane - pad }; };
        auto stack2 = [&] (const Rect& cell, Component& a, Component& b)   // two fields one above the other, as tall as the block lets them be (at most 96)
        {
            const int h = std::min (96, (cell.h - kQLane) / 2);
            a.place ({ cell.x, cell.y, cell.w, h });
            b.place ({ cell.x, cell.y + h + kQLane, cell.w, h });
        };
        if (! random_)
        {
            // one size for the medium knobs of the page (RISE, SMOOTH, PHASE, DELAY, SWING, STEREO): the dial at the left with its name and value beside it when the cell is wide enough (172 px, the minimum of a side knob),
            // over the texts when it is not (three knobs in a row of the MOTION block)
            const int kKnobH = 114;
            auto medium = [&] (Knob& k, const Rect& cell) { k.state.side = cell.w >= 172; k.place (cell); };

            // RATE: the RATE knob is large (the whole height of the block, with its value) on the left; RISE and SMOOTH one above the other on the right, medium. In the BPM mode the cell of the rate holds the division
            // and TRIP / DOT under it
            Rect in0 = inner (0);
            const int rw = std::min (in0.w * 45 / 100, in0.w - 172 - kGap);
            rate.place ({ in0.x, in0.y, rw, in0.h });
            if (keyActive_)   // the key follow: the two numbers that set the rate stand where the rate knob stands
                stack2 ({ in0.x, in0.y, rw, in0.h }, keyTranspose, keyTune);
            const int sx = in0.x + rw + kGap, sw = in0.w - rw - kGap;
            medium (rise, { sx, in0.y, sw, kKnobH });
            medium (smoothTime, { sx, in0.bottom() - kKnobH, sw, kKnobH });
            smoothSwitch.follow (smoothTime);

            // TIMING: at the left HZ / BPM / KEY (a LED list of three rows) with the trigger MODE (a stepper: six long names) under it; at the right PHASE over DELAY (medium knobs: the same size as RISE and SMOOTH).
            // The chips read top to bottom: SYNC, PHASE, DELAY, MODE
            {
                const Rect in = inner (1);
                const int lw = 150, kx = in.x + lw + g, kw = in.w - lw - g;
                syncSwitch.mode.place ({ in.x, in.y, lw, 3 * th });
                mode.place ({ in.x, in.y + 3 * th + kQLane, lw, in.h - 3 * th - kQLane });
                medium (phase, { kx, in.y, kw, kKnobH });
                medium (delay, { kx, in.bottom() - kKnobH, kw, kKnobH });
            }

            // MOTION: at the left the direction (a LED list of three rows) with MONO under it, at the right SWING over STEREO (medium knobs). The chips read top to bottom: DIRECTION, SWING, STEREO, MONO
            {
                const Rect in = inner (2);
                const int lw = 120, kx = in.x + lw + g, kw = in.w - lw - g;
                direction.place ({ in.x, in.y, lw, 3 * th });
                medium (swing, { kx, in.y, kw, kKnobH });
                medium (stereo, { kx, in.bottom() - kKnobH, kw, kKnobH });
                monoQ.place ({ in.x, in.y + 3 * th + kQLane, lw, in.h - 3 * th - kQLane });
            }

        }
        else
        {
            // RANDOM: the RATE knob is large (the whole height of the block) on the left, as on the LFO page; at its right in ONE row HZ / BPM / KEY (a LED list of three, in one row), STEREO and the trigger (a LED list of two);
            // the rectangles are as tall as two rows less one (a list keeps its options in one row there) and as wide as the block lets them be, so that the chips of the four members stand on one line
            Rect in = inner (0);
            const int rw = std::min (220, in.w * 28 / 100), lh = 2 * th - 8;
            rate.place ({ in.x, in.y, rw, in.h });
            if (keyActive_)
                stack2 ({ in.x, in.y, rw, in.h }, keyTranspose, keyTune);
            const int x0 = in.x + rw + kGap, rest = in.right() - x0 - 2 * kGap;
            const int syncW = std::max (3 * 84, rest * 40 / 100), stereoW = std::max (112, rest * 18 / 100);
            syncSwitch.mode.place ({ x0, in.y, syncW, lh });
            stereoToggle.place ({ x0 + syncW + kGap, in.y, stereoW, lh });
            modeList.place ({ x0 + syncW + stereoW + 2 * kGap, in.y, in.right() - (x0 + syncW + stereoW + 2 * kGap), lh });
        }
        for (int i = 0; i < kMaxGroups; ++i)
            groups[i].setFrameRect (i < nGroups_ && blockOfGroup_[i] >= 0 && blockOfGroup_[i] < nBlocks_ && ! groups[i].frameless() ? blocks_[blockOfGroup_[i]] : Rect {});
    }
    void drawUnder (Pen& p) const override
    {
        p.block (display_);
        const int activeBlock = active_ >= 0 && active_ < nGroups_ ? blockOfGroup_[active_] : -1;
        for (int i = 0; i < nBlocks_; ++i)
            p.block (blocks_[i], i == activeBlock);
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        static const char* const kShape[3] = { "RATE", "TIMING", "MOTION" }, * const kRandom[1] = { "RANDOM" };
        for (int i = 0; i < nBlocks_; ++i)
        {
            const char* title = random_ ? kRandom[i] : kShape[i];
            if (i == 0 && keyActive_)
                title = random_ ? "RANDOM - RATE FOLLOWS THE KEY" : "RATE - FOLLOWS THE KEY";
            p.textLeft ({ blocks_[i].x + 10, blocks_[i].y + 4, std::max (0, blocks_[i].w - 20), 18 }, title, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        }
    }
    Rect display_, blocks_[4];
    int nBlocks_ = 3, nGroups_ = 0, active_ = -1, keyGroup_ = -1, blockOfGroup_[kMaxGroups] = {};
    bool random_ = false, keyActive_ = false, pageMode_ = false;
    char names_[kMaxGroups][16] = {};
};

}} // namespace synthyboy_mpc::ui
