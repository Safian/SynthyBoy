// SynthyBoy - MPC overlay: the MACROS page as BINDING DATA (owner decision 2026-11-27: the C++ layer has no UI elements). The page of the Macros tab: the studio spectrum across the upper area and all eight macros as native cards in
// one bottom row (components/native/macro_card: the dial, the value and the target chips are native cells of the host skin). This page holds the parameter rows macro.1 .. macro.8 (id, key, Q slot), the Q banks as a table, the
// geometry of the native cells (ui/macro_card_contract.h), the targets page of a macro as plain rows over the modulation slots (the native bridge nativeui::targets), the add-target list as plain data, the name of a card (a live
// text the overlay draws; a tap on it opens the rename page) and the displays the overlay draws itself: the spectrum, the title and the names. One object per (page view, plugin instance). Pure: no OS headers, no threads of its own.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Q groups: fixed hardware mapping: Bank 1 (Q 1..4) = macro.1 .. macro.4, Bank 2 (Q 5..8) = macro.5 .. macro.8 (16-knob XL maps all 8 directly; 4-knob devices switch physically between Bank 1 and Bank 2). Touching a card or turning
// a hardware bank knob on a 4-knob device moves the visual bank focus without swapping Bank 1/2 Q-Link slot targets. On the targets page: the depth, polarity and switch of the active row.
//
// Kept until the native replacements are proven (docs/review/OVERLAY-CONTROLS-REMOVAL-2026-11-27.md section 8): the ADD TARGET list (kPageList with listCount () / listName () / listPick (): the data of the native two-level picker, the shared picker of the Mod tab (nativeui::modboard) published from here).
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../native_mod_targets.h"
#include "../native_ui_params.h"
#include "../q_router.h"
#include "../ui/binding.h"
#include "../ui/macro_card_contract.h"
#include "../ui/native_apertures.h"
#include "../ui/qgroup.h"
#include "../native_text_entry.h"
#include "../ui/theme.h"
#include "instance_view.h"
#include "native_marker_covers.h"
#include "macro_names.h"
#include "page_view.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "spectrum_curve.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class MacroUi
{
public:
    static constexpr int kElementPage = 8;           // the page of the Macros tab
    static constexpr int kElementMacros = kElementPage;
    enum Page { kPageTargets = 50, kPageList = 51, kPageKeyboard = 52 };

    enum Id : int
    {
        kMacroKnobBase = 100,    // 100 .. 107
        kMacroCardBase = 200,    // 200 .. 207: the chips/target of a card ask for the targets of the macro
        kTargetsBack = 300, kTargetsPage, kTargetsAdd, kTargetsRow = 310,   // 310 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kList = 400, kListBackdrop, kListHeader,
        kMacroNameBase = 500    // 500 .. 507: the name of a card (a tap opens the keyboard page)
    };
    using ModRow = uibind::ModRow;

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kNumMacros = 8, kBanks = 2, kPerPage = nativeui::targets::kRows;
    static constexpr double kModsRefreshMs = 100.0;

    // ---- the parameter rows: the eight macro dials (row i = macro i + 1), then the eight target chips and the eight names (commands: no engine parameter)
#define SB_MACRO_KNOB(N, G, S) binding::Row { 100 + (N) - 1, "macro." #N, binding::Kind::Continuous, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, G, S, false }
#define SB_MACRO_CMD(ID) binding::Row { ID, "", binding::Kind::Command, 0, 0, 0, 0, binding::Format::Engine, nullptr, 0, -1, -1, false }
    static constexpr binding::Row kRows[3 * kNumMacros] = {
        SB_MACRO_KNOB (1, 0, 0), SB_MACRO_KNOB (2, 0, 1), SB_MACRO_KNOB (3, 0, 2), SB_MACRO_KNOB (4, 0, 3),
        SB_MACRO_KNOB (5, 1, 0), SB_MACRO_KNOB (6, 1, 1), SB_MACRO_KNOB (7, 1, 2), SB_MACRO_KNOB (8, 1, 3),
        SB_MACRO_CMD (200), SB_MACRO_CMD (201), SB_MACRO_CMD (202), SB_MACRO_CMD (203), SB_MACRO_CMD (204), SB_MACRO_CMD (205), SB_MACRO_CMD (206), SB_MACRO_CMD (207),
        SB_MACRO_CMD (500), SB_MACRO_CMD (501), SB_MACRO_CMD (502), SB_MACRO_CMD (503), SB_MACRO_CMD (504), SB_MACRO_CMD (505), SB_MACRO_CMD (506), SB_MACRO_CMD (507)
    };
#undef SB_MACRO_CMD
#undef SB_MACRO_KNOB
    /** The Q banks as row indices: bank 1 = macros 1 .. 4, bank 2 = macros 5 .. 8. */
    static constexpr binding::QTable<kBanks> kQ { { { 0, 1, 2, 3 }, { 4, 5, 6, 7 } } };

    // ---- the geometry of the native cells (ui/macro_card_contract.h, tab coordinates; the page is placed at kTabY) --------------------------------------------------------------------------------------------
    static ui::Rect cardRect (int m) noexcept
    {
        const auto& p = ui::macrocard::kCardPositions[static_cast<size_t> (std::clamp (m, 0, kNumMacros - 1))];
        return { p.x, kTabY + p.y, ui::macrocard::kCardW, ui::macrocard::kCardH };
    }
    static ui::Rect inCard (int m, const ui::Rect& r) noexcept { const ui::Rect c = cardRect (m); return { c.x + r.x, c.y + r.y, r.w, r.h }; }
    static ui::Rect nativeKnobArea (int m) noexcept { return inCard (m, ui::macrocard::kNativeKnobRect); }
    static ui::Rect nativeTargetArea (int m) noexcept { return inCard (m, ui::macrocard::kTargetRect); }
    static ui::Rect nameLabelRect (int m) noexcept { return inCard (m, ui::macrocard::kNameBoxRect); }
    /** The touch target of the name below the value (56 pixels high, non-overlapping). */
    static ui::Rect nameRect (int m) noexcept { return inCard (m, ui::macrocard::kNameHitRect); }
    static ui::Rect spectrumRect() noexcept { const auto& r = ui::macrocard::kSpectrumRect; return { r.x, kTabY + r.y, r.w, r.h }; }
    static ui::Rect titleRect() noexcept { const auto& r = ui::macrocard::kTitleRect; return { r.x, kTabY + r.y, r.w, r.h }; }
    /** The macro whose native dial lies at (x, y), -1 none. */
    static int nativeDialAt (int x, int y) noexcept
    {
        for (int m = 0; m < kNumMacros; ++m)
            if (nativeKnobArea (m).contains (x, y))
                return m;
        return -1;
    }
    // ---- the native picker of the add-target list (tokens mod_picker.*, generated into ui/macro_card_contract.h; tab coordinates, the page is placed at kTabY)
    static ui::Rect pickerRect (const ui::Rect& r) noexcept { return { r.x, kTabY + r.y, r.w, r.h }; }
    static ui::Rect pickerRow (int row) noexcept
    {
        const ui::Rect& r = ui::macrocard::kPickerRow;
        return pickerRect ({ r.x, r.y + row * ui::macrocard::kPickerRowStep, r.w, r.h });
    }
    static bool isNativeControl (int x, int y) noexcept
    {
        for (int m = 0; m < kNumMacros; ++m)
            if (nativeTargetArea (m).contains (x, y))
                return true;
        return nativeDialAt (x, y) >= 0;
    }

    // ---- the native cells of the targets page (tab coordinates, as in skin/TUI.json)
    static constexpr int kTgRowTop = 116, kTgStride = 72, kTgH = 56;
    static ui::Rect tgBack() noexcept { return { 4, kTabY + 32, 120, kTgH }; }
    static ui::Rect tgAdd() noexcept { return { 1076, kTabY + 32, 200, kTgH }; }
    static ui::Rect tgPage() noexcept { return { 864, kTabY + 32, 208, kTgH }; }
    static ui::Rect tgRowBand (int row) noexcept { return { 0, kTabY + kTgRowTop + row * kTgStride - 8, kCanvasW, kTgStride }; }
    static ui::Rect tgName (int row) noexcept { return { 8, kTabY + kTgRowTop + row * kTgStride, 324, kTgH }; }
    static ui::Rect tgDepth (int row) noexcept { return { 344, kTabY + kTgRowTop + row * kTgStride, 712, kTgH }; }
    static ui::Rect tgPolarity (int row) noexcept { return { 1060, kTabY + kTgRowTop + row * kTgStride, 72, kTgH }; }
    static ui::Rect tgBypass (int row) noexcept { return { 1136, kTabY + kTgRowTop + row * kTgStride, 72, kTgH }; }
    static ui::Rect tgRemove (int row) noexcept { return { 1212, kTabY + kTgRowTop + row * kTgStride, 64, kTgH }; }

    /** The targets page of a macro: the visible window of at most kPerPage entries over the modulation slots whose source is the macro. */
    struct TargetEntry
    {
        char name[40] = {};
        double depth = 0.0;                    // -1 .. +1 of the range of the target
        bool bidirectional = false, bypassed = false;
    };
    struct Targets
    {
        TargetEntry rows[kPerPage];
        int numRows = 0, activeRow = -1, total = 0, pageIndex = 0, pages = 1;
        char title[24] = {};
    };

    MacroUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        for (int i = 0; i < kNumMacros; ++i)
        {
            macroParams_[i] = uibind::indexOfKey (kRows[i].key);
            macroSource_[i] = uibind::macroSource (i + 1);
        }
        setNativeControls (true);
    }

    void setNativeControls (bool on) { nativeControls_ = on; }
    bool nativeControls() const noexcept { return nativeControls_; }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
    }

    /** Reads the instance: the eight macros, what each drives, the spectrum of the output, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input&)
    {
        const auto generation = inst.stateGeneration();
        if (presetSeen_ && generation != presetGeneration_)
        {
            if (stack.element() == kElementPage && stack.depth() > 1)
                stack.openFull (kElementPage);
            dropGesture();
            resetSpectrum();
            modsDirty_ = true;
            macroInit_ = false;
        }
        presetGeneration_ = generation;
        presetSeen_ = true;
        const bool nativeTargets = nativeControls_ && inst.nativeMacroTargetsSupported();
        const bool nativeRoot = nativeTargets && stack.depth() == 1;
        inst.publishNativeMacroTargets (nativeRoot);
        if (nativeRoot)
        {
            const int macro = inst.takeNativeMacroTarget();
            if (macro >= 0 && macro < kNumMacros)
            {
                setBank (macro / 4);
                openTargets (macro, stack, inst);
                inst.publishNativeMacroTargets (false);
            }
        }
        const int top = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        if (top != pageTop_)
        {
            if (gestureDown_)
                dropGesture();
            if (top == kPageTargets && ! (pageTop_ == kPageList && underList_ == kPageTargets))
                targetsPageIndex_ = 0;
            else if (top == kPageList)
            {
                listGroup_ = -1;
                listFirst_ = 0;
            }
            else if (top == kPageKeyboard)
                beginKeyboard (inst);
            if (pageTop_ == kPageKeyboard && top != kPageKeyboard)
                closeKeyboard (inst);
            pageTop_ = top;
            qSlots_.forget();
            modsDirty_ = true;
        }
        if (pageTop_ == kPageKeyboard)
            updateKeyboard (inst, stack);
        const bool namesChanged = names_.load (inst.content().get());
        (void) namesChanged;
        const unsigned long qWrites = inst.qWriteCount();
        int hwBankMask = 0;
        for (int m = 0; m < kNumMacros; ++m)
        {
            if (macroParams_[m] < 0)
                continue;
            const float v = inst.param (macroParams_[m]);
            if (macroInit_ && ! ui::QGroup::wideDevice() && qWrites != lastQWriteCount_ && std::fabs (v - prevMacroVal_[m]) > 1.0e-4f)
                hwBankMask |= (1 << (m / 4));
            prevMacroVal_[m] = v;
        }
        if (macroInit_ && ! ui::QGroup::wideDevice() && qWrites != lastQWriteCount_)
        {
            if (hwBankMask == 1)
                setBank (0);
            else if (hwBankMask == 2)
                setBank (1);
        }
        lastQWriteCount_ = qWrites;
        macroInit_ = true;
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
                loadTargets (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        const auto changes = stack.changes();
        updateNativeTargets (inst, stack);
        if (stack.changes() != changes)
        {
            pageTop_ = stack.depth() >= 2 ? stack.top() : 0;
            qSlots_.forget();
            if (pageTop_ == kPageList)
                listGroup_ = -1;
        }
        updateNativeList (inst, stack);
        if (pageTop_ == 0)
            updateSpectrum (inst, nowMs);
        publishNativeText (inst);
        inst.setUiFullOpen (pageTop_ != 0 || ! nativeControls_);
        publishQGroup (inst);
    }

    void setHostBackground (bool on) noexcept { nativeGround_ = on; }
    bool hostBackground() const noexcept { return pageTop_ == 0 && nativeGround_; }
    /** The cover of the host's stale pixels (page_view_base.h drawGround; it is no drawing of the page): until the sentinel of the host's background is read the cards page is covered by the canvas colour around the native cells (the cards, the
        title and the spectrum frame), and the marker squares and the instance picture of the skin always (they lie under the transparent plane). Everything else of the page, the ground of the targets, list and keyboard pages
        included, is a native element of the skin gated by nativeui::kMacroSubpage. */
    void drawPageGround (gpu::SceneBuilder& sb) const
    {
        if (pageTop_ == 0 && ! hostBackground())
            drawNativeFallback (sb);
        ui::Cell cell (sb, { 0, kTabY, kCanvasW, ui::macrocard::kBarH }, 0);
        drawNativeMarkerCovers (sb, "macros");
    }

    /** The picture: the live line of the spectrum (a display). The rename keyboard is the native one of the skin. */
    void draw (gpu::SceneBuilder& sb) const
    {
        if (pageTop_ == 0)
            drawSpectrumLive (sb, spectrumRect(), spectrumLevels_);
    }

    /** True if (x, y) lies in a native cell of the targets page: the touch belongs to the host (`row` gets the row of a row cell, -1 for the toolbar). */
    bool targetsNativeHit (int x, int y, int* row = nullptr) const noexcept
    {
        if (! targetsNative_)
            return false;
        if (row != nullptr)
            *row = -1;
        if (tgBack().contains (x, y) || tgAdd().contains (x, y) || (targets_.pages > 1 && tgPage().contains (x, y)))
            return true;
        for (int i = 0; i < targets_.numRows; ++i)
            if (tgDepth (i).contains (x, y) || tgPolarity (i).contains (x, y) || tgBypass (i).contains (x, y) || tgRemove (i).contains (x, y))
            {
                if (row != nullptr)
                    *row = i;
                return true;
            }
        return false;
    }

    bool covers (int x, int y) const noexcept
    {
        if (pageTop_ == kPageKeyboard)
            return false;   // the keyboard is the host's (native keys of the skin)
        if (pageTop_ == kPageList && listNative_)
            return false;   // the picker is the host's
        if (pageTop_ == kPageTargets && targetsNativeHit (x, y))
            return false;
        if (! ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y))
            return false;
        if (pageTop_ == 0 && nativeControls_ && isNativeControl (x, y))
            return false;
        return true;
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        if (ev.kind == TouchEvent::Down)
        {
            int targetRow = -1;
            if (pageTop_ == kPageTargets && targetsNativeHit (x, y, &targetRow))
            {
                dropGesture();
                if (targetRow >= 0)
                {
                    selectRow (targetRow);
                    publishQGroup (inst);
                }
                return false;
            }
            gestureDown_ = false;
            nameDown_ = -1;
            int macro;
            if (pageTop_ == 0 && nativeControls_ && (macro = nativeDialAt (x, y)) >= 0)
            {
                setBank (macro / 4);
                publishQGroup (inst);
                return false;
            }
            if (! covers (x, y))
                return false;
            gestureDown_ = true;
            if (pageTop_ == kPageList)
                listBackdrop (stack);   // a host without the native picker draws no list: a tap closes the page, as a tap on the backdrop did
            else if (pageTop_ == kPageTargets)
            {
                for (int i = 0; i < targets_.numRows; ++i)
                    if (tgRowBand (i).contains (x, y))
                    {
                        selectRow (i);
                        break;
                    }
            }
            else
            {
                for (int m = 0; m < kNumMacros; ++m)
                    if (nameRect (m).contains (x, y))
                    {
                        nameDown_ = m;
                        setBank (m / 4);
                        break;
                    }
                publishQGroup (inst);
            }
            return true;
        }
        if (! gestureDown_)
            return false;
        if (ev.kind != TouchEvent::Move)
        {
            const int m = nameDown_;
            nameDown_ = -1;
            gestureDown_ = false;
            if (m >= 0 && pageTop_ == 0 && nameRect (m).contains (x, y) && inst.stateGeneration() == presetGeneration_)   // a tap on the name of a card: the keyboard page
            {
                editMacro_ = m;
                stack.openDetail (kPageKeyboard);
            }
        }
        return true;
    }

    void tick (double, UiStack&, IInstanceView&) {}

    bool wheel (int, int, int, UiStack&, IInstanceView&, double) { return false; }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gestureDown_ = false; nameDown_ = -1; }
    void leftTab (IInstanceView& inst) noexcept
    {
        inst.publishNativeMacroTargets (false);
        hideNativeTargets (inst);
        hideNativeList (inst);
        resetSpectrum();
        inst.setUiFullOpen (false);
    }
    /** The page comes on screen again (another view was shown): it starts on BANK 1 (macros 1 .. 4) every time, so what the page shows and what the knobs write on arrival never depend on a bank left from an earlier visit; the group is published again. */
    void forgetQGroup() noexcept { setBank (0); qSlots_.forget(); }

    /** The hardware knob `knobIndex` (0 .. 3) turned by `delta` (normalised units): on the macros page the macro of that slot in the focused bank; on the targets page the depth, polarity or switch of the active row. */
    void turnQ (int knobIndex, double delta, UiStack&, IInstanceView& v)
    {
        if (v.stateGeneration() != presetGeneration_)
            return;
        if (pageTop_ == kPageTargets)
        {
            if (targets_.activeRow < 0 || knobIndex < 0 || knobIndex > 2)
                return;
            const int slot = targetRows_[targets_.activeRow].slot;
            if (slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            if (knobIndex == 0)
                uibind::writeNorm (v, sp.amount, binding::turnNorm (uibind::normOf (sp.amount, v.param (sp.amount)), delta));
            else if (knobIndex == 1)
                uibind::writePlain (v, sp.bipolar, delta > 0.0 ? 1.0 : 0.0);
            else
                uibind::writePlain (v, sp.bypass, delta > 0.0 ? 0.0 : 1.0);   // the switch says ON (active): bypassed is its opposite
            modsDirty_ = true;
            return;
        }
        if (pageTop_ != 0)
            return;
        const int r = kQ.row (bank_, knobIndex);
        if (r >= 0 && macroParams_[r] >= 0)
            uibind::writeNorm (v, macroParams_[r], binding::turnNorm (uibind::normOf (macroParams_[r], v.param (macroParams_[r])), delta));
    }

    /** The native bridge: a native dial of the card wrote `value` (normalised) into macro `macro` (0 .. 7). */
    void writeCell (int id, double value, IInstanceView& v) const
    {
        const int m = id - kMacroKnobBase;
        if (m >= 0 && m < kNumMacros && macroParams_[m] >= 0)
            uibind::writeNorm (v, macroParams_[m], value);
    }

    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return pageTop_; }
    /** The macro whose targets page is open (0 .. 7), -1 none. */
    int targetsOf() const noexcept { return pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets) ? targetMacro_ : -1; }

    /** The text a card carries as its name: "MACRO n" for the default, else the name in capitals (the style of the labels of the pages). */
    std::string cardName (int m) const
    {
        if (! names_.custom (m))
            return "MACRO " + std::to_string (m + 1);
        std::string s = names_.full (m);
        for (char& c : s)
            c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
        return s;
    }
    const Targets& targets() const noexcept { return targets_; }
    const ModRow& targetRow (int i) const noexcept { return targetRows_[std::clamp (i, 0, kPerPage - 1)]; }
    int bank() const noexcept { return bank_; }
    void setBank (int b) noexcept { bank_ = b == 1 ? 1 : 0; }
    const SpectrumCurve* spectrum() const noexcept { return curve_.get(); }

    int listCount() const noexcept
    {
        const uibind::DestIndex& d = uibind::DestIndex::get();
        return listGroup_ < 0 ? static_cast<int> (d.groups.size()) : static_cast<int> (d.choices[static_cast<size_t> (listGroup_)].size());
    }
    /** The choice value of entry `i` for the picker (ModUi's coding: a group is -1 - group, a destination its choice). */
    int listChoice (int i) const noexcept
    {
        const uibind::DestIndex& d = uibind::DestIndex::get();
        if (i < 0 || i >= listCount())
            return 0;
        return listGroup_ < 0 ? -1 - i : d.choices[static_cast<size_t> (listGroup_)][static_cast<size_t> (i)];
    }
    /** An entry of the list picked: a group opens its parameters, a parameter makes a new slot for the macro and returns to the targets page. */
    void listPick (int index, UiStack& stack, IInstanceView& v)
    {
        if (pageTop_ != kPageList || v.stateGeneration() != presetGeneration_ || index < 0 || index >= listCount())
            return;
        if (listGroup_ < 0)
        {
            listGroup_ = index;
            listFirst_ = 0;
            return;
        }
        addTarget (v, uibind::DestIndex::get().choices[static_cast<size_t> (listGroup_)][static_cast<size_t> (index)]);
        stack.back();
    }
    /** The header of the list (back to the groups) or the backdrop (close). */
    void listHeader() noexcept { listGroup_ = -1; listFirst_ = 0; }
    void listBackdrop (UiStack& stack) { stack.back(); }

private:
    // ---- the native add-target list: the picker of the Mod tab (nativeui::modboard) published for this page, its taps applied through listPick / listHeader / listBackdrop
    void hideNativeList (IInstanceView& inst)
    {
        if (! listNative_)
            return;
        listNative_ = false;
        inst.setNativeModBoard (0, nullptr);
    }

    void updateNativeList (IInstanceView& inst, UiStack& stack)
    {
        namespace nb = nativeui::modboard;
        if (pageTop_ != kPageList || underList_ != kPageTargets || ! nativeControls_ || ! inst.nativeModTargetsSupported())
        {
            hideNativeList (inst);
            return;
        }
        const int count = listCount();
        listFirst_ = std::clamp (listFirst_, 0, std::max (0, count - 1));
        const int identity = macroSource_[std::clamp (targetMacro_, 0, kNumMacros - 1)] + 4000;
        std::array<nativeui::FxCell, nb::kHandles> fields {};
        for (int row = 0; row < nb::kListRows && listFirst_ + row < count; ++row)
        {
            fields[nb::ListRow + row] = { nb::kChoiceBias + listChoice (listFirst_ + row), identity, nativeui::kFxToggle };
            fields[nb::ListRowGate + row] = { 1, 1, nativeui::kFxToggle };
        }
        for (int field : { nb::ListBack, nb::ListPrev, nb::ListNext, nb::ListGroups, nb::ListGate, nb::ListTitle })
            fields[field] = { listGroup_ + 1, identity + listFirst_, nativeui::kFxToggle };
        inst.setNativeModBoard (2, fields.data());
        listNative_ = true;
        for (int field = nb::ListRow; field < nb::ListGate; ++field)
        {
            float value = 0.0f;
            if (! inst.takeNativeModBoard (field, fields[field], value) || value < 0.5f)
                continue;
            if (field < nb::ListBack)
                listPick (listFirst_ + field - nb::ListRow, stack, inst);
            else if (field == nb::ListBack)
                listBackdrop (stack);
            else if (field == nb::ListGroups)
                listHeader();
            else
                listFirst_ = std::clamp (listFirst_ + (field == nb::ListPrev ? -nb::kListRows : nb::kListRows), 0, std::max (0, ((count - 1) / nb::kListRows) * nb::kListRows));
            inst.setNativeModBoard (0, nullptr);   // the next update publishes the new rows
            return;
        }
    }

    // ---- the display the overlay draws itself (the frame of the spectrum, the title and the names are native cells of the skin)
    void drawNativeFallback (gpu::SceneBuilder& sb) const
    {
        std::array<ui::Rect, kNumMacros + 2> holes;
        for (int i = 0; i < kNumMacros; ++i)
            holes[static_cast<size_t> (i)] = cardRect (i);
        holes[kNumMacros] = titleRect();
        holes[kNumMacros + 1] = spectrumRect();
        ui::drawNativeCanvas (sb, { 0, kTabY, w_, kTabH }, holes);
    }

    /** The live line of the spectrum (inside the frame of the skin's native macroSpectrumFrame, the same inset): 256 levels 0 .. 1 on a log frequency axis (20 Hz .. 20 kHz) with a faint fill; null levels: nothing is drawn. */
    static void drawSpectrumLive (gpu::SceneBuilder& sb, const ui::Rect& rect, const float* levels)
    {
        if (levels == nullptr)
            return;
        const ui::theme::Look& T = ui::theme::L();
        const ui::Rect in { rect.x + 10, rect.y + 22, std::max (0, rect.w - 20), std::max (0, rect.h - 22 - 20) };
        if (in.w < 20 || in.h < 20)
            return;
        constexpr int kPoints = 256;
        float rows[gpu::kMaxCurve];
        const int n = std::min (kPoints, static_cast<int> (gpu::kMaxCurve));
        for (int i = 0; i < n; ++i)
            rows[i] = static_cast<float> (in.bottom()) - std::clamp (levels[i], 0.0f, 1.0f) * static_cast<float> (in.h - 4);
        bool sound = false;
        for (int i = 0; i < n && ! sound; ++i)
            sound = levels[i] > 0.0f;
        sb.curve (rows, n, in.x, in.w);
        if (sound)
            sb.fill (static_cast<float> (in.bottom()), ui::colour::rgba (T.flow[0], 40));
        sb.stroke (0.8f, ui::colour::rgba (T.flow[0], sound ? 255 : 90));   // no sound: only the faint base line
    }

    /** The texts and the gate the skin shows with native cells: the names of the cards, the title, the count and the hint of the targets page; the gate hides the cards page while a sub page is open. */
    void publishNativeText (IInstanceView& inst) const
    {
        namespace t = nativeui::nativetext;
        for (int m = 0; m < kNumMacros; ++m)
            inst.setNativeMacroText (t::MacroName0 + m, cardName (m).c_str());
        const bool targets = pageTop_ == kPageTargets;
        char count[32] = {};
        if (targets)
            std::snprintf (count, sizeof count, targets_.total == 1 ? "%d TARGET" : "%d TARGETS", targets_.total);
        inst.setNativeMacroText (t::MacroTargetsTitle, targets ? targets_.title : "");
        inst.setNativeMacroText (t::MacroTargetsCount, count);
        inst.setNativeMacroText (t::MacroTargetsEmpty, targets && targets_.numRows == 0 ? "THIS SOURCE MODULATES NOTHING YET: PRESS ADD TARGET" : "");
        inst.setNativeMacroSubpage (pageTop_ != 0);
    }

    void resetSpectrum() noexcept
    {
        if (curve_)
            curve_->reset();
        lastSpectrumMs_ = -1.0;
        spectrumLevels_ = nullptr;
    }

    void updateSpectrum (IInstanceView& inst, double nowMs)
    {
        const SpectrumRing* ring = inst.spectrumRing();
        if (ring == nullptr)
        {
            spectrumLevels_ = nullptr;
            return;
        }
        if (! curve_)
            curve_ = std::make_unique<SpectrumCurve> (inst.sampleRate());
        const double dt = lastSpectrumMs_ < 0.0 ? 33.3 : nowMs - lastSpectrumMs_;
        if (lastSpectrumMs_ >= 0.0 && dt < kSpectrumMinStepMs)
            return;
        if (lastSpectrumMs_ >= 0.0 && dt > 600.0)
            curve_->reset();
        lastSpectrumMs_ = nowMs;
        curve_->update (*ring, inst.sampleRate(), dt);
        spectrumLevels_ = curve_->level();
    }

    // ---- the targets of one macro (the same page as the envelope's and the LFO's)
    void selectRow (int i) noexcept { targets_.activeRow = i >= 0 && i < targets_.numRows ? i : -1; }

    void openTargets (int macro, UiStack& stack, const IInstanceView& v)
    {
        if (v.stateGeneration() != presetGeneration_)
            return;
        targetMacro_ = macro;
        stack.openDetail (kPageTargets);
    }

    void loadTargets (const IInstanceView& v)
    {
        const int macro = std::clamp (targetMacro_, 0, kNumMacros - 1);
        const int source = macroSource_[macro];
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, source, slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + kPerPage - 1) / kPerPage);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * kPerPage, n = std::clamp (total - first, 0, kPerPage);
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (std::lround (v.param (sp.destination))));
            TargetEntry& e = targets_.rows[i];
            std::snprintf (e.name, sizeof e.name, "%.39s", p >= 0 ? uibind::info (p).name : "-");
            e.depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            e.bidirectional = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            e.bypassed = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            targetRows_[i] = { slots[first + i], source };
        }
        for (int i = n; i < kPerPage; ++i)
            targetRows_[i] = {};
        targets_.numRows = n;
        targets_.total = total;
        targets_.pages = pages;
        targets_.pageIndex = targetsPageIndex_;
        std::snprintf (targets_.title, sizeof targets_.title, "%s", cardName (macro).c_str());
        selectRow (n > 0 ? std::min (std::max (targets_.activeRow, 0), n - 1) : -1);
    }

    void addTarget (IInstanceView& v, int destChoice)
    {
        const int source = macroSource_[std::clamp (targetMacro_, 0, kNumMacros - 1)];
        const int slot = uibind::addTargetSlot (v, source, destChoice);
        if (slot < 0)
            return;
        const int position = uibind::targetPosition (v, source, slot);
        targetsPageIndex_ = position / kPerPage;
        loadTargets (v);
        selectRow (position % kPerPage);
        modsDirty_ = true;
    }

    void removeTarget (int row, IInstanceView& v)
    {
        const int slot = row >= 0 && row < kPerPage ? targetRows_[row].slot : -1;
        if (slot < 0)
            return;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
        modsDirty_ = true;
        loadTargets (v);
    }

    // ---- the native bridge of the targets page (nativeui::targets): the cells the skin shows and the writes it reports
    void hideNativeTargets (IInstanceView& inst)
    {
        if (! targetsNative_)
            return;
        targetsNative_ = false;
        inst.hideNativeModTargets (this);
    }

    void updateNativeTargets (IInstanceView& inst, UiStack& stack)
    {
        namespace n = nativeui::targets;
        const int subject = macroSource_[std::clamp (targetMacro_, 0, kNumMacros - 1)];
        if (pageTop_ != kPageTargets || subject <= 0 || w_ != kCanvasW || h_ != kCanvasH || ! inst.nativeModTargetsSupported())
        {
            hideNativeTargets (inst);
            return;
        }
        targetsNative_ = true;
        const auto bind = [&]
        {
            n::State state;
            state.owner = n::Macros;
            state.mode = n::Targets;
            state.presenter = this;
            state.subject = subject;
            state.page = targets_.pageIndex;
            state.preset = inst.stateGeneration();
            state.bindings[n::Back] = { 0, 0, nativeui::kFxDial };
            state.bindings[n::Add] = { 0, 0, nativeui::kFxDial };
            if (targets_.pages > 1)
                state.bindings[n::Page] = { targets_.pages, state.page, nativeui::kFxDial };
            for (int row = 0; row < targets_.numRows; ++row)
            {
                const int slot = targetRows_[row].slot;
                if (slot < 0)
                    continue;
                const auto sp = synthyboy::modSlotParams (slot);
                if (sp.source < 0 || sp.destination < 0)
                    continue;
                const int src = static_cast<int> (std::lround (inst.param (sp.source)));
                const int dest = static_cast<int> (std::lround (inst.param (sp.destination)));
                const int route = src == subject ? dest : 0;
                if (route <= 0)
                    continue;
                state.bindings[n::field (row, n::Amount)] = { sp.amount, route, nativeui::kFxDial };
                state.bindings[n::field (row, n::Polarity)] = { sp.bipolar, route, nativeui::kFxToggle };
                state.bindings[n::field (row, n::Active)] = { sp.bypass, route, nativeui::kFxToggle };
                state.bindings[n::field (row, n::Remove)] = { slot, route, nativeui::kFxDial };
            }
            inst.publishNativeModTargets (state);
            return state;
        };
        const auto state = bind();
        const auto take = [&] (int field, float& value)
        {
            return state.bindings[field].target >= 0 && inst.stateGeneration() == state.preset && inst.takeNativeModTarget (state, field, value);
        };
        float value;
        if (take (n::Back, value))
        {
            hideNativeTargets (inst);
            stack.back();
            return;
        }
        if (take (n::Add, value))
        {
            hideNativeTargets (inst);
            underList_ = kPageTargets;
            stack.openDetail (kPageList);
            return;
        }
        if (take (n::Page, value))
        {
            const int index = state.page + (value > 0.5f ? 1 : -1);
            if (index >= 0 && index < targets_.pages)
            {
                inst.hideNativeModTargets (this);
                targetsPageIndex_ = index;
                loadTargets (inst);
                bind();
                return;
            }
        }
        for (int row = 0; row < targets_.numRows; ++row)
            if (take (n::field (row, n::Remove), value))
            {
                inst.hideNativeModTargets (this);
                selectRow (row);
                removeTarget (row, inst);
                bind();
                return;
            }
        for (int row = 0; row < targets_.numRows; ++row)
        {
            const int slot = targetRows_[row].slot;
            if (slot < 0)
                continue;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            if (take (n::field (row, n::Amount), value))
            {
                selectRow (row);
                uibind::writeNorm (inst, sp.amount, value);
                modsDirty_ = true;
            }
            if (take (n::field (row, n::Polarity), value))
            {
                selectRow (row);
                uibind::writePlain (inst, sp.bipolar, value >= 0.5f ? 1.0 : 0.0);
                modsDirty_ = true;
            }
            if (take (n::field (row, n::Active), value))
            {
                selectRow (row);
                uibind::writePlain (inst, sp.bypass, value >= 0.5f ? 0.0 : 1.0);   // the switch says ON (active): bypassed is its opposite
                modsDirty_ = true;
            }
        }
    }

    // ---- the keyboard page: the native keyboard of the skin (components/native/keyboard); the keys arrive as codes (native_text_entry.h), the text and the OK / CANCEL logic are plain data (kb_)
    /** The keyboard opens with the macro's own name (empty for the default). */
    void beginKeyboard (IInstanceView& inst)
    {
        const auto content = inst.content();
        const std::string current = content != nullptr && editMacro_ >= 0 && editMacro_ < synthyboy::kNumMacros ? content->macroNames[editMacro_] : std::string();
        char title[40];
        std::snprintf (title, sizeof title, "NAME MACRO %d", editMacro_ + 1);
        kb_.begin (title, current.c_str(), synthyboy::kMaxMacroNameChars);
        inst.clearNativeTextKeys();
        publishKeyboard (inst);
    }
    void closeKeyboard (IInstanceView& inst)
    {
        kb_.close();
        inst.clearNativeTextKeys();
        inst.setNativeTextEntry (0, "", "");
    }
    void publishKeyboard (IInstanceView& inst) const
    {
        inst.setNativeTextEntry (textentry::kCtxMacroFirst + editMacro_, kb_.title().c_str(), kb_.display (nativeui::nativetext::kLineLen).c_str());
    }
    /** Applies the keys the host delivered: the text, then OK (the name is the macro's from now on, "" is the default name) or CANCEL (the page closes either way). */
    void updateKeyboard (IInstanceView& inst, UiStack& stack)
    {
        bool edited = false;
        for (int key; (key = inst.takeNativeTextKey()) != 0;)
        {
            const auto outcome = kb_.press (key);
            if (outcome == textentry::Outcome::Edited)
                edited = true;
            else if (outcome == textentry::Outcome::Ok || outcome == textentry::Outcome::Cancel)
            {
                if (outcome == textentry::Outcome::Ok && inst.stateGeneration() == presetGeneration_)
                    inst.setMacroName (editMacro_, synthyboy::sanitizeMacroName (kb_.text(), editMacro_), presetGeneration_);
                closeKeyboard (inst);
                stack.back();
                return;
            }
        }
        if (edited)
            publishKeyboard (inst);
    }

    /** Fixed hardware Q-Link mapping: Bank 1 (Q 1..4) = macro.1..4, Bank 2 (Q 5..8) = macro.5..8 on both 4-knob and 16-knob devices; touching a card or focusing Bank 2 never swaps Bank 1/2 slots. */
    void publishQGroup (IInstanceView& v)
    {
        if (pageTop_ == kPageTargets)
        {
            std::snprintf (qName_, sizeof qName_, "MACRO %d TARGETS", targetMacro_ + 1);
            qSlots_.publish (v, targets_.numRows, std::max (0, targets_.activeRow), [this] (int row, int* t) { modRowTargets (row < kPerPage ? targetRows_[row].slot : -1, t); });
        }
        else if (pageTop_ == kPageList || pageTop_ == kPageKeyboard)   // no group: the knobs do nothing on these pages
        {
            std::snprintf (qName_, sizeof qName_, pageTop_ == kPageList ? "LIST" : "KEYBOARD");
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else
        {
            std::snprintf (qName_, sizeof qName_, bank_ == 0 ? "MACROS 1-4" : "MACROS 5-8");
            qSlots_.publish (v, kBanks, 0, [this] (int bank, int* t)
            {
                for (int k = 0; k < binding::QTable<kBanks>::kSlots; ++k)
                {
                    const int r = kQ.row (bank, k);
                    t[k] = r >= 0 ? macroParams_[r] : -1;
                }
            });
        }
    }

    static constexpr double kSpectrumMinStepMs = 16.0;
    int w_ = kCanvasW, h_ = kCanvasH;
    int pageTop_ = 0, underList_ = 0, listGroup_ = -1, targetMacro_ = 0, targetsPageIndex_ = 0, bank_ = 0;
    int macroParams_[kNumMacros] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int macroSource_[kNumMacros] = {};
    Targets targets_;
    ModRow targetRows_[kPerPage];
    bool targetsNative_ = false;

    MacroNames names_;
    int editMacro_ = 0, nameDown_ = -1;
    textentry::Model kb_;
    std::unique_ptr<SpectrumCurve> curve_;
    const float* spectrumLevels_ = nullptr;
    double lastSpectrumMs_ = -1.0, modsAtMs_ = -1000.0;
    bool modsDirty_ = true;
    bool gestureDown_ = false;
    bool listNative_ = false;
    int listFirst_ = 0;

    QSlots qSlots_;
    char qName_[32] = "MACROS 1-4";
    bool nativeGround_ = false;
    bool nativeControls_ = true;
    bool presetSeen_ = false;
    uint64_t presetGeneration_ = 0;
    bool macroInit_ = false;
    unsigned long lastQWriteCount_ = 0;
    float prevMacroVal_[kNumMacros] = {};
};

}} // namespace synthyboy_mpc::overlay
