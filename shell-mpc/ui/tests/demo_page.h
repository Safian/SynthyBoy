// SynthyBoy - MPC UI component library tests: a DEMONSTRATION page of an oscillator (header with the type selector, the tuning row, the TypeBar under the display, the lower knob panel, the
// Q groups) built only from the library. It is test code: the real arrangement of the page on the screen is decided elsewhere; this one exists to show the components together and to
// prove that the first Q group of the specification (type, source, PAN, LEVEL) can be made adjacent in a layout.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui_test.h"

namespace uit {

struct OscPage
{
    ToggleIcon power;
    PictogramSelector type;
    Stepper source;
    RouteButton route { "M" };
    TuneRow tune;
    TypeBar bar;
    KnobPanel panel;
    QGroup q[5];
    Rect area, wave;
    bool hasTune = true;
    const char* typeName = "";

    OscPage() { power.state.icon = ToggleIcon::Icon::Power; power.state.on = true; }

    /** The page of a preset in `area` (the region of a tab: 1280 x 628 with a margin of 56 on the sides); typeIndex: sub 0, wavetable 1, granular 2, multisample 3, noise 4. */
    void build (const PanelPreset& p, const BarPreset& bp, int typeIndex, Rect a, bool withTune)
    {
        area = a;
        hasTune = withTune;
        panel.load (p);
        bar.load (bp);
        const int panelH = 240;
        panel.place ({ a.x, a.bottom() - panelH, a.w, panelH });
        power.place ({ a.x, a.y + 4, 56, 56 });
        route.place ({ a.x + 208, a.y, 64, 64 });
        tune.setShort (false);
        tune.place ({ a.x + 284, a.y + 4, 400, 56 });
        const Rect pan = panel.cellRect (PanelSlot::Pan), level = panel.cellRect (PanelSlot::Level);
        // the type selector and the source stepper over PAN and LEVEL: one block, the first Q group
        if (&p == &presets::kWavetable)
        {
            type.place ({ pan.x, a.y, pan.w, 5 * 56 });
            source.place ({ pan.x, a.y + 5 * 56 + 8, pan.w, 56 });
        }
        else
        {
            type.place ({ pan.x, a.y, pan.w, pan.w >= 5 * 76 ? 64 : 5 * 56 });
            source.place ({ level.x, a.y, level.w, 56 });
        }
        const int w = std::min (560, pan.x - a.x - 8);
        wave = { a.x, a.y + 72, w, panel.rect().y - 8 - 56 - 4 - (a.y + 72) };
        bar.place ({ a.x, wave.bottom() + 4, w, 56 });
        for (int i = 0; i < 5; ++i)
            type.setItem (i, i, i == 0 ? "SUB" : i == 1 ? "WAVE" : i == 2 ? "GRAIN" : i == 3 ? "SAMPLE" : "NOISE");
        type.state.index = typeIndex;
        typeName = "OSC 1";
        ids();
        fill();
        groups();
    }
    void ids()
    {
        power.setId (1); type.setId (2); source.setId (3); route.setId (4);
        tune.oct.setId (10); tune.sem.setId (11); tune.fin.setId (12); tune.crs.setId (13); tune.track.setId (14);
        for (int i = 0; i < bar.numSlots(); ++i)
            bar.slot (i)->setId (20 + i);
        int n = 100;
        for (int s = 0; s < static_cast<int> (PanelSlot::Count); ++s)
        {
            const PanelSlot slot = static_cast<PanelSlot> (s);
            if (Knob* k = panel.knob (slot)) k->setId (n);
            if (KnobRandPair* q2 = panel.pair (slot)) { q2->main.setId (n); q2->rand.setId (n + 50); }
            ++n;
        }
        if (UnisonBox* u = panel.unison()) { u->gear.setId (200); u->count.setId (201); }
        if (WarpPair* w = panel.warp()) { w->first.amount.setId (210); w->first.type.setId (211); w->first.var.setId (212); w->second.amount.setId (213); w->second.type.setId (214); w->second.var.setId (215); }
        if (MenuButton* m = panel.menu()) m->setId (230);
    }
    void fill()
    {
        source.state.count = 40; source.state.index = 7; source.setItem ("BASIC SHAPES");
        tune.oct.state.value = 0; tune.sem.state.value = 2; tune.fin.state.value = -12; tune.crs.state.value = 0.0;
        for (int i = 0; i < bar.numSlots(); ++i)
        {
            Component* c = bar.slot (i);
            if (auto* nf = dynamic_cast<NumberField*> (c)) { nf->state.value = nf->state.format == NumberField::Format::Degrees ? 180 : (nf->state.readOnly ? 128 : 40); }
            if (auto* st = dynamic_cast<Stepper*> (c)) { st->state.count = 3; st->state.index = 1; st->setItem ("CONTIGUOUS"); }
            if (auto* d = dynamic_cast<DetailButton*> (c)) { if (d->state.integer) d->state.value = 7; else { d->state.value = 0.5; d->setValueText ("50%"); } }
            if (auto* t = dynamic_cast<ToggleIcon*> (c)) t->state.on = true;
        }
        double v = 0.15;
        auto setK = [&] (Knob& k) { k.state.value = v; k.state.modValue = v; v += 0.11; if (v > 0.95) v = 0.1; };
        for (int s = 0; s < static_cast<int> (PanelSlot::Count); ++s)
        {
            const PanelSlot slot = static_cast<PanelSlot> (s);
            if (Knob* k = panel.knob (slot)) setK (*k);
            if (KnobRandPair* q2 = panel.pair (slot)) { setK (q2->main); setK (q2->rand); }
        }
        if (Knob* k = panel.knob (PanelSlot::Detune)) { k->state.add (1, "LFO 2", 0.3); k->state.selectedSource = 1; k->state.modValue = k->state.value + 0.2; }
        if (Knob* k = panel.knob (PanelSlot::Scan)) { k->state.add (2, "ENV 1", -0.25, true); k->state.selectedSource = 2; k->state.modValue = k->state.value - 0.15; }
        if (UnisonBox* u = panel.unison()) u->count.state.value = 7;
        if (WarpPair* w = panel.warp())
        {
            w->first.type.state.count = 12; w->first.type.state.index = 3; w->first.type.setItem ("BEND +");
            w->second.type.state.count = 12; w->second.type.state.index = 0; w->second.type.setItem ("OFF");
            w->first.setTypeOff (false); w->second.setTypeOff (true);
            w->first.var.state.value = w->first.var.state.modValue = 0.4;
        }
        if (Knob* k = panel.knob (PanelSlot::Pan)) k->state.value = k->state.modValue = 0.5;
        if (KnobRandPair* q2 = panel.pair (PanelSlot::Pan)) q2->main.state.value = q2->main.state.modValue = 0.5;
        route.state.segment[0] = true;
    }
    /** The Q groups: 0 = type, source, PAN, LEVEL (active); 1 = the slots of the TypeBar; 2 = OCT SEM FIN CRS; 3 = WT POS, UNISON, DETUNE, BLEND; 4 = WARP 1 type and amount, WARP 2 type and amount. */
    void groups()
    {
        Component* pan = panel.knob (PanelSlot::Pan) ? static_cast<Component*> (panel.knob (PanelSlot::Pan)) : panel.pair (PanelSlot::Pan);
        Component* lvl = panel.knob (PanelSlot::Level) ? static_cast<Component*> (panel.knob (PanelSlot::Level)) : panel.pair (PanelSlot::Level);
        Component* m0[4] = { &type, &source, pan, lvl };
        for (int i = 0; i < 4; ++i)
            q[0].set (i, m0[i]);
        fillGroup (q[1], bar);
        q[2].set (0, &tune.oct); q[2].set (1, &tune.sem); q[2].set (2, &tune.fin); q[2].set (3, &tune.crs);
        if (panel.warp() && panel.unison())
        {
            q[3].set (0, panel.knob (PanelSlot::WtPos)); q[3].set (1, panel.unison()); q[3].set (2, panel.knob (PanelSlot::Detune)); q[3].set (3, panel.knob (PanelSlot::Blend));
            WarpPair& w = *panel.warp();
            q[4].set (0, &w.first.type); q[4].set (1, &w.first.amount); q[4].set (2, &w.second.type); q[4].set (3, &w.second.amount);
        }
        q[0].setActive (true);
    }
    /** All leaves of the page (what the adjacency check looks at). */
    int leaves (Component** out, int max)
    {
        int n = 0;
        n = power.collectLeaves (out, n, max);
        n = type.collectLeaves (out, n, max);
        n = source.collectLeaves (out, n, max);
        n = route.collectLeaves (out, n, max);
        if (hasTune) n = tune.collectLeaves (out, n, max);
        n = bar.collectLeaves (out, n, max);
        n = panel.collectLeaves (out, n, max);
        return n;
    }
    void draw (gpu::SceneBuilder& sb) const
    {
        Pen pen (sb, true);
        pen.rect (wave, pen.c (colour::kPanel, 255));
        pen.frame (wave, 1, pen.c (colour::kRest, 255));
        pen.textCentered (wave, "WAVE DISPLAY", 2, pen.c (colour::kText, 90));
        pen.text (area.x + 72, area.y + 18, 4, typeName, pen.c (colour::kWhite, 255));
        power.draw (sb);
        route.draw (sb);
        type.draw (sb);
        source.draw (sb);
        if (hasTune) tune.draw (sb);
        bar.draw (sb);
        panel.draw (sb);
        for (const QGroup& g : q)
            g.draw (sb);
    }
};

} // namespace uit
