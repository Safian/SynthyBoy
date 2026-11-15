// SynthyBoy - MPC overlay: the interface of a view. A view is a region of one tab (overlay_layout.json) with a data source and a draw function. The controller owns one
// object per view of the layout, shows at most one of them (the one whose marker pair is on screen) and calls update () / render () of that one only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <ostream>

#include "curve_source.h"
#include "drm_interfaces.h"
#include "gpu/scene.h"
#include "overlay_layout.h"
#include "touch_decoder.h"
#include "synthyboy/readout.h"
#include "ui_stack.h"

namespace synthyboy_mpc { namespace overlay {

/** What a view gets for one update (everything is valid for the call only). */
struct ViewInput
{
    const IInstanceView* instance = nullptr;
    const synthyboy::LiveValues* live = nullptr;   // null: the engine has written nothing yet
    const float* targets = nullptr;                // the readout values of this view's targets (null with `live`), in the order of IView::targets ()
    double nowMs = 0.0;
    IFrameSource* frames = nullptr;                // the application's primary framebuffer (the envelope view checks the line colour with it)
    int rotation = 90;
    bool qlinkPanel = false;                       // the host's Q-Link panel is drawn over the right edge (a hardware knob is being turned): a view must not draw there
    IInstanceView* io = nullptr;                   // the same instance, writable: the plugin-drawn pages write parameters through it (null in a test that has none)
    const UiStack* ui = nullptr;                   // the plugin-drawn pages of this instance (main / full / detail, ui_stack.h); null in a test that has none. The views of today ignore it.
};

/** Per view counters of the status file. */
struct ViewCounters
{
    unsigned long updates = 0, changes = 0, recomputes = 0;
    double lastComputeMs = 0.0;
};

class IView
{
public:
    virtual ~IView() = default;
    virtual const char* name() const noexcept = 0;
    /** The table indices of the parameters this view wants in the engine's readout (appended to `out`; at most 8). */
    virtual void targets (std::vector<int>& out) const = 0;
    /** Reads the inputs and refreshes the data. true: the picture must be drawn again. Never throws what the draw needs; allocation only on the first call and when the content changes. */
    virtual bool update (const ViewInput& in) = 0;
    /** There is something to show (the data exists). false: the controller keeps the plane off ("no data yet"). */
    virtual bool valid() const noexcept = 0;
    /** Draws the whole picture into the canvas (the size of the region, premultiplied ARGB8888, tightly packed), opaque views with their dark background. */
    virtual void render (uint32_t* canvas, int w, int h) = 0;
    /** The same picture as a scene for the GPU helper (gpu/scene.h): a display list in canvas pixels of a region of w x h. false: this view cannot be described (the CPU draws it). */
    virtual bool describe (gpu::SceneBuilder&, int /*viewIndex*/, int /*w*/, int /*h*/) const { return false; }
    /** The view wants another rectangle than the layout's (UI pixels of the screen, y from the top of the screen); false: the layout's. Constant for the life of the view. */
    virtual bool regionOverride (Rect&) const { return false; }
    /** A touch in UI pixels (screen coordinates). `stack` is the instance's UiStack (the caller holds its lock: the view may open and close pages), `io` the instance. true: the touch belongs to this
        view's plugin-drawn pages (the application gets it as well: the screen cannot keep it from the host). */
    virtual bool onTouch (const TouchEvent&, UiStack&, IInstanceView&, double /*nowMs*/) { return false; }
    /** The clock while a finger is down (long press). */
    virtual void onTick (UiStack&, IInstanceView&, double /*nowMs*/) {}
    /** A finger is down on the plugin-drawn pages (the controller keeps its pace fast). */
    virtual bool fingerDown() const noexcept { return false; }
    /** The stack of the instance was closed from outside (the Play page left): a running gesture ends. */
    virtual void dropGesture() noexcept {}
    /** Hash (FNV-1a) of what is drawn, for the status file. */
    virtual uint32_t hash() const noexcept = 0;
    /** `key: value` lines of this view's own numbers (the status file, one second). */
    virtual void writeStatus (std::ostream& out) const = 0;
    /** The data of the picture as text (the dump switch). */
    virtual void writeDump (std::ostream& out) const = 0;
    const ViewCounters& counters() const noexcept { return counters_; }
    /** A view made new for another instance carries on counting from the numbers of the one it replaces (the status file counts per view, not per instance). */
    void adoptCounters (const ViewCounters& c) noexcept { counters_ = c; }

protected:
    ViewCounters counters_;
};

inline uint32_t fnv (uint32_t h, int32_t v) noexcept
{
    for (int b = 0; b < 4; ++b)
        h = (h ^ ((static_cast<uint32_t> (v) >> (8 * b)) & 255u)) * 16777619u;
    return h;
}

}} // namespace synthyboy_mpc::overlay
