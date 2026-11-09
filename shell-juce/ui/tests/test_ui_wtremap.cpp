// SynthyBoy - UI tests of block B16 (WTREMAP): the remap graph of the oscillators (row 02-14), the FX LEVEL knob of the effect panels and the Clean
// Mode switch of the filter panels, plus the phone / tablet screenshots of the changed pages. The last tests belong to block B20 (MODAMT): the
// per-connection modulation curve editor and the amount destinations of the matrix.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "ui/panels/ModLogic.h"
#include "ui/panels/RemapEditor.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInitRemap = new juce::ScopedJuceInitialiser_GUI();

juce::File shotDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_UI_SHOTS_DIR", {});
    return juce::File (env.isNotEmpty() ? env : juce::String ("/tmp/ui-shots")).getChildFile ("wtremap");
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

    Panel* panelTitled (const juce::String& title)
    {
        for (const auto& p : editor->panelHost().allPanels())
            if (p->title() == title)
                return p.get();
        return nullptr;
    }

    void shot (const juce::String& name, int w, int h, Page page)
    {
        editor->setSize (w, h);
        editor->panelHost().setPage (page);
        const auto file = shotDir().getChildFile (name + ".png");
        CHECK (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), file));
        std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
    }
};

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

const ContentRemap* remapOf (const StateContent& c, int osc, int slot)
{
    for (const auto& r : c.remaps)
        if (r.osc == osc && r.slot == slot)
            return &r;
    return nullptr;
}

bool hasControl (Panel& panel, const std::string& key)
{
    for (const auto& c : panel.controls())
        if (c->key() == key)
            return true;
    return false;
}
} // namespace

TEST (uiw_remap_panel_follows_the_oscillator_chip_and_has_its_four_keys)
{
    Rig rig;
    auto* panel = rig.panelTitled ("Oscillator 1 Remap");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    const auto keys = panel->keys();
    CHECK_EQ (keys.size(), static_cast<size_t> (4));
    for (const auto& k : { "osc1.remap.on", "osc1.remap.slot", "osc1.remap.rise", "osc1.remap.fall" })
        CHECK (hasControl (*panel, k));
    CHECK (! panel->hiddenInLayout());
    CHECK (rig.panelTitled ("Oscillator 2 Remap")->hiddenInLayout()); // only the chosen oscillator takes part in the layout
    CHECK (rig.panelTitled ("Oscillator 3 Remap")->hiddenInLayout());
    CHECK (findChildOfType<RemapEditor> (*panel) != nullptr);

    rig.editor->setSize (1024, 768);
    CHECK (findChildOfType<RemapEditor> (*panel) != nullptr);
}

TEST (uiw_remap_graph_edits_reach_the_host_content_through_gestures)
{
    Rig rig;
    auto* panel = rig.panelTitled ("Oscillator 1 Remap");
    CHECK (panel != nullptr);
    auto* ed = panel != nullptr ? findChildOfType<RemapEditor> (*panel) : nullptr;
    CHECK (ed != nullptr);
    if (ed == nullptr)
        return;

    // nothing drawn: the identity, nothing stored
    CHECK (! ed->isDrawn());
    CHECK_EQ (ed->slot(), 1);
    CHECK_EQ (ed->remap().points.size(), static_cast<size_t> (2));
    CHECK (remapOf (rig.proc.content(), 1, 1) == nullptr);
    CHECK_NEAR (ed->evaluateAt (0.3f), 0.3f, 1.0e-4f);

    const auto pos = [ed] (float x, float y)
    {
        const auto plot = ed->plotBounds();
        return juce::Point<float> (plot.getX() + x * plot.getWidth(), plot.getBottom() - y * plot.getHeight());
    };

    // double-tap on the line adds a point on the line (the shape does not change) and the entry reaches the host
    doubleTap (*ed, pos (0.2f, 0.2f)); // not at 0.5: the curve diamond of the straight segment sits there
    CHECK_EQ (ed->remap().points.size(), static_cast<size_t> (3));
    CHECK (ed->isDrawn());
    CHECK_EQ (ed->selectedPoint(), 1);
    CHECK_NEAR (ed->evaluateAt (0.2f), 0.2f, 1.0e-3f);
    {
        const auto content = rig.proc.content();
        const auto* r = remapOf (content, 1, 1);
        CHECK (r != nullptr);
        if (r != nullptr)
            CHECK_EQ (r->points.size(), static_cast<size_t> (3));
        CHECK (validateContent (content));
    }

    // drag it up: one drag is one undo step; the host has the new position at once
    const auto p1 = ed->pointPosition (1);
    drag (*ed, p1, p1 + juce::Point<float> (0.0f, -40.0f));
    const auto moved = ed->remap();
    CHECK (moved.points[1].y > 0.35f);
    CHECK (remapOf (rig.proc.content(), 1, 1)->points == moved.points);
    CHECK (ed->evaluateAt (0.2f) > 0.35f);
    ed->undo();
    CHECK_NEAR (ed->remap().points[1].y, 0.2f, 1.0e-3f);
    ed->redo();
    CHECK (ed->remap().points[1] == moved.points[1]);

    // end points move vertically only
    CHECK (ed->movePoint (0, 0.4f, 0.2f));
    CHECK_EQ (ed->remap().points.front().x, 0.0f);
    CHECK_NEAR (ed->remap().points.front().y, 0.2f, 1.0e-6f);
    CHECK (ed->movePoint (2, 0.2f, 0.8f));
    CHECK_EQ (ed->remap().points.back().x, 1.0f);

    // the curve power: dragging the diamond bends the segment
    const float powerBefore = ed->remap().points[0].power;
    const auto handle = ed->handlePosition (0);
    drag (*ed, handle, handle + juce::Point<float> (0.0f, -30.0f));
    CHECK (! juce::exactlyEqual (ed->remap().points[0].power, powerBefore));
    doubleTap (*ed, ed->handlePosition (0));
    CHECK_EQ (ed->remap().points[0].power, 0.0f); // double-tap the diamond: straight

    // delete the point (the button is the alternative to the double-tap)
    CHECK (ed->deletePoint (1));
    CHECK_EQ (ed->remap().points.size(), static_cast<size_t> (2));
    CHECK (! ed->deletePoint (0)); // the end points stay

    // the pencil paints freehand over the plot: one stroke = one undo step, 33 points
    ed->setPencil (true);
    CHECK (ed->pencilOn());
    const auto before = ed->remap();
    ed->mouseDown (mouse (*ed, pos (0.0f, 0.9f), pos (0.0f, 0.9f), false));
    for (int i = 1; i <= 10; ++i)
        ed->mouseDrag (mouse (*ed, pos (0.1f * static_cast<float> (i), 0.9f - 0.08f * static_cast<float> (i)), pos (0.0f, 0.9f), true));
    ed->mouseUp (mouse (*ed, pos (1.0f, 0.1f), pos (0.0f, 0.9f), true));
    CHECK_EQ (ed->remap().points.size(), static_cast<size_t> (33));
    CHECK_NEAR (ed->evaluateAt (0.0f), 0.9f, 0.02f);
    CHECK_NEAR (ed->evaluateAt (1.0f), 0.1f, 0.02f);
    CHECK_NEAR (ed->evaluateAt (0.5f), 0.5f, 0.05f);
    CHECK (remapOf (rig.proc.content(), 1, 1)->points == ed->remap().points);
    ed->undo();
    CHECK (ed->remap().points == before.points);
    ed->setPencil (false);

    // presets
    for (int i = 0; i < RemapEditor::kNumPresets; ++i)
    {
        ed->applyPreset (i);
        CHECK (validateContent (rig.proc.content()));
        CHECK (ed->remap().points.size() >= 2);
    }
    ed->applyPreset (1); // Reverse
    CHECK_NEAR (ed->evaluateAt (0.25f), 0.75f, 1.0e-3f);
    ed->applyPreset (5); // Steps 4
    CHECK_NEAR (ed->evaluateAt (0.1f), 0.0f, 1.0e-3f);
    CHECK_NEAR (ed->evaluateAt (0.9f), 1.0f, 1.0e-3f);

    // Reset: back to the identity, the entry leaves the content
    ed->resetToIdentity();
    ed->commitNow();
    CHECK (! ed->isDrawn());
    CHECK (remapOf (rig.proc.content(), 1, 1) == nullptr);
    CHECK_NEAR (ed->evaluateAt (0.7f), 0.7f, 1.0e-4f);
    CHECK (ed->button ("Reset") != nullptr && ! ed->button ("Reset")->isEnabled());
}

TEST (uiw_remap_slot_key_picks_the_curve_and_the_curve_survives_the_host_state)
{
    juce::MemoryBlock state;
    {
        Rig rig;
        auto* panel = rig.panelTitled ("Oscillator 1 Remap");
        auto* ed = panel != nullptr ? findChildOfType<RemapEditor> (*panel) : nullptr;
        CHECK (ed != nullptr);
        if (ed == nullptr)
            return;
        ed->applyPreset (1); // slot 1: reverse
        ed->commitNow();
        rig.setParam ("osc1.remap.slot", 2.0f); // Remap 3
        ed->reloadFromHost();
        CHECK_EQ (ed->slot(), 3);
        CHECK (! ed->isDrawn()); // nothing drawn there: the identity
        CHECK_NEAR (ed->evaluateAt (0.2f), 0.2f, 1.0e-4f);
        ed->applyPreset (3); // slot 3: ease out
        ed->commitNow();
        CHECK (remapOf (rig.proc.content(), 1, 1) != nullptr);
        CHECK (remapOf (rig.proc.content(), 1, 3) != nullptr);
        rig.setParam ("osc1.remap.slot", 0.0f);
        ed->reloadFromHost();
        CHECK_EQ (ed->slot(), 1);
        CHECK_NEAR (ed->evaluateAt (0.25f), 0.75f, 1.0e-3f);
        rig.setParam ("osc1.remap.on", 1.0f);
        rig.proc.getStateInformation (state);
    }
    {
        Rig rig;
        rig.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        const auto content = rig.proc.content();
        CHECK_EQ (content.remaps.size(), static_cast<size_t> (2));
        CHECK (remapOf (content, 1, 1) != nullptr && remapOf (content, 1, 3) != nullptr);
        CHECK_EQ (rig.proc.parameters().getRawParameterValue ("osc1.remap.on")->load(), 1.0f);
        auto* panel = rig.panelTitled ("Oscillator 1 Remap");
        auto* ed = panel != nullptr ? findChildOfType<RemapEditor> (*panel) : nullptr;
        CHECK (ed != nullptr);
        if (ed != nullptr)
        {
            ed->reloadFromHost();
            CHECK (ed->isDrawn());
            CHECK_NEAR (ed->evaluateAt (0.25f), 0.75f, 1.0e-3f);
        }
    }
}

TEST (uiw_fx_panels_have_the_level_knob_and_the_filter_panels_the_clean_mode_switch)
{
    Rig rig (Page::fx);
    const std::pair<const char*, const char*> modules[] = { { "Chorus", "chorus" }, { "Compressor", "compressor" }, { "Delay", "delay" },
                                                              { "Distortion", "distortion" }, { "Equalizer", "eq" }, { "Flanger", "flanger" },
                                                              { "Phaser", "phaser" }, { "Reverb", "reverb" } };
    for (const auto& m : modules)
    {
        auto* panel = rig.panelTitled (m.first);
        CHECK (panel != nullptr);
        if (panel == nullptr)
            continue;
        const std::string key = std::string (m.second) + ".level";
        const auto keys = panel->keys();
        CHECK (std::find (keys.begin(), keys.end(), key) != keys.end());
        CHECK (hasControl (*panel, key));
    }
    {
        auto* panel = rig.panelTitled ("FX Filter");
        CHECK (panel != nullptr);
        if (panel != nullptr)
            CHECK (hasControl (*panel, "fxfilter.clean_mode"));
    }

    Rig filters (Page::flt);
    for (const char* name : { "Filter 1", "Filter 2", "FX Filter" })
    {
        auto* panel = filters.panelTitled (name);
        CHECK (panel != nullptr);
        if (panel == nullptr)
            continue;
        const std::string key = std::string (name) == "Filter 1" ? "filter1.clean_mode" : (std::string (name) == "Filter 2" ? "filter2.clean_mode" : "fxfilter.clean_mode");
        CHECK (hasControl (*panel, key));
    }
}

TEST (uiw_screenshots_of_the_changed_pages)
{
    Rig rig;
    rig.setParam ("osc1.remap.on", 1.0f);
    auto* panel = rig.panelTitled ("Oscillator 1 Remap");
    auto* ed = panel != nullptr ? findChildOfType<RemapEditor> (*panel) : nullptr;
    CHECK (ed != nullptr);
    if (ed != nullptr)
    {
        ed->applyPreset (4); // S curve
        ed->commitNow();
    }
    rig.shot ("osc-remap-phone", 390, 844, Page::osc);
    rig.shot ("osc-remap-tablet", 1024, 768, Page::osc);
    rig.shot ("fx-tablet", 1024, 768, Page::fx);
}

// ---- block B20 (MODAMT): the per-connection modulation curve editor and the amount destinations of the matrix -------------------------

namespace
{
class ArrayMatrix final : public ModMatrixAccess
{
public:
    ArrayMatrix() : values (ParamValues::defaults()) {}
    float get (int i) const override { return values.plain[static_cast<size_t> (i)]; }
    void set (int i, float v) override { values.plain[static_cast<size_t> (i)] = v; }
    void beginGesture (int) override {}
    void endGesture (int) override {}
    ParamValues values;
};

int indexOfKey (const char* key)
{
    return resolveKey (key).index;
}

const ContentModCurve* modCurveOf (const StateContent& c, int slot)
{
    for (const auto& m : c.modCurves)
        if (m.slot == slot)
            return &m;
    return nullptr;
}

class ModCurvePanelAccess
{
public:
    static Panel* find (Rig& rig) { return rig.panelTitled ("Slot Curve & Remap"); }
};
} // namespace

TEST (uim_amount_destinations_are_found_and_loops_are_flagged)
{
    CHECK_EQ (modAmountSlotOfChoice (454), 0);
    CHECK_EQ (modAmountSlotOfChoice (517), 63);
    CHECK_EQ (modAmountSlotOfChoice (453), -1); // master.stereo_routing
    CHECK_EQ (modAmountSlotOfChoice (518), -1); // portamento time
    CHECK_EQ (modAmountSlotOfChoice (0), -1);

    ArrayMatrix m;
    const auto setSlot = [&] (int slot, int source, int dest)
    {
        const auto p = modSlotParams (slot);
        m.set (p.source, static_cast<float> (source));
        m.set (p.destination, static_cast<float> (dest));
    };
    // slot 1 -> amount of slot 2 -> amount of slot 3
    setSlot (0, 19, 455);
    setSlot (1, 20, 456);
    CHECK (! modAmountWouldLoop (m, 0, 455));
    CHECK (modAmountWouldLoop (m, 0, 454));  // its own amount
    CHECK (modAmountWouldLoop (m, 2, 454));  // slot 3 -> amount of slot 1 closes 1 -> 2 -> 3 -> 1
    CHECK (! modAmountWouldLoop (m, 2, 457)); // slot 3 -> amount of slot 4 is fine
    CHECK (! modAmountWouldLoop (m, 2, static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (indexOfKey ("filter1.cutoff"))))));
    // a bypassed link in the chain does not count
    m.set (modSlotParams (1).bypass, 1.0f);
    CHECK (! modAmountWouldLoop (m, 2, 454));

    // the matrix row badge uses the same check
    m.set (modSlotParams (1).bypass, 0.0f);
    setSlot (2, 21, 454);
    CHECK (modDetectFeedbackLoop (m, 2).isLoop);
    setSlot (2, 21, 458);
    CHECK (! modDetectFeedbackLoop (m, 2).isLoop);
    setSlot (0, 19, 454);
    CHECK (modDetectFeedbackLoop (m, 0).isLoop); // the slot modulates itself
}

TEST (uim_slot_curve_editor_draws_into_the_host_content_per_slot)
{
    Rig rig (Page::mod);
    auto* panel = ModCurvePanelAccess::find (rig);
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    auto* ed = findChildOfType<RemapEditor> (*panel);
    CHECK (ed != nullptr);
    if (ed == nullptr)
        return;
    CHECK (ed->isModCurve());
    CHECK (! ed->isVisible()); // the power graph is shown until "Custom" is chosen

    juce::TextButton* custom = nullptr;
    for (auto* c : panel->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c))
            if (b->getButtonText() == "Custom")
                custom = b;
    CHECK (custom != nullptr);
    if (custom == nullptr)
        return;
    custom->onClick();
    CHECK (ed->isVisible());
    CHECK_EQ (ed->slot(), 1);
    CHECK (! ed->isDrawn());
    CHECK_NEAR (ed->evaluateAt (0.3f), 0.3f, 1.0e-4f); // nothing drawn: the straight line, nothing stored
    CHECK (modCurveOf (rig.proc.content(), 1) == nullptr);

    const auto pos = [ed] (float x, float y)
    {
        const auto plot = ed->plotBounds();
        return juce::Point<float> (plot.getX() + x * plot.getWidth(), plot.getBottom() - y * plot.getHeight());
    };

    // a point on the line, dragged up: the curve of slot 1 reaches the content (and validates)
    doubleTap (*ed, pos (0.2f, 0.2f));
    CHECK_EQ (ed->remap().points.size(), static_cast<size_t> (3));
    const auto p1 = ed->pointPosition (1);
    drag (*ed, p1, p1 + juce::Point<float> (0.0f, -40.0f));
    {
        const auto content = rig.proc.content();
        const auto* mc = modCurveOf (content, 1);
        CHECK (mc != nullptr);
        if (mc != nullptr)
        {
            CHECK_EQ (mc->points.size(), static_cast<size_t> (3));
            CHECK (mc->points[1].y > 0.35f);
        }
        CHECK (content.remaps.empty()); // it is not a wavetable remap
        CHECK (validateContent (content));
    }
    ed->undo();
    CHECK_NEAR (ed->remap().points[1].y, 0.2f, 1.0e-3f);

    // another slot has its own (empty) curve; slot 1 keeps its entry
    ed->commitNow();
    ed->setModSlot (5);
    CHECK_EQ (ed->slot(), 5);
    CHECK (! ed->isDrawn());
    CHECK (modCurveOf (rig.proc.content(), 5) == nullptr);
    CHECK (modCurveOf (rig.proc.content(), 1) != nullptr);
    ed->applyPreset (1); // Reverse
    ed->commitNow();
    CHECK (modCurveOf (rig.proc.content(), 5) != nullptr);
    CHECK_NEAR (ed->evaluateAt (0.0f), 1.0f, 1.0e-3f);
    CHECK_NEAR (ed->evaluateAt (1.0f), 0.0f, 1.0e-3f);
    ed->resetToIdentity();
    ed->commitNow();
    CHECK (modCurveOf (rig.proc.content(), 5) == nullptr); // reset removes the entry: straight line again
    CHECK (modCurveOf (rig.proc.content(), 1) != nullptr);

    // a preset load with another content is picked up
    StateContent other;
    other.modCurves.push_back ({ 5, { ContentPoint (0.0f, 0.0f), ContentPoint (0.5f, 1.0f), ContentPoint (1.0f, 0.0f) } });
    rig.proc.setContent (other);
    ed->reloadFromHost();
    CHECK (ed->isDrawn());
    CHECK_EQ (ed->remap().points.size(), static_cast<size_t> (3));
    ed->setModSlot (1);
    CHECK (! ed->isDrawn());
}

TEST (uim_slot_curve_panel_fits_the_phone_and_the_tablet)
{
    for (const auto size : { juce::Point<int> (390, 844), juce::Point<int> (1024, 768) })
    {
        Rig rig (Page::mod, size.x, size.y);
        auto* panel = ModCurvePanelAccess::find (rig);
        CHECK (panel != nullptr);
        if (panel == nullptr)
            continue;
        auto* ed = findChildOfType<RemapEditor> (*panel);
        for (auto* c : panel->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "Custom")
                {
                    b->onClick();
                    CHECK (b->getHeight() >= 44 || b->getHeight() >= rig.editor->getHeight() / 20);
                }
        CHECK (ed != nullptr && ed->isVisible());
        if (ed != nullptr)
        {
            CHECK (ed->getWidth() > 0 && ed->getHeight() > 0);
            CHECK (ed->getBounds().getRight() <= panel->getWidth());
        }
    }
}

TEST (uim_master_stereo_routing_and_voice_override_are_on_the_voicing_panel)
{
    Rig rig (Page::glb);
    bool found = false;
    for (const auto& p : rig.editor->panelHost().allPanels())
        if (hasControl (*p, "master.stereo_routing") && hasControl (*p, "master.voice_override"))
            found = true;
    CHECK (found);
}

TEST (uim_screenshots_of_the_slot_curve_editor)
{
    Rig rig (Page::mod);
    auto* panel = ModCurvePanelAccess::find (rig);
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    for (auto* c : panel->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c))
            if (b->getButtonText() == "Custom")
                b->onClick();
    if (auto* ed = findChildOfType<RemapEditor> (*panel))
    {
        ed->applyPreset (4); // S curve
        ed->commitNow();
    }
    rig.shot ("mod-slot-curve-phone", 390, 844, Page::mod);
    rig.shot ("mod-slot-curve-tablet", 1024, 768, Page::mod);
}

// GROUPB (WP-08): RISE / FALL of the shown slot are two sliders of the slot curve panel, they write content.modCurves (with or without a drawn curve), the row of the
// matrix marks the connection, and "Edit Custom Curve" of the control sheet opens the free curve editor of the slot
TEST (uim_wp08_rise_fall_sliders_write_the_slot_record_and_the_sheet_opens_the_curve)
{
    struct Size { int w, h; const char* name; };
    for (const auto& sz : { Size { 390, 844, "phone" }, Size { 1024, 768, "tablet" } })
    {
        Rig rig (Page::mod, sz.w, sz.h);
        auto* panel = ModCurvePanelAccess::find (rig);
        CHECK (panel != nullptr);
        if (panel == nullptr)
            continue;
        std::vector<juce::Slider*> sliders;
        for (auto* c : panel->getChildren())
            if (auto* s = dynamic_cast<juce::Slider*> (c))
                sliders.push_back (s);
        CHECK_EQ (sliders.size(), size_t { 2 });
        if (sliders.size() != 2)
            continue;
        juce::Slider& rise = *sliders[0];
        juce::Slider& fall = *sliders[1];
        const int touch = rig.editor->theme().touch();
        CHECK (rise.getHeight() >= touch && fall.getHeight() >= touch);
        CHECK (rise.getRight() <= panel->getWidth() && fall.getRight() <= panel->getWidth());
        CHECK_EQ (rise.getValue(), 0.0);

        const auto recordOf = [&rig] (int slot) -> std::optional<ContentModCurve>
        {
            const auto content = rig.proc.content();   // by value: the record is copied out
            const auto* found = modCurveOf (content, slot);
            return found != nullptr ? std::optional<ContentModCurve> (*found) : std::nullopt;
        };
        rise.setValue (250.0, juce::sendNotificationSync);
        auto record = recordOf (1);
        CHECK (record.has_value() && record->points.empty() && std::fabs (record->riseMs - 250.0f) < 0.6f && record->fallMs == 0.0f);
        CHECK (validateContent (rig.proc.content()));
        fall.setValue (80.0, juce::sendNotificationSync);
        record = recordOf (1);
        CHECK (record.has_value() && std::fabs (record->fallMs - 80.0f) < 0.6f && std::fabs (record->riseMs - 250.0f) < 0.6f);

        // the shown slot owns its times: the next slot starts at 0, going back finds them again
        juce::TextButton* next = nullptr;
        for (auto* c : panel->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == ">")
                    next = b;
        CHECK (next != nullptr);
        if (next != nullptr)
        {
            next->onClick();
            CHECK_EQ (rise.getValue(), 0.0);
            CHECK (! recordOf (2).has_value());
            for (auto* c : panel->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c))
                    if (b->getButtonText() == "<")
                        b->onClick();
            CHECK (std::fabs (rise.getValue() - 250.0) < 0.6);
        }

        // a drawn curve and a slew share the record; taking the times away keeps the curve, a record with neither is removed
        {
            auto content = rig.proc.content();
            for (auto& m : content.modCurves)
                if (m.slot == 1)
                    m.points = { ContentPoint (0.0f, 0.0f, 0.0f), ContentPoint (1.0f, 1.0f, 0.0f) };
            CHECK (rig.proc.setContent (content));
            rise.setValue (0.0, juce::sendNotificationSync);
            fall.setValue (0.0, juce::sendNotificationSync);
            record = recordOf (1);
            CHECK (record.has_value() && record->points.size() == 2 && record->riseMs == 0.0f && record->fallMs == 0.0f);
            auto cleared = rig.proc.content();
            cleared.modCurves.clear();
            CHECK (rig.proc.setContent (cleared));
            rise.setValue (30.0, juce::sendNotificationSync);
            CHECK (recordOf (1).has_value());
            rise.setValue (0.0, juce::sendNotificationSync);
            CHECK (! recordOf (1).has_value());
        }

        // "Edit Custom Curve" on the control sheet: the matrix page, the slot, the free curve editor
        rig.editor->panelHost().setPage (Page::osc);
        CHECK (static_cast<bool> (rig.editor->context().view->openSlotCurve));
        rig.editor->context().view->openSlotCurve (4);
        auto* shown = ModCurvePanelAccess::find (rig);
        CHECK (shown != nullptr);
        if (shown != nullptr)
        {
            auto* ed = findChildOfType<RemapEditor> (*shown);
            CHECK (ed != nullptr && ed->isVisible() && ed->slot() == 5);
        }
        rig.shot ("mod-slot-curve-wp08-" + juce::String (sz.name), sz.w, sz.h, Page::mod);
    }
}
