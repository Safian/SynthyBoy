// SynthyBoy - UI: editor shell.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Editor.h"

namespace synthyboy::ui
{
SynthyBoyEditor::SynthyBoyEditor (juce::AudioProcessor& owner, UiHost& h)
    : juce::AudioProcessorEditor (owner),
      host (h),
      uiTheme (Theme::dark()),
      laf (uiTheme),
      assign (h),
      ctx { h, uiTheme, assign }
{
    setLookAndFeel (&laf);

    top = std::make_unique<TopBar> (ctx);
    macros = std::make_unique<MacroRow> (ctx);
    sources = std::make_unique<ModSourceBar> (ctx);
    tabs = std::make_unique<PageTabs> (ctx);
    panels = std::make_unique<PanelHost> (ctx);
    keyboard = std::make_unique<KeyboardStrip> (ctx);

    addAndMakeVisible (*top);
    addAndMakeVisible (*macros);
    addChildComponent (*sources);
    addAndMakeVisible (*tabs);
    addAndMakeVisible (*panels);
    addAndMakeVisible (*keyboard);

    top->onAbToggle = [this] { toggleAb(); };
    top->onAssignToggle = [this] { setSourceBarOpen (! sourcesOpen); };
    top->onMenu = [this] { showMenu(); };
    tabs->onSelect = [this] (Page p) { panels->setPage (p); };
    for (int i = 0; i < kNumPages; ++i)
        tabs->setHasPanels (static_cast<Page> (i), panels->pageHasPanels (static_cast<Page> (i)));

    // first page that has a panel
    for (int i = 0; i < kNumPages; ++i)
        if (panels->pageHasPanels (static_cast<Page> (i)))
        {
            tabs->select (static_cast<Page> (i), false);
            panels->setPage (static_cast<Page> (i));
            break;
        }

    setResizable (true, false);
    setResizeLimits (360, 480, 1800, 1400);
    setSize (1024, 768); // tablet landscape; the layout follows the width, so any host size works
    top->setPresetName (host.presetName());
    updateAbLabel();
    startTimerHz (15);
}

SynthyBoyEditor::~SynthyBoyEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SynthyBoyEditor::setReferenceSize (int width, int height)
{
    setSize (width, height);
}

void SynthyBoyEditor::paint (juce::Graphics& g)
{
    g.fillAll (uiTheme.colours.background);
}

void SynthyBoyEditor::resized()
{
    auto r = getLocalBounds();
    const auto& t = uiTheme;
    const bool shareRow = r.getWidth() >= 800;
    const bool compact = r.getHeight() < 500;

    keyboard->setBounds (r.removeFromBottom (t.keyboardHeight (compact)));

    if (shareRow)
    {
        auto row = r.removeFromTop (t.macroRowHeight());
        top->setBounds (row.removeFromLeft (row.getWidth() * 42 / 100));
        macros->setBounds (row);
    }
    else
    {
        top->setBounds (r.removeFromTop (t.topBarHeight()));
        macros->setBounds (r.removeFromTop (t.macroRowHeight()));
    }

    if (sourcesOpen)
        sources->setBounds (r.removeFromTop (sources->preferredHeight()));
    tabs->setBounds (r.removeFromTop (t.tabBarHeight()));
    panels->setBounds (r);
}

void SynthyBoyEditor::timerCallback()
{
    top->setPresetName (host.presetName());
    assign.refresh(); // host automation can move a mod slot
}

void SynthyBoyEditor::setSourceBarOpen (bool open)
{
    sourcesOpen = open;
    sources->setVisible (open);
    top->setAssignOpen (open);
    if (! open)
        assign.exitAssignMode();
    resized();
}

ParamValues SynthyBoyEditor::readLiveValues() const
{
    auto values = ParamValues::defaults();
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        if (auto* raw = host.parameters().getRawParameterValue (table[i].key))
            values.plain[i] = raw->load();
    return values;
}

void SynthyBoyEditor::applyValues (const ParamValues& values)
{
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        if (auto* p = host.parameters().getParameter (table[i].key))
        {
            const float target = p->convertTo0to1 (values.plain[i]);
            if (std::abs (p->getValue() - target) > 1.0e-7f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (target);
                p->endChangeGesture();
            }
        }
}

void SynthyBoyEditor::toggleAb()
{
    applyValues (host.abCompare().toggle (readLiveValues()));
    updateAbLabel();
    assign.refresh();
}

void SynthyBoyEditor::copyActiveToOtherAb()
{
    host.abCompare().copyActiveToOther (readLiveValues());
    updateAbLabel();
}

void SynthyBoyEditor::updateAbLabel()
{
    top->setAbLabel (host.abCompare().active() == AbCompare::Slot::a ? "A" : "B");
}

void SynthyBoyEditor::setUiScale (float scale)
{
    const float before = uiTheme.scale();
    uiTheme.setScale (scale);
    laf.refresh();
    const float ratio = uiTheme.scale() / before;
    setSize (juce::roundToInt (static_cast<float> (getWidth()) * ratio), juce::roundToInt (static_cast<float> (getHeight()) * ratio));
    resized();
    repaint();
}

void SynthyBoyEditor::setDarkMode (bool dark)
{
    uiTheme.setDark (dark);
    laf.refresh();
    sendLookAndFeelChange();
    repaint();
}

void SynthyBoyEditor::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Copy " + juce::String (host.abCompare().active() == AbCompare::Slot::a ? "A to B" : "B to A"));
    menu.addSeparator();
    menu.addItem (2, uiTheme.isDark() ? "Light theme" : "Dark theme");
    juce::PopupMenu sizes;
    const float steps[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (int i = 0; i < 5; ++i)
        sizes.addItem (10 + i, juce::String (juce::roundToInt (steps[i] * 100.0f)) + " %", true, std::abs (uiTheme.scale() - steps[i]) < 0.01f);
    menu.addSubMenu ("UI size", sizes);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&top->menu),
                        [safe = juce::Component::SafePointer<SynthyBoyEditor> (this), steps] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == 1)
                                safe->copyActiveToOtherAb();
                            else if (result == 2)
                                safe->setDarkMode (! safe->uiTheme.isDark());
                            else if (result >= 10 && result < 15)
                                safe->setUiScale (steps[result - 10]);
                        });
}
} // namespace synthyboy::ui
