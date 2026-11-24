// SynthyBoy - MPC overlay: the touch screen as the controller sees it (an interface, so that the tests have a fake and the device has the evdev reader of touch_linux.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include <string>

#include "touch_decoder.h"

namespace synthyboy_mpc { namespace overlay {

/** The touch screen as the controller sees it: told the panel and the rotation, polled once per step, hands every decoded touch event (upright UI pixels, touch_decoder.h) to the handler. */
class ITouchInput
{
public:
    virtual ~ITouchInput() = default;
    virtual void configure (int panelW, int panelH, int rotation) = 0;
    /** Reads what is pending (never blocks) and calls handler for each touch event; <0: the device is not there. */
    virtual int poll (const std::function<void (const TouchEvent&)>& handler) = 0;
    /** `key: value` lines of what the input found (the status file). */
    virtual std::string describe() const { return ""; }
};

}} // namespace synthyboy_mpc::overlay
