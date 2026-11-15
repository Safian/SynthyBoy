// SynthyBoy - MPC overlay tests: the GPU scene protocol (gpu/scene.h): the seqlock reader / writer (also against a writer on another thread), the shared block, the messages, and the
// serialisation of every view type: the scene of a view, interpreted by the reference interpreter, is the picture the CPU renderer makes of the same data.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <limits>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
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
    check (sizeof (gpu::SharedBlock) % 4 == 0 && gpu::kProtocolVersion == 5, "the shared block is word aligned, protocol version 5");
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
    {   // the Play view: three panels, drawn once through a sink for the CPU and for the scene; every sub-tab's highlight; the selected instances
        FakeInstance inst;
        inst.set ("osc2.on", 1.0f);
        inst.set ("osc2.wavetable", 3.0f);
        inst.set ("osc2.frame", 40.0f);
        inst.set ("env3.attack", 300.0f);
        inst.set ("env3.decay", 900.0f);
        inst.set ("env3.sustain", 40.0f);
        inst.set ("env3.attack_curve", 8.0f);
        inst.set ("env3.release_curve", -6.0f);
        inst.set ("lfo4.shape", 3.0f);
        inst.set ("lfo4.rate", 1.3f);
        inst.playSel[0] = 2;
        inst.playSel[1] = 3;
        inst.playSel[2] = 4;
        synthyboy::LiveValues lv;
        inst.hasLive = true;
        inst.live (lv);
        for (int idx = 1; idx <= 3; ++idx)
        {
            PlayView v (idx, 10.0);
            lv.lfoPhase[3] = 0.35f;
            lv.envFineStage[2] = 4.0f;
            lv.envFineFraction[2] = 0.4f;
            ViewInput in;
            in.instance = &inst;
            in.io = &inst;
            UiStack uiStack;
            in.ui = &uiStack;
            in.live = &lv;
            in.nowMs = 0.0;
            v.update (in);
            const int vi = layout::kNumViews - 4 + idx;   // play1 .. play3 are the last three views of the layout
            check (std::string (layout::kViews[vi].name) == "play" + std::to_string (idx) && layout::kViews[vi].kind == layout::kKindPlay && layout::kViews[vi].index == idx, "the layout's last three views are play1 .. play3");
            Rect r;
            check (v.regionOverride (r) && r.w == PlayView::kW && r.h == PlayView::kH && r.y == 0 && r.w == 1280 && r.h == 800, "the Play view's region is the whole screen");
            check (v.valid(), "the Play view has data after one update");
            checkSameAsCpu ("play " + std::to_string (idx) + " (dot and cursor)", v, vi, r.w, r.h);
            gpu::SceneData sd;
            gpu::SceneBuilder sb (sd);
            v.describe (sb, vi, r.w, r.h);
            check (sd.kind == gpu::kKindPlay && sd.numOps < static_cast<uint32_t> (gpu::kMaxOps) && sd.numPoints <= static_cast<uint32_t> (gpu::kMaxCurve), "play: kind, op count and vertex count are inside the protocol (" + std::to_string (sd.numOps) + " ops, " + std::to_string (sd.numPoints) + " vertices)");
            int curves = 0, texts = 0;
            bool envTitle = false, lfoTitle = false;
            for (uint32_t k = 0; k < sd.numOps; ++k)
            {
                curves += sd.ops[k].op == gpu::kOpCurve;
                if (sd.ops[k].op == gpu::kOpText)
                {
                    char t[13];
                    gpu::textOf (sd.ops[k], t);
                    ++texts;
                    envTitle = envTitle || std::string (t) == "ENV 3";
                    lfoTitle = lfoTitle || std::string (t) == "LFO 4";
                }
            }
            check (curves == 3 && texts > 10 && envTitle && lfoTitle, "play: three polylines (the oscillator picture, the envelope, the LFO), the two panel titles and the texts of the oscillator UI");
            std::vector<uint32_t> cv (static_cast<size_t> (r.w) * static_cast<size_t> (r.h));
            v.render (cv.data(), r.w, r.h);
            // the frame of the panel of this sub-tab is the accent colour, the others have the quiet one
            bool frames = true;
            for (int p = 1; p < 3; ++p)   // the oscillator panel is the plugin-drawn compact view: no frame of the old kind
            {
                const uint32_t top = cv[static_cast<size_t> (play::kTabY + play::kOy + 1) * static_cast<size_t> (r.w) + static_cast<size_t> (play::kOx + p * (play::kPanelW + play::kPanelGap) + 100)];
                const bool accent = ((top >> 16) & 255u) > 0xe0 && ((top >> 8) & 255u) < 0x60;
                frames = frames && (accent == (p == idx - 1));
            }
            check (frames, "play " + std::to_string (idx) + ": only the Env / LFO panel of the sub-tab has the highlight frame");
        }
        {   // under a full screen / detail page / popup every pixel of the region is opaque (the host's Env / LFO picture and its Q-Link frame must not show through); in main the oscillator third is
            for (int depth = 0; depth < 3; ++depth)
            {
                PlayView pv (1, 10.0);
                UiStack st;
                if (depth >= 1) st.openFull (1);
                if (depth == 2) st.openDetail (13);
                ViewInput in;
                in.instance = &inst; in.io = &inst; in.ui = &st; in.nowMs = 0.0;
                pv.update (in);
                in.nowMs = 500.0; pv.update (in);
                std::vector<uint32_t> cv (static_cast<size_t> (PlayView::kW) * PlayView::kH);
                pv.render (cv.data(), PlayView::kW, PlayView::kH);
                int transparent = 0;
                for (int y = play::kTabY; y < play::kTabY + play::kTabH; ++y)
                    for (int x = 0; x < (depth == 0 ? 430 : PlayView::kW); ++x)
                        transparent += (cv[static_cast<size_t> (y) * PlayView::kW + static_cast<size_t> (x)] >> 24) != 255u;
                check (transparent == 0, std::string (depth == 0 ? "main: the oscillator third" : (depth == 1 ? "full screen: the whole region" : "popup: the whole region")) + " is opaque (" + std::to_string (transparent) + " transparent pixels)");
            }
        }
        {   // the host's Q-Link panel is up: our picture covers the strip x >= 1024 over the whole height (header and tab bar included), in main and under a full screen
            for (int depth = 0; depth < 2; ++depth)
            {
                PlayView pv (1, 10.0);
                UiStack st;
                if (depth == 1) st.openFull (1);
                ViewInput in;
                in.instance = &inst; in.io = &inst; in.ui = &st; in.nowMs = 0.0; in.qlinkPanel = true;
                pv.update (in);
                in.nowMs = 500.0; pv.update (in);
                std::vector<uint32_t> cv (static_cast<size_t> (PlayView::kW) * PlayView::kH);
                pv.render (cv.data(), PlayView::kW, PlayView::kH);
                int transparent = 0;
                for (int y = 0; y < PlayView::kH; ++y)
                    for (int x = 1024; x < PlayView::kW; ++x)
                        transparent += (cv[static_cast<size_t> (y) * PlayView::kW + static_cast<size_t> (x)] >> 24) != 255u;
                check (transparent == 0, std::string ("Q-Link panel up, ") + (depth ? "full screen" : "main") + ": the strip x >= 1024 is covered over the whole screen height (" + std::to_string (transparent) + " transparent pixels)");
            }
        }
        // the cursor / dot off (no voice): still the same picture on both renderers
        PlayView idle (2, 10.0);
        ViewInput in;
        in.instance = &inst;
        in.nowMs = 0.0;
        idle.update (in);
        checkSameAsCpu ("play (no voice)", idle, layout::kNumViews - 2, PlayView::kW, PlayView::kH);
    }
    {   // the scene ops of protocol 4: a curve with its own span, and text; the reference reads them the way the helper does
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        sb.begin (gpu::kKindPlay, 0, 200, 60);
        sb.clear (0x000000ffu);
        const float a[4] = { 10.0f, 50.0f, 10.0f, 50.0f }, b[3] = { 30.0f, 30.0f, 30.0f };
        sb.curve (a, 4, 20, 100);
        sb.stroke (2.0f, 0xffffffffu);
        sb.curve (b, 3, 130, 50);
        sb.fill (59.0f, 0xff0000ffu);
        sb.text (4, 4, 2, "Osc 12", 0x00ff00ffu);
        check (sd.numPoints == 7 && sd.ops[1].op == gpu::kOpCurve && sd.ops[1].f[0] == 0.0f && sd.ops[1].f[1] == 4.0f && sd.ops[1].f[2] == 20.0f && sd.ops[1].f[3] == 100.0f && sd.ops[3].f[0] == 4.0f, "curve ops carry the first vertex, the count and the span; the vertices are appended");
        char t[13];
        gpu::textOf (sd.ops[5], t);
        check (sd.ops[5].op == gpu::kOpText && std::string (t) == "Osc 12", "a text op carries up to 12 characters");
        std::vector<uint32_t> ref;
        renderSceneReference (sd, ref);
        auto at = [&] (int x, int y) { return ref[static_cast<size_t> (y) * 200u + static_cast<size_t> (x)]; };
        check ((at (5, 30) >> 16 & 255u) == 0 && (at (21, 11) >> 8 & 255u) > 100, "the first curve is inside its span (x 20 .. 119) and nothing is drawn left of it");
        check ((at (140, 45) >> 24) > 0 && (at (140, 45) >> 16 & 255u) > 0 && (at (200 - 1, 45) & 255u) == 0 && (at (100, 45) >> 16 & 255u) == 0, "the second curve's fill is inside its span (x 130 .. 179) only");
        bool title = false;
        for (int y = 4; y < 18 && ! title; ++y)
            for (int x = 4; x < 4 + font::textWidth (6, 2) && ! title; ++x)
                title = (at (x, y) >> 8 & 255u) > 200;
        check (title, "the text is drawn at its place");
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
    // ---- the polyline span of a Curve op comes from floats in shared memory: never trusted ----
    {
        int first = -1, n = -1;
        check (gpu::curveSpan (0.0f, 512.0f, first, n) && first == 0 && n == 512, "an ordinary span is taken as it is");
        check (gpu::curveSpan (512.4f, 255.6f, first, n) && first == 512 && n == 256, "the floats are rounded to the nearest vertex");
        check (gpu::curveSpan (gpu::kMaxCurve - 2.0f, 2.0f, first, n) && first == gpu::kMaxCurve - 2 && n == 2, "a span that ends exactly at the end of the table");
        check (! gpu::curveSpan (gpu::kMaxCurve - 1.0f, 2.0f, first, n), "one vertex past the end: refused");
        check (! gpu::curveSpan (-1.0f, 4.0f, first, n) && ! gpu::curveSpan (0.0f, 1.0f, first, n) && ! gpu::curveSpan (0.0f, 0.0f, first, n), "a negative start and fewer than two vertices: refused");
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        check (! gpu::curveSpan (nan, 4.0f, first, n) && ! gpu::curveSpan (0.0f, nan, first, n) && ! gpu::curveSpan (inf, 4.0f, first, n) && ! gpu::curveSpan (0.0f, inf, first, n), "NaN and infinity: refused (a conversion to int of those is undefined)");
        check (! gpu::curveSpan (2.0e9f, 2.0e9f, first, n) && ! gpu::curveSpan (3.0e9f, 4.0f, first, n) && ! gpu::curveSpan (1.0e30f, 1.0e30f, first, n), "sums and values that overflow int: refused");
    }
    // ---- the ops of the UI component library: Line, Arc, Frame (protocol 5): validation of the floats of the shared block, and what the CPU makes of them ----
    {
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        sb.line (1.0f, 2.0f, 30.0f, 4.0f, 3.0f, 0xffffffffu);
        sb.arc (20.0f, 20.0f, 10.0f, 0.0f, 1.5f, 4.0f, 0xffffffffu);
        sb.frame (2.0f, 3.0f, 20.0f, 15.0f, 2.0f, 0xffffffffu);
        check (sd.numOps == 3 && sd.ops[0].op == gpu::kOpLine && sd.ops[1].op == gpu::kOpArc && sd.ops[2].op == gpu::kOpFrame, "the builder writes the three new ops");
        gpu::LineGeom lg;
        gpu::ArcGeom ag;
        gpu::FrameGeom fg;
        check (gpu::lineGeom (sd.ops[0], lg) && lg.half == 1.5f && gpu::arcGeom (sd.ops[1], ag) && ag.half == 2.0f && ag.mode == 0 && gpu::frameGeom (sd.ops[2], fg) && fg.t == 2 && fg.x1 == 20, "ordinary ops are taken as they are");
        gpu::SceneOp bad = sd.ops[0];
        for (int i = 0; i < 5; ++i)
            for (float v : { nan, inf, -inf, 1.0e9f })
            {
                bad = sd.ops[0];
                bad.f[i] = v;
                check (! gpu::lineGeom (bad, lg), "line: NaN, infinity and a huge value in f[" + std::to_string (i) + "] are refused");
                bad = sd.ops[2];
                bad.f[i] = v;
                check (! gpu::frameGeom (bad, fg), "frame: NaN, infinity and a huge value in f[" + std::to_string (i) + "] are refused");
                bad = sd.ops[1];
                bad.f[i] = v;
                check (! gpu::arcGeom (bad, ag), "arc: NaN, infinity and a huge value in f[" + std::to_string (i) + "] are refused");
            }
        for (float v : { nan, inf, 0.0f, -1.0f, 5000.0f })
        {
            bad = sd.ops[1];
            gpu::setArcWidth (bad, v);
            check (! gpu::arcGeom (bad, ag), "arc: the width in rgba2 (NaN, infinity, 0, negative, beyond the limit) is refused");
        }
        bad = sd.ops[1];
        bad.f[2] = 0.0f;
        check (! gpu::arcGeom (bad, ag), "arc: radius 0 refused");
        bad = sd.ops[1];
        bad.f[4] = bad.f[3];
        check (! gpu::arcGeom (bad, ag), "arc: an empty sweep is refused");
        bad = sd.ops[0];
        bad.f[2] = bad.f[0];
        bad.f[3] = bad.f[1];
        check (! gpu::lineGeom (bad, lg), "line: a point (no direction) is refused");
        bad = sd.ops[2];
        bad.f[2] = bad.f[0];
        check (! gpu::frameGeom (bad, fg), "frame: an empty rectangle is refused");
        bad = sd.ops[2];
        bad.f[4] = 0.0f;
        check (! gpu::frameGeom (bad, fg), "frame: thickness 0 refused");
        // a scene full of garbage ops must not draw outside the canvas, crash or hang
        gpu::SceneData junk;
        std::memset (&junk, 0xff, sizeof junk);
        junk.w = 64;
        junk.h = 32;
        junk.numOps = gpu::kMaxOps;
        junk.numPoints = 0;
        for (uint32_t k = 0; k < junk.numOps; ++k)
            junk.ops[k].op = gpu::kOpLine + k % 3;
        std::vector<uint32_t> cv;
        renderSceneReference (junk, cv);
        check (cv.size() == 64u * 32u, "a scene of NaN ops is interpreted without a fault");
    }
    {
        // the pictures: a line, an arc, a frame
        auto render = [] (const std::function<void (gpu::SceneBuilder&)>& f, int w, int h, std::vector<uint32_t>& cv)
        {
            gpu::SceneData sd;
            gpu::SceneBuilder sb (sd);
            sb.begin (gpu::kKindUi, 0, w, h);
            f (sb);
            renderSceneReference (sd, cv);
        };
        auto alpha = [] (const std::vector<uint32_t>& cv, int w, int x, int y) { return static_cast<int> (cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)] >> 24); };
        std::vector<uint32_t> cv;
        render ([] (gpu::SceneBuilder& sb) { sb.line (4.0f, 10.0f, 28.0f, 10.0f, 4.0f, 0xffffffffu); }, 40, 20, cv);
        check (alpha (cv, 40, 10, 8) == 255 && alpha (cv, 40, 10, 11) == 255 && alpha (cv, 40, 10, 7) == 0 && alpha (cv, 40, 10, 12) == 0, "line: a horizontal line of width 4 covers exactly the rows 8 .. 11");
        check (alpha (cv, 40, 3, 10) == 0 && alpha (cv, 40, 4, 10) == 255 && alpha (cv, 40, 27, 10) == 255 && alpha (cv, 40, 28, 10) == 0, "line: flat ends exactly at the end points");
        render ([] (gpu::SceneBuilder& sb) { sb.line (4.0f, 4.0f, 28.0f, 16.0f, 3.0f, 0xffffffffu); }, 40, 20, cv);
        int sum = 0;
        for (uint32_t p : cv)
            sum += static_cast<int> (p >> 24);
        const double expected = 3.0 * std::sqrt (24.0 * 24.0 + 12.0 * 12.0) * 255.0;   // the area of the segment
        check (std::fabs (sum - expected) < 0.04 * expected, "line: a slanted line has about the area width * length (" + std::to_string (sum) + " against " + std::to_string (static_cast<int> (expected)) + ")");
        // arc: a quarter at the right-below (angles 0 .. pi/2: clockwise from the right to the bottom)
        render ([] (gpu::SceneBuilder& sb) { sb.arc (20.0f, 20.0f, 12.0f, 0.0f, 1.5707963f, 4.0f, 0xffffffffu); }, 40, 40, cv);
        check (alpha (cv, 40, 31, 21) > 200 && alpha (cv, 40, 21, 31) > 200 && alpha (cv, 40, 29, 29) > 200, "arc: the quarter right-below is drawn (clockwise from the right)");
        check (alpha (cv, 40, 8, 20) == 0 && alpha (cv, 40, 20, 8) == 0 && alpha (cv, 40, 8, 29) == 0 && alpha (cv, 40, 29, 8) == 0, "arc: nothing in the other three quarters");
        check (alpha (cv, 40, 20, 20) == 0 && alpha (cv, 40, 36, 36) == 0, "arc: nothing in the middle or outside the ring");
        // a sweep over pi (three quarters, angle 0.5 pi .. 2 pi: the bottom, left, top) and the whole ring
        render ([] (gpu::SceneBuilder& sb) { sb.arc (20.0f, 20.0f, 12.0f, 1.5707963f, 6.2831853f, 4.0f, 0xffffffffu); }, 40, 40, cv);
        check (alpha (cv, 40, 19, 31) > 200 && alpha (cv, 40, 8, 20) > 200 && alpha (cv, 40, 20, 8) > 200 && alpha (cv, 40, 28, 11) > 200 && alpha (cv, 40, 28, 28) == 0, "arc: a sweep of more than pi is drawn from the bottom over the left and the top to the right");
        render ([] (gpu::SceneBuilder& sb) { sb.arc (20.0f, 20.0f, 12.0f, -1.0f, 9.0f, 4.0f, 0xffffffffu); }, 40, 40, cv);
        check (alpha (cv, 40, 31, 21) > 200 && alpha (cv, 40, 8, 20) > 200 && alpha (cv, 40, 20, 8) > 200 && alpha (cv, 40, 20, 31) > 200, "arc: a sweep of 2 pi or more is the whole ring");
        // frame
        render ([] (gpu::SceneBuilder& sb) { sb.frame (2.0f, 3.0f, 20.0f, 15.0f, 2.0f, 0xffffffffu); }, 30, 20, cv);
        check (alpha (cv, 30, 2, 3) == 255 && alpha (cv, 30, 19, 14) == 255 && alpha (cv, 30, 3, 8) == 255 && alpha (cv, 30, 10, 4) == 255 && alpha (cv, 30, 10, 5) == 0 && alpha (cv, 30, 4, 8) == 0 && alpha (cv, 30, 20, 8) == 0 && alpha (cv, 30, 1, 8) == 0, "frame: an outline of thickness 2 inside the outer edges");
        // arcContains agrees with the angles
        bool all = true;
        for (int i = 0; i < 4000; ++i)
        {
            const double a0 = -3.0 + 0.0013 * i, sweep = 0.1 + 6.0 * ((i * 7919) % 1000) / 1000.0, ang = a0 + std::fmod (0.37 * i, 6.2831853);
            gpu::SceneOp o {};
            o.op = gpu::kOpArc; o.f[0] = 0.0f; o.f[1] = 0.0f; o.f[2] = 10.0f; o.f[3] = static_cast<float> (a0); o.f[4] = static_cast<float> (a0 + sweep);
            gpu::setArcWidth (o, 2.0f);
            gpu::ArcGeom g;
            if (! gpu::arcGeom (o, g)) { all = false; break; }
            double rel = std::fmod (ang - a0, 6.283185307179586);
            if (rel < 0) rel += 6.283185307179586;
            if (std::fabs (rel - sweep) < 1e-3 || rel < 1e-3 || rel > 6.2831 || sweep > 6.2831) continue;   // on a cut: either answer
            const bool want = rel <= sweep;
            if (gpu::arcContains (g, static_cast<float> (5.0 * std::cos (ang)), static_cast<float> (5.0 * std::sin (ang))) != want) { all = false; break; }
        }
        check (all, "arcContains (the cross product test, which the shader repeats) agrees with the angle comparison for 4000 arcs of 0.1 .. 6.1 radians");
        // the seqlock carries the new ops unchanged
        gpu::SceneData sd, out;
        gpu::SceneBuilder sb (sd);
        sb.line (1, 2, 3, 4, 5, 6u); sb.arc (1, 2, 3, 0.5f, 1.0f, 2.0f, 7u); sb.frame (1, 2, 3, 4, 1.0f, 8u);
        auto* b = new gpu::SharedBlock;
        gpu::initBlock (b);
        gpu::writeScene (b, sd);
        uint32_t seq = 0;
        check (gpu::readScene (b, out, seq) && std::memcmp (&out, &sd, sizeof sd) == 0, "the scene with the new ops goes through the shared block unchanged");
        delete b;
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
