// SynthyBoy - MPC overlay tests: run-time discovery of the display and the cores (discovery.h) against the plane tables of BOTH devices (MPC Key 61, MPC XL; taken from the surveys
// of 2026-11-14), the rotation search from the markers (page_detector.h, controller.h), the pace grid (vblank_pacer.h) with a fake vblank clock, and the controller's handling of a lost
// plane. Everything runs on the Mac with fakes.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>

#include "../discovery.h"
#include "../vblank_pacer.h"
#include "fakes.h"
#include "fixtures/legacy_layout.h"

using namespace ovt;

namespace {

PlaneInfo plane (uint32_t id, int type, uint32_t crtc, uint32_t fb, std::vector<uint32_t> formats, int zpos = -1, uint32_t possible = 1)
{
    PlaneInfo p;
    p.id = id;
    p.type = type;
    p.crtcId = crtc;
    p.fbId = fb;
    p.formats = std::move (formats);
    p.possibleCrtcs = possible;
    if (zpos >= 0)
    {
        p.hasZpos = true;
        p.zpos = static_cast<uint64_t> (zpos);
    }
    return p;
}

constexpr uint32_t XR24 = 0x34325258u, AR24 = kFourccArgb8888, RG24 = 0x34324752u, NV12 = 0x3231564eu;

/** MPC XL (survey 2026-11-14): CRTC 82; the application: primary 34 (XR24 800x1280, zpos 0) and the sprite 40 (AR24 64x64, zpos 1); free: 46, 52 (Cluster, zpos 2, 3), 58 .. 76 (Esmart, zpos 4 .. 7). */
DrmSnapshot xlSnapshot()
{
    DrmSnapshot s;
    CrtcInfo c;
    c.id = 82; c.index = 0; c.modeValid = true; c.width = 800; c.height = 1280; c.vrefresh = 60; c.fbId = 300;
    s.crtcs.push_back (c);
    s.planes.push_back (plane (34, kPlaneTypePrimary, 82, 300, { XR24, AR24, RG24 }, 0));
    s.planes.push_back (plane (40, kPlaneTypeOverlay, 82, 301, { AR24 }, 1));
    s.planes.push_back (plane (46, kPlaneTypeOverlay, 0, 0, { XR24, AR24, RG24 }, 2));
    s.planes.push_back (plane (52, kPlaneTypeOverlay, 0, 0, { XR24, AR24, RG24 }, 3));
    for (uint32_t id : { 58u, 64u, 70u, 76u })
        s.planes.push_back (plane (id, kPlaneTypeOverlay, 0, 0, { XR24, AR24, RG24, NV12 }, static_cast<int> (4 + (id - 58) / 6)));
    return s;
}

/** MPC Key 61 (survey): CRTC 37; the application: primary 33 (the UI) and the cursor plane 35; free overlay planes 38 and 40 (no zpos property on this kernel). */
DrmSnapshot key61Snapshot()
{
    DrmSnapshot s;
    CrtcInfo c;
    c.id = 37; c.index = 0; c.modeValid = true; c.width = 800; c.height = 1280; c.vrefresh = 60; c.fbId = 100;
    s.crtcs.push_back (c);
    s.planes.push_back (plane (33, kPlaneTypePrimary, 37, 100, { XR24, AR24 }));
    s.planes.push_back (plane (35, kPlaneTypeCursor, 0, 0, { AR24 }));
    s.planes.push_back (plane (38, kPlaneTypeOverlay, 0, 0, { XR24, AR24 }));
    s.planes.push_back (plane (40, kPlaneTypeOverlay, 0, 0, { XR24, AR24 }));
    return s;
}

/** The fake vblank clock: a counter at a period, a clock that moves only by waiting; every wait costs what it says. */
class FakeVblank final : public IVblankClock
{
public:
    double periodMs = 17.711;
    double t = 1000.0;
    bool broken = false;
    int waits = 0, sleeps = 0;
    uint32_t seqAt (double ms) const { return static_cast<uint32_t> (std::floor ((ms - 1000.0) / periodMs)) + 500u; }
    double msOf (uint32_t seq) const { return 1000.0 + static_cast<double> (seq - 500u) * periodMs; }
    bool last (VblankStamp& s) override { if (broken) return false; s.seq = seqAt (t); s.ms = msOf (s.seq); return true; }
    bool waitUntil (uint32_t seq, VblankStamp& s) override
    {
        if (broken) return false;
        ++waits;
        if (static_cast<int32_t> (seq - seqAt (t)) > 0)
            t = msOf (seq) + 0.05;
        s.seq = seqAt (t);
        s.ms = msOf (s.seq);
        return true;
    }
    bool waitNext (VblankStamp& s) override { if (broken) return false; return waitUntil (seqAt (t) + 1, s); }
    double nowMs() override { return t; }
    void sleepUntilMs (double ms) override { ++sleeps; if (ms > t) t = ms; }
};

} // namespace

int main()
{
    // ================= discovery: both devices' tables =================
    {
        const std::string model = "Akai Professional MPC XL";
        DrmSnapshot dormant = xlSnapshot();
        dormant.planes[1].crtcId = dormant.planes[1].fbId = 0;
        DisplayInfo eligible = discoverDisplay (dormant, model);
        check (eligible.candidates == std::vector<uint32_t> ({ 46, 52, 58, 64, 70, 76 }) &&
               advancePlane (eligible) && eligible.plane() == 52, "XL: exclude the dormant host sprite during discovery and migration");
        dormant.planes[2].fbId = 301;
        check (discoverDisplay (dormant, model).plane() == 52, "XL: skip occupied 46 and dormant sprite 40");
        check (discoverDisplay (dormant, "other").plane() == 40, "unknown device keeps ordinary discovery");
        dormant.crtcs[0].id = dormant.planes[0].crtcId = 88;
        check (discoverDisplay (dormant, model).plane() == 40, "unknown XL topology keeps ordinary discovery");
        const DisplayInfo key = discoverDisplay (key61Snapshot(), "Akai Professional MPC Key 61");
        check (key.candidates == std::vector<uint32_t> ({ 38, 40 }), "Key 61 keeps plane 40 among its spares");
        const DisplayInfo d = discoverDisplay (xlSnapshot());
        check (d.ok && d.crtc == 82 && d.crtcIndex == 0 && d.primaryPlane == 34 && d.how == "primary plane", "XL: CRTC 82 and the primary plane 34 are found from the primary plane");
        check (d.panelW == 800 && d.panelH == 1280, "XL: the panel is 800 x 1280 (from the mode)");
        check (d.appPlanes == std::vector<uint32_t> ({ 34, 40 }), "XL: the application's planes on the CRTC are 34 and 40");
        check (d.candidates == std::vector<uint32_t> ({ 46, 52, 58, 64, 70, 76 }) && d.plane() == 46, "XL: the free overlay planes 46 52 58 64 70 76, the lowest numbered (46) is the one");
    }
    {
        const DisplayInfo d = discoverDisplay (key61Snapshot());
        check (d.ok && d.crtc == 37 && d.primaryPlane == 33, "Key 61: CRTC 37 and the primary plane 33");
        check (d.candidates == std::vector<uint32_t> ({ 38, 40 }) && d.plane() == 38, "Key 61: the free overlay planes are 38 and 40, the cursor plane 35 is not one of them; 38 is the one");
    }
    {
        DrmSnapshot s = xlSnapshot();
        s.planes[2].crtcId = 82; s.planes[2].fbId = 999;      // another program has plane 46
        const DisplayInfo d = discoverDisplay (s);
        check (d.ok && d.plane() == 52 && d.candidates.front() == 52, "XL: plane 46 is busy (taken by another program): the next free one, 52, is chosen");
        for (PlaneInfo& p : s.planes)
            if (p.id != 34 && p.id != 40) { p.crtcId = 82; p.fbId = 5; }
        const DisplayInfo none = discoverDisplay (s);
        check (! none.ok && none.error.find ("no free overlay plane") != std::string::npos, "XL: every overlay plane busy: no plane, with a reason");
    }
    {
        DrmSnapshot s = xlSnapshot();
        for (PlaneInfo& p : s.planes)
            if (p.id == 46) p.formats = { XR24, RG24 };                   // no ARGB8888
        for (PlaneInfo& p : s.planes)
            if (p.id == 52) p.possibleCrtcs = 2;                          // cannot go on this CRTC
        const DisplayInfo d = discoverDisplay (s);
        check (d.ok && d.plane() == 58 && std::find (d.candidates.begin(), d.candidates.end(), 46) == d.candidates.end() && std::find (d.candidates.begin(), d.candidates.end(), 52) == d.candidates.end(),
              "XL: a plane without ARGB8888 and a plane that cannot go on the CRTC are not candidates");
    }
    {
        DrmSnapshot s = xlSnapshot();
        s.planes[2].zpos = 0;                                              // plane 46 would stack BELOW the application's planes (34: 0, 40: 1)
        const DisplayInfo d = discoverDisplay (s);
        check (d.ok && std::find (d.candidates.begin(), d.candidates.end(), 46) == d.candidates.end() && d.plane() == 52, "a plane that would stack under a plane of the application (zpos) is not a candidate");
    }
    {
        DrmSnapshot s = xlSnapshot();
        for (PlaneInfo& p : s.planes)
            p.type = kPlaneTypeUnknown;                                    // a driver that tells no plane types: the CRTC's own framebuffer finds the primary plane
        const DisplayInfo d = discoverDisplay (s);
        check (d.ok && d.how == "crtc framebuffer" && d.primaryPlane == 34 && d.crtc == 82, "no plane types: the CRTC with a mode and a framebuffer, and the plane showing it");
        DrmSnapshot dark = xlSnapshot();
        dark.crtcs[0].fbId = 0;
        for (PlaneInfo& p : dark.planes)
            if (p.id == 34) p.fbId = 0;
        check (! discoverDisplay (dark).ok, "nothing on the display: not found");
    }
    {
        DrmSnapshot two = xlSnapshot();                                    // a second CRTC (index 1) that the primary plane is not on: the planes that can go there are not used
        CrtcInfo c2; c2.id = 90; c2.index = 1; c2.modeValid = false;
        two.crtcs.push_back (c2);
        for (PlaneInfo& p : two.planes)
            if (p.id == 46) p.possibleCrtcs = 2;                          // only on the second CRTC
        const DisplayInfo d = discoverDisplay (two);
        check (d.ok && d.crtc == 82 && std::find (d.candidates.begin(), d.candidates.end(), 46) == d.candidates.end(), "with two CRTCs only the planes of the application's CRTC are candidates");
    }
    {
        DisplayInfo d = discoverDisplay (xlSnapshot());
        int n = 0;
        while (advancePlane (d))
            ++n;
        check (n == 5 && d.plane() == 76 && ! advancePlane (d), "advancePlane walks the spares in order and stops at the last one");
    }

    // ================= the cores =================
    {
        const std::vector<int> on = parseCpuList ("0-7\n");
        check (on.size() == 8 && on.front() == 0 && on.back() == 7, "parseCpuList: 0-7");
        check (parseCpuList ("2-3") == std::vector<int> ({ 2, 3 }) && parseCpuList ("0,2,4-5\n") == std::vector<int> ({ 0, 2, 4, 5 }) && parseCpuList ("").empty(), "parseCpuList: single, ranges, empty");
        check (formatCpuList ({ 0, 1, 2, 3, 6, 8, 9 }) == "0-3,6,8-9" && formatCpuList ({}) == "none", "formatCpuList");
        // MPC XL: no isolated cores, the audio threads pinned to 4-7 (AudioWorker0-3 one core each, Audio Processing on 7), FileIO + MIDI on 3
        std::vector<ThreadInfo> xl = { { "MPC 3 Main", on }, { "AudioWorker0", { 4 } }, { "AudioWorker1", { 5 } }, { "AudioWorker2", { 6 } }, { "AudioWorker3", { 7 } }, { "Audio Processin", { 7 } },
                                       { "FileIO", { 3 } }, { "MIDI", { 3 } }, { "Background 1", { 1 } } };
        const CpuPlan p = planCpus (on, {}, xl);
        check (p.allowed == std::vector<int> ({ 0, 1, 2, 3 }) && p.avoided == std::vector<int> ({ 4, 5, 6, 7 }) && p.changed && p.audioThreads.size() == 5, "XL: the overlay stays on cores 0-3, off 4-7 where the audio threads are pinned");
        // Key 61: cores 2-3 isolated by the kernel for the audio threads
        const std::vector<int> k4 = parseCpuList ("0-3");
        const CpuPlan k = planCpus (k4, parseCpuList ("2-3"), { { "MPC", k4 }, { "AudioWorker0", { 2 } }, { "AudioWorker1", { 3 } } });
        check (k.allowed == std::vector<int> ({ 0, 1 }) && k.avoided == std::vector<int> ({ 2, 3 }), "Key 61: the isolated cores 2-3 are left to the audio threads");
        // the Key 61 as it really is (read from the device): AudioWorker0 .. 3 pinned to the cores 0 .. 3, the watchdog on 0, the kernel isolates 2-3: the audio threads sit on every core, so only the
        // isolated cores are kept off (the overlay shares 0-1 with them)
        const CpuPlan k61 = planCpus (k4, parseCpuList ("2-3"), { { "MPC 3 Main", { 0 } }, { "AudioWorker0", { 0 } }, { "AudioWorker1", { 1 } }, { "AudioWorker2", { 2 } }, { "AudioWorker3", { 3 } }, { "AudioWatchdog", { 0 } }, { "Audio Processin", { 3 } }, { "FileIO", { 1 } } });
        check (k61.allowed == std::vector<int> ({ 0, 1 }) && k61.avoided == std::vector<int> ({ 2, 3 }) && k61.changed, "Key 61 as measured: the audio threads on every core: only the isolated cores 2-3 are kept off, the overlay runs on 0-1");
        // the kernel isolates nothing and the audio threads are not pinned: nothing is changed
        const CpuPlan free = planCpus (k4, {}, { { "AudioWorker0", k4 }, { "Audio Processing", k4 } });
        check (free.allowed == k4 && ! free.changed && free.avoided.empty(), "no isolation and unpinned audio threads: every core stays allowed, nothing is set");
        // everything isolated / pinned: never an empty set
        const CpuPlan all = planCpus (k4, parseCpuList ("0-3"), {});
        check (all.allowed == k4 && ! all.changed, "nothing would be left: all the cores stay allowed");
        // names are matched without regard to the case, other threads never count
        const CpuPlan cs = planCpus (on, {}, { { "AUDIOWORKER9", { 6 } }, { "audio thread", { 7 } }, { "FileIO", { 0, 1, 2, 3 } } });
        check (cs.allowed == std::vector<int> ({ 0, 1, 2, 3, 4, 5 }), "thread names match 'audio' in any case; a pinned FileIO thread does not take cores away");
        // the rule for every thread of the shell that renders or polls (the display thread too): the plan's cores as a mask, all online cores when the plan changes nothing
        check (cpuMask (coresForOwnThreads (p, on)) == 0x0full, "XL: the cores of the shell's threads are the mask 0x0f (0-3), not 0xff");
        check (cpuMask (coresForOwnThreads (k61, k4)) == 0x03ull, "Key 61: the mask is 0x03 (0-1)");
        check (cpuMask (coresForOwnThreads (free, k4)) == 0x0full && cpuMask (coresForOwnThreads (all, k4)) == 0x0full, "a plan that changes nothing: every online core (a thread held back by an earlier plan is released)");
        check (cpuMask ({ 0, 7, 63, 64, -1 }) == ((1ull << 0) | (1ull << 7) | (1ull << 63)) && cpuMask ({}) == 0ull, "cpuMask: cores outside 0..63 are left out, the empty set is 0");
    }

    // ================= the pace grid =================
    {
        check (paceStride (17.711) == 2 && paceStride (16.667) == 2 && paceStride (33.3) == 1 && paceStride (8.33) == 4 && paceStride (0.0) == 2, "paceStride: 56.46 Hz -> 2, 60 Hz -> 2, 30 Hz -> 1, 120 Hz -> 4");
        FakeVblank vb;
        double period = 0.0;
        check (measureVblankPeriod (vb, 40, period) && std::fabs (period - 17.711) < 0.01, "measureVblankPeriod finds the period from the time stamps of 40 vblanks");
        FakeVblank bad;
        bad.broken = true;
        check (! measureVblankPeriod (bad, 40, period), "... and says so when the vblank cannot be waited for");
    }
    {
        FakeVblank vb;
        FramePacer pacer (vb, 17.711, 2, true);
        // a presenter that draws for 3 ms, then waits for its slot and updates the plane: the slots are exactly two vblanks apart
        std::vector<uint32_t> slots;
        for (int i = 0; i < 40; ++i)
        {
            vb.t += 3.0;
            pacer.waitSlot();
            VblankStamp s;
            vb.last (s);
            slots.push_back (s.seq);
            vb.t = vb.msOf (s.seq) + 0.1;                                 // the update call itself (it returns one vblank later on the device)
            vb.t += vb.periodMs;
            pacer.done();
        }
        bool even = true;
        for (size_t i = 2; i < slots.size(); ++i)
            even = even && slots[i] - slots[i - 1] == 2;
        check (even, "the pacer issues the updates on every second vblank, exactly (40 pictures)");
        check (pacer.stats().vb[2] >= 37 && pacer.stats().vb[1] == 0 && pacer.stats().vb[3] == 0, "... and the statistics say so: all intervals are two vblanks");
        // a picture that is late (not ready for its slot) takes the next slot of the same grid
        vb.t += 20.0;
        pacer.waitSlot();
        VblankStamp s;
        vb.last (s);
        check ((s.seq - slots.back()) % 2 == 0, "a late picture takes a later slot of the SAME grid (an even number of vblanks after the last one)");
        // after a pause a new grid starts at the very next vblank
        vb.t += 2000.0;
        const uint32_t before = vb.seqAt (vb.t);
        pacer.waitSlot();
        vb.last (s);
        check (s.seq == before + 1, "after a pause the next vblank starts a new grid");
        check (std::fabs (pacer.msUntilSlot() - 2.0 * 17.711) < 1.5, "msUntilSlot: right after a slot the next one is two vblanks away");
    }
    {
        // g134: the first picture after a confirmation (guard plane-off) is presented at once; the grid resumes after it
        FakeVblank vb;
        FramePacer pacer (vb, 17.711, 2, true);
        for (int i = 0; i < 6; ++i)                       // a run on the grid, then a pause shorter than 4 strides (the grid is still remembered)
        {
            vb.t += 3.0;
            pacer.waitSlot();
            VblankStamp s;
            vb.last (s);
            vb.t = vb.msOf (s.seq) + vb.periodMs + 0.1;
            pacer.done();
        }
        vb.t += 5.0;                                      // the confirmation arrives mid-stride
        const double tConfirm = vb.t;
        const double untilGrid = pacer.msUntilSlot();
        check (untilGrid > 5.0, "g134: without a confirmation the next picture waits for the grid (msUntilSlot > 5 ms mid-stride)");
        pacer.presentNextImmediately();
        check (pacer.msUntilSlot() == 0.0 && pacer.immediatePending(), "g134: after a confirmation msUntilSlot is 0");
        pacer.waitSlot();
        check (vb.t == tConfirm && vb.waits == 6 && ! pacer.immediatePending(), "g134: the first picture after the confirmation does not wait (clock unmoved, no vblank wait)");
        VblankStamp s;
        vb.last (s);
        const uint32_t firstLands = s.seq + 1u;           // the update lands on the next vblank
        vb.t += 3.0;
        check (pacer.msUntilSlot() > 0.5 * 17.711, "g134: the NEXT picture waits for the grid again");
        pacer.waitSlot();
        vb.last (s);
        check (s.seq == firstLands + 2u, "g134: ... on the stride after the first picture's vblank (the grid restarts from the immediate picture)");
        // once only: a second waitSlot without a new confirmation waits again
        pacer.waitSlot();
        VblankStamp s2;
        vb.last (s2);
        check (s2.seq == s.seq + 2u, "g134: the pace of all other pictures is unchanged (two vblanks apart)");
        // no confirmation: no change at all
        FakeVblank vb2;
        FramePacer plain (vb2, 17.711, 2, true);
        plain.waitSlot();
        const double a = vb2.t;
        plain.waitSlot();
        check (vb2.t - a > 17.0 && ! plain.immediatePending(), "g134: with no confirmation the pacer waits as before");
        // the timer pacer: the immediate picture does not sleep either
        FakeVblank nb;
        FramePacer timer (nb, 16.667, 2, false);
        timer.waitSlot();
        nb.t += 5.0;
        timer.presentNextImmediately();
        const double t0 = nb.t;
        timer.waitSlot();
        check (nb.t == t0 && nb.sleeps == 0, "g134: the timer pacer presents the first picture after a confirmation at once");
        nb.t += 3.0;
        timer.waitSlot();
        check (nb.t - t0 > 28.0, "g134: ... and the next one is a timer period after it");
    }
    {
        FakeVblank vb;
        FramePacer pacer (vb, 17.711, 2, true);
        pacer.waitSlot();
        vb.broken = true;
        pacer.waitSlot();
        check (! pacer.onGrid(), "a vblank source that fails switches the pacer to the timer");
        const double t0 = vb.t;
        pacer.waitSlot();
        pacer.waitSlot();
        check (vb.t - t0 >= 33.0 && vb.t - t0 < 40.0 && vb.sleeps >= 1, "the timer paces at 33.3 ms");
        FakeVblank noGrid;
        FramePacer timer (noGrid, 16.667, 2, false);
        timer.waitSlot();
        const double a = noGrid.t;
        for (int i = 0; i < 6; ++i)
            timer.waitSlot();
        check (std::fabs ((noGrid.t - a) - 6 * 33.3) < 1.0, "a pacer made without a vblank source runs on the timer from the start (6 pictures = 200 ms)");
    }

    // ================= the rotation: found from the markers, no table =================
    for (int rot : { 90, 270 })
    {
        TempDir dir;
        FakePlane fp;
        FakeFrame frame; frame.views = legacy::kViews;
        CpuCurveRenderer renderer;
        FakeInstance inst;
        SynthyBoyController ctl (fp, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &legacy::pageSet());
        ctl.attach (&inst);
        double now = 0.0;
        auto run = [&] (double ms) { const double end = now + ms; while (now < end) { now += 10.0; ctl.tick (now); } };
        run (500);
        check (! ctl.rotationKnown() && ! fp.planeOn, "rotation " + std::to_string (rot) + ": nothing on screen: the rotation is not known yet and nothing is shown");
        frame.setAll (true, rot);
        run (400);
        check (ctl.rotationKnown() && ctl.rotation() == rot && fp.planeOn && fp.lastRect == uiRectToPanel (rot, Rect { legacy::kViews[0].region.x, legacy::kViews[0].region.y, legacy::kViews[0].region.w, legacy::kViews[0].region.h }, 800, 1280),
              "rotation " + std::to_string (rot) + ": found from the markers of the view on screen, and the plane is where that rotation says");
        run (1100);
        const std::string st = dir.read ("status.txt");
        check (st.find ("rotation: " + std::to_string (rot) + "\n") != std::string::npos && st.find ("rotation_source: found from the markers") != std::string::npos, "rotation " + std::to_string (rot) + ": the status file says what was found");
        frame.setAll (false, rot);
        run (300);
        frame.setAll (true, rot);
        run (300);
        check (ctl.rotation() == rot && fp.planeOn, "rotation " + std::to_string (rot) + ": cached for the process (the view goes and comes back)");
        dir.write ("rot", rot == 90 ? "270\n" : "90\n");
        run (1300);
        check (ctl.rotation() == (rot == 90 ? 270 : 90) && ! fp.planeOn, "rotation " + std::to_string (rot) + ": the override file wins over the found value (markers are not there: hidden)");
        dir.remove ("rot");
        run (1300);
        check (ctl.rotation() == rot && fp.planeOn, "rotation " + std::to_string (rot) + ": without the file the found rotation is back");
    }
    for (int rot : { 0, 180 })
    {
        TempDir dir;
        FakePlane fp;
        FakeFrame frame; frame.views = legacy::kViews;
        frame.panelW = 1280;     // a landscape panel: the UI is not turned
        frame.panelH = 800;
        fp.panelW = 1280;
        fp.panelH = 800;
        CpuCurveRenderer renderer;
        FakeInstance inst;
        SynthyBoyController ctl (fp, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &legacy::pageSet());
        ctl.attach (&inst);
        frame.setAll (true, rot);
        double now = 0.0;
        for (int i = 0; i < 50; ++i) { now += 10.0; ctl.tick (now); }
        const Rect ui { legacy::kViews[0].region.x, legacy::kViews[0].region.y, legacy::kViews[0].region.w, legacy::kViews[0].region.h };
        check (ctl.rotationKnown() && ctl.rotation() == rot && fp.planeOn && fp.lastRect == uiRectToPanel (rot, ui, 1280, 800), "rotation " + std::to_string (rot) + " (a landscape panel): found, the plane rectangle is the UI rectangle turned accordingly");
    }
    {
        // markers that match under two rotations at once (both mappings drawn): nothing is decided, nothing is shown, and the reason says why
        TempDir dir;
        FakePlane fp;
        FakeFrame frame; frame.views = legacy::kViews;
        CpuCurveRenderer renderer;
        FakeInstance inst;
        SynthyBoyController ctl (fp, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &legacy::pageSet());
        ctl.attach (&inst);
        frame.setAll (true, 90);
        frame.setAll (true, 270);
        double now = 0.0;
        for (int i = 0; i < 100; ++i) { now += 10.0; ctl.tick (now); }
        check (! ctl.rotationKnown() && ! fp.planeOn && ctl.state().find ("rotation ambiguous") != std::string::npos, "markers under two rotations: ambiguous, nothing is decided and nothing is shown");
        frame.setAll (false, 270);
        for (int i = 0; i < 100; ++i) { now += 10.0; ctl.tick (now); }
        check (ctl.rotationKnown() && ctl.rotation() == 90 && fp.planeOn, "... and as soon as only one rotation matches it is taken");
    }

    // ================= the display is not found: the module disables itself at once with the reason =================
    {
        TempDir dir;
        FakePlane fp;
        FakeFrame frame; frame.views = legacy::kViews;
        frame.readable = false;
        frame.fatal = "display discovery failed: no free overlay plane on CRTC 82";
        CpuCurveRenderer renderer;
        FakeInstance inst;
        SynthyBoyController ctl (fp, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &legacy::pageSet());
        ctl.attach (&inst);
        double now = 0.0;
        for (int i = 0; i < 10; ++i) { now += 10.0; ctl.tick (now); }
        check (ctl.disabled() && ctl.state().find ("no free overlay plane") != std::string::npos, "a framebuffer source that can never work disables the module at once, with its reason in the status");
    }

    // ================= a plane another program takes =================
    {
        TempDir dir;
        FakePlane fp;
        fp.spares = { 52, 58 };
        FakeFrame frame; frame.views = legacy::kViews;
        CpuCurveRenderer renderer;
        FakeInstance inst;
        SynthyBoyController ctl (fp, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &legacy::pageSet());
        ctl.attach (&inst);
        frame.setAll (true, 90);
        double now = 0.0;
        auto run = [&] (double ms) { const double end = now + ms; while (now < end) { now += 10.0; ctl.tick (now); } };
        run (1500);
        check (fp.planeOn && fp.plane == 38 && dir.read ("status.txt").find ("display_plane_state: verified") != std::string::npos, "the plane is verified through its reported state once a second");
        {
            // switched off by a mode set of the application (no CRTC, no framebuffer): not taken: the picture is set again on the same plane
            const int presentedBefore = fp.presented;
            fp.switchedOff = true;
            run (1300);
            check (fp.plane == 38 && fp.nextPlaneCalls == 0 && fp.presented > presentedBefore && dir.read ("status.txt").find ("display_plane_refreshes: 1") != std::string::npos, "a plane that was switched off (not taken) is set again on the SAME plane, no spare is used");
        }
        const int disablesBefore = fp.countOf ("disable");
        fp.stillOurs = false;                                             // another program took plane 38
        run (1200);
        check (fp.plane == 52 && fp.nextPlaneCalls == 1, "the plane was taken: the next free plane (52) is used");
        check (fp.countOf ("disable") == disablesBefore, "... and the plane that is not ours any more is NOT switched off by us");
        check (fp.planeOn && fp.stillOurs, "... the picture is back on the new plane");
        check (dir.read ("status.txt").find ("display_plane_losses: 1") != std::string::npos && dir.read ("status.txt").find ("display_overlay_plane: 52") != std::string::npos, "... the status file says so");
        fp.stillOurs = false;
        run (1200);
        check (fp.plane == 58 && fp.planeOn, "taken again: the third plane");
        fp.stillOurs = false;
        run (1200);
        check (ctl.disabled() && ctl.state().find ("taken by another program") != std::string::npos && fp.countOf ("disable") == disablesBefore, "no spare plane left: the module disables itself with the reason, and still leaves the plane alone");
    }
    {
        // the pace follows the display: the gap of the pictures is the grid's (2 vblanks of the measured period), not a constant. The step returns when the next one is due: the gap less
        // the time the drawing took and a slack; the two displays are drawn the same way, so the difference of the two answers is the difference of the grids (35.42 - 33.33 ms)
        auto due = [] (double period, bool follow)
        {
            TempDir dir;
            FakePlane fp;
            fp.pace.periodMs = period;
            fp.pace.stride = 2;
            fp.pace.vblank = true;
            FakeFrame frame; frame.views = legacy::kViews;
            CpuCurveRenderer renderer;
            FakeInstance inst;
            Timing t;
            t.followDisplayPace = follow;
            SynthyBoyController ctl (fp, frame, renderer, dir.paths(), t, 10.0, nullptr, nullptr, &legacy::pageSet());
            ctl.attach (&inst);
            frame.setAll (true, 90);
            double now = 0.0, wait = 0.0;
            for (int i = 0; i < 80; ++i)
            {
                now += 10.0;
                inst.set ("filter1.cutoff", 500.0f + static_cast<float> (i * 97 % 3000));
                wait = ctl.tick (now);
            }
            return wait;
        };
        const double xl = due (17.711, true), k61 = due (16.667, true), fixed = due (17.711, false), fixed2 = due (16.667, false);
        check (std::fabs ((xl - k61) - 2.0 * (17.711 - 16.667)) < 0.8, "the next picture is due after the grid's gap: 2.09 ms later on the 56.46 Hz display than on a 60 Hz one (measured here: " + std::to_string (xl - k61) + ")");
        check (std::fabs (fixed - fixed2) < 0.8, "... and with the display pace switched off both are the same constant");
    }

    // ---- the display sleeps (DPMS: the CRTC has no mode, no framebuffer): nothing is sent to the plane, nothing is disabled for good, the picture is back when the display is ----
    {
        TempDir dir;
        FakePlane fp;
        FakeFrame frame; frame.views = legacy::kViews;
        CpuCurveRenderer renderer;
        FakeInstance inst;
        SynthyBoyController ctl (fp, frame, renderer, dir.paths(), Timing(), 10.0, nullptr, nullptr, &legacy::pageSet());
        ctl.attach (&inst);
        frame.setAll (true, 90);
        double now = 0.0;
        auto run = [&] (double ms) { const double end = now + ms; while (now < end) { now += 10.0; ctl.tick (now); } };
        run (600);
        check (fp.planeOn && ctl.state() == "visible", "(display sleep: shown)");
        // the display goes off while the picture is on and the markers are still readable (the last frame): no plane update is made
        fp.displayIsOn = false;
        inst.set ("filter1.cutoff", 3000.0f);
        const int sets = fp.countOf ("setplane");
        run (600);
        check (fp.countOf ("setplane") == sets, "while the display is off no plane update is made (it would wait for a vblank that does not come)");
        check (ctl.state().compare (0, 8, "disabled") != 0, "... and the module is not disabled by it (" + ctl.state() + ")");
        // the framebuffer is not readable while the display sleeps: far longer than the limit of unreadable checks
        frame.readable = false;
        run (8000);
        check (ctl.state().compare (0, 8, "disabled") != 0, "an unreadable framebuffer while the display is off does not disable the module (" + ctl.state() + ")");
        check (fp.countOf ("setplane") == sets, "... still no plane update");
        frame.readable = true;
        fp.displayIsOn = true;
        run (1500);
        check (fp.planeOn && ctl.state() == "visible", "the display is back: the picture is shown again (" + ctl.state() + ")");
        check (fp.countOf ("setplane") > sets, "... through new plane updates");
        // with the display on, an unreadable framebuffer still disables the module after the limit
        frame.readable = false;
        run (4000);
        check (ctl.state().compare (0, 8, "disabled") == 0, "an unreadable framebuffer on a display that is on still disables the module (" + ctl.state() + ")");
    }
    // ---- the frame layout of the host's framebuffer: what may be mapped and read ----
    {
        check (frameLayoutReadable (800, 1280, 3200, 0), "an ordinary 800 x 1280 XRGB framebuffer is readable");
        check (frameLayoutReadable (800, 1280, 3328, 0), "... also with a padded pitch (128 byte aligned)");
        check (! frameLayoutReadable (800, 1280, 3000, 0), "a pitch shorter than a row of pixels is not readable (the last pixels of a row would be read from the next row, the last row past the mapping)");
        check (! frameLayoutReadable (0, 1280, 3200, 0) && ! frameLayoutReadable (800, 0, 3200, 0), "an empty framebuffer is not readable");
        check (! frameLayoutReadable (800, 1280, 0, 0), "a zero pitch is not readable");
        check (! frameLayoutReadable (100000, 1280, 400000, 0), "an absurd width is not readable");
        check (! frameLayoutReadable (800, 1280, 0xffffff00u, 0), "a pitch whose size overflows 32 bits is not readable");
        check (! frameLayoutReadable (800, 1280, 3200, 0xffffff00u), "an offset past any sane buffer is not readable");
        const size_t len = static_cast<size_t> (3328) * 1280 + 64;
        size_t at = 0;
        check (framePixelOffset (800, 1280, 3328, 64, len, 799, 1279, at) && at + 4 <= len && at == 64 + 3328u * 1279 + 799u * 4, "the last pixel is inside the mapping");
        check (! framePixelOffset (800, 1280, 3328, 64, len, 800, 0, at) && ! framePixelOffset (800, 1280, 3328, 64, len, 0, 1280, at) && ! framePixelOffset (800, 1280, 3328, 64, len, -1, 0, at) && ! framePixelOffset (800, 1280, 3328, 64, len, 0, -1, at), "outside the width and height: no pixel");
        check (! framePixelOffset (800, 1280, 3328, 64, len - 200, 799, 1279, at), "a mapping shorter than the layout says: the last pixel is refused instead of read past the mapping");
        check (! framePixelOffset (800, 1280, 3000, 0, 4000000, 799, 1279, at), "a pitch shorter than a row: refused");
    }

    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "OK", g_fails);
    return g_fails ? 1 : 0;
}
