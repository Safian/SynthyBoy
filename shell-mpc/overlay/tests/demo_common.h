// SynthyBoy - MPC overlay: what the demonstration programs of the elements (filter_ui_demo, mixfx_ui_demo) share: a plugin instance made of the default parameters that
// accepts writes like the real one, a touch helper, and the function that renders a page through the CPU reference renderer into a PNG. Host tools only (built by shell-mpc/accept.sh).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../page_view.h"
#include "../scene_cpu.h"
#include "../../ui/tests/ui_test.h"
#include "synthyboy/params.h"

namespace demo
{
using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

inline int g_images = 0, g_fails = 0;

class Inst : public IInstanceView
{
public:
    Inst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }
    float param (int i) const noexcept override { return v_[i]; }
    uint64_t stateGeneration() const noexcept override { return generation; }
    bool padPressure (bool& on, float& percent) const noexcept override { on = padOn; percent = padPercent; return true; }
    void setPadPressure (bool on, float percent) noexcept override { padOn = on; padPercent = percent; ++padWrites; }
    bool outputLimiter (bool& on) const noexcept override { on = limiterOn; return true; }
    void setOutputLimiter (bool on) noexcept override { limiterOn = on; ++limiterWrites; }
    bool limiterOn = false;   // the default of the plugin (output_stage.h kOutputLimiterDefaultOn: off)
    int limiterWrites = 0;
    bool padOn = false;
    float padPercent = 35.0f;
    int padWrites = 0;
    bool clipFlag = false;
    bool takeClipFlag() noexcept override
    {
        const bool hit = clipFlag;
        clipFlag = false;
        return hit;
    }
    bool live (synthyboy::LiveValues& out) const noexcept override
    {
        out = liveValues;
        return haveLive;
    }
    double sampleRate() const noexcept override { return 48000.0; }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }
    void setQGroup (const int* t, int n) noexcept override
    {
        for (int i = 0; i < 16; ++i)
            q[i] = i < n ? t[i] : -1;
        qN = n;
        ++qCalls;
    }
    void setUiFullOpen (bool open) noexcept override { fullOpen = open; }
    void setMixerSolo (int strip, bool solo) noexcept override
    {
        if (strip >= 0 && strip < 10)
        {
            if (solo) mixerSoloMask_ |= (1u << strip);
            else mixerSoloMask_ &= ~(1u << strip);
        }
    }
    void setMixerMute (int strip, bool mute) noexcept override
    {
        if (strip >= 0 && strip < 10)
        {
            if (mute) mixerMuteMask_ |= (1u << strip);
            else mixerMuteMask_ &= ~(1u << strip);
        }
    }
    bool mixerSolo (int strip) const noexcept override
    {
        return strip >= 0 && strip < 10 && (mixerSoloMask_ & (1u << strip)) != 0;
    }
    bool mixerMute (int strip) const noexcept override
    {
        return strip >= 0 && strip < 10 && (mixerMuteMask_ & (1u << strip)) != 0;
    }
    uint32_t mixerSoloMask_ = 0;
    uint32_t mixerMuteMask_ = 0;
    double bpm() const noexcept override { return 120.0; }
    float get (const std::string& key) const { const int i = idx (key); return i >= 0 ? v_[i] : 0.0f; }
    void setAt (int i, float v) { v_[i] = v; }
    int q[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   // the Q slots the page published last (Q 1 .. Q 16)
    int qN = 0, qCalls = 0, writes = 0, lastWritten = -1;
    uint64_t generation = 0;
    bool fullOpen = false;
    // the Play selectors and the chunk-only tab view state of the fake instance (what the real plugin keeps in playSel / tabViewState)
    int playSelection (int group) const noexcept override { return group >= 0 && group < 3 ? playSel[group] : 1; }
    void setPlaySelection (int group, int instance) noexcept override { if (group >= 0 && group < 3) { playSel[group] = instance; ++selectorWrites; } }
    int tabView (int what) const noexcept override { return what >= 0 && what < kTabViewFields ? tabViewV[what] : 0; }
    void setTabView (int what, int value) noexcept override { if (what >= 0 && what < kTabViewFields) { tabViewV[what] = value; ++selectorWrites; } }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentV; }
    std::shared_ptr<const synthyboy::StateContent> contentV;   // null: no content (the default)
    int playSel[3] = { 1, 1, 1 }, tabViewV[kTabViewFields] = { 0 }, selectorWrites = 0;
    int playLastSelector() const noexcept override { return 2; }
    int lastPlayed (int which) const noexcept override { return which == 1 ? lastKey : (which == 0 ? lastVel : -1); }   // the last note-on (the live marker of the NOTE / VELO editors)
    int lastKey = -1, lastVel = -1;
    void setQVirtual (int id, float v) noexcept override { if (id >= 0 && id < kVirtualSlots) virtualOut[id] = v; }
    bool takeQVirtual (int id, float& v) noexcept override
    {
        if (id < 0 || id >= kVirtualSlots || ! virtualDue[id])
            return false;
        virtualDue[id] = false;
        v = virtualIn[id];
        return true;
    }
    /** The host writes the virtual Q target `id` (the page takes it at its next update). */
    void setVirtualIn (int id, float v) { virtualIn[id] = v; virtualDue[id] = true; }
    static constexpr int kVirtualSlots = 10;   // QRouter::kNumVirtual
    float virtualOut[kVirtualSlots] = {}, virtualIn[kVirtualSlots] = {};
    bool virtualDue[kVirtualSlots] = {};
    const SpectrumRing* spectrumRing() const noexcept override { return ring; }
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { const int i = idx (key); if (i >= 0) v_[i] = v; }
    /** A modulation slot: slot n (1 ..) with its source choice, the table index of the target parameter's key, the amount in percent. */
    void mod (int n, int source, const std::string& destKey, float amount, bool bipolar = false, bool bypass = false)
    {
        char k[24];
        std::snprintf (k, sizeof k, "mod%02d.", n);
        set (std::string (k) + "source", static_cast<float> (source));
        set (std::string (k) + "destination", static_cast<float> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (idx (destKey)))));
        set (std::string (k) + "amount", amount);
        set (std::string (k) + "bipolar", bipolar ? 1.0f : 0.0f);
        set (std::string (k) + "bypass", bypass ? 1.0f : 0.0f);
    }
    // the NOTE / VELO curve editor's writes (IInstanceView::setScalarCurve): the last curve per source and the number of writes
    void setScalarCurve (int which, const std::vector<synthyboy::ContentPoint>& pts) override { if (which >= 0 && which < 2) { scalarCurve[which] = pts; ++scalarWrites; } }
    std::vector<synthyboy::ContentPoint> scalarCurve[2];
    int scalarWrites = 0;
    bool haveLive = false;
    synthyboy::LiveValues liveValues;
    const SpectrumRing* ring = nullptr;

private:
    float v_[synthyboy::kMaxParams] = {};
};

/** Draws a scene with `draw` (the picture of a page: it makes its own ground) and writes it as a PNG; counts the images and the failures. */
template <class F>
void save (const std::string& dir, const char* name, F&& draw, int w = 1280, int h = 800)
{
    auto scene = std::make_unique<gpu::SceneData>();
    gpu::SceneBuilder sb (*scene);
    sb.begin (gpu::kKindPlay, 0, w, h);
    const ui::theme::Look& T = ui::theme::L();
    sb.clear (ui::theme::rgba (T.bg0, 255));
    sb.gradientRect (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h), ui::theme::rgba (T.bg0), ui::theme::rgba (T.bg1));
    draw (sb);
    uit::Image im;
    im.w = w; im.h = h;
    renderScene (*scene, im.px);
    const bool ok = ! sb.overflow && uit::writePng (dir + "/" + name, im, 1);
    std::printf ("%s  %s (%u ops)\n", ok ? "PASS" : "FAIL", name, scene->numOps);
    g_images += ok;
    g_fails += ! ok;
}

/** A page of an element (any Ui with the page interface) driven like the overlay thread does it. */
template <class Ui, class Instance = Inst>
struct Rig
{
    Ui ui;
    Instance inst;
    UiStack stack;
    page::Input in;
    double now = 1000.0;
    explicit Rig (int instance = 1)
    {
        in.instance = instance;
        stack.openFull (Ui::kElementPage);
    }
    void refresh (int times = 1)
    {
        for (int i = 0; i < times; ++i)
        {
            now += 150.0;
            in.live = inst.haveLive ? &inst.liveValues : nullptr;
            ui.update (inst, stack, now, in);
        }
    }
    void tapAt (int x, int y, int holdMs = 40)
    {
        TouchEvent e; e.kind = TouchEvent::Down; e.x = x; e.y = y;
        ui.touch (e, stack, inst, now);
        ui.tick (now + holdMs, stack, inst);
        e.kind = TouchEvent::Up;
        ui.touch (e, stack, inst, now + holdMs);
        refresh();
    }
    void tap (const ui::ControlModel& c, int holdMs = 40) { tapAt (c.rect().x + c.rect().w / 2, c.rect().y + c.rect().h / 2, holdMs); }
    void drag (int x0, int y0, int x1, int y1, bool lift)
    {
        TouchEvent e; e.kind = TouchEvent::Down; e.x = x0; e.y = y0;
        ui.touch (e, stack, inst, now);
        e.kind = TouchEvent::Move; e.x = x1; e.y = y1;
        ui.touch (e, stack, inst, now + 16.0);
        if (lift)
        {
            e.kind = TouchEvent::Up;
            ui.touch (e, stack, inst, now + 32.0);
        }
        refresh();
    }
    void picture (const std::string& dir, const char* name) { save (dir, name, [this] (gpu::SceneBuilder& sb) { ui.draw (sb); }); }
};
} // namespace demo
