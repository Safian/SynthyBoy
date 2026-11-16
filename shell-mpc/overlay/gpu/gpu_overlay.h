// SynthyBoy - MPC overlay, GPU helper: the plugin's side. IGpuOverlay is what the controller talks to; GpuHelperBackend implements it on top of an IHelperTransport (the process and
// its channels: gpu/helper_transport_linux.h on the device, a fake in the tests) and the IPlaneBackend (for the display descriptor and the clean-up of what a dead helper leaves).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The health state machine (docs/MPC.md "Overlay (experimental)", "GPU drawing in a helper process"):
//   Idle      no helper (not wanted yet, or switched off by the file)
//   Starting  spawned, waiting for its `ready` (time-out: kStartTimeoutMs)
//   Running   acknowledged; pictures go to it
//   Backoff   it failed (died, reported an error, did not answer in time): plane 38 and the helper's DRM objects were cleaned, the CPU draws; a new start after 1, 2, 4 s
//   Exhausted more than kMaxRestarts failures: the CPU draws for the life of the process
// A failure is: the channel closed or the process gone, an `error` message, no `ready` in time, a scene without an acknowledgement for kAckTimeoutMs, a hide without `hidden`.
// Whatever the cause, the helper is killed and reaped FIRST (so nothing can touch the plane any more), then the plane is taken off and the framebuffers / GEM handles of the ledger
// are removed through the plugin's own descriptor, then the state moves on. All calls are made by the overlay thread (shutdown () by the thread that stopped it).
#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../drm_interfaces.h"
#include "../geometry.h"
#include "scene.h"

namespace synthyboy_mpc { namespace overlay { namespace gpu {

/** Empties the file when it is larger than `maxBytes` (the helper's log lives in /tmp, which is memory on the device: Mesa warnings and restarts would fill it over weeks). A process that
    has the file open for appending (the helper) goes on at the new end. true: the file was emptied. false: no such file, small enough, or the call failed. */
inline bool trimLogFile (const char* path, long long maxBytes) noexcept
{
    if (path == nullptr || *path == 0)
        return false;
    struct stat st;
    if (::stat (path, &st) != 0 || static_cast<long long> (st.st_size) <= maxBytes)
        return false;
    return ::truncate (path, 0) == 0;
}

/** What waitpid (pid, WNOHANG) said about the helper, as the transport must read it. */
enum class ChildWait
{
    Running,    // 0, or an interrupted call: still there as far as we know
    Exited,     // the pid itself: it exited and we reaped it (the number is free from now on)
    NotOurs     // ECHILD: somebody else reaped it (a SIGCHLD handler of the application) or SIGCHLD is ignored: it is dead, and the number may be given to ANOTHER process any time
};
inline ChildWait classifyWait (pid_t pid, pid_t waitResult, int err) noexcept
{
    if (waitResult == pid)
        return ChildWait::Exited;
    if (waitResult < 0 && err == ECHILD)
        return ChildWait::NotOurs;
    return ChildWait::Running;
}
/** A signal may only go to a child that is known to be running: after Exited or NotOurs the number belongs to nobody we know (kill (pid, 0) cannot tell a stranger from the helper). */
inline bool mayKill (ChildWait w) noexcept { return w == ChildWait::Running; }

/** The helper process and the two channels to it. The real one is in helper_transport_linux.h; the tests have a fake. Not thread safe: the overlay thread only. */
class IHelperTransport
{
public:
    virtual ~IHelperTransport() = default;
    /** Creates the shared block and the socket pair, starts the helper and sends it the `hello` with the display descriptor and the block. false: `error` says why (nothing is left). */
    virtual bool start (int displayFd, const HelloParams& hello, std::string& error) = 0;
    /** The shared block of the running (or just killed) helper; nullptr when there is none. Valid until close (). */
    virtual SharedBlock* block() noexcept = 0;
    virtual int pid() const noexcept = 0;
    /** false: the channel is gone. */
    virtual bool send (const Msg& m) = 0;
    /** 1: a message was read; 0: nothing pending; -1: the channel is closed (the helper is gone). Never blocks. */
    virtual int receive (Msg& m) = 0;
    /** The process is still there (not exited). */
    virtual bool alive() = 0;
    /** SIGKILL and wait for the process (returns when it is gone). The block and the channel stay readable. Safe at any time. */
    virtual void kill() noexcept = 0;
    /** Waits up to `ms` for the process to exit by itself (after a `quit`). true: it is gone. */
    virtual bool waitExit (int ms) noexcept = 0;
    /** Closes the channel and unmaps the block (the helper must be gone). Safe at any time, also twice. */
    virtual void close() noexcept = 0;
};

struct GpuStatus
{
    bool active = false;             // renderer: gpu
    const char* state = "idle";      // idle | starting | running | backoff | exhausted | off (the file switch)
    int pid = 0;
    unsigned starts = 0, restarts = 0, failures = 0;
    std::string lastError;
    unsigned long frames = 0;        // pictures the helper has presented (its own count)
    double helperRenderMs = 0.0, helperSetPlaneMs = 0.0;
    unsigned long skippedVblanks = 0, lateSetPlanes = 0;
    std::string glRenderer;
    double startupMs = 0.0;
    RenderBlock render {};           // the helper's render statistics (cumulative: two snapshots give a window)
    PaceBlock pace {};               // the helper's presentation intervals (shared block), valid when pace.count > 0 or the helper has run
    bool paceKnown = false;
};

/** What the controller needs of the GPU path. */
class IGpuOverlay
{
public:
    virtual ~IGpuOverlay() = default;
    /** Every tick: reads the helper's messages, checks the time-outs, finishes a back-off. Never blocks. */
    virtual void poll (double nowMs) = 0;
    /** The overlay wants to show something: starts the helper when it is not running (and not in back-off, exhausted or switched off). */
    virtual void request (double nowMs) = 0;
    /** The helper is running and has said it is ready: pictures go to it. */
    virtual bool active() const noexcept = 0;
    /** No picture of the helper is on the plane and none is about to be (the CPU path may use the plane). */
    virtual bool planeFree() const noexcept = 0;
    /** Shows the region: `panel` is the plane rectangle on the panel, `ui` the landscape region (the canvas of the scenes). Sent again only when something changed. false: not active. */
    virtual bool show (const Rect& ui, const Rect& panel, int rotation, int viewIndex, const char* viewName, double nowMs) = 0;
    /** Hands a scene to the helper (written under the seqlock, then a kick). false: not active / not shown. */
    virtual bool submit (const SceneData& scene, double nowMs) = 0;
    /** The plane goes off (the helper takes it off and answers `hidden`). planeFree () becomes true with the answer, or after the clean-up when the helper does not answer. */
    virtual void hide (double nowMs) = 0;
    /** The vblanks per picture the helper paces with (1 .. 4; the hello has the first one). Sent at once when the helper runs, and again after a restart. */
    virtual void setStride (int /*vblanks*/) {}
    /** The file switch: true = never the helper (it is stopped), false = it may be used again. */
    virtual void setForceCpu (bool force, double nowMs) = 0;
    /** The helper is stopped, the plane is off, nothing of it is left. Safe at any time, also twice. */
    virtual void shutdown() noexcept = 0;
    /** The plane was taken by another program: the helper is stopped and its framebuffers and handles removed, but the plane is NOT touched (it is not ours any more). A planned stop: no
        failure; the helper starts again for the next picture with the plane the display backend names then. */
    virtual void abandonPlane() noexcept = 0;
    virtual GpuStatus status() const = 0;
};

struct GpuConfig
{
    uint32_t flags = kFlagDump;
    double startTimeoutMs = 3000.0;     // spawn -> ready (the device needs about 100 ms)
    double ackTimeoutMs = 700.0;        // a scene (or a show) without a frame acknowledgement: the helper hangs
    double hideTimeoutMs = 700.0;
    int maxRestarts = 3;                // restarts after a failure; the fourth failure keeps the CPU for good
    double backoffMs = 1000.0;          // before the first restart; doubles
    double healthyResetMs = 300000.0;   // a helper that ran this long without a failure gets its failure count back to zero
    int quitWaitMs = 400;               // how long shutdown () waits for the helper to leave by itself
};

class GpuHelperBackend final : public IGpuOverlay
{
public:
    GpuHelperBackend (IHelperTransport& transport, IPlaneBackend& plane, const GpuConfig& cfg = GpuConfig()) : tr_ (transport), plane_ (plane), cfg_ (cfg) {}
    ~GpuHelperBackend() override { shutdown(); }

    void poll (double now) override
    {
        if (state_ == State::Backoff)
            return;   // request () ends the back-off: nothing wants the helper while nothing is visible
        if (state_ != State::Starting && state_ != State::Running)
            return;
        Msg m;
        for (int guard = 0; guard < 256; ++guard)
        {
            const int r = tr_.receive (m);
            if (r < 0)
                return fail (now, "the channel to the helper closed (the helper died)");
            if (r == 0)
                break;
            if (m.magic != kMsgMagic)
                return fail (now, "bad message from the helper");
            if (! handle (m, now))
                return;
        }
        if (SharedBlock* b = tr_.block())
        {
            readPace (b);
            if (pace_.planeRefreshes != lastRefreshes_)
            {
                lastRefreshes_ = pace_.planeRefreshes;
                if (state_ == State::Running)
                    plane_.takeStacking();   // the helper found the plane switched off (a mode set of the application) and set it again: the zpos may have been reset with it
            }
        }
        if (! tr_.alive())
            return fail (now, "the helper process is gone");
        if (state_ == State::Starting && now - startedAt_ > cfg_.startTimeoutMs)
            return fail (now, "the helper did not become ready in time");
        if (state_ == State::Running)
        {
            if (shown_ && now - stackingCheckedAt_ >= kStackingCheckMs)
            {
                stackingCheckedAt_ = now;
                plane_.takeStacking();   // the order of the plane is checked while the helper shows it (reads only; a write when something else changed the zpos)
            }
            if (unacked_ && now - unackedSince_ > cfg_.ackTimeoutMs)
                return fail (now, "the helper did not acknowledge a picture in time");
            if (hiding_ && now - hideAt_ > cfg_.hideTimeoutMs)
                return fail (now, "the helper did not answer a hide in time");
            if (failures_ > 0 && now - runningSince_ > cfg_.healthyResetMs)
                failures_ = 0;
        }
    }

    void request (double now) override
    {
        if (forceCpu_)
            return;
        if (state_ == State::Idle)
        {
            if (now >= retryAt_)
                startHelper (now);
        }
        else if (state_ == State::Backoff && now >= retryAt_)
        {
            ++restarts_;
            startHelper (now);
        }
    }

    bool active() const noexcept override { return state_ == State::Running && ! forceCpu_; }
    bool planeFree() const noexcept override { return ! shown_ && ! hiding_; }

    bool show (const Rect& ui, const Rect& panel, int rotation, int viewIndex, const char* name, double now) override
    {
        if (! active())
            return false;
        if (shown_ && ui == ui_ && panel == panel_ && rotation == rotation_ && viewIndex == viewIndex_)
            return true;
        if (hiding_)
            return true;   // the plane is on its way off: the next tick shows again
        const Msg m = makeMsg (kMsgShow, static_cast<uint32_t> (ui.w), static_cast<uint32_t> (ui.h), static_cast<uint32_t> (panel.x), static_cast<uint32_t> (panel.y), static_cast<uint32_t> (rotation),
                               static_cast<uint32_t> (viewIndex), name);
        if (! tr_.send (m))
        {
            fail (now, "cannot send to the helper");
            return false;
        }
        ui_ = ui;
        panel_ = panel;
        rotation_ = rotation;
        viewIndex_ = viewIndex;
        shown_ = true;
        return true;
    }

    bool submit (const SceneData& scene, double now) override
    {
        if (! active() || ! shown_ || hiding_ || ! tr_.block())
            return false;
        lastSeq_ = writeScene (tr_.block(), scene);
        if (! tr_.send (makeMsg (kMsgScene, lastSeq_)))
        {
            fail (now, "cannot send to the helper");
            return false;
        }
        if (! unacked_)
        {
            unacked_ = true;
            unackedSince_ = now;
        }
        return true;
    }

    void setStride (int vblanks) override
    {
        wantStride_ = std::clamp (vblanks, 1, 4);
        if (state_ == State::Running)
            tr_.send (makeMsg (kMsgStride, static_cast<uint32_t> (wantStride_)));
    }

    void hide (double now) override
    {
        if (! shown_)
            return;
        shown_ = false;
        if (state_ != State::Running)
            return;
        if (! tr_.send (makeMsg (kMsgHide)))
        {
            fail (now, "cannot send to the helper");
            return;
        }
        hiding_ = true;
        hideAt_ = now;
        unacked_ = false;   // a hide answers by itself
    }

    void setForceCpu (bool force, double now) override
    {
        if (force == forceCpu_)
            return;
        forceCpu_ = force;
        if (force)
        {
            stopHelper (now);    // a planned stop: no failure, no back-off
            state_ = State::Idle;
            retryAt_ = 0.0;
        }
    }

    void shutdown() noexcept override
    {
        stopHelper (0.0);
        if (state_ == State::Starting || state_ == State::Running)
            state_ = State::Idle;
    }

    void abandonPlane() noexcept override
    {
        keepPlane_ = true;
        stopHelper (0.0);
        keepPlane_ = false;
        if (state_ == State::Starting || state_ == State::Running)
            state_ = State::Idle;
        retryAt_ = 0.0;
    }

    GpuStatus status() const override
    {
        GpuStatus s;
        s.active = active();
        s.state = forceCpu_ ? "off" : state_ == State::Idle ? "idle" : state_ == State::Starting ? "starting" : state_ == State::Running ? "running" : state_ == State::Backoff ? "backoff" : "exhausted";
        s.pid = (state_ == State::Starting || state_ == State::Running) ? tr_.pid() : 0;
        s.starts = starts_;
        s.restarts = restarts_;
        s.failures = totalFailures_;
        s.lastError = lastError_;
        s.frames = frames_;
        s.helperRenderMs = renderMs_;
        s.helperSetPlaneMs = setPlaneMs_;
        s.skippedVblanks = skipped_;
        s.lateSetPlanes = late_;
        s.glRenderer = glRenderer_;
        s.startupMs = startupMs_;
        s.pace = pace_;
        s.render = render_;
        s.paceKnown = paceKnown_;
        return s;
    }

private:
    enum class State { Idle, Starting, Running, Backoff, Exhausted };

    void startHelper (double now)
    {
        const int fd = plane_.displayFd();
        std::string error;
        const PaceParams pp = plane_.paceParams();
        HelloParams hello;
        hello.crtc = plane_.crtcId();
        hello.plane = plane_.planeId();
        hello.flags = cfg_.flags | (pp.vblank ? kFlagVblank : 0u);
        hello.pipe = static_cast<uint32_t> (pp.pipe);
        hello.stride = static_cast<uint32_t> (pp.stride);
        hello.periodUs = static_cast<uint32_t> (pp.periodMs * 1000.0 + 0.5);
        if (fd < 0)
        {
            lastError_ = "no display descriptor for the helper";
            ++totalFailures_;
            scheduleAfterFailure (now);
            return;
        }
        plane_.takeStacking();   // before the helper's first SETPLANE: our plane goes above the host's picture and below its pointer
        lastRefreshes_ = 0;
        if (! tr_.start (fd, hello, error))
        {
            lastError_ = "cannot start the helper: " + error;
            ++totalFailures_;
            tr_.close();
            scheduleAfterFailure (now);
            return;
        }
        ++starts_;
        state_ = State::Starting;
        startedAt_ = now;
        shown_ = hiding_ = unacked_ = false;
        lastSeq_ = acked_ = 0;
        lastHelperFrames_ = 0;
    }

    bool handle (const Msg& m, double now)
    {
        switch (m.type)
        {
            case kMsgReady:
                if (state_ == State::Starting)
                {
                    state_ = State::Running;
                    runningSince_ = now;
                    stackingCheckedAt_ = now;
                    startupMs_ = m.a;
                    glRenderer_.assign (m.text, strnlen (m.text, sizeof m.text));
                    if (wantStride_ > 0)
                        tr_.send (makeMsg (kMsgStride, static_cast<uint32_t> (wantStride_)));   // a restarted helper paces the way the plugin chose
                }
                return true;
            case kMsgShown:
                return true;
            case kMsgFrame:
                frames_ += (m.a >= lastHelperFrames_ ? m.a - lastHelperFrames_ : m.a);   // cumulative over restarts
                lastHelperFrames_ = m.a;
                acked_ = m.b;
                renderMs_ = m.c / 1000.0;
                setPlaneMs_ = m.d / 1000.0;
                skipped_ = m.e;
                late_ = m.f;
                if (unacked_)
                {
                    if (static_cast<int32_t> (lastSeq_ - acked_) <= 0)
                        unacked_ = false;
                    else
                        unackedSince_ = now;   // it made progress, a newer scene is waiting
                }
                return true;
            case kMsgHidden:
                hiding_ = false;
                return true;
            case kMsgError:
            {
                const std::string text (m.text, strnlen (m.text, sizeof m.text));
                if (text.compare (0, 8, "SETPLANE") == 0)
                {
                    // the plane may have been taken by another program: the next free one is used for the restart (the failure still counts)
                    std::string why;
                    tr_.kill();
                    cleanDisplay (false);
                    plane_.nextPlane (why);
                    lastError_ = "the helper reports: " + text + "; " + why;
                    ++totalFailures_;
                    tr_.close();
                    shown_ = hiding_ = unacked_ = false;
                    scheduleAfterFailure (now);
                    return false;
                }
                fail (now, "the helper reports: " + text);
                return false;
            }
            default:
                fail (now, "unknown message from the helper");
                return false;
        }
    }

    void readPace (const SharedBlock* b) noexcept
    {
        PaceBlock p;
        p.count = __atomic_load_n (&b->pace.count, __ATOMIC_RELAXED);
        p.stride = __atomic_load_n (&b->pace.stride, __ATOMIC_RELAXED);
        p.periodUs = __atomic_load_n (&b->pace.periodUs, __ATOMIC_RELAXED);
        p.onGrid = __atomic_load_n (&b->pace.onGrid, __ATOMIC_RELAXED);
        for (int i = 0; i < 10; ++i)
            p.vb[i] = __atomic_load_n (&b->pace.vb[i], __ATOMIC_RELAXED);
        for (int i = 0; i < 64; ++i)
            p.ms[i] = __atomic_load_n (&b->pace.ms[i], __ATOMIC_RELAXED);
        p.planeChecks = __atomic_load_n (&b->pace.planeChecks, __ATOMIC_RELAXED);
        p.planeRefreshes = __atomic_load_n (&b->pace.planeRefreshes, __ATOMIC_RELAXED);
        pace_ = p;
        paceKnown_ = p.stride != 0;
        RenderBlock r;
        uint32_t* dst = &r.count;
        const uint32_t* src = &b->render.count;
        for (size_t i = 0; i < sizeof (RenderBlock) / sizeof (uint32_t); ++i)
            dst[i] = __atomic_load_n (src + i, __ATOMIC_RELAXED);
        render_ = r;
    }

    /** Kills the helper (when there is one), cleans what it may have left on the display, closes the channel. */
    void stopHelper (double now)
    {
        (void) now;
        if (state_ == State::Starting || state_ == State::Running)
        {
            if (state_ == State::Running && tr_.alive())
            {
                tr_.send (makeMsg (kMsgQuit));
                if (! tr_.waitExit (cfg_.quitWaitMs))
                    tr_.kill();
            }
            else
                tr_.kill();
        }
        cleanDisplay (! keepPlane_);
        tr_.close();
        shown_ = hiding_ = unacked_ = false;
    }

    void cleanDisplay (bool planeOff = true) noexcept
    {
        // the plane is taken off only when the helper may have put something on it (it was told to show, or it has objects in the ledger): a helper that failed before that (a missing
        // library, a protocol version of another build) leaves the plane to the CPU picture that may be on it
        bool touched = shown_ || hiding_;
        if (SharedBlock* b = tr_.block())
            for (int i = 0; i < kMaxLedger; ++i)
            {
                uint32_t handle, fb;
                ledgerGet (b, i, handle, fb);
                touched = touched || handle || fb;
            }
        if (touched && planeOff)
            plane_.forceDisablePlane();
        if (planeOff)
            plane_.releaseStacking();   // the zpos the plane had at rest is put back (the helper is dead or gone: nothing of it shows on the plane any more)
        if (SharedBlock* b = tr_.block())
            for (int i = 0; i < kMaxLedger; ++i)
            {
                uint32_t handle, fb;
                ledgerGet (b, i, handle, fb);
                if (handle || fb)
                {
                    plane_.removeForeign (fb, handle);
                    ledgerSet (b, i, 0, 0);
                }
            }
    }

    void fail (double now, const std::string& why)
    {
        lastError_ = why;
        ++totalFailures_;
        tr_.kill();     // first: nothing of the helper may touch the plane any more
        cleanDisplay (true);
        tr_.close();
        shown_ = hiding_ = unacked_ = false;
        scheduleAfterFailure (now);
    }

    void scheduleAfterFailure (double now)
    {
        ++failures_;
        if (failures_ > cfg_.maxRestarts)
        {
            state_ = State::Exhausted;
            return;
        }
        double wait = cfg_.backoffMs;
        for (int i = 1; i < failures_; ++i)
            wait *= 2.0;
        state_ = State::Backoff;
        retryAt_ = now + wait;
    }

    static size_t strnlen (const char* s, size_t n) noexcept
    {
        size_t i = 0;
        while (i < n && s[i])
            ++i;
        return i;
    }

    IHelperTransport& tr_;
    IPlaneBackend& plane_;
    GpuConfig cfg_;
    State state_ = State::Idle;
    uint32_t lastRefreshes_ = 0;
    static constexpr double kStackingCheckMs = 2000.0;
    double stackingCheckedAt_ = 0.0;
    bool forceCpu_ = false, shown_ = false, hiding_ = false, unacked_ = false, keepPlane_ = false, paceKnown_ = false;
    PaceBlock pace_ {};
    RenderBlock render_ {};
    int wantStride_ = 0;   // 0: the hello's
    double startedAt_ = 0.0, runningSince_ = 0.0, retryAt_ = 0.0, unackedSince_ = 0.0, hideAt_ = 0.0;
    uint32_t lastSeq_ = 0, acked_ = 0;
    int failures_ = 0, rotation_ = 90, viewIndex_ = -1;
    unsigned starts_ = 0, restarts_ = 0, totalFailures_ = 0;
    unsigned long frames_ = 0, skipped_ = 0, late_ = 0, lastHelperFrames_ = 0;
    double renderMs_ = 0.0, setPlaneMs_ = 0.0, startupMs_ = 0.0;
    std::string lastError_, glRenderer_;
    Rect ui_, panel_;
};

}}} // namespace synthyboy_mpc::overlay::gpu
