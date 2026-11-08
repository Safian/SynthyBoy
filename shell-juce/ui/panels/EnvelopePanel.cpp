// SynthyBoy - UI: EnvelopePanel - 6 envelopes with node dragging (docs/ui-design/05-envelopes-lfos.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EnvelopePanel.h"

namespace synthyboy::ui
{

EnvelopePanel::EnvelopePanel (UiContext& context, int envIndex)
    : Panel (context),
      envIdx (juce::jlimit (1, 6, envIndex))
{
    ctx.assign.addChangeListener (this);
}

EnvelopePanel::~EnvelopePanel()
{
    ctx.assign.removeChangeListener (this);
}

juce::String EnvelopePanel::title() const
{
    if (envIdx == 1)
        return "Envelope 1 (Amp)";
    return "Envelope " + juce::String (envIdx);
}

std::vector<std::string> EnvelopePanel::keys() const
{
    const std::string prefix = "env" + std::to_string (envIdx) + ".";
    return {
        // 4 primary controls
        prefix + "attack",
        prefix + "decay",
        prefix + "sustain",
        prefix + "release",
        // 12 more controls
        prefix + "delay",
        prefix + "hold",
        prefix + "attack_curve",
        prefix + "decay_curve",
        prefix + "release_curve",
        prefix + "units",
        prefix + "legato_inverted",
        prefix + "delay_beats",
        prefix + "attack_beats",
        prefix + "hold_beats",
        prefix + "decay_beats",
        prefix + "release_beats"
    };
}

void EnvelopePanel::build()
{
    const auto all = keys();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (i < 4)
            addControl (all[i]);
        else
            addMoreControl (all[i]);
    }

    visual = std::make_unique<EnvCurveView> (ctx, envIdx);
    addAndMakeVisible (*visual);

    // Tab buttons E1..E6
    for (int i = 0; i < 6; ++i)
    {
        const int idx = i + 1;
        auto btn = std::make_unique<juce::TextButton>();
        btn->setClickingTogglesState (false);
        btn->setToggleState (idx == envIdx, juce::dontSendNotification);
        btn->onClick = [this, idx] { scrollToEnvelope (idx); };
        tabRow.addAndMakeVisible (*btn);
        tabButtons[static_cast<size_t> (i)] = std::move (btn);
    }

    zoomLockButton.setClickingTogglesState (true);
    zoomLockButton.setToggleState (visual->isZoomLocked(), juce::dontSendNotification);
    zoomLockButton.onClick = [this]
    {
        visual->setZoomLocked (zoomLockButton.getToggleState());
    };
    tabRow.addAndMakeVisible (zoomLockButton);

    addAndMakeVisible (tabRow);
    updateTabBadges();
}

void EnvelopePanel::updateTabBadges()
{
    const auto& snap = ctx.assign.snapshot();
    for (int i = 0; i < 6; ++i)
    {
        const int idx = i + 1;
        // In the table, mod sources for env1..env6 are choices 1..6
        const int destCount = snap.destinationCount (idx);

        juce::String name = (idx == 1 ? "E1*" : ("E" + juce::String (idx)));
        if (destCount > 0)
            name += " " + juce::String (destCount);

        tabButtons[static_cast<size_t> (i)]->setButtonText (name);
        tabButtons[static_cast<size_t> (i)]->setToggleState (idx == envIdx, juce::dontSendNotification);
    }
}

void EnvelopePanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &ctx.assign)
        updateTabBadges();
}

void EnvelopePanel::scrollToEnvelope (int targetEnv)
{
    if (targetEnv == envIdx)
        return;

    // Find parent viewport to scroll to the corresponding envelope panel
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        if (auto* viewed = vp->getViewedComponent())
        {
            for (auto* child : viewed->getChildren())
            {
                if (auto* p = dynamic_cast<EnvelopePanel*> (child))
                {
                    if (p->envelopeIndex() == targetEnv)
                    {
                        vp->setViewPosition (0, p->getY() - ctx.theme.gap());
                        break;
                    }
                }
            }
        }
    }
}

int EnvelopePanel::tabsPerRow (int width) const
{
    const auto& t = ctx.theme;
    return juce::jlimit (1, 7, (width + t.gap()) / (t.touch() + t.gap()));
}

int EnvelopePanel::tabRowHeight (int width) const
{
    const auto& t = ctx.theme;
    const int rows = (7 + tabsPerRow (width) - 1) / tabsPerRow (width);
    return rows * t.touch() + (rows - 1) * t.gap();
}

int EnvelopePanel::preferredHeight (int width) const
{
    const auto& t = ctx.theme;
    const int gap = t.gap();
    const int tabH = tabRowHeight (width - 2 * gap);
    const int visualH = t.ipx (160.0f);
    const juce::Rectangle<int> controlsArea (0, 0, width - 2 * gap, 100000);
    const int controlsH = const_cast<EnvelopePanel*> (this)->layoutControls (controlsArea, true);

    return headerHeight() + gap + tabH + gap + visualH + gap + controlsH + gap;
}

void EnvelopePanel::resized()
{
    const auto& t = ctx.theme;
    const int gap = t.gap();
    const auto fullArea = contentArea();

    const int tabH = tabRowHeight (fullArea.getWidth());
    tabRow.setBounds (fullArea.getX(), fullArea.getY(), fullArea.getWidth(), tabH);

    // E1..E6 and Lock are 7 cells of at least a touch target; they wrap into as many rows as the width needs
    juce::Component* cells[7];
    for (size_t i = 0; i < 6; ++i)
        cells[i] = tabButtons[i].get();
    cells[6] = &zoomLockButton;
    const int perRow = tabsPerRow (fullArea.getWidth());
    const int rows = (7 + perRow - 1) / perRow;
    const int rowLength = (7 + rows - 1) / rows;
    for (int row = 0; row < rows; ++row)
    {
        const int inRow = juce::jmin (rowLength, 7 - row * rowLength);
        const int cellW = juce::jmax (1, (fullArea.getWidth() - (inRow - 1) * gap) / inRow);
        for (int i = 0; i < inRow; ++i)
            cells[row * rowLength + i]->setBounds (i * (cellW + gap), row * (t.touch() + gap), cellW, t.touch());
    }

    const int visualH = t.ipx (160.0f);
    if (visual != nullptr)
        visual->setBounds (fullArea.getX(), tabRow.getBottom() + gap, fullArea.getWidth(), visualH);

    const int topForControls = (visual != nullptr ? visual->getBottom() : tabRow.getBottom()) + gap;
    const juce::Rectangle<int> controlsArea (fullArea.getX(), topForControls, fullArea.getWidth(),
                                             juce::jmax (0, fullArea.getBottom() - topForControls));
    layoutControls (controlsArea, false);
}

// ---- Panel Registrations (Page::env, orders 10, 20, 30, 40, 50, 60) --------------------------------

class Envelope1Panel final : public EnvelopePanel
{
public:
    explicit Envelope1Panel (UiContext& c) : EnvelopePanel (c, 1) {}
};
SYNTHYBOY_REGISTER_PANEL (Envelope1Panel, Page::env, 10)

class Envelope2Panel final : public EnvelopePanel
{
public:
    explicit Envelope2Panel (UiContext& c) : EnvelopePanel (c, 2) {}
};
SYNTHYBOY_REGISTER_PANEL (Envelope2Panel, Page::env, 20)

class Envelope3Panel final : public EnvelopePanel
{
public:
    explicit Envelope3Panel (UiContext& c) : EnvelopePanel (c, 3) {}
};
SYNTHYBOY_REGISTER_PANEL (Envelope3Panel, Page::env, 30)

class Envelope4Panel final : public EnvelopePanel
{
public:
    explicit Envelope4Panel (UiContext& c) : EnvelopePanel (c, 4) {}
};
SYNTHYBOY_REGISTER_PANEL (Envelope4Panel, Page::env, 40)

class Envelope5Panel final : public EnvelopePanel
{
public:
    explicit Envelope5Panel (UiContext& c) : EnvelopePanel (c, 5) {}
};
SYNTHYBOY_REGISTER_PANEL (Envelope5Panel, Page::env, 50)

class Envelope6Panel final : public EnvelopePanel
{
public:
    explicit Envelope6Panel (UiContext& c) : EnvelopePanel (c, 6) {}
};
SYNTHYBOY_REGISTER_PANEL (Envelope6Panel, Page::env, 60)

} // namespace synthyboy::ui
