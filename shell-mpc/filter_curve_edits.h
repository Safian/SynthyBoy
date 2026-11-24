// SynthyBoy - generation-bound edits of the two standalone PZ SVF content curves.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "synthyboy/content_state.h"

namespace synthyboy_mpc
{
class FilterCurveEdits
{
public:
    struct Edit
    {
        std::vector<synthyboy::ContentFilterCurvePoint> points;
        uint64_t generation = 0;
    };
    using Batch = std::array<Edit, 2>;

    void post (int filter, const std::vector<synthyboy::ContentFilterCurvePoint>& points, uint64_t generation)
    {
        if (filter < 1 || filter > 2 || points.size() < 2 || points.size() > synthyboy::kMaxFilterCurvePoints)
            return;
        std::lock_guard<std::mutex> guard (mutex_);
        pending_[filter - 1] = { points, generation };
    }

    bool take (Batch& out)
    {
        std::lock_guard<std::mutex> guard (mutex_);
        if (pending_[0].points.empty() && pending_[1].points.empty())
            return false;
        out = std::exchange (pending_, {});
        return true;
    }

    template <class Editable>
    static bool apply (synthyboy::StateContent& content, Batch&& queued, uint64_t generation, Editable&& editable)
    {
        bool changed = false;
        for (int i = 0; i < 2; ++i)
        {
            auto& edit = queued[i];
            if (edit.points.empty() || edit.generation != generation || ! editable (i + 1))
                continue;
            const char* key = i == 0 ? "filter1" : "filter2";
            const auto found = std::find_if (content.filterCurves.begin(), content.filterCurves.end(),
                                             [key] (const auto& old) { return old.filter == key; });
            if (found == content.filterCurves.end())
                content.filterCurves.push_back ({ key, std::move (edit.points) });
            else
                found->points = std::move (edit.points);
            changed = true;
        }
        return changed;
    }

private:
    std::mutex mutex_;
    Batch pending_;
};
} // namespace synthyboy_mpc
