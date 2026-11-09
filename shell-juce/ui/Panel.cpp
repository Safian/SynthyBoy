// SynthyBoy - UI: Panel base class and registry.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Panel.h"

#include <algorithm>

#include "ModuleSheet.h"

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

std::unique_ptr<Panel> PanelRegistry::create (const std::string& id, UiContext& context) const
{
    for (const auto& entry : entries())
        if (entry.id == id)
            return entry.create (context);
    return nullptr;
}

// ---- Panel ------------------------------------------------------------------------------------------------------------------------

Panel::Panel (UiContext& context) : ctx (context)
{
    addComponentListener (this);
    headerPress.onLongPress = [this] { headerLongPressed(); };
}

Panel::~Panel()
{
    removeComponentListener (this);
}

int Panel::headerHeight() const
{
    // a module header carries the "..." button, which is a full touch target
    return modulePrefix().empty() ? ctx.theme.ipx (28.0f) : juce::jmax (ctx.theme.ipx (28.0f), ctx.theme.touch());
}

juce::Rectangle<int> Panel::headerBounds() const
{
    return getLocalBounds().withHeight (headerHeight() + ctx.theme.gap() / 2);
}

void Panel::ensureModuleButton()
{
    const auto prefix = modulePrefix();
    if (prefix.empty() || moduleMenuButton != nullptr)
        return;
    moduleMenuButton = std::make_unique<ModuleMenuButton> (ctx, prefix);
    addAndMakeVisible (*moduleMenuButton);
    componentMovedOrResized (*this, false, true);
}

void Panel::parentHierarchyChanged()
{
    ensureModuleButton();
}

void Panel::componentMovedOrResized (juce::Component&, bool, bool)
{
    if (moduleMenuButton != nullptr)
    {
        const int s = ctx.theme.touch();
        moduleMenuButton->setBounds (getWidth() - ctx.theme.gap() - s, 0, s, s);
    }
}

void Panel::headerLongPressed()
{
    const auto prefix = modulePrefix();
    if (! prefix.empty())
    {
        headerDown = false;
        showModuleSheet (ctx, prefix, *this);
    }
}

bool Panel::dropModuleOn (juce::Point<int> parentPos, bool copy, bool withMods)
{
    const auto mine = modulePrefix();
    auto* parent = getParentComponent();
    if (mine.empty() || parent == nullptr)
        return false;
    for (auto* child : parent->getChildren())
        if (auto* other = dynamic_cast<Panel*> (child); other != nullptr && other != this && other->isVisible() && other->getBounds().contains (parentPos))
        {
            const auto theirs = other->modulePrefix();
            if (theirs.empty() || ! module_ops::isModule (theirs))
                return false;
            return copy ? module_ops::copyOnto (ctx, mine, theirs, withMods) : module_ops::swap (ctx, mine, theirs, true);
        }
    return false;
}

void Panel::mouseDown (const juce::MouseEvent& e)
{
    headerDown = ! modulePrefix().empty() && headerBounds().contains (e.getPosition());
    headerDragging = false;
    if (headerDown)
        headerPress.press (e.position);
}

void Panel::mouseDrag (const juce::MouseEvent& e)
{
    if (! headerDown)
        return;
    headerPress.moved (e.position);
    if (e.getDistanceFromDragStart() > ctx.theme.ipx (12.0f))
        headerDragging = true;
}

void Panel::mouseUp (const juce::MouseEvent& e)
{
    headerPress.cancel();
    if (headerDown && headerDragging)
        dropModuleOn (e.getEventRelativeTo (getParentComponent()).getPosition(), e.mods.isAltDown(), e.mods.isShiftDown());
    headerDown = headerDragging = false;
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
    owned.push_back (makeControl (ctx, key, label));
    return adoptMore (*owned.back());
}

ParamControl& Panel::addMoreControl (std::unique_ptr<ParamControl> custom)
{
    owned.push_back (std::move (custom));
    return adoptMore (*owned.back());
}

ParamControl& Panel::adoptMore (ParamControl& control)
{
    addAndMakeVisible (control);
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
        if (c->spansRow())
            continue; // a control with a row of its own does not size the grid
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
        if (c.spansRow())
        {
            if (col != 0)
            {
                col = 0;
                x = 0;
                y += cellH + gap;
            }
            if (! measureOnly)
                c.setBounds (area.getX(), area.getY() + y, area.getWidth(), c.preferredSize().y);
            y += c.preferredSize().y + gap;
            return;
        }
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
