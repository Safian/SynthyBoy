// SynthyBoy - native selection and routing of the standalone oscillator views.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "native_fx_cells.h"

namespace synthyboy_mpc::nativeui::osc
{
enum Field { kSource, kRouting, kVisibility, kCount };
constexpr int kPower = kCount;   // one mailbox, reachable through the currently selected source's power handle
enum Visibility { kHidden, kRootOff, kRootOn };
constexpr int kLastSource = 5, kLastRoute = 6;
constexpr int kSubSettings = 7, kNoiseSettings = 12, kSettings = kSubSettings + kNoiseSettings;
enum SettingsView { SettingsHidden, SettingsSub, SettingsNoise, SettingsNoiseSample,
                    SettingsWavetable, SettingsSample, SettingsGranular, SettingsSpectral, SettingsMultisample,
                    kLastSettings = SettingsMultisample };
enum SettingsKind { SettingsDial = 1, SettingsToggle, SettingsMenu, SettingsNumber, SettingsPhase,
                    SettingsSource, SettingsAction, SettingsOctave, SettingsType, SettingsVoices };
enum AbcSetting { AbcType, AbcSource, AbcBrowse, AbcOctave, AbcFine, AbcUnisonDetail, AbcUnisonCount, AbcDetune,
                  AbcWarp1Type, AbcWarp1Amount, AbcWarp2Type, AbcWarp2Amount, AbcPosition,
                  AbcDensity, AbcLength, AbcOffset, AbcDirection, AbcPitch, kAbcSettings };
enum SourceAction { SourceIdle, SourcePrevious, SourceBrowse, SourceNext, kLastSourceAction = SourceNext };
constexpr int kAbcSources = 3, kLastWarpMask = 3;
constexpr int kSettingsAll = kSettings + kAbcSources * kAbcSettings;
constexpr bool settingsAction (int kind) noexcept { return kind == SettingsSource || kind == SettingsAction; }
constexpr int abcSettingIndex (int source, int field) noexcept { return kSettings + (source - 2) * kAbcSettings + field; }
constexpr int kTypeChoices[] { 0, 4, 1, 2, 3 };    // order of the five TYPE pictograms
constexpr const char* kNames[kCount] = { "Osc Source", "Osc Routing", "Osc Native View" };
constexpr const char* kSourceNames[kLastSource] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" };
constexpr const char* kRoutingNames[kLastSource] = { "Osc Sub Routing", "Osc A Routing", "Osc B Routing", "Osc C Routing", "Osc Noise Routing" };
constexpr const char* kPowerNames[kLastSource] = { "Osc Sub Power", "Osc A Power", "Osc B Power", "Osc C Power", "Osc Noise Power" };
constexpr const char* kDestinationNames[kLastRoute + 1] = {
    "Filter 1", "Filter 2", "Filters 1+2", "Effects only", "Direct out", "Filter blend", "None"
};

struct ViewState
{
    int source = 0;     // 1 Sub, 2..4 Osc A..C, 5 Noise, 0 hidden
    bool routable = false;
    uint64_t preset = 0;
    int destination = 0;
    bool powered = false;

    bool sameBinding (const ViewState& other) const noexcept
    {
        return source == other.source && routable == other.routable && powered == other.powered && preset == other.preset;
    }
};

/** The selector/routing commands belong to the announced root view. A source,
    power, preset or root change invalidates a command issued for the old view. */
class Bank
{
public:
    struct Snapshot
    {
        uint64_t bits = 0;
        int source() const noexcept { return static_cast<int> (bits & 7); }
        int destination() const noexcept { return static_cast<int> ((bits >> 3) & 7); }
        int visibility() const noexcept { return source() == 0 ? kHidden : (bits & 64) != 0 ? kRootOn : kRootOff; }
        bool displaysSource (int which) const noexcept { return source() != 0 && source() == which; }
        float routingValue (int which) const noexcept { return displaysSource (which) ? value (kRouting) : 0.0f; }
        bool powered() const noexcept { return (bits & 128) != 0; }
        float powerValue (int which) const noexcept { return source() == which && powered() ? 1.0f : 0.0f; }
        uint32_t bindingRevision() const noexcept { return static_cast<uint32_t> ((bits >> 8) & 0xffffffu); }
        uint64_t revision() const noexcept { return bits >> 32; }
        float value (int field) const noexcept
        {
            if (field == kSource)
                return source() > 0 ? static_cast<float> (source() - 1) / (kLastSource - 1) : 0.0f;
            if (field == kRouting)
                return static_cast<float> (destination()) / kLastRoute;
            return static_cast<float> (visibility()) / kRootOn;
        }
    };

    void suspend() noexcept
    {
        suspended_.store (true, std::memory_order_release);
        for (auto& mailbox : pending_)
            mailbox.store (0, std::memory_order_release);
        renewInputRevision();
    }

    void publish (const ViewState& next, bool interactive = true) noexcept
    {
        if (! interactive)
            suspended_.store (true, std::memory_order_release);
        const bool wasSuspended = suspended_.load (std::memory_order_relaxed);
        const bool bindingChanged = ! view_.sameBinding (next);
        if (bindingChanged)
        {
            bindingRevision_ = (bindingRevision_ + 1) & 0xffffffu;
            if (bindingRevision_ == 0)
                bindingRevision_ = 1;
        }
        const uint64_t prevRev = revision_;
        if ((bindingChanged || wasSuspended) && ++revision_ == 0)
            ++revision_;
        view_ = next;
        const auto source = static_cast<uint64_t> (std::clamp (next.source, 0, kLastSource));
        const auto route = static_cast<uint64_t> (std::clamp (next.destination, 0, kLastRoute));
        const uint64_t state = source | (route << 3) | (next.routable ? 64 : 0) | (next.powered ? 128 : 0)
                             | (static_cast<uint64_t> (bindingRevision_) << 8);
        state_.store ((static_cast<uint64_t> (revision_) << 32) | state, std::memory_order_release);
        if (! bindingChanged && wasSuspended)
        {
            uint64_t expected = prevRev;
            reported_.compare_exchange_strong (expected, revision_, std::memory_order_acq_rel);
        }
        suspended_.store (! interactive, std::memory_order_release);
    }

    bool suspended() const noexcept { return suspended_.load (std::memory_order_acquire); }
    uint64_t preset() const noexcept { return view_.preset; }
    Snapshot snapshot() const noexcept { return { state_.load (std::memory_order_acquire) }; }
    void report (Snapshot current) noexcept { reported_.store (current.revision(), std::memory_order_release); }

    void post (int field, float value, int source = 0) noexcept
    {
        if (suspended_.load (std::memory_order_acquire)
            || (field != kSource && field != kRouting) || ! std::isfinite (value) || value < 0.0f || value > 1.0f)
            return;
        const auto current = snapshot();
        if (current.source() == 0 || (source != 0 && source != current.source())
            || (field == kRouting && current.visibility() != kRootOn))
            return;
        const int choice = static_cast<int> (std::lround (value * (field == kSource ? kLastSource - 1 : kLastRoute)));
        if (choice == (field == kSource ? current.source() - 1 : current.destination()))
            return;
        enqueue (current, field, choice);
    }

    void postPower (int source, float value) noexcept
    {
        if (suspended_.load (std::memory_order_acquire)
            || ! std::isfinite (value) || value < 0.0f || value > 1.0f || source < 1 || source > kLastSource)
            return;
        const auto current = snapshot();
        const bool on = value >= 0.5f;
        if (current.source() == source && on != current.powered())
            enqueue (current, kPower, on ? 1 : 0);
    }

    bool take (const ViewState& expected, int field, int& choice) noexcept
    {
        if (suspended_.load (std::memory_order_relaxed)
            || (field != kSource && field != kRouting && field != kPower))
            return false;
        const uint64_t pending = pending_[commandSlot (field)].exchange (0, std::memory_order_acq_rel);
        if (pending == 0 || ! view_.sameBinding (expected) || (pending >> 32) != revision_)
            return false;
        choice = static_cast<int> (static_cast<uint32_t> (pending)) - 1;
        return true;
    }

private:
    static int commandSlot (int field) noexcept { return field == kPower ? 2 : field; }

    void renewInputRevision() noexcept
    {
        const uint64_t old = state_.load (std::memory_order_relaxed);
        if (old == 0)
            return;
        uint64_t oldRev = old >> 32;
        if (++revision_ == 0)
            ++revision_;
        const uint64_t renewed = (static_cast<uint64_t> (revision_) << 32) | (old & 0xffffffffull);
        state_.store (renewed, std::memory_order_release);
        reported_.compare_exchange_strong (oldRev, revision_, std::memory_order_acq_rel);
    }

    void enqueue (Snapshot current, int field, int choice) noexcept
    {
        if (suspended_.load (std::memory_order_acquire)
            || reported_.load (std::memory_order_acquire) != current.revision())
            return;
        const uint64_t command = (current.revision() << 32) | static_cast<unsigned> (choice + 1);
        auto& mailbox = pending_[commandSlot (field)];
        uint64_t pending = mailbox.load (std::memory_order_relaxed);
        while (! suspended_.load (std::memory_order_acquire) && snapshot().revision() == current.revision()
               && ! mailbox.compare_exchange_weak (pending, command, std::memory_order_release, std::memory_order_relaxed))
        {}
    }

    std::atomic<bool> suspended_ { true };
    ViewState view_;
    uint32_t revision_ = 0, bindingRevision_ = 0;
    std::atomic<uint64_t> state_ { 0 }, reported_ { 0 }, pending_[3] {};
};

/** Read-only cached settings cell bank for the standalone Osc root panels. */
class SettingsBank : public NativeCellBank<kSettingsAll>
{
public:
    void suspend() noexcept
    {
        suspended_.store (true, std::memory_order_release);
        renewInputRevisions();
    }

    void publish (int view, const FxCell* fields, bool interactive = true) noexcept
    {
        if (! interactive)
            suspended_.store (true, std::memory_order_release);
        if (suspended_.load (std::memory_order_relaxed))
            renewInputRevisions();
        NativeCellBank<kSettingsAll>::publish (view, fields);
        suspended_.store (! interactive, std::memory_order_release);
    }

    void post (Snapshot current, float value) noexcept
    {
        if (suspended_.load (std::memory_order_acquire))
            return;
        NativeCellBank<kSettingsAll>::post (current, value);
    }

    bool take (int slot, const FxCell& expected, float& value) noexcept
    {
        if (suspended_.load (std::memory_order_relaxed))
            return false;
        return NativeCellBank<kSettingsAll>::take (slot, expected, value);
    }

    bool suspended() const noexcept { return suspended_.load (std::memory_order_acquire); }

private:
    std::atomic<bool> suspended_ { true };
};
} // namespace synthyboy_mpc::nativeui::osc
