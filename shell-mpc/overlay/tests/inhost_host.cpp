// SynthyBoy - MPC in-host drawing tests (everything that can be tested off the device): the ring (no tearing between the producer's writes and a reader's copy), the copy into a fake framebuffer (all four
// rotations, the clip at the pop-up line, the standby dim, translucent rows), the page analysis of a host frame (view, instance, the pop-up edge, the dim, the held page, the sample plan covers every read of the
// detector), the flip core (hook copy, the writer's delta rows, a flip in the way), the request reader, the slot patch with a fake slot (refuse on every unexpected layout, never left writable), the install /
// remove reference counting (16 instances in any order), the fall back policy and the seqlock boxes.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "../inhost/flip_core.h"
#include "../inhost/request_parse.h"
#include "../inhost/slot_hook.h"

using namespace synthyboy_mpc::overlay;
using namespace synthyboy_mpc::overlay::inhost;

static int g_fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++g_fails;
}

static uint32_t g_clock = 0;
static uint32_t fakeClock() noexcept { return g_clock += 10; }   // every read advances 10 us
static uint32_t slowClock() noexcept { return g_clock += 3000; }   // every read advances 3 ms

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the copy

static void testRowKinds()
{
    std::vector<uint32_t> p (4 * 3, 0u);
    for (int x = 0; x < 4; ++x) p[4 + x] = 0xff112233u;
    p[8] = 0xff000000u; p[9] = 0x80101010u; p[10] = 0xff000000u; p[11] = 0xff000000u;
    uint8_t k[3];
    classifyRows (p.data(), 4, 3, k);
    check (k[0] == kRowTransparent && k[1] == kRowOpaque && k[2] == kRowMixed, "classifyRows: transparent, opaque and mixed rows");
}

static void testDimAndBlend()
{
    check (dimPixel (0xff804020u, 256) == 0xff804020u, "dimPixel: 256 leaves the pixel");
    check (dimPixel (0xff804020u, 128) == 0xff402010u, "dimPixel: 128 halves every channel, alpha kept");
    check (dimPixel (0xffffffffu, 0) == 0xff000000u, "dimPixel: 0 is black");
    check (blendOver (0x80402010u, 0xff000000u) == 0xff402010u, "blendOver: over black gives the premultiplied colour");
    check (blendOver (0x80804020u, 0xff808080u) == 0xffc08060u, "blendOver: half alpha over grey");
    check (blendOver (0x01010101u, 0xffffffffu) == 0xffffffffu, "blendOver: saturates, never wraps");
}

/** The picture of a UI rectangle rendered the way the controller does it (a canvas rotated into the panel orientation); every canvas pixel carries its own coordinates. */
struct TestPicture
{
    Rect ui, panel;
    std::vector<uint32_t> pix;
    std::vector<uint8_t> kinds;
    int w = 0, h = 0, rotation = 90;
    PictureRef ref() const { PictureRef r; r.pix = pix.data(); r.kinds = kinds.data(); r.w = w; r.h = h; r.panel = panel; return r; }
};
static uint32_t colourOfUi (int x, int y) { return 0xff000000u | (static_cast<uint32_t> (x & 0x7ff) << 11) | static_cast<uint32_t> (y & 0x7ff); }
static TestPicture makePicture (int rotation, const Rect& ui, int panelW, int panelH)
{
    TestPicture t;
    t.rotation = rotation;
    t.ui = ui;
    t.panel = uiRectToPanel (rotation, ui, panelW, panelH);
    t.w = t.panel.w;
    t.h = t.panel.h;
    std::vector<uint32_t> canvas (static_cast<size_t> (ui.w) * ui.h);
    for (int y = 0; y < ui.h; ++y)
        for (int x = 0; x < ui.w; ++x)
            canvas[static_cast<size_t> (y) * ui.w + x] = colourOfUi (ui.x + x, ui.y + y);
    t.pix.assign (static_cast<size_t> (t.w) * t.h, 0u);
    rotateCanvasInto (rotation, canvas.data(), ui.w, ui.h, t.pix.data(), static_cast<size_t> (t.w) * 4u);
    t.kinds.assign (static_cast<size_t> (t.h), kRowOpaque);
    return t;
}

static void testBlitRotationsAndClip()
{
    for (int rot : { 0, 90, 180, 270 })
    {
        const bool sw = rotationSwapsSides (rot);
        const int W = sw ? 800 : 1280, H = sw ? 1280 : 800;
        const Rect ui { 0, 110, 1280, 628 };
        const TestPicture t = makePicture (rot, ui, W, H);
        const uint32_t sentinel = 0xff123456u;
        std::vector<uint32_t> fb (static_cast<size_t> (W) * H, sentinel);
        for (int edge : { 1280, 1022, 840, 0 })
        {
            std::fill (fb.begin(), fb.end(), sentinel);
            const Rect clip = clipToEdge (t.panel, rot, edge, W, H);
            blitRows (reinterpret_cast<uint8_t*> (fb.data()), static_cast<size_t> (W) * 4u, W, H, t.ref(), clip, 256, 0, t.h);
            bool ok = true;
            for (int y = 0; y < 800 && ok; y += 3)
                for (int x = 0; x < 1280 && ok; x += 3)
                {
                    int px, py;
                    uiPointToPanel (rot, x, y, W, H, px, py);
                    const uint32_t got = fb[static_cast<size_t> (py) * W + px];
                    const bool inRegion = y >= ui.y && y < ui.y + ui.h;
                    const uint32_t want = (inRegion && x < edge) ? colourOfUi (x, y) : sentinel;
                    ok = got == want;
                }
            check (ok, "blit rotation " + std::to_string (rot) + " clipped at x " + std::to_string (edge) + ": every UI pixel left of the edge is ours, every other pixel the host's");
        }
    }
}

static void testBlitDimAndKinds()
{
    const int W = 800, H = 1280;
    const TestPicture t = makePicture (90, { 0, 110, 1280, 628 }, W, H);
    std::vector<uint32_t> fb (static_cast<size_t> (W) * H, 0xff202020u);
    TestPicture m = t;
    for (int r = 0; r < m.h; ++r)
        m.kinds[static_cast<size_t> (r)] = r % 3 == 0 ? kRowTransparent : kRowOpaque;
    const size_t n = blitRows (reinterpret_cast<uint8_t*> (fb.data()), static_cast<size_t> (W) * 4u, W, H, m.ref(), m.panel, 128, 0, m.h);
    bool ok = true;
    for (int r = 0; r < m.h && ok; ++r)
    {
        const size_t at = static_cast<size_t> (m.panel.y + r) * W + m.panel.x + 5;
        const uint32_t want = m.kinds[static_cast<size_t> (r)] == kRowTransparent ? 0xff202020u : dimPixel (m.pix[static_cast<size_t> (r) * m.w + 5], 128);
        ok = fb[at] == want;
    }
    check (ok, "blit: transparent rows skipped, opaque rows darkened by the dim factor");
    check (n == static_cast<size_t> (m.h - (m.h + 2) / 3) * static_cast<size_t> (m.w), "blit: returns the pixels written");
    // translucent row: blended over what the buffer holds
    TestPicture one;
    one.w = 4; one.h = 1; one.panel = { 2, 3, 4, 1 };
    one.pix = { 0x00000000u, 0x80402010u, 0xff0000ffu, 0x80000000u };
    one.kinds = { kRowMixed };
    std::vector<uint32_t> small (16 * 8, 0xff000000u);
    blitRows (reinterpret_cast<uint8_t*> (small.data()), 16 * 4, 16, 8, one.ref(), { 0, 0, 16, 8 }, 256, 0, 1);
    check (small[3 * 16 + 2] == 0xff000000u && small[3 * 16 + 3] == 0xff402010u && small[3 * 16 + 4] == 0xff0000ffu && small[3 * 16 + 5] == 0xff000000u, "blit: a mixed row skips alpha 0, blends partial alpha, writes alpha 255");
    // out of range: never writes outside the buffer
    std::vector<uint32_t> tiny (4 * 4, 0xff111111u);
    TestPicture big = makePicture (90, { 0, 0, 100, 100 }, 4, 4);
    big.panel = { -3, -3, big.w, big.h };
    blitRows (reinterpret_cast<uint8_t*> (tiny.data()), 16, 4, 4, big.ref(), { -100, -100, 1000, 1000 }, 256, 0, big.h);
    check (blitRows (nullptr, 16, 4, 4, big.ref(), { 0, 0, 4, 4 }, 256, 0, 1) == 0, "blit: no buffer, nothing written");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the ring

static void testRing()
{
    FrameRing ring;
    check (ring.allocate (64 * 64, 64), "ring: allocated");
    check (ring.configure (64, 64), "ring: configured");
    FrameRing::Hold h;
    check (! ring.acquire (h), "ring: nothing to read before the first picture");
    std::atomic<bool> stop { false };
    std::atomic<int> torn { 0 }, reads { 0 }, skipped { 0 };
    std::thread producer ([&]
    {
        for (uint32_t v = 1; v < 3000 && ! stop.load(); ++v)
        {
            size_t stride = 0;
            uint32_t* p = ring.writeBuffer (stride);
            if (p == nullptr)
            {
                ++skipped;
                continue;
            }
            for (int i = 0; i < 64 * 64; ++i)
                p[i] = 0xff000000u | v;
            ring.publish (1, 0, 90, { 0, 0, 64, 64 });
        }
        stop.store (true);
    });
    std::vector<std::thread> readers;
    for (int t = 0; t < 2; ++t)
        readers.emplace_back ([&]
        {
            while (! stop.load())
            {
                FrameRing::Hold hh;
                if (! ring.acquire (hh))
                    continue;
                const uint32_t first = hh.pic.pix[0];
                bool same = true;
                for (int i = 1; i < 64 * 64 && same; ++i)
                    same = hh.pic.pix[i] == first;
                if (! same || (first & 0xffffffu) != hh.meta.gen)
                    ++torn;
                ++reads;
                ring.release (hh);
            }
        });
    producer.join();
    for (std::thread& r : readers)
        r.join();
    check (torn.load() == 0 && reads.load() > 0, "ring: " + std::to_string (reads.load()) + " reads by two readers while the producer wrote 3000 pictures: none torn, each one the picture of its generation");
    check (skipped.load() == 0, "ring: with four buffers the producer always finds one that is free (two readers)");
    ring.invalidate();
    check (! ring.acquire (h), "ring: invalidated, nothing to read");
}

static void testSeqlockBoxes()
{
    LookBox box;
    LookRecord r;
    check (! box.load (r), "LookBox: empty before the first store");
    std::atomic<bool> stop { false };
    std::atomic<int> bad { 0 }, reads { 0 };
    std::thread w ([&]
    {
        for (int i = 1; i < 200000; ++i)
        {
            LookRecord a;
            a.look.valid = true;
            a.look.view = i;
            a.look.edge = i;
            a.look.dim256 = static_cast<unsigned> (i);
            a.fb = static_cast<uint32_t> (i);
            a.flipSeq = static_cast<uint32_t> (i);
            a.drawnGen = static_cast<uint32_t> (i);
            box.store (a);
        }
        stop.store (true);
    });
    std::thread rd ([&]
    {
        while (! stop.load())
        {
            LookRecord t;
            if (box.load (t))
            {
                ++reads;
                if (t.look.view != t.look.edge || static_cast<uint32_t> (t.look.view) != t.fb || t.flipSeq != t.fb || t.drawnGen != t.fb || t.look.dim256 != t.fb)
                    ++bad;
            }
        }
    });
    w.join();
    rd.join();
    check (bad.load() == 0 && reads.load() > 0, "LookBox: a reader never sees fields of two different stores (" + std::to_string (reads.load()) + " reads)");

    SnapshotBox sb;
    Snapshot s, t;
    s.count = 100;
    for (int i = 0; i < 100; ++i) s.val[i] = static_cast<uint32_t> (i * 3);
    int rot = 0;
    check (! sb.load (t, rot), "SnapshotBox: empty before the first store");
    sb.store (s, 270);
    check (sb.load (t, rot) && rot == 270 && t.count == 100 && t.val[99] == 297u, "SnapshotBox: round trip");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the host frame and its analysis

struct FakeHostFrame
{
    int W = 800, H = 1280, rotation = 90;
    std::vector<uint32_t> px;
    FakeHostFrame() : px (static_cast<size_t> (W) * H, 0x1e1d1eu) {}
    uint8_t* bytes() { return reinterpret_cast<uint8_t*> (px.data()); }
    size_t pitch() const { return static_cast<size_t> (W) * 4u; }
    void put (int x, int y, uint32_t rgb)
    {
        int a, b;
        uiPointToPanel (rotation, x, y, W, H, a, b);
        if (a >= 0 && b >= 0 && a < W && b < H)
            px[static_cast<size_t> (b) * W + a] = rgb;
    }
    uint32_t at (int x, int y) const
    {
        int a, b;
        uiPointToPanel (rotation, x, y, W, H, a, b);
        return px[static_cast<size_t> (b) * W + a];
    }
    void rect (int x, int y, int w, int h, uint32_t rgb)
    {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
                put (x + i, y + j, rgb);
    }
    /** The skin: the two markers and the instance picture of a view, the static host picture under our page (a pattern, so a dim compares something). */
    void skin (int view, int slot)
    {
        const layout::View& v = layout::kViews[view];
        for (int j = 0; j < v.region.h; ++j)
            for (int i = 0; i < v.region.w; ++i)
                put (v.region.x + i, v.region.y + j, 0x1e1d1eu + static_cast<uint32_t> ((((i / 40) + (j / 40)) & 1) ? 0x0a0a0au : 0u));
        for (int m = 0; m < 2; ++m)
            rect (v.markers[m].rect.x, v.markers[m].rect.y, v.markers[m].rect.w, v.markers[m].rect.h, v.markers[m].rgb);
        rect (v.instance.x, v.instance.y, v.instance.w, v.instance.h, slot >= 0 ? layout::kInstanceSlotRgb[slot] : layout::kInstanceBlankRgb);
    }
    /** The host's Q-Link status pop-up: a black line of two columns at x, the dark fill to its right (the right marker is under it). */
    void popup (int x)
    {
        rect (x, 0, 2, 800, 0x000000u);
        rect (x + 2, 0, 1280 - x - 2, 800, 0x1e1d1eu);
    }
    /** The standby dim: every pixel x factor, then a small blur (box of 3). */
    void dim (unsigned f256, bool blur)
    {
        for (uint32_t& p : px)
            p = static_cast<uint32_t> (dimPixel (p | 0xff000000u, f256)) & 0xffffffu;
        if (! blur)
            return;
        std::vector<uint32_t> o = px;
        for (int y = 1; y < H - 1; ++y)
            for (int x = 1; x < W - 1; ++x)
            {
                uint32_t acc[3] = { 0, 0, 0 };
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const uint32_t p = px[static_cast<size_t> (y + dy) * W + x + dx];
                        acc[0] += p & 255u; acc[1] += (p >> 8) & 255u; acc[2] += (p >> 16) & 255u;
                    }
                o[static_cast<size_t> (y) * W + x] = (acc[0] / 9) | ((acc[1] / 9) << 8) | ((acc[2] / 9) << 16);
            }
        px = o;
    }
};

static int viewIndex (const char* name)
{
    for (int i = 0; i < layout::kNumViews; ++i)
        if (std::strcmp (layout::kViews[i].name, name) == 0)
            return i;
    return -1;
}

static void testAnalyzer()
{
    SamplePlan plan;
    plan.build (90, 800, 1280);
    check (plan.count() >= 40 && plan.count() <= SamplePlan::kMax, "plan: " + std::to_string (plan.count()) + " points (the markers and instance pictures of 24 views share their places, plus the pop-up signature)");
    const int lfo2 = viewIndex ("lfo2"), play2 = viewIndex ("play2"), mixer = viewIndex ("mixer");
    Snapshot snap;
    {
        Analyzer an;
        FakeHostFrame f;
        f.skin (lfo2, 3);
        Look l = an.analyze (plan, f.bytes(), f.pitch(), snap, -1, -1, false);
        check (l.valid && l.view == lfo2 && l.slot == 3 && l.edge == PanelKey::kNoKey && ! l.dimmed && l.dim256 == 256, "analyze: view lfo2, instance slot 3, no pop-up, no dim");
        check (an.lastMisses() == 0, "analyze: the detector reads nothing outside the sample plan (no miss)");
        FakeHostFrame b;
        l = an.analyze (plan, b.bytes(), b.pitch(), snap, -1, -1, false);
        check (! l.valid, "analyze: a frame without our markers is not our page");
    }
    {
        // the pop-up fully in: the right marker is under it
        Analyzer an;
        FakeHostFrame f;
        f.skin (mixer, 0);
        f.popup (1024);
        Look l = an.analyze (plan, f.bytes(), f.pitch(), snap, mixer, 0, false);
        check (l.valid && l.view == mixer && l.edge == 1022 && l.heldByPanel, "analyze: pop-up at 1024: the page is held, the cut is 1022 (the line's grown edge)");
        check (an.lastMisses() == 0, "analyze: the pop-up frame reads only plan points");
        for (int x : { 1100, 1050, 900, 868 })
        {
            FakeHostFrame g;
            g.skin (mixer, 0);
            g.popup (x);
            Look m = an.analyze (plan, g.bytes(), g.pitch(), snap, mixer, 0, false);
            check (m.valid && m.edge == Analyzer::scanEdge (g.bytes(), g.pitch(), 90, 800, 1280) && m.edge >= x - 2 - 2 && m.edge <= x, "analyze: pop-up sliding at " + std::to_string (x) + " found at " + std::to_string (m.edge));
        }
        // without the wanted picture the page cannot be told while its right marker is covered (the controller's rule: held only with a reference to the picture)
        Look n = an.analyze (plan, f.bytes(), f.pitch(), snap, -1, -1, false);
        check (n.valid == (n.view >= 0) && (! n.valid || n.view == mixer), "analyze: pop-up with no picture ready: no other view is invented");
    }
    {
        // the Play page: its right marker is under the pop-up by design
        Analyzer an;
        FakeHostFrame f;
        f.skin (play2, 5);
        f.popup (1024);
        const Look l = an.analyze (plan, f.bytes(), f.pitch(), snap, play2, 5, false);
        check (l.valid && l.view == play2 && l.edge == 1022, "analyze: Play page under the pop-up");
    }
    {
        // standby: backlight lowered, the host's picture x0.5 (and blurred): the page is held at the dim factor; without the backlight (a modal scrim) it is not
        Analyzer an;
        FakeHostFrame f;
        f.skin (lfo2, 2);
        Look l0 = an.analyze (plan, f.bytes(), f.pitch(), snap, lfo2, 2, false);
        check (l0.valid, "dim: the page is seen before the dim (the reference is taken)");
        FakeHostFrame d = f;
        d.dim (128, true);
        Look l = an.analyze (plan, d.bytes(), d.pitch(), snap, lfo2, 2, true);
        check (l.valid && l.dimmed && l.view == lfo2 && l.slot == 2 && l.dim256 >= 120 && l.dim256 <= 136, "dim: backlight lowered and the picture x0.5 blurred: page held, factor " + std::to_string (l.dim256) + " / 256");
        Look s = an.analyze (plan, d.bytes(), d.pitch(), snap, lfo2, 2, false);
        check (! s.valid, "dim: the same picture with the backlight not lowered (a modal scrim) is not our page");
        FakeHostFrame menu;
        for (size_t i = 0; i < menu.px.size(); ++i) menu.px[i] = static_cast<uint32_t> (0x300000u + (i % 97) * 0x010203u);
        Look mm = an.analyze (plan, menu.bytes(), menu.pitch(), snap, lfo2, 2, true);
        check (! mm.valid, "dim: a full screen menu with the backlight lowered is not taken for the dimmed page");
        FakeHostFrame w = f;
        Look back = an.analyze (plan, w.bytes(), w.pitch(), snap, lfo2, 2, true);
        check (back.valid && ! back.dimmed && back.dim256 == 256, "dim: the first undimmed frame after the wake is normal again");
    }
    {
        // every rotation: the plan follows it
        for (int rot : { 0, 90, 180, 270 })
        {
            const bool sw = rotationSwapsSides (rot);
            SamplePlan p;
            p.build (rot, sw ? 800 : 1280, sw ? 1280 : 800);
            FakeHostFrame f;
            f.W = sw ? 800 : 1280;
            f.H = sw ? 1280 : 800;
            f.rotation = rot;
            f.px.assign (static_cast<size_t> (f.W) * f.H, 0x1e1d1eu);
            f.skin (viewIndex ("env3"), 7);
            Analyzer an;
            Look l = an.analyze (p, f.bytes(), f.pitch(), snap, -1, -1, false);
            check (l.valid && l.view == viewIndex ("env3") && l.slot == 7 && an.lastMisses() == 0, "analyze: rotation " + std::to_string (rot));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the flip core

struct CoreRig
{
    FbTable fbs;
    FrameRing ring;
    FlipCore core { fbs, ring, &fakeClock };
    SamplePlan plan;
    FakeHostFrame bufA, bufB;
    int view = 0;
    Rect region, panelRect;
    CoreRig()
    {
        plan.build (90, 800, 1280);
        view = viewIndex ("lfo2");
        const layout::View& v = layout::kViews[view];
        region = { v.region.x, v.region.y, v.region.w, v.region.h };
        panelRect = uiRectToPanel (90, region, 800, 1280);
        ring.allocate (800 * 1280, 1280);
        ring.configure (panelRect.w, panelRect.h);
        FbView a { bufA.bytes(), bufA.pitch(), 800, 1280 }, b { bufB.bytes(), bufB.pitch(), 800, 1280 };
        fbs.add (49, a);
        fbs.add (50, b);
        bufA.skin (view, 0);
        bufB.skin (view, 0);
    }
    /** The overlay thread: a picture whose pixel value says which "frame" it is. */
    void publish (uint32_t value, int rowChanged = -1, uint32_t rowValue = 0)
    {
        size_t stride = 0;
        uint32_t* p = ring.writeBuffer (stride);
        for (int i = 0; i < panelRect.w * panelRect.h; ++i)
            p[i] = 0xff000000u | value;
        if (rowChanged >= 0)
            for (int x = 0; x < panelRect.w; ++x)
                p[static_cast<size_t> (rowChanged) * panelRect.w + x] = 0xff000000u | rowValue;
        ring.publish (view, 0, 90, panelRect);
    }
    uint32_t panelPixelOfUi (FakeHostFrame& f, int x, int y) const { return f.at (x, y); }
};

static void testFlipCore()
{
    CoreRig r;
    check (! r.core.onFlip (0, r.plan).hadFb, "flip: a request without a framebuffer is left alone");
    // no picture yet: the page is seen, nothing is copied (counted as missed), the host's own frame goes out untouched
    auto res = r.core.onFlip (49, r.plan);
    r.core.afterCommit (0);
    check (res.hadFb && res.identified && ! res.drew && r.core.counters.missed.load() == 1, "flip: our page on screen, no picture ready: nothing copied, counted as missed");
    check (r.core.counters.bgFills.load() == 1 && r.bufA.at (640, 300) == (0xff000000u | layout::kBackgroundRgb) && r.bufA.at (36, 137) == (0xff000000u | layout::kBackgroundRgb), "flip: no picture yet: the page's dark background over its region (markers included), never the host's skin");
    r.bufA.skin (r.view, 0);   // (the host repaints the whole buffer at its next flip)
    r.publish (0x112233u);
    res = r.core.onFlip (49, r.plan);
    check (res.drew, "flip: page on screen and a picture ready: copied");
    bool ok = true;
    for (int x = 0; x < 1280 && ok; x += 7)
        for (int y = r.region.y; y < r.region.y + r.region.h && ok; y += 5)
            ok = r.bufA.at (x, y) == 0xff112233u;
    check (ok, "flip: the whole region of the view holds our picture");
    check (r.bufA.at (5, 20) == 0x1e1d1eu, "flip: outside the region the host's pixels stay");
    r.core.afterCommit (0);
    LookRecord L;
    check (r.core.look.load (L) && L.look.valid && L.fb == 49 && L.flipSeq == 2 && L.drawnGen == r.ring.generation(), "flip: after the commit the look says: frame 49, flip 2, our picture generation drawn");
    check (! r.core.inCommit.load(), "flip: not in a commit any more");
    // a flip with the pop-up: the picture stops at the line
    r.bufB.skin (r.view, 0);
    r.bufB.popup (1024);
    res = r.core.onFlip (50, r.plan);
    r.core.afterCommit (0);
    check (res.drew && r.bufB.at (1000, 300) == 0xff112233u && r.bufB.at (1021, 300) == 0xff112233u && r.bufB.at (1023, 300) != 0xff112233u && r.bufB.at (1024, 300) == 0x000000u && r.bufB.at (1030, 300) == 0x1e1d1eu, "flip: pop-up in the frame: our picture ends left of its black line, the pop-up is the host's");
    check (r.core.edgeNow.load() == 1022 && r.core.counters.clipped.load() == 1, "flip: the edge is published for the overlay's key, the clip counted");
    // a picture of another view is not copied onto this page
    size_t stride = 0;
    uint32_t* p = r.ring.writeBuffer (stride);
    for (int i = 0; i < r.panelRect.w * r.panelRect.h; ++i) p[i] = 0xff00ff00u;
    r.ring.publish (viewIndex ("lfo3"), 0, 90, r.panelRect);
    r.bufA.skin (r.view, 0);
    res = r.core.onFlip (49, r.plan);
    r.core.afterCommit (0);
    check (! res.drew && r.bufA.at (640, 300) != 0xff00ff00u, "flip: a picture made for another view is never copied onto this page");
    // another instance's picture
    p = r.ring.writeBuffer (stride);
    for (int i = 0; i < r.panelRect.w * r.panelRect.h; ++i) p[i] = 0xff00ff00u;
    r.ring.publish (r.view, 4, 90, r.panelRect);
    r.bufA.skin (r.view, 0);
    res = r.core.onFlip (49, r.plan);
    r.core.afterCommit (0);
    check (! res.drew, "flip: a picture of instance 4 is not copied onto the page of instance 0");
    // unknown buffer
    res = r.core.onFlip (77, r.plan);
    r.core.afterCommit (0);
    check (res.hadFb && ! res.identified && r.core.counters.unidentified.load() == 1, "flip: a framebuffer we did not map is not touched and is counted");
    // a failed commit: the front does not change
    const uint32_t seq = r.core.flipSeq.load();
    r.publish (0x445566u);
    r.bufA.skin (r.view, 0);
    r.core.onFlip (49, r.plan);
    r.core.afterCommit (-22);
    check (r.core.flipSeq.load() == seq && ! r.core.inCommit.load(), "flip: a commit that failed does not count as a flip");
}

static void testBudget()
{
    CoreRig r;
    FlipCore slow (r.fbs, r.ring, &slowClock);
    r.publish (0x112233u);
    const auto res = slow.onFlip (49, r.plan);
    slow.afterCommit (0);
    LookRecord L;
    slow.look.load (L);
    check (res.drew && L.drawnGen != 0 && res.hookUs > 12000, "budget: a slow hook still finishes the copy (a frame that goes out is complete)");
    FallbackPolicy p;
    check (p.onFlip (res.hookUs, true) == FallbackPolicy::Verdict::Slow, "budget: the far guard (12 ms) is a fault: the plane takes over");
    bool same = true;
    for (int k = 0; k < 6; ++k)
    {
        std::vector<uint8_t> a (4096 + 36), b (4096 + 36);
        for (size_t i = 0; i < a.size(); ++i) a[i] = static_cast<uint8_t> (i * 7);
        copyKind (static_cast<CopyKind> (k), b.data() + 8, a.data() + 4, 4096 + 24);
        same = same && std::memcmp (b.data() + 8, a.data() + 4, 4096 + 24) == 0;
    }
    check (same, "copy variants: every one copies the same bytes");
}

static void testFrontWriter()
{
    CoreRig r;
    r.publish (0x112233u);
    // nothing has flipped: the writer looks at the front buffer itself, then draws
    check (r.core.analyseFront (49, r.plan), "front: the writer's own look at the buffer on screen");
    bool retry = false;
    check (r.core.frontPass (r.plan, retry) && r.bufA.at (640, 300) == 0xff112233u, "front: the first pass writes the whole picture into the buffer on screen");
    check (r.core.counters.frontFull.load() == 1, "front: that was a full write");
    // a change of three rows: only they are written
    const uint32_t before = r.core.counters.frontRows.load();
    r.publish (0x112233u, 100, 0x778899u);
    check (r.core.frontPass (r.plan, retry), "front: second pass");
    check (r.core.counters.frontRows.load() - before == 1 && r.core.counters.frontFull.load() == 1, "front: one changed panel row: one row written, no full write");
    // the changed row lies where the picture says (panel row panelRect.y + 100)
    check (r.bufA.px[static_cast<size_t> (r.panelRect.y + 100) * 800 + r.panelRect.x + 3] == 0xff778899u, "front: the row landed at its place in the buffer");
    // no change: nothing written
    const uint32_t w0 = r.core.counters.frontWrites.load();
    r.publish (0x112233u, 100, 0x778899u);
    r.core.frontPass (r.plan, retry);
    check (r.core.counters.frontWrites.load() == w0, "front: an identical picture writes nothing");
    // the host flips (buffer 50, the hook copies the latest picture): the writer finds the picture already there
    r.bufB.skin (r.view, 0);
    auto res = r.core.onFlip (50, r.plan);
    r.core.afterCommit (0);
    r.core.frontPass (r.plan, retry);
    check (res.drew && r.core.counters.frontWrites.load() == w0, "front: after a host flip the hook's copy is the latest picture, the writer adds nothing");
    // a new picture after the flip: delta against what the hook copied
    const uint32_t rows0 = r.core.counters.frontRows.load();
    r.publish (0x112233u, 100, 0x778899u);   // identical to the one the hook copied
    r.core.frontPass (r.plan, retry);
    r.publish (0x112233u, 200, 0xabcdefu);
    r.core.frontPass (r.plan, retry);
    check (r.core.counters.frontRows.load() - rows0 <= 101 && r.core.counters.frontFull.load() == 1 && r.bufB.px[static_cast<size_t> (r.panelRect.y + 200) * 800 + r.panelRect.x + 3] == 0xffabcdefu && r.bufB.px[static_cast<size_t> (r.panelRect.y + 100) * 800 + r.panelRect.x + 3] == 0xff112233u, "front: after a host flip the next change is written as the span of changed rows into the new front buffer (no full write)");
    // a flip in the way: the pass reports it and starts again
    r.core.inCommit.store (true);
    r.publish (0x010203u);
    const bool done = r.core.frontPass (r.plan, retry);
    check (! done && retry, "front: while the host is committing nothing is written (the pass is asked again)");
    r.core.inCommit.store (false);
    check (r.core.frontPass (r.plan, retry) && r.bufB.at (640, 300) == 0xff010203u, "front: after the commit the pass writes the new picture");
    // the overlay hides the picture while the host is idle: the page background replaces it
    r.core.forgetFront();
    r.publish (0x010203u);
    r.core.frontPass (r.plan, retry);
    r.ring.invalidate();
    r.core.eraseFront (r.plan);
    check (r.core.counters.erased.load() == 1 && r.bufB.at (640, 300) == (0xff000000u | layout::kBackgroundRgb), "front: picture hidden while the host is idle: the page's background is put back");
    // standby: the look says dimmed: the writer darkens
    CoreRig d;
    d.publish (0x808080u);
    d.bufA.dim (128, false);
    d.core.backlightDim.store (true);
    {
        // the analyzer needs one normal frame for its reference
        FakeHostFrame n;
        n.skin (d.view, 0);
        FbView nb { n.bytes(), n.pitch(), 800, 1280 };
        d.fbs.drop (49);
        d.fbs.add (48, nb);
        d.core.analyseFront (48, d.plan);
        d.fbs.drop (48);
        FbView ab { d.bufA.bytes(), d.bufA.pitch(), 800, 1280 };
        d.fbs.add (49, ab);
    }
    check (d.core.analyseFront (49, d.plan), "front: look at a dimmed frame");
    d.core.frontPass (d.plan, retry);
    const uint32_t got = d.bufA.at (640, 300);
    check ((got & 255u) >= 60u && (got & 255u) <= 68u, "front: the standby dim: our picture is darkened by the host's factor (value " + std::to_string (got & 255u) + " of 0x80 x 0.5)");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the request reader

struct FakeReq24 { uint32_t cursor, size; const void* items; };
struct Item24 { uint32_t obj, prop; uint64_t value; uint32_t* cur; };
struct Item16 { uint32_t obj, prop; uint64_t value; };

static void testRequestReader()
{
    Item24 it[3] = { { 33, 7, 49, nullptr }, { 33, 8, 0, nullptr }, { 35, 7, 45, nullptr } };
    FakeReq24 q { 3, 8, it };
    uint64_t v = 0;
    check (findItem (&q, sizeof (Item24), 33, 7, v) && v == 49, "request: the framebuffer item of plane 33 is found (24 byte items)");
    check (! findItem (&q, sizeof (Item24), 34, 7, v), "request: no item for another plane");
    check (findItem (&q, sizeof (Item24), 35, 7, v) && v == 45, "request: the cursor plane's item");
    Item16 jt[2] = { { 33, 7, 50 }, { 35, 7, 45 } };
    FakeReq24 q2 { 2, 4, jt };
    check (findItem (&q2, 16, 33, 7, v) && v == 50, "request: 16 byte items");
    const uint32_t items[3][2] = { { 0x1234u, 0x55u }, { 0x1235u, 0x56u }, { 0x1234u, 0x57u } };
    const uint64_t values[3] = { 0x1122334455667788ull, 0x99ull, 0xdeadbeefull };
    Item24 t24[3] = { { 0x1234, 0x55, values[0], nullptr }, { 0x1235, 0x56, values[1], nullptr }, { 0x1234, 0x57, values[2], nullptr } };
    FakeReq24 r24 { 3, 4, t24 };
    check (detectStride (&r24, items, values, 3) == sizeof (Item24), "self test: a libdrm with 24 byte items is recognised");
    Item16 t16[3] = { { 0x1234, 0x55, values[0] }, { 0x1235, 0x56, values[1] }, { 0x1234, 0x57, values[2] } };
    FakeReq24 r16 { 3, 4, t16 };
    check (detectStride (&r16, items, values, 3) == 16, "self test: a libdrm with 16 byte items is recognised");
    Item24 wrong[3] = { { 0x1234, 0x55, values[0], nullptr }, { 0x9999, 0x56, values[1], nullptr }, { 0x1234, 0x57, values[2], nullptr } };
    FakeReq24 rw { 3, 4, wrong };
    check (detectStride (&rw, items, values, 3) == 0, "self test: a request that does not read back is refused");
    Item24 pl[4] = { { 33, 5, 37, nullptr }, { 33, 7, 50, nullptr }, { 33, 9, 800u << 16, nullptr }, { 35, 7, 45, nullptr } };
    FakeReq24 qp { 4, 8, pl };
    uint32_t learned = 0;
    check (findPropByValue (&qp, 24, 33, 48, 50, learned) && learned == 7, "request: the framebuffer property is learned from the request (value next to the shown id)");
    check (! findPropByValue (&qp, 24, 33, 30, 50, learned), "request: two candidate properties are ambiguous: nothing learned");
    FakeReq24 bad { 9000, 4, t24 };
    check (! findItem (&bad, 24, 33, 7, v) && ! findItem (nullptr, 24, 33, 7, v), "request: an implausible header is not read");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the slot patch

struct FakeProtect final : IProtect
{
    Protection prot;
    bool failWritable = false, failRestore = false, restoreOnlyOnce = false;
    int writableCalls = 0, restoreCalls = 0;
    bool writableNow = false;
    bool query (uintptr_t, Protection& out) override { out = prot; return prot.found; }
    bool makeWritable (uintptr_t, size_t, std::string& e) override { ++writableCalls; if (failWritable) { e = "EACCES"; return false; } writableNow = true; return true; }
    bool restore (uintptr_t, size_t, const Protection&, std::string& e) override
    {
        ++restoreCalls;
        if (failRestore) { e = "EPERM"; return false; }
        writableNow = false;
        return true;
    }
};
static Protection ro() { Protection p; p.found = true; p.r = true; return p; }

static void testSlotPatch()
{
    void* orig = reinterpret_cast<void*> (0x1000);
    void* mine = reinterpret_cast<void*> (0x2000);
    alignas (16) void* slotArr[2] = { orig, nullptr };
    std::string why;
    {
        FakeProtect m;
        m.prot = ro();
        SlotPatch p;
        check (p.install (&slotArr[0], orig, mine, m, 4096, why) && slotArr[0] == mine && ! m.writableNow, "patch: replaced, the page is read-only again");
        check (m.writableCalls == 1 && m.restoreCalls == 1, "patch: the page was opened once and closed once");
        check (! p.install (&slotArr[0], orig, mine, m, 4096, why), "patch: a second install is refused");
        check (p.remove (m, why) && slotArr[0] == orig && ! m.writableNow, "patch: removed, original back, page read-only");
        check (p.remove (m, why), "patch: removing twice is harmless");
    }
    {
        FakeProtect m;
        m.prot = ro();
        slotArr[0] = reinterpret_cast<void*> (0x3000);
        SlotPatch p;
        check (! p.install (&slotArr[0], orig, mine, m, 4096, why) && slotArr[0] == reinterpret_cast<void*> (0x3000) && m.writableCalls == 0, "patch: the slot holds another function: refused, nothing opened, nothing written (" + why + ")");
        slotArr[0] = orig;
    }
    {
        FakeProtect m;
        m.prot = ro();
        m.prot.w = true;
        SlotPatch p;
        check (! p.install (&slotArr[0], orig, mine, m, 4096, why) && slotArr[0] == orig && why.find ("rw-p") != std::string::npos, "patch: the page is already writable (not RELRO): refused (" + why + ")");
        m.prot = ro();
        m.prot.x = true;
        check (! p.install (&slotArr[0], orig, mine, m, 4096, why), "patch: an executable page: refused");
        m.prot = Protection();
        check (! p.install (&slotArr[0], orig, mine, m, 4096, why), "patch: a page that is not in the map: refused");
    }
    {
        FakeProtect m;
        m.prot = ro();
        m.failWritable = true;
        SlotPatch p;
        check (! p.install (&slotArr[0], orig, mine, m, 4096, why) && slotArr[0] == orig, "patch: mprotect refused: nothing changed");
    }
    {
        FakeProtect m;
        m.prot = ro();
        m.failRestore = true;
        SlotPatch p;
        check (! p.install (&slotArr[0], orig, mine, m, 4096, why) && slotArr[0] == orig, "patch: the protection cannot be put back: the change is undone");
    }
    {
        FakeProtect m;
        m.prot = ro();
        SlotPatch p;
        p.install (&slotArr[0], orig, mine, m, 4096, why);
        slotArr[0] = reinterpret_cast<void*> (0x4000);   // another module hooked on top
        check (! p.remove (m, why) && slotArr[0] == reinterpret_cast<void*> (0x4000) && m.writableCalls == 1, "patch: another module hooked on top: our removal leaves its slot alone and says so");
        slotArr[0] = orig;
    }
    {
        FakeProtect m;
        m.prot = ro();
        SlotPatch p;
        void** odd = reinterpret_cast<void**> (reinterpret_cast<uintptr_t> (&slotArr[0]) + 1);
        check (! p.install (odd, orig, mine, m, 4096, why), "patch: an unaligned slot: refused");
    }
}

struct FakeInstaller
{
    int installs = 0, removes = 0;
    bool succeed = true;
    bool install() { ++installs; return succeed; }
    void remove() { ++removes; }
};

static void testRefCount()
{
    FakeInstaller f;
    HookRefs<FakeInstaller> refs (f);
    for (int i = 0; i < 16; ++i)
        refs.acquire();
    check (f.installs == 1 && refs.installed() && refs.count() == 16, "refs: 16 instances: installed once");
    for (int i = 0; i < 15; ++i)
        refs.release();
    check (f.removes == 0 && refs.installed(), "refs: 15 of 16 closed: still in");
    refs.release();
    check (f.removes == 1 && ! refs.installed() && refs.count() == 0, "refs: the last one removes it");
    refs.release();
    check (f.removes == 1, "refs: a release too many is harmless");
    refs.acquire();
    refs.acquire();
    refs.release();
    refs.acquire();
    refs.release();
    refs.release();
    check (f.installs == 2 && f.removes == 2, "refs: create, create, destroy, create, destroy, destroy: one install, one remove");
    FakeInstaller g;
    g.succeed = false;
    HookRefs<FakeInstaller> r2 (g);
    check (! r2.acquire() && ! r2.acquire() && g.installs == 1, "refs: a refused install is not tried again for the next instance");
    r2.release();
    r2.release();
    check (g.removes == 0, "refs: nothing to remove after a refused install");
    r2.acquire();
    check (g.installs == 2, "refs: tried again after the count was 0");
}

static void testPolicy()
{
    FallbackPolicy p;
    for (int i = 0; i < 1000; ++i)
        if (p.onFlip (3000, true) != FallbackPolicy::Verdict::Ok)
        {
            check (false, "policy: 3 ms flips are fine");
            break;
        }
    check (true, "policy: 1000 flips of 3 ms: ok");
    FallbackPolicy s;
    int verdictAt = 0;
    for (int i = 1; i <= 128 && verdictAt == 0; ++i)
        if (s.onFlip (i % 10 == 0 ? 12000 : 2000, true) == FallbackPolicy::Verdict::Slow)
            verdictAt = i;
    check (verdictAt == 80, "policy: a slow hook every 10th flip: the verdict at the 8th slow one (flip " + std::to_string (verdictAt) + ")");
    FallbackPolicy t;
    int at = 0;
    for (int i = 1; i <= 300 && at == 0; ++i)
        if (t.onFlip (2000, i % 5 != 0) == FallbackPolicy::Verdict::Unidentified)
            at = i;
    check (at == 0, "policy: unidentified flips with identified ones in between do not add up");
    FallbackPolicy u;
    for (int i = 1; i <= 39; ++i)
        u.onFlip (2000, false);
    check (u.onFlip (2000, false) == FallbackPolicy::Verdict::Unidentified, "policy: 40 unidentified flips in a row: fall back");
    FallbackPolicy w;
    for (int i = 1; i <= 39; ++i)
        w.onUnparsed();
    check (w.onUnparsed() == FallbackPolicy::Verdict::Unparsed, "policy: 40 commits in a row whose request cannot be read: fall back");
    FallbackPolicy x;
    for (int i = 1; i <= 39; ++i)
        x.onUnparsed();
    x.onFlip (2000, true);
    check (x.onUnparsed() == FallbackPolicy::Verdict::Ok, "policy: a flip that was read resets the run");
    check (std::string (FallbackPolicy::name (FallbackPolicy::Verdict::Slow)).size() > 5, "policy: the verdicts have names for the status file");
}

int main()
{
    testRowKinds();
    testDimAndBlend();
    testBlitRotationsAndClip();
    testBlitDimAndKinds();
    testRing();
    testSeqlockBoxes();
    testAnalyzer();
    testFlipCore();
    testBudget();
    testFrontWriter();
    testRequestReader();
    testSlotPatch();
    testRefCount();
    testPolicy();
    std::printf ("inhost_host: %s (%d failures)\n", g_fails == 0 ? "OK" : "FAILED", g_fails);
    return g_fails == 0 ? 0 : 1;
}
