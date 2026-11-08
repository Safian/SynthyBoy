// SynthyBoy - UI panel: FX Chain & Racks (docs/ui-design/08-effects.md, page `fx`).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
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
          bypassAllButton ("Bypass All")
    {
        for (int i = 0; i < 16; ++i)
        {
            const std::string orderKey = "fx.order." + std::to_string (i + 1);
            orderHandles.emplace_back (std::make_unique<ParamHandle> (ctx, orderKey));
            orderHandles.back()->onChange = [this] { repaint(); };

            onHandles.emplace_back (std::make_unique<ParamHandle> (ctx, kFxOnKeys[i]));
            onHandles.back()->onChange = [this] { repaint(); };

            rackHandles.emplace_back (std::make_unique<ParamHandle> (ctx, kFxRackKeys[i]));
            rackHandles.back()->onChange = [this] { repaint(); };
        }

        moveLeftButton.onClick = [this] { moveSelectedSlot (-1); };
        moveRightButton.onClick = [this] { moveSelectedSlot (1); };
        bypassAllButton.onClick = [this] { toggleBypassAll(); };

        addAndMakeVisible (moveLeftButton);
        addAndMakeVisible (moveRightButton);
        addAndMakeVisible (bypassAllButton);
    }

    int selectedSlot() const noexcept { return selectedIndex; }

    void setSelectedSlot (int index)
    {
        if (selectedIndex != index && index >= 0 && index < 16)
        {
            selectedIndex = index;
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

            g.setColour (isSelected ? t.colours.accent : t.colours.outline);
            g.drawRoundedRectangle (rect, t.px (4.0f), isSelected ? t.px (2.0f) : 1.0f);

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
        const int btnW = juce::jmax (t.touch() * 2, btnArea.getWidth() / 3 - t.gap());

        auto b = btnArea;
        moveLeftButton.setBounds (b.removeFromLeft (btnW));
        b.removeFromLeft (t.gap());
        moveRightButton.setBounds (b.removeFromLeft (btnW));
        bypassAllButton.setBounds (juce::Rectangle<int> (btnArea).removeFromRight (btnW));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int slot = slotAtPoint (e.getPosition());
        if (slot >= 0 && slot < 16)
        {
            setSelectedSlot (slot);
            dragSourceIndex = slot;
            dropTargetIndex = slot;

            // Check if power dot was tapped
            const auto slotRect = rectForSlot (slot);
            if (e.position.x < slotRect.getX() + ctx.theme.px (18.0f))
            {
                std::array<int, 16> currentOrder {};
                for (size_t i = 0; i < 16; ++i)
                    currentOrder[i] = juce::roundToInt (orderHandles[i]->plain());
                currentOrder = repairFxPermutation (currentOrder);

                const int fxId = currentOrder[static_cast<size_t> (slot)];
                if (fxId >= 0 && fxId < 16)
                {
                    const float currentVal = onHandles[static_cast<size_t> (fxId)]->plain();
                    onHandles[static_cast<size_t> (fxId)]->setPlainComplete (currentVal >= 0.5f ? 0.0f : 1.0f);
                    repaint();
                    return;
                }
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragSourceIndex >= 0)
        {
            isDraggingSlot = true;
            const int target = slotAtPoint (e.getPosition());
            if (target >= 0 && target < 16 && target != dropTargetIndex)
            {
                dropTargetIndex = target;
                repaint();
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (isDraggingSlot && dragSourceIndex >= 0 && dropTargetIndex >= 0 && dragSourceIndex != dropTargetIndex)
        {
            reorderSlots (dragSourceIndex, dropTargetIndex);
            setSelectedSlot (dropTargetIndex);
        }
        isDraggingSlot = false;
        dragSourceIndex = -1;
        dropTargetIndex = -1;
        repaint();
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

    int selectedIndex = 0;
    int dragSourceIndex = -1;
    int dropTargetIndex = -1;
    bool isDraggingSlot = false;
    uint32_t savedOnMask = 0;
};

class FxChainPanel final : public Panel
{
public:
    using Panel::Panel;

    juce::String title() const override { return "FX Chain & Racks"; }

    std::vector<std::string> keys() const override
    {
        std::vector<std::string> k;
        k.reserve (16);
        for (int i = 1; i <= 16; ++i)
            k.push_back ("fx.order." + std::to_string (i));
        return k;
    }

    void build() override
    {
        visual = std::make_unique<FxChainView> (ctx);
        addAndMakeVisible (*visual);

        // At most 4 primary controls (principle 1 of brief)
        const auto all = keys();
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i < 4)
                addControl (all[i]);
            else
                addMoreControl (all[i]);
        }
    }

    int preferredHeight (int width) const override
    {
        const auto& t = ctx.theme;
        const juce::Rectangle<int> area (0, 0, width - 2 * t.gap(), 100000);
        const int visualH = t.ipx (140.0f);
        return headerHeight() + 2 * t.gap() + visualH + t.gap()
               + const_cast<FxChainPanel*> (this)->layoutControls (area, true);
    }

    void resized() override
    {
        auto area = contentArea();
        const int visualH = ctx.theme.ipx (140.0f);
        if (visual != nullptr)
        {
            visual->setBounds (area.removeFromTop (visualH));
            area.removeFromTop (ctx.theme.gap());
        }
        layoutControls (area, false);
    }

private:
    std::unique_ptr<FxChainView> visual;
};

SYNTHYBOY_REGISTER_PANEL (FxChainPanel, Page::fx, 10)
} // namespace synthyboy::ui
