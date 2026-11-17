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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/ui.h"
#include "../ui/choice_names.h"
#include "content_browser.h"
#include "osc_spec_page.h"
#include "curve_source.h"
#include "../q_router.h"
#include "play_osc.h"
#include "q_publish.h"
#include "ui_bind.h"
#include "touch_decoder.h"
#include "ui_stack.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"

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
        kBasic = 100,                       // BasicControls::assignIds: selector, source stepper, dropdown, OCT, FILTER, noise type, PAN, LEVEL = 100 .. 107
        kTuneOct = 110, kTuneSem, kTuneFin, kTuneCrs, kTuneTrack, kTuneMore,
        kSpecLoad = 790, kSpec = 800,       // the LOAD button of an empty sample type; 800 + i: the controls of a detail page of osc_spec_page.h
        kBarSlot = 120,                     // 120 .. 123: the slots of the type bar
        kBarToggle = 130,                   // 130 .. 133: the toggle of a toggle + number slot
        kBarAmount = 140,                   // 140 .. 143: its amount
        kPanel = 200,                       // 200 + PanelSlot: the parts of the full screen's bottom band
        kWarp1Type = 300, kWarp1Amount, kWarp1Var, kWarp2Type, kWarp2Amount, kWarp2Var, kUnisonGear = 310, kUnisonCount,
        kDetailBack = 399, kDetail = 400,   // 400 + PanelSlot: the parts of a detail page
        kRouteTarget = 500, kRouteBalance, kRouteBus1, kRouteBus2, kRouteClose, kRouteBackdrop,
        kList = 700, kListBackdrop, kListHeader,   // the list popup (source, warp types, the content browser: its header is the way up)
        kModsBack = 600, kModsRow = 610,    // 610 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kModsAddStep = 660, kModsAdd
    };

    /** What a value is and how it converts to the engine's plain value. */
    enum class T : uint8_t
    {
        Knob,        // normalised 0 .. 1 of the parameter
        Number,      // a NumberField in the units of the parameter
        NumberOct,   // transpose: OCT = steps of 12 semitones
        NumberSem,   // transpose: SEM = the remainder
        NumberDeg,   // a percent parameter shown as degrees (0 .. 100 % = 0 .. 360)
        NumberRead,  // shown only (the frame number)
        Stepper,     // a choice parameter
        StepperLoop, // the granular loop mode: One-shot / Loop grains / Manual from gran.loop_grains and gran.manual
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
        Inert        // no value: a button
    };

    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        std::string key;           // the key of the engine parameter ("" = none: the control is disabled)
        int param = -1;            // its table index (-1: none)
        int param2 = -1;           // the second parameter (StepperLoop: gran.manual)
        bool alwaysOff = false;    // drawn disabled whatever the engine says (nothing behind it)
        int row = -1;              // a control of the modulations page: the row
    };

    OscUi() { setCanvas (kCanvasW, kCanvasH); configure (nullptr, 2, true); }

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
        popupBox_ = { w / 2 - 340, kTabY + kTabH / 2 - 110, 680, 220 };
        route_.place (popupBox_);
        route_.setPopup (true, tab);
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
    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs, int selectedEnv, int selectedLfo)
    {
        highlight_ = highlight;
        const int sel = pageSel_ > 0 ? pageSel_ : std::clamp (inst.playSelection (0), 1, playosc::kInstances);
        const int typeIdx = typeParam (sel);
        const int raw = typeIdx >= 0 ? static_cast<int> (std::lround (inst.param (typeIdx))) : 0;
        if (sel != sel_ || raw != typeRaw_ || ! configured_)
            configure (&inst, sel, false, raw);
        stackDepth_ = stack.depth();
        if (stackDepth_ > 0 && srcPopup_)
            closeSourceList();
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
        pull (inst, main_);
        pull (inst, page_);
        applyInactive();
        updateSpecVisibility (inst);
        updateLoadButton (inst);
        if (pageTop_ == kPageEnvelope)
            updateEnvelope();
        if (pageTop_ == kPageList && listParam_ >= 0)
            list_.state.current = static_cast<int> (std::lround (inst.param (listParam_)));
        inst.setUiFullOpen (! stack.isMain());
        publishQGroup (inst);
    }

    /** This UI is a PAGE of the OSC tab (osc_page.h): the tab names the oscillator (`sel`: 1 Sub, 2 A, 3 B, 4 C, 5 Noise), so the selection is not the Play page's selector, the full screen has no CLOSE button (its place
        carries the name) and nothing writes the selector. 0 (the default): the Play page's UI. */
    void setPage (int sel) noexcept { pageSel_ = sel >= 1 && sel <= playosc::kInstances ? sel : 0; configured_ = false; }
    bool isPage() const noexcept { return pageSel_ > 0; }

    /** The rectangle (canvas coordinates) the picture of the oscillator goes into now; empty when the current page shows none. */
    ui::Rect displayRect() const noexcept
    {
        if (stackDepth_ == 0)
            return compact_.displayRect();
        if (stackDepth_ == 1 || pageTop_ == kPageRoute || pageTop_ == kPageList)
            return full_.displayRect();
        if (oscspec::isSpecPage (pageTop_))
            return spec_.displayRect();
        if (pageTop_ == kPageUnison || pageTop_ == kPageGrain)
            return detail_.displayRect();   // the same picture of the oscillator over the page (the fan of the unison voices, the grain window on the sample)
        return {};
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

    void draw (gpu::SceneBuilder& sb) const
    {
        drawPage (sb);
        drawPopup (sb);
    }
    /** The page without its popups (the picture of the oscillator goes on it, then the popups). */
    void drawPage (gpu::SceneBuilder& sb) const
    {
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
        full_.draw (sb);
    }
    /** The envelope of the multisample oscillator as a curve in the display area of its page (the schematic of the six values: delay, attack, hold, decay, sustain level, release). */
    void drawEnvelope (gpu::SceneBuilder& sb) const
    {
        const ui::Rect r = detail_.displayRect();
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
        if (pageTop_ == kPageRoute)
            route_.draw (sb);
        else if (pageTop_ == kPageList)
            list_.draw (sb);
    }
    /** What goes on top of the picture in the display rectangle (the full screen button of the compact view). */
    void drawOnDisplay (gpu::SceneBuilder& sb) const
    {
        drawMissing (sb);
        if (stackDepth_ == 0)
        {
            compact_.full.draw (sb);
            compact_.power.draw (sb);   // the buttons that stand in the corners of the display lie over its picture
        }
        else if (loadShown_)
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
        return stackDepth_ > 0 ? ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y) : ui::Rect { kCompactX, kTabY + 4, kCompactW, kTabH - 8 }.contains (x, y);
    }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** One touch event in UI pixels (the whole screen), `stack` is the instance's stack (the caller holds its lock). Returns true when the touch belongs to the plugin-drawn pages. */
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        ui::Component* top = topComponent();
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            if (! covers (ev.x, ev.y) || ! top)
                return false;
            if (loadShown_ && ! srcPopup_ && loadBtn_.rect().contains (x, y))
            {
                loadBtn_.touchDown (x, y, nowMs);
                gesture_ = &loadBtn_;
                gestureDown_ = true;
                return true;
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
    /** The stack changed behind the UI's back (the page left, another instance): the gesture is dropped. */
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    /** Another element's page opened or closed: the Q group is published again at the next update (the router keeps what the last page set). */
    void forgetQGroup() noexcept { qSlots_.forget(); }

    // ---- access for the tests
    ui::OscillatorCompact& compact() noexcept { return compact_; }
    ui::OscillatorFullScreen& full() noexcept { return full_; }
    ui::DetailPage& detail() noexcept { return detail_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    ui::RoutePanel& routePanel() noexcept { return route_; }
    ui::ListPopup& list() noexcept { return list_; }
    const std::vector<Item>& items() const noexcept { return main_; }
    const std::vector<Item>& pageItems() const noexcept { return page_; }
    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return qActive_; }
    /** Finds the item (main or page) of an id; null when there is none. */
    const Item* item (int id) const noexcept { for (const Item& it : main_) if (it.id == id) return &it; for (const Item& it : page_) if (it.id == id) return &it; return nullptr; }

    // ---- the modulation source ids of the matrix (modSourceChoice: env n = n, lfo n = 6 + n for n <= 8, lfo 9 = 33, lfo 10 = 34)
    static int envSource (int n) noexcept { return n >= 1 && n <= 6 ? n : ui::kNoSource; }
    static int lfoSource (int n) noexcept { return n >= 1 && n <= 8 ? 6 + n : (n == 9 ? 33 : (n == 10 ? 34 : ui::kNoSource)); }
    static constexpr double kModsRefreshMs = 100.0;

    /** Converts a value of the OCT / SEM number fields into the transpose in semitones (the other field keeps its part). */
    static int transposeFrom (int oct, int sem) noexcept { return std::clamp (oct * 12 + sem, -48, 48); }
    static int octOf (int transpose) noexcept { return transpose / 12; }
    static int semOf (int transpose) noexcept { return transpose - 12 * (transpose / 12); }

private:
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
        it.id = id; it.c = c; it.type = type; it.alwaysOff = alwaysOff;
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
        const bool otherType = false;   // every type has its own full screen now (the Sample and Spectral types too)
        const synthyboy::ParamInfo* ti = nullptr;
        const int tp = typeParam (sel);
        if (tp >= 0) ti = &info (tp);
        if (ti != nullptr && ! sub && ! noise && raw >= 0 && raw < ti->numChoices)
            ui::setText (typeName_, sizeof typeName_, ti->choices[raw]);
        else
            typeName_[0] = '\0';
        compact_.setInstance (uiInstance(), kind);
        full_.setInstance (uiInstance(), kind);
        if (noise && inst != nullptr)
        {
            const int ti = indexOfKey ("noise.type");
            const bool sample = ti >= 0 && inst->param (ti) >= 0.5f;
            noiseSample_ = sample ? 1 : 0;
            full_.panel.load (sample ? ui::presets::kNoiseSampleFull : ui::presets::kNoiseGenFull);
            full_.rebuildGroups();
        }
        else if (noise)
        {
            noiseSample_ = 0;
            full_.panel.load (ui::presets::kNoiseGenFull);
            full_.rebuildGroups();
        }
        else
            noiseSample_ = -1;
        {
            static const char* const kTitles[playosc::kInstances] = { "SUB", "OSC A", "OSC B", "OSC C", "NOISE" };
            full_.setPageTitle (pageSel_ > 0 ? kTitles[pageSel_ - 1] : "");
        }
        if (otherType)
            full_.setUnsupportedType();
        full_.setActiveGroup (0);
        main_.clear();
        const bool abc = ! sub && ! noise;
        // ---- the compact view
        compact_.instances.setId (kInstances);
        compact_.power.setId (kPower);
        compact_.route.setId (kRoute);
        compact_.full.setId (kFullButton);
        compact_.basic.assignIds (kBasic);
        // ---- the full screen
        full_.close.setId (kClose);
        full_.power.setId (kPower);
        full_.route.setId (kRoute);
        full_.basic.assignIds (kBasic);
        ui::BasicControls* bc[2] = { &compact_.basic, &full_.basic };
        bind (main_, kInstances, &compact_.instances, T::Inert, "");
        bind (main_, kFullButton, &compact_.full, T::Inert, "");
        bind (main_, kClose, &full_.close, T::Inert, "");
        bind (main_, kPower, &compact_.power, T::Toggle, "on");
        bind (main_, kPower, &full_.power, T::Toggle, "on");
        bind (main_, kRoute, &compact_.route, T::Route, "destination");
        bind (main_, kRoute, &full_.route, T::Route, "destination");
        for (ui::BasicControls* b : bc)
        {
            const std::string typeKey = sub ? "waveform" : (noise ? "" : "type");
            if (! noise)
                bind (main_, kBasic + 0, &b->selector, sub ? T::PictoPlain : T::Picto, typeKey);
            if (noise)
                bind (main_, kBasic + 5, &b->noiseType, T::Stepper, "sample");
            if (abc)
            {
                // the source of a granular / multisample oscillator is content (a file), not a parameter: its name is shown and a tap opens the content browser (the folders of the machine, the WAV files, the SFZ instruments)
                const bool contentType = kind != ui::OscKind::Wavetable && ! otherType;
                bind (main_, kBasic + 1, &b->source.stepper, contentType ? T::Source : T::Stepper, kind == ui::OscKind::Wavetable && ! otherType ? "wavetable" : "", ! contentType && (kind != ui::OscKind::Wavetable || otherType));
                const bool listOk = (b == &full_.basic && kind == ui::OscKind::Wavetable && ! otherType) || contentType;   // wavetable: the list of all sources, on the full screen; the content types: the browser, from both views
                bind (main_, kBasic + 2, &b->source.dropdown, T::Inert, "", ! listOk);
                b->source.stepper.listable = listOk;
                b->source.stepper.contentName = contentType;   // a file name: the middle goes when it does not fit (content_name.h), the lint accepts that
            }
            if (sub)
                bind (main_, kBasic + 3, &b->oct, T::Number, "octave");
            if (noise)
                bind (main_, kBasic + 4, &b->filter, T::Knob, "filter");
            bind (main_, kBasic + 6, &b->pan, T::Knob, "pan");
            bind (main_, kBasic + 7, &b->level, T::Knob, "level");
        }
        // ---- the tuning column
        full_.tune.oct.setId (kTuneOct); full_.tune.sem.setId (kTuneSem); full_.tune.fin.setId (kTuneFin); full_.tune.crs.setId (kTuneCrs); full_.tune.track.setId (kTuneTrack);
        if (abc)
        {
            bind (main_, kTuneOct, &full_.tune.oct, T::NumberOct, "transpose");
            bind (main_, kTuneSem, &full_.tune.sem, T::NumberSem, "transpose");
            bind (main_, kTuneFin, &full_.tune.fin, T::Number, "tune");
            bind (main_, kTuneCrs, &full_.tune.crs, T::Number, "pitch_coarse");
            bind (main_, kTuneTrack, &full_.tune.track, T::Toggle, "pitch_track");
            bind (main_, kTuneMore, &full_.tune.more, T::Inert, "");
        }
        else if (noise)
        {
            bind (main_, kTuneCrs, &full_.tune.crs, T::Number, "pitch");           // PITCH, FINE and the key tracking of the noise
            bind (main_, kTuneFin, &full_.tune.fin, T::Number, "fine");
            bind (main_, kTuneTrack, &full_.tune.track, T::Toggle, "keytrack");
        }
        // ---- the type bar
        for (int i = 0; i < full_.bar.numSlots(); ++i)
        {
            ui::Component* s = full_.bar.slot (i);
            const ui::BarItem& bi = full_.bar.preset()->items[i];
            switch (bi.kind)
            {
                case ui::BarKind::Frame: bind (main_, kBarSlot + i, s, T::NumberRead, "frame"); break;
                case ui::BarKind::Phase: bind (main_, kBarSlot + i, s, sub ? T::NumberDeg : T::Number, "phase"); break;
                case ui::BarKind::Rand: bind (main_, kBarSlot + i, s, T::Number, "random_phase"); break;
                case ui::BarKind::Stepper:
                    if (kind == ui::OscKind::Granular)
                        bind (main_, kBarSlot + i, s, T::StepperLoop, "gran.loop_grains", false, "gran.manual");
                    else
                        bind (main_, kBarSlot + i, s, T::Inert, "");
                    break;
                case ui::BarKind::Page: bind (main_, kBarSlot + i, s, T::Inert, ""); break;
                case ui::BarKind::Number: bind (main_, kBarSlot + i, s, T::Number, noise ? "stereo" : "sample.loop_xfade"); break;
                case ui::BarKind::GrainWindow: bind (main_, kBarSlot + i, s, T::DetailNorm, "gran.window_amount"); break;
                case ui::BarKind::UnisonDetail: bind (main_, kBarSlot + i, s, T::DetailInt, "unison.voices"); break;
                case ui::BarKind::Envelope: bind (main_, kBarSlot + i, s, T::Inert, ""); break;
                case ui::BarKind::ToggleNumber:
                {
                    auto* tn = static_cast<ui::ToggleNumber*> (s);
                    bind (main_, kBarToggle + i, &tn->toggle, T::Toggle, "multi.vel_override");
                    bind (main_, kBarAmount + i, &tn->amount, T::Number, "multi.vel_track");
                    break;
                }
                case ui::BarKind::Toggle: bind (main_, kBarSlot + i, s, T::Toggle, noise ? "env1" : ""); break;
            }
        }
        // ---- the bottom band of the full screen
        bindPanel (main_, full_.panel, kPanel, kind, abc);
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
        (void) otherType;
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
            ui::Component* c = panel.part (it.slot);
            if (c == nullptr)
                continue;
            const int id = base + static_cast<int> (it.slot);
            const std::string k = slotKey (it.slot);
            switch (it.kind)
            {
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
                    bind (list, kWarp1Var, &w->first.var, T::Knob, "warp.phase");
                    bind (list, kWarp2Type, &w->second.type, T::Stepper, "warp2.type");
                    bind (list, kWarp2Amount, &w->second.amount, T::Knob, "warp2.amount");
                    bind (list, kWarp2Var, &w->second.var, T::Knob, "warp2.phase");
                    w->first.var.setLabel ("PHASE");
                    w->second.var.setLabel ("PHASE");
                    w->first.setHasVar (true);
                    w->second.setHasVar (true);
                    break;
                }
                case ui::PartKind::RandPair: break;
            }
        }
        (void) kind; (void) abc;
    }
    void rebindMainPanel()
    {
        main_.erase (std::remove_if (main_.begin(), main_.end(), [] (const Item& it) {
            return it.id >= kPanel && it.id < kPanel + static_cast<int> (ui::PanelSlot::Count);
        }), main_.end());
        bindPanel (main_, full_.panel, kPanel, ui::OscKind::Wavetable, false);
        configureNumberRanges (main_);
    }
    void updateNoiseType (bool sample, IInstanceView& inst)
    {
        const int sInt = sample ? 1 : 0;
        if (noiseSample_ == sInt)
            return;
        noiseSample_ = sInt;
        full_.panel.load (sample ? ui::presets::kNoiseSampleFull : ui::presets::kNoiseGenFull);
        full_.rebuildGroups();
        rebindMainPanel();
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
            if (it.type == T::Number || it.type == T::NumberRead)
            {
                auto& st = static_cast<ui::NumberField*> (it.c)->state;
                st.min = pi.min; st.max = pi.max; st.defaultValue = pi.def;
                const bool fine = it.id == kTuneCrs && sel_ != playosc::kNoise;   // CRS: hundredths of a semitone; the noise's PITCH: whole semitones
                st.step = fine ? 0.01 : (pi.max - pi.min > 200.0f ? 10.0 : 1.0);
                if (fine) { st.pxPerStep = 2; st.format = ui::NumberField::Format::Decimal2; }
                else if (it.id == kTuneCrs) { st.pxPerStep = 6; st.format = ui::NumberField::Format::IntSigned; }
                if (it.id == kTuneFin) { st.pxPerStep = 3; st.format = ui::NumberField::Format::IntSigned; }
                if (it.type == T::NumberRead) st.readOnly = true;
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
                browseMode_ = false;
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
                bind (page_, kRouteTarget, &route_.target, T::Route, "destination");
                bind (page_, kRouteBalance, &route_.balance, T::Knob, "filter_blend");
                bind (page_, kRouteBus1, &route_.bus1, T::Knob, "bus1_send");
                bind (page_, kRouteBus2, &route_.bus2, T::Knob, "bus2_send");
                route_.close.setId (kRouteClose);
                route_.setBackdropId (kRouteBackdrop);
                route_.group.setActive (true);
                break;
            case kPageMods: loadMods (inst); break;
            case oscspec::kWtMap: case oscspec::kMore: case oscspec::kSampleRegion: case oscspec::kSampleSlice: case oscspec::kGrain: case oscspec::kSpecFilter: case oscspec::kSpecScan:
                bindSpec (pageTop_);
                break;
            case kPageList:
                if (browseMode_)
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
            if (c.kind == oscspec::K::Stepper) t = T::Stepper;
            else if (c.kind == oscspec::K::Toggle) t = std::strcmp (c.key, "transpose_snap") == 0 ? T::SnapBit : T::Toggle;
            else if (c.kind == oscspec::K::Number) t = T::Number;
            else if (c.kind == oscspec::K::List) t = T::List;
            else if (c.kind == oscspec::K::Mask) t = T::Mask;
            bind (page_, kSpec + i, comp, t, c.key);
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
    struct ModRow { int slot = -1; int source = 0; };
    ModRow modRows_[ui::ModulationsPage::kMaxRows];
    std::vector<const char*> listShown_;   // the names of the list page (shown names)
    int modsTarget_ = 0;     // the id of the control whose modulations the page shows
    void loadMods (const IInstanceView& inst)
    {
        const Item* t = item (modsTarget_);
        ui::ModInput mi;
        char title[13] = "MODULATIONS";
        if (t != nullptr && t->param >= 0)
        {
            std::snprintf (title, sizeof title, "%.12s", t->c != nullptr && t->type == T::Knob ? static_cast<ui::Knob*> (t->c)->state.label : "MODS");
            mi = modsOf (inst, t->param, selectedSource_, modRows_);
        }
        else
            for (ModRow& r : modRows_) r = {};
        if (mi.numMods == 0)   // nothing modulates the control yet: the page is the board of every source (ui/mod_board.h), a tap on a chip makes the modulation (the same as the other pages)
            uibind::fillSourceBoard (mods_.board);
        mods_.load (mi, title);
        mods_.back.setId (kModsBack);
        for (int r = 0; r < ui::ModulationsPage::kMaxRows; ++r)
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
        modsTargetParam_ = t != nullptr ? t->param : -1;
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
    static const char* sourceName (int choice) noexcept { return uibind::sourceName (choice); }   // a macro answers with its name (macro_names.h)
    static void setSourceText (ui::Stepper& s, int choice) { s.setItem (sourceName (choice)); }
    /** The modulations of the parameter `param`: a scan of the 64 slots of the matrix (the slots whose destination is this parameter). `rows` (may be null) gets the slot of every entry. */
    static ui::ModInput modsOf (const IInstanceView& v, int param, int selected, ModRow* rows)
    {
        ui::ModInput mi;
        mi.selectedSource = selected;
        const size_t dest = synthyboy::modDestinationChoiceOf (static_cast<size_t> (param));
        if (dest == 0)
            return mi;
        int n = 0;
        for (int s = 0; s < synthyboy::kNumModSlots && n < ui::ModInput::kMaxMods; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source < 0 || sp.destination < 0 || sp.amount < 0)
                continue;
            const int src = static_cast<int> (std::lround (v.param (sp.source)));
            if (src <= 0 || static_cast<size_t> (std::lround (v.param (sp.destination))) != dest)
                continue;
            const double depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            const bool bi = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            const bool by = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            char name[7];
            uibind::shortName (src, name);   // the short name of the source (a macro: the short form of its name)
            mi.add (src, name, depth, bi, by, uibind::isMacroSource (src));
            if (rows != nullptr)
                rows[n] = { s, src };
            ++n;
        }
        return mi;
    }
    /** The slot of the matrix that modulates `param` from `source` (-1: none). */
    static int slotOf (const IInstanceView& v, int param, int source) noexcept
    {
        ModRow rows[ui::ModInput::kMaxMods];
        const ui::ModInput mi = modsOf (v, param, ui::kNoSource, rows);
        for (int i = 0; i < mi.numMods; ++i)
            if (mi.mods[i].source == source)
                return rows[i].slot;
        return -1;
    }
    static int freeSlot (const IInstanceView& v) noexcept
    {
        for (int s = 0; s < synthyboy::kNumModSlots; ++s)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
            if (sp.source >= 0 && std::lround (v.param (sp.source)) == 0)
                return s;
        }
        return -1;
    }

    void refreshMods (const IInstanceView& v)
    {
        auto one = [&] (std::vector<Item>& list)
        {
            for (Item& it : list)
            {
                if (it.c == nullptr || it.param < 0 || (it.type != T::Knob && it.type != T::Number && it.type != T::NumberRead))
                    continue;
                const ui::ModInput mi = modsOf (v, it.param, selectedSource_, nullptr);
                ui::ModInput& dst = it.type == T::Knob ? static_cast<ui::ModInput&> (static_cast<ui::Knob*> (it.c)->state) : static_cast<ui::ModInput&> (static_cast<ui::NumberField*> (it.c)->state);
                dst = mi;
            }
        };
        one (main_);
        one (page_);
        if (pageTop_ == kPageMods && modsTargetParam_ >= 0)
        {
            const ui::ModInput mi = modsOf (v, modsTargetParam_, selectedSource_, modRows_);
            setTargetText (v);
            if (mi.numMods != mods_.numRows())
            {
                loadMods (v);
                return;
            }
            for (int i = 0; i < mi.numMods; ++i)
            {
                ui::ModulationsPage::Row& r = mods_.rows[i];
                r.depth.state.value = r.depth.state.modValue = (mi.mods[i].depth + 1.0) * 0.5;
                ui::ModulationsPage::setRowState (r, mi.mods[i].bidirectional, mi.mods[i].bypassed);
            }
        }
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
                if (it.id == kPanel + static_cast<int> (ui::PanelSlot::WarpMenu) && it.param >= 0)   // the WARP menu of the granular oscillator says which warp it has now
                {
                    const synthyboy::ParamInfo& wp = info (it.param);
                    const int wi = std::clamp (static_cast<int> (std::lround (v.param (it.param) - wp.min)), 0, std::max (0, wp.numChoices - 1));
                    static_cast<ui::MenuButton*> (it.c)->setValue (wp.choices != nullptr ? wp.choices[wi] : "");
                }
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
                    const bool isPan = it.c == &compact_.basic.pan || it.c == &full_.basic.pan, isLevel = it.c == &compact_.basic.level || it.c == &full_.basic.level;
                    const bool isFilter = it.c == &compact_.basic.filter || it.c == &full_.basic.filter;
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
                case T::Number: case T::NumberRead:
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
                case T::StepperLoop:
                {
                    auto* st = static_cast<ui::Stepper*> (it.c);
                    const bool manual = it.param2 >= 0 && v.param (it.param2) >= 0.5f;
                    const int idx = manual ? 2 : (plain >= 0.5f ? 1 : 0);
                    static const char* const kNames[3] = { "One-shot", "Loop grains", "Manual" };
                    st->state.count = 3; st->state.index = idx; st->state.wrap = true; st->setItem (kNames[idx]);
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
                    else
                    {
                        showStepper (&route_.target, it.param, plain);
                        route_.setTargetIsFilter (d == 5);
                        routeFilter_ = d == 5;
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
            if (it.id == kRouteBalance)
                it.c->setEnabled (routeFilter_);
            if (it.id == kRoute && sel_ == playosc::kSub)
                it.c->setEnabled (false);   // the sub oscillator has no route button (its routing is the ROUTING block of its page): the hidden button takes no touch and no Q-Link
        }
        // the PHASE knob of a warp slot is the warp's phase: dimmed with the slot while its type is off
        for (Item& it : list)
            if (it.id == kWarp1Var || it.id == kWarp2Var)
                it.c->setEnabled (! warpIsOff (it.id == kWarp1Var ? kWarp1Amount : kWarp2Amount));

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
            return &route_;
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
                case kInstances: openSourceList(); return;
                case kFullButton: stack.openFull (kElementOsc); return;
                case kClose: case kDetailBack: case kModsBack: case kRouteClose: case kRouteBackdrop: stack.back(); return;
                case kTuneMore: stack.openDetail (oscspec::kMore); return;
                case kRoute:
                    if (stack.depth() == 0)
                        stack.openFull (kElementOsc);   // the route popup lies over the full screen: from the compact view that opens first
                    stack.openDetail (kPageRoute);
                    return;
                case kListBackdrop: stack.back(); return;
                case kListHeader:
                    if (browseMode_ && browser_.tap (0) == ContentBrowser::Action::Changed)
                        fillBrowserList();
                    return;
                case kBasic + 1: case kBasic + 2:
                {
                    const Item* src = item (kBasic + 1);
                    if (src != nullptr && src->type == T::Source) openBrowser (stack, v);
                    else openList (src, stack);
                    return;
                }
                case kWarp1Type: case kWarp2Type: openList (item (id), stack); return;
                default: break;
            }
            if (id == kUnisonGear) { stack.openDetail (kPageUnison); return; }
            const Item* it = item (id);
            if (it != nullptr && id >= kBarSlot && id < kBarSlot + 4)
            {
                const ui::BarItem& bi = full_.bar.preset()->items[id - kBarSlot];
                if (bi.kind == ui::BarKind::Envelope) { stack.openDetail (kPageEnvelope); return; }
                if (bi.kind == ui::BarKind::GrainWindow) { stack.openDetail (kPageGrain); return; }
                if (bi.kind == ui::BarKind::UnisonDetail) { stack.openDetail (kPageUnison); return; }
                if (bi.kind == ui::BarKind::Page) { const int pg = pageOfBar (bi.label); if (pg > 0) stack.openDetail (pg); return; }
            }
            if (it != nullptr && id >= kSpec && id < kSpec + oscspec::OscSpecPage::kMaxCtl) { openList (it, stack); return; }
            if (id == kPanel + static_cast<int> (ui::PanelSlot::WarpMenu) && it != nullptr && it->param >= 0)
            {
                openList (it, stack);
                return;
            }
            if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kMaxRows && (id - kModsRow) % 4 == 3)
            {
                const int slot = modRows_[(id - kModsRow) / 4].slot;
                if (slot >= 0)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
                    writePlain (v, sp.source, 0); writePlain (v, sp.destination, 0); writePlain (v, sp.amount, 0);
                    loadMods (v);
                }
                return;
            }
            return;
        }
        if (c.kind == K::ModsRequest)
        {
            modsTarget_ = id;
            stack.openDetail (kPageMods);
            return;
        }
        if (c.kind == K::AddRequest)
        {
            addModulation (v, c.index);
            return;
        }
        if (id == kList && c.kind == K::Index)
        {
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
        // ---- the instance selector of the compact view
        if (id == kInstances && c.kind == K::Index)
        {
            v.setPlaySelection (0, c.index + 1);
            return;
        }
        // ---- the modulation rows
        if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kMaxRows)
        {
            const int slot = modRows_[(id - kModsRow) / 4].slot;
            if (slot < 0)
                return;
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
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
            const int slot = slotOf (v, it->param, c.index);
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
    /** The INACTIVE look of a switched-off oscillator (docs/ui-components/theme.md, `ui::Pen::inactiveGrey`): the controls under the display, the blocks and the Q frames are drawn grey, the display picture is grey too (OscDisplay);
        the power button is red (`negativeOff`) and the selector and the buttons of the display keep their colours. Nothing is disabled: the controls stay operable, a patch can be prepared on a switched-off oscillator (as before). */
    void applyInactive()
    {
        const bool off = ! compact_.power.state.on;
        compact_.setInactive (off);
        compact_.instances.setInactive (false); compact_.power.setInactive (false); compact_.full.setInactive (false); compact_.route.setInactive (false);
        compact_.basic.group.setInactive (off);
        full_.setInactive (off);
        full_.close.setInactive (false); full_.power.setInactive (false); full_.route.setInactive (false);
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
    const std::string& missingText() const noexcept { return missingText_; }
    /** The line over the display (in the display rectangle of the state on screen), in the warning colour; a name that does not fit loses its middle. */
    void drawMissing (gpu::SceneBuilder& sb) const
    {
        const ui::Rect r = displayRect();
        if (missingText_.empty() || r.empty())
            return;
        const ui::theme::Look& T = ui::theme::L();
        const ui::Rect box { r.x + 4 + 64 + 8, r.y + 8, std::max (0, r.w - 2 * (4 + 64 + 8)), 48 };   // between the power button (top left) and the full screen button (top right) of the display
        const float size = ui::Pen::fitSize (missingText_.c_str(), static_cast<float> (box.w), static_cast<float> (box.h), T.fLabel + 4.0f, T.fMin);
        if (size <= 0.0f)
            return;
        char fitted[160];
        ui::contentname::middleFit (missingText_.c_str(), size, static_cast<float> (box.w), fitted, static_cast<int> (sizeof fitted));
        ui::Cell cell (sb, box);
        ui::Pen pen (sb, true);
        pen.textCentered (box, fitted, T.fLabel + 4.0f, pen.c (T.negative, 255), T.fMin);
    }
    std::string missingText_;

    /** The empty state: a type that plays a file with no file loaded gets one button over its display (the page of the type and the full screen). */
    void updateLoadButton (const IInstanceView& v)
    {
        const bool fileType = sel_ != playosc::kSub && sel_ != playosc::kNoise && (typeRaw_ == 1 || typeRaw_ == 2 || typeRaw_ == 3 || typeRaw_ == 4);
        const bool onPage = stackDepth_ >= 1 && (pageTop_ == 0 || oscspec::isSpecPage (pageTop_));
        const ui::Rect r = onPage ? displayRect() : ui::Rect {};
        loadShown_ = fileType && onPage && ! r.empty() && v.contentLoad (nullptr) != 1 && loadedName (v).empty();
        loadBtn_.setLabel (typeRaw_ == 4 ? "LOAD INSTRUMENT" : "LOAD SAMPLE");
        loadBtn_.place (r.empty() ? ui::Rect {} : ui::Rect { r.x + r.w / 2 - 150, r.y + r.h * 2 / 3 - 8, 300, 64 });
        loadBtn_.setId (kSpecLoad);
        marksRect_ = r;
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

    /** The list of the source selector of the compact view: Sub, Osc A, Osc B, Osc C, Noise (the names of the selector), five rows of 56 px under the header row, a popup over the third. */
    void openSourceList()
    {
        static const char* const kNames[playosc::kInstances] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" };
        const ui::Rect third { kCompactX, kTabY + 4, kCompactW, kTabH - 8 };
        list_.state.header = nullptr;
        list_.state.headerBack = false;
        list_.state.items = kNames;
        list_.state.count = playosc::kInstances;
        list_.state.current = sel_ - 1;
        list_.place ({ third.x + 6, third.y + 6 + ui::playrows::kSelectorH, third.w - 12, playosc::kInstances * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (third);
        list_.showCurrent();
        srcPopup_ = true;
    }
    void closeSourceList()
    {
        srcPopup_ = false;
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

    /** The detail page a PAGE button of the type bar opens (by its label). */
    static int pageOfBar (const char* label) noexcept
    {
        struct E { const char* l; int page; };
        static const E t[] = { { "MAP", oscspec::kWtMap }, { "REGION", oscspec::kSampleRegion }, { "SLICE", oscspec::kSampleSlice }, { "GRAIN", oscspec::kGrain }, { "FILTER", oscspec::kSpecFilter }, { "SCAN", oscspec::kSpecScan } };
        for (const E& e : t)
            if (std::strcmp (e.l, label) == 0)
                return e.page;
        return 0;
    }

    void addModulation (IInstanceView& v, int source)
    {
        if (source == ui::kNoSource || modsTargetParam_ < 0)
            return;
        if (slotOf (v, modsTargetParam_, source) >= 0)
            return;                                   // that source already modulates it
        const int slot = freeSlot (v);
        const size_t dest = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        if (slot < 0 || dest == 0)
            return;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slot);
        writePlain (v, sp.destination, static_cast<double> (dest));
        writePlain (v, sp.amount, 0.0);
        if (sp.bypass >= 0) writePlain (v, sp.bypass, 0.0);
        if (sp.bipolar >= 0) writePlain (v, sp.bipolar, info (modsTargetParam_).min < 0.0f && info (modsTargetParam_).max > 0.0f ? 1.0 : 0.0);
        writePlain (v, sp.source, source);
        loadMods (v);
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
                    writePlain (v, p, transposeFrom (static_cast<int> (std::lround (c.value)), semOf (static_cast<int> (std::lround (v.param (p))))));
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
            case T::StepperLoop:
                if (c.kind == K::Index)
                {
                    const int m = ((c.index % 3) + 3) % 3;
                    writePlain (v, p, m == 1 ? 1.0 : 0.0);
                    if (it.param2 >= 0)
                        writePlain (v, it.param2, m == 2 ? 1.0 : 0.0);
                }
                break;
            case T::Toggle: if (c.kind == K::Index) writePlain (v, p, c.index != 0 ? 1.0 : 0.0); break;
            case T::SnapBit: if (c.kind == K::Index) writePlain (v, p, (static_cast<int> (std::lround (v.param (p))) & 4095) | (c.index != 0 ? 4096 : 0)); break;
            case T::Mask: if (c.kind == K::Index) writePlain (v, p, (static_cast<int> (std::lround (v.param (p))) & 4096) | (c.index & 4095)); break;
            case T::List: if (c.kind == K::Index) writePlain (v, p, info (p).min + std::clamp (c.index, 0, std::max (0, info (p).numChoices - 1))); break;
            case T::UpDown: if (c.kind == K::Index) writePlain (v, p, c.index); break;
            case T::Picto:
                if (c.kind == K::Index)
                {
                    static const int kRaw[5] = { 0, 4, 1, 2, 3 };   // WAVE, MULTI, SAMPLE, GRAIN, SPEC
                    writePlain (v, p, kRaw[std::clamp (c.index, 0, 4)]);
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
        if (it.alwaysOff || it.type == T::Inert)
            return -1;
        if (it.type == T::NumberRead || it.type == T::SnapBit)
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
            if (highlight_ != 1)
            {
                qActive_ = 0;
                return;   // the Env / LFO sub-tab: the view of the Env / LFO owns the group (EnvUi / LfoUi)
            }
            qSlots_.publish (v, 1, 0, [&] (int, int* t) { members (compact_.basic.group, t); });
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
            qSlots_.publish (v, mods_.numRows(), mods_.activeRow(), [this] (int row, int* t) { modRowTargets (row < ui::ModulationsPage::kMaxRows ? modRows_[row].slot : -1, t); });
            setRowBlocks (mods_, ui::ModulationsPage::kMaxRows, mods_.numRows(), mods_.activeRow());
        }
        else if (pageTop_ == kPageList)
        {
            name = "List";
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else if (pageTop_ == kPageRoute)
        {
            name = "Route";
            qSlots_.publish (v, 1, 0, [&] (int, int* t) { members (route_.group, t); });
        }
        else
        {
            name = full_.activeGroup() >= 0 ? full_.groupName (full_.activeGroup()) : "";
            qSlots_.publish (v, full_.numGroups(), full_.activeGroup(), [&] (int g, int* t) { members (full_.groups[g], t); });
            for (int g = 0; g < ui::OscillatorFullScreen::kMaxGroups; ++g)
                full_.groups[g].setBlock (g < full_.numGroups() ? QRouter::blockOfGroup (full_.numGroups(), full_.activeGroup(), g) : -1);
        }
        qActive_ = stackDepth_ == 0 ? 0 : (pageTop_ == 0 ? full_.activeGroup() : 0);
        ui::setText (qName_, sizeof qName_, name);
    }

    // ======================================================================================================================================================================
    ui::OscillatorCompact compact_;
    ui::OscillatorFullScreen full_;
    ui::DetailPage detail_;
    ui::ModulationsPage mods_;
    ui::RoutePanel route_;
    ui::ListPopup list_;
    bool srcPopup_ = false;   // the list of the source selector of the compact view is open (main state only)
    oscspec::OscSpecPage spec_;
    ui::MenuButton loadBtn_ { "LOAD SAMPLE", ui::MenuButton::Icon::None };
    ui::Rect marksRect_;
    bool loadShown_ = false, specVisible_ = false;
    ContentBrowser browser_;
    std::string browserTitle_;
    bool browseMode_ = false;
    std::string lastChosen_[2];   // the file that was loaded last for the granular (0) and multisample (1) type: the browser opens in its folder
    int listParam_ = -1;
    ui::Rect popupBox_;
    std::vector<Item> main_, page_;
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false, configured_ = false, modsDirty_ = true;
    int pageSel_ = 0;   // the oscillator of a page of the OSC tab (setPage), 0: the Play page's UI
    int noiseSample_ = -1;
    int w_ = 1280, h_ = 628, sel_ = 2, typeRaw_ = 0, highlight_ = 1, stackDepth_ = 0, pageTop_ = 0, pageSig_ = 0, pageLoaded_ = 0, qActive_ = 0;
    int envSel_ = 1, lfoSel_ = 1, selectedSource_ = ui::kNoSource;
    double modsAtMs_ = -1000.0;
    std::string prefix_ = "osc1.";
    char typeName_[16] = {}, qName_[16] = {};
    QSlots qSlots_;
};

}} // namespace synthyboy_mpc::overlay
