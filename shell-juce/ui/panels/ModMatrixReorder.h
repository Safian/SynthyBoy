// SynthyBoy - UI: reordering the slots of the modulation matrix (docs/ui-design/06 row "Matrix row handle reorders slots").
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The handle is the slot number at the left of a row: drag it up or down (the row swaps its content with the neighbour it passes), or tap it
// for the row menu (Move up / Move down). The panel implements this interface; the tests reach it with a dynamic_cast.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synthyboy::ui
{
class SlotReorder
{
public:
    virtual ~SlotReorder() = default;
    /** Swaps slot `slot` (0-based) with the next shown slot above (direction -1) or below (+1). Returns the slot the content is in now,
        -1 if there is no neighbour in that direction. */
    virtual int moveSlot (int slot, int direction) = 0;
    /** The row component of a slot (null for a slot out of range) and its handle in the row's coordinates. */
    virtual juce::Component* slotRowComponent (int slot) = 0;
    virtual juce::Rectangle<int> slotHandleBounds (int slot) = 0;
    virtual bool slotRowShown (int slot) = 0;
    /** Re-reads the slots into the rows and lays them out (the panel does it on its own timer; the tests call it). */
    virtual void refreshRows() = 0;
    /** True once the row menu of a handle tap has been shown (tests). */
    virtual bool rowMenuWasShown() const = 0;
};
} // namespace synthyboy::ui
