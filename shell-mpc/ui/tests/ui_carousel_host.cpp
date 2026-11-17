// SynthyBoy - MPC UI component library tests: Carousel (the selector of the three thirds of the Play pages): the three zones and their touch targets, the neighbours (with and without wrap), a tap on a neighbour steps, a tap on the name
// is a Request, the horizontal drag steps once per 60 px, the turn, the dots of the envelope carousel, names that fit (no ellipsis), drawing inside the rectangle and the picture of the three entries.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../carousel.h"
#include "../env_screen.h"
#include "ui_test.h"

using namespace uit;

namespace
{
Change tap (Component& c, int x, int y, double t)
{
    const Change d = c.touchDown (x, y, t);
    const Change u = c.touchUp (x, y);
    return u ? u : d;
}
const char* const kNames[5] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" };
}

int main()
{
    Carousel c;
    c.place ({ 6, 6, 408, 56 });
    c.setId (50);
    c.state.count = 5; c.state.index = 2; c.state.names = kNames; c.setItem ("Osc B");
    check (c.sizeOk() && c.minSize().h == 56 && c.minSize().w <= 408, "the carousel has at least its minimum size (240 x 56)");
    Rect t[4];
    const int nt = c.touchTargets (t, 4);
    bool big = nt == 3;
    for (int i = 0; i < nt; ++i)
        big = big && t[i].h >= 56 && t[i].w >= 56;
    check (big && t[0].right() <= t[1].x && t[1].right() <= t[2].x, "three touch targets (the neighbours and the name), each at least 56 px, side by side");
    check (c.neighbour (-1) == 1 && c.neighbour (1) == 3 && std::string (c.nameOf (1)) == "Osc A" && std::string (c.nameOf (3)) == "Osc C" && std::string (c.nameOf (2)) == "Osc B", "the neighbours are the previous and the next entry, named from the owner's array");
    // a tap on the left neighbour: the previous entry; on the right one: the next; on the middle: the request
    Change ch = tap (c, t[0].x + t[0].w / 2, 30, 0.0);
    check (ch.kind == ChangeKind::Index && ch.id == 50 && ch.index == 1 && ch.value == 1.0, "a tap on the left neighbour: Index { id 50, index 1 }");
    ch = tap (c, t[2].x + t[2].w / 2, 30, 500.0);
    check (ch.kind == ChangeKind::Index && ch.id == 50 && ch.index == 3, "a tap on the right neighbour: Index 3");
    ch = tap (c, t[1].x + t[1].w / 2, 30, 1000.0);
    check (ch.kind == ChangeKind::Request && ch.id == 50, "a tap on the name: Request { id 50 } (the owner opens the list)");
    c.listable = false;
    ch = tap (c, t[1].x + t[1].w / 2, 30, 1500.0);
    check (! ch, "not listable: the name does nothing");
    c.listable = true;
    // the ends: with wrap the neighbours go round, without it the missing neighbour is empty and the end does not step
    c.state.index = 4; c.setItem ("Noise");
    c.state.wrap = true;
    check (c.neighbour (1) == 0 && c.neighbour (-1) == 3, "with wrap the neighbour of the last entry is the first");
    ch = tap (c, t[2].x + t[2].w / 2, 30, 2000.0);
    check (ch.kind == ChangeKind::Index && ch.index == 0, "with wrap a tap on the right neighbour of the last entry goes to the first");
    c.state.wrap = false;
    check (c.neighbour (1) == -1 && std::string (c.nameOf (-1)).empty(), "without wrap the missing neighbour is empty");
    ch = tap (c, t[2].x + t[2].w / 2, 30, 2500.0);
    check (! ch, "without wrap nothing happens at the end");
    // the drag: one step per 60 px, to the right brings the previous entries; the steps are counted from where the finger went down
    c.state.index = 2; c.setItem ("Osc B");
    c.touchDown (200, 30, 3000.0);
    ch = c.touchMove (200 + 59, 30);
    check (! ch, "a drag of 59 px is not a step yet");
    ch = c.touchMove (200 + 61, 30);
    check (ch.kind == ChangeKind::Index && ch.index == 1, "a drag of 61 px to the right steps to the previous entry");
    ch = c.touchMove (200 + 125, 30);
    check (ch.kind == ChangeKind::Index && ch.index == 0, "a drag of 125 px: two steps from the start");
    ch = c.touchMove (200 - 130, 30);
    check (ch.kind == ChangeKind::Index && ch.index == 4, "a drag of 130 px to the left: two steps the other way (the end of the list: 4)");
    ch = c.touchUp (200 - 130, 30);
    check (! ch, "the end of a drag is no tap: nothing more");
    c.state.wrap = true;
    // the turn steps like the stepper's
    c.state.index = 1; c.setItem ("Osc A");
    ch = c.turn (1.0);
    check (ch.kind == ChangeKind::Index && ch.id == 50 && ch.index == 2, "a turn steps to the next entry");
    ch = c.turn (-3.0);
    check (ch.kind == ChangeKind::Index && ch.index == 3, "a turn of -3 from 1 with wrap: 3");
    // one entry: nothing steps
    Carousel one;
    one.place ({ 0, 0, 408, 56 }); one.setId (1); one.state.count = 1; one.setItem ("Only");
    check (! tap (one, 10, 20, 0.0) && ! one.turn (1.0), "one entry: a tap on a neighbour and a turn do nothing");
    // the picture: inside its rectangle, the names fit without an ellipsis, the dots of the envelope carousel
    {
        EnvCarousel e;
        e.place ({ 8, 8, 400, 56 });
        e.setId (9);
        e.setTargetMask (0x05);
        e.state.index = 0; e.setItem ("ENV 1");
        check (e.hasTargets (0) && ! e.hasTargets (1) && e.hasTargets (2) && e.state.dotMask == 0x05 && e.state.count == 6 && e.state.wrap && e.listable, "the envelope carousel: six entries, wrap, a dot mask from the targets");
        Scene s (416, 72, kBackgroundRgba);
        e.draw (s.builder());
        const Image im = render (s);
        check (! s.overflow() && drawnOutside (im, { 4, 4, 408, 64 }, backgroundPixel()) == 0, "the envelope carousel draws inside its rectangle (" + std::to_string (s.numOps()) + " ops)");
        const theme::Look& T = theme::L();
        bool fit = true;
        for (const char* n : { "ENV 1", "ENV 2", "ENV 3", "ENV 4", "ENV 5", "ENV 6", "LFO 10", "RND 4", "Osc A", "Noise" })
        {
            const float sizeMid = Pen::fitSize (n, static_cast<float> (e.zone (1).w - 8 - 14), 52.0f, T.fValue, T.fMin), sizeSide = Pen::fitSize (n, static_cast<float> (e.zone (0).w - 16 - 12), 52.0f, T.fLabel + 2.0f, T.fMin);
            fit = fit && sizeMid >= 12.0f && sizeSide >= 12.0f && sdf::textWidth (n, sizeMid) <= static_cast<float> (e.zone (1).w - 22) + 0.5f && sdf::textWidth (n, sizeSide) <= static_cast<float> (e.zone (0).w - 28) + 0.5f;
        }
        check (fit, "every fixed entry name fits its zone at a legible size (no ellipsis)");
        writePng (outDir() + "/carousel_env.png", im, 1);
        e.state.index = 5; e.setItem ("ENV 6");
        Scene s2 (416, 72, kBackgroundRgba);
        e.draw (s2.builder());
        writePng (outDir() + "/carousel_env_last.png", render (s2), 1);
    }
    {
        Carousel q;
        q.place ({ 8, 8, 400, 56 }); q.state.count = 5; q.state.index = 2; q.state.names = kNames; q.setItem ("Osc B"); q.state.wrap = true;
        Scene s (416, 72, kBackgroundRgba);
        q.draw (s.builder());
        const Image a = render (s);
        q.setInactive (true);
        Scene s2 (416, 72, kBackgroundRgba);
        q.draw (s2.builder());
        const Image b = render (s2);
        bool grey = false, differs = false;
        for (int y = 8; y < 64 && ! differs; ++y)
            for (int x = 8; x < 408; ++x)
                if (a.at (x, y) != b.at (x, y)) { differs = true; break; }
        grey = differs;
        check (grey, "the inactive look changes the picture of a component (the grey rule of the theme)");
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
