// SynthyBoy - MPC overlay: the real display backends (Linux). Raw DRM ioctls from <drm/drm.h>, no libdrm. Measured on the Key 61 (see docs/MPC.md "Overlay (experimental)").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// LinuxDisplay (run-time discovery, shared by the two backends below): the plugin lives inside the MPC app, which holds the display device (/dev/dri/cardN) as DRM master. The app's
// descriptor is found by scanning /proc/self/fd for a link to /dev/dri/card* whose owner is the master (DRM_IOCTL_AUTH_MAGIC with magic 0 answers EINVAL to a master and EACCES to
// anybody else, and changes nothing) and that has CRTCs. What is on the display is read through a descriptor of our OWN on the same device (universal planes switched on there, which
// is a capability of that descriptor only, never of the application's): the CRTC the application uses, its primary plane, the free overlay planes (discovery.h has the rules). The
// real refresh period is measured from vblank time stamps. Nothing is written to the application's descriptor except what LinuxPlaneBackend does.
//
// LinuxPlaneBackend: the overlay plane is driven through the APPLICATION's descriptor. Only legacy SETPLANE on the one overlay plane, ADDFB2 / RMFB and dumb buffers; never client caps,
// never a mode set, never an atomic commit, never the application's planes. The plane must be free (no CRTC, no framebuffer) when we start and must support AR24 (ARGB8888). The update
// is issued at a vblank of the pace grid (vblank_pacer.h); a SETPLANE that fails moves the module to the next free plane before it gives up.
//
// LinuxFrameSource: reads the application's primary framebuffer read only through a descriptor of its OWN (opened here: a GEM handle closed on the app's descriptor could close the
// app's own handle). GETCRTC -> framebuffer id, GETFB2 -> handle / size / pitch, PRIME export -> dma-buf, read-only mmap, DMA_BUF_SYNC around the reads. The app turns 2 or 3
// primary buffers; the mappings are cached (up to 4) by the identity of the dma-buf (the inode, not the framebuffer id: ids are reused after RMFB), validated at every begin ().
#pragma once

#if defined(__linux__)
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <linux/dma-buf.h>

#include "discovery.h"
#include "drm_interfaces.h"
#include "vblank_linux.h"

namespace synthyboy_mpc { namespace overlay {

class LinuxDisplay
{
public:
    ~LinuxDisplay()
    {
        if (own_ >= 0)
            ::close (own_);
    }

    /** Discovery, once (a failure is final too: the reason stays). Called from the overlay thread only. */
    bool ensure (std::string& error)
    {
        if (state_ == 1)
            return true;
        if (state_ == 2)
        {
            error = error_;
            return false;
        }
        state_ = discover() ? 1 : 2;
        if (state_ == 2)
            error = error_;
        return state_ == 1;
    }
    const std::string& error() const noexcept { return error_; }
    bool ready() const noexcept { return state_ == 1; }
    int fd() const noexcept { return fd_; }             // the application's descriptor (borrowed)
    int ownFd() const noexcept { return own_; }
    const DisplayInfo& info() const noexcept { return info_; }
    PaceParams pace() const noexcept { return pace_; }
    uint32_t plane() const noexcept { return info_.plane(); }

    /** The plane reports our CRTC and a framebuffer (GETPLANE on our own descriptor). */
    bool planeStillOurs (std::string& why)
    {
        if (state_ != 1 || info_.plane() == 0)
            return true;
        drm_mode_get_plane p;
        std::memset (&p, 0, sizeof p);
        p.plane_id = info_.plane();
        if (ioctl (own_, DRM_IOCTL_MODE_GETPLANE, &p) < 0)
        {
            why = "GETPLANE failed";
            return false;
        }
        if (p.crtc_id == 0 && p.fb_id == 0)
        {
            switchedOff_ = true;     // switched off, not taken: a mode set of the application switches off every plane it does not name; the picture is set again on this plane
            return true;
        }
        if (p.crtc_id != info_.crtc || p.fb_id == 0)
        {
            why = "plane " + std::to_string (info_.plane()) + " reports crtc " + std::to_string (p.crtc_id) + ", fb " + std::to_string (p.fb_id) + " (another program took it or switched it off)";
            return false;
        }
        lastFb_ = p.fb_id;
        return true;
    }
    uint32_t lastFb() const noexcept { return lastFb_; }
    bool takeSwitchedOff() noexcept { const bool v = switchedOff_; switchedOff_ = false; return v; }
    /** The CRTC has a mode (the display is on): a plane that failed while it has none is not lost, the display is off. */
    bool crtcActive()
    {
        drm_mode_crtc c;
        std::memset (&c, 0, sizeof c);
        c.crtc_id = info_.crtc;
        return ioctl (own_, DRM_IOCTL_MODE_GETCRTC, &c) == 0 && c.mode_valid != 0 && c.mode.hdisplay != 0;
    }
    /** Is the plane free right now (no CRTC, no framebuffer)? */
    bool planeFree (uint32_t id)
    {
        drm_mode_get_plane p;
        std::memset (&p, 0, sizeof p);
        p.plane_id = id;
        return ioctl (own_, DRM_IOCTL_MODE_GETPLANE, &p) == 0 && p.crtc_id == 0 && p.fb_id == 0;
    }
    /** The next spare plane that is still free. false: none. */
    bool advance (std::string& why)
    {
        while (advancePlane (info_))
        {
            if (planeFree (info_.plane()))
            {
                ++planeSwitches_;
                why = "switched to plane " + std::to_string (info_.plane());
                return true;
            }
        }
        why = "no spare overlay plane left";
        return false;
    }
    unsigned planeSwitches() const noexcept { return planeSwitches_; }
    double discoveryMs() const noexcept { return discoveryMs_; }

    void describe (std::string& out) const
    {
        out += "display_device: " + info_.device + "\n";
        out += "display_crtc: " + std::to_string (info_.crtc) + " (index " + std::to_string (info_.crtcIndex) + ", found by " + info_.how + ")\n";
        out += "display_primary_plane: " + std::to_string (info_.primaryPlane) + "\n";
        std::string apps;
        for (uint32_t id : info_.appPlanes)
            apps += (apps.empty() ? "" : " ") + std::to_string (id);
        out += "display_app_planes: " + apps + "\n";
        std::string cands;
        for (uint32_t id : info_.candidates)
            cands += (cands.empty() ? "" : " ") + std::to_string (id);
        out += "display_overlay_candidates: " + cands + "\n";
        out += "display_overlay_plane: " + std::to_string (info_.plane()) + "\n";
        out += "display_plane_switches: " + std::to_string (planeSwitches_) + "\n";
        char b[200];
        std::snprintf (b, sizeof b, "display_panel: %dx%d\ndisplay_mode_refresh_hz: %d\ndisplay_refresh_hz: %.3f\ndisplay_vblank_period_ms: %.4f\ndisplay_vblank_measured: %s\n", info_.panelW, info_.panelH,
                       info_.modeRefresh, pace_.periodMs > 0 ? 1000.0 / pace_.periodMs : 0.0, pace_.periodMs, pace_.vblank ? "yes" : "no (the mode's rate is used)");
        out += b;
        std::snprintf (b, sizeof b, "pace_stride_vblanks: %d\npace_mode: %s\ndiscovery_ms: %.1f\n", pace_.stride, pace_.vblank ? "vblank grid" : "timer", discoveryMs_);
        out += b;
    }

private:
    struct Cand { int fd; std::string path; };

    bool discover()
    {
        const double t0 = nowMs();
        std::vector<Cand> cands;
        if (DIR* d = opendir ("/proc/self/fd"))
        {
            while (dirent* e = readdir (d))
            {
                char path[320], target[160];
                std::snprintf (path, sizeof path, "/proc/self/fd/%s", e->d_name);
                const ssize_t n = readlink (path, target, sizeof target - 1);
                if (n <= 0)
                    continue;
                target[n] = 0;
                if (std::strncmp (target, "/dev/dri/card", 13) == 0)
                    cands.push_back ({ std::atoi (e->d_name), target });
            }
            closedir (d);
        }
        if (cands.empty())
        {
            error_ = "no descriptor of a /dev/dri/card* device in this process";
            return false;
        }
        std::string lastWhy;
        for (const Cand& c : cands)
        {
            drm_auth auth;
            std::memset (&auth, 0, sizeof auth);
            errno = 0;
            if (! (ioctl (c.fd, DRM_IOCTL_AUTH_MAGIC, &auth) < 0 && errno == EINVAL))   // EINVAL = passed the master check, no such magic
            {
                lastWhy = c.path + " without master rights";
                continue;
            }
            const int own = ::open (c.path.c_str(), O_RDWR | O_CLOEXEC);
            if (own < 0)
            {
                lastWhy = "cannot open " + c.path + ": " + std::strerror (errno);
                continue;
            }
            DrmSnapshot snap;
            if (! snapshot (own, snap) || snap.crtcs.empty())
            {
                ::close (own);
                lastWhy = c.path + " has no CRTCs";
                continue;
            }
            DisplayInfo d = discoverDisplay (snap);
            d.device = c.path;
            if (! d.ok)
            {
                ::close (own);
                lastWhy = c.path + ": " + d.error;
                continue;
            }
            fd_ = c.fd;
            own_ = own;
            info_ = d;
            info_.ok = true;
            measure();
            discoveryMs_ = nowMs() - t0;
            return true;
        }
        error_ = "display discovery failed: " + lastWhy;
        return false;
    }

    static double nowMs()
    {
        timespec t;
        clock_gettime (CLOCK_MONOTONIC, &t);
        return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
    }

    /** The real refresh period (40 vblanks, about 0.7 s once), the stride of the pace grid. */
    void measure()
    {
        LinuxVblankClock clock (own_, info_.crtcIndex);
        double period = 0.0;
        pace_.pipe = info_.crtcIndex;
        if (measureVblankPeriod (clock, 40, period))
        {
            pace_.periodMs = period;
            pace_.vblank = true;
        }
        else
        {
            pace_.periodMs = 1000.0 / (info_.modeRefresh > 0 ? info_.modeRefresh : 60);
            pace_.vblank = false;
        }
        pace_.stride = paceStride (pace_.periodMs);
    }

    static bool snapshot (int fd, DrmSnapshot& out)
    {
        drm_set_client_cap cap;
        std::memset (&cap, 0, sizeof cap);
        cap.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES;   // on OUR descriptor only: lists the primary and cursor planes with their types
        cap.value = 1;
        ioctl (fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);
        drm_mode_card_res res;
        std::memset (&res, 0, sizeof res);
        if (ioctl (fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 || res.count_crtcs == 0)
            return false;
        std::vector<uint32_t> crtcIds (res.count_crtcs);
        res.crtc_id_ptr = reinterpret_cast<uintptr_t> (crtcIds.data());
        res.count_fbs = res.count_connectors = res.count_encoders = 0;
        res.fb_id_ptr = res.connector_id_ptr = res.encoder_id_ptr = 0;
        if (ioctl (fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
            return false;
        for (uint32_t i = 0; i < res.count_crtcs && i < crtcIds.size(); ++i)
        {
            drm_mode_crtc c;
            std::memset (&c, 0, sizeof c);
            c.crtc_id = crtcIds[i];
            if (ioctl (fd, DRM_IOCTL_MODE_GETCRTC, &c) < 0)
                continue;
            CrtcInfo ci;
            ci.id = crtcIds[i];
            ci.index = static_cast<int> (i);
            ci.modeValid = c.mode_valid != 0 && c.mode.hdisplay != 0;
            ci.width = c.mode.hdisplay;
            ci.height = c.mode.vdisplay;
            ci.vrefresh = static_cast<int> (c.mode.vrefresh);
            ci.fbId = c.fb_id;
            out.crtcs.push_back (ci);
        }
        drm_mode_get_plane_res pr;
        std::memset (&pr, 0, sizeof pr);
        if (ioctl (fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr) < 0 || pr.count_planes == 0)
            return true;
        std::vector<uint32_t> planeIds (pr.count_planes);
        pr.plane_id_ptr = reinterpret_cast<uintptr_t> (planeIds.data());
        if (ioctl (fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr) < 0)
            return true;
        for (uint32_t i = 0; i < pr.count_planes && i < planeIds.size(); ++i)
        {
            drm_mode_get_plane gp;
            std::memset (&gp, 0, sizeof gp);
            gp.plane_id = planeIds[i];
            if (ioctl (fd, DRM_IOCTL_MODE_GETPLANE, &gp) < 0)
                continue;
            PlaneInfo pi;
            pi.id = planeIds[i];
            pi.possibleCrtcs = gp.possible_crtcs;
            pi.crtcId = gp.crtc_id;
            pi.fbId = gp.fb_id;
            if (gp.count_format_types > 0)
            {
                pi.formats.resize (gp.count_format_types);
                gp.format_type_ptr = reinterpret_cast<uintptr_t> (pi.formats.data());
                if (ioctl (fd, DRM_IOCTL_MODE_GETPLANE, &gp) < 0)
                    pi.formats.clear();
            }
            planeProperties (fd, pi);
            out.planes.push_back (pi);
        }
        return true;
    }

    static void planeProperties (int fd, PlaneInfo& pi)
    {
        drm_mode_obj_get_properties g;
        std::memset (&g, 0, sizeof g);
        g.obj_id = pi.id;
        g.obj_type = DRM_MODE_OBJECT_PLANE;
        if (ioctl (fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) < 0 || g.count_props == 0)
            return;
        std::vector<uint32_t> ids (g.count_props);
        std::vector<uint64_t> vals (g.count_props);
        g.props_ptr = reinterpret_cast<uintptr_t> (ids.data());
        g.prop_values_ptr = reinterpret_cast<uintptr_t> (vals.data());
        if (ioctl (fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g) < 0)
            return;
        for (uint32_t i = 0; i < g.count_props && i < ids.size(); ++i)
        {
            drm_mode_get_property gp;
            std::memset (&gp, 0, sizeof gp);
            gp.prop_id = ids[i];
            if (ioctl (fd, DRM_IOCTL_MODE_GETPROPERTY, &gp) < 0)
                continue;
            if (std::strcmp (gp.name, "type") == 0)
                pi.type = static_cast<int> (vals[i]);
            else if (std::strcmp (gp.name, "zpos") == 0)
            {
                pi.hasZpos = true;
                pi.zpos = vals[i];
            }
        }
    }

    int state_ = 0;   // 0 not yet, 1 found, 2 failed
    std::string error_;
    int fd_ = -1, own_ = -1;
    DisplayInfo info_;
    PaceParams pace_;
    uint32_t lastFb_ = 0;
    bool switchedOff_ = false;
    unsigned planeSwitches_ = 0;
    double discoveryMs_ = 0.0;
};

class LinuxPlaneBackend final : public IPlaneBackend
{
public:
    explicit LinuxPlaneBackend (std::shared_ptr<LinuxDisplay> display) : display_ (std::move (display)) {}
    ~LinuxPlaneBackend() override
    {
        disablePlane();
        destroyBuffers();
    }

    bool open (int& panelW, int& panelH, std::string& error) override
    {
        if (fd_ >= 0)
        {
            panelW = display_->info().panelW;
            panelH = display_->info().panelH;
            return true;
        }
        if (! display_->ensure (error))
            return false;
        for (;;)
        {
            const uint32_t plane = display_->plane();
            if (display_->planeFree (plane))
                break;
            std::string why;
            if (! display_->advance (why))
            {
                error = "plane " + std::to_string (plane) + " is in use and " + why;
                return false;
            }
        }
        fd_ = display_->fd();
        panelW = display_->info().panelW;
        panelH = display_->info().panelH;
        pace_ = std::make_unique<Pacing> (*display_);
        return true;
    }

    bool createBuffers (int w, int h, std::string& error) override
    {
        destroyBuffers();
        for (int i = 0; i < 2; ++i)
        {
            Buf& b = buf_[i];
            drm_mode_create_dumb c;
            std::memset (&c, 0, sizeof c);
            c.width = static_cast<uint32_t> (w);
            c.height = static_cast<uint32_t> (h);
            c.bpp = 32;
            if (ioctl (fd_, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0)
                return fail (error, "CREATE_DUMB");
            b.handle = c.handle;
            b.pitch = c.pitch;
            b.size = c.size;
            drm_mode_map_dumb m;
            std::memset (&m, 0, sizeof m);
            m.handle = c.handle;
            if (ioctl (fd_, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0)
                return fail (error, "MAP_DUMB");
            void* p = mmap (nullptr, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, static_cast<off_t> (m.offset));
            if (p == MAP_FAILED)
                return fail (error, "mmap of a dumb buffer");
            b.map = static_cast<uint32_t*> (p);
            std::memset (b.map, 0, c.size);
            drm_mode_fb_cmd2 f;
            std::memset (&f, 0, sizeof f);
            f.width = static_cast<uint32_t> (w);
            f.height = static_cast<uint32_t> (h);
            f.pixel_format = DRM_FORMAT_ARGB8888;
            f.handles[0] = c.handle;
            f.pitches[0] = c.pitch;
            if (ioctl (fd_, DRM_IOCTL_MODE_ADDFB2, &f) < 0)
                return fail (error, "ADDFB2");
            b.fb = f.fb_id;
        }
        return true;
    }

    uint32_t* buffer (int index, size_t& strideBytes) noexcept override
    {
        if (index < 0 || index > 1 || ! buf_[index].map)
            return nullptr;
        strideBytes = buf_[index].pitch;
        return buf_[index].map;
    }

    bool setPlane (int index, const Rect& panel, std::string& error) override
    {
        if (index < 0 || index > 1 || buf_[index].fb == 0)
            return fail (error, "no buffer");
        if (pace_)
            pace_->pacer.waitSlot();      // the vblank of the next slot of the grid: the update is made the moment it is reached
        bool ok = doSetPlane (index, panel);
        for (int attempts = 0; ! ok && attempts < 4; ++attempts)
        {
            // the plane may have been taken by another program since the discovery: the next free one
            const int err = errno;
            std::string why;
            if (! display_->crtcActive() || ! display_->advance (why))
            {
                errno = err;
                break;
            }
            ok = doSetPlane (index, panel);
        }
        if (! ok)
            return fail (error, "SETPLANE");
        if (pace_)
            pace_->pacer.done();
        planeSet_ = true;
        return true;
    }

    uint32_t crtcId() const noexcept override { return display_->info().crtc; }
    uint32_t planeId() const noexcept override { return display_->plane(); }
    PaceParams paceParams() const noexcept override { return display_->pace(); }
    bool planeStillOurs (std::string& why) override { return display_->planeStillOurs (why); }
    bool planeWasSwitchedOff() noexcept override { return display_->takeSwitchedOff(); }
    bool nextPlane (std::string& why) override
    {
        planeSet_ = false;
        if (! display_->crtcActive())
        {
            why = "the display is off (the CRTC has no mode): the plane is kept";   // a failure while the display is off is no loss of the plane
            return true;
        }
        return display_->advance (why);
    }
    void describe (std::string& out) const override { display_->describe (out); }
    const PaceStats* paceStats() const noexcept override { return pace_ ? &pace_->pacer.stats() : nullptr; }

    void disablePlane() noexcept override
    {
        if (fd_ < 0 || ! planeSet_)
            return;
        drm_mode_set_plane s;
        std::memset (&s, 0, sizeof s);
        s.plane_id = display_->plane();   // fb 0 and crtc 0 take the plane off
        ioctl (fd_, DRM_IOCTL_MODE_SETPLANE, &s);
        planeSet_ = false;
    }

    int displayFd() const noexcept override { return fd_; }

    void forceDisablePlane() noexcept override
    {
        if (fd_ < 0)
            return;
        drm_mode_set_plane s;
        std::memset (&s, 0, sizeof s);
        s.plane_id = display_->plane();
        ioctl (fd_, DRM_IOCTL_MODE_SETPLANE, &s);
        planeSet_ = false;
    }

    void removeForeign (uint32_t fbId, uint32_t gemHandle) noexcept override
    {
        if (fd_ < 0)
            return;
        if (fbId)
        {
            uint32_t id = fbId;
            ioctl (fd_, DRM_IOCTL_MODE_RMFB, &id);
        }
        if (gemHandle)
        {
            drm_gem_close gc;
            std::memset (&gc, 0, sizeof gc);
            gc.handle = gemHandle;
            ioctl (fd_, DRM_IOCTL_GEM_CLOSE, &gc);
        }
    }

    void destroyBuffers() noexcept override
    {
        for (Buf& b : buf_)
        {
            if (b.map)
                munmap (b.map, b.size);
            if (b.fb && fd_ >= 0)
            {
                uint32_t id = b.fb;
                ioctl (fd_, DRM_IOCTL_MODE_RMFB, &id);
            }
            if (b.handle && fd_ >= 0)
            {
                drm_mode_destroy_dumb d;
                std::memset (&d, 0, sizeof d);
                d.handle = b.handle;
                ioctl (fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
            }
            b = Buf();
        }
    }

private:
    struct Buf
    {
        uint32_t handle = 0, fb = 0, pitch = 0;
        size_t size = 0;
        uint32_t* map = nullptr;
    };

    bool fail (std::string& error, const char* what)
    {
        error = std::string (what) + ": " + std::strerror (errno);
        return false;
    }

    bool doSetPlane (int index, const Rect& panel)
    {
        drm_mode_set_plane s;
        std::memset (&s, 0, sizeof s);
        s.plane_id = display_->plane();
        s.crtc_id = display_->info().crtc;
        s.fb_id = buf_[index].fb;
        s.crtc_x = panel.x;
        s.crtc_y = panel.y;
        s.crtc_w = static_cast<uint32_t> (panel.w);
        s.crtc_h = static_cast<uint32_t> (panel.h);
        s.src_x = 0;
        s.src_y = 0;
        s.src_w = static_cast<uint32_t> (panel.w) << 16;
        s.src_h = static_cast<uint32_t> (panel.h) << 16;
        return ioctl (fd_, DRM_IOCTL_MODE_SETPLANE, &s) == 0;
    }

    struct Pacing
    {
        explicit Pacing (LinuxDisplay& d) : clock (d.ownFd(), d.pace().pipe), pacer (clock, d.pace().periodMs, d.pace().stride, d.pace().vblank) {}
        LinuxVblankClock clock;
        FramePacer pacer;
    };

    std::shared_ptr<LinuxDisplay> display_;
    int fd_ = -1;   // the application's descriptor: borrowed, never closed here
    bool planeSet_ = false;
    Buf buf_[2];
    std::unique_ptr<Pacing> pace_;
};

class LinuxFrameSource final : public IFrameSource
{
public:
    explicit LinuxFrameSource (std::shared_ptr<LinuxDisplay> display) : display_ (std::move (display)) {}
    ~LinuxFrameSource() override
    {
        for (Entry& e : cache_)
            drop (e);
        if (fd_ >= 0)
            ::close (fd_);
    }

    bool begin (int& panelW, int& panelH) override
    {
        if (fd_ < 0)
        {
            std::string why;
            if (! display_->ensure (why))
            {
                fatal_ = why;
                return false;
            }
            fd_ = ::open (display_->info().device.c_str(), O_RDWR | O_CLOEXEC);
            if (fd_ < 0)
                return false;
        }
        drm_mode_crtc c;
        std::memset (&c, 0, sizeof c);
        c.crtc_id = display_->info().crtc;
        if (ioctl (fd_, DRM_IOCTL_MODE_GETCRTC, &c) < 0 || c.fb_id == 0)
            return false;
        drm_mode_fb_cmd2 f;
        std::memset (&f, 0, sizeof f);
        f.fb_id = c.fb_id;
        if (ioctl (fd_, DRM_IOCTL_MODE_GETFB2, &f) < 0 || f.handles[0] == 0)
            return false;
        drm_prime_handle p;
        std::memset (&p, 0, sizeof p);
        p.handle = f.handles[0];
        p.flags = DRM_CLOEXEC;
        const int rc = ioctl (fd_, DRM_IOCTL_PRIME_HANDLE_TO_FD, &p);
        for (int i = 0; i < 4; ++i)   // every distinct handle GETFB2 made in this descriptor is ours to close
        {
            bool seen = f.handles[i] == 0;
            for (int j = 0; j < i; ++j)
                seen = seen || f.handles[j] == f.handles[i];
            if (seen)
                continue;
            drm_gem_close gc;
            std::memset (&gc, 0, sizeof gc);
            gc.handle = f.handles[i];
            ioctl (fd_, DRM_IOCTL_GEM_CLOSE, &gc);
        }
        if (rc < 0)
            return false;
        if (f.pixel_format != DRM_FORMAT_XRGB8888 && f.pixel_format != DRM_FORMAT_ARGB8888)
        {
            ::close (p.fd);
            return false;
        }
        struct stat st;
        if (fstat (p.fd, &st) < 0)
        {
            ::close (p.fd);
            return false;
        }
        Entry* hit = nullptr;
        for (Entry& e : cache_)
            if (e.map && e.ino == st.st_ino && e.dev == st.st_dev && e.pitch == f.pitches[0] && e.offset == f.offsets[0] && e.width == f.width && e.height == f.height)
                hit = &e;
        if (hit)
            ::close (p.fd);
        else
        {
            Entry* slot = &cache_[0];
            for (Entry& e : cache_)
                if (! e.map) { slot = &e; break; }
                else if (e.used < slot->used) slot = &e;
            drop (*slot);
            slot->len = static_cast<size_t> (f.pitches[0]) * f.height + f.offsets[0];
            void* m = mmap (nullptr, slot->len, PROT_READ, MAP_SHARED, p.fd, 0);
            if (m == MAP_FAILED)
            {
                ::close (p.fd);
                return false;
            }
            slot->map = static_cast<const uint8_t*> (m);
            slot->dfd = p.fd;
            slot->ino = st.st_ino;
            slot->dev = st.st_dev;
            slot->pitch = f.pitches[0];
            slot->offset = f.offsets[0];
            slot->width = f.width;
            slot->height = f.height;
            hit = slot;
        }
        hit->used = ++clock_;
        cur_ = hit;
        dma_buf_sync s;
        s.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
        ioctl (cur_->dfd, DMA_BUF_IOCTL_SYNC, &s);
        panelW = static_cast<int> (cur_->width);
        panelH = static_cast<int> (cur_->height);
        return true;
    }

    bool pixel (int px, int py, uint32_t& rgb) noexcept override
    {
        if (! cur_ || px < 0 || py < 0 || px >= static_cast<int> (cur_->width) || py >= static_cast<int> (cur_->height))
            return false;
        const uint32_t v = *reinterpret_cast<const volatile uint32_t*> (cur_->map + cur_->offset + static_cast<size_t> (py) * cur_->pitch + static_cast<size_t> (px) * 4);
        rgb = v & 0xffffffu;
        return true;
    }

    std::string fatalError() const override { return fatal_; }

    void end() noexcept override
    {
        if (! cur_)
            return;
        dma_buf_sync s;
        s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
        ioctl (cur_->dfd, DMA_BUF_IOCTL_SYNC, &s);
        cur_ = nullptr;
    }

private:
    struct Entry
    {
        const uint8_t* map = nullptr;
        size_t len = 0;
        int dfd = -1;
        ino_t ino = 0;
        dev_t dev = 0;
        uint32_t pitch = 0, offset = 0, width = 0, height = 0;
        unsigned long used = 0;
    };

    static void drop (Entry& e) noexcept
    {
        if (e.map)
            munmap (const_cast<uint8_t*> (e.map), e.len);
        if (e.dfd >= 0)
            ::close (e.dfd);
        e = Entry();
    }

    std::shared_ptr<LinuxDisplay> display_;
    std::string fatal_;
    int fd_ = -1;
    Entry cache_[4];
    Entry* cur_ = nullptr;
    unsigned long clock_ = 0;
};

}} // namespace synthyboy_mpc::overlay
#endif // __linux__
