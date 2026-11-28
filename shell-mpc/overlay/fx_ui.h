// SynthyBoy - MPC overlay: the FX UI, the page of the FX tab: the chain of the effects of the three racks (Main, Bus 1, Bus 2) and the page of the selected effect, wired to the engine's FX racks (fx_rack.h: the modules of
// the fixed chain and the slots of the pool) and to the parameter descriptors of the effect types (fx_types.h: the name, the range and the unit of every parameter come from the descriptor's source key: one source of
// truth). Owner decision 2026-11-27: the C++ layer has no UI elements. Every control of the page (the cards of the chain, the power, the rack, the panels, REMOVE, the cells of the effect, the gallery cards, the type menu)
// is a native component of the host skin; this page holds the DATA behind them (FxEditorModel: the table of the effect, the live controls, the rows and panels, the geometry the skin lays out) and the native bridge
// (the chain, the header and the cells), the Q-Link groups (one per row of the effect), the displays (ui::FxDisplay: EQ, compressor, delay, FX filter, distortion) and the pictures of the gallery. One object per (page
// view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick () / draw (), with the instance as IInstanceView. Docs: docs/MPC.md "Mixer and FX UI".
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows: the page is always open. The cards: a native tap selects, the power zone switches, a drag of the handle moves the card (fxMove: the rack and the position), the native menu adds an effect of the chosen type at the
// end of the Main rack. The page of an effect has its power switch, its rack (the effect moves to another rack), the pages of its cells, REMOVE (the slots of the pool: the modules of the fixed chain are switched off instead),
// the display and the cells in rows of four: one Q group per row. An empty chain has no effect to edit: the page is the GALLERY of the effect types the engine has (a native card with a picture per type); one tap on a card
// adds that effect at the end of the Main rack and selects it.
// THE RACKS OF THE BANDS: the page of a splitter has the data of one lane per band (FxBandData: the tile and the chain of the band). The commands of a lane (openBand, bandAdd, bandSelect, bandPower, bandMove) open the chain
// of the band (scope_): the chain bar then holds the cards of that band (engine racks 3 .. 9; only the 13 modules of instance 0 that are no splitters sit in them), a BACK card first (the way up to the splitter), and the page
// of the selected effect is the same table page as in the Main chain; the place stands over the name ("MAIN CHAIN > SPLITTER L/M/H > MID BAND"). The chain offers the types whose module is free (off and not in a band), moves the
// module into the band and switches it on; a drag reorders; the rack switch of the page moves the effect to any of the ten racks; REMOVE switches the effect off and takes it out of its band. The lanes are the native BandLane component (components/native/band_lane, handles nativeui::fxbands): the tile and ADD open the band, a card selects or switches, a drag of the MOVE grip of a card reorders it (the commands below).
#pragma once

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../native_fx_bands.h"
#include "../native_fx_chain.h"
#include "../native_fx_cells.h"
#include "../native_fx_header.h"
#include "../q_router.h"
#include "../ui/binding.h"
#include "../ui/choice_names.h"
#include "../ui/content_name.h"
#include "../ui/fx_gallery_picture.h"
#include "../ui/native_apertures.h"
#include "fx_editor_model.h"
#include "instance_view.h"
#include "native_marker_covers.h"
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

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;

    enum class DrawPass : uint8_t { All, LiveOnly };

    // ---- the geometry of the native chain bar (the rectangles the plugin leaves open and reads a drag on) ---------------------------------------------------------------------------------------------------------
    static constexpr int kNativeCell = 56, kNativeGap = 4, kChainPad = 8;
    static constexpr int kChainSlots = 16;
    ui::Rect chainRect() const noexcept { return { kChainPad, kTabY + kChainPad, w_ - 2 * kChainPad, 2 * kNativeCell + 2 * kNativeGap }; }
    ui::Rect dragRect() const noexcept { const auto r = chainRect(); return { r.x, r.y, kNativeCell, kNativeCell }; }
    ui::Rect nativeBackRect() const noexcept { const auto r = chainRect(); return { r.x, r.y + kNativeCell + kNativeGap, kNativeCell, kNativeCell }; }
    ui::Rect nativeAddRect() const noexcept { const auto r = chainRect(); return { r.right() - 68, nativeBackRect().y, 68, kNativeCell }; }
    ui::Rect nativePageRect() const noexcept { const auto r = chainRect(); return { r.right() - 110, r.y, kNativeCell, kNativeCell }; }
    ui::Rect nativeSlotRect (int i) const noexcept
    {
        const int start = dragRect().right() + kNativeGap;
        const int width = (nativePageRect().x - start) / kChainSlots;
        const auto r = chainRect();
        return { start + i * width, r.y, width - kNativeGap, 2 * kNativeCell + kNativeGap };
    }
    ui::Rect nativePowerRect (int i) const noexcept { const auto r = nativeSlotRect (i); return { r.x, r.y, r.w, kNativeCell }; }
    ui::Rect nativeSelectRect (int i) const noexcept { const auto r = nativeSlotRect (i); return { r.x, r.bottom() - kNativeCell, r.w, kNativeCell }; }
    // ---- the native band lanes of a splitter page (nativeui::fxbands::geometry: the skin's fxNativeBandLane entries; screen coordinates) -------------------------------------------------------------------------
    static_assert (kTabY + nativeui::fxbands::geometry::kY == FxEditorModel::kEditorY + 2 * FxEditorModel::kMargin + FxEditorModel::kHeaderH, "the lanes stand where the display of a splitter page stands");
    ui::Rect bandLaneRect (int lane) const noexcept
    {
        namespace g = nativeui::fxbands::geometry;
        return { g::kX, kTabY + g::kY + lane * g::kPitch, g::kAddX + g::kAddW, g::kHeight };
    }
    ui::Rect bandTileRect (int lane) const noexcept { const auto r = bandLaneRect (lane); return { r.x, r.y, nativeui::fxbands::geometry::kTileW, r.h }; }
    ui::Rect bandAddRect (int lane) const noexcept { const auto r = bandLaneRect (lane); return { r.x + nativeui::fxbands::geometry::kAddX, r.y, nativeui::fxbands::geometry::kAddW, r.h }; }
    ui::Rect bandCardRect (int lane, int card) const noexcept
    {
        namespace g = nativeui::fxbands::geometry;
        const auto r = bandLaneRect (lane);
        return { r.x + g::kCardX + card * g::kCardPitch, r.y, g::kCardW, r.h };
    }
    ui::Rect bandPowerRect (int lane, int card) const noexcept { auto r = bandCardRect (lane, card); r.h = nativeui::fxbands::geometry::kPowerH; r.w = nativeui::fxbands::geometry::kGripX; return r; }
    ui::Rect bandGripRect (int lane, int card) const noexcept
    {
        namespace g = nativeui::fxbands::geometry;
        const auto r = bandCardRect (lane, card);
        return { r.x + g::kGripX, r.y, g::kGripW, g::kGripH };
    }
    ui::Rect bandSelectRect (int lane, int card) const noexcept
    {
        namespace g = nativeui::fxbands::geometry;
        const auto r = bandCardRect (lane, card);
        return { r.x, r.y + g::kPowerH, r.w, r.h - g::kPowerH };
    }
    bool bandsShown() const noexcept { return editorShown_ && editor_.hasBands() && editor_.bands.numBands > 0 && scope_ == 0; }
    /** The area of the gallery of an empty chain (under the chain bar). */
    ui::Rect galleryRect() const noexcept
    {
        const int topY = kTabY + kChainPad + 2 * kNativeCell + 2 * kNativeGap + kChainPad;
        return { kChainPad, topY, w_ - 2 * kChainPad, std::max (100, kTabY + kTabH - topY - kChainPad) };
    }

    FxUi()
    {
        plain_.assign (synthyboy::paramCount(), 0.0f);
        cardItems_.reserve (2 * kMaxItems);
        setCanvas (kCanvasW, kCanvasH);
        for (int k = 1; k < ui::fxl::kNumKinds; ++k)
            typeNames_[k - 1] = ui::fxl::layoutOf (k).name;   // the 13 types of the pool, then the three splitters
        setNativeControls (true);
    }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        editor_.setCanvas (w);
    }

    FxEditorModel& editor() noexcept { return editor_; }
    const FxEditorModel& editor() const noexcept { return editor_; }

    int selectedSlot() const noexcept { return selectedSlot_; }
    int numCards() const noexcept { return static_cast<int> (cardItems_.size()); }
    const synthyboy::FxRackItem& cardItem (int i) const noexcept { return cardItems_[static_cast<size_t> (i)]; }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    void setHostBackground (bool on) noexcept { drawPass_ = on ? DrawPass::LiveOnly : DrawPass::All; }
    bool hostBackground() const noexcept { return &ui::theme::L() == &ui::theme::kLook1 && drawPass_ == DrawPass::LiveOnly; }
    void setNativeControls (bool on) noexcept { nativeControls_ = on; setHostBackground (on); }
    bool nativeControls() const noexcept { return nativeControls_; }
    void leftTab (IInstanceView& inst) noexcept
    {
        inst.setNativeFxCells (0, nullptr);
        inst.setNativeFxHeader ({});
        inst.setNativeFxChain ({});
        inst.setNativeFxBands ({});
    }
    void setNativeActionPopup (IInstanceView& inst, int action) noexcept
    {
        inst.setNativeFxRackPopup (action == 0);
        inst.setNativeFxAddPopup (action == 1);
    }
    int topPage() const noexcept { return 0; }
    /** The chain on the page: 0 the Main / Bus chain, 3 .. 9 the chain of that band rack (fxl::bandOfRack). */
    int scope() const noexcept { return scope_; }
    /** The modules of a band rack (3 .. 9) in processing order, as of the last update. */
    const std::vector<synthyboy::FxRackItem>& bandItems (int rack) const noexcept { return bandItems_[static_cast<size_t> (std::clamp (rack, ui::fxl::kFirstBandRack, ui::fxl::kNumRacks - 1) - ui::fxl::kFirstBandRack)]; }
    const synthyboy::FxRackItem& selectedItem() const noexcept { return selectedItem_; }
    bool galleryShown() const noexcept { return ! editorShown_ && (galleryN_ > 0 || galleryNote_[0] != '\0'); }
    bool editorShown() const noexcept { return editorShown_; }

    /** Reads the instance: the chain (only when its structure changed), the selected effect with its cells, display data and Q group. */
    void update (IInstanceView& inst, const UiStack& stack, double nowMs, const page::Input& in)
    {
        (void) nowMs;
        (void) stack;
        live_ = in.live;
        // the structure of the chain: a signature of the parameters that decide it
        readRackStructure (inst);
        const uint64_t generation = inst.stateGeneration();
        const bool sameInstance = lastInstance_ == &inst;
        const bool bindingChanged = generation != stateGeneration_ || ! sameInstance;
        const uint32_t sig = structureSignature();
        if (bindingChanged)
        {
            leftTab (inst);
            const bool preserve = sameInstance && renderedEditorStillLive();
            selectedItem_ = preserve ? renderedItem_ : Item { false, -1 };
            if (! preserve)
                editor_.setPage (0);
            stateGeneration_ = generation;
            lastInstance_ = &inst;
            selectedSlot_ = 0;
            selectionChanged_ = true;
            galleryOpen_ = false;
            chainPage_ = 0;
            dropGesture();
            qSlots_.forget();
        }
        refreshEditor (inst, sig);
        // The native editor uses the regular instance marker: the page never needs to mask the underlying tab.
        inst.setUiFullOpen (false);
        if (syncNativeChain (inst)
            || syncNativeBands (inst)
            || syncNativeHeader (inst))
        {
            readRackStructure (inst);
            refreshEditor (inst, structureSignature());
        }
        inst.setNativeFxChain (nativeChain());
        inst.setNativeFxBands (nativeBands());
        inst.setNativeFxHeader (nativeHeader());
        syncNativeCells (inst);
        publishQGroup (inst);
    }

    void drawPageGround (gpu::SceneBuilder&) const noexcept {}

    void draw (gpu::SceneBuilder& sb) const
    {
        if (! hostBackground())
        {
            page::ground (sb);
            clearNativeApertures (sb);
        }
        else
            drawNativeMarkerCovers (sb, "fx");
        if (editorShown_)
        {
            // the display (the graph the host cannot draw): its static part, then the live parts
            if (editor_.hasDisplay() && editor_.display.kind != ui::FxDisplay::Kind::None)
            {
                editor_.display.draw (sb);
                editor_.display.drawLive (sb);
            }
        }
        else if (galleryShown())
            drawGallery (sb);
    }

    bool covers (int x, int y) const noexcept
    {
        const ui::Rect bounds { 0, kTabY, w_, kTabH };
        const bool inTab = bounds.contains (x, y);
        if (! inTab)
            return false;
        bool found = editorShown_ && editor_.isNativeControl (x, y);
        visitNativeChainControls ([&] (ui::Rect area) { found = found || area.contains (x, y); });
        return ! found;
    }

    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        (void) stack;
        (void) nowMs;
        if (inst.stateGeneration() != stateGeneration_)
        {
            const bool held = gestureDown_;
            dropGesture();
            return held;
        }
        if (ev.kind == TouchEvent::Down)
        {
            dropGesture();
            if (editorShown_ && editor_.nativeBandRect().contains (ev.x, ev.y))
                editor_.setActiveRow (editor_.rowOfControl (FxEditorModel::kSel));
            if (! covers (ev.x, ev.y))
                return false;
            dragSignature_ = chainSig_;
            if (editorShown_ && dragEnabled_ && dragRect().contains (ev.x, ev.y))
            {
                draggedItem_ = selectedItem_;
                dragOverPage_ = false;
                gestureDown_ = true;
                return true;
            }
            if (bandsShown())
                for (int l = 0; l < editor_.bands.numBands && bandDrag_.lane < 0; ++l)
                    for (int i = 0; i < editor_.bands.lane[l].numCards; ++i)
                        if (editor_.bands.lane[l].cards[i].on && bandGripRect (l, i).contains (ev.x, ev.y))
                        {
                            bandDrag_ = { l, i };   // the MOVE grip of a card of a lane: the drop picks the place in that lane
                            break;
                        }
            gestureDown_ = true;   // the touch of a part of the page the overlay owns (the display, a MOVE grip): swallowed
            return true;
        }
        if (bandDrag_.lane >= 0)
        {
            if (ev.kind == TouchEvent::Up)
            {
                const int lane = bandDrag_.lane, card = bandDrag_.card, count = editor_.bands.lane[lane].numCards;
                int target = card, best = 1 << 30;   // the card the finger was released over (the nearest one)
                for (int i = 0; i < count; ++i)
                {
                    const ui::Rect r = bandCardRect (lane, i);
                    const int d = std::abs (ev.x - (r.x + r.w / 2));
                    if (d < best)
                    {
                        best = d;
                        target = i;
                    }
                }
                if (target != card)
                    bandMove (inst, lane, card, target);
                dropGesture();
            }
            return true;
        }
        if (draggedItem_.index >= 0)
        {
            if (ev.kind == TouchEvent::Move)
            {
                const bool overPage = canPageChain() && nativePageRect().contains (ev.x, ev.y);
                if (overPage && ! dragOverPage_)
                    chainPage_ ^= 1;
                dragOverPage_ = overPage;
            }
            if (ev.kind == TouchEvent::Up)
            {
                if (canMoveCard (inst, draggedItem_))
                    for (int i = 0; i < visibleCards(); ++i)
                        if (nativeSlotRect (i).contains (ev.x, ev.y))
                        {
                            moveCard (inst, draggedItem_, firstCard() + i);
                            break;
                        }
                dropGesture();
            }
            return true;
        }
        if (! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Up)
            gestureDown_ = false;
        return true;
    }

    void tick (double, UiStack&, IInstanceView&) {}

    /** The mouse wheel: every control of the page is a native cell of the host; the overlay takes no wheel. */
    bool wheel (int, int, int, UiStack&, IInstanceView&, double) { return false; }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept
    {
        gestureDown_ = false;
        draggedItem_ = { false, -1 };
        bandDrag_ = {};
    }
    void forgetQGroup() noexcept { qSlots_.forget(); }
    /** The hardware knob `knobIndex` (0 .. 3) of the active row turned by `delta`: the member of the row in that slot. A knob moves by `delta` (normalised units), a number or a choice by whole steps, a toggle on / off by the sign. */
    void turnQ (int knobIndex, double delta, UiStack&, IInstanceView& inst)
    {
        if (! editorShown_ || ! editor_.powerOn || selectedSlot_ < 0 || selectedSlot_ >= numCards())
            return;
        const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
        const int p = editor_.memberControl (editor_.activeRow(), knobIndex);
        const int idx = p >= 0 ? paramIndexOf (item, p) : -1;
        const synthyboy::ParamInfo* pi = editor_.infoOf (p);
        if (idx < 0 || pi == nullptr)
            return;
        const float plain = plainOfControl (item, inst, p);
        if (editor_.isChoice (p) || editor_.baseKind (p) == FxEditorModel::Kind::Number)
        {
            const bool toggle = editor_.baseKind (p) == FxEditorModel::Kind::Toggle;
            const long step = toggle ? (delta > 0.0 ? 1 : (delta < 0.0 ? -1 : 0)) : std::clamp<long> (std::lround (delta), -100000, 100000);
            if (step == 0)
                return;
            if (editor_.isChoice (p))
            {
                int count = pi->numChoices;
                if (std::strcmp (pi->key, "fxfilter.style") == 0)
                    count = synthyboy::filters::filterModelStyleCount (static_cast<int> (std::lround (plainOfKey (item, kindOfItem (item), inst, "fxfilter.model", 0.0f))));
                const int next = binding::turnChoice (static_cast<int> (std::lround (plain)), static_cast<int> (step), std::max (1, count));
                writeChoice (inst, item, p, next);
            }
            else
            {
                const double next = std::clamp (std::round (static_cast<double> (plain)) + static_cast<double> (step), static_cast<double> (pi->min), static_cast<double> (pi->max));
                inst.writeParam (idx, static_cast<float> (synthyboy::toNormalised (*pi, static_cast<float> (next))));
            }
            return;
        }
        const double cur = synthyboy::toNormalised (*pi, plain);
        const double next = binding::turnNorm (cur, delta);
        if (std::fabs (next - cur) >= 1.0e-12)
            uibind::writeNorm (inst, idx, next);
    }
    bool listOpen() const noexcept { return false; }

    /** The table index of control p of the effect on the page (-1 none): the parameter of the slot of the pool for a control that is one of the type's own, else the engine's key of the control itself (the splitters,
        the modules of the fixed chain, the force enable of the reverb). The page is the one of `item` (the selected effect). */
    int paramIndexOf (const synthyboy::FxRackItem& item, int p) const noexcept
    {
        const synthyboy::ParamInfo* pi = editor_.infoOf (p);
        if (pi == nullptr)
            return -1;
        const int kind = kindOfItem (item);
        if (item.pool && kind < synthyboy::kNumFxInstanceTypes && p < synthyboy::fxTypeDescriptor (kind).count)
            return synthyboy::fxInstanceKeys (item.index).p[p];
        return static_cast<int> (pi - synthyboy::paramTable());
    }

    // ---- the commands of the native header (power, rack, panel, remove, band) and of the lanes of a splitter ------------------------------------------------------------------------------------------------
    /** POWER of the effect on the page. */
    void setPower (IInstanceView& inst, bool on) { if (selectedItem_.index >= 0) powerCard (inst, selectedItem_, on); }
    /** The rack switch of the page: the effect moves to rack `target` (the page follows it into a band or back to the buses). */
    void moveToRack (IInstanceView& inst, int target)
    {
        if (cardItems_.empty() || selectedSlot_ < 0 || selectedSlot_ >= numCards())
            return;
        const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
        const synthyboy::FxRacks racks = synthyboy::fxRacks (plain_.data());
        target = std::clamp (target, 0, rackLimit (item));
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
    /** REMOVE: a slot of the pool is freed, a module of instance 0 is switched off and leaves its band. */
    void removeSelected (IInstanceView& inst)
    {
        if (cardItems_.empty() || selectedSlot_ < 0 || selectedSlot_ >= numCards())
            return;
        const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
        std::vector<synthyboy::FxWrite> writes;
        bool removed = false;
        if (item.pool)
        {
            readSlotParameters (inst, item.index);
            removed = synthyboy::fxRemoveSlot (plain_.data(), item.index, writes);
        }
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
    /** A cell of an effect wrote the plain value `index` of a choice (a model change of the FX filter clamps the style to the styles of the model). */
    void writeChoice (IInstanceView& inst, const synthyboy::FxRackItem& item, int p, int index)
    {
        const synthyboy::ParamInfo* pi = editor_.infoOf (p);
        const int idx = paramIndexOf (item, p);
        if (idx < 0 || pi == nullptr)
            return;
        inst.writeParam (idx, static_cast<float> (synthyboy::toNormalised (*pi, static_cast<float> (index))));
        if (std::strcmp (pi->key, "fxfilter.model") == 0)
            clampStyle (inst, item, filtersStyleCount (index));
    }

    /** The lane of band `band` of the splitter on the page: the chain of the band opens. */
    void openBand (int band)
    {
        const int rack = bandRackOfSelected (band);
        if (rack >= 0)
            enterBand (rack, { false, -1 });
    }
    /** + ADD of a lane: the chain of the band opens with the gallery of the types it can still take. */
    void bandAdd (int band)
    {
        const int rack = bandRackOfSelected (band);
        if (rack < 0)
            return;
        enterBand (rack, { false, -1 });
        galleryOpen_ = true;
    }
    /** A tap on card `card` of a lane: the band opens with that effect selected. */
    void bandSelect (int band, int card)
    {
        const int rack = bandRackOfSelected (band);
        if (rack < 0)
            return;
        const std::vector<Item>& items = bandItems_[static_cast<size_t> (rack - ui::fxl::kFirstBandRack)];
        if (card >= 0 && card < static_cast<int> (items.size()))
            enterBand (rack, items[static_cast<size_t> (card)]);
    }
    /** The power of card `card` of a lane. */
    void bandPower (IInstanceView& inst, int band, int card, bool on)
    {
        const int rack = bandRackOfSelected (band);
        if (rack < 0)
            return;
        const std::vector<Item>& items = bandItems_[static_cast<size_t> (rack - ui::fxl::kFirstBandRack)];
        if (card >= 0 && card < static_cast<int> (items.size()))
        {
            std::vector<synthyboy::FxWrite> writes;
            synthyboy::fxSetOn (plain_.data(), items[static_cast<size_t> (card)], on, writes);
            applyWrites (inst, writes);
        }
    }
    /** A card of a lane dropped on position `target` of the same lane. */
    void bandMove (IInstanceView& inst, int band, int card, int target)
    {
        const int rack = bandRackOfSelected (band);
        if (rack < 0)
            return;
        const std::vector<Item>& items = bandItems_[static_cast<size_t> (rack - ui::fxl::kFirstBandRack)];
        if (card < 0 || card >= static_cast<int> (items.size()) || ! canMoveCard (inst, items[static_cast<size_t> (card)]))
            return;
        std::vector<synthyboy::FxWrite> writes;
        if (synthyboy::fxMove (plain_.data(), items[static_cast<size_t> (card)], rack, std::clamp (target, 0, static_cast<int> (items.size()) - 1), writes))
            applyWrites (inst, writes);
    }


private:
    using Item = synthyboy::FxRackItem;
    static constexpr int kMaxItems = 16;   // the cards of the chain bar (nativeui::fxchain::kSlots)
    int firstCard() const noexcept { return chainPage_ * kMaxItems; }
    int visibleCards() const noexcept { return std::clamp (numCards() - firstCard(), 0, kMaxItems); }
    bool canPageChain() const noexcept { return numCards() > kMaxItems && ! galleryOpen_; }

    uint32_t structureSignature() const noexcept
    {
        uint32_t h = 2166136261u ^ static_cast<uint32_t> (scope_ * 7919);
        for (int index : synthyboy::fxRackStructureIndices())
        {
            uint32_t bits;
            const float value = plain_[static_cast<size_t> (index)];
            std::memcpy (&bits, &value, sizeof bits);
            h = (h ^ bits) * 16777619u;
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
        const int maxItems = 2 * kMaxItems;
        for (int r = 0; scope_ == 0 && r < synthyboy::kNumPoolRacks && numCards() < maxItems; ++r)
            for (const synthyboy::FxRackItem& item : racks.rack[static_cast<size_t> (r)])
            {
                if (numCards() >= maxItems)
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
        const bool limited = scope_ == 0 && poolFull();
        for (int k = 1; k < ui::fxl::kNumKinds; ++k)
        {
            bool offered = true;
            if (scope_ != 0)
                offered = k < ui::fxl::kKindSplitterLh && moduleFree (k - 1);
            else if (limited)
                offered = k >= ui::fxl::kKindSplitterLh && ! synthyboy::fxItemOn (plain_.data(), { false, k - 1 });
            if (offered)
                out[n++] = k;
        }
        return n;
    }

    /** The gallery of an empty chain offers what the native add menu offers; in a band its headline says where the effect goes. */
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
        galleryN_ = std::min (n, ui::fxgallery::kMaxTypes);
        for (int i = 0; i < galleryN_; ++i)
            galleryKinds_[i] = kinds[i];
        std::snprintf (galleryHeadline_, sizeof galleryHeadline_, "TAP AN EFFECT TO ADD IT TO THE CHAIN");
        galleryNote_[0] = '\0';
        int kind = 0, band = 0;
        if (scope_ != 0 && ui::fxl::bandOfRack (scope_, kind, band))
        {
            char sp[24];
            upper (sp, sizeof sp, ui::fxl::layoutOf (kind).name);
            std::snprintf (galleryHeadline_, sizeof galleryHeadline_, "%s BAND OF %s: TAP AN EFFECT TO ADD IT", ui::fxl::bandName (kind, band), sp);
            std::snprintf (galleryNote_, sizeof galleryNote_, "%s BAND OF %s: EVERY EFFECT IS IN USE (MOVE ONE HERE WITH ITS RACK SWITCH)", ui::fxl::bandName (kind, band), sp);
        }
        else
            std::snprintf (galleryNote_, sizeof galleryNote_, "NO EFFECT TO ADD");
    }
    /** The rectangle of the native gallery card of kind index `i` of the offered ones. */
    ui::Rect galleryCardRect (int i) const noexcept
    {
        return i >= 0 && i < galleryN_ ? ui::fxgallery::cardRectOfType (galleryRect(), galleryKinds_[i]) : ui::Rect {};
    }
    /** The headline (or the note when no type is offered) and the pictures of the cards of the gallery: the part of it the host cannot draw. */
    void drawGallery (gpu::SceneBuilder& sb) const
    {
        const ui::Rect area = galleryRect();
        ui::Pen p (sb, true);
        ui::Cell cell (sb, area);
        const ui::theme::Look& T = ui::theme::L();
        p.textLeft ({ area.x + ui::fxgallery::kPad, area.y + 4, std::max (0, area.w - 2 * ui::fxgallery::kPad), ui::fxgallery::kHeadH - 8 }, galleryN_ == 0 ? galleryNote_ : galleryHeadline_,
                    T.fLabel + 2.0f, p.c (T.textDim, 255), T.fMin);
        for (int i = 0; i < galleryN_; ++i)
        {
            const ui::Rect plot = ui::fxgallery::pictureRect (galleryCardRect (i));
            if (plot.w >= 24 && plot.h >= 24)
                ui::fxgallery::drawPicture (p, sb, galleryKinds_[i], plot);
        }
    }


    /** The value (plain) of control p of the item, from the instance: a slot of the pool stores the normalised value of the type's own parameters (fxInstancePlain turns it into the plain value of its source), the other
        controls (the modules of the fixed chain, the splitters, the force enable of the reverb) are engine keys of their own. */
    float plainOfControl (const Item& item, IInstanceView& inst, int p) const
    {
        const synthyboy::ParamInfo* pi = editor_.infoOf (p);
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
        const int p = editor_.controlOfKey (key);
        return p >= 0 && paramIndexOf (item, p) >= 0 ? plainOfControl (item, inst, p) : def;
    }

    // Keep table indexing for the rack helpers. The editor reads its live controls directly.
    void readRackStructure (const IInstanceView& inst)
    {
        for (int index : synthyboy::fxRackStructureIndices())
            plain_[static_cast<size_t> (index)] = inst.param (index);
    }

    // ADD and REMOVE compare the slot payload to its defaults before emitting writes.
    void readSlotParameters (const IInstanceView& inst, int slot)
    {
        for (int index : synthyboy::fxInstanceKeys (slot).p)
            plain_[static_cast<size_t> (index)] = inst.param (index);
    }

    void refreshEditor (IInstanceView& inst, uint32_t signature)
    {
        if (signature != chainSig_ || selectionChanged_)
        {
            const bool followSelection = selectionChanged_;
            chainSig_ = signature;
            selectionChanged_ = false;
            rebuildChain();
            chainPage_ = followSelection ? selectedSlot_ / kMaxItems : std::min (chainPage_, std::max (0, numCards() - 1) / kMaxItems);
        }
        pullEditor (inst);
        refreshGallery();
        editorShown_ = ! cardItems_.empty() && ! galleryOpen_;
        renderedItem_ = editorShown_ ? selectedItem_ : Item { false, -1 };
        dragEnabled_ = editorShown_ && selectedItem_.index >= 0 && synthyboy::fxItemOn (plain_.data(), selectedItem_);
    }

    bool nativeAddAvailable() const noexcept
    {
        if (scope_ == 0 && ! poolFull())
            return true;
        int kinds[ui::fxl::kNumKinds];
        return offeredKinds (kinds) > 0;
    }
    bool poolFull() const noexcept { return synthyboy::fxFirstFreeSlot (plain_.data()) < 0; }

    template <typename Visit>
    void visitNativeChainControls (Visit&& visit) const
    {
        if (scope_ != 0 || galleryOpen_)
            visit (nativeBackRect());
        if (nativeAddAvailable() && ! galleryOpen_)
            visit (nativeAddRect());
        if (canPageChain())
            visit (nativePageRect());
        for (int i = 0; i < visibleCards(); ++i)
        {
            visit (nativePowerRect (i));
            visit (nativeSelectRect (i));
        }
        if (galleryShown())
            for (int i = 0; i < galleryN_; ++i)
                visit (galleryCardRect (i));
        if (bandsShown())
            for (int l = 0; l < editor_.bands.numBands; ++l)
            {
                const FxBandData::Lane& lane = editor_.bands.lane[l];
                visit (bandTileRect (l));
                if (lane.addEnabled)
                    visit (bandAddRect (l));
                for (int i = 0; i < lane.numCards; ++i)
                {
                    visit (bandPowerRect (l, i));
                    visit (bandSelectRect (l, i));
                }
            }
    }

    nativeui::fxbands::State nativeBands() const noexcept
    {
        namespace b = nativeui::fxbands;
        b::State state;
        if (! bandsShown())
            return state;
        state.shown = true;
        state.bands = editor_.bands.numBands;
        state.structure = chainSig_ ^ 0x5bd1e995u;
        for (int l = 0; l < state.bands; ++l)
        {
            const FxBandData::Lane& in = editor_.bands.lane[l];
            b::Lane& out = state.lane[static_cast<size_t> (l)];
            out.kind = in.kind;
            out.band = in.band;
            out.colour = in.colour;
            out.count = in.numCards;
            out.low = in.low;
            out.high = in.high;
            out.bypassed = in.bypassed;
            out.idle = in.idle;
            out.addAvail = in.addEnabled;
            for (int i = 0; i < in.numCards; ++i)
                out.cards[static_cast<size_t> (i)] = { in.cards[i].item, in.cards[i].type, in.cards[i].on };
        }
        return state;
    }

    /** The presses of the native lanes: the tile and ADD open the band, a card selects it or switches its power. */
    bool syncNativeBands (IInstanceView& inst)
    {
        namespace b = nativeui::fxbands;
        const auto state = nativeBands();
        if (! state.shown)
            return false;
        for (int handle = 0; handle < b::kHandles; ++handle)
        {
            float value;
            if (! inst.takeNativeFxBands (handle, state, value))
                continue;
            const int lane = handle / b::kLaneHandles, field = handle % b::kLaneHandles;
            bool applied = false;
            if (field == b::Open && value >= 0.5f)
            {
                openBand (lane);
                applied = true;
            }
            else if (field == b::Add && value >= 0.5f)
            {
                bandAdd (lane);
                applied = true;
            }
            else if (field >= b::Select && field < b::Power && value >= 0.5f)
            {
                bandSelect (lane, field - b::Select);
                applied = true;
            }
            else if (field >= b::Power && field < b::Power + b::kCards && field - b::Power < state.lane[static_cast<size_t> (lane)].count
                     && state.lane[static_cast<size_t> (lane)].cards[static_cast<size_t> (field - b::Power)].on != (value >= 0.5f))
            {
                bandPower (inst, lane, field - b::Power, value >= 0.5f);
                applied = true;
            }
            if (applied)
            {
                leftTab (inst);
                dropGesture();
                return true;
            }
        }
        return false;
    }


    nativeui::fxchain::State nativeChain() const noexcept
    {
        namespace c = nativeui::fxchain;
        c::State state;
        const bool gallery = galleryShown();
        int kinds[ui::fxl::kNumKinds];
        const int offered = offeredKinds (kinds);
        const bool add = offered > 0;
        if (galleryOpen_)
            state.mode = c::FilteredGallery;
        else if (scope_ == 0 && ! poolFull())
            state.mode = gallery ? c::MainGallery : add ? c::MainAdd : c::MainLocked;
        else if (scope_ == 0)
            state.mode = add ? c::FilteredAdd : c::MainLocked;
        else
            state.mode = gallery ? c::FilteredGallery : add ? c::FilteredAdd : c::BandLocked;
        state.scope = scope_;
        state.structure = chainSig_ ^ (static_cast<uint32_t> (chainPage_) * 2654435761u);
        state.count = visibleCards();
        state.page = chainPage_;
        state.canPage = canPageChain();
        const int selected = selectedSlot_ - firstCard();
        state.selected = selected >= 0 && selected < state.count ? selected + 1 : 0;
        for (int i = 0; i < offered; ++i)
            state.offered |= 1u << (kinds[i] - 1);
        for (int i = 0; i < state.count; ++i)
        {
            const auto& item = cardItem (firstCard() + i);
            state.cards[static_cast<size_t> (i)] = { item.index + (item.pool ? synthyboy::kNumFxSlots : 0),
                kindOfItem (item), synthyboy::fxItemRack (plain_.data(), item), synthyboy::fxItemOn (plain_.data(), item) };
        }
        return state;
    }

    bool syncNativeChain (IInstanceView& inst)
    {
        namespace c = nativeui::fxchain;
        const auto state = nativeChain();
        if (state.mode == c::Hidden)
            return false;
        float pageValue;
        // Process a window change before the position-bound buttons. A card
        // callback from the outgoing window must never follow its position.
        if (inst.takeNativeFxChain (c::Page, state, pageValue) && state.canPage && pageValue >= 0.25f)
        {
            const int next = pageValue < 0.75f ? 0 : 1;
            if (next != chainPage_)
            {
                chainPage_ = next;
                leftTab (inst);
                dropGesture();
                return true;
            }
        }
        for (int action = c::Select; action < c::Page; ++action)
        {
            float value;
            if (! inst.takeNativeFxChain (action, state, value))
                continue;
            bool applied = false;
            if (action < c::Power)
            {
                const int slot = action - c::Select;
                if (value >= 0.5f && slot < state.count && (firstCard() + slot != selectedSlot_ || galleryOpen_))
                {
                    galleryOpen_ = false;
                    selectCard (firstCard() + slot);
                    applied = true;
                }
            }
            else if (action < c::Gallery)
            {
                const int slot = action - c::Power;
                if (slot < state.count && state.cards[static_cast<size_t> (slot)].on != (value >= 0.5f))
                {
                    galleryOpen_ = false;
                    selectCard (firstCard() + slot);
                    powerCard (inst, selectedItem_, value >= 0.5f);
                    applied = true;
                }
            }
            else if (action <= c::Menu)
            {
                const int type = action == c::Menu ? static_cast<int> (std::lround (value * c::kTypes))
                                                  : value < 0.5f ? action - c::Gallery + 1 : 0;
                applied = type > 0 && state.available (type - 1) && addEffect (inst, type);
                if (applied)
                    galleryOpen_ = false;
            }
            else if ((action == c::Choose) == (value >= 0.5f))
            {
                if (action == c::Choose)
                    galleryOpen_ = true;
                else if (galleryOpen_)
                    galleryOpen_ = false;
                else
                    leaveBand();
                applied = true;
            }
            if (applied)
            {
                leftTab (inst);
                dropGesture();
                return true;
            }
        }
        return false;
    }

    nativeui::fxheader::State nativeHeader() const noexcept
    {
        namespace h = nativeui::fxheader;
        if (! editorShown_ || selectedItem_.index < 0)
            return {};
        const auto& ed = editor_;
        const int layout = h::layoutFor (ed.currentType(), ed.powerOn);
        const int item = selectedItem_.index + (selectedItem_.pool ? synthyboy::kNumFxSlots : 0);
        const int mask = ed.bandDisabledMask();
        const int bandMode = ! ed.nativeBandShown() ? h::NoBand : mask == 4 ? h::LowAndMid : mask == 1 ? h::MidAndHigh : h::AllBands;
        return { item, kindOfItem (selectedItem_), layout, ed.rackIndex,
                 bandMode != h::NoBand ? ed.effectiveBand() : -1, bandMode, ed.pageIndex(), ed.numPages(), ed.title(), ed.path(), ed.notice };
    }

    bool syncNativeHeader (IInstanceView& inst)
    {
        namespace h = nativeui::fxheader;
        const auto state = nativeHeader();
        if (state.layout == h::Hidden)
            return false;
        for (int field = h::Power; field <= h::kBand; ++field)
        {
            float value;
            if (! inst.takeNativeFxHeader (field, state, value))
                continue;
            bool applied = false;
            if (field == h::Power && (value >= 0.5f) != state.on())
            {
                leftTab (inst);
                setPower (inst, value >= 0.5f);
                applied = true;
            }
            else if (field == h::Rack)
            {
                const int rack = static_cast<int> (std::lround (value * (state.rackCount() - 1)));
                if (rack != state.rack)
                {
                    leftTab (inst);
                    moveToRack (inst, rack);
                    applied = true;
                }
            }
            else if (field == h::Page)
            {
                const int panel = static_cast<int> (std::lround (value * 2.0f));
                if (panel >= 0 && panel < state.tabCount() && panel != state.tabIndex())
                {
                    leftTab (inst);
                    editor_.setPage (panel);
                    applied = true;
                }
            }
            else if (field == h::Remove && value >= 0.5f)
            {
                leftTab (inst);
                removeSelected (inst);
                applied = true;
            }
            else if (field == h::kBand)
            {
                const int band = static_cast<int> (std::lround (value * 3.0f)) - 1;
                if (band != state.band && state.permitsBand (band))
                {
                    leftTab (inst);
                    editor_.setBand (band);   // the BAND selector of the compressor: a view-local choice, no parameter, the next refresh publishes the Q groups of the band
                    applied = true;
                }
            }
            if (applied)
                return true;
        }
        return false;
    }

    void clearNativeApertures (gpu::SceneBuilder& sb) const
    {
        const auto clear = [&] (ui::Rect area) { ui::clearNativeCell (sb, area); };
        visitNativeChainControls (clear);
        if (editorShown_)
            editor_.visitNativeControls (clear);
    }


    nativeui::FxCell nativeCell (int slot) const noexcept
    {
        const auto& ed = editor_;
        const int row = slot / 4;
        if (row >= ed.rowsOnPage())
            return {};
        const int p = ed.controlInCell (ed.firstRowOfPage() + row, slot % 4);
        const int kind = ed.nativeKindOf (p);
        if (kind == nativeui::kFxHidden)
            return {};
        const auto* source = ed.infoOf (p);
        return { paramIndexOf (selectedItem_, p), synthyboy::findParamIndex (source->id), kind };
    }

    void syncNativeCells (IInstanceView& inst)
    {
        auto& ed = editor_;
        const int layout = ed.nativeLayout();
        if (! editorShown_ || layout == 0)
        {
            inst.setNativeFxCells (0, nullptr);
            return;
        }
        const int rack = synthyboy::fxItemRack (plain_.data(), selectedItem_);
        if (ed.pageIndex() != nativeFxPage_ || rack != nativeFxRack_ || layout != nativeFxLayout_
            || ed.powerOn != nativeFxPower_)
        {
            inst.setNativeFxCells (0, nullptr);
            nativeFxPage_ = ed.pageIndex();
            nativeFxRack_ = rack;
            nativeFxPower_ = ed.powerOn;
        }
        nativeui::FxCell cells[nativeui::kFxCells];
        for (int slot = 0; slot < nativeui::kFxCells; ++slot)
        {
            const auto cell = nativeCell (slot);
            float value;
            if (inst.takeNativeFxCell (slot, cell, value) && cell.target >= 0 && ed.powerOn)
            {
                const float norm = nativeui::fxEngineValue (cell, value);
                const auto& real = synthyboy::paramTable()[cell.target];
                const float before = synthyboy::toNormalised (real, inst.param (cell.target));
                if (std::fabs (before - norm) > 1.0e-6f)
                {
                    ed.setActiveRow (ed.firstRowOfPage() + slot / 4);
                    inst.writeParam (cell.target, norm);
                    // A change to SYNC, BAND MODE or MODEL can replace cells;
                    // the following queued writes see only that updated layout.
                    pullEditor (inst);
                    if (ed.nativeLayout() != layout)
                        break;
                }
            }
        }
        for (int slot = 0; slot < nativeui::kFxCells; ++slot)
            cells[slot] = nativeCell (slot);
        nativeFxLayout_ = ed.nativeLayout();
        inst.setNativeFxCells (nativeFxLayout_, cells);
    }


    /** The style count of a model of the FX filter. */
    static int filtersStyleCount (int model) noexcept { return synthyboy::filters::filterModelStyleCount (model); }

    /** A style beyond the styles of the model is written back to the last one of them. */
    void clampStyle (IInstanceView& inst, const Item& item, int count)
    {
        const int styleP = editor_.controlOfKey ("fxfilter.style");
        const int styleIdx = styleP >= 0 ? paramIndexOf (item, styleP) : -1;
        if (styleIdx < 0 || count <= 0)
            return;
        if (static_cast<int> (std::lround (plainOfControl (item, inst, styleP))) >= count)
        {
            const synthyboy::ParamInfo* spi = editor_.infoOf (styleP);
            if (spi != nullptr)
                inst.writeParam (styleIdx, static_cast<float> (synthyboy::toNormalised (*spi, static_cast<float> (count - 1))));
        }
    }

    /** The page of the selected effect from the instance: the table of its controls, which ones are live, the header facts, the display. The values of the cells are the host's own (the native bridge reads the engine). */
    void pullEditor (IInstanceView& inst)
    {
        if (cardItems_.empty() || selectedSlot_ >= static_cast<int> (cardItems_.size()))
            return;
        const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
        const int kind = kindOfItem (item);
        FxEditorModel& ed = editor_;
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
        const int styleP = ed.controlOfKey ("fxfilter.style");
        if (styleP >= 0 && paramIndexOf (item, styleP) >= 0)   // a model with fewer styles than the stored one: the style is the last of the model
            clampStyle (inst, item, filtersStyleCount (static_cast<int> (std::lround (plainOfKey (item, kind, inst, "fxfilter.model", 0.0f)))));
        // the power, the rack
        ed.powerOn = synthyboy::fxItemOn (plain_.data(), item);
        ed.setModuleEnabled (ed.powerOn);
        const int rack = synthyboy::fxItemRack (plain_.data(), item);
        ed.rackCount = rackChoices (item);
        ed.rackIndex = std::clamp (rack, 0, ed.rackCount - 1);
        pullPath (ed, item, rack);
        if (ed.hasBands())
            pullBands (inst, item, kind);
        pullDisplay (inst, item, kind, ed.display);
    }

    /** The racks an item can be moved to: the ten of the engine for a module of instance 0 that is no splitter (Main, Bus 1, Bus 2 and the seven band racks), else the three buses. */
    static int rackChoices (const Item& item) noexcept { return ! item.pool && item.index < 13 ? ui::fxl::kNumRacks : synthyboy::kNumPoolRacks; }

    /** The place of the effect over its name: "MAIN CHAIN", "BUS 1 CHAIN", or for an effect of a band "MAIN CHAIN > SPLITTER L/M/H > MID BAND" (the chain the splitter sits in). */
    void pullPath (FxEditorModel& ed, const Item& item, int rack)
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

    /** The lanes of the page of a splitter: per band the facts of the tile and the cards of its rack. */
    void pullBands (IInstanceView& inst, const Item& item, int kind)
    {
        FxBandData& board = editor_.bands;
        const int nb = ui::fxl::bandsOfSplitter (kind);
        const bool splitterOn = synthyboy::fxItemOn (plain_.data(), item);
        const char* const pre = kind == ui::fxl::kKindSplitterLh ? "splitter_lh." : (kind == ui::fxl::kKindSplitterLmh ? "splitter_lmh." : "splitter_ms.");
        auto val = [&] (const char* suffix, float def) { return plainOfKey (item, kind, inst, (std::string (pre) + suffix).c_str(), def); };
        float f1 = 0.0f, f2 = 0.0f;
        if (kind == ui::fxl::kKindSplitterLh)
            f1 = val ("freq", 500.0f);
        else if (kind == ui::fxl::kKindSplitterLmh)
        {
            f1 = val ("freq_low", 200.0f);
            f2 = val ("freq_high", 3000.0f);
        }
        board.numBands = std::min (nb, FxBandData::kMaxBands);
        for (int b = 0; b < board.numBands; ++b)
        {
            const int rack = ui::fxl::bandRack (kind, b);
            const std::vector<Item>& items = bandItems_[static_cast<size_t> (rack - ui::fxl::kFirstBandRack)];
            const int n = std::min (static_cast<int> (items.size()), FxBandData::kMaxCards);
            FxBandData::Lane& lane = board.lane[b];
            lane.kind = kind;
            lane.band = b;
            lane.numCards = n;
            for (int i = 0; i < n; ++i)
            {
                FxBandData::Card& card = lane.cards[i];
                card.item = items[static_cast<size_t> (i)].index;
                card.type = kindOfItem (items[static_cast<size_t> (i)]);
                card.on = synthyboy::fxItemOn (plain_.data(), items[static_cast<size_t> (i)]);
            }
            lane.addEnabled = numFreeModules() > 0 && n < FxBandData::kMaxCards;
            lane.idle = ! splitterOn;
            lane.low = f1;
            lane.high = f2;
            lane.colour = kind == ui::fxl::kKindSplitterMs ? (b == 0 ? 0 : 3) : (b == 0 ? 0 : (b == nb - 1 ? 3 : 1));
            if (kind == ui::fxl::kKindSplitterMs)
                lane.bypassed = val (b == 0 ? "mid_bypass" : "side_bypass", 0.0f) > 0.5f;
            else
                lane.bypassed = val (b == 0 ? "low_bypass" : (b == nb - 1 ? "high_bypass" : "mid_bypass"), 0.0f) > 0.5f;
        }
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
                d.eq.law = static_cast<int> (std::lround (plainOfKey (item, type, inst, "eq.law", 0.0f)));
                d.eq.sampleRate = inst.sampleRate() > 0.0 ? static_cast<float> (inst.sampleRate()) : 48000.0f;
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
                d.comp.mode = static_cast<int> (std::lround (plainOfKey (item, type, inst, "compressor.limit", 0.0f)));
                d.comp.mix = plainOfKey (item, type, inst, "compressor.mix", 100.0f) * 0.01f;
                // the band of the row of knobs that is on screen (the first live member of the active row names it)
                {
                    const FxEditorModel& ed = editor_;
                    const int first = ed.activeRow();
                    int pm = -1;
                    for (int c = 0; c < 4 && pm < 0; ++c)
                        pm = ed.controlInCell (first, c);
                    const char* k = pm >= 0 && ed.infoOf (pm) != nullptr ? ed.infoOf (pm)->key : "";
                    d.comp.activeBand = std::strstr (k, ".low.") != nullptr ? 0 : (std::strstr (k, ".high.") != nullptr ? 2 : ed.effectiveBand());   // a row of one band's values names it, else the band the BAND selector has
                }
                break;
            }
            case K::Delay:
            {
                d.delay.timeMs1 = delayMs (item, type, inst, "delay.time", "delay.division");
                d.delay.timeMs2 = delayMs (item, type, inst, "delay.time2", "delay.division2");
                d.delay.feedback = plainOfKey (item, type, inst, "delay.feedback", 40.0f) * 0.01f;
                d.delay.style = static_cast<int> (std::lround (plainOfKey (item, type, inst, "delay.style", 1.0f)));
                d.delay.offsetL = plainOfKey (item, type, inst, "delay.offset_left", 1.0f);
                d.delay.offsetR = plainOfKey (item, type, inst, "delay.offset", 1.0f);
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

    /** The groups of the page are ALL the rows (blocks) of the open effect, the functional panels in tab order, in reading order: the active row is on Q 1 .. 4, the other rows (wrapping) on 5 .. 16 (the XL keeps up to
        four rows where they are). */
    void publishQGroup (IInstanceView& inst)
    {
        const char* name = "";
        FxEditorModel& ed = editor_;
        if (editorShown_ && selectedSlot_ >= 0 && selectedSlot_ < static_cast<int> (cardItems_.size()))
        {
            const int row = ed.activeRow();
            name = ed.rowTitle (row);
            const Item item = cardItems_[static_cast<size_t> (selectedSlot_)];
            // Keep the selected editor and the page navigable while OFF, but
            // never publish parameter targets that could be turned accidentally.
            if (! ed.powerOn)
            {
                qSlots_.publish (inst, 0, 0, [] (int, int*) {});
                ui::setText (qName_, sizeof qName_, name);
                return;
            }
            // ALL the rows of the effect are groups, the functional panels in tab order (the same physical routing is retained across panel selection): a row of the other view has no
            // control on the screen but its knobs work and the XL's knob displays show its names. The rows are in the order of the page (QRouter rule: more than four rows are paged).
            const int n = std::max (0, ed.numRows());
            const int count = std::max (1, n), active = std::clamp (row, 0, count - 1);
            qSlots_.publish (inst, count, active, [&] (int k, int* t)
            {
                for (int c = 0; c < 4; ++c)   // the live controls of the row: a dim control has no knob
                {
                    const int p = ed.memberControl (k, c);
                    t[c] = p >= 0 ? paramIndexOf (item, p) : -1;
                }
            });
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
        const int freeSlot = synthyboy::fxFirstFreeSlot (plain_.data());
        if (freeSlot < 0)
            return false;
        readSlotParameters (inst, freeSlot);
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
        editor_.setActiveRow (0);
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
        editor_.setActiveRow (0);
        qSlots_.forget();
    }
    /** The highest rack an item can be moved to: the ten racks for a module of instance 0 that is no splitter, else the buses. */
    static int rackLimit (const Item& item) noexcept { return rackChoices (item) - 1; }

    /** The engine rack of band `band` of the splitter that is on the page (-1: the page is no splitter's or the band does not exist). */
    int bandRackOfSelected (int band) const noexcept
    {
        if (cardItems_.empty() || selectedSlot_ < 0 || selectedSlot_ >= static_cast<int> (cardItems_.size()))
            return -1;
        return ui::fxl::bandRack (kindOfItem (cardItems_[static_cast<size_t> (selectedSlot_)]), band);
    }

    bool renderedEditorStillLive() const noexcept
    {
        // Card releases may refresh plain_ before the display tick. The
        // editor and renderedItem_ keep the last published type and power.
        return renderedItem_.index >= 0 && editor_.powerOn
            && synthyboy::fxItemOn (plain_.data(), renderedItem_)
            && kindOfItem (renderedItem_) == editor_.currentType();
    }


    bool canMoveCard (IInstanceView& inst, const Item& item)
    {
        readRackStructure (inst);
        return inst.stateGeneration() == stateGeneration_ && structureSignature() == dragSignature_
            && synthyboy::fxItemOn (plain_.data(), item);
    }

    void moveCard (IInstanceView& inst, const Item& item, int target)
    {
        const Item at = cardItem (target);
        if (at == item)
            return;
        const synthyboy::FxRacks racks = synthyboy::fxRacks (plain_.data());
        const int rack = std::clamp (synthyboy::fxItemRack (plain_.data(), at), 0, rackLimit (item));
        int index = 0;
        for (const Item& other : racks.rack[static_cast<size_t> (rack)])
        {
            if (other == at)
                break;
            if (! (other == item))
                ++index;
        }
        std::vector<synthyboy::FxWrite> writes;
        if (synthyboy::fxMove (plain_.data(), item, rack, index, writes))
        {
            applyWrites (inst, writes);
            selectedItem_ = item;
            selectionChanged_ = true;
        }
    }

    void selectCard (int slot) noexcept
    {
        selectedSlot_ = slot;
        selectedItem_ = cardItem (slot);
        editor_.setActiveRow (0);
        qSlots_.forget();
    }

    void powerCard (IInstanceView& inst, const Item& item, bool on)
    {
        std::vector<synthyboy::FxWrite> writes;
        synthyboy::fxSetOn (plain_.data(), item, on, writes);
        applyWrites (inst, writes);
    }

    int w_ = kCanvasW, h_ = kCanvasH;
    FxEditorModel editor_;
    DrawPass drawPass_ = DrawPass::All;
    bool selectionChanged_ = true, galleryOpen_ = false, editorShown_ = false, dragEnabled_ = false;
    int selectedSlot_ = 0;
    int chainPage_ = 0;
    Item selectedItem_ { false, -1 }, renderedItem_ { false, -1 };
    std::vector<Item> cardItems_;
    std::vector<Item> bandItems_[ui::fxl::kNumBandRacks];   // the modules of the band racks 3 .. 9 (the lanes of the page of a splitter)
    bool nativeControls_ = false;
    int nativeFxPage_ = -1, nativeFxRack_ = -1, nativeFxLayout_ = 0;
    bool nativeFxPower_ = false;
    const IInstanceView* lastInstance_ = nullptr;
    int scope_ = 0;                                         // 0: the chain of Main / Bus 1 / Bus 2; 3 .. 9: the chain of a band rack
    uint32_t gallerySig_ = 0;
    int galleryScope_ = -1;
    int galleryN_ = 0, galleryKinds_[ui::fxgallery::kMaxTypes] = {};   // the kinds the gallery offers (the engine's type numbers 1 ..)
    char galleryHeadline_[96] = "TAP AN EFFECT TO ADD IT TO THE CHAIN", galleryNote_[96] = {};
    std::vector<float> plain_;
    const synthyboy::LiveValues* live_ = nullptr;   // the readout of the last update (the meters)
    uint32_t chainSig_ = 0;
    uint64_t stateGeneration_ = 0;
    const char* typeNames_[ui::fxl::kNumKinds - 1] = {};

    // the response of the FX filter
    std::unique_ptr<synthyboy::FilterResponse> response_;
    synthyboy::ParamValues filterValues_ = synthyboy::ParamValues::defaults();
    float filterDb_[256] = {}, filterVals_[17] = {};
    double filterRate_ = 0.0;
    bool haveFilter_ = false;

    Item draggedItem_ { false, -1 };
    struct BandDrag { int lane = -1, card = -1; } bandDrag_;   // the card of a lane whose MOVE grip the finger holds
    uint32_t dragSignature_ = 0;
    bool dragOverPage_ = false;
    bool gestureDown_ = false;
    QSlots qSlots_;
    char qName_[24] = {};
};

}} // namespace synthyboy_mpc::overlay


