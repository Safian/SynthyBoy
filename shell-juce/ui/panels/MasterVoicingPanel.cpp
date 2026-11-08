// SynthyBoy - UI panel: Master / Voicing (docs/ui-design/07-voice-midi-global.md, page `master_voicing`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// THE TEMPLATE for panel authors (shell-juce/ui/README.md): one class, three overrides, one registration line at the end.
#include "../Panel.h"

namespace synthyboy::ui
{
class MasterVoicingPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Master / Voicing"; }

    /** Every key of the panel, primary controls first. Keys the engine does not implement yet (master.scale.*, master.tuning.*,
        master.mpe.*) are listed too: the kit dims them by itself (EngineCaps). */
    std::vector<std::string> keys() const override
    {
        return { "master.volume", "master.polyphony", "master.transpose", "master.tune",
                 "master.legato", "master.bend_range", "master.velocity_track",
                 "master.portamento.time", "master.portamento.slope", "master.portamento.always", "master.portamento.scaled",
                 "master.scale.key", "master.scale.type", "master.scale.mode",
                 "master.tuning.a4", "master.tuning.scale", "master.tuning.mts_esp", "master.tuning.mts_note_on_only",
                 "master.mpe.on", "master.mpe.bend_range", "voice.phase_legato",
                 "master.stereo_routing", "master.voice_override" };
    }

    void build() override
    {
        // at most four primary controls (brief, principle 1); everything else behind "More"
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
            if (i < 4)
                addControl (all[i]);
            else
                addMoreControl (all[i]);
    }
};

SYNTHYBOY_REGISTER_PANEL (MasterVoicingPanel, Page::glb, 10)
} // namespace synthyboy::ui
