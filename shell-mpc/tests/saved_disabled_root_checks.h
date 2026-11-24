// SynthyBoy - saved OFF roots through the MPC display controller and VST
// dispatcher. Included by play_env_lfo_host.cpp to reuse its actual shell rig.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

void testSavedDisabledNativeRoots()
{
    using namespace synthyboy_mpc::overlay;
    using synthyboy_mpc::QRouter;
    namespace u = synthyboy_mpc::nativeui;
    namespace osc = u::osc;
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
    const int base = PluginInstance::nativeUiBase();
    const auto write = [&] (int index, float norm) { r.effect.setParameter (&r.effect, index, norm); };
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto writes = [&] { return r.plugin.uiWriteCalls.load (std::memory_order_relaxed); };
    const auto automates = [&] { return r.plugin.uiAutomateCalls.load (std::memory_order_relaxed); };
    const auto tick = [&] (int count = 6)
    {
        for (int n = 0; n < count; ++n)
        {
            controller.tick (r.now += 25.0);
            r.plugin.qPoll (&r.effect);
            r.plugin.uiPoll (&r.effect);
            r.plugin.nativeUiPoll (&r.effect);
        }
    };
    const auto gesture = [&] (TouchEvent::Kind kind, synthyboy_mpc::ui::Rect box, int dy = 0)
    {
        touch.pending.push_back ({ kind, box.x + box.w / 2, box.y + box.h / 2 + dy });
        tick();
    };
    const auto sendQ = [&]
    {
        for (int q = 0; q < QRouter::kCount; ++q)
        {
            const int handle = PluginInstance::qBase() + q;
            const float current = read (handle);
            write (handle, current);
            write (handle, current < 0.5f ? 0.82f : 0.17f);
        }
    };
    const auto hasQTargets = [&]
    {
        for (int q = 0; q < QRouter::kCount; ++q)
            if (r.plugin.qBank.target (q) >= 0 || r.plugin.qBank.mode (q) == QRouter::kVirtual)
                return true;
        return false;
    };
    const auto selectOsc = [&] (int source)
    {
        write (base + u::kOscBase + osc::kSource, static_cast<float> (source - 1) / (osc::kLastSource - 1));
        tick();
    };
    const auto selectFilter = [&] (int filter)
    {
        write (base + u::kFilterValueBase + u::FilterAction, static_cast<float> (filter) / u::FilterLastAction);
        tick();
    };
    int visible = -1;
    const auto open = [&] (const char* name)
    {
        if (visible >= 0)
            frame.setView (visible, false, 90);
        const auto* tab = std::find_if (std::begin (layout::kViews), std::end (layout::kViews),
                                       [&] (const auto& v) { return std::strcmp (v.name, name) == 0; });
        if (tab == std::end (layout::kViews) || slot < 0)
            return false;
        visible = static_cast<int> (tab - std::begin (layout::kViews));
        frame.setView (visible, true, 90);
        frame.setViewInstance (visible, slot, true, 90);
        if (const auto* bg = backgroundOf (name))
            frame.setUiRect (bg->sentinelX, 110 + bg->sentinelY, 5, 5, bg->sentinelRgb, true, 90);
        tick (24);
        return controller.currentView() != nullptr && controller.currentIndex() == visible && plane.planeOn;
    };
    struct Row
    {
        const char* tab;
        const char* power;
        const char* data;
        int source;
    };
    // ABC FIN now lives in the native TUNE card and binds the engine parameter
    // directly. Exercise a visible native settings proxy (DETUNE) here so the
    // queued-handle/OFF/restore regression tests the actual bank revision gate.
    constexpr Row oscillators[] {
        { "oscsub", "sub.on", "sub.phase", 1 },
        { "osca", "osc1.on", "osc1.unison.detune", 2 },
        { "oscb", "osc2.on", "osc2.unison.detune", 3 },
        { "oscc", "osc3.on", "osc3.unison.detune", 4 },
        { "oscnoise", "noise.on", "noise.filter", 5 },
    };
    const auto setting = [&] (int field)
    {
        return base + (field < osc::kSettings ? u::kOscSettings + field : u::kOscAbcSettings + field - osc::kSettings);
    };
    const auto exercise = [&] (const Row& row, int powerHandle, int dataHandle, synthyboy_mpc::ui::Rect rect,
                              int pageOpen, int routing, const auto& stillOnRoot, const auto& navigate)
    {
        const auto require = [&] (bool value, const char* description)
        {
            check (value, (std::string (row.power) + ": " + description).c_str());
        };
        const int power = target (row.power);
        const int data = target (row.data);
        if (power < 0 || data < 0 || dataHandle < base || rect.empty())
        {
            require (false, "report an enabled native data handle and its real gesture rectangle");
            return;
        }
        const auto old = read (data);
        const float next = old < 0.5f ? 0.83f : 0.18f;
        require (hasQTargets() && std::fabs (read (dataHandle) - old) < 1.0e-4f,
                 "the real powered page reports the stored value and active Q bindings");
        write (power, 0.0f);
        const auto saved = r.save();
        write (power, 1.0f);
        tick();
        const auto before = writes();
        const auto beforeAutomates = automates();
        const int beforeEngineReports = engineReports;
        const auto beforeQ = r.plugin.qBank.writeCount();
        const auto events = controller.touchEvents();
        gesture (TouchEvent::Down, rect);
        write (dataHandle, next);
        require (! saved.empty() && r.restore (saved), "restore the saved OFF state while touch and native value are pending");
        r.expireRestoreWindow();
        write (dataHandle, next);
        require (r.save() == saved, "the restored state is exact before any Q input");
        // The display has not had a chance to publish its OFF routes.
        sendQ();
        require (r.save() == saved, "the previous powered Q map is rejected before the first display tick");
        gesture (TouchEvent::Up, rect);
        require (! hasQTargets() && read (power) == 0.0f && stillOnRoot(),
                 "recalling OFF disarms the prior editor and its Q bindings");
        gesture (TouchEvent::Down, rect);
        gesture (TouchEvent::Move, rect, -36);
        write (dataHandle, old);   // A visible-value echo also has to be free of side effects.
        write (dataHandle, next);
        if (routing >= 0)
            write (routing, 1.0f);
        if (pageOpen >= 0)
            write (pageOpen, 1.0f);
        tick();
        gesture (TouchEvent::Up, rect, -36);
        gesture (TouchEvent::Down, rect);
        tick (24);   // Cross the modulation long-press threshold while OFF.
        require (stillOnRoot(), "a stationary press on an OFF control opens no editor");
        gesture (TouchEvent::Up, rect);
        sendQ();
        tick();
        require (stillOnRoot() && controller.touchEvents() == events + 7 && r.plugin.qBank.writeCount() == beforeQ + 4 * QRouter::kCount,
                 "the display receives every gesture and Q callback without opening an editor");
        navigate();
        require (stillOnRoot() && read (power) == 0.0f && read (data) == old && r.save() == saved
                 && writes() == before && automates() == beforeAutomates && engineReports == beforeEngineReports,
                 "OFF gestures, navigation and echoes leave byte-identical project state and zero UI/engine sends");
        write (powerHandle, 1.0f);
        tick();
        require (read (power) == 1.0f && read (data) == old && hasQTargets() && writes() == before + 1,
                 "the native POWER action restores the same settings and active Q bindings exactly once");
        write (dataHandle, old);
        tick();
        require (writes() == before + 1, "the newly armed stock widget ignores the stored-value echo");
        write (dataHandle, next);
        tick();
        require (writes() == before + 2 && std::fabs (read (data) - next) < 1.0e-4f && r.save() != saved,
                 "the newly armed stock widget makes one real write to the selected module");
        if (data == target ("filter2.cutoff"))
        {
            const auto savedOn = r.save();
            require (r.restore (savedOn), "an ON-to-ON recall retains the same Q map");
            const auto firstRevision = r.plugin.overlayView.qBindingRevision();
            require (r.restore (savedOn), "a second ON recall keeps the view and cutoff target");
            r.expireRestoreWindow();
            r.plugin.overlayView.finishQBindingUpdate (firstRevision);
            sendQ();
            require (! r.plugin.qBindingsReady() && r.save() == savedOn,
                     "an older display acknowledgment cannot reinstate the same view's Q map");

            {
                auto firstLoad = std::make_unique<PluginInstance::QBindingRestore> (r.plugin);
                const PluginInstance::QBindingRestore secondLoad (r.plugin);
                firstLoad.reset();
                tick();
                require (! r.plugin.qBindingsReady(), "completing one overlapping restore cannot arm Q during the other");
            }
            require (! r.plugin.qBindingsReady(), "the last restore also waits for a fresh display update");
            tick();
            int cutoffQ = 0;
            while (cutoffQ < QRouter::kCount && r.plugin.qBank.target (cutoffQ) != data)
                ++cutoffQ;
            require (cutoffQ < QRouter::kCount, "the refreshed ON map includes this cutoff's own Q proxy");
            if (cutoffQ < QRouter::kCount)
            {
                const auto handle = PluginInstance::qBase() + cutoffQ;
                write (handle, read (handle));
                require (r.save() == savedOn, "a matching Q report has no effect on the recalled project");
                write (handle, old);
                require (std::fabs (read (data) - old) < 1.0e-4f && r.save() != savedOn,
                         "the first fresh Q turn after the refresh writes its own cutoff again");
            }
        }
        write (power, 0.0f);
        tick();
    };

    for (const auto& row : oscillators)
        write (target (row.power), 0.0f);
    write (target ("filter1.on"), 0.0f);
    write (target ("filter2.on"), 0.0f);
    for (const auto& row : oscillators)
    {
        const auto opened = open (row.tab);
        auto* view = dynamic_cast<pages::OscPage*> (const_cast<IView*> (controller.currentView()));
        check (opened && view != nullptr, "the standalone oscillator's actual factory root opens");
        if (! opened || view == nullptr)
            continue;
        write (target (row.power), 1.0f);
        tick();
        const int data = target (row.data);
        const auto items = view->ui().ui().nativeSettingsItems();
        const auto field = std::find_if (items.begin(), items.end(), [&] (const auto* item) { return item != nullptr && item->param == data; });
        const auto root = [&] { return controller.uiState (slot).depth() == 1 && view->ui().selection() == row.source; };
        const auto navigate = [&] {
            const int other = row.source % osc::kLastSource + 1;
            selectOsc (other);
            check (controller.uiState (slot).depth() == 1 && view->ui().selection() == other
                   && r.plugin.nativeOsc.snapshot().visibility() == osc::kRootOff,
                   "standalone OSC source choice stays usable while both sources are OFF");
            selectOsc (row.source);
        };
        const int handle = field == items.end() ? -1 : setting (static_cast<int> (field - items.begin()));
        exercise (row, base + u::kOscPower + row.source - 1, handle,
                  field == items.end() ? synthyboy_mpc::ui::Rect {} : (*field)->c->rect(),
                  base + u::kOscDetail + u::oscdetail::kNavigation, base + u::kOscRoutingBySource + row.source - 1, root, navigate);
    }
    const auto opened = open ("filter1");
    auto* view = dynamic_cast<pages::FilterPage*> (const_cast<IView*> (controller.currentView()));
    check (opened && view != nullptr, "the standalone Filter's actual factory root opens");
    if (opened && view != nullptr)
    {
        for (const Row row : { Row { "filter1", "filter1.on", "filter1.cutoff", 1 }, Row { "filter1", "filter2.on", "filter2.cutoff", 2 } })
        {
            selectFilter (row.source);
            write (target (row.power), 1.0f);
            tick();
            const auto root = [&] { return controller.uiState (slot).depth() == 1 && view->ui().nativeViewFrame() == row.source; };
            const auto navigate = [&] {
                selectFilter (3 - row.source);
                check (view->ui().nativeViewFrame() == 3 - row.source, "standalone Filter F1/F2 choice stays usable with both filters OFF");
                selectFilter (row.source);
            };
            exercise (row, base + u::kFilterValueBase + u::FilterPower, base + u::kFilterValueBase + u::FilterCutoff,
                      view->ui().full().cutoff.rect(), -1, -1, root, navigate);
        }
    }
    namespace c = u::fxchain;
    const auto& delay = synthyboy::fxInstanceKeys (1);
    const auto setPlain = [&] (int index, float value)
    {
        write (index, synthyboy::toNormalised (synthyboy::paramTable()[index], value));
    };
    setPlain (delay.type, synthyboy::kFxTypeDelay);
    setPlain (delay.order, 1.0f);
    write (delay.on, 1.0f);
    const auto openedFx = open ("fx");
    auto* fx = dynamic_cast<pages::FxPage*> (const_cast<IView*> (controller.currentView()));
    check (openedFx && fx != nullptr, "the actual standalone FX rack opens");
    if (openedFx && fx != nullptr)
    {
        const int chain = base + u::kFxChain;
        int card = 0;
        while (card < c::kSlots && r.plugin.nativeFxChain.card (card).kind != synthyboy::kFxTypeDelay)
            ++card;
        check (card < c::kSlots, "the powered Delay has a native chain card");
        if (card < c::kSlots)
        {
            write (chain + c::Select + card, 1.0f);
            tick();
            auto& editor = fx->ui().screen().slotEditor;
            const int feedback = editor.controlOfKey ("delay.feedback");
            check (feedback >= 0, "the Delay card opens its real feedback control");
            if (feedback >= 0)
            {
                const int data = delay.p[feedback];
                int field = 0;
                while (field < u::kFxCells && r.plugin.nativeFxCells.at (field).cell().target != data)
                    ++field;
                const int handle = field == u::kFxCells ? -1 : base + u::kFxValueBase + field;
                const auto root = [&] {
                    return controller.uiState (slot).depth() == 1
                        && r.plugin.nativeFxHeader.state().item == synthyboy::kNumFxSlots + 1;
                };
                exercise ({ "fx", synthyboy::paramTable()[delay.on].key, synthyboy::paramTable()[data].key, 0 },
                          chain + c::Power + card, handle, editor.knobs[feedback].rect(), -1, -1, root, [] {});
            }
        }
    }
    controller.detach (&r.plugin.overlayView);
}
