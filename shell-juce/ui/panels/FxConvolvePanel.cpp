// SynthyBoy - UI panel: Convolve (IR Reverb) (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FxCommon.h"

#include "../ContentIO.h"

namespace synthyboy::ui
{
class FxConvolveView final : public FxVisualBase
{
public:
    explicit FxConvolveView (UiContext& context)
        : FxVisualBase (context, "Impulse Response & Envelope (Drag Handles: Attack/Decay)"),
          attackHandle (context, "convolve.attack"),
          decayHandle (context, "convolve.decay"),
          sizeHandle (context, "convolve.size"),
          toneHandle (context, "convolve.tone"),
          irHandle (context, "convolve.ir"),
          loadIrButton ("Load IR...")
    {
        attackHandle.onChange = [this] { repaint(); };
        decayHandle.onChange = [this] { repaint(); };
        sizeHandle.onChange = [this] { repaint(); };
        toneHandle.onChange = [this] { repaint(); };
        irHandle.onChange = [this] { refreshImpulse(); };

        loadIrButton.onClick = [this]
        {
            if (! hasImpulse())
            {
                requestImpulseFile();
                return;
            }
            juce::PopupMenu menu;
            menu.addItem (1, "Load another impulse response...");
            menu.addItem (2, "Remove the impulse response");
            juce::Component::SafePointer<FxConvolveView> safe (this); // MSVC: no init-capture of `this` in a nested lambda
            menu.showMenuAsync (popupOptions (&loadIrButton), [safe] (int result)
            {
                if (safe == nullptr)
                    return;
                if (result == 1)
                    safe->requestImpulseFile();
                else if (result == 2)
                    safe->removeImpulseFile();
            });
        };
        addAndMakeVisible (loadIrButton);
        refreshImpulse();

        if (ctx.host.qualityTier() == Tier::low)
            setTierBypassed (true);
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f))
                                                    .withTrimmedTop (t.px (16.0f))
                                                    .withTrimmedBottom (static_cast<float> (t.touch()) + t.px (2.0f));

        drawGrid (g, area, 5, 3);

        // the impulse response of the library: its envelope (the loudest sample of each column) instead of the sketch of the idea
        if (! irEnvelope.empty())
        {
            juce::Path env;
            const int columns = static_cast<int> (irEnvelope.size());
            env.startNewSubPath (area.getX(), area.getBottom());
            for (int i = 0; i < columns; ++i)
                env.lineTo (area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (juce::jmax (1, columns - 1)),
                            area.getBottom() - area.getHeight() * 0.9f * irEnvelope[static_cast<size_t> (i)]);
            env.lineTo (area.getRight(), area.getBottom());
            env.closeSubPath();
            g.setColour (t.colours.accentDim.withAlpha (0.35f));
            g.fillPath (env);
            g.setColour (t.colours.accent);
            g.strokePath (env, juce::PathStrokeType (t.px (1.5f)));
            g.setColour (t.colours.textDim);
            g.setFont (t.labelFont());
            g.drawText (irName + "  " + juce::String (irSeconds, 2) + " s", area.toNearestInt().removeFromTop (t.ipx (16.0f)), juce::Justification::centredLeft, true);
        }

        const float attackNorm = juce::jlimit (0.0f, 1.0f, attackHandle.plain() / 2000.0f);
        const float decayNorm = juce::jlimit (0.05f, 1.0f, decayHandle.plain() / 100.0f);

        const float attX = area.getX() + area.getWidth() * attackNorm * 0.4f;
        const float decX = area.getX() + area.getWidth() * decayNorm;

        // IR noise/decay waveform envelope (the synthetic IR of the Size / Attack / Decay controls when no file is loaded)
        juce::Path p;
        p.startNewSubPath (area.getX(), area.getBottom());
        p.lineTo (attX, area.getY() + area.getHeight() * 0.15f);
        p.cubicTo (attX + (decX - attX) * 0.3f, area.getY() + area.getHeight() * 0.4f,
                   attX + (decX - attX) * 0.7f, area.getBottom() - area.getHeight() * 0.2f,
                   decX, area.getBottom());

        juce::Path filled = p;
        filled.lineTo (area.getRight(), area.getBottom());
        filled.closeSubPath();

        if (irEnvelope.empty())
        {
            g.setColour (t.colours.accentDim.withAlpha (0.2f));
            g.fillPath (filled);

            g.setColour (t.colours.accent);
            g.strokePath (p, juce::PathStrokeType (t.px (2.0f)));
        }

        // Draggable envelope nodes
        drawNode (g, juce::Point<float> (attX, area.getY() + area.getHeight() * 0.15f), draggingAtt, "Att");
        drawNode (g, juce::Point<float> (decX, area.getBottom() - t.px (8.0f)), draggingDec, "Dec");
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        auto btnArea = getLocalBounds().removeFromBottom (t.touch()).reduced (t.ipx (8.0f), 0);
        loadIrButton.setBounds (btnArea.removeFromRight (t.touch() * 2));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f))
                                                    .withTrimmedTop (t.px (16.0f))
                                                    .withTrimmedBottom (static_cast<float> (t.touch()) + t.px (2.0f));

        const float attackNorm = attackHandle.plain() / 2000.0f;
        const float decayNorm = decayHandle.plain() / 100.0f;
        const float attX = area.getX() + area.getWidth() * attackNorm * 0.4f;
        const float decX = area.getX() + area.getWidth() * decayNorm;

        if (std::abs (e.position.x - attX) < t.px (20.0f))
        {
            draggingAtt = true;
            attackHandle.beginGesture();
        }
        else if (std::abs (e.position.x - decX) < t.px (20.0f))
        {
            draggingDec = true;
            decayHandle.beginGesture();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto& t = ctx.theme;
        const auto area = getLocalBounds().toFloat().reduced (t.px (8.0f))
                                                    .withTrimmedTop (t.px (16.0f))
                                                    .withTrimmedBottom (static_cast<float> (t.touch()) + t.px (2.0f));

        const float normX = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
        if (draggingAtt)
        {
            const float attMs = normX * 2.5f * 2000.0f;
            attackHandle.setPlainComplete (juce::jlimit (0.0f, 2000.0f, attMs));
            repaint();
        }
        else if (draggingDec)
        {
            const float decPct = normX * 100.0f;
            decayHandle.setPlainComplete (juce::jlimit (0.0f, 100.0f, decPct));
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingAtt)
        {
            attackHandle.endGesture();
            draggingAtt = false;
        }
        if (draggingDec)
        {
            decayHandle.endGesture();
            draggingDec = false;
        }
    }

    // ---- content pipeline (WP-01 d): Load IR... is a real file ----
    bool hasImpulse() const { return ! ctx.host.content().impulses.empty(); }
    void requestImpulseFile()
    {
        FileRequest request;
        request.title = "Load an impulse response";
        request.wildcards = audioFileWildcard();
        request.purpose = "impulse";
        if (! ctx.host.requestFile (request, [safe = juce::Component::SafePointer<FxConvolveView> (this)] (const juce::File& file)
            {
                if (safe != nullptr)
                    safe->loadImpulse (file);
            }))
            status = "This host cannot ask for a file";
    }
    FlowResult loadImpulse (const juce::File& file)
    {
        const FlowResult result = loadImpulseFile (ctx, file);
        status = result.message;
        refreshImpulse();
        return result;
    }
    FlowResult removeImpulseFile()
    {
        const FlowResult result = removeImpulse (ctx);
        status = result.message;
        refreshImpulse();
        return result;
    }
    /** The envelope drawn from the stored file: 256 columns of the peak of |IR| (0..1). Empty without a file. */
    void refreshImpulse()
    {
        irEnvelope.clear();
        std::vector<float> mono;
        double rate = 0.0;
        if (loadImpulsePreview (ctx.host, mono, rate) && ! mono.empty())
        {
            constexpr int columns = 256;
            irEnvelope.assign (columns, 0.0f);
            float peak = 1.0e-9f;
            for (size_t i = 0; i < mono.size(); ++i)
            {
                float& slot = irEnvelope[i * columns / mono.size()];
                slot = juce::jmax (slot, std::abs (mono[i]));
                peak = juce::jmax (peak, slot);
            }
            for (auto& v : irEnvelope)
                v /= peak;
            irSeconds = static_cast<float> (static_cast<double> (mono.size()) / rate);
            const auto content = ctx.host.content();
            irName = content.impulses.empty() ? juce::String() : juce::String (content.impulses[0].name);
        }
        repaint();
    }
    const std::vector<float>& impulseEnvelope() const noexcept { return irEnvelope; }
    const juce::String& statusText() const noexcept { return status; }
    juce::TextButton& loadButton() noexcept { return loadIrButton; }

private:
    std::vector<float> irEnvelope;
    float irSeconds = 0.0f;
    juce::String irName, status;
    ParamHandle attackHandle;
    ParamHandle decayHandle;
    ParamHandle sizeHandle;
    ParamHandle toneHandle;
    ParamHandle irHandle;

    juce::TextButton loadIrButton;
    bool draggingAtt = false;
    bool draggingDec = false;
};

class FxConvolvePanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "Convolve"; }

    std::vector<std::string> keys() const override
    {
        return {
            "convolve.size", "convolve.tone", "convolve.predelay", "convolve.mix",
            "convolve.on", "convolve.ir", "convolve.phi_min", "convolve.sync", "convolve.division",
            "convolve.attack", "convolve.decay", "convolve.damp", "convolve.ir_gain", "convolve.level", "convolve.rack",
            "convolve.law" // block B35
        };
    }

    void build() override
    {
        visual = std::make_unique<FxConvolveView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls
        addControl ("convolve.size");
        addControl ("convolve.tone");
        addControl ("convolve.predelay");
        addControl ("convolve.mix");

        // More controls (11 keys)
        addMoreControl ("convolve.on");
        addMoreControl ("convolve.ir");
        addMoreControl ("convolve.phi_min");
        addMoreControl ("convolve.sync");
        addMoreControl ("convolve.division");
        addMoreControl ("convolve.attack");
        addMoreControl ("convolve.decay");
        addMoreControl ("convolve.damp");
        addMoreControl ("convolve.ir_gain");
        addMoreControl ("convolve.level");
        addMoreControl ("convolve.rack");
        addMoreControl ("convolve.law");   // block B35 (REFCOMPARE round 3): Vital | Serum (the convolution of Serum 2.1.5)
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (130.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxConvolvePanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (130.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxConvolveView> visual;

public:
    FxConvolveView* view() noexcept { return visual.get(); }
};

SYNTHYBOY_REGISTER_PANEL (FxConvolvePanel, Page::fx, 140)
} // namespace synthyboy::ui
