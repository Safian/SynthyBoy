// SynthyBoy - MPC UI component library: ModSourceBoard, the quick-add board of the modulation matrix. It fills the page while no modulation exists: every modulation source as a chip, the chips in
// groups (a caption at the left of each group, the chips flow along the row and wrap), one tap on a chip asks the owner to make a modulation from that source. The board knows nothing of the
// engine: the owner hands it the sources (`add`: the id the owner uses, the label, the name that picks the colour, the group) and gets the id of the tapped one back. Same contract as every
// component (core.h): a state in (the sources), a rectangle, a change out (`Index`: `index` = the id of the source that was tapped, `value` = the same as a double).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Layout: a one line headline, then the groups top down. A chip is at least 56 pixels high and 64 wide (a touch target of its own, a gap between neighbours); its width follows its label. The groups
// are separated by a faint line, the rows of a group by the gap. When the sources need more height than the rectangle has, the gaps shrink (never the chips).
#pragma once

#include "core.h"

namespace synthyboy_mpc { namespace ui {

class ModSourceBoard : public Component
{
public:
    static constexpr int kMaxSources = 64, kMaxGroups = 8;
    static constexpr int kChipH = 56, kChipMinW = 64, kChipMaxW = 260, kHeadH = 44, kCaptionW = 120, kPad = 12;

    ModSourceBoard() = default;

    /** Forgets the sources and the group captions. */
    void clear()
    {
        n_ = 0;
        for (char* c : captions_)
            c[0] = '\0';
        layout();
    }
    void setGroupName (int group, const char* name)
    {
        if (group >= 0 && group < kMaxGroups)
            setText (captions_[group], sizeof captions_[group], name);
    }
    /** One source: the owner's id, the text on the chip, the name that picks the colour of its bar (theme::sourceColour), the group (0 .. kMaxGroups - 1; the groups are drawn in this order). false when full. */
    bool add (int id, const char* label, const char* colourName, int group)
    {
        if (n_ >= kMaxSources || group < 0 || group >= kMaxGroups)
            return false;
        Chip& c = chips_[n_++];
        c.id = id;
        c.group = group;
        setText (c.label, sizeof c.label, label);
        setText (c.colourName, sizeof c.colourName, colourName);
        layout();
        return true;
    }
    int numSources() const noexcept { return n_; }
    int sourceId (int i) const noexcept { return i >= 0 && i < n_ ? chips_[i].id : 0; }
    const char* sourceLabel (int i) const noexcept { return i >= 0 && i < n_ ? chips_[i].label : ""; }
    int sourceGroup (int i) const noexcept { return i >= 0 && i < n_ ? chips_[i].group : -1; }
    /** The rectangle of the chip of the source number i (in the order of `add`); empty when it does not exist. */
    Rect chipRect (int i) const noexcept { return i >= 0 && i < n_ ? chips_[i].r : Rect {}; }
    /** The rectangle of the chip of the source with the id `id` (empty: none). */
    Rect chipRectOfId (int id) const noexcept
    {
        for (int i = 0; i < n_; ++i)
            if (chips_[i].id == id)
                return chips_[i].r;
        return {};
    }
    /** How many groups have at least one source. */
    int numGroups() const noexcept
    {
        int g = 0;
        for (int k = 0; k < kMaxGroups; ++k)
            g += groupCount (k) > 0;
        return g;
    }
    /** The height the board needs at width `w` at the full gaps (the owner can check it against the room it has). */
    int heightFor (int w) const noexcept { return needed (w, kRowGap, kGroupGap); }
    bool fits() const noexcept { return rect_.h >= needed (rect_.w, kMinRowGap, kMinGroupGap); }

    Size minSize() const override { return { 900, needed (900, kMinRowGap, kMinGroupGap) }; }

    void place (const Rect& r) override { rect_ = r; layout(); }

    int touchTargets (Rect* out, int max) const override
    {
        int k = 0;
        for (int i = 0; i < n_ && k < max; ++i)
            out[k++] = chips_[i].r;
        return k;
    }

protected:
    void drawBody (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.textLeft ({ rect_.x + kPad, rect_.y + 4, std::max (0, rect_.w - 2 * kPad), kHeadH - 8 }, "TAP A SOURCE TO START A MODULATION", T.fLabel + 2.0f, p.c (T.textDim, 255), T.fMin);
        int lastBottom = -1;
        for (int g = 0; g < kMaxGroups; ++g)
        {
            if (groupCount (g) == 0)
                continue;
            int top = 1 << 30, bottom = 0;
            for (int i = 0; i < n_; ++i)
                if (chips_[i].group == g)
                {
                    top = std::min (top, chips_[i].r.y);
                    bottom = std::max (bottom, chips_[i].r.bottom());
                }
            if (lastBottom >= 0)
            {
                const float y = static_cast<float> ((lastBottom + top) / 2);
                p.line (static_cast<float> (rect_.x + kPad), y, static_cast<float> (rect_.right() - kPad), y, 1.0f, p.c (T.keyline, 80));
            }
            lastBottom = bottom;
            char cap[40];
            std::snprintf (cap, sizeof cap, "%s", captions_[g]);
            p.textLeft ({ rect_.x + kPad, top + 2, kCaptionW - 12, 28 }, cap, T.fLabel + 1.0f, p.c (T.text, 255), T.fMin);
            char cnt[16];
            std::snprintf (cnt, sizeof cnt, "%d", groupCount (g));
            p.textLeft ({ rect_.x + kPad, top + 32, kCaptionW - 12, 20 }, cnt, T.fSmall, p.c (T.textFaint, 255), T.fMin);
        }
        for (int i = 0; i < n_; ++i)
        {
            const Chip& c = chips_[i];
            const bool down = pressed_ == i && captured_;
            p.control (c.r, false, down);
            const uint32_t col = theme::sourceColour (c.colourName);
            p.rrect ({ c.r.x + 8, c.r.y + 14, 4, c.r.h - 28 }, 2.0f, p.c (col, 255));
            p.textCentered ({ c.r.x + 16, c.r.y + 2, std::max (0, c.r.w - 20), c.r.h - 4 }, c.label, T.fLabel + 1.0f, p.c (T.text, 255), T.fMin);
            if (down)
                p.rrect (c.r, T.rControl, p.c (col, 255), 1.6f);
        }
    }
    Change onDown (int x, int y, double now) override
    {
        pressed_ = -1;
        for (int i = 0; i < n_; ++i)
            if (chips_[i].r.contains (x, y))
                pressed_ = i;
        tap_.down (x, y, now);
        return Change::none();
    }
    Change onMove (int x, int y) override
    {
        tap_.move (x, y);
        return Change::none();
    }
    Change onUp (int x, int y) override
    {
        const int i = pressed_;
        pressed_ = -1;
        if (i < 0 || tap_.up() == 0 || ! chips_[i].r.contains (x, y))
            return Change::none();
        return Change::indexed (id_, chips_[i].id, static_cast<double> (chips_[i].id));
    }

private:
    static constexpr int kChipGap = 6, kRowGap = 8, kGroupGap = 14, kMinRowGap = 6, kMinGroupGap = 8;
    struct Chip
    {
        int id = 0, group = 0;
        char label[28] = {}, colourName[12] = {};
        Rect r;
    };

    int groupCount (int g) const noexcept
    {
        int k = 0;
        for (int i = 0; i < n_; ++i)
            k += chips_[i].group == g;
        return k;
    }
    int chipWidth (const Chip& c) const noexcept
    {
        const float tw = Pen::textWidth (c.label, theme::L().fLabel + 1.0f);
        return std::clamp (static_cast<int> (std::ceil (tw)) + 26, kChipMinW, kChipMaxW);
    }
    /** The rows a group takes at the width `w`: the chips flow from the caption column to the right edge. */
    int groupRows (int g, int w) const noexcept
    {
        const int x0 = kPad + kCaptionW, x1 = w - kPad;
        int rows = 0, x = x0;
        bool any = false;
        for (int i = 0; i < n_; ++i)
        {
            if (chips_[i].group != g)
                continue;
            const int cw = chipWidth (chips_[i]);
            if (! any || x + cw > x1)
            {
                rows += 1;
                x = x0;
            }
            any = true;
            x += cw + kChipGap;
        }
        return rows;
    }
    int needed (int w, int rowGap, int groupGap) const noexcept
    {
        int h = kHeadH, groups = 0;
        for (int g = 0; g < kMaxGroups; ++g)
        {
            const int rows = groupRows (g, w);
            if (rows == 0)
                continue;
            h += rows * kChipH + (rows - 1) * rowGap + (groups > 0 ? groupGap : 0);
            ++groups;
        }
        return h + kPad;
    }
    void layout()
    {
        if (rect_.empty())
            return;
        int rowGap = kRowGap, groupGap = kGroupGap;
        if (needed (rect_.w, rowGap, groupGap) > rect_.h)
        {
            rowGap = kMinRowGap;
            groupGap = kMinGroupGap;
        }
        const int x0 = rect_.x + kPad + kCaptionW, x1 = rect_.right() - kPad;
        int y = rect_.y + kHeadH;
        bool firstGroup = true;
        for (int g = 0; g < kMaxGroups; ++g)
        {
            if (groupCount (g) == 0)
                continue;
            if (! firstGroup)
                y += groupGap;
            firstGroup = false;
            int x = x0;
            bool any = false;
            for (int i = 0; i < n_; ++i)
            {
                if (chips_[i].group != g)
                    continue;
                const int cw = chipWidth (chips_[i]);
                if (any && x + cw > x1)
                {
                    x = x0;
                    y += kChipH + rowGap;
                }
                any = true;
                chips_[i].r = { x, y, cw, kChipH };
                x += cw + kChipGap;
            }
            y += kChipH;
        }
    }

    Chip chips_[kMaxSources];
    int n_ = 0;
    char captions_[kMaxGroups][24] = {};
    int pressed_ = -1;
    TapGesture tap_;
};

}} // namespace synthyboy_mpc::ui
