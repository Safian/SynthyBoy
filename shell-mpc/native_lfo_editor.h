// SynthyBoy - native host controls around the standalone LFO shape canvas.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"
#include "ui/lfo_model.h"

#include <array>

namespace synthyboy_mpc::nativeui::lfoeditor
{
enum Field
{
    kBack, kTool, kShapes, kMore, kUndo, kRedo, kDelete, kMod,
    kSnap, kLoop, kSmooth, kCycleLink, kLoopLink,
    kPoint, kX, kY, kCurve, kGridX, kGridY, kPhase, kBend, kMode, kCount
};
enum Type { kAction, kToolChoice, kToggle, kNumber, kChoice };
struct FieldInfo { const char* name; Type type; };
constexpr FieldInfo kFields[kCount] {
    { "BACK", kAction }, { "TOOL", kToolChoice }, { "SHAPES", kAction }, { "MORE", kAction },
    { "UNDO", kAction }, { "REDO", kAction }, { "DELETE", kAction }, { "MOD", kAction },
    { "SNAP", kToggle }, { "LOOP", kToggle }, { "SMOOTH", kToggle },
    { "CYCLE LINK", kToggle }, { "LOOP LINK", kToggle }, { "POINT", kNumber },
    { "X", kNumber }, { "Y", kNumber }, { "CURVE", kNumber }, { "GRID X", kNumber },
    { "GRID Y", kNumber }, { "PHASE", kNumber }, { "BEND ALL", kNumber }, { "MODE", kChoice }
};
constexpr const char* kTools[] { "POINT", "FLAT", "RAMP+", "RAMP-", "SELECT", "SOFT", "BEND" };
constexpr int kToolCount = static_cast<int> (std::size (kTools));
constexpr int kModeCount = 6;
constexpr Field kOptional[] { kUndo, kRedo, kDelete, kMod, kLoop, kLoopLink, kPoint, kX, kY, kCurve, kBend };
constexpr int kAvailabilityCount = static_cast<int> (std::size (kOptional));
static_assert (kAvailability == kValues + kCount && kScale == kAvailability + kAvailabilityCount && kHandles == kScale + 1);
static_assert (ui::lfo::kMaxPoints <= 255);

struct Range
{
    int min = 0, max = 1, units = 1;
    float encode (double plain) const noexcept
    {
        return static_cast<float> (std::clamp ((plain * units - min) / (max - min), 0.0, 1.0));
    }
    double decode (float value) const noexcept
    {
        return (min + std::lround (value * static_cast<float> (max - min))) / static_cast<double> (units);
    }
};

struct Context
{
    int owner = 0;                          // 0 = outside the standalone Shape Editor
    uint64_t preset = 0, shape = 0;         // preset generation and externally reloaded shape
    ui::lfo::PointSet selection {};
    int focus = -1, points = 0;             // primary point can change with an unchanged multi-selection

    bool operator== (const Context& other) const noexcept
    {
        return owner == other.owner && preset == other.preset && shape == other.shape
            && focus == other.focus && points == other.points && selection == other.selection;
    }
};

struct State
{
    Context context {};
    uint32_t enabled = 0;
    std::array<float, kCount> values {};

    bool allows (int field) const noexcept
    {
        return context.owner != 0 && field >= 0 && field < kCount && (enabled & (1u << field)) != 0;
    }
    bool scale() const noexcept { return context.selection.count() > 1; }
    FxCell cell (int field) const noexcept
    {
        return allows (field) ? FxCell { field, context.owner, kFxDial } : FxCell {};
    }
};

inline Range rangeOf (int field, int points, bool scale) noexcept
{
    switch (field)
    {
        case kTool: return { 0, kToolCount - 1 };
        case kPoint: return { 0, std::max (2, points) };
        case kX: case kY: return { 0, 100, 100 };
        case kCurve: return scale ? Range { 1, 100, 100 } : Range { -20, 20 };
        case kGridX: case kGridY: return { ui::lfo::kMinGrid, ui::lfo::kMaxGrid };
        case kPhase: return { 0, 360 };
        case kBend: return { -20, 20 };
        case kMode: return { 0, kModeCount - 1 };
        default: return {};
    }
}

class Bank : public NativeCellBank<kCount>
{
public:
    struct Display
    {
        int points = 0, selected = 0;
        bool shown = false, scale = false;
    };

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
        NativeCellBank<kCount>::publish (context_.owner != 0 ? 1 : 0, cells.data());
        display_.store (static_cast<uint32_t> ((context_.owner != 0 ? 1 : 0) | (context_.points << 1)
                        | (context_.selection.count() << 9)), std::memory_order_release);
    }

    Display display() const noexcept
    {
        const uint32_t bits = display_.load (std::memory_order_acquire);
        const auto points = static_cast<int> ((bits >> 1) & 255u);
        const auto selected = static_cast<int> (bits >> 9);
        return { points, selected, (bits & 1u) != 0, selected > 1 };
    }
    float value (int field) const noexcept { return values_[field].load (std::memory_order_relaxed); }

    bool take (const State& expected, int field, float& value) noexcept
    {
        if (! (context_ == expected.context))
        {
            // Drain a write for a selection/preset that changed since publication.
            NativeCellBank<kCount>::take (field, {}, value);
            return false;
        }
        return NativeCellBank<kCount>::take (field, expected.cell (field), value);
    }

private:
    Context context_;                    // overlay thread only
    std::atomic<uint32_t> display_ { 0 };
    std::atomic<float> values_[kCount] {};
};
}
