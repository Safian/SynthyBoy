// SynthyBoy - MPC overlay: the PLAY view, one composite picture of three panels side by side (docs/MPC.md "Play page"): the oscillator cycle, the envelope curve with the live dot and the
// LFO shape with the live cursor of the instances the three plugin parameters "Play Osc", "Play Env" and "Play LFO" select (IInstanceView::playSelection). The same picture is on the three
// sub-tabs of the Play function key; the panel of the sub-tab that is on screen (the view's `index`: 1 oscillator, 2 envelope, 3 LFO) has a frame. Every panel has a small title with the
// selected instance ("OSC 2", "ENV 3", "LFO 10"). The picture is drawn once through a sink (CpuSink: the canvas, SceneSink: the GPU helper's display list), so the two renderers
// cannot drift apart.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>

#include "../play_params.h"
#include "../ui/display.h"
#include "env_ui.h"
#include "lfo_ui.h"
#include "osc_display.h"
#include "osc_ui.h"
#include "page_detector.h"
#include "play_osc.h"
#include "scene_cpu.h"
#include "synthyboy/env_display.h"
#include "views.h"

namespace synthyboy_mpc { namespace overlay {

namespace play
{
constexpr int kPanels = 3;
constexpr int kOscs = 3, kEnvs = 6, kLfos = 10;                 // the instances of the own engine (LiveValues::kNumEnvs / kNumLfos, three wavetable oscillators)
constexpr int kOscSelections = playosc::kInstances;            // the oscillator selector: Sub, A, B, C, Noise (play_osc.h); the picture exists for A, B, C only
constexpr int kInstancesOf[kPanels] = { kOscSelections, kEnvs, kLfos };
constexpr int kPanelW = 400, kPanelGap = 17;                    // 3 * 400 + 2 * 17 = 1234 px: the region of the view
constexpr float kTitleSize = 24.0f;                              // pixels per em of the titles
constexpr int kPlotX = 10, kPlotTop = 40, kPlotBottomMargin = 8, kPlotW = 380;
constexpr int kEnvPoints = 256;
constexpr int kOx = 23, kOy = 14, kPanelH = 190;                // the Env and LFO panels as they always were (tab coordinates of the old region)
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

    /** `highlight`: 1 oscillator panel, 2 envelope panel, 3 LFO panel (the sub-tab this view belongs to). */
    PlayView (int highlight, double fixedCostMs) : osd_ (fixedCostMs), highlight_ (highlight), name_ ("play" + std::to_string (highlight))
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
        const int want[play::kPanels] = { std::clamp (in.instance->playSelection (0), 1, play::kOscSelections), std::clamp (in.instance->playSelection (1), 1, play::kEnvs),
                                           std::clamp (in.instance->playSelection (2), 1, play::kLfos) };
        std::shared_ptr<const synthyboy::StateContent> content = in.instance->content();
        const synthyboy::IContentStore* store = in.instance->contentStore();
        const double bpm = std::round (in.instance->bpm() * 10.0) / 10.0;
        const bool newContent = content != content_ || store != store_;
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
        // ---- oscillator: the picture of the display rectangle of the plugin-drawn oscillator UI (osc_display.h: the cycle of a wavetable, the overview of a sample, multisample or granular oscillator, the
        // spectrum of a spectral one, the noise colour, the sub oscillator's waveform, the live read-outs)
        changed = osd_.update (*in.instance, sel_[0], in.targets, in.live, in.nowMs, counters_) || changed;
        // ---- envelope: the curve (cheap) and the dot
        {
            const bool data = group_[1]->read (*in.instance) || bpm != bpm_;
            if (data || ! have_[1])
            {
                synthyboy::EnvDisplay::Request req;
                req.env = sel_[1];
                req.bpm = bpm;
                synthyboy::EnvDisplay::compute (group_[1]->values(), req, envGeom_, envY_.data(), play::kEnvPoints);
                group_[1]->accept();
                have_[1] = true;
                forced_[1] = false;
                changed = true;
            }
            int px = -1;
            double dx = 0.0, dl = 0.0;
            if (in.live != nullptr && synthyboy::EnvDisplay::dotFor (envGeom_, envY_.data(), play::kEnvPoints, in.live->envFineStage[sel_[1] - 1], in.live->envFineFraction[sel_[1] - 1], dx, dl))
                px = static_cast<int> (std::lround (dx * (play::kPlotW - 1)));
            if (px != envDotPx_)
            {
                envDotPx_ = px;
                envDotX_ = dx;
                changed = true;
            }
        }
        // ---- LFO: the shape and the cursor
        {
            float sources[kSourceSlots] = {};
            bool sourcesChanged = false;
            if (static_cast<int> (group_[2]->value ((std::string ("lfo") + std::to_string (sel_[2]) + ".shape").c_str()) + 0.5f) == 12 && in.live != nullptr)
            {
                for (int id = 1; id < kSourceSlots && id - 1 < synthyboy::LiveValues::kMaxSources; ++id)
                {
                    const float v = in.live->source[id - 1];
                    const bool bipolar = (id >= 7 && id <= 14) || (id >= 15 && id <= 18) || id == 33 || id == 34;
                    sources[id] = bipolar ? 0.5f + 0.5f * v : v;
                }
                sourcesChanged = std::memcmp (sources, lastSources_, sizeof sources) != 0;
            }
            const bool data = group_[2]->read (*in.instance) || newContent || bpm != bpm_;
            if ((data || sourcesChanged || ! have_[2]) && (forced_[2] || ! have_[2] || budget_[2].due (in.nowMs)))
            {
                const auto t0 = std::chrono::steady_clock::now();
                synthyboy::LfoDisplay::Request req;
                req.lfo = sel_[2];
                req.bpm = bpm;
                req.sources = in.live != nullptr ? sources : nullptr;
                synthyboy::LfoDisplay::compute (group_[2]->values(), content.get(), req, lfoY_.data(), LfoView::kPoints);
                budget_[2].spent (in.nowMs, msSince (t0));
                group_[2]->accept();
                std::memcpy (lastSources_, sources, sizeof sources);
                have_[2] = true;
                forced_[2] = false;
                changed = true;
            }
            const float x = in.live != nullptr ? synthyboy::LfoDisplay::cursorX (in.live->lfoPhase[sel_[2] - 1]) : -1.0f;
            const int px = x < 0.0f ? -1 : static_cast<int> (std::lround (x * (play::kPlotW - 1)));
            if (px != lfoCursorPx_)
            {
                lfoCursorPx_ = px;
                lfoCursorX_ = x;
                changed = true;
            }
        }
        content_ = content;
        store_ = store;
        bpm_ = bpm;
        if (in.panelEdgeX != edgeX_)
        {
            edgeX_ = in.panelEdgeX;
            changed = true;
        }
        // ---- the plugin-drawn oscillator pages: they read their parameters and the Q group; what they draw changes the picture
        if (in.io != nullptr && in.ui != nullptr)
        {
            if (! osc_)
            {
                osc_ = std::make_unique<OscUi>();
                osc_->setCanvas (play::kRegionW, play::kRegionH);
                scratch_ = std::make_unique<gpu::SceneData>();
            }
            // the stack of the instance is shared by the elements: a page that belongs to another element is not the oscillator's (it sees an empty stack: its compact view)
            osc_->update (*in.io, highlight_, in.ui->isMain() || in.ui->element() == OscUi::kElementOsc ? *in.ui : idleStack_, in.nowMs, sel_[1], sel_[2]);
            if (! env_)
                env_ = std::make_unique<EnvUi>();
            const int openElement = in.ui->isMain() ? 0 : in.ui->element();
            if (openElement != openElement_)
            {
                openElement_ = openElement;   // a full screen opened or closed: the elements publish their Q group again (the router keeps what the last page set)
                osc_->forgetQGroup();
                env_->forgetQGroup();
                if (lfo_)
                    lfo_->forgetQGroup();
            }
            env_->update (*in.io, highlight_, *in.ui, in.nowMs, in.live);
            if (! lfo_)
                lfo_ = std::make_unique<LfoUi>();
            lfo_->update (*in.io, highlight_, *in.ui, in.nowMs, in.live);
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
    bool valid() const noexcept override { return osd_.valid() && have_[1] && have_[2]; }

    bool regionOverride (Rect& ui) const override
    {
        ui = { 0, 0, play::kRegionW, play::kRegionH };
        return true;
    }
    bool onTouch (const TouchEvent& ev, UiStack& stack, IInstanceView& io, double nowMs) override
    {
        // the envelope's pages while its full screen is open or a finger is down on it; else the oscillator's (its third, or its full screen), else the envelope's compact view (its third)
        if (env_ != nullptr && ((! stack.isMain() && stack.element() == EnvUi::kElementEnv) || env_->fingerDown()))
            return env_->touch (ev, stack, io, nowMs);
        // the LFO's pages likewise (its full screen, its editor, its lists); its compact view is the third at the right
        if (lfo_ != nullptr && ((! stack.isMain() && stack.element() == LfoUi::kElementLfo) || lfo_->fingerDown()))
            return lfo_->touch (ev, stack, io, nowMs);
        if (osc_ != nullptr && osc_->touch (ev, stack, io, nowMs))
            return true;
        if (env_ != nullptr && env_->touch (ev, stack, io, nowMs))
            return true;
        return lfo_ != nullptr && lfo_->touch (ev, stack, io, nowMs);
    }
    void onTick (UiStack& stack, IInstanceView& io, double nowMs) override { if (osc_ != nullptr) osc_->tick (nowMs, stack, io); if (env_ != nullptr) env_->tick (nowMs, stack, io); if (lfo_ != nullptr) lfo_->tick (nowMs, stack, io); }
    void onShown() override
    {
        if (osc_)
            osc_->forgetQGroup();
        if (env_)
            env_->forgetQGroup();
        if (lfo_)
            lfo_->forgetQGroup();
    }
    bool fingerDown() const noexcept override { return (osc_ != nullptr && osc_->fingerDown()) || (env_ != nullptr && env_->fingerDown()) || (lfo_ != nullptr && lfo_->fingerDown()); }
    void dropGesture() noexcept override { if (osc_ != nullptr) osc_->dropGesture(); if (env_ != nullptr) env_->dropGesture(); if (lfo_ != nullptr) lfo_->dropGesture(); }
    OscUi* oscUi() noexcept { return osc_.get(); }

    uint32_t oscUiHash()
    {
        gpu::SceneBuilder sb (*scratch_);
        sb.begin (gpu::kKindPlay, 0, play::kRegionW, play::kRegionH);
        osc_->draw (sb);
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < scratch_->numOps; ++i)
        {
            const gpu::SceneOp& o = scratch_->ops[i];
            h = fnv (h, static_cast<int32_t> (o.op));
            h = fnv (h, static_cast<int32_t> (o.rgba));
            h = fnv (h, static_cast<int32_t> (o.rgba2));
            for (float f : o.f)
            {
                int32_t bits;
                std::memcpy (&bits, &f, 4);   // the bits: a text op keeps its characters in two of them
                h = fnv (h, bits);
            }
        }
        return h ^ static_cast<uint32_t> (osc_->displayRect().x * 31 + osc_->displayRect().w);
    }
    void render (uint32_t* cv, int w, int h) override
    {
        // the CPU draws the same scene the GPU helper gets (scene_cpu.h is the reference of its meaning): transparent where nothing is drawn
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
        for (float v : lfoY_)
            h = fnv (h, static_cast<int32_t> (std::lround (static_cast<double> (v) * 100.0)));
        return h;
    }
    /** What is drawn for one panel only (the status file and the tests: "the panel follows its selector"). */
    uint32_t panelHash (int p) const noexcept
    {
        uint32_t h = fnv (2166136261u, sel_[p]);
        const std::vector<float>& v = p == 0 ? osd_.cycle() : (p == 1 ? envY_ : lfoY_);
        const double scale = p == 0 ? 1000.0 : (p == 1 ? 1000.0 : 100.0);
        for (float x : v)
            h = fnv (h, static_cast<int32_t> (std::lround (static_cast<double> (x) * scale)));
        return h;
    }
    int selection (int p) const noexcept { return sel_[p]; }
    int highlight() const noexcept { return highlight_; }
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
          << (envDotPx_ < 0 ? -1.0 : envDotX_) << "\nplay_lfo_cursor_x: " << (lfoCursorPx_ < 0 ? -1.0f : lfoCursorX_) << "\nplay_recomputes_total: " << counters_.recomputes << "\n";
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
        o << "# lfo shape\n";
        for (size_t i = 0; i < lfoY_.size(); ++i)
            o << i << " " << lfoY_[i] << "\n";
    }

private:
    static int panelX (int p) noexcept { return p * (play::kPanelW + play::kPanelGap); }
    static double plotY (double v01, int h, double margin) noexcept   // v01 in 0 .. 1 (1 = top) to a canvas row of the plot area
    {
        const double top = play::kPlotTop + margin, bottom = h - play::kPlotBottomMargin - margin;
        return top + (1.0 - v01) * (bottom - top);
    }

    /** The whole picture of the view as a scene: the Env and LFO panels, then the plugin-drawn oscillator UI with the cycle picture in its display rectangle. */
    void build (gpu::SceneBuilder& sb) const
    {
        const ui::theme::Look& T = ui::theme::L();
        sb.clear (0u);   // transparent: the GPU helper keeps the last picture until a Clear op, and the host's own widgets of the Env / LFO thirds must show through
        if (edgeX_ < play::kRegionW)   // the host draws over the right part of the tab (its Q-Link panel): nothing of ours right of its left edge, the picture is the host's there
            sb.clip (0.0f, 0.0f, static_cast<float> (edgeX_), static_cast<float> (play::kRegionH));
        auto ground = [&] (float x0, float y0, float x1, float y1)   // the opaque ground of the look under the plugin's own pages (a gradient fill: no distance field): a cached cell
        {
            ui::Cell cell (sb, { static_cast<int> (x0), static_cast<int> (y0), static_cast<int> (x1 - x0), static_cast<int> (y1 - y0) }, 0);
            sb.gradientRect (x0, y0, x1, y1, ui::theme::rgba (T.bg0), ui::theme::rgba (T.bg1));
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
            lfo_->drawPage (sb);
            lfo_->drawLive (sb);
            lfo_->drawPopup (sb);
            return;
        }
        if (osc_ != nullptr)
        {
            // opaque under the plugin's own pages: the whole tab for a full screen, a detail page and the popup's backdrop; the oscillator third in main (the host's panel picture, its Q-Link focus frame)
            const float w = osc_->isMain() ? 432.0f : static_cast<float> (play::kRegionW);
            ground (0.0f, static_cast<float> (play::kTabY), w, static_cast<float> (play::kTabY + play::kTabH));
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
        osc_->drawPage (sb);
        const ui::Rect r = osc_->displayRect();
        if (! r.empty())
            osd_.draw (sb, r, false);
        if (osc_ == nullptr || osc_->isMain())
            paintPanels (sb, true);
        if (! r.empty())
            osd_.draw (sb, r, true);
        osc_->drawOnDisplay (sb);
        osc_->drawPopup (sb);   // the popups lie over the picture
    }

    /** The Env and LFO panels in the language of the look (ui/display.h drawModule): a block each, the curve as a line of light, the running dot / cursor. */
    void paintPanels (gpu::SceneBuilder& sb, bool live) const
    {
        using namespace play;
        float v[kEnvPoints > LfoView::kPoints ? kEnvPoints : LfoView::kPoints];
        for (int p = 1; p < kPanels; ++p)   // the oscillator panel is the plugin-drawn compact view now (osc_ui.h)
        {
            if (p == 1 && env_ != nullptr)   // so is the envelope (env_ui.h): its own ground, the graph, the knobs; the running dots live
            {
                if (live)
                    env_->drawLive (sb);
                else
                    env_->drawCompact (sb);
                continue;
            }
            if (p == 2 && lfo_ != nullptr)   // and the LFO (lfo_ui.h): the display with the cursors of the voices, the Q group RATE
            {
                if (live)
                    lfo_->drawLive (sb);
                else
                    lfo_->drawCompact (sb);
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
    int sel_[play::kPanels] = { 0, 0, 0 };
    std::unique_ptr<ParamGroup> group_[play::kPanels];
    ComputeBudget budget_[play::kPanels];
    bool have_[play::kPanels] = { false, false, false };
    bool forced_[play::kPanels] = { true, true, true };
    std::shared_ptr<const synthyboy::StateContent> content_;
    const synthyboy::IContentStore* store_ = nullptr;
    double bpm_ = 0.0;
    std::vector<float> envY_, lfoY_;
    std::unique_ptr<OscUi> osc_;
    std::unique_ptr<EnvUi> env_;
    std::unique_ptr<LfoUi> lfo_;
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
