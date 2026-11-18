// SynthyBoy - MPC overlay tests: the page detector with REAL pictures of the Key 61 (fixtures/qlink_panel_fixtures.bin, cut by fixtures/make_qlink_panel_fixtures.py out of screenshots of the primary
// plane): the Play tab as it is, with the full screen open (instance picture blank), the Macros tab, the project selector, and the Play tab with the host's Q-Link panel over the right edge while a
// hardware knob is turned (it covers the right marker). The page must be recognised under the panel, and only there; nothing else may look like the panel.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../overlay_layout.h"
#include "../page_detector.h"

using namespace synthyboy_mpc::overlay;

#ifndef QLINK_FIXTURE
#define QLINK_FIXTURE "shell-mpc/overlay/tests/fixtures/qlink_panel_fixtures.bin"
#endif

static int g_fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++g_fails;
}

struct Crop { int x = 0, y = 0, w = 0, h = 0; std::vector<uint32_t> rgb; };
struct Picture { Crop crop[2]; };

static std::map<std::string, Picture> load()
{
    std::map<std::string, Picture> out;
    FILE* f = std::fopen (QLINK_FIXTURE, "rb");
    if (! f) return out;
    for (;;)
    {
        unsigned char n = 0;
        if (std::fread (&n, 1, 1, f) != 1) break;
        std::string name (n, ' ');
        if (std::fread (&name[0], 1, n, f) != n) break;
        Picture p;
        for (Crop& c : p.crop)
        {
            uint16_t h4[4];
            uint32_t bytes = 0;
            if (std::fread (h4, 2, 4, f) != 4 || std::fread (&bytes, 4, 1, f) != 1) return out;
            c.x = h4[0]; c.y = h4[1]; c.w = h4[2]; c.h = h4[3];
            std::vector<unsigned char> d (bytes);
            if (std::fread (d.data(), 1, bytes, f) != bytes) return out;
            for (size_t i = 0; i + 5 <= d.size(); i += 5)
            {
                const unsigned cnt = d[i] | (d[i + 1] << 8);
                const uint32_t v = (static_cast<uint32_t> (d[i + 2]) << 16) | (static_cast<uint32_t> (d[i + 3]) << 8) | d[i + 4];
                c.rgb.insert (c.rgb.end(), cnt, v);
            }
        }
        out[name] = std::move (p);
    }
    std::fclose (f);
    return out;
}

/** The primary plane of the Key 61: portrait 800 x 1280, the upright picture rotated by 90 degrees; only the cut-out strips exist, the rest of the screen reads as the dark fill. */
class PictureSource final : public IFrameSource
{
public:
    explicit PictureSource (const Picture& p) : p_ (p) {}
    bool begin (int& w, int& h) override { w = 800; h = 1280; return true; }
    bool pixel (int px, int py, uint32_t& rgb) noexcept override
    {
        int x, y;
        panelPointToUi (90, px, py, 800, 1280, x, y);
        rgb = 0x1e1d1eu;
        for (const Crop& c : p_.crop)
            if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h)
                rgb = c.rgb[static_cast<size_t> ((y - c.y) * c.w + (x - c.x))];
        return true;
    }
    void end() noexcept override {}

private:
    const Picture& p_;
};

int main()
{
    const auto pics = load();
    check (pics.size() == 5, "the fixtures load (" + std::to_string (pics.size()) + " pictures)");
    MarkerSpec markers[layout::kNumViews * 2];
    Rect instances[layout::kNumViews];
    int play1 = -1;
    for (int i = 0; i < layout::kNumViews; ++i)
    {
        const layout::View& v = layout::kViews[i];
        for (int m = 0; m < 2; ++m)
            markers[i * 2 + m] = { { v.markers[m].rect.x, v.markers[m].rect.y, v.markers[m].rect.w, v.markers[m].rect.h }, v.markers[m].rgb };
        instances[i] = { v.instance.x, v.instance.y, v.instance.w, v.instance.h };
        if (std::string (v.name) == "play1") play1 = i;
    }
    auto detect = [&] (const char* name)
    {
        PictureSource src (pics.at (name));
        PageDetector det (src, markers, layout::kNumViews, instances);
        return det.check (90);
    };
    {
        const Detection d = detect ("panel");
        check (d.view == play1 && d.qlinkPanel && d.slot == 0, "the Play tab under the host's Q-Link panel: the page is recognised (view play1, slot 0), the panel is reported");
    }
    {
        const Detection d = detect ("play");
        check (d.view == play1 && ! d.qlinkPanel && d.slot == 0, "the Play tab as it is: play1, slot 0, no panel");
    }
    {
        const Detection d = detect ("full");
        check (d.view == play1 && ! d.qlinkPanel, "the Play tab with the plugin's full screen open (the host widgets hidden, the instance picture blank): play1, no panel");
    }
    {
        const Detection d = detect ("macros");
        check (d.view != play1 && ! d.qlinkPanel, "the Macros tab: not the Play tab, no panel");
    }
    {
        const Detection d = detect ("selectproject");
        check (d.view < 0 && ! d.qlinkPanel, "the project selector (black screen): no view, no panel");
    }
    {
        // the panel without the page's left marker (another tab with the panel over it) is no Play page; a left marker alone with a screen without the panel's look is no page either
        Picture p = pics.at ("panel");
        p.crop[0].rgb.assign (p.crop[0].rgb.size(), 0x1e1d1eu);
        PictureSource src (p);
        PageDetector det (src, markers, layout::kNumViews, instances);
        const Detection d = det.check (90);
        check (d.view < 0, "the panel without the page's left marker: no view");
        Picture q = pics.at ("play");
        PictureSource src2 (q);
        q.crop[1].rgb[0] = 0x123456u;
        PageDetector det2 (src2, markers, layout::kNumViews, instances);
        check (! det2.check (90).qlinkPanel, "a Play tab whose right marker is covered by something that is not the panel: not the panel");
    }
    {
        // the panel sliding in (or out): its fill already covers the right marker, its black line is not at x 1024 yet. No page is recognised, but the left marker and the instance picture are there:
        // partialView names the view, so the controller can keep the page for the length of the slide
        Picture p = pics.at ("play");
        p.crop[1].rgb.assign (p.crop[1].rgb.size(), 0x1e1d1eu);
        PictureSource src (p);
        PageDetector det (src, markers, layout::kNumViews, instances);
        const Detection d = det.check (90);
        check (d.view < 0 && ! d.qlinkPanel && d.partialView == play1, "the right marker covered by the fill, no panel look yet (the slide): no view, partialView = the Play tab");
        PictureSource srcOk (pics.at ("play"));
        PageDetector detOk (srcOk, markers, layout::kNumViews, instances);
        check (detOk.check (90).view == play1, "a Play tab as it is has its view");
        Picture q = pics.at ("macros");
        PictureSource srcM (q);
        PageDetector detM (srcM, markers, layout::kNumViews, instances);
        check (detM.check (90).partialView < 0, "another tab: no partial view");
    }
    std::printf (g_fails ? "page_detector_host: FAILED (%d)\n" : "page_detector_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
