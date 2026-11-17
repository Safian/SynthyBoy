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
    /** The display is on: the CRTC has a mode. false while the screen sleeps (DPMS: no mode, no framebuffer): a plane update then has nothing to wait for but a vblank that does not come, and the
        framebuffer of the host cannot be read; neither is a failure of the overlay. Cheap (one read-only call). */
    virtual bool displayOn() noexcept { return true; }
    /** Takes the plane off the CRTC (no framebuffer, no CRTC). Safe to call at any time, also twice. */
    virtual void disablePlane() noexcept = 0;
    /** Removes the framebuffers and frees the buffers. Safe to call at any time, also twice. */
    virtual void destroyBuffers() noexcept = 0;
    // For the GPU helper (gpu/gpu_overlay.h), which draws on the same plane through a copy of this object's descriptor. The defaults do nothing (fakes that do not care).
    /** The display descriptor (the application's, borrowed: never closed by the caller). -1 when there is none. Valid after open (). */
    virtual int displayFd() const noexcept { return -1; }
    /** Takes the plane off whatever put something on it (the helper may have). Safe at any time, also twice. */
    virtual void forceDisablePlane() noexcept {}
    /** Puts our plane where it must lie against the host's pointer plane (the zpos rule of discovery.h, stacking.h): called before the plane is first shown (the GPU path: before the helper starts and when the
        helper has set a plane that was switched off again), every time the plane is taken again. Reads before it writes; a failure is logged in the status text and is not fatal. Defaults: nothing. */
    virtual void takeStacking() {}
    /** Puts back what takeStacking changed (the zpos at rest): the overlay releases the plane, the helper is gone. Safe at any time, also twice. */
    virtual void releaseStacking() noexcept {}
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

/** Can a framebuffer of this layout be mapped and read as 4 byte pixels: pixels at least a row wide, sane sizes, nothing that overflows 32 bits or maps more than 256 MB. The source checks
    it before it maps a buffer the host made (a layout it cannot trust is refused, not read). */
inline bool frameLayoutReadable (uint32_t width, uint32_t height, uint32_t pitch, uint32_t offset) noexcept
{
    constexpr uint64_t kMaxSide = 16384, kMaxBytes = 256ull << 20;
    if (width == 0 || height == 0 || width > kMaxSide || height > kMaxSide)
        return false;
    if (static_cast<uint64_t> (pitch) < static_cast<uint64_t> (width) * 4)
        return false;
    if (((pitch | offset) & 3u) != 0)
        return false;   // a 4 byte read of device memory at an odd address is a bus error on ARM (robustness review F20)
    return static_cast<uint64_t> (offset) + static_cast<uint64_t> (pitch) * height <= kMaxBytes;
}
/** The byte position of pixel (px, py) in a mapping of `mappedBytes`: false when the pixel is outside the picture, the layout is not readable, or the 4 bytes of the pixel are not all inside the
    mapping. */
inline bool framePixelOffset (uint32_t width, uint32_t height, uint32_t pitch, uint32_t offset, size_t mappedBytes, int px, int py, size_t& at) noexcept
{
    if (px < 0 || py < 0 || static_cast<uint32_t> (px) >= width || static_cast<uint32_t> (py) >= height || ! frameLayoutReadable (width, height, pitch, offset))
        return false;
    const uint64_t pos = static_cast<uint64_t> (offset) + static_cast<uint64_t> (py) * pitch + static_cast<uint64_t> (px) * 4;
    if (pos + 4 > static_cast<uint64_t> (mappedBytes))
        return false;
    at = static_cast<size_t> (pos);
    return true;
}

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
    /** Lets go of everything the source keeps between two reads (cached mappings of the host's framebuffers, descriptors): the overlay is shutting down (the last instance closed) and must
        not hold the host's buffers any longer. Not in the middle of a read. The next begin () starts again. */
    virtual void release() noexcept {}
    /** Non-empty: the source can never work (the display was not found, ...): the module disables itself at once with this reason. */
    virtual std::string fatalError() const { return {}; }
};

}} // namespace synthyboy_mpc::overlay
