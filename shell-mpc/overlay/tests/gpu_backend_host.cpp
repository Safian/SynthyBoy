// SynthyBoy - MPC overlay tests: the GPU helper's health state machine (gpu/gpu_overlay.h) with a fake helper process (healthy, slow to start, never answers, reports an error, dies,
// ignores quit), and the controller with that backend: the CPU draws first, the helper takes over when it is ready, the CPU is back within a tick when it fails, the kill switch file
// toggles live, the plugin closes while the helper runs. Every scenario ends with plane 38 off and none of the helper's framebuffers / GEM handles left.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <deque>

#include "fakes.h"

using namespace ovt;
using namespace synthyboy_mpc::overlay;

namespace
{
/** The helper process, as far as the plugin can tell: a queue of messages each way, a shared block, DRM objects it makes on the (fake) shared display. */
class FakeHelper final : public gpu::IHelperTransport
{
public:
    enum Mode { Healthy, NoReady, NoAck, ErrorOnShow, IgnoreQuit, NoHidden };
    FakeHelper (FakePlane& p, const double* clock) : plane (p), now (clock) {}
    ~FakeHelper() override { delete blk; }

    Mode mode = Healthy;
    uint32_t protocol = gpu::kProtocolVersion;   // the protocol version this helper binary was built with (the real helper refuses a hello of another one with an error message and exits)
    bool failStart = false;
    int starts = 0, kills = 0, quits = 0, closes = 0, scenes = 0, frames = 0;
    bool running = false;
    FakePlane& plane;

    bool start (int fd, uint32_t, uint32_t, uint32_t, std::string& error) override
    {
        if (fd != 42) { error = "bad descriptor"; return false; }
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
            outbox.push_back (gpu::makeMsg (gpu::kMsgReady, 50, 800, 1280, 0, 0, 0, "Fake GPU"));
            ready = true;
        }
        while (! inbox.empty() && running)
        {
            const gpu::Msg m = inbox.front();
            inbox.pop_front();
            if (m.type == gpu::kMsgShow)
            {
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
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
