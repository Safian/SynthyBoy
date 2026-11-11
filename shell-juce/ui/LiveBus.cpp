// SynthyBoy - UI: the live values of the engine for the displays. See LiveBus.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "LiveBus.h"

#include <algorithm>

namespace synthyboy::ui
{
LiveBus::Owner* LiveBus::find (const void* owner)
{
    for (auto& o : owners_)
        if (o.key == owner)
            return &o;
    return nullptr;
}

void LiveBus::subscribe (const void* owner, std::vector<int> paramIndices)
{
    Owner* o = find (owner);
    if (o == nullptr)
    {
        owners_.push_back ({ owner, {}, nullptr });
        o = &owners_.back();
    }
    if (o->params != paramIndices)
    {
        o->params = std::move (paramIndices);
        targetsDirty = true;
    }
}

void LiveBus::listen (const void* owner, std::function<void()> callback)
{
    Owner* o = find (owner);
    if (o == nullptr)
    {
        owners_.push_back ({ owner, {}, nullptr });
        o = &owners_.back();
    }
    if (! o->callback) // update () registers every shown display again at every poll: only a display that was not listening yet gets the extra call
        deliverNext = true;
    o->callback = std::move (callback);
    updateTimer();
}

void LiveBus::updateTimer()
{
    // 30 Hz while a display listens; 5 Hz while displays are bound but hidden (they have to notice that they are shown again: a parent that becomes visible does not
    // tell its children); nothing at all when nothing is bound
    const bool anyListener = std::any_of (owners_.begin(), owners_.end(), [] (const Owner& o) { return static_cast<bool> (o.callback); });
    if (anyListener)
        startTimerHz (kPollHz);
    else if (! bound_.empty())
        startTimerHz (5);
    else
        stopTimer();
}

void LiveBus::remove (const void* owner)
{
    const auto it = std::remove_if (owners_.begin(), owners_.end(), [owner] (const Owner& o) { return o.key == owner; });
    if (it == owners_.end())
        return;
    owners_.erase (it, owners_.end());
    rebuildTargets(); // the engine stops computing what nobody shows any more
    updateTimer();
}

void LiveBus::rebuildTargets()
{
    std::vector<int> merged;
    for (const auto& o : owners_)
        for (const int p : o.params)
            if (p >= 0 && std::find (merged.begin(), merged.end(), p) == merged.end() && static_cast<int> (merged.size()) < LiveValues::kMaxTargets)
                merged.push_back (p);
    targetsDirty = false;
    if (merged != targets_)
    {
        targets_ = std::move (merged);
        host.setReadoutTargets (targets_);
    }
}

float LiveBus::liveOf (int paramIndex, float fallback) const noexcept
{
    const auto it = std::find (targets_.begin(), targets_.end(), paramIndex);
    if (it == targets_.end())
        return fallback;
    const int slot = static_cast<int> (it - targets_.begin());
    return slot < values_.numTargets ? values_.target[slot] : fallback;
}

bool LiveBus::hasLive (int paramIndex) const noexcept
{
    const auto it = std::find (targets_.begin(), targets_.end(), paramIndex);
    return it != targets_.end() && static_cast<int> (it - targets_.begin()) < values_.numTargets;
}

std::vector<float> LiveBus::randomHistory (int n) const
{
    std::vector<float> out (static_cast<size_t> (kHistory), 0.0f);
    if (n < 1 || n > 4)
        return out;
    const auto& ring = history_[static_cast<size_t> (n - 1)];
    for (int i = 0; i < kHistory; ++i)
        out[static_cast<size_t> (i)] = ring[static_cast<size_t> ((historyHead_ + i) % kHistory)];
    return out;
}

bool LiveBus::shown (const juce::Component& component) noexcept
{
    for (const juce::Component* c = &component; c != nullptr; c = c->getParentComponent())
        if (! c->isVisible())
            return false;
    return true;
}

LiveBus::Bound::Bound (LiveBus& busToUse, juce::Component& componentToWatch, std::function<std::vector<int>()> w, std::function<void()> t)
    : bus (busToUse), component (&componentToWatch), wanted (std::move (w)), tick (std::move (t))
{
    component->addComponentListener (this);
    update();
}

LiveBus::Bound::~Bound()
{
    if (component != nullptr)
        component->removeComponentListener (this);
    if (active)
        bus.remove (this);
}

void LiveBus::Bound::componentBeingDeleted (juce::Component&)
{
    // the component dies first: the entry goes with it (unbind () deletes this)
    component->removeComponentListener (this);
    component = nullptr;
    bus.unbind (nullptr);
}

void LiveBus::Bound::update()
{
    const bool want = component != nullptr && shown (*component);
    if (want)
    {
        bus.subscribe (this, wanted ? wanted() : std::vector<int>());
        bus.listen (this, tick);
        active = true;
    }
    else if (active)
    {
        bus.remove (this);
        active = false;
    }
}

void LiveBus::bind (juce::Component& component, std::function<std::vector<int>()> wanted, std::function<void()> tick)
{
    unbind (&component);
    bound_.push_back (std::make_unique<Bound> (*this, component, std::move (wanted), std::move (tick)));
    updateTimer();
}

void LiveBus::unbind (const juce::Component* component)
{
    // component == nullptr: the entries whose component is gone
    bound_.erase (std::remove_if (bound_.begin(), bound_.end(), [component] (const std::unique_ptr<Bound>& b) { return component == nullptr ? b->component == nullptr : b->component == component; }),
                  bound_.end());
    updateTimer();
}

namespace
{
/** The content of two readouts that a display can show: everything except the block counter, which moves with every audio block. */
bool sameLiveContent (const LiveValues& a, const LiveValues& b) noexcept
{
    return a.activeVoices == b.activeVoices && a.numTargets == b.numTargets && std::equal (std::begin (a.source), std::end (a.source), std::begin (b.source))
           && std::equal (std::begin (a.lfoPhase), std::end (a.lfoPhase), std::begin (b.lfoPhase))
           && std::equal (std::begin (a.envStage), std::end (a.envStage), std::begin (b.envStage))
           && std::equal (std::begin (a.envPosition), std::end (a.envPosition), std::begin (b.envPosition))
           && std::equal (std::begin (a.multisampleZone), std::end (a.multisampleZone), std::begin (b.multisampleZone))
           && std::equal (std::begin (a.multisampleKey), std::end (a.multisampleKey), std::begin (b.multisampleKey))
           && std::equal (a.target, a.target + std::max (0, std::min (a.numTargets, LiveValues::kMaxTargets)), b.target);
}
} // namespace

void LiveBus::poll()
{
    // the displays: shown or hidden now, and the knobs they want (a modulation may have come or gone)
    for (size_t i = 0; i < bound_.size(); ++i)
        bound_[i]->update();
    if (targetsDirty)
        rebuildTargets();
    LiveValues fresh;
    bool changed = false;
    if (host.copyReadout (fresh))
    {
        const bool newBlock = fresh.block != lastBlock_;
        lastBlock_ = fresh.block;
        changed = ! sameLiveContent (fresh, values_);
        values_ = fresh;
        if (newBlock)
        {
            // the random LFOs are the sources rlfo1 .. rlfo4 (choice values 15 .. 18)
            for (int n = 0; n < 4; ++n)
                history_[static_cast<size_t> (n)][static_cast<size_t> (historyHead_)] = values_.source[14 + n];
            historyHead_ = (historyHead_ + 1) % kHistory;
            historyFilled_ = std::min (historyFilled_ + 1, kHistory);
        }
    }
    ++updates_;
    // The listeners are called after every update (the contract); whether there is anything new to draw is changed (): false for an idle engine and a still readout, true for the first
    // update after a listener was added
    changed_ = changed || deliverNext;
    deliverNext = false;
    // a listener may unsubscribe while it is called: walk a copy of the callbacks
    std::vector<std::function<void()>> calls;
    for (const auto& o : owners_)
        if (o.callback)
            calls.push_back (o.callback);
    for (auto& c : calls)
        c();
}
} // namespace synthyboy::ui
