// SynthyBoy - UI: Panel base class and registry.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Panel.h"

#include <algorithm>

namespace synthyboy::ui
{
PanelRegistry& PanelRegistry::instance()
{
    static PanelRegistry registry;
    return registry;
}

void PanelRegistry::add (PanelEntry entry)
{
    list.push_back (std::move (entry));
    sorted = false;
}

const std::vector<PanelEntry>& PanelRegistry::entries() const
{
    if (! sorted)
    {
        std::stable_sort (list.begin(), list.end(), [] (const PanelEntry& a, const PanelEntry& b)
        {
            if (a.page != b.page)
                return static_cast<int> (a.page) < static_cast<int> (b.page);
            if (a.order != b.order)
                return a.order < b.order;
            return a.id < b.id;
        });
        sorted = true;
    }
    return list;
}

// ---- Panel ------------------------------------------------------------------------------------------------------------------------

int Panel::headerHeight() const
{
    return ctx.theme.ipx (28.0f);
}

juce::Rectangle<int> Panel::contentArea() const
{
    return getLocalBounds().reduced (ctx.theme.gap()).withTrimmedTop (headerHeight());
}

ParamControl& Panel::addControl (const std::string& key, const juce::String& label)
{
    owned.push_back (makeControl (ctx, key, label));
    addAndMakeVisible (*owned.back());
    return *owned.back();
}

ParamControl& Panel::addMoreControl (const std::string& key, const juce::String& label)
{
    auto& control = addControl (key, label);
    control.setVisible (moreExpanded);
    ++moreCount;
    if (! moreButtonAdded)
    {
        moreButtonAdded = true;
        moreButton.setClickingTogglesState (true);
        moreButton.onClick = [this] { setMoreExpanded (moreButton.getToggleState()); };
        addAndMakeVisible (moreButton);
    }
    return control;
}

void Panel::setMoreExpanded (bool shouldExpand)
{
    moreExpanded = shouldExpand;
    moreButton.setToggleState (shouldExpand, juce::dontSendNotification);
    moreButton.setButtonText (shouldExpand ? "Less" : "More");
    const size_t primary = owned.size() - moreCount;
    for (size_t i = primary; i < owned.size(); ++i)
        owned[i]->setVisible (shouldExpand);
    if (onHeightChanged)
        onHeightChanged(); // the host re-measures the panel
    resized();
}

int Panel::layoutControls (juce::Rectangle<int> area, bool measureOnly)
{
    const auto& t = ctx.theme;
    const int gap = t.gap();
    const size_t primary = owned.size() - moreCount;

    int cellW = 0, cellH = 0;
    for (const auto& c : owned)
    {
        const auto s = c->preferredSize();
        cellW = juce::jmax (cellW, s.x);
        cellH = juce::jmax (cellH, s.y);
    }
    if (cellW == 0)
        return 0;

    const int perRow = juce::jmax (1, (area.getWidth() + gap) / (cellW + gap));
    // spread the cells over the width so the panel does not look left-heavy
    const int cw = juce::jmax (cellW, (area.getWidth() - (perRow - 1) * gap) / perRow);

    int x = 0, y = 0, col = 0;
    const auto place = [&] (ParamControl& c)
    {
        if (! measureOnly)
        {
            // a toggle / choice keeps its own height, centred in the row of the knobs
            const int h = juce::jmin (cellH, c.preferredSize().y);
            c.setBounds (area.getX() + x, area.getY() + y + (cellH - h) / 2, cw, h);
        }
        x += cw + gap;
        if (++col == perRow)
        {
            col = 0;
            x = 0;
            y += cellH + gap;
        }
    };

    for (size_t i = 0; i < primary; ++i)
        place (*owned[i]);
    if (col != 0)
    {
        col = 0;
        x = 0;
        y += cellH + gap;
    }

    if (moreCount > 0)
    {
        const int toggleH = t.touch();
        if (! measureOnly)
            moreButton.setBounds (area.getX(), area.getY() + y, juce::jmax (t.touch() * 2, t.ipx (96.0f)), toggleH);
        y += toggleH + gap;
        if (moreExpanded)
        {
            for (size_t i = primary; i < owned.size(); ++i)
                place (*owned[i]);
            if (col != 0)
                y += cellH + gap;
        }
    }
    return juce::jmax (0, y - gap);
}

int Panel::preferredHeight (int width) const
{
    const auto& t = ctx.theme;
    const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
    return headerHeight() + 2 * t.gap() + const_cast<Panel*> (this)->layoutControls (area, true);
}

void Panel::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    const Palette& c = t.colours;
    const auto r = getLocalBounds().toFloat().reduced (t.px (1.0f));
    g.setColour (c.panel);
    g.fillRoundedRectangle (r, t.px (10.0f));
    g.setColour (c.outline);
    g.drawRoundedRectangle (r, t.px (10.0f), 1.0f);
    g.setColour (c.text);
    g.setFont (t.titleFont());
    g.drawFittedText (title(), getLocalBounds().reduced (t.gap(), 0).withHeight (headerHeight() + t.gap() / 2), juce::Justification::centredLeft, 1);
}

void Panel::resized()
{
    layoutControls (contentArea(), false);
}
} // namespace synthyboy::ui
