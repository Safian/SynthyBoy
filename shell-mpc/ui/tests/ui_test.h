// SynthyBoy - MPC UI component library tests: the helpers every test file shares: the check, a canvas the scene is drawn into by the CPU renderer (overlay/scene_cpu.h), the "nothing outside
// the rectangle" and "which pixels changed" tests, and a PNG writer for the pictures a human looks at.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../ui.h"
#include "../../overlay/scene_cpu.h"

namespace uit {

using namespace synthyboy_mpc::ui;
namespace ov = synthyboy_mpc::overlay;

inline int g_fails = 0;
inline void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_fails;
}

/** A scene holder (the scene is big: on the heap). */
struct Scene
{
    std::unique_ptr<gpu::SceneData> data { new gpu::SceneData() };
    std::unique_ptr<gpu::SceneBuilder> sb;
    Scene (int w, int h, uint32_t clearRgba = 0)
    {
        sb.reset (new gpu::SceneBuilder (*data));
        sb->begin (gpu::kKindUi, 0, w, h);
        if (clearRgba != 0)
            sb->clear (clearRgba);
    }
    gpu::SceneBuilder& builder() { return *sb; }
    bool overflow() const { return sb->overflow; }
    uint32_t numOps() const { return data->numOps; }
};

struct Image
{
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // premultiplied ARGB
    uint32_t at (int x, int y) const { return px[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)]; }
};

inline Image render (const Scene& s)
{
    Image im;
    im.w = s.data->w;
    im.h = s.data->h;
    ov::renderScene (*s.data, im.px);
    return im;
}

/** The number of pixels that differ from `background` outside `r` (a pixel is "drawn" when it is not the background). */
inline int drawnOutside (const Image& im, const Rect& r, uint32_t background)
{
    int n = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
            if (! r.contains (x, y) && im.at (x, y) != background)
                ++n;
    return n;
}
inline int drawnInside (const Image& im, const Rect& r, uint32_t background)
{
    int n = 0;
    for (int y = std::max (0, r.y); y < std::min (im.h, r.bottom()); ++y)
        for (int x = std::max (0, r.x); x < std::min (im.w, r.right()); ++x)
            n += im.at (x, y) != background;
    return n;
}

/** The data ops of a scene that reach into the rectangle `r`: a polyline of more than two vertices with the fills that follow it (down to their base row), a band, a line, a vertical line, a disc. The ground rectangles (the two point curves
    of a gradient rectangle, rectangles, rounded rectangles) and text are not counted: this is "something of a curve, a grid or a marker is drawn there". Draw the picture alone (without the buttons of its view) when `r` is a button. */
inline int dataOpsInto (const Scene& s, const Rect& r)
{
    const gpu::SceneData& d = *s.data;
    auto hit = [&] (float x0, float y0, float x1, float y1) { return x1 > static_cast<float> (r.x) && x0 < static_cast<float> (r.right()) && y1 > static_cast<float> (r.y) && y0 < static_cast<float> (r.bottom()); };
    auto span = [&] (int first, int n, float& lo, float& hi)
    {
        lo = 1.0e9f; hi = -1.0e9f;
        for (int i = 0; i < n; ++i) { lo = std::min (lo, d.y[first + i]); hi = std::max (hi, d.y[first + i]); }
    };
    int hits = 0;
    bool data = false;
    float cx0 = 0.0f, cx1 = 0.0f, cy0 = 0.0f, cy1 = 0.0f;
    for (uint32_t k = 0; k < d.numOps; ++k)
    {
        const gpu::SceneOp& o = d.ops[k];
        switch (o.op)
        {
            case gpu::kOpCurve:
                data = o.f[1] > 2.0f;
                if (data)
                {
                    cx0 = o.f[2]; cx1 = o.f[2] + o.f[3];
                    span (static_cast<int> (o.f[0]), static_cast<int> (o.f[1]), cy0, cy1);
                    hits += hit (cx0, cy0, cx1, cy1);
                }
                break;
            case gpu::kOpFill: case gpu::kOpFillGradient:
                if (data)
                    hits += hit (cx0, std::min (cy0, o.f[0]), cx1, std::max (cy1, o.f[0]));
                break;
            case gpu::kOpBand:
            {
                float a0, a1, b0, b1;
                span (static_cast<int> (o.f[0]), static_cast<int> (o.f[1]), a0, a1);
                span (static_cast<int> (o.f[4]), static_cast<int> (o.f[1]), b0, b1);
                hits += hit (o.f[2], std::min (a0, b0), o.f[2] + o.f[3], std::max (a1, b1));
                break;
            }
            case gpu::kOpLine:
            {
                const float h = 0.5f * o.f[4];
                hits += hit (std::min (o.f[0], o.f[2]) - h, std::min (o.f[1], o.f[3]) - h, std::max (o.f[0], o.f[2]) + h, std::max (o.f[1], o.f[3]) + h);
                break;
            }
            case gpu::kOpVLine:
                hits += hit (o.f[0] - 0.5f * o.f[3], std::min (o.f[1], o.f[2]), o.f[0] + 0.5f * o.f[3], std::max (o.f[1], o.f[2]));
                break;
            case gpu::kOpDisc:
                hits += hit (o.f[0] - o.f[2], o.f[1] - o.f[2], o.f[0] + o.f[2], o.f[1] + o.f[2]);
                break;
            default: break;
        }
    }
    return hits;
}

/** The colour (RGB, 0xRRGGBB) of the pixel, composed over black (the canvases of the tests are drawn over the background colour). */
inline uint32_t rgbAt (const Image& im, int x, int y) { return im.at (x, y) & 0xffffffu; }
inline int channelDistance (uint32_t a, uint32_t b)
{
    int m = 0;
    for (int s = 0; s < 24; s += 8)
        m = std::max (m, std::abs (static_cast<int> ((a >> s) & 255u) - static_cast<int> ((b >> s) & 255u)));
    return m;
}
/** Is a pixel near (x, y) (within `r` pixels) of the colour `rgb` (within `tol` per channel)? */
inline bool colourNear (const Image& im, float fx, float fy, int r, uint32_t rgb, int tol)
{
    for (int y = static_cast<int> (fy) - r; y <= static_cast<int> (fy) + r; ++y)
        for (int x = static_cast<int> (fx) - r; x <= static_cast<int> (fx) + r; ++x)
            if (x >= 0 && y >= 0 && x < im.w && y < im.h && im.at (x, y) >> 24 == 255 && channelDistance (rgbAt (im, x, y), rgb) <= tol)
                return true;
    return false;
}

// ---- PNG (stored deflate blocks: no library, readable by any viewer)
inline uint32_t crc32 (const unsigned char* d, size_t n, uint32_t crc = 0)
{
    static uint32_t table[256];
    static bool init = false;
    if (! init)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i)
        crc = table[(crc ^ d[i]) & 255u] ^ (crc >> 8);
    return ~crc;
}
inline void put32 (std::vector<unsigned char>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back (static_cast<unsigned char> (x >> s)); }
inline void chunk (FILE* f, const char* type, const std::vector<unsigned char>& data)
{
    std::vector<unsigned char> b;
    put32 (b, static_cast<uint32_t> (data.size()));
    for (int i = 0; i < 4; ++i)
        b.push_back (static_cast<unsigned char> (type[i]));
    b.insert (b.end(), data.begin(), data.end());
    std::vector<unsigned char> c (b.begin() + 4, b.end());
    put32 (b, crc32 (c.data(), c.size()));
    std::fwrite (b.data(), 1, b.size(), f);
}
/** Writes the image as an RGB PNG (premultiplied pixels are shown over black), scaled up `scale` times (whole pixels). */
inline bool writePng (const std::string& path, const Image& im, int scale = 1)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return false;
    const int W = im.w * scale, H = im.h * scale;
    std::vector<unsigned char> raw;
    raw.reserve (static_cast<size_t> (H) * (static_cast<size_t> (W) * 3 + 1));
    for (int y = 0; y < H; ++y)
    {
        raw.push_back (0);
        for (int x = 0; x < W; ++x)
        {
            const uint32_t p = im.at (x / scale, y / scale);
            raw.push_back (static_cast<unsigned char> ((p >> 16) & 255u));
            raw.push_back (static_cast<unsigned char> ((p >> 8) & 255u));
            raw.push_back (static_cast<unsigned char> (p & 255u));
        }
    }
    std::vector<unsigned char> z { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw)
    {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    for (size_t pos = 0; pos < raw.size();)
    {
        const size_t n = std::min<size_t> (65535, raw.size() - pos);
        z.push_back (pos + n >= raw.size() ? 1 : 0);
        z.push_back (static_cast<unsigned char> (n & 255)); z.push_back (static_cast<unsigned char> (n >> 8));
        z.push_back (static_cast<unsigned char> (~n & 255)); z.push_back (static_cast<unsigned char> ((~n >> 8) & 255));
        z.insert (z.end(), raw.begin() + static_cast<long> (pos), raw.begin() + static_cast<long> (pos + n));
        pos += n;
    }
    put32 (z, (b << 16) | a);
    const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    std::fwrite (sig, 1, 8, f);
    std::vector<unsigned char> ihdr;
    put32 (ihdr, static_cast<uint32_t> (W)); put32 (ihdr, static_cast<uint32_t> (H));
    ihdr.push_back (8); ihdr.push_back (2); ihdr.push_back (0); ihdr.push_back (0); ihdr.push_back (0);
    chunk (f, "IHDR", ihdr);
    chunk (f, "IDAT", z);
    chunk (f, "IEND", {});
    std::fclose (f);
    return true;
}

/** The opaque background the pictures of the tests are drawn over (the colour of the overlay views). */
constexpr uint32_t kBackgroundRgba = 0x1e1d1eff;
inline uint32_t backgroundPixel() { return ov::sceneColour (kBackgroundRgba); }

/** A canvas bigger than the component: the component is drawn at `r` inside it. */
inline Image drawComponent (const Component& c, int canvasW, int canvasH)
{
    Scene s (canvasW, canvasH, kBackgroundRgba);
    c.draw (s.builder());
    return render (s);
}

/** Where the pictures of the tests go: the directory in UI_OUT (made when it is not there), else a directory of its own under the system's temporary directory. NEVER the working directory: a test run from
    the source tree (shell-mpc/) put its pictures into the repository, where they were committed by mistake. */
inline std::string outDir()
{
    const char* d = std::getenv ("UI_OUT");
    std::error_code ec;
    const std::filesystem::path dir = d && *d ? std::filesystem::path (d) : std::filesystem::temp_directory_path (ec) / "synthyboy-ui-images";
    std::filesystem::create_directories (dir, ec);
    return dir.string();
}

} // namespace uit
