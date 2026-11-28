// SynthyBoy - append-only native control contract for the standalone LFO tab.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace synthyboy_mpc::nativeui::lfo
{
enum Field
{
    kShape, kStyle, kSmooth, kSmoothTime, kAction, kLayout,
    kRate, kDivision, kRise, kSync, kMode, kRandomMode, kPhase, kDelay, kHost,
    kDirection, kSwing, kStereo, kMono, kRandomStereo, kTranspose, kTune, kCount
};
enum Action { kIdle, kTargets, kExpand, kActionLast = kExpand };
constexpr int kInstances = 14, kStandard = 10, kLayoutLast = 6;
constexpr int kShapeLast = 12, kStyleLast = 4;
constexpr const char* kNames[kCount] = {
    "LFO Waveform", "RND Style", "LFO Smooth", "LFO Smooth Time", "LFO Action", "LFO Layout",
    "LFO Rate", "LFO Division", "LFO Rise", "LFO Sync", "LFO Mode", "RND Mode", "LFO Phase", "LFO Delay",
    "LFO Host", "LFO Direction", "LFO Swing", "LFO Stereo", "LFO Mono", "RND Stereo", "LFO Transpose", "LFO Tune"
};
constexpr const char* kSuffixes[kCount] = {
    "shape", "style", "smooth", "smooth_time", nullptr, nullptr,
    "rate", "division", "fade_in", "sync", "sync_type", "sync_type", "phase", "delay",
    "host", "direction", "swing", "stereo", "mono", "stereo", "keytrack_transpose", "keytrack_tune"
};

struct ViewState
{
    int selection = 0;   // 1..14, zero while hidden
    int layout = 0;      // hidden, standard Hz/BPM/key, then RND Hz/BPM/key
    uint64_t preset = 0;
    uint32_t fields = 0;
    std::array<float, kCount> values {};
    bool operator== (const ViewState& other) const noexcept
    {
        return selection == other.selection && layout == other.layout && preset == other.preset && fields == other.fields;
    }
};
static_assert (kCount <= 24, "the active field mask shares the low bits of a 64-bit publication");
constexpr uint32_t bit (int field) noexcept { return uint32_t { 1 } << field; }

/** Native writes belong to the exact view announced to the host. Leaving the tab,
    changing the instance or opening a panel invalidates pending host gestures. */
class Bank
{
public:
    void publish (const ViewState& next) noexcept
    {
        const bool changed = !(view_ == next);
        if (changed)
        {
            publication_.store (0, std::memory_order_release);
            if (++revision_ == 0)
                ++revision_;
        }
        view_ = next;
        for (int field = 0; field < kCount; ++field)
            values_[field].store (next.values[field], std::memory_order_relaxed);
        if (changed)
            publication_.store (next.layout > 0 ? (static_cast<uint64_t> (revision_) << 32) | next.fields : 0, std::memory_order_release);
    }

    float value (int field) const noexcept { return values_[field].load (std::memory_order_relaxed); }
    uint64_t publication() const noexcept { return publication_.load (std::memory_order_acquire); }
    void report (uint64_t active) noexcept { announced_.store (active, std::memory_order_release); }

    void post (int field, float value) noexcept
    {
        if (! std::isfinite (value) || value < 0.0f || value > 1.0f || field < 0 || field >= kCount)
            return;
        const uint64_t active = publication();
        if ((active & bit (field)) == 0 || announced_.load (std::memory_order_acquire) != active
            || (field == kSmoothTime && this->value (kSmooth) < 0.5f))
            return;
        uint32_t bits;
        std::memcpy (&bits, &value, sizeof bits);
        const uint64_t next = (active & kRevisionMask) | bits;
        uint64_t pending = pending_[field].load (std::memory_order_relaxed);
        // A writer for the previous view cannot overwrite a newer gesture.
        while (publication() == active
               && ! pending_[field].compare_exchange_weak (pending, next, std::memory_order_release, std::memory_order_relaxed))
        {}
    }

    bool take (const ViewState& wanted, int field, float& value) noexcept
    {
        if (field < 0 || field >= kCount)
            return false;
        const uint64_t pending = pending_[field].exchange (0, std::memory_order_acq_rel);
        if (pending == 0 || !(wanted == view_) || (wanted.fields & bit (field)) == 0
            || (pending & kRevisionMask) != (publication() & kRevisionMask))
            return false;
        const auto bits = static_cast<uint32_t> (pending);
        std::memcpy (&value, &bits, sizeof value);
        return true;
    }

private:
    static constexpr uint64_t kRevisionMask = UINT64_C (0xffffffff00000000);
    ViewState view_;  // only the overlay thread uses this
    uint32_t revision_ = 0;
    std::atomic<uint64_t> publication_ { 0 }, announced_ { 0 }, pending_[kCount] {};
    std::atomic<float> values_[kCount] {};
};
}
