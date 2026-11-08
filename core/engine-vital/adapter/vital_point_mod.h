// SynthyBoy - engine-vital adapter: a custom LFO shape with its point modulation (content.lfo_shapes), baked per voice.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Header only so that the tests can compare the incremental bake with a full bake. Includes the Vital-derived LFO header.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "synth_lfo.h"
#include "synthyboy/modulation.h"
#include "synthyboy/shapes/shapes.h"

namespace synthyboy
{
struct PointMod
{
    int point = 0;
    bool isX = false;
    int source = -1; // index into modSourceTable () (the key of the content), -1: unknown key, the modulation is ignored
    float amount = 0.0f;
};

/** The sources whose value is the same for every voice: 1..8 macros, 9 mod wheel, 10 pitch wheel (-1..1), 11 channel pressure; 0 for a
    source that differs per voice (envelopes, LFOs, random, velocity, note, lift, slide, oct note, poly aftertouch). */
inline int globalKindOf (int source) noexcept
{
    if (source >= 18 && source <= 21) return source - 18 + 1; // macro1 .. macro4
    if (source >= 28 && source <= 31) return source - 28 + 5; // macro5 .. macro8
    if (source == 26) return 9;                                // mod_wheel
    if (source == 25) return 10;                               // pitch_wheel
    if (source == 24) return 11;                               // aftertouch
    return 0;
}

/** Index of a mod-source key in modSourceTable (), -1 if there is none. */
inline int sourceIndexOf (const std::string& key) noexcept
{
    const ModSourceInfo* table = modSourceTable();
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (key == table[i].key)
            return static_cast<int> (i);
    return -1;
}

constexpr int kPointSources = sbvital::LfoPointModulation::kMaxSources;
static_assert (kNumModSources <= kPointSources, "an LFO has an input for every modulation source");

/** A custom LFO shape with its point modulation: bakes the table for one set of source values. Immutable once built (shared by the voices'
    LFOs through LfoPointState, baked on the audio thread). The positions move in order and keep their neighbours' limits. */
class CustomShapeModulation final : public sbvital::LfoPointModulation
{
public:
    static constexpr int kHighResolution = 2048, kLowResolution = 512;

    CustomShapeModulation (const shapes::ShapeData& s, const std::vector<PointMod>& m)
        : shape (s), mods (m), baseHigh (static_cast<size_t> (kHighResolution)), baseLow (static_cast<size_t> (kLowResolution))
    {
        for (const PointMod& pm : mods)
            if (pm.source >= 0 && pm.source < kPointSources && pm.amount != 0.0f)
            {
                used |= 1ull << pm.source;
                if (globalKindOf (pm.source) == 0)
                    perVoice |= 1ull << pm.source;
                else
                    global |= 1ull << pm.source;
            }
        shape.bake (baseHigh.data(), baseHigh.size());
        shape.bake (baseLow.data(), baseLow.size());
    }

    uint64_t sources() const noexcept override { return used; }
    uint64_t voiceSources() const noexcept { return perVoice; }
    uint64_t globalSources() const noexcept { return global; }

    /** The point list with the modulation applied: values[s] is the value of source s. */
    void apply (const float* values, shapes::ShapeData& out) const noexcept
    {
        out = shape;
        for (const PointMod& m : mods)
        {
            if (m.source < 0 || m.source >= kPointSources)
                continue;
            const float v = values[m.source] * m.amount;
            shapes::Point& point = out.points[static_cast<size_t> (m.point)];
            if (m.isX)
            {
                const float low = out.points[static_cast<size_t> (m.point - 1)].x;
                const float high = shape.points[static_cast<size_t> (m.point + 1)].x;
                point.x = std::clamp (point.x + v, low, high);
            }
            else
                point.y = std::clamp (point.y + v, 0.0f, 1.0f);
        }
    }

    /** `resolution` values: the unmodulated table where no point has moved, the shape evaluated again only between the neighbours of every
        moved point (the same evaluate () as a full bake, so the result is identical to baking the modulated shape). */
    void bake (const float* values, int resolution, float* table) const noexcept override
    {
        shapes::ShapeData m;
        apply (values, m);
        const std::vector<float>* base = resolution == kHighResolution ? &baseHigh : (resolution == kLowResolution ? &baseLow : nullptr);
        if (base == nullptr)
        {
            m.bake (table, static_cast<size_t> (resolution));
            return;
        }
        std::memcpy (table, base->data(), static_cast<size_t> (resolution) * sizeof (float));

        const size_t n = shape.numPoints;
        const float step = 1.0f / static_cast<float> (resolution - 1);
        int done = 0; // first index not yet evaluated again (the intervals ascend)
        for (size_t p = 0; p < n; ++p)
        {
            if (m.points[p].x == shape.points[p].x && m.points[p].y == shape.points[p].y)
                continue;
            const float lo = p > 0 ? std::min (shape.points[p - 1].x, m.points[p - 1].x) : 0.0f;
            const float hi = p + 1 < n ? std::max (shape.points[p + 1].x, m.points[p + 1].x) : 1.0f;
            const int first = std::max (done, std::clamp (static_cast<int> (std::floor (lo / step)) - 1, 0, resolution - 1));
            const int last = std::clamp (static_cast<int> (std::ceil (hi / step)) + 1, 0, resolution - 1);
            for (int i = first; i <= last; ++i)
                table[i] = m.evaluate (static_cast<float> (i) * step);
            done = std::max (done, last + 1);
        }
    }

private:
    shapes::ShapeData shape;
    std::vector<PointMod> mods;
    std::vector<float> baseHigh, baseLow;
    uint64_t used = 0, perVoice = 0, global = 0;
};
} // namespace synthyboy
