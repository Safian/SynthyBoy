// SynthyBoy - MPC UI library test coverage: composites and panels
// Tests untested public behaviors of Composite, TuneRow, TypeBar, UnisonBox,
// WarpSlot, WarpPair, KnobRandPair, BasicControls, KnobPanel, FilterRoutePanel,
// and MixerStrip.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui_test.h"
#include "../composite.h"
#include "../panel.h"
#include "../filter_display.h"
#include "../mixer_strip.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

static void test_composite_base_behavior()
{
    SourcePicker sp;
    sp.place ({ 50, 50, 280, 56 });
    sp.stepper.setId (401);
    sp.dropdown.setId (402);
    sp.stepper.state.count = 4;
    sp.stepper.state.index = 0;

    // collectLeaves
    Component* leaves[8];
    int nl = sp.collectLeaves (leaves, 0, 8);
    check (nl == 2, "Composite collectLeaves collects 2 leaf children");
    check (leaves[0] == &sp.stepper && leaves[1] == &sp.dropdown, "Leaves match children in order");

    // Primary child routing
    check (sp.primary() == &sp.stepper, "Composite primary() points to primary child");
    Change c = sp.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "Composite turn routes to primary child");

    // wheelOver routing to child
    Change wheelChange;
    Rect dr = sp.dropdown.rect();
    // Dropdown handles wheelOver event but produces no value change (None)
    check (sp.wheelOver (dr.x + 5, dr.y + 5, 1, wheelChange) && wheelChange.kind == ChangeKind::None, "Wheel over dropdown produces no change");
    // Stepper responds to wheel with Index change
    Rect sr = sp.stepper.rect();
    check (sp.wheelOver (sr.x + 5, sr.y + 5, 1, wheelChange) && wheelChange.kind == ChangeKind::Index, "Wheel over stepper changes value");

    // Disabled behavior
    sp.setEnabled (false);
    check (! sp.enabled(), "Composite setEnabled false disables composite");
    check (! sp.stepper.enabled(), "Composite setEnabled false disables children");
    c = sp.turn (1.0);
    check (c.kind == ChangeKind::None, "Disabled composite ignores turn");
}

static void test_tune_row_modes()
{
    TuneRow tr;
    tr.place ({ 50, 50, 420, 56 });

    // Standard horizontal mode
    check (tr.minSize().w == 4 * 80 + 72 + 4 * kGap, "TuneRow standard minSize width");
    check (tr.minSize().h == 56, "TuneRow standard minSize height");

    Component* leaves[8];
    int nl = tr.collectLeaves (leaves, 0, 8);
    check (nl == 5, "TuneRow standard mode has 5 shown children (OCT, SEM, FIN, CRS, track)");

    Image im = drawComponent (tr, 500, 200);
    check (drawnOutside (im, tr.rect(), backgroundPixel()) == 0, "TuneRow standard draws strictly inside rect");

    // Short mode (Sub oscillator: OCT, CRS only shown)
    tr.setShort (true);
    check (tr.isShort(), "TuneRow isShort is true");
    check (tr.minSize().w == 2 * 80 + kGap, "TuneRow short mode minSize width");
    nl = tr.collectLeaves (leaves, 0, 8);
    check (nl == 2, "TuneRow short mode has 2 shown children (OCT, CRS)");
    im = drawComponent (tr, 500, 200);
    check (drawnOutside (im, tr.rect(), backgroundPixel()) == 0, "TuneRow short draws strictly inside rect");

    // Vertical mode
    tr.setVertical (true);
    tr.place ({ 50, 50, 72, 240 });
    check (tr.isVertical(), "TuneRow isVertical() is true");
    im = drawComponent (tr, 200, 300);
    check (drawnOutside (im, tr.rect(), backgroundPixel()) == 0, "TuneRow vertical draws strictly inside rect");

    // Noise mode: PITCH, FINE, KEY TRACK
    tr.setNoise (true);
    check (tr.isNoise(), "TuneRow isNoise() is true");
    check (std::strcmp (tr.crs.state.label, "PITCH") == 0, "Noise mode CRS is labeled PITCH");
    check (std::strcmp (tr.fin.state.label, "FINE") == 0, "Noise mode FIN is labeled FINE");
}

static void test_type_bar_presets()
{
    TypeBar tb;
    tb.place ({ 50, 50, 400, 56 });

    // 1. Wavetable preset: Frame, Phase, Rand, MAP (the page of the frame remap)
    tb.load (barpresets::kWavetable);
    check (tb.numSlots() == 4, "Wavetable TypeBar has 4 slots");
    check (tb.minSize().w == tb.numbers[0].minSize().w + tb.numbers[1].minSize().w + tb.numbers[2].minSize().w + tb.details[3].minSize().w + 3 * kGap, "Wavetable TypeBar minSize sums slot minimums (three number fields and the MAP button)");
    Image im = drawComponent (tb, 500, 200);
    check (drawnOutside (im, tb.rect(), backgroundPixel()) == 0, "Wavetable TypeBar draws strictly inside rect");

    // 2. Granular preset: Xfade, GrainWindow, Loop, Unison
    tb.load (barpresets::kGranular);
    check (tb.numSlots() == 4, "Granular TypeBar has 4 slots");
    im = drawComponent (tb, 500, 200);
    check (drawnOutside (im, tb.rect(), backgroundPixel()) == 0, "Granular TypeBar draws strictly inside rect");

    // 3. Sub preset: Phase (1 slot)
    tb.load (barpresets::kSub);
    check (tb.numSlots() == 1, "Sub TypeBar has 1 slot");
    im = drawComponent (tb, 500, 200);
    check (drawnOutside (im, tb.rect(), backgroundPixel()) == 0, "Sub TypeBar draws strictly inside rect");

    // 4. Multisample preset: ENV, VEL TRACK (ToggleNumber), RAND (3 slots)
    tb.load (barpresets::kMultisample);
    check (tb.numSlots() == 3, "Multisample TypeBar has 3 slots");
    im = drawComponent (tb, 500, 200);
    check (drawnOutside (im, tb.rect(), backgroundPixel()) == 0, "Multisample TypeBar draws strictly inside rect");

    // 5. Noise preset: STEREO, ENV 1 (2 slots)
    tb.load (barpresets::kNoise);
    check (tb.numSlots() == 2, "Noise TypeBar has 2 slots");
    im = drawComponent (tb, 500, 200);
    check (drawnOutside (im, tb.rect(), backgroundPixel()) == 0, "Noise TypeBar draws strictly inside rect");
}

static void test_unison_box_and_warp_pair()
{
    UnisonBox ub;
    ub.place ({ 50, 50, 120, 200 });

    check (ub.minSize().w == 116, "UnisonBox minSize width is 116");
    check (ub.minSize().h == 112 + kGap + UnisonBox::kGearH, "UnisonBox minSize height");
    check (ub.primary() == &ub.count, "UnisonBox primary child is count UpDownStepper");

    Change c = ub.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 2, "UnisonBox turn advances voice count to 2");

    Image im = drawComponent (ub, 250, 300);
    check (drawnOutside (im, ub.rect(), backgroundPixel()) == 0, "UnisonBox draws strictly inside rect");

    // WarpPair
    WarpPair wp;
    wp.place ({ 50, 50, 600, 80 });
    check (! wp.stacked(), "WarpPair default is side-by-side (not stacked)");
    check (! wp.captionBox().empty(), "Side-by-side WarpPair has a caption box");

    wp.first.setHasVar (true);
    check (wp.first.hasVar(), "WarpSlot setHasVar updates state");

    im = drawComponent (wp, 700, 200);
    check (drawnOutside (im, wp.rect(), backgroundPixel()) == 0, "Side-by-side WarpPair draws strictly inside rect");

    // Stacked mode
    wp.setStacked (true);
    wp.place ({ 50, 50, 300, 160 });
    check (wp.stacked(), "WarpPair stacked is true");
    check (wp.captionBox().empty(), "Stacked WarpPair has empty caption box");
    check (wp.second.rect().y > wp.first.rect().y, "Stacked second slot is below first slot");

    im = drawComponent (wp, 400, 250);
    check (drawnOutside (im, wp.rect(), backgroundPixel()) == 0, "Stacked WarpPair draws strictly inside rect");
}

static void test_knob_rand_pair()
{
    KnobRandPair kp ("DETUNE");
    kp.place ({ 50, 50, 100, 140 });

    check (kp.minSize().w == 96 && kp.minSize().h == 72 + 8 + 56, "KnobRandPair minSize");
    check (kp.primary() == &kp.main, "KnobRandPair primary child is main knob");

    Change c = kp.turn (0.1);
    check (c.kind == ChangeKind::Value, "KnobRandPair turn routes to main knob");

    Image im = drawComponent (kp, 200, 250);
    check (drawnOutside (im, kp.rect(), backgroundPixel()) == 0, "KnobRandPair draws strictly inside rect");
}

static void test_basic_controls_variants()
{
    BasicControls bc;
    bc.place ({ 50, 50, 600, 72 });
    bc.assignIds (500);

    // Osc A Wavetable
    bc.configure (OscInstance::A, OscKind::Wavetable);
    check (bc.instance() == OscInstance::A, "BasicControls instance is A");
    check (bc.kind() == OscKind::Wavetable, "BasicControls kind is Wavetable");
    check (bc.group.member (0) == &bc.selector, "Group slot 0 is selector");
    check (bc.group.member (1) == &bc.source, "Group slot 1 is source");
    check (bc.group.member (2) == &bc.pan, "Group slot 2 is pan");
    check (bc.group.member (3) == &bc.level, "Group slot 3 is level");

    Image im = drawComponent (bc, 700, 200);
    check (drawnOutside (im, bc.rect(), backgroundPixel()) == 0, "BasicControls Osc A draws strictly inside rect");

    // Osc Sub
    bc.configure (OscInstance::Sub, OscKind::Wavetable);
    check (bc.instance() == OscInstance::Sub, "BasicControls instance is Sub");
    check (bc.group.member (1) == &bc.oct, "Sub group slot 1 is oct NumberField");
    im = drawComponent (bc, 700, 200);
    check (drawnOutside (im, bc.rect(), backgroundPixel()) == 0, "BasicControls Sub draws strictly inside rect");

    // Osc Noise
    bc.configure (OscInstance::Noise, OscKind::Wavetable);
    check (bc.instance() == OscInstance::Noise, "BasicControls instance is Noise");
    check (bc.group.member (0) == &bc.noiseType, "Noise group slot 0 is noiseType Stepper");
    check (bc.group.member (1) == &bc.filter, "Noise group slot 1 is filter Knob");
    im = drawComponent (bc, 700, 200);
    check (drawnOutside (im, bc.rect(), backgroundPixel()) == 0, "BasicControls Noise draws strictly inside rect");

    // Compact stacked mode
    bc.setStacked (true);
    bc.place ({ 50, 50, 260, 240 });
    check (bc.stacked(), "BasicControls stacked is true");
    im = drawComponent (bc, 350, 320);
    check (drawnOutside (im, bc.rect(), backgroundPixel()) == 0, "BasicControls stacked draws strictly inside rect");
}

static void test_knob_panel_and_filter_route_panel()
{
    KnobPanel kp;
    kp.load (presets::kWavetable);
    kp.place ({ 50, 50, 800, 300 });

    check (kp.knob (PanelSlot::WtPos) != nullptr, "KnobPanel has WtPos knob");
    check (kp.knob (PanelSlot::Pan) != nullptr, "KnobPanel has Pan knob");
    check (kp.knob (PanelSlot::Level) != nullptr, "KnobPanel has Level knob");

    Image im = drawComponent (kp, 900, 400);
    check (drawnOutside (im, kp.rect(), backgroundPixel()) == 0, "KnobPanel draws strictly inside rect");

    // FilterRoutePanel
    FilterRoutePanel frp;
    frp.place ({ 50, 50, 400, 240 });

    check (frp.minSize().w == 380 && frp.minSize().h == 220, "FilterRoutePanel minSize is 380x220");
    check (frp.primary() == &frp.destination, "FilterRoutePanel primary child is destination stepper");
    check (frp.group.member (0) == &frp.destination, "FilterRoutePanel group slot 0 is destination");
    check (frp.group.member (1) == &frp.bus1, "FilterRoutePanel group slot 1 is bus1");
    check (frp.group.member (2) == &frp.bus2, "FilterRoutePanel group slot 2 is bus2");
    check (frp.group.member (3) == &frp.level, "FilterRoutePanel group slot 3 is level");

    im = drawComponent (frp, 500, 300);
    check (drawnOutside (im, frp.rect(), backgroundPixel()) == 0, "FilterRoutePanel draws strictly inside rect");
}

static void test_mixer_strip_layout_and_q_presence()
{
    MixerStrip ms;
    // Place at height 420 (which accommodates all 6 controls and padding without overflow)
    ms.place ({ 50, 50, 100, 420 });
    ms.setTitle ("CH 1");
    ms.assignIds (600);

    ms.setPresent (true, true, true, true);
    check (ms.hasLevel(), "MixerStrip hasLevel");
    check (ms.hasPan(), "MixerStrip hasPan");
    check (ms.hasSend1(), "MixerStrip hasSend1");
    check (ms.hasSend2(), "MixerStrip hasSend2");

    // Master / bus mode: only level present
    ms.setPresent (true, false, false, false);
    check (ms.hasLevel(), "MixerStrip bus mode hasLevel");
    check (! ms.hasPan(), "MixerStrip bus mode has no pan");
    check (! ms.hasSend1(), "MixerStrip bus mode has no send1");
    check (! ms.hasSend2(), "MixerStrip bus mode has no send2");

    Image im = drawComponent (ms, 200, 500);
    check (drawnOutside (im, ms.rect(), backgroundPixel()) == 0, "MixerStrip at 420px height draws strictly inside rect");
}

int main()
{
    test_composite_base_behavior();
    test_tune_row_modes();
    test_type_bar_presets();
    test_unison_box_and_warp_pair();
    test_knob_rand_pair();
    test_basic_controls_variants();
    test_knob_panel_and_filter_route_panel();
    test_mixer_strip_layout_and_q_presence();

    std::printf ("ui_cov_composites_panels_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
