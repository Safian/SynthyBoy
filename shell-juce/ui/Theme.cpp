// SynthyBoy - UI: theme and LookAndFeel.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"

namespace synthyboy::ui
{
namespace
{
Palette darkPalette()
{
    Palette p;
    p.background   = juce::Colour (0xff14161c);
    p.panel        = juce::Colour (0xff1d2029);
    p.panelRaised  = juce::Colour (0xff262a36);
    p.outline      = juce::Colour (0xff3a4052);
    p.text         = juce::Colour (0xffe9ecf4);
    p.textDim      = juce::Colour (0xff98a0b5);
    p.textOnAccent = juce::Colour (0xff0b0d12);
    p.accent       = juce::Colour (0xff4fd1c5);
    p.accentDim    = juce::Colour (0xff2c7f78);
    p.track        = juce::Colour (0xff343a4b);
    p.mod          = juce::Colour (0xffa855f7); // docs/ui-design/06: bright purple, positive depth
    p.modNegative  = juce::Colour (0xffc084fc); // lighter, negative depth
    p.modIdle      = juce::Colour (0xff6b7186);
    p.warn         = juce::Colour (0xfff5a524);
    p.danger       = juce::Colour (0xffe5484d);
    p.ok           = juce::Colour (0xff44cc44);
    p.onMod        = juce::Colour (0xffffffff);
    p.keyWhite     = juce::Colour (0xffeceff6);
    p.keyBlack     = juce::Colour (0xff1a1c24);
    p.keyDown      = juce::Colour (0xff4fd1c5);
    return p;
}

Palette lightPalette()
{
    Palette p;
    p.background   = juce::Colour (0xfff2f3f7);
    p.panel        = juce::Colour (0xffffffff);
    p.panelRaised  = juce::Colour (0xffe6e8ef);
    p.outline      = juce::Colour (0xffb9bfcf);
    p.text         = juce::Colour (0xff1b1e27);
    p.textDim      = juce::Colour (0xff626a80);
    p.textOnAccent = juce::Colour (0xffffffff);
    p.accent       = juce::Colour (0xff0f8f84);
    p.accentDim    = juce::Colour (0xff7cc4bd);
    p.track        = juce::Colour (0xffd2d6e2);
    p.mod          = juce::Colour (0xff9333ea);
    p.modNegative  = juce::Colour (0xffb26cf5);
    p.modIdle      = juce::Colour (0xff8a90a4);
    p.warn         = juce::Colour (0xffc77700);
    p.danger       = juce::Colour (0xffc62828);
    p.ok           = juce::Colour (0xff2e9e2e);
    p.onMod        = juce::Colour (0xffffffff);
    p.keyWhite     = juce::Colour (0xfffbfbfd);
    p.keyBlack     = juce::Colour (0xff2a2d38);
    p.keyDown      = juce::Colour (0xff0f8f84);
    return p;
}
} // namespace

Theme Theme::dark()
{
    Theme t;
    t.darkMode = true;
    t.colours = darkPalette();
    return t;
}

Theme Theme::light()
{
    Theme t;
    t.darkMode = false;
    t.colours = lightPalette();
    return t;
}

void Theme::setDark (bool shouldBeDark)
{
    darkMode = shouldBeDark;
    colours = shouldBeDark ? darkPalette() : lightPalette();
}

void Theme::setScale (float newScale) noexcept
{
    uiScale = juce::jlimit (0.5f, 3.0f, newScale);
}

float Theme::textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

juce::Font Theme::font (float height, bool bold) const
{
    auto options = juce::FontOptions().withHeight (height);
    if (bold)
        options = options.withStyle ("Bold");
    return juce::Font (options);
}

// ---- LookAndFeel --------------------------------------------------------------------------------------------------------------------

SynthLookAndFeel::SynthLookAndFeel (const Theme& themeToUse) : theme (themeToUse)
{
    refresh();
}

void SynthLookAndFeel::refresh()
{
    const Palette& c = theme.colours;
    setColour (juce::ResizableWindow::backgroundColourId, c.background);
    setColour (juce::TextButton::buttonColourId, c.panelRaised);
    setColour (juce::TextButton::buttonOnColourId, c.accent);
    setColour (juce::TextButton::textColourOffId, c.text);
    setColour (juce::TextButton::textColourOnId, c.textOnAccent);
    setColour (juce::ComboBox::backgroundColourId, c.panelRaised);
    setColour (juce::ComboBox::textColourId, c.text);
    setColour (juce::ComboBox::outlineColourId, c.outline);
    setColour (juce::ComboBox::arrowColourId, c.textDim);
    setColour (juce::PopupMenu::backgroundColourId, c.panel);
    setColour (juce::PopupMenu::textColourId, c.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c.accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, c.text);
    setColour (juce::PopupMenu::headerTextColourId, c.textDim);
    setColour (juce::Label::textColourId, c.text);
    setColour (juce::TextEditor::backgroundColourId, c.background);
    setColour (juce::TextEditor::textColourId, c.text);
    setColour (juce::TextEditor::outlineColourId, c.outline);
    setColour (juce::TextEditor::focusedOutlineColourId, c.accent);
    setColour (juce::CaretComponent::caretColourId, c.accent);
    setColour (juce::ScrollBar::thumbColourId, c.outline);
    setColour (juce::TooltipWindow::backgroundColourId, c.panelRaised);
    setColour (juce::TooltipWindow::textColourId, c.text);
    setColour (juce::TooltipWindow::outlineColourId, c.outline);
}

juce::Font SynthLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme.font (juce::jmin (theme.px (14.0f), static_cast<float> (buttonHeight) * 0.5f), true);
}

juce::Font SynthLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return theme.font (theme.px (14.0f));
}

juce::Font SynthLookAndFeel::getPopupMenuFont()
{
    return theme.font (theme.px (15.0f));
}

void SynthLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto area = b.getLocalBounds().toFloat().reduced (theme.px (2.0f)); // the visual inset: the hit area stays the full 44 points
    auto fill = bg;
    if (b.isEnabled() == false)
        fill = fill.withAlpha (0.4f);
    else if (down)
        fill = fill.brighter (0.15f);
    else if (highlighted)
        fill = fill.brighter (0.07f);

    const float radius = theme.px (8.0f);
    g.setColour (fill);
    g.fillRoundedRectangle (area, radius);
    g.setColour (theme.colours.outline);
    g.drawRoundedRectangle (area, radius, 1.0f);
}

void SynthLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto area = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (area, theme.px (8.0f));
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (area, theme.px (8.0f), 1.0f);
}

void SynthLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (theme.colours.panel);
    g.setColour (theme.colours.outline);
    g.drawRect (0, 0, width, height, 1);
}

juce::Rectangle<int> SynthLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto font = theme.font (theme.px (13.0f));
    juce::GlyphArrangement ga;
    ga.addFittedText (font, tipText, 0, 0, theme.px (320.0f), theme.px (200.0f), juce::Justification::left, 4);
    const int w = juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth()) + theme.ipx (16.0f);
    const int h = juce::roundToInt (ga.getBoundingBox (0, -1, true).getHeight()) + theme.ipx (12.0f);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void SynthLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto area = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (theme.colours.panelRaised);
    g.fillRoundedRectangle (area, theme.px (6.0f));
    g.setColour (theme.colours.outline);
    g.drawRoundedRectangle (area, theme.px (6.0f), 1.0f);
    g.setColour (theme.colours.text);
    g.setFont (theme.font (theme.px (13.0f)));
    g.drawFittedText (text, juce::Rectangle<int> (0, 0, width, height).reduced (theme.ipx (8.0f), theme.ipx (6.0f)), juce::Justification::centredLeft, 4);
}
} // namespace synthyboy::ui
