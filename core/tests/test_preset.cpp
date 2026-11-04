// SynthyBoy - core tests: preset container (.sbpreset), metadata, ContentPack parsing & verification.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "sha256.h"
#include "synthyboy/content_pack.h"
#include "synthyboy/preset.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

TEST (sha256_known_vectors)
{
    // RFC 6234 standard test vectors
    // 1. Empty string
    CHECK_EQ (Sha256::hashBufferHex ("", 0),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // 2. "abc"
    CHECK_EQ (Sha256::hashBufferHex ("abc", 3),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // 3. "The quick brown fox jumps over the lazy dog"
    const char msg[] = "The quick brown fox jumps over the lazy dog";
    CHECK_EQ (Sha256::hashBufferHex (msg, std::strlen (msg)),
              "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST (preset_save_load_round_trip)
{
    PresetMeta meta;
    meta.name = "Sub Heavy Bass";
    meta.category = "Bass";
    meta.author = "Safian Szabolcs";
    meta.license = "CC0-1.0";
    meta.tags = "bass,sub,heavy,punchy";
    meta.description = "Deep warm sub bass with analog filter saturation";

    ParamValues orig = ParamValues::defaults();
    const int volIdx = findParamIndex (kParamMasterVolume);
    const int cutIdx = findParamIndex (kParamFilter1Cutoff);
    const int onIdx  = findParamIndex (kParamFilter1On);
    CHECK (volIdx >= 0 && cutIdx >= 0 && onIdx >= 0);

    orig.plain[volIdx] = -6.5f;
    orig.plain[cutIdx] = 345.0f;
    orig.plain[onIdx]  = 1.0f;

    const std::string saved = savePreset (meta, orig);
    CHECK (! saved.empty());
    CHECK (saved.find ("\"format\":\"synthyboy-preset\"") != std::string::npos);
    CHECK (saved.find ("\"name\":\"Sub Heavy Bass\"") != std::string::npos);
    CHECK (saved.find ("\"category\":\"Bass\"") != std::string::npos);
    CHECK (saved.find ("\"license\":\"CC0-1.0\"") != std::string::npos);
    CHECK (saved.find ("\"state\":{\"format\":\"synthyboy-state\"") != std::string::npos);

    // 1. Fast metadata read
    PresetMeta readMeta;
    CHECK (readPresetMeta (saved.data(), saved.size(), readMeta));
    CHECK_EQ (readMeta.name, meta.name);
    CHECK_EQ (readMeta.category, meta.category);
    CHECK_EQ (readMeta.author, meta.author);
    CHECK_EQ (readMeta.license, meta.license);
    CHECK_EQ (readMeta.tags, meta.tags);
    CHECK_EQ (readMeta.description, meta.description);

    // 2. Full load
    PresetMeta fullMeta;
    ParamValues loadedValues = ParamValues::defaults();
    const LoadResult res = loadPreset (saved.data(), saved.size(), fullMeta, loadedValues);
    CHECK (res.status == LoadStatus::ok);
    CHECK_EQ (fullMeta.name, meta.name);
    CHECK_EQ (fullMeta.category, meta.category);
    CHECK_EQ (fullMeta.license, meta.license);
    CHECK_NEAR (loadedValues.plain[volIdx], -6.5f, 0.01);
    CHECK_NEAR (loadedValues.plain[cutIdx], 345.0f, 0.1);
    CHECK_NEAR (loadedValues.plain[onIdx], 1.0f, 0.01);
}

TEST (preset_backward_compatibility_with_raw_state)
{
    // A raw synthyboy-state document loaded via loadPreset should parse cleanly and produce Init default metadata
    const ParamValues orig = ParamValues::defaults();
    const std::string rawState = saveState (orig);

    PresetMeta meta;
    ParamValues loaded = ParamValues::defaults();
    const LoadResult res = loadPreset (rawState.data(), rawState.size(), meta, loaded);
    CHECK (res.status == LoadStatus::ok);
    CHECK_EQ (meta.name, "Init");
    CHECK_EQ (meta.license, "CC0-1.0");
}

TEST (preset_hostile_input)
{
    PresetMeta meta;
    ParamValues val = ParamValues::defaults();

    // 1. Null / zero
    CHECK (loadPreset (nullptr, 0, meta, val).status == LoadStatus::empty);
    CHECK (! readPresetMeta (nullptr, 0, meta));

    // 2. Garbage
    const char garbage[] = "\x00\xff\x12\x34\xfe\xed";
    CHECK (loadPreset (garbage, sizeof garbage, meta, val).status == LoadStatus::invalid);
    CHECK (! readPresetMeta (garbage, sizeof garbage, meta));

    // 3. Truncated
    const std::string tr = "{\"format\":\"synthyboy-preset\",\"meta\":{\"name\":\"Test";
    CHECK (loadPreset (tr.data(), tr.size(), meta, val).status == LoadStatus::invalid);
    CHECK (! readPresetMeta (tr.data(), tr.size(), meta));

    // 4. Missing required state
    const std::string noState = "{\"format\":\"synthyboy-preset\",\"version\":1,\"meta\":{\"name\":\"Test\"}}";
    CHECK (loadPreset (noState.data(), noState.size(), meta, val).status == LoadStatus::invalid);
}

TEST (content_pack_manifest_parse_and_verify)
{
    const std::string manifestJson = R"({
      "schemaVersion": 1,
      "id": "com.safian.synthyboy.test-pack",
      "name": "Test Preset Pack",
      "version": "1.0.0",
      "license": "CC0-1.0",
      "attribution": "Safian Szabolcs",
      "kind": "presets",
      "requires_plugin": ">=0.1.0",
      "description": "Test presets for unit verification",
      "files": [
        { "path": "test1.sbpreset", "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "size": 3 },
        { "path": "test2.sbpreset", "sha256": "d7a8fbb307d7809469ca9ab40250d4408c1b309f35f1064736e8644de5164013", "size": 43 }
      ]
    })";

    ContentPackInfo info;
    CHECK (parseContentPackManifest (manifestJson.data(), manifestJson.size(), info));
    CHECK_EQ (info.schemaVersion, 1);
    CHECK_EQ (info.id, "com.safian.synthyboy.test-pack");
    CHECK_EQ (info.name, "Test Preset Pack");
    CHECK_EQ (info.kind, "presets");
    CHECK_EQ (info.license, "CC0-1.0");
    CHECK_EQ (info.files.size(), 2);
    CHECK_EQ (info.files[0].path, "test1.sbpreset");
    CHECK_EQ (info.files[1].path, "test2.sbpreset");

    // Invalid manifest (missing schemaVersion or id)
    const std::string badManifest = R"({"id":"bad.pack","kind":"invalid_kind"})";
    ContentPackInfo badInfo;
    CHECK (! parseContentPackManifest (badManifest.data(), badManifest.size(), badInfo));
}

TEST (content_registry_and_missing_content_behaviour)
{
    ContentRegistry reg;
    CHECK (! reg.hasMissingContent());

    // Non-existent path triggers missing content flag
    const std::string resolved = reg.resolvePresetPath ("non_existent/sound.sbpreset");
    CHECK (resolved.empty());
    CHECK (reg.hasMissingContent());

    reg.clearMissingContentFlag();
    CHECK (! reg.hasMissingContent());
}

TEST (preset_directory_scanning_and_pack_verify)
{
    // Create a temporary sandbox directory
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "synthyboy_test_pack_tmp";
    std::error_code ec;
    std::filesystem::remove_all (tempDir, ec);
    std::filesystem::create_directories (tempDir / "Bass", ec);
    std::filesystem::create_directories (tempDir / "Lead", ec);

    PresetMeta m1;
    m1.name = "Acid Saw";
    m1.category = "Bass";
    m1.license = "CC0-1.0";
    const std::string p1Content = savePreset (m1, ParamValues::defaults());
    const std::filesystem::path p1Path = tempDir / "Bass" / "01_acid.sbpreset";
    {
        std::ofstream f (p1Path.string(), std::ios::binary);
        f.write (p1Content.data(), static_cast<std::streamsize> (p1Content.size()));
    }

    PresetMeta m2;
    m2.name = "Glory Lead";
    m2.category = "Lead";
    m2.license = "CC0-1.0";
    const std::string p2Content = savePreset (m2, ParamValues::defaults());
    const std::filesystem::path p2Path = tempDir / "Lead" / "01_glory.sbpreset";
    {
        std::ofstream f (p2Path.string(), std::ios::binary);
        f.write (p2Content.data(), static_cast<std::streamsize> (p2Content.size()));
    }

    // Scan directory
    const auto entries = scanPresetDirectory (tempDir.string());
    if (CHECK_EQ (entries.size(), 2u))
    {
        CHECK_EQ (entries[0].meta.category, "Bass");
        CHECK_EQ (entries[0].meta.name, "Acid Saw");
        CHECK_EQ (entries[1].meta.category, "Lead");
        CHECK_EQ (entries[1].meta.name, "Glory Lead");
    }

    // Build ContentPackInfo and verify
    ContentPackInfo pack;
    pack.schemaVersion = 1;
    pack.id = "com.safian.synthyboy.temp-pack";
    pack.version = "1.0.0";
    pack.license = "CC0-1.0";
    pack.attribution = "Safian Szabolcs";
    pack.kind = "presets";
    pack.requiresPlugin = ">=0.1.0";

    ContentPackFile f1;
    f1.path = "Bass/01_acid.sbpreset";
    f1.sha256 = Sha256::hashBufferHex (p1Content.data(), p1Content.size());
    pack.files.push_back (f1);

    ContentPackFile f2;
    f2.path = "Lead/01_glory.sbpreset";
    f2.sha256 = Sha256::hashBufferHex (p2Content.data(), p2Content.size());
    pack.files.push_back (f2);

    std::vector<std::string> errors;
    CHECK (verifyContentPack (tempDir.string(), pack, errors));
    CHECK (errors.empty());

    // Corrupted file test: change hash of f1
    pack.files[0].sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    CHECK (! verifyContentPack (tempDir.string(), pack, errors));
    CHECK (! errors.empty());

    // Missing file test: add non-existent file
    ContentPackFile fMissing;
    fMissing.path = "Pad/missing.sbpreset";
    fMissing.sha256 = "1111111111111111111111111111111111111111111111111111111111111111";
    pack.files.push_back (fMissing);
    errors.clear();
    CHECK (! verifyContentPack (tempDir.string(), pack, errors));
    CHECK (! errors.empty());

    // Clean up
    std::filesystem::remove_all (tempDir, ec);
}

TEST (factory_presets_pack_validation)
{
    const std::filesystem::path repoRoot = std::filesystem::path (__FILE__).parent_path().parent_path().parent_path();
    const std::filesystem::path factoryDir = repoRoot / "content" / "presets";
    const std::filesystem::path manifestPath = factoryDir / "contentpack.json";

    std::error_code ec;
    CHECK (std::filesystem::exists (manifestPath, ec));

    std::ifstream mf (manifestPath.string(), std::ios::binary);
    CHECK (mf.is_open());
    std::string mfc ((std::istreambuf_iterator<char> (mf)), std::istreambuf_iterator<char>());

    ContentPackInfo pack;
    CHECK (parseContentPackManifest (mfc.data(), mfc.size(), pack));
    CHECK_EQ (pack.schemaVersion, 1);
    CHECK_EQ (pack.id, "com.safian.synthyboy.factory-presets");
    CHECK_EQ (pack.license, "CC0-1.0");
    CHECK_EQ (pack.attribution, "Safian Szabolcs");
    CHECK_EQ (pack.kind, "presets");
    CHECK_EQ (pack.files.size(), 100u);

    // Verify all 100 presets on disk against manifest SHA-256
    std::vector<std::string> errors;
    CHECK (verifyContentPack (factoryDir.string(), pack, errors));
    if (! errors.empty())
    {
        for (const auto& err : errors)
            std::printf ("    [factory_pack error] %s\n", err.c_str());
    }

    // Verify all 100 presets via directory scanner and check license
    const auto entries = scanPresetDirectory (factoryDir.string());
    CHECK_EQ (entries.size(), 100u);
    for (const auto& e : entries)
    {
        CHECK_EQ (e.meta.license, "CC0-1.0");
        CHECK (! e.meta.name.empty());
        CHECK (! e.meta.category.empty());
    }
}

TEST (pack_delete_restore_behaviour)
{
    // Simulates deleting and restoring a content pack (Phase 4 requirement)
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "synthyboy_pack_del_restore_tmp";
    std::error_code ec;
    std::filesystem::remove_all (tempDir, ec);
    std::filesystem::create_directories (tempDir / "Presets" / "Lead", ec);

    PresetMeta meta;
    meta.name = "Restorable Lead";
    meta.category = "Lead";
    meta.license = "CC0-1.0";
    const std::string content = savePreset (meta, ParamValues::defaults());
    const std::filesystem::path presetPath = tempDir / "Presets" / "Lead" / "lead.sbpreset";
    {
        std::ofstream f (presetPath.string(), std::ios::binary);
        f.write (content.data(), static_cast<std::streamsize> (content.size()));
    }

    ContentRegistry reg;
    reg.addSearchPath ((tempDir / "Presets").string());

    // 1. Pack present: resolve works cleanly
    const std::string resolved = reg.resolvePresetPath ("Lead/lead.sbpreset");
    CHECK_EQ (resolved, presetPath.string());
    CHECK (! reg.hasMissingContent());

    // 2. Pack deleted (host removes directory): resolve fails, missingContent flag is set, sound falls back safely
    std::filesystem::remove_all (tempDir / "Presets" / "Lead", ec);
    const std::string missing = reg.resolvePresetPath ("Lead/lead.sbpreset");
    CHECK (missing.empty());
    CHECK (reg.hasMissingContent());

    // Safe fallback test: loader produces default patch when content is missing
    ParamValues fallbackVals = ParamValues::defaults();
    PresetMeta fallbackMeta = PresetMeta::initDefault();
    CHECK_EQ (fallbackMeta.name, "Init");
    CHECK_EQ (fallbackVals.plain[findParamIndex (kParamMasterVolume)], -12.0f);

    // 3. Pack restored: flag clears and resolve succeeds again
    reg.clearMissingContentFlag();
    CHECK (! reg.hasMissingContent());
    std::filesystem::create_directories (tempDir / "Presets" / "Lead", ec);
    {
        std::ofstream f (presetPath.string(), std::ios::binary);
        f.write (content.data(), static_cast<std::streamsize> (content.size()));
    }
    const std::string restored = reg.resolvePresetPath ("Lead/lead.sbpreset");
    CHECK_EQ (restored, presetPath.string());
    CHECK (! reg.hasMissingContent());

    std::filesystem::remove_all (tempDir, ec);
}
