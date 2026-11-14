// SynthyBoy - MPC overlay tests: the GPU scene protocol (gpu/scene.h): the seqlock reader / writer (also against a writer on another thread), the shared block, the messages, and the
// serialisation of every view type: the scene of a view, interpreted by the reference interpreter, is the picture the CPU renderer makes of the same data.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#include "fakes.h"
#include "scene_reference.h"
#include "synthyboy/readout.h"

using namespace ovt;
using namespace synthyboy_mpc::overlay;

namespace
{
/** The biggest and the number of differing pixels between two canvases (premultiplied ARGB), per channel. */
void compare (const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int& maxDiff, size_t& differing)
{
    maxDiff = 0;
    differing = 0;
    if (a.size() != b.size())
    {
        maxDiff = 255;
        differing = a.size();
        return;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        int m = 0;
        for (int s = 0; s < 32; s += 8)
            m = std::max (m, std::abs (static_cast<int> ((a[i] >> s) & 255u) - static_cast<int> ((b[i] >> s) & 255u)));
        maxDiff = std::max (maxDiff, m);
        differing += m > 2;
    }
}

void checkSameAsCpu (const std::string& name, const IView& view, int viewIndex, int w, int h)
{
    gpu::SceneData sd;
    gpu::SceneBuilder sb (sd);
    check (view.describe (sb, viewIndex, w, h) && ! sb.overflow, name + ": the view describes itself as a scene");
    std::vector<uint32_t> cpu (static_cast<size_t> (w) * static_cast<size_t> (h)), ref;
    const_cast<IView&> (view).render (cpu.data(), w, h);
    renderSceneReference (sd, ref);
    int maxDiff;
    size_t differing;
    compare (cpu, ref, maxDiff, differing);
    check (maxDiff <= 3 && differing == 0, name + ": the scene is the CPU picture (largest channel difference " + std::to_string (maxDiff) + ", pixels differing by more than 2: " + std::to_string (differing) + ")");
    check (sd.w == w && sd.h == h && sd.viewIndex == static_cast<uint32_t> (viewIndex), name + ": scene header (canvas size, view index)");
}
}

int main()
{
    // ---- layout of the shared structures ----------------------------------------------------------------------------------------------------------------------
    check (sizeof (gpu::Msg) == 64 && sizeof (gpu::SceneOp) == 32 && sizeof (gpu::SceneData) == 32 + 32 * gpu::kMaxOps + 4 * gpu::kMaxCurve, "message, op and scene have the fixed sizes of the protocol");
    check (sizeof (gpu::SharedBlock) % 4 == 0 && gpu::kProtocolVersion == 2, "the shared block is word aligned, protocol version 2");
    {
        auto* b = new gpu::SharedBlock;
        gpu::initBlock (b);
        check (gpu::blockValid (b) && b->seq == 0, "a fresh block is valid");
        b->version = gpu::kProtocolVersion - 1;
        check (! gpu::blockValid (b), "a block of the previous protocol version is refused");
        b->version = gpu::kProtocolVersion;
        b->version = 99;
        check (! gpu::blockValid (b), "a block of another version is refused");
        gpu::initBlock (b);
        gpu::ledgerSet (b, 3, 77, 88);
        uint32_t hd = 0, fb = 0;
        gpu::ledgerGet (b, 3, hd, fb);
        check (hd == 77 && fb == 88, "the ledger keeps a GEM handle and a framebuffer id");
        delete b;
    }
    {
        const gpu::Msg m = gpu::makeMsg (gpu::kMsgShow, 1, 2, 3, 4, 5, 6, "a view name that is far too long to fit into the 32 bytes of the text");
        check (m.magic == gpu::kMsgMagic && m.type == gpu::kMsgShow && m.a == 1 && m.f == 6 && m.text[31] == 0 && std::strlen (m.text) == 31, "makeMsg fills the fields and cuts the text to 31 characters");
    }
    // ---- the builder's limits ---------------------------------------------------------------------------------------------------------------------------------
    {
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        for (int i = 0; i < gpu::kMaxOps + 5; ++i)
            sb.rect (0, 0, 1, 1, 0);
        check (sb.overflow && sd.numOps == static_cast<uint32_t> (gpu::kMaxOps), "more ops than the scene holds: the extra ones are dropped and overflow is set");
        gpu::SceneData s2;
        gpu::SceneBuilder sb2 (s2);
        std::vector<float> many (gpu::kMaxCurve + 10, 1.0f);
        sb2.polyline (many.data(), static_cast<int> (many.size()));
        check (sb2.overflow && s2.numPoints == static_cast<uint32_t> (gpu::kMaxCurve), "a polyline longer than the scene holds is cut and overflow is set");
    }
    // ---- the gradient op (protocol 2): the colour at the two rows, the baseline, the second colour travels in the op ----------------------------------------------------
    {
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        sb.fillGradient (120.0f, 8.0f, 120.0f, 0x11223344u, 0x55667788u);
        check (sd.numOps == 1 && sd.ops[0].op == gpu::kOpFillGradient && sd.ops[0].rgba == 0x11223344u && sd.ops[0].rgba2 == 0x55667788u && sd.ops[0].f[0] == 120.0f && sd.ops[0].f[1] == 8.0f && sd.ops[0].f[2] == 120.0f,
               "fillGradient: baseline, the two rows and both colours are in the op");
        check (draw::gradientAt (draw::premultiplied (255, 0, 0, 200), draw::premultiplied (0, 0, 255, 20), 10.0, 110.0, 0) == draw::premultiplied (255, 0, 0, 200)
               && draw::gradientAt (draw::premultiplied (255, 0, 0, 200), draw::premultiplied (0, 0, 255, 20), 10.0, 110.0, 120) == draw::premultiplied (0, 0, 255, 20), "the gradient is constant outside the two rows");
        const uint32_t ca = draw::premultiplied (200, 100, 0, 200), mid = draw::gradientAt (ca, draw::premultiplied (0, 0, 0, 0), 0.0, 100.0, 49);   // the centre of row 49 is at 49.5: 49.5 % of the way
        check (std::abs (static_cast<int> (mid >> 24) - static_cast<int> (std::lround (200 * (1.0 - 0.495)))) <= 1 && std::abs (static_cast<int> ((mid >> 16) & 255u) - static_cast<int> (std::lround (static_cast<double> ((ca >> 16) & 255u) * 0.505))) <= 1,
               "row 49 of a gradient from row 0 to row 100: 49.5 % of the way in every premultiplied channel");
    }
    // ---- the seqlock ------------------------------------------------------------------------------------------------------------------------------------------
    {
        auto* b = new gpu::SharedBlock;
        gpu::initBlock (b);
        gpu::SceneData in, out;
        gpu::SceneBuilder sb (in);
        sb.begin (gpu::kKindFilter, 4, 100, 50);
        sb.clear (0x11223344u);
        sb.disc (1.5f, 2.5f, 3.5f, 0xaabbccddu);
        const float rows[3] = { 1.0f, 2.0f, 3.0f };
        sb.polyline (rows, 3);
        uint32_t seq0 = 0, seq1 = 0;
        const uint32_t w1 = gpu::writeScene (b, in);
        check (w1 == 2 && (w1 & 1u) == 0 && gpu::readScene (b, out, seq0) && seq0 == w1, "write then read: an even sequence number, the same one on both sides");
        check (std::memcmp (&in, &out, sizeof in) == 0, "the scene comes back bit for bit");
        const uint32_t w2 = gpu::writeScene (b, in);
        check (w2 == 4 && gpu::readScene (b, out, seq1) && seq1 == 4, "the second scene is number 4");
        b->seq = 7;   // a writer that died in the middle of a write: the reader gives up instead of waiting for ever
        check (! gpu::readScene (b, out, seq0, 50), "an odd sequence number (a writer in progress) is never read (the reader gives up after its tries)");
        delete b;
    }
    {
        // a writer on another thread writes scenes whose every word is the same counter value; a reader must never see two different values in one copy
        auto* b = new gpu::SharedBlock;
        gpu::initBlock (b);
        std::atomic<bool> stop { false };
        std::thread writer ([&]
        {
            gpu::SceneData sd;
            uint32_t v = 1;
            while (! stop.load())
            {
                for (int i = 0; i < gpu::kSceneWords; ++i)
                    std::memcpy (reinterpret_cast<char*> (&sd) + 4 * i, &v, 4);
                gpu::writeScene (b, sd);
                ++v;
                std::this_thread::sleep_for (std::chrono::microseconds (30));   // the plugin writes 30 scenes a second; this is a harsh 30 000
            }
        });
        long reads = 0, torn = 0, retried = 0;
        uint32_t last = 0;
        bool monotonic = true;
        for (int i = 0; i < 20000; ++i)
        {
            gpu::SceneData sd;
            uint32_t seq;
            if (! gpu::readScene (b, sd, seq))
            {
                ++retried;
                continue;
            }
            uint32_t first;
            std::memcpy (&first, &sd, 4);
            if (seq != 0)
            {
                for (int k = 0; k < gpu::kSceneWords; ++k)
                {
                    uint32_t w;
                    std::memcpy (&w, reinterpret_cast<const char*> (&sd) + 4 * k, 4);
                    torn += w != first;
                }
                monotonic = monotonic && seq >= last;
                last = seq;
            }
            ++reads;
        }
        stop.store(true);
        writer.join();
        check (reads > 1000 && torn == 0, "seqlock against a writer on another thread: " + std::to_string (reads) + " reads, no torn copy (" + std::to_string (torn) + " words differed), " + std::to_string (retried) + " reads gave up");
        check (monotonic, "the sequence numbers a reader sees never go back");
        delete b;
    }
    // ---- every view type: the scene is the CPU picture ---------------------------------------------------------------------------------------------------------
    CpuCurveRenderer renderer;
    {   // the filters
        FakeInstance inst;
        inst.set ("filter1.cutoff", 1800.0f);
        inst.set ("filter1.resonance", 55.0f);
        FilterView v (1, renderer, 10.0);
        ViewInput in;
        in.instance = &inst;
        in.nowMs = 0.0;
        v.update (in);
        const layout::Rect& r = layout::kViews[0].region;
        checkSameAsCpu ("filter 1", v, 0, r.w, r.h);
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        v.describe (sb, 0, r.w, r.h);
        bool rows = sd.numPoints == 256;
        for (int i = 0; i < 256 && rows; ++i)
            rows = std::fabs (sd.y[i] - std::clamp (CpuCurveRenderer::dbToY (static_cast<double> (v.curve()->curve()[i]), r.h), -60.0, r.h + 60.0)) < 1e-3;
        check (rows, "filter 1: the 256 polyline rows are the dB values through the renderer's own dB to row mapping");
        check (sd.ops[0].op == gpu::kOpClear && (sd.ops[0].rgba & 255u) == 255u && sd.ops[sd.numOps - 1].op == gpu::kOpStroke, "filter 1: an opaque clear first, the line last");
        FilterView empty (1, renderer, 10.0);
        gpu::SceneData s2;
        gpu::SceneBuilder sb2 (s2);
        check (! empty.describe (sb2, 0, r.w, r.h), "a filter view without data does not describe itself");
    }
    {   // an LFO with its cursor, and without
        FakeInstance inst;
        inst.set ("lfo1.shape", 4.0f);
        LfoView v (1, 10.0);
        synthyboy::LiveValues lv;
        inst.hasLive = true;
        inst.live (lv);
        lv.lfoPhase[0] = 0.3f;
        ViewInput in;
        in.instance = &inst;
        in.live = &lv;
        in.nowMs = 0.0;
        check (v.update (in) && v.valid(), "lfo 1 view has data");
        int idx = 0;
        for (int i = 0; i < layout::kNumViews; ++i)
            if (std::string (layout::kViews[i].name) == "lfo1") idx = i;
        const layout::Rect& r = layout::kViews[idx].region;
        checkSameAsCpu ("lfo 1 with the cursor", v, idx, r.w, r.h);
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        v.describe (sb, idx, r.w, r.h);
        int discs = 0, vlines = 0;
        for (uint32_t k = 0; k < sd.numOps; ++k) { discs += sd.ops[k].op == gpu::kOpDisc; vlines += sd.ops[k].op == gpu::kOpVLine; }
        check (discs == 2 && vlines == 1 && sd.numPoints == 300, "lfo 1: the cursor is one line and two discs, the shape has 300 points");
        lv.lfoPhase[0] = -1.0f;
        in.nowMs = 100.0;
        v.update (in);
        checkSameAsCpu ("lfo 1 without a voice", v, idx, r.w, r.h);
        gpu::SceneData s3;
        gpu::SceneBuilder sb3 (s3);
        v.describe (sb3, idx, r.w, r.h);
        int cur = 0;
        for (uint32_t k = 0; k < s3.numOps; ++k) cur += s3.ops[k].op == gpu::kOpDisc;
        check (cur == 0, "lfo 1 without a voice: no cursor in the scene");
    }
    {   // an oscillator: a wavetable, and the empty frame of one that is off
        FakeInstance inst;
        OscView v (1, 10.0);
        ViewInput in;
        in.instance = &inst;
        in.nowMs = 0.0;
        check (v.update (in) && v.valid(), "osc 1 view has data");
        int idx = 0;
        for (int i = 0; i < layout::kNumViews; ++i)
            if (std::string (layout::kViews[i].name) == "osc1") idx = i;
        const layout::Rect& r = layout::kViews[idx].region;
        checkSameAsCpu (std::string ("osc 1 (") + (v.kind() == synthyboy::OscCycle::Kind::wavetable ? "wavetable" : "other") + ")", v, idx, r.w, r.h);
        inst.set ("osc1.on", 0.0f);
        in.nowMs = 200.0;
        v.update (in);
        checkSameAsCpu ("osc 1 switched off", v, idx, r.w, r.h);
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        v.describe (sb, idx, r.w, r.h);
        check (v.kind() != synthyboy::OscCycle::Kind::wavetable ? sd.numPoints == 0 : sd.numPoints == 512, "osc 1: a wavetable carries the 512 point cycle, any other kind the empty frame");
    }
    {   // the envelope marker: transparent, two marker covers, a dot
        FakeInstance inst;
        FakeFrame frame;
        int ei = 0;
        for (int i = 0; i < layout::kNumViews; ++i)
            if (std::string (layout::kViews[i].name) == "env2") ei = i;
        EnvView v (2, layout::kViews[ei]);
        const layout::Rect& reg = layout::kViews[ei].region;
        inst.set ("env2.attack", 300.0f);
        inst.set ("env2.sustain", 60.0f);
        ViewInput in;
        in.instance = &inst;
        in.frames = &frame;
        in.rotation = 90;
        v.update (in);
        // the widget's line under the dot, as the envelope tests draw it
        for (int st = 1; st <= 6; ++st)
            for (int k = 0; k <= 400; ++k)
            {
                const envgeom::Point pt = envgeom::dotFor (v.normOf(), st, k / 400.0);
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dy = -2; dy <= 2; ++dy)
                    {
                        int px, py;
                        uiPointToPanel (90, static_cast<int> (std::lround (pt.x)) + dx, static_cast<int> (std::lround (pt.y)) + dy, 800, 1280, px, py);
                        frame.setPixel (px, py, envgeom::kLineRgb);
                    }
            }
        checkSameAsCpu ("env 2 without a voice (only the marker covers)", v, ei, reg.w, reg.h);
        synthyboy::LiveValues lv;
        inst.hasLive = true;
        inst.live (lv);
        lv.envFineStage[1] = 2.0f;
        lv.envFineFraction[1] = 0.5f;
        in.live = &lv;
        in.nowMs = 10.0;
        v.update (in);
        check (v.dotX() >= 0, "env 2: the dot is on the line");
        checkSameAsCpu ("env 2 with the dot", v, ei, reg.w, reg.h);
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        v.describe (sb, ei, reg.w, reg.h);
        check (sd.ops[0].op == gpu::kOpClear && sd.ops[0].rgba == 0u && sd.numPoints == 0, "env 2: the clear is transparent, no polyline");
    }
    {
        NullView n ("none");
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        check (! n.describe (sb, 0, 10, 10), "a view without a scene description (the null view) is drawn by the CPU");
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
