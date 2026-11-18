// SynthyBoy - MPC overlay: the views. FilterView (the response curve of filter 1 / 2), LfoView (the shape of an LFO with the live cursor), EnvView (the live phase marker over the native envelope widget), SpectrumView (the curve of the plugin's output over the 32 meters of the Macros tab). Each reads its data through IInstanceView / the engine's readout, keeps its own
// recompute budget, draws a whole picture with draw.h and reports its own counters.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "curve_source.h"
#include "draw.h"
#include "env_geometry.h"
#include "renderer.h"
#include "spectrum_curve.h"
#include "synthyboy/lfo_display.h"
#include "synthyboy/osc_cycle.h"
#include "view.h"
#include "view_cast.h"

namespace synthyboy_mpc { namespace overlay {

/** The share of one core a view's own computations may use while its data moves, and the least gap between two of them. */
constexpr double kViewBudget = 0.08;
constexpr double kViewMinGapMs = 15.0;
constexpr int kSourceSlots = 64;   // source ids (the choice values of modNN.source) fit: LiveValues::kMaxSources

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
class FilterView final : public IView
{
public:
    FilterView (int filter, ICurveRenderer& renderer, double fixedCostMs) : filter_ (filter), renderer_ (renderer), fixedCost_ (fixedCostMs), name_ (filter == 2 ? "filter2" : "filter1") {}

    const char* name() const noexcept override { return name_; }
    void targets (std::vector<int>& out) const override
    {
        int idx[2];
        synthyboy::FilterResponse::readoutIndices (filter_, idx);
        out.push_back (idx[0]);
        out.push_back (idx[1]);
    }
    bool update (const ViewInput& in) override
    {
        if (! curve_ || in.instance->sampleRate() != rate_)
        {
            CurveSource::Config cfg;
            cfg.sampleRate = in.instance->sampleRate();
            cfg.fixedCostMs = fixedCost_;
            cfg.filter = filter_;
            curve_ = std::make_unique<CurveSource> (cfg);
            rate_ = cfg.sampleRate;
            fresh_ = true;
        }
        const unsigned long before = curve_->stats().fullComputes;
        bool changed = curve_->update (instanceOf (in), in.targets, in.nowMs);
        ++counters_.updates;
        counters_.recomputes += curve_->stats().fullComputes - before;
        counters_.lastComputeMs = curve_->stats().lastComputeMs;
        if (fresh_ && curve_->valid())
            changed = true, fresh_ = false;
        counters_.changes += changed;
        return changed;
    }
    bool valid() const noexcept override { return curve_ && curve_->valid(); }
    void render (uint32_t* canvas, int w, int h) override { renderer_.render (curve_->curve(), curve_->points(), canvas, w, h, layout::kBackgroundRgb, nullptr, 0); }
    bool describe (gpu::SceneBuilder& sb, int viewIndex, int w, int h) const override
    {
        if (! valid() || w < 2 || h < 8)
            return false;
        using R = CpuCurveRenderer;
        sb.begin (gpu::kKindFilter, static_cast<uint32_t> (viewIndex), w, h);
        sb.clear (gpu::rgba8FromRgb (layout::kBackgroundRgb, 255));
        const double fs[3] = { 100.0, 1000.0, 10000.0 };
        for (double f : fs)
            sb.rect (static_cast<float> (R::gridX (f, w)), 0.0f, static_cast<float> (R::gridX (f, w) + 1), static_cast<float> (h), gpu::rgba8 (255, 255, 255, 40));
        const int zy = static_cast<int> (R::zeroLineY (h));
        sb.rect (0.0f, static_cast<float> (zy), static_cast<float> (w), static_cast<float> (zy + 1), gpu::rgba8 (255, 255, 255, 90));
        float rows[gpu::kMaxCurve];
        const int n = std::min (curve_->points(), gpu::kMaxCurve);
        for (int i = 0; i < n; ++i)
            rows[i] = static_cast<float> (std::clamp (R::dbToY (static_cast<double> (curve_->curve()[i]), h), -60.0, h + 60.0));
        sb.polyline (rows, n);
        sb.fill (static_cast<float> (h), gpu::rgba8 (0xd3, 0x11, 0x45, 64));
        sb.stroke (static_cast<float> (R::kHalfWidth), gpu::rgba8 (0xff, 0x2e, 0x63, 255));
        return ! sb.overflow;
    }
    uint32_t hash() const noexcept override { return curve_ ? curve_->hash() : 0u; }
    void writeStatus (std::ostream& o) const override
    {
        if (! curve_)
            return;
        const CurveSource::Stats& s = curve_->stats();
        o << "last_compute_ms: " << s.lastComputeMs << "\nfull_computes_total: " << s.fullComputes << "\ntranslations_total: " << s.translations << "\n";
        o << "live_cutoff_hz: " << s.cutoffHz << "\nlive_resonance_percent: " << s.resonancePercent << "\n";
        if (curve_->valid())
        {
            float lo, hi;
            curve_->minMax (lo, hi);
            char hash[16];
            std::snprintf (hash, sizeof hash, "%08x", curve_->hash());
            o << "curve_min_db: " << lo << "\ncurve_max_db: " << hi << "\ncurve_hash: " << hash << "\n";
        }
    }
    void writeDump (std::ostream& o) const override
    {
        if (! valid())
            return;
        o << "# view " << name_ << ": index shown_db exact_db (point i = 20 Hz * 1000 ^ (i / 255)), cutoff_hz " << curve_->stats().cutoffHz << " resonance_percent " << curve_->stats().resonancePercent << "\n";
        for (int i = 0; i < curve_->points(); ++i)
            o << i << " " << curve_->curve()[i] << " " << curve_->exactCurve()[i] << "\n";
    }
    const CurveSource* curve() const noexcept { return curve_.get(); }

private:
    int filter_;
    ICurveRenderer& renderer_;
    double fixedCost_;
    const char* name_;
    std::unique_ptr<CurveSource> curve_;
    double rate_ = 0.0;
    bool fresh_ = false;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** What the LFO and the oscillator views share: the parameters of one group (every key that starts with `prefix`), their change detection and the budget of recomputes. */
class ParamGroup
{
public:
    explicit ParamGroup (const std::string& prefix)
    {
        const synthyboy::ParamInfo* table = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            if (std::strncmp (table[i].key, prefix.c_str(), prefix.size()) == 0)
                idx_.push_back (static_cast<int> (i));
        values_ = synthyboy::ParamValues::defaults();
        seen_.assign (idx_.size(), 0.0f);
    }
    /** Reads the group from the instance; true when any value differs from the one of the last accepted () call. */
    bool read (const IInstanceView& v)
    {
        bool changed = ! accepted_;
        for (size_t k = 0; k < idx_.size(); ++k)
        {
            const float x = v.param (idx_[k]);
            values_.plain[idx_[k]] = x;
            changed = changed || x != seen_[k];
        }
        return changed;
    }
    void accept() noexcept
    {
        for (size_t k = 0; k < idx_.size(); ++k)
            seen_[k] = values_.plain[idx_[k]];
        accepted_ = true;
    }
    const synthyboy::ParamValues& values() const noexcept { return values_; }
    synthyboy::ParamValues& values() noexcept { return values_; }
    float value (const char* key) const
    {
        const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key);
        return p ? values_.plain[synthyboy::findParamIndex (p->id)] : 0.0f;
    }

private:
    std::vector<int> idx_;
    synthyboy::ParamValues values_;
    std::vector<float> seen_;
    bool accepted_ = false;
};

/** The gap rule of the full computations of a view: at least kViewMinGapMs and cost / kViewBudget after the last one. */
struct ComputeBudget
{
    double costMs = 0.0, lastMs = -1.0e9, fixedCostMs = -1.0;
    bool due (double nowMs) const noexcept { return nowMs - lastMs >= std::max (kViewMinGapMs, (fixedCostMs >= 0.0 ? fixedCostMs : costMs) / kViewBudget); }
    void spent (double nowMs, double ms) noexcept { costMs = lastMs < -1.0e8 ? ms : 0.7 * costMs + 0.3 * ms; lastMs = nowMs; }
};

inline double msSince (std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count(); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
class LfoView final : public IView
{
public:
    static constexpr int kPoints = 300;            // values of the shape (the region is 600 px wide)
    static constexpr double kMargin = 16.0;        // px above +1 and below -1
    LfoView (int lfo, double fixedCostMs) : lfo_ (lfo), group_ ("lfo" + std::to_string (lfo) + "."), name_ ("lfo" + std::to_string (lfo))
    {
        budget_.fixedCostMs = fixedCostMs;
        shape_.assign (kPoints, 0.0f);
    }

    const char* name() const noexcept override { return name_.c_str(); }
    void targets (std::vector<int>&) const override {}
    bool update (const ViewInput& in) override
    {
        ++counters_.updates;
        bool changed = false;
        const IInstanceView& inst = instanceOf (in);
        const synthyboy::LiveValues* live = liveOf (in);
        std::shared_ptr<const synthyboy::StateContent> content = inst.content();
        const double bpm = std::round (inst.bpm() * 10.0) / 10.0;
        bool dataChanged = group_.read (inst) || content != content_ || bpm != bpm_;
        // the sources a custom shape's points follow (point modulation): only that shape reads them
        float sources[kSourceSlots] = {};
        bool sourcesChanged = false;
        if (static_cast<int> (group_.value ((name_ + ".shape").c_str()) + 0.5f) == 12 && live != nullptr)
        {
            for (int id = 1; id < kSourceSlots && id - 1 < synthyboy::LiveValues::kMaxSources; ++id)
            {
                const float v = live->source[id - 1];
                const bool bipolar = (id >= 7 && id <= 14) || (id >= 15 && id <= 18) || id == 33 || id == 34;
                sources[id] = bipolar ? 0.5f + 0.5f * v : v;
            }
            sourcesChanged = std::memcmp (sources, lastSources_, sizeof sources) != 0;
        }
        if ((dataChanged || sourcesChanged || ! have_) && (! have_ || budget_.due (in.nowMs)))
        {
            const auto t0 = std::chrono::steady_clock::now();
            synthyboy::LfoDisplay::Request req;
            req.lfo = lfo_;
            req.bpm = bpm;
            req.sources = live != nullptr ? sources : nullptr;
            synthyboy::LfoDisplay::compute (group_.values(), content.get(), req, shape_.data(), kPoints);
            budget_.spent (in.nowMs, msSince (t0));
            counters_.lastComputeMs = budget_.costMs;
            ++counters_.recomputes;
            group_.accept();
            content_ = content;
            bpm_ = bpm;
            std::memcpy (lastSources_, sources, sizeof sources);
            have_ = true;
            baseDirty_ = true;
            changed = true;
        }
        const float x = live != nullptr ? synthyboy::LfoDisplay::cursorX (live->lfoPhase[lfo_ - 1]) : -1.0f;
        const int px = x < 0.0f ? -1 : static_cast<int> (std::lround (x * 599.0f));
        if (px != cursorPx_)
        {
            cursorPx_ = px;
            cursorX_ = x;
            changed = true;
        }
        counters_.changes += changed;
        return changed;
    }
    bool valid() const noexcept override { return have_; }

    static double valueToY (double v, int h) noexcept { return kMargin + (1.0 - v) * 0.5 * (h - 2.0 * kMargin); }

    void render (uint32_t* cv, int w, int h) override
    {
        if (static_cast<int> (base_.size()) != w * h)
            baseDirty_ = true;
        if (baseDirty_)
        {
            base_.assign (static_cast<size_t> (w) * static_cast<size_t> (h), 0u);
            draw::setRect (base_.data(), w, h, 0, 0, w, h, draw::opaque (layout::kBackgroundRgb));
            const int zy = static_cast<int> (valueToY (0.0, h));
            draw::fillRect (base_.data(), w, h, 0, zy, w, zy + 1, draw::premultiplied (255, 255, 255, 90));
            for (int q = 1; q < 4; ++q)
                draw::fillRect (base_.data(), w, h, (w - 1) * q / 4, 0, (w - 1) * q / 4 + 1, h, draw::premultiplied (255, 255, 255, 40));
            draw::sampleColumns (ys_, shape_.data(), kPoints, w, h, [h] (double v) { return valueToY (v, h); }, 4);
            draw::fillToBaseline (base_.data(), w, h, ys_, 4, valueToY (0.0, h), draw::premultiplied (0xd3, 0x11, 0x45, 64));
            draw::strokeCurve (base_.data(), w, h, ys_, 4, 2.0, draw::premultiplied (0xff, 0x2e, 0x63, 255));
            baseDirty_ = false;
        }
        std::memcpy (cv, base_.data(), static_cast<size_t> (w) * static_cast<size_t> (h) * 4);
        if (cursorPx_ >= 0)
        {
            const double x = cursorX_ * (w - 1);
            draw::vline (cv, w, h, x + 0.5, 0, h, 2.0, draw::premultiplied (255, 255, 255, 150));
            const double pos = std::clamp (static_cast<double> (cursorX_), 0.0, 1.0) * (kPoints - 1);
            const int i = std::min (static_cast<int> (pos), kPoints - 2);
            const double v = shape_[static_cast<size_t> (i)] * (1.0 - (pos - i)) + shape_[static_cast<size_t> (i) + 1] * (pos - i);
            draw::disc (cv, w, h, x + 0.5, valueToY (v, h), 7.0, draw::premultiplied (255, 255, 255, 255));
            draw::disc (cv, w, h, x + 0.5, valueToY (v, h), 4.5, draw::premultiplied (0xff, 0x2e, 0x63, 255));
        }
    }
    bool describe (gpu::SceneBuilder& sb, int viewIndex, int w, int h) const override
    {
        if (! have_ || w < 2 || h < 8)
            return false;
        sb.begin (gpu::kKindLfo, static_cast<uint32_t> (viewIndex), w, h);
        sb.clear (gpu::rgba8FromRgb (layout::kBackgroundRgb, 255));
        const int zy = static_cast<int> (valueToY (0.0, h));
        sb.rect (0.0f, static_cast<float> (zy), static_cast<float> (w), static_cast<float> (zy + 1), gpu::rgba8 (255, 255, 255, 90));
        for (int q = 1; q < 4; ++q)
            sb.rect (static_cast<float> ((w - 1) * q / 4), 0.0f, static_cast<float> ((w - 1) * q / 4 + 1), static_cast<float> (h), gpu::rgba8 (255, 255, 255, 40));
        float rows[gpu::kMaxCurve];
        for (int i = 0; i < kPoints; ++i)
            rows[i] = static_cast<float> (std::clamp (valueToY (static_cast<double> (shape_[static_cast<size_t> (i)]), h), -60.0, h + 60.0));
        sb.polyline (rows, kPoints);
        sb.fill (static_cast<float> (valueToY (0.0, h)), gpu::rgba8 (0xd3, 0x11, 0x45, 64));
        sb.stroke (2.0f, gpu::rgba8 (0xff, 0x2e, 0x63, 255));
        if (cursorPx_ >= 0)
        {
            const double x = cursorX_ * (w - 1);
            sb.vline (static_cast<float> (x + 0.5), 0.0f, static_cast<float> (h), 2.0f, gpu::rgba8 (255, 255, 255, 150));
            const double pos = std::clamp (static_cast<double> (cursorX_), 0.0, 1.0) * (kPoints - 1);
            const int i = std::min (static_cast<int> (pos), kPoints - 2);
            const double v = shape_[static_cast<size_t> (i)] * (1.0 - (pos - i)) + shape_[static_cast<size_t> (i) + 1] * (pos - i);
            sb.disc (static_cast<float> (x + 0.5), static_cast<float> (valueToY (v, h)), 7.0f, gpu::rgba8 (255, 255, 255, 255));
            sb.disc (static_cast<float> (x + 0.5), static_cast<float> (valueToY (v, h)), 4.5f, gpu::rgba8 (0xff, 0x2e, 0x63, 255));
        }
        return ! sb.overflow;
    }
    uint32_t hash() const noexcept override
    {
        uint32_t h = 2166136261u;
        for (float v : shape_)
            h = fnv (h, static_cast<int32_t> (std::lround (static_cast<double> (v) * 100.0)));
        return h;
    }
    float cursor() const noexcept { return cursorX_; }
    const std::vector<float>& shape() const noexcept { return shape_; }
    void writeStatus (std::ostream& o) const override
    {
        char hash[16];
        std::snprintf (hash, sizeof hash, "%08x", this->hash());
        o << "lfo_shape_hash: " << hash << "\nlfo_cursor_x: " << (cursorPx_ < 0 ? -1.0f : cursorX_) << "\nlfo_recomputes_total: " << counters_.recomputes << "\nlfo_compute_ms: " << counters_.lastComputeMs << "\n";
    }
    void writeDump (std::ostream& o) const override
    {
        o << "# view " << name_ << ": index value (-1 .. 1, x = i / " << (kPoints - 1) << " cycles since the note start), cursor_x " << (cursorPx_ < 0 ? -1.0f : cursorX_) << "\n";
        for (int i = 0; i < kPoints; ++i)
            o << i << " " << shape_[static_cast<size_t> (i)] << "\n";
    }

private:
    int lfo_;
    ParamGroup group_;
    std::string name_;
    ComputeBudget budget_;
    std::vector<float> shape_;
    std::shared_ptr<const synthyboy::StateContent> content_;
    double bpm_ = 0.0;
    float lastSources_[kSourceSlots] = {};
    bool have_ = false, baseDirty_ = true;
    int cursorPx_ = -1;
    float cursorX_ = -1.0f;
    std::vector<uint32_t> base_;
    std::vector<double> ys_;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** The live phase marker over the native envelope widget: a dot (and a soft vertical line) at the point of the curve where the playing voice is. The plane is transparent;
    only the two marker squares are painted over (in the colour of the widget's background). The dot is drawn only when the pixel of the primary framebuffer under the computed
    point has the colour of the widget's line (the widget is where the rule says it is); otherwise nothing is drawn and the miss is counted. */
class EnvView final : public IView
{
public:
    EnvView (int env, const layout::View& layoutView) : env_ (env), group_ ("env" + std::to_string (env) + "."), name_ ("env" + std::to_string (env)), view_ (layoutView) {}

    const char* name() const noexcept override { return name_.c_str(); }
    void targets (std::vector<int>&) const override {}
    bool update (const ViewInput& in) override
    {
        ++counters_.updates;
        group_.read (instanceOf (in));   // the knob values (what the widget draws), not the modulated ones
        group_.accept();
        bool changed = ! have_;
        have_ = true;
        stage_ = 0;
        fraction_ = 0.0f;
        if (const synthyboy::LiveValues* live = liveOf (in))
        {
            stage_ = static_cast<int> (live->envFineStage[env_ - 1] + 0.5f);
            fraction_ = live->envFineFraction[env_ - 1];
        }
        const envgeom::Norm norm = normOf();
        envgeom::Point p = envgeom::dotFor (norm, stage_, static_cast<double> (fraction_));
        bool draw = false;
        if (p.valid)
        {
            ++attempts_;
            if (lineUnder (in, p.x, p.y))
            {
                draw = true;
                ++pointHits_;
            }
            else if (polylineVerified (in, norm, in.nowMs))
            {
                // the handle circles of the widget (and the stage's own corner) hide the line there: the polyline is where the rule says, verified on the stretches that are visible
                draw = true;
                ++polylineHits_;
            }
            else
                ++misses_;
        }
        const int px = draw ? static_cast<int> (std::lround (p.x)) : -1, py = draw ? static_cast<int> (std::lround (p.y)) : -1;
        if (px != dotX_ || py != dotY_)
        {
            dotX_ = px;
            dotY_ = py;
            changed = true;
        }
        counters_.changes += changed;
        return changed;
    }
    bool valid() const noexcept override { return have_; }

    void render (uint32_t* cv, int w, int h) override
    {
        std::memset (cv, 0, static_cast<size_t> (w) * static_cast<size_t> (h) * 4);
        // the marker squares: painted over in the colour of what is under them (the widget's background), one pixel larger than the square
        for (int m = 0; m < 2; ++m)
        {
            const layout::Marker& mk = view_.markers[m];
            draw::setRect (cv, w, h, mk.rect.x - view_.region.x - 1, mk.rect.y - view_.region.y - 1, mk.rect.x - view_.region.x + mk.rect.w + 1, mk.rect.y - view_.region.y + mk.rect.h + 1, draw::opaque (mk.cover));
        }
        {   // the instance picture of the skin (the colour that says whose page it is) is painted over in the same way
            const layout::Rect& ir = view_.instance;
            draw::setRect (cv, w, h, ir.x - view_.region.x - 1, ir.y - view_.region.y - 1, ir.x - view_.region.x + ir.w + 1, ir.y - view_.region.y + ir.h + 1, draw::opaque (view_.markers[0].cover));
        }
        if (dotX_ < 0)
            return;
        const double x = dotX_ - view_.region.x + 0.5, y = dotY_ - view_.region.y + 0.5;
        draw::vline (cv, w, h, x, static_cast<int> (envgeom::kTopY - view_.region.y) - 4, static_cast<int> (envgeom::kBottomY - view_.region.y) + 5, 2.0, draw::premultiplied (255, 255, 255, 70));
        draw::disc (cv, w, h, x, y, 8.0, draw::premultiplied (255, 255, 255, 255));
        draw::disc (cv, w, h, x, y, 5.0, draw::premultiplied (0x1e, 0x1d, 0x1e, 255));
        draw::disc (cv, w, h, x, y, 3.0, draw::premultiplied (255, 255, 255, 255));
    }
    bool describe (gpu::SceneBuilder& sb, int viewIndex, int w, int h) const override
    {
        if (! have_)
            return false;
        sb.begin (gpu::kKindEnv, static_cast<uint32_t> (viewIndex), w, h);
        sb.clear (0u);   // transparent
        for (int m = 0; m < 2; ++m)
        {
            const layout::Marker& mk = view_.markers[m];
            sb.rect (static_cast<float> (mk.rect.x - view_.region.x - 1), static_cast<float> (mk.rect.y - view_.region.y - 1), static_cast<float> (mk.rect.x - view_.region.x + mk.rect.w + 1),
                     static_cast<float> (mk.rect.y - view_.region.y + mk.rect.h + 1), gpu::rgba8FromRgb (mk.cover, 255));
        }
        {
            const layout::Rect& ir = view_.instance;
            sb.rect (static_cast<float> (ir.x - view_.region.x - 1), static_cast<float> (ir.y - view_.region.y - 1), static_cast<float> (ir.x - view_.region.x + ir.w + 1),
                     static_cast<float> (ir.y - view_.region.y + ir.h + 1), gpu::rgba8FromRgb (view_.markers[0].cover, 255));
        }
        if (dotX_ >= 0)
        {
            const double x = dotX_ - view_.region.x + 0.5, y = dotY_ - view_.region.y + 0.5;
            sb.vline (static_cast<float> (x), static_cast<float> (static_cast<int> (envgeom::kTopY - view_.region.y) - 4), static_cast<float> (static_cast<int> (envgeom::kBottomY - view_.region.y) + 5), 2.0f,
                      gpu::rgba8 (255, 255, 255, 70));
            sb.disc (static_cast<float> (x), static_cast<float> (y), 8.0f, gpu::rgba8 (255, 255, 255, 255));
            sb.disc (static_cast<float> (x), static_cast<float> (y), 5.0f, gpu::rgba8 (0x1e, 0x1d, 0x1e, 255));
            sb.disc (static_cast<float> (x), static_cast<float> (y), 3.0f, gpu::rgba8 (255, 255, 255, 255));
        }
        return ! sb.overflow;
    }
    uint32_t hash() const noexcept override { return fnv (fnv (2166136261u, dotX_), dotY_); }
    envgeom::Norm normOf() const
    {
        const std::string p = "env" + std::to_string (env_) + ".";
        auto n = [&] (const char* key)
        {
            const synthyboy::ParamInfo* info = synthyboy::findParamByKey ((p + key).c_str());
            return info ? static_cast<double> (synthyboy::toNormalised (*info, group_.values().plain[synthyboy::findParamIndex (info->id)])) : 0.0;
        };
        envgeom::Norm out;
        out.delay = n ("delay");
        out.attack = n ("attack");
        out.hold = n ("hold");
        out.decay = n ("decay");
        out.release = n ("release");
        out.sustain = group_.value ((p + "sustain").c_str()) * 0.01;
        return out;
    }
    int stage() const noexcept { return stage_; }
    int dotX() const noexcept { return dotX_; }
    int dotY() const noexcept { return dotY_; }
    unsigned long hits() const noexcept { return pointHits_ + polylineHits_; }
    unsigned long pointHits() const noexcept { return pointHits_; }
    unsigned long polylineHits() const noexcept { return polylineHits_; }
    unsigned long misses() const noexcept { return misses_; }
    void writeStatus (std::ostream& o) const override
    {
        o << "env_stage: " << stage_ << "\nenv_fraction: " << fraction_ << "\nenv_dot: " << dotX_ << " " << dotY_ << "\nenv_selfcheck_point_hits: " << pointHits_
          << "\nenv_selfcheck_polyline_hits: " << polylineHits_ << "\nenv_selfcheck_misses: " << misses_ << "\nenv_selfcheck_attempts: " << attempts_
          << "\nenv_polyline_probes: " << probeHits_ << " of " << probeCount_ << "\n";
    }
    void writeDump (std::ostream& o) const override
    {
        const envgeom::Norm n = normOf();
        double e[6];
        envgeom::stageEnds (n, e);
        o << "# view " << name_ << ": normalised delay attack hold decay sustain release " << n.delay << " " << n.attack << " " << n.hold << " " << n.decay << " " << n.sustain << " " << n.release << "\n";
        o << "stage_ends_x " << e[0] << " " << e[1] << " " << e[2] << " " << e[3] << " " << e[4] << " " << e[5] << "\nsustain_y " << envgeom::sustainY (n) << "\n";
        o << "dot " << dotX_ << " " << dotY_ << " stage " << stage_ << " fraction " << fraction_ << "\n";
    }

private:
    /** A hold on the framebuffer of the screen for as many reads as the caller wants: every begin () of the real source is a set of kernel calls (the CRTC, the framebuffer, a dma-buf
        descriptor, the sync), so the many probes of one verification share one. */
    struct FrameHold
    {
        explicit FrameHold (IFrameSource* f) : frames (f) { open = f != nullptr && f->begin (w, h); }
        ~FrameHold() { if (open) frames->end(); }
        FrameHold (const FrameHold&) = delete;
        FrameHold& operator= (const FrameHold&) = delete;
        IFrameSource* frames;
        bool open = false;
        int w = 0, h = 0;
    };

    /** Is the pixel under the point (screen coordinates) of the colour of the widget's line (2 per channel of tolerance for the anti-aliasing: 6)? The framebuffer is already held. */
    static bool lineUnderHeld (const ViewInput& in, const FrameHold& hold, double x, double y)
    {
        if (! hold.open)
            return false;
        int px, py;
        uint32_t rgb = 0;
        uiPointToPanel (in.rotation, static_cast<int> (std::lround (x)), static_cast<int> (std::lround (y)), hold.w, hold.h, px, py);
        if (! in.frames->pixel (px, py, rgb))
            return false;
        for (int s = 0; s <= 16; s += 8)
            if (std::abs (static_cast<int> ((rgb >> s) & 255u) - static_cast<int> ((envgeom::kLineRgb >> s) & 255u)) > 6)
                return false;
        return true;
    }
    /** The same for one point: holds the framebuffer for this one read. */
    bool lineUnder (const ViewInput& in, double x, double y) const
    {
        if (in.frames == nullptr)
            return false;
        const FrameHold hold (in.frames);
        return lineUnderHeld (in, hold, x, y);
    }

    /** The widget draws the stage handles (circles) over the polyline, so the line is not visible at every point. The rule is verified on the middle stretches of the long
        segments (the 25 %, 50 % and 75 % points of every segment that is at least 60 px long; one pixel of tolerance): at least three probes, and 35 % of them, must have the
        colour of the line. Cached for the same parameters for one second. */
    bool polylineVerified (const ViewInput& in, const envgeom::Norm& n, double nowMs)
    {
        const bool same = n.delay == verifiedNorm_.delay && n.attack == verifiedNorm_.attack && n.hold == verifiedNorm_.hold && n.decay == verifiedNorm_.decay
                          && n.sustain == verifiedNorm_.sustain && n.release == verifiedNorm_.release;
        if (same && nowMs - verifiedAtMs_ < 1000.0)
            return verified_;
        verifiedNorm_ = n;
        verifiedAtMs_ = nowMs;
        probeHits_ = probeCount_ = 0;
        if (in.frames == nullptr)
            return verified_ = false;
        const FrameHold hold (in.frames);   // one hold for all the probes (up to 18 probes of 9 pixels)
        for (int st = 1; st <= 6; ++st)
        {
            const envgeom::Point a = envgeom::dotFor (n, st, 0.0), b = envgeom::dotFor (n, st, 1.0);
            if (st == 5)
            {
                // the sustain segment: its two ends
                const envgeom::Point lo = envgeom::dotFor (n, 4, 1.0), hi = envgeom::dotFor (n, 6, 0.0);
                if (std::hypot (hi.x - lo.x, hi.y - lo.y) < 60.0)
                    continue;
                for (double f : { 0.25, 0.5, 0.75 })
                    probe (in, hold, lo.x + (hi.x - lo.x) * f, lo.y + (hi.y - lo.y) * f);
                continue;
            }
            if (std::hypot (b.x - a.x, b.y - a.y) < 60.0)
                continue;
            for (double f : { 0.25, 0.5, 0.75 })
                probe (in, hold, a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f);
        }
        verified_ = probeHits_ >= 3 && probeHits_ * 100 >= probeCount_ * 35;
        return verified_;
    }
    void probe (const ViewInput& in, const FrameHold& hold, double x, double y)
    {
        ++probeCount_;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (lineUnderHeld (in, hold, x + dx, y + dy))
                {
                    ++probeHits_;
                    return;
                }
    }

    int env_;
    ParamGroup group_;
    std::string name_;
    layout::View view_;
    bool have_ = false;
    int stage_ = 0, dotX_ = -1, dotY_ = -1;
    float fraction_ = 0.0f;
    unsigned long pointHits_ = 0, polylineHits_ = 0, misses_ = 0, attempts_ = 0;
    // the verification of the whole polyline (cached while the parameters stay and for a second at most)
    double verifiedAtMs_ = -1.0e9;
    bool verified_ = false;
    envgeom::Norm verifiedNorm_;
    int probeHits_ = 0, probeCount_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** The spectrum of the plugin's own output as a smooth filled curve (EQ analyser style) over the row of the 32 meters of the Macros tab: 256 points on a log axis from 20 Hz to
    20 kHz, the vertical scale the one of the meters (kSpectrumFloorDb at the bottom, kSpectrumCeilingDb at the top). The data is SpectrumCurve (FFT of the overlay thread from the ring the
    audio thread writes). The picture is an opaque dark background (the meters do not show through), three faint grid lines at 100 Hz, 1 kHz and 10 kHz, a fill with a vertical gradient and a
    2.5 pixel line in the accent colours of the skin. While the curve stands still (silence, or a steady tone) update () says "unchanged": no picture is drawn and no FFT is run for silence. */
class SpectrumView final : public IView
{
public:
    static constexpr double kTopMargin = 8.0;      // rows above the ceiling of the scale
    static constexpr double kBottomMargin = 5.0;   // rows below the floor: the line at the floor is whole
    static constexpr double kHalfWidth = 1.25;     // line 2.5 px
    static constexpr double kMinStepMs = 16.0;     // the analyses are at least this far apart (30 a second is the rate of the pictures)
    static constexpr double kGapResetMs = 600.0;   // the view was not updated for this long (another tab, a menu): the curve starts from the floor
    static constexpr int kFillTopRgb = 0xff2e63, kFillBottomRgb = 0xd31145, kFillTopAlpha = 150, kFillBottomAlpha = 14;

    SpectrumView() = default;

    const char* name() const noexcept override { return "spectrum"; }
    void targets (std::vector<int>&) const override {}
    bool update (const ViewInput& in) override
    {
        ++counters_.updates;
        const SpectrumRing* ring = in.instance != nullptr ? instanceOf (in).spectrumRing() : nullptr;
        if (ring == nullptr)
        {
            have_ = false;
            return false;
        }
        double dt = lastMs_ < 0.0 ? 33.3 : in.nowMs - lastMs_;
        if (have_ && dt < kMinStepMs)
            return false;
        if (lastMs_ >= 0.0 && dt > kGapResetMs)
            curve_.reset();
        lastMs_ = in.nowMs;
        const auto t0 = std::chrono::steady_clock::now();
        bool analysed = false;
        bool changed = curve_.update (*ring, in.instance->sampleRate(), dt, &analysed);
        if (analysed)
        {
            ++counters_.recomputes;
            counters_.lastComputeMs = 0.8 * counters_.lastComputeMs + 0.2 * msSince (t0);
        }
        if (! have_)
        {
            have_ = true;
            changed = true;
        }
        counters_.changes += changed;
        return changed;
    }
    bool valid() const noexcept override { return have_; }

    /** The row of a level (0 .. 1) on a canvas of height h. */
    static double levelToY (double level, int h) noexcept { return (h - kBottomMargin) - level * (h - kBottomMargin - kTopMargin); }
    static uint32_t gradientStraight (bool top) noexcept
    {
        const unsigned rgb = top ? kFillTopRgb : kFillBottomRgb;
        return gpu::rgba8 ((rgb >> 16) & 255u, (rgb >> 8) & 255u, rgb & 255u, static_cast<uint32_t> (top ? kFillTopAlpha : kFillBottomAlpha));
    }

    void render (uint32_t* cv, int w, int h) override { renderLevels (curve_.level(), cv, w, h, ys_); }
    bool describe (gpu::SceneBuilder& sb, int viewIndex, int w, int h) const override
    {
        if (! have_)
            return false;
        return describeLevels (curve_.level(), sb, viewIndex, w, h);
    }

    /** The picture of 256 levels (0 .. 1) on the CPU: the opaque background, the grid, the gradient fill, the line. `ys` is scratch memory. Static so that a tool can draw a dump of the levels. */
    static void renderLevels (const float* levels, uint32_t* cv, int w, int h, std::vector<double>& ys)
    {
        const uint32_t bg = draw::opaque (layout::kBackgroundRgb);
        draw::setRect (cv, w, h, 0, 0, w, h, bg);
        if (w < 2 || h < 8)
            return;
        const double fs[3] = { 100.0, 1000.0, 10000.0 };
        for (double f : fs)
            draw::fillRect (cv, w, h, CpuCurveRenderer::gridX (f, w), 0, CpuCurveRenderer::gridX (f, w) + 1, h, draw::premultiplied (255, 255, 255, 40));
        draw::sampleColumns (ys, levels, SpectrumCurve::kPoints, w, h, [h] (double v) { return levelToY (v, h); }, 4);
        draw::fillToBaselineGradient (cv, w, h, ys, 4, h, draw::premultiplied (0xff, 0x2e, 0x63, kFillTopAlpha), draw::premultiplied (0xd3, 0x11, 0x45, kFillBottomAlpha), kTopMargin, h);
        draw::strokeCurve (cv, w, h, ys, 4, kHalfWidth, draw::premultiplied (0xff, 0x2e, 0x63, 255));
    }
    /** The same picture as a scene for the GPU helper. */
    static bool describeLevels (const float* levels, gpu::SceneBuilder& sb, int viewIndex, int w, int h)
    {
        if (w < 2 || h < 8)
            return false;
        sb.begin (gpu::kKindSpectrum, static_cast<uint32_t> (viewIndex), w, h);
        sb.clear (gpu::rgba8FromRgb (layout::kBackgroundRgb, 255));
        const double fs[3] = { 100.0, 1000.0, 10000.0 };
        for (double f : fs)
            sb.rect (static_cast<float> (CpuCurveRenderer::gridX (f, w)), 0.0f, static_cast<float> (CpuCurveRenderer::gridX (f, w) + 1), static_cast<float> (h), gpu::rgba8 (255, 255, 255, 40));
        float rows[gpu::kMaxCurve];
        for (int i = 0; i < SpectrumCurve::kPoints; ++i)
            rows[i] = static_cast<float> (levelToY (static_cast<double> (levels[i]), h));
        sb.polyline (rows, SpectrumCurve::kPoints);
        sb.fillGradient (static_cast<float> (h), static_cast<float> (kTopMargin), static_cast<float> (h), gpu::rgba8 (0xff, 0x2e, 0x63, kFillTopAlpha), gpu::rgba8 (0xd3, 0x11, 0x45, kFillBottomAlpha));
        sb.stroke (static_cast<float> (kHalfWidth), gpu::rgba8 (0xff, 0x2e, 0x63, 255));
        return ! sb.overflow;
    }
    uint32_t hash() const noexcept override { return curve_.hash(); }
    const SpectrumCurve& curve() const noexcept { return curve_; }
    void writeStatus (std::ostream& o) const override
    {
        char hash[16];
        std::snprintf (hash, sizeof hash, "%08x", curve_.hash());
        int peak = 0;
        for (int i = 1; i < SpectrumCurve::kPoints; ++i)
            if (curve_.level()[i] > curve_.level()[peak])
                peak = i;
        o << "spectrum_hash: " << hash << "\nspectrum_settled: " << (curve_.settled() ? 1 : 0) << "\nspectrum_max_level: " << curve_.maxLevel() << "\nspectrum_peak_hz: " << SpectrumCurve::pointHz (peak)
          << "\nspectrum_ffts_total: " << counters_.recomputes << "\nspectrum_analysis_ms: " << counters_.lastComputeMs << "\n";
    }
    void writeDump (std::ostream& o) const override
    {
        o << "# view spectrum: index hz level (0 = " << kSpectrumFloorDb << " dB .. 1 = " << kSpectrumCeilingDb << " dB), 256 points, log axis 20 Hz .. 20 kHz\n";
        for (int i = 0; i < SpectrumCurve::kPoints; ++i)
            o << i << " " << SpectrumCurve::pointHz (i) << " " << curve_.level()[i] << "\n";
    }

private:
    SpectrumCurve curve_;
    bool have_ = false;
    double lastMs_ = -1.0;
    std::vector<double> ys_;
};

/** A view without data (a kind that is not built yet): never valid, so the plane stays off. */
class NullView final : public IView
{
public:
    explicit NullView (const char* name) : name_ (name) {}
    const char* name() const noexcept override { return name_; }
    void targets (std::vector<int>&) const override {}
    bool update (const ViewInput&) override { ++counters_.updates; return false; }
    bool valid() const noexcept override { return false; }
    void render (uint32_t*, int, int) override {}
    uint32_t hash() const noexcept override { return 0; }
    void writeStatus (std::ostream&) const override {}
    void writeDump (std::ostream&) const override {}

private:
    const char* name_;
};

/** An ICurveRenderer that draws nothing: for the code that only asks a FilterView for its readout targets. */
struct FilterRendererStub final : ICurveRenderer
{
    void render (const float*, int, uint32_t*, int, int, uint32_t, const Rect*, int) override {}
};

}} // namespace synthyboy_mpc::overlay
