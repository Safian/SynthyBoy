// SynthyBoy - transient native FX chain and gallery handles.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "native_fx_cells.h"
#include "synthyboy/fx_rack.h"

namespace synthyboy_mpc::nativeui::fxchain
{
constexpr int kSlots = 16, kTypes = 16;
enum Offset
{
    View, Count, Select, Power = Select + kSlots, Gallery = Power + kSlots,
    Menu = Gallery + kTypes, Choose, Back, Page, kHandles
};
enum Mode { Hidden, MainLocked, MainAdd, MainGallery, BandLocked, FilteredAdd, FilteredGallery, kModes };
static_assert (kSlots == synthyboy::kNumFxSlots);
static_assert (kHandles == kFxChainHandles);

struct Card
{
    int item = -1, kind = 0, rack = 0;
    bool on = false;
};

struct State
{
    Mode mode = Hidden;
    int scope = 0, count = 0, selected = 0;
    int page = 0;
    bool canPage = false;
    uint32_t structure = 0, offered = 0;
    std::array<Card, kSlots> cards {};

    bool gallery() const noexcept { return mode == MainGallery || mode == FilteredGallery; }
    bool available (int type) const noexcept { return (offered & (1u << type)) != 0; }

    FxCell cell (int offset) const noexcept
    {
        if (mode == Hidden)
            return {};
        if (offset >= Select && offset < Select + count)
        {
            const auto& c = cards[static_cast<size_t> (offset - Select)];
            return { c.item, c.kind, kFxDial };
        }
        if (offset >= Power && offset < Power + count)
        {
            const auto& c = cards[static_cast<size_t> (offset - Power)];
            return { c.item, c.kind, kFxDial };
        }
        if (offset >= Gallery && offset < Menu && gallery() && available (offset - Gallery))
            return { scope, offset - Gallery, kFxDial };
        if ((offset == Menu && (mode == MainAdd || mode == MainGallery))
            || (offset == Choose && mode == FilteredAdd)
            || (offset == Back && (scope != 0 || mode == FilteredGallery))
            || (offset == Page && canPage))
            return { scope, offset, kFxDial };
        return {};
    }
};

class Bank
{
public:
    void publish (const State& s, uint64_t preset) noexcept
    {
        if (s.mode == Hidden)
            setMenuOpen (false);
        if (structure_ != s.structure || preset_.load (std::memory_order_relaxed) != preset)
            actions_.invalidate();
        structure_ = s.structure;
        for (int i = 0; i < s.count; ++i)
        {
            const auto& c = s.cards[static_cast<size_t> (i)];
            cards_[i].store (c.kind | (c.rack << 5) | (c.on ? 1 << 9 : 0), std::memory_order_release);
        }
        FxCell links[kHandles];
        for (int field = Select; field < kHandles; ++field)
            links[field] = s.cell (field);
        actions_.publish (s.mode, links);
        preset_.store (preset, std::memory_order_release);
        const int pageFrame = s.canPage ? s.page + 1 : 0;
        view_.store (s.mode | (s.count << 3) | (s.selected << 8) | (pageFrame << 13), std::memory_order_release);
    }

    Card card (int slot) const noexcept
    {
        if (slot < 0 || slot >= count (view_.load (std::memory_order_acquire)))
            return {};
        const int bits = cards_[slot].load (std::memory_order_acquire);
        return { -1, bits & 31, (bits >> 5) & 15, ((bits >> 9) & 1) != 0 };
    }

    float value (int offset) const noexcept
    {
        const int view = view_.load (std::memory_order_acquire);
        if (offset == View)
            return static_cast<float> (view & 7) / (kModes - 1);
        if (offset == Count)
            return static_cast<float> (count (view)) / kSlots;
        if (offset >= Select && offset < Power)
            return ((view >> 8) & 31) == offset - Select + 1 ? 1.0f : 0.0f;
        if (offset >= Power && offset < Gallery)
            return card (offset - Power).on ? 1.0f : 0.0f;
        if ((offset >= Gallery && offset < Menu) || offset == Back)
            return actions_.at (offset).cell().target >= 0 ? 1.0f : 0.0f;
        if (offset == Page)
            return static_cast<float> (view >> 13) * 0.5f;
        return 0.0f;
    }

    void post (int field, float value, uint64_t preset) noexcept
    {
        if (field < Select || preset_.load (std::memory_order_acquire) != preset)
            return;
        const auto link = actions_.at (field);
        if (field != Menu || menuLink_.load (std::memory_order_acquire) == link.link)
            actions_.post (link, value);
    }

    void setMenuOpen (bool open) noexcept
    {
        if (menuOpen_ == open)
            return;
        menuOpen_ = open;
        const auto link = actions_.at (Menu);
        menuLink_.store (open && link.cell().target >= 0 ? link.link : 0, std::memory_order_release);
    }

    bool take (int field, const State& expected, float& v) noexcept
    {
        const bool same = actions_.layout() == expected.mode && structure_ == expected.structure;
        return actions_.take (field, same ? expected.cell (field) : FxCell {}, v);
    }

    template <class Report>
    void poll (Report&& report)
    {
        const int view = view_.load (std::memory_order_acquire);
        const int mode = view & 7;
        const bool gallery = mode == MainGallery || mode == FilteredGallery;
        const auto fieldReport = [&] (int field, bool always = false)
        {
            const auto link = actions_.at (field);
            const bool live = link.cell().target >= 0;
            if (live || always)
                report (field, value (field), live && actions_.needsReport (link));
            if (live)
                actions_.report (link);
        };
        for (int slot = 0; slot < count (view); ++slot)
        {
            fieldReport (Select + slot);
            fieldReport (Power + slot);
        }
        if (gallery || lastGallery_)
            for (int field = Gallery; field < Menu; ++field)
                fieldReport (field, true);
        lastGallery_ = gallery;
        fieldReport (Menu);
        fieldReport (Choose);
        fieldReport (Back, true);
        fieldReport (Page, true);
        report (Count, value (Count), false);
        report (View, value (View), false);
    }

private:
    static int count (int view) noexcept { return (view >> 3) & 31; }
    NativeCellBank<kHandles> actions_;
    std::atomic<int> view_ {}, cards_[kSlots] {};
    std::atomic<uint64_t> preset_ {}, menuLink_ {};
    // Observed solely by the overlay thread.
    uint32_t structure_ = 0;
    bool menuOpen_ = false;
    bool lastGallery_ = false;
};
}
