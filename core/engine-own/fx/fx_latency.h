// SynthyBoy - own engine: the latency compensation delays of the buses (document 06 section 11).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "../dsp/util.h"

namespace synthyboy
{
namespace own
{
/** An integer sample delay of a stereo pair, at most 255 samples (a longer one is clamped). A change of the delay clears its history. */
class IntDelay
{
public:
    static constexpr int kMax = 255;
    void set (int samples) noexcept
    {
        const int d = std::clamp (samples, 0, kMax);
        if (d != delay)
        {
            delay = d;
            clear();
        }
    }
    void clear() noexcept
    {
        bufL.fill (0.0f);
        bufR.fill (0.0f);
    }
    int get() const noexcept { return delay; }
    void process (float* l, float* r, int n) noexcept
    {
        if (delay == 0)
            return;
        for (int i = 0; i < n; ++i)
        {
            bufL[static_cast<size_t> (pos & kMask)] = l[i];
            bufR[static_cast<size_t> (pos & kMask)] = r[i];
            l[i] = bufL[static_cast<size_t> ((pos - delay) & kMask)];
            r[i] = bufR[static_cast<size_t> ((pos - delay) & kMask)];
            ++pos;
        }
    }

private:
    static constexpr int kMask = 511;
    std::array<float, 512> bufL {}, bufR {};
    int delay = 0, pos = 0;
};

/** The latencies of the paths of the mixer and the delays that line them up (document 06, 11). All values are integer samples of the chain rate. */
struct LatencyPlan
{
    int voicesToMain = 0, busOut[2] = {}, busIn[2] = {}, mainOut = 0, direct = 0, total = 0;

    /** lat0 = latency of the Main rack, lat[0..1] = of Bus 1 / Bus 2 (already 0 for a bypassed rack); dest[0..1]: 0 Main, 1 Direct, 2 Other bus (bus 2 acts as Main when both say Other). */
    static LatencyPlan compute (int lat0, const int lat[2], const int dest[2]) noexcept
    {
        LatencyPlan p;
        int LB[2] = { lat[0], lat[1] };
        bool feeds[2] = { dest[0] == 2, dest[1] == 2 };
        if (feeds[0])
        {
            p.busIn[1] = lat[0]; // the second bus waits for what the first one sends into it
            LB[1] = lat[0] + lat[1];
            feeds[1] = false;
        }
        else if (feeds[1])
        {
            p.busIn[0] = lat[1];
            LB[0] = lat[1] + lat[0];
        }
        int lmIn = 0;
        for (int b = 0; b < 2; ++b)
            if (!feeds[b] && dest[b] == 0)
                lmIn = std::max (lmIn, LB[b]);
        const int lm = lmIn + lat0;
        int total = lm;
        for (int b = 0; b < 2; ++b)
            if (!feeds[b] && dest[b] == 1)
                total = std::max (total, LB[b]);
        p.voicesToMain = lmIn;
        for (int b = 0; b < 2; ++b)
        {
            if (feeds[b])
                p.busOut[b] = 0;
            else if (dest[b] == 1)
                p.busOut[b] = total - LB[b];
            else
                p.busOut[b] = lmIn - LB[b];
        }
        p.mainOut = total - lm;
        p.direct = total;
        p.total = total;
        return p;
    }
};
} // namespace own
} // namespace synthyboy
