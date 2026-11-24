// SynthyBoy - shell-mpc acceptance: a minimal VST2 stub host (state round trip with a `content` section, two-thread parameter writes while processing).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Usage: stub_host <plugin library>. Links the core only to build a state document that has content. Run the ThreadSanitizer build of it too.
#include "mpc_vst2_abi.h"
#include "synthyboy/content_state.h"
#include "synthyboy/state.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <vector>

static VstIntPtr hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    return opcode == audioMasterVersion ? 2400 : 0;
}

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}

static std::string getChunk (AEffect* e)
{
    void* data = nullptr;
    const VstIntPtr n = e->dispatcher (e, effGetChunk, 0, 0, &data, 0.0f);
    return (data != nullptr && n > 0) ? std::string (static_cast<const char*> (data), static_cast<size_t> (n)) : std::string();
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: stub_host <plugin>\n"); return 2; }
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) { std::fprintf (stderr, "dlopen: %s\n", dlerror()); return 2; }
    using MainProc = AEffect* (*) (audioMasterCallback);
    auto entry = reinterpret_cast<MainProc> (dlsym (lib, "VSTPluginMain"));
    if (entry == nullptr) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }
    AEffect* e = entry (hostCallback);
    if (e == nullptr || e->magic != kEffectMagic) { std::fprintf (stderr, "bad AEffect\n"); return 2; }

    e->dispatcher (e, effOpen, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    e->dispatcher (e, effSetBlockSize, 0, 128, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    // 1. state round trip with a content section (a custom LFO shape and a velocity curve: no file references, so no content store is needed)
    synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
    synthyboy::StateContent content;
    synthyboy::ContentLfoShape shape;
    shape.lfo = 2; shape.name = "AcceptShape"; shape.smooth = true;
    shape.points = { { 0.0f, 0.0f }, { 0.25f, 1.0f }, { 0.6f, 0.3f }, { 1.0f, 0.0f } };
    content.lfoShapes.push_back (shape);
    content.velocityCurve = { { 0.0f, 0.0f }, { 0.5f, 0.8f }, { 1.0f, 1.0f } };
    const std::string doc = synthyboy::saveState (values, content);
    check (doc.find ("AcceptShape") != std::string::npos, "the test document has a content section");

    e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (doc.size()), const_cast<char*> (doc.data()), 0.0f);
    const std::string a = getChunk (e);
    check (a.find ("AcceptShape") != std::string::npos, "content survives effSetChunk -> effGetChunk");
    check (a.find ("lfo_shapes") != std::string::npos, "the saved chunk has the lfo_shapes part");
    e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (a.size()), const_cast<char*> (a.data()), 0.0f);
    const std::string b = getChunk (e);
    check (! a.empty() && a == b, "chunk with content is byte-identical after a second round trip");

    // 2. sample rate / block size change must re-prepare without crashing and still render
    e->dispatcher (e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 48000.0f);
    e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.0f);
    e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.0f);

    // 3. two writer threads (the MPC writes parameters from the audio thread and from the UI thread) while the audio thread processes
    std::vector<float> left (256), right (256);
    float* outs[2] = { left.data(), right.data() };
    std::atomic<bool> run { true };
    const VstInt32 n = e->numParams;
    auto writer = [&] (unsigned seed)
    {
        unsigned s = seed;
        while (run.load())
        {
            s = s * 1664525u + 1013904223u;
            const VstInt32 index = static_cast<VstInt32> ((s >> 8) % static_cast<unsigned> (n));
            e->setParameter (e, index, static_cast<float> ((s >> 4) & 0xffff) / 65535.0f);
            (void) e->getParameter (e, index);
        }
    };
    std::thread t1 (writer, 1u), t2 (writer, 2u);
    bool finite = true;
    for (int block = 0; block < 400; ++block)
    {
        e->processReplacing (e, nullptr, outs, 256);
        for (int i = 0; i < 256; ++i) finite = finite && left[i] == left[i] && right[i] == right[i];
        if (block % 50 == 0) (void) getChunk (e);
    }
    run.store (false);
    t1.join(); t2.join();
    check (finite, "audio stayed finite while two threads wrote parameters");
    e->processReplacing (e, nullptr, outs, 0);
    check (true, "a zero-frame process call returned");

    // 4. event flood: two note-ons, far more controller events than any buffer holds, then the two note-offs, all for one block.
    //    Whatever is dropped, the note-offs must arrive: afterwards the instrument has to go quiet.
    {
        const std::string init = synthyboy::saveState (synthyboy::ParamValues::defaults());
        e->dispatcher (e, effSetChunk, 0, static_cast<VstIntPtr> (init.size()), const_cast<char*> (init.data()), 0.0f);
        const int flood = 4000, after = 300, total = 2 + flood + 2 + after;
        std::vector<VstMidiEvent> midi (static_cast<size_t> (total));
        std::vector<char> block (sizeof (VstEvents) + sizeof (VstEvent*) * static_cast<size_t> (total));
        auto* list = reinterpret_cast<VstEvents*> (block.data());
        list->numEvents = total; list->reserved = 0;
        auto put = [&] (int i, int status, int d1, int d2, int frame)
        {
            VstMidiEvent& m = midi[static_cast<size_t> (i)];
            std::memset (&m, 0, sizeof (m));
            m.type = kVstMidiType; m.byteSize = sizeof (VstMidiEvent); m.deltaFrames = frame;
            m.midiData[0] = static_cast<char> (status); m.midiData[1] = static_cast<char> (d1); m.midiData[2] = static_cast<char> (d2);
            list->events[i] = reinterpret_cast<VstEvent*> (&m);
        };
        put (0, 0x90, 60, 100, 0); put (1, 0x90, 64, 100, 0);
        for (int i = 0; i < flood; ++i) put (2 + i, 0xB0, 1, i % 128, 10 + (i * 200) / flood);
        put (2 + flood, 0x80, 60, 0, 250); put (3 + flood, 0x80, 64, 0, 250);
        for (int i = 0; i < after; ++i) put (4 + flood + i, 0xB0, 1, i % 128, 252); // controllers after the note-offs must not push them out
        e->dispatcher (e, effProcessEvents, 0, 0, list, 0.0f);
        double tailPeak = 0.0; bool sounded = false;
        const int blocks = (48000 * 4) / 256;
        for (int b2 = 0; b2 < blocks; ++b2)
        {
            e->processReplacing (e, nullptr, outs, 256);
            for (int i = 0; i < 256; ++i)
            {
                const double v = left[i] < 0 ? -left[i] : left[i];
                if (v > 1e-4) sounded = true;
                if (b2 >= blocks - 40 && v > tailPeak) tailPeak = v;
            }
        }
        check (sounded, "event flood: the notes sounded");
        check (tailPeak < 1e-4, "event flood: both note-offs arrived (quiet 4 s later)");
    }

    e->dispatcher (e, effMainsChanged, 0, 0, nullptr, 0.0f);
    e->dispatcher (e, effClose, 0, 0, nullptr, 0.0f);
    std::printf ("stub_host: %s\n", fails == 0 ? "OK" : "FAILED");
    return fails == 0 ? 0 : 1;
}
