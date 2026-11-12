// SynthyBoy - own engine: the tuned note of every key (scale conforming, tuning scales, A4, user tuning files, MTS-ESP) (original code).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/tuning.h"

namespace synthyboy
{
namespace own
{
/** `master.scale.*`: the notes of the scale (semitones above the key) of every scale type 1 .. 19, in the order of the choice list. */
inline int conformKey (int key, int rootKey, int scaleType) noexcept
{
    static const int kLen[20] = { 12, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6, 8, 8, 7, 7, 7, 5 };
    static const int kSteps[20][8] = {
        { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 11 },
        { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 6, 8, 10 },
        { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 2, 4, 6, 8, 10 }, { 0, 1, 3, 4, 6, 7, 9, 10 },
        { 0, 2, 3, 5, 6, 8, 9, 11 }, { 0, 1, 4, 5, 7, 8, 10 }, { 0, 2, 3, 6, 7, 8, 11 }, { 0, 1, 4, 5, 7, 8, 11 }, { 0, 2, 3, 7, 8 } };
    if (scaleType <= 0 || scaleType > 19)
        return key;
    auto inScale = [&] (int k)
    {
        const int pc = ((k - rootKey) % 12 + 12) % 12;
        for (int i = 0; i < kLen[scaleType]; ++i)
            if (kSteps[scaleType][i] == pc)
                return true;
        return false;
    };
    for (int d = 0; d < 12; ++d) // the nearest in-scale key, a tie goes down (A7 of 13-new-keys.md)
    {
        if (key - d >= 0 && inScale (key - d))
            return key - d;
        if (key + d <= 127 && inScale (key + d))
            return key + d;
    }
    return key;
}

/** The tuned notes (fractional MIDI notes, 69 = A4 at 440 Hz): rebuilt on the audio thread once per block from the cheap inputs; the heavy part (the scales
    and the user file) is made in the constructor / on the message thread. NaN = the tuning does not map the key (it does not sound). */
class NoteTuning
{
public:
    NoteTuning()
    {
        buildScale (pyth, { { 256, 243 }, { 9, 8 }, { 32, 27 }, { 81, 64 }, { 4, 3 }, { 729, 512 }, { 3, 2 }, { 128, 81 }, { 27, 16 }, { 16, 9 }, { 243, 128 }, { 2, 1 } });
        buildScale (just, { { 16, 15 }, { 9, 8 }, { 6, 5 }, { 5, 4 }, { 4, 3 }, { 45, 32 }, { 3, 2 }, { 8, 5 }, { 5, 3 }, { 9, 5 }, { 15, 8 }, { 2, 1 } });
        for (auto& u : userNote)
            u.store (0.0f, std::memory_order_relaxed);
    }

    /** Message thread: the tuning file of the state (empty = none). */
    void setContent (const ContentTuning& t)
    {
        if (!t.present)
        {
            userPresent.store (false, std::memory_order_release);
            return;
        }
        tuning::Tuning tu;
        bool ok = true;
        if (!t.scl.empty())
            ok = tuning::parseScl (t.scl, tu).ok();
        if (ok && t.hasKbm && !t.kbm.empty())
            tuning::parseKbm (t.kbm, tu);
        if (!ok)
            tu = tuning::Tuning();
        const tuning::Table tab = tu.table();
        for (int k = 0; k < 128; ++k)
            userNote[static_cast<size_t> (k)].store (tab[static_cast<size_t> (k)] > 0.0 ? static_cast<float> (69.0 + 12.0 * std::log2 (tab[static_cast<size_t> (k)] / 440.0)) : -1000.0f,
                                                     std::memory_order_relaxed);
        userPresent.store (true, std::memory_order_release);
    }

    /** Any thread. */
    void setSource (const tuning::ITuningSource* s) noexcept { source.store (s, std::memory_order_release); }

    /** Audio thread: fills `out[128]`. scaleChoice: 0 12-TET, 1 Pythagorean, 2 Just intonation, 3 User file. */
    void compute (float* out, int scaleChoice, float a4Hz, bool useMts) noexcept
    {
        const float shift = static_cast<float> (12.0 * std::log2 (std::max (a4Hz, 1.0f) / 440.0f));
        const std::array<float, 128>* tab = nullptr;
        if (scaleChoice == 1)
            tab = &pyth;
        else if (scaleChoice == 2)
            tab = &just;
        const bool user = scaleChoice == 3 && userPresent.load (std::memory_order_acquire);
        for (int k = 0; k < 128; ++k)
        {
            float v = static_cast<float> (k) + shift;
            if (tab != nullptr)
                v = (*tab)[static_cast<size_t> (k)] + shift;
            else if (user)
            {
                const float u = userNote[static_cast<size_t> (k)].load (std::memory_order_relaxed);
                v = u < -999.0f ? std::nanf ("") : u + shift;
            }
            out[k] = v;
        }
        const tuning::ITuningSource* src = useMts ? source.load (std::memory_order_acquire) : nullptr;
        if (src != nullptr && src->isConnected())
        {
            std::array<double, tuning::kNumKeys> f {};
            if (src->query128Keys (f))
                for (int k = 0; k < 128; ++k)
                    if (f[static_cast<size_t> (k)] > 0.0)
                        out[k] = static_cast<float> (69.0 + 12.0 * std::log2 (f[static_cast<size_t> (k)] / 440.0));
        }
    }

private:
    struct Ratio { int n, d; };
    /** A twelve note scale on C (degree 0 = C) with A = 440 Hz as the reference. */
    static void buildScale (std::array<float, 128>& out, const std::vector<Ratio>& r)
    {
        std::vector<double> cents (13, 0.0);
        for (size_t i = 0; i < r.size(); ++i)
            cents[i + 1] = 1200.0 * std::log2 (static_cast<double> (r[i].n) / r[i].d);
        const double aCents = cents[9]; // degree 9 is the A
        for (int k = 0; k < 128; ++k)
        {
            const int rel = k - 60;
            const int oct = rel >= 0 ? rel / 12 : -((-rel + 11) / 12);
            const int deg = rel - oct * 12;
            const double c = cents[static_cast<size_t> (deg)] + 1200.0 * oct - aCents; // cents relative to A4 (key 69 is degree 9 of octave 0)
            out[static_cast<size_t> (k)] = static_cast<float> (69.0 + c / 100.0);
        }
    }

    std::array<float, 128> pyth {}, just {};
    std::array<std::atomic<float>, 128> userNote;
    std::atomic<bool> userPresent { false };
    std::atomic<const tuning::ITuningSource*> source { nullptr };
};
} // namespace own
} // namespace synthyboy
