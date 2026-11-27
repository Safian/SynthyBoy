// SynthyBoy - MPC UI library test coverage: Q groups and chips
// Tests untested public behaviors of QGroup: 4-slot routing, wide device
// MPC XL mode chips 5..16, idle chips, mapped blocks, frameless mode,
// and adjacency checks.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui_test.h"
#include "../base.h"
#include "../qgroup.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

static void test_qgroup_slots_and_turn_routing()
{
    Knob k0 ("K0"), k1 ("K1"), k2 ("K2"), k3 ("K3");
    k0.setId (501); k1.setId (502); k2.setId (503); k3.setId (504);
    k0.place ({ 100, 100, 80, 56 });
    k1.place ({ 190, 100, 80, 56 });
    k2.place ({ 100, 170, 80, 56 });
    k3.place ({ 190, 170, 80, 56 });

    QGroup qg;
    check (qg.numMembers() == 0, "Initial QGroup has 0 members");
    check (! qg.active(), "Initial QGroup is inactive");

    qg.set (0, &k0);
    qg.set (1, &k1);
    qg.set (2, &k2);
    qg.set (3, &k3);
    check (qg.numMembers() == 4, "QGroup has 4 members after set");
    check (qg.member (0) == &k0, "Slot 0 is k0");
    check (qg.member (1) == &k1, "Slot 1 is k1");
    check (qg.member (2) == &k2, "Slot 2 is k2");
    check (qg.member (3) == &k3, "Slot 3 is k3");

    // Out of bounds set/member
    check (! qg.set (-1, &k0), "Set slot -1 returns false");
    check (! qg.set (4, &k0), "Set slot 4 returns false");
    check (qg.member (-1) == nullptr, "Member -1 is nullptr");
    check (qg.member (4) == nullptr, "Member 4 is nullptr");

    // Inactive: members have no Q badge
    check (! k0.qFrame() && k0.qIndex() == 0, "Inactive group members have no Q badge");

    // Inactive: turn does nothing
    Change c = qg.turn (0, 0.1);
    check (c.kind == ChangeKind::None, "Inactive QGroup turn returns None");

    // Activate
    qg.setActive (true);
    check (qg.active(), "QGroup is active");
    if constexpr (QVisualPolicy::kHasQFrame)
    {
        check (k0.qFrame() && k0.qIndex() == 1, "Slot 0 member gets Q index 1");
        check (k1.qFrame() && k1.qIndex() == 2, "Slot 1 member gets Q index 2");
        check (k2.qFrame() && k2.qIndex() == 3, "Slot 2 member gets Q index 3");
        check (k3.qFrame() && k3.qIndex() == 4, "Slot 3 member gets Q index 4");
    }
    else
    {
        check (! k0.qFrame() && k0.qIndex() == 0, "Gen 2: Slot 0 member has no Q frame or index");
        check (! k1.qFrame() && k1.qIndex() == 0, "Gen 2: Slot 1 member has no Q frame or index");
        check (! k2.qFrame() && k2.qIndex() == 0, "Gen 2: Slot 2 member has no Q frame or index");
        check (! k3.qFrame() && k3.qIndex() == 0, "Gen 2: Slot 3 member has no Q frame or index");
    }

    // Active: turn routes to corresponding knob
    c = qg.turn (0, 0.1);
    check (c.kind == ChangeKind::Value && c.id == 501, "QGroup turn 0 routes to slot 0 (k0)");
    c = qg.turn (2, 0.2);
    check (c.kind == ChangeKind::Value && c.id == 503, "QGroup turn 2 routes to slot 2 (k2)");

    // Turn invalid knob index does nothing
    c = qg.turn (4, 0.1);
    check (c.kind == ChangeKind::None, "QGroup turn 4 (out of bounds) returns None");

    // Empty a slot while active
    qg.set (2, nullptr);
    check (qg.member (2) == nullptr, "Slot 2 is now empty");
    check (qg.numMembers() == 3, "QGroup now has 3 members");
    check (! k2.qFrame() && k2.qIndex() == 0, "Cleared member loses Q frame and index");
    c = qg.turn (2, 0.1);
    check (c.kind == ChangeKind::None, "Turn on empty slot returns None");

    // Deactivate
    qg.setActive (false);
    check (! qg.active(), "QGroup deactivated");
    check (! k0.qFrame() && k0.qIndex() == 0, "Deactivated members lose Q frame and index");
}

static void test_qgroup_rect_and_frame_rect()
{
    Knob k0 ("K0"), k1 ("K1");
    k0.place ({ 100, 100, 80, 56 });
    k1.place ({ 200, 100, 80, 56 });

    QGroup qg;
    qg.set (0, &k0);
    qg.set (1, &k1);

    Rect u = qg.rect();
    check (u.x == 100 && u.y == 100 && u.w == 180 && u.h == 56, "QGroup rect is union of members");

    // Custom frameRect overrides union rect
    Rect custom { 90, 90, 200, 76 };
    qg.setFrameRect (custom);
    check (qg.frameRect().x == 90 && qg.frameRect().w == 200, "QGroup frameRect getter");

    // Reset frameRect
    qg.setFrameRect ({});
    check (qg.frameRect().empty(), "QGroup frameRect reset to empty");
}

static void test_qgroup_wide_device_and_idle_chips()
{
    Knob k0 ("K0"), k1 ("K1");
    k0.place ({ 100, 100, 80, 56 });
    k1.place ({ 200, 100, 80, 56 });

    QGroup qg;
    qg.set (0, &k0);
    qg.set (1, &k1);

    // Default: wide device is false
    QGroup::setWideDevice (false);
    check (! QGroup::wideDevice(), "Wide device flag is false");

    qg.setBlock (1);
    check (qg.block() == 1, "QGroup block is 1");

    // Owner visual rule: idle groups draw no chips/frames
    {
        Scene s (400, 300);
        qg.drawIdle (s.builder());
        check (s.numOps() == 0, "Standard device idle groups draw no stray numbered chips");
    }

    // Wide device mode (MPC XL)
    QGroup::setWideDevice (true);
    check (QGroup::wideDevice(), "Wide device flag is true");

    // In wide mode: drawIdle and drawWide produce no chips per owner visual rule
    qg.setBlock (-1);
    {
        Scene s (400, 300);
        qg.drawIdle (s.builder());
        check (s.numOps() == 0, "Wide device with block -1 produces no idle chips");
    }

    qg.setBlock (2);
    {
        Scene s (400, 300);
        qg.drawIdle (s.builder());
        check (s.numOps() == 0, "Wide device normal mode suppresses overlay yellow badges");
    }
    {
        const ControlModel* row[] { &k0, &k1 };
        Scene s (400, 300);
        QGroup::drawMappedMembers (s.builder(), 1, row, 2);
        check (s.numOps() == 0, "XL list and panel rows hide idle slot badges outside diagnostics");
    }
    {
        Scene s (400, 300);
        qg.drawMapped (s.builder());
        check (s.numOps() == 0, "drawMapped with block >= 1 generates no numbered chips");
    }
    qg.setBlock (0);
    {
        Scene s (400, 300);
        qg.drawMapped (s.builder());
        check (s.numOps() == 0, "wide device: drawMapped with block 0 generates no numbered chips");
    }

    // Static drawMappedMembers helper
    const ControlModel* members[2] = { &k0, &k1 };
    {
        Scene s (400, 300);
        QGroup::drawMappedMembers (s.builder(), 1, members, 2);
        check (s.numOps() == 0, "drawMappedMembers generates no numbered chips");
    }
    {
        Scene s (400, 300);
        QGroup::drawMappedMembers (s.builder(), 0, members, 2);
        check (s.numOps() == 0, "wide device: drawMappedMembers with block 0 generates no numbered chips");
    }
    // Wide device: active group emits no frame (all 16 knobs always mapped)
    {
        qg.setBlock (0);
        qg.setActive (true);
        Scene w (400, 300);
        qg.draw (w.builder());
        check (w.numOps() == 0, "wide device: all knobs mapped, no active group frame drawn");
        qg.setActive (false);
        QGroup::setWideDevice (false);
    }
    // 4 knob device: active group draws frame and single unnumbered Q marker in Gen 1; 0 ops in Gen 2
    {
        qg.setBlock (0);
        qg.setActive (true);
        Scene n (400, 300);
        qg.draw (n.builder());
        if constexpr (QVisualPolicy::kHasQFrame)
            check (n.numOps() > 0 && k0.qFrame(), "4 knob device: the active group has its frame and unnumbered Q marker");
        else
            check (n.numOps() == 0 && ! k0.qFrame(), "Gen 2 4 knob device: active group produces zero frame/marker ops");
        qg.setActive (false);
    }

    QGroup::setWideDevice (false);
}

static void test_qgroup_single_unnumbered_q_marker()
{
    Knob k0 ("K0"), k1 ("K1"), k2 ("K2"), k3 ("K3");
    k0.place ({ 100, 100, 80, 56 });
    k1.place ({ 200, 100, 80, 56 });
    k2.place ({ 100, 170, 80, 56 });
    k3.place ({ 200, 170, 80, 56 });

    QGroup qg;
    qg.set (0, &k0);
    qg.set (1, &k1);
    qg.set (2, &k2);
    qg.set (3, &k3);

    QGroup::setWideDevice (false);

    // Inactive group: draws nothing (0 ops)
    {
        Scene s (400, 300);
        qg.draw (s.builder());
        check (s.numOps() == 0, "Inactive group draw produces 0 ops");
        qg.drawIdle (s.builder());
        check (s.numOps() == 0, "Inactive group drawIdle produces 0 ops");
    }

    // Active group: draws frame and single unnumbered Q marker inside one Cell in Gen 1; 0 ops in Gen 2
    qg.setActive (true);
    {
        Scene s (400, 300);
        qg.draw (s.builder());
        if constexpr (QVisualPolicy::kHasQFrame)
        {
            check (s.numOps() >= 3, "Active group draw produces cell, frame outline, and Q marker ops");
            check (s.data->ops[0].op == gpu::kOpCellBegin, "QGroup frame begins a cached Cell");
            check (s.data->ops[1].op == gpu::kOpRRect, "QGroup draws outer frame outline");
            int frames = 0, markers = 0, text = 0;
            for (uint32_t i = 0; i < s.data->numOps; ++i)
            {
                const auto& op = s.data->ops[i];
                if (op.op == gpu::kOpRRect && op.rgba2 != 0)
                    ++frames;
                if (op.op == gpu::kOpText)
                {
                    ++text;
                    markers += gpu::textString (*s.data, op) == "Q";
                }
            }
            check (frames == 1 && markers == 1 && text == 1, "one active Q frame and one unnumbered marker");
            // Q marker is drawn inside the frame bounds at top-left
            const Rect u = qg.rect();
            check (s.data->ops[0].f[0] <= static_cast<float> (u.x) &&
                   s.data->ops[0].f[1] <= static_cast<float> (u.y) &&
                   s.data->ops[0].f[2] >= static_cast<float> (u.right()) &&
                   s.data->ops[0].f[3] >= static_cast<float> (u.bottom()),
                   "Frame cell completely encloses union rect and Q marker");
        }
        else
        {
            check (s.numOps() == 0, "Gen 2: active group draw produces 0 ops");
        }
    }

    // Active state with inactive look (e.g. switched-off oscillator)
    qg.setInactive (true);
    check (qg.inactive(), "QGroup inactive look is set");
    {
        Scene s (400, 300);
        qg.draw (s.builder());
        if constexpr (QVisualPolicy::kHasQFrame)
            check (s.numOps() >= 3, "Inactive look active group still draws frame and marker in grey");
        else
            check (s.numOps() == 0, "Gen 2: inactive look active group produces 0 ops");
    }
    qg.setInactive (false);

    // Deactivating clears active state
    qg.setActive (false);
    check (! qg.active(), "Group deactivated");
}

static void test_qgroup_frameless_mode()
{
    Knob k0 ("K0"), k1 ("K1");
    k0.place ({ 100, 100, 80, 56 });
    k1.place ({ 300, 100, 80, 56 });

    QGroup qg;
    qg.set (0, &k0);
    qg.set (1, &k1);
    qg.setActive (true);

    check (! qg.frameless(), "Default QGroup is framed");

    // Scene with frame in Gen 1; 0 ops in Gen 2
    uint32_t opsWithFrame = 0;
    {
        Scene s (500, 300);
        qg.draw (s.builder());
        opsWithFrame = s.numOps();
        if constexpr (QVisualPolicy::kHasQFrame)
            check (opsWithFrame > 0, "Framed QGroup draws frame and Q marker");
        else
            check (opsWithFrame == 0, "Gen 2 QGroup produces 0 ops");
    }

    // Frameless mode
    qg.setFrameless (true);
    check (qg.frameless(), "QGroup frameless() is true");

    {
        Scene s (500, 300);
        qg.draw (s.builder());
        if constexpr (QVisualPolicy::kHasQFrame)
            check (s.numOps() < opsWithFrame, "Frameless QGroup omits the outer bounding frame");
        else
            check (s.numOps() == 0, "Gen 2 frameless QGroup produces 0 ops");
    }
}

static void test_qgroup_adjacency_check()
{
    Knob k0 ("K0"), k1 ("K1");
    k0.place ({ 100, 100, 80, 80 });
    k1.place ({ 200, 100, 80, 80 });

    QGroup qg;
    qg.set (0, &k0);
    qg.set (1, &k1);

    // Intruder placed between k0 and k1 (overlaps union rectangle)
    Knob intruder ("INTRUDER");
    intruder.place ({ 150, 100, 40, 80 });

    // Innocent knob completely outside
    Knob outside ("OUTSIDE");
    outside.place ({ 400, 400, 80, 80 });

    ControlModel* all[4] = { &k0, &k1, &intruder, &outside };
    const ControlModel* first = nullptr;
    int bad = qg.check (all, 4, &first);
    check (bad == 1, "Adjacency check finds 1 intruder inside union");
    check (first == &intruder, "Adjacency check reports intruder component");

    // Clean layout without intruder
    ControlModel* clean[3] = { &k0, &k1, &outside };
    first = nullptr;
    bad = qg.check (clean, 3, &first);
    check (bad == 0, "Clean layout has 0 adjacency violations");
    check (first == nullptr, "Clean layout reports nullptr first violation");

    // In frameless mode, adjacency check is bypassed
    qg.setFrameless (true);
    bad = qg.check (all, 4, &first);
    check (bad == 0, "Frameless mode bypasses adjacency check");
}

static void test_qgroup_mixed_models_and_widgets()
{
    KnobModel km ("CUTOFF");
    km.setParamHandle ({ 601, 12, "filter1.cutoff" });
    km.place ({ 100, 100, 100, 80 });
    km.state.value = 0.25;
    km.state.defaultValue = 0.5;
    setText (km.state.valueText, sizeof km.state.valueText, "420 Hz");

    NumberFieldModel nm ("OCT", NumberFieldModel::Format::IntSigned);
    nm.setParamHandle ({ 602, 13, "osc1.octave" });
    nm.place ({ 210, 100, 100, 80 });
    nm.state.min = -4.0;
    nm.state.max = 4.0;
    nm.state.step = 1.0;
    nm.state.value = 1.0;

    Stepper st;
    st.setId (603);
    st.place ({ 320, 100, 140, 80 });
    st.state.count = 4;
    st.state.index = 1;
    st.setItem ("SAW");

    // ParamHandle & model contract metadata checks
    check (km.paramHandle().bound() && km.param() == 12 && std::strcmp (km.paramKey(), "filter1.cutoff") == 0,
           "KnobModel stores ParamHandle (id, param, key)");
    check (km.visible() && nm.visible(), "Models default to visible");
    km.setVisible (false);
    check (! km.visible(), "Model setVisible updates visibility");
    km.setVisible (true);

    char buf[16];
    km.formatText (buf, sizeof buf);
    check (std::strcmp (buf, "420 Hz") == 0, "KnobModel formatText returns valueText");
    nm.formatText (buf, sizeof buf);
    check (std::strcmp (buf, "+1") == 0, "NumberFieldModel formatText returns formatted value");

    QGroup qg;
    qg.set (0, &km);
    qg.set (1, &nm);
    qg.set (2, &st);
    check (qg.numMembers() == 3, "Mixed QGroup holds 3 members (2 pure models + 1 widget)");
    check (qg.member (0) == &km && qg.member (1) == &nm && qg.member (2) == &st,
           "Mixed QGroup member() returns exact ControlModel pointers");
    check (qg.rect() == Rect { 100, 100, 360, 80 }, "Mixed QGroup rect() unites model and widget rects");

    qg.setActive (true);
    Change c = qg.turn (0, 0.25);
    check (c.kind == ChangeKind::Value && c.id == 601 && std::fabs (c.value - 0.5) < 1.0e-9,
           "Mixed QGroup turn(0) routes to KnobModel");
    c = qg.turn (1, 2.0);
    check (c.kind == ChangeKind::Index && c.id == 602 && c.index == 7 && std::fabs (c.value - 3.0) < 1.0e-9,
           "Mixed QGroup turn(1) routes to NumberFieldModel");
    c = qg.turn (2, 1.0);
    check (c.kind == ChangeKind::Index && c.id == 603 && c.index == 2,
           "Mixed QGroup turn(2) routes to legacy Stepper widget");

    KnobModel intruder ("INTRUDER");
    intruder.place ({ 200, 110, 40, 60 });
    KnobModel outside ("OUTSIDE");
    outside.place ({ 600, 400, 80, 60 });

    ControlModel* all[5] = { &km, &nm, &st, &intruder, &outside };
    const ControlModel* first = nullptr;
    check (qg.check (all, 5, &first) == 1 && first == &intruder,
           "Mixed QGroup check() detects ControlModel intruder inside union");

    ControlModel* clean[4] = { &km, &nm, &st, &outside };
    first = nullptr;
    check (qg.check (clean, 4, &first) == 0 && first == nullptr,
           "Mixed QGroup check() passes clean layout with mixed models and widgets");
}

int main()
{
    test_qgroup_slots_and_turn_routing();
    test_qgroup_rect_and_frame_rect();
    test_qgroup_wide_device_and_idle_chips();
    test_qgroup_single_unnumbered_q_marker();
    test_qgroup_frameless_mode();
    test_qgroup_adjacency_check();
    test_qgroup_mixed_models_and_widgets();

    std::printf ("ui_cov_qgroups_chips_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
