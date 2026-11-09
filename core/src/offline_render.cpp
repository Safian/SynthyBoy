// SynthyBoy - core: the offline render driver (GROUPD WP-15, see offline_render.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synthyboy/offline_render.h"

#include <algorithm>
#include <cmath>

#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "synthyboy/state.h"

namespace synthyboy
{
namespace
{
constexpr int kBlock = 256;
constexpr float kSilent = 3.1623e-5f; // -90 dB
constexpr double kSilentSeconds = 0.25;

struct TimedEvent
{
    int64_t frame;
    Event event;
};

size_t framesOf (double seconds, double sampleRate) noexcept
{
    return seconds <= 0.0 ? 0u : static_cast<size_t> (std::llround (seconds * sampleRate));
}

void append (OfflineAudio& out, const float* l, const float* r, int n)
{
    for (int i = 0; i < n; ++i)
    {
        out.left.push_back (std::isfinite (l[i]) ? l[i] : 0.0f);
        out.right.push_back (std::isfinite (r[i]) ? r[i] : 0.0f);
    }
}
} // namespace

bool runOfflineRequest (IEngine& twin, const OfflineRequest& request, OfflineAudio& out, const std::function<void (IEngine&)>& afterPrepare)
{
    out = OfflineAudio {};
    if (! (request.sampleRate >= 8000.0 && request.sampleRate <= 384000.0) || request.steps.empty())
        return false;
    double total = std::max (0.0, request.tailSeconds);
    for (const auto& step : request.steps)
        total += std::max (0.0, step.seconds);
    if (! (total > 0.0) || total > kMaxOfflineSeconds)
        return false;

    Quality quality = qualityForTier (Tier::high);
    quality.realtime = false; // deterministic: helper thread work is waited for
    twin.setUltraRender (request.ultra);
    twin.prepare (request.sampleRate, kBlock, quality);
    if (afterPrepare)
        afterPrepare (twin);
    const auto* table = paramTable();
    if (request.values != nullptr)
        for (size_t i = 0; i < paramCount(); ++i)
            twin.setParam (table[i].id, request.values->plain[i]);
    for (const auto& o : request.overrides)
        twin.setParam (o.id, o.value);
    TransportInfo transport;
    transport.bpm = request.bpm > 0.0 ? request.bpm : 120.0;
    twin.setTransport (transport);

    out.sampleRate = request.sampleRate;
    out.left.reserve (static_cast<size_t> (total * request.sampleRate) + kBlock);
    out.right.reserve (out.left.capacity());
    std::vector<float> l (kBlock), r (kBlock);
    int32_t noteId = 1;
    bool first = true;

    for (const auto& step : request.steps)
    {
        out.stepStart.push_back (out.left.size());
        for (const auto& o : step.overrides)
            twin.setParam (o.id, o.value);
        if (step.reset || first)
            twin.reset();
        first = false;

        const size_t stepFrames = framesOf (step.seconds, request.sampleRate);
        std::vector<TimedEvent> events;
        for (const auto& note : step.notes)
        {
            const int64_t on = static_cast<int64_t> (framesOf (note.startSeconds, request.sampleRate));
            if (on >= static_cast<int64_t> (stepFrames))
                continue;
            const int64_t off = std::min<int64_t> (static_cast<int64_t> (stepFrames) - 1, on + std::max<int64_t> (1, static_cast<int64_t> (framesOf (note.lengthSeconds, request.sampleRate))));
            Event e;
            e.channel = static_cast<int16_t> (std::clamp (note.channel, 0, 15));
            e.key = static_cast<int16_t> (std::clamp (note.key, 0, 127));
            e.noteId = noteId++;
            e.type = Event::NoteOn;
            e.value = std::clamp (note.velocity, 0.0f, 1.0f);
            events.push_back ({ on, e });
            e.type = Event::NoteOff;
            e.value = 0.0f;
            events.push_back ({ std::max (off, on), e });
        }
        std::stable_sort (events.begin(), events.end(), [] (const TimedEvent& a, const TimedEvent& b) { return a.frame < b.frame; });

        size_t done = 0, next = 0;
        std::vector<Event> block;
        while (done < stepFrames)
        {
            const int n = static_cast<int> (std::min<size_t> (kBlock, stepFrames - done));
            block.clear();
            while (next < events.size() && events[next].frame < static_cast<int64_t> (done) + n)
            {
                Event e = events[next++].event;
                e.frame = static_cast<int32_t> (std::max<int64_t> (0, events[next - 1].frame - static_cast<int64_t> (done)));
                block.push_back (e);
            }
            twin.process (block.empty() ? nullptr : block.data(), static_cast<int> (block.size()), l.data(), r.data(), n);
            append (out, l.data(), r.data(), n);
            done += static_cast<size_t> (n);
        }
        // a note whose release was clamped to the last frame of the step is off now; the next step (reset) cuts what still rings
    }

    if (request.tailSeconds > 0.0)
    {
        const size_t tailFrames = framesOf (request.tailSeconds, request.sampleRate);
        const size_t silentNeeded = framesOf (kSilentSeconds, request.sampleRate);
        size_t done = 0, silentRun = 0;
        while (done < tailFrames)
        {
            const int n = static_cast<int> (std::min<size_t> (kBlock, tailFrames - done));
            twin.process (nullptr, 0, l.data(), r.data(), n);
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
                peak = std::max ({ peak, std::fabs (l[static_cast<size_t> (i)]), std::fabs (r[static_cast<size_t> (i)]) });
            append (out, l.data(), r.data(), n);
            done += static_cast<size_t> (n);
            silentRun = peak < kSilent ? silentRun + static_cast<size_t> (n) : 0;
            if (silentRun >= silentNeeded)
            {
                const size_t drop = std::min (silentRun, out.left.size());
                out.left.resize (out.left.size() - drop);
                out.right.resize (out.right.size() - drop);
                break;
            }
        }
    }
    return ! out.left.empty();
}

bool renderOfflineOnTwin (const IEngine& live, EngineKind kind, const StateContent* content, const IContentStore* store, const OfflineRequest& request, OfflineAudio& out)
{
    auto twin = createEngine (kind);
    if (twin == nullptr)
        return false;
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        twin->setParam (table[i].id, live.getParam (table[i].id));
    return runOfflineRequest (*twin, request, out, [content, store] (IEngine& e)
    {
        e.setContentStore (store);
        if (content != nullptr)
            e.setContent (*content);
    });
}

bool renderOfflineAny (IEngine& live, EngineKind fallbackKind, const StateContent* content, const IContentStore* store, const OfflineRequest& request, OfflineAudio& out)
{
    if (live.renderOffline (request, out))
        return true;
    return renderOfflineOnTwin (live, fallbackKind, content, store, request, out);
}
} // namespace synthyboy
