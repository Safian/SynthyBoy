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
#include "ui/panels/LfoShapeEditor.h"
#include "ui/panels/LfoTabsPanel.h"
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

    void shot (const juce::String& name, int w, int h, bool whole = false)
    {
        editor->setSize (w, h);
        editor->panelHost().setPage (Page::lfo); // the wide layout scrolls to the first LFO tile
        if (whole && w < 1000)
            editor->setSize (w, h + editor->panelHost().contentHeight());
        const auto file = shotDir().getChildFile (name + ".png");
        CHECK (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), file));
        std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
        editor->setSize (w, h);
        editor->panelHost().setPage (Page::lfo);
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

TEST (uil_zz_shutdown)
{
    delete juceInitLfo;
    juceInitLfo = nullptr;
    CHECK (true);
}
