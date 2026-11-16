// SynthyBoy - MPC overlay: the envelope UI of the Play page, the plugin-drawn pages built from the UI library (shell-mpc/ui/env_*.h, mod_targets.h) and wired to the real engine parameters (envN.* of the table).
// One object per (Play view, plugin instance), like OscUi (osc_ui.h). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw*, with the instance as IInstanceView.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementEnv, the pages above it are ours): main = the compact view in the middle third of the Play page (EnvCompact); full = the full screen
// (EnvFullScreen) over the whole plugin area of the Play page; detail pages above it: the targets of the envelope (ModTargetsPage, the modulation matrix seen from the source), the list that picks a new target
// (groups, then the parameters of a group), the modulations of one control (ModulationsPage, a long press on a knob).
// What it does with a touch: the library component gives a Change { id, kind, value }; the id says which control it was, the binding of the id (a key of the engine table and a type) says which parameter and how
// the UI value converts; the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). The graph's handles give an Edit (EnvGraph::edit) that is written the same way. Every update ()
// reads every parameter back, so host automation, presets and the host's own widgets show on the next frame.
// Units: the time knobs, the number fields and the graph follow `envN.units`: milliseconds (envN.attack ..) or beats (envN.attack_beats ..); the sustain and the three curves have one parameter each.
// Live: the running dots come from LiveValues::envVoiceStage / envVoiceFraction (one per sounding voice, the newest first); an engine that fills only envFineStage / envFineFraction gives the one dot.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/env_screen.h"
#include "../ui/list.h"
#include "../ui/mod_targets.h"
#include "../q_router.h"
#include "curve_source.h"
#include "touch_decoder.h"
#include "page_view.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class EnvUi
{
public:
    // ---- the element and its pages (UiStack ids)
    static constexpr int kElementEnv = 2;
    static constexpr int kElementPage = kElementEnv;   // the page of an Env tab is the full screen of the element (page_view.h)
    enum Page { kPageTargets = 30, kPageList = 31, kPageMods = 32 };

    // ---- the id spaces of the controls (a Change comes out with the id the owner gave)
    enum Id : int
    {
        kClose = 1, kFullButton, kInstances, kGraph, kTargets,
        kKnob = 100,                        // 100 + Knob: the knobs (the compact view and the full screen share the ids)
        kBeats = 130, kLegato,
        kTargetsBack = 150, kTargetsPage, kTargetsAdd, kTargetsRow = 160,    // 160 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kModsBack = 600, kModsRow = 610, kModsAddStep = 660, kModsAdd,
        kList = 700, kListBackdrop, kListHeader
    };
    enum KnobId : int { kAttack = 0, kDecay, kSustain, kRelease, kACurve, kDCurve, kRCurve, kDelay, kHold, kNumKnobs };

    /** What a value is and how it converts to the engine's plain value. */
    enum class T : uint8_t { Knob, Field, Toggle, Select, Graph, Inert };

    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        int param = -1;        // the table index (milliseconds / the only one)
        int paramBeats = -1;   // the table index in beats (-1: the value has no beats version)
        int slot = -1;         // Field: 0 .. 5, Knob: kAttack ..
        int active (bool beats) const noexcept { return beats && paramBeats >= 0 ? paramBeats : param; }
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kCompactX = 440, kCompactW = 400;       // the middle third of the Play page
    static constexpr double kModsRefreshMs = 100.0;

    EnvUi() { setCanvas (kCanvasW, kCanvasH); configure (1); }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        compact_.place ({ kCompactX, kTabY + 4, kCompactW, kTabH - 8 });
        full_.place (tab);
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
        list_.setHeaderId (kListHeader);
        targets_.place (tab);
        mods_.place (tab);
    }

    /** Reads the instance: the selected envelope, every value of the visible controls, the modulations of the knobs, the pages of the stack, the dots of the voices, the Q group. */
    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs, const synthyboy::LiveValues* live)
    {
        highlight_ = highlight;
        const int sel = page_ ? pageSel_ : std::clamp (inst.playSelection (1), 1, kEnvs);   // the page of a tab keeps its own envelope (the tab names the first one); the Play page follows the host's selector
        if (sel != sel_ || ! configured_)
            configure (sel);
        selectedSource_ = uibind::envSource (sel_);
        const int unitsIdx = uibind::indexOfKey (prefix_ + "units");
        beats_ = unitsIdx >= 0 && inst.param (unitsIdx) >= 0.5f;
        const bool open = stack.depth() > 0 && stack.element() == kElementEnv;
        if (open && stackDepth_ == 0)
            full_.setActiveGroup (0);
        stackDepth_ = open ? stack.depth() : 0;
        otherOpen_ = stack.depth() > 0 && ! open;
        syncPages (open ? stack : idle_, inst);
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        pull (inst, items_);
        pullGraph (inst, live);
        if (open)
            inst.setUiFullOpen (true);
        else if (stack.isMain())
            inst.setUiFullOpen (false);
        publishQGroup (inst);
    }

    /** The page of an Env tab (page_view.h): the full screen of the envelope the tab names, nothing to close, the selector chooses another envelope for this page only. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        if (! page_)
        {
            page_ = true;
            pageSel_ = std::clamp (in.instance, 1, kEnvs);
            full_.setPageMode (true);
        }
        update (inst, 2, stack, nowMs, in.live);
    }
    /** The whole picture of the page: its ground, the full screen or the page above it, the running dots, the popup. */
    void draw (gpu::SceneBuilder& sb) const
    {
        drawPage (sb);
        drawLive (sb);
        drawPopup (sb);
    }

    bool isMain() const noexcept { return stackDepth_ == 0; }
    int selection() const noexcept { return sel_; }
    bool beats() const noexcept { return beats_; }
    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return full_.activeGroup(); }
    const int* qTargets() const noexcept { return qTargets_; }
    const char* qGroupName() const noexcept { return qName_; }

    // ---- the drawing (a picture is one scene of cached cells, then the live parts, then the popups)
    /** The opaque ground and the compact view (main state). */
    void drawCompact (gpu::SceneBuilder& sb) const
    {
        const ui::theme::Look& L = ui::theme::L();
        {
            ui::Cell cell (sb, { kGroundX0, kTabY, kGroundX1 - kGroundX0, kTabH }, 0);
            sb.gradientRect (static_cast<float> (kGroundX0), static_cast<float> (kTabY), static_cast<float> (kGroundX1), static_cast<float> (kTabY + kTabH), ui::theme::rgba (L.bg0), ui::theme::rgba (L.bg1));
        }
        compact_.draw (sb);
    }
    /** The opaque ground of the whole tab and the page of the stack (the full screen, the targets, the modulations of a control). */
    void drawPage (gpu::SceneBuilder& sb) const
    {
        const ui::theme::Look& L = ui::theme::L();
        {
            ui::Cell cell (sb, { 0, kTabY, w_, kTabH }, 0);
            sb.gradientRect (0.0f, static_cast<float> (kTabY), static_cast<float> (w_), static_cast<float> (kTabY + kTabH), ui::theme::rgba (L.bg0), ui::theme::rgba (L.bg1));
        }
        if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            targets_.draw (sb);
        else if (pageTop_ == kPageMods)
            mods_.draw (sb);
        else
            full_.draw (sb);
    }
    void drawPopup (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ > 0 && pageTop_ == kPageList)
            list_.draw (sb);
    }
    /** The running dots (the graph that is on screen). */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
            compact_.drawLive (sb);
        else if (pageTop_ == 0)
            full_.drawLive (sb);
    }
    /** What the current state draws, as one number (the picture changes when it does): a scene of everything, the live dots included. */
    uint32_t drawHash (gpu::SceneData& scratch) const
    {
        gpu::SceneBuilder sb (scratch);
        sb.begin (gpu::kKindPlay, 0, w_, h_);
        if (stackDepth_ == 0)
            drawCompact (sb);
        else
            drawPage (sb);
        drawLive (sb);
        drawPopup (sb);
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < scratch.numOps; ++i)
        {
            const gpu::SceneOp& o = scratch.ops[i];
            h = mixIn (h, static_cast<int32_t> (o.op));
            h = mixIn (h, static_cast<int32_t> (o.rgba));
            h = mixIn (h, static_cast<int32_t> (o.rgba2));
            for (float f : o.f)
            {
                int32_t bits;
                std::memcpy (&bits, &f, 4);
                h = mixIn (h, bits);
            }
        }
        return h ^ static_cast<uint32_t> (stackDepth_ * 7 + pageTop_);
    }
    /** The touch hit this UI's drawing (the main state: the middle third; any other state: the whole tab). */
    bool covers (int x, int y) const noexcept
    {
        return stackDepth_ > 0 ? ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y) : ui::Rect { kCompactX, kTabY + 4, kCompactW, kTabH - 8 }.contains (x, y);
    }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** One touch event in UI pixels (the whole screen), `stack` is the instance's stack (the caller holds its lock). true: the touch belongs to the plugin-drawn pages. */
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        ui::Component* top = topComponent();
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            if (! covers (x, y) || ! top)
                return false;
            gesture_ = top;
            gestureDown_ = true;
            handle (top->touchDown (x, y, nowMs), stack, inst);
            publishQGroup (inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (x, y), stack, inst);
        else
        {
            handle (gesture_->touchUp (x, y), stack, inst);
            gestureDown_ = false;
            gesture_ = nullptr;
        }
        return true;
    }
    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
    }
    bool fingerDown() const noexcept { return gestureDown_; }
    /** Another element's full screen opened or closed: the Q group is published again at the next update (the router keeps what the last page set). */
    void forgetQGroup() noexcept { qSet_ = false; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }

    // ---- access for the tests
    ui::EnvCompact& compact() noexcept { return compact_; }
    ui::EnvFullScreen& full() noexcept { return full_; }
    ui::ModTargetsPage& targetsPage() noexcept { return targets_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    ui::ListPopup& list() noexcept { return list_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    const Item* item (int id) const noexcept { for (const Item& it : items_) if (it.id == id) return &it; return nullptr; }
    int timeParam (int stage, bool beats) const noexcept { return stage >= 0 && stage < 5 ? (beats && timeBeats_[stage] >= 0 ? timeBeats_[stage] : timeMs_[stage]) : -1; }
    int sustainParam() const noexcept { return sustainP_; }
    int curveParam (int i) const noexcept { return i >= 0 && i < 3 ? curveP_[i] : -1; }
    /** The envelope that has a table index the transition of which the UI shows now (the number of the target rows etc.). */
    int numTargets() const noexcept { return numTargets_; }
    uint8_t targetMask() const noexcept { return full_.instances.targetMask(); }
    static constexpr int kEnvs = 6;

private:
    using ModRow = uibind::ModRow;
    static constexpr int kGroundX0 = 434, kGroundX1 = 848;
    static uint32_t mixIn (uint32_t h, int32_t v) noexcept
    {
        for (int b = 0; b < 4; ++b)
            h = (h ^ ((static_cast<uint32_t> (v) >> (8 * b)) & 255u)) * 16777619u;
        return h;
    }

    // ======================================================================================================================================================================
    // building: the selected envelope decides the parameters behind the ids
    void bind (int id, ui::Component* c, T type, const std::string& suffix, const std::string& suffixBeats = std::string(), int slot = -1)
    {
        Item it;
        it.id = id; it.c = c; it.type = type; it.slot = slot;
        it.param = suffix.empty() ? -1 : uibind::indexOfKey (prefix_ + suffix);
        it.paramBeats = suffixBeats.empty() ? -1 : uibind::indexOfKey (prefix_ + suffixBeats);
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }
    void configure (int sel)
    {
        sel_ = sel;
        configured_ = true;
        prefix_ = "env" + std::to_string (sel) + ".";
        items_.clear();
        static const char* const kTime[5] = { "delay", "attack", "hold", "decay", "release" };
        for (int i = 0; i < 5; ++i)
        {
            timeMs_[i] = uibind::indexOfKey (prefix_ + kTime[i]);
            timeBeats_[i] = uibind::indexOfKey (prefix_ + kTime[i] + "_beats");
        }
        sustainP_ = uibind::indexOfKey (prefix_ + "sustain");
        static const char* const kCurve[3] = { "attack_curve", "decay_curve", "release_curve" };
        for (int i = 0; i < 3; ++i)
            curveP_[i] = uibind::indexOfKey (prefix_ + kCurve[i]);
        bind (kClose, &full_.close, T::Inert, "");
        bind (kFullButton, &compact_.full, T::Inert, "");
        bind (kTargets, &full_.targets, T::Inert, "");
        bind (kInstances, &compact_.instances, T::Select, "");
        bind (kInstances, &full_.instances, T::Select, "");
        bind (kGraph, &compact_.graph, T::Graph, "");
        bind (kGraph, &full_.graph, T::Graph, "");
        struct K { int knob; const char* key; const char* beats; };
        const K ks[kNumKnobs] = { { kAttack, "attack", "attack_beats" }, { kDecay, "decay", "decay_beats" }, { kSustain, "sustain", "" }, { kRelease, "release", "release_beats" },
                                  { kACurve, "attack_curve", "" }, { kDCurve, "decay_curve", "" }, { kRCurve, "release_curve", "" }, { kDelay, "delay", "delay_beats" }, { kHold, "hold", "hold_beats" } };
        ui::Knob* fk[kNumKnobs] = { &full_.attack, &full_.decay, &full_.sustain, &full_.release, &full_.aCurve, &full_.dCurve, &full_.rCurve, &full_.delay, &full_.hold };
        ui::Knob* ck[kNumKnobs] = { &compact_.attack, &compact_.decay, &compact_.sustain, &compact_.release, nullptr, nullptr, nullptr, nullptr, nullptr };
        for (const K& k : ks)
        {
            bind (kKnob + k.knob, fk[k.knob], T::Knob, k.key, k.beats, k.knob);
            if (ck[k.knob] != nullptr)
                bind (kKnob + k.knob, ck[k.knob], T::Knob, k.key, k.beats, k.knob);
        }
        bind (kBeats, &full_.beats, T::Toggle, "units");
        bind (kLegato, &full_.legato, T::Toggle, "legato_inverted");
        pageTop_ = 0;
        underList_ = 0;
        modsDirty_ = true;
        std::memset (qTargets_, 0xff, sizeof qTargets_);
        qSet_ = false;
        gesture_ = nullptr; gestureDown_ = false;
        compact_.group.setActive (highlight_ == 2);
        full_.setActiveGroup (0);
    }

    // ======================================================================================================================================================================
    // reading: the states of the components from the parameters
    void pull (IInstanceView& v, std::vector<Item>& list)
    {
        for (Item& it : list)
        {
            if (it.c == nullptr)
                continue;
            if (it.type == T::Select)
            {
                static_cast<ui::PictogramSelector*> (it.c)->state.index = sel_ - 1;
                continue;
            }
            const int p = it.active (beats_);
            if (it.type == T::Inert || it.type == T::Graph)
            {
                if (it.id == kTargets)
                {
                    char t[8];
                    std::snprintf (t, sizeof t, "%d", numTargets_);
                    full_.targets.setValue (t);
                }
                continue;
            }
            if (p < 0)
            {
                it.c->setEnabled (false);
                continue;
            }
            it.c->setEnabled (true);
            const float plain = v.param (p);
            const synthyboy::ParamInfo& pi = uibind::info (p);
            switch (it.type)
            {
                case T::Knob:
                {
                    auto* k = static_cast<ui::Knob*> (it.c);
                    k->state.value = k->state.modValue = uibind::normOf (p, plain);
                    k->state.defaultValue = uibind::normOf (p, pi.def);
                    k->state.bipolar = pi.min < 0.0f && pi.max > 0.0f;
                    valueText (pi, plain, k->state.valueText, sizeof k->state.valueText);
                    break;
                }
                case T::Field:
                {
                    auto* f = static_cast<ui::NumberField*> (it.c);
                    f->state.value = plain;
                    f->state.min = pi.min; f->state.max = pi.max; f->state.defaultValue = pi.def;
                    const bool isBeats = pi.unit == synthyboy::Unit::beats;
                    f->state.step = isBeats ? 0.01 : 1.0;
                    f->state.pxPerStep = pi.unit == synthyboy::Unit::percent ? 2 : 1;
                    f->state.format = isBeats ? ui::NumberField::Format::Decimal2 : ui::NumberField::Format::IntPlain;
                    static const char* const kName[6] = { "DELAY", "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE" };
                    char lab[24];
                    std::snprintf (lab, sizeof lab, "%s %s", kName[it.slot], pi.unit == synthyboy::Unit::percent ? "%" : (isBeats ? "beat" : "ms"));
                    f->setLabel (lab);
                    break;
                }
                case T::Toggle:
                    static_cast<ui::ToggleIcon*> (it.c)->state.on = plain >= 0.5f;
                    break;
                default: break;
            }
        }
    }
    /** The text under a knob: the value with its unit. */
    static void valueText (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
    {
        if (pi.unit == synthyboy::Unit::none)
            std::snprintf (out, cap, "%+.1f", static_cast<double> (plain));
        else
            uibind::formatValue (pi, plain, out, cap);
    }
    /** The shape of envelope `n` (1 .. 6) from the parameters: normalised times, the sustain level, the curves; false when the table has none of it. */
    bool shapeOf (const IInstanceView& v, int n, bool beats, ui::envgraph::Shape& s, ui::envgraph::Shape* def) const
    {
        const std::string pre = "env" + std::to_string (n) + ".";
        static const char* const kTime[5] = { "delay", "attack", "hold", "decay", "release" };
        const int units = uibind::indexOfKey (pre + "units");
        const bool b = units >= 0 ? v.param (units) >= 0.5f : beats;
        for (int i = 0; i < 5; ++i)
        {
            const int p = uibind::indexOfKey (pre + kTime[i] + (b ? "_beats" : ""));
            if (p < 0)
                return false;
            s.t[i] = uibind::normOf (p, v.param (p));
            if (def != nullptr)
                def->t[i] = uibind::normOf (p, uibind::info (p).def);
        }
        const int sp = uibind::indexOfKey (pre + "sustain");
        if (sp < 0)
            return false;
        s.sustain = std::clamp (static_cast<double> (v.param (sp)) / 100.0, 0.0, 1.0);
        if (def != nullptr)
            def->sustain = static_cast<double> (uibind::info (sp).def) / 100.0;
        static const char* const kCurve[3] = { "attack_curve", "decay_curve", "release_curve" };
        for (int i = 0; i < 3; ++i)
        {
            const int p = uibind::indexOfKey (pre + kCurve[i]);
            if (p < 0)
                return false;
            s.curve[i] = static_cast<double> (v.param (p));
            if (def != nullptr)
                def->curve[i] = static_cast<double> (uibind::info (p).def);
        }
        return true;
    }
    void pullGraph (IInstanceView& v, const synthyboy::LiveValues* live)
    {
        for (ui::EnvGraph* g : { &compact_.graph, &full_.graph })
        {
            ui::EnvGraph::State& st = g->state;
            ui::envgraph::Shape s, d;
            if (! shapeOf (v, sel_, beats_, s, &d))
                continue;
            st.shape = s;
            st.defaults = d;
            st.colour = ui::envColour (sel_ - 1);
            st.beats = beats_;
            st.ghost = false;
            if (sel_ != 1)
            {
                ui::envgraph::Shape gs;
                if (shapeOf (v, 1, false, gs, nullptr))
                {
                    st.ghost = true;
                    st.ghostShape = gs;
                }
            }
            // the value of every handle as text (with its unit)
            for (int i = 0; i < 5; ++i)
            {
                const int p = timeParam (i, beats_);
                if (p >= 0)
                    uibind::formatValue (uibind::info (p), v.param (p), st.text[i], sizeof st.text[i]);
            }
            if (sustainP_ >= 0 && timeParam (3, beats_) >= 0)
            {
                char t[16], u[24];
                uibind::formatValue (uibind::info (timeParam (3, beats_)), v.param (timeParam (3, beats_)), t, sizeof t);
                std::snprintf (u, sizeof u, "%s %.0f%%", t, static_cast<double> (v.param (sustainP_)));
                std::snprintf (st.text[ui::envgraph::Decay], sizeof st.text[ui::envgraph::Decay], "%.15s", u);
                std::snprintf (st.decayTime, sizeof st.decayTime, "%.15s", t);   // under the axis: the time under DECAY, the level under SUSTAIN
                std::snprintf (st.sustainText, sizeof st.sustainText, "%.0f%%", static_cast<double> (v.param (sustainP_)));
            }
            for (int i = 0; i < 3; ++i)
                if (curveP_[i] >= 0)
                    std::snprintf (st.text[ui::envgraph::CurveA + i], sizeof st.text[0], "%+.1f", static_cast<double> (v.param (curveP_[i])));
            // the dots: one per sounding voice (newest first); an engine without them gives the one the fine read-out has
            st.numDots = 0;
            if (live != nullptr)
            {
                const int e = sel_ - 1;
                for (int i = 0; i < synthyboy::LiveValues::kReadVoices && st.numDots < ui::EnvGraph::kMaxDots; ++i)
                {
                    const int stage = static_cast<int> (live->envVoiceStage[e][i] + 0.5f);
                    if (stage >= 1 && stage <= 6)
                        st.dots[st.numDots++] = { stage, quantise (live->envVoiceFraction[e][i]) };
                }
                if (st.numDots == 0)
                {
                    const int stage = static_cast<int> (live->envFineStage[e] + 0.5f);
                    if (stage >= 1 && stage <= 6)
                        st.dots[st.numDots++] = { stage, quantise (live->envFineFraction[e]) };
                }
            }
        }
    }
    static float quantise (float f) noexcept { return std::round (std::clamp (f, 0.0f, 1.0f) * 256.0f) / 256.0f; }

    // ---- the modulations of the controls (the arcs of the knobs, the chips of the fields) and of the envelope itself
    void refreshMods (IInstanceView& v)
    {
        for (Item& it : items_)
        {
            const int p = it.active (beats_);
            if (it.c == nullptr || p < 0 || (it.type != T::Knob && it.type != T::Field))
                continue;
            const ui::ModInput mi = uibind::modsOf (v, p, selectedSource_, nullptr);
            ui::ModInput& dst = it.type == T::Knob ? static_cast<ui::ModInput&> (static_cast<ui::Knob*> (it.c)->state) : static_cast<ui::ModInput&> (static_cast<ui::NumberField*> (it.c)->state);
            dst = mi;
        }
        // what the envelope drives: the slots of the matrix with this source
        int slots[uibind::kMaxTargetSlots];
        numTargets_ = uibind::slotsOfSource (v, selectedSource_, slots, uibind::kMaxTargetSlots);
        for (int i = 0; i < 3; ++i)
            full_.targetLines[i].text[0] = '\0';
        full_.setNumTargets (numTargets_);
        full_.numTargetLines = std::min (3, numTargets_);
        for (int i = 0; i < full_.numTargetLines; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[i]);
            char name[40];
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), name, sizeof name);
            std::snprintf (full_.targetLines[i].text, sizeof full_.targetLines[i].text, "%.28s  %+.0f%%", name, static_cast<double> (v.param (sp.amount)));
        }
        uint8_t targetMask = 0;
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0)
                continue;
            if (std::lround (v.param (sp.destination)) > 0)
            {
                const int src = static_cast<int> (std::lround (v.param (sp.source)));
                if (src >= 1 && src <= kEnvs)
                    targetMask |= static_cast<uint8_t> (1u << (src - 1));
            }
        }
        compact_.instances.setTargetMask (targetMask);
        full_.instances.setTargetMask (targetMask);
        if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            loadTargets (v, false);
        else if (pageTop_ == kPageMods && modsTargetParam_ >= 0)
        {
            const ui::ModInput mi = uibind::modsOf (v, modsTargetParam_, selectedSource_, modRows_);
            if (mi.numMods != mods_.numRows())
            {
                loadMods (v);
                return;
            }
            for (int i = 0; i < mi.numMods; ++i)
            {
                ui::ModulationsPage::Row& r = mods_.rows[i];
                r.depth.state.value = r.depth.state.modValue = (mi.mods[i].depth + 1.0) * 0.5;
                ui::ModulationsPage::setRowState (r, mi.mods[i].bidirectional, mi.mods[i].bypassed);
            }
        }
    }
    static void targetName (size_t destChoice, char* out, size_t cap)
    {
        const int p = synthyboy::modDestinationParamIndex (destChoice);
        std::snprintf (out, cap, "%s", p >= 0 ? uibind::info (p).name : "-");
    }

    // ======================================================================================================================================================================
    // the pages above the full screen
    void syncPages (const UiStack& stack, IInstanceView& inst)
    {
        const int top = stack.depth() >= 2 ? stack.top() : 0;
        const int sig = top * 16 + (top == kPageMods ? modsTarget_ & 0xff : 0);
        if (top == pageTop_ && sig == pageSig_)
            return;
        if (gesture_ != nullptr && pageTop_ != top)
            dropGesture();
        if (top == kPageList && pageTop_ != kPageList)
            underList_ = pageTop_;
        pageTop_ = top;
        pageSig_ = sig;
        modsDirty_ = true;
        qSet_ = false;
        switch (top)
        {
            case kPageTargets: targetsPageIndex_ = 0; loadTargets (inst, true); break;
            case kPageMods: loadMods (inst); break;
            case kPageList: openGroupList(); break;
            default: full_.setActiveGroup (0); break;
        }
    }

    // ---- the targets page: the rows of the matrix whose source is this envelope
    ModRow targetRows_[ui::ModTargetsPage::kPerPage];
    int targetTotal_ = 0;
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, selectedSource_, slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + ui::ModTargetsPage::kPerPage - 1) / ui::ModTargetsPage::kPerPage);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * ui::ModTargetsPage::kPerPage, n = std::clamp (total - first, 0, ui::ModTargetsPage::kPerPage);
        ui::ModTargetsPage::Entry e[ui::ModTargetsPage::kPerPage];
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), e[i].name, sizeof e[i].name);
            e[i].depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            e[i].bidirectional = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            e[i].bypassed = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            targetRows_[i] = { slots[first + i], selectedSource_ };
        }
        // only a change of the rows is loaded again (a reload resets the touch of a row that is being dragged)
        bool same = ! fresh && n == targets_.numRows() && total == targetTotal_;
        for (int i = 0; same && i < n; ++i)
        {
            const ui::ModTargetsPage::Row& r = targets_.rows[i];
            same = std::strcmp (r.name, e[i].name) == 0 && std::fabs (r.depth.state.value - (e[i].depth + 1.0) * 0.5) < 1.0e-9 && r.polarity.state.on == e[i].bidirectional && r.bypass.state.on == ! e[i].bypassed;
        }
        if (same)
            return;
        targetTotal_ = total;
        char title[16];
        std::snprintf (title, sizeof title, "ENV %d", sel_);
        targets_.load (e, n, title, ui::theme::sourceColour ("ENV"), total, targetsPageIndex_);   // the colour of the source on the depth arcs of the knobs
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }

    // ---- the list that picks a new target: the groups of the destinations, then the parameters of a group
    using DestIndex = uibind::DestIndex;
    std::vector<const char*> listNames_;
    int listGroup_ = -1;   // -1: the list shows the groups; else the group whose parameters it shows
    void openGroupList()
    {
        listGroup_ = -1;
        fillList();
    }
    void fillList()
    {
        const DestIndex& d = DestIndex::get();
        listNames_.clear();
        if (listGroup_ < 0)
        {
            for (const std::string& g : d.groups)
                listNames_.push_back (g.c_str());
            list_.state.header = nullptr;
            list_.state.headerBack = false;
        }
        else
        {
            for (int c : d.choices[static_cast<size_t> (listGroup_)])
                listNames_.push_back (uibind::info (synthyboy::modDestinationParamIndex (static_cast<size_t> (c))).name);
            list_.state.header = d.groups[static_cast<size_t> (listGroup_)].c_str();
            list_.state.headerBack = true;
        }
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.state.current = -1;
        list_.showCurrent();
    }

    // ---- the modulations page of one control (a long press on a knob or a field)
    ModRow modRows_[ui::ModulationsPage::kMaxRows];
    int modsTarget_ = 0, modsTargetParam_ = -1, modsAddOffer_ = 0;
    void loadMods (const IInstanceView& v)
    {
        const Item* t = item (modsTarget_);
        ui::ModInput mi;
        char title[13] = "MODULATIONS";
        modsTargetParam_ = t != nullptr ? t->active (beats_) : -1;
        if (t != nullptr && modsTargetParam_ >= 0)
        {
            if (t->c != nullptr && t->type == T::Knob)
                std::snprintf (title, sizeof title, "%.12s", static_cast<ui::Knob*> (t->c)->state.label);
            else if (t->c != nullptr && t->type == T::Field)
                std::snprintf (title, sizeof title, "%.12s", static_cast<ui::NumberField*> (t->c)->state.label);
            mi = uibind::modsOf (v, modsTargetParam_, selectedSource_, modRows_);
        }
        else
            for (ModRow& r : modRows_) r = {};
        mods_.load (mi, title);
        mods_.back.setId (kModsBack);
        mods_.addSource.setId (kModsAddStep);
        mods_.add.setId (kModsAdd);
        for (int r = 0; r < ui::ModulationsPage::kMaxRows; ++r)
            mods_.setRowIds (r, kModsRow + 4 * r, kModsRow + 4 * r + 1, kModsRow + 4 * r + 2, kModsRow + 4 * r + 3);
        // what "add source" offers: this envelope first, then the other envelopes' first and the LFOs
        int offer[8], n = 0;
        auto push = [&] (int s) { if (s == ui::kNoSource || n >= 8) return; for (int i = 0; i < n; ++i) if (offer[i] == s) return; offer[n++] = s; };
        push (selectedSource_);
        for (int s : { 7, 8, 1, 2, 9, 3, 19, 20 }) push (s);
        for (int i = 0; i < n; ++i)
            mods_.sourceIds[i] = offer[i];
        mods_.addSource.state.count = n;
        mods_.addSource.state.index = std::clamp (mods_.addSource.state.index, 0, std::max (0, n - 1));
        mods_.addSource.setItem (uibind::sourceName (offer[std::clamp (mods_.addSource.state.index, 0, std::max (0, n - 1))]));
        modsAddOffer_ = n;
        char tv[16] = "";
        if (modsTargetParam_ >= 0)
            uibind::formatValue (uibind::info (modsTargetParam_), v.param (modsTargetParam_), tv, sizeof tv);
        mods_.setTargetValue (tv);
    }

    // ======================================================================================================================================================================
    // writing: a Change of a component to a parameter
    ui::Component* topComponent() noexcept
    {
        if (stackDepth_ == 0)
            return &compact_;
        if (pageTop_ == kPageTargets)
            return &targets_;
        if (pageTop_ == kPageMods)
            return &mods_;
        if (pageTop_ == kPageList)
            return &list_;
        return &full_;
    }
    Item* find (int id) noexcept
    {
        for (Item& it : items_)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        if (c.kind == K::Request)
        {
            switch (id)
            {
                case kFullButton: stack.openFull (kElementEnv); return;
                case kClose: case kTargetsBack: case kModsBack: case kListBackdrop: stack.back(); return;
                case kTargets: stack.openDetail (kPageTargets); return;
                case kTargetsAdd: stack.openDetail (kPageList); return;
                case kListHeader: listGroup_ = -1; fillList(); return;
                default: break;
            }
            if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage && (id - kTargetsRow) % 4 == 3)
            {
                const int slot = targetRows_[(id - kTargetsRow) / 4].slot;
                if (slot >= 0)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    modsDirty_ = true;
                    loadTargets (v, true);
                }
                return;
            }
            if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kMaxRows && (id - kModsRow) % 4 == 3)
            {
                const int slot = modRows_[(id - kModsRow) / 4].slot;
                if (slot >= 0)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    loadMods (v);
                }
            }
            return;
        }
        if (c.kind == K::ModsRequest)
        {
            modsTarget_ = id;
            stack.openDetail (kPageMods);
            return;
        }
        if (c.kind == K::AddRequest)
        {
            addModulation (v, c.index);
            return;
        }
        if (id == kList && c.kind == K::Index)
        {
            if (listGroup_ < 0)
            {
                listGroup_ = c.index;
                fillList();
            }
            else
            {
                const std::vector<int>& ch = DestIndex::get().choices[static_cast<size_t> (listGroup_)];
                if (c.index >= 0 && c.index < static_cast<int> (ch.size()))
                    addTarget (v, ch[static_cast<size_t> (c.index)]);
                stack.back();
            }
            return;
        }
        if (id == kInstances && c.kind == K::Index)
        {
            if (page_)
                pageSel_ = c.index + 1;   // the page only: the host's Play selector belongs to the Play page
            else
                v.setPlaySelection (1, c.index + 1);
            return;
        }
        if (id == kTargetsPage && c.kind == K::Index)
        {
            targetsPageIndex_ = c.index;
            loadTargets (v, true);
            return;
        }
        if (id == kModsAddStep && c.kind == K::Index)
        {
            const int i = std::clamp (c.index, 0, std::max (0, modsAddOffer_ - 1));
            mods_.addSource.state.index = i;
            mods_.addSource.setItem (uibind::sourceName (mods_.sourceIds[i]));
            return;
        }
        // ---- the rows of the targets page
        if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage)
        {
            const int slot = targetRows_[(id - kTargetsRow) / 4].slot;
            if (slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            switch ((id - kTargetsRow) % 4)
            {
                case 0: if (c.kind == K::Value) uibind::writeNorm (v, sp.amount, c.value); break;
                case 1: if (c.kind == K::Index) uibind::writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0); break;
                case 2: if (c.kind == K::Index) uibind::writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0); break;   // the switch says ON (active): bypassed is its opposite
                default: break;
            }
            modsDirty_ = true;
            return;
        }
        // ---- the rows of the modulations page of a control
        if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kMaxRows)
        {
            const int slot = modRows_[(id - kModsRow) / 4].slot;
            if (slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            switch ((id - kModsRow) % 4)
            {
                case 0: if (c.kind == K::Value) uibind::writeNorm (v, sp.amount, c.value); break;
                case 1: if (c.kind == K::Index) uibind::writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0); break;
                case 2: if (c.kind == K::Index) uibind::writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0); break;
                default: break;
            }
            modsDirty_ = true;
            return;
        }
        // ---- the graph: the handle that was moved
        if (id == kGraph)
        {
            applyEdit ((stackDepth_ == 0 ? compact_.graph : full_.graph).edit(), v);
            return;
        }
        Item* it = find (id);
        if (it == nullptr || it->c == nullptr)
            return;
        const int p = it->active (beats_);
        if (p < 0)
            return;
        if (c.kind == K::Depth)
        {
            const int slot = uibind::slotOf (v, p, c.index);
            if (slot >= 0)
                uibind::writeNorm (v, synthyboy::modSlotParams (slot).amount, (c.value + 1.0) * 0.5);
            modsDirty_ = true;
            return;
        }
        switch (it->type)
        {
            case T::Knob: if (c.kind == K::Value) uibind::writeNorm (v, p, c.value); break;
            case T::Field: if (c.kind == K::Index || c.kind == K::Value) uibind::writePlain (v, p, c.value); break;
            case T::Toggle: if (c.kind == K::Index) uibind::writePlain (v, p, c.index != 0 ? 1.0 : 0.0); break;
            default: break;
        }
    }

    /** A handle of the graph was moved: the parameters it stands for. */
    void applyEdit (const ui::EnvGraph::Edit& e, IInstanceView& v)
    {
        using namespace ui::envgraph;
        if (e.handle >= Delay && e.handle <= Release)
        {
            const int p = timeParam (e.handle == Release ? 4 : e.handle, beats_);
            if (p >= 0)
                uibind::writeNorm (v, p, e.t);
            if (e.handle == Decay && sustainP_ >= 0)
                uibind::writeNorm (v, sustainP_, e.level);
        }
        else if (e.handle >= CurveA && e.handle <= CurveR)
        {
            const int p = curveP_[e.handle - CurveA];
            if (p >= 0)
                uibind::writePlain (v, p, e.curve);
        }
    }
    void addTarget (IInstanceView& v, int destChoice)
    {
        if (destChoice <= 0 || uibind::slotOfPair (v, selectedSource_, destChoice) >= 0)
            return;   // that target is on the list already
        const int slot = uibind::freeSlot (v);
        if (slot < 0)
            return;
        const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (destChoice));
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        uibind::writePlain (v, sp.destination, destChoice);
        uibind::writePlain (v, sp.amount, 50.0);
        if (sp.bypass >= 0) uibind::writePlain (v, sp.bypass, 0.0);
        if (sp.bipolar >= 0) uibind::writePlain (v, sp.bipolar, p >= 0 && uibind::info (p).min < 0.0f && uibind::info (p).max > 0.0f ? 1.0 : 0.0);
        uibind::writePlain (v, sp.source, selectedSource_);
        modsDirty_ = true;
        const int total = uibind::slotsOfSource (v, selectedSource_, scratchSlots_, uibind::kMaxTargetSlots);
        targetsPageIndex_ = std::max (0, (total - 1) / ui::ModTargetsPage::kPerPage);   // the page that shows the new row
    }
    void addModulation (IInstanceView& v, int source)
    {
        if (source == ui::kNoSource || modsTargetParam_ < 0 || uibind::slotOf (v, modsTargetParam_, source) >= 0)
            return;
        const int slot = uibind::freeSlot (v);
        const size_t dest = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        if (slot < 0 || dest == 0)
            return;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        uibind::writePlain (v, sp.destination, static_cast<double> (dest));
        uibind::writePlain (v, sp.amount, 0.0);
        if (sp.bypass >= 0) uibind::writePlain (v, sp.bypass, 0.0);
        if (sp.bipolar >= 0) uibind::writePlain (v, sp.bipolar, uibind::info (modsTargetParam_).min < 0.0f && uibind::info (modsTargetParam_).max > 0.0f ? 1.0 : 0.0);
        uibind::writePlain (v, sp.source, source);
        loadMods (v);
    }

    // ======================================================================================================================================================================
    // the Q group: the four targets the hardware knobs are mapped to
    int paramOfMember (const ui::Component* m) const noexcept
    {
        while (m != nullptr)
        {
            for (const Item& it : items_)
                if (it.c == m && (it.type == T::Knob || it.type == T::Toggle || it.type == T::Field))
                    return it.active (beats_);
            m = m->primaryChild();
        }
        return -1;
    }
    void publishQGroup (IInstanceView& v)
    {
        if (otherOpen_)
            return;   // the Q group belongs to the page of another element
        int t[4] = { -1, -1, -1, -1 };
        const ui::QGroup* g = nullptr;
        const char* name = "";
        if (stackDepth_ == 0)
        {
            g = &compact_.group;
            name = "ADSR";
            compact_.group.setActive (highlight_ == 2);
            if (highlight_ != 2)
                return;   // another sub-tab: its element owns the group
        }
        else if (pageTop_ == kPageTargets || pageTop_ == kPageMods)
        {
            name = pageTop_ == kPageTargets ? "Targets" : "Mods";
            const int row = pageTop_ == kPageTargets ? targets_.activeRow() : mods_.activeRow();
            const ModRow* rows = pageTop_ == kPageTargets ? targetRows_ : modRows_;
            if (row >= 0 && row < ui::ModulationsPage::kMaxRows && rows[row].slot >= 0)
            {
                const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (rows[row].slot);
                t[0] = sp.amount; t[1] = sp.bipolar; t[2] = sp.bypass;
            }
        }
        else if (pageTop_ == kPageList)
            name = "List";
        else
        {
            g = full_.activeGroup() >= 0 ? &full_.groups[full_.activeGroup()] : nullptr;
            name = ui::EnvFullScreen::groupName (full_.activeGroup());
        }
        if (g != nullptr)
            for (int i = 0; i < 4; ++i)
                t[i] = paramOfMember (g->member (i));
        if (! qSet_ || std::memcmp (t, qTargets_, sizeof t) != 0)
        {
            std::memcpy (qTargets_, t, sizeof t);
            v.setQGroup (t, 4);
            qSet_ = true;
        }
        ui::setText (qName_, sizeof qName_, name);
    }

    // ======================================================================================================================================================================
    ui::EnvCompact compact_;
    ui::EnvFullScreen full_;
    ui::ModTargetsPage targets_;
    ui::ModulationsPage mods_;
    ui::ListPopup list_;
    UiStack idle_;                       // an empty stack: the pages are read from it while another element's page is open
    std::vector<Item> items_;
    ui::Component* gesture_ = nullptr;
    bool page_ = false;                  // the page of an Env tab
    int pageSel_ = 1;                    // the envelope that page shows
    bool gestureDown_ = false, otherOpen_ = false, configured_ = false, modsDirty_ = true, qSet_ = false, beats_ = false;
    int w_ = 1280, h_ = 800, sel_ = 1, highlight_ = 1, stackDepth_ = 0, pageTop_ = 0, pageSig_ = 0, underList_ = 0, selectedSource_ = ui::kNoSource, numTargets_ = 0, targetsPageIndex_ = 0;
    int timeMs_[5] = { -1, -1, -1, -1, -1 }, timeBeats_[5] = { -1, -1, -1, -1, -1 }, sustainP_ = -1, curveP_[3] = { -1, -1, -1 };
    int scratchSlots_[uibind::kMaxTargetSlots] = {};
    double modsAtMs_ = -1000.0;
    std::string prefix_ = "env1.";
    char qName_[16] = {};
    int qTargets_[4] = { -1, -1, -1, -1 };
};

}} // namespace synthyboy_mpc::overlay
