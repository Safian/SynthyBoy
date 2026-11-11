// SynthyBoy - UI tests: Voice, MIDI and Global panels (docs/ui-design/07-voice-midi-global.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "EditorAdapter.h"
#include "ui/Editor.h"
#include "ui/Panel.h"
#include "ui/ParamControls.h"
#include "ui/model/ControlModel.h"
#include "ui/model/ModModel.h"
#include "synthyboy/quality.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInit = nullptr;
// started in main() (sbtest::runStartups), never during static initialisation: that crashes on Windows
SBTEST_STARTUP (juceInit, [] { juceInit = new juce::ScopedJuceInitialiser_GUI(); }, [] { delete juceInit; juceInit = nullptr; });

class CapsHost final : public UiHost
{
public:
    CapsHost (SynthyBoyProcessor& p, EngineCapsMask c) : inner (p), caps (c) {}
    ParameterStore& parameterStore() override { return inner.parameterStore(); }
    bool overlaysInsideEditor() const noexcept override { return inner.overlaysInsideEditor(); }
    EngineCapsMask engineCaps() const noexcept override { return caps; }
    juce::MidiKeyboardState& keyboardState() override { return inner.keyboardState(); }
    AbCompare& abCompare() override { return inner.abCompare(); }
    void stepPreset (int d) override { inner.stepPreset (d); }
    juce::String presetName() const override { return inner.presetName(); }

private:
    SynthyBoyProcessor& inner;
    EngineCapsMask caps;
};

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                             downPos, juce::Time::getCurrentTime(), 1, dragged);
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

bool writePng (juce::Component& c, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
    juce::Graphics g (image);
    c.paintEntireComponent (g, true);
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}

void collectInteractiveTargets (juce::Component& c, std::vector<juce::Component*>& out)
{
    if (! c.isVisible())
        return;
    if (dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<ParamControl*> (&c) != nullptr)
        out.push_back (&c);
    for (auto* child : c.getChildren())
        collectInteractiveTargets (*child, out);
}
} // namespace

// ---- 1. Pure Logic Tests ---------------------------------------------------------------------------------------------------------

TEST (glb_pure_logic_portamento_curve)
{
    // Slope == 0 is linear (y = t)
    const auto calcCurve = [] (float t, float slope) -> float
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        slope = juce::jlimit (-8.0f, 8.0f, slope);
        if (std::abs (slope) < 1.0e-3f)
            return t;
        if (slope > 0.0f)
            return 1.0f - std::pow (1.0f - t, 1.0f + slope * 0.4f);
        return std::pow (t, 1.0f - slope * 0.4f);
    };

    CHECK_NEAR (calcCurve (0.0f, 0.0f), 0.0, 1e-4);
    CHECK_NEAR (calcCurve (0.5f, 0.0f), 0.5, 1e-4);
    CHECK_NEAR (calcCurve (1.0f, 0.0f), 1.0, 1e-4);

    // Convex (slope > 0): rises faster than linear in the first half
    const float convexMid = calcCurve (0.5f, 4.0f);
    CHECK (convexMid > 0.5f);
    CHECK (convexMid < 1.0f);

    // Concave (slope < 0): rises slower than linear in the first half
    const float concaveMid = calcCurve (0.5f, -4.0f);
    CHECK (concaveMid < 0.5f);
    CHECK (concaveMid > 0.0f);
}

TEST (glb_pure_logic_key_scale_conforming)
{
    // C Major scale mask: C, D, E, F, G, A, B -> intervals {0, 2, 4, 5, 7, 9, 11}
    const uint16_t cMajorMask = 0x0AB5;

    const auto inScale = [] (int note, int root, uint16_t mask) -> bool
    {
        const int interval = ((note - root) % 12 + 12) % 12;
        return (mask & (1u << interval)) != 0;
    };

    const auto conform = [&inScale] (int note, int root, uint16_t mask) -> int
    {
        if (inScale (note, root, mask))
            return note;
        for (int dist = 1; dist <= 6; ++dist)
        {
            const int down = ((note - dist) % 12 + 12) % 12;
            if (inScale (down, root, mask))
                return down;
            const int up = ((note + dist) % 12 + 12) % 12;
            if (inScale (up, root, mask))
                return up;
        }
        return note;
    };

    // In C Major:
    CHECK (inScale (0, 0, cMajorMask));  // C is in scale
    CHECK (inScale (2, 0, cMajorMask));  // D is in scale
    CHECK (! inScale (1, 0, cMajorMask)); // C# is outside
    CHECK (! inScale (3, 0, cMajorMask)); // D# is outside

    // C# (1) ties between C (0) and D (2) -> tie goes down to C (0) (Assumption A7)
    CHECK_EQ (conform (1, 0, cMajorMask), 0);
    // D# (3) ties between D (2) and E (4) -> tie goes down to D (2)
    CHECK_EQ (conform (3, 0, cMajorMask), 2);
    // F# (6) ties between F (5) and G (7) -> tie goes down to F (5)
    CHECK_EQ (conform (6, 0, cMajorMask), 5);
}

TEST (glb_pure_logic_scala_parsing)
{
    // Standard Scala format snippet
    const std::string scl =
        "! test_scale.scl\n"
        "! 5-tone pentatonic\n"
        "Pentatonic test\n"
        "5\n"
        "!\n"
        "9/8\n"
        "5/4\n"
        "3/2\n"
        "5/3\n"
        "2/1\n";

    // Parse logic check
    std::vector<float> cents;
    std::istringstream stream (scl);
    std::string line;
    bool foundDesc = false, foundCount = false;
    while (std::getline (stream, line))
    {
        const auto first = line.find_first_not_of (" \t\r\n");
        if (first == std::string::npos || line[first] == '!')
            continue;
        const auto clean = line.substr (first);
        if (! foundDesc) { foundDesc = true; continue; }
        if (! foundCount) { foundCount = true; continue; }

        if (const auto slash = clean.find ('/'); slash != std::string::npos)
        {
            const float num = std::stof (clean.substr (0, slash));
            const float den = std::stof (clean.substr (slash + 1));
            cents.push_back (1200.0f * std::log2 (num / den));
        }
    }
    CHECK_EQ (static_cast<int> (cents.size()), 5);
    CHECK_NEAR (cents[0], 203.91, 0.1);  // 9/8 = 203.91 cents
    CHECK_NEAR (cents[1], 386.31, 0.1);  // 5/4 = 386.31 cents
    CHECK_NEAR (cents[2], 701.95, 0.1);  // 3/2 = 701.95 cents
    CHECK_NEAR (cents[4], 1200.0, 0.1);  // 2/1 octave
}

TEST (glb_pure_logic_quality_tier_specs)
{
    const Quality qLow = qualityForTier (Tier::low);
    CHECK_EQ (qLow.maxVoices, 8);
    CHECK_EQ (qLow.maxUnison, 4);
    CHECK_EQ (qLow.oversampling, 1);
    CHECK (qLow.interpolation == Interpolation::linear);
    CHECK ((qLow.fxMask & kFxReverb) == 0); // Reverb masked on low tier
    CHECK ((qLow.fxMask & kFxFlanger) == 0);

    // Reverb force enable unmasks reverb on low tier (Q27)
    const uint32_t eff = effectiveFxMask (qLow.fxMask, true);
    CHECK ((eff & kFxReverb) != 0);

    const Quality qMed = qualityForTier (Tier::medium);
    CHECK_EQ (qMed.maxVoices, 16);
    CHECK_EQ (qMed.maxUnison, 8);
    CHECK_EQ (qMed.oversampling, 2);

    const Quality qHigh = qualityForTier (Tier::high);
    CHECK_EQ (qHigh.maxVoices, kMaxVoices);
    CHECK_EQ (qHigh.maxUnison, kMaxUnisonVoices);
    CHECK (qHigh.interpolation == Interpolation::spectral);
}

// ---- 2. Registered Panels Contract and Direct Manipulation ------------------------------------------------------------------------

TEST (glb_panels_registered_and_contract)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    const auto& entries = PanelRegistry::instance().entries();
    int glbCount = 0;
    for (const auto& e : entries)
    {
        if (e.page != Page::glb)
            continue;
        ++glbCount;
        auto panel = e.create (ctx);
        panel->build();

        CHECK (panel->title().isNotEmpty());
        const auto keys = panel->keys();
        CHECK (! keys.empty() || panel->title() == "Scope"); // the scope is a display, it has no key
        CHECK_EQ (keys.size(), panel->controls().size());

        for (size_t i = 0; i < keys.size(); ++i)
        {
            CHECK (resolveKey (keys[i]).valid());
            CHECK_EQ (keys[i], panel->controls()[i]->key());
        }
        // At most 4 primary controls
        CHECK (static_cast<int> (panel->controls().size()) - panel->numMoreControls() <= 4);
    }
    // We expect MasterVoicingPanel + 7 new panels = 8 panels on Page::glb
    CHECK (glbCount >= 8);
}

TEST (glb_portamento_direct_manipulation)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    // Find GlbPortamentoPanel
    std::unique_ptr<Panel> portaPanel;
    for (const auto& e : PanelRegistry::instance().entries())
    {
        if (e.id == "GlbPortamentoPanel")
        {
            portaPanel = e.create (ctx);
            break;
        }
    }
    CHECK (portaPanel != nullptr);
    if (portaPanel == nullptr)
        return;

    portaPanel->build();
    portaPanel->setSize (390, portaPanel->preferredHeight (390));

    setPlain (proc, "master.portamento.time", 100.0f);
    setPlain (proc, "master.portamento.slope", 0.0f);

    // Locate curve view child
    juce::Component* curveView = nullptr;
    for (auto* child : portaPanel->getChildren())
    {
        if (dynamic_cast<ParamControl*> (child) == nullptr && child->getHeight() > 80)
        {
            curveView = child;
            break;
        }
    }
    CHECK (curveView != nullptr);
    if (curveView == nullptr)
        return;

    // Drag upwards on curve view: slope should increase
    const juce::Point<float> down (100.0f, 60.0f);
    curveView->mouseDown (mouse (*curveView, down, down, false));
    curveView->mouseDrag (mouse (*curveView, down + juce::Point<float> (40.0f, -30.0f), down, true));
    curveView->mouseUp (mouse (*curveView, down + juce::Point<float> (40.0f, -30.0f), down, true));

    CHECK (plainOf (proc, "master.portamento.slope") > 0.5f);
    CHECK (plainOf (proc, "master.portamento.time") > 100.0f);

    // Double click resets to default
    curveView->mouseDoubleClick (mouse (*curveView, down, down, false));
    CHECK_NEAR (plainOf (proc, "master.portamento.slope"), 0.0, 0.1);
    CHECK_NEAR (plainOf (proc, "master.portamento.time"), 0.0, 1.0);
}

TEST (glb_key_scale_direct_manipulation)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    std::unique_ptr<Panel> scalePanel;
    for (const auto& e : PanelRegistry::instance().entries())
    {
        if (e.id == "GlbKeyScalePanel")
        {
            scalePanel = e.create (ctx);
            break;
        }
    }
    CHECK (scalePanel != nullptr);
    if (scalePanel == nullptr)
        return;

    scalePanel->build();
    scalePanel->setSize (390, scalePanel->preferredHeight (390));

    setPlain (proc, "master.scale.key", 0.0f); // C

    juce::Component* visual = nullptr;
    for (auto* child : scalePanel->getChildren())
    {
        if (dynamic_cast<ParamControl*> (child) == nullptr && child->getHeight() > 70)
        {
            visual = child;
            break;
        }
    }
    CHECK (visual != nullptr);
    if (visual == nullptr)
        return;

    // Clicking pad index 7 (G)
    const float padW = static_cast<float> (visual->getWidth()) / 12.0f;
    const juce::Point<float> tapPos (padW * 7.5f, static_cast<float> (visual->getHeight()) * 0.7f);
    visual->mouseDown (mouse (*visual, tapPos, tapPos, false));
    visual->mouseUp (mouse (*visual, tapPos, tapPos, false));

    CHECK_EQ (static_cast<int> (std::round (plainOf (proc, "master.scale.key"))), 7);
}

TEST (glb_osc_map_direct_manipulation)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    std::unique_ptr<Panel> oscMapPanel;
    for (const auto& e : PanelRegistry::instance().entries())
    {
        if (e.id == "GlbOscMapPanel")
        {
            oscMapPanel = e.create (ctx);
            break;
        }
    }
    CHECK (oscMapPanel != nullptr);
    if (oscMapPanel == nullptr)
        return;

    oscMapPanel->build();
    oscMapPanel->setSize (390, oscMapPanel->preferredHeight (390));

    setPlain (proc, "osc1.map.key_low", 0.0f);
    setPlain (proc, "osc1.map.key_high", 127.0f);

    juce::Component* mapView = nullptr;
    for (auto* child : oscMapPanel->getChildren())
    {
        if (dynamic_cast<ParamControl*> (child) == nullptr && child->getHeight() > 100)
        {
            mapView = child;
            break;
        }
    }
    CHECK (mapView != nullptr);
    if (mapView == nullptr)
        return;

    // Double tap on lane 0 resets
    const juce::Point<float> lane0 (150.0f, 75.0f);
    mapView->mouseDoubleClick (mouse (*mapView, lane0, lane0, false));
    CHECK_NEAR (plainOf (proc, "osc1.map.key_low"), 0.0, 0.1);
    CHECK_NEAR (plainOf (proc, "osc1.map.key_high"), 127.0, 0.1);
}

namespace
{
std::unique_ptr<Panel> buildPanelById (UiContext& ctx, const char* idPart)
{
    for (const auto& e : PanelRegistry::instance().entries())
        if (e.id.find (idPart) != std::string::npos)
        {
            auto panel = e.create (ctx);
            panel->build();
            return panel;
        }
    return nullptr;
}

ParamControl* controlOf (Panel& panel, const char* key)
{
    for (const auto& c : panel.controls())
        if (c->key() == key)
            return c.get();
    return nullptr;
}

/** Arms `source`, drags the knob of `key` up and returns whether a slot with that destination was made (the knob keeps its value). */
bool assignByTouch (SynthyBoyProcessor& proc, ModAssignController& assign, ParamControl& knob, const char* key, const char* sourceKey)
{
    const float before = plainOf (proc, key);
    if (assign.tapSource (sourceChoiceForKey (sourceKey)) != AssignFlow::Phase::armed)
        return false;
    knob.setSize (knob.preferredSize().x, knob.preferredSize().y);
    const juce::Point<float> down (static_cast<float> (knob.getWidth()) * 0.5f, static_cast<float> (knob.getHeight()) * 0.5f);
    knob.mouseDown (mouse (knob, down, down, false));
    for (int i = 1; i <= 4; ++i)
        knob.mouseDrag (mouse (knob, down - juce::Point<float> (0.0f, 15.0f * static_cast<float> (i)), down, true));
    knob.mouseUp (mouse (knob, down - juce::Point<float> (0.0f, 60.0f), down, true));
    assign.refresh();
    const int destChoice = static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (resolveKey (key).index)));
    const bool made = std::fabs (plainOf (proc, key) - before) < 1e-3f
                      && static_cast<int> (std::lround (plainOf (proc, "mod01.destination"))) == destChoice
                      && static_cast<int> (std::lround (plainOf (proc, "mod01.source"))) == sourceChoiceForKey (sourceKey);
    assign.tapSource (sourceChoiceForKey (sourceKey)); // leaves assign mode
    clearSlot (assign.matrix(), 0);
    assign.refresh();
    return made;
}
} // namespace

TEST (glb_voiceglb_panels_and_modulation)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    // 1. Voice / Stereo panel: G12 / G13 / G15 are primary, the bend-down pair is behind More, each with the right kit control
    auto voice = buildPanelById (ctx, "GlbVoicePanel");
    CHECK (voice != nullptr);
    if (voice != nullptr)
    {
        CHECK_EQ (static_cast<int> (voice->keys().size()), 6);
        CHECK_EQ (static_cast<int> (voice->controls().size()), 6);
        CHECK_EQ (voice->numMoreControls(), 2);
        voice->setMoreExpanded (true);
        voice->setSize (390, voice->preferredHeight (390));
        CHECK (dynamic_cast<ParamChoice*> (controlOf (*voice, "master.voice.same_note")) != nullptr);
        CHECK (dynamic_cast<ParamChoice*> (controlOf (*voice, "master.voice.steal_priority")) != nullptr);
        CHECK (dynamic_cast<ParamToggle*> (controlOf (*voice, "master.poly_legato")) != nullptr);
        CHECK (dynamic_cast<ParamKnob*> (controlOf (*voice, "master.stereo_spread")) != nullptr);
        CHECK (dynamic_cast<ParamToggle*> (controlOf (*voice, "master.bend_down_link")) != nullptr);
        CHECK (controlOf (*voice, "master.bend_down_range") != nullptr);

        // touch: choosing a steal mode through the control writes the parameter (steal priority Lowest = 1)
        auto* steal = dynamic_cast<ParamChoice*> (controlOf (*voice, "master.voice.steal_priority"));
        if (steal != nullptr)
        {
            setPlain (proc, "master.voice.steal_priority", 0.0f);
            CHECK_NEAR (plainOf (proc, "master.voice.steal_priority"), 0.0, 1e-6);
            setPlain (proc, "master.voice.steal_priority", 2.0f);
            CHECK_NEAR (plainOf (proc, "master.voice.steal_priority"), 2.0, 1e-6);
        }

        // assign-by-touch: stereo spread is a modulation destination (an LFO dragged onto the knob makes a slot, the knob keeps its value)
        auto* spread = controlOf (*voice, "master.stereo_spread");
        CHECK (spread != nullptr);
        if (spread != nullptr)
        {
            CHECK ((spread->paramHandle().info().flags & kParamModulatable) != 0);
            CHECK (assignByTouch (proc, assign, *spread, "master.stereo_spread", "lfo1"));
        }
        // bend down range is automatable but not a destination: assign mode does not make a slot on it
        auto* bendDown = controlOf (*voice, "master.bend_down_range");
        if (bendDown != nullptr)
            CHECK ((bendDown->paramHandle().info().flags & kParamModulatable) == 0);
    }

    // 2. the Master / Voicing panel lost nothing and kept no copy of the B15 keys
    auto master = buildPanelById (ctx, "MasterVoicing");
    CHECK (master != nullptr);
    if (master != nullptr)
        for (const char* k : { "master.voice.same_note", "master.voice.steal_priority", "master.poly_legato", "master.bend_down_range", "master.stereo_spread" })
            CHECK (controlOf (*master, k) == nullptr);

    // 3. Quality panel: 4x oversampling and no smoothing are primary controls (toggles)
    auto quality = buildPanelById (ctx, "GlbQualityPanel");
    CHECK (quality != nullptr);
    if (quality != nullptr)
    {
        CHECK_EQ (quality->numMoreControls(), 3);
        CHECK (dynamic_cast<ParamToggle*> (controlOf (*quality, "quality.oversample_4x")) != nullptr);
        CHECK (dynamic_cast<ParamToggle*> (controlOf (*quality, "quality.no_smoothing")) != nullptr);
        auto* four = controlOf (*quality, "quality.oversample_4x");
        if (four != nullptr)
        {
            CHECK_NEAR (plainOf (proc, "quality.oversample_4x"), 0.0, 1e-6);
            setPlain (proc, "quality.oversample_4x", 1.0f);
            CHECK_NEAR (plainOf (proc, "quality.oversample_4x"), 1.0, 1e-6);
            setPlain (proc, "quality.oversample_4x", 0.0f);
        }
    }

    // 4. LFO swing: a modulatable knob behind More on every LFO panel, assign-by-touch works on it
    for (const char* lfo : { "lfo1", "lfo10" })
    {
        const std::string swingKey = std::string (lfo) + ".swing";
        bool found = false;
        for (const auto& e : PanelRegistry::instance().entries())
        {
            auto panel = e.create (ctx);
            if (panel->modulePrefix() != lfo)
                continue;
            panel->build();
            panel->setMoreExpanded (true);
            panel->setSize (390, panel->preferredHeight (390));
            auto* swing = controlOf (*panel, swingKey.c_str());
            CHECK (swing != nullptr);
            if (swing != nullptr)
            {
                found = true;
                CHECK (dynamic_cast<ParamKnob*> (swing) != nullptr);
                CHECK ((swing->paramHandle().info().flags & kParamModulatable) != 0);
                CHECK (assignByTouch (proc, assign, *swing, swingKey.c_str(), "env1"));
            }
            break;
        }
        CHECK (found);
    }
}

TEST (glb_touch_targets_on_all_new_panels)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    const int minTouch = theme.touch();
    CHECK (minTouch >= 44);

    for (const auto& e : PanelRegistry::instance().entries())
    {
        if (e.page != Page::glb)
            continue;
        auto panel = e.create (ctx);
        panel->build();
        panel->setSize (390, panel->preferredHeight (390));

        std::vector<juce::Component*> targets;
        collectInteractiveTargets (*panel, targets);

        for (auto* c : targets)
        {
            const bool ok = (c->getWidth() >= minTouch && c->getHeight() >= minTouch);
            if (! ok)
                std::printf ("    [target too small in %s]: %dx%d (min %d)\n",
                             e.id.c_str(), c->getWidth(), c->getHeight(), minTouch);
            CHECK (ok);
        }
    }
}

// ---- 3. Headless Render to PNG at 390x844 and 1024x768 ----------------------------------------------------------------------------

TEST (glb_headless_render_to_png)
{
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = innerEditor (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;

    // Switch to GLB page
    editor->pageTabs().select (Page::glb);
    CHECK (editor->panelHost().page() == Page::glb);

    const juce::File outDir ("/tmp/ui-shots/glb");
    outDir.createDirectory();

    // 1. Phone portrait 390x844
    editor->setSize (390, 844);
    const juce::File phoneFile = outDir.getChildFile ("phone-390x844.png");
    CHECK (writePng (*editor, phoneFile));
    CHECK (phoneFile.getSize() > 2000);
    std::printf ("    Saved phone shot: %s (%lld bytes)\n",
                 phoneFile.getFullPathName().toRawUTF8(), static_cast<long long> (phoneFile.getSize()));

    // 2. Tablet landscape 1024x768
    editor->setSize (1024, 768);
    const juce::File tabletFile = outDir.getChildFile ("tablet-1024x768.png");
    CHECK (writePng (*editor, tabletFile));
    CHECK (tabletFile.getSize() > 2000);
    std::printf ("    Saved tablet shot: %s (%lld bytes)\n",
                 tabletFile.getFullPathName().toRawUTF8(), static_cast<long long> (tabletFile.getSize()));
}

TEST (glb_zz_shutdown)
{
    delete juceInit;
    juceInit = nullptr;
    CHECK (true);
}
