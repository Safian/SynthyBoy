// SynthyBoy - native controls for the compact Play ENV and LFO panels.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"

#include <array>

namespace synthyboy_mpc::nativeui::playenvlfo
{
enum View { Hidden, LfoHz, LfoBpm, LfoKey, RndHz, RndBpm, RndKey, LastView = RndKey };
enum Field { Shape, Rate, Division, Style, Sync, Stereo, Choose, StandardSync, Count };
constexpr int kOriginalHandles = 1 + StandardSync;  // StandardSync is appended after compact OSC.
constexpr int kStandardLfos = 10, kSources = kStandardLfos + 4, kLastChoice = kSources - 1;
constexpr float choiceValue (int source) noexcept { return static_cast<float> (std::max (1, source) - 1) / kLastChoice; }
/** A field whose value is chosen only in the Play page's own list (native_play_list.h: the LFO / RND selector, the shape, the division, the style, the sync): the plugin applies the row
    (overlay/play_view.h), the host's handle only shows the value and a host write to it is refused. */
constexpr bool listOnly (int field) noexcept
{
    return field == Choose || field == Shape || field == Division || field == Style || field == Sync;
}
constexpr const char* kNames[] { "SHAPE", "RATE", "DIVISION", "STYLE", "SYNC", "STEREO", "Play LFO Choose", "Play LFO Sync Mode" };

struct Context
{
    View view = Hidden;
    int env = 0, lfo = 0;
    uint64_t preset = 0, selection = 0;

    bool operator== (const Context& other) const noexcept
    {
        return view == other.view && env == other.env && lfo == other.lfo && preset == other.preset && selection == other.selection;
    }
};

struct State
{
    Context context;
    std::array<FxCell, Count> fields {};
};

class Bank : public NativeCellBank<Count>
{
public:
    void suspend() noexcept
    {
        suspended_.store (true, std::memory_order_release);
    }

    void publish (const State& state, bool interactive = true) noexcept
    {
        if (! interactive)
            suspend();
        if (suspended_.load (std::memory_order_relaxed))
            renewInputRevisions();
        if (! (state.context == context_))
            invalidate();
        context_ = state.context;
        source_.store (context_.lfo, std::memory_order_relaxed);
        NativeCellBank<Count>::publish (context_.view, state.fields.data());
        publishedPreset_.store (context_.preset, std::memory_order_release);
        publishedSelection_.store (context_.selection, std::memory_order_release);
        suspended_.store (! interactive, std::memory_order_release);
    }

    void post (int field, float value, uint64_t preset, uint64_t selection) noexcept
    {
        if (publishedPreset_.load (std::memory_order_acquire) != preset
            || publishedSelection_.load (std::memory_order_acquire) != selection)
            return;
        const auto link = at (field);
        if (suspended_.load (std::memory_order_acquire) || listOnly (field))
            return;
        NativeCellBank<Count>::post (link, value);
    }

    bool suspended() const noexcept { return suspended_.load (std::memory_order_relaxed); }
    int source() const noexcept { return source_.load (std::memory_order_relaxed); }
    float selection() const noexcept { return choiceValue (source()); }

    bool take (const State& expected, int field, float& value) noexcept
    {
        if (suspended_.load (std::memory_order_relaxed))
            return false;
        return NativeCellBank<Count>::take (field, expected.context == context_ ? expected.fields[field] : FxCell {}, value);
    }

private:
    std::atomic<bool> suspended_ { false };
    Context context_;   // overlay thread
    std::atomic<int> source_ { 0 };
    std::atomic<uint64_t> publishedPreset_ {}, publishedSelection_ {};
};
}
