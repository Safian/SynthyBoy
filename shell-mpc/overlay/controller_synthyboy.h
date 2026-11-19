// SynthyBoy - MPC overlay: SynthyBoy's Controller. The Controller (controller.h) is the generic runtime of the kit: it is given the tabs of the plugin (mpckit::IPageSet) and the maker of their views
// (mpckit::IPageFactory) and knows no engine. This class is the Controller of SynthyBoy as the host tests build it: the tabs of overlay_layout.h (or a table of the same form), the factory of pages.h
// (SynthyBoyPageFactory, with the renderer and the fixed compute cost the views of the first overlay take). The plugin itself gives its tabs and its factory to mpckit::Overlay (service.h) and uses no
// SynthyBoyController.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../kit/synthyboy_adapter.h"
#include "controller.h"
#include "pages.h"

namespace synthyboy_mpc { namespace overlay {

namespace detail
{
/** The factory is a base that comes before the Controller, so it exists when the Controller is built. */
struct OwnedFactory
{
    SynthyBoyPageFactory factory;
    OwnedFactory (ICurveRenderer& renderer, double fixedCostMs) noexcept : factory (&renderer, fixedCostMs) {}
};
} // namespace detail

class SynthyBoyController : private detail::OwnedFactory, public Controller
{
public:
    /** `pages`: the tabs (null: SynthyBoy's, overlay_layout.h); the other arguments are those of the old Controller of SynthyBoy. */
    SynthyBoyController (IPlaneBackend& backend, IFrameSource& frames, ICurveRenderer& renderer, const Paths& paths, const Timing& timing = Timing(), double fixedComputeCostMs = -1.0,
                         gpu::IGpuOverlay* gpuOverlay = nullptr, ITouchInput* touchInput = nullptr, const mpckit::IPageSet* pages = nullptr, bool deferViews = false)
        : detail::OwnedFactory (renderer, fixedComputeCostMs),
          Controller (backend, frames, paths, timing, gpuOverlay, touchInput, pages != nullptr ? *pages : mpckit::synthyboy_adapter::PageSet::get(), detail::OwnedFactory::factory, deferViews) {}
};

}} // namespace synthyboy_mpc::overlay
