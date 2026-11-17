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
    check (k0.qFrame() && k0.qIndex() == 1, "Slot 0 member gets Q index 1");
    check (k1.qFrame() && k1.qIndex() == 2, "Slot 1 member gets Q index 2");
    check (k2.qFrame() && k2.qIndex() == 3, "Slot 2 member gets Q index 3");
    check (k3.qFrame() && k3.qIndex() == 4, "Slot 3 member gets Q index 4");

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

    // Draw idle chips on standard device
    {
        Scene s (400, 300);
        qg.drawIdle (s.builder());
        check (s.numOps() > 0, "Standard device idle chips generate scene ops");
    }

    // Wide device mode (MPC XL)
    QGroup::setWideDevice (true);
    check (QGroup::wideDevice(), "Wide device flag is true");

    // In wide mode with block 0 (active group) or block -1 (unmapped): drawIdle does nothing
    qg.setBlock (-1);
    {
        Scene s (400, 300);
        qg.drawIdle (s.builder());
        check (s.numOps() == 0, "Wide device with block -1 produces no idle chips");
    }

    // In wide mode with block 1..3: draws chips 5..16
    qg.setBlock (2); // block 2 -> knobs 9..12
    {
        Scene s (400, 300);
        qg.drawIdle (s.builder());
        check (s.numOps() > 0, "Wide device with block 2 generates scene ops for knobs 9..12");
    }

    // drawMapped only draws when block >= 1
    {
        Scene s (400, 300);
        qg.drawMapped (s.builder());
        check (s.numOps() > 0, "drawMapped with block >= 1 generates scene ops");
    }
    qg.setBlock (0);
    {
        Scene s (400, 300);
        qg.drawMapped (s.builder());
        check (s.numOps() == 0, "drawMapped with block 0 does nothing");
    }

    // Static drawMappedMembers helper
    const Component* members[2] = { &k0, &k1 };
    {
        Scene s (400, 300);
        QGroup::drawMappedMembers (s.builder(), 1, members, 2);
        check (s.numOps() > 0, "drawMappedMembers generates scene ops");
    }
    {
        Scene s (400, 300);
        QGroup::drawMappedMembers (s.builder(), 0, members, 2);
        check (s.numOps() == 0, "drawMappedMembers with block 0 does nothing");
    }

    // Reset wide device flag
    QGroup::setWideDevice (false);
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

    // Scene with frame
    uint32_t opsWithFrame = 0;
    {
        Scene s (500, 300);
        qg.draw (s.builder());
        opsWithFrame = s.numOps();
        check (opsWithFrame > 0, "Framed QGroup draws frame and chips");
    }

    // Frameless mode
    qg.setFrameless (true);
    check (qg.frameless(), "QGroup frameless() is true");

    {
        Scene s (500, 300);
        qg.draw (s.builder());
        check (s.numOps() < opsWithFrame, "Frameless QGroup omits the outer bounding frame");
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

    Component* all[4] = { &k0, &k1, &intruder, &outside };
    const Component* first = nullptr;
    int bad = qg.check (all, 4, &first);
    check (bad == 1, "Adjacency check finds 1 intruder inside union");
    check (first == &intruder, "Adjacency check reports intruder component");

    // Clean layout without intruder
    Component* clean[3] = { &k0, &k1, &outside };
    first = nullptr;
    bad = qg.check (clean, 3, &first);
    check (bad == 0, "Clean layout has 0 adjacency violations");
    check (first == nullptr, "Clean layout reports nullptr first violation");

    // In frameless mode, adjacency check is bypassed
    qg.setFrameless (true);
    bad = qg.check (all, 4, &first);
    check (bad == 0, "Frameless mode bypasses adjacency check");
}

int main()
{
    test_qgroup_slots_and_turn_routing();
    test_qgroup_rect_and_frame_rect();
    test_qgroup_wide_device_and_idle_chips();
    test_qgroup_frameless_mode();
    test_qgroup_adjacency_check();

    std::printf ("ui_cov_qgroups_chips_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
