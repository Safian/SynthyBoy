// MpcKit - the PAGES of a plugin as the overlay runtime and the skin generator see them: the list of tabs with their marker colours (IPageSet, PageDef), the instance colour table, and the check
// that the colours can be told apart (validatePages), which is what scripts/gen-overlay-layout-header.py checks for the SynthyBoy layout.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// HOW THE OVERLAY FINDS WHICH PAGE IS ON SCREEN. The overlay draws on a free display plane over the host's picture. It cannot ask the host which tab is showing, so the SKIN tells it: on every tab
// the skin draws two tiny solid squares (the MARKERS, 6 x 6 pixels, in colours no other tab uses) and a film strip image bound to the display parameter "Overlay Instance" that shows the colour of the
// instance's SLOT. The overlay reads those pixels from the host's framebuffer (the page detector): both markers of a tab seen = that tab is up; the instance colour says which plugin instance's page
// it is. The numbers (positions, colours) must be the same in the skin (generated) and in the plugin (a constant table): they live in ONE JSON file, template-mpc/skin/overlay_layout.json for the
// template (shell-mpc/overlay/overlay_layout.json for SynthyBoy), and a generator writes the C++ table from it. IPageSet is how the plugin hands that table to the runtime.
//
// A plugin's tabs on the MPC: a function key of the MPC UI has up to 8 sub-tabs, a skin up to 32 tabs (the SDK limit). Every PAGE the plugin draws entirely is one tab of the skin; its host widgets
// stay in the skin (the tab works with the overlay off) and are hidden while the page is up (RangedEnabling on "Overlay Instance", frame 24).
//
// NO DEPENDENCY on the synthesizer engine of SynthyBoy: standard library only (the view factory refers to the overlay's IView by a forward declaration).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace synthyboy_mpc { namespace overlay { class IView; } }

namespace mpckit {

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

/** A marker square: its place in the tab (tab coordinates, the tab is 1280 x 628) and its colour 0xRRGGBB. `cover` is the colour of what lies under it (an opaque page paints its dark picture over the marker). */
struct MarkerDef
{
    Rect rect;
    uint32_t rgb = 0;
    uint32_t cover = 0x1e1d1e;
};

/** One tab the plugin draws. `kind` is the plugin's own number of the page class (the factory switches on it; page kinds are >= kFirstPageKind), `index` the instance number the tab names (Env 3 -> 3;
    1 for a plugin that has one of everything). */
struct PageDef
{
    const char* name = "";    // the id of the view ("main"), unique
    const char* tab = "";     // the name of the tab in the skin ("Main"): what the player reads on the function key; the skin generator finds the tab by it
    int kind = 0;
    int index = 1;
    Rect region;              // the picture's rectangle in SCREEN coordinates; a page covers the whole tab: { 0, 110, 1280, 628 }
    bool opaque = true;
    MarkerDef markers[2];     // left, right
    Rect instance;            // the film strip image next to the markers (6 x 6)
};

constexpr int kPlayKind = 5;        // the Play tab of SynthyBoy (a page drawn entirely, with the full screens of its elements over it); a plugin without one never uses the number
constexpr int kFirstPageKind = 6;   // kinds below are the pictures of the first overlay (gone); isPageKind is kind >= kFirstPageKind or the Play kind 5
constexpr int kScreenW = 1280, kScreenH = 800, kTabOriginY = 110, kTabH = 628;
constexpr int kMaxInstances = 16;
constexpr int kInstanceSteps = 32;    // the display parameter "Overlay Instance" has this many steps: frame f is the value f / kInstanceSteps (exact in a float)
constexpr int kInstanceFrames = 33;   // frames of the film strip: 0 blank, 1 .. 16 slot 0 .. 15, 17 none, the rest blank
constexpr int kMaxPages = 40;         // the page detector's limit (Detection::kMaxViews)
constexpr uint32_t kBackgroundRgb = 0x1e1d1e;   // the colour of the dark picture of an opaque page (the host's own background)

/** A page the plugin draws entirely (its own touch handling, the host widgets hidden under it): the Play kind or a kind from kFirstPageKind. */
constexpr bool isPageKind (int kind) noexcept { return kind == kPlayKind || kind >= kFirstPageKind; }

/** The colours of the instance film strip: slot 0 .. 15, "none" (an instance beyond the 16 slots), "blank" (nothing reported yet). The defaults are SynthyBoy's, which a second plugin can keep:
    the colours only have to differ from the markers of the SAME skin. */
struct InstanceColours
{
    uint32_t slot[kMaxInstances] = { 0x1414d8, 0x3e14d8, 0x6814d8, 0x9214d8, 0x143ed8, 0x3e3ed8, 0x683ed8, 0x923ed8, 0x1468d8, 0x3e68d8, 0x6868d8, 0x9268d8, 0x1492d8, 0x3e92d8, 0x6892d8, 0x9292d8 };
    uint32_t none = 0xc814d8;
    uint32_t blank = 0x14c8d8;
};

/** The pages of a plugin. A constant object of the plugin (the table is constexpr data); every method is const and thread safe. */
class IPageSet
{
public:
    virtual ~IPageSet() = default;
    virtual int numPages() const noexcept = 0;
    virtual const PageDef& page (int index) const noexcept = 0;
    virtual const InstanceColours& instanceColours() const noexcept = 0;
};

/** Add a view's ordered range only when the same readouts are not already in the plan. */
inline int appendReadoutRange (std::vector<int>& plan, const std::vector<int>& range)
{
    if (range.empty())
        return 0;
    const auto found = std::search (plan.begin(), plan.end(), range.begin(), range.end());
    if (found != plan.end())
        return static_cast<int> (found - plan.begin());
    const int first = static_cast<int> (plan.size());
    plan.insert (plan.end(), range.begin(), range.end());
    return first;
}

/** The overlay's factory of the page views: the plugin makes the object behind a PageDef (a PageView<Ui> of its own page class, page_view.h after the migration). Called once per page when the overlay
    starts, from the overlay thread. */
class IPageFactory
{
public:
    virtual ~IPageFactory() = default;
    virtual std::unique_ptr<synthyboy_mpc::overlay::IView> make (const PageDef& page) const = 0;
    /** The parameter indices all pages together want in the engine's live readout (IView::targets of every view); empty for a plugin that has none. */
    virtual void readoutTargets (std::vector<int>& out) const { out.clear(); }
    /** The size in bytes of the plugin's live block (IInstance::readLive: the controller reads it once a picture and hands it to the views as ViewInput::live); 0: the plugin has none. */
    virtual size_t liveBytes() const noexcept { return 0; }
    /** The readout values of a view whose targets start at `base` in the list of readoutTargets and number `count`: a pointer into `live` (the block just read), null when they are not all there. */
    virtual const float* viewTargets (const void* /*live*/, int /*base*/, int /*count*/) const noexcept { return nullptr; }
};

/** Two colours are told apart by the page detector when at least one channel differs by more than 4 (the generator uses the same rule; the detector's own tolerance is 2). */
inline bool colourClose (uint32_t a, uint32_t b) noexcept
{
    for (int shift = 0; shift <= 16; shift += 8)
        if (std::abs (static_cast<int> ((a >> shift) & 255u) - static_cast<int> ((b >> shift) & 255u)) > 4)
            return false;
    return true;
}

/** The checks of the layout, the same ones as scripts/gen-overlay-layout-header.py: exactly two markers per page that no other marker is close to, instance colours that differ from each other and from every
    marker, from the background of an opaque picture and from the colour under every marker, the instance picture inside the region (at least 4 x 4) and clear of the markers, at most 24 pages (the detector's
    limit). The kind of a page is not checked (a table may hold the pictures of the first overlay). Returns true when all hold; otherwise `why` says what is wrong. */
inline bool validatePages (const IPageSet& set, std::string& why)
{
    const int n = set.numPages();
    if (n < 1 || n > kMaxPages)
    {
        why = "a plugin has 1 .. 40 pages (the page detector holds 40 views)";
        return false;
    }
    std::vector<uint32_t> marker, under;   // `under`: the background of an opaque picture and what lies under every marker
    under.push_back (kBackgroundRgb);
    for (int p = 0; p < n; ++p)
    {
        const PageDef& d = set.page (p);
        for (const MarkerDef& m : d.markers)
        {
            for (uint32_t other : marker)
                if (colourClose (m.rgb, other))
                {
                    why = std::string ("page ") + d.name + ": a marker colour is within the detector's tolerance of another marker";
                    return false;
                }
            marker.push_back (m.rgb);
            under.push_back (m.cover);
        }
        const Rect& r = d.region;
        const Rect& i = d.instance;
        if (! (r.x <= i.x && i.x + i.w <= r.x + r.w && r.y <= i.y && i.y + i.h <= r.y + r.h) || i.w < 4 || i.h < 4)
        {
            why = std::string ("page ") + d.name + ": the instance picture is not inside the region or smaller than 4 x 4";
            return false;
        }
        for (const MarkerDef& m : d.markers)
            if (i.x < m.rect.x + m.rect.w + 1 && m.rect.x < i.x + i.w + 1 && i.y < m.rect.y + m.rect.h + 1 && m.rect.y < i.y + i.h + 1)
            {
                why = std::string ("page ") + d.name + ": the instance picture touches a marker";
                return false;
            }
    }
    const InstanceColours& c = set.instanceColours();
    std::vector<uint32_t> inst (c.slot, c.slot + kMaxInstances);
    inst.push_back (c.none);
    inst.push_back (c.blank);
    for (size_t a = 0; a < inst.size(); ++a)
    {
        for (size_t b = a + 1; b < inst.size(); ++b)
            if (colourClose (inst[a], inst[b]))
            {
                why = "two instance colours are within the detector's tolerance of each other";
                return false;
            }
        for (uint32_t m : marker)
            if (colourClose (inst[a], m))
            {
                why = "an instance colour is within the detector's tolerance of a marker colour";
                return false;
            }
        for (uint32_t m : under)
            if (colourClose (inst[a], m))
            {
                why = "an instance colour is within the detector's tolerance of the background or of the colour under a marker";
                return false;
            }
    }
    return true;
}

} // namespace mpckit
