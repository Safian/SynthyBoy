// SynthyBoy - UI: editor shell.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Editor.h"
#include <algorithm>
#include <string>
#include <vector>

namespace synthyboy::ui
{
SynthyBoyEditor::SynthyBoyEditor (UiHost& h)
    : host (h),
      uiTheme (Theme::dark()),
      laf (uiTheme),
      assign (h),
      ctx { h, uiTheme, assign }
{
    setLookAndFeel (&laf);
    ctx.view->overlayRoot = this;

    host.parameterStore().addListener (this); // gestures of every parameter: the undo steps

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
    top->onPresetNameClick = [this] { showPresetMenu(); };
    top->onVoiceBadge = [this]
    {
        tabs->select (Page::glb, false);
        panels->setPage (Page::glb);
    };
    tabs->onSelect = [this] (Page p) { panels->setPage (p); };
    ctx.view->openSlotCurve = [this] (int slot0)
    {
        tabs->select (Page::mod, false);
        panels->setPage (Page::mod);
        for (const auto& panel : panels->allPanels())
            panel->showModSlotCurve (slot0);
    };
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

    setSize (1024, 768); // tablet landscape; the layout follows the width, so any host size works
    lastPresetName = host.presetName();
    lastModifiedState = host.isPresetModified();
    top->setPresetName (lastPresetName);
    top->setPresetModified (lastModifiedState);
    updateAbLabel();
    updateUndoRedoButtons();
    setWantsKeyboardFocus (true);
    startTimerHz (15);
}

SynthyBoyEditor::~SynthyBoyEditor()
{
    stopTimer();
    // a call-out that is still open is a child of this editor (OverlayHost): it must not outlive the controls it shows
    for (int i = getNumChildComponents(); --i >= 0;)
        if (auto* box = dynamic_cast<juce::CallOutBox*> (getChildComponent (i)))
        {
            box->setVisible (false);
            box->dismiss();
        }
    host.parameterStore().removeListener (this);
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

void SynthyBoyEditor::setKeyboardVisible (bool visible)
{
    if (keyboardVisible == visible)
        return;
    keyboardVisible = visible;
    keyboard->setVisible (visible);
    resized();
}

void SynthyBoyEditor::resized()
{
    auto r = getLocalBounds();
    const auto& t = uiTheme;
    const bool shareRow = r.getWidth() >= 800;
    const bool compact = r.getHeight() < 500;

    if (keyboardVisible)
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
    retiredSheets.clear();
    if (const uint32_t revision = host.contentRevision(); revision != checkedContentRevision)
    {
        checkedContentRevision = revision;
        if (sheet == nullptr)
            checkMissingContent();
    }
    const auto currentName = host.presetName();
    const bool isMod = host.isPresetModified() || undoHistory.canUndo();
    if (currentName != lastPresetName || isMod != lastModifiedState)
    {
        if (currentName != lastPresetName)
        {
            lastPresetName = currentName;
            undoHistory.clear();
        }
        lastModifiedState = isMod;
        top->setPresetName (currentName);
        top->setPresetModified (isMod);
        updateUndoRedoButtons();
    }
    assign.refresh(); // host automation can move a mod slot
    static const int polyphonyIndex = resolveKey ("master.polyphony").index;
    if (host.parameterStore().contains (polyphonyIndex))
    {
        const int voices = host.activeVoiceCount();
        top->setVoiceBadge (voices, juce::roundToInt (host.parameterStore().plain (polyphonyIndex)));
        tabs->setBadge (Page::glb, voices > 0 ? juce::String (voices) : juce::String());
    }
}

void SynthyBoyEditor::parameterGestureChanged (int, bool starting)
{
    if (replayingHistory) // the writes of undo / redo themselves are no new undo steps (they would clear the redo stack)
        return;
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
        replayingHistory = true;
        applyValues (target);
        replayingHistory = false;
        updateUndoRedoButtons();
        assign.refresh();
    }
}

void SynthyBoyEditor::performRedo()
{
    ParamValues target;
    if (undoHistory.redo (readLiveValues(), target))
    {
        replayingHistory = true;
        applyValues (target);
        replayingHistory = false;
        updateUndoRedoButtons();
        assign.refresh();
    }
}

void SynthyBoyEditor::updateUndoRedoButtons()
{
    top->updateUndoRedo (undoHistory.canUndo(), undoHistory.canRedo());
    const bool isMod = host.isPresetModified() || undoHistory.canUndo();
    lastModifiedState = isMod;
    top->setPresetModified (isMod);
}

bool SynthyBoyEditor::keyPressed (const juce::KeyPress& key)
{
    if (! host.preferences().keyboardShortcuts) // "Keyboard shortcuts" off (S p.314): the computer keyboard belongs to the host
        return false;
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

void SynthyBoyEditor::showSheet (std::unique_ptr<juce::Component> component)
{
    closeContentSheet();
    sheet = std::move (component);
    addAndMakeVisible (*sheet);
    const int w = juce::jmin (getWidth() - 2 * uiTheme.gap(), uiTheme.ipx (640.0f));
    const int h = juce::jmin (getHeight() - 2 * uiTheme.gap(), uiTheme.ipx (520.0f));
    sheet->setBounds (getLocalBounds().withSizeKeepingCentre (w, h));
    sheet->toFront (true);
}

void SynthyBoyEditor::closeContentSheet()
{
    // a button of the sheet may be what called us: the sheet is hidden now and deleted on the next timer tick
    if (sheet != nullptr)
    {
        sheet->setVisible (false);
        retiredSheets.push_back (std::move (sheet));
        sheet.reset();
        repaint();
    }
}

bool SynthyBoyEditor::checkMissingContent()
{
    MissingFilesModel model (ctx);
    if (model.empty())
        return false;
    auto missing = std::make_unique<MissingFilesSheet> (ctx);
    missing->onFinished = [this] { closeContentSheet(); };
    showSheet (std::move (missing));
    return true;
}

VitalFlowResult SynthyBoyEditor::importVitalFile (const juce::File& file)
{
    VitalFlowResult result = synthyboy::ui::importVitalFile (ctx, file, ctx.view->oscTab + 1, ctx.view->lfoTab < 10 ? ctx.view->lfoTab + 1 : 0);
    auto report = std::make_unique<ImportReportSheet> (ctx, result);
    report->onFinished = [this] { closeContentSheet(); };
    showSheet (std::move (report));
    return result;
}

void SynthyBoyEditor::importVitalFromMenu()
{
    FileRequest request;
    request.title = "Import a Vital file";
    request.wildcards = "*.vital;*.vitalbank;*.vitaltable;*.vitallfo";
    request.purpose = "vital";
    host.requestFile (request, [safe = juce::Component::SafePointer<SynthyBoyEditor> (this)] (const juce::File& file)
    {
        if (safe != nullptr)
            safe->importVitalFile (file);
    });
}

SerumFlowResult SynthyBoyEditor::importSerumFile (const juce::File& file)
{
    SerumFlowResult result = synthyboy::ui::importSerumFile (ctx, file, ctx.view->lfoTab < 10 ? ctx.view->lfoTab + 1 : 0);
    auto report = std::make_unique<ImportReportSheet> (ctx, result);
    report->onFinished = [this] { closeContentSheet(); };
    showSheet (std::move (report));
    return result;
}

void SynthyBoyEditor::importSerumFromMenu()
{
    FileRequest request;
    request.title = "Import a Serum 2 file";
    request.wildcards = "*.SerumPreset;*.SerumFX;*.SerumFXRack;*.XferShape;*.XferPath;*.XferClip;*.XferArp;*.SerumStyle;*.XferFilterPZSingle;*.XferFilterPZMulti";
    request.purpose = "serum";
    host.requestFile (request, [safe = juce::Component::SafePointer<SynthyBoyEditor> (this)] (const juce::File& file)
    {
        if (safe != nullptr)
            safe->importSerumFile (file);
    });
}

FlowResult SynthyBoyEditor::renderOscWarpFromMenu (int osc)
{
    const int target = osc >= 1 && osc <= 3 ? osc : juce::jlimit (1, 3, ctx.view->oscTab + 1);
    const FlowResult result = synthyboy::ui::renderOscWarp (ctx, target);
    auto message = std::make_unique<MessageSheet> (ctx, "Render OSC Warp", result.message);
    message->onFinished = [this] { closeContentSheet(); };
    showSheet (std::move (message));
    return result;
}

FlowResult SynthyBoyEditor::resampleFromMenu (int target)
{
    const FlowResult result = synthyboy::ui::resampleToWavetable (ctx, target);
    auto message = std::make_unique<MessageSheet> (ctx, "Resample", result.message);
    message->onFinished = [this] { closeContentSheet(); };
    showSheet (std::move (message));
    return result;
}

LastNoteSheet* SynthyBoyEditor::showLastNoteSheet()
{
    auto note = std::make_unique<LastNoteSheet> (ctx);
    auto* raw = note.get();
    note->onFinished = [this] { closeContentSheet(); };
    showSheet (std::move (note));
    return raw;
}

void SynthyBoyEditor::showPresetMenu()
{
    const auto presets = host.presetList();
    if (presets.empty())
        return;

    // Item ids: presetBase + list index (juce menu ids must be non-zero). Categories become submenus in order of first appearance
    // (the list is already sorted by category, then name); entries without a category (Init) sit at the top level.
    constexpr int presetBase = 1000;
    const int active = host.currentPresetIndex();
    juce::PopupMenu menu;
    std::vector<std::pair<std::string, juce::PopupMenu>> categories;
    bool anyTopLevel = false;

    for (const auto& p : presets)
    {
        const bool ticked = p.index == active;
        if (p.category.empty())
        {
            menu.addItem (presetBase + p.index, p.name, true, ticked);
            anyTopLevel = true;
            continue;
        }
        auto it = std::find_if (categories.begin(), categories.end(), [&] (const auto& c) { return c.first == p.category; });
        if (it == categories.end())
        {
            categories.emplace_back (p.category, juce::PopupMenu());
            it = categories.end() - 1;
        }
        it->second.addItem (presetBase + p.index, p.name, true, ticked);
    }
    if (anyTopLevel && ! categories.empty())
        menu.addSeparator();
    for (auto& c : categories)
    {
        // a ticked item inside a submenu: tick the submenu title as well, so the current category is visible when closed
        bool hasActive = false;
        for (const auto& p : presets)
            hasActive = hasActive || (p.index == active && p.category == c.first);
        menu.addSubMenu (c.first, c.second, true, nullptr, hasActive);
    }

    menu.showMenuAsync (popupOptions (top.get()),
                        [safe = juce::Component::SafePointer<SynthyBoyEditor> (this)] (int result)
                        {
                            if (safe == nullptr || result < presetBase)
                                return;
                            safe->host.selectPreset (result - presetBase);
                        });
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
    menu.addItem (60, "Import Vital file...", host.canRequestFile() && host.contentLibrary() != nullptr);
    menu.addItem (62, "Import Serum 2 file...", host.canRequestFile() && host.contentLibrary() != nullptr);
    menu.addItem (61, "Missing files...", host.contentLibrary() != nullptr);
    // Rendering (Serum's main menu, manual p.324) and the last note as audio (S01-09)
    juce::PopupMenu rendering, resampleMenu;
    rendering.addItem (70, "Render OSC Warp (current oscillator)", host.contentLibrary() != nullptr);
    resampleMenu.addItem (71, "Osc A", host.contentLibrary() != nullptr);
    resampleMenu.addItem (72, "Osc B", host.contentLibrary() != nullptr);
    resampleMenu.addItem (73, "Osc C", host.contentLibrary() != nullptr);
    resampleMenu.addItem (74, "Osc A + Osc B", host.contentLibrary() != nullptr);
    rendering.addSubMenu ("Resample to", resampleMenu, host.contentLibrary() != nullptr);
    rendering.addSeparator();
    rendering.addItem (75, "Last note as audio...");
    menu.addSubMenu ("Rendering", rendering);
    menu.addSeparator();
    menu.addItem (2, uiTheme.isDark() ? "Light theme" : "Dark theme");
    juce::PopupMenu sizes;
    const float steps[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (int i = 0; i < 5; ++i)
        sizes.addItem (20 + i, juce::String (juce::roundToInt (steps[i] * 100.0f)) + " %", true, std::abs (uiTheme.scale() - steps[i]) < 0.01f);
    menu.addSubMenu ("UI size", sizes);

    menu.showMenuAsync (popupOptions (&top->menu),
                        [safe = juce::Component::SafePointer<SynthyBoyEditor> (this), steps] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == 60)
                                safe->importVitalFromMenu();
                            else if (result == 62)
                                safe->importSerumFromMenu();
                            else if (result == 61)
                                safe->checkMissingContent();
                            else if (result == 70)
                                safe->renderOscWarpFromMenu();
                            else if (result >= 71 && result <= 73)
                                safe->resampleFromMenu (result - 70);
                            else if (result == 74)
                                safe->resampleFromMenu (0);
                            else if (result == 75)
                                safe->showLastNoteSheet();
                            else if (result == 10)
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
