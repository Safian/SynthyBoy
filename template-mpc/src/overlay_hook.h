// MpcKit Demo - the ONE place where the plugin touches the overlay runtime: it registers an instance with the overlay (which then watches the screen for the plugin's tabs and draws the
// pages) and unregisters it. Everything else in the template is plugin code or page code.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// With the build option MPCKIT_OVERLAY (template-mpc/CMakeLists.txt; a Linux target, the device) the hook below is
//     slot = mpckit::Overlay::attach (instance, demo::pageSet(), factory, &demo::kBranding);     and     mpckit::Overlay::detach (instance);
// (shell-mpc/kit/overlay.h): the runtime of SynthyBoy's overlay (shell-mpc/overlay/), which knows no engine since migration step 8 (docs/ui-components/TEMPLATE.md). Without the option the plugin is a complete,
// working VST2 instrument without plugin-drawn pages: the host's own widgets of the tabs (skin/pages.json) are what the player sees.
//
// TO ADD A PAGE: a `case` in DemoPageFactory::make () below (the page kind of demo_page_set.h and the page class), next to the other lines.
#pragma once

#include "demo_instance.h"
#include "demo_page_set.h"

#if defined(MPCKIT_OVERLAY) && MPCKIT_OVERLAY
#include <memory>

#include "../../shell-mpc/kit/overlay.h"
#include "pages/list_page.h"
#include "pages/main_page.h"
#include "pages/shape_page.h"
#endif

namespace demo {

#if defined(MPCKIT_OVERLAY) && MPCKIT_OVERLAY

/** The names the runtime gives its files and its helper: nothing of SynthyBoy's (/tmp/mpckit-demo-overlay-off, <folder of the plugin>/mpckit-demo-overlay-gpu-<arch>, the environment variable MPCKIT_DEMO_Q_KNOBS). */
inline const mpckit::Branding kBranding { "mpckit-demo", "mpckit-demo-overlay-gpu", "MPCKIT_DEMO_Q_KNOBS" };

/** Makes the view of each tab: a mpckit::PageView of the page class (the view around a PageBase page: the instance and the readout targets it takes, the picture, the touch). */
class DemoPageFactory final : public mpckit::IPageFactory
{
public:
    std::unique_ptr<synthyboy_mpc::overlay::IView> make (const mpckit::PageDef& d) const override
    {
        switch (d.kind)
        {
            case kKindMain: return std::make_unique<mpckit::PageView<MainPage>> (d.name, d.index);
            case kKindShape: return std::make_unique<mpckit::PageView<ShapePage>> (d.name, d.index);
            case kKindList: return std::make_unique<mpckit::PageView<ListPage>> (d.name, d.index);
            default: return nullptr;
        }
    }
};

/** Registers the instance with the overlay; returns its slot (0 .. 15), -1 when it has none (more than 16 instances, or the tabs are refused). */
inline int attachOverlay (DemoInstance& instance)
{
    static DemoPageFactory factory;
    return mpckit::Overlay::attach (instance, pageSet(), factory, &kBranding);
}
inline void detachOverlay (DemoInstance& instance) { mpckit::Overlay::detach (instance); }

#else

/** No overlay in this build: no slot, nothing to unregister. */
inline int attachOverlay (DemoInstance& /*instance*/) { return -1; }
inline void detachOverlay (DemoInstance& /*instance*/) {}

#endif

} // namespace demo
