// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
// Included by the existing VST host fixture: one focused engine-to-native-meter acceptance.
#include <fstream>

namespace
{
void testMixerStereoSignalAndReadonlyBridge()
{
    constexpr int frames = 256;
    auto set = [] (synthyboy::IEngine& e, const std::string& key, float value)
    {
        const auto* info = synthyboy::findParamByKey (key.c_str());
        check (info != nullptr, "stereo fixture parameter exists");
        if (info) e.setParam (info->id, value);
    };
    auto make = [&] (float pan, bool allSources = false)
    {
        auto e = synthyboy::createOwnEngine();
        e->setParam (synthyboy::kParamMasterPolyphony, 32.0f);
        for (const char* source : { "sub", "osc1", "osc2", "osc3", "noise" })
        {
            set (*e, std::string (source) + ".on", allSources || std::strcmp (source, "osc1") == 0 ? 1.0f : 0.0f);
            set (*e, std::string (source) + ".level", 35.0f);
            set (*e, std::string (source) + ".pan", pan);
            set (*e, std::string (source) + ".destination", 2.0f);
            set (*e, std::string (source) + ".bus1_send", 25.0f);
            set (*e, std::string (source) + ".bus2_send", 20.0f);
        }
        for (const char* source : { "osc1", "osc2", "osc3" })
        {
            set (*e, std::string (source) + ".wavetable", 0.0f);
            set (*e, std::string (source) + ".random_phase", 0.0f);
        }
        set (*e, "filter1.on", 0); set (*e, "filter2.on", 0); // true bypass passes stereo filter inputs
        set (*e, "filter1.level", 0); set (*e, "filter2.level", 0);
        set (*e, "main.fx_bypass", 1); set (*e, "bus1.fx_bypass", 1); set (*e, "bus2.fx_bypass", 1);
        set (*e, "master.volume", -12); set (*e, "env1.attack", 1); set (*e, "env1.release", 5);
        e->prepare (48000.0, frames, synthyboy::qualityForTier (synthyboy::Tier::high));
        return e;
    };
    auto sound = [&] (synthyboy::IEngine& e, bool meter, int notes = 1)
    {
        std::vector<synthyboy::Event> events;
        for (int i = 0; i < notes; ++i)
        {
            synthyboy::Event note;
            note.type = synthyboy::Event::NoteOn; note.key = 69; note.value = 1.0f; note.noteId = i + 1;
            events.push_back (note);
        }
        float left[frames] {}, right[frames] {};
        for (int block = 0; block < 45; ++block)
        {
            if (meter) e.requestMeters();
            e.process (block == 0 ? events.data() : nullptr, block == 0 ? notes : 0, left, right, frames);
        }
        synthyboy::LiveValues live;
        check (e.copyReadout (live), "engine publishes stereo readout");
        return live;
    };
    synthyboy::LiveValues leftTap;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto e = make (ch == 0 ? -100.0f : 100.0f, true);
        const auto live = sound (*e, true);
        if (ch == 0) leftTap = live;
        for (int strip = 0; strip < 10; ++strip)
        {
            const float on = ch == 0 ? live.stripPeakL[strip] : live.stripPeakR[strip];
            const float off = ch == 0 ? live.stripPeakR[strip] : live.stripPeakL[strip];
            if (! (on > .1f && off == 0.0f))
                std::printf ("stereo pan side=%d strip=%d on=%.6f off=%.6f\n", ch, strip, on, off);
            check (on > .1f && off == 0.0f, "all ten strips report genuine asymmetric hard-pan stereo");
        }
    }
    auto centered = make (0.0f);
    auto center = sound (*centered, true);
    check (center.stripPeakL[1] > .1f && std::fabs (center.stripPeakL[1] - center.stripPeakR[1]) < .0001f,
           "center source reports both real channels equally");
    auto quiet = make (-100.0f);
    set (*quiet, "osc1.level", 10);
    const auto low = sound (*quiet, true);
    check (low.stripClipL[1] == 0 && low.stripClipR[1] == 0, "quiet waveform does not light clip");
    auto hot = make (-100.0f);
    set (*hot, "osc1.level", 10);
    set (*hot, "master.volume", -60);
    const auto clipped = sound (*hot, true, 24);
    check (clipped.stripClipL[1] == 1 && clipped.stripClipR[1] == 0,
           "same fader clips only the genuinely over-range polyphonic left waveform");
    check (clipped.stripClipL[9] == 0 && clipped.stripClipR[9] == 0, "quiet final Master is not inferred clipped from upstream gain");
    check (clipped.stripHoldL[1] >= clipped.stripPeakL[1] && clipped.stripHoldR[1] == 0,
           "peak holds remain independent per side");
    hot->reset();
    synthyboy::LiveValues reset;
    hot->copyReadout (reset);
    check (reset.stripClipL[1] == 0 && reset.stripHoldL[1] == 0, "reset clears stereo clip and hold");

    auto measured = make (0.0f), reference = make (0.0f);
    synthyboy::Event note; note.type = synthyboy::Event::NoteOn; note.key = 67; note.value = .83f;
    float ml[frames] {}, mr[frames] {}, rl[frames] {}, rr[frames] {};
    bool equal = true;
    for (int block = 0; block < 60; ++block)
    {
        measured->requestMeters();
        measured->process (block == 0 ? &note : nullptr, block == 0 ? 1 : 0, ml, mr, frames);
        reference->process (block == 0 ? &note : nullptr, block == 0 ? 1 : 0, rl, rr, frames);
        equal &= std::memcmp (ml, rl, sizeof (ml)) == 0 && std::memcmp (mr, rr, sizeof (mr)) == 0;
    }
    check (equal, "enabling stereo source measurement leaves rendered audio bit-identical");

    Rig rig;
    namespace ns = synthyboy_mpc::nativeui;
    check (ns::kMixerStereoBase == 900 && ns::kCount == 960, "new stereo fields append after all existing native IDs");
    for (int strip = 0; strip < 10; ++strip)
    {
        const float fields[] { leftTap.stripPeakL[strip], leftTap.stripPeakR[strip], leftTap.stripHoldL[strip],
                               leftTap.stripHoldR[strip], strip == 1 ? clipped.stripClipL[1] : leftTap.stripClipL[strip], leftTap.stripClipR[strip] };
        rig.plugin.overlayView.setNativeMixerStereoMeter (strip, fields);
        for (int field = 0; field < 6; ++field)
        {
            const int index = PluginInstance::nativeUiBase() + ns::kMixerStereoBase + field * 10 + strip;
            check (std::fabs (rig.effect.getParameter (&rig.effect, index) - fields[field]) < .00001f,
                   "VST stereo parameter exposes the actual independent engine field");
            rig.effect.setParameter (&rig.effect, index, .123f);
            check (std::fabs (rig.effect.getParameter (&rig.effect, index) - fields[field]) < .00001f,
                   "VST stereo meter ignores writes");
            check (rig.effect.dispatcher (&rig.effect, effCanBeAutomated, index, 0, nullptr, 0) == 0,
                   "VST stereo meter is not automatable");
        }
    }
    auto fixture = make (0.0f, true);
    set (*fixture, "sub.pan", -20); set (*fixture, "osc2.pan", -35); set (*fixture, "osc3.pan", 35); set (*fixture, "noise.pan", 15);
    const auto visible = sound (*fixture, true);
    const char* path = std::getenv ("SYNTHYBOY_MIXER_READOUT_JSON");
    if (path && *path)
    {
        std::ofstream out (path);
        out << "{\"strips\":[";
        for (int strip = 0; strip < 10; ++strip)
        {
            if (strip) out << ',';
            out << "{\"PeakL\":" << visible.stripPeakL[strip] << ",\"PeakR\":" << visible.stripPeakR[strip]
                << ",\"HoldL\":" << visible.stripHoldL[strip] << ",\"HoldR\":" << visible.stripHoldR[strip]
                << ",\"ClipL\":" << visible.stripClipL[strip] << ",\"ClipR\":" << visible.stripClipR[strip] << '}';
        }
        out << "]}";
        check (out.good(), "actual engine fixture JSON is saved");
    }
    std::printf ("Mixer stereo focused acceptance: %d failures\n", failures);
}
}
