// MpcKit Demo - the sound: a sine voice through a state variable filter with an attack / release envelope. Deliberately tiny: replace this file with your own engine; nothing else in the template
// depends on how the sound is made, only on the parameter values you read here.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "demo_params.h"

namespace demo {

/** The parameter values of one block, in the units the DSP wants. */
struct Snapshot
{
    float gain = 0.5f, gainL = 0.5f, gainR = 0.5f;
    float pitchRatio = 1.0f;
    float cutoffHz = 8000.0f, resonance = 0.1f;
    bool filterOn = true;
    int mode = 0;
    float attackMs = 10.0f, releaseMs = 300.0f;
};

/** Plain parameter values (ParamId order) -> Snapshot. */
inline Snapshot snapshotOf (const float* plain)
{
    Snapshot s;
    s.gain = std::pow (10.0f, plain[kLevel] / 20.0f);
    const float p = std::clamp (plain[kPan] / 100.0f, -1.0f, 1.0f);   // equal power pan
    s.gainL = s.gain * std::cos ((p + 1.0f) * 0.78539816f);
    s.gainR = s.gain * std::sin ((p + 1.0f) * 0.78539816f);
    s.pitchRatio = std::pow (2.0f, plain[kPitch] / 12.0f);
    s.cutoffHz = plain[kCutoff];
    s.resonance = plain[kResonance] / 100.0f;
    s.filterOn = plain[kFilterOn] > 0.5f;
    s.mode = std::clamp (static_cast<int> (std::lround (plain[kMode])), 0, 3);
    s.attackMs = plain[kAttack];
    s.releaseMs = plain[kRelease];
    return s;
}

class Voice
{
public:
    bool active() const noexcept { return stage_ != Stage::idle; }
    int note() const noexcept { return note_; }
    bool held() const noexcept { return stage_ == Stage::attack || stage_ == Stage::sustain; }

    void noteOn (int note, float velocity) noexcept
    {
        note_ = note;
        vel_ = velocity;
        freq_ = 440.0f * std::pow (2.0f, (static_cast<float> (note) - 69.0f) / 12.0f);
        stage_ = Stage::attack;
    }
    void noteOff() noexcept
    {
        if (stage_ != Stage::idle)
            stage_ = Stage::release;
    }

    /** Adds this voice to l / r (n samples). */
    void process (float* l, float* r, int n, double sampleRate, const Snapshot& s) noexcept
    {
        if (stage_ == Stage::idle)
            return;
        const float fs = static_cast<float> (sampleRate);
        const float inc = 6.28318531f * freq_ * s.pitchRatio / fs;
        const float attackStep = s.attackMs < 0.5f ? 1.0f : 1.0f / (s.attackMs * 0.001f * fs);
        const float releaseStep = s.releaseMs < 0.5f ? 1.0f : 1.0f / (s.releaseMs * 0.001f * fs);
        const float g = std::tan (3.14159265f * std::clamp (s.cutoffHz, 20.0f, 0.45f * fs) / fs);
        const float damping = 2.0f * (1.0f - 0.97f * std::clamp (s.resonance, 0.0f, 1.0f));
        const float h = 1.0f / (1.0f + damping * g + g * g);
        for (int i = 0; i < n; ++i)
        {
            if (stage_ == Stage::attack)
            {
                env_ += attackStep;
                if (env_ >= 1.0f)
                {
                    env_ = 1.0f;
                    stage_ = Stage::sustain;
                }
            }
            else if (stage_ == Stage::release)
            {
                env_ -= releaseStep;
                if (env_ <= 0.0f)
                {
                    env_ = 0.0f;
                    stage_ = Stage::idle;
                    s1_ = s2_ = 0.0f;
                    break;
                }
            }
            float x = std::sin (phase_);
            phase_ += inc;
            if (phase_ > 6.28318531f)
                phase_ -= 6.28318531f;
            if (s.filterOn)
            {
                const float hp = (x - (damping + g) * s1_ - s2_) * h;
                const float bp = g * hp + s1_;
                s1_ = g * hp + bp;
                const float lp = g * bp + s2_;
                s2_ = g * bp + lp;
                x = s.mode == 0 ? lp : (s.mode == 1 ? hp : (s.mode == 2 ? bp * damping : hp + lp));
            }
            const float y = x * env_ * vel_;
            l[i] += y * s.gainL;
            r[i] += y * s.gainR;
        }
    }

private:
    enum class Stage : uint8_t { idle, attack, sustain, release };
    Stage stage_ = Stage::idle;
    int note_ = -1;
    float vel_ = 0.0f, freq_ = 440.0f, phase_ = 0.0f, env_ = 0.0f, s1_ = 0.0f, s2_ = 0.0f;
};

/** Eight voices; the first idle one is taken, else voice 0 is stolen. Audio thread only. */
class Engine
{
public:
    static constexpr int kVoices = 8;
    void noteOn (int note, int velocity127) noexcept
    {
        for (Voice& v : voices_)
            if (! v.active())
            {
                v.noteOn (note, static_cast<float> (velocity127) / 127.0f);
                return;
            }
        voices_[0].noteOn (note, static_cast<float> (velocity127) / 127.0f);
    }
    void noteOff (int note) noexcept
    {
        for (Voice& v : voices_)
            if (v.held() && v.note() == note)
                v.noteOff();
    }
    void allNotesOff() noexcept
    {
        for (Voice& v : voices_)
            v.noteOff();
    }
    void process (float* l, float* r, int n, double sampleRate, const Snapshot& s) noexcept
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        for (Voice& v : voices_)
            v.process (l, r, n, sampleRate, s);
    }

private:
    Voice voices_[kVoices];
};

} // namespace demo
