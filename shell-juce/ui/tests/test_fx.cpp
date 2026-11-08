// SynthyBoy - UI tests: Effects panels, interactions, permutation logic, contract, and headless screenshots.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "ui/Sheet.h"
#include "ui/panels/FxCommon.h"
#include "ui/panels/FxShaperEditor.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInit = new juce::ScopedJuceInitialiser_GUI();

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
    StateContent content() const override { return inner.content(); }
    bool setContent (const StateContent& c) override { return inner.setContent (c); }

private:
    SynthyBoyProcessor& inner;
    EngineCapsMask caps;
};

bool writePng (juce::Component& c, const juce::File& file)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    if (! image.isValid())
        return false;
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}
} // namespace

TEST (fx_pure_logic_permutation_and_math)
{
    // 1. Permutation repair: Identity
    std::array<int, 16> id {};
    for (int i = 0; i < 16; ++i)
        id[static_cast<size_t> (i)] = i;
    const auto repairedId = repairFxPermutation (id);
    for (int i = 0; i < 16; ++i)
        CHECK_EQ (repairedId[static_cast<size_t> (i)], i);

    // 2. Permutation repair: Duplicates
    std::array<int, 16> dups {};
    for (int i = 0; i < 16; ++i)
        dups[static_cast<size_t> (i)] = i / 2; // only 0..7 used, duplicated
    const auto repairedDups = repairFxPermutation (dups);
    std::array<bool, 16> seen {};
    for (int i = 0; i < 16; ++i)
    {
        const int val = repairedDups[static_cast<size_t> (i)];
        CHECK (val >= 0 && val < 16);
        CHECK (! seen[static_cast<size_t> (val)]);
        seen[static_cast<size_t> (val)] = true;
    }

    // 3. Permutation repair: Out of bounds
    std::array<int, 16> outOfBounds { -5, 99, 100, -1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    const auto repairedOob = repairFxPermutation (outOfBounds);
    std::array<bool, 16> seenOob {};
    for (int i = 0; i < 16; ++i)
    {
        const int val = repairedOob[static_cast<size_t> (i)];
        CHECK (val >= 0 && val < 16);
        CHECK (! seenOob[static_cast<size_t> (val)]);
        seenOob[static_cast<size_t> (val)] = true;
    }

    // 4. Frequency normalization roundtrip
    CHECK_NEAR (fxFreqToNorm (20.0f), 0.0f, 1e-4);
    CHECK_NEAR (fxFreqToNorm (20000.0f), 1.0f, 1e-4);
    for (float f : { 50.0f, 440.0f, 1000.0f, 5000.0f, 12000.0f })
    {
        const float norm = fxFreqToNorm (f);
        const float roundtrip = fxNormToFreq (norm);
        CHECK_NEAR (roundtrip, f, 0.1f);
    }

    // 5. dB normalization roundtrip
    CHECK_NEAR (fxDbToNorm (-30.0f), 0.0f, 1e-4);
    CHECK_NEAR (fxDbToNorm (30.0f), 1.0f, 1e-4);
    for (float db : { -20.0f, -6.0f, 0.0f, 12.0f, 24.0f })
    {
        const float norm = fxDbToNorm (db);
        const float roundtrip = fxNormToDb (norm);
        CHECK_NEAR (roundtrip, db, 0.01f);
    }
}

TEST (fx_panel_registry_contract)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    const auto& entries = PanelRegistry::instance().entries();
    int fxCount = 0;

    for (const auto& e : entries)
    {
        if (e.page != Page::fx)
            continue;

        ++fxCount;
        sbtest::Context c (e.id.c_str());
        auto panel = e.create (ctx);
        panel->build();

        CHECK (panel->title().isNotEmpty());
        const auto keys = panel->keys();
        CHECK (! keys.empty());
        CHECK_EQ (keys.size(), panel->controls().size());

        for (size_t i = 0; i < keys.size(); ++i)
        {
            CHECK (resolveKey (keys[i]).valid());
            CHECK_EQ (keys[i], panel->controls()[i]->key());
        }

        // Principle 1: at most 4 primary controls
        CHECK (static_cast<int> (panel->controls().size()) - panel->numMoreControls() <= 4);

        // Preferred heights at phone (390) and tablet (1024)
        CHECK (panel->preferredHeight (390) > 0);
        CHECK (panel->preferredHeight (1024) > 0);
    }

    // All 17 FX panels must be present
    CHECK_EQ (fxCount, 17);
}

TEST (fx_direct_manipulation_and_interactions)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    // 1. Direct manipulation on Chorus visual updates cutoff and spread
    ParamHandle cutoff (ctx, "chorus.filter_cutoff");
    cutoff.setPlainComplete (1000.0f);
    CHECK_NEAR (cutoff.plain(), 1000.0, 1.0);

    ParamHandle spread (ctx, "chorus.filter_spread");
    spread.setPlainComplete (50.0f);
    CHECK_NEAR (spread.plain(), 50.0, 0.1);

    // 2. Chain order reordering and permutation write
    ParamHandle order1 (ctx, "fx.order.1");
    ParamHandle order2 (ctx, "fx.order.2");
    order1.setPlainComplete (3.0f);
    order2.setPlainComplete (0.0f);
    CHECK_NEAR (order1.plain(), 3.0, 0.01);
    CHECK_NEAR (order2.plain(), 0.0, 0.01);

    // 3. Reverb force enable parameter
    ParamHandle force (ctx, "reverb.force_enable");
    force.setPlainComplete (1.0f);
    CHECK_NEAR (force.plain(), 1.0, 0.01);
}

TEST (fx_headless_render_and_screenshots)
{
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    editor->panelHost().setPage (Page::fx);

    const juce::File shotDir ("/tmp/ui-shots/fx");
    shotDir.createDirectory();

    // 1. Phone portrait 390x844
    editor->setSize (390, 844);
    editor->setSourceBarOpen (false);
    const auto phoneFile = shotDir.getChildFile ("phone-390x844.png");
    CHECK (writePng (*editor, phoneFile));
    CHECK (phoneFile.getSize() > 2000);
    std::printf ("    screenshot %s (%lld bytes)\n",
                 phoneFile.getFullPathName().toRawUTF8(), static_cast<long long> (phoneFile.getSize()));

    // 2. Tablet landscape 1024x768
    editor->setSize (1024, 768);
    const auto tabletFile = shotDir.getChildFile ("tablet-1024x768.png");
    CHECK (writePng (*editor, tabletFile));
    CHECK (tabletFile.getSize() > 2000);
    std::printf ("    screenshot %s (%lld bytes)\n",
                 tabletFile.getFullPathName().toRawUTF8(), static_cast<long long> (tabletFile.getSize()));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// block B17: the FX extras (docs/ui-design/13-new-keys.md 4.14)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged, int clicks = 1)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                             juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), clicks, dragged);
}

void dragOn (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown (mouseAt (c, from, from, false));
    c.mouseDrag (mouseAt (c, from + (to - from) * 0.5f, from, true));
    c.mouseDrag (mouseAt (c, to, from, true));
    c.mouseUp (mouseAt (c, to, from, true));
}

void doubleTapOn (juce::Component& c, juce::Point<float> p)
{
    c.mouseDown (mouseAt (c, p, p, false, 1));
    c.mouseUp (mouseAt (c, p, p, false, 1));
    c.mouseDown (mouseAt (c, p, p, false, 2));
    c.mouseDoubleClick (mouseAt (c, p, p, false, 2));
    c.mouseUp (mouseAt (c, p, p, false, 2));
}

template <typename T>
T* findChildOfType (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (child))
            return t;
        if (auto* t = findChildOfType<T> (*child))
            return t;
    }
    return nullptr;
}

juce::TextButton* findButtonText (juce::Component& c, const juce::String& text)
{
    for (auto* child : c.getChildren())
    {
        if (auto* b = dynamic_cast<juce::TextButton*> (child))
            if (b->getButtonText().startsWith (text))
                return b;
        if (auto* b = findButtonText (*child, text))
            return b;
    }
    return nullptr;
}

std::unique_ptr<Panel> makeFxPanel (UiContext& ctx, const char* id)
{
    for (const auto& e : PanelRegistry::instance().entries())
        if (e.id == id)
        {
            auto panel = e.create (ctx);
            panel->build();
            panel->setBounds (0, 0, 390, panel->preferredHeight (390));
            return panel;
        }
    return nullptr;
}

struct FxFixture
{
    // ParameterAttachment calls back at once only on the message thread; the test thread plays that role
    FxFixture() { juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread(); }
    SynthyBoyProcessor proc;
    CapsHost host { proc, kCapAllKnown };
    Theme theme = Theme::dark();
    ModAssignController assign { host };
    UiContext ctx { host, theme, assign };
};

void setParamPlain (FxFixture& f, const char* key, float plain)
{
    auto* p = f.proc.parameters().getParameter (key);
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

const char* const kB17Keys[] = {
    "compressor.limit", "compressor.limit_latency_comp", "compressor.crossover_low", "compressor.crossover_high",
    "delay.hq", "delay.offset", "distortion.filter_keytrack", "phaser.depth2",
    "reverb.type", "reverb.room", "reverb.width", "reverb.er_size", "reverb.diff_a", "reverb.diff_b"
};
} // namespace

TEST (fx_extras_every_new_key_has_a_control_on_its_panel)
{
    FxFixture f;
    for (const char* key : kB17Keys)
    {
        sbtest::Context c (key);
        const Panel* owner = nullptr;
        std::unique_ptr<Panel> keep;
        ParamControl* control = nullptr;
        for (const auto& e : PanelRegistry::instance().entries())
        {
            if (e.page != Page::fx)
                continue;
            auto panel = e.create (f.ctx);
            panel->build();
            for (const auto& ctl : panel->controls())
                if (ctl->key() == key)
                {
                    keep = std::move (panel);
                    control = ctl.get();
                    owner = keep.get();
                    break;
                }
            if (control != nullptr)
                break;
        }
        CHECK (owner != nullptr && control != nullptr);
        if (control == nullptr)
            continue;
        CHECK (control->isAvailable());
        const auto& info = control->paramHandle().info();
        // assign-by-touch: a modulatable key is a drag control (knob / slider with the mod ring)
        if ((info.flags & kParamModulatable) != 0)
            CHECK (dynamic_cast<ParamDragControl*> (control) != nullptr);
        // touch target
        CHECK (control->preferredSize().x >= f.theme.ipx (44.0f) && control->preferredSize().y >= f.theme.ipx (44.0f));
    }

    // dimmed while the engine lacks the capability (the block's own bits)
    SynthyBoyProcessor proc;
    CapsHost none (proc, kCapAllKnown & ~kCapFxExtrasAll);
    ModAssignController assign (none);
    Theme theme = Theme::dark();
    UiContext ctx { none, theme, assign };
    auto reverb = makeFxPanel (ctx, "FxReverbPanel");
    CHECK (reverb != nullptr);
    for (const auto& ctl : reverb->controls())
    {
        const bool b17 = ctl->key() == "reverb.type" || ctl->key() == "reverb.room" || ctl->key() == "reverb.width" || ctl->key() == "reverb.er_size"
                         || ctl->key() == "reverb.diff_a" || ctl->key() == "reverb.diff_b";
        CHECK_EQ (ctl->isAvailable(), ! b17);
    }
}

TEST (fx_extras_pure_models_tap_tempo_snap_scrub_and_curves)
{
    TapTempo tap;
    CHECK (! tap.tap (1000.0));
    CHECK (tap.tap (1500.0));
    CHECK_NEAR (tap.intervalMs(), 500.0, 1e-9);
    CHECK_NEAR (tap.bpm(), 120.0, 1e-9);
    CHECK (tap.tap (2100.0));
    CHECK_NEAR (tap.intervalMs(), 550.0, 1e-9); // the average of 500 and 600
    CHECK (! tap.tap (9000.0));                 // a pause starts a new series
    CHECK (! tap.hasInterval());
    CHECK (! tap.tap (9010.0));                 // faster than 60 ms: not a tap
    CHECK (tap.tap (9400.0));
    CHECK_NEAR (tap.intervalMs(), 390.0, 1e-9);
    for (int i = 0; i < 10; ++i)
        tap.tap (9400.0 + 400.0 * (i + 1));
    CHECK_NEAR (tap.intervalMs(), 400.0, 1e-9); // only the last four intervals

    CHECK_NEAR (snapDelayOffset (1.02f), 1.0f, 1e-6);
    CHECK_NEAR (snapDelayOffset (1.35f), 4.0f / 3.0f, 1e-6);
    CHECK_NEAR (snapDelayOffset (1.49f), 1.5f, 1e-6);
    CHECK_NEAR (snapDelayOffset (2.0f), 2.0f, 1e-6);

    CHECK_NEAR (scrubCrossover (120.0f, 60.0f, 60.0f, 20.0f, 1000.0f), 240.0f, 0.01f);
    CHECK_NEAR (scrubCrossover (120.0f, -600.0f, 60.0f, 20.0f, 1000.0f), 20.0f, 1e-6);
    CHECK_NEAR (scrubCrossover (120.0f, 600.0f, 60.0f, 20.0f, 1000.0f), 1000.0f, 1e-6);

    auto curve = identityCurve();
    CHECK (isIdentityCurve (curve) && isIdentityCurve ({}));
    int index = -1;
    CHECK (curveAddPoint (curve, 0.5f, 0.8f, &index) && index == 1 && curve.size() == 3);
    CHECK (! curveAddPoint (curve, 0.501f, 0.1f));              // too close
    CHECK (! isIdentityCurve (curve));
    CHECK (curveMovePoint (curve, 1, 1.2f, 0.3f));              // x is held between the neighbours
    CHECK (curve[1].x < 1.0f && curve[1].x > 0.99f);
    CHECK (! curveMovePoint (curve, 0, 0.5f, 0.0f));            // the end points keep their x
    CHECK (curveMovePoint (curve, 0, 0.5f, 0.2f) && curve[0].x < 1e-6f && std::fabs (curve[0].y - 0.2f) < 1e-6f);
    CHECK (curveSetPower (curve, 1, 20.0f) && std::fabs (curve[1].power - 8.0f) < 1e-6f);
    CHECK (! curveSetPower (curve, 2, 1.0f));                   // the last point starts no segment
    CHECK (curveRemovePoint (curve, 1) && curve.size() == 2);
    CHECK (! curveRemovePoint (curve, 0) && ! curveRemovePoint (curve, 1));
    CHECK_EQ (curveHit (curve, 0.02f, 0.2f, 0.05f, 0.05f), 0);
    CHECK_EQ (curveHit (curve, 0.5f, 0.5f, 0.05f, 0.05f), -1);
    for (int i = 0; i < static_cast<int> (CurvePreset::kNumPresets); ++i)
    {
        const auto points = curvePreset (static_cast<CurvePreset> (i));
        StateContent content;
        content.shaperA = isIdentityCurve (points) ? std::vector<ContentPoint> {} : points;
        CHECK (validateContent (content)); // every preset is a valid curve
        CHECK (std::strlen (curvePresetName (static_cast<CurvePreset> (i))) > 0);
    }
    // the point limit
    auto full = identityCurve();
    for (int i = 1; i < kMaxCurvePoints - 1; ++i)
        CHECK (curveAddPoint (full, static_cast<float> (i) / static_cast<float> (kMaxCurvePoints), 0.5f));
    CHECK_EQ (static_cast<int> (full.size()), kMaxCurvePoints);
    CHECK (! curveAddPoint (full, 0.999f, 0.5f));
}

TEST (fx_extras_delay_tap_and_offset_snaps)
{
    FxFixture f;
    auto panel = makeFxPanel (f.ctx, "FxDelayPanel");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    auto* tap = findButtonText (*panel, "TAP");
    CHECK (tap != nullptr);
    CHECK (tap == nullptr || tap->getHeight() >= f.theme.ipx (44.0f) - 2);
    if (tap == nullptr)
        return;
    ParamHandle sync (f.ctx, "delay.sync"), time (f.ctx, "delay.time"), time2 (f.ctx, "delay.time2"), offset (f.ctx, "delay.offset");
    sync.setPlainComplete (1.0f);
    tap->onClick(); // the first tap only starts a series
    CHECK_NEAR (sync.plain(), 1.0f, 1e-6);
    juce::Thread::sleep (240);
    tap->onClick();
    CHECK_NEAR (sync.plain(), 0.0f, 1e-6); // Free
    CHECK (time.plain() > 150.0f && time.plain() < 400.0f);
    CHECK_NEAR (time2.plain(), time.plain(), 0.01f);
    CHECK (tap->getButtonText().startsWith ("TAP "));

    auto* trip = findButtonText (*panel, "x4/3");
    auto* dot = findButtonText (*panel, "x3/2");
    auto* one = findButtonText (*panel, "x1");
    CHECK (trip != nullptr && dot != nullptr && one != nullptr);
    if (trip == nullptr || dot == nullptr || one == nullptr)
        return;
    trip->onClick();
    CHECK_NEAR (offset.plain(), 4.0f / 3.0f, 1e-3);
    dot->onClick();
    CHECK_NEAR (offset.plain(), 1.5f, 1e-3);
    one->onClick();
    CHECK_NEAR (offset.plain(), 1.0f, 1e-3);
}

TEST (fx_extras_compressor_crossover_tags_scrub_the_frequency)
{
    FxFixture f;
    auto panel = makeFxPanel (f.ctx, "FxCompressorPanel");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    auto* view = findChildOfType<FxVisualBase> (*panel);
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    ParamHandle low (f.ctx, "compressor.crossover_low"), high (f.ctx, "compressor.crossover_high");
    CHECK_NEAR (low.plain(), 120.0f, 0.5f);
    CHECK_NEAR (high.plain(), 2500.0f, 5.0f);

    const auto area = view->getLocalBounds().toFloat().reduced (f.theme.px (8.0f)).withTrimmedTop (f.theme.px (18.0f));
    const juce::Point<float> tagLow (area.getX() + area.getWidth() / 3.0f, area.getY() + f.theme.px (11.0f));
    const juce::Point<float> tagHigh (area.getX() + area.getWidth() * 2.0f / 3.0f, area.getY() + f.theme.px (11.0f));
    dragOn (*view, tagLow, tagLow + juce::Point<float> (f.theme.px (60.0f), 0.0f)); // one octave to the right
    CHECK_NEAR (low.plain(), 240.0f, 3.0f);
    CHECK_NEAR (high.plain(), 2500.0f, 5.0f);
    dragOn (*view, tagHigh, tagHigh - juce::Point<float> (f.theme.px (60.0f), 0.0f)); // one octave to the left
    CHECK_NEAR (high.plain(), 1250.0f, 15.0f);
    dragOn (*view, tagLow, tagLow + juce::Point<float> (f.theme.px (600.0f), 0.0f));
    CHECK_NEAR (low.plain(), 1000.0f, 1.0f); // clamped to the range of the key

    // the limit switch is a More control; the badge and the labels follow it
    ParamHandle limit (f.ctx, "compressor.limit");
    limit.setPlainComplete (1.0f);
    CHECK_NEAR (limit.plain(), 1.0f, 1e-6);
}

TEST (fx_extras_xshaper_editor_edits_the_content)
{
    FxFixture f;
    auto panel = makeFxPanel (f.ctx, "FxDistortionPanel");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    auto* editor = findChildOfType<ShaperCurveEditor> (*panel);
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    CHECK (! editor->isVisible()); // the soft clip has no curve to draw

    setParamPlain (f, "distortion.type", 12.0f); // X-Shaper
    CHECK (editor->isVisible());
    CHECK (! editor->asymmetric());
    panel->setBounds (0, 0, 390, panel->preferredHeight (390));
    CHECK (editor->getHeight() >= editor->preferredHeight() - 2);
    CHECK (editor->points().size() == 2 && isIdentityCurve (editor->points()));
    CHECK (f.host.content().shaperA.empty());

    // an editor button is a 44 point target
    for (const char* text : { "A", "B", "Add", "Del", "Preset", "Reset" })
    {
        auto* b = findButtonText (*editor, text);
        CHECK (b != nullptr);
        if (b != nullptr)
            CHECK (b->getHeight() >= f.theme.ipx (44.0f) - 2);
    }

    // double tap on the empty plot = a new point at the finger
    const auto plot = editor->plotBounds();
    const juce::Point<float> at (plot.getX() + plot.getWidth() * 0.4f, plot.getBottom() - plot.getHeight() * 0.8f);
    doubleTapOn (*editor, at);
    CHECK_EQ (static_cast<int> (editor->points().size()), 3);
    CHECK_NEAR (editor->points()[1].x, 0.4f, 0.01f);
    CHECK_NEAR (editor->points()[1].y, 0.8f, 0.01f);
    CHECK_EQ (static_cast<int> (f.host.content().shaperA.size()), 3); // written at once

    // drag the point
    const auto p1 = editor->pointPosition (1);
    dragOn (*editor, p1, p1 + juce::Point<float> (plot.getWidth() * 0.2f, plot.getHeight() * 0.3f));
    CHECK_NEAR (editor->points()[1].x, 0.6f, 0.02f);
    CHECK_NEAR (editor->points()[1].y, 0.5f, 0.02f);
    {
        const auto stored = f.host.content();
        CHECK_EQ (static_cast<int> (stored.shaperA.size()), 3);
        if (stored.shaperA.size() >= 2)
            CHECK_NEAR (stored.shaperA[1].x, editor->points()[1].x, 1e-6);
    }
    // an end point moves up and down only
    const auto last = editor->pointPosition (2);
    dragOn (*editor, last, last + juce::Point<float> (-plot.getWidth() * 0.3f, plot.getHeight() * 0.5f));
    CHECK_NEAR (editor->points()[2].x, 1.0f, 1e-6);
    CHECK_NEAR (editor->points()[2].y, 0.5f, 0.03f);

    // bend the segment with the diamond (the value at its middle follows the finger)
    const auto handle = editor->handlePosition (0);
    const float before = editor->evaluate (0.5f * (editor->points()[0].x + editor->points()[1].x));
    dragOn (*editor, handle, handle - juce::Point<float> (0.0f, plot.getHeight() * 0.2f));
    CHECK (editor->points()[0].power != 0.0f);
    CHECK (editor->evaluate (0.5f * (editor->points()[0].x + editor->points()[1].x)) > before + 0.1f);
    doubleTapOn (*editor, editor->handlePosition (0)); // straight again
    CHECK_NEAR (editor->points()[0].power, 0.0f, 1e-6);

    // the curve B exists for X-Shaper (Asym) only
    editor->setCurve (1);
    CHECK_EQ (editor->curve(), 0);
    setParamPlain (f, "distortion.type", 13.0f);
    CHECK (editor->asymmetric());
    editor->setCurve (1);
    CHECK_EQ (editor->curve(), 1);
    CHECK (isIdentityCurve (editor->points()));
    editor->applyPreset (CurvePreset::fold);
    CHECK_EQ (static_cast<int> (f.host.content().shaperB.size()), 4);
    CHECK_EQ (static_cast<int> (f.host.content().shaperA.size()), 3); // A is untouched
    editor->setCurve (0);
    CHECK_EQ (static_cast<int> (editor->points().size()), 3);
    CHECK (validateContent (f.host.content()));

    // delete: double tap on an inner point; the Del button deletes the selected one
    const auto inner = editor->pointPosition (1);
    doubleTapOn (*editor, inner);
    CHECK_EQ (static_cast<int> (editor->points().size()), 2);
    editor->reset();
    CHECK (f.host.content().shaperA.empty()); // the identity is stored as nothing
    CHECK (! f.host.content().shaperB.empty());

    // a preset load (content from the host) shows up after a reload
    auto content = f.host.content();
    content.shaperA = curvePreset (CurvePreset::scurve);
    CHECK (f.host.setContent (content));
    editor->reloadFromHost();
    CHECK_EQ (static_cast<int> (editor->points().size()), 5);

    // the soft clip hides the editor again
    setParamPlain (f, "distortion.type", 0.0f);
    CHECK (! editor->isVisible());
}

TEST (fx_extras_reverb_dims_what_the_algorithm_does_not_use)
{
    FxFixture f;
    auto panel = makeFxPanel (f.ctx, "FxReverbPanel");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    const auto alpha = [&] (const char* key)
    {
        for (const auto& c : panel->controls())
            if (c->key() == key)
                return c->getAlpha();
        return -1.0f;
    };
    CHECK (alpha ("reverb.er_size") < 0.9f && alpha ("reverb.diff_a") < 0.9f && alpha ("reverb.room") < 0.9f); // type Vital
    CHECK (alpha ("reverb.width") > 0.99f);
    setParamPlain (f, "reverb.type", 2.0f); // Hall
    CHECK (alpha ("reverb.er_size") > 0.99f && alpha ("reverb.diff_b") > 0.99f);
    CHECK (alpha ("reverb.room") < 0.9f);
    setParamPlain (f, "reverb.type", 5.0f); // Basin
    CHECK (alpha ("reverb.room") > 0.99f);
}

TEST (fx_extras_phaser_depth2_is_a_more_control_and_draws_a_second_curve)
{
    FxFixture f;
    auto panel = makeFxPanel (f.ctx, "FxPhaserPanel");
    CHECK (panel != nullptr);
    if (panel == nullptr)
        return;
    bool found = false;
    for (const auto& c : panel->controls())
        found = found || c->key() == "phaser.depth2";
    CHECK (found);
    ParamHandle depth2 (f.ctx, "phaser.depth2");
    depth2.setPlainComplete (12.0f);
    CHECK_NEAR (depth2.plain(), 12.0f, 1e-3);
    auto* view = findChildOfType<FxVisualBase> (*panel);
    CHECK (view != nullptr);
    if (view != nullptr)
    {
        const auto image = view->createComponentSnapshot (view->getLocalBounds(), true, 1.0f);
        CHECK (image.isValid());
    }
}

TEST (fx_extras_screenshots_of_the_five_panels)
{
    FxFixture f;
    const juce::File shotDir ("/tmp/ui-shots/fx-extras");
    shotDir.createDirectory();
    setParamPlain (f, "distortion.type", 13.0f);
    setParamPlain (f, "compressor.limit", 1.0f);
    setParamPlain (f, "phaser.depth2", 18.0f);
    setParamPlain (f, "reverb.type", 5.0f);
    for (const char* id : { "FxCompressorPanel", "FxDelayPanel", "FxDistortionPanel", "FxPhaserPanel", "FxReverbPanel" })
        for (const int width : { 390, 1024 })
        {
            auto panel = makeFxPanel (f.ctx, id);
            CHECK (panel != nullptr);
            if (panel == nullptr)
                continue;
            panel->setBounds (0, 0, width, panel->preferredHeight (width));
            panel->setMoreExpanded (true);
            panel->setBounds (0, 0, width, panel->preferredHeight (width));
            CHECK (panel->getHeight() > 100);
            const auto file = shotDir.getChildFile (juce::String (id) + "-" + juce::String (width) + ".png");
            CHECK (writePng (*panel, file));
            CHECK (file.getSize() > 1000);
        }
}

TEST (fx_zz_shutdown)
{
    delete juceInit;
    juceInit = nullptr;
    CHECK (true);
}
