// SynthyBoy - MPC overlay tests: the table of views of the first overlay (a live filter curve over a region of a host tab, the LFO, oscillator, envelope and spectrum pictures, the Play tab), kept as it was for the
// tests of the controller machinery (page detection, the instance picture, the slots, the status file, the failures of the plane): they need a view that is a picture of its own and not a page the plugin draws
// entirely. The controller takes the table as a page set (pageSet () below: Controller's `pages` argument); the real layout is shell-mpc/overlay/overlay_layout.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstring>

#include "overlay_layout.h"
#include "../../../kit/synthyboy_adapter.h"

namespace synthyboy_mpc { namespace overlay { namespace testlayout {

using layout::View;
constexpr int kNumViews = 16;
constexpr View kViews[kNumViews] = {
    { "filter1", 0, 1, { 56, 124, 1168, 232 }, true, { { { 66, 134, 6, 6 }, 0x103a2c, 0x1e1d1e }, { { 1208, 134, 6, 6 }, 0x3a1064, 0x1e1d1e } }, { 78, 134, 6, 6 } },   // tab "Filter 1"
    { "lfo1", 1, 1, { 340, 140, 600, 240 }, true, { { { 350, 150, 6, 6 }, 0x1e3a2c, 0x1e1d1e }, { { 924, 150, 6, 6 }, 0x3a1e64, 0x1e1d1e } }, { 362, 150, 6, 6 } },   // tab "LFO 1"
    { "lfo2", 1, 2, { 340, 140, 600, 240 }, true, { { { 350, 150, 6, 6 }, 0x253a2c, 0x1e1d1e }, { { 924, 150, 6, 6 }, 0x3a2564, 0x1e1d1e } }, { 362, 150, 6, 6 } },   // tab "LFO 2"
    { "lfo3", 1, 3, { 340, 140, 600, 240 }, true, { { { 350, 150, 6, 6 }, 0x2c3a2c, 0x1e1d1e }, { { 924, 150, 6, 6 }, 0x3a2c64, 0x1e1d1e } }, { 362, 150, 6, 6 } },   // tab "LFO 3"
    { "lfo4", 1, 4, { 340, 140, 600, 240 }, true, { { { 350, 150, 6, 6 }, 0x333a2c, 0x1e1d1e }, { { 924, 150, 6, 6 }, 0x3a3364, 0x1e1d1e } }, { 362, 150, 6, 6 } },   // tab "LFO 4"
    { "osc1", 2, 1, { 384, 140, 512, 240 }, true, { { { 394, 150, 6, 6 }, 0x3a3a2c, 0x1e1d1e }, { { 880, 150, 6, 6 }, 0x3a3a64, 0x1e1d1e } }, { 406, 150, 6, 6 } },   // tab "Osc 1"
    { "osc2", 2, 2, { 384, 140, 512, 240 }, true, { { { 394, 150, 6, 6 }, 0x413a2c, 0x1e1d1e }, { { 880, 150, 6, 6 }, 0x3a4164, 0x1e1d1e } }, { 406, 150, 6, 6 } },   // tab "Osc 2"
    { "osc3", 2, 3, { 384, 140, 512, 240 }, true, { { { 394, 150, 6, 6 }, 0x483a2c, 0x1e1d1e }, { { 880, 150, 6, 6 }, 0x3a4864, 0x1e1d1e } }, { 406, 150, 6, 6 } },   // tab "Osc 3"
    { "env1", 3, 1, { 23, 178, 1230, 222 }, false, { { { 1180, 180, 4, 4 }, 0x4f3a2c, 0x1e1d1e }, { { 1200, 180, 4, 4 }, 0x3a4f64, 0x1e1d1e } }, { 1190, 180, 4, 4 } },   // tab "Env 1"
    { "env2", 3, 2, { 23, 178, 1230, 222 }, false, { { { 1180, 180, 4, 4 }, 0x563a2c, 0x1e1d1e }, { { 1200, 180, 4, 4 }, 0x3a5664, 0x1e1d1e } }, { 1190, 180, 4, 4 } },   // tab "Env 2"
    { "env3", 3, 3, { 23, 178, 1230, 222 }, false, { { { 1180, 180, 4, 4 }, 0x5d3a2c, 0x1e1d1e }, { { 1200, 180, 4, 4 }, 0x3a5d64, 0x1e1d1e } }, { 1190, 180, 4, 4 } },   // tab "Env 3"
    { "env4", 3, 4, { 23, 178, 1230, 222 }, false, { { { 1180, 180, 4, 4 }, 0x643a2c, 0x1e1d1e }, { { 1200, 180, 4, 4 }, 0x3a6464, 0x1e1d1e } }, { 1190, 180, 4, 4 } },   // tab "Env 4"
    { "spectrum", 4, 1, { 36, 590, 1208, 136 }, true, { { { 67, 594, 6, 6 }, 0x6b3a2c, 0x1e1d1e }, { { 1207, 594, 6, 6 }, 0x3a6b64, 0x1e1d1e } }, { 105, 594, 6, 6 } },   // tab "Macros"
    { "play1", 5, 1, { 23, 124, 1234, 190 }, true, { { { 33, 134, 6, 6 }, 0x723a2c, 0x1e1d1e }, { { 1241, 134, 6, 6 }, 0x3a7264, 0x1e1d1e } }, { 45, 134, 6, 6 } },   // tab "Play"
    { "play2", 5, 2, { 23, 124, 1234, 190 }, true, { { { 33, 134, 6, 6 }, 0x793a2c, 0x1e1d1e }, { { 1241, 134, 6, 6 }, 0x3a7964, 0x1e1d1e } }, { 45, 134, 6, 6 } },   // tab "Play Env"
    { "play3", 5, 3, { 23, 124, 1234, 190 }, true, { { { 33, 134, 6, 6 }, 0x803a2c, 0x1e1d1e }, { { 1241, 134, 6, 6 }, 0x3a8064, 0x1e1d1e } }, { 45, 134, 6, 6 } },   // tab "Play LFO"
};

inline int indexOf (const char* name)
{
    for (int i = 0; i < kNumViews; ++i)
        if (std::strcmp (kViews[i].name, name) == 0)
            return i;
    return -1;
}

/** The table as the page set the controller takes (kit/page_set.h; the instance colours are those of overlay_layout.h). */
inline const mpckit::IPageSet& pageSet()
{
    static const mpckit::synthyboy_adapter::TablePageSet set (kViews, kNumViews);
    return set;
}
}}} // namespace synthyboy_mpc::overlay::testlayout
