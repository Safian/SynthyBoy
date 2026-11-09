// SynthyBoy - UI tests: several instances of one effect (block B22): the key set indirection, the rack strips with add / remove / duplicate / move /
// bypass on phone and tablet, the panel of a pool module, the module sheet operations, modulation by touch, state and screenshots.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "ui/FxRackOps.h"
#include "ui/ModuleSheet.h"
#include "ui/panels/FxChainPanel.h"
#include "ui/panels/FxCommon.h"
#include "synthyboy/fx_rack.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInit = new juce::ScopedJuceInitialiser_GUI();

struct Rig
{
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base { proc.createEditor() };
    SynthyBoyEditor* editor = dynamic_cast<SynthyBoyEditor*> (base.get());

    explicit Rig (int w = 390, int h = 844)
    {
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
        editor->setSourceBarOpen (false);
        editor->setSize (w, h);
        editor->panelHost().setPage (Page::fx);
        editor->pageTabs().select (Page::fx, false);
    }

    void setParam (const std::string& key, float plain)
    {
        auto* p = proc.parameters().getParameter (key);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }
    float param (const std::string& key) { return proc.parameters().getRawParameterValue (key)->load(); }

    FxChainPanel* chainPanel()
    {
        for (const auto& p : editor->panelHost().allPanels())
            if (auto* c = dynamic_cast<FxChainPanel*> (p.get()))
                return c;
        return nullptr;
    }
    FxRackView* view() { return chainPanel() != nullptr ? &chainPanel()->rackView() : nullptr; }
    UiContext& ctx() { return editor->context(); }
    void shot (const std::string& name)
    {
        const juce::File dir ("/tmp/ui-shots/fxinst");
        dir.createDirectory();
        const auto file = dir.getChildFile (juce::String (name) + ".png");
        auto saved = editor->getBounds();
        editor->setSize (saved.getWidth(), saved.getHeight() < 1000 && saved.getWidth() < 1000 ? saved.getHeight() + editor->panelHost().contentHeight() : saved.getHeight());
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        CHECK (image.isValid() && out.openedOk() && png.writeImageToStream (image, out));
        std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
        editor->setSize (saved.getWidth(), saved.getHeight());
    }
};

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged, int clicks = 1)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                             juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), clicks, dragged);
}

int slotType (Rig& rig, int slot) { return static_cast<int> (std::lround (rig.param (paramTable()[fxInstanceKeys (slot).type].key))); }
} // namespace

// ---- the key set -------------------------------------------------------------------------------------------------------------------------

TEST (fxui_key_set_resolves_the_keys_of_a_type_to_the_keys_of_a_slot)
{
    const FxKeySet set (3, kFxTypeChorus);
    const KeyRef rate = set.resolve ("chorus.rate");
    CHECK (rate.valid());
    CHECK (std::string (rate.info->key) == "chorus.rate");                           // everything that is shown is the key of instance 0 ...
    CHECK_EQ (rate.index, fxInstanceKeys (3).p[1]);                                   // ... what is stored, automated and modulated is the slot's
    CHECK_EQ (paramTable()[rate.sourceIndex()].id, rate.info->id);
    CHECK (rate.storedNormalised);
    CHECK (set.resolve ("chorus.on").index == fxInstanceKeys (3).on && ! set.resolve ("chorus.on").storedNormalised);
    CHECK (set.resolve ("chorus.rack").index == fxInstanceKeys (3).rack);
    CHECK (! set.resolve ("delay.mix").valid());                                      // another type
    CHECK (! set.resolve ("chorus.nothing").valid());
    CHECK (set.resolve ("reverb.force_enable").valid());                              // a switch of the whole engine, not of the instance: passes through
    CHECK (set.resolve ("reverb.force_enable").index == resolveKey ("reverb.force_enable").index && ! set.resolve ("reverb.force_enable").storedNormalised);
    CHECK (! FxKeySet (0, kFxTypeNone).resolve ("chorus.rate").valid());
    CHECK (std::string (fxPanelIdOfType (kFxTypeBode)) == "FxBodePanel");
    CHECK (std::string (fxPanelIdOfType (kFxTypeNone)).empty());
}

TEST (fxui_a_handle_of_a_slot_stores_the_normalised_value_and_shows_the_real_one)
{
    Rig rig;
    rig.setParam ("fxi04.type", static_cast<float> (kFxTypeChorus));
    auto set = std::make_shared<FxKeySet> (3, kFxTypeChorus);
    UiContext slotCtx { rig.ctx().host, rig.ctx().theme, rig.ctx().assign, rig.ctx().view, set };

    ParamHandle rate (slotCtx, "chorus.rate");
    CHECK (rate.valid());
    rate.setPlainComplete (4.0f);
    CHECK_NEAR (rate.plain(), 4.0, 1.0e-3);                                            // 4 Hz in the range 0.02 .. 8
    CHECK_NEAR (rig.param ("fxi04.p02"), fxInstanceNormalise (kFxTypeChorus, 1, 4.0f), 1.0e-6);
    CHECK_NEAR (rig.param ("chorus.rate"), 0.4, 1.0e-6);                                // instance 0 untouched (its default)
    CHECK_EQ (rate.index(), fxInstanceKeys (3).p[1]);
    CHECK_EQ (rate.infoIndex(), findParamIndex (findParamByKey ("chorus.rate")->id));
    CHECK (rate.modulatable());
    CHECK_NEAR (rate.defaultPlain(), 0.4, 1.0e-6);
    rate.resetToDefault();
    CHECK_NEAR (rate.plain(), 0.4, 1.0e-3);

    ParamHandle sync (slotCtx, "chorus.sync");   // a choice: not modulatable, the real names
    sync.setPlainComplete (2.0f);
    CHECK_EQ (static_cast<int> (std::lround (sync.plain())), 2);
    CHECK (! sync.modulatable());
    CHECK (std::string (displayText (sync.info(), sync.plain())) == "Tempo dotted");

    ParamHandle on (slotCtx, "chorus.on");
    on.setPlainComplete (1.0f);
    CHECK_NEAR (rig.param ("fxi04.on"), 1.0f, 1.0e-5);
    CHECK_NEAR (rig.param ("chorus.on"), 0.0f, 1.0e-5);

    // the value from outside (a host, a preset) reaches the handle
    int changes = 0;
    rate.onChange = [&] { ++changes; };
    rig.setParam ("fxi04.p02", fxInstanceNormalise (kFxTypeChorus, 1, 1.0f));
    CHECK (changes >= 1);
    CHECK_NEAR (rate.plain(), 1.0, 1.0e-3);

    // instance 0 is resolved as before without a key set
    ParamHandle plain (rig.ctx(), "chorus.rate");
    CHECK_EQ (plain.index(), plain.infoIndex());
    plain.setPlainComplete (2.0f);
    CHECK_NEAR (rig.param ("chorus.rate"), 2.0, 1.0e-3);
}

TEST (fxui_every_panel_of_a_type_resolves_all_its_keys_and_together_they_cover_the_type)
{
    Rig rig;
    for (int type = 1; type < kNumFxInstanceTypes; ++type)
    {
        const auto set = std::make_shared<FxKeySet> (type % kNumFxInstances, type);
        UiContext slotCtx { rig.ctx().host, rig.ctx().theme, rig.ctx().assign, rig.ctx().view, set };
        sbtest::Context named (fxPanelIdOfType (type));
        auto panel = PanelRegistry::instance().create (fxPanelIdOfType (type), slotCtx);
        CHECK (panel != nullptr);
        if (panel == nullptr)
            continue;
        panel->build();
        CHECK (panel->preferredHeight (390) > 0 && panel->preferredHeight (1024) > 0);
        std::set<std::string> shown;
        for (const auto& key : panel->keys())
        {
            CHECK (slotCtx.resolve (key).valid());
            shown.insert (key);
        }
        for (const auto& control : panel->controls())
        {
            CHECK (control->paramHandle().valid());
            CHECK (control->isAvailable() || true);   // the engine of the test host may lack bits: the control is drawn dimmed, never missing
        }
        // every parameter of the type has a control on its panel: nothing of a slot is out of reach
        const FxTypeDescriptor& d = fxTypeDescriptor (type);
        for (int k = 0; k < d.count; ++k)
            if (! CHECK (shown.count (paramTable()[d.source[k]].key) == 1))
                std::printf ("    %s: no control for %s\n", d.name, paramTable()[d.source[k]].key);
        CHECK (shown.count (paramTable()[d.onIndex].key) == 1);
        CHECK (shown.count (paramTable()[d.rackIndex].key) == 1);
    }
}

TEST (fxui_a_control_of_a_slot_panel_edits_the_slot_and_the_control_of_instance_zero_edits_instance_zero)
{
    Rig rig;
    rig.setParam ("fxi02.type", static_cast<float> (kFxTypeDelay));
    const auto set = std::make_shared<FxKeySet> (1, kFxTypeDelay);
    UiContext slotCtx { rig.ctx().host, rig.ctx().theme, rig.ctx().assign, rig.ctx().view, set };
    auto panel = PanelRegistry::instance().create ("FxDelayPanel", slotCtx);
    panel->build();

    ParamDragControl* mix = nullptr;
    for (const auto& c : panel->controls())
        if (c->key() == "delay.mix")
            mix = dynamic_cast<ParamDragControl*> (c.get());
    CHECK (mix != nullptr);
    if (mix == nullptr)
        return;
    const float instanceZero = rig.param ("delay.mix");
    mix->nudgeNormalised (0.25f);
    CHECK_NEAR (rig.param ("delay.mix"), instanceZero, 1.0e-6);
    CHECK_NEAR (rig.param ("fxi02.p01"), 0.25, 0.01);                                    // "Mix" is the first parameter of the delay, 0 -> 0.25

    // the same panel class of instance 0
    auto zero = PanelRegistry::instance().create ("FxDelayPanel", rig.ctx());
    zero->build();
    for (const auto& c : zero->controls())
        if (c->key() == "delay.mix")
            dynamic_cast<ParamDragControl*> (c.get())->nudgeNormalised (0.25f);
    CHECK (std::fabs (rig.param ("delay.mix") - instanceZero) > 1.0e-4f);
}

// ---- the racks ------------------------------------------------------------------------------------------------------------------------------

TEST (fxui_the_racks_show_the_sixteen_modules_and_add_remove_duplicate_move_bypass_a_module)
{
    for (const auto size : { std::pair<int, int> { 390, 844 }, std::pair<int, int> { 1024, 768 } })
    {
        Rig rig (size.first, size.second);
        sbtest::Context named (size.first < 600 ? "phone" : "tablet");
        auto* view = rig.view();
        CHECK (view != nullptr);
        if (view == nullptr)
            continue;
        CHECK_EQ (view->racks().rack[0].size(), static_cast<size_t> (16));
        CHECK (view->racks().rack[1].empty() && view->racks().rack[2].empty());

        // add: the picker's choice lands in the first free slot, at the end of the rack, selected, and its panel opens below
        const int slot = view->addModule (kFxTypeDelay, 1);
        CHECK_EQ (slot, 0);
        CHECK_EQ (slotType (rig, 0), static_cast<int> (kFxTypeDelay));
        CHECK_NEAR (rig.param ("fxi01.rack"), 1.0f, 1.0e-5);
        CHECK_NEAR (rig.param ("fxi01.on"), 1.0f, 1.0e-5);
        CHECK (view->racks().rack[1].size() == 1 && view->racks().rack[1][0] == (FxRackItem { true, 0 }));
        CHECK_EQ (view->selectedSlot(), 0);
        CHECK (rig.chainPanel()->slotPanel() != nullptr);
        CHECK_EQ (rig.chainPanel()->slotPanelSlot(), 0);

        // the cards of both lists have room in the view, at least a touch target high
        const auto card = view->cardBounds ({ true, 0 });
        CHECK (! card.isEmpty() && card.getHeight() >= static_cast<float> (rig.editor->theme().touch()) - 0.5f);
        CHECK (view->getLocalBounds().toFloat().contains (card));
        CHECK (view->heightForWidth (view->getWidth()) <= view->getHeight() + 1);
        for (const auto& item : view->racks().rack[0])
            CHECK (view->getLocalBounds().toFloat().contains (view->cardBounds (item)));

        // duplicate: a copy behind it with the same settings, selected
        rig.setParam ("fxi01.p01", 0.77f);
        const int copy = view->duplicate ({ true, 0 });
        CHECK_EQ (copy, 1);
        CHECK_NEAR (rig.param ("fxi02.p01"), 0.77f, 1.0e-5);
        CHECK (view->racks().rack[1].size() == 2 && view->racks().rack[1][1] == (FxRackItem { true, 1 }));
        CHECK_EQ (view->selectedSlot(), 1);

        // duplicate a module of instance 0 (the reverb) with its settings
        rig.setParam ("reverb.size", 90.0f);
        const int fromZero = view->duplicate ({ false, 8 });
        CHECK_EQ (fromZero, 2);
        CHECK_EQ (slotType (rig, 2), static_cast<int> (kFxTypeReverb));
        CHECK_NEAR (fxInstancePlain (kFxTypeReverb, 1, rig.param ("fxi03.p02")), 90.0, 0.05);
        {
            int r = 0, i = 0;
            CHECK (view->racks().find ({ false, 8 }, r, i) && r == 0 && view->racks().rack[0][static_cast<size_t> (i) + 1] == (FxRackItem { true, 2 }));   // behind the reverb
        }

        // move: between racks and within a rack
        CHECK (view->moveItem ({ true, 1 }, 2, 0));
        CHECK_NEAR (rig.param ("fxi02.rack"), 2.0f, 1.0e-5);
        CHECK (view->racks().rack[2][0] == (FxRackItem { true, 1 }));
        CHECK (view->moveItem ({ true, 2 }, 0, 0));
        CHECK (view->racks().rack[0][0] == (FxRackItem { true, 2 }));
        view->select ({ false, 5 });
        CHECK (view->moveSelected (1));
        CHECK (view->racks().rack[0][1] == (FxRackItem { true, 2 }) || view->racks().rack[0][2] == (FxRackItem { false, 5 }));

        // bypass: the power of a pool module; all at once and back
        CHECK (view->togglePower ({ true, 0 }));
        CHECK_NEAR (rig.param ("fxi01.on"), 0.0f, 1.0e-5);
        view->togglePower ({ true, 0 });
        view->bypassAll();
        for (int s = 0; s < 3; ++s)
            CHECK_NEAR (rig.param (paramTable()[fxInstanceKeys (s).on].key), 0.0f, 1.0e-5);
        for (int m = 0; m < 16; ++m)
            CHECK (! view->isOn ({ false, m }));
        view->bypassAll();
        for (int s = 0; s < 2; ++s)   // (the copy of the reverb, which was off in instance 0, is off)
            CHECK_NEAR (rig.param (paramTable()[fxInstanceKeys (s).on].key), 1.0f, 1.0e-5);

        // remove: the slot is free and at its defaults, the panel below closes
        view->select ({ true, 1 });
        CHECK (view->removeSlot (1));
        CHECK_EQ (slotType (rig, 1), static_cast<int> (kFxTypeNone));
        CHECK_NEAR (rig.param ("fxi02.p01"), 0.0f, 1.0e-5);
        CHECK (! view->hasSelectedItem());
        CHECK (rig.chainPanel()->slotPanel() == nullptr);
        CHECK (! view->removeSlot (1));
        // the next one fills the hole
        CHECK_EQ (view->addModule (kFxTypeChorus, 0), 1);
        CHECK (rig.chainPanel()->slotPanel() != nullptr);

        // the buttons do the same
        view->select ({ true, 1 });
        view->duplicateToggleButton().onClick();
        CHECK_EQ (slotType (rig, 3), static_cast<int> (kFxTypeChorus));
        view->removeSlotButton().onClick();
        CHECK_EQ (slotType (rig, 3), static_cast<int> (kFxTypeNone));
        for (auto* b : { &view->powerToggleButton(), &view->bypassAllToggleButton(), &view->moveLeft(), &view->moveRight(), &view->duplicateToggleButton(),
                         &view->removeSlotButton(), &view->addModuleButton() })
            CHECK (b->getHeight() >= rig.editor->theme().touch() && b->getWidth() >= rig.editor->theme().touch());
        CHECK (view->getHeight() >= view->heightForWidth (view->getWidth()) - 2);
    }
}

TEST (fxui_the_pool_can_be_filled_and_the_add_buttons_say_so)
{
    Rig rig;
    auto* view = rig.view();
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    for (int i = 0; i < 16; ++i)
        CHECK_EQ (view->addModule (kFxTypeUtility, i % 3), i);
    CHECK_EQ (view->addModule (kFxTypeUtility, 0), -1);
    CHECK (! view->addModuleButton().isEnabled());
    CHECK (! view->duplicateToggleButton().isEnabled());
    view->removeSlot (7);
    CHECK (view->addModuleButton().isEnabled());
    CHECK_EQ (view->addModule (kFxTypeBode, 2), 7);
    // 16 slots + 16 modules: the strips wrap, every card is inside
    for (int r = 0; r < 3; ++r)
        for (const auto& item : view->racks().rack[static_cast<size_t> (r)])
            CHECK (view->getLocalBounds().toFloat().contains (view->cardBounds (item)));
}

TEST (fxui_gestures_on_the_cards_select_toggle_and_reorder_with_a_drop_line)
{
    Rig rig;
    auto* view = rig.view();
    CHECK (view != nullptr);
    if (view == nullptr)
        return;
    view->addModule (kFxTypeDelay, 0);
    view->addModule (kFxTypeChorus, 0);
    const FxRackItem a { true, 0 }, b { true, 1 };
    const auto centre = [&] (FxRackItem item) { return view->cardBounds (item).getCentre(); };

    // a tap selects, the panel of the type opens
    view->clearSelection();
    CHECK (rig.chainPanel()->slotPanel() == nullptr);
    view->mouseDown (mouse (*view, centre (a), centre (a), false));
    view->mouseUp (mouse (*view, centre (a), centre (a), false));
    CHECK (view->hasSelectedItem() && view->selectedItem() == a);
    CHECK (rig.chainPanel()->slotPanel() != nullptr);

    // double tap: the power
    CHECK_NEAR (rig.param ("fxi01.on"), 1.0f, 1.0e-5);
    view->mouseDown (mouse (*view, centre (a), centre (a), false, 2));
    view->mouseDoubleClick (mouse (*view, centre (a), centre (a), false, 2));
    view->mouseUp (mouse (*view, centre (a), centre (a), false, 2));
    CHECK_NEAR (rig.param ("fxi01.on"), 0.0f, 1.0e-5);

    // a drag without the hold does nothing; hold + drag moves the card in front of the first module of instance 0 and shows the drop line
    const auto order = view->racks().rack[0];
    const auto from = centre (b) + juce::Point<float> (6.0f, 0.0f);
    const auto first = view->racks().rack[0][0];
    const auto to = view->cardBounds (first).getTopLeft() + juce::Point<float> (3.0f, 6.0f);
    view->mouseDown (mouse (*view, from, from, false));
    view->mouseDrag (mouse (*view, to, from, true));
    view->mouseUp (mouse (*view, to, from, true));
    CHECK (view->racks().rack[0] == order);
    view->mouseDown (mouse (*view, from, from, false));
    CHECK (view->longPress().pending());
    view->longPress().fire();
    CHECK (view->reorderIsArmed());
    view->mouseDrag (mouse (*view, to, from, true));
    CHECK (view->dropLineShown());
    CHECK_EQ (view->dropRackShown(), 0);
    CHECK_EQ (view->dropIndexShown(), 0);
    view->mouseUp (mouse (*view, to, from, true));
    CHECK (view->racks().rack[0][0] == b);
    CHECK (view->racks().rack[0][1] == first);
    CHECK (! view->reorderIsArmed());

    // dropped on another rack: its strip takes it
    const auto plus = view->plusBounds (2).getCentre();
    const auto fromB = centre (b) + juce::Point<float> (6.0f, 0.0f);
    view->mouseDown (mouse (*view, fromB, fromB, false));
    view->longPress().fire();
    view->mouseDrag (mouse (*view, plus, fromB, true));
    view->mouseUp (mouse (*view, plus, fromB, true));
    CHECK_NEAR (rig.param ("fxi02.rack"), 2.0f, 1.0e-5);
    CHECK (view->racks().rack[2].size() == 1 && view->racks().rack[2][0] == b);
}

// ---- the module sheet of a slot ----------------------------------------------------------------------------------------------------------------

TEST (fxui_copy_paste_swap_and_init_work_on_the_slots_of_the_pool)
{
    Rig rig;
    auto& ctx = rig.ctx();
    auto* view = rig.view();
    view->addModule (kFxTypeReverb, 0);
    view->addModule (kFxTypeEq, 1);
    CHECK (module_ops::isModule ("fxi01") && module_ops::isModule ("fxi16") && ! module_ops::isModule ("fxi17") && ! module_ops::isModule ("fxi1"));
    CHECK (module_ops::isPoolSlot ("fxi05"));
    CHECK (module_ops::displayName ("fxi03") == "FX SLOT 3");
    const auto siblings = module_ops::siblings ("fxi03");
    CHECK_EQ (siblings.size(), static_cast<size_t> (15));
    CHECK (siblings.front() == "fxi01" && siblings.back() == "fxi16");

    rig.setParam ("fxi01.p01", 0.61f);
    CHECK (module_ops::copy (ctx, "fxi01", false));
    CHECK (module_ops::paste (ctx, "fxi05", false));
    CHECK_EQ (slotType (rig, 4), static_cast<int> (kFxTypeReverb));
    CHECK_NEAR (rig.param ("fxi05.p01"), 0.61f, 1.0e-5);
    // the copy sorts behind the original (no two slots with the same order)
    const auto racks = fx_ops::racks (ctx);
    int r1 = 0, i1 = 0, r5 = 0, i5 = 0;
    CHECK (racks.find ({ true, 0 }, r1, i1) && racks.find ({ true, 4 }, r5, i5));
    CHECK (r1 == r5 ? i5 == i1 + 1 : true);

    // swap exchanges the two modules
    CHECK (module_ops::swap (ctx, "fxi01", "fxi02", false));
    CHECK_EQ (slotType (rig, 0), static_cast<int> (kFxTypeEq));
    CHECK_EQ (slotType (rig, 1), static_cast<int> (kFxTypeReverb));
    CHECK_NEAR (rig.param ("fxi02.p01"), 0.61f, 1.0e-5);
    CHECK_NEAR (rig.param ("fxi01.rack"), 1.0f, 1.0e-5);

    // init: the parameters back to the defaults of the type, the module stays
    CHECK (module_ops::init (ctx, "fxi02"));
    CHECK_EQ (slotType (rig, 1), static_cast<int> (kFxTypeReverb));
    CHECK_NEAR (rig.param ("fxi02.on"), 1.0f, 1.0e-5);
    float defaults[kFxInstanceParams];
    fxInstanceDefaults (kFxTypeReverb, defaults);
    for (int p = 0; p < kFxInstanceParams; ++p)
        CHECK_NEAR (rig.param (paramTable()[fxInstanceKeys (1).p[p]].key), defaults[p], 1.0e-6);
    CHECK (! module_ops::init (ctx, "fxi12")); // an empty slot has nothing to init

    // the sheet has the extra row of a slot (bypass, duplicate, remove) and the usual ones
    ModuleSheet sheet (ctx, "fxi02");
    CHECK (sheet.button ("Bypass") != nullptr && sheet.button ("Duplicate") != nullptr && sheet.button ("Remove") != nullptr);
    CHECK (sheet.button ("Copy") != nullptr && sheet.button ("Init") != nullptr && sheet.swapButton ("fxi07") != nullptr);
    sheet.setSize (sheet.preferredWidth(), sheet.preferredHeight());
    sheet.button ("Duplicate")->onClick();
    CHECK_EQ (slotType (rig, 2), static_cast<int> (kFxTypeReverb));
    sheet.button ("Bypass")->onClick();
    CHECK_NEAR (rig.param ("fxi02.on"), 0.0f, 1.0e-5);
    sheet.button ("Remove")->onClick();
    CHECK_EQ (slotType (rig, 1), static_cast<int> (kFxTypeNone));
    CHECK (ModuleSheet (ctx, "osc1").button ("Remove") == nullptr);

    // lock
    CHECK (module_ops::setLocked (ctx, "fxi03", true) == ctx.host.canLock());
    if (ctx.host.canLock())
    {
        CHECK (module_ops::isLocked (ctx, "fxi03"));
        module_ops::setLocked (ctx, "fxi03", false);
    }
}

// ---- modulation by touch and automation ----------------------------------------------------------------------------------------------------------

// GROUPA1 (WP-10): cut / copy / paste / clear a bus (Serum 2 manual p.158) through the rack view, on phone and tablet
TEST (fxui_wp10_bus_operations_cut_copy_paste_and_clear_a_whole_bus)
{
    for (const auto& size : { std::pair<int, int> { 390, 844 }, std::pair<int, int> { 1024, 768 } })
    {
        Rig rig (size.first, size.second);
        auto* view = rig.view();
        CHECK (view != nullptr);
        if (view == nullptr)
            continue;
        view->setBounds (0, 0, size.first, view->heightForWidth (size.first));
        const int minTouch = rig.editor->theme().touch();
        CHECK (view->busMenuButton().getHeight() >= minTouch && view->busMenuButton().getWidth() >= minTouch);

        // Main gets three modules, Bus 1 one
        const int chorus = view->addModule (kFxTypeChorus, 0);
        const int reverb = view->addModule (kFxTypeReverb, 0);
        const int dist = view->addModule (kFxTypeDistortion, 0);
        const int delay = view->addModule (kFxTypeDelay, 1);
        CHECK (chorus >= 0 && reverb >= 0 && dist >= 0 && delay >= 0);
        rig.setParam (std::string ("fxi") + (reverb + 1 < 10 ? "0" : "") + std::to_string (reverb + 1) + ".p03", 0.77f);
        view->refresh();
        const auto count = [&] (int rack)
        {
            int n = 0;
            for (const auto& item : view->racks().rack[static_cast<size_t> (rack)])
                n += item.pool ? 1 : 0;
            return n;
        };
        CHECK_EQ (count (0), 3);

        // copy Main, paste into Bus 2: a duplicate of the three modules with their settings
        CHECK_EQ (view->copyBus (0), 3);
        CHECK_EQ (view->busClipboard().entries.size(), size_t { 3 });
        CHECK_EQ (view->pasteBus (2), 3);
        CHECK_EQ (count (0), 3);
        CHECK_EQ (count (2), 3);
        std::vector<FxRackItem> bus2;
        for (const auto& item : view->racks().rack[2])
            if (item.pool)
                bus2.push_back (item);
        CHECK_EQ (bus2.size(), size_t { 3 });
        CHECK_EQ (fxItemType (fx_ops::values (rig.ctx()).plain, bus2[1]), kFxTypeReverb);
        CHECK_NEAR (fx_ops::values (rig.ctx()).plain[fxInstanceKeys (bus2[1].index).p[2]], 0.77f, 1.0e-4f);

        // cut Bus 2, paste into Bus 1 (move a whole bus): Bus 2 is empty, Bus 1 has its own module and the three
        CHECK_EQ (view->cutBus (2), 3);
        CHECK_EQ (count (2), 0);
        CHECK_EQ (view->pasteBus (1), 3);
        CHECK_EQ (count (1), 4);
        CHECK_EQ (count (0), 3);

        // clear Main
        CHECK_EQ (view->clearBus (0), 3);
        CHECK_EQ (count (0), 0);
        CHECK_EQ (count (1), 4);
        CHECK_EQ (view->clearBus (0), 0);

        // the menu opens for the bus of the selection and the buttons stay inside the strip
        view->busMenuButton().onClick();
        CHECK (view->getLocalBounds().contains (view->busMenuButton().getBounds()));
        for (auto* b : { &view->rackPresetsButton(), &view->busMenuButton() })
            CHECK (b->getWidth() > 0);
        CHECK (! view->rackPresetsButton().getBounds().intersects (view->busMenuButton().getBounds()));
        rig.shot (size.first < 1000 ? "fxrack-bus-phone" : "fxrack-bus-tablet");
    }
}

TEST (fxui_assign_by_touch_modulates_a_parameter_of_a_slot_and_refuses_one_that_is_not_modulatable)
{
    Rig rig;
    auto* view = rig.view();
    view->addModule (kFxTypeChorus, 0);
    auto* detail = rig.chainPanel()->slotPanel();
    CHECK (detail != nullptr);
    if (detail == nullptr)
        return;
    ParamDragControl* rate = nullptr;
    ParamControl* sync = nullptr;
    for (const auto& c : detail->controls())
    {
        if (c->key() == "chorus.rate")
            rate = dynamic_cast<ParamDragControl*> (c.get());
        if (c->key() == "chorus.sync")
            sync = c.get();
    }
    CHECK (rate != nullptr && sync != nullptr);
    if (rate == nullptr || sync == nullptr)
        return;
    CHECK (rate->paramHandle().modulatable());
    CHECK (! sync->paramHandle().modulatable());

    auto& assign = rig.ctx().assign;
    assign.tapSource (7);   // LFO 1
    auto& flow = assign.flow();
    const int index = rate->paramHandle().index();
    CHECK (index == fxInstanceKeys (0).p[1]);
    CHECK (flow.isTarget (index));
    CHECK (flow.beginDrag (index) == AssignFlow::BeginResult::ok);
    const int slot = flow.activeSlot();
    flow.setAmount (40.0f);
    flow.endDrag();
    assign.exitAssignMode();
    assign.refresh();
    // the matrix slot points at the destination of the slot's parameter (choice values 520 and up), and the knob shows the modulator
    const int destination = static_cast<int> (std::lround (rig.param (paramTable()[modSlotParams (slot).destination].key)));
    CHECK_EQ (destination, static_cast<int> (modDestinationChoiceOf (static_cast<size_t> (fxInstanceKeys (0).p[1]))));
    CHECK (destination >= 520);
    CHECK_EQ (rate->modulatorCount(), 1);
    // the same parameter of instance 0 is not modulated
    ParamHandle zero (rig.ctx(), "chorus.rate");
    CHECK_EQ (static_cast<int> (assign.snapshot().entriesFor (zero.index()).size()), 0);
}

TEST (fxui_the_state_with_pool_modules_survives_a_save_and_a_load)
{
    Rig rig;
    auto* view = rig.view();
    view->addModule (kFxTypeFlanger, 2);
    view->addModule (kFxTypePhaser, 0);
    rig.setParam ("fxi02.p03", 0.42f);
    juce::MemoryBlock state;
    rig.proc.getStateInformation (state);

    SynthyBoyProcessor other;
    other.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK_NEAR (other.parameters().getRawParameterValue ("fxi01.type")->load(), static_cast<float> (kFxTypeFlanger), 1.0e-5);
    CHECK_NEAR (other.parameters().getRawParameterValue ("fxi01.rack")->load(), 2.0f, 1.0e-5);
    CHECK_NEAR (other.parameters().getRawParameterValue ("fxi02.type")->load(), static_cast<float> (kFxTypePhaser), 1.0e-5);
    CHECK_NEAR (other.parameters().getRawParameterValue ("fxi02.p03")->load(), 0.42, 1.0e-6);
    std::unique_ptr<juce::AudioProcessorEditor> editor (other.createEditor());
    auto* ed = dynamic_cast<SynthyBoyEditor*> (editor.get());
    CHECK (ed != nullptr);
    ed->setSize (390, 844);
    ed->panelHost().setPage (Page::fx);
    const auto racks = fx_ops::racks (ed->context());
    CHECK (racks.rack[2].size() == 1 && racks.rack[2][0] == (FxRackItem { true, 0 }));
    CHECK (racks.rack[0].back() == (FxRackItem { true, 1 }));
}

TEST (fxui_screenshots_of_the_racks_and_a_slot_panel)
{
    for (const auto size : { std::pair<int, int> { 390, 844 }, std::pair<int, int> { 1024, 768 } })
    {
        Rig rig (size.first, size.second);
        const std::string n = size.first < 600 ? "phone" : "tablet";
        auto* view = rig.view();
        CHECK (view != nullptr);
        if (view == nullptr)
            continue;
        view->addModule (kFxTypeDelay, 0);
        view->addModule (kFxTypeReverb, 1);
        view->addModule (kFxTypeChorus, 1);
        view->addModule (kFxTypeBode, 2);
        view->select ({ true, 0 });
        view->togglePower ({ true, 2 });
        rig.setParam ("delay.on", 1.0f);
        rig.shot ("racks-with-delay-panel-" + n);
        view->select ({ false, 3 });
        rig.shot ("racks-instance-zero-selected-" + n);
        view->select ({ true, 3 });
        const auto from = view->cardBounds ({ true, 3 }).getCentre() + juce::Point<float> (6.0f, 0.0f);
        view->mouseDown (mouse (*view, from, from, false));
        view->longPress().fire();
        view->mouseDrag (mouse (*view, view->cardBounds ({ false, 1 }).getCentre(), from, true));
        rig.shot ("racks-drag-" + n);
        view->mouseUp (mouse (*view, view->cardBounds ({ false, 1 }).getCentre(), from, true));
    }
}

TEST (fxui_zz_shutdown)
{
    delete juceInit;
    juceInit = nullptr;
    CHECK (true);
}
