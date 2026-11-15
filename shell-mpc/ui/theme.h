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
    float glowSoft, glowAlpha, glowWidth;     // the feather of the soft pass, its alpha (0 .. 1), the half width added to the line
    float afterglow;                          // the display keeps this share (0 .. 1) of its previous picture under the new one (a trail; 0 = none): the GPU's feedback layer
    // ---- type (pixels per em)
    float fTitle, fValue, fLabel, fItem, fSmall, fMin;
    // ---- shape (pixels)
    float rBlock, rControl, rChip;
    int gap, pad;
};

// look 1: the reference neon, balanced
inline const Look kLook1 = {
    "neon",
    0x0b1026, 0x05070f, 0x141a38, 0x0c1026, 0x27306a, 0x4a5aa8, 0x1c2350, 0x1a2250,
    0xeef1ff, 0xaab2d8, 0x6c76a8, 0x05070f,
    0xffc23d,
    { 0x2ee6f0, 0x6c7bff, 0xb45cff, 0xff4fc3 },
    { 0xffb03d, 0x2ee6f0, 0xb45cff, 0xff4fc3, 0x5cff9d },
    { 0xff9a3d, 0x5cff9d, 0xffd84a, 0x6c9bff, 0xff7ad9, 0x4fe0c8, 0xb08cff, 0xff6f6f },
    { 0xff9a3d, 0xff4f4f }, { 0x5cff9d, 0x2ee6f0 }, { 0x2ee6f0, 0xb45cff },
    0xff5a7a,
    1.25f, 2.0f, 0.9f, 1.0f,
    0.060f, 0.078f, 0.042f,
    1.0f,
    8.0f, 0.34f, 2.2f, 0.55f,
    28.0f, 24.0f, 15.0f, 22.0f, 12.0f, 9.0f,
    14.0f, 10.0f, 6.0f,
    8, 12
};

// look 2: calmer, the colour in smaller doses, thinner lines, little light
inline const Look kLook2 = {
    "aurora",
    0x0a0f1e, 0x04060c, 0x121a30, 0x0a1020, 0x243058, 0x4562a0, 0x182044, 0x182046,
    0xe6ecf8, 0x9fadc8, 0x64708f, 0x04060c,
    0xffc23d,
    { 0x34d6c0, 0x4c8dff, 0x8f6bff, 0xe86bd8 },
    { 0xffc058, 0x34d6c0, 0x8f6bff, 0xe86bd8, 0x7de08a },
    { 0xffa95a, 0x7de08a, 0xf2d75a, 0x6aa3ff, 0xe58ad8, 0x5fd8c4, 0xa79bff, 0xf08080 },
    { 0xffa95a, 0xf06a6a }, { 0x7de08a, 0x34d6c0 }, { 0x34d6c0, 0x8f6bff },
    0xf06a86,
    1.0f, 1.7f, 0.7f, 0.8f,
    0.050f, 0.062f, 0.036f,
    1.0f,
    6.0f, 0.20f, 1.8f, 0.0f,
    28.0f, 24.0f, 15.0f, 22.0f, 12.0f, 9.0f,
    12.0f, 8.0f, 5.0f,
    8, 12
};

// look 3: the boldest: saturated, thicker lines, a lot of light, the instance colours dealt the other way round
inline const Look kLook3 = {
    "plasma",
    0x120a1e, 0x07040d, 0x1c1133, 0x10081f, 0x3a2764, 0x6a4cc0, 0x2a1a50, 0x2a1c52,
    0xffffff, 0xc4b8e8, 0x8576b4, 0x07040d,
    0xffc23d,
    { 0xff4fc3, 0xb45cff, 0x2e8bff, 0x2ee6f0 },
    { 0xffc23d, 0xff4fc3, 0x2ee6f0, 0xb45cff, 0x5cff9d },
    { 0xffa030, 0x5cff9d, 0xffe14a, 0x4fa0ff, 0xff5ccc, 0x2ef0d0, 0xc090ff, 0xff6060 },
    { 0xffa030, 0xff3b6b }, { 0x5cff9d, 0x2ee6f0 }, { 0xff4fc3, 0x2ee6f0 },
    0xff4060,
    1.5f, 2.4f, 1.1f, 1.0f,
    0.075f, 0.098f, 0.052f,
    1.5f,
    10.0f, 0.46f, 2.8f, 0.72f,
    28.0f, 24.0f, 15.0f, 22.0f, 12.0f, 9.0f,
    16.0f, 12.0f, 8.0f,
    8, 12
};

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

}}} // namespace synthyboy_mpc::ui::theme
