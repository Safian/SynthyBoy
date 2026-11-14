// SynthyBoy - MPC overlay: the two small interfaces to the display driver. The real implementations are in drm_linux.h; the tests use fakes (tests/fakes.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "geometry.h"

namespace synthyboy_mpc { namespace overlay {

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
};

}} // namespace synthyboy_mpc::overlay
