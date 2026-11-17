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
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../q_router.h"
#include "../ui/fx_screen.h"
#include "../ui/list.h"
#include "../ui/ui.h"
#include "curve_source.h"
#include "page_view.h"
#include "q_publish.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "ui_stack.h"
#include "synthyboy/filter_response.h"
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
        setCanvas (kCanvasW, kCanvasH);
        int galleryTypes[synthyboy::kNumFxInstanceTypes - 1];
        for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
        {
            typeNames_[t - 1] = synthyboy::fxTypeDescriptor (t).name;
            galleryTypes[t - 1] = t;
        }
        screen_.gallery.setTypes (galleryTypes, typeNames_, synthyboy::kNumFxInstanceTypes - 1);
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
        const uint32_t sig = structureSignature();
        if (sig != chainSig_ || selectionChanged_)
        {
            chainSig_ = sig;
            selectionChanged_ = false;
            rebuildChain();
        }
        pullCards();
        pullEditor (inst);
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

    /** The table index of parameter p of the selected effect (-1 none) and its name / unit descriptor (null none). */
    int paramIndexOf (const synthyboy::FxRackItem& item, int p) const noexcept
    {
        const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (synthyboy::fxItemType (plain_.data(), item));
        if (p < 0 || p >= desc.count)
            return -1;
        return item.pool ? synthyboy::fxInstanceKeys (item.index).p[p] : desc.source[p];
    }

private:
    using Item = synthyboy::FxRackItem;
    static constexpr int kMaxItems = ui::FxChainBar::kMaxSlots;

    uint32_t structureSignature() const noexcept
    {
        uint32_t h = 2166136261u;
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
        return h;
    }

    /** The cards: every effect of the pool, and the modules of the fixed chain that are on (or selected), rack by rack in processing order. */
    void rebuildChain()
    {
        const synthyboy::FxRacks racks = synthyboy::fxRacks (plain_.data());
        cardItems_.clear();
        for (int r = 0; r < synthyboy::kNumPoolRacks && static_cast<int> (cardItems_.size()) < kMaxItems; ++r)
            for (const synthyboy::FxRackItem& item : racks.rack[static_cast<size_t> (r)])
            {
                if (static_cast<int> (cardItems_.size()) >= kMaxItems)
                    break;
                if (item.pool)
                {
                    if (synthyboy::fxItemType (plain_.data(), item) != synthyboy::kFxTypeNone)
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

    void pullCards()
    {
        const int numActive = static_cast<int> (cardItems_.size());
        screen_.chainBar.setNumActiveSlots (numActive);
        for (int i = 0; i < numActive; ++i)
        {
            ui::FxSlotCard& card = screen_.chainBar.cards[i];
            const Item item = cardItems_[static_cast<size_t> (i)];
            card.state.slot = i;
            card.state.type = synthyboy::fxItemType (plain_.data(), item);
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
        screen_.chainBar.plusCard.setEnabled (numActive < ui::FxChainBar::kMaxSlots);
        for (ui::FxSlotCard& sp : screen_.chainBar.spare)
            sp.setEnabled (numActive < ui::FxChainBar::kMaxSlots);
    }

    /** The value of the parameter with the key `key` of the selected effect (plain), or `def` when the effect has no such parameter. */
    float plainOfKey (const Item& item, int type, IInstanceView& inst, const char* key, float def) const
    {
        const int idx = uibind::indexOfKey (key);
        const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (type);
        for (int p = 0; p < desc.count; ++p)
            if (desc.source[p] == idx)
            {
                if (item.pool)
                {
                    const int pi = synthyboy::fxInstanceKeys (item.index).p[p];
                    return pi >= 0 ? synthyboy::fxInstancePlain (type, p, inst.param (pi)) : def;
                }
                return inst.param (idx);
            }
        return def;
    }

    /** The name of a knob: the parameter's own name without the name of its effect in front ("Delay Time" -> TIME), in capitals. */
    static void knobName (const char* name, const char* typeName, char* out, size_t cap)
    {
        std::string s = name;
        const std::string t = typeName;
        const char* prefixes[] = { t.c_str(), "Compressor", "Distortion", "EQ", "Delay", "Chorus", "Flanger", "Phaser", "Reverb", "Filter", "FX Filter", "Hyper", "Utility", "Bode", "Convolve" };
        for (const char* pre : prefixes)
        {
            const size_t n = std::strlen (pre);
            if (n > 0 && s.size() > n + 1 && s.compare (0, n, pre) == 0 && s[n] == ' ')
            {
                s = s.substr (n + 1);
                break;
            }
        }
        for (char& c : s)
            c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
        std::snprintf (out, cap, "%.23s", s.c_str());
    }

    void pullEditor (IInstanceView& inst)
    {
        if (cardItems_.empty() || selectedSlot_ >= static_cast<int> (cardItems_.size()))
            return;
        const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
        const int type = synthyboy::fxItemType (plain_.data(), item);
        ui::FxSlotEditor& ed = screen_.slotEditor;
        ed.setType (type, item.pool);
        const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (type);
        for (int p = 0; p < desc.count && p < ui::FxSlotEditor::kMaxParams; ++p)
        {
            int paramIdx = -1;
            const synthyboy::ParamInfo* pi = nullptr;
            float norm = 0.0f, plainVal = 0.0f;
            if (item.pool)
            {
                paramIdx = synthyboy::fxInstanceKeys (item.index).p[p];
                pi = synthyboy::fxInstanceSource (type, p);
                if (paramIdx >= 0)
                {
                    norm = inst.param (paramIdx);
                    plainVal = synthyboy::fxInstancePlain (type, p, norm);
                }
            }
            else
            {
                paramIdx = desc.source[p];
                if (paramIdx >= 0)
                {
                    pi = &synthyboy::paramTable()[paramIdx];
                    plainVal = inst.param (paramIdx);
                    norm = static_cast<float> (synthyboy::toNormalised (*pi, plainVal));
                }
            }
            ui::Knob& knob = ed.knobs[p];
            ui::Stepper& stepper = ed.steppers[p];
            ui::LedList& list = ed.lists[p];
            ui::LedToggle& toggle = ed.toggles[p];
            const bool isChoiceParam = ed.isChoice (p);
            knob.state.value = knob.state.modValue = norm;
            if (pi != nullptr)
            {
                char name[24];
                knobName (pi->name, desc.name, name, sizeof name);
                if (isChoiceParam)
                {
                    stepper.setCaption (name);
                    stepper.state.count = pi->numChoices;
                    const int curIdx = std::clamp (static_cast<int> (std::lround (plainVal)), 0, std::max (0, pi->numChoices - 1));
                    stepper.state.index = curIdx;
                    if (pi->choices != nullptr && curIdx >= 0 && curIdx < pi->numChoices)
                        stepper.setItem (pi->choices[curIdx]);
                    else
                        stepper.setItem ("---");
                    // the same value as a LED list (3 .. 8 values, all visible) and as a LED toggle (Off / On): the editor shows the kind the cell has
                    list.setTitle (name);
                    list.setOptions (pi->choices, std::min (static_cast<int> (pi->numChoices), static_cast<int> (ui::LedList::kMaxOptions)));
                    list.state.index = std::min (curIdx, ui::LedList::kMaxOptions - 1);
                    toggle.setLabel (name);
                    toggle.state.on = curIdx == 1;
                }
                else
                {
                    knob.state.defaultValue = synthyboy::toNormalised (*pi, pi->def);
                    knob.state.bipolar = pi->min < 0.0f && pi->max > 0.0f;
                    paramText (*pi, plainVal, knob.state.valueText, sizeof knob.state.valueText);
                    knob.setLabel (name);
                }
            }
            const bool live = pi != nullptr && paramIdx >= 0;
            knob.setEnabled (live && ed.kindOf (p) == ui::FxSlotEditor::Kind::Knob);
            stepper.setEnabled (live && ed.kindOf (p) == ui::FxSlotEditor::Kind::Stepper);
            list.setEnabled (live && ed.kindOf (p) == ui::FxSlotEditor::Kind::List);
            toggle.setEnabled (live && ed.kindOf (p) == ui::FxSlotEditor::Kind::Toggle);
        }
        // the power, the rack
        ed.power.state.on = synthyboy::fxItemOn (plain_.data(), item);
        const int rack = synthyboy::fxItemRack (plain_.data(), item);
        ed.rack.state.count = 3;
        ed.rack.state.index = std::clamp (rack, 0, 2);
        static const char* const kRack[3] = { "MAIN", "BUS 1", "BUS 2" };
        ed.rack.setItem (rack >= 0 && rack < 3 ? kRack[rack] : "SPLITTER");
        pullDisplay (inst, item, type, ed.display);
    }

    /** The text of a value: the name of a choice, else the number with the parameter's unit. */
    static void paramText (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
    {
        if (pi.numChoices > 0 && pi.choices != nullptr)
        {
            const int n = std::clamp (static_cast<int> (std::lround (plain)), 0, pi.numChoices - 1);
            std::snprintf (out, cap, "%.15s", pi.choices[n]);
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
                // the band of the row of knobs that is on screen
                const int pa = screen_.slotEditor.activeRow() * 4;
                const int idx = paramIndexOfType (item, type, pa);
                const char* k = idx >= 0 ? synthyboy::paramTable()[idx].key : "";
                d.comp.activeBand = std::strstr (k, ".low.") != nullptr ? 0 : (std::strstr (k, ".high.") != nullptr ? 2 : 1);
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
    int paramIndexOfType (const Item& item, int type, int p) const noexcept
    {
        const synthyboy::FxTypeDescriptor& desc = synthyboy::fxTypeDescriptor (type);
        if (p < 0 || p >= desc.count)
            return -1;
        return desc.source[p] >= 0 ? desc.source[p] : -1;
        (void) item;
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

    /** The groups of the page are the rows of four values of the open effect that are on the page of the editor (the first and the second row, or three rows without a display), in reading order: the active row is
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
            name = ui::FxFullScreen::groupName (row);
            const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
            const int first = ed.firstRowOfPage(), n = std::max (0, std::min (ed.rowsPerPage(), ed.numRows() - first));
            const bool onPage = row >= first && row < first + n;
            const int count = onPage ? n : 1, base = onPage ? first : row, active = onPage ? row - first : 0;
            qSlots_.publish (inst, count, active, [&] (int k, int* t) { for (int c = 0; c < 4; ++c) t[c] = paramIndexOf (item, (base + k) * 4 + c); });
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

    void openTypeList (UiStack& stack)
    {
        listKind_ = ListKind::Type;
        listParam_ = -1;
        listNames_.clear();
        for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
            listNames_.push_back (typeNames_[t - 1]);
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.state.current = 0;
        const int popW = 300, popH = std::min (9, list_.state.count) * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad;
        const ui::Rect btn = screen_.chainBar.plusCard.rect();
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
        const int type = synthyboy::fxItemType (plain_.data(), item);
        const synthyboy::ParamInfo* pi = synthyboy::fxInstanceSource (type, p);
        if (pi == nullptr || pi->numChoices <= 0 || pi->choices == nullptr)
            return;
        listKind_ = ListKind::ParamChoice;
        listParam_ = p;
        listNames_.clear();
        for (int i = 0; i < pi->numChoices; ++i)
            listNames_.push_back (pi->choices[i]);
        list_.state.items = listNames_.data();
        list_.state.count = pi->numChoices;
        const int idx = paramIndexOf (item, p);
        int curChoice = 0;
        if (idx >= 0)
        {
            const float plainVal = item.pool ? synthyboy::fxInstancePlain (type, p, inst.param (idx)) : inst.param (idx);
            curChoice = std::clamp (static_cast<int> (std::lround (plainVal)), 0, pi->numChoices - 1);
        }
        list_.state.current = curChoice;
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

    /** A new effect of `type` (synthyboy::FxInstanceType) at the end of the Main rack; it becomes the selected one. false: the pool is full or the type is none. */
    bool addEffect (IInstanceView& inst, int type)
    {
        if (type <= synthyboy::kFxTypeNone || type >= synthyboy::kNumFxInstanceTypes)
            return false;
        std::vector<synthyboy::FxWrite> writes;
        const int newSlot = synthyboy::fxAddModule (plain_.data(), type, 0, writes);
        if (newSlot < 0)
            return false;
        selectedItem_ = { true, newSlot };
        selectionChanged_ = true;
        applyWrites (inst, writes);
        return true;
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
                addEffect (inst, c.index + 1);
            }
            else if (listKind_ == ListKind::ParamChoice && listParam_ >= 0)
            {
                if (! cardItems_.empty() && selectedSlot_ >= 0 && selectedSlot_ < static_cast<int> (cardItems_.size()))
                {
                    const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
                    const int type = synthyboy::fxItemType (plain_.data(), item);
                    const int idx = paramIndexOf (item, listParam_);
                    const synthyboy::ParamInfo* pi = synthyboy::fxInstanceSource (type, listParam_);
                    if (idx >= 0 && pi != nullptr)
                    {
                        const float norm = synthyboy::fxInstanceNormalise (type, listParam_, static_cast<float> (c.index));
                        inst.writeParam (idx, norm);
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
        if (c.id == kListBackdrop)
        {
            listParam_ = -1;
            listOpen_ = false;
            stack.back();
            return;
        }
        // the chain bar
        if (c.id >= kChainBase && c.id <= kChainBase + ui::FxChainBar::kMaxSlots)
        {
            const int idx = c.id - kChainBase;
            if (idx == ui::FxChainBar::kMaxSlots)
            {
                if (c.kind == K::Index && c.index == ui::FxSlotCard::kAdd)
                    openTypeList (stack);
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
                const int cardW = std::max (1, screen_.chainBar.cards[0].rect().w + 6);
                const int target = std::clamp ((static_cast<int> (c.value) - screen_.chainBar.rect().x) / cardW, 0, static_cast<int> (cardItems_.size()) - 1);
                const synthyboy::FxRacks racks = synthyboy::fxRacks (plain_.data());
                int rackOf = synthyboy::fxItemRack (plain_.data(), item);
                const Item at = cardItems_[static_cast<size_t> (target)];
                const int targetRack = synthyboy::fxItemRack (plain_.data(), at);
                int rackIdx = 0;
                for (const Item& o : racks.rack[static_cast<size_t> (std::clamp (targetRack, 0, 2))])
                {
                    if (o == at)
                        break;
                    if (! (o == item))
                        ++rackIdx;
                }
                (void) rackOf;
                std::vector<synthyboy::FxWrite> writes;
                if (synthyboy::fxMove (plain_.data(), item, std::clamp (targetRack, 0, 2), rackIdx, writes))
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
            const int target = std::clamp (c.index, 0, 2);
            int count = 0;
            for (const Item& o : racks.rack[static_cast<size_t> (target)])
                count += ! (o == item);
            std::vector<synthyboy::FxWrite> writes;
            if (synthyboy::fxMove (plain_.data(), item, target, count, writes))
            {
                applyWrites (inst, writes);
                selectedItem_ = item;
                selectionChanged_ = true;
            }
        }
        else if (c.id == kEditorPage && c.kind == K::Index)
            screen_.slotEditor.setPage (c.index);
        else if (c.id == kEditorRemove && c.kind == K::Request && item.pool)
        {
            std::vector<synthyboy::FxWrite> writes;
            if (synthyboy::fxRemoveSlot (plain_.data(), item.index, writes))
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
                const int type = synthyboy::fxItemType (plain_.data(), item);
                const synthyboy::ParamInfo* pi = synthyboy::fxInstanceSource (type, p);
                if (idx >= 0 && pi != nullptr)
                {
                    const float norm = synthyboy::fxInstanceNormalise (type, p, static_cast<float> (c.index));
                    inst.writeParam (idx, norm);
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
    std::vector<float> plain_;
    const synthyboy::LiveValues* live_ = nullptr;   // the readout of the last update (the meters)
    uint32_t chainSig_ = 0;
    int orderIdx_[16] = {};
    const char* typeNames_[synthyboy::kNumFxInstanceTypes - 1] = {};
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
