// SynthyBoy - MPC overlay: the process wide service. One overlay thread, one controller, for however many plugin instances live in the MPC app. The plugin shell calls
// attach () when an instance is created and detach () when it is closed (effClose); nothing else of the overlay is visible to it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The thread starts with the first instance and stops with the last one (the plane is off and the buffers are gone when detach () returns). An atexit handler does the same when the
// process ends while an instance is still open. The thread runs with the same scheduling as the display thread (thread_policy.h: SCHED_OTHER, nice 0, cores 0-1 by the cpuset
// of the system; never the isolated audio cores).
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
#include <sched.h>
#endif
#include "../thread_policy.h"
#include "controller.h"
#include "drm_linux.h"

#ifndef SYNTHYBOY_MPC_OVERLAY_CRTC
#define SYNTHYBOY_MPC_OVERLAY_CRTC 37
#endif
#ifndef SYNTHYBOY_MPC_OVERLAY_PLANE
#define SYNTHYBOY_MPC_OVERLAY_PLANE 38
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

#if defined(__linux__)
/** useNormalScheduling gives the thread every CPU of the system. The cores the kernel isolates for the audio threads (isolcpus, /sys/devices/system/cpu/isolated, "2-3" on the
    Key 61) are taken out again: this thread never runs there. Best effort; nothing is changed when the file is missing or would leave no CPU. */
inline void keepOffIsolatedCpus() noexcept
{
    FILE* f = std::fopen ("/sys/devices/system/cpu/isolated", "r");
    if (! f)
        return;
    char text[128] = {};
    const size_t n = std::fread (text, 1, sizeof text - 1, f);
    std::fclose (f);
    text[n] = 0;
    cpu_set_t set;
    if (sched_getaffinity (0, sizeof set, &set) != 0)
        return;
    cpu_set_t out = set;
    for (const char* p = text; *p && *p != '\n';)
    {
        char* end = nullptr;
        const long a = std::strtol (p, &end, 10);
        if (end == p)
            break;
        long b = a;
        if (*end == '-')
        {
            p = end + 1;
            b = std::strtol (p, &end, 10);
        }
        for (long c = a; c <= b && c < CPU_SETSIZE; ++c)
            CPU_CLR (static_cast<int> (c), &out);
        p = *end == ',' ? end + 1 : end;
        if (*end != ',')
            break;
    }
    if (CPU_COUNT (&out) > 0 && ! CPU_EQUAL (&out, &set))
        (void) sched_setaffinity (0, sizeof out, &out);
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

    void attach (IInstanceView* view)
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        ensureController();
        controller_->attach (view);
        if (! thread_.joinable())
        {
            stop_.store (false);
            thread_ = std::thread ([this] { run(); });
        }
        if (! atexitRegistered_)
        {
            atexitRegistered_ = true;
            std::atexit ([] { Service::instance().shutdownAtExit(); });
        }
    }

    void detach (IInstanceView* view)
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (! controller_)
            return;
        if (controller_->detach (view) == 0)
            stopThread();
    }

private:
    Service() = default;

    void ensureController()
    {
        if (controller_)
            return;
#if defined(__linux__)
        backend_ = std::make_unique<LinuxPlaneBackend> (SYNTHYBOY_MPC_OVERLAY_CRTC, SYNTHYBOY_MPC_OVERLAY_PLANE);
        frames_ = std::make_unique<LinuxFrameSource> (SYNTHYBOY_MPC_OVERLAY_CRTC);
#else
        backend_ = std::make_unique<UnsupportedPlaneBackend>();
        frames_ = std::make_unique<UnsupportedFrameSource>();
#endif
        renderer_ = std::make_unique<CpuCurveRenderer>();
        controller_ = std::make_unique<Controller> (*backend_, *frames_, *renderer_, Paths());
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
#if defined(__linux__)
        keepOffIsolatedCpus();
#endif
        controller_->setCpuClock ([]
        {
            timespec ts;
            clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
            return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
        });
        const auto start = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lk (wakeMu_);
        while (! stop_.load())
        {
            lk.unlock();
            const double now = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count() + 1.0;
            const double wait = controller_->tick (now);
            lk.lock();
            if (stop_.load())
                break;
            wake_.wait_for (lk, std::chrono::microseconds (static_cast<long long> (wait * 1000.0)), [this] { return stop_.load(); });
        }
    }

    std::mutex lifeMu_, wakeMu_;
    std::condition_variable wake_;
    std::atomic<bool> stop_ { false };
    std::thread thread_;
    bool atexitRegistered_ = false;
    std::unique_ptr<IPlaneBackend> backend_;
    std::unique_ptr<IFrameSource> frames_;
    std::unique_ptr<ICurveRenderer> renderer_;
    std::unique_ptr<Controller> controller_;
};

}} // namespace synthyboy_mpc::overlay
