// SynthyBoy - MPC overlay: the FX UI (fx_ui.h).
// Connects the FxFullScreen component to the engine FX racks, type descriptors,
// parameter table, Q-groups, drag-and-drop reordering, and type add popup.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/fx_screen.h"
#include "../ui/list.h"
#include "../ui/ui.h"
#include "curve_source.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class FxUi
{
public:
    static constexpr int kElementFx = 3;
    static constexpr int kPageList = 35;

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    enum IdBase : int
    {
        kChainBase = 3000,
        kEditorBase = 4000,
        kList = 5000,
        kListBackdrop = 5001
    };

    FxUi()
    {
        screen_.assignIds (kChainBase, kEditorBase);
        setCanvas (kCanvasW, kCanvasH);
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        screen_.place (tab);
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
    }

    ui::FxFullScreen& screen() noexcept { return screen_; }
    const ui::FxFullScreen& screen() const noexcept { return screen_; }
    ui::ListPopup& list() noexcept { return list_; }
    const ui::ListPopup& list() const noexcept { return list_; }

    int selectedSlot() const noexcept { return selectedSlot_; }
    void selectSlot (int slot) noexcept
    {
        selectedSlot_ = std::max (0, slot);
    }

    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs)
    {
        (void) highlight;
        (void) stack;
        (void) nowMs;

        // 1. Read all engine parameters
        std::vector<float> plain (synthyboy::paramCount(), 0.0f);
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            plain[i] = inst.param (static_cast<int> (i));

        // 2. Discover modules from racks
        const synthyboy::FxRacks racks = synthyboy::fxRacks (plain.data());
        cardItems_.clear();

        for (int r = 0; r < synthyboy::kNumPoolRacks; ++r)
        {
            for (const synthyboy::FxRackItem& item : racks.rack[static_cast<size_t> (r)])
            {
                if (item.pool)
                {
                    if (synthyboy::fxItemType (plain.data(), item) != synthyboy::kFxTypeNone)
                        cardItems_.push_back (item);
                }
                else
                {
                    const bool on = synthyboy::fxItemOn (plain.data(), item);
                    const bool isSelected = (selectedItem_.index >= 0 && selectedItem_ == item);
                    if (on || isSelected)
                        cardItems_.push_back (item);
                }
                if (cardItems_.size() >= ui::FxChainBar::kMaxSlots)
                    break;
            }
            if (cardItems_.size() >= ui::FxChainBar::kMaxSlots)
                break;
        }

        if (cardItems_.empty())
        {
            selectedSlot_ = 0;
            selectedItem_ = { false, -1 };
        }
        else
        {
            int found = -1;
            for (size_t i = 0; i < cardItems_.size(); ++i)
            {
                if (cardItems_[i] == selectedItem_)
                {
                    found = static_cast<int> (i);
                    break;
                }
            }
            if (found >= 0)
            {
                selectedSlot_ = found;
            }
            else
            {
                selectedSlot_ = std::clamp (selectedSlot_, 0, static_cast<int> (cardItems_.size()) - 1);
                selectedItem_ = cardItems_[static_cast<size_t> (selectedSlot_)];
            }
        }

        // 3. Populate chain bar cards
        const int numActive = static_cast<int> (cardItems_.size());
        screen_.chainBar.setNumActiveSlots (numActive);

        for (int i = 0; i < numActive; ++i)
        {
            ui::FxSlotCard& card = screen_.chainBar.cards[i];
            const synthyboy::FxRackItem item = cardItems_[static_cast<size_t> (i)];
            const int type = synthyboy::fxItemType (plain.data(), item);
            card.state.slot = i;
            card.state.type = type;
            card.state.on = synthyboy::fxItemOn (plain.data(), item);
            card.state.rack = synthyboy::fxItemRack (plain.data(), item);
            card.state.selected = (i == selectedSlot_);
            card.state.empty = false;
            card.state.isPlus = false;
            const char* nm = synthyboy::fxItemName (plain.data(), item);
            std::strncpy (card.state.name, nm ? nm : "FX", sizeof (card.state.name) - 1);
            card.state.name[sizeof (card.state.name) - 1] = '\0';
            card.setEnabled (true);
        }

        screen_.chainBar.plusCard.state.isPlus = true;
        screen_.chainBar.plusCard.setEnabled (numActive < ui::FxChainBar::kMaxSlots);

        // 4. Update slot editor
        if (! cardItems_.empty() && selectedSlot_ < static_cast<int> (cardItems_.size()))
        {
            screen_.showEditor (true);
            const synthyboy::FxRackItem item = cardItems_[static_cast<size_t> (selectedSlot_)];
            const int type = synthyboy::fxItemType (plain.data(), item);
            screen_.slotEditor.setType (type);

            const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (type);
            for (int p = 0; p < desc.count && p < ui::FxSlotEditor::kMaxParams; ++p)
            {
                int paramIdx = -1;
                const synthyboy::ParamInfo* pi = nullptr;
                float normVal = 0.0f;
                float plainVal = 0.0f;

                if (item.pool)
                {
                    paramIdx = synthyboy::fxInstanceKeys (item.index).p[p];
                    pi = synthyboy::fxInstanceSource (type, p);
                    if (paramIdx >= 0)
                    {
                        normVal = inst.param (paramIdx);
                        plainVal = synthyboy::fxInstancePlain (type, p, normVal);
                    }
                }
                else
                {
                    paramIdx = desc.source[p];
                    if (paramIdx >= 0)
                    {
                        pi = &synthyboy::paramTable()[paramIdx];
                        plainVal = inst.param (paramIdx);
                        normVal = synthyboy::toNormalised (*pi, plainVal);
                    }
                }

                ui::Knob& knob = screen_.slotEditor.knobs[p];
                knob.state.value = normVal;
                if (pi != nullptr)
                {
                    knob.state.defaultValue = synthyboy::toNormalised (*pi, pi->def);
                    char buf[24];
                    synthyboy::valueToText (*pi, plainVal, buf, sizeof buf);
                    ui::setText (knob.state.valueText, sizeof knob.state.valueText, buf);
                    knob.setLabel (pi->name);
                }
                knob.setEnabled (true);
            }
        }
        else
        {
            screen_.showEditor (false);
        }

        // 5. Publish Q-Group
        publishQGroup (inst);
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        // 1. List popup active
        if (listOpen_)
        {
            ui::Change c;
            if (ev.kind == TouchEvent::Down) c = list_.touchDown (ev.x, ev.y, nowMs);
            else if (ev.kind == TouchEvent::Move) c = list_.touchMove (ev.x, ev.y);
            else if (ev.kind == TouchEvent::Up) c = list_.touchUp (ev.x, ev.y);

            if (c)
            {
                if (c.kind == ui::ChangeKind::Index)
                {
                    const int typeChoice = c.index + 1; // 1-based type
                    std::vector<float> plain (synthyboy::paramCount(), 0.0f);
                    for (size_t i = 0; i < synthyboy::paramCount(); ++i)
                        plain[i] = inst.param (static_cast<int> (i));

                    std::vector<synthyboy::FxWrite> writes;
                    const int newSlot = synthyboy::fxAddModule (plain.data(), typeChoice, addRack_, writes);
                    if (newSlot >= 0)
                    {
                        selectedItem_ = { true, newSlot };
                        for (const auto& w : writes)
                        {
                            const synthyboy::ParamInfo& pi = synthyboy::paramTable()[w.param];
                            inst.writeParam (w.param, synthyboy::toNormalised (pi, w.value));
                        }
                    }
                    listOpen_ = false;
                    stack.back();
                    return true;
                }
                else if (c.kind == ui::ChangeKind::Request) // backdrop
                {
                    listOpen_ = false;
                    stack.back();
                    return true;
                }
            }
            return true;
        }

        // 2. Main screen touch
        ui::Change c;
        if (ev.kind == TouchEvent::Down) c = screen_.touchDown (ev.x, ev.y, nowMs);
        else if (ev.kind == TouchEvent::Move) c = screen_.touchMove (ev.x, ev.y);
        else if (ev.kind == TouchEvent::Up) c = screen_.touchUp (ev.x, ev.y);

        if (c)
        {
            handleScreenChange (c, stack, inst);
            return true;
        }
        return false;
    }

    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        (void) nowMs; (void) stack; (void) inst;
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        screen_.draw (sb);
        if (listOpen_)
            list_.draw (sb);
    }

    bool listOpen() const noexcept { return listOpen_; }

private:
    int w_ = kCanvasW, h_ = kCanvasH;
    ui::FxFullScreen screen_;
    ui::ListPopup list_;
    bool listOpen_ = false;
    int addRack_ = 0; // 0 Main
    int selectedSlot_ = 0;
    synthyboy::FxRackItem selectedItem_ { false, -1 };
    std::vector<synthyboy::FxRackItem> cardItems_;

    static constexpr const char* const kTypeNames[] = {
        "Chorus", "Compressor", "Delay", "Distortion", "EQ", "FX Filter",
        "Flanger", "Phaser", "Reverb", "Hyper/Dimension", "Utility", "Bode", "Convolve"
    };
    static constexpr int kNumTypes = 13;

    void publishQGroup (IInstanceView& inst)
    {
        int targets[4] = { -1, -1, -1, -1 };
        const int grp = screen_.activeGroup();

        if (grp == 0) // Group 0: Chain
        {
            if (selectedSlot_ >= 0 && selectedSlot_ < static_cast<int> (cardItems_.size()))
            {
                std::vector<float> plain (synthyboy::paramCount(), 0.0f);
                for (size_t i = 0; i < synthyboy::paramCount(); ++i)
                    plain[i] = inst.param (static_cast<int> (i));

                const synthyboy::FxRackItem item = cardItems_[static_cast<size_t> (selectedSlot_)];
                const int type = synthyboy::fxItemType (plain.data(), item);
                const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (type);

                if (item.pool)
                {
                    targets[0] = synthyboy::fxInstanceKeys (item.index).order;
                    targets[1] = synthyboy::fxInstanceKeys (item.index).on;
                    targets[2] = desc.count > 0 ? synthyboy::fxInstanceKeys (item.index).p[0] : -1;
                    targets[3] = synthyboy::fxInstanceKeys (item.index).type;
                }
                else
                {
                    targets[0] = -1;
                    targets[1] = desc.onIndex;
                    targets[2] = desc.count > 0 ? desc.source[0] : -1;
                    targets[3] = -1;
                }
            }
        }
        else // Groups 1..6: Editor rows
        {
            const int r = grp - 1;
            if (selectedSlot_ >= 0 && selectedSlot_ < static_cast<int> (cardItems_.size()))
            {
                std::vector<float> plain (synthyboy::paramCount(), 0.0f);
                for (size_t i = 0; i < synthyboy::paramCount(); ++i)
                    plain[i] = inst.param (static_cast<int> (i));

                const synthyboy::FxRackItem item = cardItems_[static_cast<size_t> (selectedSlot_)];
                const int type = synthyboy::fxItemType (plain.data(), item);
                const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (type);

                for (int col = 0; col < 4; ++col)
                {
                    const int p = r * 4 + col;
                    if (p < desc.count)
                    {
                        if (item.pool)
                            targets[col] = synthyboy::fxInstanceKeys (item.index).p[p];
                        else
                            targets[col] = desc.source[p];
                    }
                }
            }
        }
        inst.setQGroup (targets, 4);
    }

    void handleScreenChange (const ui::Change& c, UiStack& stack, IInstanceView& inst)
    {
        // 1. Chain bar changes
        if (c.id >= kChainBase && c.id < kChainBase + ui::FxChainBar::kMaxSlots + 1)
        {
            const int cardIdx = c.id - kChainBase;
            if (cardIdx == ui::FxChainBar::kMaxSlots) // Plus card
            {
                if (c.kind == ui::ChangeKind::Index && c.index == ui::FxSlotCard::kAdd)
                {
                    list_.state.items = kTypeNames;
                    list_.state.count = kNumTypes;
                    list_.state.current = 0;
                    const ui::Rect btnRect = screen_.chainBar.plusCard.rect();
                    const int popW = 260;
                    const int popH = std::min (8, kNumTypes) * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad;
                    const int popX = std::clamp (btnRect.x - (popW - btnRect.w) / 2, 10, w_ - popW - 10);
                    const int popY = std::clamp (btnRect.bottom() + 4, kTabY + 10, kTabY + kTabH - popH - 10);
                    list_.place ({ popX, popY, popW, popH });
                    list_.showCurrent();
                    listOpen_ = true;
                    stack.openDetail (kPageList);
                }
            }
            else if (cardIdx >= 0 && cardIdx < static_cast<int> (cardItems_.size()))
            {
                if (c.kind == ui::ChangeKind::Index)
                {
                    if (c.index == ui::FxSlotCard::kSelect)
                    {
                        selectedSlot_ = cardIdx;
                        selectedItem_ = cardItems_[static_cast<size_t> (cardIdx)];
                        screen_.setActiveGroup (1); // Activate first row of parameters
                    }
                    else if (c.index == ui::FxSlotCard::kTogglePower)
                    {
                        std::vector<float> plain (synthyboy::paramCount(), 0.0f);
                        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
                            plain[i] = inst.param (static_cast<int> (i));

                        const synthyboy::FxRackItem item = cardItems_[static_cast<size_t> (cardIdx)];
                        std::vector<synthyboy::FxWrite> writes;
                        synthyboy::fxSetOn (plain.data(), item, c.value >= 0.5, writes);
                        for (const auto& w : writes)
                        {
                            const synthyboy::ParamInfo& pi = synthyboy::paramTable()[w.param];
                            inst.writeParam (w.param, synthyboy::toNormalised (pi, w.value));
                        }
                    }
                    else if (c.index == ui::FxSlotCard::kMove)
                    {
                        std::vector<float> plain (synthyboy::paramCount(), 0.0f);
                        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
                            plain[i] = inst.param (static_cast<int> (i));

                        const synthyboy::FxRackItem item = cardItems_[static_cast<size_t> (cardIdx)];
                        const int targetX = static_cast<int> (c.value);
                        const int cardW = screen_.chainBar.cards[0].rect().w;
                        const int gap = 6;
                        const int targetIndex = std::clamp ((targetX - screen_.chainBar.rect().x) / std::max (1, cardW + gap),
                                                            0, static_cast<int> (cardItems_.size()) - 1);

                        std::vector<synthyboy::FxWrite> writes;
                        const int rack = synthyboy::fxItemRack (plain.data(), item);
                        if (synthyboy::fxMove (plain.data(), item, rack, targetIndex, writes))
                        {
                            for (const auto& w : writes)
                            {
                                const synthyboy::ParamInfo& pi = synthyboy::paramTable()[w.param];
                                inst.writeParam (w.param, synthyboy::toNormalised (pi, w.value));
                            }
                            selectedSlot_ = targetIndex;
                            selectedItem_ = item;
                        }
                    }
                }
            }
        }
        // 2. Slot editor knob changes
        else if (c.id >= kEditorBase && c.id < kEditorBase + ui::FxSlotEditor::kMaxParams)
        {
            const int p = c.id - kEditorBase;
            if (selectedSlot_ >= 0 && selectedSlot_ < static_cast<int> (cardItems_.size()))
            {
                std::vector<float> plain (synthyboy::paramCount(), 0.0f);
                for (size_t i = 0; i < synthyboy::paramCount(); ++i)
                    plain[i] = inst.param (static_cast<int> (i));

                const synthyboy::FxRackItem item = cardItems_[static_cast<size_t> (selectedSlot_)];
                const int type = synthyboy::fxItemType (plain.data(), item);
                const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (type);

                if (p < desc.count)
                {
                    int paramIdx = -1;
                    if (item.pool)
                        paramIdx = synthyboy::fxInstanceKeys (item.index).p[p];
                    else
                        paramIdx = desc.source[p];

                    if (paramIdx >= 0)
                        inst.writeParam (paramIdx, static_cast<float> (c.value));
                }
            }
        }
    }
};

}} // namespace synthyboy_mpc::overlay
