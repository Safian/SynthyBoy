// MpcKit - the OVERLAY: the runtime that draws a plugin's pages on a free display plane over the host's picture, finds which of the plugin's tabs is on screen (the page detector), takes the touch screen and the
// mouse, and keeps the host's Q-Link panel. A plugin registers each instance with it and unregisters it at effClose; the rest is the runtime's. Header only; the implementation is shell-mpc/overlay/ (service.h:
// the one overlay thread of the process, controller.h: pictures and input, page_view_base.h: the view of a page), which is Linux code for the device and compiles to nothing useful elsewhere.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//     slot = mpckit::Overlay::attach (instance, pageSet, pageFactory, &branding);     // at the end of the plugin's entry function (effOpen / main)
//     mpckit::Overlay::detach (instance);                                             // at effClose, before the instance is freed
//
// `pageSet` (page_set.h IPageSet): the plugin's tabs with their marker colours; `pageFactory` (IPageFactory): makes the view of each tab, typically a `mpckit::PageView<MyPage>` (the page class derives from
// PageBase). `branding` (branding.h): the prefix of the files in /tmp and the name of the GPU helper. The first instance of the process decides all three. The returned slot (0 .. 15, -1 for none) goes to the
// instance, which tells it to the host through the display parameter "Overlay Instance" (the skin shows the slot as a colour: the overlay reads it to know which instance's page is on screen).
//
// THREADS. attach and detach are called from the host's threads; the overlay thread is the runtime's own; everything a page does runs on it (instance.h).
#pragma once

#include <functional>
#include <string>

#include "../overlay/page_view_base.h"
#include "../overlay/service.h"
#include "branding.h"
#include "instance.h"
#include "page_set.h"

namespace mpckit {

/** The hooks of a page view that needs nothing of the plugin around it, and the page view over a page of the kit (PageBase). */
using NoHooks = synthyboy_mpc::overlay::NoHooks;
template <class Ui, class Hooks = NoHooks>
using PageView = synthyboy_mpc::overlay::BasicPageView<Ui, Hooks>;

class Overlay
{
public:
    /** Registers the instance with the overlay and returns its slot (0 .. 15; -1: more than 16 instances, or the pages are refused: validatePages). The pages and the factory must outlive the overlay (constants of the plugin). */
    static int attach (IInstance& instance, const IPageSet& pages, IPageFactory& factory, const Branding* branding = nullptr)
    {
        return synthyboy_mpc::overlay::Service::instance().attach (&instance, &pages, &factory, branding);
    }
    /** Unregisters the instance: after it returns the overlay thread no longer touches it. false: the overlay thread did not let go in time (a call that does not return): the instance must not be freed. */
    static bool detach (IInstance& instance) { return synthyboy_mpc::overlay::Service::instance().detach (&instance); }
    /** Adds lines ("key: value\n" each) to the overlay's status file (the plugin's own diagnostics). Call it before attach. */
    static void setStatus (std::function<std::string()> lines) { synthyboy_mpc::overlay::Service::instance().setStatus (std::move (lines)); }
};

} // namespace mpckit
