// SynthyBoy - UI tests of the LFO page: tab chips, drag-to-copy, sync-mode dimming, the free-form shape editor (gestures, undo, host
// content round trip) and the screenshots of the changed pages (phone 390x844, tablet 1024x768).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "ui/panels/EnvCurveView.h"
#include "ui/panels/LfoShapeEditor.h"
#include "ui/panels/LfoTabsPanel.h"
#include "ui/panels/OscWaveView.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInitLfo = new juce::ScopedJuceInitialiser_GUI();

juce::File shotDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_UI_SHOTS_DIR", {});
    return juce::File (env.isNotEmpty() ? env : juce::String ("/tmp/ui-shots")).getChildFile ("follow");
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

    explicit Rig (int w = 390, int h = 844)
    {
        // ParameterAttachment calls back at once only on the message thread; the test thread plays that role
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
        editor->setSourceBarOpen (false);
        editor->setSize (w, h);
        editor->panelHost().setPage (Page::lfo);
        editor->pageTabs().select (Page::lfo, false);
    }

    void setParam (const char* key, float plain)
    {
        auto* p = proc.parameters().getParameter (key);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }
    float param (const char* key) { return proc.parameters().getRawParameterValue (key)->load(); }

    LfoTabsPanel* tabs()
    {
        for (const auto& p : editor->panelHost().allPanels())
            if (auto* t = dynamic_cast<LfoTabsPanel*> (p.get()))
                return t;
        return nullptr;
    }

    Panel* panelTitled (const juce::String& title)
    {
        for (auto* p : editor->panelHost().visiblePanels())
            if (p->title() == title)
                return p;
        return nullptr;
    }

    void shot (const juce::String& name, int w, int h, bool whole = false, Page page = Page::lfo)
    {
        editor->setSize (w, h);
        editor->panelHost().setPage (page); // the wide layout scrolls to the first LFO tile
        if (whole && w < 1000)
            editor->setSize (w, h + editor->panelHost().contentHeight());
        const auto file = shotDir().getChildFile (name + ".png");
        CHECK (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), file));
        std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
        editor->setSize (w, h);
        editor->panelHost().setPage (page);
    }
};

LfoShapeEditor* findEditor (juce::Component& c)
{
    if (auto* e = dynamic_cast<LfoShapeEditor*> (&c))
        return e;
    for (auto* child : c.getChildren())
        if (auto* e = findEditor (*child))
            return e;
    return nullptr;
}

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged, int clicks = 1)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                             juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), clicks, dragged);
}

void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown (mouse (c, from, from, false));
    c.mouseDrag (mouse (c, from + (to - from) * 0.5f, from, true));
    c.mouseDrag (mouse (c, to, from, true));
    c.mouseUp (mouse (c, to, from, true));
}

void doubleTap (juce::Component& c, juce::Point<float> p)
{
    c.mouseDown (mouse (c, p, p, false, 1));
    c.mouseUp (mouse (c, p, p, false, 1));
    c.mouseDown (mouse (c, p, p, false, 2));
    c.mouseDoubleClick (mouse (c, p, p, false, 2));
    c.mouseUp (mouse (c, p, p, false, 2));
}

const ContentLfoShape* shapeOf (const StateContent& c, int lfo)
{
    for (const auto& l : c.lfoShapes)
        if (l.lfo == lfo)
            return &l;
    return nullptr;
}
} // namespace

TEST (uil_tab_chips_show_one_lfo_at_a_time)
{
    Rig rig;
    CHECK (rig.editor != nullptr);
    auto* tabs = rig.tabs();
    CHECK (tabs != nullptr);
    if (tabs == nullptr)
        return;

    CHECK (rig.panelTitled ("LFO") != nullptr);
    CHECK (rig.panelTitled ("LFO 1") != nullptr);
    CHECK (rig.panelTitled ("LFO 2") == nullptr);
    CHECK_EQ (rig.editor->panelHost().visiblePanels().size(), static_cast<size_t> (2));

    // a real tap on the chip of LFO 4
    auto* chip = tabs->getComponentAt (tabs->chipBounds (3).getCentre());
    CHECK (chip != nullptr && chip != tabs);
    if (chip != nullptr)
    {
        const auto centre = chip->getLocalBounds().getCentre().toFloat();
        chip->mouseDown (mouse (*chip, centre, centre, false));
        chip->mouseUp (mouse (*chip, centre, centre, false));
    }
    CHECK_EQ (tabs->selectedTab(), 3);
    CHECK (rig.panelTitled ("LFO 4") != nullptr);
    CHECK (rig.panelTitled ("LFO 1") == nullptr);

    tabs->selectTab (10);
    CHECK (rig.panelTitled ("Random LFO 1") != nullptr);
    CHECK (rig.panelTitled ("LFO 4") == nullptr);
    tabs->selectTab (13);
    CHECK (rig.panelTitled ("Random LFO 4") != nullptr);
    tabs->selectTab (9);
    CHECK (rig.panelTitled ("LFO 10") != nullptr);

    // the wide layout shows the same: chips + the chosen LFO
    rig.editor->setSize (1280, 800);
    CHECK (rig.panelTitled ("LFO 10") != nullptr);
    CHECK (rig.panelTitled ("LFO 9") == nullptr);

    // every chip is a 44 point target at phone width
    rig.editor->setSize (390, 844);
    for (int i = 0; i < kNumLfoTabs; ++i)
    {
        CHECK (tabs->chipBounds (i).getWidth() >= rig.editor->theme().touch());
        CHECK (tabs->chipBounds (i).getHeight() >= rig.editor->theme().touch());
    }
}

TEST (uil_drag_a_chip_onto_another_copies_the_lfo)
{
    Rig rig;
    auto* tabs = rig.tabs();
    CHECK (tabs != nullptr);
    if (tabs == nullptr)
        return;

    rig.setParam ("lfo1.shape", 9);
    rig.setParam ("lfo1.rate", 3.5f);
    rig.setParam ("lfo1.sync_type", 2);
    rig.setParam ("lfo1.keytrack_tune", 25);
    rig.setParam ("lfo5.rate", 20.0f);

    auto* chip = tabs->getComponentAt (tabs->chipBounds (0).getCentre());
    CHECK (chip != nullptr && chip != tabs);
    if (chip == nullptr)
        return;
    const auto from = chip->getLocalBounds().getCentre().toFloat();
    const auto to = from + (tabs->chipBounds (4).getCentre() - tabs->chipBounds (0).getCentre()).toFloat();
    drag (*chip, from, to);

    CHECK_EQ (rig.param ("lfo5.shape"), 9.0f);
    CHECK (std::abs (rig.param ("lfo5.rate") - 3.5f) < 0.01f);
    CHECK_EQ (rig.param ("lfo5.sync_type"), 2.0f);
    CHECK (std::abs (rig.param ("lfo5.keytrack_tune") - 25.0f) < 0.01f);
    CHECK_EQ (rig.param ("lfo1.shape"), 9.0f); // the source is unchanged
    CHECK_EQ (tabs->selectedTab(), 0);           // a drag does not select

    // a random LFO chip cannot be dropped on a normal one, and the other way round
    CHECK (! tabs->copyTab (10, 2));
    CHECK (! tabs->copyTab (2, 11));
    CHECK (! tabs->copyTab (3, 3));
    rig.setParam ("rlfo1.rate", 7.0f);
    CHECK (tabs->copyTab (10, 12));
    CHECK (std::abs (rig.param ("rlfo3.rate") - 7.0f) < 0.01f);

    // a tap (no drag) still selects
    chip->mouseDown (mouse (*chip, from, from, false));
    chip->mouseUp (mouse (*chip, from, from, false));
    CHECK_EQ (tabs->selectedTab(), 0);
}

TEST (uil_sync_mode_dims_the_rate_and_key_follow_controls)
{
    Rig rig;
    auto* panel = rig.panelTitled ("LFO 1");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    const auto alphaOf = [panel] (const char* key)
    {
        for (const auto& c : panel->controls())
            if (c->key() == key)
                return c->getAlpha();
        return -1.0f;
    };

    rig.setParam ("lfo1.sync", 0); // Free (Hz)
    CHECK_EQ (alphaOf ("lfo1.rate"), 1.0f);
    CHECK (alphaOf ("lfo1.division") < 0.5f);
    CHECK (alphaOf ("lfo1.keytrack_transpose") < 0.5f);
    CHECK (alphaOf ("lfo1.keytrack_tune") < 0.5f);

    rig.setParam ("lfo1.sync", 2); // Tempo dotted
    CHECK (alphaOf ("lfo1.rate") < 0.5f);
    CHECK_EQ (alphaOf ("lfo1.division"), 1.0f);
    CHECK (alphaOf ("lfo1.keytrack_tune") < 0.5f);

    rig.setParam ("lfo1.sync", 4); // Key follow
    CHECK (alphaOf ("lfo1.rate") < 0.5f);
    CHECK (alphaOf ("lfo1.division") < 0.5f);
    CHECK_EQ (alphaOf ("lfo1.keytrack_transpose"), 1.0f);
    CHECK_EQ (alphaOf ("lfo1.keytrack_tune"), 1.0f);
    CHECK_EQ (alphaOf ("lfo1.shape"), 1.0f); // everything else is untouched
}

TEST (uil_shape_editor_edits_the_point_list_through_gestures)
{
    Rig rig;
    rig.setParam ("lfo1.shape", 12); // Custom: the editor replaces the read-only display
    auto* ed = findEditor (*rig.editor);
    CHECK (ed != nullptr);
    if (ed == nullptr)
        return;
    CHECK (ed->isActive());
    CHECK (ed->isShowing() || ed->isVisible());

    // activation wrote the sine it replaced (33 points) into the host content
    {
        const auto content = rig.proc.content();
        const auto* s = shapeOf (content, 1);
        CHECK (s != nullptr);
        if (s != nullptr)
            CHECK_EQ (s->points.size(), static_cast<size_t> (33));
        CHECK (validateContent (content));
    }

    ed->applyPreset (1); // triangle: (0,0) (0.5,1) (1,0)
    CHECK_EQ (ed->shape().points.size(), static_cast<size_t> (3));
    ed->commitNow();
    CHECK_EQ (shapeOf (rig.proc.content(), 1)->points.size(), static_cast<size_t> (3));

    const auto pos = [ed] (float x, float y)
    {
        const auto plot = ed->plotBounds();
        return juce::Point<float> (plot.getX() + x * plot.getWidth(), plot.getBottom() - y * plot.getHeight());
    };

    // double-tap on the line (not near a point or a curve diamond) adds a point
    doubleTap (*ed, pos (0.1f, 0.2f));
    CHECK_EQ (ed->shape().points.size(), static_cast<size_t> (4));
    CHECK_EQ (ed->selectedPoint(), 1);
    CHECK_EQ (shapeOf (rig.proc.content(), 1)->points.size(), static_cast<size_t> (4)); // reached the host at once

    // drag it: one drag is one undo step, x stays between the neighbours
    const auto undoBefore = ed->canUndo();
    CHECK (undoBefore);
    const auto p1 = ed->pointPosition (1);
    drag (*ed, p1, p1 + juce::Point<float> (20.0f, -25.0f));
    CHECK (ed->shape().points[1].x > 0.1f);
    CHECK (ed->shape().points[1].x < 0.5f);
    CHECK (ed->shape().points[1].y > 0.2f);
    const auto moved = ed->shape();
    CHECK (shapeOf (rig.proc.content(), 1)->points[1] == moved.points[1]);

    ed->undo(); // back to the added point where it was added
    CHECK_EQ (ed->shape().points.size(), static_cast<size_t> (4));
    CHECK (ed->shape().points[1].x < moved.points[1].x);
    ed->redo();
    CHECK (ed->shape().points[1] == moved.points[1]);

    // end points only move vertically
    CHECK (ed->movePoint (0, 0.4f, 0.7f));
    CHECK_EQ (ed->shape().points.front().x, 0.0f);
    CHECK (ed->movePoint (3, 0.2f, 0.3f));
    CHECK_EQ (ed->shape().points.back().x, 1.0f);

    // curve power: dragging the diamond of segment 1 (point 1 -> point 2) up bends it
    const float powerBefore = ed->shape().points[1].power;
    const auto handle = ed->handlePosition (1);
    drag (*ed, handle, handle + juce::Point<float> (0.0f, -30.0f));
    CHECK (! juce::exactlyEqual (ed->shape().points[1].power, powerBefore));
    CHECK (ed->shape().points[1].power >= -20.0f && ed->shape().points[1].power <= 20.0f);
    CHECK (ed->setSegmentPower (1, 99.0f));
    CHECK_EQ (ed->shape().points[1].power, 20.0f); // clamped to the content range
    doubleTap (*ed, ed->handlePosition (1));       // double-tap on the diamond = straight again
    CHECK_EQ (ed->shape().points[1].power, 0.0f);

    // delete: double-tap on a middle point; end points cannot be deleted
    const auto count = ed->shape().points.size();
    doubleTap (*ed, ed->pointPosition (1));
    CHECK_EQ (ed->shape().points.size(), count - 1);
    CHECK (! ed->deletePoint (0));
    CHECK (! ed->deletePoint (static_cast<int> (ed->shape().points.size()) - 1));

    // add / delete by button path, snap, smooth
    CHECK (ed->addPointAt (0.3f, 0.9f));
    CHECK (ed->deleteSelected());
    ed->setSmooth (true);
    CHECK (ed->shape().smooth);
    ed->setSnap (true);
    const auto before = ed->shape().points.size();
    const auto target = pos (0.31f, 0.52f);
    doubleTap (*ed, target);
    CHECK_EQ (ed->shape().points.size(), before + 1);
    {
        const auto& p = ed->shape().points[static_cast<size_t> (ed->selectedPoint())];
        CHECK (std::abs (p.x * 16.0f - std::round (p.x * 16.0f)) < 1.0e-4f); // snapped to the 16 column grid
        CHECK (std::abs (p.y * 8.0f - std::round (p.y * 8.0f)) < 1.0e-4f);
    }
    ed->setSnap (false);

    // Draw tool: paints 32 columns
    ed->setDrawTool (true);
    drag (*ed, pos (0.1f, 0.2f), pos (0.9f, 0.8f));
    CHECK_EQ (ed->shape().points.size(), static_cast<size_t> (33));
    CHECK (ed->shape().points[16].y > 0.4f && ed->shape().points[16].y < 0.6f);
    ed->setDrawTool (false);

    // presets replace the shape; undo brings the drawing back
    ed->applyPreset (4);
    CHECK_EQ (ed->shape().points.size(), static_cast<size_t> (4)); // square
    ed->undo();
    CHECK_EQ (ed->shape().points.size(), static_cast<size_t> (33));

    // the point limit
    ed->applyPreset (2);
    int guard = 0;
    while (ed->addPointAt (0.5f, 0.5f) && guard++ < 200) {}
    CHECK_EQ (ed->shape().points.size(), static_cast<size_t> (kMaxLfoShapePoints));
    CHECK (! ed->addPointAt (0.5f, 0.5f));

    ed->commitNow();
    CHECK (validateContent (rig.proc.content()));
}

TEST (uil_custom_shape_survives_the_host_state_and_other_changes_reload)
{
    ContentLfoShape expected;
    juce::MemoryBlock state;
    {
        Rig rig;
        rig.setParam ("lfo2.shape", 12);
        CHECK (shapeOf (rig.proc.content(), 2) != nullptr); // switching to Custom wrote the starting shape
        auto content = rig.proc.content();
        ContentLfoShape s = lfoShapePreset (2, 8); // random steps
        content.lfoShapes.erase (std::remove_if (content.lfoShapes.begin(), content.lfoShapes.end(), [] (const ContentLfoShape& l) { return l.lfo == 2; }),
                                 content.lfoShapes.end());
        content.lfoShapes.push_back (s);
        CHECK (rig.proc.setContent (content));
        expected = s;
        rig.proc.getStateInformation (state);

        // an invalid point list is refused and changes nothing
        auto bad = rig.proc.content();
        bad.lfoShapes.back().points.resize (1);
        CHECK (! rig.proc.setContent (bad));
        CHECK_EQ (shapeOf (rig.proc.content(), 2)->points.size(), expected.points.size());
    }
    {
        Rig rig;
        rig.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        const auto loaded = rig.proc.content();
        const auto* s = shapeOf (loaded, 2);
        CHECK (s != nullptr);
        if (s != nullptr)
        {
            CHECK_EQ (s->points.size(), expected.points.size());
            CHECK (s->points == expected.points);
        }
    }
}

// ---- gestures added by the gesture audit (docs/ui-design/UI-GESTURE-AUDIT.md) ----

template <typename T>
T* findChildOfType (juce::Component& c)
{
    if (auto* hit = dynamic_cast<T*> (&c))
        return hit;
    for (auto* child : c.getChildren())
        if (auto* hit = findChildOfType<T> (*child))
            return hit;
    return nullptr;
}

TEST (uil_mix_console_strip_drag_is_the_level_and_the_name_tap_is_the_power)
{
    Rig rig;
    rig.editor->panelHost().setPage (Page::mix);
    rig.editor->pageTabs().select (Page::mix, false);
    auto* console = findChildOfType<OscMixConsoleView> (*rig.editor);
    CHECK (console != nullptr);
    if (console == nullptr)
        return;
    CHECK (console->getWidth() > 0 && console->getHeight() > 0);

    rig.setParam ("osc1.level", 20.0f);
    const float before = rig.param ("osc1.level");
    const auto strip = console->stripBounds (0);
    const juce::Point<float> from (strip.getCentreX(), strip.getY() + strip.getHeight() * 0.5f);
    drag (*console, from, from + juce::Point<float> (0.0f, -strip.getHeight() * 0.3f));
    CHECK (rig.param ("osc1.level") > before); // dragging up raises osc1.level

    const float powerBefore = rig.param ("osc1.on");
    const juce::Point<float> name (strip.getCentreX(), strip.getBottom() - 4.0f);
    console->mouseDown (mouse (*console, name, name, false));
    console->mouseUp (mouse (*console, name, name, false));
    CHECK (! juce::exactlyEqual (rig.param ("osc1.on"), powerBefore));
    const juce::Point<float> f1 (console->stripBounds (4).getCentreX(), console->stripBounds (4).getBottom() - 4.0f);
    const float f1Level = rig.param ("filter1.level");
    console->mouseDown (mouse (*console, f1, f1, false));
    console->mouseUp (mouse (*console, f1, f1, false));
    CHECK (juce::exactlyEqual (rig.param ("filter1.level"), f1Level)); // a tap on a filter strip's foot changes nothing
}

TEST (uil_envelope_zoom_only_while_unlocked_and_eq_pinch_sets_the_resonance)
{
    Rig rig;
    rig.editor->panelHost().setPage (Page::env);
    auto* env = findChildOfType<EnvCurveView> (*rig.editor);
    CHECK (env != nullptr);
    if (env != nullptr)
    {
        CHECK (env->isZoomLocked());
        env->mouseMagnify (mouse (*env, { 5.0f, 5.0f }, { 5.0f, 5.0f }, false), 2.0f);
        CHECK_EQ (env->zoom(), 1.0f); // locked: the graph always fits
        env->setZoomLocked (false);
        env->mouseMagnify (mouse (*env, { 5.0f, 5.0f }, { 5.0f, 5.0f }, false), 2.0f);
        CHECK_EQ (env->zoom(), 2.0f);
        juce::MouseWheelDetails wheel;
        wheel.deltaX = 0.0f;
        wheel.deltaY = -0.5f;
        wheel.isReversed = false;
        wheel.isSmooth = false;
        wheel.isInertial = false;
        env->mouseWheelMove (mouse (*env, { 5.0f, 5.0f }, { 5.0f, 5.0f }, false), wheel);
        CHECK (env->zoom() < 2.0f);
        env->mouseMagnify (mouse (*env, { 5.0f, 5.0f }, { 5.0f, 5.0f }, false), 0.01f);
        CHECK_EQ (env->zoom(), 1.0f); // never zooms out beyond "fits"
        env->setZoomLocked (true);
        CHECK_EQ (env->zoom(), 1.0f);
    }

    rig.editor->panelHost().setPage (Page::fx);
    Panel* eq = nullptr;
    for (const auto& p : rig.editor->panelHost().allPanels())
        if (p->title() == "Equalizer")
            eq = p.get();
    CHECK (eq != nullptr);
    if (eq == nullptr)
        return;
    juce::Component* view = nullptr;
    for (auto* child : eq->getChildren())
        if (dynamic_cast<juce::Button*> (child) == nullptr && dynamic_cast<ParamControl*> (child) == nullptr && child->getWidth() > 50 && view == nullptr)
            view = child;
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    const auto sum = [&rig] { return rig.param ("eq.low.resonance") + rig.param ("eq.mid.resonance") + rig.param ("eq.high.resonance"); };
    const float before = sum();
    const juce::Point<float> p (static_cast<float> (view->getWidth()) * 0.5f, static_cast<float> (view->getHeight()) * 0.5f);
    view->mouseMagnify (mouse (*view, p, p, false), 1.5f);
    CHECK (sum() > before);
}

TEST (uil_mono_pill_toggles_the_polyphony)
{
    Rig rig;
    rig.editor->panelHost().setPage (Page::glb);
    Panel* porta = nullptr;
    for (const auto& p : rig.editor->panelHost().allPanels())
        if (p->title() == "Portamento / Legato")
            porta = p.get();
    CHECK (porta != nullptr);
    if (porta == nullptr)
        return;
    juce::TextButton* mono = nullptr;
    for (auto* child : porta->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText() == "MONO")
            mono = b;
    CHECK (mono != nullptr);
    if (mono == nullptr)
        return;
    CHECK (mono->getWidth() >= rig.editor->theme().touch() && mono->getHeight() >= rig.editor->theme().touch());

    rig.setParam ("master.polyphony", 12);
    mono->onClick();
    CHECK_EQ (rig.param ("master.polyphony"), 1.0f);
    CHECK (mono->getToggleState());
    mono->onClick();
    CHECK_EQ (rig.param ("master.polyphony"), 12.0f); // back to the voice count it had
    CHECK (! mono->getToggleState());
}

TEST (uil_zz_screenshots)
{
    struct Size { int w, h; const char* name; };
    const Size sizes[] = { { 390, 844, "phone-390x844" }, { 1024, 768, "tablet-1024x768" } };
    for (const auto& sz : sizes)
    {
        Rig rig (sz.w, sz.h);
        const juce::String n (sz.name);
        rig.shot ("lfo-tabs-" + n, sz.w, sz.h);

        rig.setParam ("lfo1.sync", 4);
        rig.shot ("lfo-keyfollow-dimming-" + n, sz.w, sz.h, true);
        rig.setParam ("lfo1.sync", 0);

        rig.tabs()->selectTab (10);
        rig.shot ("lfo-random-" + n, sz.w, sz.h);
        rig.tabs()->selectTab (0);

        rig.setParam ("lfo1.shape", 12);
        auto* ed = findEditor (*rig.editor);
        CHECK (ed != nullptr);
        if (ed != nullptr)
        {
            ed->applyPreset (1);
            ed->addPointAt (0.2f, 0.35f);
            ed->setSegmentPower (1, -6.0f);
            ed->setSegmentPower (2, 4.0f);
            ed->addPointAt (0.75f, 0.8f);
            ed->setSmooth (false);
        }
        rig.shot ("lfo-shape-editor-" + n, sz.w, sz.h, true);

        // the other pages the audit touched: mixer console strips, envelope (unlocked zoom), equalizer (resonance by pinch), portamento + MONO
        rig.editor->pageTabs().select (Page::mix, false);
        rig.shot ("mix-console-strips-" + n, sz.w, sz.h, false, Page::mix);
        rig.editor->pageTabs().select (Page::env, false);
        rig.shot ("env-" + n, sz.w, sz.h, false, Page::env);
        rig.editor->pageTabs().select (Page::fx, false);
        rig.shot ("fx-eq-" + n, sz.w, sz.h, true, Page::fx);
        rig.editor->pageTabs().select (Page::glb, false);
        rig.shot ("glb-portamento-mono-" + n, sz.w, sz.h, true, Page::glb);
        rig.editor->pageTabs().select (Page::lfo, false);
        rig.editor->panelHost().setPage (Page::lfo);

        // a drag of a chip in flight: highlight on the drop target and the hint
        if (auto* tabs = rig.tabs())
        {
            auto* chip = tabs->getComponentAt (tabs->chipBounds (0).getCentre());
            if (chip != nullptr && chip != tabs)
            {
                const auto from = chip->getLocalBounds().getCentre().toFloat();
                const auto to = from + (tabs->chipBounds (3).getCentre() - tabs->chipBounds (0).getCentre()).toFloat();
                chip->mouseDown (mouse (*chip, from, from, false));
                chip->mouseDrag (mouse (*chip, to, from, true));
                rig.shot ("lfo-chip-drag-" + n, sz.w, sz.h);
                chip->mouseUp (mouse (*chip, to, from, true));
            }
        }
    }
}

// ---- point modulation of the custom shape (audit row 05-22) ----

namespace
{
juce::File pointModShotDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_UI_SHOTS_DIR", {});
    return juce::File (env.isNotEmpty() ? env : juce::String ("/tmp/ui-shots")).getChildFile ("pointmod");
}

/** A rig whose LFO 1 is a custom flat shape (two points at 50 %), routed to the level of oscillator 1 at +100 %. */
struct PointModRig
{
    Rig rig;
    LfoShapeEditor* ed = nullptr;

    PointModRig()
    {
        rig.setParam ("lfo1.shape", 12);
        ed = findEditor (*rig.editor);
        CHECK (ed != nullptr);
        if (ed == nullptr)
            return;
        ed->applyPreset (99); // flat centre line: the LFO output is 0 until a point is modulated
        ed->commitNow();
        auto& assign = rig.editor->context().assign;
        assign.tapSource (pointModSourceChoice ("lfo1"));
        const int dest = resolveKey ("osc1.level").index;
        auto& flow = assign.flow();
        CHECK (flow.isTarget (dest));
        CHECK (flow.beginDrag (dest) == AssignFlow::BeginResult::ok);
        flow.setAmount (100.0f);
        flow.endDrag();
        assign.exitAssignMode();
        assign.refresh();
        rig.setParam ("lfo1.rate", 1.0f);
    }

    float render()
    {
        rig.proc.setRateAndBufferSizeDetails (48000.0, 512);
        rig.proc.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);
        double sum = 0.0;
        for (int block = 0; block < 40; ++block)
        {
            buffer.clear();
            rig.proc.processBlock (buffer, midi);
            midi.clear();
            if (block >= 8)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    sum += static_cast<double> (buffer.getSample (0, i)) * static_cast<double> (buffer.getSample (0, i));
        }
        return static_cast<float> (std::sqrt (sum / (32.0 * 512.0)));
    }

    /** Arms `source` and drags point `index` by (dx, dy) pixels like a finger. */
    void modulateByDrag (int index, const char* source, float dx, float dy)
    {
        auto& assign = rig.editor->context().assign;
        assign.tapSource (pointModSourceChoice (source));
        const auto from = ed->pointPosition (index);
        drag (*ed, from, from + juce::Point<float> (dx, dy));
        assign.exitAssignMode();
    }
};
} // namespace

TEST (uil_point_mod_drag_with_an_armed_source_writes_mod_y_and_mod_x_and_undoes)
{
    PointModRig m;
    auto* ed = m.ed;
    if (ed == nullptr)
        return;
    ed->applyPreset (1); // triangle: (0,0) (0.5,1) (1,0)
    ed->commitNow();
    const auto before = ed->shape();

    // no source armed: the same drag moves the point and writes no modulation
    {
        const auto p = ed->pointPosition (1);
        drag (*ed, p, p + juce::Point<float> (0.0f, 20.0f));
        CHECK (! ed->shape().points[1].modY.present && ed->shape().points[1].y < before.points[1].y);
        ed->undo();
        CHECK (ed->shape().points[1] == before.points[1]);
    }

    // armed: a vertical drag on the middle point sets Y, the point stays where it is
    m.modulateByDrag (1, "velocity", 0.0f, -ed->plotBounds().getHeight() * 0.25f);
    {
        const auto& pt = ed->shape().points[1];
        CHECK (pt.modY.present && pt.modY.source == "velocity");
        CHECK (pt.modY.amount > 0.2f && pt.modY.amount < 0.3f); // a quarter of the plot up = depth 0.25 (the bar follows the finger)
        CHECK (! pt.modX.present);
        CHECK_NEAR (pt.x, before.points[1].x, 1e-9);
        CHECK_NEAR (pt.y, before.points[1].y, 1e-9);
        const auto content = m.rig.proc.content();
        const auto* s = shapeOf (content, 1);
        CHECK (s != nullptr && s->points[1].modY.present && s->points[1].modY.source == "velocity"); // reached the host
        CHECK (validateContent (m.rig.proc.content()));
    }

    // a sideways drag with another source sets X and leaves Y alone
    m.modulateByDrag (1, "macro1", -ed->plotBounds().getWidth() * 0.1f, 2.0f);
    {
        const auto& pt = ed->shape().points[1];
        CHECK (pt.modX.present && pt.modX.source == "macro1");
        CHECK (pt.modX.amount < -0.07f && pt.modX.amount > -0.13f);
        CHECK (pt.modY.present && pt.modY.source == "velocity");
    }

    // end points take Y only: a sideways drag of the first point never writes X
    m.modulateByDrag (0, "env2", 40.0f, 0.0f);
    CHECK (! ed->shape().points[0].modX.present);

    // a tap with a source armed gives Y that source at 50 %
    {
        auto& assign = m.rig.editor->context().assign;
        assign.tapSource (pointModSourceChoice ("mod_wheel"));
        const auto p = ed->pointPosition (2);
        ed->mouseDown (mouse (*ed, p, p, false));
        ed->mouseUp (mouse (*ed, p, p, false));
        assign.exitAssignMode();
        CHECK (ed->shape().points[2].modY.present && ed->shape().points[2].modY.source == "mod_wheel");
        CHECK_NEAR (ed->shape().points[2].modY.amount, 0.5, 1e-6);
    }

    // one drag / tap = one undo step; undo walks back through all of them, redo forward again
    ed->undo();
    CHECK (! ed->shape().points[2].modY.present);
    ed->undo();
    CHECK (! ed->shape().points[0].modY.present && ed->shape().points[1].modX.present);
    ed->undo();
    CHECK (! ed->shape().points[1].modX.present && ed->shape().points[1].modY.present);
    ed->undo();
    CHECK (ed->shape().points == before.points);
    ed->commitNow();
    {
        const auto content = m.rig.proc.content();
        CHECK (! shapeOf (content, 1)->points[1].modY.present);
    }
    ed->redo();
    CHECK (ed->shape().points[1].modY.present);
}

TEST (uil_point_mod_sheet_actions_bypass_restore_and_remove)
{
    PointModRig m;
    auto* ed = m.ed;
    if (ed == nullptr)
        return;
    ed->applyPreset (1);
    CHECK (ed->assignPointMod (1, PointAxis::y, pointModSourceChoice ("env2"), 0.6f));
    CHECK (ed->assignPointMod (1, PointAxis::x, pointModSourceChoice ("lfo2"), -0.3f));
    CHECK (! ed->assignPointMod (0, PointAxis::x, pointModSourceChoice ("lfo2"), 0.3f)); // end point
    CHECK (! ed->assignPointMod (1, PointAxis::y, 0, 0.3f) && ! ed->assignPointMod (9, PointAxis::y, 1, 0.3f));

    // a held press on a point arms the long press that opens the sheet (the menu itself needs a display and is not shown here)
    const auto p = ed->pointPosition (1);
    ed->mouseDown (mouse (*ed, p, p, false));
    CHECK (ed->longPress().pending());
    ed->mouseUp (mouse (*ed, p, p, false));
    CHECK (! ed->longPress().pending());

    CHECK (ed->bypassPointMod (1, PointAxis::y, true));
    CHECK (ed->pointModBypassed (1, PointAxis::y) && ! ed->pointModBypassed (1, PointAxis::x));
    CHECK (ed->shape().points[1].modY.present && ed->shape().points[1].modY.source == "env2");
    CHECK_NEAR (ed->shape().points[1].modY.amount, 0.0, 1e-9);
    CHECK (! ed->bypassPointMod (1, PointAxis::y, true)); // already bypassed
    CHECK (ed->bypassPointMod (1, PointAxis::y, false));
    CHECK_NEAR (ed->shape().points[1].modY.amount, 0.6, 1e-6);

    CHECK (ed->clearPointMod (1, PointAxis::x));
    CHECK (! ed->shape().points[1].modX.present);
    CHECK (! ed->clearPointMod (1, PointAxis::x));
    CHECK (! shapeOf (m.rig.proc.content(), 1)->points[1].modX.present);

    // deleting the point takes its modulation along; undo brings it back
    CHECK (ed->deletePoint (1));
    CHECK_EQ (ed->shape().points.size(), static_cast<size_t> (2));
    ed->undo();
    CHECK (ed->shape().points[1].modY.present && ed->shape().points[1].modY.source == "env2");
    ed->undo(); // the remove of X
    CHECK (ed->shape().points[1].modX.present);
}

TEST (uil_point_mod_reaches_the_engine_the_render_differs)
{
    float plain = 0.0f, modulated = 0.0f, bypassed = 0.0f;
    {
        PointModRig m;
        plain = m.render();
    }
    {
        PointModRig m;
        m.modulateByDrag (0, "velocity", 0.0f, -m.ed->plotBounds().getHeight() * 0.4f);
        m.modulateByDrag (1, "velocity", 0.0f, -m.ed->plotBounds().getHeight() * 0.4f);
        CHECK (m.ed->shape().points[0].modY.present && m.ed->shape().points[1].modY.present);
        const auto content = m.rig.proc.content();
        const auto* s = shapeOf (content, 1);
        CHECK (s != nullptr && s->points[0].modY.present && s->points[1].modY.present);
        modulated = m.render();
    }
    {
        PointModRig m;
        m.modulateByDrag (0, "velocity", 0.0f, -m.ed->plotBounds().getHeight() * 0.4f);
        m.modulateByDrag (1, "velocity", 0.0f, -m.ed->plotBounds().getHeight() * 0.4f);
        CHECK (m.ed->bypassPointMod (0, PointAxis::y, true) && m.ed->bypassPointMod (1, PointAxis::y, true));
        bypassed = m.render();
    }
    std::printf ("    [POINT MOD] rms plain %.5f, modulated %.5f, bypassed %.5f\n", static_cast<double> (plain), static_cast<double> (modulated),
                 static_cast<double> (bypassed));
    CHECK (plain > 1.0e-4f);
    CHECK (std::abs (modulated - plain) > 0.002f * plain);
    CHECK_NEAR (bypassed, plain, 1.0e-6); // bypassed = the same sound as without
}

TEST (uil_point_mod_screenshots)
{
    struct Size { int w, h; const char* name; };
    for (const auto sz : { Size { 390, 844, "phone" }, Size { 1024, 768, "tablet" } })
    {
        PointModRig m;
        m.rig.editor->setSize (sz.w, sz.h);
        m.rig.editor->panelHost().setPage (Page::lfo);
        auto* ed = m.ed;
        if (ed == nullptr)
            return;
        ed->applyPreset (1);
        ed->assignPointMod (1, PointAxis::y, pointModSourceChoice ("velocity"), 0.45f);
        ed->assignPointMod (1, PointAxis::x, pointModSourceChoice ("macro1"), -0.2f);
        ed->assignPointMod (2, PointAxis::y, pointModSourceChoice ("env2"), 0.3f);
        ed->bypassPointMod (2, PointAxis::y, true);
        ed->assignPointMod (0, PointAxis::y, pointModSourceChoice ("lfo2"), -0.35f);
        ed->addPointAt (0.25f, 0.5f);
        const juce::String n = sz.name;
        auto snap = [&] (const juce::String& name)
        {
            m.rig.editor->setSize (sz.w, sz.h);
            if (sz.w < 1000)
                m.rig.editor->setSize (sz.w, sz.h + m.rig.editor->panelHost().contentHeight());
            CHECK (writePng (m.rig.editor->createComponentSnapshot (m.rig.editor->getLocalBounds(), true, 1.0f),
                             pointModShotDir().getChildFile (name + ".png")));
            m.rig.editor->setSize (sz.w, sz.h);
        };
        ed->selectInRect (juce::Rectangle<float> (ed->pointPosition (1), ed->pointPosition (1)).expanded (4.0f));
        snap ("pointmod-assigned-" + n);
        // a source armed: dashed target rings on the free points
        m.rig.editor->context().assign.tapSource (pointModSourceChoice ("velocity"));
        snap ("pointmod-armed-targets-" + n);
        m.rig.editor->context().assign.exitAssignMode();
    }
}

TEST (uil_zz_shutdown)
{
    delete juceInitLfo;
    juceInitLfo = nullptr;
    CHECK (true);
}
