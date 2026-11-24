// SynthyBoy - UI: the source of the spectral filter of an oscillator (REMAINDER item 2, docs/ui-design/13-new-keys.md 4.42).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <string>

#include "../ContentIO.h"
#include "../ParamControls.h"
#include "synthyboy/wavetable_edit.h"

namespace synthyboy::ui
{
/** The source of the spectral filter (REMAINDER item 2, Serum 2 manual p.120): the curve drawn in the filter editor, or a wavetable that acts as the filter. A menu on the bar picks
    "Curve Filter" or a table (the list of oscN.wavetable); with a table the editor gives way to the picture of the filter (the frame of the table at the position) and the frame
    slider: "it is not possible to edit the filter if you selected a wavetable". Keys oscN.spec.flt_use / flt_table / flt_frame. */
class SpecFilterSource final : public juce::Component
{
public:
    SpecFilterSource (UiContext& context, int oscIndex)
        : ctx (context), osc (oscIndex),
          useHandle (context, key ("flt_use")), tableHandle (context, key ("flt_table")), frameHandle (context, key ("flt_frame")),
          frameSlider (context, key ("flt_frame"), "FRAME")
    {
        menuButton.setTooltip ("Spectral filter source: the drawn curve or a wavetable");
        menuButton.onClick = [this] { showMenu(); };
        addAndMakeVisible (menuButton);
        addChildComponent (frameSlider);
        useHandle.onChange = tableHandle.onChange = frameHandle.onChange = [this] { refresh(); };
        refresh();
    }

    bool usesTable() const { return useHandle.valid() && useHandle.plain() >= 0.5f; }
    int tableChoice() const { return tableHandle.valid() ? juce::roundToInt (tableHandle.plain()) : 0; }
    /** -1: the drawn curve (Curve Filter), otherwise the wavetable choice (the list of oscN.wavetable). */
    void select (int entry)
    {
        if (entry < 0)
        {
            useHandle.setPlainComplete (0.0f);
            return;
        }
        tableHandle.setPlainComplete (static_cast<float> (entry));
        useHandle.setPlainComplete (1.0f);
    }
    juce::PopupMenu buildMenu() const
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Curve Filter", true, ! usesTable());
        menu.addSeparator();
        if (tableHandle.valid())
            for (int i = 0; i < static_cast<int> (choiceCount (tableHandle.info())); ++i)
                menu.addItem (100 + i, juce::String ("Wavetable: ") + choiceName (tableHandle.info(), static_cast<size_t> (i)), true, usesTable() && tableChoice() == i);
        return menu;
    }
    juce::TextButton& button() noexcept { return menuButton; }
    ParamControl& frameControl() noexcept { return frameSlider; }

    int preferredHeight (int width) const
    {
        const auto& t = ctx.theme;
        return t.touch() + (usesTable() ? t.gap() + t.ipx (96.0f) + t.gap() + frameSlider.preferredSize().y : 0) + 0 * width;
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        auto area = getLocalBounds();
        menuButton.setBounds (area.removeFromTop (t.touch()));
        area.removeFromTop (t.gap());
        thumb = usesTable() ? area.removeFromTop (t.ipx (96.0f)) : juce::Rectangle<int>();
        area.removeFromTop (t.gap());
        frameSlider.setBounds (usesTable() ? area.removeFromTop (frameSlider.preferredSize().y) : juce::Rectangle<int>());
    }

    void paint (juce::Graphics& g) override
    {
        if (! usesTable() || thumb.isEmpty())
            return;
        const auto& t = ctx.theme;
        const auto plot = thumb.toFloat();
        g.setColour (t.colours.panelRaised);
        g.fillRoundedRectangle (plot, t.px (6.0f));
        g.setColour (t.colours.outline);
        g.drawRoundedRectangle (plot, t.px (6.0f), 1.0f);
        std::array<float, 2048> curve {};
        const bool known = filterFrame (curve);
        g.setColour (known ? t.colours.accent : t.colours.textDim);
        if (known)
        {
            juce::Path path;
            const auto inner = plot.reduced (t.px (6.0f));
            for (int i = 0; i <= 256; ++i)
            {
                const float v = curve[static_cast<size_t> (juce::jmin (2047, i * 8))];
                const float x = inner.getX() + inner.getWidth() * static_cast<float> (i) / 256.0f;
                const float y = inner.getBottom() - inner.getHeight() * 0.5f * (v + 1.0f);
                i == 0 ? path.startNewSubPath (x, y) : path.lineTo (x, y);
            }
            g.strokePath (path, juce::PathStrokeType (t.px (2.0f)));
        }
        g.setFont (t.labelFont());
        g.setColour (t.colours.textDim);
        g.drawText (menuButton.getButtonText(), thumb.reduced (t.gap()), juce::Justification::topRight, true);
    }

    /** The frame of the table at the position as 2048 samples: exact for the Preset table (the stored file), the ideal shape for Sine / Saw / Square / Triangle, otherwise false
        (the picture shows the name). */
    bool filterFrame (std::array<float, 2048>& out) const
    {
        const int choice = tableChoice();
        const float position = frameHandle.valid() ? frameHandle.plain() * 0.01f : 0.0f;
        if (choice == kWavetablePresetTable)
        {
            const ContentWavetable* w = wavetableOf (ctx.host.content(), osc);
            if (w == nullptr)
                return false;
            const auto table = contentCache (ctx).table (ctx.host, w->ref);
            if (table == nullptr || table->numFrames() == 0)
                return false;
            const float at = juce::jlimit (0.0f, 1.0f, position) * static_cast<float> (table->numFrames() - 1);
            const size_t f0 = static_cast<size_t> (at), f1 = juce::jmin (f0 + 1, table->numFrames() - 1);
            const float mix = at - static_cast<float> (f0);
            for (size_t i = 0; i < 2048; ++i)
                out[i] = table->getFrame (f0)[i] * (1.0f - mix) + table->getFrame (f1)[i] * mix;
            return true;
        }
        if (choice < 0 || choice > 3)
            return false;
        for (int i = 0; i < 2048; ++i)
        {
            const float ph = static_cast<float> (i) / 2048.0f;
            out[static_cast<size_t> (i)] = choice == 0 ? std::sin (6.28318530718f * ph)                               // Sine
                                         : choice == 1 ? 1.0f - 2.0f * ph                                             // Saw (the falling ramp of the sine series)
                                         : choice == 2 ? (ph < 0.5f ? 0.9f : -0.9f)                                   // Square
                                                       : (ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : -4.0f + 4.0f * ph));   // Triangle
        }
        return true;
    }

    void refresh()
    {
        juce::String text = "Filter: Curve";
        if (usesTable() && tableHandle.valid())
            text = "Filter: Wavetable " + juce::String (choiceName (tableHandle.info(), static_cast<size_t> (juce::jlimit (0, static_cast<int> (choiceCount (tableHandle.info())) - 1, tableChoice()))));
        menuButton.setButtonText (text);
        frameSlider.setVisible (usesTable());
        if (onChanged)
            onChanged();
        resized();
        repaint();
    }
    std::function<void()> onChanged;

private:
    std::string key (const char* tail) const { return "osc" + std::to_string (osc) + ".spec." + tail; }
    void showMenu()
    {
        buildMenu().showMenuAsync (popupOptions (&menuButton), [safe = juce::Component::SafePointer<SpecFilterSource> (this)] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            safe->select (result == 1 ? -1 : result - 100);
        });
    }

    UiContext& ctx;
    int osc;
    ParamHandle useHandle, tableHandle, frameHandle;
    ParamSlider frameSlider;
    juce::TextButton menuButton { "Filter: Curve" };
    juce::Rectangle<int> thumb;
};

} // namespace synthyboy::ui
