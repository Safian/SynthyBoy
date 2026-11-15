// SynthyBoy - MPC UI component library: the DEMONSTRATION program (a host tool built by shell-mpc/accept.sh): renders the oscillator pages (wavetable, granular, noise: header with the
// pictogram selector, tuning row, type bar, lower knob panel, the first Q group active), the type bar presets, the detail pages (unison settings, grain window, modulations of a control),
// the route panel and a sheet of knob states into PNG files in the directory given as the first argument.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"

using namespace uit;

namespace
{
int g_images = 0;
bool save (const std::string& dir, const char* name, const Scene& s, int scale = 1)
{
    check (! s.overflow(), std::string (name) + ": the scene fits (" + std::to_string (s.numOps()) + " ops of " + std::to_string (gpu::kMaxOps) + ")");
    const bool ok = writePng (dir + "/" + name, render (s), scale);
    check (ok, std::string (name) + ": written");
    g_images += ok;
    return ok;
}
void label (gpu::SceneBuilder& sb, int x, int y, const char* t) { sb.text (x, y, 2, t, colour::rgba (colour::kText, 150)); }
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    // the compact view (a third of the width) of A (wavetable), Sub and Noise, on one sheet each
    {
        struct C { OscInstance i; OscKind k; const char* file; };
        const C cs[] = { { OscInstance::A, OscKind::Wavetable, "compact_A_wavetable.png" }, { OscInstance::Sub, OscKind::Wavetable, "compact_sub.png" }, { OscInstance::Noise, OscKind::Wavetable, "compact_noise.png" } };
        for (const C& c : cs)
        {
            static OscillatorCompact oc;
            oc.place ({ 10, 10, 420, 700 });
            oc.setInstance (c.i, c.k);
            oc.basic.source.stepper.state.count = 40; oc.basic.source.stepper.setItem ("BASIC SHAPES");
            oc.basic.noiseType.state.count = 4; oc.basic.noiseType.setItem ("WHITE");
            oc.basic.selector.state.index = c.i == OscInstance::Sub ? 3 : 0;
            oc.basic.level.state.value = oc.basic.level.state.modValue = 0.8;
            oc.basic.filter.state.value = oc.basic.filter.state.modValue = 0.6;
            oc.route.state.segment[0] = true;
            Scene s (440, 720, kBackgroundRgba);
            oc.draw (s.builder());
            s.builder().text (oc.displayRect().x + 130, oc.displayRect().y + oc.displayRect().h / 2, 2, "DISPLAY AREA", colour::rgba (colour::kText, 90));
            save (dir, c.file, s);
        }
    }
    // the full screens: Sub, A (wavetable, multisample, granular), Noise, with the Basic group active
    {
        struct F { OscInstance i; OscKind k; const char* file; };
        const F fsx[] = { { OscInstance::Sub, OscKind::Wavetable, "fullscreen_sub.png" }, { OscInstance::A, OscKind::Wavetable, "fullscreen_A_wavetable.png" },
                          { OscInstance::A, OscKind::Multisample, "fullscreen_A_multisample.png" }, { OscInstance::A, OscKind::Granular, "fullscreen_A_granular.png" },
                          { OscInstance::Noise, OscKind::Wavetable, "fullscreen_noise.png" } };
        for (const F& f : fsx)
        {
            static OscillatorFullScreen fs;
            fs.place ({ 0, 0, 1280, 800 });
            fs.setInstance (f.i, f.k);
            fs.basic.source.stepper.state.count = 40; fs.basic.source.stepper.setItem ("BASIC SHAPES");
            fs.basic.noiseType.state.count = 4; fs.basic.noiseType.setItem ("WHITE");
            fs.basic.selector.state.index = f.i == OscInstance::Sub ? 3 : (f.k == OscKind::Wavetable ? 0 : f.k == OscKind::Multisample ? 1 : 2);
            fs.basic.level.state.value = fs.basic.level.state.modValue = 0.8;
            fs.basic.filter.state.value = fs.basic.filter.state.modValue = 0.6;
            fs.route.state.segment[0] = true;
            for (int k = 0; k < fs.bar.numSlots(); ++k)
            {
                Component* c = fs.bar.slot (k);
                if (auto* nf = dynamic_cast<NumberField*> (c)) nf->state.value = nf->state.format == NumberField::Format::Degrees ? 180 : 40;
                if (auto* st = dynamic_cast<Stepper*> (c)) { st->state.count = 3; st->setItem ("PER VOICE"); }
                if (auto* db = dynamic_cast<DetailButton*> (c)) { db->state.value = db->state.integer ? 7 : 0.5; db->setValueText (db->state.integer ? "" : "50%"); }
                if (auto* tn = dynamic_cast<ToggleNumber*> (c)) { tn->toggle.state.on = true; tn->amount.state.value = 60; }
            }
            double v = 0.15;
            Component* pl[96];
            const int np = fs.panel.collectLeaves (pl, 0, 96);
            for (int i = 0; i < np; ++i)
                if (auto* kn = dynamic_cast<Knob*> (pl[i])) { kn->state.value = kn->state.modValue = v; v += 0.11; if (v > 0.95) v = 0.1; }
            if (WarpPair* w = fs.panel.warp()) { w->first.type.state.count = 12; w->first.type.setItem ("BEND +"); w->second.type.state.count = 12; w->second.type.setItem ("OFF"); w->second.setTypeOff (true); }
            if (UnisonBox* u = fs.panel.unison()) u->count.state.value = 7;
            fs.tune.sem.state.value = 2; fs.tune.fin.state.value = -12;
            Scene s (1280, 800, kBackgroundRgba);
            fs.draw (s.builder());
            const Rect d = fs.displayRect();
            s.builder().text (d.x + d.w / 2 - 70, d.y + d.h / 2 - 7, 2, "DISPLAY AREA", colour::rgba (colour::kText, 90));
            save (dir, f.file, s);
        }
    }
    // the type bar presets, one strip each, on one sheet
    {
        Scene s (640, 430, kBackgroundRgba);
        const BarPreset* ps[5] = { &barpresets::kWavetable, &barpresets::kGranular, &barpresets::kSub, &barpresets::kMultisample, &barpresets::kNoise };
        static TypeBar bars[5];
        for (int i = 0; i < 5; ++i)
        {
            bars[i].load (*ps[i]);
            bars[i].place ({ 40, 40 + i * 80, 560, 56 });
            for (int k = 0; k < bars[i].numSlots(); ++k)
                if (auto* d = dynamic_cast<DetailButton*> (bars[i].slot (k))) { d->state.value = d->state.integer ? 7 : 0.5; d->setValueText (d->state.integer ? "" : "50%"); }
            QGroup g;
            fillGroup (g, bars[i]);
            g.setActive (true);
            label (s.builder(), 40, 22 + i * 80, ps[i]->name);
            bars[i].draw (s.builder());
            g.draw (s.builder());
        }
        save (dir, "typebar_presets.png", s);
    }
    // the detail pages: every preset
    {
        struct P { const PanelPreset* p; const char* title; const char* file; };
        const P ps[] = { { &presets::kUnisonDetail, "UNISON", "detail_unison_wavetable.png" }, { &presets::kUnisonDetailMultisample, "UNISON", "detail_unison_multisample.png" },
                         { &presets::kUnisonDetailGranular, "UNISON", "detail_unison_granular.png" }, { &presets::kGrainDetail, "GRAIN WINDOW", "detail_grain_window.png" },
                         { &presets::kEnvelopeDetail, "ENVELOPE", "detail_multisample_envelope.png" } };
        for (const P& pr : ps)
        {
            static DetailPage pg;
            pg.load (*pr.p, pr.title);
            pg.place ({ 56, 14, 1168, 600 });
            for (PanelSlot sl : { PanelSlot::Mode, PanelSlot::Stack, PanelSlot::Shape })
                if (Stepper* st = pg.panel.stepper (sl)) { st->state.count = 5; st->setItem (sl == PanelSlot::Mode ? "LINEAR" : sl == PanelSlot::Stack ? "12+7" : "HANN"); }
            double v = 0.2;
            Component* pl[32];
            const int np = pg.panel.collectLeaves (pl, 0, 32);
            for (int i = 0; i < np; ++i)
                if (auto* kn = dynamic_cast<Knob*> (pl[i])) { kn->state.value = kn->state.modValue = v; v += 0.1; if (v > 0.9) v = 0.15; }
            if (ToggleIcon* t = pg.panel.toggle (PanelSlot::Override)) t->state.on = true;
            Scene s (1280, 628, kBackgroundRgba);
            pg.draw (s.builder());
            const Rect d = pg.displayRect();
            if (! d.empty())
            {
                float rows[200];
                for (int i = 0; i < 200; ++i)
                {
                    const float t = (static_cast<float> (i) / 199.0f - 0.5f) * 2.0f;
                    rows[i] = static_cast<float> (d.bottom() - 10) - static_cast<float> (d.h - 24) * std::exp (-4.0f * t * t);
                }
                s.builder().curve (rows, 200, d.x + 8, d.w - 16);   // the owner's picture goes here: a stand-in curve
                s.builder().stroke (2.0f, colour::rgba (colour::kAccent, 255));
            }
            save (dir, pr.file, s);
        }
    }
    {
        ModInput m;
        m.add (7, "LFO 2", 0.35);
        m.add (8, "ENV 1", -0.5, true);
        m.add (9, "MACRO1", 0.8, false, true);
        m.add (10, "VEL", 0.2, true);
        ModulationsPage pg;
        pg.load (m, "DETUNE");
        pg.place ({ 56, 14, 1168, 600 });
        Scene s (1280, 628, kBackgroundRgba);
        pg.draw (s.builder());
        save (dir, "detail_modulations.png", s);
    }
    // the route popup over a screen
    {
        static OscillatorFullScreen fs;
        fs.place ({ 0, 0, 1280, 800 });
        fs.setInstance (OscInstance::A, OscKind::Wavetable);
        static RoutePanel rp;
        rp.place ({ 300, 220, 640, 260 });
        rp.setPopup (true, { 0, 0, 1280, 800 });
        rp.target.state.count = 4; rp.target.setItem ("FILTER");
        rp.bus1.state.value = rp.bus1.state.modValue = 0.3; rp.bus2.state.value = rp.bus2.state.modValue = 0.6;
        rp.group.setActive (true);
        Scene s (1280, 800, kBackgroundRgba);
        fs.draw (s.builder());
        rp.draw (s.builder());
        save (dir, "route_popup.png", s);
    }
    // the list popup (the source list, the warp types) over a screen
    {
        static const char* const names[] = { "Sine", "Saw", "Square", "Triangle", "Pulse", "Noise", "Organ", "Bell", "Vocal", "Pad", "Bass", "Lead" };
        static OscillatorFullScreen fs;
        fs.place ({ 0, 0, 1280, 628 });
        fs.setInstance (OscInstance::A, OscKind::Wavetable);
        static ListPopup lp;
        lp.state.items = names; lp.state.count = 12; lp.state.current = 1;
        lp.place ({ 330, 40, 620, 400 });
        lp.setPopup ({ 0, 0, 1280, 628 });
        lp.showCurrent();
        Scene s (1280, 628, kBackgroundRgba);
        fs.draw (s.builder());
        lp.draw (s.builder());
        save (dir, "list_popup_screen.png", s);
    }
    // the route panel
    {
        RoutePanel r;
        r.place ({ 40, 40, 560, 200 });
        r.target.state.count = 4; r.target.setItem ("FILTER");
        r.bus1.state.value = r.bus1.state.modValue = 0.3; r.bus2.state.value = r.bus2.state.modValue = 0.6;
        RoutePanel off;
        off.place ({ 640, 40, 560, 200 });
        off.target.state.count = 4; off.target.state.index = 1; off.target.setItem ("MAIN");
        off.setTargetIsFilter (false);
        Scene s (1280, 280, kBackgroundRgba);
        label (s.builder(), 40, 20, "TARGET FILTER");
        label (s.builder(), 640, 20, "TARGET MAIN: BALANCE DIM");
        r.draw (s.builder());
        off.draw (s.builder());
        save (dir, "route_panel.png", s);
    }
    // the knob in every modulation state
    {
        Scene s (1280, 360, kBackgroundRgba);
        struct K { const char* name; int mode; };
        static Knob knobs[12];
        const char* names[12] = { "NO MODS", "GREY", "COLOURED", "BYPASSED", "NEG DEPTH", "BIDIR", "CLAMPED", "BIPOLAR", "LIVE MOD", "SMALL", "PLAIN", "NUMBER" };
        for (int i = 0; i < 12; ++i)
        {
            Knob& k = knobs[i];
            k.state = Knob::State {};
            k.setLabel ("DETUNE");
            k.state.value = k.state.modValue = 0.5;
            k.place ({ 20 + (i % 6) * 206, 40 + (i / 6) * 160, 190, 130 });
            auto& m = static_cast<ModInput&> (k.state);
            switch (i)
            {
                case 1: m.add (1, "LFO 1", 0.3); break;
                case 2: m.add (1, "LFO 1", 0.3); m.selectedSource = 1; break;
                case 3: m.add (1, "LFO 1", 0.3, false, true); m.selectedSource = 1; break;
                case 4: m.add (1, "ENV 1", -0.3); m.selectedSource = 1; break;
                case 5: m.add (1, "LFO 1", 0.25, true); m.selectedSource = 1; break;
                case 6: k.state.value = k.state.modValue = 0.8; m.add (1, "LFO 1", 0.6); m.selectedSource = 1; break;
                case 7: k.state.bipolar = true; k.state.value = k.state.modValue = 0.65; break;
                case 8: m.add (1, "LFO 1", 0.3); m.selectedSource = 1; k.state.modValue = 0.68; break;
                case 9: k.state.small = true; k.setLabel ("RAND"); break;
                default: break;
            }
            if (i < 11)
            {
                label (s.builder(), 20 + (i % 6) * 206, 20 + (i / 6) * 160, names[i]);
                k.draw (s.builder());
            }
        }
        static NumberField nf;
        nf.state = NumberField::State {};
        nf.setLabel ("OCT"); nf.state.min = -4; nf.state.max = 4; nf.state.value = 1;
        nf.state.add (1, "LFO 1", 0.4); nf.state.selectedSource = 1;
        nf.place ({ 20 + 5 * 206 + 40, 200, 110, 70 });
        label (s.builder(), 20 + 5 * 206, 180, names[11]);
        nf.draw (s.builder());
        save (dir, "knob_states.png", s);
    }
    // pictogram selectors
    {
        Scene s (640, 460, kBackgroundRgba);
        static PictogramSelector row, col;
        const char* nm[5] = { "SUB", "WAVE", "GRAIN", "SAMPLE", "NOISE" };
        for (int i = 0; i < 5; ++i) { row.setItem (i, i, nm[i], i != 3); col.setItem (i, i, nm[i]); }
        row.state.index = 2; col.state.index = 4;
        row.place ({ 20, 40, 5 * 90, 64 });
        col.place ({ 20 + 5 * 90 + 40, 40, 100, 5 * 56 });
        label (s.builder(), 20, 20, "ROW (SAMPLE DISABLED)");
        label (s.builder(), 20 + 5 * 90 + 40, 20, "COLUMN");
        row.draw (s.builder()); col.draw (s.builder());
        static PictogramSelector sub;
        const int sid[6] = { pictogram::Sine, pictogram::HalfSine, pictogram::Triangle, pictogram::Saw, pictogram::Square, pictogram::Pulse };
        const char* snm[6] = { "SINE", "HALF", "TRI", "SAW", "SQR", "PULSE" };
        for (int i = 0; i < 6; ++i) sub.setItem (i, sid[i], snm[i]);
        sub.place ({ 20, 140, 6 * 62, 56 });
        label (s.builder(), 20, 120, "SUB SHAPES");
        sub.draw (s.builder());
        save (dir, "pictogram_selector.png", s);
    }
    std::printf ("%s: %d images, %d failed\n", g_fails ? "FAIL" : "PASS", g_images, g_fails);
    return g_fails ? 1 : 0;
}
