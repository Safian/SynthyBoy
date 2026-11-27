// SynthyBoy - MPC UI component library: the THEME. Every colour, gradient, line width, font size, corner radius and spacing the components and the views draw with is in one table (struct Look);
// the components take their numbers from here and from nowhere else. Three looks (look 1 .. 3) are three tables, not branches in the code: they differ in how bold the colour is (and how much
// light), and in how the instance colours are handed out, all of them in the same neon direction: a quiet dark ground, the colour in the signal and in the values.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Colours are 0xRRGGBB (alpha is the drawing's business). The modulation sources have a colour each: the SAME colour is used on the depth arc of a knob, on its tab and on the modulation page
// (sourceColour). Room for the elements that are still to come (envelope, LFO, filter): the `env`, `lfo` gradients and the `filter` stops are in the table already.
#pragma once

#include <cstdint>
#include <cstring>

namespace synthyboy_mpc { namespace ui { namespace theme {

constexpr int kNumSources = 8;

struct Look
{
    const char* name;
    // ---- surfaces
    uint32_t bg0, bg1;               // the ground of a screen, top and bottom
    uint32_t panel0, panel1;         // a block (panel) fill, top and bottom
    uint32_t keyline, keylineHi;     // the outline of a block / of a control, and of a focused one
    uint32_t controlTop, controlBottom;          // the face of a control (Pen::control): the skin Button gradient colours (button.gradient.top / .bottom)
    uint32_t controlBorder, controlBorderBottom; // its keyline, top and bottom (button.gradient.border / .border.bottom)
    uint32_t track;                  // the empty part of an arc, a bar, a slider
    uint32_t grid;                   // the lines of the displays
    // ---- ink
    uint32_t text, textDim, textFaint, onBright;
    uint32_t q;                      // the Q-Link group frame (the host's own focus colour)
    uint32_t flow[4];                // the main signal gradient: four equal stops
    uint32_t inst[5];                // Sub, A, B, C, Noise
    uint32_t src[kNumSources];       // the modulation sources: env, lfo, velocity, key, wheel, pressure, macro, random
    uint32_t env[2], lfo[2];         // the gradients of the envelope (orange -> red) and the LFO (green -> cyan) panels
    uint32_t filter[2];              // for the filter display, still to come
    uint32_t negative;               // a negative modulation depth
    // ---- strokes (pixels)
    float curve, curveHi, curveBack, hair;   // the signal curve, the highlighted one, the layers behind, the grid
    float arcTrack, arcValue, arcMod;         // the thickness of a knob's track, value arc and modulation arc as a fraction of the dial's diameter (at least 2 pixels)
    float frame;                              // the outline of a block
    // ---- light
    float backGlow;                           // the faint glow BEHIND a display (a bloom of its lines, added under nothing else: lines, arcs, values and texts are never lit; 0 = none)
    float afterglow;                          // the display keeps this share (0 .. 1) of its previous picture under the new one (a trail; 0 = none, in every look: the feedback layer works, but the bloom of the picture is part of what it keeps and a trail of it washes the display out; it costs about 2 ms more a picture, see "Observed on the device")
    // ---- type (pixels per em)
    float fTitle, fValue, fLabel, fItem, fSmall, fMin;
    // ---- shape (pixels)
    float rBlock, rControl, rChip;
    int gap, pad;
    float controlGradStart, controlGradEnd;   // the control face is flat to this fraction of its height, linear to the end fraction, flat below (control.gradient)
};

// Exact palette and geometry values are generated from the shared JSON tokens.
#include "theme_tokens.h"

/** The LEVEL OF DETAIL, next to the look: how much of the drawing is made (the number of frames of the wavetable stack, of unison hairlines, the spectrum, the soft light passes, the afterglow).
    "full" is the designed, richest picture; "reduced" is what a machine that cannot keep it within its frame time gets (the plugin chooses from the measured frame time of the GPU helper; a file
    overrides it, see controller.h). One table, no branches on the machine in the components. */
struct Detail
{
    const char* name;
    int stackFrames;      // frames of the wavetable shown behind the current one (0 = none)
    int unisonHairs;      // at most this many unison hairlines around the main curve
    bool spectrum;        // the faint spectrum under a wave
    bool glow;            // the soft light passes (curves, arcs, dots)
    bool afterglow;       // the feedback layer under the displays
};
inline const Detail kDetailFull = { "full", 7, 6, true, true, true };
inline const Detail kDetailReduced = { "reduced", 3, 2, false, false, false };
inline const Detail* g_detail = &kDetailFull;
/** 1 = full, 2 = reduced (anything else: full). */
inline void setDetail (int n) noexcept { g_detail = n == 2 ? &kDetailReduced : &kDetailFull; }
inline const Detail& D() noexcept { return *g_detail; }
inline int detailLevel() noexcept { return g_detail == &kDetailReduced ? 2 : 1; }

inline const Look* g_look = &kLook1;
/** The active look (1 .. 3; anything else selects look 1). The views read it on every draw: switching is one pointer. */
inline void setLook (int n) noexcept { g_look = n == 2 ? &kLook2 : (n == 3 ? &kLook3 : &kLook1); }
inline const Look& L() noexcept { return *g_look; }

/** An 0xRRGGBB colour with an alpha (0 .. 255) as 0xRRGGBBAA. */
constexpr uint32_t rgba (uint32_t rgb, uint32_t a = 255) noexcept { return (rgb << 8) | a; }

/** The colour of a modulation source by its short name (ENV.., LFO.., VEL, KEY, MOD, PRES, MAC.., RND): the same in every place the source is drawn. Unknown names get a stable colour of the table. */
inline uint32_t sourceColour (const char* name) noexcept
{
    const Look& l = L();
    auto starts = [&] (const char* p) { return std::strncmp (name, p, std::strlen (p)) == 0; };
    if (starts ("ENV") || starts ("Env")) return l.src[0];
    if (starts ("LFO") || starts ("Lfo")) return l.src[1];
    if (starts ("VEL") || starts ("Vel")) return l.src[2];
    if (starts ("KEY") || starts ("Key") || starts ("NOTE")) return l.src[3];
    if (starts ("MOD") || starts ("Mod") || starts ("WHL")) return l.src[4];
    if (starts ("PRES") || starts ("Pres") || starts ("AT")) return l.src[5];
    if (starts ("MAC") || starts ("Mac")) return l.src[6];
    if (starts ("RND") || starts ("Rnd") || starts ("RAND")) return l.src[7];
    unsigned h = 0;
    for (const char* p = name; *p; ++p)
        h = h * 31u + static_cast<unsigned char> (*p);
    return l.src[h % kNumSources];
}

/** The colour of an instance (0 Sub, 1 A, 2 B, 3 C, 4 Noise; others wrap). */
inline uint32_t instanceColour (int i) noexcept { return L().inst[((i % 5) + 5) % 5]; }

/** Mixes two 0xRRGGBB colours (t = 0 .. 1). */
inline uint32_t mix (uint32_t a, uint32_t b, float t) noexcept
{
    uint32_t out = 0;
    for (int s = 0; s < 24; s += 8)
    {
        const float ca = static_cast<float> ((a >> s) & 255u), cb = static_cast<float> ((b >> s) & 255u);
        out |= static_cast<uint32_t> (ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

/** The theme's alert red colour (clipping and safety warnings: T.negative). */
inline uint32_t alert() noexcept { return L().negative; }

}}} // namespace synthyboy_mpc::ui::theme
