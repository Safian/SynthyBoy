// SynthyBoy - MPC in-host drawing (experimental, build switch SYNTHYBOY_MPC_INHOST_DRAW, default OFF): the process wide runtime of the second presentation path. Linux only.
//   - the hook: the first plugin instance replaces the host's GOT slot of drmModeAtomicCommit (slot_hook.h) by hookEntry (), the last one puts the original back and waits until no thread is inside our function;
//   - the buffers of the host: the two dumb buffers it flips are mapped (GETFB2 + MAP_DUMB through a descriptor of our own) by the writer thread, never in the hook;
//   - the writer thread ("sb-inhost"): maps buffers when the hook names one it does not know, writes our changed rows into the buffer on screen while the host is idle (flip_core.h frontPass), polls the backlight
//     (the standby dim), the kill-switch file and the fall back decision;
//   - the status text (counters, address checks, why the hook is not installed).
// Order of removal (the host must never jump into unmapped code): state -> Draining (the hook is a pass-through from this instant) -> slot restored -> wait until the in-flight counter is 0 (a thread inside
// hookEntry () has left our code but for its return) -> writer thread joined -> mappings and memory freed. The plugin's effClose reaches this (Service::detach of the last instance) before it returns, so the library
// cannot be unloaded while the hook is in. If the slot cannot be restored (another module hooked on top) or a thread does not leave, the library is pinned (dlopen RTLD_NODELETE) so the code stays mapped.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#if defined(__linux__)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <time.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

#include "flip_core.h"
#include "policy.h"
#include "request_parse.h"
#include "slot_hook.h"

namespace synthyboy_mpc { namespace overlay { namespace inhost {

inline uint32_t monotonicUs() noexcept
{
    timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t> (static_cast<uint64_t> (ts.tv_sec) * 1000000ull + static_cast<uint64_t> (ts.tv_nsec) / 1000ull);
}

struct RuntimePaths
{
    std::string killSwitch = "/tmp/synthyboy-overlay-inhost-off";
    std::string backlightDir = "/sys/class/backlight";
};

class Runtime
{
public:
    using CommitFn = int (*) (int, void*, uint32_t, void*);
    enum State : int { kOff = 0, kArmed = 1, kDraining = 2, kFaulted = 3 };

    static Runtime& get()
    {
        static Runtime* r = new Runtime();   // lives to the end of the process on purpose (the hook may run until the last moment)
        return *r;
    }

    RuntimePaths paths;

    // ---- life (Service: every attach / detach of an instance) ----
    /** The first call installs the hook. true: the hook is in place (or was already). The reason of a refusal is in statusText (). */
    bool acquire()
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (refs_++ == 0)
            installed_ = installLocked();
        return installed_;
    }
    /** The last call removes the hook (and everything else: thread, mappings, memory). */
    void release()
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (refs_ == 0)
            return;
        if (--refs_ == 0)
        {
            if (installed_ || configured_ || state_.load() != kOff)
                removeLocked();
            installed_ = false;
        }
    }
    /** The process ends with instances open (atexit). */
    void shutdownAtExit()
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (installed_ || state_.load() != kOff)
            removeLocked();
        refs_ = 0;
        installed_ = false;
    }

    // ---- the overlay thread ----
    /** Called when the display is known (the backend's open ()): maps nothing yet, builds the tables, starts the writer thread. */
    bool configure (int hostFd, int ownFd, uint32_t primaryPlane, int panelW, int panelH, int vblankPipe, std::string& why)
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        if (! installed_)
        {
            why = "the hook is not installed: " + installNote_;
            return false;
        }
        if (configured_)
            return state_.load() == kArmed;
        if (panelW <= 0 || panelH <= 0 || primaryPlane == 0 || ownFd < 0)
        {
            why = "the display is not known";
            return false;
        }
        ownFd_ = ownFd;
        hostFd_ = hostFd;
        vblankPipe_ = vblankPipe;
        {   // the copy variant (default memcpy): the file holds a name; the bench tool tools/ihbench tells which one is fastest
            FILE* cf = std::fopen ("/tmp/synthyboy-overlay-inhost-copy", "r");
            char name[32] = {};
            if (cf)
            {
                if (std::fscanf (cf, "%31s", name) != 1) name[0] = 0;
                std::fclose (cf);
            }
            static const char* const kNames[] = { "memcpy", "neon64", "neon64np", "neon128", "u32", "u32pf" };
            int pick = 0;
            for (int i = 0; i < 6; ++i)
                if (std::strcmp (name, kNames[i]) == 0) pick = i;
            copyChoice().store (pick);
            copyName_ = kNames[pick];
        }
        core_.waitVblank = &Runtime::waitVblankThunk;
        core_.waitVblankCtx = this;
        primary_ = primaryPlane;
        W_ = panelW;
        H_ = panelH;
        fbProp_ = propertyId (primary_, "FB_ID");   // FB_ID is an atomic-only property: asked through the host's own descriptor (it has the atomic capability), then through ours with the capabilities set
        propSource_ = propSourceNow_;
        // (a property that cannot be looked up is learned from the host's own request in the hook: the item of the primary plane whose value is the framebuffer the plane shows now)
        if (! ring_.allocate (static_cast<size_t> (W_) * static_cast<size_t> (H_), H_))
        {
            why = "no memory for the picture ring";
            return false;
        }
        plan_[0].build (90, W_, H_);
        planIdx_.store (0);
        core_.reset();
        core_.counters.fbRemaps.store (0);
        fbs_.clear();
        wantMap_.store (0);
        policy_.reset();
        faultReason_.clear();
        stopWriter_ = false;
        writerWake_ = false;
        configured_ = true;
        writer_ = std::thread ([this] { writerLoop(); });
        state_.store (kArmed, std::memory_order_seq_cst);   // the hook starts to act from here
        return true;
    }
    bool armed() const noexcept { return state_.load (std::memory_order_acquire) == kArmed; }
    bool faulted() const noexcept { return state_.load (std::memory_order_acquire) == kFaulted; }
    int state() const noexcept { return state_.load(); }
    FrameRing& ring() noexcept { return ring_; }
    FlipCore& core() noexcept { return core_; }

    /** The overlay thread: the next picture is about to be published (view of the table, slot, rotation): the plan of sample points follows the rotation. */
    void noteRotation (int rotation) noexcept
    {
        const SamplePlan& cur = plan_[planIdx_.load()];
        if (cur.rotation() == normaliseRotation (rotation))
            return;
        const int next = 1 - planIdx_.load();
        plan_[next].build (rotation, W_, H_);
        planIdx_.store (next, std::memory_order_seq_cst);
        frontNeedsLook_.store (true);   // the writer thread looks at the frame on screen again under the new rotation
    }
    /** The overlay thread published a picture: the writer thread looks at it. */
    void wakeWriter() noexcept
    {
        writerWake_.store (true, std::memory_order_release);
        wake_.notify_one();
    }
    int hostEdge() const noexcept { return armed() ? core_.edgeNow.load (std::memory_order_relaxed) : PanelKey::kNoKey; }
    /** The snapshot the overlay thread's frame source reads (false: the hook has not seen a page yet, or is not on). */
    bool snapshotAny (Snapshot& s, const SamplePlan*& plan) const noexcept
    {
        if (! armed())
            return false;
        int rot = 0;
        if (! core_.snapshot.load (s, rot))
            return false;
        plan = &plan_[planIdx_.load()];
        return plan->rotation() == rot && s.count == plan->count();
    }
    bool kill() const noexcept { return ::access (paths.killSwitch.c_str(), F_OK) == 0; }

    /** The overlay thread asks to give up the in-host path now (kill switch, a status of the fall back): the hook is taken out, the writer stops. */
    void fallBack (const std::string& why)
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        faultLocked (why);   // (a caller that is not the writer thread: the overlay thread; the writer is not joined here)
    }
    std::string faultReason() const
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        return faultReason_;
    }

    std::string statusText() const
    {
        std::lock_guard<std::mutex> g (lifeMu_);
        const Counters& c = core_.counters;
        const uint32_t flips = c.flips.load(), copied = c.copied.load(), fw = c.frontWrites.load();
        char b[3600];
        static const char* const kStateName[] = { "off", "armed", "draining", "faulted" };
        std::snprintf (b, sizeof b,
                       "inhost_build: yes\ninhost_state: %s\ninhost_hook_installed: %s\ninhost_install_note: %s\ninhost_fault: %s\n"
                       "inhost_slot: %p -> %p (expected libdrm function %p in %s)\ninhost_slot_protection: %s\ninhost_request_stride: %zu\ninhost_primary_plane: %u (FB_ID property %u)\n"
                       "inhost_hook_entries: %u\ninhost_importers_of_commit: %s\ninhost_fb_prop_source: %s\ninhost_host_fd: %d\ninhost_mapped_buffers: %d\ninhost_flips_seen: %u\ninhost_flips_with_page: %u\ninhost_frames_copied: %u\ninhost_frames_missed: %u\ninhost_copies_clipped: %u\ninhost_copies_dimmed: %u\n"
                       "inhost_unidentified_flips: %u\ninhost_commits_without_fb: %u\ninhost_read_phase_ms: %.3f (max %.3f)\ninhost_copy_variant: %s\ninhost_background_fills: %u\ninhost_erased_pictures: %u\ninhost_vblank_waits: %u\ninhost_copy_ms: %.3f (max %.3f)\ninhost_hook_ms: %.3f (max %.3f)\ninhost_hook_over_limit: %u\n"
                       "inhost_front_writes: %u\ninhost_front_full_writes: %u\ninhost_front_write_ms: %.3f (max %.3f)\ninhost_front_rows: %u\ninhost_front_retries: %u\n"
                       "inhost_fallbacks: %u\ninhost_hook_installs: %u\ninhost_hook_removals: %u\ninhost_buffer_remaps: %u\ninhost_busy_skips: %u\ninhost_backlight_dim: %s\ninhost_kill_switch: %s\ninhost_ring_readers: %d\n",
                       kStateName[state_.load() & 3], installed_ ? "yes" : "no", installNote_.c_str(), faultReason_.empty() ? "none" : faultReason_.c_str(),
                       slotAddr_, hookAddr_, expectedAddr_, expectedPath_.c_str(), protNote_.c_str(), stride_, primary_, fbProp_.load(),
                       entries_.load(), importers_.c_str(), propSource_.c_str(), hostFd_, fbs_.count(), flips, c.pageFlips.load(), copied, c.missed.load(), c.clipped.load(), c.dimmedCopies.load(),
                       c.unidentified.load(), c.noFbItem.load(), flips ? c.readUsSum.load() / 1000.0 / flips : 0.0, c.readUsMax.load() / 1000.0, copyName_.c_str(), c.bgFills.load(), c.erased.load(), c.vblankWaits.load(), copied ? c.copyUsSum.load() / 1000.0 / copied : 0.0, c.copyUsMax.load() / 1000.0, flips ? c.hookUsSum.load() / 1000.0 / flips : 0.0, c.hookUsMax.load() / 1000.0, c.overLimit.load(),
                       fw, c.frontFull.load(), fw ? c.frontUsSum.load() / 1000.0 / fw : 0.0, c.frontUsMax.load() / 1000.0, c.frontRows.load(), c.frontRetries.load(),
                       c.fallbacks.load(), c.installs.load(), c.removals.load(), c.fbRemaps.load(), c.busySkips.load(), core_.backlightDim.load() ? "dimmed" : "normal", kill() ? "present" : "absent", ring_.readersNow());
        return b;
    }

    // ---- test seams of the install (the checks) are in installLocked (); the pieces are in slot_hook.h and request_parse.h ----

private:
    Runtime() : core_ (fbs_, ring_, &monotonicUs), refs_ (0) {}

    // ======== the hook ========
    static int hookEntry (int fd, void* req, uint32_t flags, void* user)
    {
        Runtime& rt = get();
        rt.inflight_.fetch_add (1, std::memory_order_seq_cst);
        rt.entries_.fetch_add (1, std::memory_order_relaxed);
        CommitFn orig = rt.orig_;
        bool acted = false;
        if (rt.state_.load (std::memory_order_seq_cst) == kArmed)
        {
            if (rt.core_.tryEnter())
            {
                acted = rt.beforeCommit (req, flags);
                if (! acted)
                    rt.core_.leave();
            }
            else
                Counters::bump (rt.core_.counters.busySkips);
        }
        const int ret = orig (fd, req, flags, user);
        if (acted)
        {
            rt.core_.afterCommit (ret);
            rt.core_.leave();
        }
        rt.inflight_.fetch_sub (1, std::memory_order_seq_cst);
        return ret;
    }

    /** true: the hook has work to finish after the commit (afterCommit). */
    bool beforeCommit (const void* req, uint32_t flags) noexcept
    {
        if (flags & 0x100u)   // DRM_MODE_ATOMIC_TEST_ONLY
            return false;
        uint64_t v = 0;
        uint32_t prop = fbProp_.load (std::memory_order_relaxed);
        if (prop == 0)
        {
            uint32_t learned = 0;
            const uint32_t hint = fbHint_.load (std::memory_order_relaxed);
            if (hint != 0 && findPropByValue (req, stride_, primary_, hint > 1 ? hint - 1 : hint, hint + 1, learned))
            {
                fbProp_.store (learned, std::memory_order_relaxed);
                propSource_ = "learned from the host's request";
                prop = learned;
            }
        }
        if (prop == 0 || ! findItem (req, stride_, primary_, prop, v) || v == 0 || v > 0xffffffffull)
        {
            if ((flags & 1u) != 0u)   // a page flip event: a frame
            {
                Counters::bump (core_.counters.noFbItem);
                judge (policy_.onUnparsed());
            }
            return false;
        }
        const SamplePlan& plan = plan_[planIdx_.load()];
        const FlipCore::FlipResult r = core_.onFlip (static_cast<uint32_t> (v), plan);
        if (! r.hadFb)
            return false;
        if (! r.identified)
            wantMap_.store (static_cast<uint32_t> (v), std::memory_order_release);   // the writer thread maps it
        if (r.hookUs > limits_.slowUs)
            Counters::bump (core_.counters.overLimit);
        judge (policy_.onFlip (r.hookUs, r.identified));
        return true;
    }
    void judge (FallbackPolicy::Verdict verdict) noexcept
    {
        if (verdict == FallbackPolicy::Verdict::Ok)
            return;
        // the hook does not act any more from this instant; the writer thread takes the hook out
        state_.store (kFaulted, std::memory_order_seq_cst);
        faultWhy_.store (static_cast<int> (verdict), std::memory_order_release);
        writerWake_.store (true, std::memory_order_release);
    }

    // ======== install / remove ========
    bool installLocked()
    {
        installNote_ = "";
        { std::string imp; findMainProgramSlots ("drmModeAtomicCommit", &imp); importers_ = imp.empty() ? "none" : imp; }
        const SlotSearch found = findMainProgramSlots ("drmModeAtomicCommit");
        if (found.count != 1)
        {
            installNote_ = "the main program has " + std::to_string (found.count) + " jump slots for drmModeAtomicCommit (exactly 1 expected)" + (found.note.empty() ? "" : " (" + found.note + ")");
            return false;
        }
        void* expected = dlsym (RTLD_DEFAULT, "drmModeAtomicCommit");
        if (expected == nullptr)
        {
            installNote_ = "drmModeAtomicCommit is not resolvable";
            return false;
        }
        Dl_info di;
        std::memset (&di, 0, sizeof di);
        expectedPath_ = dladdr (expected, &di) && di.dli_fname ? di.dli_fname : "?";
        if (expectedPath_.find ("libdrm") == std::string::npos)
        {
            installNote_ = "the function is in " + expectedPath_ + ", not in libdrm";
            return false;
        }
        size_t stride = selfTestRequestLayout();
        if (stride == 0)
        {
            installNote_ = "libdrm's atomic request could not be read back (layout not recognised)";
            return false;
        }
        stride_ = stride;
        expectedAddr_ = expected;
        hookAddr_ = reinterpret_cast<void*> (&Runtime::hookEntry);
        slotAddr_ = found.slots[0];
        orig_ = reinterpret_cast<CommitFn> (expected);
        LinuxProtect prot;
        Protection p;
        if (prot.query (reinterpret_cast<uintptr_t> (found.slots[0]), p))
            protNote_ = p.text();
        std::string why;
        if (! patch_.install (found.slots[0], expected, hookAddr_, prot, static_cast<size_t> (sysconf (_SC_PAGESIZE)), why))
        {
            installNote_ = why;
            return false;
        }
        installNote_ = "installed";
        Counters::bump (core_.counters.installs);
        return true;
    }

    /** libdrm builds a request with three known items; the reader of request_parse.h must find them (and says the item size). */
    static size_t selfTestRequestLayout()
    {
        using AllocFn = void* (*) ();
        using AddFn = int (*) (void*, uint32_t, uint32_t, uint64_t);
        using FreeFn = void (*) (void*);
        AllocFn alloc = reinterpret_cast<AllocFn> (dlsym (RTLD_DEFAULT, "drmModeAtomicAlloc"));
        AddFn add = reinterpret_cast<AddFn> (dlsym (RTLD_DEFAULT, "drmModeAtomicAddProperty"));
        FreeFn freeFn = reinterpret_cast<FreeFn> (dlsym (RTLD_DEFAULT, "drmModeAtomicFree"));
        if (! alloc || ! add || ! freeFn)
            return 0;
        void* req = alloc();
        if (! req)
            return 0;
        static const uint32_t items[3][2] = { { 0x1234u, 0x55u }, { 0x1235u, 0x56u }, { 0x1234u, 0x57u } };
        static const uint64_t values[3] = { 0x1122334455667788ull, 0x99ull, 0xdeadbeefull };
        for (int i = 0; i < 3; ++i)
            if (add (req, items[i][0], items[i][1], values[i]) < 0)
            {
                freeFn (req);
                return 0;
            }
        const size_t s = detectStride (req, items, values, 3);
        freeFn (req);
        return s;
    }

    void removeLocked()
    {
        state_.store (kDraining, std::memory_order_seq_cst);   // the hook is a pass-through from here
        LinuxProtect prot;
        std::string why;
        bool pin = false;
        if (patch_.installed() && ! patch_.remove (prot, why))
        {
            pin = true;
            installNote_ = "removal incomplete: " + why;
        }
        // nobody may be inside our function any more
        for (int i = 0; i < 400 && inflight_.load (std::memory_order_seq_cst) != 0; ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        if (inflight_.load (std::memory_order_seq_cst) != 0)
        {
            pin = true;
            installNote_ = "a thread was still inside the hook after 400 ms";
        }
        stopWriterLocked();
        releaseMemoryLocked();
        if (pin)
            pinLibrary();
        else
            installNote_ = "removed";
        Counters::bump (core_.counters.removals);
        state_.store (kOff, std::memory_order_seq_cst);
    }
    /** Takes the hook out because it misbehaved (or the kill switch asks): the plane overlay carries on. lifeMu_ is held. The picture ring, the writer thread and the mappings stay until the last instance goes. */
    void faultLocked (const std::string& why)
    {
        if (faultReason_.empty())
            faultReason_ = why;
        if (state_.load() != kArmed)
            return;
        state_.store (kFaulted, std::memory_order_seq_cst);   // the hook does not act from this instant
        Counters::bump (core_.counters.fallbacks);
        LinuxProtect prot;
        std::string w;
        bool pin = false;
        if (patch_.installed() && ! patch_.remove (prot, w))
        {
            pin = true;
            installNote_ = "removal incomplete: " + w;
        }
        for (int i = 0; i < 400 && inflight_.load() != 0; ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        if (inflight_.load() != 0)
            pin = true;
        if (pin)
            pinLibrary();
        else
            installNote_ = "removed after a fall back";
        Counters::bump (core_.counters.removals);
    }
    static void pinLibrary()
    {
        Dl_info di;
        if (dladdr (reinterpret_cast<void*> (&Runtime::get), &di) && di.dli_fname)
            dlopen (di.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);
    }
    void stopWriterLocked()
    {
        if (writer_.joinable())
        {
            {
                std::lock_guard<std::mutex> g (wakeMu_);
                stopWriter_ = true;
            }
            wake_.notify_all();
            writer_.join();
        }
    }
    void releaseMemoryLocked()
    {
        fbs_.clear();
        for (Mapping& m : maps_)
            if (m.map)
                munmap (m.map, m.len);
        maps_.clear();
        ring_.invalidate();
        ring_.release();
        core_.reset();
        configured_ = false;
    }

    // ======== buffers of the host ========
    struct Mapping { uint32_t id = 0; uint8_t* map = nullptr; size_t len = 0; ino_t ino = 0; dev_t dev = 0; };

    uint32_t propertyId (uint32_t plane, const char* name)
    {
        propSourceNow_ = "host descriptor";
        uint32_t id = propertyIdOn (hostFd_, plane, name);
        if (id == 0)
        {
            propSourceNow_ = "own descriptor with atomic capability";
            drm_set_client_cap cap;
            cap.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES;
            cap.value = 1;
            ioctl (ownFd_, DRM_IOCTL_SET_CLIENT_CAP, &cap);
            cap.capability = DRM_CLIENT_CAP_ATOMIC;
            ioctl (ownFd_, DRM_IOCTL_SET_CLIENT_CAP, &cap);
            id = propertyIdOn (ownFd_, plane, name);
        }
        if (id == 0)
            propSourceNow_ = "not found";
        return id;
    }
    uint32_t propertyIdOn (int fd, uint32_t plane, const char* name) const
    {
        if (fd < 0)
            return 0;
        uint32_t ids[64];
        uint64_t vals[64];
        drm_mode_obj_get_properties g;
        std::memset (&g, 0, sizeof g);
        g.obj_id = plane;
        g.obj_type = DRM_MODE_OBJECT_PLANE;
        g.count_props = 64;
        g.props_ptr = reinterpret_cast<uintptr_t> (ids);
        g.prop_values_ptr = reinterpret_cast<uintptr_t> (vals);
        if (ioctl (fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) < 0)
            return 0;
        for (uint32_t i = 0; i < g.count_props && i < 64; ++i)
        {
            drm_mode_get_property gp;
            std::memset (&gp, 0, sizeof gp);
            gp.prop_id = ids[i];
            if (ioctl (fd, DRM_IOCTL_MODE_GETPROPERTY, &gp) == 0 && std::strcmp (gp.name, name) == 0)
                return ids[i];
        }
        return 0;
    }
    uint32_t planeFb() const
    {
        drm_mode_get_plane p;
        std::memset (&p, 0, sizeof p);
        p.plane_id = primary_;
        return ioctl (ownFd_, DRM_IOCTL_MODE_GETPLANE, &p) == 0 ? p.fb_id : 0;
    }
    /** GETFB2 + MAP_DUMB + a read / write mapping of the framebuffer `id` (writer thread). The layout must be what the hook assumes: XR24 / AR24, the panel size, no modifier, one plane. */
    bool mapFb (uint32_t id)
    {
        if (id == 0 || fbs_.find (id) != nullptr || maps_.size() >= 16)
            return false;
        drm_mode_fb_cmd2 f;
        std::memset (&f, 0, sizeof f);
        f.fb_id = id;
        if (ioctl (ownFd_, DRM_IOCTL_MODE_GETFB2, &f) < 0 || f.handles[0] == 0)
            return false;
        bool ok = (f.pixel_format == DRM_FORMAT_XRGB8888 || f.pixel_format == DRM_FORMAT_ARGB8888) && static_cast<int> (f.width) == W_ && static_cast<int> (f.height) == H_
                  && f.pitches[0] >= static_cast<uint32_t> (W_) * 4u && f.offsets[0] == 0 && f.handles[1] == 0 && (! (f.flags & DRM_MODE_FB_MODIFIERS) || f.modifier[0] == 0);
        uint8_t* map = nullptr;
        const size_t len = static_cast<size_t> (f.pitches[0]) * static_cast<size_t> (H_);
        ino_t ino = 0;
        dev_t dev = 0;
        if (ok)
        {
            drm_mode_map_dumb md;
            std::memset (&md, 0, sizeof md);
            md.handle = f.handles[0];
            if (ioctl (ownFd_, DRM_IOCTL_MODE_MAP_DUMB, &md) == 0)
            {
                void* p = mmap (nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, ownFd_, static_cast<off_t> (md.offset));
                if (p != MAP_FAILED)
                    map = static_cast<uint8_t*> (p);
            }
            drm_prime_handle ph;
            std::memset (&ph, 0, sizeof ph);
            ph.handle = f.handles[0];
            ph.flags = DRM_CLOEXEC;
            if (ioctl (ownFd_, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) == 0)
            {
                struct stat st;
                if (fstat (ph.fd, &st) == 0)
                {
                    ino = st.st_ino;
                    dev = st.st_dev;
                }
                ::close (ph.fd);
            }
        }
        for (int i = 0; i < 4; ++i)   // every distinct handle GETFB2 gave us is ours to close (the mapping keeps the buffer alive)
        {
            bool seen = f.handles[i] == 0;
            for (int j = 0; j < i; ++j)
                seen = seen || f.handles[j] == f.handles[i];
            if (seen)
                continue;
            drm_gem_close gc;
            std::memset (&gc, 0, sizeof gc);
            gc.handle = f.handles[i];
            ioctl (ownFd_, DRM_IOCTL_GEM_CLOSE, &gc);
        }
        if (! ok || map == nullptr)
        {
            if (map)
                munmap (map, len);
            return false;
        }
        Mapping m;
        m.id = id;
        m.map = map;
        m.len = len;
        m.ino = ino;
        m.dev = dev;
        maps_.push_back (m);
        FbView v;
        v.map = map;
        v.pitch = f.pitches[0];
        v.w = W_;
        v.h = H_;
        fbs_.add (id, v);
        return true;
    }
    /** Once a second: the framebuffer behind an id is still the buffer we mapped (the host did not remove it and make another with the same id). A changed one stops being served (its old mapping stays until the end). */
    void validateMaps()
    {
        for (Mapping& m : maps_)
        {
            if (m.id == 0 || fbs_.find (m.id) == nullptr)
                continue;
            drm_mode_fb_cmd2 f;
            std::memset (&f, 0, sizeof f);
            f.fb_id = m.id;
            bool same = false;
            if (ioctl (ownFd_, DRM_IOCTL_MODE_GETFB2, &f) == 0 && f.handles[0] != 0)
            {
                drm_prime_handle ph;
                std::memset (&ph, 0, sizeof ph);
                ph.handle = f.handles[0];
                ph.flags = DRM_CLOEXEC;
                if (ioctl (ownFd_, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) == 0)
                {
                    struct stat st;
                    same = fstat (ph.fd, &st) == 0 && st.st_ino == m.ino && st.st_dev == m.dev;
                    ::close (ph.fd);
                }
                for (int i = 0; i < 4; ++i)
                {
                    bool seen = f.handles[i] == 0;
                    for (int j = 0; j < i; ++j)
                        seen = seen || f.handles[j] == f.handles[i];
                    if (seen)
                        continue;
                    drm_gem_close gc;
                    std::memset (&gc, 0, sizeof gc);
                    gc.handle = f.handles[i];
                    ioctl (ownFd_, DRM_IOCTL_GEM_CLOSE, &gc);
                }
            }
            if (! same)
            {
                fbs_.drop (m.id);
                m.id = 0;
                Counters::bump (core_.counters.fbRemaps);
            }
        }
    }

    /** One vblank of our own descriptor (the writer thread; a failure or a timeout of the driver just returns: the write then goes ahead without the sync). */
    static void waitVblankThunk (void* self)
    {
        Runtime& rt = *static_cast<Runtime*> (self);
        drm_wait_vblank vbl;
        std::memset (&vbl, 0, sizeof vbl);
        vbl.request.type = static_cast<drm_vblank_seq_type> (_DRM_VBLANK_RELATIVE | (rt.vblankPipe_ > 0 ? ((rt.vblankPipe_ << _DRM_VBLANK_HIGH_CRTC_SHIFT) & _DRM_VBLANK_HIGH_CRTC_MASK) : 0));
        vbl.request.sequence = 1;
        ioctl (rt.ownFd_, DRM_IOCTL_WAIT_VBLANK, &vbl);
    }

    // ======== the writer thread ========
    double nowMs() const { return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count(); }

    void pollBacklight()
    {
        if (backlight_.empty())
        {
            if (DIR* d = opendir (paths.backlightDir.c_str()))
            {
                while (dirent* e = readdir (d))
                    if (e->d_name[0] != '.')
                    {
                        backlight_ = paths.backlightDir + "/" + e->d_name;
                        break;
                    }
                closedir (d);
            }
            if (backlight_.empty())
                backlight_ = "-";
        }
        if (backlight_ == "-")
            return;
        int cur = readInt (backlight_ + "/brightness");
        if (cur < 0)
            return;
        backlightMax_ = std::max (backlightMax_, cur);
        const bool dim = backlightMax_ > 0 && cur * 100 < backlightMax_ * 80;
        if (dim != core_.backlightDim.load())
        {
            core_.backlightDim.store (dim);
            frontNeedsLook_.store (true);
        }
    }
    static int readInt (const std::string& path)
    {
        FILE* f = std::fopen (path.c_str(), "r");
        if (! f)
            return -1;
        int v = -1;
        if (std::fscanf (f, "%d", &v) != 1)
            v = -1;
        std::fclose (f);
        return v;
    }

    void writerLoop()
    {
        double nextSlow = 0.0;
        bool again = false;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lk (wakeMu_);
                wake_.wait_for (lk, std::chrono::milliseconds (again ? 2 : 10), [this] { return stopWriter_ || writerWake_.load (std::memory_order_acquire); });
                if (stopWriter_)
                    return;
            }
            writerWake_.store (false, std::memory_order_release);
            again = false;
            {
                const int v = faultWhy_.exchange (0);
                if (v != 0)
                    handleVerdict (static_cast<FallbackPolicy::Verdict> (v));
            }
            if (! pendingWhy_.empty())
            {
                tryFault();
                again = ! pendingWhy_.empty();
            }
            if (state_.load() != kArmed)
                continue;
            fbHint_.store (planeFb(), std::memory_order_relaxed);
            const uint32_t want = wantMap_.exchange (0, std::memory_order_acq_rel);
            if (want != 0)
                mapFb (want);
            const double now = nowMs();
            if (now >= nextSlow)
            {
                nextSlow = now + 1000.0;
                validateMaps();
                if (kill())
                {
                    handleVerdictText ("the kill-switch file " + paths.killSwitch + " exists");
                    again = true;
                    continue;
                }
            }
            if (now >= nextBacklight_)
            {
                nextBacklight_ = now + 100.0;
                pollBacklight();
            }
            const SamplePlan& plan = plan_[planIdx_.load()];
            // a look of the frame on screen when the hook has not made one (nothing has flipped since it was armed), or the standby state changed
            LookRecord L;
            const bool have = core_.look.load (L);
            if (! have || frontNeedsLook_.load() || (have && L.fb == 0))
            {
                const uint32_t fb = planeFb();
                if (fb != 0 && fbs_.find (fb) == nullptr)
                    mapFb (fb);
                if (fb != 0 && core_.analyseFront (fb, plan))
                    frontNeedsLook_.store (false);
            }
            if (ring_.hasPicture())
            {
                bool retry = false;
                core_.frontPass (plan, retry);
                again = retry;
            }
            else
                core_.eraseFront (plan);
        }
    }
    void handleVerdict (FallbackPolicy::Verdict v) { pendingWhy_ = FallbackPolicy::name (v); }
    void handleVerdictText (const std::string& why) { pendingWhy_ = why; }
    /** The writer thread cannot take lifeMu_ while the owner of it joins this thread (removeLocked): it only tries, and tries again at the next round. */
    void tryFault()
    {
        if (pendingWhy_.empty())
            return;
        std::unique_lock<std::mutex> lk (lifeMu_, std::try_to_lock);
        if (! lk.owns_lock())
            return;
        faultLocked (pendingWhy_);
        pendingWhy_.clear();
    }

    // ======== state ========
    FbTable fbs_;
    FrameRing ring_;
    FlipCore core_;
    SlotPatch patch_;
    FallbackPolicy::Limits limits_;
    FallbackPolicy policy_ { limits_ };

    mutable std::mutex lifeMu_, wakeMu_;
    std::condition_variable wake_;
    std::thread writer_;
    std::atomic<bool> writerWake_ { false };
    bool stopWriter_ = false;
    std::atomic<int> state_ { kOff };
    std::atomic<int> inflight_ { 0 };
    std::atomic<uint32_t> entries_ { 0 };   // every call of the hook, armed or not: the proof that the host's commit goes through the patched slot
    std::atomic<int> faultWhy_ { 0 };
    std::atomic<uint32_t> wantMap_ { 0 };
    std::atomic<int> planIdx_ { 0 };
    SamplePlan plan_[2];
    int refs_ = 0;
    bool installed_ = false, configured_ = false;
    std::atomic<bool> frontNeedsLook_ { false };
    int vblankPipe_ = 0;
    int hostFd_ = -1, ownFd_ = -1, W_ = 0, H_ = 0, backlightMax_ = 0;
    double nextBacklight_ = 0.0;
    uint32_t primary_ = 0;
    std::atomic<uint32_t> fbProp_ { 0 }, fbHint_ { 0 };
    size_t stride_ = 24;
    CommitFn orig_ = nullptr;
    void* slotAddr_ = nullptr;
    void* hookAddr_ = nullptr;
    void* expectedAddr_ = nullptr;
    std::string copyName_ = "memcpy", importers_, propSource_, propSourceNow_, expectedPath_, protNote_, installNote_ = "not tried", faultReason_, backlight_, pendingWhy_;
    std::vector<Mapping> maps_;
};

}}} // namespace synthyboy_mpc::overlay::inhost

#endif // __linux__
