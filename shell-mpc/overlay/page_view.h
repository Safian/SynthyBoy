// SynthyBoy - MPC overlay: a PAGE view, the view of a tab that the plugin draws entirely (docs/MPC.md "Plugin-drawn pages"): the filter, the mixer, the FX chain, the modulation matrix, the macros, the
// master page, the voice page, and the envelope / LFO pages of the instance the tab names. Like the Play view the region is the whole screen, the picture is one scene (CpuSink: the canvas,
// SceneSink: the GPU helper's display list), the host's Q-Link panel stays visible (the view is transparent in its strip), and the page has NO host widget: they are hidden while the page is up (the
// instance reports "Overlay Instance" frame 24, the skin hides the widgets at it) and the four Q-Links are the Q proxies Q 1 .. Q 4.
// The element behind the view is a class with the interface PageView needs (the same for every element, see the template parameter's members used below). A page is the full-screen page of its element:
// the controller keeps the element's page open for the instance on screen (IView::pageElement); the pages above it (lists, details) are the element's own.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../ui/theme.h"
#include "macro_names.h"
#include "page_detector.h"
#include "scene_cpu.h"
#include "ui_stack.h"
#include "view.h"

namespace synthyboy_mpc { namespace overlay {

namespace page
{
constexpr int kW = 1280, kH = 800, kTabY = 110, kTabH = 628;   // the screen, and the tab (the host's top bar above it, its function key bar below it)

/** What the controller hands an element besides the instance: the live values, the readout targets of this view (IView::targets order) and the instance number the tab names (Env 3, LFO 2, Filter 2: 3, 2, 2; 1 for the others). */
struct Input
{
    const synthyboy::LiveValues* live = nullptr;
    const float* targets = nullptr;
    int instance = 1;
};

/** The opaque ground of the tab (a gradient: a cached cell). */
inline void ground (gpu::SceneBuilder& sb)
{
    const ui::theme::Look& T = ui::theme::L();
    ui::Cell cell (sb, { 0, kTabY, kW, kTabH }, 0);
    sb.gradientRect (0.0f, static_cast<float> (kTabY), static_cast<float> (kW), static_cast<float> (kTabY + kTabH), ui::theme::rgba (T.bg0), ui::theme::rgba (T.bg1));
}

inline uint32_t sceneHash (const gpu::SceneData& d) noexcept
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < d.numOps; ++i)
    {
        const gpu::SceneOp& o = d.ops[i];
        h = fnv (h, static_cast<int32_t> (o.op));
        h = fnv (h, static_cast<int32_t> (o.rgba));
        h = fnv (h, static_cast<int32_t> (o.rgba2));
        for (float f : o.f)
        {
            int32_t bits;
            std::memcpy (&bits, &f, 4);   // the bits: a text op keeps its characters in two of them
            h = fnv (h, bits);
        }
    }
    return h;
}
} // namespace page

namespace page
{
/** How many Q slots the element publishes (qTargets () has that many entries): 16 for an element with a qSlotCount (), 4 for the others. */
template <class Ui, class = void>
struct QSlotCount { static int of (const Ui&) noexcept { return 4; } };
template <class Ui>
struct QSlotCount<Ui, decltype (void (std::declval<const Ui&>().qSlotCount()))> { static int of (const Ui& u) noexcept { return u.qSlotCount(); } };
} // namespace page

/** The elements a page can show are classes with:
      static constexpr int kElementPage   the UiStack id of the element's full page
      void update (IInstanceView&, const UiStack&, double nowMs, const page::Input&)
      void draw (gpu::SceneBuilder&) const        the picture of the page: ground excluded, popups and live parts included
      bool touch (const TouchEvent&, UiStack&, IInstanceView&, double nowMs);  void tick (double, UiStack&, IInstanceView&)
      bool fingerDown() const noexcept;  void dropGesture() noexcept;  void forgetQGroup() noexcept
      int topPage() const noexcept   (the status file) */
template <class Ui>
class PageView final : public IView
{
public:
    PageView (const char* name, int instance, const std::vector<int>& targets = {}) : name_ (name), instance_ (instance), targets_ (targets) {}

    const char* name() const noexcept override { return name_.c_str(); }
    void targets (std::vector<int>& out) const override { out.insert (out.end(), targets_.begin(), targets_.end()); }
    int pageElement() const noexcept override { return Ui::kElementPage; }

    bool update (const ViewInput& in) override
    {
        ++counters_.updates;
        if (in.io == nullptr || in.ui == nullptr)
            return false;
        bool changed = false;
        {
            const auto content = in.io->content();   // the names of the macros the pages show (macro_names.h): the ones of this instance, for everything this view does
            names_.load (content.get());
        }
        const NamesScope scope (names_);
        page::Input pi;
        pi.live = in.live;
        pi.targets = in.targets;
        pi.instance = instance_;
        ui_.update (*in.io, *in.ui, in.nowMs, pi);
        if (in.panelEdgeX != edgeX_)
        {
            edgeX_ = in.panelEdgeX;
            changed = true;
        }
        if (! scratch_)
            scratch_ = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*scratch_);
        sb.begin (gpu::kKindPlay, 0, page::kW, page::kH);
        buildPicture (sb);
        const uint32_t h = page::sceneHash (*scratch_);
        if (h != hash_ || ! have_)
        {
            hash_ = h;
            changed = true;
        }
        have_ = true;
        counters_.changes += changed;
        return changed;
    }
    bool valid() const noexcept override { return have_; }
    bool regionOverride (Rect& ui) const override
    {
        ui = { 0, 0, page::kW, page::kH };
        return true;
    }
    bool onTouch (const TouchEvent& ev, UiStack& stack, IInstanceView& io, double nowMs) override { const NamesScope scope (names_); return ! stack.isMain() && ui_.touch (ev, stack, io, nowMs); }
    bool onWheel (int x, int y, int notches, UiStack& stack, IInstanceView& io, double nowMs) override { const NamesScope scope (names_); return ! stack.isMain() && ui_.wheel (x, y, notches, stack, io, nowMs); }
    void onTick (UiStack& stack, IInstanceView& io, double nowMs) override { const NamesScope scope (names_); if (! stack.isMain()) ui_.tick (nowMs, stack, io); }
    void onShown() override { ui_.forgetQGroup(); }
    bool fingerDown() const noexcept override { return ui_.fingerDown(); }
    void dropGesture() noexcept override { ui_.dropGesture(); }

    void render (uint32_t* cv, int w, int h) override
    {
        const NamesScope scope (names_);
        if (! scratch_)
            scratch_ = std::make_unique<gpu::SceneData>();
        gpu::SceneBuilder sb (*scratch_);
        sb.begin (gpu::kKindPlay, 0, w, h);
        buildPicture (sb);
        std::vector<uint32_t> pixels;
        renderScene (*scratch_, pixels);
        std::memcpy (cv, pixels.data(), std::min (pixels.size(), static_cast<size_t> (w) * static_cast<size_t> (h)) * sizeof (uint32_t));
    }
    bool describe (gpu::SceneBuilder& sb, int viewIndex, int w, int h) const override
    {
        if (! have_ || w != page::kW || h != page::kH)
            return false;
        const NamesScope scope (names_);
        sb.begin (gpu::kKindPlay, static_cast<uint32_t> (viewIndex), w, h);
        buildPicture (sb);
        return ! sb.overflow;
    }
    uint32_t hash() const noexcept override { return hash_; }
    void writeStatus (std::ostream& o) const override
    {
        char hex[16];
        std::snprintf (hex, sizeof hex, "%08x", hash_);
        o << "page_view: " << name_ << "\npage_instance: " << instance_ << "\npage_top: " << ui_.topPage() << "\npage_hash: " << hex << "\n";
        const int* qt = ui_.qTargets();   // the Q group this page published: table index (the view in the bits above 16: kVirtual = 3 is "not a parameter"), -1 none
        o << "page_q_targets:";
        for (int i = 0; i < 4; ++i)
            o << ' ' << qt[i];
        o << "\n";
        const int slots = page::QSlotCount<Ui>::of (ui_);   // all the slots (Q 1 .. Q 16: the active group first, then the next groups of the page, q_publish.h)
        o << "page_q_slots:";
        for (int i = 0; i < slots; ++i)
            o << ' ' << qt[i];
        o << "\n";
    }
    void writeDump (std::ostream& o) const override { o << "# page view " << name_ << ": instance " << instance_ << ", top page " << ui_.topPage() << "\n"; }
    Ui& ui() noexcept { return ui_; }
    const Ui& ui() const noexcept { return ui_; }

private:
    /** The whole picture: transparent where the host keeps the screen (above the tab, in the strip of the Q-Link panel), the ground of the tab, the page. */
    void buildPicture (gpu::SceneBuilder& sb) const
    {
        sb.clear (0u);
        if (edgeX_ < page::kW)   // the host draws over the right part of the tab (its Q-Link panel): nothing of ours right of its left edge
            sb.clip (0.0f, 0.0f, static_cast<float> (edgeX_), static_cast<float> (page::kH));
        page::ground (sb);
        ui_.draw (sb);
    }

    std::string name_;
    int instance_;
    std::vector<int> targets_;
    Ui ui_;
    MacroNames names_;   // the macro names of the instance this view shows, loaded at every update
    std::unique_ptr<gpu::SceneData> scratch_;
    uint32_t hash_ = 0;
    bool have_ = false;
    int edgeX_ = 1280;   // the key (ViewInput::panelEdgeX)
};

}} // namespace synthyboy_mpc::overlay
