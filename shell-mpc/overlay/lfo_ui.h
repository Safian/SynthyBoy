// SynthyBoy - MPC overlay: the LFO UI of the Play page, the plugin-drawn pages built from the UI library (shell-mpc/ui/lfo_*.h, shape_editor.h, mod_targets.h, panel.h, ...) and wired to the real engine
// parameters (lfoN.* and rlfoN.* of the table). One object per (Play view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick (), with the instance
// as IInstanceView. The element has two types: an LFO (LFO 1 .. 10: a shape of one cycle, the drawn shape in the content state) and a RANDOM LFO (RND 1 .. 4: a style; the display is the trace of its output).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementLfo, the pages above it are ours): main = the compact view in the right third of the Play page (LfoCompact); full = the full screen
// (LfoFullScreen) over the whole plugin area of the Play page; detail pages above it: the shape editor (ShapeEditorPage), the modulation of the selected points (PointModPage) and the name keyboard of a saved
// shape (TextEntry) above that, the targets of the LFO (ModTargetsPage: the modulation matrix seen from the source), the list popup (instances, choices, the destination to add, the SHAPES and MORE lists of the editor)
// and the modulations of a control (ModulationsPage). The editor's file work (the saved shapes in SynthyBoy/LFO Shapes/ on the card, lfo_shape_files.h) happens in the touch handler on the overlay thread, never in draw.
// What it does with a touch: the library component gives a Change { id, kind, value }; the id says which control it was, the binding of the id (a key suffix of the engine table and a type) says which parameter and
// how the UI value converts; the write goes through IInstanceView::writeParam (normalised; the plugin tells the host). The drawn shape is not a parameter: the editor changes a shape model, which goes to the
// content state through IInstanceView::setLfoShape (at most every kShapeSendMs while a finger is down, and at the end of the gesture). Every update () reads every parameter back into the component states, so host
// automation, presets and the host's own widgets show on the next frame.
// Q-Link: the compact view and the full screen have Q groups of table parameters (q_router.h); the shape editor's group EDIT (X, Y, CURVE, GRID of the selected point) is VIRTUAL: the router keeps the values and hands
// the host's writes back through takeQVirtual.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <sys/stat.h>

#include "../native_lfo_detail.h"
#include "../native_lfo_editor.h"
#include "../native_play_env_lfo.h"
#include "../ui/lfo_screen.h"
#include "../ui/native_apertures.h"
#include "../ui/mod_targets.h"
#include "../ui/shape_editor.h"
#include "../ui/ui.h"
#include "../safe_file.h"
#include "../q_router.h"
#include "backgrounds_manifest.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "instance_view.h"
#include "lfo_shape_files.h"
#include "overlay_layout.h"
#include "page_view.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "native_mod_targets_ui.h"
#include "modulation_window.h"
#include "ui_stack.h"
#include "synthyboy/lfo_display.h"
#include "synthyboy/osc_cycle.h"
#include "synthyboy/params.h"
#include "synthyboy/user_content.h"
#include "synthyboy/wave_convert.h"
#include "synthyboy/wavetable_catalogue.h"
#include "synthyboy/wavetable_edit.h"

namespace synthyboy_mpc { namespace overlay {

class LfoUi
{
public:
    /** Selects LFO 1 .. 10 or RND 1 .. 4 (0 .. 13) without a touch: the gallery and the tests. */
    /** Chooses LFO 1 .. 10 or RND 1 .. 4 by its position 0 .. 13 (view state only; the tests' way to a random LFO; the carousel and the list of the compact view choose among their cells: `selectSlot`). */
    void select (int s, IInstanceView& v)
    {
        s = std::clamp (s, 0, kInstances - 1);
        uibind::chooseLfo (v, s < kLfos ? s + 1 : uibind::kRndBase + (s - kLfos + 1));
        adoptSelection (v);
    }
    // ---- the pages (UiStack ids)
    static constexpr int kElementPage = 3;                                    // the page of an LFO tab is the full screen of the element (page_view.h)
    static constexpr int kElementLfo = 3;                                     // the full screen of the LFO (the oscillator is 1, the envelope 2)
    enum Page { kPageEditor = 40, kPageTargets = 41, kPageList = 42, kPageMods = 43, kPagePointMod = 44, kPageKeyboard = 45 };
    /** The virtual Q targets of the router (QRouter::kNumVirtual = 10) this page uses: 0 .. 7 are the editor's (POINT, X, Y, CURVE, GRID X, GRID Y, SNAP, BEND ALL), 8 and 9 belong to the page of the point modulation (MODULATE, AMOUNT). */
    static constexpr int kVirtEditorCount = 8, kVirtPmTarget = 8, kVirtPmAmount = 9;
    static constexpr int kVirtualSelector = 0;   // separate from the editor: the standalone page root only
    static_assert (kVirtPmAmount < QRouter::kNumVirtual, "the router must have a virtual target for every Q knob of the page");

    // ---- the id spaces of the controls (a Change comes out with the id the owner gave)
    enum Id : int
    {
        kClose = 1, kFullButton, kInstance, kSelector, kSyncMode, kSyncTrip, kSyncDot, kTargets, kPicker, kPagePicker, kSmoothPick, kHostPick, kEdit, kPageInstance,   // kInstance: compact carousel; kPageInstance: standalone tab dropdown; kSelector: Play full-screen cells
        kShape = 100, kStyle, kRate, kDivision, kRise, kSmoothTime, kSync, kMode, kPhase, kDelay, kDirection, kSwing, kStereo, kMono, kStereoToggle, kKeyTranspose, kKeyTune, kModeList,
        kEdBack = 200, kEdTools, kEdUndo, kEdRedo, kEdDelete, kEdSnap, kEdLoop, kEdSmooth, kEdCycleLink, kEdLoopLink, kEdCanvas, kEdX, kEdY, kEdCurve, kEdGridX, kEdGridY, kEdPoint, kEdShapes, kEdMore, kEdMod, kEdMode, kEdPhase, kEdBend,
        kMpBack = 230, kMpTarget, kMpAmount, kMpRemove, kMpBoard, kKbOk = 250, kKbCancel,
        kTargetsBack = 300, kTargetsPage, kTargetsAdd, kTargetsRow = 310,   // 310 + 4 * row + (0 depth, 1 polarity, 2 bypass, 3 remove)
        kList = 400, kListBackdrop, kListHeader,
        kModsBack = 500, kModsRow = 510, kModsAddStep = 560, kModsAdd, kModsPage
    };

    /** What a value is and how it converts to the engine's plain value. */
    enum class T : uint8_t
    {
        Knob,      // normalised 0 .. 1 of the parameter
        Number,    // a NumberField in the units of the parameter
        Stepper,   // a choice parameter
        Toggle,
        List,      // a choice parameter as a LED list (every option visible)
        Picker,    // the shape / style picker: the index is the value of the parameter
        Inert
    };
    struct Item
    {
        int id = 0;
        ui::Component* c = nullptr;
        T type = T::Inert;
        int param = -1;
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kCompactX = 858, kCompactW = 408;                    // the right third of the Play page
    static constexpr int kInstances = 14;                                      // LFO 1 .. 10, RND 1 .. 4
    static constexpr int kCurvePoints = 256, kTracePoints = 192;
    static constexpr double kModsRefreshMs = 100.0, kShapeSendMs = 90.0, kTraceMs = 33.0;

    LfoUi() { setCanvas (kCanvasW, kCanvasH); compact_.setNativeInstanceSelector (false); compact_.setNativeDials(); }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        const ui::Rect tab { 0, kTabY, w, kTabH };
        compact_.place ({ kCompactX, kTabY + 4, kCompactW, kTabH - 8 });
        full_.place (tab);
        editor_.place (tab);
        targets_.place (tab);
        mods_.place (tab);
        pointMod_.place (tab);
        entry_.place (tab);
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (tab);
        list_.setId (kList);
        list_.setBackdropId (kListBackdrop);
        list_.setHeaderId (kListHeader);
    }

    /** Reads the instance: the selected LFO (the type and the number), every value of the visible controls, the modulations of the knobs, the pages of the stack, the shape, the cursors of the voices, the Q group. */
    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs, const synthyboy::LiveValues* live)
    {
        highlight_ = highlight;
        readSelectorQ (inst, stack);
        adoptSelection (inst);   // ONE selection for the LFO tab and the Play page (Play LFO + mpcTabView.rnd, chunk saved)
        const bool open = stack.depth() > 0 && stack.element() == kElementLfo;
        if (open && stack.top() == kPageMods)
        {
            const auto* control = item (modsTarget_);
            const int parameter = control != nullptr ? control->param : -1;
            const uint64_t preset = inst.stateGeneration();
            if (modsPreset_ != preset || modsTargetParam_ != parameter)
            {
                dropGesture();
                modsPreset_ = preset;
                modsDirty_ = true;
                mods_.setPickingSource (false);
            }
        }
        if (open && stackDepth_ == 0)
            full_.setActiveGroup (0);
        stackDepth_ = open ? stack.depth() : 0;
        if (stackDepth_ > 0 && instPopup_)
            closeInstanceList();
        syncPages (open ? stack : idle_, inst);
        content_ = inst.content();
        bpm_ = inst.bpm();
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        uibind::fillSelector (uibind::SelKind::Lfo, scan_, inst, full_.selector);
        full_.syncSelectorLayout();
        uibind::cellsFromSelector (uibind::SelKind::Lfo, full_.selector, playCells_);   // the Play page's carousel: the SAME cells as the tab's selector
        if (page_ && stackDepth_ == 1 && pageTop_ == 0)
            reportSelectorQ (inst);
        playDots_ = 0;
        for (int k = 0; k < playCells_.n && k < 32; ++k)
            if (isSourceAssigned (playCells_.id[k]))
                playDots_ |= (1u << k);
        pull (inst);
        updateDisplay (inst, live, nowMs);
        if (stackDepth_ > 0 && pageTop_ == kPageEditor)
            updateEditor (inst, nowMs);
        else
        {
            if (stackDepth_ > 0 && pageTop_ == kPagePointMod)
                updatePointMod (inst, nowMs);
            else if (nativeDetailView_ != nativeui::lfodetail::Hidden && pageTop_ == kPageList && listKind_ == ListKind::Editor)
                syncEditorContent();
            if (shapeDirty_ && ! editorContentExternal_)
                flushShape (inst, nowMs);
        }
        if (open)
            inst.setUiFullOpen (true);
        else if (stack.isMain())
            inst.setUiFullOpen (false);
        publishQGroup (inst);
    }

    /** The page of the LFO tab (page_view.h): the full screen of the selected LFO or random LFO (shared with the Play page), nothing to close; the selector ("used + one free") chooses another one. */
    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input& in)
    {
        if (! page_)
        {
            page_ = true;
            full_.setPageMode (true);
            configured_ = false;
        }
        updateHostedEditor (inst, stack, 3, nowMs, in.live, true);
    }

    void syncNativePlayEditor (IInstanceView& inst, UiStack& stack, int highlight, double nowMs,
                               const synthyboy::LiveValues* live, bool enable)
    {
        if (! enable || stack.isMain() || stack.element() != kElementLfo)
        {
            if (nativePlayEditor_)
            {
                hideNativePlayEditor (inst);
                update (inst, highlight, stack, nowMs, live);
            }
            return;
        }
        const bool entering = ! nativePlayEditor_;
        nativePlayEditor_ = true;
        full_.setNativePlayEditor (true);
        updateHostedEditor (inst, stack, highlight, nowMs, live, entering);
    }

    void hideNativePlayEditor (IInstanceView& inst)
    {
        if (! nativePlayEditor_)
            return;
        nativePlayEditor_ = false;
        full_.setNativePlayEditor (false);
        full_.setNativePageControls (false);
        inst.setNativeLfoState ({});
        nativePublished_ = false;
        publishNativeEditor (inst, {});
        publishNativeDetail (inst, {});
        dropGesture();
    }

    /** The whole picture of the page: its ground, the full screen or the page above it, the cursors, the popup. */
    void draw (gpu::SceneBuilder& sb) const
    {
        drawPage (sb);
        drawLive (sb);
        drawPopup (sb);
    }

    bool isMain() const noexcept { return stackDepth_ == 0; }
    void setNativeSmooth (bool on) { full_.setNativeSmooth (on); }
    bool nativeSmooth() const noexcept { return page_ && full_.nativeSmooth() && ! random(); }
    void setHostBackground (bool confirmed) noexcept { nativeGround_ = confirmed; }
    bool hostBackground() const noexcept { return nativeGround_ && full_.nativePageControls() && pageTop_ == 0; }

    void drawPageGround (gpu::SceneBuilder& sb) const
    {
        if (full_.nativePageControls() && ! hostBackground())
            drawRootGroundWithApertures (sb);
    }

    ui::Rect nativeRect (int field, int action = nativeui::lfo::kTargets) const noexcept
    {
        namespace n = nativeui::lfo;
        switch (field)
        {
            case n::kSelection: return full_.instance.rect();
            case n::kShape: case n::kStyle: return full_.namedPicker.rect();
            case n::kAction: return action == n::kExpand ? full_.edit.rect() : full_.targets.rect();
            case n::kSmooth: case n::kSmoothTime:
            {
                const auto r = full_.smoothArea();
                if (r.empty())
                    return {};
                constexpr int buttonW = 72;
                return field == n::kSmooth ? ui::Rect { r.x, r.y, buttonW, r.h }
                                          : ui::Rect { r.x + buttonW, r.y, r.w - buttonW, r.h };
            }
            case n::kRate: case n::kDivision: return full_.rate.rect();
            case n::kRise: return full_.rise.rect();
            case n::kSync: return full_.syncSwitch.mode.rect();
            case n::kMode: return full_.mode.rect();
            case n::kRandomMode: return full_.modeList.rect();
            case n::kPhase: return full_.phase.rect();
            case n::kDelay: return full_.delay.rect();
            case n::kHost: return full_.hostPick.rect();
            case n::kDirection: return full_.direction.rect();
            case n::kSwing: return full_.swing.rect();
            case n::kStereo: return full_.stereo.rect();
            case n::kMono: return full_.monoQ.rect();
            case n::kRandomStereo: return full_.stereoToggle.rect();
            case n::kTranspose: return full_.keyTranspose.rect();
            case n::kTune: return full_.keyTune.rect();
            default: return {};
        }
    }
    ui::Rect nativeEditorRect (int field) const noexcept
    {
        return editorFields (*this)[field]->rect();
    }
    ui::Rect nativeEditorToolRect (int tool) const noexcept { return editor_.tools.cell (tool); }
    ui::Rect editorCanvasRect() const noexcept { return editor_.canvasRect(); }
    ui::Rect nativeDetailRect (int field) const noexcept
    {
        namespace n = nativeui::lfodetail;
        if (field < n::Header)
            return pointModFields (*this)[field]->rect();
        if (field == n::Header)
            return list_.headerRect();
        const int row = field - n::FirstRow;
        return row < listNativeRows() ? list_.rowRect (list_.firstRow() + row) : ui::Rect {};
    }

    /** The LFO shown: 1 .. 10 (LFO) or 1 .. 4 (RND, `random ()`). */
    bool random() const noexcept { return rnd_ != 0; }
    int selection() const noexcept { return rnd_ != 0 ? rnd_ : lfoNum_; }
    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return full_.activeGroup(); }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    /** The selector of the full screen (tests). */
    const ui::UsedSelector& selector() const noexcept { return full_.selector; }
    /** The modulation source id (choice of modNN.source) of the LFO shown. */
    int source() const noexcept { return rnd_ != 0 ? 14 + rnd_ : uibind::lfoSource (lfoNum_); }
    /** The rectangle the picture of the LFO goes into now (empty when the current page shows none). */
    ui::Rect displayRect() const noexcept
    {
        if (stackDepth_ == 0)
            return compact_.displayRect();
        if (pageTop_ == 0 || pageTop_ == kPageList)
            return full_.displayRect();
        return {};
    }

    // ---- the drawing (a picture is one scene of cached cells, then the live parts, then the popups)
    void drawCompact (gpu::SceneBuilder& sb) const
    {
        const ui::theme::Look& L = ui::theme::L();
        auto ground = [&] (int x, int y, int w, int h)
        {
            if (w <= 0 || h <= 0)
                return;
            ui::Cell cell (sb, { x, y, w, h }, 0);
            sb.gradientRect (static_cast<float> (x), static_cast<float> (y), static_cast<float> (x + w), static_cast<float> (y + h), ui::theme::rgba (L.bg0), ui::theme::rgba (L.bg1));
        };
        const ui::Rect header { kCompactX + 6, kTabY + 10, kCompactW - 12, 56 };
        ground (kGroundX0, kTabY, kGroundX1 - kGroundX0, header.y - kTabY);
        ground (kGroundX0, header.y, header.x - kGroundX0, header.h);
        ground (header.right(), header.y, kGroundX1 - header.right(), header.h);
        const int midY = header.bottom();
        if (rnd_ == 0)
        {
            const ui::Rect rise = compact_.rise.rect();
            const ui::Rect smooth = compact_.smoothTime.rect();
            ground (kGroundX0, midY, kGroundX1 - kGroundX0, rise.y - midY);
            ground (kGroundX0, rise.y, rise.x - kGroundX0, rise.h);
            ground (rise.right(), rise.y, smooth.x - rise.right(), rise.h);
            ground (smooth.right(), rise.y, kGroundX1 - smooth.right(), rise.h);
            ground (kGroundX0, rise.bottom(), kGroundX1 - kGroundX0, kTabY + kTabH - rise.bottom());
        }
        else
            ground (kGroundX0, midY, kGroundX1 - kGroundX0, kTabY + kTabH - midY);
        compact_.draw (sb);
        ui::lfodisplay::draw (sb, compact_.displayRect(), compactData(), ! displayHot_, displayHot_ && ! instPopup_ ? ui::lfodisplay::Part::Ground : ui::lfodisplay::Part::All);   // hot: the data follows with the live parts (drawLive), the cells keep their groups
        compact_.full.draw (sb);
        if (instPopup_)
            list_.draw (sb);   // the list of the selector lies over the third (drawLive paints no cursors then)
    }
    /** The opaque ground of the whole tab and the page of the stack (the full screen, the editor, the targets, the modulations of a control). */
    void drawPage (gpu::SceneBuilder& sb) const
    {
        if (pageTop_ == kPageEditor || (pageTop_ == kPageList && underList_ == kPageEditor))
        {
            std::array<ui::Rect, nativeui::lfoeditor::kCount> cells {};
            for (int field = 0; field < nativeui::lfoeditor::kCount; ++field)
                cells[field] = nativeEditorRect (field);
            ui::outsideNativeCells (ui::Rect { 0, kTabY, w_, kTabH }, cells,
                                    [&] (ui::Rect strip) { drawRootGround (sb, strip); });
        }
        else if (pageTop_ == kPagePointMod)
        {
            const auto parts = pointModFields (*this);
            std::array<ui::Rect, nativeui::lfodetail::Header> cells {};
            for (size_t i = 0; i < cells.size(); ++i)
                cells[i] = parts[i]->rect();
            ui::outsideNativeCells (ui::Rect { 0, kTabY, w_, kTabH }, cells,
                                    [&] (ui::Rect strip) { drawRootGround (sb, strip); });
        }
        else if (! full_.nativePageControls())
            drawRootGround (sb, { 0, kTabY, w_, kTabH });
        else if (hostBackground())
            coverRootMarkers (sb);
        if (pageTop_ == kPageEditor || (pageTop_ == kPageList && underList_ == kPageEditor))
        {
            editor_.draw (sb);
            return;
        }
        if (pageTop_ == kPagePointMod)
        {
            pointMod_.draw (sb);
            return;
        }
        if (pageTop_ == kPageKeyboard)
        {
            entry_.draw (sb);
            return;
        }
        if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            targets_.draw (sb);
        else if (pageTop_ == kPageMods)
            mods_.draw (sb);
        else
        {
            full_.draw (sb);
            ui::lfodisplay::draw (sb, full_.displayRect(), fullDisplayData(), ! displayHot_, displayHot_ ? ui::lfodisplay::Part::Ground : ui::lfodisplay::Part::All);
        }
    }
    void drawPopup (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ > 0 && pageTop_ == kPageList)
        {
            namespace n = nativeui::lfodetail;
            if (listKind_ == ListKind::Editor)
            {
                list_.drawNativeChrome (sb);
                auto cut = [&] (ui::Rect cell)
                {
                    const gpu::ScopedClip clip (sb, cell.x, cell.y, cell.right(), cell.bottom());
                    if (clip)
                        sb.clear (0u);
                };
                cut (list_.headerRect());
                if (listNativeRows() > 0)
                    cut (ui::Rect::unite (nativeDetailRect (n::FirstRow), nativeDetailRect (n::FirstRow + listNativeRows() - 1)));
            }
            else
                list_.draw (sb);
        }
    }
    /** The cursors of the voices / the newest value of the trace (the display that is on screen). */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
        {
            if (! instPopup_)
            {
                if (displayHot_)   // (with the selector's list open the list lies over the third: drawCompact kept the data there)
                    ui::lfodisplay::draw (sb, compact_.displayRect(), compactData(), false, ui::lfodisplay::Part::Data);   // the data of a hot display (drawCompact left it out): live, after every cell
                ui::lfodisplay::drawLive (sb, compact_.displayRect(), compactData());
            }
        }
        else if (pageTop_ == 0 || (pageTop_ == kPageList && underList_ == 0))
        {
            const auto data = fullDisplayData();
            if (displayHot_)
                ui::lfodisplay::draw (sb, full_.displayRect(), data, false, ui::lfodisplay::Part::Data);
            ui::lfodisplay::drawLive (sb, full_.displayRect(), data);
        }
    }
    /** What the current state draws, as one number (the picture changes when it does): a scene of everything, the live parts included. */
    uint32_t drawHash (gpu::SceneData& scratch) const
    {
        gpu::SceneBuilder sb (scratch);
        sb.begin (gpu::kKindPlay, 0, w_, h_);
        if (stackDepth_ == 0)
            drawCompact (sb);
        else
            drawPage (sb);
        drawLive (sb);
        drawPopup (sb);
        return page::sceneHash (scratch) ^ static_cast<uint32_t> (stackDepth_ * 7 + pageTop_);
    }
    /** The touch hit this UI's drawing (the main state: the right third; any other state: the whole tab). */
    bool covers (int x, int y) const noexcept
    {
        if ((pageTop_ == kPageTargets && targets_.nativeHit (x, y))
            || (pageTop_ == kPageMods && mods_.nativeHit (x, y)))
            return false;
        if (stackDepth_ > 0)
        {
            if ((nativePlayEditor_ && pageTop_ == 0 && full_.close.rect().contains (x, y))
                || nativeFieldAt (x, y) >= 0 || nativeEditorHit (x, y) || nativeDetailHit (x, y))
                return false;
            return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
        }
        if (rnd_ == 0 && ! instPopup_ && (compact_.rise.rect().contains (x, y) || compact_.smoothTime.rect().contains (x, y)))
            return false;
        return ui::Rect { kCompactX, kTabY + 4, kCompactW, kTabH - 8 }.contains (x, y);
    }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** One touch event in UI pixels (the whole screen), `stack` is the instance's stack (the caller holds its lock). true: the touch belongs to the plugin-drawn pages. */
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        ui::Component* top = topComponent();
        if (ev.kind == TouchEvent::Down)
        {
            int targetRow = -1;
            const bool modulations = pageTop_ == kPageMods;
            if (modulations ? mods_.nativeHit (x, y, &targetRow)
                            : pageTop_ == kPageTargets && targets_.nativeHit (x, y, &targetRow))
            {
                dropGesture();
                if (targetRow >= 0)
                {
                    if (modulations)
                        mods_.selectRow (targetRow);
                    else
                        targets_.selectRow (targetRow);
                    publishQGroup (inst);
                }
                return false;
            }
            gesture_ = nullptr;
            gestureDown_ = false;
            if (nativeEditorHit (x, y))
            {
                editor_.selectGroupAt (x, y);
                publishQGroup (inst);
                return false;
            }
            if (nativeDetailHit (x, y))
                return false;
            const int native = nativeFieldAt (x, y);
            if (native >= 0)
            {
                selectNativeQGroup (native);
                publishQGroup (inst);
                return false;
            }
            if (! covers (x, y) || ! top)
                return false;
            gesture_ = top;
            gestureDown_ = true;
            virtualUndoActive_ = false;
            handle (top->touchDown (x, y, nowMs), stack, inst, nowMs);
            publishQGroup (inst);
            return true;
        }
        if (! gesture_ || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            handle (gesture_->touchMove (x, y), stack, inst, nowMs);
        else
        {
            handle (gesture_->touchUp (x, y), stack, inst, nowMs);
            gestureDown_ = false;
            gesture_ = nullptr;
            virtualUndoActive_ = false;
            if (shapeDirty_ && ! editorContentExternal_)
                flushShape (inst, nowMs);   // the end of a gesture: the shape goes to the content at once
        }
        return true;
    }
    /** The clock while a finger is down (the long press of a knob, of a point of the shape). */
    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ && gestureDown_)
            handle (gesture_->tick (nowMs), stack, inst, nowMs);
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
        handle (ch, stack, inst, nowMs);
        (void) nowMs;
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = nullptr; gestureDown_ = false; }
    /** The page went off the screen: the open list of the compact view closes. */
    void closePopups() { if (instPopup_) closeInstanceList(); }
    /** A full screen opened or closed somewhere: the router keeps what the last page set, so this UI publishes its Q group again at the next update (). */
    void forgetQGroup() noexcept { qSlots_.forget(); }

    // ---- access for the tests
    ui::LfoCompact& compact() noexcept { return compact_; }
    const ui::LfoCompact& compact() const noexcept { return compact_; }

    ui::Rect compactNativeRect (int field) const noexcept
    {
        const auto* component = compactNativePart (*this, field);
        auto rect = component != nullptr ? component->rect() : ui::Rect {};
        if (field == nativeui::playenvlfo::Choose)
            rect.w = 338;   // the compact Play skin reserves the right header gutter
        return rect;
    }
    void setCompactNativeParts (const nativeui::playenvlfo::State& view)
    {
        namespace n = nativeui::playenvlfo;
        for (int field = n::LfoExpand; field < n::Count; ++field)
            if (auto* control = compactNativePart (*this, field))
                control->setHostPainted (view.fields[field].target >= 0);
        compact_.rate.setModeCellsVisible (view.fields[n::StandardSync].target < 0);
        compactNativeKey_ = view.context.view == n::LfoKey || view.context.view == n::RndKey;
    }
    int compactNativeTarget (int field) const noexcept
    {
        if (field == nativeui::playenvlfo::StandardSync && ! random())
            return paramOf ("sync");
        const auto* part = compactNativePart (*this, field);
        for (const auto& item : items_)
            if (item.c == part && item.type != T::Inert)
                return item.param;
        return -1;
    }
    bool applyCompactNative (IInstanceView& v, UiStack& stack, int field, float value)
    {
        namespace n = nativeui::playenvlfo;
        if (field == n::LfoExpand && value >= 0.5f)
            return stack.openFull (kElementLfo);
        if (field == n::StandardSync && ! random())
        {
            const int target = paramOf ("sync");
            const int mode = static_cast<int> (std::lround (value * 2.0f));
            const auto change = ui::Change::indexed (kSyncMode, mode, mode);
            const int next = full_.syncSwitch.nextSync (ui::SyncSwitch::Mode, change);
            if (target < 0 || static_cast<float> (next) == v.param (target))
                return false;
            handle (change, stack, v, lastNowMs_);
            return true;
        }
        const auto* part = compactNativePart (*this, field);
        for (const auto& item : items_)
            if (item.c == part && item.type != T::Inert && item.param >= 0)
            {
                const auto& info = uibind::info (item.param);
                if (field == n::Rate)
                {
                    if (std::fabs (uibind::normOf (item.param, v.param (item.param)) - value) <= 1.0e-6)
                        return false;
                    write (item, ui::Change::normalised (item.id, value), v);
                }
                else
                {
                    const auto wanted = synthyboy::constrainValue (info, synthyboy::fromNormalised (info, value));
                    if (wanted == v.param (item.param))
                        return false;
                    write (item, ui::Change::indexed (item.id, static_cast<int> (std::lround (wanted - info.min)), wanted), v);
                }
                return true;
            }
        return false;
    }
    ui::LfoFullScreen& full() noexcept { return full_; }
    ui::ShapeEditorPage& editor() noexcept { return editor_; }
    void syncNativeTargets (IInstanceView& inst, UiStack& stack, bool fromPlay)
    {
        using namespace nativeui::targets;
        const auto before = stack.changes();
        nativeTargetPage_.update (inst, targets_, pageTop_ == kPageTargets, fromPlay ? PlayLfo : Lfo,
                               source(), targetRows_, [&] (ui::Change change) { handle (change, stack, inst, lastNowMs_); });
        const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        nativeModulations_.update (inst, mods_, ! mods_.boardShown() && modsContextValid (inst),
            fromPlay ? PlayLfo : Lfo, static_cast<int> (destination), modRows_,
            [&] (ui::Change change) { handle (change, stack, inst, lastNowMs_); }, Modulations);
        if (stack.changes() != before)
        {
            stackDepth_ = stack.depth();
            syncPages (stack, inst);
        }
        if (pageTop_ == kPageTargets || pageTop_ == kPageMods || stack.changes() != before)
            publishQGroup (inst);
    }
    void hideNativeTargets (IInstanceView& inst)
    {
        nativeTargetPage_.hide (inst, targets_);
        nativeModulations_.hide (inst, mods_);
    }
    ui::ModTargetsPage& targetsPage() noexcept { return targets_; }
    ui::ModulationsPage& modsPage() noexcept { return mods_; }
    ui::ListPopup& list() noexcept { return list_; }
    ui::PointModPage& pointModPage() noexcept { return pointMod_; }
    ui::TextEntry& keyboard() noexcept { return entry_; }
    LfoShapeLibrary& library() noexcept { return lib_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    const Item* item (int id) const noexcept { for (const Item& it : items_) if (it.id == id) return &it; return nullptr; }
    const ui::lfodisplay::Data& displayData() const noexcept { return data_; }
    /** The data of the compact view (the Play third): the same picture with the plot area below the header band. The full page and the shape editor draw data_ as it is (no area). */
    ui::lfodisplay::Data fullDisplayData() const noexcept
    {
        auto data = data_;
        const auto edit = full_.edit.rect();
        if (! edit.empty())
        {
            const auto graph = full_.displayRect().inset (8);
            const int top = edit.bottom() + 8;
            data.area = { graph.x, top, graph.w, std::max (0, graph.bottom() - 6 - top) };
            data.aperture = edit;
        }
        return data;
    }

    ui::lfodisplay::Data compactData() const noexcept
    {
        ui::lfodisplay::Data d = data_;
        d.area = ui::playrows::plotRect (compact_.displayRect());
        return d;
    }
    int gridOf (int lfo) const noexcept { return gridXOf (lfo); }
    int gridXOf (int lfo) const noexcept { return lfo >= 1 && lfo <= kLfos ? gridX_[lfo - 1] : 8; }
    int gridYOf (int lfo) const noexcept { return lfo >= 1 && lfo <= kLfos ? gridY_[lfo - 1] : 8; }
    static constexpr int kLfos = 10;

    // ---- the drawn shape and the content state (public for the tests)
    static ui::lfo::ShapeModel modelOf (const synthyboy::ContentLfoShape& s)
    {
        ui::lfo::ShapeModel m;
        m.n = static_cast<int> (std::min<size_t> (s.points.size(), ui::lfo::kMaxPoints));
        for (int i = 0; i < m.n; ++i)
        {
            const synthyboy::ContentPoint& cp = s.points[static_cast<size_t> (i)];
            m.p[i] = ui::lfo::Point (cp.x, cp.y, cp.power);
            m.p[i].mx = modOf (cp.modX);
            m.p[i].my = modOf (cp.modY);
            m.p[i].mc = modOf (cp.modCurve);
        }
        m.smooth = s.smooth;
        m.loop = s.loopPoint;
        m.linkCycleEnds = s.linkCycleEnds;
        m.linkLoopEnds = s.linkLoopEnds;
        m.clean();
        return m;
    }
    /** The content entry of a shape and, stored with it (state version 5, "grid_x" / "grid_y"), the grid of the editor (0 = not stored). */
    static synthyboy::ContentLfoShape contentOf (int lfo, const ui::lfo::ShapeModel& m, int gridX = 0, int gridY = 0)
    {
        synthyboy::ContentLfoShape s;
        s.lfo = lfo;
        s.gridX = gridX >= ui::lfo::kMinGrid && gridX <= ui::lfo::kMaxGrid ? gridX : 0;
        s.gridY = gridY >= ui::lfo::kMinGrid && gridY <= ui::lfo::kMaxGrid ? gridY : 0;
        s.name = "Drawn";
        s.smooth = m.smooth;
        s.loopPoint = m.loop >= 0 && m.loop < m.n ? m.loop : -1;
        s.linkCycleEnds = m.linkCycleEnds;
        s.linkLoopEnds = m.linkLoopEnds;
        for (int i = 0; i < m.n; ++i)
        {
            synthyboy::ContentPoint cp (m.p[i].x, m.p[i].y, m.p[i].power);
            cp.modX = refOf (m.p[i].mx);
            cp.modY = refOf (m.p[i].my);
            cp.modCurve = refOf (m.p[i].mc);
            s.points.push_back (cp);
        }
        return s;
    }
    /** The modulation of a point as the model holds it (the choice id of the source) from the content state (the key of the source) and back; a source the table does not know is none. */
    static ui::lfo::PointMod modOf (const synthyboy::ContentModRef& r)
    {
        ui::lfo::PointMod m;
        if (! r.present)
            return m;
        for (size_t i = 0; i < synthyboy::modSourceCount(); ++i)
            if (r.source == synthyboy::modSourceTable()[i].key)
            {
                m.source = static_cast<int> (i) + 1;
                m.amount = std::clamp (r.amount, -1.0f, 1.0f);
                break;
            }
        return m;
    }
    static synthyboy::ContentModRef refOf (const ui::lfo::PointMod& m)
    {
        synthyboy::ContentModRef r;
        if (m.on() && m.source <= static_cast<int> (synthyboy::modSourceCount()))
        {
            r.present = true;
            r.source = synthyboy::modSourceTable()[m.source - 1].key;
            r.amount = m.amount;
        }
        return r;
    }

private:
    static constexpr int kGroundX0 = 852, kGroundX1 = 1280;
    static uint32_t mixIn (uint32_t h, int32_t v) noexcept
    {
        for (int b = 0; b < 4; ++b)
            h = (h ^ ((static_cast<uint32_t> (v) >> (8 * b)) & 255u)) * 16777619u;
        return h;
    }
    using ModRow = uibind::ModRow;

    // ======================================================================================================================================================================
    // building: the selected LFO (the type and the number) decides the parameters behind the ids
    void bind (int id, ui::Component* c, T type, const char* suffix)
    {
        Item it;
        it.id = id; it.c = c; it.type = type;
        it.param = suffix != nullptr && suffix[0] != '\0' ? uibind::indexOfKey (prefix_ + suffix) : -1;
        if (c != nullptr)
            c->setId (id);
        items_.push_back (it);
    }
    /** Paint the corresponding slice of the same gradient exported for the host skin. */
    static void drawRootGround (gpu::SceneBuilder& sb, const ui::Rect& area)
    {
        if (area.empty())
            return;
        const auto& look = ui::theme::L();
        ui::Cell cell (sb, area, 0);
        const float y = static_cast<float> (area.y) - 0.001f;
        const float rows[2] { y, y };
        if (sb.curve (rows, 2, area.x, area.w) >= 0)
            sb.fillGradient (static_cast<float> (area.bottom()), page::kTabY, page::kTabY + page::kTabH,
                             ui::theme::rgba (look.bg0), ui::theme::rgba (look.bg1));
    }

    void updateHostedEditor (IInstanceView& inst, UiStack& stack, int highlight, double nowMs,
                             const synthyboy::LiveValues* live, bool refresh)
    {
        const auto useNative = [&]
        {
            return inst.nativeLfoControls() && stack.depth() == 1 && stack.element() == kElementLfo
                && &ui::theme::L() == &ui::theme::kLook1;
        };
        const auto pull = [&]
        {
            full_.setNativePageControls (useNative());
            update (inst, highlight, stack, nowMs, live);
        };
        if (refresh || full_.nativePageControls() != useNative())
            pull();
        if (page_)
            syncNativeTargets (inst, stack, false);
        full_.setNativePageControls (useNative());
        const auto changes = stack.changes();
        updateNativeEditor (inst, stack, nowMs);
        updateNativeDetail (inst, stack, nowMs);
        if (stack.changes() != changes)
        {
            pull();
            publishNativeEditor (inst, nativeEditorView (inst, stack));
            publishNativeDetail (inst, nativeDetailView (inst, stack));
        }
        if (! full_.nativePageControls())
        {
            if (nativePublished_)
                inst.setNativeLfoState ({});
            nativePublished_ = false;
            return;
        }
        auto state = nativeView (inst);
        for (int field = 0; field < nativeui::lfo::kCount; ++field)
        {
            float value;
            if (inst.takeNativeLfo (state, field, value) && applyNative (inst, stack, field, value))
            {
                pull();
                state = nativeView (inst);
            }
        }
        updateNativeEditor (inst, stack, nowMs);
        inst.setNativeLfoState (state);
        nativePublished_ = state.layout != 0;
    }

    void drawRootGroundWithApertures (gpu::SceneBuilder& sb) const
    {
        namespace n = nativeui::lfo;
        constexpr int maxHoles = n::kCount + 2, top = page::kTabY, bottom = page::kTabY + page::kTabH;
        ui::Rect holes[maxHoles];
        int edges[2 + 2 * maxHoles] { top, bottom };
        int numHoles = 0, numEdges = 2;
        auto cut = [&](const ui::Rect& r)
        {
            if (! r.empty())
            {
                holes[numHoles++] = r;
                edges[numEdges++] = r.y;
                edges[numEdges++] = r.bottom();
            }
        };
        for (int field = 0; field < n::kCount; ++field)
            if ((nativeFields_ & n::bit (field)) != 0)
                cut (nativeRect (field));
        if (! random())
            cut (nativeRect (n::kAction, n::kExpand));
        if (nativePlayEditor_)
            cut (full_.close.rect());
        std::sort (edges, edges + numEdges);
        for (int i = 1; i < numEdges; ++i)
        {
            const int y = edges[i - 1], h = edges[i] - y;
            if (h == 0)
                continue;
            std::pair<int, int> spans[maxHoles];
            int numSpans = 0;
            for (int k = 0; k < numHoles; ++k)
                if (y >= holes[k].y && y < holes[k].bottom())
                    spans[numSpans++] = { holes[k].x, holes[k].right() };
            std::sort (spans, spans + numSpans);
            int x = 0;
            for (int k = 0; k < numSpans; ++k)
            {
                drawRootGround (sb, { x, y, spans[k].first - x, h });
                x = std::max (x, spans[k].second);
            }
            drawRootGround (sb, { x, y, page::kW - x, h });
        }
    }

    static void coverRootMarkers (gpu::SceneBuilder& sb)
    {
        for (const auto& view : layout::kViews)
            if (view.kind == layout::kKindLfoUi)
            {
                for (const auto& spot : { view.markers[0].rect, view.instance, view.markers[1].rect })
                    drawRootGround (sb, { spot.x - 1, spot.y - 1, spot.w + 2, spot.h + 2 });
                if (const auto* asset = backgroundOf (view.name))
                    drawRootGround (sb, { asset->sentinelX, page::kTabY + asset->sentinelY, 5, 5 });
                return;
            }
    }

    int nativeFieldAt (int x, int y) const noexcept
    {
        namespace n = nativeui::lfo;
        if (! full_.nativePageControls())
            return -1;
        for (int field = 0; field < n::kCount; ++field)
            if ((nativeFields_ & n::bit (field)) != 0 && nativeRect (field).contains (x, y))
                return field;
        return ! random() && nativeRect (n::kAction, n::kExpand).contains (x, y) ? n::kAction : -1;
    }

    void selectNativeQGroup (int field)
    {
        const int target = nativeTargets_[field];
        if (target < 0)
            return;
        for (int group = 0; group < full_.numGroups(); ++group)
            for (int slot = 0; slot < ui::QGroup::kSlots; ++slot)
                if (paramOfMember (full_.groups[group].member (slot)) == target)
                {
                    full_.setActiveGroup (group);
                    return;
                }
    }

    nativeui::lfo::ViewState nativeView (const IInstanceView& inst) noexcept
    {
        namespace n = nativeui::lfo;
        nativeFields_ = 0;
        if (! full_.nativePageControls())
            return {};
        n::ViewState state;
        state.selection = slot() + 1;
        state.preset = inst.stateGeneration();
        const int syncTarget = nativeTargets_[n::kSync];
        const auto sync = ui::lfo::syncModeOf (syncTarget >= 0 ? static_cast<int> (std::lround (inst.param (syncTarget))) : 0);
        const bool key = sync == ui::lfo::SyncMode::Key, beats = sync == ui::lfo::SyncMode::Bpm;
        state.layout = (random() ? 3 : 0) + (key ? 3 : beats ? 2 : 1);
        state.values[n::kSelection] = static_cast<float> (slot()) / (n::kInstances - 1);
        state.values[n::kLayout] = static_cast<float> (state.layout) / n::kLayoutLast;
        state.fields = n::bit (n::kSelection) | n::bit (n::kAction);
        for (int field = 0; field < n::kCount; ++field)
        {
            const int target = nativeTargets_[field];
            if (target < 0 || ! nativeFieldVisible (field, key, beats))
                continue;
            state.values[field] = uibind::normOf (target, inst.param (target));
            state.fields |= n::bit (field);
        }
        nativeFields_ = state.fields;
        return state;
    }

    bool nativeFieldVisible (int field, bool key, bool beats) const noexcept
    {
        namespace n = nativeui::lfo;
        switch (field)
        {
            case n::kRate: return ! key && ! beats;
            case n::kDivision: return beats;
            case n::kTranspose: case n::kTune: return key;
            case n::kShape: case n::kSmooth: case n::kSmoothTime: case n::kRise: case n::kMode:
            case n::kPhase: case n::kDelay: case n::kHost: case n::kDirection: case n::kSwing:
            case n::kStereo: case n::kMono: return ! random();
            case n::kStyle: case n::kRandomMode: case n::kRandomStereo: return random();
            default: return true;
        }
    }

    bool applyNative (IInstanceView& inst, UiStack& stack, int field, float value)
    {
        namespace n = nativeui::lfo;
        if (field == n::kSelection)
        {
            const int selected = static_cast<int> (std::lround (value * (n::kInstances - 1)));
            if (selected == slot())
                return false;
            uibind::chooseLfo (inst, selected + 1);
            return true;
        }
        if (field == n::kAction)
        {
            const int action = static_cast<int> (std::lround (value * n::kActionLast));
            if (action == n::kTargets)
                return stack.openDetail (kPageTargets);
            if (action == n::kExpand && ! random())
            {
                openEditor (stack, inst);
                return true;
            }
            return false;
        }
        const int target = nativeTargets_[field];
        if (target < 0 || (field == n::kSmoothTime && inst.param (nativeTargets_[n::kSmooth]) < 0.5f))
            return false;
        const auto& info = uibind::info (target);
        const float plain = synthyboy::constrainValue (info, synthyboy::fromNormalised (info, value));
        if (plain == inst.param (target))
            return false;
        selectNativeQGroup (field);
        if (field == n::kShape)
        {
            auto* shape = find (kPagePicker);
            if (shape == nullptr)
                return false;
            const int next = static_cast<int> (std::lround (value * n::kShapeLast));
            write (*shape, ui::Change::indexed (kPagePicker, next, next), inst);
        }
        else
            uibind::writeNorm (inst, target, value);
        return true;
    }

    void configure (IInstanceView& inst)
    {
        (void) inst;
        configured_ = true;
        const bool rnd = rnd_ != 0;
        prefix_ = rnd ? "rlfo" + std::to_string (rnd_) + "." : "lfo" + std::to_string (lfoNum_) + ".";
        for (int field = 0; field < nativeui::lfo::kCount; ++field)
        {
            const char* suffix = nativeui::lfo::kSuffixes[field];
            nativeTargets_[field] = suffix != nullptr ? uibind::indexOfKey (prefix_ + suffix) : -1;
        }
        items_.clear();
        // ---- the compact view
        bind (kInstance, &compact_.instance, T::Inert, "");
        bind (kFullButton, &compact_.full, T::Inert, "");
        bind (kShape, &compact_.shape, T::Stepper, rnd ? "" : "shape");
        bind (kStyle, &compact_.style, T::Stepper, rnd ? "style" : "");
        bind (kRate, &compact_.rate.rate, T::Knob, "rate");
        bind (kDivision, &compact_.rate.division, T::Stepper, "division");
        bind (kSyncMode, &compact_.rate.modeCells, T::Inert, "");
        bind (kRise, &compact_.rise, T::Knob, rnd ? "" : "fade_in");
        bind (kSmoothTime, &compact_.smoothTime, T::Knob, rnd ? "" : "smooth_time");
        bind (kSync, &compact_.sync, T::Stepper, "sync");
        bind (kStereoToggle, &compact_.stereo, T::Toggle, rnd ? "stereo" : "");
        // ---- the full screen
        bind (kClose, &full_.close, T::Inert, "");
        bind (kSelector, &full_.selector, T::Inert, "");
        bind (kPageInstance, &full_.instance, T::Inert, "");
        bind (kSyncMode, &full_.syncSwitch.mode, T::Inert, "");
        bind (kSyncTrip, &full_.rate.trip, T::Inert, "");
        bind (kSyncDot, &full_.rate.dot, T::Inert, "");
        bind (kTargets, &full_.targets, T::Inert, "");
        bind (kPicker, &full_.picker, T::Picker, rnd ? "style" : "shape");
        bind (kPagePicker, &full_.namedPicker, T::Stepper, rnd ? "style" : "shape");
        bind (kSmoothPick, &full_.smoothSwitch, T::Toggle, rnd ? "" : "smooth");   // the Q member of the LED on the label of SMOOTH
        bind (kHostPick, &full_.hostPick, T::Toggle, rnd ? "" : "host");   // touch only: a member of no Q group
        bind (kEdit, &full_.edit, T::Inert, "");
        bind (kRate, &full_.rate.rate, T::Knob, "rate");
        bind (kDivision, &full_.rate.division, T::Stepper, "division");
        bind (kRise, &full_.rise, T::Knob, rnd ? "" : "fade_in");
        bind (kSmoothTime, &full_.smoothTime, T::Knob, rnd ? "" : "smooth_time");
        bind (kMode, &full_.mode, T::Stepper, rnd ? "" : "sync_type");
        bind (kModeList, &full_.modeList, T::List, rnd ? "sync_type" : "");   // the trigger of a random LFO (two options) is a LED list, the six long names of an LFO a stepper
        bind (kPhase, &full_.phase, T::Knob, rnd ? "" : "phase");
        bind (kDelay, &full_.delay, T::Knob, rnd ? "" : "delay");
        bind (kDirection, &full_.direction, T::List, rnd ? "" : "direction");
        bind (kSwing, &full_.swing, T::Knob, rnd ? "" : "swing");
        bind (kStereo, &full_.stereo, T::Knob, rnd ? "" : "stereo");
        bind (kMono, &full_.monoQ, T::Toggle, rnd ? "" : "mono");
        bind (kStereoToggle, &full_.stereoToggle, T::Toggle, rnd ? "stereo" : "");
        bind (kKeyTranspose, &full_.keyTranspose, T::Number, "keytrack_transpose");
        bind (kKeyTune, &full_.keyTune, T::Number, "keytrack_tune");
        // ---- the ranges of the knobs and fields come from the parameters
        for (Item& it : items_)
        {
            if (it.c == nullptr || it.param < 0)
                continue;
            const synthyboy::ParamInfo& pi = uibind::info (it.param);
            if (it.type == T::Knob)
            {
                auto* k = static_cast<ui::Knob*> (it.c);
                k->state.bipolar = pi.min < 0.0f && pi.max > 0.0f;
                k->state.defaultValue = synthyboy::toNormalised (pi, pi.def);
            }
            else if (it.type == T::Number)
            {
                auto& st = static_cast<ui::NumberField*> (it.c)->state;
                st.min = pi.min; st.max = pi.max; st.defaultValue = pi.def; st.step = 1.0;
            }
        }
        compact_.configure (rnd);
        full_.configure (rnd);
        // the parameters of the display
        values_ = synthyboy::ParamValues::defaults();
        dispIdx_.clear();
        const std::string lp = "lfo" + std::to_string (lfoNum_) + ".";
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            if (std::strncmp (synthyboy::paramTable()[i].key, lp.c_str(), lp.size()) == 0)
                dispIdx_.push_back (static_cast<int> (i));
        seen_.assign (dispIdx_.size(), 0.0f);
        haveCurve_ = false;
        std::fill (std::begin (trace_), std::end (trace_), 0.0f);
        qSlots_.forget();
        modsDirty_ = true;
        gesture_ = nullptr; gestureDown_ = false;
        editorContent_.reset();
        editorContentExternal_ = false;
        editorShapeIdx_ = -1;
        pageSig_ = -1;
        nCache_ = 0;
    }

    // ======================================================================================================================================================================
    // the pages above the full screen
    void syncPages (const UiStack& stack, IInstanceView& inst)
    {
        const int top = stack.depth() >= 2 ? stack.top() : 0;
        const int sig = top * 16 + (top == kPageMods ? modsTarget_ & 0xff : 0);
        if (top == pageTop_ && sig == pageSig_)
            return;
        // A standalone instance-list is a cached popup scene.  Clear its
        // model as soon as the stack leaves the list; otherwise a same-sized
        // RND1 -> RND2 transition can leave the old dim/list cells resident
        // until the tab is reopened.
        if (pageTop_ == kPageList && top != kPageList)
        {
            list_.state.items = nullptr;
            list_.state.count = 0;
            list_.state.current = -1;
            list_.setPopup ({ 0, kTabY, w_, kTabH });
        }
        if (gesture_ != nullptr && pageTop_ != top)
            dropGesture();
        if (pageTop_ == kPageEditor && top != kPageEditor && top != kPageList && ! editorContentExternal_)
            flushShape (inst, lastNowMs_);
        if (top == kPageList && pageTop_ != kPageList)
            underList_ = stack.depth() >= 3 ? stack.id (stack.depth() - 2) : 0;   // the page the list lies over (the stack knows it: a page may have been closed and the list opened in one step)
        const bool fromTargetsList = pageTop_ == kPageList && underList_ == kPageTargets;
        pageTop_ = top;
        pageSig_ = sig;
        switch (top)
        {
            case kPageTargets: if (! fromTargetsList) targetsPageIndex_ = 0; loadTargets (inst, true); break;
            case kPageMods: loadMods (inst); break;
            case kPageEditor: setIds(); syncEditor (inst); break;
            case kPagePointMod: setIds(); loadPointMod (inst, true); break;
            case kPageKeyboard: beginKeyboard(); break;
            default: break;
        }
        modsDirty_ = true;
        qSlots_.forget();
    }

    // ---- the targets page: the rows of the matrix whose source is this LFO
    ModRow targetRows_[ui::ModTargetsPage::kPerPage];
    int targetTotal_ = 0;
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, source(), slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + ui::ModTargetsPage::kPerPage - 1) / ui::ModTargetsPage::kPerPage);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * ui::ModTargetsPage::kPerPage, n = std::clamp (total - first, 0, ui::ModTargetsPage::kPerPage);
        ui::ModTargetsPage::Entry e[ui::ModTargetsPage::kPerPage];
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), e[i].name, sizeof e[i].name);
            e[i].depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            e[i].bidirectional = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            e[i].bypassed = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            targetRows_[i] = { slots[first + i], source() };
        }
        // only a change of the rows is loaded again (a reload resets the touch of a row that is being dragged)
        bool same = ! fresh && n == targets_.numRows() && total == targetTotal_;
        for (int i = 0; same && i < n; ++i)
        {
            const ui::ModTargetsPage::Row& r = targets_.rows[i];
            same = std::strcmp (r.name, e[i].name) == 0 && std::fabs (r.depth.state.value - (e[i].depth + 1.0) * 0.5) < 1.0e-9 && r.polarity.state.on == e[i].bidirectional && r.bypass.state.on == ! e[i].bypassed;
        }
        if (same)
            return;
        targetTotal_ = total;
        char title[16];
        std::snprintf (title, sizeof title, rnd_ != 0 ? "RND %d" : "LFO %d", selection());
        targets_.load (e, n, title, ui::theme::sourceColour (rnd_ != 0 ? "RND" : "LFO"), total, targetsPageIndex_);
        targets_.back.setId (kTargetsBack);
        targets_.page.setId (kTargetsPage);
        targets_.add.setId (kTargetsAdd);
        for (int r = 0; r < ui::ModTargetsPage::kPerPage; ++r)
            targets_.setRowIds (r, kTargetsRow + 4 * r, kTargetsRow + 4 * r + 1, kTargetsRow + 4 * r + 2, kTargetsRow + 4 * r + 3);
    }
    /** The name of a destination choice ("LFO 1 Rate"); the appended destinations that are no table parameter get a generic name. */
    static void targetName (size_t destChoice, char* out, size_t cap)
    {
        const int p = synthyboy::modDestinationParamIndex (destChoice);
        if (p >= 0)
            std::snprintf (out, cap, "%s", uibind::info (p).name);
        else
            std::snprintf (out, cap, "Destination %d", static_cast<int> (destChoice));
    }
    void addTarget (IInstanceView& v, int destChoice)
    {
        const int modulationSource = source();
        const int slot = uibind::addTargetSlot (v, modulationSource, destChoice);
        if (slot < 0)
            return;
        constexpr int rows = ui::ModTargetsPage::kPerPage;
        const int position = uibind::targetPosition (v, modulationSource, slot);
        targetsPageIndex_ = position / rows;
        loadTargets (v, true);
        targets_.selectRow (position % rows);
        modsDirty_ = true;
    }

    // ---- the list popup: the instances, the choices of a parameter, the groups of the destinations and the parameters of a group
    enum class ListKind : uint8_t { None, Instance, PageInstance, Param, Dest, Editor, Plus };
    using DestIndex = uibind::DestIndex;
    ListKind listKind_ = ListKind::None;
    int listParam_ = -1, listGroup_ = -1, underList_ = 0;
    std::vector<const char*> listNames_;
    /** The list of the selector of the compact view: LFO 1 .. 10, RND 1 .. 4 (scrolls), a popup over the third (not a page of the stack): the same behaviour as the envelope's and the oscillator's selector. */
    void openInstanceList (const IInstanceView& v, ListKind kind = ListKind::Instance)
    {
        const ui::Rect third { kCompactX, kTabY + 4, kCompactW, kTabH - 8 };
        listKind_ = kind;   // Instance: the cells of the carousel; Plus: the two free kinds of a tap on `+`
        fillList (&v);
        const int rows = std::max (1, std::min (list_.state.count, (third.h - 6 - ui::playrows::kSelectorH - 6 - 2 * ui::ListPopup::kPad) / ui::ListPopup::kRowH));
        list_.place ({ third.x + 6, third.y + 6 + ui::playrows::kSelectorH, third.w - 12, rows * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup (third);
        list_.showCurrent();
        instPopup_ = true;
    }
    void closeInstanceList()
    {
        instPopup_ = false;
        listKind_ = ListKind::None;
        list_.state.current = -1;
        placePageList();
    }
    bool instPopup_ = false;   // the list of the selector of the compact view is open (main state only)
    void placePageList()
    {
        list_.place ({ 340, kTabY + 20, 600, 9 * ui::ListPopup::kRowH + 2 * ui::ListPopup::kPad });
        list_.setPopup ({ 0, kTabY, w_, kTabH });
    }
    bool isSourceAssigned (int id) const noexcept
    {
        if (id >= 1 && id <= kLfos)
            return scan_.used[uibind::lfoSource (id)];
        if (id > uibind::kRndBase && id <= uibind::kRndBase + uibind::kSelRnds)
            return scan_.used[uibind::rndSource (id - uibind::kRndBase)];
        return false;
    }
    void fillList (const IInstanceView* v)
    {
        if (listKind_ == ListKind::Editor)
        {
            fillEditorList();
            return;
        }
        listNames_.clear();
        list_.state.header = nullptr;
        list_.state.headerBack = false;
        list_.state.current = -1;
        list_.state.dotMask = 0;
        list_.state.dotColours = nullptr;
        list_.state.icons = nullptr;
        if (listKind_ == ListKind::Instance)
        {
            for (int i = 0; i < playCells_.n; ++i)
                listNames_.push_back (playCells_.names[i]);   // the same cells as the carousel and the tab's selector
            list_.state.current = playCells_.selectedPos;
            list_.state.dotMask = playDots_;
        }
        else if (listKind_ == ListKind::PageInstance)
        {
            for (int i = 0; i < kInstances; ++i)
            {
                listNames_.push_back (instanceName (i));
                const int id = i < kLfos ? i + 1 : uibind::kRndBase + (i - kLfos + 1);
                if (isSourceAssigned (id))
                    list_.state.dotMask |= (1u << i);
            }
            list_.state.current = slot();
        }
        else if (listKind_ == ListKind::Plus)
        {
            int ch[ui::UsedSelector::kMaxChoices];
            const int n = full_.selector.plusChoices (ch, ui::UsedSelector::kMaxChoices);
            static std::string names[ui::UsedSelector::kMaxChoices];
            for (int i = 0; i < n; ++i)
            {
                plusIds_[i] = full_.selector.state.items[ch[i]].id;
                names[i] = uibind::selectorRowName (uibind::SelKind::Lfo, plusIds_[i]);
                listNames_.push_back (names[i].c_str());
            }
        }
        else if (listKind_ == ListKind::Param && listParam_ >= 0)
        {
            const synthyboy::ParamInfo& pi = uibind::info (listParam_);
            for (int i = 0; i < pi.numChoices; ++i)
                listNames_.push_back (pi.choices[i]);
            if (v != nullptr)
                list_.state.current = static_cast<int> (std::lround (v->param (listParam_) - pi.min));
        }
        else if (listKind_ == ListKind::Dest)
        {
            const DestIndex& d = DestIndex::get();
            if (listGroup_ < 0)
                for (const std::string& g : d.groups)
                    listNames_.push_back (g.c_str());
            else
            {
                for (int c : d.choices[static_cast<size_t> (listGroup_)])
                    listNames_.push_back (uibind::info (synthyboy::modDestinationParamIndex (static_cast<size_t> (c))).name);
                list_.state.header = d.groups[static_cast<size_t> (listGroup_)].c_str();
                list_.state.headerBack = true;
            }
        }
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.showCurrent();
    }
    /** The "+" of the selector with two free kinds: a two-row list "LFO n" / "RND m" over the page; a choice selects it (nothing is written). */
    int plusIds_[ui::UsedSelector::kMaxChoices] = {};
    void openPlusList (UiStack& stack)
    {
        listKind_ = ListKind::Plus;
        placePageList();
        stack.openDetail (kPageList);
        fillList (nullptr);
    }
    void openPageInstanceList (UiStack& stack, IInstanceView& v)
    {
        listKind_ = ListKind::PageInstance;
        placePageList();
        stack.openDetail (kPageList);
        fillList (&v);
    }
    static const char* const* instanceNames() noexcept
    {
        static const char* const n[kInstances] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "LFO 9", "LFO 10", "RND 1", "RND 2", "RND 3", "RND 4" };
        return n;
    }
    static const char* instanceName (int i) noexcept { return instanceNames()[std::clamp (i, 0, kInstances - 1)]; }
    /** The instance as one number 0 .. 13: LFO 1 .. 10, then RND 1 .. 4. */
    int slot() const noexcept { return rnd_ != 0 ? kLfos + rnd_ - 1 : lfoNum_ - 1; }
    /** The shown LFO from the shared view state (Play LFO and mpcTabView.rnd): a change of either reconfigures the page. */
    void adoptSelection (IInstanceView& v)
    {
        const int id = uibind::selectedLfo (v);   // 1 .. 10, or kRndBase + m
        const int num = std::clamp (v.playSelection (2), 1, kLfos), r = id > uibind::kRndBase ? id - uibind::kRndBase : 0;
        if (! configured_ || num != lfoNum_ || r != rnd_)
        {
            lfoNum_ = num;
            rnd_ = r;
            configure (v);
        }
    }
    /** A choice (a position among the cells of the compact view's carousel and list: used LFOs, the selected one, `+`): view state only, nothing is written to the engine. The `+` cell selects the lowest free LFO (or random LFO) when only one
        kind is free; with two free kinds it opens the two-row list over the third (`openPlusPopup`). */
    void selectSlot (int s, IInstanceView& v)
    {
        if (s >= 0 && s < playCells_.n && playCells_.id[s] == uibind::SelectorCells::kPlusCell)
        {
            int ch[ui::UsedSelector::kMaxChoices];
            if (full_.selector.plusChoices (ch, ui::UsedSelector::kMaxChoices) > 1)
            {
                plusPending_ = true;
                return;
            }
        }
        const int id = uibind::cellChoice (full_.selector, playCells_, s);
        if (id > 0)
            uibind::chooseLfo (v, id);
        adoptSelection (v);
    }
    /** The knob steps through the shown candidates, followed by the existing
        '+' choices. When both families are free they each get a knob position. */
    int selectorQChoices (int* choices) const noexcept
    {
        int count = 0;
        for (int i = 0; i < playCells_.n; ++i)
            if (playCells_.id[i] > 0)
                choices[count++] = playCells_.id[i];
        int free[ui::UsedSelector::kMaxChoices];
        const int n = full_.selector.plusChoices (free, ui::UsedSelector::kMaxChoices);
        for (int i = 0; i < n; ++i)
            choices[count++] = full_.selector.state.items[free[i]].id;
        return count;
    }
    void readSelectorQ (IInstanceView& v, const UiStack& stack) noexcept
    {
        if (! page_)
            return;
        const bool visible = stack.depth() == 1 && stack.element() == kElementLfo;
        const bool owned = selectorQOwner_ == &v;
        const bool same = owned && selectorQGeneration_ == v.stateGeneration();
        float value;
        if ((visible || owned) && v.takeQVirtual (kVirtualSelector, value) && visible && same)
        {
            int choices[kInstances];
            const int count = selectorQChoices (choices);
            if (count > 0)
            {
                selectorQValue_ = std::clamp (value, 0.0f, 1.0f);
                const int pos = static_cast<int> (std::lround (selectorQValue_ * (count - 1)));
                if (choices[pos] != uibind::selectedLfo (v))
                    uibind::chooseLfo (v, choices[pos]);
            }
        }
        if (! visible || ! same)
        {
            selectorQOwner_ = nullptr;
            selectorQValue_ = -1.0f;
            if (visible)
                modsDirty_ = true;
        }
    }
    void reportSelectorQ (IInstanceView& v) noexcept
    {
        int choices[kInstances];
        const int count = selectorQChoices (choices);
        const int* selected = std::find (choices, choices + count, uibind::selectedLfo (v));
        const int pos = selected < choices + count ? static_cast<int> (selected - choices) : 0;
        const int last = std::max (0, count - 1);
        if (selectorQValue_ < 0.0f || std::lround (selectorQValue_ * last) != pos)
            selectorQValue_ = last > 0 ? static_cast<float> (pos) / last : 0.0f;
        v.setQVirtual (kVirtualSelector, selectorQValue_);
        selectorQOwner_ = &v;
        selectorQGeneration_ = v.stateGeneration();
    }
    void addSelectorQ (int* targets, int capacity) const noexcept
    {
        if (! page_)
            return;
        for (int i = capacity - 1; i >= 0; --i)
            if (targets[i] < 0)
            {
                targets[i] = QRouter::encode (kVirtualSelector, QRouter::kVirtual);
                break;
            }
    }
    bool plusPending_ = false;   // a tap on the `+` cell of the compact view with two free kinds: the list opens at the end of the handler
    const IInstanceView* selectorQOwner_ = nullptr;
    uint64_t selectorQGeneration_ = 0;
    float selectorQValue_ = -1.0f;
    uibind::SelectorCells playCells_;
    unsigned playDots_ = 0;
public:
    const uibind::SelectorCells& playCells() const noexcept { return playCells_; }
    const ui::UsedSelector& tabSelector() const noexcept { return full_.selector; }
    /** The LFO tab was left (page_view_base.h): a selected LFO or random LFO that is neither used nor edited (a `+` pick the user did not touch) falls back to the last used one (uibind::fallbackSelection). */
    void leftTab (IInstanceView& v)
    {
        hideNativeTargets (v);
        publishNativeEditor (v, {});
        publishNativeDetail (v, {});
        if (nativePublished_)
        {
            v.setNativeLfoState ({});
            nativePublished_ = false;
        }
        if (selectorQOwner_ == &v)
        {
            float pending;
            v.takeQVirtual (kVirtualSelector, pending);
            selectorQOwner_ = nullptr;
            selectorQValue_ = -1.0f;
        }
        uibind::leaveSelector (uibind::SelKind::Lfo, v);
    }
private:

    // ---- the modulations page of one control (a long press on a knob or a field)
    ModRow modRows_[ui::ModulationsPage::kPerPage];
    NativeModTargets nativeModulations_;
    int modsPageIndex_ = 0;
    uint64_t modsPreset_ = 0;
    int modsTarget_ = 0, modsTargetParam_ = -1, modsAddOffer_ = 0;
    bool modsContextValid (const IInstanceView& inst) const noexcept
    {
        const auto* control = item (modsTarget_);
        return pageTop_ == kPageMods && modsTargetParam_ >= 0 && modsPreset_ == inst.stateGeneration()
            && uibind::selectedLfo (inst) == (rnd_ > 0 ? uibind::kRndBase + rnd_ : lfoNum_)
            && control != nullptr && control->param == modsTargetParam_;
    }

    void loadMods (const IInstanceView& v, int focusSlot = -1)
    {
        const Item* t = item (modsTarget_);
        char title[13] = "MODULATIONS";
        const int target = t != nullptr ? t->param : -1;
        if (modsTargetParam_ != target)
        {
            modsPageIndex_ = 0;
            modsTargetParam_ = target;
        }
        if (t != nullptr && modsTargetParam_ >= 0)
        {
            if (t->c != nullptr && t->type == T::Knob)
                std::snprintf (title, sizeof title, "%.12s", static_cast<ui::Knob*> (t->c)->state.label);
            else if (t->c != nullptr && t->type == T::Number)
                std::snprintf (title, sizeof title, "%.12s", static_cast<ui::NumberField*> (t->c)->state.label);
        }
        const auto window = uibind::modulationWindow (v, modsTargetParam_, source(), modsPageIndex_, modRows_, focusSlot);
        modsPageIndex_ = window.page;
        if (window.total == 0)
            uibind::fillSourceBoard (mods_.board);
        mods_.load (window.entries, title, window.total, window.page);
        if (window.focusRow >= 0)
            mods_.selectRow (window.focusRow);
        mods_.page.setId (kModsPage);
        mods_.back.setId (kModsBack);
        mods_.addSource.setId (kModsAddStep);
        mods_.add.setId (kModsAdd);
        for (int r = 0; r < ui::ModulationsPage::kPerPage; ++r)
            mods_.setRowIds (r, kModsRow + 4 * r, kModsRow + 4 * r + 1, kModsRow + 4 * r + 2, kModsRow + 4 * r + 3);
        // what "add source" offers: this LFO first, then the first LFOs, envelopes and macros
        int offer[8], n = 0;
        auto push = [&] (int s) { if (s == ui::kNoSource || n >= 8) return; for (int i = 0; i < n; ++i) if (offer[i] == s) return; offer[n++] = s; };
        push (source());
        for (int s : { 7, 8, 1, 2, 9, 3, 19, 20 }) push (s);
        for (int i = 0; i < n; ++i)
            mods_.sourceIds[i] = offer[i];
        mods_.addSource.state.count = n;
        mods_.addSource.state.index = std::clamp (mods_.addSource.state.index, 0, std::max (0, n - 1));
        mods_.addSource.setItem (uibind::sourceName (offer[std::clamp (mods_.addSource.state.index, 0, std::max (0, n - 1))]));
        modsAddOffer_ = n;
        char tv[16] = "";
        if (modsTargetParam_ >= 0)
            uibind::formatValue (uibind::info (modsTargetParam_), v.param (modsTargetParam_), tv, sizeof tv);
        mods_.setTargetValue (tv);
    }
    void addModulation (IInstanceView& v, int src)
    {
        if (! modsContextValid (v))
            return;
        if (const int slot = uibind::ensureModulationSource (v, modsTargetParam_, src); slot >= 0)
        {
            mods_.setPickingSource (false);
            loadMods (v, slot);
        }
    }
    void refreshMods (const IInstanceView& v)
    {
        scan_ = uibind::scanUsed (v);   // which LFOs the selector shows
        for (Item& it : items_)
        {
            if (it.c == nullptr || it.param < 0 || (it.type != T::Knob && it.type != T::Number))
                continue;
            const ui::ModInput mi = uibind::modsOf (v, it.param, source(), nullptr);
            ui::ModInput& dst = it.type == T::Knob ? static_cast<ui::ModInput&> (static_cast<ui::Knob*> (it.c)->state) : static_cast<ui::ModInput&> (static_cast<ui::NumberField*> (it.c)->state);
            dst = mi;
        }
        if (pageTop_ == kPageTargets)
            loadTargets (v, false);
        else if (pageTop_ == kPageMods && modsTargetParam_ >= 0)
            loadMods (v);
    }

    // ======================================================================================================================================================================
    // reading: the states of the components from the parameters
    static void fmt (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
    {
        if (pi.unit == synthyboy::Unit::Hz)
        {
            const double v = static_cast<double> (plain);
            if (v >= 1000.0) std::snprintf (out, cap, "%.2f kHz", v / 1000.0);
            else if (v < 10.0) std::snprintf (out, cap, "%.2f Hz", v);
            else std::snprintf (out, cap, v < 100.0 ? "%.1f Hz" : "%.0f Hz", v);
            return;
        }
        uibind::formatValue (pi, plain, out, cap);
    }
    static void showStepper (ui::Stepper* st, int param, float plain)
    {
        const synthyboy::ParamInfo& pi = uibind::info (param);
        const int n = pi.numChoices > 0 ? pi.numChoices : static_cast<int> (pi.max - pi.min) + 1;
        const int idx = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, n - 1));
        st->state.count = n;
        st->state.index = idx;
        st->state.wrap = pi.numChoices <= 14;
        if (pi.choices != nullptr && idx < pi.numChoices)
            st->setItem (pi.choices[idx]);
        else
        {
            char t[13];
            std::snprintf (t, sizeof t, "%d", idx);
            st->setItem (t);
        }
    }
    /** The table index of a key of the LFO shown ("sync" -> lfo3.sync): looked up once per string literal and configuration (the update asks for a dozen of them every picture). */
    int paramOf (const char* suffix) const
    {
        for (int i = 0; i < nCache_; ++i)
            if (cacheKey_[i] == suffix)
                return cacheIdx_[i];
        const int idx = uibind::indexOfKey (prefix_ + suffix);
        if (nCache_ < kCache)
        {
            cacheKey_[nCache_] = suffix;
            cacheIdx_[nCache_] = idx;
            ++nCache_;
        }
        return idx;
    }
    void pull (IInstanceView& v)
    {
        for (Item& it : items_)
        {
            if (it.c == nullptr)
                continue;
            if (it.type == T::Inert)
            {
                if (it.id == kInstance)
                {
                    auto* st = static_cast<ui::Carousel*> (it.c);
                    st->state.count = playCells_.n; st->state.index = playCells_.selectedPos; st->state.wrap = true;
                    st->state.names = playCells_.names;
                    st->state.dotMask = playDots_; st->state.dotColours = nullptr;
                    st->setItem (playCells_.n > 0 ? playCells_.names[playCells_.selectedPos] : instanceName (slot()));
                }
                if (it.id == kPageInstance)
                {
                    auto* st = static_cast<ui::Carousel*> (it.c);
                    st->state.count = playCells_.n; st->state.index = playCells_.selectedPos; st->state.wrap = true;
                    st->state.names = playCells_.names;
                    st->state.dotMask = playDots_; st->state.dotColours = nullptr;
                    st->setItem (playCells_.n > 0 ? playCells_.names[playCells_.selectedPos] : instanceName (slot()));
                }
                it.c->setEnabled (true);
                if (it.id == kInstance || it.id == kSelector || it.id == kPageInstance || it.id == kTargets || it.id == kClose)
                    it.c->setInactive (false);   // navigation stays live across an unused/off instance
                continue;
            }
            if (it.param < 0)
            {
                it.c->setEnabled (false);
                continue;
            }
            const float plain = v.param (it.param);
            const synthyboy::ParamInfo& pi = uibind::info (it.param);
            switch (it.type)
            {
                case T::Knob:
                {
                    auto* k = static_cast<ui::Knob*> (it.c);
                    k->state.value = k->state.modValue = uibind::normOf (it.param, plain);
                    fmt (pi, plain, k->state.valueText, sizeof k->state.valueText);
                    break;
                }
                case T::Number: static_cast<ui::NumberField*> (it.c)->state.value = plain; break;
                case T::Stepper: showStepper (static_cast<ui::Stepper*> (it.c), it.param, plain); break;
                case T::Toggle: static_cast<ui::ToggleIcon*> (it.c)->state.on = plain >= 0.5f; break;
                case T::List:
                {
                    auto* ll = static_cast<ui::LedList*> (it.c);
                    ll->setOptions (pi.choices, std::min (static_cast<int> (pi.numChoices), static_cast<int> (ui::LedList::kMaxOptions)));
                    ll->state.index = std::clamp (static_cast<int> (std::lround (plain - pi.min)), 0, std::max (0, ll->state.count - 1));
                    break;
                }
                case T::Picker: static_cast<ui::ShapePicker*> (it.c)->state.index = std::clamp (static_cast<int> (std::lround (plain)), 0, static_cast<ui::ShapePicker*> (it.c)->count() - 1); break;
                default: break;
            }
            it.c->setEnabled (! (it.id == kSmoothPick && full_.nativeSmooth()));
        }
        // ---- what depends on the sync, the mode and the smoothing
        const int syncP = paramOf ("sync");
        const int sync = syncP >= 0 ? static_cast<int> (std::lround (v.param (syncP))) : 0;
        const ui::lfo::SyncMode sm = ui::lfo::syncModeOf (sync);
        compact_.rate.setMode (sm);
        full_.rate.setMode (sm);
        full_.setSync (sync);
        full_.setKeyActive (sm == ui::lfo::SyncMode::Key);
        if (sm == ui::lfo::SyncMode::Key)   // the rate does not matter: the key decides
            compact_.rate.rate.state.valueText[0] = full_.rate.rate.state.valueText[0] = '\0';
        const int smoothP = paramOf ("smooth");
        const bool smoothOn = smoothP < 0 || v.param (smoothP) >= 0.5f;
        compact_.smoothTime.state.ledOn = full_.smoothTime.state.ledOn = smoothOn;   // SMOOTH is ONE control: the time with the LED of the switch on its label (off: the knob is dim and takes only the switch)
        compact_.smoothTime.setEnabled (rnd_ == 0);
        full_.smoothTime.setEnabled (rnd_ == 0 && ! full_.nativeSmooth());
        for (const Item& it : items_)   // the rate knob's own enabled state is the control's (it is dim in the key follow mode)
            if (it.id == kRate && it.c != nullptr)
                it.c->setEnabled (sm != ui::lfo::SyncMode::Key && it.param >= 0);
        const int modeP = paramOf ("sync_type");
        syncType_ = modeP >= 0 ? static_cast<int> (std::lround (v.param (modeP))) : 0;
        const int directionP = paramOf ("direction");
        const int swingP = paramOf ("swing");
        direction_ = directionP >= 0 ? static_cast<int> (std::lround (v.param (directionP))) : 0;
        swing_ = swingP >= 0 ? static_cast<float> (v.param (swingP) * 0.01) : 0.0f;
        const int shapeP = paramOf (rnd_ != 0 ? "style" : "shape");
        shapeIdx_ = shapeP >= 0 ? static_cast<int> (std::lround (v.param (shapeP))) : 0;
        full_.edit.setEnabled (rnd_ == 0);
        full_.setEditShown (rnd_ == 0 && (page_ || nativePlayEditor_ || shapeIdx_ == ui::lfo::kShapeCustom));   // standalone expands any LFO shape; Play popup expands only Custom
        if (stackDepth_ > 0 && pageTop_ == kPageEditor)
            syncEditor (v);
        // the instance stepper of the compact view and the nameplate
        std::snprintf (name_, sizeof name_, rnd_ != 0 ? "RND %d - %s" : "LFO %d - %s", selection(),
                       rnd_ != 0 ? ui::styleName (shapeIdx_) : ui::lfoglyph::name (shapeIdx_));
    }

    // ======================================================================================================================================================================
    // the display: the shape of one cycle (the engine's own picture), the lead-in, the loop point, the cursors of the voices; the trace of a random LFO
    static float beatsOf (int division) noexcept
    {
        static const float kBeats[13] = { 0.0f, 128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
        return kBeats[std::clamp (division, 0, 12)];
    }
    /** The rate of the LFO in Hz as the engine reads it (0: frozen or not known: the key follow). */
    double rateHz (const IInstanceView& v) const noexcept
    {
        const int sync = static_cast<int> (std::lround (v.param (paramOf ("sync"))));
        if (sync == 0)
            return v.param (paramOf ("rate"));
        if (sync == 4)
            return 0.0;
        const float b = beatsOf (static_cast<int> (std::lround (v.param (paramOf ("division")))));
        if (! (b > 0.0f))
            return 0.0;
        return std::max (0.0, bpm_) / 60.0 / static_cast<double> (b) * (sync == 2 ? 1.0 / 1.5 : (sync == 3 ? 1.5 : 1.0));
    }
    void updateDisplay (IInstanceView& v, const synthyboy::LiveValues* live, double nowMs)
    {
        lastNowMs_ = nowMs;
        ui::lfodisplay::Data d;
        std::snprintf (label_, sizeof label_, "%s", name_);
        d.label = label_;
        d.dim = false;
        bool changed = false;
        const uint64_t generation = v.stateGeneration();
        const bool presetChanged = generation != traceGeneration_;
        if (rnd_ != traceSource_ || presetChanged)
        {
            std::fill (std::begin (trace_), std::end (trace_), 0.0f);
            traceRepeats_ = kTracePoints;
            traceHasSignal_ = false;
            traceSource_ = rnd_;
            traceGeneration_ = generation;
            changed = rnd_ != 0;
        }
        if (rnd_ == 0)
        {
            // ---- the shape: the picture the engine's own code gives (shape, swing, direction, phase, sync type, smoothing), again when a parameter of the LFO, the content or the tempo moved
            bool dirty = ! haveCurve_ || content_ != contentSeen_ || bpm_ != bpmSeen_;
            for (size_t k = 0; k < dispIdx_.size(); ++k)
            {
                const float x = v.param (dispIdx_[k]);
                values_.plain[dispIdx_[k]] = x;
                dirty = dirty || x != seen_[k];
            }
            if (dirty && (nowMs - curveAtMs_ >= 30.0 || ! haveCurve_))
            {
                synthyboy::LfoDisplay::Request req;
                req.lfo = lfoNum_;
                req.bpm = bpm_;
                synthyboy::LfoDisplay::compute (values_, content_.get(), req, curve_, kCurvePoints);
                for (size_t k = 0; k < dispIdx_.size(); ++k)
                    seen_[k] = values_.plain[dispIdx_[k]];
                contentSeen_ = content_;
                bpmSeen_ = bpm_;
                haveCurve_ = true;
                curveAtMs_ = nowMs;
                changed = true;
            }
            d.kind = ui::lfodisplay::Data::Kind::Shape;
            d.y = curve_;
            d.n = kCurvePoints;
            const bool envType = syncType_ >= 2 && syncType_ <= 5;
            d.envelope = envType;
            const float phase = std::clamp (v.param (paramOf ("phase")) / 360.0f, 0.0f, 1.0f);
            d.phase = phase;
            d.loopX = syncType_ >= 3 ? phase : -1.0f;
            const double hz = rateHz (v);
            if (hz > 0.0)
            {
                const double period = 1.0 / hz;
                d.delayShare = static_cast<float> (std::min (4.0, static_cast<double> (v.param (paramOf ("delay"))) * 0.001 / period));
                d.riseShare = static_cast<float> (std::min (4.0, static_cast<double> (v.param (paramOf ("fade_in"))) * 0.001 / period));
            }
            d.gridX = d.gridY = 0;
            // the cursors: one per sounding voice (the mono LFO has one), the first at the value the LFO really gives now
            d.numCursors = 0;
            if (live != nullptr)
            {
                const bool mono = v.param (paramOf ("mono")) >= 0.5f;
                const int n = mono ? std::min (1, live->numLfoVoices) : std::min (live->numLfoVoices, ui::lfodisplay::kMaxCursors);
                for (int i = 0; i < n; ++i)
                {
                    const float x = synthyboy::LfoDisplay::cursorX (live->lfoVoicePhase[lfoNum_ - 1][i]);
                    if (x < 0.0f)
                        continue;
                    const float f = x * static_cast<float> (kCurvePoints - 1);
                    const int k = std::min (static_cast<int> (f), kCurvePoints - 2);
                    d.cx[d.numCursors] = x;
                    d.cy[d.numCursors] = curve_[k] + (curve_[k + 1] - curve_[k]) * (f - static_cast<float> (k));
                    ++d.numCursors;
                }
                const int src = uibind::lfoSource (lfoNum_);
                if (d.numCursors > 0 && src > 0 && src <= synthyboy::LiveValues::kMaxSources)
                    d.cy[0] = std::clamp (live->source[src - 1], -1.0f, 1.0f);
            }
        }
        else
        {
            // ---- a random LFO: the trace of its output over the last moments
            d.kind = ui::lfodisplay::Data::Kind::Random;
            // Silence freezes the last trace. An active midpoint is a real zero sample.
            // A preset change may still have the preceding audio block's live readout.
            if (! presetChanged && live != nullptr && live->numLfoVoices > 0 && nowMs - traceAtMs_ >= kTraceMs)
            {
                traceAtMs_ = nowMs;
                changed = changed || ! traceHasSignal_;
                traceHasSignal_ = true;
                const int src = 14 + rnd_;
                const float y = std::clamp (2.0f * live->source[src - 1] - 1.0f, -1.0f, 1.0f);   // the engine's scale is 0 .. 1 (0.5 + 0.5 value)
                if (y != trace_[kTracePoints - 1])
                    traceRepeats_ = 0;
                if (traceRepeats_ < kTracePoints)
                {
                    std::memmove (trace_, trace_ + 1, sizeof (float) * (kTracePoints - 1));
                    trace_[kTracePoints - 1] = y;
                    ++traceRepeats_;
                    changed = true;
                }
            }
            d.trace = trace_;
            d.traceN = kTracePoints;
            d.traceHasSignal = traceHasSignal_;
            d.style = static_cast<int> (std::lround (v.param (paramOf ("style"))));
            if (d.style != styleSeen_)   // the picture of a style at rest changes with the style
            {
                styleSeen_ = d.style;
                changed = true;
            }
        }
        if (changed)
            displayChangedMs_ = nowMs;
        displayHot_ = nowMs - displayChangedMs_ < 350.0;
        d.traceMarkerCached = ! displayHot_ && ! instPopup_;
        data_ = d;
        data_.label = label_;
    }

    // ======================================================================================================================================================================
    // the shape editor
    /** Makes the shape of the LFO a drawn one (the built-in shape is copied into points) and opens the editor. */
    void openEditor (UiStack& stack, IInstanceView& v)
    {
        if (rnd_ != 0)
            return;
        const int shapeP = paramOf ("shape");
        if (shapeP < 0)
            return;
        ui::lfo::ShapeModel m;
        const int shape = static_cast<int> (std::lround (v.param (shapeP)));
        const synthyboy::ContentLfoShape* have = contentShape (lfoNum_);
        if (shape == ui::lfo::kShapeCustom && have != nullptr)
            m = modelOf (*have);
        else if (shape == ui::lfo::kShapeCustom)
            m = ui::lfo::ShapeModel::fromBuiltin (1);   // a drawn shape that has no points yet starts as a triangle
        else
            m = ui::lfo::ShapeModel::fromBuiltin (shape);
        editor_.model = m;
        ++nativeEditorShape_;
        linkedSnapshot_ = m; linkedSnapshotValid_ = true;
        editor_.undo.clear();
        editor_.canvas.selectOnly (-1);
        editorContent_ = content_;
        editorContentExternal_ = false;
        editorSentValid_ = false;
        editorShapeIdx_ = shape;
        phaseBound_ = false;
        // the grid: the one stored with the shape (the preset / the project), else the default (the grid the points lie on)
        int& gx = gridX_[lfoNum_ - 1];
        int& gy = gridY_[lfoNum_ - 1];
        if (have != nullptr && shape == ui::lfo::kShapeCustom && have->gridX > 0 && have->gridY > 0)
        {
            gx = have->gridX;
            gy = have->gridY;
        }
        else
            gx = gy = ui::lfo::inferGrid (m);
        editor_.canvas.state.gridX = gx;
        editor_.canvas.state.gridY = gy;
        editor_.setActiveGroup (0);
        editor_.setStatus ("");
        setIds();
        if (shape != ui::lfo::kShapeCustom || have == nullptr)
        {
            v.setLfoShape (lfoNum_, contentOf (lfoNum_, m, gx, gy));
            uibind::writePlain (v, shapeP, ui::lfo::kShapeCustom);
        }
        syncEditor (v);
        stack.openDetail (kPageEditor);
    }
    const synthyboy::ContentLfoShape* contentShape (int lfo) const noexcept
    {
        if (content_ == nullptr)
            return nullptr;
        for (const synthyboy::ContentLfoShape& s : content_->lfoShapes)
            if (s.lfo == lfo && s.points.size() >= 2)
                return &s;
        return nullptr;
    }
    void reloadEditorContent()
    {
        ++nativeEditorShape_;
        const synthyboy::ContentLfoShape* shape = shapeIdx_ == ui::lfo::kShapeCustom ? contentShape (lfoNum_) : nullptr;
        const ui::lfo::ShapeModel m = shape != nullptr ? modelOf (*shape) : ui::lfo::ShapeModel::fromBuiltin (shapeIdx_);
        editor_.model = m;
        linkedSnapshot_ = m; linkedSnapshotValid_ = true;
        editor_.undo.clear();
        editor_.canvas.selectOnly (-1);
        int& gx = gridX_[lfoNum_ - 1];
        int& gy = gridY_[lfoNum_ - 1];
        if (shape != nullptr && shape->gridX >= ui::lfo::kMinGrid && shape->gridY >= ui::lfo::kMinGrid)
        {
            gx = shape->gridX;
            gy = shape->gridY;
        }
        else
            gx = gy = ui::lfo::inferGrid (m);
        editor_.canvas.state.gridX = gx;
        editor_.canvas.state.gridY = gy;
        phaseBound_ = false;
        shapeDirty_ = false;
        editorContent_ = content_;
        editorContentExternal_ = false;
        editorSentValid_ = false;
        editorShapeIdx_ = shapeIdx_;
    }
    void syncEditorContent()
    {
        if (content_ == editorContent_ && editorShapeIdx_ == shapeIdx_ && ! editorContentExternal_)
            return;
        const synthyboy::ContentLfoShape* shape = shapeIdx_ == ui::lfo::kShapeCustom ? contentShape (lfoNum_) : nullptr;
        const ui::lfo::ShapeModel incoming = shape != nullptr ? modelOf (*shape) : ui::lfo::ShapeModel::fromBuiltin (shapeIdx_);
        if (editorSentValid_ && incoming == editorSentModel_)
        {
            // A display/content acknowledgement of our own earlier gesture may arrive after
            // the model has moved again; it is not a preset change and must not cancel capture.
            editorContent_ = content_;
            editorContentExternal_ = false;
            editorSentValid_ = false;
            editorShapeIdx_ = shapeIdx_;
            linkedSnapshot_ = incoming; linkedSnapshotValid_ = true;
            return;
        }
        const bool sameModel = incoming == editor_.model;
        const bool sameGrid = shape == nullptr || (shape->gridX == editor_.canvas.state.gridX && shape->gridY == editor_.canvas.state.gridY);
        if (sameModel && sameGrid)
        {
            editorContent_ = content_;
            editorContentExternal_ = false;
            editorShapeIdx_ = shapeIdx_;
            linkedSnapshot_ = incoming; linkedSnapshotValid_ = true;
            return;
        }
        if (gestureDown_)
        {
            // Keep the in-progress picture, but never flush it into the newly arrived preset.
            shapeDirty_ = false;
            editorContent_ = content_;
            editorContentExternal_ = true;
            return;
        }
        reloadEditorContent();
    }
    /** The shape is the drawn one: it must have points (the engine reads a drawn shape without points as a flat line). `from` = the built-in shape the points start as (the shape that was shown when DRAWN was chosen: EDIT is
        hidden for the built-in shapes, so the DRAWN cell is the way to start from one; a triangle when none). */
    void ensureCustom (IInstanceView& v, int from = 1)
    {
        if (rnd_ != 0 || contentShape (lfoNum_) != nullptr)
            return;
        v.setLfoShape (lfoNum_, contentOf (lfoNum_, ui::lfo::ShapeModel::fromBuiltin (from >= 0 && from < ui::lfo::kShapeCustom ? from : 1), gridXOf (lfoNum_), gridYOf (lfoNum_)));
    }
    void setIds()
    {
        editor_.back.setId (kEdBack); editor_.tools.setId (kEdTools); editor_.undoButton.setId (kEdUndo); editor_.redoButton.setId (kEdRedo); editor_.del.setId (kEdDelete);
        editor_.snap.setId (kEdSnap); editor_.loop.setId (kEdLoop); editor_.smooth.setId (kEdSmooth); editor_.cycleLink.setId (kEdCycleLink); editor_.loopLink.setId (kEdLoopLink); editor_.canvas.setId (kEdCanvas);
        editor_.fx.setId (kEdX); editor_.fy.setId (kEdY); editor_.fcurve.setId (kEdCurve); editor_.fgx.setId (kEdGridX); editor_.fgy.setId (kEdGridY); editor_.fpoint.setId (kEdPoint);
        editor_.shapes.setId (kEdShapes); editor_.more.setId (kEdMore); editor_.modButton.setId (kEdMod); editor_.mode.setId (kEdMode); editor_.fphase.setId (kEdPhase); editor_.fbend.setId (kEdBend);
        pointMod_.back.setId (kMpBack); pointMod_.target.setId (kMpTarget); pointMod_.amount.setId (kMpAmount); pointMod_.remove.setId (kMpRemove); pointMod_.board.setId (kMpBoard);
    }
    void applyEditorLinks() noexcept
    {
        auto& m = editor_.model;
        if (m.n < 2)
            return;
        int a = -1, b = -1;
        if (m.linkLoopEnds)
        {
            // Loop modes use the transport PHASE, not the optional authored loop index.
            // The engine first applies swing and direction, so use the same authored abscissa
            // here.  If PHASE falls inside a segment, materialise the seam as a point so either
            // endpoint remains editable and the link never silently chooses a neighbour.
            float phase = std::clamp (editor_.canvas.state.markerX, 0.0f, 1.0f);
            auto shapePhase = [this] (float u) noexcept {
                const float b = 0.5f + 0.25f * std::min (std::max (swing_, 0.0f), 1.0f);
                const float x = u * 4.0f;
                const float k = std::floor (x);
                const float w = x - k;
                const float w2 = w < b ? 0.5f * w / b : 0.5f + 0.5f * (w - b) / (1.0f - b);
                const float swung = (k + w2) * 0.25f;
                if (direction_ == 1) return 1.0f - swung;
                if (direction_ == 2) return 1.0f - std::fabs (2.0f * swung - 1.0f);
                return swung;
            };
            const float seam = shapePhase (phase);
            const float other = shapePhase (syncType_ == 5 ? 0.0f : 1.0f);
            auto pointAt = [&m] (float x) noexcept {
                for (int i = 0; i < m.n; ++i)
                    if (std::fabs (m.p[i].x - x) <= 1.0e-5f)
                        return i;
                return -1;
            };
            int seamPoint = pointAt (seam);
            if (seamPoint < 0 && m.n < ui::lfo::kMaxPoints)
                seamPoint = m.insert (seam, m.eval (seam));
            int otherPoint = pointAt (other);
            if (seamPoint >= 0 && otherPoint >= 0 && seamPoint != otherPoint)
            {
                if (syncType_ == 5) { a = otherPoint; b = seamPoint; }
                else { a = seamPoint; b = otherPoint; }
            }
        }
        // In both loop modes one end of the phase seam is a cycle endpoint. With
        // both links enabled, an edit at any of the three points moves all three.
        if (a < 0 && m.linkCycleEnds) { a = 0; b = m.n - 1; }
        if (a < 0 || b < 0 || a == b)
            return;
        const auto changed = [&] (int i) {
            if (! linkedSnapshotValid_) return false;
            if (m.n == linkedSnapshot_.n) return m.p[i].y != linkedSnapshot_.p[i].y;
            // Materialising a phase point may have moved all later indices.
            for (int j = 0; j < linkedSnapshot_.n; ++j)
                if (m.p[i].x == linkedSnapshot_.p[j].x)
                    return m.p[i].y != linkedSnapshot_.p[j].y;
            return false;
        };
        int source = a;
        if (! changed (a))
        {
            if (changed (b)) source = b;
            else if (m.linkCycleEnds && changed (0)) source = 0;
            else if (m.linkCycleEnds && changed (m.n - 1)) source = m.n - 1;
        }
        const float y = m.p[source].y;
        m.p[a].y = m.p[b].y = y;
        if (m.linkCycleEnds) m.p[0].y = m.p[m.n - 1].y = y;
    }
    void markDirty() noexcept { applyEditorLinks(); linkedSnapshot_ = editor_.model; linkedSnapshotValid_ = true; shapeDirty_ = true; }
    /** Sends the model to the content state (the plugin applies it on its display thread). */
    void flushShape (IInstanceView& v, double nowMs)
    {
        if (! shapeDirty_)
            return;
        shapeDirty_ = false;
        shapeSentMs_ = nowMs;
        editorSentModel_ = editor_.model;
        editorSentValid_ = true;
        v.setLfoShape (lfoNum_, contentOf (lfoNum_, editor_.model, editor_.canvas.state.gridX, editor_.canvas.state.gridY));
    }
    /** The editor page from the model, the canvas state and the trigger of the LFO (the mode and the phase: the marker). */
    void syncEditor (IInstanceView& v)
    {
        const int modeP = paramOf ("sync_type"), phaseP = paramOf ("phase");
        if (modeP >= 0 && phaseP >= 0)
        {
            const synthyboy::ParamInfo& pi = uibind::info (modeP);
            const int st = std::clamp (static_cast<int> (std::lround (v.param (modeP) - pi.min)), 0, std::max (0, pi.numChoices - 1));
            const float keep = editor_.canvas.state.markerX;
            editor_.setTrigger (st, pi.choices != nullptr && st < pi.numChoices ? pi.choices[st] : "", pi.numChoices, static_cast<double> (v.param (phaseP)));
            if (gestureDown_ && gesture_ == &editor_)
                editor_.canvas.state.markerX = keep;   // a finger on the page: the marker is where the finger put it until the parameter says the same
            const ui::lfo::ShapeModel& m = editor_.model;
            if (! gestureDown_)   // the loop point and the PHASE belong together while they say the same (a change of the shape then moves the PHASE)
                phaseBound_ = st >= 3 && m.loop >= 0 && m.loop < m.n && std::fabs (m.p[m.loop].x * 360.0f - v.param (phaseP)) < 0.51f;
        }
        editor_.sync();
    }
    /** The loop modes (3, 4, 5) read the PHASE as the loop position (docs/ui-design/lfo-editor-capabilities.md): after a change that moved the loop point the PHASE follows it when it was bound to it; `force`: it takes the loop point of the model. */
    void alignPhase (IInstanceView& v, bool force)
    {
        const ui::lfo::ShapeModel& m = editor_.model;
        const int phaseP = paramOf ("phase");
        if (phaseP < 0 || syncType_ < 3 || m.loop < 0 || m.loop >= m.n || ! (force || phaseBound_))
            return;
        uibind::writePlain (v, phaseP, std::clamp (static_cast<double> (m.p[m.loop].x) * 360.0, 0.0, 360.0));
    }
    void setStatusText (const std::string& s)
    {
        editor_.setStatus (s.c_str());
        statusUntilMs_ = lastNowMs_ + kStatusMs;
    }
    // the virtual targets of the Q groups of the editor, each as 0 .. 1: EDIT 0 POINT (the selected point of n), 1 X, 2 Y, 3 CURVE (with several points selected: the centre in time, the centre in level, the height); GRID 4 GRID X,
    // 5 GRID Y, 6 SNAP; TRIGGER 7 BEND ALL (MODE and PHASE are parameters of the table)
    static float gridNorm (int g) noexcept { return static_cast<float> (g - ui::lfo::kMinGrid) / static_cast<float> (ui::lfo::kMaxGrid - ui::lfo::kMinGrid); }
    static int gridOfNorm (float x) noexcept { return std::clamp (ui::lfo::kMinGrid + static_cast<int> (std::lround (x * static_cast<float> (ui::lfo::kMaxGrid - ui::lfo::kMinGrid))), ui::lfo::kMinGrid, ui::lfo::kMaxGrid); }
    float virtualValue (int id) const noexcept
    {
        const ui::lfo::ShapeModel& m = editor_.model;
        const int sel = editor_.canvas.state.selected;
        const bool ok = sel >= 0 && sel < m.n;
        const ui::lfo::PointSet set = editor_.canvas.selection();
        const bool multi = set.count() >= 2;
        float x0 = 0.0f, x1 = 0.0f, y0 = 0.0f, y1 = 0.0f;
        if (multi)
            m.spanOf (set, x0, x1, y0, y1);
        switch (id)
        {
            case 0: return ok && m.n > 1 ? static_cast<float> (sel) / static_cast<float> (m.n - 1) : 0.0f;
            case 1: return multi ? 0.5f * (x0 + x1) : (ok ? m.p[sel].x : 0.0f);
            case 2: return multi ? 0.5f * (y0 + y1) : (ok ? m.p[sel].y : 0.0f);
            case 3: return multi ? y1 - y0 : (ok && sel + 1 < m.n ? (m.p[sel].power + ui::lfo::kMaxPower) / (2.0f * ui::lfo::kMaxPower) : 0.5f);
            case 4: return gridNorm (editor_.canvas.state.gridX);
            case 5: return gridNorm (editor_.canvas.state.gridY);
            case 6: return editor_.canvas.state.snap ? 1.0f : 0.0f;
            default: return std::clamp ((m.meanCurve() + ui::lfo::kMaxPower) / (2.0f * ui::lfo::kMaxPower), 0.0f, 1.0f);
        }
    }
    void applyVirtual (int id, float x, IInstanceView& v)
    {
        ui::lfo::ShapeModel& m = editor_.model;
        ui::ShapeCanvas::State& cs = editor_.canvas.state;
        if (id == 4 || id == 5)
        {
            const int g = gridOfNorm (x);
            (id == 4 ? cs.gridX : cs.gridY) = g;
            (id == 4 ? gridX_ : gridY_)[lfoNum_ - 1] = g;
            markDirty();   // the grid is stored with the shape (state version 5)
            return;
        }
        if (id == 6)
        {
            cs.snap = x >= 0.5f;
            return;
        }
        if (id == 0)
        {
            editor_.canvas.selectOnly (m.n > 0 ? std::clamp (static_cast<int> (std::lround (x * static_cast<float> (std::max (1, m.n - 1)))), 0, m.n - 1) : -1);
            return;
        }
        const ui::lfo::ShapeModel before = m;
        if (id == 7)
        {
            if (m.bendAll (x * 2.0f * ui::lfo::kMaxPower - ui::lfo::kMaxPower - m.meanCurve()))
            {
                pushVirtualUndo (before);
                markDirty();
            }
            return;
        }
        const ui::lfo::PointSet set = editor_.canvas.selection();
        if (set.count() >= 2)   // several points: the fields move and scale the selection
        {
            float x0, x1, y0, y1;
            m.spanOf (set, x0, x1, y0, y1);
            float dx = id == 1 ? x - 0.5f * (x0 + x1) : 0.0f, dy = id == 2 ? x - 0.5f * (y0 + y1) : 0.0f;
            const bool changed = id == 3 ? m.scaleSetY (set, x) : m.moveSet (set, dx, dy);
            if (changed)
            {
                pushVirtualUndo (before);
                markDirty();
                alignPhase (v, false);
            }
            return;
        }
        const int sel = cs.selected;
        if (! (sel >= 0 && sel < m.n))
            return;
        if (id == 1) m.move (sel, x, m.p[sel].y);
        else if (id == 2) m.move (sel, m.p[sel].x, x);
        else if (sel + 1 < m.n) m.setPower (sel, std::round (x * 2.0f * ui::lfo::kMaxPower - ui::lfo::kMaxPower));
        if (! (m == before))   // a turn that changes nothing (CURVE on the last point, a value already there) leaves no undo state and writes nothing
        {
            pushVirtualUndo (before);
            markDirty();
            alignPhase (v, false);
        }
    }
    void pushVirtualUndo (const ui::lfo::ShapeModel& before) noexcept
    {
        if (gestureDown_)
        {
            if (! virtualUndoActive_)
            {
                editor_.undo.push (before);
                virtualUndoActive_ = true;
            }
        }
        else
            editor_.undo.push (before);
    }
    template <class Self>
    static auto editorFields (Self& self) noexcept
        -> std::array<std::conditional_t<std::is_const_v<Self>, const ui::Component*, ui::Component*>, nativeui::lfoeditor::kCount>
    {
        return {
            &self.editor_.back, &self.editor_.tools, &self.editor_.shapes, &self.editor_.more,
            &self.editor_.undoButton, &self.editor_.redoButton, &self.editor_.del, &self.editor_.modButton,
            &self.editor_.snap, &self.editor_.loop, &self.editor_.smooth, &self.editor_.cycleLink, &self.editor_.loopLink,
            &self.editor_.fpoint, &self.editor_.fx, &self.editor_.fy, &self.editor_.fcurve, &self.editor_.fgx, &self.editor_.fgy,
            &self.editor_.fphase, &self.editor_.fbend, &self.editor_.mode
        };
    }
    bool nativeEditorHit (int x, int y) const noexcept
    {
        if (pageTop_ != kPageEditor) return false;
        const auto fields = editorFields (*this);
        for (int field = 0; field < nativeui::lfoeditor::kCount; ++field)
            if (fields[field]->enabled() && nativeEditorRect (field).contains (x, y))
                return true;
        return false;
    }

    nativeui::lfoeditor::Context nativeEditorContext (const IInstanceView& v) const
    {
        return { lfoNum_, v.stateGeneration(), nativeEditorShape_, editor_.canvas.selection(),
                 editor_.canvas.state.selected, editor_.model.n };
    }

    nativeui::lfoeditor::State nativeEditorView (IInstanceView& v, const UiStack& stack) const
    {
        namespace n = nativeui::lfoeditor;
        if ((! page_ && ! nativePlayEditor_) || ! v.nativeLfoShapeEditor() || rnd_ != 0 || editorContentExternal_
            || stack.depth() <= 1 || stack.element() != kElementLfo || stack.top() != kPageEditor
            || &ui::theme::L() != &ui::theme::kLook1)
            return {};
        n::State state;
        state.context = nativeEditorContext (v);
        const auto fields = editorFields (*this);
        const int selected = state.context.selection.count();
        for (int field = 0; field < n::kCount; ++field)
        {
            const auto* control = fields[field];
            if (control->enabled())
                state.enabled |= 1u << field;
            const auto range = n::rangeOf (field, state.context.points, selected > 1);
            switch (n::kFields[field].type)
            {
                case n::kAction: break;
                case n::kToolChoice: state.values[field] = range.encode (editor_.tools.state.index); break;
                case n::kToggle: state.values[field] = static_cast<const ui::LedToggle*> (control)->state.on ? 1.0f : 0.0f; break;
                case n::kNumber:
                    if (field != n::kPoint || selected == 1)
                        state.values[field] = range.encode (static_cast<const ui::NumberField*> (control)->state.value);
                    break;
                case n::kChoice: state.values[field] = range.encode (editor_.mode.state.index); break;
            }
        }
        return state;
    }

    void publishNativeEditor (IInstanceView& v, const nativeui::lfoeditor::State& state)
    {
        if (state.context.owner == 0 && nativeEditorState_.context.owner == 0)
            return;
        nativeEditorState_ = state;
        v.setNativeLfoEditor (state);
    }

    void updateNativeEditor (IInstanceView& v, UiStack& stack, double nowMs)
    {
        namespace n = nativeui::lfoeditor;
        auto state = nativeEditorView (v, stack);
        if (state.context.owner != 0)
        {
            const auto fields = editorFields (*this);
            for (int field = 0; field < n::kCount && state.context.owner != 0; ++field)
            {
                float value;
                if (! v.takeNativeLfoEditor (state, field, value))
                    continue;
                const auto type = n::kFields[field].type;
                const auto range = n::rangeOf (field, state.context.points, state.scale());
                const double next = range.decode (value);
                const double current = range.decode (state.values[field]);
                const int id = fields[field]->id();
                ui::Change change;
                if (type == n::kAction && value >= 0.5f)
                    change = ui::Change::request (id);
                else if (type != n::kAction && next != current && (field != n::kPoint || next > 0.0))
                    change = type == n::kNumber ? ui::Change::normalised (id, next)
                                                : ui::Change::indexed (id, static_cast<int> (next), next);
                if (change.kind != ui::ChangeKind::None)
                {
                    editorChange (change, stack, v, nowMs);
                    state = nativeEditorView (v, stack);
                }
            }
            if (shapeDirty_ && ! editorContentExternal_ && ! gestureDown_)
                flushShape (v, nowMs);
        }
        publishNativeEditor (v, state);
    }

    void updateEditor (IInstanceView& v, double nowMs)
    {
        syncEditorContent();
        for (int id = 0; id < kVirtEditorCount; ++id)
        {
            float x;
            if (v.takeQVirtual (id, x))
                applyVirtual (id, x, v);
        }
        if (statusUntilMs_ > 0.0 && nowMs >= statusUntilMs_)
        {
            editor_.setStatus ("");
            statusUntilMs_ = 0.0;
        }
        syncEditor (v);
        for (int id = 0; id < kVirtEditorCount; ++id)
            v.setQVirtual (id, virtualValue (id));
        if (shapeDirty_ && (nowMs - shapeSentMs_ >= kShapeSendMs || ! gestureDown_))
            flushShape (v, nowMs);
    }
    void editorChange (const ui::Change& c, UiStack& stack, IInstanceView& v, double nowMs)
    {
        using K = ui::ChangeKind;
        ui::lfo::ShapeModel& m = editor_.model;
        ui::ShapeCanvas& cv = editor_.canvas;
        ui::ShapeCanvas::State& cs = cv.state;
        const ui::lfo::PointSet set = cv.selection();
        const int cnt = set.count();
        const bool multi = cnt >= 2;
        const int sel = cs.selected >= 0 && cs.selected < m.n ? cs.selected : (cnt == 1 ? set.first() : -1);   // the point the fields show
        bool follow = false;   // the model changed in a way that can move the loop point: the PHASE follows it
        const bool value = c.kind == K::Index || c.kind == K::Value;
        switch (c.id)
        {
            case kEdBack: if (! editorContentExternal_) flushShape (v, nowMs); stack.back(); return;
            case kEdTools: if (c.kind == K::Index) cs.tool = std::clamp (c.index, 0, ui::ToolSelector::kTools - 1); break;
            case kEdUndo:
                if (c.kind == K::Request)
                {
                    ui::lfo::ShapeModel back;
                    if (editor_.undo.pop (back))
                    {
                        editor_.undo.rememberRedo (m);
                        m = back;
                        cs.marks.clear();
                        if (cs.selected >= m.n) cs.selected = -1;
                        cs.selectedSeg = -1;
                        markDirty();
                        follow = true;
                    }
                }
                break;
            case kEdRedo:
                if (c.kind == K::Request)
                {
                    ui::lfo::ShapeModel next;
                    if (editor_.undo.popRedo (next))
                    {
                        editor_.undo.pushUndo (m);
                        m = next;
                        cs.marks.clear();
                        if (cs.selected >= m.n) cs.selected = -1;
                        cs.selectedSeg = -1;
                        markDirty();
                        follow = true;
                    }
                }
                break;
            case kEdDelete:
                if (c.kind == K::Request)
                {
                    int deletable = 0;
                    for (int i = 1; i + 1 < m.n; ++i)
                        deletable += set.has (i) ? 1 : 0;
                    if (deletable > 0)
                    {
                        editor_.undo.push (m);
                        m.removeSet (set);
                        cv.selectOnly (-1);
                        markDirty();
                        follow = true;
                    }
                }
                break;
            case kEdSnap: if (c.kind == K::Index) cs.snap = c.index != 0; break;
            case kEdLoop:
                if (c.kind == K::Index && cs.selected >= 0 && cs.selected < m.n)
                {
                    editor_.undo.push (m);
                    m.loop = c.index != 0 ? cs.selected : -1;
                    markDirty();
                    if (c.index != 0)
                        alignPhase (v, true);
                }
                break;
            case kEdSmooth:
                if (c.kind == K::Index)
                {
                    editor_.undo.push (m);
                    m.smooth = c.index != 0;
                    markDirty();
                }
                break;
            case kEdCycleLink:
                if (c.kind == K::Index)
                {
                    editor_.undo.push (m);
                    m.linkCycleEnds = c.index != 0;
                    markDirty();
                }
                break;
            case kEdLoopLink:
                if (c.kind == K::Index)
                {
                    editor_.undo.push (m);
                    m.linkLoopEnds = c.index != 0;
                    markDirty();
                }
                break;
            case kEdCanvas:
                if (c.kind == K::Index)
                {
                    if (c.index == ui::ShapeCanvas::Edited)
                    {
                        markDirty();
                        follow = true;
                    }
                    else if (c.index == ui::ShapeCanvas::Marker)   // the marker is the PHASE; in the loop modes the model's loop point went with it
                    {
                        const int phaseP = paramOf ("phase");
                        if (phaseP >= 0)
                            uibind::writePlain (v, phaseP, std::clamp (c.value, 0.0, 1.0) * 360.0);
                        if (cs.markerKind == ui::ShapeCanvas::MarkerLoop)
                        {
                            markDirty();
                            phaseBound_ = m.loop >= 0;
                        }
                    }
                }
                break;
            case kEdX: case kEdY:
                if (value && multi)
                {
                    float x0, x1, y0, y1, dx = 0.0f, dy = 0.0f;
                    m.spanOf (set, x0, x1, y0, y1);
                    if (c.id == kEdX) dx = static_cast<float> (c.value) - 0.5f * (x0 + x1); else dy = static_cast<float> (c.value) - 0.5f * (y0 + y1);
                    const ui::lfo::ShapeModel before = m;
                    if (m.moveSet (set, dx, dy))
                    {
                        pushVirtualUndo (before);
                        markDirty();
                        follow = true;
                    }
                }
                else if (value && sel >= 0)
                {
                    pushVirtualUndo (m);
                    if (c.id == kEdX) m.move (sel, static_cast<float> (c.value), m.p[sel].y); else m.move (sel, m.p[sel].x, static_cast<float> (c.value));
                    markDirty();
                    follow = true;
                }
                break;
            case kEdCurve:
                if (value && multi)   // the field is SCALE: the height of the selection
                {
                    const ui::lfo::ShapeModel before = m;
                    if (m.scaleSetY (set, static_cast<float> (c.value)))
                    {
                        pushVirtualUndo (before);
                        markDirty();
                    }
                }
                else if (value && sel >= 0 && sel + 1 < m.n)
                {
                    pushVirtualUndo (m);
                    m.setPower (sel, static_cast<float> (c.value));
                    markDirty();
                }
                break;
            case kEdBend:
                if (value)
                {
                    const ui::lfo::ShapeModel before = m;
                    if (m.bendAll (static_cast<float> (c.value) - m.meanCurve()))
                    {
                        pushVirtualUndo (before);
                        markDirty();
                    }
                }
                break;
            case kEdGridX: case kEdGridY:
                if (value)
                {
                    const int g = std::clamp (static_cast<int> (std::lround (c.value)), ui::lfo::kMinGrid, ui::lfo::kMaxGrid);
                    (c.id == kEdGridX ? cs.gridX : cs.gridY) = g;
                    (c.id == kEdGridX ? gridX_ : gridY_)[lfoNum_ - 1] = g;
                    markDirty();   // the grid is stored with the shape (state version 5)
                }
                break;
            case kEdPoint:
                if (value && m.n > 0)
                    cv.selectOnly (std::clamp (static_cast<int> (std::lround (c.value)) - 1, 0, m.n - 1));
                break;
            case kEdMode:
                if (c.kind == K::Request)
                    openModeList (stack, v);
                else if (c.kind == K::Index && paramOf ("sync_type") >= 0)
                    uibind::writePlain (v, paramOf ("sync_type"), c.index);
                break;
            case kEdPhase:
                if (value && paramOf ("phase") >= 0)
                {
                    uibind::writePlain (v, paramOf ("phase"), c.value);
                    phaseBound_ = false;
                }
                break;
            case kEdShapes: if (c.kind == K::Request) openEditorList (stack, v, Ed::ShapesRoot); break;
            case kEdMore: if (c.kind == K::Request) openEditorList (stack, v, Ed::ActionsRoot); break;
            case kEdMod: if (c.kind == K::Request && cnt > 0) stack.openDetail (kPagePointMod); break;
            default: break;
        }
        if (follow)
            alignPhase (v, false);
        syncEditor (v);
    }

    // ---- the lists of the editor: SHAPES (the shapes, saving), MORE (the actions); the lists are the one ListPopup of the page
    enum class Ed : uint8_t { None, ShapesRoot, ShapesFactory, ShapesMine, Confirm, ActionsRoot, ActionsGroup, CopyLfo };
    enum class Verb : uint8_t { Load, Overwrite, Rename, Delete };
    enum class Kb : uint8_t { SaveAs, Rename };
    enum class Pending : uint8_t { SaveOver, OverwriteItem, DeleteItem };
    enum class Act : uint8_t { SelectAll, FlipH, FlipV, ShiftL, ShiftR, ResetCurves, ClearLoop, Grid4, Grid8, Grid12, Grid16, Grid32, GridHalve, GridDouble, GridFollow, CopyToLfo, FromOscA, FromOscB, FromOscC, ToOscA, ToOscB, ToOscC };
    struct ActionRow { int group; const char* text; Act act; };
    static constexpr int kActionGroups = 3;
    static const char* actionGroupName (int g) noexcept { static const char* const n[kActionGroups] = { "EDIT", "GRID", "COPY" }; return n[std::clamp (g, 0, kActionGroups - 1)]; }
    /** The rows of the MORE list: a table (a new action is a row). */
    static const std::vector<ActionRow>& actionRows()
    {
        static const std::vector<ActionRow> rows = {
            { 0, "SELECT ALL", Act::SelectAll }, { 0, "FLIP HORIZONTAL", Act::FlipH }, { 0, "FLIP VERTICAL", Act::FlipV }, { 0, "SHIFT LEFT", Act::ShiftL }, { 0, "SHIFT RIGHT", Act::ShiftR },
            { 0, "RESET ALL CURVES", Act::ResetCurves }, { 0, "CLEAR LOOP POINT", Act::ClearLoop },
            { 1, "GRID 4 x 4", Act::Grid4 }, { 1, "GRID 8 x 8", Act::Grid8 }, { 1, "GRID 12 x 12", Act::Grid12 }, { 1, "GRID 16 x 16", Act::Grid16 }, { 1, "GRID 32 x 32", Act::Grid32 },
            { 1, "HALVE THE GRID", Act::GridHalve }, { 1, "DOUBLE THE GRID", Act::GridDouble }, { 1, "GRID FOLLOWS THE POINTS", Act::GridFollow },
            { 2, "COPY TO LFO ...", Act::CopyToLfo }, { 2, "OSC A CYCLE TO THIS LFO", Act::FromOscA }, { 2, "OSC B CYCLE TO THIS LFO", Act::FromOscB }, { 2, "OSC C CYCLE TO THIS LFO", Act::FromOscC },
            { 2, "THIS SHAPE TO OSC A", Act::ToOscA }, { 2, "THIS SHAPE TO OSC B", Act::ToOscB }, { 2, "THIS SHAPE TO OSC C", Act::ToOscC } };
        return rows;
    }
    static std::vector<std::string> userRootsOf (const IInstanceView& v)
    {
        synthyboy::CatalogueSources src;
        v.wavetableSources (src);
        return src.userRoots;
    }
    void openModeList (UiStack& stack, IInstanceView& v)
    {
        const int p = paramOf ("sync_type");
        if (p < 0)
            return;
        listKind_ = ListKind::Param;
        listParam_ = p;
        stack.openDetail (kPageList);
        fillList (&v);
    }
    void openEditorList (UiStack& stack, IInstanceView& v, Ed which)
    {
        listKind_ = ListKind::Editor;
        ed_ = which;
        edGroup_ = -1;
        verb_ = Verb::Load;
        if (which == Ed::ShapesRoot)
        {
            lib_.setRoots (userRootsOf (v));
            lib_.refresh();
        }
        stack.openDetail (kPageList);
        fillEditorList();
    }
    const char* verbHeader() const noexcept
    {
        switch (verb_)
        {
            case Verb::Overwrite: return "REPLACE WHICH SHAPE?";
            case Verb::Rename: return "RENAME WHICH SHAPE?";
            case Verb::Delete: return "DELETE WHICH SHAPE?";
            default: return "MY SHAPES";
        }
    }
    void fillEditorList()
    {
        ++nativeMenuRevision_;
        edRows_.clear();
        edMap_.clear();
        const char* header = "";
        bool back = false;
        switch (ed_)
        {
            case Ed::ShapesRoot:
                header = "SHAPES";
                for (const char* r : { "FACTORY SHAPES", "MY SHAPES", "SAVE AS ...", "REPLACE A SAVED SHAPE ...", "RENAME A SAVED SHAPE ...", "DELETE A SAVED SHAPE ..." })
                    edRows_.push_back (r);
                break;
            case Ed::ShapesFactory:
                header = "FACTORY SHAPES";
                back = true;
                for (int i = 0; i < ui::lfo::kNumBuiltin; ++i)
                    edRows_.push_back (ui::lfoglyph::name (i));
                break;
            case Ed::ShapesMine:
                header = verbHeader();
                back = true;
                for (const LfoShapeLibrary::Item& it : lib_.items())
                    edRows_.push_back (it.name);
                if (edRows_.empty())
                    edRows_.push_back (lib_.roots().empty() ? "NO CARD OR DRIVE FOUND" : "NO SAVED SHAPES");
                break;
            case Ed::Confirm:
                header = edHeader_.c_str();
                edRows_.push_back (edYes_);
                edRows_.push_back ("CANCEL");
                break;
            case Ed::ActionsRoot:
                header = "MORE";
                for (int g = 0; g < kActionGroups; ++g)
                    edRows_.push_back (actionGroupName (g));
                break;
            case Ed::ActionsGroup:
                header = actionGroupName (edGroup_);
                back = true;
                for (size_t i = 0; i < actionRows().size(); ++i)
                    if (actionRows()[i].group == edGroup_)
                    {
                        edRows_.push_back (actionRows()[i].text);
                        edMap_.push_back (static_cast<int> (i));
                    }
                break;
            case Ed::CopyLfo:
                header = "COPY THIS SHAPE TO";
                back = true;
                for (int n = 1; n <= kLfos; ++n)
                    if (n != lfoNum_)
                    {
                        edRows_.push_back ("LFO " + std::to_string (n));
                        edMap_.push_back (n);
                    }
                break;
            default: break;
        }
        listNames_.clear();
        for (const std::string& r : edRows_)
            listNames_.push_back (r.c_str());
        list_.state.header = header;
        list_.state.headerBack = back;
        list_.state.current = -1;
        list_.state.items = listNames_.data();
        list_.state.count = static_cast<int> (listNames_.size());
        list_.showCurrent();
    }
    /** The header of an editor list was tapped: one level up (the shapes and the actions start at their root). */
    void editorListUp()
    {
        if (ed_ == Ed::ShapesFactory || ed_ == Ed::ShapesMine)
            ed_ = Ed::ShapesRoot;
        else if (ed_ == Ed::ActionsGroup)
            ed_ = Ed::ActionsRoot;
        else if (ed_ == Ed::CopyLfo)
        {
            ed_ = Ed::ActionsGroup;
            edGroup_ = 2;
        }
        verb_ = Verb::Load;
        fillEditorList();
    }
    void askConfirm (UiStack& stack, const std::string& header, const char* yes, Pending what)
    {
        listKind_ = ListKind::Editor;
        ed_ = Ed::Confirm;
        edHeader_ = header;
        edYes_ = yes;
        pending_ = what;
        if (stack.top() != kPageList)
            stack.openDetail (kPageList);
        fillEditorList();
    }
    void openKeyboard (UiStack& stack, Kb purpose, const std::string& initial)
    {
        kbPurpose_ = purpose;
        kbInitial_ = initial;
        stack.openDetail (kPageKeyboard);
    }
    void beginKeyboard()
    {
        entry_.begin (kbPurpose_ == Kb::SaveAs ? "NAME OF THE SHAPE" : "NEW NAME OF THE SHAPE", kbInitial_.c_str(), kMaxShapeName, kKbOk, kKbCancel);
    }
    void doSave (const std::string& name, bool overwrite)
    {
        if (lib_.save (name, editor_.model, overwrite))
            setStatusText ("SAVED " + name);
        else
            setStatusText (lib_.error());
    }
    void keyboardOk (UiStack& stack)
    {
        const std::string name = entry_.text();
        stack.back();
        if (kbPurpose_ == Kb::Rename)
        {
            if (lib_.rename (pendingItem_, name))
                setStatusText ("RENAMED TO " + name);
            else
                setStatusText (lib_.error());
            return;
        }
        pendingName_ = name;
        if (lib_.find (name) >= 0)
            askConfirm (stack, "REPLACE " + name + "?", "REPLACE", Pending::SaveOver);
        else
            doSave (name, false);
    }
    /** A shape loaded or copied into the editor: one undo state, the grid the points lie on, the PHASE of the loop modes follows the loop point. */
    void adoptShape (const ui::lfo::ShapeModel& m, const std::string& shown, IInstanceView& v)
    {
        editor_.undo.push (editor_.model);
        editor_.model = m;
        editor_.canvas.selectOnly (-1);
        const int g = ui::lfo::inferGrid (m);
        editor_.canvas.state.gridX = editor_.canvas.state.gridY = g;
        gridX_[lfoNum_ - 1] = gridY_[lfoNum_ - 1] = g;
        markDirty();
        alignPhase (v, true);
        setStatusText ("LOADED " + shown);
        syncEditor (v);
    }
    void runPending (IInstanceView& v)
    {
        (void) v;
        const int n = lib_.count();
        switch (pending_)
        {
            case Pending::SaveOver: doSave (pendingName_, true); break;
            case Pending::OverwriteItem: if (pendingItem_ >= 0 && pendingItem_ < n) doSave (lib_.items()[static_cast<size_t> (pendingItem_)].name, true); break;
            case Pending::DeleteItem:
                if (pendingItem_ >= 0 && pendingItem_ < n)
                {
                    const std::string name = lib_.items()[static_cast<size_t> (pendingItem_)].name;
                    setStatusText (lib_.remove (pendingItem_) ? "DELETED " + name : lib_.error());
                }
                break;
        }
    }
    void editorListChosen (int index, UiStack& stack, IInstanceView& v)
    {
        switch (ed_)
        {
            case Ed::ShapesRoot:
                if (index == 0) { ed_ = Ed::ShapesFactory; fillEditorList(); }
                else if (index == 2)   // SAVE AS: the keyboard (the list closes first: the keyboard replaces it)
                {
                    if (! lib_.canSave())
                        setStatusText ("NO CARD OR DRIVE FOUND");
                    stack.back();
                    if (lib_.canSave())
                        openKeyboard (stack, Kb::SaveAs, "");
                }
                else if (index >= 1 && index <= 5)
                {
                    verb_ = index == 1 ? Verb::Load : (index == 3 ? Verb::Overwrite : (index == 4 ? Verb::Rename : Verb::Delete));
                    ed_ = Ed::ShapesMine;
                    fillEditorList();
                }
                break;
            case Ed::ShapesFactory:
                if (index >= 0 && index < ui::lfo::kNumBuiltin)
                    adoptShape (ui::lfo::ShapeModel::fromBuiltin (index), ui::lfoglyph::name (index), v);
                stack.back();
                break;
            case Ed::ShapesMine:
                if (index < 0 || index >= lib_.count())
                {
                    ed_ = Ed::ShapesRoot;   // the row that says there is nothing: back to the start
                    fillEditorList();
                    break;
                }
                pendingItem_ = index;
                if (verb_ == Verb::Load)
                {
                    ui::lfo::ShapeModel m;
                    if (lib_.load (index, m))
                        adoptShape (m, lib_.items()[static_cast<size_t> (index)].name, v);
                    else
                        setStatusText (lib_.error());
                    stack.back();
                }
                else if (verb_ == Verb::Rename)
                {
                    stack.back();
                    openKeyboard (stack, Kb::Rename, lib_.items()[static_cast<size_t> (index)].name);
                }
                else if (verb_ == Verb::Overwrite)
                    askConfirm (stack, "REPLACE " + lib_.items()[static_cast<size_t> (index)].name + "?", "REPLACE", Pending::OverwriteItem);
                else
                    askConfirm (stack, "DELETE " + lib_.items()[static_cast<size_t> (index)].name + "?", "DELETE", Pending::DeleteItem);
                break;
            case Ed::Confirm:
                if (index == 0)
                    runPending (v);
                stack.back();
                break;
            case Ed::ActionsRoot:
                edGroup_ = std::clamp (index, 0, kActionGroups - 1);
                ed_ = Ed::ActionsGroup;
                fillEditorList();
                break;
            case Ed::ActionsGroup:
                if (index >= 0 && index < static_cast<int> (edMap_.size()))
                {
                    const Act a = actionRows()[static_cast<size_t> (edMap_[static_cast<size_t> (index)])].act;
                    if (a == Act::CopyToLfo)
                    {
                        ed_ = Ed::CopyLfo;
                        fillEditorList();
                    }
                    else
                    {
                        stack.back();
                        runAction (a, v);
                    }
                }
                break;
            case Ed::CopyLfo:
                if (index >= 0 && index < static_cast<int> (edMap_.size()))
                    copyToLfo (edMap_[static_cast<size_t> (index)], v);
                stack.back();
                break;
            default: stack.back(); break;
        }
    }
    void setGridBoth (int gx, int gy)
    {
        ui::ShapeCanvas::State& cs = editor_.canvas.state;
        cs.gridX = std::clamp (gx, ui::lfo::kMinGrid, ui::lfo::kMaxGrid);
        cs.gridY = std::clamp (gy, ui::lfo::kMinGrid, ui::lfo::kMaxGrid);
        gridX_[lfoNum_ - 1] = cs.gridX;
        gridY_[lfoNum_ - 1] = cs.gridY;
        markDirty();   // the grid is stored with the shape
    }
    /** One action of the MORE list. Every change of the shape is ONE undo state (the state before it); the selection is cleared when the points move to other indices. */
    void runAction (Act a, IInstanceView& v)
    {
        ui::lfo::ShapeModel& m = editor_.model;
        ui::ShapeCanvas& cv = editor_.canvas;
        const ui::lfo::ShapeModel before = m;
        bool changed = false;
        switch (a)
        {
            case Act::SelectAll: cv.selectAll(); break;
            case Act::FlipH: m.flipH(); cv.selectOnly (-1); changed = true; break;
            case Act::FlipV: m.flipV(); changed = true; break;
            case Act::ShiftL: case Act::ShiftR:
            {
                const float d = 1.0f / static_cast<float> (std::clamp (cv.state.gridX, ui::lfo::kMinGrid, ui::lfo::kMaxGrid));
                if (m.shift (a == Act::ShiftR ? d : 1.0f - d))
                {
                    cv.selectOnly (-1);
                    changed = true;
                }
                else
                    setStatusText ("CANNOT SHIFT: THE SHAPE HAS TOO MANY POINTS");
                break;
            }
            case Act::ResetCurves: changed = m.resetCurves(); break;
            case Act::ClearLoop: if (m.loop >= 0) { m.loop = -1; changed = true; } break;
            case Act::Grid4: setGridBoth (4, 4); break;
            case Act::Grid8: setGridBoth (8, 8); break;
            case Act::Grid12: setGridBoth (12, 12); break;
            case Act::Grid16: setGridBoth (16, 16); break;
            case Act::Grid32: setGridBoth (32, 32); break;
            case Act::GridHalve: setGridBoth (cv.state.gridX / 2, cv.state.gridY / 2); break;
            case Act::GridDouble: setGridBoth (cv.state.gridX * 2, cv.state.gridY * 2); break;
            case Act::GridFollow: { const int g = ui::lfo::inferGrid (m); setGridBoth (g, g); break; }
            case Act::FromOscA: case Act::FromOscB: case Act::FromOscC: shapeFromOsc (static_cast<int> (a) - static_cast<int> (Act::FromOscA) + 1, v); return;
            case Act::ToOscA: case Act::ToOscB: case Act::ToOscC: shapeToOsc (static_cast<int> (a) - static_cast<int> (Act::ToOscA) + 1, v); return;
            case Act::CopyToLfo: return;
        }
        if (changed)
        {
                        pushVirtualUndo (before);
            markDirty();
            alignPhase (v, false);
        }
        syncEditor (v);
    }
    /** The shape of this LFO to LFO `n`: the points, SMOOTH, the loop point, the modulation and the grid; the LFO takes the drawn shape (lfoN.shape = Custom) so that the copy is what plays. */
    void copyToLfo (int n, IInstanceView& v)
    {
        if (n < 1 || n > kLfos || n == lfoNum_)
            return;
        const int gx = editor_.canvas.state.gridX, gy = editor_.canvas.state.gridY;
        v.setLfoShape (n, contentOf (n, editor_.model, gx, gy));
        const int p = uibind::indexOfKey ("lfo" + std::to_string (n) + ".shape");
        if (p >= 0)
            uibind::writePlain (v, p, ui::lfo::kShapeCustom);
        gridX_[n - 1] = gx;
        gridY_[n - 1] = gy;
        setStatusText ("COPIED TO LFO " + std::to_string (n));
    }
    /** One cycle of oscillator `osc` (1 .. 3) as the shape of this LFO (at most 128 points, the error below 0.2 percent of full scale: convert::frameToLfoShape). */
    void shapeFromOsc (int osc, IInstanceView& v)
    {
        synthyboy::ParamValues values = synthyboy::ParamValues::defaults();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            values.plain[i] = v.param (static_cast<int> (i));
        synthyboy::OscCycle cycle;   // allocates (a real oscillator): once per action, on the overlay thread
        const std::shared_ptr<const synthyboy::StateContent> content = v.content();
        cycle.setContent (content != nullptr ? *content : synthyboy::StateContent {}, v.contentStore());
        synthyboy::OscCycle::Request req;
        req.osc = osc;
        req.bpm = v.bpm();
        float cyc[synthyboy::OscCycle::kSamples];
        if (cycle.compute (values, req, cyc) != synthyboy::OscCycle::Kind::wavetable)
        {
            setStatusText ("THAT OSCILLATOR HAS NO WAVETABLE CYCLE");
            return;
        }
        synthyboy::wtedit::Frame frame;
        const int cn = synthyboy::OscCycle::kSamples;
        for (size_t i = 0; i < frame.data.size(); ++i)   // 512 values to the 2048 of a frame (linear, the cycle wraps)
        {
            const float pos = static_cast<float> (i) * static_cast<float> (cn) / static_cast<float> (frame.data.size());
            const int k = std::min (static_cast<int> (pos), cn - 1);
            frame[i] = cyc[k] + (cyc[(k + 1) % cn] - cyc[k]) * (pos - static_cast<float> (k));
        }
        synthyboy::ContentLfoShape cs;
        if (! synthyboy::convert::frameToLfoShape (frame, lfoNum_, cs))
        {
            setStatusText ("CANNOT MAKE A SHAPE OF THAT CYCLE");
            return;
        }
        adoptShape (modelOf (cs), std::string ("OSC ") + static_cast<char> ('A' + osc - 1), v);
    }
    /** This shape as a wavetable of oscillator `osc`: one frame of 2048 samples, a WAV file in SynthyBoy/Wavetables/ (the player's own tables: the browser lists it in the category User) that the plugin loads like a file chosen in
        the browser. A new file every time (the name is not used twice: a preset keeps the hash of the file it was made with). */
    void shapeToOsc (int osc, IInstanceView& v)
    {
        lib_.setRoots (userRootsOf (v));
        const std::string dir = lib_.folderFor ("SynthyBoy/Wavetables", true);
        if (dir.empty())
        {
            setStatusText ("NO CARD OR DRIVE FOUND");
            return;
        }
        synthyboy::wtedit::Frame frame;
        synthyboy::convert::lfoShapeToFrame (contentOf (lfoNum_, editor_.model), frame);
        const std::vector<uint8_t> wav = synthyboy::encodeWav (frame.data.data(), nullptr, frame.data.size(), 48000, 32);
        std::string stem;
        for (int k = 1; k < 1000; ++k)
        {
            stem = "LFO " + std::to_string (lfoNum_) + " Shape" + (k > 1 ? " " + std::to_string (k) : std::string());
            struct stat st;
            if (::stat ((dir + "/" + stem + ".wav").c_str(), &st) != 0)
                break;
        }
        if (wav.empty() || ! safefile::writeReplacing ((dir + "/" + stem + ".wav").c_str(), reinterpret_cast<const char*> (wav.data()), wav.size()))
        {
            setStatusText ("CANNOT WRITE THE WAVETABLE FILE");
            return;
        }
        synthyboy::ContentFileRef ref;
        ref.pack = synthyboy::kUserWavetablePack;
        ref.file = stem + ".wav";
        ref.name = stem;
        v.requestWavetable (osc, ref);
        setStatusText ("SAVED " + stem + " FOR OSC " + static_cast<char> ('A' + osc - 1));
    }

    // ---- the modulation of the selected points (a page above the editor): which value (time, level, curve), the source, the amount
    struct PointModSelection
    {
        ui::lfo::PointMod common;
        ui::PointModPage::SelectionMod mix = ui::PointModPage::SelectionMod::Common;
    };

    PointModSelection pointModSelection() const noexcept
    {
        const auto& model = editor_.model;
        const auto selection = editor_.canvas.selection();
        const int first = selection.first();
        if (first < 0 || first >= model.n)
            return {};
        auto modAt = [&] (int i)
        {
            const auto& point = model.p[i];
            return pmTarget_ == 0 ? point.mx : (pmTarget_ == 1 ? point.my : point.mc);
        };
        using Mix = ui::PointModPage::SelectionMod;
        PointModSelection result { modAt (first) };
        for (int point = first + 1; point < model.n; ++point)
            if (selection.has (point))
            {
                const auto mod = modAt (point);
                if (mod.source != result.common.source)
                    return { {}, Mix::MixedSource };
                if (mod.amount != result.common.amount)
                    result.mix = Mix::MixedAmount;
            }
        if (result.mix == Mix::MixedAmount)
            result.common.amount = 0.0f;
        return result;
    }

    void loadPointMod (const IInstanceView& v, bool fresh)
    {
        const auto selection = editor_.canvas.selection();
        if (fresh)
        {
            uibind::fillSourceBoard (pointMod_.board);
            pmTarget_ = 1;
            pmTurnUntilMs_ = 0.0;
            pmPreset_ = v.stateGeneration();
            pmShape_ = nativeEditorShape_;
            pmSelection_ = selection;
        }
        const auto state = pointModSelection();
        pointMod_.load (pmTarget_, state.common.on() ? uibind::sourceName (state.common.source) : "",
                        static_cast<double> (state.common.amount) * 100.0, selection.count(), selection.first(), state.mix);
    }
    template <class Self>
    static auto pointModFields (Self& self) noexcept
        -> std::array<std::conditional_t<std::is_const_v<Self>, const ui::Component*, ui::Component*>, nativeui::lfodetail::Header>
    {
        auto& page = self.pointMod_;
        return { &page.back, &page.target, &page.amount, &page.remove };
    }

    int listNativeRows() const noexcept
    {
        return std::clamp (list_.state.count - list_.firstRow(), 0, std::min (list_.visibleRows(), nativeui::lfodetail::kRows));
    }

    bool nativeDetailHit (int x, int y) const noexcept
    {
        namespace n = nativeui::lfodetail;
        if (pageTop_ == kPagePointMod)
        {
            const auto fields = pointModFields (*this);
            for (int field = 0; field < n::Header; ++field)
                if (fields[field]->enabled() && fields[field]->rect().contains (x, y)) return true;
        }
        if (pageTop_ == kPageList && listKind_ == ListKind::Editor)
        {
            if (list_.headerRect().contains (x, y)) return true;
            for (int row = 0; row < listNativeRows(); ++row)
                if (nativeDetailRect (n::FirstRow + row).contains (x, y)) return true;
        }
        return false;
    }

    nativeui::lfodetail::State nativeDetailView (IInstanceView& v, const UiStack& stack) const
    {
        namespace n = nativeui::lfodetail;
        if ((! page_ && ! nativePlayEditor_) || ! v.nativeLfoShapeDetails() || rnd_ != 0 || editorContentExternal_
            || stack.depth() <= 2 || stack.element() != kElementLfo || &ui::theme::L() != &ui::theme::kLook1)
            return {};
        n::State state;
        state.context.editor = nativeEditorContext (v);
        if (stack.top() == kPagePointMod)
        {
            state.context.view = n::PointMod;
            const auto mod = pointModSelection();
            state.context.pointTarget = pmTarget_;
            state.context.pointSource = mod.common.source;
            using Mix = ui::PointModPage::SelectionMod;
            if (! state.context.editor.selection.any())
                state.point = n::NoPoints;
            else if (mod.mix == Mix::MixedSource)
                state.point = n::MixedSources;
            else if (mod.mix == Mix::MixedAmount)
                state.point = n::MixedAmounts;
            else
                state.point = mod.common.on() ? n::Assigned : n::NoMod;
            const auto fields = pointModFields (*this);
            for (int field = 0; field < n::Header; ++field)
                if (fields[field]->enabled())
                    state.fields |= 1u << field;
            state.values[n::Target] = n::rangeOf (n::Target).encode (pmTarget_);
            state.values[n::Amount] = n::rangeOf (n::Amount).encode (state.point == n::Assigned ? pointMod_.amount.state.value : -101.0);
        }
        else if (stack.top() == kPageList && listKind_ == ListKind::Editor)
        {
            state.context.view = list_.scrolls() ? n::ScrollingMenu : n::Menu;
            state.context.menu = nativeMenuRevision_;
            state.context.firstRow = list_.firstRow();
            state.headerBack = list_.state.headerBack;
            state.rows = listNativeRows();
            state.names[0] = list_.state.header;
            if (state.headerBack)
                state.fields |= 1u << n::Header;
            for (int i = 0; i < state.rows; ++i)
            {
                state.names[i + 1] = list_.state.items[state.context.firstRow + i];
                state.fields |= 1u << (n::FirstRow + i);
            }
        }
        else
            return {};
        return state;
    }

    void publishNativeDetail (IInstanceView& v, const nativeui::lfodetail::State& state)
    {
        namespace n = nativeui::lfodetail;
        if (state.context.view == n::Hidden && nativeDetailView_ == n::Hidden)
            return;
        list_.reserveScrollZone (pageTop_ == kPageList && listKind_ == ListKind::Editor);
        v.setNativeLfoDetail (state);
        nativeDetailView_ = state.context.view;
        nativeDetailFields_ = state.fields;
    }

    void updateNativeDetail (IInstanceView& v, UiStack& stack, double nowMs)
    {
        namespace n = nativeui::lfodetail;
        auto state = nativeDetailView (v, stack);
        for (int field = 0; field < n::kCount && state.context.view != n::Hidden; ++field)
        {
            float value;
            if (! v.takeNativeLfoDetail (state, field, value))
                continue;
            if (field == n::Target || field == n::Amount)
            {
                const auto next = n::rangeOf (field).decode (value);
                if (field == n::Target && next != pmTarget_)
                    pointModChange (ui::Change::indexed (kMpTarget, static_cast<int> (next), next), stack, v);
                else if (field == n::Amount && next >= -100.0)
                    pointModChange (ui::Change::normalised (kMpAmount, next), stack, v);
            }
            else if (value >= 0.5f)
            {
                if (field == n::Header)
                    editorListUp();
                else if (field >= n::FirstRow)
                    editorListChosen (state.context.firstRow + field - n::FirstRow, stack, v);
                else
                    pointModChange (ui::Change::request (field == n::Back ? kMpBack : kMpRemove), stack, v);
            }
            state = nativeDetailView (v, stack);
        }
        if (shapeDirty_ && ! editorContentExternal_ && ! gestureDown_ && nativeDetailView_ != n::Hidden)
            flushShape (v, nowMs);
        publishNativeDetail (v, state);
    }

    /** The two knobs of the page, as 0 .. 1: MODULATE (TIME, LEVEL, CURVE: 0, 0.5, 1) and AMOUNT (-100 .. +100 percent; 0.5 while nothing modulates the chosen value: the knob is off then). */
    float pointModVirtualValue (int id) const noexcept
    {
        if (id == kVirtPmTarget)
            return static_cast<float> (pmTarget_) * 0.5f;
        const ui::lfo::PointMod pm = pointModSelection().common;
        return pm.on() ? std::clamp (0.5f + 0.5f * pm.amount, 0.0f, 1.0f) : 0.5f;
    }
    /** A write of the host to one of the two knobs. MODULATE chooses the value (no undo state: it is a choice of the page, like the stepper). AMOUNT sets the amount of the chosen value of EVERY selected point, in whole
        percent like the touch field, and only when that value is modulated (the knob is off otherwise). A turn that changes nothing leaves no undo state; one continuous turn is ONE undo state (the ticks of a turn
        that follow each other within kTurnGapMs share the state pushed by the first: a touch, another knob or a pause starts a new one). */
    void applyPointModVirtual (int id, float x, double nowMs)
    {
        ui::lfo::ShapeModel& m = editor_.model;
        const ui::lfo::PointSet set = editor_.canvas.selection();
        if (set.first() < 0)
            return;
        if (id == kVirtPmTarget)
        {
            const int t = std::clamp (static_cast<int> (std::lround (x * 2.0f)), 0, 2);
            if (t != pmTarget_)
            {
                pmTarget_ = t;
                pmTurnUntilMs_ = 0.0;
            }
            return;
        }
        const ui::lfo::PointMod cur = pointModSelection().common;
        if (! cur.on())
            return;
        const ui::lfo::ShapeModel before = m;
        m.setMod (set, pmTarget_, cur.source, std::round ((x * 2.0f - 1.0f) * 100.0f) / 100.0f);
        if (m == before)
            return;
        if (nowMs > pmTurnUntilMs_)
            editor_.undo.push (before);
        pmTurnUntilMs_ = nowMs + kTurnGapMs;
        markDirty();
    }
    void updatePointMod (IInstanceView& v, double nowMs)
    {
        syncEditorContent();
        const auto selection = editor_.canvas.selection();
        const uint64_t preset = v.stateGeneration();
        const bool sameBinding = pmPreset_ == preset && pmShape_ == nativeEditorShape_ && pmSelection_ == selection;
        pmPreset_ = preset;
        pmShape_ = nativeEditorShape_;
        pmSelection_ = selection;
        if (! sameBinding || editorContentExternal_)
        {
            dropGesture();
            pmTurnUntilMs_ = 0.0;
        }
        for (int id = kVirtPmTarget; id <= kVirtPmAmount; ++id)
        {
            float x;
            if (v.takeQVirtual (id, x) && sameBinding && ! editorContentExternal_)
                applyPointModVirtual (id, x, nowMs);
        }
        loadPointMod (v, false);
        editor_.sync();
        for (int id = kVirtPmTarget; id <= kVirtPmAmount; ++id)
            v.setQVirtual (id, pointModVirtualValue (id));
    }
    void pointModChange (const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        using K = ui::ChangeKind;
        ui::lfo::ShapeModel& m = editor_.model;
        const ui::lfo::PointSet set = editor_.canvas.selection();
        const int first = set.first();
        if ((first < 0 || first >= m.n || editorContentExternal_) && c.id != kMpBack)
            return;
        pmTurnUntilMs_ = 0.0;   // a touch ends the run of knob ticks that shared one undo state
        const ui::lfo::ShapeModel before = m;
        const ui::lfo::PointMod cur = pointModSelection().common;
        switch (c.id)
        {
            case kMpBack: stack.back(); return;
            case kMpTarget: if (c.kind == K::Index) pmTarget_ = ((c.index % 3) + 3) % 3; break;
            case kMpAmount: if ((c.kind == K::Index || c.kind == K::Value) && cur.on()) m.setMod (set, pmTarget_, cur.source, static_cast<float> (c.value / 100.0)); break;
            case kMpRemove: if (c.kind == K::Request) m.setMod (set, pmTarget_, 0, 0.0f); break;
            case kMpBoard: if (c.kind == K::Index && c.index > 0) m.setMod (set, pmTarget_, c.index, cur.on() ? cur.amount : 0.5f); break;
            default: break;
        }
        if (! (m == before))
        {
            editor_.undo.push (before);
            markDirty();
        }
        loadPointMod (v, false);
        editor_.sync();
    }

    // ======================================================================================================================================================================
    // writing: a Change of a component to a parameter
    ui::Component* topComponent() noexcept
    {
        if (stackDepth_ == 0)
            return instPopup_ ? static_cast<ui::Component*> (&list_) : &compact_;
        if (pageTop_ == kPageEditor)
            return &editor_;
        if (pageTop_ == kPagePointMod)
            return &pointMod_;
        if (pageTop_ == kPageKeyboard)
            return &entry_;
        if (pageTop_ == kPageTargets)
            return &targets_;
        if (pageTop_ == kPageMods)
            return &mods_;
        if (pageTop_ == kPageList)
            return &list_;
        return &full_;
    }
    Item* find (int id) noexcept
    {
        for (Item& it : items_)
            if (it.id == id)
                return &it;
        return nullptr;
    }
    void openParamList (const Item* it, UiStack& stack, const IInstanceView& v)
    {
        if (it == nullptr || it->param < 0 || uibind::info (it->param).choices == nullptr || uibind::info (it->param).numChoices < 2)
            return;
        listKind_ = ListKind::Param;
        listParam_ = it->param;
        placePageList();
        stack.openDetail (kPageList);
        fillList (&v);
    }

    void handle (const ui::Change& c, UiStack& stack, IInstanceView& v, double nowMs)
    {
        if (! c)
            return;
        using K = ui::ChangeKind;
        const int id = c.id;
        if (id >= kEdBack && id <= kEdBend && stackDepth_ > 0 && pageTop_ == kPageEditor)
        {
            editorChange (c, stack, v, nowMs);
            return;
        }
        if (id >= kMpBack && id <= kMpBoard && stackDepth_ > 0 && pageTop_ == kPagePointMod)
        {
            pointModChange (c, stack, v);
            return;
        }
        if (instPopup_ && stackDepth_ == 0)   // the list of the selector of the compact view
        {
            if (id == kList && c.kind == K::Index)
            {
                if (listKind_ == ListKind::Plus)
                {
                    if (c.index >= 0 && c.index < ui::UsedSelector::kMaxChoices && plusIds_[c.index] > 0)
                    {
                        uibind::chooseLfo (v, plusIds_[c.index]);
                        adoptSelection (v);
                    }
                }
                else
                    selectSlot (c.index, v);
                const bool again = plusPending_;
                plusPending_ = false;
                closeInstanceList();
                if (again)
                    openInstanceList (v, ListKind::Plus);
            }
            else if (id == kListBackdrop && c.kind == K::Request)
                closeInstanceList();
            return;
        }
        if (c.kind == K::Request)
        {
            switch (id)
            {
                case kFullButton: stack.openFull (kElementLfo); return;
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
                case kClose: case kTargetsBack: stack.back(); return;
                case kListBackdrop: stack.back(); return;
                case kTargets: stack.openDetail (kPageTargets); return;
                case kTargetsAdd: listKind_ = ListKind::Dest; listGroup_ = -1; placePageList(); stack.openDetail (kPageList); fillList (&v); return;
                case kListHeader:
                    if (listKind_ == ListKind::Dest) { listGroup_ = -1; fillList (&v); }
                    else if (listKind_ == ListKind::Editor) editorListUp();
                    return;
                case kKbOk: if (pageTop_ == kPageKeyboard) keyboardOk (stack); return;
                case kKbCancel: if (pageTop_ == kPageKeyboard) stack.back(); return;
                case kEdit: openEditor (stack, v); return;
                case kInstance: openInstanceList (v); return;
                case kPageInstance: openPageInstanceList (stack, v); return;
                case kPagePicker: openParamList (find (id), stack, v); return;
                case kSelector: openPlusList (stack); return;
                case kShape: case kStyle: case kSync: case kMode: case kDirection: case kDivision:
                    if (stack.depth() == 0)
                        stack.openFull (kElementLfo);   // the list lies over the full screen: from the compact view that opens first
                    openParamList (find (id), stack, v);
                    return;
                default: break;
            }
            if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage && (id - kTargetsRow) % 4 == 3)
            {
                const int s = targetRows_[(id - kTargetsRow) / 4].slot;
                if (s >= 0)
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    modsDirty_ = true;
                    loadTargets (v, true);
                }
                return;
            }
            if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kPerPage && (id - kModsRow) % 4 == 3)
            {
                const auto row = modRows_[(id - kModsRow) / 4];
                if (modsContextValid (v) && uibind::modulationMatches (v, modsTargetParam_, row))
                {
                    const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (row.slot);
                    uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
                    loadMods (v);
                }
            }
            return;
        }
        if (c.kind == K::ModsRequest)
        {
            if (stack.depth() == 0)
                stack.openFull (kElementLfo);
            modsPageIndex_ = 0;
            modsPreset_ = v.stateGeneration();
            mods_.setPickingSource (false);
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
            listChosen (c.index, stack, v);
            return;
        }
        // ---- the instance: the stepper of the compact view, the cells of the full screen
        if (id == kInstance && c.kind == K::Index)
        {
            selectSlot (c.index, v);
            if (plusPending_)   // `+` with two free kinds: the two-row list over the third
            {
                plusPending_ = false;
                openInstanceList (v, ListKind::Plus);
            }
            return;
        }
        if (id == kPageInstance && c.kind == K::Index)
        {
            selectSlot (c.index, v);
            if (plusPending_)
            {
                plusPending_ = false;
                openPlusList (stack);
            }
            return;
        }
        if (id == kSelector && c.kind == K::Index)
        {
            uibind::chooseLfo (v, c.index);   // the id of the candidate: 1 .. 10, 11 .. 14 (R1 .. R4); view state only
            adoptSelection (v);
            return;
        }
        // ---- the sync switch: the mode and the two switches make one parameter
        if ((id == kSyncMode || id == kSyncTrip || id == kSyncDot) && c.kind == K::Index)
        {
            const int p = paramOf ("sync");
            const int part = id == kSyncMode ? ui::SyncSwitch::Mode : (id == kSyncTrip ? ui::SyncSwitch::Trip : ui::SyncSwitch::Dot);
            if (p >= 0)
                uibind::writePlain (v, p, full_.syncSwitch.nextSync (part, c));
            return;
        }
        if (id == kTargetsPage && c.kind == K::Index)
        {
            targetsPageIndex_ = c.index;
            loadTargets (v, true);
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
            mods_.addSource.setItem (uibind::sourceName (mods_.sourceIds[i]));
            return;
        }
        // ---- the rows of the targets page and of the modulations page of a control
        if (id >= kTargetsRow && id < kTargetsRow + 4 * ui::ModTargetsPage::kPerPage)
        {
            matrixRow (targetRows_[(id - kTargetsRow) / 4].slot, (id - kTargetsRow) % 4, c, v);
            return;
        }
        if (id >= kModsRow && id < kModsRow + 4 * ui::ModulationsPage::kPerPage)
        {
            const auto row = modRows_[(id - kModsRow) / 4];
            if (modsContextValid (v) && uibind::modulationMatches (v, modsTargetParam_, row))
                matrixRow (row.slot, (id - kModsRow) % 4, c, v);
            return;
        }
        Item* it = find (id);
        if (it == nullptr || it->c == nullptr || it->param < 0)
            return;
        if (c.kind == K::Depth)
        {
            const int s = uibind::slotOf (v, it->param, c.index);
            if (s >= 0)
                uibind::writeNorm (v, synthyboy::modSlotParams (s).amount, (c.value + 1.0) * 0.5);
            modsDirty_ = true;
            return;
        }
        write (*it, c, v);
    }
    void matrixRow (int s, int what, const ui::Change& c, IInstanceView& v)
    {
        using K = ui::ChangeKind;
        if (s < 0)
            return;
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        switch (what)
        {
            case 0: if (c.kind == K::Value) uibind::writeNorm (v, sp.amount, c.value); break;
            case 1: if (c.kind == K::Index) uibind::writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0); break;
            case 2: if (c.kind == K::Index) uibind::writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0); break;   // the switch says ON (active): bypassed is its opposite
            default: break;
        }
        modsDirty_ = true;
    }
    void listChosen (int index, UiStack& stack, IInstanceView& v)
    {
        if (listKind_ == ListKind::Instance)
        {
            selectSlot (index, v);
            const bool plus = plusPending_;
            plusPending_ = false;
            stack.back();
            if (plus)
                openPlusList (stack);
        }
        else if (listKind_ == ListKind::PageInstance)
        {
            if (index >= 0 && index < kInstances)
            {
                uibind::chooseLfo (v, index < kLfos ? index + 1 : uibind::kRndBase + index - kLfos + 1);
                adoptSelection (v);
            }
            stack.back();
        }
        else if (listKind_ == ListKind::Plus)
        {
            if (index >= 0 && index < ui::UsedSelector::kMaxChoices && plusIds_[index] > 0)
            {
                uibind::chooseLfo (v, plusIds_[index]);
                adoptSelection (v);
            }
            stack.back();
        }
        else if (listKind_ == ListKind::Editor)
            editorListChosen (index, stack, v);
        else if (listKind_ == ListKind::Param)
        {
            if (listParam_ >= 0)
            {
                const int was = static_cast<int> (std::lround (v.param (listParam_)));
                uibind::writePlain (v, listParam_, uibind::info (listParam_).min + index);
                if (rnd_ == 0 && listParam_ == paramOf ("shape") && index == ui::lfo::kShapeCustom)
                    ensureCustom (v, was);
            }
            stack.back();
        }
        else if (listKind_ == ListKind::Dest)
        {
            if (listGroup_ < 0)
            {
                listGroup_ = index;
                fillList (&v);
            }
            else
            {
                const std::vector<int>& ch = DestIndex::get().choices[static_cast<size_t> (listGroup_)];
                if (index >= 0 && index < static_cast<int> (ch.size()))
                    addTarget (v, ch[static_cast<size_t> (index)]);
                stack.back();
            }
        }
    }
    void write (const Item& it, const ui::Change& c, IInstanceView& v)
    {
        using K = ui::ChangeKind;
        const int p = it.param;
        const synthyboy::ParamInfo& pi = uibind::info (p);
        switch (it.type)
        {
            case T::Knob:
                if (c.kind == K::Value)
                    uibind::writeNorm (v, p, c.value);
                else if (c.kind == K::Index && it.id == kSmoothTime)   // the LED on the label of SMOOTH: the switch of the smoothing, the time is the knob
                    uibind::writePlain (v, paramOf ("smooth"), c.index != 0 ? 1.0 : 0.0);
                break;
            case T::Number: if (c.kind == K::Index || c.kind == K::Value) uibind::writePlain (v, p, c.value); break;
            case T::Stepper:
                if (c.kind == K::Index)
                {
                    const int n = pi.numChoices > 0 ? pi.numChoices : static_cast<int> (pi.max - pi.min) + 1;
                    const int idx = n > 0 ? (c.index % n + n) % n : 0;
                    const int was = static_cast<int> (std::lround (v.param (p)));
                    uibind::writePlain (v, p, pi.min + idx);
                    if (rnd_ == 0 && idx == ui::lfo::kShapeCustom && (it.id == kShape || it.id == kPagePicker))
                        ensureCustom (v, was);
                }
                break;
            case T::Toggle: if (c.kind == K::Index) uibind::writePlain (v, p, c.index != 0 ? 1.0 : 0.0); break;
            case T::List: if (c.kind == K::Index) uibind::writePlain (v, p, pi.min + std::clamp (c.index, 0, std::max (0, pi.numChoices - 1))); break;
            case T::Picker:
                if (c.kind == K::Index)
                {
                    const int was = static_cast<int> (std::lround (v.param (p)));
                    uibind::writePlain (v, p, c.index);
                    if (rnd_ == 0 && c.index == ui::lfo::kShapeCustom)
                        ensureCustom (v, was);
                }
                break;
            default: break;
        }
    }

    // ======================================================================================================================================================================
    // the Q group: the four targets the hardware knobs are mapped to
    template <class Self>
    static auto compactNativePart (Self& self, int field) noexcept
        -> std::conditional_t<std::is_const_v<Self>, const ui::Component*, ui::Component*>
    {
        namespace n = nativeui::playenvlfo;
        auto& compact = self.compact_;
        switch (field)
        {
            case n::LfoExpand: return &compact.full;
            case n::Shape: return &compact.shape;
            case n::Rate: return &compact.rate.rate;
            case n::Division: return &compact.rate.division;
            case n::Style: return &compact.style;
            case n::Sync: return &compact.sync;
            case n::StandardSync: return &compact.rate.modeCells;
            case n::Stereo: return &compact.stereo;
            case n::Choose: return &compact.instance;
            default: return nullptr;
        }
    }

    /** The four targets of the RATE (or RANDOM) group of the compact view and its block of knobs (the Play view maps the groups of the three thirds together). */
public:   // the Play view reads the targets of the compact group and sets its block
    void compactTargets (int* t) const noexcept
    {
        for (int i = 0; i < 4; ++i)
            t[i] = paramOfMember (compact_.group.member (i));
        if (compactNativeKey_)
            t[1] = -1;
    }
    void setCompactBlock (int block) noexcept { compact_.group.setBlock (block); }
private:
    int paramOfMember (const ui::Component* m) const noexcept
    {
        while (m != nullptr)
        {
            if (m == &full_.syncSwitch.mode)
                return paramOf ("sync");   // the HZ / BPM / KEY row is the member: a knob step moves the selection (the sync values: Free, Tempo, dotted, triplet, Key follow)
            for (const Item& it : items_)
                if (it.c == m && it.type != T::Inert)
                    return it.param;
            m = m->primaryChild();
        }
        return -1;
    }
    /** The groups of the page in reading order, published to the sixteen Q slots (q_publish.h): the compact view has RATE (RANDOM); the full screen its blocks (RATE, TIMING, MOTION, KEY when it is there, SHAPE; RANDOM,
        TRIGGER, KEY for a random LFO), the active one on Q 1 .. 4, the next on 5 .. 16; the editor its four virtual knobs; the targets and modulation pages one group per row. */
    void publishQGroup (IInstanceView& v)
    {
        const char* name = "";
        auto members = [this] (const ui::QGroup& g, int* t) { for (int i = 0; i < 4; ++i) t[i] = paramOfMember (g.member (i)); };
        if (stackDepth_ == 0)
        {
            name = rnd_ != 0 ? "Random" : "Rate";
            compact_.group.setActive (highlight_ == 3);
            return;   // the main state: the Play view publishes the three groups of the page together (PlayView::publishQ)
        }
        else if (pageTop_ == kPageEditor)
        {
            const int act = std::clamp (editor_.activeGroup(), 0, ui::ShapeEditorPage::kGroups - 1);
            name = act == 0 ? "Edit" : (act == 1 ? "Grid" : "Trigger");
            // EDIT = the virtual targets 0 .. 3 (POINT, X, Y, CURVE), GRID = 4 .. 6 (GRID X, GRID Y, SNAP), TRIGGER = MODE and PHASE (the parameters of the table) and the virtual 7 (BEND ALL); the active one is on Q 1 .. 4, the others after it
            const int modeP = paramOf ("sync_type"), phaseP = paramOf ("phase");
            qSlots_.publish (v, ui::ShapeEditorPage::kGroups, act, [modeP, phaseP] (int g, int* t)
            {
                for (int i = 0; i < 4; ++i)
                    t[i] = -1;
                if (g == 0)
                    for (int i = 0; i < 4; ++i)
                        t[i] = QRouter::encode (i, QRouter::kVirtual);
                else if (g == 1)
                    for (int i = 0; i < 3; ++i)
                        t[i] = QRouter::encode (4 + i, QRouter::kVirtual);
                else
                {
                    t[0] = modeP;
                    t[1] = phaseP;
                    t[2] = QRouter::encode (7, QRouter::kVirtual);
                }
            });
            int ids[ui::ShapeEditorPage::kGroups] = { 0, 1, 2 };
            setBlocks (editor_.groups, ui::ShapeEditorPage::kGroups, ids, ui::ShapeEditorPage::kGroups, act);
        }
        else if (pageTop_ == kPagePointMod)
        {
            name = "Modulation";
            // ONE group, always the active one: Q 1 = MODULATE (virtual 8), Q 2 = AMOUNT (virtual 9); the editor's own targets come back with the editor page (the slots are published again at the first update there)
            qSlots_.publish (v, pointMod_.target.enabled() ? 1 : 0, 0, [this] (int, int* t)
            {
                t[0] = QRouter::encode (kVirtPmTarget, QRouter::kVirtual);
                t[1] = pointModSelection().mix == ui::PointModPage::SelectionMod::MixedSource
                    ? -1 : QRouter::encode (kVirtPmAmount, QRouter::kVirtual);
                t[2] = t[3] = -1;
            });
            int ids[1] = { 0 };
            setBlocks (&pointMod_.group, 1, ids, 1, 0);
        }
        else if (pageTop_ == kPageKeyboard)
        {
            name = "Name";
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else if (pageTop_ == kPageTargets || pageTop_ == kPageMods)
        {
            const bool tg = pageTop_ == kPageTargets;
            name = tg ? "Targets" : "Mods";
            const ModRow* rows = tg ? targetRows_ : modRows_;
            const int n = tg ? targets_.numRows() : mods_.numRows(), act = tg ? targets_.activeRow() : mods_.activeRow();
            qSlots_.publish (v, n, act, [&] (int row, int* t) { modRowTargets (row >= 0 && row < ui::ModulationsPage::kPerPage ? rows[row].slot : -1, t); });
            if (tg)
                setRowBlocks (targets_, ui::ModTargetsPage::kPerPage, n, act);
            else
                setRowBlocks (mods_, ui::ModulationsPage::kPerPage, n, act);
        }
        else if (pageTop_ == kPageList)
        {
            name = "List";
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else
        {
            name = full_.activeGroup() >= 0 ? full_.groupName (full_.activeGroup()) : "";
            int ids[ui::LfoFullScreen::kMaxGroups], used = 0, pos = -1;
            for (int g = 0; g < full_.numGroups() && g < ui::LfoFullScreen::kMaxGroups; ++g)
                if (full_.groups[g].numMembers() > 0 && ! (g == full_.keyGroup() && ! full_.keyActive()))   // the KEY block is there only with the key follow sync
                {
                    if (g == full_.activeGroup())
                        pos = used;
                    ids[used++] = g;
                }
            if (ui::QGroup::wideDevice() && ! random())
            {
                // RATE's last member is the smoothing time. Put its one-member switch group
                // directly after it, then keep KEY, TIMING and MOTION contiguous on the XL strip.
                int targets[QRouter::kCount];
                std::fill_n (targets, QRouter::kCount, -1);
                int first = 0;
                for (int k = 0; k < used; ++k)
                {
                    auto& group = full_.groups[ids[k]];
                    group.setWideFirstSlot (first);
                    int next = first;
                    for (int i = 0; i < ui::QGroup::kSlots && first + i < QRouter::kCount; ++i)
                        if (const auto* member = group.member (i))
                        {
                            targets[first + i] = paramOfMember (member);
                            next = first + i + 1;
                        }
                    first = next;
                }
                addSelectorQ (targets, QRouter::kCount);
                qSlots_.publishRaw (v, targets);
            }
            else
            {
                int targets[QRouter::kCount];
                const int capacity = QRouter::mapGroups (used, pos, [&] (int k, int* t) { members (full_.groups[ids[k]], t); }, targets);
                // Prefer the spare slot in Smooth (standard) or Trigger
                // (random). A Key 61 bank can page Smooth out in KEY mode.
                const int block = QRouter::blockOfGroup (used, pos, random() ? used - 1 : 1);
                if (block >= 0)
                    addSelectorQ (targets + block * QRouter::kGroupSize, QRouter::kGroupSize);
                else
                    addSelectorQ (targets, capacity);
                qSlots_.publishRaw (v, targets);
                setBlocks (full_.groups, ui::LfoFullScreen::kMaxGroups, ids, used, pos);
            }
        }
        ui::setText (qName_, sizeof qName_, name);
    }

    // ======================================================================================================================================================================
    ui::LfoCompact compact_;
    ui::LfoFullScreen full_;
    ui::ShapeEditorPage editor_;
    ui::ModTargetsPage targets_;
    NativeModTargets nativeTargetPage_;
    ui::ModulationsPage mods_;
    ui::PointModPage pointMod_;
    ui::TextEntry entry_;
    LfoShapeLibrary lib_;
    ui::ListPopup list_;
    UiStack idle_;                       // an empty stack: the pages are read from it while another element's page is open
    std::vector<Item> items_;
    ui::Component* gesture_ = nullptr;
    bool gestureDown_ = false, virtualUndoActive_ = false, configured_ = false, modsDirty_ = true, shapeDirty_ = false, editorContentExternal_ = false, editorSentValid_ = false, haveCurve_ = false, displayHot_ = false;
    int nativeTargets_[nativeui::lfo::kCount] {};
    uint32_t nativeFields_ = 0;
    bool nativePublished_ = false;
    bool compactNativeKey_ = false;
    uint64_t nativeEditorShape_ = 0;
    nativeui::lfoeditor::State nativeEditorState_ {};
    nativeui::lfodetail::View nativeDetailView_ = nativeui::lfodetail::Hidden;
    uint16_t nativeDetailFields_ = 0;
    uint64_t nativeMenuRevision_ = 0;
    // the editor: the lists (SHAPES, MORE), the name keyboard, the modulation page
    Ed ed_ = Ed::None;
    Verb verb_ = Verb::Load;
    Pending pending_ = Pending::SaveOver;
    Kb kbPurpose_ = Kb::SaveAs;
    int edGroup_ = -1, pendingItem_ = -1, pmTarget_ = 1;
    double pmTurnUntilMs_ = 0.0;   // the knob ticks of AMOUNT up to this time share the undo state pushed by the first (applyPointModVirtual)
    uint64_t pmPreset_ = 0, pmShape_ = 0;
    ui::lfo::PointSet pmSelection_;
    std::string pendingName_, edHeader_, edYes_, kbInitial_;
    std::vector<std::string> edRows_;
    std::vector<int> edMap_;
    bool phaseBound_ = false;   // the PHASE and the loop point of the model say the same (loop modes): a change of the shape moves the PHASE
    double statusUntilMs_ = 0.0;
    static constexpr double kStatusMs = 3500.0, kTurnGapMs = 600.0;
    static constexpr int kMaxShapeName = 40;
    bool nativePlayEditor_ = false;
    bool page_ = false, nativeGround_ = false;   // standalone LFO and its confirmed host background
    uibind::UsedScan scan_;   // the last scan of the matrix and the parameters (selector candidates)
    int w_ = 1280, h_ = 800, lfoNum_ = 1, rnd_ = 0, highlight_ = 1, stackDepth_ = 0, pageTop_ = 0, pageSig_ = -1;
    int targetsPageIndex_ = 0, syncType_ = 0, direction_ = 0, shapeIdx_ = 0, editorShapeIdx_ = -1;
    float swing_ = 0.0f;
    int gridX_[kLfos] = {}, gridY_[kLfos] = {};   // the grid of the editor of every LFO (0: not chosen yet: the grid the shape was drawn on; stored with the shape, state version 5)
    double modsAtMs_ = -1000.0, shapeSentMs_ = -1000.0, curveAtMs_ = -1000.0, traceAtMs_ = -1000.0, displayChangedMs_ = -1000.0, lastNowMs_ = 0.0, bpm_ = 120.0, bpmSeen_ = -1.0;
    std::string prefix_ = "lfo1.";
    static constexpr int kCache = 16;
    mutable const char* cacheKey_[kCache] = {};
    mutable int cacheIdx_[kCache] = {}, nCache_ = 0;
    char qName_[16] = {}, name_[40] = {}, label_[40] = {};
    QSlots qSlots_;
    // the display
    synthyboy::ParamValues values_ = synthyboy::ParamValues::defaults();
    std::vector<int> dispIdx_;
    std::vector<float> seen_;
    std::shared_ptr<const synthyboy::StateContent> content_, contentSeen_;
    std::shared_ptr<const synthyboy::StateContent> editorContent_;
    ui::lfo::ShapeModel editorSentModel_;
    ui::lfo::ShapeModel linkedSnapshot_;
    bool linkedSnapshotValid_ = false;
    float curve_[kCurvePoints] = {};
    float trace_[kTracePoints] = {};
    int traceRepeats_ = kTracePoints;   // the initial trace already consists of equal samples
    int traceSource_ = 0;
    bool traceHasSignal_ = false;
    uint64_t traceGeneration_ = 0;
    int styleSeen_ = -1;
    ui::lfodisplay::Data data_;
};

}} // namespace synthyboy_mpc::overlay
