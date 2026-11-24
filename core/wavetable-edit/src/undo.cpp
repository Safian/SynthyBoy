// SynthyBoy - wavetable-edit: Undo/Redo bounded command stack implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

namespace synthyboy::wtedit
{
UndoStack::UndoStack(size_t maxDepth)
    : maxDepth_(maxDepth > 0 ? maxDepth : 50)
{
}

void UndoStack::push(const Wavetable& wt)
{
    // Discard any redo states ahead of currentIndex_
    if (currentIndex_ >= 0 && static_cast<size_t>(currentIndex_ + 1) < history_.size())
    {
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(currentIndex_ + 1), history_.end());
    }

    history_.push_back(wt);
    currentIndex_ = static_cast<int>(history_.size()) - 1;

    // Prune oldest state if exceeding maxDepth_
    while (history_.size() > maxDepth_)
    {
        history_.erase(history_.begin());
        --currentIndex_;
    }
}

bool UndoStack::canUndo() const noexcept
{
    return currentIndex_ > 0;
}

bool UndoStack::canRedo() const noexcept
{
    return currentIndex_ >= 0 && static_cast<size_t>(currentIndex_ + 1) < history_.size();
}

bool UndoStack::undo(Wavetable& wt)
{
    if (!canUndo())
        return false;

    --currentIndex_;
    wt = history_[static_cast<size_t>(currentIndex_)];
    return true;
}

bool UndoStack::redo(Wavetable& wt)
{
    if (!canRedo())
        return false;

    ++currentIndex_;
    wt = history_[static_cast<size_t>(currentIndex_)];
    return true;
}

void UndoStack::clear() noexcept
{
    history_.clear();
    currentIndex_ = -1;
}

} // namespace synthyboy::wtedit
