// SynthyBoy - MPC overlay tests: the GPU helper's health state machine (gpu/gpu_overlay.h) with a fake helper process (healthy, slow to start, never answers, reports an error, dies,
// ignores quit), and the controller with that backend: the CPU draws first, the helper takes over when it is ready, the CPU is back within a tick when it fails, the kill switch file
// toggles live, the plugin closes while the helper runs. Every scenario ends with plane 38 off and none of the helper's framebuffers / GEM handles left.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cerrno>
#include <deque>
#include <fstream>

#include <cmath>

#include "fakes.h"

using namespace ovt;
using namespace synthyboy_mpc::overlay;

namespace
{
/** The helper process, as far as the plugin can tell: a queue of messages each way, a shared block, DRM objects it makes on the (fake) shared display. */
class FakeHelper final : public gpu::IHelperTransport
{
public:
    enum Mode { Healthy, NoReady, NoAck, ErrorOnShow, IgnoreQuit, NoHidden, SetPlaneError };
    FakeHelper (FakePlane& p, const double* clock) : plane (p), now (clock) {}
    ~FakeHelper() override { delete blk; }

    Mode mode = Healthy;
    uint32_t protocol = gpu::kProtocolVersion;   // the protocol version this helper binary was built with (the real helper refuses a hello of another one with an error message and exits)
    bool failStart = false;
    std::string rendererName = "Fake GPU";
    int strideSeen = 0;
    gpu::HelloParams lastHello;
    int starts = 0, kills = 0, quits = 0, closes = 0, scenes = 0, frames = 0;
    int stackingTakesAtStart = -1;   // how often the plugin had taken the stacking of the plane when the helper was started (it must be before the helper's first SETPLANE)
    bool running = false;
    FakePlane& plane;

    bool start (int fd, const gpu::HelloParams& hello, std::string& error) override
    {
        if (fd != 42) { error = "bad descriptor"; return false; }
        lastHello = hello;
        stackingTakesAtStart = plane.countOf ("takestacking");
        if (failStart) { error = "injected spawn failure"; return false; }
        delete blk;
        blk = new gpu::SharedBlock;
        gpu::initBlock (blk);
        running = true;
        eof = ready = shown = false;
        inbox.clear();
        outbox.clear();
        pid_ = 1000 + ++starts;
        readyAt = *now + 50.0;
        return true;
    }
    gpu::SharedBlock* block() noexcept override { return blk; }
    int pid() const noexcept override { return running ? pid_ : 0; }
    bool send (const gpu::Msg& m) override
    {
        if (! running || eof)
            return false;
        inbox.push_back (m);
        return true;
    }
    int receive (gpu::Msg& m) override
    {
        if (! outbox.empty()) { m = outbox.front(); outbox.pop_front(); return 1; }
        return eof ? -1 : 0;
    }
    bool alive() override { return running; }
    void kill() noexcept override { if (running) ++kills; running = false; }   // a kill leaves whatever the helper made on the display
    bool waitExit (int) noexcept override
    {
        step();   // the helper reads its quit
        return ! running;
    }
    void close() noexcept override { ++closes; running = false; }

    /** The helper process gets some time: reads what the plugin sent, does what it does. */
    void step()
    {
        if (! running)
            return;
        if (protocol != gpu::kProtocolVersion && ! eof)
        {
            outbox.push_back (gpu::makeMsg (gpu::kMsgError, 0, 0, 0, 0, 0, 0, "protocol version mismatch"));
            running = false;
            eof = true;
            return;
        }
        if (! ready && mode != NoReady && *now >= readyAt)
        {
            outbox.push_back (gpu::makeMsg (gpu::kMsgReady, 50, 800, 1280, 0, 0, 0, rendererName.c_str()));
            ready = true;
        }
        while (! inbox.empty() && running)
        {
            const gpu::Msg m = inbox.front();
            inbox.pop_front();
            if (m.type == gpu::kMsgShow)
            {
                if (mode == SetPlaneError)
                {
                    outbox.push_back (gpu::makeMsg (gpu::kMsgError, 0, 0, 0, 0, 0, 0, "SETPLANE: Invalid argument"));
                    running = false;
                    eof = true;
                    return;
                }
                if (mode == ErrorOnShow)
                {
                    outbox.push_back (gpu::makeMsg (gpu::kMsgError, 0, 0, 0, 0, 0, 0, "injected GL failure"));
                    running = false;   // it leaves what it made
                    eof = true;
                    return;
                }
                shown = true;
                outbox.push_back (gpu::makeMsg (gpu::kMsgShown));
            }
            else if (m.type == gpu::kMsgStride)
                strideSeen = static_cast<int> (m.a);
            else if (m.type == gpu::kMsgScene)
            {
                ++scenes;
                if (shown && mode != NoAck)
                    present (m.a);
            }
            else if (m.type == gpu::kMsgHide)
            {
                if (mode != NoHidden)
                {
                    clean();
                    shown = false;
                    outbox.push_back (gpu::makeMsg (gpu::kMsgHidden));
                }
            }
            else if (m.type == gpu::kMsgQuit)
            {
                ++quits;
                if (mode != IgnoreQuit)
                {
                    clean();
                    running = false;
                    eof = true;
                }
            }
        }
    }
    /** kill -9 from outside, or a crash: nothing is cleaned. */
    void die() { running = false; eof = true; }

private:
    void present (uint32_t seq)
    {
        const int slot = frames % 2;
        uint32_t handle = 0, fb = 0;
        gpu::ledgerGet (blk, slot, handle, fb);
        if (! fb)
        {
            handle = 900 + static_cast<uint32_t> (starts * 10 + slot);
            fb = 500 + static_cast<uint32_t> (starts * 10 + slot);
            plane.foreignHandles.insert (handle);
            plane.foreignFbs.insert (fb);
            gpu::ledgerSet (blk, slot, handle, fb);
        }
        plane.planeOn = true;
        ++frames;
        outbox.push_back (gpu::makeMsg (gpu::kMsgFrame, static_cast<uint32_t> (frames), seq, 700, 16000, 0, 0));
    }
    /** The helper's own clean exit of the display: plane off, its framebuffers and handles removed, the ledger empty. */
    void clean()
    {
        plane.planeOn = false;
        for (int i = 0; i < gpu::kMaxLedger; ++i)
        {
            uint32_t handle, fb;
            gpu::ledgerGet (blk, i, handle, fb);
            plane.foreignHandles.erase (handle);
            plane.foreignFbs.erase (fb);
            gpu::ledgerSet (blk, i, 0, 0);
        }
    }

    const double* now;
    gpu::SharedBlock* blk = nullptr;
    std::deque<gpu::Msg> inbox, outbox;
    bool eof = false, ready = false, shown = false;
public:
    bool isShown() const { return shown; }
private:
    int pid_ = 0;
    double readyAt = 0.0;
};

bool clean (const FakePlane& p) { return ! p.planeOn && p.foreignFbs.empty() && p.foreignHandles.empty(); }

struct BackendRig
{
    FakePlane plane;
    double now = 1000.0;
    FakeHelper helper { plane, &now };
    gpu::GpuHelperBackend backend { helper, plane };
    gpu::SceneData scene {};
    Rect ui { 56, 124, 1168, 232 }, panel { 124, 56, 232, 1168 };

    void advance (double ms, double step = 10.0)
    {
        const double end = now + ms;
        while (now < end)
        {
            now += step;
            helper.step();
            backend.poll (now);
        }
    }
    bool runUntil (const char* state, double maxMs)
    {
        for (double t = 0; t < maxMs; t += 10.0)
        {
            if (std::string (backend.status().state) == state)
                return true;
            advance (10.0);
        }
        return std::string (backend.status().state) == state;
    }
    bool startAndShow()
    {
        backend.request (now);
        advance (100.0);
        if (! backend.active())
            return false;
        if (! backend.show (ui, panel, 90, 0, "filter1", now))
            return false;
        return backend.submit (scene, now);
    }
};

struct CRig
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    double now = 0.0;
    FakeHelper helper { plane, &now };
    gpu::GpuHelperBackend backend { helper, plane };
    std::unique_ptr<Controller> ctl;
    CRig()
    {
        ctl = std::make_unique<Controller> (plane, frame, renderer, dir.paths(), Timing(), 10.0, &backend);
        inst.set ("filter1.cutoff", 1000.0f);
        inst.set ("filter1.resonance", 30.0f);
        ctl->attach (&inst);
        frame.setAll (true, 90);
    }
    void run (double ms, double step = 10.0)
    {
        const double end = now + ms;
        while (now < end) { now += step; helper.step(); ctl->tick (now); }
    }
    template <class F> double until (F cond, double maxMs, double step = 10.0)
    {
        const double start = now;
        while (now - start < maxMs) { now += step; helper.step(); ctl->tick (now); if (cond()) return now - start; }
        return -1.0;
    }
    std::string status() { return dir.read ("status.txt"); }
    static std::string line (const std::string& text, const std::string& key)
    {
        const size_t p = text.find (key + ": ");
        if (p == std::string::npos) return "";
        const size_t e = text.find ('\n', p);
        return text.substr (p + key.size() + 2, e - p - key.size() - 2);
    }
    std::string st (const std::string& key) { return line (status(), key); }
};


}

int main()
{
    // ---- healthy: start, show, scene, acknowledgement, hide ------------------------------------------------------------------------------------------------
    // ---- the rate of the pictures: a GPU of the Mali-T series keeps the grid of two vblanks (30 a second); another one is asked for one picture per vblank (60), the file says what it says
    {
        CRig r;
        r.run (3500);
        check (r.helper.strideSeen == 1 && r.st ("gpu_rate_stride") == "1", "rate: a GPU that is not a Mali-T is asked for a picture per vblank (stride 1)");
        CRig m;
        m.helper.rendererName = "Mali-T760 (Panfrost)";
        m.run (3500);
        check (m.helper.strideSeen == 0 && m.st ("gpu_rate_stride") == "2", "rate: a Mali-T keeps the grid of two vblanks (" + m.st ("gpu_rate_source") + ")");
        CRig f;
        std::ofstream (f.dir.paths().rate) << "30\n";
        f.run (3500);
        check (f.helper.strideSeen == 0 && f.st ("gpu_rate_stride") == "2", "rate: the file says 30: the grid of two vblanks");
        CRig g;
        g.helper.rendererName = "Mali-T760 (Panfrost)";
        std::ofstream (g.dir.paths().rate) << "60\n";
        g.run (3500);
        check (g.helper.strideSeen == 1, "rate: the file says 60: asked for it on a Mali-T too");
    }
    {
        BackendRig r;
        check (! r.backend.active() && r.backend.planeFree() && std::string (r.backend.status().state) == "idle", "before anything is wanted: idle, no helper");
        r.backend.request (r.now);
        check (std::string (r.backend.status().state) == "starting" && r.backend.status().pid > 0 && ! r.backend.active(), "request: the helper is started (not ready yet)");
        check (! r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now) && ! r.backend.submit (r.scene, r.now), "nothing is handed to a helper that is not ready");
        r.advance (30.0);
        check (! r.backend.active(), "not ready after 30 ms");
        r.advance (60.0);
        check (r.backend.active() && r.backend.status().glRenderer == "Fake GPU" && r.backend.status().startupMs == 50.0, "ready after 90 ms: active, renderer name and start-up time from the helper");
        check (r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now) && ! r.backend.planeFree(), "show: the plane belongs to the helper");
        check (r.backend.submit (r.scene, r.now) && (r.helper.block()->seq & 1u) == 0 && r.helper.block()->seq == 2, "submit: the scene is in the block under an even sequence number");
        r.advance (20.0);
        check (r.helper.frames == 1 && r.backend.status().frames == 1 && r.backend.status().helperSetPlaneMs > 15.0, "the helper presented a frame and the acknowledgement arrived (frames, timing)");
        r.advance (2000.0);
        check (r.backend.active() && std::string (r.backend.status().state) == "running", "an acknowledged scene leaves no time-out behind");
        r.backend.hide (r.now);
        check (! r.backend.planeFree(), "hide: the plane is free only after the helper answered");
        r.advance (20.0);
        check (r.backend.planeFree() && clean (r.plane), "hidden: plane off, no framebuffer, no handle left");
        r.backend.shutdown();
        check (! r.helper.running && r.helper.quits == 1 && clean (r.plane) && std::string (r.backend.status().state) == "idle", "shutdown: the helper was told to quit and left, nothing is left behind");
    }
    // ---- the stacking of the plane against the host's pointer plane (stacking.h): taken before the helper starts, put back when the helper is gone, kept when the plane was lost ----
    {
        BackendRig r;
        r.backend.request (r.now);
        check (r.helper.stackingTakesAtStart == 1 && r.plane.countOf ("takestacking") == 1, "the stacking is taken once before the helper is started (it comes before the helper's first SETPLANE)");
        r.advance (100.0);
        check (r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now), "the helper shows a view");
        r.advance (200.0);
        check (r.plane.countOf ("releasestacking") == 0, "while the helper runs the zpos stays as taken");
        r.helper.block()->pace.planeRefreshes = 1;   // the helper found the plane switched off by a mode set and set it again
        r.advance (50.0);
        check (r.plane.countOf ("takestacking") == 2, "the helper set a switched-off plane again: the stacking is taken again (" + std::to_string (r.plane.countOf ("takestacking")) + ")");
        r.advance (50.0);
        check (r.plane.countOf ("takestacking") == 2, "... once per event");
        r.backend.shutdown();
        check (r.plane.countOf ("releasestacking") == 1, "shutdown: the zpos at rest is put back");
        r.backend.request (r.now);
        check (r.plane.countOf ("takestacking") == 3 && r.helper.stackingTakesAtStart == 3, "the helper starts again: the stacking is taken again before it");
    }
    {
        BackendRig r;
        r.backend.request (r.now);
        r.advance (100.0);
        r.helper.mode = FakeHelper::ErrorOnShow;
        r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now);
        r.advance (60.0);
        check (r.plane.countOf ("releasestacking") >= 1, "the helper fails: the plugin takes the plane away and puts the zpos back");
    }
    {
        BackendRig r;
        r.plane.spares = { 52 };
        r.backend.request (r.now);
        r.advance (100.0);
        r.helper.mode = FakeHelper::SetPlaneError;
        r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now);
        r.advance (30.0);
        check (r.plane.countOf ("releasestacking") == 0, "a SETPLANE error (the plane may be another program's): the zpos is not written back on a plane that is not ours");
    }
    {
        BackendRig r;
        check (r.startAndShow(), "a helper is running with a view shown");
        r.backend.abandonPlane();
        check (r.plane.countOf ("releasestacking") == 0, "abandonPlane: the plane is not ours any more: no zpos is written back");
    }
    // ---- what the plugin tells the helper about the display it found (plane, CRTC, pace) -----------------------------------------------------------------------
    {
        BackendRig r;
        r.plane.crtc = 82;
        r.plane.plane = 46;
        r.plane.pace.periodMs = 17.711;
        r.plane.pace.stride = 2;
        r.plane.pace.vblank = true;
        r.plane.pace.pipe = 0;
        r.backend.request (r.now);
        const gpu::HelloParams h = r.helper.lastHello;
        check (h.crtc == 82 && h.plane == 46 && h.stride == 2 && h.pipe == 0 && h.periodUs == 17711 && (h.flags & gpu::kFlagVblank), "the hello carries the discovered CRTC, plane, pace stride, pipe and the measured period (XL numbers)");
        BackendRig k;
        k.plane.pace.vblank = false;
        k.backend.request (k.now);
        check (! (k.helper.lastHello.flags & gpu::kFlagVblank) && k.helper.lastHello.crtc == 37 && k.helper.lastHello.plane == 38, "a display whose vblank cannot be waited for: no vblank flag (the helper paces with the timer); the fake's own plane ids are passed on");
    }
    // ---- the helper's SETPLANE fails: the plane may have been taken: the next free plane is used for the restart, the plane that is not ours is not switched off --------------
    {
        BackendRig r;
        r.plane.spares = { 52 };
        r.backend.request (r.now);
        r.advance (100.0);
        r.helper.mode = FakeHelper::SetPlaneError;
        r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now);
        r.advance (30.0);
        check (r.plane.nextPlaneCalls == 1 && r.plane.plane == 52, "a SETPLANE error of the helper moves the plane to the next free one");
        check (r.plane.countOf ("forcedisable") == 0, "... without switching off the plane that another program may have");
        check (std::string (r.backend.status().state) == "backoff" && r.backend.status().lastError.find ("SETPLANE") != std::string::npos && r.backend.status().lastError.find ("switched to plane 52") != std::string::npos, "... the failure counts and says why");
        r.helper.mode = FakeHelper::Healthy;
        r.advance (1100.0);
        r.backend.request (r.now);
        check (r.helper.starts == 2 && r.helper.lastHello.plane == 52, "the restarted helper is told the new plane");
    }
    // ---- abandonPlane: the plane is not ours any more ------------------------------------------------------------------------------------------------------------
    {
        BackendRig r;
        check (r.startAndShow(), "a helper is running with a view shown");
        r.advance (50.0);
        const int forced = r.plane.countOf ("forcedisable");
        r.backend.abandonPlane();
        check (! r.helper.running && r.plane.countOf ("forcedisable") == forced && r.plane.foreignFbs.empty(), "abandonPlane: the helper is gone and its framebuffers are removed, the plane itself is left alone");
        check (std::string (r.backend.status().state) == "idle" && r.backend.status().failures == 0, "... a planned stop: no failure");
        r.backend.request (r.now);
        check (r.helper.starts == 2, "... and the helper starts again for the next picture");
    }
    // ---- the helper's presentation statistics reach the status ---------------------------------------------------------------------------------------------------
    {
        BackendRig r;
        check (r.startAndShow(), "a helper is running");
        gpu::SharedBlock* b = r.helper.block();
        b->pace.count = 40;
        b->pace.stride = 2;
        b->pace.vb[2] = 40;
        b->pace.ms[17] = 40;
        r.advance (20.0);
        const gpu::GpuStatus st = r.backend.status();
        check (st.paceKnown && st.pace.count == 40 && st.pace.vb[2] == 40 && st.pace.ms[17] == 40 && st.pace.stride == 2, "the pace block of the helper is read into the status");
    }
    // ---- the helper cannot be started -----------------------------------------------------------------------------------------------------------------------
    {
        BackendRig r;
        r.helper.failStart = true;
        r.backend.request (r.now);
        check (std::string (r.backend.status().state) == "backoff" && r.backend.status().lastError.find ("injected spawn failure") != std::string::npos, "a spawn failure: back-off, the reason is kept");
        r.backend.request (r.now + 500.0);
        check (r.helper.starts == 0 && std::string (r.backend.status().state) == "backoff", "no new try before the back-off is over");
        double t = r.now + 1000.0;
        for (int i = 0; i < 3; ++i)
        {
            r.backend.request (t);
            t += (i == 0 ? 2000.0 : i == 1 ? 4000.0 : 0.0);
        }
        check (std::string (r.backend.status().state) == "exhausted" && r.backend.status().failures == 4, "four failures in a row (the start and three restarts): exhausted");
        r.helper.failStart = false;
        r.backend.request (t + 100000.0);
        check (r.helper.starts == 0 && ! r.backend.active() && r.backend.planeFree(), "exhausted: never started again in this process");
    }
    // ---- it dies while a view is shown: kill -9 ---------------------------------------------------------------------------------------------------------------
    {
        BackendRig r;
        check (r.startAndShow(), "a helper is running with a view shown");
        r.advance (50.0);
        check (r.plane.planeOn && ! r.plane.foreignFbs.empty(), "the helper has the plane and framebuffers");
        r.helper.die();
        r.advance (10.0);
        check (! r.backend.active() && std::string (r.backend.status().state) == "backoff" && r.backend.planeFree(), "the helper died: not active within one poll, the plane is free for the CPU");
        check (clean (r.plane), "its plane was taken off and its framebuffers and handles were removed through the plugin's own descriptor");
        check (r.backend.status().lastError.find ("died") != std::string::npos, "the last error says it died");
        double t = r.now;
        r.backend.request (t + 500.0);
        check (r.helper.starts == 1, "no restart within the first second");
        r.backend.request (t + 1000.0);
        check (r.helper.starts == 2 && r.backend.status().restarts == 1, "a restart after one second");
        r.advance (100.0);
        check (r.backend.active(), "the restarted helper is ready");
        r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now);
        r.backend.submit (r.scene, r.now);
        r.advance (30.0);
        r.helper.die();
        r.advance (10.0);
        const double t2 = r.now;
        r.backend.request (t2 + 1500.0);
        check (r.helper.starts == 2, "the second back-off is two seconds");
        r.backend.request (t2 + 2000.0);
        check (r.helper.starts == 3 && clean (r.plane), "a restart after two seconds, nothing left of the second helper");
        r.advance (100.0);
        r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now);
        r.backend.submit (r.scene, r.now);
        r.advance (30.0);
        r.helper.die();
        r.advance (10.0);
        const double t3 = r.now;
        r.backend.request (t3 + 3900.0);
        check (r.helper.starts == 3, "the third back-off is four seconds");
        r.backend.request (t3 + 4000.0);
        check (r.helper.starts == 4, "a restart after four seconds");
        r.advance (100.0);
        r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now);
        r.backend.submit (r.scene, r.now);
        r.advance (30.0);
        r.helper.die();
        r.advance (10.0);
        check (std::string (r.backend.status().state) == "exhausted" && clean (r.plane), "the fourth death: exhausted, the CPU keeps the plane for the life of the process");
        r.backend.request (r.now + 1e7);
        check (r.helper.starts == 4, "and no fifth start");
    }
    // ---- it reports an error ---------------------------------------------------------------------------------------------------------------------------------
    {
        BackendRig r;
        r.backend.request (r.now);
        r.advance (100.0);
        r.helper.mode = FakeHelper::ErrorOnShow;
        r.backend.show (r.ui, r.panel, 90, 0, "filter1", r.now);
        r.backend.submit (r.scene, r.now);
        r.advance (20.0);
        check (std::string (r.backend.status().state) == "backoff" && r.backend.status().lastError.find ("injected GL failure") != std::string::npos, "an error message: back-off, the helper's text is the last error");
        check (clean (r.plane) && r.backend.planeFree(), "after an error: plane off, nothing left");
    }
    // ---- a helper of another protocol version (an old helper next to a new plugin, or the other way round): refused, the CPU draws, nothing is left --------------------
    {
        BackendRig r;
        r.helper.protocol = gpu::kProtocolVersion - 1;
        r.backend.request (r.now);
        r.advance (100.0);
        check (! r.backend.active() && std::string (r.backend.status().state) == "backoff" && r.backend.status().lastError.find ("protocol version mismatch") != std::string::npos,
               "a helper of another protocol version answers with an error: no GPU drawing, back-off, the reason is kept (" + r.backend.status().lastError + ")");
        check (clean (r.plane) && r.backend.planeFree(), "... and nothing is left on the display");
        r.helper.protocol = gpu::kProtocolVersion + 1;
        r.backend.request (r.now + 1.0e7);
        r.advance (200.0);
        check (! r.backend.active(), "a helper of a newer version is refused as well");
    }
    {
        CRig r;
        r.helper.protocol = gpu::kProtocolVersion - 1;
        r.run (30000);
        check (r.st ("renderer") == "cpu" && r.plane.planeOn && r.st ("gpu_state") == "exhausted" && r.st ("gpu_last_error").find ("protocol version mismatch") != std::string::npos,
               "the controller with a helper of another version: the CPU draws all the time (the picture is on the plane), the status says why (" + r.st ("gpu_last_error") + ")");
        check (r.plane.foreignFbs.empty() && r.helper.frames == 0, "nothing of the helper is on the display, it presented no picture");
    }
    // ---- slow: never ready; never acknowledges; never answers a hide -----------------------------------------------------------------------------------------
    {
        BackendRig r;
        r.helper.mode = FakeHelper::NoReady;
        r.backend.request (r.now);
        r.advance (2900.0);
        check (std::string (r.backend.status().state) == "starting", "a slow start is waited for (2.9 s)");
        r.advance (200.0);
        check (std::string (r.backend.status().state) == "backoff" && r.helper.kills >= 1 && r.backend.status().lastError.find ("ready") != std::string::npos, "no `ready` within 3 s: the helper is killed, back-off");
    }
    {
        BackendRig r;
        check (r.startAndShow(), "(running)");
        r.helper.mode = FakeHelper::NoAck;
        r.backend.submit (r.scene, r.now);
        r.advance (500.0);
        check (r.backend.active(), "a scene without an answer for 500 ms is still waited for");
        r.advance (400.0);
        check (! r.backend.active() && std::string (r.backend.status().state) == "backoff" && r.backend.status().lastError.find ("acknowledge") != std::string::npos, "no acknowledgement for 700 ms: killed, back-off, the plane is the CPU's");
        check (clean (r.plane) && r.helper.kills >= 1, "(and nothing left)");
    }
    {
        BackendRig r;
        check (r.startAndShow(), "(running)");
        r.advance (30.0);
        r.helper.mode = FakeHelper::NoHidden;
        r.backend.hide (r.now);
        r.advance (500.0);
        check (r.backend.active() && ! r.backend.planeFree(), "a hide without an answer is waited for 500 ms");
        r.advance (300.0);
        check (! r.backend.active() && r.backend.planeFree() && clean (r.plane), "a hide without an answer for 700 ms: the helper is killed and the plane cleaned");
    }
    // ---- the switch -----------------------------------------------------------------------------------------------------------------------------------------
    {
        BackendRig r;
        check (r.startAndShow(), "(running)");
        r.advance (30.0);
        r.backend.setForceCpu (true, r.now);
        check (! r.backend.active() && std::string (r.backend.status().state) == "off" && ! r.helper.running && clean (r.plane) && r.backend.planeFree(), "the switch: the helper is stopped, plane off, nothing left (no failure counted)");
        check (r.backend.status().failures == 0, "a planned stop is no failure");
        r.backend.request (r.now + 5000.0);
        check (r.helper.starts == 1, "switched off: no helper is started");
        r.backend.setForceCpu (false, r.now + 6000.0);
        r.backend.request (r.now + 6000.0);
        check (r.helper.starts == 2 && std::string (r.backend.status().state) == "starting", "switched on again: the helper starts at once");
    }
    // ---- the plugin closes while the helper runs --------------------------------------------------------------------------------------------------------------
    {
        BackendRig r;
        check (r.startAndShow(), "(running)");
        r.advance (30.0);
        r.backend.shutdown();
        check (! r.helper.running && clean (r.plane) && r.helper.quits == 1 && r.helper.kills == 0, "close: a quit, the helper leaves, nothing is left");
    }
    {
        BackendRig r;
        check (r.startAndShow(), "(running)");
        r.advance (30.0);
        r.helper.mode = FakeHelper::IgnoreQuit;
        r.backend.shutdown();
        check (! r.helper.running && r.helper.kills == 1 && clean (r.plane), "close with a helper that ignores quit: killed, and its plane and framebuffers are removed from here");
    }
    // ---- a helper that ran well is forgiven -------------------------------------------------------------------------------------------------------------------
    {
        BackendRig r;
        check (r.startAndShow(), "(running)");
        r.helper.die();
        r.advance (10.0);
        r.backend.request (r.now + 1000.0);
        r.now += 1000.0;
        r.advance (100.0);
        check (r.backend.active() && r.backend.status().restarts == 1, "(a restart after one death)");
        r.advance (310000.0, 1000.0);
        r.helper.die();
        r.advance (10.0);
        const double t = r.now;
        r.backend.request (t + 1000.0);
        check (r.helper.starts == 3, "after five minutes of good running the next failure starts the back-off from one second again");
    }

    // ===================================================================================================================================================================
    // the controller with the backend
    {
        // ---- the CPU draws first, the helper takes over when it is ready ----
        {
            CRig r;
            const double t = r.until ([&] { return r.plane.planeOn; }, 500);
            check (t >= 0 && t <= 100 && ! r.ctl->usingGpu(), "the first picture is the CPU's (the helper is still starting): plane on within 100 ms");
            check (r.helper.running && r.helper.starts == 1, "the helper was started by the first visible view");
            const double g = r.until ([&] { return r.ctl->usingGpu(); }, 1000);
            check (g >= 0, "the helper is ready and takes over (" + std::to_string (g) + " ms)");
            r.run (300);
            check (! r.plane.buffersAlive && r.plane.planeOn && r.helper.frames >= 1, "the CPU buffers are gone, the helper's picture is on the plane");
            r.run (1200);
            check (r.st ("renderer") == "gpu" && r.st ("gpu_state") == "running" && std::stoi (r.st ("gpu_helper_pid")) > 0 && r.st ("view") == "filter1" && r.st ("state") == "visible", "status file: renderer gpu, helper running with its pid, view filter1 visible");
            check (r.st ("gpu_renderer") == "Fake GPU" && r.st ("gpu_last_error") == "none" && r.st ("gpu_helper_restarts") == "0", "status file: GL renderer, no error, no restart");
            const int scenes = r.helper.scenes;
            r.run (3000);
            check (r.helper.scenes == scenes, "nothing changed for 3 s: no new scene was sent (" + std::to_string (scenes) + ")");
            r.inst.set ("filter1.resonance", 70.0f);
            r.run (300);
            check (r.helper.scenes > scenes, "a parameter changed: a new scene");
            // the scene is the filter picture
            check (r.helper.block()->seq >= 4, "scenes are written under the seqlock (sequence " + std::to_string (r.helper.block()->seq) + ")");
            // leaving the tab: the plane goes off through the helper
            r.frame.setAll (false, 90);
            r.run (400);
            check (! r.plane.planeOn && clean (r.plane) && r.st ("state").find ("hidden") == 0, "marker gone: hidden, plane off, no framebuffer of the helper left");
            // back
            r.frame.setAll (true, 90);
            r.run (400);
            check (r.plane.planeOn && r.ctl->usingGpu(), "the markers are back: the helper draws again (it was kept)");
            check (r.helper.starts == 1, "the same helper process all the time");
            // closing
            r.ctl->shutdown();
            check (! r.helper.running && clean (r.plane), "the plugin closes while the helper runs: it is stopped, plane 38 is off, nothing is left");
        }
        // ---- the helper is killed while the view is visible ----
        {
            CRig r;
            r.until ([&] { return r.ctl->usingGpu() && r.plane.planeOn; }, 2000);
            r.run (300);
            check (r.ctl->usingGpu() && r.helper.frames >= 1, "(the helper draws)");
            const int cpuFramesBefore = r.plane.presented;
            r.helper.die();
            const double t = r.until ([&] { return r.plane.buffersAlive && r.plane.planeOn && ! r.ctl->usingGpu(); }, 1000);
            check (t >= 0 && t <= 100, "kill -9: the CPU path draws again within one tick or two (" + std::to_string (t) + " ms)");
            check (r.plane.foreignFbs.empty() && r.plane.foreignHandles.empty() && r.plane.presented > cpuFramesBefore, "the dead helper's framebuffers and handles are gone, the CPU presented a picture");
            r.run (30);   // the status file is written when the renderer changes, not only once a second
            check (r.st ("renderer") == "cpu" && r.st ("gpu_state") == "backoff" && r.st ("gpu_last_error").find ("died") != std::string::npos, "status file: renderer cpu, back-off, the last error says it died");
            // the picture keeps updating on the CPU
            const int p0 = r.plane.presented;
            r.inst.set ("filter1.cutoff", 3000.0f);
            r.run (300);
            check (r.plane.presented > p0, "the picture keeps updating while the CPU draws");
            // the restart after the back-off, and the GPU again
            const double g = r.until ([&] { return r.ctl->usingGpu(); }, 3000);
            check (g >= 0 && r.helper.starts == 2, "the helper is restarted after the back-off and takes over again (" + std::to_string (g) + " ms)");
            r.run (1200);
            check (r.st ("renderer") == "gpu" && r.st ("gpu_helper_restarts") == "1", "status file: gpu again, one restart");
            r.ctl->shutdown();
            check (clean (r.plane), "(closed clean)");
        }
        // ---- the file switch, both ways, while visible ----
        {
            CRig r;
            r.until ([&] { return r.ctl->usingGpu(); }, 2000);
            r.run (300);
            r.dir.touch ("nogpu");
            const double t = r.until ([&] { return ! r.ctl->usingGpu() && r.plane.buffersAlive && r.plane.planeOn; }, 2500);
            check (t >= 0 && t <= 1100, "the file appears: the CPU draws within about a second (" + std::to_string (t) + " ms)");
            check (! r.helper.running && r.plane.foreignFbs.empty(), "the helper was stopped (no process, no framebuffer) by the switch");
            r.run (1200);
            check (r.st ("renderer") == "cpu" && r.st ("gpu_state") == "off", "status file: renderer cpu, gpu_state off");
            const int starts = r.helper.starts;
            r.run (3000);
            check (r.helper.starts == starts, "with the file there the helper is not started again");
            r.dir.remove ("nogpu");
            const double g = r.until ([&] { return r.ctl->usingGpu(); }, 4000);
            check (g >= 0 && g <= 2500, "the file is removed: the GPU draws again (" + std::to_string (g) + " ms)");
            r.run (100);
            check (r.helper.starts == starts + 1 && r.plane.planeOn && ! r.plane.buffersAlive, "a new helper, the CPU buffers are gone");
            r.ctl->shutdown();
        }
        // ---- a helper that always fails: the CPU for the life of the process ----
        {
            CRig r;
            r.helper.mode = FakeHelper::ErrorOnShow;
            r.run (20000);
            check (r.helper.starts == 4 && r.st ("gpu_state") == "exhausted" && r.st ("renderer") == "cpu", "a helper that fails at every start: four starts, then exhausted, renderer cpu");
            check (r.plane.planeOn && r.plane.foreignFbs.empty() && r.st ("gpu_failures") == "4", "the CPU picture is on the plane all the time, nothing of the helper is left");
            const int starts = r.helper.starts;
            r.run (30000);
            check (r.helper.starts == starts, "no fifth start");
        }
        // ---- a helper that hangs (never answers a scene) ----
        {
            CRig r;
            r.until ([&] { return r.ctl->usingGpu(); }, 2000);
            r.run (200);
            r.helper.mode = FakeHelper::NoAck;
            r.inst.set ("filter1.cutoff", 4000.0f);   // a new scene that is never answered
            const double t = r.until ([&] { return ! r.ctl->usingGpu() && r.plane.planeOn && r.plane.buffersAlive; }, 3000);
            check (t >= 0 && t <= 1000, "the helper hangs: the CPU draws within one second (" + std::to_string (t) + " ms)");
            check (r.helper.kills >= 1 && r.plane.foreignFbs.empty(), "the hung helper was killed and cleaned up");
        }
        // ---- two instances on the GPU path: the slot colour on screen picks the data, a colour that is nobody's takes the picture off ----
        {
            CRig r;
            FakeInstance second;
            second.set ("filter1.cutoff", 4000.0f);
            second.set ("filter1.resonance", 70.0f);
            r.ctl->attach (&second);
            r.until ([&] { return r.ctl->usingGpu() && r.ctl->activeSlot() == 0; }, 2000);
            r.run (300);
            int scenes = r.helper.scenes;
            r.frame.setViewInstance (0, 1, true, 90);
            const double t = r.until ([&] { return r.ctl->activeSlot() == 1 && r.helper.scenes > scenes; }, 1000);
            check (t >= 0 && t <= 200 && r.ctl->usingGpu(), "GPU path: the colour of slot 1 hands the helper instance 1's scene within 200 ms (" + std::to_string (t) + " ms)");
            r.run (500);
            scenes = r.helper.scenes;
            r.run (1000);
            check (r.helper.scenes == scenes, "... and nothing more while nothing changes");
            r.frame.setViewInstance (0, 9, true, 90);   // nobody's slot
            const double h = r.until ([&] { return ! r.helper.isShown(); }, 1000);
            check (h >= 0 && h <= 300 && r.ctl->state() == "hidden: instance slot 9 is not alive", "GPU path: a slot nobody holds: the helper is told to hide (" + std::to_string (h) + " ms)");
            r.frame.setViewInstance (0, 0, true, 90);
            check (r.until ([&] { return r.helper.isShown() && r.ctl->activeSlot() == 0; }, 1000) >= 0, "GPU path: slot 0 again: shown");
            r.ctl->detach (&second);
        }
        // ---- the scenes follow the pace grid of the display: 80 % of two vblanks, so that no slot of the grid finds no scene ----
        {
            CRig r;
            r.plane.pace.periodMs = 17.711;
            r.plane.pace.stride = 2;
            r.plane.pace.vblank = true;
            r.run (400);
            double wait = 0.0;
            for (int i = 0; i < 60; ++i)
            {
                r.now += 10.0;
                r.helper.step();
                r.inst.set ("filter1.cutoff", 400.0f + static_cast<float> (i * 131 % 4000));
                wait = r.ctl->tick (r.now);
            }
            check (r.ctl->usingGpu() && std::fabs (wait - 0.8 * 2 * 17.711) < 0.5, "GPU path on a 56.46 Hz display: a scene every 28.3 ms (due in " + std::to_string (wait) + " ms)");
            const std::string st = r.status();
            check (r.st ("display_crtc") == "37" && r.st ("display_overlay_plane") == "38" && st.find ("pace_stride_vblanks") == std::string::npos, "the status file has the display lines of the backend (the fake's)");
        }
        // ---- the scene goes BEFORE the page detection of the same step (reading the CRTC can wait for the display's locks while the helper's plane update holds them) ----
        {
            CRig r;
            r.run (400);
            int kicksBeforeDetect = 0, detects = 0;
            int scenesAtStart = 0;
            r.frame.onBegin = [&] { ++detects; if (static_cast<int> (r.helper.block()->seq) > scenesAtStart) ++kicksBeforeDetect; };
            for (int i = 0; i < 60; ++i)
            {
                r.now += 10.0;
                r.helper.step();
                r.inst.set ("filter1.cutoff", 400.0f + static_cast<float> (i * 131 % 4000));
                scenesAtStart = static_cast<int> (r.helper.block()->seq);
                r.ctl->tick (r.now);
            }
            check (r.ctl->usingGpu() && detects >= 5 && kicksBeforeDetect == detects, "GPU path: in every step that reads the framebuffer the scene was handed over first (" + std::to_string (kicksBeforeDetect) + " of " + std::to_string (detects) + ")");
        }
        // ---- without a backend the controller is the one of the CPU build ----
        {
            TempDir dir;
            FakePlane plane;
            FakeFrame frame;
            CpuCurveRenderer renderer;
            FakeInstance inst;
            Controller ctl (plane, frame, renderer, dir.paths(), Timing(), 10.0);
            ctl.attach (&inst);
            frame.setAll (true, 90);
            double now = 0.0;
            for (int i = 0; i < 300; ++i) { now += 10.0; ctl.tick (now); }
            const std::string st = dir.read ("status.txt");
            check (plane.planeOn && ! ctl.usingGpu() && st.find ("renderer: cpu") != std::string::npos && st.find ("gpu_state: not built") != std::string::npos, "no GPU backend: the CPU draws, the status file says so");
        }
    }
    // ---- the helper's log file does not grow without bound (the file lives in /tmp, which is memory on the device) ----
    {
        TempDir dir;
        const std::string log = dir.path + "/helper.log";
        check (! gpu::trimLogFile (log.c_str(), 1000), "no file: nothing to trim");
        dir.write ("helper.log", std::string (900, 'x'));
        check (! gpu::trimLogFile (log.c_str(), 1000) && dir.read ("helper.log").size() == 900, "a log under the limit is kept");
        dir.write ("helper.log", std::string (1500, 'y'));
        check (gpu::trimLogFile (log.c_str(), 1000) && dir.read ("helper.log").empty(), "a log over the limit is emptied");
        // a writer that has the file open for appending (the helper) goes on at the new end
        FILE* w = std::fopen (log.c_str(), "a");
        std::fputs (std::string (1200, 'z').c_str(), w);
        std::fflush (w);
        check (gpu::trimLogFile (log.c_str(), 1000), "a file written by a running helper is over the limit again");
        std::fputs ("after", w);
        std::fclose (w);
        check (dir.read ("helper.log") == "after", "... and its next lines start at the beginning (no hole of zeros)");
        check (! gpu::trimLogFile ("", 1000) && ! gpu::trimLogFile (nullptr, 1000), "no path: nothing happens");
    }
    // ---- what waitpid says about the helper: only its own exit counts; a child that is not ours (any more) is never signalled ----
    {
        using gpu::ChildWait;
        check (gpu::classifyWait (1234, 0, 0) == ChildWait::Running, "waitpid returned 0: still running");
        check (gpu::classifyWait (1234, 1234, 0) == ChildWait::Exited, "waitpid returned the pid: it exited and was reaped by us");
        check (gpu::classifyWait (1234, -1, ECHILD) == ChildWait::NotOurs, "ECHILD: reaped by somebody else (or SIGCHLD ignored): it is gone, and the number may be given to another process by now");
        check (gpu::classifyWait (1234, -1, EINTR) == ChildWait::Running, "an interrupted call tells nothing: not gone");
        check (gpu::classifyWait (1234, 99, 0) == ChildWait::Running, "another child's pid is no news about this one");
        check (gpu::mayKill (ChildWait::Running) && ! gpu::mayKill (ChildWait::Exited) && ! gpu::mayKill (ChildWait::NotOurs), "SIGKILL only for a child that is known to be running");
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
