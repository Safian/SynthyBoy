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

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace synthyboy_mpc { namespace overlay { namespace gpu {

constexpr uint32_t kProtocolVersion = 4;   // 4: kMaxOps 96, kMaxCurve 1088, kOpCurve (a polyline with its own span), kOpText, kKindPlay. 2: SceneOp::rgba2 (was f[0] .. f[5], now f[0] .. f[4]), kOpFillGradient, kKindSpectrum. 3: the hello carries the vblank pipe, the pace stride and the measured period (flag kFlagVblank), the shared block carries the helper's presentation statistics (PaceBlock), the display may be rotated 0 / 90 / 180 / 270. The plugin and the helper are shipped together; a helper of another version refuses the hello (kMsgError) and the plugin draws with the CPU.
constexpr uint32_t kBlockMagic = 0x53424f56u;   // 'SBOV'
constexpr uint32_t kMsgMagic = 0x4d534f56u;     // 'MSOV' ... the first word of every message
constexpr int kMaxCurve = 1088;                  // vertices of all polylines of a scene together (the oscillator cycle has 512; the Play page: 512 + 256 + 300)
constexpr int kMaxOps = 96;
constexpr int kMaxLedger = 8;

/** The span of vertices of a Curve op: `firstF` and `nF` are floats of a shared block (written by the plugin, read by the helper: never trusted). true: first (>= 0) and n (>= 2) are whole
    numbers (rounded) with first + n <= kMaxCurve; false for NaN, infinity, a negative start, fewer than two vertices and anything that reaches past the table or does not fit an int. */
inline bool curveSpan (float firstF, float nF, int& first, int& n) noexcept
{
    if (! std::isfinite (firstF) || ! std::isfinite (nF) || firstF < -0.5f || nF < 1.5f || firstF > static_cast<float> (kMaxCurve) || nF > static_cast<float> (kMaxCurve))
        return false;
    const int f = static_cast<int> (firstF + 0.5f), c = static_cast<int> (nF + 0.5f);
    if (f < 0 || c < 2 || f + c > kMaxCurve)
        return false;
    first = f;
    n = c;
    return true;
}

enum SceneKind : uint32_t { kKindNone = 0, kKindFilter = 1, kKindLfo = 2, kKindOsc = 3, kKindEnv = 4, kKindSpectrum = 5, kKindPlay = 6 };

/** The display list. Drawn in order, source over, onto the cleared canvas.
    Clear   rgba: the whole canvas is set to this colour (replaces everything; alpha 0 = transparent)
    Rect    f[0..3] = x0 y0 x1 y1 (edges, pixel aligned in practice), rgba: a solid rectangle
    VLine   f[0..3] = x (centre) y0 y1 width: a vertical line, anti-aliased on its two sides
    Fill    f[0] = baseline row: the area between the polyline and the baseline (a baseline at or below the canvas height fills down to the bottom)
    Stroke  f[0] = half width: the polyline as an anti-aliased line of 2 * half width pixels
    Disc    f[0..2] = cx cy radius: an anti-aliased disc
    FillGradient  f[0] = baseline row (as Fill), f[1] = row yA, f[2] = row yB: the same area as Fill, coloured by a vertical gradient: `rgba` at row yA, `rgba2` at row yB, linear
            in between and constant outside (premultiplied colours are interpolated; evaluated at the centre of every pixel row)
    Curve   f[0] = first vertex, f[1] = number of vertices n (>= 2), f[2] = x0, f[3] = width W: from here on the polyline is the vertices y[first .. first + n - 1], point i at
            x = x0 + i / (n - 1) * (W - 1) + 0.5, the first and the last continue horizontally to x0 and x0 + W (a Fill or Stroke of this curve is limited to those columns)
    Text    f[0..1] = x y (top left, whole pixels), f[2] = pixels per font pixel (whole), rgba: the text of up to 12 characters (font5x7.h; solid squares): the first four characters are the bytes
            of rgba2 (first character in the least significant byte), the next four of the bits of f[3], the last four of f[4]; the text ends at the first zero byte
    The polyline: without a Curve op, SceneData::y[0 .. numPoints - 1] are rows, point i at x = i / (numPoints - 1) * (w - 1) + 0.5 (the first and the last continue horizontally to the canvas edges). */
enum OpCode : uint32_t { kOpNone = 0, kOpClear = 1, kOpRect = 2, kOpVLine = 3, kOpFill = 4, kOpStroke = 5, kOpDisc = 6, kOpFillGradient = 7, kOpCurve = 8, kOpText = 9 };

struct SceneOp
{
    uint32_t op;
    uint32_t rgba;
    uint32_t rgba2;   // the second colour of a gradient (0 for the other ops)
    float f[5];
};
static_assert (sizeof (SceneOp) == 32, "SceneOp layout");

struct SceneData
{
    uint32_t kind;        // SceneKind (for the status and the tests; the helper draws from the ops alone)
    uint32_t viewIndex;   // the index in overlay_layout.h
    int32_t w, h;         // the canvas = the region in landscape UI pixels
    uint32_t numOps;
    uint32_t numPoints;
    uint32_t reserved[2];
    SceneOp ops[kMaxOps];
    float y[kMaxCurve];
};
static_assert (sizeof (SceneData) == 32 + 32 * kMaxOps + 4 * kMaxCurve, "SceneData layout");
constexpr int kSceneWords = static_cast<int> (sizeof (SceneData) / 4);

inline uint32_t rgba8 (uint32_t r, uint32_t g, uint32_t b, uint32_t a) noexcept { return (r << 24) | (g << 16) | (b << 8) | a; }
inline uint32_t rgba8FromRgb (uint32_t rgb, uint32_t a) noexcept { return ((rgb & 0xffffffu) << 8) | a; }

/** Builds a scene: the helpers keep the count within the limits (an op beyond the limit is dropped and `overflow` is set). */
struct SceneBuilder
{
    explicit SceneBuilder (SceneData& s) : s_ (s) { std::memset (&s_, 0, sizeof s_); }
    void begin (SceneKind kind, uint32_t viewIndex, int w, int h) { s_.kind = kind; s_.viewIndex = viewIndex; s_.w = w; s_.h = h; }
    SceneOp* add (OpCode op, uint32_t rgba)
    {
        if (s_.numOps >= static_cast<uint32_t> (kMaxOps)) { overflow = true; return nullptr; }
        SceneOp& o = s_.ops[s_.numOps++];
        o.op = op;
        o.rgba = rgba;
        return &o;
    }
    void clear (uint32_t rgba) { add (kOpClear, rgba); }
    void rect (float x0, float y0, float x1, float y1, uint32_t rgba) { if (SceneOp* o = add (kOpRect, rgba)) { o->f[0] = x0; o->f[1] = y0; o->f[2] = x1; o->f[3] = y1; } }
    void vline (float x, float y0, float y1, float width, uint32_t rgba) { if (SceneOp* o = add (kOpVLine, rgba)) { o->f[0] = x; o->f[1] = y0; o->f[2] = y1; o->f[3] = width; } }
    void fill (float baseRow, uint32_t rgba) { if (SceneOp* o = add (kOpFill, rgba)) o->f[0] = baseRow; }
    void fillGradient (float baseRow, float yA, float yB, uint32_t rgbaA, uint32_t rgbaB) { if (SceneOp* o = add (kOpFillGradient, rgbaA)) { o->rgba2 = rgbaB; o->f[0] = baseRow; o->f[1] = yA; o->f[2] = yB; } }
    void stroke (float halfWidth, uint32_t rgba) { if (SceneOp* o = add (kOpStroke, rgba)) o->f[0] = halfWidth; }
    void disc (float cx, float cy, float r, uint32_t rgba) { if (SceneOp* o = add (kOpDisc, rgba)) { o->f[0] = cx; o->f[1] = cy; o->f[2] = r; } }
    /** The polyline rows (n <= kMaxCurve; more is a programming error: it is cut and `overflow` is set). The scene's one polyline over the whole width. */
    void polyline (const float* rows, int n)
    {
        if (n > kMaxCurve) { overflow = true; n = kMaxCurve; }
        for (int i = 0; i < n; ++i)
            s_.y[i] = rows[i];
        s_.numPoints = static_cast<uint32_t> (n < 0 ? 0 : n);
    }
    /** Appends the rows as another polyline and makes it the current one (kOpCurve) over the columns x0 .. x0 + width - 1; the following Fill and Stroke ops draw it. */
    void curve (const float* rows, int n, int x0, int width)
    {
        if (n < 2 || s_.numPoints + static_cast<uint32_t> (n) > static_cast<uint32_t> (kMaxCurve)) { overflow = true; return; }
        const uint32_t first = s_.numPoints;
        for (int i = 0; i < n; ++i)
            s_.y[first + static_cast<uint32_t> (i)] = rows[i];
        s_.numPoints += static_cast<uint32_t> (n);
        if (SceneOp* o = add (kOpCurve, 0)) { o->f[0] = static_cast<float> (first); o->f[1] = static_cast<float> (n); o->f[2] = static_cast<float> (x0); o->f[3] = static_cast<float> (width); }
    }
    /** Text of at most 12 characters (more are cut) with its top left corner at (x, y), `cell` pixels per font pixel. */
    void text (int x, int y, int cell, const char* str, uint32_t rgba)
    {
        if (SceneOp* o = add (kOpText, rgba))
        {
            unsigned char b[12] = {};
            for (int i = 0; i < 12 && str[i] != '\0'; ++i)
                b[i] = static_cast<unsigned char> (str[i]);
            uint32_t w[3];
            for (int k = 0; k < 3; ++k)
                w[k] = static_cast<uint32_t> (b[4 * k]) | (static_cast<uint32_t> (b[4 * k + 1]) << 8) | (static_cast<uint32_t> (b[4 * k + 2]) << 16) | (static_cast<uint32_t> (b[4 * k + 3]) << 24);
            o->rgba2 = w[0];
            std::memcpy (&o->f[3], &w[1], 4);
            std::memcpy (&o->f[4], &w[2], 4);
            o->f[0] = static_cast<float> (x); o->f[1] = static_cast<float> (y); o->f[2] = static_cast<float> (cell);
        }
    }
    bool overflow = false;

private:
    SceneData& s_;
};

/** The characters of a Text op (up to 12, zero terminated in `out[12]`). */
inline void textOf (const SceneOp& o, char out[13]) noexcept
{
    uint32_t w[3];
    w[0] = o.rgba2;
    std::memcpy (&w[1], &o.f[3], 4);
    std::memcpy (&w[2], &o.f[4], 4);
    for (int i = 0; i < 12; ++i)
        out[i] = static_cast<char> ((w[i / 4] >> (8 * (i % 4))) & 255u);
    out[12] = '\0';
}

/** The ledger: the GEM handles and framebuffer ids the helper has made on the shared display descriptor (PRIME import, ADDFB2). The helper writes an entry as soon as the object
    exists and clears it when it has removed it; when the helper dies, the plugin (which owns the descriptor too) removes what is left. */
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
    uint32_t planeChecks, reserved;   // how often the helper has asked the display, right after an update, whether the plane shows our framebuffer on our CRTC (once a second)
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
};

inline void initBlock (SharedBlock* b) noexcept
{
    std::memset (b, 0, sizeof *b);
    b->magic = kBlockMagic;
    b->version = kProtocolVersion;
    b->size = static_cast<uint32_t> (sizeof *b);
}
inline bool blockValid (const SharedBlock* b) noexcept { return b->magic == kBlockMagic && b->version == kProtocolVersion && b->size == sizeof (SharedBlock); }

/** The plugin's side: one writer. Returns the (even) sequence number of the scene written. */
inline uint32_t writeScene (SharedBlock* b, const SceneData& s) noexcept
{
    uint32_t words[kSceneWords];
    std::memcpy (words, &s, sizeof words);
    const uint32_t start = __atomic_load_n (&b->seq, __ATOMIC_RELAXED);
    __atomic_store_n (&b->seq, start + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence (__ATOMIC_RELEASE);
    for (int i = 0; i < kSceneWords; ++i)
        __atomic_store_n (&b->words[i], words[i], __ATOMIC_RELAXED);
    __atomic_store_n (&b->seq, start + 2, __ATOMIC_RELEASE);
    return start + 2;
}

/** The helper's side: copies the scene out; retries while the writer is busy. false when no consistent copy was possible in `maxTries` (the caller tries again at the next kick).
    `seqOut` is the sequence number of the copy. */
inline bool readScene (const SharedBlock* b, SceneData& out, uint32_t& seqOut, int maxTries = 1000) noexcept
{
    uint32_t words[kSceneWords];
    for (int t = 0; t < maxTries; ++t)
    {
        const uint32_t s1 = __atomic_load_n (&b->seq, __ATOMIC_ACQUIRE);
        if (s1 & 1u)
            continue;
        for (int i = 0; i < kSceneWords; ++i)
            words[i] = __atomic_load_n (&b->words[i], __ATOMIC_RELAXED);
        __atomic_thread_fence (__ATOMIC_ACQUIRE);
        if (__atomic_load_n (&b->seq, __ATOMIC_RELAXED) == s1)
        {
            std::memcpy (&out, words, sizeof words);
            seqOut = s1;
            return true;
        }
    }
    return false;
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
    // helper -> plugin
    kMsgReady = 101,  // a = start-up time in ms, b = panel width, c = panel height; text = GL_RENDERER
    kMsgShown = 102,  // the surface for the region exists (answer to a show)
    kMsgFrame = 103,  // a = frames presented in total, b = sequence number of the scene presented, c = render time of the last frame (microseconds, CPU side), d = SETPLANE time (us), e = skipped vblanks in total (frames in a row), f = SETPLANE calls longer than 20 ms in total
    kMsgHidden = 104, // the plane is off and the framebuffers are gone (answer to a hide)
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
