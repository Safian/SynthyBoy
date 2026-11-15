// SynthyBoy - MPC UI component library: the DEMONSTRATION of the envelope element (a host tool): renders the EnvGraph, the compact view and the full screen of the envelope into PNG files in the directory
// given as the first argument (one sub directory per look). The data is synthetic.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sys/stat.h>

#include "ui_test.h"
#include "../env_screen.h"

using namespace uit;

namespace
{
int g_images = 0;
struct GScene : Scene
{
    GScene (int w, int h) : Scene (w, h, theme::rgba (theme::L().bg0, 255))
    {
        const theme::Look& T = theme::L();
        builder().gradientRect (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h), theme::rgba (T.bg0), theme::rgba (T.bg1));
    }
};
bool save (const std::string& dir, const char* name, const Scene& s)
{
    check (! s.overflow(), std::string (name) + ": the scene fits (" + std::to_string (s.numOps()) + " ops of " + std::to_string (gpu::kMaxOps) + ")");
    const bool ok = writePng (dir + "/" + name, render (s), 1);
    check (ok, std::string (name) + ": written");
    g_images += ok;
    return ok;
}
}

// a made up envelope: the graph, the knobs and the fields with the same numbers
static void fillShape (EnvGraph& g, int env)
{
    g.state.shape = envgraph::Shape();
    g.state.shape.t[1] = 0.1 + 0.05 * env; g.state.shape.t[3] = 0.38; g.state.shape.t[4] = 0.3 + 0.04 * env;
    g.state.shape.sustain = 0.6;
    g.state.defaults = envgraph::Shape();
    g.state.colour = envColour (env);
    g.state.ghost = env != 0;
    g.state.ghostShape = envgraph::Shape();
    g.state.numDots = 3; g.state.dots[0] = { 4, 0.4f }; g.state.dots[1] = { 6, 0.5f }; g.state.dots[2] = { 2, 0.7f };
    std::snprintf (g.state.text[envgraph::Delay], 16, "0 ms"); std::snprintf (g.state.text[envgraph::Attack], 16, "5.0 ms"); std::snprintf (g.state.text[envgraph::Hold], 16, "0 ms");
    std::snprintf (g.state.text[envgraph::Decay], 16, "400 ms"); std::snprintf (g.state.text[envgraph::Release], 16, "200 ms");
    std::snprintf (g.state.text[envgraph::CurveA], 16, "+0.0"); std::snprintf (g.state.text[envgraph::CurveD], 16, "-3.0"); std::snprintf (g.state.text[envgraph::CurveR], 16, "-3.0");
}
static void knob (Knob& k, double v, const char* text) { k.state.value = k.state.modValue = v; std::snprintf (k.state.valueText, sizeof k.state.valueText, "%s", text); }

static void renderAll (const std::string& dir)
{
    {
        static EnvCompact c;
        c.place ({ 0, 0, 400, 620 });
        c.instances.state.index = 2;
        fillShape (c.graph, 2);
        c.graph.state.selected = envgraph::Attack;
        knob (c.attack, 0.12, "5.0 ms"); knob (c.decay, 0.38, "400 ms"); knob (c.sustain, 0.6, "60%"); knob (c.release, 0.3, "200 ms");
        c.attack.state.add (7, "LFO 2", 0.35); c.attack.state.selectedSource = 7; c.attack.state.modValue = 0.2;
        GScene s (400, 620);
        c.draw (s.builder());
        c.drawLive (s.builder());
        save (dir, "compact.png", s);
    }
    {
        static EnvFullScreen f;
        f.place ({ 0, 0, 1280, 628 });
        f.instances.state.index = 2;
        fillShape (f.graph, 2);
        f.graph.state.selected = envgraph::Decay;
        knob (f.attack, 0.12, "5.0 ms"); knob (f.decay, 0.38, "400 ms"); knob (f.sustain, 0.6, "60%"); knob (f.release, 0.3, "200 ms");
        knob (f.aCurve, 0.5, "+0.0"); knob (f.dCurve, 0.4, "-3.0"); knob (f.rCurve, 0.4, "-3.0"); knob (f.delay, 0.0, "0 ms"); knob (f.hold, 0.0, "0 ms");
        f.beats.state.on = false; f.legato.state.on = true;
        const char* fv[6] = { "0", "5", "0", "400", "60", "200" };
        const char* ls[6] = { "DELAY ms", "ATTACK ms", "HOLD ms", "DECAY ms", "SUSTAIN %", "RELEASE ms" };
        for (int i = 0; i < 6; ++i) { f.fields[i].setLabel (ls[i]); f.fields[i].state.value = std::atof (fv[i]); f.fields[i].state.max = 10000; }
        f.targets.setValue ("3");
        f.numTargets = 3; f.numTargetLines = 3;
        std::snprintf (f.targetLines[0].text, 40, "Filter 1 Cutoff   +35%%"); std::snprintf (f.targetLines[1].text, 40, "Osc A Level   +100%%"); std::snprintf (f.targetLines[2].text, 40, "Osc B Detune   -12%%");
        GScene s (1280, 628);
        f.draw (s.builder());
        f.drawLive (s.builder());
        save (dir, "fullscreen.png", s);
    }
    {
        static EnvGraph g;
        g.place ({ 20, 20, 388, 336 });
        g.state.shape.t[0] = 0.0; g.state.shape.t[1] = 0.1; g.state.shape.t[2] = 0.0; g.state.shape.t[3] = 0.38; g.state.shape.t[4] = 0.3;
        g.state.shape.sustain = 0.6;
        g.state.defaults = g.state.shape;
        g.state.selected = envgraph::Decay;
        std::snprintf (g.state.text[envgraph::Decay], 16, "400 ms  60%%");
        g.state.numDots = 2; g.state.dots[0] = { 4, 0.4f }; g.state.dots[1] = { 6, 0.5f };
        GScene s (428, 376);
        g.draw (s.builder());
        g.drawLive (s.builder());
        save (dir, "graph_compact.png", s);
    }
}

int main (int argc, char** argv)
{
    const std::string base = argc > 1 ? argv[1] : ".";
    mkdir (base.c_str(), 0755);
    for (int look = 1; look <= 3; ++look)
    {
        theme::setLook (look);
        const std::string dir = base + "/look-" + std::to_string (look);
        mkdir (dir.c_str(), 0755);
        renderAll (dir);
    }
    theme::setLook (1);
    std::printf ("%s: %d images, %d failed\n", g_fails ? "FAIL" : "PASS", g_images, g_fails);
    return g_fails ? 1 : 0;
}
