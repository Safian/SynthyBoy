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
#include "ui/panels/EnvelopePanel.h"
#include "ui/ModSourceBar.h"
#include "ui/panels/FxRackView.h"
#include "ui/panels/GlbScopePanel.h"
#include "ui/panels/ModMatrixReorder.h"
#include "ui/panels/LfoShapeEditor.h"
#include "ui/panels/OscTabsPanel.h"
#include "ui/panels/OscWaveView.h"
#include "ui/panels/TransposeControl.h"
#include "ui/panels/WavetableEditorSheet.h"
#include "ui/panels/WavetablePanel.h"
#include "ui/panels/SubPanel.h"
#include "ui/panels/NoisePanel.h"
#include "ui/model/RatioSource.h"
#include "ui/panels/FilterModels.h"
#include "ui/panels/FilterViews.h"
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
    CHECK (sheet.button ("Lock module") != nullptr && sheet.button ("Lock module")->isEnabled()); // G08: a real lock now
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

TEST (uig_pitch_mode_and_transpose_snap_keyboard)
{
    Rig rig;
    auto* panel = rig.panelTitled ("Oscillator 1");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    panel->setMoreExpanded (true);

    // Row 02-09: pitch MODE selector (Semitones / Harmonics / Ratio / Step)
    auto* pitchMode = rig.find<ParamChoice> ([] (ParamChoice& c) { return c.key() == "osc1.pitch_mode"; });
    CHECK (pitchMode != nullptr);
    if (pitchMode != nullptr)
    {
        CHECK_EQ (rig.param ("osc1.pitch_mode"), 0.0f); // Default Semitones
        rig.setParam ("osc1.pitch_mode", 1.0f);         // Harmonics
        CHECK_EQ (rig.param ("osc1.pitch_mode"), 1.0f);
        rig.setParam ("osc1.pitch_mode", 2.0f);         // Ratio
        CHECK_EQ (rig.param ("osc1.pitch_mode"), 2.0f);
        rig.setParam ("osc1.pitch_mode", 3.0f);         // Step
        CHECK_EQ (rig.param ("osc1.pitch_mode"), 3.0f);
        rig.setParam ("osc1.pitch_mode", 0.0f);         // Back to Semitones
    }

    // Row 02-10: Transpose snap keyboard
    auto* snap = rig.find<TransposeSnapControl>();
    CHECK (snap != nullptr);
    if (snap == nullptr)
        return;
    CHECK (snap->key() == "osc1.transpose_snap");
    CHECK (snap->isVisible() && snap->getWidth() >= snap->getParentComponent()->getWidth() - 2 * rig.editor->theme().gap() - 1);

    // Initially off
    CHECK_EQ (snap->rawMask(), 0);
    CHECK (! snap->isKeyActive (0));
    CHECK (! snap->isGlobalActive());

    // Tap note key 0 (C)
    tap (*snap, snap->noteBounds (0).getCentre().toFloat());
    CHECK (snap->isKeyActive (0));
    CHECK_EQ (rig.param ("osc1.transpose_snap"), 1.0f);

    // Tap note key 7 (G) -> bit 7 = 128 -> total 129
    tap (*snap, snap->noteBounds (7).getCentre().toFloat());
    CHECK (snap->isKeyActive (7));
    CHECK_EQ (rig.param ("osc1.transpose_snap"), 129.0f);

    // Tap note key 0 again to toggle off
    tap (*snap, snap->noteBounds (0).getCentre().toFloat());
    CHECK (! snap->isKeyActive (0));
    CHECK_EQ (rig.param ("osc1.transpose_snap"), 128.0f);

    // Tap Global Snap button
    tap (*snap, snap->globalBounds().getCentre().toFloat());
    CHECK (snap->isGlobalActive());
    CHECK_EQ (static_cast<int> (rig.param ("osc1.transpose_snap")) & TransposeSnapControl::kGlobalSnapBit, TransposeSnapControl::kGlobalSnapBit);

    // Tap Clear button
    tap (*snap, snap->clearBounds().getCentre().toFloat());
    CHECK_EQ (rig.param ("osc1.transpose_snap"), 0.0f);
    CHECK_EQ (snap->rawMask(), 0);
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
    CHECK_EQ (groups, 8 + 2);     // the eight groups, a separator and Swap Warps
    CHECK_EQ (disabledGroups, 0); // block B21: Filter / Distortion / PD / AM have their modes
    CHECK_EQ (subMenus, 8);       // Basic / Alt / Filter / Distortion / FM / PD / AM / RM
    CHECK_EQ (tickedSub, 1);      // warp type None sits in Basic
    CHECK (warp->hasExtraAction());

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

// ---- LFO shape editor tools -----------------------------------------------------------------------------------------------------------

namespace
{
const ContentLfoShape* shapeOf (const StateContent& c, int lfo)
{
    for (const auto& l : c.lfoShapes)
        if (l.lfo == lfo)
            return &l;
    return nullptr;
}

LfoShapeEditor* customEditor (Rig& rig)
{
    rig.setParam ("lfo1.shape", 12.0f); // Custom
    return rig.find<LfoShapeEditor>();
}

juce::Point<float> plotPos (LfoShapeEditor& ed, float x, float y)
{
    const auto plot = ed.plotBounds();
    return { plot.getX() + x * plot.getWidth(), plot.getBottom() - y * plot.getHeight() };
}
} // namespace

TEST (uig_lfo_steps_tool_draws_one_step_per_grid_column)
{
    Rig rig (Page::lfo);
    auto* ed = customEditor (rig);
    CHECK (ed != nullptr);
    if (ed == nullptr)
        return;
    CHECK (ed->barButton ("Steps") != nullptr && ed->barButton ("Brush") != nullptr);
    ed->setGrid (8, 4);
    CHECK_EQ (ed->gridColumns(), 8);
    CHECK_EQ (ed->gridRows(), 4);
    ed->setTool (LfoShapeEditor::Tool::steps);
    CHECK (ed->tool() == LfoShapeEditor::Tool::steps);

    // drag from column 1 to column 4 at 80 %, flat levels at the grid: columns 1..4 are 0.8, a stepped shape of 2 points per column
    drag (*ed, plotPos (*ed, 1.5f / 8.0f, 0.8f), plotPos (*ed, 4.5f / 8.0f, 0.8f));
    const auto shape = ed->shape();
    CHECK_EQ (shape.points.size(), static_cast<size_t> (16));
    for (size_t i = 0; i < shape.points.size(); i += 2)
        CHECK (approx (shape.points[i].y, shape.points[i + 1].y, 1e-5f)); // each column is flat
    CHECK (approx (shape.points[2].y, 0.8f, 0.03f) && approx (shape.points[8].y, 0.8f, 0.03f));
    CHECK (std::abs (shape.points[0].y - 0.8f) > 0.05f || std::abs (shape.points[14].y - 0.8f) > 0.05f); // the other columns kept their level
    CHECK (std::is_sorted (shape.points.begin(), shape.points.end(), [] (const ContentPoint& a, const ContentPoint& b) { return a.x < b.x; }));
    CHECK (validateContent (rig.proc.content()));
    CHECK (ed->canUndo()); // one drag = one undo step
    ed->undo();
    CHECK (ed->shape().points.size() != static_cast<size_t> (16));

    // with Snap the level lands on the row grid (4 rows: 0.25 steps)
    ed->setSnap (true);
    drag (*ed, plotPos (*ed, 0.5f / 8.0f, 0.62f), plotPos (*ed, 0.5f / 8.0f, 0.62f) + juce::Point<float> (1.0f, 0.0f));
    CHECK (approx (ed->shape().points[0].y, 0.5f, 1e-4f));
    ed->setSnap (false);
}

TEST (uig_lfo_brush_tool_paints_the_chosen_brush)
{
    Rig rig (Page::lfo);
    auto* ed = customEditor (rig);
    CHECK (ed != nullptr);
    if (ed == nullptr)
        return;
    CHECK_EQ (LfoShapeEditor::kNumBrushes, 5);
    CHECK (std::string (LfoShapeEditor::brushName (0)) == "Step" && std::string (LfoShapeEditor::brushName (4)) == "Tri");
    ed->setGrid (4, 8);
    ed->setTool (LfoShapeEditor::Tool::brush);

    struct Expect { int brush; size_t pointsPerCell; };
    for (const auto& ex : { Expect { 0, 2 }, Expect { 1, 5 }, Expect { 2, 2 }, Expect { 3, 2 }, Expect { 4, 3 } })
    {
        ed->setBrush (ex.brush);
        CHECK (ed->barButton (LfoShapeEditor::brushName (ex.brush)) != nullptr); // the picker shows the brush
        drag (*ed, plotPos (*ed, 0.1f, 0.9f), plotPos (*ed, 0.9f, 0.9f));
        CHECK_EQ (ed->shape().points.size(), 4 * ex.pointsPerCell);
        CHECK (validateContent (rig.proc.content()));
    }

    // Down: each cell falls from the level to 0; Up rises; Tri peaks in the middle; Half is a bump that is 0 at the cell borders
    ed->setBrush (2);
    drag (*ed, plotPos (*ed, 0.1f, 0.8f), plotPos (*ed, 0.2f, 0.8f));
    CHECK (approx (ed->shape().points[0].y, 0.8f, 0.03f) && approx (ed->shape().points[1].y, 0.0f, 1e-4f));
    ed->setBrush (4);
    drag (*ed, plotPos (*ed, 0.6f, 0.7f), plotPos (*ed, 0.65f, 0.7f));
    const auto& pts = ed->shape().points;
    bool foundPeak = false;
    for (size_t i = 0; i + 2 < pts.size(); ++i)
        if (approx (pts[i].y, 0.0f, 1e-4f) && pts[i + 1].y > 0.6f && approx (pts[i + 2].y, 0.0f, 1e-4f))
            foundPeak = true;
    CHECK (foundPeak);
}

TEST (uig_lfo_marquee_select_and_proportional_move)
{
    Rig rig (Page::lfo);
    auto* ed = customEditor (rig);
    CHECK (ed != nullptr);
    if (ed == nullptr)
        return;
    ed->applyPreset (0); // sine: 33 points
    const size_t n = ed->shape().points.size();
    CHECK_EQ (n, static_cast<size_t> (33));

    // hold still on the empty plot (the long press), then drag a rectangle around the points of the left half
    const auto from = plotPos (*ed, 0.02f, 0.02f); // below the curve at the start: empty
    const auto to = plotPos (*ed, 0.52f, 1.0f);
    ed->mouseDown (mouse (*ed, from, from, false));
    CHECK (ed->longPress().pending());
    ed->longPress().fire();
    CHECK (ed->marqueeActive());
    ed->mouseDrag (mouse (*ed, to, from, true));
    ed->mouseUp (mouse (*ed, to, from, true));
    CHECK (! ed->marqueeActive());
    CHECK (ed->selection().size() >= 15 && ed->selection().size() <= 18);
    for (const int i : ed->selection())
        CHECK (ed->shape().points[static_cast<size_t> (i)].x <= 0.53f);

    // a plain drag of one selected point moves them all by the same amount
    const auto before = ed->shape();
    const int anchor = ed->selection()[ed->selection().size() / 2];
    const auto a = ed->pointPosition (anchor);
    drag (*ed, a, a + juce::Point<float> (0.0f, -ed->plotBounds().getHeight() * 0.10f));
    int moved = 0;
    for (const int i : ed->selection())
    {
        const float d = ed->shape().points[static_cast<size_t> (i)].y - before.points[static_cast<size_t> (i)].y;
        if (d > 0.02f)
            ++moved;
        CHECK (d >= -1e-6f);
    }
    CHECK (moved >= static_cast<int> (ed->selection().size()) / 2); // the ones clamped at the top stay
    // the points outside did not move
    CHECK (approx (ed->shape().points[n - 1].y, before.points[n - 1].y, 1e-5f));
    CHECK (ed->canUndo());
    ed->undo();
    CHECK (ed->selectInRect (juce::Rectangle<float> (from, to))); // undo drops the selection: select again

    // Prop: the rainbow falloff - the finger's point moves fully, the ends of the selection hardly
    ed->setProportional (true);
    CHECK (ed->proportional());
    const auto base = ed->shape();
    CHECK (ed->moveSelection (anchor, 0.0f, -0.2f));
    const float dAnchor = base.points[static_cast<size_t> (anchor)].y - ed->shape().points[static_cast<size_t> (anchor)].y;
    const int lastSel = ed->selection().back();
    const float dEnd = std::abs (base.points[static_cast<size_t> (lastSel)].y - ed->shape().points[static_cast<size_t> (lastSel)].y);
    CHECK (approx (dAnchor, 0.2f, 0.01f));
    CHECK (dEnd < 0.05f);
    // x never crosses a neighbour
    for (size_t i = 1; i < n; ++i)
        CHECK (ed->shape().points[i].x >= ed->shape().points[i - 1].x);
    ed->undo();
    ed->setProportional (false);

    // a tap on empty space drops the selection
    tap (*ed, plotPos (*ed, 0.2f, 0.02f));
    CHECK (ed->selection().empty());
}

TEST (uig_lfo_selected_point_bar_loop_start_segment_fields_grid_all_curves)
{
    Rig rig (Page::lfo);
    auto* ed = customEditor (rig);
    CHECK (ed != nullptr);
    if (ed == nullptr)
        return;
    ed->applyPreset (1); // triangle (0,0) (0.5,1) (1,0)

    // nothing selected: the point bar is off
    CHECK (! ed->xFieldEditor().isEnabled() && ! ed->barButton ("Loop here")->isEnabled() && ! ed->barButton ("Start here")->isEnabled());
    const auto mid = ed->pointPosition (1);
    tap (*ed, mid);
    CHECK_EQ (ed->selectedPoint(), 1);
    CHECK (ed->xFieldEditor().isEnabled() && ed->barButton ("Loop here")->isEnabled() && ed->barButton ("Segment")->isEnabled());
    CHECK (ed->xFieldEditor().getText().getFloatValue() > 49.0f && ed->xFieldEditor().getText().getFloatValue() < 51.0f);
    CHECK (ed->yFieldEditor().getText().getFloatValue() > 99.0f);

    // numeric X / Y: the alternative of the point drag
    CHECK (ed->applyFieldText (true, "30"));
    CHECK (approx (ed->shape().points[1].x, 0.30f, 1e-4f));
    CHECK (ed->applyFieldText (false, "55.5"));
    CHECK (approx (ed->shape().points[1].y, 0.555f, 1e-4f));
    CHECK (! ed->applyFieldText (true, "140"));
    CHECK (! ed->applyFieldText (true, "abc"));
    ed->xFieldEditor().setText ("45", juce::dontSendNotification);
    ed->xFieldEditor().onReturnKey();
    CHECK (approx (ed->shape().points[1].x, 0.45f, 1e-4f));
    CHECK (approx (shapeOf (rig.proc.content(), 1)->points[1].x, 0.45f, 1e-4f)); // reached the host

    // Loop here toggles the loop point and it is saved in the content
    CHECK_EQ (ed->loopPoint(), -1);
    ed->barButton ("Loop here")->onClick();
    CHECK_EQ (ed->loopPoint(), 1);
    CHECK_EQ (shapeOf (rig.proc.content(), 1)->loopPoint, 1);
    CHECK (validateContent (rig.proc.content()));
    ed->barButton ("Loop here")->onClick();
    CHECK_EQ (ed->loopPoint(), -1);
    ed->loopHere();

    // Start here sets the LFO phase marker to the x of the point
    CHECK (ed->startHere());
    CHECK (approx (rig.param ("lfo1.phase"), 0.45f * 360.0f, 1.0f));

    // Segment shape menu entries: the segment that starts at the selected point
    CHECK (ed->setSegmentShape (1));
    CHECK (ed->shape().points[1].power > 0.0f);
    CHECK (ed->setSegmentShape (4));
    CHECK (ed->shape().points[1].power < 0.0f);
    CHECK (ed->setSegmentShape (0));
    CHECK (approx (ed->shape().points[1].power, 0.0f, 1e-6f));

    // All curves: one handle bends every segment
    CHECK (ed->setSegmentPower (0, 2.0f));
    ed->setAllCurves (true);
    const auto startPowers = std::vector<float> { ed->shape().points[0].power, ed->shape().points[1].power };
    const auto handle = ed->handlePosition (1);
    drag (*ed, handle, handle + juce::Point<float> (0.0f, -25.0f));
    CHECK (! approx (ed->shape().points[0].power, startPowers[0], 1e-4f));
    CHECK (! approx (ed->shape().points[1].power, startPowers[1], 1e-4f));
    ed->setAllCurves (false);

    // grid fields and steppers
    ed->barButton ("+")->onClick(); // the first "+" is the columns stepper
    CHECK_EQ (ed->gridColumns(), 17);
    CHECK_EQ (ed->gridRows(), 8);
    ed->setGrid (0, 99);
    CHECK (ed->gridColumns() == 1 && ed->gridRows() == 32);
    ed->setGrid (16, 8);

    // every button of the bar is a 44 point target
    for (const char* text : { "Point", "Draw", "Steps", "Brush", "Snap", "Smooth", "Prop", "All curves", "Add", "Delete", "Undo", "Redo", "Presets",
                              "Loop here", "Start here", "Segment" })
    {
        auto* b = ed->barButton (text);
        CHECK (b != nullptr && b->getHeight() >= rig.editor->theme().touch() && b->getWidth() >= rig.editor->theme().touch());
    }
}

// ---- envelope tabs ----------------------------------------------------------------------------------------------------------------------

TEST (uig_envelope_tabs_select_long_press_and_drop_on_a_control)
{
    Rig rig (Page::env);
    CHECK (rig.panelTitled ("Envelope 1 (Amp)") != nullptr);
    CHECK (rig.panelTitled ("Envelope 2") == nullptr);
    auto* env1 = dynamic_cast<EnvelopePanel*> (rig.panelTitled ("Envelope 1 (Amp)"));
    CHECK (env1 != nullptr);
    if (env1 == nullptr)
        return;

    // tap E3: that envelope is the one shown
    auto* tab3 = env1->tabButton (3);
    CHECK (tab3 != nullptr && tab3->getWidth() >= rig.editor->theme().touch() && tab3->getHeight() >= rig.editor->theme().touch());
    tab3->onClick();
    CHECK_EQ (rig.editor->context().view->envTab, 2);
    CHECK (rig.panelTitled ("Envelope 3") != nullptr && rig.panelTitled ("Envelope 1 (Amp)") == nullptr);
    env1->selectEnvelope (1);
    CHECK (rig.panelTitled ("Envelope 1 (Amp)") != nullptr);

    // a long press opens the destination list (and the release after it is no tap)
    env1 = dynamic_cast<EnvelopePanel*> (rig.panelTitled ("Envelope 1 (Amp)"));
    tab3 = env1->tabButton (3);
    const auto c = tab3->getLocalBounds().getCentre().toFloat();
    tab3->mouseDown (mouse (*tab3, c, c, false));
    CHECK (tab3->longPress().pending());
    tab3->longPress().fire(); // the 400 ms timer: ModSourceChip::showAssignmentsFor (menu, async)
    juce::PopupMenu::dismissAllActiveMenus();
    tab3->mouseUp (mouse (*tab3, c, c, false));
    CHECK (tab3->consumeSuppressedClick());
    CHECK_EQ (rig.editor->context().view->envTab, 0); // unchanged

    // drag the tab onto a modulatable knob of another panel: the assignment is made at 50 %
    rig.editor->panelHost().setPage (Page::flt);
    auto* cutoff = rig.find<ParamKnob> ([] (ParamKnob& k) { return k.key() == "filter1.cutoff"; });
    CHECK (cutoff != nullptr);
    if (cutoff == nullptr)
        return;
    env1->tabButton (2)->mouseDown (mouse (*env1->tabButton (2), { 10, 10 }, { 10, 10 }, false));
    CHECK (env1->dropSourceOn (2, cutoff));
    const auto entries = rig.editor->context().assign.snapshot().entriesForSource (2);
    bool found = false;
    for (const auto& [dest, e] : entries)
        if (std::string (paramTable()[static_cast<size_t> (dest)].key) == "filter1.cutoff")
            found = e.amount > 0.0f;
    CHECK (found);
    CHECK (rig.editor->context().assign.flow().armedSource() == 0); // assign mode is left
    env1->tabButton (2)->mouseUp (mouse (*env1->tabButton (2), { 10, 10 }, { 10, 10 }, false));

    // dropped on nothing or on a non-modulatable control: no assignment, mode left
    CHECK (! env1->dropSourceOn (2, nullptr));
    auto* onKey = rig.find<ParamToggle> ([] (ParamToggle& k) { return k.key() == "filter1.on"; });
    if (onKey != nullptr)
        CHECK (! env1->dropSourceOn (2, onKey));
    CHECK (rig.editor->context().assign.flow().armedSource() == 0);
}

// ---- mixer strips, voices / CPU / scope ---------------------------------------------------------------------------------------------

TEST (uig_mixer_strip_header_sheet_swap_copy_and_pan)
{
    Rig rig (Page::mix);
    auto* mix = rig.find<OscMixConsoleView>();
    CHECK (mix != nullptr);
    if (mix == nullptr)
        return;
    CHECK (std::string (OscMixConsoleView::stripModulePrefix (1)) == "osc2" && std::string (OscMixConsoleView::stripModulePrefix (5)) == "filter2");
    CHECK (std::string (OscMixConsoleView::stripModulePrefix (3)).empty() && std::string (OscMixConsoleView::stripModulePrefix (6)).empty());

    // the strip header is a 44 point band
    for (int i = 0; i < OscMixConsoleView::kNumStrips; ++i)
        CHECK (mix->nameBounds (i).getHeight() >= static_cast<float> (rig.editor->theme().touch()) - 0.5f);

    // tap on the header: power (O1 .. NOISE), as before
    rig.setParam ("osc2.on", 1.0f);
    const auto n2 = mix->nameBounds (1).getCentre();
    tap (*mix, n2);
    CHECK_EQ (rig.param ("osc2.on"), 0.0f);
    tap (*mix, n2);
    CHECK_EQ (rig.param ("osc2.on"), 1.0f);

    // long-press on the header of O2 opens the module sheet; the release after it is no tap
    const int before = desktopComponents();
    mix->mouseDown (mouse (*mix, n2, n2, false));
    CHECK (mix->longPress().pending());
    mix->longPress().fire();
    CHECK (desktopComponents() == before + 1);
    dismissCallOuts();
    mix->mouseUp (mouse (*mix, n2, n2, false));
    CHECK_EQ (rig.param ("osc2.on"), 1.0f);
    // NOISE and the buses have no module sheet: no long press is armed
    const auto nn = mix->nameBounds (3).getCentre();
    mix->mouseDown (mouse (*mix, nn, nn, false));
    CHECK (! mix->longPress().pending());
    mix->mouseUp (mouse (*mix, nn, nn, false));

    // drag the header of O1 onto O3: swap; with Alt: copy
    rig.setParam ("osc1.tune", 17.0f);
    rig.setParam ("osc3.tune", -9.0f);
    const auto from = mix->nameBounds (0).getCentre();
    const auto to = mix->nameBounds (2).getCentre();
    mix->mouseDown (mouse (*mix, from, from, false));
    mix->mouseDrag (mouse (*mix, to, from, true));
    CHECK_EQ (mix->dropTargetStrip(), 2);
    mix->mouseUp (mouse (*mix, to, from, true));
    CHECK (approx (rig.param ("osc1.tune"), -9.0f) && approx (rig.param ("osc3.tune"), 17.0f));
    CHECK_EQ (rig.param ("osc2.on"), 1.0f); // a drag is no tap on the power
    CHECK (mix->dropOnStrip (2, 0, true, false)); // copy O3 onto O1
    CHECK (approx (rig.param ("osc1.tune"), 17.0f) && approx (rig.param ("osc3.tune"), 17.0f));
    CHECK (! mix->dropOnStrip (0, 3, false, false)); // NOISE is no module
    CHECK (! mix->dropOnStrip (0, 4, false, false)); // an oscillator and a filter do not swap
    CHECK (! mix->dropOnStrip (1, 1, false, false));
    rig.setParam ("filter1.cutoff", 100.0f);
    rig.setParam ("filter2.cutoff", 400.0f);
    CHECK (mix->dropOnStrip (4, 5, false, false)); // F1 <-> F2
    CHECK (approx (rig.param ("filter1.cutoff"), 400.0f, 4.0f) && approx (rig.param ("filter2.cutoff"), 100.0f, 4.0f));

    // pan: a sideways drag in the band at the top of the strip (O1 O2 O3 NOISE F1 F2 have a pan key, the buses do not)
    CHECK (mix->hasPan (0) && mix->hasPan (3) && mix->hasPan (5) && ! mix->hasPan (6) && ! mix->hasPan (7));
    rig.setParam ("osc3.pan", 0.0f);
    const auto band = mix->panBounds (2);
    const juce::Point<float> p0 { band.getX() + band.getWidth() * 0.5f, band.getCentreY() };
    drag (*mix, p0, { band.getRight() - 1.0f, band.getCentreY() });
    CHECK (rig.param ("osc3.pan") > 40.0f);
    drag (*mix, { band.getRight() - 1.0f, band.getCentreY() }, { band.getX() + 1.0f, band.getCentreY() });
    CHECK (rig.param ("osc3.pan") < -40.0f);
    // the level fader of the strip still works above the band
    const float level0 = rig.param ("osc3.level");
    const auto col = mix->stripBounds (2);
    drag (*mix, { col.getCentreX(), col.getY() + col.getHeight() * 0.3f }, { col.getCentreX(), col.getY() + 2.0f });
    CHECK (! approx (rig.param ("osc3.level"), level0, 1e-4f));
}

TEST (uig_voice_badge_cpu_pill_and_scope)
{
    // the processor side of the three new host read-outs
    {
        SynthyBoyProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        CHECK_EQ (proc.activeVoiceCount(), 0);
        CHECK_EQ (proc.copyScope (nullptr, 0), 0);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        for (int block = 0; block < 12; ++block)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
            midi.clear();
        }
        CHECK (proc.activeVoiceCount() >= 1);
        CHECK (proc.cpuLoad() > 0.0f && proc.cpuLoad() < 50.0f);
        std::vector<float> scope (UiHost::kScopeSamples + 10, 0.0f);
        const int got = proc.copyScope (scope.data(), static_cast<int> (scope.size()));
        CHECK_EQ (got, UiHost::kScopeSamples);
        float peak = 0.0f;
        for (int i = 0; i < got; ++i)
            peak = std::max (peak, std::abs (scope[static_cast<size_t> (i)]));
        CHECK (peak > 0.001f);
        CHECK_EQ (proc.copyScope (scope.data(), 16), 16); // the latest samples, oldest first
        proc.releaseResources();
    }

    Rig rig (Page::glb, 768, 1024);
    // top bar: the badge shows where there is room, a tap jumps to GLOBAL
    CHECK (rig.editor->topBar().voiceBadgeShown());
    rig.editor->topBar().setVoiceBadge (3, 16);
    CHECK (rig.editor->topBar().voices.getButtonText() == "3/16v");
    rig.editor->pageTabs().select (Page::osc, false);
    rig.editor->panelHost().setPage (Page::osc);
    rig.editor->topBar().voices.onClick();
    CHECK (rig.editor->panelHost().page() == Page::glb);
    CHECK (rig.editor->topBar().voices.getHeight() >= rig.editor->theme().touch() && rig.editor->topBar().voices.getWidth() >= rig.editor->theme().touch());
    rig.editor->setSize (390, 844);
    CHECK (! rig.editor->topBar().voiceBadgeShown()); // phone: no room in the bar; the GLB tab carries the count, GLOBAL > Scope the read-out
    rig.editor->pageTabs().setBadge (Page::glb, "3");
    CHECK (rig.editor->pageTabs().button (Page::glb).getButtonText() == "GLB 3");
    rig.editor->pageTabs().setBadge (Page::glb, {});
    CHECK (rig.editor->pageTabs().button (Page::glb).getButtonText() == juce::String (pageLabel (Page::glb)));

    // the scope panel: tap toggles Wave / FFT, a pinch does too, the segment buttons are the alternative
    rig.editor->panelHost().setPage (Page::glb);
    auto* scopePanel = dynamic_cast<GlbScopePanel*> (rig.anyPanelTitled ("Scope"));
    CHECK (scopePanel != nullptr);
    if (scopePanel == nullptr)
        return;
    auto& scope = scopePanel->scope();
    CHECK (scope.mode() == ScopeView::Mode::wave);
    tap (scope, { 20.0f, 20.0f });
    CHECK (scope.mode() == ScopeView::Mode::fft);
    TouchTracker::Gesture pinch;
    pinch.kind = TouchTracker::Kind::pinch;
    pinch.scale = 1.4f;
    scope.handleTouchGesture (pinch);
    CHECK (scope.mode() == ScopeView::Mode::wave);
    scope.mouseMagnify (mouse (scope, { 10, 10 }, { 10, 10 }, false), 0.7f);
    CHECK (scope.mode() == ScopeView::Mode::fft);
    scopePanel->waveButton().onClick();
    CHECK (scope.mode() == ScopeView::Mode::wave);
    scopePanel->fftButton().onClick();
    CHECK (scope.mode() == ScopeView::Mode::fft);
    CHECK (scopePanel->waveButton().getHeight() >= rig.editor->theme().touch());
    // a drag over the display is no tap
    scope.mouseDown (mouse (scope, { 10, 10 }, { 10, 10 }, false));
    scope.mouseDrag (mouse (scope, { 60, 10 }, { 10, 10 }, true));
    scope.mouseUp (mouse (scope, { 60, 10 }, { 10, 10 }, true));
    CHECK (scope.mode() == ScopeView::Mode::fft);
    CHECK (scopePanel->readout().contains ("voices") && scopePanel->readout().contains ("CPU"));

    // the unison pill: green below 50 % CPU, amber below 80 %, red above (pure rule) and absent for one voice
    CHECK_EQ (cpuLevel (0.1f), 0);
    CHECK_EQ (cpuLevel (0.5f), 1);
    CHECK_EQ (cpuLevel (0.79f), 1);
    CHECK_EQ (cpuLevel (0.8f), 2);
    rig.setParam ("osc1.unison.voices", 1.0f);
    auto* view = rig.oscView (1);
    CHECK (view != nullptr);
    if (view != nullptr)
    {
        CHECK_EQ (view->unisonPillLevel(), -1);
        rig.setParam ("osc1.unison.voices", 12.0f);
        CHECK_EQ (view->unisonPillLevel(), 0); // the test host measures no load
        CHECK (view->unisonCapped());          // 12 > 8: amber outline
        rig.setParam ("osc1.unison.voices", 6.0f);
        CHECK (! view->unisonCapped());
    }
}

// ---- modulation, global, effects ------------------------------------------------------------------------------------------------------

namespace
{
/** One assignment through the touch flow, as a user would make it: arm the source, drag the control, release. Returns the slot. */
int assignBySource (Rig& rig, int source, const char* destKey, float percent)
{
    auto& assign = rig.editor->context().assign;
    assign.tapSource (source);
    auto& flow = assign.flow();
    const int index = resolveKey (destKey).index;
    CHECK (flow.isTarget (index));
    CHECK (flow.beginDrag (index) == AssignFlow::BeginResult::ok);
    const int slot = flow.activeSlot();
    flow.setAmount (percent);
    flow.endDrag();
    assign.exitAssignMode();
    assign.refresh();
    return slot;
}
} // namespace

TEST (uig_mod_swap_slots_model_and_matrix_row_handle)
{
    Rig rig (Page::mod);
    const int s0 = assignBySource (rig, 7, "filter1.cutoff", 40.0f);
    const int s1 = assignBySource (rig, 8, "filter1.resonance", -30.0f);
    CHECK (s0 >= 0 && s1 >= 0 && s0 != s1);

    // the model: a swap exchanges everything the two slots hold
    auto& m = rig.editor->context().assign.matrix();
    const float srcA = m.get (modSlotParams (s0).source), srcB = m.get (modSlotParams (s1).source);
    const float amtA = m.get (modSlotParams (s0).amount), amtB = m.get (modSlotParams (s1).amount);
    CHECK (swapModSlots (m, s0, s1));
    CHECK (approx (m.get (modSlotParams (s0).source), srcB) && approx (m.get (modSlotParams (s1).source), srcA));
    CHECK (approx (m.get (modSlotParams (s0).amount), amtB) && approx (m.get (modSlotParams (s1).amount), amtA));
    CHECK (swapModSlots (m, s0, s1)); // and back
    CHECK (approx (m.get (modSlotParams (s0).source), srcA) && approx (m.get (modSlotParams (s0).amount), amtA));
    CHECK (! swapModSlots (m, 3, 3) && ! swapModSlots (m, -1, 2) && ! swapModSlots (m, 0, kNumModSlots));

    auto* panel = rig.panelTitled ("Modulation Matrix");
    auto* reorder = dynamic_cast<SlotReorder*> (panel);
    CHECK (panel != nullptr && reorder != nullptr);
    if (reorder == nullptr)
        return;
    reorder->refreshRows();
    CHECK (reorder->slotRowShown (s0) && reorder->slotRowShown (s1));
    CHECK (reorder->slotHandleBounds (s0).getWidth() >= rig.editor->theme().touch());

    // the menu route (Move down) ...
    CHECK_EQ (reorder->moveSlot (s0, +1), s1);
    CHECK (approx (m.get (modSlotParams (s1).source), srcA));
    CHECK_EQ (reorder->moveSlot (s0, -1), -1); // nothing above the first shown slot
    CHECK_EQ (reorder->moveSlot (s1, +1), -1); // nothing below the last shown slot
    CHECK_EQ (reorder->moveSlot (s1, -1), s0);
    CHECK (approx (m.get (modSlotParams (s0).source), srcA));

    // ... and the drag on the handle: pull the first row down past the second
    auto* row = reorder->slotRowComponent (s0);
    CHECK (row != nullptr);
    if (row == nullptr)
        return;
    const auto handle = reorder->slotHandleBounds (s0).getCentre().toFloat();
    drag (*row, handle, handle + juce::Point<float> (0.0f, static_cast<float> (row->getHeight())));
    CHECK (approx (m.get (modSlotParams (s1).source), srcA));
    CHECK (approx (m.get (modSlotParams (s0).source), srcB));
    CHECK (! reorder->rowMenuWasShown()); // a drag is no tap

    // a tap on the handle opens the row menu (the non-gesture alternative of the drag)
    row = reorder->slotRowComponent (s0);
    tap (*row, reorder->slotHandleBounds (s0).getCentre().toFloat());
    CHECK (reorder->rowMenuWasShown());
    juce::PopupMenu::dismissAllActiveMenus();
}

TEST (uig_macro_chips_swap_by_drag)
{
    Rig rig (Page::mod);
    rig.editor->setSourceBarOpen (true);
    rig.setParam ("macro.1", 30.0f);
    rig.setParam ("macro.2", 70.0f);
    auto* a = rig.editor->sourceBar().chipFor (19);
    auto* b = rig.editor->sourceBar().chipFor (20);
    CHECK (a != nullptr && b != nullptr);
    if (a == nullptr || b == nullptr)
        return;
    CHECK (macroOfSourceChoice (19) == 1 && macroOfSourceChoice (22) == 4 && macroOfSourceChoice (29) == 5 && macroOfSourceChoice (32) == 8
           && macroOfSourceChoice (7) == 0 && macroOfSourceChoice (23) == 0);

    const auto from = a->getLocalBounds().getCentre().toFloat();
    const auto to = a->getLocalPoint (b, b->getLocalBounds().getCentre()).toFloat();
    a->mouseDown (mouse (*a, from, from, false));
    a->mouseDrag (mouse (*a, to, from, true));
    CHECK (a->isDragging());
    a->mouseUp (mouse (*a, to, from, true));
    CHECK (approx (rig.param ("macro.1"), 70.0f, 0.5f) && approx (rig.param ("macro.2"), 30.0f, 0.5f));
    CHECK (! rig.editor->context().assign.flow().armedSource()); // a drag does not arm the chip

    // a tap still arms; a non-macro chip never swaps
    tap (*a, from);
    CHECK_EQ (rig.editor->context().assign.flow().armedSource(), 19);
    rig.editor->context().assign.exitAssignMode();
    CHECK (! ModSourceChip::swapMacroSources (rig.editor->context(), 19, 7));
    CHECK (! ModSourceChip::swapMacroSources (rig.editor->context(), 19, 19));
    CHECK (ModSourceChip::swapMacroSources (rig.editor->context(), 29, 32)); // macros 5 and 8
}

TEST (uig_depth_halo_badge_is_a_target)
{
    Rig rig (Page::flt);
    const int slot = assignBySource (rig, 7, "filter1.cutoff", 40.0f);
    auto* knob = rig.find<ParamKnob> ([] (ParamKnob& k) { return k.key() == "filter1.cutoff"; });
    CHECK (knob != nullptr);
    if (knob == nullptr)
        return;
    CHECK_EQ (knob->modulatorCount(), 1);
    const auto badge = knob->modulatorBadgeBounds();
    CHECK (! badge.isEmpty());
    CHECK (knob->getLocalBounds().contains (badge));

    // a tap on the badge lists the modulators; a press elsewhere is still the knob drag
    const auto centre = badge.getCentre().toFloat();
    tap (*knob, centre);
    CHECK (knob->modulatorListShown());
    juce::PopupMenu::dismissAllActiveMenus();
    const float before = rig.param ("filter1.cutoff");
    drag (*knob, centre, centre + juce::Point<float> (0.0f, -30.0f)); // starts on the badge: no value change
    CHECK (approx (rig.param ("filter1.cutoff"), before, 1.0f));

    // choosing the modulator arms its source: the next drag on the knob sets the depth of exactly that slot
    CHECK (knob->selectModulator (slot));
    CHECK_EQ (rig.editor->context().assign.flow().armedSource(), 7);
    const auto m = knob->getLocalBounds().getCentre().toFloat();
    drag (*knob, m, m + juce::Point<float> (0.0f, -40.0f));
    CHECK (rig.param (("mod" + std::string (slot < 9 ? "0" : "") + std::to_string (slot + 1) + ".amount").c_str()) > 40.5f);
    CHECK (approx (rig.param ("filter1.cutoff"), before, 1.0f)); // the value itself did not move
    CHECK (! knob->selectModulator (63));
    rig.editor->context().assign.exitAssignMode();
}

TEST (uig_settings_buttons_act_and_confirm_with_a_toast)
{
    Rig rig (Page::glb);
    const auto button = [&rig] (const char* text)
    {
        return rig.find<juce::TextButton> ([text] (juce::TextButton& b) { return b.getButtonText() == text; });
    };
    auto* initPreset = button ("Init Preset");
    auto* initMods = button ("Init Mods");
    auto* initLfos = button ("Init LFOs");
    auto* revert = button ("Revert Saved");
    CHECK (initPreset != nullptr && initMods != nullptr && initLfos != nullptr && revert != nullptr);
    if (initPreset == nullptr || initMods == nullptr || initLfos == nullptr || revert == nullptr)
        return;

    // Init Mods
    assignBySource (rig, 7, "filter1.cutoff", 40.0f);
    CHECK (rig.editor->context().assign.snapshot().destinationCount (7) > 0);
    initMods->onClick();
    rig.editor->context().assign.refresh();
    CHECK_EQ (rig.editor->context().assign.snapshot().destinationCount (7), 0);
    CHECK (Toast::current() != nullptr && Toast::current()->text() == "All modulations cleared");

    // Init LFOs: parameters and custom shapes
    rig.setParam ("lfo2.rate", 12.0f);
    rig.setParam ("rlfo1.rate", 9.0f);
    {
        auto c = rig.proc.content();
        ContentLfoShape s;
        s.lfo = 2;
        s.points = { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 1.0f) };
        c.lfoShapes.push_back (s);
        CHECK (rig.proc.setContent (c));
    }
    initLfos->onClick();
    CHECK (std::abs (rig.param ("lfo2.rate") - 12.0f) > 0.01f);
    CHECK (std::abs (rig.param ("rlfo1.rate") - 9.0f) > 0.01f);
    CHECK (rig.proc.content().lfoShapes.empty());
    CHECK (Toast::current()->text() == "LFOs initialised");

    // Init Preset: every parameter back to its default, the content empty
    rig.setParam ("osc1.tune", 33.0f);
    rig.setParam ("filter1.cutoff", 123.0f);
    rig.setParam ("reverb.on", 1.0f);
    initPreset->onClick();
    CHECK (std::abs (rig.param ("osc1.tune")) < 0.01f);
    CHECK (std::abs (rig.param ("filter1.cutoff") - 12000.0f) < 5.0f);
    CHECK (approx (rig.param ("reverb.on"), ParamValues::defaults().plain[static_cast<size_t> (resolveKey ("reverb.on").index)]));
    CHECK (Toast::current()->text() == "Preset initialised");

    // Revert Saved goes back to what the host loaded last
    rig.setParam ("osc2.tune", 20.0f);
    juce::MemoryBlock state;
    rig.proc.getStateInformation (state);
    rig.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize())); // the "load": this is the saved patch now
    rig.setParam ("osc2.tune", -40.0f);
    CHECK (approx (rig.param ("osc2.tune"), -40.0f, 0.5f));
    revert->onClick();
    CHECK (approx (rig.param ("osc2.tune"), 20.0f, 0.5f));
    CHECK (Toast::current()->text() == "Reverted to the saved patch");
    CHECK (Toast::current()->getWidth() > 100);
}

TEST (uig_fx_chain_slot_power_reorder_and_bypass_alternatives)
{
    // the racks of the FX page are strips of cards (docs/ui-design/18-FX-INSTANCES.md 2.8); the gestures of the old 16 slot ribbon are the ones of the cards
    Rig rig (Page::fx);
    auto* view = rig.find<FxRackView>();
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    for (auto* b : { &view->powerToggleButton(), &view->bypassAllToggleButton(), &view->moveLeft(), &view->moveRight(), &view->duplicateToggleButton(),
                     &view->removeSlotButton(), &view->addModuleButton() })
        CHECK (b->getHeight() >= rig.editor->theme().touch() && b->getWidth() >= rig.editor->theme().touch());

    const auto mainRack = [view] { return view->racks().rack[0]; };
    const auto centreOf = [view] (FxRackItem item) { return view->cardBounds (item).getCentre(); };
    const auto dotOf = [view, &rig] (FxRackItem item)
    {
        const auto r = view->cardBounds (item);
        return juce::Point<float> (r.getX() + rig.editor->theme().px (6.0f), r.getCentreY());
    };
    const FxRackItem first = mainRack()[0], second = mainRack()[1], third = mainRack()[2], fourth = mainRack()[3], fifth = mainRack()[4];

    // double tap on a card toggles its module (design 08-02), a tap on the dot does too
    const bool before2 = view->isOn (third);
    doubleTap (*view, centreOf (third));
    CHECK (view->isOn (third) != before2);
    doubleTap (*view, centreOf (third));
    CHECK_EQ (view->isOn (third), before2);
    const bool before3 = view->isOn (fourth);
    tap (*view, dotOf (fourth));
    CHECK (view->isOn (fourth) != before3);
    tap (*view, dotOf (fourth));
    CHECK_EQ (view->isOn (fourth), before3);

    // [ON] is the visible alternative: it follows the selected card
    view->select (fifth);
    const bool before4 = view->isOn (fifth);
    CHECK (view->powerToggleButton().getButtonText() == (before4 ? "ON" : "OFF"));
    view->powerToggleButton().onClick();
    CHECK (view->isOn (fifth) != before4);
    CHECK (view->powerToggleButton().getButtonText() == (view->isOn (fifth) ? "ON" : "OFF"));

    // Opt/Alt on the dot: every module at once (bypass), again: restored
    for (const auto& item : mainRack())
        if (! view->isOn (item))
            view->togglePower (item);
    const auto altTap = [&] (juce::Point<float> p)
    {
        const auto mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
        view->mouseDown (mouse (*view, p, p, false, 1, mods));
        view->mouseUp (mouse (*view, p, p, false, 1, mods));
    };
    const auto countOn = [&]
    {
        int on = 0;
        for (const auto& item : mainRack())
            on += view->isOn (item) ? 1 : 0;
        return on;
    };
    altTap (dotOf (fifth));
    CHECK_EQ (countOn(), 0);
    altTap (dotOf (fifth));
    CHECK_EQ (countOn(), 16);

    // reorder needs the hold: a quick drag does nothing, hold + drag moves the card, the drop line shows while it is armed
    const auto order = mainRack();
    view->mouseDown (mouse (*view, centreOf (first), centreOf (first), false));
    view->mouseDrag (mouse (*view, centreOf (fourth), centreOf (first), true));
    view->mouseUp (mouse (*view, centreOf (fourth), centreOf (first), true));
    CHECK (mainRack() == order);
    const auto from = centreOf (first) + juce::Point<float> (6.0f, 0.0f); // away from the dot
    const auto to = centreOf (fourth) + juce::Point<float> (6.0f, 0.0f);   // right of the centre of the 4th card: behind it
    view->mouseDown (mouse (*view, from, from, false));
    CHECK (view->longPress().pending());
    view->longPress().fire();
    CHECK (view->reorderIsArmed());
    view->mouseDrag (mouse (*view, to, from, true));
    CHECK (view->dropLineShown());
    view->mouseUp (mouse (*view, to, from, true));
    const auto moved = mainRack();
    CHECK (moved[3] == first);
    CHECK (moved[0] == second);
    CHECK (! view->reorderIsArmed());

    // the buttons are the alternative: Move Left / Right act on the selected card
    view->select (first);
    view->moveLeft().onClick();
    CHECK (mainRack()[2] == first);
}

// ---- screenshots of the changed screens (phone 390x844, tablet 1024x768) -> /tmp/ui-shots/gaps/ --------------------------------------------

TEST (uig_screenshots_of_the_changed_screens)
{
    struct Size { int w, h; const char* name; };
    for (const auto& sz : { Size { 390, 844, "phone" }, Size { 1024, 768, "tablet" } })
    {
        const juce::String n (sz.name);
        {
            // oscillator page: the chip row, the OCT / SEM control and the grouped warp list behind More
            Rig rig (Page::osc, sz.w, sz.h);
            rig.setParam ("osc1.on", 1.0f);
            rig.setParam ("osc2.on", 0.0f);
            rig.setParam ("osc1.transpose", 7.0f);
            if (auto* p = rig.panelTitled ("Oscillator 1"))
                p->setMoreExpanded (true);
            rig.shot ("osc-chips-oct-sem-" + n, sz.w, sz.h, true, Page::osc);

            // sample view: zoomed, slices, loop pair, the loupe above the finger
            rig.setParam ("osc1.type", 1.0f);
            rig.setParam ("osc1.sample.slice_mode", 2.0f);
            rig.setParam ("osc1.sample.loop_start", 30.0f);
            rig.setParam ("osc1.sample.loop_end", 60.0f);
            giveSample (rig, 1, { 0.0f, 0.18f, 0.37f, 0.55f, 0.8f });
            if (auto* p = rig.panelTitled ("Oscillator 1"))
                p->setMoreExpanded (false);
            if (auto* view = rig.oscView (1))
            {
                view->refreshSliceCache();
                view->zoomSampleAt (view->waveArea().getCentreX(), 1.6f);
                const float y = view->waveArea().getCentreY();
                const float x = view->sampleNormToX (0.37f);
                view->mouseDown (mouse (*view, { x, y }, { x, y }, false));
                view->mouseDrag (mouse (*view, { x + 14.0f, y }, { x, y }, true));
                rig.shot ("sample-zoom-slices-loupe-" + n, sz.w, sz.h, false, Page::osc);
                view->mouseUp (mouse (*view, { x + 14.0f, y }, { x, y }, true));
                rig.setParam ("osc1.sample.slice_mode", 1.0f);
                rig.setParam ("osc1.sample.slice_threshold", 35.0f);
                rig.shot ("sample-threshold-line-" + n, sz.w, sz.h, false, Page::osc);
            }
        }
        {
            Rig rig (Page::mix, sz.w, sz.h);
            rig.shot ("mixer-strip-headers-pan-" + n, sz.w, sz.h, false, Page::mix);
        }
        {
            // wavetable: 3D view tilted and zoomed
            Rig rig (Page::osc, sz.w, sz.h);
            WavetablePanel panel (rig.editor->context());
            panel.build();
            panel.setBounds (0, 0, sz.w, panel.preferredHeight (sz.w));
            if (auto* v = panel.visual())
            {
                v->setViewMode (WavetableView::ViewMode::mode3D);
                TouchTracker::Gesture pan;
                pan.kind = TouchTracker::Kind::pan;
                pan.dy = static_cast<float> (v->getHeight()) * 0.2f;
                pan.dx = static_cast<float> (v->getWidth()) * 0.15f;
                v->handleTouchGesture (pan);
                const auto file = shotDir().getChildFile ("wavetable-3d-tilt-" + n + ".png");
                CHECK (writePng (panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f), file));
                std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
            }

            // the editor sheet: zoomed canvas, the spectrum with the NUDGE toggle, the thumbnail reorder cursor
            WavetableEditorSheet sheet (rig.editor->context(), makeTable (8));
            sheet.setSize (sz.w, sz.h);
            sheet.zoomWave (0.3f, 3.0f);
            sheet.setCurrentFrame (2);
            {
                const auto file = shotDir().getChildFile ("wt-editor-canvas-zoom-" + n + ".png");
                CHECK (writePng (sheet.createComponentSnapshot (sheet.getLocalBounds(), true, 1.0f), file));
                std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
            }
            if (auto* strip = sheet.thumbnailsComponent())
            {
                const auto& t = rig.editor->theme();
                const int step = juce::jmax (t.touch(), t.ipx (56)) + t.ipx (4);
                const float y = static_cast<float> (strip->getHeight()) * 0.5f;
                strip->mouseDown (mouse (*strip, { static_cast<float> (t.gap() + step / 2), y }, { static_cast<float> (t.gap() + step / 2), y }, false));
                sheet.thumbnailLongPress()->fire();
                strip->mouseDrag (mouse (*strip, { static_cast<float> (t.gap() + 3 * step), y }, { static_cast<float> (t.gap() + step / 2), y }, true));
                sheet.setMode (WavetableEditorSheet::Mode::spectrum);
                sheet.setNudgeAllPhases (true);
                const auto file = shotDir().getChildFile ("wt-editor-spectrum-nudge-reorder-" + n + ".png");
                CHECK (writePng (sheet.createComponentSnapshot (sheet.getLocalBounds(), true, 1.0f), file));
                std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
                strip->mouseUp (mouse (*strip, { static_cast<float> (t.gap() + 3 * step), y }, { static_cast<float> (t.gap() + step / 2), y }, true));
            }
        }
        {
            // LFO shape editor with the Brush tool, a marquee selection and the selected point bar
            Rig rig (Page::lfo, sz.w, sz.h);
            if (auto* ed = customEditor (rig))
            {
                ed->applyPreset (0);
                ed->setGrid (8, 4);
                ed->setTool (LfoShapeEditor::Tool::brush);
                ed->setBrush (1);
                drag (*ed, plotPos (*ed, 0.07f, 0.8f), plotPos (*ed, 0.45f, 0.4f));
                ed->setTool (LfoShapeEditor::Tool::point);
                ed->selectInRect (juce::Rectangle<float> (plotPos (*ed, 0.0f, 1.0f), plotPos (*ed, 0.6f, 0.0f)));
                ed->loopHere();
                rig.shot ("lfo-brush-marquee-pointbar-" + n, sz.w, sz.h, true, Page::lfo);
            }
        }
        {
            Rig rig (Page::env, sz.w, sz.h);
            if (auto* env = dynamic_cast<EnvelopePanel*> (rig.panelTitled ("Envelope 1 (Amp)")))
            {
                env->curveView().setZoomLocked (false);
                env->curveView().setZoom (3.0f);
            }
            rig.shot ("env-tabs-magnifier-" + n, sz.w, sz.h, false, Page::env);
        }
        {
            Rig rig (Page::glb, sz.w, sz.h);
            rig.proc.setRateAndBufferSizeDetails (48000.0, 512);
            rig.proc.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);
            for (int block = 0; block < 12; ++block)
            {
                buffer.clear();
                rig.proc.processBlock (buffer, midi);
                midi.clear();
            }
            rig.shot ("glb-scope-voices-" + n, sz.w, sz.h, true, Page::glb);
            if (auto* sp = dynamic_cast<GlbScopePanel*> (rig.anyPanelTitled ("Scope")))
            {
                sp->scope().setMode (ScopeView::Mode::fft);
                rig.shot ("glb-scope-fft-" + n, sz.w, sz.h, true, Page::glb);
            }
        }
        {
            Rig rig (Page::fx, sz.w, sz.h);
            if (auto* view = rig.find<FxRackView>())
            {
                const auto main = view->racks().rack[0];
                const auto from = view->cardBounds (main[1]).getCentre() + juce::Point<float> (6.0f, 0.0f);
                view->mouseDown (mouse (*view, from, from, false));
                view->longPress().fire();
                view->mouseDrag (mouse (*view, view->cardBounds (main[4]).getCentre(), from, true));
            }
            rig.shot ("fx-chain-armed-reorder-" + n, sz.w, sz.h, false, Page::fx);
        }
        {
            Rig rig (Page::mod, sz.w, sz.h);
            assignBySource (rig, 7, "filter1.cutoff", 40.0f);
            assignBySource (rig, 8, "filter1.resonance", -30.0f);
            if (auto* reorder = dynamic_cast<SlotReorder*> (rig.panelTitled ("Modulation Matrix")))
                reorder->refreshRows();
            rig.shot ("mod-matrix-row-handles-" + n, sz.w, sz.h, false, Page::mod);
        }
    }
}

TEST (uig_sub_waveform_picker_and_noise_sample_stepper)
{
    Rig rig (Page::osc, 390, 844);

    // 1. Sub waveform picker (row 02-27)
    auto* subPanel = rig.find<SubPanel>();
    CHECK (subPanel != nullptr);
    if (subPanel != nullptr)
    {
        // Check initial waveform is 0 (Sine)
        CHECK_EQ (static_cast<int> (rig.param ("sub.waveform") + 0.5f), 0);

        // Click waveform button 3 (SAW)
        subPanel->waveformButton (3).onClick();
        CHECK_EQ (static_cast<int> (rig.param ("sub.waveform") + 0.5f), 3);

        // Click waveform button 5 (PULSE)
        subPanel->waveformButton (5).onClick();
        CHECK_EQ (static_cast<int> (rig.param ("sub.waveform") + 0.5f), 5);

        // Click waveform button 0 (SIN)
        subPanel->waveformButton (0).onClick();
        CHECK_EQ (static_cast<int> (rig.param ("sub.waveform") + 0.5f), 0);

        // Touch target size >= 44pt
        const int minTouch = rig.editor->theme().touch();
        for (int i = 0; i < 6; ++i)
        {
            CHECK (subPanel->waveformButton (i).getWidth() >= minTouch);
            CHECK (subPanel->waveformButton (i).getHeight() >= minTouch);
        }
    }

    // 2. Noise sample stepper < > and name (row 02-28)
    auto* noisePanel = rig.find<NoisePanel>();
    CHECK (noisePanel != nullptr);
    if (noisePanel != nullptr)
    {
        // Initial noise sample is 0 (White Noise)
        CHECK_EQ (static_cast<int> (rig.param ("noise.sample") + 0.5f), 0);
        CHECK_EQ (noisePanel->sampleNameButton().getButtonText(), "White Noise");

        // Click next (>) -> 1 (Pink Noise)
        noisePanel->sampleNextButton().onClick();
        CHECK_EQ (static_cast<int> (rig.param ("noise.sample") + 0.5f), 1);
        CHECK_EQ (noisePanel->sampleNameButton().getButtonText(), "Pink Noise");

        // Click next (>) -> 2 (Brown Noise)
        noisePanel->sampleNextButton().onClick();
        CHECK_EQ (static_cast<int> (rig.param ("noise.sample") + 0.5f), 2);

        // Click prev (<) -> back to 1 (Pink Noise)
        noisePanel->samplePrevButton().onClick();
        CHECK_EQ (static_cast<int> (rig.param ("noise.sample") + 0.5f), 1);
        CHECK_EQ (noisePanel->sampleNameButton().getButtonText(), "Pink Noise");

        // Click prev (<) twice -> 0, then wraps to 8 (Atmosphere)
        noisePanel->samplePrevButton().onClick();
        CHECK_EQ (static_cast<int> (rig.param ("noise.sample") + 0.5f), 0);
        noisePanel->samplePrevButton().onClick();
        CHECK_EQ (static_cast<int> (rig.param ("noise.sample") + 0.5f), 8);
        CHECK_EQ (noisePanel->sampleNameButton().getButtonText(), "Atmosphere");

        // Touch targets >= 44pt
        const int minTouch = rig.editor->theme().touch();
        CHECK (noisePanel->samplePrevButton().getWidth() >= minTouch);
        CHECK (noisePanel->samplePrevButton().getHeight() >= minTouch);
        CHECK (noisePanel->sampleNextButton().getWidth() >= minTouch);
        CHECK (noisePanel->sampleNextButton().getHeight() >= minTouch);
        CHECK (noisePanel->sampleNameButton().getWidth() >= minTouch);
        CHECK (noisePanel->sampleNameButton().getHeight() >= minTouch);
    }
}

TEST (uig_sub_and_noise_panels_on_phone_and_tablet)
{
    struct Size { int w, h; const char* name; };
    for (const auto& sz : { Size { 390, 844, "phone" }, Size { 1024, 768, "tablet" } })
    {
        const juce::String n (sz.name);
        Rig rig (Page::osc, sz.w, sz.h);
        rig.setParam ("sub.on", 1.0f);
        rig.setParam ("sub.waveform", 3.0f);
        rig.setParam ("noise.on", 1.0f);
        rig.setParam ("noise.sample", 2.0f);
        auto* subPanel = rig.find<SubPanel>();
        auto* noisePanel = rig.find<NoisePanel>();
        CHECK (subPanel != nullptr && noisePanel != nullptr);
        if (subPanel == nullptr || noisePanel == nullptr)
            return;

        // the picker row: six segments side by side, every target >= 44 pt, none outside the panel, none on another
        const int minTouch = rig.editor->theme().touch();
        juce::Rectangle<int> previous;
        for (int i = 0; i < 6; ++i)
        {
            const auto b = subPanel->waveformButton (i).getBounds();
            CHECK (b.getWidth() >= minTouch && b.getHeight() >= minTouch);
            CHECK (subPanel->getLocalBounds().contains (b));
            CHECK (! b.intersects (previous));
            previous = b;
        }
        // the active segment follows the key
        subPanel->updateWaveformButtons();
        CHECK_EQ (subPanel->waveformParamHandle().plain(), 3.0f);

        // stepping the noise sample at the ends wraps; the key is always a whole sample number
        rig.setParam ("noise.sample", 8.0f);
        noisePanel->sampleNextButton().onClick();
        CHECK_EQ (static_cast<int> (rig.param ("noise.sample") + 0.5f), 0);
        noisePanel->samplePrevButton().onClick();
        CHECK_EQ (static_cast<int> (rig.param ("noise.sample") + 0.5f), 8);

        // the whole panel is reachable: the controls stay inside it
        for (auto* panel : { static_cast<Panel*> (subPanel), static_cast<Panel*> (noisePanel) })
        {
            panel->setMoreExpanded (true);
            CHECK (panel->getHeight() >= panel->preferredHeight (panel->getWidth()) - 1);
            CHECK (panel->getWidth() <= sz.w);
        }

        // direct manipulation on the view: a vertical drag moves the level, a horizontal one the pan
        rig.setParam ("sub.level", 40.0f);
        rig.setParam ("sub.pan", 0.0f);
        auto& view = subPanel->view();
        const auto centre = view.getLocalBounds().getCentre().toFloat();
        drag (view, centre, centre + juce::Point<float> (0.0f, -static_cast<float> (view.getHeight()) * 0.25f));
        CHECK (rig.param ("sub.level") > 45.0f);
        drag (view, centre, centre + juce::Point<float> (static_cast<float> (view.getWidth()) * 0.25f, 0.0f));
        CHECK (rig.param ("sub.pan") > 20.0f);

        rig.shot ("sub-noise-" + n, sz.w, sz.h, true, Page::osc);
    }
}

// GROUPA1 (WP-04): the new keys of the Sub and Noise panels on phone and tablet; the views follow them, the file buttons are touch sized
TEST (uig_wp04_sub_phase_and_noise_colour_controls_on_phone_and_tablet)
{
    struct Size { int w, h; const char* name; };
    for (const auto& sz : { Size { 390, 844, "phone" }, Size { 1024, 768, "tablet" } })
    {
        const juce::String n (sz.name);
        Rig rig (Page::osc, sz.w, sz.h);
        auto* subPanel = rig.find<SubPanel>();
        auto* noisePanel = rig.find<NoisePanel>();
        CHECK (subPanel != nullptr && noisePanel != nullptr);
        if (subPanel == nullptr || noisePanel == nullptr)
            continue;

        const auto has = [] (const std::vector<std::string>& keys, const char* key) { return std::find (keys.begin(), keys.end(), std::string (key)) != keys.end(); };
        for (const char* key : { "sub.phase", "sub.phase_contiguous" })
            CHECK (has (subPanel->keys(), key));
        for (const char* key : { "noise.type", "noise.bounce", "noise.stereo", "noise.filter" })
            CHECK (has (noisePanel->keys(), key));

        // the Sub picture starts at the phase and says CONT for the contiguous option
        rig.setParam ("sub.on", 1.0f);
        rig.setParam ("sub.waveform", 0.0f);
        rig.setParam ("sub.phase", 0.0f);
        auto& view = subPanel->view();
        view.setBounds (0, 0, juce::jmax (200, view.getWidth()), juce::jmax (80, view.getHeight()));
        const auto atZero = view.createComponentSnapshot (view.getLocalBounds());
        rig.setParam ("sub.phase", 25.0f);
        CHECK_NEAR (view.startPhase(), 0.25f, 1.0e-4f);
        const auto atQuarter = view.createComponentSnapshot (view.getLocalBounds());
        int differing = 0;
        for (int y = 0; y < atZero.getHeight(); ++y)
            for (int x = 0; x < atZero.getWidth(); ++x)
                differing += atZero.getPixelAt (x, y) != atQuarter.getPixelAt (x, y) ? 1 : 0;
        CHECK (differing > 50); // the sine starts a quarter cycle later: another picture
        CHECK (! view.phaseContiguous());
        rig.setParam ("sub.phase_contiguous", 1.0f);
        CHECK (view.phaseContiguous());

        // the file row of the noise panel: Load and Remove are touch sized, Remove waits for a sample, Load for a host that can ask for a file
        const int minTouch = rig.editor->theme().touch();
        CHECK (noisePanel->loadButton().getHeight() >= minTouch && noisePanel->loadButton().getWidth() >= minTouch);
        CHECK (noisePanel->removeButton().getHeight() >= minTouch && noisePanel->removeButton().getWidth() >= minTouch);
        CHECK (! noisePanel->removeButton().getBounds().intersects (noisePanel->loadButton().getBounds()));
        CHECK (! noisePanel->removeButton().isEnabled());
        // noise.type = Sample without a loaded file: the generator's name stays (the engine plays the generator)
        rig.setParam ("noise.type", 1.0f);
        CHECK (noisePanel->usesSample());
        CHECK_EQ (noisePanel->sampleNameButton().getButtonText(), juce::String ("White Noise"));
        rig.setParam ("noise.type", 0.0f);

        for (auto* panel : { static_cast<Panel*> (subPanel), static_cast<Panel*> (noisePanel) })
        {
            panel->setMoreExpanded (true);
            CHECK (panel->getHeight() >= panel->preferredHeight (panel->getWidth()) - 1);
            CHECK (panel->getWidth() <= sz.w);
        }
        rig.shot ("sub-noise-wp04-" + n, sz.w, sz.h, true, Page::osc);
    }
}

TEST (uig_wp04_new_keys_take_modulation_by_touch_where_they_are_continuous)
{
    Rig rig (Page::osc);
    const int s0 = assignBySource (rig, 7, "sub.phase", 40.0f);
    const int s1 = assignBySource (rig, 8, "noise.stereo", -30.0f);
    const int s2 = assignBySource (rig, 9, "noise.filter", 25.0f);
    CHECK (s0 >= 0 && s1 >= 0 && s2 >= 0 && s0 != s1 && s1 != s2);
    auto& m = rig.editor->context().assign.matrix();
    CHECK_EQ (static_cast<int> (m.get (modSlotParams (s0).destination)), static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (resolveKey ("sub.phase").index))));
    CHECK_EQ (static_cast<int> (m.get (modSlotParams (s2).destination)), static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (resolveKey ("noise.filter").index))));
    auto& flow = rig.editor->context().assign.flow();
    rig.editor->context().assign.tapSource (7);
    CHECK (! flow.isTarget (resolveKey ("sub.phase_contiguous").index));
    CHECK (! flow.isTarget (resolveKey ("noise.type").index));
    CHECK (! flow.isTarget (resolveKey ("noise.bounce").index));
}

// GROUPA1 (WP-09): the quick maps of the MPE pad set the keys (they only changed their own caption before), and the four keys are controls of the panel
TEST (uig_wp09_mpe_quick_maps_set_the_keys_on_phone_and_tablet)
{
    struct Size { int w, h; const char* name; };
    for (const auto& sz : { Size { 390, 844, "phone" }, Size { 1024, 768, "tablet" } })
    {
        const juce::String n (sz.name);
        Rig rig (Page::glb, sz.w, sz.h);
        Panel* mpe = rig.panelTitled ("MPE & Expression");
        CHECK (mpe != nullptr);
        if (mpe == nullptr)
            continue;
        const auto keys = mpe->keys();
        for (const char* key : { "master.mpe.xyz_macro", "master.mpe.y_modwheel", "master.mpe.bend_to_x", "master.mpe.y_bipolar" })
            CHECK (std::find (keys.begin(), keys.end(), std::string (key)) != keys.end());

        const auto button = [&] (const char* caption) -> juce::TextButton*
        {
            return rig.find<juce::TextButton> (*mpe, [caption] (juce::TextButton& b) { return b.getButtonText().startsWith (caption); });
        };
        auto* xyz = button ("XYZ");
        auto* yz = button ("YZ");
        auto* yWheel = button ("Y ->");
        CHECK (xyz != nullptr && yz != nullptr && yWheel != nullptr);
        if (xyz == nullptr || yz == nullptr || yWheel == nullptr)
            continue;
        const int minTouch = rig.editor->theme().touch();
        for (auto* b : { xyz, yz, yWheel })
            CHECK (b->getHeight() >= minTouch && b->getWidth() >= minTouch);

        CHECK_EQ (static_cast<int> (rig.param ("master.mpe.xyz_macro")), 0);
        xyz->onClick();
        CHECK_EQ (static_cast<int> (rig.param ("master.mpe.xyz_macro") + 0.5f), 1);
        CHECK (xyz->getToggleState() && ! yz->getToggleState());
        yz->onClick();
        CHECK_EQ (static_cast<int> (rig.param ("master.mpe.xyz_macro") + 0.5f), 2);
        CHECK (yz->getToggleState() && ! xyz->getToggleState());
        yz->onClick(); // a second tap switches the map off
        CHECK_EQ (static_cast<int> (rig.param ("master.mpe.xyz_macro") + 0.5f), 0);
        CHECK (! yz->getToggleState());
        yWheel->onClick();
        CHECK_EQ (static_cast<int> (rig.param ("master.mpe.y_modwheel") + 0.5f), 1);
        CHECK (yWheel->getToggleState());
        yWheel->onClick();
        CHECK_EQ (static_cast<int> (rig.param ("master.mpe.y_modwheel") + 0.5f), 0);

        // the key changed elsewhere (a preset, the generic control, automation) shows on the buttons
        rig.setParam ("master.mpe.xyz_macro", 1.0f);
        CHECK (xyz->getToggleState());
        rig.setParam ("master.mpe.xyz_macro", 0.0f);

        mpe->setMoreExpanded (true);
        CHECK (mpe->getHeight() >= mpe->preferredHeight (mpe->getWidth()) - 1);
        CHECK (mpe->getWidth() <= sz.w);
        rig.shot ("mpe-wp09-" + n, sz.w, sz.h, true, Page::glb);
    }
}

// GROUPB (WP-05): Ratio SRC, smooth interpolation and the slice destination are controls of the oscillator panel; the source list greys the oscillator itself and
// loops, the controls dim when their mode is not the one that reads them, and the slice knob takes a modulation source by touch
TEST (uig_wp05_ratio_source_smooth_interpolation_and_slice_destination)
{
    // the loop rule of the model
    CHECK (ratioSourceAllowed (1, 0, { 0, 0, 0 }) && ratioSourceAllowed (1, 4, { 0, 0, 0 }) && ratioSourceAllowed (1, 2, { 0, 0, 0 }));
    CHECK (! ratioSourceAllowed (2, 2, { 0, 0, 0 }) && ! ratioSourceAllowed (1, 5, { 0, 0, 0 }));
    CHECK (! ratioSourceAllowed (1, 2, { 0, 1, 0 }));       // osc 2 is tuned to osc 1: osc 1 may not be tuned to osc 2
    CHECK (ratioSourceAllowed (1, 3, { 0, 0, 2 }));         // osc 1 -> osc 3 -> osc 2 -> the note: a chain, no loop
    CHECK (ratioSourceAllowed (1, 3, { 0, 0, 4 }));         // osc 3 is tuned to the sub: fine
    CHECK (! ratioSourceAllowed (1, 3, { 0, 3, 2 }));       // osc 3 -> osc 2 -> osc 3

    struct Size { int w, h; const char* name; };
    for (const auto& sz : { Size { 390, 844, "phone" }, Size { 1024, 768, "tablet" } })
    {
        const juce::String n (sz.name);
        Rig rig (Page::osc, sz.w, sz.h);
        rig.editor->context().view->oscTab = 1; // oscillator 2
        rig.editor->panelHost().setPage (Page::osc);
        Panel* panel = rig.panelTitled ("Oscillator 2");
        CHECK (panel != nullptr);
        if (panel == nullptr)
            continue;
        const auto keys = panel->keys();
        for (const char* key : { "osc2.ratio_src", "osc2.smooth_interp", "osc2.sample.slice" })
            CHECK (std::find (keys.begin(), keys.end(), std::string (key)) != keys.end());
        panel->setMoreExpanded (true);
        const auto controlOf = [&] (const char* key) -> ParamControl*
        {
            for (const auto& c : panel->controls())
                if (c->key() == key)
                    return c.get();
            return nullptr;
        };
        auto* ratio = dynamic_cast<ParamChoice*> (controlOf ("osc2.ratio_src"));
        auto* smooth = dynamic_cast<ParamToggle*> (controlOf ("osc2.smooth_interp"));
        auto* slice = controlOf ("osc2.sample.slice");
        CHECK (ratio != nullptr && smooth != nullptr && slice != nullptr);
        if (ratio == nullptr || smooth == nullptr || slice == nullptr)
            continue;

        // dimming: the Ratio pitch mode reads the source, a wavetable reads the smooth switch, Play Single Slice reads the slice
        CHECK (ratio->getAlpha() < 0.9f);
        CHECK (slice->getAlpha() < 0.9f);
        CHECK (smooth->getAlpha() > 0.9f);
        rig.setParam ("osc2.pitch_mode", 2.0f);
        CHECK (ratio->getAlpha() > 0.9f);
        rig.setParam ("osc2.type", 1.0f);
        rig.setParam ("osc2.sample.slice_mode", 1.0f);
        rig.setParam ("osc2.sample.slice_play", 1.0f);
        CHECK (slice->getAlpha() > 0.9f);
        CHECK (smooth->getAlpha() < 0.9f);
        rig.setParam ("osc2.type", 0.0f);

        // the source list: not the oscillator itself, not a loop
        const auto enabled = [&] (const juce::PopupMenu& menu, int id)
        {
            juce::PopupMenu::MenuItemIterator it (menu);
            while (it.next())
                if (it.getItem().itemID == id)
                    return it.getItem().isEnabled;
            return false;
        };
        auto menu = ratio->buildOptionMenu();
        CHECK (enabled (menu, 1) && enabled (menu, 2) && ! enabled (menu, 3) && enabled (menu, 4) && enabled (menu, 5)); // Base, Osc 1, (Osc 2), Osc 3, Sub
        rig.setParam ("osc1.ratio_src", 2.0f);  // osc 1 is tuned to osc 2: osc 2 may not be tuned to osc 1
        menu = ratio->buildOptionMenu();
        CHECK (! enabled (menu, 2) && enabled (menu, 4));
        rig.setParam ("osc1.ratio_src", 0.0f);
        ratio->step (+1);                       // the arrow skips the greyed Osc 2 ... from Base: Osc 1
        CHECK_EQ (static_cast<int> (rig.param ("osc2.ratio_src") + 0.5f), 1);
        ratio->step (+1);                       // ... and from Osc 1 on to Osc 3
        CHECK_EQ (static_cast<int> (rig.param ("osc2.ratio_src") + 0.5f), 3);
        smooth->toggle();
        CHECK_EQ (static_cast<int> (rig.param ("osc2.smooth_interp") + 0.5f), 1);

        // every control keeps the touch size and fits the panel
        const int minTouch = rig.editor->theme().touch();
        for (auto* c : { static_cast<ParamControl*> (ratio), static_cast<ParamControl*> (smooth), slice })
            CHECK (c->getHeight() >= minTouch);
        CHECK (panel->getHeight() >= panel->preferredHeight (panel->getWidth()) - 1);
        CHECK (panel->getWidth() <= sz.w);

        // the slice destination takes a modulation source by touch
        const int slot = assignBySource (rig, 7, "osc2.sample.slice", 50.0f);
        CHECK (slot >= 0);
        auto& m = rig.editor->context().assign.matrix();
        CHECK_EQ (static_cast<int> (m.get (modSlotParams (slot).destination)), static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (resolveKey ("osc2.sample.slice").index))));
        rig.shot ("osc-wp05-" + n, sz.w, sz.h, true, Page::osc);
    }
}

TEST (uig_sub_and_noise_knobs_take_modulation_by_touch)
{
    Rig rig (Page::osc);
    const int s0 = assignBySource (rig, 7, "sub.level", 40.0f);
    const int s1 = assignBySource (rig, 8, "noise.pitch", -30.0f);
    const int s2 = assignBySource (rig, 9, "sub.bus1_send", 25.0f);
    CHECK (s0 >= 0 && s1 >= 0 && s2 >= 0 && s0 != s1 && s1 != s2);
    auto& m = rig.editor->context().assign.matrix();
    CHECK_EQ (static_cast<int> (m.get (modSlotParams (s0).destination)), static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (resolveKey ("sub.level").index))));
    CHECK_EQ (static_cast<int> (m.get (modSlotParams (s1).destination)), static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (resolveKey ("noise.pitch").index))));
    CHECK (approx (m.get (modSlotParams (s1).amount), -30.0f, 0.5f));
    // the switches and choices of the block are not modulation targets
    auto& flow = rig.editor->context().assign.flow();
    rig.editor->context().assign.tapSource (7);
    CHECK (! flow.isTarget (resolveKey ("sub.waveform").index));
    CHECK (! flow.isTarget (resolveKey ("noise.sample").index));
    CHECK (! flow.isTarget (resolveKey ("noise.loop").index));
    rig.editor->context().assign.exitAssignMode();
}

TEST (uig_pz_svf_ui_curve_editor_moves_rendered_response)
{
    Rig rig (Page::flt);
    auto* f1Panel = rig.panelTitled ("Filter 1");
    CHECK (f1Panel != nullptr);
    if (f1Panel == nullptr)
        return;

    auto* view = rig.find<FilterResponseView> (*f1Panel);
    CHECK (view != nullptr);
    if (view == nullptr)
        return;

    // Setup noise through Filter 1
    rig.setParam ("osc1.on", 0.0f);
    rig.setParam ("noise.on", 1.0f);
    rig.setParam ("noise.level", 100.0f);
    rig.setParam ("noise.destination", 0.0f); // Filter 1
    rig.setParam ("filter1.on", 1.0f);
    rig.setParam ("filter1.model", 21.0f); // PZ SVF
    rig.setParam ("filter1.cutoff", 1000.0f);
    rig.setParam ("filter1.resonance", 0.0f);

    rig.proc.setRateAndBufferSizeDetails (48000.0, 512);
    rig.proc.prepareToPlay (48000.0, 512);

    // Initial render with flat response
    juce::AudioBuffer<float> bufFlat (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    // Warm up
    for (int i = 0; i < 4; ++i)
    {
        bufFlat.clear();
        rig.proc.processBlock (bufFlat, midi);
        midi.clear();
    }
    float rmsFlat = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        bufFlat.clear();
        rig.proc.processBlock (bufFlat, midi);
        rmsFlat += bufFlat.getRMSLevel (0, 0, 512);
    }
    rmsFlat /= 8.0f;
    CHECK (rmsFlat > 1e-4f);

    // Drag point 1 (centre point at normX = 0.5f, normY = 36/54 ~ 0.667) up to 0.95 (+15.3 dB)
    const auto plotArea = view->getLocalBounds().toFloat().reduced (rig.editor->theme().px (6.0f), rig.editor->theme().px (8.0f));
    const float ptX = plotArea.getX() + 0.5f * plotArea.getWidth();
    const float ptY = plotArea.getBottom() - (36.0f / 54.0f) * plotArea.getHeight();
    const float targetY = plotArea.getBottom() - 0.95f * plotArea.getHeight();

    drag (*view, { ptX, ptY }, { ptX, targetY });

    // Verify content.filterCurves was updated through UiHost::setContent
    const auto& content = rig.proc.content();
    auto it = std::find_if (content.filterCurves.begin(), content.filterCurves.end(),
                            [] (const auto& c) { return c.filter == "filter1"; });
    CHECK (it != content.filterCurves.end());
    if (it != content.filterCurves.end())
    {
        CHECK (it->points.size() >= 3);
        // Centre point y should be around +15 dB
        CHECK (it->points[1].y > 10.0f);
    }

    // Render with boosted curve
    juce::AudioBuffer<float> bufBoosted (2, 512);
    float rmsBoosted = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        bufBoosted.clear();
        rig.proc.processBlock (bufBoosted, midi);
        rmsBoosted += bufBoosted.getRMSLevel (0, 0, 512);
    }
    rmsBoosted /= 8.0f;

    // Rendered response moved significantly!
    CHECK (std::fabs (rmsBoosted - rmsFlat) / rmsFlat > 0.05f);

    // Also test double-click adds a new point
    const float newPtX = plotArea.getX() + 0.25f * plotArea.getWidth();
    const float newPtY = plotArea.getBottom() - 0.2f * plotArea.getHeight(); // -25 dB
    doubleTap (*view, { newPtX, newPtY });
    const auto& contentAfterAdd = rig.proc.content();
    auto itAdd = std::find_if (contentAfterAdd.filterCurves.begin(), contentAfterAdd.filterCurves.end(),
                               [] (const auto& c) { return c.filter == "filter1"; });
    CHECK (itAdd != contentAfterAdd.filterCurves.end());
    if (itAdd != contentAfterAdd.filterCurves.end())
    {
        CHECK (itAdd->points.size() == 4);
    }

    // Test Reset button resets to 3 neutral points and updates content
    auto* resetBtn = rig.find<juce::TextButton> (*view, [] (juce::TextButton& b) { return b.getButtonText() == "Reset"; });
    CHECK (resetBtn != nullptr);
    if (resetBtn != nullptr)
    {
        resetBtn->onClick();
        const auto& contentAfterReset = rig.proc.content();
        auto itReset = std::find_if (contentAfterReset.filterCurves.begin(), contentAfterReset.filterCurves.end(),
                                     [] (const auto& c) { return c.filter == "filter1"; });
        CHECK (itReset != contentAfterReset.filterCurves.end());
        if (itReset != contentAfterReset.filterCurves.end())
        {
            CHECK (itReset->points.size() == 3);
            CHECK_NEAR (itReset->points[1].y, 0.0, 0.5);
        }
    }
}

TEST (uig_filter_hierarchical_catalog_and_var_labels)
{
    // Check catalog has all 80 Serum filter types
    const auto& catalog = getSerumFilterCatalog();
    CHECK_EQ (static_cast<int> (catalog.size()), 80);

    // Verify models 22..26 and their styles and Var labels
    // Model 22: Normal (12 styles)
    CHECK (std::string (getFilterModelName (22)) == "Normal");
    CHECK_EQ (static_cast<int> (getStylesForModel (22).size()), 12);
    CHECK (getFilterTypeNameForModelAndStyle (22, 0) == "MG Low 12");
    CHECK (getFilterTypeNameForModelAndStyle (22, 1) == "MG Low 24");
    CHECK (getFilterTypeNameForModelAndStyle (22, 2) == "Low 12");
    CHECK (getFilterTypeNameForModelAndStyle (22, 3) == "Low 24");
    CHECK (getFilterTypeNameForModelAndStyle (22, 7) == "Band 24");
    CHECK (getFilterTypeNameForModelAndStyle (22, 11) == "Notch 24");
    CHECK (std::string (getVarLabelForModel (22, 0)) == "FAT");

    // Model 23: Multi Morph (4 styles)
    CHECK (std::string (getFilterModelName (23)) == "Multi Morph");
    CHECK_EQ (static_cast<int> (getStylesForModel (23).size()), 4);
    CHECK (getFilterTypeNameForModelAndStyle (23, 0) == "LBH");
    CHECK (getFilterTypeNameForModelAndStyle (23, 1) == "LPH");
    CHECK (getFilterTypeNameForModelAndStyle (23, 3) == "BPN");
    CHECK (std::string (getVarLabelForModel (23, 0)) == "MORPH");

    // Model 24: Flanges (9 styles)
    CHECK (std::string (getFilterModelName (24)) == "Flanges");
    CHECK_EQ (static_cast<int> (getStylesForModel (24).size()), 9);
    CHECK (getFilterTypeNameForModelAndStyle (24, 0) == "Cmb L");
    CHECK (getFilterTypeNameForModelAndStyle (24, 4) == "Flg H");
    CHECK (getFilterTypeNameForModelAndStyle (24, 8) == "Phs HL");
    CHECK (std::string (getVarLabelForModel (24, 0)) == "LP FREQ");
    CHECK (std::string (getVarLabelForModel (24, 4)) == "HP FREQ");
    CHECK (std::string (getVarLabelForModel (24, 8)) == "HL WID");

    // Model 25: Misc (12 styles)
    CHECK (std::string (getFilterModelName (25)) == "Misc");
    CHECK_EQ (static_cast<int> (getStylesForModel (25).size()), 12);
    CHECK (getFilterTypeNameForModelAndStyle (25, 0) == "Low EQ 12");
    CHECK (getFilterTypeNameForModelAndStyle (25, 3) == "German LP");
    CHECK (getFilterTypeNameForModelAndStyle (25, 4) == "Formant-I");
    CHECK (getFilterTypeNameForModelAndStyle (25, 7) == "Bandreject");
    CHECK (getFilterTypeNameForModelAndStyle (25, 8) == "Dist.Comb 1 LP");
    CHECK (std::string (getVarLabelForModel (25, 0)) == "DB");
    CHECK (std::string (getVarLabelForModel (25, 3)) == "");
    CHECK (std::string (getVarLabelForModel (25, 4)) == "FORMNT");
    CHECK (std::string (getVarLabelForModel (25, 7)) == "WIDTH");
    CHECK (std::string (getVarLabelForModel (25, 8)) == "COMBFRQ");

    // Model 26: New (6 styles)
    CHECK (std::string (getFilterModelName (26)) == "New");
    CHECK_EQ (static_cast<int> (getStylesForModel (26).size()), 6);
    CHECK (getFilterTypeNameForModelAndStyle (26, 0) == "MG Ladder");
    CHECK (getFilterTypeNameForModelAndStyle (26, 1) == "Acid Ladder");
    CHECK (getFilterTypeNameForModelAndStyle (26, 2) == "MG Dirty");
    CHECK (getFilterTypeNameForModelAndStyle (26, 3) == "Comb 2");
    CHECK (getFilterTypeNameForModelAndStyle (26, 4) == "Exp MM");
    CHECK (getFilterTypeNameForModelAndStyle (26, 5) == "Exp BPF");
    CHECK (std::string (getVarLabelForModel (26, 0)) == "SMOOTH");
    CHECK (std::string (getVarLabelForModel (26, 2)) == "PAIN");
    CHECK (std::string (getVarLabelForModel (26, 3)) == "FRQ2");
    CHECK (std::string (getVarLabelForModel (26, 4)) == "MIX");
    CHECK (std::string (getVarLabelForModel (26, 5)) == "");

    // Verify in Panel UI that setting model/style updates button label
    Rig rig (Page::flt);
    rig.setParam ("filter1.model", 26.0f); // New
    rig.setParam ("filter1.style", 2.0f);  // MG Dirty
    auto* f1Panel = rig.panelTitled ("Filter 1");
    CHECK (f1Panel != nullptr);
    if (f1Panel != nullptr)
    {
        auto* modelBtn = rig.find<juce::TextButton> (*f1Panel, [] (juce::TextButton& b) { return b.getButtonText() == "MG Dirty"; });
        CHECK (modelBtn != nullptr);
    }
}

// ---- block B21 (warp-env): Swap Warps, the VAR fader of Sync Var, the ENV 1 buttons of the mixer channels -------------------------------

TEST (uig_swap_warps_exchanges_the_two_warp_slots)
{
    Rig rig;
    auto* panel = rig.panelTitled ("Oscillator 1");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    panel->setMoreExpanded (true);
    auto* warp = rig.find<ParamChoice> ([] (ParamChoice& c) { return c.key() == "osc1.warp.type"; });
    auto* warp2 = rig.find<ParamChoice> ([] (ParamChoice& c) { return c.key() == "osc1.warp2.type"; });
    CHECK (warp != nullptr && warp2 != nullptr);
    if (warp == nullptr || warp2 == nullptr)
        return;
    CHECK (warp->hasExtraAction() && warp2->hasExtraAction());
    CHECK (! warp2->choiceGroups().empty()); // the second slot has the same grouped list

    rig.setParam ("osc1.warp.type", 18.0f); // Flip
    rig.setParam ("osc1.warp.amount", 30.0f);
    rig.setParam ("osc1.warp.phase", 90.0f);
    rig.setParam ("osc1.warp.spread", 10.0f);
    rig.setParam ("osc1.warp2.type", 25.0f); // LPF
    rig.setParam ("osc1.warp2.amount", 70.0f);
    rig.setParam ("osc1.warp2.phase", 200.0f);
    rig.setParam ("osc1.warp2.spread", -20.0f);
    warp2->runExtraAction();
    CHECK_EQ (rig.param ("osc1.warp.type"), 25.0f);
    CHECK_EQ (rig.param ("osc1.warp2.type"), 18.0f);
    CHECK_NEAR (rig.param ("osc1.warp.amount"), 70.0f, 0.01);
    CHECK_NEAR (rig.param ("osc1.warp2.amount"), 30.0f, 0.01);
    CHECK_NEAR (rig.param ("osc1.warp.phase"), 200.0f, 0.5);
    CHECK_NEAR (rig.param ("osc1.warp2.phase"), 90.0f, 0.5);
    CHECK_NEAR (rig.param ("osc1.warp.spread"), -20.0f, 0.01);
    CHECK_NEAR (rig.param ("osc1.warp2.spread"), 10.0f, 0.01);
    warp->runExtraAction(); // twice is the original
    CHECK_EQ (rig.param ("osc1.warp.type"), 18.0f);
    CHECK_NEAR (rig.param ("osc1.warp2.amount"), 70.0f, 0.01);
    // the other oscillators are not touched
    CHECK_EQ (rig.param ("osc2.warp.type"), 0.0f);
}

TEST (uig_sync_var_names_the_warp_phase_knob_var)
{
    Rig rig;
    auto* panel = rig.panelTitled ("Oscillator 1");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    panel->setMoreExpanded (true);
    auto* phase = rig.find<ParamControl> ([] (ParamControl& c) { return c.key() == "osc1.warp.phase"; });
    auto* phase2 = rig.find<ParamControl> ([] (ParamControl& c) { return c.key() == "osc1.warp2.phase"; });
    CHECK (phase != nullptr && phase2 != nullptr);
    if (phase == nullptr || phase2 == nullptr)
        return;
    const juce::String original = phase->label();
    CHECK (original != "Var");
    rig.setParam ("osc1.warp.type", 11.0f); // Sync Var
    CHECK_EQ (phase->label(), juce::String ("Var"));
    CHECK_EQ (phase2->label(), original);
    rig.setParam ("osc1.warp2.type", 11.0f);
    CHECK_EQ (phase2->label(), juce::String ("Var"));
    rig.setParam ("osc1.warp.type", 12.0f);
    CHECK_EQ (phase->label(), original);
}

TEST (uig_env1_buttons_sit_on_the_mixer_and_grey_out_off_main_and_direct)
{
    Rig rig (Page::mix);
    auto* panel = rig.panelTitled ("Mixer");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    panel->setMoreExpanded (true);
    struct Channel { const char* env; const char* destination; float toFilter, toMain, toDirect, toNone; };
    const Channel channels[] = { { "osc1.env1", "osc1.destination", 0.0f, 3.0f, 4.0f, 6.0f }, { "osc2.env1", "osc2.destination", 0.0f, 3.0f, 4.0f, 6.0f },
                                 { "osc3.env1", "osc3.destination", 0.0f, 3.0f, 4.0f, 6.0f }, { "sub.env1", "sub.destination", 0.0f, 3.0f, 4.0f, 6.0f },
                                 { "noise.env1", "noise.destination", 0.0f, 3.0f, 4.0f, 6.0f },
                                 { "filter1.env1", "filter1.destination", 99.0f, 0.0f, 1.0f, 2.0f }, { "filter2.env1", "filter2.destination", 99.0f, 0.0f, 1.0f, 2.0f } };
    for (const Channel& c : channels)
    {
        auto* button = rig.find<ParamToggle> ([&] (ParamToggle& t) { return t.key() == c.env; });
        CHECK (button != nullptr);
        if (button == nullptr)
            continue;
        CHECK_EQ (rig.param (c.env), 1.0f); // On is today's sound
        if (c.toFilter != 99.0f)
        {
            rig.setParam (c.destination, c.toFilter);
            CHECK (button->getAlpha() < 0.9f);
        }
        rig.setParam (c.destination, c.toMain);
        CHECK (button->getAlpha() > 0.9f);
        rig.setParam (c.destination, c.toDirect);
        CHECK (button->getAlpha() > 0.9f);
        rig.setParam (c.destination, c.toNone);
        CHECK (button->getAlpha() < 0.9f);
        rig.setParam (c.destination, c.toMain);
        tap (*button, button->getLocalBounds().getCentre().toFloat());
        CHECK_EQ (rig.param (c.env), 0.0f);
        tap (*button, button->getLocalBounds().getCentre().toFloat());
        CHECK_EQ (rig.param (c.env), 1.0f);
    }
}

TEST (uig_filter_menu_reaches_every_serum_type_and_every_type_shows_its_name_and_var_label_in_all_three_slots)
{
    // the catalog (the hierarchical menu) reaches each of the 80 core types exactly once, under its name, in the family of the manual
    const auto& catalog = getSerumFilterCatalog();
    std::vector<int> reached (static_cast<size_t> (synthyboy::filters::kFilterTypeCount), 0);
    for (const auto& item : catalog)
    {
        CHECK (item.model >= synthyboy::filters::kFirstSerumModel && item.model <= synthyboy::filters::kLastSerumModel);
        CHECK (item.style >= 0 && item.style < synthyboy::filters::serumModelStyleCount (item.model));
        const int type = synthyboy::filters::serumFilterTypeOf (item.model, item.style);
        CHECK (std::string (item.name) == synthyboy::filters::getFilterTypeName (type));
        CHECK (std::string (item.family) == synthyboy::filters::getFilterFamilyName (type));
        if (type >= 0 && type < synthyboy::filters::kFilterTypeCount)
            ++reached[static_cast<size_t> (type)];
        // the Var label of the UI is the one the manual (and core/filters-serum) gives the type
        const std::string manual = synthyboy::filters::getFilterVarName (type);
        const std::string shown = getVarLabelForModel (item.model, item.style);
        if (item.model >= 22)
            CHECK (shown == (manual == "N/A" ? std::string() : manual));
    }
    for (const int n : reached)
        CHECK_EQ (n, 1);

    for (const char* prefix : { "filter1", "filter2", "fxfilter" })
    {
        Rig rig (Page::flt);
        const char* title = std::string (prefix) == "filter1" ? "Filter 1" : (std::string (prefix) == "filter2" ? "Filter 2" : "FX Filter");
        auto* panel = rig.panelTitled (title);
        CHECK (panel != nullptr);
        if (panel == nullptr)
            continue;
        const std::string p (prefix);
        for (int model = 22; model <= 26; ++model)
            for (int style = 0; style < static_cast<int> (getStylesForModel (model).size()); ++style)
            {
                rig.setParam ((p + ".model").c_str(), static_cast<float> (model));
                rig.setParam ((p + ".style").c_str(), static_cast<float> (style));
                const std::string name = getFilterTypeNameForModelAndStyle (model, style);
                auto* modelBtn = rig.find<juce::TextButton> (*panel, [&] (juce::TextButton& b) { return b.getButtonText() == juce::String (name); });
                CHECK (modelBtn != nullptr);
                const std::string label = getVarLabelForModel (model, style);
                auto* varControl = rig.find<ParamControl> (*panel, [&] (ParamControl& c) { return c.key() == p + ".var"; });
                CHECK (varControl != nullptr);
                if (varControl != nullptr)
                    CHECK (varControl->label() == juce::String (label.empty() ? "Var (N/A)" : label));
            }
    }
    // the classic models keep their own name on the type button
    Rig rig (Page::flt);
    rig.setParam ("filter1.model", 2.0f);
    rig.setParam ("filter1.style", 1.0f);
    auto* f1 = rig.panelTitled ("Filter 1");
    CHECK (f1 != nullptr);
    if (f1 != nullptr)
        CHECK (rig.find<juce::TextButton> (*f1, [] (juce::TextButton& b) { return b.getButtonText() == "Ladder"; }) != nullptr);
}

TEST (uig_filter_view_shows_the_measured_dsp_response_of_the_new_types)
{
    using synthyboy::ui::FilterResponseMeasure;
    FilterResponseMeasure m;
    // High 12 (model 22 style 4) at 1 kHz: stop band below, pass band above; Low 12 (style 2) the other way round
    const auto hpLow = m.at (100.0f, 22, 4, 1000.0f, 0.0f, 0.0f, 50.0f);
    const auto hpHigh = m.at (8000.0f, 22, 4, 1000.0f, 0.0f, 0.0f, 50.0f);
    CHECK (hpHigh.magnitudeDb > hpLow.magnitudeDb + 20.0f);
    CHECK (m.measureCount() == 1); // the same settings are measured once
    const auto lpLow = m.at (100.0f, 22, 2, 1000.0f, 0.0f, 0.0f, 50.0f);
    const auto lpHigh = m.at (8000.0f, 22, 2, 1000.0f, 0.0f, 0.0f, 50.0f);
    CHECK (lpLow.magnitudeDb > lpHigh.magnitudeDb + 20.0f);
    CHECK_EQ (m.measureCount(), 2);
    // the cutoff moves the curve, the resonance raises the peak
    const auto at1k = m.at (1000.0f, 22, 2, 1000.0f, 90.0f, 0.0f, 50.0f);
    const auto flat1k = m.at (1000.0f, 22, 2, 1000.0f, 0.0f, 0.0f, 50.0f);
    CHECK (at1k.magnitudeDb > flat1k.magnitudeDb + 6.0f);
    const auto low2k = m.at (2000.0f, 22, 2, 1000.0f, 0.0f, 0.0f, 50.0f);
    const auto moved = m.at (2000.0f, 22, 2, 4000.0f, 0.0f, 0.0f, 50.0f);
    CHECK (moved.magnitudeDb > low2k.magnitudeDb + 6.0f);
    // Bandreject (25, 7) has its notch at the cutoff
    const auto notch = m.at (1000.0f, 25, 7, 1000.0f, 0.0f, 0.0f, 50.0f);
    const auto away = m.at (4000.0f, 25, 7, 1000.0f, 0.0f, 0.0f, 50.0f);
    CHECK (away.magnitudeDb > notch.magnitudeDb + 6.0f);
    // every point of every one of the 43 types is a finite number
    for (int model = 22; model <= 26; ++model)
        for (int style = 0; style < static_cast<int> (getStylesForModel (model).size()); ++style)
        {
            FilterResponseMeasure each;
            each.at (1000.0f, model, style, 1000.0f, 100.0f, 100.0f, 100.0f);
            for (const auto& v : each.points())
                CHECK (std::isfinite (v.magnitudeDb) && std::isfinite (v.phaseRad));
        }
}

TEST (uig_pz_svf_curve_has_at_most_16_points_and_a_double_tap_on_a_point_deletes_it)
{
    PzSvfCurve curve;
    for (int i = 0; i < 40; ++i)
        curve.addPoint ({ 0.02f + 0.024f * static_cast<float> (i), 0.5f });
    CHECK_EQ (static_cast<int> (curve.getPoints().size()), synthyboy::kMaxFilterCurvePoints);
    CHECK_EQ (curve.addPoint ({ 0.5f, 0.5f }), -1);
    // moving a point across its neighbour keeps the order and returns the new index
    PzSvfCurve three;
    const int moved = three.setPoint (0, { 0.7f, 0.9f });
    CHECK_EQ (moved, 1);
    CHECK (three.getPoints()[0].x <= three.getPoints()[1].x && three.getPoints()[1].x <= three.getPoints()[2].x);

    Rig rig (Page::flt);
    rig.setParam ("filter2.model", 21.0f);
    auto* panel = rig.panelTitled ("Filter 2");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    auto* view = rig.find<FilterResponseView> (*panel);
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    const auto plot = view->getLocalBounds().toFloat().reduced (rig.editor->theme().px (6.0f), rig.editor->theme().px (8.0f));
    const juce::Point<float> where (plot.getX() + 0.25f * plot.getWidth(), plot.getBottom() - 0.8f * plot.getHeight());
    doubleTap (*view, where);
    auto count = [&]
    {
        for (const auto& c : rig.proc.content().filterCurves)
            if (c.filter == "filter2")
                return static_cast<int> (c.points.size());
        return -1;
    };
    CHECK_EQ (count(), 4);
    doubleTap (*view, where); // on the new point: deleted again
    CHECK_EQ (count(), 3);
    // a curve of Filter 2 does not touch Filter 1
    for (const auto& c : rig.proc.content().filterCurves)
        CHECK (c.filter != "filter1");
    CHECK (synthyboy::validateContent (rig.proc.content()));
}

TEST (uig_filter_type_picker_sheet_sets_model_and_style_of_each_of_the_80_types_in_each_slot)
{
    Rig rig (Page::flt);
    auto& ctx = rig.editor->context();
    for (const char* prefix : { "filter1", "filter2", "fxfilter" })
    {
        const std::string p (prefix);
        ParamHandle model (ctx, p + ".model"), style (ctx, p + ".style"), blend (ctx, p + ".blend");
        FilterTypePickerSheet sheet (ctx, model, style, blend);
        sheet.setSize (sheet.preferredWidth(), sheet.preferredHeight());
        rig.setParam ((p + ".blend").c_str(), 37.0f);
        int taps = 0;
        for (const auto& item : getSerumFilterCatalog())
        {
            auto* button = rig.find<juce::TextButton> (sheet, [&] (juce::TextButton& b) { return b.getButtonText() == item.name; });
            CHECK (button != nullptr);
            if (button == nullptr)
                continue;
            rig.setParam ((p + ".model").c_str(), 0.0f);
            rig.setParam ((p + ".style").c_str(), 0.0f);
            button->onClick();
            ++taps;
            CHECK_EQ (static_cast<int> (rig.param ((p + ".model").c_str())), item.model);
            CHECK_EQ (static_cast<int> (rig.param ((p + ".style").c_str())), item.style);
            if (item.model >= 22) // these types have no Blend: the knob is left alone
                CHECK_NEAR (rig.param ((p + ".blend").c_str()), 37.0f, 0.5f);
            rig.setParam ((p + ".blend").c_str(), 37.0f);
        }
        CHECK_EQ (taps, 80);
    }
}

TEST (uig_zz_shutdown)
{
    delete juceInitGaps;
    juceInitGaps = nullptr;
    CHECK (true);
}
