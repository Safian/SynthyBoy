// SynthyBoy - native host choices of the LFO: the SOURCE of a point modulation, ADD SOURCE of the MODULATIONS page and ADD TARGET of the TARGETS page.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every choice is a plain Selector of the skin that opens the host's own MenuOverlay: the host holds the rows, the scrolling and the check mark; a row tap marks, the tap on the header applies and closes
// (docs/dsp-knowledge/skin-host-behaviour.md). The plugin publishes only the current index of each choice and takes the index the host applied. A choice that adds something (ADD SOURCE, the targets)
// is reset to 0 by the plugin right after it has added, so the next open starts on the placeholder row.
//
// ADD TARGET is two steps because the 1262 destinations are far too many for one list: GROUP (one choice of the groups of the matrix, the same grouping as `uibind::DestIndex`) and one TARGET choice per group
// that the skin shows while its group is the chosen one.
#pragma once

#include "native_lfo_editor.h"

namespace synthyboy_mpc::nativeui::lfopick
{
enum View { Hidden, PointSource, AddSource, AddTarget, LastView = AddTarget };
enum Field { Source, AddSourceChoice, Group, FirstTarget };
constexpr int kMaxGroups = 40;   // the matrix has 35 groups (docs/params.json); the skin asserts it fits
constexpr int kCount = FirstTarget + kMaxGroups;
constexpr int kViewHandle = 0, kHandles = 1 + kCount;
static_assert (kHandles == kLfoPickHandles);
constexpr int fieldHandle (int field) noexcept { return 1 + field; }
constexpr int handleField (int handle) noexcept { return handle >= 1 && handle < kHandles ? handle - 1 : -1; }
constexpr const char* kNames[] { "LFO Point Source", "LFO Add Source", "LFO Add Target Group" };
/** The index of a choice runs 0 .. last (0 is the placeholder row: nothing chosen); `uibind::lfoPickLast` gives the last one of every choice. */
inline lfoeditor::Range rangeOf (int last) noexcept { return { 0, std::max (1, last) }; }

struct Context
{
    View view = Hidden;
    int owner = 0, slot = 0, subject = 0, target = 0;   // the page (tab or Play), the LFO, the control of MODULATIONS / the LFO of TARGETS, the value of a point modulation
    uint64_t preset = 0;
    lfoeditor::Context editor {};                      // the point selection of a point modulation: a write for another selection is dropped

    bool operator== (const Context& other) const noexcept
    {
        return view == other.view && owner == other.owner && slot == other.slot && subject == other.subject
            && target == other.target && preset == other.preset && editor == other.editor;
    }
};

struct State
{
    Context context;
    uint64_t fields = 0;                     // the choices that take input now
    std::array<float, kCount> values {};     // their published index, normalised

    bool allows (int field) const noexcept
    {
        return field >= 0 && field < kCount && (fields & (uint64_t (1) << field)) != 0;
    }
    FxCell cell (int field) const noexcept
    {
        return allows (field) ? FxCell { field, 1, kFxDial } : FxCell {};
    }
};

class Bank : public NativeCellBank<kCount>
{
public:
    void publish (const State& next) noexcept
    {
        if (! (context_ == next.context))
            invalidate();
        context_ = next.context;
        std::array<FxCell, kCount> cells {};
        for (int field = 0; field < kCount; ++field)
        {
            values_[field].store (next.values[field], std::memory_order_relaxed);
            cells[field] = next.cell (field);
        }
        NativeCellBank<kCount>::publish (static_cast<int> (context_.view), cells.data());
        view_.store (static_cast<int> (context_.view), std::memory_order_release);
    }

    View view() const noexcept { return static_cast<View> (view_.load (std::memory_order_acquire)); }
    float value (int field) const noexcept { return field >= 0 && field < kCount ? values_[field].load (std::memory_order_relaxed) : 0.0f; }

    bool take (const State& expected, int field, float& value) noexcept
    {
        return NativeCellBank<kCount>::take (field, context_ == expected.context ? expected.cell (field) : FxCell {}, value);
    }

private:
    Context context_;   // the overlay thread owns the binding context
    std::atomic<int> view_ { 0 };
    std::atomic<float> values_[kCount] {};
};
}
