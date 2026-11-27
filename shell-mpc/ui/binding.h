// SynthyBoy - MPC plugin layer: the PARAMETER BINDING DATA of the native controls (owner decision 2026-11-27: the C++ layer has no UI elements).
// A control exists only in the host skin (components/native). The plugin knows, for every control the skin shows, one plain row: which engine parameter it is, what kind of value it carries, its
// range / step / default, how its value is written as text and which Q-Link slot it has. Pure data and pure functions: no virtuals, no drawing, no touch, no state. A page holds its rows as a
// constexpr table; the Q-Link banks are tables of ROW INDICES (QTable). The engine parameter index is resolved by the page (uibind::indexOfKey (row.key)), so this header knows no engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace synthyboy_mpc { namespace binding {

/** What the value of a row is. Continuous: a normalised 0 .. 1 value the skin dial / slider / fader writes. Toggle: 0 / 1. Choice: a whole number 0 .. numChoices - 1 (a dropdown, a radio, a selector). Command: no stored value,
    the skin button only triggers an action of the plugin. */
enum class Kind : uint8_t { Continuous, Toggle, Choice, Command };

/** How the value is written as text (effGetParamDisplay, the cell value of the native bridge). Engine: the parameter's own unit and format (uibind::formatValue). The other rules are fixed by the row. */
enum class Format : uint8_t { Engine, PanLR, Percent, Db, IntSigned, IntPlain, Decimal2, Degrees, ChoiceName };

constexpr int kNoParam = -1;
constexpr int kNoSlot = -1;

/** One control of a page. `min` / `max` / `step` / `def` are in the plain unit of the parameter; `min == max` means "as the engine parameter table says" (the page reads ParamInfo). `qGroup` / `qSlot`: the Q-Link group of the
    page (0 ..) and the slot in it (0 .. 3); `kNoSlot` = touch only (no hardware knob). `inverted`: the stored value is the opposite of what the control shows (a bypass shown as a power switch). */
struct Row
{
    int id = 0;                          // the page-local id the native bridge reports (the cell / handle id of the skin)
    const char* key = "";                // the engine parameter key ("osc1.level"); "" = no engine parameter (a command)
    Kind kind = Kind::Continuous;
    float min = 0.0f, max = 0.0f, step = 0.0f, def = 0.0f;
    Format format = Format::Engine;
    const char* const* choices = nullptr;   // Kind::Choice: the names; null = the engine's own choice names
    uint8_t numChoices = 0;
    int8_t qGroup = static_cast<int8_t> (kNoSlot);
    int8_t qSlot = static_cast<int8_t> (kNoSlot);
    bool inverted = false;

    constexpr bool hasParam() const noexcept { return key != nullptr && key[0] != '\0'; }
    constexpr bool hasRange() const noexcept { return max != min; }
    constexpr bool hasQ() const noexcept { return qSlot >= 0; }
};

/** The Q-Link banks of a page: `rows[group][slot]` is the INDEX into the page's row table (-1: empty slot). Four slots per group, `groups` groups. No interface, no component: the router reads the table. */
template <int G>
struct QTable
{
    static constexpr int kGroups = G;
    static constexpr int kSlots = 4;
    int16_t rows[G][kSlots];

    constexpr int groups() const noexcept { return G; }
    constexpr int row (int group, int slot) const noexcept { return group >= 0 && group < G && slot >= 0 && slot < kSlots ? rows[group][slot] : -1; }
    /** The group and slot of a row index (false: the row is no Q member). */
    constexpr bool find (int rowIndex, int& group, int& slot) const noexcept
    {
        for (int g = 0; g < G; ++g)
            for (int s = 0; s < kSlots; ++s)
                if (rows[g][s] == rowIndex && rowIndex >= 0)
                {
                    group = g;
                    slot = s;
                    return true;
                }
        return false;
    }
    constexpr int members (int group) const noexcept
    {
        int n = 0;
        for (int s = 0; s < kSlots; ++s)
            n += row (group, s) >= 0;
        return n;
    }
};

/** The row index with page id `id` in a table of n rows (-1: none). */
inline int indexOfId (const Row* rows, int n, int id) noexcept
{
    for (int i = 0; i < n; ++i)
        if (rows[i].id == id)
            return i;
    return -1;
}

// ---- value rules (pure): the same functions the page uses for the Q-Link turn, the wheel and the write of a native cell ----------------------------------------------------------------

/** One Q-Link detent on a continuous row: 1 / 128 of the range (the hardware knob step). */
constexpr double kQStep = 1.0 / 128.0;

/** The normalised value after `delta` (normalised units), clamped to 0 .. 1. */
inline double turnNorm (double norm, double delta) noexcept { return std::clamp (norm + delta, 0.0, 1.0); }

/** The index of a Choice row after `delta` whole steps, clamped to 0 .. n - 1 (no wrap). */
inline int turnChoice (int index, int delta, int n) noexcept { return std::clamp (index + delta, 0, std::max (0, n - 1)); }

/** The shown state of a Toggle row: `plain` is the stored parameter value, `inverted` the row flag. */
constexpr bool toggleOn (double plain, bool inverted) noexcept { return (plain >= 0.5) != inverted; }
/** The value to store for a shown state. */
constexpr double toggleStore (bool on, bool inverted) noexcept { return (on != inverted) ? 1.0 : 0.0; }

/** A value that rounds to zero has no sign ("-0.0 dB" is written "0.0 dB"). */
inline double noNegZero (double v, int decimals) noexcept
{
    const double scale = std::pow (10.0, decimals);
    return std::round (v * scale) == 0.0 ? 0.0 : v;
}

/** Writes `plain` by the row's own rule into out (always zero terminated). Format::Engine and ChoiceName are written by the page (it knows the parameter table): here they give "" and return false. */
inline bool formatPlain (Format f, double plain, char* out, size_t cap) noexcept
{
    if (out == nullptr || cap == 0)
        return false;
    out[0] = '\0';
    switch (f)
    {
        case Format::PanLR:
            if (std::fabs (plain) < 0.5)
                std::snprintf (out, cap, "C");
            else
                std::snprintf (out, cap, plain < 0.0 ? "L %.0f" : "R %.0f", std::fabs (plain));
            return true;
        case Format::Percent: std::snprintf (out, cap, "%.0f%%", noNegZero (plain, 0)); return true;
        case Format::Db: std::snprintf (out, cap, "%.1f dB", noNegZero (plain, 1)); return true;
        case Format::IntSigned: { const long n = std::lround (plain); if (n == 0) std::snprintf (out, cap, "0"); else std::snprintf (out, cap, "%+ld", n); return true; }
        case Format::IntPlain: std::snprintf (out, cap, "%ld", std::lround (plain)); return true;
        case Format::Decimal2: std::snprintf (out, cap, "%.2f", noNegZero (plain, 2)); return true;
        case Format::Degrees: std::snprintf (out, cap, "%ld\xc2\xb0", std::lround (plain)); return true;
        case Format::Engine:
        case Format::ChoiceName: return false;
    }
    return false;
}

}} // namespace synthyboy_mpc::binding
