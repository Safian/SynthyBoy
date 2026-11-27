// MpcKit - PageInput: what the controller hands a page besides the instance. (The widget page base and the binder of the kit were deleted 2026-11-27: the C++ layer has no UI elements, the controls are native
// components of the host skin and the plugin holds only parameter binding data, shell-mpc/ui/binding.h.)
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../overlay/touch_decoder.h"
#include "../overlay/ui_stack.h"
#include "instance.h"

namespace mpckit {

using synthyboy_mpc::overlay::TouchEvent;
using synthyboy_mpc::overlay::UiStack;

/** What the controller hands a page besides the instance: the readout targets of this view and the instance number the tab names (page::Input of page_view.h derives from this one). */
struct PageInput
{
    const float* targets = nullptr;
    int instance = 1;
};

} // namespace mpckit
