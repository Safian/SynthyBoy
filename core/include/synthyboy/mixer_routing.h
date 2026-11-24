// SynthyBoy - shared read-only Mixer routing-closure presentation helper.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "synthyboy/params.h"

namespace synthyboy
{

/** Visual presentation state for Mixer Solo / FX Enable / Mute buttons.
    Matches the FilmStrip frame indices in components/native/solo_mute/solo_mute.py. */
namespace mixer_visual
{
constexpr int kNeutral = 0;
constexpr int kActive = 1;            // explicit Solo (cyan), explicit Mute (red), or Master FX ON (blue)
constexpr int kImplicitSoloMute = 2;  // dimmed due to another channel's solo (without mutating writable Mute)
constexpr int kDisabled = 3;          // channel source OFF (Sub, Osc A..C, Noise)
} // namespace mixer_visual

struct MixerRouteParams
{
    int subOn = -1, oscOn[3] = { -1, -1, -1 }, noiseOn = -1;
    int subDest = -1, oscDest[3] = { -1, -1, -1 }, noiseDest = -1;
    int subSend1 = -1, subSend2 = -1;
    int oscSend1[3] = { -1, -1, -1 }, oscSend2[3] = { -1, -1, -1 };
    int noiseSend1 = -1, noiseSend2 = -1;
    int filterSend1[2] = { -1, -1 }, filterSend2[2] = { -1, -1 };
    int filterRouting = -1;
    int bus1Dest = -1, bus2Dest = -1;
    int mainFxBypass = -1;

    static const MixerRouteParams& get() noexcept
    {
        static const MixerRouteParams table = []
        {
            MixerRouteParams p;
            auto idx = [] (const char* key) { return findParamIndex (hashKey (key)); };
            p.subOn = idx ("sub.on");
            p.subDest = idx ("sub.destination");
            p.subSend1 = idx ("sub.bus1_send");
            p.subSend2 = idx ("sub.bus2_send");
            const char* const oscKeys[3] = { "osc1", "osc2", "osc3" };
            for (int o = 0; o < 3; ++o)
            {
                const std::string prefix (oscKeys[o]);
                p.oscOn[o] = findParamIndex (hashKey ((prefix + ".on").c_str()));
                p.oscDest[o] = findParamIndex (hashKey ((prefix + ".destination").c_str()));
                p.oscSend1[o] = findParamIndex (hashKey ((prefix + ".bus1_send").c_str()));
                p.oscSend2[o] = findParamIndex (hashKey ((prefix + ".bus2_send").c_str()));
            }
            p.noiseOn = idx ("noise.on");
            p.noiseDest = idx ("noise.destination");
            p.noiseSend1 = idx ("noise.bus1_send");
            p.noiseSend2 = idx ("noise.bus2_send");
            p.filterSend1[0] = idx ("filter1.bus1_send");
            p.filterSend2[0] = idx ("filter1.bus2_send");
            p.filterSend1[1] = idx ("filter2.bus1_send");
            p.filterSend2[1] = idx ("filter2.bus2_send");
            p.filterRouting = idx ("filter.routing");
            p.bus1Dest = idx ("bus1.destination");
            p.bus2Dest = idx ("bus2.destination");
            p.mainFxBypass = idx ("main.fx_bypass");
            return p;
        }();
        return table;
    }
};

struct MixerRouteState
{
    uint32_t effectiveSoloMask = 0;
    std::array<bool, 10> stripOn {};
    std::array<bool, 10> routeActive {};
    std::array<int, 10> soloVisual {};   // 0..8: Solo visual (0..3), 9: Master FX visual (0=off, 1=on)
    std::array<int, 10> muteVisual {};   // 0..9: Mute visual (0..3)
};

/** Computes the read-only per-strip effective routing closure matching OwnEngine + Voice::route(). */
template <typename ReadParam>
inline MixerRouteState computeMixerRouteState (uint32_t rawSoloMask, uint32_t rawMuteMask, ReadParam&& readParam) noexcept
{
    const auto& p = MixerRouteParams::get();
    auto val = [&] (int idx, float fallback = 0.0f) noexcept -> float {
        return idx >= 0 ? readParam (idx) : fallback;
    };
    auto choice = [&] (int idx) noexcept -> int {
        return idx >= 0 ? static_cast<int> (readParam (idx) + 0.5f) : 0;
    };
    auto hasSend = [&] (int idx) noexcept -> bool {
        return idx >= 0 && readParam (idx) > 0.0f;
    };

    MixerRouteState out {};
    out.stripOn[0] = p.subOn < 0 || val (p.subOn, 1.0f) >= 0.5f;
    for (int o = 0; o < 3; ++o)
        out.stripOn[1 + o] = p.oscOn[o] < 0 || val (p.oscOn[o], 1.0f) >= 0.5f;
    out.stripOn[4] = p.noiseOn < 0 || val (p.noiseOn, 1.0f) >= 0.5f;
    for (int s = 5; s < 10; ++s)
        out.stripOn[static_cast<size_t> (s)] = true; // filters (bypass only), buses, master stay active

    // Effective solo mask excludes powered-off generators and Master (bit 9)
    uint32_t effSolo = 0;
    for (int s = 0; s < 9; ++s)
        if ((rawSoloMask & (1u << s)) != 0 && out.stripOn[static_cast<size_t> (s)])
            effSolo |= (1u << s);
    out.effectiveSoloMask = effSolo;

    const bool anySolo = effSolo != 0;
    const bool f0Muted = (rawMuteMask & (1u << 5)) != 0;
    const bool f1Muted = (rawMuteMask & (1u << 6)) != 0;
    const bool b1Muted = (rawMuteMask & (1u << 7)) != 0;
    const bool b2Muted = (rawMuteMask & (1u << 8)) != 0;

    const bool f1Solo = (effSolo & (1u << 5)) != 0;
    const bool f2Solo = (effSolo & (1u << 6)) != 0;
    const bool b1Solo = (effSolo & (1u << 7)) != 0;
    const bool b2Solo = (effSolo & (1u << 8)) != 0;

    const int b1Dest = choice (p.bus1Dest);
    const int b2Dest = choice (p.bus2Dest);
    const int fRouting = choice (p.filterRouting);

    const bool b1LeadsToSolo = b1Solo || (b2Solo && b1Dest == 2 && ! b1Muted);
    const bool b2LeadsToSolo = b2Solo || (b1Solo && b2Dest == 2 && b1Dest != 2 && ! b2Muted);

    bool f1LeadsToSolo = f1Solo || (! f0Muted && ((hasSend (p.filterSend1[0]) && b1LeadsToSolo) || (hasSend (p.filterSend2[0]) && b2LeadsToSolo)));
    bool f2LeadsToSolo = f2Solo || (! f1Muted && ((hasSend (p.filterSend1[1]) && b1LeadsToSolo) || (hasSend (p.filterSend2[1]) && b2LeadsToSolo)));
    if (fRouting == 1 && ! f0Muted && f2LeadsToSolo)
        f1LeadsToSolo = true;
    else if (fRouting == 2 && ! f1Muted && f1LeadsToSolo)
        f2LeadsToSolo = true;

    auto feedsFilter = [] (int dest, int f) noexcept { return dest == f || dest == 2 || dest == 5; };

    const int srcDest[5] {
        choice (p.subDest), choice (p.oscDest[0]), choice (p.oscDest[1]), choice (p.oscDest[2]), choice (p.noiseDest)
    };
    const bool srcSend1[5] {
        hasSend (p.subSend1), hasSend (p.oscSend1[0]), hasSend (p.oscSend1[1]), hasSend (p.oscSend1[2]), hasSend (p.noiseSend1)
    };
    const bool srcSend2[5] {
        hasSend (p.subSend2), hasSend (p.oscSend2[0]), hasSend (p.oscSend2[1]), hasSend (p.oscSend2[2]), hasSend (p.noiseSend2)
    };

    bool f1Reached = f1Solo;
    bool f2Reached = f2Solo;
    for (int s = 0; s < 5; ++s)
    {
        if ((effSolo & (1u << s)) != 0)
        {
            if (feedsFilter (srcDest[s], 0)) f1Reached = true;
            if (feedsFilter (srcDest[s], 1)) f2Reached = true;
        }
    }
    if (fRouting == 1 && f1Reached && ! f0Muted)
        f2Reached = true;
    else if (fRouting == 2 && f2Reached && ! f1Muted)
        f1Reached = true;

    auto computeBusActive = [&] (int b) noexcept -> bool
    {
        if (! anySolo)
            return true;
        if (b == 1)
        {
            if (b1Solo) return true;
            for (int s = 0; s < 5; ++s)
                if ((effSolo & (1u << s)) != 0 && srcSend1[s]) return true;
            if (f1Reached && ! f0Muted && hasSend (p.filterSend1[0])) return true;
            if (f2Reached && ! f1Muted && hasSend (p.filterSend1[1])) return true;
            if (b2Dest == 2 && b1Dest != 2 && ! b2Muted)
            {
                if (b2Solo) return true;
                for (int s = 0; s < 5; ++s)
                    if ((effSolo & (1u << s)) != 0 && srcSend2[s]) return true;
                if (f1Reached && ! f0Muted && hasSend (p.filterSend2[0])) return true;
                if (f2Reached && ! f1Muted && hasSend (p.filterSend2[1])) return true;
            }
            if (b1Dest == 2 && b2Solo) return true;
            return false;
        }
        if (b2Solo) return true;
        for (int s = 0; s < 5; ++s)
            if ((effSolo & (1u << s)) != 0 && srcSend2[s]) return true;
        if (f1Reached && ! f0Muted && hasSend (p.filterSend2[0])) return true;
        if (f2Reached && ! f1Muted && hasSend (p.filterSend2[1])) return true;
        if (b1Dest == 2 && ! b1Muted)
        {
            if (b1Solo) return true;
            for (int s = 0; s < 5; ++s)
                if ((effSolo & (1u << s)) != 0 && srcSend1[s]) return true;
            if (f1Reached && ! f0Muted && hasSend (p.filterSend1[0])) return true;
            if (f2Reached && ! f1Muted && hasSend (p.filterSend1[1])) return true;
        }
        if (b2Dest == 2 && b1Dest != 2 && b1Solo) return true;
        return false;
    };

    for (int s = 0; s < 5; ++s)
    {
        if (! out.stripOn[static_cast<size_t> (s)])
            out.routeActive[static_cast<size_t> (s)] = false;
        else if (! anySolo)
            out.routeActive[static_cast<size_t> (s)] = true;
        else
        {
            const bool selfSolo = (effSolo & (1u << s)) != 0;
            const bool feedsActiveFilter = (feedsFilter (srcDest[s], 0) && f1LeadsToSolo)
                                        || (feedsFilter (srcDest[s], 1) && f2LeadsToSolo);
            const bool sendsToActiveBus = (srcSend1[s] && b1LeadsToSolo) || (srcSend2[s] && b2LeadsToSolo);
            out.routeActive[static_cast<size_t> (s)] = selfSolo || feedsActiveFilter || sendsToActiveBus;
        }
    }
    out.routeActive[5] = ! anySolo || f1LeadsToSolo || f1Reached;
    out.routeActive[6] = ! anySolo || f2LeadsToSolo || f2Reached;
    out.routeActive[7] = computeBusActive (1);
    out.routeActive[8] = computeBusActive (2);
    out.routeActive[9] = true; // Master is never implicitly muted by channel solo

    for (int s = 0; s < 10; ++s)
    {
        const bool on = out.stripOn[static_cast<size_t> (s)];
        const bool uSolo = s < 9 && (rawSoloMask & (1u << s)) != 0;
        const bool uMute = (rawMuteMask & (1u << s)) != 0;
        const bool implicitMuted = on && ! uMute && anySolo && ! out.routeActive[static_cast<size_t> (s)];

        if (s == 9)
        {
            const bool fxEnabled = p.mainFxBypass < 0 || val (p.mainFxBypass, 0.0f) < 0.5f;
            out.soloVisual[9] = fxEnabled ? mixer_visual::kActive : mixer_visual::kNeutral;
        }
        else if (! on)
            out.soloVisual[static_cast<size_t> (s)] = mixer_visual::kDisabled;
        else if (uSolo)
            out.soloVisual[static_cast<size_t> (s)] = mixer_visual::kActive;
        else if (implicitMuted)
            out.soloVisual[static_cast<size_t> (s)] = mixer_visual::kImplicitSoloMute;
        else
            out.soloVisual[static_cast<size_t> (s)] = mixer_visual::kNeutral;

        if (! on)
            out.muteVisual[static_cast<size_t> (s)] = mixer_visual::kDisabled;
        else if (uMute)
            out.muteVisual[static_cast<size_t> (s)] = mixer_visual::kActive;
        else if (implicitMuted)
            out.muteVisual[static_cast<size_t> (s)] = mixer_visual::kImplicitSoloMute;
        else
            out.muteVisual[static_cast<size_t> (s)] = mixer_visual::kNeutral;
    }

    return out;
}

} // namespace synthyboy
