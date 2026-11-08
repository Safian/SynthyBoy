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

    for (auto* param : host.parameters().processor.getParameters())
        param->addListener (this);

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

    top->onUndo = [this] { performUndo(); };
    top->onRedo = [this] { performRedo(); };
    top->onAbToggle = [this] { toggleAb(); };
    top->onAssignToggle = [this] { setSourceBarOpen (! sourcesOpen); };
    top->onMenu = [this] { showMenu(); };
    top->onVoiceBadge = [this]
    {
        tabs->select (Page::glb, false);
        panels->setPage (Page::glb);
    };
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
    lastPresetName = host.presetName();
    top->setPresetName (lastPresetName);
    updateAbLabel();
    updateUndoRedoButtons();
    setWantsKeyboardFocus (true);
    startTimerHz (15);
}

SynthyBoyEditor::~SynthyBoyEditor()
{
    stopTimer();
    for (auto* param : host.parameters().processor.getParameters())
        param->removeListener (this);
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
    const auto currentName = host.presetName();
    if (currentName != lastPresetName)
    {
        lastPresetName = currentName;
        top->setPresetName (currentName);
        undoHistory.clear();
        updateUndoRedoButtons();
    }
    assign.refresh(); // host automation can move a mod slot
    if (auto* poly = host.parameters().getRawParameterValue ("master.polyphony"))
    {
        const int voices = host.activeVoiceCount();
        top->setVoiceBadge (voices, juce::roundToInt (poly->load()));
        tabs->setBadge (Page::glb, voices > 0 ? juce::String (voices) : juce::String());
    }
}

void SynthyBoyEditor::parameterGestureChanged (int, bool starting)
{
    if (starting)
    {
        if (openGestures++ == 0)
            gestureStartValues = readLiveValues();
    }
    else
    {
        if (--openGestures <= 0)
        {
            openGestures = 0;
            const auto current = readLiveValues();
            if (current != gestureStartValues)
            {
                undoHistory.push (gestureStartValues);
                updateUndoRedoButtons();
            }
        }
    }
}

void SynthyBoyEditor::performUndo()
{
    ParamValues target;
    if (undoHistory.undo (readLiveValues(), target))
    {
        applyValues (target);
        updateUndoRedoButtons();
        assign.refresh();
    }
}

void SynthyBoyEditor::performRedo()
{
    ParamValues target;
    if (undoHistory.redo (readLiveValues(), target))
    {
        applyValues (target);
        updateUndoRedoButtons();
        assign.refresh();
    }
}

void SynthyBoyEditor::updateUndoRedoButtons()
{
    top->updateUndoRedo (undoHistory.canUndo(), undoHistory.canRedo());
}

bool SynthyBoyEditor::keyPressed (const juce::KeyPress& key)
{
    const bool cmdOrCtrl = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    if (cmdOrCtrl)
    {
        if (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')
        {
            if (key.getModifiers().isShiftDown())
                performRedo();
            else
                performUndo();
            return true;
        }
        if (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y')
        {
            performRedo();
            return true;
        }
    }
    return false;
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
    menu.addItem (10, "Undo", undoHistory.canUndo());
    menu.addItem (11, "Redo", undoHistory.canRedo());
    menu.addSeparator();
    menu.addItem (1, "Copy " + juce::String (host.abCompare().active() == AbCompare::Slot::a ? "A to B" : "B to A"));
    menu.addSeparator();

    // Module submenu
    juce::PopupMenu moduleMenu;
    const auto& clip = ModuleClipboard::instance();
    const bool canPaste = clip.hasContent();
    moduleMenu.addItem (50, "Copy Active Module", true);
    moduleMenu.addItem (51, "Copy Active Module (w/ mods)", true);
    moduleMenu.addItem (52, "Paste Module", canPaste);
    moduleMenu.addItem (53, "Paste Module (w/ mods)", canPaste);
    moduleMenu.addItem (54, "Init All Modules", true);
    menu.addSubMenu ("Module", moduleMenu);

    menu.addSeparator();
    menu.addItem (2, uiTheme.isDark() ? "Light theme" : "Dark theme");
    juce::PopupMenu sizes;
    const float steps[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (int i = 0; i < 5; ++i)
        sizes.addItem (20 + i, juce::String (juce::roundToInt (steps[i] * 100.0f)) + " %", true, std::abs (uiTheme.scale() - steps[i]) < 0.01f);
    menu.addSubMenu ("UI size", sizes);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&top->menu),
                        [safe = juce::Component::SafePointer<SynthyBoyEditor> (this), steps] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == 10)
                                safe->performUndo();
                            else if (result == 11)
                                safe->performRedo();
                            else if (result == 1)
                                safe->copyActiveToOtherAb();
                            else if (result == 50)
                            {
                                safe->undoHistory.push (safe->readLiveValues());
                                ModuleClipboard::instance().copy ("osc1", safe->readLiveValues(), false);
                                safe->updateUndoRedoButtons();
                            }
                            else if (result == 51)
                            {
                                safe->undoHistory.push (safe->readLiveValues());
                                ModuleClipboard::instance().copy ("osc1", safe->readLiveValues(), true, &safe->assign.matrix());
                                safe->updateUndoRedoButtons();
                            }
                            else if (result == 52)
                            {
                                auto target = safe->readLiveValues();
                                safe->undoHistory.push (target);
                                if (ModuleClipboard::instance().paste ("osc2", target, false))
                                    safe->applyValues (target);
                                safe->updateUndoRedoButtons();
                            }
                            else if (result == 53)
                            {
                                auto target = safe->readLiveValues();
                                safe->undoHistory.push (target);
                                if (ModuleClipboard::instance().paste ("osc2", target, true, &safe->assign.matrix()))
                                    safe->applyValues (target);
                                safe->updateUndoRedoButtons();
                            }
                            else if (result == 54)
                            {
                                auto target = safe->readLiveValues();
                                safe->undoHistory.push (target);
                                for (const char* p : { "osc1", "osc2", "osc3", "filter1", "filter2", "env1", "env2", "env3", "env4", "env5", "env6", "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "lfo7", "lfo8" })
                                    ModuleClipboard::initModule (p, target);
                                safe->applyValues (target);
                                safe->updateUndoRedoButtons();
                            }
                            else if (result == 2)
                                safe->setDarkMode (! safe->uiTheme.isDark());
                            else if (result >= 20 && result < 25)
                                safe->setUiScale (steps[result - 20]);
                        });
}
} // namespace synthyboy::ui
