// MpcKit - OPTIONAL: a list of things the plugin owns that a page shows and the player picks from (presets of its own, samples, patterns, files). Not a parameter: the list can be long, it changes
// at run time and it is read by the overlay thread, so it has its own small interface (IContentSource) and a ready implementation over a constant array (StaticContent).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A page that needs a list of choices which ARE a parameter (a stepped parameter with names) does not need this: the binder (binder.h) opens the host list from the parameter's choices. This is for
// the other lists: the names come from the plugin, the selection is a number the plugin keeps (and saves in the chunk), the load happens off the overlay thread (the plugin's display thread).
// SynthyBoy's own sample / instrument browser (shell-mpc/overlay/content_browser.h, IInstanceView::contentRoots / requestContent) is the specific version of this; the migration plan keeps it there.
//
// THREADS: count () and name () are called from the overlay thread and must be lock free (a constant array, or a snapshot the plugin swaps atomically); select () only stores the request.
//
// NO DEPENDENCY on the synthesizer engine of SynthyBoy.
#pragma once

#include <atomic>

namespace mpckit {

class IContentSource
{
public:
    virtual ~IContentSource() = default;
    /** The title above the list ("PRESETS"). */
    virtual const char* title() const noexcept = 0;
    virtual int count() const noexcept = 0;
    /** The name of entry `index` (at most 12 characters are shown). */
    virtual const char* name (int index) const noexcept = 0;
    /** The selected entry, -1 when none. */
    virtual int selected() const noexcept = 0;
    /** The player picked entry `index`: the plugin stores the request and applies it on its own thread. */
    virtual void select (int index) noexcept = 0;
};

/** A ready IContentSource over a constant array of names; the selection is an atomic the plugin reads (selected ()) when it applies it. */
class StaticContent final : public IContentSource
{
public:
    StaticContent (const char* title, const char* const* names, int count) noexcept : title_ (title), names_ (names), count_ (count) {}
    const char* title() const noexcept override { return title_; }
    int count() const noexcept override { return count_; }
    const char* name (int index) const noexcept override { return index >= 0 && index < count_ ? names_[index] : ""; }
    int selected() const noexcept override { return selected_.load (std::memory_order_relaxed); }
    void select (int index) noexcept override { if (index >= 0 && index < count_) selected_.store (index, std::memory_order_relaxed); }

private:
    const char* title_;
    const char* const* names_;
    int count_;
    std::atomic<int> selected_ { 0 };
};

} // namespace mpckit
