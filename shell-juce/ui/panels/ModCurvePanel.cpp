// SynthyBoy - UI panel: Modulation Slot Curve & Remap Inspector (docs/ui-design/06-modulation.md, page `mod`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../Panel.h"
#include "ModLogic.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace synthyboy::ui
{
namespace
{
/** Interactive big visual curve display for a matrix slot. */
class ModCurveView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit ModCurveView (UiContext& context)
        : ctx (context)
    {
        startTimerHz (30); // 30 FPS animated playback preview
    }

    ~ModCurveView() override
    {
        stopTimer();
    }

    void setSlot (int slotIndex)
    {
        slot = slotIndex;
        repaint();
    }

    int getSlot() const noexcept { return slot; }

    void setFreeDrawMode (bool freeDraw)
    {
        isFreeDraw = freeDraw;
        repaint();
    }

    bool getFreeDrawMode() const noexcept { return isFreeDraw; }

    std::function<void (float)> onPowerChanged;

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.theme;
        const auto bounds = getLocalBounds().toFloat().reduced (t.px (4.0f));

        // Background
        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (bounds, t.px (6.0f));
        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (bounds, t.px (6.0f), 1.0f);

        const auto plotArea = bounds.reduced (t.px (24.0f), t.px (18.0f));
        if (plotArea.getWidth() <= 10.0f || plotArea.getHeight() <= 10.0f)
            return;

        // Current slot parameters
        const auto snap = readSlot (ctx.assign.matrix(), slot);
        const float power = snap.power;
        const bool bipolar = snap.bipolar;

        // Grid lines
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (plotArea.getY()), plotArea.getX(), plotArea.getRight());
        g.drawHorizontalLine (juce::roundToInt (plotArea.getBottom()), plotArea.getX(), plotArea.getRight());
        g.drawVerticalLine (juce::roundToInt (plotArea.getX()), plotArea.getY(), plotArea.getBottom());
        g.drawVerticalLine (juce::roundToInt (plotArea.getRight()), plotArea.getY(), plotArea.getBottom());

        // Center lines
        const float midY = plotArea.getCentreY();
        const float midX = plotArea.getCentreX();
        g.drawHorizontalLine (juce::roundToInt (midY), plotArea.getX(), plotArea.getRight());
        g.drawVerticalLine (juce::roundToInt (midX), plotArea.getY(), plotArea.getBottom());

        // Axis labels
        g.setFont (t.font (t.px (10.0f)));
        g.setColour (t.colours.textDim);
        g.drawText (bipolar ? "-1" : "0", juce::Rectangle<float> (plotArea.getX() - t.px (20.0f), plotArea.getBottom() - t.px (6.0f), t.px (16.0f), t.px (12.0f)), juce::Justification::centredRight, false);
        g.drawText ("+1", juce::Rectangle<float> (plotArea.getX() - t.px (20.0f), plotArea.getY() - t.px (6.0f), t.px (16.0f), t.px (12.0f)), juce::Justification::centredRight, false);
        g.drawText ("In: 0", juce::Rectangle<float> (plotArea.getX(), plotArea.getBottom() + t.px (2.0f), t.px (40.0f), t.px (12.0f)), juce::Justification::centredLeft, false);
        g.drawText ("1", juce::Rectangle<float> (plotArea.getRight() - t.px (16.0f), plotArea.getBottom() + t.px (2.0f), t.px (16.0f), t.px (12.0f)), juce::Justification::centredRight, false);

        // Power readout badge
        char pwrStr[32];
        std::snprintf (pwrStr, sizeof pwrStr, "Power: %+.1f %s", power, isFreeDraw ? "(GAP: Free Draw)" : "");
        g.drawText (pwrStr, juce::Rectangle<float> (plotArea.getX() + t.px (4.0f), plotArea.getY() + t.px (4.0f), t.px (160.0f), t.px (14.0f)), juce::Justification::centredLeft, false);

        // Curve path
        juce::Path curvePath;
        const int steps = 120;
        const float w = plotArea.getWidth();
        const float h = plotArea.getHeight();

        for (int i = 0; i <= steps; ++i)
        {
            const float normX = static_cast<float> (i) / static_cast<float> (steps);
            const float inputVal = bipolar ? (normX * 2.0f - 1.0f) : normX;
            const float outVal = modPowerCurve (inputVal, power, bipolar);

            const float normY = bipolar ? ((outVal + 1.0f) * 0.5f) : outVal;
            const float px = plotArea.getX() + normX * w;
            const float py = plotArea.getBottom() - normY * h;

            if (i == 0)
                curvePath.startNewSubPath (px, py);
            else
                curvePath.lineTo (px, py);
        }

        // Draw curve glow and main stroke
        g.setColour (t.colours.mod.withAlpha (0.35f));
        g.strokePath (curvePath, juce::PathStrokeType (t.px (4.0f)));
        g.setColour (t.colours.mod);
        g.strokePath (curvePath, juce::PathStrokeType (t.px (2.0f)));

        // Animated live modulation ball
        const float animT = 0.5f + 0.5f * std::sin (animPhase);
        const float liveIn = bipolar ? (animT * 2.0f - 1.0f) : animT;
        const float liveOut = modPowerCurve (liveIn, power, bipolar);
        const float liveNormY = bipolar ? ((liveOut + 1.0f) * 0.5f) : liveOut;
        const float ballX = plotArea.getX() + animT * w;
        const float ballY = plotArea.getBottom() - liveNormY * h;
        const float ballR = t.px (5.0f);

        g.setColour (t.colours.accent);
        g.fillEllipse (ballX - ballR, ballY - ballR, ballR * 2.0f, ballR * 2.0f);
        g.setColour (t.colours.text);
        g.drawEllipse (ballX - ballR, ballY - ballR, ballR * 2.0f, ballR * 2.0f, 1.0f);

        // Control handle at center
        const float handleX = plotArea.getX() + 0.5f * w;
        const float midOut = modPowerCurve (bipolar ? 0.0f : 0.5f, power, bipolar);
        const float handleY = plotArea.getBottom() - (bipolar ? 0.5f : midOut) * h;
        const float handleR = t.px (6.0f);
        g.setColour (t.colours.accentDim);
        g.fillEllipse (handleX - handleR, handleY - handleR, handleR * 2.0f, handleR * 2.0f);
        g.setColour (t.colours.text);
        g.drawEllipse (handleX - handleR, handleY - handleR, handleR * 2.0f, handleR * 2.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        downPos = e.position;
        const auto snap = readSlot (ctx.assign.matrix(), slot);
        startPower = snap.power;
        dragging = true;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float dy = downPos.y - e.position.y;
        // Dragging up increases curvature, dragging down decreases
        const float deltaPower = dy * 0.1f;
        const float newPower = std::clamp (startPower + deltaPower, -10.0f, 10.0f);

        const auto p = modSlotParams (slot);
        if (p.power >= 0)
        {
            ctx.assign.matrix().set (p.power, newPower);
            if (onPowerChanged) onPowerChanged (newPower);
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        // Double-tap resets curve to linear 0.0
        const auto p = modSlotParams (slot);
        if (p.power >= 0)
        {
            ctx.assign.matrix().set (p.power, 0.0f);
            if (onPowerChanged) onPowerChanged (0.0f);
        }
        repaint();
    }

private:
    void timerCallback() override
    {
        animPhase += 0.08f;
        if (animPhase > 6.2831853f) animPhase -= 6.2831853f;
        repaint();
    }

    UiContext& ctx;
    int slot = 0;
    bool isFreeDraw = false;
    juce::Point<float> downPos;
    float startPower = 0.0f;
    bool dragging = false;
    float animPhase = 0.0f;
};
} // namespace

class ModCurvePanel final : public Panel
{
public:
    explicit ModCurvePanel (UiContext& context)
        : Panel (context), curveView (context)
    {
    }

    juce::String title() const override { return "Slot Curve & Remap"; }

    std::vector<std::string> keys() const override
    {
        return {
            "mod01.amount",
            "mod01.power",
            "mod01.bipolar",
            "mod01.stereo",
            "mod01.bypass",
            "mod01.aux_source",
            "mod01.aux_inv",
            "mod01.aux_power",
            "mod01.aux_output"
        };
    }

    void build() override
    {
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i < 4)
                addControl (all[i]);
            else
                addMoreControl (all[i]);
        }

        prevSlotButton.setButtonText ("<");
        prevSlotButton.setTooltip ("Previous modulation slot");
        prevSlotButton.onClick = [this] { stepSlot (-1); };
        addAndMakeVisible (prevSlotButton);

        slotLabel.setText ("Slot #01", juce::dontSendNotification);
        slotLabel.setJustificationType (juce::Justification::centred);
        slotLabel.setFont (ctx.theme.font (ctx.theme.px (13.0f), true));
        slotLabel.setColour (juce::Label::textColourId, ctx.theme.colours.text);
        addAndMakeVisible (slotLabel);

        nextSlotButton.setButtonText (">");
        nextSlotButton.setTooltip ("Next modulation slot");
        nextSlotButton.onClick = [this] { stepSlot (1); };
        addAndMakeVisible (nextSlotButton);

        routingLabel.setText ("LFO 1 -> Filter 1 Cutoff", juce::dontSendNotification);
        routingLabel.setJustificationType (juce::Justification::centredLeft);
        routingLabel.setFont (ctx.theme.font (ctx.theme.px (12.0f)));
        routingLabel.setColour (juce::Label::textColourId, ctx.theme.colours.textDim);
        addAndMakeVisible (routingLabel);

        powerModeButton.setButtonText ("Power Crv");
        powerModeButton.setClickingTogglesState (true);
        powerModeButton.setToggleState (true, juce::dontSendNotification);
        powerModeButton.onClick = [this] {
            curveView.setFreeDrawMode (false);
            powerModeButton.setToggleState (true, juce::dontSendNotification);
            freeDrawButton.setToggleState (false, juce::dontSendNotification);
        };
        addAndMakeVisible (powerModeButton);

        freeDrawButton.setButtonText ("Free Draw");
        freeDrawButton.setClickingTogglesState (true);
        freeDrawButton.setToggleState (false, juce::dontSendNotification);
        freeDrawButton.onClick = [this] {
            curveView.setFreeDrawMode (true);
            freeDrawButton.setToggleState (true, juce::dontSendNotification);
            powerModeButton.setToggleState (false, juce::dontSendNotification);
        };
        addAndMakeVisible (freeDrawButton);

        gapBadge.setText ("GAP", juce::dontSendNotification);
        gapBadge.setJustificationType (juce::Justification::centred);
        gapBadge.setColour (juce::Label::backgroundColourId, ctx.theme.colours.warn);
        gapBadge.setColour (juce::Label::textColourId, ctx.theme.colours.textOnAccent);
        gapBadge.setFont (ctx.theme.font (ctx.theme.px (9.0f), true));
        gapBadge.setTooltip ("Free-form multi-point remap curve is an engine GAP feature.");
        addAndMakeVisible (gapBadge);

        addAndMakeVisible (curveView);
        curveView.onPowerChanged = [this] (float) { repaint(); };

        updateRoutingDisplay();
    }

    /** One bar: [<] slot [>] .. [GAP] [free draw] [power mode] routing. Narrower than that the right group drops to a second bar. */
    bool twoBars (int innerWidth) const
    {
        const auto& t = ctx.theme;
        return innerWidth < 2 * t.touch() + t.ipx (70.0f) + 3 * t.gap() + t.ipx (34.0f) + 2 * t.touch() + t.ipx (78.0f) + t.ipx (74.0f) + t.ipx (60.0f);
    }

    int preferredHeight (int width) const override
    {
        const int touch = ctx.theme.touch();
        const int gap = ctx.theme.gap();
        const int bars = twoBars (width - 4 * gap) ? 2 : 1;
        const int visualH = ctx.theme.ipx (160.0f);
        const int controlsH = isMoreExpanded() ? ctx.theme.ipx (140.0f) : ctx.theme.ipx (70.0f);
        return headerHeight() + bars * touch + (bars - 1) * gap + gap + visualH + gap + controlsH + gap;
    }

    void resized() override
    {
        auto area = contentArea();
        const int h = ctx.theme.touch();
        const int gap = ctx.theme.gap();

        auto topBar = area.removeFromTop (h);
        const bool two = twoBars (area.getWidth());
        juce::Rectangle<int> secondBar;
        if (two)
        {
            area.removeFromTop (gap);
            secondBar = area.removeFromTop (h);
        }
        prevSlotButton.setBounds (topBar.removeFromLeft (h));
        topBar.removeFromLeft (ctx.theme.ipx (2.0f));
        slotLabel.setBounds (topBar.removeFromLeft (ctx.theme.ipx (70.0f)));
        topBar.removeFromLeft (ctx.theme.ipx (2.0f));
        nextSlotButton.setBounds (topBar.removeFromLeft (h));
        topBar.removeFromLeft (gap);

        auto& right = two ? secondBar : topBar;
        if (! two)
        {
            gapBadge.setBounds (right.removeFromRight (ctx.theme.ipx (34.0f)));
            right.removeFromRight (ctx.theme.ipx (2.0f));
        }
        else
        {
            gapBadge.setBounds (topBar.removeFromRight (ctx.theme.ipx (34.0f)));
        }
        freeDrawButton.setBounds (right.removeFromRight (juce::jmax (h, ctx.theme.ipx (74.0f))));
        right.removeFromRight (ctx.theme.ipx (2.0f));
        powerModeButton.setBounds (right.removeFromRight (juce::jmax (h, ctx.theme.ipx (78.0f))));
        right.removeFromRight (gap);
        routingLabel.setBounds (right);

        area.removeFromTop (gap);
        const int visualH = ctx.theme.ipx (160.0f);
        curveView.setBounds (area.removeFromTop (visualH));

        area.removeFromTop (gap);
        layoutControls (area, false);
    }

private:
    void stepSlot (int delta)
    {
        currentSlot = (currentSlot + delta + kNumModSlots) % kNumModSlots;
        slotLabel.setText (juce::String::formatted ("Slot #%02d", currentSlot + 1), juce::dontSendNotification);
        curveView.setSlot (currentSlot);
        updateRoutingDisplay();
    }

    void updateRoutingDisplay()
    {
        const auto snap = readSlot (ctx.assign.matrix(), currentSlot);
        juce::String src = (snap.source >= 1 && snap.source <= static_cast<int> (kNumModSources))
            ? modSourceTable()[snap.source - 1].name : "None";

        juce::String dst = "None";
        if (snap.destination > 0)
        {
            const int pIndex = modDestinationParamIndex (static_cast<size_t> (snap.destination));
            if (pIndex >= 0 && static_cast<size_t> (pIndex) < paramCount())
                dst = paramTable()[pIndex].name;
        }

        routingLabel.setText (src + " -> " + dst, juce::dontSendNotification);
    }

    int currentSlot = 0;
    juce::TextButton prevSlotButton;
    juce::Label slotLabel;
    juce::TextButton nextSlotButton;
    juce::Label routingLabel;
    juce::TextButton powerModeButton;
    juce::TextButton freeDrawButton;
    juce::Label gapBadge;

    ModCurveView curveView;
};

SYNTHYBOY_REGISTER_PANEL (ModCurvePanel, Page::mod, 20)
} // namespace synthyboy::ui
