// SynthyBoy - MPC overlay: the controller. One tick () of the overlay thread: switches, instance count, page detection, curve update, drawing, presenting, status file.
// Everything it touches from outside is behind an interface (IPlaneBackend, IFrameSource, ICurveRenderer, IInstanceView) and time is a parameter, so the tests drive it with
// fakes and a fake clock; the thread and the real backends are in service.h / drm_linux.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// States: "disabled: <reason>" (an ioctl failed, the descriptor was not found: for the life of the process), "hidden: <reason>" (kill switch, several instances, no instance, the
// page is not on screen, no curve yet), "visible". The files (all in /tmp by default, see Paths):
//   synthyboy-overlay-off      exists -> hidden (checked once a second; removing the file brings the overlay back)
//   synthyboy-overlay-rot      "90" or "270" (default 90), read once a second
//   synthyboy-overlay-status.txt  rewritten once a second (temporary file + rename)
//   synthyboy-overlay-dump     exists -> synthyboy-overlay-curve.txt is rewritten once a second with the 256 values of the shown and of the exact curve (debug)
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "curve_source.h"
#include "drm_interfaces.h"
#include "overlay_layout.h"
#include "page_detector.h"
#include "presenter.h"
#include "renderer.h"

namespace synthyboy_mpc { namespace overlay {

struct Paths
{
    std::string killSwitch = "/tmp/synthyboy-overlay-off";
    std::string rotation = "/tmp/synthyboy-overlay-rot";
    std::string status = "/tmp/synthyboy-overlay-status.txt";
    std::string dumpRequest = "/tmp/synthyboy-overlay-dump";
    std::string dump = "/tmp/synthyboy-overlay-curve.txt";
};

struct Timing
{
    double detectEveryMs = 100.0;     // 10 checks a second
    double frameEveryMs = 33.3;       // at most 30 pictures a second (two vblanks of a 60 Hz panel)
    double switchesEveryMs = 1000.0;  // kill switch, rotation file, status file
    int unreadableLimit = 20;         // checks in a row (2 s) without a readable framebuffer -> disabled
};

class Controller
{
public:
    Controller (IPlaneBackend& backend, IFrameSource& frames, ICurveRenderer& renderer, const Paths& paths, const Timing& timing = Timing(), double fixedComputeCostMs = -1.0)
        : presenter_ (backend), frames_ (frames), renderer_ (renderer), paths_ (paths), timing_ (timing), fixedCost_ (fixedComputeCostMs)
    {
        for (int i = 0; i < layout::kNumMarkers; ++i)
            markers_[i] = { { layout::kMarkers[i].rect.x, layout::kMarkers[i].rect.y, layout::kMarkers[i].rect.w, layout::kMarkers[i].rect.h }, layout::kMarkers[i].rgb };
        detector_ = std::make_unique<PageDetector> (frames_, markers_, layout::kNumMarkers);
        curveRect_ = { layout::kCurve.x, layout::kCurve.y, layout::kCurve.w, layout::kCurve.h };
        for (int i = 0; i < layout::kNumMarkers; ++i)
            covers_[i] = { markers_[i].rect.x - curveRect_.x, markers_[i].rect.y - curveRect_.y, markers_[i].rect.w, markers_[i].rect.h };
    }

    // ---- instances (host threads; the lock is also held while the overlay thread uses an instance) ----
    void attach (IInstanceView* view)
    {
        std::lock_guard<std::mutex> g (mu_);
        instances_.push_back (view);
    }
    /** Returns the number of instances left. */
    size_t detach (IInstanceView* view)
    {
        std::lock_guard<std::mutex> g (mu_);
        for (size_t i = 0; i < instances_.size(); ++i)
            if (instances_[i] == view)
            {
                instances_.erase (instances_.begin() + static_cast<std::ptrdiff_t> (i));
                break;
            }
        return instances_.size();
    }

    /** Plane off, buffers freed (the overlay thread has stopped, or this is the thread). */
    void shutdown() noexcept
    {
        presenter_.hide();
        if (! disabled())
        {
            state_ = "hidden: no instance";
            try { writeStatus (false); } catch (...) {}
        }
    }

    bool disabled() const noexcept { return presenter_.disabled() || disabledHere_; }
    const std::string& state() const noexcept { return state_; }
    bool visible() const noexcept { return presenter_.planeOn(); }
    int rotation() const noexcept { return rotation_; }
    const Presenter& presenter() const noexcept { return presenter_; }
    unsigned long framesTotal() const noexcept { return presenter_.frames(); }
    const CurveSource* curveSource() const noexcept { return curve_.get(); }
    /** Optional: the CPU time of the calling thread in ms (the status file shows the overlay thread's load). */
    void setCpuClock (std::function<double()> clock) { cpuClock_ = std::move (clock); }

    /** One step. Returns after how many milliseconds the next one is due. Never throws. */
    double tick (double nowMs)
    {
        try
        {
            return step (nowMs);
        }
        catch (...)
        {
            disable ("exception in the overlay thread");
            return 1000.0;
        }
    }

private:
    double step (double nowMs)
    {
        if (disabled())
        {
            if (state_.compare (0, 8, "disabled") != 0)
                disable (presenter_.reason());
            return 1000.0;
        }
        if (nextSwitchesMs_ == 0.0 || nowMs >= nextSwitchesMs_)
        {
            readSwitches();
            nextSwitchesMs_ = nowMs + timing_.switchesEveryMs;
            if (secondStartMs_ == 0.0) secondStartMs_ = nowMs;
            secondTick (nowMs);
        }
        size_t count;
        {
            std::lock_guard<std::mutex> g (mu_);
            count = instances_.size();
        }
        std::string hiddenReason;
        if (killed_)            hiddenReason = "kill switch";
        else if (count == 0)    hiddenReason = "no instance";
        else if (count > 1)     hiddenReason = "several instances";
        if (! hiddenReason.empty())
        {
            presenter_.hide();
            setState ("hidden: " + hiddenReason);
            return timing_.detectEveryMs;
        }

        if (nowMs >= nextDetectMs_)
        {
            nextDetectMs_ = nowMs + timing_.detectEveryMs;
            const Detection d = detector_->check (rotation_);
            for (int i = 0; i < d.count && i < 8; ++i)
                seen_[i] = d.seen[i];
            if (! d.readable)
            {
                if (++unreadable_ >= timing_.unreadableLimit)
                {
                    disable ("the framebuffer cannot be read");
                    return 1000.0;
                }
            }
            else
                unreadable_ = 0;
            pageVisible_ = d.visible;
            if (! pageVisible_)
            {
                presenter_.hide();
                setState (d.readable ? "hidden: page not on screen" : "hidden: framebuffer not readable");
            }
        }
        if (pageVisible_)
        {
            if (! presenter_.show (curveRect_, rotation_))
            {
                disable (presenter_.reason());
                return 1000.0;
            }
            nextWaitMs_ = timing_.frameEveryMs;
            drawIfChanged (nowMs);
            if (presenter_.disabled())
            {
                disable (presenter_.reason());
                return 1000.0;
            }
            return nextWaitMs_;
        }
        return timing_.detectEveryMs;
    }

    void drawIfChanged (double nowMs)
    {
        const auto workStart = std::chrono::steady_clock::now();
        bool changed = false;
        {
            std::lock_guard<std::mutex> g (mu_);
            if (instances_.size() != 1)
                return;
            IInstanceView* v = instances_[0];
            if (! curve_)
            {
                CurveSource::Config cfg;
                cfg.sampleRate = v->sampleRate();
                cfg.fixedCostMs = fixedCost_;
                curve_ = std::make_unique<CurveSource> (cfg);
                curveRate_ = cfg.sampleRate;
            }
            else if (v->sampleRate() != curveRate_)
            {
                CurveSource::Config cfg;
                cfg.sampleRate = v->sampleRate();
                cfg.fixedCostMs = fixedCost_;
                curve_ = std::make_unique<CurveSource> (cfg);
                curveRate_ = cfg.sampleRate;
                needFrame_ = true;
            }
            const unsigned long before = curve_->stats().fullComputes;
            changed = curve_->update (*v, nowMs);
            fullThisSecond_ += curve_->stats().fullComputes - before;
        }
        if (! curve_ || ! curve_->valid())
        {
            setState ("hidden: no curve yet");
            return;
        }
        if (! presenter_.planeOn())
            needFrame_ = true;
        if (! changed && ! needFrame_)
        {
            setState ("visible");
            return;
        }
        if (nowMs - lastFrameMs_ < kMinFrameGapMs && presenter_.planeOn())
        {
            needFrame_ = true;     // keep the picture for the next tick
            return;
        }
        using Clock = std::chrono::steady_clock;
        const auto ms = [] (Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli> (b - a).count(); };
        const int w = curveRect_.w, h = curveRect_.h;
        canvas_.resize (static_cast<size_t> (w) * static_cast<size_t> (h));
        const auto t0 = Clock::now();
        renderer_.render (curve_->curve(), curve_->points(), canvas_.data(), w, h, covers_, layout::kNumMarkers);
        const auto t1 = Clock::now();
        size_t stride = 0;
        uint32_t* back = presenter_.backBuffer (stride);
        if (! back)
            return;
        rotateCanvasInto (rotation_, canvas_.data(), w, h, back, stride);
        const auto t2 = Clock::now();
        const bool ok = presenter_.present();   // waits for the vblank on the device
        const auto t3 = Clock::now();
        // the work before SETPLANE (the full computation included, which only some of the pictures need): a decaying maximum, so that even those pictures are ready in time
        workPeakMs_ = std::max (ms (workStart, t2), 0.95 * workPeakMs_);
        renderMs_ = 0.8 * renderMs_ + 0.2 * ms (t0, t1);
        rotateMs_ = 0.8 * rotateMs_ + 0.2 * ms (t1, t2);
        if (ok)
        {
            setPlaneMs_ = 0.8 * setPlaneMs_ + 0.2 * ms (t2, t3);
            lastFrameMs_ = nowMs;
            needFrame_ = false;
            ++framesThisSecond_;
            setState ("visible");
            // SETPLANE returns right after a vblank. The next picture is due two vblanks later: start drawing early enough that it is ready (and SETPLANE then
            // waits for the vblank, which locks the pictures to every second one) instead of starting a full period after the return, which would miss it
            nextWaitMs_ = std::max (1.0, timing_.frameEveryMs - workPeakMs_ - kSlackMs);
        }
    }

    void disable (const std::string& why)
    {
        disabledHere_ = true;
        presenter_.hide();
        setState ("disabled: " + why);
        writeStatus (true);
    }

    void setState (const std::string& s)
    {
        if (s != state_)
            state_ = s;
    }

    static bool exists (const std::string& path) { return ::access (path.c_str(), F_OK) == 0; }

    void readSwitches()
    {
        killed_ = exists (paths_.killSwitch);
        int rot = 90;
        std::ifstream in (paths_.rotation);
        int v = 0;
        if (in && (in >> v))
            rot = normaliseRotation (v);
        if (rot != rotation_)
        {
            presenter_.hide();   // set up again with the other orientation
            rotation_ = rot;
            needFrame_ = true;
        }
        dumpRequested_ = exists (paths_.dumpRequest);
    }

    void secondTick (double nowMs)
    {
        const double cpu = cpuClock_ ? cpuClock_() : 0.0;
        const double wall = nowMs - secondStartMs_;
        framesLast_ = framesThisSecond_;
        fullLast_ = fullThisSecond_;
        cpuPercent_ = (cpuClock_ && wall > 100.0) ? 100.0 * (cpu - cpuAtSecond_) / wall : 0.0;
        cpuAtSecond_ = cpu;
        framesThisSecond_ = 0;
        fullThisSecond_ = 0;
        secondStartMs_ = nowMs;
        writeStatus (false);
        if (dumpRequested_)
            writeDump();
    }

    void writeStatus (bool final_)
    {
        std::ostringstream o;
        o << "state: " << state_ << "\n";
        o << "rotation: " << rotation_ << "\n";
        size_t count;
        {
            std::lock_guard<std::mutex> g (mu_);
            count = instances_.size();
        }
        o << "instances: " << count << "\n";
        o << "frames_last_second: " << (final_ ? 0u : framesLast_) << "\n";
        o << "frames_total: " << presenter_.frames() << "\n";
        o << "full_recomputes_last_second: " << fullLast_ << "\n";
        o << "overlay_thread_cpu_percent_of_one_core: " << cpuPercent_ << "\n";
        o << "work_peak_ms: " << workPeakMs_ << "\nrender_ms: " << renderMs_ << "\nrotate_ms: " << rotateMs_ << "\nsetplane_ms: " << setPlaneMs_ << "\n";
        if (curve_)
        {
            const CurveSource::Stats& s = curve_->stats();
            o << "last_compute_ms: " << s.lastComputeMs << "\n";
            o << "full_computes_total: " << s.fullComputes << "\n";
            o << "translations_total: " << s.translations << "\n";
            o << "live_cutoff_hz: " << s.cutoffHz << "\n";
            o << "live_resonance_percent: " << s.resonancePercent << "\n";
            if (curve_->valid())
            {
                float lo, hi;
                curve_->minMax (lo, hi);
                char hash[16];
                std::snprintf (hash, sizeof hash, "%08x", curve_->hash());
                o << "curve_min_db: " << lo << "\ncurve_max_db: " << hi << "\ncurve_hash: " << hash << "\n";
            }
        }
        char buf[160];
        std::snprintf (buf, sizeof buf, "markers_read: left=%06x right=%06x (expected %06x %06x)\n", seen_[0] & 0xffffff, seen_[1] & 0xffffff, layout::kMarkers[0].rgb, layout::kMarkers[1].rgb);
        o << buf;
        o << "panel: " << presenter_.panelWidth() << "x" << presenter_.panelHeight() << "\n";
        if (presenter_.planeOn())
            o << "plane_rect: " << presenter_.panelRect().w << "x" << presenter_.panelRect().h << "+" << presenter_.panelRect().x << "+" << presenter_.panelRect().y << "\n";
        writeAtomically (paths_.status, o.str());
    }

    void writeDump()
    {
        if (! curve_ || ! curve_->valid())
            return;
        std::ostringstream o;
        o << "# index shown_db exact_db (point i = 20 Hz * 1000 ^ (i / 255)), cutoff_hz " << curve_->stats().cutoffHz << " resonance_percent " << curve_->stats().resonancePercent << "\n";
        for (int i = 0; i < curve_->points(); ++i)
            o << i << " " << curve_->curve()[i] << " " << curve_->exactCurve()[i] << "\n";
        writeAtomically (paths_.dump, o.str());
    }

    static void writeAtomically (const std::string& path, const std::string& text)
    {
        const std::string tmp = path + ".tmp";
        {
            std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
            if (! out)
                return;
            out << text;
        }
        std::rename (tmp.c_str(), path.c_str());
    }

    static constexpr double kMinFrameGapMs = 5.0;    // never faster than this (a present waits for the vblank, 16.7 ms, anyway)
    static constexpr double kSlackMs = 3.0;
    Presenter presenter_;
    IFrameSource& frames_;
    ICurveRenderer& renderer_;
    Paths paths_;
    Timing timing_;
    double fixedCost_;
    MarkerSpec markers_[layout::kNumMarkers];
    Rect covers_[layout::kNumMarkers];
    Rect curveRect_;
    std::unique_ptr<PageDetector> detector_;
    std::unique_ptr<CurveSource> curve_;
    double curveRate_ = 0.0;
    std::vector<uint32_t> canvas_;

    std::mutex mu_;
    std::vector<IInstanceView*> instances_;

    std::string state_ = "hidden: starting";
    std::function<double()> cpuClock_;
    bool killed_ = false, pageVisible_ = false, disabledHere_ = false, dumpRequested_ = false, needFrame_ = true;
    int rotation_ = 90, unreadable_ = 0;
    uint32_t seen_[8] = { 0xffffffffu, 0xffffffffu, 0, 0, 0, 0, 0, 0 };
    double nextSwitchesMs_ = 0.0, nextDetectMs_ = 0.0, lastFrameMs_ = -1000.0, secondStartMs_ = 0.0, cpuAtSecond_ = 0.0, cpuPercent_ = 0.0;
    unsigned framesThisSecond_ = 0, framesLast_ = 0;
    double nextWaitMs_ = 33.3, workPeakMs_ = 0.0, renderMs_ = 0.0, rotateMs_ = 0.0, setPlaneMs_ = 0.0;
    unsigned long fullThisSecond_ = 0, fullLast_ = 0;
};

}} // namespace synthyboy_mpc::overlay
