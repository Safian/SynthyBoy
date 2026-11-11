// SynthyBoy - UI library test: a real SynthyBoyEditor on a host that is NOT an AudioProcessor and has no APVTS (docs/ui-design/UILIB-REPORT.md).
// The host is what the Kick app is: an in-memory PlainParameterStore behind a UiHost, nothing of juce_audio_processors. Covers the two-way parameter flow
// (UI edit -> host callback, host change -> control), gestures, the undo history built from them, the external-change refresh of knobs and the
// mod matrix, panel registration without static initialisers, the 1024x600 layout of the Kick MPC, and the pure-library link (no AudioProcessor symbol needed).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ui/Editor.h"
#include "ui/GestureKit.h"
#include "ui/LibraryMenu.h"
#include "ui/Overlays.h"
#include "ui/Panel.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInit = nullptr;
// started in main() (sbtest::runStartups), never during static initialisation: that crashes on Windows
SBTEST_STARTUP (juceInit, [] { juceInit = new juce::ScopedJuceInitialiser_GUI(); }, [] { delete juceInit; juceInit = nullptr; });

/** A host as the Kick app would write it: the parameter values live in a PlainParameterStore, everything else is the UiHost default. */
class MemoryHost final : public UiHost
{
public:
    MemoryHost()
    {
        store.onUiChange = [this] (int index, float plain) { uiChanges.push_back ({ index, plain }); };
        store.onUiGesture = [this] (int index, bool starting) { (starting ? begins : ends).push_back (index); };
    }

    ParameterStore& parameterStore() override { return store; }
    EngineCapsMask engineCaps() const noexcept override { return kCapAllKnown; }
    juce::MidiKeyboardState& keyboardState() override { return keyboard; }
    AbCompare& abCompare() override { return ab; }
    void stepPreset (int) override {}
    juce::String presetName() const override { return "Memory"; }

    PlainParameterStore store;
    juce::MidiKeyboardState keyboard;
    AbCompare ab;
    std::vector<std::pair<int, float>> uiChanges;
    std::vector<int> begins, ends;
};

int indexOf (const char* key) { return resolveKey (key).index; }

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                             juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1, dragged);
}

/** The first visible control of `key` in the panels of the editor. */
ParamControl* findControl (juce::Component& root, const std::string& key)
{
    if (auto* control = dynamic_cast<ParamControl*> (&root); control != nullptr && control->key() == key)
        return control;
    for (auto* child : root.getChildren())
        if (auto* found = findControl (*child, key))
            return found;
    return nullptr;
}

void startMessageThread() { juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread(); }
} // namespace

TEST (uph_store_two_way_flow_and_gestures)
{
    startMessageThread();
    PlainParameterStore store;
    struct Counter final : ParameterStore::Listener
    {
        void parameterChanged (int i) override { changed.push_back (i); }
        void parameterGestureChanged (int i, bool s) override { (s ? begins : ends).push_back (i); }
        std::vector<int> changed, begins, ends;
    } counter, other;
    const int cutoff = indexOf ("filter1.cutoff"), volume = indexOf ("master.volume");
    CHECK (cutoff >= 0 && volume >= 0 && cutoff != volume);
    store.addListener (cutoff, &counter);
    store.addListener (&other); // every parameter

    CHECK (store.contains (cutoff) && ! store.contains (-1) && ! store.contains (static_cast<int> (paramCount())));
    CHECK_NEAR (store.plain (cutoff), paramTable()[cutoff].def, 1.0e-3);

    int uiCalls = 0;
    store.onUiChange = [&] (int, float) { ++uiCalls; };
    store.setPlainComplete (cutoff, 1000.0f);
    CHECK_NEAR (store.plain (cutoff), 1000.0, 1.0);
    CHECK_EQ (uiCalls, 1);
    CHECK_EQ (counter.begins.size(), 1u);
    CHECK_EQ (counter.ends.size(), 1u);
    CHECK (! counter.changed.empty());
    CHECK_NEAR (store.fromNormalised (cutoff, store.toNormalised (cutoff, 1000.0f)), 1000.0, 1.0);
    CHECK_NEAR (store.normalised (cutoff), store.toNormalised (cutoff, 1000.0f), 1.0e-5);

    // a change from the host reaches the listeners but is not echoed to the host
    const size_t before = counter.changed.size();
    store.setFromHost (cutoff, 3000.0f);
    CHECK_EQ (uiCalls, 1);
    CHECK_EQ (counter.changed.size(), before + 1);
    CHECK_NEAR (store.plain (cutoff), 3000.0, 1.0);
    store.setFromHost (cutoff, 3000.0f); // no change, no notification
    CHECK_EQ (counter.changed.size(), before + 1);

    // a listener that removes itself in its callback is safe, and a removed listener hears nothing
    struct SelfRemoving final : ParameterStore::Listener
    {
        explicit SelfRemoving (ParameterStore& s) : owner (s) {}
        void parameterChanged (int) override { ++calls; owner.removeListener (this); }
        ParameterStore& owner;
        int calls = 0;
    } once (store);
    store.addListener (&once);
    store.setFromHost (volume, -12.0f);
    store.setFromHost (volume, -6.0f);
    CHECK_EQ (once.calls, 1);
    store.removeListener (cutoff, &counter);
    const size_t afterRemove = counter.changed.size();
    store.setFromHost (cutoff, 500.0f);
    CHECK_EQ (counter.changed.size(), afterRemove);

    // whole-patch helpers
    auto values = readAllValues (store);
    CHECK_NEAR (values.plain[volume], -6.0, 1.0e-3);
    values.plain[volume] = -18.0f;
    writeAllValues (store, values);
    CHECK_NEAR (store.plain (volume), -18.0, 0.05);
    store.removeListener (&other);
}

TEST (uph_real_editor_on_a_non_apvts_host)
{
    startMessageThread();
    MemoryHost host;
    SynthyBoyEditor editor (host);
    editor.setSize (1024, 768);
    editor.setSourceBarOpen (false);

    // the editor finds its controls: pick the first panel page that has the filter cutoff
    ParamControl* knob = nullptr;
    for (int i = 0; i < kNumPages && knob == nullptr; ++i)
    {
        editor.panelHost().setPage (static_cast<Page> (i));
        knob = findControl (editor.panelHost(), "filter1.cutoff");
    }
    CHECK (knob != nullptr);
    if (knob == nullptr)
        return;
    const int cutoff = indexOf ("filter1.cutoff");
    const auto size = knob->preferredSize();
    knob->setSize (size.x, size.y);

    // UI -> host: a drag is one gesture with changes in between, forwarded to the host callbacks
    host.store.setFromHost (cutoff, 1000.0f);
    host.uiChanges.clear();
    const juce::Point<float> down (static_cast<float> (size.x) * 0.5f, static_cast<float> (size.y) * 0.5f);
    knob->mouseDown (mouse (*knob, down, down, false));
    for (int i = 1; i <= 5; ++i)
        knob->mouseDrag (mouse (*knob, down - juce::Point<float> (0.0f, 10.0f * static_cast<float> (i)), down, true));
    knob->mouseUp (mouse (*knob, down - juce::Point<float> (0.0f, 50.0f), down, true));
    CHECK_EQ (host.begins.size(), 1u);
    CHECK_EQ (host.ends.size(), 1u);
    CHECK_EQ (host.begins[0], cutoff);
    CHECK (host.uiChanges.size() >= 5);
    CHECK (host.uiChanges.back().first == cutoff && host.uiChanges.back().second > 1000.0f);
    CHECK_NEAR (host.uiChanges.back().second, host.store.plain (cutoff), 1.0e-3);

    // the gesture became an undo step of the editor, and undo writes back through the store (a gesture per parameter)
    CHECK (editor.undoStack().canUndo());
    editor.performUndo();
    CHECK_NEAR (host.store.plain (cutoff), 1000.0, 1.0);
    editor.performRedo();
    CHECK (host.store.plain (cutoff) > 1000.0f);

    // host -> UI: an external change reaches the handle of the control at once (no echo to the host)
    host.uiChanges.clear();
    host.store.setFromHost (cutoff, 2500.0f);
    CHECK_NEAR (knob->paramHandle().plain(), 2500.0, 2.0);
    CHECK (host.uiChanges.empty());

    // reset to default: a single complete gesture
    host.begins.clear();
    host.ends.clear();
    knob->paramHandle().resetToDefault();
    CHECK_NEAR (host.store.plain (cutoff), paramTable()[cutoff].def, 1.0);
    CHECK_EQ (host.begins.size(), 1u);
    CHECK_EQ (host.ends.size(), 1u);

    // an external change of a modulation slot is picked up by the assign controller (the editor timer calls refresh ())
    const int source = indexOf ("mod01.source"), amount = indexOf ("mod01.amount"), dest = indexOf ("mod01.destination");
    CHECK (source >= 0 && amount >= 0 && dest >= 0);
    const auto before = editor.context().assign.snapshot();
    host.store.setFromHost (source, 1.0f);
    host.store.setFromHost (dest, 5.0f);
    host.store.setFromHost (amount, 50.0f);
    editor.context().assign.refresh();
    CHECK (! (editor.context().assign.snapshot() == before));

    // A/B goes through the store too
    host.store.setFromHost (indexOf ("master.volume"), -6.0f);
    editor.toggleAb();
    host.store.setFromHost (indexOf ("master.volume"), -20.0f);
    editor.toggleAb();
    CHECK_NEAR (host.store.plain (indexOf ("master.volume")), -6.0, 0.05);
}

TEST (uph_panels_registered_explicitly_and_layout_at_the_mpc_size)
{
    startMessageThread();
    // registration is a function call: every SYNTHYBOY_REGISTER_PANEL of the panels directory is in the registry, in the order (page, order, id)
    const auto& entries = PanelRegistry::instance().entries();
    CHECK (entries.size() >= 60);
    for (size_t i = 1; i < entries.size(); ++i)
        CHECK (static_cast<int> (entries[i - 1].page) < static_cast<int> (entries[i].page)
               || (entries[i - 1].page == entries[i].page && entries[i - 1].order <= entries[i].order));
    CHECK (PanelRegistry::instance().entries().size() == entries.size()); // a second call does not register twice

    MemoryHost host;
    SynthyBoyEditor editor (host);
    for (const auto size : { juce::Point<int> (1024, 600), juce::Point<int> (1024, 768), juce::Point<int> (390, 844) })
    {
        editor.setSize (size.x, size.y);
        CHECK (editor.panelHost().allPanels().size() == entries.size());
        for (const auto& panel : editor.panelHost().allPanels())
            CHECK (panel->getWidth() > 0 || panel->hiddenInLayout() || ! panel->isVisible());
        CHECK (editor.getLocalBounds().contains (editor.panelHost().getBounds()));
        CHECK (editor.getLocalBounds().contains (editor.keyboardStrip().getBounds()));
    }
}

namespace
{
template <typename T>
T* findDescendant (juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (child))
            return t;
        if (auto* found = findDescendant<T> (*child))
            return found;
    }
    return nullptr;
}
} // namespace

TEST (uph_overlays_stay_inside_an_embedded_editor)
{
    startMessageThread();
    MemoryHost host;
    CHECK (host.overlaysInsideEditor()); // the default of the UiHost contract
    // the editor sits in somebody else's window (the Kick PluginPanel): its top level component is NOT the editor
    juce::Component window;
    window.setSize (1280, 800);
    SynthyBoyEditor editor (host);
    window.addAndMakeVisible (editor);
    editor.setBounds (128, 100, 1024, 600);
    CHECK (editor.getTopLevelComponent() == &window);

    ParamControl* knob = nullptr;
    for (int i = 0; i < kNumPages && knob == nullptr; ++i)
    {
        editor.panelHost().setPage (static_cast<Page> (i));
        knob = findControl (editor.panelHost(), "filter1.cutoff");
    }
    CHECK (knob != nullptr);
    if (knob == nullptr)
        return;
    CHECK (overlayRootOf (*knob) == &editor);

    // a call-out (the long-press sheet) is a child of the editor, not a window of the desktop
    const int desktopBefore = juce::Desktop::getInstance().getNumComponents();
    knob->showSheet();
    auto* callOut = findDescendant<juce::CallOutBox> (editor);
    CHECK (callOut != nullptr);
    CHECK_EQ (juce::Desktop::getInstance().getNumComponents(), desktopBefore);
    if (callOut != nullptr)
    {
        CHECK (editor.getLocalBounds().contains (callOut->getBounds()));
        callOut->dismiss();
    }

    // the text prompt (a preset name) is a child of the editor too: confirm it, the callback gets the text, and it is gone
    juce::String answer;
    askText (editor.context(), "Save shape", "Init", [&answer] (const juce::String& text) { answer = text; });
    juce::Component* promptSheet = nullptr; // the child of the editor with a text field and a "Save" button
    for (auto* child : editor.getChildren())
        if (findDescendant<juce::TextEditor> (*child) != nullptr)
            for (auto* grand : child->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (grand); button != nullptr && button->getButtonText() == "Save")
                    promptSheet = child;
    auto* prompt = promptSheet != nullptr ? findDescendant<juce::TextEditor> (*promptSheet) : nullptr;
    CHECK (prompt != nullptr);
    CHECK_EQ (juce::Desktop::getInstance().getNumComponents(), desktopBefore);
    if (prompt != nullptr)
    {
        CHECK (prompt->getText() == "Init");
        prompt->setText ("My shape", false);
        juce::Component* sheetComponent = prompt->getParentComponent();
        CHECK (sheetComponent != nullptr && sheetComponent->getParentComponent() == &editor);
        for (auto* child : sheetComponent->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == "Save")
                button->onClick();
        CHECK (answer == "My shape");
    }

    // a toast and the loupe go to the editor, not to the window around it
    Toast::show (editor.context(), *knob, "Hello", 1000);
    CHECK (Toast::current() != nullptr && Toast::current()->getParentComponent() == &editor);

    // an editor that goes away with a call-out open takes it along
    knob->showSheet();
    CHECK (findDescendant<juce::CallOutBox> (editor) != nullptr);
}

TEST (uph_zz_shutdown)
{
    delete juceInit;
    juceInit = nullptr;
    CHECK (true);
}
