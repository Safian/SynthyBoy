// SynthyBoy - MPC UI library coverage tests: Knobs and Values
// Tests Rotary Knobs (standard, small, large, side), NumberFields, HBars, FilterKnobs,
// Faders, and SliderBars: gestures, sensitivity, wheel input, value formatting, clamping,
// modulation tabs, tick long-press, disabled states, and bounds containment.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui_test.h"
#include "../filter_display.h"
#include "../mixer_strip.h"

using namespace uit;

namespace {

bool near(double a, double b, double eps = 1.0e-5)
{
    return std::fabs(a - b) <= eps;
}

Change dragVertical(Component& c, int x, int y0, int y1, double t = 0.0)
{
    Change last = c.touchDown(x, y0, t);
    const int dir = y1 > y0 ? 1 : -1;
    for (int y = y0 + dir; dir > 0 ? y <= y1 : y >= y1; y += dir * 4)
    {
        if (const Change m = c.touchMove(x, y))
            last = m;
    }
    if (const Change m = c.touchMove(x, y1))
        last = m;
    return last;
}

Change dragHorizontal(Component& c, int x0, int x1, int y, double t = 0.0)
{
    Change last = c.touchDown(x0, y, t);
    const int dir = x1 > x0 ? 1 : -1;
    for (int x = x0 + dir; dir > 0 ? x <= x1 : x >= x1; x += dir * 4)
    {
        if (const Change m = c.touchMove(x, y))
            last = m;
    }
    if (const Change m = c.touchMove(x1, y))
        last = m;
    return last;
}

void testKnobWheelAndGestures()
{
    Knob k("CUTOFF");
    k.place({100, 100, 120, 100});
    k.setId(10);
    k.state.value = 0.5;
    k.state.defaultValue = 0.5;

    // 1. Wheel input: wheelUnit is 1/128
    check(near(k.wheelUnit(), 1.0 / 128.0), "knob: wheelUnit is 1/128 of range");
    Change c = k.wheel(1);
    check(c.kind == ChangeKind::Value && c.id == 10 && near(c.value, 0.5 + 1.0 / 128.0),
          "knob: wheel(+1) advances by 1/128");
    k.state.value = c.value;

    c = k.wheel(-2);
    check(c.kind == ChangeKind::Value && c.id == 10 && near(c.value, 0.5 - 1.0 / 128.0),
          "knob: wheel(-2) steps backward by 2/128");

    // Clamping at ends
    k.state.value = 1.0;
    c = k.wheel(1);
    check(!c, "knob: wheel(+1) at 1.0 clamps and gives none");

    k.state.value = 0.0;
    c = k.wheel(-1);
    check(!c, "knob: wheel(-1) at 0.0 clamps and gives none");

    // wheelOver hit testing
    k.state.value = 0.5;
    Change out;
    check(k.wheelOver(150, 150, 1, out) && out.kind == ChangeKind::Value,
          "knob: wheelOver inside rect succeeds");
    check(!k.wheelOver(50, 50, 1, out), "knob: wheelOver outside rect returns false");

    // Disabled knob
    k.setEnabled(false);
    check(!k.wheel(1), "knob: disabled wheel returns none");
    check(!k.wheelOver(150, 150, 1, out), "knob: disabled wheelOver returns false");
    check(!k.turn(0.1), "knob: disabled turn returns none");
    k.setEnabled(true);

    // 2. Variants: side, small, large (layout metadata & minSize)
    Knob sideK("PAN", false);
    sideK.state.side = true;
    sideK.place({100, 100, 180, 80});
    check(sideK.minSize().w == 172 && sideK.minSize().h == 72 && sideK.sizeOk(), "knob: side variant minSize is 172x72");

    Knob smallK("TUNE", true);
    check(smallK.minSize().w == 72 && smallK.minSize().h == 56, "knob: small variant minSize is 72x56");

    Knob largeK("DRIVE", false);
    largeK.state.large = true;
    largeK.place({100, 100, 250, 250});
    check(largeK.sizeOk() && largeK.state.large, "knob: large variant preserves large state flag and geometry");

    // 3. Turn clamping
    k.state.value = 0.5;
    c = k.turn(0.1);
    check(c.kind == ChangeKind::Value && c.id == 10 && near(c.value, 0.6), "knob: turn(+0.1) advances value to 0.6");
    k.state.value = 1.0;
    check(!k.turn(0.1), "knob: turn(+0.1) at 1.0 clamps and gives none");
    k.state.value = 0.0;
    check(!k.turn(-0.1), "knob: turn(-0.1) at 0.0 clamps and gives none");

    // 4. Modulation tab model state
    k.state.numMods = 0;
    k.state.add(5, "LFO 1", 0.2);
    k.state.selectedSource = 5;
    check(tabState(k.state) == TabState::Coloured && k.tab() == TabState::Coloured, "knob: tabState is Coloured when selected source present");

    // Bypassed mod tab
    k.state.mods[0].bypassed = true;
    check(tabState(k.state) == TabState::Hollow && k.tab() == TabState::Hollow, "knob: tabState is Hollow when source is bypassed");
}

void testNumberFieldCoverage()
{
    NumberField nf("RATIO", NumberField::Format::Decimal2);
    nf.place({50, 50, 100, 60});
    nf.setId(22);
    nf.state.min = 0.0;
    nf.state.max = 10.0;
    nf.state.step = 0.5;
    nf.state.value = 2.0;
    nf.state.defaultValue = 2.0;
    nf.state.pxPerStep = 8;

    // 1. Wheel input
    Change c = nf.wheel(1);
    check(c.kind == ChangeKind::Index && c.id == 22 && c.index == 5 && near(c.value, 2.5),
          "number field: wheel(+1) advances by 1 step (to 2.5)");
    nf.state.value = 2.5;

    c = nf.wheel(-2);
    check(c.kind == ChangeKind::Index && c.id == 22 && c.index == 3 && near(c.value, 1.5),
          "number field: wheel(-2) steps down by 2 steps (to 1.5)");

    Change out;
    check(nf.wheelOver(80, 80, 1, out), "number field: wheelOver inside rect returns true");
    check(!nf.wheelOver(10, 10, 1, out), "number field: wheelOver outside rect returns false");

    // Clamping at ends
    nf.state.value = 10.0;
    check(!nf.wheel(1), "number field: wheel at max clamps and returns none");
    nf.state.value = 0.0;
    check(!nf.wheel(-1), "number field: wheel at min clamps and returns none");

    // 2. ReadOnly mode
    nf.state.readOnly = true;
    nf.state.value = 2.0;
    check(!nf.wheel(1), "number field: readOnly ignores wheel");
    check(!nf.turn(1.0), "number field: readOnly ignores turn");
    nf.state.readOnly = false;

    // 3. Format value formatting
    char buf[16];
    NumberField::State s;
    s.notSet = true;
    NumberField::formatValue(s, buf);
    check(std::strcmp(buf, "-") == 0, "number field: notSet formats as \"-\"");

    s.notSet = false;
    s.format = NumberField::Format::IntSigned;
    s.value = 4.0;
    NumberField::formatValue(s, buf);
    check(std::strcmp(buf, "+4") == 0, "number field: IntSigned formats positive as \"+4\"");

    s.value = -3.0;
    NumberField::formatValue(s, buf);
    check(std::strcmp(buf, "-3") == 0, "number field: IntSigned formats negative as \"-3\"");

    s.value = 0.0;
    NumberField::formatValue(s, buf);
    check(std::strcmp(buf, "0") == 0, "number field: IntSigned formats zero as \"0\"");

    s.format = NumberField::Format::Decimal2;
    s.value = 3.14159;
    NumberField::formatValue(s, buf);
    check(std::strcmp(buf, "3.14") == 0, "number field: Decimal2 formats as \"3.14\"");

    s.format = NumberField::Format::Degrees;
    s.value = 90.0;
    NumberField::formatValue(s, buf);
    check(std::strcmp(buf, "90\xc2\xb0") == 0, "number field: Degrees formats as \"90°\"");

    s.format = NumberField::Format::IntPlain;
    s.value = 127.0;
    NumberField::formatValue(s, buf);
    check(std::strcmp(buf, "127") == 0, "number field: IntPlain formats as \"127\"");

    // 4. Turn steps and formatText
    nf.state.value = 2.0;
    c = nf.turn(2.0);
    check(c.kind == ChangeKind::Index && c.id == 22 && c.index == 6 && near(c.value, 3.0),
          "number field: turn(+2) advances by 2 steps (to 3.0)");
    nf.formatText(buf, sizeof buf);
    check(std::strcmp(buf, "2.00") == 0,
          "number field: formatText formats current state.value");

    // 5. MinSize and sizeOk
    check(nf.minSize().w == 80 && nf.minSize().h == 56 && nf.sizeOk(), "number field: minSize is 80x56");
}

void testHBarCoverage()
{
    HBar bar;
    bar.place({50, 50, 240, 56});
    bar.setId(33);
    bar.state.value = 0.5;
    bar.state.defaultValue = 0.5;
    bar.state.bipolar = true;

    // 1. Wheel input: wheelUnit is 1/128
    check(near(bar.wheelUnit(), 1.0 / 128.0), "hbar: wheelUnit is 1/128");
    Change c = bar.wheel(2);
    check(c.kind == ChangeKind::Value && c.id == 33 && near(c.value, 0.5 + 2.0 / 128.0),
          "hbar: wheel(+2) advances by 2/128");
    bar.state.value = c.value;

    c = bar.wheel(-3);
    check(c.kind == ChangeKind::Value && c.id == 33 && near(c.value, 0.5 - 1.0 / 128.0),
          "hbar: wheel(-3) steps back");

    // Clamping at ends
    bar.state.value = 1.0;
    check(!bar.wheel(1), "hbar: wheel at 1.0 clamps and gives none");
    bar.state.value = 0.0;
    check(!bar.wheel(-1), "hbar: wheel at 0.0 clamps and gives none");

    Change out;
    check(bar.wheelOver(100, 70, 1, out), "hbar: wheelOver inside rect succeeds");
    check(!bar.wheelOver(10, 10, 1, out), "hbar: wheelOver outside rect returns false");

    // 2. Drag sensitivity: dx / (track().w - 16)
    bar.state.value = 0.5;
    const int trackW = std::max(1, bar.track().w - 16);
    // drag right by trackW / 4 px -> +0.25
    c = dragHorizontal(bar, 100, 100 + trackW / 4, 70);
    check(c.kind == ChangeKind::Value && near(c.value, 0.75), "hbar: drag right by 25% of track gives 0.75");
    bar.touchUp(100 + trackW / 4, 70);

    // 3. Double-tap resets to default
    bar.state.value = 0.9;
    bar.touchDown(100, 70, 0.0);
    bar.touchUp(100, 70);
    bar.touchDown(100, 70, 100.0);
    c = bar.touchUp(100, 70);
    check(c.kind == ChangeKind::Value && near(c.value, 0.5), "hbar: double tap resets to default 0.5");

    // 4. Value text formatting
    bar.setValueText("CUSTOM");
    check(std::strcmp(bar.state.text, "CUSTOM") == 0, "hbar: custom text set correctly");

    // 5. MinSize and drawing
    check(bar.minSize().w == 200 && bar.minSize().h == 56, "hbar: minSize is 200x56");
    Scene sc(400, 200, kBackgroundRgba);
    bar.draw(sc.builder());
    Image im = render(sc);
    check(drawnOutside(im, bar.rect(), backgroundPixel()) == 0, "hbar: draws strictly inside rect");
}

void testFilterKnobCoverage()
{
    FilterKnob fk("RESO");
    fk.place({50, 50, 120, 100});
    fk.setId(44);
    fk.state.value = 0.4;
    fk.state.defaultValue = 0.2;

    // Enabled matches Knob
    Change c = fk.wheel(1);
    check(c.kind == ChangeKind::Value && c.id == 44 && near(c.value, 0.4 + 1.0 / 128.0),
          "filter knob: enabled wheel works like knob");
    c = fk.turn(0.1);
    check(c.kind == ChangeKind::Value && c.id == 44 && near(c.value, 0.5),
          "filter knob: enabled turn works like knob");

    // Disabled model state blocks turn/wheel
    fk.setEnabled(false);
    check(!fk.turn(0.1) && !fk.wheel(1), "filter knob: disabled model rejects turn and wheel");
    fk.setEnabled(true);
}

void testFaderAndSliderBarExpected()
{
    // Fader
    Fader f("VOL");
    f.place({100, 100, 80, 200});
    f.setId(55);
    f.state.value = 0.5;
    f.state.defaultValue = 0.75;
    f.state.hasMeter = true;
    f.state.numTicks = 4;
    f.state.tickPos[0] = 0.0f; std::strcpy(f.state.tickText[0], "-inf");
    f.state.tickPos[1] = 0.5f; std::strcpy(f.state.tickText[1], "-12");
    f.state.tickPos[2] = 0.75f; std::strcpy(f.state.tickText[2], "0");
    f.state.tickPos[3] = 1.0f; std::strcpy(f.state.tickText[3], "+6");

    // Vertical drag: kDragPx = 200. Drag up 50 px -> +0.25
    Change c = dragVertical(f, 130, 200, 150);
    check(c.kind == ChangeKind::Value && near(c.value, 0.75), "fader: drag up 50px gives +0.25 (to 0.75)");
    f.touchUp(130, 150);

    // Double-tap resets to default
    f.state.value = 0.2;
    f.touchDown(130, 200, 0.0);
    f.touchUp(130, 200);
    f.touchDown(130, 200, 100.0);
    c = f.touchUp(130, 200);
    check(c.kind == ChangeKind::Value && near(c.value, 0.75), "fader: double tap resets to default 0.75");

    // Turn
    f.state.value = 0.5;
    c = f.turn(0.1);
    check(c.kind == ChangeKind::Value && near(c.value, 0.6), "fader: turn(+0.1) advances value");

    // MinSize and draw
    check(f.minSize().w == 56 && f.minSize().h == 120, "fader: minSize is 56x120");
    Scene scF(300, 400, kBackgroundRgba);
    f.draw(scF.builder());
    Image imF = render(scF);
    check(drawnOutside(imF, f.rect(), backgroundPixel()) == 0, "fader: draws strictly inside rect");

    // SliderBar
    SliderBar sb("PAN", true);
    sb.place({100, 100, 120, 56});
    sb.setId(66);
    sb.state.value = 0.5;
    sb.state.defaultValue = 0.5;

    // Horizontal drag: dx / track().w
    const int sbW = std::max(1, sb.track().w);
    c = dragHorizontal(sb, 120, 120 + sbW / 4, 128);
    check(c.kind == ChangeKind::Value && near(c.value, 0.75), "slider bar: drag right 25% of track gives 0.75");
    sb.touchUp(120 + sbW / 4, 128);

    // Turn
    sb.state.value = 0.5;
    c = sb.turn(-0.2);
    check(c.kind == ChangeKind::Value && near(c.value, 0.3), "slider bar: turn(-0.2) reduces value");

    // Double tap
    sb.state.value = 0.9;
    sb.touchDown(120, 128, 0.0);
    sb.touchUp(120, 128);
    sb.touchDown(120, 128, 100.0);
    c = sb.touchUp(120, 128);
    check(c.kind == ChangeKind::Value && near(c.value, 0.5), "slider bar: double tap resets to default 0.5");

    check(sb.minSize().w == 96 && sb.minSize().h == 56, "slider bar: minSize is 96x56");
    Scene scS(300, 200, kBackgroundRgba);
    sb.draw(scS.builder());
    Image imS = render(scS);
    check(drawnOutside(imS, sb.rect(), backgroundPixel()) == 0, "slider bar: draws strictly inside rect");
}

} // namespace

// Regression (library finding 1 and 2 of the first coverage round): the mouse wheel moves a fader and a slider bar by one Q-Link step (1 / 128 of the range, not the whole range), a touch outside the
// rectangle is not captured, and capturing () tells the truth for both.
void testFaderAndSliderBarWheelAndCapture()
{
    SliderBar sb ("PAN", true);
    sb.setId (1);
    sb.place ({ 50, 50, 80, 56 });
    sb.state.value = 0.5;
    check (near (sb.wheelUnit(), 1.0 / 128.0), "slider bar: one wheel notch is 1 / 128 of the range");
    Change c = sb.wheel (1);
    check (c.kind == ChangeKind::Value && near (c.value, 0.5 + 1.0 / 128.0), "slider bar: one notch moves it by one step, not to the end");
    sb.touchDown (999, 999, 0.0);
    check (! sb.capturing(), "slider bar: a touch outside is not captured");
    sb.touchDown (60, 70, 0.0);
    check (sb.capturing(), "slider bar: a touch inside is captured (capturing () says so)");
    sb.touchUp (60, 70);
    check (! sb.capturing(), "slider bar: the lift releases it");

    Fader f ("LEVEL");
    f.setId (2);
    f.place ({ 50, 50, 80, 160 });
    f.state.value = 0.5;
    check (near (f.wheelUnit(), 1.0 / 128.0), "fader: one wheel notch is 1 / 128 of the range");
    c = f.wheel (1);
    check (c.kind == ChangeKind::Value && near (c.value, 0.5 + 1.0 / 128.0), "fader: one notch moves it by one step, not to the end");
    f.state.value = 0.5;
    c = f.wheel (-1);
    check (c.kind == ChangeKind::Value && near (c.value, 0.5 - 1.0 / 128.0), "fader: the wheel down moves it down by one step");
    c = f.touchDown (999, 999, 1.0);
    check (! c && ! f.capturing(), "fader: a touch outside the rectangle is not captured");
    check (! f.touchMove (999, 900) && ! f.touchUp (999, 900), "fader: and the move and the lift of it change nothing");
    f.touchDown (90, 120, 2.0);
    check (f.capturing(), "fader: a touch inside is captured (capturing () says so)");
    f.setEnabled (false);
    check (! f.capturing(), "fader: disabling it drops the capture");
    f.setEnabled (true);
    f.touchDown (90, 120, 3.0);
    f.touchUp (90, 120);
    check (! f.capturing(), "fader: the lift releases it");
}

void testKnobActiveInactiveStatus()
{
    // 1. Standard active knob: no status string
    Knob k ("LEVEL");
    k.place ({ 100, 100, 100, 72 });
    k.state.value = 0.5;
    setText (k.state.valueText, sizeof k.state.valueText, "50%");
    check (! k.hasStatus(), "knob: active knob is not inactive state");
    check (std::strcmp (k.statusString(), "") == 0, "knob: active knob has empty status string");

    // 2. Inactive via Status enum: Off, Inactive, Disabled
    k.state.status = Knob::State::Status::Off;
    check (k.hasStatus(), "knob: status Off is inactive state");
    check (std::strcmp (k.statusString(), "OFF") == 0, "knob: status Off gives 'OFF'");

    k.state.status = Knob::State::Status::Inactive;
    check (k.hasStatus(), "knob: status Inactive is inactive state");
    check (std::strcmp (k.statusString(), "INACTIVE") == 0, "knob: status Inactive gives 'INACTIVE'");

    k.state.status = Knob::State::Status::Disabled;
    check (k.hasStatus(), "knob: status Disabled is inactive state");
    check (std::strcmp (k.statusString(), "DISABLED") == 0, "knob: status Disabled gives 'DISABLED'");

    // A display string and the generic grey look do not change the model status.
    k.state.status = Knob::State::Status::Active;
    setText (k.state.valueText, sizeof k.state.valueText, "OFF");
    k.setInactive (true);
    check (! k.hasStatus() && k.statusString()[0] == '\0',
           "knob: generic greying and display formatting do not imply a status");
    k.setInactive (false);

    // 5. Side knob variant with PAN and LEVEL
    Knob sidePan ("PAN");
    sidePan.state.side = true;
    sidePan.state.bipolar = true;
    sidePan.place ({ 50, 50, 172, 72 });
    setText (sidePan.state.valueText, sizeof sidePan.state.valueText, "C");
    check (! sidePan.hasStatus(), "side PAN: active is not inactive");
    check (sidePan.qIndex() == 0, "side PAN: no separate badge");

    sidePan.state.status = Knob::State::Status::Off;
    check (sidePan.hasStatus(), "side PAN: status Off is inactive");
    check (std::strcmp (sidePan.statusString(), "OFF") == 0, "side PAN: statusString is OFF");

    // 6. 48 px compact PAN (bipolar) and LEVEL (unipolar) side variants
    {
        Knob compactPan ("PAN");
        compactPan.state.compact = true;
        compactPan.state.side = true;
        compactPan.state.bipolar = true;
        compactPan.state.value = compactPan.state.modValue = 0.5;
        compactPan.state.dialMax = 40;
        setText (compactPan.state.valueText, sizeof compactPan.state.valueText, "C");
        check (compactPan.minSize().w == 172 && compactPan.minSize().h == 48,
               "compact knob: minSize is 172x48");

        compactPan.place ({ 30, 20, 202, 48 });
        check (compactPan.rect().h == 48 && compactPan.sizeOk(), "compact PAN: placed height is 48 px");
        check (compactPan.qIndex() == 0, "compact PAN: no visible Q-Link badge");

        // Inactive / Off state
        compactPan.state.status = Knob::State::Status::Off;
        compactPan.setEnabled (false);
        check (compactPan.hasStatus(), "compact PAN: status Off is inactive");
        check (std::strcmp (compactPan.statusString(), "OFF") == 0, "compact PAN: status is OFF");
        check (! compactPan.turn (0.1) && ! compactPan.wheel (1), "compact PAN: disabled/off ignores turn and wheel");
    }

    {
        Knob compactLevel ("LEVEL");
        compactLevel.state.compact = true;
        compactLevel.state.side = true;
        compactLevel.state.bipolar = false;
        compactLevel.state.value = compactLevel.state.modValue = 0.5;
        compactLevel.state.dialMax = 40;
        setText (compactLevel.state.valueText, sizeof compactLevel.state.valueText, "50.0");

        compactLevel.place ({ 30, 80, 202, 48 });
        check (compactLevel.rect().h == 48 && compactLevel.sizeOk(), "compact LEVEL: placed height is 48 px");
        check (compactLevel.qIndex() == 0, "compact LEVEL: no visible Q-Link badge");

        // Inactive / Off state
        compactLevel.state.status = Knob::State::Status::Off;
        compactLevel.setEnabled (false);
        check (compactLevel.hasStatus(), "compact LEVEL: status Off is inactive");
        check (std::strcmp (compactLevel.statusString(), "OFF") == 0, "compact LEVEL: status is OFF");
    }
}

int main()
{
    testKnobWheelAndGestures();
    testNumberFieldCoverage();
    testHBarCoverage();
    testFilterKnobCoverage();
    testFaderAndSliderBarExpected();

    testFaderAndSliderBarWheelAndCapture();
    testKnobActiveInactiveStatus();
    std::printf(g_fails ? "ui_cov_knobs_values_host: FAILED (%d)\n" : "ui_cov_knobs_values_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
