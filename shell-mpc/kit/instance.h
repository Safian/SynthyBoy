// MpcKit - what the overlay thread and the overlay pages may ask of ONE plugin instance (IInstance), and the shell-level settings of an instance that are saved in its chunk (IInstanceState,
// SettingsStore).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// IInstance is the generic half of SynthyBoy's IInstanceView (shell-mpc/overlay/curve_source.h): the methods every page needs whatever the plugin is. SynthyBoy's IInstanceView will DERIVE from it
// (migration plan, TEMPLATE.md) and keep only the engine specific methods (live values, content, spectrum, Play selectors, pad pressure, ...), so a page written against IInstance& runs unchanged on
// both. The signatures are exactly the ones of IInstanceView.
//
// THREADS. Every method is called from the OVERLAY thread (the page code) while the instance is registered with the overlay; it must be lock free and must not allocate (only qStatus () and the
// methods that say so may). The audio thread and the host thread keep running at the same time: the plugin's own atomics (the parameter mirror) are what param () reads.
//
// NO DEPENDENCY on the synthesizer engine of SynthyBoy.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "content_source.h"
#include "param_source.h"

namespace mpckit {

/** One shell-level setting of an instance: not a host parameter, but a switch or a number the player sets on a page and the plugin saves in its chunk (the output limiter of SynthyBoy's Master page,
    the pad pressure layer). The key is the name in the chunk (never renamed). */
struct SettingSpec
{
    enum class Kind : uint8_t { toggle, number };
    const char* key = "";
    const char* label = "";
    Kind kind = Kind::toggle;
    float min = 0.0f, max = 1.0f, def = 0.0f;
};

/** The settings of an instance. A setting is addressed by its index in the list `setting (i)`. */
class IInstanceState
{
public:
    virtual ~IInstanceState() = default;
    virtual int numSettings() const noexcept = 0;
    virtual const SettingSpec& setting (int index) const noexcept = 0;
    /** The value now (a toggle 0 / 1). false: this instance has no such setting (the page shows the control disabled). Lock free. */
    virtual bool get (int index, float& value) const noexcept = 0;
    /** The page sets it (the instance applies it and its chunk keeps it). Lock free. */
    virtual void set (int index, float value) noexcept = 0;
};

class IInstance
{
public:
    virtual ~IInstance() = default;

    // ---- the parameters
    /** The parameter table of the plugin (static facts: ranges, names, conversions). The same object for every instance. */
    virtual const IParamSource& params() const noexcept = 0;
    /** The PLAIN value of parameter `index` (IParamSource indices). Out of range: 0. */
    virtual float param (int index) const noexcept = 0;
    /** The page writes a real parameter with a NORMALISED value (0 .. 1): the same write as the host's own (mirror, engine), and the host is told afterwards (the plugin's display thread makes the
        audioMasterAutomate call, never this thread), so automation and the host's own widgets follow. */
    virtual void writeParam (int index, float normalised) noexcept = 0;

    // ---- the time base
    virtual double sampleRate() const noexcept = 0;
    /** The tempo of the host (beats per minute). */
    virtual double bpm() const noexcept { return 120.0; }

    // ---- the sixteen Q proxies (q_targets.h)
    /** The page sets the targets of the sixteen Q proxies "Q 1" .. "Q 16": the first `n` (at most 16) of `targets`, each a parameter index (the view of it in the bits above 16, QRouter::encode) or -1
        (none). Slots 1 .. 4 are the active group of the page, 5 .. 8 / 9 .. 12 / 13 .. 16 the next groups in reading order. A page that knows one group passes n = 4. The plugin forwards it to its QRouter. */
    virtual void setQGroup (const int* targets, int n) noexcept = 0;
    /** Changes when an earlier map is unsafe. Accept input for the new map after a complete view update. */
    virtual uint64_t qBindingRevision() const noexcept { return 0; }
    virtual void finishQBindingUpdate (uint64_t /*revision*/) noexcept {}
    /** The VIRTUAL Q targets: a Q knob that is not a parameter (a value of a display component). The page tells the router the value (0 .. 1) a virtual target has now; the host's write to the Q proxy comes
        back through takeQVirtual (the latest written value; false when there is none since the last call). Plugins without virtual targets keep the defaults. */
    virtual void setQVirtual (int /*id*/, float /*normalised*/) noexcept {}
    virtual bool takeQVirtual (int /*id*/, float& /*normalised*/) noexcept { return false; }
    /** How many times the host wrote one of the Q proxies so far (a hardware knob was turned: the host draws its Q-Link panel a moment later). */
    virtual unsigned long qWriteCount() const noexcept { return 0; }
    /** "key: value" lines about the Q proxies for the status file (QRouter::describe). */
    virtual void qStatus (std::string& /*out*/) const {}

    // ---- diagnostics
    /** Appends "key: value" lines about the instance to the status file of the overlay (SynthyBoy: its voices and held keys). Nothing by default. */
    virtual void diagnostics (std::string& /*out*/) const {}

    // ---- the overlay's own bookkeeping
    /** A overlay full screen page is open for this instance: the plugin hides the host widgets (the display parameter "Overlay Instance" takes the frame the skin hides them at). */
    virtual void setUiFullOpen (bool /*open*/) noexcept {}
    /** Asks the instance to report its display parameter "Overlay Instance" to the host again soon. It must only set a flag (the instance's display thread makes the call). */
    virtual void requestOverlayReport() noexcept {}

    // ---- optional parts
    /** The plugin's LIVE block: a plain struct the plugin's engine writes and its own views read (SynthyBoy: synthyboy::LiveValues, the readout of the voices; a plugin without one keeps false). Copies it into
        `out` (`bytes` = sizeof the struct the caller expects: a different size copies nothing) and returns true when the engine has written one. Lock free. */
    virtual bool readLive (void* /*out*/, size_t /*bytes*/) const noexcept { return false; }
    /** The shell-level settings of this instance (null: none). */
    virtual IInstanceState* state() noexcept { return nullptr; }
    virtual const IInstanceState* state() const noexcept { return nullptr; }
};

/** A ready IInstanceState: a fixed list of settings kept in atomics, with the text form the chunk of a plugin made with the kit uses: "key=value;key=value" (read with load (), written with save ()).
    The list must outlive the store (a constant array of the plugin). Thread safe: relaxed atomics. */
class SettingsStore final : public IInstanceState
{
public:
    static constexpr int kMax = 16;

    SettingsStore (const SettingSpec* list, int count) noexcept : list_ (list), count_ (count < kMax ? count : kMax)
    {
        for (int i = 0; i < kMax; ++i)
            value_[i].store (i < count_ ? list_[i].def : 0.0f, std::memory_order_relaxed);
    }
    int numSettings() const noexcept override { return count_; }
    const SettingSpec& setting (int index) const noexcept override { return list_[index < 0 ? 0 : (index >= count_ ? (count_ > 0 ? count_ - 1 : 0) : index)]; }
    bool get (int index, float& value) const noexcept override
    {
        if (index < 0 || index >= count_)
            return false;
        value = value_[index].load (std::memory_order_relaxed);
        return true;
    }
    void set (int index, float value) noexcept override
    {
        if (index < 0 || index >= count_ || ! (value == value))   // out of range or NaN
            return;
        const SettingSpec& s = list_[index];
        value_[index].store (value < s.min ? s.min : (value > s.max ? s.max : value), std::memory_order_relaxed);
    }
    /** "key=value;" for every setting. */
    std::string save() const
    {
        std::string out;
        char b[96];
        for (int i = 0; i < count_; ++i)
        {
            std::snprintf (b, sizeof b, "%s=%.9g;", list_[i].key, static_cast<double> (value_[i].load (std::memory_order_relaxed)));
            out += b;
        }
        return out;
    }
    /** Reads what save () wrote (unknown keys are ignored, missing ones keep their value). */
    void load (const std::string& text)
    {
        for (int i = 0; i < count_; ++i)
        {
            const std::string key = std::string (list_[i].key) + "=";
            size_t at = text.find (key);
            while (at != std::string::npos && at > 0 && text[at - 1] != ';')
                at = text.find (key, at + 1);
            if (at != std::string::npos)
                set (i, static_cast<float> (std::atof (text.c_str() + at + key.size())));
        }
    }

private:
    const SettingSpec* list_;
    int count_;
    std::atomic<float> value_[kMax];
};

} // namespace mpckit
