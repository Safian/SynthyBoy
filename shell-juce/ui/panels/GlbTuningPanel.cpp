// SynthyBoy - UI panel: Microtonal Tuning (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
#include "../Panel.h"
#include "../ParamHandle.h"

namespace synthyboy::ui
{
namespace
{
/** Theoretical cents deviation from 12-TET for 12 chromatic degrees (C through B). */
constexpr std::array<float, 12> kPythagoreanCents = {
    0.0f, -10.0f, 4.0f, -6.0f, 8.0f, -2.0f, -12.0f, 2.0f, -8.0f, 6.0f, -4.0f, 10.0f
};

constexpr std::array<float, 12> kJustIntonationCents = {
    0.0f, 11.7f, 3.9f, 15.6f, -13.7f, -2.0f, -9.8f, 2.0f, 13.7f, -15.6f, 17.6f, -11.7f
};

const char* const kTuningNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/** Parses Scala (.scl) text format. Returns 12 cents deviations from 12-TET. */
std::array<float, 12> parseScalaCents (const std::string& sclContent)
{
    std::array<float, 12> result {};
    std::istringstream stream (sclContent);
    std::string line;
    std::string description;
    int numNotes = 0;
    bool foundDesc = false;
    bool foundCount = false;
    std::vector<float> scaleCents;

    while (std::getline (stream, line))
    {
        // Trim leading whitespace
        const auto first = line.find_first_not_of (" \t\r\n");
        if (first == std::string::npos || line[first] == '!')
            continue; // comment or empty
        const std::string clean = line.substr (first);

        if (! foundDesc)
        {
            description = clean;
            foundDesc = true;
            continue;
        }
        if (! foundCount)
        {
            try { numNotes = std::stoi (clean); } catch (...) { numNotes = 0; }
            foundCount = true;
            continue;
        }

        // Parse cents or ratio
        if (clean.find ('.') != std::string::npos)
        {
            try { scaleCents.push_back (std::stof (clean)); } catch (...) {}
        }
        else if (const auto slash = clean.find ('/'); slash != std::string::npos)
        {
            try
            {
                const float num = std::stof (clean.substr (0, slash));
                const float den = std::stof (clean.substr (slash + 1));
                if (den > 0.0f && num > 0.0f)
                    scaleCents.push_back (1200.0f * std::log2 (num / den));
            }
            catch (...) {}
        }
        else
        {
            try { scaleCents.push_back (1200.0f * std::log2 (std::stof (clean))); } catch (...) {}
        }
    }

    // Map degrees to 12-TET deviation
    for (size_t i = 0; i < 12; ++i)
    {
        const float tetCents = static_cast<float> (i) * 100.0f;
        if (i == 0)
        {
            result[0] = 0.0f;
        }
        else if (i - 1 < scaleCents.size())
        {
            result[i] = scaleCents[i - 1] - tetCents;
        }
    }
    return result;
}
} // namespace

/** Direct manipulation and monitoring visual for Microtonal Tuning. */
class GlbTuningPitchView final : public juce::Component
{
public:
    explicit GlbTuningPitchView (UiContext& context)
        : ctx (context),
          a4Handle (context, "master.tuning.a4"),
          scaleHandle (context, "master.tuning.scale"),
          mtsEspHandle (context, "master.tuning.mts_esp"),
          mtsNoteOnHandle (context, "master.tuning.mts_note_on_only")
    {
        const auto update = [this] { repaint(); };
        a4Handle.onChange = update;
        scaleHandle.onChange = update;
        mtsEspHandle.onChange = update;
        mtsNoteOnHandle.onChange = update;

        loadButton.setButtonText ("Load .scl / .tun");
        loadButton.onClick = [this] { chooseAndLoadScaleFile(); };
        addAndMakeVisible (loadButton);

        clearButton.setButtonText ("Clear Tuning");
        clearButton.onClick = [this] { resetTuning(); };
        addAndMakeVisible (clearButton);
    }

    void loadScalaString (const std::string& sclText)
    {
        userCents = parseScalaCents (sclText);
        hasUserCents = true;
        scaleHandle.setPlainComplete (3.0f); // 3 = User file
        repaint();
    }

    void resetTuning()
    {
        scaleHandle.setPlainComplete (0.0f); // 12-TET
        a4Handle.setPlainComplete (440.0f);
        hasUserCents = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto& c = t.colours;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (2.0f));

        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        const float pad = t.px (8.0f);
        const float headerY = bounds.getY() + pad;

        // Current tuning info
        const float a4 = a4Handle.plain();
        const int scaleChoice = juce::jlimit (0, 3, static_cast<int> (scaleHandle.plain()));
        const bool isMts = (mtsEspHandle.plain() >= 0.5f);

        g.setFont (t.labelFont());
        g.setColour (c.text);
        juce::String scaleName = (scaleChoice < static_cast<int> (scaleHandle.info().numChoices))
                               ? scaleHandle.info().choices[static_cast<size_t> (scaleChoice)]
                               : "12-TET";

        juce::String title = "Concert Pitch A4: " + juce::String (a4, 2) + " Hz | " + scaleName;
        g.drawText (title, juce::Rectangle<float> (bounds.getX() + pad, headerY, bounds.getWidth() - 2 * pad, t.px (16.0f)),
                    juce::Justification::centredLeft, true);

        // MTS-ESP status pill
        juce::String mtsStatus = isMts ? "MTS-ESP: ACTIVE" : "MTS-ESP: Off";
        g.setColour (isMts ? c.accent : c.textDim);
        g.drawText (mtsStatus, juce::Rectangle<float> (bounds.getX() + pad, headerY, bounds.getWidth() - 2 * pad, t.px (16.0f)),
                    juce::Justification::centredRight, true);

        // Deviation graph area
        const float buttonAreaH = static_cast<float> (t.touch()) + pad;
        const float graphY = headerY + t.px (20.0f);
        const float graphH = bounds.getBottom() - buttonAreaH - graphY - pad;
        const float graphW = bounds.getWidth() - 2 * pad;

        if (graphH <= 10.0f || graphW <= 10.0f)
            return;

        const float zeroY = graphY + graphH * 0.5f;
        g.setColour (c.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (zeroY), bounds.getX() + pad, bounds.getRight() - pad);

        // Determine active deviation per degree
        std::array<float, 12> deviations {};
        if (scaleChoice == 1)
            deviations = kPythagoreanCents;
        else if (scaleChoice == 2)
            deviations = kJustIntonationCents;
        else if (scaleChoice == 3 && hasUserCents)
            deviations = userCents;

        const float colW = (graphW - 11.0f * t.px (2.0f)) / 12.0f;
        const float maxCents = 30.0f; // +-30 cents range for display

        for (int i = 0; i < 12; ++i)
        {
            const float cx = bounds.getX() + pad + static_cast<float> (i) * (colW + t.px (2.0f));
            const float dev = deviations[static_cast<size_t> (i)];
            const float barH = (std::abs (dev) / maxCents) * (graphH * 0.45f);

            juce::Rectangle<float> barRect;
            if (dev >= 0.0f)
                barRect = juce::Rectangle<float> (cx, zeroY - barH, colW, barH);
            else
                barRect = juce::Rectangle<float> (cx, zeroY, colW, barH);

            if (barH > 0.5f)
            {
                g.setColour (dev >= 0.0f ? c.accent : c.warn);
                g.fillRect (barRect);
            }

            // Note label
            g.setFont (t.font (t.px (10.0f)));
            g.setColour (c.textDim);
            g.drawText (kTuningNoteNames[i], juce::Rectangle<float> (cx, graphY + graphH - t.px (12.0f), colW, t.px (12.0f)),
                        juce::Justification::centred, false);

            // Cent value label
            if (std::abs (dev) > 0.1f)
            {
                g.setFont (t.font (t.px (8.5f)));
                g.setColour (c.text);
                juce::String valStr = (dev > 0.0f ? "+" : "") + juce::String (dev, 1);
                const float textY = dev >= 0.0f ? (zeroY - barH - t.px (10.0f)) : (zeroY + barH);
                g.drawText (valStr, juce::Rectangle<float> (cx - t.px (6.0f), textY, colW + t.px (12.0f), t.px (10.0f)),
                            juce::Justification::centred, false);
            }
        }
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const auto bounds = getLocalBounds().reduced (t.gap());
        const int touchH = t.touch();
        const int btnW = juce::jmax (t.touch() * 2, (bounds.getWidth() - t.gap()) / 2);

        auto bottomArea = bounds.removeFromBottom (touchH);
        loadButton.setBounds (bottomArea.removeFromLeft (btnW));
        clearButton.setBounds (bottomArea.removeFromRight (btnW));
    }

private:
    void chooseAndLoadScaleFile()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select Scala (.scl) or Tuning (.tun) file",
            juce::File {},
            "*.scl;*.tun;*.kbm");

        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                const auto content = file.loadFileAsString().toStdString();
                loadScalaString (content);
            }
        });
    }

    UiContext& ctx;
    ParamHandle a4Handle;
    ParamHandle scaleHandle;
    ParamHandle mtsEspHandle;
    ParamHandle mtsNoteOnHandle;

    juce::TextButton loadButton;
    juce::TextButton clearButton;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::array<float, 12> userCents {};
    bool hasUserCents = false;
};

class GlbTuningPanel final : public Panel
{
public:
    explicit GlbTuningPanel (UiContext& context)
        : Panel (context), visual (context)
    {
        addAndMakeVisible (visual);
    }

    juce::String title() const override { return "Microtonal Tuning"; }

    std::vector<std::string> keys() const override
    {
        return { "master.tuning.a4",
                 "master.tuning.scale",
                 "master.tuning.mts_esp",
                 "master.tuning.mts_note_on_only",
                 "master.tune" };
    }

    void build() override
    {
        // 4 primary controls next to visual, 1 behind More
        addControl ("master.tuning.a4");
        addControl ("master.tuning.scale");
        addControl ("master.tuning.mts_esp");
        addControl ("master.tuning.mts_note_on_only");

        addMoreControl ("master.tune");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (140.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
             + const_cast<GlbTuningPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (140.0f);
        visual.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

    GlbTuningPitchView& tuningView() noexcept { return visual; }

private:
    GlbTuningPitchView visual;
};

SYNTHYBOY_REGISTER_PANEL (GlbTuningPanel, Page::glb, 40)
} // namespace synthyboy::ui
