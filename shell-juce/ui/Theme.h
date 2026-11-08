// SynthyBoy - UI: colour tokens (light / dark), fonts, metrics and the one LookAndFeel.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every colour and size a panel uses comes from here (no literals in panels). Sizes are given at scale 1 and multiplied by
// Theme::scale; touch() is the 44 point minimum target (docs/ui-design/00-BRIEF.md principle 5).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ControlModel.h"

namespace synthyboy::ui
{
struct Palette
{
    juce::Colour background, panel, panelRaised, outline;
    juce::Colour text, textDim, textOnAccent;
    juce::Colour accent, accentDim;     // controls: knob fill, selected tab, toggles
    juce::Colour track;                 // empty part of a knob / slider track
    juce::Colour mod, modNegative, modIdle; // purple depth ring (+), lighter ring (-), thin ring of inactive assignments
    juce::Colour warn;                  // amber: tier-limited, matrix full
    juce::Colour keyWhite, keyBlack, keyDown;
};

class Theme
{
public:
    static Theme dark();
    static Theme light();

    bool isDark() const noexcept { return darkMode; }
    void setDark (bool shouldBeDark);

    /** UI scale (menu "Size 75 .. 200 %"); metrics and fonts follow. Clamped to 0.5 .. 3. */
    void setScale (float newScale) noexcept;
    float scale() const noexcept { return uiScale; }

    float px (float v) const noexcept { return v * uiScale; }
    int ipx (float v) const noexcept { return juce::roundToInt (v * uiScale); }
    /** The minimum touch target: 44 points, never smaller than 44 device independent pixels whatever the scale. */
    int touch() const noexcept { return juce::jmax (ipx (kMinTouchPx), static_cast<int> (kMinTouchPx)); }
    int gap() const noexcept { return juce::jmax (ipx (kMinGapPx), static_cast<int> (kMinGapPx)); }

    // layout bands of the always-visible shell (docs/ui-design/01 section 2.1)
    int topBarHeight() const noexcept { return touch(); }
    int macroRowHeight() const noexcept { return juce::jmax (ipx (64.0f), touch()); }
    int tabBarHeight() const noexcept { return touch(); }
    int keyboardHeight (bool compact) const noexcept { return ipx (compact ? 88.0f : 104.0f); }
    int knobDiameter() const noexcept { return juce::jmax (ipx (56.0f), touch()); }

    juce::Font font (float height, bool bold = false) const;
    juce::Font labelFont() const { return font (px (12.0f)); }
    juce::Font valueFont() const { return font (px (13.0f), true); }
    juce::Font titleFont() const { return font (px (15.0f), true); }

    /** Width of a text in a font (points). */
    static float textWidth (const juce::Font& f, const juce::String& text);

    Palette colours;

private:
    bool darkMode = true;
    float uiScale = 1.0f;
};

/** The one LookAndFeel: buttons, combo boxes, popup menus, tooltips, tab-like buttons. The param controls draw themselves from the Theme. */
class SynthLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit SynthLookAndFeel (const Theme& themeToUse);
    /** Re-applies the colour ids after the Theme changed (dark / light, scale). */
    void refresh();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    const Theme& theme;
};
} // namespace synthyboy::ui
