// SynthyBoy - UI: mod ring and value bubble.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Overlays.h"

#include <cmath>

namespace synthyboy::ui
{
namespace
{
constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.25f; // 7:30, measured clockwise from 12 o'clock
constexpr float kRotaryEnd = juce::MathConstants<float>::pi * 2.75f;   // 4:30

float angleFor (float normalised)
{
    return kRotaryStart + juce::jlimit (0.0f, 1.0f, normalised) * (kRotaryEnd - kRotaryStart);
}
} // namespace

ModRing::ModRing (const Theme& themeToUse, Shape shapeToUse) : theme (themeToUse), shape (shapeToUse)
{
    setInterceptsMouseClicks (false, false);
}

void ModRing::setState (State newState)
{
    current = std::move (newState);
    repaint();
}

std::pair<float, float> ModRing::spanFor (const ModEntry& e, float position) noexcept
{
    const float d = e.amount / 100.0f;
    float lo, hi;
    if (e.bipolar)
    {
        lo = position - std::fabs (d);
        hi = position + std::fabs (d);
    }
    else
    {
        lo = juce::jmin (position, position + d);
        hi = juce::jmax (position, position + d);
    }
    return { juce::jlimit (0.0f, 1.0f, lo), juce::jlimit (0.0f, 1.0f, hi) };
}

void ModRing::paint (juce::Graphics& g)
{
    const Palette& c = theme.colours;
    const auto bounds = getLocalBounds().toFloat();
    const float stroke = juce::jmax (2.0f, theme.px (3.0f));

    if (shape == Shape::ring)
    {
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - stroke;

        if (current.target)
        {
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, kRotaryStart, kRotaryEnd, true);
            g.setColour (c.mod.withAlpha (0.45f));
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.0f, stroke * 0.5f)));
        }

        for (const auto& e : current.entries)
        {
            const bool armed = current.armedSource == 0 || current.armedSource == e.source || e.slot == current.activeSlot;
            const auto span = spanFor (e, current.position);
            if (span.second - span.first < 0.002f)
                continue;
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleFor (span.first), angleFor (span.second), true);
            if (e.bypass)
            {
                juce::Path dashed;
                const float dash[] = { 3.0f, 3.0f };
                juce::PathStrokeType (stroke * 0.7f).createDashedStroke (dashed, p, dash, 2);
                g.setColour (c.modIdle);
                g.fillPath (dashed);
                continue;
            }
            g.setColour (! armed ? c.modIdle.withAlpha (0.7f) : (e.amount >= 0.0f ? c.mod : c.modNegative));
            g.strokePath (p, juce::PathStrokeType (armed ? stroke : stroke * 0.6f));
        }
    }
    else
    {
        const float y = bounds.getBottom() - stroke;
        const float x0 = bounds.getX(), w = bounds.getWidth();
        if (current.target)
        {
            g.setColour (c.mod.withAlpha (0.45f));
            g.fillRect (x0, y + stroke * 0.25f, w, stroke * 0.5f);
        }
        for (const auto& e : current.entries)
        {
            const bool armed = current.armedSource == 0 || current.armedSource == e.source || e.slot == current.activeSlot;
            const auto span = spanFor (e, current.position);
            g.setColour (e.bypass ? c.modIdle : (! armed ? c.modIdle.withAlpha (0.7f) : (e.amount >= 0.0f ? c.mod : c.modNegative)));
            g.fillRoundedRectangle (x0 + span.first * w, y, juce::jmax (2.0f, (span.second - span.first) * w), stroke, stroke * 0.5f);
        }
    }

    if (current.live >= 0.0f && ! current.entries.empty())
    {
        // the live value (WP-17): a bright mark where the engine plays the control right now, on top of the halo of the modulations
        g.setColour (c.text);
        if (shape == Shape::ring)
        {
            const auto centre = bounds.getCentre();
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - stroke;
            const float angle = angleFor (current.live);
            const float inner = radius - stroke * 1.6f, outer = radius + stroke * 1.2f;
            g.drawLine (centre.x + std::sin (angle) * inner, centre.y - std::cos (angle) * inner, centre.x + std::sin (angle) * outer, centre.y - std::cos (angle) * outer,
                        juce::jmax (2.0f, stroke * 0.8f));
        }
        else
        {
            const float x = bounds.getX() + current.live * bounds.getWidth();
            g.fillRect (x - stroke * 0.4f, bounds.getBottom() - stroke * 2.4f, juce::jmax (2.0f, stroke * 0.8f), stroke * 2.6f);
        }
    }

    if (current.learning)
    {
        // MIDI learn: the control waits for a CC
        g.setColour (c.warn);
        g.drawRoundedRectangle (bounds.reduced (1.0f), theme.px (6.0f), juce::jmax (2.0f, theme.px (2.0f)));
        g.setFont (theme.font (theme.px (10.0f), true));
        g.drawText ("LEARN", bounds.toNearestInt().removeFromBottom (theme.ipx (14.0f)), juce::Justification::centred, false);
    }
    else if (current.midiCc >= 0)
    {
        // the mapped controller, bottom right
        const juce::String text = "CC" + juce::String (current.midiCc);
        g.setFont (theme.font (theme.px (9.0f), true));
        const int w = text.length() * theme.ipx (6.0f) + theme.ipx (4.0f);
        const auto tag = juce::Rectangle<int> (getWidth() - w, getHeight() - theme.ipx (12.0f), w, theme.ipx (12.0f));
        g.setColour (c.panel.withAlpha (0.85f));
        g.fillRoundedRectangle (tag.toFloat(), theme.px (3.0f));
        g.setColour (c.mod);
        g.drawText (text, tag, juce::Justification::centred, false);
    }

    if (! current.entries.empty())
    {
        // count badge, top left
        const float d = theme.px (14.0f);
        const juce::Rectangle<float> badge (bounds.getX(), bounds.getY(), d, d);
        g.setColour (c.mod);
        g.fillEllipse (badge);
        g.setColour (c.onMod);
        g.setFont (theme.font (d * 0.72f, true));
        g.drawText (juce::String (static_cast<int> (current.entries.size())), badge.toNearestInt(), juce::Justification::centred, false);
    }
}

// ---- ValueBubble ------------------------------------------------------------------------------------------------------------------

void ValueBubble::setContent (const juce::String& valueText, FineZone activeZone)
{
    text = valueText;
    zone = activeZone;
    repaint();
}

juce::Rectangle<int> ValueBubble::idealSize() const
{
    const int textW = juce::roundToInt (Theme::textWidth (theme.valueFont(), text));
    return { 0, 0, juce::jmax (theme.ipx (120.0f), textW + theme.ipx (24.0f)), theme.ipx (50.0f) };
}

void ValueBubble::paint (juce::Graphics& g)
{
    const Palette& c = theme.colours;
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (c.panelRaised);
    g.fillRoundedRectangle (area, theme.px (8.0f));
    g.setColour (c.accent);
    g.drawRoundedRectangle (area, theme.px (8.0f), 1.5f);

    auto inner = getLocalBounds().reduced (theme.ipx (6.0f), theme.ipx (4.0f));
    g.setColour (c.text);
    g.setFont (theme.valueFont());
    g.drawText (text, inner.removeFromTop (inner.getHeight() / 2 + 2), juce::Justification::centred, false);

    // the ladder: slide the finger sideways to step down (x1, x0.25, x0.05)
    static const char* const names[] = { "x1", "x0.25", "x0.05" };
    const int zoneWidth = inner.getWidth() / 3;
    g.setFont (theme.font (theme.px (11.0f)));
    for (int i = 0; i < 3; ++i)
    {
        const auto cell = inner.removeFromLeft (zoneWidth);
        g.setColour (static_cast<int> (zone) == i ? c.accent : c.textDim);
        g.drawText (names[i], cell, juce::Justification::centred, false);
    }
}

// ---- text prompt -------------------------------------------------------------------------------------------------------------------------

namespace
{
class TextPromptSheet final : public juce::Component, private juce::ComponentListener
{
public:
    TextPromptSheet (UiContext& context, const juce::String& titleText, const juce::String& labelText, const juce::String& initial, const juce::String& okText,
                     std::function<void (const juce::String&)> doneCallback)
        : theme (context.theme), title (titleText), label (labelText), done (std::move (doneCallback))
    {
        editor.setText (initial, false);
        editor.setSelectAllWhenFocused (true);
        editor.setFont (theme.titleFont());
        editor.onReturnKey = [this] { finish (true); };
        editor.onEscapeKey = [this] { finish (false); };
        ok.setButtonText (okText);
        ok.onClick = [this] { finish (true); };
        cancel.setButtonText ("Cancel");
        cancel.onClick = [this] { finish (false); };
        addAndMakeVisible (editor);
        addAndMakeVisible (ok);
        addAndMakeVisible (cancel);

        if (auto* root = context.view->overlayRoot.getComponent())
        {
            host = root;
            root->addAndMakeVisible (this);
            root->addComponentListener (this);
            setBounds (root->getLocalBounds());
            toFront (true);
        }
        else // a control outside an editor (never the shell): a window of its own
        {
            setSize (theme.ipx (420.0f), theme.ipx (260.0f));
            addToDesktop (juce::ComponentPeer::windowHasDropShadow);
            setVisible (true);
        }
        editor.grabKeyboardFocus();
    }

    ~TextPromptSheet() override
    {
        if (host != nullptr)
            host->removeComponentListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& c = theme.colours;
        if (host != nullptr)
            g.fillAll (c.background.withAlpha (0.75f));
        else
            g.fillAll (c.panel);
        const auto box = panelBounds();
        g.setColour (c.panelRaised);
        g.fillRoundedRectangle (box.toFloat(), theme.px (8.0f));
        g.setColour (c.outline);
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), theme.px (8.0f), 1.0f);
        g.setColour (c.text);
        g.setFont (theme.titleFont());
        g.drawText (title, titleArea, juce::Justification::centredLeft, true);
        g.setColour (c.textDim);
        g.setFont (theme.labelFont());
        g.drawText (label, labelArea, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        const int gap = theme.gap();
        auto box = panelBounds().reduced (gap);
        titleArea = box.removeFromTop (theme.touch());
        labelArea = box.removeFromTop (theme.ipx (20.0f));
        editor.setBounds (box.removeFromTop (theme.touch()));
        box.removeFromTop (gap);
        auto buttons = box.removeFromTop (theme.touch());
        cancel.setBounds (buttons.removeFromLeft ((buttons.getWidth() - gap) / 2));
        buttons.removeFromLeft (gap);
        ok.setBounds (buttons);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panelBounds().contains (e.getPosition()))
            finish (false);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            finish (false);
            return true;
        }
        return false;
    }

private:
    juce::Rectangle<int> panelBounds() const
    {
        const int w = juce::jmin (getWidth() - 2 * theme.gap(), theme.ipx (420.0f));
        const int h = juce::jmin (getHeight() - 2 * theme.gap(), 3 * theme.touch() + theme.ipx (20.0f) + 4 * theme.gap());
        return getLocalBounds().withSizeKeepingCentre (w, h);
    }

    void finish (bool accepted)
    {
        if (finished)
            return;
        finished = true;
        const auto text = editor.getText().trim();
        auto callback = std::move (done);
        setVisible (false);
        // this sheet may be inside one of its own callbacks: it goes away on the next message
        juce::MessageManager::callAsync ([self = SafePointer<TextPromptSheet> (this)] { delete self.getComponent(); });
        if (accepted && callback)
            callback (text);
    }

    void componentBeingDeleted (juce::Component&) override
    {
        host = nullptr;
        delete this;
    }

    const Theme& theme;
    juce::String title, label;
    std::function<void (const juce::String&)> done;
    juce::TextEditor editor;
    juce::TextButton ok, cancel;
    juce::Rectangle<int> titleArea, labelArea;
    SafePointer<juce::Component> host;
    bool finished = false;
};
} // namespace

void showTextPrompt (UiContext& context, const juce::String& title, const juce::String& label, const juce::String& initial, const juce::String& okText,
                     std::function<void (const juce::String&)> done)
{
    new TextPromptSheet (context, title, label, initial, okText, std::move (done)); // owns itself
}
} // namespace synthyboy::ui
