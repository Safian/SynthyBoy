// SynthyBoy - MPC overlay: the two small interfaces to the display driver. The real implementations are in drm_linux.h; the tests use fakes (tests/fakes.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "geometry.h"
#include "vblank_pacer.h"

namespace synthyboy_mpc { namespace overlay {

/** What the presenting side needs of the refresh: the measured period, the vblanks per picture, whether the vblank can be waited for, and the pipe of the CRTC. */
struct PaceParams
{
    double periodMs = 16.67;
    int stride = 2;
    bool vblank = false;
    int pipe = 0;
};

/** The overlay plane and its two ARGB buffers (panel orientation, portrait). Every method is called from the overlay thread only. */
class IPlaneBackend
{
public:
    virtual ~IPlaneBackend() = default;
    /** Finds the descriptor, checks that the plane is free; reports the panel size. false: `error` says why (the module then disables itself). */
    virtual bool open (int& panelW, int& panelH, std::string& error) = 0;
    /** Two zeroed buffers of w x h pixels (ARGB8888, premultiplied alpha) with framebuffers. */
    virtual bool createBuffers (int w, int h, std::string& error) = 0;
    /** The pixels of buffer 0 or 1 and the row pitch in bytes; nullptr before createBuffers. */
    virtual uint32_t* buffer (int index, size_t& strideBytes) noexcept = 0;
    /** Shows buffer `index` on the plane at the panel rectangle (source rectangle = the whole buffer). Blocks until the next vblank on the device. */
    virtual bool setPlane (int index, const Rect& panel, std::string& error) = 0;
    /** Takes the plane off the CRTC (no framebuffer, no CRTC). Safe to call at any time, also twice. */
    virtual void disablePlane() noexcept = 0;
    /** Removes the framebuffers and frees the buffers. Safe to call at any time, also twice. */
    virtual void destroyBuffers() noexcept = 0;
    // For the GPU helper (gpu/gpu_overlay.h), which draws on the same plane through a copy of this object's descriptor. The defaults do nothing (fakes that do not care).
    /** The display descriptor (the application's, borrowed: never closed by the caller). -1 when there is none. Valid after open (). */
    virtual int displayFd() const noexcept { return -1; }
    /** Takes the plane off whatever put something on it (the helper may have). Safe at any time, also twice. */
    virtual void forceDisablePlane() noexcept {}
    /** Removes a framebuffer and a GEM handle that another process made on the shared descriptor (0 = none). */
    virtual void removeForeign (uint32_t /*fbId*/, uint32_t /*gemHandle*/) noexcept {}
    // What run-time discovery found (valid after open ()); the defaults are the numbers of the tests' fakes.
    virtual uint32_t crtcId() const noexcept { return 37; }
    virtual uint32_t planeId() const noexcept { return 38; }
    virtual PaceParams paceParams() const noexcept { return PaceParams(); }
    /** The plane reports the state we put it in (our CRTC, a framebuffer). false: another program took it (`why`). Read only. */
    virtual bool planeStillOurs (std::string& /*why*/) { return true; }
    /** The last check found the plane SWITCHED OFF (no CRTC, no framebuffer: a mode set of the application does that to every plane it does not name), not taken: the picture has to be set again on the
        same plane. Returns true once per such event. */
    virtual bool planeWasSwitchedOff() noexcept { return false; }
    /** The plane was lost: moves to the next free plane found by the discovery. false: no spare left. The next show () uses it. */
    virtual bool nextPlane (std::string& /*why*/) { return false; }
    /** Appends "key: value" lines about the discovery (device, CRTC, planes, refresh, how long it took) to the status file. */
    virtual void describe (std::string& /*out*/) const {}
    /** Statistics of the CPU path's presentation intervals (null: none). */
    virtual const PaceStats* paceStats() const noexcept { return nullptr; }
};

/** A read-only view of the pixels the application's primary plane shows (the markers of the skin). begin / pixel / end are called from the overlay thread only. */
class IFrameSource
{
public:
    virtual ~IFrameSource() = default;
    /** Takes hold of the framebuffer that is on screen now; reports its size in pixels. false: unreadable (no framebuffer, mapping failed). */
    virtual bool begin (int& panelW, int& panelH) = 0;
    /** The 0xRRGGBB value of the panel pixel (px, py) of the framebuffer taken by begin (). false outside of it. */
    virtual bool pixel (int px, int py, uint32_t& rgb) noexcept = 0;
    virtual void end() noexcept = 0;
    /** Non-empty: the source can never work (the display was not found, ...): the module disables itself at once with this reason. */
    virtual std::string fatalError() const { return {}; }
};

}} // namespace synthyboy_mpc::overlay
