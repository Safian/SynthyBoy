// SynthyBoy - UI tests: the pure view-model (key resolution, cap dimming, gestures, layout modes, assign flow, A/B). No JUCE, no display.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
#include <cmath>
#include <cstring>
#include <string>

#include "ModModel.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
/** ModMatrixAccess on a plain array, counting gestures. */
class FakeMatrix final : public ModMatrixAccess
{
public:
    FakeMatrix() : values (ParamValues::defaults()) {}

    float get (int i) const override { return values.plain[static_cast<size_t> (i)]; }
    void set (int i, float v) override { values.plain[static_cast<size_t> (i)] = v; ++writes; }
    void beginGesture (int) override { ++open; ++begins; }
    void endGesture (int) override { --open; }

    ParamValues values;
    int writes = 0, open = 0, begins = 0;
};

int idx (const char* key)
{
    return resolveKey (key).index;
}

float getKey (const FakeMatrix& m, const char* key)
{
    return m.get (idx (key));
}
} // namespace

TEST (ui_resolve_key_by_key)
{
    const KeyRef r = resolveKey ("filter1.cutoff");
    CHECK (r.valid());
    CHECK_EQ (std::string (r.info->key), std::string ("filter1.cutoff"));
    CHECK_EQ (r.index, findParamIndex (r.info->id));
    CHECK (! resolveKey ("no.such.key").valid());
    CHECK (! resolveKey ("").valid());
    CHECK (! resolveKey (std::string (200, 'x')).valid());

    // the keys the shell uses all exist
    for (int n = 1; n <= 8; ++n)
        CHECK (resolveKey (macroKey (n)).valid());
}

TEST (ui_default_kind_and_names)
{
    CHECK (defaultKind (*resolveKey ("master.volume").info) == ControlKind::knob);
    CHECK (defaultKind (*resolveKey ("master.legato").info) == ControlKind::toggle);
    CHECK (defaultKind (*resolveKey ("osc1.wavetable").info) == ControlKind::choice);
    CHECK_EQ (shortName (*resolveKey ("master.volume").info), std::string ("Volume"));
    CHECK_EQ (shortName (*resolveKey ("osc1.pan").info), std::string ("Pan"));
    CHECK (shortName (*resolveKey ("macro.1").info).size() > 0);
}

TEST (ui_display_text_with_units)
{
    const ParamInfo& cutoff = *resolveKey ("filter1.cutoff").info;
    CHECK_EQ (displayText (cutoff, 440.0f), std::string ("440.0 Hz"));
    CHECK_EQ (displayText (cutoff, 12000.0f), std::string ("12.00 kHz"));
    CHECK_EQ (displayText (*resolveKey ("master.volume").info, -12.0f), std::string ("-12.0 dB"));
    CHECK_EQ (displayText (*resolveKey ("osc1.pan").info, 25.0f), std::string ("25.0 %"));
    CHECK_EQ (displayText (*resolveKey ("master.legato").info, 1.0f), std::string ("On"));
    CHECK_EQ (displayText (*resolveKey ("master.polyphony").info, 16.0f).substr (0, 2), std::string ("16"));
}

TEST (ui_cap_dimming_decisions)
{
    // a key of state version 2 needs nothing: available whatever the engine reports
    const auto vol = capStateForKey (idx ("master.volume"), kCapNone);
    CHECK (vol.available);
    CHECK (vol.tooltip.empty());

    // a version 3 key without its cap bit: dimmed with the tooltip text from the brief
    const auto tuning = capStateForKey (idx ("master.tuning.a4"), kCapNone);
    CHECK (! tuning.available);
    CHECK_EQ (tuning.tooltip, std::string ("not available in this engine"));
    CHECK ((tuning.missing & kCapTuning) != 0);

    // ... and available once the engine reports the bit
    CHECK (capStateForKey (idx ("master.tuning.a4"), kCapTuning).available);
    CHECK (capStateForKey (idx ("master.tuning.a4"), kCapAllKnown).available);

    // macro.5 needs kCapMacros58, macro.4 nothing
    CHECK (capStateForKey (idx ("macro.4"), kCapNone).available);
    CHECK (! capStateForKey (idx ("macro.5"), kCapNone).available);
    CHECK (capStateForKey (idx ("macro.5"), kCapMacros58).available);

    // an appended entry of an old list: osc1.wavetable = Preset table needs kCapUserWavetable, Saw does not
    const int wt = idx ("osc1.wavetable");
    CHECK (capStateForValue (wt, 1.0f, kCapNone).available);
    CHECK (! capStateForValue (wt, static_cast<float> (kWavetablePresetTable), kCapNone).available);
    CHECK (capStateForValue (wt, static_cast<float> (kWavetablePresetTable), kCapUserWavetable).available);

    // an invalid index never dims
    CHECK (capStateForKey (-1, kCapNone).available);
}

TEST (ui_gesture_maths)
{
    CHECK (zoneForOffset (0.0f) == FineZone::normal);
    CHECK (zoneForOffset (kFineZoneWidthPx - 1.0f) == FineZone::normal);
    CHECK (zoneForOffset (-(kFineZoneWidthPx + 1.0f)) == FineZone::fine); // either side
    CHECK (zoneForOffset (kFineZoneWidthPx * 2.0f + 1.0f) == FineZone::ultra);
    CHECK_NEAR (zoneFactor (FineZone::normal), 1.0, 1e-6);
    CHECK_NEAR (zoneFactor (FineZone::fine), 0.25, 1e-6);
    CHECK_NEAR (zoneFactor (FineZone::ultra), 0.05, 1e-6);

    // 200 px = the whole range; the fine zones scale it; always clamped
    CHECK_NEAR (applyDragDelta (0.0f, 100.0f, FineZone::normal), 0.5, 1e-6);
    CHECK_NEAR (applyDragDelta (0.5f, 100.0f, FineZone::fine), 0.625, 1e-6);
    CHECK_NEAR (applyDragDelta (0.5f, -200.0f, FineZone::ultra), 0.45, 1e-6);
    CHECK_NEAR (applyDragDelta (0.9f, 500.0f, FineZone::normal), 1.0, 1e-6);
    CHECK_NEAR (applyDragDelta (0.1f, -500.0f, FineZone::normal), 0.0, 1e-6);

    // double-tap = reset (Q3); long press = hold without moving
    CHECK (isDoubleTap (200, 5.0f));
    CHECK (! isDoubleTap (kDoubleTapMs + 1, 5.0f));
    CHECK (! isDoubleTap (100, kDoubleTapSlopPx + 1.0f));
    CHECK (isLongPress (kLongPressMs, 0.0f));
    CHECK (! isLongPress (kLongPressMs - 1, 0.0f));
    CHECK (! isLongPress (kLongPressMs + 100, kLongPressSlopPx + 1.0f));
}

TEST (ui_layout_modes_switch_by_width)
{
    CHECK (layoutModeForWidth (390) == LayoutMode::phone);
    CHECK (layoutModeForWidth (599) == LayoutMode::phone);
    CHECK (layoutModeForWidth (600) == LayoutMode::tablet);
    CHECK (layoutModeForWidth (844) == LayoutMode::tablet);
    CHECK (layoutModeForWidth (999) == LayoutMode::tablet);
    CHECK (layoutModeForWidth (1000) == LayoutMode::wide);
    CHECK (layoutModeForWidth (1024) == LayoutMode::wide);
    CHECK_EQ (columnsFor (LayoutMode::phone), 1);
    CHECK_EQ (columnsFor (LayoutMode::tablet), 2);
    CHECK_EQ (columnsFor (LayoutMode::wide), 3);
    CHECK_EQ (std::string (pageLabel (Page::glb)), std::string ("GLB"));
}

TEST (ui_source_chip_labels)
{
    CHECK_EQ (sourceChipLabel (1), std::string ("ENV1"));
    CHECK_EQ (sourceChipLabel (sourceChoiceForKey ("macro1")), std::string ("M1"));
    CHECK_EQ (sourceChipLabel (sourceChoiceForKey ("lfo3")), std::string ("LFO3"));
    CHECK_EQ (sourceChoiceForKey ("macro1"), 19);
    CHECK_EQ (sourceChoiceForKey ("nope"), 0);
    CHECK_EQ (sourceChipLabel (0), std::string());
    CHECK_EQ (sourceChipLabel (kNumModSources + 1), std::string());
}

TEST (ui_assign_flow_state_machine)
{
    FakeMatrix m;
    AssignFlow flow (m);
    const int cutoff = idx ("filter1.cutoff");
    const int lfo1 = sourceChoiceForKey ("lfo1");

    CHECK (flow.phase() == AssignFlow::Phase::idle);
    CHECK (! flow.isTarget (cutoff));
    CHECK (flow.beginDrag (cutoff) == AssignFlow::BeginResult::notArmed);

    // tap the chip: armed; every modulatable key is a target, a stepped non-modulatable one is not
    CHECK (flow.tapSource (lfo1) == AssignFlow::Phase::armed);
    CHECK_EQ (flow.armedSource(), lfo1);
    CHECK (flow.isTarget (cutoff));
    CHECK (! flow.isTarget (idx ("master.legato")));
    CHECK (flow.beginDrag (idx ("master.legato")) == AssignFlow::BeginResult::notModulatable);
    CHECK (flow.phase() == AssignFlow::Phase::armed);

    // drag: slot 0 is taken, source and destination are written, the gesture is open
    CHECK (flow.beginDrag (cutoff) == AssignFlow::BeginResult::ok);
    CHECK (flow.phase() == AssignFlow::Phase::dragging);
    CHECK_EQ (flow.activeSlot(), 0);
    CHECK_EQ (static_cast<int> (getKey (m, "mod01.source")), lfo1);
    CHECK_EQ (static_cast<int> (getKey (m, "mod01.destination")), static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (cutoff))));
    CHECK_NEAR (getKey (m, "mod01.amount"), 0.0, 1e-6);
    CHECK_EQ (m.open, 1);

    // dragging up raises the amount (200 px = whole -100..100 range), fine zone scales it
    flow.dragBy (50.0f, FineZone::normal);
    CHECK_NEAR (getKey (m, "mod01.amount"), 50.0, 0.01);
    flow.dragBy (-50.0f, FineZone::fine);
    CHECK_NEAR (getKey (m, "mod01.amount"), 50.0 - 12.5, 0.01);
    flow.dragBy (1000.0f, FineZone::normal);
    CHECK_NEAR (getKey (m, "mod01.amount"), 100.0, 1e-3); // clamped
    flow.setAmount (-30.0f);
    CHECK_NEAR (getKey (m, "mod01.amount"), -30.0, 1e-3);

    // release: armed again (next knob), the gesture is closed
    flow.endDrag();
    CHECK (flow.phase() == AssignFlow::Phase::armed);
    CHECK_EQ (flow.activeSlot(), -1);
    CHECK_EQ (m.open, 0);

    // the same (source, destination) again reuses slot 0 and keeps its amount; another destination takes slot 1
    CHECK (flow.beginDrag (cutoff) == AssignFlow::BeginResult::ok);
    CHECK_EQ (flow.activeSlot(), 0);
    CHECK_NEAR (getKey (m, "mod01.amount"), -30.0, 1e-3);
    flow.endDrag();
    CHECK (flow.beginDrag (idx ("osc1.level")) == AssignFlow::BeginResult::ok);
    CHECK_EQ (flow.activeSlot(), 1);
    flow.endDrag();

    // another source on the same knob is another slot
    CHECK (flow.tapSource (sourceChoiceForKey ("env2")) == AssignFlow::Phase::armed);
    CHECK_EQ (flow.armedSource(), sourceChoiceForKey ("env2"));
    CHECK (flow.beginDrag (cutoff) == AssignFlow::BeginResult::ok);
    CHECK_EQ (flow.activeSlot(), 2);
    flow.endDrag();

    // the armed chip again leaves the mode; cancel does too
    CHECK (flow.tapSource (sourceChoiceForKey ("env2")) == AssignFlow::Phase::idle);
    CHECK_EQ (flow.armedSource(), 0);
    CHECK (flow.tapSource (lfo1) == AssignFlow::Phase::armed);
    flow.cancel();
    CHECK (flow.phase() == AssignFlow::Phase::idle);
    CHECK_EQ (m.open, 0);

    // out of range chips are ignored
    CHECK (flow.tapSource (0) == AssignFlow::Phase::idle);
    CHECK (flow.tapSource (kNumModSources + 1) == AssignFlow::Phase::idle);
}

TEST (ui_assign_flow_polarity_and_chip_while_dragging)
{
    FakeMatrix m;
    AssignFlow flow (m);
    flow.tapSource (sourceChoiceForKey ("lfo2"));

    // centred destination (pan): bipolar slot; one-sided (level): unipolar
    CHECK (flow.beginDrag (idx ("osc1.pan")) == AssignFlow::BeginResult::ok);
    CHECK_EQ (static_cast<int> (getKey (m, "mod01.bipolar")), 1);
    flow.endDrag();
    CHECK (flow.beginDrag (idx ("osc1.level")) == AssignFlow::BeginResult::ok);
    CHECK_EQ (static_cast<int> (getKey (m, "mod02.bipolar")), 0);

    // a chip tap during a drag is ignored; cancel closes the gesture
    CHECK (flow.tapSource (sourceChoiceForKey ("lfo3")) == AssignFlow::Phase::dragging);
    CHECK_EQ (flow.armedSource(), sourceChoiceForKey ("lfo2"));
    flow.cancel();
    CHECK_EQ (m.open, 0);
    CHECK (flow.phase() == AssignFlow::Phase::idle);
}

TEST (ui_assign_flow_matrix_full)
{
    FakeMatrix m;
    AssignFlow flow (m);
    flow.tapSource (sourceChoiceForKey ("lfo1"));

    // fill all 64 slots with different destinations (the modulatable keys of the table, in order)
    int filled = 0;
    for (size_t i = 0; i < paramCount() && filled < kNumModSlots; ++i)
    {
        if (modDestinationChoiceOf (i) == 0)
            continue;
        CHECK (flow.beginDrag (static_cast<int> (i)) == AssignFlow::BeginResult::ok);
        flow.endDrag();
        ++filled;
    }
    CHECK_EQ (filled, kNumModSlots);
    CHECK (ModSnapshot::build (m).full());

    // a 65th destination is refused with matrixFull; an existing one is still editable
    for (size_t i = paramCount(); i-- > 0;)
        if (modDestinationChoiceOf (i) > 0 && ModSnapshot::build (m).entriesFor (static_cast<int> (i)).empty())
        {
            CHECK (flow.beginDrag (static_cast<int> (i)) == AssignFlow::BeginResult::matrixFull);
            CHECK (flow.phase() == AssignFlow::Phase::armed);
            break;
        }
    for (size_t i = 0; i < paramCount(); ++i)
        if (modDestinationChoiceOf (i) > 0)
        {
            CHECK (flow.beginDrag (static_cast<int> (i)) == AssignFlow::BeginResult::ok);
            flow.endDrag();
            break;
        }
    CHECK_EQ (m.open, 0);
}

TEST (ui_mod_snapshot_and_slot_editing)
{
    FakeMatrix m;
    AssignFlow flow (m);
    const int cutoff = idx ("filter1.cutoff");
    const int lfo1 = sourceChoiceForKey ("lfo1");
    flow.tapSource (lfo1);
    flow.beginDrag (cutoff);
    flow.setAmount (35.0f);
    flow.endDrag();
    flow.tapSource (sourceChoiceForKey ("env1")); // another source
    flow.beginDrag (cutoff);
    flow.setAmount (-10.0f);
    flow.endDrag();

    auto snap = ModSnapshot::build (m);
    CHECK_EQ (snap.slotsUsed(), 2);
    CHECK_EQ (static_cast<int> (snap.entriesFor (cutoff).size()), 2);
    CHECK_EQ (snap.entriesFor (cutoff)[0].source, lfo1);
    CHECK_NEAR (snap.entriesFor (cutoff)[0].amount, 35.0, 1e-3);
    CHECK_EQ (snap.destinationCount (lfo1), 1);
    CHECK_EQ (static_cast<int> (snap.entriesForSource (lfo1).size()), 1);
    CHECK_EQ (snap.entriesForSource (lfo1)[0].first, cutoff);
    CHECK (snap.entriesFor (idx ("osc1.level")).empty());

    // equality (the editor timer only broadcasts on change)
    CHECK (snap == ModSnapshot::build (m));
    setSlotBypass (m, 0, true);
    CHECK (! (snap == ModSnapshot::build (m)));
    CHECK (ModSnapshot::build (m).entriesFor (cutoff)[0].bypass);

    // remove: all seven slot keys back to default, the slot is free again and is reused first
    clearSlot (m, 0);
    CHECK_EQ (ModSnapshot::build (m).slotsUsed(), 1);
    CHECK_EQ (static_cast<int> (getKey (m, "mod01.source")), 0);
    CHECK_NEAR (getKey (m, "mod01.amount"), 0.0, 1e-6);
    CHECK_EQ (static_cast<int> (getKey (m, "mod01.bypass")), 0);
    flow.tapSource (lfo1);
    flow.beginDrag (idx ("osc1.level"));
    CHECK_EQ (flow.activeSlot(), 0);
    flow.endDrag();
    CHECK_EQ (m.open, 0);

    // out of range slot numbers are ignored
    clearSlot (m, -1);
    clearSlot (m, kNumModSlots);
    setSlotBypass (m, 99, true);
}

TEST (ui_ab_compare_snapshots)
{
    AbCompare ab;
    CHECK (ab.active() == AbCompare::Slot::a);
    CHECK (! ab.otherIsStored());

    const int vol = idx ("master.volume");
    ParamValues live = ParamValues::defaults();
    live.plain[vol] = -6.0f;

    // first toggle: B starts as a copy of A, so nothing audible changes
    const ParamValues& toB = ab.toggle (live);
    CHECK (ab.active() == AbCompare::Slot::b);
    CHECK_NEAR (toB.plain[vol], -6.0, 1e-6);

    live = toB;
    live.plain[vol] = -20.0f; // edit in B
    const ParamValues& toA = ab.toggle (live);
    CHECK (ab.active() == AbCompare::Slot::a);
    CHECK_NEAR (toA.plain[vol], -6.0, 1e-6);
    CHECK_NEAR (ab.snapshot (AbCompare::Slot::b).plain[vol], -20.0, 1e-6);
    CHECK (ab.otherIsStored());

    live = toA;
    CHECK_NEAR (ab.toggle (live).plain[vol], -20.0, 1e-6);

    // copy the active (live) state over the other
    live.plain[vol] = -3.0f;
    ab.copyActiveToOther (live);
    CHECK_NEAR (ab.snapshot (AbCompare::Slot::a).plain[vol], -3.0, 1e-6);
    CHECK_NEAR (ab.snapshot (AbCompare::Slot::b).plain[vol], -3.0, 1e-6);
    CHECK_NEAR (ab.toggle (live).plain[vol], -3.0, 1e-6);
}
