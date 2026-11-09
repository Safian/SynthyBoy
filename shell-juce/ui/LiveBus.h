// SynthyBoy - UI: the live values of the engine for the displays (GROUPA2 WP-17, docs/ui-design/13-new-keys.md 4.32).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One bus per editor (UiContext::live). A display that wants live values subscribes the knobs it shows (their table indices) and adds a listener; the bus
// merges all subscriptions into the one target list the engine takes (UiHost::setReadoutTargets), copies the engine's LiveValues 30 times a second while
// anything listens (UiHost::copyReadout) and tells the listeners. It keeps the short history of the random LFOs for their traces. Message thread only.
#pragma once

#include <juce_events/juce_events.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "UiHost.h"
#include "synthyboy/readout.h"

namespace synthyboy::ui
{
class LiveBus final : private juce::Timer
{
public:
    explicit LiveBus (UiHost& hostToUse) : host (hostToUse) {}
    ~LiveBus() override { bound_.clear(); stopTimer(); }

    static constexpr int kHistory = 160;     // samples of the history of a random LFO: about 5 s at 30 Hz
    static constexpr int kPollHz = 30;

    /** The knobs `owner` shows (table indices); replaces the earlier list of the owner. An empty list keeps the owner as a listener without knobs. */
    void subscribe (const void* owner, std::vector<int> paramIndices);
    /** `owner` wants a call after every update of the values. Starts the timer. */
    void listen (const void* owner, std::function<void()> callback);
    /** Takes the owner out (its knobs and its listener). Stops the timer when nobody listens any more. */
    void remove (const void* owner);

    /** The modulated plain value of a knob that is in the target list of the last update; `fallback` when it is not (or the engine has no read-out). */
    float liveOf (int paramIndex, float fallback) const noexcept;
    bool hasLive (int paramIndex) const noexcept;
    /** True once the engine delivered a block. */
    bool available() const noexcept { return values_.block != 0; }
    const LiveValues& values() const noexcept { return values_; }
    /** The value of the modulation source with choice value `choice` (1 ..), 0 when unknown. */
    float source (int choice) const noexcept { return choice >= 1 && choice <= LiveValues::kMaxSources ? values_.source[choice - 1] : 0.0f; }
    /** History of the random LFO n (1 .. 4), oldest first; kHistory values, -1 .. 1 (0 before anything was read). */
    std::vector<float> randomHistory (int n) const;

    /** A display binds itself: while `component` and all its parents are visible, the bus subscribes the knobs `wanted ()` returns (asked again at every update: a
        modulation may come and go) and calls `tick` after every update; hidden, the component costs nothing. Unbound when the component is deleted. */
    void bind (juce::Component& component, std::function<std::vector<int>()> wanted, std::function<void()> tick);
    void unbind (const juce::Component* component);
    /** True when the component and every parent are visible (a component of a hidden tab is not; a window is not needed, so a headless test counts as shown). */
    static bool shown (const juce::Component& component) noexcept;

    /** One update: merge the subscriptions, hand the target list to the host if it changed, copy the values, tell the listeners. The timer calls it; the tests call it. */
    void poll();
    /** The target list as handed to the host (the tests look at it). */
    const std::vector<int>& targets() const noexcept { return targets_; }
    int listeners() const noexcept { return static_cast<int> (owners_.size()); }
    uint32_t updates() const noexcept { return updates_; }

private:
    void timerCallback() override { poll(); }
    void updateTimer();
    void rebuildTargets();

    struct Owner
    {
        const void* key = nullptr;
        std::vector<int> params;
        std::function<void()> callback;
    };
    Owner* find (const void* owner);

    struct Bound final : private juce::ComponentListener
    {
        Bound (LiveBus& busToUse, juce::Component& componentToWatch, std::function<std::vector<int>()> w, std::function<void()> t);
        ~Bound() override;
        void update();
        LiveBus& bus;
        juce::Component* component;
        std::function<std::vector<int>()> wanted;
        std::function<void()> tick;
        bool active = false;

    private:
        void componentVisibilityChanged (juce::Component&) override { update(); }
        void componentParentHierarchyChanged (juce::Component&) override { update(); }
        void componentBeingDeleted (juce::Component&) override;
    };
    std::vector<std::unique_ptr<Bound>> bound_;

    UiHost& host;
    std::vector<Owner> owners_;
    std::vector<int> targets_;
    bool targetsDirty = true;
    LiveValues values_;
    uint32_t updates_ = 0;
    std::array<std::array<float, kHistory>, 4> history_ {};
    int historyHead_ = 0;
    int historyFilled_ = 0;
    uint32_t lastBlock_ = 0;
};
} // namespace synthyboy::ui
