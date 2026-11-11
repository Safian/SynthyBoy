// SynthyBoy - UI / shell tests of the shell features (docs/ui-design/15-shell-features.md): MIDI learn (G07), Save as Default (G02) and the
// module Lock (G08). Headless: the processor is driven through processBlock with real MIDI buffers, the sheets through their buttons.
// Screenshots of the changed screens go to $SYNTHYBOY_UI_SHOTS_DIR (default /tmp/ui-shots)/shellfeat/ (phone 390x844, tablet 1024x768).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "PluginProcessor.h"
#include "EditorAdapter.h"
#include "ui/Editor.h"
#include "ui/GestureKit.h"
#include "ui/ModuleSheet.h"
#include "ui/Overlays.h"
#include "ui/Sheet.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInitShell = nullptr;
// started in main() (sbtest::runStartups), never during static initialisation: that crashes on Windows
SBTEST_STARTUP (juceInitShell, [] { juceInitShell = new juce::ScopedJuceInitialiser_GUI(); }, [] { delete juceInitShell; juceInitShell = nullptr; });

/** The user data directory of every processor of this test: a temp directory, never the real one. Set before any processor exists. */
const std::filesystem::path& userDir()
{
    static const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sb_test_ui_shellfeat_user";
    return dir;
}
const bool userDirSet = []
{
    std::filesystem::remove_all (userDir());
    sbtest::setEnv ("SYNTHYBOY_USER_DIR", userDir().string().c_str());
    return true;
}();

void removeDefault()
{
    clearUserDefault (userDir().string());
}

juce::File shotDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_UI_SHOTS_DIR", {});
    return juce::File (env.isNotEmpty() ? env : juce::String ("/tmp/ui-shots")).getChildFile ("shellfeat");
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    if (! image.isValid())
        return false;
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}

bool approx (float a, float b, float tol = 0.01f)
{
    return std::abs (a - b) <= tol;
}

struct Rig
{
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base { proc.createEditor() };
    SynthyBoyEditor* editor = innerEditor (base.get());

    explicit Rig (Page page = Page::flt, int w = 390, int h = 844)
    {
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
        editor->setSourceBarOpen (false);
        editor->setSize (w, h);
        editor->panelHost().setPage (page);
        editor->pageTabs().select (page, false);
        proc.setPlayConfigDetails (0, 2, 48000.0, 128);
        proc.prepareToPlay (48000.0, 128);
    }

    void setParam (const char* key, float plain)
    {
        auto* p = proc.parameters().getParameter (key);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }
    float param (const char* key) { return proc.parameters().getRawParameterValue (key)->load(); }

    /** One audio block with a controller message in it. */
    void controller (int cc, int value, int channel = 1)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (channel, cc, value), 0);
        proc.processBlock (buffer, midi);
    }

    template <typename T>
    T* find (juce::Component& c, const std::function<bool (T&)>& pred = {})
    {
        if (auto* t = dynamic_cast<T*> (&c))
            if (! pred || pred (*t))
                return t;
        for (auto* child : c.getChildren())
            if (auto* t = find<T> (*child, pred))
                return t;
        return nullptr;
    }
    template <typename T>
    T* find (const std::function<bool (T&)>& pred = {})
    {
        return find<T> (*editor, pred);
    }

    void shot (const juce::String& name, int w, int h, Page page)
    {
        editor->setSize (w, h);
        editor->panelHost().setPage (page);
        const auto file = shotDir().getChildFile (name + ".png");
        CHECK (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), file));
        std::printf ("    [UI SHOT] %s\n", file.getFullPathName().toRawUTF8());
        editor->setSize (390, 844);
        editor->panelHost().setPage (page);
    }
};

const ParamInfo& info (const char* key)
{
    return *findParamByKey (key);
}

juce::TextButton* buttonNamed (Rig& rig, const char* text)
{
    return rig.find<juce::TextButton> ([text] (juce::TextButton& b) { return b.getButtonText() == text; });
}
} // namespace

TEST (shf_user_dir_is_isolated)
{
    CHECK (userDirSet);
    Rig rig;
    CHECK (rig.proc.userDataDir() == userDir().string());
}

TEST (shf_midi_learn_through_the_param_sheet)
{
    removeDefault();
    Rig rig (Page::flt);
    auto& ctx = rig.editor->context();
    const auto& cutoff = info ("filter1.cutoff");
    CHECK (rig.proc.canMidiLearn());

    ParamSheet sheet (ctx, "filter1.cutoff");
    sheet.setSize (sheet.preferredWidth(), sheet.preferredHeight());
    auto& learn = sheet.learnButton();
    CHECK (learn.isEnabled());
    CHECK (learn.getButtonText() == "MIDI learn");

    // arm: the processor waits for a controller; a reserved CC (sustain) is not learned
    rig.setParam ("filter1.cutoff", 60.0f);
    learn.onClick();
    CHECK (rig.proc.midiLearnKey() == "filter1.cutoff");
    rig.controller (64, 100);
    CHECK (rig.proc.midiLearnKey() == "filter1.cutoff");

    // the first learnable CC is mapped and does not move the knob
    rig.controller (21, 90);
    CHECK (rig.proc.midiLearnKey().empty());
    CHECK_EQ (rig.proc.midiCcForKey ("filter1.cutoff"), 21);
    CHECK (approx (rig.param ("filter1.cutoff"), 60.0f, 0.05f));

    // from now on the controller moves it, from any channel; the value is the table's mapping of CC / 127
    rig.controller (21, 127, 3);
    CHECK (approx (rig.param ("filter1.cutoff"), cutoff.max, 0.5f));
    rig.controller (21, 0, 9);
    CHECK (approx (rig.param ("filter1.cutoff"), cutoff.min, 0.5f));
    rig.controller (21, 64);
    CHECK (approx (rig.param ("filter1.cutoff"), constrainValue (cutoff, fromNormalised (cutoff, 64.0f / 127.0f)), 0.5f));
    rig.controller (22, 127); // another controller: nothing
    CHECK (approx (rig.param ("filter1.cutoff"), constrainValue (cutoff, fromNormalised (cutoff, 64.0f / 127.0f)), 0.5f));

    // the knob shows the CC; the sheet offers to remove it
    ctx.assign.refresh();
    ctx.assign.sendSynchronousChangeMessage();
    auto* ring = rig.find<ModRing> ([] (ModRing& r) { return r.state().midiCc == 21; });
    CHECK (ring != nullptr);
    ParamSheet again (ctx, "filter1.cutoff");
    CHECK (again.learnButton().getButtonText() == "Remove CC 21");
    again.learnButton().onClick();
    CHECK_EQ (rig.proc.midiCcForKey ("filter1.cutoff"), -1);
    CHECK (again.learnButton().getButtonText() == "MIDI learn");
    rig.controller (21, 127);
    CHECK (approx (rig.param ("filter1.cutoff"), constrainValue (cutoff, fromNormalised (cutoff, 64.0f / 127.0f)), 0.5f)); // unmapped now
}

TEST (shf_midi_learn_cancel_remap_and_reserved)
{
    removeDefault();
    Rig rig;
    auto& ctx = rig.editor->context();

    ParamSheet sheet (ctx, "filter1.resonance");
    sheet.learnButton().onClick();
    CHECK (rig.proc.midiLearnKey() == "filter1.resonance");

    // armed: the knob says LEARN
    ctx.assign.refresh();
    ctx.assign.sendSynchronousChangeMessage();
    auto* ring = rig.find<ModRing> ([] (ModRing& r) { return r.state().learning; });
    CHECK (ring != nullptr);
    sheet.setSize (sheet.preferredWidth(), sheet.preferredHeight());
    CHECK (sheet.learnButton().getButtonText() == "MIDI learn"); // text refreshes when the sheet is reopened
    ParamSheet reopened (ctx, "filter1.resonance");
    CHECK (reopened.learnButton().getButtonText() == "Cancel learn");
    reopened.learnButton().onClick();
    CHECK (rig.proc.midiLearnKey().empty());
    rig.controller (30, 100);
    CHECK_EQ (rig.proc.midiCcForKey ("filter1.resonance"), -1);

    // arming another key replaces the first; learning a CC that is mapped elsewhere moves the mapping
    rig.proc.startMidiLearn ("filter1.resonance");
    rig.controller (30, 10);
    CHECK_EQ (rig.proc.midiCcForKey ("filter1.resonance"), 30);
    rig.proc.startMidiLearn ("filter2.resonance");
    rig.proc.startMidiLearn ("filter1.drive");
    CHECK (rig.proc.midiLearnKey() == "filter1.drive");
    rig.controller (30, 10);
    CHECK_EQ (rig.proc.midiCcForKey ("filter1.drive"), 30);
    CHECK_EQ (rig.proc.midiCcForKey ("filter1.resonance"), -1);

    // an unknown key arms nothing
    rig.proc.startMidiLearn ("no.such.key");
    CHECK (rig.proc.midiLearnKey().empty());
}

TEST (shf_midi_map_and_locks_are_saved_with_the_state)
{
    removeDefault();
    juce::MemoryBlock state;
    {
        Rig rig;
        rig.proc.startMidiLearn ("filter1.cutoff");
        rig.controller (21, 10);
        rig.proc.startMidiLearn ("osc1.level");
        rig.controller (22, 10);
        rig.proc.setModuleLocked ("osc2", true);
        rig.proc.setModuleLocked ("filter1", true);
        rig.proc.getStateInformation (state);
    }
    const std::string text (static_cast<const char*> (state.getData()), state.getSize());
    CHECK (text.find ("\"shell\":{") != std::string::npos);

    // the document is still a plain state document for the parameter loader
    auto values = ParamValues::defaults();
    CHECK (loadState (text.data(), text.size(), values).status == LoadStatus::ok);

    Rig other;
    CHECK_EQ (other.proc.midiCcForKey ("filter1.cutoff"), -1);
    other.proc.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK_EQ (other.proc.midiCcForKey ("filter1.cutoff"), 21);
    CHECK_EQ (other.proc.midiCcForKey ("osc1.level"), 22);
    CHECK (other.proc.isModuleLocked ("osc2") && other.proc.isModuleLocked ("filter1") && ! other.proc.isModuleLocked ("osc1"));
    other.controller (21, 127);
    CHECK (approx (other.param ("filter1.cutoff"), info ("filter1.cutoff").max, 0.5f));

    // a session restore is exact, locks included: a locked module is restored too
    other.setParam ("osc2.level", 0.9f);
    juce::MemoryBlock session;
    other.proc.getStateInformation (session);
    other.setParam ("osc2.level", 0.1f);
    other.proc.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (approx (other.param ("osc2.level"), 0.9f, 0.005f));
}

TEST (shf_module_lock_keeps_the_module_when_a_preset_is_loaded)
{
    removeDefault();
    Rig rig (Page::osc);
    auto& ctx = rig.editor->context();
    rig.setParam ("osc1.level", 0.25f);
    rig.setParam ("osc2.level", 0.5f);
    rig.setParam ("osc1.tune", 7.0f);

    // the sheet button toggles the lock, the header button shows it
    ModuleSheet sheet (ctx, "osc1");
    sheet.setSize (sheet.preferredWidth(), sheet.preferredHeight());
    auto* lock = sheet.button ("Lock module");
    CHECK (lock != nullptr && lock->isEnabled());
    CHECK (! rig.proc.isModuleLocked ("osc1"));
    lock->onClick();
    CHECK (rig.proc.isModuleLocked ("osc1"));
    CHECK (module_ops::isLocked (ctx, "osc1"));
    ModuleSheet reopened (ctx, "osc1");
    CHECK (reopened.button ("Unlock module") != nullptr && reopened.button ("Lock module") == nullptr);
    ctx.assign.sendSynchronousChangeMessage();
    auto* menu = rig.find<ModuleMenuButton> ([] (ModuleMenuButton& b) { return b.prefix() == "osc1"; });
    CHECK (menu != nullptr && menu->showsLock());
    auto* other = rig.find<ModuleMenuButton> ([] (ModuleMenuButton& b) { return b.prefix() == "osc2"; });
    CHECK (other != nullptr && ! other->showsLock());

    // a preset change: osc1 stays, everything else follows the preset
    auto preset = ParamValues::defaults();
    preset.plain[findParamIndex (info ("osc1.level").id)] = 0.9f;
    preset.plain[findParamIndex (info ("osc1.tune").id)] = -12.0f;
    preset.plain[findParamIndex (info ("osc2.level").id)] = 0.8f;
    CHECK (rig.proc.loadPatch (preset, StateContent {}));
    CHECK (approx (rig.param ("osc1.level"), 0.25f, 0.005f));
    CHECK (approx (rig.param ("osc1.tune"), 7.0f, 0.05f));
    CHECK (approx (rig.param ("osc2.level"), 0.8f, 0.005f));

    // a state without the shell section is a preset too; one of ours (with the section) is a session
    const std::string presetText = saveState (preset);
    rig.proc.setStateInformation (presetText.data(), static_cast<int> (presetText.size()));
    CHECK (approx (rig.param ("osc1.level"), 0.25f, 0.005f));
    CHECK (approx (rig.param ("osc2.level"), 0.8f, 0.005f));
    CHECK (rig.proc.isModuleLocked ("osc1")); // the locks of this instance stay

    // unlock: the preset overwrites it
    ModuleSheet third (ctx, "osc1");
    third.button ("Unlock module")->onClick();
    CHECK (! rig.proc.isModuleLocked ("osc1"));
    CHECK (rig.proc.loadPatch (preset, StateContent {}));
    CHECK (approx (rig.param ("osc1.level"), 0.9f, 0.005f));

    // not a module / not lockable
    CHECK (! module_ops::setLocked (ctx, "master", true));
    CHECK (! rig.proc.isModuleLocked ("master"));
}

TEST (shf_module_lock_keeps_the_content_of_the_module)
{
    removeDefault();
    Rig rig (Page::lfo);
    ContentLfoShape mine;
    mine.lfo = 2;
    mine.name = "mine";
    mine.points = { ContentPoint (0.0f, 0.0f), ContentPoint (1.0f, 1.0f) };
    StateContent content;
    content.lfoShapes.push_back (mine);
    CHECK (rig.proc.setContent (content));
    rig.proc.setModuleLocked ("lfo2", true);

    StateContent presetContent;
    ContentLfoShape theirs = mine;
    theirs.name = "theirs";
    presetContent.lfoShapes.push_back (theirs);
    CHECK (rig.proc.loadPatch (ParamValues::defaults(), presetContent));
    CHECK_EQ (rig.proc.content().lfoShapes.size(), static_cast<size_t> (1));
    CHECK (rig.proc.content().lfoShapes[0].name == "mine");

    rig.proc.setModuleLocked ("lfo2", false);
    CHECK (rig.proc.loadPatch (ParamValues::defaults(), presetContent));
    CHECK (rig.proc.content().lfoShapes[0].name == "theirs");
}

TEST (shf_save_as_default_new_instances_and_init_preset)
{
    removeDefault();
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    {
        Rig rig (Page::glb);
        auto* save = buttonNamed (rig, "Save as Default");
        auto* clear = buttonNamed (rig, "Clear Default");
        auto* init = buttonNamed (rig, "Init Preset");
        CHECK (save != nullptr && clear != nullptr && init != nullptr);
        if (save == nullptr || clear == nullptr || init == nullptr)
            return;
        CHECK (! rig.proc.hasUserDefault());
        CHECK (! clear->isEnabled());

        rig.setParam ("filter1.cutoff", 77.0f);
        rig.setParam ("osc2.tune", 5.0f);
        rig.proc.startMidiLearn ("master.volume");
        rig.controller (40, 10);
        save->onClick();
        CHECK (rig.proc.hasUserDefault());
        CHECK (clear->isEnabled());
        CHECK (std::filesystem::is_regular_file (userDefaultPath (userDir().string())));
        CHECK (Toast::current() != nullptr && Toast::current()->text() == "Saved as the default patch");

        // Init Preset now goes to the default, not to the factory values
        rig.setParam ("filter1.cutoff", 20.0f);
        rig.setParam ("osc2.tune", -9.0f);
        init->onClick();
        CHECK (approx (rig.param ("filter1.cutoff"), 77.0f, 0.5f));
        CHECK (approx (rig.param ("osc2.tune"), 5.0f, 0.05f));
        CHECK (Toast::current()->text() == "Preset initialised from your default");
    }
    {
        // a new instance starts from the default: values and the MIDI map
        Rig fresh (Page::glb);
        CHECK (approx (fresh.param ("filter1.cutoff"), 77.0f, 0.5f));
        CHECK (approx (fresh.param ("osc2.tune"), 5.0f, 0.05f));
        CHECK_EQ (fresh.proc.midiCcForKey ("master.volume"), 40);
        // "Revert Saved" of a new instance is the default as well
        fresh.setParam ("filter1.cutoff", 15.0f);
        CHECK (fresh.proc.revertToSaved());
        CHECK (approx (fresh.param ("filter1.cutoff"), 77.0f, 0.5f));

        // a locked module survives Init Preset
        fresh.proc.setModuleLocked ("osc2", true);
        fresh.setParam ("osc2.tune", -4.0f);
        fresh.setParam ("filter1.cutoff", 15.0f);
        auto* init = buttonNamed (fresh, "Init Preset");
        CHECK (init != nullptr);
        if (init != nullptr)
            init->onClick();
        CHECK (approx (fresh.param ("osc2.tune"), -4.0f, 0.05f));
        CHECK (approx (fresh.param ("filter1.cutoff"), 77.0f, 0.5f));

        // Clear Default: back to the factory init patch
        auto* clear = buttonNamed (fresh, "Clear Default");
        CHECK (clear != nullptr && clear->isEnabled());
        if (clear != nullptr)
            clear->onClick();
        CHECK (! fresh.proc.hasUserDefault());
        CHECK (Toast::current()->text() == "Default patch cleared");
        fresh.proc.setModuleLocked ("osc2", false);
        if (init != nullptr)
            init->onClick();
        CHECK (approx (fresh.param ("filter1.cutoff"), 12000.0f, 5.0f));
        CHECK (Toast::current()->text() == "Preset initialised");
    }
    {
        Rig plain;
        CHECK (approx (plain.param ("filter1.cutoff"), 12000.0f, 5.0f));
        CHECK_EQ (plain.proc.midiCcForKey ("master.volume"), -1);
    }
    removeDefault();
}

TEST (shf_a_broken_default_file_is_ignored)
{
    removeDefault();
    std::filesystem::create_directories (userDir());
    {
        std::ofstream out (userDefaultPath (userDir().string()), std::ios::binary | std::ios::trunc);
        out << "this is not a state document";
    }
    Rig rig;
    CHECK (approx (rig.param ("filter1.cutoff"), 12000.0f, 5.0f));
    CHECK (rig.proc.initPatch()); // falls back to the factory init patch
    CHECK (approx (rig.param ("filter1.cutoff"), 12000.0f, 5.0f));
    removeDefault();
}

TEST (shf_screenshots_of_the_changed_screens)
{
    removeDefault();
    {
        Rig rig (Page::flt);
        auto& ctx = rig.editor->context();
        rig.proc.startMidiLearn ("filter1.cutoff");
        rig.controller (21, 10);
        rig.proc.startMidiLearn ("filter1.resonance");
        ctx.assign.refresh();
        ctx.assign.sendSynchronousChangeMessage();
        rig.shot ("phone-filter-learn", 390, 844, Page::flt);
        rig.shot ("tablet-filter-learn", 1024, 768, Page::flt);
    }
    {
        Rig rig (Page::osc);
        rig.proc.setModuleLocked ("osc1", true);
        rig.editor->context().assign.sendSynchronousChangeMessage();
        rig.shot ("phone-osc-locked", 390, 844, Page::osc);
        rig.proc.setModuleLocked ("osc1", false);
    }
    {
        Rig rig (Page::glb);
        rig.shot ("phone-settings-default", 390, 844, Page::glb);
        rig.shot ("tablet-settings-default", 1024, 768, Page::glb);
    }
}
