// SynthyBoy - UI panel: Voice / Stereo (docs/ui-design/13-new-keys.md 4.19, B15: G12 same note and voice steal, G13 poly legato,
// G14 separate bend down range, G15 stereo spread).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Plain kit panel: the four things a player reaches for are primary (same note, steal priority, poly legato, stereo spread), the pitch
// wheel's down range and its link to the up range sit behind More. Every control is a kit control, so the choices are touch pickers,
// the toggle is 44 pt, and the modulatable key (stereo spread) takes a purple ring and assign-by-touch like any knob.
#include "../Panel.h"

namespace synthyboy::ui
{
class GlbVoicePanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Voice / Stereo"; }

    std::vector<std::string> keys() const override
    {
        return { "master.voice.same_note", "master.voice.steal_priority", "master.poly_legato", "master.stereo_spread",
                 "master.bend_down_range", "master.bend_down_link" };
    }

    void build() override
    {
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
            if (i < 4)
                addControl (all[i]);
            else
                addMoreControl (all[i]);
    }
};

SYNTHYBOY_REGISTER_PANEL (GlbVoicePanel, Page::glb, 15)
} // namespace synthyboy::ui
