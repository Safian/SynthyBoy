// SynthyBoy - MPC UI component library tests: the theme (three looks, the colours of the modulation sources, the sizes) and the oscillator display (ui/display.h): every kind of picture
// stays inside its rectangle and inside the frame budget, the highlight of the wavetable stack follows the position, the module panels.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"

using namespace uit;

namespace
{
void wave (std::vector<float>& v, float pos)
{
    v.assign (512, 0.0f);
    for (int i = 0; i < 512; ++i)
        v[static_cast<size_t> (i)] = 0.9f * (std::sin (6.2832f * static_cast<float> (i) / 512.0f) * (1.0f - pos) + (2.0f * static_cast<float> (i) / 512.0f - 1.0f) * pos);
}
}

int main()
{
    // ---- the looks: three tables, all complete, different from each other; the colour of a source is the same everywhere
    {
        const theme::Look* looks[3] = { &theme::kLook1, &theme::kLook2, &theme::kLook3 };
        bool complete = true, distinct = true;
        for (int i = 0; i < 3; ++i)
        {
            const theme::Look& l = *looks[i];
            complete = complete && l.name[0] != '\0' && l.curve > 0.0f && l.curve < 2.0f && l.curveHi > l.curve && l.curveBack < l.curve && l.fTitle > l.fValue && l.fValue > l.fLabel && l.fLabel > l.fSmall && l.fSmall >= l.fMin
                       && l.rBlock > l.rControl && l.rControl > l.rChip && l.gap > 0 && l.glowAlpha > 0.0f && l.q == 0xffc23d;
            for (int j = i + 1; j < 3; ++j)
                distinct = distinct && (looks[j]->flow[0] != l.flow[0] || looks[j]->flow[3] != l.flow[3]) && looks[j]->glowAlpha != l.glowAlpha && looks[j]->inst[1] != l.inst[1];
        }
        check (complete, "the three looks have every number (strokes, type scale, radii, spacing, the Q-Link colour)");
        check (distinct, "the three looks differ in their colours, in the light and in how the instance colours are dealt");
        theme::setLook (2);
        check (&theme::L() == &theme::kLook2, "setLook (2) selects look 2");
        theme::setLook (7);
        check (&theme::L() == &theme::kLook1, "an unknown look selects look 1");
        check (theme::sourceColour ("ENV 1") == theme::sourceColour ("ENV 3") && theme::sourceColour ("LFO 2") == theme::sourceColour ("LFO 7") && theme::sourceColour ("ENV 1") != theme::sourceColour ("LFO 1"), "a modulation source has one colour (all envelopes alike, all LFOs alike, the two kinds apart)");
        check (theme::sourceColour ("ENV 1") == theme::L().env[0] || theme::sourceColour ("ENV 1") == theme::L().src[0], "the envelope source colour is the orange of the envelope");
    }
    // ---- the displays
    for (int look = 1; look <= 3; ++look)
    {
        theme::setLook (look);
        const std::string nm = "look " + std::to_string (look) + ": ";
        std::vector<float> cyc, st[7], mn (512), mx (512), spec (64, 0.3f);
        wave (cyc, 0.4f);
        for (int k = 0; k < 7; ++k)
            wave (st[k], static_cast<float> (k) / 6.0f);
        for (int i = 0; i < 512; ++i) { mx[static_cast<size_t> (i)] = 0.5f * std::exp (-3.0f * static_cast<float> (i) / 512.0f); mn[static_cast<size_t> (i)] = -mx[static_cast<size_t> (i)]; }
        const Rect r { 40, 30, 600, 300 };
        auto make = [&] (int kind) -> display::Data
        {
            display::Data d;
            d.colour = theme::instanceColour (1);
            d.n = 512;
            d.cycle = cyc.data();
            d.kind = kind == 0 || kind == 1 ? display::Data::Kind::Wave : (kind == 2 ? display::Data::Kind::MinMax : (kind == 3 ? display::Data::Kind::Spectrum : display::Data::Kind::Message));
            d.message = "NOISE";
            if (kind == 1)
            {
                d.numStack = 7;
                for (int k = 0; k < 7; ++k) d.stack[k] = st[static_cast<size_t> (k)].data();
                d.voices = 7; d.detune = 1.0f; d.blend = 1.0f; d.spectrum = spec.data(); d.numSpectrum = 64; d.pos = 0.4f;
            }
            if (kind == 2) { d.cycle = mn.data(); d.cycleMax = mx.data(); d.loopOn = true; d.loopStart = 0.2f; d.loopEnd = 0.7f; d.grain = true; d.grainPos = 0.3f; d.grainSize = 0.1f; d.playhead = 0.5f; }
            return d;
        };
        const char* names[5] = { "wave", "wavetable with stack and unison", "min / max with loop, grain and playhead", "spectrum", "message" };
        for (int kind = 0; kind < 5; ++kind)
        {
            Scene s (680, 360, theme::rgba (theme::L().bg0, 255));
            display::draw (s.builder(), r, make (kind));
            const Image im = render (s);
            const uint32_t bg = ov::sceneColour (theme::rgba (theme::L().bg0, 255));
            check (! s.overflow() && s.numOps() < 200 && s.data->numPoints < 16000u, nm + names[kind] + ": " + std::to_string (s.numOps()) + " ops, " + std::to_string (s.data->numPoints) + " vertices (a small part of the frame)");
            check (drawnOutside (im, r, bg) == 0 && drawnInside (im, r, bg) > 2000, nm + names[kind] + ": drawn inside its rectangle only");
        }
        // the lit part of the stack follows the position: the picture changes, the brightness near the lit frame changes place
        display::Data a = make (1), b = make (1);
        a.pos = 0.1f; b.pos = 0.9f;
        Scene sa (680, 360, theme::rgba (theme::L().bg0, 255)), sb (680, 360, theme::rgba (theme::L().bg0, 255));
        display::draw (sa.builder(), r, a);
        display::draw (sb.builder(), r, b);
        check (render (sa).px != render (sb).px, nm + "moving the wavetable position changes the picture (the highlight runs along the stack)");
        // the module panels
        std::vector<float> v (256);
        for (int i = 0; i < 256; ++i) v[static_cast<size_t> (i)] = 0.5f + 0.4f * std::sin (static_cast<float> (i) * 0.05f);
        display::Module m;
        m.rect = { 10, 10, 400, 190 };
        m.plot = { 20, 56, 380, 120 };
        m.title = "ENV 1";
        m.v = v.data();
        m.n = 256;
        m.dot = 0.4f;
        Scene sm (420, 210, theme::rgba (theme::L().bg0, 255));
        display::drawModule (sm.builder(), m);
        const Image im = render (sm);
        check (! sm.overflow() && drawnOutside (im, m.rect, ov::sceneColour (theme::rgba (theme::L().bg0, 255))) == 0 && colourNear (im, 20.0f + 0.4f * 379.0f, 120.0f, 80, 0xffffff, 8), nm + "module panel: inside its rectangle, the running point is there");
        m.env = false;
        Scene sl (420, 210, theme::rgba (theme::L().bg0, 255));
        display::drawModule (sl.builder(), m);
        check (render (sl).px != im.px, nm + "module panel: the LFO is drawn in its own colours");
    }
    theme::setLook (1);
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
