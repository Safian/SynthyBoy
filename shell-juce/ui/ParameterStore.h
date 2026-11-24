// SynthyBoy - UI: the narrow parameter-access interface of the editor (host independent).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The UI reads and writes parameters only through ParameterStore, addressed by the INDEX into synthyboy::paramTable() (the key / id of the entry
// is paramTable()[index].key / .id). Two implementations ship:
//   ApvtsParameterStore  (shell-juce/ApvtsParameterStore.h, plug-in side)  juce::AudioProcessorValueTreeState: host automation, gestures, undo of the host
//   PlainParameterStore  (this library)                                   an in-memory table with callbacks, for a host that has its own parameter
//                                                                         store (the Kick app: a CLAP plug-in instance behind PluginSession)
// Threading: every call and every listener callback is on the message thread. A store whose values change on another thread (host automation)
// marshals the notification itself before it calls the listeners. The audio side never touches a ParameterStore.
#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "synthyboy/params.h"
#include "synthyboy/state.h"

namespace synthyboy::ui
{
class ParameterStore
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        /** The value of `index` changed: a UI edit, the host, a state load. */
        virtual void parameterChanged (int /*index*/) {}
        /** A gesture (touch, drag, a single complete edit) on `index` starts or ends: the editor builds its undo steps from these. */
        virtual void parameterGestureChanged (int /*index*/, bool /*starting*/) {}
    };

    virtual ~ParameterStore() = default;

    /** True when the store has a parameter at table index `index`. A control of an unknown index draws a warning placeholder. */
    virtual bool contains (int index) const noexcept = 0;
    /** The current value: plain (the unit of ParamInfo) and normalised (0..1 in the store's own range, the one a host automates). */
    virtual float plain (int index) const = 0;
    virtual float normalised (int index) const = 0;
    /** The store's conversion between plain and normalised for `index` (a host may use a different curve than the core: what counts is that
        setNormalised (index, toNormalised (index, v)) stores v). */
    virtual float toNormalised (int index, float plain) const = 0;
    virtual float fromNormalised (int index, float normalised) const = 0;

    /** Gestures: begin / end around a drag; a single edit outside a gesture is wrapped by setPlainComplete. Nesting is not counted: the caller pairs them. */
    virtual void beginGesture (int index) = 0;
    virtual void endGesture (int index) = 0;
    /** Sets the value (normalised 0..1, clamped by the store) and tells the host. Inside or outside a gesture. */
    virtual void setNormalised (int index, float normalised) = 0;

    void setPlain (int index, float plainValue) { setNormalised (index, toNormalised (index, plainValue)); }
    /** begin + set + end: tap, reset, numeric entry, menu choice. */
    void setPlainComplete (int index, float plainValue);

    /** Listeners: of one parameter, or of every parameter. Registered and removed on the message thread; a listener may remove itself from its callback. */
    void addListener (int index, Listener* listener);
    void removeListener (int index, Listener* listener);
    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    /** Expires when the store is destroyed. A holder that may outlive it (a call-out that JUCE deletes on a later message, after the host has gone) checks it before it
        removes its listener. */
    std::weak_ptr<void> lifetime() const noexcept { return lifetimeToken; }

protected:
    /** For the implementations: call on the message thread. */
    void notifyChanged (int index);
    void notifyGesture (int index, bool starting);

private:
    std::shared_ptr<void> lifetimeToken = std::make_shared<char>();
    std::map<int, juce::ListenerList<Listener>> perParameter;
    juce::ListenerList<Listener> everyParameter;
};

/** Every parameter as a ParamValues (plain values; parameters the store lacks stay at their default). */
ParamValues readAllValues (const ParameterStore& store);
/** Writes every value that differs from the store (one gesture per parameter). */
void writeAllValues (ParameterStore& store, const ParamValues& values);

/** The store for a host without a parameter system of its own: the values live here (message thread), the host is told by callbacks.
    The plain / normalised conversion is the core's (toNormalised / fromNormalised of the ParamInfo). */
class PlainParameterStore final : public ParameterStore
{
public:
    /** All parameters at their table defaults. */
    PlainParameterStore();

    /** The UI changed a value / opened or closed a gesture: forward to the host (CLAP param events, ...). The value is plain. */
    std::function<void (int index, float plain)> onUiChange;
    std::function<void (int index, bool starting)> onUiGesture;

    /** The host changed a value (automation, state load): stores it and tells the listeners, without calling onUiChange. Message thread. */
    void setFromHost (int index, float plain);
    /** All values at once from the host (state load): one notification per value that differs. */
    void setAllFromHost (const ParamValues& values);

    bool contains (int index) const noexcept override { return index >= 0 && static_cast<size_t> (index) < values.size(); }
    float plain (int index) const override;
    float normalised (int index) const override;
    float toNormalised (int index, float plainValue) const override;
    float fromNormalised (int index, float normalisedValue) const override;
    void beginGesture (int index) override;
    void endGesture (int index) override;
    void setNormalised (int index, float normalisedValue) override;

private:
    std::vector<float> values;
};
} // namespace synthyboy::ui
