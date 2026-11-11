// SynthyBoy - golden images of the bitmap GUI: the editor as the extension renders it (not through the editor directly) at 390 x 844 (phone), 1024 x 768 (tablet) and 1024 x 600 (the
// Akai MPC), compared with the stored pictures with a tolerance. SYNTHYBOY_UPDATE_GOLDEN=1 writes the goldens (tests/bitmap-gui/golden/); SYNTHYBOY_BITMAP_GUI_DUMP_DIR=dir writes the actual
// pictures as PNG (and the difference of a failed comparison).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>

#include "bitmap_gui_harness.h"
#include "golden_image.h"
#include "png_writer.h"
#include "test_runner.h"

using namespace bgtest;

#ifndef SYNTHYBOY_GOLDEN_DIR
 #error "SYNTHYBOY_GOLDEN_DIR names tests/bitmap-gui/golden"
#endif

namespace
{
// The renderer is the software renderer of JUCE and the glyphs come from the embedded Roboto, so the pictures are the same on every platform up to floating point rounding in the
// rasteriser. Measured: macOS arm64 against Linux aarch64 (GCC 12): the phone, tablet and MPC first frames differ by at most one step of one channel; the filter page (a curve drawn from
// floating point maths) has 0.06 % of its pixels more than 8 steps apart, at most 86 (an anti-aliased edge a pixel off). The tolerance is a pixel counts as different from 9 steps of a
// channel on; at most 0.2 % of the pixels may differ that much, and on average no more than 0.15 of 255 per channel. A missing glyph, a shifted label or another font fails by far.
constexpr int kChannelThreshold = 8;
constexpr double kMaxFractionOver = 0.002;
constexpr double kMaxMeanDiff = 0.15;

void checkAgainstGolden (const char* name, const Image& actual)
{
    const std::string path = std::string (SYNTHYBOY_GOLDEN_DIR) + "/" + name + ".sbg";
    if (const char* dir = std::getenv ("SYNTHYBOY_BITMAP_GUI_DUMP_DIR"))
        writePng (std::string (dir) + "/" + name + ".png", actual);
    if (std::getenv ("SYNTHYBOY_UPDATE_GOLDEN") != nullptr)
    {
        CHECK (writeGolden (path, actual));
        std::printf ("    golden written: %s\n", path.c_str());
        return;
    }
    Image golden;
    if (! CHECK (readGolden (path, golden)))
    {
        std::printf ("    no golden %s (SYNTHYBOY_UPDATE_GOLDEN=1 writes it)\n", path.c_str());
        return;
    }
    const Difference d = compareImages (actual, golden, kChannelThreshold);
    std::printf ("    %s: %u x %u, %.4f %% of the pixels differ by more than %d, mean %.4f, max %d\n", name, actual.width, actual.height, d.fractionOver * 100.0, kChannelThreshold,
                 d.meanChannelDiff, d.maxChannelDiff);
    CHECK (d.sameSize);
    CHECK (d.fractionOver <= kMaxFractionOver);
    CHECK (d.meanChannelDiff <= kMaxMeanDiff);
    if (d.fractionOver > kMaxFractionOver || d.meanChannelDiff > kMaxMeanDiff)
        if (const char* dir = std::getenv ("SYNTHYBOY_BITMAP_GUI_DUMP_DIR"))
            writePng (std::string (dir) + "/" + name + "-golden.png", golden);
}

/** One frame of a freshly created GUI of the given size after it has settled. */
Image renderScene (Instance& host, uint32_t w, uint32_t h)
{
    CHECK (host.create (w, h, 1.0));
    host.tickFor (300);
    FrameInfo f;
    Image img;
    host.render (f, &img);
    host.render (f, &img);
    CHECK (f.dirty.empty()); // settled
    return img;
}
} // namespace

TEST (golden_phone_390x844)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    checkAgainstGolden ("phone-390x844", renderScene (host, 390, 844));
}

TEST (golden_tablet_1024x768)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    checkAgainstGolden ("tablet-1024x768", renderScene (host, 1024, 768));
}

TEST (golden_mpc_1024x600)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    checkAgainstGolden ("mpc-1024x600", renderScene (host, 1024, 600));
}

TEST (golden_mpc_filter_page_and_scale_two)
{
    Instance host;
    if (! CHECK (host.ok()))
        return;
    // the second page (a tap on the FLT tab, as the MPC's touch screen would) and a frame at device scale 2 (the pixels double, the layout does not change)
    CHECK (host.create (1024, 600, 1.0));
    host.tickFor (200);
    FrameInfo f;
    Image img;
    host.render (f, &img);
    host.tap (320, 86);
    host.tickFor (300);
    host.render (f, &img);
    host.render (f, &img);
    checkAgainstGolden ("mpc-1024x600-flt", img);
    CHECK (host.gui->set_scale (host.plugin, 2.0));
    host.tickFor (200);
    CHECK (host.render (f, &img));
    CHECK ((f.flags & SAFIAN_BITMAP_GUI_FRAME_FULL) != 0);
    CHECK_EQ (img.width, 2048u);
    CHECK_EQ (img.height, 1200u);
}

TEST (golden_zz_module_shutdown)
{
    Module::get().shutdown();
    CHECK (true);
}
