// SynthyBoy - MPC UI library test: Q-Link visual build policy and compile-out verification.
// Verifies:
//   1. Gen 1 vs Gen 2 compile-time visual policy definitions.
//   2. Functional physical Q-Link routing / turn handling is 100% preserved in both Gen 1 and Gen 2.
//   3. Gen 1 active quad on 4-knob setups renders a factory-style bank frame and single unnumbered Q marker.
//   4. Gen 2 builds produce ZERO frame / marker scene ops in both 4-knob and 16-knob configurations.
//   5. Specific Q-frame drawing routines (Pen::qFocus, Pen::qMarkerRect, QGroup::draw frame path)
//      are eliminated at compile time in Gen 2 builds.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>
#include <cstring>

#include "ui_test.h"
#include "../base.h"
#include "../qgroup.h"
#include "../q_visual_policy.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

static void test_q_visual_policy_constants()
{
    static_assert (QVisualPolicy::generation == SYNTHYBOY_MPC_UI_GEN, "Generation macro mismatch");
    static_assert (QVisualPolicy::kHasQFrame == (SYNTHYBOY_MPC_QFRAME_ENABLED != 0), "Frame enabled macro mismatch");
    static_assert (QGroup::hasQFrame() == QVisualPolicy::kHasQFrame, "QGroup::hasQFrame mismatch");
    static_assert (QGroup::uiGeneration() == QVisualPolicy::generation, "QGroup::uiGeneration mismatch");

    check (QVisualPolicy::generation == 1 || QVisualPolicy::generation == 2,
           "QVisualPolicy::generation is either 1 or 2");

    if (QVisualPolicy::generation == 1)
    {
        check (QVisualPolicy::kHasQFrame, "Gen 1 policy enables Q bank frame");
        check (SYNTHYBOY_MPC_QFRAME_ENABLED == 1, "SYNTHYBOY_MPC_QFRAME_ENABLED is 1 in Gen 1");
    }
    else
    {
        check (! QVisualPolicy::kHasQFrame, "Gen 2 policy disables Q bank frame");
        check (SYNTHYBOY_MPC_QFRAME_ENABLED == 0, "SYNTHYBOY_MPC_QFRAME_ENABLED is 0 in Gen 2");
    }
}

static void test_physical_qslot_routing_and_turn()
{
    Knob k0 ("K0"), k1 ("K1"), k2 ("K2"), k3 ("K3");
    k0.setId (801); k1.setId (802); k2.setId (803); k3.setId (804);
    k0.place ({ 100, 100, 80, 56 });
    k1.place ({ 190, 100, 80, 56 });
    k2.place ({ 100, 170, 80, 56 });
    k3.place ({ 190, 170, 80, 56 });

    QGroup qg;
    qg.set (0, &k0);
    qg.set (1, &k1);
    qg.set (2, &k2);
    qg.set (3, &k3);

    check (qg.numMembers() == 4, "Physical group has 4 members assigned");

    // Inactive: turn routes nothing
    Change c = qg.turn (0, 0.1);
    check (c.kind == ChangeKind::None, "Inactive group turn returns None");

    // Activate group
    qg.setActive (true);
    check (qg.active(), "Group is active");

    // Hardware rotary knob delta routing must work regardless of Gen1 / Gen2 build policy
    c = qg.turn (0, 0.15);
    check (c.kind == ChangeKind::Value && c.id == 801, "Knob 0 delta routes to slot 0 (ID 801)");

    c = qg.turn (1, 0.2);
    check (c.kind == ChangeKind::Value && c.id == 802, "Knob 1 delta routes to slot 1 (ID 802)");

    c = qg.turn (2, 0.3);
    check (c.kind == ChangeKind::Value && c.id == 803, "Knob 2 delta routes to slot 2 (ID 803)");

    c = qg.turn (3, 0.4);
    check (c.kind == ChangeKind::Value && c.id == 804, "Knob 3 delta routes to slot 3 (ID 804)");

    // Invalid slot index returns None
    c = qg.turn (4, 0.5);
    check (c.kind == ChangeKind::None, "Invalid slot 4 turn returns None");

    c = qg.turn (-1, 0.5);
    check (c.kind == ChangeKind::None, "Invalid slot -1 turn returns None");

    // Emptying a slot clears routing
    qg.set (1, nullptr);
    c = qg.turn (1, 0.1);
    check (c.kind == ChangeKind::None, "Turn on cleared slot 1 returns None");

    qg.setActive (false);
    check (! qg.active(), "Group is deactivated");
    c = qg.turn (0, 0.1);
    check (c.kind == ChangeKind::None, "Turn after deactivation returns None");
}

static void test_gen1_frame_drawing_behavior()
{
    if constexpr (! QVisualPolicy::kHasQFrame)
        return; // Only executed in Gen 1 builds

    Knob k0 ("K0"), k1 ("K1"), k2 ("K2"), k3 ("K3");
    k0.setId (901); k1.setId (902); k2.setId (903); k3.setId (904);
    k0.place ({ 100, 100, 80, 56 });
    k1.place ({ 190, 100, 80, 56 });
    k2.place ({ 100, 170, 80, 56 });
    k3.place ({ 190, 170, 80, 56 });

    QGroup qg;
    qg.set (0, &k0);
    qg.set (1, &k1);
    qg.set (2, &k2);
    qg.set (3, &k3);

    // 1. 4-knob mode (Key 61 standard)
    QGroup::setWideDevice (false);
    qg.setActive (true);
    check (k0.qFrame() && k0.qIndex() == 1, "Gen 1: Active 4-knob member gets Q frame flag");

    {
        Scene s (400, 300);
        qg.draw (s.builder());
        check (s.numOps() >= 3, "Gen 1: Active 4-knob group draws frame ops");

        int frameBoxes = 0, qMarkers = 0, numberedDigits = 0;
        for (uint32_t i = 0; i < s.data->numOps; ++i)
        {
            const auto& op = s.data->ops[i];
            if (op.op == gpu::kOpRRect && op.rgba2 != 0)
                ++frameBoxes;
            if (op.op == gpu::kOpText)
            {
                std::string txt = gpu::textString (*s.data, op);
                if (txt == "Q")
                    ++qMarkers;
                else
                    ++numberedDigits;
            }
        }
        check (frameBoxes == 1, "Gen 1: Exactly one bank outline box drawn");
        check (qMarkers == 1, "Gen 1: Exactly one unnumbered Q marker drawn");
        check (numberedDigits == 0, "Gen 1: Zero numbered digits / badges drawn");
    }

    // 2. 16-knob runtime mode in Gen 1 (MPC X runtime mode)
    qg.setActive (false);
    QGroup::setWideDevice (true);
    qg.setActive (true);
    check (! k0.qFrame(), "Gen 1 16-knob: Member Q frame flag suppressed");
    {
        Scene s (400, 300);
        qg.draw (s.builder());
        check (s.numOps() == 0, "Gen 1 16-knob: Bank frame suppressed because all 16 knobs mapped");
    }

    qg.setActive (false);
    QGroup::setWideDevice (false);
}

static void test_gen2_frame_drawing_behavior()
{
    if constexpr (QVisualPolicy::kHasQFrame)
        return; // Only executed in Gen 2 builds

    Knob k0 ("K0"), k1 ("K1"), k2 ("K2"), k3 ("K3");
    k0.setId (951); k1.setId (952); k2.setId (953); k3.setId (954);
    k0.place ({ 100, 100, 80, 56 });
    k1.place ({ 190, 100, 80, 56 });
    k2.place ({ 100, 170, 80, 56 });
    k3.place ({ 190, 170, 80, 56 });

    QGroup qg;
    qg.set (0, &k0);
    qg.set (1, &k1);
    qg.set (2, &k2);
    qg.set (3, &k3);

    // 1. Gen 2 in 4-knob mode: ZERO frame ops, NO Q marker, NO member qFrame
    QGroup::setWideDevice (false);
    qg.setActive (true);
    check (! k0.qFrame(), "Gen 2 4-knob: Member qFrame flag is false");
    check (k0.qIndex() == 0, "Gen 2 4-knob: Member qIndex is 0");

    {
        Scene s (400, 300);
        qg.draw (s.builder());
        check (s.numOps() == 0, "Gen 2 4-knob: Active group produces 0 scene ops (no frame, no Q marker)");
    }

    // Direct Pen::qFocus test in Gen 2
    {
        Scene s (400, 300);
        Pen p (s.builder(), true, false);
        p.qFocus ({ 100, 100, 200, 100 });
        check (s.numOps() == 0, "Gen 2: Pen::qFocus produces 0 scene ops (compiled out)");
    }

    // Pen::qMarkerRect test in Gen 2
    {
        Rect r = Pen::qMarkerRect ({ 100, 100, 200, 100 });
        check (r.empty(), "Gen 2: Pen::qMarkerRect returns empty rect");
    }

    // 2. Gen 2 in 16-knob mode (MPC XL): ZERO frame ops
    QGroup::setWideDevice (true);
    check (! k0.qFrame(), "Gen 2 16-knob: Member qFrame flag is false");
    {
        Scene s (400, 300);
        qg.draw (s.builder());
        check (s.numOps() == 0, "Gen 2 16-knob: Active group produces 0 scene ops");
    }

    // Functional knob routing must still work in both 4-knob and 16-knob modes in Gen 2
    Change c = qg.turn (0, 0.1);
    check (c.kind == ChangeKind::Value && c.id == 951, "Gen 2: Knob 0 routing functional");
    c = qg.turn (2, 0.2);
    check (c.kind == ChangeKind::Value && c.id == 953, "Gen 2: Knob 2 routing functional");

    qg.setActive (false);
    QGroup::setWideDevice (false);
}

static void test_gen2_compile_elimination_proof()
{
#if !SYNTHYBOY_MPC_QFRAME_ENABLED
    // In Gen 2 builds, prove that the Q-frame drawing code path is a complete compile-time no-op.
    static_assert (! QVisualPolicy::kHasQFrame, "QVisualPolicy::kHasQFrame must be false");
    static_assert (SYNTHYBOY_MPC_UI_GEN == 2, "SYNTHYBOY_MPC_UI_GEN must be 2");
    static_assert (Pen::qMarkerRect ({ 50, 50, 100, 100 }).empty(), "Gen 2 marker rect must be compile-time empty");
    static_assert (Pen::qMarkerRect ({ 50, 50, 100, 100 }).w == 0, "Gen 2 marker rect width must be 0");
    static_assert (Pen::qMarkerRect ({ 50, 50, 100, 100 }).h == 0, "Gen 2 marker rect height must be 0");

    // Verify Pen::qMarkerRect is a compile-time empty rect
    Rect r = Pen::qMarkerRect ({ 50, 50, 100, 100 });
    check (r.w == 0 && r.h == 0 && r.x == 0 && r.y == 0, "Gen 2 compile-out: qMarkerRect is zeroed");

    // Verify SceneBuilder receives 0 ops when Pen::qFocus is called with any bounds
    Scene s (800, 600);
    Pen pen (s.builder(), true, false);
    pen.qFocus ({ 10, 10, 300, 200 });
    check (s.numOps() == 0, "Gen 2 compile-out: Pen::qFocus emits 0 ops to SceneBuilder");

    // Verify QGroup::draw with various parameters emits 0 ops
    Knob k ("K");
    k.place ({ 10, 10, 80, 56 });
    QGroup qg;
    qg.set (0, &k);
    qg.setActive (true);
    Rect cardBounds { 5, 5, 100, 70 };
    qg.draw (s.builder(), &cardBounds);
    check (s.numOps() == 0, "Gen 2 compile-out: QGroup::draw emits 0 ops with card bounds");
#else
    // Gen 1 build verification
    static_assert (QVisualPolicy::kHasQFrame, "QVisualPolicy::kHasQFrame must be true in Gen 1");
    static_assert (SYNTHYBOY_MPC_UI_GEN == 1, "SYNTHYBOY_MPC_UI_GEN must be 1 in Gen 1");
    static_assert (! Pen::qMarkerRect ({ 50, 50, 100, 100 }).empty(), "Gen 1 marker rect must not be empty");
    check (QVisualPolicy::kHasQFrame, "Gen 1 active verification");
#endif
}

int main()
{
    test_q_visual_policy_constants();
    test_physical_qslot_routing_and_turn();
    test_gen1_frame_drawing_behavior();
    test_gen2_frame_drawing_behavior();
    test_gen2_compile_elimination_proof();

    std::printf ("q_visual_policy_host [GEN=%d]: %d failure(s)\n",
                 QVisualPolicy::generation, g_fails);
    return g_fails ? 1 : 0;
}
