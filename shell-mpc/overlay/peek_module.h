// SynthyBoy - the module page hosted inside the Mixer's modal panel.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "page_view.h"
#include "../ui/core.h"

namespace synthyboy_mpc { namespace overlay {

class PeekModule
{
public:
    virtual ~PeekModule() = default;
    static constexpr int kCloseWidth = 100;
    virtual void placePeek (ui::Rect panel) = 0;
    virtual void setNativePeek (bool enabled) noexcept = 0;
    virtual bool nativePeekReady() const noexcept = 0;
    virtual bool peekCloseVisible() const noexcept { return ! topPage(); }
    virtual ui::Rect nativePeekCloseRect() const noexcept = 0;
    virtual bool covers (int x, int y) const noexcept = 0;
    virtual void clearNativePeekApertures (gpu::SceneBuilder&) const = 0;
    virtual void leftTab (IInstanceView&) noexcept = 0;
    virtual void setNativeActionPopup (IInstanceView&, int) noexcept {}
    virtual ui::Component& peekRoot() noexcept = 0;
    virtual int topPage() const noexcept = 0;
    virtual const int* qTargets() const noexcept = 0;
    virtual const char* qGroupName() const noexcept = 0;
    virtual void update (IInstanceView&, const UiStack&, double, const page::Input&) = 0;
    virtual void draw (gpu::SceneBuilder&) const = 0;
    virtual bool touch (const TouchEvent&, UiStack&, IInstanceView&, double) = 0;
    virtual void tick (double, UiStack&, IInstanceView&) = 0;
    virtual bool wheel (int, int, int, UiStack&, IInstanceView&, double) = 0;
    virtual void turnQ (int, double, UiStack&, IInstanceView&) = 0;
    virtual bool fingerDown() const noexcept = 0;
    virtual void dropGesture() noexcept = 0;
    virtual void forgetQGroup() noexcept = 0;
};

}} // namespace synthyboy_mpc::overlay
