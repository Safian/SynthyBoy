// SynthyBoy - MPC plugin-drawn layer: the common contract. Every component is a black box with an INTERFACE: a small plain state struct (what it shows), a rectangle it must stay inside,
// a `qFrame` flag (it belongs to the active Q-Link group) and an `enabled` flag (disabled = drawn dim, no reaction) go in; a drawing (scene ops inside the rectangle) and an optional
// change { id, value } come out. The component never stores the truth: the value comes in through the state struct, and after a change the owner writes the new value back.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Events (single finger): touchDown (x, y, nowMs) / touchMove (x, y) / touchUp (x, y) in canvas pixels, and turn (delta) for the hardware knob (Q-Link) assigned to the component. A
// component that got a touch down keeps the gesture until the touch up, even when the finger leaves its rectangle. Each call returns a Change (kind None: nothing happened).
// `id` is an opaque integer the OWNER of the component gave to the value (setId): the component never knows what a parameter is.
// Drawing: draw (SceneBuilder&) emits ops only inside the rectangle: no allocation, no locks, no I/O, the same state gives the same ops.
// The style comes from theme.h: every colour, line width, font size, radius and spacing is a number of the active look (three tables); the components never carry one of their own.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../overlay/gpu/scene.h"
#include "../overlay/sdf_font.h"
#include "model.h"
#include "theme.h"
#include "q_visual_policy.h"

namespace synthyboy_mpc { namespace ui {

namespace gpu = synthyboy_mpc::overlay::gpu;
namespace sdf = synthyboy_mpc::overlay::sdf;

constexpr float kPi = 3.14159265358979323846f;
/** The strip above a Q group member that holds its index chip (Pen::qIndex): the vertical gap between two stacked members is at least this. */
constexpr int kQLane = 16;

namespace colour
{
constexpr uint32_t rgba (uint32_t rgb, uint32_t a = 255) noexcept { return (rgb << 8) | a; }
// the colours of the components, from the active look (theme.h): the names the components and the tests use
inline uint32_t background() noexcept { return theme::L().bg1; }
inline uint32_t panel() noexcept { return theme::L().panel1; }
inline uint32_t rest() noexcept { return theme::L().keyline; }
inline uint32_t accent() noexcept { return theme::L().flow[0]; }
inline uint32_t text() noexcept { return theme::L().textDim; }
inline uint32_t white() noexcept { return theme::L().text; }
inline uint32_t q() noexcept { return theme::L().q; }
}

inline uint32_t ModEntry::colour() const noexcept { return theme::sourceColour (macro ? "MAC" : name); }

/** The tab (a labelled chip with the name of the source at the top left of the control) and the rectangle that starts a depth gesture: 56 x 52 pixels where the control allows it. */
inline Rect tabRect (const Rect& r, const char* name = "") noexcept
{
    const float tw = name[0] != '\0' ? sdf::textWidth (name, 11.0f) : 24.0f;
    const int w = std::clamp (static_cast<int> (std::ceil (tw)) + 10, 36, 56);
    return { r.x + 2, r.y + 2, std::min (w, std::max (0, r.w - 4)), std::min (18, std::max (0, r.h - 4)) };
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** A cached cell of the scene around the drawing of a part of the picture (scene.h, CellBegin): the helper keeps what the cell drew from one picture to the next and draws it again only when its content
    (or its rectangle) changed. The rectangle must contain everything drawn inside (`pad` pixels more are added). The state ops inside must be the cell's own (it starts and ends with the default style and paint). */
struct Cell
{
    Cell (gpu::SceneBuilder& sb, const Rect& r, int pad = 2) : sb_ (sb) { sb_.cellBegin (static_cast<float> (r.x - pad), static_cast<float> (r.y - pad), static_cast<float> (r.right() + pad), static_cast<float> (r.bottom() + pad)); }
    ~Cell() { sb_.cellEnd(); }
    Cell (const Cell&) = delete;
    Cell& operator= (const Cell&) = delete;

private:
    gpu::SceneBuilder& sb_;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
/** The drawing pen of a component: scene ops with the colours of a disabled component dimmed. The component draws through it and keeps inside its rectangle by construction (tests check). */
class Pen
{
public:
    /** `inactive`: the INACTIVE look of the theme (docs/ui-components/theme.md): every colour goes to a grey of its own brightness, a little darker (kInactivePercent); the component stays operable (a switched-off oscillator's controls). One rule here,
        used by every component that draws through a pen. */
    Pen (gpu::SceneBuilder& sb, bool enabled, bool inactive = false) : sb_ (sb), dim_ (enabled ? 255 : 150), grey_ (inactive) {}
    static constexpr uint32_t kInactivePercent = 70;
    /** The grey of a colour in the inactive look (0xRRGGBBAA in, the same alpha out). */
    static uint32_t inactiveGrey (uint32_t rgba) noexcept
    {
        const uint32_t r = (rgba >> 24) & 255u, g = (rgba >> 16) & 255u, b = (rgba >> 8) & 255u, luma = (r * 30u + g * 59u + b * 11u) / 100u, v = luma * kInactivePercent / 100u;
        return (v << 24) | (v << 16) | (v << 8) | (rgba & 255u);
    }

    /** The colour `rgba` (0xRRGGBBAA) with the alpha of a disabled component scaled down, and grey in the inactive look. */
    uint32_t c (uint32_t rgba) const noexcept { if (grey_) rgba = inactiveGrey (rgba); return (rgba & 0xffffff00u) | (((rgba & 255u) * dim_ + 127u) / 255u); }
    uint32_t c (uint32_t rgb, int alpha) const noexcept { return c (colour::rgba (rgb, static_cast<uint32_t> (alpha))); }

    void rect (int x0, int y0, int x1, int y1, uint32_t col) { if (x1 > x0 && y1 > y0) sb_.rect (static_cast<float> (x0), static_cast<float> (y0), static_cast<float> (x1), static_cast<float> (y1), col); }
    void rect (const Rect& r, uint32_t col) { rect (r.x, r.y, r.right(), r.bottom(), col); }
    void frame (const Rect& r, int t, uint32_t col) { if (! r.empty()) sb_.frame (static_cast<float> (r.x), static_cast<float> (r.y), static_cast<float> (r.right()), static_cast<float> (r.bottom()), static_cast<float> (t), col); }
    void line (float x0, float y0, float x1, float y1, float width, uint32_t col) { sb_.line (x0, y0, x1, y1, width, col); }
    void arc (float cx, float cy, float radius, float a0, float a1, float width, uint32_t col) { if (a1 - a0 > 0.004f || a0 - a1 > 0.004f) sb_.arc (cx, cy, radius, a0, a1, width, col); }
    void disc (float cx, float cy, float r, uint32_t col) { sb_.disc (cx, cy, r, col); }

    // ---- the look: the state ops of the scene (feather, caps, gradient paint) and the shapes made with them. Every function that sets a state puts it back.
    const theme::Look& look() const noexcept { return theme::L(); }
    /** The feather (pixels; 1 = plain) and the caps of the following line-like ops. */
    void style (float soft, bool roundCaps) { sb_.style (soft, roundCaps); }
    void plain() { sb_.style (1.0f, false); sb_.paintOff(); }
    void paintOff() { sb_.paintOff(); }
    /** A four colour gradient (equal stops, 0xRRGGBB and one alpha) along x between the columns x0 .. x1, along y between the rows, or along the angle of the next arc. */
    void paintX (float x0, float x1, const uint32_t rgb[4], int alpha) { sb_.paintX (x0, x1, c (rgb[0], alpha), c (rgb[1], alpha), c (rgb[2], alpha), c (rgb[3], alpha)); }
    void paintY (float y0, float y1, const uint32_t rgb[4], int alpha) { sb_.paintY (y0, y1, c (rgb[0], alpha), c (rgb[1], alpha), c (rgb[2], alpha), c (rgb[3], alpha)); }
    void paintArc (const uint32_t rgb[4], int alpha) { sb_.paintArc (c (rgb[0], alpha), c (rgb[1], alpha), c (rgb[2], alpha), c (rgb[3], alpha)); }
    /** A two colour gradient (the colours repeated into four stops). */
    void paintY2 (float y0, float y1, uint32_t a, uint32_t b, int alpha) { sb_.paintY (y0, y1, c (a, alpha), c (a, alpha), c (b, alpha), c (b, alpha)); }
    void paintX2 (float x0, float x1, uint32_t a, uint32_t b, int alpha) { sb_.paintX (x0, x1, c (a, alpha), c (a, alpha), c (b, alpha), c (b, alpha)); }
    void rrect (const Rect& r, float radius, uint32_t col, float outline = 0.0f) { if (! r.empty()) sb_.rrect (static_cast<float> (r.x), static_cast<float> (r.y), static_cast<float> (r.right()), static_cast<float> (r.bottom()), radius, col, outline); }
    /** A block (panel): rounded, a vertical gradient fill, a keyline; `hi` = the keyline of a focused block. */
    void block (const Rect& r, bool hi = false)
    {
        const theme::Look& t = theme::L();
        paintY2 (static_cast<float> (r.y), static_cast<float> (r.bottom()), t.panel0, t.panel1, 255);
        rrect (r, t.rBlock, c (colour::rgba (t.panel1, 255)));
        sb_.paintOff();
        rrect (r, t.rBlock, c (colour::rgba (hi ? t.keylineHi : t.keyline, 255)), t.frame);
    }
    /** A string with the pen at (x, baseline), `size` pixels per em (the font of sdf_font.h; any length, UTF-8). */
    void text (float x, float baseline, float size, const char* s, uint32_t col, float tracking = 0.0f) { if (s[0] != '\0' && size > 0.0f) sb_.text (x, baseline, size, s, col, tracking); }
    static float textWidth (const char* s, float size) noexcept { return sdf::textWidth (s, size); }

    /** The size (pixels per em) at which `s` fits into maxW x maxH: the largest <= maxSize whose width and height fit; the height of the text is that of the capitals and the lower tails (0.89 em).
        Below minSize the size stays minSize (the text is then cut with an ellipsis by the drawing functions). 0 when not even minSize fits the HEIGHT. */
    static float fitSize (const char* s, float maxW, float maxH, float maxSize, float minSize) noexcept
    {
        float size = std::min (maxSize, maxH / 0.89f);
        if (size < minSize)
            return 0.0f;
        const float w = sdf::textWidth (s, size);
        if (w > maxW && w > 0.0f)
            size = std::max (minSize, size * maxW / w);
        return size;
    }
    /** The text centred in the box (both ways, the capitals centred) at the largest size <= maxSize that fits (never below minSize: then the end is cut with "..."); the size used (0: nothing is drawn). */
    float textCentered (const Rect& box, const char* s, float maxSize, uint32_t col, float minSize = 8.0f)
    {
        const float size = fitSize (s, static_cast<float> (box.w), static_cast<float> (box.h), maxSize, minSize);
        if (size <= 0.0f || s[0] == '\0')
            return 0.0f;
        char buf[96];
        const float w = sdf::fitText (s, size, static_cast<float> (box.w), buf, static_cast<int> (sizeof buf));
        text (static_cast<float> (box.x) + (static_cast<float> (box.w) - w) * 0.5f, std::round (sdf::baselineForCentre (static_cast<float> (box.y) + static_cast<float> (box.h) * 0.5f, size)), size, buf, col);
        return size;
    }
    /** The text at the left edge of the box, vertically centred. */
    float textLeft (const Rect& box, const char* s, float maxSize, uint32_t col, float minSize = 8.0f)
    {
        const float size = fitSize (s, static_cast<float> (box.w), static_cast<float> (box.h), maxSize, minSize);
        if (size <= 0.0f || s[0] == '\0')
            return 0.0f;
        char buf[96];
        sdf::fitText (s, size, static_cast<float> (box.w), buf, static_cast<int> (sizeof buf));
        text (static_cast<float> (box.x), std::round (sdf::baselineForCentre (static_cast<float> (box.y) + static_cast<float> (box.h) * 0.5f, size)), size, buf, col);
        return size;
    }
    /** The text at the right edge of the box. */
    float textRight (const Rect& box, const char* s, float maxSize, uint32_t col, float minSize = 8.0f)
    {
        const float size = fitSize (s, static_cast<float> (box.w), static_cast<float> (box.h), maxSize, minSize);
        if (size <= 0.0f || s[0] == '\0')
            return 0.0f;
        char buf[96];
        const float w = sdf::fitText (s, size, static_cast<float> (box.w), buf, static_cast<int> (sizeof buf));
        text (static_cast<float> (box.right()) - w, std::round (sdf::baselineForCentre (static_cast<float> (box.y) + static_cast<float> (box.h) * 0.5f, size)), size, buf, col);
        return size;
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
    }
    void iconEnvelope (float cx, float cy, uint32_t col)   // an ADSR outline
    {
        const float x[6] = { -13.0f, -8.0f, -3.0f, 3.0f, 8.0f, 13.0f }, y[6] = { 8.0f, -8.0f, -2.0f, -2.0f, 8.0f, 8.0f };
        for (int i = 0; i < 5; ++i)
            line (cx + x[i], cy + y[i], cx + x[i + 1], cy + y[i + 1], 2.0f, col);
    }
    /** Place the marker on the top edge, clear of the highlighted control's interior. */
    static constexpr Rect qMarkerRect (const Rect& frame) noexcept
    {
#if SYNTHYBOY_MPC_QFRAME_ENABLED
        return { frame.x + 2, std::max (0, frame.y - 10), 18, 17 };
#else
        (void) frame;
        return {};
#endif
    }
    /** The active Q-Link frame and its single unnumbered marker. */
    void qFocus (const Rect& frame)
    {
#if SYNTHYBOY_MPC_QFRAME_ENABLED
        const theme::Look& T = theme::L();
        rrect (frame, T.rControl, c (T.q, 255), 2.0f);
        const Rect b = qMarkerRect (frame);
        rrect (b, T.rChip, c (colour::rgba (T.q, 255)));
        textCentered (b, "Q", 13.0f, c (colour::rgba (T.onBright, 255)), 8.0f);
#else
        (void) frame;
#endif
    }

private:
    gpu::SceneBuilder& sb_;
    uint32_t dim_;
    bool grey_ = false;
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
/** The base of every drawable/touchable overlay component: extends ControlModel with drawing (draw / drawBody), touch gesture capture (touchDown / touchMove / touchUp / tick),
    and widget tree traversal (touchTargets / collectLeaves / primaryChild). */
class Component : public ControlModel
{
public:
    Component() = default;
    ~Component() override = default;
    Component (const Component&) = delete;
    Component& operator= (const Component&) = delete;

    void setEnabled (bool e) override { enabled_ = e; if (! e) captured_ = false; }
    /** The smallest size at which the component is legible with its touch targets (the layout must give at least this). */
    Size minSize() const override = 0;

    // ---- the output: the drawing
    void setHostPainted (bool on) noexcept { hostPainted_ = on; }
    virtual void draw (gpu::SceneBuilder& sb) const
    {
        if (hostPainted_)
            return;
        Pen pen (sb, enabled_, inactive_);
        Cell cell (sb, rect_);
        drawBody (pen);   // the index of a Q group member is drawn by its group (QGroup::draw), on the frame
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
    virtual Change touchMove (int x, int y, double nowMs) { return captured_ && enabled_ ? onMove (x, y, nowMs) : Change::none(); }
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
    bool capturing() const noexcept { return captured_; }

    // ---- structure
    /** The rectangles that react to a touch (a stepper: its two halves). */
    virtual int touchTargets (Rect* out, int max) const { if (max > 0) { out[0] = rect_; return 1; } return 0; }
    /** The child a hardware knob turns when this component is a member of a Q group (null: the component itself; a composite: its primary child). */
    virtual Component* primaryChild() const { return nullptr; }
    ControlModel* primaryModel() const override { return primaryChild(); }

protected:
    virtual void drawBody (Pen& pen) const = 0;
    virtual Change onDown (int, int, double) { return Change::none(); }
    virtual Change onMove (int, int) { return Change::none(); }
    virtual Change onMove (int x, int y, double /*nowMs*/) { return onMove (x, y); }
    virtual Change onUp (int, int) { return Change::none(); }

    bool captured_ = false, hostPainted_ = false;
};

/** Marks a ControlModel host-painted if it is a drawable Component; no-op for pure models. */
inline void setHostPainted (ControlModel* m, bool on) noexcept
{
    if (auto* c = dynamic_cast<Component*> (m))
        c->setHostPainted (on);
}

}} // namespace synthyboy_mpc::ui
