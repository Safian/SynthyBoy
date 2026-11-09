// SynthyBoy - UI panel: Voice Control (docs/ui-design/13-new-keys.md 4.30, B26: GROUPA2 WP-07; Serum 2 manual p.316 - 319).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The step grid of the per voice sequence: six rows (Pan, Detune, Cutoff, Envelope time, Mod 1, Mod 2) of eight steps. One row is shown at a time (the
// row chips), so the eight cells are 44 point wide touch targets (two rows of four on a phone); the number buttons above them are the sequence length
// ("click one of the numbers 1 to 8", p.317): the cells after the length are dimmed. A cell is a vertical bar: drag it, double tap resets it, long press opens
// the value sheet. The 14 other keys are ordinary controls: sequence length, the two scalings (envelope time, LFO time with its Percent / Rate mode) and the
// four primary knobs, the random amounts and the oscillator mask behind More. "Init" resets the configuration, "Presets" is the user library
// (category voice_control) and loads a .SerumStyle file.
#pragma once

#include <array>

#include "../ContentIO.h"
#include "../LibraryMenu.h"
#include "../Panel.h"
#include "../ParamHandle.h"

#include "synthyboy/library_items.h"
#include "synthyboy/user_library.h"

namespace synthyboy::ui
{
constexpr int kVcSteps = 8;
constexpr int kVcRows = 6;
constexpr const char* kVcRowKey[kVcRows] = { "pan", "detune", "cutoff", "env", "mod1", "mod2" };
constexpr const char* kVcRowName[kVcRows] = { "Pan", "Detune", "Cutoff", "Env time", "Mod 1", "Mod 2" };

inline std::string vcStepKey (int step, int row) { return "voicectl.s" + std::to_string (step + 1) + "." + kVcRowKey[row]; }

/** One step value: a bipolar vertical bar. */
class VoiceStepCell final : public ParamDragControl
{
public:
    VoiceStepCell (UiContext& context, const std::string& key, int stepIndex)
        : ParamDragControl (context, key), step (stepIndex)
    {
        initRing();
    }

    juce::Point<int> preferredSize() const override { return { ctx.theme.touch(), ctx.theme.touch() * 2 }; }

    /** The step is beyond the sequence length: it is shown dimmed (it still can be edited). */
    void setBeyondLength (bool beyond)
    {
        if (beyond != dimmed)
        {
            dimmed = beyond;
            repaint();
        }
    }
    int stepIndex() const noexcept { return step; }

    void paint (juce::Graphics& g) override
    {
        if (! handle.valid())
        {
            paintUnknownKey (g);
            return;
        }
        const auto& t = ctx.theme;
        const Palette& c = t.colours;
        const ParamInfo& info = handle.info();
        auto area = getLocalBounds().toFloat().reduced (t.px (2.0f));
        g.setColour (c.track.withAlpha (dimmed ? 0.5f : 1.0f));
        g.fillRoundedRectangle (area, t.px (6.0f));

        const float labelH = t.px (16.0f);
        const auto labelArea = area.removeFromBottom (labelH).toNearestInt();
        const auto bar = area.reduced (t.px (6.0f), t.px (4.0f));
        const float norm = handle.normalised();
        const float zero = toNormalised (info, 0.0f);
        const float yZero = bar.getBottom() - zero * bar.getHeight();
        const float yValue = bar.getBottom() - norm * bar.getHeight();
        g.setColour (c.outline);
        g.drawHorizontalLine (juce::roundToInt (yZero), bar.getX(), bar.getRight());
        g.setColour (dimmed ? c.accentDim : c.accent);
        g.fillRoundedRectangle (bar.getX() + bar.getWidth() * 0.2f, juce::jmin (yZero, yValue), bar.getWidth() * 0.6f,
                                juce::jmax (t.px (2.0f), std::abs (yZero - yValue)), t.px (3.0f));

        g.setFont (t.valueFont());
        g.setColour (dimmed ? c.textDim : c.text);
        g.drawFittedText (juce::String (displayText (info, handle.plain())), labelArea, juce::Justification::centred, 1);
    }

protected:
    juce::Rectangle<int> ringBounds() const override { return getLocalBounds(); }
    ModRing::Shape ringShape() const noexcept override { return ModRing::Shape::bar; }
    float pixelsForFullRange() const override { return juce::jmax (60.0f, static_cast<float> (getHeight()) * 1.2f); }

private:
    int step = 0;
    bool dimmed = false;
};

class GlbVoiceControlPanel final : public Panel
{
public:
    explicit GlbVoiceControlPanel (UiContext& context)
        : Panel (context), lengthHandle (context, "voicectl.length")
    {
    }

    juce::String title() const override { return "Voice Control"; }

    std::vector<std::string> keys() const override
    {
        std::vector<std::string> k { "voicectl.length", "voicectl.scale.env", "voicectl.scale.lfo", "voicectl.scale.lfo_mode",
                                     "voicectl.rand.pan", "voicectl.rand.detune", "voicectl.rand.cutoff", "voicectl.rand.env", "voicectl.rand.detune10x",
                                     "voicectl.osc.a", "voicectl.osc.b", "voicectl.osc.c", "voicectl.osc.noise", "voicectl.osc.sub" };
        for (int s = 0; s < kVcSteps; ++s)
            for (int r = 0; r < kVcRows; ++r)
                k.push_back (vcStepKey (s, r));
        return k;
    }

    void build() override
    {
        addControl ("voicectl.length");
        addControl ("voicectl.scale.env");
        addControl ("voicectl.scale.lfo");
        addControl ("voicectl.scale.lfo_mode");
        for (const char* key : { "voicectl.rand.pan", "voicectl.rand.detune", "voicectl.rand.cutoff", "voicectl.rand.env", "voicectl.rand.detune10x",
                                 "voicectl.osc.a", "voicectl.osc.b", "voicectl.osc.c", "voicectl.osc.noise", "voicectl.osc.sub" })
            addMoreControl (key);
        for (int s = 0; s < kVcSteps; ++s)
            for (int r = 0; r < kVcRows; ++r)
            {
                auto cell = std::make_unique<VoiceStepCell> (ctx, vcStepKey (s, r), s);
                cells[static_cast<size_t> (s * kVcRows + r)] = cell.get();
                excludeFromFlow (addMoreControl (std::move (cell)));
            }

        for (int r = 0; r < kVcRows; ++r)
        {
            rowButtons[static_cast<size_t> (r)].setButtonText (kVcRowName[r]);
            rowButtons[static_cast<size_t> (r)].setClickingTogglesState (false);
            rowButtons[static_cast<size_t> (r)].onClick = [this, r] { selectRow (r); };
            addAndMakeVisible (rowButtons[static_cast<size_t> (r)]);
        }
        for (int s = 0; s < kVcSteps; ++s)
        {
            numberButtons[static_cast<size_t> (s)].setButtonText (juce::String (s + 1));
            numberButtons[static_cast<size_t> (s)].onClick = [this, s] { setLength (s + 1); };
            addAndMakeVisible (numberButtons[static_cast<size_t> (s)]);
        }
        initButton.setButtonText ("Init");
        initButton.onClick = [this] { initialise(); };
        addAndMakeVisible (initButton);
        presetsButton.setButtonText ("Presets");
        addAndMakeVisible (presetsButton);
        initLibrary();

        lengthHandle.onChange = [this] { syncLength(); };
        syncLength();
        selectRow (0);
    }

    // ---- the operations the buttons call (the tests call them too) ----------------------------------------------------------------------
    int selectedRow() const noexcept { return row; }
    void selectRow (int newRow)
    {
        row = juce::jlimit (0, kVcRows - 1, newRow);
        for (int r = 0; r < kVcRows; ++r)
            rowButtons[static_cast<size_t> (r)].setToggleState (r == row, juce::dontSendNotification);
        resized();
        repaint();
    }
    int sequenceLength() const { return lengthHandle.valid() ? juce::roundToInt (lengthHandle.plain()) : kVcSteps; }
    void setLength (int length)
    {
        if (lengthHandle.valid())
            lengthHandle.setPlainComplete (static_cast<float> (juce::jlimit (1, kVcSteps, length)));
    }
    /** Resets the whole configuration (Serum: "Init" in the voice control menu). */
    void initialise()
    {
        ParamValues values = readLiveValues (ctx);
        for (size_t i = 0; i < paramCount(); ++i)
            if (isVoiceControlKey (paramTable()[i].key))
                values.plain[i] = paramTable()[i].def;
        applyValues (ctx, values);
        syncLength();
    }
    VoiceStepCell* cell (int step, int forRow) noexcept { return cells[static_cast<size_t> (step * kVcRows + forRow)]; }
    juce::Button& rowButton (int r) noexcept { return rowButtons[static_cast<size_t> (r)]; }
    juce::Button& numberButton (int s) noexcept { return numberButtons[static_cast<size_t> (s)]; }
    juce::Button& initButtonRef() noexcept { return initButton; }
    juce::Button& presetsButtonRef() noexcept { return presetsButton; }
    LibraryMenu& presets() noexcept { return *library; }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + gridHeight (area.getWidth()) + t.gap()
             + const_cast<GlbVoiceControlPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const int gap = t.gap();
        auto area = contentArea();
        const int originX = area.getX();
        const int top0 = area.getY();
        int y = top0;
        const int width = area.getWidth();
        const int touch = t.touch();

        // tool row
        initButton.setBounds (originX, y, juce::jmax (touch * 2, t.ipx (88.0f)), touch);
        presetsButton.setBounds (originX + initButton.getWidth() + gap, y, juce::jmax (touch * 2, t.ipx (96.0f)), touch);
        y += touch + gap;

        // row chips: as many per line as fit
        const int chipW = juce::jmax (touch, t.ipx (72.0f));
        const int perLine = juce::jmax (1, (width + gap) / (chipW + gap));
        const int cw = juce::jmax (chipW, (width - (perLine - 1) * gap) / perLine);
        for (int r = 0; r < kVcRows; ++r)
        {
            const int line = r / perLine, col = r % perLine;
            rowButtons[static_cast<size_t> (r)].setBounds (originX + col * (cw + gap), y + line * (touch + gap), cw, touch);
        }
        y += ((kVcRows + perLine - 1) / perLine) * (touch + gap);

        // numbers and cells
        const int perGridLine = columnsFor (width);
        const int colW = (width - (perGridLine - 1) * gap) / perGridLine;
        const int cellH = touch * 2;
        for (int s = 0; s < kVcSteps; ++s)
        {
            const int line = s / perGridLine, col = s % perGridLine;
            const int x = originX + col * (colW + gap);
            const int top = y + line * (touch + gap + cellH + gap);
            numberButtons[static_cast<size_t> (s)].setBounds (x, top, colW, touch);
            for (int r = 0; r < kVcRows; ++r)
            {
                auto* c = cell (s, r);
                c->setVisible (r == row);
                c->setBounds (x, top + touch + gap, colW, cellH);
            }
        }
        y = top0 + gridHeight (width) + gap;
        layoutControls (juce::Rectangle<int> (originX, y, width, juce::jmax (0, area.getBottom() - y)), false);
    }

private:
    /** 8 columns when a column is a 44 point target, else 4. */
    int columnsFor (int width) const
    {
        const auto& t = ctx.theme;
        return (width - 7 * t.gap()) / kVcSteps >= t.touch() ? kVcSteps : kVcSteps / 2;
    }
    int gridHeight (int width) const
    {
        const auto& t = ctx.theme;
        const int gap = t.gap(), touch = t.touch();
        const int lines = kVcSteps / columnsFor (width);
        const int chipW = juce::jmax (touch, t.ipx (72.0f));
        const int perLine = juce::jmax (1, (width + gap) / (chipW + gap));
        const int chipLines = (kVcRows + perLine - 1) / perLine;
        return touch + gap                                              // Init, Presets
             + chipLines * (touch + gap)                                // the row chips
             + lines * (touch + gap + touch * 2 + gap) - gap;          // numbers + cells
    }
    void syncLength()
    {
        const int length = sequenceLength();
        for (int s = 0; s < kVcSteps; ++s)
        {
            numberButtons[static_cast<size_t> (s)].setToggleState (s < length, juce::dontSendNotification);
            for (int r = 0; r < kVcRows; ++r)
                if (auto* c = cell (s, r))
                    c->setBeyondLength (s >= length);
        }
    }
    void initLibrary()
    {
        LibraryMenuSpec spec;
        spec.category = kLibVoiceControl;
        spec.noun = "voice control";
        spec.suggestedName = "My voice control";
        spec.makeItem = [this] (const juce::String& name) { return serializeVoiceControlPreset (name.toStdString(), readLiveValues (ctx)); };
        spec.applyItem = [this] (const juce::String&, const std::string& json)
        {
            ParamValues values = readLiveValues (ctx);
            if (applyVoiceControlPreset (json, values) == 0)
                return false;
            applyValues (ctx, values);
            syncLength();
            return true;
        };
        spec.fileWildcards = "*.SerumStyle";
        spec.applyFile = [this] (const juce::File& file)
        {
            const SerumFlowResult result = importSerumFile (ctx, file, 0);
            syncLength();
            return result.flow.ok;
        };
        library = std::make_shared<LibraryMenu> (ctx, std::move (spec));
        presetsButton.onClick = [this] { library->show (presetsButton); };
    }

    ParamHandle lengthHandle;
    int row = 0;
    std::array<VoiceStepCell*, kVcSteps * kVcRows> cells {};
    std::array<juce::TextButton, kVcRows> rowButtons;
    std::array<juce::TextButton, kVcSteps> numberButtons;
    juce::TextButton initButton, presetsButton;
    std::shared_ptr<LibraryMenu> library;
};

} // namespace synthyboy::ui
