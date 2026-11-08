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
#include "ui/panels/WavetableEditorSheet.h"
#include "ui/panels/WavetablePanel.h"
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

// ---- sample view ----------------------------------------------------------------------------------------------------------------------

namespace
{
void giveSample (Rig& rig, int osc, std::vector<float> slices)
{
    auto c = rig.proc.content();
    ContentSample s;
    s.osc = osc;
    s.ref.pack = "test";
    s.ref.file = "loop.wav";
    s.frames = 44100;
    s.channels = 1;
    s.sampleRate = 44100;
    s.slices = std::move (slices);
    c.samples.clear();
    c.samples.push_back (s);
    CHECK (rig.proc.setContent (c));
}

std::vector<float> storedSlices (Rig& rig, int osc)
{
    for (const auto& s : rig.proc.content().samples)
        if (s.osc == osc)
            return s.slices;
    return {};
}
} // namespace

TEST (uig_sample_view_zoom_pan_buttons_pinch_wheel)
{
    Rig rig;
    rig.setParam ("osc1.type", 1.0f);
    auto* view = rig.oscView (1);
    CHECK (view != nullptr && view->isSampleType());
    if (view == nullptr)
        return;
    auto& vp = view->sampleViewport();
    CHECK (vp.isFit());
    CHECK (view->zoomInButton().isVisible() && view->zoomOutButton().isVisible() && view->fitButton().isVisible());
    for (auto* b : { &view->zoomInButton(), &view->zoomOutButton(), &view->fitButton() })
        CHECK (b->getWidth() >= rig.editor->theme().touch() && b->getHeight() >= rig.editor->theme().touch());

    view->zoomInButton().onClick();
    CHECK (approx (static_cast<float> (vp.span), 0.5f, 1e-4f));
    view->zoomInButton().onClick();
    CHECK (approx (static_cast<float> (vp.span), 0.25f, 1e-4f));
    view->zoomOutButton().onClick();
    CHECK (approx (static_cast<float> (vp.span), 0.5f, 1e-4f));
    view->fitButton().onClick();
    CHECK (vp.isFit());

    // pinch about the right edge keeps that point under the fingers
    const auto area = view->waveArea();
    const float x = area.getX() + area.getWidth() * 0.75f;
    const float before = view->xToSampleNorm (x);
    TouchTracker::Gesture pinch;
    pinch.kind = TouchTracker::Kind::pinch;
    pinch.scale = 4.0f;
    pinch.centre = { x, area.getCentreY() };
    view->handleTouchGesture (pinch);
    CHECK (approx (static_cast<float> (vp.span), 0.25f, 1e-4f));
    CHECK (approx (view->xToSampleNorm (x), before, 1e-3f));

    // two-finger pan to the left reveals later parts
    const double startBefore = vp.start;
    TouchTracker::Gesture pan;
    pan.kind = TouchTracker::Kind::pan;
    pan.dx = -static_cast<float> (area.getWidth()) * 0.5f;
    view->handleTouchGesture (pan);
    CHECK (vp.start > startBefore);

    // the trackpad pinch and the wheel
    view->fitSample();
    view->mouseMagnify (mouse (*view, { x, area.getCentreY() }, { x, area.getCentreY() }, false), 2.0f);
    CHECK (approx (static_cast<float> (vp.span), 0.5f, 1e-4f));
    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f;
    wheel.deltaY = -0.5f;
    view->mouseWheelMove (mouse (*view, { x, area.getCentreY() }, { x, area.getCentreY() }, false), wheel);
    CHECK (vp.start > 0.0); // plain wheel pans
    view->mouseWheelMove (mouse (*view, { x, area.getCentreY() }, { x, area.getCentreY() }, false, 1,
                                 juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)), wheel);
    CHECK (vp.span > 0.5); // ctrl + wheel down zooms out
}

TEST (uig_sample_view_markers_loupe_loop_pair)
{
    Rig rig;
    rig.setParam ("osc1.type", 1.0f);
    rig.setParam ("osc1.sample.start", 5.0f);
    rig.setParam ("osc1.sample.end", 95.0f);
    rig.setParam ("osc1.sample.loop_start", 30.0f);
    rig.setParam ("osc1.sample.loop_end", 60.0f);
    auto* view = rig.oscView (1);
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    const float midY = view->waveArea().getCentreY();

    // the start marker: drag with the loupe above the finger
    const float sx = view->sampleNormToX (0.05f);
    const float target = view->sampleNormToX (0.15f);
    view->mouseDown (mouse (*view, { sx, midY }, { sx, midY }, false));
    CHECK (view->isDraggingMarker());
    view->mouseDrag (mouse (*view, { target, midY }, { sx, midY }, true));
    CHECK (view->loupeShowing());
    CHECK (approx (rig.param ("osc1.sample.start"), 15.0f, 0.5f));
    view->mouseUp (mouse (*view, { target, midY }, { sx, midY }, true));
    CHECK (! view->loupeShowing());
    CHECK (! view->isDraggingMarker());

    // the loupe sits above the finger, inside the editor
    view->mouseDown (mouse (*view, { sx, midY }, { sx, midY }, false)); // the marker is at 15 %: the 44 pt tolerance still catches it
    view->mouseDrag (mouse (*view, { sx + 5.0f, midY }, { sx, midY }, true));
    CHECK (view->loupeShowing());
    const auto finger = rig.editor->getLocalPoint (view, juce::Point<float> (sx + 5.0f, midY).toInt());
    juce::Rectangle<int> loupeBounds;
    if (auto* loupeComp = rig.find<Loupe>())
        loupeBounds = loupeComp->loupeBounds();
    CHECK (! loupeBounds.isEmpty());
    CHECK (loupeBounds.getBottom() <= finger.y);
    CHECK (rig.editor->getLocalBounds().contains (loupeBounds));
    view->mouseUp (mouse (*view, { sx + 5.0f, midY }, { sx, midY }, true));

    // drag between the loop markers: both move, the length stays
    const float between = view->sampleNormToX (0.45f);
    drag (*view, { between, midY }, { between + view->waveArea().getWidth() * 0.2f, midY });
    CHECK (approx (rig.param ("osc1.sample.loop_start"), 50.0f, 1.0f));
    CHECK (approx (rig.param ("osc1.sample.loop_end"), 80.0f, 1.0f));
    // ... and stop at the sample end with the length kept
    drag (*view, { view->sampleNormToX (0.65f), midY }, { view->waveArea().getRight(), midY });
    CHECK (approx (rig.param ("osc1.sample.loop_end") - rig.param ("osc1.sample.loop_start"), 30.0f, 1.0f));
    CHECK (rig.param ("osc1.sample.loop_end") <= 100.0f);

    // double tap between the markers: the loop is the start / end again
    rig.setParam ("osc1.sample.loop_start", 40.0f);
    rig.setParam ("osc1.sample.loop_end", 80.0f);
    doubleTap (*view, { view->sampleNormToX (0.6f), midY });
    CHECK (approx (rig.param ("osc1.sample.loop_start"), rig.param ("osc1.sample.start"), 0.5f));
    CHECK (approx (rig.param ("osc1.sample.loop_end"), rig.param ("osc1.sample.end"), 0.5f));
    // double tap outside the loop resets start / end to the full range (existing gesture)
    doubleTap (*view, { view->sampleNormToX (0.97f), midY });
    CHECK (approx (rig.param ("osc1.sample.start"), 0.0f, 0.5f) && approx (rig.param ("osc1.sample.end"), 100.0f, 0.5f));

    // a second finger cancels the one-finger gesture: no marker stays grabbed
    view->mouseDown (mouse (*view, { sx, midY }, { sx, midY }, false));
    TouchTracker::Gesture pinch;
    pinch.kind = TouchTracker::Kind::pinch;
    pinch.scale = 2.0f;
    pinch.centre = { sx, midY };
    view->handleTouchGesture (pinch);
    view->mouseUp (mouse (*view, { sx, midY }, { sx, midY }, false));
    CHECK (! view->isDraggingMarker());
}

TEST (uig_sample_view_slices_manual)
{
    Rig rig;
    rig.setParam ("osc1.type", 1.0f);
    auto* view = rig.oscView (1);
    CHECK (view != nullptr);
    if (view == nullptr)
        return;

    // without a loaded sample there is no slice list to edit
    rig.setParam ("osc1.sample.slice_mode", 2.0f);
    view->refreshSliceCache();
    CHECK (! view->canEditSlices());
    CHECK (view->addSliceButton().isVisible() && ! view->addSliceButton().isEnabled());

    giveSample (rig, 1, { 0.0f, 0.25f, 0.5f });
    view->refreshSliceCache();
    CHECK (view->canEditSlices());
    CHECK_EQ (view->slices().size(), static_cast<size_t> (3));
    rig.setParam ("osc1.sample.start", 0.0f);
    rig.setParam ("osc1.sample.end", 100.0f);
    rig.setParam ("osc1.sample.loop_start", 70.0f);
    rig.setParam ("osc1.sample.loop_end", 90.0f);
    const float midY = view->waveArea().getCentreY();

    // drag the slice at 25 % to 40 %: the content is written once, at the end
    const float x0 = view->sampleNormToX (0.25f);
    const float x1 = view->sampleNormToX (0.40f);
    view->mouseDown (mouse (*view, { x0, midY }, { x0, midY }, false));
    view->mouseDrag (mouse (*view, { x1, midY }, { x0, midY }, true));
    CHECK (view->loupeShowing());
    CHECK (approx (storedSlices (rig, 1)[1], 0.25f, 1e-4f)); // not committed yet
    view->mouseUp (mouse (*view, { x1, midY }, { x0, midY }, true));
    const auto moved = storedSlices (rig, 1);
    CHECK_EQ (moved.size(), static_cast<size_t> (3));
    CHECK (approx (moved[1], 0.40f, 0.01f));
    CHECK (moved[0] < moved[1] && moved[1] < moved[2]);

    // long-press the empty waveform adds a slice, long-press a handle removes it
    const float ax = view->sampleNormToX (0.2f);
    view->mouseDown (mouse (*view, { ax, midY }, { ax, midY }, false));
    CHECK (view->longPress().pending());
    view->longPress().fire();
    view->mouseUp (mouse (*view, { ax, midY }, { ax, midY }, false));
    CHECK_EQ (storedSlices (rig, 1).size(), static_cast<size_t> (4));
    const float hx = view->sampleNormToX (0.5f);
    view->mouseDown (mouse (*view, { hx, midY }, { hx, midY }, false));
    view->longPress().fire();
    view->mouseUp (mouse (*view, { hx, midY }, { hx, midY }, false));
    const auto afterRemove = storedSlices (rig, 1);
    CHECK_EQ (afterRemove.size(), static_cast<size_t> (3));
    CHECK (std::none_of (afterRemove.begin(), afterRemove.end(), [] (float v) { return std::abs (v - 0.5f) < 0.01f; }));

    // the visible tools: + Slice adds in the middle of the view, a tap selects, - Slice removes the selection
    view->addSliceButton().onClick();
    CHECK_EQ (storedSlices (rig, 1).size(), static_cast<size_t> (4));
    const float tapX = view->sampleNormToX (0.4f);
    tap (*view, { tapX, midY });
    CHECK (view->selectedSliceIndex() >= 0);
    view->removeSliceButton().onClick();
    CHECK_EQ (storedSlices (rig, 1).size(), static_cast<size_t> (3));
    CHECK (view->addSliceButton().getWidth() >= rig.editor->theme().touch() && view->removeSliceButton().getHeight() >= rig.editor->theme().touch());

    // the slice list can never leave the validated shape (ascending, 0..1)
    const auto finalSlices = storedSlices (rig, 1);
    CHECK (std::is_sorted (finalSlices.begin(), finalSlices.end()));
}

TEST (uig_sample_view_threshold_line_auto_slices)
{
    Rig rig;
    rig.setParam ("osc1.type", 1.0f);
    rig.setParam ("osc1.sample.slice_mode", 1.0f); // Auto
    rig.setParam ("osc1.sample.slice_threshold", 50.0f);
    auto* view = rig.oscView (1);
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    CHECK (! view->addSliceButton().isVisible()); // Auto slices have no manual tools
    const auto area = view->waveArea();
    CHECK (approx (view->thresholdLineY(), area.getBottom() - 0.5f * area.getHeight(), 1.0f));

    const float x = area.getCentreX();
    const float y0 = view->thresholdLineY();
    const float y1 = area.getY() + area.getHeight() * 0.2f; // up = fewer slices = lower threshold %
    view->mouseDown (mouse (*view, { x, y0 }, { x, y0 }, false));
    CHECK (view->isDraggingMarker());
    view->mouseDrag (mouse (*view, { x, y1 }, { x, y0 }, true));
    CHECK (view->loupeShowing());
    view->mouseUp (mouse (*view, { x, y1 }, { x, y0 }, true));
    CHECK (approx (rig.param ("osc1.sample.slice_threshold"), 20.0f, 1.0f));
    CHECK (approx (view->thresholdLineY(), y1, 1.5f));
    // dragged to the bottom: 100 %
    drag (*view, { x, view->thresholdLineY() }, { x, area.getBottom() + 40.0f });
    CHECK (approx (rig.param ("osc1.sample.slice_threshold"), 100.0f, 0.5f));
}

// ---- wavetable editor and 3D view -----------------------------------------------------------------------------------------------------

namespace
{
wtedit::Wavetable makeTable (int frames)
{
    wtedit::Wavetable table ("Gaps");
    for (int k = 0; k < frames; ++k)
    {
        wtedit::Frame f;
        for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
            f[i] = 0.1f * static_cast<float> (k + 1) * std::sin (6.2831853f * static_cast<float> (i) / static_cast<float> (wtedit::kWaveformSize));
        if (k == 0)
            table.setFrame (0, f);
        else
            table.insertFrame (static_cast<size_t> (k), f);
    }
    return table;
}

float frameMarker (const wtedit::Wavetable& table, size_t frame)
{
    return table.getFrame (frame)[wtedit::kWaveformSize / 4]; // the peak of the sine: identifies the frame (0.1 * (k + 1))
}

void polar (const wtedit::Frame& f, std::array<float, wtedit::kWaveformSize>& mags, std::array<float, wtedit::kWaveformSize>& phases)
{
    wtedit::Radix2Fft::realToPolar (f.sampleData(), mags.data(), phases.data(), wtedit::kWaveformSize);
}
} // namespace

TEST (uig_wavetable_canvas_zoom_pan_loupe_nudge_two_finger)
{
    Rig rig;
    WavetableEditorSheet sheet (rig.editor->context(), makeTable (3));
    sheet.setSize (390, 844);
    auto* canvas = sheet.waveCanvasComponent();
    CHECK (canvas != nullptr && canvas->getWidth() > 100);
    if (canvas == nullptr)
        return;

    // the visible alternatives of pinch / two-finger drag: + - Fit
    for (int i = 0; i < 3; ++i)
    {
        auto* b = sheet.waveZoomButton (i);
        CHECK (b != nullptr && b->getWidth() >= rig.editor->theme().touch() && b->getHeight() >= rig.editor->theme().touch());
    }
    sheet.waveZoomButton (0)->onClick();
    CHECK (approx (static_cast<float> (sheet.waveViewport().span), 0.5f, 1e-4f));
    sheet.waveZoomButton (1)->onClick();
    CHECK (sheet.waveViewport().isFit());
    sheet.waveZoomButton (0)->onClick();
    sheet.waveZoomButton (2)->onClick();
    CHECK (sheet.waveViewport().isFit());

    // pinch and two-finger pan through the gesture handler
    TouchTracker::Gesture pinch;
    pinch.kind = TouchTracker::Kind::pinch;
    pinch.scale = 4.0f;
    pinch.centre = { static_cast<float> (canvas->getWidth()) * 0.5f, 10.0f };
    sheet.handleTouchGesture (pinch);
    CHECK (approx (static_cast<float> (sheet.waveViewport().span), 0.25f, 1e-4f));
    TouchTracker::Gesture pan;
    pan.kind = TouchTracker::Kind::pan;
    pan.dx = -static_cast<float> (canvas->getWidth()) * 0.25f;
    const double before = sheet.waveViewport().start;
    sheet.handleTouchGesture (pan);
    CHECK (sheet.waveViewport().start > before);
    sheet.fitWave();

    // drawing while zoomed in at the start: a point at the middle of the screen is at 25 % of the cycle
    sheet.zoomWave (0.0f, 2.0f); // span 0.5, start 0
    const auto original = sheet.wavetable().getFrame (0);
    const float cx = static_cast<float> (canvas->getWidth()) * 0.5f, cy = static_cast<float> (canvas->getHeight()) * 0.25f;
    canvas->mouseDown (mouse (*canvas, { cx, cy }, { cx, cy }, false));
    canvas->mouseDrag (mouse (*canvas, { cx + 2.0f, cy }, { cx, cy }, true));
    CHECK (sheet.loupeShowing());
    const auto& after = sheet.wavetable().getFrame (0);
    int firstChanged = -1;
    for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        if (std::abs (after[i] - original[i]) > 1.0e-6f)
        {
            firstChanged = static_cast<int> (i);
            break;
        }
    CHECK (firstChanged > 0);
    CHECK (std::abs (static_cast<float> (firstChanged) / static_cast<float> (wtedit::kWaveformSize) - 0.25f) < 0.07f);
    canvas->mouseUp (mouse (*canvas, { cx + 2.0f, cy }, { cx, cy }, true));
    CHECK (! sheet.loupeShowing());
    CHECK (sheet.canUndo()); // one stroke = one undo step
    sheet.performUndo();
    CHECK (approx (sheet.wavetable().getFrame (0)[100], original[100], 1e-6f) && approx (sheet.wavetable().getFrame (0)[static_cast<size_t> (firstChanged)], original[static_cast<size_t> (firstChanged)], 1e-6f));
    sheet.fitWave();

    // long-press then drag nudges, whatever tool is selected; the first touch point is taken back
    canvas->mouseDown (mouse (*canvas, { cx, cy }, { cx, cy }, false));
    CHECK (sheet.waveLongPress()->pending());
    sheet.waveLongPress()->fire();
    CHECK (sheet.waveNudgeStroke());
    bool restored = true;
    for (size_t i = 0; i < wtedit::kWaveformSize; i += 64)
        restored = restored && approx (sheet.wavetable().getFrame (0)[i], original[i], 1e-6f);
    CHECK (restored);
    canvas->mouseDrag (mouse (*canvas, { cx, cy + 40.0f }, { cx, cy }, true));
    canvas->mouseUp (mouse (*canvas, { cx, cy + 40.0f }, { cx, cy }, true));
    CHECK (! sheet.waveNudgeStroke());

    // a second finger takes the running stroke back (it was a gesture), with no undo or redo entry
    sheet.performUndo();
    sheet.performRedo();
    const bool couldUndo = sheet.canUndo();
    const bool couldRedo = sheet.canRedo();
    const auto snapshot = sheet.wavetable().getFrame (0);
    canvas->mouseDown (mouse (*canvas, { cx, cy }, { cx, cy }, false));
    CHECK (sheet.strokeInProgress());
    sheet.waveTouchInput().touchTracker().feed (TouchTracker::Phase::down, 7, { cx + 30.0f, cy }, 1000);
    canvas->mouseDrag (mouse (*canvas, { cx + 5.0f, cy }, { cx, cy }, true));
    CHECK (! sheet.strokeInProgress());
    bool same = true;
    for (size_t i = 0; i < wtedit::kWaveformSize; i += 32)
        same = same && approx (sheet.wavetable().getFrame (0)[i], snapshot[i], 1e-6f);
    CHECK (same);
    CHECK_EQ (sheet.canUndo(), couldUndo);
    CHECK_EQ (sheet.canRedo(), couldRedo);
    sheet.waveTouchInput().touchTracker().reset();
    canvas->mouseUp (mouse (*canvas, { cx + 5.0f, cy }, { cx, cy }, true));

    // two-finger tap = undo (the Undo button is the visible alternative)
    sheet.applyToolAt (wtedit::DrawingTool::FlatLine, 16, 3, 0.5f);
    const float edited = sheet.wavetable().getFrame (0)[wtedit::kWaveformSize / 5];
    TouchTracker::Gesture tapGesture;
    tapGesture.kind = TouchTracker::Kind::twoFingerTap;
    sheet.handleTouchGesture (tapGesture, false);
    CHECK (std::abs (sheet.wavetable().getFrame (0)[wtedit::kWaveformSize / 5] - edited) > 1.0e-4f);
}

TEST (uig_wavetable_fft_bin_entry_and_nudge_all_phases)
{
    Rig rig;
    WavetableEditorSheet sheet (rig.editor->context(), makeTable (1));
    sheet.setSize (390, 844);
    sheet.setMode (WavetableEditorSheet::Mode::spectrum);
    std::array<float, wtedit::kWaveformSize> mags {}, phases {};

    // build a frame with harmonics 1..6 so the phases are defined
    {
        wtedit::Frame f;
        for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
        {
            float v = 0.0f;
            for (int h = 1; h <= 6; ++h)
                v += std::sin (6.2831853f * static_cast<float> (h) * static_cast<float> (i) / static_cast<float> (wtedit::kWaveformSize)) / static_cast<float> (h);
            f[i] = v * 0.3f;
        }
        sheet.wavetable().setFrame (0, f);
    }

    CHECK (sheet.enterBinValue (3, false, "50"));
    polar (sheet.wavetable().getFrame (0), mags, phases);
    const float m3 = mags[3];
    CHECK (m3 > 0.0f);
    CHECK (sheet.canUndo()); // numeric entry is one undo step
    CHECK (sheet.enterBinValue (3, false, "100 %"));
    polar (sheet.wavetable().getFrame (0), mags, phases);
    CHECK (mags[3] > m3);
    const float m3full = mags[3];
    CHECK (! sheet.enterBinValue (3, false, "250")); // outside 0..100
    CHECK (! sheet.enterBinValue (3, false, "loud"));
    polar (sheet.wavetable().getFrame (0), mags, phases);
    CHECK (std::abs (mags[3] - m3full) < 1e-4f);

    // phase entry in degrees
    CHECK (sheet.enterBinValue (2, true, "90"));
    polar (sheet.wavetable().getFrame (0), mags, phases);
    CHECK (std::abs (phases[2] - 1.5707963f) < 0.05f);
    CHECK (! sheet.enterBinValue (2, true, "x"));

    // one phase bar alone ...
    polar (sheet.wavetable().getFrame (0), mags, phases);
    const float p1 = phases[1], p4 = phases[4], p2 = phases[2];
    sheet.applyBinEdit (2, true, p2 + 0.4f);
    polar (sheet.wavetable().getFrame (0), mags, phases);
    CHECK (std::abs (phases[2] - (p2 + 0.4f)) < 0.05f);
    CHECK (std::abs (phases[1] - p1) < 0.05f && std::abs (phases[4] - p4) < 0.05f);

    // ... and with "nudge all phases" every sounding harmonic moves by the same amount
    sheet.setNudgeAllPhases (true);
    CHECK (sheet.nudgeAllPhases());
    polar (sheet.wavetable().getFrame (0), mags, phases);
    const float q1 = phases[1], q4 = phases[4], q2 = phases[2];
    sheet.applyBinEdit (2, true, q2 + 0.4f);
    polar (sheet.wavetable().getFrame (0), mags, phases);
    CHECK (std::abs (phases[2] - (q2 + 0.4f)) < 0.05f);
    CHECK (std::abs (phases[1] - (q1 + 0.4f)) < 0.05f);
    CHECK (std::abs (phases[4] - (q4 + 0.4f)) < 0.05f);
    sheet.setNudgeAllPhases (false);

    // the real mouse path: a vertical drag in the amplitude half of the spectrum canvas, with the loupe
    auto* spec = sheet.spectrumComponent();
    CHECK (spec != nullptr);
    if (spec != nullptr)
    {
        const auto& t = rig.editor->theme();
        auto area = spec->getLocalBounds().reduced (t.ipx (2));
        area.removeFromBottom (t.touch());
        const float x = static_cast<float> (area.getX()) + static_cast<float> (area.getWidth()) * (4.5f / 64.0f); // bin 5
        const float y0 = static_cast<float> (area.getY()) + static_cast<float> (area.getHeight()) * 0.15f;
        spec->mouseDown (mouse (*spec, { x, y0 }, { x, y0 }, false));
        spec->mouseDrag (mouse (*spec, { x, y0 - 10.0f }, { x, y0 }, true));
        CHECK (sheet.loupeShowing());
        spec->mouseUp (mouse (*spec, { x, y0 - 10.0f }, { x, y0 }, true));
        CHECK (! sheet.loupeShowing());
        polar (sheet.wavetable().getFrame (0), mags, phases);
        CHECK (mags[5] > 0.5f);
    }
}

TEST (uig_wavetable_thumbnail_reorder_swipe_close)
{
    Rig rig;
    WavetableEditorSheet sheet (rig.editor->context(), makeTable (5));
    sheet.setSize (390, 844);
    auto* strip = sheet.thumbnailsComponent();
    CHECK (strip != nullptr);
    if (strip == nullptr)
        return;
    const auto& t = rig.editor->theme();
    const int step = juce::jmax (t.touch(), t.ipx (56)) + t.ipx (4);
    const auto slotX = [&] (int i) { return static_cast<float> (t.gap() + i * step + step / 2); };
    const float y = static_cast<float> (strip->getHeight()) * 0.5f;

    // a tap selects, as before
    tap (*strip, { slotX (2), y });
    CHECK_EQ (sheet.currentFrame(), static_cast<size_t> (2));

    // hold thumbnail 0, drag it to the gap after thumbnail 3: it becomes frame 3 (yellow insertion cursor while dragging)
    const float v0 = frameMarker (sheet.wavetable(), 0), v1 = frameMarker (sheet.wavetable(), 1);
    strip->mouseDown (mouse (*strip, { slotX (0), y }, { slotX (0), y }, false));
    CHECK (sheet.thumbnailLongPress()->pending());
    sheet.thumbnailLongPress()->fire();
    CHECK (sheet.thumbnailReordering());
    strip->mouseDrag (mouse (*strip, { slotX (3) + static_cast<float> (step) * 0.4f, y }, { slotX (0), y }, true));
    strip->mouseUp (mouse (*strip, { slotX (3) + static_cast<float> (step) * 0.4f, y }, { slotX (0), y }, true));
    CHECK (! sheet.thumbnailReordering());
    CHECK (approx (frameMarker (sheet.wavetable(), 3), v0, 1e-4f));
    CHECK (approx (frameMarker (sheet.wavetable(), 0), v1, 1e-4f));
    CHECK_EQ (sheet.currentFrame(), static_cast<size_t> (3)); // the moved frame stays selected
    sheet.performUndo();
    CHECK (approx (frameMarker (sheet.wavetable(), 0), v0, 1e-4f)); // one undo step

    // a quick drag without the hold does not reorder
    strip->mouseDown (mouse (*strip, { slotX (0), y }, { slotX (0), y }, false));
    strip->mouseDrag (mouse (*strip, { slotX (3), y }, { slotX (0), y }, true));
    strip->mouseUp (mouse (*strip, { slotX (3), y }, { slotX (0), y }, true));
    CHECK (approx (frameMarker (sheet.wavetable(), 0), v0, 1e-4f));

    // the menu alternative (Move frame left / right) is the same operation
    CHECK (sheet.moveFrameTo (1, 0));
    CHECK (approx (frameMarker (sheet.wavetable(), 0), v1, 1e-4f));
    CHECK (! sheet.moveFrameTo (0, 0) && ! sheet.moveFrameTo (0, 9));

    // swipe down on the header closes the sheet; a short swipe or a sideways one does not
    bool closed = false;
    sheet.onClosed = [&closed] (const wtedit::Wavetable&) { closed = true; };
    sheet.setVisible (true);
    sheet.mouseDown (mouse (sheet, { 200.0f, 6.0f }, { 200.0f, 6.0f }, false));
    sheet.mouseUp (mouse (sheet, { 204.0f, 30.0f }, { 200.0f, 6.0f }, true));
    CHECK (! closed);
    sheet.mouseDown (mouse (sheet, { 200.0f, 6.0f }, { 200.0f, 6.0f }, false));
    sheet.mouseUp (mouse (sheet, { 330.0f, 130.0f }, { 200.0f, 6.0f }, true));
    CHECK (! closed);
    sheet.mouseDown (mouse (sheet, { 200.0f, 6.0f }, { 200.0f, 6.0f }, false));
    sheet.mouseUp (mouse (sheet, { 204.0f, 140.0f }, { 200.0f, 6.0f }, true));
    CHECK (closed && ! sheet.isVisible());
    // a press far below the header never arms it
    closed = false;
    sheet.setVisible (true);
    sheet.mouseDown (mouse (sheet, { 200.0f, 400.0f }, { 200.0f, 400.0f }, false));
    sheet.mouseUp (mouse (sheet, { 204.0f, 600.0f }, { 200.0f, 400.0f }, true));
    CHECK (! closed);
}

TEST (uig_wavetable_import_runs_on_a_worker_with_a_progress_bar)
{
    Rig rig;
    WavetableEditorSheet sheet (rig.editor->context(), makeTable (1));
    sheet.setSize (390, 844);
    const auto firstMarker = frameMarker (sheet.wavetable(), 0);

    std::vector<float> audio (44100 * 2);
    for (size_t i = 0; i < audio.size(); ++i)
        audio[i] = 0.6f * std::sin (6.2831853f * 220.0f * static_cast<float> (i) / 44100.0f);
    wtedit::ImportOptions opts;
    opts.mode = wtedit::ImportMode::Fft1024;
    CHECK (sheet.importAudioAsync (audio, 44100.0, opts));
    CHECK (sheet.importRunning());
    CHECK (sheet.importProgressVisible());
    CHECK (! sheet.importAudioAsync (audio, 44100.0, opts)); // one at a time
    for (int i = 0; i < 1500 && ! sheet.importWorkerDone(); ++i)
        juce::Thread::sleep (10);
    CHECK (sheet.importWorkerDone());
    CHECK (sheet.finishImport());
    CHECK (! sheet.importRunning() && ! sheet.importProgressVisible());
    CHECK (sheet.wavetable().numFrames() > 1);
    CHECK (sheet.canUndo());
    sheet.performUndo();
    CHECK (approx (frameMarker (sheet.wavetable(), 0), firstMarker, 1e-4f));

    // cancel: the worker finishes, the result is thrown away
    CHECK (sheet.importAudioAsync (audio, 44100.0, opts));
    sheet.cancelImport();
    for (int i = 0; i < 1500 && ! sheet.importWorkerDone(); ++i)
        juce::Thread::sleep (10);
    CHECK (sheet.finishImport());
    CHECK_EQ (sheet.wavetable().numFrames(), static_cast<size_t> (1));
    CHECK (! sheet.finishImport()); // nothing left to finish
}

TEST (uig_wavetable_3d_view_tilt_pinch)
{
    Rig rig;
    WavetablePanel panel (rig.editor->context());
    panel.build();
    panel.setBounds (0, 0, 390, panel.preferredHeight (390));
    auto* view = panel.visual();
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    view->setViewMode (WavetableView::ViewMode::mode3D);
    CHECK (approx (view->tilt(), 1.0f) && approx (view->yaw(), 0.0f) && approx (view->zoom(), 1.0f));

    TouchTracker::Gesture pinch;
    pinch.kind = TouchTracker::Kind::pinch;
    pinch.scale = 1.5f;
    view->handleTouchGesture (pinch);
    CHECK (approx (view->zoom(), 1.5f));
    pinch.scale = 10.0f;
    view->handleTouchGesture (pinch);
    CHECK (approx (view->zoom(), 2.5f)); // clamped like the zoom buttons

    TouchTracker::Gesture pan;
    pan.kind = TouchTracker::Kind::pan;
    pan.dy = static_cast<float> (view->getHeight()) * 0.25f;
    pan.dx = -static_cast<float> (view->getWidth()) * 0.25f;
    view->handleTouchGesture (pan);
    CHECK (view->tilt() > 1.2f);
    CHECK (view->yaw() < -0.3f);

    // the trackpad: pinch and the wheel
    view->resetView();
    view->mouseMagnify (mouse (*view, { 50, 50 }, { 50, 50 }, false), 0.5f);
    CHECK (approx (view->zoom(), 0.5f));
    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f;
    wheel.deltaY = 0.25f;
    view->mouseWheelMove (mouse (*view, { 50, 50 }, { 50, 50 }, false), wheel);
    CHECK (view->tilt() > 1.0f);

    // the visible alternative: zoom buttons; double tap resets the whole view
    view->zoomIn();
    view->mouseDoubleClick (mouse (*view, { 50, 50 }, { 50, 50 }, false, 2));
    CHECK (approx (view->zoom(), 1.0f) && approx (view->tilt(), 1.0f) && approx (view->yaw(), 0.0f));

    // the other view modes ignore the gestures
    view->setViewMode (WavetableView::ViewMode::mode2D);
    view->handleTouchGesture (pinch);
    CHECK (approx (view->zoom(), 1.0f));
}

TEST (uig_zz_shutdown)
{
    delete juceInitGaps;
    juceInitGaps = nullptr;
    CHECK (true);
}
