// SynthyBoy - UI: EnvelopePanel - 6 envelopes with node dragging (docs/ui-design/05-envelopes-lfos.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "../Panel.h"
#include "EnvCurveView.h"

namespace synthyboy::ui
{

/** Envelope panel base class for Envelopes 1..6.
    Combines the tab chips [E1*]..[E6] with destination badges, zoom lock,
    interactive EnvCurveView visual, 4 primary knobs (Attack, Decay, Sustain, Release),
    and More row for delay, hold, curves, units, and beats parameters. */
class EnvelopePanel : public Panel,
                      private juce::ChangeListener
{
public:
    EnvelopePanel (UiContext& context, int envIndex);
    ~EnvelopePanel() override;

    int envelopeIndex() const noexcept { return envIdx; }

    juce::String title() const override;
    std::string modulePrefix() const override { return "env" + std::to_string (envIdx); }
    std::vector<std::string> keys() const override;
    void build() override;

    int preferredHeight (int width) const override;
    void resized() override;

    EnvCurveView& curveView() noexcept { return *visual; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateTabBadges();
    void scrollToEnvelope (int targetEnv);
    int tabsPerRow (int width) const;
    int tabRowHeight (int width) const;

    int envIdx = 1;
    std::unique_ptr<EnvCurveView> visual;

    // Header tabs row
    juce::Component tabRow;
    std::array<std::unique_ptr<juce::TextButton>, 6> tabButtons;
    juce::TextButton zoomLockButton { "Lock" };
};

} // namespace synthyboy::ui
