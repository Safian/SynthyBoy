// SynthyBoy - C++ Play overlay render fixture
// Renders the real PlayView + scene_cpu.h overlay for the documented fixture state.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "shell-mpc/overlay/native_marker_covers.h"
#include "shell-mpc/overlay/play_view.h"
#include "shell-mpc/overlay/tests/fakes.h"
#include "shell-mpc/ui/tests/ui_test.h"

namespace {

class PlayFixtureInstance final : public synthyboy_mpc::overlay::IInstanceView
{
public:
    void set (const char* key, float v) { base_.set (key, v); }
    void setPlaySelection (int g, int i) noexcept override { base_.setPlaySelection (g, i); }

    float param (int i) const noexcept override { return base_.param (i); }
    void writeParam (int i, float norm) noexcept override { base_.writeParam (i, norm); }
    double sampleRate() const noexcept override { return base_.sampleRate(); }
    double bpm() const noexcept override { return base_.bpm(); }
    void setQGroup (const int* targets, int n) noexcept override { base_.setQGroup (targets, n); }
    bool live (synthyboy::LiveValues& out) const noexcept override { return base_.live (out); }
    int playSelection (int group) const noexcept override { return base_.playSelection (group); }
    int tabView (int what) const noexcept override { return base_.tabView (what); }
    void setTabView (int what, int value) noexcept override { base_.setTabView (what, value); }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return base_.content(); }
    synthyboy_mpc::overlay::OscContentSnapshot oscContent() const override { return base_.oscContent(); }
    void setUiFullOpen (bool o) noexcept override { base_.setUiFullOpen (o); }

    bool nativePlayOscControls() const noexcept override { return true; }
    uint64_t playOscSelectionRevision() const noexcept override { return 1; }
    bool nativePlayEnvLfoControls() const noexcept override { return true; }
    uint64_t nativePlayEnvLfoRevision() const noexcept override { return 1; }

private:
    ovt::FakeInstance base_;
};

uint32_t alphaOver (uint32_t dst, uint32_t src) noexcept
{
    const uint32_t sa = (src >> 24) & 255u;
    if (sa == 0u)
        return dst;
    if (sa == 255u)
        return src;
    const uint32_t da = (dst >> 24) & 255u;
    const uint32_t inv = 255u - sa;
    const auto blend = [&] (int shift) -> uint32_t {
        const uint32_t s = (src >> shift) & 255u;
        const uint32_t d = (dst >> shift) & 255u;
        return std::min<uint32_t> (255u, s + (d * inv + 127u) / 255u);
    };
    const uint32_t outA = std::min<uint32_t> (255u, sa + (da * inv + 127u) / 255u);
    return (outA << 24) | (blend (16) << 16) | (blend (8) << 8) | blend (0);
}

bool writePngRgba (const std::string& path, int W, int H, const uint32_t* px)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return false;
    std::vector<unsigned char> raw;
    raw.reserve (static_cast<size_t> (H) * (static_cast<size_t> (W) * 4 + 1));
    for (int y = 0; y < H; ++y)
    {
        raw.push_back (0); // filter: none
        for (int x = 0; x < W; ++x)
        {
            const uint32_t p = px[y * W + x];
            raw.push_back (static_cast<unsigned char> ((p >> 16) & 255u)); // R
            raw.push_back (static_cast<unsigned char> ((p >> 8) & 255u));  // G
            raw.push_back (static_cast<unsigned char> (p & 255u));         // B
            raw.push_back (static_cast<unsigned char> ((p >> 24) & 255u)); // A
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
        z.push_back (static_cast<unsigned char> (n & 255));
        z.push_back (static_cast<unsigned char> (n >> 8));
        z.push_back (static_cast<unsigned char> (~n & 255));
        z.push_back (static_cast<unsigned char> ((~n >> 8) & 255));
        z.insert (z.end(), raw.begin() + static_cast<long> (pos), raw.begin() + static_cast<long> (pos + n));
        pos += n;
    }
    uit::put32 (z, (b << 16) | a);
    const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    std::fwrite (sig, 1, 8, f);
    std::vector<unsigned char> ihdr;
    uit::put32 (ihdr, static_cast<uint32_t> (W));
    uit::put32 (ihdr, static_cast<uint32_t> (H));
    ihdr.push_back (8); // bit depth 8
    ihdr.push_back (6); // color type 6: RGBA
    ihdr.push_back (0);
    ihdr.push_back (0);
    ihdr.push_back (0);
    uit::chunk (f, "IHDR", ihdr);
    uit::chunk (f, "IDAT", z);
    std::vector<unsigned char> empty;
    uit::chunk (f, "IEND", empty);
    std::fclose (f);
    return true;
}

} // namespace

int main (int argc, char** argv)
{
    std::string outPath = "play_overlay.png";
    bool tabOnly = true;
    bool smoothOn = true;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "--out" || arg == "-o") && i + 1 < argc)
            outPath = argv[++i];
        else if (arg == "--full")
            tabOnly = false;
        else if (arg == "--tab-only")
            tabOnly = true;
        else if (arg == "--smooth-off")
            smoothOn = false;
        else if (arg == "--smooth" && i + 1 < argc)
            smoothOn = (std::atoi (argv[++i]) != 0);
        else if (arg == "--help" || arg == "-h")
        {
            std::printf ("Usage: %s [--out PATH] [--full | --tab-only] [--smooth-off | --smooth 0|1]\n", argv[0]);
            return 0;
        }
    }

    // Enforce XL / Rule 5b: zero Q-Link badges or frames on compact Play.
    synthyboy_mpc::ui::QGroup::setWideDevice (true);

    // Documented fixture parameter state:
    // Sub active, Env 1 active, LFO 1 active, highlight on group 1 (Oscillator),
    // with production native Play OSC/ENV/LFO apertures enabled.
    PlayFixtureInstance inst;
    inst.setPlaySelection (0, synthyboy_mpc::playosc::kSub);
    inst.setPlaySelection (1, 1);
    inst.setPlaySelection (2, 1);

    inst.set ("sub.on", 1.0f);
    inst.set ("sub.waveform", 0.0f);
    inst.set ("sub.octave", 0.0f);
    inst.set ("sub.pan", 0.0f);
    inst.set ("sub.level", 50.0f);

    inst.set ("env1.attack", 0.0f);
    inst.set ("env1.decay", 1000.0f);
    inst.set ("env1.sustain", 50.0f);
    inst.set ("env1.release", 500.0f);

    inst.set ("lfo1.rate", 1.0f);
    inst.set ("lfo1.sync", 0.0f);
    inst.set ("lfo1.fade_in", 0.0f);
    inst.set ("lfo1.smooth", smoothOn ? 1.0f : 0.0f);
    inst.set ("lfo1.smooth_time", 150.0f);
    inst.set ("lfo1.shape", 0.0f);

    synthyboy_mpc::overlay::PlayView pv (1, 0.0);
    synthyboy_mpc::overlay::UiStack stack;
    synthyboy_mpc::overlay::ViewInput in;
    in.instance = &inst;
    in.io = &inst;
    in.ui = &stack;
    in.nowMs = 1000.0;
    pv.update (in);
    in.nowMs = 1500.0;
    pv.update (in);

    const int fullW = 1280, fullH = 800;
    std::vector<uint32_t> fullPixels (static_cast<size_t> (fullW) * static_cast<size_t> (fullH), 0u);
    pv.render (fullPixels.data(), fullW, fullH);

    // Cover layout markers and instance pixel using production native_marker_covers.h
    // against the selector header panel ground (#141a38).
    {
        synthyboy_mpc::overlay::gpu::SceneData markerScene;
        synthyboy_mpc::overlay::gpu::SceneBuilder msb (markerScene);
        msb.begin (synthyboy_mpc::overlay::gpu::kKindPlay, 0, fullW, fullH);
        msb.clear (0u);
        synthyboy_mpc::overlay::drawNativeMarkerCovers (msb, "play1", 0x141a38u, 0x141a38u);
        std::vector<uint32_t> markerPixels;
        synthyboy_mpc::overlay::renderScene (markerScene, markerPixels);
        const size_t n = std::min (fullPixels.size(), markerPixels.size());
        for (size_t i = 0; i < n; ++i)
            fullPixels[i] = alphaOver (fullPixels[i], markerPixels[i]);
    }

    int outW = fullW;
    int outH = fullH;
    std::vector<uint32_t> outPixels;

    if (tabOnly)
    {
        // Crop to tab coordinates: x=0..1280, y=110..738 (1280 x 628)
        outW = 1280;
        outH = 628;
        outPixels.resize (static_cast<size_t> (outW) * static_cast<size_t> (outH));
        for (int y = 0; y < outH; ++y)
        {
            for (int x = 0; x < outW; ++x)
            {
                outPixels[static_cast<size_t> (y * outW + x)] =
                    fullPixels[static_cast<size_t> ((y + 110) * fullW + x)];
            }
        }
    }
    else
    {
        outPixels = std::move (fullPixels);
    }

    if (! writePngRgba (outPath, outW, outH, outPixels.data()))
    {
        std::fprintf (stderr, "play_render_fixture: error: failed to write %s\n", outPath.c_str());
        return 1;
    }

    std::printf ("play_render_fixture: rendered %dx%d RGBA overlay to %s\n", outW, outH, outPath.c_str());
    return 0;
}
