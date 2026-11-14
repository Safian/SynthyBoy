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
    // ---- the slot table and the colour table ----
    {
        bool maps = true, tolerant = true, distinct = true;
        for (int sl = 0; sl < layout::kMaxInstances; ++sl)
        {
            const uint32_t c = layout::kInstanceSlotRgb[sl];
            maps = maps && classifyInstanceRgb (c) == sl && slotOfInstanceFrame (instanceFrameOfSlot (sl)) == sl && instanceFrameRgb (instanceFrameOfSlot (sl)) == c && instanceFrameOfSlot (sl) == sl + 1
                   && static_cast<int> (instanceParamValue (sl) * static_cast<float> (layout::kInstanceSteps)) == sl + 1;
            tolerant = tolerant && classifyInstanceRgb (c + 0x020202) == sl && classifyInstanceRgb (c - 0x020202) == sl && classifyInstanceRgb (c + 0x030000) == kSlotUnknown;
            for (int o = sl + 1; o < layout::kMaxInstances; ++o)
                distinct = distinct && (layout::kInstanceSlotRgb[o] != c);
        }
        check (maps, "the 16 slots: frame slot + 1, the colour table maps back to the slot, the parameter value is frame / 32 (exact)");
        check (tolerant, "a colour within 2 per channel is the slot, 3 away is nothing");
        check (distinct, "no two slots share a colour");
        check (classifyInstanceRgb (layout::kInstanceNoneRgb) == kSlotNone && instanceFrameRgb (layout::kMaxInstances + 1) == layout::kInstanceNoneRgb && instanceFrameOfSlot (-1) == 17 && instanceFrameOfSlot (16) == 17
               && instanceParamValue (-1) == 17.0f / 32.0f, "an instance without a slot shows frame 17: the none colour");
        bool blank = classifyInstanceRgb (layout::kInstanceBlankRgb) == kSlotUnknown && instanceFrameRgb (0) == layout::kInstanceBlankRgb;
        for (int f = 18; f < layout::kInstanceFrames; ++f)
            blank = blank && instanceFrameRgb (f) == layout::kInstanceBlankRgb;
        check (blank && instanceParamValue (-1) != 0.0f, "frame 0 and the frames after 17 are blank: no valid colour");
        bool exact = true;
        for (int f = 0; f < layout::kInstanceFrames; ++f)
            exact = exact && static_cast<int> (static_cast<float> (f) / 32.0f * 32.0f) == f && static_cast<float> (f) / 32.0f * 32.0f == static_cast<float> (f);
        check (exact, "every value frame / 32 times 32 is the frame again in a float (the host picks the frame by a multiplication)");
        bool apart = classifyInstanceRgb (layout::kBackgroundRgb) == kSlotUnknown;
        for (int v = 0; v < layout::kNumViews; ++v)
            for (int m = 0; m < 2; ++m)
                apart = apart && classifyInstanceRgb (layout::kViews[v].markers[m].rgb) == kSlotUnknown && classifyInstanceRgb (layout::kViews[v].markers[m].cover) == kSlotUnknown;
        check (apart, "the background, the marker colours and the covers are not instance colours");
        FakeInstance a[18];
        InstanceSlots table;
        bool inOrder = true;
        for (int i = 0; i < 16; ++i)
            inOrder = inOrder && table.acquire (&a[i]) == i;
        check (inOrder && table.slotted() == 16 && table.aliveMask() == 0xffffu, "16 instances get slots 0 .. 15 in order");
        check (table.acquire (&a[16]) == -1 && table.acquire (&a[17]) == -1 && table.total() == 18 && table.slotted() == 16, "the 17th and 18th get no slot but are counted");
        check (table.release (&a[5]) && table.at (5) == nullptr && table.slotOf (&a[6]) == 6 && table.slotOf (&a[15]) == 15, "a closed instance frees its slot, the others keep theirs");
        check (table.acquire (&a[5]) == 5 && table.slotOf (&a[5]) == 5, "the freed slot is the next one given out (reused only after its instance closed)");
        check (table.release (&a[16]) && table.total() == 17 && ! table.release (&a[16]), "an instance without a slot can leave, once");
    }
    // ---- several instances: the instance whose slot colour is on screen is the one drawn ----
    {
        // what a single instance with these knobs looks like (the picture and the hash), for comparison
        auto reference = [&] (float cutoff, float res, uint32_t& hash, std::vector<uint32_t>& picture)
        {
            Rig q;
            q.inst.set ("filter1.cutoff", cutoff);
            q.inst.set ("filter1.resonance", res);
            q.frame.setAll (true, 90);
            q.until ([&] { return q.on(); }, 500);
            q.run (300);
            hash = q.ctl->currentView()->hash();
            picture = q.plane.shownCopy;
        };
        uint32_t h0 = 0, h1 = 0, h2 = 0;
        std::vector<uint32_t> p0, p1, p2;
        reference (1000.0f, 30.0f, h0, p0);
        reference (4000.0f, 70.0f, h1, p1);
        reference (200.0f, 5.0f, h2, p2);
        check (h0 != h1 && h1 != h2 && h0 != h2 && p0 != p1 && p1 != p2, "(the three parameter sets draw three different pictures)");

        Rig r;   // slot 0: cutoff 1000, resonance 30
        FakeInstance second, third;
        second.set ("filter1.cutoff", 4000.0f);
        second.set ("filter1.resonance", 70.0f);
        third.set ("filter1.cutoff", 200.0f);
        third.set ("filter1.resonance", 5.0f);
        check (r.ctl->attach (&second) == 1 && r.ctl->attach (&third) == 2 && r.ctl->slotOf (&r.inst) == 0, "three instances get the slots 0, 1, 2");
        r.frame.setView (0, true, 90);   // markers and the instance picture of slot 0
        check (r.until ([&] { return r.on(); }, 500) >= 0 && r.ctl->activeSlot() == 0 && r.ctl->state() == "visible", "slot 0 colour: visible, the data of slot 0");
        r.run (300);
        check (r.ctl->currentView()->hash() == h0 && r.plane.shownCopy == p0, "... the picture is the one of instance 0's knobs, pixel for pixel");
        r.frame.setViewInstance (0, 1, true, 90);
        double t = r.until ([&] { return r.ctl->activeSlot() == 1 && r.ctl->currentView()->hash() == h1; }, 1000);
        check (t >= 0 && t <= 200, "the colour changes to slot 1: its data within 200 ms (took " + std::to_string (t) + " ms)");
        r.run (300);
        check (r.plane.shownCopy == p1 && r.on(), "... the picture on the plane is instance 1's, pixel for pixel");
        r.frame.setViewInstance (0, 2, true, 90);
        t = r.until ([&] { return r.ctl->activeSlot() == 2 && r.ctl->currentView()->hash() == h2; }, 1000);
        check (t >= 0 && t <= 200, "slot 2: its data within 200 ms (took " + std::to_string (t) + " ms)");
        r.run (300);
        check (r.plane.shownCopy == p2, "... the picture is instance 2's");
        r.inst.set ("filter1.cutoff", 8000.0f);   // another instance changes: the picture on the plane does not
        const int shown = r.plane.presented;
        r.run (1500);
        check (r.plane.presented == shown && r.plane.shownCopy == p2, "a knob of an instance that is not on screen changes nothing on the plane");
        r.inst.set ("filter1.cutoff", 1000.0f);
        r.frame.setViewInstance (0, 0, true, 90);
        t = r.until ([&] { return r.ctl->activeSlot() == 0 && r.ctl->currentView()->hash() == h0; }, 1000);
        r.run (300);
        check (t >= 0 && t <= 200 && r.plane.shownCopy == p0, "back to slot 0: its data again (no trace of the other two)");
        const std::string st = r.dir.read ("status.txt");
        check (line (st, "instances") == "3" && line (st, "instance_slot") == "0" && line (st, "instance_slots_alive") == "0007" && line (st, "instance_slot_read") == "0" && line (st, "instances_with_slot") == "3", "the status file has the instance count, the slot shown and the slots alive");
        check (line (st, "view") == "filter1" && line (st, "instance_pixel_read") == "1414d8", "... and the colour read (1414d8 = slot 0)");

        // the instance whose page is on screen closes: hidden within one tick, nothing is used afterwards (run under AddressSanitizer: the object is freed)
        auto heap = std::make_unique<FakeInstance>();
        heap->set ("filter1.cutoff", 3000.0f);
        const int hs = r.ctl->attach (heap.get());
        check (hs == 3, "a fourth instance gets slot 3");
        r.frame.setViewInstance (0, 3, true, 90);
        check (r.until ([&] { return r.ctl->activeSlot() == 3; }, 500) >= 0, "its colour: it is the one on screen");
        r.ctl->detach (heap.get());
        const double gone = r.until ([&] { return ! r.on(); }, 500);
        check (gone >= 0 && gone <= 20, "the instance of the page on screen closes: the overlay is hidden within one tick (took " + std::to_string (gone) + " ms)");
        heap.reset();
        r.run (1500);
        check (! r.on() && r.ctl->state().rfind ("hidden: instance", 0) == 0, "... and stays hidden while its colour is still on screen (" + r.ctl->state() + ")");
        // the slot is free again: a new instance gets it, and it is drawn from its own data
        FakeInstance reuse;
        reuse.set ("filter1.cutoff", 4000.0f);
        reuse.set ("filter1.resonance", 70.0f);
        check (r.ctl->attach (&reuse) == 3, "the freed slot 3 is reused by the next instance");
        r.until ([&] { return r.on(); }, 500);
        r.run (300);
        check (r.on() && r.plane.shownCopy == p1 && r.ctl->activeSlot() == 3, "the new instance on slot 3 is drawn from its own data");
        r.ctl->detach (&reuse);
        r.frame.setViewInstance (0, 0, true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0, "(back to slot 0)");

        // a valid colour of a slot nobody holds
        r.frame.setViewInstance (0, 7, true, 90);
        const unsigned long before = r.ctl->rereports();
        const double h = r.until ([&] { return ! r.on(); }, 500);
        check (h >= 0 && h <= 120 && r.ctl->state() == "hidden: instance slot 7 is not alive", "the colour of slot 7, which is not alive: hidden (" + r.ctl->state() + ")");
        r.run (2000);
        check (r.ctl->rereports() == before, "... a valid colour of a dead slot does not ask for new reports");
        r.ctl->detach (&second);
        r.ctl->detach (&third);
    }
    // ---- the instance picture is not readable: hidden, and the instances are asked to report again (rate limited) ----
    {
        Rig r;
        FakeInstance second;
        r.ctl->attach (&second);
        r.frame.setView (0, true, 90);
        r.until ([&] { return r.on(); }, 500);
        check (r.ctl->rereports() == 0 && r.inst.reportRequests == 0, "a valid colour: nobody was asked to report");
        r.frame.setViewInstance (0, kSlotUnknown, true, 90);   // the blank frame: the host has not drawn this instance's value yet
        const double h = r.until ([&] { return ! r.on(); }, 500);
        check (h >= 0 && h <= 120 && r.ctl->state() == "hidden: instance of the page not identified", "the blank frame: hidden (" + r.ctl->state() + ")");
        check (r.ctl->rereports() == 1 && r.inst.reportRequests == 1 && second.reportRequests == 1, "... and both instances were asked to report their slot once");
        r.run (1250);
        check (r.ctl->rereports() >= 2 && r.ctl->rereports() <= 3 && r.inst.reportRequests == static_cast<int> (r.ctl->rereports()), "1.25 s later: 2 or 3 requests in all, not one per check (" + std::to_string (r.ctl->rereports()) + ")");
        r.run (5000);
        check (r.ctl->rereports() <= 5, "6.5 s of a missing colour: at most 5 requests, the gap grows (" + std::to_string (r.ctl->rereports()) + ")");
        {
            const unsigned long inFile = std::stoul (line (r.dir.read ("status.txt"), "instance_rereports"));   // written once a second: at most a second behind
            check (! r.on() && inFile >= 3 && inFile <= r.ctl->rereports(), "... still hidden; the status file counts the requests (" + std::to_string (inFile) + ")");
        }
        const unsigned long n = r.ctl->rereports();
        r.frame.setViewInstance (0, 0, true, 90);   // the host has now drawn the value
        const double s = r.until ([&] { return r.on(); }, 500);
        check (s >= 0 && s <= 120 && r.ctl->activeSlot() == 0, "a valid colour: shown within 120 ms (took " + std::to_string (s) + " ms)");
        r.run (3000);
        check (r.ctl->rereports() == n, "... and no more requests");
        // no instance picture at all, or a colour that is none of the table: the same
        r.frame.setViewInstanceRgb (0, 0, false, 90);
        check (r.until ([&] { return ! r.on(); }, 500) >= 0 && r.ctl->rereports() == n + 1, "no instance picture on the screen (a skin without it): hidden, a request is made at once (the gap starts again)");
        r.frame.setViewInstanceRgb (0, 0x9a3b00, true, 90);
        r.run (1500);
        check (! r.on() && r.ctl->rereports() >= n + 2, "a colour that is not in the table: hidden, requests go on");
        // a page without markers (another tab) asks for nothing
        r.frame.setView (0, false, 90);
        r.run (200);
        const unsigned long m = r.ctl->rereports();
        r.run (5000);
        check (r.ctl->rereports() == m && r.ctl->state() == "hidden: no view on screen", "no markers on screen: nobody is asked");
        // a half read colour (one corner differs) is not a colour
        r.frame.setView (0, true, 90);
        r.until ([&] { return r.on(); }, 500);
        const layout::Rect& ir = layout::kViews[0].instance;
        int px, py;
        uiPointToPanel (90, ir.x + 1, ir.y + 1, 800, 1280, px, py);
        r.frame.setPixel (px, py, 0x3e14d8);
        check (r.until ([&] { return ! r.on(); }, 500) >= 0, "one sample of five in another slot's colour: not identified, hidden");
        r.ctl->detach (&second);
    }
    // ---- more than 16 instances ----
    {
        Rig r;
        std::vector<std::unique_ptr<FakeInstance>> many;
        for (int i = 1; i < 17; ++i)
        {
            many.push_back (std::make_unique<FakeInstance>());
            many.back()->set ("filter1.cutoff", 100.0f + 400.0f * static_cast<float> (i));
        }
        bool slots = true;
        for (int i = 1; i < 16; ++i)
            slots = slots && r.ctl->attach (many[static_cast<size_t> (i) - 1].get()) == i;
        check (slots, "instances 2 .. 16 get slots 1 .. 15");
        check (r.ctl->attach (many[15].get()) == -1, "the 17th instance gets no slot");
        r.frame.setViewInstance (0, 15, true, 90);
        r.frame.setViewMarker (0, 0, true, 90);
        r.frame.setViewMarker (0, 1, true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0 && r.ctl->activeSlot() == 15, "the 16th instance (slot 15) is on screen");
        r.frame.setViewInstance (0, kSlotNone, true, 90);   // what the 17th instance reports
        const double h = r.until ([&] { return ! r.on(); }, 500);
        check (h >= 0 && r.ctl->state() == "hidden: instance has no slot (more than 16 instances)", "the 17th instance's page: hidden (" + r.ctl->state() + ")");
        const unsigned long n = r.ctl->rereports();
        r.run (2000);
        check (r.ctl->rereports() == n, "... without asking for reports (the none colour is a valid answer)");
        check (line (r.dir.read ("status.txt"), "instances") == "17" && line (r.dir.read ("status.txt"), "instances_with_slot") == "16", "the status file counts 17 instances, 16 with a slot");
        r.ctl->detach (many[15].get());
        for (int i = 1; i < 16; ++i)
            r.ctl->detach (many[static_cast<size_t> (i) - 1].get());
        check (r.ctl->slotOf (&r.inst) == 0, "(the first instance kept slot 0 all along)");
    }
    // ---- one instance: its colour is slot 0 ----
    {
        Rig r;
        r.frame.setView (0, true, 90);
        check (r.until ([&] { return r.on(); }, 300) >= 0 && r.ctl->state() == "visible", "one instance: shown as before, with slot 0's colour");
        r.frame.setViewInstance (0, 1, true, 90);
        check (r.until ([&] { return ! r.on(); }, 300) >= 0, "one instance and the colour of slot 1: hidden (that page is not ours)");
        r.frame.setViewInstance (0, 0, true, 90);
        check (r.until ([&] { return r.on(); }, 300) >= 0, "slot 0 again: shown");
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
        // other threads open and close instances (heap objects, freed right after detach) and flip the slot colour on screen, while the overlay thread ticks
        std::atomic<bool> churnStop { false };
        std::atomic<int> opened { 0 };
        std::thread churn ([&]
        {
            while (! churnStop.load())
            {
                auto x = std::make_unique<FakeInstance>();
                x->set ("filter1.cutoff", 300.0f + 50.0f * static_cast<float> (opened.load() % 20));
                const int sl = r.ctl->attach (x.get());
                r.frame.setViewInstance (0, sl, true, 90);
                std::this_thread::sleep_for (std::chrono::milliseconds (7));
                r.ctl->detach (x.get());
                x.reset();
                ++opened;
            }
        });
        std::thread flip ([&]
        {
            int k = 0;
            while (! churnStop.load())
            {
                r.frame.setViewInstance (0, k++ % 4, true, 90);
                std::this_thread::sleep_for (std::chrono::milliseconds (3));
            }
        });
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
        churnStop.store (true);
        churn.join();
        flip.join();
        stop.store (true);
        t.join();
        r.ctl->shutdown();
        check (! r.plane.planeOn && ! r.plane.buffersAlive && r.plane.countOf ("setplane") > 5, "threaded run: pictures were shown (" + std::to_string (r.plane.countOf ("setplane")) + ") and everything is off at the end");
        check (opened.load() > 20, "threaded run: " + std::to_string (opened.load()) + " instances were opened and closed on another thread while the overlay thread ticked");
    }
    std::printf (g_fails ? "controller_host: FAILED (%d)\n" : "controller_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
