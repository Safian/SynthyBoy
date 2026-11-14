// SynthyBoy - MPC overlay: the controller. One tick () of the overlay thread: switches, instance count, page detection (which VIEW is on screen), view update, drawing, presenting, status file.
// Everything it touches from outside is behind an interface (IPlaneBackend, IFrameSource, ICurveRenderer, IInstanceView) and time is a parameter, so the tests drive it with
// fakes and a fake clock; the thread and the real backends are in service.h / drm_linux.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// States: "disabled: <reason>" (an ioctl failed, the descriptor was not found: for the life of the process), "hidden: <reason>" (kill switch, several instances, no instance, no
// view of the skin is on screen, no data yet), "visible" (the status file names the view: `view: lfo2`). The files (all in /tmp by default, see Paths):
//   synthyboy-overlay-off      exists -> hidden (checked once a second; removing the file brings the overlay back)
//   synthyboy-overlay-rot      "90" or "270" (default 90), read once a second
//   synthyboy-overlay-nogpu    exists -> the CPU draws, the GPU helper is stopped (checked once a second, switches live in both directions; gpu/gpu_overlay.h)
//   synthyboy-overlay-status.txt  rewritten once a second (temporary file + rename)
//   synthyboy-overlay-dump     exists -> synthyboy-overlay-curve.txt is rewritten once a second with the data of the visible view (debug)
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
#include "gpu/gpu_overlay.h"
#include "overlay_layout.h"
#include "page_detector.h"
#include "presenter.h"
#include "renderer.h"
#include "view.h"
#include "views.h"

namespace synthyboy_mpc { namespace overlay {

struct Paths
{
    std::string killSwitch = "/tmp/synthyboy-overlay-off";
    std::string rotation = "/tmp/synthyboy-overlay-rot";
    std::string noGpu = "/tmp/synthyboy-overlay-nogpu";
    std::string status = "/tmp/synthyboy-overlay-status.txt";
    std::string dumpRequest = "/tmp/synthyboy-overlay-dump";
    std::string dump = "/tmp/synthyboy-overlay-curve.txt";
};

struct Timing
{
    double detectEveryMs = 100.0;     // 10 checks a second
    double frameEveryMs = 33.3;       // at most 30 pictures a second (two vblanks of a 60 Hz panel)
    double gpuFrameEveryMs = 33.3;    // the GPU path: the helper draws, the thread only hands over scenes
    double switchesEveryMs = 1000.0;  // kill switch, rotation file, status file
    int unreadableLimit = 20;         // checks in a row (2 s) without a readable framebuffer -> disabled
};

class Controller
{
public:
    /** `gpu`: the GPU helper (null: the CPU draws everything, as without the helper). Not owned. */
    Controller (IPlaneBackend& backend, IFrameSource& frames, ICurveRenderer& renderer, const Paths& paths, const Timing& timing = Timing(), double fixedComputeCostMs = -1.0,
                gpu::IGpuOverlay* gpuOverlay = nullptr)
        : presenter_ (backend), frames_ (frames), renderer_ (renderer), paths_ (paths), timing_ (timing), fixedCost_ (fixedComputeCostMs), gpu_ (gpuOverlay)
    {
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const layout::View& v = layout::kViews[i];
            for (int m = 0; m < 2; ++m)
                markers_[i * 2 + m] = { { v.markers[m].rect.x, v.markers[m].rect.y, v.markers[m].rect.w, v.markers[m].rect.h }, v.markers[m].rgb };
            regions_[i] = { v.region.x, v.region.y, v.region.w, v.region.h };
            switch (v.kind)
            {
                case layout::kKindFilter: views_.push_back (std::make_unique<FilterView> (v.index, renderer_, fixedCost_)); break;
                case layout::kKindLfo: views_.push_back (std::make_unique<LfoView> (v.index, fixedCost_)); break;
                case layout::kKindOsc: views_.push_back (std::make_unique<OscView> (v.index, fixedCost_)); break;
                case layout::kKindEnv: views_.push_back (std::make_unique<EnvView> (v.index, v)); break;
                default: views_.push_back (std::make_unique<NullView> (v.name)); break;
            }
            targetBase_[i] = static_cast<int> (targets_.size());
            views_.back()->targets (targets_);
        }
        detector_ = std::make_unique<PageDetector> (frames_, markers_, layout::kNumViews);
    }

    /** The table indices the overlay wants in the engine's readout (IEngine::setReadoutTargets), all views together; view i's targets start at targetBase (i). */
    static void readoutTargets (std::vector<int>& out)
    {
        out.clear();
        FilterRendererStub stub;
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const layout::View& v = layout::kViews[i];
            if (v.kind == layout::kKindFilter)
                FilterView (v.index, stub, -1.0).targets (out);
            else if (v.kind == layout::kKindOsc)
                OscView (v.index, -1.0).targets (out);
        }
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
        if (gpu_)
            gpu_->shutdown();
        presenter_.hide();
        if (! disabled())
        {
            state_ = "hidden: no instance";
            try { writeStatus (false); } catch (...) {}
        }
    }

    bool disabled() const noexcept { return presenter_.disabled() || disabledHere_; }
    const std::string& state() const noexcept { return state_; }
    bool visible() const noexcept { return presenter_.planeOn() || gpuShown_; }
    /** The last picture was made by the helper (renderer: gpu in the status file). */
    bool usingGpu() const noexcept { return usingGpu_; }
    int rotation() const noexcept { return rotation_; }
    const Presenter& presenter() const noexcept { return presenter_; }
    unsigned long framesTotal() const noexcept { return presenter_.frames() + (gpu_ ? gpu_->status().frames : 0); }
    /** The visible view's object (tests), or null. */
    const IView* currentView() const noexcept { return current_ >= 0 ? views_[static_cast<size_t> (current_)].get() : nullptr; }
    const IView* viewByName (const std::string& name) const noexcept { for (const auto& v : views_) if (name == v->name()) return v.get(); return nullptr; }
    int currentIndex() const noexcept { return current_; }
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
        if (gpu_)
            gpu_->poll (nowMs);
        if (statusDue_)
        {
            statusDue_ = false;
            writeStatus (false);
        }
        if (nextSwitchesMs_ == 0.0 || nowMs >= nextSwitchesMs_)
        {
            readSwitches (nowMs);
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
            hideGpu (nowMs);
            setState ("hidden: " + hiddenReason);
            return timing_.detectEveryMs;
        }

        if (nowMs >= nextDetectMs_)
        {
            nextDetectMs_ = nowMs + timing_.detectEveryMs;
            const Detection d = detector_->check (rotation_);
            for (int i = 0; i < d.count && i < Detection::kMaxViews; ++i)
                for (int m = 0; m < 2; ++m)
                    seen_[i][m] = d.seen[i][m];
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
            if (d.view != current_)
            {
                current_ = d.view;
                needFrame_ = true;
            }
            if (current_ < 0)
            {
                presenter_.hide();
                hideGpu (nowMs);
                setState (d.readable ? "hidden: no view on screen" : "hidden: framebuffer not readable");
            }
        }
        if (current_ >= 0)
        {
            if (gpu_ && ! noGpu_)
            {
                if (! presenter_.open())     // the helper needs the display descriptor and the panel size
                {
                    disable (presenter_.reason());
                    return 1000.0;
                }
                gpu_->request (nowMs);
            }
            const bool wantGpu = gpu_ && gpu_->active() && gpuSkipView_ != current_;
            if (wantGpu)
            {
                if (presenter_.shown() || presenter_.planeOn())
                {
                    presenter_.hide();       // the CPU buffers go, the helper takes the plane
                    needFrame_ = true;
                }
                nextWaitMs_ = timing_.gpuFrameEveryMs;
                drawGpu (nowMs);
                setRenderer (true);
                return nextWaitMs_;
            }
            if (gpu_ && ! gpu_->planeFree())
            {
                // the helper has the plane (or is taking it off): the CPU path waits until it has answered, or until the clean-up of a failure
                hideGpu (nowMs);
                setRenderer (false);
                return 5.0;
            }
            setRenderer (false);
            if (! presenter_.show (regions_[current_], rotation_))
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

    /** The part of a picture every renderer shares: the view reads its data. false: not exactly one instance. */
    bool updateView (double nowMs, bool& changed)
    {
        changed = false;
        IView& view = *views_[static_cast<size_t> (current_)];
        std::lock_guard<std::mutex> g (mu_);
        if (instances_.size() != 1)
            return false;
        IInstanceView* v = instances_[0];
        synthyboy::LiveValues lv;
        const bool haveLive = v->live (lv);
        ViewInput in;
        in.instance = v;
        in.live = haveLive ? &lv : nullptr;
        const int base = targetBase_[current_];
        in.targets = haveLive && lv.numTargets > base ? lv.target + base : nullptr;
        in.nowMs = nowMs;
        in.frames = &frames_;
        in.rotation = rotation_;
        changed = view.update (in);
        ++updatesThisSecond_;
        if (changed)
        {
            ++changesThisSecond_;
            ++viewFrames_[static_cast<size_t> (current_)].changes;
        }
        fullThisSecond_ = view.counters().recomputes;
        return true;
    }

    /** The GPU path of a tick: update, describe, hand the scene over. The helper draws and presents; this thread never waits for the vblank. */
    void drawGpu (double nowMs)
    {
        bool changed = false;
        if (! updateView (nowMs, changed))
            return;
        IView& view = *views_[static_cast<size_t> (current_)];
        if (! view.valid())
        {
            setState ("hidden: no data yet");
            hideGpu (nowMs);
            return;
        }
        const Rect& ui = regions_[current_];
        const Rect panel = uiRectToPanel (rotation_, ui, presenter_.panelWidth(), presenter_.panelHeight());
        if (panel.x < 0 || panel.y < 0 || panel.x + panel.w > presenter_.panelWidth() || panel.y + panel.h > presenter_.panelHeight() || panel.w <= 0 || panel.h <= 0)
        {
            disable ("the region does not fit the panel");
            return;
        }
        if (! gpuShown_ || gpuView_ != current_ || gpuRotation_ != rotation_)
            needFrame_ = true;
        if (! changed && ! needFrame_)
        {
            setState ("visible");
            return;
        }
        if (nowMs - lastFrameMs_ < kMinFrameGapMs && gpuShown_ && gpuView_ == current_)
        {
            needFrame_ = true;
            return;
        }
        gpu::SceneBuilder sb (gpuScene_);
        if (! view.describe (sb, current_, ui.w, ui.h))
        {
            gpuSkipView_ = current_;     // this view cannot be described: the CPU draws it
            hideGpu (nowMs);
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        if (! gpu_->show (ui, panel, rotation_, current_, views_[static_cast<size_t> (current_)]->name(), nowMs) || ! gpu_->submit (gpuScene_, nowMs))
            return;     // the helper failed: the next tick draws with the CPU
        gpuShown_ = true;
        gpuView_ = current_;
        gpuRotation_ = rotation_;
        renderMs_ = 0.8 * renderMs_ + 0.2 * std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        lastFrameMs_ = nowMs;
        needFrame_ = false;
        ++framesThisSecond_;
        ++viewFrames_[static_cast<size_t> (current_)].frames;
        setState ("visible");
    }

    void hideGpu (double nowMs)
    {
        if (! gpu_)
            return;
        gpu_->hide (nowMs);
        gpuShown_ = false;
        gpuView_ = -1;
        needFrame_ = true;
    }

    void setRenderer (bool gpuNow)
    {
        if (usingGpu_ == gpuNow)
            return;
        usingGpu_ = gpuNow;
        statusDue_ = true;     // the status file says which one draws: right away, not at the next second
    }

    void drawIfChanged (double nowMs)
    {
        const auto workStart = std::chrono::steady_clock::now();
        bool changed = false;
        IView& view = *views_[static_cast<size_t> (current_)];
        if (! updateView (nowMs, changed))
            return;
        if (! view.valid())
        {
            setState ("hidden: no data yet");
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
        const int w = regions_[current_].w, h = regions_[current_].h;
        canvas_.resize (static_cast<size_t> (w) * static_cast<size_t> (h));
        const auto t0 = Clock::now();
        view.render (canvas_.data(), w, h);
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
            ++viewFrames_[static_cast<size_t> (current_)].frames;
            setState ("visible");
            // SETPLANE returns right after a vblank. The next picture is due two vblanks later: start drawing early enough that it is ready (and SETPLANE then
            // waits for the vblank, which locks the pictures to every second one) instead of starting a full period after the return, which would miss it
            nextWaitMs_ = std::max (1.0, timing_.frameEveryMs - workPeakMs_ - kSlackMs);
        }
    }

    void disable (const std::string& why)
    {
        disabledHere_ = true;
        if (gpu_)
            gpu_->shutdown();
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

    void readSwitches (double nowMs)
    {
        killed_ = exists (paths_.killSwitch);
        const bool noGpu = exists (paths_.noGpu);
        if (noGpu != noGpu_)
        {
            noGpu_ = noGpu;
            if (gpu_)
            {
                if (noGpu)
                    hideGpu (nowMs);
                gpu_->setForceCpu (noGpu, nowMs);
                needFrame_ = true;
            }
        }
        int rot = 90;
        std::ifstream in (paths_.rotation);
        int v = 0;
        if (in && (in >> v))
            rot = normaliseRotation (v);
        if (rot != rotation_)
        {
            presenter_.hide();   // set up again with the other orientation
            hideGpu (nowMs);
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
        if (gpu_)
        {
            const unsigned long f = gpu_->status().frames;     // what the helper has presented (not what was handed over)
            framesLast_ = usingGpu_ ? static_cast<unsigned> (f - gpuFramesAtSecond_) : framesThisSecond_;
            gpuFramesAtSecond_ = f;
        }
        updatesLast_ = updatesThisSecond_;
        changesLast_ = changesThisSecond_;
        updatesThisSecond_ = changesThisSecond_ = 0;
        cpuPercent_ = (cpuClock_ && wall > 100.0) ? 100.0 * (cpu - cpuAtSecond_) / wall : 0.0;
        cpuAtSecond_ = cpu;
        framesThisSecond_ = 0;
        secondStartMs_ = nowMs;
        writeStatus (false);
        recomputesAtSecond_ = current_ >= 0 ? views_[static_cast<size_t> (current_)]->counters().recomputes : 0;
        if (dumpRequested_)
            writeDump();
    }

    void writeStatus (bool final_)
    {
        std::ostringstream o;
        o << "state: " << state_ << "\n";
        o << "view: " << (current_ >= 0 && state_ == "visible" ? views_[static_cast<size_t> (current_)]->name() : "none") << "\n";
        o << "rotation: " << rotation_ << "\n";
        writeGpuStatus (o);
        size_t count;
        {
            std::lock_guard<std::mutex> g (mu_);
            count = instances_.size();
        }
        o << "instances: " << count << "\n";
        o << "frames_last_second: " << (final_ ? 0u : framesLast_) << "\n";
        o << "frames_total: " << presenter_.frames() << "\n";
        o << "curve_updates_last_second: " << updatesLast_ << "\ncurve_changes_last_second: " << changesLast_ << "\n";
        o << "overlay_thread_cpu_percent_of_one_core: " << cpuPercent_ << "\n";
        o << "work_peak_ms: " << workPeakMs_ << "\nrender_ms: " << renderMs_ << "\nrotate_ms: " << rotateMs_ << "\nsetplane_ms: " << setPlaneMs_ << "\n";
        if (current_ >= 0)
        {
            const IView& v = *views_[static_cast<size_t> (current_)];
            o << "full_recomputes_last_second: " << (v.counters().recomputes - recomputesAtSecond_) << "\n";
            v.writeStatus (o);
        }
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            const ViewCounters& c = views_[static_cast<size_t> (i)]->counters();
            o << "view_" << layout::kViews[i].name << ": updates=" << c.updates << " changes=" << c.changes << " recomputes=" << c.recomputes << " frames=" << viewFrames_[static_cast<size_t> (i)].frames << "\n";
        }
        char buf[200];
        for (int i = 0; i < layout::kNumViews; ++i)
        {
            std::snprintf (buf, sizeof buf, "markers_read_%s: left=%06x right=%06x (expected %06x %06x)\n", layout::kViews[i].name, seen_[i][0] & 0xffffff, seen_[i][1] & 0xffffff,
                           layout::kViews[i].markers[0].rgb, layout::kViews[i].markers[1].rgb);
            o << buf;
        }
        o << "panel: " << presenter_.panelWidth() << "x" << presenter_.panelHeight() << "\n";
        if (presenter_.planeOn())
            o << "plane_rect: " << presenter_.panelRect().w << "x" << presenter_.panelRect().h << "+" << presenter_.panelRect().x << "+" << presenter_.panelRect().y << "\n";
        writeAtomically (paths_.status, o.str());
    }

    void writeGpuStatus (std::ostream& o) const
    {
        if (! gpu_)
        {
            o << "renderer: cpu\ngpu_state: not built\n";
            return;
        }
        const gpu::GpuStatus g = gpu_->status();
        o << "renderer: " << (usingGpu_ && g.active ? "gpu" : "cpu") << "\ngpu_state: " << g.state << "\ngpu_helper_pid: " << g.pid << "\ngpu_helper_starts: " << g.starts << "\ngpu_helper_restarts: " << g.restarts
          << "\ngpu_failures: " << g.failures << "\ngpu_last_error: " << (g.lastError.empty() ? "none" : g.lastError) << "\ngpu_helper_frames: " << g.frames
          << "\ngpu_helper_render_ms: " << g.helperRenderMs << "\ngpu_helper_setplane_ms: " << g.helperSetPlaneMs << "\ngpu_helper_skipped_vblanks: " << g.skippedVblanks << "\ngpu_helper_late_setplanes: " << g.lateSetPlanes
          << "\ngpu_helper_startup_ms: " << g.startupMs << "\ngpu_renderer: " << (g.glRenderer.empty() ? "none" : g.glRenderer) << "\n";
    }

    void writeDump()
    {
        if (current_ < 0 || ! views_[static_cast<size_t> (current_)]->valid())
            return;
        std::ostringstream o;
        views_[static_cast<size_t> (current_)]->writeDump (o);
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
    gpu::IGpuOverlay* gpu_ = nullptr;
    gpu::SceneData gpuScene_ {};
    bool noGpu_ = false, gpuShown_ = false, usingGpu_ = false, statusDue_ = false;
    int gpuView_ = -1, gpuRotation_ = 0, gpuSkipView_ = -1;
    unsigned long gpuFramesAtSecond_ = 0;
    MarkerSpec markers_[layout::kNumViews * 2];
    Rect regions_[layout::kNumViews];
    int targetBase_[layout::kNumViews] = {};
    std::vector<int> targets_;
    std::vector<std::unique_ptr<IView>> views_;
    struct ViewFrames { unsigned long frames = 0, changes = 0; };
    std::vector<ViewFrames> viewFrames_ = std::vector<ViewFrames> (layout::kNumViews);
    std::unique_ptr<PageDetector> detector_;
    std::vector<uint32_t> canvas_;

    std::mutex mu_;
    std::vector<IInstanceView*> instances_;

    std::string state_ = "hidden: starting";
    std::function<double()> cpuClock_;
    bool killed_ = false, disabledHere_ = false, dumpRequested_ = false, needFrame_ = true;
    int current_ = -1;
    int rotation_ = 90, unreadable_ = 0;
    uint32_t seen_[Detection::kMaxViews][2] = {};
    double nextSwitchesMs_ = 0.0, nextDetectMs_ = 0.0, lastFrameMs_ = -1000.0, secondStartMs_ = 0.0, cpuAtSecond_ = 0.0, cpuPercent_ = 0.0;
    unsigned framesThisSecond_ = 0, framesLast_ = 0, updatesThisSecond_ = 0, updatesLast_ = 0, changesThisSecond_ = 0, changesLast_ = 0;
    double nextWaitMs_ = 33.3, workPeakMs_ = 0.0, renderMs_ = 0.0, rotateMs_ = 0.0, setPlaneMs_ = 0.0;
    unsigned long fullThisSecond_ = 0, recomputesAtSecond_ = 0;
};

}} // namespace synthyboy_mpc::overlay
