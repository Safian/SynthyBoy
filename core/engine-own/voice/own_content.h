// SynthyBoy - own engine: the `content` of the state that the voice reads (custom LFO shapes with point modulation, per slot modulation curves) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

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
    CompiledModCurve velocityCurve, noteCurve;   // content.curves.velocity / .note (the curves the sources `velocity` and `note` read through); absent = identity
};

/** A serialized publisher and one audio consumer exchange ownership of three slots.
    The audio consumer keeps its slot until it acquires a newer snapshot. */
class ContentHub
{
public:
    struct Snapshot
    {
        const OwnContent* content = nullptr;
        uint64_t generation = 0;
    };

    void publish (const StateContent& c)
    {
        auto o = std::make_unique<OwnContent>();
        for (const auto& s : c.lfoShapes)
        {
            if (s.lfo < 1 || s.lfo > 10 || s.points.size() < 2)
                continue;
            compileLfoShape (s, o->lfo[static_cast<size_t> (s.lfo - 1)]);
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
        auto compileCurve = [] (const std::vector<ContentPoint>& pts, CompiledModCurve& d)
        {
            if (pts.size() < 2)
                return;
            d.present = true;
            shapes::ShapeData sd;
            sd.numPoints = std::min<size_t> (pts.size(), shapes::ShapeData::kMaxPoints);
            for (size_t i = 0; i < sd.numPoints; ++i)
            {
                sd.points[i].x = pts[i].x;
                sd.points[i].y = pts[i].y;
                sd.points[i].curve = pts[i].power;
            }
            sd.sanitize();
            sd.bake (d.table.data(), d.table.size());
        };
        compileCurve (c.velocityCurve, o->velocityCurve);
        compileCurve (c.noteCurve, o->noteCurve);

        std::lock_guard<std::mutex> lock (publishMutex);
        slots[writerSlot] = { std::move (o), ++publishedGeneration };
        writerSlot = exchangeSlot.exchange (writerSlot | kReady, std::memory_order_acq_rel) & kSlotMask;
        slots[writerSlot] = {}; // Reclaim the replaced snapshot only on the publisher.
    }

    /** One custom LFO shape of the content as the voice reads it (also used by the LFO display). */
    static void compileLfoShape (const ContentLfoShape& s, CompiledShape& d)
    {
        d = CompiledShape();
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

    /** Audio thread only. The relaxed load checks for work. The exchange acquires the
        published snapshot and releases the previously held slot to the publisher. */
    Snapshot acquire() noexcept
    {
        if ((exchangeSlot.load (std::memory_order_relaxed) & kReady) != 0)
            readerSlot = exchangeSlot.exchange (readerSlot, std::memory_order_acq_rel) & kSlotMask;
        const auto& slot = slots[readerSlot];
        return { slot.content.get(), slot.generation };
    }

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

    struct Slot
    {
        std::unique_ptr<const OwnContent> content;
        uint64_t generation = 0;
    };

    static constexpr unsigned kSlotMask = 3;
    static constexpr unsigned kReady = 4;
    static_assert (std::atomic<unsigned>::is_always_lock_free);

    std::array<Slot, 3> slots;
    std::atomic<unsigned> exchangeSlot { 1 };
    unsigned readerSlot = 0;
    std::mutex publishMutex;
    unsigned writerSlot = 2;
    uint64_t publishedGeneration = 0;
};
} // namespace own
} // namespace synthyboy
