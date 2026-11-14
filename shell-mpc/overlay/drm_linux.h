// SynthyBoy - MPC overlay: the real display backends (Linux). Raw DRM ioctls from <drm/drm.h>, no libdrm. Measured on the Key 61 (see docs/MPC.md "Overlay (experimental)").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// LinuxPlaneBackend: the plugin runs inside the MPC app, which holds /dev/dri/card0 as DRM master. The overlay plane is driven through THAT descriptor (found by scanning
// /proc/self/fd for a link to /dev/dri/card0 whose owner is the master: DRM_IOCTL_AUTH_MAGIC with magic 0 answers EINVAL to a master and EACCES to anybody else, and changes nothing).
// Only legacy SETPLANE on the one overlay plane, ADDFB2 / RMFB and dumb buffers; never client caps, never a mode set, never an atomic commit, never planes 33 / 35.
// The plane must be free (no CRTC, no framebuffer) when we start and must support AR24 (ARGB8888).
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

#include "drm_interfaces.h"

namespace synthyboy_mpc { namespace overlay {

class LinuxPlaneBackend final : public IPlaneBackend
{
public:
    LinuxPlaneBackend (uint32_t crtcId, uint32_t planeId) : crtc_ (crtcId), plane_ (planeId) {}
    ~LinuxPlaneBackend() override
    {
        disablePlane();
        destroyBuffers();
    }

    bool open (int& panelW, int& panelH, std::string& error) override
    {
        if (fd_ >= 0)
        {
            panelW = panelW_;
            panelH = panelH_;
            return true;
        }
        std::vector<int> candidates;
        if (DIR* d = opendir ("/proc/self/fd"))
        {
            while (dirent* e = readdir (d))
            {
                char path[320], target[128];
                std::snprintf (path, sizeof path, "/proc/self/fd/%s", e->d_name);
                const ssize_t n = readlink (path, target, sizeof target - 1);
                if (n <= 0)
                    continue;
                target[n] = 0;
                if (std::strcmp (target, "/dev/dri/card0") == 0)
                    candidates.push_back (std::atoi (e->d_name));
            }
            closedir (d);
        }
        if (candidates.empty())
        {
            error = "no descriptor of /dev/dri/card0 in this process";
            return false;
        }
        int found = -1;
        for (int fd : candidates)
        {
            drm_auth auth;
            std::memset (&auth, 0, sizeof auth);
            errno = 0;
            if (ioctl (fd, DRM_IOCTL_AUTH_MAGIC, &auth) < 0 && errno == EINVAL)   // EINVAL = passed the master check, no such magic
            {
                found = fd;
                break;
            }
        }
        if (found < 0)
        {
            error = "no descriptor of /dev/dri/card0 with master rights (" + std::to_string (candidates.size()) + " found)";
            return false;
        }
        drm_mode_crtc c;
        std::memset (&c, 0, sizeof c);
        c.crtc_id = crtc_;
        if (ioctl (found, DRM_IOCTL_MODE_GETCRTC, &c) < 0 || ! c.mode_valid || c.mode.hdisplay == 0)
        {
            error = "GETCRTC " + std::to_string (crtc_) + " failed or the CRTC has no mode";
            return false;
        }
        drm_mode_get_plane p;
        std::memset (&p, 0, sizeof p);
        p.plane_id = plane_;
        if (ioctl (found, DRM_IOCTL_MODE_GETPLANE, &p) < 0)
        {
            error = "GETPLANE " + std::to_string (plane_) + " failed";
            return false;
        }
        if (p.crtc_id != 0 || p.fb_id != 0)
        {
            error = "plane " + std::to_string (plane_) + " is in use (crtc " + std::to_string (p.crtc_id) + ", fb " + std::to_string (p.fb_id) + ")";
            return false;
        }
        std::vector<uint32_t> formats (p.count_format_types ? p.count_format_types : 1);
        const uint32_t count = p.count_format_types;
        p.format_type_ptr = reinterpret_cast<uintptr_t> (formats.data());
        if (count == 0 || ioctl (found, DRM_IOCTL_MODE_GETPLANE, &p) < 0)
        {
            error = "GETPLANE formats failed";
            return false;
        }
        bool argb = false;
        for (uint32_t i = 0; i < count && i < formats.size(); ++i)
            argb = argb || formats[i] == DRM_FORMAT_ARGB8888;
        if (! argb)
        {
            error = "plane " + std::to_string (plane_) + " has no ARGB8888";
            return false;
        }
        fd_ = found;
        panelW_ = c.mode.hdisplay;
        panelH_ = c.mode.vdisplay;
        panelW = panelW_;
        panelH = panelH_;
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
        drm_mode_set_plane s;
        std::memset (&s, 0, sizeof s);
        s.plane_id = plane_;
        s.crtc_id = crtc_;
        s.fb_id = buf_[index].fb;
        s.crtc_x = panel.x;
        s.crtc_y = panel.y;
        s.crtc_w = static_cast<uint32_t> (panel.w);
        s.crtc_h = static_cast<uint32_t> (panel.h);
        s.src_x = 0;
        s.src_y = 0;
        s.src_w = static_cast<uint32_t> (panel.w) << 16;
        s.src_h = static_cast<uint32_t> (panel.h) << 16;
        if (ioctl (fd_, DRM_IOCTL_MODE_SETPLANE, &s) < 0)
            return fail (error, "SETPLANE");
        planeSet_ = true;
        return true;
    }

    void disablePlane() noexcept override
    {
        if (fd_ < 0 || ! planeSet_)
            return;
        drm_mode_set_plane s;
        std::memset (&s, 0, sizeof s);
        s.plane_id = plane_;   // fb 0 and crtc 0 take the plane off
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
        s.plane_id = plane_;
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

    uint32_t crtc_, plane_;
    int fd_ = -1;   // the application's descriptor: borrowed, never closed here
    int panelW_ = 0, panelH_ = 0;
    bool planeSet_ = false;
    Buf buf_[2];
};

class LinuxFrameSource final : public IFrameSource
{
public:
    explicit LinuxFrameSource (uint32_t crtcId) : crtc_ (crtcId) {}
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
            fd_ = ::open ("/dev/dri/card0", O_RDWR | O_CLOEXEC);
            if (fd_ < 0)
                return false;
        }
        drm_mode_crtc c;
        std::memset (&c, 0, sizeof c);
        c.crtc_id = crtc_;
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

    uint32_t crtc_;
    int fd_ = -1;
    Entry cache_[4];
    Entry* cur_ = nullptr;
    unsigned long clock_ = 0;
};

}} // namespace synthyboy_mpc::overlay
#endif // __linux__
