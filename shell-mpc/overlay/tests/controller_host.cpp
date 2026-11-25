// SynthyBoy - MPC overlay tests: the controller state machine with a fake display backend, a fake framebuffer and a fake clock; then the same controller on a real thread
// (run it under ThreadSanitizer too).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <thread>

#include "fakes.h"
#include "demo_common.h"
#include "fixtures/legacy_layout.h"

using namespace ovt;

struct Rig
{
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    FakeInstance inst;
    std::unique_ptr<SynthyBoyController> ctl;
    double now = 0.0;

    explicit Rig (bool attach = true, double cost = 10.0, ITouchInput* touch = nullptr)
    {
        frame.views = legacy::kViews;
        ctl = std::make_unique<SynthyBoyController> (plane, frame, renderer, dir.paths(), Timing(), cost, nullptr, touch, &legacy::pageSet());
        inst.set ("filter1.cutoff", 1000.0f);
        inst.set ("filter1.resonance", 30.0f);
        if (attach)
            ctl->attach (&inst);
    }
    void run (double ms, double step = 10.0)
    {
        const double end = now + ms;
        while (now < end) { now += step; ctl->tick (now); }
    }
    /** Runs until `cond` holds; returns the milliseconds it took (or -1 after maxMs). */
    template <class F> double until (F cond, double maxMs, double step = 10.0)
    {
        const double start = now;
        while (now - start < maxMs) { now += step; ctl->tick (now); if (cond()) return now - start; }
        return -1.0;
    }
    bool on() const { return plane.planeOn; }
};

static std::string line (const std::string& text, const std::string& key)
{
    const size_t p = text.find (key + ": ");
    if (p == std::string::npos) return "";
    const size_t e = text.find ('\n', p);
    return text.substr (p + key.size() + 2, e - p - key.size() - 2);
}

template <size_t N>
static int viewIndex (const layout::View (&views)[N], const char* name)
{
    for (size_t i = 0; i < N; ++i)
        if (std::strcmp (views[i].name, name) == 0)
            return static_cast<int> (i);
    return -1;
}

static void testProductionTabDetectionTour()
{
    demo::Inst first, second;
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    SynthyBoyController controller (plane, frame, renderer, dir.paths());
    const int slots[] { controller.attach (&first), controller.attach (&second) };
    int previous = -1;
    double now = 0.0;
    const auto tick = [&] {
        for (int i = 0; i < 25; ++i)
            controller.tick (now += 10.0);
    };
    for (int tab = 0; tab < layout::kNumViews; ++tab)
    {
        if (previous >= 0)
            frame.setView (previous, false, 90);
        const auto& expected = layout::kViews[tab];
        const int slot = slots[tab % 2];
        frame.setView (tab, true, 90);
        frame.setViewInstance (tab, slot, true, 90);
        tick();
        const std::string label = expected.name;
        check (plane.planeOn && controller.currentIndex() == tab && controller.activeSlot() == slot,
               "the production detector shows the right instance of " + label);
        if (label == "mixer")
        {
            auto& inst = tab % 2 == 0 ? first : second;
            check (inst.fullOpen, "the Mixer base has an open page stack");
            dir.touch ("off");
            for (int i = 0; i < 12; ++i) tick();   // the three-second disable used on the Key 61
            check (! plane.planeOn && ! inst.fullOpen && controller.uiState (slot).isMain(),
                   "disabling the Mixer releases the instance marker reported by the plugin");
            frame.setViewInstanceRgb (tab, layout::kInstanceBlankRgb, true, 90);
            dir.remove ("off");
            for (int i = 0; i < 5; ++i) tick();
            check (! plane.planeOn && controller.activeSlot() < 0, "a stale blank frame cannot select either instance after re-enable");
            if (! inst.fullOpen)
                frame.setViewInstance (tab, slot, true, 90);
            tick();
            check (plane.planeOn && controller.activeSlot() == slot && controller.currentIndex() == tab,
                   "the reported marker restores the same Mixer without another tab press");
        }
        if (label == "filter1" || label == "mod")
        {
            // RGB at the damaged left marker in the 5f006ab8 XL captures.
            const uint32_t oldColour = label == "filter1" ? 0x274d40 : 0x3f3f42;
            frame.setViewMarker (tab, 0, true, 90, oldColour);
            tick();
            check (! plane.planeOn && controller.currentIndex() < 0, label + " reproduces the old recognition failure");
            frame.setViewMarker (tab, 0, true, 90);
            tick();
            check (plane.planeOn && controller.currentIndex() == tab && controller.activeSlot() == slot,
                   label + " recognizes the intact production markers again");
        }
        previous = tab;
    }
}

static void testMixerDestinationDrawer()
{
    demo::Inst inst;
    inst.set ("sub.destination", 0.0f);
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    SynthyBoyController controller (plane, frame, renderer, dir.paths());
    const int slot = controller.attach (&inst);
    const int mixer = viewIndex (layout::kViews, "mixer");
    const auto* background = backgroundOf ("mixer");
    if (mixer < 0 || background == nullptr)
    {
        check (false, "Mixer native root and sentinel are available");
        return;
    }
    double now = 0.0;
    const auto show = [&] (bool menu) {
        frame.setView (mixer, ! menu, 90);
        if (! menu)
            frame.setViewInstance (mixer, slot, true, 90);
        frame.setUiRect (background->sentinelX, background->sentinelY + page::kTabY, 5, 5, background->sentinelRgb, ! menu, 90);
        for (const auto& mark : kNativeParameterModalMarkers)
            frame.setUiRect (mark.rect.x, mark.rect.y, mark.rect.w, mark.rect.h, mark.rgb, menu, 90);
        for (int i = 0; i < 25; ++i)
            controller.tick (now += 10.0);
    };
    show (false);
    check (plane.planeOn && controller.currentIndex() == mixer && inst.qN > 0, "the Mixer publishes its base Q mapping");
    const int before = inst.writes;
    show (true);
    check (! plane.planeOn && controller.currentIndex() == mixer && inst.qN == 0
           && controller.state() == "hidden: native Mixer editor",
           "the marked destination drawer withdraws the overlay and suspends the Mixer Q mapping");
    const auto touch = controller.touchTaken(), wheel = controller.wheelTaken();
    controller.touchEvent ({ TouchEvent::Down, 56, 400 }, now);
    controller.touchEvent ({ TouchEvent::Move, 56, 470 }, now + 10.0);
    controller.touchEvent ({ TouchEvent::Up, 56, 470 }, now + 20.0);
    controller.wheelEvent (56, 400, 1, now + 30.0);
    show (false);
    check (plane.planeOn && controller.touchTaken() == touch && controller.wheelTaken() == wheel
           && inst.writes == before && inst.qN > 0 && inst.get ("sub.destination") == 0.0f,
           "no background gesture writes while the destination drawer is open, and Cancel restores Mixer Q");
}

static void testNativeFxPopup ()
{
    struct NativeInstance : demo::Inst
    {
        synthyboy_mpc::nativeui::FxCellBank bank;
        void setNativeFxCells (int layout, const synthyboy_mpc::nativeui::FxCell* cells) noexcept override { bank.publish (layout, cells); }
        bool takeNativeFxCell (int cell, const synthyboy_mpc::nativeui::FxCell& binding, float& value) noexcept override
        {
            return bank.take (cell, binding, value);
        }
        synthyboy_mpc::nativeui::fxheader::Bank header;
        void setNativeFxHeader (const synthyboy_mpc::nativeui::fxheader::State& state) noexcept override { header.publish (state, generation); }
        void setNativeFxRackPopup (bool on) noexcept override { header.setRackPopup (on); }
        bool takeNativeFxHeader (int field, const synthyboy_mpc::nativeui::fxheader::State& state, float& value) noexcept override
        {
            return header.take (field, state, value);
        }
        synthyboy_mpc::nativeui::fxchain::Bank chain;
        void setNativeFxChain (const synthyboy_mpc::nativeui::fxchain::State& state) noexcept override
        {
            chain.publish (state, generation);
        }
        void setNativeFxAddPopup (bool on) noexcept override { chain.setMenuOpen (on); }
        bool takeNativeFxChain (int field, const synthyboy_mpc::nativeui::fxchain::State& state, float& value) noexcept override
        {
            return chain.take (field, state, value);
        }
        void chooseAdd (int type)
        {
            namespace c = synthyboy_mpc::nativeui::fxchain;
            chain.poll ([] (int, float, bool) {});
            chain.post (c::Menu, static_cast<float> (type) / c::kTypes, generation);
        }
        void chooseRack (int target)
        {
            namespace h = synthyboy_mpc::nativeui::fxheader;
            const auto state = header.state();
            header.report (header.at (h::Rack));
            header.post (state.item, h::Rack, static_cast<float> (target) / (state.rackCount() - 1), generation);
        }
        void chooseSync (float norm)
        {
            namespace n = synthyboy_mpc::nativeui;
            const auto menu = bank.domain (n::kFxFirstDomain + n::kFxSync);
            bank.report (menu);
            bank.post (menu, norm);
        }
    } first, second;
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    SynthyBoyController controller (plane, frame, renderer, dir.paths());
    const int firstSlot = controller.attach (&first);
    const int secondSlot = controller.attach (&second);
    first.set ("delay.on", 1.0f);
    first.set ("delay.sync", 0.0f);
    second.set ("delay.on", 1.0f);
    const int fx = viewIndex (layout::kViews, "fx");
    const int play = viewIndex (layout::kViews, "play1");
    const auto* bg = synthyboy_mpc::overlay::backgroundOf ("fx");
    if (fx < 0 || play < 0 || bg == nullptr)
    {
        check (false, "native FX view and background exist");
        return;
    }
    double now = 0.0;
    auto tick = [&]
    {
        for (int i = 0; i < 25; ++i)
            controller.tick (now += 10.0);
    };
    auto page = [&] (int view, int slot)
    {
        frame.setView (fx, false, 90);
        frame.setView (play, false, 90);
        if (view >= 0)
        {
            frame.setView (view, true, 90);
            frame.setViewInstance (view, slot, true, 90);
        }
        frame.setUiRect (bg->sentinelX, bg->sentinelY + 110, 5, 5, bg->sentinelRgb, view == fx, 90);
    };
    auto markers = [&] (int count, bool rack = false, bool add = false)
    {
        int i = 0;
        for (const auto& mark : kNativeParameterModalMarkers)
            frame.setUiRect (mark.rect.x, mark.rect.y, mark.rect.w, mark.rect.h, mark.rgb, i++ < count, 90);
        const auto& mark = kNativeFxRackPopupMarker;
        frame.setUiRect (mark.rect.x, mark.rect.y, mark.rect.w, mark.rect.h, mark.rgb, count == 3 && rack, 90);
        const auto& addMark = kNativeFxAddPopupMarker;
        frame.setUiRect (addMark.rect.x, addMark.rect.y, addMark.rect.w, addMark.rect.h, addMark.rgb, count == 3 && add, 90);
    };
    auto modal = [&] (bool rack = false)
    {
        page (-1, firstSlot);
        markers (3, rack);
        tick();
    };
    page (fx, firstSlot);
    tick();
    check (plane.planeOn && first.bank.layout() > 0, "native FX controller starts with an armed Delay editor");

    modal();
    check (! plane.planeOn && controller.state() == "hidden: native FX editor" && controller.currentIndex() == fx
           && first.bank.layout() > 0 && first.qN == 0 && ! controller.uiState (firstSlot).isMain(),
           "recognized FX popup withdraws the plane and suspends FX Q controls while keeping the editor session");
    const auto taken = controller.touchTaken();
    controller.touchEvent ({ TouchEvent::Down, 12, 240 }, now);
    controller.touchEvent ({ TouchEvent::Up, 12, 240 }, now);
    check (controller.touchTaken() == taken, "native FX popup owns the pointer until it closes");
    first.chooseSync (1.0f / 3.0f);
    page (fx, firstSlot);
    markers (0);
    tick();
    check (plane.planeOn && first.get ("delay.sync") == 1.0f && first.writes == 1 && first.qN > 0,
           "native menu selection applies exactly once when the FX page returns and restores its Q-Link mapping");

    modal();
    page (fx, firstSlot);
    markers (0);
    tick();
    check (plane.planeOn && first.writes == 1, "Cancel or Back resumes the FX editor without a write");

    page (-1, firstSlot);
    markers (2);
    tick();
    check (controller.currentIndex() < 0 && first.bank.layout() == 0 && ! plane.planeOn,
           "an unrelated host menu with only two matching patches disarms the FX editor");
    page (fx, firstSlot);
    markers (0);
    tick();

    modal();
    first.chooseSync (2.0f / 3.0f);
    page (play, firstSlot);
    markers (0);
    tick();
    check (controller.currentIndex() == play && first.bank.layout() == 0, "a real tab switch releases the native FX session");
    page (fx, firstSlot);
    tick();
    check (first.get ("delay.sync") == 1.0f && first.writes == 1, "a previous menu write cannot cross a real tab switch");

    modal();
    first.chooseSync (1.0f);
    page (fx, secondSlot);
    markers (0);
    tick();
    check (first.bank.layout() == 0 && second.bank.layout() > 0 && second.writes == 0,
           "a real instance switch disarms the old FX session and arms the new instance only");
    page (fx, firstSlot);
    tick();
    check (first.get ("delay.sync") == 1.0f && first.writes == 1, "a previous menu write cannot cross an instance switch");

    first.chooseRack (1);
    tick();
    check (first.get ("delay.rack") == 0.0f && first.writes == 1, "rack writes require the actual rack popup");
    modal();
    first.chooseRack (1);
    page (fx, firstSlot);
    markers (0);
    tick();
    check (first.get ("delay.rack") == 0.0f && first.writes == 1,
           "the ordinary FX choice popup cannot authorize a rack write");
    modal (true);
    first.chooseRack (1);
    page (fx, firstSlot);
    markers (0);
    tick();
    const int afterRack = first.writes;
    check (first.get ("delay.rack") == 1.0f && afterRack > 1,
           "the marked native rack popup moves the selected Delay to BUS 1");

    modal (true);
    const int sameSource = first.header.state().item;
    ++first.generation;
    first.set ("delay.sync", 2.0f);
    tick();
    ++first.generation;
    first.set ("delay.sync", 1.0f);
    tick();
    first.chooseRack (2);
    page (fx, firstSlot);
    markers (0);
    tick();
    first.chooseRack (2);
    tick();
    check (first.header.state().item == sameSource && first.get ("delay.rack") == 1.0f && first.writes == afterRack,
           "the old rack popup cannot write during A/B/A recall or after the same item is republished");
    modal (true);
    first.chooseRack (2);
    page (fx, secondSlot);
    markers (0);
    tick();
    check (first.get ("delay.rack") == 1.0f && second.get ("delay.rack") == 0.0f && first.writes == afterRack,
           "switching instances discards a rack action already queued for the old popup");
    page (fx, firstSlot);
    tick();
    modal (true);
    first.chooseRack (2);
    page (fx, firstSlot);
    markers (0);
    tick();
    check (first.get ("delay.rack") == 2.0f, "a fresh rack popup on the same item authorizes BUS 2");

    auto addModal = [&]
    {
        page (-1, firstSlot);
        markers (3, false, true);
        tick();
    };
    auto closeAdd = [&] (int selectedSlot)
    {
        page (fx, selectedSlot);
        markers (0);
        tick();
    };
    const int writesBeforeAdd = first.writes;
    constexpr int chorus = synthyboy::kFxTypeChorus;
    const auto& slot0 = synthyboy::fxInstanceKeys (0);
    modal (true);
    first.chooseAdd (chorus);
    closeAdd (firstSlot);
    check (first.writes == writesBeforeAdd, "the RACK marker cannot authorize an ADD callback");
    addModal();
    closeAdd (firstSlot);
    first.chooseAdd (chorus);
    tick();
    check (first.writes == writesBeforeAdd && plane.planeOn, "canceling ADD revokes its lease before a delayed callback");

    addModal();
    ++first.generation;
    first.set ("delay.sync", 2.0f);
    tick();
    ++first.generation;
    first.set ("delay.sync", 1.0f);
    tick();
    first.chooseAdd (chorus);
    closeAdd (firstSlot);
    check (first.writes == writesBeforeAdd, "an ADD callback cannot cross two preset recalls while the same popup stays visible");

    addModal();
    first.chooseAdd (chorus);
    closeAdd (secondSlot);
    check (first.writes == writesBeforeAdd && second.writes == 0, "instance switching invalidates a queued ADD choice");
    closeAdd (firstSlot);
    addModal();
    first.chooseRack (0);
    first.chooseAdd (chorus);
    first.chooseAdd (chorus);
    closeAdd (firstSlot);
    check (first.param (slot0.type) == chorus && first.param (synthyboy::fxInstanceKeys (1).type) == synthyboy::kFxTypeNone
           && first.get ("delay.rack") == 2.0f && first.writes > writesBeforeAdd,
           "one marked native ADD selection creates one Chorus and never authorizes the RACK action");
}

static void testNativePagePopups()
{
    namespace lfo = synthyboy_mpc::nativeui::lfo;
    namespace editor = synthyboy_mpc::nativeui::lfoeditor;
    namespace osc = synthyboy_mpc::nativeui::osc;
    namespace master = synthyboy_mpc::nativeui::master;
    namespace sequence = synthyboy_mpc::nativeui::voice;
    namespace mod = synthyboy_mpc::nativeui::mod;
    namespace playosc = synthyboy_mpc::playosc;
    struct NativeInstance : demo::Inst
    {
        lfo::Bank bank;
        editor::Bank editorBank;
        bool nativeLfoShapeEditor() const noexcept override { return true; }
        void setNativeLfoEditor (const editor::State& state) noexcept override { editorBank.publish (state); }
        bool takeNativeLfoEditor (const editor::State& state, int field, float& value) noexcept override
        {
            return editorBank.take (state, field, value);
        }
        void edit (int field, float value)
        {
            const auto link = editorBank.at (field);
            editorBank.report (link);
            editorBank.post (link, value);
        }
        osc::Bank oscBank;
        sequence::Bank voiceBank;
        synthyboy_mpc::nativeui::NativeCellBank<mod::kNavigation> modNavigation;
        int modSourceGroup = 0;
        void setNativeModNavigation (int mode, int source, int, const synthyboy_mpc::nativeui::FxCell* fields) noexcept override
        {
            modNavigation.publish (mode, fields);
            modSourceGroup = source;
        }
        bool takeNativeModNavigation (int field, const synthyboy_mpc::nativeui::FxCell& expected, float& value) noexcept override
        {
            return modNavigation.take (field, expected, value);
        }
        bool nativeVoiceSequenceControls() const noexcept override { return true; }
        void setNativeVoiceState (const sequence::ViewState& state) noexcept override { voiceBank.publish (state); }
        bool takeNativeVoice (const sequence::ViewState& state, int field, float& value) noexcept override
        {
            return voiceBank.take (state, field, value);
        }
        master::View masterView = master::Hidden;
        void setNativeMasterView (master::View view) noexcept override { masterView = view; }
        bool nativeLfoControls() const noexcept override { return true; }
        void setNativeLfoState (const lfo::ViewState& state) noexcept override { bank.publish (state); }
        bool takeNativeLfo (const lfo::ViewState& state, int field, float& value) noexcept override
        {
            return bank.take (state, field, value);
        }
        void setNativeOscState (const osc::ViewState& state) noexcept override { oscBank.publish (state); }
        bool takeNativeOsc (const osc::ViewState& state, int field, int& choice) noexcept override
        {
            return oscBank.take (state, field, choice);
        }
    } inst;
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    SynthyBoyController controller (plane, frame, renderer, dir.paths());
    const int slot = controller.attach (&inst);
    const int page = viewIndex (layout::kViews, "lfo1");
    const int play = viewIndex (layout::kViews, "play1");
    const int oscillator = viewIndex (layout::kViews, "osca");
    const int filter = viewIndex (layout::kViews, "filter1");
    const int masterViewIndex = viewIndex (layout::kViews, "master");
    const int voice = viewIndex (layout::kViews, "voice");
    const int matrix = viewIndex (layout::kViews, "mod");
    if (page < 0 || play < 0 || oscillator < 0 || filter < 0 || masterViewIndex < 0 || voice < 0 || matrix < 0)
    {
        check (false, "native LFO, OSC, Filter, Master, Voice, Mod and Play views exist");
        return;
    }
    double now = 0.0;
    int visible = -1;
    auto show = [&] (int tab, int markerCount)
    {
        if (visible >= 0)
            frame.setView (visible, false, 90);
        visible = tab;
        if (tab >= 0)
        {
            frame.setView (tab, true, 90);
            frame.setViewInstance (tab, slot, true, 90);
        }
        int i = 0;
        for (const auto& mark : kNativeParameterModalMarkers)
            frame.setUiRect (mark.rect.x, mark.rect.y, mark.rect.w, mark.rect.h, mark.rgb, i++ < markerCount, 90);
        for (int i = 0; i < 25; ++i)
            controller.tick (now += 10.0);
    };
    show (page, 0);
    check (plane.planeOn && inst.bank.publication() && inst.qN > 0, "LFO native root arms without a full-page background sentinel");
    const auto* lfoBackground = backgroundOf ("lfo1");
    const auto* lfoView = dynamic_cast<const PageView<LfoUi>*> (controller.viewByName ("lfo1"));
    check (lfoBackground != nullptr && lfoView != nullptr && controller.currentIndex() == page,
           "the relocated LFO marker triplet identifies its view and instance");
    if (lfoBackground != nullptr && lfoView != nullptr)
    {
        const auto transmitSentinel = [&](bool present)
        {
            frame.setUiRect (lfoBackground->sentinelX, 110 + lfoBackground->sentinelY, 5, 5, lfoBackground->sentinelRgb, present, 90);
            show (page, 0);
        };
        check (! lfoView->ui().hostBackground(), "LFO detector falls back before the host gradient is seen");
        const auto* previous = backgroundOf ("env1");
        if (previous != nullptr)
        {
            frame.setUiRect (previous->sentinelX, 110 + previous->sentinelY, 5, 5, previous->sentinelRgb, true, 90);
            show (page, 0);
            check (! lfoView->ui().hostBackground(), "new LFO markers cannot expose a stale ENV background");
            frame.setUiRect (previous->sentinelX, 110 + previous->sentinelY, 5, 5, previous->sentinelRgb, false, 90);
        }
        transmitSentinel (true);
        check (lfoView->ui().hostBackground(), "LFO detector enables the host gradient after reading the 5x5 sentinel");
        transmitSentinel (false);
        check (! lfoView->ui().hostBackground() && controller.currentIndex() == page,
               "LFO detector restores fallback when the sentinel disappears");
    }
    show (-1, 3);
    check (! plane.planeOn && controller.state() == "hidden: native LFO editor" && controller.currentIndex() == page
           && inst.bank.publication() && inst.qN == 0, "LFO native popup suspends overlay touch and Q-Links while retaining the root binding");
    inst.bank.report (inst.bank.publication());
    inst.bank.post (lfo::kShape, 3.0f / lfo::kShapeLast);
    show (page, 0);
    check (plane.planeOn && inst.get ("lfo1.shape") == 3.0f && inst.writes == 1 && inst.qN > 0,
           "the LFO waveform menu writes once when it closes and restores the root mapping");
    show (-1, 3);
    inst.bank.report (inst.bank.publication());
    inst.bank.post (lfo::kShape, 4.0f / lfo::kShapeLast);
    show (play, 0);
    check (controller.currentIndex() == play && ! inst.bank.publication(), "leaving the LFO tab invalidates its pending host gesture");
    show (page, 0);
    check (inst.get ("lfo1.shape") == 3.0f && inst.writes == 1, "the pending waveform does not write when LFO reopens");

    inst.set ("osc1.on", 1.0f);
    inst.set ("osc2.on", 1.0f);
    inst.set ("osc1.destination", 1.0f);
    inst.set ("osc2.destination", 2.0f);
    show (oscillator, 0);
    check (plane.planeOn && inst.oscBank.snapshot().source() == 2 && inst.qN > 0,
           "opening the Osc A tab arms A independently of Play");
    show (-1, 3);
    check (! plane.planeOn && controller.state() == "hidden: native OSC editor" && controller.currentIndex() == oscillator
           && inst.oscBank.snapshot().source() == 2 && inst.qN == 0,
           "OSC source popup suspends overlay touch and Q-Links without closing its root");
    inst.oscBank.report (inst.oscBank.snapshot());
    inst.oscBank.post (osc::kSource, 0.5f);
    show (oscillator, 0);
    check (plane.planeOn && inst.oscBank.snapshot().source() == 3 && inst.qN > 0 && inst.writes == 1 && inst.selectorWrites == 0,
           "the OSC menu reopens the view on B with its Q-Links and leaves Play untouched");
    show (-1, 3);
    inst.oscBank.report (inst.oscBank.snapshot());
    inst.oscBank.post (osc::kRouting, 0.5f);
    show (oscillator, 0);
    check (inst.get ("osc2.destination") == 3.0f && inst.get ("osc1.destination") == 1.0f && inst.writes == 2,
           "the native routing menu writes the chosen B destination once on returning");
    show (-1, 3);
    inst.oscBank.report (inst.oscBank.snapshot());
    inst.oscBank.post (osc::kRouting, 1.0f);
    show (play, 0);
    check (inst.oscBank.snapshot().source() == 0, "leaving OSC disarms pending routing");
    show (oscillator, 0);
    check (inst.oscBank.snapshot().source() == 2 && inst.get ("osc2.destination") == 3.0f && inst.writes == 2,
           "reopening Osc A restores A and does not replay the old destination");

    show (filter, 0);
    show (-1, 3);
    check (! plane.planeOn && controller.state() == "hidden: native Filter editor" && controller.currentIndex() == filter,
           "the Filter menu uses the same native modal ownership");
    show (filter, 0);
    check (plane.planeOn && controller.currentIndex() == filter, "closing the Filter menu restores its view");

    show (voice, 0);
    using synthyboy_mpc::ui::VoiceGrid;
    const auto gridBounds = synthyboy_mpc::ui::voicelayout::kSequenceGrid;
    VoiceGrid grid;
    grid.place ({ gridBounds.x, layout::kTabOriginY + gridBounds.y, gridBounds.w, gridBounds.h });
    const auto knob = grid.knobRect (1);
    const int knobX = knob.x + knob.w / 2, knobY = knob.y + knob.h / 2;
    const auto voiceTouchCount = controller.touchTaken();
    controller.touchEvent ({ TouchEvent::Down, knobX, knobY }, now);
    controller.touchEvent ({ TouchEvent::Up, knobX, knobY }, now);
    check (controller.touchTaken() == voiceTouchCount, "the native Voice step knob owns its pointer");
    const auto column = grid.columnRect (1);
    const int headerY = column.y + 1;
    const int voiceWrites = inst.writes;
    controller.touchEvent ({ TouchEvent::Down, knobX, headerY }, now);
    check (controller.touchTaken() == voiceTouchCount + 1 && inst.voiceBank.at (sequence::kStep).cell().target == 1
           && inst.writes == voiceWrites, "the Voice column header selects its step without writing a value");
    show (-1, 3);
    check (! plane.planeOn && controller.state() == "hidden: native Voice editor" && controller.currentIndex() == voice && inst.qN == 0,
           "the Voice SDK popup owns the screen and suspends Q-Links");
    const auto voiceWheelCount = controller.wheelTaken();
    controller.wheelEvent (knobX, knobY, 1, now);
    controller.touchEvent ({ TouchEvent::Move, knobX, knobY }, now);
    check (controller.touchTaken() == voiceTouchCount + 1 && controller.wheelTaken() == voiceWheelCount && inst.writes == voiceWrites,
           "the Voice SDK popup blocks overlay touch and wheel writes");
    show (voice, 0);
    controller.touchEvent ({ TouchEvent::Move, knobX, knobY }, now);
    controller.touchEvent ({ TouchEvent::Up, knobX, knobY }, now);
    check (plane.planeOn && controller.currentIndex() == voice && inst.qN == 16 && inst.writes == voiceWrites,
           "closing the Voice popup restores Q-Links and drops the pre-popup grid gesture");
    const auto next = grid.columnRect (2);
    controller.touchEvent ({ TouchEvent::Down, next.x + next.w / 2, headerY }, now);
    controller.touchEvent ({ TouchEvent::Up, next.x + next.w / 2, headerY }, now);
    check (inst.voiceBank.at (sequence::kStep).cell().target == 2 && inst.writes == voiceWrites,
           "the restored Voice grid selects a new edit target without touching its value");

    // A native keypad commits through the held Voice editor session. If the
    // host recalls a preset while the keypad is up, its previous edit expires.
    const auto confirmVoiceValue = [&](int oldTarget) {
        const auto snapshot = inst.voiceBank.at (sequence::kValue);
        const float newValue = inst.param (oldTarget) >= 0.0f ? 0.15f : 0.85f;
        inst.voiceBank.report (snapshot);
        inst.voiceBank.post (snapshot, newValue);
    };
    const int cell = inst.voiceBank.at (sequence::kValue).cell().target;
    check (cell >= 0, "Voice has a published value target after the grid step changes");
    if (cell >= 0)
    {
        const int beforeKeypad = inst.writes;
        show (-1, 3);
        check (inst.qN == 0 && inst.voiceBank.at (sequence::kValue).cell().target == cell,
               "the marked Voice keypad holds its selected cell while other input is paused");
        confirmVoiceValue (cell);
        show (voice, 0);
        check (inst.writes == beforeKeypad + 1 && inst.lastWritten == cell,
               "the native Voice keypad writes the held cell exactly once on confirmation");
        show (-1, 3);
        confirmVoiceValue (cell);
        ++inst.generation;
        show (voice, 0);
        check (inst.writes == beforeKeypad + 1 && inst.qN == 16,
               "a Voice keypad value from the previous preset is discarded when the page returns");
    }

    inst.set ("mod01.source", 1.0f);
    inst.set ("mod01.destination", 1.0f);
    show (matrix, 0);
    auto postMod = [&] (int field, float value)
    {
        const auto snapshot = inst.modNavigation.at (field);
        inst.modNavigation.report (snapshot);
        inst.modNavigation.post (snapshot, value);
    };
    postMod (mod::Mode, 2.0f / mod::kLastMode);
    show (matrix, 0);
    const int beforeMod = inst.writes;
    const auto modTouches = controller.touchTaken();
    controller.touchEvent ({ TouchEvent::Down, 750, 142 }, now);
    controller.touchEvent ({ TouchEvent::Up, 750, 142 }, now);
    show (-1, 3);
    check (! plane.planeOn && controller.currentIndex() == matrix && controller.state() == "hidden: native Mod filter"
           && inst.qN == 0 && inst.modNavigation.layout() == 2 && controller.touchTaken() == modTouches,
           "the native Mod group menu owns the gesture and suspends overlay input and Q-Links");
    postMod (mod::SourceGroup, 1.0f / mod::kLastSourceGroup);
    show (matrix, 0);
    check (plane.planeOn && inst.modSourceGroup == 1 && inst.qN > 0 && inst.writes == beforeMod,
           "choosing a source group restores the Mod view and Q-Links without writing a route");
    show (-1, 3);
    postMod (mod::SourceGroup, 0.0f);
    ++inst.generation;
    show (matrix, 0);
    check (plane.planeOn && inst.modSourceGroup == 1 && inst.writes == beforeMod,
           "a Mod group selection queued before preset recall expires on return");

    show (masterViewIndex, 0);
    check (plane.planeOn && inst.masterView == master::Main && inst.qN > 0, "Master has an active native MAIN view");
    show (-1, 3);
    check (! plane.planeOn && controller.state() == "hidden: native Master editor" && controller.currentIndex() == masterViewIndex
           && inst.masterView == master::Main && inst.qN == 0,
           "the marked Master popup owns input and keeps the native binding alive");
    show (masterViewIndex, 0);
    check (plane.planeOn && inst.masterView == master::Main && inst.qN > 0, "closing the Master popup restores its Q group");
    show (-1, 3);
    show (play, 0);
    check (inst.masterView == master::Hidden && controller.currentIndex() == play, "leaving Master clears the native visibility frame");
    inst.set ("sub.on", 0.0f);
    inst.set ("osc2.on", 0.0f);
    inst.setPlaySelection (0, playosc::kSub);
    show (play, 0);
    check (plane.planeOn && inst.qN == 16 && inst.q[2] < 0 && inst.q[3] < 0,
           "Play shows OFF Sub with ENV and LFO Q routes available");
    const int before = inst.writes;
    const auto touches = controller.touchTaken(), wheels = controller.wheelTaken();
    controller.touchEvent ({ TouchEvent::Down, 140, 145 }, now);
    controller.touchEvent ({ TouchEvent::Up, 140, 145 }, now);
    show (-1, 3);
    check (controller.touchTaken() == touches && ! plane.planeOn && inst.qN == 0
           && controller.state() == "hidden: native Play selector" && controller.currentIndex() == play,
           "the Play selector keeps its host touch and suspends every underlying Q slot while open from OFF");
    controller.touchEvent ({ TouchEvent::Down, 560, 560 }, now);
    controller.wheelEvent (560, 560, 3, now);
    controller.touchEvent ({ TouchEvent::Up, 560, 560 }, now);
    check (inst.writes == before && controller.touchTaken() == touches && controller.wheelTaken() == wheels,
           "Play selector modal prevents edits to the envelope underneath");
    inst.setPlaySelection (0, playosc::kB);
    show (play, 0);
    check (plane.planeOn && inst.playSelection (0) == playosc::kB && inst.qN == 16
           && inst.q[2] < 0 && inst.q[3] < 0 && inst.q[13] == demo::Inst::idx ("osc2.on") && inst.writes == before,
           "selecting OFF Osc B returns to Play and restores its power Q slot without a sound write");
    show (-1, 3);
    show (play, 0);
    check (plane.planeOn && inst.playSelection (0) == playosc::kB && inst.writes == before,
           "cancelling the Play selector keeps the selected oscillator and sound");

    show (page, 0);
    inst.bank.report (inst.bank.publication());
    inst.bank.post (lfo::kAction, 1.0f);
    show (page, 0);
    check (inst.editorBank.display().shown && inst.qN > 0, "native LFO Expand opens the standalone Shape Editor");
    if (lfoView != nullptr)
    {
        const auto graph = lfoView->ui().editorCanvasRect();
        controller.touchEvent ({ TouchEvent::Down, graph.x + 90, graph.y + 90 }, now);
        check (lfoView->fingerDown(), "the Shape Editor canvas owns its point gesture");
    }
    show (-1, 3);
    check (! plane.planeOn && inst.qN == 0 && inst.editorBank.display().shown
               && (lfoView == nullptr || ! lfoView->fingerDown()), "the Shape Editor SDK modal suspends the layer, gesture and Q-Links");
    inst.edit (editor::kMode, 0.6f);
    show (page, 0);
    check (plane.planeOn && inst.get ("lfo1.sync_type") == 3.0f && inst.qN > 0,
           "closing the native MODE list applies the selected LFO trigger and resumes its Q-Links");
    show (-1, 3);
    inst.edit (editor::kPhase, 0.5f);
    ++inst.generation;
    const auto previousPhase = inst.get ("lfo1.phase");
    show (page, 0);
    check (plane.planeOn && inst.get ("lfo1.phase") == previousPhase,
           "the Shape Editor rejects a keypad write held over a preset change");
}

static void testLfoNativeDrawers (bool wide)
{
    using synthyboy_mpc::QRouter;
    namespace ui = synthyboy_mpc::ui;
    namespace standalone = synthyboy_mpc::nativeui::lfo;
    namespace compact = synthyboy_mpc::nativeui::playenvlfo;
    struct Inst : demo::Inst
    {
        standalone::Bank lfo;
        compact::Bank play;
        QRouter router;
        int action = -1;

        bool nativeLfoControls() const noexcept override { return true; }
        void setNativeLfoState (const standalone::ViewState& state) noexcept override { lfo.publish (state); }
        bool takeNativeLfo (const standalone::ViewState& state, int field, float& value) noexcept override
        {
            return lfo.take (state, field, value);
        }
        bool nativePlayEnvLfoControls() const noexcept override { return true; }
        void setNativePlayEnvLfo (const compact::State& state) noexcept override { play.publish (state); }
        bool takeNativePlayEnvLfo (const compact::State& state, int field, float& value) noexcept override
        {
            return play.take (state, field, value);
        }
        void setNativePlayEnvLfoPopup (int field) noexcept override { action = field; play.setPopup (field); }
        void setQGroup (const int* targets, int count) noexcept override
        {
            demo::Inst::setQGroup (targets, count);
            router.setGroup (targets, count, static_cast<int> (synthyboy::paramCount()));
        }
        QRouter::Write turn (int slot, float value)
        {
            return router.hostWrite (slot, value, false, [&] (int target) { return uibind::normOf (target, param (target)); },
                                     [&] (int target, float norm) { writeParam (target, norm); });
        }
    } inst;
    inst.set ("lfo1.sync", 0.0f);
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    SynthyBoyController controller (plane, frame, renderer, dir.paths());
    const int slot = controller.attach (&inst);
    const int lfo = viewIndex (layout::kViews, "lfo1"), play = viewIndex (layout::kViews, "play1");
    if (lfo < 0 || play < 0)
    {
        check (false, "standalone and compact Play LFO drawer pages are available");
        return;
    }
    double now = 0.0;
    int visible = -1;
    auto show = [&] (int page, bool modal, int action = -1)
    {
        if (visible >= 0)
            frame.setView (visible, false, 90);
        visible = page;
        if (page >= 0)
        {
            frame.setView (page, true, 90);
            frame.setViewInstance (page, slot, true, 90);
        }
        for (const auto& mark : kNativeParameterModalMarkers)
            frame.setUiRect (mark.rect.x, mark.rect.y, mark.rect.w, mark.rect.h, mark.rgb, modal, 90);
        for (int i = 0; i < static_cast<int> (std::size (kNativePlayActionMarkers)); ++i)
        {
            const auto& mark = kNativePlayActionMarkers[i];
            frame.setUiRect (mark.rect.x, mark.rect.y, mark.rect.w, mark.rect.h, mark.rgb, modal && i == action, 90);
        }
        for (int i = 0; i < 20; ++i)
            controller.tick (now += 10.0);
    };
    auto runDrawer = [&] (int page, const ui::Rect& button, int action, const ui::Rect& graph, const char* caption)
    {
        const std::string label = std::string (caption) + (wide ? " [16 Q-Links]" : " [4 Q-Links]");
        const int x = graph.x + graph.w / 2, y = graph.y + graph.h / 2;
        const int bx = button.x + button.w / 2, by = button.y + button.h / 2;
        const int rate = demo::Inst::idx ("lfo1.rate");
        const auto taken = controller.touchTaken();
        controller.touchEvent ({ TouchEvent::Down, bx, by }, now);
        controller.touchEvent ({ TouchEvent::Up, bx, by }, now);
        // Play's native touch also makes this third the active Q group.
        // Capture the mapping after that selection has reached the router.
        controller.tick (now += 10.0);
        int qSlot = 0;
        while (qSlot < QRouter::kCount && inst.router.target (qSlot) != rate)
            ++qSlot;
        const auto qCount = inst.qN;
        const auto writes = inst.writes, selectorWrites = inst.selectorWrites;
        const auto shape = inst.get ("lfo1.shape");
        const auto wheels = controller.wheelTaken();
        check (! button.empty() && qSlot < QRouter::kCount && (page != play || qSlot == (wide ? 9 : 1))
                   && controller.touchTaken() == taken,
               label + " single tap belongs to the native host and preserves device-specific RATE routing");

        show (-1, true, action);
        const int expected = action < 0 ? -1 : compact::popupField (action);
        check (! plane.planeOn && controller.currentIndex() == page && inst.action == expected && inst.qN == 0,
               label + " framebuffer markers suspend the overlay, hold the page and bind the right menu");
        controller.touchEvent ({ TouchEvent::Down, x, y }, now);
        controller.touchEvent ({ TouchEvent::Move, x + 12, y }, now);
        controller.wheelEvent (x, y, 3, now);
        check (inst.turn (qSlot, 0.82f) == QRouter::Write::none && inst.writes == writes
                   && controller.touchTaken() == taken && controller.wheelTaken() == wheels,
               label + " blocks background touch, wheel and a real Q proxy write");

        show (page, false);
        controller.touchEvent ({ TouchEvent::Move, x + 24, y }, now);
        controller.touchEvent ({ TouchEvent::Up, x + 24, y }, now);
        check (plane.planeOn && controller.currentIndex() == page && inst.qN == qCount && inst.router.target (qSlot) == rate
                   && inst.action == -1 && inst.get ("lfo1.shape") == shape && inst.selectorWrites == selectorWrites
                   && inst.writes == writes && controller.touchTaken() == taken,
               label + " cancel restores the same targets and leaves the host's held touch and synth state alone");
        controller.touchEvent ({ TouchEvent::Down, x, y }, now);
        controller.touchEvent ({ TouchEvent::Up, x, y }, now);
        const float value = uibind::normOf (rate, inst.param (rate)) < 0.5f ? 0.7f : 0.3f;
        check (controller.touchTaken() > taken && inst.turn (qSlot, value) == QRouter::Write::written
                   && inst.writes == writes + 1 && inst.lastWritten == rate,
               label + " accepts fresh overlay touch and restores the real RATE Q route after cancel");
    };

    show (lfo, false);
    auto* page = const_cast<PageView<LfoUi>*> (dynamic_cast<const PageView<LfoUi>*> (controller.viewByName ("lfo1")));
    show (play, false);
    auto* playView = const_cast<PlayView*> (dynamic_cast<const PlayView*> (controller.viewByName ("play1")));
    if (page == nullptr || playView == nullptr || playView->lfoUi() == nullptr)
    {
        check (false, "controller creates both LFO views");
        return;
    }
    show (lfo, false);
    const auto standaloneGraph = page->ui().full().displayRect();
    runDrawer (lfo, page->ui().nativeRect (standalone::kSelection), -1, standaloneGraph, "standalone LFO/RND drawer");
    runDrawer (lfo, page->ui().nativeRect (standalone::kShape), -1, standaloneGraph, "standalone waveform drawer");
    show (play, false);
    const auto compactGraph = playView->lfoUi()->compact().displayRect();
    for (int i = 0; i < 2; ++i)
    {
        const int action = compact::kFirstPopup + i;
        runDrawer (play, playView->lfoUi()->compactNativeRect (compact::popupField (action)), action, compactGraph,
                   i == 0 ? "compact Play LFO/RND drawer" : "compact Play waveform drawer");
    }
}

static void testEnvNativeModal()
{
    namespace nativeui = synthyboy_mpc::nativeui;
    namespace n = nativeui::env;
    struct Inst : demo::Inst
    {
        nativeui::NativeCellBank<n::kFields> editor;
        void setNativeEnvEditor (bool active, const nativeui::FxCell* fields) noexcept override
        {
            editor.publish (active ? 1 : 0, fields);
        }
        bool takeNativeEnvField (int field, const nativeui::FxCell& expected, float& value) noexcept override
        {
            return editor.take (field, expected, value);
        }
        void choose (float v)
        {
            const auto cell = editor.at (n::Attack);
            editor.report (cell);
            editor.post (cell, v);
        }
    } inst;
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    SynthyBoyController controller (plane, frame, renderer, dir.paths());
    const int slot = controller.attach (&inst);
    const int env = viewIndex (layout::kViews, "env1");
    const int play = viewIndex (layout::kViews, "play1");
    if (env < 0 || play < 0)
    {
        check (false, "standalone Env and Play views are available");
        return;
    }
    int shown = -1;
    double now = 0.0;
    auto show = [&] (int tab, bool modal)
    {
        if (shown >= 0)
            frame.setView (shown, false, 90);
        shown = tab;
        if (tab >= 0)
        {
            frame.setView (tab, true, 90);
            frame.setViewInstance (tab, slot, true, 90);
        }
        for (const auto& mark : kNativeParameterModalMarkers)
            frame.setUiRect (mark.rect.x, mark.rect.y, mark.rect.w, mark.rect.h, mark.rgb, modal, 90);
        for (int i = 0; i < 25; ++i)
            controller.tick (now += 10.0);
    };
    show (env, false);
    check (plane.planeOn && inst.editor.layout() == 1 && inst.qN > 0, "the ENV native editor is armed on the real tab");
    show (-1, true);
    check (! plane.planeOn && controller.state() == "hidden: native Env editor" && controller.currentIndex() == env
           && inst.editor.layout() == 1 && inst.qN == 0, "the marked ENV keypad owns the pointer and retains the active binding");
    inst.choose (0.65f);
    show (env, false);
    check (plane.planeOn && inst.writes == 1 && inst.lastWritten == demo::Inst::idx ("env1.attack") && inst.qN > 0,
           "closing the marked keypad applies its value once and restores the ENV Q group");
    show (-1, true);
    inst.choose (0.2f);
    ++inst.generation;
    show (env, false);
    check (inst.writes == 1, "a marked keypad edit queued before a preset load is discarded");
    show (-1, true);
    inst.choose (0.3f);
    show (play, false);
    show (env, false);
    check (plane.planeOn && inst.writes == 1 && inst.editor.layout() == 1,
           "switching tabs discards the old ENV keypad write and rearms the returned tab");
}

static void testCompactPlayOsc ()
{
    namespace n = synthyboy_mpc::nativeui::playoscui;
    struct NativeOscInst : demo::Inst
    {
        n::Bank bank;
        uint64_t selectionRev = 0;
        bool nativePlayOscControls() const noexcept override { return true; }
        uint64_t playOscSelectionRevision() const noexcept override { return selectionRev; }
        void setPlaySelection (int group, int source) noexcept override
        {
            if (group == 0 && source != playSelection (0))
                ++selectionRev;
            demo::Inst::setPlaySelection (group, source);
        }
        void setNativePlayOsc (const n::State& state, const char*) noexcept override { bank.publish (state); }
        bool takeNativePlayOsc (const n::State& state, int field, float& value) noexcept override
        {
            return bank.take (state, field, value);
        }
        void setNativePlayOscPopup (int field) noexcept override { bank.setPopup (field); }
        void post (int source, int field, float value)
        {
            bank.report (bank.at (field));
            bank.post (source, field, value, generation, selectionRev);
        }
    } first, second;
    TempDir dir;
    FakePlane plane;
    FakeFrame frame;
    CpuCurveRenderer renderer;
    SynthyBoyController controller (plane, frame, renderer, dir.paths());
    const int slot = controller.attach (&first);
    const int other = controller.attach (&second);
    const int play = viewIndex (layout::kViews, "play1");
    const int master = viewIndex (layout::kViews, "master");
    const auto* bg = synthyboy_mpc::overlay::backgroundOf ("play1");
    if (play < 0 || master < 0 || bg == nullptr)
    {
        check (false, "compact native Play and its background exist");
        return;
    }
    double now = 0.0;
    auto tick = [&] { for (int i = 0; i < 20; ++i) controller.tick (now += 10.0); };
    auto page = [&] (int view, int instance)
    {
        frame.setView (play, false, 90);
        frame.setView (master, false, 90);
        if (view != -1)
        {
            frame.setView (view, true, 90);
            frame.setViewInstance (view, instance, true, 90);
        }
        frame.setUiRect (bg->sentinelX, bg->sentinelY + 110, 5, 5, bg->sentinelRgb, view == play, 90);
    };
    auto popup = [&] (int action)
    {
        for (const auto& marker : kNativeParameterModalMarkers)
            frame.setUiRect (marker.rect.x, marker.rect.y, marker.rect.w, marker.rect.h, marker.rgb, action != -1, 90);
        for (int i = 0; i < 3; ++i)
        {
            const auto& marker = kNativePlayActionMarkers[i];
            frame.setUiRect (marker.rect.x, marker.rect.y, marker.rect.w, marker.rect.h, marker.rgb, action == i, 90);
        }
        if (action != -1)
            page (-1, slot);
        tick();
    };
    first.setPlaySelection (0, 2);
    first.set ("osc1.on", 1.0f);
    first.set ("osc1.destination", 0.0f);
    first.set ("osc2.on", 1.0f);
    page (play, slot);
    tick();
    check (n::Bank::frame (first.bank.view()) == 2 && first.bank.at (n::Pan).cell().target == demo::Inst::idx ("osc1.pan"),
           "the real Play tab binds compact OSC A with its own source-specific targets");
    const auto currentPlay = [&] {
        return const_cast<PlayView*> (dynamic_cast<const PlayView*> (controller.viewByName ("play1")));
    };
    auto* playView = currentPlay();
    if (playView == nullptr || playView->oscUi() == nullptr)
    {
        check (false, "the controller produced a Play view");
        return;
    }
    const auto& compact = playView->oscUi()->compact();
    const auto& cells = playView->oscUi()->compactNativeCells();
    std::vector<uint32_t> pixels (1280u * 800u);
    playView->render (pixels.data(), 1280, 800);
    bool clear = true;
    for (const auto& r : cells)
    {
        const int x = r.x + r.w / 2, y = r.y + r.h / 2;
        clear &= r.w >= 56 && r.h >= 56 && pixels[y * 1280 + x] == 0u && ! playView->oscUi()->covers (x, y);
    }
    const auto plot = compact.plotRect();
    check (clear && pixels[(plot.y + plot.h / 2) * 1280 + plot.x + 20] != 0u,
           "all seven active native source controls expose the underlying skin and preserve the graph");
    first.post (2, n::Route, 2.0f / n::kRoutingDetails);
    tick();
    check (first.writes == 0, "compact routing refuses writes outside its recognized host menu");
    popup (3);   // only the generic marker triplet, as in the shared source drawer
    first.post (2, n::Level, 0.3f);
    page (play, slot);
    popup (-1);
    check (first.writes == 0, "another native Play menu blocks direct compact OSC edits");
    popup (0);
    first.post (2, n::Route, 2.0f / n::kRoutingDetails);
    page (play, slot);
    popup (-1);
    check (first.writes == 1 && first.get ("osc1.destination") == 2.0f,
           "one marked compact route selection applies to OSC A");

    popup (0);
    first.post (2, n::Route, 1.0f / n::kRoutingDetails);
    first.setPlaySelection (0, 3);
    first.setPlaySelection (0, 2);
    first.post (2, n::Route, 1.0f / n::kRoutingDetails);
    page (play, slot);
    popup (-1);
    first.post (2, n::Route, 1.0f / n::kRoutingDetails);
    tick();
    check (first.writes == 1 && first.get ("osc1.destination") == 2.0f && first.get ("osc2.destination") == 0.0f,
           "the same compact route popup cannot write before or after an A–B–A source transition");
    popup (0);
    first.post (2, n::Route, 1.0f / n::kRoutingDetails);
    ++first.generation;
    ++first.generation;
    first.post (2, n::Level, 0.3f);
    first.post (2, n::Route, 1.0f / n::kRoutingDetails);
    page (play, slot);
    popup (-1);
    check (first.writes == 1 && first.get ("osc1.destination") == 2.0f,
           "preset generation and the native modal gate prevent both a stale choice and a dial write behind the menu");
    popup (0);
    first.post (2, n::Route, 1.0f / n::kRoutingDetails);
    page (master, slot);
    popup (-1);
    page (play, slot);
    tick();
    check (first.writes == 1, "leaving Play drops a queued compact OSC routing choice");
    popup (0);
    first.post (2, n::Route, 1.0f / n::kRoutingDetails);
    page (play, other);
    popup (-1);
    page (play, slot);
    tick();
    check (first.writes == 1 && second.writes == 0, "changing the Play instance drops a queued route choice");

    first.post (2, n::Power, 0.0f);
    first.post (2, n::Level, 0.3f);
    tick();
    const int off = first.writes;
    const float level = first.get ("osc1.level");
    first.post (2, n::Expand, 1.0f);
    first.post (2, n::Level, 0.2f);
    tick();
    check (off == 2 && first.get ("osc1.on") == 0.0f && first.get ("osc1.level") == level && first.writes == off
           && controller.uiState (slot).isMain(), "POWER has priority and an OFF source disarms Expand and level writes");
    first.post (2, n::Power, 1.0f);
    tick();
    first.post (2, n::Expand, 1.0f);
    tick();
    check (! controller.uiState (slot).isMain() && controller.uiState (slot).element() == OscUi::kElementOsc
           && n::Bank::frame (first.bank.view()) == 0 && first.writes == off + 1,
           "the native Expand opens the existing full OSC editor and disarms the compact skin");
    // The instance changes above replace the controller's Play view object.
    playView = currentPlay();
    if (playView == nullptr || playView->oscUi() == nullptr)
    {
        check (false, "returning to the first instance recreates the Play oscillator view");
        return;
    }
    const auto close = playView->oscUi()->full().close.rect();
    controller.touchEvent ({ TouchEvent::Down, close.x + close.w / 2, close.y + close.h / 2 }, now);
    controller.touchEvent ({ TouchEvent::Up, close.x + close.w / 2, close.y + close.h / 2 }, now + 20.0);
    tick();
    check (controller.uiState (slot).isMain() && n::Bank::frame (first.bank.view()) == 2,
           "closing the existing editor restores compact OSC A and its native controls");
    popup (0);
    first.post (2, n::Route, 1.0f);
    page (play, slot);
    popup (-1);
    check (controller.uiState (slot).top() == OscUi::kPageRoute && first.get ("osc1.destination") == 2.0f
           && first.writes == off + 1, "the Routing controls item opens the existing balance and sends without writing a destination");
    check (controller.back (slot), "return from routing to the expanded editor");
    tick();
    controller.touchEvent ({ TouchEvent::Down, close.x + close.w / 2, close.y + close.h / 2 }, now);
    controller.touchEvent ({ TouchEvent::Up, close.x + close.w / 2, close.y + close.h / 2 }, now + 20.0);
    tick();

    first.setPlaySelection (0, 3);
    tick();
    popup (1);
    first.post (3, n::First, 2.0f / 4.0f);   // Sample in the UI's reordered domain
    page (play, slot);
    popup (-1);
    check (first.get ("osc2.type") == 1.0f && first.get ("osc1.type") == 0.0f
           && n::Bank::frame (first.bank.view()) == 5,
           "the ABC type menu edits OSC B and republishes the file-browser layout");

    first.setPlaySelection (0, 1);
    first.set ("sub.on", 1.0f);
    tick();
    popup (1);
    first.post (1, n::First, 1.0f);
    page (play, slot);
    popup (-1);
    popup (2);
    first.post (1, n::Second, 0.0f);
    page (play, slot);
    popup (-1);
    check (first.get ("sub.octave") == -4.0f, "Sub OCT accepts -4 via numeric list");
    popup (2);
    first.post (1, n::Second, 1.0f);
    page (play, slot);
    popup (-1);
    check (first.get ("sub.octave") == 4.0f, "Sub OCT accepts +4 via numeric list");
    popup (2);
    first.post (1, n::Second, 7.0f / 8.0f);
    page (play, slot);
    popup (-1);
    check (first.get ("sub.waveform") == 5.0f && first.get ("sub.octave") == 3.0f
           && first.bank.at (n::Route).cell().target < 0,
           "Sub waveform and octave use their exact stepped targets while no routing is exposed");

    auto checkSubBody = [&] (const char* state)
    {
        auto* view = currentPlay();
        auto& osc = *view->oscUi();
        view->render (pixels.data(), 1280, 800);
        const auto& cells = osc.compactNativeCells();
        check (cells[n::First] == (synthyboy_mpc::ui::Rect { 14, play::kTabY + 404, 292, 72 })
               && cells[n::Second] == (synthyboy_mpc::ui::Rect { 310, play::kTabY + 412, 112, 56 }),
               std::string ("SUB ") + state + " WAVE and OCT apertures match contract geometry (292x72 and 112x56)");
        for (int field : { n::Power, n::Expand, n::First, n::Second, n::Pan, n::Level })
        {
            const auto rect = cells[field];
            bool transparent = ! rect.empty();
            for (int y = rect.y; y < rect.bottom(); ++y)
                for (int x = rect.x; x < rect.right(); ++x)
                    transparent &= pixels[y * 1280 + x] == 0;
            check (transparent && !osc.covers (rect.x + rect.w / 2, rect.y + rect.h / 2)
                   && rect.w >= 56 && rect.h >= 56,
                   std::string ("SUB ") + state + " host owns all pixels and touch of body field " + std::to_string (field));
        }
    };
    checkSubBody ("ON");
    const int subTargets[] = {demo::Inst::idx ("sub.waveform"), demo::Inst::idx ("sub.octave"),
                              demo::Inst::idx ("sub.pan"), demo::Inst::idx ("sub.level")};
    check (std::equal (std::begin (subTargets), std::end (subTargets), first.q),
           "SUB native controls keep Q1-4 bound to waveform, octave, pan and level");
    const int beforePreset = first.writes;
    first.post (1, n::Pan, 0.91f);
    ++first.generation;
    tick();
    check (first.writes == beforePreset, "SUB drops a pending native dial write on preset replacement");
    first.post (1, n::Pan, 0.61f);
    tick();
    check (first.writes == beforePreset + 1 && first.lastWritten == subTargets[2],
           "the newly bound SUB pan dial writes only its target");
    first.post (1, n::Power, 0.0f);
    tick();
    checkSubBody ("OFF");
    const int offSubWrites = first.writes;
    for (int field : { n::First, n::Second, n::Pan, n::Level })
        first.post (1, field, 0.5f);
    tick();
    check (first.writes == offSubWrites, "OFF SUB readouts reject edits to all four fields");
    first.setPlaySelection (0, 5);
    first.set ("noise.on", 1.0f);
    tick();
    popup (1);
    first.post (5, n::First, 0.5f);
    page (play, slot);
    popup (-1);
    first.post (5, n::Second, 0.37f);
    tick();
    check (first.get ("noise.sample") == 4.0f && first.lastWritten == demo::Inst::idx ("noise.filter")
           && first.bank.at (n::Pan).cell().target == demo::Inst::idx ("noise.pan")
           && currentPlay()->oscUi()->compactNativeCells()[n::First] == (synthyboy_mpc::ui::Rect { 14, play::kTabY + 404, 202, 72 })
           && currentPlay()->oscUi()->compactNativeCells()[n::Second] == (synthyboy_mpc::ui::Rect { 220, play::kTabY + 404, 202, 72 }),
           "the Noise sound menu and filter dial address the Noise parameters with unchanged 202x72 apertures");

    first.setPlaySelection (0, 2);
    tick();
    check (currentPlay()->oscUi()->compactNativeCells()[n::First] == (synthyboy_mpc::ui::Rect { 14, play::kTabY + 404, 202, 72 })
           && currentPlay()->oscUi()->compactNativeCells()[n::Second] == (synthyboy_mpc::ui::Rect { 220, play::kTabY + 404, 202, 72 }),
           "ABC compact apertures remain unchanged at 202x72");
    const float previousTable = first.get ("osc1.wavetable");
    const int beforeSource = first.writes;
    first.post (2, n::Second, 1.0f);
    tick();
    check (first.writes == beforeSource + 1 && first.get ("osc1.wavetable") != previousTable
           && first.lastWritten == demo::Inst::idx ("osc1.wavetable"), "the native table step changes only OSC A's wavetable");
    first.post (2, n::Second, 2.0f / 3.0f);
    tick();
    check (controller.uiState (slot).top() == OscUi::kPageList && n::Bank::frame (first.bank.view()) == 0
           && first.writes == beforeSource + 1, "the native source button opens the existing browser without a parameter write");
}

static void testCompactPlayOscReports ()
{
    namespace n = synthyboy_mpc::nativeui::playoscui;
    n::Bank bank;
    std::array<float, n::kHandles> last {};
    std::vector<int> changes;
    auto tick = [&]
    {
        changes.clear();
        bank.poll (last[0], [] (int, synthyboy_mpc::nativeui::FxCell) { return 0.25f; },
                   [&] (int offset, float value, bool force) {
                       if (force || value != last[offset])
                           changes.push_back (offset);
                       last[offset] = value;
                   });
        return changes.size();
    };
    check (tick() == 0, "hidden compact OSC sends no parameter reports");
    n::State state;
    state.source = 2;
    state.layout = n::AbcWave;
    state.powered = true;
    for (auto& cell : state.fields)
        cell = { 1, 1, synthyboy_mpc::nativeui::osc::SettingsDial };
    bank.publish (state);
    check (tick() == 8 && changes.back() == 0 && tick() == 0, "compact OSC publishes seven fields and its frame once");
    state.source = 3;
    ++state.selection;
    bank.publish (state);
    check (tick() == 9 && changes.front() == 0 && changes[1] == 15 && changes[7] == 21
           && changes.back() == 0 && tick() == 0, "a compact source swap reports only the seven new bindings and its two frame updates");
}

int main()
{
    testEnvNativeModal();
    testNativeFxPopup();
    testProductionTabDetectionTour();
    testMixerDestinationDrawer();
    testCompactPlayOsc();
    testCompactPlayOscReports();
    testNativePagePopups();
    const bool previousWide = synthyboy_mpc::ui::QGroup::wideDevice();
    for (bool wide : { false, true })
    {
        synthyboy_mpc::ui::QGroup::setWideDevice (wide);
        testLfoNativeDrawers (wide);
    }
    synthyboy_mpc::ui::QGroup::setWideDevice (previousWide);
    const Rect curveUi { legacy::kViews[0].region.x, legacy::kViews[0].region.y, legacy::kViews[0].region.w, legacy::kViews[0].region.h };
    // ---- the markers are there from the start: shown at the first check ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        const double t = r.until ([&] { return r.on(); }, 500);
        check (t >= 0 && t <= 100, "markers present: the plane is on within 100 ms (took " + std::to_string (t) + " ms)");
        check (r.plane.lastRect == uiRectToPanel (90, curveUi, 800, 1280), "the plane rectangle is the curve area on the panel (rotation 90)");
        int alpha = 0;
        for (uint32_t p : r.plane.shownCopy) alpha += (p >> 24) != 0;
        check (alpha > 1000, "the picture on the plane has pixels");
        check (r.ctl->state() == "visible", "state: visible");
        r.run (1200);
        const std::string st = r.dir.read ("status.txt");
        check (line (st, "state") == "visible" && ! line (st, "curve_hash").empty() && ! line (st, "markers_read_filter1").empty() && line (st, "view") == "filter1" && line (st, "rotation") == "90", "the status file says visible and has the hash, the marker values and the rotation");
        {
            const Rect pr = uiRectToPanel (90, curveUi, 800, 1280);
            const std::string want = std::to_string (pr.w) + "x" + std::to_string (pr.h) + "+" + std::to_string (pr.x) + "+" + std::to_string (pr.y);
            check (line (st, "plane_rect") == want && pr.w == 232 && pr.h == 1168, "the status file has the plane rectangle (" + want + ")");
        }
        // nothing changes: no more pictures
        const int shown = r.plane.presented;
        const auto original = r.plane.shownCopy;
        r.run (3000);
        check (r.plane.planeOn && r.plane.presented == shown && r.plane.shownCopy == original,
               "the unchanged filter retains its last picture without presenting for 3 s");
        r.inst.set ("filter1.resonance", 70.0f);
        const double wake = r.until ([&] { return r.plane.presented > shown; }, 300);
        const auto changed = r.plane.shownCopy;
        check (wake >= 0 && changed != original, "the first picture after waking shows the changed filter response");
        const int afterWake = r.plane.presented;
        r.run (3000);
        check (r.plane.presented == afterWake && r.plane.shownCopy == changed,
               "the changed filter returns to rest with its new picture intact");
        r.inst.set ("filter1.resonance", 30.0f);
        check (r.until ([&] { return r.plane.presented > afterWake; }, 300) >= 0 && r.plane.shownCopy == original,
               "restoring resonance restores the original pixels on the first presentation");
        // the picture follows a moving cutoff at about 30 pictures a second
        r.inst.hasLive = true;
        const int before = r.plane.presented;
        for (int i = 0; i < 60; ++i) { r.inst.liveCutoff = 600.0f + 30.0f * (float) i; r.now += 33.0; r.ctl->tick (r.now); }
        const int frames = r.plane.presented - before;
        check (frames >= 50 && frames <= 62, "cutoff moving every 33 ms for 2 s: " + std::to_string (frames) + " pictures (about 30 a second)");
    }
    // ---- the markers appear later; one disappears; a colour that is 3 off is not accepted, 2 off is ----
    {
        Rig r;
        r.run (500);
        check (! r.on() && r.plane.countOf ("create") == 0, "no markers: no plane, no buffers");
        check (r.ctl->state() == "hidden: no view on screen", "state: hidden: no view on screen");
        r.frame.setAll (true, 90);
        const double t = r.until ([&] { return r.on(); }, 500);
        check (t >= 0 && t <= 110, "the markers appear: shown within 110 ms (took " + std::to_string (t) + " ms)");
        r.frame.setMarker (1, false, 90);
        const double h = r.until ([&] { return ! r.on(); }, 500);
        check (h >= 0 && h <= 150, "one marker missing: hidden within 150 ms (took " + std::to_string (h) + " ms)");
        check (! r.plane.buffersAlive, "... and the buffers are freed");
        check (r.plane.calls() > 0 && r.plane.callLog().back() == "destroy", "... plane off first, then the buffers");
        r.frame.setMarker (1, true, 90, legacy::kViews[0].markers[1].rgb + 2);   // +2 in blue
        check (r.until ([&] { return r.on(); }, 300) >= 0, "a marker 2 off in one channel is accepted");
        r.frame.setMarker (1, true, 90, legacy::kViews[0].markers[1].rgb + 3);
        check (r.until ([&] { return ! r.on(); }, 300) >= 0, "a marker 3 off is not");
        r.frame.setMarker (1, true, 90);
        r.until ([&] { return r.on(); }, 300);
        // the framebuffer cannot be read for one check: hidden, and it comes back
        r.frame.readable = false;
        check (r.until ([&] { return ! r.on(); }, 300) >= 0, "framebuffer unreadable: hidden");
        r.frame.readable = true;
        check (r.until ([&] { return r.on(); }, 300) >= 0, "readable again: back");
    }
    // ---- the key of the host's Q-Link panel (panel_key.h): the host's picture under the page is taken once, every tick the strip is compared with it, the picture is cut at the left edge of what the host drew ----
    {
        Rig r;
        auto paintBand = [&] (int left, uint32_t fill)   // the panel: a black line (2 px) at `left`, the fill right of it, over the tab's height (the sampled rows and all between them)
        {
            for (int y = 112; y < 740; ++y)
                for (int x = left; x < 1280; ++x)
                {
                    int px, py;
                    uiPointToPanel (90, x, y, 800, 1280, px, py);
                    r.frame.setPixel (px, py, x < left + 2 ? 0x000000u : fill);
                }
        };
        auto clearBand = [&]
        {
            for (int y = 112; y < 740; ++y)
                for (int x = 840; x < 1280; ++x)
                {
                    int px, py;
                    uiPointToPanel (90, x, y, 800, 1280, px, py);
                    r.frame.setPixel (px, py, 0x202020u);
                }
            r.frame.setMarker (1, true, 90);   // the marker the band covered
        };
        r.frame.setAll (true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0, "the page is on");
        check (r.ctl->keyEdge() == PanelKey::kNoKey, "no panel: not cut");
        r.run (2200);   // settled (600 ms), then the strip stands still for the stable time (250 ms): the reference is taken
        check (line (r.dir.read ("status.txt"), "key_reference_taken") == "1", "the page was whole and still: the reference of the strip was taken once (" + line (r.dir.read ("status.txt"), "key_reference_taken") + ")");
        // the panel slides in: the line at three places, then in; the page never goes, the cut follows the line at the very next tick
        bool always = true;
        const int places[4] = { 1250, 1150, 1050, 1024 };
        for (int left : places)
        {
            paintBand (left, 0x1e1d1eu);
            r.run (40);
            always = always && r.on();
            const int e = r.ctl->keyEdge();
            check (e >= left - 4 && e <= left, "the panel's line at x " + std::to_string (left) + ": the cut is at " + std::to_string (e) + " (the line's column less the grown edge)");
        }
        check (always, "the page stayed on through the slide (the right marker covered, the key says the band is there)");
        // out again
        for (int i = 3; i >= 0; --i)
        {
            clearBand();
            if (i > 0) paintBand (places[i - 1] + 40 * (3 - i), 0x1e1d1eu);
            r.run (40);
            always = always && r.on();
        }
        clearBand();
        r.run (200);
        check (always && r.on() && r.ctl->keyEdge() == PanelKey::kNoKey, "the panel gone: not cut, the page never blinked");
        check (line (r.dir.read ("status.txt"), "view_hides") == "0" || r.ctl->state() == "visible", "no hide on the way");
        // a host rectangle that is no band (a popup in the middle of the tab): not keyed (the page's markers decide)
        for (int y = 300; y < 450; ++y)
            for (int x = 900; x < 1100; ++x)
            {
                int px, py;
                uiPointToPanel (90, x, y, 800, 1280, px, py);
                r.frame.setPixel (px, py, 0x808080u);
            }
        r.run (100);
        check (r.ctl->keyEdge() == PanelKey::kNoKey, "a popup in the middle (not a band over most of the strip's height): not keyed");
        // the reference is not the picture under us any more (another host page, both markers whole and still): taken again
        for (int y = 112; y < 740; ++y)
            for (int x = 840; x < 1280; ++x)
            {
                int px, py;
                uiPointToPanel (90, x, y, 800, 1280, px, py);
                r.frame.setPixel (px, py, 0x404040u);
            }
        r.frame.setMarker (1, true, 90);
        r.run (300);
        check (r.ctl->keyEdge() == PanelKey::kNoKey, "the strip changed completely (a band from the strip's left end): no cut");
        r.run (2200);
        check (line (r.dir.read ("status.txt"), "key_reference_retakes") != "0", "... the reference was dropped (key_reference_retakes " + line (r.dir.read ("status.txt"), "key_reference_retakes") + ")");
        check (line (r.dir.read ("status.txt"), "key_reference_taken") == "2", "... and it was taken again (" + line (r.dir.read ("status.txt"), "key_reference_taken") + ")");
        // another tab: the reference goes with the view
        r.frame.setAll (false, 90);
        r.run (300);
        r.frame.setAll (true, 90);
        r.run (2200);
        check (line (r.dir.read ("status.txt"), "key_reference_taken") == "3", "the view came back: its reference is taken again (" + line (r.dir.read ("status.txt"), "key_reference_taken") + ")");
        const std::string st = r.dir.read ("status.txt");
        check (! line (st, "key_cost_ms").empty() && ! line (st, "key_pixels").empty() && ! line (st, "key_active").empty(), "the status file has key_active, key_pixels and key_cost_ms");
    }
    // ---- failures disable for good, without a retry storm ----
    {
        Rig r;
        r.plane.failSetPlaneAt = 1;
        r.frame.setAll (true, 90);
        r.run (400);
        check (r.ctl->disabled() && r.ctl->state().find ("disabled: setplane") == 0, "SETPLANE fails: disabled (" + r.ctl->state() + ")");
        const size_t calls = r.plane.calls();
        r.run (6000);
        check (r.plane.calls() == calls, "... and not a single backend call in the 6 s after that (no retry storm)");
        check (line (r.dir.read ("status.txt"), "state").find ("disabled: setplane") == 0, "the status file has the one line with the reason");
        check (! r.plane.planeOn && ! r.plane.buffersAlive, "... plane off, buffers freed");
        r.dir.touch ("off"); r.run (1500); r.dir.remove ("off"); r.run (1500);
        check (r.ctl->disabled() && ! r.on(), "the kill switch going on and off does not bring it back");
    }
    {
        Rig r;
        r.plane.failOpenAt = 1;
        r.frame.setAll (true, 90);
        r.run (3000);
        check (r.ctl->disabled() && r.plane.countOf ("open") == 1 && r.plane.countOf ("create") == 0, "open fails (no descriptor): disabled after one attempt");
        check (r.ctl->state().find ("disabled: open: injected open failure") == 0, "... with the reason: " + r.ctl->state());
    }
    {
        Rig r;
        r.plane.failCreateAt = 1;
        r.frame.setAll (true, 90);
        r.run (3000);
        check (r.ctl->disabled() && r.plane.countOf ("create") == 1 && ! r.plane.buffersAlive, "buffer creation fails: disabled, tried once, nothing left allocated");
    }
    {
        Rig r;
        r.plane.failSetPlaneAt = 6;
        r.frame.setAll (true, 90);
        r.inst.hasLive = true;
        for (int i = 0; i < 60; ++i) { r.inst.liveCutoff = 600.0f + 30.0f * (float) i; r.now += 33.0; r.ctl->tick (r.now); }
        check (r.ctl->disabled() && r.plane.presented == 5, "a failure in the middle of the run (6th SETPLANE): disabled, 5 pictures were shown");
        const size_t calls = r.plane.calls();
        for (int i = 0; i < 100; ++i) { r.inst.liveCutoff = 700.0f + (float) i; r.now += 33.0; r.ctl->tick (r.now); }
        check (r.plane.calls() == calls && ! r.plane.planeOn && ! r.plane.buffersAlive, "... no further call, plane off, buffers freed");
    }
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.frame.readable = false;
        r.run (1500);
        check (! r.ctl->disabled(), "framebuffer unreadable for 1.5 s: still waiting");
        r.run (1500);
        check (r.ctl->disabled() && r.ctl->state().find ("disabled: the framebuffer cannot be read") == 0 && r.plane.countOf ("setplane") == 0, "unreadable for 3 s: disabled (" + r.ctl->state() + ")");
    }
    // ---- the kill switch ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.dir.touch ("off");
        const double h = r.until ([&] { return ! r.on(); }, 3000);
        check (h >= 0 && h <= 1100, "kill switch file: hidden within 1.1 s (took " + std::to_string (h) + " ms)");
        check (r.ctl->state() == "hidden: kill switch" && ! r.plane.buffersAlive, "state: hidden: kill switch, buffers freed");
        const int shown = r.plane.presented;
        r.run (3000);
        check (r.plane.presented == shown, "no picture while the switch file exists");
        r.dir.remove ("off");
        const double s = r.until ([&] { return r.on(); }, 3000);
        check (s >= 0 && s <= 1200, "file removed: back within 1.2 s (took " + std::to_string (s) + " ms)");
    }
    // ---- the thread is started again after the last instance closed: its clock begins at zero, the controller must not wait for the old deadlines (found on the Key 61: after a long run the overlay
    //      of a re-created instance stayed dark for as long as the first run had lasted) ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0, "(first run: shown)");
        r.run (120000, 50.0);   // two minutes
        check (r.ctl->detach (&r.inst) == 0, "(the only instance closes)");
        r.run (500);
        check (! r.on() && r.ctl->state() == "hidden: no instance", "(no instance: plane off)");
        r.now = 0.0;   // the thread starts again: a new clock
        check (r.ctl->attach (&r.inst) == 0, "(a new instance: slot 0)");
        const double t = r.until ([&] { return r.on(); }, 1500);
        check (t >= 0 && t <= 1100, "a restarted clock (0 after 120 s): the overlay is shown within 1.1 s (took " + std::to_string (t) + " ms)");
        r.dir.touch ("off");
        const double h = r.until ([&] { return ! r.on(); }, 3000);
        check (h >= 0 && h <= 1100, "... and the kill switch works at once (took " + std::to_string (h) + " ms)");
    }
    // ---- the readout values a view is given: only when the engine's readout holds ALL of the view's targets (a view reads up to 12: the Play page's three oscillators) ----
    {
        synthyboy::LiveValues lv;
        lv.numTargets = 20;
        check (SynthyBoyPageFactory().viewTargets (&lv, 4, 12) == lv.target + 4, "the readout holds all 12 targets of a view at base 4: the values are given");
        lv.numTargets = 10;
        check (SynthyBoyPageFactory().viewTargets (&lv, 4, 12) == nullptr, "the readout is shorter than base + count (10 < 16): no values (the view would read past the engine's list)");
        check (SynthyBoyPageFactory().viewTargets (&lv, 4, 6) == lv.target + 4, "... exactly base + count is enough");
        check (SynthyBoyPageFactory().viewTargets (&lv, 4, 7) == nullptr, "... one more is not");
        lv.numTargets = synthyboy::LiveValues::kMaxTargets;
        check (SynthyBoyPageFactory().viewTargets (&lv, synthyboy::LiveValues::kMaxTargets - 4, 12) == nullptr, "a view whose targets end past the array (base + count > kMaxTargets): no values");
        check (SynthyBoyPageFactory().viewTargets (&lv, -4, 4) == nullptr, "a negative base: no values");
        check (SynthyBoyPageFactory().viewTargets (&lv, 0, 0) == nullptr, "a view without targets gets none");
        std::vector<int> all;
        SynthyBoyPageFactory::targetsOfAllPages (all);
        check (! all.empty() && all.size() <= static_cast<size_t> (synthyboy::LiveValues::kMaxTargets), "the targets of all views together fit the engine's list (" + std::to_string (all.size()) + " of " + std::to_string (synthyboy::LiveValues::kMaxTargets) + ")");
    }
    // ---- the last instance closes: the mappings and dma-buf descriptors of the host's framebuffers that the frame source kept are let go (they would hold the host's buffers until the process ends) ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0, "(release: shown)");
        check (r.frame.releases == 0, "(release: nothing is let go while an instance lives)");
        check (r.ctl->detach (&r.inst) == 0, "(release: the last instance closes)");
        r.ctl->shutdown();
        check (r.frame.releases >= 1, "the frame source is told to let go of its cached framebuffers when the overlay shuts down");
    }
    // ---- a slot is given to a NEW instance: no view object of the earlier instance is reused, whatever page is on screen when the new one is first looked at ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0, "(slot reuse: shown)");
        r.run (300);
        const IView* before = r.ctl->currentView();
        check (before != nullptr, "(slot reuse: the Filter 1 view is there)");
        // the page changes to another view, then the instance closes and a new one gets slot 0 before the first view is looked at again
        r.frame.setView (0, false, 90);
        r.frame.setView (1, true, 90);
        check (r.until ([&] { return r.ctl->currentView() != before && r.ctl->currentView() != nullptr; }, 500) >= 0, "(slot reuse: another view is on screen)");
        r.ctl->detach (&r.inst);
        FakeInstance successor;
        successor.set ("filter1.cutoff", 1000.0f);
        successor.set ("filter1.resonance", 30.0f);
        check (r.ctl->attach (&successor) == 0, "(slot reuse: the successor got slot 0)");
        r.frame.setView (1, false, 90);
        r.frame.setView (0, true, 90);
        check (r.until ([&] { return r.on() && r.ctl->currentView() != nullptr; }, 800) >= 0, "(slot reuse: the first view is on screen again)");
        r.run (200);
        check (r.ctl->currentView() != before, "a slot reused by a new instance gets a NEW view object (the cache of the earlier instance's page is not carried over)");
        // and the same when the page stays on screen and the instance is replaced between two ticks
        const IView* second = r.ctl->currentView();
        r.ctl->detach (&successor);
        FakeInstance third;
        third.set ("filter1.cutoff", 1000.0f);
        third.set ("filter1.resonance", 30.0f);
        check (r.ctl->attach (&third) == 0, "(slot reuse: third instance, slot 0, no tick between)");
        r.run (300);
        check (r.ctl->currentView() != second, "... also when the replacement happens between two ticks with the same page on screen");
        r.ctl->detach (&third);
    }
    // ---- the slot table and the colour table ----
    {
        bool maps = true, tolerant = true, distinct = true;
        for (int sl = 0; sl < layout::kMaxInstances; ++sl)
        {
            const uint32_t c = layout::kInstanceSlotRgb[sl];
            maps = maps && classifyInstanceRgb (c) == sl && slotOfInstanceFrame (instanceFrameOfSlot (sl)) == sl && instanceFrameRgb (instanceFrameOfSlot (sl)) == c && instanceFrameOfSlot (sl) == sl + 1
                   && static_cast<int> (instanceParamValue (sl) * static_cast<float> (layout::kInstanceSteps)) == sl + 1;
            tolerant = tolerant && classifyInstanceRgb (c + 0x020202) == sl && classifyInstanceRgb (c - 0x020202) == sl && classifyInstanceRgb (c + 0x030000) == kSlotUnknown;
            for (int o = sl + 1; o < layout::kMaxInstances; ++o)
                distinct = distinct && (layout::kInstanceSlotRgb[o] != c);
        }
        check (maps, "the 16 slots: frame slot + 1, the colour table maps back to the slot, the parameter value is frame / 32 (exact)");
        check (tolerant, "a colour within 2 per channel is the slot, 3 away is nothing");
        check (distinct, "no two slots share a colour");
        check (classifyInstanceRgb (layout::kInstanceNoneRgb) == kSlotNone && instanceFrameRgb (layout::kMaxInstances + 1) == layout::kInstanceNoneRgb && instanceFrameOfSlot (-1) == 17 && instanceFrameOfSlot (16) == 17
               && instanceParamValue (-1) == 17.0f / 32.0f, "an instance without a slot shows frame 17: the none colour");
        bool blank = classifyInstanceRgb (layout::kInstanceBlankRgb) == kSlotUnknown && instanceFrameRgb (0) == layout::kInstanceBlankRgb;
        for (int f = 18; f < layout::kInstanceFrames; ++f)
            blank = blank && instanceFrameRgb (f) == layout::kInstanceBlankRgb;
        check (blank && instanceParamValue (-1) != 0.0f, "frame 0 and the frames after 17 are blank: no valid colour");
        bool exact = true;
        for (int f = 0; f < layout::kInstanceFrames; ++f)
            exact = exact && static_cast<int> (static_cast<float> (f) / 32.0f * 32.0f) == f && static_cast<float> (f) / 32.0f * 32.0f == static_cast<float> (f);
        check (exact, "every value frame / 32 times 32 is the frame again in a float (the host picks the frame by a multiplication)");
        bool apart = classifyInstanceRgb (layout::kBackgroundRgb) == kSlotUnknown;
        for (int v = 0; v < legacy::kNumViews; ++v)
            for (int m = 0; m < 2; ++m)
                apart = apart && classifyInstanceRgb (legacy::kViews[v].markers[m].rgb) == kSlotUnknown && classifyInstanceRgb (legacy::kViews[v].markers[m].cover) == kSlotUnknown;
        check (apart, "the background, the marker colours and the covers are not instance colours");
        FakeInstance a[18];
        InstanceSlots table;
        bool inOrder = true;
        for (int i = 0; i < 16; ++i)
            inOrder = inOrder && table.acquire (&a[i]) == i;
        check (inOrder && table.slotted() == 16 && table.aliveMask() == 0xffffu, "16 instances get slots 0 .. 15 in order");
        check (table.acquire (&a[16]) == -1 && table.acquire (&a[17]) == -1 && table.total() == 18 && table.slotted() == 16, "the 17th and 18th get no slot but are counted");
        check (table.release (&a[5]) && table.at (5) == nullptr && table.slotOf (&a[6]) == 6 && table.slotOf (&a[15]) == 15, "a closed instance frees its slot, the others keep theirs");
        check (table.acquire (&a[5]) == 5 && table.slotOf (&a[5]) == 5, "the freed slot is the next one given out (reused only after its instance closed)");
        check (table.release (&a[16]) && table.total() == 17 && ! table.release (&a[16]), "an instance without a slot can leave, once");
    }
    // ---- several instances: the instance whose slot colour is on screen is the one drawn ----
    {
        // what a single instance with these knobs looks like (the picture and the hash), for comparison
        auto reference = [&] (float cutoff, float res, uint32_t& hash, std::vector<uint32_t>& picture)
        {
            Rig q;
            q.inst.set ("filter1.cutoff", cutoff);
            q.inst.set ("filter1.resonance", res);
            q.frame.setAll (true, 90);
            q.until ([&] { return q.on(); }, 500);
            q.run (300);
            hash = q.ctl->currentView()->hash();
            picture = q.plane.shownCopy;
        };
        uint32_t h0 = 0, h1 = 0, h2 = 0;
        std::vector<uint32_t> p0, p1, p2;
        reference (1000.0f, 30.0f, h0, p0);
        reference (4000.0f, 70.0f, h1, p1);
        reference (200.0f, 5.0f, h2, p2);
        check (h0 != h1 && h1 != h2 && h0 != h2 && p0 != p1 && p1 != p2, "(the three parameter sets draw three different pictures)");

        Rig r;   // slot 0: cutoff 1000, resonance 30
        FakeInstance second, third;
        second.set ("filter1.cutoff", 4000.0f);
        second.set ("filter1.resonance", 70.0f);
        third.set ("filter1.cutoff", 200.0f);
        third.set ("filter1.resonance", 5.0f);
        check (r.ctl->attach (&second) == 1 && r.ctl->attach (&third) == 2 && r.ctl->slotOf (&r.inst) == 0, "three instances get the slots 0, 1, 2");
        r.frame.setView (0, true, 90);   // markers and the instance picture of slot 0
        check (r.until ([&] { return r.on(); }, 500) >= 0 && r.ctl->activeSlot() == 0 && r.ctl->state() == "visible", "slot 0 colour: visible, the data of slot 0");
        r.run (300);
        check (r.ctl->currentView()->hash() == h0 && r.plane.shownCopy == p0, "... the picture is the one of instance 0's knobs, pixel for pixel");
        r.frame.setViewInstance (0, 1, true, 90);
        double t = r.until ([&] { return r.ctl->activeSlot() == 1 && r.ctl->currentView()->hash() == h1; }, 1000);
        check (t >= 0 && t <= 200, "the colour changes to slot 1: its data within 200 ms (took " + std::to_string (t) + " ms)");
        r.run (300);
        check (r.plane.shownCopy == p1 && r.on(), "... the picture on the plane is instance 1's, pixel for pixel");
        r.frame.setViewInstance (0, 2, true, 90);
        t = r.until ([&] { return r.ctl->activeSlot() == 2 && r.ctl->currentView()->hash() == h2; }, 1000);
        check (t >= 0 && t <= 200, "slot 2: its data within 200 ms (took " + std::to_string (t) + " ms)");
        r.run (300);
        check (r.plane.shownCopy == p2, "... the picture is instance 2's");
        r.inst.set ("filter1.cutoff", 8000.0f);   // another instance changes: the picture on the plane does not
        const int shown = r.plane.presented;
        r.run (1500);
        check (r.plane.presented == shown && r.plane.shownCopy == p2, "a knob of an instance that is not on screen changes nothing on the plane");
        r.inst.set ("filter1.cutoff", 1000.0f);
        r.frame.setViewInstance (0, 0, true, 90);
        t = r.until ([&] { return r.ctl->activeSlot() == 0 && r.ctl->currentView()->hash() == h0; }, 1000);
        r.run (300);
        check (t >= 0 && t <= 200 && r.plane.shownCopy == p0, "back to slot 0: its data again (no trace of the other two)");
        const std::string st = r.dir.read ("status.txt");
        check (line (st, "instances") == "3" && line (st, "instance_slot") == "0" && line (st, "instance_slots_alive") == "0007" && line (st, "instance_slot_read") == "0" && line (st, "instances_with_slot") == "3", "the status file has the instance count, the slot shown and the slots alive");
        check (line (st, "view") == "filter1" && line (st, "instance_pixel_read") == "1414d8", "... and the colour read (1414d8 = slot 0)");

        // the instance whose page is on screen closes: hidden within one tick, nothing is used afterwards (run under AddressSanitizer: the object is freed)
        auto heap = std::make_unique<FakeInstance>();
        heap->set ("filter1.cutoff", 3000.0f);
        const int hs = r.ctl->attach (heap.get());
        check (hs == 3, "a fourth instance gets slot 3");
        r.frame.setViewInstance (0, 3, true, 90);
        check (r.until ([&] { return r.ctl->activeSlot() == 3; }, 500) >= 0, "its colour: it is the one on screen");
        r.ctl->detach (heap.get());
        const double gone = r.until ([&] { return ! r.on(); }, 500);
        check (gone >= 0 && gone <= 20, "the instance of the page on screen closes: the overlay is hidden within one tick (took " + std::to_string (gone) + " ms)");
        heap.reset();
        r.run (1500);
        check (! r.on() && r.ctl->state().rfind ("hidden: instance", 0) == 0, "... and stays hidden while its colour is still on screen (" + r.ctl->state() + ")");
        // the slot is free again: a new instance gets it, and it is drawn from its own data
        FakeInstance reuse;
        reuse.set ("filter1.cutoff", 4000.0f);
        reuse.set ("filter1.resonance", 70.0f);
        check (r.ctl->attach (&reuse) == 3, "the freed slot 3 is reused by the next instance");
        r.until ([&] { return r.on(); }, 500);
        r.run (300);
        check (r.on() && r.plane.shownCopy == p1 && r.ctl->activeSlot() == 3, "the new instance on slot 3 is drawn from its own data");
        r.ctl->detach (&reuse);
        r.frame.setViewInstance (0, 0, true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0, "(back to slot 0)");

        // a valid colour of a slot nobody holds
        r.frame.setViewInstance (0, 7, true, 90);
        const unsigned long before = r.ctl->rereports();
        const double h = r.until ([&] { return ! r.on(); }, 500);
        check (h >= 0 && h <= 120 && r.ctl->state() == "hidden: instance slot 7 is not alive", "the colour of slot 7, which is not alive: hidden (" + r.ctl->state() + ")");
        r.run (2000);
        check (r.ctl->rereports() == before, "... a valid colour of a dead slot does not ask for new reports");
        r.ctl->detach (&second);
        r.ctl->detach (&third);
    }
    // ---- the instance picture is not readable: hidden, and the instances are asked to report again (rate limited) ----
    {
        Rig r;
        FakeInstance second;
        r.ctl->attach (&second);
        r.frame.setView (0, true, 90);
        r.until ([&] { return r.on(); }, 500);
        check (r.ctl->rereports() == 0 && r.inst.reportRequests == 0, "a valid colour: nobody was asked to report");
        r.frame.setViewInstance (0, kSlotUnknown, true, 90);   // the blank frame: the host has not drawn this instance's value yet
        const double h = r.until ([&] { return ! r.on(); }, 500);
        check (h >= 0 && h <= 120 && r.ctl->state() == "hidden: instance of the page not identified", "the blank frame: hidden (" + r.ctl->state() + ")");
        check (r.ctl->rereports() == 1 && r.inst.reportRequests == 1 && second.reportRequests == 1, "... and both instances were asked to report their slot once");
        r.run (1250);
        check (r.ctl->rereports() >= 2 && r.ctl->rereports() <= 3 && r.inst.reportRequests == static_cast<int> (r.ctl->rereports()), "1.25 s later: 2 or 3 requests in all, not one per check (" + std::to_string (r.ctl->rereports()) + ")");
        r.run (5000);
        check (r.ctl->rereports() <= 5, "6.5 s of a missing colour: at most 5 requests, the gap grows (" + std::to_string (r.ctl->rereports()) + ")");
        {
            const unsigned long inFile = std::stoul (line (r.dir.read ("status.txt"), "instance_rereports"));   // written once a second: at most a second behind
            check (! r.on() && inFile >= 3 && inFile <= r.ctl->rereports(), "... still hidden; the status file counts the requests (" + std::to_string (inFile) + ")");
        }
        const unsigned long n = r.ctl->rereports();
        r.frame.setViewInstance (0, 0, true, 90);   // the host has now drawn the value
        const double s = r.until ([&] { return r.on(); }, 500);
        check (s >= 0 && s <= 120 && r.ctl->activeSlot() == 0, "a valid colour: shown within 120 ms (took " + std::to_string (s) + " ms)");
        r.run (3000);
        check (r.ctl->rereports() == n, "... and no more requests");
        // no instance picture at all, or a colour that is none of the table: the same
        r.frame.setViewInstanceRgb (0, 0, false, 90);
        check (r.until ([&] { return ! r.on(); }, 500) >= 0 && r.ctl->rereports() == n + 1, "no instance picture on the screen (a skin without it): hidden, a request is made at once (the gap starts again)");
        r.frame.setViewInstanceRgb (0, 0x9a3b00, true, 90);
        r.run (1500);
        check (! r.on() && r.ctl->rereports() >= n + 2, "a colour that is not in the table: hidden, requests go on");
        // a page without markers (another tab) asks for nothing
        r.frame.setView (0, false, 90);
        r.run (200);
        const unsigned long m = r.ctl->rereports();
        r.run (5000);
        check (r.ctl->rereports() == m && r.ctl->state() == "hidden: no view on screen", "no markers on screen: nobody is asked");
        // a half read colour (one corner differs) is not a colour
        r.frame.setView (0, true, 90);
        r.until ([&] { return r.on(); }, 500);
        const layout::Rect& ir = legacy::kViews[0].instance;
        int px, py;
        uiPointToPanel (90, ir.x + 1, ir.y + 1, 800, 1280, px, py);
        r.frame.setPixel (px, py, 0x3e14d8);
        check (r.until ([&] { return ! r.on(); }, 500) >= 0, "one sample of five in another slot's colour: not identified, hidden");
        r.ctl->detach (&second);
    }
    // ---- more than 16 instances ----
    {
        Rig r;
        std::vector<std::unique_ptr<FakeInstance>> many;
        for (int i = 1; i < 17; ++i)
        {
            many.push_back (std::make_unique<FakeInstance>());
            many.back()->set ("filter1.cutoff", 100.0f + 400.0f * static_cast<float> (i));
        }
        bool slots = true;
        for (int i = 1; i < 16; ++i)
            slots = slots && r.ctl->attach (many[static_cast<size_t> (i) - 1].get()) == i;
        check (slots, "instances 2 .. 16 get slots 1 .. 15");
        check (r.ctl->attach (many[15].get()) == -1, "the 17th instance gets no slot");
        r.frame.setViewInstance (0, 15, true, 90);
        r.frame.setViewMarker (0, 0, true, 90);
        r.frame.setViewMarker (0, 1, true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0 && r.ctl->activeSlot() == 15, "the 16th instance (slot 15) is on screen");
        r.frame.setViewInstance (0, kSlotNone, true, 90);   // what the 17th instance reports
        const double h = r.until ([&] { return ! r.on(); }, 500);
        check (h >= 0 && r.ctl->state() == "hidden: instance has no slot (more than 16 instances)", "the 17th instance's page: hidden (" + r.ctl->state() + ")");
        const unsigned long n = r.ctl->rereports();
        r.run (2000);
        check (r.ctl->rereports() == n, "... without asking for reports (the none colour is a valid answer)");
        check (line (r.dir.read ("status.txt"), "instances") == "17" && line (r.dir.read ("status.txt"), "instances_with_slot") == "16", "the status file counts 17 instances, 16 with a slot");
        r.ctl->detach (many[15].get());
        for (int i = 1; i < 16; ++i)
            r.ctl->detach (many[static_cast<size_t> (i) - 1].get());
        check (r.ctl->slotOf (&r.inst) == 0, "(the first instance kept slot 0 all along)");
    }
    // ---- one instance: its colour is slot 0 ----
    {
        Rig r;
        r.frame.setView (0, true, 90);
        check (r.until ([&] { return r.on(); }, 300) >= 0 && r.ctl->state() == "visible", "one instance: shown as before, with slot 0's colour");
        r.frame.setViewInstance (0, 1, true, 90);
        check (r.until ([&] { return ! r.on(); }, 300) >= 0, "one instance and the colour of slot 1: hidden (that page is not ours)");
        r.frame.setViewInstance (0, 0, true, 90);
        check (r.until ([&] { return r.on(); }, 300) >= 0, "slot 0 again: shown");
        check (r.ctl->detach (&r.inst) == 0, "the last instance leaves");
        r.run (100);
        check (! r.on() && r.ctl->state() == "hidden: no instance", "no instance: hidden");
    }
    // ---- close while visible ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.ctl->detach (&r.inst);
        r.ctl->shutdown();
        const auto log = r.plane.callLog();
        size_t lastDisable = 0, lastDestroy = 0;
        for (size_t i = 0; i < log.size(); ++i) { if (log[i] == "disable") lastDisable = i; if (log[i] == "destroy") lastDestroy = i; }
        check (! r.plane.planeOn && ! r.plane.buffersAlive, "close while visible: plane off and buffers freed");
        check (lastDisable < lastDestroy && lastDestroy == log.size() - 1, "... in the right order: the plane is taken off before the buffers go");
        check (line (r.dir.read ("status.txt"), "state") == "hidden: no instance", "... and the status file says so");
        r.ctl->shutdown();
        check (r.plane.callLog().size() == log.size(), "a second shutdown (the exit handler) does nothing");
    }
    // ---- the rotation file ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.dir.write ("rot", "270\n");
        r.run (1300);
        check (! r.on() && r.ctl->rotation() == 270, "rotation file 270: the overlay re-reads it within a second; the markers are not where the 270 mapping looks, so it is hidden");
        r.frame.setAll (false, 90);
        r.frame.setAll (true, 270);
        check (r.until ([&] { return r.on(); }, 400) >= 0, "markers drawn for 270: shown");
        check (r.plane.lastRect == uiRectToPanel (270, curveUi, 800, 1280), "... at the 270 rectangle");
        const Rect rr = r.plane.lastRect;
        check (rr.x == 800 - (curveUi.y + curveUi.h) && rr.y == curveUi.x, "... which is x = panelW - (y + h), y = x of the UI");
        check (line (r.dir.read ("status.txt"), "rotation") == "270", "the status file says 270");
        r.dir.write ("rot", "banana");
        r.run (1300);
        check (r.ctl->rotation() == 90, "an unreadable rotation file is no override: the rotation found from the markers (90, the first view) is back");
    }
    // ---- the look file ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.run (1300);
        check (line (r.dir.read ("status.txt"), "ui_look") == "1" && &synthyboy_mpc::ui::theme::L() == &synthyboy_mpc::ui::theme::kLook1, "without a look file the UI has look 1");
        r.dir.write ("look", "3\n");
        r.run (1300);
        check (line (r.dir.read ("status.txt"), "ui_look") == "3" && &synthyboy_mpc::ui::theme::L() == &synthyboy_mpc::ui::theme::kLook3, "the look file selects look 3 within a second");
        r.dir.write ("look", "banana");
        r.run (1300);
        check (line (r.dir.read ("status.txt"), "ui_look") == "1", "an unreadable look file is no choice: look 1 is back");
    }
    // ---- the level of detail ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.until ([&] { return r.on(); }, 300);
        r.dir.write ("detail", "full\n");
        r.run (1300);
        check (line (r.dir.read ("status.txt"), "ui_detail") == "full" && line (r.dir.read ("status.txt"), "ui_detail_source") == "file" && synthyboy_mpc::ui::theme::detailLevel() == 1, "the detail file says full: level full");
        r.dir.write ("detail", "2\n");
        r.run (1300);
        check (line (r.dir.read ("status.txt"), "ui_detail") == "reduced" && synthyboy_mpc::ui::theme::detailLevel() == 2, "the detail file says 2: level reduced");
        r.dir.write ("detail", "auto\n");
        r.run (1300);
        check (line (r.dir.read ("status.txt"), "ui_detail_source") == "auto" && line (r.dir.read ("status.txt"), "ui_detail") == "reduced", "automatic without the GPU helper (the CPU draws): reduced");
        synthyboy_mpc::ui::theme::setDetail (1);
    }
    // ---- the curve dump on request ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        r.dir.touch ("dump");
        r.run (2200);
        const std::string d = r.dir.read ("curve.txt");
        check (d.find ("# view filter1: index shown_db exact_db") == 0 && std::count (d.begin(), d.end(), '\n') == 257, "the dump file has the 256 values");
    }
    // ---- the plugin-drawn pages of an instance: main -> full(element) -> detail(page) ..., closed when the Play page is no longer on screen; per instance, never saved ----
    {
        const int play1 = legacy::indexOf ("play1"), play2 = legacy::indexOf ("play2"), filter1 = 0;
        check (legacy::kViews[play1].kind == layout::kKindPlay && legacy::kViews[play2].kind == layout::kKindPlay && legacy::kViews[filter1].kind == layout::kKindFilter, "(the view numbers of the test)");
        {
            UiStack u;
            check (u.isMain() && u.level() == UiStack::Level::main && u.depth() == 0 && u.element() == -1 && u.top() == -1 && ! u.back() && ! u.closeAll(), "a stack starts in main; back and closeAll of main change nothing");
            check (! u.openDetail (4) && ! u.openFull (-1) && u.changes() == 0, "a detail page needs a full page under it; a negative id is no page");
            check (u.openFull (5) && u.level() == UiStack::Level::full && u.element() == 5 && u.depth() == 1, "openFull: full(5)");
            check (u.openFull (5) && u.changes() == 1, "the same full page again changes nothing");
            check (u.openDetail (11) && u.openDetail (12) && u.level() == UiStack::Level::detail && u.depth() == 3 && u.top() == 12 && u.id (0) == 5 && u.id (1) == 11 && u.id (2) == 12 && u.id (3) == -1, "detail pages stack on the full page and on each other");
            check (u.back() && u.top() == 11 && u.back() && u.level() == UiStack::Level::full && u.back() && u.isMain() && ! u.back(), "back steps down one page at a time, full -> main");
            u.openFull (1);
            u.openDetail (2);
            check (u.openFull (9) && u.depth() == 1 && u.element() == 9, "another full page replaces the whole stack");
            u.openDetail (1);
            check (u.closeAll() && u.isMain() && ! u.closeAll(), "closeAll goes to main at once");
            bool ok = true;
            u.openFull (0);
            for (int i = 1; i < UiStack::kMaxDepth; ++i) ok = ok && u.openDetail (i);
            check (ok && u.depth() == UiStack::kMaxDepth && ! u.openDetail (99), "the depth is limited: the page over the limit is refused and nothing breaks");
        }
        Rig r;
        const int slot = r.ctl->slotOf (&r.inst);
        check (r.ctl->uiState (slot).isMain() && ! r.ctl->openDetail (slot, 1) && ! r.ctl->openFull (-1, 1) && ! r.ctl->openFull (layout::kMaxInstances, 1) && ! r.ctl->openFull (5, 1), "a new instance is in main; no page for a slot that is not an instance");
        r.frame.setView (play1, true, 90);
        check (r.until ([&] { return r.on(); }, 500) >= 0 && r.ctl->currentIndex() == play1, "the Play page is on screen");
        check (r.ctl->openFull (slot, 3) && r.ctl->openDetail (slot, 8), "openFull / openDetail through the controller");
        r.run (1200);
        std::string st = r.dir.read ("status.txt");
        check (line (st, "ui_state") == "detail" && line (st, "ui_depth") == "2" && line (st, "ui_auto_closes") == "0", "the status file (the readout path) says detail, depth 2");
        check (r.ctl->back (slot) && r.ctl->uiState (slot).level() == UiStack::Level::full, "back: full");
        // the sub-tab of the Play page changes (the Play key pressed again): still the Play page, the stack stays
        r.frame.setView (play1, false, 90);
        r.frame.setView (play2, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == play2 && r.ctl->uiState (slot).level() == UiStack::Level::full && r.ctl->uiState (slot).element() == 3, "another sub-tab of the Play page: the stack stays");
        // the instance picture goes blank (the host redraws): the page is there, the instance is not told: the stack stays
        r.frame.setViewInstanceRgb (play2, layout::kInstanceBlankRgb, true, 90);
        r.run (500);
        check (r.ctl->uiState (slot).level() == UiStack::Level::full, "the instance picture is blank: the page did not leave, the stack stays");
        r.frame.setViewInstance (play2, slot, true, 90);
        r.run (300);
        // the screen is not readable: nothing is known about the page, the stack stays
        const bool wasReadable = r.frame.readable;
        r.frame.readable = false;
        r.run (500);
        r.frame.readable = wasReadable;
        r.run (300);
        check (r.ctl->uiState (slot).level() == UiStack::Level::full && r.ctl->state() != "disabled", "an unreadable framebuffer for half a second: the stack stays");
        // the user presses Mixer (any other tab): the whole stack closes, and coming back shows main
        r.ctl->openDetail (slot, 4);
        r.frame.setView (play2, false, 90);
        r.frame.setView (filter1, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == filter1 && r.ctl->uiState (slot).isMain(), "another tab on screen: the stack is closed (main)");
        r.run (1200);
        st = r.dir.read ("status.txt");
        check (line (st, "ui_auto_closes") == "1" && line (st, "ui_state") == "main", "... counted in the status file");
        r.frame.setView (filter1, false, 90);
        r.frame.setView (play1, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == play1 && r.ctl->uiState (slot).isMain(), "back on the Play page: main, nothing is remembered");
        // no view of the skin on screen at all (a page without markers): closed too
        r.ctl->openFull (slot, 2);
        r.frame.setView (play1, false, 90);
        r.run (500);
        check (r.ctl->uiState (slot).isMain(), "no page of the skin on screen: closed");

        // two instances: the stack belongs to the instance whose page it is
        FakeInstance other;
        check (r.ctl->attach (&other) == 1, "(a second instance, slot 1)");
        r.frame.setView (play1, true, 90);
        r.frame.setViewInstance (play1, 0, true, 90);
        r.run (500);
        r.ctl->openFull (0, 2);
        r.ctl->openFull (1, 7);   // an instance whose page is not on screen: its stack does not live (the next check closes it)
        r.run (300);
        check (r.ctl->uiState (0).element() == 2 && r.ctl->uiState (1).isMain(), "instance 0's page is on screen: its stack stays, the one opened for instance 1 is closed by the next check");
        r.frame.setViewInstance (play1, 1, true, 90);
        r.run (300);
        r.ctl->openFull (1, 7);
        r.ctl->openDetail (1, 9);
        r.run (300);
        check (r.ctl->uiState (0).isMain() && r.ctl->uiState (1).depth() == 2 && r.ctl->uiState (1).element() == 7, "instance 1's Play page on screen: instance 0's stack is closed, instance 1's stays");
        r.frame.setView (play1, false, 90);
        r.frame.setView (filter1, true, 90);
        r.run (500);
        check (r.ctl->uiState (1).isMain(), "another tab: instance 1's stack is closed as well");
        // not saved, not inherited: a slot given to a new instance starts in main
        r.frame.setView (filter1, false, 90);
        r.frame.setView (play1, true, 90);
        r.frame.setViewInstance (play1, 1, true, 90);
        r.run (300);
        r.ctl->openFull (1, 4);
        r.ctl->detach (&other);
        FakeInstance again;
        check (r.ctl->attach (&again) == 1 && r.ctl->uiState (1).isMain(), "a closed instance leaves no stack: the next instance in its slot starts in main");
        r.ctl->detach (&again);
    }
    // ---- the touch screen: events of the touch input go to the plugin-drawn pages of the Play page (the compact view's full-screen button opens the full screen, the close button closes it) ----
    {
        struct FakeTouch final : ITouchInput
        {
            std::vector<TouchEvent> queue;
            int panelW = 0, panelH = 0, rotation = 0, configured = 0;
            void configure (int w, int h, int r) override { panelW = w; panelH = h; rotation = r; ++configured; }
            int poll (const std::function<void (const TouchEvent&)>& h) override { for (const TouchEvent& e : queue) h (e); const int n = static_cast<int> (queue.size()); queue.clear(); return n; }
        } touch;
        const int play1 = viewIndex (legacy::kViews, "play1"), filter1 = 0;
        Rig r (true, 10.0, &touch);
        r.inst.playSel[0].store (2);
        r.frame.setView (play1, true, 90);
        check (r.until ([&] { return r.on() && r.ctl->currentIndex() == play1; }, 800) >= 0, "(the Play page is on screen)");
        r.run (100);
        check (touch.configured > 0 && touch.panelW == 800 && touch.panelH == 1280 && touch.rotation == 90, "(" + std::to_string (touch.configured) + " " + std::to_string (touch.panelW) + "x" + std::to_string (touch.panelH) + " " + std::to_string (touch.rotation) + ") the touch input is told the panel and the rotation of the discovery, no number of its own");
        auto* pv = static_cast<PlayView*> (const_cast<IView*> (r.ctl->viewByName ("play1")));
        r.run (200);
        check (pv != nullptr && pv->oscUi() != nullptr, "the Play view has the oscillator UI");
        auto tap = [&] (int x, int y)
        {
            TouchEvent d; d.kind = TouchEvent::Down; d.x = x; d.y = y; d.timeUs = 1;
            TouchEvent u = d; u.kind = TouchEvent::Up;
            touch.queue.push_back (d);
            r.run (60);
            touch.queue.push_back (u);
            r.run (200);
        };
        const synthyboy_mpc::ui::Rect fb = pv->oscUi()->compact().full.rect();
        tap (fb.x + fb.w / 2, fb.y + fb.h / 2);
        const int slot = r.ctl->slotOf (&r.inst);
        check (r.ctl->uiState (slot).level() == UiStack::Level::full && r.inst.uiFullOpen.load(), "a tap on the full-screen button opens the full screen and tells the instance (the host widgets hide)");
        check (r.ctl->touchEvents() == 2 && r.ctl->touchTaken() == 2, "the controller counted the two events, both taken");
        r.run (1200);
        const std::string st = r.dir.read ("status.txt");
        check (line (st, "ui_state") == "full" && line (st, "touch_events") == "2", "the status file says full and counts the touch events");
        // a knob drag on the full screen writes a parameter
        const synthyboy_mpc::ui::Rect lv = pv->oscUi()->full().top.pos.rect();   // (LEVEL is read-only on the full screen now: its home is the Mixer; the position knob WT POS is the knob to drag)
        const float before = r.inst.get ("osc1.frame");
        TouchEvent d; d.kind = TouchEvent::Down; d.x = lv.x + lv.w / 2; d.y = lv.y + lv.h / 2;
        touch.queue.push_back (d);
        r.run (30);
        for (int i = 1; i <= 8; ++i) { TouchEvent m = d; m.kind = TouchEvent::Move; m.y -= i * 8; touch.queue.push_back (m); r.run (30); }
        TouchEvent u = d; u.kind = TouchEvent::Up; u.y -= 64;
        touch.queue.push_back (u);
        r.run (200);
        check (r.inst.get ("osc1.frame") > before + 10.0f && r.inst.uiWrites.load() > 0, "a drag on WT POS of the full screen wrote osc1.frame through the instance");
        // the blank instance frame the plugin reports while the full screen is open does not hide the view
        r.frame.setViewInstanceRgb (play1, layout::kInstanceBlankRgb, true, 90);
        r.run (500);
        check (r.ctl->currentIndex() == play1 && r.ctl->uiState (slot).level() == UiStack::Level::full, "the full screen open: a blank instance picture (the frame the skin hides the host widgets at) keeps the view and the stack");
        // navigating away closes it, a touch on another tab goes nowhere
        r.frame.setView (play1, false, 90);
        r.frame.setView (filter1, true, 90);
        r.run (500);
        const unsigned long taken = r.ctl->touchTaken();
        tap (100, 300);
        check (r.ctl->uiState (slot).isMain() && r.ctl->touchTaken() == taken, "another tab: the stack is closed and a touch is not taken by the plugin's pages");
        check (! r.inst.uiFullOpen.load(), "the stack closed by the page leaving: the instance is told at once (it stops reporting the frame the host widgets are hidden at: coming back finds them)");
    }
    // ---- a hidden overlay takes no touch (a host dialog over the page): the page check of the SAME step judges a touch; the host's gesture is the host's; a pop-up never stays open under a hidden page ----
    {
        struct FakeTouch final : ITouchInput
        {
            std::vector<TouchEvent> queue;
            void configure (int, int, int) override {}
            int poll (const std::function<void (const TouchEvent&)>& h) override { for (const TouchEvent& e : queue) h (e); const int n = static_cast<int> (queue.size()); queue.clear(); return n; }
        } touch;
        const int play1 = viewIndex (legacy::kViews, "play1");
        Rig r (true, 10.0, &touch);
        r.inst.playSel[0].store (2);
        r.frame.setView (play1, true, 90);
        check (r.until ([&] { return r.on() && r.ctl->currentIndex() == play1; }, 800) >= 0, "(hidden touch: the Play page is on screen)");
        r.run (300);
        auto* pv = static_cast<PlayView*> (const_cast<IView*> (r.ctl->viewByName ("play1")));
        const synthyboy_mpc::ui::Rect sel = pv->oscUi()->compact().basic.selector.rect();
        const int cx = sel.x + sel.w / 2, cy = sel.y + sel.h / 2;
        auto ev = [&] (TouchEvent::Kind k) { TouchEvent e; e.kind = k; e.x = cx; e.y = cy; e.timeUs = 1; touch.queue.push_back (e); };
        auto tap = [&] { ev (TouchEvent::Down); ev (TouchEvent::Up); r.run (30); r.run (200); };
        // (b) the same tap while visible is taken and opens the list
        const unsigned long taken0 = r.ctl->touchTaken();
        tap();
        check (r.ctl->touchTaken() > taken0 && pv->oscUi()->popupOpen(), "(b) visible: the tap is taken and opens the oscillator type list");
        r.ctl->closeAll (r.ctl->slotOf (&r.inst));
        pv->oscUi()->closePopups();
        // (a) a host dialog hides the view; the check has seen it: a tap takes nothing, opens nothing, writes nothing
        r.frame.setView (play1, false, 90);
        check (r.until ([&] { return r.ctl->currentIndex() < 0; }, 600) >= 0, "(a) the host dialog hides the view");
        const unsigned long taken1 = r.ctl->touchTaken(), events1 = r.ctl->touchEvents();
        const auto writes1 = r.inst.uiWrites.load();
        const int selBefore = r.inst.playSel[0].load();
        tap();
        tap();
        check (r.ctl->touchTaken() == taken1 && r.ctl->touchEvents() == events1 && ! pv->oscUi()->popupOpen() && r.inst.uiWrites.load() == writes1 && r.inst.playSel[0].load() == selBefore, "(a) hidden: the taps are not taken, not counted, open no list and change no state");
        // the taps of the host's dialog are thrown away, not kept for later: the page comes back and nothing of them arrives
        r.frame.setView (play1, true, 90);
        r.run (300);
        check (r.ctl->currentIndex() == play1 && r.ctl->touchTaken() == taken1 && ! pv->oscUi()->popupOpen(), "(a) the page is back: nothing of the host's taps was kept and replayed");
        // a finger that went down on the host and is still down when the page is back is the host's until it is up
        r.frame.setView (play1, false, 90);
        r.until ([&] { return r.ctl->currentIndex() < 0; }, 600);
        ev (TouchEvent::Down);
        r.run (150);
        r.frame.setView (play1, true, 90);
        r.until ([&] { return r.ctl->currentIndex() == play1; }, 600);
        ev (TouchEvent::Up);
        r.run (100);
        check (r.ctl->touchTaken() == taken1 && ! pv->oscUi()->popupOpen(), "(a) the rest of a gesture that began on the host is not ours");
        // (c) the touch comes in the very step in which the host dialog appears: the page check of that step runs before the touch is judged
        r.run (300);   // the detector is idle until the next check is due: the touch must not wait for it
        r.frame.setView (play1, false, 90);
        ev (TouchEvent::Down);
        ev (TouchEvent::Up);
        r.run (10);
        check (r.ctl->currentIndex() < 0 && r.ctl->touchTaken() == taken1 && ! pv->oscUi()->popupOpen(), "(c) a tap in the first step after the dialog appeared: the touch itself forces the page check, which hides the view first");
        r.frame.setView (play1, true, 90);
        r.until ([&] { return r.ctl->currentIndex() == play1; }, 600);
        r.run (300);
        // (c) what the code cannot know (the dialog and the tap in one frame): the list that tap opened closes when the view hides
        tap();
        check (pv->oscUi()->popupOpen(), "(c) (the list is open again)");
        r.frame.setView (play1, false, 90);
        r.until ([&] { return r.ctl->currentIndex() < 0; }, 600);
        check (! pv->oscUi()->popupOpen(), "(c) the view hides: the open list is closed, it is not open when the page comes back");
        r.frame.setView (play1, true, 90);
        r.until ([&] { return r.ctl->currentIndex() == play1; }, 600);
        check (! pv->oscUi()->popupOpen(), "(c) the page is back with no list open");
        // (d) a touch is judged by the state that draws: the page is on screen exactly when a touch is taken
        const unsigned long t2 = r.ctl->touchTaken();
        r.run (300);
        tap();
        check (r.ctl->currentIndex() == play1 && r.ctl->touchTaken() > t2 && r.plane.planeOn, "(d) the page drawn is the page that takes the touch (one source: the page check of the step)");
    }
    // ---- the Q group a page sets reaches the instance (IInstanceView::setQGroup); the interface default does nothing ----
    {
        FakeInstance f;
        const int targets[4] = { 3, -1, 7, 11 };
        f.setQGroup (targets, 4);
        check (f.qGroupCalls == 1 && f.qGroup[0] == 3 && f.qGroup[1] == -1 && f.qGroup[2] == 7 && f.qGroup[3] == 11 && f.qGroupN == 4, "the Q group is handed to the instance as given");
    }
    // ---- the same controller on a thread, with parameters, markers and instances changing under it (ThreadSanitizer) ----
    {
        Rig r;
        r.frame.setAll (true, 90);
        std::atomic<bool> stop { false };
        std::thread t ([&]
        {
            const auto start = std::chrono::steady_clock::now();
            while (! stop.load())
            {
                const double now = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count() + 1.0;
                const double wait = r.ctl->tick (now);
                std::this_thread::sleep_for (std::chrono::microseconds ((long long) (wait * 1000.0)));
            }
        });
        FakeInstance second;
        // other threads open and close instances (heap objects, freed right after detach) and flip the slot colour on screen, while the overlay thread ticks
        std::atomic<bool> churnStop { false };
        std::atomic<int> opened { 0 };
        std::thread churn ([&]
        {
            while (! churnStop.load())
            {
                auto x = std::make_unique<FakeInstance>();
                x->set ("filter1.cutoff", 300.0f + 50.0f * static_cast<float> (opened.load() % 20));
                const int sl = r.ctl->attach (x.get());
                r.frame.setViewInstance (0, sl, true, 90);
                std::this_thread::sleep_for (std::chrono::milliseconds (7));
                r.ctl->detach (x.get());
                x.reset();
                ++opened;
            }
        });
        std::thread flip ([&]
        {
            int k = 0;
            while (! churnStop.load())
            {
                r.frame.setViewInstance (0, k++ % 4, true, 90);
                std::this_thread::sleep_for (std::chrono::milliseconds (3));
            }
        });
        for (int i = 0; i < 60; ++i)
        {
            r.inst.set ("filter1.resonance", 10.0f + (float) (i % 7) * 10.0f);
            r.inst.hasLive = true;
            r.inst.liveCutoff = 500.0f + 20.0f * (float) i;
            if (i == 20) r.ctl->attach (&second);
            if (i == 30) r.ctl->detach (&second);
            if (i == 40) r.frame.setMarker (0, false, 90);
            if (i == 45) r.frame.setMarker (0, true, 90);
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }
        churnStop.store (true);
        churn.join();
        flip.join();
        stop.store (true);
        t.join();
        r.ctl->shutdown();
        check (! r.plane.planeOn && ! r.plane.buffersAlive && r.plane.countOf ("setplane") > 5, "threaded run: pictures were shown (" + std::to_string (r.plane.countOf ("setplane")) + ") and everything is off at the end");
        check (opened.load() > 20, "threaded run: " + std::to_string (opened.load()) + " instances were opened and closed on another thread while the overlay thread ticked");
    }
    std::printf (g_fails ? "controller_host: FAILED (%d)\n" : "controller_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
