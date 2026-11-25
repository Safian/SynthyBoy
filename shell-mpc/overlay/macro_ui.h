// SynthyBoy - MPC overlay: the MACROS UI, the page of the Macros tab (the plugin draws the whole tab, page_view.h): the studio spectrum across the upper area and all eight macros as compact cards in one bottom row
// (shell-mpc/ui/mod_macros.h) wired to the parameters macro.1 .. macro.8, with renameable name buttons, native target actions / chips, and the targets page of a macro (ModTargetsPage).
// One object per (page view, plugin instance). Pure: no OS headers, no threads of its own.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Q groups: fixed hardware mapping: Bank 1 (Q 1..4) = macro.1 .. macro.4, Bank 2 (Q 5..8) = macro.5 .. macro.8 (16-knob XL maps all 8 directly; 4-knob devices switch physically between Bank 1 and Bank 2).
// Touching a card or turning a hardware bank knob on a 4-knob device updates the Gen 1 visual bank focus without swapping Bank 1/2 Q-Link slot targets. On the targets page: the depth, polarity and switch of the active row.
// The name of a macro is a button below its value: a tap opens the keyboard page (ui/text_entry.h, a page of the stack like the targets page; no Q group), OK stores the trimmed name through
// IInstanceView::setMacroName, CANCEL and every other way out leave it.
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

#include "../q_router.h"
#include "../ui/list.h"
#include "../ui/mod_macros.h"
#include "../ui/mod_targets.h"
#include "../ui/native_apertures.h"
#include "../ui/text_entry.h"
#include "instance_view.h"
#include "native_marker_covers.h"
#include "macro_names.h"
#include "page_view.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "spectrum_curve.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "native_mod_targets_ui.h"
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
        kMacroNameBase = 500,    // 500 .. 507: the name of a card (a tap opens the keyboard page)
        kKeyboardOk = 520, kKeyboardCancel
    };
    using ModRow = uibind::ModRow;

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr double kModsRefreshMs = 100.0;

    MacroUi()
    {
        setCanvas (kCanvasW, kCanvasH);
        assignIds();
        for (int i = 0; i < 8; ++i)
        {
            char key[16];
            std::snprintf (key, sizeof key, "macro.%d", i + 1);
            macroParams_[i] = uibind::indexOfKey (key);
            macroSource_[i] = uibind::macroSource (i + 1);
        }
        setNativeControls (true);
    }

    void setNativeControls (bool on)
    {
        nativeControls_ = on;
    }
    bool nativeControls() const noexcept { return nativeControls_; }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        view_.place (tab);
        targets_.place (tab);
        entry_.place (tab);
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
        list_.setHeaderId (kListHeader);
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
            if (macro >= 0 && macro < ui::MacroView::kNumMacros)
            {
                view_.setBank (macro / 4);
                handle (ui::Change::request (kMacroCardBase + macro), stack, inst);
                inst.publishNativeMacroTargets (false);
            }
        }
        const int top = stack.depth() >= 2 ? stack.id (stack.depth() - 1) : 0;
        if (top != pageTop_)
        {
            if (gesture_ != nullptr)
                dropGesture();
            if (top == kPageTargets && !(pageTop_ == kPageList && underList_ == kPageTargets))
                targetsPageIndex_ = 0;
            else if (top == kPageList)
                listGroup_ = -1;
            else if (top == kPageKeyboard)
                beginKeyboard (inst);
            pageTop_ = top;
            qSlots_.forget();
            modsDirty_ = true;
            if (top == kPageList)
                fillList();
        }
        const bool namesChanged = names_.load (inst.content().get());
        const unsigned long qWrites = inst.qWriteCount();
        int hwBankMask = 0;
        for (int m = 0; m < 8; ++m)
        {
            if (macroParams_[m] < 0)
                continue;
            const float v = inst.param (macroParams_[m]);
            if (macroInit_ && ! ui::QGroup::wideDevice() && qWrites != lastQWriteCount_ && std::fabs (v - prevMacroVal_[m]) > 1.0e-4f)
                hwBankMask |= (1 << (m / 4));
            if (! macroInit_ || v != prevMacroVal_[m])
            {
                prevMacroVal_[m] = v;
                char text[16];
                uibind::formatValue (uibind::info (macroParams_[m]), v, text, sizeof text);
                view_.cards[m].setValue (uibind::normOf (macroParams_[m], v), text);
            }
            if (! macroInit_ || namesChanged)
                view_.cards[m].setMacro (m, cardName (m).c_str());
        }
        if (macroInit_ && ! ui::QGroup::wideDevice() && qWrites != lastQWriteCount_)
        {
            if (hwBankMask == 1)
                view_.setBank (0);
            else if (hwBankMask == 2)
                view_.setBank (1);
        }
        lastQWriteCount_ = qWrites;
        macroInit_ = true;
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
                loadTargets (inst, false);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        const auto changes = stack.changes();
        nativeTargets_.update (inst, targets_, pageTop_ == kPageTargets, nativeui::targets::Macros,
                               macroSource_[std::clamp (targetMacro_, 0, 7)], targetRows_,
                               [&] (ui::Change change) { handle (change, stack, inst); });
        if (stack.changes() != changes)
        {
            pageTop_ = stack.depth() >= 2 ? stack.top() : 0;
            qSlots_.forget();
            if (pageTop_ == kPageList)
            {
                listGroup_ = -1;
                fillList();
            }
        }
        if (pageTop_ == 0)
            updateSpectrum (inst, nowMs);
        inst.setUiFullOpen (pageTop_ != 0 || ! nativeControls_);
        publishQGroup (inst);
    }

    void setHostBackground (bool on) noexcept { nativeGround_ = on; }
    bool hostBackground() const noexcept { return pageTop_ == 0 && nativeGround_; }
    // Macros draws its ground here, with apertures where it shares the page with the host.
    void drawPageGround (gpu::SceneBuilder&) const noexcept {}

    /** The three opaque cover patches of the host-background mode: the skin's two marker squares and the instance picture (6 x 6, opaque, drawn by the skin on top of the host background) lie under our
        transparent plane, where the player would see them; each is covered by a patch one pixel larger, in the flat ground colour the background has at that spot (the places are the layout's, SCREEN coordinates). */
    static void drawMarkerCovers (gpu::SceneBuilder& sb)
    {
        ui::Cell cell (sb, { 0, kTabY, kCanvasW, ui::MacroView::kBarH }, 0);
        drawNativeMarkerCovers (sb, "macros");
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        if (hostBackground())
            drawMarkerCovers (sb);
        else if (pageTop_ == 0)
        {
            drawNativeFallback (sb);
            drawMarkerCovers (sb);
        }
        else
            page::ground (sb);

        if (pageTop_ == kPageKeyboard)
            entry_.draw (sb);
        else if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            targets_.draw (sb);
        else
            view_.draw (sb);

        if (pageTop_ == kPageList)
            list_.draw (sb);
    }

    bool covers (int x, int y) const noexcept
    {
        if (pageTop_ == kPageTargets && targets_.nativeHit (x, y))
            return false;
        if (! ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y))
            return false;
        if (pageTop_ == 0 && nativeControls_)
        {
            if (view_.isNativeControl (x, y))
                return false;
        }
        return true;
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        ui::Component* top = topComponent();
        if (ev.kind == TouchEvent::Down)
        {
            int targetRow = -1;
            if (pageTop_ == kPageTargets && targets_.nativeHit (x, y, &targetRow))
            {
                dropGesture();
                if (targetRow >= 0)
                {
                    targets_.selectRow (targetRow);
                    publishQGroup (inst);
                }
                return false;
            }
            gesture_ = nullptr;
            int macro;
            if (pageTop_ == 0 && nativeControls_ && view_.isNativeDial (x, y, &macro))
            {
                view_.setBank (macro / 4);
                publishQGroup (inst);
                return false;
            }
            if (! covers (x, y) || ! top)
                return false;
            gesture_ = top;
            gestureDown_ = true;
            handle (top->touchDown (x, y, nowMs), stack, inst);
            if (pageTop_ == 0)
                publishQGroup (inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (x, y), stack, inst);
        else
        {
            handle (gesture_->touchUp (x, y), stack, inst);
            gestureDown_ = false;
            gesture_ = nullptr;
        }
        return true;
    }

    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
    }

    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the control of the page under the pointer takes it as steps of its own (a knob: one Q-Link step, a stepper, a list, a scrolled
        list; ui::Component::wheelOver). false: nothing of this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        ui::Component* top = topComponent();
        if (gestureDown_ || ! top || ! covers (x, y))
            return false;
        ui::Change ch;
        if (! top->wheelOver (x, y, notches, ch))
            return false;
        handle (ch, stack, inst);
        (void) nowMs;
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    void leftTab (IInstanceView& inst) noexcept
    {
        inst.publishNativeMacroTargets (false);
        nativeTargets_.hide (inst, targets_);
        resetSpectrum();
        inst.setUiFullOpen (false);
    }
    /** The page comes on screen again (another view was shown): it starts on BANK 1 (macros 1 .. 4) every time, so what the page shows and what the knobs write on arrival never depend on a bank left from an earlier visit; the group is published again. */
    void forgetQGroup() noexcept { view_.setBank (0); qSlots_.forget(); }

    void turnQ (int knobIndex, double delta, UiStack& stack, IInstanceView& inst)
    {
        handle (pageTop_ == kPageTargets ? targets_.turnQ (knobIndex, delta) : view_.turnQ (knobIndex, delta), stack, inst);
    }

    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return pageTop_; }
    /** The macro whose targets page is open (0 .. 7), -1 none. */
    int targetsOf() const noexcept { return pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets) ? targetMacro_ : -1; }

    ui::MacroView& view() noexcept { return view_; }
    const ui::MacroView& view() const noexcept { return view_; }
    ui::TextEntry& keyboard() noexcept { return entry_; }
    /** The macro whose name the keyboard page edits (0 .. 7), -1 when the page is not open. */
    int editedMacro() const noexcept { return pageTop_ == kPageKeyboard ? editMacro_ : -1; }
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
    ui::ModTargetsPage& targetsPage() noexcept { return targets_; }
    ui::ListPopup& list() noexcept { return list_; }
    int bank() const noexcept { return view_.bank(); }
    void setBank (int b) { view_.setBank (b); }
    int macroParam (int m) const noexcept { return m >= 0 && m < 8 ? macroParams_[m] : -1; }
    const SpectrumCurve* spectrum() const noexcept { return curve_.get(); }

private:
    void drawNativeFallback (gpu::SceneBuilder& sb) const
    {
        std::array<ui::Rect, ui::MacroView::kNumMacros> holes;
        for (size_t i = 0; i < holes.size(); ++i)
            holes[i] = view_.cards[i].rect();
        ui::drawNativeCanvas (sb, { 0, kTabY, w_, kTabH }, holes);
    }

    void resetSpectrum() noexcept
    {
        if (curve_)
            curve_->reset();
        lastSpectrumMs_ = -1.0;
        view_.spectrum.levels = nullptr;
    }

    ui::Component* topComponent() noexcept
    {
        if (pageTop_ == kPageList) return &list_;
        if (pageTop_ == kPageKeyboard) return &entry_;
        if (pageTop_ == kPageTargets) return &targets_;
        return &view_;
    }

    void assignIds()
    {
        for (int i = 0; i < 8; ++i)
        {
            view_.cards[i].knob.setId (kMacroKnobBase + i);
            view_.cards[i].setId (kMacroCardBase + i);
            view_.cards[i].nameId = kMacroNameBase + i;
        }
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }


    void updateSpectrum (IInstanceView& inst, double nowMs)
    {
        const SpectrumRing* ring = inst.spectrumRing();
        if (ring == nullptr)
        {
            view_.spectrum.levels = nullptr;
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
        view_.spectrum.levels = curve_->level();
    }

    // ---- the targets of one macro (the same page as the envelope's and the LFO's)
    ModRow targetRows_[ui::ModTargetsPage::kPerPage];
    int targetTotal_ = 0;
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        const int source = macroSource_[std::clamp (targetMacro_, 0, 7)];
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, source, slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + ui::ModTargetsPage::kPerPage - 1) / ui::ModTargetsPage::kPerPage);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * ui::ModTargetsPage::kPerPage, n = std::clamp (total - first, 0, ui::ModTargetsPage::kPerPage);
        ui::ModTargetsPage::Entry e[ui::ModTargetsPage::kPerPage];
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            const int p = synthyboy::modDestinationParamIndex (static_cast<size_t> (std::lround (v.param (sp.destination))));
            std::snprintf (e[i].name, sizeof e[i].name, "%.39s", p >= 0 ? uibind::info (p).name : "-");
            e[i].depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            e[i].bidirectional = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            e[i].bypassed = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            targetRows_[i] = { slots[first + i], source };
        }
        bool same = ! fresh && n == targets_.numRows() && total == targetTotal_ && cardName (std::clamp (targetMacro_, 0, 7)) == targetsTitle_;
        for (int i = 0; same && i < n; ++i)
        {
            const ui::ModTargetsPage::Row& r = targets_.rows[i];
            same = std::strcmp (r.name, e[i].name) == 0 && std::fabs (r.depth.state.value - (e[i].depth + 1.0) * 0.5) < 1.0e-9 && r.polarity.state.on == e[i].bidirectional && r.bypass.state.on == ! e[i].bypassed;
        }
        if (same)
            return;
        targetTotal_ = total;
        char title[32];
        std::snprintf (title, sizeof title, "%s", cardName (std::clamp (targetMacro_, 0, 7)).c_str());
        targets_.load (e, n, title, ui::theme::sourceColour ("MAC"), total, targetsPageIndex_);
        std::snprintf (targetsTitle_, sizeof targetsTitle_, "%s", title);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }

    // ---- the list that picks a new target: the groups of the destinations, then the parameters of a group
    void fillList()
    {
        const uibind::DestIndex& d = uibind::DestIndex::get();
        listNames_.clear();
        if (listGroup_ < 0)
        {
            for (const std::string& g : d.groups)
                listNames_.push_back (g.c_str());
            list_.state.header = nullptr;
            list_.state.headerBack = false;
        }
        else
        {
            for (int c : d.choices[static_cast<size_t> (listGroup_)])
                listNames_.push_back (uibind::info (synthyboy::modDestinationParamIndex (static_cast<size_t> (c))).name);
            list_.state.header = d.groups[static_cast<size_t> (listGroup_)].c_str();
            list_.state.headerBack = true;
        }
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.state.current = -1;
        list_.showCurrent();
    }
    void addTarget (IInstanceView& v, int destChoice)
    {
        const int source = macroSource_[std::clamp (targetMacro_, 0, 7)];
        const int slot = uibind::addTargetSlot (v, source, destChoice);
        if (slot < 0)
            return;
        constexpr int rows = ui::ModTargetsPage::kPerPage;
        const int position = uibind::targetPosition (v, source, slot);
        targetsPageIndex_ = position / rows;
        loadTargets (v, true);
        targets_.selectRow (position % rows);
        modsDirty_ = true;
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        if (! c || v.stateGeneration() != presetGeneration_)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        if (c.kind == K::Request)
        {
            if (id == kTargetsBack || id == kListBackdrop)
            {
                stack.back();
                return;
            }
            if (id == kTargetsAdd)
            {
                underList_ = kPageTargets;
                stack.openDetail (kPageList);
                return;
            }
            if (id == kListHeader)
            {
                listGroup_ = -1;
                fillList();
                return;
            }
            if (id >= kMacroNameBase && id < kMacroNameBase + 8)   // the name of a card: the keyboard page
            {
                editMacro_ = id - kMacroNameBase;
                stack.openDetail (kPageKeyboard);
                return;
            }
            if (id == kKeyboardOk)   // the trimmed name is the macro's from now on ("" is the default name)
            {
                v.setMacroName (editMacro_, synthyboy::sanitizeMacroName (entry_.text(), editMacro_), presetGeneration_);
                stack.back();
                return;
            }
            if (id == kKeyboardCancel)
            {
                stack.back();
                return;
            }
            if (id >= kMacroCardBase && id < kMacroCardBase + 8)   // the chips of a card: the targets of the macro
            {
                targetMacro_ = id - kMacroCardBase;
                stack.openDetail (kPageTargets);
                return;
            }
            if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage && (id - kTargetsRow) % 4 == 3)
            {
                const int slot = targetRows_[(id - kTargetsRow) / 4].slot;
                if (slot >= 0)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    modsDirty_ = true;
                    loadTargets (v, true);
                }
                return;
            }
            return;
        }
        if (id == kList && c.kind == K::Index)
        {
            if (listGroup_ < 0)
            {
                listGroup_ = c.index;
                fillList();
            }
            else
            {
                const std::vector<int>& ch = uibind::DestIndex::get().choices[static_cast<size_t> (listGroup_)];
                if (c.index >= 0 && c.index < static_cast<int> (ch.size()))
                    addTarget (v, ch[static_cast<size_t> (c.index)]);
                stack.back();
            }
            return;
        }
        if (id == kTargetsPage && c.kind == K::Index)
        {
            targetsPageIndex_ = c.index;
            loadTargets (v, true);
            return;
        }
        if (id >= kMacroKnobBase && id < kMacroKnobBase + 8)
        {
            const int m = id - kMacroKnobBase;
            if (c.kind == K::Value && macroParams_[m] >= 0)
                uibind::writeNorm (v, macroParams_[m], c.value);
            return;
        }
        if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage)
        {
            const int slot = targetRows_[(id - kTargetsRow) / 4].slot;
            if (slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
            switch ((id - kTargetsRow) % 4)
            {
                case 0: if (c.kind == K::Value) uibind::writeNorm (v, sp.amount, c.value); break;
                case 1: if (c.kind == K::Index) uibind::writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0); break;
                case 2: if (c.kind == K::Index) uibind::writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0); break;   // the switch says ON (active): bypassed is its opposite
                default: break;
            }
            modsDirty_ = true;
        }
    }

    /** Fixed hardware Q-Link mapping: Bank 1 (Q 1..4) = macro.1..4, Bank 2 (Q 5..8) = macro.5..8 on both 4-knob and 16-knob devices; touching a card or focusing Bank 2 never swaps Bank 1/2 slots. */
    void publishQGroup (IInstanceView& v)
    {
        if (pageTop_ == kPageTargets)
        {
            std::snprintf (qName_, sizeof qName_, "MACRO %d TARGETS", targetMacro_ + 1);
            qSlots_.publish (v, targets_.numRows(), targets_.activeRow(), [this] (int row, int* t) { modRowTargets (row < ui::ModTargetsPage::kPerPage ? targetRows_[row].slot : -1, t); });
            setRowBlocks (targets_, ui::ModTargetsPage::kPerPage, targets_.numRows(), targets_.activeRow());
        }
        else if (pageTop_ == kPageList || pageTop_ == kPageKeyboard)   // no group: the knobs do nothing on these pages
        {
            std::snprintf (qName_, sizeof qName_, pageTop_ == kPageList ? "LIST" : "KEYBOARD");
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else
        {
            std::snprintf (qName_, sizeof qName_, view_.bank() == 0 ? "MACROS 1-4" : "MACROS 5-8");
            qSlots_.publish (v, 2, 0, [this] (int bank, int* t) { for (int k = 0; k < 4; ++k) t[k] = macroParams_[bank * 4 + k]; });
        }
    }

    static constexpr double kSpectrumMinStepMs = 16.0;
    int w_ = kCanvasW, h_ = kCanvasH;
    int pageTop_ = 0, underList_ = 0, listGroup_ = -1, targetMacro_ = 0, targetsPageIndex_ = 0;
    int macroParams_[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int macroSource_[8] = {};
    std::vector<const char*> listNames_;

    /** The keyboard page opens with the macro's own name (empty for the default: a new name starts with a capital). */
    void beginKeyboard (IInstanceView& inst)
    {
        const auto content = inst.content();
        const std::string current = content != nullptr && editMacro_ >= 0 && editMacro_ < synthyboy::kNumMacros ? content->macroNames[editMacro_] : std::string();
        char title[40];
        std::snprintf (title, sizeof title, "NAME OF MACRO %d", editMacro_ + 1);
        entry_.begin (title, current.c_str(), synthyboy::kMaxMacroNameChars, kKeyboardOk, kKeyboardCancel);
    }

    MacroNames names_;
    int editMacro_ = 0;
    char targetsTitle_[32] = {};
    ui::TextEntry entry_;
    ui::MacroView view_;
    ui::ModTargetsPage targets_;
    NativeModTargets nativeTargets_;
    ui::ListPopup list_;
    std::unique_ptr<SpectrumCurve> curve_;
    double lastSpectrumMs_ = -1.0, modsAtMs_ = -1000.0;
    bool modsDirty_ = true;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;

    QSlots qSlots_;
    char qName_[32] = "MACROS 1-4";
    bool nativeGround_ = false;
    bool nativeControls_ = true;
    bool presetSeen_ = false;
    uint64_t presetGeneration_ = 0;
    bool macroInit_ = false;
    unsigned long lastQWriteCount_ = 0;
    float prevMacroVal_[8] = {};
};

}} // namespace synthyboy_mpc::overlay
