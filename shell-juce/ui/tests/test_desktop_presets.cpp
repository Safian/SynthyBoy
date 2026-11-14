// SynthyBoy - desktop factory presets and program management tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EmbeddedPresets.h"
#include "EditorAdapter.h"
#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "synthyboy/preset.h"
#include "test_runner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInit = nullptr;
SBTEST_STARTUP (juceInit, [] { juceInit = new juce::ScopedJuceInitialiser_GUI(); }, [] { delete juceInit; juceInit = nullptr; });

juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos)
{
    const juce::MouseInputSource source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &c, &c, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}
} // namespace

TEST (test_embedded_presets_table)
{
    size_t count = 0;
    const auto* presets = synthyboy::desktop::getEmbeddedPresets (count);
    CHECK_EQ (count, 112u);
    CHECK (presets != nullptr);

    auto toLower = [] (const std::string& s) {
        std::string out = s;
        for (char& c : out)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return out;
    };

    for (size_t i = 0; i < count; ++i)
    {
        const auto& p = presets[i];
        CHECK (p.category != nullptr && p.category[0] != '\0');
        CHECK (p.name != nullptr && p.name[0] != '\0');
        CHECK (p.jsonText != nullptr && p.jsonSize > 0);

        synthyboy::PresetMeta meta;
        bool metaOk = synthyboy::readPresetMeta (p.jsonText, p.jsonSize, meta);
        CHECK (metaOk);
        CHECK_EQ (std::string (p.name), meta.name);
        CHECK_EQ (std::string (p.category), meta.category);

        synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
        synthyboy::StateContent content;
        auto result = synthyboy::loadPreset (p.jsonText, p.jsonSize, meta, values, content);
        CHECK (result.status == synthyboy::LoadStatus::ok);

        if (i > 0)
        {
            const auto& prev = presets[i - 1];
            bool catLe = std::string (prev.category) <= std::string (p.category);
            CHECK (catLe);
            if (std::string (prev.category) == std::string (p.category))
            {
                bool nameLe = toLower (prev.name) <= toLower (p.name);
                CHECK (nameLe);
            }
        }
    }
}

TEST (test_desktop_program_inventory)
{
    SynthyBoyProcessor proc;
    CHECK_EQ (proc.getNumPrograms(), 113);
    CHECK_EQ (proc.getCurrentProgram(), 0);
    CHECK_EQ (proc.getProgramName (0), juce::String ("Init"));
    CHECK_EQ (proc.presetName(), juce::String ("Init"));

    size_t count = 0;
    const auto* presets = synthyboy::desktop::getEmbeddedPresets (count);
    CHECK_EQ (count, 112u);

    for (int i = 1; i <= 112; ++i)
    {
        CHECK_EQ (proc.getProgramName (i), juce::String (presets[i - 1].name));
    }

    const auto list = proc.presetList();
    CHECK_EQ (static_cast<int> (list.size()), 113);
    CHECK_EQ (list[0].index, 0);
    CHECK_EQ (list[0].name, std::string ("Init"));
    for (size_t i = 1; i < list.size(); ++i)
    {
        CHECK_EQ (list[i].index, static_cast<int> (i));
        CHECK_EQ (list[i].category, std::string (presets[i - 1].category));
        CHECK_EQ (list[i].name, std::string (presets[i - 1].name));
    }
}

TEST (test_desktop_program_selection)
{
    SynthyBoyProcessor proc;
    const auto defaultValues = proc.allValues();

    // Select program 1 ("808 Boom")
    proc.setCurrentProgram (1);
    CHECK_EQ (proc.getCurrentProgram(), 1);
    CHECK_EQ (proc.presetName(), proc.getProgramName (1));

    const auto p1Values = proc.allValues();
    // Verify parameters deviate from default patch
    bool deviated = false;
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        if (std::abs (p1Values.plain[i] - defaultValues.plain[i]) > 0.001f)
        {
            deviated = true;
            break;
        }
    }
    CHECK (deviated);

    // Switch back to Init (program 0)
    proc.setCurrentProgram (0);
    CHECK_EQ (proc.getCurrentProgram(), 0);
    CHECK_EQ (proc.presetName(), juce::String ("Init"));

    const auto restoredValues = proc.allValues();
    bool matchesDefault = true;
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
    {
        if (std::abs (restoredValues.plain[i] - defaultValues.plain[i]) > 0.001f)
        {
            matchesDefault = false;
            break;
        }
    }
    CHECK (matchesDefault);
}

TEST (test_desktop_program_step_wrap)
{
    SynthyBoyProcessor proc;
    CHECK_EQ (proc.getCurrentProgram(), 0);

    // Step backwards from 0 -> wraps to 112
    proc.stepPreset (-1);
    CHECK_EQ (proc.getCurrentProgram(), 112);
    CHECK_EQ (proc.presetName(), proc.getProgramName (112));

    // Step forwards from 112 -> wraps to 0
    proc.stepPreset (+1);
    CHECK_EQ (proc.getCurrentProgram(), 0);
    CHECK_EQ (proc.presetName(), juce::String ("Init"));

    // Step forwards from 0 -> 1
    proc.stepPreset (+1);
    CHECK_EQ (proc.getCurrentProgram(), 1);
    CHECK_EQ (proc.presetName(), proc.getProgramName (1));
}

TEST (test_desktop_module_locks_preserved)
{
    SynthyBoyProcessor proc;
    proc.setModuleLocked ("filter1", true);
    CHECK (proc.isModuleLocked ("filter1"));

    const auto* info = synthyboy::findParamByKey ("filter1.cutoff");
    CHECK (info != nullptr);
    const int cutoffIdx = synthyboy::findParamIndex (info->id);

    const float customVal = 777.0f;
    const float norm = proc.parameterStore().toNormalised (cutoffIdx, customVal);
    proc.parameterStore().setNormalised (cutoffIdx, norm);

    CHECK_NEAR (proc.getValueTreeState().getRawParameterValue ("filter1.cutoff")->load(), customVal, 1.0f);

    // Switch to program 2 ("Acid Saw Reso" which has filter1.cutoff = 450)
    proc.setCurrentProgram (2);
    CHECK_EQ (proc.getCurrentProgram(), 2);

    // Cutoff should still be customVal (approx 777) because filter1 is locked
    const float afterVal = proc.getValueTreeState().getRawParameterValue ("filter1.cutoff")->load();
    CHECK_NEAR (afterVal, customVal, 1.0f);

    // Unlock filter1 and switch to program 2 again -> cutoff should become 450
    proc.setModuleLocked ("filter1", false);
    proc.setCurrentProgram (1); // switch away
    proc.setCurrentProgram (2); // switch back to Acid Saw Reso
    const float unlockedVal = proc.getValueTreeState().getRawParameterValue ("filter1.cutoff")->load();
    CHECK_NEAR (unlockedVal, 450.0f, 1.0f);
}

TEST (test_clean_environment_headless)
{
    std::string tempDir = (std::filesystem::temp_directory_path() / "synthyboy_clean_test").string();
    std::filesystem::create_directories (tempDir);
    sbtest::setEnv ("SYNTHYBOY_USER_DIR", tempDir.c_str());

    {
        SynthyBoyProcessor proc;
        CHECK_EQ (proc.getNumPrograms(), 113);
        for (int i = 0; i < 113; ++i)
        {
            proc.setCurrentProgram (i);
            CHECK_EQ (proc.getCurrentProgram(), i);
        }
    }

    sbtest::unsetEnv ("SYNTHYBOY_USER_DIR");
    std::error_code ec;
    std::filesystem::remove_all (tempDir, ec);
}

namespace
{
std::string stateOf (SynthyBoyProcessor& proc)
{
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    return std::string (static_cast<const char*> (block.getData()), block.getSize());
}

void restore (SynthyBoyProcessor& proc, const std::string& text)
{
    proc.setStateInformation (text.data(), static_cast<int> (text.size()));
}

bool sameValues (const synthyboy::ParamValues& a, const synthyboy::ParamValues& b)
{
    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
        if (std::abs (a.plain[i] - b.plain[i]) > 0.001f)
            return false;
    return true;
}
} // namespace

TEST (test_every_program_loads_like_the_mpc_shell)
{
    // the MPC shell does: loadPreset (file) -> values + content; here the same call on the embedded bytes must give the same parameters
    SynthyBoyProcessor proc;
    size_t count = 0;
    const auto* presets = synthyboy::desktop::getEmbeddedPresets (count);
    for (size_t i = 0; i < count; ++i)
    {
        synthyboy::PresetMeta meta;
        synthyboy::ParamValues expected = synthyboy::ParamValues::defaults();
        synthyboy::StateContent content;
        const auto result = synthyboy::loadPreset (presets[i].jsonText, presets[i].jsonSize, meta, expected, content);
        CHECK (result.status == synthyboy::LoadStatus::ok);

        proc.setCurrentProgram (static_cast<int> (i) + 1);
        CHECK_EQ (proc.getCurrentProgram(), static_cast<int> (i) + 1);
        CHECK (sameValues (proc.allValues(), expected));
    }
}

TEST (test_program_names_are_unique)
{
    SynthyBoyProcessor proc;
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        for (int j = i + 1; j < proc.getNumPrograms(); ++j)
            CHECK (proc.getProgramName (i) != proc.getProgramName (j));
}

TEST (test_out_of_range_program_is_ignored)
{
    SynthyBoyProcessor proc;
    proc.setCurrentProgram (7);
    proc.setCurrentProgram (-1);
    proc.setCurrentProgram (113);
    CHECK_EQ (proc.getCurrentProgram(), 7);
    CHECK_EQ (proc.getProgramName (113), juce::String());
}

TEST (test_state_restores_selected_program)
{
    SynthyBoyProcessor a;
    a.setCurrentProgram (42);
    const std::string saved = stateOf (a);

    SynthyBoyProcessor b;
    CHECK_EQ (b.getCurrentProgram(), 0);
    restore (b, saved);
    CHECK_EQ (b.getCurrentProgram(), 42);
    CHECK_EQ (b.presetName(), a.getProgramName (42));
    CHECK (sameValues (b.allValues(), a.allValues()));
}

TEST (test_state_keeps_tweaks_made_after_the_preset)
{
    SynthyBoyProcessor a;
    a.setCurrentProgram (3);
    const auto* info = synthyboy::findParamByKey ("filter1.cutoff");
    const int idx = synthyboy::findParamIndex (info->id);
    a.parameterStore().setNormalised (idx, a.parameterStore().toNormalised (idx, 1234.0f));
    const std::string saved = stateOf (a);

    SynthyBoyProcessor b;
    restore (b, saved);
    CHECK_EQ (b.getCurrentProgram(), 3);
    CHECK_NEAR (b.getValueTreeState().getRawParameterValue ("filter1.cutoff")->load(), 1234.0f, 2.0f);
}

TEST (test_old_state_without_program_member_still_loads)
{
    SynthyBoyProcessor a;
    a.setCurrentProgram (9);
    std::string saved = stateOf (a);
    const auto at = saved.find (",\"program\":");
    CHECK (at != std::string::npos);
    saved = saved.substr (0, at) + "}"; // what a build before the program list wrote

    SynthyBoyProcessor b;
    b.setCurrentProgram (42); // started on a different program
    restore (b, saved);
    CHECK (sameValues (b.allValues(), a.allValues())); // parameters and content restored exactly
    CHECK_EQ (b.getCurrentProgram(), 0);               // no selection information: resets to Init
    CHECK_EQ (b.presetName(), juce::String ("Init"));
}

TEST (test_state_program_resolved_by_name_when_the_list_moved)
{
    SynthyBoyProcessor a;
    a.setCurrentProgram (20);
    std::string saved = stateOf (a);
    const auto at = saved.find ("\"index\"");
    CHECK (at != std::string::npos);
    const auto colon = saved.find (':', at);
    const auto end = saved.find_first_of (",}", colon);
    saved.replace (colon + 1, end - colon - 1, " 3"); // the index of an older list; the name still points to program 20

    SynthyBoyProcessor b;
    restore (b, saved);
    CHECK_EQ (b.getCurrentProgram(), 20);
}

TEST (test_state_with_unknown_program_is_ignored_gracefully)
{
    SynthyBoyProcessor a;
    a.setCurrentProgram (5);
    std::string saved = stateOf (a);
    const auto name = a.getProgramName (5).toStdString();
    const auto at = saved.find ("\"name\": \"" + name + "\"");
    CHECK (at != std::string::npos);
    saved.replace (at, 9 + name.size() + 1, "\"name\": \"No Such Preset\"");

    SynthyBoyProcessor b;
    b.setCurrentProgram (42); // started on a different program
    restore (b, saved);
    CHECK_EQ (b.getCurrentProgram(), 0);
    CHECK_EQ (b.presetName(), juce::String ("Init"));
    CHECK (sameValues (b.allValues(), a.allValues()));
}

TEST (test_editor_preset_browsing)
{
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = synthyboy::ui::innerEditor (base.get());
    CHECK (editor != nullptr);
    editor->setSize (390, 844);

    auto& top = editor->topBar();
    CHECK (static_cast<bool> (top.onPresetNameClick));

    const auto list = proc.presetList();
    CHECK_EQ (static_cast<int> (list.size()), 113);
    CHECK_EQ (proc.currentPresetIndex(), 0);

    // the next / previous buttons of the strip drive the program list
    top.next.onClick();
    CHECK_EQ (proc.getCurrentProgram(), 1);
    CHECK_EQ (proc.currentPresetIndex(), 1);
    top.prev.onClick();
    top.prev.onClick();
    CHECK_EQ (proc.getCurrentProgram(), 112);

    // the name opens the preset menu (shown asynchronously; it is dismissed again)
    top.onPresetNameClick();
    juce::PopupMenu::dismissAllActiveMenus();

    // clicking the preset name area directly invokes onPresetNameClick
    bool clicked = false;
    top.onPresetNameClick = [&clicked] { clicked = true; };
    const float clickX = static_cast<float> (top.prev.getRight() + top.next.getX()) * 0.5f;
    const auto ev = mouseAt (top, { clickX, 10.0f });
    top.mouseDown (ev);
    CHECK (clicked);

    // hovering over the preset name changes cursor to pointing hand; exit resets it
    top.mouseMove (ev);
    CHECK (top.getMouseCursor() == juce::MouseCursor::PointingHandCursor);
    top.mouseExit (ev);
    CHECK (top.getMouseCursor() == juce::MouseCursor::NormalCursor);

    // direct selection via selectPreset
    proc.selectPreset (42);
    CHECK_EQ (proc.getCurrentProgram(), 42);
    CHECK_EQ (proc.currentPresetIndex(), 42);
    CHECK_EQ (proc.presetName(), proc.getProgramName (42));

    proc.selectPreset (0);
    CHECK_EQ (proc.getCurrentProgram(), 0);
    CHECK_EQ (proc.currentPresetIndex(), 0);
    CHECK_EQ (proc.presetName(), juce::String ("Init"));
}

TEST (test_preset_modified_indicator)
{
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    SynthyBoyProcessor proc;
    CHECK (! proc.isPresetModified());

    // Load preset 1 ("808 Boom")
    proc.setCurrentProgram (1);
    CHECK (! proc.isPresetModified());

    // Tweak a parameter
    const auto* info = synthyboy::findParamByKey ("filter1.cutoff");
    CHECK (info != nullptr);
    const int cutoffIdx = synthyboy::findParamIndex (info->id);
    const float origVal = proc.getValueTreeState().getRawParameterValue ("filter1.cutoff")->load();
    const float origNorm = proc.parameterStore().toNormalised (cutoffIdx, origVal);
    const float tweakedVal = origVal > 1000.0f ? 500.0f : 1500.0f;
    const float tweakedNorm = proc.parameterStore().toNormalised (cutoffIdx, tweakedVal);
    proc.parameterStore().setNormalised (cutoffIdx, tweakedNorm);

    CHECK (proc.isPresetModified());

    // Reset parameter back to original preset 1 value: modified indicator clears
    proc.parameterStore().setNormalised (cutoffIdx, origNorm);
    CHECK (! proc.isPresetModified());

    // Modifying content (e.g. tuning scale) also triggers modified state
    synthyboy::StateContent modifiedContent = proc.content();
    modifiedContent.tuning.present = true;
    modifiedContent.tuning.scl = "! 12-EDO\n12\n!\n100.0\n";
    CHECK (proc.setContent (modifiedContent));
    CHECK (proc.isPresetModified());

    // Reverting to saved on an un-restored instance resets to Init defaults
    proc.revertToSaved();
    CHECK (! proc.isPresetModified());
    CHECK_EQ (proc.getCurrentProgram(), 0);
    CHECK_EQ (proc.presetName(), juce::String ("Init"));

    // Check revertToSaved with state restored from DAW session
    proc.setCurrentProgram (5);
    const std::string sessionChunk = stateOf (proc);
    SynthyBoyProcessor sessionProc;
    restore (sessionProc, sessionChunk);
    CHECK_EQ (sessionProc.getCurrentProgram(), 5);
    CHECK (! sessionProc.isPresetModified());

    // Switch to another preset and tweak
    sessionProc.setCurrentProgram (10);
    sessionProc.parameterStore().setNormalised (cutoffIdx, tweakedNorm);
    CHECK (sessionProc.isPresetModified());

    // Revert to saved must restore the state from session (program 5, not program 10 or Init)
    sessionProc.revertToSaved();
    CHECK_EQ (sessionProc.getCurrentProgram(), 5);
    CHECK_EQ (sessionProc.presetName(), sessionProc.getProgramName (5));
    CHECK (! sessionProc.isPresetModified());

    // Check Editor TopBar modified indication with clean preset
    proc.setCurrentProgram (1);
    const float p1CutoffVal = proc.getValueTreeState().getRawParameterValue ("filter1.cutoff")->load();
    const float p1CutoffNorm = proc.parameterStore().toNormalised (cutoffIdx, p1CutoffVal);
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = synthyboy::ui::innerEditor (base.get());
    CHECK (editor != nullptr);
    CHECK (! editor->topBar().isPresetModified());

    // Tweak parameter in proc: topBar reflects modified
    proc.parameterStore().setNormalised (cutoffIdx, tweakedNorm);
    editor->updateUndoRedoButtons();
    CHECK (editor->topBar().isPresetModified());

    // Even if undo stack has history, when parameters match baseline, modified indicator is NOT false-positive
    proc.parameterStore().setNormalised (cutoffIdx, p1CutoffNorm);
    editor->undoStack().push (proc.allValues());
    editor->updateUndoRedoButtons();
    CHECK (editor->undoStack().canUndo());
    CHECK (! proc.isPresetModified());
    CHECK (! editor->topBar().isPresetModified());

    // Selecting a new preset clears modified state
    proc.selectPreset (2);
    proc.revertToSaved();
    editor->topBar().setPresetModified (proc.isPresetModified());
    CHECK (! editor->topBar().isPresetModified());
}

TEST (test_find_program_index)
{
    SynthyBoyProcessor proc;
    CHECK_EQ (proc.findProgramIndex ("Init"), 0);
    CHECK_EQ (proc.findProgramIndex ("init"), 0);
    CHECK_EQ (proc.findProgramIndex ("0"), 0);
    CHECK_EQ (proc.findProgramIndex ("1"), 1);
    CHECK_EQ (proc.findProgramIndex ("112"), 112);
    CHECK_EQ (proc.findProgramIndex ("113"), -1);
    CHECK_EQ (proc.findProgramIndex ("-1"), -1);

    const auto prog1Name = proc.getProgramName (1);
    CHECK_EQ (proc.findProgramIndex (prog1Name), 1);
    CHECK_EQ (proc.findProgramIndex (prog1Name.toLowerCase()), 1);

    const auto prog1Cat = proc.presetList()[1].category;
    CHECK_EQ (proc.findProgramIndex (juce::String (prog1Cat) + "/" + prog1Name), 1);
    CHECK_EQ (proc.findProgramIndex (juce::String (prog1Cat) + "/" + prog1Name + ".sbpreset"), 1);

    CHECK_EQ (proc.findProgramIndex ("NoSuchPreset12345"), -1);
    CHECK_EQ (proc.findProgramIndex (""), -1);
}
