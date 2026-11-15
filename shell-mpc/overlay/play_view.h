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
#include "master_ui.h"
#include "osc_ui.h"
#include "voice_ui.h"
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
    PlayView (int highlight, double fixedCostMs) : highlight_ (highlight), name_ ("play" + std::to_string (highlight))
    {
        for (ComputeBudget& b : budget_)
            b.fixedCostMs = fixedCostMs;
        cycle_.assign (synthyboy::OscCycle::kSamples, 0.0f);
        cycleMax_.assign (synthyboy::OscCycle::kSamples, 0.0f);
        envY_.assign (play::kEnvPoints, 0.0f);
        lfoY_.assign (LfoView::kPoints, 0.0f);
        for (int o = 0; o < play::kOscs; ++o)
            synthyboy::OscCycle::readoutIndices (o + 1, targetIdx_[o]);
    }

    const char* name() const noexcept override { return name_.c_str(); }
    /** All three oscillators' readouts (12 values): the selected one is used. */
    void targets (std::vector<int>& out) const override
    {
        for (int o = 0; o < play::kOscs; ++o)
            for (int i = 0; i < 4; ++i)
                out.push_back (targetIdx_[o][i]);
    }

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
                if (p == 0)
                    group_[p] = std::make_unique<ParamGroup> (sel_[0] == playosc::kSub ? std::string ("sub.") : (sel_[0] == playosc::kNoise ? std::string ("noise.") : std::string ("osc") + std::to_string (playosc::oscNumber (sel_[0])) + "."));
                else
                    group_[p] = std::make_unique<ParamGroup> (std::string (prefix[p]) + std::to_string (sel_[p]) + ".");
                forced_[p] = true;
                have_[p] = false;
                changed = true;
            }
        }
        if (! group_[0])
            return false;
        if (ui::theme::detailLevel() != detailSeen_)   // the level of detail changed: the picture of the oscillator is made again (the stack, the spectrum)
        {
            detailSeen_ = ui::theme::detailLevel();
            forced_[0] = true;
            have_[0] = false;
            changed = true;
        }
        // ---- oscillator: the picture of the display rectangle of the plugin-drawn oscillator UI (the cycle of a wavetable, the overview of a sample, multisample or granular oscillator, the spectrum of a
        // spectral one, the noise colour, the sub oscillator's waveform)
        if (playosc::oscNumber (sel_[0]) == 0 && sel_[0] == playosc::kSub)
        {
            const float wave = group_[0]->value ("sub.waveform"), phase = group_[0]->value ("sub.phase");
            const bool data = group_[0]->read (*in.instance) || ! have_[0] || forced_[0];
            if (data)
            {
                fillSub (static_cast<int> (std::lround (wave)), phase);
                stackN_ = 0;
                computeSpectrum();
                kind_ = synthyboy::OscCycle::Kind::wavetable;
                res_ = synthyboy::OscCycle::Result();
                res_.kind = kind_;
                group_[0]->accept();
                have_[0] = true;
                forced_[0] = false;
                changed = true;
                displayDataMs_ = in.nowMs;
            }
        }
        else
        {
            if (! oc_)
                oc_ = std::make_unique<synthyboy::OscCycle>();
            const int osc = sel_[0] == playosc::kNoise ? 4 : playosc::oscNumber (sel_[0]);
            float t[4] = { -1, -1, -1, -1 };
            if (in.targets != nullptr && osc <= play::kOscs)
                for (int i = 0; i < 4; ++i)
                    t[i] = std::round (in.targets[(osc - 1) * 4 + i] * 100.0f) / 100.0f;
            const bool data = group_[0]->read (*in.instance) || newContent || std::memcmp (t, lastTargets_, sizeof t) != 0;
            if ((data || ! have_[0]) && (forced_[0] || ! have_[0] || budget_[0].due (in.nowMs)))
            {
                const auto t0 = std::chrono::steady_clock::now();
                if (newContent)
                    oc_->setContent (content != nullptr ? *content : synthyboy::StateContent {}, store);
                synthyboy::OscCycle::Request req;
                req.osc = osc;
                req.bpm = bpm;
                if (in.targets != nullptr && osc <= play::kOscs)
                {
                    req.framePercent = t[0];
                    req.warpAmountPercent[0] = t[1];
                    req.warpAmountPercent[1] = t[2];
                    req.morphAmountPercent = t[3];
                }
                const bool wt = osc != 4 && group_[0]->value ((std::string ("osc") + std::to_string (osc) + ".type").c_str()) < 0.5f;
                displayPoints_ = wt ? synthyboy::OscCycle::kSamples : play::kDisplayPoints;
                res_ = oc_->computeDisplay (group_[0]->values(), req, cycle_.data(), cycleMax_.data(), displayPoints_);
                kind_ = res_.kind;
                // the wavetable's frames behind the current one (fixed positions along the table) and a faint spectrum of the cycle
                stackN_ = 0;
                if (wt && kind_ == synthyboy::OscCycle::Kind::wavetable)
                {
                    const int kStackFrames = ui::theme::D().stackFrames >= 2 ? ui::theme::D().stackFrames : 0;
                    if (kStackFrames > 0 && stack_.size() != static_cast<size_t> (kStackFrames))
                        stack_.assign (static_cast<size_t> (kStackFrames), std::vector<float> (static_cast<size_t> (synthyboy::OscCycle::kSamples), 0.0f));
                    for (int k = 0; k < kStackFrames; ++k)
                    {
                        synthyboy::OscCycle::Request rq = req;
                        rq.framePercent = 100.0f * static_cast<float> (k) / static_cast<float> (kStackFrames - 1);
                        oc_->computeDisplay (group_[0]->values(), rq, stack_[static_cast<size_t> (k)].data(), cycleMax_.data(), displayPoints_);
                    }
                    stackN_ = kStackFrames;
                }
                computeSpectrum();
                budget_[0].spent (in.nowMs, msSince (t0));
                counters_.lastComputeMs = budget_[0].costMs;
                ++counters_.recomputes;
                group_[0]->accept();
                std::memcpy (lastTargets_, t, sizeof t);
                have_[0] = true;
                forced_[0] = false;
                changed = true;
                displayDataMs_ = in.nowMs;
            }
        }
        // ---- the live read-outs of the engine: the read heads of the newest voices (sample, multisample, the scan of a granular cloud), the grains that sound, the phases of the sub, the read head of a sample noise
        {
            Live nl;
            const int o = playosc::oscNumber (sel_[0]);
            if (in.live != nullptr)
            {
                if (o >= 1 && o <= play::kOscs && (kind_ == synthyboy::OscCycle::Kind::sample || kind_ == synthyboy::OscCycle::Kind::multisample || kind_ == synthyboy::OscCycle::Kind::granular))
                {
                    for (int v = 0; v < ui::display::Data::kMaxHeads; ++v)
                        nl.heads[v] = in.live->voicePos[o - 1][v];
                    if (kind_ == synthyboy::OscCycle::Kind::granular)
                    {
                        nl.numGrains = std::min (in.live->numGrains[o - 1], static_cast<int> (ui::display::Data::kMaxGrains));
                        for (int g = 0; g < nl.numGrains; ++g)
                        {
                            nl.grainPos[g] = in.live->grainPos[o - 1][g];
                            nl.grainLife[g] = in.live->grainLife[o - 1][g];
                        }
                    }
                }
                if (sel_[0] == playosc::kSub)
                    for (int v = 0; v < ui::display::Data::kMaxHeads; ++v)
                        nl.phases[v] = in.live->subPhase[v];
                if (sel_[0] == playosc::kNoise && kind_ == synthyboy::OscCycle::Kind::noise)
                    nl.noise = in.live->noisePos;
            }
            if (nl.differs (live_))
            {
                live_ = nl;
                changed = true;
            }
        }
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
        if (in.qlinkPanel != qlinkPanel_)
        {
            qlinkPanel_ = in.qlinkPanel;
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
            osc_->update (*in.io, highlight_, *in.ui, in.nowMs, sel_[1], sel_[2]);
            if (highlight_ != 1 && in.ui->isMain())
                publishModuleGroup (*in.io);
            const uint32_t h = oscUiHash();
            if (h != uiHash_)
            {
                uiHash_ = h;
                changed = true;
            }
        }
        {
            const bool hot = in.nowMs - displayDataMs_ < 350.0;
            if (hot != displayHot_)
            {
                displayHot_ = hot;
                changed = true;   // the picture is described again (cached cell or not)
            }
        }
        counters_.changes += changed;
        return changed;
    }
    bool valid() const noexcept override { return have_[0] && have_[1] && have_[2]; }

    bool regionOverride (Rect& ui) const override
    {
        ui = { 0, 0, play::kRegionW, play::kRegionH };
        return true;
    }
    bool onTouch (const TouchEvent& ev, UiStack& stack, IInstanceView& io, double nowMs) override { return osc_ != nullptr && osc_->touch (ev, stack, io, nowMs); }
    void onTick (UiStack& stack, IInstanceView& io, double nowMs) override { if (osc_ != nullptr) osc_->tick (nowMs, stack, io); }
    bool fingerDown() const noexcept override { return osc_ != nullptr && osc_->fingerDown(); }
    void dropGesture() noexcept override { if (osc_ != nullptr) osc_->dropGesture(); }
    OscUi* oscUi() noexcept { return osc_.get(); }
    MasterUi* masterUi() noexcept { return master_.get(); }
    VoiceUi* voiceUi() noexcept { return voice_.get(); }

    /** The Q group of the Env and LFO sub-tabs: the Q-Links 1 .. 4 of those pages are Q 1 .. Q 4 (the skin), so the first four knobs of the module are the group (they were Q-Link 1 .. 4 before). */
    void publishModuleGroup (IInstanceView& io)
    {
        const int g = highlight_ == 2 ? 1 : 2;
        int t[4];
        for (int s = 0; s < 4; ++s)
            t[s] = synthyboy_mpc::play::realIndex (g, sel_[g], s);
        if (! moduleGroupSet_ || std::memcmp (t, moduleGroup_, sizeof t) != 0)
        {
            std::memcpy (moduleGroup_, t, sizeof t);
            io.setQGroup (t, 4);
            moduleGroupSet_ = true;
        }
    }
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
        for (float v : cycle_)
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
        const std::vector<float>& v = p == 0 ? cycle_ : (p == 1 ? envY_ : lfoY_);
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
        o << "play_osc_kind: " << (kind_ == synthyboy::OscCycle::Kind::wavetable ? "wavetable" : (kind_ == synthyboy::OscCycle::Kind::off ? "off" : "other")) << "\nplay_env_dot_x: "
          << (envDotPx_ < 0 ? -1.0 : envDotX_) << "\nplay_lfo_cursor_x: " << (lfoCursorPx_ < 0 ? -1.0f : lfoCursorX_) << "\nplay_recomputes_total: " << counters_.recomputes << "\n";
        if (osc_ != nullptr)
        {
            o << "osc_ui_instance: " << osc_->selection() << "\nosc_ui_type: " << osc_->typeRaw() << "\nosc_ui_page: " << osc_->topPage() << "\nosc_ui_q_group: " << osc_->qGroupName() << "\nosc_ui_q_targets:";
            for (int k = 0; k < 4; ++k)
                o << " " << (osc_->qTargets()[k] >= 0 ? synthyboy::paramTable()[QRouter::indexOf (osc_->qTargets()[k])].key : "-");
            o << "\n";
        }
    }
    void writeDump (std::ostream& o) const override
    {
        o << "# view " << name_ << ": highlight " << highlight_ << ", osc " << sel_[0] << " env " << sel_[1] << " lfo " << sel_[2] << "\n# osc cycle\n";
        for (size_t i = 0; i < cycle_.size(); ++i)
            o << i << " " << cycle_[i] << "\n";
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
        if (qlinkPanel_)   // the host's Q-Link panel is up over the right edge: nothing of ours is drawn there (it stays transparent, the panel shows as on every other page)
            sb.clip (0.0f, 0.0f, static_cast<float> (PageDetector::kPanelLineX), static_cast<float> (play::kRegionH));
        auto ground = [&] (float x0, float y0, float x1, float y1)   // the opaque ground of the look under the plugin's own pages (a gradient fill: no distance field): a cached cell
        {
            ui::Cell cell (sb, { static_cast<int> (x0), static_cast<int> (y0), static_cast<int> (x1 - x0), static_cast<int> (y1 - y0) }, 0);
            sb.gradientRect (x0, y0, x1, y1, ui::theme::rgba (T.bg0), ui::theme::rgba (T.bg1));
        };
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
            drawPicture (sb, r, false);
        if (osc_ == nullptr || osc_->isMain())
            paintPanels (sb, true);
        if (! r.empty())
            drawPicture (sb, r, true);
        osc_->drawOnDisplay (sb);
        osc_->drawPopup (sb);   // the popups lie over the picture
    }

    /** The magnitudes of the first harmonics of the cycle (0 .. 1, a square root so that the weak ones show): the faint spectrum under the wave. */
    void computeSpectrum()
    {
        const int n = std::min (displayPoints_, static_cast<int> (cycle_.size()));
        spectrum_.assign (static_cast<size_t> (ui::display::kMaxSpectrum), 0.0f);
        if (! ui::theme::D().spectrum || n < 64 || kind_ == synthyboy::OscCycle::Kind::off)
            return;
        for (int k = 1; k <= ui::display::kMaxSpectrum; ++k)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double a = 6.283185307179586 * static_cast<double> (k) * static_cast<double> (i) / static_cast<double> (n);
                re += static_cast<double> (cycle_[static_cast<size_t> (i)]) * std::cos (a);
                im -= static_cast<double> (cycle_[static_cast<size_t> (i)]) * std::sin (a);
            }
            spectrum_[static_cast<size_t> (k - 1)] = static_cast<float> (std::sqrt (std::min (1.0, std::sqrt (re * re + im * im) / (0.5 * n))));
        }
    }

    void fillSub (int wave, float phasePercent)
    {
        const int n = static_cast<int> (cycle_.size());
        for (int i = 0; i < n; ++i)
        {
            const double t = std::fmod (static_cast<double> (i) / n + static_cast<double> (phasePercent) * 0.01, 1.0);
            double v = 0.0;
            switch (wave)
            {
                case 0: v = std::sin (2.0 * 3.14159265358979 * t); break;
                case 1: v = std::tanh (3.0 * std::sin (2.0 * 3.14159265358979 * t)); break;
                case 2: v = t < 0.25 ? 4.0 * t : (t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0); break;
                case 3: v = 2.0 * t - 1.0; break;
                case 4: v = t < 0.5 ? 1.0 : -1.0; break;
                default: v = t < 0.25 ? 1.0 : -1.0; break;
            }
            cycle_[static_cast<size_t> (i)] = static_cast<float> (v);
        }
        displayPoints_ = n;
    }

    /** The picture of the oscillator in a rectangle of the canvas: what the data of OscCycle::computeDisplay is (docs/MPC.md "Oscillator UI"), drawn by ui/display.h. */
    void drawPicture (gpu::SceneBuilder& sb, const ui::Rect& r, bool live) const
    {
        using K = synthyboy::OscCycle::Kind;
        ui::display::Data d;
        d.colour = ui::theme::instanceColour (sel_[0] - 1);
        d.n = std::min (displayPoints_, static_cast<int> (cycle_.size()));
        d.cycle = cycle_.data();
        if (kind_ == K::wavetable)
        {
            d.kind = ui::display::Data::Kind::Wave;
            if (sel_[0] != playosc::kSub)
            {
                const std::string base = "osc" + std::to_string (playosc::oscNumber (sel_[0])) + ".unison.";
                d.voices = std::clamp (static_cast<int> (group_[0]->value ((base + "voices").c_str()) + 0.5f), 1, 16);
                d.detune = std::clamp (group_[0]->value ((base + "detune").c_str()) / 100.0f, 0.0f, 1.0f);
                d.blend = std::clamp (group_[0]->value ((base + "blend").c_str()) / 100.0f, 0.0f, 1.0f);
                d.pos = std::clamp (lastTargets_[0] / 100.0f, 0.0f, 1.0f);
                d.numStack = stackN_;
                for (int i = 0; i < stackN_; ++i)
                    d.stack[i] = stack_[static_cast<size_t> (i)].data();
                d.spectrum = spectrum_.data();
                d.numSpectrum = static_cast<int> (spectrum_.size());
            }
            else
                for (int v = 0; v < ui::display::Data::kMaxHeads; ++v)   // the sub oscillator: the phase of every voice on the wave
                    d.phases[v] = live_.phases[v];
        }
        else if (kind_ == K::sample || kind_ == K::multisample || kind_ == K::granular)
        {
            d.kind = ui::display::Data::Kind::MinMax;
            d.cycle = cycle_.data();
            d.cycleMax = cycleMax_.data();
            d.loopOn = res_.loopOn;
            d.loopStart = res_.loopStart;
            d.loopEnd = res_.loopEnd;
            d.grain = kind_ == K::granular && res_.grainSize > 0.0f;
            d.grainPos = res_.grainPosition;
            d.grainSize = res_.grainSize;
            for (int v = 0; v < ui::display::Data::kMaxHeads; ++v)   // live: where the newest voices read their file (the grains that sound and the scan of a cloud included)
                d.heads[v] = live_.heads[v];
            d.numGrainsLive = live_.numGrains;
            for (int g = 0; g < live_.numGrains; ++g)
            {
                d.liveGrainPos[g] = live_.grainPos[g];
                d.liveGrainLife[g] = live_.grainLife[g];
            }
        }
        else if (kind_ == K::spectral)
            d.kind = ui::display::Data::Kind::Spectrum;
        else
        {
            d.kind = ui::display::Data::Kind::Message;
            d.noiseHead = live_.noise;
            d.message = kind_ == K::noise ? "NOISE" : (kind_ == K::noSample ? "NO SAMPLE" : (kind_ == K::off ? "OFF" : ""));
        }
        if (live)
            ui::display::drawLive (sb, r, d);
        else
            ui::display::draw (sb, r, d, ! displayHot_);   // while the data changes every picture (a knob dragged along the wavetable) it is drawn with the picture, not into a layer
    }

    /** The Env and LFO panels in the language of the look (ui/display.h drawModule): a block each, the curve as a line of light, the running dot / cursor. */
    void paintPanels (gpu::SceneBuilder& sb, bool live) const
    {
        using namespace play;
        float v[kEnvPoints > LfoView::kPoints ? kEnvPoints : LfoView::kPoints];
        for (int p = 1; p < kPanels; ++p)   // the oscillator panel is the plugin-drawn compact view now (osc_ui.h)
        {
            if (qlinkPanel_ && p == 2)
                continue;   // the LFO panel lies under the host's Q-Link panel: not drawn while it is up
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

    /** The live read-outs shown in the display (a change of one pixel or of a grain's step is a change of the picture). */
    struct Live
    {
        float heads[ui::display::Data::kMaxHeads], phases[ui::display::Data::kMaxHeads], grainPos[ui::display::Data::kMaxGrains], grainLife[ui::display::Data::kMaxGrains], noise = -1.0f;
        int numGrains = 0;
        Live() { std::fill (heads, heads + ui::display::Data::kMaxHeads, -1.0f); std::fill (phases, phases + ui::display::Data::kMaxHeads, -1.0f); std::fill (grainPos, grainPos + ui::display::Data::kMaxGrains, 0.0f); std::fill (grainLife, grainLife + ui::display::Data::kMaxGrains, 0.0f); }
        static bool far (float a, float b, float tol) noexcept { return (a < 0.0f) != (b < 0.0f) || std::fabs (a - b) > tol; }
        bool differs (const Live& o) const noexcept
        {
            for (int v = 0; v < ui::display::Data::kMaxHeads; ++v)
                if (far (heads[v], o.heads[v], 0.0025f) || far (phases[v], o.phases[v], 0.0025f))
                    return true;
            if (numGrains != o.numGrains || far (noise, o.noise, 0.0025f))
                return true;
            for (int g = 0; g < numGrains; ++g)
                if (far (grainPos[g], o.grainPos[g], 0.0025f) || far (grainLife[g], o.grainLife[g], 0.04f))
                    return true;
            return false;
        }
    };
    Live live_;
    double displayDataMs_ = -1.0e9;   // when the data of the display last changed
    bool displayHot_ = false;          // it changed within the last third of a second: drawn uncached
    int highlight_;
    std::string name_;
    int sel_[play::kPanels] = { 0, 0, 0 };
    std::unique_ptr<ParamGroup> group_[play::kPanels];
    ComputeBudget budget_[play::kPanels];
    bool have_[play::kPanels] = { false, false, false };
    bool forced_[play::kPanels] = { true, true, true };
    int targetIdx_[play::kOscs][4] = {};
    std::unique_ptr<synthyboy::OscCycle> oc_;
    std::shared_ptr<const synthyboy::StateContent> content_;
    const synthyboy::IContentStore* store_ = nullptr;
    double bpm_ = 0.0;
    std::vector<float> cycle_, cycleMax_, envY_, lfoY_, spectrum_;
    std::vector<std::vector<float>> stack_;
    int stackN_ = 0, detailSeen_ = 1;
    int displayPoints_ = synthyboy::OscCycle::kSamples;
    synthyboy::OscCycle::Result res_;
    std::unique_ptr<OscUi> osc_;
    std::unique_ptr<MasterUi> master_;
    std::unique_ptr<VoiceUi> voice_;
    std::unique_ptr<gpu::SceneData> scratch_;
    uint32_t uiHash_ = 0;
    bool qlinkPanel_ = false;
    int moduleGroup_[4] = { -1, -1, -1, -1 };
    bool moduleGroupSet_ = false;
    float lastTargets_[4] = { -1, -1, -1, -1 };
    float lastSources_[kSourceSlots] = {};
    synthyboy::OscCycle::Kind kind_ = synthyboy::OscCycle::Kind::other;
    synthyboy::EnvDisplay::Geometry envGeom_;
    int envDotPx_ = -1, lfoCursorPx_ = -1;
    double envDotX_ = 0.0;
    float lfoCursorX_ = -1.0f;
};

}} // namespace synthyboy_mpc::overlay
