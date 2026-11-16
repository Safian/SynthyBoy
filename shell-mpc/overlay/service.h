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
#include "../device_profile.h"
#include "../output_peak.h"
#include "../play_log.h"
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
/** The helper executable next to the plugin binary: <folder of the .so>/synthyboy-overlay-gpu-<arch> (the package puts it into VST/ beside synthyboy-<arch>.so). Empty when the plugin's path is unknown. */
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
    return dir + "/synthyboy-overlay-gpu-arm64";
#else
    return dir + "/synthyboy-overlay-gpu-arm32";
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

    /** Returns the slot of the instance (0 .. 15, kept until detach; -1: more than 16 instances, this one never shows the overlay). */
    int attach (IInstanceView* view)
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        ensureController();
        const int slot = controller_->attach (view);
        synthyboy_mpc::PlayLog::line ("Service::attach slot %d, thread joinable %d", slot, thread_.joinable() ? 1 : 0);
        if (! thread_.joinable())
        {
            stop_.store (false);
            thread_ = std::thread ([this] { run(); });
            synthyboy_mpc::PlayLog::line ("Service::attach started the overlay thread");
        }
        if (! atexitRegistered_)
        {
            atexitRegistered_ = true;
            std::atexit ([] { Service::instance().shutdownAtExit(); });
        }
        return slot;
    }

    void detach (IInstanceView* view)
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (! controller_)
            return;
        const size_t left = controller_->detach (view);
        synthyboy_mpc::PlayLog::line ("Service::detach, %zu instances left", left);
        if (left == 0)
            stopThread();
    }

private:
    Service() = default;
    const std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();

    void ensureController()
    {
        if (controller_)
            return;
        ui::QGroup::setWideDevice (detectSixteenQLinks());   // the idle chips of the other mapped groups: knob numbers 5 .. 16 on the XL, 1 .. 4 (bank-relative) on the others
#if defined(__linux__)
        display_ = std::make_shared<LinuxDisplay>();     // found at run time by the first use (discovery.h): the CRTC, the planes, the panel, the refresh
        backend_ = std::make_unique<LinuxPlaneBackend> (display_);
        frames_ = std::make_unique<LinuxFrameSource> (display_);
#else
        backend_ = std::make_unique<UnsupportedPlaneBackend>();
        frames_ = std::make_unique<UnsupportedFrameSource>();
#endif
        renderer_ = std::make_unique<CpuCurveRenderer>();
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
        controller_ = std::make_unique<Controller> (*backend_, *frames_, *renderer_, Paths(), Timing(), -1.0, gpu_.get(), touch_.get());
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
            thread_.join();
        controller_->shutdown();
    }

    void shutdownAtExit()
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (controller_ && thread_.joinable())
            stopThread();
        else if (controller_)
            controller_->shutdown();
    }

    void run()
    {
        useNormalScheduling (0, "sb-overlay");
        synthyboy_mpc::PlayLog::line ("Service::run entered");
#if defined(__linux__)
        applyCpus (true);
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
        std::unique_lock<std::mutex> lk (wakeMu_);
        while (! stop_.load())
        {
            lk.unlock();
            const double now = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count() + 1.0;
#if defined(__linux__)
            if (nowSteadyMs() >= nextCpuScanMs_)
                applyCpus (false);
#endif
            const auto tickStart = std::chrono::steady_clock::now();
            const double wait = controller_->tick (now);
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

    std::string cpuStatus()
    {
        std::lock_guard<std::mutex> g (cpuMu_);
        std::string t = "cpus_online: " + formatCpuList (cpu_.online) + "\ncpus_isolated_by_kernel: " + formatCpuList (cpu_.isolated) + "\ncpus_overlay_allowed: " + formatCpuList (cpu_.plan.allowed)
                        + "\ncpus_avoided: " + formatCpuList (cpu_.plan.avoided) + "\ncpus_overlay_thread_now: " + threadAffinity_ + "\ncpus_helper_now: " + helperAffinity_ + "\ncpus_audio_threads:";
        for (const std::string& n : cpu_.plan.audioThreads)
            t += " " + n;
        char pk[160];
        std::snprintf (pk, sizeof pk, "\noutput_peak: %.5f\noutput_peak_total: %.5f\noutput_blocks: %lu\n", static_cast<double> (outputPeak().takeWindow()), static_cast<double> (outputPeak().total.load (std::memory_order_relaxed)), outputPeak().blocks.load (std::memory_order_relaxed));   // output_peak.h: what the plugin hands to the host
        return t + (cpu_.plan.audioThreads.empty() ? " none" : "") + pk + "quality_profile: " + describeProfile (profileInForce().profile) + " (" + profileInForce().source + ")\n";   // device_profile.h: the engine quality of this process
    }

    std::mutex cpuMu_;
    CpuState cpu_;
    double nextCpuScanMs_ = 0.0;
    int helperPidSet_ = 0;
    std::string threadAffinity_ = "unknown", helperAffinity_ = "no helper";
#endif

    std::mutex lifeMu_, wakeMu_;
    std::condition_variable wake_;
    std::atomic<bool> stop_ { false };
    std::thread thread_;
    bool atexitRegistered_ = false;
#if defined(__linux__)
    std::shared_ptr<LinuxDisplay> display_;
#endif
    std::unique_ptr<IPlaneBackend> backend_;
    std::unique_ptr<IFrameSource> frames_;
    std::unique_ptr<ICurveRenderer> renderer_;
    std::unique_ptr<gpu::IHelperTransport> transport_;
    std::unique_ptr<gpu::IGpuOverlay> gpu_;
    std::unique_ptr<ITouchInput> touch_;
    std::unique_ptr<IMouseInput> mouse_;
    std::unique_ptr<IPointerSource> pointer_;
    std::unique_ptr<Controller> controller_;
};

}} // namespace synthyboy_mpc::overlay
