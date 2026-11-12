// SynthyBoy - UI: the tab chip row of the LFO page (see LfoTabsPanel.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LfoTabsPanel.h"

#include "LfoShapeEditor.h"
#include "OscWaveView.h"

namespace synthyboy::ui
{
class LfoTabsPanel::Chip final : public juce::Component
{
public:
    Chip (LfoTabsPanel& owner, int tabIndex) : panel (owner), tab (tabIndex)
    {
        setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = panel.ctx.theme;
        const auto& c = t.colours;
        const bool selected = panel.selectedTab() == tab;
        const bool target = panel.dragOver == tab && panel.dragFrom != tab;
        const auto r = getLocalBounds().toFloat().reduced (t.px (2.0f));
        g.setColour (selected ? c.accent : c.panelRaised);
        g.fillRoundedRectangle (r, t.px (6.0f));
        g.setColour (target ? c.mod : c.outline);
        g.drawRoundedRectangle (r, t.px (6.0f), target ? t.px (3.0f) : 1.0f);
        g.setColour (selected ? c.textOnAccent : c.text);
        g.setFont (t.valueFont());
        g.drawFittedText (tabLabel (tab), getLocalBounds(), juce::Justification::centred, 1);
    }

    void mouseDown (const juce::MouseEvent&) override { dragging = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() < panel.ctx.theme.ipx (8.0f))
            return;
        dragging = true;
        panel.dragFrom = tab;
        panel.dragOver = panel.tabAt (e.getEventRelativeTo (&panel).getPosition());
        panel.hint = panel.dragOver >= 0 && panel.dragOver != tab ? "Copy " + tabLabel (tab) + " onto " + tabLabel (panel.dragOver)
                                                                   : "Drop on another chip to copy " + tabLabel (tab);
        panel.repaint();
        for (auto& chip : panel.chips)
            chip->repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging && e.mods.isAltDown() && tab < 10 && panel.dropOnWavetable (tab, e.getScreenPosition()))
            panel.dragEnded (tab, -1);
        else if (dragging)
            panel.dragEnded (tab, panel.tabAt (e.getEventRelativeTo (&panel).getPosition()));
        else if (getLocalBounds().contains (e.getPosition()))
            panel.selectTab (tab);
        dragging = false;
    }

private:
    LfoTabsPanel& panel;
    int tab;
    bool dragging = false;
};

LfoTabsPanel::LfoTabsPanel (UiContext& context) : Panel (context) {}
LfoTabsPanel::~LfoTabsPanel() = default;

juce::String LfoTabsPanel::tabLabel (int tab)
{
    return tab < 10 ? "L" + juce::String (tab + 1) : "R" + juce::String (tab - 9);
}

void LfoTabsPanel::build()
{
    for (int i = 0; i < kNumLfoTabs; ++i)
    {
        chips[static_cast<size_t> (i)] = std::make_unique<Chip> (*this, i);
        addAndMakeVisible (*chips[static_cast<size_t> (i)]);
    }
}

int LfoTabsPanel::layoutChips (int width, bool apply) const
{
    const auto& t = ctx.theme;
    const int gap = t.gap();
    const int chipW = juce::jmax (t.touch(), t.ipx (48.0f));
    const int perRow = juce::jmax (1, (width + gap) / (chipW + gap));
    const auto origin = contentArea().getPosition();
    int x = 0, y = 0, col = 0;
    for (int i = 0; i < kNumLfoTabs; ++i)
    {
        if (i == 10 && col != 0) // the random LFOs start a row of their own
        {
            col = 0;
            x = 0;
            y += t.touch() + gap;
        }
        if (apply)
            chips[static_cast<size_t> (i)]->setBounds (origin.x + x, origin.y + y, chipW, t.touch());
        x += chipW + gap;
        if (++col == perRow)
        {
            col = 0;
            x = 0;
            y += t.touch() + gap;
        }
    }
    return col != 0 ? y + t.touch() : juce::jmax (t.touch(), y - gap);
}

int LfoTabsPanel::preferredHeight (int width) const
{
    const auto& t = ctx.theme;
    return headerHeight() + 2 * t.gap() + layoutChips (width - 2 * t.gap(), false);
}

void LfoTabsPanel::resized()
{
    layoutChips (contentArea().getWidth(), true);
}

void LfoTabsPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    if (hint.isNotEmpty())
    {
        const auto& t = ctx.theme;
        g.setColour (t.colours.mod);
        g.setFont (t.labelFont());
        g.drawText (hint, getLocalBounds().reduced (t.gap(), 0).withHeight (headerHeight() + t.gap() / 2), juce::Justification::centredRight, true);
    }
}

juce::Rectangle<int> LfoTabsPanel::chipBounds (int tab) const
{
    return chips[static_cast<size_t> (juce::jlimit (0, kNumLfoTabs - 1, tab))]->getBounds();
}

int LfoTabsPanel::tabAt (juce::Point<int> p) const
{
    for (int i = 0; i < kNumLfoTabs; ++i)
        if (chips[static_cast<size_t> (i)]->getBounds().contains (p))
            return i;
    return -1;
}

void LfoTabsPanel::selectTab (int tab)
{
    tab = juce::jlimit (0, kNumLfoTabs - 1, tab);
    if (ctx.view->lfoTab == tab)
        return;
    ctx.view->lfoTab = tab;
    for (auto& c : chips)
        c->repaint();
    if (onHeightChanged)
        onHeightChanged(); // the host shows the panel of the chosen LFO and hides the others
}

bool LfoTabsPanel::copyTab (int from, int to)
{
    if (from < 0 || to < 0 || from >= kNumLfoTabs || to >= kNumLfoTabs || from == to || (from < 10) != (to < 10))
        return false;
    const bool random = from >= 10;
    return copyLfoSettings (ctx, random, random ? from - 9 : from + 1, random ? to - 9 : to + 1);
}

void LfoTabsPanel::dragEnded (int from, int overTab)
{
    dragFrom = -1;
    dragOver = -1;
    hint = overTab >= 0 && overTab != from && copyTab (from, overTab) ? "Copied " + tabLabel (from) + " to " + tabLabel (overTab) : juce::String();
    repaint();
    for (auto& c : chips)
        c->repaint();
}

bool LfoTabsPanel::dropOnWavetable (int from, juce::Point<int> screenPos)
{
    if (from < 0 || from >= 10)
        return false;
    auto* top = overlayRootOf (*this);
    if (top == nullptr)
        return false;
    juce::Component* hit = top->getComponentAt (top->getLocalPoint (nullptr, screenPos));
    while (hit != nullptr && dynamic_cast<OscWaveView*> (hit) == nullptr)
        hit = hit->getParentComponent();
    auto* view = dynamic_cast<OscWaveView*> (hit);
    if (view == nullptr || view->oscType() != kOscTypeWavetable)
        return false;
    const FlowResult result = view->copyLfoShapeHere (from + 1);
    hint = result.message;
    repaint();
    return result.ok;
}

SYNTHYBOY_REGISTER_PANEL (LfoTabsPanel, Page::lfo, 5)
} // namespace synthyboy::ui
