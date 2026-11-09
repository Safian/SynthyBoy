// SynthyBoy - UI tests: the pure view-model (key resolution, cap dimming, gestures, layout modes, assign flow, A/B). No JUCE, no display.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <array>
#include <cmath>
#include <cstring>
#include <string>

#include "GestureModel.h"
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
    // GROUPB WP-18: "Saw Down" needs its own bit, not the user table bit
    CHECK (! capStateForValue (wt, static_cast<float> (kWavetableSawDown), kCapNone).available);
    CHECK (! capStateForValue (wt, static_cast<float> (kWavetableSawDown), kCapUserWavetable).available);
    CHECK (capStateForValue (wt, static_cast<float> (kWavetableSawDown), kCapWavetableSawDown).available);
    CHECK_EQ (std::string (choiceName (paramTable()[wt], static_cast<size_t> (kWavetableSawDown))), std::string ("Saw Down"));

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

// ---- gap closure: gesture maths (GestureModel.h) --------------------------------------------------------------------------------------

TEST (uim_viewport_zoom_pan_fit)
{
    Viewport v;
    CHECK (v.isFit());
    v.zoomAbout (0.5, 4.0);
    CHECK_NEAR (v.span, 0.25, 1e-9);
    CHECK_NEAR (v.toRange (0.5), 0.5, 1e-9); // the anchor stays under the finger
    v.pan (10.0);                            // clamped at the end
    CHECK_NEAR (v.start + v.span, 1.0, 1e-9);
    v.pan (-10.0);
    CHECK_NEAR (v.start, 0.0, 1e-9);
    v.zoomAbout (0.9, 1e9); // clamped at the finest span
    CHECK_NEAR (v.span, Viewport::kMinSpan, 1e-12);
    v.zoomAbout (0.9, 1e-9); // and back out to fit
    CHECK (v.isFit());
    CHECK_NEAR (v.start, 0.0, 1e-12);
    v.zoomAbout (0.2, 2.0);
    v.fit();
    CHECK (v.isFit() && v.start == 0.0);
    CHECK_NEAR (v.toScreen (0.25), 0.25, 1e-12);
}

TEST (uim_slice_list_edits)
{
    std::vector<float> s { 0.0f, 0.5f };
    CHECK (addSlice (s, 0.25f));
    CHECK (s.size() == 3 && s[1] == 0.25f);
    CHECK (! addSlice (s, 0.2501f)); // closer than the minimum gap
    CHECK (! addSlice (s, 1.5f));
    CHECK (nearestSlice (s, 0.26f, 0.05f) == 1);
    CHECK (nearestSlice (s, 0.7f, 0.05f) == -1);
    CHECK (moveSlice (s, 1, 0.9f)); // clamped below the next slice
    CHECK (s[1] < s[2] && s[1] > s[0]);
    CHECK (moveSlice (s, 1, 0.1f));
    CHECK_NEAR (s[1], 0.1, 1e-6);
    CHECK (removeSlice (s, 1) && s.size() == 2);
    CHECK (! removeSlice (s, 5));
    std::vector<float> many;
    for (int i = 0; i < kMaxSlices; ++i)
        CHECK (addSlice (many, static_cast<float> (i) / 129.0f));
    CHECK (! addSlice (many, 0.999f)); // 128 at most
    // the threshold line: higher threshold = lower line (more onsets pass)
    CHECK_NEAR (thresholdToLine (0.0f), 1.0, 1e-9);
    CHECK_NEAR (thresholdToLine (100.0f), 0.0, 1e-9);
    CHECK_NEAR (lineToThreshold (thresholdToLine (37.0f)), 37.0, 1e-4);
}

TEST (uim_loop_pair_and_transpose_split)
{
    float a = 0.2f, b = 0.5f;
    moveLoopPair (a, b, 0.1f);
    CHECK_NEAR (a, 0.3, 1e-6);
    CHECK_NEAR (b, 0.6, 1e-6);
    moveLoopPair (a, b, 5.0f); // stops at the end, keeps the length
    CHECK_NEAR (b, 1.0, 1e-6);
    CHECK_NEAR (b - a, 0.3, 1e-6);
    moveLoopPair (a, b, -5.0f);
    CHECK_NEAR (a, 0.0, 1e-6);
    CHECK (betweenLoopMarkers (0.5f, 0.2f, 0.8f, 0.05f));
    CHECK (! betweenLoopMarkers (0.21f, 0.2f, 0.8f, 0.05f));
    CHECK (! betweenLoopMarkers (0.9f, 0.2f, 0.8f, 0.05f));

    for (int t = -48; t <= 48; ++t)
    {
        const auto sp = splitTranspose (t);
        CHECK_EQ (joinTranspose (sp.oct, sp.sem), t);
        CHECK (sp.sem >= -6 && sp.sem <= 5);
    }
    CHECK_EQ (splitTranspose (12).oct, 1);
    CHECK_EQ (splitTranspose (-7).oct, -1);
    CHECK_EQ (joinTranspose (9, 0), 48);
    CHECK_EQ (joinTranspose (-9, 0), -48);
}

TEST (uim_warp_groups_cover_every_choice_once)
{
    const auto groups = warpTypeGroups();
    const auto ref = resolveKey ("osc1.warp.type");
    CHECK (ref.valid());
    std::array<int, 96> seen {};
    for (const auto& g : groups)
    {
        CHECK (! g.gap || g.items.empty());
        for (const int i : g.items)
            ++seen[static_cast<size_t> (i)];
    }
    for (size_t i = 0; i < choiceCount (*ref.info); ++i)
        CHECK_EQ (seen[i], 1);
    CHECK_EQ (std::string (choiceName (*ref.info, 4)), std::string ("Bend"));
    bool bendInAlt = false;
    for (const auto& g : groups)
    {
        CHECK (! g.gap);
        if (g.name == "Alt")
            bendInAlt = g.items.size() == 14 && g.items[0] == 4;
    }
    CHECK (bendInAlt);
    CHECK_EQ (std::string (choiceName (*ref.info, 11)), std::string ("Sync Var"));
    CHECK_EQ (choiceCount (*ref.info), static_cast<size_t> (kNumWarpTypes));
}

TEST (uim_touch_tracker_tap_swipe_pinch_pan)
{
    TouchTracker t;
    using P = TouchTracker::Phase;
    using K = TouchTracker::Kind;
    // two-finger tap
    CHECK (t.feed (P::down, 1, { 100, 100 }, 1000).kind == K::none);
    CHECK (t.feed (P::down, 2, { 160, 100 }, 1040).kind == K::none);
    CHECK (t.feed (P::up, 1, { 101, 100 }, 1150).kind == K::twoFingerTap);
    t.feed (P::up, 2, { 160, 100 }, 1160);
    CHECK_EQ (t.activeTouches(), 0);
    // a slow press is no tap
    t.feed (P::down, 1, { 100, 100 }, 2000);
    t.feed (P::down, 2, { 160, 100 }, 2000);
    CHECK (t.feed (P::up, 1, { 100, 100 }, 2800).kind == K::none);
    t.feed (P::up, 2, { 160, 100 }, 2810);
    // swipe: both fingers 100 px to the right
    t.feed (P::down, 1, { 100, 100 }, 3000);
    t.feed (P::down, 2, { 160, 100 }, 3000);
    t.feed (P::drag, 1, { 150, 100 }, 3100);
    t.feed (P::drag, 2, { 210, 100 }, 3100);
    t.feed (P::drag, 1, { 200, 102 }, 3200);
    t.feed (P::drag, 2, { 260, 102 }, 3200);
    const auto swipe = t.feed (P::up, 1, { 200, 102 }, 3300);
    CHECK (swipe.kind == K::twoFingerSwipe);
    CHECK (swipe.dx > 90.0f);
    t.feed (P::up, 2, { 260, 102 }, 3310);
    // pinch out
    t.feed (P::down, 1, { 100, 100 }, 4000);
    t.feed (P::down, 2, { 140, 100 }, 4000);
    t.feed (P::drag, 2, { 170, 100 }, 4040);
    const auto pinch = t.feed (P::drag, 2, { 200, 100 }, 4050);
    CHECK (pinch.kind == K::pinch);
    CHECK (pinch.scale > 1.2f);
    CHECK (t.feed (P::up, 1, { 100, 100 }, 4100).kind == K::none); // a pinch is no swipe / tap
    t.feed (P::up, 2, { 200, 100 }, 4110);
    // pan: both fingers down 40 px
    t.feed (P::down, 1, { 100, 100 }, 5000);
    t.feed (P::down, 2, { 160, 100 }, 5000);
    t.feed (P::drag, 1, { 100, 120 }, 5050);
    const auto pan = t.feed (P::drag, 2, { 160, 120 }, 5050);
    CHECK (pan.kind == K::pan);
    CHECK (pan.dy > 0.0f);
    // a third finger is ignored
    CHECK (t.feed (P::down, 3, { 10, 10 }, 5060).kind == K::none);
    CHECK_EQ (t.activeTouches(), 2);
}

TEST (uim_parsers)
{
    float f = 0.0f;
    CHECK (parseBinAmplitude ("37", f) && std::fabs (f - 0.37f) < 1e-6f);
    CHECK (parseBinAmplitude (" 100 % ", f) && f == 1.0f);
    CHECK (! parseBinAmplitude ("101", f));
    CHECK (! parseBinAmplitude ("-3", f));
    CHECK (! parseBinAmplitude ("", f));
    CHECK (! parseBinAmplitude ("abc", f));
    CHECK (parseBinPhaseDegrees ("-90", f) && std::fabs (f - 270.0f) < 1e-4f);
    CHECK (parseBinPhaseDegrees ("450d", f) && std::fabs (f - 90.0f) < 1e-4f);
    CHECK (! parseBinPhaseDegrees ("x", f));
    double d = 0.0;
    CHECK (parseNumber ("12.5", d) && d == 12.5);
    CHECK (! parseNumber ("inf", d));
}

TEST (uim_warp_picture_and_cpu_level)
{
    const auto saw = [] (float ph) { return 1.0f - 2.0f * ph; };
    for (int phaseStep = 0; phaseStep < 16; ++phaseStep)
    {
        const float ph = static_cast<float> (phaseStep) / 16.0f;
        CHECK_NEAR (warpedValue (0, 0.8f, ph, saw), saw (ph), 1e-6);  // None
        CHECK_NEAR (warpedValue (3, 0.0f, ph, saw), saw (ph), 1e-6);  // amount 0
    }
    // every mode with an amount really changes the picture (11 choices of oscN.warp.type, 10 modes)
    for (int type = 1; type <= 10; ++type)
    {
        float maxDiff = 0.0f;
        for (int i = 0; i < 128; ++i)
        {
            const float ph = static_cast<float> (i) / 128.0f;
            const float w = warpedValue (type, 0.7f, ph, saw);
            CHECK (std::isfinite (w));
            CHECK (std::fabs (w) <= 1.0001f);
            maxDiff = std::max (maxDiff, std::fabs (w - saw (ph)));
        }
        CHECK (maxDiff > 0.02f);
    }
    // the modes of block B21 (11 .. 51) too; Remap 1, 2 and 4 follow the drawn curve (here x squared)
    const auto drawn = [] (float x) { return x * x; };
    for (int type = 11; type <= 51; ++type)
    {
        float maxDiff = 0.0f;
        for (int i = 0; i < 128; ++i)
        {
            const float ph = static_cast<float> (i) / 128.0f;
            const float w = warpedValue (type, 0.7f, ph, saw, drawn);
            CHECK (std::isfinite (w));
            CHECK (std::fabs (w) <= 2.0001f);
            maxDiff = std::max (maxDiff, std::fabs (w - saw (ph)));
        }
        if (! CHECK (maxDiff > 0.02f))
            std::printf ("    warp picture %d\n", type);
    }
    CHECK_NEAR (warpedValue (14, 0.5f, 0.3f, saw), saw (0.3f), 1e-5);  // Bend +/- is neutral in the middle
    CHECK_NEAR (warpedValue (20, 1.0f, 0.4f, saw), saw (0.4f), 1e-5);  // without a drawn curve Remap 1 is the diagonal
    // a few exact ones: Sync doubles the speed at amount 1/3, Quantize holds the value inside a step, Formant is silent after the squeezed cycle
    CHECK_NEAR (warpedValue (1, 1.0f / 3.0f, 0.25f, saw), saw (0.5f), 1e-5);
    CHECK_NEAR (warpedValue (3, 1.0f, 0.5f, saw), warpedValue (3, 1.0f, 0.5f + 0.01f, saw), 1e-5);
    CHECK_NEAR (warpedValue (2, 1.0f, 0.9f, saw), 0.0, 1e-6);
    CHECK_NEAR (warpedValue (9, 1.0f, 0.125f, [] (float) { return 1.0f; }), 1.0, 1e-5); // RM: sin (2 pi 2 / 8) = 1

    CHECK_EQ (cpuLevel (0.0f), 0);
    CHECK_EQ (cpuLevel (0.499f), 0);
    CHECK_EQ (cpuLevel (0.5f), 1);
    CHECK_EQ (cpuLevel (0.8f), 2);
    CHECK_EQ (cpuLevel (3.0f), 2);
}

// ---- point modulation of a custom LFO shape (row 05-22) ----

TEST (uim_point_mod_set_remove_and_limits)
{
    ContentPoint p (0.4f, 0.5f);
    const int velocity = pointModSourceChoice ("velocity");
    CHECK (velocity > 0);
    CHECK_EQ (std::string (pointModSourceKey (velocity)), std::string ("velocity"));
    CHECK_EQ (pointModSourceChoice ("nope"), 0);
    CHECK (std::string (pointModSourceKey (0)).empty() && std::string (pointModSourceKey (kNumModSources + 1)).empty());

    // X needs an inner point; Y works everywhere; a bad source or index is refused and writes nothing
    CHECK (canModulatePointAxis (1, 3, PointAxis::x) && canModulatePointAxis (1, 3, PointAxis::y));
    CHECK (! canModulatePointAxis (0, 3, PointAxis::x) && canModulatePointAxis (0, 3, PointAxis::y));
    CHECK (! canModulatePointAxis (2, 3, PointAxis::x) && ! canModulatePointAxis (3, 3, PointAxis::y));
    CHECK (! setPointMod (p, 0, 3, PointAxis::x, velocity, 0.5f));
    CHECK (! setPointMod (p, 1, 3, PointAxis::y, 0, 0.5f));
    CHECK (! p.modX.present && ! p.modY.present);

    CHECK (setPointMod (p, 1, 3, PointAxis::y, velocity, 7.0f)); // clamped to the data model range
    CHECK (p.modY.present && p.modY.source == "velocity");
    CHECK_NEAR (p.modY.amount, 1.0, 1e-6);
    CHECK (setPointMod (p, 1, 3, PointAxis::y, pointModSourceChoice ("env2"), -0.25f)); // one source per axis: replaced
    CHECK (p.modY.source == "env2");
    CHECK_NEAR (p.modY.amount, -0.25, 1e-6);
    CHECK (pointModActive (p, PointAxis::y) && ! pointModActive (p, PointAxis::x));
    CHECK (setPointMod (p, 1, 3, PointAxis::y, pointModSourceChoice ("env2"), 0.0f));
    CHECK (p.modY.present && ! pointModActive (p, PointAxis::y)); // depth 0 = bypassed, still stored

    CHECK (removePointMod (p, PointAxis::y));
    CHECK (! p.modY.present && p.modY.source.empty());
    CHECK (! removePointMod (p, PointAxis::y));

    // every source key of the table is a valid mod_x / mod_y of the data model
    for (int i = 1; i <= kNumModSources; ++i)
    {
        StateContent content;
        ContentLfoShape s;
        s.lfo = 1;
        s.points = { ContentPoint (0.0f, 0.0f), ContentPoint (0.5f, 0.5f), ContentPoint (1.0f, 1.0f) };
        CHECK (setPointMod (s.points[1], 1, 3, PointAxis::x, i, 0.5f));
        CHECK (setPointMod (s.points[1], 1, 3, PointAxis::y, i, -0.5f));
        content.lfoShapes.push_back (s);
        CHECK (validateContent (content));
    }
}

TEST (uim_point_mod_drag_amount_and_axis)
{
    // the bar follows the finger (the whole plot = 1.0); the larger movement picks the axis; X only where it is allowed
    CHECK_NEAR (pointModDragAmount (0.0f, 100.0f, 100.0f), 1.0, 1e-6);
    CHECK_NEAR (pointModDragAmount (0.0f, 50.0f, 100.0f), 0.5, 1e-6);
    CHECK_NEAR (pointModDragAmount (0.5f, -100.0f, 100.0f), -0.5, 1e-6);
    CHECK_NEAR (pointModDragAmount (0.9f, 500.0f, 100.0f), 1.0, 1e-6);
    CHECK (pointModDragAxis (30.0f, 5.0f, true) == PointAxis::x);
    CHECK (pointModDragAxis (5.0f, -30.0f, true) == PointAxis::y);
    CHECK (pointModDragAxis (30.0f, 5.0f, false) == PointAxis::y);
    CHECK (pointModDragAxis (10.0f, 10.0f, true) == PointAxis::y);
}
