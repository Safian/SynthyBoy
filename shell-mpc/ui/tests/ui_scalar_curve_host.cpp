// SynthyBoy - MPC UI component library tests: the NOTE / VELOCITY curve editor (ui/scalar_curve_editor.h). The model against the engine's own evaluation (ContentHub / ShapeData), the editing operations and their limits
// (2 .. 64 points, ends fixed, y 0 .. 1, x ascending, bend range), bounded undo, the gestures of the canvas (drag, tap adds, segment drag bends, long press deletes), snap, the number fields, the Q members, the layout inside the
// rectangle, and the drawing (the curve and the handles are in the ops, the drawn curve is the engine's). Host only; links the core.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../scalar_curve_editor.h"
#include "voice/own_content.h"

using namespace uit;
namespace sc = synthyboy_mpc::ui::scurve;

namespace
{
bool near (float a, float b, float tol = 1.0e-5f) { return std::fabs (a - b) <= tol; }

/** The engine's reading of a content curve: ContentHub::publish (the code the plugin runs), then Voice::curvedSource's own evaluateTable. */
struct Engine
{
    mutable synthyboy::own::ContentHub hub;
    const synthyboy::own::OwnContent* content = nullptr;
    explicit Engine (const std::vector<synthyboy::ContentPoint>& pts, bool note = false)
    {
        synthyboy::StateContent c;
        (note ? c.noteCurve : c.velocityCurve) = pts;
        hub.publish (c);
        content = hub.acquire().content;
    }
    float at (float x, bool note = false) const
    {
        const auto& cc = note ? content->noteCurve : content->velocityCurve;
        if (! cc.present)
            return std::clamp (x, 0.0f, 1.0f);
        return std::clamp (synthyboy::shapes::ShapeData::evaluateTable (cc.table.data(), cc.table.size(), std::clamp (x, 0.0f, 1.0f)), 0.0f, 1.0f);
    }
};
std::vector<synthyboy::ContentPoint> contentOf (const sc::ScalarCurve& m)
{
    std::vector<synthyboy::ContentPoint> o;
    if (m.isIdentity())
        return o;
    for (int i = 0; i < m.n; ++i)
        o.push_back (synthyboy::ContentPoint (m.p[i].x, m.p[i].y, m.p[i].power));
    return o;
}

/** The NOTE axis is named by the ONE key-name function of the library (note_mask.h keyName): the MPC convention, key 60 = C3, key 0 = C-2, key 127 = G8. */
void testAxisLabels()
{
    char n[8];
    keyName (60, n, sizeof n);
    check (std::string (n) == "C3", "key 60 is C3 (the MPC convention)");
    keyName (0, n, sizeof n);
    check (std::string (n) == "C-2", "key 0 is C-2");
    keyName (127, n, sizeof n);
    check (std::string (n) == "G8", "key 127 is G8");
    keyName (61, n, sizeof n);
    check (std::string (n) == "C#3", "key 61 is C#3");
    keyName (-5, n, sizeof n);
    check (std::string (n) == "C-2", "a key below 0 is clamped to C-2");
    keyName (200, n, sizeof n);
    check (std::string (n) == "G8", "a key above 127 is clamped to G8");
    check (std::string (scurve::axisLabel (scurve::Source::Note, 0)) == "C-2" && std::string (scurve::axisLabel (scurve::Source::Note, 1)) == "C1" && std::string (scurve::axisLabel (scurve::Source::Note, 2)) == "C3"
           && std::string (scurve::axisLabel (scurve::Source::Note, 3)) == "C5" && std::string (scurve::axisLabel (scurve::Source::Note, 4)) == "G8", "the NOTE axis: C-2, C1, C3, C5, G8");
    check (std::string (scurve::axisLabel (scurve::Source::Velocity, 0)) == "0" && std::string (scurve::axisLabel (scurve::Source::Velocity, 2)) == "64" && std::string (scurve::axisLabel (scurve::Source::Velocity, 4)) == "127", "the VELO axis: 0, 32, 64, 96, 127");
    // the middle label of the axis is the key 60 of the engine's scale (x = 60 / 127)
    check (scurve::axisValue (scurve::Source::Note, 2) == 60, "the middle place of the NOTE axis is key 60");
    for (int k = 0; k < 5; ++k)
    {
        char want[8];
        keyName (scurve::axisValue (scurve::Source::Note, k), want, sizeof want);
        check (std::string (scurve::axisLabel (scurve::Source::Note, k)) == want, "the label of place " + std::to_string (k) + " is keyName of its key");
    }
}

/** The live marker is drawn by drawLive ONLY: the cached picture (draw) is the same with and without it, so a new note repaints no cell; it is a vertical line from the axis to the curve and a dot on the curve. */
void testLiveMarker()
{
    ScalarCurveEditor ed;
    ed.place ({ 0, 0, 1280, 556 });
    const scurve::Pt pts[3] = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.8f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    ed.setCurve (pts, 3);
    auto ops = [&] (bool live, bool marker)
    {
        ed.setMarker (marker ? 60.0f / 127.0f : -1.0f);
        auto data = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*data);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        ed.draw (sb);
        const uint32_t staticOps = data->numOps;
        if (live)
            ed.drawLive (sb);
        return std::make_pair (staticOps, data->numOps);
    };
    const auto none = ops (true, false), with = ops (true, true);
    check (none.first == none.second, "no marker: drawLive adds nothing");
    check (with.first == none.first && with.second > with.first, "a marker: the cached picture has the same ops, drawLive adds the line and the dot (" + std::to_string (with.second - with.first) + " ops)");
    const auto cached = ops (false, true), cachedNone = ops (false, false);
    check (cached.second == cachedNone.second, "draw alone does not know the marker");
    // the dot is on the curve at x = 60 / 127: the engine's own evaluation
    ed.setMarker (60.0f / 127.0f);
    const float y = ed.canvas.model.at (60.0f / 127.0f);
    const int px = ed.canvas.pxOf (60.0f / 127.0f), py = ed.canvas.pyOf (y);
    check (ed.canvas.plot().contains (px, py) && y > 0.5f && y < 0.8f, "the marker is inside the plot, on the curve between its points (y " + std::to_string (y) + ")");
    ed.setMarker (-1.0f);
}

void testModel()
{
    sc::ScalarCurve m;
    check (m.isIdentity() && m.n == 2, "a new curve is the identity (two points)");
    check (near (m.at (0.3f), 0.3f) && near (m.at (-1.0f), 0.0f) && near (m.at (2.0f), 1.0f), "the identity reads x, clamped to 0 .. 1");
    // ends: only y moves
    check (m.move (0, 0.5f, 0.2f) && m.p[0].x == 0.0f && near (m.p[0].y, 0.2f), "the first point keeps x = 0, y moves");
    check (m.move (1, 0.2f, 0.5f) && m.p[1].x == 1.0f && m.move (1, 0.2f, 5.0f) && m.p[1].x == 1.0f && m.p[1].y == 1.0f, "the last point keeps x = 1, y clamps to 1");
    check (! m.isIdentity(), "a moved end makes the curve custom");
    m.identity();
    // insert: on the curve, inherits the segment power
    check (m.setPower (0, 4.0f), "bend the segment");
    const float yExpect = m.at (0.5f);
    const int at = m.insert (0.5f);
    check (at == 1 && m.n == 3 && near (m.p[1].x, 0.5f) && near (m.p[1].y, yExpect) && m.p[1].power == 4.0f, "insert: a point ON the curve at that x, the segment power is inherited");
    check (m.insert (0.505f) == -1 && m.n == 3, "insert within the minimum gap of a point is refused");
    // move between neighbours with the gap
    check (m.move (1, 5.0f, 0.5f) && near (m.p[1].x, 1.0f - sc::kMinGap) && m.p[1].y == 0.5f, "a middle point stops one gap before its right neighbour");
    check (m.move (1, -5.0f, 0.5f) && near (m.p[1].x, sc::kMinGap), "... and one gap after its left neighbour");
    check (! m.move (1, m.p[1].x, m.p[1].y), "a move that changes nothing reports nothing");
    // bend range
    check (m.setPower (0, 99.0f) && m.p[0].power == sc::kMaxPower && m.setPower (0, -99.0f) && m.p[0].power == -sc::kMaxPower, "power clamps to +-20");
    check (! m.setPower (2, 1.0f) && ! m.setPower (-1, 1.0f), "the last point has no segment after it");
    // delete
    check (! m.remove (0) && ! m.remove (2), "the ends are never deleted");
    check (m.remove (1) && m.n == 2, "a middle point is deleted");
    check (! m.remove (1) && m.n == 2, "never below two points");
    // limits: 64 points
    m.identity();
    int added = 0;
    for (int i = 1; i < 200; ++i)
        if (m.insert (static_cast<float> (i) / 200.0f) >= 0)
            ++added;
    check (m.n == sc::kMaxPoints && added == sc::kMaxPoints - 2, "the curve stops at 64 points (62 added)");
    check (m.insert (0.4999f) == -1, "no 65th point");
    bool asc = true;
    for (int i = 1; i < m.n; ++i)
        asc = asc && m.p[i].x >= m.p[i - 1].x;
    check (asc, "x stays ascending");
    // bendTo: the middle of the segment reaches the height
    m.identity();
    m.move (1, 1.0f, 1.0f);
    check (m.bendTo (0, 0.25f) && near (m.at (0.5f), 0.25f, 0.01f), "bendTo: the middle of the segment reaches the finger's height");
    m.identity();
    m.move (0, 0.0f, 0.5f); m.move (1, 1.0f, 0.5f);
    check (! m.bendable (0) && ! m.bendTo (0, 0.9f), "a flat segment cannot be bent");
    // set(): the engine's sanitize (first x 0, last x 1, sorted), fewer than two points = the identity
    const sc::Pt raw[3] = { { 0.2f, 0.1f, 0.0f }, { 0.9f, 0.5f, 30.0f }, { 0.5f, 0.7f, 0.0f } };
    m.set (raw, 3);
    check (m.n == 3 && m.p[0].x == 0.0f && m.p[2].x == 1.0f && near (m.p[1].x, 0.5f) && m.p[1].power == 0.0f && m.p[2].power == sc::kMaxPower, "set: sorted (power clamped to 20), the first x = 0, the last x = 1");
    m.set (raw, 1);
    check (m.isIdentity(), "set with fewer than 2 points: the identity (what an empty content curve is)");
}

void testEngineIdentity()
{
    // the model's value is the engine's: random curves, 129 samples, both sources
    unsigned seed = 7;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return static_cast<float> ((seed >> 8) & 0xffff) / 65535.0f; };
    int bad = 0, total = 0;
    for (int trial = 0; trial < 40; ++trial)
    {
        sc::ScalarCurve m;
        const int pts = 2 + trial % 20;
        sc::Pt raw[64];
        for (int i = 0; i < pts; ++i)
            raw[i] = { i == 0 ? 0.0f : (i == pts - 1 ? 1.0f : rnd()), rnd(), (rnd() - 0.5f) * 40.0f };
        m.set (raw, pts);
        const Engine e (contentOf (m), trial % 2 == 1);
        for (int k = 0; k <= 128; ++k)
        {
            const float x = static_cast<float> (k) / 128.0f;
            bad += m.at (x) != e.at (x, trial % 2 == 1);
            ++total;
        }
    }
    check (bad == 0, "the model's value equals the engine's (ContentHub table + evaluateTable) at " + std::to_string (total) + " sample points, bit for bit");
    sc::ScalarCurve id;
    const Engine e0 ({});
    bool same = true;
    for (int k = 0; k <= 127; ++k)
        same = same && id.at (static_cast<float> (k) / 127.0f) == e0.at (static_cast<float> (k) / 127.0f);
    check (same && contentOf (id).empty(), "the identity is the EMPTY content curve and reads x like the engine's absent curve");
}

void testUndo()
{
    sc::History h;
    sc::ScalarCurve m;
    for (int i = 0; i < sc::kUndoDepth + 10; ++i)
    {
        sc::Snapshot s = m.snapshot();
        h.push (s);
        m.insert (0.01f + 0.015f * static_cast<float> (i));
    }
    check (h.depth() == sc::kUndoDepth, "the history is bounded to " + std::to_string (sc::kUndoDepth));
    int n = 0;
    sc::Snapshot s;
    while (h.pop (s))
        ++n;
    check (n == sc::kUndoDepth && ! h.pop (s), "undo steps back exactly that far");
    sc::History h2;
    h2.push (m.snapshot()); h2.push (m.snapshot());
    check (h2.depth() == 1, "an identical state on top is not kept twice");
}

// ---- the canvas
struct Rig
{
    ScalarCurveCanvas cv;
    double t = 1000.0;
    Rig() { cv.place ({ 20, 20, 760, 420 }); cv.setId (5); }
    int px (float x) const { return cv.pxOf (x); }
    int py (float y) const { return cv.pyOf (y); }
    Change tap (int x, int y) { const Change d = cv.touchDown (x, y, t); const Change u = cv.touchUp (x, y); t += 1000.0; return u ? u : d; }
};

void testGestures()
{
    {   // a tap on the empty graph adds a point ON the curve at that x
        Rig r;
        const Change c = r.tap (r.px (0.5f), r.py (0.9f));
        check (c.kind == ChangeKind::Index && c.id == 5 && c.index == ScalarCurveCanvas::kEditEnd, "tap on the empty graph: one kEditEnd");
        check (r.cv.model.n == 3 && near (r.cv.model.p[1].x, 0.5f, 0.01f) && near (r.cv.model.p[1].y, r.cv.model.p[1].x, 0.01f), "... the point is on the curve (y of the identity there), not at the finger");
        check (r.cv.state.selected == 1, "... and it is selected");
        // a tap on a handle selects it and adds nothing
        const Change s = r.tap (r.px (0.0f), r.py (0.0f));
        check (s.kind == ChangeKind::Index && s.index == ScalarCurveCanvas::kSelect && r.cv.model.n == 3 && r.cv.state.selected == 0, "tap on a handle: it is selected, nothing is added");
        check (r.tap (r.px (0.0f), r.py (0.0f)).kind == ChangeKind::None, "tap on the selected handle: nothing");
    }
    {   // drag a middle point: the handle follows with its offset, clamped between the neighbours
        Rig r;
        r.cv.model.insert (0.5f);
        r.cv.select (1);
        const int x0 = r.px (0.5f) + 3, y0 = r.py (0.5f) + 2;   // the finger is a little off the centre: no jump
        r.cv.touchDown (x0, y0, r.t);
        const Change m = r.cv.touchMove (x0 + 100, y0 - 100);
        check (m.kind == ChangeKind::Index && m.index == ScalarCurveCanvas::kEdit, "drag: kEdit while moving");
        const float ex = r.cv.xFrom (r.px (0.5f) + 100), ey = r.cv.yFrom (r.py (0.5f) - 100);
        check (near (r.cv.model.p[1].x, ex, 0.003f) && near (r.cv.model.p[1].y, ey, 0.003f), "the point follows the finger (offset kept)");
        r.cv.touchMove (r.px (5.0f), r.py (0.5f));
        check (r.cv.model.p[1].x < 1.0f && near (r.cv.model.p[1].x, 1.0f - sc::kMinGap), "dragged past the neighbour: one gap before it");
        const Change e = r.cv.touchUp (x0, y0);
        check (e.kind == ChangeKind::Index && e.index == ScalarCurveCanvas::kEditEnd, "touch up: kEditEnd");
        check (r.cv.undo.depth() == 1, "one gesture = one undo step");
        check (r.cv.opUndo() && near (r.cv.model.p[1].x, 0.5f, 1.0e-6f), "undo restores the start of the gesture");
    }
    {   // an end point moves in y only
        Rig r;
        r.cv.touchDown (r.px (0.0f), r.py (0.0f), r.t);
        r.cv.touchMove (r.px (0.5f), r.py (0.4f));
        r.cv.touchUp (0, 0);
        check (r.cv.model.p[0].x == 0.0f && near (r.cv.model.p[0].y, 0.4f, 0.01f), "an end point drags in y, x stays");
    }
    {   // a vertical drag on a segment bends its power
        Rig r;
        r.cv.touchDown (r.px (0.5f), r.py (0.5f) + 30, r.t);   // off the curve's own handle: the segment's middle is 30 px away
        const Change b = r.cv.touchMove (r.px (0.5f), r.py (0.2f));
        check (b.kind == ChangeKind::Index && b.index == ScalarCurveCanvas::kEdit && r.cv.model.p[0].power > 1.0f, "vertical drag below the curve: the segment bends (power > 0, convex)");
        check (near (r.cv.model.at (0.5f), 0.2f, 0.02f), "the middle of the segment is where the finger is");
        r.cv.touchUp (0, 0);
        check (r.cv.model.n == 2 && r.cv.undo.depth() == 1, "no point was added by the bend; one undo step");
        Rig h;
        h.cv.touchDown (h.px (0.5f), h.py (0.5f) + 40, h.t);
        const Change none = h.cv.touchMove (h.px (0.9f), h.py (0.5f) + 40);
        check (none.kind == ChangeKind::None && h.cv.model.isIdentity(), "a horizontal drag on the empty graph does nothing");
    }
    {   // long press deletes a middle point, never an end
        Rig r;
        r.cv.model.insert (0.5f);
        r.cv.touchDown (r.px (0.5f), r.py (0.5f), r.t);
        check (r.cv.tick (r.t + 300.0).kind == ChangeKind::None, "short of 600 ms: nothing");
        const Change c = r.cv.tick (r.t + 650.0);
        check (c.kind == ChangeKind::Index && c.index == ScalarCurveCanvas::kEditEnd && r.cv.model.n == 2, "long press on a middle point deletes it");
        check (r.cv.touchUp (0, 0).kind == ChangeKind::None, "the release after the long press does nothing");
        Rig e;
        e.cv.touchDown (e.px (0.0f), e.py (0.0f), e.t);
        check (e.cv.tick (e.t + 700.0).kind == ChangeKind::None && e.cv.model.n == 2, "long press on an end point: nothing");
    }
    {   // SNAP: x and y of a dragged point and x of an added point go to the grid
        Rig r;
        r.cv.state.snap = true; r.cv.state.gridX = 4; r.cv.state.gridY = 4;
        r.cv.model.insert (0.5f);
        r.cv.select (1);
        r.cv.touchDown (r.px (0.5f), r.py (0.5f), r.t);
        r.cv.touchMove (r.px (0.62f), r.py (0.77f));
        r.cv.touchUp (0, 0);
        check (near (r.cv.model.p[1].x, 0.5f, 1.0e-4f) && near (r.cv.model.p[1].y, 0.75f, 1.0e-4f), "snap: the dragged point sits on the grid (x 0.62 -> 0.5, y 0.77 -> 0.75)");
        Rig a;
        a.cv.state.snap = true; a.cv.state.gridX = 4;
        a.tap (a.px (0.3f), a.py (0.9f));
        check (a.cv.model.n == 3 && near (a.cv.model.p[1].x, 0.25f, 1.0e-4f), "snap: an added point's x goes to the grid");
    }
    {   // limits through the gesture: the 65th point is not added
        Rig r;
        for (int i = 1; i < 63; ++i)
            r.cv.model.insert (static_cast<float> (i) / 63.0f);
        check (r.cv.model.n == 64, "64 points");
        const Change c = r.tap (r.px (0.5f) + 3, r.py (0.5f) + 60);
        check (r.cv.model.n == 64 && c.kind != ChangeKind::Index, "a tap at 64 points adds nothing and reports no edit");
    }
}

// ---- the screen
void testScreen()
{
    ScalarCurveEditor ed;
    ed.place ({ 0, 0, 1280, 556 });
    check (ed.sizeOk() && ed.minSize().w == 1100, "the screen fits 1280 x 556 (minimum 1100 x 500)");
    // every leaf inside the rectangle, the touch targets at least 56 px (the canvas is big), nothing overlaps
    Component* leaves[64];
    const int n = ed.collectLeaves (leaves, 0, 64);
    int outside = 0, small = 0, overlap = 0;
    for (int i = 0; i < n; ++i)
    {
        outside += ! ed.rect().contains (leaves[i]->rect());
        small += leaves[i]->rect().w < 56 || leaves[i]->rect().h < 56;
        for (int k = i + 1; k < n; ++k)
            overlap += leaves[i]->rect().overlaps (leaves[k]->rect());
    }
    check (n == 2 && outside == 0 && small == 0 && overlap == 0, "native editor keeps only canvas and USED BY as GPU leaves (" + std::to_string (n) + " parts)");
    for (int g = 0; g < ScalarCurveEditor::kMaxGroups; ++g)
    {
        const Component* first = nullptr;
        check (ed.groups[g].check (leaves, n, &first) == 0, std::string ("Q group ") + ScalarCurveEditor::groupName (g) + ": adjacent, no foreign part inside its frame");
    }
    check (ed.qMember (0, 0) == &ed.point && ed.qMember (0, 1) == &ed.xField && ed.qMember (0, 2) == &ed.yField && ed.qMember (0, 3) == &ed.curveField, "Q1 = [POINT, X, Y, CURVE]");
    check (ed.qMember (1, 0) == &ed.legato && ed.qMember (1, 1) == &ed.gridX && ed.qMember (1, 2) == &ed.gridY && ed.qMember (1, 3) == &ed.snap, "Q2 = [LEGATO, GRID X, GRID Y, SNAP]");
    check (ed.canvas.rect().w >= 640 && ed.canvas.rect().h >= 300, "the graph is at least 640 x 300");
    // the fields follow the model
    ed.canvas.model.insert (0.5f);
    ed.canvas.select (1);
    ed.sync();
    check (ed.point.state.value == 2.0 && ed.point.state.max == 3.0 && ed.xField.state.value == 64.0 && ed.yField.state.value == 50.0 && ! ed.xField.state.readOnly, "POINT 2 of 3, X 64 (0.5 * 127), Y 50%");
    ed.canvas.select (0);
    ed.sync();
    check (ed.xField.state.readOnly && ! ed.curveField.state.readOnly, "an end point: X is read only");
    ed.canvas.select (2);
    ed.sync();
    check (ed.curveField.state.readOnly, "the last point: CURVE is read only");
    // number fields edit the model and ask for a write
    ed.canvas.select (1);
    ed.sync();
    ScalarCurveEvent ev = ed.apply (Change::indexed (ScalarCurveEditor::kIdY, 80, 80.0));
    check (ev.kind == ScalarCurveEvent::Curve && ev.final && near (ed.canvas.model.p[1].y, 0.8f), "Y field: the point moves, a write at once");
    ev = ed.apply (Change::indexed (ScalarCurveEditor::kIdX, 100, 100.0));
    check (ev.kind == ScalarCurveEvent::Curve && near (ed.canvas.model.p[1].x, 100.0f / 127.0f), "X field: the point moves in x");
    ev = ed.apply (Change::indexed (ScalarCurveEditor::kIdX, 127, 127.0));
    check (near (ed.canvas.model.p[1].x, 1.0f - sc::kMinGap), "X field past the neighbour: clamped one gap before it");
    ev = ed.apply (Change::indexed (ScalarCurveEditor::kIdCurve, 0, 3.5));
    check (ev.kind == ScalarCurveEvent::Curve && ed.canvas.model.p[1].power == 3.5f, "CURVE field: the segment power");
    ev = ed.apply (Change::indexed (ScalarCurveEditor::kIdPoint, 3, 3.0));
    check (ev.kind == ScalarCurveEvent::None && ed.canvas.state.selected == 2, "POINT field: selects (no write)");
    ev = ed.apply (Change::indexed (ScalarCurveEditor::kIdGridX, 8, 8.0));
    ed.apply (Change::indexed (ScalarCurveEditor::kIdGridY, 6, 6.0));
    ed.apply (Change::indexed (ScalarCurveEditor::kIdSnap, 1, 1.0));
    check (ev.kind == ScalarCurveEvent::None && ed.canvas.state.gridX == 8 && ed.canvas.state.gridY == 6 && ed.canvas.state.snap && ed.snap.state.on, "GRID X / GRID Y / SNAP: view settings, no write");
    ev = ed.apply (Change::indexed (ScalarCurveEditor::kIdLegato, 1, 1.0));
    check (ev.kind == ScalarCurveEvent::Legato && ev.value == 1 && ! ev.final, "LEGATO toggle: a Legato event with the value");
    ev = ed.apply (Change::request (ScalarCurveEditor::kIdTargets));
    check (ev.kind == ScalarCurveEvent::Targets, "TARGETS: a request to the page owner");
    // buttons
    ed.canvas.select (1);
    ev = ed.apply (Change::request (ScalarCurveEditor::kIdDelete));
    check (ev.kind == ScalarCurveEvent::Curve && ed.canvas.model.n == 2, "DELETE POINT deletes the selected middle point");
    ev = ed.apply (Change::request (ScalarCurveEditor::kIdDelete));
    check (ev.kind == ScalarCurveEvent::None, "DELETE POINT on an end point: nothing");
    ev = ed.apply (Change::request (ScalarCurveEditor::kIdUndo));
    check (ev.kind == ScalarCurveEvent::Curve && ed.canvas.model.n == 3, "UNDO brings the point back");
    ev = ed.apply (Change::request (ScalarCurveEditor::kIdInit));
    check (ev.kind == ScalarCurveEvent::Curve && ed.canvas.model.isIdentity(), "INIT: the identity (written as the empty curve)");
    ev = ed.apply (Change::request (ScalarCurveEditor::kIdInit));
    check (ev.kind == ScalarCurveEvent::None, "INIT on the identity: nothing");
    ev = ed.apply (Change::request (ScalarCurveEditor::kIdUndo));
    check (ev.kind == ScalarCurveEvent::Curve && ed.canvas.model.n == 3, "UNDO after INIT brings the curve back");
    // a touch on a block makes its Q group the active one
    check (ed.activeGroup() == 0, "group EDIT is active first");
    ed.touchDown (ed.gridX.rect().x + 10, ed.gridX.rect().y + 10, 0.0);
    ed.touchUp (0, 0);
    check (ed.activeGroup() == 1, "a touch on the second block activates its Q group");
}

// ---- the drawing
struct OpScan
{
    int discsR10 = 0, discsAll = 0, lines = 0;
    std::vector<std::pair<float, float>> handleCentres;
};
OpScan scan (const Scene& s, const ScalarCurveCanvas& cv)
{
    OpScan o;
    const gpu::SceneData& d = *s.data;
    for (uint32_t k = 0; k < d.numOps; ++k)
    {
        const gpu::SceneOp& op = d.ops[k];
        if (op.op == gpu::kOpDisc)
        {
            ++o.discsAll;
            if (std::fabs (op.f[2] - static_cast<float> (ScalarCurveCanvas::kHandleR)) < 0.01f)
            {
                ++o.discsR10;
                o.handleCentres.push_back ({ op.f[0], op.f[1] });
            }
        }
        else if (op.op == gpu::kOpLine)
            ++o.lines;
    }
    (void) cv;
    return o;
}

void testDrawing()
{
    ScalarCurveCanvas cv;
    cv.place ({ 20, 20, 760, 420 });
    cv.model.insert (0.25f);
    cv.model.insert (0.7f);
    cv.model.setPower (1, 5.0f);
    cv.model.move (2, 0.7f, 0.4f);
    for (int src = 0; src < 2; ++src)
    {
        cv.state.source = src == 0 ? sc::Source::Velocity : sc::Source::Note;
        Scene s (800, 460, kBackgroundRgba);
        cv.draw (s.builder());
        const Image im = render (s);
        check (! s.overflow() && drawnOutside (im, cv.rect(), backgroundPixel()) == 0, std::string (src == 0 ? "VELO" : "NOTE") + ": drawn inside its rectangle (" + std::to_string (s.numOps()) + " ops)");
        const OpScan o = scan (s, cv);
        check (o.discsR10 == cv.model.n, std::string (src == 0 ? "VELO" : "NOTE") + ": one 10 px handle per point (" + std::to_string (o.discsR10) + ")");
        bool atPoints = true;
        for (int i = 0; i < cv.model.n; ++i)
        {
            bool found = false;
            for (const auto& c : o.handleCentres)
                found = found || (std::fabs (c.first - static_cast<float> (cv.pxOf (cv.model.p[i].x))) < 0.6f && std::fabs (c.second - static_cast<float> (cv.pyOf (cv.model.p[i].y))) < 0.6f);
            atPoints = atPoints && found;
        }
        check (atPoints, "the handles are at the points' pixel positions");
        // the curve polyline: the line ops that are not grid lines have the curve's colour; every 4 px column is on the engine's value
        int onCurve = 0, columns = 0;
        const gpu::SceneData& d = *s.data;
        const Engine e (contentOf (cv.model), src == 1);
        const uint32_t col = cv.lineColour();
        for (uint32_t k = 0; k < d.numOps; ++k)
        {
            const gpu::SceneOp& op = d.ops[k];
            if (op.op != gpu::kOpLine || std::fabs (op.f[4] - theme::L().curve) > 1.0e-4f)
                continue;
            ++columns;
            const float xn = cv.xFrom (static_cast<int> (std::lround (op.f[2])));
            const float yEnd = static_cast<float> (cv.pyOf (e.at (xn, src == 1)));
            onCurve += std::fabs (yEnd - op.f[3]) < 0.51f;
        }
        (void) col;
        check (columns > 150 && onCurve == columns, "the drawn curve is the engine's: " + std::to_string (onCurve) + " of " + std::to_string (columns) + " segments end on the engine value");
        writePng (outDir() + (src == 0 ? "/scalar_curve_canvas_velo.png" : "/scalar_curve_canvas_note.png"), im, 1);
    }
    // the identity draws no diagonal helper, a custom curve does; both stay inside
    ScalarCurveCanvas id;
    id.place ({ 20, 20, 640, 300 });
    Scene s0 (700, 340, kBackgroundRgba), s1 (700, 340, kBackgroundRgba);
    id.draw (s0.builder());
    id.model.move (1, 1.0f, 0.5f);
    id.draw (s1.builder());
    check (scan (s1, id).lines == scan (s0, id).lines + 1, "a custom curve adds the faint identity diagonal");
    check (drawnOutside (render (s0), id.rect(), backgroundPixel()) == 0 && drawnOutside (render (s1), id.rect(), backgroundPixel()) == 0, "at the minimum size 640 x 300 everything is inside");
}

void testScreenDrawing()
{
    ScalarCurveEditor ed;
    ed.place ({ 10, 10, 1280, 556 });
    ed.setSource (sc::Source::Note);
    ed.setLegato (true);
    ed.setGlide ("120 ms");
    ScalarCurveEditor::UsedBy u;
    const char* names[3] = { "Osc A Level", "Filter 1 Cutoff", "Amp Pan" };
    for (int i = 0; i < 3; ++i)
    {
        std::snprintf (u.rows[i].dest, sizeof u.rows[i].dest, "%s", names[i]);
        u.rows[i].percent = 35 - 20 * i; u.rows[i].bipolar = i == 1; u.rows[i].aux = i == 2;
    }
    u.count = u.total = 3;
    ed.setUsedBy (u);
    Scene s (1300, 580, kBackgroundRgba);
    ed.draw (s.builder());
    const Image im = render (s);
    check (! s.overflow() && drawnOutside (im, ed.rect(), backgroundPixel()) == 0, "screen with legato, glide chip and three rows: drawn inside its rectangle (" + std::to_string (s.numOps()) + " ops)");
    writePng (outDir() + "/scalar_curve_screen.png", im, 1);
    ed.setLegato (false);
    u = ScalarCurveEditor::UsedBy {};
    ed.setUsedBy (u);
    Scene e (1300, 580, kBackgroundRgba);
    ed.draw (e.builder());
    check (drawnOutside (render (e), ed.rect(), backgroundPixel()) == 0, "empty routing and legato off: drawn inside its rectangle");
}
void testCompactScreen()
{
    ScalarCurveEditor ed;
    ed.setCompact (true);
    check (ed.minSize().w == 480 && ed.minSize().h == 500, "compact mode lowers minSize to fit 40% split panel (480x500)");
    ed.place ({ 771, 16, 501, 604 });
    ed.setSource (sc::Source::Velocity);
    ed.setLegato (true);
    const Rect b = ed.rect();
    Component* leaves[64];
    const int n = ed.collectLeaves (leaves, 0, 64);
    int badSize = 0, overlap = 0;
    for (int i = 0; i < n; ++i)
    {
        const Rect r = leaves[i]->rect();
        if (! b.contains (r) || r.w < 56 || r.h < 56)
            ++badSize;
        for (int j = i + 1; j < n; ++j)
            if (r.overlaps (leaves[j]->rect()))
                ++overlap;
    }
    check (n == 1 && badSize == 0 && overlap == 0, "compact native curve keeps only the real canvas as a GPU leaf");
    const Component* first = nullptr;
    check (ed.groups[0].check (leaves, n, &first) == 0, "compact split Q1 EDIT encloses POINT/X/Y/CURVE with zero foreign overlap");
    check (ed.targets.rect().y == 16 && ed.targets.rect().x >= 771 + 365, "compact split header reserves top-left 365x56 for NOTE|VELO selector");
    const Rect ec = ed.expandBtn.rect();
    const int cx = ec.x + ec.w / 2, cy = ec.y + ec.h / 2;
    const Change ch = ed.touchDown (cx, cy, 100.0);
    const Change chUp = ed.touchUp (cx, cy);
    const auto ev = ed.apply (ch.kind != ChangeKind::None ? ch : chUp);
    check (ev.kind == ScalarCurveEvent::None, "overlay tap on native expand coordinates has no fallback button");
    check (ed.apply (Change::request (ScalarCurveEditor::kIdExpand)).kind == ScalarCurveEvent::Expand, "acknowledged native expand action still reaches the owner");

    Scene s (1280, 628, kBackgroundRgba);
    ed.draw (s.builder());
    check (! s.overflow() && drawnOutside (render (s), b, backgroundPixel()) == 0, "compact split curve draws strictly inside its bounds");

    ed.setCompact (false);
    check (ed.minSize().w == 1100 && ed.minSize().h == 500, "disabling compact restores full 1100x500 ScalarCurveEditor");
}
}

int main()
{
    testAxisLabels();
    testLiveMarker();
    testModel();
    testEngineIdentity();
    testUndo();
    testGestures();
    testScreen();
    testDrawing();
    testScreenDrawing();
    testCompactScreen();
    std::printf ("ui_scalar_curve_host: %s (%d failures)\n", g_fails == 0 ? "OK" : "FAILED", g_fails);
    return g_fails == 0 ? 0 : 1;
}
