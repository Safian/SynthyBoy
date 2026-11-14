// SynthyBoy - MPC overlay: the curve source. Turns "the knobs of Filter 1 plus the live (modulated) cutoff and resonance" into 256 dB values, as cheaply as the picture allows.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Inputs, read through IInstanceView (the shell implements it with paramMirror and the engine's readout): the filter1.* parameters and the live cutoff (Hz) / resonance (percent)
// of the voice that sounded last (the plain knob values when nothing sounds).
//
// What is recomputed, and when:
//  - Nothing changed (the filter1.* parameters except the cutoff, the live resonance, the live cutoff) -> nothing: update () returns false, no new picture.
//  - Anything but the cutoff changed, or the cutoff moved: a FULL computation (FilterResponse::compute, the real filter code run on an impulse, 1 .. 2 ms on a Mac) as soon as the
//    budget allows it: the full computations together may use at most kBudget of one core (the cost of the last computation is measured; the gap to the next one is
//    cost / budget, at least kMinGapMs). When it does not allow it yet:
//  - only the cutoff moved: the last exact curve is TRANSLATED along the log frequency axis by the cutoff ratio (the response of every model translates with the cutoff; the error
//    measured on the standard models is below 0.01 octave) and shown at once; the full computation follows as soon as the budget allows, so the picture converges to the
//    exact curve when the motion stops (the next update finds the live cutoff different from the one of the last full computation, which is the whole schedule).
//  - something else changed: the old picture stays until the budget allows the full computation (at most kMinGapMs plus the gap above).
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "synthyboy/filter_response.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"

namespace synthyboy_mpc { namespace overlay {

/** One plugin instance as the overlay sees it. Both methods are called from the overlay thread while the instance is registered; they must be lock free. */
class IInstanceView
{
public:
    virtual ~IInstanceView() = default;
    virtual float param (int tableIndex) const noexcept = 0;
    /** The modulated cutoff (Hz) and resonance (percent) of filter 1. false: the engine has not written live values yet (the knob values are used). */
    virtual bool live (float& cutoffHz, float& resonancePercent) const noexcept = 0;
    /** The sample rate of the instance (Hz). */
    virtual double sampleRate() const noexcept = 0;
};

class CurveSource
{
public:
    static constexpr int kPoints = 256;
    static constexpr double kFMin = 20.0, kFMax = 20000.0;
    static constexpr double kBudget = 0.08;        // share of one core the full computations may use while the curve moves
    static constexpr double kMinGapMs = 15.0;
    static constexpr double kEpsilonOctaves = 0.002; // a cutoff move below this is not a move (0.05 of a picture point)

    struct Config
    {
        double sampleRate = 48000.0;
        double fixedCostMs = -1.0;   // >= 0: use this as the cost of a full computation instead of measuring it (tests)
    };

    struct Stats
    {
        double lastComputeMs = 0.0;
        unsigned long fullComputes = 0, translations = 0;
        float cutoffHz = 0.0f, resonancePercent = 0.0f;   // the live values of the last update
    };

    explicit CurveSource (const Config& config) : config_ (config) { findIndices(); }

    /** Reads the inputs and refreshes the curve when it should change. Returns true when curve () is a new picture. `nowMs`: any monotonic clock in milliseconds. */
    bool update (const IInstanceView& view, double nowMs)
    {
        if (! response_)
            response_ = std::make_unique<synthyboy::FilterResponse> (config_.sampleRate);   // allocates; first call only
        for (int idx : relevant_)
            values_.plain[idx] = view.param (idx);
        float cutoff = values_.plain[cutoffIdx_], resonance = values_.plain[resonanceIdx_];
        float liveCut = 0.0f, liveRes = 0.0f;
        if (view.live (liveCut, liveRes) && liveCut > 0.0f && liveRes >= 0.0f)
        {
            cutoff = liveCut;
            resonance = liveRes;
        }
        cutoff = std::clamp (cutoff, static_cast<float> (kFMin), static_cast<float> (kFMax));   // a modulated cutoff can go far beyond the knob (84 kHz seen); the voice's filter does not follow it above the audible range
        stats_.cutoffHz = cutoff;
        stats_.resonancePercent = resonance;

        bool structural = ! haveCurve_ || std::fabs (resonance - fullResonance_) > 0.01f;
        for (size_t k = 0; ! structural && k < fullValues_.size(); ++k)
            structural = fullValues_[k] != values_.plain[relevant_[k]] && relevant_[k] != cutoffIdx_;
        const bool moved = haveCurve_ && std::fabs (std::log2 (static_cast<double> (cutoff) / static_cast<double> (fullCutoff_))) > kEpsilonOctaves;
        if (! structural && ! moved)
            return false;

        const double gap = std::max (kMinGapMs, (config_.fixedCostMs >= 0.0 ? config_.fixedCostMs : costMs_) / kBudget);
        if (! haveCurve_ || nowMs - lastFullMs_ >= gap)
            return fullCompute (cutoff, resonance, nowMs);
        if (structural)
            return false;   // the old picture stays until the budget allows the computation
        // only the cutoff moved: slide the last exact curve
        const double shiftPoints = std::log2 (static_cast<double> (cutoff) / static_cast<double> (fullCutoff_)) * (kPoints - 1) / std::log2 (kFMax / kFMin);
        if (haveShown_ && std::fabs (shiftPoints - shownShift_) < 0.02)
            return false;
        translate (shiftPoints);
        shownShift_ = shiftPoints;
        haveShown_ = true;
        ++stats_.translations;
        return true;
    }

    const float* curve() const noexcept { return shown_; }
    int points() const noexcept { return kPoints; }
    bool valid() const noexcept { return haveCurve_; }
    const Stats& stats() const noexcept { return stats_; }

    /** FNV-1a of the shown values rounded to 0.01 dB, min / max over the points (the status file). */
    uint32_t hash() const noexcept
    {
        uint32_t h = 2166136261u;
        for (int i = 0; i < kPoints; ++i)
        {
            const int32_t v = static_cast<int32_t> (std::lround (static_cast<double> (shown_[i]) * 100.0));
            for (int b = 0; b < 4; ++b)
                h = (h ^ ((static_cast<uint32_t> (v) >> (8 * b)) & 255u)) * 16777619u;
        }
        return h;
    }
    void minMax (float& lo, float& hi) const noexcept
    {
        lo = hi = shown_[0];
        for (int i = 1; i < kPoints; ++i)
        {
            lo = std::min (lo, shown_[i]);
            hi = std::max (hi, shown_[i]);
        }
    }
    /** The exact curve of the last full computation (the status dump and the tests). */
    const float* exactCurve() const noexcept { return exact_; }
    double fullCutoffHz() const noexcept { return fullCutoff_; }

private:
    void findIndices()
    {
        const synthyboy::ParamInfo* table = synthyboy::paramTable();
        const size_t count = synthyboy::paramCount();
        for (size_t i = 0; i < count; ++i)
            if (std::strncmp (table[i].key, "filter1.", 8) == 0)
                relevant_.push_back (static_cast<int> (i));
        const synthyboy::ParamInfo* c = synthyboy::findParamByKey ("filter1.cutoff");
        const synthyboy::ParamInfo* r = synthyboy::findParamByKey ("filter1.resonance");
        cutoffIdx_ = c ? static_cast<int> (c - table) : 0;
        resonanceIdx_ = r ? static_cast<int> (r - table) : 0;
        values_ = synthyboy::ParamValues::defaults();
        fullValues_.assign (relevant_.size(), 0.0f);
    }

    bool fullCompute (float cutoff, float resonance, double nowMs)
    {
        synthyboy::FilterResponse::Request req;
        req.filter = 1;
        req.cutoffHz = cutoff;
        req.resonancePercent = resonance;
        req.fMin = static_cast<float> (kFMin);
        req.fMax = static_cast<float> (kFMax);
        const auto t0 = std::chrono::steady_clock::now();
        response_->compute (values_, req, exact_, kPoints);   // false (filter off): 0 dB everywhere, which is what the picture shows
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        stats_.lastComputeMs = ms;
        costMs_ = haveCurve_ ? 0.7 * costMs_ + 0.3 * ms : ms;
        for (size_t k = 0; k < relevant_.size(); ++k)
            fullValues_[k] = values_.plain[relevant_[k]];
        fullCutoff_ = cutoff;
        fullResonance_ = resonance;
        lastFullMs_ = nowMs;
        haveCurve_ = true;
        std::memcpy (shown_, exact_, sizeof shown_);
        shownShift_ = 0.0;
        haveShown_ = true;
        ++stats_.fullComputes;
        return true;
    }

    /** shown[i] = exact[i - shift] (linear interpolation; the edges keep their value). */
    void translate (double shift) noexcept
    {
        for (int i = 0; i < kPoints; ++i)
        {
            const double src = std::clamp (static_cast<double> (i) - shift, 0.0, static_cast<double> (kPoints - 1));
            int j = static_cast<int> (src);
            if (j >= kPoints - 1)
                j = kPoints - 2;
            const double f = src - j;
            shown_[i] = static_cast<float> (exact_[j] * (1.0 - f) + exact_[j + 1] * f);
        }
    }

    Config config_;
    std::unique_ptr<synthyboy::FilterResponse> response_;
    std::vector<int> relevant_;
    std::vector<float> fullValues_;
    synthyboy::ParamValues values_;
    int cutoffIdx_ = 0, resonanceIdx_ = 0;
    float exact_[kPoints] = {}, shown_[kPoints] = {};
    bool haveCurve_ = false, haveShown_ = false;
    double fullCutoff_ = 1000.0, fullResonance_ = 0.0, lastFullMs_ = 0.0, costMs_ = 0.0, shownShift_ = 0.0;
    Stats stats_;
};

}} // namespace synthyboy_mpc::overlay
