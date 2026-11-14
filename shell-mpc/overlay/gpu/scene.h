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

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace synthyboy_mpc { namespace overlay { namespace gpu {

constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kBlockMagic = 0x53424f56u;   // 'SBOV'
constexpr uint32_t kMsgMagic = 0x4d534f56u;     // 'MSOV' ... the first word of every message
constexpr int kMaxCurve = 512;                   // vertices of the polyline (the oscillator cycle has 512)
constexpr int kMaxOps = 32;
constexpr int kMaxLedger = 8;

enum SceneKind : uint32_t { kKindNone = 0, kKindFilter = 1, kKindLfo = 2, kKindOsc = 3, kKindEnv = 4 };

/** The display list. Drawn in order, source over, onto the cleared canvas.
    Clear   rgba: the whole canvas is set to this colour (replaces everything; alpha 0 = transparent)
    Rect    f[0..3] = x0 y0 x1 y1 (edges, pixel aligned in practice), rgba: a solid rectangle
    VLine   f[0..3] = x (centre) y0 y1 width: a vertical line, anti-aliased on its two sides
    Fill    f[0] = baseline row: the area between the polyline and the baseline (a baseline at or below the canvas height fills down to the bottom)
    Stroke  f[0] = half width: the polyline as an anti-aliased line of 2 * half width pixels
    Disc    f[0..2] = cx cy radius: an anti-aliased disc
    The polyline: SceneData::y[0 .. numPoints - 1] are rows, point i at x = i / (numPoints - 1) * (w - 1) + 0.5 (the first and the last continue horizontally to the canvas edges). */
enum OpCode : uint32_t { kOpNone = 0, kOpClear = 1, kOpRect = 2, kOpVLine = 3, kOpFill = 4, kOpStroke = 5, kOpDisc = 6 };

struct SceneOp
{
    uint32_t op;
    uint32_t rgba;
    float f[6];
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
    void stroke (float halfWidth, uint32_t rgba) { if (SceneOp* o = add (kOpStroke, rgba)) o->f[0] = halfWidth; }
    void disc (float cx, float cy, float r, uint32_t rgba) { if (SceneOp* o = add (kOpDisc, rgba)) { o->f[0] = cx; o->f[1] = cy; o->f[2] = r; } }
    /** The polyline rows (n <= kMaxCurve; more is a programming error: it is cut and `overflow` is set). */
    void polyline (const float* rows, int n)
    {
        if (n > kMaxCurve) { overflow = true; n = kMaxCurve; }
        for (int i = 0; i < n; ++i)
            s_.y[i] = rows[i];
        s_.numPoints = static_cast<uint32_t> (n < 0 ? 0 : n);
    }
    bool overflow = false;

private:
    SceneData& s_;
};

/** The ledger: the GEM handles and framebuffer ids the helper has made on the shared display descriptor (PRIME import, ADDFB2). The helper writes an entry as soon as the object
    exists and clears it when it has removed it; when the helper dies, the plugin (which owns the descriptor too) removes what is left. */
struct Ledger
{
    uint32_t handle[kMaxLedger];
    uint32_t fb[kMaxLedger];
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
    kMsgHello = 1,    // a = protocol version, b = crtc id, c = plane id, d = flags (kFlag...); carries two descriptors (SCM_RIGHTS): [0] the display, [1] the shared block
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
constexpr uint32_t kFlagDump = 1u;   // the helper may write its finished frame to a file when /tmp/synthyboy-overlay-gpu-dump exists (debug)

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
