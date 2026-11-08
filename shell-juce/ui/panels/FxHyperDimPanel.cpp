// SynthyBoy - UI panel: Hyper / Dimension (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

namespace synthyboy::ui
{
class FxHyperDimView final : public FxVisualBase
{
public:
    explicit FxHyperDimView (UiContext& context)
        : FxVisualBase (context, "Stereo Unison & Dimension Expander (Drag Left: Detune, Right: Size)"),
          detuneHandle (context, "hyperdim.detune"),
          dimSizeHandle (context, "hyperdim.dim_size"),
          unisonHandle (context, "hyperdim.unison"),
          mixHandle (context, "hyperdim.mix"),
          dimMixHandle (context, "hyperdim.dim_mix")
    {
        detuneHandle.onChange = [this] { repaint(); };
        dimSizeHandle.onChange = [this] { repaint(); };
        unisonHandle.onChange = [this] { repaint(); };
        mixHandle.onChange = [this] { repaint(); };
        dimMixHandle.onChange = [this] { repaint(); };

        #if defined(SYNTHYBOY_DEFAULT_TIER) && SYNTHYBOY_DEFAULT_TIER == low
            setTierBypassed (true);
        #endif
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        const float halfW = area.getWidth() * 0.5f;
        const auto leftArea = area.withWidth (halfW - t.px (4.0f));
        const auto rightArea = area.withTrimmedLeft (halfW + t.px (4.0f));

        // Center separator
        g.setColour (t.colours.outline.withAlpha (0.4f));
        g.drawVerticalLine (juce::roundToInt (area.getCentreX()), area.getY(), area.getBottom());

        // 1. Hyper Unison field (Left)
        g.setColour (t.colours.textDim);
        g.setFont (t.font (t.px (9.0f)));
        g.drawText ("HYPER UNISON", leftArea.removeFromTop (t.px (12.0f)), juce::Justification::centred, false);

        const int numVoices = juce::jlimit (1, 7, juce::roundToInt (unisonHandle.plain()));
        const float detune = juce::jlimit (0.0f, 1.0f, detuneHandle.plain() / 100.0f);
        const float midY = leftArea.getCentreY();
        const float midX = leftArea.getCentreX();

        for (int i = 0; i < numVoices; ++i)
        {
            const float spread = (numVoices > 1) ? (static_cast<float> (i) / static_cast<float> (numVoices - 1) - 0.5f) : 0.0f;
            const float x = midX + spread * (leftArea.getWidth() * 0.8f * detune);
            const float y = midY + std::sin (static_cast<float> (i) * 1.5f) * t.px (12.0f);

            g.setColour (t.colours.accent);
            g.fillEllipse (x - t.px (4.0f), y - t.px (4.0f), t.px (8.0f), t.px (8.0f));
        }

        // 2. Dimension Expander square (Right)
        g.setColour (t.colours.textDim);
        g.setFont (t.font (t.px (9.0f)));
        g.drawText ("DIMENSION", rightArea.removeFromTop (t.px (12.0f)), juce::Justification::centred, false);

        const float dimSize = juce::jlimit (0.0f, 1.0f, dimSizeHandle.plain() / 100.0f);
        const float maxSide = juce::jmin (rightArea.getWidth(), rightArea.getHeight()) * 0.75f;
        const float side = t.px (10.0f) + maxSide * dimSize;

        const auto dimSquare = juce::Rectangle<float> (rightArea.getCentreX() - side * 0.5f,
                                                       rightArea.getCentreY() - side * 0.5f,
                                                       side, side);

        g.setColour (t.colours.accentDim.withAlpha (0.25f));
        g.fillRoundedRectangle (dimSquare, t.px (3.0f));
        g.setColour (t.colours.accent);
        g.drawRoundedRectangle (dimSquare, t.px (3.0f), t.px (1.5f));

        // Draggable node for dimension size
        drawNode (g, dimSquare.getTopRight(), draggingRight, juce::String (juce::roundToInt (dimSizeHandle.plain())) + "%");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        if (e.position.x < area.getCentreX())
        {
            draggingLeft = true;
            detuneHandle.beginGesture();
            updateLeft (e.position, area.withWidth (area.getWidth() * 0.5f));
        }
        else
        {
            draggingRight = true;
            dimSizeHandle.beginGesture();
            updateRight (e.position, area.withTrimmedLeft (area.getWidth() * 0.5f));
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f)).withTrimmedTop (t.px (16.0f));

        if (draggingLeft)
            updateLeft (e.position, area.withWidth (area.getWidth() * 0.5f));
        else if (draggingRight)
            updateRight (e.position, area.withTrimmedLeft (area.getWidth() * 0.5f));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingLeft)
        {
            detuneHandle.endGesture();
            draggingLeft = false;
        }
        if (draggingRight)
        {
            dimSizeHandle.endGesture();
            draggingRight = false;
        }
    }

private:
    void updateLeft (juce::Point<float> pos, juce::Rectangle<float> leftArea)
    {
        const float normX = juce::jlimit (0.0f, 1.0f, (pos.x - leftArea.getX()) / leftArea.getWidth());
        detuneHandle.setNormalised (normX);
        repaint();
    }

    void updateRight (juce::Point<float> pos, juce::Rectangle<float> rightArea)
    {
        const float normY = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - rightArea.getY()) / rightArea.getHeight());
        dimSizeHandle.setNormalised (normY);
        repaint();
    }

    ParamHandle detuneHandle;
    ParamHandle dimSizeHandle;
    ParamHandle unisonHandle;
    ParamHandle mixHandle;
    ParamHandle dimMixHandle;

    bool draggingLeft = false;
    bool draggingRight = false;
};

class FxHyperDimPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Hyper / Dimension"; }

    std::vector<std::string> keys() const override
    {
        return {
            "hyperdim.mix", "hyperdim.detune", "hyperdim.dim_size", "hyperdim.dim_mix",
            "hyperdim.on", "hyperdim.rate", "hyperdim.unison", "hyperdim.retrig", "hyperdim.level", "hyperdim.rack"
        };
    }

    void build() override
    {
        visual = std::make_unique<FxHyperDimView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("hyperdim.mix");
        addControl ("hyperdim.detune");
        addControl ("hyperdim.dim_size");
        addControl ("hyperdim.dim_mix");

        // More controls (6 keys)
        addMoreControl ("hyperdim.on");
        addMoreControl ("hyperdim.rate");
        addMoreControl ("hyperdim.unison");
        addMoreControl ("hyperdim.retrig");
        addMoreControl ("hyperdim.level");
        addMoreControl ("hyperdim.rack");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (120.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxHyperDimPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (120.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxHyperDimView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxHyperDimPanel, Page::fx, 110)
} // namespace synthyboy::ui
