// SynthyBoy - MPC overlay: the oscillator UI of the Play page and of the oscillator tabs, wired to the real engine parameters. Owner decision 2026-11-27: the C++ layer has no UI elements; every control of
// the oscillator is a native cell of the host skin. This object holds what the plugin knows about them: the rows (`Item`: id, engine key, kind, the state read from the parameter and the rectangle of the cell, osc_model.h), the Q-Link groups
// (tables of item ids), the native bridge (compact, settings, detail), the displays and the picture rectangles, the pages above the full screen. One object per (Play view, plugin instance). Pure: no OS headers, no threads of its own; the overlay
// thread calls update () / touch () / tick () / describe (), with the instance as IInstanceView.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h): main = the compact view in the oscillator third of the Play page; full = the full screen of the instance over the whole plugin area of the Play page; detail pages above it (unison settings, grain
// window, multisample envelope, the modulations of a control) and the route popup. The picture of the oscillator cycle goes into the display rectangle (displayRect (); the Play view draws it).
// What it does with a touch: the host's native cell writes the value (native bridge: applyNativeSetting / applyNativeDetail, the id of the cell says which parameter and how the value converts); the write goes through
// IInstanceView::writeParam (normalised; the plugin tells the host). Every update () reads every parameter back into the cells, so host automation, presets and the host's own widgets show on the next frame.
// Controls whose engine parameter does not exist or is not read by the engine are DISABLED (WARP Var, phase memory, the source list of the sample types, the source dropdown).
// The lists (the content browser, the wavetable browser, the source of a new modulation) are the native cells of the modulation picker (nativeui::modboard, names through IInstanceView::setNativeListText) and the mask of the transpose snap is
// twelve native toggle cells (nativeui::kNoteMask): the plugin holds the data of both (listData_, maskItem_) and takes their gestures (publishNativeLists, publishNativeNoteMask).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../native_osc_controls.h"
#include "../play_osc_native_contract.h"
#include "../ui/choice_names.h"
#include "../ui/content_name.h"
#include "../ui/core.h"
#include "../ui/display.h"
#include "../ui/native_apertures.h"
#include "../ui/play_rows.h"
#include "../ui/qgroup.h"
#include "content_browser.h"
#include "wavetable_browser.h"
#include "osc_spec_page.h"
#include "modulation_window.h"
#include "mod_model.h"
#include "mod_rows_page.h"
#include "native_mod_targets_ui.h"
#include "osc_model.h"
#include "instance_view.h"
#include "../native_osc_detail.h"
#include "../q_router.h"
#include "play_osc.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "ui_bind.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/wavetable_catalogue.h"

namespace synthyboy_mpc { namespace overlay {

/** The state and the geometry of the full screen of an oscillator instance (plain data, osc_model.h): which instance and type it is for, which parts the host's native controls take (the instance selector, the routing, the power, the tune
    card), whether the oscillator is switched off, and the rectangles that follow from it. Nothing is drawn here and nothing takes a touch: the controls are the host's. */
class OscFullView
{
public:
    static constexpr int kMaxGroups = oscmodel::kMaxGroups;
    static constexpr size_t kNativePanelCells = oscmodel::kNativePanelCells;
    using NativeCells = oscmodel::NativeCells;
    static constexpr ui::Rect tuneAreaAt (int x, int y) noexcept { return oscmodel::tuneAreaAt (x, y); }

    void place (const ui::Rect& r) { rect_ = r; relayout(); }
    ui::Rect rect() const noexcept { return rect_; }
    /** The instance and the type of A, B, C, the noise mode: the top row, the bottom band and the warps change. */
    void setInstance (oscmodel::Instance inst, oscmodel::Kind kind, bool noiseSample = false)
    {
        s_.inst = inst;
        s_.kind = kind;
        s_.noiseSample = inst == oscmodel::Instance::Noise && noiseSample;
        warpApplies_[0] = warpApplies_[1] = true;
        relayout();
    }
    oscmodel::Instance instance() const noexcept { return s_.inst; }
    oscmodel::Kind kind() const noexcept { return s_.kind; }
    bool noiseSample() const noexcept { return s_.noiseSample; }
    /** The screen is a PAGE of a tab (the tab names the oscillator and nothing closes it). */
    void setPageTab (bool page) { s_.pageTab = page; relayout(); }
    bool isPage() const noexcept { return s_.pageTab; }
    void setNativeInstanceSelector (bool shown) { if (s_.nativeSelector != shown) { s_.nativeSelector = shown; relayout(); } }
    bool hasNativeInstanceSelector() const noexcept { return s_.nativeSelector; }
    ui::Rect selectorArea() const noexcept { return L_.selectorArea; }
    void setNativeRouting (bool shown) { if (s_.nativeRouting != shown) { s_.nativeRouting = shown; relayout(); } }
    bool hasNativeRouting() const noexcept { return s_.nativeRouting; }
    bool isNativeRoutingActive() const noexcept { return s_.nativeRouting && ! inactive_; }
    ui::Rect routingArea() const noexcept { return L_.routingArea; }
    void setNativePower (bool on) noexcept { s_.nativePower = on; }
    bool hasNativePower() const noexcept { return s_.nativePower; }
    bool hasNativeTune() const noexcept { return oscmodel::hasNativeTune (s_); }
    ui::Rect tuneArea() const noexcept { return tuneAreaAt (rect_.x, rect_.y); }
    ui::Rect panArea() const noexcept { return { rect_.right() - 416, rect_.bottom() - 48 - (oscmodel::nativeStrip (s_) ? 48 : 0), 202, 48 }; }
    ui::Rect levelArea() const noexcept { return { rect_.right() - 210, rect_.bottom() - 48 - (oscmodel::nativeStrip (s_) ? 48 : 0), 202, 48 }; }
    bool isNativeRouting (int x, int y) const noexcept
    {
        return isNativeRoutingActive() && (panArea().contains (x, y) || levelArea().contains (x, y) || L_.routingArea.contains (x, y));
    }
    /** The oscillator is switched off: dimmed and read-only except POWER, navigation and viewing. */
    void setInactive (bool off) noexcept { inactive_ = off; }
    bool inactive() const noexcept { return inactive_; }
    /** Hide what does not apply: the amount of warp `slot` (0, 1) while its type is Off. */
    void setWarpApplies (int slot, bool applies)
    {
        if (slot < 0 || slot > 1 || warpApplies_[slot] == applies)
            return;
        warpApplies_[slot] = applies;
        relayout();
    }
    bool warpApplies (int slot) const noexcept { return slot >= 0 && slot <= 1 && warpApplies_[slot]; }
    /** Is the part of this slot on the screen (FIN is the tune card's while the host shows it). */
    bool partShown (oscmodel::PanelSlot slot) const noexcept { return slot != oscmodel::PanelSlot::Fin || ! hasNativeTune(); }

    const oscmodel::FullLayout& layout() const noexcept { return L_; }
    ui::Rect displayRect() const noexcept { return L_.display; }
    ui::Rect powerRect() const noexcept { return L_.power; }
    ui::Rect moreRect() const noexcept { return L_.more; }
    ui::Rect subBlendRect() const noexcept { return L_.subBlend; }
    /** The rectangle of the item of `slot` in the bottom band (empty: the preset has none). */
    ui::Rect slotRect (oscmodel::PanelSlot slot) const noexcept
    {
        const int i = indexOf (slot);
        return i >= 0 ? L_.cells.item[i] : ui::Rect {};
    }
    int indexOf (oscmodel::PanelSlot slot) const noexcept
    {
        for (int i = 0; L_.preset != nullptr && i < L_.preset->numItems && i < oscmodel::kMaxItems; ++i)
            if (L_.preset->items[i].slot == slot)
                return i;
        return -1;
    }

private:
    void relayout() { if (rect_.w > 0) L_ = oscmodel::layoutFull (rect_, s_, warpApplies_); }
    ui::Rect rect_ {};
    oscmodel::FullState s_ {};
    oscmodel::FullLayout L_ {};
    bool warpApplies_[2] = { true, true };
    bool inactive_ = false;
};

class OscUi
{
public:
    // ---- the pages (UiStack ids)
    static constexpr int kElementOsc = 1;                                     // the full screen of the oscillator
    enum Page { kPageUnison = 10, kPageGrain = 11, kPageEnvelope = 12, kPageRoute = 13, kPageMods = 14, kPageList = 15 };   // 16 .. 22: the detail pages of osc_spec_page.h (oscspec::PageId)

    // ---- the id spaces of the controls (a Change comes out with the id the owner gave)
    enum Id : int
    {
        kClose = 1, kPower, kRoute, kFullButton, kInstances,
        kBasic = 100,                       // the first Q group's parts: selector, source stepper, dropdown, OCT (Sub), FILTER, SOUND of the noise, PAN, LEVEL (compact view) = 100 .. 107
        kTuneOct = 110, kTuneFin = 112, kTuneCrs = 113, kTuneMore = 115, kWavePhase, kWaveRandom, kWaveMap,   // OCT (A, B, C), FIN, the PITCH of the noise, the MORE button of the full screen
        kSpecLoad = 790, kSpec = 800,       // the LOAD button of an empty sample type; 800 + i: the controls of a detail page of osc_spec_page.h
        kPanel = 200,                       // 200 + PanelSlot: the parts of the full screen's bottom band (and the position knob, the Sub's CRS and PHASE, the noise's KIND and STEREO of its top row)
        kWarp1Type = 300, kWarp1Amount, kWarp1Var, kWarp2Type, kWarp2Amount, kWarp2Var, kUnisonGear = 310, kUnisonCount,   // (the Var ids are free: the warp PHASE knobs are on MORE now)
        kDetailBack = 399, kDetail = 400,   // 400 + PanelSlot: the parts of a detail page
        kRouteTarget = 500, kRouteBalance, kRouteBus1, kRouteBus2,
        kList = 700, kListBackdrop, kListHeader,   // the list popup (source, warp types, the content browser: its header is the way up)
        kModsBack = 600, kModsRow = 610,    // 610 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kModsAddStep = 660, kModsAdd, kModsPage
    };

    /** What a value is and how it converts to the engine's plain value. */
    enum class T : uint8_t
    {
        Dial,        // normalised 0 .. 1 of the parameter
        Number,      // a whole-number field in the units of the parameter
        NumberOct,   // transpose: OCT = steps of 12 semitones
        NumberSem,   // transpose: SEM = the remainder
        NumberDeg,   // a percent parameter shown as degrees (0 .. 100 % = 0 .. 360)
        Choice,      // a choice parameter
        Toggle,
        UpDown,
        Picto,       // the type pictograms of A, B, C
        PictoPlain,  // the waveform pictograms of the sub oscillator
        DetailNorm,  // a detail button with a 0 .. 1 value
        DetailInt,   // a detail button with an integer value
        Route,       // the route button: the destination choice
        List,        // a short choice parameter (all options visible)
        Mask,        // the notes of the transpose snap (NoteMask): bits 0 .. 11 of the parameter
        SnapBit,     // the toggle for bit 12 of the same parameter (snap the played note, not the transpose)
        Source,      // the source of a granular / multisample oscillator: the name of the file that is loaded; a tap opens the content browser
        Table,       // the table of a wavetable oscillator: a built-in table (the choice parameter) or a table FILE (content); the name opens the wavetable browser, the arrows step the 15 choices or the files of the category
        Inert        // no value: a button
    };

    struct Item
    {
        int id = 0;
        oscmodel::Cell cell;       // the native cell of the row: its rectangle and what the parameter says now
        bool hasCell = true;       // false: a row with no cell of its own (the rows of the route popup)
        T type = T::Inert;
        std::string key;           // the key of the engine parameter ("" = none: the control is disabled)
        int param = -1;            // its table index (-1: none)
        int param2 = -1;           // the second parameter (none of the current types has one)
        bool alwaysOff = false;    // drawn disabled whatever the engine says (nothing behind it)
        int row = -1;              // a control of the modulations page: the row
        bool full = false;         // a part of the full screen (rebuilt when the noise changes its mode)
    };

    using NativeCellsArray = oscmodel::NativeCells;
    OscUi() { setCanvas (kCanvasW, kCanvasH); configure (nullptr, 2, true); }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** The canvas is the whole screen (1280 x 800: the Play view's region; every coordinate is a screen pixel); the pages live in the tab (y kTabY .. kTabY + kTabH). */
    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kCompactX = 8, kCompactW = 420;                // the left third of the Play page
    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        compactRect_ = { kCompactX, kTabY + 4, kCompactW, kTabH - 8 };
        tabRect_ = tab;
        full_.place (tab);
        spec_.place (tab);
        mods_.place (tab);
        mods_.setIds (kModsBack, kModsAdd, kModsPage, kModsRow);
        relayout();
    }

    /** Reads the instance: the selected oscillator, the type of A, B, C, every value of the visible controls, the modulations of the knobs, the pages of the stack, the Q group. */
    void update (IInstanceView& inst, int highlight, UiStack& stack, double nowMs, int selectedEnv, int selectedLfo, bool expandedPlay = false)
    {
        highlight_ = highlight;
        const int sel = pageSel_ > 0 ? pageSel_ : std::clamp (inst.playSelection (0), 1, playosc::kInstances);
        const int typeIdx = typeParam (sel);
        const int raw = typeIdx >= 0 ? static_cast<int> (std::lround (inst.param (typeIdx))) : 0;
        if (sel != sel_ || raw != typeRaw_ || ! configured_)
            configure (&inst, sel, false, raw);
        if (stack.top() == kPageMods && modsTargetParam_ >= 0)
        {
            if (sel_ != modsSource_ || raw != modsType_ || ! modsModulePowered (inst))
            {
                dropGesture();
                stack.openFull (kElementOsc);
            }
            else if (modsPreset_ != inst.stateGeneration())
            {
                dropGesture();
                modsPreset_ = inst.stateGeneration();
                modsDirty_ = true;
                modsPicking_ = false;
            }
        }
        // Older sessions may still carry the removed Sub/Noise MORE page.
        if ((sel_ == playosc::kSub || sel_ == playosc::kNoise) && stack.depth() > 1 && stack.top() == oscspec::kMore)
            stack.openFull (kElementOsc);
        stackDepth_ = stack.depth();
        syncPages (stack, inst);
        envSel_ = selectedEnv; lfoSel_ = selectedLfo;
        const int lastSel = inst.playLastSelector();
        selectedSource_ = lastSel == 1 ? envSource (selectedEnv) : lfoSource (selectedLfo);
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        if (sel_ == playosc::kNoise)
        {
            const int ti = indexOfKey ("noise.type");
            const bool sample = ti >= 0 && inst.param (ti) >= 0.5f;
            updateNoiseType (sample, inst);
        }
        updateMissing (inst);
        updateWavetable (inst);
        pull (inst, main_);
        pull (inst, page_);
        updateMix (inst);

        applyInactive();
        updateSpecVisibility (inst);
        updateLoadButton (inst);
        if (pageTop_ == kPageEnvelope)
            updateEnvelope();
        const bool standaloneNative = isPage() && ! expandedPlay && (nativeSelector_ || nativeRouting_);
        inst.setUiFullOpen (standaloneNative ? (pageTop_ != 0) : ! stack.isMain());
        publishNativeLists (inst, stack);
        publishQGroup (inst);
    }

    /** On standalone tabs OscPageUi supplies the active source independently of Play and uses the close position for its name.
        `sel`: 1 Sub, 2 A, 3 B, 4 C, 5 Noise. 0 selects the Play layout. */
    void setPage (int sel) noexcept { pageSel_ = sel >= 1 && sel <= playosc::kInstances ? sel : 0; configured_ = false; }
    bool isPage() const noexcept { return pageSel_ > 0; }

    /** The rectangle (canvas coordinates) the picture of the oscillator goes into now; empty when the current page shows none. */
    ui::Rect displayRect() const noexcept
    {
        if (listOpen())
            return {};   // the native list cells fill the tab: no picture under them
        if (nativeDetail_ >= nativeui::oscdetail::Graph && nativeDetailState_.page == pageTop_ && pageTop_ != kPageEnvelope)
            return nativeDetailGraphRect();
        if (stackDepth_ == 0)
            return cl_.display;
        if (stackDepth_ == 1)
            return full_.displayRect();
        if (oscspec::isSpecPage (pageTop_))
            return spec_.displayRect();
        if (pageTop_ == kPageUnison || pageTop_ == kPageGrain)
            return detailLayout_.display;   // the same picture of the oscillator over the page (the fan of the unison voices, the grain window on the sample)
        return {};
    }
    bool canKeepDetailOnRecall (const IInstanceView& inst, int source) const
    {
        if (source != sel_)
            return false;
        const int type = typeParam (source);
        if (type < 0 || static_cast<int> (std::lround (inst.param (type))) != typeRaw_)
            return false;
        return pageTop_ == kPageEnvelope || pageTop_ == kPageMods
            || ((pageTop_ == kPageUnison || pageTop_ == kPageGrain || oscspec::isSpecPage (pageTop_)) && ! displayRect().empty());
    }

    /** The rectangle the picture is drawn in: the plot area of the compact view (below the header band with the buttons, ui::playrows::plotRect); empty in every other state (the picture has its display rectangle). */
    ui::Rect plotArea() const noexcept
    {
        if (stackDepth_ == 0)
            return cl_.plot();
        if (pageTop_ == 0 && full_.hasNativeTune() && full_.hasNativePower())
        {
            const auto r = full_.displayRect();
            return { r.x + ui::playrows::kPlotPadX, r.y + ui::playrows::kPlotPadTop,
                     r.w - 2 * ui::playrows::kPlotPadX, r.h - ui::playrows::kPlotPadTop - ui::playrows::kPlotPadBottom };
        }
        return pageTop_ == 0 && full_.hasNativePower() ? ui::playrows::plotRect (full_.displayRect()) : ui::Rect {};
    }
    bool isMain() const noexcept { return stackDepth_ == 0; }
    /** The instance (1 Sub .. 5 Noise) and the type of the oscillator it shows (0 wavetable, 1 sample, 2 granular, 3 spectral, 4 multisample; -1 for Sub and Noise). */
    int selection() const noexcept { return sel_; }
    int typeRaw() const noexcept { return sel_ == playosc::kSub || sel_ == playosc::kNoise ? -1 : typeRaw_; }
    const char* typeName() const noexcept { return typeName_; }
    /** The list of the targets of the active Q group (table indices, -1 none) and the name of the group. */
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }

    void draw (gpu::SceneBuilder& sb) const { drawPage (sb); }
    /** The page without its popups (the picture of the oscillator goes on it, then the popups): the native detail pages' title, block names and note mask; the controls are the host's. */
    void drawPage (gpu::SceneBuilder& sb) const
    {
        if (nativeDetail_ >= nativeui::oscdetail::Plain)
        {
            drawNativeDetail (sb);
            if (pageTop_ == kPageEnvelope)
                drawEnvelope (sb);
        }
        if (pageTop_ == kPageMods)
            mods_.draw (sb);   // the cut-outs of the native cells and the texts of the modulations page
    }
    /** The envelope of the multisample oscillator as a curve in the display area of its page (the schematic of the six values: delay, attack, hold, decay, sustain level, release). */
    void drawEnvelope (gpu::SceneBuilder& sb) const
    {
        const ui::Rect r = nativeDetail_ >= nativeui::oscdetail::Graph ? nativeDetailGraphRect() : detailLayout_.display;
        ui::display::Module m;
        m.rect = r;
        m.plot = { r.x + 16, r.y + 38, r.w - 32, r.h - 52 };
        m.env = true;
        m.title = "ENVELOPE";
        m.v = envCurve_;
        m.n = kEnvPoints;
        ui::display::drawModule (sb, m);
    }
    /** What goes on top of the picture in the display rectangle: the line of the content that is missing. */
    void drawOnDisplay (gpu::SceneBuilder& sb) const { drawMissing (sb); }
    const oscspec::OscSpecPage& specPage() const noexcept { return spec_; }
    oscspec::OscSpecPage& specPage() noexcept { return spec_; }
    /** One of the lists (the content browser, the wavetable browser, the source of a new modulation) is on the screen: the modulations page of a control that has no modulation yet is the list of the sources. */
    bool listOpen() const noexcept { return pageTop_ == kPageList || (pageTop_ == kPageMods && modsBoard()); }
    /** The touch hit this view's drawing (the main state: the oscillator third; any other state: everything). */
    bool covers (int x, int y) const noexcept
    {
        if (stackDepth_ > 0)
        {
            if (pageTop_ == kPageMods && ! modsPicking_ && modsNativeHit (x, y))
                return false;
            if (pageTop_ == 0)
            {
                if (nativeRootLoad() && loadRect_.contains (x, y))
                    return false;
                if (nativeSelector_ && selectorArea().contains (x, y))
                    return false;
                if (full_.isNativeRouting (x, y))
                    return false;
            }
            return tabRect_.contains (x, y);
        }
        for (const auto& rect : compactNativeCells_)
            if (rect.contains (x, y))
                return false;
        if (cl_.pan.contains (x, y) || cl_.level.contains (x, y))
            return false;
        return compactRect_.contains (x, y);
    }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** One touch event in UI pixels (the whole screen), `stack` is the instance's stack (the caller holds its lock). A touch on a row of the modulations page makes it the active one; every control of the pages is a native cell of
        the host (the gestures arrive through the native bridge), so nothing here takes a touch: false. */
    bool touch (const TouchEvent& ev, UiStack&, IInstanceView& inst, double)
    {
        if (ev.kind != TouchEvent::Down || pageTop_ != kPageMods)
            return false;
        int nativeRow = -1;
        if (modsNativeHit (ev.x, ev.y, &nativeRow))
        {
            if (nativeRow >= 0)
            {
                mods_.selectRow (nativeRow);
                publishQGroup (inst);
            }
            return false;
        }
        if (! listOpen())
            for (int i = 0; i < mods_.numRows(); ++i)   // a touch on a row makes it the active one
                if (ev.y >= mods_.rowY (i) && ev.y < mods_.rowY (i) + ModRowsPage::kRowH)
                {
                    mods_.selectRow (i);
                    publishQGroup (inst);
                    break;
                }
        return false;
    }
    /** The mouse wheel: nothing of this page takes it (the cells are the host's). */
    bool wheel (int, int, int, UiStack&, IInstanceView&, double) { return false; }
    /** The clock: nothing of this page runs a gesture of its own. */
    void tick (double, UiStack&, IInstanceView&) {}

    bool fingerDown() const noexcept { return false; }
    /** The stack changed behind the UI's back (the page left, another instance): nothing to drop (the gestures are the host's). */
    void dropGesture() noexcept {}
    /** The page went off the screen (a host dialog over it): nothing of ours stays open on the third. */
    void closePopups() {}
    /** Another element's page opened or closed: the Q group is published again at the next update (the router keeps what the last page set). */
    void forgetQGroup() noexcept { qSlots_.forget(); }

    // ---- access for the tests
    /** The oscillator is ON (the power parameter of the instance on screen). */
    bool powerOn() const noexcept { const Item* it = item (kPower); return it != nullptr && it->cell.on; }
    /** The cells of the power, the full screen and the route button of the display band of the compact view (empty while the host does not show them). */
    std::array<ui::Rect, 3> compactNativeHeader() const noexcept
    {
        namespace n = nativeui::playoscui;
        return { compactNativeCells_[n::Power], compactNativeCells_[n::Expand], compactNativeCells_[n::Route] };
    }
    /** The label rectangle of the display band of the compact view. */
    ui::Rect compactLabelRect() const noexcept { return oscmodel::compactLabelRect (cl_, instance()); }
    /** Standalone oscillator tabs expose the instance choice to the host; the
        page wrapper owns the aperture and touch pass-through. */
    void setNativeInstanceSelector (bool shown) noexcept
    {
        nativeSelector_ = shown;
        full_.setNativeInstanceSelector (shown);
        relayout();
    }
    bool hasNativeInstanceSelector() const noexcept { return nativeSelector_; }
    ui::Rect selectorArea() const noexcept { return nativeSelector_ ? full_.selectorArea() : ui::Rect {}; }
    /** Standalone oscillator tabs expose the destination routing choice to the host while ON; modals and OFF state close this aperture. */
    void setNativeRouting (bool shown) noexcept
    {
        nativeRouting_ = shown;
        full_.setNativeRouting (shown);
        relayout();
    }
    bool hasNativeRouting() const noexcept { return nativeRouting_; }
    bool isNativeRoutingActive() const noexcept { return nativeRouting_ && full_.isNativeRoutingActive(); }
    ui::Rect routingArea() const noexcept { return nativeRouting_ ? full_.routingArea() : ui::Rect {}; }
    void focusNativeTune (bool track, IInstanceView& inst)
    {
        tuneFocus_ = track ? 1 : 0;
        fullActive_ = -1;
        publishQGroup (inst);
    }
    void focusNativeMix (IInstanceView& inst)
    {
        tuneFocus_ = -1;
        fullActive_ = -1;
        publishQGroup (inst);
    }
    OscFullView& full() noexcept { return full_; }
    const OscFullView& full() const noexcept { return full_; }
    ModRowsPage& modsPage() noexcept { return mods_; }
    const ModRowsPage& modsPage() const noexcept { return mods_; }
    void syncNativeModulations (IInstanceView& inst, UiStack& stack, nativeui::targets::Owner owner)
    {
        const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        const bool shown = stack.element() == kElementOsc && stack.top() == kPageMods && ! modsBoard()
            && modsContextValid (inst);
        nativeModulations_.update (inst, mods_, shown, owner, static_cast<int> (destination), modRows_,
            [&] (ui::Change c)
            {
                handle (c, stack, inst);
                stackDepth_ = stack.depth();
                syncPages (stack, inst);
                publishQGroup (inst);
            }, nativeui::targets::Modulations);
    }
    void hideNativeModulations (IInstanceView& inst)
    {
        nativeModulations_.hide (inst, mods_);
        hideNativeLists (inst);
    }

    // ---- the lists and the note mask as native cells
    /** The list on the screen as the cells of the modulation picker (nativeui::modboard, the skin of 72_osc_lists.py): kListRows rows a page, BACK, the way UP (the header of a browser) or the groups of the sources, PREVIOUS and NEXT.
        The names are the plugin's (IInstanceView::setNativeListText), the taps come back through takeNativeModBoard and make the same changes the overlay list made (kList, kListBackdrop, kListHeader). */
    void publishNativeLists (IInstanceView& inst, UiStack& stack)
    {
        namespace nb = nativeui::modboard;
        if (! listOpen())
        {
            hideNativeLists (inst);
            return;
        }
        const int count = listData_.count;
        const int lastFirst = std::max (0, ((count - 1) / nb::kListRows) * nb::kListRows);
        nativeListFirst_ = std::clamp (nativeListFirst_ - nativeListFirst_ % nb::kListRows, 0, lastFirst);
        const bool up = pageTop_ == kPageList && (wtMode_ || browseMode_);   // a browser: its header is the way UP
        nativeui::nativetext::ListPage page;
        std::snprintf (page.title, sizeof page.title, "%s", listData_.header != nullptr ? listData_.header : up ? "" : "SELECT MODULATION SOURCE");
        page.up = up;
        page.count = std::clamp (count - nativeListFirst_, 0, nb::kListRows);
        uint32_t signature = 2166136261u;
        auto mix = [&] (const char* text) { for (const char* c = text; *c != '\0'; ++c) signature = (signature ^ static_cast<unsigned char> (*c)) * 16777619u; signature = (signature ^ 0xffu) * 16777619u; };
        mix (page.title);
        for (int row = 0; row < page.count; ++row)
        {
            std::snprintf (page.rows[row], sizeof page.rows[row], "%s", listData_.items != nullptr && listData_.items[nativeListFirst_ + row] != nullptr ? listData_.items[nativeListFirst_ + row] : "");
            mix (page.rows[row]);
        }
        signature = (signature ^ static_cast<uint32_t> (nativeListFirst_ * 4 + count * 64 + (up ? 1 : 0) + (listData_.headerBack ? 2 : 0))) * 16777619u;
        if (! listPublished_ || signature != nativeListSignature_)
        {
            nativeListSignature_ = signature;
            nativeListSerial_ = (nativeListSerial_ + 1) % 8000;
            inst.setNativeListText (&page);
            listPublished_ = true;
        }
        const int identity = nativeListSerial_ + 1;   // a new page: new cell codes, so the host reads the names again
        std::array<nativeui::FxCell, nb::kHandles> fields {};
        for (int row = 0; row < page.count; ++row)
        {
            fields[nb::ListRow + row] = { nb::kChoiceBias + nativeListFirst_ + row, identity, nativeui::kFxDial };
            fields[nb::ListRowGate + row] = { 1, 1, nativeui::kFxToggle };
        }
        for (int field : { nb::ListBack, nb::ListPrev, nb::ListNext, nb::ListGroups, nb::ListGate, nb::ListTitle })
            fields[field] = { 1, identity, nativeui::kFxToggle };
        inst.setNativeModBoard (2, fields.data());
        for (int field = nb::ListRow; field < nb::ListGate; ++field)
        {
            float value = 0.0f;
            if (! inst.takeNativeModBoard (field, fields[field], value) || value < 0.5f)
                continue;
            if (field < nb::ListBack)
                handle (ui::Change::indexed (kList, nativeListFirst_ + field - nb::ListRow, value), stack, inst);
            else if (field == nb::ListBack)
                handle (ui::Change::request (kListBackdrop), stack, inst);
            else if (field == nb::ListGroups)
                handle (ui::Change::request (kListHeader), stack, inst);
            else
                nativeListFirst_ = std::clamp (nativeListFirst_ + (field == nb::ListPrev ? -nb::kListRows : nb::kListRows), 0, lastFirst);
            inst.setNativeModBoard (0, nullptr);   // the gesture is used: new cells at the next update (an unused gesture of the old page cannot act on the new one)
            return;
        }
    }
    void hideNativeLists (IInstanceView& inst)
    {
        if (! listPublished_)
            return;
        inst.setNativeModBoard (0, nullptr);
        inst.setNativeListText (nullptr);
        listPublished_ = false;
    }
    /** The twelve notes of the transpose snap as native toggle cells over the mask area of the OPTIONS page (nativeui::kNoteMask, the skin of 72_osc_lists.py): a tap sets or clears one bit of the parameter (bit 12, SNAP NOTE, is kept). */
    void publishNativeNoteMask (IInstanceView& inst, bool consume)
    {
        if (maskItem_ == nullptr || ! maskItem_->hasCell || maskItem_->param < 0)
        {
            if (maskPublished_)
            {
                inst.setNativeNoteMask (false, nullptr);
                maskPublished_ = false;
            }
            return;
        }
        std::array<nativeui::FxCell, nativeui::kNoteMaskHandles> fields {};
        fields.fill ({ maskItem_->param, maskItem_->id, nativeui::kFxToggle });
        inst.setNativeNoteMask (true, fields.data());
        maskPublished_ = true;
        if (! consume)
            return;
        for (int note = 0; note < 12; ++note)
        {
            float value = 0.0f;
            if (! inst.takeNativeNoteMask (note, fields[note], value))
                continue;
            focusNativeDetail (*maskItem_, inst);
            const int current = static_cast<int> (std::lround (inst.param (maskItem_->param)));
            const int bit = 1 << note, mask = value >= 0.5f ? (current | bit) : (current & ~bit);
            writePlain (inst, maskItem_->param, (current & 4096) | (mask & 4095));
            return;
        }
    }
    const std::vector<Item>& items() const noexcept { return main_; }
    using CompactNativeCells = std::array<ui::Rect, nativeui::playoscui::kFields>;
    const CompactNativeCells& compactNativeCells() const noexcept { return compactNativeCells_; }

    std::array<const Item*, nativeui::playoscui::kFields> compactNativeItems() const noexcept
    {
        const bool sub = sel_ == playosc::kSub, noise = sel_ == playosc::kNoise;
        return { item (kPower), item (kFullButton), sub ? nullptr : item (kRoute),
                 item (kBasic + (noise ? 5 : 0)), item (kBasic + (sub ? 3 : noise ? 4 : 1)),
                 item (kBasic + 6), item (kBasic + 7) };
    }

    nativeui::playoscui::State compactNativeState (const IInstanceView& inst) const noexcept
    {
        namespace n = nativeui::playoscui;
        n::State state;
        state.source = sel_;
        state.layout = sel_ == playosc::kSub ? n::Sub : sel_ == playosc::kNoise ? n::Noise
                     : typeRaw_ == 0 ? n::AbcWave : n::AbcContent;
        state.variant = sel_ == playosc::kNoise ? noiseSample_ : typeRaw_;
        state.powered = powerOn();
        state.preset = inst.stateGeneration();
        state.selection = inst.playOscSelectionRevision();
        const auto items = compactNativeItems();
        for (int field = 0; field < n::kFields; ++field)
            if (items[field] != nullptr)
                state.fields[field] = nativeSettingsBinding (*items[field]);
        if (state.fields[n::Route].target >= 0)
            state.fields[n::Route].kind = nativeui::osc::SettingsMenu;
        return state;
    }

    // Prepare only the compact bindings and display values. Running update() here would also
    // publish Q bindings and consume pending wavetable gestures before this page owns input.
    nativeui::playoscui::State prepareCompactPresentation (IInstanceView& inst, std::string& sourceName)
    {
        const int source = std::clamp (inst.playSelection (0), 1, playosc::kInstances);
        const int type = typeParam (source);
        configure (&inst, source, false, type >= 0 ? static_cast<int> (std::lround (inst.param (type))) : 0);
        pull (inst, main_);
        const auto state = compactNativeState (inst);
        if ((state.layout == nativeui::playoscui::AbcWave || state.layout == nativeui::playoscui::AbcContent) && state.powered)
            sourceName = compactSourceText();
        return state;
    }

    void publishCompactNative (IInstanceView& inst, bool shown, bool leavingTab = false)
    {
        namespace n = nativeui::playoscui;
        const auto state = shown ? compactNativeState (inst) : n::State {};
        const auto items = compactNativeItems();
        compactNativeCells_.fill ({});
        for (int field = 0; field < n::kFields; ++field)
            if (shown && items[field] != nullptr)
            {
                const auto cr = n::contractRect (state.layout, field);
                compactNativeCells_[field] = cr.w > 0 && cr.h > 0
                    ? ui::Rect { cr.x, kTabY + cr.y, cr.w, cr.h }
                    : items[field]->cell.rect;
            }
        const bool abc = state.layout == n::AbcWave || state.layout == n::AbcContent;
        if (leavingTab)
            inst.suspendNativePlayOsc();
        else
            inst.setNativePlayOsc (state, abc && state.powered ? compactSourceText() : "");
    }

    bool syncCompactNative (IInstanceView& inst, UiStack& stack)
    {
        namespace n = nativeui::playoscui;
        const auto state = compactNativeState (inst);
        const auto items = compactNativeItems();
        bool changed = false;
        for (int field = 0; field < n::kFields; ++field)
        {
            const auto* target = items[field];
            float value;
            if (target == nullptr || ! inst.takeNativePlayOsc (state, field, value)
                || state.preset != inst.stateGeneration() || state.selection != inst.playOscSelectionRevision())
                continue;
            if (field == n::Route)
            {
                const int route = static_cast<int> (std::lround (value * n::kRoutingDetails));
                if (route == n::kRoutingDetails)
                    handle (ui::Change::request (target->id), stack, inst);
                else if (inst.param (target->param) != static_cast<float> (route))
                    writePlain (inst, target->param, static_cast<float> (route));
            }
            else
                applyNativeSetting (*target, inst, stack, value);
            changed = true;
            if (field == n::Power || field == n::First || ! stack.isMain())
            {
                inst.setNativePlayOsc ({}, "");
                dropGesture();
                break;
            }
        }
        return changed;
    }
    nativeui::osc::SettingsView nativeSettingsView() const noexcept
    {
        namespace n = nativeui::osc;
        if (sel_ == playosc::kSub)
            return n::SettingsSub;
        if (sel_ == playosc::kNoise)
            return noiseSample_ == 1 ? n::SettingsNoiseSample : n::SettingsNoise;
        return sel_ >= 2 && sel_ <= 4
            ? static_cast<n::SettingsView> (n::SettingsWavetable + std::clamp (typeRaw_, 0, 4))
            : n::SettingsHidden;
    }

    int nativeWarpMask() const noexcept
    {
        if (full_.indexOf (oscmodel::PanelSlot::Warp) < 0)
            return 0;
        return (full_.warpApplies (0) ? 1 : 0) | (! full_.layout().warpSingle && full_.warpApplies (1) ? 2 : 0);
    }

    /** Stable full-screen field order. The generated skin uses these live bound
        items, skipping a cell when the current mode's layout hides it. */
    std::array<Item*, nativeui::osc::kSettingsAll> nativeSettingsItems()
    {
        namespace n = nativeui::osc;
        std::array<Item*, n::kSettingsAll> fields {};
        const auto view = nativeSettingsView();
        if (view == n::SettingsHidden)
            return fields;
        const bool abc = view >= n::SettingsWavetable;
        int next = view == n::SettingsSub ? 0 : n::kSubSettings;
        const int limit = view == n::SettingsSub ? n::kSubSettings : n::kSettings;
        for (auto& it : main_)
        {
            if (! it.full || it.id == kPower || it.id == kPanel + static_cast<int> (oscmodel::PanelSlot::SubBlend) || ! it.hasCell || it.alwaysOff)
                continue;
            const int abcSlot = abc ? abcNativeField (it) : -1;
            const int slot = abc ? (abcSlot >= 0 ? n::abcSettingIndex (sel_, abcSlot) : -1)
                                 : (it.param >= 0 && next < limit ? next++ : -1);
            const bool cardOctave = full_.hasNativeTune() && it.id == kTuneOct;
            if (slot >= 0 && ! it.cell.rect.empty() && (cardOctave || it.cell.shown))
                fields[slot] = &it;
        }
        return fields;
    }

    /** The cells the host's controls stand in on the full screen (the apertures of the page's ground), in the order of the fields; the oct of the tune card is the card's. */
    void setNativeSettingsPaint (const std::array<Item*, nativeui::osc::kSettingsAll>& fields)
    {
        nativeSettingsCells_.fill ({});
        nativeSettingsPaint_.fill (-1);
        for (size_t slot = 0; slot < fields.size(); ++slot)
        {
            const Item* field = fields[slot];
            nativeSettingsPaint_[slot] = field && ! (full_.hasNativeTune() && field->id == kTuneOct) ? field->id : -1;
        }
        size_t next = 0;
        for (size_t slot = 0; slot < fields.size(); ++slot)
            if (nativeSettingsPaint_[slot] >= 0 && next < nativeSettingsCells_.size())
                nativeSettingsCells_[next++] = fields[slot]->cell.rect;
    }

    const NativeCellsArray& nativeSettingsCells() const noexcept { return nativeSettingsCells_; }

    void focusNativeSetting (int x, int y, IInstanceView& inst)
    {
        for (int i = 0; i < nFullGroups_; ++i)
            if (groupRect (fullGroups_[i]).contains (x, y))
            {
                fullActive_ = i;
                publishQGroup (inst);
                return;
            }
    }

    static nativeui::FxCell nativeSettingsBinding (const Item& item) noexcept
    {
        namespace n = nativeui::osc;
        int kind;
        switch (item.type)
        {
            case T::Number: kind = n::SettingsNumber; break;
            case T::NumberOct: kind = n::SettingsOctave; break;
            case T::Toggle: kind = n::SettingsToggle; break;
            case T::Picto: kind = n::SettingsType; break;
            case T::Choice: case T::PictoPlain: kind = n::SettingsMenu; break;
            case T::UpDown: kind = n::SettingsVoices; break;
            case T::Table: case T::Source: kind = n::SettingsSource; break;
            case T::Inert: kind = item.param >= 0 ? n::SettingsMenu : n::SettingsAction; break;
            default: kind = item.id == kPanel + static_cast<int> (oscmodel::PanelSlot::SubPhase) ? n::SettingsPhase : n::SettingsDial; break;
        }
        const int target = std::max (0, item.param);
        return { target, target, kind };
    }

    static float nativeSettingsValue (nativeui::FxCell cell, float plain) noexcept
    {
        namespace n = nativeui::osc;
        if (cell.target < 0 || n::settingsAction (cell.kind))
            return 0.0f;
        if (cell.kind == n::SettingsOctave)
            return static_cast<float> (octOf (static_cast<int> (std::lround (plain))) + 4) / 8;
        if (cell.kind == n::SettingsType)
        {
            const auto end = std::end (n::kTypeChoices);
            const auto choice = std::find (std::begin (n::kTypeChoices), end, static_cast<int> (std::lround (plain)));
            return choice == end ? 0.0f : static_cast<float> (choice - std::begin (n::kTypeChoices)) / 4;
        }
        return synthyboy::toNormalised (synthyboy::paramTable()[cell.target], plain);
    }

    bool applyNativeSetting (const Item& item, IInstanceView& inst, UiStack& stack, float value)
    {
        namespace n = nativeui::osc;
        const auto cell = nativeSettingsBinding (item);
        if (n::settingsAction (cell.kind))
        {
            if (cell.kind == n::SettingsSource && item.type == T::Table)
            {
                const int action = static_cast<int> (std::lround (value * n::kLastSourceAction));
                if (action == n::SourcePrevious || action == n::SourceNext)
                {
                    turnTable (item, action == n::SourcePrevious ? -1 : 1, inst);
                    return false;
                }
                if (action != n::SourceBrowse)
                    return false;
            }
            else if (value < 0.5f)
                return false;
            handle (ui::Change::request (item.id), stack, inst);
            return stack.depth() > 1;
        }

        const auto& info = synthyboy::paramTable()[item.param];
        const auto previous = inst.param (item.param);
        float plain = synthyboy::fromNormalised (info, value);
        if (item.type == T::Number)
        {
            const auto& range = item.cell;
            plain = static_cast<float> (range.min + std::round ((plain - range.min) / range.step) * range.step);
        }
        else if (item.type == T::NumberOct)
        {
            const int octave = static_cast<int> (std::lround (value * 8.0f)) - 4;
            plain = static_cast<float> (replaceOctave (static_cast<int> (std::lround (previous)), octave));
        }
        else if (item.type == T::Picto)
            plain = static_cast<float> (n::kTypeChoices[std::clamp (static_cast<int> (std::lround (value * 4.0f)), 0, 4)]);
        plain = synthyboy::constrainValue (info, plain);
        if (std::fabs (plain - previous) <= 1.0e-6f)
            return false;
        writePlain (inst, item.param, plain);
        return item.type == T::Picto || item.id == kWarp1Type || item.id == kWarp2Type
            || item.id == kPanel + static_cast<int> (oscmodel::PanelSlot::NoiseType);
    }

    static ui::Rect nativeDetailCell (int slot, bool graph) noexcept
    {
        namespace n = nativeui::oscdetail;
        return { n::kLeft + (slot / n::kRows) * n::kWidth,
                 kTabY + (graph ? n::kGraphTop : n::kPlainTop) + (slot % n::kRows) * (graph ? n::kGraphStep : n::kPlainStep),
                 n::kWidth, n::kHeight };
    }

    ui::Rect nativeDetailRect (int field) const noexcept
    {
        namespace n = nativeui::oscdetail;
        if (field == 0 && pageTop_ == 0)
        {
            if (sel_ == playosc::kSub) return full_.subBlendRect();
            if (sel_ == playosc::kNoise)
                if (const ui::Rect blend = full_.slotRect (oscmodel::PanelSlot::SubBlend); ! blend.empty()) return blend;
        }
        if (field == n::kMore) return full_.moreRect();
        if (field == n::kBack) return { n::kBackX, kTabY + n::kBackY, n::kBackW, n::kBackH };
        if (field == n::kLoad) return { n::kLoadX, kTabY + n::kLoadY, n::kLoadW, n::kLoadH };
        if (field >= n::kRootLoad) return loadRect_;
        return nativeDetailCell (field % n::kCells, nativeDetail_ >= n::Graph);
    }

    const nativeui::oscdetail::State& prepareNativeDetail (bool enabled, uint64_t preset)
    {
        namespace n = nativeui::oscdetail;
        nativeDetailItems_.fill (nullptr);
        nativeDetailBlocks_.fill (nullptr);
        maskItem_ = nullptr;
        nativeDetailTitle_ = "";
        nativeDetailState_ = {};
        nativeDetailState_.preset = preset;
        const bool spec = oscspec::isSpecPage (pageTop_);
        const bool route = pageTop_ == kPageRoute;
        const bool detail = route || pageTop_ == kPageUnison || pageTop_ == kPageGrain || pageTop_ == kPageEnvelope;
        const bool root = stackDepth_ == 1 && pageTop_ == 0;
        if (! enabled || full_.inactive() || (! root && ! spec && ! detail))
        {
            if (nativeDetail_ >= n::Plain && spec)
                spec_.relayoutNow();
            nativeDetail_ = n::Hidden;
            return nativeDetailState_;
        }
        const bool graph = ! root && ! route && (spec ? spec_.spec()->display : true);
        const bool load = graph && loadShown_;
        if (graph)
            marksRect_ = nativeDetailGraphRect();
        nativeDetail_ = root ? (sel_ == playosc::kSub || sel_ == playosc::kNoise ? n::RootSmall : n::Root)
                             : graph ? (load ? n::GraphWithLoad : n::Graph) : n::Plain;
        auto& state = nativeDetailState_;
        state.frame = nativeDetail_;
        state.source = sel_;
        state.page = pageTop_;
        state.type = typeRaw_;
        const auto action = [&] (int field, int id, const char* label)
        {
            state.fields[field] = { id, id, n::Action };
            state.labels[field] = label;
        };
        if (root)
        {
            if (sel_ == playosc::kSub || sel_ == playosc::kNoise)
            {
                // Reuse a value cell on the root; no new host parameter/ABI slot.
                if (auto* blend = find (kPanel + static_cast<int> (oscmodel::PanelSlot::SubBlend)))
                {
                    state.fields[0] = { blend->param, blend->id, n::Uni };
                    state.labels[0] = "BLEND";
                    nativeDetailItems_[0] = blend;
                }
            }
            else
                action (n::kMore, kTuneMore, "MORE");
            if (loadShown_ && nativeDetail_ == n::Root)
                action (n::kRootLoad + playosc::oscNumber (sel_) - 1, kSpecLoad, loadLabel());
            return state;
        }
        action (n::kBack, kDetailBack, "BACK");
        if (route)
        {
            nativeDetailTitle_ = "ROUTING";
            static const char* const labels[] = { "DESTINATION", "F1 / F2", "BUS 1", "BUS 2" };
            for (int i = 0; i < 4; ++i)
            {
                auto* item = find (kRouteTarget + i);
                if (! item || item->param < 0 || (i == 1 && ! routeFilter_))
                    continue;
                const int field = n::slot (i, 0);
                const int kind = i == 0 ? n::Choice : i == 1 ? n::Bi : n::Uni;
                nativeDetailItems_[field] = item;
                state.fields[field] = { item->param, item->id, kind };
                state.labels[field] = labels[i];
                if (kind == n::Choice)
                    state.fields[field + n::kCells] = state.fields[field];
            }
            return state;
        }
        if (load)
            action (n::kLoad, kSpecLoad, loadLabel());
        const auto add = [&] (int block, int cell, Item* item, const char* label)
        {
            const int rows = graph ? n::kRows - 1 : n::kRows;
            if (item == nullptr || ! item->hasCell || item->alwaysOff || cell >= 4 || block >= rows * 2)
                return;
            const int field = n::slot ((block % 2) * 4 + cell, block / 2);
            nativeDetailItems_[field] = item;
            if (item->type == T::Mask)
            {
                // the twelve notes: three cells wide, two rows of kMaskRowH and a gap (the native cells of the skin, components/native/note_mask)
                maskItem_ = item;
                maskArea_ = nativeDetailRect (field);
                maskArea_.w *= 3;
                maskArea_.h = kMaskHeight;
                return;
            }
            const int target = item->param;
            if (item->type == T::Inert)
                action (field, item->id, label);
            else if (target >= 0)
            {
                const auto& pi = info (target);
                const int kind = nativeDetailKind (*item, pi);
                state.fields[field] = { target, item->id, kind };
                if (kind == n::Choice)
                    state.fields[field + n::kCells] = { target, item->id, kind };
                state.labels[field] = label != nullptr ? label : pi.name;
            }
        };
        if (const auto* definition = spec ? spec_.spec() : nullptr)
        {
            nativeDetailTitle_ = definition->title;
            for (int b = 0; b < definition->numBlocks; ++b)
            {
                const auto& block = definition->blocks[b];
                int cell = 0;
                for (int i = block.first; i < block.first + block.n; ++i)
                    if (spec_.visible (i))
                    {
                        nativeDetailBlocks_[b] = block.title;
                        add (b, cell, find (kSpec + i), definition->ctls[i].label);
                        cell += definition->ctls[i].kind == oscspec::K::Mask ? 3 : 1;
                    }
            }
        }
        else if (const auto* panel = detailPreset_)
        {
            nativeDetailTitle_ = pageTop_ == kPageUnison ? "UNISON" : pageTop_ == kPageGrain ? "GRAIN WINDOW" : "ENVELOPE";
            for (int g = 0; g < nDetailGroups_; ++g)
            {
                if (g < panel->numBlocks)
                    nativeDetailBlocks_[g] = panel->blocks[g].title;
                for (int cell = 0; cell < 4; ++cell)
                    if (detailGroups_[g].id[cell] >= 0)
                    {
                        const auto* partInfo = std::find_if (panel->items, panel->items + panel->numItems,
                            [&] (const oscmodel::PanelItem& pi) { return pi.slot == panel->q[g].slot[cell]; });
                        add (g, cell, find (detailGroups_[g].id[cell]), partInfo != panel->items + panel->numItems ? partInfo->label : nullptr);
                    }
            }
        }
        return state;
    }

    int nativeDetailFieldAt (int x, int y) const noexcept
    {
        namespace n = nativeui::oscdetail;
        for (int field = 0; field < n::kBindings; ++field)
            if ((field < n::kCells || field >= n::kMore) && nativeDetailState_.fields[field].target >= 0
                && nativeDetailRect (field).contains (x, y))
                return field;
        return -1;
    }

    int nativeDetailMenu (int field) const noexcept
    {
        namespace n = nativeui::oscdetail;
        return field >= 0 && field < n::kCells && nativeDetailState_.fields[field].kind >= n::kFirstMenuKind
            ? nativeDetailState_.fields[field].kind - n::kFirstMenuKind : -1;
    }

    void clearNativeDetailPixels (gpu::SceneBuilder& sb) const
    {
        namespace n = nativeui::oscdetail;
        for (int field = 0; field < n::kBindings; ++field)
            if ((field < n::kCells || field >= n::kMore) && nativeDetailState_.fields[field].target >= 0)
            {
                const auto r = nativeDetailRect (field);
                const gpu::ScopedClip clip (sb, r.x, r.y, r.right(), r.bottom());
                if (clip)
                    sb.clear (0u);
            }
    }

    void focusNativeDetail (int field, IInstanceView& v)
    {
        namespace n = nativeui::oscdetail;
        if (field >= 0 && field < n::kCells && nativeDetailItems_[field] && nativeDetailItems_[field]->hasCell)
            focusNativeDetail (*nativeDetailItems_[field], v);
    }

    bool nativeDetailCanModulate (int field) const noexcept
    {
        namespace n = nativeui::oscdetail;
        if (field < 0 || field >= n::kCells)
            return false;
        const int kind = n::kind (nativeDetailState_.fields[field].kind);
        return kind == n::Uni || kind == n::Bi || kind == n::Number;
    }

    bool nativeDetailPower (int field) const noexcept
    {
        return field >= 0 && field < nativeui::oscdetail::kCells && nativeDetailItems_[field] != nullptr
            && nativeDetailItems_[field]->id == kSpec && pageTop_ == oscspec::kOptions;
    }

    std::pair<int, int> nativeModTarget (int x, int y) const noexcept
    {
        const int field = nativeDetailFieldAt (x, y);
        if (nativeDetailCanModulate (field) && nativeDetailItems_[field] != nullptr)
            return { nativeDetailItems_[field]->id, nativeDetailItems_[field]->param };
        if (pageTop_ == 0 && ! full_.inactive())
        {
            const auto modParam = [] (int id, int param) noexcept -> std::pair<int, int>
            {
                if (param >= 0 && synthyboy::modDestinationChoiceOf (static_cast<size_t> (param)) != 0)
                    return { id, param };
                return { -1, -1 };
            };
            if (isNativeRoutingActive())
            {
                if (full_.panArea().contains (x, y))
                    if (const auto* it = item (kBasic + 6))
                        return modParam (kBasic + 6, it->param);
                if (full_.levelArea().contains (x, y))
                    if (const auto* it = item (kBasic + 7))
                        return modParam (kBasic + 7, it->param);
                if (full_.hasNativeTune() && full_.tuneArea().contains (x, y))
                {
                    const auto r = full_.tuneArea();
                    if (y >= r.y + 96)
                        return { -1, -1 };
                    const bool left = x < r.x + r.w / 2;
                    if (y < r.y + 59)
                        return modParam (kTuneOct, tuneTargets_[1]);
                    return left ? modParam (kTuneFin, tuneTargets_[2])
                                : modParam (kTuneCrs, tuneTargets_[3]);
                }
            }
            for (const auto& item : main_)
                if (item.param >= 0 && item.full && item.hasCell && item.cell.rect.contains (x, y)
                    && (item.type == T::Dial || item.type == T::Number || item.type == T::NumberOct || item.type == T::UpDown)
                    && synthyboy::modDestinationChoiceOf (static_cast<size_t> (item.param)) != 0
                    && std::find (nativeSettingsPaint_.begin(), nativeSettingsPaint_.end(), item.id) != nativeSettingsPaint_.end())
                    return { item.id, item.param };
        }
        return { -1, -1 };
    }

    void openNativeMods (int id, UiStack& stack, IInstanceView& v) { handle (ui::Change::modsRequest (id), stack, v); }

    bool applyNativeDetail (int field, float value, IInstanceView& v, UiStack& stack)
    {
        namespace n = nativeui::oscdetail;
        const auto cell = nativeDetailState_.fields[field];
        if (cell.target < 0 || full_.inactive())
            return false;
        if (cell.kind == n::Action)
        {
            if (value < 0.5f)
                return false;
            if (cell.source == kSpecLoad)
                openBrowser (stack, v);
            else
                handle (ui::Change::request (cell.source), stack, v);
            return true;
        }
        auto* item = nativeDetailItems_[field % n::kCells];
        if (! item || (item->id == kRouteBalance && std::lround (v.param (nativeDetailState_.fields[n::slot (0, 0)].target)) != 5))
            return false;
        const auto& pi = info (cell.target);
        const float old = v.param (cell.target);
        float plain = synthyboy::fromNormalised (pi, value);
        switch (cell.kind)
        {
            case n::Choice:
            {
                if (field < n::kCells || value < 0.2f)
                    return false;
                const int length = std::max (1, static_cast<int> (pi.numChoices));
                const int index = static_cast<int> (std::lround (old - pi.min));
                const int delta = value < 0.65f ? -1 : 1;
                plain = pi.min + (item->type == T::Choice ? (index + delta + length) % length
                                                          : std::clamp (index + delta, 0, length - 1));
                break;
            }
            case n::Semitone:
                plain = static_cast<float> (transposeFrom (octOf (static_cast<int> (std::lround (old))), static_cast<int> (std::lround (22.0f * value)) - 11));
                break;
            case n::Snap: plain = static_cast<float> ((static_cast<int> (std::lround (old)) & 4095) | (value >= 0.5f ? 4096 : 0)); break;
            case n::Number:
            {
                const auto& range = item->cell;
                plain = static_cast<float> (range.min + std::round ((plain - range.min) / range.step) * range.step);
                break;
            }
            default: break;
        }
        plain = synthyboy::constrainValue (pi, plain);
        if (plain == old)
            return false;
        writePlain (v, cell.target, plain);
        return true;
    }

    const char* nativeSourceLabel() const noexcept
    {
        for (const Item& it : main_)
            if (it.full && it.id == kBasic + 1)
                return it.cell.text;
        return "";
    }


    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return qActive_; }
    /** Finds the item (main or page) of an id; null when there is none. */
    const Item* item (int id) const noexcept { for (const Item& it : main_) if (it.id == id) return &it; for (const Item& it : page_) if (it.id == id) return &it; return nullptr; }

    // ---- the modulation source ids of the matrix (modSourceChoice: env n = n, lfo n = 6 + n for n <= 8, lfo 9 = 33, lfo 10 = 34)
    static int envSource (int n) noexcept { return n >= 1 && n <= 6 ? n : ui::kNoSource; }
    static int lfoSource (int n) noexcept { return n >= 1 && n <= 8 ? 6 + n : (n == 9 ? 33 : (n == 10 ? 34 : ui::kNoSource)); }
    static constexpr double kModsRefreshMs = 100.0;

    /** Converts a value of the OCT / SEM number fields into the transpose in semitones (the other field keeps its part). */
    int abcNativeField (const Item& item) const noexcept
    {
        namespace n = nativeui::osc;
        using oscmodel::PanelSlot;
        if (item.full && item.id == kPanel + static_cast<int> (positionSlot (kindOfRaw (typeRaw_))))
            return n::AbcPosition;
        switch (item.id)
        {
            // These three granular slots are available on the wavetable layout.
            case kWavePhase: return n::AbcDensity;
            case kWaveRandom: return n::AbcLength;
            case kWaveMap: return n::AbcOffset;
            case kBasic: return n::AbcType;
            case kBasic + 1: return n::AbcSource;
            case kBasic + 2: return n::AbcBrowse;
            case kTuneOct: return n::AbcOctave;
            case kTuneFin: return n::AbcFine;
            case kUnisonGear: return n::AbcUnisonDetail;
            case kUnisonCount: return n::AbcUnisonCount;
            case kPanel + static_cast<int> (PanelSlot::Detune): return n::AbcDetune;
            case kPanel + static_cast<int> (PanelSlot::WarpMenu): case kWarp1Type: return n::AbcWarp1Type;
            case kWarp1Amount: return n::AbcWarp1Amount;
            case kWarp2Type: return n::AbcWarp2Type;
            case kWarp2Amount: return n::AbcWarp2Amount;
            case kPanel + static_cast<int> (PanelSlot::Dens): return n::AbcDensity;
            case kPanel + static_cast<int> (PanelSlot::Length): return n::AbcLength;
            case kPanel + static_cast<int> (PanelSlot::Offset): return n::AbcOffset;
            case kPanel + static_cast<int> (PanelSlot::Dir): return n::AbcDirection;
            case kPanel + static_cast<int> (PanelSlot::Pitch): return n::AbcPitch;
            default: return -1;
        }
    }
    static int transposeFrom (int oct, int sem) noexcept { return std::clamp (oct * 12 + sem, -48, 48); }
    static int octOf (int transpose) noexcept { return transpose / 12; }
    static int semOf (int transpose) noexcept { return transpose - 12 * (transpose / 12); }
    static int replaceOctave (int transpose, int octave) noexcept { return transposeFrom (octave, semOf (transpose)); }

private:
    static int nativeDetailKind (const Item& item, const synthyboy::ParamInfo& pi) noexcept
    {
        namespace n = nativeui::oscdetail;
        switch (item.type)
        {
            case T::Number: return item.cell.format == oscmodel::NumberFormat::Degrees ? n::Phase : n::Number;
            case T::NumberDeg: return n::Phase;
            case T::NumberSem: return n::Semitone;
            case T::Toggle: return n::Toggle;
            case T::SnapBit: return n::Snap;
            case T::Choice: case T::List:
            {
                const std::string_view key (pi.key);
                const auto suffix = key.substr (key.find ('.') + 1);
                const auto end = std::end (n::kMenuSuffixes);
                const auto menu = std::find (std::begin (n::kMenuSuffixes), end, suffix);
                return menu != end ? n::kFirstMenuKind + static_cast<int> (menu - std::begin (n::kMenuSuffixes)) : n::Choice;
            }
            default: return pi.min < 0.0f && pi.max > 0.0f ? n::Bi : n::Uni;
        }
    }

    static ui::Rect nativeDetailGraphRect() noexcept
    {
        namespace n = nativeui::oscdetail;
        return { 4, kTabY + n::kGraphY, kCanvasW - 8, n::kGraphHeight };
    }

    /** The Q group of the page on screen that holds the item, and its slot there ({ -1, -1 }: none). */
    std::pair<int, int> nativeDetailQ (const Item* member) const noexcept
    {
        if (member == nullptr)
            return { -1, -1 };
        if (oscspec::isSpecPage (pageTop_))
        {
            int slot = 0;
            const int g = member->id >= kSpec ? spec_.groupOf (member->id - kSpec, &slot) : -1;
            return g >= 0 ? std::pair<int, int> { g, slot } : std::pair<int, int> { -1, -1 };
        }
        const QGroupIds* groups = pageTop_ == 0 ? fullGroups_ : detailGroups_;
        const int count = pageTop_ == 0 ? nFullGroups_ : nDetailGroups_;
        for (int g = 0; g < count; ++g)
            for (int key = 0; key < 4; ++key)
                if (groups[g].id[key] == member->id && member->id >= 0)
                    return { g, key };
        return { -1, -1 };
    }

    void focusNativeDetail (const Item& part, IInstanceView& v)
    {
        const int group = nativeDetailQ (&part).first;
        if (group < 0)
            return;
        if (pageTop_ == 0)
            fullActive_ = group;
        else if (oscspec::isSpecPage (pageTop_))
            spec_.setActiveGroup (group);
        else
            detailActive_ = group;
        publishQGroup (v);
    }
    void drawNativeDetail (gpu::SceneBuilder& sb) const
    {
        namespace n = nativeui::oscdetail;
        const auto& look = ui::theme::L();
        ui::Pen pen (sb, true);
        {
            const ui::Rect header { 4, kTabY, kCanvasW - 8, 64 };
            ui::Cell cell (sb, header);
            pen.rect (header.x, header.bottom() - 1, header.right(), header.bottom(), pen.c (look.keyline, 255));
            pen.textLeft ({ 140, kTabY + 4, kCanvasW - 150, 56 }, nativeDetailTitle_, look.fTitle, pen.c (look.text, 255), look.fMin);
        }
        if (nativeDetail_ >= n::Graph)
        {
            const auto graph = nativeDetailGraphRect();
            ui::Cell cell (sb, graph);
            pen.block (graph);
        }
        for (int b = 0; b < static_cast<int> (nativeDetailBlocks_.size()); ++b)
            if (const char* title = nativeDetailBlocks_[b])
            {
                const auto r = nativeDetailRect (n::slot ((b % 2) * 4, b / 2));
                const ui::Rect area { r.x + 32, r.y - 16, r.w - 36, 16 };
                ui::Cell cell (sb, area);
                pen.textLeft (area, title, look.fLabel, pen.c (look.textDim, 255), look.fMin);
            }
        drawNativeDetailQFrame (sb);
    }

    void drawNativeDetailQFrame (gpu::SceneBuilder& sb) const
    {
#if SYNTHYBOY_MPC_QFRAME_ENABLED
        if (ui::QGroup::wideDevice())
            return;
        namespace n = nativeui::oscdetail;
        ui::Pen pen (sb, true);
        const bool spec = oscspec::isSpecPage (pageTop_);
        const int active = spec ? spec_.activeGroup() : detailActive_;
        ui::Rect frame;
        for (int i = 0; i < n::kCells; ++i)
            if (const auto* part = nativeDetailItems_[i])
                if (pageTop_ == kPageRoute || nativeDetailQ (part).first == active)
                {
                    const auto rect = part->type == T::Mask ? maskArea_ : nativeDetailRect (i);
                    frame = frame.empty() ? rect : ui::Rect::unite (frame, rect);
                }
        if (! frame.empty())
        {
            frame = frame.inset (-3);
            ui::Cell cell (sb, ui::Rect::unite (frame, ui::Pen::qMarkerRect (frame)));
            pen.qFocus (frame);
        }
#else
        (void) sb;
#endif
    }

    // ======================================================================================================================================================================
    // the parameters
    static int indexOfKey (const std::string& key) noexcept
    {
        if (key.empty())
            return -1;
        const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key.c_str());
        return p != nullptr ? static_cast<int> (p - synthyboy::paramTable()) : -1;
    }
    int typeParam (int sel) const noexcept
    {
        if (sel == playosc::kSub) return indexOfKey ("sub.waveform");
        if (sel == playosc::kNoise) return indexOfKey ("noise.sample");
        return indexOfKey (std::string ("osc") + std::to_string (playosc::oscNumber (sel)) + ".type");
    }
    static const synthyboy::ParamInfo& info (int idx) noexcept { return synthyboy::paramTable()[idx]; }
    static float plainOf (const IInstanceView& v, int idx) noexcept { return v.param (idx); }
    static double normOf (int idx, float plain) noexcept { return synthyboy::toNormalised (info (idx), plain); }
    static void writePlain (IInstanceView& v, int idx, double plain) noexcept
    {
        const synthyboy::ParamInfo& i = info (idx);
        v.writeParam (idx, static_cast<float> (synthyboy::toNormalised (i, synthyboy::constrainValue (i, static_cast<float> (plain)))));
    }
    static void writeNorm (IInstanceView& v, int idx, double norm) noexcept { v.writeParam (idx, static_cast<float> (std::clamp (norm, 0.0, 1.0))); }

    // ======================================================================================================================================================================
    // building: the instance (and the type) decides the components, the ids and the bindings
    oscmodel::Instance instance() const noexcept { return static_cast<oscmodel::Instance> (std::clamp (sel_ - 1, 0, 4)); }
    static oscmodel::Kind kindOfRaw (int raw) noexcept { return raw == 4 ? oscmodel::Kind::Multisample : raw == 2 ? oscmodel::Kind::Granular : raw == 1 ? oscmodel::Kind::Sample : raw == 3 ? oscmodel::Kind::Spectral : oscmodel::Kind::Wavetable; }
    /** The panel slot of the position knob of the top row: WT POS, TIMBRE, SCAN (sample), POSITION (spectral), SCAN (granular). */
    static oscmodel::PanelSlot positionSlot (oscmodel::Kind kind) noexcept
    {
        using oscmodel::PanelSlot;
        return kind == oscmodel::Kind::Wavetable ? PanelSlot::WtPos : kind == oscmodel::Kind::Multisample ? PanelSlot::Timbre : kind == oscmodel::Kind::Sample ? PanelSlot::SampleScan
             : kind == oscmodel::Kind::Spectral ? PanelSlot::SpecPos : PanelSlot::Scan;
    }

    /** One row: the id, the type of the value, the key of the parameter (the prefix of the instance is put before it) and the name of the cell. */
    void bind (std::vector<Item>& list, int id, T type, const std::string& suffix, const char* label = "", bool hasCell = true)
    {
        Item it;
        it.id = id; it.type = type; it.hasCell = hasCell; it.full = bindingFull_;
        it.key = suffix.empty() ? std::string() : prefix_ + suffix;
        it.param = indexOfKey (it.key);
        oscmodel::setLabel (it.cell, label);
        list.push_back (it);
    }

    void configure (IInstanceView* inst, int sel, bool first, int raw = 0)
    {
        (void) first;
        sel_ = sel;
        typeRaw_ = raw;
        configured_ = true;
        const bool sub = sel == playosc::kSub, noise = sel == playosc::kNoise;
        prefix_ = sub ? "sub." : (noise ? "noise." : std::string ("osc") + std::to_string (playosc::oscNumber (sel)) + ".");
        const oscmodel::Kind kind = kindOfRaw (raw);
        const synthyboy::ParamInfo* ti = nullptr;
        const int tp = typeParam (sel);
        if (tp >= 0) ti = &info (tp);
        if (ti != nullptr && ! sub && ! noise && raw >= 0 && raw < ti->numChoices)
            ui::setText (typeName_, sizeof typeName_, ti->choices[raw]);
        else
            typeName_[0] = '\0';
        noiseSample_ = -1;
        if (noise)
        {
            const int ni = indexOfKey ("noise.type");
            noiseSample_ = inst != nullptr && ni >= 0 && inst->param (ni) >= 0.5f ? 1 : 0;
        }
        full_.setInstance (instance(), kind, noiseSample_ == 1);
        full_.setPageTab (pageSel_ > 0);
        fullActive_ = 0;
        main_.clear();
        // ---- the compact view
        {
            const bool abc = ! sub && ! noise;
            bind (main_, kInstances, T::Inert, "");
            bind (main_, kFullButton, T::Inert, "");
            bind (main_, kPower, T::Toggle, "on");
            bind (main_, kRoute, T::Route, "destination");
            const std::string typeKey = sub ? "waveform" : (noise ? "" : "type");
            if (! noise)
                bind (main_, kBasic + 0, sub ? T::PictoPlain : T::Picto, typeKey);
            if (noise)
                bind (main_, kBasic + 5, T::Choice, "sample", "SOUND");
            if (abc)
                bindSource (kind);
            if (sub)
                bind (main_, kBasic + 3, T::Number, "octave", "OCT");
            if (noise)
                bind (main_, kBasic + 4, T::Dial, "filter", "FILTER");
            bind (main_, kBasic + 6, T::Dial, "pan", "PAN");
            bind (main_, kBasic + 7, T::Dial, "level", "LEVEL");
        }
        tuneFocus_ = -1;
        if (! sub && ! noise)
        {
            const int transpose = indexOfKey (prefix_ + "transpose");
            tuneTargets_ = { QRouter::encode (transpose, QRouter::kOct), transpose, indexOfKey (prefix_ + "tune"),
                             indexOfKey (prefix_ + "pitch_coarse"), indexOfKey (prefix_ + "pitch_track") };
        }
        bindFull (kind);
        if (inst != nullptr)
            resolveQ (*inst);
        configureNumberRanges (main_);
        page_.clear();
        detailPreset_ = nullptr;
        nDetailGroups_ = 0;
        pageLoaded_ = 0;
        pageTop_ = 0;
        modsDirty_ = true;
        qSlots_.clear();
        qSlots_.forget();
        relayout();
    }

    /** The source of a granular / multisample / sample / spectral oscillator is content (a file), not a parameter: its name is shown and a tap opens the content browser (the folders of the machine, the WAV files, the SFZ instruments); the
        wavetable's source is the table (the built-in tables and the table files, the wavetable browser). */
    void bindSource (oscmodel::Kind kind)
    {
        const bool contentType = kind != oscmodel::Kind::Wavetable;
        const bool tableType = kind == oscmodel::Kind::Wavetable;
        bind (main_, kBasic + 1, contentType ? T::Source : T::Table, tableType ? "wavetable" : "", "SOURCE");
        bind (main_, kBasic + 2, T::Inert, "", "BROWSE");
    }

    /** The full screen's rows and their bindings: the top row (the first Q group), the MORE button and the bottom band. Level, pan and destination are not here (read-only indicator, the Mixer is their home). */
    void bindFull (oscmodel::Kind kind)
    {
        using oscmodel::PanelSlot;
        const bool sub = sel_ == playosc::kSub, noise = sel_ == playosc::kNoise, abc = ! sub && ! noise;
        bindingFull_ = true;
        bind (main_, kClose, T::Inert, "", "CLOSE");
        bind (main_, kPower, T::Toggle, "on");
        if (abc)
            bind (main_, kTuneMore, T::Inert, "", "MORE");
        if (! noise)
            bind (main_, kBasic + 0, sub ? T::PictoPlain : T::Picto, sub ? "waveform" : "type");
        if (abc)
        {
            bindSource (kind);
            if (kind == oscmodel::Kind::Wavetable)
            {
                bind (main_, kWavePhase, T::Dial, "phase", "PHASE");
                bind (main_, kWaveRandom, T::Dial, "random_phase", "RAND");
                bind (main_, kWaveMap, T::Inert, "", "MAP");
            }
            const PanelSlot ps = positionSlot (kind);
            bind (main_, kPanel + static_cast<int> (ps), T::Dial, slotKey (ps),
                  kind == oscmodel::Kind::Wavetable ? "WT POS" : kind == oscmodel::Kind::Multisample ? "TIMBRE" : kind == oscmodel::Kind::Spectral ? "POSITION" : "SCAN");
            bind (main_, kTuneOct, T::NumberOct, "transpose", "OCT");
        }
        else if (sub)
        {
            bind (main_, kBasic + 3, T::Number, "octave", "OCT");
            bind (main_, kPanel + static_cast<int> (PanelSlot::SubCrs), T::Dial, "tune", "CRS");
            bind (main_, kPanel + static_cast<int> (PanelSlot::SubPhase), T::Dial, "phase", "PHASE");
        }
        else
        {
            bind (main_, kPanel + static_cast<int> (PanelSlot::NoiseType), T::Choice, "type", "KIND");
            bind (main_, kBasic + 5, T::Choice, "sample", "SOUND");
            bind (main_, kBasic + 4, T::Dial, "filter", "FILTER");
            bind (main_, kPanel + static_cast<int> (PanelSlot::NoiseStereo), T::Dial, "stereo", "STEREO");
        }
        bindPanel (main_, full_.layout().preset, kPanel, kind, abc);
        if (sub)
            bind (main_, kPanel + static_cast<int> (PanelSlot::SubBlend), T::Dial, "filter_blend", "BLEND");
        bindingFull_ = false;
    }
    /** The full screen again (the noise changed its mode: another bottom band): its rows are dropped and bound anew. */
    void rebindFull()
    {
        main_.erase (std::remove_if (main_.begin(), main_.end(), [] (const Item& it) { return it.full; }), main_.end());
        bindFull (kindOfRaw (typeRaw_));
        configureNumberRanges (main_);
        relayout();
    }

    /** The rows of the parts of a preset with their ids (base + slot) and bindings; `abc`: the panel belongs to A, B or C. */
    void bindPanel (std::vector<Item>& list, const oscmodel::PanelPreset* pp, int base, oscmodel::Kind kind, bool abc)
    {
        using oscmodel::PanelSlot;
        if (pp == nullptr)
            return;
        for (int i = 0; i < pp->numItems; ++i)
        {
            const oscmodel::PanelItem& it = pp->items[i];
            if (bindingFull_ && sel_ == playosc::kSub && (it.slot == PanelSlot::SubCrs || it.slot == PanelSlot::SubPhase))
                continue;
            if (bindingFull_ && sel_ == playosc::kNoise && (it.slot == PanelSlot::Filter || it.slot == PanelSlot::NoiseStereo))
                continue;
            int id = base + static_cast<int> (it.slot);
            const std::string k = slotKey (it.slot);
            switch (it.kind)
            {
                case oscmodel::PartKind::Number:   // FIN, the noise's PITCH and FINE: the ids of the tuning column (their ranges and formats are set by id in configureNumberRanges)
                    id = it.slot == PanelSlot::NoisePitch ? kTuneCrs : kTuneFin;
                    if (base == kPanel)
                        bind (list, id, T::Number, k, it.label);
                    break;
                case oscmodel::PartKind::Dial: bind (list, id, T::Dial, k, it.label); break;
                case oscmodel::PartKind::Choice: bind (list, id, T::Choice, k, it.label); break;
                case oscmodel::PartKind::Toggle: bind (list, id, T::Toggle, k, it.label); break;
                case oscmodel::PartKind::Menu: bind (list, id, T::Inert, k, it.label); break;   // the WARP menu of the granular oscillator: a tap steps through the warp types
                case oscmodel::PartKind::Unison:
                    bind (list, kUnisonGear, T::Inert, "", "DETAIL");
                    bind (list, kUnisonCount, T::UpDown, "unison.voices", "VOICES");
                    break;
                case oscmodel::PartKind::WarpPair:
                    bind (list, kWarp1Type, T::Choice, "warp.type");
                    bind (list, kWarp1Amount, T::Dial, "warp.amount", "WARP 1");
                    bind (list, kWarp2Type, T::Choice, "warp2.type");
                    bind (list, kWarp2Amount, T::Dial, "warp2.amount", "WARP 2");
                    break;
                case oscmodel::PartKind::RandPair: break;
            }
        }
        (void) kind; (void) abc;
    }
    void updateNoiseType (bool sample, IInstanceView& inst)
    {
        const int sInt = sample ? 1 : 0;
        if (noiseSample_ == sInt)
            return;
        noiseSample_ = sInt;
        full_.setInstance (instance(), oscmodel::Kind::Wavetable, sample);
        rebindFull();
        resolveQ (inst);
    }
    static const char* slotKey (oscmodel::PanelSlot s) noexcept
    {
        using oscmodel::PanelSlot;
        switch (s)
        {
            case PanelSlot::WtPos: return "frame";
            case PanelSlot::Unison: return "unison.voices";
            case PanelSlot::Detune: return "unison.detune";
            case PanelSlot::Blend: return "unison.blend";
            case PanelSlot::Warp: return "warp.type";
            case PanelSlot::Pan: return "pan";
            case PanelSlot::Level: return "level";
            case PanelSlot::WarpMenu: return "warp.type";
            case PanelSlot::Scan: return "gran.position";
            case PanelSlot::Dens: return "gran.density";
            case PanelSlot::Length: return "gran.length";
            case PanelSlot::Offset: return "gran.rand_offset";
            case PanelSlot::Dir: return "gran.rand_dir";
            case PanelSlot::Pitch: return "gran.rand_pitch";
            case PanelSlot::Filter: return "filter";
            case PanelSlot::Mode: return "unison.mode";
            case PanelSlot::Stack: return "unison.stack";
            case PanelSlot::Width: return "unison.spread";
            case PanelSlot::Range: return "unison.range";
            case PanelSlot::WtSpread: return "frame_spread";
            case PanelSlot::Warp1Spread: return "warp.spread";
            case PanelSlot::Warp2Spread: return "warp2.spread";
            case PanelSlot::Amount: return "gran.window_amount";
            case PanelSlot::AmountRand: return "gran.rand_window";
            case PanelSlot::Skew: return "gran.window_skew";
            case PanelSlot::SkewRand: return "gran.rand_skew";
            case PanelSlot::Shape: return "gran.window_shape";
            case PanelSlot::LengthRand: return "gran.rand_length";
            case PanelSlot::PanRand: return "gran.rand_pan";
            case PanelSlot::LevelRand: return "gran.rand_level";
            case PanelSlot::Timbre: return "multi.timbre";
            case PanelSlot::Start: return "sample.uni_start";
            case PanelSlot::Delay: return "multi.delay";
            case PanelSlot::Attack: return "multi.attack";
            case PanelSlot::Hold: return "multi.hold";
            case PanelSlot::Decay: return "multi.decay";
            case PanelSlot::Sustain: return "multi.sustain";
            case PanelSlot::Release: return "multi.release";
            case PanelSlot::Override: return "multi.override";
            case PanelSlot::NoiseType: return "type";
            case PanelSlot::NoiseStart: return "phase";
            case PanelSlot::NoiseRand: return "random_phase";
            case PanelSlot::NoiseLoop: return "loop";
            case PanelSlot::NoiseBounce: return "bounce";
            case PanelSlot::NoiseStereo: return "stereo";
            case PanelSlot::NoiseEnv1: return "env1";
            case PanelSlot::Fin: return "tune";
            case PanelSlot::NoisePitch: return "pitch";
            case PanelSlot::NoiseFine: return "fine";
            case PanelSlot::NoiseKey: return "keytrack";
            case PanelSlot::SubCrs: return "tune";
            case PanelSlot::SubEnv1: return "env1";
            case PanelSlot::SubMono: return "mono";
            case PanelSlot::SubPhase: return "phase";
            case PanelSlot::SubContig: return "phase_contiguous";
            case PanelSlot::SubDest: return "destination";
            case PanelSlot::SubBlend: return "filter_blend";
            case PanelSlot::SubBus1: return "bus1_send";
            case PanelSlot::SubBus2: return "bus2_send";
            case PanelSlot::SampleScan: return "sample.scan";
            case PanelSlot::SpecPos: return "spec.position";
            case PanelSlot::Span: return "sample.uni_span";
            default: return "";
        }
    }

    /** The ranges of the number fields come from the parameters (once per configuration; a detail page's after its load). */
    void configureNumberRanges (std::vector<Item>& list)
    {
        for (Item& it : list)
        {
            if (! it.hasCell || it.param < 0)
                continue;
            const synthyboy::ParamInfo& pi = info (it.param);
            oscmodel::Cell& c = it.cell;
            if (it.type == T::Number)
            {
                c.min = pi.min; c.max = pi.max; c.def = pi.def;
                const bool fine = (it.id == kTuneCrs && sel_ != playosc::kNoise) || it.key == prefix_ + "pitch_coarse";   // CRS: hundredths of a semitone; the noise's PITCH: whole semitones
                c.step = fine ? 0.01 : (pi.max - pi.min > 200.0f ? 10.0 : 1.0);
                if (fine) c.format = oscmodel::NumberFormat::Decimal2;
                else if (it.id == kTuneCrs) c.format = oscmodel::NumberFormat::IntSigned;
                if (it.id == kTuneFin) c.format = oscmodel::NumberFormat::IntSigned;
            }
            else if (it.type == T::NumberDeg) { c.min = 0; c.max = 360; c.step = 1; c.def = 0; }
            else if (it.type == T::NumberOct) { c.min = -4; c.max = 4; c.step = 1; c.def = 0; }
            else if (it.type == T::NumberSem) { c.min = -11; c.max = 11; c.step = 1; c.def = 0; }
            else if (it.type == T::Dial)
            {
                c.bipolar = pi.min < 0.0f && pi.max > 0.0f;
                c.def = synthyboy::toNormalised (pi, pi.def);
            }
            else if (it.type == T::UpDown) { c.min = static_cast<int> (pi.min); c.max = static_cast<int> (pi.max); }
        }
    }

    // ======================================================================================================================================================================
    // the geometry: the cells of the rows are placed into the rectangles the model (osc_model.h) lays out
    /** The compact view, the full screen and the detail page are laid out again (the canvas, the instance, the type, the native parts changed) and every row gets its rectangle. */
    void relayout()
    {
        cl_ = oscmodel::layoutCompact (compactRect_, instance());
        if (detailPreset_ != nullptr)
            detailLayout_ = oscmodel::layoutDetail (*detailPreset_, tabRect_);
        for (Item& it : main_)
            placeItem (it, it.full);
        for (Item& it : page_)
            placeDetailItem (it);
        buildFullGroups();
    }
    static int slotOfId (int id) noexcept { return id >= kPanel && id < kPanel + static_cast<int> (oscmodel::PanelSlot::Count) ? id - kPanel : -1; }
    void placeItem (Item& it, bool full)
    {
        using oscmodel::PanelSlot;
        oscmodel::Cell& c = it.cell;
        const bool sub = sel_ == playosc::kSub, noise = sel_ == playosc::kNoise, abc = ! sub && ! noise;
        c.rect = {};
        c.shown = true;
        if (! it.hasCell)
            return;
        if (! full)
        {
            switch (it.id)
            {
                case kInstances: c.rect = cl_.instances; c.shown = false; break;   // the host's own selector
                case kFullButton: c.rect = cl_.full; break;
                case kPower: c.rect = cl_.power; break;
                case kRoute: c.rect = cl_.route; break;
                case kBasic + 0: case kBasic + 5: c.rect = cl_.first; break;
                case kBasic + 1: case kBasic + 3: case kBasic + 4: c.rect = cl_.second; break;
                case kBasic + 2: c.shown = false; break;
                case kBasic + 6: c.rect = cl_.pan; break;
                case kBasic + 7: c.rect = cl_.level; break;
                default: break;
            }
            return;
        }
        const oscmodel::FullLayout& L = full_.layout();
        const bool tune = full_.hasNativeTune();
        const bool waveTools = full_.hasNativeInstanceSelector() && tune && full_.layout().preset != nullptr && typeRaw_ == 0 && abc;
        switch (it.id)
        {
            case kClose: c.rect = L.close; c.shown = ! full_.isPage() && ! nativeSelector_; return;
            case kPower: c.rect = L.power; c.shown = ! full_.hasNativePower(); return;
            case kTuneMore: c.rect = L.more; return;
            case kBasic + 0: c.rect = L.tSelector; c.shown = ! noise; return;
            case kBasic + 1: c.rect = L.tSourceStepper; return;
            case kBasic + 2: c.rect = L.tSourceDropdown; return;
            case kWavePhase: c.rect = L.tStartPhase; c.shown = waveTools; return;
            case kWaveRandom: c.rect = L.tRandomPhase; c.shown = waveTools; return;
            case kWaveMap: c.rect = L.tWaveMap; c.shown = waveTools; return;
            case kTuneOct: c.rect = L.tOct; c.shown = ! tune; return;
            case kBasic + 3: c.rect = L.tOct; return;
            case kBasic + 5: c.rect = L.tSound; c.shown = ! full_.noiseSample(); return;
            case kBasic + 4: c.rect = full_.slotRect (PanelSlot::Filter); return;
            case kTuneFin: c.rect = full_.slotRect (noise ? PanelSlot::NoiseFine : PanelSlot::Fin); c.shown = noise || ! tune; return;
            case kTuneCrs: c.rect = full_.slotRect (PanelSlot::NoisePitch); return;
            case kUnisonGear: c.rect = L.cells.unisonGear; return;
            case kUnisonCount: c.rect = L.cells.unisonCount; return;
            case kWarp1Type: c.rect = L.cells.warpType[0]; return;
            case kWarp1Amount: c.rect = L.cells.warpAmount[0]; c.shown = full_.warpApplies (0); return;
            case kWarp2Type: c.rect = L.cells.warpType[1]; c.shown = ! L.warpSingle; return;
            case kWarp2Amount: c.rect = L.cells.warpAmount[1]; c.shown = ! L.warpSingle && full_.warpApplies (1); return;
            default: break;
        }
        const int slot = slotOfId (it.id);
        if (slot < 0)
            return;
        const PanelSlot ps = static_cast<PanelSlot> (slot);
        if (ps == PanelSlot::NoiseType) { c.rect = L.tKind; return; }
        if (ps == PanelSlot::SubBlend && sub) { c.rect = L.subBlend; return; }
        if (abc && ps == positionSlot (kindOfRaw (typeRaw_))) { c.rect = L.tPos; return; }
        c.rect = full_.slotRect (ps);
        c.shown = full_.partShown (ps);
    }
    void placeDetailItem (Item& it)
    {
        oscmodel::Cell& c = it.cell;
        c.rect = {};
        c.shown = true;
        if (! it.hasCell || detailPreset_ == nullptr)
            return;
        const oscmodel::PanelCells& cells = detailLayout_.cells;
        if (it.id == kUnisonGear) { c.rect = cells.unisonGear; return; }
        if (it.id == kUnisonCount) { c.rect = cells.unisonCount; return; }
        const int slot = it.id >= kDetail && it.id < kDetail + static_cast<int> (oscmodel::PanelSlot::Count) ? it.id - kDetail : -1;
        for (int i = 0; slot >= 0 && i < detailPreset_->numItems; ++i)
            if (static_cast<int> (detailPreset_->items[i].slot) == slot)
                c.rect = cells.item[i];
    }

    // ---- the Q groups of the pages (tables of item ids; the member ids of the full screen are the ids of the rows with `full`)
    struct QGroupIds { int id[4] = { -1, -1, -1, -1 }; char name[20] = {}; };
    /** The item with `id` of a list (a full screen row when `full`, else the first of that id). */
    static const Item* lookup (const std::vector<Item>& list, int id, bool full) noexcept
    {
        for (const Item& it : list)
            if (it.id == id && (it.full == full || ! full))
                return &it;
        return nullptr;
    }
    ui::Rect groupRect (const QGroupIds& g) const noexcept
    {
        ui::Rect u;
        for (int id : g.id)
            if (const Item* it = lookup (main_, id, true))
                u = ui::Rect::unite (u, it->cell.rect);
        return u;
    }
    /** The id of the row a preset slot of the full screen is. */
    int idOfFullSlot (oscmodel::PanelSlot s) const noexcept
    {
        using oscmodel::PanelSlot;
        switch (s)
        {
            case PanelSlot::Unison: return kUnisonCount;
            case PanelSlot::Fin: return kTuneFin;
            case PanelSlot::NoiseFine: return kTuneFin;
            case PanelSlot::NoisePitch: return kTuneCrs;
            case PanelSlot::Filter: return kBasic + 4;
            default: return kPanel + static_cast<int> (s);
        }
    }
    void addFullGroup (const char* name, const int* ids)
    {
        int live[4] = { -1, -1, -1, -1 }, n = 0;
        for (int i = 0; i < 4; ++i)
            if (ids[i] >= 0)
                live[n++] = ids[i];
        if (n == 0 || nFullGroups_ >= oscmodel::kMaxGroups)
            return;
        QGroupIds& g = fullGroups_[nFullGroups_++];
        ui::setText (g.name, sizeof g.name, name);
        for (int i = 0; i < 4; ++i)
            g.id[i] = live[i];
    }
    /** The groups of the full screen: Basic (the top row), then the blocks of the bottom band (the WARP group right after the first block of A, B, C), closed up from slot 1; a group with no live member is not made. */
    void buildFullGroups()
    {
        using oscmodel::PanelSlot;
        nFullGroups_ = 0;
        const oscmodel::FullLayout& L = full_.layout();
        if (L.preset == nullptr)
            return;
        const bool sub = sel_ == playosc::kSub, noise = sel_ == playosc::kNoise, abc = ! sub && ! noise;
        {
            int ids[4] = { -1, -1, -1, -1 };
            int members[4];
            oscmodel::FullState st;
            st.inst = instance();
            st.noiseSample = full_.noiseSample();
            const int n = oscmodel::topRowMembers (st, members);
            for (int i = 0; i < n; ++i)
                switch (members[i])
                {
                    case oscmodel::kTopSelector: ids[i] = kBasic; break;
                    case oscmodel::kTopSource: ids[i] = kBasic + 1; break;
                    case oscmodel::kTopPos: ids[i] = kPanel + static_cast<int> (positionSlot (kindOfRaw (typeRaw_))); break;
                    case oscmodel::kTopOct: ids[i] = abc ? (full_.hasNativeTune() ? -1 : kTuneOct) : kBasic + 3; break;   // the card of the tune is the host's: OCT leaves the group
                    case oscmodel::kTopKind: ids[i] = kPanel + static_cast<int> (PanelSlot::NoiseType); break;
                    default: ids[i] = kBasic + 5; break;
                }
            addFullGroup ("Basic", ids);
        }
        const oscmodel::PanelPreset& pp = *L.preset;
        static const char* const kUnused = "";
        (void) kUnused;
        for (int i = 0; i < pp.numQ; ++i)
        {
            int ids[4];
            for (int k = 0; k < 4; ++k)
            {
                const PanelSlot s = pp.q[i].slot[k];
                ids[k] = s == PanelSlot::Count || ! full_.partShown (s) || full_.indexOf (s) < 0 ? -1 : idOfFullSlot (s);
            }
            if (sub && i == 1)
                ids[3] = kPanel + static_cast<int> (PanelSlot::SubBlend);
            char name[20];
            const char* t = i < pp.numBlocks ? pp.blocks[i].title : "Group";
            std::snprintf (name, sizeof name, "%c%s", t[0], t + 1);
            for (char* c = name + 1; *c; ++c)
                *c = static_cast<char> (std::tolower (static_cast<unsigned char> (*c)));
            addFullGroup (name, ids);
            if (i == 0 && full_.indexOf (PanelSlot::Warp) >= 0 && abc)
            {
                // the WARP group right after the unison group: [WARP 1 TYPE, WARP 1 AMOUNT, WARP 2 TYPE, WARP 2 AMOUNT]; the warp 2 members only when the screen shows the second slot, an amount only while its type is not Off
                int wm[4] = { kWarp1Type, full_.warpApplies (0) ? kWarp1Amount : -1, -1, -1 };
                if (! L.warpSingle)
                {
                    wm[2] = kWarp2Type;
                    wm[3] = full_.warpApplies (1) ? kWarp2Amount : -1;
                }
                addFullGroup ("Warp", wm);
            }
        }
        if (fullActive_ >= nFullGroups_)   // (-1 stays: the Mix or the popup's group is the active one)
            fullActive_ = nFullGroups_ > 0 ? 0 : -1;
    }
    /** The groups of a detail page: the Q specs of its preset, the members are the rows of the page. */
    void buildDetailGroups()
    {
        nDetailGroups_ = 0;
        if (detailPreset_ == nullptr)
            return;
        nDetailGroups_ = std::min (detailPreset_->numQ, kMaxDetailGroups);
        for (int gi = 0; gi < nDetailGroups_; ++gi)
            for (int i = 0; i < 4; ++i)
            {
                const oscmodel::PanelSlot s = detailPreset_->q[gi].slot[i];
                const int id = s == oscmodel::PanelSlot::Count ? -1 : kDetail + static_cast<int> (s);
                detailGroups_[gi].id[i] = id >= 0 && lookup (page_, id, false) != nullptr ? id : -1;
            }
        detailActive_ = 0;
    }

    // ======================================================================================================================================================================
    // the pages above the full screen
    void syncPages (const UiStack& stack, IInstanceView& inst)
    {
        const int top = stack.depth() >= 2 ? stack.top() : 0;
        const int sig = top * 16 + (top == kPageMods ? modsTarget_ & 0xff : 0);
        if (top != pageTop_ || sig != pageSig_)
        {
            if (top == kPageRoute && pageTop_ != kPageRoute)
            {
                savedGroup_ = fullActive_;
                fullActive_ = -1;      // the popup's group is the active one: the full screen's frame goes away while it is open
            }
            else if (top != kPageRoute && pageTop_ == kPageRoute && savedGroup_ >= 0)
            {
                fullActive_ = savedGroup_;
                savedGroup_ = -1;
            }
            pageTop_ = top;
            pageSig_ = sig;
            if (top != kPageList)
                browseMode_ = wtMode_ = false;
            modsListFilled_ = false;
            if (top != kPageMods)
                closeModsPicking();
            loadPage (inst);
        }
    }
    void loadPage (IInstanceView& inst)
    {
        page_.clear();
        pageLoaded_ = pageTop_;
        detailPreset_ = nullptr;
        nDetailGroups_ = 0;
        const oscmodel::Kind kind = kindOfRaw (typeRaw_);
        const bool wt = kind == oscmodel::Kind::Wavetable;
        switch (pageTop_)
        {
            case kPageUnison:
                loadDetail (wt ? oscmodel::presets::kUnisonDetail : (kind == oscmodel::Kind::Multisample ? oscmodel::presets::kUnisonDetailMultisample : oscmodel::presets::kUnisonDetailGranular));
                break;
            case kPageGrain: loadDetail (oscmodel::presets::kGrainDetail); break;
            case kPageEnvelope: loadDetail (oscmodel::presets::kEnvelopeDetail); break;
            case kPageRoute:
                bind (page_, kRouteTarget, T::Choice, "destination", "", false);
                bind (page_, kRouteBalance, T::Dial, "filter_blend", "", false);
                bind (page_, kRouteBus1, T::Dial, "bus1_send", "", false);
                bind (page_, kRouteBus2, T::Dial, "bus2_send", "", false);
                break;
            case kPageMods: loadMods (inst); break;
            case oscspec::kWtMap: case oscspec::kMore: case oscspec::kOptions: case oscspec::kSampleRegion: case oscspec::kSampleSlice: case oscspec::kGrain: case oscspec::kSpecFilter: case oscspec::kSpecScan:
                bindSpec (pageTop_);
                break;
            case kPageList:
                if (wtMode_)
                    fillWavetableList();
                else if (browseMode_)
                    fillBrowserList();
                break;
            default: break;
        }
        configureNumberRanges (page_);
        modsDirty_ = true;
        qSlots_.forget();
    }
    /** A detail page of osc_spec_page.h: its controls are bound to the parameters of this oscillator (ids kSpec + index). */
    void bindSpec (int page)
    {
        const oscspec::Spec* sp = oscspec::specOf (page);
        if (sp == nullptr)
            return;
        spec_.loadSpec (*sp);
        for (int i = 0; i < sp->numCtls; ++i)
        {
            const oscspec::Ctl& c = sp->ctls[i];
            T t = T::Dial;
            std::string key = c.key;
            oscmodel::NumberFormat format = oscmodel::NumberFormat::IntPlain;
            if (c.kind == oscspec::K::Choice) t = T::Choice;
            else if (c.kind == oscspec::K::Toggle) t = std::strcmp (c.key, "transpose_snap") == 0 ? T::SnapBit : T::Toggle;
            else if (c.kind == oscspec::K::Number)
            {
                t = T::Number;
                if (std::strcmp (c.key, "transpose_sem") == 0) { t = T::NumberSem; key = "transpose"; }   // the semitones of the transpose (OCT of the default view is its octaves)
                else if (std::strcmp (c.key, "phase") == 0) format = oscmodel::NumberFormat::Degrees;
            }
            else if (c.kind == oscspec::K::List) t = T::List;
            else if (c.kind == oscspec::K::Mask) t = T::Mask;
            else if (c.kind == oscspec::K::Page) { t = T::Inert; key.clear(); }
            bind (page_, kSpec + i, t, key, c.label);
            Item& it = page_.back();
            it.cell.format = format;
            if (c.kind == oscspec::K::List)
            {
                const int pi = indexOfKey (prefix_ + c.key);
                if (pi >= 0 && info (pi).choices != nullptr)
                    it.cell.count = std::min (static_cast<int> (info (pi).numChoices), 8);
            }
        }
        spec_.relayoutNow();
        specVisible_ = true;
    }
    bool condHolds (oscspec::Cond c, const IInstanceView& v) const
    {
        auto val = [&] (const char* suffix) { const int i = indexOfKey (prefix_ + suffix); return i >= 0 ? static_cast<int> (std::lround (v.param (i))) : 0; };
        using C = oscspec::Cond;
        switch (c)
        {
            case C::Always: return true;
            case C::LoopOn: return val ("sample.loop_mode") != 0;
            case C::SliceAuto: return val ("sample.slice_mode") == 1;
            case C::SliceManual: return val ("sample.slice_mode") == 2;
            case C::SliceAny: return val ("sample.slice_mode") != 0;
            case C::KeytrackOn: return val ("sample.keytrack") != 0;
            case C::DensSync: return val ("gran.density_mode") == 1;
            case C::DensGrains: return val ("gran.density_mode") == 2;
            case C::LenSync: return val ("gran.length_mode") == 1;
            case C::LenPercent: return val ("gran.length_mode") == 2;
            case C::RemapOn: return val ("remap.on") != 0;
            case C::FltOn: return val ("spec.flt_use") != 0;
            case C::SpecYAxis: return val ("spec.y_axis") != 0;
            case C::GranYAxis: return val ("gran.y_axis") != 0;
            case C::TrackOff: return val ("pitch_track") == 0;
            case C::TempoLen: return val ("sample.scan_lock_tempo") != 0 || val ("sample.scan_to_bpm") != 0;
            case C::KWave: return typeRaw_ == 0;
            case C::KMulti: return typeRaw_ == 4;
            case C::KSample: return typeRaw_ == 1;
            case C::KSpec: return typeRaw_ == 3;
            case C::KGran: return typeRaw_ == 2;
            case C::NotGran: return typeRaw_ != 2;
            case C::MultiSampleSpec: return typeRaw_ == 4 || typeRaw_ == 1 || typeRaw_ == 3;
            case C::PhaseKinds: return typeRaw_ == 0 || typeRaw_ == 1 || typeRaw_ == 3;
            case C::RandKinds: return typeRaw_ == 0 || typeRaw_ == 4;
            case C::Warp1On: return typeRaw_ != 2 && val ("warp.type") != 0;
            case C::Warp2On: return typeRaw_ != 2 && val ("warp2.type") != 0;
            case C::Warp2AmtOn: return (typeRaw_ == 4 || typeRaw_ == 1 || typeRaw_ == 3) && val ("warp2.type") != 0;
            default: return true;
        }
    }
    /** Which controls of the detail page apply in the state of the parameters (a loop mode of one shot has no loop points ...). */
    void updateSpecVisibility (const IInstanceView& v)
    {
        const oscspec::Spec* sp = spec_.spec();
        if (sp == nullptr || ! oscspec::isSpecPage (pageTop_))
            return;
        bool vis[oscspec::OscSpecPage::kMaxCtl] = {};
        for (int i = 0; i < sp->numCtls; ++i)
            vis[i] = condHolds (sp->ctls[i].cond, v);
        spec_.setVisible (vis);
    }
    /** The detail page of a preset (the unison settings, the grain window, the multisample envelope): the rows of its parts (ids kDetail + slot), the Q groups of the preset. */
    void loadDetail (const oscmodel::PanelPreset& preset)
    {
        detailPreset_ = &preset;
        detailLayout_ = oscmodel::layoutDetail (preset, tabRect_);
        bindPanel (page_, &preset, kDetail, kindOfRaw (typeRaw_), true);
        for (Item& it : page_)
            placeDetailItem (it);
        buildDetailGroups();
    }
    // ---- the modulations page
    static constexpr int kModsPerPage = ModRowsPage::kPerPage;
    uibind::ModRow modRows_[kModsPerPage];
    NativeModTargets nativeModulations_;
    int modsPageIndex_ = 0, modsSource_ = 0, modsType_ = -1;
    uint64_t modsPreset_ = 0;
    int modsTarget_ = 0;     // the id of the control whose modulations the page shows
    bool modsModulePowered (const IInstanceView& inst) const noexcept
    {
        const auto* power = item (kPower);
        return power != nullptr && power->param >= 0 && inst.param (power->param) >= 0.5f;
    }
    bool modsContextValid (const IInstanceView& inst) const noexcept
    {
        return pageTop_ == kPageMods && modsTargetParam_ >= 0 && modsPreset_ == inst.stateGeneration()
            && modsSource_ == sel_ && (isPage() || inst.playSelection (0) == sel_) && modsModulePowered (inst);
    }
    /** While no modulation exists on the control, or while a source is being picked, the page is the list of the sources (case (c): the native picker cells). */
    bool modsBoard() const noexcept { return modsPicking_ || mods_.numRows() == 0; }
    bool modsNativeHit (int x, int y, int* row = nullptr) const noexcept { return nativeModulations_.active() && ! modsBoard() && mods_.nativeHit (x, y, row); }

    void loadMods (const IInstanceView& inst, int focusSlot = -1)
    {
        const auto window = uibind::modulationWindow (inst, modsTargetParam_, selectedSource_, modsPageIndex_, modRows_, focusSlot);
        modsPageIndex_ = window.page;
        ModRowsPage::Entry e[kModsPerPage];
        const int n = std::clamp (window.entries.numMods, 0, kModsPerPage);
        for (int i = 0; i < n; ++i)
        {
            ui::setText (e[i].name, sizeof e[i].name, window.entries.mods[i].name);
            e[i].source = window.entries.mods[i].source;
            e[i].macro = window.entries.mods[i].macro;
            e[i].depth = window.entries.mods[i].depth;
            e[i].bidirectional = window.entries.mods[i].bidirectional;
            e[i].bypassed = window.entries.mods[i].bypassed;
        }
        const bool switchedPage = mods_.pageIndex() != std::clamp (window.page, 0, std::max (0, (window.total + kModsPerPage - 1) / kModsPerPage - 1));
        mods_.load (e, n, modsTitle_, 0xffffffu, window.total, window.page);
        if (switchedPage)
            mods_.selectRow (n == 0 ? -1 : 0);
        if (window.focusRow >= 0)
            mods_.selectRow (window.focusRow);
        if (pageTop_ == kPageMods && modsBoard() && ! modsListFilled_)
            fillModsSources();
    }
    /** The list of the sources a new modulation can come from: the groups, then the sources of one group (the header is the way up). */
    void fillModsSources()
    {
        modsListNames_.clear();
        modsListChoices_.clear();
        if (modsListGroup_ < 0)
        {
            for (int g = 1; g <= uibind::kSourceGroups; ++g)
            {
                modsListNames_.push_back (uibind::sourceGroupName (g));
                modsListChoices_.push_back (-g);
            }
            listData_.header = nullptr;
            listData_.headerBack = false;
        }
        else
        {
            for (int c = 1; c <= static_cast<int> (synthyboy::modSourceCount()); ++c)
                if (uibind::sourceGroup (c) == modsListGroup_)
                {
                    modsListNames_.push_back (uibind::sourceName (c));
                    modsListChoices_.push_back (c);
                }
            listData_.header = uibind::sourceGroupName (modsListGroup_);
            listData_.headerBack = true;
        }
        listData_.items = modsListNames_.data();
        listData_.count = static_cast<int> (modsListNames_.size());
        listData_.current = -1;
        showCurrentRow();
        modsListFilled_ = true;
    }
    int modsTargetParam_ = -1, savedGroup_ = -1;
    char modsTitle_[32] = "MODULATIONS";
    static const char* sourceName (int choice) noexcept { return uibind::sourceName (choice); }   // a macro answers with its name (macro_names.h)
    void refreshMods (const IInstanceView& v)
    {
        if (pageTop_ == kPageMods && modsTargetParam_ >= 0)
            loadMods (v);
    }

    // ======================================================================================================================================================================
    // reading: the states of the components from the parameters
    void pull (IInstanceView& v, std::vector<Item>& list)
    {
        for (Item& it : list)
        {
            if (! it.hasCell)
                continue;
            oscmodel::Cell& c = it.cell;
            if (it.alwaysOff)
            {
                c.enabled = false;
                if (it.type == T::Choice && it.param >= 0)
                    showStepper (c, it.param, v.param (it.param));
                else if (it.type == T::Choice)
                {
                    c.count = 0;
                    oscmodel::setText (c, "-");
                }
                continue;
            }
            if (it.type == T::Inert)
            {
                c.enabled = true;
                if (it.id == kPanel + static_cast<int> (oscmodel::PanelSlot::WarpMenu) && it.param >= 0)   // the WARP menu of the granular oscillator says which warp it has now
                {
                    const synthyboy::ParamInfo& wp = info (it.param);
                    const int wi = std::clamp (static_cast<int> (std::lround (v.param (it.param) - wp.min)), 0, std::max (0, wp.numChoices - 1));
                    oscmodel::setText (c, wp.choices != nullptr ? wp.choices[wi] : "");
                }
                continue;
            }
            if (it.type == T::Table && it.param >= 0)
            {
                showTable (c, it, v.param (it.param), v);
                c.enabled = true;
                continue;
            }
            if (it.type == T::Source)
            {
                c.count = 0;
                c.index = 0;
                c.wrap = false;
                sourceText (v, c);
                c.enabled = true;
                continue;
            }
            if (it.param < 0)
            {
                c.enabled = false;
                continue;
            }
            const float plain = v.param (it.param);
            const synthyboy::ParamInfo& pi = info (it.param);
            switch (it.type)
            {
                case T::Dial:
                {
                    c.value = normOf (it.param, plain);
                    const bool isPan = ! it.full && it.id == kBasic + 6, isLevel = ! it.full && it.id == kBasic + 7;
                    const bool isFilter = it.id == kBasic + 4;
                    if (isPan)
                    {
                        const int whole = static_cast<int> (std::lround (plain));
                        std::snprintf (c.text, sizeof c.text, whole == 0 ? "C" : (whole < 0 ? "L %d" : "R %d"), std::abs (whole));
                    }
                    else if (isLevel)
                        std::snprintf (c.text, sizeof c.text, "%.1f", static_cast<double> (plain));
                    else if (isFilter)
                        std::snprintf (c.text, sizeof c.text, "%.0f", static_cast<double> (plain));
                    else if (it.id == kPanel + static_cast<int> (oscmodel::PanelSlot::SubPhase))
                    {
                        const double deg = std::round (static_cast<double> (plain) * 3.6);
                        std::snprintf (c.text, sizeof c.text, "%.0f\xc2\xb0", deg);
                    }
                    else
                        formatValue (pi, plain, c.text, sizeof c.text);   // every other knob says its value too (with the unit)
                    break;
                }
                case T::Number: c.value = plain; break;
                case T::NumberOct: c.value = octOf (static_cast<int> (std::lround (plain))); break;
                case T::NumberSem: c.value = semOf (static_cast<int> (std::lround (plain))); break;
                case T::NumberDeg: c.value = std::round (static_cast<double> (plain) * 3.6); break;
                case T::Choice:
                {
                    showStepper (c, it.param, plain);
                    if (it.id == kWarp1Type || it.id == kWarp2Type)
                        warpOff (it.id, c.index == 0);
                    break;
                }
                case T::Toggle: c.on = plain >= 0.5f; break;
                case T::SnapBit: c.on = (static_cast<int> (std::lround (plain)) & 4096) != 0; break;
                case T::Mask: break;   // the host reads the mask from the parameter (nativeui::kNoteMask)
                case T::List: c.index = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, c.count - 1)); break;
                case T::UpDown: c.value = static_cast<int> (std::lround (plain)); break;
                case T::Picto:
                {
                    const int raw = static_cast<int> (std::lround (plain));
                    c.index = raw == 0 ? 0 : raw == 4 ? 1 : raw == 1 ? 2 : raw == 2 ? 3 : raw == 3 ? 4 : -1;
                    break;
                }
                case T::PictoPlain: c.index = static_cast<int> (std::lround (plain)); break;
                case T::Route:
                {
                    static const char* const kTarget[7] = { "F1", "F2", "F12", "M", "DIR", "BAL", "OFF" };
                    c.index = std::clamp (static_cast<int> (std::lround (plain)), 0, 6);
                    oscmodel::setText (c, kTarget[c.index]);
                    break;
                }
                default: break;
            }
            // a cell never gets a state when its parameter is read only by nothing: enabled stays as the model says (a warp slot with the type Off takes no amount)
            c.enabled = true;
            if (it.type == T::Dial && (it.id == kWarp1Amount || it.id == kWarp2Amount))
                c.enabled = ! warpIsOff (it.id);
            if (it.id == kRoute && sel_ == playosc::kSub)
                c.enabled = false;   // the sub oscillator has no route button (its routing is the ROUTING block of its page): the hidden cell takes no touch and no Q-Link
        }
    }
    /** What the page needs of the routing of the module: BAL (the balance of the F1 / F2 routing) shows a second routing cell. (The level, pan and destination of the module are shown by the host's native cells.) */
    void updateMix (const IInstanceView& v)
    {
        const int di = indexOfKey (prefix_ + "destination");
        const int d = di >= 0 ? std::clamp (static_cast<int> (std::lround (v.param (di))), 0, 6) : 0;
        routeFilter_ = d == 5;
    }
    /** A value of a parameter as text with its unit: "35%", "+12 ct", "420 Hz", "1.20 s", "-6.0 dB" (at most 15 characters). */
    static void formatValue (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
    {
        using synthyboy::Unit;
        const double v = static_cast<double> (plain);
        const int dec = std::min<int> (pi.decimals, 2);
        switch (pi.unit)
        {
            case Unit::percent: if (std::fabs (v) >= 99.995) std::snprintf (out, cap, pi.min < 0.0f ? "%+.0f%%" : "%.0f%%", v); else std::snprintf (out, cap, pi.min < 0.0f ? "%+.*f%%" : "%.*f%%", dec, v); break;
            case Unit::cents: std::snprintf (out, cap, "%+.0f ct", v); break;
            case Unit::semitone: std::snprintf (out, cap, "%+.*f st", dec, v); break;
            case Unit::Hz: if (v >= 1000.0) std::snprintf (out, cap, "%.2f kHz", v / 1000.0); else std::snprintf (out, cap, "%.0f Hz", v); break;
            case Unit::ms: if (v >= 1000.0) std::snprintf (out, cap, "%.2f s", v / 1000.0); else std::snprintf (out, cap, v < 10.0 ? "%.1f ms" : "%.0f ms", v); break;
            case Unit::dB: std::snprintf (out, cap, "%+.1f dB", v); break;
            case Unit::degrees: std::snprintf (out, cap, "%.0f\xc2\xb0", v); break;
            case Unit::beats: std::snprintf (out, cap, "%.2f", v); break;
            default: std::snprintf (out, cap, "%.*f", dec, v); break;
        }
    }
    /** The curve of the envelope page: delay, attack, hold, decay, the sustain level held for a while, release; the widths follow the six knobs (schematic: the knobs are normalised values). */
    void updateEnvelope()
    {
        auto knobOf = [&] (oscmodel::PanelSlot slot) { const Item* k = lookup (page_, kDetail + static_cast<int> (slot), false); return k != nullptr ? static_cast<float> (std::clamp (k->cell.value, 0.0, 1.0)) : 0.0f; };
        const float d = knobOf (oscmodel::PanelSlot::Delay), a = knobOf (oscmodel::PanelSlot::Attack), h = knobOf (oscmodel::PanelSlot::Hold), dc = knobOf (oscmodel::PanelSlot::Decay), su = knobOf (oscmodel::PanelSlot::Sustain), r = knobOf (oscmodel::PanelSlot::Release);
        const float w[6] = { 0.03f + 0.14f * d, 0.04f + 0.20f * a, 0.02f + 0.12f * h, 0.04f + 0.24f * dc, 0.20f, 0.05f + 0.24f * r };
        float total = 0.0f;
        for (float x : w) total += x;
        float edge[7] = { 0.0f };
        for (int i = 0; i < 6; ++i)
            edge[i + 1] = edge[i] + w[i] / total;
        for (int i = 0; i < kEnvPoints; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (kEnvPoints - 1);
            int seg = 0;
            while (seg < 5 && t > edge[seg + 1]) ++seg;
            const float f = std::clamp ((t - edge[seg]) / std::max (1.0e-6f, edge[seg + 1] - edge[seg]), 0.0f, 1.0f);
            float y = 0.0f;
            switch (seg)
            {
                case 0: y = 0.0f; break;
                case 1: y = f; break;
                case 2: y = 1.0f; break;
                case 3: y = su + (1.0f - su) * std::exp (-3.0f * f); break;
                case 4: y = su; break;
                default: y = su * std::exp (-3.5f * f) * (1.0f - f * f * f); break;
            }
            envCurve_[i] = y;
        }
    }
    static constexpr int kEnvPoints = 128;
    float envCurve_[kEnvPoints] = {};
    static std::string stemOf (const std::string& path)
    {
        const size_t slash = path.rfind ('/');
        std::string s = slash == std::string::npos ? path : path.substr (slash + 1);
        const size_t dot = s.rfind ('.');
        return dot == std::string::npos || dot == 0 ? s : s.substr (0, dot);
    }
    /** The text of the source stepper of a granular / multisample oscillator: the name of the loaded file, "LOADING ..." / "LOAD FAILED" for the last load of this oscillator, else "NO SAMPLE". */
    void sourceText (const IInstanceView& v, oscmodel::Cell& st) const
    {
        const int osc = playosc::oscNumber (sel_);
        std::string msg;
        const int state = v.contentLoad (&msg);
        if (state == 1) { oscmodel::setText (st, "LOADING ..."); return; }
        if (state == 3) { oscmodel::setText (st, "LOAD FAILED"); return; }
        const std::shared_ptr<const synthyboy::StateContent> c = v.content();
        std::string name;
        if (c != nullptr)
        {
            if (typeRaw_ == 4)
            {
                for (const synthyboy::ContentInstrument& i : c->instruments)
                    if (i.osc == osc) name = ui::contentname::display (i.ref.name, i.ref.file);
            }
            else
                for (const synthyboy::ContentSample& s : c->samples)
                    if (s.osc == osc) name = ui::contentname::display (s.ref.name, s.ref.file);
        }
        oscmodel::setText (st, name.empty() ? "NO SAMPLE" : name.c_str());
    }
    bool warpOff_[2] = { false, false };
    bool routeFilter_ = false;
    bool warpIsOff (int id) const noexcept { return warpOff_[id == kWarp1Amount ? 0 : 1]; }
    void warpOff (int typeId, bool off)
    {
        const int slot = typeId == kWarp1Type ? 0 : 1;
        warpOff_[slot] = off;
        if (full_.warpApplies (slot) == off)   // hide what does not apply: the amount of a warp that is Off is not on the screen and is no Q member
        {
            full_.setWarpApplies (slot, ! off);
            relayout();
        }
    }
    static void showStepper (oscmodel::Cell& st, int param, float plain)
    {
        const synthyboy::ParamInfo& pi = info (param);
        const int n = pi.numChoices > 0 ? pi.numChoices : static_cast<int> (pi.max - pi.min) + 1;
        const int idx = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, n - 1));
        st.count = n;
        st.index = idx;
        st.wrap = pi.numChoices <= 12;
        if (pi.choices != nullptr && idx < pi.numChoices)
            oscmodel::setText (st, ui::shownChoice (pi.key, idx, pi.choices[idx]));
        else
        {
            char t[13];
            std::snprintf (t, sizeof t, "%d", idx);
            oscmodel::setText (st, t);
        }
    }

    // ======================================================================================================================================================================
    // writing: a Change of a component to a parameter
    Item* find (int id) noexcept
    {
        for (Item& it : page_) if (it.id == id) return &it;
        for (Item& it : main_) if (it.id == id) return &it;
        return nullptr;
    }
    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        // ---- navigation
        if (c.kind == K::Request)
        {
            switch (id)
            {
                case kFullButton: stack.openFull (kElementOsc); return;
                case kModsBack:
                    if (modsPicking_)
                        closeModsPicking();
                    else
                        stack.back();
                    return;
                case kModsAdd:
                    if (modsContextValid (v))
                    {
                        modsPicking_ = true;
                        modsListGroup_ = -1;
                        fillModsSources();
                    }
                    return;
                case kClose: case kDetailBack: stack.back(); return;
                case kWaveMap: stack.openDetail (oscspec::kWtMap); return;
                case kTuneMore:
                    if (sel_ != playosc::kSub && sel_ != playosc::kNoise)
                        stack.openDetail (oscspec::kMore);
                    return;
                case kRoute:
                    if (stack.depth() == 0)
                        stack.openFull (kElementOsc);   // the route popup lies over the full screen: from the compact view that opens first
                    stack.openDetail (kPageRoute);
                    return;
                case kListBackdrop:
                    if (pageTop_ == kPageMods && modsPicking_ && mods_.numRows() > 0)
                        closeModsPicking();
                    else
                        stack.back();
                    return;
                case kListHeader:
                    if (pageTop_ == kPageMods)
                    {
                        if (modsListGroup_ >= 0)
                        {
                            modsListGroup_ = -1;
                            fillModsSources();
                        }
                    }
                    else if (wtMode_ && wtBrowser_.up() == WavetableBrowser::Action::Changed)
                        fillWavetableList();
                    else if (browseMode_ && browser_.tap (0) == ContentBrowser::Action::Changed)
                        fillBrowserList();
                    return;
                case kBasic + 1: case kBasic + 2:
                {
                    const Item* src = item (kBasic + 1);
                    if (src != nullptr && src->type == T::Source) openBrowser (stack, v);
                    else if (src != nullptr && src->type == T::Table) openWavetableBrowser (stack, v);
                    return;
                }
                default: break;
            }
            if (id == kUnisonGear) { stack.openDetail (kPageUnison); return; }
            const Item* it = item (id);
            if (it != nullptr && id >= kSpec && id < kSpec + oscspec::OscSpecPage::kMaxCtl)
            {
                // a button of a detail page that opens another page (the page buttons of MORE)
                const oscspec::Spec* sp = spec_.spec();
                const int ci = id - kSpec;
                if (sp != nullptr && ci < sp->numCtls && sp->ctls[ci].kind == oscspec::K::Page)
                {
                    const int pg = pageOfToken (sp->ctls[ci].key);
                    if (pg > 0)
                        stack.openDetail (pg);
                }
                return;
            }
            if (id >= kModsRow && id < kModsRow + 4 * kModsPerPage && (id - kModsRow) % 4 == 3)
            {
                const auto row = modRows_[(id - kModsRow) / 4];
                if (modsContextValid (v) && uibind::modulationMatches (v, modsTargetParam_, row))
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (row.slot);
                    writePlain (v, sp.source, 0); writePlain (v, sp.destination, 0); writePlain (v, sp.amount, 0);
                    loadMods (v);
                }
                return;
            }
            return;
        }
        if (c.kind == K::ModsRequest)
        {
            const auto* target = item (id);
            const int targetParam = target != nullptr ? target->param : (id == kTuneCrs ? tuneTargets_[3] : -1);
            if (targetParam >= 0 && synthyboy::modDestinationChoiceOf (static_cast<size_t> (targetParam)) != 0
                && stack.openDetail (kPageMods))
            {
                modsTarget_ = id;
                modsTargetParam_ = targetParam;
                modsPageIndex_ = 0;
                modsSource_ = sel_;
                modsType_ = typeRaw_;
                modsPreset_ = v.stateGeneration();
                modsPicking_ = false;
                modsListGroup_ = -1;
                modsListFilled_ = false;
                const char* name = target != nullptr && target->hasCell && target->type == T::Dial && target->cell.label[0] != '\0'
                    ? target->cell.label
                    : info (targetParam).name;
                ui::setText (modsTitle_, sizeof modsTitle_, name);
            }
            return;
        }
        if (id == kList && c.kind == K::Index)
        {
            if (pageTop_ == kPageMods)
            {
                chooseModsSource (c.index, v);
                return;
            }
            if (wtMode_)
            {
                chooseWavetableRow (c.index, stack, v);
                return;
            }
            if (browseMode_)
            {
                const ContentBrowser::Action a = browser_.tap (c.index + 1);   // the list shows the rows after the header
                if (a == ContentBrowser::Action::Changed)
                    fillBrowserList();
                else if (a == ContentBrowser::Action::Chosen)
                {
                    lastChosen_[browser_.kind() - 1] = browser_.chosen();
                    v.requestContent (playosc::oscNumber (sel_), browser_.kind(), browser_.chosen());
                    stack.back();
                }
                return;
            }
            stack.back();
            return;
        }
        // ---- the modulation rows (the native cells of the modulations page)
        if (id >= kModsRow && id < kModsRow + 4 * kModsPerPage)
        {
            const auto row = modRows_[(id - kModsRow) / 4];
            if (! modsContextValid (v) || ! uibind::modulationMatches (v, modsTargetParam_, row))
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (row.slot);
            switch ((id - kModsRow) % 4)
            {
                case 0: if (c.kind == K::Value) writeNorm (v, sp.amount, c.value); break;
                case 1: if (c.kind == K::Index) writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0); break;
                case 2: if (c.kind == K::Index) writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0); break;   // the switch says ON (active): bypassed is its opposite
                default: break;
            }
            modsDirty_ = true;
            return;
        }
        if (id == kModsPage && c.kind == K::Index && modsContextValid (v))
        {
            modsPageIndex_ = c.index;
            loadMods (v);
            return;
        }
    }
    void closeModsPicking()
    {
        modsPicking_ = false;
        modsListGroup_ = -1;
        modsListFilled_ = false;
    }
    /** A row of the list of the sources of a new modulation: a group opens its sources, a source makes the modulation. */
    void chooseModsSource (int row, IInstanceView& v)
    {
        const int sel = row >= 0 && row < static_cast<int> (modsListChoices_.size()) ? modsListChoices_[static_cast<size_t> (row)] : 0;
        if (sel < 0)
        {
            modsListGroup_ = -sel;
            fillModsSources();
        }
        else if (sel > 0)
            addModulation (v, sel);
    }

    /** The name of the file the type plays, "" when none is loaded. */
    std::string loadedName (const IInstanceView& v) const
    {
        const int osc = playosc::oscNumber (sel_);
        const std::shared_ptr<const synthyboy::StateContent> c = v.content();
        std::string name;
        if (c != nullptr)
        {
            if (typeRaw_ == 4)
            {
                for (const synthyboy::ContentInstrument& i : c->instruments)
                    if (i.osc == osc) name = ui::contentname::display (i.ref.name, i.ref.file);
            }
            else
                for (const synthyboy::ContentSample& s : c->samples)
                    if (s.osc == osc) name = ui::contentname::display (s.ref.name, s.ref.file);
        }
        return name;
    }
    // ---- the content the sound needs and does not have (IInstanceView::missingContent: bits 0 .. 2 the file of oscillator 1 .. 3, 3 .. 5 the user wavetable of oscillator 1 .. 3, 6 the impulse of Convolve, 7 the sample of the noise)
    /** A switched-off oscillator is read-only: only POWER remains usable; every other control, Q route and display decoration is dimmed. */
    void applyInactive()
    {
        const bool off = ! powerOn();
        full_.setInactive (off);
        // POWER, the instance selector and CLOSE stay usable; every other cell takes no edit while the oscillator is off
        for (Item& it : main_)
            if (it.hasCell && off && it.id != kInstances && it.id != kClose && it.id != kPower)
                it.cell.enabled = false;
    }
    /** The text "content missing: <name>" for the oscillator on screen ("" when it misses nothing); the name is taken from the state's content reference (the pack's title, else the file name; the reference is there although the file is not). */
    void updateMissing (const IInstanceView& v)
    {
        missingText_.clear();
        const unsigned bits = v.missingContent();
        if (bits == 0)
            return;
        const int osc = playosc::oscNumber (sel_);
        bool missing = false;
        std::string name;
        const std::shared_ptr<const synthyboy::StateContent> c = v.content();
        if (sel_ == playosc::kNoise)
        {
            missing = (bits & (1u << 7)) != 0;
            if (missing && c != nullptr)
                for (const synthyboy::ContentSample& s : c->samples)
                    if (s.osc == synthyboy::kNoiseSampleOsc)
                        name = ui::contentname::display (s.ref.name, s.ref.file);
        }
        else if (osc >= 1 && osc <= 3)
        {
            const bool fileType = typeRaw_ == 1 || typeRaw_ == 2 || typeRaw_ == 3 || typeRaw_ == 4;
            if (fileType)
            {
                missing = (bits & (1u << (osc - 1))) != 0;
                if (missing)
                    name = loadedName (v);
            }
            else
            {
                missing = (bits & (1u << (3 + osc - 1))) != 0;
                if (missing && c != nullptr)
                    for (const synthyboy::ContentWavetable& w : c->wavetables)
                        if (w.osc == osc)
                            name = ui::contentname::display (w.ref.name, w.ref.file);
            }
        }
        if (missing)
            missingText_ = name.empty() ? std::string ("content missing") : "content missing: " + name;
    }
public:
private:
    /** The line over the display (in the header band of the compact view, else at the top of the display rectangle of the state on screen), in the warning colour; a name that does not fit loses its middle. */
    void drawMissing (gpu::SceneBuilder& sb) const
    {
        const ui::Rect r = displayRect();
        if (missingText_.empty() || r.empty())
            return;
        const ui::theme::Look& T = ui::theme::L();
        // the compact view: the band of the display between the power button and the route / full screen button; the other states: between the corners of the display
        const ui::Rect box = stackDepth_ == 0 ? compactLabelRect() : ui::Rect { r.x + 4 + 64 + 8, r.y + 8, std::max (0, r.w - 2 * (4 + 64 + 8)), 48 };
        const float size = ui::Pen::fitSize (missingText_.c_str(), static_cast<float> (box.w), static_cast<float> (box.h), T.fLabel + 4.0f, T.fMin);
        if (size <= 0.0f)
            return;
        char fitted[96];   // the size of the pen's own buffer: middleFit cuts a name that would not go into it
        ui::contentname::middleFit (missingText_.c_str(), size, static_cast<float> (box.w), fitted, static_cast<int> (sizeof fitted));
        ui::Cell cell (sb, box);
        ui::Pen pen (sb, true);
        pen.textCentered (box, fitted, size, pen.c (T.negative, 255), T.fMin);   // at the size the middle cut was made for (a larger maximum would let the pen cut the end of the cut text again)
    }
    std::string missingText_;

    bool nativeRootLoad() const noexcept
    {
        namespace n = nativeui::oscdetail;
        const int source = playosc::oscNumber (sel_);
        return source > 0 && nativeDetailState_.fields[n::kRootLoad + source - 1].target >= 0;
    }
    const char* loadLabel() const noexcept { return typeRaw_ == 4 ? "LOAD INSTRUMENT" : "LOAD SAMPLE"; }

    /** The empty state: a type that plays a file with no file loaded gets one button over its display (the page of the type and the full screen). */
    void updateLoadButton (const IInstanceView& v)
    {
        const bool fileType = sel_ != playosc::kSub && sel_ != playosc::kNoise && (typeRaw_ == 1 || typeRaw_ == 2 || typeRaw_ == 3 || typeRaw_ == 4);
        const bool onPage = stackDepth_ >= 1 && (pageTop_ == 0 || oscspec::isSpecPage (pageTop_));
        const ui::Rect r = onPage ? displayRect() : ui::Rect {};
        loadShown_ = fileType && onPage && ! full_.inactive() && ! r.empty() && v.contentLoad (nullptr) != 1 && loadedName (v).empty();
        loadRect_ = r.empty() ? ui::Rect {} : ui::Rect { r.x + r.w / 2 - 150, r.y + r.h * 2 / 3 - 8, 300, 64 };
        marksRect_ = r;
    }

    // ======================================================================================================================================================================
    // the wavetable browser and the stepping of the table (docs/MPC.md "Wavetable browser")
    /** The content reference of the table FILE oscillator `sel_` has (pack, file, shown name); false when its content has none. */
    bool contentTable (const IInstanceView& v, std::string& pack, std::string& file, std::string& title) const
    {
        const std::shared_ptr<const synthyboy::StateContent> c = v.content();
        const int osc = playosc::oscNumber (sel_);
        if (c == nullptr)
            return false;
        for (const synthyboy::ContentWavetable& w : c->wavetables)
            if (w.osc == osc)
            {
                pack = w.ref.pack;
                file = w.ref.file;
                title = ui::contentname::display (w.ref.name, w.ref.file);
                return true;
            }
        return false;
    }
    /** The table stepper: the name of the built-in table, or the title of the file when the oscillator plays one (the parameter is "Preset table" and the content has the file). With a file the stepper has three
        positions (previous, this, next: the arrows are the files of the category, dimmed at its ends once the catalogue says where the file is), else the 15 choices of the parameter. */
    void showTable (oscmodel::Cell& st, const Item& it, float plain, const IInstanceView& v) const
    {
        const synthyboy::ParamInfo& pi = info (it.param);
        const int choice = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, pi.numChoices - 1));
        std::string pack, file, title;
        const bool have = contentTable (v, pack, file, title);
        showStepper (st, it.param, plain);
        if (choice != synthyboy::kWavetablePresetTable || ! have)
            return;
        oscmodel::setText (st, title.c_str());
        if (! stepValid_)
        {
            st.count = 3; st.index = 1; st.wrap = false;   // the catalogue is read at the first step: both arrows are lit until then
            return;
        }
        int entry = -1, index = 0, count = 0;
        const int category = stepCat_.findFile (pack, file, entry);   // where the file the content names stands in the catalogue we have
        if (category >= 0 && synthyboy::filePosition (stepCat_, category, entry, index, count))
        {
            // the position among the files of the category as three places: first (no previous), in the middle, last (no next); a category of one file has no arrow
            st.wrap = false;
            st.count = count > 1 ? 3 : 1;
            st.index = count > 1 ? (index <= 0 ? 0 : (index >= count - 1 ? 2 : 1)) : 0;
        }
        // a file that is in no category: the arrows step the 15 choices as for a built-in table (showStepper's state)
    }
    /** The catalogue and the place of the file `pack` / `file` in it, read once per file (a file that is loaded now, or the one a step asked for): the directory listing happens at the first step, not at every frame. */
    void ensureStepInfo (IInstanceView& v, const std::string& pack, const std::string& file)
    {
        if (stepValid_ && stepPack_ == pack && stepFile_ == file)
            return;
        if (stepValid_ && v.contentLoad (nullptr) == 1)
            return;   // a file we asked for is still loading: the content is the old one, our position is the new one
        synthyboy::CatalogueSources src;
        v.wavetableSources (src);
        stepCat_ = synthyboy::buildWavetableCatalogue (synthyboy::StdCatalogueFs {}, src);
        stepCategory_ = stepCat_.findFile (pack, file, stepEntry_);
        stepPack_ = pack;
        stepFile_ = file;
        stepValid_ = true;
    }
    void requestFile (IInstanceView& v, const synthyboy::WavetableEntry& e)
    {
        wtLastPack_ = e.ref.pack;
        wtLastFile_ = e.ref.file;
        v.requestWavetable (playosc::oscNumber (sel_), e.ref);
    }
    /** One step to the previous / next file of the category of the file `pack` / `file`: 0 asked for it, 1 the end of the category (nothing happens: no wrap), 2 the file is in no category. */
    int stepFileBy (IInstanceView& v, const std::string& pack, const std::string& file, int dir)
    {
        ensureStepInfo (v, pack, file);
        if (stepCategory_ < 0)
            return 2;
        int next = -1;
        if (! synthyboy::neighbourFile (stepCat_, stepCategory_, stepEntry_, dir, next))
            return 1;
        const synthyboy::WavetableEntry& e = stepCat_.categories[static_cast<size_t> (stepCategory_)].entries[static_cast<size_t> (next)];
        requestFile (v, e);
        stepEntry_ = next;   // the next step starts from here, before the content shows the new file (the latest request wins in the plugin)
        stepPack_ = e.ref.pack;
        stepFile_ = e.ref.file;
        return 0;
    }
    /** A step of the table: `delta` positions (the arrows: -1 / +1, the wheel: more). A file table steps the files of its category (the end: nothing), a built-in table the choices (the "Preset table" entry is skipped while the
        oscillator has no file: it would play the saw). `target`: the choice the stepper asked for. */
    void stepTable (IInstanceView& v, const Item& it, int delta, int target)
    {
        const synthyboy::ParamInfo& pi = info (it.param);
        const int last = std::max (0, pi.numChoices - 1);
        const int cur = std::clamp (static_cast<int> (std::lround (v.param (it.param) - pi.min)), 0, last);
        std::string pack, file, title;
        const bool have = contentTable (v, pack, file, title);
        if (cur == synthyboy::kWavetablePresetTable && have)
        {
            const int dir = delta > 0 ? 1 : -1;
            int result = 0;
            for (int n = 0; n < std::min (std::abs (delta), 16); ++n)
            {
                result = stepFileBy (v, n == 0 ? pack : stepPack_, n == 0 ? file : stepFile_, dir);
                if (result != 0)
                    break;
            }
            if (result != 2)
                return;   // asked for the neighbour, or the end of the category: nothing more happens
            target = std::clamp (cur + dir, 0, last);   // a file that is in no category: the built-in neighbour
        }
        const int dir = target > cur ? 1 : -1;
        if (target == synthyboy::kWavetablePresetTable && ! have)
            target += dir;   // no file table: "Preset table" would play the saw
        writePlain (v, it.param, pi.min + std::clamp (target, 0, last));
    }
    /** The arrows of the table cell (and the Q-Link): one position to the previous / next table or file; nothing at the ends of a list without wrap. */
    void turnTable (const Item& it, int d, IInstanceView& v)
    {
        const oscmodel::Cell& c = it.cell;
        if (! c.enabled || c.count <= 1 || it.param < 0)
            return;
        const int n = c.wrap ? ((c.index + d) % c.count + c.count) % c.count : std::clamp (c.index + d, 0, c.count - 1);
        if (n != c.index)
            stepTable (v, it, n - c.index, n);
    }
    /** Every frame: the steps the Q knob left (the table is a file: the knob does not move the parameter), and the message of a refused file (the line over the display, the row of the browser). */
    void updateWavetable (IInstanceView& v)
    {
        const int osc = playosc::oscNumber (sel_);
        if (osc < 1 || typeRaw_ != 0)
            return;
        int steps = std::clamp (v.takeWavetableSteps (osc), -8, 8);
        const Item* it = item (kBasic + 1);
        if (steps != 0 && it != nullptr && it->type == T::Table && it->param >= 0)
            stepTable (v, *it, steps, static_cast<int> (std::lround (v.param (it->param) - info (it->param).min)) + (steps > 0 ? 1 : -1));
        std::string message;
        static const char kPrefix[] = "not a wavetable file: ";
        if (v.contentLoad (&message) == 3 && message.compare (0, sizeof kPrefix - 1, kPrefix) == 0)
        {
            missingText_ = message;
            if (! wtLastFile_.empty())
            {
                wtBrowser_.markRefused (wtLastPack_, wtLastFile_, message.substr (sizeof kPrefix - 1));
                if (wtMode_)
                    fillWavetableList();   // the browser's strings were made again: the list points at the new ones
                wtLastFile_.clear();
                stepValid_ = false;   // our position was the file that failed: the content's file is where we are
            }
        }
    }
    /** The wavetable browser in the list popup (over the full screen: from the compact view the full screen opens first): the categories, the tables of one, the current one marked. */
    void openWavetableBrowser (UiStack& stack, IInstanceView& v)
    {
        const Item* it = item (kBasic + 1);
        if (it == nullptr || it->param < 0)
            return;
        synthyboy::CatalogueSources src;
        v.wavetableSources (src);
        synthyboy::WavetableCatalogue cat = synthyboy::buildWavetableCatalogue (synthyboy::StdCatalogueFs {}, src);
        const synthyboy::ParamInfo& pi = info (it->param);
        const int choice = std::clamp (static_cast<int> (std::lround (v.param (it->param) - pi.min)), 0, std::max (0, pi.numChoices - 1));
        std::string pack, file, title;
        const bool fileTable = choice == synthyboy::kWavetablePresetTable && contentTable (v, pack, file, title);
        if (fileTable)
        {
            stepCat_ = cat;
            stepCategory_ = stepCat_.findFile (pack, file, stepEntry_);
            stepPack_ = pack;
            stepFile_ = file;
            stepValid_ = true;
        }
        wtBrowser_.open (std::move (cat), fileTable ? -1 : choice, pack, file);
        wtMode_ = true;
        browseMode_ = false;
        if (stack.depth() == 0)
            stack.openFull (kElementOsc);
        stack.openDetail (kPageList);
        fillWavetableList();
    }
    void fillWavetableList()
    {
        // the header is the list's own bar (the way up and the title), the rows are the categories or the tables
        listData_.header = wtBrowser_.title().c_str();
        listData_.headerBack = ! wtBrowser_.atCategories();
        listData_.items = wtBrowser_.items();
        listData_.count = wtBrowser_.count();
        listData_.current = wtBrowser_.currentRow();
        showCurrentRow();
    }
    void chooseWavetableRow (int row, UiStack& stack, IInstanceView& v)
    {
        using A = WavetableBrowser::Action;
        const Item* it = item (kBasic + 1);
        const A a = wtBrowser_.tap (row);
        if (a == A::Changed)
            fillWavetableList();
        else if (a == A::ChoseBuiltin && it != nullptr && it->param >= 0)
        {
            writePlain (v, it->param, info (it->param).min + wtBrowser_.chosenChoice());   // as the stepper did: the choice parameter, the old table plays until the new one is built
            stack.back();
        }
        else if (a == A::ChoseFile)
        {
            requestFile (v, wtBrowser_.chosenEntry());
            stepCat_ = wtBrowser_.catalogue();
            stepCategory_ = wtBrowser_.chosenCategory();
            stepEntry_ = wtBrowser_.chosenIndex();
            stepPack_ = wtBrowser_.chosenEntry().ref.pack;
            stepFile_ = wtBrowser_.chosenEntry().ref.file;
            stepValid_ = true;
            stack.back();
        }
        // A::Refused: the row says why, nothing changes
    }

    /** The content browser in the list popup (over the full screen: from the compact view the full screen opens first): the roots the instance offers, the folders, the files. */
    void openBrowser (UiStack& stack, IInstanceView& v)
    {
        std::vector<IInstanceView::ContentRoot> roots;
        v.contentRoots (roots);
        if (roots.empty())
            return;
        std::vector<ContentBrowser::Root> rs;
        for (const auto& r : roots)
            rs.push_back ({ r.label, r.path });
        browser_.setRoots (std::move (rs));
        const int kind = typeRaw_ == 4 ? 2 : 1;
        if (! lastChosen_[kind - 1].empty())
            browser_.openAt (kind, lastChosen_[kind - 1]);
        else
            browser_.open (kind);
        browseMode_ = true;
        if (stack.depth() == 0)
            stack.openFull (kElementOsc);
        stack.openDetail (kPageList);
        fillBrowserList();
    }
    void fillBrowserList()
    {
        // the header (the way up and the path) is the list's own bar, the rows are the ones below the first
        browserTitle_ = browser_.title();
        listData_.header = browserTitle_.c_str();
        listData_.headerBack = ! browser_.atRoots();
        listData_.items = browser_.items() + 1;
        listData_.count = std::max (0, browser_.count() - 1);
        const int cur = browser_.rowOf (lastChosen_[browser_.kind() - 1]);
        listData_.current = cur > 0 ? cur - 1 : -1;
        showCurrentRow();
    }

    /** The page of the list that holds the current row (the list opens there). */
    void showCurrentRow() noexcept
    {
        nativeListFirst_ = listData_.current > 0 ? listData_.current - listData_.current % nativeui::modboard::kListRows : 0;
    }

    /** The detail page a page button of MORE opens (by the token of its key in osc_spec_page.h). */
    static int pageOfToken (const char* token) noexcept
    {
        struct E { const char* t; int page; };
        static const E t[] = { { "map", oscspec::kWtMap }, { "region", oscspec::kSampleRegion }, { "slice", oscspec::kSampleSlice }, { "grain", oscspec::kGrain }, { "filter", oscspec::kSpecFilter }, { "scan", oscspec::kSpecScan },
                               { "options", oscspec::kOptions }, { "env", kPageEnvelope }, { "window", kPageGrain }, { "unison", kPageUnison } };
        for (const E& e : t)
            if (std::strcmp (e.t, token) == 0)
                return e.page;
        return 0;
    }

    void addModulation (IInstanceView& v, int source)
    {
        if (! modsContextValid (v))
            return;
        if (const int slot = uibind::ensureModulationSource (v, modsTargetParam_, source); slot >= 0)
        {
            closeModsPicking();
            loadMods (v, slot);
        }
    }

    // ======================================================================================================================================================================
    // the Q group: the four targets the hardware knobs are mapped to
    void resolveQ (IInstanceView&) {}
public:   // the Play view reads the targets of the compact group and sets its block
    /** The four targets of the Basic group of the compact view (table indices, -1 none): TYPE (or the noise kind), SOURCE (OCT of the Sub, FILTER of the noise), PAN, LEVEL. */
    void compactTargets (int* t) const noexcept
    {
        const bool sub = sel_ == playosc::kSub, noise = sel_ == playosc::kNoise;
        const int ids[4] = { noise ? kBasic + 5 : kBasic, noise ? kBasic + 4 : sub ? kBasic + 3 : kBasic + 1, kBasic + 6, kBasic + 7 };
        for (int i = 0; i < 4; ++i)
            t[i] = paramOfId (ids[i], false);
    }
    /** The block of knobs the Basic group of the compact view has (the Play view maps the groups of the three thirds together). */
    void setCompactBlock (int block) noexcept { compactBlock_ = block; }
    int compactBlock() const noexcept { return compactBlock_; }
private:
    int paramOfId (int id, bool full) const noexcept
    {
        if (id < 0)
            return -1;
        if (const Item* it = lookup (page_, id, false))
            if (! full)
                return paramOfItem (*it);
        if (const Item* it = lookup (main_, id, full))
            return paramOfItem (*it);
        if (const Item* it = lookup (page_, id, false))
            return paramOfItem (*it);
        return -1;
    }
    int paramOfItem (const Item& it) const noexcept
    {
        if (! powerOn())
        {
            const Item* power = item (kPower);
            if (! power || it.param != power->param)
                return -1;
        }
        if (it.alwaysOff || (it.type == T::Inert && it.id != kPanel + static_cast<int> (oscmodel::PanelSlot::WarpMenu)))
            return -1;   // (the WARP menu of the granular oscillator is a button with a parameter: warp.type, a Q member of the GRAIN group)
        if (it.type == T::SnapBit)
            return -1;   // (SnapBit: bit 12 of the parameter the mask editor of its block already is the Q member of)
        if (it.type == T::NumberOct && it.param >= 0)
            return QRouter::encode (it.param, QRouter::kOct);   // Q 1 of the Tune group: the octaves of the transpose
        if (it.type == T::NumberSem && it.param >= 0)
            return QRouter::encode (it.param, QRouter::kSem);   // Q 2: what is left inside the octave
        return it.param;
    }
    /** The groups of the page in reading order: the compact view has one (Basic), the full screen the blocks of its panel (Basic, Tune, ... Warp), the detail pages their blocks, the modulation list one per row; the active
        group is on Q 1 .. 4, the next three (wrapping) on 5 .. 16. */
    void publishQGroup (IInstanceView& v)
    {
        const char* name = "";
        auto members = [this] (const QGroupIds& g, bool full, int* t) { for (int i = 0; i < 4; ++i) t[i] = paramOfId (g.id[i], full); };
        if (stackDepth_ == 0)
        {
            qActive_ = 0;
            return;   // the main state: the Play view publishes the three groups of the page together (PlayView::publishQ), the oscillator's is the first
        }
        else if (pageTop_ == kPageUnison || pageTop_ == kPageGrain || pageTop_ == kPageEnvelope)
        {
            name = "Detail";
            qSlots_.publish (v, nDetailGroups_, detailActive_, [&] (int g, int* t) { members (detailGroups_[g], false, t); });
        }
        else if (oscspec::isSpecPage (pageTop_))
        {
            name = "Page";
            qSlots_.publish (v, spec_.numGroups(), spec_.activeGroup(), [&] (int g, int* t) {
                for (int i = 0; i < 4; ++i)
                {
                    const int ctl = spec_.member (g, i);
                    t[i] = ctl >= 0 ? paramOfId (kSpec + ctl, false) : -1;
                }
            });
        }
        else if (pageTop_ == kPageMods)
        {
            name = "Mods";
            qSlots_.publish (v, powerOn() && ! modsBoard() ? mods_.numRows() : 0, mods_.activeRow(), [this] (int row, int* t) { modRowTargets (row < kModsPerPage ? modRows_[row].slot : -1, t); });
        }
        else if (pageTop_ == kPageList)
        {
            name = "List";
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else if (pageTop_ == kPageRoute)
        {
            name = "Route";
            qSlots_.publish (v, 1, 0, [&] (int, int* t) {
                for (int i = 0; i < 4; ++i)
                    if (const auto* item = find (kRouteTarget + i))
                        t[i] = full_.inactive() || (i == 1 && ! routeFilter_) ? -1 : item->param;
            });
        }
        else if (fullActive_ < 0 && tuneFocus_ >= 0 && full_.hasNativeTune() && isNativeRoutingActive())
        {
            name = "Tune";
            qSlots_.publish (v, 2, tuneFocus_, [this] (int g, int* t) {
                if (g == 0)
                    std::copy_n (tuneTargets_.begin(), 4, t);
                else
                    t[0] = tuneTargets_[4];
            });
            qActive_ = tuneFocus_;
        }
        else
        {
            const int mix = nFullGroups_;
            const bool hasMix = isNativeRoutingActive();
            const int count = mix + (hasMix ? 1 : 0);
            const int active = hasMix && fullActive_ < 0 ? mix : fullActive_;
            name = hasMix && active == mix ? "Mix" : (active >= 0 && active < mix ? fullGroups_[active].name : "");
            qSlots_.publish (v, count, active, [&] (int g, int* t) {
                if (g < mix)
                    members (fullGroups_[g], true, t);
                else
                {
                    t[0] = paramOfId (kBasic + 6, false);
                    t[1] = paramOfId (kBasic + 7, false);
                    t[2] = paramOfId (kRoute, false);
                }
            });
            qActive_ = active;
        }
        if (pageTop_ != 0)
            qActive_ = 0;
        ui::setText (qName_, sizeof qName_, name);
    }

    // ======================================================================================================================================================================
    static constexpr int kMaxDetailGroups = 4;
    const char* compactSourceText() const noexcept { const Item* it = item (kBasic + 1); return it != nullptr ? it->cell.text : ""; }

    oscmodel::CompactLayout cl_;
    ui::Rect compactRect_ { kCompactX, kTabY + 4, kCompactW, kTabH - 8 }, tabRect_ { 0, kTabY, kCanvasW, kTabH };
    CompactNativeCells compactNativeCells_ {};
    OscFullView full_;
    QGroupIds fullGroups_[oscmodel::kMaxGroups];
    int nFullGroups_ = 0, fullActive_ = 0;
    const oscmodel::PanelPreset* detailPreset_ = nullptr;      // the preset of the detail page on screen (unison, grain window, envelope)
    oscmodel::DetailLayout detailLayout_;
    QGroupIds detailGroups_[kMaxDetailGroups];
    int nDetailGroups_ = 0, detailActive_ = 0;
    ModRowsPage mods_ { ModRowsPage::Kind::Modulations };
    /** The list on the screen (the content browser, the wavetable browser, the source of a new modulation): the header (the way up and the title), the rows. Plain data; the host skin shows it with the cells of the modulation picker. */
    struct ListData
    {
        const char* header = nullptr;
        bool headerBack = false;
        const char* const* items = nullptr;
        int count = 0, current = -1;
    };
    ListData listData_;
    int nativeListFirst_ = 0;            // the first row of the page of the list shown
    uint32_t nativeListSignature_ = 0;   // what the host was told last (the names, the page): a new one makes new cells and a new text page
    int nativeListSerial_ = 0;
    bool listPublished_ = false, maskPublished_ = false;
    Item* maskItem_ = nullptr;           // the note mask of the transpose snap on the detail page now (the item holds the parameter)
    ui::Rect maskArea_;
    static constexpr int kMaskHeight = 120;   // two rows of 56 and a gap of 8 (components/native/note_mask)
    bool modsPicking_ = false, modsListFilled_ = false;
    int modsListGroup_ = -1;
    std::vector<const char*> modsListNames_;
    std::vector<int> modsListChoices_;                         // per row: a source (> 0) or a group (< 0) of the list of the sources
    int compactBlock_ = -1;
    bool nativeSelector_ = false;
    NativeCellsArray nativeSettingsCells_ {};
    std::array<int, nativeui::osc::kSettingsAll> nativeSettingsPaint_ = [] { std::array<int, nativeui::osc::kSettingsAll> a {}; a.fill (-1); return a; }();
    nativeui::oscdetail::Frame nativeDetail_ = nativeui::oscdetail::Hidden;
    nativeui::oscdetail::State nativeDetailState_;
    std::array<Item*, nativeui::oscdetail::kCells> nativeDetailItems_ {};
    std::array<const char*, oscspec::OscSpecPage::kMaxBlocks> nativeDetailBlocks_ {};
    const char* nativeDetailTitle_ = "";
    bool nativeRouting_ = false;
    oscspec::OscSpecPage spec_;
    ui::Rect loadRect_;
    ui::Rect marksRect_;
    bool loadShown_ = false, specVisible_ = false;
    ContentBrowser browser_;
    std::string browserTitle_;
    bool browseMode_ = false;
    WavetableBrowser wtBrowser_;
    bool wtMode_ = false;                       // the list is the wavetable browser
    // the stepping through the files of one category (the arrows of the table cell, the Q knob): the catalogue as it was when the file table was loaded or the browser opened, and where the table is in it
    synthyboy::WavetableCatalogue stepCat_;
    std::string stepPack_, stepFile_, wtLastPack_, wtLastFile_;
    int stepCategory_ = -1, stepEntry_ = -1;
    bool stepValid_ = false;
    std::string lastChosen_[2];   // the file that was loaded last for the granular (0) and multisample (1) type: the browser opens in its folder
    std::vector<Item> main_, page_;
    bool bindingFull_ = false;   // bind () marks the rows of the full screen
    bool configured_ = false, modsDirty_ = true;
    int pageSel_ = 0;   // the oscillator of a page of the OSC tab (setPage), 0: the Play page's UI
    int noiseSample_ = -1;
    int w_ = 1280, h_ = 628, sel_ = 2, typeRaw_ = 0, highlight_ = 1, stackDepth_ = 0, pageTop_ = 0, pageSig_ = 0, pageLoaded_ = 0, qActive_ = 0;
    int envSel_ = 1, lfoSel_ = 1, selectedSource_ = ui::kNoSource;
    double modsAtMs_ = -1000.0;
    int tuneFocus_ = -1;
    std::array<int, 5> tuneTargets_ {{ -1, -1, -1, -1, -1 }};
    std::string prefix_ = "osc1.";
    char typeName_[16] = {}, qName_[16] = {};
    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
