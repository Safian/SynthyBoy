// SynthyBoy - enumerate native FX skin contracts from the live C++ editor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../ui/fx_screen.h"
#include "../../ui/choice_names.h"

#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace fxcontract
{
using namespace synthyboy_mpc;
using Widget = std::tuple<int, int, int>; // layout, row-major cell, kind
using Widgets = std::set<Widget>;

inline std::vector<std::string> options (const synthyboy::ParamInfo& p)
{
    std::vector<std::string> names;
    if (p.choices != nullptr)
        for (int i = 0; i < p.numChoices; ++i)
            names.emplace_back (ui::shownChoice (p.key, i, p.choices[i]));
    else
        for (int i = static_cast<int> (p.min); i <= static_cast<int> (p.max); ++i)
            names.push_back (std::to_string (i));
    return names;
}

inline auto domains (bool& consistent)
{
    std::array<std::vector<std::string>, nativeui::kFxDomains> out;
    for (const auto& d : nativeui::kFxChoiceDomains)
    {
        const auto* p = synthyboy::findParamByKey (d.key);
        if (p == nullptr)
        {
            consistent = false;
            continue;
        }
        const auto names = options (*p);
        if (! out[d.id].empty() && out[d.id] != names)
            consistent = false;
        out[d.id] = names;
    }
    for (int model = 0; model < nativeui::kFxStyleModels; ++model)
    {
        const auto* names = ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
        const int n = synthyboy::filters::filterModelStyleCount (model);
        if (names == nullptr || n < 1)
            consistent = false;
        else
            out[nativeui::kFxStyleBase + model].assign (names, names + n);
    }
    return out;
}

inline auto widgets (bool& consistent)
{
    ui::FxFullScreen screen;
    screen.place ({ 0, 110, 1280, 628 });
    screen.setNativeControls (true);
    auto& ed = screen.slotEditor;
    Widgets out;
    // pass 0: the full page; pass 1: the stacked layout of the pages that have a side layout (the docked Mixer peek shows them stacked)
    for (int kindPass = 0; kindPass < 2 * (ui::fxl::kNumKinds - 1); ++kindPass)
    {
        const int kind = 1 + kindPass % (ui::fxl::kNumKinds - 1);
        const bool stacked = kindPass >= ui::fxl::kNumKinds - 1;
        const auto& table = ui::fxl::layoutOf (kind);
        if (stacked && ! table.side)
            continue;
        ed.setSideAllowed (! stacked);
        std::map<std::string, int> values;
        auto add = [&] (const char* key)
        {
            if (key != nullptr && ! ui::fxl::isView (key))
                values.try_emplace (key, 0);
        };
        for (int row = 0; row < table.numRows; ++row)
            for (const auto& cell : table.row[row].cell)
                for (const auto& alt : cell.alt)
                {
                    add (alt.live.key);
                    add (alt.live.key2);
                }
        if (kind == synthyboy::kFxTypeFilter)
            add ("fxfilter.model");
        ed.setType (kind, true);
        auto sample = [&]
        {
            ed.setState ([&] (const char* key)
            {
                const auto it = values.find (key);
                if (it != values.end())
                    return static_cast<float> (it->second);
                const auto* p = synthyboy::findParamByKey (key);
                return p != nullptr ? p->def : 0.0f;
            });
            const int bands = kind == synthyboy::kFxTypeCompressor ? 3 : 1;
            for (int band = 0; band < bands; ++band)
            {
                ed.setBand (band);
                for (int page = 0; page < ed.numPages(); ++page)
                {
                    ed.setPage (page);
                    const int layout = ed.nativeLayout();
                    if (layout == 0)
                        consistent = false;
                    bool seen[nativeui::kFxDomains] = {};
                    for (int row = 0; row < ed.rowsOnPage(); ++row)
                        for (int col = 0; col < 4; ++col)
                        {
                            const int p = ed.controlInCell (ed.firstRowOfPage() + row, col);
                            const int widget = ed.nativeKindOf (p);
                            if (widget == nativeui::kFxHidden)
                            {
                                if (ed.isLive (p))
                                    consistent = false;
                                continue;
                            }
                            const auto& g = nativeui::kFxLayouts[layout - 1];
                            const auto rect = ed.cellRect (row, col);
                            const int rowH = (620 - g.top) / g.rows;
                            if (g.side != 0)
                            {
                                if (! ed.sideMode() || ed.sideLines (ed.firstRowOfPage() + row) != g.lines[row]
                                    || rect.x != nativeui::kFxSideX + nativeui::kFxSideStride * (col % 2) || rect.w != nativeui::kFxSideCellW
                                    || rect.y != 110 + nativeui::fxSideRowY (g, row) + nativeui::kFxSideLine * (col / 2) || rect.h != nativeui::kFxSideLine
                                    || rect.bottom() > 110 + 620)
                                    consistent = false;
                            }
                            else if (rect.x != 8 + 318 * col || rect.w != 310
                                || rect.y != 110 + g.top + 16 + row * rowH || rect.h != std::max (72, rowH - 16))
                                consistent = false;
                            if (! nativeui::fxCellProxy (widget))
                            {
                                bool& used = seen[widget - nativeui::kFxFirstDomain];
                                if (used)
                                    consistent = false;
                                used = true;
                            }
                            out.emplace (layout, row * 4 + col, widget);
                        }
                }
            }
        };
        auto visit = [&] (auto&& recurse, auto it) -> void
        {
            if (it == values.end())
            {
                sample();
                return;
            }
            const auto* p = synthyboy::findParamByKey (it->first.c_str());
            if (p == nullptr)
            {
                consistent = false;
                return;
            }
            auto next = it;
            ++next;
            for (int value = static_cast<int> (p->min); value <= static_cast<int> (p->max); ++value)
            {
                it->second = value;
                recurse (recurse, next);
            }
        };
        visit (visit, values.begin());
    }
    ed.setSideAllowed (true);
    return out;
}

inline void quote (const std::string& value)
{
    std::putchar ('"');
    for (char c : value)
    {
        if (c == '"' || c == '\\')
            std::putchar ('\\');
        std::putchar (c);
    }
    std::putchar ('"');
}

using HeaderRect = std::array<int, 4>;
using HeaderRects = std::array<HeaderRect, 4>;

inline auto headerRects (bool& consistent)
{
    ui::FxFullScreen screen;
    const ui::Rect tab { 0, 110, 1280, 628 };
    screen.place (tab);
    screen.setNativeControls (true);
    auto& ed = screen.slotEditor;
    const auto relative = [&] (ui::Rect r) -> HeaderRect { return { r.x - tab.x, r.y - tab.y, r.w, r.h }; };
    std::array<HeaderRects, 2> headers {};
    std::array<bool, 2> seen {};
    for (int kind = 1; kind < ui::fxl::kNumKinds; ++kind)
    {
        ed.setType (kind, true);
        ed.setState ([] (const char* key) {
            const auto* p = synthyboy::findParamByKey (key);
            return p != nullptr ? p->def : 0.0f;
        });
        const bool more = ed.numPages() > 1;
        const HeaderRects actual { relative (ed.power.rect()), relative (ed.rack.rect()),
                                   relative (ed.remove.rect()), more ? relative (ed.tabs.rect()) : HeaderRect {} };
        if (seen[more] && headers[more] != actual)
            consistent = false;
        headers[more] = actual;
        seen[more] = true;
    }
    consistent = consistent && seen[0] && seen[1];
    return headers;
}

inline bool exportManifest ()
{
    bool consistent = true;
    const auto choices = domains (consistent);
    const auto controls = widgets (consistent);
    const auto headers = headerRects (consistent);
    if (! consistent)
        return false;
    std::printf ("{\n  \"kind_last\": %d,\n  \"layouts\": [", nativeui::kFxKindLast);
    for (int i = 0; i < nativeui::kFxLayoutLast; ++i)
    {
        const auto& g = nativeui::kFxLayouts[i];
        std::printf ("%s[%d, %d", i == 0 ? "" : ", ", g.top, g.rows);
        if (g.side != 0)   // a side layout adds its flag and the lines of its rows
            std::printf (", %d, %d, %d, %d, %d", g.side, g.lines[0], g.lines[1], g.lines[2], g.lines[3]);
        std::printf ("]");
    }
    std::printf ("],\n  \"domains\": [\n");
    for (size_t i = 0; i < choices.size(); ++i)
    {
        std::printf ("    [");
        for (size_t j = 0; j < choices[i].size(); ++j)
        {
            if (j > 0)
                std::printf (", ");
            quote (choices[i][j]);
        }
        std::printf ("]%s\n", i + 1 == choices.size() ? "" : ",");
    }
    std::printf ("  ],\n  \"widgets\": [");
    bool first = true;
    for (auto [layout, slot, kind] : controls)
    {
        std::printf ("%s\n    [%d, %d, %d]", first ? "" : ",", layout, slot, kind);
        first = false;
    }
    std::printf ("\n  ],\n  \"editor_headers\": [");
    const char* names[] = { "power", "rack", "remove", "tabs" };
    for (size_t layout = 0; layout < headers.size(); ++layout)
    {
        std::printf ("%s\n    { ", layout ? "," : "");
        for (size_t i = 0; i < 3 + layout; ++i)
        {
            const auto& r = headers[layout][i];
            std::printf ("%s\"%s\": [%d, %d, %d, %d]", i ? ", " : "", names[i], r[0], r[1], r[2], r[3]);
        }
        std::printf (" }");
    }
    std::printf ("\n  ],\n  \"panels\": [");
    for (int kind = 1; kind < ui::fxl::kNumKinds; ++kind)
    {
        const auto panels = ui::fxl::panelsOf (kind);
        std::printf ("%s\n    {\"kind\": %d, \"names\": [", kind == 1 ? "" : ",", kind);
        for (int panel = 0; panel < panels.count; ++panel)
        {
            if (panel) std::printf (", ");
            quote (panels.names[panel]);
        }
        ui::FxFullScreen headerScreen;
        headerScreen.place ({ 0, 0, 1280, 628 });
        auto& header = headerScreen.slotEditor;
        header.setType (kind, true);
        const ui::Rect texts[] { header.pathRect(), header.nameRect(), header.noticeRect() };
        std::printf ("], \"text\": [");
        for (int i = 0; i < 3; ++i)
            std::printf ("%s[%d, %d, %d, %d]", i ? ", " : "", texts[i].x, texts[i].y,
                         texts[i].w, texts[i].h);
        const auto& look = ui::theme::L();
        std::printf ("], \"text_font\": [%g, %g, %g]}", look.fLabel - 2.0f, look.fTitle, look.fLabel + 2.0f);
    }
    std::printf ("\n  ],\n  \"chain\": {\n");
    ui::FxFullScreen screen;
    const ui::Rect tab { 0, 110, 1280, 628 };
    screen.place (tab);
    screen.setNativeControls (true);
    const auto rect = [&] (ui::Rect r) { std::printf ("[%d, %d, %d, %d]", r.x - tab.x, r.y - tab.y, r.w, r.h); };
    auto& ed = screen.slotEditor;
    ed.setType (synthyboy::kFxTypeCompressor, true);
    ed.setModuleEnabled (true);
    // the BAND selector of the full page (the compressor's side layout) and of the stacked layout the docked Mixer peek shows (the skin moves that one up by the peek's offset)
    ui::Rect bands[2];
    for (int stacked = 0; stacked < 2; ++stacked)
    {
        ed.setSideAllowed (stacked == 0);
        for (int bandMode = 0; bandMode < 3; ++bandMode)
            for (int dynamics = 0; dynamics < 3; ++dynamics)
            {
                ed.setState ([&] (const char* key) {
                    if (std::strcmp (key, "compressor.bands") == 0) return static_cast<float> (bandMode);
                    if (std::strcmp (key, "compressor.limit") == 0) return static_cast<float> (dynamics);
                    const auto* p = synthyboy::findParamByKey (key);
                    return p != nullptr ? p->def : 0.0f;
                });
                const auto actual = ed.nativeBandRect();
                consistent &= ! actual.empty() && (bands[stacked].empty() || bands[stacked] == actual);
                bands[stacked] = actual;
            }
    }
    ed.setSideAllowed (true);
    std::printf ("    \"band\": ");
    rect (bands[0]);
    std::printf (",\n    \"band_stacked\": ");
    rect (bands[1]);
    std::printf (",\n");
    std::printf ("    \"add\": ");
    rect (screen.chainBar.nativeAddRect());
    std::printf (",\n    \"back\": ");
    rect (screen.chainBar.nativeBackRect());
    std::printf (",\n    \"drag\": ");
    rect (screen.chainBar.dragRect());
    std::printf (",\n    \"page\": ");
    rect (screen.chainBar.nativePageRect());
    std::printf (",\n    \"cards\": [");
    for (int i = 0; i < ui::FxChainBar::kMaxSlots; ++i)
    {
        std::printf ("%s\n      [", i ? "," : "");
        rect (screen.chainBar.nativePowerRect (i));
        std::printf (", ");
        rect (screen.chainBar.nativeSelectRect (i));
        std::printf ("]");
    }
    int kinds[ui::FxGallery::kMaxTypes];
    const char* typeNames[ui::FxGallery::kMaxTypes];
    for (int i = 0; i < ui::FxGallery::kMaxTypes; ++i)
    {
        kinds[i] = i + 1;
        typeNames[i] = ui::fxl::layoutOf (kinds[i]).name;
    }
    screen.gallery.setTypes (kinds, typeNames, ui::FxGallery::kMaxTypes);
    std::printf ("\n    ],\n    \"gallery\": [");
    for (int i = 0; i < ui::FxGallery::kMaxTypes; ++i)
    {
        std::printf ("%s\n      [", i ? "," : "");
        quote (typeNames[i]);
        std::printf (", ");
        rect (screen.gallery.cardRect (i));
        std::printf ("]");
    }
    std::printf ("\n    ]\n  }\n}\n");
    return true;
}
} // namespace fxcontract
