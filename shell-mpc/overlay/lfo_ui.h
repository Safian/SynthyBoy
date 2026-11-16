// SynthyBoy - MPC overlay: the LFO UI of the Play page, the plugin-drawn pages built from the UI library (shell-mpc/ui/lfo_*.h, shape_editor.h, mod_targets.h, panel.h, ...) and wired to the real engine
// parameters (lfoN.* and rlfoN.* of the table). One object per (Play view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick (), with the instance
// as IInstanceView. The element has two types: an LFO (LFO 1 .. 10: a shape of one cycle, the drawn shape in the content state) and a RANDOM LFO (RND 1 .. 4: a style; the display is the trace of its output).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementLfo, the pages above it are ours): main = the compact view in the right third of the Play page (LfoCompact); full = the full screen
// (LfoFullScreen) over the whole plugin area of the Play page; detail pages above it: the shape editor (ShapeEditorPage), the targets of the LFO (ModTargetsPage: the modulation matrix seen from the source), the
// list popup (instances, choices, the destination to add) and the modulations of a control (ModulationsPage).
// What it does with a touch: the library component gives a Change { id, kind, value }; the id says which control it was, the binding of the id (a key suffix of the engine table and a type) says which parameter and
// how the UI value converts; the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). The drawn shape is not a parameter: the editor changes a shape model, which goes to the
// content state through IInstanceView::setLfoShape (at most every kShapeSendMs while a finger is down, and at the end of the gesture). Every update () reads every parameter back into the component states, so host
// automation, presets and the host's own widgets show on the next frame.
// Q-Link: the compact view and the full screen have Q groups of table parameters (q_router.h); the shape editor's group EDIT (X, Y, CURVE, GRID of the selected point) is VIRTUAL: the router keeps the values and hands
// the host's writes back through takeQVirtual.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../ui/lfo_screen.h"
#include "../ui/mod_targets.h"
#include "../ui/shape_editor.h"
#include "../ui/ui.h"
#include "../q_router.h"
#include "curve_source.h"
#include "page_view.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/lfo_display.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace overlay {

class LfoUi
{
public:
    // ---- the pages (UiStack ids)
    static constexpr int kElementPage = 3;                                    // the page of an LFO tab is the full screen of the element (page_view.h)
    static constexpr int kElementLfo = 3;                                     // the full screen of the LFO (the oscillator is 1, the envelope 2)
    enum Page { kPageEditor = 40, kPageTargets = 41, kPageList = 42, kPageMods = 43 };

    // ---- the id spaces of the controls (a Change comes out with the id the owner gave)
    enum Id : int
    {
        kClose = 1, kFullButton, kInstance, kLfoCells, kRndCells, kSyncMode, kSyncTrip, kSyncDot, kMonoTop, kTargets, kPicker, kSmoothPick, kHostPick, kEdit,
        kShape = 100, kStyle, kRate, kDivision, kRise, kSmoothTime, kSync, kMode, kPhase, kDelay, kDirection, kSwing, kStereo, kMono, kStereoToggle, kKeyTranspose, kKeyTune,
        kEdBack = 200, kEdTools, kEdUndo, kEdDelete, kEdSnap, kEdLoop, kEdSmooth, kEdCanvas, kEdX, kEdY, kEdCurve, kEdGrid,
        kTargetsBack = 300, kTargetsPage, kTargetsAdd, kTargetsRow = 310,   // 310 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kList = 400, kListBackdrop, kListHeader,
        kModsBack = 500, kModsRow = 510, kModsAddStep = 560, kModsAdd
    };

    /** What a value is and how it converts to the engine's plain value. */
    enum class T : uint8_t
    {
        Knob,      // normalised 0 .. 1 of the parameter
        Number,    // a NumberField in the units of the parameter
        Stepper,   // a choice parameter
        Toggle,
        Picker,    // the shape / style picker: the index is the value of the parameter
        Inert
    };
    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        int param = -1;
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kCompactX = 858, kCompactW = 408;                    // the right third of the Play page
    static constexpr int kInstances = 14;                                      // LFO 1 .. 10, RND 1 .. 4
    static constexpr int kCurvePoints = 256, kTracePoints = 192;
    static constexpr double kModsRefreshMs = 100.0, kShapeSendMs = 90.0, kTraceMs = 33.0;

    LfoUi() { setCanvas (kCanvasW, kCanvasH); }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        compact_.place ({ kCompactX, kTabY + 4, kCompactW, kTabH - 8 });
        full_.place (tab);
        editor_.place (tab);
        targets_.place (tab);
        mods_.place (tab);
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
        list_.setHeaderId (kListHeader);
    }

    /** Reads the instance: the selected LFO (the type and the number), every value of the visible controls, the modulations of the knobs, the pages of the stack, the shape, the cursors of the voices, the Q group. */
    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs, const synthyboy::LiveValues* live)
    {
        highlight_ = highlight;
        const int num = page_ ? lfoNum_ : std::clamp (inst.playSelection (2), 1, kLfos);   // the page of a tab keeps its own LFO (the tab names the first one); the Play page follows the host's selector
        if (! configured_ || num != lfoNum_)
        {
            lfoNum_ = num;   // the host's own Play LFO selector (a widget, automation, a preset) chose an LFO: that is the one shown (a random LFO is chosen here only)
            rnd_ = 0;
            configure (inst);
        }
        const bool open = stack.depth() > 0 && stack.element() == kElementLfo;
        if (open && stackDepth_ == 0)
            full_.setActiveGroup (0);
        stackDepth_ = open ? stack.depth() : 0;
        syncPages (open ? stack : idle_, inst);
        content_ = inst.content();
        bpm_ = inst.bpm();
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        pull (inst);
        updateDisplay (inst, live, nowMs);
        if (stackDepth_ > 0 && pageTop_ == kPageEditor)
            updateEditor (inst, nowMs);
        else if (shapeDirty_)
            flushShape (inst, nowMs);
        if (open)
            inst.setUiFullOpen (true);
        else if (stack.isMain())
            inst.setUiFullOpen (false);
        publishQGroup (inst);
    }

    /** The page of an LFO tab (page_view.h): the full screen of the LFO the tab names, nothing to close; the instance cells choose another LFO or random LFO for this page only. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        if (! page_)
        {
            page_ = true;
            lfoNum_ = std::clamp (in.instance, 1, kLfos);
            rnd_ = 0;
            full_.setPageMode (true);
            configured_ = false;
        }
        update (inst, 3, stack, nowMs, in.live);
    }
    /** The whole picture of the page: its ground, the full screen or the page above it, the cursors, the popup. */
    void draw (gpu::SceneBuilder& sb) const
    {
        drawPage (sb);
        drawLive (sb);
        drawPopup (sb);
    }

    bool isMain() const noexcept { return stackDepth_ == 0; }
    /** The LFO shown: 1 .. 10 (LFO) or 1 .. 4 (RND, `random ()`). */
    bool random() const noexcept { return rnd_ != 0; }
    int selection() const noexcept { return rnd_ != 0 ? rnd_ : lfoNum_; }
    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return full_.activeGroup(); }
    const int* qTargets() const noexcept { return qTargets_; }
    const char* qGroupName() const noexcept { return qName_; }
    /** The modulation source id (choice of modNN.source) of the LFO shown. */
    int source() const noexcept { return rnd_ != 0 ? 14 + rnd_ : uibind::lfoSource (lfoNum_); }
    /** The rectangle the picture of the LFO goes into now (empty when the current page shows none). */
    ui::Rect displayRect() const noexcept
    {
        if (stackDepth_ == 0)
            return compact_.displayRect();
        if (pageTop_ == 0 || pageTop_ == kPageList)
            return full_.displayRect();
        return {};
    }

    // ---- the drawing (a picture is one scene of cached cells, then the live parts, then the popups)
    void drawCompact (gpu::SceneBuilder& sb) const
    {
        const ui::theme::Look& L = ui::theme::L();
        {
            ui::Cell cell (sb, { kGroundX0, kTabY, kGroundX1 - kGroundX0, kTabH }, 0);
            sb.gradientRect (static_cast<float> (kGroundX0), static_cast<float> (kTabY), static_cast<float> (kGroundX1), static_cast<float> (kTabY + kTabH), ui::theme::rgba (L.bg0), ui::theme::rgba (L.bg1));
        }
        compact_.draw (sb);
        ui::lfodisplay::draw (sb, compact_.displayRect(), data_, ! displayHot_);
        compact_.full.draw (sb);
    }
    /** The opaque ground of the whole tab and the page of the stack (the full screen, the editor, the targets, the modulations of a control). */
    void drawPage (gpu::SceneBuilder& sb) const
    {
        const ui::theme::Look& L = ui::theme::L();
        {
            ui::Cell cell (sb, { 0, kTabY, w_, kTabH }, 0);
            sb.gradientRect (0.0f, static_cast<float> (kTabY), static_cast<float> (w_), static_cast<float> (kTabY + kTabH), ui::theme::rgba (L.bg0), ui::theme::rgba (L.bg1));
        }
        if (pageTop_ == kPageEditor)
        {
            editor_.draw (sb);
            return;
        }
        if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            targets_.draw (sb);
        else if (pageTop_ == kPageMods)
            mods_.draw (sb);
        else
        {
            full_.draw (sb);
            ui::lfodisplay::draw (sb, full_.displayRect(), data_, ! displayHot_);
        }
    }
    void drawPopup (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ > 0 && pageTop_ == kPageList)
            list_.draw (sb);
    }
    /** The cursors of the voices / the newest value of the trace (the display that is on screen). */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
            ui::lfodisplay::drawLive (sb, compact_.displayRect(), data_);
        else if (pageTop_ == 0 || (pageTop_ == kPageList && underList_ == 0))
            ui::lfodisplay::drawLive (sb, full_.displayRect(), data_);
    }
    /** What the current state draws, as one number (the picture changes when it does): a scene of everything, the live parts included. */
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
    /** The touch hit this UI's drawing (the main state: the right third; any other state: the whole tab). */
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
            handle (top->touchDown (x, y, nowMs), stack, inst, nowMs);
            publishQGroup (inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (x, y), stack, inst, nowMs);
        else
        {
            handle (gesture_->touchUp (x, y), stack, inst, nowMs);
            gestureDown_ = false;
            gesture_ = nullptr;
            if (shapeDirty_)
                flushShape (inst, nowMs);   // the end of a gesture: the shape goes to the content at once
        }
        return true;
    }
    /** The clock while a finger is down (the long press of a knob, of a point of the shape). */
    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst, nowMs);
    }
    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    /** A full screen opened or closed somewhere: the router keeps what the last page set, so this UI publishes its Q group again at the next update (). */
    void forgetQGroup() noexcept { qSet_ = false; }

    // ---- access for the tests
    ui::LfoCompact& compact() noexcept { return compact_; }
    ui::LfoFullScreen& full() noexcept { return full_; }
    ui::ShapeEditorPage& editor() noexcept { return editor_; }
    ui::ModTargetsPage& targetsPage() noexcept { return targets_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    ui::ListPopup& list() noexcept { return list_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    const Item* item (int id) const noexcept { for (const Item& it : items_) if (it.id == id) return &it; return nullptr; }
    const ui::lfodisplay::Data& displayData() const noexcept { return data_; }
    int gridOf (int lfo) const noexcept { return lfo >= 1 && lfo <= kLfos ? grid_[lfo - 1] : 8; }
    static constexpr int kLfos = 10;

    // ---- the drawn shape and the content state (public for the tests)
    static ui::lfo::ShapeModel modelOf (const synthyboy::ContentLfoShape& s)
    {
        ui::lfo::ShapeModel m;
        m.n = static_cast<int> (std::min<size_t> (s.points.size(), ui::lfo::kMaxPoints));
        for (int i = 0; i < m.n; ++i)
            m.p[i] = { s.points[static_cast<size_t> (i)].x, s.points[static_cast<size_t> (i)].y, s.points[static_cast<size_t> (i)].power };
        m.smooth = s.smooth;
        m.loop = s.loopPoint;
        m.clean();
        return m;
    }
    static synthyboy::ContentLfoShape contentOf (int lfo, const ui::lfo::ShapeModel& m)
    {
        synthyboy::ContentLfoShape s;
        s.lfo = lfo;
        s.name = "Drawn";
        s.smooth = m.smooth;
        s.loopPoint = m.loop >= 0 && m.loop < m.n ? m.loop : -1;
        for (int i = 0; i < m.n; ++i)
            s.points.emplace_back (m.p[i].x, m.p[i].y, m.p[i].power);
        return s;
    }

private:
    static constexpr int kGroundX0 = 852, kGroundX1 = 1280;
    static uint32_t mixIn (uint32_t h, int32_t v) noexcept
    {
        for (int b = 0; b < 4; ++b)
            h = (h ^ ((static_cast<uint32_t> (v) >> (8 * b)) & 255u)) * 16777619u;
        return h;
    }
    using ModRow = uibind::ModRow;

    // ======================================================================================================================================================================
    // building: the selected LFO (the type and the number) decides the parameters behind the ids
    void bind (int id, ui::Component* c, T type, const char* suffix)
    {
        Item it;
        it.id = id; it.c = c; it.type = type;
        it.param = suffix != nullptr && suffix[0] != '\0' ? uibind::indexOfKey (prefix_ + suffix) : -1;
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }
    void configure (IInstanceView& inst)
    {
        (void) inst;
        configured_ = true;
        const bool rnd = rnd_ != 0;
        prefix_ = rnd ? "rlfo" + std::to_string (rnd_) + "." : "lfo" + std::to_string (lfoNum_) + ".";
        items_.clear();
        // ---- the compact view
        bind (kInstance, &compact_.instance, T::Inert, "");
        bind (kFullButton, &compact_.full, T::Inert, "");
        bind (kShape, &compact_.shape, T::Stepper, rnd ? "" : "shape");
        bind (kStyle, &compact_.style, T::Stepper, rnd ? "style" : "");
        bind (kRate, &compact_.rate.rate, T::Knob, "rate");
        bind (kDivision, &compact_.rate.division, T::Stepper, "division");
        bind (kSyncMode, &compact_.rate.modeCells, T::Inert, "");
        bind (kRise, &compact_.rise, T::Knob, rnd ? "" : "fade_in");
        bind (kSmoothTime, &compact_.smoothTime, T::Knob, rnd ? "" : "smooth_time");
        bind (kSync, &compact_.sync, T::Stepper, "sync");
        bind (kStereoToggle, &compact_.stereo, T::Toggle, rnd ? "stereo" : "");
        // ---- the full screen
        bind (kClose, &full_.close, T::Inert, "");
        bind (kLfoCells, &full_.bar.lfoCells, T::Inert, "");
        bind (kRndCells, &full_.bar.rndCells, T::Inert, "");
        bind (kSyncMode, &full_.syncSwitch.mode, T::Inert, "");
        bind (kSyncTrip, &full_.syncSwitch.trip, T::Inert, "");
        bind (kSyncDot, &full_.syncSwitch.dot, T::Inert, "");
        bind (kMonoTop, &full_.monoTop, T::Inert, "");
        bind (kTargets, &full_.targets, T::Inert, "");
        bind (kPicker, &full_.picker, T::Picker, rnd ? "style" : "shape");
        bind (kSmoothPick, &full_.smoothPick, T::Toggle, rnd ? "" : "smooth");
        bind (kHostPick, &full_.hostPick, T::Toggle, rnd ? "" : "host");
        bind (kEdit, &full_.edit, T::Inert, "");
        bind (kStyle, &full_.style, T::Stepper, rnd ? "style" : "");
        bind (kSync, &full_.sync, T::Stepper, "sync");
        bind (kRate, &full_.rate.rate, T::Knob, "rate");
        bind (kDivision, &full_.rate.division, T::Stepper, "division");
        bind (kRise, &full_.rise, T::Knob, rnd ? "" : "fade_in");
        bind (kSmoothTime, &full_.smoothTime, T::Knob, rnd ? "" : "smooth_time");
        bind (kMode, &full_.mode, T::Stepper, "sync_type");
        bind (kPhase, &full_.phase, T::Knob, rnd ? "" : "phase");
        bind (kDelay, &full_.delay, T::Knob, rnd ? "" : "delay");
        bind (kDirection, &full_.direction, T::Stepper, rnd ? "" : "direction");
        bind (kSwing, &full_.swing, T::Knob, rnd ? "" : "swing");
        bind (kStereo, &full_.stereo, T::Knob, rnd ? "" : "stereo");
        bind (kMono, &full_.monoQ, T::Toggle, rnd ? "" : "mono");
        bind (kStereoToggle, &full_.stereoToggle, T::Toggle, rnd ? "stereo" : "");
        bind (kKeyTranspose, &full_.keyTranspose, T::Number, "keytrack_transpose");
        bind (kKeyTune, &full_.keyTune, T::Number, "keytrack_tune");
        // ---- the ranges of the knobs and fields come from the parameters
        for (Item& it : items_)
        {
            if (it.c == nullptr || it.param < 0)
                continue;
            const synthyboy::ParamInfo& pi = uibind::info (it.param);
            if (it.type == T::Knob)
            {
                auto* k = static_cast<ui::Knob*> (it.c);
                k->state.bipolar = pi.min < 0.0f && pi.max > 0.0f;
                k->state.defaultValue = synthyboy::toNormalised (pi, pi.def);
            }
            else if (it.type == T::Number)
            {
                auto& st = static_cast<ui::NumberField*> (it.c)->state;
                st.min = pi.min; st.max = pi.max; st.defaultValue = pi.def; st.step = 1.0;
            }
        }
        compact_.configure (rnd);
        full_.configure (rnd);
        full_.bar.configure (rnd, rnd ? rnd_ : lfoNum_);
        // the parameters of the display
        values_ = synthyboy::ParamValues::defaults();
        dispIdx_.clear();
        const std::string lp = "lfo" + std::to_string (lfoNum_) + ".";
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            if (std::strncmp (synthyboy::paramTable()[i].key, lp.c_str(), lp.size()) == 0)
                dispIdx_.push_back (static_cast<int> (i));
        seen_.assign (dispIdx_.size(), 0.0f);
        haveCurve_ = false;
        std::fill (std::begin (trace_), std::end (trace_), 0.0f);
        qSet_ = false;
        modsDirty_ = true;
        gesture_ = nullptr; gestureDown_ = false;
        pageSig_ = -1;
        nCache_ = 0;
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
        if (pageTop_ == kPageEditor && top != kPageEditor && top != kPageList)
            flushShape (inst, lastNowMs_);
        if (top == kPageList && pageTop_ != kPageList)
            underList_ = pageTop_;
        pageTop_ = top;
        pageSig_ = sig;
        switch (top)
        {
            case kPageTargets: targetsPageIndex_ = 0; loadTargets (inst, true); break;
            case kPageMods: loadMods (inst); break;
            case kPageEditor: editor_.sync(); break;
            default: break;
        }
        modsDirty_ = true;
        qSet_ = false;
    }

    // ---- the targets page: the rows of the matrix whose source is this LFO
    ModRow targetRows_[ui::ModTargetsPage::kPerPage];
    int targetTotal_ = 0;
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, source(), slots, uibind::kMaxTargetSlots);
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
            targetRows_[i] = { slots[first + i], source() };
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
        std::snprintf (title, sizeof title, rnd_ != 0 ? "RND %d" : "LFO %d", selection());
        targets_.load (e, n, title, ui::theme::sourceColour (rnd_ != 0 ? "RND" : "LFO"), total, targetsPageIndex_);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }
    /** The name of a destination choice ("LFO 1 Rate"); the appended destinations that are no table parameter get a generic name. */
    static void targetName (size_t destChoice, char* out, size_t cap)
    {
        const int p = synthyboy::modDestinationParamIndex (destChoice);
        if (p >= 0)
            std::snprintf (out, cap, "%s", uibind::info (p).name);
        else
            std::snprintf (out, cap, "Destination %d", static_cast<int> (destChoice));
    }
    void addTarget (IInstanceView& v, int destChoice)
    {
        if (destChoice <= 0 || uibind::slotOfPair (v, source(), destChoice) >= 0)
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
        uibind::writePlain (v, sp.source, source());
        modsDirty_ = true;
        const int total = uibind::slotsOfSource (v, source(), scratchSlots_, uibind::kMaxTargetSlots);
        targetsPageIndex_ = std::max (0, (total - 1) / ui::ModTargetsPage::kPerPage);   // the page that shows the new row
    }

    // ---- the list popup: the instances, the choices of a parameter, the groups of the destinations and the parameters of a group
    enum class ListKind : uint8_t { None, Instance, Param, Dest };
    using DestIndex = uibind::DestIndex;
    ListKind listKind_ = ListKind::None;
    int listParam_ = -1, listGroup_ = -1, underList_ = 0;
    std::vector<const char*> listNames_;
    void fillList (const IInstanceView* v)
    {
        listNames_.clear();
        list_.state.header = nullptr;
        list_.state.headerBack = false;
        list_.state.current = -1;
        if (listKind_ == ListKind::Instance)
        {
            for (int i = 0; i < kInstances; ++i)
                listNames_.push_back (instanceName (i));
            list_.state.current = slot();
        }
        else if (listKind_ == ListKind::Param && listParam_ >= 0)
        {
            const synthyboy::ParamInfo& pi = uibind::info (listParam_);
            for (int i = 0; i < pi.numChoices; ++i)
                listNames_.push_back (pi.choices[i]);
            if (v != nullptr)
                list_.state.current = static_cast<int> (std::lround (v->param (listParam_) - pi.min));
        }
        else if (listKind_ == ListKind::Dest)
        {
            const DestIndex& d = DestIndex::get();
            if (listGroup_ < 0)
                for (const std::string& g : d.groups)
                    listNames_.push_back (g.c_str());
            else
            {
                for (int c : d.choices[static_cast<size_t> (listGroup_)])
                    listNames_.push_back (uibind::info (synthyboy::modDestinationParamIndex (static_cast<size_t> (c))).name);
                list_.state.header = d.groups[static_cast<size_t> (listGroup_)].c_str();
                list_.state.headerBack = true;
            }
        }
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.showCurrent();
    }
    static const char* instanceName (int i) noexcept
    {
        static const char* const n[kInstances] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "LFO 9", "LFO 10", "RND 1", "RND 2", "RND 3", "RND 4" };
        return n[std::clamp (i, 0, kInstances - 1)];
    }
    /** The instance as one number 0 .. 13: LFO 1 .. 10, then RND 1 .. 4. */
    int slot() const noexcept { return rnd_ != 0 ? kLfos + rnd_ - 1 : lfoNum_ - 1; }
    void selectSlot (int s, IInstanceView& v)
    {
        s = std::clamp (s, 0, kInstances - 1);
        if (s >= kLfos)
            rnd_ = s - kLfos + 1;
        else
        {
            rnd_ = 0;
            lfoNum_ = s + 1;
            if (! page_)
                v.setPlaySelection (2, s + 1);   // the host follows (the Play LFO selector); the next update () reads the same number
        }
        configure (v);
        full_.bar.configure (rnd_ != 0, selection());
    }

    // ---- the modulations page of one control (a long press on a knob or a field)
    ModRow modRows_[ui::ModulationsPage::kMaxRows];
    int modsTarget_ = 0, modsTargetParam_ = -1, modsAddOffer_ = 0;
    void loadMods (const IInstanceView& v)
    {
        const Item* t = item (modsTarget_);
        ui::ModInput mi;
        char title[13] = "MODULATIONS";
        modsTargetParam_ = t != nullptr ? t->param : -1;
        if (t != nullptr && modsTargetParam_ >= 0)
        {
            if (t->c != nullptr && t->type == T::Knob)
                std::snprintf (title, sizeof title, "%.12s", static_cast<ui::Knob*> (t->c)->state.label);
            else if (t->c != nullptr && t->type == T::Number)
                std::snprintf (title, sizeof title, "%.12s", static_cast<ui::NumberField*> (t->c)->state.label);
            mi = uibind::modsOf (v, modsTargetParam_, source(), modRows_);
        }
        else
            for (ModRow& r : modRows_) r = {};
        mods_.load (mi, title);
        mods_.back.setId (kModsBack);
        mods_.addSource.setId (kModsAddStep);
        mods_.add.setId (kModsAdd);
        for (int r = 0; r < ui::ModulationsPage::kMaxRows; ++r)
            mods_.setRowIds (r, kModsRow + 4 * r, kModsRow + 4 * r + 1, kModsRow + 4 * r + 2, kModsRow + 4 * r + 3);
        // what "add source" offers: this LFO first, then the first LFOs, envelopes and macros
        int offer[8], n = 0;
        auto push = [&] (int s) { if (s == ui::kNoSource || n >= 8) return; for (int i = 0; i < n; ++i) if (offer[i] == s) return; offer[n++] = s; };
        push (source());
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
    void addModulation (IInstanceView& v, int src)
    {
        if (src == ui::kNoSource || modsTargetParam_ < 0 || uibind::slotOf (v, modsTargetParam_, src) >= 0)
            return;
        const int slot2 = uibind::freeSlot (v);
        const size_t dest = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        if (slot2 < 0 || dest == 0)
            return;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot2);
        uibind::writePlain (v, sp.destination, static_cast<double> (dest));
        uibind::writePlain (v, sp.amount, 0.0);
        if (sp.bypass >= 0) uibind::writePlain (v, sp.bypass, 0.0);
        if (sp.bipolar >= 0) uibind::writePlain (v, sp.bipolar, uibind::info (modsTargetParam_).min < 0.0f && uibind::info (modsTargetParam_).max > 0.0f ? 1.0 : 0.0);
        uibind::writePlain (v, sp.source, src);
        loadMods (v);
    }
    void refreshMods (const IInstanceView& v)
    {
        for (Item& it : items_)
        {
            if (it.c == nullptr || it.param < 0 || (it.type != T::Knob && it.type != T::Number))
                continue;
            const ui::ModInput mi = uibind::modsOf (v, it.param, source(), nullptr);
            ui::ModInput& dst = it.type == T::Knob ? static_cast<ui::ModInput&> (static_cast<ui::Knob*> (it.c)->state) : static_cast<ui::ModInput&> (static_cast<ui::NumberField*> (it.c)->state);
            dst = mi;
        }
        if (pageTop_ == kPageTargets)
            loadTargets (v, false);
        else if (pageTop_ == kPageMods && modsTargetParam_ >= 0)
        {
            const ui::ModInput mi = uibind::modsOf (v, modsTargetParam_, source(), modRows_);
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

    // ======================================================================================================================================================================
    // reading: the states of the components from the parameters
    static void fmt (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
    {
        if (pi.unit == synthyboy::Unit::Hz)
        {
            const double v = static_cast<double> (plain);
            if (v >= 1000.0) std::snprintf (out, cap, "%.2f kHz", v / 1000.0);
            else if (v < 10.0) std::snprintf (out, cap, "%.2f Hz", v);
            else std::snprintf (out, cap, v < 100.0 ? "%.1f Hz" : "%.0f Hz", v);
            return;
        }
        uibind::formatValue (pi, plain, out, cap);
    }
    static void showStepper (ui::Stepper* st, int param, float plain)
    {
        const synthyboy::ParamInfo& pi = uibind::info (param);
        const int n = pi.numChoices > 0 ? pi.numChoices : static_cast<int> (pi.max - pi.min) + 1;
        const int idx = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, n - 1));
        st->state.count = n;
        st->state.index = idx;
        st->state.wrap = pi.numChoices <= 14;
        if (pi.choices != nullptr && idx < pi.numChoices)
            st->setItem (pi.choices[idx]);
        else
        {
            char t[13];
            std::snprintf (t, sizeof t, "%d", idx);
            st->setItem (t);
        }
    }
    /** The table index of a key of the LFO shown ("sync" -> lfo3.sync): looked up once per string literal and configuration (the update asks for a dozen of them every picture). */
    int paramOf (const char* suffix) const
    {
        for (int i = 0; i < nCache_; ++i)
            if (cacheKey_[i] == suffix)
                return cacheIdx_[i];
        const int idx = uibind::indexOfKey (prefix_ + suffix);
        if (nCache_ < kCache)
        {
            cacheKey_[nCache_] = suffix;
            cacheIdx_[nCache_] = idx;
            ++nCache_;
        }
        return idx;
    }
    void pull (IInstanceView& v)
    {
        for (Item& it : items_)
        {
            if (it.c == nullptr)
                continue;
            if (it.type == T::Inert)
            {
                if (it.id == kInstance)
                {
                    auto* st = static_cast<ui::Stepper*> (it.c);
                    st->state.count = kInstances; st->state.index = slot(); st->state.wrap = true;
                    st->setItem (instanceName (slot()));
                }
                it.c->setEnabled (true);
                continue;
            }
            if (it.param < 0)
            {
                it.c->setEnabled (false);
                continue;
            }
            const float plain = v.param (it.param);
            const synthyboy::ParamInfo& pi = uibind::info (it.param);
            switch (it.type)
            {
                case T::Knob:
                {
                    auto* k = static_cast<ui::Knob*> (it.c);
                    k->state.value = k->state.modValue = uibind::normOf (it.param, plain);
                    fmt (pi, plain, k->state.valueText, sizeof k->state.valueText);
                    break;
                }
                case T::Number: static_cast<ui::NumberField*> (it.c)->state.value = plain; break;
                case T::Stepper: showStepper (static_cast<ui::Stepper*> (it.c), it.param, plain); break;
                case T::Toggle: static_cast<ui::ToggleIcon*> (it.c)->state.on = plain >= 0.5f; break;
                case T::Picker: static_cast<ui::ShapePicker*> (it.c)->state.index = std::clamp (static_cast<int> (std::lround (plain)), 0, static_cast<ui::ShapePicker*> (it.c)->count() - 1); break;
                default: break;
            }
            it.c->setEnabled (true);
        }
        // ---- what depends on the sync, the mode and the smoothing
        const int syncP = paramOf ("sync");
        const int sync = syncP >= 0 ? static_cast<int> (std::lround (v.param (syncP))) : 0;
        const ui::lfo::SyncMode sm = ui::lfo::syncModeOf (sync);
        compact_.rate.setMode (sm);
        full_.rate.setMode (sm);
        full_.syncSwitch.setSync (sync);
        full_.setKeyActive (sm == ui::lfo::SyncMode::Key);
        if (sm == ui::lfo::SyncMode::Key)   // the rate does not matter: the key decides
            compact_.rate.rate.state.valueText[0] = full_.rate.rate.state.valueText[0] = '\0';
        const int smoothP = paramOf ("smooth");
        const bool smoothOn = smoothP < 0 || v.param (smoothP) >= 0.5f;
        compact_.smoothTime.setEnabled (rnd_ == 0 && smoothOn);
        full_.smoothTime.setEnabled (rnd_ == 0 && smoothOn);
        for (const Item& it : items_)   // the rate knob's own enabled state is the control's (it is dim in the key follow mode)
            if (it.id == kRate && it.c != nullptr)
                it.c->setEnabled (sm != ui::lfo::SyncMode::Key && it.param >= 0);
        const int modeP = paramOf ("sync_type");
        syncType_ = modeP >= 0 ? static_cast<int> (std::lround (v.param (modeP))) : 0;
        const int shapeP = paramOf (rnd_ != 0 ? "style" : "shape");
        shapeIdx_ = shapeP >= 0 ? static_cast<int> (std::lround (v.param (shapeP))) : 0;
        full_.edit.setEnabled (rnd_ == 0);
        full_.bar.configure (rnd_ != 0, selection());
        if (stackDepth_ > 0 && pageTop_ == kPageEditor)
            editor_.sync();
        // the instance stepper of the compact view and the nameplate
        std::snprintf (name_, sizeof name_, rnd_ != 0 ? "RND %d - %s" : "LFO %d - %s", selection(),
                       rnd_ != 0 ? ui::styleName (shapeIdx_) : ui::lfoglyph::name (shapeIdx_));
    }

    // ======================================================================================================================================================================
    // the display: the shape of one cycle (the engine's own picture), the lead-in, the loop point, the cursors of the voices; the trace of a random LFO
    static float beatsOf (int division) noexcept
    {
        static const float kBeats[13] = { 0.0f, 128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
        return kBeats[std::clamp (division, 0, 12)];
    }
    /** The rate of the LFO in Hz as the engine reads it (0: frozen or not known: the key follow). */
    double rateHz (const IInstanceView& v) const noexcept
    {
        const int sync = static_cast<int> (std::lround (v.param (paramOf ("sync"))));
        if (sync == 0)
            return v.param (paramOf ("rate"));
        if (sync == 4)
            return 0.0;
        const float b = beatsOf (static_cast<int> (std::lround (v.param (paramOf ("division")))));
        if (! (b > 0.0f))
            return 0.0;
        return std::max (0.0, bpm_) / 60.0 / static_cast<double> (b) * (sync == 2 ? 1.0 / 1.5 : (sync == 3 ? 1.5 : 1.0));
    }
    void updateDisplay (IInstanceView& v, const synthyboy::LiveValues* live, double nowMs)
    {
        lastNowMs_ = nowMs;
        ui::lfodisplay::Data d;
        std::snprintf (label_, sizeof label_, "%s", name_);
        d.label = label_;
        d.dim = false;
        bool changed = false;
        if (rnd_ == 0)
        {
            // ---- the shape: the picture the engine's own code gives (shape, swing, direction, phase, sync type, smoothing), again when a parameter of the LFO, the content or the tempo moved
            bool dirty = ! haveCurve_ || content_ != contentSeen_ || bpm_ != bpmSeen_;
            for (size_t k = 0; k < dispIdx_.size(); ++k)
            {
                const float x = v.param (dispIdx_[k]);
                values_.plain[dispIdx_[k]] = x;
                dirty = dirty || x != seen_[k];
            }
            if (dirty && (nowMs - curveAtMs_ >= 30.0 || ! haveCurve_))
            {
                synthyboy::LfoDisplay::Request req;
                req.lfo = lfoNum_;
                req.bpm = bpm_;
                synthyboy::LfoDisplay::compute (values_, content_.get(), req, curve_, kCurvePoints);
                for (size_t k = 0; k < dispIdx_.size(); ++k)
                    seen_[k] = values_.plain[dispIdx_[k]];
                contentSeen_ = content_;
                bpmSeen_ = bpm_;
                haveCurve_ = true;
                curveAtMs_ = nowMs;
                changed = true;
            }
            d.kind = ui::lfodisplay::Data::Kind::Shape;
            d.y = curve_;
            d.n = kCurvePoints;
            const bool envType = syncType_ >= 2 && syncType_ <= 5;
            d.envelope = envType;
            const float phase = std::clamp (v.param (paramOf ("phase")) / 360.0f, 0.0f, 1.0f);
            d.phase = phase;
            d.loopX = syncType_ >= 3 ? phase : -1.0f;
            const double hz = rateHz (v);
            if (hz > 0.0)
            {
                const double period = 1.0 / hz;
                d.delayShare = static_cast<float> (std::min (4.0, static_cast<double> (v.param (paramOf ("delay"))) * 0.001 / period));
                d.riseShare = static_cast<float> (std::min (4.0, static_cast<double> (v.param (paramOf ("fade_in"))) * 0.001 / period));
            }
            d.grid = 0;
            // the cursors: one per sounding voice (the mono LFO has one), the first at the value the LFO really gives now
            d.numCursors = 0;
            if (live != nullptr)
            {
                const bool mono = v.param (paramOf ("mono")) >= 0.5f;
                const int n = mono ? std::min (1, live->numLfoVoices) : std::min (live->numLfoVoices, ui::lfodisplay::kMaxCursors);
                for (int i = 0; i < n; ++i)
                {
                    const float x = synthyboy::LfoDisplay::cursorX (live->lfoVoicePhase[lfoNum_ - 1][i]);
                    if (x < 0.0f)
                        continue;
                    const float f = x * static_cast<float> (kCurvePoints - 1);
                    const int k = std::min (static_cast<int> (f), kCurvePoints - 2);
                    d.cx[d.numCursors] = x;
                    d.cy[d.numCursors] = curve_[k] + (curve_[k + 1] - curve_[k]) * (f - static_cast<float> (k));
                    ++d.numCursors;
                }
                const int src = uibind::lfoSource (lfoNum_);
                if (d.numCursors > 0 && src > 0 && src <= synthyboy::LiveValues::kMaxSources)
                    d.cy[0] = std::clamp (live->source[src - 1], -1.0f, 1.0f);
            }
        }
        else
        {
            // ---- a random LFO: the trace of its output over the last moments
            d.kind = ui::lfodisplay::Data::Kind::Random;
            if (nowMs - traceAtMs_ >= kTraceMs)
            {
                traceAtMs_ = nowMs;
                float y = 0.0f;
                if (live != nullptr && live->numLfoVoices > 0)
                {
                    const int src = 14 + rnd_;
                    y = std::clamp (2.0f * live->source[src - 1] - 1.0f, -1.0f, 1.0f);   // the engine's scale of a random LFO is 0 .. 1 (0.5 + 0.5 value)
                }
                std::memmove (trace_, trace_ + 1, sizeof (float) * (kTracePoints - 1));
                trace_[kTracePoints - 1] = y;
                changed = true;
            }
            d.trace = trace_;
            d.traceN = kTracePoints;
        }
        if (changed)
            displayChangedMs_ = nowMs;
        displayHot_ = nowMs - displayChangedMs_ < 350.0;
        data_ = d;
        data_.label = label_;
    }

    // ======================================================================================================================================================================
    // the shape editor
    /** Makes the shape of the LFO a drawn one (the built-in shape is copied into points) and opens the editor. */
    void openEditor (UiStack& stack, IInstanceView& v)
    {
        if (rnd_ != 0)
            return;
        const int shapeP = paramOf ("shape");
        if (shapeP < 0)
            return;
        ui::lfo::ShapeModel m;
        const int shape = static_cast<int> (std::lround (v.param (shapeP)));
        const synthyboy::ContentLfoShape* have = contentShape (lfoNum_);
        if (shape == ui::lfo::kShapeCustom && have != nullptr)
            m = modelOf (*have);
        else if (shape == ui::lfo::kShapeCustom)
            m = ui::lfo::ShapeModel::fromBuiltin (1);   // a drawn shape that has no points yet starts as a triangle
        else
            m = ui::lfo::ShapeModel::fromBuiltin (shape);
        editor_.model = m;
        editor_.undo.clear();
        editor_.canvas.state.selected = -1;
        editor_.canvas.state.selectedSeg = -1;
        int& g = grid_[lfoNum_ - 1];
        if (g == 0)
            g = ui::lfo::inferGrid (m);
        editor_.canvas.state.grid = g;
        setIds();
        if (shape != ui::lfo::kShapeCustom || have == nullptr)
        {
            v.setLfoShape (lfoNum_, contentOf (lfoNum_, m));
            uibind::writePlain (v, shapeP, ui::lfo::kShapeCustom);
        }
        editor_.sync();
        stack.openDetail (kPageEditor);
    }
    const synthyboy::ContentLfoShape* contentShape (int lfo) const noexcept
    {
        if (content_ == nullptr)
            return nullptr;
        for (const synthyboy::ContentLfoShape& s : content_->lfoShapes)
            if (s.lfo == lfo && s.points.size() >= 2)
                return &s;
        return nullptr;
    }
    /** The shape is the drawn one: it must have points (the engine reads a drawn shape without points as a flat line). */
    void ensureCustom (IInstanceView& v)
    {
        if (rnd_ != 0 || contentShape (lfoNum_) != nullptr)
            return;
        v.setLfoShape (lfoNum_, contentOf (lfoNum_, ui::lfo::ShapeModel::fromBuiltin (1)));
    }
    void setIds()
    {
        editor_.back.setId (kEdBack); editor_.tools.setId (kEdTools); editor_.undoButton.setId (kEdUndo); editor_.del.setId (kEdDelete);
        editor_.snap.setId (kEdSnap); editor_.loop.setId (kEdLoop); editor_.smooth.setId (kEdSmooth); editor_.canvas.setId (kEdCanvas);
        editor_.fx.setId (kEdX); editor_.fy.setId (kEdY); editor_.fcurve.setId (kEdCurve); editor_.fgrid.setId (kEdGrid);
    }
    void markDirty() noexcept { shapeDirty_ = true; }
    /** Sends the model to the content state (the plugin applies it on its display thread). */
    void flushShape (IInstanceView& v, double nowMs)
    {
        if (! shapeDirty_)
            return;
        shapeDirty_ = false;
        shapeSentMs_ = nowMs;
        v.setLfoShape (lfoNum_, contentOf (lfoNum_, editor_.model));
    }
    // the virtual targets of the Q group EDIT: 0 X, 1 Y, 2 CURVE, 3 GRID, each as 0 .. 1
    float virtualValue (int id) const noexcept
    {
        const ui::lfo::ShapeModel& m = editor_.model;
        const int sel = editor_.canvas.state.selected;
        const bool ok = sel >= 0 && sel < m.n;
        switch (id)
        {
            case 0: return ok ? m.p[sel].x : 0.0f;
            case 1: return ok ? m.p[sel].y : 0.0f;
            case 2: return ok && sel + 1 < m.n ? (m.p[sel].power + ui::lfo::kMaxPower) / (2.0f * ui::lfo::kMaxPower) : 0.5f;
            default: return static_cast<float> (editor_.canvas.state.grid - ui::lfo::kMinGrid) / static_cast<float> (ui::lfo::kMaxGrid - ui::lfo::kMinGrid);
        }
    }
    void applyVirtual (int id, float x)
    {
        ui::lfo::ShapeModel& m = editor_.model;
        ui::ShapeCanvas::State& cs = editor_.canvas.state;
        const int sel = cs.selected;
        const bool ok = sel >= 0 && sel < m.n;
        if (id == 3)
        {
            const int g = std::clamp (ui::lfo::kMinGrid + static_cast<int> (std::lround (x * static_cast<float> (ui::lfo::kMaxGrid - ui::lfo::kMinGrid))), ui::lfo::kMinGrid, ui::lfo::kMaxGrid);
            cs.grid = g;
            grid_[lfoNum_ - 1] = g;
            return;
        }
        if (! ok)
            return;
        editor_.undo.push (m);
        if (id == 0) m.move (sel, x, m.p[sel].y);
        else if (id == 1) m.move (sel, m.p[sel].x, x);
        else if (sel + 1 < m.n) m.setPower (sel, std::round (x * 2.0f * ui::lfo::kMaxPower - ui::lfo::kMaxPower));
        markDirty();
    }
    void updateEditor (IInstanceView& v, double nowMs)
    {
        for (int id = 0; id < 4; ++id)
        {
            float x;
            if (v.takeQVirtual (id, x))
                applyVirtual (id, x);
        }
        editor_.sync();
        for (int id = 0; id < 4; ++id)
            v.setQVirtual (id, virtualValue (id));
        if (shapeDirty_ && (nowMs - shapeSentMs_ >= kShapeSendMs || ! gestureDown_))
            flushShape (v, nowMs);
    }
    void editorChange (const ui::Change& c, UiStack& stack, IInstanceView& v, double nowMs)
    {
        using K = ui::ChangeKind;
        ui::lfo::ShapeModel& m = editor_.model;
        ui::ShapeCanvas::State& cs = editor_.canvas.state;
        const int sel = cs.selected >= 0 && cs.selected < m.n ? cs.selected : -1;
        switch (c.id)
        {
            case kEdBack: flushShape (v, nowMs); stack.back(); return;
            case kEdTools: if (c.kind == K::Index) cs.tool = std::clamp (c.index, 0, ui::ToolSelector::kTools - 1); break;
            case kEdUndo:
                if (c.kind == K::Request)
                {
                    ui::lfo::ShapeModel back;
                    if (editor_.undo.pop (back))
                    {
                        m = back;
                        if (cs.selected >= m.n) cs.selected = -1;
                        markDirty();
                    }
                }
                break;
            case kEdDelete:
                if (c.kind == K::Request && sel > 0 && sel < m.n - 1)
                {
                    editor_.undo.push (m);
                    if (m.remove (sel))
                        cs.selected = -1;
                    markDirty();
                }
                break;
            case kEdSnap: if (c.kind == K::Index) cs.snap = c.index != 0; break;
            case kEdLoop:
                if (c.kind == K::Index && sel >= 0)
                {
                    editor_.undo.push (m);
                    m.loop = c.index != 0 ? sel : -1;
                    markDirty();
                }
                break;
            case kEdSmooth:
                if (c.kind == K::Index)
                {
                    editor_.undo.push (m);
                    m.smooth = c.index != 0;
                    markDirty();
                }
                break;
            case kEdCanvas:
                if (c.kind == K::Index && c.index == ui::ShapeCanvas::Edited)
                    markDirty();
                break;
            case kEdX: case kEdY:
                if ((c.kind == K::Index || c.kind == K::Value) && sel >= 0)
                {
                    editor_.undo.push (m);
                    if (c.id == kEdX) m.move (sel, static_cast<float> (c.value), m.p[sel].y); else m.move (sel, m.p[sel].x, static_cast<float> (c.value));
                    markDirty();
                }
                break;
            case kEdCurve:
                if ((c.kind == K::Index || c.kind == K::Value) && sel >= 0 && sel + 1 < m.n)
                {
                    editor_.undo.push (m);
                    m.setPower (sel, static_cast<float> (c.value));
                    markDirty();
                }
                break;
            case kEdGrid:
                if (c.kind == K::Index || c.kind == K::Value)
                {
                    cs.grid = std::clamp (static_cast<int> (std::lround (c.value)), ui::lfo::kMinGrid, ui::lfo::kMaxGrid);
                    grid_[lfoNum_ - 1] = cs.grid;
                }
                break;
            default: break;
        }
        editor_.sync();
    }

    // ======================================================================================================================================================================
    // writing: a Change of a component to a parameter
    ui::Component* topComponent() noexcept
    {
        if (stackDepth_ == 0)
            return &compact_;
        if (pageTop_ == kPageEditor)
            return &editor_;
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
    void openParamList (const Item* it, UiStack& stack)
    {
        if (it == nullptr || it->param < 0 || uibind::info (it->param).choices == nullptr || uibind::info (it->param).numChoices < 2)
            return;
        listKind_ = ListKind::Param;
        listParam_ = it->param;
        stack.openDetail (kPageList);
        fillList (nullptr);
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v, double nowMs)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        if (id >= kEdBack && id <= kEdGrid && stackDepth_ > 0 && pageTop_ == kPageEditor)
        {
            editorChange (c, stack, v, nowMs);
            return;
        }
        if (c.kind == K::Request)
        {
            switch (id)
            {
                case kFullButton: stack.openFull (kElementLfo); return;
                case kClose: case kTargetsBack: case kModsBack: stack.back(); return;
                case kListBackdrop: stack.back(); return;
                case kTargets: stack.openDetail (kPageTargets); return;
                case kTargetsAdd: listKind_ = ListKind::Dest; listGroup_ = -1; stack.openDetail (kPageList); fillList (&v); return;
                case kListHeader: if (listKind_ == ListKind::Dest) { listGroup_ = -1; fillList (&v); } return;
                case kEdit: openEditor (stack, v); return;
                case kInstance: listKind_ = ListKind::Instance; if (stack.depth() == 0) stack.openFull (kElementLfo); stack.openDetail (kPageList); fillList (&v); return;
                case kShape: case kStyle: case kSync: case kMode: case kDirection: case kDivision:
                    if (stack.depth() == 0)
                        stack.openFull (kElementLfo);   // the list lies over the full screen: from the compact view that opens first
                    openParamList (find (id), stack);
                    return;
                default: break;
            }
            if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage && (id - kTargetsRow) % 4 == 3)
            {
                const int s = targetRows_[(id - kTargetsRow) / 4].slot;
                if (s >= 0)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    modsDirty_ = true;
                    loadTargets (v, true);
                }
                return;
            }
            if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kMaxRows && (id - kModsRow) % 4 == 3)
            {
                const int s = modRows_[(id - kModsRow) / 4].slot;
                if (s >= 0)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    loadMods (v);
                }
            }
            return;
        }
        if (c.kind == K::ModsRequest)
        {
            if (stack.depth() == 0)
                stack.openFull (kElementLfo);
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
            listChosen (c.index, stack, v);
            return;
        }
        // ---- the instance: the stepper of the compact view, the cells of the full screen
        if (id == kInstance && c.kind == K::Index)
        {
            selectSlot (c.index, v);
            return;
        }
        if ((id == kLfoCells || id == kRndCells) && c.kind == K::Index)
        {
            if (id == kRndCells)
                selectSlot (kLfos + c.index, v);
            else
                selectSlot (c.index, v);
            return;
        }
        // ---- the sync switch: the mode and the two switches make one parameter
        if ((id == kSyncMode || id == kSyncTrip || id == kSyncDot) && c.kind == K::Index)
        {
            const int p = paramOf ("sync");
            const ui::Component* from = id == kSyncMode ? static_cast<const ui::Component*> (&full_.syncSwitch.mode) : (id == kSyncTrip ? static_cast<const ui::Component*> (&full_.syncSwitch.trip) : &full_.syncSwitch.dot);
            if (p >= 0)
                uibind::writePlain (v, p, full_.syncSwitch.nextSync (from, c));
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
        // ---- the rows of the targets page and of the modulations page of a control
        if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage)
        {
            matrixRow (targetRows_[(id - kTargetsRow) / 4].slot, (id - kTargetsRow) % 4, c, v);
            return;
        }
        if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kMaxRows)
        {
            matrixRow (modRows_[(id - kModsRow) / 4].slot, (id - kModsRow) % 4, c, v);
            return;
        }
        Item* it = find (id);
        if (it == nullptr || it->c == nullptr || it->param < 0)
            return;
        if (c.kind == K::Depth)
        {
            const int s = uibind::slotOf (v, it->param, c.index);
            if (s >= 0)
                uibind::writeNorm (v, synthyboy::modSlotParams (s).amount, (c.value + 1.0) * 0.5);
            modsDirty_ = true;
            return;
        }
        write (*it, c, v);
    }
    void matrixRow (int s, int what, const ui::Change& c, IInstanceView& v)
    {
        using K = ui::ChangeKind;
        if (s < 0)
            return;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        switch (what)
        {
            case 0: if (c.kind == K::Value) uibind::writeNorm (v, sp.amount, c.value); break;
            case 1: if (c.kind == K::Index) uibind::writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0); break;
            case 2: if (c.kind == K::Index) uibind::writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0); break;   // the switch says ON (active): bypassed is its opposite
            default: break;
        }
        modsDirty_ = true;
    }
    void listChosen (int index, UiStack& stack, IInstanceView& v)
    {
        if (listKind_ == ListKind::Instance)
        {
            selectSlot (index, v);
            stack.back();
        }
        else if (listKind_ == ListKind::Param)
        {
            if (listParam_ >= 0)
            {
                uibind::writePlain (v, listParam_, uibind::info (listParam_).min + index);
                if (listParam_ == paramOf ("shape") && index == ui::lfo::kShapeCustom)
                    ensureCustom (v);
            }
            stack.back();
        }
        else if (listKind_ == ListKind::Dest)
        {
            if (listGroup_ < 0)
            {
                listGroup_ = index;
                fillList (&v);
            }
            else
            {
                const std::vector<int>& ch = DestIndex::get().choices[static_cast<size_t> (listGroup_)];
                if (index >= 0 && index < static_cast<int> (ch.size()))
                    addTarget (v, ch[static_cast<size_t> (index)]);
                stack.back();
            }
        }
    }
    void write (Item& it, const ui::Change& c, IInstanceView& v)
    {
        using K = ui::ChangeKind;
        const int p = it.param;
        const synthyboy::ParamInfo& pi = uibind::info (p);
        switch (it.type)
        {
            case T::Knob: if (c.kind == K::Value) uibind::writeNorm (v, p, c.value); break;
            case T::Number: if (c.kind == K::Index || c.kind == K::Value) uibind::writePlain (v, p, c.value); break;
            case T::Stepper:
                if (c.kind == K::Index)
                {
                    const int n = pi.numChoices > 0 ? pi.numChoices : static_cast<int> (pi.max - pi.min) + 1;
                    const int idx = n > 0 ? (c.index % n + n) % n : 0;
                    uibind::writePlain (v, p, pi.min + idx);
                    if (idx == ui::lfo::kShapeCustom && it.id == kShape)
                        ensureCustom (v);
                }
                break;
            case T::Toggle: if (c.kind == K::Index) uibind::writePlain (v, p, c.index != 0 ? 1.0 : 0.0); break;
            case T::Picker:
                if (c.kind == K::Index)
                {
                    uibind::writePlain (v, p, c.index);
                    if (rnd_ == 0 && c.index == ui::lfo::kShapeCustom)
                        ensureCustom (v);
                }
                break;
            default: break;
        }
    }

    // ======================================================================================================================================================================
    // the Q group: the four targets the hardware knobs are mapped to
    int paramOfMember (const ui::Component* m) const noexcept
    {
        while (m != nullptr)
        {
            for (const Item& it : items_)
                if (it.c == m && it.type != T::Inert)
                    return it.param;
            m = m->primaryChild();
        }
        return -1;
    }
    void publishQGroup (IInstanceView& v)
    {
        int t[4] = { -1, -1, -1, -1 };
        const ui::QGroup* g = nullptr;
        const char* name = "";
        if (stackDepth_ == 0)
        {
            g = &compact_.group;
            name = rnd_ != 0 ? "Random" : "Rate";
            compact_.group.setActive (highlight_ == 3);
            if (highlight_ != 3)
                return;   // another sub-tab: its element owns the group
        }
        else if (pageTop_ == kPageEditor)
        {
            name = "Edit";
            for (int i = 0; i < 4; ++i)
                t[i] = QRouter::encode (i, QRouter::kVirtual);
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
            name = full_.activeGroup() >= 0 ? full_.groupName (full_.activeGroup()) : "";
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
    ui::LfoCompact compact_;
    ui::LfoFullScreen full_;
    ui::ShapeEditorPage editor_;
    ui::ModTargetsPage targets_;
    ui::ModulationsPage mods_;
    ui::ListPopup list_;
    UiStack idle_;                       // an empty stack: the pages are read from it while another element's page is open
    std::vector<Item> items_;
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false, configured_ = false, modsDirty_ = true, qSet_ = false, shapeDirty_ = false, haveCurve_ = false, displayHot_ = false;
    bool page_ = false;   // the page of an LFO tab
    int w_ = 1280, h_ = 800, lfoNum_ = 1, rnd_ = 0, highlight_ = 1, stackDepth_ = 0, pageTop_ = 0, pageSig_ = -1;
    int targetsPageIndex_ = 0, syncType_ = 0, shapeIdx_ = 0;
    int scratchSlots_[uibind::kMaxTargetSlots] = {};
    int grid_[kLfos] = {};               // the grid of the editor of every LFO (0: not chosen yet: the grid the shape was drawn on)
    double modsAtMs_ = -1000.0, shapeSentMs_ = -1000.0, curveAtMs_ = -1000.0, traceAtMs_ = -1000.0, displayChangedMs_ = -1000.0, lastNowMs_ = 0.0, bpm_ = 120.0, bpmSeen_ = -1.0;
    std::string prefix_ = "lfo1.";
    static constexpr int kCache = 16;
    mutable const char* cacheKey_[kCache] = {};
    mutable int cacheIdx_[kCache] = {}, nCache_ = 0;
    char qName_[16] = {}, name_[40] = {}, label_[40] = {};
    int qTargets_[4] = { -1, -1, -1, -1 };
    // the display
    synthyboy::ParamValues values_ = synthyboy::ParamValues::defaults();
    std::vector<int> dispIdx_;
    std::vector<float> seen_;
    std::shared_ptr<const synthyboy::StateContent> content_, contentSeen_;
    float curve_[kCurvePoints] = {};
    float trace_[kTracePoints] = {};
    ui::lfodisplay::Data data_;
};

}} // namespace synthyboy_mpc::overlay
