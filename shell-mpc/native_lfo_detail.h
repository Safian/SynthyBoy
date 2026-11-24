// SynthyBoy - native Point Mod controls and the visible Shape Editor menu rows.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_lfo_editor.h"

namespace synthyboy_mpc::nativeui::lfodetail
{
enum View { Hidden, PointMod, Menu, ScrollingMenu, LastView = ScrollingMenu };
enum PointState { NoPoints, NoMod, Assigned, MixedAmounts, MixedSources, LastPointState = MixedSources };
enum Field { Back, Target, Amount, Remove, Header, FirstRow };
constexpr int kRows = 8, kCount = FirstRow + kRows;
constexpr int kViewHandle = 0, kPointBase = 1, kPointState = 5;
constexpr int kHeaderBack = 6, kVisibleRows = 7, kHeaderHandle = 8, kHandles = 17;
static_assert (kHeaderHandle + kRows + 1 == kHandles);
constexpr int fieldHandle (int field) noexcept { return field < Header ? kPointBase + field : kHeaderHandle + field - Header; }
constexpr int handleField (int handle) noexcept
{
    if (handle >= kPointBase && handle < kPointState)
        return handle - kPointBase;
    return handle >= kHeaderHandle && handle < kHandles ? Header + handle - kHeaderHandle : -1;
}
constexpr const char* kPointNames[] { "BACK", "MODULATE", "AMOUNT", "REMOVE" };
constexpr const char* kPointTargets[] { "TIME", "LEVEL", "CURVE" };
constexpr int kLabelSize = 64;

struct Context
{
    View view = Hidden;
    lfoeditor::Context editor {};
    uint64_t menu = 0;
    int firstRow = 0, pointTarget = 0, pointSource = 0;

    bool operator== (const Context& other) const noexcept
    {
        return view == other.view && editor == other.editor && menu == other.menu
            && firstRow == other.firstRow && pointTarget == other.pointTarget && pointSource == other.pointSource;
    }
};

struct State
{
    Context context;
    PointState point = NoPoints;
    int rows = 0;
    bool headerBack = false;
    uint16_t fields = 0;
    std::array<float, kCount> values {};
    std::array<const char*, kRows + 1> names {};

    bool allows (int field) const noexcept
    {
        return field >= 0 && field < kCount && (fields & (1 << field)) != 0;
    }
    FxCell cell (int field) const noexcept
    {
        return allows (field) ? FxCell { field, context.editor.owner, kFxDial } : FxCell {};
    }
};

inline lfoeditor::Range rangeOf (int field) noexcept
{
    if (field == Target)
        return { 0, 2 };
    if (field == Amount)
        return { -101, 100 };  // -101 is the displayed mixed/empty sentinel
    return {};
}

class Bank : public NativeCellBank<kCount>
{
public:
    struct Display
    {
        View view = Hidden;
        PointState point = NoPoints;
        int rows = 0;
        bool headerBack = false;
    };

    void publish (const State& next) noexcept
    {
        if (! (context_ == next.context))
        {
            invalidate();
            updateNames (next.names);
        }
        context_ = next.context;
        std::array<FxCell, kCount> cells {};
        for (int field = 0; field < kCount; ++field)
        {
            values_[field].store (next.values[field], std::memory_order_relaxed);
            cells[field] = next.cell (field);
        }
        NativeCellBank<kCount>::publish (context_.view, cells.data());
        display_.store (static_cast<unsigned> (context_.view | (next.point << 2) | (next.rows << 5)
                        | ((next.headerBack ? 1 : 0) << 9)), std::memory_order_release);
    }

    Display display() const noexcept
    {
        const auto bits = display_.load (std::memory_order_acquire);
        return { static_cast<View> (bits & 3u), static_cast<PointState> ((bits >> 2) & 7u),
                 static_cast<int> ((bits >> 5) & 15u), ((bits >> 9) & 1u) != 0 };
    }

    float value (int field) const noexcept { return values_[field].load (std::memory_order_relaxed); }

    bool take (const State& expected, int field, float& value) noexcept
    {
        return NativeCellBank<kCount>::take (field, context_ == expected.context ? expected.cell (field) : FxCell {}, value);
    }

    void label (int field, char* out, size_t cap) const noexcept
    {
        if (cap == 0)
            return;
        out[0] = '\0';
        if (field < Header || field >= kCount)
            return;
        const auto revision = namesRevision_.load (std::memory_order_acquire);
        if ((revision & 1u) != 0)
            return;
        const auto& source = names_[field - Header];
        const auto count = std::min (cap - 1, source.size());
        for (size_t i = 0; i < count; ++i)
            out[i] = source[i].load (std::memory_order_relaxed);
        out[count] = '\0';
        if (namesRevision_.load (std::memory_order_acquire) != revision)
            out[0] = '\0';
    }

private:
    void updateNames (const std::array<const char*, kRows + 1>& names) noexcept
    {
        namesRevision_.fetch_add (1u, std::memory_order_acq_rel);
        for (size_t row = 0; row < names.size(); ++row)
        {
            const char* name = names[row] != nullptr ? names[row] : "";
            size_t pos = 0;
            while (pos < kLabelSize - 1 && name[pos] != '\0')
            {
                names_[row][pos].store (name[pos], std::memory_order_relaxed);
                ++pos;
            }
            names_[row][pos].store ('\0', std::memory_order_relaxed);
        }
        namesRevision_.fetch_add (1u, std::memory_order_release);
    }

    Context context_; // the overlay thread owns the binding context
    std::atomic<unsigned> display_ { 0 }, namesRevision_ { 0 };
    std::atomic<float> values_[kCount] {};
    std::array<std::array<std::atomic<char>, kLabelSize>, kRows + 1> names_ {};
};
}
