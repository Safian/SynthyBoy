// SynthyBoy - MPC overlay: from the mouse's events to the pages. One finger: the left button is touch down / up (at the pointer's position when it happens), movement with the button held is touch move (the
// position read again at most once per picture), the wheel is steps of the control under the pointer. The position is the host's own pointer plane (IPointerSource) moved by the arrow's hot spot and mapped
// to the UI by the rotation mapping (mouse_input.h). The hot spot is a pair per rotation (a measured table, else the corner rule) with a debug override file. Pure logic: the sinks are functions.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdio>
#include <functional>
#include <string>

#include "mouse_input.h"
#include "touch_decoder.h"

namespace synthyboy_mpc { namespace overlay {

// The Key 61's arrow tip in its pointer plane (rotation 90), measured on the device on 2026-11-16 from the plane's picture (docs/MPC.md "The mouse path"): the pointer plane is the application's CURSOR type plane 35, 64 x 64 AR24, the
// arrow is drawn turned with the screen, its tip is the outline pixel in the bottom left corner (0, 63), the same as the corner rule gives.
constexpr bool kKey61HotSpotMeasured = true;
constexpr int kKey61HotSpotX = 0, kKey61HotSpotY = 63;

/** The arrow tip measured on a device: (rotation, panel size) -> the pixel of the pointer plane's picture. false: not measured (the default rule is used). The Key 61's (rotation 90, panel 800 x 1280) is
    filled in from the measurement of the pointer plane's picture (docs/MPC.md "Mouse on the plugin-drawn pages"). */
inline bool measuredHotSpot (int rotation, int panelW, int panelH, int planeW, int planeH, HotSpot& out) noexcept
{
    (void) planeW;
    (void) planeH;
    if (rotation == 90 && panelW == 800 && panelH == 1280 && kKey61HotSpotMeasured)
    {
        out.x = kKey61HotSpotX;
        out.y = kKey61HotSpotY;
        out.source = "measured";
        return true;
    }
    return false;
}

class MouseRouter
{
public:
    using TouchSink = std::function<void (const TouchEvent&)>;
    using WheelSink = std::function<void (int x, int y, int notches)>;

    MouseRouter (IMouseInput& mouse, IPointerSource& pointer) : mouse_ (mouse), pointer_ (pointer) {}

    void configure (int panelW, int panelH, int rotation) noexcept
    {
        panelW_ = panelW;
        panelH_ = panelH;
        rotation_ = normaliseRotation (rotation);
    }
    /** The debug override of the hot spot (the text of the file: "x y" in plane pixels); empty or unreadable: none. */
    void setHotSpotOverride (const std::string& text)
    {
        int x = 0, y = 0;
        override_ = parseHotSpot (text, x, y);
        overrideX_ = x;
        overrideY_ = y;
    }
    bool buttonDown() const noexcept { return down_; }

    /** The mice are read; every event goes to a sink. `pictureMs`: the time between two pictures (a drag is read at most that often). */
    void poll (double nowMs, double pictureMs, const TouchSink& touch, const WheelSink& wheel)
    {
        bool moved = false;
        mouse_.poll ([&] (const MouseEvent& m)
        {
            ++events_;
            switch (m.kind)
            {
                case MouseEvent::Down: onDown (m, touch); break;
                case MouseEvent::Up: onUp (m, touch); break;
                case MouseEvent::Wheel: onWheel (m, wheel); break;
                case MouseEvent::Move: moved = true; break;
            }
        });
        if (moved && ! down_)
            readPending_ = true;
        if (readPending_ && ! down_ && nowMs - lastReadMs_ >= pictureMs)
        {
            readPending_ = false;
            // the mouse moves with no button down: the position is read for the status file (pointer_ui_xy follows the pointer, at most once per picture while it moves; nothing is sent to the pages)
            lastReadMs_ = nowMs;
            int x = 0, y = 0;
            position (x, y);
        }
        if (down_ && (moved || movePending_))
        {
            movePending_ = true;
            if (nowMs - lastMoveMs_ >= pictureMs)
            {
                lastMoveMs_ = nowMs;
                movePending_ = false;
                int x = 0, y = 0;
                if (position (x, y) && (x != lastX_ || y != lastY_))
                {
                    lastX_ = x;
                    lastY_ = y;
                    TouchEvent ev;
                    ev.kind = TouchEvent::Move;
                    ev.x = x;
                    ev.y = y;
                    touch (ev);
                    ++moves_;
                }
            }
        }
    }

    /** The pages are not on screen: the events of that time are thrown away, a running drag is over. */
    void idle()
    {
        mouse_.drain();
        down_ = false;
        movePending_ = false;
    }

    std::string describe() const
    {
        char b[512];
        std::snprintf (b, sizeof b,
                       "mouse_events: %lu\nmouse_downs: %lu\nmouse_moves: %lu\nmouse_ups: %lu\nmouse_wheel_events: %lu\nmouse_wheel_notches: %ld\nmouse_no_pointer: %lu\n"
                       "pointer_plane: %s\npointer_plane_xy: %d,%d (%dx%d)\npointer_ui_xy: %d,%d\npointer_hotspot: %d,%d (%s)\n",
                       events_, downs_, moves_, ups_, wheels_, notches_, noPointer_, planeSeen_ ? (lastPlane_.valid ? std::to_string (lastPlane_.id).c_str() : "none") : "not read yet", lastPlane_.x, lastPlane_.y, lastPlane_.w, lastPlane_.h, uiX_, uiY_,
                       hot_.x, hot_.y, hot_.source);
        return b + mouse_.describe() + pointer_.describe();
    }

private:
    bool position (int& x, int& y)
    {
        const PointerPlane p = pointer_.read();
        planeSeen_ = true;
        lastPlane_ = p;
        if (! p.valid || p.w <= 0 || p.h <= 0)
        {
            ++noPointer_;
            return false;
        }
        HotSpot hot = defaultHotSpot (rotation_, p.w, p.h);
        HotSpot measured;
        if (override_)
        {
            hot.x = overrideX_;
            hot.y = overrideY_;
            hot.source = "file";
        }
        else if (measuredHotSpot (rotation_, panelW_, panelH_, p.w, p.h, measured))
            hot = measured;
        hot_ = hot;
        pointerToUi (p, hot, rotation_, panelW_, panelH_, x, y);
        uiX_ = x;
        uiY_ = y;
        return true;
    }

    void onDown (const MouseEvent&, const TouchSink& touch)
    {
        ++downs_;
        if (down_)
            return;
        int x = 0, y = 0;
        if (! position (x, y))
            return;                // no pointer on the screen: no touch (the click goes to the host alone)
        down_ = true;
        lastX_ = x;
        lastY_ = y;
        lastMoveMs_ = -1.0e9;
        TouchEvent ev;
        ev.kind = TouchEvent::Down;
        ev.x = x;
        ev.y = y;
        touch (ev);
    }
    void onUp (const MouseEvent&, const TouchSink& touch)
    {
        ++ups_;
        if (! down_)
            return;
        int x = lastX_, y = lastY_;
        position (x, y);           // where the button went up; the last position when the pointer cannot be read
        down_ = false;
        movePending_ = false;
        TouchEvent ev;
        ev.kind = TouchEvent::Up;
        ev.x = x;
        ev.y = y;
        touch (ev);
    }
    void onWheel (const MouseEvent& m, const WheelSink& wheel)
    {
        ++wheels_;
        notches_ += m.notches;
        int x = 0, y = 0;
        if (position (x, y))
            wheel (x, y, m.notches);
    }

    IMouseInput& mouse_;
    IPointerSource& pointer_;
    int panelW_ = 800, panelH_ = 1280, rotation_ = 90;
    bool down_ = false, movePending_ = false, override_ = false, planeSeen_ = false;
    int overrideX_ = 0, overrideY_ = 0, lastX_ = 0, lastY_ = 0, uiX_ = 0, uiY_ = 0;
    double lastReadMs_ = -1000.0;
    bool readPending_ = false;   // the mouse moved with no button down: the position is read for the status file at the next picture time
    double lastMoveMs_ = -1.0e9;
    HotSpot hot_;
    PointerPlane lastPlane_;
    unsigned long events_ = 0, downs_ = 0, moves_ = 0, ups_ = 0, wheels_ = 0, noPointer_ = 0;
    long notches_ = 0;
};

}} // namespace synthyboy_mpc::overlay
