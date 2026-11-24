// SynthyBoy - MPC UI library test coverage: composites and panels
// Tests untested public behaviors of Composite, UnisonBox,
// WarpSlot, WarpPair, KnobRandPair, BasicControls, KnobPanel, FilterOutReadout,
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
    bc.place ({ 50, 50, 420, 232 });   // the compact row of the Play page: the type cells, then ONE row (the second member, PAN, LEVEL)
    check (bc.stacked(), "BasicControls stacked is true");
    im = drawComponent (bc, 520, 320);
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

    // FilterOutReadout (the read-only OUT indicators of the Filter page)
    FilterOutReadout ro;
    ro.place ({ 50, 50, 200, 160 });
    check (ro.minSize().w == 170 && ro.minSize().h == 140, "FilterOutReadout minSize is 170x140");
    Rect none[2];
    check (ro.touchTargets (none, 2) == 0, "FilterOutReadout has no touch target");
    check (ro.touchDown (100, 100, 0.0).kind == ChangeKind::None && ! ro.capturing(), "FilterOutReadout gives no change on a touch and does not capture it");
    check (std::string (FilterOutReadout::rowName (0)) == "DEST" && std::string (FilterOutReadout::rowName (4)) == "PAN", "FilterOutReadout rows are DEST, BUS 1, BUS 2, LEVEL, PAN");
    im = drawComponent (ro, 300, 250);
    check (drawnOutside (im, { 0, 0, 0, 0 }, backgroundPixel()) == 0, "FilterOutReadout leaves all base artwork to native MPC");
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

// Regression (library finding 3 and 4 of the first coverage round): a mixer strip placed at the height it reports as its minimum lays all its members inside itself; a chain bar shows only the cards of the
// slots it has (the others are not placed and not drawn), also before any layout.
static void test_mixer_strip_min_size_and_chain_bar_cards()
{
    MixerStrip ms;
    const Size minS = ms.minSize();
    check (minS.w == 96 && minS.h == MixerStrip::kMinH && minS.h == 456, "MixerStrip reports the height its layout needs (456)");
    ms.place ({ 0, 0, minS.w, minS.h });
    ms.assignIds (700);
    ms.setPresent (true, true, true, true);
    bool inside = true;
    for (const Component* c : { static_cast<const Component*> (&ms.power), static_cast<const Component*> (&ms.destButton), static_cast<const Component*> (&ms.pan),
                                static_cast<const Component*> (&ms.send1), static_cast<const Component*> (&ms.send2), static_cast<const Component*> (&ms.fader) })
        inside = inside && ms.rect().contains (c->rect());
    check (inside, "at its minimum height every member of the strip lies inside it (send 2 included)");
    check (ms.fader.rect().h >= 96 && ms.send2.rect().bottom() <= ms.rect().bottom(), "the fader keeps its 96 px and the last slider ends inside the strip");
    const Image im = drawComponent (ms, 200, 500);
    check (drawnOutside (im, ms.rect(), backgroundPixel()) == 0, "the strip at its minimum height draws strictly inside its rectangle");

    FxChainBar fb;
    check (! fb.childShown (10) && fb.childShown (0) && fb.childShown (5), "a chain bar that has not been given its slots shows the cards of the first six only");
    fb.place ({ 8, 100, 1264, 88 });   // (the room the FX page gives it)
    const Image im2 = drawComponent (fb, 1280, 300);
    check (drawnOutside (im2, fb.rect(), backgroundPixel()) == 0, "a chain bar draws inside its rectangle: no card at the origin");
    fb.setNumActiveSlots (2);
    check (fb.childShown (1) && ! fb.childShown (2) && ! fb.childShown (10), "the cards follow the number of slots");
}

int main()
{
    test_composite_base_behavior();
    test_unison_box_and_warp_pair();
    test_knob_rand_pair();
    test_basic_controls_variants();
    test_knob_panel_and_filter_route_panel();
    test_mixer_strip_layout_and_q_presence();

    test_mixer_strip_min_size_and_chain_bar_cards();
    std::printf ("ui_cov_composites_panels_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
