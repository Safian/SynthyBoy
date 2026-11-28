// SynthyBoy - MPC overlay, GPU helper: the protocol shared by the plugin and the helper process (synthyboy-overlay-gpu). One header, versioned (kProtocolVersion).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Two channels between the two processes:
//  - a shared memory block (memfd, mapped by both): the SCENE (what to draw, as a short display list plus one polyline), written by the plugin under a seqlock so that the helper
//    never reads a half written scene, and a LEDGER of the DRM objects the helper has made on the shared display descriptor (the plugin removes them when the helper dies);
//  - a Unix socket pair (SOCK_SEQPACKET, fixed 64 byte messages, struct Msg): control messages (hello with the descriptors, show / scene kick / hide / quit) and the helper's answers
//    (ready, frame acknowledgements with timing, hidden, error text).
// Only fixed size integer and float members (no pointers, no padding): both sides are the same architecture, and the Mac tests run the same header on a 64 bit host.
//
// The scene is in canvas pixels of the LANDSCAPE region (x right, y down, a pixel (px, py) covers [px, px + 1) x [py, py + 1)); the helper rotates it into the panel's portrait buffer
// (rotation 90 / 270, geometry.h) in its vertex shader. Colours are straight (not premultiplied) 0xRRGGBBAA.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace synthyboy_mpc { namespace overlay { namespace gpu {

constexpr uint32_t kProtocolVersion = 9;   // 9: kMsgGuard / kMsgPageOk (the page guard: the helper stays hidden after a guard plane-off until the plugin confirms a page, gpu/guard_state.h); a pair of another version refuses the hello, so a helper never waits for a confirmation that its plugin cannot send. 8: kOpCellBegin / kOpCellEnd (cached cells: the helper keeps what the static parts of a scene drew in layers and redraws only what changed). 7: the shared block carries the helper's render statistics (RenderBlock: the time of every picture in a histogram and the sums of its parts). 6: the frame (kMaxOps 2048, kMaxVertices 65536 for the polylines of a scene together, kMaxCurve 2048 per polyline, a text pool of kMaxText bytes); only the used part of the scene is copied; kOpStyle (feather, round caps), kOpPaint (a gradient of four colours along x, y or an arc), kOpRRect, kOpBand, kOpFeedback, kOpGlowBegin / kOpGlowEnd; kOpText is a string from the pool in the proportional signed distance field font (sdf_font.h), any size; kOpStroke / kOpLine / kOpArc / kOpDisc / kOpVLine take their feather and caps from the style, their colour from the paint. 5: kMaxOps 640, kOpLine, kOpArc, kOpFrame. 4: kMaxOps 96, kMaxCurve 1088, kOpCurve (a polyline with its own span), kOpText, kKindPlay. 2: SceneOp::rgba2 (was f[0] .. f[5], now f[0] .. f[4]), kOpFillGradient, kKindSpectrum. 3: the hello carries the vblank pipe, the pace stride and the measured period (flag kFlagVblank), the shared block carries the helper's presentation statistics (PaceBlock), the display may be rotated 0 / 90 / 180 / 270. The plugin and the helper are shipped together; a helper of another version refuses the hello (kMsgError) and the plugin draws with the CPU.
constexpr uint32_t kBlockMagic = 0x53424f56u;   // 'SBOV'
constexpr uint32_t kMsgMagic = 0x4d534f56u;     // 'MSOV' ... the first word of every message
constexpr int kMaxVertices = 65536;              // vertices of all polylines of a scene together (a pool of floats: the rows of the curves)
constexpr int kMaxCurve = 2048;                  // vertices of ONE polyline (also the size of the scratch arrays of the views)
constexpr int kMaxOps = 2048;
constexpr int kMaxText = 8192;                   // bytes of all texts of a scene together (UTF-8)
constexpr int kMaxLedger = 8;

/** The span of vertices of a Curve op: `firstF` and `nF` are floats of a shared block (written by the plugin, read by the helper: never trusted). true: first (>= 0) and n (>= 2) are whole
    numbers (rounded) with n <= kMaxCurve and first + n <= kMaxVertices; false for NaN, infinity, a negative start, fewer than two vertices and anything that reaches past the pool or does not fit an int. */
inline bool curveSpan (float firstF, float nF, int& first, int& n) noexcept
{
    if (! std::isfinite (firstF) || ! std::isfinite (nF) || firstF < -0.5f || nF < 1.5f || firstF > static_cast<float> (kMaxVertices) || nF > static_cast<float> (kMaxCurve))
        return false;
    const int f = static_cast<int> (firstF + 0.5f), c = static_cast<int> (nF + 0.5f);
    if (f < 0 || c < 2 || c > kMaxCurve || f + c > kMaxVertices)
        return false;
    first = f;
    n = c;
    return true;
}

enum SceneKind : uint32_t { kKindNone = 0, kKindFilter = 1, kKindLfo = 2, kKindOsc = 3, kKindSpectrum = 5, kKindPlay = 6, kKindUi = 7 };

/** The display list. Drawn in order, source over, onto the cleared canvas. "Op alpha" is the alpha byte of `rgba`.
    Clear   rgba: the whole canvas is set to this colour (replaces everything; alpha 0 = transparent)
    Rect    f[0..3] = x0 y0 x1 y1 (edges, pixel aligned in practice), rgba: a solid rectangle
    VLine   f[0..3] = x (centre) y0 y1 width: a vertical line, anti-aliased on its two sides
    Fill    f[0] = baseline row: the area between the polyline and the baseline (a baseline at or below the canvas height fills down to the bottom)
    Stroke  f[0] = half width (any positive fraction: 0.25 is a hairline): the polyline as an anti-aliased line of 2 * half width pixels; the feather is the style's
    Disc    f[0..2] = cx cy radius: an anti-aliased disc
    FillGradient  f[0] = baseline row (as Fill), f[1] = row yA, f[2] = row yB: the same area as Fill, coloured by a vertical gradient: `rgba` at row yA, `rgba2` at row yB, linear
            in between and constant outside (premultiplied colours are interpolated; evaluated at the centre of every pixel row)
    Curve   f[0] = first vertex, f[1] = number of vertices n (>= 2), f[2] = x0, f[3] = width W: from here on the polyline is the vertices y[first .. first + n - 1], point i at
            x = x0 + i / (n - 1) * (W - 1) + 0.5, the first and the last continue horizontally to x0 and x0 + W (a Fill or Stroke of this curve is limited to those columns)
    Text    f[0..1] = x and the BASELINE row (pixels, any fraction), f[2] = size (pixels per em, 4 .. 512, any fraction), f[3] = offset of the first byte in the text pool, f[4] = number of bytes
            (UTF-8), rgba2 = the tracking (extra advance after every glyph, em, the bits of a float; 0 = none): the string in the proportional font of sdf_font.h, glyph quads from the atlas,
            coverage clamp (0.5 + d * size / kEm, 0, 1) from the distance d of the atlas; a code point without a glyph is drawn as '?'
    Line    f[0..3] = x0 y0 x1 y1 (the end points, canvas pixels), f[4] = width: a straight segment at any angle, anti-aliased on its two long sides; butt ends exactly at the end points
            (a pixel is drawn when its centre projects onto the segment, 0 <= t <= length, with the coverage of the feathered line) or, with the round cap style, ends that are half discs
    Arc     f[0..2] = cx cy radius, f[3] = a0, f[4] = a1 (radians), the line width as the bits of a float in rgba2 (arcWidth): a ring segment, anti-aliased on its two circular sides. The angle
            runs CLOCKWISE on the screen (y is down): the point of angle a is (cx + radius cos a, cy + radius sin a); the ring is drawn from a0 to a1 (a0 < a1; a sweep of 2 pi or more is the
            whole ring); the ends are flat and cut along the radius (a pixel is inside when its centre is, no feathering at the cut) or, with the round cap style, half discs on the ring's line
    Frame   f[0..3] = x0 y0 x1 y1 (outer edges), f[4] = thickness (whole pixels, at least 1): the outline of a rectangle, inside the outer edges, four solid rectangles
    Style   f[0] = feather in pixels (0 .. kMaxSoft; below 1 it is 1: the plain 1 pixel anti-aliasing; above 1 the coverage ramps over that many pixels with a smooth step: the soft edge
            of a glow), f[1] = round caps (0 or 1). The state stays until the next Style op (the scene starts with feather 1, butt caps). Used by Stroke, Line, Arc, Disc, VLine, RRect.
    Paint   mode f[4] (0 = none: every op takes its own colour; 1 = along x, 2 = along y, 3 = along the angle of the Arc op that is drawn), the four colours of the gradient (equal spaced
            stops at 0, 1/3, 2/3, 1): rgba, rgba2, the bits of f[0], f[1] (straight 0xRRGGBBAA, premultiplied colours are interpolated), the range f[2] .. f[3] (columns / rows; mode 3 ignores it).
            While a paint is on, the following Fill, Stroke, Line, Arc, Disc, RRect, Band and Text ops take their colour from it and the ALPHA of their own rgba as a factor (a per curve
            alpha); the op's own rgb is ignored. The state stays until the next Paint op.
    RRect   f[0..3] = x0 y0 x1 y1, f[4] = corner radius (cut to half of the shorter side), rgba2 = the bits of a float: 0 = a filled rounded rectangle, w > 0 = its outline of width w inside
            the edges. The feather of the style softens the edge (a shadow / glow of a box).
    Band    f[0] = first vertex of the upper rows A, f[1] = n, f[2] = x0, f[3] = width (as Curve), f[4] = first vertex of the lower rows B (same n): the area between two polylines, per pixel
            row the exact coverage of the strip [A, B] (a band thinner than a pixel is a faint line): the min / max picture of a dense wave. Not limited by the current Curve op.
    Feedback f[0..3] = x0 y0 x1 y1, f[4] = gain 0 .. 1: the previous presented picture of this region, times gain, source over (the afterglow when the scene draws an opaque ground first).
    Clip    f[0..3] = x0 y0 x1 y1 (canvas pixels): everything drawn after this op is limited to the rectangle (the pixels outside keep what they had: with a transparent Clear first, they stay transparent) until the
            next Clip op; an empty rectangle (x1 <= x0 or y1 <= y0) switches the clip off. The glow layers and the passes of a bloom are not clipped.
    GlowBegin f[0] = divisor 2, 4 or 8: the next ops go to a layer of 1 / divisor the size (cleared, transparent).
    CellBegin f[0..3] = x0 y0 x1 y1 (the canvas rectangle that contains everything the cell draws), rgba = the hash of the content (SceneBuilder::cellEnd makes it from the ops, the text and the rows of the curves inside; the
            text offset and the pool position of a curve are not part of it). CellEnd closes the cell. A CELL is a part of the picture that is the same from one scene to the next while its hash is the same: the helper draws the
            cells of a scene into LAYERS (textures the size of the picture) and, when the next scene has the same cells, only the cells whose hash or rectangle changed are drawn again (scissor = their rectangle, plus the
            cells that lie over it), then one textured quad per layer puts the layer on the picture. Rules: no nesting (the builder counts a nested begin / end and keeps only the outer pair); a cell starts and ends with
            the default style and paint (the ops inside must not rely on a style, paint, clip or curve set before the cell, and set none that lasts after it); inside a cell no Clear, Clip or Feedback op (a GlowBegin / GlowEnd pair is allowed: its bloom is added into the layer); the
            picture, the bloom rectangle included, must stay inside the rectangle. The ops that are NOT in a cell are drawn every picture, in their order: a cell that comes after one of them is on a second layer (over them); a cell after a second group of
            such ops makes the whole scene uncached (drawn whole). The CPU reference draws the cells as they are (the begin / end only reset the style and the paint).
    GlowEnd rgba = tint, f[0] = gain (0 .. 4), f[1] = blur width (1 = the default, 0.5 .. 4), f[2], f[3] = x0, y0 and rgba2 = (x1 << 16) | y1 (whole pixels, below 65536): the rectangle that is blurred and added
            over the picture (an empty rectangle: the whole canvas; a rectangle is much cheaper: the passes are as big as it is). A bloom: the same curves drawn into the layer.
    The polyline: without a Curve op, SceneData::y[0 .. numPoints - 1] are rows, point i at x = i / (numPoints - 1) * (w - 1) + 0.5 (the first and the last continue horizontally to the canvas edges). */
enum OpCode : uint32_t { kOpNone = 0, kOpClear = 1, kOpRect = 2, kOpVLine = 3, kOpFill = 4, kOpStroke = 5, kOpDisc = 6, kOpFillGradient = 7, kOpCurve = 8, kOpText = 9, kOpLine = 10, kOpArc = 11, kOpFrame = 12,
                         kOpStyle = 13, kOpPaint = 14, kOpRRect = 15, kOpBand = 16, kOpFeedback = 17, kOpGlowBegin = 18, kOpGlowEnd = 19, kOpClip = 20, kOpCellBegin = 21, kOpCellEnd = 22 };
constexpr float kMaxSoft = 64.0f;

// The GPU's ordinary two-pass blur, drawn over opaque black, also supplies
// the frozen popup backdrop. Mark this use so the CPU fallback renders it too.
// XL popup measurements: docs/dsp-knowledge/mpc-display.md.
constexpr uint32_t kPopupBackdrop = 1; // GlowBegin rgba; unused by the GPU shader
constexpr int kBackdropRadius = 4;
constexpr float kBackdropBrightness = 0.2f;

struct SceneOp
{
    uint32_t op;
    uint32_t rgba;
    uint32_t rgba2;   // the second colour of a gradient, the tracking of a text, the width of an arc or a rounded rectangle outline (0 for the other ops)
    float f[5];
};
static_assert (sizeof (SceneOp) == 32, "SceneOp layout");

struct SceneData
{
    uint32_t kind;        // SceneKind (for the status and the tests; the helper draws from the ops alone)
    uint32_t viewIndex;   // the index in overlay_layout.h
    int32_t w, h;         // the canvas = the region in landscape UI pixels
    uint32_t numOps;
    uint32_t numPoints;   // vertices used in y
    uint32_t numText;     // bytes used in text
    uint32_t numDefault;  // vertices of the scene's own polyline (SceneBuilder::polyline): the first numDefault values of y
    SceneOp ops[kMaxOps];
    char text[kMaxText];
    float y[kMaxVertices];
};
static_assert (sizeof (SceneData) == 32 + 32 * kMaxOps + kMaxText + 4 * kMaxVertices, "SceneData layout");
constexpr int kSceneWords = static_cast<int> (sizeof (SceneData) / 4);

inline uint32_t rgba8 (uint32_t r, uint32_t g, uint32_t b, uint32_t a) noexcept { return (r << 24) | (g << 16) | (b << 8) | a; }
inline uint32_t rgba8FromRgb (uint32_t rgb, uint32_t a) noexcept { return ((rgb & 0xffffffu) << 8) | a; }
inline uint32_t floatBits (float v) noexcept { uint32_t b; std::memcpy (&b, &v, 4); return b; }
inline float bitsFloat (uint32_t b) noexcept { float v; std::memcpy (&v, &b, 4); return v; }

/** Builds a scene: the helpers keep the count within the limits (an op beyond the limit is dropped and `overflow` is set). Only the header is cleared (the arrays are written before they are read). */
struct SceneBuilder
{
    explicit SceneBuilder (SceneData& s) : s_ (s) { std::memset (&s_, 0, offsetof (SceneData, ops)); }
    void begin (SceneKind kind, uint32_t viewIndex, int w, int h) { s_.kind = kind; s_.viewIndex = viewIndex; s_.w = w; s_.h = h; }
    SceneOp* add (OpCode op, uint32_t rgba)
    {
        if (s_.numOps >= static_cast<uint32_t> (kMaxOps)) { overflow = true; return nullptr; }
        SceneOp& o = s_.ops[s_.numOps++];
        std::memset (&o, 0, sizeof o);
        o.op = op;
        o.rgba = rgba;
        return &o;
    }
    void clear (uint32_t rgba) { add (kOpClear, rgba); }
    void rect (float x0, float y0, float x1, float y1, uint32_t rgba) { if (SceneOp* o = add (kOpRect, rgba)) { o->f[0] = x0; o->f[1] = y0; o->f[2] = x1; o->f[3] = y1; } }
    void vline (float x, float y0, float y1, float width, uint32_t rgba) { if (SceneOp* o = add (kOpVLine, rgba)) { o->f[0] = x; o->f[1] = y0; o->f[2] = y1; o->f[3] = width; } }
    void line (float x0, float y0, float x1, float y1, float width, uint32_t rgba) { if (SceneOp* o = add (kOpLine, rgba)) { o->f[0] = x0; o->f[1] = y0; o->f[2] = x1; o->f[3] = y1; o->f[4] = width; } }
    /** A ring segment from angle a0 to a1 (radians, clockwise on the screen, see the Arc op); a0 > a1 are swapped. */
    void arc (float cx, float cy, float radius, float a0, float a1, float width, uint32_t rgba)
    {
        if (a0 > a1) { const float t = a0; a0 = a1; a1 = t; }
        if (SceneOp* o = add (kOpArc, rgba)) { o->f[0] = cx; o->f[1] = cy; o->f[2] = radius; o->f[3] = a0; o->f[4] = a1; std::memcpy (&o->rgba2, &width, 4); }
    }
    void frame (float x0, float y0, float x1, float y1, float thickness, uint32_t rgba) { if (SceneOp* o = add (kOpFrame, rgba)) { o->f[0] = x0; o->f[1] = y0; o->f[2] = x1; o->f[3] = y1; o->f[4] = thickness; } }
    void fill (float baseRow, uint32_t rgba) { if (SceneOp* o = add (kOpFill, rgba)) o->f[0] = baseRow; }
    void fillGradient (float baseRow, float yA, float yB, uint32_t rgbaA, uint32_t rgbaB) { if (SceneOp* o = add (kOpFillGradient, rgbaA)) { o->rgba2 = rgbaB; o->f[0] = baseRow; o->f[1] = yA; o->f[2] = yB; } }
    void stroke (float halfWidth, uint32_t rgba) { if (SceneOp* o = add (kOpStroke, rgba)) o->f[0] = halfWidth; }
    void disc (float cx, float cy, float r, uint32_t rgba) { if (SceneOp* o = add (kOpDisc, rgba)) { o->f[0] = cx; o->f[1] = cy; o->f[2] = r; } }
    /** The state ops: the feather (pixels) and the caps of the following line-like ops; the paint (a gradient of four colours along x / y / the arc angle; the ranges are columns / rows). */
    void style (float soft, bool roundCaps) { if (SceneOp* o = add (kOpStyle, 0)) { o->f[0] = soft; o->f[1] = roundCaps ? 1.0f : 0.0f; } }
    void paint (int mode, float a, float b, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
    {
        if (SceneOp* o = add (kOpPaint, c0)) { o->rgba2 = c1; o->f[0] = bitsFloat (c2); o->f[1] = bitsFloat (c3); o->f[2] = a; o->f[3] = b; o->f[4] = static_cast<float> (mode); }
    }
    void paintX (float x0, float x1, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) { paint (1, x0, x1, c0, c1, c2, c3); }
    void paintY (float y0, float y1, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) { paint (2, y0, y1, c0, c1, c2, c3); }
    void paintArc (uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) { paint (3, 0.0f, 1.0f, c0, c1, c2, c3); }
    void paintOff() { paint (0, 0.0f, 1.0f, 0, 0, 0, 0); }
    /** A rounded rectangle: filled (width 0) or its outline of `width` inside the edges. */
    void rrect (float x0, float y0, float x1, float y1, float radius, uint32_t rgba, float outlineWidth = 0.0f)
    {
        if (SceneOp* o = add (kOpRRect, rgba)) { o->f[0] = x0; o->f[1] = y0; o->f[2] = x1; o->f[3] = y1; o->f[4] = radius; o->rgba2 = floatBits (outlineWidth); }
    }
    /** Limits the following ops to the rectangle (see the Clip op); `clipOff ()` switches it off. */
    void clip (float x0, float y0, float x1, float y1) { clip ({ x0, y0, x1, y1 }); }
    void clip (const std::array<float, 4>& bounds)
    {
        if (SceneOp* o = add (kOpClip, 0))
        {
            clip_ = bounds;
            std::copy (bounds.begin(), bounds.end(), o->f);
        }
    }
    void clipOff() { clip ({}); }
    const std::array<float, 4>& clipBounds() const noexcept { return clip_; }
    void feedback (float x0, float y0, float x1, float y1, float gain) { if (SceneOp* o = add (kOpFeedback, 0)) { o->f[0] = x0; o->f[1] = y0; o->f[2] = x1; o->f[3] = y1; o->f[4] = gain; } }
    void backdropBegin()
    {
        if (SceneOp* o = add (kOpGlowBegin, kPopupBackdrop))
            o->f[0] = 2; // half-size target, 9-tap separable blur
    }
    void backdropEnd (int x0, int y0, int x1, int y1)
    {
        glowEnd (kBackdropBrightness, 0.6f, 0xffffffffu, x0, y0, x1, y1);
    }
    void glowBegin (int divisor) { if (SceneOp* o = add (kOpGlowBegin, 0)) o->f[0] = static_cast<float> (divisor); }
    void glowEnd (float gain, float blur, uint32_t tint = 0xffffffffu, int x0 = 0, int y0 = 0, int x1 = 0, int y1 = 0)
    {
        if (SceneOp* o = add (kOpGlowEnd, tint))
        {
            o->f[0] = gain; o->f[1] = blur; o->f[2] = static_cast<float> (x0); o->f[3] = static_cast<float> (y0);
            o->rgba2 = (static_cast<uint32_t> (std::min (std::max (x1, 0), 65535)) << 16) | static_cast<uint32_t> (std::min (std::max (y1, 0), 65535));
        }
    }
    /** The scene's one polyline over the whole width (n <= kMaxCurve; more is a programming error: it is cut and `overflow` is set). It occupies the first vertices of the pool: call it before
        any curve (a Curve op does not use it). */
    void polyline (const float* rows, int n)
    {
        if (n > kMaxCurve) { overflow = true; n = kMaxCurve; }
        if (n < 0) n = 0;
        for (int i = 0; i < n; ++i)
            s_.y[i] = rows[i];
        s_.numPoints = static_cast<uint32_t> (n);
        s_.numDefault = static_cast<uint32_t> (n);
    }
    /** Appends the rows to the pool; returns the index of the first (-1: no room, `overflow` is set). */
    int addRows (const float* rows, int n)
    {
        if (n < 2 || n > kMaxCurve || s_.numPoints + static_cast<uint32_t> (n) > static_cast<uint32_t> (kMaxVertices)) { overflow = true; return -1; }
        const int first = static_cast<int> (s_.numPoints);
        std::memcpy (s_.y + first, rows, sizeof (float) * static_cast<size_t> (n));
        s_.numPoints += static_cast<uint32_t> (n);
        return first;
    }
    /** Appends the rows as another polyline and makes it the current one (kOpCurve) over the columns x0 .. x0 + width - 1; the following Fill and Stroke ops draw it. */
    int curve (const float* rows, int n, int x0, int width)
    {
        const int first = addRows (rows, n);
        if (first < 0)
            return -1;
        curveAt (first, n, x0, width);
        return first;
    }
    /** Makes rows that are in the pool already (the return value of curve ()) the current polyline again, over another span: the same vertices drawn a second time (a glow pass, a shifted copy) cost no new vertices. */
    void curveAt (int first, int n, int x0, int width)
    {
        if (first < 0 || n < 2 || first + n > static_cast<int> (s_.numPoints)) { overflow = true; return; }
        if (SceneOp* o = add (kOpCurve, 0)) { o->f[0] = static_cast<float> (first); o->f[1] = static_cast<float> (n); o->f[2] = static_cast<float> (x0); o->f[3] = static_cast<float> (width); }
    }
    /** A rectangle with a vertical gradient (top colour at y0, bottom colour at y1): a flat two point curve and a gradient fill under it, the cheapest fill with a gradient (no distance field). It makes the
        flat curve the current polyline. */
    void gradientRect (float x0, float y0, float x1, float y1, uint32_t rgbaTop, uint32_t rgbaBottom)
    {
        const float rows[2] = { y0 - 0.001f, y0 - 0.001f };   // a hair above the edge: the top row is covered whole (and the row above not visibly)
        if (curve (rows, 2, static_cast<int> (x0), static_cast<int> (x1 - x0)) >= 0)
            fillGradient (y1, y0, y1, rgbaTop, rgbaBottom);
    }
    /** The area between the rows `top` and `bottom` (both n values) over the columns x0 .. x0 + width - 1. */
    void band (const float* top, const float* bottom, int n, int x0, int width, uint32_t rgba)
    {
        if (n < 2 || n > kMaxCurve || s_.numPoints + 2u * static_cast<uint32_t> (n) > static_cast<uint32_t> (kMaxVertices)) { overflow = true; return; }
        const int a = addRows (top, n), b = addRows (bottom, n);
        if (SceneOp* o = add (kOpBand, rgba)) { o->f[0] = static_cast<float> (a); o->f[1] = static_cast<float> (n); o->f[2] = static_cast<float> (x0); o->f[3] = static_cast<float> (width); o->f[4] = static_cast<float> (b); }
    }
    /** A string at (x, baseline) in `size` pixels per em (UTF-8, any length the pool holds; `len` < 0: up to the zero byte). Returns false (and sets `overflow`) when the pool or the op list is full. */
    bool text (float x, float baseline, float size, const char* str, uint32_t rgba, float tracking = 0.0f, int len = -1)
    {
        if (len < 0)
            len = static_cast<int> (std::strlen (str));
        if (len == 0)
            return true;
        if (s_.numText + static_cast<uint32_t> (len) > static_cast<uint32_t> (kMaxText)) { overflow = true; return false; }
        SceneOp* o = add (kOpText, rgba);
        if (! o)
            return false;
        std::memcpy (s_.text + s_.numText, str, static_cast<size_t> (len));
        o->f[0] = x; o->f[1] = baseline; o->f[2] = size; o->f[3] = static_cast<float> (s_.numText); o->f[4] = static_cast<float> (len); o->rgba2 = floatBits (tracking);
        s_.numText += static_cast<uint32_t> (len);
        return true;
    }
    /** A cached cell (see the CellBegin op): everything drawn until the matching cellEnd () is one cell with the rectangle given here. Nested pairs count and are ignored. A cell without ops is dropped. */
    void cellBegin (float x0, float y0, float x1, float y1)
    {
        if (cellDepth_++ > 0)
            return;
        cellOp_ = s_.numOps;
        if (SceneOp* o = add (kOpCellBegin, 0)) { o->f[0] = x0; o->f[1] = y0; o->f[2] = x1; o->f[3] = y1; }
        else cellOp_ = ~0u;
    }
    void cellEnd ()
    {
        if (cellDepth_ == 0 || --cellDepth_ > 0)
            return;
        if (cellOp_ == ~0u)
            return;
        if (s_.numOps == cellOp_ + 1)   // nothing inside: the begin op is taken back
        {
            --s_.numOps;
            return;
        }
        if (add (kOpCellEnd, 0) != nullptr)
            s_.ops[cellOp_].rgba = cellHash (s_, cellOp_);
        else
            overflow = true;
    }
    /** The hash of the cell that begins at op `begin` and ends at the next CellEnd op (or at numOps): its rectangle, its ops, the text and the rows of the curves they use (not the offsets into the pools). */
    static uint32_t cellHash (const SceneData& s, uint32_t begin) noexcept
    {
        uint32_t h = 2166136261u;
        auto mix = [&] (uint32_t v) { h = (h ^ (v & 255u)) * 16777619u; h = (h ^ ((v >> 8) & 255u)) * 16777619u; h = (h ^ ((v >> 16) & 255u)) * 16777619u; h = (h ^ (v >> 24)) * 16777619u; };
        for (uint32_t k = begin; k < s.numOps && k < static_cast<uint32_t> (kMaxOps); ++k)
        {
            const SceneOp& o = s.ops[k];
            if (k > begin && o.op == kOpCellEnd)
                break;
            mix (o.op); mix (o.rgba); mix (o.rgba2);
            const bool isText = o.op == kOpText, isCurve = o.op == kOpCurve, isBand = o.op == kOpBand;
            for (int i = 0; i < 5; ++i)
            {
                if ((isText && i == 3) || (isCurve && i == 0) || (isBand && (i == 0 || i == 4)))
                    continue;   // positions in the pools: not part of the content
                mix (floatBits (o.f[i]));
            }
            if (k == begin)
                continue;
            if (isText)
            {
                int off = static_cast<int> (o.f[3] + 0.5f), len = static_cast<int> (o.f[4] + 0.5f);
                if (off >= 0 && len > 0 && off + len <= static_cast<int> (s.numText))
                    for (int i = 0; i < len; ++i)
                        h = (h ^ static_cast<uint8_t> (s.text[off + i])) * 16777619u;
            }
            else if (isCurve || isBand)
            {
                const int n = static_cast<int> (o.f[1] + 0.5f);
                const int firsts[2] = { static_cast<int> (o.f[0] + 0.5f), isBand ? static_cast<int> (o.f[4] + 0.5f) : -1 };
                for (int f : firsts)
                    if (f >= 0 && n > 0 && f + n <= static_cast<int> (s.numPoints))
                        for (int i = 0; i < n; ++i)
                            mix (floatBits (s.y[f + i]));
            }
        }
        return h == 0 ? 1u : h;
    }
    uint32_t numOps () const noexcept { return s_.numOps; }
    bool overflow = false;

private:
    SceneData& s_;
    std::array<float, 4> clip_ {};
    int cellDepth_ = 0;
    uint32_t cellOp_ = ~0u;
};

/** Draw within an inner rectangle and restore any outer clipping on exit. */
class ScopedClip
{
public:
    ScopedClip (SceneBuilder& scene, float x0, float y0, float x1, float y1)
        : scene_ (scene), outer_ (scene.clipBounds())
    {
        if (outer_[0] < outer_[2] && outer_[1] < outer_[3])
        {
            x0 = std::max (x0, outer_[0]); y0 = std::max (y0, outer_[1]);
            x1 = std::min (x1, outer_[2]); y1 = std::min (y1, outer_[3]);
        }
        visible_ = x0 < x1 && y0 < y1;
        if (visible_)
            scene_.clip (x0, y0, x1, y1);
    }
    ~ScopedClip() { if (visible_) scene_.clip (outer_); }
    ScopedClip (const ScopedClip&) = delete;
    ScopedClip& operator= (const ScopedClip&) = delete;
    explicit operator bool() const noexcept { return visible_; }

private:
    SceneBuilder& scene_;
    std::array<float, 4> outer_;
    bool visible_ = false;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The geometry of the ops. The floats of a scene come from shared memory (the helper never trusts them): these functions are the ONE place that decides whether an op
// is drawable and with which numbers; the helper and the CPU reference both use them. false: the op is not drawn (NaN, infinity, a coordinate beyond +- kMaxCoord, a width or a
// radius outside its limit, an empty sweep).
constexpr float kMaxCoord = 32768.0f;    // a coordinate of an op (the canvas is a few thousand pixels)
constexpr float kMaxStroke = 1024.0f;    // the width of a line or an arc, the thickness of a frame
constexpr float kMaxRadius = 8192.0f;
constexpr float kMaxAngle = 1.0e4f;      // radians
constexpr float kMinText = 4.0f, kMaxTextSize = 512.0f;

inline bool coordOk (float v) noexcept { return std::isfinite (v) && v >= -kMaxCoord && v <= kMaxCoord; }

inline float arcWidth (const SceneOp& o) noexcept { return bitsFloat (o.rgba2); }
inline void setArcWidth (SceneOp& o, float width) noexcept { std::memcpy (&o.rgba2, &width, 4); }

/** The style state: the feather (>= 1) and the caps. */
struct StyleState { float soft = 1.0f; bool round = false; };
inline void applyStyle (const SceneOp& o, StyleState& st) noexcept
{
    const float s = o.f[0];
    st.soft = std::isfinite (s) ? std::min (std::max (s, 1.0f), kMaxSoft) : 1.0f;
    st.round = o.f[1] > 0.5f;
}
/** The coverage of a feathered edge: `x` = how far inside the edge the pixel centre is (half width - distance), `soft` >= 1 the feather in pixels. soft = 1 is clamp (0.5 + x, 0, 1). */
inline float edgeCoverage (float x, float soft) noexcept
{
    float c = 0.5f + x / soft;
    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    return soft > 1.0f ? c * c * (3.0f - 2.0f * c) : c;
}

/** The coverage of a line of half width `half` at the distance `dist` of the pixel centre from its axis: the feathered edge; a line thinner than a pixel is not wider but fainter (at most its width). */
inline float lineCoverage (float half, float dist, float soft) noexcept
{
    float c = edgeCoverage (half - dist, soft);
    if (soft <= 1.0f && c > 2.0f * half)
        c = 2.0f * half;
    return c;
}

/** The paint state: 0 none, 1 x, 2 y, 3 arc angle; four straight colours, the range. */
struct PaintState { int mode = 0; uint32_t c[4] = {}; float a = 0.0f, b = 1.0f; };
inline void applyPaint (const SceneOp& o, PaintState& p) noexcept
{
    const int mode = std::isfinite (o.f[4]) ? static_cast<int> (o.f[4] + 0.5f) : 0;
    p.mode = (mode >= 1 && mode <= 3) ? mode : 0;
    p.c[0] = o.rgba; p.c[1] = o.rgba2; p.c[2] = floatBits (o.f[0]); p.c[3] = floatBits (o.f[1]);
    p.a = coordOk (o.f[2]) ? o.f[2] : 0.0f;
    p.b = coordOk (o.f[3]) ? o.f[3] : 1.0f;
}
/** The premultiplied colour (r, g, b, a in 0 .. 1) of the paint at the position t (0 .. 1, clamped), times `alpha`. */
inline void paintColour (const PaintState& p, float t, float alpha, float out[4]) noexcept
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float s = t * 3.0f;
    int i = static_cast<int> (s);
    if (i > 2) i = 2;
    const float u = s - static_cast<float> (i);
    float c[2][4];
    for (int k = 0; k < 2; ++k)
    {
        const uint32_t v = p.c[i + k];
        const float a = static_cast<float> (v & 255u) / 255.0f;
        c[k][0] = static_cast<float> ((v >> 24) & 255u) / 255.0f * a; c[k][1] = static_cast<float> ((v >> 16) & 255u) / 255.0f * a; c[k][2] = static_cast<float> ((v >> 8) & 255u) / 255.0f * a; c[k][3] = a;
    }
    for (int k = 0; k < 4; ++k)
        out[k] = (c[0][k] + (c[1][k] - c[0][k]) * u) * alpha;
}

struct LineGeom { float x0, y0, x1, y1, half; };
inline bool lineGeom (const SceneOp& o, LineGeom& g) noexcept
{
    for (int i = 0; i < 4; ++i)
        if (! coordOk (o.f[i]))
            return false;
    if (! std::isfinite (o.f[4]) || o.f[4] <= 0.0f || o.f[4] > kMaxStroke)
        return false;
    g.x0 = o.f[0]; g.y0 = o.f[1]; g.x1 = o.f[2]; g.y1 = o.f[3]; g.half = o.f[4] * 0.5f;
    const float dx = g.x1 - g.x0, dy = g.y1 - g.y0;
    return dx * dx + dy * dy > 1.0e-6f;    // a point has no direction
}

/** The ring segment in the form both drawing sides use: centre, radius, half width, the unit directions of the two ends and `mode`: 0 = the sweep is at most pi, 1 = more than pi, 2 = the whole ring; a0 and the sweep (radians). */
struct ArcGeom { float cx, cy, r, half, u0x, u0y, u1x, u1y; int mode; float a0, sweep; };
inline bool arcGeom (const SceneOp& o, ArcGeom& g) noexcept
{
    const float width = arcWidth (o);
    if (! coordOk (o.f[0]) || ! coordOk (o.f[1]) || ! std::isfinite (o.f[2]) || o.f[2] <= 0.0f || o.f[2] > kMaxRadius)
        return false;
    if (! std::isfinite (o.f[3]) || ! std::isfinite (o.f[4]) || std::fabs (o.f[3]) > kMaxAngle || std::fabs (o.f[4]) > kMaxAngle)
        return false;
    if (! std::isfinite (width) || width <= 0.0f || width > kMaxStroke)
        return false;
    const double a0 = static_cast<double> (o.f[3]), sweep = static_cast<double> (o.f[4]) - a0;
    if (sweep <= 0.0)
        return false;
    constexpr double kPi = 3.14159265358979323846;
    g.cx = o.f[0]; g.cy = o.f[1]; g.r = o.f[2]; g.half = width * 0.5f;
    g.u0x = static_cast<float> (std::cos (a0)); g.u0y = static_cast<float> (std::sin (a0));
    g.u1x = static_cast<float> (std::cos (static_cast<double> (o.f[4]))); g.u1y = static_cast<float> (std::sin (static_cast<double> (o.f[4])));
    g.mode = sweep >= 2.0 * kPi - 1.0e-6 ? 2 : (sweep > kPi ? 1 : 0);
    g.a0 = o.f[3];
    g.sweep = static_cast<float> (sweep);
    return true;
}
/** Is the offset (dx, dy) from the centre inside the angular range of the arc (the ends belong to it)? Exactly the test of the helper's shader. */
inline bool arcContains (const ArcGeom& g, float dx, float dy) noexcept
{
    if (g.mode == 2)
        return true;
    if (g.mode == 0)
        return g.u0x * dy - g.u0y * dx >= 0.0f && dx * g.u1y - dy * g.u1x >= 0.0f;
    return ! (g.u1x * dy - g.u1y * dx > 0.0f && dx * g.u0y - dy * g.u0x > 0.0f);   // not in the other, shorter, part of the circle
}
/** The position 0 .. 1 of the angle of the offset (dx, dy) inside the sweep of the arc (for the paint of mode 3). */
inline float arcParam (const ArcGeom& g, float dx, float dy) noexcept
{
    constexpr float kTwoPi = 6.28318530717958647692f;
    float d = std::atan2 (dy, dx) - g.a0;
    d -= kTwoPi * std::floor (d / kTwoPi);
    return g.sweep > 0.0f ? d / g.sweep : 0.0f;
}

/** A frame: whole pixel edges and thickness, 1 <= t, the rectangle not empty. */
struct FrameGeom { int x0, y0, x1, y1, t; };
inline bool frameGeom (const SceneOp& o, FrameGeom& g) noexcept
{
    for (int i = 0; i < 4; ++i)
        if (! coordOk (o.f[i]))
            return false;
    if (! std::isfinite (o.f[4]) || o.f[4] < 0.5f || o.f[4] > kMaxStroke)
        return false;
    g.x0 = static_cast<int> (o.f[0]); g.y0 = static_cast<int> (o.f[1]); g.x1 = static_cast<int> (o.f[2]); g.y1 = static_cast<int> (o.f[3]);
    g.t = static_cast<int> (o.f[4] + 0.5f);
    return g.x1 > g.x0 && g.y1 > g.y0;
}
/** The (up to four) solid rectangles of a frame: calls plot (x0, y0, x1, y1). Thicker than half the rectangle: it is solid. */
template <class Plot>
void frameRects (const FrameGeom& g, Plot plot)
{
    const int t = g.t;
    if (2 * t >= g.x1 - g.x0 || 2 * t >= g.y1 - g.y0)
    {
        plot (g.x0, g.y0, g.x1, g.y1);
        return;
    }
    plot (g.x0, g.y0, g.x1, g.y0 + t);
    plot (g.x0, g.y1 - t, g.x1, g.y1);
    plot (g.x0, g.y0 + t, g.x0 + t, g.y1 - t);
    plot (g.x1 - t, g.y0 + t, g.x1, g.y1 - t);
}

/** A rounded rectangle: the edges (floats), the radius cut to half of the shorter side, the outline width (0 = filled). `distance` is the signed distance of a point to the outline (negative inside). */
struct RRectGeom
{
    float x0, y0, x1, y1, r, width;
    float cx() const noexcept { return 0.5f * (x0 + x1); }
    float cy() const noexcept { return 0.5f * (y0 + y1); }
    float distance (float px, float py) const noexcept
    {
        const float hx = 0.5f * (x1 - x0), hy = 0.5f * (y1 - y0);
        const float qx = std::fabs (px - cx()) - (hx - r), qy = std::fabs (py - cy()) - (hy - r);
        const float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
        const float inner = qx > qy ? qx : qy;
        return std::sqrt (ox * ox + oy * oy) + (inner < 0.0f ? inner : 0.0f) - r;
    }
};
inline bool rrectGeom (const SceneOp& o, RRectGeom& g) noexcept
{
    for (int i = 0; i < 4; ++i)
        if (! coordOk (o.f[i]))
            return false;
    const float w = bitsFloat (o.rgba2);
    if (! std::isfinite (o.f[4]) || o.f[4] < 0.0f || o.f[4] > kMaxRadius || ! std::isfinite (w) || w < 0.0f || w > kMaxStroke)
        return false;
    g.x0 = o.f[0]; g.y0 = o.f[1]; g.x1 = o.f[2]; g.y1 = o.f[3];
    if (! (g.x1 > g.x0 && g.y1 > g.y0))
        return false;
    g.r = std::min (o.f[4], 0.5f * std::min (g.x1 - g.x0, g.y1 - g.y0));
    g.width = w;
    return true;
}
/** The coverage of a rounded rectangle at the point (px, py), feather `soft`: the fill is edgeCoverage (-distance), the outline the fill minus the same coverage of the inner edge. */
inline float rrectCoverage (const RRectGeom& g, float px, float py, float soft) noexcept
{
    const float d = g.distance (px, py);
    const float outer = edgeCoverage (-d, soft);
    if (g.width <= 0.0f)
        return outer;
    const float inner = edgeCoverage (-d - g.width, soft);
    return outer > inner ? outer - inner : 0.0f;
}

/** A band: two spans of the pool (top rows, bottom rows) over the columns x0 .. x0 + width. */
struct BandGeom { int firstA, firstB, n; float x0, width; };
inline bool bandGeom (const SceneOp& o, BandGeom& g) noexcept
{
    int a = 0, b = 0, n = 0, n2 = 0;
    if (! curveSpan (o.f[0], o.f[1], a, n) || ! curveSpan (o.f[4], o.f[1], b, n2) || ! coordOk (o.f[2]) || ! std::isfinite (o.f[3]) || o.f[3] < 1.0f || o.f[3] > kMaxCoord)
        return false;
    g.firstA = a; g.firstB = b; g.n = n; g.x0 = o.f[2]; g.width = o.f[3];
    return true;
}
/** The exact coverage of the pixel row [row, row + 1) by the strip [top, bottom] (rows), any order of the two. */
inline float bandCoverage (float top, float bottom, int row) noexcept
{
    if (top > bottom) { const float t = top; top = bottom; bottom = t; }
    const float lo = std::max (top, static_cast<float> (row)), hi = std::min (bottom, static_cast<float> (row) + 1.0f);
    return hi > lo ? hi - lo : 0.0f;
}

/** The clip rectangle in whole pixels (outward rounded); `on` false for an empty rectangle (clip off). false return: the op is not valid (NaN, a coordinate beyond the limit). */
struct ClipGeom { int x0, y0, x1, y1; bool on; };
inline bool clipGeom (const SceneOp& o, ClipGeom& g) noexcept
{
    for (int i = 0; i < 4; ++i)
        if (! coordOk (o.f[i]))
            return false;
    g.x0 = static_cast<int> (std::floor (o.f[0])); g.y0 = static_cast<int> (std::floor (o.f[1])); g.x1 = static_cast<int> (std::ceil (o.f[2])); g.y1 = static_cast<int> (std::ceil (o.f[3]));
    g.on = g.x1 > g.x0 && g.y1 > g.y0;
    return true;
}

struct FeedbackGeom { float x0, y0, x1, y1, gain; };
inline bool feedbackGeom (const SceneOp& o, FeedbackGeom& g) noexcept
{
    for (int i = 0; i < 4; ++i)
        if (! coordOk (o.f[i]))
            return false;
    if (! std::isfinite (o.f[4]) || o.f[4] <= 0.0f)
        return false;
    g.x0 = o.f[0]; g.y0 = o.f[1]; g.x1 = o.f[2]; g.y1 = o.f[3]; g.gain = std::min (o.f[4], 1.0f);
    return g.x1 > g.x0 && g.y1 > g.y0;
}

struct GlowGeom { int divisor; float gain, blur; float x0, y0, x1, y1; bool rect; };
inline bool glowBeginGeom (const SceneOp& o, int& divisor) noexcept
{
    if (! std::isfinite (o.f[0]))
        return false;
    const int d = static_cast<int> (o.f[0] + 0.5f);
    if (d != 2 && d != 4 && d != 8)
        return false;
    divisor = d;
    return true;
}
inline bool glowEndGeom (const SceneOp& o, GlowGeom& g) noexcept
{
    if (! std::isfinite (o.f[0]) || ! std::isfinite (o.f[1]) || o.f[0] <= 0.0f)
        return false;
    g.divisor = 0;
    g.gain = std::min (o.f[0], 4.0f);
    g.blur = std::min (std::max (o.f[1], 0.5f), 4.0f);
    g.x0 = coordOk (o.f[2]) ? o.f[2] : 0.0f;
    g.y0 = coordOk (o.f[3]) ? o.f[3] : 0.0f;
    g.x1 = static_cast<float> (o.rgba2 >> 16);
    g.y1 = static_cast<float> (o.rgba2 & 0xffffu);
    g.rect = g.x1 > g.x0 && g.y1 > g.y0;
    return true;
}

/** The characters of a text op as a string (empty when the op is not drawable). */
inline std::string textString (const SceneData& s, const SceneOp& o);

/** A text op: where the bytes are, the size and the position. false: not drawn. */
struct TextGeom { float x, baseline, size, tracking; int offset, length; };
inline bool textGeom (const SceneOp& o, TextGeom& g) noexcept
{
    if (! coordOk (o.f[0]) || ! coordOk (o.f[1]) || ! std::isfinite (o.f[2]) || o.f[2] < kMinText || o.f[2] > kMaxTextSize || ! std::isfinite (o.f[3]) || ! std::isfinite (o.f[4]))
        return false;
    if (o.f[3] < -0.5f || o.f[4] < 0.5f || o.f[3] > static_cast<float> (kMaxText) || o.f[4] > static_cast<float> (kMaxText))
        return false;
    const float tr = bitsFloat (o.rgba2);
    g.offset = static_cast<int> (o.f[3] + 0.5f);
    g.length = static_cast<int> (o.f[4] + 0.5f);
    if (g.offset < 0 || g.length < 1 || g.offset + g.length > kMaxText)
        return false;
    g.x = o.f[0]; g.baseline = o.f[1]; g.size = o.f[2];
    g.tracking = (std::isfinite (tr) && std::fabs (tr) <= 4.0f) ? tr : 0.0f;
    return true;
}

/** The ledger: the GEM handles and framebuffer ids the helper has made on the shared display descriptor (PRIME import, ADDFB2). The helper writes an entry as soon as the object
    exists and clears it when it has removed it; when the helper dies, the plugin (which owns the descriptor too) removes what is left. */
inline std::string textString (const SceneData& s, const SceneOp& o)
{
    TextGeom g;
    if (o.op != kOpText || ! textGeom (o, g) || g.offset + g.length > static_cast<int> (s.numText))
        return std::string();
    return std::string (s.text + g.offset, static_cast<size_t> (g.length));
}

struct Ledger
{
    uint32_t handle[kMaxLedger];
    uint32_t fb[kMaxLedger];
};

/** The helper's presentation statistics (written by the helper with relaxed atomic stores, read by the plugin for the status file): the intervals between two plane updates of a run,
    in vblanks (index 0 .. 9, the last = 9 and more) and in 2 ms buckets (the last = 126 ms and more); `stride` and `periodUs` as the helper uses them, `onGrid` = 1 when it waits
    for vblanks (0: timer). Same layout as PaceStats of vblank_pacer.h. */
struct PaceBlock
{
    uint32_t count, stride, periodUs, onGrid;
    uint32_t vb[10];
    uint32_t ms[64];
    uint32_t planeChecks, planeRefreshes, loopWakes, loopWakesIdle;   // loopWakes: every return of the helper's main loop from its wait (g134); loopWakesIdle: the timeouts with nothing to do (at rest at most one per 10 s); planeChecks: how often the helper has asked the display, right after an update, whether the plane shows our framebuffer on our CRTC (once a second); planeRefreshes: how often it found the plane switched off (a mode set of the application) and set it again (the plugin then takes the stacking again)
};

/** The helper's render statistics, cumulative since it started (the plugin takes two snapshots and subtracts them: the mean and the 99th percentile of a window, not the last picture): the number of pictures, the sums
    of the microseconds of their parts (total = read + build + submit + finish + tail; read = the copy of the scene, build = the vertices of the ops (CPU), submit = the GL calls, finish = glFinish (the GPU), tail = the swap and
    the framebuffer), the sums of the ops, vertices and draw calls, the ops that were in cached cells and the cells drawn again, the pixels of the buffer that were painted (the box of a partial picture, buffer age) and the pictures that were painted whole, and the histogram of the total in 1 ms buckets (the last bucket: 63 ms and more). */
struct RenderBlock
{
    uint32_t count, totalUs, readUs, buildUs, submitUs, finishUs, tailUs, ops, verts, draws, cachedOps, redrawn, paintedPx, fullFrames;
    uint32_t hist[64];
};

struct SharedBlock
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t reserved;
    uint32_t seq;                    // the seqlock: odd while the plugin writes
    uint32_t words[kSceneWords];     // SceneData, word by word (relaxed atomic accesses; the seqlock orders them)
    Ledger ledger;
    PaceBlock pace;
    RenderBlock render;
};

inline void initBlock (SharedBlock* b) noexcept
{
    std::memset (b, 0, sizeof *b);
    b->magic = kBlockMagic;
    b->version = kProtocolVersion;
    b->size = static_cast<uint32_t> (sizeof *b);
}
inline bool blockValid (const SharedBlock* b) noexcept { return b->magic == kBlockMagic && b->version == kProtocolVersion && b->size == sizeof (SharedBlock); }

/** The plugin's side: one writer. Returns the (even) sequence number of the scene written. Only the used part of the scene is copied (the header, numOps ops, numText bytes of text, numPoints
    vertices): a scene of a few hundred ops is a few kilobytes, a full frame of curves some hundred; the counts are cut to the limits. */
inline uint32_t writeScene (SharedBlock* b, const SceneData& s) noexcept
{
    const uint32_t* src = reinterpret_cast<const uint32_t*> (&s);
    const uint32_t nOps = std::min (s.numOps, static_cast<uint32_t> (kMaxOps)), nText = std::min (s.numText, static_cast<uint32_t> (kMaxText)), nPts = std::min (s.numPoints, static_cast<uint32_t> (kMaxVertices));
    constexpr uint32_t kHead = static_cast<uint32_t> (offsetof (SceneData, ops) / 4), kOpsAt = kHead, kTextAt = static_cast<uint32_t> (offsetof (SceneData, text) / 4), kYAt = static_cast<uint32_t> (offsetof (SceneData, y) / 4);
    const uint32_t start = __atomic_load_n (&b->seq, __ATOMIC_RELAXED);
    __atomic_store_n (&b->seq, start + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence (__ATOMIC_RELEASE);
    auto copy = [&] (uint32_t at, uint32_t words) { for (uint32_t i = 0; i < words; ++i) __atomic_store_n (&b->words[at + i], src[at + i], __ATOMIC_RELAXED); };
    copy (0, kHead);
    copy (kOpsAt, nOps * 8u);
    copy (kTextAt, (nText + 3u) / 4u);
    copy (kYAt, nPts);
    __atomic_store_n (&b->seq, start + 2, __ATOMIC_RELEASE);
    return start + 2;
}

/** The helper's side: copies the used part of the scene out; retries while the writer is busy. false when no consistent copy was possible in `maxTries` (the caller tries again at the next kick).
    `seqOut` is the sequence number of the copy. The counts of a torn read are cut to the limits before they are used; the copy is thrown away when the sequence number changed. */
inline bool readScene (const SharedBlock* b, SceneData& out, uint32_t& seqOut, int maxTries = 1000) noexcept
{
    uint32_t* dst = reinterpret_cast<uint32_t*> (&out);
    constexpr uint32_t kHead = static_cast<uint32_t> (offsetof (SceneData, ops) / 4), kTextAt = static_cast<uint32_t> (offsetof (SceneData, text) / 4), kYAt = static_cast<uint32_t> (offsetof (SceneData, y) / 4);
    for (int t = 0; t < maxTries; ++t)
    {
        const uint32_t s1 = __atomic_load_n (&b->seq, __ATOMIC_ACQUIRE);
        if (s1 & 1u)
            continue;
        auto copy = [&] (uint32_t at, uint32_t words) { for (uint32_t i = 0; i < words; ++i) dst[at + i] = __atomic_load_n (&b->words[at + i], __ATOMIC_RELAXED); };
        copy (0, kHead);
        const uint32_t nOps = std::min (dst[4], static_cast<uint32_t> (kMaxOps)), nPts = std::min (dst[5], static_cast<uint32_t> (kMaxVertices)), nText = std::min (dst[6], static_cast<uint32_t> (kMaxText));
        copy (kHead, nOps * 8u);
        copy (kTextAt, (nText + 3u) / 4u);
        copy (kYAt, nPts);
        __atomic_thread_fence (__ATOMIC_ACQUIRE);
        if (__atomic_load_n (&b->seq, __ATOMIC_RELAXED) == s1)
        {
            out.numOps = nOps;
            out.numPoints = nPts;
            out.numText = nText;
            out.numDefault = std::min (out.numDefault, nPts);
            seqOut = s1;
            return true;
        }
    }
    return false;
}

/** SHOW travels over the socket and scenes through the shared block. A coherent
    scene can still belong to the previous or next view, including at the same size. */
inline bool readSceneForView (const SharedBlock* block, SceneData& scene, uint32_t& seq, int view, int w, int h) noexcept
{
    return readScene (block, scene, seq) && scene.viewIndex == static_cast<uint32_t> (view) && scene.w == w && scene.h == h;
}

inline void ledgerSet (SharedBlock* b, int slot, uint32_t handle, uint32_t fb) noexcept
{
    __atomic_store_n (&b->ledger.handle[slot], handle, __ATOMIC_RELEASE);
    __atomic_store_n (&b->ledger.fb[slot], fb, __ATOMIC_RELEASE);
}
inline void ledgerGet (const SharedBlock* b, int slot, uint32_t& handle, uint32_t& fb) noexcept
{
    handle = __atomic_load_n (&b->ledger.handle[slot], __ATOMIC_ACQUIRE);
    fb = __atomic_load_n (&b->ledger.fb[slot], __ATOMIC_ACQUIRE);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The control messages.
enum MsgType : uint32_t
{
    // plugin -> helper
    kMsgHello = 1,    // a = protocol version, b = crtc id, c = plane id, d = flags (kFlag...), e = vblank pipe | (pace stride << 8), f = measured vblank period in microseconds; carries two descriptors (SCM_RIGHTS): [0] the display, [1] the shared block
    kMsgShow = 2,     // a, b = canvas w, h (landscape); c, d = panel x, y of the plane; e = rotation (90 / 270); f = view index; text = view name
    kMsgScene = 3,    // a = sequence number of the scene just written (a kick: the helper reads the latest)
    kMsgHide = 4,     // the plane goes off, the buffers go
    kMsgQuit = 5,
    kMsgPageOk = 7,   // a = the guard epoch of the helper's last kMsgGuard, b = the view index a page detection made AFTER that guard found on the current host framebuffer (gpu/guard_state.h)
    kMsgStride = 6,   // a = the vblanks per picture (1 .. 4): 1 = a picture per vblank (60 a second on a 60 Hz panel), the plane update at once after the render with no slot wait; 2 = the pace grid of the hello
    // helper -> plugin
    kMsgReady = 101,  // a = start-up time in ms, b = panel width, c = panel height; text = GL_RENDERER
    kMsgShown = 102,  // the surface for the region exists (answer to a show)
    kMsgFrame = 103,  // a = frames presented in total, b = sequence number of the scene presented, c = render time of the last frame (microseconds, CPU side), d = SETPLANE time (us), e = skipped vblanks in total (frames in a row), f = SETPLANE calls longer than 20 ms in total
    kMsgHidden = 104, // the plane is off and the framebuffers are gone (answer to a hide)
    kMsgGuard = 106,  // a = guard epoch: the commit shim turned the plane off with a host commit (guard_plane_off); the helper stays hidden until a kMsgPageOk of this epoch (gpu/guard_state.h)
    kMsgError = 105   // text = what failed; the helper exits after it
};
constexpr uint32_t kFlagDump = 1u;     // the helper may write its finished frame to a file when /tmp/synthyboy-overlay-gpu-dump exists (debug)
constexpr uint32_t kFlagVblank = 2u;   // the vblank can be waited for on the display descriptor (else the helper paces with a timer)

/** What the plugin tells the helper about the display it found. */
struct HelloParams
{
    uint32_t crtc = 0, plane = 0, flags = 0;
    uint32_t pipe = 0, stride = 2, periodUs = 16667;
};

struct Msg
{
    uint32_t magic;
    uint32_t type;
    uint32_t a, b, c, d, e, f;
    char text[32];
};
static_assert (sizeof (Msg) == 64, "Msg layout");

inline Msg makeMsg (uint32_t type, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0, uint32_t f = 0, const char* text = nullptr) noexcept
{
    Msg m;
    std::memset (&m, 0, sizeof m);
    m.magic = kMsgMagic;
    m.type = type;
    m.a = a; m.b = b; m.c = c; m.d = d; m.e = e; m.f = f;
    if (text)
    {
        size_t n = 0;
        while (text[n] && n < sizeof m.text - 1)
            ++n;
        std::memcpy (m.text, text, n);   // m.text[n] is already 0
    }
    return m;
}

}}} // namespace synthyboy_mpc::overlay::gpu
