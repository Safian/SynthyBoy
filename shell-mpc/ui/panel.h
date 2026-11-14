// SynthyBoy - MPC UI component library: KnobPanel (the frame of the lower knob row: one component, three presets given as DATA, not as three hand written layouts) and QGroup (the four
// values of one Q-Link set: the frame around the group, the index at each member, the routing of the four hardware knobs, the adjacency check).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "composite.h"

namespace synthyboy_mpc { namespace ui {

// ===============================================================================================================================================================
/** The place of a part in a preset: the owner finds a child by it (`KnobPanel::part (slot)`). */
enum class PanelSlot : uint8_t { WtPos, Unison, Detune, Blend, Warp, Pan, Level, WarpMenu, Scan, Dens, Length, Offset, Dir, Pitch, Filter,
                                 Mode, Stack, Width, Range, WtSpread, Warp1Spread, Warp2Spread, Amount, AmountRand, Skew, SkewRand, Shape, Count };
enum class PartKind : uint8_t { Knob, RandPair, Unison, WarpPair, Menu, Stepper };

/** One part of a preset: what, which slot, its label, and the cells it covers in the grid. */
struct PanelItem
{
    PartKind kind;
    PanelSlot slot;
    const char* label;
    uint8_t col, row, colSpan, rowSpan;
    bool bipolar;
};

/** A preset: the grid, the column before which the separator stands (-1: none; the column after it is the separated one), and the parts. */
/** The four values of one Q group by slot (Count = an empty place). */
struct QSpec { PanelSlot slot[4]; };

struct PanelPreset
{
    const char* name;
    int cols, rows, separatorBefore;
    const PanelItem* items;
    int numItems;
    const QSpec* q = nullptr;   // the Q groups of the preset (a DetailPage activates them one at a time)
    int numQ = 0;
    int displayH = 0;           // a DetailPage: the height of the display area above the grid (0: none)
};

namespace presets
{
// wavetable: WT POS, the unison box, DETUNE, BLEND in a row; the warp pair below; the separated right column PAN over LEVEL
constexpr PanelItem kWavetableItems[] = {
    { PartKind::Knob, PanelSlot::WtPos, "WT POS", 0, 0, 1, 1, false },
    { PartKind::Unison, PanelSlot::Unison, "", 1, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Detune, "DETUNE", 2, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Blend, "BLEND", 3, 0, 1, 1, false },
    { PartKind::WarpPair, PanelSlot::Warp, "", 0, 1, 4, 1, false },
    { PartKind::Knob, PanelSlot::Pan, "PAN", 4, 0, 1, 1, true },
    { PartKind::Knob, PanelSlot::Level, "LEVEL", 4, 1, 1, 1, false },
};
// granular: 2 rows x 6: WARP menu, SCAN, DENS, LENGTH, PAN, LEVEL / OFFSET, DIR, PITCH, and the RAND knobs under LENGTH, PAN, LEVEL (a KnobRandPair spans both rows)
constexpr PanelItem kGranularItems[] = {
    { PartKind::Menu, PanelSlot::WarpMenu, "WARP", 0, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Scan, "SCAN", 1, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Dens, "DENS", 2, 0, 1, 1, false },
    { PartKind::RandPair, PanelSlot::Length, "LENGTH", 3, 0, 1, 2, false },
    { PartKind::RandPair, PanelSlot::Pan, "PAN", 4, 0, 1, 2, true },
    { PartKind::RandPair, PanelSlot::Level, "LEVEL", 5, 0, 1, 2, false },
    { PartKind::Knob, PanelSlot::Offset, "OFFSET", 0, 1, 1, 1, false },
    { PartKind::Knob, PanelSlot::Dir, "DIR", 1, 1, 1, 1, false },
    { PartKind::Knob, PanelSlot::Pitch, "PITCH", 2, 1, 1, 1, true },
};
// noise: FILTER, PAN, LEVEL
constexpr PanelItem kNoiseItems[] = {
    { PartKind::Knob, PanelSlot::Filter, "FILTER", 0, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Pan, "PAN", 1, 0, 1, 1, true },
    { PartKind::Knob, PanelSlot::Level, "LEVEL", 2, 0, 1, 1, false },
};
constexpr PanelPreset kWavetable { "wavetable", 5, 2, 4, kWavetableItems, static_cast<int> (sizeof kWavetableItems / sizeof kWavetableItems[0]) };
constexpr PanelPreset kGranular { "granular", 6, 2, -1, kGranularItems, static_cast<int> (sizeof kGranularItems / sizeof kGranularItems[0]) };
constexpr PanelPreset kNoise { "noise", 3, 1, -1, kNoiseItems, static_cast<int> (sizeof kNoiseItems / sizeof kNoiseItems[0]) };
// DetailPage presets: the unison settings of the wavetable oscillator (MODE and STACK steppers, five knobs) and the grain window of the granular oscillator
constexpr PanelItem kUnisonDetailItems[] = {
    { PartKind::Stepper, PanelSlot::Mode, "MODE", 0, 0, 1, 1, false },
    { PartKind::Stepper, PanelSlot::Stack, "STACK", 1, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Width, "WIDTH", 0, 1, 1, 1, false },
    { PartKind::Knob, PanelSlot::Range, "RANGE", 1, 1, 1, 1, false },
    { PartKind::Knob, PanelSlot::WtSpread, "WT POS", 2, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Warp1Spread, "WARP 1", 3, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Warp2Spread, "WARP 2", 2, 1, 1, 1, false },
};
constexpr QSpec kUnisonDetailQ[] = { { { PanelSlot::Mode, PanelSlot::Stack, PanelSlot::Width, PanelSlot::Range } },
                                     { { PanelSlot::WtSpread, PanelSlot::Warp1Spread, PanelSlot::Warp2Spread, PanelSlot::Count } } };
constexpr PanelItem kGrainDetailItems[] = {
    { PartKind::Knob, PanelSlot::Amount, "AMOUNT", 0, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::AmountRand, "RAND", 1, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Skew, "SKEW", 2, 0, 1, 1, true },
    { PartKind::Knob, PanelSlot::SkewRand, "RAND", 3, 0, 1, 1, false },
    { PartKind::Stepper, PanelSlot::Shape, "SHAPE", 0, 1, 2, 1, false },
};
constexpr QSpec kGrainDetailQ[] = { { { PanelSlot::Amount, PanelSlot::AmountRand, PanelSlot::Skew, PanelSlot::SkewRand } },
                                    { { PanelSlot::Shape, PanelSlot::Count, PanelSlot::Count, PanelSlot::Count } } };
constexpr PanelPreset kUnisonDetail { "unison details", 4, 2, -1, kUnisonDetailItems, 7, kUnisonDetailQ, 2, 0 };
constexpr PanelPreset kGrainDetail { "grain window", 4, 2, -1, kGrainDetailItems, 5, kGrainDetailQ, 2, 140 };
}

/** 13. KnobPanel: lays out the parts of a preset on a grid inside its rectangle (equal columns and rows, kGap between the cells, a wider gap and a thin line at the separator). The
    owner finds the parts by slot, gives their ids and writes their states; the panel's own state is the preset. Same contract as every component. */
class KnobPanel : public Composite
{
public:
    static constexpr int kMaxKnobs = 12, kMaxPairs = 3, kMaxItems = 12, kMaxSteppers = 4;
    static constexpr int kSeparatorGap = 16;

    KnobPanel() = default;
    explicit KnobPanel (const PanelPreset& p) { load (p); }

    /** Switches to a preset: every part comes back to its defaults (labels from the table, no ids). */
    void load (const PanelPreset& p)
    {
        clearKids();
        preset_ = &p;
        int nk = 0, np = 0, nm = 0, ns = 0;
        nItems_ = 0;
        for (int i = 0; i < p.numItems && nItems_ < kMaxItems; ++i)
        {
            const PanelItem& it = p.items[i];
            Item& m = items_[nItems_++];
            m.def = &it;
            m.comp = nullptr;
            switch (it.kind)
            {
                case PartKind::Knob:
                    if (nk < kMaxKnobs)
                    {
                        Knob& k = knobs_[nk++];
                        k.state = Knob::State {};
                        k.setLabel (it.label);
                        k.state.bipolar = it.bipolar;
                        k.state.defaultValue = it.bipolar ? 0.5 : 0.0;
                        k.state.value = k.state.modValue = k.state.defaultValue;
                        k.setId (0);
                        m.comp = &k;
                    }
                    break;
                case PartKind::RandPair:
                    if (np < kMaxPairs)
                    {
                        KnobRandPair& q = pairs_[np++];
                        q.main.state = Knob::State {};
                        q.main.setLabel (it.label);
                        q.main.state.bipolar = it.bipolar;
                        q.main.state.defaultValue = it.bipolar ? 0.5 : 0.0;
                        q.main.state.value = q.main.state.modValue = q.main.state.defaultValue;
                        m.comp = &q;
                    }
                    break;
                case PartKind::Stepper:
                    if (ns < kMaxSteppers)
                    {
                        Stepper& st = steppers_[ns++];
                        st.state = Stepper::State {};
                        st.setItem (it.label);
                        m.comp = &st;
                    }
                    break;
                case PartKind::Unison: m.comp = &unison_; break;
                case PartKind::WarpPair: m.comp = &warp_; break;
                case PartKind::Menu:
                    if (nm < 1)
                    {
                        menu_.state = MenuButton::State {};
                        menu_.setLabel (it.label);
                        m.comp = &menu_;
                        ++nm;
                    }
                    break;
            }
            if (m.comp)
                addKid (*m.comp);
        }
        layout();
    }
    const PanelPreset* preset() const noexcept { return preset_; }
    int numParts() const noexcept { return nItems_; }

    /** The part of a slot (null when the preset has none). */
    Component* part (PanelSlot s) const noexcept
    {
        for (int i = 0; i < nItems_; ++i)
            if (items_[i].def->slot == s)
                return items_[i].comp;
        return nullptr;
    }
    Knob* knob (PanelSlot s) const noexcept
    {
        for (int i = 0; i < nItems_; ++i)
            if (items_[i].def->slot == s && items_[i].def->kind == PartKind::Knob)
                return static_cast<Knob*> (items_[i].comp);
        return nullptr;
    }
    Stepper* stepper (PanelSlot s) const noexcept
    {
        for (int i = 0; i < nItems_; ++i)
            if (items_[i].def->slot == s && items_[i].def->kind == PartKind::Stepper)
                return static_cast<Stepper*> (items_[i].comp);
        return nullptr;
    }
    KnobRandPair* pair (PanelSlot s) const noexcept
    {
        for (int i = 0; i < nItems_; ++i)
            if (items_[i].def->slot == s && items_[i].def->kind == PartKind::RandPair)
                return static_cast<KnobRandPair*> (items_[i].comp);
        return nullptr;
    }
    MenuButton* menu() const noexcept { return part (PanelSlot::WarpMenu) ? const_cast<MenuButton*> (&menu_) : nullptr; }
    UnisonBox* unison() const noexcept { return part (PanelSlot::Unison) ? const_cast<UnisonBox*> (&unison_) : nullptr; }
    WarpPair* warp() const noexcept { return part (PanelSlot::Warp) ? const_cast<WarpPair*> (&warp_) : nullptr; }
    /** The rectangle a part got (the cells it covers). */
    Rect cellRect (PanelSlot s) const noexcept { const Component* c = part (s); return c ? c->rect() : Rect {}; }

    Size minSize() const override
    {
        if (! preset_)
            return {};
        int colW = 0, rowH = 0;
        for (int i = 0; i < nItems_; ++i)
        {
            if (! items_[i].comp)
                continue;
            const Size m = items_[i].comp->minSize();
            const PanelItem& it = *items_[i].def;
            colW = std::max (colW, (m.w - (it.colSpan - 1) * kGap + it.colSpan - 1) / it.colSpan);
            rowH = std::max (rowH, (m.h - (it.rowSpan - 1) * kGap + it.rowSpan - 1) / it.rowSpan);
        }
        return { colW * preset_->cols + kGap * (preset_->cols - 1) + (preset_->separatorBefore >= 0 ? kSeparatorGap - kGap : 0) + 2 * kMargin,
                 rowH * preset_->rows + kGap * (preset_->rows - 1) + 2 * kMargin };
    }
    /** The x of the separator line (-1: none). */
    int separatorX() const noexcept { return sepX_; }

protected:
    static constexpr int kMargin = 4;
    struct Item { const PanelItem* def = nullptr; Component* comp = nullptr; };

    void layout() override
    {
        sepX_ = -1;
        if (! preset_)
            return;
        const PanelPreset& p = *preset_;
        const Rect in = rect_.inset (kMargin);
        const int extra = p.separatorBefore >= 0 ? kSeparatorGap - kGap : 0;
        const int cw = std::max (0, (in.w - kGap * (p.cols - 1) - extra) / p.cols), ch = std::max (0, (in.h - kGap * (p.rows - 1)) / p.rows);
        auto colX = [&] (int c) { return in.x + c * (cw + kGap) + (p.separatorBefore >= 0 && c >= p.separatorBefore ? extra : 0); };
        auto rowY = [&] (int r) { return in.y + r * (ch + kGap); };
        for (int i = 0; i < nItems_; ++i)
        {
            const PanelItem& it = *items_[i].def;
            if (! items_[i].comp)
                continue;
            const int x0 = colX (it.col), y0 = rowY (it.row), x1 = colX (it.col + it.colSpan - 1) + cw, y1 = rowY (it.row + it.rowSpan - 1) + ch;
            items_[i].comp->place ({ x0, y0, x1 - x0, y1 - y0 });
        }
        if (p.separatorBefore >= 0)
            sepX_ = colX (p.separatorBefore) - (kGap + extra) / 2;
    }
    void drawExtra (Pen& p) const override
    {
        p.frame (rect_, 1, p.c (colour::kRest, 255));
        if (sepX_ >= 0)
            p.rect (sepX_, rect_.y + kMargin, sepX_ + 1, rect_.bottom() - kMargin, p.c (colour::kRest, 255));
    }

    const PanelPreset* preset_ = nullptr;
    Item items_[kMaxItems];
    int nItems_ = 0, sepX_ = -1;
    Knob knobs_[kMaxKnobs];
    Stepper steppers_[kMaxSteppers];
    KnobRandPair pairs_[kMaxPairs];
    UnisonBox unison_;
    WarpPair warp_;
    MenuButton menu_;
};

// ===============================================================================================================================================================
/** The four values of one Q-Link set (the four hardware knobs Q-Link 1 .. 4 control them at a time). Four slots, each a component (a leaf, or a composite whose turn goes to its primary
    child); the rectangle of the group is the union of the members' rectangles. Active: the members get the accent frame colour and their index 1 .. 4 (setQ), the group draws ONE
    frame around the union, and turn (knobIndex, delta) is routed to the member. Members must be adjacent in the layout: `check` reports every component of the layout that is not a
    member (or a part of one) but lies in the union. */
class QGroup
{
public:
    static constexpr int kSlots = 4;
    static constexpr int kMaxCheck = 64;

    /** Puts a member into slot 0 .. 3 (null empties it). The frame of the group follows the members' rectangles when it is drawn. */
    bool set (int slot, Component* c)
    {
        if (slot < 0 || slot >= kSlots)
            return false;
        if (active_ && slot_[slot])
            slot_[slot]->setQ (false, 0);
        slot_[slot] = c;
        if (active_ && c)
            c->setQ (true, slot + 1);
        return true;
    }
    Component* member (int slot) const noexcept { return slot >= 0 && slot < kSlots ? slot_[slot] : nullptr; }
    int numMembers() const noexcept { int n = 0; for (Component* c : slot_) n += c != nullptr; return n; }
    bool active() const noexcept { return active_; }
    void setActive (bool a)
    {
        active_ = a;
        for (int i = 0; i < kSlots; ++i)
            if (slot_[i])
                slot_[i]->setQ (a, i + 1);
    }
    /** The union of the members' rectangles. */
    Rect rect() const noexcept
    {
        Rect u;
        for (Component* c : slot_)
            if (c)
                u = Rect::unite (u, c->rect());
        return u;
    }
    /** The one frame around the whole group (nothing when not active), inside the union. */
    void draw (gpu::SceneBuilder& sb) const
    {
        if (! active_)
            return;
        const Rect u = rect();
        if (u.empty())
            return;
        Pen pen (sb, true);
        pen.frame (u, 2, pen.c (colour::kAccent, 255));
    }
    /** The hardware knob `knobIndex` (0 .. 3) turned by delta: routed to the member of that slot; nothing when not active or the slot is empty. */
    Change turn (int knobIndex, double delta)
    {
        Component* c = member (knobIndex);
        return active_ && c ? c->turn (delta) : Change::none();
    }
    /** The adjacency check: how many components of `all` (the leaves of the layout) that are not members, and not parts of a member, overlap the union; `first` gets the first such one. */
    int check (Component* const* all, int n, const Component** first = nullptr) const
    {
        const Rect u = rect();
        Component* own[kSlots * kMaxCheck];
        int nOwn = 0;
        for (Component* c : slot_)
            if (c)
                nOwn = c->collectLeaves (own, nOwn, kSlots * kMaxCheck);
        int bad = 0;
        if (first)
            *first = nullptr;
        for (int i = 0; i < n; ++i)
        {
            const Component* c = all[i];
            bool mine = false;
            for (int j = 0; j < nOwn && ! mine; ++j)
                mine = own[j] == c;
            for (Component* m : slot_)
                mine = mine || m == c;
            if (! mine && c->rect().overlaps (u))
            {
                if (bad == 0 && first)
                    *first = c;
                ++bad;
            }
        }
        return bad;
    }

private:
    Component* slot_[kSlots] = {};
    bool active_ = false;
};


/** The filled slots of a TypeBar as one Q group (empty slots take no Q-Link). */
inline void fillGroup (QGroup& g, const TypeBar& bar)
{
    for (int i = 0; i < QGroup::kSlots; ++i)
        g.set (i, bar.slot (i));
}

// ===============================================================================================================================================================
/** DetailPage: a full-rectangle page for the details of one control (the unison settings, the grain window): a top bar with a back button and a title, an optional display area (its
    rectangle is `displayRect ()`; the owner draws in it, the page draws its frame) and the values laid out by a KnobPanel preset (the same grid table) in Q groups of four (the preset's
    QSpec list; one is active at a time: setActiveGroup). Out: the changes of the children with their ids, and the back button's Request change. */
class DetailPage : public Composite
{
public:
    static constexpr int kBarH = 64, kMaxGroups = 4;
    MenuButton back { "BACK", MenuButton::Icon::None };
    KnobPanel panel;
    QGroup groups[kMaxGroups];

    DetailPage() { addKid (back); addKid (panel); }
    void load (const PanelPreset& p, const char* title)
    {
        setText (title_, sizeof title_, title);
        panel.load (p);
        for (QGroup& g : groups)
            for (int i = 0; i < 4; ++i)
                g.set (i, nullptr);
        nGroups_ = std::min (p.numQ, kMaxGroups);
        for (int gi = 0; gi < nGroups_; ++gi)
            for (int i = 0; i < 4; ++i)
                groups[gi].set (i, p.q[gi].slot[i] == PanelSlot::Count ? nullptr : panel.part (p.q[gi].slot[i]));
        active_ = -1;
        setActiveGroup (0);
        layout();
    }
    int numGroups() const noexcept { return nGroups_; }
    int activeGroup() const noexcept { return active_; }
    void setActiveGroup (int i)
    {
        for (int k = 0; k < nGroups_; ++k)
            groups[k].setActive (k == i);
        active_ = i >= 0 && i < nGroups_ ? i : -1;
    }
    /** The hardware knob of the active group. */
    Change turnQ (int knobIndex, double delta) { return active_ >= 0 ? groups[active_].turn (knobIndex, delta) : Change::none(); }
    Rect displayRect() const noexcept { return display_; }
    Size minSize() const override { const Size m = panel.minSize(); return { std::max (m.w, 200), kBarH + display_h() + m.h }; }

    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        for (int i = 0; i < nGroups_; ++i)
            groups[i].draw (sb);
    }

protected:
    int display_h() const noexcept { return panel.preset() ? panel.preset()->displayH : 0; }
    void layout() override
    {
        back.place ({ rect_.x + 4, rect_.y + 4, 120, kBarH - 8 });
        const int dh = display_h();
        display_ = dh > 0 ? Rect { rect_.x + 4, rect_.y + kBarH + 4, rect_.w - 8, dh } : Rect {};
        const int top = rect_.y + kBarH + (dh > 0 ? dh + 8 : 0);
        panel.place ({ rect_.x, top, rect_.w, std::max (0, rect_.bottom() - top) });
    }
    void drawExtra (Pen& p) const override
    {
        p.rect (rect_.x + 4, rect_.y + kBarH - 1, rect_.right() - 4, rect_.y + kBarH, p.c (colour::kRest, 255));
        p.textLeft ({ rect_.x + 140, rect_.y + 4, std::max (0, rect_.w - 150), kBarH - 8 }, title_, 4, p.c (colour::kWhite, 255));
        if (! display_.empty())
        {
            p.rect (display_, p.c (colour::kPanel, 255));
            p.frame (display_, 1, p.c (colour::kRest, 255));
        }
    }
    char title_[13] = {};
    Rect display_;
    int nGroups_ = 0, active_ = -1;
};


// ===============================================================================================================================================================
/** ModulationsPage: the DetailPage "modulations of a control" (opened by the long press on a knob or a number field): a top bar with the back button and a title, then one row per entry of
    the ModInput: the source name, the depth (a small bipolar Knob: value = (depth + 1) / 2, so its Value change is the depth in 0 .. 1; the page does not convert), the polarity toggle
    (on = bidirectional), the bypass toggle (on = bypassed) and the remove button (a Request change). Out: the changes of the children with their ids (`setRowIds`) and the back button's
    Request change. The rows are laid out from the top, kRowH pixels each (at most kMaxRows). */
class ModulationsPage : public Composite
{
public:
    static constexpr int kMaxRows = 8, kRowH = 64, kBarH = 64;
    struct Row
    {
        Knob depth { "", true };
        ToggleIcon polarity, bypass;
        MenuButton remove { "X", MenuButton::Icon::None };
        int source = 0;
        char name[7] = {};
    };
    MenuButton back { "BACK", MenuButton::Icon::None };
    Row rows[kMaxRows];

    ModulationsPage()
    {
        addKid (back);
        for (Row& r : rows)
        {
            r.depth.state.bipolar = true;
            r.depth.state.defaultValue = 0.5;
            r.polarity.state.icon = ToggleIcon::Icon::Generic;
            r.bypass.state.icon = ToggleIcon::Icon::Power;
            addKid (r.depth, false); addKid (r.polarity, false); addKid (r.bypass, false); addKid (r.remove, false);
        }
    }
    /** Takes the entries (the states of the rows come from them; the owner writes them back after every change). */
    void load (const ModInput& m, const char* title)
    {
        setText (title_, sizeof title_, title);
        nRows_ = std::min (m.numMods, kMaxRows);
        for (int i = 0; i < kMaxRows; ++i)
        {
            Row& r = rows[i];
            const bool on = i < nRows_;
            show (r.depth, on); show (r.polarity, on); show (r.bypass, on); show (r.remove, on);
            if (on)
            {
                const ModEntry& e = m.mods[i];
                r.source = e.source;
                setText (r.name, sizeof r.name, e.name);
                r.depth.state.value = r.depth.state.modValue = (std::clamp (e.depth, -1.0, 1.0) + 1.0) * 0.5;
                r.polarity.state.on = e.bidirectional;
                r.bypass.state.on = e.bypassed;
            }
        }
        layout();
    }
    void setRowIds (int row, int depth, int polarity, int bypass, int remove)
    {
        if (row < 0 || row >= kMaxRows)
            return;
        rows[row].depth.setId (depth); rows[row].polarity.setId (polarity); rows[row].bypass.setId (bypass); rows[row].remove.setId (remove);
    }
    int numRows() const noexcept { return nRows_; }
    Size minSize() const override { return { 128 + 72 + 56 * 3 + 4 * kGap + 8, kBarH + 8 + std::max (1, nRows_) * kRowH }; }

protected:
    void layout() override
    {
        back.place ({ rect_.x + 4, rect_.y + 4, 120, kBarH - 8 });
        for (int i = 0; i < nRows_; ++i)
        {
            Row& r = rows[i];
            const int y = rect_.y + kBarH + 8 + i * kRowH;
            Rect k[4];
            const int fixed[4] = { 0, 56, 56, 56 }, weight[4] = { 1, 0, 0, 0 };
            layoutRow ({ rect_.x + 4 + 128, y, rect_.w - 8 - 128, kRowH - 4 }, fixed, weight, 4, kGap, k);
            r.depth.place (k[0]); r.polarity.place (k[1]); r.bypass.place (k[2]); r.remove.place (k[3]);
        }
    }
    void drawExtra (Pen& p) const override
    {
        p.rect (rect_.x + 4, rect_.y + kBarH - 1, rect_.right() - 4, rect_.y + kBarH, p.c (colour::kRest, 255));
        p.textLeft ({ rect_.x + 140, rect_.y + 4, std::max (0, rect_.w - 150), kBarH - 8 }, title_, 3, p.c (colour::kWhite, 255));
        for (int i = 0; i < nRows_; ++i)
            p.textLeft ({ rect_.x + 8, rect_.y + kBarH + 8 + i * kRowH, 120, kRowH - 4 }, rows[i].name, 3, p.c (colour::kText, 255));
    }
    char title_[13] = {};
    int nRows_ = 0;
};

}} // namespace synthyboy_mpc::ui
