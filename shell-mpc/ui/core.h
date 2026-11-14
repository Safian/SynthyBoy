// SynthyBoy - MPC UI component library: the common contract. Every component is a black box with an INTERFACE: a small plain state struct (what it shows), a rectangle it must stay inside,
// a `qFrame` flag (it belongs to the active Q-Link group) and an `enabled` flag (disabled = drawn dim, no reaction) go in; a drawing (scene ops inside the rectangle) and an optional
// change { id, value } come out. The component never stores the truth: the value comes in through the state struct, and after a change the owner writes the new value back.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Events (single finger): touchDown (x, y, nowMs) / touchMove (x, y) / touchUp (x, y) in canvas pixels, and turn (delta) for the hardware knob (Q-Link) assigned to the component. A
// component that got a touch down keeps the gesture until the touch up, even when the finger leaves its rectangle. Each call returns a Change (kind None: nothing happened).
// `id` is an opaque integer the OWNER of the component gave to the value (setId): the component never knows what a parameter is.
// Drawing: draw (SceneBuilder&) emits ops only inside the rectangle: no allocation, no locks, no I/O, the same state gives the same ops.
// The style is the palette of the overlay views (views.h, play_view.h): the dark background 1e1d1e, the panel 262527, the rest grey 3d3b3e, the accent ff2e63 / d31145, white / light grey.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "../overlay/font5x7.h"
#include "../overlay/gpu/scene.h"

namespace synthyboy_mpc { namespace ui {

namespace gpu = synthyboy_mpc::overlay::gpu;
namespace font = synthyboy_mpc::overlay::font;

constexpr float kPi = 3.14159265358979323846f;

struct Size { int w = 0, h = 0; };

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    int right() const noexcept { return x + w; }
    int bottom() const noexcept { return y + h; }
    bool empty() const noexcept { return w <= 0 || h <= 0; }
    bool contains (int px, int py) const noexcept { return px >= x && px < x + w && py >= y && py < y + h; }
    bool contains (const Rect& o) const noexcept { return o.x >= x && o.y >= y && o.right() <= right() && o.bottom() <= bottom(); }
    bool overlaps (const Rect& o) const noexcept { return ! empty() && ! o.empty() && x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom(); }
    Rect inset (int d) const noexcept { return { x + d, y + d, std::max (0, w - 2 * d), std::max (0, h - 2 * d) }; }
    bool operator== (const Rect& o) const noexcept { return x == o.x && y == o.y && w == o.w && h == o.h; }
    static Rect unite (const Rect& a, const Rect& b) noexcept
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        const int x0 = std::min (a.x, b.x), y0 = std::min (a.y, b.y), x1 = std::max (a.right(), b.right()), y1 = std::max (a.bottom(), b.bottom());
        return { x0, y0, x1 - x0, y1 - y0 };
    }
};

/** What a component hands back. Value: `value` is normalised 0 .. 1 (a knob). Index: `index` is a whole number (a list position, a switch 0 / 1, the number of steps from the minimum of a
    number field) and `value` is the same number as a double (a number field: the value in its own units, a whole number of steps). Request: the user asked for a menu / a panel; the owner
    opens whatever it wants. */
enum class ChangeKind : uint8_t { None = 0, Value, Index, Request, Depth, ModsRequest };
struct Change
{
    ChangeKind kind = ChangeKind::None;
    int id = 0;
    double value = 0.0;
    int index = 0;
    explicit operator bool() const noexcept { return kind != ChangeKind::None; }
    /** Depth: the modulation depth (-1 .. +1, `value`) of the source `index` (its id) of a knob / number field changed (a drag that started on the tab). ModsRequest: a long press asks for the
        list of the modulations of the control. */
    static Change depth (int id, int source, double d) noexcept { Change c; c.kind = ChangeKind::Depth; c.id = id; c.index = source; c.value = d; return c; }
    static Change modsRequest (int id) noexcept { Change c; c.kind = ChangeKind::ModsRequest; c.id = id; return c; }
    static Change none() noexcept { return {}; }
    static Change normalised (int id, double v) noexcept { Change c; c.kind = ChangeKind::Value; c.id = id; c.value = v; return c; }
    static Change indexed (int id, int i, double v) noexcept { Change c; c.kind = ChangeKind::Index; c.id = id; c.index = i; c.value = v; return c; }
    static Change request (int id) noexcept { Change c; c.kind = ChangeKind::Request; c.id = id; return c; }
};

namespace colour
{
constexpr uint32_t rgba (uint32_t rgb, uint32_t a = 255) noexcept { return (rgb << 8) | a; }
constexpr uint32_t kBackground = 0x1e1d1eu, kPanel = 0x262527u, kRest = 0x3d3b3eu, kAccent = 0xff2e63u, kAccentDark = 0xd31145u, kText = 0xe6e6e6u, kWhite = 0xffffffu;
}

/** Copies up to cap - 1 characters (zero terminated). */
inline void setText (char* dst, size_t cap, const char* src) noexcept
{
    size_t n = 0;
    if (src)
        while (src[n] != '\0' && n + 1 < cap)
        {
            dst[n] = src[n];
            ++n;
        }
    dst[n] = '\0';
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** The modulation input of a Knob and a NumberField: up to kMaxMods entries {source id (opaque), source name <= 6 characters, depth -1 .. +1 as a fraction of the full range, polarity,
    bypassed} and the selected source (kNoSource = none). */
constexpr int kNoSource = INT32_MIN;
struct ModEntry
{
    int source = 0;
    char name[7] = {};
    double depth = 0.0;
    bool bidirectional = false, bypassed = false;
};
struct ModInput
{
    static constexpr int kMaxMods = 8;
    ModEntry mods[kMaxMods];
    int numMods = 0;
    int selectedSource = kNoSource;

    bool add (int source, const char* name, double depth, bool bidirectional = false, bool bypassed = false) noexcept
    {
        if (numMods >= kMaxMods)
            return false;
        ModEntry& e = mods[numMods++];
        e.source = source; setText (e.name, sizeof e.name, name); e.depth = depth; e.bidirectional = bidirectional; e.bypassed = bypassed;
        return true;
    }
    /** The entry of the selected source (null when none is selected or it is not in `mods`). */
    const ModEntry* selected() const noexcept
    {
        for (int i = 0; i < std::min (numMods, kMaxMods); ++i)
            if (mods[i].source == selectedSource && selectedSource != kNoSource)
                return &mods[i];
        return nullptr;
    }
};
/** The tab at the top left: Absent (no modulation entries), Grey (entries, but not the selected source), Coloured (the selected source is in the entries), Hollow (and it is bypassed). */
enum class TabState : uint8_t { Absent, Grey, Coloured, Hollow };
inline TabState tabState (const ModInput& m) noexcept
{
    if (m.numMods <= 0)
        return TabState::Absent;
    const ModEntry* e = m.selected();
    return ! e ? TabState::Grey : (e->bypassed ? TabState::Hollow : TabState::Coloured);
}
/** The ends (0 .. 1) of the depth arc around the value position v: unidirectional v .. v + depth, bidirectional v - |depth| .. v + |depth|, clamped to the range; from <= to. false when
    the selected source is not in the entries or is bypassed (no arc). */
inline bool depthArc (const ModInput& m, double v, double& from, double& to) noexcept
{
    const ModEntry* e = m.selected();
    if (! e || e->bypassed)
        return false;
    v = std::clamp (v, 0.0, 1.0);
    const double d = std::clamp (e->depth, -1.0, 1.0);
    double a, b;
    if (e->bidirectional) { a = v - std::fabs (d); b = v + std::fabs (d); }
    else { a = std::min (v, v + d); b = std::max (v, v + d); }
    from = std::clamp (a, 0.0, 1.0);
    to = std::clamp (b, 0.0, 1.0);
    return true;
}
/** The tab and the rectangle that starts a depth gesture. */
inline Rect tabRect (const Rect& r) noexcept { return { r.x + 3, r.y + 3, 14, 8 }; }
inline Rect tabHitRect (const Rect& r) noexcept { return { r.x, r.y, std::min (r.w, 32), std::min (r.h, 28) }; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** The drawing pen of a component: scene ops with the colours of a disabled component dimmed. The component draws through it and keeps inside its rectangle by construction (tests check). */
class Pen
{
public:
    Pen (gpu::SceneBuilder& sb, bool enabled) : sb_ (sb), dim_ (enabled ? 255 : 100) {}

    /** The colour `rgba` (0xRRGGBBAA) with the alpha of a disabled component scaled down. */
    uint32_t c (uint32_t rgba) const noexcept { return (rgba & 0xffffff00u) | (((rgba & 255u) * dim_ + 127u) / 255u); }
    uint32_t c (uint32_t rgb, int alpha) const noexcept { return c (colour::rgba (rgb, static_cast<uint32_t> (alpha))); }

    void rect (int x0, int y0, int x1, int y1, uint32_t col) { if (x1 > x0 && y1 > y0) sb_.rect (static_cast<float> (x0), static_cast<float> (y0), static_cast<float> (x1), static_cast<float> (y1), col); }
    void rect (const Rect& r, uint32_t col) { rect (r.x, r.y, r.right(), r.bottom(), col); }
    void frame (const Rect& r, int t, uint32_t col) { if (! r.empty()) sb_.frame (static_cast<float> (r.x), static_cast<float> (r.y), static_cast<float> (r.right()), static_cast<float> (r.bottom()), static_cast<float> (t), col); }
    void line (float x0, float y0, float x1, float y1, float width, uint32_t col) { sb_.line (x0, y0, x1, y1, width, col); }
    void arc (float cx, float cy, float radius, float a0, float a1, float width, uint32_t col) { if (a1 - a0 > 0.004f || a0 - a1 > 0.004f) sb_.arc (cx, cy, radius, a0, a1, width, col); }
    void disc (float cx, float cy, float r, uint32_t col) { sb_.disc (cx, cy, r, col); }
    void text (int x, int y, int cell, const char* s, uint32_t col) { if (s[0] != '\0' && cell > 0) sb_.text (x, y, cell, s, col); }

    /** The largest cell (pixels per font pixel, at most maxCell) at which `n` characters fit into maxW x maxH; 0 when not even one pixel per font pixel fits. */
    static int fitCell (int n, int maxW, int maxH, int maxCell) noexcept
    {
        for (int cell = maxCell; cell >= 1; --cell)
            if (font::textWidth (n, cell) <= maxW && font::kGlyphH * cell <= maxH)
                return cell;
        return 0;
    }
    /** The text centred in the box (both ways) at the largest cell <= maxCell that fits; the cell used (0: it does not fit, nothing is drawn). */
    int textCentered (const Rect& box, const char* s, int maxCell, uint32_t col)
    {
        const int n = static_cast<int> (std::min<size_t> (std::strlen (s), 12));
        const int cell = fitCell (n, box.w, box.h, maxCell);
        if (cell > 0 && n > 0)
            text (box.x + (box.w - font::textWidth (n, cell)) / 2, box.y + (box.h - font::kGlyphH * cell) / 2, cell, s, col);
        return cell;
    }
    /** The text at the left edge of the box, vertically centred. */
    int textLeft (const Rect& box, const char* s, int maxCell, uint32_t col)
    {
        const int n = static_cast<int> (std::min<size_t> (std::strlen (s), 12));
        const int cell = fitCell (n, box.w, box.h, maxCell);
        if (cell > 0 && n > 0)
            text (box.x, box.y + (box.h - font::kGlyphH * cell) / 2, cell, s, col);
        return cell;
    }
    /** A chevron (a "<" ">" "^" "v" made of two lines) in a box of `s` pixels around (cx, cy): dir 0 = left, 1 = right, 2 = up, 3 = down. */
    void chevron (float cx, float cy, float s, int dir, float width, uint32_t col)
    {
        const float a = s * 0.5f;
        float p0x = 0, p0y = 0, p1x = 0, p1y = 0, p2x = 0, p2y = 0;   // the corner is p1
        switch (dir)
        {
            case 0: p0x = cx + a * 0.6f; p0y = cy - a; p1x = cx - a * 0.6f; p1y = cy; p2x = p0x; p2y = cy + a; break;
            case 1: p0x = cx - a * 0.6f; p0y = cy - a; p1x = cx + a * 0.6f; p1y = cy; p2x = p0x; p2y = cy + a; break;
            case 2: p0x = cx - a; p0y = cy + a * 0.6f; p1x = cx; p1y = cy - a * 0.6f; p2x = cx + a; p2y = p0y; break;
            default: p0x = cx - a; p0y = cy - a * 0.6f; p1x = cx; p1y = cy + a * 0.6f; p2x = cx + a; p2y = p0y; break;
        }
        line (p0x, p0y, p1x, p1y, width, col);
        line (p1x, p1y, p2x, p2y, width, col);
    }
    /** The gear (a ring with eight teeth, about 24 px), the bell curve (about 28 x 20 px) and the route fork (about 24 px) around (cx, cy): the icons of the DetailButton. */
    void iconGear (float cx, float cy, uint32_t col)
    {
        arc (cx, cy, 6.0f, 0.0f, 2.0f * kPi, 3.0f, col);
        for (int i = 0; i < 8; ++i)
        {
            const float a = static_cast<float> (i) * kPi / 4.0f;
            line (cx + 7.5f * std::cos (a), cy + 7.5f * std::sin (a), cx + 11.0f * std::cos (a), cy + 11.0f * std::sin (a), 3.0f, col);
        }
    }
    void iconBell (float cx, float cy, uint32_t col)
    {
        float px = cx - 13.0f, py = cy + 8.0f;
        for (int i = 1; i <= 10; ++i)
        {
            const float t = -1.0f + 0.2f * static_cast<float> (i), x = cx + 13.0f * t, y = cy + 8.0f - 16.0f * std::exp (-3.2f * t * t);
            line (px, py, x, y, 2.0f, col);
            px = x; py = y;
        }
    }
    void iconRoute (float cx, float cy, uint32_t col)
    {
        line (cx - 11.0f, cy, cx - 2.0f, cy, 3.0f, col);
        line (cx - 2.0f, cy, cx + 8.0f, cy - 7.0f, 3.0f, col);
        line (cx - 2.0f, cy, cx + 8.0f, cy + 7.0f, 3.0f, col);
        disc (cx + 10.0f, cy - 7.0f, 2.5f, col);
        disc (cx + 10.0f, cy + 7.0f, 2.5f, col);
    }
    /** The modulation tab at the top left of `r` for a tab state (nothing when absent). */
    void modTab (const Rect& r, TabState st)
    {
        if (st == TabState::Absent)
            return;
        const Rect t = tabRect (r);
        if (st == TabState::Hollow)
            frame (t, 2, c (colour::kAccent, 255));
        else
            rect (t, c (st == TabState::Coloured ? colour::kAccent : colour::kRest, 255));
    }
    /** The small index badge of a Q-Link group member (1 .. 4) in the top right corner of `r`. */
    void qBadge (const Rect& r, int index)
    {
        if (index < 1 || index > 4 || r.w < 24 || r.h < 24)
            return;
        const Rect b { r.right() - 18, r.y + 2, 16, 18 };
        rect (b, c (colour::kAccent, 255));
        char d[2] = { static_cast<char> ('0' + index), '\0' };
        text (b.x + 3, b.y + 2, 2, d, c (colour::kBackground, 255));
    }

private:
    gpu::SceneBuilder& sb_;
    uint32_t dim_;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** The tap / double tap / drag detection of one finger. A tap is a gesture that never left a square of kSlop pixels around its start; a double tap is a tap that follows another tap
    (down to down) within kDoubleMs milliseconds and kDoubleDist pixels. Triple taps count as a double and a single one. */
class TapGesture
{
public:
    static constexpr int kSlop = 6;
    static constexpr int kDoubleDist = 30;
    static constexpr double kDoubleMs = 400.0;

    void down (int x, int y, double nowMs) noexcept { x0_ = x; y0_ = y; t_ = nowMs; moved_ = false; }
    /** true once the finger has left the square of kSlop pixels (and stays true for the gesture). */
    bool move (int x, int y) noexcept
    {
        if (! moved_ && (std::abs (x - x0_) > kSlop || std::abs (y - y0_) > kSlop))
            moved_ = true;
        return moved_;
    }
    bool moved() const noexcept { return moved_; }
    /** The end of the gesture: 0 = it was a drag, 1 = a tap, 2 = a double tap. */
    int up() noexcept
    {
        if (moved_)
        {
            haveLast_ = false;   // a drag between two taps breaks the double tap
            return 0;
        }
        if (haveLast_ && t_ - lastT_ <= kDoubleMs && t_ >= lastT_ && std::abs (x0_ - lastX_) <= kDoubleDist && std::abs (y0_ - lastY_) <= kDoubleDist)
        {
            haveLast_ = false;
            return 2;
        }
        haveLast_ = true;
        lastT_ = t_; lastX_ = x0_; lastY_ = y0_;
        return 1;
    }
    int startX() const noexcept { return x0_; }
    int startY() const noexcept { return y0_; }
    void reset() noexcept { haveLast_ = false; moved_ = false; }

private:
    int x0_ = 0, y0_ = 0, lastX_ = 0, lastY_ = 0;
    double t_ = 0.0, lastT_ = 0.0;
    bool moved_ = false, haveLast_ = false;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** The base of every component: the rectangle, the id, the enabled and qFrame flags, the gesture capture, and the interface the Q groups and the panels use. A base component implements
    onDown / onMove / onUp / onTurn and drawBody; a composite (composite.h) overrides the public events. */
class Component
{
public:
    Component() = default;
    virtual ~Component() = default;
    Component (const Component&) = delete;
    Component& operator= (const Component&) = delete;

    // ---- the common input
    Rect rect() const noexcept { return rect_; }
    virtual void place (const Rect& r) { rect_ = r; }
    bool enabled() const noexcept { return enabled_; }
    virtual void setEnabled (bool e) { enabled_ = e; if (! e) captured_ = false; }
    bool qFrame() const noexcept { return qFrame_; }
    int qIndex() const noexcept { return qIndex_; }
    /** The component belongs to the active Q-Link group (frame true) as member number `index` (1 .. 4, 0 = no badge). */
    virtual void setQ (bool frame, int index) { qFrame_ = frame; qIndex_ = frame ? index : 0; }
    int id() const noexcept { return id_; }
    void setId (int id) noexcept { id_ = id; }
    /** The smallest size at which the component is legible with its touch targets (the layout must give at least this). */
    virtual Size minSize() const = 0;
    bool sizeOk() const noexcept { const Size m = minSize(); return rect_.w >= m.w && rect_.h >= m.h; }

    // ---- the output: the drawing
    virtual void draw (gpu::SceneBuilder& sb) const
    {
        Pen pen (sb, enabled_);
        drawBody (pen);
        if (qIndex_ > 0)
            pen.qBadge (rect_, qIndex_);
    }

    // ---- the output: the changes
    virtual Change touchDown (int x, int y, double nowMs)
    {
        captured_ = false;
        if (! enabled_ || ! rect_.contains (x, y))
            return Change::none();
        captured_ = true;
        return onDown (x, y, nowMs);
    }
    virtual Change touchMove (int x, int y) { return captured_ && enabled_ ? onMove (x, y) : Change::none(); }
    virtual Change touchUp (int x, int y)
    {
        if (! captured_)
            return Change::none();
        captured_ = false;
        return enabled_ ? onUp (x, y) : Change::none();
    }
    /** The clock: the owner calls it regularly (every frame) with the current time while a finger is down; a component that detects a long press says so here. The components have no clock of
        their own: time comes in with touchDown (nowMs) and with tick (nowMs). */
    virtual Change tick (double /*nowMs*/) { return Change::none(); }
    /** The hardware knob assigned to the component turned by `delta` detents (a knob: normalised units; a list / number: whole steps). */
    virtual Change turn (double delta) { return enabled_ ? onTurn (delta) : Change::none(); }
    bool capturing() const noexcept { return captured_; }

    // ---- structure
    /** The rectangles that react to a touch (a stepper: its two halves). */
    virtual int touchTargets (Rect* out, int max) const { if (max > 0) { out[0] = rect_; return 1; } return 0; }
    /** The leaves (base components) of this component, appended to out[n ..]; returns the new count (at most max). */
    virtual int collectLeaves (Component** out, int n, int max) { if (n < max) out[n++] = this; return n; }

protected:
    virtual void drawBody (Pen& pen) const = 0;
    virtual Change onDown (int, int, double) { return Change::none(); }
    virtual Change onMove (int, int) { return Change::none(); }
    virtual Change onUp (int, int) { return Change::none(); }
    virtual Change onTurn (double) { return Change::none(); }

    Rect rect_;
    bool enabled_ = true, qFrame_ = false, captured_ = false;
    int qIndex_ = 0, id_ = 0;
};

/** The colour of a label: the accent in an active Q group, else the light grey. */
inline uint32_t labelColour (bool qFrame) noexcept { return colour::rgba (qFrame ? colour::kAccent : colour::kText, 255); }

}} // namespace synthyboy_mpc::ui
