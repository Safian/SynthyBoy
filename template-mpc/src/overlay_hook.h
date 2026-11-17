// MpcKit Demo - the ONE place where the plugin touches the overlay runtime: it registers an instance with the overlay (which then watches the screen for the plugin's tabs and draws the
// pages) and unregisters it. Everything else in the template is plugin code or page code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// STATUS: the overlay runtime of SynthyBoy (shell-mpc/overlay/: controller, service, page view, page detector ...) still names SynthyBoy's engine in a few places and is therefore not linked
// into the template yet. Migration step 8 (docs/ui-components/TEMPLATE.md, "Migration plan") turns it into the generic runtime `mpckit::Overlay`; this file then contains exactly
//     slot = mpckit::Overlay::attach (instance, demo::pageSet(), pageFactory);     and     mpckit::Overlay::detach (instance);
// and the build option MPCKIT_OVERLAY (template-mpc/CMakeLists.txt) switches it on. Until then the plugin is a complete, working VST2 instrument without plugin-drawn pages: the host's own
// widgets of the tabs (skin/pages.json) are what the player sees.
#pragma once

#include "demo_instance.h"
#include "demo_page_set.h"

#if defined(MPCKIT_OVERLAY) && MPCKIT_OVERLAY
#error "MPCKIT_OVERLAY needs migration step 8 of docs/ui-components/TEMPLATE.md (the generic overlay runtime): fill attachOverlay / detachOverlay below with the calls shown at the top of this file"
#endif

namespace demo {

/** Registers the instance with the overlay; returns its slot (0 .. 15), -1 when it has none (no overlay, or more than 16 instances). */
inline int attachOverlay(DemoInstance& /*instance*/) { return -1; }
inline void detachOverlay(DemoInstance& /*instance*/) {}

} // namespace demo
