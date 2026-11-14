// SynthyBoy - desktop factory presets and program management tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EmbeddedPresets.h"
#include "PluginProcessor.h"
#include "synthyboy/preset.h"
#include "test_runner.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

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
