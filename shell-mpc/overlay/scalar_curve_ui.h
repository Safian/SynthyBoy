// SynthyBoy - MPC overlay: the page logic of the NOTE / VELOCITY curve editor (ui/scalar_curve_editor.h), step 5 of the Env / Filter / LFO merges. It is NOT a registered page: the Filter tab (step 4) holds one
// ScalarCurveUi per source as its NOTE and VELO states and calls pull / handle / tick / draw like the other Ui classes.
// Read: content().velocityCurve / noteCurve (the points), curve.velocity.legato / curve.note.legato, master.portamento.time (the read-only GLIDE chip), the 64 mod slots (USED BY).
// Written: the points through IInstanceView::setScalarCurve (CONTENT, an empty vector = the identity; at most every kSendMs during a gesture and at its end, like the LFO shape), the legato key through writeParam.
// Nothing else: slot edits stay on the TARGETS page (a Targets event is a Request to the page owner), master.velocity_track stays on the Master page.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "../q_router.h"
#include "../ui/scalar_curve_editor.h"
#include "instance_view.h"
#include "ui_bind.h"

namespace synthyboy_mpc { namespace overlay {

class ScalarCurveUi
{
public:
    static constexpr double kSendMs = 90.0, kHoldMs = 600.0;   // the throttle of the writes (kShapeSendMs of the LFO), how long the old content is ignored after a write (the display thread applies it a moment later)
    static constexpr int kVirtualCount = 7;                    // the virtual Q targets: 0 POINT, 1 X, 2 Y, 3 CURVE, 4 GRID X, 5 GRID Y, 6 SNAP (LEGATO is the parameter)

    ui::ScalarCurveEditor screen;
    /** What the owner has to do after handle (): open the TARGETS page of this source, or toggle expand. */
    bool targetsRequested = false;
    bool expandRequested = false;

    /** which: 0 velocity, 1 note. */
    void configure (int which)
    {
        which_ = which == 1 ? 1 : 0;
        screen.setSource (which_ == 1 ? ui::scurve::Source::Note : ui::scurve::Source::Velocity);
        legatoParam_ = uibind::indexOfKey (which_ == 1 ? "curve.note.legato" : "curve.velocity.legato");
        glideParam_ = uibind::indexOfKey ("master.portamento.time");
    }
    int which() const noexcept { return which_; }
    int source() const noexcept { return which_ == 1 ? 24 : 23; }   // the modulation source id (core/src/modulation.cpp): velocity 23, note 24
    int legatoParam() const noexcept { return legatoParam_; }

    /** The points of a content curve as the editor's points / the content points of a model (an identity is the EMPTY vector). */
    static std::vector<ui::scurve::Pt> ptsOf (const std::vector<synthyboy::ContentPoint>& c)
    {
        std::vector<ui::scurve::Pt> o;
        for (const synthyboy::ContentPoint& p : c)
            o.push_back ({ p.x, p.y, p.power });
        return o;
    }
    static std::vector<synthyboy::ContentPoint> contentOf (const ui::scurve::ScalarCurve& m)
    {
        std::vector<synthyboy::ContentPoint> o;
        if (m.isIdentity())
            return o;
        for (int i = 0; i < m.n; ++i)
            o.push_back (synthyboy::ContentPoint (m.p[i].x, m.p[i].y, m.p[i].power));
        return o;
    }

    /** The slots that use the source (primary or AUX): one scan of the 64 slots (the rule of uibind::usedSourcesOf: a slot counts with a source AND a destination; bypassed slots count and are marked). */
    static void scanUsedBy (const IInstanceView& v, int src, ui::ScalarCurveEditor::UsedBy& out)
    {
        out = ui::ScalarCurveEditor::UsedBy {};
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0)
                continue;
            const long source = std::lround (v.param (sp.source)), dest = std::lround (v.param (sp.destination));
            const long aux = sp.auxSource >= 0 ? std::lround (v.param (sp.auxSource)) : 0;
            if (source <= 0 || dest <= 0 || (source != src && aux != src))
                continue;
            ++out.total;
            if (out.count >= ui::ScalarCurveEditor::kMaxRows)
                continue;
            ui::ScalarCurveEditor::UsedRow& r = out.rows[out.count++];
            const int pi = synthyboy::modDestinationParamIndex (static_cast<size_t> (dest));
            std::snprintf (r.dest, sizeof r.dest, "%s", pi >= 0 ? uibind::info (pi).name : "?");
            r.aux = source != src;
            r.percent = sp.amount >= 0 ? static_cast<int> (std::lround (v.param (sp.amount))) : 0;
            r.bipolar = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            r.bypass = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
        }
    }

    /** Reads the instance into the screen (every update of the tab). While a finger is down on the graph, and for kHoldMs after a write, the content is not read back (the editor holds the truth of a gesture). */
    void pull (IInstanceView& v, double nowMs)
    {
        lastNowMs_ = nowMs;
        const int played = v.lastPlayed (which_ == 1 ? 1 : 0);   // the key / velocity of the last note-on: the live marker (animated layer only)
        screen.setMarker (played >= 0 ? static_cast<float> (std::min (played, ui::scurve::kKeys)) / static_cast<float> (ui::scurve::kKeys) : -1.0f);
        screen.setLegato (legatoParam_ >= 0 && v.param (legatoParam_) >= 0.5f);
        if (glideParam_ >= 0)
        {
            char t[16];
            uibind::formatValue (uibind::info (glideParam_), v.param (glideParam_), t, sizeof t);
            screen.setGlide (t);
        }
        ui::ScalarCurveEditor::UsedBy u;
        scanUsedBy (v, source(), u);
        screen.setUsedBy (u);
        if (! screen.canvas.capturing() && ! dirty_ && nowMs >= holdUntilMs_)
        {
            const std::shared_ptr<const synthyboy::StateContent> c = v.content();
            const std::vector<ui::scurve::Pt> pts = c ? ptsOf (which_ == 1 ? c->noteCurve : c->velocityCurve) : std::vector<ui::scurve::Pt> {};
            ui::scurve::ScalarCurve shown;
            shown.set (pts.data(), static_cast<int> (pts.size()));
            if (! (shown == screen.canvas.model))
                screen.setCurve (pts.data(), static_cast<int> (pts.size()));
        }
        screen.sync();
    }

    /** One change of a part of the screen: edits the model and writes. */
    void handle (const ui::Change& c, IInstanceView& v, double nowMs)
    {
        const ui::ScalarCurveEvent e = screen.apply (c);
        switch (e.kind)
        {
            case ui::ScalarCurveEvent::Curve:
                dirty_ = true;
                if (e.final || nowMs - sentMs_ >= kSendMs)
                    flush (v, nowMs);
                break;
            case ui::ScalarCurveEvent::Legato:
                if (legatoParam_ >= 0)
                    uibind::writePlain (v, legatoParam_, e.value);
                break;
            case ui::ScalarCurveEvent::Targets: targetsRequested = true; break;
            case ui::ScalarCurveEvent::Expand: expandRequested = true; break;
            default: break;
        }
    }
    /** The clock (every frame): the long press of the canvas, the throttled write that was left over. */
    void tick (IInstanceView& v, double nowMs)
    {
        lastNowMs_ = nowMs;
        handle (screen.canvas.tick (nowMs), v, nowMs);
        if (dirty_ && (nowMs - sentMs_ >= kSendMs || ! screen.canvas.capturing()))
            flush (v, nowMs);
    }
    bool dirty() const noexcept { return dirty_; }
    void flush (IInstanceView& v, double nowMs)
    {
        if (! dirty_)
            return;
        dirty_ = false;
        sentMs_ = nowMs;
        holdUntilMs_ = nowMs + kHoldMs;
        v.setScalarCurve (which_, contentOf (screen.canvas.model));
    }

    // ---- the Q groups: Q1 [POINT, X, Y, CURVE] = virtual 0 .. 3, Q2 [LEGATO, GRID X, GRID Y, SNAP] = the legato parameter, virtual 4 .. 6
    /** The Q targets of group g in slot order (table index, or QRouter::encode (id, kVirtual); -1 none). */
    void qTargets (int g, int t[4]) const noexcept
    {
        for (int i = 0; i < 4; ++i)
            t[i] = -1;
        if (g == 0)
            for (int i = 0; i < 4; ++i)
                t[i] = QRouter::encode (i, QRouter::kVirtual);
        else if (g == 1)
        {
            t[0] = legatoParam_;
            for (int i = 0; i < 3; ++i)
                t[1 + i] = QRouter::encode (4 + i, QRouter::kVirtual);
        }
    }
    float virtualValue (int id) const noexcept
    {
        const ui::scurve::ScalarCurve& m = screen.canvas.model;
        const int sel = std::clamp (screen.canvas.state.selected, 0, m.n - 1);
        auto grid = [] (int g) { return static_cast<float> (g - ui::lfo::kMinGrid) / static_cast<float> (ui::lfo::kMaxGrid - ui::lfo::kMinGrid); };
        switch (id)
        {
            case 0: return m.n > 1 ? static_cast<float> (sel) / static_cast<float> (m.n - 1) : 0.0f;
            case 1: return m.p[sel].x;
            case 2: return m.p[sel].y;
            case 3: return sel + 1 < m.n ? (m.p[sel].power + ui::scurve::kMaxPower) / (2.0f * ui::scurve::kMaxPower) : 0.5f;
            case 4: return grid (screen.canvas.gridXc());
            case 5: return grid (screen.canvas.gridYc());
            default: return screen.canvas.state.snap ? 1.0f : 0.0f;
        }
    }
    void applyVirtual (int id, float x, IInstanceView& v, double nowMs)
    {
        ui::scurve::ScalarCurve& m = screen.canvas.model;
        ui::ScalarCurveCanvas& cv = screen.canvas;
        const int sel = std::clamp (cv.state.selected, 0, m.n - 1);
        x = std::clamp (x, 0.0f, 1.0f);
        auto gridOf = [] (float n) { return std::clamp (ui::lfo::kMinGrid + static_cast<int> (std::lround (n * static_cast<float> (ui::lfo::kMaxGrid - ui::lfo::kMinGrid))), ui::lfo::kMinGrid, ui::lfo::kMaxGrid); };
        bool changed = false;
        switch (id)
        {
            case 0: cv.select (static_cast<int> (std::lround (x * static_cast<float> (std::max (1, m.n - 1))))); break;
            case 1: changed = cv.opMove (sel, x, m.p[sel].y); break;
            case 2: changed = cv.opMove (sel, m.p[sel].x, x); break;
            case 3: changed = cv.opPower (sel, std::round (x * 2.0f * ui::scurve::kMaxPower - ui::scurve::kMaxPower)); break;
            case 4: cv.state.gridX = gridOf (x); break;
            case 5: cv.state.gridY = gridOf (x); break;
            default: cv.state.snap = x >= 0.5f; break;
        }
        screen.sync();
        if (changed)
        {
            dirty_ = true;
            flush (v, nowMs);
        }
    }
    /** The virtual targets: the host's writes in, the values out (every update of the tab). */
    void updateVirtuals (IInstanceView& v, double nowMs)
    {
        for (int id = 0; id < kVirtualCount; ++id)
        {
            float x;
            if (v.takeQVirtual (id, x))
                applyVirtual (id, x, v, nowMs);
        }
        for (int id = 0; id < kVirtualCount; ++id)
            v.setQVirtual (id, virtualValue (id));
    }

private:
    int which_ = 0, legatoParam_ = -1, glideParam_ = -1;
    bool dirty_ = false;
    double sentMs_ = -1.0e9, holdUntilMs_ = 0.0, lastNowMs_ = 0.0;
};

}} // namespace synthyboy_mpc::overlay
