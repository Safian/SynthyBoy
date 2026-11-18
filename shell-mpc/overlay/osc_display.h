// SynthyBoy - MPC overlay: the DISPLAY of the oscillator UI, shared by the Play page (the compact view and the full screen of the oscillator third) and the pages of the OSC tab (osc_page.h): the data of
// the picture of the instance a selector names (Sub, A, B, C, Noise) and its drawing in a rectangle (ui/display.h). The data: the cycle of a wavetable with the frames behind it and a faint spectrum, the
// min / max overview of a sample, multisample or granular oscillator, the colour of the noise, the waveform of the sub oscillator; and the live read-outs of the engine (the read heads and grains,
// the phases of the sub, the read head of a sample noise). One implementation: nothing of it is kept twice (docs/MPC.md "Oscillator UI").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../ui/display.h"
#include "../ui/theme.h"
#include "curve_source.h"
#include "gpu/scene.h"
#include "play_osc.h"
#include "view.h"
#include "views.h"
#include "synthyboy/osc_cycle.h"

namespace synthyboy_mpc { namespace overlay {

class OscDisplay
{
public:
    static constexpr int kOscs = 3;                 // the wavetable oscillators of the own engine (their readout targets: 4 each)
    static constexpr int kDisplayPoints = 256;      // the points of a min / max overview (two polylines of this many vertices)

    explicit OscDisplay (double fixedCostMs = -1.0)
    {
        budget_.fixedCostMs = fixedCostMs;
        cycle_.assign (synthyboy::OscCycle::kSamples, 0.0f);
        cycleMax_.assign (synthyboy::OscCycle::kSamples, 0.0f);
        for (int o = 0; o < kOscs; ++o)
            synthyboy::OscCycle::readoutIndices (o + 1, targetIdx_[o]);
    }

    /** The table indices of the readout of the three oscillators (12 values, 4 each: frame, the two warp amounts, the morph amount): what a view that shows this display lists as its targets. */
    void targets (std::vector<int>& out) const
    {
        for (int o = 0; o < kOscs; ++o)
            for (int i = 0; i < 4; ++i)
                out.push_back (targetIdx_[o][i]);
    }

    /** Reads the instance for the selector value `sel` (1 Sub .. 5 Noise). `targets`: the readout values of targets () (null: none yet); `live`: the engine's live values (null: none yet). true: the picture changed. */
    bool update (const IInstanceView& inst, int sel, const float* targets, const synthyboy::LiveValues* live, double nowMs, ViewCounters& counters)
    {
        bool changed = false;
        sel = std::clamp (sel, 1, playosc::kInstances);
        std::shared_ptr<const synthyboy::StateContent> content = inst.content();
        const synthyboy::IContentStore* store = inst.contentStore();
        const double bpm = std::round (inst.bpm() * 10.0) / 10.0;
        const bool newContent = content != content_ || store != store_;
        if (sel != sel_ || ! group_)
        {
            sel_ = sel;
            group_ = std::make_unique<ParamGroup> (sel_ == playosc::kSub ? std::string ("sub.") : (sel_ == playosc::kNoise ? std::string ("noise.") : std::string ("osc") + std::to_string (playosc::oscNumber (sel_)) + "."));
            forced_ = true;
            have_ = false;
            changed = true;
        }
        if (ui::theme::detailLevel() != detailSeen_)   // the level of detail changed: the picture of the oscillator is made again (the stack, the spectrum)
        {
            detailSeen_ = ui::theme::detailLevel();
            forced_ = true;
            have_ = false;
            changed = true;
        }
        if (sel_ == playosc::kSub)
        {
            const float wave = group_->value ("sub.waveform"), phase = group_->value ("sub.phase");
            const bool data = group_->read (inst) || ! have_ || forced_;
            if (data)
            {
                fillSub (static_cast<int> (std::lround (wave)), phase);
                stackN_ = 0;
                computeSpectrum();
                kind_ = synthyboy::OscCycle::Kind::wavetable;
                res_ = synthyboy::OscCycle::Result();
                res_.kind = kind_;
                group_->accept();
                have_ = true;
                forced_ = false;
                changed = true;
                dataMs_ = nowMs;
            }
        }
        else
        {
            if (! oc_)
                oc_ = std::make_unique<synthyboy::OscCycle>();
            const int osc = sel_ == playosc::kNoise ? 4 : playosc::oscNumber (sel_);
            float t[4] = { -1, -1, -1, -1 };
            if (targets != nullptr && osc <= kOscs)
                for (int i = 0; i < 4; ++i)
                    t[i] = std::round (targets[(osc - 1) * 4 + i] * 100.0f) / 100.0f;
            const bool data = group_->read (inst) || newContent || std::memcmp (t, lastTargets_, sizeof t) != 0;
            if ((data || ! have_) && (forced_ || ! have_ || budget_.due (nowMs)))
            {
                const auto t0 = std::chrono::steady_clock::now();
                if (newContent)
                    oc_->setContent (content != nullptr ? *content : synthyboy::StateContent {}, store);
                synthyboy::OscCycle::Request req;
                req.osc = osc;
                req.bpm = bpm;
                if (targets != nullptr && osc <= kOscs)
                {
                    req.framePercent = t[0];
                    req.warpAmountPercent[0] = t[1];
                    req.warpAmountPercent[1] = t[2];
                    req.morphAmountPercent = t[3];
                }
                const bool wt = osc != 4 && group_->value ((std::string ("osc") + std::to_string (osc) + ".type").c_str()) < 0.5f;
                displayPoints_ = wt ? synthyboy::OscCycle::kSamples : kDisplayPoints;
                res_ = oc_->computeDisplay (group_->values(), req, cycle_.data(), cycleMax_.data(), displayPoints_);
                kind_ = res_.kind;
                // an oscillator that is switched off: the picture of its current content (the table, the sample), drawn dim with a small OFF tag, not an empty display (`kind ()` stays off)
                ghost_ = synthyboy::OscCycle::Kind::off;
                if (kind_ == synthyboy::OscCycle::Kind::off && osc >= 1 && osc <= kOscs)
                {
                    const synthyboy::ParamInfo* on = synthyboy::findParamByKey ((std::string ("osc") + std::to_string (osc) + ".on").c_str());
                    if (on != nullptr)
                    {
                        synthyboy::ParamValues shown = group_->values();
                        shown.plain[synthyboy::findParamIndex (on->id)] = 1.0f;
                        const synthyboy::OscCycle::Result g = oc_->computeDisplay (shown, req, cycle_.data(), cycleMax_.data(), displayPoints_);
                        if (g.kind == synthyboy::OscCycle::Kind::wavetable || g.kind == synthyboy::OscCycle::Kind::sample || g.kind == synthyboy::OscCycle::Kind::multisample || g.kind == synthyboy::OscCycle::Kind::granular || g.kind == synthyboy::OscCycle::Kind::spectral)
                        {
                            res_ = g;
                            ghost_ = g.kind;
                        }
                    }
                }
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
                        oc_->computeDisplay (group_->values(), rq, stack_[static_cast<size_t> (k)].data(), cycleMax_.data(), displayPoints_);
                    }
                    stackN_ = kStackFrames;
                }
                computeSpectrum();
                budget_.spent (nowMs, msSince (t0));
                counters.lastComputeMs = budget_.costMs;
                ++counters.recomputes;
                group_->accept();
                std::memcpy (lastTargets_, t, sizeof t);
                have_ = true;
                forced_ = false;
                changed = true;
                dataMs_ = nowMs;
            }
        }
        content_ = content;
        store_ = store;
        // ---- the live read-outs of the engine: the read heads of the newest voices (sample, multisample, the scan of a granular cloud), the grains that sound, the phases of the sub, the read head of a sample noise
        {
            Live nl;
            const int o = playosc::oscNumber (sel_);
            if (live != nullptr)
            {
                if (o >= 1 && o <= kOscs && (kind_ == synthyboy::OscCycle::Kind::sample || kind_ == synthyboy::OscCycle::Kind::multisample || kind_ == synthyboy::OscCycle::Kind::granular))
                {
                    for (int v = 0; v < ui::display::Data::kMaxHeads; ++v)
                        nl.heads[v] = live->voicePos[o - 1][v];
                    if (kind_ == synthyboy::OscCycle::Kind::granular)
                    {
                        nl.numGrains = std::min (live->numGrains[o - 1], static_cast<int> (ui::display::Data::kMaxGrains));
                        for (int g = 0; g < nl.numGrains; ++g)
                        {
                            nl.grainPos[g] = live->grainPos[o - 1][g];
                            nl.grainLife[g] = live->grainLife[o - 1][g];
                        }
                    }
                }
                if (sel_ == playosc::kSub)
                    for (int v = 0; v < ui::display::Data::kMaxHeads; ++v)
                        nl.phases[v] = live->subPhase[v];
                if (sel_ == playosc::kNoise && kind_ == synthyboy::OscCycle::Kind::noise)
                    nl.noise = live->noisePos;
            }
            if (nl.differs (live_))
            {
                live_ = nl;
                changed = true;
            }
        }
        const bool hot = nowMs - dataMs_ < 350.0;
        if (hot != hot_)
        {
            hot_ = hot;
            changed = true;   // the picture is described again (cached cell or not)
        }
        return changed;
    }

    bool valid() const noexcept { return have_; }
    int selection() const noexcept { return sel_; }
    synthyboy::OscCycle::Kind kind() const noexcept { return kind_; }
    const std::vector<float>& cycle() const noexcept { return cycle_; }
    /** The hash of the data (the status file): the selector and the cycle. */
    uint32_t hash (uint32_t h = 2166136261u) const noexcept
    {
        h = fnv (h, sel_);
        for (float v : cycle_)
            h = fnv (h, static_cast<int32_t> (std::lround (static_cast<double> (v) * 1000.0)));
        return h;
    }
    float framePosition() const noexcept { return lastTargets_[0]; }

    /** The picture of the oscillator in a rectangle of the canvas: what the data of OscCycle::computeDisplay is (docs/MPC.md "Oscillator UI"), drawn by ui/display.h. `live`: the running parts only. `plotArea`: the rectangle the picture is drawn in (the compact view's, below its header band); empty = the display rectangle. */
    void draw (gpu::SceneBuilder& sb, const ui::Rect& r, bool live, const ui::Rect& plotArea = {}) const
    {
        using K = synthyboy::OscCycle::Kind;
        ui::display::Data d;
        d.area = plotArea;   // the compact view of the Play page: the picture lies below the header band that holds the buttons (OscUi::plotArea)
        d.dim = kind_ == K::off && ghost_ != K::off;   // switched off, with the picture of its content under a veil
        const K kind = d.dim ? ghost_ : kind_;
        d.colour = ui::theme::instanceColour (sel_ - 1);
        if (kind_ == K::off)   // a switched off oscillator: the picture in the grey of the inactive look (ui::Pen::inactiveGrey), as the controls under it
            d.colour = ui::Pen::inactiveGrey ((d.colour << 8) | 255u) >> 8;
        d.n = std::min (displayPoints_, static_cast<int> (cycle_.size()));
        d.cycle = cycle_.data();
        if (kind == K::wavetable)
        {
            d.kind = ui::display::Data::Kind::Wave;
            if (sel_ != playosc::kSub)
            {
                const std::string base = "osc" + std::to_string (playosc::oscNumber (sel_)) + ".unison.";
                d.voices = std::clamp (static_cast<int> (group_->value ((base + "voices").c_str()) + 0.5f), 1, 16);
                d.detune = std::clamp (group_->value ((base + "detune").c_str()) / 100.0f, 0.0f, 1.0f);
                d.blend = std::clamp (group_->value ((base + "blend").c_str()) / 100.0f, 0.0f, 1.0f);
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
        else if (kind == K::sample || kind == K::multisample || kind == K::granular)
        {
            d.kind = ui::display::Data::Kind::MinMax;
            d.cycle = cycle_.data();
            d.cycleMax = cycleMax_.data();
            d.loopOn = res_.loopOn;
            d.loopStart = res_.loopStart;
            d.loopEnd = res_.loopEnd;
            d.grain = kind == K::granular && res_.grainSize > 0.0f;
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
        else if (kind == K::spectral)
            d.kind = ui::display::Data::Kind::Spectrum;
        else if (sel_ == playosc::kNoise && (kind == K::noise || kind == K::off))
        {
            // the noise: the picture of its colour (faint while the source is switched off), never the word OFF
            d.kind = ui::display::Data::Kind::Noise;
            d.noiseColour = std::clamp (static_cast<int> (group_->value ("noise.sample") + 0.5f), 0, 8);
            d.dim = kind_ == K::off;
        }
        else
        {
            d.kind = ui::display::Data::Kind::Message;
            d.noiseHead = live_.noise;
            d.message = kind == K::noise ? "NOISE" : (kind == K::noSample ? "NO SAMPLE" : (kind == K::off ? "OFF" : ""));
        }
        if (live)
            ui::display::drawLive (sb, r, d);
        else
            ui::display::draw (sb, r, d, ! hot_);   // while the data changes every picture (a knob dragged along the wavetable) it is drawn with the picture, not into a layer
    }

private:
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

    int sel_ = 0;
    std::unique_ptr<ParamGroup> group_;
    ComputeBudget budget_;
    bool have_ = false, forced_ = true, hot_ = false;
    double dataMs_ = -1.0e9;   // when the data last changed
    int targetIdx_[kOscs][4] = {};
    std::unique_ptr<synthyboy::OscCycle> oc_;
    std::shared_ptr<const synthyboy::StateContent> content_;
    const synthyboy::IContentStore* store_ = nullptr;
    std::vector<float> cycle_, cycleMax_, spectrum_;
    std::vector<std::vector<float>> stack_;
    int stackN_ = 0, detailSeen_ = 1;
    int displayPoints_ = synthyboy::OscCycle::kSamples;
    synthyboy::OscCycle::Result res_;
    synthyboy::OscCycle::Kind kind_ = synthyboy::OscCycle::Kind::other, ghost_ = synthyboy::OscCycle::Kind::off;   // ghost_: the kind of the content an oscillator that is off still shows (off: none)
    float lastTargets_[4] = { -1, -1, -1, -1 };
    Live live_;
};

}} // namespace synthyboy_mpc::overlay
