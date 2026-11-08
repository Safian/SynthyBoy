// SynthyBoy - UI tests: Effects panels, interactions, permutation logic, contract, and headless screenshots.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "ui/Sheet.h"
#include "ui/panels/FxCommon.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInit = new juce::ScopedJuceInitialiser_GUI();

class CapsHost final : public UiHost
{
public:
    CapsHost (SynthyBoyProcessor& p, EngineCapsMask c) : inner (p), caps (c) {}
    juce::AudioProcessorValueTreeState& parameters() override { return inner.parameters(); }
    EngineCapsMask engineCaps() const noexcept override { return caps; }
    juce::MidiKeyboardState& keyboardState() override { return inner.keyboardState(); }
    AbCompare& abCompare() override { return inner.abCompare(); }
    void stepPreset (int d) override { inner.stepPreset (d); }
    juce::String presetName() const override { return inner.presetName(); }

private:
    SynthyBoyProcessor& inner;
    EngineCapsMask caps;
};

bool writePng (juce::Component& c, const juce::File& file)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    if (! image.isValid())
        return false;
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}
} // namespace

TEST (fx_pure_logic_permutation_and_math)
{
    // 1. Permutation repair: Identity
    std::array<int, 16> id {};
    for (int i = 0; i < 16; ++i)
        id[static_cast<size_t> (i)] = i;
    const auto repairedId = repairFxPermutation (id);
    for (int i = 0; i < 16; ++i)
        CHECK_EQ (repairedId[static_cast<size_t> (i)], i);

    // 2. Permutation repair: Duplicates
    std::array<int, 16> dups {};
    for (int i = 0; i < 16; ++i)
        dups[static_cast<size_t> (i)] = i / 2; // only 0..7 used, duplicated
    const auto repairedDups = repairFxPermutation (dups);
    std::array<bool, 16> seen {};
    for (int i = 0; i < 16; ++i)
    {
        const int val = repairedDups[static_cast<size_t> (i)];
        CHECK (val >= 0 && val < 16);
        CHECK (! seen[static_cast<size_t> (val)]);
        seen[static_cast<size_t> (val)] = true;
    }

    // 3. Permutation repair: Out of bounds
    std::array<int, 16> outOfBounds { -5, 99, 100, -1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    const auto repairedOob = repairFxPermutation (outOfBounds);
    std::array<bool, 16> seenOob {};
    for (int i = 0; i < 16; ++i)
    {
        const int val = repairedOob[static_cast<size_t> (i)];
        CHECK (val >= 0 && val < 16);
        CHECK (! seenOob[static_cast<size_t> (val)]);
        seenOob[static_cast<size_t> (val)] = true;
    }

    // 4. Frequency normalization roundtrip
    CHECK_NEAR (fxFreqToNorm (20.0f), 0.0f, 1e-4);
    CHECK_NEAR (fxFreqToNorm (20000.0f), 1.0f, 1e-4);
    for (float f : { 50.0f, 440.0f, 1000.0f, 5000.0f, 12000.0f })
    {
        const float norm = fxFreqToNorm (f);
        const float roundtrip = fxNormToFreq (norm);
        CHECK_NEAR (roundtrip, f, 0.1f);
    }

    // 5. dB normalization roundtrip
    CHECK_NEAR (fxDbToNorm (-30.0f), 0.0f, 1e-4);
    CHECK_NEAR (fxDbToNorm (30.0f), 1.0f, 1e-4);
    for (float db : { -20.0f, -6.0f, 0.0f, 12.0f, 24.0f })
    {
        const float norm = fxDbToNorm (db);
        const float roundtrip = fxNormToDb (norm);
        CHECK_NEAR (roundtrip, db, 0.01f);
    }
}

TEST (fx_panel_registry_contract)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    const auto& entries = PanelRegistry::instance().entries();
    int fxCount = 0;

    for (const auto& e : entries)
    {
        if (e.page != Page::fx)
            continue;

        ++fxCount;
        sbtest::Context c (e.id.c_str());
        auto panel = e.create (ctx);
        panel->build();

        CHECK (panel->title().isNotEmpty());
        const auto keys = panel->keys();
        CHECK (! keys.empty());
        CHECK_EQ (keys.size(), panel->controls().size());

        for (size_t i = 0; i < keys.size(); ++i)
        {
            CHECK (resolveKey (keys[i]).valid());
            CHECK_EQ (keys[i], panel->controls()[i]->key());
        }

        // Principle 1: at most 4 primary controls
        CHECK (static_cast<int> (panel->controls().size()) - panel->numMoreControls() <= 4);

        // Preferred heights at phone (390) and tablet (1024)
        CHECK (panel->preferredHeight (390) > 0);
        CHECK (panel->preferredHeight (1024) > 0);
    }

    // All 17 FX panels must be present
    CHECK_EQ (fxCount, 17);
}

TEST (fx_direct_manipulation_and_interactions)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    // 1. Direct manipulation on Chorus visual updates cutoff and spread
    ParamHandle cutoff (ctx, "chorus.filter_cutoff");
    cutoff.setPlainComplete (1000.0f);
    CHECK_NEAR (cutoff.plain(), 1000.0, 1.0);

    ParamHandle spread (ctx, "chorus.filter_spread");
    spread.setPlainComplete (50.0f);
    CHECK_NEAR (spread.plain(), 50.0, 0.1);

    // 2. Chain order reordering and permutation write
    ParamHandle order1 (ctx, "fx.order.1");
    ParamHandle order2 (ctx, "fx.order.2");
    order1.setPlainComplete (3.0f);
    order2.setPlainComplete (0.0f);
    CHECK_NEAR (order1.plain(), 3.0, 0.01);
    CHECK_NEAR (order2.plain(), 0.0, 0.01);

    // 3. Reverb force enable parameter
    ParamHandle force (ctx, "reverb.force_enable");
    force.setPlainComplete (1.0f);
    CHECK_NEAR (force.plain(), 1.0, 0.01);
}

TEST (fx_headless_render_and_screenshots)
{
    SynthyBoyProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<SynthyBoyEditor*> (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    editor->panelHost().setPage (Page::fx);

    const juce::File shotDir ("/tmp/ui-shots/fx");
    shotDir.createDirectory();

    // 1. Phone portrait 390x844
    editor->setSize (390, 844);
    editor->setSourceBarOpen (false);
    const auto phoneFile = shotDir.getChildFile ("phone-390x844.png");
    CHECK (writePng (*editor, phoneFile));
    CHECK (phoneFile.getSize() > 2000);
    std::printf ("    screenshot %s (%lld bytes)\n",
                 phoneFile.getFullPathName().toRawUTF8(), static_cast<long long> (phoneFile.getSize()));

    // 2. Tablet landscape 1024x768
    editor->setSize (1024, 768);
    const auto tabletFile = shotDir.getChildFile ("tablet-1024x768.png");
    CHECK (writePng (*editor, tabletFile));
    CHECK (tabletFile.getSize() > 2000);
    std::printf ("    screenshot %s (%lld bytes)\n",
                 tabletFile.getFullPathName().toRawUTF8(), static_cast<long long> (tabletFile.getSize()));
}

TEST (fx_zz_shutdown)
{
    delete juceInit;
    juceInit = nullptr;
    CHECK (true);
}
