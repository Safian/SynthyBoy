// SynthyBoy - UI: the oscillator chip row (see OscTabsPanel.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OscTabsPanel.h"

#include "../ModuleSheet.h"

namespace synthyboy::ui
{
class OscTabsPanel::Chip final : public juce::Component
{
public:
    Chip (OscTabsPanel& owner, int tabIndex) : panel (owner), tab (tabIndex)
    {
        press.onLongPress = [this]
        {
            longPressed = true;
            dragging = false;
            panel.chipLongPressed (tab);
        };
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
        g.drawFittedText ("OSC" + juce::String (tab + 1), getLocalBounds(), juce::Justification::centred, 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        longPressed = false;
        press.press (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        press.moved (e.position);
        if (longPressed)
            return;
        if (! dragging && e.getDistanceFromDragStart() < panel.ctx.theme.ipx (8.0f))
            return;
        dragging = true;
        panel.dragFrom = tab;
        panel.dragOver = panel.tabAt (e.getEventRelativeTo (&panel).getPosition());
        const bool copy = e.mods.isAltDown();
        panel.hint = panel.dragOver >= 0 && panel.dragOver != tab
                         ? (copy ? "Copy OSC" : "Swap OSC") + juce::String (tab + 1) + " and OSC" + juce::String (panel.dragOver + 1)
                         : "Swipe = other oscillator";
        panel.repaint();
        for (auto& chip : panel.chips)
            chip->repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        press.cancel();
        if (longPressed)
        {
            longPressed = false;
            return;
        }
        const int over = panel.tabAt (e.getEventRelativeTo (&panel).getPosition());
        if (dragging)
        {
            if (over >= 0 && over != tab)
                panel.dragEnded (tab, over, e.mods.isAltDown(), e.mods.isShiftDown());
            else if (std::abs (e.x - e.getMouseDownX()) >= panel.ctx.theme.ipx (48.0f))
                panel.stepTab ((e.x - e.getMouseDownX()) < 0 ? +1 : -1); // swipe left = the next oscillator
            panel.dragFrom = panel.dragOver = -1;
            panel.repaint();
            for (auto& chip : panel.chips)
                chip->repaint();
        }
        else if (getLocalBounds().contains (e.getPosition()))
            panel.selectTab (tab);
        dragging = false;
    }

    LongPressDetector& longPress() noexcept { return press; }

private:
    OscTabsPanel& panel;
    int tab;
    bool dragging = false, longPressed = false;
    LongPressDetector press;
};

class OscTabsPanel::PowerDot final : public juce::Component
{
public:
    PowerDot (OscTabsPanel& owner, int tabIndex)
        : panel (owner), tab (tabIndex), handle (owner.ctx, "osc" + std::to_string (tabIndex + 1) + ".on")
    {
        handle.onChange = [this] { repaint(); };
        setTooltip ("Oscillator " + juce::String (tab + 1) + " on / off");
    }

    bool isOn() const { return handle.valid() && handle.plain() >= 0.5f; }

    void paint (juce::Graphics& g) override
    {
        const auto& t = panel.ctx.theme;
        const auto& c = t.colours;
        const auto centre = getLocalBounds().toFloat().getCentre();
        const float r = t.px (8.0f);
        g.setColour (isOn() ? c.accent : c.panelRaised);
        g.fillEllipse (centre.x - r, centre.y - r, 2.0f * r, 2.0f * r);
        g.setColour (isOn() ? c.accent : c.outline);
        g.drawEllipse (centre.x - r, centre.y - r, 2.0f * r, 2.0f * r, 1.5f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()))
            panel.togglePower (tab);
    }

    void setTooltip (const juce::String& text) { tooltip = text; }
    juce::String getTooltip() const { return tooltip; }

private:
    OscTabsPanel& panel;
    int tab;
    ParamHandle handle;
    juce::String tooltip;
};

OscTabsPanel::OscTabsPanel (UiContext& context) : Panel (context) {}
OscTabsPanel::~OscTabsPanel() = default;

void OscTabsPanel::build()
{
    for (int i = 0; i < kNumOscTabs; ++i)
    {
        chips[static_cast<size_t> (i)] = std::make_unique<Chip> (*this, i);
        dots[static_cast<size_t> (i)] = std::make_unique<PowerDot> (*this, i);
        addAndMakeVisible (*chips[static_cast<size_t> (i)]);
        addAndMakeVisible (*dots[static_cast<size_t> (i)]);
    }
}

int OscTabsPanel::preferredHeight (int) const
{
    return headerHeight() + 2 * ctx.theme.gap() + ctx.theme.touch();
}

void OscTabsPanel::resized()
{
    const auto& t = ctx.theme;
    const auto area = contentArea();
    const int gap = t.gap();
    const int pairW = (area.getWidth() - (kNumOscTabs - 1) * gap) / kNumOscTabs;
    const int dotW = t.touch();
    const int chipW = juce::jmax (t.touch(), pairW - dotW);
    for (int i = 0; i < kNumOscTabs; ++i)
    {
        const int x = area.getX() + i * (pairW + gap);
        chips[static_cast<size_t> (i)]->setBounds (x, area.getY(), chipW, t.touch());
        dots[static_cast<size_t> (i)]->setBounds (x + chipW, area.getY(), dotW, t.touch());
    }
}

void OscTabsPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    if (hint.isNotEmpty())
    {
        g.setColour (ctx.theme.colours.mod);
        g.setFont (ctx.theme.labelFont());
        g.drawText (hint, getLocalBounds().reduced (ctx.theme.gap(), 0).withHeight (headerHeight() + ctx.theme.gap() / 2),
                    juce::Justification::centredRight, true);
    }
}

juce::Rectangle<int> OscTabsPanel::chipBounds (int tab) const
{
    return chips[static_cast<size_t> (juce::jlimit (0, kNumOscTabs - 1, tab))]->getBounds();
}

juce::Rectangle<int> OscTabsPanel::powerBounds (int tab) const
{
    return dots[static_cast<size_t> (juce::jlimit (0, kNumOscTabs - 1, tab))]->getBounds();
}

int OscTabsPanel::tabAt (juce::Point<int> p) const
{
    for (int i = 0; i < kNumOscTabs; ++i)
        if (chips[static_cast<size_t> (i)]->getBounds().contains (p))
            return i;
    return -1;
}

void OscTabsPanel::selectTab (int tab)
{
    tab = juce::jlimit (0, kNumOscTabs - 1, tab);
    if (ctx.view->oscTab == tab)
        return;
    ctx.view->oscTab = tab;
    for (auto& c : chips)
        c->repaint();
    if (onHeightChanged)
        onHeightChanged(); // the host shows the panel of the chosen oscillator and hides the others
}

void OscTabsPanel::stepTab (int direction)
{
    selectTab (ctx.view->oscTab + (direction < 0 ? -1 : 1));
}

void OscTabsPanel::togglePower (int tab)
{
    ParamHandle h (ctx, "osc" + std::to_string (juce::jlimit (0, kNumOscTabs - 1, tab) + 1) + ".on");
    if (h.valid())
        h.setPlainComplete (h.plain() >= 0.5f ? 0.0f : 1.0f);
}

bool OscTabsPanel::dragEnded (int from, int overTab, bool copy, bool withMods)
{
    dragFrom = dragOver = -1;
    bool done = false;
    if (from >= 0 && overTab >= 0 && from != overTab)
    {
        const std::string a = "osc" + std::to_string (from + 1), b = "osc" + std::to_string (overTab + 1);
        done = copy ? module_ops::copyOnto (ctx, a, b, withMods) : module_ops::swap (ctx, a, b, true);
        hint = done ? (copy ? "Copied OSC" : "Swapped OSC") + juce::String (from + 1) + " / OSC" + juce::String (overTab + 1) : juce::String();
    }
    repaint();
    return done;
}

void OscTabsPanel::chipLongPressed (int tab)
{
    tab = juce::jlimit (0, kNumOscTabs - 1, tab);
    showModuleSheet (ctx, "osc" + std::to_string (tab + 1), *chips[static_cast<size_t> (tab)]);
}

SYNTHYBOY_REGISTER_PANEL (OscTabsPanel, Page::osc, 5)
} // namespace synthyboy::ui
