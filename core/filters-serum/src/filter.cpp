// SynthyBoy - core/filters-serum: Filter implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/filters/filter.h"
#include "filter_dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace synthyboy::filters
{

class FilterImpl
{
public:
    FilterImpl() = default;
    ~FilterImpl() = default;

    void prepare (double sampleRate)
    {
        sampleRate_ = std::max (22050.0, std::min (sampleRate, 192000.0));
        const auto srFloat = static_cast<float> (sampleRate_);

        // Allocate maximum delay buffers (min cutoff 20 Hz -> ~9600 samples at 192 kHz)
        const size_t maxDelaySamples = static_cast<size_t> (sampleRate_ * 0.06) + 64; // ~11500 samples
        combDelay1_.prepare (maxDelaySamples);
        combDelay2_.prepare (maxDelaySamples);

        // Diffuser delays (8 stages with staggered lengths)
        static constexpr float kDiffuserMs[8] = { 1.2f, 2.5f, 4.1f, 6.7f, 9.3f, 13.1f, 17.5f, 23.3f };
        for (int i = 0; i < 8; ++i)
        {
            const size_t diffLen = static_cast<size_t> (kDiffuserMs[i] * 0.001f * srFloat) + 16;
            diffuserDelays_[i].prepare (diffLen);
        }

        // Reverb comb & allpass delays
        static constexpr float kReverbCombMs[4] = { 29.7f, 37.1f, 41.3f, 43.7f };
        for (int i = 0; i < 4; ++i)
        {
            const size_t combLen = static_cast<size_t> (kReverbCombMs[i] * 0.001f * srFloat) + 16;
            reverbCombs_[i].prepare (combLen);
        }
        reverbAP_[0].prepare (static_cast<size_t> (5.1f * 0.001f * srFloat) + 16, 0.7f);
        reverbAP_[1].prepare (static_cast<size_t> (12.7f * 0.001f * srFloat) + 16, 0.7f);

        reset();
    }

    void reset() noexcept
    {
        svf1_.reset();
        svf2_.reset();
        onePole1_.reset();
        onePole2_.reset();
        ladder_.reset();
        diodeLadder_.reset();
        biquad1_.reset();
        biquad2_.reset();

        combDelay1_.reset();
        combDelay2_.reset();
        for (auto& d : diffuserDelays_)
            d.reset();
        for (auto& rc : reverbCombs_)
            rc.reset();
        reverbAP_[0].reset();
        reverbAP_[1].reset();

        for (auto& s : formantSVF_)
            s.reset();

        waspSVF_.reset();
        combFeedbackLP_.reset();
        combFeedbackHP_.reset();
        for (auto& ap : phaserAP_)
            ap.reset();

        ringModPhase1_ = 0.0f;
        ringModPhase2_ = 0.0f;
        shPhase_ = 1.0f;
        shHeld_ = 0.0f;

        smoothedCutoff_ = std::max (20.0f, std::min (params_.cutoff, static_cast<float> (sampleRate_ * 0.49)));
        smoothedRes_ = std::max (0.0f, std::min (params_.resonance, 1.0f));
    }

    void setType (int type) noexcept
    {
        if (type >= 0 && type < kFilterTypeCount)
            type_ = static_cast<FilterType> (type);
    }

    int getType() const noexcept
    {
        return static_cast<int> (type_);
    }

    void setParams (const FilterParams& params) noexcept
    {
        params_ = params;
    }

    const FilterParams& getParams() const noexcept
    {
        return params_;
    }

    float processSample (float in) noexcept
    {
        in = sanitize (in);

        // Smooth cutoff and resonance towards target
        const auto srFloat = static_cast<float> (sampleRate_);
        const float targetCutoff = std::max (20.0f, std::min (params_.cutoff, srFloat * 0.49f));
        const float targetRes = std::max (0.0f, std::min (params_.resonance, 1.0f));

        // One-pole smoother (~30 Hz tracking rate)
        const float smoothCoeff = std::min (0.05f, 180.0f / srFloat);
        smoothedCutoff_ += smoothCoeff * (targetCutoff - smoothedCutoff_);
        smoothedRes_ += smoothCoeff * (targetRes - smoothedRes_);

        const float out = processInternal (in, smoothedCutoff_, smoothedRes_);
        return sanitize (out);
    }

    void processBlock (const float* in, float* out, int numSamples,
                       const float* cutoffMod, const float* resMod) noexcept
    {
        const auto srFloat = static_cast<float> (sampleRate_);
        const float smoothCoeff = std::min (0.05f, 180.0f / srFloat);
        const int hold = params_.coefHold;

        for (int i = 0; i < numSamples; ++i)
        {
            float targetCutoff = params_.cutoff;
            if (cutoffMod != nullptr)
                targetCutoff = cutoffMod[i];
            targetCutoff = std::max (20.0f, std::min (targetCutoff, srFloat * 0.49f));

            float targetRes = params_.resonance;
            if (resMod != nullptr)
                targetRes = resMod[i];
            targetRes = std::max (0.0f, std::min (targetRes, 1.0f));

            smoothedCutoff_ += smoothCoeff * (targetCutoff - smoothedCutoff_);
            smoothedRes_ += smoothCoeff * (targetRes - smoothedRes_);
            if (hold <= 1 || (i % hold) == 0)
            {
                heldCutoff_ = smoothedCutoff_;
                heldRes_ = smoothedRes_;
            }

            const float sampleIn = sanitize (in[i]);
            out[i] = sanitize (processInternal (sampleIn, hold <= 1 ? smoothedCutoff_ : heldCutoff_, hold <= 1 ? smoothedRes_ : heldRes_));
        }
    }

private:
    float processInternal (float in, float cutoff, float res) noexcept;

    // A small cache of the transcendental functions of the coefficients (the same argument gives the same result: nothing changes in the output). Cutoff and resonance move
    // slowly or not at all, so most samples ask for the arguments of the sample before.
    struct MemoEntry { float x = 0.0f, y = 0.0f; bool valid = false; };
    static constexpr int kMemo = 16;
    MemoEntry tanMemo_[kMemo], pow2Memo_[kMemo], pow10Memo_[kMemo];
    static int memoSlot (float x) noexcept
    {
        uint32_t u;
        std::memcpy (&u, &x, 4);
        return static_cast<int> ((u ^ (u >> 11) ^ (u >> 19)) % kMemo);
    }
    float tanM (float x) noexcept
    {
        MemoEntry& e = tanMemo_[memoSlot (x)];
        if (!(e.valid && e.x == x))
        {
            e.x = x;
            e.y = std::tan (x);
            e.valid = true;
        }
        return e.y;
    }
    float pow2M (float x) noexcept
    {
        MemoEntry& e = pow2Memo_[memoSlot (x)];
        if (!(e.valid && e.x == x))
        {
            e.x = x;
            e.y = std::pow (2.0f, x);
            e.valid = true;
        }
        return e.y;
    }
    float pow10M (float x) noexcept
    {
        MemoEntry& e = pow10Memo_[memoSlot (x)];
        if (!(e.valid && e.x == x))
        {
            e.x = x;
            e.y = std::pow (10.0f, x);
            e.valid = true;
        }
        return e.y;
    }

    double sampleRate_ = 44100.0;
    FilterType type_ = FilterType::MGLow24;
    FilterParams params_;

    float smoothedCutoff_ = 1000.0f;
    float smoothedRes_ = 0.0f;
    float heldCutoff_ = 1000.0f, heldRes_ = 0.0f; // what processBlock runs on while coefHold > 1

    // Filter DSP modules
    SVFTPT svf1_;
    SVFTPT svf2_;
    OnePoleTPT onePole1_;
    OnePoleTPT onePole2_;
    Ladder4Pole ladder_;
    DiodeLadder diodeLadder_;
    Biquad biquad1_;
    Biquad biquad2_;

    DelayLine combDelay1_;
    DelayLine combDelay2_;
    DelayLine diffuserDelays_[8];
    DelayLine reverbCombs_[4];
    AllpassDiffuser reverbAP_[2];

    SVFTPT formantSVF_[3];
    SVFTPT waspSVF_;
    OnePoleTPT combFeedbackLP_;
    OnePoleTPT combFeedbackHP_;
    OnePoleTPT phaserAP_[6];

    float ringModPhase1_ = 0.0f;
    float ringModPhase2_ = 0.0f;
    float shPhase_ = 0.0f;
    float shHeld_ = 0.0f;
};

float FilterImpl::processInternal (float in, float cutoff, float res) noexcept
{
    const auto srFloat = static_cast<float> (sampleRate_);
    const float g = tanM (kPi * cutoff / srFloat);
    const float R = 1.0f - 0.96f * res;
    const float drive = std::max (0.0f, std::min (params_.drive, 1.0f));
    const float var = std::max (0.0f, std::min (params_.var, 1.0f));
    const float blend = std::max (0.0f, std::min (params_.blend, 1.0f));
    const float mix = std::max (0.0f, std::min (params_.mix, 1.0f));

    // Input drive
    const float driveGain = 1.0f + 2.5f * drive;
    const float drivenIn = softClip (driveGain * in);

    float rawOut = 0.0f;

    switch (type_)
    {
    // ==========================================
    // 1. NORMAL FAMILY (18 Types)
    // ==========================================
    case FilterType::MGLow6:
    case FilterType::MGLow12:
    case FilterType::MGLow18:
    case FilterType::MGLow24:
    {
        float y1 = 0.0f, y2 = 0.0f, y3 = 0.0f, y4 = 0.0f;
        ladder_.process (drivenIn, g, res, var, drive, y1, y2, y3, y4);
        if (type_ == FilterType::MGLow6)
            rawOut = y1;
        else if (type_ == FilterType::MGLow12)
            rawOut = y2;
        else if (type_ == FilterType::MGLow18)
            rawOut = y3;
        else
            rawOut = y4;
        break;
    }
    case FilterType::Low6:
    {
        rawOut = onePole1_.processLPOnly (drivenIn, g);
        break;
    }
    case FilterType::Low12:
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, var, lp, bp, hp, notch, peak);
        rawOut = lp;
        break;
    }
    case FilterType::Low18:
    {
        const float stage1 = onePole1_.processLPOnly (drivenIn, g);
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (stage1, g, R, var, lp, bp, hp, notch, peak);
        rawOut = lp;
        break;
    }
    case FilterType::Low24:
    {
        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
        const float R1 = std::sqrt (R);
        svf1_.process (drivenIn, g, R1, var, lp1, bp1, hp1, notch1, peak1);
        svf2_.process (lp1, g, R1, var, lp2, bp2, hp2, notch2, peak2);
        rawOut = lp2;
        break;
    }
    case FilterType::High6:
    {
        rawOut = onePole1_.processHPOnly (drivenIn, g);
        break;
    }
    case FilterType::High12:
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, var, lp, bp, hp, notch, peak);
        rawOut = hp;
        break;
    }
    case FilterType::High18:
    {
        const float stage1 = onePole1_.processHPOnly (drivenIn, g);
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (stage1, g, R, var, lp, bp, hp, notch, peak);
        rawOut = hp;
        break;
    }
    case FilterType::High24:
    {
        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
        const float R1 = std::sqrt (R);
        svf1_.process (drivenIn, g, R1, var, lp1, bp1, hp1, notch1, peak1);
        svf2_.process (hp1, g, R1, var, lp2, bp2, hp2, notch2, peak2);
        rawOut = hp2;
        break;
    }
    case FilterType::Band12:
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, var, lp, bp, hp, notch, peak);
        rawOut = bp;
        break;
    }
    case FilterType::Band24:
    {
        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
        const float R1 = std::sqrt (R);
        svf1_.process (drivenIn, g, R1, var, lp1, bp1, hp1, notch1, peak1);
        svf2_.process (bp1, g, R1, var, lp2, bp2, hp2, notch2, peak2);
        rawOut = bp2;
        break;
    }
    case FilterType::Peak12:
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, var, lp, bp, hp, notch, peak);
        rawOut = drivenIn + softClip ((1.0f + 2.0f * res) * bp);
        break;
    }
    case FilterType::Peak24:
    {
        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
        svf1_.process (drivenIn, g, R, var, lp1, bp1, hp1, notch1, peak1);
        const float st1 = softClip (drivenIn + (1.0f + 1.5f * res) * bp1);
        svf2_.process (st1, g, R, var, lp2, bp2, hp2, notch2, peak2);
        rawOut = softClip (st1 + (1.0f + 1.5f * res) * bp2);
        break;
    }
    case FilterType::Notch12:
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, var, lp, bp, hp, notch, peak);
        rawOut = notch;
        break;
    }
    case FilterType::Notch24:
    {
        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
        svf1_.process (drivenIn, g, R, var, lp1, bp1, hp1, notch1, peak1);
        svf2_.process (notch1, g, R, var, lp2, bp2, hp2, notch2, peak2);
        rawOut = notch2;
        break;
    }

    // ==========================================
    // 2. MULTI FAMILY (16 Types)
    // ==========================================
    case FilterType::LH:
    case FilterType::LB:
    case FilterType::LP:
    case FilterType::LN:
    case FilterType::HB:
    case FilterType::HP:
    case FilterType::HN:
    case FilterType::BP:
    case FilterType::BN:
    case FilterType::PP:
    case FilterType::PN:
    case FilterType::NN:
    {
        // Second cutoff shifted by Var (FREQ: -4 to +4 octaves)
        const float fc2 = std::max (20.0f, std::min (cutoff * pow2M ((var - 0.5f) * 8.0f), srFloat * 0.49f));
        const float g2 = tanM (kPi * fc2 / srFloat);

        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
        svf1_.process (drivenIn, g, R, 0.0f, lp1, bp1, hp1, notch1, peak1);

        // Stage 1 output select
        float out1 = lp1;
        if (type_ == FilterType::HB || type_ == FilterType::HP || type_ == FilterType::HN)
            out1 = hp1;
        else if (type_ == FilterType::BP || type_ == FilterType::BN)
            out1 = bp1;
        else if (type_ == FilterType::PP || type_ == FilterType::PN)
            out1 = softClip (drivenIn + (1.0f + 1.5f * res) * bp1);
        else if (type_ == FilterType::NN)
            out1 = notch1;
        out1 = softClip (out1);

        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
        svf2_.process (out1, g2, R, 0.0f, lp2, bp2, hp2, notch2, peak2);

        // Stage 2 output select
        float out2 = hp2;
        if (type_ == FilterType::LB || type_ == FilterType::HB)
            out2 = bp2;
        else if (type_ == FilterType::LP || type_ == FilterType::HP || type_ == FilterType::BP || type_ == FilterType::PP)
            out2 = softClip (out1 + (1.0f + 1.5f * res) * bp2);
        else if (type_ == FilterType::LN || type_ == FilterType::HN || type_ == FilterType::BN || type_ == FilterType::PN || type_ == FilterType::NN)
            out2 = notch2;
        out2 = softClip (out2);

        rawOut = (1.0f - 0.5f * blend) * out2 + (0.5f * blend) * out1;
        break;
    }
    case FilterType::LBH:
    case FilterType::LPH:
    case FilterType::LNH:
    case FilterType::BPN:
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, 0.0f, lp, bp, hp, notch, peak);
        const float peakOut = drivenIn + (1.0f + 2.0f * res) * bp;

        float m1 = lp, m2 = bp, m3 = hp;
        if (type_ == FilterType::LPH)
        {
            m2 = peakOut;
        }
        else if (type_ == FilterType::LNH)
        {
            m2 = notch;
        }
        else if (type_ == FilterType::BPN)
        {
            m1 = bp;
            m2 = peakOut;
            m3 = notch;
        }

        if (var < 0.5f)
        {
            const float t = var * 2.0f;
            rawOut = (1.0f - t) * m1 + t * m2;
        }
        else
        {
            const float t = (var - 0.5f) * 2.0f;
            rawOut = (1.0f - t) * m2 + t * m3;
        }
        break;
    }

    // ==========================================
    // 3. FLANGES FAMILY (9 Types)
    // ==========================================
    case FilterType::CmbL:
    case FilterType::FlgL:
    case FilterType::PhsL:
    case FilterType::CmbH:
    case FilterType::FlgH:
    case FilterType::PhsH:
    case FilterType::CmbHL:
    case FilterType::FlgHL:
    case FilterType::PhsHL:
    {
        const bool isComb = (type_ == FilterType::CmbL || type_ == FilterType::CmbH || type_ == FilterType::CmbHL);
        const bool isFlg  = (type_ == FilterType::FlgL || type_ == FilterType::FlgH || type_ == FilterType::FlgHL);

        if (isComb || isFlg)
        {
            float delaySamples = srFloat / std::max (20.0f, cutoff);
            if (isFlg)
            {
                // Flanger range: 0.2 ms to 10 ms
                const float delaySec = 0.0002f + 0.0098f * (1.0f - std::log10 (std::max (20.0f, cutoff) / 20.0f) / 3.0f);
                delaySamples = std::max (2.0f, delaySec * srFloat);
            }

            const float delayed = combDelay1_.read (delaySamples);

            // Filter in feedback path
            float fbFiltered = delayed;
            if (type_ == FilterType::CmbL || type_ == FilterType::FlgL)
            {
                const float fLP = 50.0f + 18000.0f * var;
                const float gLP = tanM (kPi * std::min (fLP, srFloat * 0.49f) / srFloat);
                fbFiltered = combFeedbackLP_.processLPOnly (delayed, gLP);
            }
            else if (type_ == FilterType::CmbH || type_ == FilterType::FlgH)
            {
                const float fHP = 20.0f + 12000.0f * var;
                const float gHP = tanM (kPi * std::min (fHP, srFloat * 0.49f) / srFloat);
                fbFiltered = combFeedbackHP_.processHPOnly (delayed, gHP);
            }
            else // HL
            {
                const float fCenter = cutoff;
                const float bwFactor = 0.1f + 1.8f * var;
                const float fLP = std::min (fCenter * (1.0f + bwFactor), srFloat * 0.49f);
                const float fHP = std::max (20.0f, fCenter / (1.0f + bwFactor));
                const float gLP = tanM (kPi * fLP / srFloat);
                const float gHP = tanM (kPi * fHP / srFloat);
                fbFiltered = combFeedbackHP_.processHPOnly (combFeedbackLP_.processLPOnly (delayed, gLP), gHP);
            }

            const float fbGain = 0.97f * res;
            const float toWrite = drivenIn + softClip (fbGain * fbFiltered);
            combDelay1_.write (toWrite);

            // Comb/Flange wet response: mix of dry and delayed
            rawOut = 0.5f * (drivenIn + delayed);
        }
        else // Phaser (Phs L, Phs H, Phs HL)
        {
            // 6-stage TPT APF phaser
            float ap = drivenIn;
            for (int i = 0; i < 6; ++i)
            {
                float lp = 0.0f, hp = 0.0f;
                phaserAP_[i].processLP (ap, g, lp, hp);
                ap = lp - hp;
            }

            float fbFiltered = ap;
            if (type_ == FilterType::PhsL)
            {
                const float fLP = 50.0f + 18000.0f * var;
                const float gLP = tanM (kPi * std::min (fLP, srFloat * 0.49f) / srFloat);
                fbFiltered = combFeedbackLP_.processLPOnly (ap, gLP);
            }
            else if (type_ == FilterType::PhsH)
            {
                const float fHP = 20.0f + 12000.0f * var;
                const float gHP = tanM (kPi * std::min (fHP, srFloat * 0.49f) / srFloat);
                fbFiltered = combFeedbackHP_.processHPOnly (ap, gHP);
            }
            else // Phs HL
            {
                const float fCenter = cutoff;
                const float bwFactor = 0.1f + 1.8f * var;
                const float fLP = std::min (fCenter * (1.0f + bwFactor), srFloat * 0.49f);
                const float fHP = std::max (20.0f, fCenter / (1.0f + bwFactor));
                const float gLP = tanM (kPi * fLP / srFloat);
                const float gHP = tanM (kPi * fHP / srFloat);
                fbFiltered = combFeedbackHP_.processHPOnly (combFeedbackLP_.processLPOnly (ap, gLP), gHP);
            }

            rawOut = 0.5f * (drivenIn + softClip (fbFiltered * (1.0f + res)));
        }
        break;
    }

    // ==========================================
    // 4. MISC FAMILY (26 Types)
    // ==========================================
    case FilterType::LowEQ6:
    {
        const float gainDb = (var - 0.5f) * 48.0f;
        const float A = pow10M (gainDb / 20.0f);
        const float lp = onePole1_.processLPOnly (drivenIn, g);
        rawOut = drivenIn + (A - 1.0f) * lp;
        break;
    }
    case FilterType::LowEQ12:
    {
        const float gainDb = (var - 0.5f) * 48.0f;
        const float Q = 0.5f + 4.5f * res;
        biquad1_.setLowShelf (cutoff, srFloat, gainDb, Q);
        rawOut = biquad1_.process (drivenIn);
        break;
    }
    case FilterType::BandEQ6:
    {
        const float gainDb = (var - 0.5f) * 48.0f;
        const float A = pow10M (gainDb / 20.0f);
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, 1.0f, 0.0f, lp, bp, hp, notch, peak);
        rawOut = drivenIn + (A - 1.0f) * bp;
        break;
    }
    case FilterType::BandEQ12:
    {
        const float gainDb = (var - 0.5f) * 48.0f;
        const float Q = 0.5f + 9.5f * res;
        biquad1_.setPeakingEQ (cutoff, srFloat, gainDb, Q);
        rawOut = biquad1_.process (drivenIn);
        break;
    }
    case FilterType::HighEQ6:
    {
        const float gainDb = (var - 0.5f) * 48.0f;
        const float A = pow10M (gainDb / 20.0f);
        const float hp = onePole1_.processHPOnly (drivenIn, g);
        rawOut = drivenIn + (A - 1.0f) * hp;
        break;
    }
    case FilterType::HighEQ12:
    {
        const float gainDb = (var - 0.5f) * 48.0f;
        const float Q = 0.5f + 4.5f * res;
        biquad1_.setHighShelf (cutoff, srFloat, gainDb, Q);
        rawOut = biquad1_.process (drivenIn);
        break;
    }
    case FilterType::RingMod:
    {
        ringModPhase1_ += 2.0f * kPi * cutoff / srFloat;
        if (ringModPhase1_ >= 2.0f * kPi)
            ringModPhase1_ -= 2.0f * kPi;
        const float carrier = std::sin (ringModPhase1_);
        rawOut = drivenIn * carrier;
        break;
    }
    case FilterType::RingModX2:
    {
        const float fc2 = std::max (20.0f, std::min (cutoff * pow2M ((var - 0.5f) * 4.0f), srFloat * 0.49f));
        ringModPhase1_ += 2.0f * kPi * cutoff / srFloat;
        if (ringModPhase1_ >= 2.0f * kPi)
            ringModPhase1_ -= 2.0f * kPi;
        ringModPhase2_ += 2.0f * kPi * fc2 / srFloat;
        if (ringModPhase2_ >= 2.0f * kPi)
            ringModPhase2_ -= 2.0f * kPi;
        const float carrier = 0.5f * (std::sin (ringModPhase1_) + std::sin (ringModPhase2_));
        rawOut = drivenIn * carrier;
        break;
    }
    case FilterType::SampHold:
    {
        shPhase_ += cutoff / srFloat;
        if (shPhase_ >= 1.0f)
        {
            shPhase_ -= std::floor (shPhase_);
            shHeld_ = drivenIn;
        }
        rawOut = shHeld_;
        break;
    }
    case FilterType::SampHoldMinus:
    {
        shPhase_ += cutoff / srFloat;
        if (shPhase_ >= 1.0f)
        {
            shPhase_ -= std::floor (shPhase_);
            shHeld_ = drivenIn;
        }
        rawOut = drivenIn - shHeld_;
        break;
    }
    case FilterType::Combs:
    {
        // 4 parallel combs with prime ratios
        static constexpr float kCombRatios[4] = { 1.0f, 1.33f, 1.67f, 2.11f };
        const float baseDelay = std::max (2.0f, srFloat / std::max (20.0f, cutoff));
        float sum = 0.0f;
        const float dampG = tanM (kPi * std::max (100.0f, srFloat * (0.45f - 0.4f * var)) / srFloat);
        for (int i = 0; i < 4; ++i)
        {
            const float d = std::max (2.0f, std::min (baseDelay * kCombRatios[i], static_cast<float> (sampleRate_ * 0.05)));
            const float delayed = reverbCombs_[i].read (d);
            const float damped = combFeedbackLP_.processLPOnly (delayed, dampG);
            reverbCombs_[i].write (drivenIn + softClip (0.85f * res * damped));
            sum += delayed;
        }
        rawOut = 0.25f * sum;
        break;
    }
    case FilterType::Allpasses:
    {
        float ap = drivenIn;
        const float gAp = 0.6f * res;
        for (int i = 0; i < 6; ++i)
        {
            const float d = std::max (2.0f, (srFloat / std::max (20.0f, cutoff)) * (0.3f + 0.2f * static_cast<float> (i)));
            const float delayed = diffuserDelays_[i].read (d);
            const float x = ap - gAp * delayed;
            ap = delayed + gAp * x;
            diffuserDelays_[i].write (x);
        }
        rawOut = ap;
        break;
    }
    case FilterType::Reverb:
    {
        // 4 combs into 2 allpasses
        float sum = 0.0f;
        const float dampG = tanM (kPi * std::max (200.0f, srFloat * (0.4f - 0.35f * var)) / srFloat);
        static constexpr float kRevDelayMs[4] = { 25.0f, 32.0f, 37.0f, 41.0f };
        for (int i = 0; i < 4; ++i)
        {
            const float d = kRevDelayMs[i] * 0.001f * srFloat;
            const float delayed = reverbCombs_[i].read (d);
            const float damped = combFeedbackLP_.processLPOnly (delayed, dampG);
            reverbCombs_[i].write (drivenIn + softClip (0.8f * res * damped));
            sum += delayed;
        }
        float rev = reverbAP_[0].process (sum * 0.25f);
        rev = reverbAP_[1].process (rev);
        rawOut = 0.5f * drivenIn + 0.5f * rev;
        break;
    }
    case FilterType::FrenchLP:
    {
        // Steiner-Parker diode LP with secondary BOEUF resonance
        const float boeufFreq = std::min (cutoff * (1.0f + 0.8f * var), srFloat * 0.49f);
        const float g2 = tanM (kPi * boeufFreq / srFloat);

        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
        svf1_.process (drivenIn, g, R, 0.4f, lp1, bp1, hp1, notch1, peak1);
        svf2_.process (lp1, g2, 1.0f - 0.95f * var, 0.2f, lp2, bp2, hp2, notch2, peak2);
        rawOut = softClip (lp1 + 0.5f * lp2);
        break;
    }
    case FilterType::GermanLP:
    {
        // Clean ZDF 4-pole LP: the resonance lives in the first 12 dB stage, the second one is critically damped (R = 1), so the
        // type keeps a smooth 24 dB roll-off with one resonant peak, unlike the state variable Low 24 whose two stages share the resonance
        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
        svf1_.process (drivenIn, g, R, 0.0f, lp1, bp1, hp1, notch1, peak1);
        svf2_.process (lp1, g, 1.0f, 0.0f, lp2, bp2, hp2, notch2, peak2);
        rawOut = lp2;
        break;
    }
    case FilterType::AddBass:
    {
        // Lowpass + 90 deg phase rotated dry signal (via 1st order allpass)
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, 0.0f, lp, bp, hp, notch, peak);

        // 90 deg allpass rotation around 150 Hz
        const float gAp = tanM (kPi * 150.0f / srFloat);
        float apLp = 0.0f, apHp = 0.0f;
        onePole1_.processLP (drivenIn, gAp, apLp, apHp);
        const float rotatedDry = apLp - apHp;

        rawOut = lp * (1.0f + 2.0f * drive) + var * rotatedDry;
        break;
    }
    case FilterType::FormantI:
    case FilterType::FormantII:
    case FilterType::FormantIII:
    {
        // Vowel formants
        // Normalized vowel position from cutoff: 20..20000 -> 0..4
        const float normVowel = std::max (0.0f, std::min (std::log10 (cutoff / 20.0f) / 3.0f, 1.0f)) * 4.0f;
        const auto vIdx = static_cast<size_t> (normVowel);
        const float vFrac = normVowel - static_cast<float> (vIdx);

        // Formant frequencies for Set 1 (A, E, I, O, U)
        static constexpr float kF1_1[5] = { 800.0f, 400.0f, 300.0f, 500.0f, 350.0f };
        static constexpr float kF2_1[5] = { 1200.0f, 2000.0f, 2500.0f, 900.0f, 700.0f };
        static constexpr float kF3_1[5] = { 2500.0f, 2800.0f, 3200.0f, 2500.0f, 2400.0f };

        // Set 2 (A, I, U, O)
        static constexpr float kF1_2[5] = { 800.0f, 300.0f, 350.0f, 500.0f, 800.0f };
        static constexpr float kF2_2[5] = { 1200.0f, 2500.0f, 700.0f, 900.0f, 1200.0f };
        static constexpr float kF3_2[5] = { 2500.0f, 3200.0f, 2400.0f, 2500.0f, 2500.0f };

        // Set 3 (Vocal tract / nasal)
        static constexpr float kF1_3[5] = { 300.0f, 550.0f, 750.0f, 450.0f, 250.0f };
        static constexpr float kF2_3[5] = { 900.0f, 1400.0f, 1900.0f, 1100.0f, 850.0f };
        static constexpr float kF3_3[5] = { 2200.0f, 2500.0f, 2900.0f, 2400.0f, 2100.0f };

        const float* f1Table = kF1_1;
        const float* f2Table = kF2_1;
        const float* f3Table = kF3_1;
        if (type_ == FilterType::FormantII)
        {
            f1Table = kF1_2;
            f2Table = kF2_2;
            f3Table = kF3_2;
        }
        else if (type_ == FilterType::FormantIII)
        {
            f1Table = kF1_3;
            f2Table = kF2_3;
            f3Table = kF3_3;
        }

        const size_t i0 = std::min (vIdx, size_t (3));
        const size_t i1 = i0 + 1;

        // Formant shift from Var (FORMNT: -1 to +1 octave)
        const float shift = pow2M ((var - 0.5f) * 2.0f);
        const float f1 = (f1Table[i0] * (1.0f - vFrac) + f1Table[i1] * vFrac) * shift;
        const float f2 = (f2Table[i0] * (1.0f - vFrac) + f2Table[i1] * vFrac) * shift;
        const float f3 = (f3Table[i0] * (1.0f - vFrac) + f3Table[i1] * vFrac) * shift;

        const float g1 = tanM (kPi * std::min (f1, srFloat * 0.49f) / srFloat);
        const float g2_f = tanM (kPi * std::min (f2, srFloat * 0.49f) / srFloat);
        const float g3 = tanM (kPi * std::min (f3, srFloat * 0.49f) / srFloat);

        const float formR = 1.0f - 0.98f * (0.3f + 0.7f * res);
        float lpA = 0.0f, bpA = 0.0f, hpA = 0.0f, nA = 0.0f, pA = 0.0f;
        float lpB = 0.0f, bpB = 0.0f, hpB = 0.0f, nB = 0.0f, pB = 0.0f;
        float lpC = 0.0f, bpC = 0.0f, hpC = 0.0f, nC = 0.0f, pC = 0.0f;

        formantSVF_[0].process (drivenIn, g1, formR, 0.0f, lpA, bpA, hpA, nA, pA);
        formantSVF_[1].process (drivenIn, g2_f, formR, 0.0f, lpB, bpB, hpB, nB, pB);
        formantSVF_[2].process (drivenIn, g3, formR, 0.0f, lpC, bpC, hpC, nC, pC);

        rawOut = bpA + 0.8f * bpB + 0.5f * bpC;
        break;
    }
    case FilterType::Bandreject:
    {
        // Width controlled by Var (WIDTH: Q from 0.2 to 15.0)
        const float qVal = 0.2f * std::pow (75.0f, var);
        const float customR = 1.0f / (2.0f * qVal);
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, customR, 0.0f, lp, bp, hp, notch, peak);
        rawOut = notch;
        break;
    }
    case FilterType::DistComb1LP:
    case FilterType::DistComb1BP:
    case FilterType::DistComb2LP:
    case FilterType::DistComb2BP:
    {
        // Positive vs negative comb feedback
        const float sign = (type_ == FilterType::DistComb1LP || type_ == FilterType::DistComb1BP) ? 1.0f : -1.0f;
        const bool isBP = (type_ == FilterType::DistComb1BP || type_ == FilterType::DistComb2BP);

        // Comb frequency from Var (COMBFRQ: 20 Hz to 10 kHz)
        const float fComb = std::max (20.0f, std::min (20.0f * std::pow (500.0f, var), srFloat * 0.49f));
        const float delaySamples = srFloat / fComb;
        const float delayed = combDelay1_.read (delaySamples);

        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (delayed, g, R, 0.0f, lp, bp, hp, notch, peak);
        const float filteredFb = isBP ? (2.0f * R * bp) : (2.0f * R * lp);

        const float toWrite = drivenIn + softClip (sign * 0.85f * res * filteredFb);
        combDelay1_.write (toWrite);

        rawOut = 0.5f * (drivenIn + delayed);
        break;
    }
    case FilterType::ScreamLP:
    case FilterType::ScreamBP:
    {
        // High feedback filter effect (screams when drive > 0.5)
        const float screamFreq = std::max (20.0f, std::min (cutoff * pow2M ((var - 0.5f) * 4.0f), srFloat * 0.49f));
        const float screamG = tanM (kPi * screamFreq / srFloat);

        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, 0.0f, lp, bp, hp, notch, peak);
        const float primary = (type_ == FilterType::ScreamBP) ? bp : lp;

        // Feedback screaming loop
        float screamLP = 0.0f, screamHP = 0.0f;
        onePole1_.processLP (primary, screamG, screamLP, screamHP);

        float screamGain = 1.0f;
        if (drive > 0.5f)
            screamGain += 12.0f * (drive - 0.5f) * (drive - 0.5f);

        rawOut = softClip (primary + softClip (screamGain * res * screamLP));
        break;
    }

    // ==========================================
    // 5. NEW FAMILY (11 Types)
    // ==========================================
    case FilterType::NewWsp:
    {
        // EDP Wasp OTA model: blend LP -> Notch -> HP
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        // Non-linear distortion inside Wasp loop
        // the Wasp overdrives its OTA input stage: a hard saturation in front of the loop (the buzz), unity gain for small signals
        waspSVF_.process (softClip (3.0f * drivenIn) * (1.0f / 3.0f), g, R, 0.5f, lp, bp, hp, notch, peak);

        if (var < 0.5f)
        {
            const float t = var * 2.0f;
            rawOut = (1.0f - t) * lp + t * notch;
        }
        else
        {
            const float t = (var - 0.5f) * 2.0f;
            rawOut = (1.0f - t) * notch + t * hp;
        }
        break;
    }
    case FilterType::DJMixerXfer:
    {
        // Center (cutoff ~ 1000 Hz) is flat bypass. Left = LP, Right = HP.
        const float normC = std::log10 (std::max (20.0f, cutoff) / 20.0f) / 3.0f; // 0..1
        if (normC < 0.48f)
        {
            // Low-pass side
            const float lpCutoff = 20.0f * std::pow (1000.0f / 20.0f, normC / 0.48f);
            const float lpG = tanM (kPi * lpCutoff / srFloat);
            float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
            float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
            svf1_.process (drivenIn, lpG, R, 0.0f, lp1, bp1, hp1, notch1, peak1);
            svf2_.process (lp1, lpG, R, 0.0f, lp2, bp2, hp2, notch2, peak2);
            rawOut = lp2;
        }
        else if (normC > 0.52f)
        {
            // High-pass side
            const float hpCutoff = 1000.0f * std::pow (20000.0f / 1000.0f, (normC - 0.52f) / 0.48f);
            const float hpG = tanM (kPi * hpCutoff / srFloat);
            float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f, notch1 = 0.0f, peak1 = 0.0f;
            float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f, notch2 = 0.0f, peak2 = 0.0f;
            svf1_.process (drivenIn, hpG, R, 0.0f, lp1, bp1, hp1, notch1, peak1);
            svf2_.process (hp1, hpG, R, 0.0f, lp2, bp2, hp2, notch2, peak2);
            rawOut = hp2;
        }
        else
        {
            // Flat pass-through near 1 kHz center
            rawOut = drivenIn;
        }
        break;
    }
    case FilterType::Diffusor:
    {
        // Var controls active stages (1 to 8)
        const int activeStages = std::max (1, std::min (static_cast<int> (1.0f + 7.0f * var), 8));
        float ap = drivenIn;
        const float gDiff = 0.5f + 0.45f * res;
        for (int i = 0; i < activeStages; ++i)
        {
            const float d = std::max (2.0f, (srFloat / std::max (20.0f, cutoff)) * (0.2f + 0.15f * static_cast<float> (i)));
            const float delayed = diffuserDelays_[i].read (d);
            const float x = ap - gDiff * delayed;
            ap = delayed + gDiff * x;
            diffuserDelays_[i].write (x);
        }
        rawOut = ap;
        break;
    }
    case FilterType::MGLadder:
    {
        // Moog transistor ladder with clean ZDF modeling
        float y1 = 0.0f, y2 = 0.0f, y3 = 0.0f, y4 = 0.0f;
        ladder_.process (drivenIn, g, res, 0.0f, drive, y1, y2, y3, y4);
        rawOut = y4;
        break;
    }
    case FilterType::AcidLadder:
    {
        // TB-303 diode ladder
        rawOut = diodeLadder_.process (drivenIn, g, res, drive, srFloat);
        break;
    }
    case FilterType::EMSLadder:
    {
        // EMS VCS3 diode matrix ladder
        float y1 = 0.0f, y2 = 0.0f, y3 = 0.0f, y4 = 0.0f;
        ladder_.process (drivenIn, g, res, 0.3f, drive + 0.2f, y1, y2, y3, y4);
        rawOut = y4;
        break;
    }
    case FilterType::MGDirty:
    {
        // Overdriven Moog ladder with PAIN parameter
        const float painGain = 1.0f + 10.0f * var;
        const float overdriven = softClip (painGain * drivenIn + 0.15f * var);
        float y1 = 0.0f, y2 = 0.0f, y3 = 0.0f, y4 = 0.0f;
        ladder_.process (overdriven, g, res, var, drive + 0.5f * var, y1, y2, y3, y4);
        rawOut = softClip (y4 * (1.0f + var));
        break;
    }
    case FilterType::PZSVF:
    {
        // Pole-zero SVF with zero placement controlled by blend
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (drivenIn, g, R, 0.0f, lp, bp, hp, notch, peak);
        // Zero placement blends between pole output and notch
        rawOut = (1.0f - blend) * lp + blend * notch;
        break;
    }
    case FilterType::Comb2:
    {
        // Dual-tap resonant comb filter
        const float delay1 = srFloat / std::max (20.0f, cutoff);
        const float fc2 = std::max (20.0f, std::min (cutoff * pow2M ((var - 0.5f) * 4.0f), srFloat * 0.49f));
        const float delay2 = srFloat / fc2;

        const float tap1 = combDelay1_.read (delay1);
        const float tap2 = combDelay2_.read (delay2);

        const float fbGain = 0.95f * res;
        combDelay1_.write (drivenIn + softClip (fbGain * tap1));
        combDelay2_.write (drivenIn + softClip (fbGain * tap2));

        rawOut = 0.5f * (tap1 + tap2);
        break;
    }
    case FilterType::ExpMM:
    {
        // Oberheim SEM multimode: blend LP -> Notch -> HP via Var (MIX). The SEM state variable core has OTA input saturation, a gentler
        // resonance range than the plain SVF (R from 1 down to 0.3) and some feedback saturation
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (softClip (1.8f * drivenIn) * (1.0f / 1.8f), g, 1.0f - 0.7f * res, 0.3f, lp, bp, hp, notch, peak);

        if (var < 0.5f)
        {
            const float t = var * 2.0f;
            rawOut = (1.0f - t) * lp + t * notch;
        }
        else
        {
            const float t = (var - 0.5f) * 2.0f;
            rawOut = (1.0f - t) * notch + t * hp;
        }
        break;
    }
    case FilterType::ExpBPF:
    {
        // Oberheim SEM bandpass: the same core as Exp MM (OTA input saturation, gentler resonance), band output
        float lp = 0.0f, bp = 0.0f, hp = 0.0f, notch = 0.0f, peak = 0.0f;
        svf1_.process (softClip (1.8f * drivenIn) * (1.0f / 1.8f), g, 1.0f - 0.7f * res, 0.3f, lp, bp, hp, notch, peak);
        rawOut = bp;
        break;
    }
    default:
        rawOut = drivenIn;
        break;
    }

    // Dry / Wet Mix
    return (1.0f - mix) * in + mix * rawOut;
}

// ==========================================
// Filter Public API
// ==========================================

Filter::Filter()
    : impl_ (std::make_unique<FilterImpl>())
{
}

Filter::~Filter() = default;

Filter::Filter (Filter&&) noexcept = default;
Filter& Filter::operator= (Filter&&) noexcept = default;

void Filter::prepare (double sampleRate)
{
    impl_->prepare (sampleRate);
}

void Filter::reset() noexcept
{
    impl_->reset();
}

void Filter::setType (int type) noexcept
{
    impl_->setType (type);
}

int Filter::getType() const noexcept
{
    return impl_->getType();
}

void Filter::setParams (const FilterParams& params) noexcept
{
    impl_->setParams (params);
}

const FilterParams& Filter::getParams() const noexcept
{
    return impl_->getParams();
}

float Filter::process (float in) noexcept
{
    return impl_->processSample (in);
}

void Filter::process (const float* in, float* out, int numSamples,
                      const float* cutoffMod, const float* resMod) noexcept
{
    impl_->processBlock (in, out, numSamples, cutoffMod, resMod);
}

} // namespace synthyboy::filters
