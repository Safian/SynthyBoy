// SynthyBoy - core tests: the FROZEN REFERENCE of the delay effect before the CPU review item 3 (2026-11-26): Delay::process as it was at 391febd (the glide, the read position and the
// 8 point Lagrange weights computed every sample, two LoopBand::set per call). Test only; never edit it (test_fxopt_cpu_delay.cpp compares the Delay with it). Namespace synthyboy::own::cpuref.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "../../engine-own/fx/fx_effects.h"

namespace synthyboy
{
namespace own
{
namespace cpuref
{
class DelayBefore
{
public:
    struct Params
    {
        float mix = 0.3f, feedback = 0.4f, timeL = 0.3f, timeR = 0.3f, cutoff = 260.0f, spread = 1.0f, offsetL = 1.0f, offsetR = 1.0f;
        int style = 1;
        bool hq = false;
    };
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& d : line)
            d.prepare (static_cast<int> (sr * 4.2));
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : line) d.reset();
        for (auto& b : band) b.reset();
        fbRead[0] = fbRead[1] = 0.0f;
        primed = false;
    }
    void set (const Params& np) noexcept { p = np; }
    void process (float* l, float* r, int n) noexcept
    {
        const MixGains mg (p.mix);
        const float sr = static_cast<float> (sampleRate);
        // the target periods (seconds) of the two sides; Mid ping pong adds the right time to the left side
        float tL = p.timeL * p.offsetL, tR = (p.style == 0 ? p.timeL : p.timeR) * p.offsetR;
        if (p.style == 0)
            tR = p.timeL * p.offsetR;
        if (p.style == 3)
            tL = tL + tR;
        const float fTarget[2] = { 1.0f / std::clamp (tL, 3.0f / sr, 4.0f), 1.0f / std::clamp (tR, 3.0f / sr, 4.0f) };
        if (!primed)
        {
            fLast[0] = fTarget[0];
            fLast[1] = fTarget[1];
            primed = true;
        }
        const float glide = 1.0f - std::pow (0.5f, 1.0f / (0.02f * sr)); // half life of 20 ms: a change of the time bends the echoes like a tape
        for (auto& b : band)
            b.set (p.cutoff, p.spread, sampleRate);
        const float fb = std::clamp (p.feedback, -1.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
                fLast[c] += (fTarget[c] - fLast[c]) * glide;
            const float period[2] = { std::clamp (sr / fLast[0], 3.0f, 4.0f * sr), std::clamp (sr / fLast[1], 3.0f, 4.0f * sr) };
            float rd[2];
            for (int c = 0; c < 2; ++c)
                rd[c] = p.hq ? line[c].readLagrange8 (period[c]) : line[c].readCubic (period[c]);
            const float xl = l[i], xr = r[i];
            float wl = 0.0f, wr = 0.0f, outL = 0.0f, outR = 0.0f;
            switch (p.style)
            {
                case 4: // tap -> delay: the left side is a tap of the mono input, the right side is a delay fed by the tap and by itself
                {
                    const float mono = 0.70710678f * (xl + xr);
                    wl = mono;
                    wr = loopLimit (rd[0] + fb * rd[1]);
                    outL = outR = rd[0] + rd[1];
                    line[0].push (band[0].process (wl));
                    line[1].push (band[1].process (wr));
                    l[i] = mg.apply (xl, outL);
                    r[i] = mg.apply (xr, outR);
                    continue;
                }
                case 2: // ping pong: the written values change the channel (the right lane feedback is 1, the left one the knob)
                    wl = loopLimit (xl + fb * rd[0]);
                    wr = loopLimit (xr + rd[1]);
                    line[0].push (band[0].process (wr));
                    line[1].push (band[1].process (wl));
                    break;
                case 3: // mid ping pong: the mono sum on the left
                    wl = loopLimit (0.70710678f * (xl + xr) + fb * rd[0]);
                    wr = loopLimit (rd[1]);
                    line[0].push (band[0].process (wr));
                    line[1].push (band[1].process (wl));
                    break;
                default: // mono, stereo
                    wl = loopLimit (xl + fb * rd[0]);
                    wr = loopLimit (xr + fb * rd[1]);
                    line[0].push (band[0].process (wl));
                    line[1].push (band[1].process (wr));
                    break;
            }
            l[i] = mg.apply (xl, rd[0]);
            r[i] = mg.apply (xr, rd[1]);
        }
    }

private:
    Params p;
    double sampleRate = 48000.0;
    std::array<DelayLine, 2> line;
    LoopBand band[2];
    float fLast[2] = {}, fbRead[2] = {};
    bool primed = false;
};

} // namespace cpuref
} // namespace own
} // namespace synthyboy
