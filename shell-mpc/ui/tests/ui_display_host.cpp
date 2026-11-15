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
                       && l.rBlock > l.rControl && l.rControl > l.rChip && l.gap > 0 && l.backGlow >= 0.0f && l.backGlow < 0.5f && l.q == 0xffc23d;
            for (int j = i + 1; j < 3; ++j)
                distinct = distinct && (looks[j]->flow[0] != l.flow[0] || looks[j]->flow[3] != l.flow[3]) && looks[j]->backGlow != l.backGlow && looks[j]->inst[1] != l.inst[1];
        }
        check (complete, "the three looks have every number (strokes, type scale, radii, spacing, the Q-Link colour)");
        check (distinct, "the three looks differ in their colours, in the faint back glow and in how the instance colours are dealt");
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
        // the live read-outs: a head per voice, the grains (position and life), the phases of the sub, the read head of a sample noise: each changes the picture and stays inside the display
        {
            display::Data base = make (2), h = make (2), g = make (2), w = make (0), pw = make (0), nz = make (4), nz2 = make (4);
            h.heads[0] = 0.25f; h.heads[1] = 0.6f;
            g.grain = false; g.numGrainsLive = 3; g.liveGrainPos[0] = 0.2f; g.liveGrainLife[0] = 0.5f; g.liveGrainPos[1] = 0.5f; g.liveGrainLife[1] = 0.1f; g.liveGrainPos[2] = 0.8f; g.liveGrainLife[2] = 0.9f;
            pw.phases[0] = 0.3f; pw.phases[1] = 0.7f;
            nz2.noiseHead = 0.4f;
            const uint32_t bg = ov::sceneColour (theme::rgba (theme::L().bg0, 255));
            auto pic = [&] (const display::Data& d) { Scene s (680, 360, theme::rgba (theme::L().bg0, 255)); display::draw (s.builder(), r, d); display::drawLive (s.builder(), r, d); return std::make_pair (render (s), s.overflow()); };
            const auto pb = pic (base), ph = pic (h), pg = pic (g), pww = pic (w), ppw = pic (pw), pn = pic (nz), pn2 = pic (nz2);
            const float hx0 = static_cast<float> (r.x + 8) + 0.25f * static_cast<float> (r.w - 16 - 1);
            check (pb.first.px != ph.first.px && colourNear (ph.first, hx0, 150.0f, 2, 0xffffff, 40) && ! ph.second && drawnOutside (ph.first, r, bg) == 0, nm + "live read-outs: a read head line per voice, white at its position");
            check (pb.first.px != pg.first.px && ! pg.second && drawnOutside (pg.first, r, bg) == 0, nm + "live read-outs: the grains that sound are drawn inside the display");
            check (pww.first.px != ppw.first.px && ! ppw.second && drawnOutside (ppw.first, r, bg) == 0, nm + "live read-outs: the phase of the sub's voices are dots on the wave");
            check (pn.first.px != pn2.first.px && ! pn2.second && drawnOutside (pn2.first, r, bg) == 0, nm + "live read-outs: the read head of a sample noise is drawn");
        }
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
        display::drawModuleLive (sm.builder(), m);
        const Image im = render (sm);
        check (! sm.overflow() && drawnOutside (im, m.rect, ov::sceneColour (theme::rgba (theme::L().bg0, 255))) == 0 && colourNear (im, 20.0f + 0.4f * 379.0f, 120.0f, 80, 0xffffff, 8), nm + "module panel: inside its rectangle, the running point is there");
        m.env = false;
        Scene sl (420, 210, theme::rgba (theme::L().bg0, 255));
        display::drawModule (sl.builder(), m);
        check (render (sl).px != im.px, nm + "module panel: the LFO is drawn in its own colours");
    }
    // ---- review 2: the cycle as the display shows it
    {
        // a band limited saw (the first sample in the middle of its jump, ringing at both edges)
        std::vector<float> saw (512), sm (512);
        for (int i = 0; i < 512; ++i)
        {
            float y = 0.0f;
            for (int h = 1; h < 120; ++h)
                y += std::sin (6.2832f * static_cast<float> (h) * static_cast<float> (i) / 512.0f) / static_cast<float> (h);
            saw[static_cast<size_t> (i)] = -y * 0.6366f;
        }
        float rawStep = 0.0f, smStep = 0.0f, rawMax = 0.0f;
        const int m = display::smoothCycle (saw.data(), sm.data(), 512);
        for (int i = 1; i < 512; ++i)
            rawStep = std::max (rawStep, std::fabs (saw[static_cast<size_t> (i)] - saw[static_cast<size_t> (i - 1)]));
        for (int i = 1; i < m; ++i)
            smStep = std::max (smStep, std::fabs (sm[static_cast<size_t> (i)] - sm[static_cast<size_t> (i - 1)]));
        for (float v : saw) rawMax = std::max (rawMax, std::fabs (v));
        float smMax = 0.0f;
        for (int i = 0; i < m; ++i) smMax = std::max (smMax, std::fabs (sm[static_cast<size_t> (i)]));
        check (rawStep > 0.3f && smStep < 0.05f && smMax <= 1.0f && m == 512 - 2 * display::cycleCrop (512), "a band limited saw: the ringing and the half jump at the edges are gone from the display's line (raw step " + std::to_string (rawStep) + ", shown " + std::to_string (smStep) + ")");
        // a stack of identical frames is not drawn, a stack of different ones is; the unison hairlines share the x range of the main curve
        std::vector<float> a, b;
        wave (a, 0.0f); wave (b, 1.0f);
        const float* same[3] = { a.data(), a.data(), a.data() };
        const float* diff[3] = { a.data(), b.data(), b.data() };
        check (! display::framesDiffer (a.data(), same, 3, 512) && display::framesDiffer (a.data(), diff, 3, 512), "the stack of the table: identical frames are no stack, different ones are");
        const Rect r { 10, 10, 600, 300 };
        const uint32_t bg = ov::sceneColour (theme::rgba (theme::L().bg0, 255));
        display::Data d;
        d.kind = display::Data::Kind::Wave; d.n = 512; d.cycle = saw.data(); d.colour = 0x2ee6f0;
        for (int i = 0; i < 3; ++i) d.stack[i] = saw.data();
        d.numStack = 3;
        Scene one (620, 320, theme::rgba (theme::L().bg0, 255)), fan (620, 320, theme::rgba (theme::L().bg0, 255));
        display::draw (one.builder(), r, d, false);
        d.voices = 5; d.detune = 0.6f; d.blend = 1.0f;
        display::draw (fan.builder(), r, d, false);
        const Image i1 = render (one), i2 = render (fan);
        auto columns = [&] (const Image& im, int& first, int& last) { first = -1; last = -1; for (int x = r.x + 12; x < r.right() - 12; ++x) for (int y = r.y + 14; y < r.bottom() - 14; ++y) if (im.at (x, y) != bg && std::abs (static_cast<int> (im.at (x, y) & 0xff) - static_cast<int> (bg & 0xff)) > 80) { if (first < 0) first = x; last = x; break; } };
        int f1, l1, f2, l2;
        columns (i1, f1, l1); columns (i2, f2, l2);
        check (i1.px != i2.px && std::abs (f1 - f2) <= 3 && std::abs (l1 - l2) <= 3 && drawnOutside (i2, r, bg) == 0, "unison: the voices appear as lines around the main curve over the same x range (one voice: only the main curve)");
        // the noise colours: nine pictures, all different, the dim one is fainter
        std::vector<Image> pics;
        for (int c = 0; c < 9; ++c)
        {
            display::Data nd;
            nd.kind = display::Data::Kind::Noise; nd.noiseColour = c;
            Scene sn (620, 320, theme::rgba (theme::L().bg0, 255));
            display::draw (sn.builder(), r, nd, false);
            pics.push_back (render (sn));
            check (! sn.overflow() && drawnOutside (pics.back(), r, bg) == 0, "noise colour " + std::to_string (c) + ": the picture stays inside the display");
        }
        bool different = true;
        for (int i = 0; i < 9; ++i) for (int j = i + 1; j < 9; ++j) different = different && pics[static_cast<size_t> (i)].px != pics[static_cast<size_t> (j)].px;
        display::Data dim;
        dim.kind = display::Data::Kind::Noise; dim.noiseColour = 0; dim.dim = true;
        Scene sd (620, 320, theme::rgba (theme::L().bg0, 255));
        display::draw (sd.builder(), r, dim, false);
        auto lit = [&] (const Image& im) { long t = 0; for (int y = r.y; y < r.bottom(); ++y) for (int x = r.x; x < r.right(); ++x) t += std::abs (static_cast<int> (im.at (x, y) & 0xffffff) - static_cast<int> (bg & 0xffffff)) > 0x300000; return t; };
        check (different && lit (render (sd)) < lit (pics[0]), "the nine noise colours have nine pictures; switched off the picture stays, fainter (no word OFF)");
    }
    theme::setLook (1);
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
