// SynthyBoy - MPC overlay: the page detector. "A view of OUR skin is on screen and not covered" = both marker pictures of that view (two small solid squares of colours
// that no other view uses, inside its region, shell-mpc/overlay/overlay_layout.json) are in the application's primary framebuffer. The overlay lives on another plane, so the markers
// stay readable while it covers them. A popup, a menu or another tab covers or replaces them, and the overlay goes.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdlib>

#include "drm_interfaces.h"
#include "instance_slots.h"
#include "overlay_layout.h"

namespace synthyboy_mpc { namespace overlay {

struct MarkerSpec
{
    Rect rect;      // landscape UI pixels
    uint32_t rgb;   // 0xRRGGBB
};

struct Detection
{
    static constexpr int kMaxViews = 24;
    bool readable = false;           // the framebuffer could be read
    int view = -1;                   // the first view whose two markers are both on screen, -1 when none
    int slot = kSlotUnknown;         // with a view: the instance the page belongs to (the colour of the view's instance picture): 0 .. 15, kSlotNone, or kSlotUnknown (blank or not a colour of the table)
    uint32_t instanceSeen = 0xffffffffu;   // the pixel at the centre of that picture (0xffffffff when it could not be read), for the status file
    uint32_t seen[kMaxViews][2] = {};   // the pixel at the centre of every marker (0xRRGGBB; 0xffffffff when it could not be read), for the status file
    bool qlinkPanel = false;         // the host's Q-Link side panel is drawn over the right edge (while a hardware knob is turned); it covers the right marker of a page, the view is still on screen (our layer is transparent in the strip meanwhile)
    bool partialLook = false;        // with partialView: the host's panel look (the black line and the fill, see qlinkPanelOnScreen) is on screen: the panel is fully in
    int partialView = -1;            // with no view on screen: the first view whose left marker (and its instance picture) is on screen while the right one is NOT, and the panel is not recognised either: the picture of the host's Q-Link panel sliding in or out (it covers the right marker first and last), or something else on top of the right marker
    int count = 0;                   // views checked
};

/** The result of looking for our own markers under every rotation. */
struct RotationSearch
{
    bool readable = false;     // the framebuffer could be read
    int rotation = -1;         // the one rotation under which a view's marker pair is on screen; -1 when none or ambiguous
    int matches = 0;           // how many different rotations matched (more than one: the pictures cannot be told apart, nothing is decided)
    Detection detection;       // the detection under the rotation found (a view, its instance), or an empty one
};

/** Looks for the marker pairs of all views (two markers each) in the primary framebuffer. The colours of the pairs differ from one another by more than the tolerance, so at most one
    view matches: the one whose tab is on screen. For that view it also reads the instance picture (a film strip frame of one solid colour, bound to the display parameter "Overlay
    Instance": the colour says which plugin instance the page on screen belongs to, instance_slots.h). */
class PageDetector
{
public:
    static constexpr int kTolerance = 2;   // per channel
    /** The host draws a Q-Link panel over the right edge of the screen while a knob is turned (docs/ui-reference.md 1.4: "roughly x >= 1025"): a 2 pixel black line at x 1024 and 1025 over the whole
        height, and to the right of it the dark fill 1e1d1e. Measured on the Key 61 (.hw-local/mpc-perf/overlay-pkgs/ov14-osc-ui-results, tests/fixtures/qlink_panel_fixtures.bin). */
    static constexpr int kPanelLineX = 1024, kPanelFillRgb = 0x1e1d1e;
    static constexpr int kPanelRows[6] = { 20, 120, 250, 400, 560, 700 };   // above the tab bar (it is black at the bottom of the screen)

    /** `instances`: one rectangle per view (landscape UI pixels), null: the views have no instance picture (the slot then stays kSlotUnknown). */
    PageDetector (IFrameSource& source, const MarkerSpec* markers, int numViews, const Rect* instances = nullptr)
        : source_ (source), markers_ (markers), instances_ (instances), count_ (numViews > Detection::kMaxViews ? Detection::kMaxViews : numViews) {}

    /** Reads the sample pixels of every marker (the four corners of an inset square and the centre) through the shared UI -> panel mapping. */
    Detection check (int rotation)
    {
        Detection d;
        d.count = count_;
        int w = 0, h = 0;
        for (int i = 0; i < count_; ++i)
            d.seen[i][0] = d.seen[i][1] = 0xffffffffu;
        if (count_ <= 0 || ! source_.begin (w, h))
            return d;
        d.readable = true;
        for (int i = 0; i < count_; ++i)
        {
            bool all = true, rightOnlyMissing = true;
            for (int m = 0; m < 2; ++m)
            {
                const MarkerSpec& spec = markers_[i * 2 + m];
                bool markerOk = true;
                const Rect& r = spec.rect;
                const int ix = r.w > 3 ? 1 : 0, iy = r.h > 3 ? 1 : 0;
                const int xs[5] = { r.x + ix, r.x + r.w - 1 - ix, r.x + ix, r.x + r.w - 1 - ix, r.x + r.w / 2 };
                const int ys[5] = { r.y + iy, r.y + iy, r.y + r.h - 1 - iy, r.y + r.h - 1 - iy, r.y + r.h / 2 };
                for (int k = 0; k < 5; ++k)
                {
                    int px, py;
                    uint32_t v = 0;
                    uiPointToPanel (rotation, xs[k], ys[k], w, h, px, py);
                    const bool ok = source_.pixel (px, py, v);
                    if (k == 4)
                        d.seen[i][m] = ok ? v : 0xffffffffu;
                    if (! ok || ! close (v, spec.rgb))
                        markerOk = false;
                }
                if (! markerOk)
                {
                    all = false;
                    if (m == 0)
                        rightOnlyMissing = false;
                }
            }
            // the right marker lies under the host's Q-Link panel and the view's instance picture does not (the pages of the Play tab): the left marker and the panel's own look say the page is still there
            if (! all && rightOnlyMissing && instances_ && markers_[i * 2 + 1].rect.x >= kPanelLineX + 2 && instances_[i].x + instances_[i].w < kPanelLineX && qlinkPanelOnScreen (rotation, w, h))
            {
                all = true;
                if (d.view < 0)
                    d.qlinkPanel = true;
            }
            if (! all && rightOnlyMissing && d.partialView < 0)
            {
                d.partialView = i;
                d.partialLook = qlinkPanelOnScreen (rotation, w, h);
            }
            if (all && d.view < 0)
            {
                d.view = i;
                if (instances_)
                    readInstance (instances_[i], rotation, w, h, d);
            }
        }
        source_.end();
        return d;
    }

    /** The rotation of the UI on the panel, without a table: the marker pair of the view on screen is looked for under 90, 270, 0 and 180 degrees. The solid squares of OUR skin (colours no
        other view uses) are at the places one rotation says and nowhere else: exactly one rotation may match, else nothing is decided (a second look follows at the next check). */
    RotationSearch searchRotation()
    {
        RotationSearch r;
        static const int kRotations[4] = { 90, 270, 0, 180 };
        for (int rot : kRotations)
        {
            const Detection d = check (rot);
            r.readable = r.readable || d.readable;
            if (d.view >= 0)
            {
                ++r.matches;
                r.rotation = rot;
                r.detection = d;
            }
        }
        if (r.matches != 1)
        {
            r.rotation = -1;
            r.detection = Detection();
            r.detection.readable = r.readable;
        }
        return r;
    }

private:
    /** The panel's signature in the framebuffer: on every sample row the black line (x 1024 and 1025), not black just left of it, the fill colour at three columns right of it. */
    bool qlinkPanelOnScreen (int rotation, int w, int h)
    {
        for (int y : kPanelRows)
        {
            const int xs[6] = { kPanelLineX - 2, kPanelLineX, kPanelLineX + 1, kPanelLineX + 6, kPanelLineX + 60, 1270 };
            for (int k = 0; k < 6; ++k)
            {
                int px, py;
                uint32_t v = 0;
                uiPointToPanel (rotation, xs[k], y, w, h, px, py);
                if (! source_.pixel (px, py, v))
                    return false;
                const bool wantBlack = k == 1 || k == 2, wantFill = k >= 3;
                if (wantBlack && ! close (v, 0x000000u))
                    return false;
                if (wantFill && ! close (v, static_cast<uint32_t> (kPanelFillRgb)))
                    return false;
                if (k == 0 && close (v, 0x000000u))
                    return false;
            }
        }
        return true;
    }

    /** The five sample pixels of the instance picture (the same pattern as a marker) must all be the same colour of the table. */
    void readInstance (const Rect& r, int rotation, int w, int h, Detection& d)
    {
        const int ix = r.w > 3 ? 1 : 0, iy = r.h > 3 ? 1 : 0;
        const int xs[5] = { r.x + ix, r.x + r.w - 1 - ix, r.x + ix, r.x + r.w - 1 - ix, r.x + r.w / 2 };
        const int ys[5] = { r.y + iy, r.y + iy, r.y + r.h - 1 - iy, r.y + r.h - 1 - iy, r.y + r.h / 2 };
        int slot = kSlotUnknown;
        for (int k = 0; k < 5; ++k)
        {
            int px, py;
            uint32_t v = 0;
            uiPointToPanel (rotation, xs[k], ys[k], w, h, px, py);
            const bool ok = source_.pixel (px, py, v);
            if (k == 4)
                d.instanceSeen = ok ? v : 0xffffffffu;
            const int s = ok ? classifyInstanceRgb (v, kTolerance) : kSlotUnknown;
            if (k == 0)
                slot = s;
            else if (s != slot)
                slot = kSlotUnknown;
        }
        d.slot = slot;
    }

    static bool close (uint32_t a, uint32_t b) noexcept
    {
        for (int shift = 0; shift <= 16; shift += 8)
            if (std::abs (static_cast<int> ((a >> shift) & 255u) - static_cast<int> ((b >> shift) & 255u)) > kTolerance)
                return false;
        return true;
    }

    IFrameSource& source_;
    const MarkerSpec* markers_;
    const Rect* instances_;
    int count_;
};

}} // namespace synthyboy_mpc::overlay
