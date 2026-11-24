// MpcKit Demo - ONE plugin instance as the kit sees it: implements mpckit::IInstance (what the pages and the overlay thread may ask), keeps the parameter mirror, the sound engine, the Q proxies
// and the bookkeeping of the display parameter "Overlay Instance". No VST types here: demo_plugin.cpp is the VST2 skin around this class.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "../../shell-mpc/kit/kit.h"
#include "demo_params.h"
#include "demo_voice.h"
#include "log_compat.h"

namespace demo {

class DemoInstance;

/** The list of the List page: the presets. Selecting one writes all its parameter values. */
class PresetList final : public mpckit::IContentSource
{
public:
    explicit PresetList (DemoInstance& owner) noexcept : owner_ (owner) {}
    const char* title() const noexcept override { return "PRESETS"; }
    int count() const noexcept override { return 5; }
    const char* name (int index) const noexcept override { return index >= 0 && index < 5 ? kPresetNames[index] : ""; }
    int selected() const noexcept override { return selected_.load (std::memory_order_relaxed); }
    void select (int index) noexcept override;   // below: needs DemoInstance

private:
    DemoInstance& owner_;
    std::atomic<int> selected_ { 0 };
};

class DemoInstance final : public mpckit::IInstance
{
public:
    // The display parameter "Overlay Instance" is the film strip frame / 32: frame 0 blank, 1 .. 16 the slot, 17 none, 24 a plugin-drawn full screen page is open (the skin hides the host
    // widgets of the tab at it). The same numbers as shell-mpc/overlay/instance_slots.h; after migration step 8 (TEMPLATE.md) the runtime provides them.
    static constexpr int kFrames = 32, kFullScreenFrame = 24, kNoneFrame = 17;
    static float frameValue (int frame) noexcept { return static_cast<float> (frame) / static_cast<float> (kFrames); }

    DemoInstance() : presets (*this), q (params())
    {
        for (int i = 0; i < kNumParams; ++i)
            mirror_[i].store (kParams[i].def, std::memory_order_relaxed);
        q.bind (this);
    }

    // ---- mpckit::IInstance (overlay thread)
    const mpckit::IParamSource& params() const noexcept override { return demo::params(); }
    float param (int index) const noexcept override { return index >= 0 && index < kNumParams ? mirror_[index].load (std::memory_order_relaxed) : 0.0f; }
    void writeParam (int index, float normalised) noexcept override
    {
        if (hostWrite (index, normalised))
        {
            dirty_.fetch_or (1u << index, std::memory_order_release);   // the display thread tells the host (automation, the host's own widgets)
            MPCKIT_LOG ("param", DEMO_DEBUG, "page wrote %s", kParams[index].key);
        }
    }
    double sampleRate() const noexcept override { return rate_.load (std::memory_order_relaxed); }
    void setQGroup (const int* targets, int n) noexcept override { q.setGroup (targets, n); }
    void setQVirtual (int id, float v) noexcept override { q.setVirtual (id, v); }
    bool takeQVirtual (int id, float& v) noexcept override { return q.takeVirtual (id, v); }
    unsigned long qWriteCount() const noexcept override { return q.writeCount(); }
    void qStatus (std::string& out) const override { q.describe (out); }
    void setUiFullOpen (bool open) noexcept override
    {
        if (fullOpen_.exchange (open, std::memory_order_relaxed) != open)
            reportDue.store (true, std::memory_order_release);
    }
    void requestOverlayReport() noexcept override { reportDue.store (true, std::memory_order_release); }
    mpckit::IContentSource* contentSource() noexcept override { return &presets; }

    // ---- the host's thread and the audio thread
    /** The host-side write of a parameter (the same as the page's, without the report). true: the value changed. */
    bool hostWrite (int index, float normalised) noexcept
    {
        if (index < 0 || index >= kNumParams)
            return false;
        const mpckit::IParamSource& p = demo::params();
        const float plain = p.constrain (index, p.fromNormalised (index, normalised));
        if (plain == mirror_[index].load (std::memory_order_relaxed))
            return false;
        mirror_[index].store (plain, std::memory_order_relaxed);
        return true;
    }
    float normalised (int index) const noexcept { return demo::params().toNormalised (index, param (index)); }
    void setSampleRate (double rate) noexcept { rate_.store (rate, std::memory_order_relaxed); }
    /** A preset: every parameter gets its value; the host is told. */
    void applyPreset (int index) noexcept
    {
        if (index < 0 || index >= 5)
            return;
        MPCKIT_LOG ("preset", DEMO_DEBUG, "preset %d", index);
        for (int i = 0; i < kNumParams; ++i)
            writeParam (i, demo::params().toNormalised (i, kPresets[index].v[i]));
    }
    /** The audio thread: renders the block with the current values. */
    void process (float* l, float* r, int n) noexcept
    {
        float plain[kNumParams];
        for (int i = 0; i < kNumParams; ++i)
            plain[i] = mirror_[i].load (std::memory_order_relaxed);
        engine.process (l, r, n, rate_.load (std::memory_order_relaxed), snapshotOf (plain));
    }
    /** The display thread: the parameters a page wrote since the last call (bit i = parameter i), cleared. */
    uint32_t takeDirty() noexcept { return dirty_.exchange (0u, std::memory_order_acq_rel); }
    /** What getParameter ("Overlay Instance") answers: the slot's frame (kNoneFrame without a slot), or the full screen frame while a page is open. */
    float overlayValue() const noexcept
    {
        if (fullOpen_.load (std::memory_order_relaxed))
            return frameValue (kFullScreenFrame);
        const int slot = slot_.load (std::memory_order_relaxed);
        return frameValue (slot >= 0 && slot < mpckit::kMaxInstances ? slot + 1 : kNoneFrame);
    }
    void setSlot (int slot) noexcept { slot_.store (slot, std::memory_order_relaxed); }

    PresetList presets;
    mpckit::QProxyBank q;
    Engine engine;
    std::atomic<bool> reportDue { true };   // the display thread reports "Overlay Instance" when this is set

private:
    std::atomic<float> mirror_[kNumParams];
    std::atomic<uint32_t> dirty_ { 0 };
    std::atomic<double> rate_ { 44100.0 };
    std::atomic<bool> fullOpen_ { false };
    std::atomic<int> slot_ { -1 };
};

inline void PresetList::select (int index) noexcept
{
    if (index < 0 || index >= 5)
        return;
    selected_.store (index, std::memory_order_relaxed);
    owner_.applyPreset (index);   // a write of 9 parameters: lock free, the display thread reports them
}

} // namespace demo
