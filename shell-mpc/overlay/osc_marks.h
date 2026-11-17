// SynthyBoy - MPC overlay: what the pages of the oscillator types draw ON the display of the oscillator, from the parameters (the display itself is osc_display.h): the start and the end of a sample region
// (the parts outside are dimmed, a marker at each end), the remap curve of the wavetable's frame position on the map page. Thin crisp lines in the library palette, no glow.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../ui/ui.h"
#include "curve_source.h"
#include "osc_spec_page.h"
#include "play_osc.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace overlay {

struct OscMarks
{
    bool region = false;                    // the sample type: start / end of the region (0 .. 1 over the file)
    float start = 0.0f, end = 1.0f;
    bool remap = false;                     // the map page of a wavetable with the remap on: the curve of the chosen slot (x, y 0 .. 1)
    std::vector<std::pair<float, float>> curve;
    float rise = 0.0f, fall = 0.0f;         // ms, shown as text beside the curve

    /** Reads what the page of the oscillator `sel` (2 .. 4 = A .. C) needs. `page`: the detail page on top (0: the full screen). */
    static OscMarks of (const IInstanceView& v, int sel, int page)
    {
        OscMarks m;
        const int osc = playosc::oscNumber (sel);
        if (osc < 1)
            return m;
        const std::string p = "osc" + std::to_string (osc) + ".";
        auto val = [&] (const char* k, float def) { const synthyboy::ParamInfo* pi = synthyboy::findParamByKey ((p + k).c_str()); return pi != nullptr ? v.param (static_cast<int> (pi - synthyboy::paramTable())) : def; };
        const int type = static_cast<int> (std::lround (val ("type", 0.0f)));
        if (type == 1 && (page == 0 || page == oscspec::kSampleRegion || page == oscspec::kSampleSlice))
        {
            m.region = true;
            m.start = std::clamp (val ("sample.start", 0.0f) / 100.0f, 0.0f, 1.0f);
            m.end = std::clamp (val ("sample.end", 100.0f) / 100.0f, 0.0f, 1.0f);
        }
        if (type == 0 && page == oscspec::kWtMap && val ("remap.on", 0.0f) >= 0.5f)
        {
            m.remap = true;
            m.rise = val ("remap.rise", 0.0f);
            m.fall = val ("remap.fall", 0.0f);
            const int slot = static_cast<int> (std::lround (val ("remap.slot", 0.0f))) + 1;
            const std::shared_ptr<const synthyboy::StateContent> c = v.content();
            if (c != nullptr)
                for (const synthyboy::ContentRemap& r : c->remaps)
                    if (r.osc == osc && r.slot == slot)
                        for (const synthyboy::ContentPoint& pt : r.points)
                            m.curve.push_back ({ pt.x, pt.y });
            if (m.curve.size() < 2)
                m.curve = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };   // a slot without a drawn curve is the identity
        }
        return m;
    }
    bool any() const noexcept { return region || remap; }

    /** Over the display rectangle `r` (the plot is the rectangle the display draws in: inset 8). */
    void draw (gpu::SceneBuilder& sb, const ui::Rect& r) const
    {
        const ui::theme::Look& T = ui::theme::L();
        const ui::Rect in = r.inset (8);
        const float x0 = static_cast<float> (in.x), x1 = static_cast<float> (in.right()), top = static_cast<float> (in.y) + 6.0f, bottom = static_cast<float> (in.bottom()) - 6.0f;
        auto a = [] (uint32_t rgb, int al) { return (rgb << 8) | static_cast<uint32_t> (al); };
        sb.style (1.0f, false);
        sb.paintOff();
        if (region)
        {
            const float xs = x0 + start * (x1 - x0 - 1.0f), xe = x0 + end * (x1 - x0 - 1.0f);
            if (xs > x0)
                sb.rect (x0, top, xs, bottom, a (T.panel0, 170));
            if (xe < x1)
                sb.rect (xe, top, x1, bottom, a (T.panel0, 170));
            for (float x : { xs, xe })
                sb.line (x, top, x, bottom, 1.5f, a (T.text, 255));
            sb.text (xs + 6.0f, top + 22.0f, T.fLabel - 2.0f, "START", a (T.textDim, 255));
            sb.text (std::max (xs + 70.0f, xe - 52.0f), top + 22.0f, T.fLabel - 2.0f, "END", a (T.textDim, 255));
        }
        if (remap)
        {
            const float w = std::min (260.0f, (x1 - x0) * 0.3f), h = std::min (150.0f, (bottom - top) * 0.7f);
            const float px = x1 - w - 14.0f, py = bottom - h - 12.0f;
            sb.rect (px - 6.0f, py - 6.0f, px + w + 6.0f, py + h + 6.0f, a (T.panel0, 215));
            sb.line (px, py + h, px + w, py + h, T.hair, a (T.grid, 255));
            sb.line (px, py, px, py + h, T.hair, a (T.grid, 255));
            sb.line (px, py + h, px + w, py, T.hair, a (T.grid, 255));   // the identity, for reference
            for (size_t i = 0; i + 1 < curve.size(); ++i)
                sb.line (px + curve[i].first * w, py + h - curve[i].second * h, px + curve[i + 1].first * w, py + h - curve[i + 1].second * h, T.curve, a (T.q, 255));
            sb.text (px, py - 12.0f, T.fLabel - 2.0f, "REMAP", a (T.textDim, 255));
        }
    }
};

}} // namespace synthyboy_mpc::overlay
