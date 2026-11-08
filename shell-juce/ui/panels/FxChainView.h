// SynthyBoy - UI: the FX chain ribbon (16 slots) and its gestures (docs/ui-design/08-effects.md sections 2, 3).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Slot gestures: tap selects, tap on the power dot or double tap on the slot toggles the effect (Opt/Alt: every effect), hold still on a slot
// and drag = reorder (yellow drop line). Visible alternatives: [ON] (selected slot), `< Move Left` / `Move Right >`, [Bypass All].
#pragma once

#include "../GestureKit.h"
#include "FxCommon.h"

namespace synthyboy::ui
{
/** Interactive 16-slot chain ribbon and rack manager with direct drag reordering and non-gesture alternative buttons. */
class FxChainView final : public FxVisualBase
{
public:
    explicit FxChainView (UiContext& context)
        : FxVisualBase (context, "Chain Order & Racks (Drag = to reorder)"),
          moveLeftButton ("< Move Left"),
          moveRightButton ("Move Right >"),
          bypassAllButton ("Bypass All"),
          powerButton ("ON")
    {
        for (int i = 0; i < 16; ++i)
        {
            const std::string orderKey = "fx.order." + std::to_string (i + 1);
            orderHandles.emplace_back (std::make_unique<ParamHandle> (ctx, orderKey));
            orderHandles.back()->onChange = [this] { repaint(); };

            onHandles.emplace_back (std::make_unique<ParamHandle> (ctx, kFxOnKeys[i]));
            onHandles.back()->onChange = [this] { updatePowerButton(); repaint(); };

            rackHandles.emplace_back (std::make_unique<ParamHandle> (ctx, kFxRackKeys[i]));
            rackHandles.back()->onChange = [this] { repaint(); };
        }

        moveLeftButton.onClick = [this] { moveSelectedSlot (-1); };
        moveRightButton.onClick = [this] { moveSelectedSlot (1); };
        bypassAllButton.onClick = [this] { toggleBypassAll(); };
        // [ON]: the selected slot's power (the visible alternative of the double tap); Opt/Alt-click toggles every effect at once
        powerButton.onClick = [this]
        {
            if (juce::ModifierKeys::currentModifiers.isAltDown())
                toggleBypassAll();
            else
                toggleSlotPower (selectedIndex);
        };
        press.onLongPress = [this]
        {
            if (dragSourceIndex >= 0)
            {
                reorderArmed = true; // held still on a slot: now it can be dragged to another place
                repaint();
            }
        };

        addAndMakeVisible (moveLeftButton);
        addAndMakeVisible (moveRightButton);
        addAndMakeVisible (bypassAllButton);
        addAndMakeVisible (powerButton);
        updatePowerButton();
    }

    int selectedSlot() const noexcept { return selectedIndex; }

    void setSelectedSlot (int index)
    {
        if (selectedIndex != index && index >= 0 && index < 16)
        {
            selectedIndex = index;
            updatePowerButton();
            repaint();
        }
    }

    void moveSelectedSlot (int delta)
    {
        if (selectedIndex < 0 || selectedIndex >= 16)
            return;
        const int target = juce::jlimit (0, 15, selectedIndex + delta);
        if (target == selectedIndex)
            return;

        reorderSlots (selectedIndex, target);
        selectedIndex = target;
        repaint();
    }

    void toggleBypassAll()
    {
        bool anyOn = false;
        for (const auto& h : onHandles)
        {
            if (h->plain() >= 0.5f)
            {
                anyOn = true;
                break;
            }
        }

        if (anyOn)
        {
            savedOnMask = 0;
            for (size_t i = 0; i < onHandles.size(); ++i)
            {
                if (onHandles[i]->plain() >= 0.5f)
                    savedOnMask |= (1u << i);
                onHandles[i]->setPlainComplete (0.0f);
            }
        }
        else
        {
            const uint32_t mask = (savedOnMask != 0) ? savedOnMask : 0xFFFFu;
            for (size_t i = 0; i < onHandles.size(); ++i)
            {
                if ((mask & (1u << i)) != 0)
                    onHandles[i]->setPlainComplete (1.0f);
            }
        }
        repaint();
    }

    /** The effect (fx id) sitting in chain slot `slot`, -1 if the order is not usable. */
    int fxInSlot (int slot) const
    {
        std::array<int, 16> currentOrder {};
        for (size_t i = 0; i < 16; ++i)
            currentOrder[i] = juce::roundToInt (orderHandles[i]->plain());
        currentOrder = repairFxPermutation (currentOrder);
        return slot >= 0 && slot < 16 ? currentOrder[static_cast<size_t> (slot)] : -1;
    }

    /** Toggles the power of the effect in chain slot `slot` (the dot, the double tap and [ON] call it). */
    bool toggleSlotPower (int slot, bool allEffects = false)
    {
        if (allEffects)
        {
            toggleBypassAll();
            return true;
        }
        const int fxId = fxInSlot (slot);
        if (fxId < 0 || fxId >= 16)
            return false;
        auto& h = *onHandles[static_cast<size_t> (fxId)];
        h.setPlainComplete (h.plain() >= 0.5f ? 0.0f : 1.0f);
        updatePowerButton();
        repaint();
        return true;
    }

    bool slotIsOn (int slot) const
    {
        const int fxId = fxInSlot (slot);
        return fxId >= 0 && fxId < 16 && onHandles[static_cast<size_t> (fxId)]->plain() >= 0.5f;
    }

    juce::Rectangle<float> slotBounds (int slot) const { return rectForSlot (slot); }
    juce::TextButton& powerToggleButton() noexcept { return powerButton; }
    juce::TextButton& bypassAllToggleButton() noexcept { return bypassAllButton; }
    LongPressDetector& longPress() noexcept { return press; }
    bool reorderIsArmed() const noexcept { return reorderArmed; }
    /** Order of the effects in the 16 chain slots (fx ids). */
    std::array<int, 16> chainOrder() const
    {
        std::array<int, 16> out {};
        for (int i = 0; i < 16; ++i)
            out[static_cast<size_t> (i)] = fxInSlot (i);
        return out;
    }

    void updatePowerButton()
    {
        powerButton.setButtonText (slotIsOn (selectedIndex) ? "ON" : "OFF");
        powerButton.setToggleState (slotIsOn (selectedIndex), juce::dontSendNotification);
    }

    void reorderSlots (int from, int to)
    {
        if (from == to || from < 0 || from >= 16 || to < 0 || to >= 16)
            return;

        std::array<int, 16> currentOrder {};
        for (size_t i = 0; i < 16; ++i)
            currentOrder[i] = juce::roundToInt (orderHandles[i]->plain());

        currentOrder = repairFxPermutation (currentOrder);

        const int movingFx = currentOrder[static_cast<size_t> (from)];
        if (from < to)
        {
            for (int i = from; i < to; ++i)
                currentOrder[static_cast<size_t> (i)] = currentOrder[static_cast<size_t> (i + 1)];
        }
        else
        {
            for (int i = from; i > to; --i)
                currentOrder[static_cast<size_t> (i)] = currentOrder[static_cast<size_t> (i - 1)];
        }
        currentOrder[static_cast<size_t> (to)] = movingFx;

        for (size_t i = 0; i < 16; ++i)
            orderHandles[i]->setPlainComplete (static_cast<float> (currentOrder[i]));
    }

    void paint (juce::Graphics& g) override
    {
        paintBaseBackground (g);
        const auto& t = ctx.theme;

        const auto slotArea = getLocalBounds().reduced (t.ipx (6.0f))
                                              .withTrimmedTop (t.ipx (20.0f))
                                              .withTrimmedBottom (t.touch() + t.ipx (4.0f)).toFloat();

        const int cols = 8;
        const int rows = 2;
        const float slotW = static_cast<float> (slotArea.getWidth()) / static_cast<float> (cols);
        const float slotH = static_cast<float> (slotArea.getHeight()) / static_cast<float> (rows);

        std::array<int, 16> currentOrder {};
        for (size_t i = 0; i < 16; ++i)
            currentOrder[i] = juce::roundToInt (orderHandles[i]->plain());
        currentOrder = repairFxPermutation (currentOrder);

        for (int i = 0; i < 16; ++i)
        {
            const int r = i / cols;
            const int c = i % cols;
            const juce::Rectangle<float> rect (slotArea.getX() + static_cast<float> (c) * slotW + t.px (1.0f),
                                               slotArea.getY() + static_cast<float> (r) * slotH + t.px (1.0f),
                                               slotW - t.px (2.0f),
                                               slotH - t.px (2.0f));

            const int fxId = currentOrder[static_cast<size_t> (i)];
            const bool isSelected = (i == selectedIndex);
            const bool isOn = (fxId >= 0 && fxId < 16 && onHandles[static_cast<size_t> (fxId)]->plain() >= 0.5f);
            const int rackId = (fxId >= 0 && fxId < 16) ? juce::roundToInt (rackHandles[static_cast<size_t> (fxId)]->plain()) : 0;

            // Slot background
            g.setColour (isSelected ? t.colours.accentDim.withAlpha (0.35f) : t.colours.panel);
            g.fillRoundedRectangle (rect, t.px (4.0f));

            const bool grabbed = reorderArmed && i == dragSourceIndex;
            g.setColour (grabbed ? t.colours.mod : isSelected ? t.colours.accent : t.colours.outline);
            g.drawRoundedRectangle (rect, t.px (4.0f), grabbed ? t.px (3.0f) : isSelected ? t.px (2.0f) : 1.0f);

            // Slot number & Power dot
            const float dotRadius = t.px (3.5f);
            const juce::Point<float> dotCenter (rect.getX() + t.px (6.0f) + dotRadius, rect.getY() + t.px (6.0f) + dotRadius);
            g.setColour (isOn ? t.colours.accent : t.colours.warn);
            g.fillEllipse (dotCenter.x - dotRadius, dotCenter.y - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

            // Slot label
            g.setColour (t.colours.text);
            g.setFont (t.font (t.px (10.0f), true));
            const juce::String shortName = (fxId >= 0 && fxId < 16) ? kFxModuleShortNames[fxId] : "??";
            g.drawText (juce::String (i + 1) + "." + shortName,
                        rect.withTrimmedLeft (t.px (16.0f)).withTrimmedRight (t.px (12.0f)),
                        juce::Justification::centredLeft, false);

            // Rack pill / Drag handle "="
            g.setColour (t.colours.textDim);
            g.setFont (t.font (t.px (9.0f)));
            const juce::String rackTag = (rackId >= 0 && rackId < 10) ? kFxRackNames[rackId] : "M";
            g.drawText (rackTag.substring (0, 2),
                        rect.withTrimmedTop (rect.getHeight() * 0.55f).withTrimmedRight (t.px (12.0f)),
                        juce::Justification::centredLeft, false);

            // Drag handle "="
            g.setFont (t.font (t.px (11.0f), true));
            g.drawText ("=", juce::Rectangle<float> (rect).removeFromRight (t.px (12.0f)), juce::Justification::centred, false);
        }

        // Draw drop insertion line when dragging
        if (isDraggingSlot && dropTargetIndex >= 0 && dropTargetIndex < 16)
        {
            const int r = dropTargetIndex / cols;
            const int c = dropTargetIndex % cols;
            const float lineX = slotArea.getX() + static_cast<float> (c) * slotW;
            const float lineY = slotArea.getY() + static_cast<float> (r) * slotH;

            g.setColour (t.colours.accent);
            g.drawLine (lineX, lineY, lineX, lineY + slotH, t.px (3.0f));
        }
    }

    void resized() override
    {
        const auto& t = ctx.theme;
        const auto btnArea = getLocalBounds().removeFromBottom (t.touch()).reduced (t.ipx (6.0f), 0);
        const int btnW = juce::jmax (t.touch(), (btnArea.getWidth() - 3 * t.gap()) / 4);

        auto b = btnArea;
        moveLeftButton.setBounds (b.removeFromLeft (btnW));
        b.removeFromLeft (t.gap());
        moveRightButton.setBounds (b.removeFromLeft (btnW));
        b.removeFromLeft (t.gap());
        powerButton.setBounds (b.removeFromLeft (btnW));
        bypassAllButton.setBounds (juce::Rectangle<int> (btnArea).removeFromRight (btnW));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int slot = slotAtPoint (e.getPosition());
        reorderArmed = false;
        isDraggingSlot = false;
        if (slot >= 0 && slot < 16)
        {
            setSelectedSlot (slot);
            dragSourceIndex = slot;
            dropTargetIndex = slot;
            press.press (e.position);

            // a tap on the power dot of the slot (Opt/Alt-tap: every effect)
            const auto slotRect = rectForSlot (slot);
            if (e.position.x < slotRect.getX() + ctx.theme.px (18.0f))
            {
                press.cancel();
                dragSourceIndex = -1;
                toggleSlotPower (slot, e.mods.isAltDown());
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragSourceIndex < 0)
            return;
        if (! reorderArmed)
        {
            press.moved (e.position); // a drag before the hold is no reorder
            return;
        }
        isDraggingSlot = true;
        const int target = slotAtPoint (e.getPosition());
        if (target >= 0 && target < 16 && target != dropTargetIndex)
        {
            dropTargetIndex = target;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        press.cancel();
        if (reorderArmed && isDraggingSlot && dragSourceIndex >= 0 && dropTargetIndex >= 0 && dragSourceIndex != dropTargetIndex)
        {
            reorderSlots (dragSourceIndex, dropTargetIndex);
            setSelectedSlot (dropTargetIndex);
        }
        reorderArmed = false;
        isDraggingSlot = false;
        dragSourceIndex = -1;
        dropTargetIndex = -1;
        repaint();
    }

    /** Double tap anywhere on a slot: its power (design 08: "double-tap or [ON]"). */
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int slot = slotAtPoint (e.getPosition());
        press.cancel();
        reorderArmed = false;
        dragSourceIndex = -1;
        if (slot >= 0 && slot < 16)
            toggleSlotPower (slot, e.mods.isAltDown());
    }

private:
    int slotAtPoint (juce::Point<int> pt) const
    {
        const auto& t = ctx.theme;
        const auto slotArea = getLocalBounds().reduced (t.ipx (6.0f))
                                              .withTrimmedTop (t.ipx (20.0f))
                                              .withTrimmedBottom (t.touch() + t.ipx (4.0f));
        if (! slotArea.contains (pt))
            return -1;

        const int cols = 8;
        const int rows = 2;
        const int c = juce::jlimit (0, cols - 1, (pt.x - slotArea.getX()) * cols / slotArea.getWidth());
        const int r = juce::jlimit (0, rows - 1, (pt.y - slotArea.getY()) * rows / slotArea.getHeight());
        return r * cols + c;
    }

    juce::Rectangle<float> rectForSlot (int slot) const
    {
        const auto& t = ctx.theme;
        const auto slotArea = getLocalBounds().reduced (t.ipx (6.0f))
                                              .withTrimmedTop (t.ipx (20.0f))
                                              .withTrimmedBottom (t.touch() + t.ipx (4.0f)).toFloat();
        const int cols = 8;
        const int rows = 2;
        const float slotW = static_cast<float> (slotArea.getWidth()) / static_cast<float> (cols);
        const float slotH = static_cast<float> (slotArea.getHeight()) / static_cast<float> (rows);
        const int r = slot / cols;
        const int c = slot % cols;
        return juce::Rectangle<float> (slotArea.getX() + static_cast<float> (c) * slotW,
                                       slotArea.getY() + static_cast<float> (r) * slotH,
                                       slotW, slotH);
    }

    std::vector<std::unique_ptr<ParamHandle>> orderHandles;
    std::vector<std::unique_ptr<ParamHandle>> onHandles;
    std::vector<std::unique_ptr<ParamHandle>> rackHandles;

    juce::TextButton moveLeftButton;
    juce::TextButton moveRightButton;
    juce::TextButton bypassAllButton;
    juce::TextButton powerButton;
    LongPressDetector press;
    bool reorderArmed = false;

    int selectedIndex = 0;
    int dragSourceIndex = -1;
    int dropTargetIndex = -1;
    bool isDraggingSlot = false;
    uint32_t savedOnMask = 0;
};
} // namespace synthyboy::ui
