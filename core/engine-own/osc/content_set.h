// SynthyBoy - own engine: the decoded content of a state (user wavetables, samples with their analysis, multisample instruments, drawn curves).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/multisample/multisample.h"
#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/spectral/spectral.h"
#include "wavetable_bank.h"

namespace synthyboy
{
namespace own
{
/** Everything the audio thread reads of the content section, decoded off the audio thread (IEngine::setContent / setContentStore) and handed over as one
    immutable object: the voices keep raw pointers into it, the engine frees it only when no block uses it any more. */
struct ContentSet
{
    /** index 0 .. 2 = oscillator 1 .. 3, 3 = the noise source */
    struct Sample
    {
        bool valid = false;
        sampleosc::SampleData data;
        sampleosc::SampleView view;
        spectral::Analysis analysis;
        std::vector<float> manualSlices;                      // content.samples[].slices
        std::vector<sampleosc::SampleData::Onset> onsets;     // for the Auto slice mode
    };
    Sample sample[4];
    std::unique_ptr<Wavetable> userTable[3];
    std::unique_ptr<multisample::Instrument> instrument[3];
    std::vector<float> remap[3][4];                           // 257 points or empty (identity)
    std::vector<float> specFilterX[3], specFilterY[3];        // the drawn spectral filter mask (empty = flat)
    size_t bytes = 0;                                         // rough memory use

    ContentSet() = default;
    ContentSet (const ContentSet&) = delete;
    ContentSet& operator= (const ContentSet&) = delete;

    /** Decodes `content` with `store` (null store: only what needs no file). Not real time. */
    static std::unique_ptr<ContentSet> build (const StateContent& content, const IContentStore* store);
};

/** A drawn curve (points with power) sampled at 257 positions. */
std::vector<float> sampleCurve257 (const std::vector<ContentPoint>& points);
} // namespace own
} // namespace synthyboy
