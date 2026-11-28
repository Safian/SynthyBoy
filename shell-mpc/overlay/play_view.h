// SynthyBoy - MPC overlay: the PLAY view, one composite picture of three panels side by side (docs/MPC.md "Play selectors and proxies"): the oscillator cycle, the envelope curve with the live dot and the
// LFO shape with the live cursor of the instances the three plugin parameters "Play Osc", "Play Env" and "Play LFO" select (IInstanceView::playSelection). The same picture is on the three
// sub-tabs of the Play function key; the panel of the sub-tab that is on screen (the view's `index`: 1 oscillator, 2 envelope, 3 LFO) has a frame. Every panel has a small title with the
// selected instance ("OSC 2", "ENV 3", "LFO 10"). The picture is drawn once through a sink (CpuSink: the canvas, SceneSink: the GPU helper's display list), so the two renderers
// cannot drift apart.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>

#include "../native_play_expanded.h"
#include "../play_params.h"
#include "../ui/display.h"
#include "env_ui.h"
#include "lfo_ui.h"
#include "macro_names.h"
#include "native_marker_covers.h"
#include "osc_display.h"
#include "osc_page.h"
#include "osc_ui.h"
#include "page_detector.h"
#include "page_view_base.h"
#include "play_osc.h"
#include "play_env_lfo.h"
#include "scene_cpu.h"
#include "synthyboy/env_display.h"
#include "views.h"

namespace synthyboy_mpc { namespace overlay {

namespace play
{
constexpr int kPanels = 3;
constexpr int kOscs = 3, kEnvs = 6, kLfos = 10;                 // the instances of the own engine (LiveValues::kNumEnvs / kNumLfos, three wavetable oscillators)
constexpr int kOscSelections = playosc::kInstances;            // the oscillator selector: Sub, A, B, C, Noise (play_osc.h); the picture exists for A, B, C only
constexpr int kPanelW = 400, kPanelGap = 17;                    // 3 * 400 + 2 * 17 = 1234 px: the region of the view
constexpr int kPlotX = 10, kPlotTop = 40, kPlotBottomMargin = 8, kPlotW = 380;
constexpr int kEnvPoints = 256;
constexpr int kOx = 23, kOy = 14, kPanelH = 190;                // the Env and LFO panels as they always were (tab coordinates of the old region)
constexpr int kNativeSelectorY = 10, kNativeSelectorH = 56;     // the host-owned compact Play header row (tab coordinates; 82_play.py)
constexpr int kRegionW = 1280, kRegionH = 800, kTabY = 110, kTabH = 628;   // the region of the view: the whole screen (every coordinate of the pages and of the touch is a screen pixel); the Play tab (the compact oscillator view, the full screen, the Env and LFO panels) is its rows kTabY .. kTabY + kTabH
constexpr int kDisplayPoints = 256;                              // the points of a min / max overview (two polylines of this many vertices)

/** The title of panel p (0 oscillator, 1 envelope, 2 LFO) for the instance n (1 ..). */
inline std::string title (int p, int n)
{
    if (p == 0)
    {
        static const char* const kOscTitles[kOscSelections] = { "SUB", "OSC A", "OSC B", "OSC C", "NOISE" };
        return kOscTitles[std::clamp (n, 1, kOscSelections) - 1];
    }
    static const char* const kNames[kPanels] = { "OSC ", "ENV ", "LFO " };
    return kNames[p] + std::to_string (n);
}

} // namespace play

class PlayView final : public IView
{
public:
    static constexpr int kW = play::kRegionW, kH = play::kRegionH;

    /** ONE Play page (2026-11-18; the sub-tabs Play Env and Play LFO are gone). `highlight`: the third whose Q group is the active one at first (1 oscillator, 2 envelope, 3 LFO; the page starts with 1; a touch on a control
        of a third makes it the active one: on the 4 knob device it moves to Q 1 .. 4 with the others following in reading order, on the 16 knob device the slots stay where they are). The view is named "play1". */
    PlayView (int highlight, double fixedCostMs) : osd_ (fixedCostMs), highlight_ (std::clamp (highlight, 1, 3)), name_ ("play1")
    {
        for (ComputeBudget& b : budget_)
            b.fixedCostMs = fixedCostMs;
        envY_.assign (play::kEnvPoints, 0.0f);
        lfoY_.assign (LfoView::kPoints, 0.0f);
    }

    const char* name() const noexcept override { return name_.c_str(); }
    /** All three oscillators' readouts (12 values): the selected one is used. */
    void targets (std::vector<int>& out) const override { osd_.targets (out); }

    bool update (const ViewInput& in) override
    {
        ++counters_.updates;
        bool changed = false;
        const IInstanceView& inst = instanceOf (in);   // (the generic ViewInput of view.h: SynthyBoy's views take their instance, writable instance and live block back: view_cast.h)
        IInstanceView* const io = ioOf (in);
        const synthyboy::LiveValues* const live = liveOf (in);
        const int want[play::kPanels] = { std::clamp (inst.playSelection (0), 1, play::kOscSelections), std::clamp (inst.playSelection (1), 1, play::kEnvs),
                                           std::clamp (inst.playSelection (2), 1, play::kLfos) };
        std::shared_ptr<const synthyboy::StateContent> content = inst.content();
        names_.load (content.get());   // the names of the macros the chips of the knobs show (macro_names.h)
        const NamesScope scope (names_);
        const double bpm = std::round (inst.bpm() * 10.0) / 10.0;
        const bool usePageUi = io != nullptr && in.ui != nullptr;
        changed |= usePageUi != usePageLfo_;
        usePageLfo_ = usePageUi;
        for (int p = 0; p < play::kPanels; ++p)
        {
            if (want[p] != sel_[p])
            {
                sel_[p] = want[p];
                const char* prefix[play::kPanels] = { "osc", "env", "lfo" };
                if (p > 0)
                {
                    group_[p] = std::make_unique<ParamGroup> (std::string (prefix[p]) + std::to_string (sel_[p]) + ".");
                    forced_[p] = true;
                    have_[p] = false;
                }
                changed = true;
            }
        }
        // ---- oscillator: the picture of the display rectangle of the overlay oscillator UI (osc_display.h: the cycle of a wavetable, the overview of a sample, multisample or granular oscillator, the
        // spectrum of a spectral one, the noise colour, the sub oscillator's waveform, the live read-outs)
        if (! expandedOscActive_)
            changed = osd_.update (inst, sel_[0], in.targets, live, in.nowMs, counters_) || changed;
        // ---- envelope: the curve (cheap) and the dot
        {
            const bool data = group_[1]->read (inst) || bpm != envBpm_;
            if (data || ! have_[1])
            {
                synthyboy::EnvDisplay::Request req;
                req.env = sel_[1];
                req.bpm = bpm;
                synthyboy::EnvDisplay::compute (group_[1]->values(), req, envGeom_, envY_.data(), play::kEnvPoints);
                group_[1]->accept();
                have_[1] = true;
                forced_[1] = false;
                envBpm_ = bpm;
                changed = true;
            }
            int px = -1;
            double dx = 0.0, dl = 0.0;
            if (live != nullptr && synthyboy::EnvDisplay::dotFor (envGeom_, envY_.data(), play::kEnvPoints, live->envFineStage[sel_[1] - 1], live->envFineFraction[sel_[1] - 1], dx, dl))
                px = static_cast<int> (std::lround (dx * (play::kPlotW - 1)));
            if (px != envDotPx_)
            {
                envDotPx_ = px;
                envDotX_ = dx;
                changed = true;
            }
        }
        // The read-only view supplies its own shape. The interactive page uses LfoUi's plot.
        if (! usePageLfo_)
        {
            const synthyboy::IContentStore* store = inst.contentStore();
            const bool newContent = content != content_ || store != store_;
            float sources[kSourceSlots] = {};
            bool sourcesChanged = false;
            if (static_cast<int> (group_[2]->value ((std::string ("lfo") + std::to_string (sel_[2]) + ".shape").c_str()) + 0.5f) == 12 && live != nullptr)
            {
                for (int id = 1; id < kSourceSlots && id - 1 < synthyboy::LiveValues::kMaxSources; ++id)
                {
                    const float v = live->source[id - 1];
                    const bool bipolar = (id >= 7 && id <= 14) || (id >= 15 && id <= 18) || id == 33 || id == 34;
                    sources[id] = bipolar ? 0.5f + 0.5f * v : v;
                }
                sourcesChanged = std::memcmp (sources, lastSources_, sizeof sources) != 0;
            }
            const bool data = group_[2]->read (inst) || newContent || bpm != bpm_;
            if ((data || sourcesChanged || ! have_[2]) && (forced_[2] || ! have_[2] || budget_[2].due (in.nowMs)))
            {
                const auto t0 = std::chrono::steady_clock::now();
                synthyboy::LfoDisplay::Request req;
                req.lfo = sel_[2];
                req.bpm = bpm;
                req.sources = live != nullptr ? sources : nullptr;
                synthyboy::LfoDisplay::compute (group_[2]->values(), content.get(), req, lfoY_.data(), LfoView::kPoints);
                budget_[2].spent (in.nowMs, msSince (t0));
                group_[2]->accept();
                std::memcpy (lastSources_, sources, sizeof sources);
                have_[2] = true;
                forced_[2] = false;
                content_ = content;
                store_ = store;
                bpm_ = bpm;
                changed = true;
            }
            const float x = live != nullptr ? synthyboy::LfoDisplay::cursorX (live->lfoPhase[sel_[2] - 1]) : -1.0f;
            const int px = x < 0.0f ? -1 : static_cast<int> (std::lround (x * (play::kPlotW - 1)));
            if (px != lfoCursorPx_)
            {
                lfoCursorPx_ = px;
                lfoCursorX_ = x;
                changed = true;
            }
        }
        if (in.panelEdgeX != edgeX_)
        {
            edgeX_ = in.panelEdgeX;
            changed = true;
        }
        // ---- the overlay oscillator pages: they read their parameters and the Q group; what they draw changes the picture
        if (usePageUi)
        {
            if (! osc_)
            {
                osc_ = std::make_unique<OscUi>();
                osc_->setCanvas (play::kRegionW, play::kRegionH);
                scratch_ = std::make_unique<gpu::SceneData>();
            }
            // the stack of the instance is shared by the elements: a page that belongs to another element is not the oscillator's (it sees an empty stack: its compact view)
            const auto pullOsc = [&] {
                osc_->update (*io, highlight_, in.ui->isMain() || in.ui->element() == OscUi::kElementOsc ? *in.ui : idleStack_,
                              in.nowMs, sel_[1], sel_[2]);
            };
            const auto syncExpandedOscExit = [&] {
                if (expandedOscActive_ && (in.ui->element() != OscUi::kElementOsc || &ui::theme::L() != &ui::theme::kLook1))
                {
                    expandedOsc_->leavePlay (*io);
                    expandedOscActive_ = false;
                    changed |= osd_.update (inst, sel_[0], in.targets, live, in.nowMs, counters_);
                }
            };
            syncExpandedOscExit();
            if (! expandedOscActive_)
                pullOsc();
            const bool nativeOsc = io->nativePlayOscControls();
            if (nativeOsc)
            {
                nativeOscIo_ = io;
                if (in.ui->isMain() && osc_->syncCompactNative (*io, *in.ui))
                {
                    pullOsc();
                    changed |= osd_.update (inst, sel_[0], in.targets, live, in.nowMs, counters_);
                }
            }
            if (! env_)
                env_ = std::make_unique<EnvUi>();
            const auto syncOpenElement = [&] {
                const int element = in.ui->isMain() ? 0 : in.ui->element();
                if (element != openElement_)
                {
                    openElement_ = element;
                    // The router retains the last page's Q group until the new view publishes.
                    osc_->forgetQGroup();
                    env_->forgetQGroup();
                    if (lfo_)
                        lfo_->forgetQGroup();
                }
            };
            syncOpenElement();
            env_->update (*io, highlight_, *in.ui, in.nowMs, live);
            if (! lfo_)
                lfo_ = std::make_unique<LfoUi>();
            lfo_->update (*io, highlight_, *in.ui, in.nowMs, live);
            if (nativeEnvLfo_.take (*io, *in.ui, *env_, *lfo_))
            {
                syncOpenElement();
                env_->update (*io, highlight_, *in.ui, in.nowMs, live);
                lfo_->update (*io, highlight_, *in.ui, in.nowMs, live);
            }
            if (io->nativeExpandedPlaySupported())
            {
                nativeExpandedIo_ = io;
                auto expanded = nativeExpandedState (*in.ui, *io);
                io->publishNativeExpandedPlay (expanded);
                if (io->takeNativeExpandedPlayClose (expanded))
                {
                    in.ui->back();
                    syncOpenElement();
                    syncExpandedOscExit();
                    pullOsc();
                    env_->update (*io, highlight_, *in.ui, in.nowMs, live);
                    lfo_->update (*io, highlight_, *in.ui, in.nowMs, live);
                }
                const bool hosted = &ui::theme::L() == &ui::theme::kLook1;
                env_->syncNativePlayEditor (*io, *in.ui, highlight_, in.nowMs, live, hosted);
                lfo_->syncNativePlayEditor (*io, *in.ui, highlight_, in.nowMs, live, hosted);
                if (! in.ui->isMain() && in.ui->element() == OscUi::kElementOsc && (hosted || in.ui->top() == OscUi::kPageRoute))
                {
                    if (! expandedOsc_)
                        expandedOsc_ = std::make_unique<OscPageUi> (*osc_, osd_);
                    if (! expandedOscActive_)
                    {
                        expandedOsc_->enterPlay();
                        expandedOscActive_ = true;
                    }
                    page::Input oscInput;
                    oscInput.targets = in.targets;
                    oscInput.live = live;
                    expandedOsc_->update (*io, *in.ui, in.nowMs, oscInput);
                    sel_[0] = expandedOsc_->selection();
                }
                io->publishNativeExpandedPlay (nativeExpandedState (*in.ui, *io));
            }
            if (io->nativeModTargetsSupported())
            {
                nativeTargetsIo_ = io;
                const unsigned long uiChanges = in.ui->changes();
                env_->syncNativeTargets (*io, *in.ui, true);
                lfo_->syncNativeTargets (*io, *in.ui, true);
                if (! expandedOscActive_)
                    osc_->syncNativeModulations (*io, *in.ui, nativeui::targets::PlayOsc);
                syncOpenElement();
                if (in.ui->changes() != uiChanges)
                {
                    syncExpandedOscExit();
                    if (in.ui->isMain())
                    {
                        pullOsc();
                        env_->update (*io, highlight_, *in.ui, in.nowMs, live);
                        lfo_->update (*io, highlight_, *in.ui, in.nowMs, live);
                    }
                    if (io->nativeExpandedPlaySupported())
                    {
                        const bool hosted = &ui::theme::L() == &ui::theme::kLook1;
                        env_->syncNativePlayEditor (*io, *in.ui, highlight_, in.nowMs, live, hosted);
                        lfo_->syncNativePlayEditor (*io, *in.ui, highlight_, in.nowMs, live, hosted);
                        io->publishNativeExpandedPlay (nativeExpandedState (*in.ui, *io));
                    }
                }
            }
            nativeEnvLfo_.publish (*io, *in.ui, *env_, *lfo_);
            if (nativeOsc)
                osc_->publishCompactNative (*io, in.ui->isMain());
            publishQ (in);
            const uint32_t h = oscUiHash() ^ (env_->drawHash (*scratch_) * 16777619u) ^ (lfo_->drawHash (*scratch_) * 2246822519u);
            if (h != uiHash_)
            {
                uiHash_ = h;
                changed = true;
            }
        }
        counters_.changes += changed;
        return changed;
    }
    bool valid() const noexcept override { return osd_.valid() && have_[1] && (usePageLfo_ || have_[2]); }

    bool regionOverride (Rect& ui) const override
    {
        ui = { 0, 0, play::kRegionW, play::kRegionH };
        return true;
    }
    bool onTouch (const TouchEvent& ev, UiStack& stack, mpckit::IInstance& base, double nowMs) override
    {
        IInstanceView& io = static_cast<IInstanceView&> (base);   // every instance of SynthyBoy is an IInstanceView
        const NamesScope scope (names_);
        if (ev.kind == TouchEvent::Down && stack.isMain() && env_ && lfo_
            && nativeEnvLfo_.coversNative (*env_, *lfo_, ev.x, ev.y))
        {
            highlight_ = ev.x < 850 ? 2 : 3;
            return false;
        }
        // The compact Play selectors are native host components.  Let their
        // bound sliderWithFocusMenu instances receive both the tap and the
        // subsequent gesture; the plugin owns this row only when a full page
        // or modal is open (the skin hides the host instances at frame 24).
        if (ev.kind == TouchEvent::Down && stack.isMain() && ev.y >= play::kTabY + play::kNativeSelectorY && ev.y < play::kTabY + play::kNativeSelectorY + play::kNativeSelectorH)
            return false;
        if (ev.kind == TouchEvent::Down && stack.isMain() && ev.y >= play::kTabY && ev.y < play::kTabY + play::kTabH)   // a touch on a third makes its Q group the active one (the same rule as every page: the group that was touched)
            highlight_ = ev.x < 434 ? 1 : (ev.x < 850 ? 2 : 3);
        // the envelope's pages while its full screen is open or a finger is down on it; else the oscillator's (its third, or its full screen), else the envelope's compact view (its third)
        if (env_ != nullptr && ((! stack.isMain() && stack.element() == EnvUi::kElementEnv) || env_->fingerDown()))
            return env_->touch (ev, stack, io, nowMs);
        // the LFO's pages likewise (its full screen, its editor, its lists); its compact view is the third at the right
        if (lfo_ != nullptr && ((! stack.isMain() && stack.element() == LfoUi::kElementLfo) || lfo_->fingerDown()))
            return lfo_->touch (ev, stack, io, nowMs);
        if (expandedOscActive_ && ! stack.isMain() && stack.element() == OscUi::kElementOsc)
            return expandedOsc_->touch (ev, stack, io, nowMs);
        if (osc_ != nullptr && ! stack.isMain() && stack.element() == OscUi::kElementOsc)
            return osc_->touch (ev, stack, io, nowMs);
        if (osc_ != nullptr && osc_->touch (ev, stack, io, nowMs))
            return true;
        if (env_ != nullptr && env_->touch (ev, stack, io, nowMs))
            return true;
        return lfo_ != nullptr && lfo_->touch (ev, stack, io, nowMs);
    }
    bool onWheel (int x, int y, int notches, UiStack& stack, mpckit::IInstance& base, double nowMs) override
    {
        IInstanceView& io = static_cast<IInstanceView&> (base);
        const NamesScope scope (names_);
        if (stack.isMain() && env_ && lfo_ && nativeEnvLfo_.coversNative (*env_, *lfo_, x, y))
            return false;
        if (stack.isMain() && y >= play::kTabY + play::kNativeSelectorY && y < play::kTabY + play::kNativeSelectorY + play::kNativeSelectorH)
            return false;
        // the same routing as a touch: the envelope's / LFO's full screens while open, else the three panels in turn
        if (env_ != nullptr && ! stack.isMain() && stack.element() == EnvUi::kElementEnv)
            return env_->wheel (x, y, notches, stack, io, nowMs);
        if (lfo_ != nullptr && ! stack.isMain() && stack.element() == LfoUi::kElementLfo)
            return lfo_->wheel (x, y, notches, stack, io, nowMs);
        if (expandedOscActive_ && ! stack.isMain() && stack.element() == OscUi::kElementOsc)
            return expandedOsc_->wheel (x, y, notches, stack, io, nowMs);
        if (osc_ != nullptr && ! stack.isMain() && stack.element() == OscUi::kElementOsc)
            return osc_->wheel (x, y, notches, stack, io, nowMs);
        if (osc_ != nullptr && osc_->wheel (x, y, notches, stack, io, nowMs))
            return true;
        if (env_ != nullptr && env_->wheel (x, y, notches, stack, io, nowMs))
            return true;
        return lfo_ != nullptr && lfo_->wheel (x, y, notches, stack, io, nowMs);
    }
    void onTick (UiStack& stack, mpckit::IInstance& base, double nowMs) override
    {
        IInstanceView& io = static_cast<IInstanceView&> (base);
        const NamesScope scope (names_);
        if (expandedOscActive_) expandedOsc_->tick (nowMs, stack, io);
        else if (osc_ != nullptr) osc_->tick (nowMs, stack, io);
        if (env_ != nullptr) env_->tick (nowMs, stack, io);
        if (lfo_ != nullptr) lfo_->tick (nowMs, stack, io);
    }
    void onShown() override
    {
        qs_.forget();
        if (osc_)
            osc_->forgetQGroup();
        if (env_)
            env_->forgetQGroup();
        if (lfo_)
            lfo_->forgetQGroup();
    }
    bool fingerDown() const noexcept override
    {
        return (expandedOscActive_ ? expandedOsc_->fingerDown() : osc_ != nullptr && osc_->fingerDown())
            || (env_ != nullptr && env_->fingerDown()) || (lfo_ != nullptr && lfo_->fingerDown());
    }
    void dropGesture() noexcept override
    {
        if (expandedOscActive_) expandedOsc_->dropGesture();
        else if (osc_ != nullptr) osc_->dropGesture();
        if (env_ != nullptr) env_->dropGesture();
        if (lfo_ != nullptr) lfo_->dropGesture();
    }
    void setNativeActionPopup (int action) noexcept override
    {
        nativeEnvLfo_.setPopup (action);
        if (expandedOscActive_)
        {
            expandedOsc_->setNativeActionPopup (*nativeExpandedIo_, action);
            return;
        }
        namespace n = nativeui::playoscui;
        constexpr int fields[] { n::Route, n::First, n::Second };
        if (nativeOscIo_ != nullptr)
            nativeOscIo_->setNativePlayOscPopup (action >= 0 && action < 3 ? fields[action]
                : action == n::kNoPopup ? n::kNoPopup : n::kOtherPopup);
    }
    void onHidden() override
    {
        if (nativeOscIo_ != nullptr)
        {
            osc_->publishCompactNative (*nativeOscIo_, false, true);
            nativeOscIo_ = nullptr;
        }
        if (nativeExpandedIo_ != nullptr)
        {
            env_->hideNativePlayEditor (*nativeExpandedIo_);
            lfo_->hideNativePlayEditor (*nativeExpandedIo_);
            if (expandedOscActive_)
            {
                expandedOsc_->leavePlay (*nativeExpandedIo_);
                expandedOscActive_ = false;
            }
            nativeExpandedIo_->publishNativeExpandedPlay ({});
            nativeExpandedIo_ = nullptr;
        }
        if (nativeTargetsIo_ != nullptr)
        {
            env_->hideNativeTargets (*nativeTargetsIo_);
            lfo_->hideNativeTargets (*nativeTargetsIo_);
            osc_->hideNativeModulations (*nativeTargetsIo_);
            nativeTargetsIo_ = nullptr;
        }
        nativeEnvLfo_.leave();
        dropGesture();
        if (osc_ != nullptr) osc_->closePopups();
        if (env_ != nullptr) env_->closePopups();
        if (lfo_ != nullptr) lfo_->closePopups();
        qs_.forget();
        if (osc_ != nullptr) osc_->forgetQGroup();
        if (env_ != nullptr) env_->forgetQGroup();
        if (lfo_ != nullptr) lfo_->forgetQGroup();
        openElement_ = 0;
    }

    uint32_t oscUiHash()
    {
        gpu::SceneBuilder sb (*scratch_);
        sb.begin (gpu::kKindPlay, 0, play::kRegionW, play::kRegionH);
        if (expandedOscActive_)
            expandedOsc_->draw (sb);
        else
            osc_->draw (sb);
        return page::sceneHash (*scratch_) ^ static_cast<uint32_t> (osc_->displayRect().x * 31 + osc_->displayRect().w);
    }
    void render (uint32_t* cv, int w, int h) override
    {
        // the CPU draws the same scene the GPU helper gets (scene_cpu.h is the reference of its meaning): transparent where nothing is drawn
        const NamesScope scope (names_);
        if (! scratch_)
            scratch_ = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*scratch_);
        sb.begin (gpu::kKindPlay, 0, w, h);
        build (sb);
        std::vector<uint32_t> pixels;
        renderScene (*scratch_, pixels);
        std::memcpy (cv, pixels.data(), std::min (pixels.size(), static_cast<size_t> (w) * static_cast<size_t> (h)) * sizeof (uint32_t));
    }
    bool describe (gpu::SceneBuilder& sb, int viewIndex, int w, int h) const override
    {
        if (! valid() || w != kW || h != kH)
            return false;
        const NamesScope scope (names_);
        sb.begin (gpu::kKindPlay, static_cast<uint32_t> (viewIndex), w, h);
        build (sb);
        return ! sb.overflow;
    }
    uint32_t hash() const noexcept override
    {
        uint32_t h = 2166136261u;
        for (int p = 0; p < play::kPanels; ++p)
            h = fnv (h, sel_[p]);
        for (float v : osd_.cycle())
            h = fnv (h, static_cast<int32_t> (std::lround (static_cast<double> (v) * 1000.0)));
        for (float v : envY_)
            h = fnv (h, static_cast<int32_t> (std::lround (static_cast<double> (v) * 1000.0)));
        return mixLfoData (h);
    }
    /** What is drawn for one panel only (the status file and the tests: "the panel follows its selector"). */
    uint32_t panelHash (int p) const noexcept
    {
        uint32_t h = fnv (2166136261u, sel_[p]);
        if (p == 2)
            return mixLfoData (h);
        for (float x : p == 0 ? osd_.cycle() : envY_)
            h = fnv (h, static_cast<int32_t> (std::lround (static_cast<double> (x) * 1000.0)));
        return h;
    }
    int selection (int p) const noexcept { return sel_[p]; }
    int highlight() const noexcept { return highlight_; }
    static constexpr int kGroups = 4;   // oscillator, envelope, LFO, and the extra group (extraTargets)
    /** The four groups of the page in reading order (oscillator, envelope, LFO, extra), mapped to the sixteen Q slots together while all three thirds show their compact views: the active third's group on Q 1 .. 4, the next ones on 5 .. 8,
        9 .. 12 and 13 .. 16 (wrapping); on the 16 knob device the order never changes (QRouter::wide). While a full screen is open its own page publishes, and this view publishes again when it closes. */
    void publishQ (const ViewInput& in)
    {
        IInstanceView* const io = ioOf (in);
        if (io == nullptr || osc_ == nullptr || env_ == nullptr || lfo_ == nullptr)
            return;
        if (! (in.ui->isMain() && osc_->isMain() && env_->isMain() && lfo_->isMain()))
        {
            qs_.forget();
            return;
        }
        const int active = highlight_ - 1;
        osc_->setCompactBlock (QRouter::blockOfGroup (kGroups, active, 0));
        env_->setCompactBlock (QRouter::blockOfGroup (kGroups, active, 1));
        lfo_->setCompactBlock (QRouter::blockOfGroup (kGroups, active, 2));
        qs_.publish (*io, kGroups, active, [&] (int g, int* t)
        {
            if (g == 0) osc_->compactTargets (t); else if (g == 1) env_->compactTargets (t); else if (g == 2) lfo_->compactTargets (t); else extraTargets (t);
        });
    }
    /** The fourth group of the page, on Q 13 .. 16 of the XL and the bank 4 of the Key 61 (no chips: its controls are the compact views' own): [LFO SYNC, OSC POWER, LFO MONO, -] of the LFO and the oscillator that are shown. All
        three are STEPPED targets: a Q-Link steps them relatively, one step per 6 clicks (QRouter::hostWrite), so a single click cannot switch an oscillator off. The MONO member exists only for a standard LFO (a random LFO has no mono parameter: the slot stays empty). A fourth member (a selector) is not published: the open question is
        which of the three selectors it is (docs/ui-map/q-link-audit.md "Feasibility"). */
    void extraTargets (int* t) const
    {
        t[0] = t[1] = t[2] = t[3] = -1;
        const int osc = osc_->selection(), lfo = lfo_->selection();
        const std::string oscKey = osc == playosc::kSub ? "sub.on" : (osc == playosc::kNoise ? "noise.on" : "osc" + std::to_string (playosc::oscNumber (osc)) + ".on");
        const std::string lfoPre = (lfo_->random() ? "rlfo" : "lfo") + std::to_string (lfo) + ".";
        t[0] = uibind::indexOfKey (lfoPre + "sync");
        t[1] = uibind::indexOfKey (oscKey);
        // MONO exists for the standard LFOs only: the random LFOs have no `rlfoN.mono` parameter (the table has none), so for a random LFO the member is EMPTY by design (hidden: the group is [LFO SYNC, OSC POWER, -, -])
        if (! lfo_->random())
            t[2] = uibind::indexOfKey (lfoPre + "mono");
    }
    const int* qTargets() const noexcept { return qs_.data(); }
    void writeStatus (std::ostream& o) const override
    {
        char hash[16];
        o << "play_view: " << name_ << "\nplay_highlight: " << highlight_ << "\nplay_osc: " << sel_[0] << "\nplay_env: " << sel_[1] << "\nplay_lfo: " << sel_[2] << "\n";
        const char* names[play::kPanels] = { "osc", "env", "lfo" };
        for (int p = 0; p < play::kPanels; ++p)
        {
            std::snprintf (hash, sizeof hash, "%08x", panelHash (p));
            o << "play_" << names[p] << "_hash: " << hash << "\n";
        }
        o << "play_osc_kind: " << (osd_.kind() == synthyboy::OscCycle::Kind::wavetable ? "wavetable" : (osd_.kind() == synthyboy::OscCycle::Kind::off ? "off" : "other")) << "\nplay_env_dot_x: "
          << (envDotPx_ < 0 ? -1.0 : envDotX_) << "\nplay_lfo_cursor_x: " << statusLfoCursor() << "\nplay_recomputes_total: " << counters_.recomputes << "\n";
        if (osc_ != nullptr)
        {
            o << "osc_ui_instance: " << osc_->selection() << "\nosc_ui_type: " << osc_->typeRaw() << "\nosc_ui_page: " << osc_->topPage() << "\nosc_ui_q_group: " << osc_->qGroupName() << "\nosc_ui_q_targets:";
            for (int k = 0; k < 4; ++k)
                o << " " << (osc_->qTargets()[k] >= 0 ? synthyboy::paramTable()[QRouter::indexOf (osc_->qTargets()[k])].key : "-");
            o << "\n";
        }
        if (env_ != nullptr)
        {
            o << "env_ui_instance: " << env_->selection() << "\nenv_ui_page: " << env_->topPage() << "\nenv_ui_q_group: " << env_->qGroupName() << "\nenv_ui_q_targets:";
            for (int k = 0; k < 4; ++k)
                o << " " << (env_->qTargets()[k] >= 0 ? synthyboy::paramTable()[QRouter::indexOf (env_->qTargets()[k])].key : "-");
            o << "\n";
        }
        if (lfo_ != nullptr)
        {
            o << "lfo_ui_instance: " << (lfo_->random() ? "RND " : "LFO ") << lfo_->selection() << "\nlfo_ui_page: " << lfo_->topPage() << "\nlfo_ui_q_group: " << lfo_->qGroupName() << "\nlfo_ui_q_targets:";
            for (int k = 0; k < 4; ++k)
            {
                const int t = lfo_->qTargets()[k];
                if (t < 0)
                    o << " -";
                else if (QRouter::modeOf (t) == QRouter::kVirtual)
                    o << " virtual" << QRouter::indexOf (t);   // the shape editor's values: not a parameter
                else
                    o << " " << synthyboy::paramTable()[QRouter::indexOf (t)].key;
            }
            o << "\n";
        }
    }
    void writeDump (std::ostream& o) const override
    {
        o << "# view " << name_ << ": highlight " << highlight_ << ", osc " << sel_[0] << " env " << sel_[1] << " lfo " << sel_[2] << "\n# osc cycle\n";
        for (size_t i = 0; i < osd_.cycle().size(); ++i)
            o << i << " " << osd_.cycle()[i] << "\n";
        o << "# env curve\n";
        for (size_t i = 0; i < envY_.size(); ++i)
            o << i << " " << envY_[i] << "\n";
        const auto* d = pageLfoData();
        const bool random = d != nullptr && d->kind == ui::lfodisplay::Data::Kind::Random;
        const float* points = d == nullptr ? lfoY_.data() : (random ? d->trace : d->y);
        const int count = d == nullptr ? static_cast<int> (lfoY_.size()) : (random ? d->traceN : d->n);
        o << (random ? "# rnd trace\n" : "# lfo shape\n");
        if (points != nullptr)
            for (int i = 0; i < count; ++i)
                o << i << " " << points[i] << "\n";
    }

private:
    const ui::lfodisplay::Data* pageLfoData() const noexcept
    {
        return usePageLfo_ && lfo_ ? &lfo_->displayData() : nullptr;
    }
    uint32_t mixLfoData (uint32_t h) const noexcept
    {
        const auto* d = pageLfoData();
        if (d != nullptr && d->kind == ui::lfodisplay::Data::Kind::Random)
            return fnv (fnv (h, lfo_->selection()), d->style);
        const float* points = d == nullptr ? lfoY_.data() : d->y;
        const int count = d == nullptr ? static_cast<int> (lfoY_.size()) : d->n;
        if (points != nullptr)
            for (int i = 0; i < count; ++i)
                h = fnv (h, static_cast<int32_t> (std::lround (static_cast<double> (points[i]) * 100.0)));
        return h;
    }
    float statusLfoCursor() const noexcept
    {
        const auto* d = pageLfoData();
        if (d != nullptr)
            return d->numCursors > 0 ? d->cx[0] : -1.0f;
        return lfoCursorPx_ < 0 ? -1.0f : lfoCursorX_;
    }

    static int panelX (int p) noexcept { return p * (play::kPanelW + play::kPanelGap); }
    static double plotY (double v01, int h, double margin) noexcept   // v01 in 0 .. 1 (1 = top) to a canvas row of the plot area
    {
        const double top = play::kPlotTop + margin, bottom = h - play::kPlotBottomMargin - margin;
        return top + (1.0 - v01) * (bottom - top);
    }

    /** The whole picture of the view as a scene: the Env and LFO panels, then the overlay oscillator UI with the cycle picture in its display rectangle. */
    void build (gpu::SceneBuilder& sb) const
    {
        const ui::theme::Look& T = ui::theme::L();
        sb.clear (0u);   // transparent: the GPU helper keeps the last picture until a Clear op, and the host's own widgets of the Env / LFO thirds must show through
        if (edgeX_ < play::kRegionW)   // the host draws over the right part of the tab (its Q-Link panel): nothing of ours right of its left edge, the picture is the host's there
            sb.clip (0.0f, 0.0f, static_cast<float> (edgeX_), static_cast<float> (play::kRegionH));
        const bool compact = osc_ != nullptr && osc_->isMain();
        const bool oscOff = compact && ! osc_->powerOn();
        const float selectorTop = static_cast<float> (play::kTabY + play::kNativeSelectorY);
        const float selectorBottom = selectorTop + static_cast<float> (play::kNativeSelectorH);
        auto ground = [&] (float x0, float y0, float x1, float y1)   // the opaque ground of the look under the plugin's own pages (a gradient fill: no distance field): a cached cell
        {
            if (x1 <= x0 || y1 <= y0)
                return;
            ui::Cell cell (sb, { static_cast<int> (x0), static_cast<int> (y0), static_cast<int> (x1 - x0), static_cast<int> (y1 - y0) }, 0);
            const uint32_t top = ui::theme::rgba (T.bg0), bottom = ui::theme::rgba (T.bg1);
            sb.gradientRect (x0, y0, x1, y1, oscOff ? ui::Pen::inactiveGrey (top) : top, oscOff ? ui::Pen::inactiveGrey (bottom) : bottom);
        };
        auto compactGround = [&] (float x0, float x1)
        {
            if (! compact)
            {
                ground (x0, static_cast<float> (play::kTabY), x1, static_cast<float> (play::kTabY + play::kTabH));
                return;
            }
            if (! osc_->compactNativeHeader()[0].empty())
            {
                // The native skin owns the complete compact background, including
                // the transparent padding of its rounded controls.
                return;
            }
            // Keep the native header row transparent without redrawing any
            // component or live curve a second time.
            ground (x0, static_cast<float> (play::kTabY), x1, selectorTop);
            if (oscOff)
            {
                // The host selector starts at x=70 (right of the power button) and ends at x=422. Leave
                // its body visible while hiding the coloured native header gutters.
                ground (x0, selectorTop, 70.0f, selectorBottom);
                ground (422.0f, selectorTop, x1, selectorBottom);
            }
            const float dialTop = 634.0f;
            const float dialBottom = 728.0f;
            ground (x0, selectorBottom, x1, dialTop);
            ground (x0, dialTop, 14.0f, dialBottom);
            ground (214.0f, dialTop, 222.0f, dialBottom);
            ground (422.0f, dialTop, x1, dialBottom);
            ground (x0, dialBottom, x1, static_cast<float> (play::kTabY + play::kTabH));
        };
        if (env_ != nullptr && ! env_->isMain())   // the full screen of the envelope covers the whole plugin area of the page: its page, its dots, its popups
        {
            env_->drawPage (sb);
            env_->drawLive (sb);
            env_->drawPopup (sb);
            return;
        }
        if (lfo_ != nullptr && ! lfo_->isMain())   // the full screen of the LFO likewise: its page (the editor, the targets), its cursors, its popups
        {
            lfo_->drawPageGround (sb);
            lfo_->drawPage (sb);
            lfo_->drawLive (sb);
            lfo_->drawPopup (sb);
            return;
        }
        if (expandedOscActive_)
        {
            expandedOsc_->draw (sb);
            return;
        }
        if (compact)
        {
            ui::Cell markers (sb, { 0, play::kTabY, play::kRegionW, play::kNativeSelectorY }, 0);
            drawNativeMarkerCovers (sb, "play1");
        }
        if (osc_ != nullptr)
        {
            // opaque under the plugin's own pages: the whole tab for a full screen, a detail page and the popup's backdrop; the oscillator third in main (the host's panel picture, its Q-Link focus frame)
            const float w = osc_->isMain() ? 432.0f : static_cast<float> (play::kRegionW);
            compactGround (0.0f, w);
        }
        if (osc_ == nullptr || osc_->isMain())
            paintPanels (sb, false);   // the Env and LFO panels (not drawn while a full screen covers them): cached cells
        if (osc_ == nullptr)
        {
            paintPanels (sb, true);
            return;
        }
        // the order is the layers' (scene.h, CellBegin): first every cached cell that lies under the live parts (the page, the picture of the data), then what moves (the running point and the cursor of the panels,
        // the live read-outs over the picture), then the cached cells that lie over them (the full screen button of the compact view, the popups)
        const ui::Rect r = osc_->displayRect();
        osc_->drawPage (sb);
        const auto& nativeHeader = osc_->compactNativeHeader();
        const auto* apertures = compact && ! nativeHeader[0].empty() ? &nativeHeader : nullptr;
        if (! r.empty())
            osd_.draw (sb, r, false, osc_->plotArea(), apertures);
        if (osc_ == nullptr || osc_->isMain())
            paintPanels (sb, true);
        if (! r.empty())
            osd_.draw (sb, r, true, osc_->plotArea());
        if (edgeX_ < play::kRegionW)
            sb.clip (0.0f, 0.0f, static_cast<float> (edgeX_), static_cast<float> (play::kRegionH));
        else
            sb.clipOff();
        osc_->drawOnDisplay (sb);
        if (compact && osc_->selection() == playosc::kSub)
            for (const auto& cell : osc_->compactNativeCells())
                ui::clearNativeCell (sb, cell);
        if (osc_->isMain() && env_ && lfo_)
            nativeEnvLfo_.drawApertures (sb, *env_, *lfo_);
    }

    /** The Env and LFO panels in the language of the look (ui/display.h drawModule): a block each, the curve as a line of light, the running dot / cursor. */
    void paintPanels (gpu::SceneBuilder& sb, bool live) const
    {
        using namespace play;
        float v[kEnvPoints > LfoView::kPoints ? kEnvPoints : LfoView::kPoints];
        for (int p = 1; p < kPanels; ++p)   // the oscillator panel is the overlay compact view now (osc_ui.h)
        {
            if (p == 1 && env_ != nullptr)   // so is the envelope (env_ui.h): its own ground, the graph, the knobs; the running dots live
            {
                if (live)
                    env_->drawLive (sb);
                else
                    env_->drawCompact (sb, true);
                continue;
            }
            if (p == 2 && usePageLfo_)   // and the LFO (lfo_ui.h): the display with the cursors of the voices, the Q group RATE
            {
                if (live)
                    lfo_->drawLive (sb);
                else
                    lfo_->drawCompact (sb, true);
                continue;
            }
            ui::display::Module m;
            const int x0 = kOx + panelX (p), y0 = kOy + kTabY;
            m.rect = { x0, y0, kPanelW, kPanelH };
            const int top = y0 + static_cast<int> (std::lround (plotY (1.0, kPanelH, 6.0))), bottom = y0 + static_cast<int> (std::lround (plotY (0.0, kPanelH, 6.0)));
            m.plot = { x0 + kPlotX, top, kPlotW, bottom - top };
            m.env = p == 1;
            m.highlighted = highlight_ == p + 1;
            const std::string tt = title (p, sel_[p]);
            m.title = tt.c_str();
            m.n = p == 1 ? kEnvPoints : LfoView::kPoints;
            for (int i = 0; i < m.n; ++i)
                v[i] = p == 1 ? envY_[static_cast<size_t> (i)] : 0.5f + 0.5f * lfoY_[static_cast<size_t> (i)];
            m.v = v;
            m.dot = p == 1 ? (envDotPx_ >= 0 ? static_cast<float> (envDotX_) : -1.0f) : (lfoCursorPx_ >= 0 ? lfoCursorX_ : -1.0f);
            if (live)
                ui::display::drawModuleLive (sb, m);
            else
                ui::display::drawModule (sb, m);
        }
    }

    OscDisplay osd_;   // the data and the drawing of the oscillator's display (shared with the OSC tab's pages)
    int highlight_;
    std::string name_;
    QSlots qs_;   // what the page published of its sixteen Q slots (the three groups together)
    int sel_[play::kPanels] = { 0, 0, 0 };
    std::unique_ptr<ParamGroup> group_[play::kPanels];
    ComputeBudget budget_[play::kPanels];
    bool have_[play::kPanels] = { false, false, false };
    bool forced_[play::kPanels] = { true, true, true };
    std::shared_ptr<const synthyboy::StateContent> content_;
    MacroNames names_;   // the macro names of the instance on screen, loaded at every update
    const synthyboy::IContentStore* store_ = nullptr;
    double bpm_ = 0.0;
    double envBpm_ = 0.0;
    std::vector<float> envY_, lfoY_;
    std::unique_ptr<OscUi> osc_;
    nativeui::expandedplay::State nativeExpandedState (const UiStack& stack, const IInstanceView& inst) const noexcept
    {
        namespace n = nativeui::expandedplay;
        if (stack.isMain() || (&ui::theme::L() != &ui::theme::kLook1
            && !(stack.element() == OscUi::kElementOsc && stack.top() == OscUi::kPageRoute)))
            return {};
        if (stack.element() == OscUi::kElementOsc)
            return {stack.depth() == 1 ? n::OscRoot : n::OscDetail, std::clamp (inst.playSelection (0), 1, playosc::kInstances), inst.stateGeneration()};
        if (stack.element() == EnvUi::kElementEnv)
            return {stack.depth() == 1 ? n::EnvRoot : n::EnvDetail, uibind::selectedEnv (inst), inst.stateGeneration()};
        if (stack.element() == LfoUi::kElementLfo)
            return {stack.depth() == 1 ? n::LfoRoot : n::LfoDetail, uibind::selectedLfo (inst), inst.stateGeneration()};
        return {};
    }

    IInstanceView* nativeExpandedIo_ = nullptr;
    std::unique_ptr<OscPageUi> expandedOsc_;
    bool expandedOscActive_ = false;
    IInstanceView* nativeOscIo_ = nullptr;
    IInstanceView* nativeTargetsIo_ = nullptr;
    PlayEnvLfo nativeEnvLfo_;
    std::unique_ptr<EnvUi> env_;
    std::unique_ptr<LfoUi> lfo_;
    bool usePageLfo_ = false;
    UiStack idleStack_;      // an empty stack: what the oscillator UI reads while another element's page is open
    int openElement_ = 0;    // the element whose full screen is open (0: none)
    std::unique_ptr<gpu::SceneData> scratch_;
    uint32_t uiHash_ = 0;
    int edgeX_ = 1280;   // the key (ViewInput::panelEdgeX)
    float lastSources_[kSourceSlots] = {};
    synthyboy::EnvDisplay::Geometry envGeom_;
    int envDotPx_ = -1, lfoCursorPx_ = -1;
    double envDotX_ = 0.0;
    float lfoCursorX_ = -1.0f;
};

}} // namespace synthyboy_mpc::overlay
