// SynthyBoy - MPC overlay: the controller. One tick () of the overlay thread: switches, instance count, page detection (which VIEW is on screen), view update, drawing, presenting, status file.
// Everything it touches from outside is behind an interface (IPlaneBackend, IFrameSource, ICurveRenderer, mpckit::IInstance) and time is a parameter, so the tests drive it with
// fakes and a fake clock; the thread and the real backends are in service.h / drm_linux.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// States: "disabled: <reason>" (an ioctl failed, the descriptor was not found: for the life of the process), "hidden: <reason>" (kill switch, no instance, no view of the skin is on
// screen, the instance of the page on screen cannot be told / has no slot / is gone, no data yet), "visible" (the status file names the view and the instance slot: `view: lfo2`,
// `instance_slot: 1`). With several instances the page on screen belongs to one of them: the skin shows its slot as a colour (instance_slots.h) and that instance's data is drawn. The files (all in /tmp by default, see Paths):
//   synthyboy-overlay-off      exists -> hidden (checked once a second; removing the file brings the overlay back)
//   synthyboy-overlay-rot      "0", "90", "180" or "270": overrides the rotation the overlay finds itself (from its own markers, page_detector.h); read once a second
//   synthyboy-overlay-nogpu    exists -> the CPU draws, the GPU helper is stopped (checked once a second, switches live in both directions; gpu/gpu_overlay.h)
//   synthyboy-overlay-status.txt  rewritten once a second (temporary file + rename)
//   synthyboy-overlay-dump     exists -> synthyboy-overlay-curve.txt is rewritten once a second with the data of the visible view (debug)
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
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

#include "../kit/branding.h"
#include "drm_interfaces.h"
#include "gpu/gpu_overlay.h"
#include "gpu_clock.h"
#include "instance_slots.h"
#include "../kit/instance.h"
#include "../kit/page_set.h"
#include "host_background.h"
#include "page_detector.h"
#include "panel_key.h"
#include "mouse_router.h"
#include "qlink_setting.h"
#include "presenter.h"
#include "standby.h"
#include "../ui/theme.h"
#include "renderer.h"
#include "touch_input.h"
#include "../safe_file.h"
#include "../write_files.h"
#include "../startup_timing.h"
#include "ui_stack.h"
#include "vblank_pacer.h"
#include "view.h"

namespace synthyboy_mpc { namespace overlay {

struct Paths
{
    std::string killSwitch = "/tmp/synthyboy-overlay-off";
    std::string rotation = "/tmp/synthyboy-overlay-rot";
    std::string noGpu = "/tmp/synthyboy-overlay-nogpu";
    std::string status = "/tmp/synthyboy-overlay-status.txt";
    std::string statusRequest = "/tmp/synthyboy-overlay-status-on";   // only a build with SYNTHYBOY_MPC_WRITE_FILES 0 (write_files.h) looks at it: the status file is written while this file exists
    std::string detail = "/tmp/synthyboy-overlay-detail";   // 1 / full, 2 / reduced: the level of detail of the UI (ui/theme.h Detail); without the file (or "auto") the plugin chooses from the measured frame time
    std::string gpuClock = "/tmp/synthyboy-overlay-gpuclock";   // "off": the clock of the GPU is left to its governor; otherwise it is raised while the picture moves (gpu_clock.h)
    std::string rate = "/tmp/synthyboy-overlay-fps";   // 30 or 60: the pictures a second the GPU path aims at; without the file (or "auto") 30 on a GPU of the Mali-T series (the Key 61), 60 (tried, kept when it is reached) on the others
    std::string pointerHotSpot = "/tmp/synthyboy-overlay-pointer-hotspot";   // debug: "x y", the pixel of the host's pointer plane picture that is the arrow's tip (plane pixels, panel orientation); overrides the measured / default hot spot
    std::string qlinkSettings = "/data/Settings/MPC/MPC.settings";   // the host's settings (read only): the option "Show Q-Link Status When Touched" (qlink_setting.h); with it off the key does nothing
    std::string look = "/tmp/synthyboy-overlay-look";   // a number 1 .. 3: the look of the UI (ui/theme.h); without the file look 1
    std::string dumpRequest = "/tmp/synthyboy-overlay-dump";
    std::string dump = "/tmp/synthyboy-overlay-curve.txt";
    std::string backlightDir = "/sys/class/backlight";   // the panel's backlight nodes (read only): the host's standby dim shows in them (standby.h)

    /** The paths of a plugin with the given branding (kit/branding.h): /tmp/<prefix>-overlay-off ... The members above are the same names for SynthyBoy's branding (a test compares them). */
    static Paths forBranding (const mpckit::Branding& b)
    {
        Paths p;
        p.killSwitch = mpckit::tmpFile (b, "overlay-off");
        p.rotation = mpckit::tmpFile (b, "overlay-rot");
        p.noGpu = mpckit::tmpFile (b, "overlay-nogpu");
        p.status = mpckit::tmpFile (b, "overlay-status.txt");
        p.statusRequest = mpckit::tmpFile (b, "overlay-status-on");
        p.detail = mpckit::tmpFile (b, "overlay-detail");
        p.gpuClock = mpckit::tmpFile (b, "overlay-gpuclock");
        p.rate = mpckit::tmpFile (b, "overlay-fps");
        p.pointerHotSpot = mpckit::tmpFile (b, "overlay-pointer-hotspot");
        p.look = mpckit::tmpFile (b, "overlay-look");
        p.dumpRequest = mpckit::tmpFile (b, "overlay-dump");
        p.dump = mpckit::tmpFile (b, "overlay-curve.txt");
        return p;   // (qlinkSettings and backlightDir are the host's own files: no branding)
    }
};

#ifndef SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS
#define SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS 33.3
#endif

struct Timing
{
    double detectEveryMs = 100.0;     // 10 checks a second
    double frameEveryMs = 33.3;       // at most 30 pictures a second (two vblanks of a 60 Hz panel)
    double gpuFrameEveryMs = SYNTHYBOY_MPC_OVERLAY_GPU_FRAME_MS;   // the GPU path: the helper draws, the thread only hands over scenes (33.3: 30 a second like the CPU path; 16.7 for 60)
    bool followDisplayPace = true;    // once the display is known: the picture gap is the pace grid's (vblanks per picture x the measured period; scenes for the GPU 20 % faster than the grid)
    double switchesEveryMs = 1000.0;  // kill switch, rotation file, status file
    int unreadableLimit = 20;         // checks in a row (2 s) without a readable framebuffer -> disabled
    bool nativeTabTouch = false;      // the XL host function key shared by pages with native backgrounds
    bool prewarmGpu = false;          // start the GPU helper as soon as an instance is attached and the display is known, not when the first page is found on screen (the service sets it; the helper then ran in parallel with the host's loading)
};

// Full-screen host MenuOverlay and NumericOverlay for native parameter editors and selectors.
// Their skin definitions place these on top of the popup, independently of its body.
inline constexpr MarkerSpec kNativeParameterModalMarkers[] = {
    { { 16, 120, 6, 6 }, 0xfe356c },
    { { 28, 120, 6, 6 }, 0x24c7ff },
    { { 40, 120, 6, 6 }, 0x90ed43 },
};
inline constexpr MarkerSpec kNativeFxRackPopupMarker { { 52, 120, 6, 6 }, 0xf1ae47 };
inline constexpr MarkerSpec kNativeFxAddPopupMarker { { 64, 120, 6, 6 }, 0xef72bf };
inline constexpr MarkerSpec kNativeFxActionMarkers[] { kNativeFxRackPopupMarker, kNativeFxAddPopupMarker };
inline constexpr MarkerSpec kNativePlayActionMarkers[] {
    { { 52, 120, 6, 6 }, 0xe5a44f },   // routing
    { { 64, 120, 6, 6 }, 0xde78c9 },   // type, waveform or noise sound
    { { 76, 120, 6, 6 }, 0x58d4b1 },   // octave keypad
    { { 88, 120, 6, 6 }, 0x459be9 },   // Play LFO instance
    { { 100, 120, 6, 6 }, 0xaee33c },  // shape
    { { 112, 120, 6, 6 }, 0x9659df },  // division
    { { 124, 120, 6, 6 }, 0xf0bc77 },  // random style
    { { 136, 120, 6, 6 }, 0x6ef2a0 },  // random sync
};

class Controller
{
public:
    /** `gpuOverlay`: the GPU helper (null: the CPU draws everything, as without the helper). Not owned. `pages`: the tabs the plugin draws (the markers, the regions, the instance colours: kit/page_set.h);
        `factory`: makes the view of each page and knows the plugin's live block (kit/page_set.h IPageFactory). Both are the plugin's and must outlive the controller. This class knows no engine: SynthyBoy's controller
        for the tests is SynthyBoyController (controller_synthyboy.h). */
    Controller (IPlaneBackend& backend, IFrameSource& frames, const Paths& paths, const Timing& timing, gpu::IGpuOverlay* gpuOverlay, ITouchInput* touchInput,
                const mpckit::IPageSet& pages, mpckit::IPageFactory& factory, bool deferViews = false)
        : pages_ (pages), factory_ (factory), numViews_ (std::min (pages.numPages(), static_cast<int> (Detection::kMaxViews))), backend_ (backend), presenter_ (backend), frames_ (frames), paths_ (paths), timing_ (timing), gpu_ (gpuOverlay), touchIn_ (touchInput)
    {
        liveBuf_.resize ((factory_.liveBytes() + sizeof (std::max_align_t) - 1) / sizeof (std::max_align_t));
        if (! deferViews)
            buildViews();
        for (int& s : viewSlot_)
            s = -1;
        for (int& g : uiGroup_)
            g = -1;
    }

    /** Makes the views (the 37 pages' scenes: every layout view is constructed, its region and readout targets are taken) and the page detector. The constructor does it unless `deferViews` was given (the service does:
        the plugin's calling thread, the host's effOpen path, must not pay for it); then the first tick () of the overlay thread does it. Idempotent. Only the thread that ticks calls it after construction. */
    void buildViews()
    {
        if (viewsBuilt_)
            return;
        viewsBuilt_ = true;
        std::vector<int> range;
        for (int i = 0; i < numViews_; ++i)
        {
            const mpckit::PageDef& v = pages_.page (i);
            for (int m = 0; m < 2; ++m)
                markers_[i * 2 + m] = { { v.markers[m].rect.x, v.markers[m].rect.y, v.markers[m].rect.w, v.markers[m].rect.h }, v.markers[m].rgb };
            regions_[i] = { v.region.x, v.region.y, v.region.w, v.region.h };
            views_.push_back (makeView (i));
            { Rect own; if (views_.back()->regionOverride (own)) regions_[i] = own; }
            views_.back()->hostBackgroundSentinel (backgrounds_[i].rect, backgrounds_[i].rgb);
            instanceRects_[i] = { v.instance.x, v.instance.y, v.instance.w, v.instance.h };
            range.clear();
            views_.back()->targets (range);
            targetBase_[i] = mpckit::appendReadoutRange (targets_, range);
            targetCount_[i] = static_cast<int> (range.size());
        }
        detector_ = std::make_unique<PageDetector> (frames_, markers_, numViews_, instanceRects_, &pages_.instanceColours(), backgrounds_);
        startupTimes().markUiStep (StartupTimes::kUiViewsBuilt);
    }

    /** The table indices the overlay wants in the engine's readout (IEngine::setReadoutTargets), all views together (what the page factory says); view i's targets start at targetBase (i). Before a controller exists
        SynthyBoy's shell asks SynthyBoyPageFactory::targetsOfAllPages (pages.h). */
    void readoutTargets (std::vector<int>& out) const { factory_.readoutTargets (out); }

    // ---- instances (host threads; the lock is also held while the overlay thread uses an instance) ----
    /** Registers an instance and returns its slot (0 .. 15: the lowest free one, kept until detach). -1: all 16 slots are taken; such an instance never shows the overlay. */
    int attach (mpckit::IInstance* view)
    {
        std::lock_guard<std::mutex> g (mu_);
        const int slot = slots_.acquire (view);
        if (slot >= 0)
        {
            ui_[slot] = UiStack();   // a new instance starts in main
            uiGroup_[slot] = -1;
            uiOpenSeen_[slot] = false;
            blankGraceUntilMs_[slot] = 0.0;
        }
        return slot;
    }
    /** As detach (), but it waits for the lock for at most `limitMs` (a tick of the overlay thread that is stuck must not hold up effClose of the host for ever). True when the instance is detached: after it the overlay thread no
        longer touches `view`; false when the lock could not be had in time: the overlay thread may still use `view`, the caller must keep the instance alive. `left` is then not set. */
    bool detachWithin (mpckit::IInstance* view, int limitMs, size_t& left)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (limitMs);
        while (! mu_.try_lock())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
        std::lock_guard<std::mutex> g (mu_, std::adopt_lock);
        const int slot = slots_.slotOf (view);
        if (slot >= 0)
        {
            ui_[slot] = UiStack();
            uiGroup_[slot] = -1;
        }
        slots_.release (view);
        left = static_cast<size_t> (slots_.total());
        return true;
    }
    /** Returns the number of instances left. After it returns the overlay thread no longer touches `view` (the same lock guards every use of an instance). */
    size_t detach (mpckit::IInstance* view)
    {
        std::lock_guard<std::mutex> g (mu_);
        const int slot = slots_.slotOf (view);
        if (slot >= 0)
        {
            ui_[slot] = UiStack();
            uiGroup_[slot] = -1;
        }
        slots_.release (view);
        return static_cast<size_t> (slots_.total());
    }
    /** The slot of a registered instance, -1 when it has none (tests). */
    int slotOf (const mpckit::IInstance* view)
    {
        std::lock_guard<std::mutex> g (mu_);
        return slots_.slotOf (view);
    }

    // ---- the overlay pages of an instance (ui_stack.h): main -> full(element) -> detail(page) ... Per instance (slot), never saved. The UI (the overlay thread) calls these; the lock makes them safe
    // against attach / detach. The whole stack of an instance closes when its page is no longer on screen (closeUiWhenLeft).
    bool openFull (int slot, int element) { return withUi (slot, [&] (UiStack& u) { return u.openFull (element); }); }
    bool openDetail (int slot, int page) { return withUi (slot, [&] (UiStack& u) { return u.openDetail (page); }); }
    bool back (int slot) { return withUi (slot, [&] (UiStack& u) { return u.back(); }); }
    bool closeAll (int slot) { return withUi (slot, [&] (UiStack& u) { return u.closeAll(); }); }

    /** The x the views cut their picture at (the left edge of what the host draws over the right part of the tab: its Q-Link panel; PanelKey::kNoKey: nothing), and how many times the key came or went (status file, tests). */
    int keyEdge() const noexcept { return keyEdge_; }

    /** One touch event (upright UI pixels), as the touch input hands it over (tests call it directly). */
    void touchEvent (const TouchEvent& ev, double nowMs)
    {
        ++touchEvents_;
        if (nativeParameterModal_)
        {
            noteHostTouch (ev);
            return;
        }
        if (hostGesture_)
        {
            if (ev.kind == TouchEvent::Down)
                hostGesture_ = false;   // a new gesture (an Up was lost): it is ours
            else
            {
                if (ev.kind == TouchEvent::Up)
                    hostGesture_ = false;
                return;   // the rest of a gesture that went down on the host's own screen
            }
        }
        if (swallowTouch (ev))
            return;   // the host is dimmed (standby): this touch wakes it and nothing else, whole gesture included
        if (keyEdge_ < PanelKey::kNoKey && ev.x >= keyEdge_)
            return;   // the host shows through there (its panel): our controls do not take that touch
        std::lock_guard<std::mutex> g (mu_);
        if (current_ < 0 || ! isUiPage (current_) || activeSlot_ < 0)
            return;
        mpckit::IInstance* v = slots_.at (activeSlot_);
        if (v == nullptr)
            return;
        if (views_[static_cast<size_t> (current_)]->onTouch (ev, ui_[activeSlot_], *v, nowMs))
        {
            ++touchTaken_;
            needFrame_ = true;
        }
    }

    /** The mouse: its events and the host's pointer plane (mouse_router.h). Not owned; called before the first tick. */
    void setMouse (IMouseInput* mouse, IPointerSource* pointer)
    {
        if (mouse != nullptr && pointer != nullptr)
            mouse_ = std::make_unique<MouseRouter> (*mouse, *pointer);
        else
            mouse_.reset();
    }
    /** The mouse wheel at a point of the UI (upright UI pixels): the control under it takes it (tests call it directly). */
    void wheelEvent (int x, int y, int notches, double nowMs)
    {
        ++wheelEvents_;
        if (nativeParameterModal_)
            return;
        if (dimmed_.load())
        {
            ++dimTouchSwallowed_;
            return;   // the host is dimmed (standby): the wheel wakes it and nothing else
        }
        if (keyEdge_ < PanelKey::kNoKey && x >= keyEdge_)
            return;   // the host shows through there (its panel): our controls do not take it
        std::lock_guard<std::mutex> g (mu_);
        if (current_ < 0 || ! isUiPage (current_) || activeSlot_ < 0)
            return;
        mpckit::IInstance* v = slots_.at (activeSlot_);
        if (v == nullptr)
            return;
        if (views_[static_cast<size_t> (current_)]->onWheel (x, y, notches, ui_[activeSlot_], *v, nowMs))
        {
            ++wheelTaken_;
            needFrame_ = true;
        }
    }

    /** Plane off, buffers freed (the overlay thread has stopped, or this is the thread). */
    void shutdown() noexcept
    {
        if (gpu_)
            gpu_->shutdown();
        presenter_.hide();
        frames_.release();   // the cached mappings / dma-buf descriptors of the host's framebuffers: not kept while no instance lives
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
    const gpu::GuardRetry& guardRetry() const noexcept { return guardRetry_; }
    const IView* currentView() const noexcept { return current_ >= 0 ? views_[static_cast<size_t> (current_)].get() : nullptr; }
    int currentIndex() const noexcept { return current_; }
    /** The slot whose instance the visible view shows (-1 when nothing is shown), and the number of times the instances were asked to report their slot again. */
    int activeSlot() const noexcept { return activeSlot_; }
    /** Optional: the CPU time of the calling thread in ms (the status file shows the overlay thread's load). */
    void setCpuClock (std::function<double()> clock) { cpuClock_ = std::move (clock); }
    /** Optional: extra "key: value" lines of the status file (the overlay thread's CPU plan, ...), asked for at every write. */
    void setExtraStatus (std::function<std::string()> f) { extraStatus_ = std::move (f); }
    /** The rotation is decided (found from the markers or named by the file). */
    bool rotationKnown() const noexcept { return rotationOverride_ >= 0 || rotationFound_ >= 0; }

    /** One step. Returns after how many milliseconds the next one is due. Never throws. */
    double tick (double nowMs)
    {
        try
        {
            buildViews();   // a no-op after the first tick (the service defers it to here: off the host's thread)
            if (nowMs < lastNowMs_)
                rebaseClock();   // the caller's clock began again (a thread that was started anew): the deadlines of the earlier run would never come
            lastNowMs_ = nowMs;
            tickStartedAt_ = std::chrono::steady_clock::now();
            double wait = step (nowMs);
            if (gpu_)
                wait = guardRetry_.limit (wait, guardWatch_, guardWatchEpoch_);   // woken on the vblank timer ONLY while a guard is pending (counted); at rest the wait is the step's own
            if ((touchIn_ != nullptr || mouse_) && current_ >= 0 && isUiPage (current_) && activeSlot_ >= 0)
                wait = std::min (wait, kTouchPollMs);   // the overlay pages are on screen: look at the touch screen often
            tickMaxMs_ = std::max (tickMaxMs_, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - tickStartedAt_).count());
            return wait;
        }
        catch (...)
        {
            disable ("exception in the overlay thread");
            return 1000.0;
        }
    }

private:
    /** Every deadline and timestamp this object keeps against the caller's clock is forgotten (the clock went back). */
    void rebaseClock() noexcept
    {
        nextReportMs_ = 0.0;
        nextSwitchesMs_ = 0.0;
        nextDetectMs_ = 0.0;
        lastFrameMs_ = -1000.0;
        secondStartMs_ = 0.0;
        lastKickMs_ = 0.0;
        lastWholeMs_ = wholeSinceMs_ = viewSinceMs_ = lastQWriteMs_ = 0.0;
    }

    double step (double nowMs)
    {
        if (disabled())
        {
            if (state_.compare (0, 8, "disabled") != 0)
                disable (presenter_.reason());
            return 1000.0;
        }
        if (gpu_)
        {
            gpu_->poll (nowMs);
            // the helper reported a page guard plane-off: one page detection NOW (this tick, this thread) instead of at the next cycle, so the helper is confirmed at once; at most once per kGuardDetectGapMs
            // (a second report inside the gap waits for the normal cycle)
            if (gpu_->takeGuardReport())
            {
                guardWatch_ = true;   // watched until a page is found (the helper is hidden meanwhile; our own hide of the plane below clears the backend's pending flag, so the watch is ours)
                guardWatchEpoch_ = gpu_->guardEpoch();
                if (nowMs - lastGuardDetectMs_ >= kGuardDetectGapMs)
                {
                    lastGuardDetectMs_ = nowMs;
                    nextDetectMs_ = nowMs;
                }
            }
            // while a guard report waits for its page (the immediate detection found nothing yet) the detection is repeated every vblank, bounded, instead of waiting for the next cycle (gpu/guard_state.h GuardRetry)
            if (guardRetry_.due (guardWatch_, guardWatchEpoch_, nowMs))
            {
                lastGuardDetectMs_ = nowMs;
                nextDetectMs_ = nowMs;
            }
        }
        readTouch (nowMs);   // read now, delivered after the page check below (a touch is judged by the check of its own step)
        // a GPU picture is on the plane: the next scene goes FIRST, before any call that can wait for the display's locks (the page detection below reads the CRTC, which waits while the helper's
        // plane update holds them for a vblank): the scenes then come at the rhythm of the pace grid, whatever the rest of the step takes
        double early = 0.0;
        bool kickedEarly = false;
        int earlyView = -1, earlySlot = -1;
        const bool checkNativeFirst = timing_.nativeTabTouch && hostBgCapable_ && nowMs >= nextDetectMs_;
        if (nativeTabSource_ < 0 && ! nativeParameterModal_ && ! checkNativeFirst && current_ >= 0 && gpu_ && usingGpu_ && gpuShown_ && ! noGpu_ && ! killed_)
        {
            earlyView = current_;
            earlySlot = activeSlot_;
            early = drawCurrent (nowMs);
            kickedEarly = ! disabled();
        }
        if (disabled())
            return 1000.0;
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
            if (disabled())
                return 1000.0;
        }
        int count;
        {
            std::lock_guard<std::mutex> g (mu_);
            count = slots_.total();
        }
        std::string hiddenReason;
        if (killed_)            hiddenReason = "kill switch";
        else if (count == 0)    hiddenReason = "no instance";
        if (! hiddenReason.empty())
        {
            nativeTabSource_ = -1;
            nativeParameterModal_ = false;
            hideCurrentView();
            if (killed_)
                closeUiExcept (-1);   // release the full-screen marker before losing the slot needed to identify this page
            presenter_.hide();
            hideGpu (nowMs);
            current_ = shownView_ = -1;
            activeSlot_ = -1;
            setState ("hidden: " + hiddenReason);
            return timing_.detectEveryMs;
        }

        if (timing_.prewarmGpu && ! prewarmTried_ && gpu_ && ! noGpu_ && ! killed_)
        {
            // The helper's start (EGL, GBM, the first context: 584 ms on the Key 61) in parallel with the host's loading: the overlay thread asks for it now, with no page on screen yet. The helper only
            // takes the plane when a scene is shown; a failure is handled by the backend (back off, then the CPU path) as for any start.
            prewarmTried_ = true;
            if (presenter_.open())
            {
                applyPace();
                startupTimes().markUiStep (StartupTimes::kUiHelperAsked);
                gpu_->request (nowMs);
            }
        }
        if (nowMs >= nextDetectMs_)
        {
            nextDetectMs_ = nowMs + (standby_.busy() ? std::min (timing_.detectEveryMs, kStandbyBusyMs) : timing_.detectEveryMs);
            Detection d;
            const char* nativeEditor = nativeEditorStatus();
            if (rotationKnown())
            {
                const int currentKind = current_ >= 0 ? pages_.page (current_).kind : -1;
                const bool fxActions = currentKind == layout::kKindFx || currentKind == layout::kKindMixer;
                const auto* actions = fxActions ? kNativeFxActionMarkers
                                    : currentKind == layout::kKindPlay ? kNativePlayActionMarkers : nullptr;
                const int nActions = fxActions ? static_cast<int> (std::size (kNativeFxActionMarkers)) : currentKind == layout::kKindPlay ? static_cast<int> (std::size (kNativePlayActionMarkers)) : 0;
                d = nativeEditor != nullptr ? detector_->check (rotation_, kNativeParameterModalMarkers, 3, actions, nActions)
                                            : detector_->check (rotation_);
            }
            else
            {
                // no rotation table: the marker pair of the view on screen is looked for under every rotation; exactly one of them may match
                const RotationSearch rs = detector_->searchRotation();
                d = rs.detection;
                rotationAmbiguous_ = rs.matches > 1;
                if (rs.rotation >= 0)
                {
                    rotationFound_ = rs.rotation;
                    updateRotation (nowMs);
                }
            }
            if (guardWatch_)
                gpu::guardTrace ("plugin", "detect", d.view);
            for (int i = 0; i < d.count && i < Detection::kMaxViews; ++i)
                for (int m = 0; m < 2; ++m)
                    seen_[i][m] = d.seen[i][m];
            if (! d.readable)
            {
                const std::string fatal = frames_.fatalError();
                if (! fatal.empty())
                {
                    disable (fatal);
                    return 1000.0;
                }
                if (! backend_.displayOn())
                    unreadable_ = 0;   // the screen sleeps (no mode, no framebuffer): nothing to read, and nothing wrong
                else if (++unreadable_ >= timing_.unreadableLimit)
                {
                    disable ("the framebuffer cannot be read");
                    return 1000.0;
                }
            }
            else
                unreadable_ = 0;
            if (nativeEditor != nullptr && d.nativeModal && (d.view < 0 || d.view == current_)
                && (d.slot == kSlotUnknown || d.slot == activeSlot_) && holdNativeParameterModal (d.nativeActionPopup))
            {
                if (! nativeParameterModal_)
                {
                    nativeParameterModal_ = true;
                    nativeTabSource_ = -1;
                    presenter_.hide();
                    hideGpu (nowMs);
                    shownView_ = -1;   // onShown republishes the page's Q mapping after the host menu
                    needFrame_ = true;
                }
                setState (nativeEditor);
                deliverTouch (nowMs);
                return kTouchPollMs;
            }
            if (nativeParameterModal_)
            {
                nativeParameterModal_ = false;
                closeNativeActionPopup();
                keys_[static_cast<size_t> (current_)].forget();
                needFrame_ = true;
            }
            instanceSeen_ = d.instanceSeen;
            // the host's Q-Link panel covers the right marker of the page; while it slides its black line is not at its place and no panel look is recognised yet. The key (PanelKey) says where the host's band
            // begins: a view that was on screen and has its left marker only, with the band at or left of the right marker, is the same page under the panel (no time hold: the key follows the pixels)
            // With no reference (a new page, or the reference was dropped) the key cannot say where the band begins; the page is held all the same while the panel is there (its look fully in, or its black line
            // anywhere in the strip: PanelKey::findLine) and the picture is cut at that line (updateKey): better a strip too much than the page blinking off for as long as the panel is up
            bool blindHold = false;
            if (qlinkOpt_.keyOn() && d.view < 0 && d.partialView >= 0 && d.partialView == current_ && current_ >= 0 && rotationKnown() && ! keys_[static_cast<size_t> (current_)].hasReference())
                blindHold = d.partialLook || PanelKey::findLine (frames_, rotation_) < PanelKey::kNoKey;
            if (d.view < 0 && d.partialView >= 0 && d.partialView == current_ && current_ >= 0 && (keyEdge_ <= markers_[d.partialView * 2 + 1].rect.x || blindHold))
            {
                d.view = d.partialView;
                d.slot = seenSlot_;
                d.qlinkPanel = true;
                ++keyHolds_;
            }
            standbyStep (nowMs, d);
            const bool whole = d.view >= 0 && ! d.qlinkPanel;   // both markers seen: nothing of the host over the strip's right marker
            if (whole)
            {
                if (! wasWhole_)
                    wholeSinceMs_ = nowMs;
                lastWholeMs_ = nowMs;
            }
            wasWhole_ = whole;
            // which instance's page it is: the view's own markers are on screen, and the instance picture says whose page it is
            int view = d.view, slot = -1;
            std::string why;
            if (view < 0)
                why = ! d.readable ? "framebuffer not readable" : (! rotationKnown() && rotationAmbiguous_ ? "rotation ambiguous (markers match under several rotations)" : "no view on screen");
            else
            {
                std::lock_guard<std::mutex> g (mu_);
                // A stack that went from open to main since the last check (the Play page came over a full screen page, a page was closed): the instance was still reporting the frame the host widgets are hidden at (a
                // blank picture) and is told the full screen is gone only now; the host's redraw with the slot colour takes 100 to 330 ms (XL), longer than a check. The page is there all the time, so the blank
                // picture keeps the view for that time instead of the overlay going off and coming back (the blank of the Play page after every arrival, measured on the XL 2026-11-21).
                for (int s = 0; s < mpckit::kMaxInstances; ++s)
                {
                    const bool open = ! ui_[s].isMain();
                    if (uiOpenSeen_[s] && ! open)
                        blankGraceUntilMs_[s] = nowMs + kBlankGraceMs;
                    uiOpenSeen_[s] = open;
                }
                if (d.slot == kSlotUnknown && activeSlot_ >= 0 && slots_.at (activeSlot_) != nullptr && (! ui_[activeSlot_].isMain() || nowMs < blankGraceUntilMs_[activeSlot_]) && isUiPage (view))
                {
                    slot = activeSlot_;   // the overlay full screen is open for this instance (or was closed a moment ago): the instance reports the frame the skin hides the host widgets at (a blank picture), by design
                }
                else if (d.slot == kSlotUnknown)
                {
                    why = "instance of the page not identified";
                    askForReports (nowMs);   // the host may not have drawn the instance picture yet, or shows a stale frame until the parameter is reported
                }
                else
                {
                    reportGapMs_ = kReportFirstGapMs;
                    if (d.slot == kSlotNone)
                        why = "instance has no slot (more than 16 instances)";
                    else if (! slots_.at (d.slot))
                        why = "instance slot " + std::to_string (d.slot) + " is not alive";
                    else
                        slot = d.slot;
                }
            }
            seenSlot_ = d.slot;
            closeUiWhenLeft (d, rotationKnown() || ! rotationAmbiguous_);
            if (slot < 0)
                view = -1;
            if (view != current_ || slot != activeSlot_)
            {
                if (current_ >= 0 && view < 0)
                    ++viewHides_;   // the page went off the screen (a blink when the panel comes or goes)
                hideCurrentView();   // the view that was on screen is not any more: its pop-up lists close, its gesture ends
                if (view >= 0 && view != current_)
                {
                    keys_[view].forget();   // another tab or sub-tab: the host's picture under us is another one, the reference is taken again
                    bandWholeSinceMs_ = -1.0;
                }
                viewSinceMs_ = nowMs;
                current_ = view;
                activeSlot_ = slot;
                needFrame_ = true;
            }
            {
                // A view with a background in the host's skin stays OPAQUE (it draws everything, ground included) until the sentinel of that background is read on the host's framebuffer; a page change,
                // a hidden page or a sentinel that is gone puts it back (host_background.h). The view takes the flag at its next update (updateView).
                hostBgCapable_ = current_ >= 0 && backgrounds_[current_].rect.w > 0;
                hostBgOn_ = hostBg_.step (current_, hostBgCapable_, current_ >= 0 && d.sentinelView == current_);
            }
            if (nativeTabFinger_ && (current_ != nativeTabPressedView_ || activeSlot_ != nativeTabSlot_ || ! whole || dimmed_.load()))
                nativeTabPressedView_ = -1;
            if (nativeTabSource_ >= 0)
            {
                if (current_ != nativeTabSource_ || activeSlot_ != nativeTabSlot_ || (! nativeTabPlaneHidden_ && ! hostBgOn_) || ! whole ||
                    dimmed_.load() || nowMs >= nativeTabUntilMs_)
                    nativeTabSource_ = -1;
                else
                {
                    if (! nativeTabPlaneHidden_)
                    {
                        presenter_.hide();
                        hideGpu (nowMs);
                        needFrame_ = true;
                        nativeTabPlaneHidden_ = true;
                        ++nativeTabHandoffs_;
                        setState ("host background during navigation");
                    }
                    nextDetectMs_ = std::min (nextDetectMs_, nowMs + kTouchPollMs);
                }
            }
            if (current_ >= 0)
                guardWatch_ = false;   // a page of ours is on the host framebuffer: the guard report has its answer
            if (current_ < 0)
            {
                presenter_.hide();
                hideGpu (nowMs);
                setState ("hidden: " + why);
            }
            else if (gpu_ && usingGpu_ && gpuShown_)
                gpu_->confirmPage (current_, nowMs);   // a page of ours is on the current host framebuffer: a helper held back by the page guard may draw again (gpu/guard_state.h)
        }
        deliverTouch (nowMs);
        if (nativeParameterModal_)
            return kTouchPollMs;
        updateKey (nowMs);
        if (nativeTabSource_ >= 0)
            return kTouchPollMs;
        if (current_ >= 0)
        {
            if (kickedEarly && current_ == earlyView && activeSlot_ == earlySlot && gpuShown_)
                return early;
            return drawCurrent (nowMs);
        }
        return timing_.detectEveryMs;
    }

    /** The host's standby dim (standby.h), once per detection. The awake markers decide first; without them the half brightness markers of the page held on screen and the backlight decide. A page that is held
        keeps `d` as if its awake markers were on screen (view, slot, panel state): the picture is drawn darkened, the touch is swallowed, the key stays where it was. */
    void standbyStep (double nowMs, Detection& d)
    {
        int bl = 0;
        const bool haveBl = backlight_.read (nowMs, bl);
        blNow_ = haveBl ? bl : -1;
        const bool awake = d.view >= 0;
        const bool havePage = current_ >= 0 && activeSlot_ >= 0 && rotationKnown() && d.readable;
        const bool panel = keyEdge_ < PanelKey::kNoKey;
        const bool look = ! awake && havePage && detector_->dimLook (current_, rotation_, standby::kDimFactor, panel);
        PageDetector::BlurLook blur;
        if (! awake && havePage)
            blur = detector_->blurLook (current_, rotation_, activeSlot_, panel);   // picture-alone evidence (a host that only blurs: the XL) and the host's own brightness
        const standby::Verdict v = standby_.update (nowMs, awake, look, havePage, haveBl, bl, blur.ok);
        if (v.dimmed && (look || blur.measured))
        {
            // our picture is drawn at the host's brightness: measured from its background near the markers (the Key 61 half, the XL 1.0); with no measurement the half of the backlight evidence
            const double f = blur.measured ? blur.factor : (look ? standby::kDimFactor : 1.0);
            if (std::fabs (f - dimFactor_.load()) > 0.004)
            {
                dimFactor_.store (f);
                needFrame_ = true;
            }
        }
        if (v.hold && ! awake)
        {
            d.view = current_;
            d.slot = activeSlot_;
            d.qlinkPanel = keyEdge_ < PanelKey::kNoKey;
        }
        if (v.dimmed != dimmed_.load())
        {
            dimmed_.store (v.dimmed);
            needFrame_ = true;   // the picture is drawn again at the other brightness
            statusDue_ = true;
        }
    }

    /** While the host is dimmed a touch only wakes it: it is not ours, nor is the rest of its gesture (the finger is still down when the picture is awake again). */
    bool swallowTouch (const TouchEvent& ev)
    {
        if (dimmed_.load())
        {
            swallowGesture_ = ev.kind != TouchEvent::Up;
            ++dimTouchSwallowed_;
            return true;
        }
        if (swallowGesture_)
        {
            if (ev.kind == TouchEvent::Down)
                swallowGesture_ = false;   // a new gesture (an Up was lost): it is ours
            else
            {
                if (ev.kind == TouchEvent::Up)
                    swallowGesture_ = false;
                ++dimTouchSwallowed_;
                return true;
            }
        }
        return false;
    }

    std::string standbyStatus() const
    {
        std::ostringstream o;
        o << "backlight_node: " << (backlight_.node().empty() ? "none" : backlight_.node()) << "\nbacklight_value: " << blNow_ << "\nbacklight_awake_reference: " << standby_.awakeBacklight()
          << "\ndimmed: " << (dimmed_.load() ? "yes" : "no") << "\ndim_evidence: " << standby_.evidence() << "\ndim_factor: " << (dimmed_.load() ? dimFactor_.load() : 1.0) << "\ndim_transitions: " << standby_.dims() << "\nwake_transitions: " << standby_.wakes() << "\ndim_hold_ticks: " << standby_.holdTicks()
          << "\ndim_hidden_frames: " << standby_.hiddenTicks() << "\ndim_touch_swallowed: " << dimTouchSwallowed_ << "\n";
        return o.str();
    }

    /** The key of the host's Q-Link panel (panel_key.h), one rule for the Play view and every page. The reference (the host's strip under the page) is taken when the page is settled: both markers seen for a while,
        no panel look, no write of the host to a Q proxy lately (a knob turn brings the panel). Every tick while a view is on screen the strip is compared with it and `keyEdge_` is the x the picture is cut at. A band
        that starts at the left end of the strip, or a band while both markers are whole and still, means that the reference is not the picture under us any more: it is taken again. */
    void updateKey (double nowMs)
    {
        unsigned long writes = 0;
        {
            std::lock_guard<std::mutex> g (mu_);
            if (activeSlot_ >= 0)
                if (mpckit::IInstance* v = slots_.at (activeSlot_))
                    writes = v->qWriteCount();
        }
        if (activeSlot_ != qWriteSlot_)
        {
            qWriteSlot_ = activeSlot_;
            qWrites_ = writes;
        }
        else if (writes != qWrites_)
        {
            qWrites_ = writes;
            lastQWriteMs_ = nowMs;
        }
        if (dimmed_.load())
            return;   // the host's picture is dimmed, the strip does not look like its reference: the cut stays where it was until the wake
        int edge = PanelKey::kNoKey;
        constexpr bool useInhostEdge = false;
        if (current_ >= 0 && rotationKnown() && qlinkOpt_.keyOn() && ! useInhostEdge)
        {
            PanelKey& k = keys_[current_];
            if (! k.hasReference())
            {
                const bool settled = wasWhole_ && nowMs - wholeSinceMs_ >= kKeySettleMs && nowMs - lastWholeMs_ <= kKeyFreshMs && nowMs - viewSinceMs_ >= kKeySettleMs && nowMs - lastQWriteMs_ >= kKeyQuietMs;
                if (settled && k.consider (frames_, rotation_, nowMs, kKeyStableMs, *keyCand_))
                    ++keyReferences_;
                else if (! settled)
                    keyCand_->drop();
                if (! k.hasReference() && current_ >= 0)
                {
                    // no reference to compare with: the panel's black line is looked for in the strip itself (every tick, the detector's look is up to 100 ms old), and while the page is held by the panel (the
                    // markers covered) and no line is found yet, the line the panel has when it is fully in
                    const int line = PanelKey::findLine (frames_, rotation_);
                    if (line < PanelKey::kNoKey || ! wasWhole_)
                    {
                        edge = std::min (kBlindEdge, line);
                        ++keyBlindTicks_;
                    }
                }
            }
            else
            {
                const auto t0 = std::chrono::steady_clock::now();
                const PanelKey::Result r = k.measure (frames_, rotation_);
                const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                keyCostSum_ += ms;
                keyCostMax_ = std::max (keyCostMax_, ms);
                ++keyMeasures_;
                if (r.read)
                {
                    // a band that stays while the detector sees BOTH markers, for kKeyStaleMs without a break: the host's picture under us is another one now. (The time is the time of the band AND the markers
                    // together: a panel that has been up for seconds and now slides out shows the right marker again at once, and that is no reason to drop the reference.)
                    if (r.edge < PanelKey::kNoKey && wasWhole_)
                    {
                        if (bandWholeSinceMs_ < 0.0)
                            bandWholeSinceMs_ = nowMs;
                    }
                    else
                        bandWholeSinceMs_ = -1.0;
                    const bool bandWhileWhole = bandWholeSinceMs_ >= 0.0 && nowMs - bandWholeSinceMs_ >= kKeyStaleMs;
                    if (r.stale || bandWhileWhole)
                    {
                        k.forget();   // not the picture under us any more
                        bandWholeSinceMs_ = -1.0;
                        ++keyRetakes_;
                    }
                    else
                        edge = r.edge;
                }
                else
                    edge = keyEdge_;   // an unreadable look keeps what we had
            }
        }
        if (edge != keyEdge_)
        {
            if ((edge < PanelKey::kNoKey) != (keyEdge_ < PanelKey::kNoKey))
                ++qlinkPanelChanges_;
            keyEdge_ = edge;
            needFrame_ = true;
        }
    }

    /** The picture of the view on screen (current_ >= 0): the GPU scene or the CPU drawing. Returns after how many milliseconds the next step is due. */
    double drawCurrent (double nowMs)
    {
        startupTimes().markUiStep (StartupTimes::kUiPageSeen);
        {
            std::lock_guard<std::mutex> g (mu_);
            if (! slots_.at (activeSlot_))     // the instance whose page is on screen has closed since the last check
            {
                current_ = shownView_ = -1;
                activeSlot_ = -1;
                needFrame_ = true;
            }
        }
        if (current_ < 0)
        {
            presenter_.hide();
            hideGpu (nowMs);
            setState ("hidden: instance closed");
            return timing_.detectEveryMs;
        }
        if (gpu_ && ! noGpu_)
        {
            if (! presenter_.open())     // the helper needs the display descriptor and the panel size
            {
                disable (presenter_.reason());
                return 1000.0;
            }
            applyPace();
            startupTimes().markUiStep (StartupTimes::kUiHelperAsked);
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
        applyPace();
        nextWaitMs_ = timing_.frameEveryMs;
        drawIfChanged (nowMs);
        if (presenter_.disabled())
        {
            disable (presenter_.reason());
            return 1000.0;
        }
        return nextWaitMs_;
    }

    template <class F>
    bool withUi (int slot, F&& f)
    {
        std::lock_guard<std::mutex> g (mu_);
        return slot >= 0 && slot < mpckit::kMaxInstances && slots_.at (slot) != nullptr && f (ui_[slot]);
    }

    /** The views the plugin draws entirely (the Play page with the full-screen pages over it, and the pages of the elements, page_view.h): the stack of an instance lives while one of these is on screen for it. */
    bool isUiPage (int view) const noexcept { return view >= 0 && view < numViews_ && mpckit::isPageKind (pages_.page (view).kind); }

    /** The stack of the instance in `slot` belongs to a group of views: the three views of the Play page share one (the full screen of an element stays open when its sub-tab changes), every page of an
        element has its own. Another group on screen: the stack closes, and a page that always shows its element's page opens it. */
    void ensureUiPage (int slot, int view)
    {
        UiStack& u = ui_[slot];
        const int group = pages_.page (view).kind == mpckit::kPlayKind ? 0 : view + 1;
        if (uiGroup_[slot] != group)
        {
            u.closeAll();
            uiGroup_[slot] = group;
        }
        const int element = views_[static_cast<size_t> (view)]->pageElement();
        if (element >= 0 && (u.isMain() || u.element() != element))
            u.openFull (element);
    }

    /** After a page check: the stack of every instance closes whose page is no longer on screen (the user pressed another function key, Mixer, ...). Only a READABLE check says so: a screen that
        sleeps, an unreadable framebuffer, an ambiguous rotation and an instance picture that is not drawn yet (the page IS there, the instance is not told) change nothing. */
    void closeUiWhenLeft (const Detection& d, bool rotationUsable)
    {
        if (! d.readable || ! rotationUsable)
            return;
        if (d.view >= 0 && isUiPage (d.view) && (d.slot == kSlotUnknown || d.slot == kSlotNone))
            return;
        closeUiExcept (d.view >= 0 && isUiPage (d.view) ? d.slot : -1);
    }

    void closeUiExcept (int keep)
    {
        std::lock_guard<std::mutex> g (mu_);
        for (int s = 0; s < mpckit::kMaxInstances; ++s)
            if (s != keep && ui_[s].closeAll())
            {
                ++uiAutoCloses_;
                if (mpckit::IInstance* v = slots_.at (s))
                    v->setUiFullOpen (false);   // the host widgets of that instance's Play page come back (the instance stops reporting the frame they are hidden at)
            }
    }

    /** Every instance registered with a slot is asked to report "Overlay Instance" again (the controller's lock is held; at most once per gap, which doubles while nothing valid is read). */
    void askForReports (double nowMs)
    {
        if (nowMs < nextReportMs_)
            return;
        for (int s = 0; s < mpckit::kMaxInstances; ++s)
            if (mpckit::IInstance* v = slots_.at (s))
            {
                // An open stack can be a native root. Only its view knows whether the host controls are covered.
                if (ui_[s].isMain())
                    v->setUiFullOpen (false);
                v->requestOverlayReport();
            }
        ++rereports_;
        nextReportMs_ = nowMs + reportGapMs_;
        reportGapMs_ = std::min (reportGapMs_ * 2.0, kReportMaxGapMs);
    }

    std::unique_ptr<IView> makeView (int i)
    {
        return factory_.make (pages_.page (i));   // (the plugin's factory: SynthyBoy's is in pages.h)
    }

    /** The touch screen is read for the overlay pages only while a view of the Play page is on screen (by what the last page check said). */
    bool touchEligible() const noexcept { return ! nativeParameterModal_ && current_ >= 0 && isUiPage (current_) && activeSlot_ >= 0 && presenter_.opened(); }

    const char* nativeEditorStatus() const noexcept
    {
        if (current_ < 0 || activeSlot_ < 0)
            return nullptr;
        switch (pages_.page (current_).kind)
        {
            case layout::kKindPlay: return "hidden: native Play selector";
            case layout::kKindEnvUi: return "hidden: native Env editor";
            case layout::kKindLfoUi: return "hidden: native LFO editor";
            case layout::kKindOscUi: return "hidden: native OSC editor";
            case layout::kKindFilterUi: return "hidden: native Filter editor";
            case layout::kKindMaster: return "hidden: native Master editor";
            case layout::kKindVoice: return "hidden: native Voice editor";
            case layout::kKindMod: return "hidden: native Mod filter";
            case layout::kKindMixer: return hostBgOn_ || nativeParameterModal_ ? "hidden: native Mixer editor" : nullptr;
            case layout::kKindFx: return hostBgOn_ || nativeParameterModal_ ? "hidden: native FX editor" : nullptr;
            default: return nullptr;
        }
    }

    bool holdNativeParameterModal (int actionPopup)
    {
        std::lock_guard<std::mutex> guard (mu_);
        auto* inst = slots_.at (activeSlot_);
        if (inst == nullptr || viewSlot_[current_] != activeSlot_ || viewGeneration_[current_] != slots_.generation (activeSlot_))
            return false;
        views_[static_cast<size_t> (current_)]->setNativeActionPopup (actionPopup >= 0 ? actionPopup : nativeui::playoscui::kOtherPopup);
        if (! nativeParameterModal_)
        {
            inst->setQGroup (nullptr, 0);
            const int kind = pages_.page (current_).kind;
            if (kind == layout::kKindPlay || kind == layout::kKindEnvUi || kind == layout::kKindVoice || kind == layout::kKindMod
                || kind == layout::kKindOscUi || kind == layout::kKindLfoUi || kind == layout::kKindFx || kind == layout::kKindMixer)
                views_[static_cast<size_t> (current_)]->dropGesture();
        }
        return true;
    }

    void closeNativeActionPopup()
    {
        std::lock_guard<std::mutex> guard (mu_);
        if (current_ >= 0 && slots_.at (activeSlot_) != nullptr
            && viewSlot_[current_] == activeSlot_ && viewGeneration_[current_] == slots_.generation (activeSlot_))
            views_[static_cast<size_t> (current_)]->setNativeActionPopup (-1);
    }

    /** A touch event that is not ours (nothing of ours on screen): the host's. A finger that went down on the host is the host's until it is up, also when our page is on screen again meanwhile. */
    void noteHostTouch (const TouchEvent& ev) noexcept
    {
        if (ev.kind == TouchEvent::Down)
            hostGesture_ = true;
        else if (ev.kind == TouchEvent::Up)
            hostGesture_ = false;
    }

    /** Hand the touch to the host for its entire lifetime. The primary plane supplies the
        verified source artwork while its key is held. Only a fresh page check can show
        the new live page or restore a cancelled/lost press to the same instance. */
    bool nativeTabTouch (const TouchEvent& ev, double nowMs)
    {
        if (ev.kind != TouchEvent::Down)
        {
            if (! nativeTabFinger_)
                return false;
            if (ev.kind == TouchEvent::Up)
            {
                nativeTabFinger_ = false;
                if (nativeTabSource_ >= 0)
                    nativeTabUntilMs_ = nowMs + (views_[nativeTabSource_]->onNativeTabKey (ev.x, ev.y) ? 250.0 : 0.0);
                else if (current_ == nativeTabPressedView_ && activeSlot_ == nativeTabSlot_ &&
                         canHandOffTabKey (ev))
                    awaitNativeTab (nowMs, 250.0);   // a long press restored the source before its eventual release
                nextDetectMs_ = nowMs;
            }
            return true;
        }
        nativeTabFinger_ = false;   // a fresh Down also recovers a missing release
        nativeTabSource_ = -1;
        if (! canHandOffTabKey (ev))
            return false;
        nativeTabFinger_ = true;
        nativeTabPressedView_ = current_;
        awaitNativeTab (nowMs, 700.0);   // recover even if the input disappears without an Up
        return true;
    }

    bool canHandOffTabKey (const TouchEvent& ev)
    {
        if (! timing_.nativeTabTouch || ! touchEligible() || ! hostBgOn_ || ! wasWhole_ ||
            dimmed_.load() || keyEdge_ < PanelKey::kNoKey)
            return false;
        const IView& view = *views_[current_];
        if (! view.onNativeTabKey (ev.x, ev.y))
            return false;
        {
            std::lock_guard<std::mutex> g (mu_);
            if (slots_.at (activeSlot_) == nullptr || view.fingerDown() ||
                ui_[activeSlot_].depth() != (view.pageElement() >= 0 ? 1 : 0))
                return false;
        }
        return true;
    }

    void awaitNativeTab (double nowMs, double waitMs)
    {
        nativeTabSource_ = current_;
        nativeTabSlot_ = activeSlot_;
        nativeTabUntilMs_ = nowMs + waitMs;
        nativeTabPlaneHidden_ = false;
        nextDetectMs_ = nowMs;   // verify the host markers, slot and sentinel before handing off our plane
    }

    /** First part of the touch step, before the page check: while our page is on screen (as far as the last check knows) the events are read and kept; with no page they are read and thrown away (they must not
        arrive later, when a page is on screen again: the taps of the host's dialogs). A finger going down asks for a page check in this very step: the touch is delivered after it (deliverTouch). */
    void readTouch (double nowMs)
    {
        for (const TouchEvent& ev : pendingTouch_)   // left over from a step that ended before its delivery: not ours
            noteHostTouch (ev);
        pendingTouch_.clear();
        if (touchIn_ == nullptr)
            return;
        if (touchEligible())
        {
            touchIn_->configure (presenter_.panelWidth(), presenter_.panelHeight(), rotation_);
            bool down = false;
            touchIn_->poll ([this, &down, nowMs] (const TouchEvent& ev)
            {
                if (nativeTabTouch (ev, nowMs))
                    return;
                if (ev.kind == TouchEvent::Down)
                    down = true;
                if (pendingTouch_.size() < kMaxPendingTouch || ev.kind != TouchEvent::Move)
                    pendingTouch_.push_back (ev);
            });
            if (down)
                nextDetectMs_ = nowMs;
        }
        else
            touchIn_->poll ([this, nowMs] (const TouchEvent& ev) { if (! nativeTabTouch (ev, nowMs)) noteHostTouch (ev); });
    }

    /** Second part, after the page check of this step (current_ is what the check found, the one source of the touch and of the picture): the kept events go to the view on screen, or are the host's. A running
        finger gets the clock for the long press. */
    void deliverTouch (double nowMs)
    {
        pollMouse (nowMs);
        if (touchIn_ == nullptr)
            return;
        const bool ours = touchEligible();
        for (const TouchEvent& ev : pendingTouch_)
        {
            if (ours)
                touchEvent (ev, nowMs);
            else
                noteHostTouch (ev);
        }
        pendingTouch_.clear();
        if (! ours)
            return;
        std::lock_guard<std::mutex> g (mu_);
        mpckit::IInstance* v = slots_.at (activeSlot_);
        if (v != nullptr && views_[static_cast<size_t> (current_)]->fingerDown())
        {
            views_[static_cast<size_t> (current_)]->onTick (ui_[activeSlot_], *v, nowMs);
            needFrame_ = true;
        }
    }

    /** The view on screen goes off it (or is replaced): its gesture ends and its pop-up lists close, so a list opened by a touch that came before the check noticed the host's dialog is not open when the page
        comes back. */
    void hideCurrentView()
    {
        if (current_ < 0)
            return;
        std::lock_guard<std::mutex> g (mu_);
        // The view can still hold the last instance pointer after effClose.
        // A detached or reused slot is never a destination for onHidden.
        if (viewSlot_[current_] >= 0 && slots_.at (viewSlot_[current_]) != nullptr
            && viewGeneration_[current_] == slots_.generation (viewSlot_[current_]))
            views_[static_cast<size_t> (current_)]->onHidden();
        views_[static_cast<size_t> (current_)]->setHostBackground (false);   // it left the screen: opaque until its sentinel is read again (host_background.h)
        needFrame_ = true;
    }

    /** The mouse: like the touch screen only while a overlay page is on screen (the events of other times are read and thrown away: they must not arrive later). Left button = a finger, the wheel = steps. */
    void pollMouse (double nowMs)
    {
        if (! mouse_)
            return;
        if (! touchEligible())
        {
            mouse_->idle();             // a drag that was running when the page went is over (the page drops its gesture itself)
            return;
        }
        mouse_->configure (presenter_.panelWidth(), presenter_.panelHeight(), rotation_);
        mouse_->poll (nowMs, timing_.frameEveryMs, [this, nowMs] (const TouchEvent& ev) { touchEvent (ev, nowMs); },
                      [this, nowMs] (int x, int y, int notches) { wheelEvent (x, y, notches, nowMs); });
        std::lock_guard<std::mutex> g (mu_);
        mpckit::IInstance* v = slots_.at (activeSlot_);
        if (v != nullptr && mouse_->buttonDown() && views_[static_cast<size_t> (current_)]->fingerDown())
        {
            views_[static_cast<size_t> (current_)]->onTick (ui_[activeSlot_], *v, nowMs);   // the clock of a long press
            needFrame_ = true;
        }
    }

    /** The part of a picture every renderer shares: the view reads its data from the instance of the slot on screen. false: that instance is gone. */
    bool updateView (double nowMs, bool& changed)
    {
        changed = false;
        std::lock_guard<std::mutex> g (mu_);
        mpckit::IInstance* v = slots_.at (activeSlot_);
        if (! v)
            return false;
        const auto instanceGeneration = slots_.generation (activeSlot_);
        const bool switchedInstance = viewSlot_[current_] != activeSlot_ || viewGeneration_[current_] != instanceGeneration;
        if (switchedInstance)
        {
            // the view last showed another instance (or none): its cached data (curves, cutoff history, smoothing, dot checks) is that one's. A new object starts from nothing.
            if (viewSlot_[current_] >= 0)
            {
                std::unique_ptr<IView> fresh = makeView (current_);
                fresh->adoptCounters (views_[static_cast<size_t> (current_)]->counters());
                views_[static_cast<size_t> (current_)] = std::move (fresh);
            }
            viewSlot_[current_] = activeSlot_;
            viewGeneration_[current_] = instanceGeneration;
            needFrame_ = true;
        }
        if (isUiPage (current_))
            ensureUiPage (activeSlot_, current_);
        IView& view = *views_[static_cast<size_t> (current_)];
        const uint64_t qRevision = v->qBindingRevision();
        const bool refreshQ = switchedInstance || shownView_ != current_ || shownQRevision_ != qRevision;
        if (refreshQ)
        {
            shownView_ = current_;
            shownQRevision_ = qRevision;
            view.onShown();   // publish again when the old page or preset's Q map expired
        }
        const size_t liveBytes = factory_.liveBytes();   // the plugin's live block (SynthyBoy: synthyboy::LiveValues), read once a picture into a buffer the controller owns
        const bool haveLive = liveBytes > 0 && v->readLive (liveBuf_.data(), liveBytes);
        ViewInput in;
        in.instance = v;
        in.live = haveLive ? liveBuf_.data() : nullptr;
        const int base = targetBase_[current_];
        in.targets = haveLive ? factory_.viewTargets (liveBuf_.data(), base, targetCount_[current_]) : nullptr;
        view.setHostBackground (hostBgOn_);   // (host_background.h: transparent only while the sentinel of the host's background is read)
        in.nowMs = nowMs;
        in.rotation = rotation_;
        in.panelEdgeX = keyEdge_;
        in.io = v;
        in.ui = &ui_[activeSlot_];   // under mu_ (held here): the view reads it during update ()
        changed = view.update (in);
        if (refreshQ)
            v->finishQBindingUpdate (qRevision);
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
        if (gpu_ != nullptr)
        {
            const gpu::GpuStatus g = gpu_->status();
            if (g.frames != detailFrames_)    // a new picture of the helper: its render time counts once
            {
                detailFrames_ = g.frames;
                if (g.helperRenderMs > 0.0)
                {
                    renderAcc_ += g.helperRenderMs;
                    ++renderN_;
                }
            }
        }
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
        if (dimmed_.load() && dimFactor_.load() < kNoDarkening)
            standby::dimScene (gpuScene_, dimFactor_.load());   // the host's picture is darkened (Key 61: half): so is ours; a host that only blurs (the XL) leaves ours sharp and bright
        const auto t0 = std::chrono::steady_clock::now();
        // A slow preset or scene build precedes the send and consumes none of the helper acknowledgement timeout.
        const double submittedAtMs = nowMs + std::chrono::duration<double, std::milli> (t0 - tickStartedAt_).count();
        if (! gpu_->show (ui, panel, rotation_, current_, views_[static_cast<size_t> (current_)]->name(), submittedAtMs) || ! gpu_->submit (gpuScene_, submittedAtMs))
            return;     // the helper failed: the next tick draws with the CPU
        if (! gpuShown_)
            gpuFramesAtShow_ = gpu_->status().frames;
        gpuShown_ = true;
        gpuView_ = current_;
        gpuRotation_ = rotation_;
        renderMs_ = 0.8 * renderMs_ + 0.2 * std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        if (lastKickMs_ > 0.0 && nowMs - lastKickMs_ < 1000.0)
            kickGapMaxMs_ = std::max (kickGapMaxMs_, nowMs - lastKickMs_);
        lastKickMs_ = nowMs;
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

    /** The display is known (discovery, measured refresh): the pace of the pictures is the pace grid's. */
    void applyPace()
    {
        if (paceApplied_ || ! presenter_.opened())
            return;
        paceApplied_ = true;
        if (! timing_.followDisplayPace)
            return;
        const PaceParams pp = backend_.paceParams();
        const double grid = pp.stride * pp.periodMs;
        if (grid >= 8.0 && grid <= 200.0)
        {
            timing_.frameEveryMs = grid;
            timing_.gpuFrameEveryMs = 0.8 * grid;
        }
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
        if (! updateView (nowMs, changed))
            return;
        IView& view = *views_[static_cast<size_t> (current_)];   // after updateView: it replaces the object when another instance's page came on screen
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
        if (dimmed_.load() && dimFactor_.load() < kNoDarkening)
            standby::dimCanvas (canvas_.data(), canvas_.size(), dimFactor_.load());
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
        else if (presenter_.asleep())
            nextWaitMs_ = std::max (nextWaitMs_, 250.0);   // the screen sleeps: look again in a quarter of a second, the picture stays wanted (needFrame_)
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
#if ! SYNTHYBOY_MPC_WRITE_FILES
        statusRequested_ = ! paths_.statusRequest.empty() && exists (paths_.statusRequest);
#endif
        if (qlinkOpt_.poll())
        {
            // the option of the host changed (or was read the first time): with it off the pop-up never comes, the key is off entirely: nothing is read, nothing is cut, the references are dropped
            if (! qlinkOpt_.keyOn())
            {
                for (PanelKey& k : keys_)
                    k.forget();
                bandWholeSinceMs_ = -1.0;
            }
            keyOptionChanges_++;
            needFrame_ = true;
        }
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
        int over = -1;
        long v = 0;
        if (safefile::readInt (paths_.rotation.c_str(), v) && (v == 0 || v == 90 || v == 180 || v == 270))   // every switch file is read with safe_file.h: a regular file only, a few bytes, never blocking
            over = static_cast<int> (v);
        rotationOverride_ = over;
        {
            int look = 1;
            long lv = 0;
            if (safefile::readInt (paths_.look.c_str(), lv) && lv >= 1 && lv <= 3)
                look = static_cast<int> (lv);
            if (look != look_)
            {
                look_ = look;
                ui::theme::setLook (look);
                needFrame_ = true;
            }
        }
        if (mouse_)
        {
            char text[safefile::kSwitchBytes];
            if (! safefile::readFirstLine (paths_.pointerHotSpot.c_str(), text, sizeof text))
                text[0] = '\0';
            mouse_->setHotSpotOverride (text);
        }
        chooseDetail();
        chooseRate (nowMs);
        updateRotation (nowMs);
        dumpRequested_ = exists (paths_.dumpRequest);
    }

    /** The level of detail (once a second): the file's when it says one; else "full" while the helper's pictures cost at most kDetailBudgetMs (the mean of the second, 3 seconds in a row over it
        switch to "reduced" for the rest of the run); the CPU drawing, when the helper is not on, is "reduced". The mean is the helper's render time (the CPU side, glFinish included). */
    static constexpr double kDetailBudgetMs = 14.0;
    static constexpr int kDetailMinFrames = 12;
    void chooseDetail()
    {
        int want = 0;
        {
            char w[32];
            if (safefile::readWord (paths_.detail.c_str(), w, sizeof w))
                want = (! std::strcmp (w, "1") || ! std::strcmp (w, "full")) ? 1 : ((! std::strcmp (w, "2") || ! std::strcmp (w, "reduced")) ? 2 : 0);
        }
        detailSource_ = want != 0 ? "file" : "auto";
        // A native tab handoff withdraws scanout while the selected GPU renderer remains ready.
        const bool gpuOn = gpu_ != nullptr && usingGpu_ && gpu_->active() && ! noGpu_;
        if (want == 0)
        {
            if (gpuOn && renderN_ >= kDetailMinFrames)   // only seconds with a moving picture count: a page at rest draws a few pictures at the idle clock of the GPU, which says nothing
            {
                const double mean = renderAcc_ / renderN_;
                detailMeanMs_ = mean;
                overBudget_ = mean > kDetailBudgetMs ? overBudget_ + 1 : 0;
                if (overBudget_ >= 3 && ui::theme::detailLevel() == 1)
                    autoReduced_ = true;
            }
            want = (! gpuOn && current_ >= 0) || autoReduced_ ? 2 : 1;
        }
        renderAcc_ = 0.0;
        renderN_ = 0;
        if (want != ui::theme::detailLevel())
        {
            ui::theme::setDetail (want);   // no frame is asked for: the Play view sees the new level at its next update and says it changed
            statusDue_ = true;
        }
    }

    /** The rate of the pictures (once a second, GPU path): 30 a second is the pace grid of two vblanks; 60 is one picture per vblank (the helper's stride 1: the plane update follows the render at once).
        The file `rate` says 30 or 60; without it the GPUs of the Mali-T series (the Key 61: its frame time at the lowest clock does not fit a sixtieth of a second) keep 30, the others try 60 and keep it
        when the helper really presents 45 or more pictures a second while the picture moves (else back to 30 for two minutes; the plane update of a panel may land one vblank late at random). */
    void chooseRate (double nowMs)
    {
        if (gpu_ == nullptr || ! usingGpu_ || ! gpuShown_)
        {
            clock_.boost (false);
            return;
        }
        const gpu::GpuStatus g = gpu_->status();
        if (! g.active)
        {
            clock_.boost (false);
            return;
        }
        int want = 0;
        {
            char w[32];
            if (safefile::readWord (paths_.rate.c_str(), w, sizeof w))
                want = ! std::strcmp (w, "60") ? 60 : (! std::strcmp (w, "30") ? 30 : 0);
        }
        const bool slowGpu = g.glRenderer.compare (0, 6, "Mali-T") == 0 || g.glRenderer.empty();
        if (want == 0)
            want = slowGpu ? 30 : 60;
        const double dt = nowMs - rateAtMs_;
        const unsigned long pics = g.frames >= rateFrames_ ? g.frames - rateFrames_ : 0;
        rateFrames_ = g.frames;
        rateAtMs_ = nowMs;
        const double fps = dt > 100.0 ? 1000.0 * static_cast<double> (pics) / dt : 0.0;
        // the clock of the GPU: while the picture moves and a picture costs something, the lowest clock the governor may use is raised (gpu_clock.h); it is put back when the picture has rested for three seconds
        {
            const uint32_t dCount = g.render.count - rcCount_, dTotal = g.render.totalUs - rcTotal_;
            rcCount_ = g.render.count;
            rcTotal_ = g.render.totalUs;
            const double meanMs = dCount > 0 ? static_cast<double> (dTotal) / static_cast<double> (dCount) / 1000.0 : 0.0;
            if (char w[16]; safefile::readWord (paths_.gpuClock.c_str(), w, sizeof w) && ! std::strcmp (w, "off"))
                clock_.boost (false);
            else if (fps >= 8.0 && meanMs >= 3.0)
            {
                clock_.boost (true);
                clockIdleMs_ = 0.0;
            }
            else if (fps < 2.0)
            {
                if (clockIdleMs_ <= 0.0)
                    clockIdleMs_ = nowMs;
                else if (nowMs - clockIdleMs_ >= 3000.0)
                    clock_.boost (false);
            }
        }
        rateSource_ = want == 30 ? (slowGpu ? "auto: Mali-T" : "file") : (paths_.rate.empty() ? "auto" : "auto / file");
        if (want == 30 || nowMs < rateRetryMs_)
        {
            if (rateStride_ != 2)
                applyStride (2);
            return;
        }
        if (rateStride_ != 1)
        {
            applyStride (1);
            rateTrialMs_ = nowMs;
            rateConfirmed_ = false;
            return;
        }
        if (! rateConfirmed_ && fps >= 20.0)   // the picture moves: is it presented faster than the grid of two vblanks could?
        {
            if (fps >= 45.0)
                rateConfirmed_ = true;
            else if (nowMs - rateTrialMs_ >= 4000.0)
            {
                if (slowGpu || detailMeanMs_ > 11.0)
                {
                    applyStride (2);
                    rateRetryMs_ = nowMs + 120000.0;
                    rateSource_ = "60 not reached: 30";
                }
                else
                {
                    // a GPU that is not short of time (a picture costs well under a vblank): the pictures a second follow what changes, not what the GPU can do: the full rate stays
                    rateConfirmed_ = true;
                    rateSource_ = "auto: full rate kept (cheap pictures)";
                }
            }
        }
        else if (rateConfirmed_ && fps >= 20.0 && fps < 36.0 && detailMeanMs_ > 11.0)
        {
            applyStride (2);   // it cannot keep up any more (the pictures cost more than a vblank)
            rateRetryMs_ = nowMs + 30000.0;
            rateSource_ = "60 too slow: 30";
        }
    }
    void applyStride (int s)
    {
        rateStride_ = s;
        gpu_->setStride (s);
        const gpu::GpuStatus g = gpu_->status();
        const double period = g.paceKnown && g.pace.periodUs > 0 ? g.pace.periodUs / 1000.0 : 16.67;
        timing_.gpuFrameEveryMs = 0.8 * s * period;   // the scenes follow the grid: a scene a little before each picture
        timing_.frameEveryMs = s * period;
        statusDue_ = true;
    }

    /** The rotation in use: the file's, else the one found from the markers (cached for the life of the process), else the default while searching. */
    void updateRotation (double nowMs)
    {
        const int rot = rotationOverride_ >= 0 ? rotationOverride_ : (rotationFound_ >= 0 ? rotationFound_ : rotation_);
        if (rot != rotation_)
        {
            presenter_.hide();   // set up again with the other orientation
            hideGpu (nowMs);
            rotation_ = rot;
            needFrame_ = true;
        }
    }

    /** Once a second while a picture is on the plane: the plane still reports our state. If another program took it, everything of ours is dropped without touching it and the next free
        plane the discovery found is used (the picture comes back there); with none left the module disables itself. */
    void verifyPlane (double nowMs)
    {
        // the CPU path only: the helper checks its own plane (a GETPLANE from this thread would wait for the lock that the helper's update holds while it waits for the vblank, and delay a scene)
        if (! presenter_.planeOn())
            return;
        std::string why;
        if (backend_.planeStillOurs (why))
        {
            planeNote_ = "verified";
            if (backend_.planeWasSwitchedOff())
            {
                planeNote_ = "was switched off (a mode set of the application?): set again";
                ++planeRefreshes_;
                needFrame_ = true;       // the next step draws and presents the picture again, on the same plane
            }
            return;
        }
        planeNote_ = "lost: " + why;
        ++planeLosses_;
        presenter_.abandon();
        if (gpu_)
            gpu_->abandonPlane();
        gpuShown_ = false;
        gpuView_ = -1;
        needFrame_ = true;
        std::string next;
        if (! backend_.nextPlane (next))
        {
            disable ("the overlay plane was taken by another program and " + next);
            return;
        }
        planeNote_ += "; " + next;
        (void) nowMs;
    }

    void secondTick (double nowMs)
    {
        verifyPlane (nowMs);
        if (disabled())
            return;
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
        tickMaxLast_ = tickMaxMs_;
        kickGapMaxLast_ = kickGapMaxMs_;
        tickMaxMs_ = kickGapMaxMs_ = 0.0;
        framesThisSecond_ = 0;
        secondStartMs_ = nowMs;
        writeStatus (false);
        recomputesAtSecond_ = current_ >= 0 ? views_[static_cast<size_t> (current_)]->counters().recomputes : 0;
        if (dumpRequested_)
            writeDump();
    }

    void writeStatus (bool final_)
    {
#if ! SYNTHYBOY_MPC_WRITE_FILES
        if (! statusRequested_)
            return;   // the release build (write_files.h): no periodic status file unless /tmp/synthyboy-overlay-status-on exists (read once a second in readSwitches)
#endif
        std::ostringstream o;
        o << "state: " << state_ << "\n";
        o << "view: " << (current_ >= 0 && state_ == "visible" ? views_[static_cast<size_t> (current_)]->name() : "none") << "\n";
        o << "guard_retry: detections=" << guardRetry_.detections << " wakeups=" << guardRetry_.wakeups << " attempts_now=" << guardRetry_.attempts << "\n";
        o << "native_tab_handoffs: " << nativeTabHandoffs_ << "\n";
        o << "host_background: " << (hostBgCapable_ ? (hostBgOn_ ? "on (sentinel read)" : "off (sentinel not seen, opaque plane)") : "none") << "\n";
        if (rotationKnown())
            o << "rotation: " << rotation_ << "\nrotation_source: " << (rotationOverride_ >= 0 ? "override file" : "found from the markers") << "\n";
        else
            o << "rotation: unknown\nrotation_source: " << (rotationAmbiguous_ ? "ambiguous (markers match under several rotations)" : "searching (no view of the skin on screen yet)") << "\n";
        {
            std::string disp;
            backend_.describe (disp);
            o << disp;
            std::string note = planeNote_;
            std::string loop;
            if (gpu_ && usingGpu_)
            {
                const gpu::GpuStatus g = gpu_->status();
                note = "verified by the helper after its updates (" + std::to_string (g.pace.planeChecks) + " checks)";
                loop = "loop_wakes: " + std::to_string (g.pace.loopWakes) + "\nloop_wakes_idle: " + std::to_string (g.pace.loopWakesIdle) + "\n";
            }
            o << "display_plane_state: " << note << "\ndisplay_plane_losses: " << planeLosses_ << "\ndisplay_plane_refreshes: " << planeRefreshes_ << "\n" << loop;
        }
        o << standbyStatus();
        {
            std::string qs;
            {
                std::lock_guard<std::mutex> g (mu_);
                if (activeSlot_ >= 0)
                    if (mpckit::IInstance* v = slots_.at (activeSlot_))
                        v->qStatus (qs);
            }
            o << qs;
            std::string vs;
            {
                std::lock_guard<std::mutex> g (mu_);
                if (activeSlot_ >= 0)
                    if (mpckit::IInstance* v = slots_.at (activeSlot_))
                        v->diagnostics (vs);   // (the plugin's own lines: SynthyBoy's voices and held keys)
            }
            o << vs;   // the voices and the keys the engine believes are held (a stuck note shows as orphan_voices > 0 or held_keys > 0 with no key down)
            // the stepping of the Q proxies of the instance on screen: q_step_travel and q_knob_n (raw value, delta, travel)
        }
        if (extraStatus_)
            o << extraStatus_();
        writeGpuStatus (o);
        writePaceStatus (o);
        int count, slotted;
        unsigned alive;
        {
            std::lock_guard<std::mutex> g (mu_);
            count = slots_.total();
            slotted = slots_.slotted();
            alive = slots_.aliveMask();
        }
        o << "instances: " << count << "\n";
        char ib[160];
        std::snprintf (ib, sizeof ib, "instances_with_slot: %d\ninstance_slots_alive: %04x\ninstance_slot: %s\ninstance_pixel_read: %06x\ninstance_slot_read: %s\ninstance_rereports: %lu\n", slotted, alive,
                       current_ >= 0 && state_ == "visible" ? std::to_string (activeSlot_).c_str() : "none", instanceSeen_ & 0xffffff,
                       seenSlot_ == kSlotUnknown ? "unknown" : (seenSlot_ == kSlotNone ? "none" : std::to_string (seenSlot_).c_str()), rereports_);
        o << ib;
        {
            std::lock_guard<std::mutex> g (mu_);
            const UiStack& u = activeSlot_ >= 0 ? ui_[activeSlot_] : ui_[0];
            o << "ui_state: " << (activeSlot_ < 0 || u.isMain() ? "main" : (u.level() == UiStack::Level::full ? "full" : "detail")) << "\nui_depth: " << (activeSlot_ < 0 ? 0 : u.depth())
              << "\nui_auto_closes: " << uiAutoCloses_ << "\ngpu_clock_boost: " << (clock_.boosted() ? "yes" : "no") << "\ngpu_rate_stride: " << rateStride_ << "\ngpu_rate_source: " << rateSource_ << "\nui_look: " << look_ << "\nui_detail: " << ui::theme::D().name << "\nui_detail_source: " << detailSource_ << "\nui_detail_helper_render_ms: " << detailMeanMs_ << "\nqlink_panel: " << (keyEdge_ < PanelKey::kNoKey ? "yes" : "no") << "\nqlink_panel_changes: " << qlinkPanelChanges_ << "\nkey_active: " << (keyEdge_ < PanelKey::kNoKey ? "yes" : "no") << "\nkey_pixels: " << (PanelKey::kNoKey - keyEdge_) << "\nkey_reference_taken: " << keyReferences_ << "\nkey_reference_retakes: " << keyRetakes_ << "\nqlink_status_option: " << qlinkSettingText (qlinkOpt_.value()) << "\nkey_enabled: " << (qlinkOpt_.keyOn() ? "yes" : "no") << "\nqlink_status_option_reads: " << qlinkOpt_.reads() << " (of " << qlinkOpt_.polls() << " checks)\nkey_holds: " << keyHolds_ << "\nkey_blind_ticks: " << keyBlindTicks_ << "\nkey_cost_ms: " << (keyMeasures_ > 0 ? keyCostSum_ / static_cast<double> (keyMeasures_) : 0.0) << " (max " << keyCostMax_ << ")\nview_hides: " << viewHides_ << "\ntouch_input: " << (touchIn_ != nullptr ? "yes" : "no") << "\ntouch_events: " << touchEvents_ << "\ntouch_taken: " << touchTaken_ << "\nwheel_events: " << wheelEvents_ << "\nwheel_taken: " << wheelTaken_ << "\nmouse_input: " << (mouse_ ? "yes" : "no") << "\n" << (mouse_ ? mouse_->describe() : std::string()) << (touchIn_ != nullptr ? touchIn_->describe() : std::string());
        }
        o << "frames_last_second: " << (final_ ? 0u : framesLast_) << "\n";
        o << "frames_total: " << framesTotal() << "\n";
        o << "curve_updates_last_second: " << updatesLast_ << "\ncurve_changes_last_second: " << changesLast_ << "\n";
        o << "overlay_thread_cpu_percent_of_one_core: " << cpuPercent_ << "\n";
        o << "overlay_tick_max_ms_last_second: " << tickMaxLast_ << "\noverlay_scene_gap_max_ms_last_second: " << kickGapMaxLast_ << "\n";
        o << "work_peak_ms: " << workPeakMs_ << "\nrender_ms: " << renderMs_ << "\nrotate_ms: " << rotateMs_ << "\nsetplane_ms: " << setPlaneMs_ << "\n";
        if (current_ >= 0)
        {
            const IView& v = *views_[static_cast<size_t> (current_)];
            o << "full_recomputes_last_second: " << (v.counters().recomputes - recomputesAtSecond_) << "\n";
            v.writeStatus (o);
        }
        for (int i = 0; i < numViews_; ++i)
        {
            const ViewCounters& c = views_[static_cast<size_t> (i)]->counters();
            o << "view_" << pages_.page (i).name << ": updates=" << c.updates << " changes=" << c.changes << " recomputes=" << c.recomputes << " frames=" << viewFrames_[static_cast<size_t> (i)].frames << "\n";
        }
        char buf[200];
        for (int i = 0; i < numViews_; ++i)
        {
            std::snprintf (buf, sizeof buf, "markers_read_%s: left=%06x right=%06x (expected %06x %06x)\n", pages_.page (i).name, seen_[i][0] & 0xffffff, seen_[i][1] & 0xffffff,
                           pages_.page (i).markers[0].rgb, pages_.page (i).markers[1].rgb);
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
        // cumulative render statistics of the helper (the measuring script subtracts two snapshots): pictures, the sums of the microseconds of the parts, of the ops, vertices, draw calls, and the histogram of the time of a picture (1 ms buckets)
        const gpu::RenderBlock& r = g.render;
        o << "gpu_render_count: " << r.count << "\ngpu_render_sums: total_us=" << r.totalUs << " read_us=" << r.readUs << " build_us=" << r.buildUs << " submit_us=" << r.submitUs << " finish_us=" << r.finishUs << " tail_us=" << r.tailUs
          << " ops=" << r.ops << " verts=" << r.verts << " draws=" << r.draws << " cached_ops=" << r.cachedOps << " redrawn=" << r.redrawn << " painted_px=" << r.paintedPx << " full_frames=" << r.fullFrames << "\ngpu_render_hist_ms:";
        for (int i = 0; i < 64; ++i)
            o << " " << r.hist[i];
        o << "\n";
    }

    /** The presentation intervals of the renderer in use: in vblanks and in 2 ms buckets (the pace grid is the point: the intervals of a picture a second are even when one bucket holds them all). */
    void writePaceStatus (std::ostream& o) const
    {
        PaceStats ps;
        bool have = false;
        const char* who = "cpu";
        if (gpu_ && usingGpu_)
        {
            const gpu::GpuStatus g = gpu_->status();
            if (g.paceKnown)
            {
                ps.count = g.pace.count;
                for (int i = 0; i < PaceStats::kVb; ++i) ps.vb[i] = g.pace.vb[i];
                for (int i = 0; i < PaceStats::kMs; ++i) ps.ms[i] = g.pace.ms[i];
                have = true;
                who = "gpu";
            }
        }
        else if (const PaceStats* p = backend_.paceStats())
        {
            ps = *p;
            have = true;
        }
        if (! have)
            return;
        o << "pace_renderer: " << who << "\npace_intervals: " << ps.count << "\npace_interval_vblanks:";
        for (int i = 0; i < PaceStats::kVb; ++i)
            if (ps.vb[i])
                o << " " << i << (i == PaceStats::kVb - 1 ? "+" : "") << "=" << ps.vb[i];
        o << "\npace_interval_ms:";
        for (int i = 0; i < PaceStats::kMs; ++i)
            if (ps.ms[i])
                o << " " << i * 2 << "-" << i * 2 + 2 << "=" << ps.ms[i];
        o << "\n";
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
        safefile::writeReplacing (path.c_str(), text.data(), text.size());   // no symlink followed, mode 0644, one rename
    }

    static constexpr double kStandbyBusyMs = 34.0;   // a dim or a wake is under way (the picture and the backlight do not change together): look at the screen every other vblank
    static constexpr double kTouchPollMs = 12.0;     // the touch screen is read this often while the Play page is on screen
    static constexpr double kMinFrameGapMs = 5.0;    // never faster than this (a present waits for the vblank, 16.7 ms, anyway)
    static constexpr double kSlackMs = 3.0;
    const mpckit::IPageSet& pages_;   // the tabs the plugin draws (kit/page_set.h; SynthyBoy's: the adapter over overlay_layout.h, a test gives its own table)
    mpckit::IPageFactory& factory_;   // makes the views of the pages, knows the live block (the plugin's)
    int numViews_;   // (at most Detection::kMaxViews of them)
    IPlaneBackend& backend_;
    Presenter presenter_;
    IFrameSource& frames_;
    Paths paths_;
    QLinkSettingFile qlinkOpt_ { paths_.qlinkSettings };   // the host's option for the pop-up (qlink_setting.h): read at the start and when the file changes; off: the key does nothing
    Timing timing_;
    gpu::IGpuOverlay* gpu_ = nullptr;
    gpu::SceneData gpuScene_ {};
    bool noGpu_ = false, gpuShown_ = false, usingGpu_ = false, statusDue_ = false;
    int gpuView_ = -1, gpuRotation_ = 0, gpuSkipView_ = -1;
    unsigned long gpuFramesAtSecond_ = 0, gpuFramesAtShow_ = 0;
    std::function<std::string()> extraStatus_;
    std::vector<std::max_align_t> liveBuf_;                // the plugin's live block of this picture (IInstance::readLive), the size the factory says
    standby::Backlight backlight_ { paths_.backlightDir };
    standby::Tracker standby_;
    std::atomic<double> dimFactor_ { standby::kDimFactor };   // the brightness of the host's picture while dimmed (measured, standby.h measureFactor)
    static constexpr double kNoDarkening = 0.98;               // a factor above this: the host's picture is not darkened, ours is drawn as it is
    std::atomic<bool> dimmed_ { false };   // the host's picture is dimmed (standby_): ours is drawn at half brightness, the touch is swallowed
    bool swallowGesture_ = false;
    int blNow_ = -1;
    unsigned long dimTouchSwallowed_ = 0;
    int rotationOverride_ = -1, rotationFound_ = -1;
    bool rotationAmbiguous_ = false, paceApplied_ = false;
    unsigned planeLosses_ = 0, planeRefreshes_ = 0;
    std::string planeNote_ = "not on yet";
    MarkerSpec markers_[Detection::kMaxViews * 2];
    MarkerSpec backgrounds_[Detection::kMaxViews] {};
    Rect regions_[Detection::kMaxViews];
    ITouchInput* touchIn_ = nullptr;
    std::unique_ptr<MouseRouter> mouse_;
    bool autoReduced_ = false, wasWhole_ = false;
    static constexpr double kKeySettleMs = 600.0, kKeyFreshMs = 250.0, kKeyQuietMs = 800.0, kKeyStaleMs = 300.0, kKeyStableMs = 250.0;   // the page must be whole and still this long before the reference is taken, the look at it this fresh, no knob write this lately
    std::unique_ptr<PanelKey::Candidate> keyCand_ { new PanelKey::Candidate() };   // the look of the strip that may become the next reference (one for the view on screen); on the heap with the references: 14 KB each, the controller lives on the stack of some tests
    std::vector<PanelKey> keys_ = std::vector<PanelKey> (Detection::kMaxViews);                 // the reference of the strip under each view (taken again on a view change)
    int keyEdge_ = PanelKey::kNoKey;
    static constexpr int kBlindEdge = PageDetector::kPanelLineX - 2;   // the cut while there is no reference: the left edge of the panel when it is fully in, less the grown edge
    double lastWholeMs_ = 0.0, wholeSinceMs_ = 0.0, bandWholeSinceMs_ = -1.0, viewSinceMs_ = 0.0, lastQWriteMs_ = 0.0, keyCostSum_ = 0.0, keyCostMax_ = 0.0;
    unsigned long qWrites_ = 0, keyReferences_ = 0, keyRetakes_ = 0, keyHolds_ = 0, keyMeasures_ = 0, keyBlindTicks_ = 0, keyOptionChanges_ = 0, viewHides_ = 0, qlinkPanelChanges_ = 0;
    int qWriteSlot_ = -1;
    double renderAcc_ = 0.0, detailMeanMs_ = 0.0;
    GpuClock clock_;                                      // the lowest clock the GPU's governor may use, raised while the picture moves
    uint32_t rcCount_ = 0, rcTotal_ = 0;
    double clockIdleMs_ = 0.0;
    unsigned long rateFrames_ = 0;                       // the rate of the pictures (chooseRate): the helper's picture count and the time of the last look, the stride in use, the trial of 60 a second
    double rateAtMs_ = 0.0, rateTrialMs_ = 0.0, rateRetryMs_ = 0.0;
    int rateStride_ = 2;
    bool rateConfirmed_ = false;
    const char* rateSource_ = "auto";
    int renderN_ = 0, overBudget_ = 0;
    unsigned long detailFrames_ = 0;
    const char* detailSource_ = "auto";
    int look_ = 1;
    std::vector<TouchEvent> pendingTouch_;   // read at the start of the step, delivered after its page check
    bool hostGesture_ = false;               // a finger is down that went down while no page of ours was on screen
    static constexpr size_t kMaxPendingTouch = 256;
    unsigned long touchEvents_ = 0, touchTaken_ = 0, wheelEvents_ = 0, wheelTaken_ = 0;
    Rect instanceRects_[Detection::kMaxViews];
    int targetBase_[Detection::kMaxViews] = {};
    int targetCount_[Detection::kMaxViews] = {};
    std::vector<int> targets_;
    std::vector<std::unique_ptr<IView>> views_;
    struct ViewFrames { unsigned long frames = 0, changes = 0; };
    std::vector<ViewFrames> viewFrames_ = std::vector<ViewFrames> (Detection::kMaxViews);
    std::unique_ptr<PageDetector> detector_;
    HostBackgroundGate hostBg_;      // overlay thread: the view with a background in the host's skin draws transparent only while its sentinel is on the host's framebuffer
    bool hostBgOn_ = false, hostBgCapable_ = false;
    int nativeTabSource_ = -1, nativeTabPressedView_ = -1, nativeTabSlot_ = -1;
    bool nativeTabFinger_ = false, nativeTabPlaneHidden_ = false;
    double nativeTabUntilMs_ = 0.0;
    unsigned long nativeTabHandoffs_ = 0;
    bool prewarmTried_ = false;   // the pre-warm of the helper was tried once (Timing::prewarmGpu)
    [[maybe_unused]] bool statusRequested_ = false;   // SYNTHYBOY_MPC_WRITE_FILES 0 only: the request file existed at the last look
    bool viewsBuilt_ = false;   // buildViews () has run (the overlay thread's first tick when the constructor was told to defer)
    std::vector<uint32_t> canvas_;

    std::mutex mu_;
    InstanceSlots slots_;                    // under mu_
    UiStack ui_[mpckit::kMaxInstances];      // under mu_: the overlay pages of the instance in each slot
    int uiGroup_[mpckit::kMaxInstances];     // under mu_: the group of views (ensureUiPage) the stack of each slot belongs to; -1: none yet
    bool uiOpenSeen_[mpckit::kMaxInstances] = {};         // under mu_: the stack of each slot was not in main at the last page check
    double blankGraceUntilMs_[mpckit::kMaxInstances] = {};   // under mu_: until when a blank instance picture still keeps the page of that slot (the stack went to main at a check)
    unsigned long uiAutoCloses_ = 0;         // stacks closed because the page left the screen (under mu_)
    int viewSlot_[Detection::kMaxViews];        // the slot whose instance each view object last read (-1: none yet); overlay thread
    unsigned long viewGeneration_[Detection::kMaxViews] = {};   // ... and which holder of that slot it was (InstanceSlots::generation): a slot given to a NEW instance is not the same instance
    int activeSlot_ = -1, seenSlot_ = kSlotUnknown;   // the instance of the visible view / what the last check read (the status file)
    uint32_t instanceSeen_ = 0xffffffffu;
    unsigned long rereports_ = 0;
    double nextReportMs_ = 0.0, reportGapMs_ = 400.0, lastNowMs_ = 0.0;
    static constexpr double kReportFirstGapMs = 400.0, kReportMaxGapMs = 3200.0;
    static constexpr double kGuardDetectGapMs = 16.0;   // the shortest time between two page detections asked for by the helper's page guard reports
    double lastGuardDetectMs_ = -1000.0;
    gpu::GuardRetry guardRetry_;
    bool guardWatch_ = false;          // a helper guard report waits for the page detection to find a page (overlay thread)
    uint32_t guardWatchEpoch_ = 0;
    static constexpr double kBlankGraceMs = 600.0;   // a blank instance picture keeps the page this long after the stack went to main (the host's redraw after the instance's report; 330 ms the longest on the XL)

    std::string state_ = "hidden: starting";
    std::function<double()> cpuClock_;
    bool killed_ = false, disabledHere_ = false, dumpRequested_ = false, needFrame_ = true;
    int current_ = -1, shownView_ = -1;   // shownView_: the view whose update () ran last (onShown () when it changes)
    uint64_t shownQRevision_ = 0;
    bool nativeParameterModal_ = false;
    int rotation_ = 90, unreadable_ = 0;
    uint32_t seen_[Detection::kMaxViews][2] = {};
    double nextSwitchesMs_ = 0.0, nextDetectMs_ = 0.0, lastFrameMs_ = -1000.0, secondStartMs_ = 0.0, cpuAtSecond_ = 0.0, cpuPercent_ = 0.0;
    unsigned framesThisSecond_ = 0, framesLast_ = 0, updatesThisSecond_ = 0, updatesLast_ = 0, changesThisSecond_ = 0, changesLast_ = 0;
    double tickMaxMs_ = 0.0, tickMaxLast_ = 0.0, kickGapMaxMs_ = 0.0, kickGapMaxLast_ = 0.0, lastKickMs_ = 0.0;
    std::chrono::steady_clock::time_point tickStartedAt_;
    double nextWaitMs_ = 33.3, workPeakMs_ = 0.0, renderMs_ = 0.0, rotateMs_ = 0.0, setPlaneMs_ = 0.0;
    unsigned long fullThisSecond_ = 0, recomputesAtSecond_ = 0;
};

}} // namespace synthyboy_mpc::overlay
