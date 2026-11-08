// SynthyBoy - UI tests: Components headless (no window, no display needed): controls bound to the real processor, assign by touch,
// A/B, keyboard to audio, touch target sizes, panel registry contract and the screenshots.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <typeinfo>
#include <vector>

#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "ui/Sheet.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
// message manager for the Components; released by the last test, so JUCE shuts down before the static destructors run
juce::ScopedJuceInitialiser_GUI* juceInit = new juce::ScopedJuceInitialiser_GUI();

/** The real processor, with the engine caps the test wants the UI to see. */
class CapsHost final : public UiHost
{
public:
    CapsHost (SynthyBoyProcessor& p, EngineCapsMask c) : inner (p), caps (c) {}
    juce::AudioProcessorValueTreeState& parameters() override { return inner.parameters(); }
    EngineCapsMask engineCaps() const noexcept override { return caps; }
    juce::MidiKeyboardState& keyboardState() override { return inner.keyboardState(); }
    AbCompare& abCompare() override { return inner.abCompare(); }
    void stepPreset (int d) override { inner.stepPreset (d); }
    juce::String presetName() const override { return inner.presetName(); }
    float outputPeakL() const noexcept override { return inner.outputPeakL(); }
    float outputPeakR() const noexcept override { return inner.outputPeakR(); }
    void injectMidiMessage (const juce::MidiMessage& msg) override { inner.injectMidiMessage (msg); }

private:
    SynthyBoyProcessor& inner;
    EngineCapsMask caps;
};

struct GestureCounter final : juce::AudioProcessorParameter::Listener
{
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
    int changes = 0, begins = 0, ends = 0;
};

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged, bool popup = false)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, popup ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::popupMenuClickModifier)
                                                : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1, dragged);
}

void tap (juce::Component& c, juce::Point<float> p)
{
    auto* knob = dynamic_cast<juce::Component*> (&c);
    knob->mouseDown (mouse (c, p, p, false));
    knob->mouseUp (mouse (c, p, p, false));
}

float plainOf (SynthyBoyProcessor& p, const char* key)
{
    return p.parameters().getRawParameterValue (key)->load();
}

void setPlain (SynthyBoyProcessor& p, const char* key, float v)
{
    auto* param = p.parameters().getParameter (key);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

/** Visible descendants that are interactive controls (buttons, param controls, chips). */
void collectTouchTargets (juce::Component& root, juce::Component& c, std::vector<juce::Component*>& out)
{
    if (! c.isVisible())
        return;
    if (dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<ParamControl*> (&c) != nullptr || dynamic_cast<ModSourceChip*> (&c) != nullptr)
        out.push_back (&c);
    for (auto* child : c.getChildren())
        collectTouchTargets (root, *child, out);
}

bool inClippedViewport (juce::Component& c)
{
    // controls scrolled out of the viewport and the horizontally scrolling chip / tab rows are not on screen
    for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (auto* vp = dynamic_cast<juce::Viewport*> (p))
            return ! vp->getLocalBounds().intersects (vp->getLocalArea (&c, c.getLocalBounds()));
    return false;
}

void checkTouchTargets (SynthyBoyEditor& editor, const char* what)
{
    sbtest::Context ctx (what);
    std::vector<juce::Component*> targets;
    for (auto* child : editor.getChildren())
        collectTouchTargets (editor, *child, targets);
    const int min = editor.theme().touch();
    int checked = 0;
    for (auto* c : targets)
    {
        if (inClippedViewport (*c) || c->getWidth() == 0 || c->getHeight() == 0)
            continue; // scrolled out, or the zero-width scroll buttons of the keyboard
        ++checked;
        const bool ok = c->getWidth() >= min && c->getHeight() >= min;
        if (! ok)
            std::printf ("    too small: %s %dx%d\n", typeid (*c).name(), c->getWidth(), c->getHeight());
        CHECK (ok);
    }
    CHECK (checked > 10);
}

juce::File screenshotDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_UI_SCREENSHOT_DIR", {});
    auto dir = env.isNotEmpty() ? juce::File (env) : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-screenshots");
    dir.createDirectory();
    return dir;
}

bool writePng (juce::Component& c, const juce::File& file)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    if (! image.isValid())
        return false;
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}
} // namespace

TEST (uir_panel_registry_contract)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    const auto& entries = PanelRegistry::instance().entries();
    CHECK (! entries.empty());
    for (const auto& e : entries)
    {
        sbtest::Context c (e.id.c_str());
        auto panel = e.create (ctx);
        panel->build();
        CHECK (panel->title().isNotEmpty());

        const auto keys = panel->keys();
        CHECK (! keys.empty());
        CHECK_EQ (keys.size(), panel->controls().size());
        for (size_t i = 0; i < keys.size() && i < panel->controls().size(); ++i)
        {
            CHECK (resolveKey (keys[i]).valid());          // never an invented key
            CHECK_EQ (keys[i], panel->controls()[i]->key()); // build() creates what keys() promises, in that order
        }
        // brief principle 1: at most 4 primary controls, the rest behind More
        CHECK (static_cast<int> (panel->controls().size()) - panel->numMoreControls() <= 4);
    }
}

TEST (uir_controls_bind_by_key_and_gestures_are_host_gestures)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    ParamKnob knob (ctx, "filter1.cutoff");
    const auto size = knob.preferredSize();
    knob.setSize (size.x, size.y);
    CHECK (size.x >= 44 && size.y >= 44);
    CHECK (knob.paramHandle().valid());
    CHECK_NEAR (knob.paramHandle().plain(), 12000.0, 1e-3);

    GestureCounter counter;
    auto* param = proc.parameters().getParameter ("filter1.cutoff");
    param->addListener (&counter);

    setPlain (proc, "filter1.cutoff", 1000.0f);
    // a drag up: one host gesture (begin ... end) with changes in between
    const juce::Point<float> down (static_cast<float> (size.x) * 0.5f, static_cast<float> (size.y) * 0.5f);
    knob.mouseDown (mouse (knob, down, down, false));
    for (int i = 1; i <= 5; ++i)
        knob.mouseDrag (mouse (knob, down - juce::Point<float> (0.0f, 10.0f * static_cast<float> (i)), down, true));
    knob.mouseUp (mouse (knob, down - juce::Point<float> (0.0f, 50.0f), down, true));
    CHECK_EQ (counter.begins, 1);
    CHECK_EQ (counter.ends, 1);
    CHECK (counter.changes >= 5);
    CHECK (plainOf (proc, "filter1.cutoff") > 1000.0f);

    // sliding sideways during the drag = the fine zone: the same dy changes the value less
    setPlain (proc, "filter1.cutoff", 1000.0f);
    const float n0 = param->getValue();
    knob.mouseDown (mouse (knob, down, down, false));
    knob.mouseDrag (mouse (knob, down + juce::Point<float> (0.0f, -20.0f), down, true));
    const float coarse = param->getValue() - n0;
    knob.mouseUp (mouse (knob, down, down, true));
    setPlain (proc, "filter1.cutoff", 1000.0f);
    knob.mouseDown (mouse (knob, down, down, false));
    knob.mouseDrag (mouse (knob, down + juce::Point<float> (theme.px (kFineZoneWidthPx) + 5.0f, -20.0f), down, true));
    const float fine = param->getValue() - n0;
    knob.mouseUp (mouse (knob, down, down, true));
    CHECK (coarse > 0.0f);
    CHECK (fine > 0.0f && fine < coarse * 0.5f);

    // double-tap = reset to the default (decision Q3); a single tap changes nothing
    setPlain (proc, "filter1.cutoff", 500.0f);
    tap (knob, down);
    CHECK_NEAR (plainOf (proc, "filter1.cutoff"), 500.0, 0.5);
    tap (knob, down);
    CHECK_NEAR (plainOf (proc, "filter1.cutoff"), 12000.0, 1.0);
    param->removeListener (&counter);

    // changes from the host show in the control (ParameterAttachment), unknown keys are survivable
    setPlain (proc, "filter1.cutoff", 2000.0f);
    CHECK_NEAR (knob.paramHandle().plain(), 2000.0, 1.0);
    ParamKnob bad (ctx, "no.such.key");
    bad.setSize (80, 100);
    CHECK (! bad.paramHandle().valid());
    juce::Image img (juce::Image::ARGB, 80, 100, true);
    juce::Graphics g (img);
    bad.paint (g);
}

TEST (uir_toggle_choice_and_slider)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    auto toggle = makeControl (ctx, "master.legato");
    auto* t = dynamic_cast<ParamToggle*> (toggle.get());
    CHECK (t != nullptr);
    t->setSize (t->preferredSize().x, t->preferredSize().y);
    CHECK (! t->isOn());
    tap (*t, { 10.0f, 10.0f });
    CHECK (t->isOn());
    CHECK_NEAR (plainOf (proc, "master.legato"), 1.0, 1e-6);

    auto choice = makeControl (ctx, "osc1.wavetable");
    auto* c = dynamic_cast<ParamChoice*> (choice.get());
    CHECK (c != nullptr);
    c->setSize (c->preferredSize().x, c->preferredSize().y);
    setPlain (proc, "osc1.wavetable", 3.0f);
    c->step (+1);
    CHECK_NEAR (plainOf (proc, "osc1.wavetable"), 4.0, 1e-6);
    c->step (-1);
    c->step (-1);
    CHECK_NEAR (plainOf (proc, "osc1.wavetable"), 2.0, 1e-6);

    // the "Preset table" entry needs kCapUserWavetable: stepping onto it is refused when the engine lacks the bit
    CapsHost noCaps (proc, kCapNone);
    UiContext ctx0 { noCaps, theme, assign };
    ParamChoice limited (ctx0, "osc1.wavetable");
    setPlain (proc, "osc1.wavetable", static_cast<float> (kWavetablePresetTable - 1));
    limited.step (+1);
    CHECK_NEAR (plainOf (proc, "osc1.wavetable"), kWavetablePresetTable - 1, 1e-6);

    ParamSlider slider (ctx, "osc1.level");
    slider.setSize (slider.preferredSize().x, slider.preferredSize().y);
    CHECK (slider.preferredSize().y >= 44);
    setPlain (proc, "osc1.level", 10.0f);
    const juce::Point<float> down (50.0f, 30.0f);
    slider.mouseDown (mouse (slider, down, down, false));
    slider.mouseDrag (mouse (slider, down + juce::Point<float> (60.0f, 0.0f), down, true)); // horizontal
    slider.mouseUp (mouse (slider, down + juce::Point<float> (60.0f, 0.0f), down, true));
    CHECK (plainOf (proc, "osc1.level") > 10.0f);
}

TEST (uir_engine_caps_dim_controls)
{
    SynthyBoyProcessor proc;
    Theme theme = Theme::dark();

    CapsHost none (proc, kCapNone);
    ModAssignController assignNone (none);
    UiContext c0 { none, theme, assignNone };
    ParamKnob unavailable (c0, "master.tuning.a4");
    ParamKnob available (c0, "master.volume");
    CHECK (! unavailable.isAvailable());
    CHECK_NEAR (unavailable.getAlpha(), 0.4, 1e-3);
    CHECK_EQ (unavailable.getTooltip().toStdString(), std::string ("not available in this engine"));
    CHECK (available.isAvailable());
    CHECK_NEAR (available.getAlpha(), 1.0, 1e-3);

    // still editable (the value is stored and saved)
    unavailable.paramHandle().setPlainComplete (442.0f);
    CHECK_NEAR (plainOf (proc, "master.tuning.a4"), 442.0, 0.01);

    CapsHost all (proc, kCapAllKnown);
    ModAssignController assignAll (all);
    UiContext c1 { all, theme, assignAll };
    ParamKnob live (c1, "master.tuning.a4");
    CHECK (live.isAvailable());
    CHECK_NEAR (live.getAlpha(), 1.0, 1e-3);

    // the macro row: macro.5..8 dim without kCapMacros58
    MacroRow row (c0);
    CHECK_EQ (static_cast<int> (row.knobs().size()), 8);
    CHECK (row.knobs()[3]->isAvailable());
    CHECK (! row.knobs()[4]->isAvailable());
    setPlain (proc, "master.tuning.a4", 440.0f);
}

TEST (uir_assign_by_touch_through_controls)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    ModSourceBar bar (ctx);
    bar.setSize (390, bar.preferredHeight());
    const int lfo1 = sourceChoiceForKey ("lfo1");
    auto* chip = bar.chipFor (lfo1);
    CHECK (chip != nullptr);
    chip->setSize (chip->preferredSize().x, chip->preferredSize().y);

    ParamKnob cutoff (ctx, "filter1.cutoff");
    cutoff.setSize (cutoff.preferredSize().x, cutoff.preferredSize().y);
    ParamKnob legatoLike (ctx, "osc1.level");
    legatoLike.setSize (legatoLike.preferredSize().x, legatoLike.preferredSize().y);
    setPlain (proc, "filter1.cutoff", 12000.0f);
    const float cutoffBefore = plainOf (proc, "filter1.cutoff");

    // 1. tap the chip -> armed, purple target rings on modulatable knobs
    tap (*chip, { 10.0f, 10.0f });
    CHECK (assign.flow().phase() == AssignFlow::Phase::armed);
    CHECK_EQ (assign.flow().armedSource(), lfo1);
    CHECK (chip->isArmed());

    // 2. drag the knob up: the value does NOT change, mod01 is created and its amount grows
    const juce::Point<float> down (static_cast<float> (cutoff.getWidth()) * 0.5f, static_cast<float> (cutoff.getHeight()) * 0.5f);
    cutoff.mouseDown (mouse (cutoff, down, down, false));
    for (int i = 1; i <= 4; ++i)
        cutoff.mouseDrag (mouse (cutoff, down - juce::Point<float> (0.0f, 15.0f * static_cast<float> (i)), down, true));
    CHECK (assign.flow().phase() == AssignFlow::Phase::dragging);
    cutoff.mouseUp (mouse (cutoff, down - juce::Point<float> (0.0f, 60.0f), down, true));
    CHECK (assign.flow().phase() == AssignFlow::Phase::armed);
    CHECK_NEAR (plainOf (proc, "filter1.cutoff"), cutoffBefore, 1e-3);
    CHECK_EQ (static_cast<int> (std::lround (plainOf (proc, "mod01.source"))), lfo1);
    const int destChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (resolveKey ("filter1.cutoff").index)));
    CHECK_EQ (static_cast<int> (std::lround (plainOf (proc, "mod01.destination"))), destChoice);
    CHECK (plainOf (proc, "mod01.amount") > 20.0f);

    // 3. the snapshot / ring know about it
    assign.refresh();
    CHECK_EQ (assign.snapshot().slotsUsed(), 1);
    CHECK_EQ (assign.snapshot().destinationCount (lfo1), 1);
    CHECK_EQ (static_cast<int> (assign.snapshot().entriesFor (resolveKey ("filter1.cutoff").index).size()), 1);

    // ring geometry: positive amount sweeps up from the knob position, bipolar both ways, always clamped
    ModEntry e; e.amount = 50.0f;
    auto span = ModRing::spanFor (e, 0.25f);
    CHECK_NEAR (span.first, 0.25, 1e-6);
    CHECK_NEAR (span.second, 0.75, 1e-6);
    e.amount = -50.0f;
    span = ModRing::spanFor (e, 0.25f);
    CHECK_NEAR (span.first, 0.0, 1e-6);
    CHECK_NEAR (span.second, 0.25, 1e-6);
    e.bipolar = true;
    span = ModRing::spanFor (e, 0.5f);
    CHECK_NEAR (span.first, 0.0, 1e-6);
    CHECK_NEAR (span.second, 1.0, 1e-6);

    // 4. a plain tap on a target does not create a slot; non-target stepped keys keep working
    legatoLike.mouseDown (mouse (legatoLike, down, down, false));
    legatoLike.mouseUp (mouse (legatoLike, down, down, false));
    assign.refresh();
    CHECK_EQ (assign.snapshot().slotsUsed(), 1);

    // 5. tapping the chip again leaves assign mode
    tap (*chip, { 10.0f, 10.0f });
    CHECK (assign.flow().phase() == AssignFlow::Phase::idle);

    // the sheet's list removes it again
    clearSlot (assign.matrix(), 0);
    assign.refresh();
    CHECK_EQ (assign.snapshot().slotsUsed(), 0);

    // a source the engine lacks is refused by the controller and its chip is dimmed
    CapsHost none (proc, kCapNone);
    ModAssignController assign0 (none);
    UiContext c0 { none, theme, assign0 };
    ModSourceChip lift (c0, sourceChoiceForKey ("lift"));
    CHECK (! lift.isAvailable());
    CHECK_NEAR (lift.getAlpha(), 0.4, 1e-3);
    CHECK (assign0.tapSource (sourceChoiceForKey ("lift")) == AssignFlow::Phase::idle);
    CHECK (assign0.tapSource (sourceChoiceForKey ("lfo1")) == AssignFlow::Phase::armed);
}

TEST (uir_sheet_value_entry)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    ParamSheet sheet (ctx, "filter1.cutoff");
    CHECK (sheet.getHeight() > 100);
    CHECK (sheet.applyValueText ("1500"));
    CHECK_NEAR (plainOf (proc, "filter1.cutoff"), 1500.0, 1.0);
    CHECK (sheet.applyValueText ("2.5k"));
    CHECK_NEAR (plainOf (proc, "filter1.cutoff"), 2500.0, 1.0);
    CHECK (! sheet.applyValueText (""));          // empty text = cancel
    CHECK_NEAR (plainOf (proc, "filter1.cutoff"), 2500.0, 1.0);
    CHECK (! sheet.applyValueText ("garbage"));
    CHECK (sheet.applyValueText ("99999999"));    // clamped into the range
    CHECK_NEAR (plainOf (proc, "filter1.cutoff"), 20000.0, 1.0);
    sheet.nudge (-1);
    CHECK (plainOf (proc, "filter1.cutoff") < 20000.0f);
    sheet.paramHandle().resetToDefault();
    CHECK_NEAR (plainOf (proc, "filter1.cutoff"), 12000.0, 1.0);
}

TEST (uir_editor_layout_touch_targets_and_modes)
{
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;

    struct Case { int w, h; LayoutMode mode; const char* name; };
    const Case cases[] = { { 390, 844, LayoutMode::phone, "phone portrait 390x844" },
                           { 844, 390, LayoutMode::tablet, "phone landscape 844x390" },
                           { 820, 1180, LayoutMode::tablet, "tablet portrait 820x1180" },
                           { 1024, 768, LayoutMode::wide, "tablet landscape 1024x768" } };
    for (const auto& c : cases)
    {
        editor->setSize (c.w, c.h);
        CHECK (editor->panelHost().mode() == c.mode);
        checkTouchTargets (*editor, c.name);
        // nothing of the shell overflows the editor
        for (auto* child : { static_cast<juce::Component*> (&editor->topBar()), static_cast<juce::Component*> (&editor->macroRow()),
                             static_cast<juce::Component*> (&editor->pageTabs()), static_cast<juce::Component*> (&editor->panelHost()),
                             static_cast<juce::Component*> (&editor->keyboardStrip()) })
            CHECK (editor->getLocalBounds().contains (child->getBounds()));
    }

    // phone: only the panels of the selected page; wide: all panels as tiles
    editor->setSize (390, 844);
    CHECK (editor->panelHost().visiblePanels().size() >= 1);
    editor->pageTabs().select (Page::osc);
    CHECK (editor->panelHost().page() == Page::osc);
    CHECK (editor->panelHost().visiblePanels().empty() == ! editor->panelHost().pageHasPanels (Page::osc));
    editor->pageTabs().select (Page::glb);
    CHECK (! editor->panelHost().visiblePanels().empty());
    editor->setSize (1024, 768);
    CHECK_EQ (editor->panelHost().visiblePanels().size(), editor->panelHost().allPanels().size());

    // UI scale keeps the 44 point minimum and the dark / light switch repaints without trouble
    editor->setUiScale (1.5f);
    CHECK (editor->theme().touch() >= 44);
    checkTouchTargets (*editor, "scale 150 %");
    editor->setUiScale (0.75f);
    CHECK (editor->theme().touch() >= 44);
    editor->setUiScale (1.0f);
    editor->setDarkMode (false);
    CHECK (! editor->theme().isDark());
    editor->setDarkMode (true);
}

TEST (uir_ab_compare_through_editor)
{
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;

    setPlain (proc, "master.volume", -6.0f);
    editor->toggleAb(); // -> B (copy of A)
    CHECK (proc.abCompare().active() == AbCompare::Slot::b);
    CHECK_NEAR (plainOf (proc, "master.volume"), -6.0, 0.01);
    setPlain (proc, "master.volume", -20.0f);
    editor->toggleAb(); // -> A
    CHECK_NEAR (plainOf (proc, "master.volume"), -6.0, 0.01);
    CHECK_EQ (editor->topBar().ab.getButtonText().toStdString(), std::string ("A"));
    editor->toggleAb(); // -> B
    CHECK_NEAR (plainOf (proc, "master.volume"), -20.0, 0.01);
    CHECK_EQ (editor->topBar().ab.getButtonText().toStdString(), std::string ("B"));
}

TEST (uir_keyboard_notes_reach_the_engine)
{
    SynthyBoyProcessor proc;
    proc.setRateAndBufferSizeDetails (44100.0, 512);
    proc.prepareToPlay (44100.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto energy = [&]
    {
        float e = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
            e += buffer.getMagnitude (0, 512);
        }
        return e;
    };
    CHECK (energy() < 1.0e-6f); // silent without a note

    proc.keyboardState().noteOn (1, 60, 0.9f); // what a key press on the strip does (the strip is wired to this state)
    CHECK (editor->keyboardStrip().keyboard().getKeyWidth() > 10.0f);
    CHECK (energy() > 1.0e-4f);
    proc.keyboardState().noteOff (1, 60, 0.0f);
}

TEST (uir_preset_strip_and_screenshots)
{
    SynthyBoyProcessor proc;
    int steps = 0;
    proc.setPresetStepHandler ([&steps] (int d) { steps += d; });
    proc.setPresetName ("Factory Bass 01");

    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;

    editor->topBar().next.onClick();
    editor->topBar().next.onClick();
    editor->topBar().prev.onClick();
    CHECK_EQ (steps, 1);
    editor->topBar().setPresetName (proc.presetName());

    const auto dir = screenshotDir();
    struct Shot { int w, h; const char* file; };
    const Shot shots[] = { { 390, 844, "phone-390x844.png" }, { 1024, 768, "tablet-1024x768.png" } };
    for (const auto& s : shots)
    {
        editor->setSize (s.w, s.h);
        editor->setSourceBarOpen (std::strcmp (s.file, "phone-390x844.png") == 0); // the phone shot shows the source drawer
        editor->panelHost().setPage (Page::glb);
        editor->panelHost().allPanels()[0]->setMoreExpanded (true);
        const auto file = dir.getChildFile (s.file);
        CHECK (writePng (*editor, file));
        CHECK (file.getSize() > 2000);
        std::printf ("    screenshot %s (%lld bytes)\n", file.getFullPathName().toRawUTF8(), static_cast<long long> (file.getSize()));
    }
    editor->setSourceBarOpen (false);
}

TEST (uir_zz_shutdown)
{
    delete juceInit;
    juceInit = nullptr;
    CHECK (true);
}
