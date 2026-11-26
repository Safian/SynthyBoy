// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
// SynthyBoy - MPC standalone Macros screen overlay C++ scene render fixture.
// Renders the authentic dynamic overlay layer of the Macros tab using the
// real MacroView, SpectrumStrip, and scene_cpu scene renderer.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "shell-mpc/overlay/native_marker_covers.h"
#include "shell-mpc/overlay/page_view_base.h"
#include "shell-mpc/overlay/scene_cpu.h"
#include "shell-mpc/ui/mod_macros.h"

namespace {

uint32_t crc32_calc (const unsigned char* d, size_t n)
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
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i)
        crc = table[(crc ^ d[i]) & 255u] ^ (crc >> 8);
    return ~crc;
}

void put32 (std::vector<unsigned char>& v, uint32_t x)
{
    for (int s = 24; s >= 0; s -= 8)
        v.push_back (static_cast<unsigned char> (x >> s));
}

void chunk (FILE* f, const char* type, const std::vector<unsigned char>& data)
{
    std::vector<unsigned char> b;
    put32 (b, static_cast<uint32_t> (data.size()));
    for (int i = 0; i < 4; ++i)
        b.push_back (static_cast<unsigned char> (type[i]));
    b.insert (b.end(), data.begin(), data.end());
    std::vector<unsigned char> c (b.begin() + 4, b.end());
    put32 (b, crc32_calc (c.data(), c.size()));
    std::fwrite (b.data(), 1, b.size(), f);
}

bool writeRgbaPng (const char* path, const uint32_t* argb, int w, int h)
{
    FILE* f = std::fopen (path, "wb");
    if (! f)
        return false;

    std::vector<unsigned char> raw;
    raw.reserve (static_cast<size_t> (h) * (static_cast<size_t> (w) * 4 + 1));
    for (int y = 0; y < h; ++y)
    {
        raw.push_back (0); // filter: None
        for (int x = 0; x < w; ++x)
        {
            const uint32_t p = argb[y * w + x];
            const unsigned char a = static_cast<unsigned char> ((p >> 24) & 255u);
            unsigned char r = static_cast<unsigned char> ((p >> 16) & 255u);
            unsigned char g = static_cast<unsigned char> ((p >> 8) & 255u);
            unsigned char b = static_cast<unsigned char> (p & 255u);

            // Un-premultiply ARGB to straight RGBA for standard PNG scanout
            if (a > 0 && a < 255)
            {
                r = static_cast<unsigned char> (std::min (255, (r * 255 + a / 2) / a));
                g = static_cast<unsigned char> (std::min (255, (g * 255 + a / 2) / a));
                b = static_cast<unsigned char> (std::min (255, (b * 255 + a / 2) / a));
            }
            raw.push_back (r);
            raw.push_back (g);
            raw.push_back (b);
            raw.push_back (a);
        }
    }

    // Uncompressed deflate zlib stream
    std::vector<unsigned char> z { 0x78, 0x01 };
    uint32_t adler_a = 1, adler_b = 0;
    for (const unsigned char c : raw)
    {
        adler_a = (adler_a + c) % 65521u;
        adler_b = (adler_b + adler_a) % 65521u;
    }
    for (size_t pos = 0; pos < raw.size();)
    {
        const size_t n = std::min<size_t> (65535, raw.size() - pos);
        z.push_back (pos + n >= raw.size() ? 1 : 0);
        z.push_back (static_cast<unsigned char> (n & 255u));
        z.push_back (static_cast<unsigned char> (n >> 8));
        z.push_back (static_cast<unsigned char> (~n & 255u));
        z.push_back (static_cast<unsigned char> ((~n >> 8) & 255u));
        z.insert (z.end(), raw.begin() + static_cast<long> (pos), raw.begin() + static_cast<long> (pos + n));
        pos += n;
    }
    put32 (z, (adler_b << 16) | adler_a);

    const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    std::fwrite (sig, 1, 8, f);

    std::vector<unsigned char> ihdr;
    put32 (ihdr, static_cast<uint32_t> (w));
    put32 (ihdr, static_cast<uint32_t> (h));
    ihdr.push_back (8); // 8-bit
    ihdr.push_back (6); // RGBA color type
    ihdr.push_back (0); // compression
    ihdr.push_back (0); // filter
    ihdr.push_back (0); // interlace
    chunk (f, "IHDR", ihdr);
    chunk (f, "IDAT", z);
    chunk (f, "IEND", {});
    std::fclose (f);
    return true;
}

std::vector<std::string> splitCsv (const std::string& s)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size())
    {
        size_t comma = s.find (',', start);
        if (comma == std::string::npos)
        {
            out.push_back (s.substr (start));
            break;
        }
        out.push_back (s.substr (start, comma - start));
        start = comma + 1;
    }
    return out;
}

} // namespace

int main (int argc, char** argv)
{
    std::string outPath = "macros_overlay.png";
    int bank = 0;
    std::string device = "xl"; // "xl" (16 Q-Links, no bank frame) or "key61" (4 Q-Links with bank frame)
    std::string fixture = "default"; // "default", "named", "zero"
    std::string customNamesStr;
    std::string customValuesStr;
    int w = 1280;
    int h = 628;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if ((arg == "--out" || arg == "-o") && i + 1 < argc)
            outPath = argv[++i];
        else if (arg == "--bank" && i + 1 < argc)
            bank = std::atoi (argv[++i]);
        else if (arg == "--device" && i + 1 < argc)
            device = argv[++i];
        else if (arg == "--fixture" && i + 1 < argc)
            fixture = argv[++i];
        else if (arg == "--macro-names" && i + 1 < argc)
            customNamesStr = argv[++i];
        else if (arg == "--macro-values" && i + 1 < argc)
            customValuesStr = argv[++i];
        else if (arg == "--help" || arg == "-h")
        {
            std::printf ("Usage: %s [options]\n", argv[0]);
            std::printf ("  --out FILE            Output RGBA PNG path (default: macros_overlay.png)\n");
            std::printf ("  --bank 0|1            Active Q-Link bank (0: Bank 1, 1: Bank 2)\n");
            std::printf ("  --device xl|key61     Hardware device model (xl: wide 16 knobs; key61: 4 knobs)\n");
            std::printf ("  --fixture NAME        Fixture state preset: default|named|zero (default: default)\n");
            std::printf ("  --macro-names CSV     Comma-separated custom macro names\n");
            std::printf ("  --macro-values CSV    Comma-separated custom macro values (0.0..1.0 or 0..100)\n");
            return 0;
        }
        else
        {
            std::fprintf (stderr, "Unknown or incomplete option: %s\n", arg.c_str());
            return 2;
        }
    }

    using namespace synthyboy_mpc;

    ui::MacroView view;
    view.setBank (bank);
    constexpr int canvasH = overlay::page::kH;
    constexpr int originY = overlay::page::kTabY;
    view.place ({ 0, originY, w, h });

    // Explicit synthetic spectrum fixture representing typical synthesizer output
    // (musical harmonic roll-off with resonant peaks across 256 log-frequency bins)
    float syntheticSpectrum[ui::SpectrumStrip::kPoints];
    for (int i = 0; i < ui::SpectrumStrip::kPoints; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (ui::SpectrumStrip::kPoints);
        const double env = std::exp (-3.5 * t) * std::pow (std::sin (t * 12.0), 2) * 0.70
                         + 0.45 * std::exp (-std::pow (t - 0.28, 2) / 0.015)
                         + 0.55 * std::exp (-std::pow (t - 0.48, 2) / 0.02)
                         + 0.28 * std::exp (-std::pow (t - 0.75, 2) / 0.04);
        syntheticSpectrum[i] = static_cast<float> (std::clamp (env, 0.04, 0.92));
    }
    view.spectrum.levels = syntheticSpectrum;

    // Hardware Q-Link configuration
    const bool isWide = (device == "xl");
    ui::QGroup::setWideDevice (isWide);

    // Resolve Macro state fixture:
    // Supports two differing non-zero macro values fixtures and two name set fixtures,
    // plus baseline zero-state, ensuring state resolution cannot be concealed by 0.0 placeholders.
    std::vector<std::string> macroNames (ui::MacroView::kNumMacros);
    std::vector<double> macroValues (ui::MacroView::kNumMacros, 0.0);
    std::vector<std::string> macroValueTexts (ui::MacroView::kNumMacros);

    if (fixture == "named")
    {
        // Fixture B: Contextual sound design parameter names with distinct non-zero values
        const char* const kNamedNames[8] = {
            "CUTOFF", "RESO", "DRIVE", "WARP", "SPACE", "ATTACK", "DECAY", "RATE"
        };
        const double kNamedValues[8] = {
            0.50, 0.75, 0.45, 0.85, 0.60, 0.30, 0.15, 0.90
        };
        for (int i = 0; i < 8; ++i)
        {
            macroNames[i] = kNamedNames[i];
            macroValues[i] = kNamedValues[i];
            char buf[16];
            std::snprintf (buf, sizeof buf, "%.1f%%", kNamedValues[i] * 100.0);
            macroValueTexts[i] = buf;
        }
    }
    else if (fixture == "zero")
    {
        // Fixture C: Baseline zero state
        for (int i = 0; i < 8; ++i)
        {
            char name[16];
            std::snprintf (name, sizeof name, "MACRO %d", i + 1);
            macroNames[i] = name;
            macroValues[i] = 0.0;
            macroValueTexts[i] = "0.0%";
        }
    }
    else // "default"
    {
        // Fixture A: Documented numbered names with authentic non-zero values
        // (distinct values for Bank 1: 35%, 70%, 12%, 95% and Bank 2: 50%, 25%, 80%, 40%)
        const double kDefaultValues[8] = {
            0.35, 0.70, 0.12, 0.95, 0.50, 0.25, 0.80, 0.40
        };
        for (int i = 0; i < 8; ++i)
        {
            char name[16];
            std::snprintf (name, sizeof name, "MACRO %d", i + 1);
            macroNames[i] = name;
            macroValues[i] = kDefaultValues[i];
            char buf[16];
            std::snprintf (buf, sizeof buf, "%.1f%%", kDefaultValues[i] * 100.0);
            macroValueTexts[i] = buf;
        }
    }

    if (! customNamesStr.empty())
    {
        auto custom = splitCsv (customNamesStr);
        for (size_t i = 0; i < custom.size() && i < 8; ++i)
            macroNames[i] = custom[i];
    }
    if (! customValuesStr.empty())
    {
        auto custom = splitCsv (customValuesStr);
        for (size_t i = 0; i < custom.size() && i < 8; ++i)
        {
            double v = std::atof (custom[i].c_str());
            if (v > 1.0) v /= 100.0;
            macroValues[i] = std::clamp (v, 0.0, 1.0);
            char buf[16];
            std::snprintf (buf, sizeof buf, "%.1f%%", macroValues[i] * 100.0);
            macroValueTexts[i] = buf;
        }
    }

    for (int i = 0; i < ui::MacroView::kNumMacros; ++i)
    {
        view.cards[i].setMacro (i, macroNames[i].c_str());
        view.cards[i].setValue (macroValues[i], macroValueTexts[i].c_str());
        view.cards[i].nameId = 500 + i; // kMacroNameBase + i
    }

    view.updateQGroup();

    auto scene = std::make_unique<overlay::gpu::SceneData>();
    overlay::gpu::SceneBuilder sb (*scene);
    sb.begin (overlay::gpu::kKindPlay, 0, w, canvasH);

    {
        ui::Cell cell (sb, { 0, originY, w, ui::MacroView::kBarH }, 0);
        overlay::drawNativeMarkerCovers (sb, "macros");
    }
    view.draw (sb);

    std::vector<uint32_t> renderedPixels;
    overlay::renderScene (*scene, renderedPixels);
    std::vector<uint32_t> pixels (static_cast<size_t> (w * h));
    for (int y = 0; y < h; ++y)
        std::copy_n (renderedPixels.data() + static_cast<size_t> (y + originY) * static_cast<size_t> (w), w, pixels.data() + static_cast<size_t> (y) * static_cast<size_t> (w));

    if (! writeRgbaPng (outPath.c_str(), pixels.data(), w, h))
    {
        std::fprintf (stderr, "Error: Failed to write PNG to %s\n", outPath.c_str());
        return 1;
    }

    // Report render metrics to stdout for test verification
    size_t nonZeroAlpha = 0;
    size_t headerExtraPixels = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const uint32_t p = pixels[static_cast<size_t> (y * w + x)];
            if ((p >> 24) > 0)
                nonZeroAlpha++;
            if (x >= 300 && x < 1200 && y >= 4 && y < 60 && (p >> 24) > 0)
                headerExtraPixels++;
        }
    }

    std::printf ("{\"status\":\"ok\",\"file\":\"%s\",\"width\":%d,\"height\":%d,\"device\":\"%s\",\"bank\":%d,\"wide\":%s,\"fixture\":\"%s\",\"active_pixels\":%zu,\"header_extra_pixels\":%zu}\n",
                 outPath.c_str(), w, h, device.c_str(), bank, isWide ? "true" : "false", fixture.c_str(), nonZeroAlpha, headerExtraPixels);

    return 0;
}
