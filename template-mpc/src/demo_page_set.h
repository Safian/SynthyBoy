// MpcKit Demo - the tabs of the plugin: names, kinds and the marker colours the page detector looks for. The SAME numbers are in skin/overlay_layout.json (the skin is generated from it);
// scripts/gen-overlay-layout-header.py (after migration step 9, TEMPLATE.md) writes this table from that file, so do not edit the colours here by hand: edit the JSON.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TO ADD A PAGE: a new page class in src/pages/, a new entry in skin/overlay_layout.json (a name, a tab title, a pair of marker colours no other tab uses), a new line in skin/pages.json (the tab's
// host widgets), a new PageDef here and a new `case` in the factory of demo_plugin.cpp. The tab title must be at most 8 characters (MPC function key limit).
#pragma once

#include "../../shell-mpc/kit/page_set.h"

namespace demo {

constexpr int kKindMain = 16, kKindShape = 17, kKindList = 18;   // the plugin's own page kinds (>= mpckit::kFirstPageKind)

class DemoPageSet final : public mpckit::IPageSet
{
public:
    int numPages() const noexcept override { return 3; }
    const mpckit::PageDef& page (int index) const noexcept override { return pages_[index < 0 ? 0 : (index > 2 ? 2 : index)]; }
    const mpckit::InstanceColours& instanceColours() const noexcept override { return colours_; }

private:
    // name, tab, kind, index, region (screen coordinates), opaque, markers { rect, rgb, cover } x 2, instance picture. The tab starts at screen y 110: tab y 24 is screen y 134.
    static constexpr mpckit::PageDef make (const char* name, const char* tab, int kind, unsigned left, unsigned right)
    {
        return { name, tab, kind, 1, { 0, 110, 1280, 628 }, true, { { { 33, 134, 6, 6 }, left, 0x1e1d1e }, { { 1241, 134, 6, 6 }, right, 0x1e1d1e } }, { 45, 134, 6, 6 } };
    }
    mpckit::PageDef pages_[3] = {
        make ("main", "Main", kKindMain, 0xe0a020, 0x20e0a0),
        make ("shape", "Shape", kKindShape, 0xe0b020, 0x20e0b0),
        make ("list", "List", kKindList, 0xe0c020, 0x20e0c0),
    };
    mpckit::InstanceColours colours_;
};

inline const DemoPageSet& pageSet()
{
    static const DemoPageSet set;
    return set;
}

} // namespace demo
