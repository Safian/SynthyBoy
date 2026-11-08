// SynthyBoy - UI tests of the gesture gap closure (docs/ui-design/UI-GAPS-REPORT.md): module sheet, oscillator chip row, OCT / SEM, grouped
// warp list, sample view (zoom / pan, slices, threshold line, loop pair, loupe), wavetable view and editor gestures, LFO shape editor
// tools, envelope tabs, modulation, global and effect chain gestures. Headless Components driven by synthesised mouse events.
// Screenshots of the changed screens go to /tmp/ui-shots/gaps/ (phone 390x844, tablet 1024x768).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "ui/ModuleSheet.h"
#include "ui/panels/OscTabsPanel.h"
#include "ui/panels/OscWaveView.h"
#include "ui/panels/TransposeControl.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInitGaps = new juce::ScopedJuceInitialiser_GUI();

juce::File shotDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_UI_SHOTS_DIR", {});
    return juce::File (env.isNotEmpty() ? env : juce::String ("/tmp/ui-shots")).getChildFile ("gaps");
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    if (! image.isValid())
        return false;
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}

struct Rig
{
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base { proc.createEditor() };
    SynthyBoyEditor* editor = dynamic_cast<SynthyBoyEditor*> (base.get());

    explicit Rig (Page page = Page::osc, int w = 390, int h = 844)
    {
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
        editor->setSourceBarOpen (false);
        editor->setSize (w, h);
        editor->panelHost().setPage (page);
        editor->pageTabs().select (page, false);
    }

    void setParam (const char* key, float plain)
    {
        auto* p = proc.parameters().getParameter (key);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }
    float param (const char* key) { return proc.parameters().getRawParameterValue (key)->load(); }

    template <typename T>
    T* find (juce::Component& c, const std::function<bool (T&)>& pred = {})
    {
        if (auto* t = dynamic_cast<T*> (&c))
            if (! pred || pred (*t))
                return t;
        for (auto* child : c.getChildren())
            if (auto* t = find<T> (*child, pred))
                return t;
        return nullptr;
    }
    template <typename T>
    T* find (const std::function<bool (T&)>& pred = {})
    {
        return find<T> (*editor, pred);
    }

    OscTabsPanel* oscTabs() { return find<OscTabsPanel>(); }
    OscWaveView* oscView (int osc)
    {
        return find<OscWaveView> ([osc] (OscWaveView& v) { return v.oscIndex() == osc; });
    }
    Panel* panelTitled (const juce::String& title)
    {
        for (auto* p : editor->panelHost().visiblePanels())
            if (p->title() == title)
                return p;
        return nullptr;
    }
    Panel* anyPanelTitled (const juce::String& title)
    {
        for (const auto& p : editor->panelHost().allPanels())
            if (p->title() == title)
                return p.get();
        return nullptr;
    }

    void shot (const juce::String& name, int w, int h, bool whole = false, Page page = Page::osc)
    {
        editor->setSize (w, h);
        editor->panelHost().setPage (page);
        if (whole && w < 1000)
            editor->setSize (w, h + editor->panelHost().contentHeight());
        const auto file = shotDir().getChildFile (name + ".png");
        CHECK (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), file));
        std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
        editor->setSize (w, h);
        editor->panelHost().setPage (page);
    }
};

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged, int clicks = 1,
                        juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), downPos,
                             juce::Time::getCurrentTime(), clicks, dragged);
}

void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
{
    c.mouseDown (mouse (c, from, from, false, 1, mods));
    c.mouseDrag (mouse (c, from + (to - from) * 0.5f, from, true, 1, mods));
    c.mouseDrag (mouse (c, to, from, true, 1, mods));
    c.mouseUp (mouse (c, to, from, true, 1, mods));
}

void tap (juce::Component& c, juce::Point<float> p)
{
    c.mouseDown (mouse (c, p, p, false));
    c.mouseUp (mouse (c, p, p, false));
}

void doubleTap (juce::Component& c, juce::Point<float> p)
{
    c.mouseDown (mouse (c, p, p, false, 1));
    c.mouseUp (mouse (c, p, p, false, 1));
    c.mouseDown (mouse (c, p, p, false, 2));
    c.mouseDoubleClick (mouse (c, p, p, false, 2));
    c.mouseUp (mouse (c, p, p, false, 2));
}

bool approx (float a, float b, float tol = 0.01f)
{
    return std::abs (a - b) <= tol;
}

int desktopComponents()
{
    return juce::Desktop::getInstance().getNumComponents();
}

void dismissCallOuts()
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = desktop.getNumComponents(); --i >= 0;)
        if (auto* box = dynamic_cast<juce::CallOutBox*> (desktop.getComponent (i)))
            box->dismiss();
}
} // namespace

// ---- module sheet -------------------------------------------------------------------------------------------------------------------

TEST (uig_module_ops_swap_copy_paste_init)
{
    Rig rig;
    auto& ctx = rig.editor->context();
    rig.setParam ("osc1.tune", 12.5f);
    rig.setParam ("osc2.tune", -30.0f);
    rig.setParam ("osc1.level", -3.0f);

    CHECK (module_ops::isModule ("osc1") && module_ops::isModule ("rlfo4") && module_ops::isModule ("lfo10") && module_ops::isModule ("filter2"));
    CHECK (! module_ops::isModule ("fxfilter") && ! module_ops::isModule ("osc4") && ! module_ops::isModule ("noise"));
    CHECK (module_ops::displayName ("filter2") == "FILTER 2" && module_ops::displayName ("rlfo3") == "RANDOM LFO 3");
    CHECK_EQ (module_ops::siblings ("env3").size(), static_cast<size_t> (5));
    CHECK_EQ (module_ops::siblings ("lfo1").size(), static_cast<size_t> (9));

    CHECK (module_ops::swap (ctx, "osc1", "osc2"));
    CHECK (approx (rig.param ("osc1.tune"), -30.0f) && approx (rig.param ("osc2.tune"), 12.5f));
    CHECK (! module_ops::swap (ctx, "osc1", "filter1")); // not the same kind
    CHECK (! module_ops::swap (ctx, "osc1", "osc1"));

    CHECK (module_ops::copy (ctx, "osc2", false));
    CHECK (module_ops::paste (ctx, "osc3", false));
    CHECK (approx (rig.param ("osc3.tune"), 12.5f));
    CHECK (! module_ops::paste (ctx, "filter1", false)); // wrong kind
    CHECK (! module_ops::paste (ctx, "osc2", false));    // onto itself

    CHECK (module_ops::copyOnto (ctx, "osc1", "osc3", false)); // does not touch the clipboard
    CHECK (approx (rig.param ("osc3.tune"), -30.0f));
    CHECK (ModuleClipboard::instance().content().prefix == "osc2");

    CHECK (module_ops::init (ctx, "osc3"));
    CHECK (approx (rig.param ("osc3.tune"), 0.0f));
}

TEST (uig_module_sheet_buttons_do_the_operations)
{
    Rig rig;
    auto& ctx = rig.editor->context();
    rig.setParam ("filter1.cutoff", 60.0f);
    rig.setParam ("filter2.cutoff", 20.0f);
    ModuleSheet sheet (ctx, "filter1");
    sheet.setSize (sheet.preferredWidth(), sheet.preferredHeight());
    CHECK (sheet.button ("Copy") != nullptr && sheet.button ("Copy with mods") != nullptr && sheet.button ("Paste with mods") != nullptr);
    CHECK (sheet.button ("Lock module") != nullptr && ! sheet.button ("Lock module")->isEnabled()); // GAP: no key
    CHECK (sheet.swapButton ("filter2") != nullptr && sheet.swapButton ("filter1") == nullptr);
    for (auto* b : { sheet.button ("Copy"), sheet.button ("Init"), sheet.swapButton ("filter2") })
        CHECK (b != nullptr && b->getHeight() >= rig.editor->theme().touch() && b->getWidth() >= rig.editor->theme().touch());

    sheet.swapButton ("filter2")->onClick();
    CHECK (approx (rig.param ("filter1.cutoff"), 20.0f, 0.5f) && approx (rig.param ("filter2.cutoff"), 60.0f, 0.5f));
    sheet.button ("Init")->onClick();
    CHECK (rig.param ("filter1.cutoff") != 20.0f);
    CHECK (approx (rig.param ("filter1.cutoff"), ParamValues::defaults().plain[static_cast<size_t> (resolveKey ("filter1.cutoff").index)], 0.5f));

    sheet.button ("Copy")->onClick();
    CHECK (ModuleClipboard::instance().content().prefix == "filter1");
    ModuleSheet other (ctx, "filter2");
    CHECK (other.button ("Paste")->isEnabled());
    other.button ("Paste")->onClick();
    CHECK (approx (rig.param ("filter2.cutoff"), rig.param ("filter1.cutoff"), 0.5f));
    ModuleSheet oscSheet (ctx, "osc1");
    CHECK (! oscSheet.button ("Paste")->isEnabled()); // a filter on the clipboard cannot go onto an oscillator
}

TEST (uig_module_header_long_press_opens_the_sheet_and_dot_button_is_the_alternative)
{
    Rig rig (Page::flt);
    auto* panel = rig.panelTitled ("Filter 1");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    CHECK (panel->modulePrefix() == "filter1");
    CHECK (panel->moduleButton() != nullptr);
    CHECK (rig.panelTitled ("FX Filter")->moduleButton() == nullptr); // not a module
    CHECK (panel->moduleButton()->getWidth() >= rig.editor->theme().touch() && panel->moduleButton()->getHeight() >= rig.editor->theme().touch());

    // a press on the empty header arms the long press; moving away cancels it
    const auto header = juce::Point<float> (static_cast<float> (panel->getWidth()) * 0.5f, 6.0f);
    panel->mouseDown (mouse (*panel, header, header, false));
    CHECK (panel->headerLongPress().pending());
    panel->mouseDrag (mouse (*panel, header + juce::Point<float> (30.0f, 0.0f), header, true));
    CHECK (! panel->headerLongPress().pending());
    panel->mouseUp (mouse (*panel, header, header, true));

    const int before = desktopComponents();
    panel->mouseDown (mouse (*panel, header, header, false));
    panel->headerLongPress().fire(); // the 400 ms timer
    CHECK (desktopComponents() == before + 1);
    dismissCallOuts();
    panel->mouseUp (mouse (*panel, header, header, false));

    // the "..." button opens the same sheet
    const int before2 = desktopComponents();
    static_cast<juce::Button*> (panel->moduleButton())->onClick();
    CHECK (desktopComponents() == before2 + 1);
    dismissCallOuts();

    // a press below the header does not arm it
    const auto body = juce::Point<float> (static_cast<float> (panel->getWidth()) * 0.5f, static_cast<float> (panel->getHeight()) - 3.0f);
    panel->mouseDown (mouse (*panel, body, body, false));
    CHECK (! panel->headerLongPress().pending());
    panel->mouseUp (mouse (*panel, body, body, false));
}

TEST (uig_module_header_drag_onto_another_header_swaps_alt_copies)
{
    Rig rig (Page::flt, 1280, 900); // wide layout: both filters are tiles
    auto* f1 = rig.panelTitled ("Filter 1");
    auto* f2 = rig.panelTitled ("Filter 2");
    CHECK (f1 != nullptr && f2 != nullptr);
    if (f1 == nullptr || f2 == nullptr)
        return;
    rig.setParam ("filter1.cutoff", 70.0f);
    rig.setParam ("filter2.cutoff", 30.0f);
    const auto target = f2->getBounds().getCentre();
    CHECK (f1->dropModuleOn (target, false, false));
    CHECK (approx (rig.param ("filter1.cutoff"), 30.0f, 0.5f) && approx (rig.param ("filter2.cutoff"), 70.0f, 0.5f));

    // Alt = copy: filter 1 (30) goes onto filter 2, filter 1 stays
    CHECK (f1->dropModuleOn (target, true, false));
    CHECK (approx (rig.param ("filter2.cutoff"), 30.0f, 0.5f) && approx (rig.param ("filter1.cutoff"), 30.0f, 0.5f));
    CHECK (! f1->dropModuleOn ({ -50, -50 }, false, false)); // dropped on nothing

    // the real mouse path: a header drag with the mouse events
    rig.setParam ("filter1.cutoff", 90.0f);
    const auto from = juce::Point<float> (static_cast<float> (f1->getWidth()) * 0.5f, 6.0f);
    const auto to = from + (f2->getBounds().getCentre() - f1->getBounds().getCentre()).toFloat();
    drag (*f1, from, to);
    CHECK (approx (rig.param ("filter2.cutoff"), 90.0f, 0.5f));
}

// ---- oscillator chip row --------------------------------------------------------------------------------------------------------------

TEST (uig_osc_chips_select_swipe_power_swap)
{
    Rig rig;
    auto* tabs = rig.oscTabs();
    CHECK (tabs != nullptr);
    if (tabs == nullptr)
        return;
    CHECK (rig.panelTitled ("Oscillators") != nullptr);
    CHECK (rig.panelTitled ("Oscillator 1") != nullptr);
    CHECK (rig.panelTitled ("Oscillator 2") == nullptr && rig.panelTitled ("Oscillator 3") == nullptr);

    // a real tap on the chip of OSC 2
    auto* chip = tabs->getComponentAt (tabs->chipBounds (1).getCentre());
    CHECK (chip != nullptr && chip != tabs);
    if (chip == nullptr)
        return;
    tap (*chip, chip->getLocalBounds().getCentre().toFloat());
    CHECK_EQ (tabs->selectedTab(), 1);
    CHECK (rig.panelTitled ("Oscillator 2") != nullptr && rig.panelTitled ("Oscillator 1") == nullptr);

    // swipe on the chip row: a horizontal drag that is not dropped on a chip
    const auto c0 = chip->getLocalBounds().getCentre().toFloat();
    drag (*chip, c0, c0 + juce::Point<float> (-80.0f, 0.0f));
    CHECK_EQ (tabs->selectedTab(), 2); // swipe left = the next oscillator
    tabs->stepTab (+1);
    CHECK_EQ (tabs->selectedTab(), 2); // stays at the end
    tabs->stepTab (-1);
    CHECK_EQ (tabs->selectedTab(), 1);

    // every chip and power dot is a 44 point target
    for (int i = 0; i < kNumOscTabs; ++i)
    {
        CHECK (tabs->chipBounds (i).getWidth() >= rig.editor->theme().touch() && tabs->chipBounds (i).getHeight() >= rig.editor->theme().touch());
        CHECK (tabs->powerBounds (i).getWidth() >= rig.editor->theme().touch() && tabs->powerBounds (i).getHeight() >= rig.editor->theme().touch());
    }

    // the power dot toggles oscN.on and the whole panel dims
    rig.setParam ("osc2.on", 1.0f);
    CHECK_EQ (rig.param ("osc2.on"), 1.0f);
    auto* dot = tabs->getComponentAt (tabs->powerBounds (1).getCentre());
    CHECK (dot != nullptr && dot != tabs);
    tap (*dot, dot->getLocalBounds().getCentre().toFloat());
    CHECK_EQ (rig.param ("osc2.on"), 0.0f);
    CHECK (approx (rig.panelTitled ("Oscillator 2")->getAlpha(), 0.4f));
    tap (*dot, dot->getLocalBounds().getCentre().toFloat());
    CHECK_EQ (rig.param ("osc2.on"), 1.0f);
    CHECK (approx (rig.panelTitled ("Oscillator 2")->getAlpha(), 1.0f));

    // drag a chip onto another chip: swap; with Alt: copy
    rig.setParam ("osc1.tune", 11.0f);
    rig.setParam ("osc3.tune", -22.0f);
    CHECK (tabs->dragEnded (0, 2, false, false));
    CHECK (approx (rig.param ("osc1.tune"), -22.0f) && approx (rig.param ("osc3.tune"), 11.0f));
    CHECK (tabs->dragEnded (0, 2, true, false));
    CHECK (approx (rig.param ("osc3.tune"), -22.0f) && approx (rig.param ("osc1.tune"), -22.0f));
    CHECK (! tabs->dragEnded (1, 1, false, false));

    // a long press on a chip opens the module sheet
    const int before = desktopComponents();
    tabs->chipLongPressed (2);
    CHECK (desktopComponents() == before + 1);
    dismissCallOuts();
}

TEST (uig_osc_view_two_finger_swipe_switches_oscillator)
{
    Rig rig;
    auto* view = rig.oscView (1);
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    TouchTracker::Gesture swipe;
    swipe.kind = TouchTracker::Kind::twoFingerSwipe;
    swipe.dx = -120.0f;
    view->handleTouchGesture (swipe);
    CHECK_EQ (rig.editor->context().view->oscTab, 1);
    CHECK (rig.panelTitled ("Oscillator 2") != nullptr);
    swipe.dx = 120.0f;
    view->handleTouchGesture (swipe);
    CHECK_EQ (rig.editor->context().view->oscTab, 0);
    view->handleTouchGesture (swipe); // stays at the first
    CHECK_EQ (rig.editor->context().view->oscTab, 0);
}

TEST (uig_osc_wavetable_name_swipe_steps_the_table)
{
    Rig rig;
    auto* view = rig.oscView (1);
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    rig.setParam ("osc1.wavetable", 3.0f);
    const float y = 8.0f; // the name strip
    const float x = static_cast<float> (view->getWidth()) * 0.6f;
    drag (*view, { x, y }, { x - 90.0f, y });
    CHECK_EQ (rig.param ("osc1.wavetable"), 4.0f); // swipe left = next
    drag (*view, { x, y }, { x + 90.0f, y });
    CHECK_EQ (rig.param ("osc1.wavetable"), 3.0f);
    drag (*view, { x, y }, { x + 20.0f, y }); // too short: nothing
    CHECK_EQ (rig.param ("osc1.wavetable"), 3.0f);
}

// ---- OCT / SEM, warp list -----------------------------------------------------------------------------------------------------------

TEST (uig_transpose_oct_sem_control)
{
    Rig rig;
    auto* panel = rig.panelTitled ("Oscillator 1");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    panel->setMoreExpanded (true);
    auto* tc = rig.find<TransposeControl>();
    CHECK (tc != nullptr);
    if (tc == nullptr)
        return;
    CHECK (tc->key() == "osc1.transpose");
    CHECK (tc->isVisible() && tc->getWidth() >= tc->getParentComponent()->getWidth() - 2 * rig.editor->theme().gap() - 1);

    rig.setParam ("osc1.transpose", 7.0f);
    CHECK_EQ (tc->octave(), 1);   // 7 = 1 * 12 - 5
    CHECK_EQ (tc->semitone(), -5);
    tap (*tc, tc->octBounds (0).getCentre().toFloat()); // OCT -2 keeps SEM
    CHECK (approx (rig.param ("osc1.transpose"), -29.0f));
    tap (*tc, tc->octBounds (3).getCentre().toFloat()); // OCT +1
    CHECK_EQ (rig.param ("osc1.transpose"), 7.0f);
    tap (*tc, tc->semPlusBounds().getCentre().toFloat());
    CHECK_EQ (rig.param ("osc1.transpose"), 8.0f);
    tap (*tc, tc->semMinusBounds().getCentre().toFloat());
    tap (*tc, tc->semMinusBounds().getCentre().toFloat());
    CHECK_EQ (rig.param ("osc1.transpose"), 6.0f);

    // the SEM wheel: a vertical drag, 14 points per semitone, up = higher
    const auto w = tc->semWheelBounds().getCentre().toFloat();
    drag (*tc, w, w + juce::Point<float> (0.0f, -42.0f));
    CHECK_EQ (rig.param ("osc1.transpose"), 9.0f);
    drag (*tc, w, w + juce::Point<float> (0.0f, 1000.0f)); // clamped at -48
    CHECK (approx (rig.param ("osc1.transpose"), -48.0f));

    // every part is a 44 point target
    for (int i = 0; i < TransposeControl::kNumOctSegments; ++i)
        CHECK (tc->octBounds (i).getWidth() >= rig.editor->theme().touch() && tc->octBounds (i).getHeight() >= rig.editor->theme().touch());
    CHECK (tc->semMinusBounds().getWidth() >= rig.editor->theme().touch() && tc->semPlusBounds().getHeight() >= rig.editor->theme().touch());
}

TEST (uig_warp_type_list_is_grouped)
{
    Rig rig;
    auto* panel = rig.panelTitled ("Oscillator 1");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    panel->setMoreExpanded (true);
    auto* warp = rig.find<ParamChoice> ([] (ParamChoice& c) { return c.key() == "osc1.warp.type"; });
    auto* morph = rig.find<ParamChoice> ([] (ParamChoice& c) { return c.key() == "osc1.morph.type"; });
    CHECK (warp != nullptr && morph != nullptr);
    if (warp == nullptr || morph == nullptr)
        return;
    CHECK (! warp->choiceGroups().empty());
    CHECK (morph->choiceGroups().empty()); // the morph list is flat

    auto menu = warp->buildOptionMenu();
    juce::PopupMenu::MenuItemIterator it (menu);
    int groups = 0, disabledGroups = 0, subMenus = 0, tickedSub = 0;
    while (it.next())
    {
        auto& item = it.getItem();
        ++groups;
        if (! item.isEnabled)
            ++disabledGroups;
        if (item.subMenu != nullptr)
        {
            ++subMenus;
            if (item.isTicked)
                ++tickedSub; // the group of the current type is marked
        }
    }
    CHECK_EQ (groups, 8);
    CHECK_EQ (disabledGroups, 4); // Filter / Distortion / PD / AM have no mode in this engine
    CHECK_EQ (subMenus, 4);       // Basic / Alt / FM / RM
    CHECK_EQ (tickedSub, 1);      // warp type None sits in Basic

    // <  > still step through the flat order
    rig.setParam ("osc1.warp.type", 2.0f);
    warp->step (+1);
    CHECK_EQ (rig.param ("osc1.warp.type"), 3.0f);
}

TEST (uig_zz_shutdown)
{
    delete juceInitGaps;
    juceInitGaps = nullptr;
    CHECK (true);
}
