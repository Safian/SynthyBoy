// SynthyBoy - bitmap GUI: how long the module needs for a frame (not a test: prints numbers). The software renderer is the same code everywhere, the CPU is not: run it on the target.
// Usage: synthyboy_bitmap_gui_bench   (SYNTHYBOY_CLAP_UNDER_TEST = the .clap)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cstdio>

#include "bitmap_gui_harness.h"

using namespace bgtest;

namespace
{
double ms (Clock::duration d) { return std::chrono::duration<double, std::milli> (d).count(); }

struct Stats
{
    double sum = 0.0, worst = 0.0;
    int n = 0;
    void add (double v) { sum += v; worst = std::max (worst, v); ++n; }
    double mean() const { return n > 0 ? sum / n : 0.0; }
};

void sizeCase (uint32_t w, uint32_t h)
{
    Instance host;
    host.activate();
    const auto t0 = Clock::now();
    host.create (w, h, 1.0);
    const double createMs = ms (Clock::now() - t0);
    FrameInfo f;
    const auto t1 = Clock::now();
    host.render (f);
    const double firstMs = ms (Clock::now() - t1);
    host.tickFor (300);
    host.render (f);
    host.render (f);

    // idle: tick () + is_dirty () when nothing happens
    const auto t2 = Clock::now();
    constexpr int kIdle = 2000;
    for (int i = 0; i < kIdle; ++i)
    {
        host.gui->tick (host.plugin);
        host.gui->is_dirty (host.plugin);
    }
    const double idleUs = ms (Clock::now() - t2) * 1000.0 / kIdle;

    // a knob turning: every move is one small dirty area
    Stats drag, dragArea;
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_DOWN, 587, 498, 1, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);
    for (int i = 0; i < 60 && w >= 1000; ++i)
    {
        host.pointer (SAFIAN_BITMAP_GUI_POINTER_MOVE, 587, 498 - i, 1, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);
        const auto t = Clock::now();
        host.render (f);
        drag.add (ms (Clock::now() - t));
        dragArea.add (static_cast<double> (f.dirtyArea()));
    }
    host.pointer (SAFIAN_BITMAP_GUI_POINTER_UP, 587, 438, 1, SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE);

    // the wavetable view animating: osc1.frame moves every frame (host automation), the view of oscillator 1 repaints
    Stats anim, animArea;
    for (int i = 0; i < 150 && w >= 1000; ++i)
    {
        host.queueParam ("osc1.frame", 50.0 + 50.0 * std::sin (i * 0.2));
        host.processBlock (64);
        const auto t = Clock::now();
        host.render (f);
        anim.add (ms (Clock::now() - t));
        animArea.add (static_cast<double> (f.dirtyArea()));
    }
    std::printf ("%4u x %-4u create %6.1f ms | first full frame %6.1f ms | idle tick+is_dirty %5.2f us | knob drag %6.2f ms mean (worst %6.2f, %6.0f px^2) | wavetable animation %6.2f ms mean (worst %6.2f, %6.0f px^2)\n",
                 w, h, createMs, firstMs, idleUs, drag.mean(), drag.worst, dragArea.mean(), anim.mean(), anim.worst, animArea.mean());
}
} // namespace

int main()
{
    if (Module::get().factory == nullptr)
        return 1;
    sizeCase (1024, 600);
    sizeCase (1024, 768);
    sizeCase (390, 844);
    Module::get().shutdown();
    return 0;
}
