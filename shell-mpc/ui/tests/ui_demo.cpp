// SynthyBoy - MPC UI component library demonstration (a host tool built by shell-mpc/accept.sh). Renders the compact and full oscillator views, detail pages,
// the route panel and other UI components to PNG files in the directory given as the first argument.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sys/stat.h>

#include "ui_test.h"
#include "../mod_detail.h"
#include "../mod_macros.h"
#include "../mod_matrix.h"

using namespace uit;

namespace
{
static std::string g_filter;

static bool matchesFilter (const char* name)
{
    if (g_filter.empty())
        return true;
    return (std::strstr (name, g_filter.c_str()) != nullptr);
}

int g_images = 0;
bool save (const std::string& dir, const char* name, const Scene& s, int scale = 1)
{
    if (! matchesFilter (name))
        return true;
    check (! s.overflow(), std::string (name) + ": the scene fits (" + std::to_string (s.numOps()) + " ops of " + std::to_string (gpu::kMaxOps) + ")");
    const bool ok = writePng (dir + "/" + name, render (s), scale);
    check (ok, std::string (name) + ": written");
    g_images += ok;
    return ok;
}
bool saveImage (const std::string& dir, const char* name, const Image& im)
{
    if (! matchesFilter (name))
        return true;
    const bool ok = writePng (dir + "/" + name, im, 1);
    check (ok, std::string (name) + ": written");
    g_images += ok;
    return ok;
}
/** A scene on the ground of the look (the vertical gradient of the screens). */
struct GScene : Scene
{
    GScene (int w, int h) : Scene (w, h, theme::rgba (theme::L().bg0, 255))
    {
        const theme::Look& T = theme::L();
        builder().gradientRect (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h), theme::rgba (T.bg0), theme::rgba (T.bg1));
    }
};
void label (gpu::SceneBuilder& sb, int x, int y, const char* t) { sb.text (static_cast<float> (x), static_cast<float> (y + 14), 14.0f, t, theme::rgba (theme::L().textDim)); }

// ---- synthetic data for the displays (the engine is not here: shapes that look like what it gives)
constexpr int kN = 512;
void waveFrame (std::vector<float>& v, float pos, float phase = 0.0f)
{
    v.assign (kN, 0.0f);
    for (int i = 0; i < kN; ++i)
    {
        const float t = static_cast<float> (i) / kN + phase;
        const float saw = 2.0f * (t - std::floor (t)) - 1.0f, sine = std::sin (2.0f * kPi * t), tri = 2.0f * std::fabs (saw) - 1.0f;
        const float sq = std::tanh (4.0f * std::sin (2.0f * kPi * t));
        v[static_cast<size_t> (i)] = pos < 0.5f ? sine + (saw - sine) * pos * 2.0f : saw + (sq - saw) * (pos - 0.5f) * 2.0f - 0.0f * tri;
        v[static_cast<size_t> (i)] *= 0.9f;
    }
}
struct DemoDisplay
{
    std::vector<float> cyc, mn, mx, stack[7], spec;
    display::Data data;
    void wave (float pos, int voices, uint32_t colour, bool stacked = true)
    {
        waveFrame (cyc, pos);
        for (int k = 0; k < 7; ++k)
            waveFrame (stack[k], static_cast<float> (k) / 6.0f);
        spec.assign (display::kMaxSpectrum, 0.0f);
        for (int k = 1; k <= display::kMaxSpectrum; ++k)
        {
            double re = 0, im = 0;
            for (int i = 0; i < kN; ++i)
            {
                const double a = 2.0 * 3.14159265358979 * k * i / kN;
                re += cyc[static_cast<size_t> (i)] * std::cos (a);
                im -= cyc[static_cast<size_t> (i)] * std::sin (a);
            }
            spec[static_cast<size_t> (k - 1)] = static_cast<float> (std::sqrt (std::min (1.0, std::sqrt (re * re + im * im) / (0.5 * kN))));
        }
        data = display::Data();
        data.kind = display::Data::Kind::Wave;
        data.cycle = cyc.data();
        data.n = kN;
        data.pos = pos;
        data.voices = voices;
        data.detune = 0.45f;
        data.blend = 0.7f;
        data.colour = colour;
        data.spectrum = spec.data();
        data.numSpectrum = display::kMaxSpectrum;
        if (stacked)
        {
            data.numStack = 7;
            for (int k = 0; k < 7; ++k)
                data.stack[k] = stack[k].data();
        }
    }
    void minmax (uint32_t colour, bool granular, float playhead = 0.4f)
    {
        mn.assign (kN, 0.0f);
        mx.assign (kN, 0.0f);
        unsigned r = 12345;
        for (int i = 0; i < kN; ++i)
        {
            r = r * 1664525u + 1013904223u;
            const float env = std::exp (-2.2f * static_cast<float> (i) / kN) * (0.55f + 0.45f * std::sin (static_cast<float> (i) * 0.07f)), j = static_cast<float> (r >> 16 & 255u) / 255.0f;
            mx[static_cast<size_t> (i)] = env * (0.45f + 0.55f * j);
            mn[static_cast<size_t> (i)] = -env * (0.45f + 0.55f * (1.0f - j));
        }
        data = display::Data();
        data.kind = display::Data::Kind::MinMax;
        data.cycle = mn.data();
        data.cycleMax = mx.data();
        data.n = kN;
        data.colour = colour;
        data.loopOn = ! granular;
        data.loopStart = 0.18f;
        data.loopEnd = 0.74f;
        data.grain = granular;
        data.grainPos = 0.33f;
        data.grainSize = 0.12f;
        data.playhead = playhead;
    }
};
/** The display block with the picture in it: what the screens show in their display rectangle. */
void showDisplay (gpu::SceneBuilder& sb, const Rect& r, const display::Data& d, bool block = false)
{
    (void) block;
    display::draw (sb, r, d);
}
}

static void renderAll (const std::string& dir)
{
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
            GScene s (440, 720);
            oc.draw (s.builder());
            static DemoDisplay dd;
            if (c.i == OscInstance::Noise) { dd.data = display::Data(); dd.data.kind = display::Data::Kind::Message; dd.data.message = "NOISE"; }
            else dd.wave (0.42f, c.i == OscInstance::Sub ? 1 : 7, theme::instanceColour (c.i == OscInstance::Sub ? 0 : c.i == OscInstance::A ? 1 : 2), c.i != OscInstance::Sub);
            showDisplay (s.builder(), oc.displayRect(), dd.data);
            oc.full.draw (s.builder());
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
            fs.top.source.stepper.state.count = 40; fs.top.source.stepper.setItem ("BASIC SHAPES");
            fs.top.kind.state.count = 2; fs.top.kind.setItem ("NOISE"); fs.top.sound.state.count = 4; fs.top.sound.setItem ("WHITE");
            fs.top.selector.state.index = f.i == OscInstance::Sub ? 3 : (f.k == OscKind::Wavetable ? 0 : f.k == OscKind::Multisample ? 1 : 2);
            fs.top.pos.state.value = fs.top.pos.state.modValue = 0.8;
            fs.top.filter.state.value = fs.top.filter.state.modValue = 0.6;
            fs.mix.setDest ("F1"); fs.mix.setInfo ("-3.0 dB  C"); fs.mix.state.segment[0] = true;
            double v = 0.15;
            Component* pl[96];
            const int np = fs.panel.collectLeaves (pl, 0, 96);
            for (int i = 0; i < np; ++i)
                if (auto* kn = dynamic_cast<Knob*> (pl[i])) { kn->state.value = kn->state.modValue = v; v += 0.11; if (v > 0.95) v = 0.1; }
            if (WarpPair* w = fs.panel.warp()) { w->first.type.state.count = 12; w->first.type.setItem ("BEND +"); w->second.type.state.count = 12; w->second.type.setItem ("OFF"); w->second.setTypeOff (true); }
            if (UnisonBox* u = fs.panel.unison()) u->count.state.value = 7;
            if (NumberField* fin = fs.panel.number (PanelSlot::Fin)) fin->state.value = -12;
            GScene s (1280, 800);
            fs.draw (s.builder());
            const Rect d = fs.displayRect();
            static DemoDisplay dd;
            if (f.i == OscInstance::Noise) { dd.data = display::Data(); dd.data.kind = display::Data::Kind::Message; dd.data.message = "NOISE"; }
            else if (f.k == OscKind::Wavetable) dd.wave (0.42f, f.i == OscInstance::Sub ? 1 : 7, theme::instanceColour (f.i == OscInstance::Sub ? 0 : 1), f.i != OscInstance::Sub);
            else dd.minmax (theme::instanceColour (2), f.k == OscKind::Granular);
            showDisplay (s.builder(), d, dd.data);
            save (dir, f.file, s);
        }
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
            GScene s (1280, 628);
            pg.draw (s.builder());
            const Rect d = pg.displayRect();
            if (! d.empty())
            {
                static DemoDisplay dd;
                if (pr.p == &presets::kGrainDetail) dd.minmax (theme::instanceColour (3), true);
                else if (pr.p == &presets::kEnvelopeDetail) dd.minmax (theme::instanceColour (2), false);
                else dd.wave (0.6f, 7, theme::instanceColour (1));
                showDisplay (s.builder(), d, dd.data);
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
        GScene s (1280, 628);
        pg.draw (s.builder());
        save (dir, "detail_modulations.png", s);
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
        GScene s (1280, 628);
        fs.draw (s.builder());
        lp.draw (s.builder());
        save (dir, "list_popup_screen.png", s);
    }
    // the knob in every modulation state
    {
        GScene s (1280, 360);
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
        GScene s (640, 460);
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
    // ---- the displays: the four kinds side by side, the wavetable with its position moving (the stack lights up one frame after the other), the module panels with the running point
    {
        static DemoDisplay a, b, c, d;
        a.wave (0.35f, 1, theme::instanceColour (1), false);
        b.wave (0.62f, 7, theme::instanceColour (2), true);
        c.minmax (theme::instanceColour (3), false);
        d.minmax (theme::instanceColour (4), true);
        GScene s (1280, 700);
        showDisplay (s.builder(), { 16, 16, 612, 320 }, a.data, true);
        showDisplay (s.builder(), { 652, 16, 612, 320 }, b.data, true);
        showDisplay (s.builder(), { 16, 352, 612, 320 }, c.data, true);
        showDisplay (s.builder(), { 652, 352, 612, 320 }, d.data, true);
        label (s.builder(), 24, 20, "WAVE, ONE VOICE");
        label (s.builder(), 660, 20, "WAVETABLE, STACK + UNISON");
        label (s.builder(), 24, 356, "MULTISAMPLE: MIN / MAX, LOOP, PLAYHEAD");
        label (s.builder(), 660, 356, "GRANULAR: GRAIN WINDOW");
        save (dir, "display_kinds.png", s);
    }
    {
        // the two levels of detail side by side: full (left) and reduced (right): the stack, the unison hairlines, the spectrum, the light
        constexpr int W = 640, H = 760;
        Image both;
        both.w = 2 * W;
        both.h = H;
        both.px.assign (static_cast<size_t> (both.w) * static_cast<size_t> (both.h), 0u);
        for (int lvl = 1; lvl <= 2; ++lvl)
        {
            theme::setDetail (lvl);
            static DemoDisplay dd;
            dd.wave (0.45f, 7, theme::instanceColour (2));
            GScene s (W, H);
            showDisplay (s.builder(), { 14, 14, W - 28, 330 }, dd.data);
            std::vector<float> env (256);
            for (int i = 0; i < 256; ++i)
            {
                const float t = static_cast<float> (i) / 255.0f;
                env[static_cast<size_t> (i)] = t < 0.12f ? t / 0.12f : (t < 0.4f ? 1.0f - 0.35f * (t - 0.12f) / 0.28f : (t < 0.75f ? 0.65f : 0.65f * (1.0f - (t - 0.75f) / 0.25f)));
            }
            display::Module m;
            m.rect = { 14, 360, W - 28, 190 };
            m.plot = { 28, 404, W - 56, 120 };
            m.title = lvl == 1 ? "ENV 1   FULL" : "ENV 1   REDUCED";
            m.highlighted = true;
            m.v = env.data();
            m.n = 256;
            m.dot = 0.3f;
            display::drawModule (s.builder(), m);
            static Knob k[3];
            for (int i = 0; i < 3; ++i)
            {
                k[i].state = Knob::State {};
                k[i].setLabel (i == 0 ? "WT POS" : (i == 1 ? "DETUNE" : "BLEND"));
                k[i].state.value = k[i].state.modValue = 0.3 + 0.25 * i;
                k[i].place ({ 30 + i * 200, 570, 180, 170 });
                k[i].draw (s.builder());
            }
            const Image im = render (s);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    both.px[static_cast<size_t> (y) * static_cast<size_t> (both.w) + static_cast<size_t> ((lvl - 1) * W + x)] = im.at (x, y);
        }
        theme::setDetail (1);
        saveImage (dir, "detail_full_vs_reduced.png", both);
    }
    {
        // 8 frames of the wavetable position moving 0.05 .. 0.95: the lit frames of the stack run along the table
        constexpr int W = 420, H = 240;
        Image strip;
        strip.w = 4 * W;
        strip.h = 2 * H;
        strip.px.assign (static_cast<size_t> (strip.w) * static_cast<size_t> (strip.h), 0u);
        for (int f = 0; f < 8; ++f)
        {
            static DemoDisplay dd;
            dd.wave (0.05f + 0.9f * static_cast<float> (f) / 7.0f, 7, theme::instanceColour (2));
            GScene s (W, H);
            showDisplay (s.builder(), { 6, 6, W - 12, H - 12 }, dd.data, true);
            const Image im = render (s);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    strip.px[static_cast<size_t> ((f / 4) * H + y) * static_cast<size_t> (strip.w) + static_cast<size_t> ((f % 4) * W + x)] = im.at (x, y);
        }
        saveImage (dir, "motion_wavetable_position.png", strip);
    }
    {
        // the envelope and LFO panels (the compact view), the running point in eight places
        constexpr int W = 420, H = 210;
        Image strip;
        strip.w = 4 * W;
        strip.h = 4 * H;
        strip.px.assign (static_cast<size_t> (strip.w) * static_cast<size_t> (strip.h), 0u);
        std::vector<float> env (256), lfo (300);
        for (int i = 0; i < 256; ++i)
        {
            const float t = static_cast<float> (i) / 255.0f;
            env[static_cast<size_t> (i)] = t < 0.12f ? t / 0.12f : (t < 0.4f ? 1.0f - 0.35f * (t - 0.12f) / 0.28f : (t < 0.75f ? 0.65f : 0.65f * (1.0f - (t - 0.75f) / 0.25f)));
        }
        for (int i = 0; i < 300; ++i)
            lfo[static_cast<size_t> (i)] = 0.5f + 0.42f * std::sin (6.2832f * static_cast<float> (i) / 299.0f * 2.0f) * (0.7f + 0.3f * std::sin (6.2832f * static_cast<float> (i) / 299.0f * 5.0f));
        for (int f = 0; f < 8; ++f)
        {
            GScene s (W, H);
            display::Module m;
            m.rect = { 10, 10, W - 20, H - 20 };
            m.plot = { 20, 56, W - 40, H - 84 };
            const float pos = static_cast<float> (f) / 7.0f;
            for (int k = 0; k < 2; ++k)
            {
                m.env = k == 0;
                m.highlighted = k == 0;
                m.title = k == 0 ? "ENV 3" : "LFO 4";
                m.v = k == 0 ? env.data() : lfo.data();
                m.n = k == 0 ? 256 : 300;
                m.dot = pos;
                GScene t (W, H);
                display::drawModule (t.builder(), m);
                const Image im = render (t);
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x)
                        strip.px[static_cast<size_t> ((k * 2 + f / 4) * H + y) * static_cast<size_t> (strip.w) + static_cast<size_t> ((f % 4) * W + x)] = im.at (x, y);
            }
        }
        saveImage (dir, "motion_env_lfo.png", strip);
    }
}

/** Every special sign the screens use (degree, plus minus, middle dot, dashes, ellipsis, the arrows and triangles, the Hungarian accents) at a small and a large size: nothing may fall back to the question mark. */
void glyphImage (const std::string& dir)
{
    GScene s (900, 300);
    const char* rows[] = { "\xc2\xb0  \xc2\xb1  \xc2\xb7  \xe2\x80\x93  \xe2\x80\x94  \xe2\x80\xa6  0\xc2\xb0 180\xc2\xb0 360\xc2\xb0  +12 -7",
                           "\xe2\x86\x90 \xe2\x86\x91 \xe2\x86\x92 \xe2\x86\x93  \xe2\x96\xb2 \xe2\x96\xb6 \xe2\x96\xbc \xe2\x97\x80",
                           "\xc3\x81\xc3\x89\xc3\x8d\xc3\x93\xc3\x96\xc5\x90\xc3\x9a\xc3\x9c\xc5\xb0  \xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xb6\xc5\x91\xc3\xba\xc3\xbc\xc5\xb1" };
    float y = 50.0f;
    for (const char* r : rows)
    {
        s.builder().text (20.0f, y, 36.0f, r, theme::rgba (theme::L().text));
        s.builder().text (20.0f, y + 40.0f, 14.0f, r, theme::rgba (theme::L().textDim));
        y += 90.0f;
    }
    saveImage (dir, "glyphs.png", render (s));
}

int main (int argc, char** argv)
{
    const std::string base = argc > 1 ? argv[1] : ".";
    g_filter = argc > 2 ? argv[2] : "";
    for (int look = 1; look <= 3; ++look)
    {
        theme::setLook (look);
        const std::string dir = base + "/look-" + std::to_string (look);
        mkdir (base.c_str(), 0755);
        mkdir (dir.c_str(), 0755);
        renderAll (dir);
        glyphImage (dir);
    }
    theme::setLook (1);
    std::printf ("%s: %d images, %d failed\n", g_fails ? "FAIL" : "PASS", g_images, g_fails);
    return g_fails ? 1 : 0;
}
