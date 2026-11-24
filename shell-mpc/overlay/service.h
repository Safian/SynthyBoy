// SynthyBoy - MPC overlay: the process wide service. One overlay thread, one controller, for however many plugin instances live in the MPC app. The plugin shell calls
// attach () when an instance is created and detach () when it is closed (effClose); nothing else of the overlay is visible to it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The thread starts with the first instance and stops with the last one (the plane is off and the buffers are gone when detach () returns). An atexit handler does the same when the
// process ends while an instance is still open. The thread runs with the same scheduling as the display thread (thread_policy.h: SCHED_OTHER, nice 0) on the cores the discovery
// leaves to it: never the isolated cores, never the cores the application's audio threads are pinned to (cpu_plan_linux.h; re-read every 10 seconds, the helper follows).
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <thread>

#if defined(__linux__)
#include <dlfcn.h>
#include <sched.h>
#endif
#include "../kit/branding.h"
#include "../kit/page_set.h"
#include "../output_peak.h"
#include "../play_log.h"
#include "../startup_timing.h"
#include "../poll_guard.h"
#include "../thread_policy.h"
#include "controller.h"
#include "cpu_plan_linux.h"
#include "drm_linux.h"
#include "gpu/gpu_overlay.h"
#include "q_device.h"
#include "../ui/qgroup.h"
#include "mouse_linux.h"
#include "touch_linux.h"
#if defined(__linux__) && defined(SYNTHYBOY_MPC_OVERLAY_GPU)
#include "gpu/helper_transport_linux.h"
#endif

namespace synthyboy_mpc { namespace overlay {

#if !defined(__linux__)
/** Other systems (the Mac build of the shell for the tests): no display to drive; the module disables itself with this reason. */
class UnsupportedPlaneBackend final : public IPlaneBackend
{
public:
    bool open (int&, int&, std::string& error) override { error = "no DRM on this system"; return false; }
    bool createBuffers (int, int, std::string& error) override { error = "unsupported"; return false; }
    uint32_t* buffer (int, size_t&) noexcept override { return nullptr; }
    bool setPlane (int, const Rect&, std::string& error) override { error = "unsupported"; return false; }
    void disablePlane() noexcept override {}
    void destroyBuffers() noexcept override {}
};
class UnsupportedFrameSource final : public IFrameSource
{
public:
    bool begin (int&, int&) override { return false; }
    bool pixel (int, int, uint32_t&) noexcept override { return false; }
    void end() noexcept override {}
};
#endif

#if defined(__linux__) && defined(SYNTHYBOY_MPC_OVERLAY_GPU)
/** The helper executable next to the plugin binary: <folder of the .so>/<helper name>-<arch> (the package puts it into VST/ beside the plugin binary; SynthyBoy: synthyboy-overlay-gpu-arm64 / -arm32, the name
    is mpckit::Branding::helperName). Empty when the plugin's path is unknown. */
inline std::string gpuHelperPath()
{
    Dl_info info;
    if (! dladdr (reinterpret_cast<const void*> (&gpuHelperPath), &info) || ! info.dli_fname)
        return {};
    char real[4096];
    const char* so = realpath (info.dli_fname, real) ? real : info.dli_fname;
    std::string dir (so);
    const size_t slash = dir.rfind ('/');
    if (slash == std::string::npos)
        return {};
    dir.resize (slash);
#if defined(__aarch64__)
    return dir + "/" + mpckit::branding().helperName + "-arm64";
#else
    return dir + "/" + mpckit::branding().helperName + "-arm32";
#endif
}
#endif

class Service
{
public:
    static Service& instance()
    {
        static Service* s = new Service();   // lives to the end of the process on purpose: the exit handler and the thread may need it last
        return *s;
    }

#if defined(__linux__)
    /** The CPU rule of the overlay for ANOTHER thread of the shell that renders or polls (the display thread; the engine helper threads through the hook of helper_thread_hook.h, once that is merged: its hook calls
        this too): the calling thread gets the cores of the plan (coresForOwnThreads). `seen` is the plan the thread applied last (start with 0): the call does nothing while the plan is the same, and while the overlay
        thread has not made its first plan (false: ask again later), so it is cheap enough to call on every pass of a loop. True: the cores were set now. */
    static bool applyCpusToThisThread (unsigned& seen)
    {
        Service& s = instance();
        const unsigned gen = s.cpuGen_.load (std::memory_order_acquire);
        if (gen == 0 || gen == seen)
            return false;
        std::vector<int> cpus;
        {
            std::lock_guard<std::mutex> g (s.cpuMu_);
            cpus = coresForOwnThreads (s.cpu_.plan, s.cpu_.online);
        }
        seen = gen;
        return setAffinity (0, cpus);
    }
#endif

    /** Returns the slot of the instance (0 .. 15, kept until detach; -1: more than 16 instances, this one never shows the overlay, or the plugin's pages are refused). `pages` and `factory` (kit/page_set.h) are the plugin's
        tabs and the maker of their views, `branding` (kit/branding.h) the names of its files (null: SynthyBoy's): the FIRST attach of the process decides, a later attach of the same library finds the controller built and
        its own arguments ignored (there is one overlay thread and one plane). A page set that fails mpckit::validatePages is refused (the reason is in the play log). The objects must outlive the overlay. */
    int attach (mpckit::IInstance* view, const mpckit::IPageSet* pages, mpckit::IPageFactory* factory, const mpckit::Branding* branding = nullptr)
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (stuck_)
            return -1;   // the overlay thread of an earlier run never ended (it is stuck in a call that does not return): no second thread on the same controller, this instance shows no overlay
        if (! controller_)
        {
            if (pages == nullptr || factory == nullptr)
                return -1;
            std::string why;
            if (! mpckit::validatePages (*pages, why))
            {
                synthyboy_mpc::PlayLog::line ("Service::attach: the page set is not valid: %s", why.c_str());
                refused_ = true;
                return -1;
            }
            if (branding != nullptr)
                mpckit::setBranding (*branding);
            pages_ = pages;
            factory_ = factory;
        }
        if (refused_)
            return -1;   // the page set was refused: no overlay for this library
        ensureController();
        const int slot = controller_->attach (view);
        synthyboy_mpc::PlayLog::line ("Service::attach slot %d, thread joinable %d", slot, thread_.joinable() ? 1 : 0);
        if (! thread_.joinable())
        {
            stop_.store (false);
            runDone_ = false;
            thread_ = std::thread ([this]
            {
                try { run(); }
                catch (...) { PollStatus::report ("overlay thread", this, 1, true, "an exception reached the top of the thread"); }
                {
                    std::lock_guard<std::mutex> g (wakeMu_);
                    runDone_ = true;
                }
                wake_.notify_all();
            });
            synthyboy_mpc::PlayLog::line ("Service::attach started the overlay thread");
        }
        if (! atexitRegistered_)
        {
            atexitRegistered_ = true;
            std::atexit ([] { Service::instance().shutdownAtExit(); });
        }
        return slot;
    }

    /** True when the instance is detached: the overlay thread no longer touches `view`. False when the overlay thread did not let go of the controller's lock within kDetachWaitMs (a tick that does not return): the view
        may still be used, the caller must not free the instance. effClose of the host is held up for at most that long (robustness review F5). */
    bool detach (mpckit::IInstance* view)
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (! controller_)
            return true;
        size_t left = 0;
        if (! controller_->detachWithin (view, kDetachWaitMs, left))
        {
            synthyboy_mpc::PlayLog::line ("Service::detach: the overlay thread did not let go in %d ms", kDetachWaitMs);
            return false;
        }
        synthyboy_mpc::PlayLog::line ("Service::detach, %zu instances left", left);
        if (left == 0)
            stopThread();
        return true;
    }

    /** The plugin's own lines of the status file ("key: value\n" each; SynthyBoy: its quality profile, device_profile.h): called by the overlay thread once a second, from the Linux status text. Set it before attach. */
    void setStatus (std::function<std::string()> f)
    {
        std::lock_guard<std::mutex> g (statusMu_);
        statusHook_ = std::move (f);
    }

private:
    static constexpr int kDetachWaitMs = 2000;   // detach: the controller's lock
    static constexpr int kStopWaitMs = 2500;     // stopThread: the overlay thread to end (a vblank wait is bounded by the kernel at 3 s, the helper's kill at 2 s)
    Service() = default;
    const std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();

    /** The files the controller looks at in the shipped plugin. The switches that are features (overlay-off, nogpu, rot, look, detail, fps, gpuclock) and the status file stay; the debug ones (the pointer hot spot, the
        curve dump request) are empty without SYNTHYBOY_MPC_DEBUG_HOOKS, so the plugin does not look at them. */
    /** The shipped timing: the GPU helper is started as soon as an instance is attached (the overlay thread does it, in parallel with the host's own loading), not when the first page is found on screen. */
    static Timing shippedTiming (const std::string& deviceModel)
    {
        Timing t;
        t.prewarmGpu = true;
        t.nativeTabTouch = deviceModel == "Akai Professional MPC XL";
        return t;
    }
    static Paths shippedPaths()
    {
        Paths p = Paths::forBranding (mpckit::branding());
#if defined(SYNTHYBOY_MPC_DEBUG_HOOKS) && SYNTHYBOY_MPC_DEBUG_HOOKS
#else
        p.pointerHotSpot.clear();
        p.dumpRequest.clear();
        p.dump.clear();
#endif
        return p;
    }

    void ensureController()
    {
        if (controller_)
            return;
        const std::string deviceModel = readDeviceModel();
        ui::QGroup::setWideDevice (detectSixteenQLinks (deviceModel));   // the idle chips of the other mapped groups: knob numbers 5 .. 16 on the XL, 1 .. 4 (bank-relative) on the others
        // Native XL controls already expose the current parameter name. Keep
        // the overlay's yellow slot chips out of the normal picture; a hooks
        // build may opt into them for visual diagnostics only.
        bool wideBadges = false;
#if defined(SYNTHYBOY_MPC_DEBUG_HOOKS) && SYNTHYBOY_MPC_DEBUG_HOOKS
        if (const char* e = std::getenv ("SYNTHYBOY_MPC_Q_BADGES"))
            wideBadges = std::atoi (e) != 0;
#endif
        ui::QGroup::setWideBadges (wideBadges);
#if defined(__linux__)
        display_ = std::make_shared<LinuxDisplay>();     // found at run time by the first use (discovery.h): the CRTC, the planes, the panel, the refresh
        backend_ = std::make_unique<LinuxPlaneBackend> (display_);
        frames_ = std::make_unique<LinuxFrameSource> (display_);
#else
        backend_ = std::make_unique<UnsupportedPlaneBackend>();
        frames_ = std::make_unique<UnsupportedFrameSource>();
#endif
#if defined(__linux__) && defined(SYNTHYBOY_MPC_OVERLAY_GPU)
        {
            gpu::GpuConfig cfg;
            transport_ = std::make_unique<gpu::LinuxHelperTransport> (gpuHelperPath());
            gpu_ = std::make_unique<gpu::GpuHelperBackend> (*transport_, *backend_, cfg);
        }
#endif
#if defined(__linux__)
        touch_ = std::make_unique<TouchInput>();   // the evdev node is found at run time (touch_linux.h); read only, never grabbed
        mouse_ = std::make_unique<LinuxMouseReader>();           // the mice: found at run time, hot plug, read only, never grabbed (mouse_linux.h)
        pointer_ = std::make_unique<LinuxPointerSource> (display_);   // the host's pointer plane: where the pointer is (drm_linux.h)
#endif
        controller_ = std::make_unique<Controller> (*backend_, *frames_, shippedPaths(), shippedTiming (deviceModel), gpu_.get(), touch_.get(), *pages_, *factory_, true);   // true: the views are built by the overlay thread's first tick, not here (the host's thread)
#if defined(__linux__)
        controller_->setMouse (mouse_.get(), pointer_.get());
        controller_->setExtraStatus ([this] { return cpuStatus(); });
#endif
    }

    void stopThread()
    {
        {
            std::lock_guard<std::mutex> g (wakeMu_);
            stop_.store (true);
        }
        wake_.notify_all();
        if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
        {
            std::unique_lock<std::mutex> lk (wakeMu_);
            const bool ended = wake_.wait_for (lk, std::chrono::milliseconds (kStopWaitMs), [this] { return runDone_; });
            lk.unlock();
            if (! ended)
            {
                // The thread does not come back (a call into the display or the helper that does not return). It is left running (detached): it holds nothing but the service, which lives for the whole process.
                // The controller is not shut down from here (that would wait for the lock the thread holds) and no second overlay thread is started on it.
                thread_.detach();
                stuck_ = true;
                PollStatus::report ("overlay thread", this, 1, true, "did not end within the limit when the last instance closed; left running");
                return;
            }
            thread_.join();
        }
        controller_->shutdown();
    }

    void shutdownAtExit()
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (stuck_)
            return;   // the overlay thread is stuck inside the controller: nothing is touched from the exit handler either
        if (controller_ && thread_.joinable())
            stopThread();
        else if (controller_)
            controller_->shutdown();
    }

    void run()
    {
        useNormalScheduling (0, "sb-overlay");
        startupTimes().markUiStep (StartupTimes::kUiThreadStart);
        synthyboy_mpc::PlayLog::line ("Service::run entered");
#if defined(__linux__)
        {
            PollGuard first ("CPU plan (first)", this);
            first.run ([&] { applyCpus (true); });
        }
#endif
        controller_->setCpuClock ([]
        {
            timespec ts;
            clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
            return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
        });
        // The clock the controller's deadlines are kept against must not start again at zero when the thread is started again (the last instance closed and a new one came: the track's
        // instrument was replaced, another project was loaded): the controller still holds the deadlines of the earlier run, which would lie minutes in the future of a new clock, and nothing
        // would be detected, shown or written until the new clock caught up. The epoch is that of the service, set once.
        const auto start = epoch_;
        PollGuard gTick ("overlay tick", this), gCpu ("CPU plan", this);
        std::unique_lock<std::mutex> lk (wakeMu_);
        while (! stop_.load())
        {
            lk.unlock();
            const double now = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count() + 1.0;
#if defined(__linux__)
            if (nowSteadyMs() >= nextCpuScanMs_)
            {
                gCpu.run ([&] { applyCpus (false); });   // allocates (strings, vectors): a failure is counted, five in a row switch the CPU plan off
                if (gCpu.streak > 0)
                    nextCpuScanMs_ = nowSteadyMs() + 10000.0;
            }
#endif
            const auto tickStart = std::chrono::steady_clock::now();
            double wait = 1000.0;
            gTick.run ([&] { wait = controller_->tick (now); });
            if (! startupTimes().uiReached())
            {
                if (gpu_ && gpu_->active())
                    startupTimes().markUiStep (StartupTimes::kUiHelperReady);
                if (controller_->framesTotal() > 0)
                    startupTimes().markUi();
            }   // the first frame of our UI that was shown (a relaxed load once it is set)   // Controller::tick catches everything itself; this is the second barrier (and the count)
            if (gTick.off)
            {
                lk.lock();
                break;   // the overlay is off for good: the thread ends, the plane is taken down by the last detach
            }
            if (wait > 1500.0 || wait < 0.0) synthyboy_mpc::PlayLog::line ("Service::run: tick asks for a wait of %.1f ms", wait);
            lk.lock();
            if (stop_.load())
                break;
            // the next step is due `wait` after the START of this one (a step that had to wait for the display's locks does not push the rhythm of the scenes back)
            wake_.wait_until (lk, tickStart + std::chrono::microseconds (static_cast<long long> (wait * 1000.0)), [this] { return stop_.load(); });
        }
    }

#if defined(__linux__)
    /** The cores for this thread (and the helper): every online core but the isolated ones and the ones the audio threads are pinned to. Re-read every 10 seconds (threads of the
        application may start late); the helper, which inherited the cores at its start, is set again when they change. */
    void applyCpus (bool first)
    {
        CpuState st = planOwnCpus();
        std::lock_guard<std::mutex> g (cpuMu_);
        nextCpuScanMs_ = nowSteadyMs() + 10000.0;
        const bool changed = first || st.plan.allowed != cpu_.plan.allowed;
        cpu_ = st;
        if (changed)
            cpuGen_.fetch_add (1, std::memory_order_release);   // the other threads of the shell (applyCpusToThisThread) follow
        if (st.plan.changed && changed)
            setAffinity (0, st.plan.allowed);
        if (gpu_ && st.plan.changed)
        {
            const int pid = gpu_->status().pid;
            if (pid > 0 && pid != helperPidSet_)
            {
                setAffinity (pid, st.plan.allowed);
                helperPidSet_ = pid;
            }
            else if (pid > 0 && changed)
                setAffinity (pid, st.plan.allowed);
        }
        threadAffinity_ = affinityOf (0);
        helperAffinity_ = gpu_ && gpu_->status().pid > 0 ? affinityOf (gpu_->status().pid) : "no helper";
    }

    static double nowSteadyMs()
    {
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    /** The lines the plugin adds to the status file (setStatus): SynthyBoy's quality profile of the device. Called from the overlay thread once a second. */
    std::string pluginStatus()
    {
        std::function<std::string()> f;
        {
            std::lock_guard<std::mutex> g (statusMu_);
            f = statusHook_;
        }
        return f ? f() : std::string();
    }

    std::string cpuStatus()
    {
        std::lock_guard<std::mutex> g (cpuMu_);
        std::string t = "cpus_online: " + formatCpuList (cpu_.online) + "\ncpus_isolated_by_kernel: " + formatCpuList (cpu_.isolated) + "\ncpus_overlay_allowed: " + formatCpuList (cpu_.plan.allowed)
                        + "\ncpus_avoided: " + formatCpuList (cpu_.plan.avoided) + "\ncpus_overlay_thread_now: " + threadAffinity_ + "\ncpus_helper_now: " + helperAffinity_ + "\ncpus_audio_threads:";
        for (const std::string& n : cpu_.plan.audioThreads)
            t += " " + n;
        char pk[160];
        std::snprintf (pk, sizeof pk, "\noutput_peak: %.5f\noutput_peak_total: %.5f\noutput_blocks: %lu\n", static_cast<double> (outputPeak().takeWindow()), static_cast<double> (outputPeak().total.load (std::memory_order_relaxed)), outputPeak().blocks.load (std::memory_order_relaxed));   // output_peak.h: what the plugin hands to the host
        return t + (cpu_.plan.audioThreads.empty() ? " none" : "") + pk + pluginStatus() + startupTimes().describe();   // startup_timing.h: open_ms_* and host_call_max_ms_*   // device_profile.h: the engine quality of this process
    }

    std::mutex cpuMu_;
    CpuState cpu_;
    std::atomic<unsigned> cpuGen_ { 0 };   // counts the plans that changed the cores (0: none made yet)
    double nextCpuScanMs_ = 0.0;
    int helperPidSet_ = 0;
    std::string threadAffinity_ = "unknown", helperAffinity_ = "no helper";
#endif

    std::mutex lifeMu_, wakeMu_;
    std::condition_variable wake_;
    std::atomic<bool> stop_ { false };
    bool runDone_ = false;   // under wakeMu_: run () has returned
    bool stuck_ = false;     // under lifeMu_: a thread was given up on; the overlay stays off for the rest of the process
    std::thread thread_;
    bool atexitRegistered_ = false;
#if defined(__linux__)
    std::shared_ptr<LinuxDisplay> display_;
#endif
    std::unique_ptr<IPlaneBackend> backend_;
    std::unique_ptr<IFrameSource> frames_;
    std::unique_ptr<gpu::IHelperTransport> transport_;
    std::unique_ptr<gpu::IGpuOverlay> gpu_;
    std::unique_ptr<ITouchInput> touch_;
    std::unique_ptr<IMouseInput> mouse_;
    std::unique_ptr<IPointerSource> pointer_;
    std::unique_ptr<Controller> controller_;
    bool refused_ = false;                            // under lifeMu_: the first page set did not pass validatePages
    const mpckit::IPageSet* pages_ = nullptr;         // the tabs of the plugin (the first attach decides)
    mpckit::IPageFactory* factory_ = nullptr;         // the maker of their views (the plugin's: SynthyBoy's is SynthyBoyPageFactory, pages.h)
    std::mutex statusMu_;
    std::function<std::string()> statusHook_;
};

}} // namespace synthyboy_mpc::overlay
