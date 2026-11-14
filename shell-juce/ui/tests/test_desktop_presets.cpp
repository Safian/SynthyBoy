// SynthyBoy - desktop factory presets and program management tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EmbeddedPresets.h"
#include "PluginProcessor.h"
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
