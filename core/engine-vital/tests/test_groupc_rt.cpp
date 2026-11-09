// SynthyBoy - engine-vital tests: the real-time rule for the granular, spectral and multisample oscillators (GROUPC): process () allocates nothing, also while the oscillator
// type changes under sounding notes (the 10 ms fade between the kinds), while the content is replaced, and at the extremes of every kind.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "alloc_hook.h"
#include "groupc_util.h"

using namespace groupc;

namespace
{
/** process () with the allocations of the audio thread counted into `allocs`; the output is checked for NaN / Inf and the bound. */
bool renderCounted (IEngine& engine, const std::vector<Event>& events, int totalFrames, int blockSize, unsigned long& allocs)
{
    std::vector<float> l (static_cast<size_t> (blockSize)), r (static_cast<size_t> (blockSize));
    std::vector<Event> local;
    size_t idx = 0;
    bool ok = true;
    for (int start = 0; start < totalFrames; start += blockSize)
    {
        local.clear();
        while (idx < events.size() && events[idx].frame < start + blockSize)
        {
            auto e = events[idx++];
            e.frame -= start;
            local.push_back (e);
        }
        const auto before = g_allocations.load();
        engine.process (local.data(), static_cast<int> (local.size()), l.data(), r.data(), blockSize);
        allocs += g_allocations.load() - before;
        for (int i = 0; i < blockSize; ++i)
            ok = ok && std::isfinite (l[static_cast<size_t> (i)]) && std::isfinite (r[static_cast<size_t> (i)]) && std::fabs (l[static_cast<size_t> (i)]) < 16.0f;
    }
    return ok;
}

Content everything()
{
    Content c;
    c.sample (twoTones (300.0, 900.0, 3.0));
    c.instrument ("<global> amp_veltrack=80 ampeg_release=0.1\n<region> sample=low.wav lokey=0 hikey=70 pitch_keycenter=60 loop_mode=loop_continuous loop_start=0 loop_end=48000 group=1 off_by=2\n"
                  "<region> sample=high.wav lokey=71 hikey=127 pitch_keycenter=84 group=2 seq_length=2 seq_position=1\n<region> sample=high.wav lokey=71 hikey=127 pitch_keycenter=84 seq_length=2 seq_position=2\n",
                  { { "low.wav", sine (440.0, 2.0) }, { "high.wav", sine (1760.0, 1.0) } });
    ContentRemap notch;
    notch.osc = 1;
    notch.slot = 1;
    notch.points = { ContentPoint (0.0f, 0.5f), ContentPoint (0.4f, 0.5f), ContentPoint (0.5f, 0.0f), ContentPoint (0.6f, 0.5f), ContentPoint (1.0f, 0.5f) };
    c.state.spectralFilters.push_back (notch);
    return c;
}
} // namespace

TEST (groupc_oscillator_types_allocate_nothing_in_process_even_while_the_type_changes)
{
    Content content = everything();
    auto e = makeVital();
    content.apply (*e);
    basePatch (*e, content, kOscTypeSample);
    setByKey (*e, "osc1.unison.voices", 8.0f);
    setByKey (*e, "osc1.gran.density", 400.0f);
    setByKey (*e, "osc1.gran.length", 300.0f);
    setByKey (*e, "osc1.gran.rand_pitch", 12.0f);
    setByKey (*e, "osc1.spec.phase_lock", 1.0f);
    setByKey (*e, "osc1.spec.transients", 1.0f);
    setByKey (*e, "osc1.sample.loop_mode", 3.0f);
    setByKey (*e, "osc1.warp.type", 77.0f);
    setByKey (*e, "osc1.warp.amount", 70.0f);
    // warm up every kind once (the tables of the sinc interpolation, the windows, the FFT are built at the first use of a kind: on the message thread in a plugin, here in prepare / first touch)
    unsigned long warm = 0;
    for (int type : { 1, 2, 3, 4, 0 })
    {
        setByKey (*e, "osc1.type", static_cast<float> (type));
        renderCounted (*e, { noteOn (0, 60) }, 4096, 256, warm);
    }
    e->reset();

    unsigned long allocs = 0;
    bool finite = true;
    std::vector<Event> events;
    for (int i = 0; i < 12; ++i)
        events.push_back (noteOn (i * 2400, 40 + 7 * i, 0.4f + 0.05f * static_cast<float> (i)));
    // the type changes under the running notes: every kind, every transition
    int step = 0;
    for (int type : { 2, 3, 4, 1, 3, 2, 4, 0, 2, 1, 4, 3 })
    {
        setByKey (*e, "osc1.type", static_cast<float> (type));
        finite = renderCounted (*e, step == 0 ? events : std::vector<Event> {}, 3000, 100, allocs) && finite;
        ++step;
    }
    CHECK (finite);
    CHECK_EQ (allocs, 0ul);

    // the content is replaced while notes sound (on this thread: not counted), then the audio thread picks the new snapshot up without allocating
    unsigned long afterSwap = 0;
    Content other;
    other.sample (sine (500.0, 1.0));
    other.apply (*e);
    setByKey (*e, "osc1.type", 2.0f);
    finite = renderCounted (*e, { noteOn (0, 64), noteOn (500, 67) }, 9600, 256, afterSwap) && finite;
    CHECK (finite);
    CHECK_EQ (afterSwap, 0ul);
}

TEST (groupc_extreme_settings_of_every_kind_stay_finite_and_allocate_nothing)
{
    Content content = everything();
    for (int type : { 2, 3, 4 })
    {
        auto e = makeVital();
        content.apply (*e);
        basePatch (*e, content, type);
        setByKey (*e, "osc1.unison.voices", 16.0f);
        setByKey (*e, "osc1.unison.detune", 100.0f);
        setByKey (*e, "osc1.sample.scan", 100.0f);
        setByKey (*e, "osc1.sample.scan_range", 3.0f);
        setByKey (*e, "osc1.sample.loop_mode", 3.0f);
        setByKey (*e, "osc1.warp.type", type == 3 ? 95.0f : 27.0f);
        setByKey (*e, "osc1.warp.amount", 100.0f);
        setByKey (*e, "osc1.warp2.type", type == 3 ? 89.0f : 41.0f);
        setByKey (*e, "osc1.warp2.amount", 100.0f);
        setByKey (*e, "osc1.random_phase", 100.0f);
        setByKey (*e, "osc2.on", 1.0f);
        setByKey (*e, "osc2.level", 0.0f);
        if (type == 2)
        {
            setByKey (*e, "osc1.gran.density", 1000.0f);
            setByKey (*e, "osc1.gran.length", 4000.0f);
            setByKey (*e, "osc1.gran.rand_offset", 100.0f);
            setByKey (*e, "osc1.gran.rand_pitch", 24.0f);
        }
        if (type == 3)
        {
            setByKey (*e, "osc1.spec.freq_shift", -2000.0f);
            setByKey (*e, "osc1.spec.mix", 100.0f);
        }
        if (type == 4)
        {
            setByKey (*e, "osc1.multi.override", 1.0f);
            setByKey (*e, "osc1.multi.timbre", 100.0f);
        }
        unsigned long warm = 0;
        renderCounted (*e, { noteOn (0, 50) }, 4096, 256, warm);
        e->reset();
        unsigned long allocs = 0;
        std::vector<Event> events;
        for (int i = 0; i < 16; ++i)
            events.push_back (noteOn (i * 700, 12 + 7 * i));
        const bool finite = renderCounted (*e, events, 36000, 333, allocs);
        CHECK (finite);
        CHECK_EQ (allocs, 0ul);
    }
}
