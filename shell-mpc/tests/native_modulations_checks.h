// SynthyBoy - native ModulationsPage routing through VST and the display controller.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

void testNativeModulationsVst()
{
    using namespace synthyboy_mpc::overlay;
    namespace ui = synthyboy_mpc::ui;
    namespace u = synthyboy_mpc::nativeui;
    namespace n = u::targets;
    struct Touch final : ITouchInput
    {
        std::vector<TouchEvent> pending;
        void configure (int, int, int) override {}
        int poll (const std::function<void (const TouchEvent&)>& receive) override
        {
            for (const auto& event : pending)
                receive (event);
            const auto count = static_cast<int> (pending.size());
            pending.clear();
            return count;
        }
    } touch;

    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    ovt::TempDir dir;
    ovt::FakePlane plane;
    ovt::FakeFrame frame;
    CpuCurveRenderer renderer;
    SynthyBoyController controller (plane, frame, renderer, dir.paths(), Timing {}, 10.0, nullptr, &touch);
    const int slot = controller.attach (&r.plugin.overlayView);
    const auto* tab = std::find_if (std::begin (layout::kViews), std::end (layout::kViews),
                                   [] (const auto& v) { return std::string_view (v.name) == "osca"; });
    if (slot < 0 || tab == std::end (layout::kViews))
    {
        check (false, "the Modulations VST fixture finds the actual Osc A tab");
        return;
    }

    const auto write = [&] (int index, float normalized) { r.effect.setParameter (&r.effect, index, normalized); };
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto engine = [&] (int index, float value) { write (index, synthyboy::toNormalised (synthyboy::paramTable()[index], value)); };
    const auto writes = [&] { return r.plugin.uiWriteCalls.load (std::memory_order_relaxed); };
    const auto tick = [&] (int count = 6)
    {
        for (int i = 0; i < count; ++i)
        {
            controller.tick (r.now += 25.0);
            r.plugin.nativeUiPoll (&r.effect);
        }
    };
    const auto emit = [&] (TouchEvent::Kind kind, ui::Rect area)
    {
        touch.pending.push_back ({kind, area.x + area.w / 2, area.y + area.h / 2});
        tick();
    };

    const int power = target ("osc1.on"), blend = target ("osc1.unison.blend");
    const int destination = static_cast<int> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (blend)));
    const int base = PluginInstance::nativeUiBase() + u::kModTargets;
    const int detail = PluginInstance::nativeUiBase() + u::kOscDetail;
    const int ninth = 16;
    const auto deep = synthyboy::modSlotParams (ninth);
    engine (power, 1.0f);
    engine (target ("osc1.type"), 0.0f);
    for (int i = 0; i < 10; ++i)
    {
        const auto route = synthyboy::modSlotParams (2 * i);
        engine (route.destination, static_cast<float> (destination));
        engine (route.amount, 10.0f + static_cast<float> (i));
        engine (route.source, static_cast<float> (i + 1));
    }
    const auto begin = r.save();
    engine (deep.source, 12.0f);
    const auto replacement = r.save();
    check (r.restore (begin), "the ten-route starting preset restores in the VST fixture");
    r.expireRestoreWindow();

    const int index = static_cast<int> (tab - std::begin (layout::kViews));
    frame.setView (index, true, 90);
    frame.setViewInstance (index, slot, true, 90);
    tick (24);
    write (detail + u::oscdetail::kNavigation, 1.0f);
    tick();
    const auto* view = dynamic_cast<const pages::OscPage*> (controller.currentView());
    int detailField = 0;
    while (detailField < u::oscdetail::kCells && r.plugin.nativeOscDetail.at (detailField).cell().target != blend)
        ++detailField;
    if (view == nullptr || detailField == u::oscdetail::kCells)
    {
        check (false, "the actual MORE page exposes the native blend control used as a Modulations destination");
        controller.detach (&r.plugin.overlayView);
        return;
    }

    const auto& screen = view->ui().ui().modsPage();
    const auto cell = view->ui().ui().nativeDetailRect (detailField);
    emit (TouchEvent::Down, cell);
    tick (28);
    emit (TouchEvent::Up, cell);
    check (controller.uiState (slot).top() == OscUi::kPageMods && screen.numRows() == 6
           && r.plugin.nativeModTargets.mode() == n::Modulations
           && r.plugin.nativeModTargets.layout() == n::frame (n::Osc),
           "a stationary native knob press opens the six-row host Modulations editor");

    write (base + n::Page, 1.0f);
    tick();
    const int field = n::field (2, n::Amount);
    check (screen.page.state.index == 1 && screen.numRows() == 4 && screen.rows[2].source == 9
           && r.plugin.nativeModTargets.at (field).cell().target == deep.amount,
           "host paging reaches the ninth assignment and gives its stable amount handle the visible slot");

    const auto rowRect = screen.rows[2].depth.rect();
    const auto touchBefore = controller.touchTaken();
    emit (TouchEvent::Down, rowRect);
    emit (TouchEvent::Up, rowRect);
    int q = 0;
    while (q < synthyboy_mpc::QRouter::kCount && r.plugin.qBank.target (q) != deep.amount)
        ++q;
    check (screen.activeRow() == 2 && controller.touchTaken() == touchBefore && q < synthyboy_mpc::QRouter::kCount,
           "touching the native amount cell passes to the host and publishes the visible row's real Q target");
    if (q < synthyboy_mpc::QRouter::kCount)
    {
        write (PluginInstance::qBase() + q, 0.73f);
        tick();
        check (std::fabs (read (deep.amount) - 0.73f) < 1.0e-5f,
               "the VST Q proxy edits the ninth assignment on the visible page");
    }

    const auto beforeRecall = writes();
    write (base + field, 0.85f);
    check (r.restore (replacement), "a preset can change the source of the same ninth route while the editor is open");
    r.expireRestoreWindow();
    write (base + field, 0.89f);
    tick();
    check (controller.uiState (slot).top() == OscUi::kPageMods && screen.page.state.index == 1
           && screen.rows[2].source == 12 && r.save() == replacement && writes() == beforeRecall,
           "the recalled editor retargets the row and a delayed callback leaves the new preset byte-identical");
    write (base + field, 0.25f);
    tick();
    check (std::fabs (read (deep.amount) - 0.25f) < 1.0e-5f && writes() == beforeRecall + 1,
           "a fresh stock amount gesture edits only the newly bound route");

    write (base + n::field (2, n::Remove), 1.0f);
    tick();
    check (read (deep.source) == 0.0f && read (deep.destination) == 0.0f && screen.numRows() == 3,
           "native REMOVE deletes the ninth route and repacks the visible page");
    write (base + n::Add, 1.0f);
    tick();
    const auto sourceChip = screen.board.chipRectOfId (23);
    check (screen.boardShown() && screen.pickingSource() && ! sourceChip.empty()
           && r.plugin.nativeModTargets.layout() == 0 && r.plugin.qBank.target (0) < 0,
           "native ADD opens the full source board and retires the previous rows and Q map");
    if (! sourceChip.empty())
    {
        emit (TouchEvent::Down, sourceChip);
        emit (TouchEvent::Up, sourceChip);
        const auto added = synthyboy::modSlotParams (1);
        check (static_cast<int> (std::lround (r.plugin.overlayView.param (added.source))) == 23
               && static_cast<int> (std::lround (r.plugin.overlayView.param (added.destination))) == destination
               && screen.page.state.index == 0 && screen.activeRow() == 1 && ! screen.boardShown(),
               "choosing a source outside the quick list adds it once and focuses its real slot");
    }

    for (int i = 0; i < 10; ++i)
        engine (synthyboy::modSlotParams (2 * i).source, 0.0f);
    tick();
    check (screen.numRows() == 1 && screen.rows[0].source == 23,
           "the list reloads after the host removes all routes except the one just added");
    write (base + n::field (0, n::Remove), 1.0f);
    tick (1);
    check (screen.boardShown() && r.plugin.nativeModTargets.layout() == 0
           && r.plugin.qBank.target (0) < 0,
           "removing the last route shows the source board and retires the host bank in the same tick");
    if (! sourceChip.empty())
    {
        emit (TouchEvent::Down, sourceChip);
        emit (TouchEvent::Up, sourceChip);
    }

    engine (power, 0.0f);
    const auto off = r.save();
    const auto offWrites = writes();
    write (base + field, 0.7f);
    tick();
    write (base + field, 0.8f);
    tick();
    check (controller.uiState (slot).depth() == 1 && r.plugin.nativeModTargets.layout() == 0
           && r.plugin.qBank.target (0) < 0 && r.save() == off && writes() == offWrites,
           "OFF closes the Modulations editor and rejects queued or subsequent row writes");
    controller.detach (&r.plugin.overlayView);
}
