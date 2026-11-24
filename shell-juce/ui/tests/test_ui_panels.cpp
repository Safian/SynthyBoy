// SynthyBoy - UI tests over ALL panels: every parameter key reachable, 44 point touch targets on every page and size, no literal
// colours in the UI sources, and the phone / tablet screenshots of every page (+ one overview image per size).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"
#include "EditorAdapter.h"
#include "ui/Editor.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
juce::ScopedJuceInitialiser_GUI* juceInitPanels = nullptr;
// started in main() (sbtest::runStartups), never during static initialisation: that crashes on Windows
SBTEST_STARTUP (juceInitPanels, [] { juceInitPanels = new juce::ScopedJuceInitialiser_GUI(); }, [] { delete juceInitPanels; juceInitPanels = nullptr; });

const char* pageName (Page p)
{
    switch (p)
    {
        case Page::osc: return "osc";
        case Page::mix: return "mix";
        case Page::flt: return "flt";
        case Page::env: return "env";
        case Page::lfo: return "lfo";
        case Page::mod: return "mod";
        case Page::fx:  return "fx";
        case Page::glb: return "glb";
    }
    return "?";
}

constexpr Page kPages[] = { Page::osc, Page::mix, Page::flt, Page::env, Page::lfo, Page::mod, Page::fx, Page::glb };

juce::File shotRoot()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SYNTHYBOY_UI_SHOTS_DIR", {});
    return juce::File (env.isNotEmpty() ? env : juce::String ("/tmp/ui-shots"));
}

juce::Image snapshot (juce::Component& c)
{
    return c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
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

/** Visible interactive descendants: buttons, parameter controls, combo boxes, sliders. */
void collectTargets (juce::Component& c, std::vector<juce::Component*>& out)
{
    if (! c.isVisible())
        return;
    if (dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<ParamControl*> (&c) != nullptr
        || dynamic_cast<juce::ComboBox*> (&c) != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr)
        out.push_back (&c);
    for (auto* child : c.getChildren())
        collectTargets (*child, out);
}

juce::String describe (juce::Component& c)
{
    const auto* button = dynamic_cast<juce::Button*> (&c);
    const juce::Component* parent = c.getParentComponent();
    return juce::String (typeid (c).name()) + " \"" + (button != nullptr ? button->getButtonText() : c.getName()) + "\" in "
           + (parent != nullptr ? juce::String (typeid (*parent).name()) : juce::String ("-"));
}

std::unique_ptr<SynthyBoyEditor, std::function<void (SynthyBoyEditor*)>> makeEditor (SynthyBoyProcessor& proc)
{
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = innerEditor (base.get());
    if (editor == nullptr)
        return { nullptr, [] (SynthyBoyEditor*) {} };
    auto* raw = base.release();
    return { editor, [raw] (SynthyBoyEditor*) { delete raw; } };
}
} // namespace

TEST (uip_every_parameter_key_has_a_control)
{
    SynthyBoyProcessor proc;
    auto editor = makeEditor (proc);
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    editor->setSize (1024, 768);

    std::set<std::string> reachable;
    for (const auto& panel : editor->panelHost().allPanels())
        for (const auto& key : panel->keys())
            reachable.insert (key);
    // the always visible shell: macro row, main volume, transport strip
    for (const auto& knob : editor->macroRow().knobs())
        reachable.insert (knob->key());
    reachable.insert (editor->macroRow().mainVolumeKnob().key());

    std::vector<std::string> missing;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const std::string key = paramTable()[i].key;
        // block B22: the keys of the effect instance pool are served by the rack view (type, on, rack, order) and by the effect panels through a key set
        // (pKK); that every one of them is reachable is proven by test_fxinstances_ui.cpp
        int poolSlot = 0, poolNumber = 0;
        if (fxInstanceOfParam (i, poolSlot, poolNumber))
            continue;
        if (reachable.count (key) == 0)
            missing.push_back (key);
    }
    for (const auto& key : missing)
        std::printf ("    no control: %s\n", key.c_str());
    CHECK_EQ (missing.size(), static_cast<size_t> (0));
}

TEST (uip_touch_targets_on_every_page_and_size)
{
    SynthyBoyProcessor proc;
    auto editor = makeEditor (proc);
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    editor->setSourceBarOpen (false);

    struct Case { int w, h; const char* name; };
    const Case cases[] = { { 390, 844, "phone 390x844" }, { 1024, 768, "tablet 1024x768" } };
    for (const auto& cs : cases)
    {
        for (const auto page : kPages)
        {
            sbtest::Context context ((std::string (cs.name) + " / " + pageName (page)).c_str());
            editor->setSize (cs.w, cs.h);
            editor->panelHost().setPage (page);
            std::vector<juce::Component*> targets;
            for (auto* panel : editor->panelHost().visiblePanels())
                collectTargets (*panel, targets);
            const int min = editor->theme().touch();
            for (auto* c : targets)
            {
                if (c->getWidth() == 0 || c->getHeight() == 0)
                    continue;
                const bool ok = c->getWidth() >= min && c->getHeight() >= min;
                if (! ok)
                    std::printf ("    too small [%s / %s]: %s %dx%d\n", cs.name, pageName (page), describe (*c).toRawUTF8(), c->getWidth(), c->getHeight());
                CHECK (ok);
            }
        }
    }
}

TEST (uip_no_literal_colours_in_the_ui_sources)
{
    // the panels and the kit take every colour from ctx.theme (docs/ui-design/00-BRIEF.md); Theme.cpp is where the palette is defined
    const juce::File root (juce::String (SYNTHYBOY_SOURCE_DIR));
    const auto uiDir = root.getChildFile ("shell-juce/ui");
    CHECK (uiDir.isDirectory());
    int offenders = 0;
    for (const auto& f : uiDir.findChildFiles (juce::File::findFiles, true, "*.cpp;*.h"))
    {
        const auto rel = f.getRelativePathFrom (uiDir);
        if (rel.startsWith ("tests") || rel == "Theme.cpp" || rel == "Theme.h")
            continue;
        std::ifstream in (f.getFullPathName().toStdString());
        std::string line;
        int n = 0;
        while (std::getline (in, line))
        {
            ++n;
            const auto code = line.substr (0, line.find ("//"));
            if (code.find ("juce::Colours::") != std::string::npos || code.find ("Colour (0x") != std::string::npos
                || code.find ("Colour(0x") != std::string::npos || code.find ("fromRGB") != std::string::npos
                || code.find ("fromHSV") != std::string::npos || code.find ("fromFloatRGBA") != std::string::npos
                || code.find ("Colour::fromString") != std::string::npos)
            {
                std::printf ("    literal colour: %s:%d: %s\n", rel.toRawUTF8(), n, line.c_str());
                ++offenders;
            }
        }
    }
    CHECK_EQ (offenders, 0);
}

TEST (uip_headless_render_every_page)
{
    SynthyBoyProcessor proc;
    auto editor = makeEditor (proc);
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;
    editor->setSourceBarOpen (false);

    struct Case { int w, h; const char* name; };
    const Case cases[] = { { 390, 844, "phone-390x844" }, { 1024, 768, "tablet-1024x768" } };
    const auto root = shotRoot();

    for (const auto& cs : cases)
    {
        std::vector<juce::Image> images;
        for (const auto page : kPages)
        {
            editor->setSize (cs.w, cs.h);
            editor->panelHost().setPage (page);
            editor->pageTabs().select (page, false);
            const auto image = snapshot (*editor);
            const auto file = root.getChildFile (pageName (page)).getChildFile (juce::String (cs.name) + ".png");
            CHECK (writePng (image, file));
            CHECK (file.getSize() > 2000);
            std::printf ("    [UI SHOT] %s (%lld bytes)\n", file.getFullPathName().toRawUTF8(), static_cast<long long> (file.getSize()));
            images.push_back (image);

            // the whole page, not only the viewport: the editor grows until all of the scrolled content is visible
            const int extra = editor->panelHost().contentHeight();
            if (extra > 0)
            {
                editor->setSize (cs.w, cs.h + extra);
                const auto full = root.getChildFile (pageName (page)).getChildFile (juce::String (cs.name) + "-full.png");
                CHECK (writePng (snapshot (*editor), full));
                std::printf ("    [UI SHOT] %s\n", full.getFullPathName().toRawUTF8());
                editor->setSize (cs.w, cs.h);
            }
        }

        // overview: all pages side by side
        const int cols = 4;
        const int rows = 2;
        const int gap = 8;
        juce::Image overview (juce::Image::ARGB, cols * (cs.w + gap) + gap, rows * (cs.h + gap) + gap, true);
        juce::Graphics g (overview);
        g.fillAll (juce::Colour (0xff202020));
        for (size_t i = 0; i < images.size(); ++i)
        {
            const int cx = static_cast<int> (i) % cols;
            const int cy = static_cast<int> (i) / cols;
            g.drawImageAt (images[i], gap + cx * (cs.w + gap), gap + cy * (cs.h + gap));
        }
        const auto overviewFile = root.getChildFile (juce::String ("overview-") + cs.name + ".png");
        CHECK (writePng (overview, overviewFile));
        std::printf ("    [UI SHOT] %s\n", overviewFile.getFullPathName().toRawUTF8());
    }
}

TEST (uip_zz_shutdown)
{
    delete juceInitPanels;
    juceInitPanels = nullptr;
    CHECK (true);
}
