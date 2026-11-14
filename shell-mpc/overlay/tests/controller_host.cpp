// SynthyBoy - MPC overlay tests: the controller state machine with a fake display backend, a fake framebuffer and a fake clock; then the same controller on a real thread
// (run it under ThreadSanitizer too).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <thread>

#include "fakes.h"

using namespace ovt;

struct Rig
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    std::unique_ptr<Controller> ctl;
    double now = 0.0;

    explicit Rig (bool attach = true, double cost = 10.0)
    {
        ctl = std::make_unique<Controller> (plane, frame, renderer, dir.paths(), Timing(), cost);
        inst.set ("filter1.cutoff", 1000.0f);
        inst.set ("filter1.resonance", 30.0f);
        if (attach)
            ctl->attach (&inst);
    }
    void run (double ms, double step = 10.0)
    {
        const double end = now + ms;
        while (now < end) { now += step; ctl->tick (now); }
    }
    /** Runs until `cond` holds; returns the milliseconds it took (or -1 after maxMs). */
    template <class F> double until (F cond, double maxMs, double step = 10.0)
    {
        const double start = now;
        while (now - start < maxMs) { now += step; ctl->tick (now); if (cond()) return now - start; }
        return -1.0;
    }
    bool on() const { return plane.planeOn; }
};

static std::string line (const std::string& text, const std::string& key)
{
    const size_t p = text.find (key + ": ");
    if (p == std::string::npos) return "";
    const size_t e = text.find ('\n', p);
    return text.substr (p + key.size() + 2, e - p - key.size() - 2);
}

int main()
{
    const Rect curveUi { layout::kViews[0].region.x, layout::kViews[0].region.y, layout::kViews[0].region.w, layout::kViews[0].region.h };
    // ---- the markers are there from the start: shown at the first check ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        const double t = r.until ([&] { return r.on(); }, 500);
        check (t >= 0 && t <= 100, "markers present: the plane is on within 100 ms (took " + std::to_string (t) + " ms)");
        check (r.plane.lastRect == uiRectToPanel (90, curveUi, 800, 1280), "the plane rectangle is the curve area on the panel (rotation 90)");
        int alpha = 0;
        for (uint32_t p : r.plane.shownCopy) alpha += (p >> 24) != 0;
        check (alpha > 1000, "the picture on the plane has pixels");
        check (r.ctl->state() == "visible", "state: visible");
        r.run (1200);
        const std::string st = r.dir.read ("status.txt");
        check (line (st, "state") == "visible" && ! line (st, "curve_hash").empty() && ! line (st, "markers_read_filter1").empty() && line (st, "view") == "filter1" && line (st, "rotation") == "90", "the status file says visible and has the hash, the marker values and the rotation");
        {
            const Rect pr = uiRectToPanel (90, curveUi, 800, 1280);
            const std::string want = std::to_string (pr.w) + "x" + std::to_string (pr.h) + "+" + std::to_string (pr.x) + "+" + std::to_string (pr.y);
            check (line (st, "plane_rect") == want && pr.w == 232 && pr.h == 1168, "the status file has the plane rectangle (" + want + ")");
        }
        // nothing changes: no more pictures
        const int shown = r.plane.presented;
        r.run (3000);
        check (r.plane.presented == shown, "nothing changed for 3 s: no new picture was presented (" + std::to_string (shown) + " -> " + std::to_string (r.plane.presented) + ")");
        // a parameter changes: a new picture
        r.inst.set ("filter1.resonance", 70.0f);
        r.run (300);
        check (r.plane.presented > shown, "a parameter changed: a new picture was presented");
        // the picture follows a moving cutoff at about 30 pictures a second
        r.inst.hasLive = true;
        const int before = r.plane.presented;
        for (int i = 0; i < 60; ++i) { r.inst.liveCutoff = 600.0f + 30.0f * (float) i; r.now += 33.0; r.ctl->tick (r.now); }
        const int frames = r.plane.presented - before;
        check (frames >= 50 && frames <= 62, "cutoff moving every 33 ms for 2 s: " + std::to_string (frames) + " pictures (about 30 a second)");
    }
    // ---- the markers appear later; one disappears; a colour that is 3 off is not accepted, 2 off is ----
    {
        Rig r;
        r.run (500);
        check (! r.on() && r.plane.countOf ("create") == 0, "no markers: no plane, no buffers");
        check (r.ctl->state() == "hidden: no view on screen", "state: hidden: no view on screen");
        r.frame.setAll (true, 90);
        const double t = r.until ([&] { return r.on(); }, 500);
        check (t >= 0 && t <= 110, "the markers appear: shown within 110 ms (took " + std::to_string (t) + " ms)");
        r.frame.setMarker (1, false, 90);
        const double h = r.until ([&] { return ! r.on(); }, 500);
        check (h >= 0 && h <= 150, "one marker missing: hidden within 150 ms (took " + std::to_string (h) + " ms)");
        check (! r.plane.buffersAlive, "... and the buffers are freed");
        check (r.plane.calls() > 0 && r.plane.callLog().back() == "destroy", "... plane off first, then the buffers");
        r.frame.setMarker (1, true, 90, layout::kViews[0].markers[1].rgb + 2);   // +2 in blue
        check (r.until ([&] { return r.on(); }, 300) >= 0, "a marker 2 off in one channel is accepted");
        r.frame.setMarker (1, true, 90, layout::kViews[0].markers[1].rgb + 3);
        check (r.until ([&] { return ! r.on(); }, 300) >= 0, "a marker 3 off is not");
        r.frame.setMarker (1, true, 90);
        r.until ([&] { return r.on(); }, 300);
        // the framebuffer cannot be read for one check: hidden, and it comes back
        r.frame.readable = false;
        check (r.until ([&] { return ! r.on(); }, 300) >= 0, "framebuffer unreadable: hidden");
        r.frame.readable = true;
        check (r.until ([&] { return r.on(); }, 300) >= 0, "readable again: back");
    }
    // ---- failures disable for good, without a retry storm ----
    {
        Rig r;
        r.plane.failSetPlaneAt = 1;
        r.frame.setAll (true, 90);
        r.run (400);
        check (r.ctl->disabled() && r.ctl->state().find ("disabled: setplane") == 0, "SETPLANE fails: disabled (" + r.ctl->state() + ")");
        const size_t calls = r.plane.calls();
        r.run (6000);
        check (r.plane.calls() == calls, "... and not a single backend call in the 6 s after that (no retry storm)");
        check (line (r.dir.read ("status.txt"), "state").find ("disabled: setplane") == 0, "the status file has the one line with the reason");
        check (! r.plane.planeOn && ! r.plane.buffersAlive, "... plane off, buffers freed");
        r.dir.touch ("off"); r.run (1500); r.dir.remove ("off"); r.run (1500);
        check (r.ctl->disabled() && ! r.on(), "the kill switch going on and off does not bring it back");
    }
    {
        Rig r;
        r.plane.failOpenAt = 1;
        r.frame.setAll (true, 90);
        r.run (3000);
        check (r.ctl->disabled() && r.plane.countOf ("open") == 1 && r.plane.countOf ("create") == 0, "open fails (no descriptor): disabled after one attempt");
        check (r.ctl->state().find ("disabled: open: injected open failure") == 0, "... with the reason: " + r.ctl->state());
    }
    {
        Rig r;
        r.plane.failCreateAt = 1;
        r.frame.setAll (true, 90);
        r.run (3000);
        check (r.ctl->disabled() && r.plane.countOf ("create") == 1 && ! r.plane.buffersAlive, "buffer creation fails: disabled, tried once, nothing left allocated");
    }
    {
        Rig r;
        r.plane.failSetPlaneAt = 6;
        r.frame.setAll (true, 90);
        r.inst.hasLive = true;
        for (int i = 0; i < 60; ++i) { r.inst.liveCutoff = 600.0f + 30.0f * (float) i; r.now += 33.0; r.ctl->tick (r.now); }
        check (r.ctl->disabled() && r.plane.presented == 5, "a failure in the middle of the run (6th SETPLANE): disabled, 5 pictures were shown");
        const size_t calls = r.plane.calls();
        for (int i = 0; i < 100; ++i) { r.inst.liveCutoff = 700.0f + (float) i; r.now += 33.0; r.ctl->tick (r.now); }
        check (r.plane.calls() == calls && ! r.plane.planeOn && ! r.plane.buffersAlive, "... no further call, plane off, buffers freed");
    }
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.frame.readable = false;
        r.run (1500);
        check (! r.ctl->disabled(), "framebuffer unreadable for 1.5 s: still waiting");
        r.run (1500);
        check (r.ctl->disabled() && r.ctl->state().find ("disabled: the framebuffer cannot be read") == 0 && r.plane.countOf ("setplane") == 0, "unreadable for 3 s: disabled (" + r.ctl->state() + ")");
    }
    // ---- the kill switch ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.dir.touch ("off");
        const double h = r.until ([&] { return ! r.on(); }, 3000);
        check (h >= 0 && h <= 1100, "kill switch file: hidden within 1.1 s (took " + std::to_string (h) + " ms)");
        check (r.ctl->state() == "hidden: kill switch" && ! r.plane.buffersAlive, "state: hidden: kill switch, buffers freed");
        const int shown = r.plane.presented;
        r.run (3000);
        check (r.plane.presented == shown, "no picture while the switch file exists");
        r.dir.remove ("off");
        const double s = r.until ([&] { return r.on(); }, 3000);
        check (s >= 0 && s <= 1200, "file removed: back within 1.2 s (took " + std::to_string (s) + " ms)");
    }
    // ---- several instances ----
    {
        Rig r;
        FakeInstance second;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.ctl->attach (&second);
        const double h = r.until ([&] { return ! r.on(); }, 300);
        check (h >= 0 && h <= 20, "a second instance appears: hidden at once");
        check (r.ctl->state() == "hidden: several instances", "state: hidden: several instances");
        const int shown = r.plane.presented;
        r.run (2000);
        check (r.plane.presented == shown && ! r.plane.buffersAlive, "two instances: nothing is presented, nothing is allocated");
        r.ctl->detach (&second);
        check (r.until ([&] { return r.on(); }, 400) >= 0, "back to one instance: shown again");
        check (r.ctl->detach (&r.inst) == 0, "the last instance leaves");
        r.run (100);
        check (! r.on() && r.ctl->state() == "hidden: no instance", "no instance: hidden");
    }
    // ---- close while visible ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.ctl->detach (&r.inst);
        r.ctl->shutdown();
        const auto log = r.plane.callLog();
        size_t lastDisable = 0, lastDestroy = 0;
        for (size_t i = 0; i < log.size(); ++i) { if (log[i] == "disable") lastDisable = i; if (log[i] == "destroy") lastDestroy = i; }
        check (! r.plane.planeOn && ! r.plane.buffersAlive, "close while visible: plane off and buffers freed");
        check (lastDisable < lastDestroy && lastDestroy == log.size() - 1, "... in the right order: the plane is taken off before the buffers go");
        check (line (r.dir.read ("status.txt"), "state") == "hidden: no instance", "... and the status file says so");
        r.ctl->shutdown();
        check (r.plane.callLog().size() == log.size(), "a second shutdown (the exit handler) does nothing");
    }
    // ---- the rotation file ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.dir.write ("rot", "270\n");
        r.run (1300);
        check (! r.on() && r.ctl->rotation() == 270, "rotation file 270: the overlay re-reads it within a second; the markers are not where the 270 mapping looks, so it is hidden");
        r.frame.setAll (false, 90);
        r.frame.setAll (true, 270);
        check (r.until ([&] { return r.on(); }, 400) >= 0, "markers drawn for 270: shown");
        check (r.plane.lastRect == uiRectToPanel (270, curveUi, 800, 1280), "... at the 270 rectangle");
        const Rect rr = r.plane.lastRect;
        check (rr.x == 800 - (curveUi.y + curveUi.h) && rr.y == curveUi.x, "... which is x = panelW - (y + h), y = x of the UI");
        check (line (r.dir.read ("status.txt"), "rotation") == "270", "the status file says 270");
        r.dir.write ("rot", "banana");
        r.run (1300);
        check (r.ctl->rotation() == 90, "an unreadable rotation file means 90");
    }
    // ---- the curve dump on request ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.dir.touch ("dump");
        r.run (2200);
        const std::string d = r.dir.read ("curve.txt");
        check (d.find ("# view filter1: index shown_db exact_db") == 0 && std::count (d.begin(), d.end(), '\n') == 257, "the dump file has the 256 values");
    }
    // ---- the same controller on a thread, with parameters, markers and instances changing under it (ThreadSanitizer) ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        std::atomic<bool> stop { false };
        std::thread t ([&]
        {
            const auto start = std::chrono::steady_clock::now();
            while (! stop.load())
            {
                const double now = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count() + 1.0;
                const double wait = r.ctl->tick (now);
                std::this_thread::sleep_for (std::chrono::microseconds ((long long) (wait * 1000.0)));
            }
        });
        FakeInstance second;
        for (int i = 0; i < 60; ++i)
        {
            r.inst.set ("filter1.resonance", 10.0f + (float) (i % 7) * 10.0f);
            r.inst.hasLive = true;
            r.inst.liveCutoff = 500.0f + 20.0f * (float) i;
            if (i == 20) r.ctl->attach (&second);
            if (i == 30) r.ctl->detach (&second);
            if (i == 40) r.frame.setMarker (0, false, 90);
            if (i == 45) r.frame.setMarker (0, true, 90);
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }
        stop.store (true);
        t.join();
        r.ctl->shutdown();
        check (! r.plane.planeOn && ! r.plane.buffersAlive && r.plane.countOf ("setplane") > 5, "threaded run: pictures were shown (" + std::to_string (r.plane.countOf ("setplane")) + ") and everything is off at the end");
    }
    std::printf (g_fails ? "controller_host: FAILED (%d)\n" : "controller_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
