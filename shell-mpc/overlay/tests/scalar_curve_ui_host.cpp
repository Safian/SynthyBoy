// SynthyBoy - MPC overlay tests: the page logic of the NOTE / VELO curve editor (overlay/scalar_curve_ui.h): the USED BY list (empty and used presets, aux, bypass, more than the rows shown), the content read back, the
// writes (the curve as CONTENT through setScalarCurve with the throttle and the hold, the identity as the empty vector, the legato key through writeParam, nothing else), the glide chip, the Q targets and virtual values, and the
// round trip of the curve content through the state document (set points, save, load, the engine's evaluation matches the editor's).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "demo_common.h"
#include "../scalar_curve_ui.h"
#include "synthyboy/state.h"
#include "voice/own_content.h"

using namespace uit;
namespace ui = synthyboy_mpc::ui;
namespace sc = synthyboy_mpc::ui::scurve;
using synthyboy_mpc::overlay::ScalarCurveUi;
using synthyboy_mpc::QRouter;

namespace
{
bool near (float a, float b, float tol = 1.0e-5f) { return std::fabs (a - b) <= tol; }
std::shared_ptr<const synthyboy::StateContent> contentWith (const std::vector<synthyboy::ContentPoint>& vel, const std::vector<synthyboy::ContentPoint>& note)
{
    auto c = std::make_shared<synthyboy::StateContent>();
    c->velocityCurve = vel;
    c->noteCurve = note;
    return c;
}
const std::vector<synthyboy::ContentPoint> kThree = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.8f, 3.0f }, { 1.0f, 1.0f, 0.0f } };
}

int main()
{
    // ---- USED BY
    {
        demo::Inst inst;
        ScalarCurveUi ui;
        ui.configure (0);
        ui.pull (inst, 1000.0);
        check (ui.screen.used.total == 0 && ui.screen.used.count == 0, "an empty preset: nothing uses the velocity (the list says NOT ROUTED)");
        check (ui.source() == 23, "VELO is the modulation source 23");
        inst.mod (1, 23, "master.volume", 20.0f);
        inst.mod (2, 24, "master.volume", 30.0f);                      // the NOTE source: not in the VELO list
        inst.mod (3, 23, "master.volume", -40.0f, true, true);              // bipolar, bypassed: still listed
        inst.mod (4, 5, "master.volume", 10.0f);
        inst.set ("mod04.aux_source", 23.0f);                            // used as the AUX source of a slot with source and destination
        inst.set ("mod05.source", 0.0f); inst.set ("mod05.aux_source", 23.0f);   // an aux of a slot without source: does not count
        ui.pull (inst, 1100.0);
        const auto& u = ui.screen.used;
        check (u.total == 3 && u.count == 3, "three slots use the velocity: two as the source, one as the AUX (the empty slot's aux and the NOTE slot do not count)");
        check (! u.rows[0].aux && u.rows[0].percent == 20 && ! u.rows[0].bipolar && ! u.rows[0].bypass, "row 1: depth +20, unipolar");
        check (! u.rows[1].aux && u.rows[1].percent == -40 && u.rows[1].bipolar && u.rows[1].bypass, "row 2: depth -40, bipolar, bypassed");
        check (u.rows[2].aux && std::string (u.rows[2].dest).size() > 0, "row 3: the AUX use, with a destination name (" + std::string (u.rows[2].dest) + ")");
        ScalarCurveUi note;
        note.configure (1);
        note.pull (inst, 1200.0);
        check (note.source() == 24 && note.screen.used.total == 1 && std::fabs (note.screen.used.rows[0].percent - 30) < 0.5, "NOTE (source 24): its one slot, depth +30");
        for (int i = 6; i <= 20; ++i)
            inst.mod (i, 23, "master.volume", static_cast<float> (i));
        ui.pull (inst, 1300.0);
        check (ui.screen.used.total == 3 + 15 && ui.screen.used.count == ui::ScalarCurveEditor::kMaxRows, "more slots than rows: the total counts all, the rows hold " + std::to_string (ui::ScalarCurveEditor::kMaxRows) + " (the list shows 4 and '+ n more')");
    }
    // ---- the live marker: the x of the key / velocity played last (key / 127, velocity / 127), none until a note was played
    {
        demo::Inst inst;
        ScalarCurveUi velo, note;
        velo.configure (0);
        note.configure (1);
        velo.pull (inst, 1000.0);
        note.pull (inst, 1000.0);
        check (velo.screen.canvas.state.marker < 0.0f && note.screen.canvas.state.marker < 0.0f, "no note played: no marker");
        inst.lastKey = 60;
        inst.lastVel = 127;
        velo.pull (inst, 1100.0);
        note.pull (inst, 1100.0);
        check (near (note.screen.canvas.state.marker, 60.0f / 127.0f) && near (velo.screen.canvas.state.marker, 1.0f), "NOTE: the x of the key (60 / 127), VELO: the x of the velocity (127 / 127)");
        inst.lastKey = 0;
        inst.lastVel = 1;
        note.pull (inst, 1200.0);
        velo.pull (inst, 1200.0);
        check (near (note.screen.canvas.state.marker, 0.0f) && near (velo.screen.canvas.state.marker, 1.0f / 127.0f), "key 0 is x 0, velocity 1 is 1 / 127");
        check (inst.writes == 0 && inst.scalarWrites == 0, "the marker writes nothing");
    }
    // ---- the content is read back; the legato key and the glide chip
    {
        demo::Inst inst;
        inst.contentV = contentWith (kThree, {});
        inst.set ("curve.velocity.legato", 1.0f);
        inst.set ("master.portamento.time", 120.0f);
        ScalarCurveUi ui;
        ui.configure (0);
        ui.pull (inst, 1000.0);
        check (ui.screen.canvas.model.n == 3 && near (ui.screen.canvas.model.p[1].y, 0.8f) && ui.screen.canvas.model.p[1].power == 3.0f, "the velocity curve of the content is in the editor");
        check (ui.screen.legatoOn() && std::string (ui.screen.glideText).find ("120") != std::string::npos, std::string ("LEGATO on, the glide chip text from master.portamento.time: '") + ui.screen.glideText + "'");
        ScalarCurveUi note;
        note.configure (1);
        note.pull (inst, 1000.0);
        check (note.screen.canvas.model.isIdentity() && ! note.screen.legatoOn(), "the NOTE editor of the same instance: the identity (no content), legato off");
        check (inst.scalarWrites == 0 && inst.writes == 0, "pulling writes nothing");
    }
    // ---- writes: the curve is CONTENT, written throttled and at the end of a gesture; the identity is the empty vector
    {
        demo::Inst inst;
        inst.contentV = contentWith (kThree, {});
        ScalarCurveUi ui;
        ui.configure (0);
        ui.screen.place ({ 0, 0, 1280, 556 });
        ui.pull (inst, 1000.0);
        ui::ScalarCurveCanvas& cv = ui.screen.canvas;
        // drag the middle point: edits arrive every move, the writes at most every 90 ms, the last one at the end
        double t = 2000.0;
        cv.touchDown (cv.pxOf (0.5f), cv.pyOf (0.8f), t);
        int edits = 0;
        for (int i = 1; i <= 30; ++i)
        {
            t += 10.0;
            const ui::Change c = cv.touchMove (cv.pxOf (0.5f) + 2 * i, cv.pyOf (0.8f));
            if (c)
            {
                ++edits;
                ui.handle (c, inst, t);
            }
        }
        const int midWrites = inst.scalarWrites;
        check (edits >= 20 && midWrites >= 2 && midWrites <= 4, "a 300 ms drag: " + std::to_string (edits) + " edits, only " + std::to_string (midWrites) + " writes (throttled to one per 90 ms)");
        ui.handle (cv.touchUp (cv.pxOf (0.5f) + 60, cv.pyOf (0.8f)), inst, t + 10.0);
        check (inst.scalarWrites == midWrites + 1 && inst.scalarCurve[0].size() == 3, "the end of the gesture writes the final curve at once");
        check (near (inst.scalarCurve[0][1].x, cv.model.p[1].x) && near (inst.scalarCurve[0][1].y, 0.8f, 0.01f) && inst.scalarCurve[0][1].power == 3.0f, "... with x, y and the power of the model");
        check (inst.scalarCurve[1].empty() && inst.writes == 0, "no write for the other source, no engine parameter written for the points");
        // the old content is not read back while the new one is on its way (kHoldMs), but after it
        ui.pull (inst, t + 100.0);
        check (near (cv.model.p[1].x, inst.scalarCurve[0][1].x), "a pull right after the write keeps the edited curve (the content is stale)");
        inst.contentV = contentWith (inst.scalarCurve[0], {});
        ui.pull (inst, t + 2000.0);
        check (near (cv.model.p[1].x, inst.scalarCurve[0][1].x), "the plugin's content is the same curve a moment later");
        // INIT writes the empty vector
        const int before = inst.scalarWrites;
        ui.handle (ui::Change::request (ui::ScalarCurveEditor::kIdInit), inst, t + 3000.0);
        check (inst.scalarWrites == before + 1 && inst.scalarCurve[0].empty(), "INIT: the identity is written as the EMPTY vector");
        // a tap on the empty graph adds a point and writes at once; UNDO brings the old curve back through a write
        ui.screen.canvas.touchDown (cv.pxOf (0.5f), cv.pyOf (0.9f), t + 4000.0);
        ui.handle (cv.touchUp (cv.pxOf (0.5f), cv.pyOf (0.9f)), inst, t + 4000.0);
        check (inst.scalarCurve[0].size() == 3 && near (inst.scalarCurve[0][1].y, 0.5f, 0.01f), "a tap adds a point on the identity: a 3 point curve is written");
        ui.handle (ui::Change::request (ui::ScalarCurveEditor::kIdUndo), inst, t + 5000.0);
        check (inst.scalarCurve[0].empty(), "UNDO writes the previous curve (the identity again)");
        // the NOTE editor writes slot 1, never slot 0
        ScalarCurveUi note;
        note.configure (1);
        note.screen.place ({ 0, 0, 1280, 556 });
        note.pull (inst, 0.0);
        note.handle (ui::Change::indexed (ui::ScalarCurveEditor::kIdY, 60, 60.0), inst, 10.0);
        check (note.screen.canvas.model.isIdentity() || inst.scalarCurve[1].size() >= 2, "NOTE writes its own content slot");
        // legato: the parameter, nothing else
        const int sw = inst.scalarWrites;
        ui.handle (ui::Change::indexed (ui::ScalarCurveEditor::kIdLegato, 1, 1.0), inst, t + 6000.0);
        check (inst.get ("curve.velocity.legato") == 1.0f && inst.get ("curve.note.legato") == 0.0f && inst.scalarWrites == sw, "LEGATO writes curve.velocity.legato only");
        ScalarCurveUi note2;
        note2.configure (1);
        note2.handle (ui::Change::indexed (ui::ScalarCurveEditor::kIdLegato, 1, 1.0), inst, 0.0);
        check (inst.get ("curve.note.legato") == 1.0f, "the NOTE editor writes curve.note.legato");
        // TARGETS is a request to the owner, no write
        const int w = inst.writes;
        ui.targetsRequested = false;
        ui.handle (ui::Change::request (ui::ScalarCurveEditor::kIdTargets), inst, 0.0);
        check (ui.targetsRequested && inst.writes == w, "TARGETS: a request to the page owner, no write");
    }
    // ---- the long press through the clock writes the deletion
    {
        demo::Inst inst;
        inst.contentV = contentWith (kThree, {});
        ScalarCurveUi ui;
        ui.configure (0);
        ui.screen.place ({ 0, 0, 1280, 556 });
        ui.pull (inst, 1000.0);
        ui::ScalarCurveCanvas& cv = ui.screen.canvas;
        cv.touchDown (cv.pxOf (0.5f), cv.pyOf (0.8f), 5000.0);
        ui.tick (inst, 5300.0);
        check (inst.scalarWrites == 0, "300 ms: nothing");
        ui.tick (inst, 5700.0);
        check (inst.scalarWrites == 1 && inst.scalarCurve[0].empty(), "a long press on a middle point deletes it; the two points left are the identity: the empty curve is written");
    }
    // ---- the Q groups
    {
        demo::Inst inst;
        ScalarCurveUi ui;
        ui.configure (1);
        int t0[4], t1[4];
        ui.qTargets (0, t0);
        ui.qTargets (1, t1);
        bool virt0 = true;
        for (int i = 0; i < 4; ++i)
            virt0 = virt0 && QRouter::modeOf (t0[i]) == QRouter::kVirtual;
        check (virt0, "Q1 [POINT, X, Y, CURVE]: four virtual targets (the points are content, not parameters)");
        check (t1[0] == ui.legatoParam() && ui.legatoParam() >= 0 && QRouter::modeOf (t1[1]) == QRouter::kVirtual && QRouter::modeOf (t1[3]) == QRouter::kVirtual, "Q2 [LEGATO, GRID X, GRID Y, SNAP]: LEGATO is the parameter curve.note.legato, the rest virtual");
        // a Q turn on X moves the selected point and writes
        ui.screen.place ({ 0, 0, 1280, 556 });
        ui.screen.canvas.model.insert (0.5f);
        ui.screen.canvas.select (1);
        ui.screen.sync();
        inst.setVirtualIn (1, 0.25f);
        ui.updateVirtuals (inst, 100.0);
        check (near (ui.screen.canvas.model.p[1].x, 0.25f) && inst.scalarWrites == 1 && inst.scalarCurve[1].size() == 3, "virtual X: the selected point moves, the curve is written");
        check (near (inst.virtualOut[1], 0.25f) && near (inst.virtualOut[2], ui.screen.canvas.model.p[1].y), "the virtual values are reported back (X, Y)");
        inst.setVirtualIn (6, 1.0f);
        ui.updateVirtuals (inst, 200.0);
        check (ui.screen.canvas.state.snap && inst.scalarWrites == 1, "virtual SNAP: a view setting, no write");
        inst.setVirtualIn (4, 1.0f);
        ui.updateVirtuals (inst, 300.0);
        check (ui.screen.canvas.gridXc() == 32, "virtual GRID X: 0..1 -> 2..32");
    }
    // ---- the curve content round trip through the state document, and the engine's evaluation
    {
        ui::scurve::ScalarCurve m;
        m.insert (0.3f); m.insert (0.8f);
        m.move (1, 0.3f, 0.6f); m.move (2, 0.8f, 0.7f);
        m.setPower (0, 4.0f); m.setPower (1, -6.5f);
        synthyboy::StateContent c;
        c.velocityCurve = ScalarCurveUi::contentOf (m);
        ui::scurve::ScalarCurve n;
        n.insert (0.5f); n.move (1, 0.5f, 0.2f);
        c.noteCurve = ScalarCurveUi::contentOf (n);
        check (synthyboy::validateContent (c) && c.velocityCurve.size() == 4 && c.noteCurve.size() == 3, "the editor's content passes the content validation");
        synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
        const std::string doc = synthyboy::saveState (values, c);
        synthyboy::ParamValues back = synthyboy::ParamValues::defaults();
        synthyboy::StateContent got;
        const synthyboy::LoadResult r = synthyboy::loadStateWithContent (doc.data(), doc.size(), back, got);
        check (r.status == synthyboy::LoadStatus::ok && r.content == synthyboy::ContentStatus::ok && got == c, "save -> load: the curve content round-trips exactly");
        const std::string doc2 = synthyboy::saveState (back, got);
        check (doc == doc2, "a second save is byte-identical");
        synthyboy::own::ContentHub hub;
        hub.publish (got);
        const synthyboy::own::OwnContent* content = hub.acquire().content;
        auto engineAt = [] (const synthyboy::own::CompiledModCurve& cc, float x) { return cc.present ? std::clamp (synthyboy::shapes::ShapeData::evaluateTable (cc.table.data(), cc.table.size(), x), 0.0f, 1.0f) : x; };
        int bad = 0;
        sc::ScalarCurve shownV, shownN;
        const auto pv = ScalarCurveUi::ptsOf (got.velocityCurve), pn = ScalarCurveUi::ptsOf (got.noteCurve);
        shownV.set (pv.data(), static_cast<int> (pv.size()));
        shownN.set (pn.data(), static_cast<int> (pn.size()));
        for (int k = 0; k <= 127; ++k)
        {
            const float x = static_cast<float> (k) / 127.0f;
            bad += engineAt (content->velocityCurve, x) != shownV.at (x);
            bad += engineAt (content->noteCurve, x) != shownN.at (x);
        }
        check (bad == 0, "after the round trip the engine's evaluation of both curves equals the editor's at all 128 keys / velocities");
        // an empty curve is not written into the document at all and loads back as the identity
        synthyboy::StateContent e;
        const std::string de = synthyboy::saveState (values, e);
        synthyboy::StateContent ge;
        synthyboy::ParamValues pb = synthyboy::ParamValues::defaults();
        synthyboy::loadStateWithContent (de.data(), de.size(), pb, ge);
        check (ge.velocityCurve.empty() && ge.noteCurve.empty(), "the identity (empty) stays empty through the document");
    }
    std::printf ("scalar_curve_ui_host: %s (%d failures)\n", g_fails == 0 ? "OK" : "FAILED", g_fails);
    return g_fails == 0 ? 0 : 1;
}
