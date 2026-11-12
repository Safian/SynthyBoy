// SynthyBoy - own engine: the decoded content of a state.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "content_set.h"

#include <algorithm>
#include <cmath>

#include "../dsp/util.h"

namespace synthyboy
{
namespace own
{
std::vector<float> sampleCurve257 (const std::vector<ContentPoint>& pts)
{
    std::vector<float> out;
    if (pts.size() < 2)
        return out;
    out.resize (257);
    size_t seg = 0;
    for (int i = 0; i < 257; ++i)
    {
        const float x = static_cast<float> (i) / 256.0f;
        while (seg + 2 < pts.size() && x > pts[seg + 1].x)
            ++seg;
        const ContentPoint& a = pts[seg];
        const ContentPoint& b = pts[seg + 1];
        float v;
        if (x <= a.x)
            v = a.y;
        else if (x >= b.x)
            v = b.y;
        else
        {
            const float t = (x - a.x) / std::max (b.x - a.x, 1.0e-9f);
            v = a.y + (b.y - a.y) * powerShape (t, a.power);
        }
        out[static_cast<size_t> (i)] = clamp01 (v);
    }
    return out;
}

std::unique_ptr<ContentSet> ContentSet::build (const StateContent& content, const IContentStore* store)
{
    auto set = std::make_unique<ContentSet>();
    for (const ContentRemap& r : content.remaps)
        if (r.osc >= 1 && r.osc <= 3 && r.slot >= 1 && r.slot <= kNumRemapSlots)
            set->remap[r.osc - 1][r.slot - 1] = sampleCurve257 (r.points);
    for (const ContentRemap& r : content.spectralFilters)
        if (r.osc >= 1 && r.osc <= 3)
        {
            for (const ContentPoint& p : r.points)
            {
                set->specFilterX[r.osc - 1].push_back (p.x);
                set->specFilterY[r.osc - 1].push_back (p.y);
            }
        }
    if (store == nullptr)
        return set;
    std::vector<uint8_t> bytes;
    for (const ContentWavetable& w : content.wavetables)
    {
        if (w.osc < 1 || w.osc > 3 || !store->readFile (w.ref, bytes))
            continue;
        std::vector<float> l, r;
        sampleosc::WavInfo info;
        if (!sampleosc::readWav (bytes.data(), bytes.size(), l, r, info, nullptr) || l.empty())
            continue;
        int frames = static_cast<int> (l.size() / Wavetable::kCycle);
        if (frames < 1)
        {
            l.resize (Wavetable::kCycle, 0.0f); // a short file is one frame, zero padded
            frames = 1;
        }
        frames = std::min (frames, 256);
        set->userTable[w.osc - 1] = makeWavetableFromSamples (l.data(), frames);
        if (set->userTable[w.osc - 1])
            set->bytes += set->userTable[w.osc - 1]->memoryBytes();
    }
    if (!content.impulses.empty() && store->readFile (content.impulses[0], bytes))
    {
        std::vector<float> l, r;
        sampleosc::WavInfo info;
        if (sampleosc::readWav (bytes.data(), bytes.size(), l, r, info, nullptr) && !l.empty())
        {
            auto imp = std::make_shared<ContentSet::Impulse>();
            imp->frames = static_cast<int> (l.size());
            imp->rate = info.sampleRate > 0 ? static_cast<double> (info.sampleRate) : 48000.0;
            imp->data.resize (l.size() * 2);
            for (size_t i = 0; i < l.size(); ++i)
            {
                imp->data[i * 2] = l[i];
                imp->data[i * 2 + 1] = i < r.size() ? r[i] : l[i];
            }
            set->bytes += imp->data.size() * sizeof (float);
            set->impulse = std::move (imp);
        }
    }
    for (const ContentSample& s : content.samples)
    {
        if (s.osc < 1 || s.osc > 4 || !store->readFile (s.ref, bytes))
            continue;
        ContentSet::Sample& dst = set->sample[s.osc - 1];
        if (!dst.data.loadFromMemory (bytes.data(), bytes.size(), s.ref.name.empty() ? nullptr : s.ref.name.c_str()))
            continue;
        dst.view = dst.data.view();
        dst.valid = dst.view.isValid();
        if (!dst.valid)
            continue;
        dst.manualSlices = s.slices;
        dst.onsets = dst.data.detectOnsets (256);
        set->bytes += static_cast<size_t> (dst.data.numFrames()) * 2 * sizeof (float) * 2;
        if (s.osc <= 3)
        {
            dst.analysis.build (dst.view);
            set->bytes += static_cast<size_t> (dst.analysis.numFrames()) * static_cast<size_t> (dst.analysis.channels()) * 3 * spectral::kBins * sizeof (float);
        }
    }
    for (const ContentInstrument& in : content.instruments)
    {
        if (in.osc < 1 || in.osc > 3 || !store->readFile (in.ref, bytes))
            continue;
        const std::string text (bytes.begin(), bytes.end());
        const multisample::SfzInstrument sfz = multisample::parseSfz (text);
        // the samples are files of the same pack below the folder of the SFZ file
        std::string folder = in.ref.file;
        const size_t slash = folder.find_last_of ('/');
        folder = slash == std::string::npos ? std::string() : folder.substr (0, slash + 1);
        auto reader = [&] (const std::string& rel, std::vector<uint8_t>& out)
        {
            ContentFileRef ref;
            ref.pack = in.ref.pack;
            ref.file = folder + rel;
            return store->readFile (ref, out);
        };
        auto inst = std::make_unique<multisample::Instrument>();
        if (inst->build (sfz, reader) > 0)
            set->instrument[in.osc - 1] = std::move (inst);
    }
    return set;
}
} // namespace own
} // namespace synthyboy
