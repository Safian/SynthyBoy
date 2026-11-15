// SynthyBoy - MPC overlay, GPU helper: the cache of the cached layers (what each layer holds from one picture to the next) and the plan of the PARTIAL picture (what part of the window is painted, from the age of
// the back buffer). Pure logic, no GL: the helper uses it, the Mac tests check every branch (buffer age 0 / 1 / 2 / more than the history / unknown, a changed clip, the diagnostics file, the 70 percent rule).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "cell_plan.h"

namespace synthyboy_mpc { namespace overlay { namespace gpu {

/** The cells every cached layer holds now (rectangle and hash of each): a picture asks what changed against them (`damage`) and, once the changed places are drawn, stores the new ones (`commit`). A layer that
    the scene does not use is forgotten (`forget`), a layer that is made again (another size) too. */
class LayerCache
{
public:
    static constexpr int kLayers = 2;

    /** The keys of the cells of layer l in the plan. */
    static std::vector<CellKey> keysOf (const CellPlan& plan, int l)
    {
        std::vector<CellKey> keys;
        for (const CellRef& c : plan.cells)
            if (c.layer == l)
                keys.push_back ({ c.x0, c.y0, c.x1, c.y1, c.hash });
        return keys;
    }
    /** The places of layer l to draw again for these keys (empty: nothing changed). */
    void damage (int l, const std::vector<CellKey>& keys, std::vector<DamageRect>& out) const { layerDamage (cells_[l], keys, out); }
    void commit (int l, const std::vector<CellKey>& keys) { cells_[l] = keys; }
    void forget (int l) { cells_[l].clear(); }
    void forgetAll() { for (auto& c : cells_) c.clear(); }
    size_t size (int l) const noexcept { return cells_[l].size(); }

private:
    std::vector<CellKey> cells_[kLayers];
};

/** The partial picture: remembers where the last pictures changed (EGL_EXT_buffer_age: a back buffer that is `age` pictures old needs what changed in the `age - 1` pictures after it and in this one) and says
    which boxes to paint now. */
class PartialPaint
{
public:
    static constexpr int kHistory = 4;

    /** `layerDamage`: the cells drawn again in the layers this picture; `live`: where the ops that are drawn every picture are now; `clip`: the clip box of the scene; `age`: the age of the back buffer (0 or less:
        unknown); `enabled`: false switches the partial picture off (diagnostics); w, h: the picture. Returns true with `out` = the boxes (inside the picture) to paint; false = paint all of it (age unknown or older than
        the history, the clip changed, switched off, or the boxes cover 70 percent or more of the picture, where boxes gain nothing). The state moves on either way. */
    bool plan (const std::vector<DamageRect>& layerDamage, const std::vector<DamageRect>& live, const int clip[4], int age, bool enabled, int w, int h, std::vector<DamageRect>& out)
    {
        std::vector<DamageRect> liveNow = live;
        mergeBoxes (liveNow, 6);
        std::vector<DamageRect> now = layerDamage;
        now.insert (now.end(), liveNow.begin(), liveNow.end());
        now.insert (now.end(), prevLive_.begin(), prevLive_.end());
        mergeBoxes (now, 6);
        const bool clipChanged = std::memcmp (clip, lastClip_, sizeof lastClip_) != 0;
        std::memcpy (lastClip_, clip, sizeof lastClip_);
        bool full = false;
        framePaint (now, hist_, count_, age, out, full);
        for (int i = kHistory - 1; i > 0; --i)
            hist_[i] = hist_[i - 1];
        hist_[0] = now;
        count_ = std::min (count_ + 1, kHistory);
        prevLive_ = liveNow;
        if (full || clipChanged || ! enabled)
            return false;
        long long area = 0;
        for (DamageRect& b : out)
        {
            b = { std::max (0, b.x0), std::max (0, b.y0), std::min (w, b.x1), std::min (h, b.y1) };
            area += static_cast<long long> (std::max (0, b.x1 - b.x0)) * std::max (0, b.y1 - b.y0);
        }
        return area * 10 < static_cast<long long> (w) * h * 7;
    }
    /** A picture that was drawn whole without cells: what the earlier ones changed does not help the next ones. */
    void reset() { count_ = 0; prevLive_.clear(); }
    int historyCount() const noexcept { return count_; }

private:
    std::vector<DamageRect> hist_[kHistory], prevLive_;
    int count_ = 0, lastClip_[4] = {};
};

}}} // namespace synthyboy_mpc::overlay::gpu
