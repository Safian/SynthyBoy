// SynthyBoy - MPC overlay tests: the rotation mapping (round trip, corners on an 800 x 1280 panel, the rotate-copy against the point mapping) and the CPU renderer on a memory
// buffer (the line follows the dB values, nothing outside the canvas, premultiplied alpha, the markers are covered).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <vector>

#include "../geometry.h"
#include "../overlay_layout.h"
#include "../renderer.h"

using namespace synthyboy_mpc::overlay;

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}

static void geometryTests()
{
    const int PW = 800, PH = 1280;
    for (int rot : { 90, 270 })
    {
        bool round = true;
        for (int x = 0; x < 1280 && round; x += 7)
            for (int y = 0; y < 800; y += 5)
            {
                int px, py, x2, y2;
                uiPointToPanel (rot, x, y, PW, PH, px, py);
                panelPointToUi (rot, px, py, PW, PH, x2, y2);
                if (px < 0 || py < 0 || px >= PW || py >= PH || x2 != x || y2 != y) { round = false; break; }
            }
        char name[96];
        std::snprintf (name, sizeof name, "rotation %d: UI -> panel -> UI round trip, every point inside the panel", rot);
        check (round, name);
    }
    // the four corners of the UI on the panel
    int px, py;
    uiPointToPanel (90, 0, 0, PW, PH, px, py);          check (px == 0 && py == 1279, "rot 90: UI top-left (0,0) -> panel (0,1279)");
    uiPointToPanel (90, 1279, 0, PW, PH, px, py);       check (px == 0 && py == 0, "rot 90: UI top-right (1279,0) -> panel (0,0)");
    uiPointToPanel (90, 0, 799, PW, PH, px, py);        check (px == 799 && py == 1279, "rot 90: UI bottom-left (0,799) -> panel (799,1279)");
    uiPointToPanel (90, 1279, 799, PW, PH, px, py);     check (px == 799 && py == 0, "rot 90: UI bottom-right (1279,799) -> panel (799,0)");
    uiPointToPanel (270, 0, 0, PW, PH, px, py);         check (px == 799 && py == 0, "rot 270: UI top-left -> panel (799,0)");
    uiPointToPanel (270, 1279, 0, PW, PH, px, py);      check (px == 799 && py == 1279, "rot 270: UI top-right -> panel (799,1279)");
    uiPointToPanel (270, 0, 799, PW, PH, px, py);       check (px == 0 && py == 0, "rot 270: UI bottom-left -> panel (0,0)");
    uiPointToPanel (270, 1279, 799, PW, PH, px, py);    check (px == 0 && py == 1279, "rot 270: UI bottom-right -> panel (0,1279)");
    // rectangles: the curve area of the skin
    const Rect ui { layout::kCurve.x, layout::kCurve.y, layout::kCurve.w, layout::kCurve.h };
    const Rect r90 = uiRectToPanel (90, ui, PW, PH), r270 = uiRectToPanel (270, ui, PW, PH);
    check (r90 == Rect { ui.y, PH - (ui.x + ui.w), ui.h, ui.w }, "rot 90: the curve area as a panel rectangle (w and h swapped)");
    check (r270 == Rect { PW - (ui.y + ui.h), ui.x, ui.h, ui.w }, "rot 270: the curve area as a panel rectangle");
    {
        int a, b, c, d;
        uiPointToPanel (90, ui.x, ui.y, PW, PH, a, b);
        uiPointToPanel (90, ui.x + ui.w - 1, ui.y + ui.h - 1, PW, PH, c, d);
        check (std::min (a, c) == r90.x && std::max (a, c) == r90.x + r90.w - 1 && std::min (b, d) == r90.y && std::max (b, d) == r90.y + r90.h - 1, "rot 90: the rectangle is the bounding box of its two corner points");
        uiPointToPanel (270, ui.x, ui.y, PW, PH, a, b);
        uiPointToPanel (270, ui.x + ui.w - 1, ui.y + ui.h - 1, PW, PH, c, d);
        check (std::min (a, c) == r270.x && std::max (a, c) == r270.x + r270.w - 1 && std::min (b, d) == r270.y && std::max (b, d) == r270.y + r270.h - 1, "rot 270: same");
    }
    // the rotate copy agrees with the point mapping for every pixel
    for (int rot : { 90, 270 })
    {
        const int w = 37, h = 21;
        std::vector<uint32_t> cv ((size_t) w * h), dst ((size_t) w * h + 16, 0xdeadbeefu);
        for (int i = 0; i < w * h; ++i) cv[(size_t) i] = 0x01000000u + (uint32_t) i;
        const Rect big { 100, 50, w, h };
        const Rect pr = uiRectToPanel (rot, big, PW, PH);
        rotateCanvasInto (rot, cv.data(), w, h, dst.data(), (size_t) pr.w * 4);
        bool ok = true;
        for (int cy = 0; cy < h && ok; ++cy)
            for (int cx = 0; cx < w; ++cx)
            {
                int ppx, ppy;
                uiPointToPanel (rot, big.x + cx, big.y + cy, PW, PH, ppx, ppy);
                if (dst[(size_t) (ppy - pr.y) * pr.w + (ppx - pr.x)] != cv[(size_t) cy * w + cx]) { ok = false; break; }
            }
        char name[96];
        std::snprintf (name, sizeof name, "rotation %d: the rotate copy puts every canvas pixel where the point mapping says", rot);
        check (ok, name);
        check (dst[(size_t) w * h] == 0xdeadbeefu, "... and writes nothing beyond the buffer");
    }
}

static void rendererTests()
{
    const int w = layout::kCurve.w, h = layout::kCurve.h;
    const size_t n = (size_t) w * h;
    std::vector<uint32_t> store (n + 64, 0xababababu);   // guard words behind the canvas
    uint32_t* cv = store.data();
    CpuCurveRenderer r;
    Rect covers[layout::kNumMarkers];
    for (int i = 0; i < layout::kNumMarkers; ++i)
        covers[i] = { layout::kMarkers[i].rect.x - layout::kCurve.x, layout::kMarkers[i].rect.y - layout::kCurve.y, layout::kMarkers[i].rect.w, layout::kMarkers[i].rect.h };

    // a known curve: 0 dB flat to 1 kHz, then falling 1.5 dB per point (a ramp), floor at -36
    float db[256];
    for (int i = 0; i < 256; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, i / 255.0f);
        db[i] = f < 1000.0f ? 0.0f : std::max (-36.0f, -1.5f * (float) (i - 170));
        if (i < 170) db[i] = 0.0f;
    }
    r.render (db, 256, cv, w, h, covers, layout::kNumMarkers);
    bool guard = true;
    for (size_t i = n; i < n + 64; ++i) guard = guard && store[i] == 0xabababab;
    check (guard, "the renderer writes nothing outside the canvas");

    // premultiplied alpha: no channel exceeds alpha
    bool pm = true; int opaque = 0, translucent = 0, clear = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const uint32_t a = cv[i] >> 24, rr = (cv[i] >> 16) & 255, g = (cv[i] >> 8) & 255, b = cv[i] & 255;
        if (rr > a || g > a || b > a) pm = false;
        if (a == 255) ++opaque; else if (a) ++translucent; else ++clear;
    }
    check (pm, "premultiplied alpha: no colour channel is larger than alpha, anywhere");
    check (clear > 0 && translucent > 0 && opaque > 0, "transparent background, translucent fill and an opaque line are all there");
    check (cv[0] == 0, "the top left corner stays transparent");

    // the line sits where the dB values say (centre of the pixel column -> row +-1)
    auto lineRowAt = [&] (int x) {   // the row with the most opaque accent-coloured pixel nearest to the curve: scan the column for alpha 255 with the line colour
        int first = -1, last = -1;
        for (int y = 0; y < h; ++y)
        {
            const uint32_t p = cv[(size_t) y * w + x];
            if ((p >> 24) == 255 && ((p >> 16) & 255) > 0xf0) { if (first < 0) first = y; last = y; }
        }
        return first < 0 ? -1.0 : 0.5 * (first + last);
    };
    int worst = 0;
    for (int x : { 100, 300, 500, 700, 800, 900, 1000, 1100 })
    {
        const double pos = (double) x / (w - 1) * 255.0;
        const int i = std::min (254, (int) pos);
        const double dbv = db[i] * (1.0 - (pos - i)) + db[i + 1] * (pos - i);
        const double expectY = CpuCurveRenderer::dbToY (dbv, h);
        const double got = lineRowAt (x);
        worst = std::max (worst, (int) std::lround (std::fabs (got + 0.5 - expectY)));
        if (got < 0 || std::fabs (got + 0.5 - expectY) > 1.5)
        {
            std::printf ("      column %d: expected y %.2f got %.1f\n", x, expectY, got);
            check (false, "the line is at the row of the dB value (+-1)");
        }
    }
    check (worst <= 1, "the line follows the dB values: the rows of 8 columns match the curve to +-1 px");
    // the 0 dB level is where the demo draws it (about the middle) and the fill is below the curve
    const double zy = CpuCurveRenderer::zeroLineY (h);
    check (zy > 60 && zy < 160, "the 0 dB row is inside the picture");
    const uint32_t below = cv[(size_t) (h - 3) * w + 300], above = cv[(size_t) 3 * w + 300];
    check ((below >> 24) > 0 && (below >> 24) < 128, "below the curve: translucent fill (alpha 25 %)");
    check (above == 0 || (above >> 24) <= 40, "above the curve: transparent (or the faint grid)");
    // markers covered with the opaque background
    bool cov = true;
    for (int i = 0; i < layout::kNumMarkers; ++i)
        for (int y = covers[i].y; y < covers[i].y + covers[i].h; ++y)
            for (int x = covers[i].x; x < covers[i].x + covers[i].w; ++x)
                cov = cov && cv[(size_t) y * w + x] == 0xff000000u + (layout::kBackgroundRgb);
    check (cov, "the marker squares are painted over with the opaque background colour");
    // grid lines
    const int gx = CpuCurveRenderer::gridX (1000.0, w);
    check ((cv[(size_t) 5 * w + gx] >> 24) > 0 && cv[(size_t) 5 * w + gx + 6] == 0, "the 1 kHz grid line is there and its neighbourhood is empty");

    // an extreme curve (a +60 dB peak, -120 dB floor) stays inside the canvas
    float ex[256];
    for (int i = 0; i < 256; ++i) ex[i] = i == 128 ? 60.0f : -120.0f;
    std::fill (store.begin(), store.end(), 0xababababu);
    r.render (ex, 256, cv, w, h, covers, layout::kNumMarkers);
    guard = true;
    for (size_t i = n; i < n + 64; ++i) guard = guard && store[i] == 0xabababab;
    check (guard, "extreme values (+60 / -120 dB) do not write outside the canvas either");
}

int main()
{
    geometryTests();
    rendererTests();
    std::printf (fails ? "geometry_render_host: FAILED (%d)\n" : "geometry_render_host: OK\n", fails);
    return fails ? 1 : 0;
}
