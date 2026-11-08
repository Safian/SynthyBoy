// SynthyBoy - UI panel: Quality Tier Selector (docs/ui-design/07-voice-midi-global.md, Page::glb).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "../Panel.h"
#include "../ParamHandle.h"
#include "synthyboy/quality.h"

namespace synthyboy::ui
{
/** Interactive Quality Tier selector cards (Low / Medium / High) and voice cap monitor. */
class GlbQualityView final : public juce::Component
{
public:
    explicit GlbQualityView (UiContext& context)
        : ctx (context),
          polyHandle (context, "master.polyphony"),
          reverbForceHandle (context, "reverb.force_enable"),
          mainFxBypassHandle (context, "main.fx_bypass")
    {
        const auto update = [this] { repaint(); };
        polyHandle.onChange = update;
        reverbForceHandle.onChange = update;
        mainFxBypassHandle.onChange = update;

        lockButton.setButtonText ("Lock Across Presets: OFF");
        lockButton.onClick = [this]
        {
            lockedAcrossPresets = ! lockedAcrossPresets;
            lockButton.setButtonText (lockedAcrossPresets ? "Lock Across Presets: ON" : "Lock Across Presets: OFF");
            repaint();
        };
        addAndMakeVisible (lockButton);
    }

    void setTier (Tier tier)
    {
        currentTier = tier;
        repaint();
    }

    Tier tier() const noexcept { return currentTier; }

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
        const float btnH = static_cast<float> (t.touch());

        // Voice capping status
        const Quality q = qualityForTier (currentTier);
        const int polySetting = static_cast<int> (polyHandle.plain());
        const bool isCapped = (polySetting > q.maxVoices);

        g.setFont (t.labelFont());
        g.setColour (isCapped ? c.warn : c.textDim);
        juce::String voiceStatus = "Voices: " + juce::String (polySetting) + " (Cap: " + juce::String (q.maxVoices) + ")";
        if (isCapped)
            voiceStatus += " [TIER CAP]";

        const float btnW = static_cast<float> (lockButton.getWidth());
        const float statusX = bounds.getX() + pad + btnW + pad;
        const float statusW = bounds.getRight() - pad - statusX;
        g.drawText (voiceStatus, juce::Rectangle<float> (statusX, headerY, statusW, btnH),
                    juce::Justification::centredRight, true);

        // 3 Cards layout
        const float cardsTop = headerY + btnH + t.px (6.0f);
        const float cardsH = bounds.getBottom() - pad - cardsTop;
        const float gap = t.px (6.0f);
        const float cardW = (bounds.getWidth() - 2 * pad - 2 * gap) / 3.0f;

        if (cardsH <= 10.0f || cardW <= 10.0f)
            return;

        struct CardInfo
        {
            Tier tier;
            const char* title;
            const char* sub;
            const char* voices;
            const char* unison;
            const char* oversample;
            const char* interp;
            const char* fx;
        };

        const CardInfo cards[3] = {
            { Tier::low, "LOW", "MPC", "Voices: 8", "Unison: 4", "1x Over", "WT: Lin", "No Rev/Flg" },
            { Tier::medium, "MEDIUM", "Phone", "Voices: 16", "Unison: 8", "2x Over", "WT: Lin", "All 9 FX" },
            { Tier::high, "HIGH", "Desktop", "Voices: 32", "Unison: 16", "2x Over", "WT: Spec", "All Active" }
        };

        const bool revForced = (reverbForceHandle.plain() >= 0.5f);

        for (int i = 0; i < 3; ++i)
        {
            const float cx = bounds.getX() + pad + static_cast<float> (i) * (cardW + gap);
            const juce::Rectangle<float> cardRect (cx, cardsTop, cardW, cardsH);
            const bool isSelected = (currentTier == cards[i].tier);

            // Card background & outline
            g.setColour (isSelected ? c.accentDim.withAlpha (0.45f) : c.track.withAlpha (0.4f));
            g.fillRoundedRectangle (cardRect, t.px (4.0f));

            g.setColour (isSelected ? c.accent : c.outline.withAlpha (0.6f));
            g.drawRoundedRectangle (cardRect, t.px (4.0f), isSelected ? 2.0f : 1.0f);

            // Card Header
            g.setFont (t.font (t.px (12.0f), true));
            g.setColour (isSelected ? c.accent : c.text);
            g.drawText (cards[i].title, juce::Rectangle<float> (cx, cardsTop + t.px (4.0f), cardW, t.px (14.0f)),
                        juce::Justification::centred, false);

            g.setFont (t.font (t.px (9.5f)));
            g.setColour (c.textDim);
            g.drawText (cards[i].sub, juce::Rectangle<float> (cx, cardsTop + t.px (18.0f), cardW, t.px (12.0f)),
                        juce::Justification::centred, false);

            // Card specs lines
            g.setFont (t.font (t.px (9.0f)));
            g.setColour (isSelected ? c.text : c.textDim);
            float lineY = cardsTop + t.px (32.0f);
            const float lineH = t.px (11.0f);

            g.drawText (cards[i].voices, juce::Rectangle<float> (cx + t.px (4.0f), lineY, cardW - t.px (8.0f), lineH), juce::Justification::centred, false);
            lineY += lineH;
            g.drawText (cards[i].unison, juce::Rectangle<float> (cx + t.px (4.0f), lineY, cardW - t.px (8.0f), lineH), juce::Justification::centred, false);
            lineY += lineH;
            g.drawText (cards[i].oversample, juce::Rectangle<float> (cx + t.px (4.0f), lineY, cardW - t.px (8.0f), lineH), juce::Justification::centred, false);
            lineY += lineH;
            g.drawText (cards[i].interp, juce::Rectangle<float> (cx + t.px (4.0f), lineY, cardW - t.px (8.0f), lineH), juce::Justification::centred, false);
            lineY += lineH;

            juce::String fxText = cards[i].fx;
            if (cards[i].tier == Tier::low && revForced)
                fxText = "+Reverb (Forced)";
            g.drawText (fxText, juce::Rectangle<float> (cx + t.px (4.0f), lineY, cardW - t.px (8.0f), lineH), juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (ctx.theme.px (2.0f));
        const float pad = ctx.theme.px (8.0f);
        const float btnH = static_cast<float> (ctx.theme.touch());
        const float cardsTop = bounds.getY() + pad + btnH + ctx.theme.px (6.0f);
        const float cardsH = bounds.getBottom() - pad - cardsTop;
        const float gap = ctx.theme.px (6.0f);
        const float cardW = (bounds.getWidth() - 2 * pad - 2 * gap) / 3.0f;

        if (e.position.y >= cardsTop && e.position.y <= cardsTop + cardsH)
        {
            const float localX = e.position.x - (bounds.getX() + pad);
            const int clicked = juce::jlimit (0, 2, static_cast<int> (localX / (cardW + gap)));
            if (clicked == 0)
                setTier (Tier::low);
            else if (clicked == 1)
                setTier (Tier::medium);
            else
                setTier (Tier::high);
        }
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const auto bounds = getLocalBounds().reduced (t.gap());
        const int btnH = t.touch();
        const int btnW = juce::jmax (t.touch() * 2, t.ipx (160.0f));

        lockButton.setBounds (bounds.getX(), bounds.getY(), btnW, btnH);
    }

private:
    UiContext& ctx;
    ParamHandle polyHandle;
    ParamHandle reverbForceHandle;
    ParamHandle mainFxBypassHandle;

    juce::TextButton lockButton;
    Tier currentTier = kDefaultTier;
    bool lockedAcrossPresets = false;
};

class GlbQualityPanel final : public Panel
{
public:
    explicit GlbQualityPanel (UiContext& context)
        : Panel (context), qualityView (context)
    {
        addAndMakeVisible (qualityView);
    }

    juce::String title() const override { return "Quality & Performance"; }

    std::vector<std::string> keys() const override
    {
        return { "master.polyphony",
                 "reverb.force_enable",
                 "main.fx_bypass",
                 "bus1.fx_bypass",
                 "bus2.fx_bypass" };
    }

    void build() override
    {
        // 3 primary controls, 2 behind More
        addControl ("master.polyphony");
        addControl ("reverb.force_enable");
        addControl ("main.fx_bypass");

        addMoreControl ("bus1.fx_bypass");
        addMoreControl ("bus2.fx_bypass");
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const int visualH = t.ipx (150.0f);
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
             + const_cast<GlbQualityPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (150.0f);
        qualityView.setBounds (area.removeFromTop (visualH));
        area.removeFromTop (ctx.theme.gap());
        layoutControls (area, false);
    }

    GlbQualityView& view() noexcept { return qualityView; }

private:
    GlbQualityView qualityView;
};

SYNTHYBOY_REGISTER_PANEL (GlbQualityPanel, Page::glb, 70)
} // namespace synthyboy::ui
