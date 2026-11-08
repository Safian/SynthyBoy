// SynthyBoy - UI: Full-screen mobile wavetable editor sheet (docs/ui-design/03-wavetable.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Theme.h"
#include "../UiContext.h"
#include "synthyboy/wavetable_edit.h"
#include "synthyboy/formula.h"
#include "synthyboy/wavetable_render.h"

namespace synthyboy::ui
{

/** Full-screen wavetable editor modal sheet covering phone and tablet/landscape layouts. */
class WavetableEditorSheet final : public juce::Component
{
public:
    enum class Mode
    {
        wave,
        spectrum,
        process,
        formula,
        layers
    };

    WavetableEditorSheet (UiContext& context, wtedit::Wavetable initialTable = wtedit::Wavetable ("Default"));
    ~WavetableEditorSheet() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Access to the underlying wavetable model. */
    const wtedit::Wavetable& wavetable() const noexcept { return wt; }
    wtedit::Wavetable& wavetable() noexcept { return wt; }

    /** Current frame index (0-based). */
    size_t currentFrame() const noexcept { return currentFrameIndex; }
    void setCurrentFrame (size_t index);

    /** Active editor mode. */
    Mode mode() const noexcept { return currentMode; }
    void setMode (Mode newMode);

    /** Undo / Redo access. */
    bool canUndo() const noexcept { return undoPending || undoStack.canUndo(); }
    bool canRedo() const noexcept { return ! undoPending && undoStack.canRedo(); }
    void performUndo();
    void performRedo();

    /** Closes sheet and notifies listener. */
    std::function<void(const wtedit::Wavetable&)> onClosed;

    /** Import helpers for direct programmatic invocation (and tests). */
    bool importAudioData (const float* samples, size_t numSamples, double sampleRate, const wtedit::ImportOptions& opts);
    bool importImageData (const uint8_t* grayscale, int width, int height);

    /** Applies a formula to current frame or all frames. */
    bool applyFormula (const std::string& expression, std::string* errorText = nullptr);

    /** Executes a named process command (e.g. "Normalize", "Remove DC", etc.). */
    bool executeProcessCommand (const std::string& commandName, bool allFrames, int gridSize = 16);

    /** Applies active drawing tool to current frame at given step index. */
    void applyToolAt (wtedit::DrawingTool tool, int gridSize, int stepIndex, float param);

    /** Renders the Vital component group layers to the wavetable. */
    void renderLayersToTable();

    /** Frame operations. */
    void copyCurrentFrame();
    void pasteFrame();
    void removeCurrentFrame();
    void insertNewFrame();
    void sortFrames (wtedit::SortMode sortMode);
    void morphFrames (wtedit::MorphMode morphMode);

private:
    class WaveCanvas;
    class SpectrumCanvas;
    class ProcessView;
    class FormulaView;
    class LayersView;
    class ImportSheet;
    class ThumbnailsStrip;

    /** Marks the start of an edit. The undo stack holds states (baseline first), so the state the edit produces is recorded lazily,
        by the next pushUndo / undo / redo: every edit becomes exactly one undo step. */
    void pushUndo();
    void commitPendingUndo();
    void layoutToolButtons (juce::Rectangle<int>& area);
    void updateHeaderInfo();
    void showOptionsMenu();
    void showRemoveMenu();
    void showSortMenu();
    void showMorphMenu();
    void showImportDialog();
    void exportWavPrompt (wtedit::ExportFormat format);

    UiContext& ctx;
    wtedit::Wavetable wt;
    wtedit::UndoStack undoStack { 50 };
    bool undoPending = false;   // an edit started after the last recorded state
    bool strokeActive = false;  // a drawing gesture (mouse down .. up) is in progress: it is one undo step
    wtedit::vital::Group vitalGroup;
    size_t currentFrameIndex = 0;
    int rangeStart = -1;
    int rangeEnd = -1;
    bool rangeSelectActive = false;
    bool isModified = false;

    Mode currentMode = Mode::wave;
    int currentGridSize = 16;
    wtedit::DrawingTool currentTool = wtedit::DrawingTool::FlatLine;
    wtedit::Frame clipboardFrame;
    bool hasClipboard = false;

    // Header controls
    juce::TextButton closeBtn { "X" };
    juce::Label titleLabel;
    juce::TextButton undoBtn { "Undo" };
    juce::TextButton redoBtn { "Redo" };
    juce::TextButton menuBtn { "..." };

    // Mode tab buttons
    juce::TextButton waveTabBtn { "WAVE" };
    juce::TextButton spectrumTabBtn { "SPECTRUM" };
    juce::TextButton processTabBtn { "PROCESS" };
    juce::TextButton formulaTabBtn { "FORMULA" };
    juce::TextButton layersTabBtn { "LAYERS" };

    // Tool strip for WAVE mode
    juce::TextButton toolFlatBtn { "-" };
    juce::TextButton toolSineBtn { "~" };
    juce::TextButton toolSlopeUpBtn { "/" };
    juce::TextButton toolSlopeDownBtn { "\\" };
    juce::TextButton toolCurveUpBtn { "(" };
    juce::TextButton toolCurveDownBtn { ")" };
    juce::TextButton toolHalfSinePeakBtn { "n" };
    juce::TextButton toolHalfSineValleyBtn { "u" };
    juce::TextButton toolInterpLinBtn { "/:" };
    juce::TextButton toolInterpCurvedBtn { "s" };
    juce::TextButton toolNudgeBtn { "^" };
    juce::TextButton toolNoiseBtn { "#" };
    juce::TextButton toolMirrorBtn { "M" };
    juce::TextButton gridBtn { "Grid 16" };

    // Bottom frame navigation & menus
    juce::Label framePosLabel;
    juce::TextButton prevFrameBtn { "<" };
    juce::TextButton nextFrameBtn { ">" };
    juce::TextButton copyBtn { "Copy" };
    juce::TextButton pasteBtn { "Paste" };
    juce::TextButton removeMenuBtn { "Remove" };
    juce::TextButton sortMenuBtn { "Sort" };
    juce::TextButton morphMenuBtn { "Morph" };
    juce::TextButton rangeToggleBtn { "Range" };

    // Mode components
    std::unique_ptr<WaveCanvas> waveCanvas;
    std::unique_ptr<SpectrumCanvas> spectrumCanvas;
    std::unique_ptr<ProcessView> processView;
    std::unique_ptr<FormulaView> formulaView;
    std::unique_ptr<LayersView> layersView;
    std::unique_ptr<ThumbnailsStrip> thumbnailsStrip;
    std::unique_ptr<ImportSheet> importSheet;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableEditorSheet)
};

} // namespace synthyboy::ui
