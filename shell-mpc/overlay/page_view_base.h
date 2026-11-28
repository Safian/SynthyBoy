// SynthyBoy - MPC overlay: the generic PAGE view, the view of a tab that the plugin draws entirely: the filter, the mixer, the FX chain, the modulation matrix, the macros, the
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

#include "../kit/page_input.h"
#include "../ui/core.h"
#include "../ui/theme.h"
#include "page_detector.h"
#include "scene_cpu.h"
#include "ui_stack.h"
#include "view.h"

namespace synthyboy_mpc { namespace overlay {

namespace page
{
constexpr int kW = 1280, kH = 800, kTabY = 110, kTabH = 628;   // the screen, and the tab (the host's top bar above it, its function key bar below it)

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
            std::memcpy (&bits, &f, 4);
            h = fnv (h, bits);
        }
    }
    // Text ops store offsets and lengths. Equal-length captions can change independently.
    for (uint32_t i = 0; i < d.numText && i < static_cast<uint32_t> (gpu::kMaxText); ++i)
        h = (h ^ static_cast<uint8_t> (d.text[i])) * 16777619u;
    // the rows of the polylines (the pool the Curve and Band ops index): a live line outside a cell (the spectrum) changes only there
    for (uint32_t i = 0; i < d.numPoints && i < static_cast<uint32_t> (gpu::kMaxVertices); ++i)
    {
        int32_t bits;
        std::memcpy (&bits, &d.y[i], 4);
        h = fnv (h, bits);
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

template <class Ui, class = void>
struct HasHostBackground : std::false_type {};
template <class Ui>
struct HasHostBackground<Ui, decltype (void (std::declval<const Ui&>().hostBackground()))> : std::true_type {};

template <class Ui, class = void>
struct HasSetHostBackground : std::false_type {};
template <class Ui>
struct HasSetHostBackground<Ui, decltype (void (std::declval<Ui&>().setHostBackground (true)))> : std::true_type {};

template <class Ui>
inline bool checkHostBackground (const Ui& ui) noexcept
{
    if constexpr (HasHostBackground<Ui>::value)
        return ui.hostBackground();
    else
        return false;
}

template <class Ui>
auto drawGround (const Ui& ui, gpu::SceneBuilder& sb, int) -> decltype (ui.drawPageGround (sb), void())
{
    ui.drawPageGround (sb);
}

template <class Ui>
void drawGround (const Ui& ui, gpu::SceneBuilder& sb, long)
{
    if (! checkHostBackground (ui))
        ground (sb);
}
} // namespace page

/** The hooks of a page view for a page that needs nothing of the plugin around it (a page of the kit over mpckit::IInstance): no names to load, no loading bar. A plugin with its own
    names, or a loading state, writes a class like MacroNameHooks (macro_names.h): `Instance`, `Input`, `update (IInstance&)`, `scope ()`, `loading (const IInstance&)`, `fill (Input&, const ViewInput&)`. */
struct NoHooks
{
    using Instance = mpckit::IInstance;
    using Input = mpckit::PageInput;   // what the page's update () gets besides the instance (the readout targets, the instance number); a plugin with more (SynthyBoy: the live values) derives from it
    struct Scope { Scope() noexcept {} ~Scope() {} };   // (user provided: a local of it is never "unused")
    void fill (Input&, const ViewInput&) const noexcept {}   // puts what the plugin adds to the input (SynthyBoy: the live block of the update)
    void update (mpckit::IInstance&) noexcept {}
    Scope scope() const noexcept { return Scope(); }
    bool loading (const mpckit::IInstance&) const noexcept { return false; }
};

/** The elements a page can show are classes with:
      static constexpr int kElementPage   the UiStack id of the element's full page
      void update (Hooks::Instance&, const UiStack&, double nowMs, const Hooks::Input&)   (Hooks::Instance: IInstanceView for the pages of SynthyBoy, mpckit::IInstance for a kit page)
      void draw (gpu::SceneBuilder&) const        the picture of the page: ground excluded, popups and live parts included
      bool touch (const TouchEvent&, UiStack&, Hooks::Instance&, double nowMs);  void tick (double, UiStack&, Hooks::Instance&)
      bool fingerDown() const noexcept;  void dropGesture() noexcept;  void forgetQGroup() noexcept
      int topPage() const noexcept   (the status file) */
/** `Hooks` (default: NoHooks, for the pages of the kit; SynthyBoy's are MacroNameHooks, page_view.h: PageView<Ui>) is what the view does around the page that is the plugin's: the instance type and the input type the page
    takes, the names that are active while the page updates, draws and handles input, the loading bar. */
template <class Ui, class Hooks = NoHooks>
class BasicPageView final : public IView
{
public:
    BasicPageView (const char* name, int instance, const std::vector<int>& targets = {}) : name_ (name), instance_ (instance), targets_ (targets) {}

    const char* name() const noexcept override { return name_.c_str(); }
    void targets (std::vector<int>& out) const override { out.insert (out.end(), targets_.begin(), targets_.end()); }
    int pageElement() const noexcept override { return Ui::kElementPage; }

    bool update (const ViewInput& in) override
    {
        ++counters_.updates;
        if (in.io == nullptr || in.ui == nullptr)
            return false;
        bool changed = false;
        typename Hooks::Instance& io = static_cast<typename Hooks::Instance&> (*in.io);   // (the generic ViewInput of view.h: the page takes the instance type of its plugin)
        io_ = &io;   // (for `leftTab`: the page that was on screen last)
        const bool loading = hooks_.loading (io);   // a wavetable of the program is still being built: the bar below
        if (loading != loading_)
        {
            loading_ = loading;
            changed = true;
        }
        hooks_.update (io);   // (SynthyBoy: the names of the macros the pages show, macro_names.h: the ones of this instance, for everything this view does)
        const auto scope = hooks_.scope();
        typename Hooks::Input pi;
        pi.targets = in.targets;
        pi.instance = instance_;
        hooks_.fill (pi, in);
        ui_.update (io, *in.ui, in.nowMs, pi);
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
    bool onTouch (const TouchEvent& ev, UiStack& stack, mpckit::IInstance& io, double nowMs) override { const auto scope = hooks_.scope(); return ! stack.isMain() && ui_.touch (ev, stack, static_cast<typename Hooks::Instance&> (io), nowMs); }
    bool onWheel (int x, int y, int notches, UiStack& stack, mpckit::IInstance& io, double nowMs) override { const auto scope = hooks_.scope(); return ! stack.isMain() && ui_.wheel (x, y, notches, stack, static_cast<typename Hooks::Instance&> (io), nowMs); }
    void onTick (UiStack& stack, mpckit::IInstance& io, double nowMs) override { const auto scope = hooks_.scope(); if (! stack.isMain()) ui_.tick (nowMs, stack, static_cast<typename Hooks::Instance&> (io)); }
    void onShown() override { ui_.forgetQGroup(); }
    bool fingerDown() const noexcept override { return ui_.fingerDown(); }
    void dropGesture() noexcept override { ui_.dropGesture(); }
    /** The page left the screen (a drawer or a dialog of the host over its markers): the finger that was down on it belongs to the host now and its Up never arrives here; a gesture kept would make the controller ask for a picture on every tick (fingerDown ()). */
    void onHidden() override { ui_.dropGesture(); leave (ui_, io_, 0); }

    /** A page element that has `leftTab (instance)` (the Env and LFO tabs: a selected, unused item falls back to the last used one, ui_bind.h `leaveSelector`) is told when its view leaves the screen. */
    template <class U, class I>
    static auto leave (U& u, I* io, int) -> decltype (u.leftTab (*io), void()) { if (io != nullptr) u.leftTab (*io); }
    template <class U, class I>
    static void leave (U&, I*, long) {}

    void setNativeActionPopup (int action) noexcept override { actionPopup (ui_, io_, action, 0); }

    template <class U, class I>
    static auto actionPopup (U& u, I* io, int action, int) noexcept -> decltype (u.setNativeActionPopup (*io, action), void())
    {
        if (io != nullptr)
            u.setNativeActionPopup (*io, action);
    }
    template <class U, class I>
    static void actionPopup (U&, I*, int, long) noexcept {}

    void render (uint32_t* cv, int w, int h) override
    {
        const auto scope = hooks_.scope();
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
        const auto scope = hooks_.scope();
        sb.begin (gpu::kKindPlay, static_cast<uint32_t> (viewIndex), w, h);
        buildPicture (sb);
        return ! sb.overflow;
    }
    void setHostBackground (bool on) override
    {
        if constexpr (page::HasSetHostBackground<Ui>::value)
            ui_.setHostBackground (on && hasSentinel());
        else
            (void) on;
    }
    uint32_t hash() const noexcept override { return hash_; }
    void writeStatus (std::ostream& o) const override
    {
        char hex[16];
        std::snprintf (hex, sizeof hex, "%08x", hash_);
        o << "page_view: " << name_ << "\npage_instance: " << instance_ << "\npage_top: " << ui_.topPage() << "\npage_hash: " << hex << "\n";
        if (hasSentinel())
            o << "page_host_background: " << (page::checkHostBackground (ui_) ? "on (sentinel read, transparent plane)" : "off (opaque plane)") << "\n";
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
        page::drawGround (ui_, sb, 0);
        ui_.draw (sb);
        if (loading_)   // the loading mark: a 4 px amber bar along the top edge of the tab (inside the Q-Link panel cut), drawn over the page, no text, no touch target
        {
            ui::Cell cell (sb, { 0, page::kTabY, page::kW, 4 }, 0);
            sb.rect (0.0f, static_cast<float> (page::kTabY), static_cast<float> (std::min (edgeX_, page::kW)), static_cast<float> (page::kTabY + 4), ui::theme::rgba (0xFFB000u));
        }
    }

    std::string name_;
    int instance_;
    std::vector<int> targets_;
    Ui ui_;
    typename Hooks::Instance* io_ = nullptr;   // the instance of the last update (leftTab)
    Hooks hooks_;   // what the view does around the page: the macro names of the instance this view shows (SynthyBoy), loaded at every update
    std::unique_ptr<gpu::SceneData> scratch_;
    uint32_t hash_ = 0;
    bool have_ = false;
    bool loading_ = false;   // Hooks::loading () (SynthyBoy: IInstanceView::tablesLoading ()) at the last update
    int edgeX_ = 1280;   // the key (ViewInput::panelEdgeX)
};

}} // namespace synthyboy_mpc::overlay
