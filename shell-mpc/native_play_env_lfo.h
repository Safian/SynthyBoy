// SynthyBoy - native controls for the compact Play ENV and LFO panels.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"

#include <array>

namespace synthyboy_mpc::nativeui::playenvlfo
{
enum View { Hidden, LfoHz, LfoBpm, LfoKey, RndHz, RndBpm, RndKey, LastView = RndKey };
enum Field { ReservedEnv, ReservedLfo, Shape, Rate, Division, Style, Sync, Stereo, Choose, StandardSync, Count };
constexpr int kOriginalHandles = 1 + StandardSync;  // StandardSync is appended after compact OSC.
constexpr int kStandardLfos = 10, kSources = kStandardLfos + 4, kLastChoice = kSources - 1;
constexpr float choiceValue (int source) noexcept { return static_cast<float> (std::max (1, source) - 1) / kLastChoice; }
constexpr int kFirstPopup = 3;
constexpr int kPopupFields[] { Choose, Shape, Division, Style, Sync };
constexpr int popupField (int action) noexcept
{
    const int index = action - kFirstPopup;
    return index >= 0 && index < static_cast<int> (std::size (kPopupFields)) ? kPopupFields[index] : -1;
}
constexpr bool usesPopup (int field) noexcept
{
    return field == Choose || field == Shape || field == Division || field == Style || field == Sync;
}
constexpr const char* kNames[] { "Reserved", "Reserved", "SHAPE", "RATE", "DIVISION", "STYLE", "SYNC", "STEREO", "Play LFO Choose", "Play LFO Sync Mode" };

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
        setPopup (-1);
    }

    void publish (const State& state, bool interactive = true) noexcept
    {
        if (! interactive)
            suspend();
        if (suspended_.load (std::memory_order_relaxed))
            renewInputRevisions();
        if (state.context.view == Hidden)
            setPopup (-1);
        if (! (state.context == context_))
            invalidate();
        context_ = state.context;
        source_.store (context_.lfo, std::memory_order_relaxed);
        NativeCellBank<Count>::publish (context_.view, state.fields.data());
        publishedPreset_.store (context_.preset, std::memory_order_release);
        publishedSelection_.store (context_.selection, std::memory_order_release);
        suspended_.store (! interactive, std::memory_order_release);
    }

    void setPopup (int field) noexcept
    {
        if (popup_ == field)
            return;
        popup_ = field;
        const auto link = at (field);
        popupLink_.store (usesPopup (field) && link.cell().target >= 0 ? link.link : 0, std::memory_order_release);
        popupField_.store (field, std::memory_order_release);
    }

    void post (int field, float value, uint64_t preset, uint64_t selection) noexcept
    {
        if (publishedPreset_.load (std::memory_order_acquire) != preset
            || publishedSelection_.load (std::memory_order_acquire) != selection)
            return;
        const auto link = at (field);
        if (suspended_.load (std::memory_order_acquire))
            return;
        if (usesPopup (field) && (popupField_.load (std::memory_order_acquire) != field
            || popupLink_.load (std::memory_order_acquire) != link.link))
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
    int popup_ = -1;
    std::atomic<int> source_ { 0 }, popupField_ { -1 };
    std::atomic<uint64_t> publishedPreset_ {}, publishedSelection_ {}, popupLink_ {};
};
}
