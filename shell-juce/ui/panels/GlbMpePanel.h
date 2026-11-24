// SynthyBoy - UI panel: MPE & Expression (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cmath>

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
          velTrackHandle (context, "master.velocity_track"),
          xyzHandle (context, "master.mpe.xyz_macro"),
          yWheelHandle (context, "master.mpe.y_modwheel"),
          bendToXHandle (context, "master.mpe.bend_to_x"),
          yBipolarHandle (context, "master.mpe.y_bipolar")
    {
        setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
        const auto update = [this] { syncButtons(); repaint(); };
        xyzHandle.onChange = update;
        yWheelHandle.onChange = update;
        bendToXHandle.onChange = update;
        yBipolarHandle.onChange = update;
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
        syncButtons();
        // WP-17: while nobody touches the pad it shows what the engine receives (pitch wheel, slide, pressure of the heard voice)
        ctx.live->bind (*this, {}, [this] { pullLive(); });
    }

    /** The pad sends what it shows (GROUPA2 WP-17): X is the pitch bend, Y the slide (CC 74), Z the pressure, on channel 1 - the manager channel of MPE, which moves
        every note, and an ordinary channel without MPE. The values go out when they change; letting go springs X back to the middle and Y and Z to 0. */
    void sendExpression (bool release)
    {
        const int bend = release ? 8192 : juce::jlimit (0, 16383, juce::roundToInt (exprX * 16383.0f));
        const int slide = release ? 0 : juce::jlimit (0, 127, juce::roundToInt (exprY * 127.0f));
        const int pressure = release ? 0 : juce::jlimit (0, 127, juce::roundToInt (exprZ * 127.0f));
        if (bend != sentBend)
            ctx.host.injectMidiMessage (juce::MidiMessage::pitchWheel (1, bend));
        if (slide != sentSlide)
            ctx.host.injectMidiMessage (juce::MidiMessage::controllerEvent (1, 74, slide));
        if (pressure != sentPressure)
            ctx.host.injectMidiMessage (juce::MidiMessage::channelPressureChange (1, pressure));
        sentBend = bend;
        sentSlide = slide;
        sentPressure = pressure;
    }
    bool touching() const noexcept { return touched; }
    /** Where the pad point is now (0..1 each; Y 0 = bottom). The tests read and set them. */
    float padX() const noexcept { return exprX; }
    float padY() const noexcept { return exprY; }
    float padZ() const noexcept { return exprZ; }

    /** The pad follows the engine while it is not touched. */
    void pullLive()
    {
        if (touched || ! ctx.live->available())
            return;
        const float x = juce::jlimit (0.0f, 1.0f, ctx.live->source (sourceChoiceForKey ("pitch_wheel"))); // the engine gives the wheel as 0 .. 1 with 0.5 in the middle
        const float y = juce::jlimit (0.0f, 1.0f, ctx.live->source (sourceChoiceForKey ("slide")));
        const float z = juce::jlimit (0.0f, 1.0f, ctx.live->source (sourceChoiceForKey ("poly_aftertouch")));
        if (std::abs (x - exprX) > 0.002f || std::abs (y - exprY) > 0.002f || std::abs (z - exprZ) > 0.002f)
        {
            exprX = x;
            exprY = y;
            exprZ = z;
            repaint();
        }
    }

    /** The three quick maps are real now (WP-09): they set master.mpe.xyz_macro / y_modwheel and show their state. */
    juce::Button& xyzButton() noexcept { return quickMapXyz; }
    juce::Button& yzButton() noexcept { return quickMapYz; }
    juce::Button& yModWheelButton() noexcept { return quickMapYMod; }

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
        touched = true;
        updateExpressionFromMouse (e);
        sendExpression (false);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        updateExpressionFromMouse (e);
        sendExpression (false);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        touched = false;
        exprX = 0.5f;
        exprY = 0.0f;
        exprZ = 0.0f;
        sendExpression (true);
        repaint();
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

    int xyzMode() const { return xyzHandle.valid() ? juce::roundToInt (xyzHandle.plain()) : 0; }

    void syncButtons()
    {
        quickMapXyz.setToggleState (xyzMode() == 1, juce::dontSendNotification);
        quickMapYz.setToggleState (xyzMode() == 2, juce::dontSendNotification);
        quickMapYMod.setToggleState (yWheelHandle.valid() && yWheelHandle.plain() > 0.5f, juce::dontSendNotification);
    }

    void doQuickMap (int mapType)
    {
        // 3: XYZ -> Macro 1, 2, 3 (choice 1), 2: YZ -> Macro 1, 2 (choice 2), 1: Y -> Mod Wheel (the switch); a second tap switches the map off
        if (mapType == 1)
        {
            if (yWheelHandle.valid())
                yWheelHandle.setPlainComplete (yWheelHandle.plain() > 0.5f ? 0.0f : 1.0f);
        }
        else if (xyzHandle.valid())
        {
            const int wanted = mapType == 3 ? 1 : 2;
            xyzHandle.setPlainComplete (xyzMode() == wanted ? 0.0f : static_cast<float> (wanted));
        }
        syncButtons();
        repaint();
    }

    UiContext& ctx;
    ParamHandle mpeOnHandle;
    ParamHandle mpeBendHandle;
    ParamHandle bendRangeHandle;
    ParamHandle velTrackHandle;
    ParamHandle xyzHandle;
    ParamHandle yWheelHandle;
    ParamHandle bendToXHandle;
    ParamHandle yBipolarHandle;

    juce::TextButton quickMapXyz;
    juce::TextButton quickMapYz;
    juce::TextButton quickMapYMod;

    bool touched = false;
    int sentBend = 8192, sentSlide = 0, sentPressure = 0;
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
                 "curve.note.legato",
                 "master.mpe.xyz_macro",
                 "master.mpe.y_modwheel",
                 "master.mpe.bend_to_x",
                 "master.mpe.y_bipolar" };
    }

    void build() override
    {
        // 4 primary controls next to visual, 6 behind More
        addControl ("master.mpe.on");
        addControl ("master.mpe.bend_range");
        addControl ("master.bend_range");
        addControl ("master.velocity_track");

        addMoreControl ("curve.velocity.legato");
        addMoreControl ("curve.note.legato");
        addMoreControl ("master.mpe.xyz_macro");
        addMoreControl ("master.mpe.y_modwheel");
        addMoreControl ("master.mpe.bend_to_x");
        addMoreControl ("master.mpe.y_bipolar");
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

public:
    GlbMpePadView& pad() noexcept { return padView; }
};

} // namespace synthyboy::ui
