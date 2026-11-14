// SynthyBoy - core: the shape of an LFO of the own engine as the voice plays it (see lfo_display.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/lfo_display.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/params.h"
#include "env_lfo/lfo_eval.h"
#include "voice/param_index.h"

namespace synthyboy
{
namespace
{
constexpr int kSubSteps = 4;       // smoothing: steps per output point
constexpr int kCycles = 3;         // smoothing: cycles run until the filter has settled
}

float LfoDisplay::cursorX (float readout) noexcept
{
    if (!(readout >= 0.0f))
        return -1.0f;
    return std::min (readout, 1.0f);
}

bool LfoDisplay::compute (const ParamValues& values, const StateContent* content, const Request& r, float* out, int n) noexcept
{
    if (out == nullptr || n < 2)
        return false;
    std::fill (out, out + n, 0.0f);
    if (r.lfo < 1 || r.lfo > 10)
        return false;
    const int l = r.lfo - 1;
    const own::LfoIdx& li = own::paramIndex().lfo[l];
    if (li.shape < 0 || li.sync < 0 || li.syncType < 0 || li.phase < 0)
        return false;
    const ParamInfo* table = paramTable();
    auto P = [&] (int i, float fallback = 0.0f)
    {
        if (i < 0)
            return fallback;
        const float x = values.plain[i];
        return std::isfinite (x) ? std::clamp (x, table[i].min, table[i].max) : table[i].def;
    };
    const int shape = static_cast<int> (P (li.shape) + 0.5f);
    const int sync = static_cast<int> (P (li.sync) + 0.5f);
    const int syncType = std::clamp (static_cast<int> (P (li.syncType) + 0.5f), 0, 5);
    const float knob = P (li.phase) * (1.0f / 360.0f);
    const float stereo = P (li.stereo) * (1.0f / 720.0f);
    const float swing = li.swing >= 0 ? own::clamp01 (P (li.swing) * 0.01f) : 0.0f;
    const int dir = static_cast<int> (P (li.direction) + 0.5f);
    const bool smooth = P (li.smooth) >= 0.5f;
    const float half = 0.2f * P (li.smoothTime) * 0.001f;
    const float rate = own::lfoRateHz (sync, P (li.rate), static_cast<int> (P (li.division) + 0.5f), r.bpm, 440.0f, 60.0f, P (li.keyTranspose), P (li.keyTune));

    own::CompiledShape custom;
    bool haveCustom = false;
    if (shape == 12 && content != nullptr)
        for (const ContentLfoShape& s : content->lfoShapes)
            if (s.lfo == r.lfo && s.points.size() >= 2)
            {
                own::ContentHub::compileLfoShape (s, custom);
                haveCustom = true;
                break;
            }
    shapes::ShapeData scratch;
    static const float kNoSources[own::kNumSourceIds] = {};
    const float* src = r.sources != nullptr ? r.sources : kNoSources;
    const bool wrap = syncType <= 1;

    // the position the shape is read at for the time x (cycles since the start of the note), by the sync type (the engine's LfoCore::position)
    auto positionAt = [&] (float x)
    {
        switch (syncType)
        {
            case 2: return std::min (x + knob, 1.0f);
            case 3: case 4: case 5: return std::min (x, 1.0f);
            default: return x + knob;
        }
    };
    auto laneAt = [&] (float x) // -1 .. 1
    {
        const float u = positionAt (x) + stereo;
        return 2.0f * own::lfoShapeAt (shape, u, wrap, swing, dir, haveCustom ? &custom : nullptr, src, scratch, 0u) - 1.0f;
    };

    const bool filtered = smooth && half >= 0.0002f && rate > 0.0f;
    if (!filtered)
    {
        for (int i = 0; i < n; ++i)
            out[i] = laneAt (static_cast<float> (i) / static_cast<float> (n - 1));
        return true;
    }
    // the one pole smoothing in time: steps of dt = 1 / (rate * steps per cycle), cycles run one after the other (the free types), the last one is shown
    const int stepsPerCycle = kSubSteps * (n - 1);
    const float dt = 1.0f / (rate * static_cast<float>(stepsPerCycle));
    const float k = 1.0f - std::exp2 (-dt / half);
    const int cycles = wrap ? kCycles : 1;
    float state = 0.0f;
    float prevY = 0.0f;
    int outIdx = 0;
    for (int c = 0; c < cycles; ++c)
        for (int s = 0; s <= stepsPerCycle; ++s)
        {
            if (s == stepsPerCycle && c + 1 < cycles)
                break; // the first step of the next cycle is this position
            const float x = static_cast<float> (s) / static_cast<float> (stepsPerCycle);
            const float target = laneAt (wrap ? x : x);
            if (c == 0 && s == 0)
                state = state + (target - state) * k; // the voice's first period already moves towards the shape
            else
                state += (target - state) * k;
            prevY = state;
            if (c == cycles - 1 && s % kSubSteps == 0 && outIdx < n)
                out[outIdx++] = prevY;
        }
    while (outIdx < n)
        out[outIdx++] = prevY;
    return true;
}
} // namespace synthyboy
