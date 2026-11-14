// SynthyBoy - MPC UI component library: the DEMONSTRATION program (a host tool built by shell-mpc/accept.sh): renders the oscillator pages (wavetable, granular, noise: header with the
// pictogram selector, tuning row, type bar, lower knob panel, the first Q group active), the type bar presets, the detail pages (unison settings, grain window, modulations of a control),
// the route panel and a sheet of knob states into PNG files in the directory given as the first argument.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "demo_page.h"

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
    // the three oscillator pages
    struct Case { const PanelPreset* p; const BarPreset* b; int type; bool tune; const char* file; };
    const Case cases[] = { { &presets::kWavetable, &barpresets::kWavetable, 1, true, "oscillator_wavetable.png" },
                           { &presets::kGranular, &barpresets::kGranular, 2, true, "oscillator_granular.png" },
                           { &presets::kNoise, &barpresets::kNoise, 4, false, "oscillator_noise.png" } };
    for (const Case& cs : cases)
    {
        OscPage pg;
        pg.build (*cs.p, *cs.b, cs.type, { 56, 14, 1168, 600 }, cs.tune);
        Scene s (1280, 628, kBackgroundRgba);
        pg.draw (s.builder());
        save (dir, cs.file, s);
        Component* all[96];
        const int n = pg.leaves (all, 96);
        check (pg.q[0].check (all, n) == 0, std::string (cs.file) + ": the first Q group is adjacent");
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
    // the detail pages
    {
        DetailPage pg;
        pg.load (presets::kUnisonDetail, "UNISON");
        pg.place ({ 56, 14, 1168, 600 });
        pg.panel.stepper (PanelSlot::Mode)->state.count = 5; pg.panel.stepper (PanelSlot::Mode)->setItem ("LINEAR");
        pg.panel.stepper (PanelSlot::Stack)->state.count = 5; pg.panel.stepper (PanelSlot::Stack)->setItem ("12+7");
        double v = 0.2;
        for (PanelSlot sl : { PanelSlot::Width, PanelSlot::Range, PanelSlot::WtSpread, PanelSlot::Warp1Spread, PanelSlot::Warp2Spread })
        {
            pg.panel.knob (sl)->state.value = pg.panel.knob (sl)->state.modValue = v;
            v += 0.15;
        }
        Scene s (1280, 628, kBackgroundRgba);
        pg.draw (s.builder());
        save (dir, "detail_unison.png", s);
    }
    {
        DetailPage pg;
        pg.load (presets::kGrainDetail, "GRAIN WINDOW");
        pg.place ({ 56, 14, 1168, 600 });
        pg.panel.stepper (PanelSlot::Shape)->state.count = 4; pg.panel.stepper (PanelSlot::Shape)->setItem ("HANN");
        double v = 0.3;
        for (PanelSlot sl : { PanelSlot::Amount, PanelSlot::AmountRand, PanelSlot::Skew, PanelSlot::SkewRand })
        {
            pg.panel.knob (sl)->state.value = pg.panel.knob (sl)->state.modValue = v;
            v += 0.13;
        }
        Scene s (1280, 628, kBackgroundRgba);
        pg.draw (s.builder());
        // the display area: a bell curve (the owner's drawing goes here; a stand-in for the picture)
        const Rect d = pg.displayRect();
        float rows[200];
        for (int i = 0; i < 200; ++i)
        {
            const float t = (static_cast<float> (i) / 199.0f - 0.5f) * 2.0f;
            rows[i] = static_cast<float> (d.bottom() - 10) - static_cast<float> (d.h - 24) * std::exp (-4.0f * t * t);
        }
        s.builder().curve (rows, 200, d.x + 8, d.w - 16);
        s.builder().stroke (2.0f, colour::rgba (colour::kAccent, 255));
        save (dir, "detail_grain_window.png", s);
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
    // the route popup
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
        save (dir, "pictogram_selector.png", s);
    }
    std::printf ("%s: %d images, %d failed\n", g_fails ? "FAIL" : "PASS", g_images, g_fails);
    return g_fails ? 1 : 0;
}
