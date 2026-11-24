// SynthyBoy - MPC overlay: the presenter. Owns the overlay plane through an IPlaneBackend: shows a rectangle of the landscape UI, hands out the buffer to draw into,
// swaps the two buffers with a present (), takes everything off again on hide () / shutdown ().
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Any failure of the backend disables the presenter for good (disabled () stays true, reason () names the first failure); there is no retry. Not thread safe: the overlay
// thread owns it (the plugin's close and the exit handler only get here after that thread has stopped).
#pragma once

#include <string>

#include "drm_interfaces.h"

namespace synthyboy_mpc { namespace overlay {

class Presenter
{
public:
    explicit Presenter (IPlaneBackend& backend) : backend_ (backend) {}
    ~Presenter() { hide(); }
    Presenter (const Presenter&) = delete;
    Presenter& operator= (const Presenter&) = delete;

    bool disabled() const noexcept { return disabled_; }
    const std::string& reason() const noexcept { return reason_; }
    bool shown() const noexcept { return shown_; }
    bool opened() const noexcept { return opened_; }
    bool planeOn() const noexcept { return planeOn_; }
    /** The last present () found the display asleep and did nothing. */
    bool asleep() const noexcept { return asleep_; }
    int panelWidth() const noexcept { return panelW_; }
    int panelHeight() const noexcept { return panelH_; }
    const Rect& panelRect() const noexcept { return panelRect_; }

    /** Opens the backend (once) and learns the panel size, without making any buffer: the GPU path needs the descriptor and the size before it shows anything. false when disabled. */
    bool open()
    {
        if (disabled_)
            return false;
        if (opened_)
            return true;
        std::string error;
        if (! backend_.open (panelW_, panelH_, error))
            return fail ("open: " + error);
        opened_ = true;
        return true;
    }

    /** Makes ready to show `ui` (landscape UI rectangle) with the given rotation: opens the backend once, creates the two buffers. The plane itself goes on with the first
        present (). Calling it again with the same arguments does nothing; with another rectangle or rotation the plane is taken off and set up again. false when disabled. */
    bool show (const Rect& ui, int rotation)
    {
        if (disabled_)
            return false;
        rotation = normaliseRotation (rotation);
        if (shown_ && ui == ui_ && rotation == rotation_)
            return true;
        if (shown_)
            hide();
        if (! open())
            return false;
        std::string error;
        const Rect panel = uiRectToPanel (rotation, ui, panelW_, panelH_);
        if (panel.x < 0 || panel.y < 0 || panel.x + panel.w > panelW_ || panel.y + panel.h > panelH_ || panel.w <= 0 || panel.h <= 0)
            return fail ("the region does not fit the panel");
        if (! backend_.createBuffers (panel.w, panel.h, error))
        {
            backend_.destroyBuffers();
            return fail ("buffers: " + error);
        }
        ui_ = ui;
        rotation_ = rotation;
        panelRect_ = panel;
        shown_ = true;
        back_ = 0;
        return true;
    }

    /** The buffer to draw the next picture into (the one not on screen), row pitch in bytes. nullptr when not shown. */
    uint32_t* backBuffer (size_t& strideBytes) noexcept { return shown_ ? backend_.buffer (back_, strideBytes) : nullptr; }

    /** Puts the back buffer on the plane (waits for the vblank) and makes the other one the back buffer. */
    bool present()
    {
        if (! shown_ || disabled_)
            return false;
        asleep_ = ! backend_.displayOn();
        if (asleep_)
            return false;   // the screen sleeps: no plane update (it would wait for a vblank that does not come); not a failure, the next present () tries again
        std::string error;
        if (! backend_.setPlane (back_, panelRect_, error))
            return fail ("setplane: " + error);
        planeOn_ = true;
        back_ ^= 1;
        ++frames_;
        return true;
    }

    /** Plane off, framebuffers removed, buffers freed. Does nothing when nothing is shown. */
    void hide() noexcept
    {
        if (! shown_ && ! planeOn_)
            return;
        backend_.disablePlane();
        backend_.destroyBuffers();
        planeOn_ = false;
        shown_ = false;
    }

    /** The plane was taken by another program (or lost in another way): everything of ours is forgotten WITHOUT touching the plane (it is not ours any more); the buffers are freed. The
        next show () starts again, with the plane the backend names by then. */
    void abandon() noexcept
    {
        backend_.destroyBuffers();
        planeOn_ = false;
        shown_ = false;
    }

    unsigned long frames() const noexcept { return frames_; }

private:
    bool fail (const std::string& why)
    {
        if (! disabled_)
        {
            disabled_ = true;
            reason_ = why;
        }
        backend_.disablePlane();
        backend_.destroyBuffers();
        planeOn_ = false;
        shown_ = false;
        return false;
    }

    IPlaneBackend& backend_;
    bool opened_ = false, shown_ = false, planeOn_ = false, disabled_ = false, asleep_ = false;
    int panelW_ = 0, panelH_ = 0, rotation_ = 90, back_ = 0;
    Rect ui_, panelRect_;
    std::string reason_;
    unsigned long frames_ = 0;
};

}} // namespace synthyboy_mpc::overlay
