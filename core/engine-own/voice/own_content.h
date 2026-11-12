// SynthyBoy - own engine: the `content` of the state that the voice reads (custom LFO shapes with point modulation, per slot modulation curves) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "synthyboy/content_state.h"
#include "synthyboy/modulation.h"
#include "synthyboy/shapes/shape.h"

namespace synthyboy
{
namespace own
{
struct PointMod
{
    int source = 0;      // choice value of the modulation source (0 = none)
    float amount = 0.0f; // -1 .. 1
};

struct CompiledPoint
{
    shapes::Point point;
    PointMod x, y, curve;
};

struct CompiledShape
{
    bool present = false, anyMod = false;
    int num = 0;
    std::array<CompiledPoint, shapes::ShapeData::kMaxPoints> points {};
    shapes::ShapeData fixed; // the shape without the modulation
};

struct CompiledModCurve
{
    bool present = false;
    int num = 0;
    std::array<float, 2048> table {};
};

/** What the audio thread reads: built on the message thread, never changed after it was published. */
struct OwnContent
{
    std::array<CompiledShape, 10> lfo {};
    std::array<CompiledModCurve, 64> modCurve {};
};

/** The hand-over of the content: the message thread publishes, the audio thread reads the latest one with one atomic load. A published object
    stays alive until `kRing` newer ones were published (a block never lasts that long). */
class ContentHub
{
public:
    void publish (const StateContent& c)
    {
        auto o = std::make_unique<OwnContent>();
        for (const auto& s : c.lfoShapes)
        {
            if (s.lfo < 1 || s.lfo > 10 || s.points.size() < 2)
                continue;
            CompiledShape& d = o->lfo[static_cast<size_t> (s.lfo - 1)];
            d.present = true;
            d.num = static_cast<int> (std::min<size_t> (s.points.size(), shapes::ShapeData::kMaxPoints));
            for (int i = 0; i < d.num; ++i)
            {
                const ContentPoint& p = s.points[static_cast<size_t> (i)];
                CompiledPoint& q = d.points[static_cast<size_t> (i)];
                q.point.x = p.x;
                q.point.y = p.y;
                q.point.curve = p.power;
                q.point.smooth = s.smooth;
                q.x = resolve (p.modX);
                q.y = resolve (p.modY);
                q.curve = resolve (p.modCurve);
                d.anyMod = d.anyMod || q.x.source != 0 || q.y.source != 0 || q.curve.source != 0;
            }
            d.fixed.numPoints = static_cast<size_t> (d.num);
            for (int i = 0; i < d.num; ++i)
                d.fixed.points[static_cast<size_t> (i)] = d.points[static_cast<size_t> (i)].point;
            d.fixed.sanitize();
        }
        for (const auto& m : c.modCurves)
        {
            if (m.slot < 1 || m.slot > 64 || m.points.size() < 2)
                continue;
            CompiledModCurve& d = o->modCurve[static_cast<size_t> (m.slot - 1)];
            d.present = true;
            shapes::ShapeData sd;
            sd.numPoints = std::min<size_t> (m.points.size(), shapes::ShapeData::kMaxPoints);
            for (size_t i = 0; i < sd.numPoints; ++i)
            {
                sd.points[i].x = m.points[i].x;
                sd.points[i].y = m.points[i].y;
                sd.points[i].curve = m.points[i].power;
            }
            sd.sanitize();
            sd.bake (d.table.data(), d.table.size());
        }
        const size_t slot = next++ % kRing;
        ring[slot] = std::move (o);
        current.store (ring[slot].get(), std::memory_order_release);
    }

    const OwnContent* get() const noexcept { return current.load (std::memory_order_acquire); }

private:
    static PointMod resolve (const ContentModRef& r)
    {
        PointMod m;
        if (!r.present)
            return m;
        for (size_t i = 0; i < modSourceCount(); ++i)
            if (r.source == modSourceTable()[i].key)
            {
                m.source = static_cast<int> (i + 1);
                m.amount = std::clamp (r.amount, -1.0f, 1.0f);
                break;
            }
        return m;
    }

    static constexpr size_t kRing = 8;
    std::array<std::unique_ptr<OwnContent>, kRing> ring;
    size_t next = 0;
    std::atomic<const OwnContent*> current { nullptr };
};
} // namespace own
} // namespace synthyboy
