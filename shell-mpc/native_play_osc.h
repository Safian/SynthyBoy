// SynthyBoy - source-bound host controls for the compact Play oscillator.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "native_fx_cells.h"
#include "native_osc_controls.h"

namespace synthyboy_mpc::nativeui::playoscui
{
enum Field { Power, Reserved, Route, First, Second, Pan, Level, kFields };
enum Layout { Hidden, Sub, AbcWave, AbcContent, Noise, kLastLayout = Noise };
constexpr int kSources = osc::kLastSource, kHandles = 1 + kSources * kFields;
constexpr int kRoutingDetails = osc::kLastRoute + 1;
constexpr int kLastView = 8;
constexpr int kNoPopup = -1, kOtherPopup = -2;

struct State
{
    int source = 0, layout = Hidden, variant = 0;
    bool powered = false;
    uint64_t preset = 0, selection = 0;
    std::array<FxCell, kFields> fields {};

    bool sameBinding (const State& other) const noexcept
    {
        return source == other.source && layout == other.layout && variant == other.variant
            && powered == other.powered && preset == other.preset && selection == other.selection;
    }
};

class Bank
{
public:
    using Link = NativeCellBank<kFields>::Snapshot;

    void suspend() noexcept
    {
        suspended_.store (true, std::memory_order_release);
        setPopup (kNoPopup);
    }

    void publish (const State& state, bool interactive = true) noexcept
    {
        if (! interactive)
            suspend();
        if (suspended_.load (std::memory_order_relaxed))
            links_.renewInputRevisions();
        if (state.source == 0)
            setPopup (kNoPopup);
        if (! state_.sameBinding (state))
            links_.invalidate();
        links_.publish (state.layout, state.fields.data());
        state_ = state;
        preset_.store (state.preset, std::memory_order_release);
        selection_.store (state.selection, std::memory_order_release);
        view_.store (state.source | (state.layout << 3) | (state.powered ? 1 << 6 : 0), std::memory_order_release);
        suspended_.store (! interactive, std::memory_order_release);
    }

    int view() const noexcept { return view_.load (std::memory_order_acquire); }
    static int source (int view) noexcept { return view & 7; }
    static int layout (int view) noexcept { return (view >> 3) & 7; }
    static bool powered (int view) noexcept { return (view & (1 << 6)) != 0; }
    static int frame (int view) noexcept
    {
        const int s = source (view);
        if (s <= 1)
            return s;
        if (s == kSources)
            return kLastView;
        return 2 + 2 * (s - 2) + (layout (view) == AbcContent ? 1 : 0);
    }
    Link at (int field) const noexcept { return links_.at (field); }
    void report (Link link) noexcept { links_.report (link); }

    static bool usesPopup (int kind) noexcept
    {
        return kind == osc::SettingsType || kind == osc::SettingsMenu || kind == osc::SettingsNumber;
    }

    void setPopup (int field) noexcept
    {
        if (popup_ == field)
            return;
        popup_ = field;
        if (field != kNoPopup)
        {
            float discarded;
            for (int i = 0; i < kFields; ++i)
                links_.take (i, {}, discarded);
        }
        const auto link = links_.at (field);
        popupLink_.store (usesPopup (link.cell().kind) ? link.link : 0, std::memory_order_release);
        popupField_.store (field, std::memory_order_release);
    }

    void post (int fromSource, int field, float value, uint64_t preset, uint64_t selection) noexcept
    {
        const int current = view();
        if (fromSource < 1 || field < 0 || field >= kFields || ! std::isfinite (value) || value < 0.0f || value > 1.0f
            || fromSource != source (current) || (field != Power && ! powered (current))
            || preset_.load (std::memory_order_acquire) != preset
            || selection_.load (std::memory_order_acquire) != selection)
            return;
        const auto link = links_.at (field);
        if (suspended_.load (std::memory_order_acquire))
            return;
        const auto popup = popupField_.load (std::memory_order_acquire);
        if ((popup != kNoPopup && popup != field) || (usesPopup (link.cell().kind)
            && (popup != field || popupLink_.load (std::memory_order_acquire) != link.link)))
            return;
        links_.post (link, value);
    }

    bool take (const State& expected, int field, float& value) noexcept
    {
        if (suspended_.load (std::memory_order_relaxed))
            return false;
        const bool allowed = expected.sameBinding (state_) && (field == Power || expected.powered);
        const bool taken = links_.take (field, allowed ? expected.fields[field] : FxCell {}, value);
        if (taken && osc::settingsAction (expected.fields[field].kind))
            acknowledge_[field] = true;
        return taken;
    }

    /** Only the visible source has reports. Presentation follows its values. */
    template <class Value, class Report>
    void poll (float lastFrame, Value&& read, Report&& send, bool prepared = false)
    {
        if (! prepared && suspended_.load (std::memory_order_relaxed))
            return;
        const int presentation = view();
        const int selected = source (presentation);
        const float frameValue = static_cast<float> (frame (presentation)) / kLastView;
        if (lastFrame != 0.0f && lastFrame != frameValue)
            send (0, 0.0f, false);
        if (selected > 0)
            for (int field = 0; field < kFields; ++field)
            {
                const auto link = links_.at (field);
                const auto cell = link.cell();
                if (cell.target >= 0)
                {
                    send (1 + (selected - 1) * kFields + field, read (field, cell),
                          links_.needsReport (link) || acknowledge_[field]);
                    links_.report (link);
                }
            }
        acknowledge_.fill (false);
        send (0, frameValue, false);
    }

private:
    std::atomic<bool> suspended_ { false };
    State state_;
    NativeCellBank<kFields> links_;
    std::atomic<int> view_ {}, popupField_ { kNoPopup };
    std::atomic<uint64_t> preset_ {}, selection_ {}, popupLink_ {};
    int popup_ = kNoPopup;   // overlay thread only
    std::array<bool, kFields> acknowledge_ {};
};
}
