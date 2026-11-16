// SynthyBoy - MPC overlay: a USB mouse on the plugin-drawn pages, as one finger. The mouse is read from its evdev node (the buttons and the wheel only, read only, never grabbed: the host keeps getting its
// events), the position is NOT integrated from the relative movements: the host owns the pointer and its acceleration, so the position is read from the host's pointer plane (the application's small plane on
// our CRTC: its CRTC_X / CRTC_Y), moved by the arrow's hot spot, and mapped to the UI with the rotation mapping the overlay already has (geometry.h). Left button = touch down / up, movement with the button
// held = touch move (read at the display rate), the wheel = steps of the control under the pointer. Pure logic here: the decoder of the evdev events, the interfaces, the hot spot and the mapping.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

#include "geometry.h"

namespace synthyboy_mpc { namespace overlay {

/** What the mouse did, without any position (the position is read from the pointer plane when the event is handled). */
struct MouseEvent
{
    enum Kind { Down, Move, Up, Wheel };
    Kind kind = Down;
    int notches = 0;        // Wheel: positive away from the user ("up")
    uint64_t timeUs = 0;
    int device = 0;         // which mouse (the order they were found in)
};

/** The decoder of the evdev events of ONE mouse: BTN_LEFT, REL_X / REL_Y (movement, only to know that the pointer moved), REL_WHEEL. The other buttons and axes are not ours (the host takes them). */
class MouseDecoder
{
public:
    static constexpr uint16_t kEvSyn = 0x00, kEvKey = 0x01, kEvRel = 0x02;
    static constexpr uint16_t kSynReport = 0, kSynDropped = 3;
    static constexpr uint16_t kRelX = 0x00, kRelY = 0x01, kRelWheel = 0x08;
    static constexpr uint16_t kBtnLeft = 0x110;

    bool buttonDown() const noexcept { return down_; }
    void reset() noexcept { down_ = false; moved_ = false; wheel_ = 0; }

    /** One evdev event. Handler gets a MouseEvent per button change, one Move per report in which the pointer moved with the button held, and one Wheel per report with the notches summed. */
    template <class Handler>
    void feed (uint16_t type, uint16_t code, int32_t value, uint64_t timeUs, int device, Handler&& handler)
    {
        MouseEvent ev;
        ev.timeUs = timeUs;
        ev.device = device;
        if (type == kEvKey && code == kBtnLeft)
        {
            const bool d = value != 0;
            if (d == down_)
                return;                // a repeat
            down_ = d;
            moved_ = false;
            ev.kind = d ? MouseEvent::Down : MouseEvent::Up;
            handler (ev);
        }
        else if (type == kEvRel && (code == kRelX || code == kRelY))
            moved_ = value != 0 || moved_;
        else if (type == kEvRel && code == kRelWheel)
            wheel_ += value;
        else if (type == kEvSyn && code == kSynDropped)
        {
            // events were lost: whatever was half done is dropped; a button that was down stays down until the Up arrives (or the device goes)
            moved_ = false;
            wheel_ = 0;
        }
        else if (type == kEvSyn && code == kSynReport)
        {
            if (wheel_ != 0)
            {
                ev.kind = MouseEvent::Wheel;
                ev.notches = wheel_;
                handler (ev);
            }
            if (moved_ && down_)
            {
                ev.kind = MouseEvent::Move;
                ev.notches = 0;
                handler (ev);
            }
            wheel_ = 0;
            moved_ = false;
        }
    }

private:
    bool down_ = false, moved_ = false;
    int wheel_ = 0;
};

/** The mice as the controller sees them (an interface: the tests have a fake, the device has the evdev reader of mouse_linux.h). */
class IMouseInput
{
public:
    virtual ~IMouseInput() = default;
    /** Reads what is pending of every mouse (never blocks; finds new ones and drops those that went, without a restart) and calls handler for each event. */
    virtual void poll (const std::function<void (const MouseEvent&)>& handler) = 0;
    /** Reads and throws away what is pending (the pages are not on screen: the events of that time must not arrive later). No search for devices. */
    virtual void drain() = 0;
    /** `key: value` lines (the status file). */
    virtual std::string describe() const { return ""; }
};

/** The host's pointer plane now: where it is on the panel (CRTC_X / CRTC_Y of the application's small plane on our CRTC) and its size. */
struct PointerPlane
{
    bool valid = false;      // false: the host shows no pointer (the plane is not on the CRTC), or it cannot be read
    uint32_t id = 0;
    int x = 0, y = 0;        // panel pixels of the plane's top left corner (may be negative: the pointer at the edge)
    int w = 0, h = 0;
};

class IPointerSource
{
public:
    virtual ~IPointerSource() = default;
    virtual PointerPlane read() = 0;
    virtual std::string describe() const { return ""; }
};

/** The pixel of the pointer plane's picture that is the arrow's tip, in the plane's own (panel) orientation. */
struct HotSpot
{
    int x = 0, y = 0;
    const char* source = "default";   // "default" (the rule below), "measured" (the table of devices), "file" (the debug override)
};

/** The default: the arrow is drawn upright in the UI, its tip at the UI's top left corner of the picture; that corner lies where the rotation maps the UI's (0, 0) in a w x h plane (90: the bottom left,
    270: the top right, 0: the top left, 180: the bottom right). A measurement on a device replaces it (the table in `measuredHotSpot`). */
inline HotSpot defaultHotSpot (int rotation, int w, int h) noexcept
{
    HotSpot s;
    uiPointToPanel (rotation, 0, 0, w, h, s.x, s.y);
    return s;
}

/** The UI pixel under the arrow's tip. `hot`: the hot spot in plane pixels. */
inline void pointerToUi (const PointerPlane& p, const HotSpot& hot, int rotation, int panelW, int panelH, int& ux, int& uy) noexcept
{
    panelPointToUi (rotation, p.x + hot.x, p.y + hot.y, panelW, panelH, ux, uy);
}

/** The text "hx,hy" of a debug override file ("12 40" or "12,40"); false when it is not two numbers. */
inline bool parseHotSpot (const std::string& text, int& x, int& y) noexcept
{
    int a = 0, b = 0;
    if (std::sscanf (text.c_str(), "%d%*[ ,;\t]%d", &a, &b) == 2 && a >= -256 && a <= 256 && b >= -256 && b <= 256)
    {
        x = a;
        y = b;
        return true;
    }
    return false;
}

}} // namespace synthyboy_mpc::overlay
