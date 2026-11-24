// SynthyBoy - MPC overlay: the oscillator UI of the Play page, the plugin-drawn pages built from the UI library (shell-mpc/ui/) and wired to the real engine parameters (docs/MPC.md "Oscillator UI").
// One object per (Play view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / describe (), with the instance as IInstanceView.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h): main = the compact view in the oscillator third of the Play page (OscillatorCompact); full = the full screen of the instance (OscillatorFullScreen) over the whole
// plugin area of the Play page; detail pages above it (unison settings, grain window, multisample envelope, the modulations of a control) and the route popup. The picture of the oscillator cycle goes
// into the display rectangle (displayRect (); the Play view draws it).
// What it does with a touch: the library component gives a Change { id, kind, value }; the id says which control it was (the id spaces below), the binding of the id (a key suffix of the engine table
// and a type) says which parameter and how the UI value converts; the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). Every update () reads every parameter back
// into the component states, so host automation, presets and the host's own widgets show on the next frame.
// Controls whose engine parameter does not exist or is not read by the engine are drawn DISABLED (docs/MPC.md "Oscillator UI": WARP Var, phase memory, the source list of the sample types, the
// source dropdown).
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
#include "../ui/ui.h"
#include "../ui/choice_names.h"
#include "content_browser.h"
#include "wavetable_browser.h"
#include "osc_spec_page.h"
#include "modulation_window.h"
#include "native_mod_targets_ui.h"
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
        kTuneOct = 110, kTuneFin = 112, kTuneCrs = 113, kTuneMore = 115,   // OCT (A, B, C), FIN, the PITCH of the noise, the MORE button of the full screen
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
        Knob,        // normalised 0 .. 1 of the parameter
        Number,      // a NumberField in the units of the parameter
        NumberOct,   // transpose: OCT = steps of 12 semitones
        NumberSem,   // transpose: SEM = the remainder
        NumberDeg,   // a percent parameter shown as degrees (0 .. 100 % = 0 .. 360)
        Stepper,     // a choice parameter
        Toggle,
        UpDown,
        Picto,       // the type pictograms of A, B, C
        PictoPlain,  // the waveform pictograms of the sub oscillator
        DetailNorm,  // a DetailButton with a 0 .. 1 value
        DetailInt,   // a DetailButton with an integer value
        Route,       // the route button: the destination choice
        List,        // a short choice parameter as a LedList (all options visible)
        Mask,        // the notes of the transpose snap (NoteMask): bits 0 .. 11 of the parameter
        SnapBit,     // the toggle for bit 12 of the same parameter (snap the played note, not the transpose)
        Source,      // the source of a granular / multisample oscillator: the name of the file that is loaded; a tap opens the content browser
        Table,       // the table of a wavetable oscillator: a built-in table (the choice parameter) or a table FILE (content); the name opens the wavetable browser, the arrows step the 15 choices or the files of the category
        Inert        // no value: a button
    };

    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        std::string key;           // the key of the engine parameter ("" = none: the control is disabled)
        int param = -1;            // its table index (-1: none)
        int param2 = -1;           // the second parameter (none of the current types has one)
        bool alwaysOff = false;    // drawn disabled whatever the engine says (nothing behind it)
        int row = -1;              // a control of the modulations page: the row
        bool full = false;         // a part of the full screen (rebuilt when the noise changes its mode)
    };

    OscUi() { setCanvas (kCanvasW, kCanvasH); configure (nullptr, 2, true); compact_.setNativeInstanceSelector (false); }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** The canvas is the whole screen (1280 x 800: the Play view's region; every coordinate is a screen pixel); the pages live in the tab (y kTabY .. kTabY + kTabH). */
    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kCompactX = 8, kCompactW = 420;                // the left third of the Play page
    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        compact_.place ({ kCompactX, kTabY + 4, kCompactW, kTabH - 8 });
        full_.place (tab);
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
        list_.setHeaderId (kListHeader);
        detail_.place (tab);
        spec_.place (tab);
        mods_.place (tab);
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
                mods_.setPickingSource (false);
            }
        }
        // Older sessions may still carry the removed Sub/Noise MORE page.
        if ((sel_ == playosc::kSub || sel_ == playosc::kNoise) && stack.depth() > 1 && stack.top() == oscspec::kMore)
            stack.openFull (kElementOsc);
        stackDepth_ = stack.depth();
        if (stackDepth_ > 0 && srcPopup_)
            closeSourceList();
        if (srcPopup_ && ! popupIsType_)
            updateSourceRows (inst);
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
        if (pageTop_ == kPageList && listParam_ >= 0)
            list_.state.current = static_cast<int> (std::lround (inst.param (listParam_)));
        const bool standaloneNative = isPage() && ! expandedPlay && (nativeSelector_ || nativeRouting_);
        inst.setUiFullOpen (standaloneNative ? (pageTop_ != 0) : ! stack.isMain());
        publishQGroup (inst);
    }

    /** On standalone tabs OscPageUi supplies the active source independently of Play and uses the close position for its name.
        `sel`: 1 Sub, 2 A, 3 B, 4 C, 5 Noise. 0 selects the Play layout. */
    void setPage (int sel) noexcept { pageSel_ = sel >= 1 && sel <= playosc::kInstances ? sel : 0; configured_ = false; }
    bool isPage() const noexcept { return pageSel_ > 0; }

    /** The rectangle (canvas coordinates) the picture of the oscillator goes into now; empty when the current page shows none. */
    ui::Rect displayRect() const noexcept
    {
        if (nativeDetail_ >= nativeui::oscdetail::Graph && nativeDetailState_.page == pageTop_ && pageTop_ != kPageEnvelope)
            return nativeDetailGraphRect();
        if (stackDepth_ == 0)
            return compact_.displayRect();
        if (stackDepth_ == 1 || pageTop_ == kPageList)
            return full_.displayRect();
        if (oscspec::isSpecPage (pageTop_))
            return spec_.displayRect();
        if (pageTop_ == kPageUnison || pageTop_ == kPageGrain)
            return detail_.displayRect();   // the same picture of the oscillator over the page (the fan of the unison voices, the grain window on the sample)
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
    ui::Rect plotArea() const noexcept { return stackDepth_ == 0 ? compact_.plotRect() : ui::Rect {}; }
    bool isMain() const noexcept { return stackDepth_ == 0; }
    /** The instance (1 Sub .. 5 Noise) and the type of the oscillator it shows (0 wavetable, 1 sample, 2 granular, 3 spectral, 4 multisample; -1 for Sub and Noise). */
    int selection() const noexcept { return sel_; }
    int typeRaw() const noexcept { return sel_ == playosc::kSub || sel_ == playosc::kNoise ? -1 : typeRaw_; }
    const char* typeName() const noexcept { return typeName_; }
    /** The list of the targets of the active Q group (table indices, -1 none) and the name of the group. */
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }

    void draw (gpu::SceneBuilder& sb) const
    {
        drawPage (sb);
        drawPopup (sb);
    }
    /** The page without its popups (the picture of the oscillator goes on it, then the popups). */
    void drawPage (gpu::SceneBuilder& sb) const
    {
        if (nativeDetail_ >= nativeui::oscdetail::Plain)
        {
            drawNativeDetail (sb);
            if (pageTop_ == kPageEnvelope)
                drawEnvelope (sb);
            return;
        }
        if (stackDepth_ == 0)
        {
            compact_.draw (sb);
            return;
        }
        if (oscspec::isSpecPage (pageTop_))
        {
            spec_.draw (sb);
            return;
        }
        if (pageTop_ == kPageUnison || pageTop_ == kPageGrain || pageTop_ == kPageEnvelope)
        {
            detail_.draw (sb);
            if (pageTop_ == kPageEnvelope && ! detail_.displayRect().empty())
                drawEnvelope (sb);
            return;
        }
        if (pageTop_ == kPageMods)
        {
            mods_.draw (sb);
            return;
        }
        if (pageTop_ != kPageRoute)
            full_.draw (sb);
    }
    /** The envelope of the multisample oscillator as a curve in the display area of its page (the schematic of the six values: delay, attack, hold, decay, sustain level, release). */
    void drawEnvelope (gpu::SceneBuilder& sb) const
    {
        const ui::Rect r = nativeDetail_ >= nativeui::oscdetail::Graph ? nativeDetailGraphRect() : detail_.displayRect();
        ui::display::Module m;
        m.rect = r;
        m.plot = { r.x + 16, r.y + 38, r.w - 32, r.h - 52 };
        m.env = true;
        m.title = "ENVELOPE";
        m.v = envCurve_;
        m.n = kEnvPoints;
        ui::display::drawModule (sb, m);
    }
    /** The route popup and the list popup (over the page and over the picture). */
    void drawPopup (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
        {
            if (srcPopup_)
                list_.draw (sb);   // the list of the source selector lies over the third
            return;
        }
        if (pageTop_ == kPageUnison || pageTop_ == kPageGrain || pageTop_ == kPageEnvelope || pageTop_ == kPageMods || oscspec::isSpecPage (pageTop_))
            return;
        if (pageTop_ == kPageList)
            list_.draw (sb);
    }
    /** What goes on top of the picture in the display rectangle (the full screen button of the compact view). */
    void drawOnDisplay (gpu::SceneBuilder& sb) const
    {
        drawMissing (sb);
        if (stackDepth_ == 0)
        {
            compact_.full.draw (sb);
            compact_.power.draw (sb);
            compact_.route.draw (sb);   // the buttons that stand in the corners of the display lie over its picture
        }
        else if (loadShown_ && ! nativeRootLoad() && nativeDetail_ < nativeui::oscdetail::Plain)
            loadBtn_.draw (sb);
    }
    /** The empty state of a type that plays a file (sample, grain, spectral, multisample) while no file is loaded: one button over the display that opens the browser. */
    bool loadButtonShown() const noexcept { return loadShown_; }
    const ui::MenuButton& loadButton() const noexcept { return loadBtn_; }
    const oscspec::OscSpecPage& specPage() const noexcept { return spec_; }
    oscspec::OscSpecPage& specPage() noexcept { return spec_; }
    const ui::Rect& marksRect() const noexcept { return marksRect_; }
    /** The touch hit this view's drawing (the main state: the oscillator third; any other state: everything). */
    bool covers (int x, int y) const noexcept
    {
        if (stackDepth_ > 0)
        {
            if (pageTop_ == kPageMods && mods_.nativeHit (x, y))
                return false;
            if (pageTop_ == 0)
            {
                if (nativeRootLoad() && loadBtn_.rect().contains (x, y))
                    return false;
                if (nativeSelector_ && selectorArea().contains (x, y))
                    return false;
                if (full_.isNativeRouting (x, y))
                    return false;
            }
            return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
        }
        for (const auto& rect : compactNativeCells_)
            if (rect.contains (x, y))
                return false;
        if (compact_.basic.pan.rect().contains (x, y) || compact_.basic.level.rect().contains (x, y))
            return false;
        return ui::Rect { kCompactX, kTabY + 4, kCompactW, kTabH - 8 }.contains (x, y);
    }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** One touch event in UI pixels (the whole screen), `stack` is the instance's stack (the caller holds its lock). Returns true when the touch belongs to the plugin-drawn pages. */
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        ui::Component* top = nativeDetail_ >= nativeui::oscdetail::Plain ? nativeDetailMask_ : topComponent();
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            int nativeRow = -1;
            if (pageTop_ == kPageMods && mods_.nativeHit (x, y, &nativeRow))
            {
                if (nativeRow >= 0)
                {
                    mods_.selectRow (nativeRow);
                    publishQGroup (inst);
                }
                return false;
            }
            if (! covers (ev.x, ev.y) || ! top)
                return false;
            if (nativeDetail_ < nativeui::oscdetail::Plain && loadShown_ && ! srcPopup_ && loadBtn_.rect().contains (x, y))
            {
                loadBtn_.touchDown (x, y, nowMs);
                gesture_ = &loadBtn_;
                gestureDown_ = true;
                return true;
            }
            if (nativeDetail_ >= nativeui::oscdetail::Plain)
            {
                if (! top->rect().contains (x, y))
                    return false;
                focusNativeDetail (*top, inst);
            }
            gesture_ = top;
            gestureDown_ = true;
            handle (top->touchDown (x, y, nowMs), stack, inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (x, y), stack, inst);
        else
        {
            if (gesture_ == &loadBtn_)
            {
                const ui::Change c = loadBtn_.touchUp (x, y);
                if (c.kind == ui::ChangeKind::Request)
                    openBrowser (stack, inst);
                gestureDown_ = false;
                gesture_ = nullptr;
                return true;
            }
            handle (gesture_->touchUp (x, y), stack, inst);
            gestureDown_ = false;
            gesture_ = nullptr;
        }
        return true;
    }
    /** The clock while a finger is down (the long press of a knob). */
    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst);
    }
    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the control of the page under the pointer takes it as steps of its own (a knob: one Q-Link step, a stepper, a list, a scrolled
        list; ui::Component::wheelOver). false: nothing of this page lies there, or a finger is down on it. */
    bool wheel (int x, int y, int notches, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        ui::Component* top = nativeDetail_ >= nativeui::oscdetail::Plain ? nativeDetailMask_ : topComponent();
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
    /** The stack changed behind the UI's back (the page left, another instance): the gesture is dropped. */
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    /** The page went off the screen (a host dialog over it): the open list of the compact view closes, so it is not still there when the page comes back. */
    void closePopups() { if (srcPopup_) closeSourceList(); }
    bool popupOpen() const noexcept { return srcPopup_; }
    /** Another element's page opened or closed: the Q group is published again at the next update (the router keeps what the last page set). */
    void forgetQGroup() noexcept { qSlots_.forget(); }

    // ---- access for the tests
    ui::OscillatorCompact& compact() noexcept { return compact_; }
    const ui::OscillatorCompact& compact() const noexcept { return compact_; }
    /** Standalone oscillator tabs expose the instance choice to the host; the
        page wrapper owns the aperture and touch pass-through. */
    void setNativeInstanceSelector (bool shown) noexcept
    {
        nativeSelector_ = shown;
        full_.setNativeInstanceSelector (shown);
    }
    bool hasNativeInstanceSelector() const noexcept { return nativeSelector_; }
    ui::Rect selectorArea() const noexcept { return nativeSelector_ ? ui::Rect { 8, kTabY + 33, 200, 56 } : ui::Rect {}; }
    /** Standalone oscillator tabs expose the destination routing choice to the host while ON; modals and OFF state close this aperture. */
    void setNativeRouting (bool shown) noexcept
    {
        nativeRouting_ = shown;
        full_.setNativeRouting (shown);
    }
    bool hasNativeRouting() const noexcept { return nativeRouting_; }
    bool isNativeRoutingActive() const noexcept { return nativeRouting_ && full_.isNativeRoutingActive(); }
    ui::Rect routingArea() const noexcept { return nativeRouting_ ? full_.routingArea() : ui::Rect {}; }
    void focusNativeTune (bool track, IInstanceView& inst)
    {
        tuneFocus_ = track ? 1 : 0;
        full_.setActiveGroup (-1);
        publishQGroup (inst);
    }
    void focusNativeMix (IInstanceView& inst)
    {
        tuneFocus_ = -1;
        full_.setActiveGroup (-1);
        publishQGroup (inst);
    }
    ui::OscillatorFullScreen& full() noexcept { return full_; }
    const ui::OscillatorFullScreen& full() const noexcept { return full_; }
    ui::DetailPage& detail() noexcept { return detail_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    const ui::ModulationsPage& modsPage() const noexcept { return mods_; }
    void syncNativeModulations (IInstanceView& inst, UiStack& stack, nativeui::targets::Owner owner)
    {
        const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        const bool shown = stack.element() == kElementOsc && stack.top() == kPageMods && ! mods_.boardShown()
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
    }
    ui::ListPopup& list() noexcept { return list_; }
    const std::vector<Item>& items() const noexcept { return main_; }
    const std::vector<Item>& pageItems() const noexcept { return page_; }
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
        state.powered = compact_.power.state.on;
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

    void publishCompactNative (IInstanceView& inst, bool shown)
    {
        namespace n = nativeui::playoscui;
        const auto state = shown ? compactNativeState (inst) : n::State {};
        const auto items = compactNativeItems();
        compactNativeCells_.fill ({});
        for (int field = 0; field < n::kFields; ++field)
        {
            if (compactNativePaint_[field] != nullptr)
                compactNativePaint_[field]->setHostPainted (false);
            const bool native = shown && items[field] != nullptr;
            compactNativePaint_[field] = native ? items[field]->c : nullptr;
            if (native)
            {
                compactNativePaint_[field]->setHostPainted (true);
                const auto cr = n::contractRect (state.layout, field);
                compactNativeCells_[field] = cr.w > 0 && cr.h > 0
                    ? ui::Rect { cr.x, kTabY + cr.y, cr.w, cr.h }
                    : compactNativePaint_[field]->rect();
            }
        }
        compact_.setNativeHeader ({ compactNativeCells_[n::Power], compactNativeCells_[n::Expand], compactNativeCells_[n::Route] });
        const bool abc = state.layout == n::AbcWave || state.layout == n::AbcContent;
        inst.setNativePlayOsc (state, abc && state.powered ? compact_.basic.source.stepper.state.text : "");
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
        const auto* pair = full_.panel.warp();
        if (pair == nullptr)
            return 0;
        return (pair->first.applies() ? 1 : 0) | (! pair->single() && pair->second.applies() ? 2 : 0);
    }

    /** Stable full-screen field order. The generated skin uses these live bound
        items, skipping a leaf when the current mode's layout hides it. */
    std::array<Item*, nativeui::osc::kSettingsAll> nativeSettingsItems()
    {
        namespace n = nativeui::osc;
        std::array<Item*, n::kSettingsAll> fields {};
        const auto view = nativeSettingsView();
        if (view == n::SettingsHidden)
            return fields;
        std::array<ui::Component*, ui::Composite::kMaxKids> leaves;
        const auto end = leaves.begin() + full_.leaves (leaves.data(), static_cast<int> (leaves.size()));
        const bool abc = view >= n::SettingsWavetable;
        int next = view == n::SettingsSub ? 0 : n::kSubSettings;
        const int limit = view == n::SettingsSub ? n::kSubSettings : n::kSettings;
        for (auto& it : main_)
        {
            if (! it.full || it.id == kPower || it.id == kPanel + static_cast<int> (ui::PanelSlot::SubBlend) || it.c == nullptr || it.alwaysOff)
                continue;
            const int abcSlot = abc ? abcNativeField (it) : -1;
            const int slot = abc ? (abcSlot >= 0 ? n::abcSettingIndex (sel_, abcSlot) : -1)
                                 : (it.param >= 0 && next < limit ? next++ : -1);
            const bool cardOctave = full_.hasNativeTune() && it.id == kTuneOct;
            if (slot >= 0 && ! it.c->rect().empty() && (cardOctave || std::find (leaves.begin(), end, it.c) != end))
                fields[slot] = &it;
        }
        return fields;
    }

    void setNativeSettingsPaint (const std::array<Item*, nativeui::osc::kSettingsAll>& fields)
    {
        nativeSettingsCells_.fill ({});
        for (size_t slot = 0; slot < fields.size(); ++slot)
        {
            if (nativeSettingsPaint_[slot] != nullptr)
                nativeSettingsPaint_[slot]->setHostPainted (false);
            const Item* field = fields[slot];
            nativeSettingsPaint_[slot] = field && !(full_.hasNativeTune() && field->id == kTuneOct) ? field->c : nullptr;
        }
        size_t next = 0;
        for (auto* component : nativeSettingsPaint_)
            if (component != nullptr && next < nativeSettingsCells_.size())
            {
                component->setHostPainted (true);
                nativeSettingsCells_[next++] = component->rect();
            }
        full_.setNativeCells (next > 0 ? &nativeSettingsCells_ : nullptr);
    }

    const ui::OscillatorFullScreen::NativeCells& nativeSettingsCells() const noexcept { return nativeSettingsCells_; }

    void focusNativeSetting (int x, int y, IInstanceView& inst)
    {
        for (int i = 0; i < full_.numGroups(); ++i)
            if (full_.groups[i].rect().contains (x, y))
            {
                full_.setActiveGroup (i);
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
            case T::Stepper: case T::PictoPlain: kind = n::SettingsMenu; break;
            case T::UpDown: kind = n::SettingsVoices; break;
            case T::Table: case T::Source: kind = n::SettingsSource; break;
            case T::Inert: kind = item.param >= 0 ? n::SettingsMenu : n::SettingsAction; break;
            default: kind = item.id == kPanel + static_cast<int> (ui::PanelSlot::SubPhase) ? n::SettingsPhase : n::SettingsDial; break;
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
                    const int delta = action == n::SourcePrevious ? -1 : 1;
                    writeTable (item, item.c->turn (delta), inst);
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
            const auto& range = static_cast<const ui::NumberField*> (item.c)->state;
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
            || item.id == kPanel + static_cast<int> (ui::PanelSlot::NoiseType);
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
            if (sel_ == playosc::kSub) return full_.subBlend.rect();
            if (sel_ == playosc::kNoise)
                if (const auto* blend = full_.panel.part (ui::PanelSlot::SubBlend)) return blend->rect();
        }
        if (field == n::kMore) return full_.more.rect();
        if (field == n::kBack) return { n::kBackX, kTabY + n::kBackY, n::kBackW, n::kBackH };
        if (field == n::kLoad) return { n::kLoadX, kTabY + n::kLoadY, n::kLoadW, n::kLoadH };
        if (field >= n::kRootLoad) return loadBtn_.rect();
        return nativeDetailCell (field % n::kCells, nativeDetail_ >= n::Graph);
    }

    const nativeui::oscdetail::State& prepareNativeDetail (bool enabled, uint64_t preset)
    {
        namespace n = nativeui::oscdetail;
        if (auto* blend = full_.panel.part (ui::PanelSlot::SubBlend))
            blend->setHostPainted (false);
        nativeDetailItems_.fill (nullptr);
        nativeDetailBlocks_.fill (nullptr);
        nativeDetailMask_ = nullptr;
        nativeDetailTitle_ = "";
        nativeDetailState_ = {};
        nativeDetailState_.preset = preset;
        const bool spec = oscspec::isSpecPage (pageTop_);
        const bool route = pageTop_ == kPageRoute;
        const bool detail = route || pageTop_ == kPageUnison || pageTop_ == kPageGrain || pageTop_ == kPageEnvelope;
        const bool root = stackDepth_ == 1 && pageTop_ == 0;
        if (! enabled || full_.inactive() || (! root && ! spec && ! detail))
        {
            full_.more.setHostPainted (false);
            full_.subBlend.setHostPainted (false);
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
        full_.more.setHostPainted (root && sel_ != playosc::kSub && sel_ != playosc::kNoise);
        full_.subBlend.setHostPainted (root && sel_ == playosc::kSub);
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
                if (auto* blend = find (kPanel + static_cast<int> (ui::PanelSlot::SubBlend)))
                {
                    state.fields[0] = { blend->param, blend->id, n::Uni };
                    state.labels[0] = "BLEND";
                    nativeDetailItems_[0] = blend;
                    blend->c->setHostPainted (true);
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
            if (item == nullptr || item->c == nullptr || item->alwaysOff || cell >= 4 || block >= rows * 2)
                return;
            const int field = n::slot ((block % 2) * 4 + cell, block / 2);
            nativeDetailItems_[field] = item;
            if (item->type == T::Mask)
            {
                nativeDetailMask_ = item->c;
                auto area = nativeDetailRect (field);
                area.w *= 3;
                area.h = nativeDetailMask_->minSize().h;
                nativeDetailMask_->place (area);
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
        else if (const auto* panel = detail_.panel.preset())
        {
            nativeDetailTitle_ = pageTop_ == kPageUnison ? "UNISON" : pageTop_ == kPageGrain ? "GRAIN WINDOW" : "ENVELOPE";
            for (int g = 0; g < detail_.numGroups(); ++g)
            {
                const auto& group = detail_.groups[g];
                if (g < panel->numBlocks)
                    nativeDetailBlocks_[g] = panel->blocks[g].title;
                for (int cell = 0; cell < 4; ++cell)
                    if (const auto* part = group.member (cell))
                    {
                        const auto* partInfo = std::find_if (panel->items, panel->items + panel->numItems,
                            [&] (const ui::PanelItem& p) { return p.slot == panel->q[g].slot[cell]; });
                        add (g, cell, find (part->id()), partInfo != panel->items + panel->numItems ? partInfo->label : nullptr);
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
        if (field >= 0 && field < n::kCells && nativeDetailItems_[field] && nativeDetailItems_[field]->c)
            focusNativeDetail (*nativeDetailItems_[field]->c, v);
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
                if (item.param >= 0 && item.c != nullptr && item.c->rect().contains (x, y)
                    && (item.type == T::Knob || item.type == T::Number || item.type == T::NumberOct || item.type == T::UpDown)
                    && synthyboy::modDestinationChoiceOf (static_cast<size_t> (item.param)) != 0
                    && std::find (nativeSettingsPaint_.begin(), nativeSettingsPaint_.end(), item.c) != nativeSettingsPaint_.end())
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
                plain = pi.min + (item->type == T::Stepper ? (index + delta + length) % length
                                                          : std::clamp (index + delta, 0, length - 1));
                break;
            }
            case n::Semitone:
                plain = static_cast<float> (transposeFrom (octOf (static_cast<int> (std::lround (old))), static_cast<int> (std::lround (22.0f * value)) - 11));
                break;
            case n::Snap: plain = static_cast<float> ((static_cast<int> (std::lround (old)) & 4095) | (value >= 0.5f ? 4096 : 0)); break;
            case n::Number:
            {
                const auto& range = static_cast<ui::NumberField*> (item->c)->state;
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

    const char* nativeSourceLabel() const noexcept { return full_.top.source.stepper.state.text; }

    static const char* nativeSettingsLabel (const Item& item)
    {
        switch (item.type)
        {
            case T::Knob: return static_cast<const ui::Knob*> (item.c)->state.label;
            case T::Number: case T::NumberOct: return static_cast<const ui::NumberField*> (item.c)->state.label;
            case T::Toggle: return static_cast<const ui::ToggleIcon*> (item.c)->state.label;
            case T::Stepper:
                return item.id == kWarp1Type ? "WARP 1"
                                             : item.id == kWarp2Type ? "WARP 2"
                                                                     : static_cast<const ui::Stepper*> (item.c)->state.caption;
            case T::PictoPlain: return "WAVEFORM";
            case T::Picto: return "TYPE";
            case T::Source: case T::Table: return "SOURCE";
            case T::UpDown: return "VOICES";
            case T::Inert: return item.id == kUnisonGear ? "DETAIL" : item.id == kBasic + 2 ? "BROWSE" : "WARP";
            default: return "";
        }
    }

    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return qActive_; }
    int modsTargetParam() const noexcept { return modsTargetParam_; }
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
        using ui::PanelSlot;
        if (item.c == &full_.top.pos)
            return n::AbcPosition;
        switch (item.id)
        {
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
            case T::Number: return static_cast<const ui::NumberField*> (item.c)->state.format == ui::NumberField::Format::Degrees ? n::Phase : n::Number;
            case T::NumberDeg: return n::Phase;
            case T::NumberSem: return n::Semitone;
            case T::Toggle: return n::Toggle;
            case T::SnapBit: return n::Snap;
            case T::Stepper: case T::List:
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

    std::pair<int, int> nativeDetailQ (const ui::Component* member) const noexcept
    {
        if (member == nullptr)
            return { -1, -1 };
        const bool spec = oscspec::isSpecPage (pageTop_);
        const auto* groups = pageTop_ == 0 ? full_.groups : spec ? spec_.groups : detail_.groups;
        const int count = pageTop_ == 0 ? full_.numGroups() : spec ? spec_.numGroups() : detail_.numGroups();
        for (int g = 0; g < count; ++g)
            for (int key = 0; key < ui::QGroup::kSlots; ++key)
                if (groups[g].member (key) == member)
                    return { g, key };
        return { -1, -1 };
    }

    void focusNativeDetail (const ui::Component& part, IInstanceView& v)
    {
        const int group = nativeDetailQ (&part).first;
        if (group < 0)
            return;
        if (pageTop_ == 0)
            full_.setActiveGroup (group);
        else if (oscspec::isSpecPage (pageTop_))
            spec_.setActiveGroup (group);
        else
            detail_.setActiveGroup (group);
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
        if (nativeDetailMask_ != nullptr)
            nativeDetailMask_->draw (sb);
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
        const int active = spec ? spec_.activeGroup() : detail_.activeGroup();
        ui::Rect frame;
        for (int i = 0; i < n::kCells; ++i)
            if (const auto* part = nativeDetailItems_[i])
                if (pageTop_ == kPageRoute || nativeDetailQ (part->c).first == active)
                {
                    const auto rect = part->type == T::Mask ? part->c->rect() : nativeDetailRect (i);
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
    ui::OscInstance uiInstance() const noexcept { return static_cast<ui::OscInstance> (sel_ - 1); }
    ui::OscKind kindOfRaw (int raw) const noexcept { return raw == 4 ? ui::OscKind::Multisample : raw == 2 ? ui::OscKind::Granular : raw == 1 ? ui::OscKind::Sample : raw == 3 ? ui::OscKind::Spectral : ui::OscKind::Wavetable; }

    void bind (std::vector<Item>& list, int id, ui::Component* c, T type, const std::string& suffix, bool alwaysOff = false, const char* second = nullptr)
    {
        Item it;
        it.id = id; it.c = c; it.type = type; it.alwaysOff = alwaysOff; it.full = bindingFull_;
        it.key = suffix.empty() ? std::string() : prefix_ + suffix;
        it.param = indexOfKey (it.key);
        if (second != nullptr)
            it.param2 = indexOfKey (prefix_ + second);
        if (c != nullptr)
            c->setId (id);
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
        const ui::OscKind kind = kindOfRaw (raw);
        const synthyboy::ParamInfo* ti = nullptr;
        const int tp = typeParam (sel);
        if (tp >= 0) ti = &info (tp);
        if (ti != nullptr && ! sub && ! noise && raw >= 0 && raw < ti->numChoices)
            ui::setText (typeName_, sizeof typeName_, ti->choices[raw]);
        else
            typeName_[0] = '\0';
        compact_.setInstance (uiInstance(), kind);
        noiseSample_ = -1;
        if (noise)
        {
            const int ni = indexOfKey ("noise.type");
            noiseSample_ = inst != nullptr && ni >= 0 && inst->param (ni) >= 0.5f ? 1 : 0;
        }
        full_.setInstance (uiInstance(), kind, noiseSample_ == 1);
        {
            static const char* const kTitles[playosc::kInstances] = { "SUB", "OSC A", "OSC B", "OSC C", "NOISE" };
            full_.setPageTitle (pageSel_ > 0 ? kTitles[pageSel_ - 1] : "");
        }
        full_.setActiveGroup (0);
        main_.clear();
        // ---- the compact view
        compact_.instances.setId (kInstances);
        compact_.power.setId (kPower);
        compact_.route.setId (kRoute);
        compact_.full.setId (kFullButton);
        compact_.basic.assignIds (kBasic);
        {
            ui::BasicControls* b = &compact_.basic;
            const bool abc = ! sub && ! noise;
            bind (main_, kInstances, &compact_.instances, T::Inert, "");
            bind (main_, kFullButton, &compact_.full, T::Inert, "");
            bind (main_, kPower, &compact_.power, T::Toggle, "on");
            bind (main_, kRoute, &compact_.route, T::Route, "destination");
            const std::string typeKey = sub ? "waveform" : (noise ? "" : "type");
            if (! noise)
                bind (main_, kBasic + 0, &b->selector, sub ? T::PictoPlain : T::Picto, typeKey);
            if (noise)
                bind (main_, kBasic + 5, &b->noiseType, T::Stepper, "sample");
            if (abc)
                bindSource (&b->source, kind);
            if (sub)
                bind (main_, kBasic + 3, &b->oct, T::Number, "octave");
            if (noise)
                bind (main_, kBasic + 4, &b->filter, T::Knob, "filter");
            bind (main_, kBasic + 6, &b->pan, T::Knob, "pan");
            bind (main_, kBasic + 7, &b->level, T::Knob, "level");
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
        pageLoaded_ = 0;
        pageTop_ = 0;
        modsDirty_ = true;
        qSlots_.clear();
        qSlots_.forget();
        gesture_ = nullptr; gestureDown_ = false;
    }

    /** The source of a granular / multisample / sample / spectral oscillator is content (a file), not a parameter: its name is shown and a tap opens the content browser (the folders of the machine, the WAV files, the SFZ instruments); the
        wavetable's source is the table (the built-in tables and the table files, the wavetable browser). */
    void bindSource (ui::SourcePicker* sp, ui::OscKind kind)
    {
        const bool contentType = kind != ui::OscKind::Wavetable;
        const bool tableType = kind == ui::OscKind::Wavetable;
        bind (main_, kBasic + 1, &sp->stepper, contentType ? T::Source : T::Table, tableType ? "wavetable" : "");
        bind (main_, kBasic + 2, &sp->dropdown, T::Inert, "");
        sp->stepper.listable = true;
        sp->stepper.contentName = true;   // a file name or the title of a table file: the middle goes when it does not fit (content_name.h), the lint accepts that
    }

    /** The full screen's controls and their bindings: the top row (the first Q group), the MORE button and the bottom band. Level, pan and destination are not here (read-only indicator, the Mixer is their home). */
    void bindFull (ui::OscKind kind)
    {
        using ui::PanelSlot;
        const bool sub = sel_ == playosc::kSub, noise = sel_ == playosc::kNoise, abc = ! sub && ! noise;
        bindingFull_ = true;
        ui::OscTopRow& t = full_.top;
        full_.close.setId (kClose);
        full_.power.setId (kPower);
        t.assignIds (kBasic);
        bind (main_, kClose, &full_.close, T::Inert, "");
        bind (main_, kPower, &full_.power, T::Toggle, "on");
        if (abc)
            bind (main_, kTuneMore, &full_.more, T::Inert, "");
        if (! noise)
            bind (main_, kBasic + 0, &t.selector, sub ? T::PictoPlain : T::Picto, sub ? "waveform" : "type");
        if (abc)
        {
            bindSource (&t.source, kind);
            const PanelSlot ps = kind == ui::OscKind::Wavetable ? PanelSlot::WtPos : kind == ui::OscKind::Multisample ? PanelSlot::Timbre : kind == ui::OscKind::Sample ? PanelSlot::SampleScan : kind == ui::OscKind::Spectral ? PanelSlot::SpecPos : PanelSlot::Scan;
            bind (main_, kPanel + static_cast<int> (ps), &t.pos, T::Knob, slotKey (ps));
            bind (main_, kTuneOct, &t.oct, T::NumberOct, "transpose");
        }
        else if (sub)
        {
            bind (main_, kBasic + 3, &t.oct, T::Number, "octave");
            bind (main_, kPanel + static_cast<int> (PanelSlot::SubCrs), full_.panel.part (PanelSlot::SubCrs), T::Knob, "tune");
            bind (main_, kPanel + static_cast<int> (PanelSlot::SubPhase), full_.panel.part (PanelSlot::SubPhase), T::Knob, "phase");
        }
        else
        {
            bind (main_, kPanel + static_cast<int> (PanelSlot::NoiseType), &t.kind, T::Stepper, "type");
            bind (main_, kBasic + 5, &t.sound, T::Stepper, "sample");
            bind (main_, kBasic + 4, full_.panel.part (PanelSlot::Filter), T::Knob, "filter");
            bind (main_, kPanel + static_cast<int> (PanelSlot::NoiseStereo), full_.panel.part (PanelSlot::NoiseStereo), T::Knob, "stereo");
        }
        bindPanel (main_, full_.panel, kPanel, kind, abc);
        if (sub)
            bind (main_, kPanel + static_cast<int> (PanelSlot::SubBlend), &full_.subBlend, T::Knob, "filter_blend");
        bindingFull_ = false;
    }
    /** The full screen again (the noise changed its mode: another bottom band): its items are dropped and bound anew. */
    void rebindFull()
    {
        main_.erase (std::remove_if (main_.begin(), main_.end(), [] (const Item& it) { return it.full; }), main_.end());
        bindFull (kindOfRaw (typeRaw_));
        configureNumberRanges (main_);
    }

    /** The parts of a KnobPanel with their ids (base + slot) and bindings; `abc`: the panel belongs to A, B or C. */
    void bindPanel (std::vector<Item>& list, ui::KnobPanel& panel, int base, ui::OscKind kind, bool abc)
    {
        using ui::PanelSlot;
        const ui::PanelPreset* pp = panel.preset();
        if (pp == nullptr)
            return;
        for (int i = 0; i < pp->numItems; ++i)
        {
            const ui::PanelItem& it = pp->items[i];
            if (bindingFull_ && sel_ == playosc::kSub && (it.slot == PanelSlot::SubCrs || it.slot == PanelSlot::SubPhase))
                continue;
            if (bindingFull_ && sel_ == playosc::kNoise && (it.slot == PanelSlot::Filter || it.slot == PanelSlot::NoiseStereo))
                continue;
            ui::Component* c = panel.part (it.slot);
            if (c == nullptr)
                continue;
            int id = base + static_cast<int> (it.slot);
            const std::string k = slotKey (it.slot);
            switch (it.kind)
            {
                case ui::PartKind::Number:   // FIN, the noise's PITCH and FINE: the ids of the former tuning column (their ranges and formats are set by id in configureNumberRanges)
                    id = it.slot == PanelSlot::NoisePitch ? kTuneCrs : kTuneFin;
                    if (base == kPanel)
                        bind (list, id, c, T::Number, k);
                    break;
                case ui::PartKind::Knob: bind (list, id, c, T::Knob, k); break;
                case ui::PartKind::Stepper: bind (list, id, c, T::Stepper, k); break;
                case ui::PartKind::Toggle: bind (list, id, c, T::Toggle, k); break;
                case ui::PartKind::Menu: bind (list, id, c, T::Inert, k); break;   // the WARP menu of the granular oscillator: a tap steps through the warp types
                case ui::PartKind::Unison:
                {
                    auto* u = static_cast<ui::UnisonBox*> (c);
                    bind (list, kUnisonGear, &u->gear, T::Inert, "");
                    bind (list, kUnisonCount, &u->count, T::UpDown, "unison.voices");
                    break;
                }
                case ui::PartKind::WarpPair:
                {
                    auto* w = static_cast<ui::WarpPair*> (c);
                    w->first.type.listable = w->second.type.listable = true;
                    bind (list, kWarp1Type, &w->first.type, T::Stepper, "warp.type");
                    bind (list, kWarp1Amount, &w->first.amount, T::Knob, "warp.amount");
                    bind (list, kWarp2Type, &w->second.type, T::Stepper, "warp2.type");
                    bind (list, kWarp2Amount, &w->second.amount, T::Knob, "warp2.amount");
                    w->first.setHasVar (false);   // the warp PHASE knobs are behind MORE (bold cut), not on the full screen
                    w->second.setHasVar (false);
                    break;
                }
                case ui::PartKind::RandPair: break;
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
        full_.setInstance (uiInstance(), ui::OscKind::Wavetable, sample);
        rebindFull();
        resolveQ (inst);
    }
    static const char* slotKey (ui::PanelSlot s) noexcept
    {
        using ui::PanelSlot;
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
            if (it.c == nullptr || it.param < 0)
                continue;
            const synthyboy::ParamInfo& pi = info (it.param);
            if (it.type == T::Number)
            {
                auto& st = static_cast<ui::NumberField*> (it.c)->state;
                st.min = pi.min; st.max = pi.max; st.defaultValue = pi.def;
                const bool fine = (it.id == kTuneCrs && sel_ != playosc::kNoise) || it.key == prefix_ + "pitch_coarse";   // CRS: hundredths of a semitone; the noise's PITCH: whole semitones
                st.step = fine ? 0.01 : (pi.max - pi.min > 200.0f ? 10.0 : 1.0);
                if (fine) { st.pxPerStep = 2; st.format = ui::NumberField::Format::Decimal2; }
                else if (it.id == kTuneCrs) { st.pxPerStep = 6; st.format = ui::NumberField::Format::IntSigned; }
                if (it.id == kTuneFin) { st.pxPerStep = 3; st.format = ui::NumberField::Format::IntSigned; }
            }
            else if (it.type == T::NumberDeg)
            {
                auto& st = static_cast<ui::NumberField*> (it.c)->state;
                st.min = 0; st.max = 360; st.step = 1; st.defaultValue = 0;
            }
            else if (it.type == T::NumberOct)
            {
                auto& st = static_cast<ui::NumberField*> (it.c)->state;
                st.min = -4; st.max = 4; st.step = 1; st.defaultValue = 0;
            }
            else if (it.type == T::NumberSem)
            {
                auto& st = static_cast<ui::NumberField*> (it.c)->state;
                st.min = -11; st.max = 11; st.step = 1; st.defaultValue = 0;
            }
            else if (it.type == T::Knob)
            {
                auto* k = static_cast<ui::Knob*> (it.c);
                const bool bipolar = pi.min < 0.0f && pi.max > 0.0f;
                k->state.bipolar = bipolar;
                k->state.defaultValue = synthyboy::toNormalised (pi, pi.def);
            }
            else if (it.type == T::UpDown)
            {
                auto* u = static_cast<ui::UpDownStepper*> (it.c);
                u->state.min = static_cast<int> (pi.min); u->state.max = static_cast<int> (pi.max);
            }
            else if (it.type == T::DetailInt)
            {
                auto* d = static_cast<ui::DetailButton*> (it.c);
                d->state.min = pi.min; d->state.max = pi.max;
            }
        }
    }

    // ======================================================================================================================================================================
    // the pages above the full screen
    void syncPages (const UiStack& stack, IInstanceView& inst)
    {
        const int top = stack.depth() >= 2 ? stack.top() : 0;
        const int sig = top * 16 + (top == kPageMods ? modsTarget_ & 0xff : 0);
        if (top != pageTop_ || sig != pageSig_)
        {
            if (gesture_ != nullptr && pageTop_ != top)
                dropGesture();
            if (top == kPageRoute && pageTop_ != kPageRoute)
            {
                savedGroup_ = full_.activeGroup();
                full_.setActiveGroup (-1);      // the popup's group is the active one: the full screen's frame goes away while it is open
            }
            else if (top != kPageRoute && pageTop_ == kPageRoute && savedGroup_ >= 0)
            {
                full_.setActiveGroup (savedGroup_);
                savedGroup_ = -1;
            }
            pageTop_ = top;
            pageSig_ = sig;
            if (top != kPageList)
                browseMode_ = wtMode_ = false;
            loadPage (inst);
        }
    }
    void loadPage (IInstanceView& inst)
    {
        page_.clear();
        pageLoaded_ = pageTop_;
        const ui::OscKind kind = kindOfRaw (typeRaw_);
        const bool wt = kind == ui::OscKind::Wavetable;
        switch (pageTop_)
        {
            case kPageUnison:
                detail_.load (wt ? ui::presets::kUnisonDetail : (kind == ui::OscKind::Multisample ? ui::presets::kUnisonDetailMultisample : ui::presets::kUnisonDetailGranular), "UNISON");
                bindDetail();
                break;
            case kPageGrain: detail_.load (ui::presets::kGrainDetail, "GRAIN WINDOW"); bindDetail(); break;
            case kPageEnvelope: detail_.load (ui::presets::kEnvelopeDetail, "ENVELOPE"); bindDetail(); break;
            case kPageRoute:
                bind (page_, kRouteTarget, nullptr, T::Stepper, "destination");
                bind (page_, kRouteBalance, nullptr, T::Knob, "filter_blend");
                bind (page_, kRouteBus1, nullptr, T::Knob, "bus1_send");
                bind (page_, kRouteBus2, nullptr, T::Knob, "bus2_send");
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
                else if (listParam_ >= 0)
                {
                    list_.state.header = nullptr;
                    listShown_.clear();   // the names by what the choice does, never the working name of another product (ui/choice_names.h)
                    for (int c = 0; c < info (listParam_).numChoices; ++c)
                        listShown_.push_back (ui::shownChoice (info (listParam_).key, c, info (listParam_).choices[c]));
                    list_.state.items = listShown_.data();
                    list_.state.count = info (listParam_).numChoices;
                    list_.state.current = static_cast<int> (std::lround (inst.param (listParam_)));
                    list_.showCurrent();
                }
                break;
            default: break;
        }
        configureNumberRanges (page_);
        modsDirty_ = true;
        qSlots_.forget();
    }
    /** A detail page of osc_spec_page.h: its controls are bound to the parameters of this oscillator (ids kSpec + index), the list popup of a long list is the name of the stepper. */
    void bindSpec (int page)
    {
        const oscspec::Spec* sp = oscspec::specOf (page);
        if (sp == nullptr)
            return;
        spec_.loadSpec (*sp);
        spec_.back.setId (kDetailBack);
        for (int i = 0; i < sp->numCtls; ++i)
        {
            const oscspec::Ctl& c = sp->ctls[i];
            ui::Component* comp = spec_.control (i);
            T t = T::Knob;
            std::string key = c.key;
            if (c.kind == oscspec::K::Stepper) t = T::Stepper;
            else if (c.kind == oscspec::K::Toggle) t = std::strcmp (c.key, "transpose_snap") == 0 ? T::SnapBit : T::Toggle;
            else if (c.kind == oscspec::K::Number)
            {
                t = T::Number;
                auto& ns = static_cast<ui::NumberField*> (comp)->state;
                if (std::strcmp (c.key, "transpose_sem") == 0) { t = T::NumberSem; key = "transpose"; }   // the semitones of the transpose (OCT of the default view is its octaves)
                else if (std::strcmp (c.key, "phase") == 0) { ns.format = ui::NumberField::Format::Degrees; ns.icon = ui::NumberField::Icon::Phase; }
                else if (std::strcmp (c.key, "random_phase") == 0) ns.icon = ui::NumberField::Icon::Rand;
            }
            else if (c.kind == oscspec::K::List) t = T::List;
            else if (c.kind == oscspec::K::Mask) t = T::Mask;
            else if (c.kind == oscspec::K::Page) { t = T::Inert; key.clear(); }
            bind (page_, kSpec + i, comp, t, key);
            if (c.kind == oscspec::K::Stepper)
            {
                const int pi = indexOfKey (prefix_ + c.key);
                static_cast<ui::Stepper*> (comp)->listable = pi >= 0 && info (pi).numChoices > 6;
            }
            else if (c.kind == oscspec::K::List)
            {
                const int pi = indexOfKey (prefix_ + c.key);
                if (pi >= 0 && info (pi).choices != nullptr)
                {
                    const int n = std::min (static_cast<int> (info (pi).numChoices), static_cast<int> (ui::LedList::kMaxOptions));
                    const char* shown[ui::LedList::kMaxOptions];
                    for (int o = 0; o < n; ++o)
                        shown[o] = ui::shownChoice (info (pi).key, o, info (pi).choices[o]);
                    spec_.setListOptions (i, shown, n);
                    if (const char* lab = ui::shownLabel (info (pi).key))
                        spec_.setListTitle (i, lab);
                }
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
    void bindDetail()
    {
        detail_.back.setId (kDetailBack);
        bindPanel (page_, detail_.panel, kDetail, kindOfRaw (typeRaw_), true);
        // the detail page's ids are the panel slots' + kDetail (bindPanel gave base + slot)
        detail_.setActiveGroup (0);
    }
    // ---- the modulations page
    uibind::ModRow modRows_[ui::ModulationsPage::kPerPage];
    NativeModTargets nativeModulations_;
    int modsPageIndex_ = 0, modsSource_ = 0, modsType_ = -1;
    uint64_t modsPreset_ = 0;
    std::vector<const char*> listShown_;   // the names of the list page (shown names)
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

    void loadMods (const IInstanceView& inst, int focusSlot = -1)
    {
        const auto window = uibind::modulationWindow (inst, modsTargetParam_, selectedSource_, modsPageIndex_, modRows_, focusSlot);
        modsPageIndex_ = window.page;
        if (window.total == 0)
            uibind::fillSourceBoard (mods_.board);
        mods_.load (window.entries, modsTitle_, window.total, window.page);
        if (window.focusRow >= 0)
            mods_.selectRow (window.focusRow);
        mods_.back.setId (kModsBack);
        mods_.page.setId (kModsPage);
        for (int r = 0; r < ui::ModulationsPage::kPerPage; ++r)
            mods_.setRowIds (r, kModsRow + 4 * r, kModsRow + 4 * r + 1, kModsRow + 4 * r + 2, kModsRow + 4 * r + 3);
        mods_.addSource.setId (kModsAddStep);
        mods_.add.setId (kModsAdd);
        // what the "add source" row offers: the selected source first, the other generator's selection, then the first LFOs, envelopes and macros
        int offer[8], n = 0;
        auto push = [&] (int s) { if (s == ui::kNoSource || n >= 8) return; for (int i = 0; i < n; ++i) if (offer[i] == s) return; offer[n++] = s; };
        push (selectedSource_); push (envSource (envSel_)); push (lfoSource (lfoSel_));
        for (int s : { 7, 8, 1, 2, 9, 3, 19, 20 }) push (s);
        for (int i = 0; i < n; ++i)
            mods_.sourceIds[i] = offer[i];
        mods_.addSource.state.count = n;
        mods_.addSource.state.index = std::clamp (mods_.addSource.state.index, 0, std::max (0, n - 1));
        setSourceText (mods_.addSource, offer[std::clamp (mods_.addSource.state.index, 0, std::max (0, n - 1))]);
        modsAddOffer_ = n;
        setTargetText (inst);
        // the rows' controls are bound for the Q group (the parameters of the slot of the row)
    }
    /** The value of the control the modulations page is about, beside its title. */
    void setTargetText (const IInstanceView& v)
    {
        char t[16] = "";
        if (modsTargetParam_ >= 0)
            formatValue (info (modsTargetParam_), v.param (modsTargetParam_), t, sizeof t);
        mods_.setTargetValue (t);
    }
    int modsTargetParam_ = -1, modsAddOffer_ = 0, savedGroup_ = -1;
    char modsTitle_[32] = "MODULATIONS";
    static const char* sourceName (int choice) noexcept { return uibind::sourceName (choice); }   // a macro answers with its name (macro_names.h)
    static void setSourceText (ui::Stepper& s, int choice) { s.setItem (sourceName (choice)); }
    void refreshMods (const IInstanceView& v)
    {
        auto one = [&] (std::vector<Item>& list)
        {
            for (Item& it : list)
            {
                if (it.c == nullptr || it.param < 0 || (it.type != T::Knob && it.type != T::Number))
                    continue;
                const ui::ModInput mi = uibind::modsOf (v, it.param, selectedSource_, nullptr);
                ui::ModInput& dst = it.type == T::Knob ? static_cast<ui::ModInput&> (static_cast<ui::Knob*> (it.c)->state) : static_cast<ui::ModInput&> (static_cast<ui::NumberField*> (it.c)->state);
                dst = mi;
            }
        };
        one (main_);
        one (page_);
        if (pageTop_ == kPageMods && modsTargetParam_ >= 0)
            loadMods (v);
    }

    // ======================================================================================================================================================================
    // reading: the states of the components from the parameters
    void pull (IInstanceView& v, std::vector<Item>& list)
    {
        for (Item& it : list)
        {
            if (it.c == nullptr)
                continue;
            if (it.alwaysOff)
            {
                it.c->setEnabled (false);
                if (it.type == T::Stepper && it.param >= 0)
                    showStepper (static_cast<ui::Stepper*> (it.c), it.param, v.param (it.param));
                else if (it.type == T::Stepper)
                {
                    auto* st = static_cast<ui::Stepper*> (it.c);
                    st->state.count = 0; st->setItem ("-");
                }
                continue;
            }
            if (it.type == T::Inert)
            {
                it.c->setEnabled (true);
                if (it.id == kInstances || it.id == kClose)
                    it.c->setInactive (false);
                if (it.id == kPanel + static_cast<int> (ui::PanelSlot::WarpMenu) && it.param >= 0)   // the WARP menu of the granular oscillator says which warp it has now
                {
                    const synthyboy::ParamInfo& wp = info (it.param);
                    const int wi = std::clamp (static_cast<int> (std::lround (v.param (it.param) - wp.min)), 0, std::max (0, wp.numChoices - 1));
                    static_cast<ui::MenuButton*> (it.c)->setValue (wp.choices != nullptr ? wp.choices[wi] : "");
                }
                continue;
            }
            if (it.type == T::Table && it.param >= 0)
            {
                showTable (static_cast<ui::Stepper*> (it.c), it, v.param (it.param), v);
                it.c->setEnabled (true);
                continue;
            }
            if (it.type == T::Source)
            {
                auto* st = static_cast<ui::Stepper*> (it.c);
                st->state.count = 0;
                st->state.index = 0;
                st->state.wrap = false;
                sourceText (v, st);
                it.c->setEnabled (true);
                continue;
            }
            if (it.param < 0)
            {
                it.c->setEnabled (false);
                continue;
            }
            const float plain = v.param (it.param);
            const synthyboy::ParamInfo& pi = info (it.param);
            switch (it.type)
            {
                case T::Knob:
                {
                    auto* k = static_cast<ui::Knob*> (it.c);
                    k->state.value = k->state.modValue = normOf (it.param, plain);
                    const bool isPan = it.c == &compact_.basic.pan, isLevel = it.c == &compact_.basic.level;
                    const bool isFilter = it.c == &compact_.basic.filter || it.c == &full_.top.filter;
                    if (isPan)
                    {
                        const int whole = static_cast<int> (std::lround (plain));
                        std::snprintf (k->state.valueText, sizeof k->state.valueText, whole == 0 ? "C" : (whole < 0 ? "L %d" : "R %d"), std::abs (whole));
                    }
                    else if (isLevel)
                        std::snprintf (k->state.valueText, sizeof k->state.valueText, "%.1f", static_cast<double> (plain));
                    else if (isFilter)
                        std::snprintf (k->state.valueText, sizeof k->state.valueText, "%.0f", static_cast<double> (plain));
                    else if (it.id == kPanel + static_cast<int> (ui::PanelSlot::SubPhase))
                    {
                        const double deg = std::round (static_cast<double> (plain) * 3.6);
                        std::snprintf (k->state.valueText, sizeof k->state.valueText, "%.0f\xc2\xb0", deg);
                    }
                    else
                        formatValue (pi, plain, k->state.valueText, sizeof k->state.valueText);   // every other knob says its value too (with the unit), under its name
                    break;
                }
                case T::Number:
                    static_cast<ui::NumberField*> (it.c)->state.value = plain;
                    break;
                case T::NumberOct: static_cast<ui::NumberField*> (it.c)->state.value = octOf (static_cast<int> (std::lround (plain))); break;
                case T::NumberSem: static_cast<ui::NumberField*> (it.c)->state.value = semOf (static_cast<int> (std::lround (plain))); break;
                case T::NumberDeg: static_cast<ui::NumberField*> (it.c)->state.value = std::round (static_cast<double> (plain) * 3.6); break;
                case T::Stepper:
                {
                    auto* st = static_cast<ui::Stepper*> (it.c);
                    showStepper (st, it.param, plain);
                    if (it.id == kWarp1Type || it.id == kWarp2Type)
                        warpOff (it.id == kWarp1Type ? full_.panel : full_.panel, it.id, st->state.index == 0);
                    break;
                }
                case T::Toggle: static_cast<ui::ToggleIcon*> (it.c)->state.on = plain >= 0.5f; break;
                case T::SnapBit: static_cast<ui::ToggleIcon*> (it.c)->state.on = (static_cast<int> (std::lround (plain)) & 4096) != 0; break;
                case T::Mask: static_cast<ui::NoteMask*> (it.c)->state.mask = static_cast<int> (std::lround (plain)) & 4095; break;
                case T::List: static_cast<ui::LedList*> (it.c)->state.index = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, static_cast<ui::LedList*> (it.c)->state.count - 1)); break;
                case T::UpDown:
                {
                    auto* u = static_cast<ui::UpDownStepper*> (it.c);
                    u->state.value = static_cast<int> (std::lround (plain));
                    break;
                }
                case T::Picto:
                {
                    const int raw = static_cast<int> (std::lround (plain));
                    static_cast<ui::PictogramSelector*> (it.c)->state.index = raw == 0 ? 0 : raw == 4 ? 1 : raw == 1 ? 2 : raw == 2 ? 3 : raw == 3 ? 4 : -1;
                    break;
                }
                case T::PictoPlain: static_cast<ui::PictogramSelector*> (it.c)->state.index = static_cast<int> (std::lround (plain)); break;
                case T::DetailNorm:
                {
                    auto* d = static_cast<ui::DetailButton*> (it.c);
                    d->state.hasValue = true;
                    d->state.value = normOf (it.param, plain);
                    char t[7];
                    std::snprintf (t, sizeof t, "%d", static_cast<int> (std::lround (plain)));
                    d->setValueText (t);
                    break;
                }
                case T::DetailInt:
                    static_cast<ui::DetailButton*> (it.c)->state.value = std::lround (plain);
                    break;
                case T::Route:
                {
                    const int d = static_cast<int> (std::lround (plain));
                    if (it.id == kRoute)
                    {
                        auto* rb = static_cast<ui::RouteButton*> (it.c);
                        static const char* const kTarget[7] = { "F1", "F2", "F12", "M", "DIR", "BAL", "OFF" };
                        rb->setTarget (kTarget[std::clamp (d, 0, 6)]);
                        rb->state.segment[0] = d == 0 || d == 2 || d == 5;
                        rb->state.segment[1] = d == 1 || d == 2 || d == 5;
                    }
                    break;
                }
                default: break;
            }
            (void) pi;
            // a control never gets a state when its parameter is read only by nothing: enabled stays as the component's own state (a typeOff warp slot is handled by WarpSlot)
            it.c->setEnabled (true);
            if (it.type == T::Knob && (it.id == kWarp1Amount || it.id == kWarp2Amount))
                it.c->setEnabled (! warpIsOff (it.id));
            if (it.id == kRoute && sel_ == playosc::kSub)
                it.c->setEnabled (false);   // the sub oscillator has no route button (its routing is the ROUTING block of its page): the hidden button takes no touch and no Q-Link
        }
    }
    /** The read-only indicator of the full screen: the destination, the level and the pan of the module (their home is the Mixer). */
    void updateMix (const IInstanceView& v)
    {
        const int di = indexOfKey (prefix_ + "destination"), li = indexOfKey (prefix_ + "level"), pi = indexOfKey (prefix_ + "pan");
        static const char* const kTarget[7] = { "F1", "F2", "F12", "M", "DIR", "BAL", "OFF" };
        const int d = di >= 0 ? std::clamp (static_cast<int> (std::lround (v.param (di))), 0, 6) : 0;
        routeFilter_ = d == 5;
        const int whole = pi >= 0 ? static_cast<int> (std::lround (v.param (pi))) : 0;
        char pan[12], info[28];
        std::snprintf (pan, sizeof pan, whole == 0 ? "C" : (whole < 0 ? "L %d" : "R %d"), std::abs (whole));
        std::snprintf (info, sizeof info, "%.1f dB  %s", li >= 0 ? static_cast<double> (v.param (li)) : 0.0, pan);
        full_.mix.setDest (kTarget[d]);
        full_.mix.setInfo (info);
        full_.mix.state.segment[0] = d == 0 || d == 2 || d == 5;
        full_.mix.state.segment[1] = d == 1 || d == 2 || d == 5;
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
        auto knobOf = [&] (ui::PanelSlot slot) { const ui::Knob* k = detail_.panel.knob (slot); return k != nullptr ? static_cast<float> (std::clamp (k->state.value, 0.0, 1.0)) : 0.0f; };
        const float d = knobOf (ui::PanelSlot::Delay), a = knobOf (ui::PanelSlot::Attack), h = knobOf (ui::PanelSlot::Hold), dc = knobOf (ui::PanelSlot::Decay), su = knobOf (ui::PanelSlot::Sustain), r = knobOf (ui::PanelSlot::Release);
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
    void sourceText (const IInstanceView& v, ui::Stepper* st) const
    {
        const int osc = playosc::oscNumber (sel_);
        std::string msg;
        const int state = v.contentLoad (&msg);
        if (state == 1) { st->setItem ("LOADING ..."); return; }
        if (state == 3) { st->setItem ("LOAD FAILED"); return; }
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
        st->setItem (name.empty() ? "NO SAMPLE" : name.c_str());
    }
    bool warpOff_[2] = { false, false };
    bool routeFilter_ = false;
    bool warpIsOff (int id) const noexcept { return warpOff_[id == kWarp1Amount ? 0 : 1]; }
    void warpOff (ui::KnobPanel& panel, int typeId, bool off)
    {
        warpOff_[typeId == kWarp1Type ? 0 : 1] = off;
        if (ui::WarpPair* w = panel.warp())
            (typeId == kWarp1Type ? w->first : w->second).setTypeOff (off);
        full_.setWarpApplies (typeId == kWarp1Type ? 0 : 1, ! off);   // hide what does not apply: the amount and the PHASE of a warp that is Off are not drawn and are no Q members
    }
    static void showStepper (ui::Stepper* st, int param, float plain)
    {
        const synthyboy::ParamInfo& pi = info (param);
        const int n = pi.numChoices > 0 ? pi.numChoices : static_cast<int> (pi.max - pi.min) + 1;
        const int idx = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, n - 1));
        st->state.count = n;
        st->state.index = idx;
        st->state.wrap = pi.numChoices <= 12;
        if (pi.choices != nullptr && idx < pi.numChoices)
            st->setItem (ui::shownChoice (pi.key, idx, pi.choices[idx]));
        else
        {
            char t[13];
            std::snprintf (t, sizeof t, "%d", idx);
            st->setItem (t);
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
    ui::Component* topComponent() noexcept
    {
        if (stackDepth_ == 0)
            return srcPopup_ ? static_cast<ui::Component*> (&list_) : &compact_;
        if (oscspec::isSpecPage (pageTop_))
            return &spec_;
        if (pageTop_ == kPageUnison || pageTop_ == kPageGrain || pageTop_ == kPageEnvelope)
            return &detail_;
        if (pageTop_ == kPageMods)
            return &mods_;
        if (pageTop_ == kPageRoute)
            return nullptr;
        if (pageTop_ == kPageList)
            return &list_;
        return &full_;
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        // ---- the list of the source selector of the compact view (a popup over the third, not a page of the stack)
        if (srcPopup_ && stackDepth_ == 0)
        {
            if (id == kList && c.kind == K::Index)
            {
                if (popupIsType_)   // the type list: the same Index change the cells made (the owner's handler below writes the type)
                {
                    const int chosen = c.index;
                    closeSourceList();
                    handle (ui::Change::indexed (kBasic, chosen, chosen), stack, v);
                    return;
                }
                v.setPlaySelection (0, c.index + 1);
                closeSourceList();
            }
            else if (id == kListBackdrop && c.kind == K::Request)
                closeSourceList();
            return;
        }
        // ---- navigation
        if (c.kind == K::Request)
        {
            switch (id)
            {
                case kInstances: openSourceList (v); return;
                case kBasic + 0: openTypeList(); return;   // the type drop-down of the compact view (the full screen's cells never send it)
                case kFullButton: stack.openFull (kElementOsc); return;
                case kModsBack:
                    if (mods_.pickingSource())
                        mods_.setPickingSource (false);
                    else
                        stack.back();
                    return;
                case kModsAdd:
                    if (modsContextValid (v))
                    {
                        uibind::fillSourceBoard (mods_.board);
                        mods_.setPickingSource (true);
                    }
                    return;
                case kClose: case kDetailBack: stack.back(); return;
                case kTuneMore:
                    if (sel_ != playosc::kSub && sel_ != playosc::kNoise)
                        stack.openDetail (oscspec::kMore);
                    return;
                case kRoute:
                    if (stack.depth() == 0)
                        stack.openFull (kElementOsc);   // the route popup lies over the full screen: from the compact view that opens first
                    stack.openDetail (kPageRoute);
                    return;
                case kListBackdrop: stack.back(); return;
                case kListHeader:
                    if (wtMode_ && wtBrowser_.up() == WavetableBrowser::Action::Changed)
                        fillWavetableList();
                    else if (browseMode_ && browser_.tap (0) == ContentBrowser::Action::Changed)
                        fillBrowserList();
                    return;
                case kBasic + 1: case kBasic + 2:
                {
                    const Item* src = item (kBasic + 1);
                    if (src != nullptr && src->type == T::Source) openBrowser (stack, v);
                    else if (src != nullptr && src->type == T::Table) openWavetableBrowser (stack, v);
                    else openList (src, stack);
                    return;
                }
                case kWarp1Type: case kWarp2Type: openList (item (id), stack); return;
                default: break;
            }
            if (id == kUnisonGear) { stack.openDetail (kPageUnison); return; }
            const Item* it = item (id);
            if (it != nullptr && id >= kSpec && id < kSpec + oscspec::OscSpecPage::kMaxCtl)
            {
                // a button of a detail page that opens another page (the page buttons of MORE), else the list of a choice parameter
                const oscspec::Spec* sp = spec_.spec();
                const int ci = id - kSpec;
                if (sp != nullptr && ci < sp->numCtls && sp->ctls[ci].kind == oscspec::K::Page)
                {
                    const int pg = pageOfToken (sp->ctls[ci].key);
                    if (pg > 0)
                        stack.openDetail (pg);
                    return;
                }
                openList (it, stack);
                return;
            }
            if (id == kPanel + static_cast<int> (ui::PanelSlot::WarpMenu) && it != nullptr && it->param >= 0)
            {
                openList (it, stack);
                return;
            }
            if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kPerPage && (id - kModsRow) % 4 == 3)
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
                mods_.setPickingSource (false);
                const auto* name = target != nullptr && target->c != nullptr && target->type == T::Knob
                    ? static_cast<ui::Knob*> (target->c)->state.label
                    : info (targetParam).name;
                ui::setText (modsTitle_, sizeof modsTitle_, name);
            }
            return;
        }
        if (c.kind == K::AddRequest)
        {
            addModulation (v, c.index);
            return;
        }
        if (id == kList && c.kind == K::Index)
        {
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
            if (listParam_ >= 0)
                writePlain (v, listParam_, info (listParam_).min + c.index);
            stack.back();
            return;
        }
        // ---- the modulation rows
        if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kPerPage)
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
        if (id == kModsAddStep && c.kind == K::Index)
        {
            const int i = std::clamp (c.index, 0, std::max (0, modsAddOffer_ - 1));
            mods_.addSource.state.index = i;
            setSourceText (mods_.addSource, mods_.sourceIds[i]);
            return;
        }
        Item* it = find (id);
        if (it == nullptr || it->c == nullptr || it->param < 0)
            return;
        if (c.kind == K::Depth)
        {
            const int slot = uibind::slotOf (v, it->param, c.index);
            if (slot >= 0)
                writeNorm (v, synthyboy::modSlotParams (slot).amount, (c.value + 1.0) * 0.5);
            modsDirty_ = true;
            return;
        }
        write (*it, c, v);
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
        const bool off = ! compact_.power.state.on;
        using DialStatus = ui::Knob::State::Status;
        const DialStatus dialStatus = off ? DialStatus::Off : DialStatus::Active;
        compact_.basic.pan.state.status = compact_.basic.level.state.status = dialStatus;
        compact_.setInactive (off);
        // Keep the composite itself enabled so its POWER child remains reachable.
        compact_.instances.setEnabled (true); compact_.basic.setEnabled (! off); compact_.route.setEnabled (! off); compact_.full.setEnabled (! off);
        compact_.power.setEnabled (true);
        compact_.instances.setInactive (false); compact_.power.setInactive (false); compact_.full.setInactive (off); compact_.route.setInactive (off);
        compact_.basic.group.setInactive (off);
        full_.setInactive (off);
        if (pageSel_ > 0 && pageSel_ <= playosc::kInstances)
        {
            static const char* const kTitles[playosc::kInstances][2] = {
                { "SUB  OFF", "SUB" },
                { "OSC A  OFF", "OSC A" },
                { "OSC B  OFF", "OSC B" },
                { "OSC C  OFF", "OSC C" },
                { "NOISE  OFF", "NOISE" }
            };
            full_.setPageTitle (kTitles[pageSel_ - 1][! off]);
        }
        // The page composite stays enabled for navigation and POWER hit-testing.
        full_.close.setEnabled (true); full_.top.setEnabled (! off); full_.mix.setEnabled (true); full_.more.setEnabled (! off); full_.subBlend.setEnabled (! off); full_.panel.setEnabled (! off);
        full_.power.setEnabled (true);
        full_.close.setInactive (false); full_.power.setInactive (false); full_.more.setInactive (off); full_.subBlend.setInactive (off);
        for (ui::QGroup& g : full_.groups)
            g.setInactive (off);
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
    const std::string& missingText() const noexcept { return missingText_; }
private:
    /** The line over the display (in the header band of the compact view, else at the top of the display rectangle of the state on screen), in the warning colour; a name that does not fit loses its middle. */
    void drawMissing (gpu::SceneBuilder& sb) const
    {
        const ui::Rect r = displayRect();
        if (missingText_.empty() || r.empty())
            return;
        const ui::theme::Look& T = ui::theme::L();
        // the compact view: the band of the display between the power button and the route / full screen button; the other states: between the corners of the display
        const ui::Rect box = stackDepth_ == 0 ? compact_.labelRect() : ui::Rect { r.x + 4 + 64 + 8, r.y + 8, std::max (0, r.w - 2 * (4 + 64 + 8)), 48 };
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
        loadBtn_.setLabel (loadLabel());
        loadBtn_.place (r.empty() ? ui::Rect {} : ui::Rect { r.x + r.w / 2 - 150, r.y + r.h * 2 / 3 - 8, 300, 64 });
        loadBtn_.setId (kSpecLoad);
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
    void showTable (ui::Stepper* st, const Item& it, float plain, const IInstanceView& v) const
    {
        const synthyboy::ParamInfo& pi = info (it.param);
        const int choice = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, pi.numChoices - 1));
        std::string pack, file, title;
        const bool have = contentTable (v, pack, file, title);
        showStepper (st, it.param, plain);
        if (choice != synthyboy::kWavetablePresetTable || ! have)
            return;
        st->setItem (title.c_str());
        if (! stepValid_)
        {
            st->state.count = 3; st->state.index = 1; st->state.wrap = false;   // the catalogue is read at the first step: both arrows are lit until then
            return;
        }
        int entry = -1, index = 0, count = 0;
        const int category = stepCat_.findFile (pack, file, entry);   // where the file the content names stands in the catalogue we have
        if (category >= 0 && synthyboy::filePosition (stepCat_, category, entry, index, count))
        {
            // the position among the files of the category as three places: first (no previous), in the middle, last (no next); a category of one file has no arrow
            st->state.wrap = false;
            st->state.count = count > 1 ? 3 : 1;
            st->state.index = count > 1 ? (index <= 0 ? 0 : (index >= count - 1 ? 2 : 1)) : 0;
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
    void writeTable (const Item& it, const ui::Change& c, IInstanceView& v)
    {
        if (c.kind != ui::ChangeKind::Index || it.c == nullptr)
            return;
        const int delta = c.index - static_cast<ui::Stepper*> (it.c)->state.index;
        if (delta != 0)
            stepTable (v, it, delta, c.index);
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
        listParam_ = -1;
        if (stack.depth() == 0)
            stack.openFull (kElementOsc);
        stack.openDetail (kPageList);
        fillWavetableList();
    }
    void fillWavetableList()
    {
        // the header is the list's own bar (the way up and the title), the rows are the categories or the tables
        list_.state.header = wtBrowser_.title().c_str();
        list_.state.headerBack = ! wtBrowser_.atCategories();
        list_.state.items = wtBrowser_.items();
        list_.state.icons = nullptr;
        list_.state.count = wtBrowser_.count();
        list_.state.current = wtBrowser_.currentRow();
        list_.showCurrent();
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
        listParam_ = -1;
        if (stack.depth() == 0)
            stack.openFull (kElementOsc);
        stack.openDetail (kPageList);
        fillBrowserList();
    }
    void fillBrowserList()
    {
        // the header (the way up and the path) is the list's own bar, the rows are the ones below the first
        browserTitle_ = browser_.title();
        list_.state.header = browserTitle_.c_str();
        list_.state.headerBack = ! browser_.atRoots();
        list_.state.items = browser_.items() + 1;
        list_.state.count = std::max (0, browser_.count() - 1);
        const int cur = browser_.rowOf (lastChosen_[browser_.kind() - 1]);
        list_.state.current = cur > 0 ? cur - 1 : -1;
        list_.showCurrent();
    }

    void updateSourceRows (const IInstanceView& v)
    {
        static const int power[] = { indexOfKey ("sub.on"), indexOfKey ("osc1.on"), indexOfKey ("osc2.on"), indexOfKey ("osc3.on"), indexOfKey ("noise.on") };
        static const char* const labels[][2] = { { "Sub   OFF", "Sub   ON" }, { "Osc A   OFF", "Osc A   ON" },
            { "Osc B   OFF", "Osc B   ON" }, { "Osc C   OFF", "Osc C   ON" }, { "Noise   OFF", "Noise   ON" } };
        for (int i = 0; i < playosc::kInstances; ++i)
            sourceRows_[i] = labels[i][power[i] >= 0 && v.param (power[i]) >= 0.5f];
        list_.state.current = sel_ - 1;
    }

    /** Open the five oscillator rows under the Play header. */
    void openSourceList (const IInstanceView& v)
    {
        updateSourceRows (v);
        const ui::Rect third { kCompactX, kTabY + 4, kCompactW, kTabH - 8 };
        list_.state.header = nullptr;
        list_.state.headerBack = false;
        list_.state.items = sourceRows_;
        list_.state.icons = nullptr;
        popupIsType_ = false;
        list_.state.count = playosc::kInstances;
        list_.state.current = sel_ - 1;
        list_.place ({ third.x + 6, third.y + 6 + ui::playrows::kSelectorH, third.w - 12, playosc::kInstances * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (third);
        list_.showCurrent();
        srcPopup_ = true;
    }
    /** The list of the type drop-down of the compact view: the items of the selector (WAVE .. SPEC, or the six waveforms of the sub) with their pictograms, rows of 56 px, a popup over the third. */
    void openTypeList()
    {
        const ui::TypeSelector& sel = compact_.basic.selector;
        typeNames_.clear(); typeIcons_.clear();
        for (int i = 0; i < sel.state.count; ++i)
        {
            typeNames_.push_back (sel.state.items[i].label);
            typeIcons_.push_back (sel.state.items[i].pictogram);
        }
        const ui::Rect third { kCompactX, kTabY + 4, kCompactW, kTabH - 8 };
        list_.state.header = nullptr;
        list_.state.headerBack = false;
        list_.state.items = typeNames_.data();
        list_.state.icons = typeIcons_.data();
        list_.state.count = sel.state.count;
        list_.state.current = sel.state.index;
        list_.place ({ third.x + 6, third.y + 6 + ui::playrows::kSelectorH, third.w - 12, std::min (sel.state.count, 8) * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (third);
        list_.showCurrent();
        popupIsType_ = true;
        srcPopup_ = true;
    }
    std::vector<const char*> typeNames_;
    std::vector<int> typeIcons_;
    bool popupIsType_ = false;
    void closeSourceList()
    {
        srcPopup_ = false;
        popupIsType_ = false;
        list_.state.icons = nullptr;
        list_.state.current = -1;
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup ({ 0, kTabY, w_, kTabH });
    }

    /** The list popup of a choice parameter (the owner's item: its parameter): over the full screen. */
    void openList (const Item* it, UiStack& stack)
    {
        if (it == nullptr || it->param < 0 || it->alwaysOff || info (it->param).choices == nullptr || info (it->param).numChoices < 2)
            return;
        listParam_ = it->param;
        stack.openDetail (kPageList);
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
            mods_.setPickingSource (false);
            loadMods (v, slot);
        }
    }

    void write (Item& it, const ui::Change& c, IInstanceView& v)
    {
        using K = ui::ChangeKind;
        const int p = it.param;
        switch (it.type)
        {
            case T::Knob: if (c.kind == K::Value) writeNorm (v, p, c.value); break;
            case T::Number:
                if (c.kind == K::Index || c.kind == K::Value)
                    writePlain (v, p, c.kind == K::Index ? c.value : c.value);
                break;
            case T::NumberDeg: if (c.kind == K::Index) writePlain (v, p, c.value / 3.6); break;
            case T::NumberOct:
                if (c.kind == K::Index)
                    writePlain (v, p, replaceOctave (static_cast<int> (std::lround (v.param (p))), static_cast<int> (std::lround (c.value))));
                break;
            case T::NumberSem:
                if (c.kind == K::Index)
                    writePlain (v, p, transposeFrom (octOf (static_cast<int> (std::lround (v.param (p)))), static_cast<int> (std::lround (c.value))));
                break;
            case T::Stepper:
                if (c.kind == K::Index)
                {
                    const synthyboy::ParamInfo& pi = info (p);
                    const int n = pi.numChoices > 0 ? pi.numChoices : static_cast<int> (pi.max - pi.min) + 1;
                    writePlain (v, p, pi.min + (n > 0 ? (c.index % n + n) % n : 0));
                }
                break;
            case T::Table: writeTable (it, c, v); break;
            case T::Toggle: if (c.kind == K::Index) writePlain (v, p, c.index != 0 ? 1.0 : 0.0); break;
            case T::SnapBit: if (c.kind == K::Index) writePlain (v, p, (static_cast<int> (std::lround (v.param (p))) & 4095) | (c.index != 0 ? 4096 : 0)); break;
            case T::Mask: if (c.kind == K::Index) writePlain (v, p, (static_cast<int> (std::lround (v.param (p))) & 4096) | (c.index & 4095)); break;
            case T::List: if (c.kind == K::Index) writePlain (v, p, info (p).min + std::clamp (c.index, 0, std::max (0, info (p).numChoices - 1))); break;
            case T::UpDown: if (c.kind == K::Index) writePlain (v, p, c.index); break;
            case T::Picto:
                if (c.kind == K::Index)
                {
                    writePlain (v, p, nativeui::osc::kTypeChoices[std::clamp (c.index, 0, 4)]);
                }
                break;
            case T::PictoPlain: if (c.kind == K::Index) writePlain (v, p, c.index); break;
            case T::DetailNorm: if (c.kind == K::Value) writeNorm (v, p, c.value); break;
            case T::DetailInt: if (c.kind == K::Index || c.kind == K::Value) writePlain (v, p, c.kind == K::Index ? c.index : c.value); break;
            case T::Route: if (c.kind == K::Index) writePlain (v, p, std::clamp (c.index, 0, 6)); break;
            default: break;
        }
    }

    // ======================================================================================================================================================================
    // the Q group: the four targets the hardware knobs are mapped to
    void resolveQ (IInstanceView&) {}
    /** The table index behind a member of a Q group (a composite: its primary child); -1 when there is none. */
    /** The four targets of the Basic group of the compact view (table indices, -1 none) and its block of knobs (the Play view maps the groups of the three thirds together). */
public:   // the Play view reads the targets of the compact group and sets its block
    void compactTargets (int* t) const noexcept { for (int i = 0; i < 4; ++i) t[i] = paramOfMember (compact_.basic.group.member (i)); }
    void setCompactBlock (int block) noexcept { compact_.basic.group.setBlock (block); }
private:
    int paramOfMember (const ui::Component* m) const noexcept
    {
        while (m != nullptr)
        {
            for (const Item& it : page_) if (it.c == m) return paramOfItem (it);
            for (const Item& it : main_) if (it.c == m) return paramOfItem (it);
            m = m->primaryChild();
        }
        return -1;
    }
    int paramOfItem (const Item& it) const noexcept
    {
        if (! compact_.power.state.on)
        {
            const Item* power = item (kPower);
            if (! power || it.param != power->param)
                return -1;
        }
        if (it.alwaysOff || (it.type == T::Inert && it.id != kPanel + static_cast<int> (ui::PanelSlot::WarpMenu)))
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
        auto members = [this] (const ui::QGroup& g, int* t) { for (int i = 0; i < 4; ++i) t[i] = paramOfMember (g.member (i)); };
        if (stackDepth_ == 0)
        {
            name = "Basic";
            compact_.basic.group.setActive (highlight_ == 1);
            qActive_ = 0;
            return;   // the main state: the Play view publishes the three groups of the page together (PlayView::publishQ), the oscillator's is the first
        }
        else if (pageTop_ == kPageUnison || pageTop_ == kPageGrain || pageTop_ == kPageEnvelope)
        {
            name = "Detail";
            qSlots_.publish (v, detail_.numGroups(), detail_.activeGroup(), [&] (int g, int* t) { members (detail_.groups[g], t); });
            ui::QGroup* gs = detail_.groups;
            for (int g = 0; g < ui::DetailPage::kMaxGroups; ++g)
                gs[g].setBlock (g < detail_.numGroups() ? QRouter::blockOfGroup (detail_.numGroups(), detail_.activeGroup(), g) : -1);
        }
        else if (oscspec::isSpecPage (pageTop_))
        {
            name = "Page";
            qSlots_.publish (v, spec_.numGroups(), spec_.activeGroup(), [&] (int g, int* t) { members (spec_.groups[g], t); });
            for (int g = 0; g < oscspec::OscSpecPage::kMaxBlocks; ++g)
                spec_.groups[g].setBlock (g < spec_.numGroups() ? QRouter::blockOfGroup (spec_.numGroups(), spec_.activeGroup(), g) : -1);
        }
        else if (pageTop_ == kPageMods)
        {
            name = "Mods";
            qSlots_.publish (v, compact_.power.state.on ? mods_.numRows() : 0, mods_.activeRow(), [this] (int row, int* t) { modRowTargets (row < ui::ModulationsPage::kPerPage ? modRows_[row].slot : -1, t); });
            setRowBlocks (mods_, ui::ModulationsPage::kPerPage, mods_.numRows(), mods_.activeRow());
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
        else if (full_.activeGroup() < 0 && tuneFocus_ >= 0 && full_.hasNativeTune() && isNativeRoutingActive())
        {
            name = "Tune";
            qSlots_.publish (v, 2, tuneFocus_, [this] (int g, int* t) {
                if (g == 0)
                    std::copy_n (tuneTargets_.begin(), 4, t);
                else
                    t[0] = tuneTargets_[4];
            });
            for (auto& group : full_.groups)
                group.setBlock (-1);
            qActive_ = tuneFocus_;
        }
        else
        {
            const int mix = full_.numGroups();
            const bool hasMix = isNativeRoutingActive();
            const int count = mix + (hasMix ? 1 : 0);
            const int active = hasMix && full_.activeGroup() < 0 ? mix : full_.activeGroup();
            name = hasMix && active == mix ? "Mix" : full_.groupName (active);
            qSlots_.publish (v, count, active, [&] (int g, int* t) {
                if (g < mix)
                    members (full_.groups[g], t);
                else
                {
                    t[0] = paramOfMember (&compact_.basic.pan);
                    t[1] = paramOfMember (&compact_.basic.level);
                    t[2] = paramOfMember (&compact_.route);
                }
            });
            for (int g = 0; g < ui::OscillatorFullScreen::kMaxGroups; ++g)
                full_.groups[g].setBlock (g < mix ? QRouter::blockOfGroup (count, active, g) : -1);
            qActive_ = active;
        }
        if (pageTop_ != 0)
            qActive_ = 0;
        ui::setText (qName_, sizeof qName_, name);
    }

    // ======================================================================================================================================================================
    ui::OscillatorCompact compact_;
    CompactNativeCells compactNativeCells_ {};
    std::array<ui::Component*, nativeui::playoscui::kFields> compactNativePaint_ {};
    ui::OscillatorFullScreen full_;
    ui::DetailPage detail_;
    ui::ModulationsPage mods_;
    ui::ListPopup list_;
    const char* sourceRows_[playosc::kInstances] = {};
    bool srcPopup_ = false;   // the list of the source selector of the compact view is open (main state only)
    bool nativeSelector_ = false;
    ui::OscillatorFullScreen::NativeCells nativeSettingsCells_ {};
    std::array<ui::Component*, nativeui::osc::kSettingsAll> nativeSettingsPaint_ {};
    nativeui::oscdetail::Frame nativeDetail_ = nativeui::oscdetail::Hidden;
    nativeui::oscdetail::State nativeDetailState_;
    std::array<Item*, nativeui::oscdetail::kCells> nativeDetailItems_ {};
    std::array<const char*, oscspec::OscSpecPage::kMaxBlocks> nativeDetailBlocks_ {};
    const char* nativeDetailTitle_ = "";
    ui::Component* nativeDetailMask_ = nullptr;
    bool nativeRouting_ = false;
    oscspec::OscSpecPage spec_;
    ui::MenuButton loadBtn_ { "LOAD SAMPLE", ui::MenuButton::Icon::None };
    ui::Rect marksRect_;
    bool loadShown_ = false, specVisible_ = false;
    ContentBrowser browser_;
    std::string browserTitle_;
    bool browseMode_ = false;
    WavetableBrowser wtBrowser_;
    bool wtMode_ = false;                       // the list popup is the wavetable browser
    // the stepping through the files of one category (the arrows of the table stepper, the Q knob): the catalogue as it was when the file table was loaded or the browser opened, and where the table is in it
    synthyboy::WavetableCatalogue stepCat_;
    std::string stepPack_, stepFile_, wtLastPack_, wtLastFile_;
    int stepCategory_ = -1, stepEntry_ = -1;
    bool stepValid_ = false;
    std::string lastChosen_[2];   // the file that was loaded last for the granular (0) and multisample (1) type: the browser opens in its folder
    int listParam_ = -1;
    std::vector<Item> main_, page_;
    bool bindingFull_ = false;   // bind () marks the items of the full screen
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false, configured_ = false, modsDirty_ = true;
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
