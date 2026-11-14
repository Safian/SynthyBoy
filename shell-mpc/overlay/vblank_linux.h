// SynthyBoy - MPC overlay: the real vblank source (Linux): DRM_IOCTL_WAIT_VBLANK on a DRM descriptor for the pipe (the index of the CRTC), time stamps in CLOCK_MONOTONIC. Read only:
// the call changes nothing on the display. Shared by the plugin (drm_linux.h) and the GPU helper.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#if defined(__linux__)
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <sys/ioctl.h>
#include <unistd.h>
#include <drm/drm.h>

#include "vblank_pacer.h"

namespace synthyboy_mpc { namespace overlay {

class LinuxVblankClock final : public IVblankClock
{
public:
    /** `fd`: any open descriptor of the display device (the application's borrowed one or one of our own); `pipe`: the index of the CRTC in the resource list. */
    LinuxVblankClock (int fd, int pipe) : fd_ (fd), pipe_ (pipe)
    {
        drm_get_cap cap;
        std::memset (&cap, 0, sizeof cap);
        cap.capability = DRM_CAP_TIMESTAMP_MONOTONIC;
        monotonic_ = ioctl (fd_, DRM_IOCTL_GET_CAP, &cap) == 0 ? cap.value != 0 : true;
    }

    bool last (VblankStamp& s) override { return request (_DRM_VBLANK_RELATIVE, 0, s); }
    bool waitUntil (uint32_t seq, VblankStamp& s) override { return request (_DRM_VBLANK_ABSOLUTE, seq, s); }
    bool waitNext (VblankStamp& s) override { return request (_DRM_VBLANK_RELATIVE, 1, s); }

    double nowMs() override
    {
        timespec t;
        clock_gettime (CLOCK_MONOTONIC, &t);
        return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
    }
    void sleepUntilMs (double ms) override
    {
        timespec t;
        t.tv_sec = static_cast<time_t> (ms / 1000.0);
        t.tv_nsec = static_cast<long> ((ms - static_cast<double> (t.tv_sec) * 1000.0) * 1e6);
        while (clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr) == EINTR) {}
    }

private:
    bool request (uint32_t type, uint32_t seq, VblankStamp& s)
    {
        drm_wait_vblank v;
        std::memset (&v, 0, sizeof v);
        uint32_t t = type;
        if (pipe_ == 1)
            t |= _DRM_VBLANK_SECONDARY;
        else if (pipe_ > 1)
            t |= static_cast<uint32_t> (pipe_) << _DRM_VBLANK_HIGH_CRTC_SHIFT;
        v.request.type = static_cast<drm_vblank_seq_type> (t);
        v.request.sequence = seq;
        int rc;
        do
            rc = ioctl (fd_, DRM_IOCTL_WAIT_VBLANK, &v);
        while (rc < 0 && errno == EINTR);
        if (rc < 0)
            return false;
        s.seq = v.reply.sequence;
        s.ms = static_cast<double> (v.reply.tval_sec) * 1000.0 + static_cast<double> (v.reply.tval_usec) / 1000.0;
        if (! monotonic_)
        {
            timespec r, m;
            clock_gettime (CLOCK_REALTIME, &r);
            clock_gettime (CLOCK_MONOTONIC, &m);
            s.ms += static_cast<double> (m.tv_sec - r.tv_sec) * 1000.0 + static_cast<double> (m.tv_nsec - r.tv_nsec) / 1e6;
        }
        return true;
    }

    int fd_, pipe_;
    bool monotonic_ = true;
};

}} // namespace synthyboy_mpc::overlay
#endif // __linux__
