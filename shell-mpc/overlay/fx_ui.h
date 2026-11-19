// SynthyBoy - MPC overlay: the FX UI, the page of the FX tab (the plugin draws the whole tab, page_view.h): the chain of the effects of the three racks (Main, Bus 1, Bus 2) as cards in processing order and the
// page of the selected effect (shell-mpc/ui/fx_chain_bar.h, fx_slot_editor.h, fx_display.h), wired to the engine's FX racks (fx_rack.h: the modules of the fixed chain and the slots of the pool) and to the
// parameter descriptors of the effect types (fx_types.h: the name, the range and the unit of every parameter come from the descriptor's source key: one source of truth). One object per (page view, plugin
// instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Mixer and FX UI".
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack): the page is always open; above it the list popup that picks the type of a new effect. The cards: a tap selects, the power zone switches, a drag moves the card (fxMove: the rack and
// the position), + ADD adds an effect of the chosen type at the end of the Main rack. The page of an effect has its power switch, its rack (a stepper: the effect moves to another rack), the pages of its knobs,
// REMOVE (the slots of the pool: the modules of the fixed chain are switched off instead), the display (EQ, compressor, delay, FX filter, distortion) and the knobs in rows of four: one Q group per row.
// An empty chain has no effect to edit: the page is the GALLERY of the effect types the engine has (ui/fx_gallery.h, a card with a picture per type); one tap on a card adds that effect at the end of the Main
// rack (the same as + ADD and a row of its list) and selects it, so the page of the new effect replaces the gallery.
// THE RACKS OF THE BANDS: the page of a splitter shows one lane per band (ui/fx_band_board.h): the chain of the band as cards. A tap on a card, on the tile of the band or on its + ADD OPENS THE BAND (scope_): the chain
// bar then holds the cards of that band (engine racks 3 .. 9; only the 13 modules of instance 0 that are no splitters sit in them), a BACK card first (the way up to the splitter), and the page of the selected effect
// is the same table page as in the Main chain; the place stands over the name ("MAIN CHAIN > SPLITTER L/M/H > MID BAND"). + ADD in a band offers the types whose module is free (off and not in a band), moves the module
// into the band and switches it on; a drag reorders; the rack switch of the page moves the effect to any of the ten racks; REMOVE switches the effect off and takes it out of its band.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../ui/choice_names.h"
#include "../ui/fx_screen.h"
#include "../ui/list.h"
#include "../ui/ui.h"
#include "instance_view.h"
#include "page_view.h"
#include "q_publish.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/filters/filter_types.h"
#include "synthyboy/fx_rack.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"
#include "synthyboy/readout.h"

namespace synthyboy_mpc { namespace overlay {

class FxUi
{
public:
    static constexpr int kElementPage = 6;        // the page of the FX tab (the oscillator is 1, the envelope 2, the LFO 3, the filter 4)
    static constexpr int kElementFx = kElementPage;
    static constexpr int kPageList = 35;

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    enum IdBase : int
    {
        kChainBase = 3000,
        kBandBase = 3100,
        kEditorBase = 4000,
        kEditorPower = kEditorBase + 100, kEditorRack, kEditorPage, kEditorRemove,
        kList = 5000,
        kListBackdrop = 5001,
        kGallery = 5100
    };

    FxUi()
    {
        plain_.assign (synthyboy::paramCount(), 0.0f);
        cardItems_.reserve (ui::FxChainBar::kMaxSlots);
        screen_.assignIds (kChainBase, kEditorBase);
        screen_.slotEditor.bands.assignIds (kBandBase);
        setCanvas (kCanvasW, kCanvasH);
        int galleryTypes[ui::fxl::kNumKinds - 1];
        for (int k = 1; k < ui::fxl::kNumKinds; ++k)
        {
            typeNames_[k - 1] = ui::fxl::layoutOf (k).name;   // the 13 types of the pool, then the three splitters
            galleryTypes[k - 1] = k;
        }
        screen_.gallery.setTypes (galleryTypes, typeNames_, ui::fxl::kNumKinds - 1);
        screen_.gallery.setId (kGallery);
        screen_.setEditorShown (false);
        for (int i = 0; i < 16; ++i)
        {
            char key[16];
            std::snprintf (key, sizeof key, "fx.order.%d", i + 1);
            orderIdx_[i] = uibind::indexOfKey (key);
        }
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
    int numCards() const noexcept { return static_cast<int> (cardItems_.size()); }
    const synthyboy::FxRackItem& cardItem (int i) const noexcept { return cardItems_[static_cast<size_t> (i)]; }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    int topPage() const noexcept { return listOpen_ ? kPageList : 0; }
    /** The chain on the page: 0 the Main / Bus chain, 3 .. 9 the chain of that band rack (fxl::bandOfRack). */
    int scope() const noexcept { return scope_; }
    /** The modules of a band rack (3 .. 9) in processing order, as of the last update. */
    const std::vector<synthyboy::FxRackItem>& bandItems (int rack) const noexcept { return bandItems_[static_cast<size_t> (std::clamp (rack, ui::fxl::kFirstBandRack, ui::fxl::kNumRacks - 1) - ui::fxl::kFirstBandRack)]; }
    const synthyboy::FxRackItem& selectedItem() const noexcept { return selectedItem_; }

    /** Reads the instance: the chain (only when its structure changed), the selected effect with its knobs, display data and Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        (void) nowMs;
        live_ = in.live;
        // the structure of the chain: a signature of the parameters that decide it
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            plain_[i] = inst.param (static_cast<int> (i));
        const bool openNow = stack.depth() >= 2 && stack.top() == kPageList;
        if (! openNow && listOpen_)
            listOpen_ = false;
        if (openNow && listKind_ == ListKind::ParamChoice && listParam_ >= 0 && ! cardItems_.empty() && selectedSlot_ < static_cast<int> (cardItems_.size()))
        {
            const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
            const synthyboy::ParamInfo* pi = screen_.slotEditor.infoOf (listParam_);
            if (pi != nullptr && std::strcmp (pi->key, "fxfilter.style") == 0)
            {
                const int kind = kindOfItem (item);
                const int model = static_cast<int> (std::lround (plainOfKey (item, kind, inst, "fxfilter.model", 0.0f)));
                const int count = synthyboy::filters::filterModelStyleCount (model);
                list_.state.count = count;
                list_.state.current = std::clamp (static_cast<int> (std::lround (plainOfControl (item, inst, listParam_))), 0, std::max (0, count - 1));
            }
        }
        const uint32_t sig = structureSignature();
        if (sig != chainSig_ || selectionChanged_)
        {
            chainSig_ = sig;
            selectionChanged_ = false;
            rebuildChain();
        }
        pullCards();
        pullEditor (inst);
        refreshGallery();
        screen_.setEditorShown (! cardItems_.empty());
        inst.setUiFullOpen (true);
        publishQGroup (inst);
    }

    void draw (gpu::SceneBuilder& sb) const
    {
        page::ground (sb);
        screen_.draw (sb);
        if (listOpen_)
            list_.draw (sb);
    }

    bool covers (int x, int y) const noexcept { return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y); }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        ui::Component* top = listOpen_ ? static_cast<ui::Component*> (&list_) : static_cast<ui::Component*> (&screen_);
        if (ev.kind == TouchEvent::Down)
        {
            gesture_ = nullptr;
            if (! covers (ev.x, ev.y))
                return false;
            gesture_ = top;
            gestureDown_ = true;
            handle (top->touchDown (ev.x, ev.y, nowMs), stack, inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (ev.x, ev.y), stack, inst);
        else
        {
            handle (gesture_->touchUp (ev.x, ev.y), stack, inst);
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
        ui::Component* top = listOpen_ ? static_cast<ui::Component*> (&list_) : static_cast<ui::Component*> (&screen_);
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
    void forgetQGroup() noexcept { qSlots_.forget(); }
    void turnQ (int knobIndex, double delta, UiStack& stack, IInstanceView& inst) { handle (screen_.turnQ (knobIndex, delta), stack, inst); }
    bool listOpen() const noexcept { return listOpen_; }

    /** The table index of control p of the effect on the page (-1 none): the parameter of the slot of the pool for a control that is one of the type's own, else the engine's key of the control itself (the splitters,
        the modules of the fixed chain, the force enable of the reverb). The page is the one of `item` (the selected effect). */
    int paramIndexOf (const synthyboy::FxRackItem& item, int p) const noexcept
    {
        const synthyboy::ParamInfo* pi = screen_.slotEditor.infoOf (p);
        if (pi == nullptr)
            return -1;
        const int kind = kindOfItem (item);
        if (item.pool && kind < synthyboy::kNumFxInstanceTypes && p < synthyboy::fxTypeDescriptor (kind).count)
            return synthyboy::fxInstanceKeys (item.index).p[p];
        return static_cast<int> (pi - synthyboy::paramTable());
    }

private:
    using Item = synthyboy::FxRackItem;
    static constexpr int kMaxItems = ui::FxChainBar::kMaxSlots;

    uint32_t structureSignature() const noexcept
    {
        uint32_t h = 2166136261u ^ static_cast<uint32_t> (scope_ * 7919);
        auto mix = [&] (int idx)
        {
            if (idx < 0)
                return;
            int32_t v;
            const float f = plain_[static_cast<size_t> (idx)];
            std::memcpy (&v, &f, 4);
            h = (h ^ static_cast<uint32_t> (v)) * 16777619u;
        };
        for (int i : orderIdx_)
            mix (i);
        for (int s = 0; s < synthyboy::kNumFxInstances; ++s)
        {
            const synthyboy::FxInstanceKeys& k = synthyboy::fxInstanceKeys (s);
            mix (k.type); mix (k.on); mix (k.rack); mix (k.order);
        }
        for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
        {
            const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (t);
            mix (d.onIndex); mix (d.rackIndex);
        }
        for (const char* pre : { "splitter_lh", "splitter_lmh", "splitter_ms" })   // the splitters of the fixed chain: their power and rack decide the chain too
        {
            mix (uibind::indexOfKey (std::string (pre) + ".on"));
            mix (uibind::indexOfKey (std::string (pre) + ".rack"));
        }
        return h;
    }

    /** The page of an item: the type of a slot or of a module of the chain, or the kind of a splitter (modules 13 .. 15 of the fixed chain have no type of the pool). */
    int kindOfItem (const Item& item) const noexcept
    {
        const int type = synthyboy::fxItemType (plain_.data(), item);
        if (type != synthyboy::kFxTypeNone)
            return type;
        return ! item.pool && item.index >= 13 && item.index <= 15 ? ui::fxl::kKindSplitterLh + (item.index - 13) : synthyboy::kFxTypeNone;
    }

    /** The cards: every effect of the pool, and the modules of the fixed chain that are on (or selected), rack by rack in processing order. */
    void rebuildChain()
    {
        const synthyboy::FxRacks racks = synthyboy::fxRacks (plain_.data());
        cardItems_.clear();
        for (int r = ui::fxl::kFirstBandRack; r < ui::fxl::kNumRacks; ++r)
            bandItems_[static_cast<size_t> (r - ui::fxl::kFirstBandRack)] = racks.rack[static_cast<size_t> (r)];
        if (scope_ != 0)   // the chain of a band: every module that sits in the rack (a bypassed one stays on the page, its power is off)
            for (const synthyboy::FxRackItem& item : racks.rack[static_cast<size_t> (scope_)])
                if (static_cast<int> (cardItems_.size()) < kMaxItems)
                    cardItems_.push_back (item);
        for (int r = 0; scope_ == 0 && r < synthyboy::kNumPoolRacks && static_cast<int> (cardItems_.size()) < kMaxItems; ++r)
            for (const synthyboy::FxRackItem& item : racks.rack[static_cast<size_t> (r)])
            {
                if (static_cast<int> (cardItems_.size()) >= kMaxItems)
                    break;
                if (item.pool)
                {
                    if (kindOfItem (item) != synthyboy::kFxTypeNone)
                        cardItems_.push_back (item);
                }
                else if (synthyboy::fxItemOn (plain_.data(), item) || (selectedItem_.index >= 0 && selectedItem_ == item))
                    cardItems_.push_back (item);
            }
        if (cardItems_.empty())
        {
            selectedSlot_ = 0;
            selectedItem_ = { false, -1 };
            return;
        }
        int found = -1;
        for (size_t i = 0; i < cardItems_.size(); ++i)
            if (selectedItem_.index >= 0 && cardItems_[i] == selectedItem_)
                found = static_cast<int> (i);
        if (found >= 0)
            selectedSlot_ = found;
        else
        {
            selectedSlot_ = std::clamp (selectedSlot_, 0, static_cast<int> (cardItems_.size()) - 1);
            selectedItem_ = cardItems_[static_cast<size_t> (selectedSlot_)];
        }
    }

    static void upper (char* out, size_t cap, const char* in)
    {
        size_t i = 0;
        for (; in != nullptr && in[i] != '\0' && i + 1 < cap; ++i)
            out[i] = static_cast<char> (std::toupper (static_cast<unsigned char> (in[i])));
        out[i] = '\0';
    }

    /** The types a band can still take: a module of instance 0 that is no splitter, off, and not parked in a band (a module that sits in a band is a card there). */
    bool moduleFree (int module) const noexcept
    {
        const Item item { false, module };
        return module >= 0 && module < 13 && ! synthyboy::fxItemOn (plain_.data(), item) && synthyboy::fxItemRack (plain_.data(), item) < ui::fxl::kFirstBandRack;
    }
    int numFreeModules() const noexcept
    {
        int n = 0;
        for (int m = 0; m < 13; ++m)
            n += moduleFree (m) ? 1 : 0;
        return n;
    }
    /** The kinds the + ADD list and the gallery offer: every type and splitter in the Main chain, the free types in a band. */
    int offeredKinds (int* out) const noexcept
    {
        int n = 0;
        for (int k = 1; k < ui::fxl::kNumKinds; ++k)
            if (scope_ == 0 || (k < ui::fxl::kKindSplitterLh && moduleFree (k - 1)))
                out[n++] = k;
        return n;
    }

    /** The gallery of an empty chain offers what + ADD offers; in a band its headline says where the effect goes. */
    void refreshGallery()
    {
        int kinds[ui::fxl::kNumKinds];
        const int n = offeredKinds (kinds);
        uint32_t sig = static_cast<uint32_t> (scope_) * 131u + 7u;
        for (int i = 0; i < n; ++i)
            sig = sig * 31u + static_cast<uint32_t> (kinds[i]);
        if (sig == gallerySig_ && galleryScope_ == scope_)
            return;
        gallerySig_ = sig;
        galleryScope_ = scope_;
        const char* names[ui::fxl::kNumKinds];
        for (int i = 0; i < n; ++i)
            names[i] = typeNames_[kinds[i] - 1];
        screen_.gallery.setTypes (kinds, names, n);
        char t[96] = "TAP AN EFFECT TO ADD IT TO THE CHAIN";
        char note[96] = {};
        int kind = 0, band = 0;
        if (scope_ != 0 && ui::fxl::bandOfRack (scope_, kind, band))
        {
            char sp[24];
            upper (sp, sizeof sp, ui::fxl::layoutOf (kind).name);
            std::snprintf (t, sizeof t, "%s BAND OF %s: TAP AN EFFECT TO ADD IT", ui::fxl::bandName (kind, band), sp);
            std::snprintf (note, sizeof note, "%s BAND OF %s: EVERY EFFECT IS IN USE (MOVE ONE HERE WITH ITS RACK SWITCH)", ui::fxl::bandName (kind, band), sp);
        }
        else
            std::snprintf (note, sizeof note, "NO EFFECT TO ADD");
        screen_.gallery.setHeadline (t);
        screen_.gallery.setNote (note);
    }

    void pullCards()
    {
        const int numActive = static_cast<int> (cardItems_.size());
        screen_.chainBar.setBackShown (scope_ != 0);
        if (scope_ != 0)
        {
            int kind = 0, band = 0;
            ui::fxl::bandOfRack (scope_, kind, band);
            ui::FxSlotCard::State& st = screen_.chainBar.backCard.state;
            upper (st.name, sizeof st.name, ui::fxl::layoutOf (kind).name);
            std::snprintf (st.tag, sizeof st.tag, "%s BAND", ui::fxl::bandName (kind, band));
        }
        screen_.chainBar.setNumActiveSlots (numActive);
        for (int i = 0; i < numActive; ++i)
        {
            ui::FxSlotCard& card = screen_.chainBar.cards[i];
            const Item item = cardItems_[static_cast<size_t> (i)];
            card.state.slot = i;
            card.state.type = kindOfItem (item);
            card.state.on = synthyboy::fxItemOn (plain_.data(), item);
            card.state.rack = synthyboy::fxItemRack (plain_.data(), item);
            card.state.selected = i == selectedSlot_;
            card.state.empty = false;
            card.state.isPlus = false;
            const char* nm = synthyboy::fxItemName (plain_.data(), item);
            std::snprintf (card.state.name, sizeof card.state.name, "%.23s", nm != nullptr ? nm : "FX");
            card.setEnabled (true);
        }
        screen_.chainBar.plusCard.state.isPlus = true;
        const bool canAdd = numActive < ui::FxChainBar::kMaxSlots && (scope_ == 0 || numFreeModules() > 0);
        screen_.chainBar.plusCard.setEnabled (canAdd);
        for (ui::FxSlotCard& sp : screen_.chainBar.spare)
            sp.setEnabled (canAdd);
    }

    /** The value (plain) of control p of the item, from the instance: a slot of the pool stores the normalised value of the type's own parameters (fxInstancePlain turns it into the plain value of its source), the other
        controls (the modules of the fixed chain, the splitters, the force enable of the reverb) are engine keys of their own. */
    float plainOfControl (const Item& item, IInstanceView& inst, int p) const
    {
        const synthyboy::ParamInfo* pi = screen_.slotEditor.infoOf (p);
        const int idx = paramIndexOf (item, p);
        if (pi == nullptr || idx < 0)
            return 0.0f;
        const float v = inst.param (idx);
        const int kind = kindOfItem (item);
        const bool own = item.pool && kind < synthyboy::kNumFxInstanceTypes && p < synthyboy::fxTypeDescriptor (kind).count;
        return own ? synthyboy::fxInstancePlain (kind, p, v) : v;
    }

    /** The value of the parameter with the key `key` of the selected effect (plain), or `def` when the effect has no such control. */
    float plainOfKey (const Item& item, int, IInstanceView& inst, const char* key, float def) const
    {
        const int p = screen_.slotEditor.controlOfKey (key);
        return p >= 0 && paramIndexOf (item, p) >= 0 ? plainOfControl (item, inst, p) : def;
    }

    void pullEditor (IInstanceView& inst)
    {
        if (cardItems_.empty() || selectedSlot_ >= static_cast<int> (cardItems_.size()))
            return;
        const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
        const int kind = kindOfItem (item);
        ui::FxSlotEditor& ed = screen_.slotEditor;
        ed.notice[0] = '\0';
        if (kind == synthyboy::kFxTypeConvolve && (inst.missingContent() & (1u << 6)) != 0)   // the impulse of Convolve is not there: the effect is bypassed, the page says which file it wants
        {
            std::string nm;
            const std::shared_ptr<const synthyboy::StateContent> c = inst.content();
            if (c != nullptr && ! c->impulses.empty())
                nm = ui::contentname::display (c->impulses[0].name, c->impulses[0].file);
            if (nm.empty())
                std::snprintf (ed.notice, sizeof ed.notice, "content missing");
            else
                std::snprintf (ed.notice, sizeof ed.notice, "content missing: %s", nm.c_str());
        }
        ed.setType (kind, true);   // every effect can be removed: a slot of the pool is freed, a module of instance 0 is switched off and leaves its band
        // the state of the effect decides which controls are live (the table of the effect, ui/fx_layout.h): the rows that have nothing live close up
        ed.setState ([&] (const char* key) { return plainOfKey (item, kind, inst, key, 0.0f); });
        for (int p = 0; p < ed.numParams(); ++p)
        {
            const synthyboy::ParamInfo* pi = ed.infoOf (p);
            if (pi == nullptr)
                continue;
            const float plainVal = plainOfControl (item, inst, p);
            const float norm = static_cast<float> (synthyboy::toNormalised (*pi, plainVal));
            ui::Knob& knob = ed.knobs[p];
            ui::Stepper& stepper = ed.steppers[p];
            ui::LedList& list = ed.lists[p];
            ui::LedToggle& toggle = ed.toggles[p];
            ui::NumberField& number = ed.numbers[p];
            char name[24];
            ed.nameOf (p, name, sizeof name);
            knob.state.value = knob.state.modValue = norm;
            if (ed.isChoice (p))
            {
                stepper.setCaption (name);
                const bool isFilterStyle = (std::strcmp (pi->key, "fxfilter.style") == 0);
                int choiceCount = pi->numChoices;
                const char* const* customChoices = nullptr;
                if (isFilterStyle)
                {
                    const int model = static_cast<int> (std::lround (plainOfKey (item, kind, inst, "fxfilter.model", 0.0f)));
                    choiceCount = synthyboy::filters::filterModelStyleCount (model);
                    customChoices = ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
                    if (choiceCount > 0 && static_cast<int> (std::lround (plainVal)) >= choiceCount)
                    {
                        const int clamped = choiceCount - 1;
                        const int idx = paramIndexOf (item, p);
                        if (idx >= 0)
                            inst.writeParam (idx, static_cast<float> (synthyboy::toNormalised (*pi, static_cast<float> (clamped))));
                    }
                }
                stepper.state.count = choiceCount;
                const int curIdx = std::clamp (static_cast<int> (std::lround (plainVal)), 0, std::max (0, choiceCount - 1));
                stepper.state.index = curIdx;
                if (isFilterStyle)
                {
                    if (customChoices != nullptr && curIdx >= 0 && curIdx < choiceCount)
                        stepper.setItem (customChoices[curIdx]);
                    else
                        stepper.setItem ("---");
                    // One-style rule: a model with one style shows the style control dim / as a plain label
                    if (choiceCount <= 1)
                    {
                        stepper.state.dim = true;
                        stepper.listable = false;
                    }
                    else
                    {
                        stepper.state.dim = false;
                        stepper.listable = true;
                    }
                }
                else
                {
                    if (pi->choices != nullptr && curIdx >= 0 && curIdx < pi->numChoices)
                        stepper.setItem (ui::shownChoice (pi->key, curIdx, pi->choices[curIdx]));
                    else
                        stepper.setItem ("---");
                }
                // the same value as a LED list (3 .. 8 values, all visible) and as a LED toggle (Off / On): the editor shows the kind the cell has
                list.setTitle (name);
                {
                    const char* shown[ui::LedList::kMaxOptions];
                    const int cnt = std::min (static_cast<int> (choiceCount), static_cast<int> (ui::LedList::kMaxOptions));
                    for (int c = 0; c < cnt; ++c)
                        shown[c] = isFilterStyle ? (customChoices != nullptr && c < choiceCount ? customChoices[c] : "") : ui::shownChoice (pi->key, c, pi->choices[c]);
                    list.setOptions (shown, cnt);
                }
                list.state.index = std::min (curIdx, ui::LedList::kMaxOptions - 1);
                toggle.setLabel (name);
                toggle.state.on = curIdx == 1;
            }
            else if (ed.kindOf (p) == ui::FxSlotEditor::Kind::Number)
            {
                // a whole number (VOICES, UNISON): a number field in whole steps, the engine's own range
                number.setLabel (name);
                number.state.min = static_cast<double> (pi->min);
                number.state.max = static_cast<double> (pi->max);
                number.state.step = 1.0;
                number.state.defaultValue = static_cast<double> (pi->def);
                number.state.value = std::clamp (std::round (static_cast<double> (plainVal)), number.state.min, number.state.max);
            }
            else
            {
                knob.state.defaultValue = synthyboy::toNormalised (*pi, pi->def);
                knob.state.bipolar = pi->min < 0.0f && pi->max > 0.0f;
                paramText (*pi, plainVal, knob.state.valueText, sizeof knob.state.valueText);
                knob.setLabel (name);
            }
        }
        // the power, the rack
        ed.power.state.on = synthyboy::fxItemOn (plain_.data(), item);
        const int rack = synthyboy::fxItemRack (plain_.data(), item);
        ed.rack.state.count = rackChoices (item);
        ed.rack.state.index = std::clamp (rack, 0, ed.rack.state.count - 1);
        ed.rack.setItem (ui::fxl::rackName (rack));
        pullPath (ed, item, rack);
        if (ed.hasBands())
            pullBands (inst, item, kind);
        pullDisplay (inst, item, kind, ed.display);
    }

    /** The racks an item can be moved to: the ten of the engine for a module of instance 0 that is no splitter (Main, Bus 1, Bus 2 and the seven band racks), else the three buses. */
    static int rackChoices (const Item& item) noexcept { return ! item.pool && item.index < 13 ? ui::fxl::kNumRacks : synthyboy::kNumPoolRacks; }

    /** The place of the effect over its name: "MAIN CHAIN", "BUS 1 CHAIN", or for an effect of a band "MAIN CHAIN > SPLITTER L/M/H > MID BAND" (the chain the splitter sits in). */
    void pullPath (ui::FxSlotEditor& ed, const Item& item, int rack)
    {
        char path[72];
        int kind = 0, band = 0;
        if (rack >= ui::fxl::kFirstBandRack && ui::fxl::bandOfRack (rack, kind, band))
        {
            char sp[24];
            upper (sp, sizeof sp, ui::fxl::layoutOf (kind).name);
            const int splitterRack = synthyboy::fxItemRack (plain_.data(), { false, 13 + (kind - ui::fxl::kKindSplitterLh) });
            std::snprintf (path, sizeof path, "%s CHAIN  >  %s  >  %s BAND", ui::fxl::rackName (splitterRack), sp, ui::fxl::bandName (kind, band));
        }
        else
            std::snprintf (path, sizeof path, "%s CHAIN", ui::fxl::rackName (rack));
        (void) item;
        ed.setPath (path);
    }

    /** The lanes of the page of a splitter: per band the tile (name, range, state) and the cards of its rack. */
    void pullBands (IInstanceView& inst, const Item& item, int kind)
    {
        ui::FxBandBoard& board = screen_.slotEditor.bands;
        const int nb = ui::fxl::bandsOfSplitter (kind);
        const bool splitterOn = synthyboy::fxItemOn (plain_.data(), item);
        const char* const pre = kind == ui::fxl::kKindSplitterLh ? "splitter_lh." : (kind == ui::fxl::kKindSplitterLmh ? "splitter_lmh." : "splitter_ms.");
        auto val = [&] (const char* suffix, float def) { return plainOfKey (item, kind, inst, (std::string (pre) + suffix).c_str(), def); };
        auto hz = [] (float f, char* out, size_t n)
        {
            if (f >= 1000.0f)
                std::snprintf (out, n, "%.1f kHz", static_cast<double> (f) / 1000.0);
            else
                std::snprintf (out, n, "%.0f Hz", static_cast<double> (f));
        };
        char f1[16] = {}, f2[16] = {};
        if (kind == ui::fxl::kKindSplitterLh)
            hz (val ("freq", 500.0f), f1, sizeof f1);
        else if (kind == ui::fxl::kKindSplitterLmh)
        {
            hz (val ("freq_low", 200.0f), f1, sizeof f1);
            hz (val ("freq_high", 3000.0f), f2, sizeof f2);
        }
        int counts[ui::FxBandBoard::kMaxBands] = {};
        for (int b = 0; b < nb; ++b)
        {
            const int rack = ui::fxl::bandRack (kind, b);
            const std::vector<Item>& items = bandItems_[static_cast<size_t> (rack - ui::fxl::kFirstBandRack)];
            const int n = std::min (static_cast<int> (items.size()), ui::FxBandBoard::kMaxCards);
            counts[b] = n;
            ui::FxBandBoard::Lane& lane = board.lane[b];
            for (int i = 0; i < n; ++i)
            {
                ui::FxSlotCard& card = lane.cards[i];
                card.state.slot = i;
                card.state.type = kindOfItem (items[static_cast<size_t> (i)]);
                card.state.on = synthyboy::fxItemOn (plain_.data(), items[static_cast<size_t> (i)]);
                card.state.rack = rack;
                card.state.selected = false;
                std::snprintf (card.state.name, sizeof card.state.name, "%.23s", synthyboy::fxItemName (plain_.data(), items[static_cast<size_t> (i)]));
                card.setEnabled (true);
            }
            lane.add.setEnabled (numFreeModules() > 0);
            ui::FxBandTile::State& t = lane.tile.state;
            std::snprintf (t.name, sizeof t.name, "%s", ui::fxl::bandName (kind, b));
            t.idle = ! splitterOn;
            t.colour = kind == ui::fxl::kKindSplitterMs ? (b == 0 ? 0 : 3) : (b == 0 ? 0 : (b == nb - 1 ? 3 : 1));
            if (kind == ui::fxl::kKindSplitterMs)
            {
                std::snprintf (t.range, sizeof t.range, "%s", b == 0 ? "the centre of the picture" : "what makes it wide");
                t.bypassed = val (b == 0 ? "mid_bypass" : "side_bypass", 0.0f) > 0.5f;
            }
            else
            {
                const char* const lowKey = "low_bypass";
                const char* const midKey = "mid_bypass";
                const char* const highKey = "high_bypass";
                const bool low = b == 0, high = b == nb - 1;
                if (low)
                    std::snprintf (t.range, sizeof t.range, "below %s", f1);
                else if (high)
                    std::snprintf (t.range, sizeof t.range, "above %s", nb == 3 ? f2 : f1);
                else
                    std::snprintf (t.range, sizeof t.range, "%s to %s", f1, f2);
                t.bypassed = val (low ? lowKey : (high ? highKey : midKey), 0.0f) > 0.5f;
            }
            if (! splitterOn)
                std::snprintf (t.info, sizeof t.info, "SPLITTER OFF");
            else if (n == 0)
                std::snprintf (t.info, sizeof t.info, "no effect");
            else
                std::snprintf (t.info, sizeof t.info, "%d effect%s", n, n == 1 ? "" : "s");
        }
        board.setShape (nb, counts);
    }

    /** The text of a value: the name of a choice, else the number with the parameter's unit. */
    static void paramText (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
    {
        if (pi.numChoices > 0 && pi.choices != nullptr)
        {
            const int n = std::clamp (static_cast<int> (std::lround (plain)), 0, pi.numChoices - 1);
            std::snprintf (out, cap, "%.15s", ui::shownChoice (pi.key, n, pi.choices[n]));
            return;
        }
        uibind::formatValue (pi, plain, out, cap);
    }

    static float beatsOfDivision (int d) noexcept
    {
        static const float kBeats[9] = { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
        return kBeats[std::clamp (d, 0, 8)];
    }
    /** The delay time in milliseconds: free, or a note value at the tempo of the host (straight, dotted, triplet). */
    float delayMs (const Item& item, int type, IInstanceView& inst, const char* timeKey, const char* divKey) const
    {
        const int sync = static_cast<int> (std::lround (plainOfKey (item, type, inst, "delay.sync", 1.0f)));
        if (sync <= 0)
            return plainOfKey (item, type, inst, timeKey, 300.0f);
        const float factor = sync == 2 ? 1.5f : (sync == 3 ? 2.0f / 3.0f : 1.0f);
        return beatsOfDivision (static_cast<int> (std::lround (plainOfKey (item, type, inst, divKey, 5.0f)))) * factor * static_cast<float> (60000.0 / std::max (20.0, inst.bpm()));
    }

    void pullDisplay (IInstanceView& inst, const Item& item, int type, ui::FxDisplay& d)
    {
        using K = ui::FxDisplay::Kind;
        switch (d.kind)
        {
            case K::Eq:
            {
                auto band = [&] (ui::FxDisplay::EqBand& b, const char* base, bool optional)
                {
                    char key[40];
                    std::snprintf (key, sizeof key, "%s.mode", base);
                    const int mode = static_cast<int> (std::lround (plainOfKey (item, type, inst, key, 0.0f)));
                    b.used = ! optional || mode > 0;
                    b.mode = optional ? std::max (0, mode - 1) : mode;
                    std::snprintf (key, sizeof key, "%s.cutoff", base);
                    b.freq = plainOfKey (item, type, inst, key, 1000.0f);
                    std::snprintf (key, sizeof key, "%s.gain", base);
                    b.gainDb = plainOfKey (item, type, inst, key, 0.0f);
                    std::snprintf (key, sizeof key, "%s.resonance", base);
                    b.resonance = plainOfKey (item, type, inst, key, 30.0f);
                };
                band (d.eq.low, "eq.low", false);
                band (d.eq.mid, "eq.mid", false);
                band (d.eq.mid2, "eq.mid2", true);
                band (d.eq.high, "eq.high", false);
                break;
            }
            case K::Compressor:
            {
                static const char* const kBand[3] = { "low", "mid", "high" };
                for (int b = 0; b < 3; ++b)
                {
                    char key[48];
                    ui::FxDisplay::CompBand& cb = d.comp.band[b];
                    std::snprintf (key, sizeof key, "compressor.%s.lower_threshold", kBand[b]); cb.lowerThr = plainOfKey (item, type, inst, key, -40.0f);
                    std::snprintf (key, sizeof key, "compressor.%s.upper_threshold", kBand[b]); cb.upperThr = plainOfKey (item, type, inst, key, -20.0f);
                    std::snprintf (key, sizeof key, "compressor.%s.lower_ratio", kBand[b]); cb.lowerRatio = plainOfKey (item, type, inst, key, 0.0f) * 0.01f;
                    std::snprintf (key, sizeof key, "compressor.%s.upper_ratio", kBand[b]); cb.upperRatio = plainOfKey (item, type, inst, key, 50.0f) * 0.01f;
                    std::snprintf (key, sizeof key, "compressor.%s.gain", kBand[b]); cb.gainDb = plainOfKey (item, type, inst, key, 0.0f);
                }
                d.comp.bands = static_cast<int> (std::lround (plainOfKey (item, type, inst, "compressor.bands", 0.0f)));
                // the band of the row of knobs that is on screen (the first live member of the active row names it)
                {
                    const ui::FxSlotEditor& ed = screen_.slotEditor;
                    const int first = ed.activeRow();
                    int pm = -1;
                    for (int c = 0; c < 4 && pm < 0; ++c)
                        pm = ed.controlInCell (first, c);
                    const char* k = pm >= 0 && ed.infoOf (pm) != nullptr ? ed.infoOf (pm)->key : "";
                    d.comp.activeBand = std::strstr (k, ".low.") != nullptr ? 0 : (std::strstr (k, ".high.") != nullptr ? 2 : 1);
                }
                break;
            }
            case K::Delay:
            {
                d.delay.timeMs1 = delayMs (item, type, inst, "delay.time", "delay.division");
                d.delay.timeMs2 = delayMs (item, type, inst, "delay.time2", "delay.division2");
                d.delay.feedback = plainOfKey (item, type, inst, "delay.feedback", 40.0f) * 0.01f;
                d.delay.style = static_cast<int> (std::lround (plainOfKey (item, type, inst, "delay.style", 1.0f)));
                break;
            }
            case K::Filter:
                pullFilter (inst, item, type, d);
                break;
            case K::Distortion:
                d.dist.type = static_cast<int> (std::lround (plainOfKey (item, type, inst, "distortion.type", 0.0f)));
                d.dist.driveDb = plainOfKey (item, type, inst, "distortion.drive", 0.0f);
                d.dist.mix = plainOfKey (item, type, inst, "distortion.mix", 100.0f) * 0.01f;
                {   // where the signal is now: the meter of the rack's input (the bus meters are before their racks; the main rack: the master meter, after it), 0 = -60 dBFS .. 1 = full scale
                    const int rk = std::clamp (synthyboy::fxItemRack (plain_.data(), item), 0, 2);
                    const float pk = live_ != nullptr ? live_->stripPeak[rk == 0 ? 9 : 6 + rk] : 0.0f;
                    d.dist.inLevel = pk > 0.005f ? std::pow (10.0f, (pk * 60.0f - 60.0f) / 20.0f) : -1.0f;
                }
                break;
            default: break;
        }
    }
    /** The response of the FX filter: the engine's own filter code on an impulse (FilterResponse), with the parameters of the effect put into the slots of filter 1; computed again only when they changed. */
    void pullFilter (IInstanceView& inst, const Item& item, int type, ui::FxDisplay& d)
    {
        static const char* const kKeys[] = { "model", "style", "cutoff", "resonance", "drive", "blend", "mix", "keytrack", "comb_offset", "formant.x", "formant.y", "formant.resonance", "formant.spread", "formant.transpose", "var", "pan", "clean_mode" };
        constexpr int kN = sizeof kKeys / sizeof kKeys[0];
        float vals[kN];
        bool changed = ! haveFilter_;
        for (int i = 0; i < kN; ++i)
        {
            char key[40];
            std::snprintf (key, sizeof key, "fxfilter.%s", kKeys[i]);
            vals[i] = plainOfKey (item, type, inst, key, 0.0f);
            changed = changed || vals[i] != filterVals_[i];
        }
        if (changed)
        {
            if (! response_ || inst.sampleRate() != filterRate_)
            {
                filterRate_ = inst.sampleRate();
                response_ = std::make_unique<synthyboy::FilterResponse> (filterRate_);
                filterValues_ = synthyboy::ParamValues::defaults();
            }
            std::memcpy (filterVals_, vals, sizeof vals);
            auto put = [&] (const char* suffix, float v)
            {
                const int idx = uibind::indexOfKey (std::string ("filter1.") + suffix);
                if (idx >= 0)
                    filterValues_.plain[idx] = v;
            };
            put ("on", 1.0f);
            for (int i = 0; i < kN; ++i)
                put (kKeys[i], vals[i]);
            synthyboy::FilterResponse::Request req;
            req.filter = 1;
            response_->compute (filterValues_, req, filterDb_, 256);
            haveFilter_ = true;
        }
        d.filter.db = haveFilter_ ? filterDb_ : nullptr;
        d.filter.n = 256;
        d.filter.cutoffHz = filterVals_[2];
    }

    /** The groups of the page are the rows (blocks) of the open effect that are on the page of the editor (two to four rows, as the table of the effect says), in reading order: the active row is
        on Q 1 .. 4, the other rows of the page (wrapping) on 5 .. 16. A row the active one is not on the page of (a page change is about to select another) is the only group. */
    void publishQGroup (IInstanceView& inst)
    {
        const char* name = "";
        ui::FxSlotEditor& ed = screen_.slotEditor;
        for (int r = 0; r < ui::FxSlotEditor::kMaxRows; ++r)
            ed.groups[r].setBlock (-1);
        if (listOpen_)
        {
            name = "List";
            qSlots_.publish (inst, 0, 0, [] (int, int*) {});
        }
        else if (selectedSlot_ >= 0 && selectedSlot_ < static_cast<int> (cardItems_.size()))
        {
            const int row = ed.activeRow();
            name = ed.rowTitle (row);
            const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
            const int first = ed.firstRowOfPage(), n = std::max (0, std::min (ed.rowsPerPage(), ed.numRows() - first));
            const bool onPage = row >= first && row < first + n;
            const int count = onPage ? n : 1, base = onPage ? first : row, active = onPage ? row - first : 0;
            qSlots_.publish (inst, count, active, [&] (int k, int* t)
            {
                for (int c = 0; c < 4; ++c)   // the live controls of the row: a dim control has no knob
                {
                    const int p = ed.memberControl (base + k, c);
                    t[c] = p >= 0 ? paramIndexOf (item, p) : -1;
                }
            });
            for (int k = 0; k < count; ++k)
                ed.groups[base + k].setBlock (QRouter::blockOfGroup (count, active, k));
        }
        else
            qSlots_.publish (inst, 0, 0, [] (int, int*) {});
        ui::setText (qName_, sizeof qName_, name);
    }

    void applyWrites (IInstanceView& inst, const std::vector<synthyboy::FxWrite>& writes)
    {
        for (const auto& w : writes)
        {
            const synthyboy::ParamInfo& pi = synthyboy::paramTable()[w.param];
            inst.writeParam (w.param, static_cast<float> (synthyboy::toNormalised (pi, w.value)));
        }
    }

    void openTypeList (UiStack& stack, const ui::Rect& btn)
    {
        listKind_ = ListKind::Type;
        listParam_ = -1;
        listNames_.clear();
        int kinds[ui::fxl::kNumKinds];
        const int offered = offeredKinds (kinds);
        listKinds_.assign (kinds, kinds + offered);
        for (int i = 0; i < offered; ++i)
            listNames_.push_back (typeNames_[kinds[i] - 1]);
        if (offered == 0)
            return;
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.state.current = 0;
        const int popW = 300, popH = std::min (9, list_.state.count) * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad;
        const int popX = std::clamp (btn.x - (popW - btn.w) / 2, 10, w_ - popW - 10);
        const int popY = std::clamp (btn.bottom() + 4, kTabY + 10, kTabY + kTabH - popH - 10);
        list_.place ({ popX, popY, popW, popH });
        list_.showCurrent();
        listOpen_ = true;
        stack.openDetail (kPageList);
    }

    void openParamChoiceList (int p, UiStack& stack, IInstanceView& inst)
    {
        if (cardItems_.empty() || selectedSlot_ >= static_cast<int> (cardItems_.size()))
            return;
        const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
        const synthyboy::ParamInfo* pi = screen_.slotEditor.infoOf (p);
        if (pi == nullptr || pi->numChoices <= 0 || pi->choices == nullptr)
            return;
        listKind_ = ListKind::ParamChoice;
        listParam_ = p;
        listNames_.clear();
        if (std::strcmp (pi->key, "fxfilter.style") == 0)
        {
            const int kind = kindOfItem (item);
            const int model = static_cast<int> (std::lround (plainOfKey (item, kind, inst, "fxfilter.model", 0.0f)));
            const int count = synthyboy::filters::filterModelStyleCount (model);
            if (count <= 1)
                return; // One-style rule: a model with one style has no list popup
            const char* const* styleNames = ui::shownFilterStyleNames (model, synthyboy::filters::filterModelStyleNames (model));
            for (int i = 0; i < count; ++i)
                listNames_.push_back (styleNames != nullptr && styleNames[i] != nullptr ? styleNames[i] : "");
            list_.state.items = listNames_.data();
            list_.state.count = count;
            const int curChoice = std::clamp (static_cast<int> (std::lround (plainOfControl (item, inst, p))), 0, std::max (0, count - 1));
            list_.state.current = curChoice;
        }
        else
        {
            for (int i = 0; i < pi->numChoices; ++i)
                listNames_.push_back (ui::shownChoice (pi->key, i, pi->choices[i]));
            list_.state.items = listNames_.data();
            list_.state.count = pi->numChoices;
            const int idx = paramIndexOf (item, p);
            int curChoice = 0;
            if (idx >= 0)
            {
                curChoice = std::clamp (static_cast<int> (std::lround (plainOfControl (item, inst, p))), 0, pi->numChoices - 1);
            }
            list_.state.current = curChoice;
        }
        const int popW = std::clamp (screen_.slotEditor.steppers[p].rect().w, 240, 360);
        const int popH = std::min (9, list_.state.count) * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad;
        const ui::Rect btn = screen_.slotEditor.steppers[p].rect();
        const int popX = std::clamp (btn.x + (btn.w - popW) / 2, 10, w_ - popW - 10);
        int popY = btn.bottom() + 4;
        if (popY + popH > kTabY + kTabH - 10)
            popY = std::max (kTabY + 10, btn.y - popH - 4);
        list_.place ({ popX, popY, popW, popH });
        list_.showCurrent();
        listOpen_ = true;
        stack.openDetail (kPageList);
    }

    /** A new effect of `kind` (a type of the pool, synthyboy::FxInstanceType, or a splitter, ui::fxl::kKindSplitter*) at the end of the Main rack; it becomes the selected one. A type of the pool takes a free slot
        (false: the pool is full); a splitter is a module of the fixed chain that is switched on and moved to the end of Main (one that is on already is selected). */
    bool addEffect (IInstanceView& inst, int kind)
    {
        if (kind <= synthyboy::kFxTypeNone || kind >= ui::fxl::kNumKinds)
            return false;
        std::vector<synthyboy::FxWrite> writes;
        if (scope_ != 0)
            return addToBand (inst, kind);
        if (kind >= ui::fxl::kKindSplitterLh)
        {
            const Item item { false, 13 + (kind - ui::fxl::kKindSplitterLh) };
            if (! synthyboy::fxItemOn (plain_.data(), item))
            {
                synthyboy::fxSetOn (plain_.data(), item, true, writes);
                int inMain = 0;
                const synthyboy::FxRacks racks = synthyboy::fxRacks (plain_.data());
                for (const Item& o : racks.rack[0])
                    inMain += ! (o == item);
                synthyboy::fxMove (plain_.data(), item, 0, inMain, writes);
                applyWrites (inst, writes);
            }
            selectedItem_ = item;
            selectionChanged_ = true;
            return true;
        }
        const int newSlot = synthyboy::fxAddModule (plain_.data(), kind, 0, writes);
        if (newSlot < 0)
            return false;
        selectedItem_ = { true, newSlot };
        selectionChanged_ = true;
        applyWrites (inst, writes);
        return true;
    }

    /** A new effect of the type `kind` (1 .. 13) at the end of the band rack of the page: the module of instance 0 of that type (it is free: off, in no band) is moved into the rack, switched on and selected. */
    bool addToBand (IInstanceView& inst, int kind)
    {
        if (scope_ == 0 || kind <= synthyboy::kFxTypeNone || kind >= ui::fxl::kKindSplitterLh || ! moduleFree (kind - 1))
            return false;
        const Item item { false, kind - 1 };
        std::vector<synthyboy::FxWrite> writes;
        synthyboy::fxSetOn (plain_.data(), item, true, writes);
        synthyboy::fxMove (plain_.data(), item, scope_, static_cast<int> (synthyboy::fxRacks (plain_.data()).rack[static_cast<size_t> (scope_)].size()), writes);
        applyWrites (inst, writes);
        selectedItem_ = item;
        selectionChanged_ = true;
        return true;
    }

    /** Opens the chain of a band rack (3 .. 9): the chain bar then holds its cards; `item` is selected (none: the first one). */
    void enterBand (int rack, const Item& item)
    {
        scope_ = rack;
        selectedItem_ = item;
        selectedSlot_ = 0;
        selectionChanged_ = true;
        screen_.slotEditor.setActiveRow (0);
        qSlots_.forget();
    }
    /** The way up from a band: the chain of the splitter's rack, with the splitter selected (its page shows the lanes). */
    void leaveBand()
    {
        int kind = 0, band = 0;
        if (! ui::fxl::bandOfRack (scope_, kind, band))
            return;
        scope_ = 0;
        selectedItem_ = { false, 13 + (kind - ui::fxl::kKindSplitterLh) };
        selectedSlot_ = 0;
        selectionChanged_ = true;
        screen_.slotEditor.setActiveRow (0);
        qSlots_.forget();
    }
    /** The highest rack an item can be moved to: the ten racks for a module of instance 0 that is no splitter, else the buses. */
    static int rackLimit (const Item& item) noexcept { return rackChoices (item) - 1; }

    /** The lane of a band of the splitter page: a tap on a card selects it and opens the band, the power switches it, a drag moves it inside the lane; the tile and + ADD open the band. */
    void handleBand (const ui::Change& c, int band, int what, UiStack& stack, IInstanceView& inst)
    {
        if (cardItems_.empty() || selectedSlot_ < 0 || selectedSlot_ >= static_cast<int> (cardItems_.size()))
            return;
        const int splitterKind = kindOfItem (cardItems_[static_cast<size_t> (selectedSlot_)]);
        const int rack = ui::fxl::bandRack (splitterKind, band);
        if (rack < 0 || c.kind != ui::ChangeKind::Index)
            return;
        const std::vector<Item>& items = bandItems_[static_cast<size_t> (rack - ui::fxl::kFirstBandRack)];
        ui::FxBandBoard::Lane& lane = screen_.slotEditor.bands.lane[band];
        if (what == ui::FxBandBoard::kOpenId)
            enterBand (rack, { false, -1 });
        else if (what == ui::FxBandBoard::kAddId)
        {
            const ui::Rect anchor = lane.add.rect();
            enterBand (rack, { false, -1 });
            openTypeList (stack, anchor);
        }
        else if (what >= 0 && what < static_cast<int> (items.size()))
        {
            const Item item = items[static_cast<size_t> (what)];
            if (c.index == ui::FxSlotCard::kSelect)
                enterBand (rack, item);
            else if (c.index == ui::FxSlotCard::kTogglePower)
            {
                std::vector<synthyboy::FxWrite> writes;
                synthyboy::fxSetOn (plain_.data(), item, c.value >= 0.5, writes);
                applyWrites (inst, writes);
            }
            else if (c.index == ui::FxSlotCard::kMove)
            {
                int target = 0;   // the card the finger was released over (the nearest one)
                int best = 1 << 30;
                for (int i = 0; i < static_cast<int> (items.size()); ++i)
                {
                    const ui::Rect r = lane.cards[i].rect();
                    const int d = std::abs (static_cast<int> (c.value) - (r.x + r.w / 2));
                    if (d < best)
                    {
                        best = d;
                        target = i;
                    }
                }
                std::vector<synthyboy::FxWrite> writes;
                if (synthyboy::fxMove (plain_.data(), item, rack, target, writes)   /* it ends up where the card it was dropped on stood */)
                    applyWrites (inst, writes);
            }
        }
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& inst)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        // the list of the types or parameter choices
        if (c.id == kList && c.kind == K::Index)
        {
            if (listKind_ == ListKind::Type)
            {
                if (c.index >= 0 && c.index < static_cast<int> (listKinds_.size()))
                    addEffect (inst, listKinds_[static_cast<size_t> (c.index)]);   // the list holds the kinds the page offers (all of them in the Main chain, the free types in a band)
            }
            else if (listKind_ == ListKind::ParamChoice && listParam_ >= 0)
            {
                if (! cardItems_.empty() && selectedSlot_ >= 0 && selectedSlot_ < static_cast<int> (cardItems_.size()))
                {
                    const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
                    const int idx = paramIndexOf (item, listParam_);
                    const synthyboy::ParamInfo* pi = screen_.slotEditor.infoOf (listParam_);
                    if (idx >= 0 && pi != nullptr)
                    {
                        inst.writeParam (idx, static_cast<float> (synthyboy::toNormalised (*pi, static_cast<float> (c.index))));
                        if (std::strcmp (pi->key, "fxfilter.model") == 0)
                        {
                            const int newModel = c.index;
                            const int count = synthyboy::filters::filterModelStyleCount (newModel);
                            const int styleP = screen_.slotEditor.controlOfKey ("fxfilter.style");
                            const int styleIdx = styleP >= 0 ? paramIndexOf (item, styleP) : -1;
                            if (styleIdx >= 0 && count > 0)
                            {
                                const float curStyle = plainOfControl (item, inst, styleP);
                                if (static_cast<int> (std::lround (curStyle)) >= count)
                                {
                                    const synthyboy::ParamInfo* spi = screen_.slotEditor.infoOf (styleP);
                                    if (spi != nullptr)
                                        inst.writeParam (styleIdx, static_cast<float> (synthyboy::toNormalised (*spi, static_cast<float> (count - 1))));
                                }
                            }
                        }
                    }
                }
                listParam_ = -1;
            }
            listOpen_ = false;
            stack.back();
            return;
        }
        // the gallery of the empty chain: a card is a type (the number of the engine), the effect goes to the end of the Main rack and is selected
        if (c.id == kGallery && c.kind == K::Index)
        {
            addEffect (inst, c.index);
            return;
        }
        // the lanes of the bands on the page of a splitter
        {
            int band = 0, what = 0;
            if (screen_.slotEditor.hasBands() && screen_.slotEditor.bands.decode (c.id, band, what))
            {
                handleBand (c, band, what, stack, inst);
                return;
            }
        }
        if (c.id == kListBackdrop)
        {
            listParam_ = -1;
            listOpen_ = false;
            stack.back();
            return;
        }
        // the chain bar
        if (c.id >= kChainBase && c.id <= kChainBase + ui::FxChainBar::kMaxSlots + 1)
        {
            const int idx = c.id - kChainBase;
            if (idx == ui::FxChainBar::kMaxSlots + 1)
            {
                if (c.kind == K::Index && c.index == ui::FxSlotCard::kBack)
                    leaveBand();
                return;
            }
            if (idx == ui::FxChainBar::kMaxSlots)
            {
                if (c.kind == K::Index && c.index == ui::FxSlotCard::kAdd)
                    openTypeList (stack, screen_.chainBar.plusCard.rect());
                return;
            }
            if (idx < 0 || idx >= static_cast<int> (cardItems_.size()) || c.kind != K::Index)
                return;
            const Item item = cardItems_[static_cast<size_t> (idx)];
            if (c.index == ui::FxSlotCard::kSelect)
            {
                selectedSlot_ = idx;
                selectedItem_ = item;
                screen_.slotEditor.setActiveRow (0);
                qSlots_.forget();
            }
            else if (c.index == ui::FxSlotCard::kTogglePower)
            {
                std::vector<synthyboy::FxWrite> writes;
                synthyboy::fxSetOn (plain_.data(), item, c.value >= 0.5, writes);
                applyWrites (inst, writes);
            }
            else if (c.index == ui::FxSlotCard::kMove)
            {
                int target = static_cast<int> (cardItems_.size()) - 1;   // the card the finger was released over (a release behind the last card: the last one)
                for (int i = 0; i < static_cast<int> (cardItems_.size()); ++i)
                    if (static_cast<int> (c.value) < screen_.chainBar.cards[i].rect().right() + 3)
                    {
                        target = i;
                        break;
                    }
                const synthyboy::FxRacks racks = synthyboy::fxRacks (plain_.data());
                int rackOf = synthyboy::fxItemRack (plain_.data(), item);
                const Item at = cardItems_[static_cast<size_t> (target)];
                const int targetRack = synthyboy::fxItemRack (plain_.data(), at);
                int rackIdx = 0;
                for (const Item& o : racks.rack[static_cast<size_t> (std::clamp (targetRack, 0, rackLimit (item)))])
                {
                    if (o == at)
                        break;
                    if (! (o == item))
                        ++rackIdx;
                }
                (void) rackOf;
                std::vector<synthyboy::FxWrite> writes;
                if (synthyboy::fxMove (plain_.data(), item, std::clamp (targetRack, 0, rackLimit (item)), rackIdx, writes))
                {
                    applyWrites (inst, writes);
                    selectedItem_ = item;
                    selectionChanged_ = true;
                }
            }
            return;
        }
        // the page of the effect
        if (cardItems_.empty() || selectedSlot_ < 0 || selectedSlot_ >= static_cast<int> (cardItems_.size()))
            return;
        const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
        if (c.id == kEditorPower && c.kind == K::Index)
        {
            std::vector<synthyboy::FxWrite> writes;
            synthyboy::fxSetOn (plain_.data(), item, c.index != 0, writes);
            applyWrites (inst, writes);
        }
        else if (c.id == kEditorRack && c.kind == K::Index)
        {
            const synthyboy::FxRacks racks = synthyboy::fxRacks (plain_.data());
            const int target = std::clamp (c.index, 0, rackLimit (item));
            int count = 0;
            for (const Item& o : racks.rack[static_cast<size_t> (target)])
                count += ! (o == item);
            std::vector<synthyboy::FxWrite> writes;
            if (synthyboy::fxMove (plain_.data(), item, target, count, writes))
            {
                applyWrites (inst, writes);
                selectedItem_ = item;
                selectionChanged_ = true;
                scope_ = target >= ui::fxl::kFirstBandRack ? target : 0;   // the page follows the effect: into the band it went to, or back to the chain of the buses
            }
        }
        else if (c.id == kEditorPage && c.kind == K::Index)
            screen_.slotEditor.setPage (c.index);
        else if (c.id == kEditorRemove && c.kind == K::Request)
        {
            std::vector<synthyboy::FxWrite> writes;
            bool removed = false;
            if (item.pool)
                removed = synthyboy::fxRemoveSlot (plain_.data(), item.index, writes);
            else
            {   // a module of instance 0 is switched off and leaves its band (an off module in a bus is not on the page)
                synthyboy::fxSetOn (plain_.data(), item, false, writes);
                if (synthyboy::fxItemRack (plain_.data(), item) >= ui::fxl::kFirstBandRack && item.index < 13)
                    writes.push_back ({ synthyboy::fxTypeDescriptor (item.index + 1).rackIndex, 0.0f });
                removed = ! writes.empty();
            }
            if (removed)
            {
                applyWrites (inst, writes);
                selectedItem_ = { false, -1 };
                selectedSlot_ = std::max (0, selectedSlot_ - 1);
                selectionChanged_ = true;
            }
        }
        else if (c.id >= kEditorBase && c.id < kEditorBase + ui::FxSlotEditor::kMaxParams)
        {
            const int p = c.id - kEditorBase;
            const int idx = paramIndexOf (item, p);
            if (c.kind == K::Value && idx >= 0)
            {
                uibind::writeNorm (inst, idx, c.value);
            }
            else if (c.kind == K::Index)
            {
                // a choice (c.index) or a number field (c.value, the number itself); the engine takes the normalised value
                const synthyboy::ParamInfo* pi = screen_.slotEditor.infoOf (p);
                if (idx >= 0 && pi != nullptr)
                {
                    const float plain = screen_.slotEditor.kindOf (p) == ui::FxSlotEditor::Kind::Number ? static_cast<float> (c.value) : static_cast<float> (c.index);
                    inst.writeParam (idx, static_cast<float> (synthyboy::toNormalised (*pi, plain)));
                    if (std::strcmp (pi->key, "fxfilter.model") == 0)
                    {
                        const int newModel = static_cast<int> (std::lround (plain));
                        const int count = synthyboy::filters::filterModelStyleCount (newModel);
                        const int styleP = screen_.slotEditor.controlOfKey ("fxfilter.style");
                        const int styleIdx = styleP >= 0 ? paramIndexOf (item, styleP) : -1;
                        if (styleIdx >= 0 && count > 0)
                        {
                            const float curStyle = plainOfControl (item, inst, styleP);
                            if (static_cast<int> (std::lround (curStyle)) >= count)
                            {
                                const synthyboy::ParamInfo* spi = screen_.slotEditor.infoOf (styleP);
                                if (spi != nullptr)
                                    inst.writeParam (styleIdx, static_cast<float> (synthyboy::toNormalised (*spi, static_cast<float> (count - 1))));
                            }
                        }
                    }
                }
            }
            else if (c.kind == K::Request)
            {
                openParamChoiceList (p, stack, inst);
            }
        }
    }

    int w_ = kCanvasW, h_ = kCanvasH;
    ui::FxFullScreen screen_;
    ui::ListPopup list_;
    enum class ListKind : uint8_t { Type, ParamChoice };
    ListKind listKind_ = ListKind::Type;
    int listParam_ = -1;
    bool listOpen_ = false, selectionChanged_ = true;
    int selectedSlot_ = 0;
    Item selectedItem_ { false, -1 };
    std::vector<Item> cardItems_;
    std::vector<Item> bandItems_[ui::fxl::kNumBandRacks];   // the modules of the band racks 3 .. 9 (the lanes of the page of a splitter)
    int scope_ = 0;                                         // 0: the chain of Main / Bus 1 / Bus 2; 3 .. 9: the chain of a band rack
    uint32_t gallerySig_ = 0;
    int galleryScope_ = -1;
    std::vector<int> listKinds_;                            // the kind each row of the + ADD list adds
    std::vector<float> plain_;
    const synthyboy::LiveValues* live_ = nullptr;   // the readout of the last update (the meters)
    uint32_t chainSig_ = 0;
    int orderIdx_[16] = {};
    const char* typeNames_[ui::fxl::kNumKinds - 1] = {};
    std::vector<const char*> listNames_;

    // the response of the FX filter
    std::unique_ptr<synthyboy::FilterResponse> response_;
    synthyboy::ParamValues filterValues_ = synthyboy::ParamValues::defaults();
    float filterDb_[256] = {}, filterVals_[17] = {};
    double filterRate_ = 0.0;
    bool haveFilter_ = false;

    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false;
    QSlots qSlots_;
    char qName_[24] = {};
};

}} // namespace synthyboy_mpc::overlay
