// SynthyBoy - own engine: DAHDSR envelope with curved segments (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../dsp/util.h"

namespace synthyboy
{
namespace own
{
struct EnvSettings
{
    float delay = 0, attack = 0, hold = 0, decay = 0, sustain = 1, release = 0; // seconds / 0 .. 1
    float attackCurve = 0, decayCurve = 0, releaseCurve = 0;
};

/** Stages: delay (0), attack ramps up with attackCurve, hold at 1, decay to the sustain level, sustain, release to 0. The output is evaluated
    once per control period (advance) and interpolated by the caller. */
class Envelope
{
public:
    enum Stage : int { idle, delay, attack, hold, decay, sustain, release, done };

    void noteOn (float startValue = 0.0f) noexcept
    {
        stage = Stage::delay;
        pos = 0.0;
        value = startValue;
        startLevel = startValue;
        gate = true;
    }

    void noteOff() noexcept
    {
        if (!gate)
            return;
        gate = false;
        if (stage != Stage::done && stage != Stage::idle)
        {
            stage = Stage::release;
            pos = 0.0;
            releaseFrom = value;
        }
    }

    bool finished() const noexcept { return stage == Stage::done || stage == Stage::idle; }
    bool held() const noexcept { return gate; }
    float current() const noexcept { return value; }

    /** Advances `seconds` and returns the value at the end of the step. */
    float advance (const EnvSettings& s, double seconds) noexcept
    {
        double left = seconds;
        // a stage that is shorter than the step hands the remainder to the next one
        for (int guard = 0; guard < 8 && left >= 0.0; ++guard)
        {
            switch (stage)
            {
                case Stage::idle:
                case Stage::done:
                    value = 0.0f;
                    return value;
                case Stage::delay:
                    if (!(s.delay > 0.0f) || pos + left >= s.delay)
                    {
                        left -= std::max (0.0, s.delay - pos);
                        stage = Stage::attack;
                        pos = 0.0;
                        break;
                    }
                    pos += left;
                    value = startLevel;
                    return value;
                case Stage::attack:
                    if (!(s.attack > 0.0f) || pos + left >= s.attack)
                    {
                        left -= std::max (0.0, s.attack - pos);
                        stage = Stage::hold;
                        pos = 0.0;
                        value = 1.0f;
                        break;
                    }
                    pos += left;
                    value = lerp (startLevel, 1.0f, powerShape (static_cast<float> (pos / s.attack), s.attackCurve));
                    return value;
                case Stage::hold:
                    if (!(s.hold > 0.0f) || pos + left >= s.hold)
                    {
                        left -= std::max (0.0, s.hold - pos);
                        stage = Stage::decay;
                        pos = 0.0;
                        value = 1.0f;
                        break;
                    }
                    pos += left;
                    value = 1.0f;
                    return value;
                case Stage::decay:
                    if (!(s.decay > 0.0f) || pos + left >= s.decay)
                    {
                        left = 0.0;
                        stage = Stage::sustain;
                        pos = 0.0;
                        value = s.sustain;
                        return value;
                    }
                    pos += left;
                    value = lerp (1.0f, s.sustain, powerShape (static_cast<float> (pos / s.decay), s.decayCurve));
                    return value;
                case Stage::sustain:
                    value = s.sustain; // follows the (modulated) sustain level
                    return value;
                case Stage::release:
                    if (!(s.release > 0.0f) || pos + left >= s.release)
                    {
                        stage = Stage::done;
                        value = 0.0f;
                        return value;
                    }
                    pos += left;
                    value = releaseFrom * (1.0f - powerShape (static_cast<float> (pos / s.release), s.releaseCurve));
                    return value;
            }
        }
        return value;
    }

    void kill() noexcept
    {
        stage = Stage::done;
        value = 0.0f;
    }

private:
    Stage stage = Stage::idle;
    bool gate = false;
    double pos = 0.0;
    float value = 0.0f, startLevel = 0.0f, releaseFrom = 0.0f;
};
} // namespace own
} // namespace synthyboy
