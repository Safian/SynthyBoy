// SynthyBoy - UI: panel host and page tabs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PanelHost.h"

namespace synthyboy::ui
{
PageTabs::PageTabs (UiContext& context) : ctx (context)
{
    for (int i = 0; i < kNumPages; ++i)
    {
        auto b = std::make_unique<juce::TextButton> (pageLabel (static_cast<Page> (i)));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { select (static_cast<Page> (i)); };
        strip.addAndMakeVisible (*b);
        buttons.push_back (std::move (b));
    }
    viewport.setViewedComponent (&strip, false);
    viewport.setScrollBarsShown (false, false, false, true);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    addAndMakeVisible (viewport);
    select (Page::glb, false);
}

void PageTabs::select (Page page, bool notify)
{
    current = page;
    for (int i = 0; i < kNumPages; ++i)
        buttons[static_cast<size_t> (i)]->setToggleState (i == static_cast<int> (page), juce::dontSendNotification);
    if (notify && onSelect)
        onSelect (page);
}

void PageTabs::setHasPanels (Page page, bool has)
{
    buttons[static_cast<size_t> (page)]->setAlpha (has ? 1.0f : 0.55f);
}

void PageTabs::resized()
{
    viewport.setBounds (getLocalBounds());
    const int h = getHeight();
    const int w = juce::jmax (ctx.theme.touch(), getWidth() / kNumPages);
    strip.setSize (w * kNumPages, h);
    for (int i = 0; i < kNumPages; ++i)
        buttons[static_cast<size_t> (i)]->setBounds (i * w, 0, w, h);
    // the selected tab uses the "on" colour of the button
}

// ---- PanelHost --------------------------------------------------------------------------------------------------------------------

PanelHost::PanelHost (UiContext& context) : ctx (context)
{
    for (const auto& entry : PanelRegistry::instance().entries())
    {
        auto panel = entry.create (ctx);
        panel->build();
        panel->onHeightChanged = [this] { layoutPanels(); };
        content.addAndMakeVisible (*panel);
        panelPages.push_back (entry.page);
        panels.push_back (std::move (panel));
    }
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    addAndMakeVisible (viewport);
}

PanelHost::~PanelHost()
{
    viewport.setViewedComponent (nullptr, false);
}

bool PanelHost::pageHasPanels (Page page) const
{
    for (const auto p : panelPages)
        if (p == page)
            return true;
    return false;
}

void PanelHost::setPage (Page page)
{
    currentPage = page;
    layoutPanels();
    if (layoutMode == LayoutMode::wide)
    {
        // scroll to the first tile of the page
        for (size_t i = 0; i < panels.size(); ++i)
            if (panelPages[i] == page)
            {
                viewport.setViewPosition (0, panels[i]->getY() - ctx.theme.gap());
                break;
            }
    }
    else
        viewport.setViewPosition (0, 0);
    repaint();
}

std::vector<Panel*> PanelHost::visiblePanels() const
{
    std::vector<Panel*> out;
    for (size_t i = 0; i < panels.size(); ++i)
        if (panels[i]->isVisible())
            out.push_back (panels[i].get());
    return out;
}

void PanelHost::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutPanels();
}

void PanelHost::layoutPanels()
{
    const auto& t = ctx.theme;
    layoutMode = layoutModeForWidth (getWidth());
    const int columns = columnsFor (layoutMode);
    const int gap = t.gap();
    const int scrollBar = viewport.getScrollBarThickness();
    const int availW = juce::jmax (1, getWidth() - scrollBar);
    const int colW = juce::jmax (1, (availW - gap * (columns + 1)) / columns);

    std::vector<int> colHeight (static_cast<size_t> (columns), gap);
    for (size_t i = 0; i < panels.size(); ++i)
    {
        const bool shown = (layoutMode == LayoutMode::wide || panelPages[i] == currentPage) && ! panels[i]->hiddenInLayout();
        panels[i]->setVisible (shown);
        if (! shown)
            continue;

        // masonry: the next tile goes into the shortest column
        size_t col = 0;
        for (size_t k = 1; k < colHeight.size(); ++k)
            if (colHeight[k] < colHeight[col])
                col = k;
        const int h = panels[i]->preferredHeight (colW);
        panels[i]->setBounds (gap + static_cast<int> (col) * (colW + gap), colHeight[col], colW, h);
        colHeight[col] += h + gap;
    }
    int total = gap;
    for (const int h : colHeight)
        total = juce::jmax (total, h);
    content.setSize (availW, juce::jmax (total, getHeight()));
}

void PanelHost::paint (juce::Graphics& g)
{
    g.fillAll (ctx.theme.colours.background);
    if (layoutMode != LayoutMode::wide && ! pageHasPanels (currentPage))
    {
        g.setColour (ctx.theme.colours.textDim);
        g.setFont (ctx.theme.titleFont());
        g.drawFittedText (juce::String (pageLabel (currentPage)) + ": panels come with the next Phase 5 packages",
                          getLocalBounds().reduced (ctx.theme.ipx (24.0f)), juce::Justification::centred, 3);
    }
}
} // namespace synthyboy::ui
