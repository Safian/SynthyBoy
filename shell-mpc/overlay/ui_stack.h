// SynthyBoy - MPC overlay: the state of the overlay pages of ONE plugin instance (docs/MPC.md "Q proxies" and "Play page"): main -> full (an element) -> detail (a page) -> detail ...
// "main" is the Play page with its compact views visible; "full" is the full-screen page of one element (the oscillator, ...); "detail" is a page opened from it (a modulation list, a value entry, ...),
// possibly from another detail page. Pure, no dependencies, not thread safe by itself (the controller guards it) and NOT saved anywhere: a new instance starts in main, and the controller closes
// the whole stack when the page of the instance is no longer on screen.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy_mpc { namespace overlay {

class UiStack
{
public:
    enum class Level { main, full, detail };
    static constexpr int kMaxDepth = 8;   // one full page and up to seven detail pages above it

    /** The number of open pages: 0 = main. */
    int depth() const noexcept { return depth_; }
    bool isMain() const noexcept { return depth_ == 0; }
    /** The state of the top of the stack. */
    Level level() const noexcept { return depth_ == 0 ? Level::main : (depth_ == 1 ? Level::full : Level::detail); }
    /** The id the UI gave the page at position `i` (0 = the full page's element, 1 .. = detail pages); -1 outside the stack. */
    int id (int i) const noexcept { return i >= 0 && i < depth_ ? ids_[i] : -1; }
    /** The element of the full page (-1 in main) and the id of the top page (-1 in main). */
    int element() const noexcept { return depth_ > 0 ? ids_[0] : -1; }
    int top() const noexcept { return depth_ > 0 ? ids_[depth_ - 1] : -1; }
    /** Counts every change of the stack (the views redraw when it moved). */
    unsigned long changes() const noexcept { return changes_; }

    /** Opens the full-screen page of `element` (an id the UI chooses, >= 0) from main or from another full page (it replaces the whole stack: a full page has no parent but main). */
    bool openFull (int element) noexcept
    {
        if (element < 0)
            return false;
        if (depth_ == 1 && ids_[0] == element)
            return true;   // already open
        ids_[0] = element;
        depth_ = 1;
        ++changes_;
        return true;
    }
    /** Opens a detail page (id >= 0) on top of the full page or of another detail page. false: in main (there is no full page to come from) or the stack is full. */
    bool openDetail (int page) noexcept
    {
        if (page < 0 || depth_ == 0 || depth_ >= kMaxDepth)
            return false;
        ids_[depth_++] = page;
        ++changes_;
        return true;
    }
    /** One step back: the top page closes (the full page returns to main). false in main. */
    bool back() noexcept
    {
        if (depth_ == 0)
            return false;
        --depth_;
        ++changes_;
        return true;
    }
    /** Everything closes: main. false when it already was. */
    bool closeAll() noexcept
    {
        if (depth_ == 0)
            return false;
        depth_ = 0;
        ++changes_;
        return true;
    }

private:
    int ids_[kMaxDepth] = {};
    int depth_ = 0;
    unsigned long changes_ = 0;
};

}} // namespace synthyboy_mpc::overlay
