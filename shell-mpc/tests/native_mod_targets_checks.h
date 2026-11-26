// SynthyBoy - actual VST regressions for the shared TARGETS page.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

void testNativeModTargets()
{
    using namespace synthyboy_mpc::overlay;
    namespace n = synthyboy_mpc::nativeui;
    namespace ui = synthyboy_mpc::ui;
    namespace t = n::targets;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    const int base = PluginInstance::nativeUiBase() + n::kModTargets;
    const auto write = [&] (int field, float value) { r.effect.setParameter (&r.effect, base + field, value); };
    const auto read = [&] (int field) { return r.effect.getParameter (&r.effect, base + field); };
    const auto set = [&] (int index, float plain)
    {
        r.effect.setParameter (&r.effect, index, synthyboy::toNormalised (synthyboy::paramTable()[index], plain));
    };
    const auto seed = [&] (int slot, int source, int destination)
    {
        const auto p = synthyboy::modSlotParams (slot);
        set (p.destination, static_cast<float> (destination));
        set (p.amount, 0.0f);
        set (p.bipolar, 0.0f);
        set (p.bypass, 0.0f);
        set (p.source, static_cast<float> (source));
    };
    const auto value = [&] (int param) { return r.plugin.overlayView.param (param); };
    const auto tick = [&] (IView& view)
    {
        ViewInput input;
        input.instance = input.io = &r.plugin.overlayView;
        input.ui = &r.stack;
        input.nowMs = r.now += 125.0;
        view.update (input);
        r.plugin.nativeUiPoll (&r.effect);
    };
    const auto tap = [&] (IView& view, auto area)
    {
        const int x = area.x + area.w / 2, y = area.y + area.h / 2;
        const bool down = view.onTouch ({TouchEvent::Down, x, y}, r.stack, r.plugin.overlayView, r.now);
        const bool up = view.onTouch ({TouchEvent::Up, x, y}, r.stack, r.plugin.overlayView, r.now + 20.0);
        tick (view);
        return down && up;
    };
    const auto dest = static_cast<int> (synthyboy::modDestinationChoiceOf (target ("filter1.cutoff")));
    const auto m = synthyboy::modSlotParams (0);
    const auto checkRoute = [&] (IView& view, ui::ModTargetsPage& screen, int frame, int source, int top)
    {
        const auto area = screen.rows[0].depth.rect();
        std::vector<uint32_t> pixels (1280 * 800);
        view.render (pixels.data(), 1280, 800);
        const int x = area.x + area.w / 2, y = area.y + area.h / 2;
        check (base == 3500 && read (t::kView) == static_cast<float> (frame) / t::kLastFrame
               && read (t::kRowGate) == 1.0f && r.stack.top() == top && screen.numRows() == 1
               && pixels[1280 * y + x] == 0u && ! tap (view, area),
               "the owning TARGETS view publishes a transparent, host-owned row");
        const auto writes = r.plugin.uiWriteCalls.load (std::memory_order_relaxed);
        write (t::field (0, t::Amount), 0.75f);
        write (t::field (0, t::Polarity), 1.0f);
        write (t::field (0, t::Active), 0.0f);
        tick (view);
        check (value (m.source) == source && value (m.amount) == 50.0f && value (m.bipolar) == 1.0f && value (m.bypass) == 1.0f
               && r.plugin.uiWriteCalls.load (std::memory_order_relaxed) == writes + 3,
               "native amount, polarity and active controls update the active row once each");
        write (t::field (0, t::Remove), 1.0f);
        tick (view);
        check (value (m.source) == 0.0f && read (t::kRowGate) == 0.0f, "native Remove frees the exact visible route");
        write (t::Back, 1.0f);
        tick (view);
        check (r.stack.depth() == 1 && read (t::kView) == 0.0f, "native Back disarms the TARGETS surface");
        view.onHidden();
    };

    {
        pages::EnvPage page ("env1", 1);
        seed (0, 1, dest);
        r.stack.openFull (page.pageElement());
        r.stack.openDetail (EnvUi::kPageTargets);
        tick (page);
        checkRoute (page, page.ui().targetsPage(), t::Env, 1, EnvUi::kPageTargets);
    }
    {
        pages::LfoPage page ("lfo1", 1);
        seed (0, 7, dest);
        r.stack.openFull (page.pageElement());
        r.stack.openDetail (LfoUi::kPageTargets);
        tick (page);
        checkRoute (page, page.ui().targetsPage(), t::Lfo, 7, LfoUi::kPageTargets);
    }
    for (auto filterView : {ui::FilterFullScreen::kViewNote, ui::FilterFullScreen::kViewVelo})
    {
        pages::FilterPage page ("filter1", 1);
        const int source = filterView == ui::FilterFullScreen::kViewNote ? 24 : 23;
        seed (0, source, dest);
        r.plugin.overlayView.setTabView (IInstanceView::kTabViewFilter, filterView);
        r.stack.openFull (page.pageElement());
        r.stack.openDetail (FilterUi::kPageTargets);
        tick (page);
        checkRoute (page, page.ui().targetsPage(), t::Filter, source, FilterUi::kPageTargets);
    }
    {
        pages::MacrosPage page ("macros", 1);
        const int source = uibind::macroSource (1);
        seed (0, source, dest);
        r.stack.openFull (page.pageElement());
        tick (page);
        // The current card target button belongs to the native host; an overlay
        // tap deliberately passes through without opening the TARGETS stack.
        check (! tap (page, page.ui().view().cards[0].nativeTargetArea()),
               "a native Macro TARGETS button is not handled by the overlay");
        r.effect.setParameter (&r.effect, PluginInstance::nativeUiBase() + n::kMacroTarget, 1.0f);
        tick (page);
        checkRoute (page, page.ui().targetsPage(), t::Macros, source, MacroUi::kPageTargets);
    }
    {
        pages::ModPage page ("mod", 1);
        r.stack.openFull (page.pageElement());
        tick (page);
        tap (page, page.ui().matrix().board.chipRect (0));
        check (r.stack.top() == ModUi::kPageList, "the source board opens the regular destination picker");
        seed (0, 1, dest);
        r.stack.back();
        tick (page);
        checkRoute (page, page.ui().targetsPage(), t::Mod, 1, ModUi::kPageTargets);
    }
    {
        seed (0, 1, dest);
        r.stack.openFull (EnvUi::kElementEnv);
        r.stack.openDetail (EnvUi::kPageTargets);
        tick (r.view);
        checkRoute (r.view, r.view.envUi()->targetsPage(), t::PlayEnv, 1, EnvUi::kPageTargets);
        seed (0, 7, dest);
        r.stack.openFull (LfoUi::kElementLfo);
        r.stack.openDetail (LfoUi::kPageTargets);
        tick (r.view);
        checkRoute (r.view, r.view.lfoUi()->targetsPage(), t::PlayEnv, 7, LfoUi::kPageTargets);
    }

    pages::EnvPage env ("env1", 1);
    const auto& groups = uibind::DestIndex::get().choices;
    const int expectedDest = groups.front().front();
    for (int slot : {0, 1, 3, 4, 5, 6, 7})
        seed (slot, 1, expectedDest + slot + 1);
    r.stack.openFull (env.pageElement());
    r.stack.openDetail (EnvUi::kPageTargets);
    tick (env);
    write (t::field (0, t::Amount), 0.9f);
    write (t::Page, 2.0f / 3.0f);
    tick (env);
    check (env.ui().targetsPage().page.state.index == 1 && value (m.amount) == 0.0f,
           "paging wins over a queued edit on the prior window");
    write (t::Add, 1.0f);
    tick (env);
    check (read (t::kView) == 0.0f && r.stack.top() == EnvUi::kPageList,
           "native ADD suspends TARGETS while the original grouped chooser owns input");
    tap (env, env.ui().list().rowRect (0));
    tap (env, env.ui().list().rowRect (0));
    const auto reused = synthyboy::modSlotParams (2);
    check (r.stack.top() == EnvUi::kPageTargets && read (t::kView) == static_cast<float> (t::Env) / t::kLastFrame
           && env.ui().targetsPage().page.state.index == 0 && env.ui().targetsPage().activeRow() == 2
           && r.plugin.nativeModTargets.at (t::field (2, t::Amount)).cell().target == reused.amount
           && value (reused.source) == 1.0f && value (reused.destination) == expectedDest,
           "a sparse matrix reuses slot 3, opens its real page and selects its row");
    const auto saved = r.save();
    write (t::field (2, t::Amount), 0.1f);
    check (r.restore (saved), "the VST restores a saved modulation matrix with TARGETS open");
    r.expireRestoreWindow();
    tick (env);
    check (r.save() == saved, "preset recall rejects a pending native TARGETS drag");
}
