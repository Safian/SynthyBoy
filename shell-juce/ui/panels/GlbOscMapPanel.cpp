// SynthyBoy - UI panel: Oscillator Key & Velocity Mapping (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <array>
#include <string>
#include "../Panel.h"
#include "../ParamHandle.h"

namespace synthyboy::ui
{
namespace
{
juce::String midiNoteToName (int note)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = (note / 12) - 1;
    return juce::String (names[note % 12]) + juce::String (octave);
}
} // namespace

/** Visual interactive lanes for Oscillator 1..3 Key & Velocity range mapping. */
class GlbOscMapView final : public juce::Component
{
public:
    enum class MapMode { key, velocity };

    struct OscHandles
    {
        ParamHandle keyLow;
        ParamHandle keyHigh;
        ParamHandle velLow;
        ParamHandle velHigh;
        ParamHandle fold;
        ParamHandle warp;

        OscHandles (UiContext& c, int oscNum)
            : keyLow  (c, "osc" + std::to_string (oscNum) + ".map.key_low"),
              keyHigh (c, "osc" + std::to_string (oscNum) + ".map.key_high"),
              velLow  (c, "osc" + std::to_string (oscNum) + ".map.vel_low"),
              velHigh (c, "osc" + std::to_string (oscNum) + ".map.vel_high"),
              fold    (c, "osc" + std::to_string (oscNum) + ".map.key_fold"),
              warp    (c, "osc" + std::to_string (oscNum) + ".map.key_warp")
        {
        }
    };

    explicit GlbOscMapView (UiContext& context)
        : ctx (context),
          osc1 (context, 1),
          osc2 (context, 2),
          osc3 (context, 3)
    {
        setViewportIgnoreDragFlag (true); // it uses the drag itself: the panel does not scroll under it
        const auto update = [this] { repaint(); };
        for (auto* osc : { &osc1, &osc2, &osc3 })
        {
            osc->keyLow.onChange = update;
            osc->keyHigh.onChange = update;
            osc->velLow.onChange = update;
            osc->velHigh.onChange = update;
            osc->fold.onChange = update;
            osc->warp.onChange = update;
        }

        modeButton.setButtonText ("Mode: [KEY] / VEL");
        modeButton.onClick = [this]
        {
            currentMode = (currentMode == MapMode::key) ? MapMode::velocity : MapMode::key;
            modeButton.setButtonText (currentMode == MapMode::key ? "Mode: [KEY] / VEL" : "Mode: KEY / [VEL]");
            repaint();
        };
        addAndMakeVisible (modeButton);
    }

    void setMode (MapMode mode)
    {
        currentMode = mode;
        modeButton.setButtonText (currentMode == MapMode::key ? "Mode: [KEY] / VEL" : "Mode: KEY / [VEL]");
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
        const float headerH = static_cast<float> (t.touch());
        const float topY = bounds.getY() + pad + headerH + t.px (4.0f);
        const float availH = bounds.getBottom() - pad - topY;
        const float laneH = availH / 3.0f;
        const float labelW = t.px (54.0f);
        const float trackLeft = bounds.getX() + pad + labelW;
        const float trackRight = bounds.getRight() - pad - t.px (64.0f);
        const float trackW = trackRight - trackLeft;

        if (trackW <= 10.0f || laneH <= 10.0f)
            return;

        const OscHandles* oscs[3] = { &osc1, &osc2, &osc3 };
        const bool isKeyMode = (currentMode == MapMode::key);
        const float maxVal = isKeyMode ? 127.0f : 127.0f;
        const float minVal = isKeyMode ? 0.0f : 1.0f;

        for (int i = 0; i < 3; ++i)
        {
            const float ly = topY + static_cast<float> (i) * laneH;
            const juce::Rectangle<float> laneBounds (bounds.getX() + pad, ly, bounds.getWidth() - 2 * pad, laneH - t.px (4.0f));

            // Lane label
            g.setFont (t.labelFont());
            g.setColour (c.text);
            g.drawText ("OSC " + juce::String (i + 1), juce::Rectangle<float> (bounds.getX() + pad, ly, labelW, laneH - t.px (4.0f)),
                        juce::Justification::centredLeft, false);

            // Track background
            const juce::Rectangle<float> trackRect (trackLeft, ly + (laneH - t.px (18.0f)) * 0.5f, trackW, t.px (18.0f));
            g.setColour (c.track);
            g.fillRoundedRectangle (trackRect, t.px (3.0f));

            // Active bar range
            const float lowVal = isKeyMode ? oscs[i]->keyLow.plain() : oscs[i]->velLow.plain();
            const float highVal = isKeyMode ? oscs[i]->keyHigh.plain() : oscs[i]->velHigh.plain();

            const float normLow = juce::jlimit (0.0f, 1.0f, (lowVal - minVal) / (maxVal - minVal));
            const float normHigh = juce::jlimit (0.0f, 1.0f, (highVal - minVal) / (maxVal - minVal));

            const float barX = trackLeft + normLow * trackW;
            const float barW = juce::jmax (t.px (8.0f), (normHigh - normLow) * trackW);
            const juce::Rectangle<float> barRect (barX, trackRect.getY(), barW, trackRect.getHeight());

            g.setColour (c.accent.withAlpha (0.85f));
            g.fillRoundedRectangle (barRect, t.px (3.0f));

            // Left & Right handles
            g.setColour (c.textOnAccent);
            g.fillRoundedRectangle (juce::Rectangle<float> (barX, trackRect.getY(), t.px (5.0f), trackRect.getHeight()), t.px (2.0f));
            g.fillRoundedRectangle (juce::Rectangle<float> (barX + barW - t.px (5.0f), trackRect.getY(), t.px (5.0f), trackRect.getHeight()), t.px (2.0f));

            // Value text on bar
            g.setFont (t.font (t.px (10.0f), true));
            g.setColour (c.textOnAccent);
            juce::String rangeStr = isKeyMode
                                  ? (midiNoteToName (static_cast<int> (lowVal)) + " .. " + midiNoteToName (static_cast<int> (highVal)))
                                  : (juce::String (static_cast<int> (lowVal)) + " .. " + juce::String (static_cast<int> (highVal)));
            g.drawText (rangeStr, barRect, juce::Justification::centred, true);

            // Fold / Warp badges on right
            const float badgeAreaLeft = trackRight + t.px (6.0f);
            const bool hasFold = (oscs[i]->fold.plain() >= 0.5f);
            const bool hasWarp = (oscs[i]->warp.plain() >= 0.5f);

            const juce::Rectangle<float> foldRect (badgeAreaLeft, trackRect.getY(), t.px (24.0f), trackRect.getHeight());
            const juce::Rectangle<float> warpRect (badgeAreaLeft + t.px (28.0f), trackRect.getY(), t.px (24.0f), trackRect.getHeight());

            g.setColour (hasFold ? c.accent : c.outline.withAlpha (0.4f));
            g.fillRoundedRectangle (foldRect, t.px (2.0f));
            g.setColour (hasFold ? c.textOnAccent : c.textDim);
            g.setFont (t.font (t.px (9.0f)));
            g.drawText ("F", foldRect, juce::Justification::centred, false);

            g.setColour (hasWarp ? c.accent : c.outline.withAlpha (0.4f));
            g.fillRoundedRectangle (warpRect, t.px (2.0f));
            g.setColour (hasWarp ? c.textOnAccent : c.textDim);
            g.drawText ("W", warpRect, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int lane = getLaneAt (e.position.y);
        if (lane < 0 || lane >= 3)
            return;

        activeLane = lane;
        downPos = e.position;
        auto* osc = getOsc (lane);

        // Check if fold or warp clicked
        const auto bounds = getLocalBounds().toFloat().reduced (ctx.theme.px (2.0f));
        const float pad = ctx.theme.px (8.0f);
        const float trackRight = bounds.getRight() - pad - ctx.theme.px (64.0f);
        const float badgeAreaLeft = trackRight + ctx.theme.px (6.0f);

        if (e.position.x >= badgeAreaLeft && e.position.x < badgeAreaLeft + ctx.theme.px (26.0f))
        {
            osc->fold.setPlainComplete (osc->fold.plain() >= 0.5f ? 0.0f : 1.0f);
            repaint();
            return;
        }
        if (e.position.x >= badgeAreaLeft + ctx.theme.px (28.0f))
        {
            osc->warp.setPlainComplete (osc->warp.plain() >= 0.5f ? 0.0f : 1.0f);
            repaint();
            return;
        }

        // Handle drag start on bar
        const bool isKeyMode = (currentMode == MapMode::key);
        startLow = isKeyMode ? osc->keyLow.plain() : osc->velLow.plain();
        startHigh = isKeyMode ? osc->keyHigh.plain() : osc->velHigh.plain();

        const float labelW = ctx.theme.px (54.0f);
        const float trackLeft = bounds.getX() + pad + labelW;
        const float trackW = trackRight - trackLeft;
        const float maxVal = 127.0f;
        const float minVal = isKeyMode ? 0.0f : 1.0f;

        const float normLow = (startLow - minVal) / (maxVal - minVal);
        const float normHigh = (startHigh - minVal) / (maxVal - minVal);
        const float barX = trackLeft + normLow * trackW;
        const float barRight = trackLeft + normHigh * trackW;

        if (std::abs (e.position.x - barX) < ctx.theme.px (14.0f))
            dragPart = DragPart::leftHandle;
        else if (std::abs (e.position.x - barRight) < ctx.theme.px (14.0f))
            dragPart = DragPart::rightHandle;
        else if (e.position.x >= barX && e.position.x <= barRight)
            dragPart = DragPart::body;
        else
            dragPart = DragPart::none;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (activeLane < 0 || dragPart == DragPart::none)
            return;

        auto* osc = getOsc (activeLane);
        const auto bounds = getLocalBounds().toFloat().reduced (ctx.theme.px (2.0f));
        const float pad = ctx.theme.px (8.0f);
        const float labelW = ctx.theme.px (54.0f);
        const float trackLeft = bounds.getX() + pad + labelW;
        const float trackRight = bounds.getRight() - pad - ctx.theme.px (64.0f);
        const float trackW = trackRight - trackLeft;
        if (trackW <= 0.0f)
            return;

        const bool isKeyMode = (currentMode == MapMode::key);
        const float minVal = isKeyMode ? 0.0f : 1.0f;
        const float maxVal = 127.0f;
        const float deltaVal = ((e.position.x - downPos.x) / trackW) * (maxVal - minVal);

        if (dragPart == DragPart::leftHandle)
        {
            const float newLow = juce::jlimit (minVal, startHigh, startLow + deltaVal);
            if (isKeyMode)
                osc->keyLow.setPlainComplete (std::round (newLow));
            else
                osc->velLow.setPlainComplete (std::round (newLow));
        }
        else if (dragPart == DragPart::rightHandle)
        {
            const float newHigh = juce::jlimit (startLow, maxVal, startHigh + deltaVal);
            if (isKeyMode)
                osc->keyHigh.setPlainComplete (std::round (newHigh));
            else
                osc->velHigh.setPlainComplete (std::round (newHigh));
        }
        else if (dragPart == DragPart::body)
        {
            const float span = startHigh - startLow;
            float newLow = startLow + deltaVal;
            float newHigh = startHigh + deltaVal;
            if (newLow < minVal)
            {
                newLow = minVal;
                newHigh = minVal + span;
            }
            if (newHigh > maxVal)
            {
                newHigh = maxVal;
                newLow = maxVal - span;
            }
            if (isKeyMode)
            {
                osc->keyLow.setPlainComplete (std::round (newLow));
                osc->keyHigh.setPlainComplete (std::round (newHigh));
            }
            else
            {
                osc->velLow.setPlainComplete (std::round (newLow));
                osc->velHigh.setPlainComplete (std::round (newHigh));
            }
        }
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int lane = getLaneAt (e.position.y);
        if (lane >= 0 && lane < 3)
        {
            auto* osc = getOsc (lane);
            if (currentMode == MapMode::key)
            {
                osc->keyLow.resetToDefault();
                osc->keyHigh.resetToDefault();
            }
            else
            {
                osc->velLow.resetToDefault();
                osc->velHigh.resetToDefault();
            }
            repaint();
        }
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const auto bounds = getLocalBounds().reduced (t.gap());
        const int btnH = t.touch();
        const int btnW = juce::jmax (t.touch() * 2, t.ipx (140.0f));

        modeButton.setBounds (bounds.getX(), bounds.getY(), btnW, btnH);
    }

private:
    int getLaneAt (float y) const
    {
        const auto bounds = getLocalBounds().toFloat().reduced (ctx.theme.px (2.0f));
        const float pad = ctx.theme.px (8.0f);
        const float headerH = static_cast<float> (ctx.theme.touch());
        const float topY = bounds.getY() + pad + headerH + ctx.theme.px (4.0f);
        const float availH = bounds.getBottom() - pad - topY;
        const float laneH = availH / 3.0f;
        if (y < topY || laneH <= 0.0f)
            return -1;
        return juce::jlimit (0, 2, static_cast<int> ((y - topY) / laneH));
    }

    OscHandles* getOsc (int lane)
    {
        if (lane == 0) return &osc1;
        if (lane == 1) return &osc2;
        return &osc3;
    }

    enum class DragPart { none, leftHandle, rightHandle, body };

    UiContext& ctx;
    OscHandles osc1;
    OscHandles osc2;
    OscHandles osc3;

    juce::TextButton modeButton;
    MapMode currentMode = MapMode::key;

    int activeLane = -1;
    DragPart dragPart = DragPart::none;
    juce::Point<float> downPos;
    float startLow = 0.0f;
    float startHigh = 127.0f;
};

class GlbOscMapPanel final : public Panel
{
public:
    explicit GlbOscMapPanel (UiContext& context)
        : Panel (context), mapView (context)
    {
        addAndMakeVisible (mapView);
    }

    juce::String title() const override { return "Oscillator Mapping"; }

    std::vector<std::string> keys() const override
    {
        return { "osc1.map.key_low", "osc1.map.key_high", "osc1.map.vel_low", "osc1.map.vel_high",
                 "osc1.map.key_fold", "osc1.map.key_warp",
                 "osc2.map.key_low", "osc2.map.key_high", "osc2.map.vel_low", "osc2.map.vel_high",
                 "osc2.map.key_fold", "osc2.map.key_warp",
                 "osc3.map.key_low", "osc3.map.key_high", "osc3.map.vel_low", "osc3.map.vel_high",
                 "osc3.map.key_fold", "osc3.map.key_warp" };
    }

    void build() override
    {
        // 4 primary controls next to visual (Osc 1 key/vel ranges), 14 behind More
        addControl ("osc1.map.key_low");
        addControl ("osc1.map.key_high");
        addControl ("osc1.map.vel_low");
        addControl ("osc1.map.vel_high");

        addMoreControl ("osc1.map.key_fold");
        addMoreControl ("osc1.map.key_warp");
        addMoreControl ("osc2.map.key_low");
        addMoreControl ("osc2.map.key_high");
        addMoreControl ("osc2.map.vel_low");
        addMoreControl ("osc2.map.vel_high");
        addMoreControl ("osc2.map.key_fold");
        addMoreControl ("osc2.map.key_warp");
        addMoreControl ("osc3.map.key_low");
        addMoreControl ("osc3.map.key_high");
        addMoreControl ("osc3.map.vel_low");
        addMoreControl ("osc3.map.vel_high");
        addMoreControl ("osc3.map.key_fold");
        addMoreControl ("osc3.map.key_warp");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (160.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
             + const_cast<GlbOscMapPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (160.0f);
        mapView.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

private:
    GlbOscMapView mapView;
};

SYNTHYBOY_REGISTER_PANEL (GlbOscMapPanel, Page::glb, 60)
} // namespace synthyboy::ui
