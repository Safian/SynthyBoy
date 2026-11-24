// SynthyBoy - shared host bindings for the modulation target editor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"

namespace synthyboy_mpc::nativeui::targets
{
enum Owner { Hidden, Macros, Env, Lfo, Filter, Mod, PlayEnv, Osc, Mixer, PlayLfo, PlayOsc };
enum Mode { Targets, Modulations };
constexpr int kLastFrame = Mixer;
enum Field { Back, Add, Page, FirstRow };
enum RowField { Amount, Polarity, Active, Remove, RowFields };
constexpr int kRows = 6, kFields = FirstRow + kRows * RowFields;
constexpr int kView = kFields, kPageGate = kView + 1, kRowGate = kPageGate + 1, kHandles = kRowGate + kRows;
static_assert (kHandles == kModTargetsHandles);
constexpr int frame (Owner owner) noexcept { return owner == PlayLfo || owner == PlayOsc ? PlayEnv : owner; }
constexpr int field (int row, RowField part) noexcept { return FirstRow + row * RowFields + part; }

struct State
{
    Owner owner = Hidden;
    Mode mode = Targets;
    const void* presenter = nullptr;
    int subject = 0, page = 0;
    uint64_t preset = 0;
    std::array<FxCell, kFields> bindings {};
};

class Bank : public NativeCellBank<kFields>
{
public:
    void publish (const State& next) noexcept
    {
        if (! matches (next))
            invalidate();
        owner_ = next.owner;
        mode_ = next.mode;
        presenter_ = next.presenter;
        subject_ = next.subject;
        page_ = next.page;
        preset_ = next.preset;
        NativeCellBank<kFields>::publish (frame (next.owner), next.bindings.data());
        modeForHost_.store (next.mode, std::memory_order_release);
        presetForHost_.store (next.preset, std::memory_order_release);
    }

    void hide (const void* presenter) noexcept
    {
        if (presenter != nullptr && presenter == presenter_)
            publish ({});
    }

    Mode mode() const noexcept { return modeForHost_.load (std::memory_order_acquire); }

    void post (Snapshot link, float value, uint64_t preset) noexcept
    {
        if (presetForHost_.load (std::memory_order_acquire) == preset)
            NativeCellBank<kFields>::post (link, value);
    }

    bool take (const State& expected, int slot, float& value) noexcept
    {
        if (slot < 0 || slot >= kFields)
            return false;
        return NativeCellBank<kFields>::take (slot, matches (expected) ? expected.bindings[slot] : FxCell {}, value);
    }

private:
    bool matches (const State& state) const noexcept
    {
        return presenter_ == state.presenter && owner_ == state.owner && mode_ == state.mode
            && subject_ == state.subject && page_ == state.page && preset_ == state.preset;
    }

    Owner owner_ = Hidden;
    Mode mode_ = Targets;
    const void* presenter_ = nullptr;
    int subject_ = 0, page_ = 0;
    std::atomic<Mode> modeForHost_ {Targets};
    uint64_t preset_ = 0;
    std::atomic<uint64_t> presetForHost_ {};
};
}
