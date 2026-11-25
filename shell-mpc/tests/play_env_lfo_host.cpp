// SynthyBoy - native MPC controls through the actual VST dispatcher and page views.
// Compile in one translation unit to run the real shell with a deterministic
// display tick and a test host. No device or exported test entry point is needed.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#define SYNTHYBOY_MPC_OVERLAY 1
#define SYNTHYBOY_MPC_LFO_LIVE 1
#define SYNTHYBOY_MPC_SPECTRUM 1
#define SYNTHYBOY_MPC_PAD_PRESSURE 1
#define SYNTHYBOY_MPC_DEBUG_HOOKS 1
#include "../synthyboy_mpc.cpp"
#include "../overlay/tests/fakes.h"

namespace
{
namespace n = synthyboy_mpc::nativeui::playenvlfo;
using synthyboy_mpc::overlay::PlayView;
using synthyboy_mpc::overlay::UiStack;
using synthyboy_mpc::overlay::ViewInput;

int failures = 0;
int engineReports = 0;
int standardSyncReports = 0;
int envTargetReports = 0;
int displayRefreshes = 0;

void check (bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf (stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

int block() { return PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kPlayEnvLfo; }
int parameter (int field)
{
    return field == n::StandardSync ? PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kPlayLfoStandardSync : block() + 1 + field;
}

VstIntPtr callback (AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float value)
{
    if (opcode == audioMasterUpdateDisplay)
        ++displayRefreshes;
    if (opcode == audioMasterVersion)
        return 2400;
    if (opcode == audioMasterAutomate)
    {
        if (index < static_cast<int> (synthyboy::paramCount()))
            ++engineReports;
        if (index == PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kEnvFields + synthyboy_mpc::nativeui::env::Targets)
            ++envTargetReports;
        if (index == parameter (n::StandardSync))
            ++standardSyncReports;
        if (index == PluginInstance::playBase() + 2 || index == parameter (n::Choose))
            effect->setParameter (effect, index, value);
    }
    return 0;
}

struct Rig
{
    PluginInstance plugin;
    AEffect effect {};
    UiStack stack;
    PlayView view { 3, 10.0 };
    double now = 1000;

    Rig()
    {
        effect.object = &plugin;
        effect.setParameter = pluginSetParameter;
        effect.getParameter = pluginGetParameter;
        effect.dispatcher = pluginDispatcher;
        effect.numParams = PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kCount;
        plugin.audioMaster = callback;
        plugin.hostKnows.store (true, std::memory_order_release);
    }

    void tick()
    {
        ViewInput input;
        input.instance = input.io = &plugin.overlayView;
        input.ui = &stack;
        input.nowMs = now += 75.0;
        view.update (input);
        plugin.playPoll (&effect, PluginInstance::playBase());
        plugin.nativeUiPoll (&effect);
    }

    void popup (int field)
    {
        const auto* found = std::find (std::begin (n::kPopupFields), std::end (n::kPopupFields), field);
        view.setNativeActionPopup (found == std::end (n::kPopupFields) ? -1
                                   : n::kFirstPopup + static_cast<int> (found - std::begin (n::kPopupFields)));
    }
    void write (int field, float value) { effect.setParameter (&effect, parameter (field), value); }
    float read (int field) { return effect.getParameter (&effect, parameter (field)); }

    std::string save()
    {
        void* bytes = nullptr;
        const auto size = effect.dispatcher (&effect, effGetChunk, 0, 0, &bytes, 0);
        return size > 0 && bytes != nullptr ? std::string (static_cast<const char*> (bytes), static_cast<size_t> (size)) : std::string ();
    }
    bool restore (const std::string& chunk)
    {
        return effect.dispatcher (&effect, effSetChunk, 0, static_cast<VstIntPtr> (chunk.size()),
                                  const_cast<char*> (chunk.data()), 0) == 1;
    }
    void expireRestoreWindow()
    {
        plugin.restoreSinceMs.store (PluginInstance::steadyMs() - PluginInstance::kRestoreWindowMs - 1,
                                     std::memory_order_relaxed);
    }

    void legacyChoice (int lfo)
    {
        effect.setParameter (&effect, PluginInstance::playBase() + 2, synthyboy_mpc::play::selectorNormalised (2, lfo));
    }
};

int target (const char* key)
{
    const auto* info = synthyboy::findParamByKey (key);
    return info == nullptr ? -1 : synthyboy::findParamIndex (info->id);
}

void testEnvTargetCount ()
{
    using namespace synthyboy_mpc::overlay;
    namespace native = synthyboy_mpc::nativeui;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    pages::EnvPage page ("env1", 1);
    UiStack stack;
    stack.openFull (page.pageElement());
    const int button = PluginInstance::nativeUiBase() + native::kEnvFields + native::env::Targets;
    const auto tick = [&]
    {
        ViewInput input;
        input.instance = input.io = &r.plugin.overlayView;
        input.ui = &stack;
        input.nowMs = r.now += 150.0;
        page.update (input);
        r.plugin.nativeUiPoll (&r.effect);
    };
    const auto shown = [&]
    {
        char display[64] = {};
        r.effect.dispatcher (&r.effect, effGetParamDisplay, button, 0, display, 0.0f);
        return std::string (display);
    };
    const auto set = [&] (int index, float value)
    {
        const auto& info = synthyboy::paramTable()[index];
        r.effect.setParameter (&r.effect, index, synthyboy::toNormalised (info, value));
    };
    const int destination = synthyboy::modDestinationChoiceOf (target ("filter1.cutoff"));
    for (int i = 0; i < 3; ++i)
    {
        const auto slot = synthyboy::modSlotParams (i);
        set (slot.source, uibind::envSource (i == 2 ? 2 : 1));
        set (slot.destination, destination);
        set (slot.amount, 30.0f);
    }

    tick();
    check (shown() == "2" && r.effect.getParameter (&r.effect, button) == 0.0f,
           "ENV TARGETS displays the selected envelope's count while its action stays unlatched");
    r.plugin.overlayView.setPlaySelection (1, 2);
    tick();
    check (shown() == "1", "changing the selected envelope changes the native TARGETS count");
    envTargetReports = 0;
    set (synthyboy::modSlotParams (2).source, 0.0f);
    tick();
    check (shown() == "0" && envTargetReports == 1, "editing the selected route refreshes the count once without rebinding the button");
    tick();
    check (envTargetReports == 1, "idle ticks do not repeatedly report the count");

    const auto chunk = r.save();
    r.effect.setParameter (&r.effect, button, 1.0f);
    tick();
    check (stack.top() == EnvUi::kPageTargets && shown().empty() && r.save() == chunk,
           "the stock TARGETS action opens routing once, retires the count and does not write project state");
}

void testStandaloneOscDetailVst ()
{
    using namespace synthyboy_mpc::overlay;
    namespace detail = synthyboy_mpc::nativeui::oscdetail;
    struct Touch final : ITouchInput
    {
        std::vector<TouchEvent> events;
        void configure (int, int, int) override {}
        int poll (const std::function<void (const TouchEvent&)>& receive) override
        {
            for (const auto& event : events)
                receive (event);
            const auto count = static_cast<int> (events.size());
            events.clear();
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
        [] (const auto& view) { return std::string_view (view.name) == "osca"; });
    if (slot < 0 || tab == std::end (layout::kViews))
    {
        check (false, "the real dispatcher fixture found Osc A and attached the instance");
        return;
    }
    const int osc = static_cast<int> (tab - std::begin (layout::kViews));
    auto tick = [&] (int ms)
    {
        for (int elapsed = 0; elapsed < ms; elapsed += 10)
        {
            controller.tick (r.now += 10.0);
            r.plugin.nativeUiPoll (&r.effect);
        }
    };
    auto write = [&] (int index, float normalized) { r.effect.setParameter (&r.effect, index, normalized); };
    auto engine = [&] (int index, float plain) { write (index, synthyboy::toNormalised (synthyboy::paramTable()[index], plain)); };
    auto uiWrites = [&] { return r.plugin.uiWriteCalls.load (std::memory_order_relaxed); };
    const int base = PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kOscDetail;
    const int blend = target ("osc1.unison.blend");
    const auto route = synthyboy::modSlotParams (0);
    const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (blend));
    if (blend < 0 || destination == 0)
    {
        check (false, "unison blend has a real modulation destination");
        return;
    }
    r.expireRestoreWindow();
    engine (target ("osc1.on"), 1.0f);
    engine (target ("osc1.type"), 0.0f);
    engine (route.source, 1.0f);
    engine (route.destination, static_cast<float> (destination));
    frame.setView (osc, true, 90);
    frame.setViewInstance (osc, slot, true, 90);
    tick (500);
    write (base + detail::kNavigation, 1.0f);
    tick (200);

    const auto* view = dynamic_cast<const PageView<OscPageUi>*> (controller.viewByName ("osca"));
    int field = -1;
    for (int i = 0; i < detail::kCells; ++i)
        if (r.plugin.nativeOscDetail.at (i).cell().target == blend)
            field = i;
    if (view == nullptr || field < 0 || controller.uiState (slot).top() != oscspec::kMore)
    {
        check (false, "the actual MORE dispatch exposes Unison Blend on the standalone control surface");
        return;
    }
    const auto rect = view->ui().ui().nativeDetailRect (field);
    auto emit = [&] (TouchEvent::Kind kind, int dy = 0)
    {
        TouchEvent event;
        event.kind = kind;
        event.x = rect.x + rect.w / 2;
        event.y = rect.y + rect.h / 2 + dy;
        touch.events.push_back (event);
        tick (30);
    };
    const int parameter = base + detail::kValue + field;
    const auto touches = controller.touchTaken();
    const auto events = controller.touchEvents();
    const auto oldWrites = uiWrites();
    const float before = r.effect.getParameter (&r.effect, blend);
    const float value = before < 0.5f ? 0.78f : 0.23f;
    emit (TouchEvent::Down);
    emit (TouchEvent::Move, -24);
    write (parameter, value); // The stock dial submits the drag value through VST.
    emit (TouchEvent::Up, -24);
    tick (700);
    check (controller.touchTaken() == touches && controller.touchEvents() == events + 3
           && controller.uiState (slot).top() == oscspec::kMore && uiWrites() == oldWrites + 1
           && std::abs (r.effect.getParameter (&r.effect, blend) - value) < 0.0001f,
           "the actual touch poll leaves a short OSC drag with the stock dial and VST writes exactly once");

    const auto beforeHold = uiWrites();
    emit (TouchEvent::Down);
    tick (650);
    check (controller.touchTaken() == touches && controller.uiState (slot).top() == OscUi::kPageMods
           && uiWrites() == beforeHold && view->ui().qTargets()[0] == route.amount,
           "a stationary touch opens modulation for that exact dial with no parameter write");
    emit (TouchEvent::Up);

    const int modulationsBack = PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kModTargets
        + synthyboy_mpc::nativeui::targets::Back;
    write (modulationsBack, 1.0f);
    tick (80);
    check (controller.uiState (slot).top() == oscspec::kMore && uiWrites() == beforeHold,
           "native modulation BACK returns to OSC MORE without an engine write");
    engine (target ("osc1.type"), 1.0f);
    tick (150);
    if (controller.uiState (slot).top() == 0)
    {
        write (base + detail::kNavigation, 1.0f);
        tick (150);
    }
    const int warp = target ("osc1.warp2.type");
    int warpField = -1;
    for (int i = 0; i < detail::kCells; ++i)
        if (r.plugin.nativeOscDetail.at (i).cell().target == warp)
            warpField = i;
    if (warpField < 0)
    {
        check (false, "the same real OSC surface offers sample Warp 2 on MORE");
        controller.detach (&r.plugin.overlayView);
        return;
    }
    const auto menuCell = view->ui().ui().nativeDetailRect (warpField);
    const int menu = view->ui().ui().nativeDetailMenu (warpField);
    const int menuParam = base + detail::kMenusBase + menu;
    auto modal = [&] (bool show)
    {
        frame.setView (osc, ! show, 90);
        if (! show)
            frame.setViewInstance (osc, slot, true, 90);
        for (const auto& mark : kNativeParameterModalMarkers)
            frame.setUiRect (mark.rect.x, mark.rect.y, mark.rect.w, mark.rect.h, mark.rgb, show, 90);
        tick (250);
    };
    const auto pressMenu = [&]
    {
        touch.events.push_back ({TouchEvent::Down, menuCell.x + menuCell.w / 2, menuCell.y + menuCell.h / 2});
        tick (30);
        modal (true);
        touch.events.push_back ({TouchEvent::Up, menuCell.x + menuCell.w / 2, menuCell.y + menuCell.h / 2});
        tick (30);
    };
    const auto beforeMenu = uiWrites();
    pressMenu();
    check (! plane.planeOn && controller.state() == "hidden: native OSC editor", "the actual stock-menu markers suspend the OSC page");
    write (menuParam, 0.25f);
    modal (false);
    check (plane.planeOn && uiWrites() == beforeMenu + 1 && r.effect.getParameter (&r.effect, warp) > 0.0f
           && controller.uiState (slot).top() == oscspec::kMore,
           "the stock Warp 2 menu writes once through the VST dispatcher and restores MORE");
    pressMenu();
    modal (false);
    write (menuParam, 0.90f);
    tick (120);
    check (uiWrites() == beforeMenu + 1 && controller.uiState (slot).top() == oscspec::kMore,
           "Cancel restores MORE and a late Warp 2 callback is ignored");
    controller.detach (&r.plugin.overlayView);
}

void testCompactOscVst ()
{
    namespace o = synthyboy_mpc::nativeui::playoscui;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    const int oscBase = PluginInstance::nativeUiBase() + synthyboy_mpc::nativeui::kPlayOsc;
    const auto address = [&] (int source, int field) { return oscBase + 1 + (source - 1) * o::kFields + field; };
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto write = [&] (int index, float norm) { r.effect.setParameter (&r.effect, index, norm); };
    const auto select = [&] (int source) {
        write (PluginInstance::playBase(), synthyboy_mpc::play::selectorNormalised (0, source));
    };
    const auto near = [] (float a, float b) { return std::fabs (a - b) < 1.0e-5f; };

    const int aOn = target ("osc1.on"), bOn = target ("osc2.on");
    const int aPan = target ("osc1.pan"), bPan = target ("osc2.pan");
    const int aLevel = target ("osc1.level"), bLevel = target ("osc2.level"), aRoute = target ("osc1.destination");
    write (aOn, 1.0f);
    write (bOn, 1.0f);
    select (2);
    r.tick();
    const float bPanBefore = read (bPan);
    const auto initialWrites = r.plugin.uiWriteCalls.load (std::memory_order_relaxed);
    write (address (2, o::Pan), 0.30f);
    r.tick();
    check (near (read (aPan), 0.30f) && near (read (address (2, o::Pan)), 0.30f)
           && near (read (bPan), bPanBefore)
           && r.plugin.uiWriteCalls.load (std::memory_order_relaxed) == initialWrites + 1,
           "the actual compact OSC A PAN handle writes its own engine parameter once");

    const auto levelBefore = read (aLevel);
    write (address (2, o::Power), 0.0f);
    write (address (2, o::Level), 0.85f);
    r.tick();
    const auto disabledWrites = r.plugin.uiWriteCalls.load (std::memory_order_relaxed);
    write (address (2, o::Level), 0.80f);
    write (address (2, o::Expand), 1.0f);
    r.tick();
    check (read (aOn) == 0.0f && near (read (aLevel), levelBefore)
           && near (read (address (2, o::Level)), levelBefore) && r.stack.isMain()
           && r.plugin.uiWriteCalls.load (std::memory_order_relaxed) == disabledWrites,
           "OFF retains the actual VST readout and disarms queued and subsequent Level and Expand");

    write (address (2, o::Power), 1.0f);
    r.tick();
    write (address (2, o::Pan), 0.15f);
    select (3);
    write (address (2, o::Pan), 0.10f);
    r.tick();
    check (near (read (aPan), 0.30f) && near (read (bPan), bPanBefore) && read (address (2, o::Pan)) == 0.0f,
           "a queued and late OSC A callback cannot move a dial on OSC B");
    write (address (3, o::Pan), 0.65f);
    r.tick();
    check (near (read (bPan), 0.65f) && near (read (aPan), 0.30f),
           "the real OSC B endpoint updates only its own dial");

    const auto bLevelBefore = read (bLevel);
    write (address (3, o::Level), 0.20f);
    r.view.onHidden();
    write (address (3, o::Level), 0.10f);
    r.tick();
    check (near (read (bLevel), bLevelBefore) && near (read (address (3, o::Level)), bLevelBefore),
           "leaving and reopening the actual Play view discards old writes and restores the current Level");
    write (address (3, o::Level), 0.75f);
    r.tick();
    check (near (read (bLevel), 0.75f), "a fresh OSC B endpoint writes after the view reopens");

    select (2);
    r.tick();
    const auto routeBefore = read (aRoute);
    r.view.setNativeActionPopup (0);   // compact destination
    write (address (2, o::Route), 2.0f / o::kRoutingDetails);
    select (3);
    select (2);
    write (address (2, o::Route), 2.0f / o::kRoutingDetails);
    r.view.setNativeActionPopup (-1);
    r.tick();
    check (near (read (aRoute), routeBefore), "the actual VST dispatcher rejects an old A–B–A routing callback");
    r.view.setNativeActionPopup (0);
    const float newRoute = near (routeBefore, 2.0f / 6.0f) ? 3.0f : 2.0f;
    write (address (2, o::Route), newRoute / o::kRoutingDetails);
    r.view.setNativeActionPopup (-1);
    r.tick();
    check (near (read (aRoute), newRoute / 6.0f), "a fresh marked route writes the canonical destination through VST");

    constexpr const char* powerKeys[] = { "sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on" };
    static_assert (std::size (powerKeys) == o::kSources);
    for (int source = 1; source <= o::kSources; ++source)
    {
        const int power = target (powerKeys[source - 1]);
        select (source);
        write (power, 0.0f);
        r.tick();
        const auto savedOff = r.save();
        write (address (source, o::Power), 1.0f);
        r.tick();
        check (read (power) == 1.0f && ! savedOff.empty() && r.restore (savedOff),
               "a saved disabled oscillator restores after an intervening powered-on state");
        r.expireRestoreWindow();
        r.tick();
        const auto restoredWrites = r.plugin.uiWriteCalls.load (std::memory_order_relaxed);
        const int restoredReports = engineReports;
        for (int field : { o::First, o::Second, o::Pan, o::Level, o::Expand, o::Route })
            write (address (source, field), 1.0f);
        r.tick();
        check (r.stack.isMain() && read (power) == 0.0f && r.save() == savedOff
               && r.plugin.uiWriteCalls.load (std::memory_order_relaxed) == restoredWrites
               && engineReports == restoredReports,
               "after the restore guard expires, disabled OSC inputs preserve the complete saved chunk and report no edits");
    }
}


void testCompactExpandVst()
{
    using namespace synthyboy_mpc::overlay;
    namespace u = synthyboy_mpc::nativeui;
    namespace o = u::playoscui;
    namespace x = u::expandedplay;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    const int play = PluginInstance::playBase();
    r.effect.setParameter (&r.effect, target ("osc1.on"), 1.0f);
    r.effect.setParameter (&r.effect, play, synthyboy_mpc::play::selectorNormalised (0, 2));
    r.effect.setParameter (&r.effect, play + 1, synthyboy_mpc::play::selectorNormalised (1, 3));
    r.legacyChoice (4);
    r.effect.setParameter (&r.effect, target ("lfo4.sync"), 0.0f);
    r.effect.setParameter (&r.effect, target ("rlfo2.sync"), 0.0f);
    r.tick();
    const int base = PluginInstance::nativeUiBase();
    const int oscView = base + u::kPlayOsc;
    const auto expandedFrame = [&] { return r.effect.getParameter (&r.effect, base + u::kPlayExpanded); };
    const auto oscFrame = [&] { return r.effect.getParameter (&r.effect, oscView); };
    const int oscPan = oscView + 1 + o::kFields + o::Pan;   // source 2 is Osc A
    const float oldPan = r.effect.getParameter (&r.effect, oscPan);
    const auto compactFrame = [&] { return r.effect.getParameter (&r.effect, block()); };
    const auto pixel = [] (const std::vector<uint32_t>& picture, synthyboy_mpc::ui::Rect area) {
        return picture[static_cast<size_t> ((area.y + area.h / 2) * 1280 + area.x + area.w / 2)];
    };
    std::vector<uint32_t> compact (1280 * 800), expanded (compact.size()), reopened (compact.size());
    struct Graph { bool envelope, random; const char* qTarget; };
    const Graph graphs[] {
        { true, false, "env3.attack" },
        { false, false, "lfo4.rate" },
        { false, true, "rlfo2.rate" },
    };
    const auto currentQ = [&] {
        std::array<int, synthyboy_mpc::QRouter::kCount> targets;
        for (int q = 0; q < static_cast<int> (targets.size()); ++q)
            targets[q] = r.plugin.qBank.target (q);
        return targets;
    };
    for (const auto& graph : graphs)
    {
        if (graph.random)
        {
            r.popup (n::Choose);
            r.write (n::Choose, n::choiceValue (12)); // RND 2
            r.popup (-1);
            r.tick();
        }
        const bool envelope = graph.envelope;
        auto& env = *r.view.envUi();
        auto& lfo = *r.view.lfoUi();
        const int field = envelope ? n::EnvExpand : n::LfoExpand;
        const auto expand = PlayEnvLfo::rect (env, lfo, field);
        const auto smallGraph = envelope ? env.compact().graphRect() : lfo.compact().displayRect();
        r.view.render (compact.data(), 1280, 800);
        const float oldCompact = compactFrame(), oldOsc = oscFrame();
        const auto compactQ = currentQ();
        const auto engineWrites = r.plugin.uiWriteCalls.load (std::memory_order_relaxed);
        check (oldCompact > 0.0f && oldOsc > 0.0f && pixel (compact, expand) == 0u && pixel (compact, smallGraph) != 0u,
               "the compact graph has an active VST Expand aperture");

        r.write (field, 1.0f);
        r.tick();
        r.view.render (expanded.data(), 1280, 800);
        const auto close = envelope ? env.full().close.rect() : lfo.full().close.rect();
        const auto fullGraph = envelope ? env.full().graph.rect() : lfo.full().displayRect();
        const auto expandedQ = currentQ();
        check (r.stack.depth() == 1 && r.stack.element() == (envelope ? EnvUi::kElementEnv : LfoUi::kElementLfo)
                   && env.selection() == 3 && lfo.selection() == (graph.random ? 2 : 4) && lfo.random() == graph.random
                   && std::find (expandedQ.begin(), expandedQ.end(), target (graph.qTarget)) != expandedQ.end()
                   && fullGraph.w > 2 * smallGraph.w && pixel (expanded, fullGraph) != 0u
                   && pixel (expanded, close) == 0u && expanded != compact
                   && expandedFrame() == static_cast<float> (envelope ? x::EnvRoot : x::LfoRoot) / x::LastFrame,
               "the actual VST Expand draws the selected full-size ENV, LFO or RND graph and exposes native CLOSE");
        check (compactFrame() == 0.0f && oscFrame() == 0.0f && r.plugin.uiFullOpen.load (std::memory_order_relaxed),
               "the first expanded frame disarms the compact ENV, LFO and OSC host banks");
        r.write (envelope ? n::LfoExpand : n::EnvExpand, 1.0f);
        r.effect.setParameter (&r.effect, oscPan, oldPan > 0.5f ? 0.2f : 0.8f);

        const int cx = close.x + close.w / 2, cy = close.y + close.h / 2;
        const bool down = r.view.onTouch ({ TouchEvent::Down, cx, cy }, r.stack, r.plugin.overlayView, r.now);
        const bool up = r.view.onTouch ({ TouchEvent::Up, cx, cy }, r.stack, r.plugin.overlayView, r.now + 30.0);
        check (! down && ! up && r.stack.depth() == 1, "expanded CLOSE belongs to the actual SDK control");
        const int closeIndex = base + u::kPlayExpanded + 1 + (envelope ? x::CloseEnv : x::CloseLfo);
        r.effect.setParameter (&r.effect, closeIndex, 1.0f);
        r.tick();
        r.view.render (reopened.data(), 1280, 800);
        check (r.stack.isMain() && expandedFrame() == 0.0f && compactFrame() == oldCompact && oscFrame() == oldOsc
                   && ! r.plugin.uiFullOpen.load (std::memory_order_relaxed) && currentQ() == compactQ
                   && pixel (reopened, expand) == 0u
                   && pixel (reopened, smallGraph) != 0u && reopened != expanded,
               "CLOSE redraws the compact graph and restores its banks without replaying a hidden Expand callback");
        check (r.plugin.uiWriteCalls.load (std::memory_order_relaxed) == engineWrites
                   && r.effect.getParameter (&r.effect, oscPan) == oldPan,
               "expanding and closing changes no engine parameter");

        r.write (field, 1.0f);
        r.tick();
        const int real = target (graph.qTarget);
        const int control = base + (envelope ? u::kEnvFields : u::kLfoBase + u::lfo::kRate);
        const float initial = r.effect.getParameter (&r.effect, real);
        const float next = initial > 0.5f ? 0.25f : 0.75f;
        const auto saved = r.save();
        r.effect.setParameter (&r.effect, control, next);
        r.tick();
        check (r.effect.getParameter (&r.effect, real) != initial, "the shared native editor writes the selected ENV, LFO or RND");
        r.effect.setParameter (&r.effect, control, next);
        check (r.restore (saved), "the actual VST restores a preset while a full Play editor is open");
        r.expireRestoreWindow();
        r.effect.setParameter (&r.effect, control, next);
        r.tick();
        check (r.effect.getParameter (&r.effect, real) == initial && ! r.stack.isMain(),
               "preset recall keeps the expanded graph and invalidates the previous native edit");
        if (! envelope)
        {
            r.effect.setParameter (&r.effect, base + u::kLfoBase + u::lfo::kAction, 1.0f);
            r.tick();
            if (graph.random)
                check (r.stack.depth() == 1, "RND has no editable shape");
            else
            {
                check (r.stack.top() == LfoUi::kPageEditor && r.effect.getParameter (&r.effect, base + u::kLfoEditor) > 0.0f,
                       "the Play LFO opens the same native shape toolbar and keeps its custom graph");
                r.stack.back();
                r.tick();
            }
        }
        r.effect.setParameter (&r.effect, closeIndex, 1.0f);
        r.tick();
        check (r.stack.isMain() && currentQ() == compactQ, "closing the recalled editor restores the original compact Q routes");
    }
}

void testExpandedOscVst()
{
    using namespace synthyboy_mpc::overlay;
    namespace u = synthyboy_mpc::nativeui;
    namespace x = u::expandedplay;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    const int base = PluginInstance::nativeUiBase();
    const int play = PluginInstance::playBase();
    const int frame = base + u::kPlayExpanded;
    const int close = frame + 1 + x::CloseOsc;
    const int more = base + u::kOscDetail + u::oscdetail::kNavigation;
    const int source = base + u::kOscBase + u::osc::kSource;
    const int expand = base + u::kPlayOsc + 1 + u::playoscui::kFields + u::playoscui::Expand;
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto write = [&] (int index, float value) { r.effect.setParameter (&r.effect, index, value); };
    const auto writes = [&] { return r.plugin.uiWriteCalls.load (std::memory_order_relaxed); };
    const auto isFrame = [&] (x::Frame f) { return read (frame) == static_cast<float> (f) / x::LastFrame; };
    const auto qTargets = [&] {
        std::array<int, synthyboy_mpc::QRouter::kCount> result;
        for (int q = 0; q < static_cast<int> (result.size()); ++q)
            result[q] = r.plugin.qBank.target (q);
        return result;
    };
    const int aOn = target ("osc1.on"), bOn = target ("osc2.on");
    const int aWave = target ("osc1.wavetable"), bWave = target ("osc2.wavetable");
    write (aOn, 1.0f);
    write (bOn, 0.0f);
    const float beforeBWave = read (bWave);
    write (play, synthyboy_mpc::play::selectorNormalised (0, 2));
    write (aWave, 0.0f);
    r.tick();
    const auto originalQ = qTargets();
    const auto beforeOpen = writes();
    write (expand, 1.0f);
    r.tick();
    const auto closeRect = OscPageUi::expandedCloseRect();
    const int cx = closeRect.x + closeRect.w / 2, cy = closeRect.y + closeRect.h / 2;
    const auto display = r.view.oscUi()->displayRect();
    const auto graph = [&] {
        std::vector<uint32_t> pixels (1280 * 800);
        r.view.render (pixels.data(), 1280, 800);
        check (pixels[cy * 1280 + cx] == 0, "the enlarged OSC stock CLOSE aperture stays transparent");
        std::vector<uint32_t> crop;
        const auto plot = display.inset (14);
        for (int y = plot.y; y < plot.y + plot.h; ++y)
            crop.insert (crop.end(), pixels.begin() + (y * 1280 + plot.x), pixels.begin() + (y * 1280 + plot.x + plot.w));
        return crop;
    };
    check (isFrame (x::OscRoot) && r.plugin.uiFullOpen.load (std::memory_order_relaxed)
           && r.stack.element() == OscUi::kElementOsc && r.view.selection (0) == 2
           && writes() == beforeOpen && display.w > 400, "actual compact OSC Expand opens the shared native OSC root without editing the engine");
    const auto pictureA = graph();
    const auto a = r.save();
    const auto& info = synthyboy::paramTable()[aWave];
    write (aWave, synthyboy::toNormalised (info, 1.0f));
    r.tick();
    const auto pictureB = graph();
    const auto b = r.save();
    check (pictureA != pictureB && a != b, "each saved wavetable produces a distinct stationary expanded OSC plot");
    for (const auto& saved : {std::cref (a), std::cref (b), std::cref (a)})
    {
        write (close, 1.0f);
        write (more, 1.0f);
        check (r.restore (saved.get()), "the actual VST reloads an OSC preset with the expanded graph held open");
        r.expireRestoreWindow();
        write (close, 1.0f);
        write (more, 1.0f);
        r.tick();
        check (isFrame (x::OscRoot) && graph() == (&saved.get() == &a ? pictureA : pictureB),
               "held expanded OSC redraw matches the original stationary plot after each A/B/A restore");
    }

    write (more, 1.0f);
    r.tick();
    check (isFrame (x::OscDetail) && r.stack.top() == oscspec::kMore, "the real standalone MORE binding opens details inside Play");
    write (close, 1.0f);
    write (more + 1, 1.0f);
    r.tick();
    check (isFrame (x::OscRoot) && r.stack.depth() == 1, "detail BACK returns to the expanded root and ignores a hidden root CLOSE");

    write (source, 2.0f / (u::osc::kLastSource - 1));
    write (more, 1.0f);
    r.tick();
    check (isFrame (x::OscRoot) && r.view.selection (0) == 3 && read (play) == 0.5f && read (bOn) == 0.0f,
           "the SDK selector opens disabled OSC B without carrying the OSC A MORE action across");
    const int bPower = base + u::kOscPower + 2;
    const auto beforePower = writes();
    write (more, 1.0f);
    r.tick();
    check (isFrame (x::OscRoot) && writes() == beforePower && read (bWave) == beforeBWave,
           "the expanded OFF source rejects its native editor actions");
    write (bPower, 1.0f);
    r.tick();
    check (read (bOn) == 1.0f && writes() == beforePower + 1, "expanded OSC POWER writes its selected source exactly once");
    write (source, 1.0f / (u::osc::kLastSource - 1));
    r.tick();
    const bool down = r.view.onTouch ({ TouchEvent::Down, cx, cy }, r.stack, r.plugin.overlayView, r.now);
    const bool up = r.view.onTouch ({ TouchEvent::Up, cx, cy }, r.stack, r.plugin.overlayView, r.now + 32);
    check (! down && ! up && isFrame (x::OscRoot), "the physical expanded OSC CLOSE touch passes through to the SDK");
    write (close, 1.0f);
    r.tick();
    check (r.stack.isMain() && isFrame (x::Hidden) && ! r.plugin.uiFullOpen.load (std::memory_order_relaxed)
           && r.view.selection (0) == 2 && qTargets() == originalQ,
           "one native OSC CLOSE restores the selected compact oscillator and all compact Q targets");
}

void testMixerPeekDisplayRefresh()
{
    Rig r;
    const int before = displayRefreshes;
    r.plugin.overlayView.setNativeMixerPeekView (nativeui::MixerPeekFilter);
    r.plugin.requestProxyNames (&r.effect);
    r.plugin.requestProxyNames (&r.effect);
    const auto request = r.plugin.peekNamesRequestedAt;
    r.plugin.flushPeekProxyNames (&r.effect, request);
    check (displayRefreshes == before, "leave the host free to draw the Filter before rebuilding proxy names");
    r.plugin.overlayView.observeHostWidgetGate (true);
    r.plugin.flushPeekProxyNames (&r.effect, request);
    r.plugin.flushPeekProxyNames (&r.effect, request + 20);
    check (displayRefreshes == before + 1, "the observed modal frame releases both proxy changes once");
    r.plugin.overlayView.observeHostWidgetGate (false);
    r.plugin.requestProxyNames (&r.effect);
    r.plugin.flushPeekProxyNames (&r.effect, r.plugin.peekNamesRequestedAt + 2000);
    check (displayRefreshes == before + 2, "an unavailable primary frame still releases the metadata update");
    r.plugin.requestProxyNames (&r.effect);
    r.plugin.overlayView.setNativeMixerPeekView (nativeui::MixerBase);
    r.plugin.flushPeekProxyNames (&r.effect, r.plugin.peekNamesRequestedAt);
    check (displayRefreshes == before + 3, "leaving the Filter releases a pending refresh");
}

void testMixerPeekVst ()
{
    namespace u = synthyboy_mpc::nativeui;
    using namespace synthyboy_mpc::overlay;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    pages::MixerPage view ("mixer", 0, {});
    pages::configureNativeTab (view, "mixer");
    view.setHostBackground (false); // Stock control ownership survives a missing background sentinel.
    r.stack.openFull (MixerUi::kElementPage);
    r.plugin.overlayView.setTabView (IInstanceView::kTabViewFilter, 3);
    const int base = PluginInstance::nativeUiBase();
    const int viewParam = base + u::kMixerPeekView;
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto write = [&] (int index, float norm) { r.effect.setParameter (&r.effect, index, norm); };
    const auto writes = [&] { return r.plugin.uiWriteCalls.load (std::memory_order_relaxed); };
    const auto near = [] (float a, float b) { return std::fabs (a - b) < 1.0e-4f; };
    const auto tick = [&] {
        ViewInput in;
        in.ui = &r.stack;
        in.instance = in.io = &r.plugin.overlayView;
        in.nowMs = r.now += 75.0;
        view.update (in);
        r.plugin.nativeUiPoll (&r.effect);
    };
    const auto tap = [&] (synthyboy_mpc::ui::Rect area) {
        const int x = area.x + area.w / 2, y = area.y + area.h / 2;
        view.onTouch (TouchEvent { TouchEvent::Down, x, y }, r.stack, r.plugin.overlayView, r.now);
        view.onTouch (TouchEvent { TouchEvent::Up, x, y }, r.stack, r.plugin.overlayView, r.now + 30);
    };
    write (target ("filter1.on"), 1.0f);
    write (target ("filter2.on"), 1.0f);
    write (target ("delay.on"), 1.0f);
    write (target ("chorus.on"), 1.0f);
    write (target ("reverb.on"), 1.0f);
    write (target ("delay.rack"), 0.0f);
    write (target ("chorus.rack"), 1.0f / 9.0f);
    write (target ("reverb.rack"), 2.0f / 9.0f);
    tick();
    check (! view.ui().hostBackground() && view.ui().nativeBase(),
           "the real Mixer keeps native root ownership while supplying its foreground ground");

    const int keys[] { target ("filter1.cutoff"), target ("filter2.cutoff"), target ("delay.feedback"),
                       target ("chorus.mix"), target ("reverb.mix") };
    const int names = static_cast<int> (std::size (keys));
    std::array<int, synthyboy_mpc::QRouter::kCount> mixerQ;
    std::copy_n (view.ui().qTargets(), mixerQ.size(), mixerQ.begin());
    for (int selected = 0; selected < names; ++selected)
    {
        write (base + u::kMixerPeek, static_cast<float> (selected + 1) / names);
        tick();
        const bool filter = selected < 2;
        const float expectedView = filter ? 0.5f : 1.0f;
        check (view.ui().topPage() == MixerUi::kPagePeek && r.stack.element() == MixerUi::kElementPage
                   && read (viewParam) == expectedView,
               "the real Mixer PEEK action opens its native editor without selecting a different host tab");
        check (std::count (view.ui().qTargets(), view.ui().qTargets() + synthyboy_mpc::QRouter::kCount, keys[selected]) == 1,
               "the open PEEK Q group contains its selected engine parameter once");
        int handle = filter ? base + u::kFilterValueBase + u::FilterCutoff : -1;
        if (! filter)
            for (int slot = 0; slot < u::kFxCells; ++slot)
                if (r.plugin.nativeFxCells.at (slot).cell().target == keys[selected])
                    handle = base + u::kFxValueBase + slot;
        check (handle >= base, "the rack's actual selected native cell is published");
        if (handle < base)
        {
            tap (view.ui().peekCloseRect());
            tick();
            continue;
        }
        std::array<float, names> old;
        std::transform (std::begin (keys), std::end (keys), old.begin(), read);
        const float next = old[selected] > 0.45f ? 0.25f : 0.75f;
        const auto before = writes();
        write (handle, next);
        tick();
        check (near (read (keys[selected]), next) && near (read (handle), next) && writes() == before + 1,
               "the native PEEK handle makes exactly one engine write to its selected target");
        for (int i = 0; i < names; ++i)
            if (i != selected)
                check (read (keys[i]) == old[i], "the other filter and FX racks remain isolated");

        // A host drawer runs the PageView hide hook without changing our stack.
        write (handle, 0.5f);
        view.onHidden();
        view.setHostBackground (false);
        write (handle, 0.6f);
        check (read (viewParam) == 0.0f && view.ui().topPage() == MixerUi::kPagePeek,
               "a host drawer disarms all PEEK bindings but retains the selected editor");
        view.onShown();
        tick();
        check (read (viewParam) == expectedView && near (read (keys[selected]), next) && writes() == before + 1,
               "the drawer's pending callback is dropped and its PEEK can resume");

        write (handle, 0.5f);
        tap (view.ui().peekCloseRect());
        write (handle, 0.6f);
        tick();
        check (read (viewParam) == 0.0f && ! view.ui().peekRoot() && near (read (keys[selected]), next)
                   && writes() == before + 1 && std::equal (mixerQ.begin(), mixerQ.end(), view.ui().qTargets()),
               "closing drops queued and late PEEK writes and restores all sixteen Mixer Q slots");
        check (r.plugin.overlayView.tabView (IInstanceView::kTabViewFilter) == 3,
               "PEEK preserves the standalone Filter selection");
    }

    write (base + u::kMixerPeek, 0.2f);
    tick();
    const int cutoff = base + u::kFilterValueBase + u::FilterCutoff;
    const auto beforeSwitch = writes();
    write (cutoff, 0.5f);
    view.onHidden();
    view.setHostBackground (false);
    r.stack.closeAll();
    write (cutoff, 0.6f);
    r.stack.openFull (MixerUi::kElementPage);
    tick();
    check (! view.ui().peekRoot() && read (viewParam) == 0.0f && writes() == beforeSwitch,
           "actual tab departure discards the open PEEK stack and ignores its late handle");

    // The last card can be powered, removed and added again through the
    // shared FX chain and gallery without touching either neighbouring rack.
    namespace c = u::fxchain;
    namespace h = u::fxheader;
    const int chain = base + u::kFxChain;
    write (base + u::kMixerPeek, 1.0f);
    tick();
    const int reverbOn = target ("reverb.on"), chorusOn = target ("chorus.on"), delayOn = target ("delay.on");
    write (chain + c::Power, 0.0f);
    tick();
    check (read (reverbOn) == 0.0f && read (chorusOn) == 1.0f && read (delayOn) == 1.0f,
           "powering a native BUS 2 card down touches neither BUS 1 nor MAIN");
    write (chain + c::Power, 1.0f);
    tick();
    const auto header = r.plugin.nativeFxHeader.state();
    if (header.item >= 0)
        write (base + u::kFxHeader + 2 + header.item * h::kFields + h::Remove, 1.0f);
    tick();
    const float chainMode = read (chain + c::View);
    check (header.item >= 0 && read (viewParam) == 1.0f && read (chain + c::Count) == 0.0f
               && near (chainMode, float (c::MainGallery) / (c::kModes - 1)),
           "removing the last BUS 2 card leaves the same native PEEK and opens its empty gallery");
    const int addDistortion = chain + c::Gallery + synthyboy::kFxTypeDistortion - 1;
    check (read (addDistortion) == 1.0f, "the empty BUS 2 gallery advertises the native distortion card");
    write (addDistortion, 0.0f);
    tick();
    const auto added = r.plugin.nativeFxChain.card (0);
    check (near (read (chain + c::Count), 1.0f / c::kSlots) && added.rack == 2
               && added.kind == synthyboy::kFxTypeDistortion && added.on
               && read (chorusOn) == 1.0f && read (delayOn) == 1.0f,
           "the shared native gallery adds the effect only to the PEEK rack");
    tap (view.ui().peekCloseRect());
    tick();

    char name[synthyboy_mpc::kParamStrBytes] {};
    VstParameterProperties properties {};
    const auto named = r.effect.dispatcher (&r.effect, effGetParamName, viewParam, 0, name, 0);
    const auto described = r.effect.dispatcher (&r.effect, effGetParameterProperties, viewParam, 0, &properties, 0);
    write (viewParam, 1.0f);
    check (viewParam == 3349 && base + u::kOscDetail == 3350 && base + u::kMacroTarget == 3541
               && base + u::kMixerControl == 3549 && base + u::kModDetailReserved == 3569
               && base + u::kMixerActiveTab == 3575
               && r.effect.numParams == base + u::kCount && named == 1 && described == 1
               && std::strcmp (name, "Mixer PEEK View") == 0 && properties.maxInteger == 2 && read (viewParam) == 0.0f,
           "the appended Mixer PEEK view handle is stepped, read-only and stable");
}

void testMixerTabsAndRouteClosureVisuals()
{
    namespace u = synthyboy_mpc::nativeui;
    using namespace synthyboy_mpc::overlay;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    const int base = PluginInstance::nativeUiBase();
    const auto write = [&] (int index, float norm) { r.effect.setParameter (&r.effect, index, norm); };
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto pIdx = [] (const char* key) { return synthyboy::findParamIndex (synthyboy::hashKey (key)); };
    pages::MixerPage mixer ("mixer", 0);
    const auto tick = [&]
    {
        ViewInput in;
        in.instance = in.io = &r.plugin.overlayView;
        in.ui = &r.stack;
        in.nowMs = r.now += 50.0;
        mixer.update (in);
        r.plugin.nativeUiPoll (&r.effect);
    };

    // Setup distinct effects per rack and enable filters
    const auto& dly = synthyboy::fxInstanceKeys (0);
    const auto& cho = synthyboy::fxInstanceKeys (1);
    const auto& rev = synthyboy::fxInstanceKeys (2);
    write (dly.type, synthyboy::toNormalised (synthyboy::paramTable()[dly.type], synthyboy::kFxTypeDelay));
    write (dly.rack, synthyboy::toNormalised (synthyboy::paramTable()[dly.rack], 0.0f));
    write (dly.on, 1.0f);
    write (cho.type, synthyboy::toNormalised (synthyboy::paramTable()[cho.type], synthyboy::kFxTypeChorus));
    write (cho.rack, synthyboy::toNormalised (synthyboy::paramTable()[cho.rack], 1.0f));
    write (cho.on, 1.0f);
    write (rev.type, synthyboy::toNormalised (synthyboy::paramTable()[rev.type], synthyboy::kFxTypeReverb));
    write (rev.rack, synthyboy::toNormalised (synthyboy::paramTable()[rev.rack], 2.0f));
    write (rev.on, 1.0f);
    write (pIdx ("filter1.on"), 1.0f);
    write (pIdx ("filter2.on"), 1.0f);

    // Before Mixer opens, appended visual handles report 0.0f cleanly
    check (read (base + u::kMixerActiveTab) == 0.0f && read (base + u::kMixerSoloVisual) == 0.0f
               && read (base + u::kMixerMuteVisual) == 0.0f,
           "Mixer presentation handles read zero before Mixer opens");

    r.stack.openFull (mixer.pageElement());
    r.plugin.overlayView.setTabView (IInstanceView::kTabViewFilter, 3);
    tick();

    // An MPC radio group writes only when its own handle differs from the pressed step.
    const auto pressTab = [&] (int tab)
    {
        const float selected = static_cast<float> (tab) / u::kMixerPeekTargets;
        if (read (base + u::kMixerPeek) != selected)
            write (base + u::kMixerPeek, selected);
        tick();
        return read (base + u::kMixerPeek) == selected && r.plugin.nativeUiReported[u::kMixerPeek] == selected;
    };
    // Test all 36 from -> to tab transitions through the real VST MIXER_PEEK proxy
    bool all36Ok = true;
    for (int from = 0; from < 6; ++from)
    {
        for (int to = 0; to < 6; ++to)
        {
            all36Ok &= pressTab (from);
            all36Ok &= std::fabs (read (base + u::kMixerActiveTab) - static_cast<float> (from) / 5.0f) < 1.0e-5f;

            all36Ok &= pressTab (to);
            all36Ok &= std::fabs (read (base + u::kMixerActiveTab) - static_cast<float> (to) / 5.0f) < 1.0e-5f;
            if (to == 0)
                all36Ok &= (mixer.ui().peekRoot() == nullptr && r.plugin.hostMixerWidgetsHidden.load (std::memory_order_acquire) == false);
            else if (to == 1 || to == 2)
                all36Ok &= (r.plugin.nativeFilter.layout() == to && r.plugin.nativeMixerPeekView.load (std::memory_order_acquire) == u::MixerPeekFilter);
            else
                all36Ok &= (r.plugin.nativeMixerPeekView.load (std::memory_order_acquire) == u::MixerPeekFx);
            all36Ok &= (r.plugin.overlayView.tabView (IInstanceView::kTabViewFilter) == 3);
        }
    }
    write (base + u::kMixerPeek, 0.0f);
    tick();
    check (all36Ok && mixer.ui().peekRoot() == nullptr, "all 36 VST proxy tab transitions preserve Filter/FX/MIXER identity");

    // Verify route-closure readouts: source OFF -> Disabled (3/4 = 0.75), Bus 1 solo preserves Sub feeder, dims Osc A
    write (pIdx ("sub.on"), 1.0f);
    write (pIdx ("osc1.on"), 1.0f);
    write (pIdx ("noise.on"), 0.0f);   // Noise OFF -> DISABLED
    write (pIdx ("sub.destination"), 0.0f);
    write (pIdx ("sub.bus1_send"), 1.0f); // Sub feeds Bus 1
    write (pIdx ("osc1.destination"), 0.0f); // Filter 1 (not soloed)
    write (pIdx ("osc1.bus1_send"), 0.0f);
    write (base + u::kMixerControl + u::mixer::kSolo + 7, 1.0f); // Bus 1 solo
    tick();
    const float subSolo = read (base + u::kMixerSoloVisual + 0);
    const float subMute = read (base + u::kMixerMuteVisual + 0);
    const float oscMute = read (base + u::kMixerMuteVisual + 1);
    const float noiseSolo = read (base + u::kMixerSoloVisual + 4);
    const float bus1Solo = read (base + u::kMixerSoloVisual + 7);
    write (base + u::kMixerActiveTab, 1.0f);   // attempt host write to read-only tab handle
    write (base + u::kMixerSoloVisual + 7, 0.0f);
    write (base + u::kMixerMuteVisual + 1, 0.0f);
    check (std::fabs (subSolo - 0.0f) < 1.0e-5f
               && std::fabs (subMute - 0.0f) < 1.0e-5f       // feeder preserved
               && std::fabs (oscMute - 0.5f) < 1.0e-5f       // unsoloed non-feeder = DIM (2/4)
               && std::fabs (noiseSolo - 0.75f) < 1.0e-5f    // source OFF = DISABLED (3/4)
               && std::fabs (bus1Solo - 0.25f) < 1.0e-5f     // explicit Solo = ON (1/4)
               && read (base + u::kMixerActiveTab) == 0.0f,  // read-only rejected write
           "Mixer route closure visual handles publish feeder preservation, dim solo, disabled source and reject host writes");

    mixer.onHidden();
    r.stack.back();
}



void testSavedDisabledEditors()
{
    namespace u = synthyboy_mpc::nativeui;
    namespace c = u::fxchain;
    using namespace synthyboy_mpc::overlay;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    const int base = PluginInstance::nativeUiBase(), chain = base + u::kFxChain;
    const auto write = [&] (int index, float norm) { r.effect.setParameter (&r.effect, index, norm); };
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto writes = [&] { return r.plugin.uiWriteCalls.load (std::memory_order_relaxed); };
    const auto plain = [&] (int index, float value) { write (index, synthyboy::toNormalised (synthyboy::paramTable()[index], value)); };
    const auto& chorus = synthyboy::fxInstanceKeys (0);
    const auto& delay = synthyboy::fxInstanceKeys (1);
    plain (chorus.type, synthyboy::kFxTypeChorus);
    plain (chorus.order, 1);
    write (chorus.on, 1.0f);
    plain (delay.type, synthyboy::kFxTypeDelay);
    plain (delay.order, 2);
    write (delay.on, 0.0f);
    write (target ("filter1.on"), 0.0f);
    write (target ("filter2.on"), 0.0f);
    const auto saved = r.save();
    const auto restore = [&] {
        check (! saved.empty() && r.restore (saved), "the real project chunk restores both bypassed editors");
        r.expireRestoreWindow();
        check (r.save() == saved, "recalling the project preserves the saved OFF chunk");
    };
    const auto tick = [&] (IView& view) {
        ViewInput in;
        in.ui = &r.stack;
        in.instance = in.io = &r.plugin.overlayView;
        in.nowMs = r.now += 75.0;
        view.update (in);
        r.plugin.nativeUiPoll (&r.effect);
    };
    const auto open = [&] (IView& view) {
        r.stack.closeAll();
        r.stack.openFull (view.pageElement());
        pages::configureNativeTab (view, view.name());
        view.setHostBackground (true);
        tick (view);
    };
    const auto touch = [&] (IView& view, int x, int y, int toX, int toY) {
        view.onTouch ({ TouchEvent::Down, x, y }, r.stack, r.plugin.overlayView, r.now);
        if (toX != x || toY != y)
            view.onTouch ({ TouchEvent::Move, toX, toY }, r.stack, r.plugin.overlayView, r.now + 15.0);
        view.onTouch ({ TouchEvent::Up, toX, toY }, r.stack, r.plugin.overlayView, r.now + 30.0);
    };
    const auto hit = [&] (IView& view, synthyboy_mpc::ui::Rect area, int dy) {
        const int x = area.x + area.w / 2, y = area.y + area.h / 2;
        touch (view, x, y, x, y + dy);
    };
    const auto attemptQ = [&] {
        using synthyboy_mpc::QRouter;
        const auto received = r.plugin.qBank.writeCount();
        bool disarmed = true;
        for (int slot = 0; slot < QRouter::kCount; ++slot)
        {
            disarmed &= r.plugin.qBank.target (slot) < 0 && r.plugin.qBank.mode (slot) != QRouter::kVirtual;
            write (PluginInstance::qBase() + slot, 0.85f);
        }
        // This counter includes every received callback, even for an unbound Q.
        check (disarmed && r.plugin.qBank.writeCount() - received == QRouter::kCount,
               "all sixteen host Q callbacks reached the dispatcher with no real or virtual targets");
    };
    const auto assertUntouched = [&] (uint64_t before, const char* message) {
        check (writes() == before && r.save() == saved, message);
    };

    pages::FxPage fx ("fx", 0);
    restore();
    open (fx);
    const auto findCard = [&] (int kind) {
        for (int i = 0; i < c::kSlots; ++i)
            if (r.plugin.nativeFxChain.card (i).kind == kind)
                return i;
        return -1;
    };
    const int slot = findCard (synthyboy::kFxTypeDelay), neighbour = findCard (synthyboy::kFxTypeChorus);
    const auto beforeFx = writes();
    check (slot >= 0 && neighbour >= 0 && ! r.plugin.nativeFxChain.card (slot).on
               && r.plugin.nativeFxChain.card (neighbour).on,
           "the saved bypassed Delay card and active Chorus both reappear in the native chain");
    if (slot >= 0 && neighbour >= 0)
    {
        write (chain + c::Select + slot, 1.0f);
        tick (fx);
        auto& bar = fx.ui().screen().chainBar;
        auto& editor = fx.ui().screen().slotEditor;
        const int feedback = editor.controlOfKey ("delay.feedback");
        const auto header = r.plugin.nativeFxHeader.state();
        check (header.item == synthyboy::kNumFxSlots + 1 && ! header.on() && feedback >= 0
                   && ! editor.knobs[feedback].enabled() && ! bar.nativeDragEnabled,
               "selecting a saved OFF card opens its editor and leaves its knob and MOVE disabled");
        for (int handle = 0; handle < u::kFxCells; ++handle)
            write (base + u::kFxValueBase + handle, 0.8f);
        if (feedback >= 0)
            hit (fx, editor.knobs[feedback].rect(), -35);
        const auto destination = bar.nativeSelectRect (neighbour), grip = bar.dragRect();
        touch (fx, grip.x + grip.w / 2, grip.y + grip.h / 2, destination.x + destination.w / 2, destination.y + destination.h / 2);
        attemptQ();
        tick (fx);
        assertUntouched (beforeFx, "native OFF FX selection, MOVE, knob drag and all Q writes preserve the project");

        fx.setHostBackground (false);
        tick (fx);
        const auto from = bar.cards[slot].rect(), to = bar.cards[neighbour].rect();
        touch (fx, from.x + 20, from.y + from.h / 2, to.x + 20, to.y + to.h / 2);
        tick (fx);
        assertUntouched (beforeFx, "dragging a bypassed card in the opaque fallback preserves its rack order");

        for (bool loadPreset : { true, false })
        {
            restore();
            write (delay.on, 1.0f);
            tick (fx);
            int fromIndex = -1, toIndex = -1;
            for (int i = 0; i < fx.ui().numCards(); ++i)
            {
                const auto& item = fx.ui().cardItem (i);
                if (item.pool && item.index == 1) fromIndex = i;
                if (item.pool && item.index == 0) toIndex = i;
            }
            check (fromIndex >= 0 && toIndex >= 0, "the restored fallback chain contains both pool cards");
            if (fromIndex < 0 || toIndex < 0)
                continue;
            const auto start = bar.cards[fromIndex].rect(), finish = bar.cards[toIndex].rect();
            const int x = start.x + 20, y = start.y + start.h / 2;
            const int endX = finish.x + 20, endY = finish.y + finish.h / 2;
            const auto before = writes();
            fx.onTouch ({ TouchEvent::Down, x, y }, r.stack, r.plugin.overlayView, r.now);
            fx.onTouch ({ TouchEvent::Move, endX, endY }, r.stack, r.plugin.overlayView, r.now + 15.0);
            if (loadPreset)
                restore();
            else
                write (delay.on, 0.0f);
            fx.onTouch ({ TouchEvent::Up, endX, endY }, r.stack, r.plugin.overlayView, r.now + 30.0);
            assertUntouched (before, loadPreset ? "a fallback card release before the recall update cannot reorder the new preset"
                                                : "a fallback card release before the OFF update cannot reorder the bypassed card");
            tick (fx);
        }
        restore();
        tick (fx);

        fx.onHidden();
        open (fx);
        const int reopenedSlot = findCard (synthyboy::kFxTypeDelay);
        check (reopenedSlot >= 0, "the bypassed editor can be reopened after the FX page closes");
        if (reopenedSlot >= 0)
        {
            write (chain + c::Power + reopenedSlot, 1.0f);
            tick (fx);
            const int p = editor.controlOfKey ("delay.feedback");
            if (p >= 0)
            {
                const int engine = delay.p[p];
                const float old = read (engine);
                int found = 0, address = -1;
                for (int handle = 0; handle < u::kFxCells; ++handle)
                    if (r.plugin.nativeFxCells.at (handle).cell().target == engine)
                    {
                        ++found;
                        address = base + u::kFxValueBase + handle;
                    }
                check (read (delay.on) == 1.0f && found == 1, "POWER rebinds exactly one slot handle for the restored Delay feedback");
                if (found == 1)
                {
                    const auto beforeFeedback = writes();
                    write (address, old > 0.5f ? 0.2f : 0.8f);
                    tick (fx);
                    check (read (engine) != old && writes() == beforeFeedback + 1 && r.save() != saved,
                           "the powered Delay feedback makes exactly one engine write");
                }
            }
            else
                check (false, "the powered Delay rebinds its feedback control");
        }
    }
    fx.onHidden();

    pages::MixerPage mixer ("mixer", 0);
    for (int which = 1; which <= 2; ++which)
    {
        restore();
        open (mixer);
        const auto beforeFilter = writes();
        const int filterOn = target (which == 1 ? "filter1.on" : "filter2.on");
        const int cutoff = target (which == 1 ? "filter1.cutoff" : "filter2.cutoff");
        const float value = read (cutoff), next = value > 0.5f ? 0.2f : 0.8f;
        const int edit = base + u::kFilterValueBase + u::FilterCutoff;
        const float button = static_cast<float> (which) / u::kMixerPeekTargets;
        write (base + u::kMixerPeek, button);
        tick (mixer);
        auto* root = dynamic_cast<synthyboy_mpc::ui::FilterFullScreen*> (mixer.ui().peekRoot());
        check (root && ! root->power.state.on && ! root->cutoff.enabled(), "the restored OFF filter can open its PEEK editor");
        if (root == nullptr)
            continue;
        hit (mixer, root->cutoff.rect(), -30);
        write (edit, next);
        attemptQ();
        tick (mixer);
        hit (mixer, mixer.ui().peekCloseRect(), 0);
        tick (mixer);
        write (edit, next);
        write (base + u::kMixerPeek, button);
        tick (mixer);
        check (read (filterOn) == 0.0f && read (cutoff) == value && mixer.ui().peekRoot() != nullptr,
               "CLOSE and reopening preserve the restored OFF filter and its cutoff");
        assertUntouched (beforeFilter, "an OFF Filter PEEK rejects drag, virtual Q and delayed handles without any writes");

        write (base + u::kFilterValueBase + u::FilterPower, 1.0f);
        tick (mixer);
        write (edit, next);
        tick (mixer);
        check (read (filterOn) == 1.0f && std::fabs (read (cutoff) - next) < 1.0e-4f
                   && writes() == beforeFilter + 2 && r.save() != saved,
               "restoring Filter PEEK POWER rearms its own cutoff with one parameter write per action");
        hit (mixer, mixer.ui().peekCloseRect(), 0);
        tick (mixer);
        mixer.onHidden();
    }
}

void testMasterPanelNavigation()
{
    using namespace synthyboy_mpc::overlay;
    namespace n = synthyboy_mpc::nativeui;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    pages::MasterPage page ("master", 0);
    pages::configureNativeTab (page, page.name());
    r.stack.openFull (page.pageElement());
    const int base = PluginInstance::nativeUiBase();
    const int panel = base + n::kMasterPanel;
    const auto tick = [&]
    {
        ViewInput in;
        in.instance = in.io = &r.plugin.overlayView;
        in.ui = &r.stack;
        in.nowMs = r.now += 50.0;
        page.update (in);
        r.plugin.nativeUiPoll (&r.effect);
    };
    const auto post = [&] (float value) { r.effect.setParameter (&r.effect, panel, value); };
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    tick();
    const auto saved = r.save();
    const auto aperture = [&] (synthyboy_mpc::ui::Rect rect)
    {
        const int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
        std::vector<uint32_t> pixels (1280 * 800);
        page.render (pixels.data(), 1280, 800);
        const bool down = page.onTouch ({TouchEvent::Down, x, y}, r.stack, r.plugin.overlayView, r.now);
        const bool up = page.onTouch ({TouchEvent::Up, x, y}, r.stack, r.plugin.overlayView, r.now + 30.0);
        return pixels[y * 1280 + x] == 0 && ! down && ! up;
    };
    page.setHostBackground (true);
    tick();
    check (panel == 3460 && read (panel) == 0.5f && aperture (page.ui().screen().sideTuning.rect()),
           "the Master MAIN surface yields its TUNING navigation button to the actual host handle");
    post (1.0f);
    tick();
    check (read (panel) == 1.0f && read (base + n::kMasterView) == 1.0f
           && page.ui().qTargets()[0] == target ("master.scale.type") && aperture (page.ui().screen().sideMain.rect()) && r.save() == saved,
           "native TUNING replaces the Q group and the new MAIN button has a transparent aperture");
    r.effect.setParameter (&r.effect, base + n::kMasterView, 0.5f);
    tick();
    check (read (panel) == 1.0f, "the existing Master view remains display-only");
    post (0.5f);
    tick();
    check (read (panel) == 0.5f && page.ui().qTargets()[0] == target ("master.volume"),
           "native MAIN restores its own Q group");
    post (1.0f);
    check (r.restore (saved), "the actual Master navigation fixture restores the original chunk");
    r.expireRestoreWindow();
    tick();
    check (read (panel) == 0.5f && r.save() == saved, "preset recall invalidates an outstanding Master panel choice");
    page.onHidden();
    post (1.0f);
    tick();
    check (read (panel) == 0.5f && r.save() == saved, "a late navigation callback after leaving Master cannot change the panel");
    const int link = target ("master.bend_down_link");
    r.effect.setParameter (&r.effect, link, 1.0f);
    tick();
    const auto wide = page.ui().screen().bendUp.rect();
    check (wide.x == 994 && wide.w == 272 && page.ui().screen().bendDown.rect().empty() && aperture (wide),
           "linked BEND exposes one wide native aperture");
    r.effect.setParameter (&r.effect, link, 0.0f);
    tick();
    const auto up = page.ui().screen().bendUp.rect();
    const auto down = page.ui().screen().bendDown.rect();
    check (up.w == 134 && down.x == 1132 && ! up.overlaps (down) && aperture (up) && aperture (down),
           "independent BEND UP and DOWN return to separate native cells");
}

void testNativeOscRootLoad()
{
    using namespace synthyboy_mpc::overlay;
    namespace n = synthyboy_mpc::nativeui;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    r.plugin.contentDirPath = "/tmp";
    pages::OscPage a ("osc1", 2), b ("osc2", 3);
    for (const auto* key : {"osc1.on", "osc2.on"})
        r.effect.setParameter (&r.effect, target (key), 1.0f);
    const auto type = [&] (const char* key, float kind)
    {
        const auto index = target (key);
        r.effect.setParameter (&r.effect, index, synthyboy::toNormalised (synthyboy::paramTable()[index], kind));
    };
    type ("osc1.type", 1);
    type ("osc2.type", 4);
    const int fieldA = PluginInstance::nativeUiBase() + n::kOscRootLoad, fieldB = fieldA + 1;
    const int current = PluginInstance::nativeUiBase() + n::kOscRootLoadView;
    const auto tap = [&] (int field) { r.effect.setParameter (&r.effect, field, 1.0f); };
    const auto tick = [&] (pages::OscPage& view)
    {
        ViewInput in;
        in.instance = in.io = &r.plugin.overlayView;
        in.ui = &r.stack;
        in.nowMs = r.now += 50.0;
        view.update (in);
        r.plugin.nativeUiPoll (&r.effect);
    };
    const auto name = [&] (int field)
    {
        char label[synthyboy_mpc::kParamStrBytes] {};
        r.effect.dispatcher (&r.effect, effGetParamName, field, 0, label, 0.0f);
        return std::string (label);
    };
    const auto frame = [&] { return r.effect.getParameter (&r.effect, current); };
    r.stack.openFull (a.pageElement());
    tick (a);
    check (fieldA == 3498 && current == 3501 && frame() == 1.0f / 3.0f && name (fieldA) == "LOAD SAMPLE",
           "the actual VST announces OSC A's empty sample button at its own ABI indices");
    const auto saved = r.save();
    tap (fieldA);
    tick (a);
    check (frame() == 0.0f && r.stack.top() == OscUi::kPageList && r.save() == saved,
           "native sample LOAD opens the actual content browser without modifying the chunk");
    r.stack.back();
    tick (a);
    tap (fieldA);
    check (r.restore (saved), "the actual VST recalls the empty sample preset");
    r.expireRestoreWindow();
    tick (a);
    check (frame() == 1.0f / 3.0f && r.stack.depth() == 1, "preset recall drops the pending VST LOAD callback");
    tap (fieldA);
    a.onHidden();
    r.stack.openFull (b.pageElement());
    tick (b);
    tap (fieldA);
    tick (b);
    check (frame() == 2.0f / 3.0f && name (fieldB) == "LOAD INSTRUMENT" && r.stack.depth() == 1,
           "the old OSC A handle cannot open the new OSC B instrument browser");
    tap (fieldB);
    tick (b);
    check (frame() == 0.0f && r.stack.top() == OscUi::kPageList && r.save() == saved,
           "native instrument LOAD opens on the active source with unchanged preset data");
}

void testPzCurveToolbar()
{
    using namespace synthyboy_mpc::overlay;
    namespace n = synthyboy_mpc::nativeui;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    pages::FilterPage page ("filter1", 1);
    r.stack.openFull (page.pageElement());
    const int base = PluginInstance::nativeUiBase();
    const int source = base + n::kPzSource, availability = base + n::kPzAvailability;
    const int first = base + n::kPzAction, second = first + 1;
    const auto write = [&] (int index, float value) { r.effect.setParameter (&r.effect, index, value); };
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto tick = [&]
    {
        ViewInput in;
        in.instance = in.io = &r.plugin.overlayView;
        in.ui = &r.stack;
        in.nowMs = r.now += 50.0;
        page.update (in);
        r.plugin.nativeUiPoll (&r.effect);
        r.plugin.filterCurvePoll();
    };
    const auto selectPage = [&] (int action)
    {
        write (base + n::kFilterValueBase + n::FilterAction, static_cast<float> (action) / n::FilterLastAction);
        tick();
    };
    const auto action = [&] (int param, n::pz::Action command)
    {
        write (param, static_cast<float> (command) / n::pz::Apply);
        tick();
    };
    const auto touch = [&] (int x, int y)
    {
        const bool down = page.onTouch ({TouchEvent::Down, x, y}, r.stack, r.plugin.overlayView, r.now);
        const bool up = page.onTouch ({TouchEvent::Up, x, y}, r.stack, r.plugin.overlayView, r.now + 25.0);
        tick();
        return down && up;
    };
    for (const char* key : {"filter1.model", "filter2.model"})
    {
        const int model = target (key);
        write (model, synthyboy::toNormalised (synthyboy::paramTable()[model], FilterUi::kModelPz));
    }
    write (target ("filter1.on"), 1.0f);
    write (target ("filter2.on"), 1.0f);
    tick();
    const auto clean = r.save();
    selectPage (n::FilterEditPz);
    auto& editor = page.ui().pzEditor();
    std::vector<uint32_t> pixels (1280 * 800);
    page.render (pixels.data(), 1280, 800);
    check (first == 3494 && read (source) == 0.5f && read (availability) == 0.0f
           && editor.canvas.points().size() == 3 && page.ui().topPage() == FilterUi::kPagePz,
           "the clean F1 PZ editor publishes the source with only Back and Reset active");
    for (const auto rect : editor.toolbarRects())
    {
        const int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
        check (pixels[1280 * y + x] == 0 && ! touch (x, y) && page.ui().topPage() == FilterUi::kPagePz,
               "each native PZ toolbar field passes through touches and leaves a transparent aperture");
    }
    action (first, n::pz::Apply);
    check (read (source) == 0.5f && r.save() == clean, "disabled Apply cannot close or write a clean PZ curve");
    const auto add = [&] { return touch (editor.canvas.px (0.41f), editor.canvas.py (15.0f)); };
    check (add() && editor.canvas.points().size() == 4 && read (availability) == 2.0f / 3.0f,
           "the custom PZ graph still adds a point and arms both native draft actions");
    action (first, n::pz::Remove);
    check (editor.canvas.points().size() == 3 && read (availability) == 0.0f && r.save() == clean,
           "native Delete Point edits just the draft");
    add();
    write (first, 1.0f);
    check (r.restore (clean), "actual VST chunk reload succeeds with the PZ toolbar open");
    r.expireRestoreWindow();
    tick();
    check (read (source) == 0.5f && read (availability) == 0.0f && editor.canvas.points().size() == 3 && r.save() == clean,
           "preset recall discards the pending native Apply and restores the clean graph");
    add();
    action (first, n::pz::Apply);
    const auto f1 = r.save();
    const auto content = r.plugin.overlayView.content();
    check (f1 != clean && read (source) == 0.0f && page.ui().topPage() == 0
           && content && content->filterCurves.size() == 1 && content->filterCurves.front().filter == "filter1"
           && content->filterCurves.front().points.size() == 4,
           "the active native Apply commits the F1 curve and closes the editor");
    selectPage (n::FilterF2);
    tick(); // let the common Filter action report its binding for the new root
    selectPage (n::FilterEditPz);
    check (read (source) == 1.0f, "the native F2 root opens its own PZ toolbar");
    const auto f2 = r.save();
    add();
    action (first, n::pz::Apply);
    check (read (source) == 1.0f && r.save() == f2, "a late F1 callback cannot apply the open F2 draft");
    action (second, n::pz::Reset);
    check (editor.canvas.points().size() == 3 && read (source) == 1.0f && r.save() == f2,
           "the current F2 Reset acts only on the draft");
    action (second, n::pz::Back);
    check (page.ui().topPage() == 0 && r.save() == f2, "native Back discards the F2 draft and restores the root");
    selectPage (n::FilterEditPz);
    check (add() && read (source) == 1.0f && read (availability) == 2.0f / 3.0f,
           "the second F2 visit arms the native Apply action");
    write (second, 1.0f);
    write (target ("filter2.on"), 0.0f);
    tick();
    const auto afterOff = r.plugin.overlayView.content();
    check (page.ui().topPage() == 0 && read (source) == 0.0f && afterOff && content && *afterOff == *content,
           "powering the current filter off wins over a queued native Apply");
}

void testCompressorBandNavigation()
{
    using namespace synthyboy_mpc::overlay;
    namespace n = synthyboy_mpc::nativeui;
    namespace h = n::fxheader;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    pages::FxPage view ("fx", 0);
    pages::configureNativeTab (view, view.name());
    view.setHostBackground (true);
    r.stack.openFull (view.pageElement());
    const int base = PluginInstance::nativeUiBase();
    const int bandMode = base + n::kFxBandMode, firstBand = base + n::kFxBandBase;
    const int fixedItem = synthyboy::kFxTypeCompressor - 1, poolItem = synthyboy::kNumFxSlots;
    const auto write = [&] (int index, float value) { r.effect.setParameter (&r.effect, index, value); };
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto engine = [&] (const char* key, float plain)
    {
        const int index = target (key);
        if (index >= 0)
            write (index, synthyboy::toNormalised (synthyboy::paramTable()[index], plain));
        else
            check (false, key);
    };
    const auto tick = [&]
    {
        ViewInput in;
        in.instance = in.io = &r.plugin.overlayView;
        in.ui = &r.stack;
        in.nowMs = r.now += 75.0;
        view.update (in);
        r.plugin.nativeUiPoll (&r.effect);
    };
    const auto select = [&] (int item)
    {
        for (int i = 0; i < std::min (view.ui().numCards(), n::fxchain::kSlots); ++i)
        {
            const auto& card = view.ui().cardItem (i);
            if (card.index + (card.pool ? synthyboy::kNumFxSlots : 0) == item)
            {
                write (base + n::kFxChain + n::fxchain::Select + i, 1.0f);
                return true;
            }
        }
        return false;
    };
    engine ("compressor.on", 1.0f);
    engine ("compressor.bands", 0.0f);
    engine ("fxi01.type", static_cast<float> (synthyboy::kFxTypeCompressor));
    engine ("fxi01.on", 1.0f);
    tick();
    const auto initial = r.save();
    check (select (fixedItem), "the fixed compressor is present in the published native chain");
    tick();
    const auto& editor = view.ui().screen().slotEditor;
    const auto rect = editor.nativeBandRect();
    check (firstBand == 3461 && r.plugin.nativeFxHeader.state().item == fixedItem
           && read (bandMode) == 2.0f / 3.0f && ! rect.empty(), "all three native compressor bands are live");
    if (rect.empty())
        return;
    const int x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
    std::vector<uint32_t> pixels (1280 * 800);
    view.render (pixels.data(), 1280, 800);
    const bool down = view.onTouch ({TouchEvent::Down, x, y}, r.stack, r.plugin.overlayView, r.now);
    const bool up = view.onTouch ({TouchEvent::Up, x, y}, r.stack, r.plugin.overlayView, r.now + 30.0);
    check (! down && ! up && pixels[y * 1280 + x] == 0, "the actual native BAND tile has no competing overlay gesture or paint");
    write (firstBand + fixedItem, 1.0f / 3.0f);
    tick();
    const int low = target ("compressor.low.upper_threshold");
    check (r.plugin.nativeFxHeader.state().band == 0 && r.save() == initial
           && std::find (view.ui().qTargets(), view.ui().qTargets() + synthyboy_mpc::QRouter::kCount, low)
                != view.ui().qTargets() + synthyboy_mpc::QRouter::kCount,
           "selecting LOW only remaps the live Compressor band targets and leaves the chunk unchanged");
    engine ("compressor.limit", 1.0f);
    engine ("compressor.bands", 1.0f);
    tick();
    write (firstBand + fixedItem, 1.0f);
    tick();
    check (read (bandMode) == 1.0f / 3.0f && editor.nativeBandRect() == rect && r.plugin.nativeFxHeader.state().band == 0,
           "LIMIT keeps the native tile stable and the Low/Mid mode rejects HIGH callbacks");

    check (select (poolItem), "the new pool compressor is present in the published native chain");
    tick();
    const auto pool = r.plugin.nativeFxHeader.state();
    if (pool.item != poolItem || pool.bandMode == h::NoBand)
    {
        check (false, "the pool compressor publishes its own band binding");
        return;
    }
    const float next = pool.band == 2 ? 2.0f / 3.0f : 1.0f;
    write (firstBand + fixedItem, next);
    tick();
    check (r.plugin.nativeFxHeader.state().band == pool.band, "a late fixed-compressor callback cannot change the pool compressor");
    write (firstBand + poolItem, next);
    tick();
    check (read (firstBand + poolItem) == next, "the new compressor's source-bound callback is accepted");
    const int power = base + n::kFxHeader + 2 + poolItem * h::kFields + h::Power;
    write (firstBand + poolItem, 1.0f / 3.0f);
    write (power, 0.0f);
    tick();
    check (read (bandMode) == 0.0f && ! editor.nativeBandShown(), "power off takes precedence over a queued band choice");
}

void testNativeRandomStereoOwnership()
{
    using namespace synthyboy_mpc::overlay;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    r.legacyChoice (4);
    r.plugin.overlayView.setTabView (r.plugin.overlayView.kTabViewRnd, 2);
    r.tick();
    const auto area = r.view.lfoUi()->compact().stereo.rect();
    const int x = area.x + area.w / 2, y = area.y + area.h / 2;
    const auto before = r.plugin.uiWriteCalls.load (std::memory_order_relaxed);
    const int fade = target ("lfo4.fade_in"), stereo = target ("rlfo2.stereo");
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const float oldFade = read (fade), nextStereo = 1.0f - read (stereo);
    const bool down = r.view.onTouch ({TouchEvent::Down, x, y}, r.stack, r.plugin.overlayView, r.now);
    const bool up = r.view.onTouch ({TouchEvent::Up, x, y}, r.stack, r.plugin.overlayView, r.now + 50.0);
    check (! down && ! up && r.plugin.uiWriteCalls.load (std::memory_order_relaxed) == before,
           "the RND STEREO touch is yielded to the stock Play control");
    r.write (n::Stereo, nextStereo);
    r.tick();
    check (read (fade) == oldFade && read (stereo) == nextStereo && r.plugin.uiWriteCalls.load (std::memory_order_relaxed) == before + 1,
           "the native RND STEREO callback writes exactly one random target and leaves standard Rise alone");
}

void testHistoricalXlDirectHostControls()
{
    using namespace synthyboy_mpc::overlay;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    const auto read = [&] (int index) { return r.effect.getParameter (&r.effect, index); };
    const auto write = [&] (int index, float norm) { r.effect.setParameter (&r.effect, index, norm); };
    const auto page = [&] (IView& v) {
        r.stack.closeAll();
        r.stack.openFull (v.pageElement());
        pages::configureNativeTab (v, v.name());
        v.setHostBackground (true);
        ViewInput input;
        input.ui = &r.stack;
        input.instance = input.io = &r.plugin.overlayView;
        input.nowMs = r.now += 75.0;
        v.update (input);
    };
    const int count = static_cast<int> (synthyboy::paramCount());
    std::vector<uint32_t> pixels (1280 * 800);
    std::vector<float> before (static_cast<size_t> (count));
    const auto checkHostGesture = [&] (IView& view, synthyboy_mpc::ui::Rect area, bool toggle, const char* key) {
        const int parameter = target (key);
        if (parameter < 0)
        {
            check (false, key);
            return;
        }
        for (int i = 0; i < count; ++i)
            before[i] = read (i);
        const auto beforeWrites = r.plugin.uiWriteCalls.load (std::memory_order_relaxed);
        const int x = area.x + area.w / 2, y = area.y + area.h / 2;
        view.render (pixels.data(), 1280, 800);
        const TouchEvent down { TouchEvent::Down, x, y }, move { TouchEvent::Move, x, y - 18 };
        const TouchEvent up { TouchEvent::Up, x, toggle ? y : y - 18 };
        const bool downOwned = view.onTouch (down, r.stack, r.plugin.overlayView, r.now);
        const bool moveOwned = ! toggle && view.onTouch (move, r.stack, r.plugin.overlayView, r.now + 12.0);
        const bool upOwned = view.onTouch (up, r.stack, r.plugin.overlayView, r.now + 30.0);
        const std::string label (key);
        check (pixels[y * 1280 + x] == 0u && ! downOwned && ! moveOwned && ! upOwned
                   && r.plugin.uiWriteCalls.load (std::memory_order_relaxed) == beforeWrites,
               (label + " has one visible host aperture with no overlay write").c_str());

        // Deliver the value through the same VST engine index as the stock
        // control declared in the release TUI.
        const float norm = before[parameter];
        const float next = toggle ? 1.0f - norm : (norm > 0.4f ? 0.2f : 0.8f);
        write (parameter, next);
        int changes = 0;
        for (int i = 0; i < count; ++i)
            changes += read (i) != before[i] ? 1 : 0;
        check (changes == 1 && std::fabs (read (parameter) - next) < 1.0e-4f,
               (label + " is the only changed value over the full engine table").c_str());
        view.onHidden();
    };

    pages::MixerPage mixer ("mixer", 0);
    page (mixer);
    mixer.setHostBackground (false);
    write (target ("osc1.on"), 1.0f);
    ViewInput in;
    in.ui = &r.stack;
    in.instance = in.io = &r.plugin.overlayView;
    in.nowMs = r.now += 75.0;
    mixer.update (in);
    const auto level = mixer.ui().screen().strips[1].fader.rect();
    checkHostGesture (mixer, level, false, "osc1.level");
    page (mixer);
    mixer.setHostBackground (false);
    checkHostGesture (mixer, mixer.ui().screen().strips[9].power.rect(), true, "main.fx_bypass");
    pages::VoicePage voice ("voice", 0);
    page (voice);
    checkHostGesture (voice, voice.ui().screen().randPan.rect(), false, "voicectl.rand.pan");
    pages::MasterPage master ("master", 0);
    page (master);
    check (r.plugin.nativeMasterView.load (std::memory_order_relaxed) == nativeui::master::Main,
           "Master exposes MAIN independently of its overlapping TUNING controls");
    checkHostGesture (master, master.ui().screen().volume.rect(), false, "master.volume");
}

#include "native_mod_targets_checks.h"
#include "native_modulations_checks.h"
#include "saved_disabled_root_checks.h"

void testPlayExpandedToCompactQParityVst()
{
    using namespace synthyboy_mpc::overlay;
    namespace u = synthyboy_mpc::nativeui;
    namespace o = u::playoscui;
    namespace x = u::expandedplay;
    struct WideGuard
    {
        bool prev;
        explicit WideGuard (bool wide) : prev (synthyboy_mpc::ui::QGroup::wideDevice())
        {
            synthyboy_mpc::ui::QGroup::setWideDevice (wide);
        }
        ~WideGuard() { synthyboy_mpc::ui::QGroup::setWideDevice (prev); }
    };

    const auto currentQ = [] (PluginInstance& plugin)
    {
        std::array<int, synthyboy_mpc::QRouter::kCount> out {};
        for (int q = 0; q < synthyboy_mpc::QRouter::kCount; ++q)
            out[q] = plugin.qBank.target (q);
        return out;
    };
    const auto expectQ = [] (const std::array<int, synthyboy_mpc::QRouter::kCount>& actual,
                             const std::array<int, synthyboy_mpc::QRouter::kCount>& expected,
                             const PlayView& view, const char* msg)
    {
        bool ok = actual == expected;
        for (int q = 0; ok && q < synthyboy_mpc::QRouter::kCount; ++q)
            ok = view.qTargets()[q] == expected[q];
        check (ok, msg);
    };
    const auto buildCompactOrder = [] (bool wide, int highlight,
                                       const std::array<int, 4>& osc,
                                       const std::array<int, 4>& env,
                                       const std::array<int, 4>& lfo,
                                       const std::array<int, 4>& extra)
    {
        const std::array<std::array<int, 4>, 4> groups { osc, env, lfo, extra };
        const int active = wide ? 0 : std::clamp (highlight - 1, 0, 3);
        std::array<int, synthyboy_mpc::QRouter::kCount> out {};
        for (int blockIdx = 0; blockIdx < 4; ++blockIdx)
        {
            const auto& g = groups[static_cast<size_t> ((active + blockIdx) % 4)];
            for (int k = 0; k < 4; ++k)
                out[blockIdx * 4 + k] = g[k];
        }
        return out;
    };

    for (bool wide : { false, true })
    {
        const WideGuard wideGuard (wide);
        auto rig = std::make_unique<Rig>();
        auto& r = *rig;
        const int play = PluginInstance::playBase();
        const int base = PluginInstance::nativeUiBase();
        const int oscExpand = base + u::kPlayOsc + 1 + o::kFields + o::Expand;
        const int closeOsc = base + u::kPlayExpanded + 1 + x::CloseOsc;
        const int closeEnv = base + u::kPlayExpanded + 1 + x::CloseEnv;
        const int closeLfo = base + u::kPlayExpanded + 1 + x::CloseLfo;
        const int qBase = PluginInstance::qBase();

        const auto setReal = [&] (int paramIndex, float plain)
        {
            r.effect.setParameter (&r.effect, paramIndex, synthyboy::toNormalised (synthyboy::paramTable()[paramIndex], plain));
        };
        const auto touchThird = [&] (int xPos)
        {
            r.view.onTouch ({ TouchEvent::Down, xPos, 400 }, r.stack, r.plugin.overlayView, r.now);
            r.view.onTouch ({ TouchEvent::Up, xPos, 400 }, r.stack, r.plugin.overlayView, r.now + 20.0);
            r.tick();
        };

        // Configure Osc A on, Env 3 (ms), LFO 4 (standard, HZ).
        setReal (target ("osc1.on"), 1.0f);
        r.effect.setParameter (&r.effect, play, synthyboy_mpc::play::selectorNormalised (0, 2));
        r.effect.setParameter (&r.effect, play + 1, synthyboy_mpc::play::selectorNormalised (1, 3));
        r.legacyChoice (4);
        r.effect.setParameter (&r.effect, target ("lfo4.sync"), 0.0f);
        r.effect.setParameter (&r.effect, target ("rlfo2.sync"), 0.0f);
        r.tick();

        const std::array<int, 4> oscA { target ("osc1.type"), target ("osc1.wavetable"), target ("osc1.pan"), target ("osc1.level") };
        const std::array<int, 4> env3 { target ("env3.attack"), target ("env3.decay"), target ("env3.sustain"), target ("env3.release") };
        const std::array<int, 4> lfo4Hz { target ("lfo4.shape"), target ("lfo4.rate"), target ("lfo4.fade_in"), target ("lfo4.smooth_time") };
        const std::array<int, 4> lfo4Bpm { target ("lfo4.shape"), target ("lfo4.division"), target ("lfo4.fade_in"), target ("lfo4.smooth_time") };
        const std::array<int, 4> lfo4Key { target ("lfo4.shape"), -1, target ("lfo4.fade_in"), target ("lfo4.smooth_time") };
        const std::array<int, 4> extraStd4 { target ("lfo4.sync"), target ("osc1.on"), target ("lfo4.mono"), -1 };
        const std::array<int, 4> rnd2Hz { target ("rlfo2.style"), target ("rlfo2.rate"), target ("rlfo2.sync"), target ("rlfo2.stereo") };
        const std::array<int, 4> rnd2Bpm { target ("rlfo2.style"), target ("rlfo2.division"), target ("rlfo2.sync"), target ("rlfo2.stereo") };
        const std::array<int, 4> rnd2Key { target ("rlfo2.style"), -1, target ("rlfo2.sync"), target ("rlfo2.stereo") };
        const std::array<int, 4> extraRnd2 { target ("rlfo2.sync"), target ("osc1.on"), -1, -1 };

        // 1. Hardware reading order across all three highlights (Key 61 rotates, XL stays fixed).
        touchThird (200);
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 1, oscA, env3, lfo4Hz, extraStd4), r.view,
                 "compact Play OSC-active publishes exact 16-slot Q order for Key 61 / XL");
        touchThird (600);
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 2, oscA, env3, lfo4Hz, extraStd4), r.view,
                 "compact Play ENV-active publishes adjacent groups in reading order (Key 61) or fixed strip (XL)");
        touchThird (1000);
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 3, oscA, env3, lfo4Hz, extraStd4), r.view,
                 "compact Play LFO-active keeps group 4 [lfo.sync, osc.on, lfo.mono, -1] after LFO");

        // 2. Expanded -> Compact transitions for Standard LFO (HZ, BPM, KEY), ENV, and OSC.
        r.write (n::LfoExpand, 1.0f);
        r.tick();
        r.plugin.qPoll (&r.effect);
        check (! r.stack.isMain() && r.stack.element() == LfoUi::kElementLfo
                   && currentQ (r.plugin) != buildCompactOrder (wide, 3, oscA, env3, lfo4Hz, extraStd4),
               "expanded Standard LFO replaces compact Play Q slots with expanded LFO slots");
        const int phase4 = target ("lfo4.phase");
        const float oldPhase4 = r.effect.getParameter (&r.effect, phase4);

        // Close expanded Standard LFO -> compact HZ, then check stale vs fresh Q-Link writes.
        r.effect.setParameter (&r.effect, closeLfo, 1.0f);
        r.tick();
        const auto compactStdHz = buildCompactOrder (wide, 3, oscA, env3, lfo4Hz, extraStd4);
        expectQ (currentQ (r.plugin), compactStdHz, r.view,
                 "closing expanded Standard LFO (HZ) restores exact compact Q order");
        // Before qPoll refreshes toldGen_, a Q turn on stepped extra slot Q1 (osc1.on) must not toggle osc power.
        const int extraBank = wide ? 3 : 1;
        const float oldOscOn = r.effect.getParameter (&r.effect, target ("osc1.on"));
        r.effect.setParameter (&r.effect, qBase + extraBank * 4 + 1, 0.0f);
        check (r.effect.getParameter (&r.effect, target ("osc1.on")) == oldOscOn,
               "stepped compact Q slot ignores stale pre-poll write across expanded->compact close");
        r.plugin.qPoll (&r.effect);
        const int lfoBank = wide ? 2 : 0;
        const float oldRate4 = r.effect.getParameter (&r.effect, target ("lfo4.rate"));
        r.effect.setParameter (&r.effect, qBase + lfoBank * 4 + 1, oldRate4 > 0.5f ? 0.25f : 0.75f);
        check (r.effect.getParameter (&r.effect, target ("lfo4.rate")) != oldRate4
                   && r.effect.getParameter (&r.effect, phase4) == oldPhase4,
               "post-close Q turn writes compact lfo4.rate and cannot touch expanded lfo4.phase");

        // Expanded LFO switching sync mode -> BPM and -> KEY before closing to compact.
        r.write (n::LfoExpand, 1.0f);
        r.tick();
        r.effect.setParameter (&r.effect, target ("lfo4.sync"), 0.25f);
        r.tick();
        r.effect.setParameter (&r.effect, closeLfo, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 3, oscA, env3, lfo4Bpm, extraStd4), r.view,
                 "expanded->compact in BPM maps LFO Q2 to lfo4.division");

        r.write (n::LfoExpand, 1.0f);
        r.tick();
        r.effect.setParameter (&r.effect, target ("lfo4.sync"), 1.0f);
        r.tick();
        r.effect.setParameter (&r.effect, closeLfo, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 3, oscA, env3, lfo4Key, extraStd4), r.view,
                 "expanded->compact in KEY releases LFO Q2 (-1) and keeps lfo4.sync in Extra Q1");
        r.effect.setParameter (&r.effect, target ("lfo4.sync"), 0.0f);
        r.tick();

        // 3. Expanded Standard LFO -> switch to RND 2 while expanded -> close to compact (HZ, BPM, KEY) & back to Standard.
        r.write (n::LfoExpand, 1.0f);
        r.tick();
        r.plugin.overlayView.setTabView (r.plugin.overlayView.kTabViewRnd, 2);
        r.tick();
        check (r.view.lfoUi()->random() && r.view.lfoUi()->selection() == 2 && ! r.stack.isMain(),
               "switching Standard -> RND 2 inside expanded LFO keeps expanded view active");
        r.effect.setParameter (&r.effect, closeLfo, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 3, oscA, env3, rnd2Hz, extraRnd2), r.view,
                 "closing expanded RND 2 (HZ) publishes [rlfo2.style, rlfo2.rate, rlfo2.sync, rlfo2.stereo] and [rlfo2.sync, osc1.on, -1, -1]");

        r.write (n::LfoExpand, 1.0f);
        r.tick();
        r.effect.setParameter (&r.effect, target ("rlfo2.sync"), 0.25f);
        r.tick();
        r.effect.setParameter (&r.effect, closeLfo, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 3, oscA, env3, rnd2Bpm, extraRnd2), r.view,
                 "expanded->compact RND 2 (BPM) maps Q2 to rlfo2.division and keeps Extra Q3/Q4 empty");

        r.write (n::LfoExpand, 1.0f);
        r.tick();
        r.effect.setParameter (&r.effect, target ("rlfo2.sync"), 1.0f);
        r.tick();
        r.effect.setParameter (&r.effect, closeLfo, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 3, oscA, env3, rnd2Key, extraRnd2), r.view,
                 "expanded->compact RND 2 (KEY) releases Q2 (-1) and keeps rlfo2.sync / rlfo2.stereo");
        r.effect.setParameter (&r.effect, target ("rlfo2.sync"), 0.0f);
        r.tick();

        // Expanded RND 2 -> switch back to Standard LFO 4 while expanded -> close to compact.
        r.write (n::LfoExpand, 1.0f);
        r.tick();
        r.plugin.overlayView.setTabView (r.plugin.overlayView.kTabViewRnd, 0);
        r.tick();
        check (! r.view.lfoUi()->random() && r.view.lfoUi()->selection() == 4,
               "switching RND 2 -> Standard LFO 4 while expanded restores standard LFO selection");
        r.effect.setParameter (&r.effect, closeLfo, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 3, oscA, env3, lfo4Hz, extraStd4), r.view,
                 "expanded->compact after RND->Standard switch restores Standard LFO & MONO in Extra Q3");

        // 4. Expanded ENV -> compact and Expanded OSC -> compact preserve exact parity for both RND and Standard LFO.
        r.popup (n::Choose);
        r.write (n::Choose, n::choiceValue (12));
        r.popup (-1);
        r.tick();
        const auto compactRndHz = buildCompactOrder (wide, 3, oscA, env3, rnd2Hz, extraRnd2);
        expectQ (currentQ (r.plugin), compactRndHz, r.view, "selecting RND 2 in compact Play publishes RND Q slots");

        r.write (n::EnvExpand, 1.0f);
        r.tick();
        check (! r.stack.isMain() && r.stack.element() == EnvUi::kElementEnv, "EnvExpand opens expanded ENV");
        r.effect.setParameter (&r.effect, closeEnv, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), compactRndHz, r.view, "closing expanded ENV preserves compact RND Q parity");

        r.effect.setParameter (&r.effect, oscExpand, 1.0f);
        r.tick();
        check (! r.stack.isMain() && r.stack.element() == OscUi::kElementOsc, "OscExpand opens expanded OSC");
        r.effect.setParameter (&r.effect, closeOsc, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), compactRndHz, r.view, "closing expanded OSC preserves compact RND Q parity");

        // 5. Preset restore across Standard vs RND while expanded + stale Q write protection during restore window.
        const auto chunkRnd = r.save();
        r.popup (n::Choose);
        r.write (n::Choose, n::choiceValue (4));
        r.popup (-1);
        r.tick();
        const auto chunkStd = r.save();

        r.write (n::LfoExpand, 1.0f);
        r.tick();
        check (r.restore (chunkRnd), "preset recall to RND succeeds while expanded Standard LFO is open");
        r.tick();
        r.plugin.qPoll (&r.effect);
        const int rndRateIdx = target ("rlfo2.rate");
        const float restoredRndRate = r.effect.getParameter (&r.effect, rndRateIdx);
        r.effect.setParameter (&r.effect, qBase + 1, restoredRndRate > 0.5f ? 0.1f : 0.9f);
        check (r.effect.getParameter (&r.effect, rndRateIdx) == restoredRndRate,
               "Q write during preset restore window cannot overwrite restored target");
        r.expireRestoreWindow();
        r.effect.setParameter (&r.effect, closeLfo, 1.0f);
        r.tick();
        expectQ (currentQ (r.plugin), compactRndHz, r.view,
                 "closing expanded LFO after restoring RND preset publishes exact RND compact Q slots");

        // 6. Native Targets Back transition inside expanded ENV/LFO updates expanded root on the same tick.
        r.write (n::EnvExpand, 1.0f);
        r.tick();
        r.stack.openDetail (EnvUi::kPageTargets);
        r.tick();
        check (r.effect.getParameter (&r.effect, base + u::kPlayExpanded) == static_cast<float> (x::EnvDetail) / x::LastFrame,
               "expanded ENV Targets detail publishes EnvDetail frame");
        r.effect.setParameter (&r.effect, base + u::kModTargets + u::targets::Back, 1.0f);
        r.tick();
        check (r.stack.depth() == 1 && r.stack.element() == EnvUi::kElementEnv
                   && r.effect.getParameter (&r.effect, base + u::kPlayExpanded) == static_cast<float> (x::EnvRoot) / x::LastFrame,
               "native Targets Back from expanded ENV Targets returns to EnvRoot on the same tick");
        r.effect.setParameter (&r.effect, closeEnv, 1.0f);
        r.tick();

        // 7. Reopen after onHidden() (modal or other tab clearing qBank) restores exact compact Q slots without requiring onShown().
        r.view.onHidden();
        r.plugin.overlayView.setQGroup (nullptr, 0);
        check (r.plugin.qBank.target (0) == -1 && r.plugin.qBank.target (lfoBank * 4) == -1,
               "external modal / tab clear wipes Play Q slots while PlayView is hidden");
        r.tick();
        expectQ (currentQ (r.plugin), compactRndHz, r.view,
                 "reopening PlayView after onHidden() immediately republishes compact RND Q slots");

        check (r.restore (chunkStd), "restoring Standard preset on reopen");
        r.expireRestoreWindow();
        r.tick();
        r.view.onHidden();
        r.plugin.overlayView.setQGroup (nullptr, 0);
        r.tick();
        expectQ (currentQ (r.plugin), buildCompactOrder (wide, 3, oscA, env3, lfo4Hz, extraStd4), r.view,
                 "reopening PlayView after onHidden() with Standard LFO restores exact Standard Q slots incl. LFO MONO");
    }
}

void testPlayCompactSmoothNativeHost ()
{
    using namespace synthyboy_mpc::overlay;
    namespace pl = synthyboy_mpc::play;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;

    const int prox0 = PluginInstance::playBase() + pl::kSelectors;
    const int smoothTimeProxy = prox0 + 2 * pl::kSlots + pl::kLfoSmoothTimeSlot;
    const int smoothOnProxy = prox0 + 2 * pl::kSlots + pl::kLfoSmoothOnSlot;

    // 1. Verify proxy parameters exist and have correct descriptor names
    char nameBuf[64] = {};
    r.effect.dispatcher (&r.effect, effGetParamName, smoothOnProxy, 0, nameBuf, 0.0f);
    check (std::string (nameBuf) == "Play LFO Smooth On", "smooth on proxy parameter name");
    nameBuf[0] = '\0';
    r.effect.dispatcher (&r.effect, effGetParamName, smoothTimeProxy, 0, nameBuf, 0.0f);
    check (std::string (nameBuf) == "Play LFO Smooth", "smooth time proxy parameter name");

    // 2. Select LFO 1 and verify initial ON state & smooth_time pass-through
    r.legacyChoice (1);
    r.tick();
    const int lfo1Smooth = target ("lfo1.smooth");
    const int lfo1Time = target ("lfo1.smooth_time");
    check (lfo1Smooth >= 0 && lfo1Time >= 0, "LFO 1 smooth and smooth_time parameters exist");

    r.effect.setParameter (&r.effect, smoothOnProxy, 1.0f);
    r.tick();
    check (r.effect.getParameter (&r.effect, lfo1Smooth) == 1.0f, "smooth active on LFO 1");
    r.effect.setParameter (&r.effect, smoothTimeProxy, 0.42f);
    r.tick();
    check (std::fabs (r.effect.getParameter (&r.effect, lfo1Time) - 0.42f) < 1e-4f,
           "smooth_time writes through to engine when smooth is ON");

    // 3. ON -> OFF transition and time-write protection
    r.effect.setParameter (&r.effect, smoothOnProxy, 0.0f);
    r.tick();
    check (r.effect.getParameter (&r.effect, lfo1Smooth) == 0.0f, "smooth toggles to OFF");
    const auto chunkOff = r.save();

    // Attempt to write smooth_time while OFF - must be protected
    r.effect.setParameter (&r.effect, smoothTimeProxy, 0.95f);
    r.tick();
    check (std::fabs (r.effect.getParameter (&r.effect, lfo1Time) - 0.42f) < 1e-4f,
           "time-write protection: engine smooth_time unchanged when smooth is OFF");
    check (r.save() == chunkOff,
           "time-write protection: project chunk unchanged when smooth_time written while OFF");

    // 4. OFF -> ON transition: time-write works again
    r.effect.setParameter (&r.effect, smoothOnProxy, 1.0f);
    r.tick();
    check (r.effect.getParameter (&r.effect, lfo1Smooth) == 1.0f, "smooth toggles back to ON");
    r.effect.setParameter (&r.effect, smoothTimeProxy, 0.77f);
    r.tick();
    check (std::fabs (r.effect.getParameter (&r.effect, lfo1Time) - 0.77f) < 1e-4f,
           "smooth_time writes through again after toggling back ON");

    // 5. Switching between standard LFOs (LFO 1 -> LFO 4 -> LFO 1)
    const int lfo4Smooth = target ("lfo4.smooth");
    const int lfo4Time = target ("lfo4.smooth_time");
    r.legacyChoice (4);
    r.tick();
    r.effect.setParameter (&r.effect, smoothOnProxy, 0.0f);
    r.tick();
    check (r.effect.getParameter (&r.effect, lfo4Smooth) == 0.0f, "LFO 4 smooth is OFF");
    const float oldLfo4Time = r.effect.getParameter (&r.effect, lfo4Time);
    r.effect.setParameter (&r.effect, smoothTimeProxy, 0.12f);
    r.tick();
    check (r.effect.getParameter (&r.effect, lfo4Time) == oldLfo4Time,
           "LFO 4 smooth_time protected while LFO 4 smooth is OFF");

    // Return to LFO 1
    r.legacyChoice (1);
    r.tick();
    check (r.effect.getParameter (&r.effect, smoothOnProxy) == 1.0f,
           "returning to LFO 1 restores LFO 1 active smooth state");
    check (std::fabs (r.effect.getParameter (&r.effect, smoothTimeProxy) - 0.77f) < 1e-4f,
           "returning to LFO 1 restores LFO 1 smooth_time");

    // 6. Preset save and restore roundtrip with smooth state
    const auto presetChunk = r.save();
    r.effect.setParameter (&r.effect, smoothOnProxy, 0.0f);
    r.tick();
    check (r.effect.getParameter (&r.effect, smoothOnProxy) == 0.0f, "smooth turned OFF before restore");
    check (r.restore (presetChunk), "restore preset chunk with active smooth");
    r.expireRestoreWindow();
    r.tick();
    check (r.effect.getParameter (&r.effect, smoothOnProxy) == 1.0f, "restoring preset restores smooth ON");
    check (std::fabs (r.effect.getParameter (&r.effect, smoothTimeProxy) - 0.77f) < 1e-4f,
           "restoring preset restores smooth_time value");

    // 7. RND (Random LFO) inapplicability
    r.plugin.overlayView.setTabView (r.plugin.overlayView.kTabViewRnd, 1);
    r.tick();
    check (! r.plugin.playLfoIsStandard(), "RND view active: not standard LFO");
    const int smoothTimeProxyIdx = PluginInstance::playProxyOf (smoothTimeProxy);
    const int smoothOnProxyIdx = PluginInstance::playProxyOf (smoothOnProxy);
    check (r.plugin.playProxyBlockedByRandomLfo (smoothTimeProxyIdx), "smooth time proxy blocked in RND");
    check (r.plugin.playProxyBlockedByRandomLfo (smoothOnProxyIdx), "smooth ON proxy blocked in RND");
    const auto rndChunk = r.save();
    r.effect.setParameter (&r.effect, smoothOnProxy, 0.0f);
    r.effect.setParameter (&r.effect, smoothTimeProxy, 0.33f);
    r.tick();
    check (r.save() == rndChunk, "writes to smooth proxies rejected in RND mode");

    // Return to standard LFO from RND
    r.plugin.overlayView.setTabView (r.plugin.overlayView.kTabViewRnd, 0);
    r.tick();
    check (r.plugin.playLfoIsStandard(), "standard LFO active again");
    check (r.effect.getParameter (&r.effect, smoothOnProxy) == 1.0f,
           "standard LFO retains smooth state after returning from RND");
}
}

int main()
{
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;
    const int shape4 = target ("lfo4.shape"), shape5 = target ("lfo5.shape");
    r.legacyChoice (4);
    r.tick();
    const auto standard = r.save();
    r.plugin.overlayView.setTabView (r.plugin.overlayView.kTabViewRnd, 2);
    r.tick();
    const auto random = r.save();
    check (! standard.empty() && standard != random, "a chunk carries the active random view and last standard selector");
    check (r.restore (standard) && r.restore (random), "both chunks use the real VST restore path");
    r.tick();
    check (r.view.lfoUi()->random() && r.view.lfoUi()->selection() == 2 && r.read (n::Choose) == n::choiceValue (12)
               && r.plugin.overlayView.playSelection (2) == 4,
           "the VST restore and legacy host report echo retain RND 2 and the last standard LFO");
    check (r.save() == random, "passive reports and echoes leave the restored chunk unchanged");
    const int engineBefore = engineReports;
    r.expireRestoreWindow();
    r.write (n::Choose, n::choiceValue (4));
    r.tick();
    check (r.view.lfoUi()->random(), "a choice with no marked menu cannot alter RND override");
    r.popup (n::Choose);
    r.write (n::Choose, n::choiceValue (4));
    r.popup (-1);
    r.tick();
    check (! r.view.lfoUi()->random() && r.view.lfoUi()->selection() == 4 && r.read (n::Choose) == n::choiceValue (4)
               && engineReports == engineBefore && r.save() == standard,
           "the real SDK-bound VST value returns RND to the same LFO, with no engine change and the original standard chunk");

    VstParameterProperties choices {};
    r.effect.dispatcher (&r.effect, effGetParameterProperties, parameter (n::Choose), 0, &choices, 0);
    check (choices.minInteger == 0 && choices.maxInteger == 13, "the SDK chooser exposes fourteen discrete positions");
    for (int i = 1; i <= 4; ++i)
    {
        const int id = n::kStandardLfos + i;
        r.popup (n::Choose);
        r.write (n::Choose, n::choiceValue (id));
        r.popup (-1);
        r.tick();
        char text[128] {};
        r.effect.dispatcher (&r.effect, effGetParamDisplay, parameter (n::Choose), 0, text, 0);
        check (r.view.lfoUi()->random() && r.view.lfoUi()->selection() == i && r.read (n::Choose) == n::choiceValue (id)
                   && std::string (text) == "RND " + std::to_string (i) && r.plugin.overlayView.playSelection (2) == 4
                   && engineReports == engineBefore,
               "every native random choice reports its own name and preserves the standard selector and sound");
    }
    r.popup (n::Choose);
    r.write (n::Choose, n::choiceValue (4));
    r.popup (-1);
    r.tick();
    check (r.save() == standard, "round-tripping all random views retains the exact original standard chunk");

    const int sync4 = target ("lfo4.sync"), randomSync2 = target ("rlfo2.sync");
    r.effect.setParameter (&r.effect, sync4, 0.75f);
    r.tick();
    VstParameterProperties sync {};
    r.effect.dispatcher (&r.effect, effGetParameterProperties, parameter (n::StandardSync), 0, &sync, 0);
    check (parameter (n::StandardSync) == 3348 && parameter (n::Sync) == 3309 && r.read (n::Sync) == 0.0f
               && r.read (n::StandardSync) == 0.5f && sync.maxInteger == 2,
           "the appended standard handle exposes three modes and reports triplet as BPM");
    for (const auto& mode : std::array<std::pair<float, float>, 4> {{{0.5f, 0.75f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.5f, 0.25f}}})
    {
        r.write (n::StandardSync, mode.first);
        r.tick();
        check (r.read (n::StandardSync) == mode.first && r.effect.getParameter (&r.effect, sync4) == mode.second,
               "native standard Sync follows BPM/KEY/HZ and preserves the triplet on BPM reselection");
    }
    r.popup (n::Choose);
    r.write (n::Choose, n::choiceValue (12));
    r.popup (-1);
    r.tick();
    sync = {};
    r.effect.dispatcher (&r.effect, effGetParameterProperties, parameter (n::Sync), 0, &sync, 0);
    check (sync.maxInteger == 4, "RND Sync exposes all five engine choices");
    const float oldRandomSync = r.effect.getParameter (&r.effect, randomSync2);
    const float newRandomSync = oldRandomSync == 0.75f ? 0.5f : 0.75f;
    r.write (n::Sync, newRandomSync);
    r.tick();
    check (r.effect.getParameter (&r.effect, randomSync2) == oldRandomSync, "RND Sync requires the marked menu");
    r.popup (n::Sync);
    r.write (n::Sync, newRandomSync);
    r.popup (-1);
    r.tick();
    check (r.read (n::Sync) == newRandomSync && r.effect.getParameter (&r.effect, randomSync2) == newRandomSync
               && r.effect.getParameter (&r.effect, sync4) == 0.25f,
           "the marked RND Sync writes the selected RND's subdivision and leaves the standard LFO intact");
    const int reportsBefore = standardSyncReports;
    r.popup (n::Sync);
    r.popup (n::Choose);
    r.write (n::Choose, n::choiceValue (4));
    r.popup (-1);
    r.tick();
    check (standardSyncReports > reportsBefore && r.read (n::StandardSync) == 0.5f,
           "the host has received the active standard mode after the RND drawer went away");
    r.write (n::Sync, 1.0f);
    r.tick();
    check (r.effect.getParameter (&r.effect, sync4) == 0.25f && r.effect.getParameter (&r.effect, randomSync2) == newRandomSync,
           "a late callback from the RND drawer changes neither the active standard mode nor the old random subdivision");
    r.write (n::StandardSync, 1.0f);
    r.tick();
    check (r.effect.getParameter (&r.effect, sync4) == 1.0f, "a fresh standard button still selects KEY after rejecting the old RND callback");
    check (r.restore (standard), "restore the original LFO values before testing late input");
    r.expireRestoreWindow();
    r.tick();

    const float old4 = r.effect.getParameter (&r.effect, shape4);
    const float old5 = r.effect.getParameter (&r.effect, shape5);
    r.popup (n::Shape);
    r.legacyChoice (5);
    r.legacyChoice (4);
    r.write (n::Shape, 0.5f);
    r.popup (-1);
    r.tick();
    check (r.effect.getParameter (&r.effect, shape4) == old4,
           "a stale shape callback after A/B/A source changes cannot write during the original modal");

    r.popup (n::Shape);
    check (r.restore (random) && r.restore (standard), "restoring A/B/A exercises the preset generation");
    r.expireRestoreWindow();
    r.write (n::Shape, 0.5f);
    r.popup (-1);
    r.tick();
    check (r.effect.getParameter (&r.effect, shape4) == old4,
           "the old popup cannot write on the same source after A/B/A preset recall");

    r.popup (n::Shape);
    r.view.onHidden();
    r.tick();
    r.write (n::Shape, 0.5f);
    r.tick();
    check (r.effect.getParameter (&r.effect, shape4) == old4, "a late callback after leaving and returning to Play writes nothing");
    r.popup (n::Shape);
    r.write (n::Shape, 0.5f);
    r.popup (n::Choose);
    r.write (n::Choose, n::choiceValue (5));
    r.popup (-1);
    r.tick();
    r.tick();
    check (r.view.lfoUi()->selection() == 5 && r.effect.getParameter (&r.effect, shape4) == old4
               && r.effect.getParameter (&r.effect, shape5) == old5,
           "a source choice wins over a shape callback received on the same display tick");
    r.popup (n::Shape);
    r.write (n::Shape, 0.75f);
    r.popup (-1);
    r.tick();
    check (r.effect.getParameter (&r.effect, shape4) == old4 && r.effect.getParameter (&r.effect, shape5) != old5,
           "a fresh native menu writes only the selected standard LFO");
    testCompactOscVst();
    testCompactExpandVst();
    testExpandedOscVst();
    testMixerPeekVst();
    testMixerPeekDisplayRefresh();
    testMixerTabsAndRouteClosureVisuals();
    testEnvTargetCount();
    testStandaloneOscDetailVst();
    testNativeModulationsVst();
    testMasterPanelNavigation();
    testNativeModTargets();
    testPzCurveToolbar();
    testNativeOscRootLoad();
    testCompressorBandNavigation();
    testNativeRandomStereoOwnership();
    testHistoricalXlDirectHostControls();
    testSavedDisabledEditors();
    testSavedDisabledNativeRoots();
    testPlayExpandedToCompactQParityVst();
    testPlayCompactSmoothNativeHost();
    std::printf ("Native MPC VST: %d failures\n", failures);
    return failures != 0;
}
