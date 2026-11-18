// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "core.h"
#include "theme.h"

namespace synthyboy_mpc { namespace ui {

struct LoadBarData {
    uint32_t percent; // 0..100
    bool active;
    uint32_t label_id; // id into owner-supplied table
};

// Stateless component for load progress.
// The owner hands in the data in the `state` member, then calls `draw`.
class LoadBar : public Component {
public:
    LoadBarData state;

    void draw(gpu::SceneBuilder& sb) const override {
        if (!state.active) return;

        const theme::Look& T = theme::L();
        Pen p(sb, enabled_, inactive_);
        
        // Amber color from page_view.h
        uint32_t amber = 0xFFB000;
        
        if (state.percent == 0) {
            // Compact form: solid amber bar
            p.rrect(rect_, 2.0f, theme::rgba(amber));
        } else {
            // Progress form: filled part + dim rest
            float filled_w = static_cast<float>(rect_.w) * (std::min(100u, state.percent) / 100.0f);
            
            p.rrect(rect_, 2.0f, theme::rgba(T.track));
            p.rrect({rect_.x, rect_.y, static_cast<int>(filled_w), rect_.h}, 2.0f, theme::rgba(amber));
            
            // Text: "LOADING n %"
            char text[32];
            std::snprintf(text, sizeof(text), "LOADING %u %%", state.percent);
            p.textCentered(rect_, text, T.fValue, p.c(T.text, 255), T.fMin);
        }
    }
};

}} // namespace synthyboy_mpc::ui
