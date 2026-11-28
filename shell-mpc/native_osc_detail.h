// SynthyBoy - native settings of the standalone oscillator detail pages.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_fx_cells.h"

#include <string_view>

namespace synthyboy_mpc::nativeui::oscdetail
{
enum Frame { Hidden, Root, RootSmall, Plain, Graph, GraphWithLoad, LastFrame = GraphWithLoad };
enum Kind { Empty, Uni, Bi, Toggle, Number, Choice, Action, Semitone, Snap, Phase, LastKind = Phase };
constexpr std::string_view kMenuSuffixes[] { "gran.density_sync", "gran.length_sync", "gran.window_shape", "gran.y_axis",
    "morph.type", "sample.fade_edges", "spec.flt_table", "unison.stack", "warp2.type", "spec.y_axis" };
constexpr int kMenus = static_cast<int> (std::size (kMenuSuffixes)), kFirstMenuKind = LastKind + 1, kVisualKinds = Action + kMenus;
constexpr int kColumns = 8, kRows = 4, kCells = kColumns * kRows;
constexpr int kMore = kCells * 2, kBack = kMore + 1, kLoad = kBack + 1;
constexpr int kRootLoad = kLoad + 1, kRootLoads = 3, kBindings = kRootLoad + kRootLoads;
constexpr int kView = 0, kValue = 1, kStep = kValue + kCells, kKind = kStep + kCells;
constexpr int kNavigation = kKind + kCells, kMenusBase = kNavigation + 3, kHandles = kMenusBase + kMenus;
static_assert (kHandles == kOscDetailHandles && kOscRootLoad + kRootLoads == kOscRootLoadView);
constexpr int kLeft = 8, kWidth = 158, kHeight = 92, kTop = 172, kStep = 116;
constexpr int kGraphY = 68, kGraphHeight = 200;
constexpr int kBackX = 4, kBackY = 4, kBackW = 120, kBackH = 56;
constexpr int kLoadX = 490, kLoadY = 196, kLoadW = 300, kLoadH = 64;

constexpr int slot (int column, int row) noexcept { return column * kRows + row; }
constexpr int kind (int code) noexcept
{
    if (code == Semitone || code == Phase) return Number;
    if (code == Snap) return Toggle;
    if (code >= kFirstMenuKind) return 1 + Action + code - kFirstMenuKind;
    return code;
}

struct State
{
    Frame frame = Hidden;
    int source = 0, page = 0, type = 0;
    uint64_t preset = 0;
    std::array<FxCell, kBindings> fields {};
    std::array<const char*, kBindings> labels {};
};

class Bank : public NativeCellBank<kBindings>
{
public:
    void suspend() noexcept
    {
        suspended_.store (true, std::memory_order_release);
        setMenu (-1);
        renewInputRevisions();
    }

    void publish (const State& next, bool interactive = true) noexcept
    {
        if (! interactive)
            suspended_.store (true, std::memory_order_release);
        if (suspended_.load (std::memory_order_relaxed))
            renewInputRevisions();
        if (next.frame == Hidden)
            setMenu (-1);
        if (source_ != next.source || page_ != next.page || type_ != next.type || preset_ != next.preset)
            invalidate();
        source_ = next.source;
        page_ = next.page;
        type_ = next.type;
        preset_ = next.preset;
        for (int i = 0; i < kBindings; ++i)
            names_[i].store (next.labels[i], std::memory_order_release);
        NativeCellBank<kBindings>::publish (next.frame, next.fields.data());
        announcedPreset_.store (next.preset, std::memory_order_release);
        suspended_.store (! interactive, std::memory_order_release);
    }

    bool suspended() const noexcept { return suspended_.load (std::memory_order_acquire); }

    const char* name (int i) const noexcept
    {
        return i >= 0 && i < kBindings ? names_[i].load (std::memory_order_acquire) : nullptr;
    }

    void post (Snapshot link, float value, uint64_t preset) noexcept
    {
        if (suspended_.load (std::memory_order_acquire))
            return;
        if (announcedPreset_.load (std::memory_order_acquire) == preset)
            NativeCellBank<kBindings>::post (link, value);
    }

    void setMenu (int menu) noexcept
    {
        if (menu_ == menu)
            return;
        menu_ = menu;
        for (int i = 0; i < kMenus; ++i)
            menus_[i].store (menu == -2 || menu == i ? domain (kFirstMenuKind + i).link : 0, std::memory_order_release);
    }

    void postMenu (int menu, float value, uint64_t preset) noexcept
    {
        if (suspended_.load (std::memory_order_acquire) || menu < 0 || menu >= kMenus)
            return;
        const auto snapshot = domain (kFirstMenuKind + menu);
        if (snapshot.link != 0 && menus_[menu].load (std::memory_order_acquire) == snapshot.link)
            post (snapshot, value, preset);
    }

    bool take (const State& expected, int field, float& value) noexcept
    {
        if (suspended_.load (std::memory_order_relaxed) || field < 0 || field >= kBindings)
            return false;
        const bool match = source_ == expected.source && page_ == expected.page && type_ == expected.type && preset_ == expected.preset;
        return NativeCellBank<kBindings>::take (field, match ? expected.fields[field] : FxCell {}, value);
    }

private:
    std::atomic<bool> suspended_ { true };
    int source_ = 0, page_ = 0, type_ = 0;
    uint64_t preset_ = 0;
    std::atomic<uint64_t> announcedPreset_ { 0 };
    std::array<std::atomic<const char*>, kBindings> names_ {};
    std::array<std::atomic<uint64_t>, kMenus> menus_ {};
    int menu_ = -1;
};
}
