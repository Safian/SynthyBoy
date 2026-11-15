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
                                 Mode, Stack, Width, Range, WtSpread, Warp1Spread, Warp2Spread, Amount, AmountRand, Skew, SkewRand, Shape, LengthRand, PanRand, LevelRand, Timbre, Start, Delay, Attack, Hold, Decay, Sustain, Release, Override, Count };
enum class PartKind : uint8_t { Knob, RandPair, Unison, WarpPair, Menu, Stepper, Toggle };

/** One part of a preset: what, which slot, its label, and the cells it covers in the grid. */
struct PanelItem
{
    PartKind kind;
    PanelSlot slot;
    const char* label;
    uint8_t col, row, colSpan, rowSpan;
    bool bipolar;
    uint8_t block = 0;   // in a preset with blocks: the block the cells belong to (the cells are those of the block's inner grid)
};

/** A titled, bordered block of a preset (one Q group): its cells in the outer grid of the preset and its own inner grid for the items. */
struct PanelBlock
{
    const char* title;
    uint8_t col, row, colSpan, rowSpan;
    uint8_t innerCols, innerRows;
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
    const PanelBlock* blocks = nullptr;   // the full screen's bottom band: blocks side by side, each with a title and a thin outline (the grid of the preset is the grid of the blocks)
    int numBlocks = 0;
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
// The bottom band of the full screen of an oscillator (screen.h): PAN and LEVEL are in the top band there, the granular RAND knobs are a group of their own. Every Q group is a titled block.
constexpr PanelBlock kWavetableBlocks[] = { { "UNISON", 0, 0, 1, 1, 4, 1 }, { "WARP", 1, 0, 1, 1, 1, 1 } };
constexpr PanelItem kWavetableFullItems[] = {
    { PartKind::Knob, PanelSlot::WtPos, "WT POS", 0, 0, 1, 1, false, 0 },
    { PartKind::Unison, PanelSlot::Unison, "", 1, 0, 1, 1, false, 0 },
    { PartKind::Knob, PanelSlot::Detune, "DETUNE", 2, 0, 1, 1, false, 0 },
    { PartKind::Knob, PanelSlot::Blend, "BLEND", 3, 0, 1, 1, false, 0 },
    { PartKind::WarpPair, PanelSlot::Warp, "", 0, 0, 1, 1, false, 1 },
};
constexpr QSpec kWavetableFullQ[] = { { { PanelSlot::WtPos, PanelSlot::Unison, PanelSlot::Detune, PanelSlot::Blend } } };
constexpr PanelBlock kGranularBlocks[] = { { "GRAIN", 0, 0, 4, 1, 4, 1 }, { "PLAYBACK", 4, 0, 3, 1, 3, 1 }, { "RANDOM", 7, 0, 3, 1, 3, 1 } };
constexpr PanelItem kGranularFullItems[] = {
    { PartKind::Knob, PanelSlot::Scan, "SCAN", 0, 0, 1, 1, false, 0 },
    { PartKind::Knob, PanelSlot::Dens, "DENS", 1, 0, 1, 1, false, 0 },
    { PartKind::Knob, PanelSlot::Length, "LENGTH", 2, 0, 1, 1, false, 0 },
    { PartKind::Menu, PanelSlot::WarpMenu, "WARP", 3, 0, 1, 1, false, 0 },
    { PartKind::Knob, PanelSlot::Offset, "OFFSET", 0, 0, 1, 1, false, 1 },
    { PartKind::Knob, PanelSlot::Dir, "DIR", 1, 0, 1, 1, false, 1 },
    { PartKind::Knob, PanelSlot::Pitch, "PITCH", 2, 0, 1, 1, true, 1 },
    { PartKind::Knob, PanelSlot::LengthRand, "LEN RAND", 0, 0, 1, 1, false, 2 },
    { PartKind::Knob, PanelSlot::PanRand, "PAN RAND", 1, 0, 1, 1, false, 2 },
    { PartKind::Knob, PanelSlot::LevelRand, "LVL RAND", 2, 0, 1, 1, false, 2 },
};
constexpr QSpec kGranularFullQ[] = { { { PanelSlot::Scan, PanelSlot::Dens, PanelSlot::Length, PanelSlot::WarpMenu } },
                                     { { PanelSlot::Offset, PanelSlot::Dir, PanelSlot::Pitch, PanelSlot::Count } },
                                     { { PanelSlot::LengthRand, PanelSlot::PanRand, PanelSlot::LevelRand, PanelSlot::Count } } };
constexpr PanelItem kNoiseFullItems[] = { { PartKind::Knob, PanelSlot::Filter, "FILTER", 0, 0, 1, 1, false } };
constexpr PanelItem kNoItems[] = { { PartKind::Knob, PanelSlot::Count, "", 0, 0, 1, 1, false } };
constexpr PanelPreset kWavetableFull { "wavetable (full screen)", 2, 1, -1, kWavetableFullItems, 5, kWavetableFullQ, 1, 0, kWavetableBlocks, 2 };
constexpr PanelPreset kGranularFull { "granular (full screen)", 10, 1, -1, kGranularFullItems, 10, kGranularFullQ, 3, 0, kGranularBlocks, 3 };
constexpr PanelPreset kNoiseFull { "noise (full screen)", 4, 1, -1, kNoiseFullItems, 1, nullptr, 0, 0 };
constexpr PanelPreset kEmptyFull { "none (full screen)", 4, 1, -1, kNoItems, 0, nullptr, 0, 0 };
constexpr PanelItem kMultisampleFullItems[] = {
    { PartKind::Knob, PanelSlot::Timbre, "TIMBRE", 0, 0, 1, 1, false, 0 },
    { PartKind::Unison, PanelSlot::Unison, "", 1, 0, 1, 1, false, 0 },
    { PartKind::Knob, PanelSlot::Detune, "DETUNE", 2, 0, 1, 1, false, 0 },
    { PartKind::Knob, PanelSlot::Blend, "BLEND", 3, 0, 1, 1, false, 0 },
    { PartKind::WarpPair, PanelSlot::Warp, "", 0, 0, 1, 1, false, 1 },
};
constexpr QSpec kMultisampleFullQ[] = { { { PanelSlot::Timbre, PanelSlot::Unison, PanelSlot::Detune, PanelSlot::Blend } } };
constexpr PanelPreset kMultisampleFull { "multisample (full screen)", 2, 1, -1, kMultisampleFullItems, 5, kMultisampleFullQ, 1, 0, kWavetableBlocks, 2 };
// the DetailPages of the unison settings of the three types, and the envelope of the multisample oscillator
constexpr PanelItem kUnisonMsItems[] = {
    { PartKind::Stepper, PanelSlot::Mode, "MODE", 0, 0, 1, 1, false }, { PartKind::Stepper, PanelSlot::Stack, "STACK", 1, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Width, "WIDTH", 0, 1, 1, 1, false }, { PartKind::Knob, PanelSlot::Range, "RANGE", 1, 1, 1, 1, false },
    { PartKind::Knob, PanelSlot::Warp1Spread, "WARP 1", 2, 0, 1, 1, false }, { PartKind::Knob, PanelSlot::Warp2Spread, "WARP 2", 3, 0, 1, 1, false },
};
constexpr QSpec kUnisonMsQ[] = { { { PanelSlot::Mode, PanelSlot::Stack, PanelSlot::Width, PanelSlot::Range } }, { { PanelSlot::Warp1Spread, PanelSlot::Warp2Spread, PanelSlot::Count, PanelSlot::Count } } };
constexpr PanelItem kUnisonGrItems[] = {
    { PartKind::Stepper, PanelSlot::Mode, "MODE", 0, 0, 1, 1, false }, { PartKind::Stepper, PanelSlot::Stack, "STACK", 1, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Width, "WIDTH", 0, 1, 1, 1, false }, { PartKind::Knob, PanelSlot::Range, "RANGE", 1, 1, 1, 1, false },
    { PartKind::Knob, PanelSlot::Start, "START", 2, 0, 1, 1, false },
};
constexpr QSpec kUnisonGrQ[] = { { { PanelSlot::Mode, PanelSlot::Stack, PanelSlot::Width, PanelSlot::Range } }, { { PanelSlot::Start, PanelSlot::Count, PanelSlot::Count, PanelSlot::Count } } };
constexpr PanelItem kEnvelopeItems[] = {
    { PartKind::Knob, PanelSlot::Delay, "DELAY", 0, 0, 1, 1, false }, { PartKind::Knob, PanelSlot::Attack, "A", 1, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Hold, "H", 2, 0, 1, 1, false }, { PartKind::Knob, PanelSlot::Decay, "D", 3, 0, 1, 1, false },
    { PartKind::Knob, PanelSlot::Sustain, "S", 0, 1, 1, 1, false }, { PartKind::Knob, PanelSlot::Release, "R", 1, 1, 1, 1, false },
    { PartKind::Toggle, PanelSlot::Override, "OVERRIDE", 2, 1, 1, 1, false },
};
constexpr QSpec kEnvelopeQ[] = { { { PanelSlot::Delay, PanelSlot::Attack, PanelSlot::Hold, PanelSlot::Decay } }, { { PanelSlot::Sustain, PanelSlot::Release, PanelSlot::Override, PanelSlot::Count } } };
constexpr PanelPreset kUnisonDetailMultisample { "unison details (multisample)", 4, 2, -1, kUnisonMsItems, 6, kUnisonMsQ, 2, 0 };
constexpr PanelPreset kUnisonDetailGranular { "unison details (granular)", 4, 2, -1, kUnisonGrItems, 5, kUnisonGrQ, 2, 0 };
constexpr PanelPreset kEnvelopeDetail { "multisample envelope", 4, 2, -1, kEnvelopeItems, 7, kEnvelopeQ, 2, 140 };
constexpr PanelPreset kUnisonDetail { "unison details", 4, 2, -1, kUnisonDetailItems, 7, kUnisonDetailQ, 2, 0 };
constexpr PanelPreset kGrainDetail { "grain window", 4, 2, -1, kGrainDetailItems, 5, kGrainDetailQ, 2, 140 };
}

/** 13. KnobPanel: lays out the parts of a preset on a grid inside its rectangle (equal columns and rows, kGap between the cells, a wider gap and a thin line at the separator). The
    owner finds the parts by slot, gives their ids and writes their states; the panel's own state is the preset. Same contract as every component. */
class KnobPanel : public Composite
{
public:
    static constexpr int kMaxKnobs = 12, kMaxPairs = 3, kMaxItems = 12, kMaxSteppers = 4, kMaxToggles = 2;
    static constexpr int kSeparatorGap = 16;

    KnobPanel() = default;
    explicit KnobPanel (const PanelPreset& p) { load (p); }

    /** Switches to a preset: every part comes back to its defaults (labels from the table, no ids). */
    void load (const PanelPreset& p)
    {
        clearKids();
        preset_ = &p;
        int nk = 0, np = 0, nm = 0, ns = 0, nt = 0;
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
                case PartKind::Toggle:
                    if (nt < kMaxToggles)
                    {
                        ToggleIcon& t = toggles_[nt++];
                        t.state = ToggleIcon::State {};
                        m.comp = &t;
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
        warp_.setStacked (p.numBlocks > 0);
        layout();
    }
    const PanelPreset* preset() const noexcept { return preset_; }
    int numBlocks() const noexcept { return preset_ ? preset_->numBlocks : 0; }
    /** The rectangle of block i (the titled outline of a Q group; empty without blocks). */
    Rect blockRect (int i) const noexcept { return i >= 0 && i < numBlocks() && i < kMaxBlocks ? blocks_[i] : Rect {}; }
    const char* blockTitle (int i) const noexcept { return i >= 0 && i < numBlocks() ? preset_->blocks[i].title : ""; }
    static constexpr int kMaxBlocks = 4, kBlockTitleH = 22, kBlockPad = 6, kBlockGap = 8;
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
    ToggleIcon* toggle (PanelSlot s) const noexcept
    {
        for (int i = 0; i < nItems_; ++i)
            if (items_[i].def->slot == s && items_[i].def->kind == PartKind::Toggle)
                return static_cast<ToggleIcon*> (items_[i].comp);
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
        if (preset_->numBlocks > 0)
            return blockMinSize();
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
        if (preset_->numBlocks > 0)
        {
            layoutBlocks();
            return;
        }
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
    /** The blocks: the outer grid (cols x rows) of the preset holds the titled blocks with kBlockGap between them; inside a block the items use the block's inner grid. */
    void layoutBlocks()
    {
        const PanelPreset& p = *preset_;
        const int nb = std::min (p.numBlocks, kMaxBlocks);
        const int cw = std::max (0, (rect_.w - kBlockGap * (p.cols - 1)) / p.cols), ch = std::max (0, (rect_.h - kBlockGap * (p.rows - 1)) / p.rows);
        for (int b = 0; b < nb; ++b)
        {
            const PanelBlock& bl = p.blocks[b];
            const int x0 = rect_.x + bl.col * (cw + kBlockGap), y0 = rect_.y + bl.row * (ch + kBlockGap);
            const int x1 = rect_.x + (bl.col + bl.colSpan - 1) * (cw + kBlockGap) + cw, y1 = rect_.y + (bl.row + bl.rowSpan - 1) * (ch + kBlockGap) + ch;
            blocks_[b] = { x0, y0, x1 - x0, y1 - y0 };
            const Rect in { x0 + kBlockPad, y0 + kBlockTitleH, std::max (0, x1 - x0 - 2 * kBlockPad), std::max (0, y1 - y0 - kBlockTitleH - kBlockPad) };
            const int icw = std::max (0, (in.w - kGap * (bl.innerCols - 1)) / bl.innerCols), ich = std::max (0, (in.h - kGap * (bl.innerRows - 1)) / bl.innerRows);
            for (int i = 0; i < nItems_; ++i)
            {
                const PanelItem& it = *items_[i].def;
                if (it.block != b || ! items_[i].comp)
                    continue;
                const int ix0 = in.x + it.col * (icw + kGap), iy0 = in.y + it.row * (ich + kGap);
                const int ix1 = in.x + (it.col + it.colSpan - 1) * (icw + kGap) + icw, iy1 = in.y + (it.row + it.rowSpan - 1) * (ich + kGap) + ich;
                items_[i].comp->place ({ ix0, iy0, ix1 - ix0, iy1 - iy0 });
            }
        }
    }
    Size blockMinSize() const
    {
        const PanelPreset& p = *preset_;
        const int nb = std::min (p.numBlocks, kMaxBlocks);
        int colW = 0, rowH = 0;
        for (int b = 0; b < nb; ++b)
        {
            const PanelBlock& bl = p.blocks[b];
            int icw = 0, ich = 0;
            for (int i = 0; i < nItems_; ++i)
            {
                const PanelItem& it = *items_[i].def;
                if (it.block != b || ! items_[i].comp)
                    continue;
                const Size m = items_[i].comp->minSize();
                icw = std::max (icw, (m.w - (it.colSpan - 1) * kGap + it.colSpan - 1) / it.colSpan);
                ich = std::max (ich, (m.h - (it.rowSpan - 1) * kGap + it.rowSpan - 1) / it.rowSpan);
            }
            const int bw = bl.innerCols * icw + (bl.innerCols - 1) * kGap + 2 * kBlockPad, bh = bl.innerRows * ich + (bl.innerRows - 1) * kGap + kBlockTitleH + kBlockPad;
            colW = std::max (colW, (bw - (bl.colSpan - 1) * kBlockGap + bl.colSpan - 1) / bl.colSpan);
            rowH = std::max (rowH, (bh - (bl.rowSpan - 1) * kBlockGap + bl.rowSpan - 1) / bl.rowSpan);
        }
        return { colW * p.cols + kBlockGap * (p.cols - 1), rowH * p.rows + kBlockGap * (p.rows - 1) };
    }
    void drawUnder (Pen& p) const override
    {
        if (preset_ && preset_->numBlocks > 0)
        {
            for (int b = 0; b < std::min (preset_->numBlocks, kMaxBlocks); ++b)
                p.block (blocks_[b]);
            return;
        }
        p.block (rect_);
    }
    void drawExtra (Pen& p) const override
    {
        if (preset_ && preset_->numBlocks > 0)
        {
            for (int b = 0; b < std::min (preset_->numBlocks, kMaxBlocks); ++b)
            {
                p.textLeft ({ blocks_[b].x + kBlockPad + 4, blocks_[b].y + 3, std::max (0, blocks_[b].w - 2 * kBlockPad - 4), 16 }, preset_->blocks[b].title, theme::L().fLabel - 2.0f, p.c (theme::L().textDim, 255), theme::L().fMin);
            }
            return;
        }
        if (sepX_ >= 0)
            p.rect (sepX_, rect_.y + kMargin, sepX_ + 1, rect_.bottom() - kMargin, p.c (theme::L().keyline, 255));
    }

    const PanelPreset* preset_ = nullptr;
    Item items_[kMaxItems];
    int nItems_ = 0, sepX_ = -1;
    Rect blocks_[kMaxBlocks];
    Knob knobs_[kMaxKnobs];
    Stepper steppers_[kMaxSteppers];
    ToggleIcon toggles_[kMaxToggles];
    KnobRandPair pairs_[kMaxPairs];
    UnisonBox unison_;
    WarpPair warp_;
    MenuButton menu_;
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
        p.rect (rect_.x + 4, rect_.y + kBarH - 1, rect_.right() - 4, rect_.y + kBarH, p.c (theme::L().keyline, 255));
        p.textLeft ({ rect_.x + 140, rect_.y + 4, std::max (0, rect_.w - 150), kBarH - 8 }, title_, theme::L().fTitle, p.c (theme::L().text, 255), theme::L().fMin);
    }
    void drawUnder (Pen& p) const override
    {
        if (! display_.empty())
            p.block (display_);
    }
    char title_[32] = {};
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
    static constexpr int kMaxRows = 8, kRowH = 56, kBarH = 64;
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
    /** The last row, "add source": the stepper over the sources the owner offers (names of 6 characters; the owner sets `addSource.state` and `sourceIds`) and the add button. */
    Stepper addSource;
    MenuButton add { "ADD", MenuButton::Icon::None };
    int sourceIds[8] = {};
    /** The Q group of the row touched last: [depth, polarity, bypass, -]; its frame is drawn around the three. A touch on a row makes it the active one (row 0 after `load`). */
    QGroup group;

    ModulationsPage()
    {
        addKid (back);
        addKid (addSource); addKid (add);
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
        activeRow_ = nRows_ > 0 ? 0 : -1;
        selectRow (activeRow_);
        layout();
    }
    int activeRow() const noexcept { return activeRow_; }
    void selectRow (int i)
    {
        for (int k = 0; k < 4; ++k)
            group.set (k, nullptr);
        activeRow_ = i >= 0 && i < nRows_ ? i : -1;
        if (activeRow_ < 0)
        {
            group.setActive (false);
            return;
        }
        group.set (0, &rows[i].depth); group.set (1, &rows[i].polarity); group.set (2, &rows[i].bypass);
        group.setActive (true);
    }
    /** The hardware knob of the active row's group. */
    Change turnQ (int knobIndex, double delta) { return group.turn (knobIndex, delta); }
    Change touchDown (int x, int y, double nowMs) override
    {
        const Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)
            for (int i = 0; i < nRows_; ++i)
                if (y >= rows[i].depth.rect().y && y < rows[i].depth.rect().bottom())
                {
                    selectRow (i);
                    break;
                }
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        Composite::draw (sb);
        group.draw (sb);
    }
    void setRowIds (int row, int depth, int polarity, int bypass, int remove)
    {
        if (row < 0 || row >= kMaxRows)
            return;
        rows[row].depth.setId (depth); rows[row].polarity.setId (polarity); rows[row].bypass.setId (bypass); rows[row].remove.setId (remove);
    }
    int numRows() const noexcept { return nRows_; }
    Size minSize() const override { return { std::max (128 + 72 + 56 * 3 + 4 * kGap + 8, 128 + Stepper::kMinW + 56 + 2 * kGap + 8), kBarH + 8 + (nRows_ + 1) * kRowH }; }
    Rect addRowRect() const noexcept { return { rect_.x + 4 + 128, rect_.y + kBarH + 8 + nRows_ * kRowH, rect_.w - 8 - 128, kRowH }; }

    /** The add button comes out as an AddRequest { id of the button, source id of the current item of the add stepper }, the rest as it is. */
    Change touchUp (int x, int y) override
    {
        const bool onAdd = cap_ == &add;
        const Change c = Composite::touchUp (x, y);
        if (onAdd && c.kind == ChangeKind::Request)
        {
            const int i = std::clamp (addSource.state.index, 0, 7);
            return Change::addRequest (c.id, sourceIds[i]);
        }
        return c;
    }

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
            layoutRow ({ rect_.x + 4 + 128, y, rect_.w - 8 - 128, kRowH }, fixed, weight, 4, kGap, k);
            r.depth.place (k[0]); r.polarity.place (k[1]); r.bypass.place (k[2]); r.remove.place (k[3]);
        }
        Rect a[2];
        const int fx[2] = { 0, 96 }, wt[2] = { 1, 0 };
        layoutRow (addRowRect(), fx, wt, 2, kGap, a);
        addSource.place (a[0]); add.place (a[1]);
    }
    void drawExtra (Pen& p) const override
    {
        const theme::Look& T = theme::L();
        p.rect (rect_.x + 4, rect_.y + kBarH - 1, rect_.right() - 4, rect_.y + kBarH, p.c (T.keyline, 255));
        p.textLeft ({ rect_.x + 140, rect_.y + 4, std::max (0, rect_.w - 150), kBarH - 8 }, title_, T.fValue + 2.0f, p.c (T.text, 255), T.fMin);
        p.textLeft ({ rect_.x + 8, addRowRect().y, 120, kRowH }, "ADD", T.fValue, p.c (T.textDim, 255), T.fMin);
        for (int i = 0; i < nRows_; ++i)
            p.textLeft ({ rect_.x + 8, rect_.y + kBarH + 8 + i * kRowH, 120, kRowH }, rows[i].name, T.fValue, p.c (theme::sourceColour (rows[i].name), 255), T.fMin);
    }
    char title_[32] = {};
    int nRows_ = 0, activeRow_ = -1;
};

}} // namespace synthyboy_mpc::ui
