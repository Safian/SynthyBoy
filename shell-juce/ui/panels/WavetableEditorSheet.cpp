// SynthyBoy - UI: Full-screen mobile wavetable editor sheet implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WavetableEditorSheet.h"

#include "../ContentIO.h"
#include "../LibraryMenu.h"
#include "synthyboy/library_items.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace synthyboy::ui
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
} // namespace

//==============================================================================
// WAVE CANVAS COMPONENT
//==============================================================================
class WavetableEditorSheet::WaveCanvas final : public juce::Component
{
public:
    WaveCanvas (WavetableEditorSheet& ownerSheet)
        : owner (ownerSheet)
    {
        setOpaque (false);
        zoomInBtn.onClick = [this] { owner.zoomWave (0.5f, 2.0f); };
        zoomOutBtn.onClick = [this] { owner.zoomWave (0.5f, 0.5f); };
        fitBtn.onClick = [this] { owner.fitWave(); };
        addAndMakeVisible (zoomInBtn);
        addAndMakeVisible (zoomOutBtn);
        addAndMakeVisible (fitBtn);
        press.onLongPress = [this]
        {
            // held still: this stroke nudges (design 03: long-press then drag with Nudge); the first touch point is taken back
            nudgeHeld = true;
            owner.restoreStrokeBackup();
        };
    }

    juce::Button* zoomButton (int which) noexcept
    {
        return which == 0 ? static_cast<juce::Button*> (&zoomInBtn) : which == 1 ? static_cast<juce::Button*> (&zoomOutBtn) : &fitBtn;
    }

    LongPressDetector& longPress() noexcept { return press; }
    bool nudgeActive() const noexcept { return nudgeHeld; }

    void resized() override
    {
        const auto& t = owner.ctx.theme;
        auto corner = getLocalBounds().reduced (t.ipx (2));
        fitBtn.setBounds (corner.removeFromRight (t.touch()).removeFromTop (t.touch()));
        corner = getLocalBounds().reduced (t.ipx (2));
        corner.removeFromRight (t.touch());
        zoomOutBtn.setBounds (corner.removeFromRight (t.touch()).removeFromTop (t.touch()));
        corner = getLocalBounds().reduced (t.ipx (2));
        corner.removeFromRight (2 * t.touch());
        zoomInBtn.setBounds (corner.removeFromRight (t.touch()).removeFromTop (t.touch()));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.ctx.theme;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (2.0f));
        const auto& vp = owner.waveVp;

        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));

        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        g.saveState();
        g.reduceClipRegion (bounds.toNearestInt());

        // Center line (zero amplitude)
        const float midY = bounds.getCentreY();
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (midY), bounds.getX(), bounds.getRight());

        // Grid vertical lines
        const int grid = owner.currentGridSize;
        if (grid > 1 && grid <= 256)
        {
            g.setColour (t.colours.outline.withAlpha (0.25f));
            for (int i = 1; i < grid; ++i)
            {
                const float s = static_cast<float> (vp.toScreen (static_cast<double> (i) / static_cast<double> (grid)));
                if (s < 0.0f || s > 1.0f)
                    continue;
                g.drawVerticalLine (juce::roundToInt (bounds.getX() + bounds.getWidth() * s), bounds.getY(), bounds.getBottom());
            }
        }

        // Current frame waveform
        if (owner.currentFrameIndex < owner.wt.numFrames())
        {
            const auto& frame = owner.wt.getFrame (owner.currentFrameIndex);
            juce::Path path;
            const float startX = bounds.getX();
            const float w = bounds.getWidth();
            const float h = bounds.getHeight() * 0.45f;

            bool started = false;
            for (size_t i = 0; i < wtedit::kWaveformSize; ++i)
            {
                const double pos = static_cast<double> (i) / static_cast<double> (wtedit::kWaveformSize - 1);
                const float s = static_cast<float> (vp.toScreen (pos));
                if (s < -0.02f || s > 1.02f)
                    continue;
                const float x = startX + s * w;
                const float y = midY - frame[i] * h;
                if (! started)
                {
                    path.startNewSubPath (x, y);
                    started = true;
                }
                else
                    path.lineTo (x, y);
            }

            g.setColour (t.colours.accent);
            g.strokePath (path, juce::PathStrokeType (t.px (2.0f)));
        }
        g.restoreState();

        if (! vp.isFit())
        {
            g.setColour (t.colours.textDim);
            g.setFont (t.labelFont());
            g.drawText ("x" + juce::String (vp.zoom(), 1), bounds.toNearestInt().removeFromBottom (juce::roundToInt (t.px (16.0f))),
                        juce::Justification::bottomLeft, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.touchWave.down (e);
        if (e.source.getIndex() > 0 || owner.touchWave.twoDown())
        {
            press.cancel();
            owner.cancelStroke();
            return;
        }
        nudgeHeld = false;
        owner.beginStroke();
        owner.pushUndo();
        owner.strokeActive = true;
        downPos = e.position;
        press.press (e.position);
        handleDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        owner.touchWave.up (e);
        press.cancel();
        owner.hideLoupe();
        nudgeHeld = false;
        if (e.source.getIndex() > 0)
            return;
        owner.endStroke();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        owner.touchWave.drag (e);
        if (e.source.getIndex() > 0 || owner.touchWave.twoDown())
        {
            press.cancel();
            owner.cancelStroke();
            return;
        }
        if (! owner.strokeActive)
            return;
        press.moved (e.position);
        handleDrag (e);
        owner.showLoupeAt (*this, e.getPosition());
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const float s = e.position.x / static_cast<float> (juce::jmax (1, getWidth()));
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            owner.zoomWave (s, std::exp (wheel.deltaY * 2.0f));
        else
            owner.panWave (-(wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * static_cast<float> (owner.waveVp.span));
    }

    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override
    {
        owner.zoomWave (e.position.x / static_cast<float> (juce::jmax (1, getWidth())), scaleFactor);
    }

private:
    void handleDrag (const juce::MouseEvent& e)
    {
        const auto bounds = getLocalBounds().toFloat();
        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        const float screenX = juce::jlimit (0.0f, 1.0f, (e.position.x - bounds.getX()) / bounds.getWidth());
        const float normX = juce::jlimit (0.0f, 0.999f, static_cast<float> (owner.waveVp.toRange (static_cast<double> (screenX))));
        const float normY = juce::jlimit (-1.0f, 1.0f, 1.0f - 2.0f * ((e.position.y - bounds.getY()) / bounds.getHeight()));

        const int grid = owner.currentGridSize;
        const int step = static_cast<int> (normX * static_cast<float> (grid));

        owner.applyToolAt (nudgeHeld ? wtedit::DrawingTool::Nudge : owner.currentTool, grid, step, normY);
        repaint();
    }

    WavetableEditorSheet& owner;
    juce::TextButton zoomInBtn { "+" }, zoomOutBtn { "-" }, fitBtn { "Fit" };
    LongPressDetector press;
    juce::Point<float> downPos;
    bool nudgeHeld = false;
};

//==============================================================================
// SPECTRUM CANVAS COMPONENT
//==============================================================================
class WavetableEditorSheet::SpectrumCanvas final : public juce::Component
{
public:
    SpectrumCanvas (WavetableEditorSheet& ownerSheet)
        : owner (ownerSheet)
    {
        evenBtn.setClickingTogglesState (true);
        oddBtn.setClickingTogglesState (true);
        snapBtn.setClickingTogglesState (true);
        nudgeBtn.setClickingTogglesState (true);
        nudgeBtn.setTooltip ("Nudge all phases: a phase-bar drag moves every harmonic's phase by the same amount");
        nudgeBtn.onClick = [this] { owner.setNudgeAllPhases (nudgeBtn.getToggleState()); };

        scaleBtn.onClick = [this]
        {
            owner.pushUndo();
            if (owner.currentFrameIndex < owner.wt.numFrames())
            {
                wtedit::ops::fftScaleFreqValuesByBinIndex (owner.wt.getFrame (owner.currentFrameIndex));
                owner.isModified = true;
                owner.updateHeaderInfo();
                repaint();
                if (owner.waveCanvas)
                    owner.waveCanvas->repaint();
            }
        };

        fftMenuBtn.onClick = [this] { showFftMenu(); };

        addAndMakeVisible (evenBtn);
        addAndMakeVisible (oddBtn);
        addAndMakeVisible (snapBtn);
        addAndMakeVisible (nudgeBtn);
        addAndMakeVisible (scaleBtn);
        addAndMakeVisible (fftMenuBtn);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.ctx.theme;
        auto r = getLocalBounds().reduced (t.ipx (2));
        r.removeFromBottom (t.touch()); // reserve for button bar

        const auto bounds = r.toFloat();
        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        if (owner.currentFrameIndex >= owner.wt.numFrames())
            return;

        // Perform FFT real-to-polar
        std::array<float, wtedit::kWaveformSize> mags {};
        std::array<float, wtedit::kWaveformSize> phases {};
        const auto& frame = owner.wt.getFrame (owner.currentFrameIndex);
        wtedit::Radix2Fft::realToPolar (frame.sampleData(), mags.data(), phases.data(), wtedit::kWaveformSize);

        const float halfH = bounds.getHeight() * 0.5f;
        const auto magArea = bounds.withHeight (halfH);
        const auto phaseArea = bounds.withTrimmedTop (halfH);

        // Amplitude divider
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (phaseArea.getY()), bounds.getX(), bounds.getRight());

        // Draw harmonic amplitude bars (bins 1..64)
        const int numVisibleBins = 64;
        const float barW = magArea.getWidth() / static_cast<float> (numVisibleBins);

        for (int k = 1; k <= numVisibleBins; ++k)
        {
            const float mag = juce::jlimit (0.0f, 1.0f, mags[static_cast<size_t> (k)]);
            const float barH = mag * (magArea.getHeight() - t.px (4.0f));
            const float bx = magArea.getX() + static_cast<float> (k - 1) * barW;
            const float by = magArea.getBottom() - barH;

            g.setColour (k == lastTouchedBin ? t.colours.accent : t.colours.mod);
            g.fillRect (bx + t.px (1.0f), by, barW - t.px (2.0f), barH);

            // Phase bar (mapped -pi..+pi to height)
            const float ph = phases[static_cast<size_t> (k)];
            const float normPh = (ph + kPi) / (2.0f * kPi);
            const float pBarH = normPh * (phaseArea.getHeight() - t.px (4.0f));
            const float pby = phaseArea.getBottom() - pBarH;

            g.setColour (t.colours.textDim.withAlpha (0.6f));
            g.fillRect (bx + t.px (1.0f), pby, barW - t.px (2.0f), pBarH);
        }
    }

    void resized() override
    {
        const auto& t = owner.ctx.theme;
        auto barArea = getLocalBounds().removeFromBottom (t.touch()).reduced (t.ipx (2), 0);
        const int btnW = juce::jmax (t.touch(), barArea.getWidth() / 6);

        evenBtn.setBounds (barArea.removeFromLeft (btnW).reduced (t.ipx (2), 0));
        oddBtn.setBounds (barArea.removeFromLeft (btnW).reduced (t.ipx (2), 0));
        snapBtn.setBounds (barArea.removeFromLeft (btnW).reduced (t.ipx (2), 0));
        nudgeBtn.setBounds (barArea.removeFromLeft (btnW).reduced (t.ipx (2), 0));
        scaleBtn.setBounds (barArea.removeFromLeft (btnW).reduced (t.ipx (2), 0));
        fftMenuBtn.setBounds (barArea.reduced (t.ipx (2), 0));
    }

    void setNudgeToggle (bool on) { nudgeBtn.setToggleState (on, juce::dontSendNotification); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.touchSpectrum.down (e);
        if (e.source.getIndex() > 0 || owner.touchSpectrum.twoDown())
        {
            owner.cancelStroke(); // the second finger: a gesture, not a drawing
            return;
        }
        owner.beginStroke();
        owner.pushUndo();
        owner.strokeActive = true;
        handleMouse (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        owner.touchSpectrum.drag (e);
        if (e.source.getIndex() > 0 || owner.touchSpectrum.twoDown())
        {
            owner.cancelStroke();
            return;
        }
        if (! owner.strokeActive)
            return;
        handleMouse (e);
        owner.showLoupeAt (*this, e.getPosition());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        owner.touchSpectrum.up (e);
        owner.hideLoupe();
        if (e.source.getIndex() == 0)
            owner.endStroke();
    }

    /** Double tap on a bar: numeric entry of that bin (amplitude in the upper half, phase in the lower half). */
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        owner.cancelStroke(); // the second press of the double tap started an edit of its own: take it back
        bool phase = false;
        const int bin = binAt (e.getPosition(), phase);
        if (bin > 0)
            showBinEntry (bin, phase);
    }

    /** The bin (1..64) and region under a point; -1 outside the bars. */
    int binAt (juce::Point<int> p, bool& phase) const
    {
        const auto& t = owner.ctx.theme;
        auto area = getLocalBounds().reduced (t.ipx (2));
        area.removeFromBottom (t.touch());
        if (! area.contains (p) || area.getWidth() <= 0)
            return -1;
        const float relX = static_cast<float> (p.x - area.getX()) / static_cast<float> (area.getWidth());
        phase = static_cast<float> (p.y - area.getY()) >= static_cast<float> (area.getHeight()) * 0.5f;
        return juce::jlimit (1, 64, static_cast<int> (relX * 64.0f) + 1);
    }

private:
    void showBinEntry (int bin, bool phase)
    {
        auto* w = new juce::AlertWindow (phase ? "Bin " + juce::String (bin) + " phase" : "Bin " + juce::String (bin) + " amplitude",
                                         phase ? "degrees (0..360)" : "percent (0..100)", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("value", {}, {});
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true,
                            juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SpectrumCanvas> (this), bin, phase, w] (int result)
                                                                  {
                                                                      if (result == 1 && safe != nullptr)
                                                                          safe->owner.enterBinValue (bin, phase, w->getTextEditorContents ("value"));
                                                                  }),
                            true);
    }

    void handleMouse (const juce::MouseEvent& e)
    {
        const auto& t = owner.ctx.theme;
        auto area = getLocalBounds().reduced (t.ipx (2));
        area.removeFromBottom (t.touch());
        if (! area.contains (e.getPosition()) || area.getWidth() <= 0)
            return;

        const int numVisibleBins = 64;
        const float relX = static_cast<float> (e.x - area.getX()) / static_cast<float> (area.getWidth());
        int bin = static_cast<int> (relX * static_cast<float> (numVisibleBins)) + 1;
        bin = juce::jlimit (1, numVisibleBins, bin);
        lastTouchedBin = bin;

        if (evenBtn.getToggleState() && (bin % 2 != 0))
            return;
        if (oddBtn.getToggleState() && (bin % 2 == 0))
            return;

        if (owner.currentFrameIndex >= owner.wt.numFrames())
            return;

        const float halfH = static_cast<float> (area.getHeight()) * 0.5f;
        if (static_cast<float> (e.y - area.getY()) < halfH)
        {
            // Edit amplitude
            float val = 1.0f - static_cast<float> (e.y - area.getY()) / halfH;
            val = juce::jlimit (0.0f, 1.0f, val);
            if (snapBtn.getToggleState())
                val = std::round (val * 4.0f) / 4.0f;
            owner.applyBinEdit (bin, false, val);
        }
        else
        {
            // Edit phase (moves every harmonic when "nudge all phases" is on)
            const float val = 1.0f - static_cast<float> (e.y - area.getY() - static_cast<int> (halfH)) / halfH;
            owner.applyBinEdit (bin, true, (val * 2.0f - 1.0f) * kPi);
        }
        repaint();
    }

    void showFftMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Clear All");
        menu.addItem (2, "Clear HF (from cursor)");
        menu.addItem (3, "Clear LF (to cursor)");
        menu.addItem (4, "Generate Saw");
        menu.addItem (5, "Randomize Low Bins");
        menu.addItem (6, "Randomize All");
        menu.addItem (7, "Random Series Gaps");
        menu.addItem (8, "Progressive Fade");
        menu.addItem (9, "Octave Up");
        menu.addItem (10, "Octave Down");
        menu.addItem (11, "Repeat Bin Group");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fftMenuBtn),
            [this] (int result)
            {
                if (result <= 0 || owner.currentFrameIndex >= owner.wt.numFrames())
                    return;

                owner.pushUndo();
                auto& frame = owner.wt.getFrame (owner.currentFrameIndex);

                switch (result)
                {
                    case 1:  wtedit::ops::fftClearAll (frame); break;
                    case 2:  wtedit::ops::fftClearHf (frame, static_cast<size_t> (lastTouchedBin)); break;
                    case 3:  wtedit::ops::fftClearLf (frame, static_cast<size_t> (lastTouchedBin)); break;
                    case 4:  wtedit::ops::fftGenerateSaw (frame); break;
                    case 5:  wtedit::ops::fftRandomizeLowBins (frame, 16, false); break;
                    case 6:  wtedit::ops::fftRandomizeAll (frame); break;
                    case 7:  wtedit::ops::fftCreateRandomSeriesGaps (frame, 0.3f); break;
                    case 8:  wtedit::ops::fftProgressiveFade (frame, 1.5f); break;
                    case 9:  wtedit::ops::fftShiftOctaveUp (frame); break;
                    case 10: wtedit::ops::fftShiftOctaveDown (frame); break;
                    case 11: wtedit::ops::fftRepeatBinGroup (frame, static_cast<size_t> (lastTouchedBin)); break;
                    default: break;
                }

                owner.isModified = true;
                owner.updateHeaderInfo();
                repaint();
                if (owner.waveCanvas)
                    owner.waveCanvas->repaint();
            });
    }

    WavetableEditorSheet& owner;
    juce::TextButton evenBtn { "EVEN" };
    juce::TextButton oddBtn { "ODD" };
    juce::TextButton snapBtn { "SNAP 1/4" };
    juce::TextButton nudgeBtn { "NUDGE" };
    juce::TextButton scaleBtn { "SCALE" };
    juce::TextButton fftMenuBtn { "FFT ..." };
    int lastTouchedBin = 1;
};

//==============================================================================
// PROCESS VIEW COMPONENT
//==============================================================================
class WavetableEditorSheet::ProcessView final : public juce::Component
{
public:
    struct CommandItem
    {
        std::string name;
        std::string description;
        bool usesGrid;
    };

    ProcessView (WavetableEditorSheet& ownerSheet)
        : owner (ownerSheet)
    {
        scopeSingleBtn.setClickingTogglesState (true);
        scopeSingleBtn.setRadioGroupId (101);
        scopeSingleBtn.setToggleState (true, juce::dontSendNotification);

        scopeAllBtn.setClickingTogglesState (true);
        scopeAllBtn.setRadioGroupId (101);

        searchBox.setTextToShowWhenEmpty ("Search process commands...", owner.ctx.theme.colours.textDim);
        searchBox.onTextChange = [this] { updateFilteredList(); };

        addAndMakeVisible (scopeSingleBtn);
        addAndMakeVisible (scopeAllBtn);
        addAndMakeVisible (searchBox);
        addAndMakeVisible (listBox);

        initCommands();
        updateFilteredList();
    }

    void resized() override
    {
        const auto& t = owner.ctx.theme;
        auto r = getLocalBounds().reduced (t.gap());

        auto topRow = r.removeFromTop (t.touch());
        const int scopeW = juce::jmax (t.touch() * 2, t.ipx (90));
        scopeSingleBtn.setBounds (topRow.removeFromLeft (scopeW));
        topRow.removeFromLeft (t.gap());
        scopeAllBtn.setBounds (topRow.removeFromLeft (scopeW));
        topRow.removeFromLeft (t.gap());
        searchBox.setBounds (topRow);

        r.removeFromTop (t.gap());
        listBox.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.ctx.theme;
        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (6.0f));
    }

private:
    void initCommands()
    {
        allCommands = {
            { "Normalize", "Scale waveform to maximum amplitude (1.0 peak)", false },
            { "Remove DC Offset", "Subtract mean value to center waveform", false },
            { "Flip Vertical", "Invert polarity (y = -y)", false },
            { "Flip Horizontal", "Reverse sample playback direction", false },
            { "Fade Edges", "Smooth left/right boundaries based on grid size", true },
            { "Crossfade Edges", "Crossfade start and end samples together", true },
            { "X-Fade 16 Samples", "Quick 16-sample micro crossfade on boundaries", false },
            { "Shift to Zero Crossing", "Circularly rotate frame to start on zero-crossing", false },
            { "Filter Low Pass", "FFT brickwall low-pass filter at grid harmonic", true },
            { "Sample Redux", "Downsample effective resolution to grid divisions", true },
            { "Remove Fundamental", "High-pass by zeroing the first harmonic bin", false },
            { "Remove Low Spectra", "Zero lower FFT bins based on grid size", true },
            { "Remove Low Phases", "Zero harmonic phases of lower bins", true },
            { "Nudge Fundamental Phase", "Rotate phase of harmonic 1 by 50% (pi radians)", false },
            { "Set Spectra to All", "Copy current frame FFT magnitudes to all frames", false },
            { "Set Phases to All", "Copy current frame FFT phases to all frames", false },
            { "Blur Spectra Adjacent Bins", "Smooth FFT magnitude bins across frequency", true },
            { "Blur Phases Adjacent Bins", "Smooth FFT phase bins across frequency", true },
            { "Blur Spectra Frames", "Smooth harmonic magnitudes across time (all frames)", true },
            { "Blur Phases Frames", "Smooth harmonic phases across time (all frames)", true },
            { "Resize Tables Half", "Double frame count by interpolating (2x frames)", false },
            { "Resize Tables Double", "Halve frame count by merging pairs", false },
            { "Create PWM Sweep", "Generate 256-frame PWM sweep from current frame", false },
            { "Init Silence", "Clear frame to complete silence (0.0)", false },
            { "Init Default", "Reset wavetable to standard sawtooth frame", false }
        };
    }

    void updateFilteredList()
    {
        filteredCommands.clear();
        runButtons.clear();

        const auto query = searchBox.getText().trim().toLowerCase();
        for (const auto& cmd : allCommands)
        {
            if (query.isEmpty() || juce::String (cmd.name).toLowerCase().contains (query) ||
                juce::String (cmd.description).toLowerCase().contains (query))
            {
                filteredCommands.push_back (cmd);
            }
        }

        listBox.update (filteredCommands, scopeAllBtn.getToggleState(), owner);
    }

    class CommandList final : public juce::Component
    {
    public:
        void update (const std::vector<CommandItem>& items, bool all, WavetableEditorSheet& sheet)
        {
            commands = items;
            allFrames = all;
            ownerSheet = &sheet;
            buttons.clear();

            for (size_t i = 0; i < commands.size(); ++i)
            {
                auto btn = std::make_unique<juce::TextButton> ("Run");
                btn->onClick = [this, i]
                {
                    if (ownerSheet && i < commands.size())
                    {
                        ownerSheet->executeProcessCommand (commands[i].name, allFrames, ownerSheet->currentGridSize);
                    }
                };
                addAndMakeVisible (*btn);
                buttons.push_back (std::move (btn));
            }
            resized();
            repaint();
        }

        void resized() override
        {
            if (! ownerSheet)
                return;
            const auto& t = ownerSheet->ctx.theme;
            const int itemH = juce::jmax (t.touch(), t.ipx (48));
            int y = 0;

            for (size_t i = 0; i < buttons.size(); ++i)
            {
                const int btnW = juce::jmax (t.touch() * 2, t.ipx (80));
                buttons[i]->setBounds (getWidth() - btnW - t.gap(), y + (itemH - t.touch()) / 2, btnW, t.touch());
                y += itemH + t.ipx (2);
            }
        }

        void paint (juce::Graphics& g) override
        {
            if (! ownerSheet)
                return;
            const auto& t = ownerSheet->ctx.theme;
            const int itemH = juce::jmax (t.touch(), t.ipx (48));
            int y = 0;

            for (size_t i = 0; i < commands.size(); ++i)
            {
                auto r = juce::Rectangle<int> (0, y, getWidth(), itemH).reduced (t.ipx (4), t.ipx (2));
                g.setColour (t.colours.panel);
                g.fillRoundedRectangle (r.toFloat(), t.px (4.0f));

                g.setColour (t.colours.text);
                g.setFont (t.valueFont());
                g.drawText (juce::String (commands[i].name) + (commands[i].usesGrid ? (" [Grid " + juce::String (ownerSheet->currentGridSize) + "]") : juce::String()),
                            r.withTrimmedRight (t.touch() * 2 + t.gap() * 2), juce::Justification::topLeft, true);

                g.setColour (t.colours.textDim);
                g.setFont (t.labelFont());
                g.drawText (juce::String (commands[i].description), r.withTrimmedTop (t.ipx (20)).withTrimmedRight (t.touch() * 2 + t.gap() * 2),
                            juce::Justification::topLeft, true);

                y += itemH + t.ipx (2);
            }
        }

    private:
        std::vector<CommandItem> commands;
        bool allFrames = false;
        WavetableEditorSheet* ownerSheet = nullptr;
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
    };

    WavetableEditorSheet& owner;
    juce::TextButton scopeSingleBtn { "SINGLE" };
    juce::TextButton scopeAllBtn { "ALL" };
    juce::TextEditor searchBox;
    CommandList listBox;
    std::vector<CommandItem> allCommands;
    std::vector<CommandItem> filteredCommands;
    std::vector<std::unique_ptr<juce::TextButton>> runButtons;
};

//==============================================================================
// FORMULA VIEW COMPONENT
//==============================================================================
class WavetableEditorSheet::FormulaView final : public juce::Component
{
public:
    FormulaView (WavetableEditorSheet& ownerSheet)
        : owner (ownerSheet)
    {
        formulaInput.setText ("sin(2*pi*x)");
        formulaInput.onTextChange = [this] { validateFormula(); };

        applyBtn.onClick = [this]
        {
            std::string err;
            if (owner.applyFormula (formulaInput.getText().toStdString(), &err))
            {
                statusLabel.setText ("Formula applied successfully", juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, owner.ctx.theme.colours.ok);
            }
            else
            {
                statusLabel.setText (err, juce::dontSendNotification);
                statusLabel.setColour (juce::Label::textColourId, owner.ctx.theme.colours.warn);
            }
        };

        clearBtn.onClick = [this]
        {
            formulaInput.clear();
            validateFormula();
        };

        presetsBtn.onClick = [this] { showPresetsMenu(); };

        addAndMakeVisible (formulaInput);
        addAndMakeVisible (applyBtn);
        addAndMakeVisible (clearBtn);
        addAndMakeVisible (presetsBtn);
        addAndMakeVisible (statusLabel);

        initChips();
        validateFormula();
    }

    void resized() override
    {
        const auto& t = owner.ctx.theme;
        auto r = getLocalBounds().reduced (t.gap());

        auto inputRow = r.removeFromTop (t.touch());
        clearBtn.setBounds (inputRow.removeFromRight (t.touch()));
        inputRow.removeFromRight (t.gap());
        applyBtn.setBounds (inputRow.removeFromRight (juce::jmax (t.touch() * 2, t.ipx (80))));
        inputRow.removeFromRight (t.gap());
        presetsBtn.setBounds (inputRow.removeFromRight (juce::jmax (t.touch() * 2, t.ipx (90))));
        inputRow.removeFromRight (t.gap());
        formulaInput.setBounds (inputRow);

        r.removeFromTop (t.ipx (4));
        statusLabel.setBounds (r.removeFromTop (t.ipx (24)));
        r.removeFromTop (t.gap());

        // Layout chips in rows
        const int chipH = t.touch();
        const int chipW = juce::jmax (t.touch(), t.ipx (50));
        int x = r.getX(), y = r.getY();

        for (auto& chip : chips)
        {
            if (x + chipW > r.getRight())
            {
                x = r.getX();
                y += chipH + t.ipx (4);
            }
            chip->setBounds (x, y, chipW, chipH);
            x += chipW + t.ipx (4);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.ctx.theme;
        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (6.0f));
    }

private:
    void initChips()
    {
        const char* tokens[] = {
            "sin", "cos", "tan", "asin", "acos", "atan", "abs", "sqrt", "exp", "log", "min", "max", "rint", "sign",
            "x", "w", "y", "z", "q", "in", "sel", "rand", "pi", "e",
            "+", "-", "*", "/", "^", "?", ":", "%", "(", ")"
        };

        for (const auto* tok : tokens)
        {
            auto btn = std::make_unique<juce::TextButton> (tok);
            const juce::String textToInsert = tok;
            btn->onClick = [this, textToInsert]
            {
                formulaInput.insertTextAtCaret (textToInsert + " ");
                validateFormula();
            };
            addAndMakeVisible (*btn);
            chips.push_back (std::move (btn));
        }
    }

    void validateFormula()
    {
        formula::Formula f;
        const auto err = formula::Formula::compile (formulaInput.getText().toStdString(), f);
        if (err.ok)
        {
            statusLabel.setText (f.isMultiFrame() ? "Valid multi-frame formula (uses y/z)" : "Valid single-frame formula",
                                 juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, owner.ctx.theme.colours.ok);
        }
        else
        {
            statusLabel.setText (juce::String ("Error at pos ") + juce::String (err.position) + ": " + err.message,
                                 juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, owner.ctx.theme.colours.warn);
        }
    }

    void ensureLibrary()
    {
        if (formulaLibrary == nullptr)
        {
            LibraryMenuSpec spec;
            spec.category = kLibFormulas;
            spec.noun = "formula";
            spec.suggestedName = "My formula";
            spec.makeItem = [this] (const juce::String& name)
            {
                const auto text = formulaInput.getText().toStdString();
                return text.empty() ? std::string() : serializeFormulaItem (name.toStdString(), text);
            };
            spec.applyItem = [this] (const juce::String&, const std::string& json) { return setFormulaFromItem (json); };
            spec.fileWildcards = "*.json";
            spec.applyFile = [this] (const juce::File& file) { return setFormulaFromItem (file.loadFileAsString().toStdString()); };
            formulaLibrary = std::make_shared<LibraryMenu> (owner.ctx, std::move (spec));
        }
    }

    void showPresetsMenu()
    {
        ensureLibrary();
        juce::PopupMenu menu;
        juce::PopupMenu examples;
        const auto& list = formulaExamples();
        for (size_t i = 0; i < list.size(); ++i)
            examples.addItem (static_cast<int> (i) + 1, juce::String (list[i].name) + ":  " + list[i].expression);
        menu.addSubMenu ("Examples", examples);
        menu.addSeparator();
        formulaLibrary->addTo (menu, 1000);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetsBtn),
            [this, library = formulaLibrary] (int result)
            {
                if (result >= 1000)
                    library->run (result);
                else if (result > 0 && static_cast<size_t> (result) <= formulaExamples().size())
                {
                    formulaInput.setText (formulaExamples()[static_cast<size_t> (result) - 1].expression);
                    validateFormula();
                }
            });
    }

public:
    /** Makes the library menu (also done by the Presets button). */
    void openLibrary() { ensureLibrary(); }
    /** Sets the text field from a formula item of the user library; false when it is not one. */
    bool setFormulaFromItem (const std::string& json)
    {
        std::string name, expression;
        if (! parseFormulaItem (json, name, expression))
            return false;
        formulaInput.setText (expression);
        validateFormula();
        return true;
    }
    LibraryMenu* library() noexcept { return formulaLibrary.get(); }
    const juce::String formulaText() const { return formulaInput.getText(); }
    void setFormulaText (const juce::String& text) { formulaInput.setText (text); validateFormula(); }

private:
    std::shared_ptr<LibraryMenu> formulaLibrary;
    WavetableEditorSheet& owner;
    juce::TextEditor formulaInput;
    juce::TextButton applyBtn { "Apply" };
    juce::TextButton clearBtn { "Clear" };
    juce::TextButton presetsBtn { "Presets" };
    juce::Label statusLabel;
    std::vector<std::unique_ptr<juce::TextButton>> chips;
};

//==============================================================================
// LAYERS VIEW COMPONENT (VITAL GROUP MODEL)
//==============================================================================
class WavetableEditorSheet::LayersView final : public juce::Component
{
public:
    LayersView (WavetableEditorSheet& ownerSheet)
        : owner (ownerSheet)
    {
        addComponentBtn.onClick = [this] { showAddComponentMenu(); };
        renderTableBtn.onClick = [this]
        {
            owner.pushUndo();
            owner.renderLayersToTable();
        };

        addAndMakeVisible (addComponentBtn);
        addAndMakeVisible (renderTableBtn);
        addAndMakeVisible (infoLabel);
        infoLabel.setText ("Vital Group: Sources & Modifiers with Keyframe Timeline (0..255)", juce::dontSendNotification);
    }

    void resized() override
    {
        const auto& t = owner.ctx.theme;
        auto r = getLocalBounds().reduced (t.gap());

        auto topRow = r.removeFromTop (t.touch());
        const int btnW = juce::jmax (t.touch() * 2, t.ipx (140));
        addComponentBtn.setBounds (topRow.removeFromLeft (btnW));
        topRow.removeFromLeft (t.gap());
        renderTableBtn.setBounds (topRow.removeFromLeft (btnW));
        topRow.removeFromLeft (t.gap());
        infoLabel.setBounds (topRow);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.ctx.theme;
        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (6.0f));

        auto r = getLocalBounds().reduced (t.gap());
        r.removeFromTop (t.touch() + t.gap());

        // Timeline visualization 0..255
        const int timelineH = t.ipx (36);
        auto timelineRect = r.removeFromTop (timelineH);
        g.setColour (t.colours.panel);
        g.fillRoundedRectangle (timelineRect.toFloat(), t.px (4.0f));
        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (timelineRect.toFloat(), t.px (4.0f), 1.0f);

        // Draw component summary list
        const size_t numComp = owner.vitalGroup.numComponents();
        int y = r.getY() + t.gap();
        for (size_t i = 0; i < numComp && y < r.getBottom() - t.touch(); ++i)
        {
            const auto* c = owner.vitalGroup.getComponent (i);
            if (! c)
                continue;
            auto cr = juce::Rectangle<int> (r.getX(), y, r.getWidth(), t.touch()).reduced (0, t.ipx (2));
            g.setColour (t.colours.panel);
            g.fillRoundedRectangle (cr.toFloat(), t.px (4.0f));
            g.setColour (t.colours.text);
            g.setFont (t.valueFont());
            g.drawText ("Component " + juce::String (i + 1) + ": " + juce::String (c->name()) +
                        " (" + juce::String (c->numKeyframes()) + " keyframes)",
                        cr.reduced (t.gap(), 0), juce::Justification::centredLeft, true);
            y += t.touch() + t.ipx (4);
        }
    }

private:
    void showAddComponentMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Wave Source");
        menu.addItem (2, "Phase Modifier");
        menu.addItem (3, "Wave Window Modifier");
        menu.addItem (4, "Frequency Filter Modifier");
        menu.addItem (5, "Slew Limiter Modifier");
        menu.addItem (6, "Wave Folder Modifier");
        menu.addItem (7, "Wave Warp Modifier");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addComponentBtn),
            [this] (int result)
            {
                if (result <= 0)
                    return;
                owner.pushUndo();
                using CT = wtedit::vital::ComponentType;
                CT type = CT::WaveSource;
                std::string name = "Wave Source";

                switch (result)
                {
                    case 1: type = CT::WaveSource; name = "Wave Source"; break;
                    case 2: type = CT::PhaseModifier; name = "Phase Shift"; break;
                    case 3: type = CT::WaveWindowModifier; name = "Wave Window"; break;
                    case 4: type = CT::FrequencyFilterModifier; name = "Frequency Filter"; break;
                    case 5: type = CT::SlewLimitModifier; name = "Slew Limiter"; break;
                    case 6: type = CT::WaveFoldModifier; name = "Wave Folder"; break;
                    case 7: type = CT::WaveWarpModifier; name = "Wave Warp"; break;
                    default: break;
                }

                auto comp = std::make_unique<wtedit::vital::Component> (type, name);
                comp->insertKeyframe (wtedit::vital::Keyframe (0.0f));
                comp->insertKeyframe (wtedit::vital::Keyframe (255.0f));
                owner.vitalGroup.addComponent (std::move (comp));
                repaint();
            });
    }

    WavetableEditorSheet& owner;
    juce::TextButton addComponentBtn { "+ Add Component" };
    juce::TextButton renderTableBtn { "Render to Table" };
    juce::Label infoLabel;
};

//==============================================================================
// IMPORT WORKER
//==============================================================================
class WavetableEditorSheet::ImportJob final : public juce::Thread
{
public:
    ImportJob (std::vector<float> samplesIn, double sampleRateIn, wtedit::ImportOptions optionsIn)
        : juce::Thread ("wavetable import"), samples (std::move (samplesIn)), sampleRate (sampleRateIn), options (optionsIn), result ("Imported Audio")
    {
    }

    void run() override
    {
        wtedit::Wavetable imported ("Imported Audio");
        ok = wtedit::importAudioSamples (samples.data(), samples.size(), sampleRate, options, imported);
        result = std::move (imported);
        samples.clear();
        done = true;
    }

    std::vector<float> samples;
    double sampleRate;
    wtedit::ImportOptions options;
    wtedit::Wavetable result;
    bool ok = false;
    std::atomic<bool> done { false };
    std::atomic<bool> cancelled { false };
};

//==============================================================================
// THUMBNAILS STRIP COMPONENT
//==============================================================================
class WavetableEditorSheet::ThumbnailsStrip final : public juce::Component
{
public:
    ThumbnailsStrip (WavetableEditorSheet& ownerSheet)
        : owner (ownerSheet)
    {
        setOpaque (false);
        press.onLongPress = [this]
        {
            if (downIdx >= 0)
            {
                reordering = true; // held still on a thumbnail: drag it to another place (yellow insertion cursor)
                dropBoundary = downIdx;
                repaint();
            }
        };
    }

    LongPressDetector& longPress() noexcept { return press; }
    bool isReordering() const noexcept { return reordering; }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.ctx.theme;
        const size_t num = owner.wt.numFrames();
        const int thumbW = juce::jmax (t.touch(), t.ipx (56));
        const int thumbH = getHeight() - t.ipx (4);

        int x = t.gap();
        for (size_t i = 0; i < num; ++i)
        {
            auto thumbRect = juce::Rectangle<int> (x, t.ipx (2), thumbW, thumbH);
            const bool isCurrent = (i == owner.currentFrameIndex);
            const bool inRange = (owner.rangeStart >= 0 && owner.rangeEnd >= 0 &&
                                  static_cast<int> (i) >= owner.rangeStart && static_cast<int> (i) <= owner.rangeEnd);

            g.setColour (isCurrent ? t.colours.accent.withAlpha (0.3f) :
                         inRange ? t.colours.mod.withAlpha (0.2f) : t.colours.panelRaised);
            g.fillRoundedRectangle (thumbRect.toFloat(), t.px (4.0f));

            g.setColour (isCurrent ? t.colours.accent :
                         inRange ? t.colours.mod : t.colours.outline);
            g.drawRoundedRectangle (thumbRect.toFloat(), t.px (4.0f), isCurrent ? 2.0f : 1.0f);

            // Draw miniature waveform
            const auto& frame = owner.wt.getFrame (i);
            juce::Path p;
            const float midY = static_cast<float> (thumbRect.getCentreY());
            const float scaleY = static_cast<float> (thumbH) * 0.35f;

            for (size_t s = 0; s < wtedit::kWaveformSize; s += 32)
            {
                const float px = static_cast<float> (thumbRect.getX()) +
                    (static_cast<float> (s) / static_cast<float> (wtedit::kWaveformSize - 1)) * static_cast<float> (thumbW);
                const float py = midY - frame[s] * scaleY;
                if (s == 0)
                    p.startNewSubPath (px, py);
                else
                    p.lineTo (px, py);
            }
            g.setColour (isCurrent ? t.colours.accent : t.colours.textDim);
            g.strokePath (p, juce::PathStrokeType (1.0f));

            // Index label
            g.setColour (t.colours.text);
            g.setFont (t.labelFont());
            g.drawText (juce::String (i + 1), thumbRect.withTrimmedTop (thumbH - t.ipx (14)), juce::Justification::centred, false);

            x += thumbW + t.ipx (4);
        }

        if (reordering)
        {
            const int step = thumbW + t.ipx (4);
            const int cx = t.gap() + dropBoundary * step - t.ipx (2);
            g.setColour (t.colours.warn);
            g.fillRect (cx - t.ipx (1), t.ipx (1), t.ipx (3), getHeight() - t.ipx (2));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = owner.ctx.theme;
        const int thumbW = juce::jmax (t.touch(), t.ipx (56));
        const int totalStep = thumbW + t.ipx (4);
        const int idx = (e.x - t.gap()) / totalStep;
        downIdx = e.x >= t.gap() && idx >= 0 && static_cast<size_t> (idx) < owner.wt.numFrames() ? idx : -1;
        reordering = false;
        press.press (e.position);

        if (idx >= 0 && static_cast<size_t> (idx) < owner.wt.numFrames())
        {
            if (owner.rangeSelectActive)
            {
                if (owner.rangeStart < 0)
                    owner.rangeStart = idx;
                else
                    owner.rangeEnd = idx;
            }
            else
            {
                owner.rangeStart = -1;
                owner.rangeEnd = -1;
            }
            owner.setCurrentFrame (static_cast<size_t> (idx));
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! reordering)
        {
            press.moved (e.position);
            return;
        }
        const auto& t = owner.ctx.theme;
        const int step = juce::jmax (t.touch(), t.ipx (56)) + t.ipx (4);
        dropBoundary = juce::jlimit (0, static_cast<int> (owner.wt.numFrames()), juce::roundToInt (static_cast<float> (e.x - t.gap()) / static_cast<float> (step)));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        press.cancel();
        if (reordering && downIdx >= 0)
        {
            // the cursor sits in a gap between two thumbnails: the frame goes there
            const int to = dropBoundary > downIdx ? dropBoundary - 1 : dropBoundary;
            if (to != downIdx)
                owner.moveFrameTo (static_cast<size_t> (downIdx), static_cast<size_t> (to));
        }
        reordering = false;
        downIdx = -1;
        repaint();
    }

private:
    WavetableEditorSheet& owner;
    LongPressDetector press;
    int downIdx = -1, dropBoundary = 0;
    bool reordering = false;
};

//==============================================================================
// IMPORT SHEET COMPONENT
//==============================================================================
class WavetableEditorSheet::ImportSheet final : public juce::Component
{
public:
    using ImportKind = WavetableEditorSheet::ImportKind;
    ImportSheet (WavetableEditorSheet& ownerSheet)
        : owner (ownerSheet)
    {
        audioTabBtn.setClickingTogglesState (true);
        audioTabBtn.setRadioGroupId (201);
        audioTabBtn.setToggleState (true, juce::dontSendNotification);

        imageTabBtn.setClickingTogglesState (true);
        imageTabBtn.setRadioGroupId (201);

        wtTabBtn.setClickingTogglesState (true);
        wtTabBtn.setRadioGroupId (201);

        cancelBtn.onClick = [this]
        {
            owner.cancelImport(); // a running import is discarded when it ends
            setVisible (false);
        };
        importBtn.onClick = [this] { doImport(); };
        chooseBtn.onClick = [this] { chooseFile(); };
        addAndMakeVisible (chooseBtn);

        interpolateToggle.setToggleState (true, juce::dontSendNotification);
        fadeEdgesToggle.setToggleState (false, juce::dontSendNotification);
        normalizeToggle.setToggleState (true, juce::dontSendNotification);

        addAndMakeVisible (audioTabBtn);
        addAndMakeVisible (imageTabBtn);
        addAndMakeVisible (wtTabBtn);
        addAndMakeVisible (methodChoice);
        addAndMakeVisible (samplesPerCycleBox);
        addAndMakeVisible (interpolateToggle);
        addAndMakeVisible (fadeEdgesToggle);
        addAndMakeVisible (normalizeToggle);
        addAndMakeVisible (channelChoice);
        addAndMakeVisible (cancelBtn);
        addAndMakeVisible (importBtn);
        addAndMakeVisible (infoLabel);
        addChildComponent (progressBar);

        methodChoice.addItem ("Constant framesize (Pitch Average)", 1);
        methodChoice.addItem ("Dynamic pitch - zero snap", 2);
        methodChoice.addItem ("Dynamic pitch - follow", 3);
        methodChoice.addItem ("Frequency estimation", 4);
        methodChoice.addItem ("FFT 256", 5);
        methodChoice.addItem ("FFT 512", 6);
        methodChoice.addItem ("FFT 1024", 7);
        methodChoice.addItem ("FFT 2048", 8);
        methodChoice.setSelectedId (1, juce::dontSendNotification);

        channelChoice.addItem ("Left / Mono (Osc 1, 3)", 1);
        channelChoice.addItem ("Right (Osc 2)", 2);
        channelChoice.setSelectedId (1, juce::dontSendNotification);

        samplesPerCycleBox.setText ("2048");
        infoLabel.setText ("Choose a file and a method", juce::dontSendNotification);
    }

    void resized() override
    {
        const auto& t = owner.ctx.theme;
        auto r = getLocalBounds().reduced (t.gap());

        auto tabs = r.removeFromTop (t.touch());
        const int tabW = juce::jmax (t.touch() * 2, r.getWidth() / 3);
        audioTabBtn.setBounds (tabs.removeFromLeft (tabW));
        imageTabBtn.setBounds (tabs.removeFromLeft (tabW));
        wtTabBtn.setBounds (tabs);

        r.removeFromTop (t.gap());
        chooseBtn.setBounds (r.removeFromTop (t.touch()));
        r.removeFromTop (t.gap());
        methodChoice.setBounds (r.removeFromTop (t.touch()));
        r.removeFromTop (t.gap());

        auto sampleRow = r.removeFromTop (t.touch());
        samplesPerCycleBox.setBounds (sampleRow.removeFromLeft (juce::jmax (t.touch() * 2, t.ipx (120))));
        sampleRow.removeFromLeft (t.gap());
        channelChoice.setBounds (sampleRow);

        r.removeFromTop (t.gap());
        auto toggleRow = r.removeFromTop (t.touch());
        const int tw = toggleRow.getWidth() / 3;
        interpolateToggle.setBounds (toggleRow.removeFromLeft (tw));
        fadeEdgesToggle.setBounds (toggleRow.removeFromLeft (tw));
        normalizeToggle.setBounds (toggleRow);

        r.removeFromTop (t.gap());
        infoLabel.setBounds (r.removeFromTop (t.ipx (24)));
        r.removeFromTop (t.gap());
        progressBar.setBounds (r.removeFromTop (t.touch()));

        auto bottom = r.removeFromBottom (t.touch());
        const int bw = juce::jmax (t.touch() * 2, t.ipx (100));
        cancelBtn.setBounds (bottom.removeFromLeft (bw));
        importBtn.setBounds (bottom.removeFromRight (bw));
    }

    /** While the worker thread imports: the progress bar (indeterminate, the importer reports no fraction) and no second import. */
    void setBusy (bool busy)
    {
        progressBar.setVisible (busy);
        importBtn.setEnabled (! busy);
        infoLabel.setText (busy ? "Importing ... (you can cancel)" : "Select file and method to import into wavetable", juce::dontSendNotification);
    }
    bool progressVisible() const { return progressBar.isVisible(); }
    void setInfo (const juce::String& text) { infoLabel.setText (text, juce::dontSendNotification); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.ctx.theme;
        g.setColour (t.colours.panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), t.px (8.0f));
        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat(), t.px (8.0f), 1.5f);
    }

private:
    /** The Choose button: the host asks for a file, the sheet remembers it. The wildcard follows the tab. */
    void chooseFile()
    {
        FileRequest request;
        request.title = imageTabBtn.getToggleState() ? "Import an image as a wavetable" : (wtTabBtn.getToggleState() ? "Import a wavetable" : "Import audio as a wavetable");
        request.wildcards = imageTabBtn.getToggleState() ? "*.png;*.jpg;*.jpeg;*.gif"
                                                         : (wtTabBtn.getToggleState() ? "*.wav;*.vitaltable;*.flac;*.aif;*.aiff" : audioFileWildcard());
        request.purpose = "wavetable";
        if (! owner.ctx.host.requestFile (request, [safe = juce::Component::SafePointer<ImportSheet> (this)] (const juce::File& file)
            {
                if (safe != nullptr)
                    safe->setChosen (file);
            }))
            infoLabel.setText ("This host cannot ask for a file", juce::dontSendNotification);
    }

    void doImport()
    {
        if (! chosen.existsAsFile())
        {
            chooseFile(); // nothing chosen yet: Import asks for the file first
            return;
        }
        wtedit::ImportOptions opts;
        opts.channel = (channelChoice.getSelectedId() == 2) ? 1 : 0;
        opts.fadeEdges = fadeEdgesToggle.getToggleState();
        opts.normalize = normalizeToggle.getToggleState();
        opts.frameSize = static_cast<size_t> (samplesPerCycleBox.getText().getIntValue());
        if (opts.frameSize < 32)
            opts.frameSize = 2048;
        switch (methodChoice.getSelectedId())
        {
            case 1: opts.mode = wtedit::ImportMode::PitchAverage; break;
            case 2: opts.mode = wtedit::ImportMode::PitchDetectZeroSnap; break;
            case 3: opts.mode = wtedit::ImportMode::PitchDetectFollow; break;
            case 4: opts.mode = wtedit::ImportMode::ByFrameSize; break;
            case 5: opts.mode = wtedit::ImportMode::Fft256; break;
            case 6: opts.mode = wtedit::ImportMode::Fft512; break;
            case 7: opts.mode = wtedit::ImportMode::Fft1024; break;
            case 8: opts.mode = wtedit::ImportMode::Fft2048; break;
            default: opts.mode = wtedit::ImportMode::PitchAverage; break;
        }
        const auto kind = imageTabBtn.getToggleState() ? ImportKind::image : (wtTabBtn.getToggleState() ? ImportKind::wavetable : ImportKind::audio);
        if (! owner.importFile (chosen, kind, opts))
        {
            infoLabel.setText (owner.statusText().isEmpty() ? juce::String ("Import failed") : owner.statusText(), juce::dontSendNotification);
            return;
        }
        if (kind != ImportKind::audio)
            setVisible (false);
        // audio: the sheet stays up with its progress bar until the worker is done
    }

public:
    void setChosen (const juce::File& file)
    {
        chosen = file;
        infoLabel.setText (file.getFileName(), juce::dontSendNotification);
        chooseBtn.setButtonText (file.getFileName());
    }
    const juce::File& chosenFile() const noexcept { return chosen; }
    void selectKind (ImportKind kind)
    {
        audioTabBtn.setToggleState (kind == ImportKind::audio, juce::dontSendNotification);
        imageTabBtn.setToggleState (kind == ImportKind::image, juce::dontSendNotification);
        wtTabBtn.setToggleState (kind == ImportKind::wavetable, juce::dontSendNotification);
    }
    void pressImport() { doImport(); }

private:
    juce::File chosen;
    juce::TextButton chooseBtn { "Choose file..." };

    WavetableEditorSheet& owner;
    juce::TextButton audioTabBtn { "Audio" };
    juce::TextButton imageTabBtn { "Image" };
    juce::TextButton wtTabBtn { "Wavetable" };
    juce::ComboBox methodChoice;
    juce::TextEditor samplesPerCycleBox;
    juce::ToggleButton interpolateToggle { "Interpolate" };
    juce::ToggleButton fadeEdgesToggle { "Fade Edges" };
    juce::ToggleButton normalizeToggle { "Normalize" };
    juce::ComboBox channelChoice;
    juce::TextButton cancelBtn { "Cancel" };
    juce::TextButton importBtn { "Import" };
    juce::Label infoLabel;
    double progressValue { -1.0 }; // < 0: indeterminate
    juce::ProgressBar progressBar { progressValue };
};

//==============================================================================
// WAVETABLE EDITOR SHEET MAIN IMPLEMENTATION
//==============================================================================
WavetableEditorSheet::~WavetableEditorSheet()
{
    stopTimer();
    if (importJob != nullptr)
        importJob->waitForThreadToExit (-1);
}

WavetableEditorSheet::WavetableEditorSheet (UiContext& context, wtedit::Wavetable initialTable)
    : ctx (context), wt (std::move (initialTable))
{
    // Sync vital group from wavetable
    vitalGroup.initFromWavetable (wt);
    undoStack.push (wt); // baseline state

    // Header buttons
    closeBtn.onClick = [this] { requestClose(); };
    touchWave.onGesture = [this] (const TouchTracker::Gesture& g) { handleTouchGesture (g, true); };
    touchSpectrum.onGesture = [this] (const TouchTracker::Gesture& g) { handleTouchGesture (g, false); };

    undoBtn.onClick = [this] { performUndo(); };
    redoBtn.onClick = [this] { performRedo(); };
    menuBtn.onClick = [this] { showOptionsMenu(); };

    addAndMakeVisible (closeBtn);
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);
    addAndMakeVisible (menuBtn);

    // Mode tabs
    const auto setupTab = [this] (juce::TextButton& btn, Mode m)
    {
        btn.setClickingTogglesState (true);
        btn.setRadioGroupId (301);
        btn.onClick = [this, m] { setMode (m); };
        addAndMakeVisible (btn);
    };

    setupTab (waveTabBtn, Mode::wave);
    setupTab (spectrumTabBtn, Mode::spectrum);
    setupTab (processTabBtn, Mode::process);
    setupTab (formulaTabBtn, Mode::formula);
    setupTab (layersTabBtn, Mode::layers);
    waveTabBtn.setToggleState (true, juce::dontSendNotification);

    // Tool strip buttons
    const auto setupTool = [this] (juce::TextButton& btn, wtedit::DrawingTool t)
    {
        btn.setClickingTogglesState (true);
        btn.setRadioGroupId (401);
        btn.onClick = [this, t] { currentTool = t; };
        addAndMakeVisible (btn);
    };

    setupTool (toolFlatBtn, wtedit::DrawingTool::FlatLine);
    setupTool (toolSineBtn, wtedit::DrawingTool::Sine);
    setupTool (toolSlopeUpBtn, wtedit::DrawingTool::SlopeUp);
    setupTool (toolSlopeDownBtn, wtedit::DrawingTool::SlopeDown);
    setupTool (toolCurveUpBtn, wtedit::DrawingTool::CurveUp);
    setupTool (toolCurveDownBtn, wtedit::DrawingTool::CurveDown);
    setupTool (toolHalfSinePeakBtn, wtedit::DrawingTool::HalfSinePeak);
    setupTool (toolHalfSineValleyBtn, wtedit::DrawingTool::HalfSineValley);
    setupTool (toolInterpLinBtn, wtedit::DrawingTool::InterpolateLinear);
    setupTool (toolInterpCurvedBtn, wtedit::DrawingTool::InterpolateCurved);
    setupTool (toolNudgeBtn, wtedit::DrawingTool::Nudge);
    setupTool (toolNoiseBtn, wtedit::DrawingTool::Noise);
    setupTool (toolMirrorBtn, wtedit::DrawingTool::Mirror);
    toolFlatBtn.setToggleState (true, juce::dontSendNotification);

    gridBtn.onClick = [this]
    {
        juce::PopupMenu menu;
        const int sizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
        for (int s : sizes)
            menu.addItem (s, "Grid " + juce::String (s));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (gridBtn),
            [this] (int result)
            {
                if (result > 0)
                {
                    currentGridSize = result;
                    gridBtn.setButtonText ("Grid " + juce::String (result));
                    if (waveCanvas)
                        waveCanvas->repaint();
                }
            });
    };
    addAndMakeVisible (gridBtn);

    // Frame strip navigation
    prevFrameBtn.onClick = [this]
    {
        if (currentFrameIndex > 0)
            setCurrentFrame (currentFrameIndex - 1);
    };

    nextFrameBtn.onClick = [this]
    {
        if (currentFrameIndex + 1 < wt.numFrames())
            setCurrentFrame (currentFrameIndex + 1);
    };

    copyBtn.onClick = [this] { copyCurrentFrame(); };
    pasteBtn.onClick = [this] { pasteFrame(); };
    removeMenuBtn.onClick = [this] { showRemoveMenu(); };
    sortMenuBtn.onClick = [this] { showSortMenu(); };
    morphMenuBtn.onClick = [this] { showMorphMenu(); };

    rangeToggleBtn.setClickingTogglesState (true);
    rangeToggleBtn.onClick = [this]
    {
        rangeSelectActive = rangeToggleBtn.getToggleState();
        if (! rangeSelectActive)
        {
            rangeStart = -1;
            rangeEnd = -1;
        }
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    };

    addAndMakeVisible (framePosLabel);
    addAndMakeVisible (prevFrameBtn);
    addAndMakeVisible (nextFrameBtn);
    addAndMakeVisible (copyBtn);
    addAndMakeVisible (pasteBtn);
    addAndMakeVisible (removeMenuBtn);
    addAndMakeVisible (sortMenuBtn);
    addAndMakeVisible (morphMenuBtn);
    addAndMakeVisible (rangeToggleBtn);

    // Child views
    waveCanvas = std::make_unique<WaveCanvas> (*this);
    spectrumCanvas = std::make_unique<SpectrumCanvas> (*this);
    processView = std::make_unique<ProcessView> (*this);
    formulaView = std::make_unique<FormulaView> (*this);
    layersView = std::make_unique<LayersView> (*this);
    thumbnailsStrip = std::make_unique<ThumbnailsStrip> (*this);
    importSheet = std::make_unique<ImportSheet> (*this);
    importSheet->setVisible (false);

    addAndMakeVisible (*waveCanvas);
    addChildComponent (*spectrumCanvas);
    addChildComponent (*processView);
    addChildComponent (*formulaView);
    addChildComponent (*layersView);
    addAndMakeVisible (*thumbnailsStrip);
    addChildComponent (*importSheet);

    updateHeaderInfo();
}

void WavetableEditorSheet::setCurrentFrame (size_t index)
{
    if (index < wt.numFrames())
    {
        currentFrameIndex = index;
        updateHeaderInfo();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    }
}

void WavetableEditorSheet::setMode (Mode newMode)
{
    currentMode = newMode;
    waveTabBtn.setToggleState (newMode == Mode::wave, juce::dontSendNotification);
    spectrumTabBtn.setToggleState (newMode == Mode::spectrum, juce::dontSendNotification);
    processTabBtn.setToggleState (newMode == Mode::process, juce::dontSendNotification);
    formulaTabBtn.setToggleState (newMode == Mode::formula, juce::dontSendNotification);
    layersTabBtn.setToggleState (newMode == Mode::layers, juce::dontSendNotification);

    const bool isWide = (getWidth() >= 600);

    if (waveCanvas)
        waveCanvas->setVisible (newMode == Mode::wave || isWide);
    if (spectrumCanvas)
        spectrumCanvas->setVisible (newMode == Mode::spectrum || isWide);
    if (processView)
        processView->setVisible (newMode == Mode::process && ! isWide);
    if (formulaView)
        formulaView->setVisible (newMode == Mode::formula && ! isWide);
    if (layersView)
        layersView->setVisible (newMode == Mode::layers && ! isWide);

    resized();
}

void WavetableEditorSheet::commitPendingUndo()
{
    if (undoPending)
    {
        undoStack.push (wt);
        undoPending = false;
    }
}

void WavetableEditorSheet::pushUndo()
{
    commitPendingUndo();
    undoPending = true;
    isModified = true;
    statusMessage = {};
    updateHeaderInfo();
}

void WavetableEditorSheet::performUndo()
{
    commitPendingUndo();
    if (undoStack.canUndo())
    {
        undoStack.undo (wt);
        if (currentFrameIndex >= wt.numFrames())
            currentFrameIndex = wt.numFrames() - 1;
        updateHeaderInfo();
        repaint();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    }
}

void WavetableEditorSheet::performRedo()
{
    commitPendingUndo();
    if (undoStack.canRedo())
    {
        undoStack.redo (wt);
        if (currentFrameIndex >= wt.numFrames())
            currentFrameIndex = wt.numFrames() - 1;
        updateHeaderInfo();
        repaint();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    }
}

void WavetableEditorSheet::layoutToolButtons (juce::Rectangle<int>& area)
{
    // 13 drawing tools + the grid button wrap into as many rows as the width needs, every button at least a touch target wide
    juce::Component* const buttons[] = { &toolFlatBtn, &toolSineBtn, &toolSlopeUpBtn, &toolSlopeDownBtn, &toolCurveUpBtn, &toolCurveDownBtn,
                                         &toolHalfSinePeakBtn, &toolHalfSineValleyBtn, &toolInterpLinBtn, &toolInterpCurvedBtn,
                                         &toolNudgeBtn, &toolNoiseBtn, &toolMirrorBtn, &gridBtn };
    constexpr int count = static_cast<int> (sizeof (buttons) / sizeof (buttons[0]));
    const int touch = ctx.theme.touch();
    const int perRow = juce::jlimit (1, count, area.getWidth() / touch);
    const int rows = (count + perRow - 1) / perRow;
    const int rowLength = (count + rows - 1) / rows;

    for (int row = 0; row < rows; ++row)
    {
        auto rowArea = area.removeFromTop (touch);
        const int inRow = juce::jmin (rowLength, count - row * rowLength);
        const int w = rowArea.getWidth() / juce::jmax (1, inRow);
        for (int i = 0; i < inRow; ++i)
            buttons[row * rowLength + i]->setBounds (i == inRow - 1 ? rowArea : rowArea.removeFromLeft (w));
    }
}

void WavetableEditorSheet::updateHeaderInfo()
{
    titleLabel.setText ("OSC " + juce::String (oscIndex) + "  \"" + juce::String (wt.name()) + "\"" + (isModified ? " *" : "")
                            + (statusMessage.isNotEmpty() ? "  -  " + statusMessage : juce::String()),
                        juce::dontSendNotification);
    titleLabel.setFont (ctx.theme.titleFont());
    titleLabel.setColour (juce::Label::textColourId, ctx.theme.colours.text);

    framePosLabel.setText ("FRAME [ " + juce::String (currentFrameIndex + 1) + " / " +
                           juce::String (wt.numFrames()) + " ]",
                           juce::dontSendNotification);
    framePosLabel.setFont (ctx.theme.valueFont());
    framePosLabel.setColour (juce::Label::textColourId, ctx.theme.colours.text);

    undoBtn.setEnabled (canUndo());
    redoBtn.setEnabled (canRedo());
    pasteBtn.setEnabled (hasClipboard);
    morphMenuBtn.setEnabled (wt.numFrames() > 1 && wt.numFrames() < wtedit::kMaxFrames);
    sortMenuBtn.setEnabled (wt.numFrames() > 1);
}

void WavetableEditorSheet::copyCurrentFrame()
{
    if (currentFrameIndex < wt.numFrames())
    {
        clipboardFrame = wt.getFrame (currentFrameIndex);
        hasClipboard = true;
        updateHeaderInfo();
    }
}

void WavetableEditorSheet::pasteFrame()
{
    if (hasClipboard)
    {
        pushUndo();
        if (currentFrameIndex < wt.numFrames())
            wt.setFrame (currentFrameIndex, clipboardFrame);
        else
            wt.insertFrame (wt.numFrames(), clipboardFrame);

        updateHeaderInfo();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    }
}

void WavetableEditorSheet::removeCurrentFrame()
{
    if (wt.numFrames() > 1)
    {
        pushUndo();
        wtedit::ops::removeFrame (wt, currentFrameIndex);
        if (currentFrameIndex >= wt.numFrames())
            currentFrameIndex = wt.numFrames() - 1;
        updateHeaderInfo();
        repaint();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    }
}

void WavetableEditorSheet::insertNewFrame()
{
    if (wt.numFrames() < wtedit::kMaxFrames)
    {
        pushUndo();
        wt.duplicateFrame (currentFrameIndex);
        setCurrentFrame (currentFrameIndex + 1);
    }
}

void WavetableEditorSheet::sortFrames (wtedit::SortMode sortMode)
{
    if (wt.numFrames() > 1)
    {
        pushUndo();
        wtedit::ops::sortFrames (wt, sortMode);
        updateHeaderInfo();
        repaint();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    }
}

void WavetableEditorSheet::morphFrames (wtedit::MorphMode morphMode)
{
    if (wt.numFrames() > 1 && wt.numFrames() < wtedit::kMaxFrames)
    {
        pushUndo();
        std::vector<size_t> kfIndices;
        kfIndices.push_back (0);
        kfIndices.push_back (wt.numFrames() - 1);
        wtedit::ops::interpolateBetweenKeyframes (wt, kfIndices, morphMode);
        updateHeaderInfo();
        repaint();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    }
}

void WavetableEditorSheet::applyToolAt (wtedit::DrawingTool tool, int gridSize, int stepIndex, float param)
{
    if (! strokeActive)
        pushUndo(); // a single call outside a mouse gesture is its own undo step
    if (currentFrameIndex < wt.numFrames())
    {
        wtedit::applyDrawingTool (wt.getFrame (currentFrameIndex), tool, gridSize, stepIndex, param);
        isModified = true;
        updateHeaderInfo();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
    }
}

bool WavetableEditorSheet::applyFormula (const std::string& expression, std::string* errorText)
{
    formula::Formula f;
    const auto err = formula::Formula::compile (expression, f);
    if (! err.ok)
    {
        if (errorText)
            *errorText = err.message;
        return false;
    }

    pushUndo();
    if (f.isMultiFrame())
    {
        std::vector<float> buffer (wt.numFrames() * wtedit::kWaveformSize);
        formula::renderFrames (f, static_cast<int> (wt.numFrames()), buffer.data());
        for (size_t i = 0; i < wt.numFrames(); ++i)
        {
            auto& frame = wt.getFrame (i);
            std::copy_n (buffer.data() + i * wtedit::kWaveformSize, wtedit::kWaveformSize, frame.sampleData());
        }
    }
    else
    {
        if (currentFrameIndex < wt.numFrames())
        {
            auto& frame = wt.getFrame (currentFrameIndex);
            for (size_t s = 0; s < wtedit::kWaveformSize; ++s)
            {
                const double x = (static_cast<double> (s) / static_cast<double> (wtedit::kWaveformSize - 1)) * 2.0 - 1.0;
                frame[s] = f.evaluate (x, static_cast<int> (currentFrameIndex), static_cast<int> (wt.numFrames()));
            }
        }
    }

    isModified = true;
    updateHeaderInfo();
    repaint();
    if (waveCanvas)
        waveCanvas->repaint();
    if (spectrumCanvas)
        spectrumCanvas->repaint();
    if (thumbnailsStrip)
        thumbnailsStrip->repaint();
    return true;
}

bool WavetableEditorSheet::executeProcessCommand (const std::string& commandName, bool allFrames, int gridSize)
{
    pushUndo();

    if (commandName == "Normalize")
    {
        if (allFrames)
            wtedit::ops::normaliseEach (wt);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::normalise (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Remove DC Offset")
    {
        if (allFrames)
            wtedit::ops::removeDcAll (wt);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::removeDc (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Flip Vertical")
    {
        if (allFrames)
            wtedit::ops::flipVerticalAll (wt);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::flipVertical (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Flip Horizontal")
    {
        if (allFrames)
            wtedit::ops::flipHorizontalAll (wt);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::flipHorizontal (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Fade Edges")
    {
        if (allFrames)
            wtedit::ops::fadeEdgesAll (wt, gridSize);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::fadeEdges (wt.getFrame (currentFrameIndex), gridSize);
    }
    else if (commandName == "Crossfade Edges")
    {
        if (allFrames)
            wtedit::ops::crossfadeEdgesAll (wt, gridSize);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::crossfadeEdges (wt.getFrame (currentFrameIndex), gridSize);
    }
    else if (commandName == "X-Fade 16 Samples")
    {
        if (allFrames)
            wtedit::ops::crossfadeEdges16SamplesAll (wt);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::crossfadeEdges16Samples (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Shift to Zero Crossing")
    {
        if (allFrames)
            wtedit::ops::shiftToZeroCrossingAll (wt);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::shiftToZeroCrossing (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Filter Low Pass")
    {
        if (allFrames)
            wtedit::ops::filterLowPassAll (wt, gridSize);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::filterLowPass (wt.getFrame (currentFrameIndex), gridSize);
    }
    else if (commandName == "Sample Redux")
    {
        if (allFrames)
            wtedit::ops::sampleReduxAll (wt, gridSize);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::sampleRedux (wt.getFrame (currentFrameIndex), gridSize);
    }
    else if (commandName == "Remove Fundamental")
    {
        if (allFrames)
            wtedit::ops::removeFundamentalAll (wt);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::removeFundamental (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Remove Low Spectra")
    {
        if (allFrames)
            wtedit::ops::removeLowSpectraAll (wt, gridSize);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::removeLowSpectra (wt.getFrame (currentFrameIndex), gridSize);
    }
    else if (commandName == "Remove Low Phases")
    {
        if (allFrames)
            wtedit::ops::removeLowPhasesAll (wt, gridSize);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::removeLowPhases (wt.getFrame (currentFrameIndex), gridSize);
    }
    else if (commandName == "Nudge Fundamental Phase")
    {
        if (allFrames)
            wtedit::ops::nudgeFundamentalPhaseTo50PercentAll (wt);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::nudgeFundamentalPhaseTo50Percent (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Set Spectra to All")
    {
        wtedit::ops::setSpectraThisFrameToAll (wt, currentFrameIndex);
    }
    else if (commandName == "Set Phases to All")
    {
        wtedit::ops::setPhasesThisFrameToAll (wt, currentFrameIndex);
    }
    else if (commandName == "Blur Spectra Adjacent Bins")
    {
        if (allFrames)
            wtedit::ops::blurSpectraAdjacentBinsAll (wt, gridSize);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::blurSpectraAdjacentBins (wt.getFrame (currentFrameIndex), gridSize);
    }
    else if (commandName == "Blur Phases Adjacent Bins")
    {
        if (allFrames)
            wtedit::ops::blurPhasesAdjacentBinsAll (wt, gridSize);
        else if (currentFrameIndex < wt.numFrames())
            wtedit::ops::blurPhasesAdjacentBins (wt.getFrame (currentFrameIndex), gridSize);
    }
    else if (commandName == "Blur Spectra Frames")
    {
        wtedit::ops::blurSpectraAdjacentFrames (wt, gridSize);
    }
    else if (commandName == "Blur Phases Frames")
    {
        wtedit::ops::blurPhasesAdjacentFrames (wt, gridSize);
    }
    else if (commandName == "Resize Tables Half")
    {
        wtedit::ops::resizeTablesHalf (wt);
    }
    else if (commandName == "Resize Tables Double")
    {
        wtedit::ops::resizeTablesDouble (wt);
    }
    else if (commandName == "Create PWM Sweep")
    {
        wtedit::ops::createPwmFromFrame (wt, currentFrameIndex);
    }
    else if (commandName == "Init Silence")
    {
        if (currentFrameIndex < wt.numFrames())
            wtedit::ops::initSilence (wt.getFrame (currentFrameIndex));
    }
    else if (commandName == "Init Default")
    {
        wtedit::ops::initAllDefault (wt);
        currentFrameIndex = 0;
    }

    isModified = true;
    updateHeaderInfo();
    repaint();
    if (waveCanvas)
        waveCanvas->repaint();
    if (spectrumCanvas)
        spectrumCanvas->repaint();
    if (thumbnailsStrip)
        thumbnailsStrip->repaint();
    return true;
}

void WavetableEditorSheet::renderLayersToTable()
{
    vitalGroup.renderWavetable (wt, wt.numFrames());
    isModified = true;
    updateHeaderInfo();
    repaint();
    if (waveCanvas)
        waveCanvas->repaint();
    if (spectrumCanvas)
        spectrumCanvas->repaint();
    if (thumbnailsStrip)
        thumbnailsStrip->repaint();
}

bool WavetableEditorSheet::importAudioData (const float* samples, size_t numSamples, double sampleRate, const wtedit::ImportOptions& opts)
{
    wtedit::Wavetable newWt ("Imported Audio");
    if (wtedit::importAudioSamples (samples, numSamples, sampleRate, opts, newWt))
    {
        pushUndo();
        wt = std::move (newWt);
        currentFrameIndex = 0;
        vitalGroup.initFromWavetable (wt);
        isModified = true;
        updateHeaderInfo();
        repaint();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
        return true;
    }
    return false;
}

bool WavetableEditorSheet::importImageData (const uint8_t* grayscale, int width, int height)
{
    wtedit::Wavetable newWt ("Imported Image");
    if (wtedit::importImageGrayscale (grayscale, width, height, newWt))
    {
        pushUndo();
        wt = std::move (newWt);
        currentFrameIndex = 0;
        vitalGroup.initFromWavetable (wt);
        isModified = true;
        updateHeaderInfo();
        repaint();
        if (waveCanvas)
            waveCanvas->repaint();
        if (spectrumCanvas)
            spectrumCanvas->repaint();
        if (thumbnailsStrip)
            thumbnailsStrip->repaint();
        return true;
    }
    return false;
}

void WavetableEditorSheet::showOptionsMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Import Audio / Image / Wavetable...");
    menu.addItem (2, "Export WAV (16-bit PCM)");
    menu.addItem (3, "Export WAV (32-bit Float)");
    menu.addItem (4, "Export Single-Cycle Frame");
    menu.addItem (5, "Save Table");
    menu.addItem (6, "Embed in Preset");
    menu.addSeparator();
    menu.addItem (7, "Move frame left", currentFrameIndex > 0);
    menu.addItem (8, "Move frame right", currentFrameIndex + 1 < wt.numFrames());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuBtn),
        [this] (int result)
        {
            switch (result)
            {
                case 1: showImportDialog(); break;
                case 2: exportWavPrompt (wtedit::ExportFormat::Wav16Bit); break;
                case 3: exportWavPrompt (wtedit::ExportFormat::Wav32BitFloat); break;
                case 4: exportWavPrompt (wtedit::ExportFormat::SingleCycle); break;
                case 5: saveToLibrary (false); break;
                case 6: saveToLibrary (true); break;
                case 7: moveFrameTo (currentFrameIndex, currentFrameIndex - 1); break; // the visible alternative of the thumbnail drag
                case 8: moveFrameTo (currentFrameIndex, currentFrameIndex + 1); break;
                default: break;
            }
        });
}

void WavetableEditorSheet::showRemoveMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Init All (Saw)");
    menu.addItem (2, "Remove Current Frame");
    menu.addItem (3, "Remove Selected Range");
    menu.addItem (4, "Remove Beginning to Selected");
    menu.addItem (5, "Remove Selected to End");
    menu.addItem (6, "Remove All Except Selected (Crop)");
    menu.addItem (7, "Reduce to Every 2nd Frame");
    menu.addItem (8, "Reduce to Every 4th Frame");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (removeMenuBtn),
        [this] (int result)
        {
            if (result <= 0)
                return;
            pushUndo();

            switch (result)
            {
                case 1:
                    wtedit::ops::initAllDefault (wt);
                    currentFrameIndex = 0;
                    break;
                case 2:
                    wtedit::ops::removeFrame (wt, currentFrameIndex);
                    if (currentFrameIndex >= wt.numFrames())
                        currentFrameIndex = wt.numFrames() - 1;
                    break;
                case 3:
                    if (rangeStart >= 0 && rangeEnd >= 0)
                        wtedit::ops::removeRange (wt, static_cast<size_t> (rangeStart), static_cast<size_t> (rangeEnd));
                    break;
                case 4:
                    wtedit::ops::removeBeginningToSelected (wt, currentFrameIndex);
                    currentFrameIndex = 0;
                    break;
                case 5:
                    wtedit::ops::removeSelectedToEnd (wt, currentFrameIndex);
                    if (currentFrameIndex >= wt.numFrames())
                        currentFrameIndex = wt.numFrames() - 1;
                    break;
                case 6:
                    if (rangeStart >= 0 && rangeEnd >= 0)
                    {
                        wtedit::ops::removeAllExceptSelected (wt, static_cast<size_t> (rangeStart), static_cast<size_t> (rangeEnd));
                        currentFrameIndex = 0;
                    }
                    break;
                case 7:
                    wtedit::ops::reduceTo (wt, 2);
                    if (currentFrameIndex >= wt.numFrames())
                        currentFrameIndex = wt.numFrames() - 1;
                    break;
                case 8:
                    wtedit::ops::reduceTo (wt, 4);
                    if (currentFrameIndex >= wt.numFrames())
                        currentFrameIndex = wt.numFrames() - 1;
                    break;
                default: break;
            }

            updateHeaderInfo();
            repaint();
            if (waveCanvas)
                waveCanvas->repaint();
            if (spectrumCanvas)
                spectrumCanvas->repaint();
            if (thumbnailsStrip)
                thumbnailsStrip->repaint();
        });
}

void WavetableEditorSheet::showSortMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Peak Spectrum");
    menu.addItem (2, "Average Spectrum");
    menu.addItem (3, "Peak Amount");
    menu.addItem (4, "Num with Spectrum");
    menu.addItem (5, "Highest with Spectrum");
    menu.addItem (6, "Fundamental Amount");
    menu.addItem (7, "Randomize");
    menu.addItem (8, "Reverse Entire Table Order");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sortMenuBtn),
        [this] (int result)
        {
            switch (result)
            {
                case 1: sortFrames (wtedit::SortMode::PeakSpect); break;
                case 2: sortFrames (wtedit::SortMode::AverageSpect); break;
                case 3: sortFrames (wtedit::SortMode::PeakAmount); break;
                case 4: sortFrames (wtedit::SortMode::NumWithSpect); break;
                case 5: sortFrames (wtedit::SortMode::HighestWithSpect); break;
                case 6: sortFrames (wtedit::SortMode::FundamentalAmount); break;
                case 7: sortFrames (wtedit::SortMode::Randomize); break;
                case 8: sortFrames (wtedit::SortMode::Reverse); break;
                default: break;
            }
        });
}

void WavetableEditorSheet::showMorphMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Crossfade");
    menu.addItem (2, "Spectral");
    menu.addItem (3, "Spectral (Zero Fund Phase)");
    menu.addItem (4, "Spectral (Zero All Phases)");
    menu.addItem (5, "Remove Morph Tables");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (morphMenuBtn),
        [this] (int result)
        {
            switch (result)
            {
                case 1: morphFrames (wtedit::MorphMode::Crossfade); break;
                case 2: morphFrames (wtedit::MorphMode::Spectral); break;
                case 3: morphFrames (wtedit::MorphMode::SpectralZeroFundPhase); break;
                case 4: morphFrames (wtedit::MorphMode::SpectralZeroAllPhases); break;
                case 5:
                    if (wt.numFrames() > 2)
                    {
                        pushUndo();
                        std::vector<size_t> kfs { 0, wt.numFrames() - 1 };
                        wtedit::ops::removeMorphTables (wt, kfs);
                        updateHeaderInfo();
                        repaint();
                    }
                    break;
                default: break;
            }
        });
}

void WavetableEditorSheet::showImportDialog()
{
    if (importSheet)
    {
        importSheet->setBounds (getLocalBounds().reduced (ctx.theme.gap() * 2));
        importSheet->setVisible (true);
        importSheet->toFront (true);
    }
}

void WavetableEditorSheet::exportWavPrompt (wtedit::ExportFormat format)
{
    FileRequest request;
    request.mode = FileRequest::Mode::save;
    request.title = format == wtedit::ExportFormat::SingleCycle ? "Export the frame as a single cycle WAV" : "Export the wavetable as a WAV file";
    request.wildcards = "*.wav";
    request.purpose = "export";
    request.suggestedName = juce::String (wt.name()).replaceCharacters ("/\\:", "___") + ".wav";
    if (! ctx.host.requestFile (request, [safe = juce::Component::SafePointer<WavetableEditorSheet> (this), format] (const juce::File& file)
        {
            if (safe != nullptr)
                safe->exportToFile (format, file.hasFileExtension ("wav") ? file : file.withFileExtension ("wav"));
        }))
        say ("This host cannot ask for a file");
}

bool WavetableEditorSheet::exportToFile (wtedit::ExportFormat format, const juce::File& file)
{
    wtedit::ExportOptions opts;
    opts.format = format;
    opts.singleFrameIndex = currentFrameIndex;
    std::vector<uint8_t> wavBytes;
    if (! wtedit::exportWav (wt, opts, wavBytes) || ! writeFileBytes (file, wavBytes))
    {
        say ("Export failed: " + file.getFileName());
        return false;
    }
    say ("Exported " + file.getFileName());
    return true;
}

void WavetableEditorSheet::say (const juce::String& text)
{
    statusMessage = text;
    updateHeaderInfo();
}

void WavetableEditorSheet::setTargetOsc (int osc)
{
    oscIndex = juce::jlimit (1, kMaxContentWavetables, osc);
    updateHeaderInfo();
}

FlowResult WavetableEditorSheet::saveToLibrary (bool embed)
{
    commitPendingUndo();
    const FlowResult result = saveWavetable (ctx, oscIndex, wt, embed);
    if (result.ok)
    {
        isModified = false;
        if (onSaved)
            onSaved();
    }
    say (result.message);
    return result;
}

void WavetableEditorSheet::replaceTable (wtedit::Wavetable newTable)
{
    pushUndo();
    wt = std::move (newTable);
    currentFrameIndex = 0;
    vitalGroup.initFromWavetable (wt);
    isModified = true;
    refreshAll();
}

bool WavetableEditorSheet::importFile (const juce::File& file, ImportKind kind, const wtedit::ImportOptions& options)
{
    lastImportFile = file;
    switch (kind)
    {
        case ImportKind::audio:
        {
            DecodedAudio audio;
            juce::String error;
            if (! decodeAudioFile (file, audio, &error))
            {
                say ("Import failed: " + error);
                return false;
            }
            wtedit::ImportOptions opts = options;
            const std::vector<float>& channel = (opts.channel == 1 && ! audio.right.empty()) ? audio.right : audio.left;
            if (! importAudioAsync (channel, audio.sampleRate, opts))
            {
                say ("Import failed: an import is already running");
                return false;
            }
            return true;
        }
        case ImportKind::image:
        {
            juce::Image image = juce::ImageFileFormat::loadFrom (file);
            if (! image.isValid())
            {
                say ("Import failed: not an image this build can read");
                return false;
            }
            if (image.getWidth() > 256 || image.getHeight() > 1024)
                image = image.rescaled (juce::jmin (256, image.getWidth()), juce::jmin (1024, image.getHeight()));
            std::vector<uint8_t> gray (static_cast<size_t> (image.getWidth()) * static_cast<size_t> (image.getHeight()));
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                {
                    const auto c = image.getPixelAt (x, y);
                    gray[static_cast<size_t> (y) * static_cast<size_t> (image.getWidth()) + static_cast<size_t> (x)] =
                        static_cast<uint8_t> (juce::roundToInt (0.299f * c.getFloatRed() * 255.0f + 0.587f * c.getFloatGreen() * 255.0f + 0.114f * c.getFloatBlue() * 255.0f));
                }
            pushUndo();
            if (! importImageData (gray.data(), image.getWidth(), image.getHeight()))
            {
                say ("Import failed: the image gives no table");
                return false;
            }
            return true;
        }
        case ImportKind::wavetable:
        {
            wtedit::Wavetable table (file.getFileNameWithoutExtension().toStdString());
            std::vector<uint8_t> bytes = readFileBytes (file);
            if (file.hasFileExtension ("vitaltable"))
            {
                auto* library = ctx.host.contentLibrary();
                if (library == nullptr)
                {
                    say ("This host keeps no content library");
                    return false;
                }
                const auto imported = importVitalFileForPlugin (file.getFileName().toStdString(), bytes, *library, ctx.host.userDirectory(), ctx.host.engineCaps());
                bytes.clear();
                if (! imported.ok || ! library->readFile (imported.tableRef, bytes))
                {
                    say ("Import failed: " + juce::String (imported.error));
                    return false;
                }
            }
            if (bytes.empty() || ! wavetableFromWav (bytes, table.name(), table))
            {
                DecodedAudio audio; // a wavetable in a format other than WAV: its samples are cut into 2048 sample cycles
                juce::String error;
                if (! decodeAudioFile (file, audio, &error))
                {
                    say ("Import failed: " + error);
                    return false;
                }
                const std::vector<uint8_t> wav = encodeWav (audio.left.data(), nullptr, audio.left.size(), juce::roundToInt (audio.sampleRate), 16);
                if (! wavetableFromWav (wav, table.name(), table))
                {
                    say ("Import failed: no table in the file");
                    return false;
                }
            }
            replaceTable (std::move (table));
            say ("Imported " + file.getFileName());
            return true;
        }
    }
    return false;
}

void WavetableEditorSheet::chooseImportFile (const juce::File& file, ImportKind kind)
{
    if (importSheet == nullptr)
        return;
    showImportDialog();
    importSheet->selectKind (kind);
    importSheet->setChosen (file);
}

void WavetableEditorSheet::pressImport()
{
    if (importSheet != nullptr)
        importSheet->pressImport();
}

LibraryMenu* WavetableEditorSheet::formulaLibraryMenu() noexcept
{
    if (formulaView == nullptr)
        return nullptr;
    formulaView->openLibrary();
    return formulaView->library();
}

bool WavetableEditorSheet::applyFormulaItem (const std::string& json)
{
    return formulaView != nullptr && formulaView->setFormulaFromItem (json);
}

WavetableEditorSheet& WavetableEditorSheet::open (UiContext& context, int osc, juce::Component& anchor, std::unique_ptr<WavetableEditorSheet>& holder,
                                                   const wtedit::Wavetable& builtIn, std::function<void(const wtedit::Wavetable&)> onClosedCallback,
                                                   std::function<void()> onSavedCallback)
{
    // unsaved edits survive closing the sheet; a saved (or never edited) sheet starts again from the table the oscillator really plays
    if (holder == nullptr || holder->targetOsc() != osc || ! holder->modified())
    {
        wtedit::Wavetable initial = builtIn;
        wtedit::Wavetable stored;
        if (loadWavetableOf (context.host, osc, stored)
            && juce::roundToInt (readParameter (context.host, ("osc" + std::to_string (osc) + ".wavetable").c_str())) == choiceIndexOf (("osc" + std::to_string (osc) + ".wavetable").c_str(), "Preset"))
            initial = std::move (stored);
        holder = std::make_unique<WavetableEditorSheet> (context, std::move (initial));
        holder->setTargetOsc (osc);
    }
    holder->onClosed = std::move (onClosedCallback);
    holder->onSaved = std::move (onSavedCallback);
    auto* top = anchor.getTopLevelComponent();
    juce::Component& parent = top != nullptr ? *top : anchor;
    parent.addAndMakeVisible (*holder);
    holder->setBounds (parent.getLocalBounds());
    holder->setVisible (true);
    holder->toFront (true);
    return *holder;
}

void WavetableEditorSheet::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme;
    g.fillAll (t.colours.background);
    // the grabber: a swipe down on the header closes the sheet
    g.setColour (t.colours.outline);
    g.fillRoundedRectangle (juce::Rectangle<float> (static_cast<float> (getWidth()) * 0.5f - t.px (20.0f), t.px (2.0f), t.px (40.0f), t.px (3.0f)),
                            t.px (1.5f));
}

void WavetableEditorSheet::resized()
{
    const auto& t = ctx.theme;
    const int touch = t.touch();
    const int gap = t.gap();
    auto r = getLocalBounds().reduced (gap);

    // 1. Header (height = touch)
    auto header = r.removeFromTop (touch);
    closeBtn.setBounds (header.removeFromLeft (touch));
    header.removeFromLeft (gap);
    menuBtn.setBounds (header.removeFromRight (touch));
    header.removeFromRight (gap);
    redoBtn.setBounds (header.removeFromRight (juce::jmax (touch, t.ipx (60))));
    header.removeFromRight (gap);
    undoBtn.setBounds (header.removeFromRight (juce::jmax (touch, t.ipx (60))));
    header.removeFromRight (gap);
    titleLabel.setBounds (header);

    r.removeFromTop (gap);

    // 2. Mode tabs (height = touch)
    auto tabs = r.removeFromTop (touch);
    const int numTabs = 5;
    const int tabW = juce::jmax (touch, (tabs.getWidth() - (numTabs - 1) * gap) / numTabs);
    waveTabBtn.setBounds (tabs.removeFromLeft (tabW));
    tabs.removeFromLeft (gap);
    spectrumTabBtn.setBounds (tabs.removeFromLeft (tabW));
    tabs.removeFromLeft (gap);
    processTabBtn.setBounds (tabs.removeFromLeft (tabW));
    tabs.removeFromLeft (gap);
    formulaTabBtn.setBounds (tabs.removeFromLeft (tabW));
    tabs.removeFromLeft (gap);
    layersTabBtn.setBounds (tabs);

    r.removeFromTop (gap);

    // 3. Bottom Frame Strip & Controls (reserved from bottom)
    const int thumbH = juce::jmax (touch, t.ipx (48));
    auto bottomArea = r.removeFromBottom (thumbH + touch + gap * 2);
    auto frameRow = bottomArea.removeFromTop (touch);
    prevFrameBtn.setBounds (frameRow.removeFromLeft (touch));
    frameRow.removeFromLeft (gap);
    nextFrameBtn.setBounds (frameRow.removeFromLeft (touch));
    frameRow.removeFromLeft (gap);
    framePosLabel.setBounds (frameRow.removeFromLeft (juce::jmax (touch * 2, t.ipx (140))));
    frameRow.removeFromLeft (gap);

    const int actionW = juce::jmax (touch, t.ipx (60));
    copyBtn.setBounds (frameRow.removeFromLeft (actionW));
    frameRow.removeFromLeft (gap);
    pasteBtn.setBounds (frameRow.removeFromLeft (actionW));
    frameRow.removeFromLeft (gap);
    rangeToggleBtn.setBounds (frameRow.removeFromLeft (actionW));
    frameRow.removeFromLeft (gap);

    const int menuW = juce::jmax (touch, t.ipx (70));
    removeMenuBtn.setBounds (frameRow.removeFromLeft (menuW));
    frameRow.removeFromLeft (gap);
    sortMenuBtn.setBounds (frameRow.removeFromLeft (menuW));
    frameRow.removeFromLeft (gap);
    morphMenuBtn.setBounds (frameRow);

    bottomArea.removeFromTop (gap);
    if (thumbnailsStrip)
        thumbnailsStrip->setBounds (bottomArea);

    r.removeFromBottom (gap);

    // 4. Middle Canvas Area
    const bool isWide = (getWidth() >= 600);

    if (isWide)
    {
        // Tablet / Landscape side-by-side
        const int halfW = (r.getWidth() - gap) / 2;
        auto leftArea = r.removeFromLeft (halfW);
        auto rightArea = r;

        // Tool strip on left top
        layoutToolButtons (leftArea);

        leftArea.removeFromTop (gap);
        if (waveCanvas)
            waveCanvas->setBounds (leftArea);

        if (spectrumCanvas)
            spectrumCanvas->setBounds (rightArea);
    }
    else
    {
        // Phone Portrait layout
        if (currentMode == Mode::wave)
        {
            layoutToolButtons (r);
            r.removeFromTop (gap);
            if (waveCanvas)
                waveCanvas->setBounds (r);
        }
        else if (currentMode == Mode::spectrum)
        {
            if (spectrumCanvas)
                spectrumCanvas->setBounds (r);
        }
        else if (currentMode == Mode::process)
        {
            if (processView)
                processView->setBounds (r);
        }
        else if (currentMode == Mode::formula)
        {
            if (formulaView)
                formulaView->setBounds (r);
        }
        else if (currentMode == Mode::layers)
        {
            if (layersView)
                layersView->setBounds (r);
        }
    }

    if (importSheet && importSheet->isVisible())
        importSheet->setBounds (getLocalBounds().reduced (gap * 2));
}


//==============================================================================
// Gestures of the audit gap closure
//==============================================================================
juce::Component* WavetableEditorSheet::waveCanvasComponent() noexcept { return waveCanvas.get(); }
juce::Component* WavetableEditorSheet::spectrumComponent() noexcept { return spectrumCanvas.get(); }
juce::Component* WavetableEditorSheet::thumbnailsComponent() noexcept { return thumbnailsStrip.get(); }
juce::Component* WavetableEditorSheet::importSheetComponent() noexcept { return importSheet.get(); }
LongPressDetector* WavetableEditorSheet::waveLongPress() noexcept { return waveCanvas != nullptr ? &waveCanvas->longPress() : nullptr; }
LongPressDetector* WavetableEditorSheet::thumbnailLongPress() noexcept { return thumbnailsStrip != nullptr ? &thumbnailsStrip->longPress() : nullptr; }
bool WavetableEditorSheet::waveNudgeStroke() const noexcept { return waveCanvas != nullptr && waveCanvas->nudgeActive(); }
bool WavetableEditorSheet::thumbnailReordering() const noexcept { return thumbnailsStrip != nullptr && thumbnailsStrip->isReordering(); }
juce::Button* WavetableEditorSheet::waveZoomButton (int which) noexcept { return waveCanvas != nullptr ? waveCanvas->zoomButton (which) : nullptr; }

void WavetableEditorSheet::zoomWave (float anchor, float factor)
{
    waveVp.zoomAbout (waveVp.toRange (static_cast<double> (juce::jlimit (0.0f, 1.0f, anchor))), static_cast<double> (factor));
    if (waveCanvas)
        waveCanvas->repaint();
}

void WavetableEditorSheet::panWave (float deltaScreenFraction)
{
    waveVp.pan (static_cast<double> (deltaScreenFraction) * waveVp.span);
    if (waveCanvas)
        waveCanvas->repaint();
}

void WavetableEditorSheet::fitWave()
{
    waveVp.fit();
    if (waveCanvas)
        waveCanvas->repaint();
}

void WavetableEditorSheet::handleTouchGesture (const TouchTracker::Gesture& g, bool fromWaveCanvas)
{
    switch (g.kind)
    {
        case TouchTracker::Kind::twoFingerTap:
            performUndo(); // the visible alternative is the Undo button
            break;
        case TouchTracker::Kind::pinch:
            if (fromWaveCanvas && waveCanvas != nullptr)
                zoomWave (g.centre.x / static_cast<float> (juce::jmax (1, waveCanvas->getWidth())), g.scale);
            break;
        case TouchTracker::Kind::pan:
            if (fromWaveCanvas && waveCanvas != nullptr)
                panWave (-g.dx / static_cast<float> (juce::jmax (1, waveCanvas->getWidth())));
            break;
        case TouchTracker::Kind::twoFingerSwipe:
        case TouchTracker::Kind::none:
            break;
    }
}

void WavetableEditorSheet::beginStroke()
{
    strokeBackup = std::make_unique<wtedit::Wavetable> (wt);
}

void WavetableEditorSheet::endStroke()
{
    strokeActive = false;
    strokeBackup.reset();
}

void WavetableEditorSheet::restoreStrokeBackup()
{
    if (strokeBackup != nullptr)
    {
        wt = *strokeBackup;
        if (currentFrameIndex >= wt.numFrames())
            currentFrameIndex = wt.numFrames() - 1;
        refreshAll();
    }
}

void WavetableEditorSheet::cancelStroke()
{
    if (! strokeActive)
        return;
    restoreStrokeBackup();
    undoPending = false; // the state is the recorded one again: nothing to undo, nothing to redo
    strokeActive = false;
    strokeBackup.reset();
    hideLoupe();
    updateHeaderInfo();
}

void WavetableEditorSheet::showLoupeAt (juce::Component& source, juce::Point<int> point)
{
    if (loupe == nullptr)
        loupe = std::make_unique<Loupe> (ctx.theme);
    loupe->show (source, point);
}

void WavetableEditorSheet::hideLoupe()
{
    if (loupe != nullptr)
        loupe->hide();
}

void WavetableEditorSheet::refreshAll()
{
    updateHeaderInfo();
    repaint();
    if (waveCanvas)
        waveCanvas->repaint();
    if (spectrumCanvas)
        spectrumCanvas->repaint();
    if (thumbnailsStrip)
        thumbnailsStrip->repaint();
}

void WavetableEditorSheet::setNudgeAllPhases (bool on)
{
    nudgeAll = on;
    if (spectrumCanvas)
        spectrumCanvas->setNudgeToggle (on);
}

void WavetableEditorSheet::applyBinEdit (int bin, bool phase, float value)
{
    constexpr int kHalf = static_cast<int> (wtedit::kWaveformSize / 2);
    if (currentFrameIndex >= wt.numFrames() || bin < 1 || bin >= kHalf)
        return;
    auto& frame = wt.getFrame (currentFrameIndex);
    std::array<float, wtedit::kWaveformSize> mags {};
    std::array<float, wtedit::kWaveformSize> phases {};
    wtedit::Radix2Fft::realToPolar (frame.sampleData(), mags.data(), phases.data(), wtedit::kWaveformSize);

    const auto wrap = [] (float a)
    {
        while (a > kPi)
            a -= 2.0f * kPi;
        while (a < -kPi)
            a += 2.0f * kPi;
        return a;
    };
    if (phase)
    {
        if (nudgeAll)
        {
            // every harmonic that sounds moves by the amount this bar moved
            const float delta = wrap (value - phases[static_cast<size_t> (bin)]);
            for (int k = 1; k < kHalf; ++k)
                if (mags[static_cast<size_t> (k)] > 1.0e-5f)
                    phases[static_cast<size_t> (k)] = wrap (phases[static_cast<size_t> (k)] + delta);
            phases[static_cast<size_t> (bin)] = wrap (value);
        }
        else
            phases[static_cast<size_t> (bin)] = wrap (value);
    }
    else
        mags[static_cast<size_t> (bin)] = juce::jlimit (0.0f, 1.0f, value);

    wtedit::Radix2Fft::polarToReal (mags.data(), phases.data(), frame.sampleData(), wtedit::kWaveformSize);
    isModified = true;
    refreshAll();
}

bool WavetableEditorSheet::enterBinValue (int bin, bool phase, const juce::String& text)
{
    float v = 0.0f;
    if (phase)
    {
        if (! parseBinPhaseDegrees (text.toStdString(), v))
            return false;
        const float rad = (v > 180.0f ? v - 360.0f : v) * kPi / 180.0f;
        pushUndo();
        applyBinEdit (bin, true, rad);
        return true;
    }
    if (! parseBinAmplitude (text.toStdString(), v))
        return false;
    pushUndo();
    applyBinEdit (bin, false, v);
    return true;
}

bool WavetableEditorSheet::moveFrameTo (size_t from, size_t to)
{
    if (from == to || from >= wt.numFrames() || to >= wt.numFrames())
        return false;
    pushUndo();
    if (! wt.moveFrame (from, to))
    {
        undoPending = false;
        return false;
    }
    currentFrameIndex = to;
    rangeStart = rangeEnd = -1;
    refreshAll();
    return true;
}

void WavetableEditorSheet::requestClose()
{
    if (onClosed)
        onClosed (wt);
    setVisible (false);
}

bool WavetableEditorSheet::handleCloseSwipe (juce::Point<float> from, juce::Point<float> to)
{
    const float dy = to.y - from.y, dx = to.x - from.x;
    if (dy >= static_cast<float> (ctx.theme.ipx (80.0f)) && std::abs (dx) * 2.0f < dy)
    {
        requestClose();
        return true;
    }
    return false;
}

void WavetableEditorSheet::mouseDown (const juce::MouseEvent& e)
{
    // the header band (the buttons take their own presses): a finger that goes down here may swipe the sheet away
    closeSwipeArmed = e.position.y < static_cast<float> (ctx.theme.touch() + 2 * ctx.theme.gap());
    closeSwipeFrom = e.position;
}

void WavetableEditorSheet::mouseUp (const juce::MouseEvent& e)
{
    if (closeSwipeArmed)
        handleCloseSwipe (closeSwipeFrom, e.position);
    closeSwipeArmed = false;
}

bool WavetableEditorSheet::importAudioAsync (std::vector<float> samples, double sampleRate, const wtedit::ImportOptions& opts)
{
    if (importJob != nullptr || samples.empty())
        return false;
    importJob = std::make_unique<ImportJob> (std::move (samples), sampleRate, opts);
    importJob->startThread();
    if (importSheet)
        importSheet->setBusy (true);
    startTimerHz (20);
    return true;
}

bool WavetableEditorSheet::importWorkerDone() const noexcept
{
    return importJob != nullptr && importJob->done.load();
}

bool WavetableEditorSheet::finishImport()
{
    if (importJob == nullptr || ! importJob->done.load())
        return false;
    importJob->waitForThreadToExit (-1); // the flag is the last thing run () sets
    const auto job = std::move (importJob);
    importJob = nullptr;
    stopTimer();
    if (importSheet)
        importSheet->setBusy (false);
    if (job->cancelled.load())
        return true; // discarded
    if (! job->ok)
    {
        if (importSheet)
            importSheet->setInfo ("Import failed: no usable audio");
        return true;
    }
    pushUndo();
    wt = job->result;
    currentFrameIndex = 0;
    vitalGroup.initFromWavetable (wt);
    isModified = true;
    refreshAll();
    if (importSheet)
        importSheet->setVisible (false);
    return true;
}

void WavetableEditorSheet::cancelImport()
{
    if (importJob != nullptr)
        importJob->cancelled = true;
}

bool WavetableEditorSheet::importProgressVisible() const
{
    return importSheet != nullptr && importSheet->progressVisible();
}

void WavetableEditorSheet::timerCallback()
{
    if (importJob == nullptr)
    {
        stopTimer();
        return;
    }
    finishImport();
}

} // namespace synthyboy::ui
