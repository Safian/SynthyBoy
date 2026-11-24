// SynthyBoy - MPC UI component library tests: the theme (three looks, the colours of the modulation sources, the sizes) and the oscillator display (ui/display.h): every kind of picture
// stays inside its rectangle and inside the frame budget, the highlight of the wavetable stack follows the position, the module panels.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../play_rows.h"

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
        // a wave, a min / max overview and a spectrum of a source that is switched off: the picture stays under a veil (fainter), with a small OFF tag in the corner, inside the display
        std::vector<float> cy2, lo2 (512), hi2 (512), sp2 (512);
        wave (cy2, 0.4f);
        for (int i = 0; i < 512; ++i) { hi2[static_cast<size_t> (i)] = 0.5f * std::exp (-3.0f * static_cast<float> (i) / 512.0f); lo2[static_cast<size_t> (i)] = -hi2[static_cast<size_t> (i)]; sp2[static_cast<size_t> (i)] = -10.0f - 0.1f * static_cast<float> (i); }
        for (int kind : { 0, 2, 3 })
        {
            display::Data on;
            on.colour = theme::instanceColour (1);
            on.n = 512;
            on.cycle = kind == 0 ? cy2.data() : (kind == 2 ? lo2.data() : sp2.data());
            on.cycleMax = kind == 2 ? hi2.data() : nullptr;
            on.kind = kind == 0 ? display::Data::Kind::Wave : (kind == 2 ? display::Data::Kind::MinMax : display::Data::Kind::Spectrum);
            display::Data off = on;
            off.dim = true;
            Scene s1 (620, 320, theme::rgba (theme::L().bg0, 255)), s2 (620, 320, theme::rgba (theme::L().bg0, 255));
            display::draw (s1.builder(), r, on, false);
            display::draw (s2.builder(), r, off, false);
            const Image a = render (s1), b = render (s2);
            const Rect plot = display::plotOf (r, off).inset (8);
            bool grey = true;
            for (int y = plot.y; y < plot.bottom(); ++y)
                for (int x = plot.x; x < plot.right(); ++x)
                {
                    const uint32_t pixel = b.at (x, y);
                    grey = grey && ((pixel >> 16) & 255u) == ((pixel >> 8) & 255u) && ((pixel >> 8) & 255u) == (pixel & 255u);
                }
            check (grey, "the inactive plot contains only grey pixels for kind " + std::to_string (kind));
            const Rect tag { r.x + 14, r.y + 6, 60, 36 };   // where the OFF tag stands: some text pixels there that the picture of the source in use does not have
            long tagPixels = 0;
            for (int y = tag.y; y < tag.bottom(); ++y)
                for (int x = tag.x; x < tag.right(); ++x)
                    tagPixels += std::abs (static_cast<int> (b.at (x, y) & 0xffffff) - static_cast<int> (b.at (tag.x - 4, tag.y) & 0xffffff)) > 0x400000;
            check (! s2.overflow() && drawnOutside (b, r, bg) == 0 && lit (b) < lit (a),   // the OFF word is drawn by the page next to the red power button (the display itself only goes grey)
                    std::string ("a switched off source (kind ") + std::to_string (kind) + "): its picture under a veil, fainter (the OFF word belongs to the page), inside the display (lit " + std::to_string (lit (a)) + " -> " + std::to_string (lit (b)) + ", tag pixels " + std::to_string (tagPixels) + ", outside " + std::to_string (drawnOutside (b, r, bg)) + ")");
        }
    }
    // ---- review 2: the filter curve is cut at the frame (no flat stretch along the bottom); the Q frame is ONE frame; the macro frame runs around the cards; the toggle in a row of knobs has the cell of a knob
    {
        theme::setLook (1);
        FilterDisplay fd;
        fd.place ({ 0, 0, 1000, 400 });
        float falling[200], deep[200];
        for (int i = 0; i < 200; ++i)
        {
            falling[i] = 6.0f - 0.9f * static_cast<float> (i);   // runs under -48 dB at about point 60
            deep[i] = i > 90 && i < 110 ? -90.0f : 0.0f;         // a deep notch in the middle
        }
        auto curves = [&] (const float* db)
        {
            fd.state.curve1 = db; fd.state.numPoints1 = 200; fd.state.dual = false; fd.state.activeFilter = 1;
            Scene s (1000, 400);
            fd.draw (s.builder());
            int n = 0;
            for (uint32_t k = 0; k < s.data->numOps; ++k)
                n += s.data->ops[k].op == gpu::kOpCurve;
            return n;
        };
        check (curves (falling) == 1, "filter display: a curve that falls under -48 dB is ONE line that ends on the bottom edge (no flat stretch)");
        check (curves (deep) == 2, "filter display: a deep notch splits the line into two stretches that leave and re-enter through the bottom edge");
        check (FilterDisplay::kDbBottom == -48.0, "filter display: the bottom of the plot is -48 dB");
    }
    {
        // NEVER STORE THE TRUTH: a touch gives the finger as two changes (X, then Y with takePending) and leaves `state` and the picture alone; only the owner's write shows
        theme::setLook (1);
        FilterDisplay fd;
        fd.assignIds (150, 152, 151, 153);
        fd.place ({ 0, 0, 1000, 400 });
        float flat[200];
        for (int i = 0; i < 200; ++i) flat[i] = -6.0f;
        fd.state.curve1 = flat; fd.state.numPoints1 = 200; fd.state.curve2 = flat; fd.state.numPoints2 = 200;
        fd.state.cutoffHz = fd.state.cutoffHz1 = 500.0; fd.state.resonancePct = fd.state.resonancePct1 = 10.0;
        fd.state.cutoffHz2 = 6000.0; fd.state.resonancePct2 = 45.0;
        const Image before = drawComponent (fd, 1000, 400);
        const Rect pl = fd.plot();
        const int x = pl.x + pl.w * 3 / 4, y = pl.y + pl.h / 4;
        Change cx = fd.touchDown (x, y, 0.0), cy;
        const bool haveY = fd.takePending (cy);
        check (cx.kind == ChangeKind::Value && cx.id == 150 && haveY && cy.kind == ChangeKind::Value && cy.id == 152 && ! fd.takePending (cy), "filter display: a finger down gives X (the cutoff id) and then Y (the resonance id, taken once)");
        check (std::fabs (FilterDisplay::hzOfNorm (cx.value) - 20.0 * std::pow (10.0, 3.0 * 0.75)) < 60.0 && cy.value > 0.6 && cy.value < 0.8 && cx.index == 1 && cy.index == 1, "filter display: X is the position on the log axis (hzOfNorm gives the Hz), Y is 0 .. 1, the index is the filter (1)");
        Change m = fd.touchMove (x, y);
        check (m.kind == ChangeKind::None && ! fd.takePending (cy), "filter display: a move that stays on the same pixel reports nothing");
        m = fd.touchMove (x, y + 20);
        check (m.kind == ChangeKind::Value && m.id == 152 && ! fd.takePending (cy), "filter display: a move that changes only the height reports only the resonance");
        m = fd.touchMove (x + 30, y + 20);
        check (m.kind == ChangeKind::Value && m.id == 150 && ! fd.takePending (cy), "filter display: a move that changes only the frequency reports only the cutoff");
        check (fd.state.cutoffHz == 500.0 && fd.state.resonancePct == 10.0 && fd.state.cutoffHz1 == 500.0 && fd.state.resonancePct1 == 10.0 && fd.state.activeFilter == 1 && fd.state.formantX == 50.0, "filter display: after the gesture with no write back `state` is as the owner left it");
        check (drawComponent (fd, 1000, 400).px != before.px, "filter display: (the bubble of the dragged values is drawn while the finger is down)");
        fd.touchUp (x, y);
        check (drawComponent (fd, 1000, 400).px == before.px, "filter display: with the finger lifted the picture is the same as before the touch");
        fd.state.cutoffHz = fd.state.cutoffHz1 = FilterDisplay::hzOfNorm (cx.value); fd.state.resonancePct = fd.state.resonancePct1 = cy.value * 100.0;   // the owner wrote the parameters and sets the state
        check (drawComponent (fd, 1000, 400).px != before.px, "filter display: with the owner writing back the handle is drawn where the finger was");
        // dual: the finger holds the nearer handle, whatever `state.activeFilter` says
        fd.state.dual = true; fd.state.activeFilter = 1;
        float h2x = 0.0f, h2y = 0.0f;
        fd.handlePointFor (2, h2x, h2y);
        cx = fd.touchDown (static_cast<int> (h2x), static_cast<int> (h2y), 100.0);
        check (cx.index == 2 && fd.takePending (cy) && cy.index == 2 && fd.state.activeFilter == 1, "filter display: dual mode: a finger on the handle of filter 2 reports index 2 and leaves `state.activeFilter` to the owner");
        fd.touchUp (static_cast<int> (h2x), static_cast<int> (h2y));
        // the formant pad
        fd.state.dual = false; fd.state.isFormant = true;
        const Rect fp = fd.plot();
        cx = fd.touchDown (fp.x + fp.w / 4, fp.bottom() - fp.h / 4, 200.0);
        check (cx.id == 151 && fd.takePending (cy) && cy.id == 153 && cx.value < 0.3 && cy.value < 0.3 && fd.state.formantX == 50.0 && fd.state.formantY == 50.0, "filter display: the formant pad gives X (151) and Y (153) and stores neither");
        fd.touchUp (fp.x, fp.y);
    }
    {
        // a member of a Q group never draws a yellow keyline of its own: the picture of an active member is the one of an inactive one
        NumberField a, b;
        a.place ({ 20, 20, 160, 64 }); b.place ({ 20, 20, 160, 64 });
        b.setQ (true, 2);
        Scene sa (200, 100), sb2 (200, 100);
        a.draw (sa.builder()); b.draw (sb2.builder());
        const Image ia = render (sa), ib = render (sb2);
        long yellow = 0;
        for (int y = 20; y < 84; ++y)
            for (int x = 20; x < 180; ++x)
                if (ia.at (x, y) != ib.at (x, y) && (ib.at (x, y) & 0xffffff) == (theme::L().q & 0xffffff))
                    ++yellow;
        check (yellow == 0, "Q rule: a member in the active group has no frame of its own (one frame is the group's)");
        QGroup g;
        g.set (0, &a); g.set (1, &b);
        g.setActive (true);
        Scene s1 (300, 200), s2 (300, 200);
        g.draw (s1.builder());
        const Rect around { 4, 4, 280, 180 };
        g.draw (s2.builder(), &around);
        check (render (s1).px != render (s2).px && drawnOutside (render (s2), { 3, 3, 282, 182 }, 0) == 0, "QGroup: the frame can run around cards (a rectangle given by the owner), inside the rectangle");
        ToggleIcon t;
        t.state.knobLike = true; t.setLabel ("ENV 1");
        t.place ({ 0, 0, 100, 160 });
        Scene st (100, 160);
        t.draw (st.builder());
        check (! st.overflow() && drawnOutside (render (st), { 0, 0, 100, 160 }, 0) == 0, "ToggleIcon in a row of knobs: the picture stays in its cell");
    }
    // ---- the compact view's display (the Play page's third): the picture lies in the plot area below the header band; nothing of a curve, a fill, a grid line or a marker reaches into the band, so none runs under the power, route or full screen button,
    //      for every kind of picture, for a switched off source and for the live read-outs
    for (int look = 1; look <= 3; ++look)
    {
        theme::setLook (look);
        const std::string nm = "compact display, look " + std::to_string (look) + ": ";
        std::vector<float> saw (512), cyc, st[7], mn (512), mx (512), sp (512, -30.0f);   // sp: the dB values of the spectrum kind (512 points); the faint spectrum under a wave uses its first 64
        for (int i = 0; i < 512; ++i)
        {
            saw[static_cast<size_t> (i)] = 2.0f * static_cast<float> (i) / 511.0f - 1.0f;   // the saw: its peak is at the top right, where the full screen button stands
            mx[static_cast<size_t> (i)] = 1.0f - 0.9f * static_cast<float> (i) / 512.0f;
            mn[static_cast<size_t> (i)] = -mx[static_cast<size_t> (i)];
        }
        wave (cyc, 0.4f);
        for (int k = 0; k < 7; ++k)
            wave (st[k], static_cast<float> (k) / 6.0f);
        const Rect r { 14, 190, 408, 292 };   // the display rectangle of a third (playrows::displayRect of a 420 x 628 third)
        const Rect area = playrows::plotRect (r), band = playrows::bandRect (r);
        const Rect buttons[3] = { playrows::powerRect (r), playrows::routeRect (r), playrows::fullRect (r) };
        check (! area.empty() && area.y >= band.bottom() && area.x >= r.x && area.right() <= r.right() && area.bottom() <= r.bottom() && ! area.overlaps (band), nm + "the plot area lies below the band, inside the display");
        const uint32_t bg = ov::sceneColour (theme::rgba (theme::L().bg0, 255));
        struct Case { const char* name; int kind; bool dim; };
        const Case cases[] = { { "saw wave", 0, false }, { "saw wave, switched off", 0, true }, { "wavetable stack with unison", 1, false }, { "min / max with loop, grain and playhead", 2, false },
                               { "spectrum", 3, false }, { "message", 4, false }, { "noise", 5, false }, { "noise, switched off", 5, true } };
        for (const Case& cs : cases)
        {
            display::Data d;
            d.colour = theme::instanceColour (1);
            d.n = 512;
            d.cycle = saw.data();
            d.area = area;
            d.dim = cs.dim;
            d.kind = cs.kind <= 1 ? display::Data::Kind::Wave : cs.kind == 2 ? display::Data::Kind::MinMax : cs.kind == 3 ? display::Data::Kind::Spectrum : cs.kind == 4 ? display::Data::Kind::Message : display::Data::Kind::Noise;
            d.message = "NO SAMPLE";
            if (cs.kind == 1)
            {
                d.cycle = cyc.data();
                d.numStack = 7;
                for (int k = 0; k < 7; ++k) d.stack[k] = st[static_cast<size_t> (k)].data();
                d.voices = 7; d.detune = 1.0f; d.blend = 1.0f; d.spectrum = sp.data(); d.numSpectrum = 64; d.pos = 0.4f;
            }
            if (cs.kind == 2) { d.cycle = mn.data(); d.cycleMax = mx.data(); d.loopOn = true; d.loopStart = 0.2f; d.loopEnd = 0.7f; d.grain = true; d.grainPos = 0.3f; d.grainSize = 0.1f; d.playhead = 0.5f; d.numGrainsLive = 2; d.liveGrainPos[0] = 0.1f; d.liveGrainPos[1] = 0.9f; d.liveGrainLife[0] = 0.5f; d.liveGrainLife[1] = 0.5f; d.heads[0] = 0.95f; }
            if (cs.kind == 3) d.cycle = sp.data();
            if (cs.kind <= 1) { d.phases[0] = 0.99f; d.phases[1] = 0.0f; }
            Scene s (440, 520, theme::rgba (theme::L().bg0, 255));
            display::draw (s.builder(), r, d, false);
            display::drawLive (s.builder(), r, d);
            const Image im = render (s);
            check (! s.overflow() && drawnOutside (im, r, bg) == 0, nm + cs.name + ": drawn inside the display");
            check (dataOpsInto (s, band) == 0, nm + cs.name + ": no curve, fill, grid line or marker reaches into the header band (" + std::to_string (dataOpsInto (s, band)) + ")");
            for (int b = 0; b < 3; ++b)
                check (dataOpsInto (s, buttons[b]) == 0, nm + cs.name + ": nothing of the picture lies under the " + (b == 0 ? "power" : b == 1 ? "route" : "full screen") + " button");
            display::Data whole = d;   // the same picture without a plot area (the pages): it reaches up into the band, so the test above can fail
            whole.area = Rect {};
            Scene sw (440, 520, theme::rgba (theme::L().bg0, 255));
            display::draw (sw.builder(), r, whole, false);
            if (cs.kind <= 1 && ! cs.dim)
                check (dataOpsInto (sw, band) > 0, nm + cs.name + ": (control) without a plot area the same picture does reach into the band");
        }
    }
    theme::setLook (1);
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
