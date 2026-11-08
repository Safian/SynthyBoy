// SynthyBoy - UI panel: MPE & Expression (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "../Panel.h"
#include "../ParamHandle.h"

namespace synthyboy::ui
{
/** Interactive MPE expression pad: X = Pitch Bend, Y = Slide / Timbre, Z = Pressure ring. */
class GlbMpePadView final : public juce::Component
{
public:
    explicit GlbMpePadView (UiContext& context)
        : ctx (context),
          mpeOnHandle (context, "master.mpe.on"),
          mpeBendHandle (context, "master.mpe.bend_range"),
          bendRangeHandle (context, "master.bend_range"),
          velTrackHandle (context, "master.velocity_track")
    {
        const auto update = [this] { repaint(); };
        mpeOnHandle.onChange = update;
        mpeBendHandle.onChange = update;
        bendRangeHandle.onChange = update;
        velTrackHandle.onChange = update;

        quickMapXyz.setButtonText ("XYZ -> Macro 1..3");
        quickMapXyz.onClick = [this] { doQuickMap (3); };
        addAndMakeVisible (quickMapXyz);

        quickMapYz.setButtonText ("YZ -> Macro 1,2");
        quickMapYz.onClick = [this] { doQuickMap (2); };
        addAndMakeVisible (quickMapYz);

        quickMapYMod.setButtonText ("Y -> Mod Wheel");
        quickMapYMod.onClick = [this] { doQuickMap (1); };
        addAndMakeVisible (quickMapYMod);
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

        const bool mpeOn = (mpeOnHandle.plain() >= 0.5f);
        const float bendRng = mpeOn ? mpeBendHandle.plain() : bendRangeHandle.plain();

        g.setFont (t.labelFont());
        g.setColour (mpeOn ? c.accent : c.textDim);
        juce::String status = mpeOn ? "MPE: ON (CLAP Note Expression)" : "MPE: Off (Standard MIDI)";
        g.drawText (status, juce::Rectangle<float> (bounds.getX() + pad, headerY, bounds.getWidth() - 2 * pad, t.px (16.0f)),
                    juce::Justification::centredLeft, true);

        g.setColour (c.text);
        juce::String bendInfo = "Bend Range: +-" + juce::String (static_cast<int> (bendRng)) + " st";
        g.drawText (bendInfo, juce::Rectangle<float> (bounds.getX() + pad, headerY, bounds.getWidth() - 2 * pad, t.px (16.0f)),
                    juce::Justification::centredRight, true);

        // Expression pad surface
        const float btnH = static_cast<float> (t.touch());
        const float padTop = headerY + t.px (20.0f);
        const float padBottom = bounds.getBottom() - btnH - pad - t.px (4.0f);
        const float padLeft = bounds.getX() + pad;
        const float padRight = bounds.getRight() - pad;
        const float padW = padRight - padLeft;
        const float padH = padBottom - padTop;

        if (padW <= 10.0f || padH <= 10.0f)
            return;

        const juce::Rectangle<float> padArea (padLeft, padTop, padW, padH);
        g.setColour (c.background);
        g.fillRoundedRectangle (padArea, t.px (4.0f));
        g.setColour (c.outline.withAlpha (0.5f));
        g.drawRoundedRectangle (padArea, t.px (4.0f), 1.0f);

        // Center crosshair lines
        const float midX = padLeft + padW * 0.5f;
        const float midY = padTop + padH * 0.5f;
        g.setColour (c.outline.withAlpha (0.3f));
        g.drawHorizontalLine (juce::roundToInt (midY), padLeft, padRight);
        g.drawVerticalLine (juce::roundToInt (midX), padTop, padBottom);

        // Current expression position: posX in 0..1 (0.5 center), posY in 0..1 (0 bottom, 1 top)
        const float ptX = padLeft + exprX * padW;
        const float ptY = padBottom - exprY * padH;
        const float radius = t.px (6.0f) + exprZ * t.px (16.0f);

        // Pressure outer halo
        g.setColour (c.accent.withAlpha (0.25f * exprZ + 0.1f));
        g.fillEllipse (ptX - radius, ptY - radius, radius * 2.0f, radius * 2.0f);

        // Point center
        g.setColour (c.accent);
        g.fillEllipse (ptX - t.px (5.0f), ptY - t.px (5.0f), t.px (10.0f), t.px (10.0f));
        g.setColour (c.textOnAccent);
        g.drawEllipse (ptX - t.px (5.0f), ptY - t.px (5.0f), t.px (10.0f), t.px (10.0f), 1.5f);

        // Axis labels
        g.setFont (t.font (t.px (10.0f)));
        g.setColour (c.textDim);
        const float semiX = (exprX - 0.5f) * 2.0f * bendRng;
        juce::String readX = juce::String ("X (Bend): ") + (semiX >= 0.0f ? "+" : "") + juce::String (semiX, 1) + " st";
        juce::String readY = "Y (Slide): " + juce::String (juce::roundToInt (exprY * 100.0f)) + "%";
        juce::String readZ = "Z (Press): " + juce::String (juce::roundToInt (exprZ * 100.0f)) + "%";
        g.drawText (readX + "  |  " + readY + "  |  " + readZ,
                    padArea.reduced (t.px (6.0f)), juce::Justification::bottomLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        updateExpressionFromMouse (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        updateExpressionFromMouse (e);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        exprZ = juce::jlimit (0.0f, 1.0f, exprZ + wheel.deltaY * 0.2f);
        repaint();
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        auto bounds = getLocalBounds().reduced (t.gap());
        const int touchH = t.touch();
        const int gap = t.gap();
        const int btnW = juce::jmax (t.touch() * 2, (bounds.getWidth() - 2 * gap) / 3);

        auto bottomArea = bounds.removeFromBottom (touchH);
        quickMapXyz.setBounds (bottomArea.removeFromLeft (btnW));
        bottomArea.removeFromLeft (gap);
        quickMapYz.setBounds (bottomArea.removeFromLeft (btnW));
        bottomArea.removeFromLeft (gap);
        quickMapYMod.setBounds (bottomArea.removeFromLeft (btnW));
    }

private:
    void updateExpressionFromMouse (const juce::MouseEvent& e)
    {
        const auto& t = ctx.theme;
        const float pad = t.px (8.0f);
        const float headerY = t.px (2.0f) + pad;
        const float btnH = static_cast<float> (t.touch());
        const float padTop = headerY + t.px (20.0f);
        const float padBottom = static_cast<float> (getHeight()) - btnH - pad - t.px (4.0f);
        const float padLeft = pad;
        const float padRight = static_cast<float> (getWidth()) - pad;

        const float padW = padRight - padLeft;
        const float padH = padBottom - padTop;
        if (padW <= 0.0f || padH <= 0.0f)
            return;

        exprX = juce::jlimit (0.0f, 1.0f, (e.position.x - padLeft) / padW);
        exprY = juce::jlimit (0.0f, 1.0f, (padBottom - e.position.y) / padH);
        exprZ = 0.75f; // Active touch pressure
        repaint();
    }

    void doQuickMap (int mapType)
    {
        // Feedback highlight
        if (mapType == 1)
            quickMapYMod.setButtonText ("Mapped: Y->MW");
        else if (mapType == 2)
            quickMapYz.setButtonText ("Mapped: YZ->M12");
        else
            quickMapXyz.setButtonText ("Mapped: XYZ->M123");
        repaint();
    }

    UiContext& ctx;
    ParamHandle mpeOnHandle;
    ParamHandle mpeBendHandle;
    ParamHandle bendRangeHandle;
    ParamHandle velTrackHandle;

    juce::TextButton quickMapXyz;
    juce::TextButton quickMapYz;
    juce::TextButton quickMapYMod;

    float exprX = 0.5f; // Center pitch bend
    float exprY = 0.0f; // Slide
    float exprZ = 0.0f; // Pressure
};

class GlbMpePanel final : public Panel
{
public:
    explicit GlbMpePanel (UiContext& context)
        : Panel (context), padView (context)
    {
        addAndMakeVisible (padView);
    }

    juce::String title() const override { return "MPE & Expression"; }

    std::vector<std::string> keys() const override
    {
        return { "master.mpe.on",
                 "master.mpe.bend_range",
                 "master.bend_range",
                 "master.velocity_track",
                 "curve.velocity.legato",
                 "curve.note.legato" };
    }

    void build() override
    {
        // 4 primary controls next to visual, 2 behind More
        addControl ("master.mpe.on");
        addControl ("master.mpe.bend_range");
        addControl ("master.bend_range");
        addControl ("master.velocity_track");

        addMoreControl ("curve.velocity.legato");
        addMoreControl ("curve.note.legato");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (140.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
             + const_cast<GlbMpePanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (140.0f);
        padView.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

private:
    GlbMpePadView padView;
};

SYNTHYBOY_REGISTER_PANEL (GlbMpePanel, Page::glb, 50)
} // namespace synthyboy::ui
