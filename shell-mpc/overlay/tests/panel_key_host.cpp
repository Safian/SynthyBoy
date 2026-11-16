// SynthyBoy - MPC overlay tests: the key of the host's Q-Link panel (panel_key.h) over fake framebuffers: no panel, the panel sliding in at three places, in, sliding out, a host popup in the middle of the
// tab (no band), the tolerance, the grown edge, and a reference that is no longer the picture under the page (another host page: the band starts at the left end of the strip).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <map>
#include <string>

#include "../panel_key.h"

using namespace synthyboy_mpc::overlay;

static int g_fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++g_fails;
}

/** A framebuffer of the Key 61's shape (800 x 1280 portrait, the UI rotated by 90): a host page of one colour, and what the test paints over it, in UI pixels. */
class Fb final : public IFrameSource
{
public:
    uint32_t page = 0x202020u;
    bool readable = true;
    bool begin (int& w, int& h) override { w = 800; h = 1280; return readable; }
    bool pixel (int px, int py, uint32_t& rgb) noexcept override
    {
        auto it = over_.find (py * 800 + px);
        rgb = it == over_.end() ? page : it->second;
        return px >= 0 && py >= 0 && px < 800 && py < 1280;
    }
    void end() noexcept override {}
    void paint (int x, int y, uint32_t rgb) { int px, py; uiPointToPanel (90, x, y, 800, 1280, px, py); over_[py * 800 + px] = rgb; }
    void clear() { over_.clear(); }
    /** The panel: a black line of two pixels at `left`, the dark fill right of it (the colour of the host's tab, 1e1d1e against the 202020 of this fake page: two apart), four bright rings in it. */
    void panel (int left)
    {
        for (int y = 100; y < 760; ++y)
            for (int x = left; x < 1280; ++x)
                paint (x, y, x < left + 2 ? 0x000000u : 0x1e1d1eu);
        for (int k = 0; k < 4; ++k)
            for (int y = 160 + k * 150; y < 260 + k * 150; ++y)
                for (int x = std::max (left + 40, 1100); x < 1200; ++x)
                    paint (x, y, 0xd8143cu);
    }
private:
    std::map<int, uint32_t> over_;
};

int main()
{
    Fb fb;
    PanelKey key;
    check (! key.hasReference() && ! key.measure (fb, 90).read, "no reference: nothing is measured");
    check (key.capture (fb, 90) && key.hasReference(), "the reference is taken from a readable framebuffer");
    PanelKey::Result r = key.measure (fb, 90);
    check (r.read && r.edge == PanelKey::kNoKey && ! r.stale, "no panel: not cut");
    // the panel at the places of a slide, in, and out again (the same places backwards)
    const int places[7] = { 1270, 1200, 1100, 1024, 1100, 1200, 1270 };
    for (int left : places)
    {
        fb.clear();
        fb.panel (left);
        r = key.measure (fb, 90);
        check (r.read && ! r.stale && r.edge >= left - 4 && r.edge <= left, "the panel's line at x " + std::to_string (left) + ": cut at " + std::to_string (r.edge) + " (the line less the grown edge, within a column step)");
    }
    fb.clear();
    r = key.measure (fb, 90);
    check (r.edge == PanelKey::kNoKey, "the panel gone: not cut again");
    // a host popup in the middle: a rectangle over part of the strip, not a band over most of its height
    for (int y = 300; y < 460; ++y)
        for (int x = 900; x < 1100; ++x)
            fb.paint (x, y, 0xffffffu);
    r = key.measure (fb, 90);
    check (r.read && r.edge == PanelKey::kNoKey, "a popup in the middle of the tab: no band, not keyed");
    // the tolerance: the host's own picture differs by a few levels (a gradient), the panel by far more
    fb.clear();
    for (int y = 100; y < 760; ++y)
        for (int x = 1000; x < 1280; ++x)
            fb.paint (x, y, 0x232323u);   // three levels off the reference
    r = key.measure (fb, 90);
    check (r.edge == PanelKey::kNoKey, "three levels off in every channel: the tolerance, not keyed");
    fb.clear();
    for (int y = 100; y < 760; ++y)
        for (int x = 1000; x < 1280; ++x)
            fb.paint (x, y, 0x242424u);   // four levels off
    r = key.measure (fb, 90);
    check (r.edge >= 996 && r.edge <= 1000, "four levels off: keyed, at the left end of the band less the grown edge (" + std::to_string (r.edge) + ")");
    // another host page under us (a new project, another tab): every column differs, the band starts at the left end of the strip: the reference is stale
    fb.clear();
    fb.page = 0x606060u;
    r = key.measure (fb, 90);
    check (r.stale && r.edge == PanelKey::kX0, "the whole strip differs: the reference is stale (edge " + std::to_string (r.edge) + ")");
    check (key.capture (fb, 90) && key.measure (fb, 90).edge == PanelKey::kNoKey, "taken again from the new picture: not cut");
    // an unreadable framebuffer: not read, the caller keeps what it had
    fb.readable = false;
    check (! key.measure (fb, 90).read && ! key.capture (fb, 90), "unreadable: nothing read");
    key.forget();
    fb.readable = true;
    check (! key.hasReference(), "forget: no reference");
    // findLine (): the panel's black line without a reference
    {
        Fb f5;
        check (PanelKey::findLine (f5, 90) == PanelKey::kNoKey, "findLine: no panel, no line");
        for (int left : { 1270, 1150, 1024 })
        {
            f5.clear();
            f5.panel (left);
            const int e = PanelKey::findLine (f5, 90);
            check (e >= left - 4 && e <= left, "findLine: the panel at " + std::to_string (left) + ": cut at " + std::to_string (e));
        }
        f5.clear();
        for (int y = 300; y < 460; ++y)
            for (int x = 900; x < 1100; ++x)
                f5.paint (x, y, 0x000000u);
        check (PanelKey::findLine (f5, 90) == PanelKey::kNoKey, "findLine: a black rectangle in the middle is no line over the whole height");
        f5.readable = false;
        check (PanelKey::findLine (f5, 90) == PanelKey::kNoKey, "findLine: unreadable: none");
    }
    // consider (): the reference only from a strip that stood still for the stable time
    {
        Fb f2;
        PanelKey k2;
        check (! k2.consider (f2, 90, 0.0, 250.0) && ! k2.hasReference(), "consider: the first look is a candidate only");
        check (! k2.consider (f2, 90, 200.0, 250.0) && ! k2.hasReference(), "consider: 200 ms of a still strip are not enough for 250 ms");
        check (k2.consider (f2, 90, 260.0, 250.0) && k2.hasReference() && k2.measure (f2, 90).edge == PanelKey::kNoKey, "consider: 260 ms of a still strip: the reference is taken, the strip is not cut");
        PanelKey k3;
        double t = 0.0;
        bool taken = false;
        for (int left = 1280; left >= 1100 && ! taken; left -= 10)      // the panel slides in while the candidate is being watched
        {
            f2.clear();
            f2.panel (left);
            taken = k3.consider (f2, 90, t += 10.0, 250.0);
        }
        check (! taken && ! k3.hasReference(), "consider: a strip with a panel sliding over it (every look different) never becomes the reference");
        f2.clear();
        f2.panel (1150);
        for (int i = 0; i < 40 && ! taken; ++i)
            taken = k3.consider (f2, 90, t += 10.0, 250.0);
        check (taken, "consider: once the panel stands still the strip (with the panel in it) is taken: the controller's own checks (the markers, the band that stays) are what let it go");
        PanelKey k4;
        k4.consider (f2, 90, 0.0, 250.0);
        k4.dropCandidate();
        check (! k4.consider (f2, 90, 300.0, 250.0), "dropCandidate: the stable time starts again");
        k4.forget();
        f2.readable = false;
        check (! k4.consider (f2, 90, 600.0, 250.0), "consider: an unreadable framebuffer takes nothing");
    }
    std::printf (g_fails ? "panel_key_host: FAILED (%d)\n" : "panel_key_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
