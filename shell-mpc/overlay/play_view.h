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
#include "osc_ui.h"
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
constexpr int kTitleCell = 3;                                   // pixels per font pixel of the titles
constexpr int kPlotX = 10, kPlotTop = 40, kPlotBottomMargin = 8, kPlotW = 380;
constexpr int kEnvPoints = 256;
constexpr int kOx = 23, kOy = 14, kPanelH = 190;                // the Env and LFO panels as they always were (tab coordinates of the old region)
constexpr int kRegionW = 1280, kRegionH = 628, kTabY = 110;      // the region of the view now: the whole tab of the Play page (the compact oscillator view, the full screen, the Env and LFO panels)
constexpr int kDisplayPoints = 256;                              // the points of a min / max overview (two polylines of this many vertices)
constexpr uint32_t kPanelRgb = 0x262527, kFrameRgb = 0xff2e63, kRestFrameRgb = 0x3d3b3e;

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

/** The drawing target of the CPU: straight 0xRRGGBBAA colours as in the scene, premultiplied on the canvas. */
struct CpuSink
{
    CpuSink (uint32_t* cv_, int w_, int h_) : cv (cv_), w (w_), h (h_) {}
    static uint32_t pm (uint32_t c) { return draw::premultiplied (static_cast<int> ((c >> 24) & 255u), static_cast<int> ((c >> 16) & 255u), static_cast<int> ((c >> 8) & 255u), static_cast<int> (c & 255u)); }
    void clear (uint32_t rgba) { draw::setRect (cv, w, h, 0, 0, w, h, pm (rgba)); }
    void rect (int x0, int y0, int x1, int y1, uint32_t c) { draw::fillRect (cv, w, h, x0, y0, x1, y1, pm (c)); }
    void vline (double x, int y0, int y1, double width, uint32_t c) { draw::vline (cv, w, h, x, y0, y1, width, pm (c)); }
    void disc (double x, double y, double r, uint32_t c) { draw::disc (cv, w, h, x, y, r, pm (c)); }
    void curve (const float* rows, int n, int x0_, int width)
    {
        x0 = x0_;
        cols = width;
        draw::sampleColumns (ys, rows, n, width, h, [] (double v) { return v; }, 4);
    }
    void fill (double baseRow, uint32_t c) { draw::fillToBaseline (cv, w, h, ys, 4, baseRow, pm (c), x0, cols); }
    void stroke (double half, uint32_t c) { draw::strokeCurve (cv, w, h, ys, 4, half, pm (c), x0, cols); }
    void text (int x, int y, int cell, const char* s, uint32_t c) { draw::text (cv, w, h, x, y, cell, s, 12, pm (c)); }
    uint32_t* cv;
    int w, h, x0 = 0, cols = 0;
    std::vector<double> ys;
};

/** Shifts everything a sink draws by (dx, dy): the Env and LFO panels keep their own coordinates (0, 0 = the top left of their old region) inside the bigger region. */
template <class Sink>
struct ShiftSink
{
    ShiftSink (Sink& s_, int dx_, int dy_) : s (s_), dx (dx_), dy (dy_) {}
    void clear (uint32_t) {}
    void rect (int x0, int y0, int x1, int y1, uint32_t c) { s.rect (x0 + dx, y0 + dy, x1 + dx, y1 + dy, c); }
    void vline (double x, int y0, int y1, double width, uint32_t c) { s.vline (x + dx, y0 + dy, y1 + dy, width, c); }
    void disc (double x, double y, double r, uint32_t c) { s.disc (x + dx, y + dy, r, c); }
    void curve (const float* rows, int n, int x0, int width)
    {
        float shifted[gpu::kMaxCurve];
        for (int i = 0; i < n && i < gpu::kMaxCurve; ++i)
            shifted[i] = rows[i] + static_cast<float> (dy);
        s.curve (shifted, std::min (n, static_cast<int> (gpu::kMaxCurve)), x0 + dx, width);
    }
    void fill (double baseRow, uint32_t c) { s.fill (baseRow + dy, c); }
    void stroke (double half, uint32_t c) { s.stroke (half, c); }
    void text (int x, int y, int cell, const char* str, uint32_t c) { s.text (x + dx, y + dy, cell, str, c); }
    Sink& s;
    int dx, dy;
};

/** The drawing target of the GPU helper: the display list of gpu/scene.h. */
struct SceneSink
{
    explicit SceneSink (gpu::SceneBuilder& b) : sb (b) {}
    void clear (uint32_t rgba) { sb.clear (rgba); }
    void rect (int x0, int y0, int x1, int y1, uint32_t c) { sb.rect (static_cast<float> (x0), static_cast<float> (y0), static_cast<float> (x1), static_cast<float> (y1), c); }
    void vline (double x, int y0, int y1, double width, uint32_t c) { sb.vline (static_cast<float> (x), static_cast<float> (y0), static_cast<float> (y1), static_cast<float> (width), c); }
    void disc (double x, double y, double r, uint32_t c) { sb.disc (static_cast<float> (x), static_cast<float> (y), static_cast<float> (r), c); }
    void curve (const float* rows, int n, int x0, int width) { sb.curve (rows, n, x0, width); }
    void fill (double baseRow, uint32_t c) { sb.fill (static_cast<float> (baseRow), c); }
    void stroke (double half, uint32_t c) { sb.stroke (static_cast<float> (half), c); }
    void text (int x, int y, int cell, const char* s, uint32_t c) { sb.text (x, y, cell, s, c); }
    gpu::SceneBuilder& sb;
};
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
        // ---- oscillator: the picture of the display rectangle of the plugin-drawn oscillator UI (the cycle of a wavetable, the overview of a sample, multisample or granular oscillator, the spectrum of a
        // spectral one, the noise colour, the sub oscillator's waveform)
        if (playosc::oscNumber (sel_[0]) == 0 && sel_[0] == playosc::kSub)
        {
            const float wave = group_[0]->value ("sub.waveform"), phase = group_[0]->value ("sub.phase");
            const bool data = group_[0]->read (*in.instance) || ! have_[0] || forced_[0];
            if (data)
            {
                fillSub (static_cast<int> (std::lround (wave)), phase);
                kind_ = synthyboy::OscCycle::Kind::wavetable;
                res_ = synthyboy::OscCycle::Result();
                res_.kind = kind_;
                group_[0]->accept();
                have_[0] = true;
                forced_[0] = false;
                changed = true;
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
                budget_[0].spent (in.nowMs, msSince (t0));
                counters_.lastComputeMs = budget_[0].costMs;
                ++counters_.recomputes;
                group_[0]->accept();
                std::memcpy (lastTargets_, t, sizeof t);
                have_[0] = true;
                forced_[0] = false;
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
        counters_.changes += changed;
        return changed;
    }
    bool valid() const noexcept override { return have_[0] && have_[1] && have_[2]; }

    bool regionOverride (Rect& ui) const override
    {
        ui = { 0, play::kTabY, play::kRegionW, play::kRegionH };
        return true;
    }
    bool onTouch (const TouchEvent& ev, UiStack& stack, IInstanceView& io, double nowMs) override { return osc_ != nullptr && osc_->touch (ev, stack, io, nowMs); }
    void onTick (UiStack& stack, IInstanceView& io, double nowMs) override { if (osc_ != nullptr) osc_->tick (nowMs, stack, io); }
    bool fingerDown() const noexcept override { return osc_ != nullptr && osc_->fingerDown(); }
    void dropGesture() noexcept override { if (osc_ != nullptr) osc_->dropGesture(); }
    OscUi* oscUi() noexcept { return osc_.get(); }

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
                o << " " << (osc_->qTargets()[k] >= 0 ? synthyboy::paramTable()[osc_->qTargets()[k]].key : "-");
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
        sb.clear (0u);   // transparent: the GPU helper keeps the last picture until a Clear op, and the host's own widgets of the Env / LFO thirds must show through
        play::SceneSink base (sb);
        play::ShiftSink<play::SceneSink> shifted (base, play::kOx, play::kOy);
        if (osc_ == nullptr || osc_->isMain())
            paint (shifted, play::kPanelW, play::kPanelH);   // the Env and LFO panels (not drawn while a full screen covers them)
        if (osc_ == nullptr)
            return;
        osc_->draw (sb);
        const ui::Rect r = osc_->displayRect();
        if (! r.empty())
            drawPicture (sb, r);
        osc_->drawOnDisplay (sb);
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

    /** The picture of the oscillator in a rectangle of the canvas: what the data of OscCycle::computeDisplay is (docs/MPC.md "Oscillator UI"). */
    void drawPicture (gpu::SceneBuilder& sb, const ui::Rect& r) const
    {
        using K = synthyboy::OscCycle::Kind;
        if (r.w < 60 || r.h < 40)
            return;
        const ui::Rect in = r.inset (6);
        const double top = in.y + 4.0, bottom = in.bottom() - 4.0;
        auto row = [&] (double v01) { return top + (1.0 - std::clamp (v01, -0.2, 1.2)) * (bottom - top); };
        const uint32_t line = gpu::rgba8 (0xff, 0x2e, 0x63, 255), area = gpu::rgba8 (0xd3, 0x11, 0x45, 64), grid = gpu::rgba8 (255, 255, 255, 70), grid2 = gpu::rgba8 (255, 255, 255, 30);
        const int px = in.x, pw = in.w;
        const bool envelopeKind = kind_ == K::sample || kind_ == K::multisample || kind_ == K::granular;
        if (kind_ == K::wavetable || envelopeKind || kind_ == K::spectral)
        {
            if (kind_ != K::spectral)
                sb.rect (static_cast<float> (px), static_cast<float> (row (0.5)), static_cast<float> (px + pw), static_cast<float> (row (0.5)) + 1.0f, grid);
            for (int q = 1; q < 4; ++q)
                sb.rect (static_cast<float> (px + (pw - 1) * q / 4), static_cast<float> (row (1.0)), static_cast<float> (px + (pw - 1) * q / 4 + 1), static_cast<float> (row (0.0)) + 1.0f, grid2);
        }
        float rows[gpu::kMaxCurve];
        const int n = std::min (displayPoints_, static_cast<int> (cycle_.size()));
        if (kind_ == K::wavetable)
        {
            float peak = 1.0f;
            for (int i = 0; i < n; ++i)
                peak = std::max (peak, std::fabs (cycle_[static_cast<size_t> (i)]));
            for (int i = 0; i < n; ++i)
                rows[i] = static_cast<float> (row (0.5 + 0.5 * static_cast<double> (cycle_[static_cast<size_t> (i)] / peak)));
            sb.curve (rows, n, px, pw);
            sb.fill (row (0.5), area);
            sb.stroke (2.0f, line);
        }
        else if (envelopeKind)
        {
            float peak = 0.05f;
            for (int i = 0; i < n; ++i)
                peak = std::max ({ peak, std::fabs (cycle_[static_cast<size_t> (i)]), std::fabs (cycleMax_[static_cast<size_t> (i)]) });
            for (int pass = 0; pass < 2; ++pass)
            {
                const std::vector<float>& src = pass == 0 ? cycleMax_ : cycle_;
                for (int i = 0; i < n; ++i)
                    rows[i] = static_cast<float> (row (0.5 + 0.5 * static_cast<double> (src[static_cast<size_t> (i)] / peak)));
                sb.curve (rows, n, px, pw);
                sb.fill (row (0.5), area);
                sb.stroke (2.0f, line);
            }
            if (res_.loopOn)
                for (float m : { res_.loopStart, res_.loopEnd })
                    sb.vline (static_cast<float> (px + m * static_cast<float> (pw - 1)), static_cast<float> (row (1.0)), static_cast<float> (row (0.0)), 2.0f, gpu::rgba8 (255, 255, 255, 150));
            if (kind_ == K::granular && res_.grainSize > 0.0f)
            {
                const float x0 = static_cast<float> (px) + res_.grainPosition * static_cast<float> (pw - 1);
                sb.rect (x0, static_cast<float> (row (1.0)), x0 + std::max (2.0f, res_.grainSize * static_cast<float> (pw - 1)), static_cast<float> (row (0.0)) + 1.0f, gpu::rgba8 (255, 255, 255, 40));
            }
        }
        else if (kind_ == K::spectral)
        {
            for (int i = 0; i < n; ++i)
                rows[i] = static_cast<float> (row ((static_cast<double> (cycle_[static_cast<size_t> (i)]) + 120.0) / 180.0));
            sb.curve (rows, n, px, pw);
            sb.fill (row (0.0), area);
            sb.stroke (2.0f, line);
        }
        else
        {
            const char* msg = kind_ == K::noise ? "NOISE" : (kind_ == K::noSample ? "NO SAMPLE" : (kind_ == K::off ? "OFF" : ""));
            const int cell = 4, tw = static_cast<int> (std::strlen (msg)) * 6 * cell;
            if (msg[0] != '\0')
                sb.text (in.x + (in.w - tw) / 2, in.y + (in.h - 7 * cell) / 2, cell, msg, gpu::rgba8 (0xe6, 0xe6, 0xe6, 255));
        }
    }

    template <class Sink>
    void paint (Sink& s, int w, int h) const
    {
        using namespace play;
        s.clear (gpu::rgba8FromRgb (layout::kBackgroundRgb, 255));
        for (int p = 1; p < kPanels; ++p)   // the oscillator panel is the plugin-drawn compact view now (osc_ui.h)
        {
            const int x0 = panelX (p);
            const bool hi = highlight_ == p + 1;
            const uint32_t frame = gpu::rgba8FromRgb (hi ? kFrameRgb : kRestFrameRgb, 255);
            const int t = hi ? 3 : 1;
            s.rect (x0, 0, x0 + kPanelW, h, gpu::rgba8FromRgb (kPanelRgb, 255));
            s.rect (x0, 0, x0 + kPanelW, t, frame);
            s.rect (x0, h - t, x0 + kPanelW, h, frame);
            s.rect (x0, t, x0 + t, h - t, frame);
            s.rect (x0 + kPanelW - t, t, x0 + kPanelW, h - t, frame);
            const std::string tt = title (p, sel_[p]);
            s.text (x0 + 12, 9, kTitleCell, tt.c_str(), gpu::rgba8 (0xe6, 0xe6, 0xe6, 255));
            const int px = x0 + kPlotX;
            // the grid: the zero line (the envelope's is its bottom) and the quarters
            const double zero = p == 1 ? plotY (0.0, h, 6.0) : plotY (0.5, h, 6.0);
            s.rect (px, static_cast<int> (zero), px + kPlotW, static_cast<int> (zero) + 1, gpu::rgba8 (255, 255, 255, 70));
            for (int q = 1; q < 4; ++q)
                s.rect (px + (kPlotW - 1) * q / 4, static_cast<int> (plotY (1.0, h, 6.0)), px + (kPlotW - 1) * q / 4 + 1, static_cast<int> (plotY (0.0, h, 6.0)) + 1, gpu::rgba8 (255, 255, 255, 30));
            float rows[gpu::kMaxCurve];
            const uint32_t line = gpu::rgba8 (0xff, 0x2e, 0x63, 255), area = gpu::rgba8 (0xd3, 0x11, 0x45, 64);
            if (p == 0)
            {
                if (kind_ == synthyboy::OscCycle::Kind::wavetable)
                {
                    float peak = 1.0f;
                    for (float v : cycle_)
                        peak = std::max (peak, std::fabs (v));
                    const int n = static_cast<int> (cycle_.size());
                    for (int i = 0; i < n; ++i)
                        rows[i] = static_cast<float> (std::clamp (plotY (0.5 + 0.5 * static_cast<double> (cycle_[static_cast<size_t> (i)] / peak), h, 6.0), -60.0, h + 60.0));
                    s.curve (rows, n, px, kPlotW);
                    s.fill (zero, area);
                    s.stroke (2.0, line);
                }
            }
            else if (p == 1)
            {
                for (int i = 0; i < kEnvPoints; ++i)
                    rows[i] = static_cast<float> (plotY (static_cast<double> (envY_[static_cast<size_t> (i)]), h, 6.0));
                s.curve (rows, kEnvPoints, px, kPlotW);
                s.fill (zero, area);
                s.stroke (2.0, line);
                if (envDotPx_ >= 0)
                {
                    const double dx = px + envDotX_ * (kPlotW - 1) + 0.5;
                    const double pos = std::clamp (envDotX_, 0.0, 1.0) * (kEnvPoints - 1);
                    const int i = std::min (static_cast<int> (pos), kEnvPoints - 2);
                    const double v = envY_[static_cast<size_t> (i)] * (1.0 - (pos - i)) + envY_[static_cast<size_t> (i) + 1] * (pos - i);
                    s.disc (dx, plotY (v, h, 6.0), 7.0, gpu::rgba8 (255, 255, 255, 255));
                    s.disc (dx, plotY (v, h, 6.0), 4.5, line);
                }
            }
            else
            {
                const int n = LfoView::kPoints;
                for (int i = 0; i < n; ++i)
                    rows[i] = static_cast<float> (std::clamp (plotY (0.5 + 0.5 * static_cast<double> (lfoY_[static_cast<size_t> (i)]), h, 6.0), -60.0, h + 60.0));
                s.curve (rows, n, px, kPlotW);
                s.fill (zero, area);
                s.stroke (2.0, line);
                if (lfoCursorPx_ >= 0)
                {
                    const double cx = px + lfoCursorX_ * (kPlotW - 1) + 0.5;
                    s.vline (cx, static_cast<int> (plotY (1.0, h, 6.0)), static_cast<int> (plotY (0.0, h, 6.0)) + 1, 2.0, gpu::rgba8 (255, 255, 255, 150));
                    const double pos = std::clamp (static_cast<double> (lfoCursorX_), 0.0, 1.0) * (n - 1);
                    const int i = std::min (static_cast<int> (pos), n - 2);
                    const double v = lfoY_[static_cast<size_t> (i)] * (1.0 - (pos - i)) + lfoY_[static_cast<size_t> (i) + 1] * (pos - i);
                    const double cy = plotY (0.5 + 0.5 * v, h, 6.0);
                    s.disc (cx, cy, 7.0, gpu::rgba8 (255, 255, 255, 255));
                    s.disc (cx, cy, 4.5, line);
                }
            }
        }
        (void) w;
    }

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
    std::vector<float> cycle_, cycleMax_, envY_, lfoY_;
    int displayPoints_ = synthyboy::OscCycle::kSamples;
    synthyboy::OscCycle::Result res_;
    std::unique_ptr<OscUi> osc_;
    std::unique_ptr<gpu::SceneData> scratch_;
    uint32_t uiHash_ = 0;
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
