// SynthyBoy - native ModulationsPage routing through VST and the display controller.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

void testMixerFilterPeekNativeModulationsVst();

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
    testMixerFilterPeekNativeModulationsVst();
}

void testMixerFilterPeekNativeModulationsVst()
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
                                   [] (const auto& v) { return std::string_view (v.name) == "mixer"; });
    if (slot < 0 || tab == std::end (layout::kViews))
    {
        check (false, "the Mixer Filter PEEK Modulations fixture finds the actual Mixer tab");
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

    const int base = PluginInstance::nativeUiBase() + u::kModTargets;
    const int viewParam = PluginInstance::nativeUiBase() + u::kMixerPeekView;
    const int peekParam = PluginInstance::nativeUiBase() + u::kMixerPeek;
    constexpr int kMixerFrame = n::frame (n::Mixer);
    VstParameterProperties viewProps {};
    check (r.effect.numParams == PluginInstance::nativeUiBase() + u::kCount
           && PluginInstance::nativeUiBase() + u::kMacroTarget == 3541 && base == 3502
           && PluginInstance::nativeUiBase() + u::kModDetailReserved == 3569
           && PluginInstance::nativeUiBase() + u::kMixerActiveTab == 3575
           && r.effect.dispatcher (&r.effect, effGetParameterProperties, base + n::kView, 0, &viewProps, 0.0f) == 1
           && viewProps.maxInteger == kMixerFrame,
           "Mixer Filter PEEK Modulations retains the original ABI slots, appends 8 Macro Targets handles and advertises all 9 owner frames");

    const int f1On = target ("filter1.on"), f2On = target ("filter2.on");
    const int f1Cut = target ("filter1.cutoff"), f2Cut = target ("filter2.cutoff");
    const int f1Dest = static_cast<int> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (f1Cut)));
    const int f2Dest = static_cast<int> (synthyboy::modDestinationChoiceOf (static_cast<size_t> (f2Cut)));
    engine (f1On, 1.0f);
    engine (f2On, 1.0f);
    for (int i = 0; i < 10; ++i)
    {
        const auto route = synthyboy::modSlotParams (2 * i);
        engine (route.destination, static_cast<float> (f1Dest));
        engine (route.amount, 10.0f + static_cast<float> (i));
        engine (route.source, static_cast<float> (i + 1));
    }
    const int f2Slot0 = 20, f2Slot1 = 22;
    const auto f2Route0 = synthyboy::modSlotParams (f2Slot0);
    const auto f2Route1 = synthyboy::modSlotParams (f2Slot1);
    engine (f2Route0.destination, static_cast<float> (f2Dest));
    engine (f2Route0.amount, 25.0f);
    engine (f2Route0.source, 1.0f);
    engine (f2Route1.destination, static_cast<float> (f2Dest));
    engine (f2Route1.amount, 35.0f);
    engine (f2Route1.source, 2.0f);

    const int ninth = 16;
    const auto deep = synthyboy::modSlotParams (ninth);
    r.plugin.overlayView.setTabView (IInstanceView::kTabViewFilter, 3);
    const auto begin = r.save();
    engine (deep.source, 12.0f);
    const auto replacement = r.save();
    check (r.restore (begin), "the Mixer Filter PEEK modulation preset restores in the VST fixture");
    r.expireRestoreWindow();

    r.plugin.overlayView.setTabView (IInstanceView::kTabViewFilter, 3);
    const int index = static_cast<int> (tab - std::begin (layout::kViews));
    frame.setView (index, true, 90);
    frame.setViewInstance (index, slot, true, 90);
    tick (24);

    // 1. Open Filter 1 PEEK and long-press cutoff to open the shared native Modulations editor.
    write (peekParam, 1.0f / u::kMixerPeekTargets);
    tick();
    const auto* view = dynamic_cast<const pages::MixerPage*> (controller.currentView());
    if (view == nullptr)
    {
        check (false, "the Mixer Filter PEEK fixture opens the real MixerPage");
        controller.detach (&r.plugin.overlayView);
        return;
    }
    ui::FilterFullScreen refFilter;
    refFilter.setPageMode (true);
    refFilter.setNativePeekGeometry (true);
    refFilter.place ({0, MixerUi::kTabY, MixerUi::kCanvasW, MixerUi::kTabH});
    const auto cutoffRect = refFilter.cutoff.rect();
    check (controller.uiState (slot).top() == MixerUi::kPagePeek && read (viewParam) == 0.5f
           && r.plugin.nativeFilter.layout() == 1 && r.plugin.nativeFilter.at (u::FilterCutoff).cell().target == f1Cut,
           "Filter 1 PEEK publishes its native Filter surface before Modulations opens");

    const auto cutoffBefore = read (f1Cut);
    emit (TouchEvent::Down, cutoffRect);
    tick (28);
    emit (TouchEvent::Up, cutoffRect);
    check (controller.uiState (slot).depth() == 3 && controller.uiState (slot).top() == FilterUi::kPageMods
           && controller.uiState (slot).element() == MixerUi::kElementPage
           && read (viewParam) == 0.0f && r.plugin.nativeFilter.layout() == 0
           && r.plugin.nativeModTargets.mode() == n::Modulations
           && r.plugin.nativeModTargets.layout() == kMixerFrame
           && std::fabs (read (base + n::kView) - 1.0f) < 1.0e-5f
           && read (base + n::kRowGate) == 0.5f && read (f1Cut) == cutoffBefore,
           "a stationary Filter 1 PEEK cutoff press opens the Mixer native Modulations editor without editing cutoff");

    // 2. Clipped composition & touch routing: native controls inside peekPanelRect are transparent and host-owned;
    //    blurred Mixer border outside peekPanelRect remains opaque and overlay-owned.
    ui::ModulationsPage refMods;
    refMods.place ({0, MixerUi::kTabY, MixerUi::kCanvasW, MixerUi::kTabH});
    refMods.setNativeControls (true);
    ui::ModInput refInput;
    for (int i = 0; i < 6; ++i)
        refInput.add (i + 1, "SRC", 0.1, false, false, false);
    refMods.load (refInput, "CUTOFF", 10, 0);
    uibind::fillSourceBoard (refMods.board);

    std::vector<uint32_t> pixels (1280 * 800, 0u);
    const_cast<IView*> (controller.currentView())->render (pixels.data(), 1280, 800);
    const auto peekPanel = view->ui().peekPanelRect();
    bool clippedInside = true, transparentCells = true, hostOwned = true;
    for (const auto area : {refMods.back.rect(), refMods.add.rect(), refMods.page.rect(),
                            refMods.rows[0].depth.rect(), refMods.rows[0].polarity.rect(),
                            refMods.rows[0].bypass.rect(), refMods.rows[0].remove.rect()})
    {
        const int cx = area.x + area.w / 2, cy = area.y + area.h / 2;
        clippedInside &= peekPanel.contains (area.x, area.y) && peekPanel.contains (area.right() - 1, area.bottom() - 1);
        transparentCells &= pixels[static_cast<size_t> (cy * 1280 + cx)] == 0u;
        hostOwned &= ! view->ui().covers (cx, cy);
    }
    bool borderOpaque = true;
    for (const auto pt : {std::pair<int, int> {2, 170}, std::pair<int, int> {1278, 170},
                          std::pair<int, int> {640, 112}, std::pair<int, int> {640, 736}})
    {
        borderOpaque &= (pixels[static_cast<size_t> (pt.second * 1280 + pt.first)] >> 24) == 255
                     && view->ui().covers (pt.first, pt.second);
    }
    check (clippedInside && transparentCells && hostOwned && borderOpaque,
           "Mixer Filter PEEK Modulations clips native apertures inside peekPanelRect and keeps the blurred border opaque");

    // 3. Paging, single-write VST routing, Q-Link focus without duplicate touch, preset recall guard, ADD/REMOVE.
    write (base + n::Page, 2.0f / 3.0f);
    tick();
    const int field = n::field (2, n::Amount);
    check (r.plugin.nativeModTargets.at (field).cell().target == deep.amount,
           "host paging reaches the ninth Filter 1 cutoff route inside Mixer PEEK");

    const auto rowRect = refMods.rows[2].depth.rect();
    const auto touchBefore = controller.touchTaken();
    emit (TouchEvent::Down, rowRect);
    emit (TouchEvent::Up, rowRect);
    int q = 0;
    while (q < synthyboy_mpc::QRouter::kCount && r.plugin.qBank.target (q) != deep.amount)
        ++q;
    check (controller.touchTaken() == touchBefore && q < synthyboy_mpc::QRouter::kCount,
           "touching the native amount cell inside PEEK yields to host with no duplicate input and maps its Q target");
    if (q < synthyboy_mpc::QRouter::kCount)
    {
        const auto qBefore = r.plugin.qBank.writeCount();
        const auto beforeUiWrite = writes();
        write (PluginInstance::qBase() + q, 0.73f);
        tick();
        check (std::fabs (read (deep.amount) - 0.73f) < 1.0e-5f && r.plugin.qBank.writeCount() == qBefore + 1 && writes() == beforeUiWrite,
               "the VST Q proxy writes the visible Filter 1 PEEK modulation row once");
    }

    const auto polBefore = writes();
    write (base + n::field (2, n::Polarity), 1.0f);
    write (base + n::field (2, n::Active), 0.0f);
    tick();
    check (read (deep.bipolar) == 1.0f && read (deep.bypass) == 1.0f && writes() == polBefore + 2
           && read (f2Route0.amount) == synthyboy::toNormalised (synthyboy::paramTable()[f2Route0.amount], 25.0f),
           "native Polarity and Active update only the selected Filter 1 route");

    const auto beforeRecall = writes();
    write (base + field, 0.85f);
    check (r.restore (replacement), "a preset can retarget the Filter 1 PEEK route while Modulations is open");
    r.expireRestoreWindow();
    write (base + field, 0.89f);
    tick();
    check (controller.uiState (slot).top() == FilterUi::kPageMods && r.save() == replacement
           && writes() == beforeRecall && r.plugin.nativeModTargets.at (field).cell().target == deep.amount,
           "recalled Filter 1 PEEK Modulations rejects delayed writes across preset change");
    write (base + field, 0.25f);
    tick();
    check (std::fabs (read (deep.amount) - 0.25f) < 1.0e-5f && writes() == beforeRecall + 1,
           "a fresh native gesture edits the recalled Filter 1 route once");

    write (base + n::field (2, n::Remove), 1.0f);
    tick();
    check (read (deep.source) == 0.0f && read (deep.destination) == 0.0f,
           "native REMOVE frees the ninth Filter 1 route inside Mixer PEEK");
    write (base + n::Add, 1.0f);
    tick();
    const auto sourceChip = refMods.board.chipRectOfId (23);
    check (r.plugin.nativeModTargets.layout() == 0 && r.plugin.qBank.target (0) < 0 && ! sourceChip.empty(),
           "native ADD opens the overlay source board inside PEEK and retires host rows and Q map");
    if (! sourceChip.empty())
    {
        emit (TouchEvent::Down, sourceChip);
        emit (TouchEvent::Up, sourceChip);
        const auto added = synthyboy::modSlotParams (1);
        check (static_cast<int> (std::lround (r.plugin.overlayView.param (added.source))) == 23
               && static_cast<int> (std::lround (r.plugin.overlayView.param (added.destination))) == f1Dest
               && r.plugin.nativeModTargets.layout() == kMixerFrame,
               "tapping a source chip adds the Filter 1 route and restores the native PEEK Modulations surface");
    }

    // 4. Back preserves PEEK and selected Filter 1, restores native Filter PEEK surface, and blocks stale/late writes.
    const auto beforeBack = writes();
    write (base + n::field (0, n::Amount), 0.91f);
    write (base + n::Back, 1.0f);
    tick();
    write (base + n::field (0, n::Amount), 0.92f);
    tick();
    check (controller.uiState (slot).depth() == 2 && controller.uiState (slot).top() == MixerUi::kPagePeek
           && controller.uiState (slot).element() == MixerUi::kElementPage
           && r.plugin.nativeModTargets.layout() == 0 && read (base + n::kView) == 0.0f
           && read (viewParam) == 0.5f && r.plugin.nativeFilter.layout() == 1
           && r.plugin.nativeFilter.at (u::FilterCutoff).cell().target == f1Cut
           && r.plugin.overlayView.tabView (IInstanceView::kTabViewFilter) == 3
           && writes() == beforeBack,
           "native Back returns to Filter 1 PEEK, restores its Filter controls, preserves standalone view, and blocks stale writes");

    // 5. Stale controls after tab departure / close cannot write.
    emit (TouchEvent::Down, cutoffRect);
    tick (28);
    emit (TouchEvent::Up, cutoffRect);
    check (controller.uiState (slot).top() == FilterUi::kPageMods && r.plugin.nativeModTargets.layout() == kMixerFrame,
           "long-press reopens Filter 1 PEEK Modulations before tab departure");
    const auto beforeLeave = writes();
    write (base + n::field (0, n::Amount), 0.88f);
    frame.setView (index, false, 90);
    tick();
    write (base + n::field (0, n::Amount), 0.89f);
    tick();
    check (read (base + n::kView) == 0.0f && r.plugin.nativeModTargets.layout() == 0 && writes() == beforeLeave,
           "leaving the Mixer tab disarms Modulations and rejects queued or late writes");

    frame.setView (index, true, 90);
    frame.setViewInstance (index, slot, true, 90);
    tick (24);

    // 6. Stale controls after filter OFF cannot write.
    write (peekParam, 1.0f / u::kMixerPeekTargets);
    tick();
    emit (TouchEvent::Down, cutoffRect);
    tick (28);
    emit (TouchEvent::Up, cutoffRect);
    check (controller.uiState (slot).top() == FilterUi::kPageMods && r.plugin.nativeModTargets.layout() == kMixerFrame,
           "Filter 1 PEEK Modulations reopens before OFF test");
    engine (f1On, 0.0f);
    const auto offState = r.save();
    const auto beforeOff = writes();
    write (base + n::field (0, n::Amount), 0.7f);
    tick();
    write (base + n::field (0, n::Amount), 0.8f);
    tick();
    check (controller.uiState (slot).depth() == 2 && controller.uiState (slot).top() == MixerUi::kPagePeek
           && read (base + n::kView) == 0.0f && r.plugin.nativeModTargets.layout() == 0
           && r.save() == offState && writes() == beforeOff,
           "turning the peeked filter OFF closes Modulations to Filter PEEK and rejects stale writes");
    emit (TouchEvent::Down, view->ui().peekCloseRect());
    emit (TouchEvent::Up, view->ui().peekCloseRect());

    // 7. Filter 2 PEEK isolation and Back preserving Filter 2.
    write (peekParam, 2.0f / u::kMixerPeekTargets);
    tick();
    check (controller.uiState (slot).top() == MixerUi::kPagePeek && r.plugin.nativeFilter.layout() == 2
           && r.plugin.nativeFilter.at (u::FilterCutoff).cell().target == f2Cut,
           "Filter 2 PEEK opens Filter 2 independently of Filter 1 and standalone Filter");
    emit (TouchEvent::Down, cutoffRect);
    tick (28);
    emit (TouchEvent::Up, cutoffRect);
    check (controller.uiState (slot).top() == FilterUi::kPageMods && r.plugin.nativeModTargets.layout() == kMixerFrame
           && r.plugin.nativeModTargets.at (n::field (0, n::Amount)).cell().target == f2Route0.amount,
           "Filter 2 PEEK long-press opens Modulations bound to Filter 2 cutoff");
    const auto f2Before = writes();
    write (base + n::field (0, n::Amount), 0.65f);
    tick();
    check (std::fabs (read (f2Route0.amount) - 0.65f) < 1.0e-5f && writes() == f2Before + 1,
           "Filter 2 PEEK Modulations edits only Filter 2's route");
    write (base + n::Back, 1.0f);
    tick();
    check (controller.uiState (slot).depth() == 2 && controller.uiState (slot).top() == MixerUi::kPagePeek
           && r.plugin.nativeModTargets.layout() == 0 && read (viewParam) == 0.5f
           && r.plugin.nativeFilter.layout() == 2 && r.plugin.nativeFilter.at (u::FilterCutoff).cell().target == f2Cut
           && r.plugin.overlayView.tabView (IInstanceView::kTabViewFilter) == 3,
           "native Back from Filter 2 PEEK Modulations preserves PEEK on Filter 2 and standalone VELO");

    controller.detach (&r.plugin.overlayView);
}
