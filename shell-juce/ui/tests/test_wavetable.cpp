// SynthyBoy - UI tests: Wavetable view and mobile wavetable editor.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "PluginProcessor.h"
#include "EditorAdapter.h"
#include "ui/Editor.h"
#include "ui/panels/WavetablePanel.h"
#include "ui/panels/WavetableEditorSheet.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::ui;

namespace
{
class CapsHost final : public UiHost
{
public:
    CapsHost (SynthyBoyProcessor& p, EngineCapsMask c) : inner (p), caps (c) {}
    ParameterStore& parameterStore() override { return inner.parameterStore(); }
    bool overlaysInsideEditor() const noexcept override { return inner.overlaysInsideEditor(); }
    EngineCapsMask engineCaps() const noexcept override { return caps; }
    juce::MidiKeyboardState& keyboardState() override { return inner.keyboardState(); }
    AbCompare& abCompare() override { return inner.abCompare(); }
    void stepPreset (int d) override { inner.stepPreset (d); }
    juce::String presetName() const override { return inner.presetName(); }

private:
    SynthyBoyProcessor& inner;
    EngineCapsMask caps;
};

juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged, bool popup = false)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (source, pos, popup ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::popupMenuClickModifier)
                                                : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1, dragged);
}

void collectInteractiveControls (juce::Component& c, std::vector<juce::Component*>& out)
{
    if (! c.isVisible())
        return;
    if (dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<ParamControl*> (&c) != nullptr || dynamic_cast<juce::ComboBox*> (&c) != nullptr)
        out.push_back (&c);
    for (auto* child : c.getChildren())
        collectInteractiveControls (*child, out);
}

bool saveComponentPng (juce::Component& c, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    if (! image.isValid())
        return false;
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}
} // namespace

//==============================================================================
// PURE LOGIC & MODEL TESTS
//==============================================================================
TEST (ui_wavetable_model_and_operations)
{
    // 1. Wavetable initialization and frame operations
    wtedit::Wavetable wt ("Test Table");
    CHECK_EQ (wt.numFrames(), 1);
    CHECK_EQ (wt.name(), "Test Table");

    // Insert frames up to 8
    for (size_t i = 1; i < 8; ++i)
    {
        wtedit::Frame f;
        f[0] = static_cast<float> (i);
        CHECK (wt.insertFrame (i, f));
    }
    CHECK_EQ (wt.numFrames(), 8);

    // Duplicate frame
    CHECK (wt.duplicateFrame (3));
    CHECK_EQ (wt.numFrames(), 9);

    // Move frame
    CHECK (wt.moveFrame (1, 5));
    CHECK_EQ (wt.numFrames(), 9);

    // Remove frame
    CHECK (wt.removeFrame (2));
    CHECK_EQ (wt.numFrames(), 8);

    // 2. Normalization and DC removal
    auto& f0 = wt.getFrame (0);
    f0[100] = 2.5f;
    f0[200] = -1.5f;
    wtedit::ops::normalise (f0);
    float maxVal = 0.0f;
    for (size_t s = 0; s < wtedit::kWaveformSize; ++s)
        maxVal = juce::jmax (maxVal, std::abs (f0[s]));
    CHECK (std::abs (maxVal - 1.0f) < 1.0e-4f);

    wtedit::ops::removeDc (f0);
    double sum = 0.0;
    for (size_t s = 0; s < wtedit::kWaveformSize; ++s)
        sum += f0[s];
    CHECK (std::abs (sum / static_cast<double> (wtedit::kWaveformSize)) < 1.0e-5);

    // 3. Flips and edges
    wtedit::ops::flipVertical (f0);
    wtedit::ops::flipHorizontal (f0);
    wtedit::ops::fadeEdges (f0, 16);
    wtedit::ops::crossfadeEdges (f0, 16);
    wtedit::ops::crossfadeEdges16Samples (f0);
    wtedit::ops::shiftToZeroCrossing (f0);

    // 4. Drawing tools
    wtedit::applyDrawingTool (f0, wtedit::DrawingTool::FlatLine, 16, 2, 0.5f);
    wtedit::applyDrawingTool (f0, wtedit::DrawingTool::Sine, 16, 3, 0.0f);
    wtedit::applyDrawingTool (f0, wtedit::DrawingTool::SlopeUp, 16, 4, 0.0f);
    wtedit::applyDrawingTool (f0, wtedit::DrawingTool::HalfSinePeak, 16, 5, 0.0f);
    wtedit::applyDrawingTool (f0, wtedit::DrawingTool::Nudge, 16, 6, 0.1f);
    wtedit::applyDrawingTool (f0, wtedit::DrawingTool::Mirror, 16, 0, 0.0f);

    // 5. FFT operations
    wtedit::ops::fftGenerateSaw (f0);
    wtedit::ops::fftRandomizeLowBins (f0, 16);
    wtedit::ops::fftShiftOctaveUp (f0);
    wtedit::ops::fftShiftOctaveDown (f0);
    wtedit::ops::fftProgressiveFade (f0, 1.5f);
    wtedit::ops::fftScaleFreqValuesByBinIndex (f0);

    // 6. UndoStack
    wtedit::UndoStack undo (10);
    CHECK (! undo.canUndo());
    CHECK (! undo.canRedo());

    // the stack records states: the baseline first, then the state after every edit
    undo.push (wt);
    CHECK (! undo.canUndo());
    wt.setName ("Modified 1");
    undo.push (wt);
    CHECK (undo.canUndo());

    wt.setName ("Modified 2");
    undo.push (wt);

    CHECK (undo.undo (wt));
    CHECK_EQ (wt.name(), "Modified 1");
    CHECK (undo.redo (wt));
    CHECK_EQ (wt.name(), "Modified 2");

    // 7. Formula evaluation
    formula::Formula form;
    CHECK (formula::Formula::compile ("sin(2*pi*x)", form).ok);
    CHECK (! form.isMultiFrame());
    const float val0 = form.evaluate (0.0);
    CHECK (std::abs (val0) < 1.0e-4f);

    formula::Formula multiForm;
    CHECK (formula::Formula::compile ("(1 - 2*w) * (1 - y) + (x < 0 ? -1 : 1) * y", multiForm).ok);
    CHECK (multiForm.isMultiFrame());

    std::vector<float> buffer (wt.numFrames() * wtedit::kWaveformSize);
    CHECK (formula::renderFrames (multiForm, static_cast<int> (wt.numFrames()), buffer.data()).ok);

    // 8. Vital component group
    wtedit::vital::Group group;
    group.initFromWavetable (wt);
    CHECK (group.numComponents() >= 1);
    wtedit::Wavetable renderedWt ("Rendered");
    group.renderWavetable (renderedWt, 4);
    CHECK_EQ (renderedWt.numFrames(), 4);

    // 9. WAV Export
    wtedit::ExportOptions exportOpts;
    exportOpts.format = wtedit::ExportFormat::Wav32BitFloat;
    std::vector<uint8_t> wavBytes;
    CHECK (wtedit::exportWav (wt, exportOpts, wavBytes));
    CHECK (wavBytes.size() > 44);

    // 10. Audio & Image Import
    wtedit::Wavetable audioWt ("Audio");
    std::vector<float> testAudio (44100, 0.5f);
    wtedit::ImportOptions importOpts;
    importOpts.mode = wtedit::ImportMode::PitchAverage;
    CHECK (wtedit::importAudioSamples (testAudio.data(), testAudio.size(), 44100.0, importOpts, audioWt));
    CHECK (audioWt.numFrames() >= 1);

    wtedit::Wavetable imageWt ("Image");
    std::vector<uint8_t> gray (32 * 32, 100);
    CHECK (wtedit::importImageGrayscale (gray.data(), 32, 32, imageWt));
    CHECK_EQ (imageWt.numFrames(), 32);
}

//==============================================================================
// PANEL & VISUAL INTERACTIONS TEST
//==============================================================================
TEST (ui_wavetable_panel_controls_and_visual)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    WavetablePanel panel (ctx);
    panel.build();

    // Registry contract checks on WavetablePanel
    CHECK (panel.title().isNotEmpty());
    const auto keys = panel.keys();
    CHECK_EQ (keys.size(), panel.controls().size());
    CHECK_EQ (keys.size(), static_cast<size_t> (12));
    // Principle 1: exactly 4 primary controls next to visual
    CHECK_EQ (static_cast<int> (panel.controls().size()) - panel.numMoreControls(), 4);

    for (size_t i = 0; i < keys.size(); ++i)
    {
        CHECK (resolveKey (keys[i]).valid());
        CHECK_EQ (keys[i], panel.controls()[i]->key());
    }

    panel.setBounds (0, 0, 390, panel.preferredHeight (390));

    auto* visual = panel.visual();
    CHECK (visual != nullptr);
    if (visual == nullptr)
        return;

    // Direct manipulation on visual: Horizontal drag scrubs osc1.frame
    const float initFrame = *proc.parameters().getRawParameterValue ("osc1.frame");
    const auto center = visual->getLocalBounds().getCentre().toFloat();

    visual->mouseDown (mouseEvent (*visual, center, center, false));
    visual->mouseDrag (mouseEvent (*visual, center.translated (50.0f, 0.0f), center, true));
    visual->mouseUp (mouseEvent (*visual, center.translated (50.0f, 0.0f), center, false));

    const float scrubbedFrame = *proc.parameters().getRawParameterValue ("osc1.frame");
    CHECK (std::abs (scrubbedFrame - initFrame) > 1.0e-6f);

    // Tap cycles view mode
    const auto initialMode = visual->viewMode();
    visual->cycleViewMode();
    CHECK (visual->viewMode() != initialMode);
    visual->cycleViewMode();
    visual->cycleViewMode();
    CHECK (visual->viewMode() == initialMode);

    // Zoom controls
    visual->zoomIn();
    visual->zoomOut();

    // More expand/collapse
    CHECK (! panel.isMoreExpanded());
    panel.setMoreExpanded (true);
    CHECK (panel.isMoreExpanded());
    panel.setMoreExpanded (false);
}

//==============================================================================
// EDITOR SHEET MODES & OPERATIONS TEST
//==============================================================================
TEST (ui_wavetable_editor_sheet_operations)
{
    SynthyBoyProcessor proc;
    CapsHost host (proc, kCapAllKnown);
    Theme theme = Theme::dark();
    ModAssignController assign (host);
    UiContext ctx { host, theme, assign };

    wtedit::Wavetable wt ("Editor Test");
    for (size_t i = 0; i < 4; ++i)
        wt.insertFrame (i, wtedit::Frame());

    WavetableEditorSheet sheet (ctx, std::move (wt));
    sheet.setBounds (0, 0, 390, 844);

    // 1. Mode switching
    sheet.setMode (WavetableEditorSheet::Mode::wave);
    CHECK (sheet.mode() == WavetableEditorSheet::Mode::wave);
    sheet.setMode (WavetableEditorSheet::Mode::spectrum);
    CHECK (sheet.mode() == WavetableEditorSheet::Mode::spectrum);
    sheet.setMode (WavetableEditorSheet::Mode::process);
    CHECK (sheet.mode() == WavetableEditorSheet::Mode::process);
    sheet.setMode (WavetableEditorSheet::Mode::formula);
    CHECK (sheet.mode() == WavetableEditorSheet::Mode::formula);
    sheet.setMode (WavetableEditorSheet::Mode::layers);
    CHECK (sheet.mode() == WavetableEditorSheet::Mode::layers);

    // 2. Tool application
    sheet.applyToolAt (wtedit::DrawingTool::SlopeUp, 16, 2, 0.8f);
    CHECK (sheet.canUndo());

    // 3. Process commands
    CHECK (sheet.executeProcessCommand ("Normalize", false, 16));
    CHECK (sheet.executeProcessCommand ("Remove DC Offset", true, 16));
    CHECK (sheet.executeProcessCommand ("Fade Edges", false, 16));
    CHECK (sheet.executeProcessCommand ("Crossfade Edges", true, 16));

    // 4. Formula application
    std::string err;
    CHECK (sheet.applyFormula ("sin(2*pi*x)", &err));
    CHECK (sheet.applyFormula ("(1-2*w)*(1-y) + (x<0?-1:1)*y", &err));

    // 5. Frame operations
    const size_t prevCount = sheet.wavetable().numFrames();
    sheet.copyCurrentFrame();
    sheet.pasteFrame();
    CHECK_EQ (sheet.wavetable().numFrames(), prevCount);

    sheet.insertNewFrame();
    CHECK_EQ (sheet.wavetable().numFrames(), prevCount + 1);

    sheet.sortFrames (wtedit::SortMode::Reverse);
    sheet.morphFrames (wtedit::MorphMode::Crossfade);

    // 6. Undo / Redo end-to-end
    while (sheet.canUndo())
        sheet.performUndo();
    CHECK (! sheet.canUndo());
}

//==============================================================================
// HEADLESS RENDER TO PNG SCREENSHOTS TEST
//==============================================================================
TEST (ui_wavetable_headless_render_screenshots)
{
    SynthyBoyProcessor proc;
    int steps = 0;
    proc.setPresetStepHandler ([&steps] (int d) { steps += d; });
    proc.setPresetName ("Init Wavetable Preset");

    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = innerEditor (base.get());
    CHECK (editor != nullptr);
    if (editor == nullptr)
        return;

    // Switch editor to Page::osc where WavetablePanel is registered
    editor->panelHost().setPage (Page::osc);

    const juce::File outDir ("/tmp/ui-shots/wavetable");
    outDir.createDirectory();

    // 1. Phone portrait rendering: 390x844
    editor->setSize (390, 844);
    const auto phoneFile = outDir.getChildFile ("phone-390x844.png");
    CHECK (saveComponentPng (*editor, phoneFile));
    CHECK (phoneFile.getSize() > 2000);
    std::printf ("    [UI SHOT] %s (%lld bytes)\n", phoneFile.getFullPathName().toRawUTF8(), static_cast<long long> (phoneFile.getSize()));

    // 2. Tablet landscape rendering: 1024x768
    editor->setSize (1024, 768);
    const auto tabletFile = outDir.getChildFile ("tablet-1024x768.png");
    CHECK (saveComponentPng (*editor, tabletFile));
    CHECK (tabletFile.getSize() > 2000);
    std::printf ("    [UI SHOT] %s (%lld bytes)\n", tabletFile.getFullPathName().toRawUTF8(), static_cast<long long> (tabletFile.getSize()));

    // 3. Render WavetableEditorSheet directly at phone & tablet sizes
    WavetableEditorSheet editorSheet (editor->context());

    // Phone editor sheet shot
    editorSheet.setSize (390, 844);
    const auto sheetPhoneFile = outDir.getChildFile ("editor-sheet-phone-390x844.png");
    CHECK (saveComponentPng (editorSheet, sheetPhoneFile));
    CHECK (sheetPhoneFile.getSize() > 2000);
    std::printf ("    [UI SHOT] %s (%lld bytes)\n", sheetPhoneFile.getFullPathName().toRawUTF8(), static_cast<long long> (sheetPhoneFile.getSize()));

    // Tablet editor sheet shot (shows WAVE and SPECTRUM side-by-side)
    editorSheet.setSize (1024, 768);
    editorSheet.setMode (WavetableEditorSheet::Mode::wave);
    const auto sheetTabletFile = outDir.getChildFile ("editor-sheet-tablet-1024x768.png");
    CHECK (saveComponentPng (editorSheet, sheetTabletFile));
    CHECK (sheetTabletFile.getSize() > 2000);
    std::printf ("    [UI SHOT] %s (%lld bytes)\n", sheetTabletFile.getFullPathName().toRawUTF8(), static_cast<long long> (sheetTabletFile.getSize()));

    // 4. Verify touch targets >= 44 points for all visible interactive controls
    editorSheet.setVisible (true); // a free-standing component is hidden until shown; the walk skips hidden ones
    std::vector<juce::Component*> targets;
    collectInteractiveControls (editorSheet, targets);
    const int minTouch = editor->theme().touch();
    int verifiedTargets = 0;

    for (auto* c : targets)
    {
        if (c->getWidth() == 0 || c->getHeight() == 0)
            continue;
        const bool touchOk = (c->getWidth() >= minTouch && c->getHeight() >= minTouch);
        if (! touchOk)
            {
                const auto* button = dynamic_cast<juce::Button*> (c);
                const juce::Component* parent = c->getParentComponent();
                std::printf ("    Control too small: %s \"%s\" in %s %dx%d (min %d)\n", typeid (*c).name(),
                             button != nullptr ? button->getButtonText().toRawUTF8() : c->getName().toRawUTF8(),
                             parent != nullptr ? typeid (*parent).name() : "-", c->getWidth(), c->getHeight(), minTouch);
            }
        CHECK (touchOk);
        ++verifiedTargets;
    }
    CHECK (verifiedTargets > 15);
}
