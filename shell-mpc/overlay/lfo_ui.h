// SynthyBoy - MPC overlay: the LFO UI of the Play page and of the LFO tab (owner decision 2026-11-27: the C++ layer has no UI elements). Every control of the element is a native component of the host skin: the shape / style
// choice, the rate, the division, the sync, the rise, the smoothing, the trigger, the phase, the delay, the direction, the swing, the stereo, the mono and host switches, the key numbers, the targets button, the instance choice
// of the compact view and, in the shape editor, the tools, the buttons, the numbers and the choices around the canvas. This page holds what is left: the parameter ROWS (the parameter behind every control: `rows`), the Q-Link
// groups (published to the router), the native bridge (nativeui::lfo, lfoeditor, lfodetail, playenvlfo, targets: publishes the values, takes the edits and writes them), the GEOMETRY the skin lays the cells out in
// (lfo_layout.h: so the overlay leaves them open and selects the Q group of a touched one), the displays (the shape of one cycle, the trace of a random LFO: ui::lfodisplay) and the EDITORS (the drawn shape on its canvas,
// the shape model, the undo stack, the content state, the saved shapes). One object per (Play view, plugin instance). Pure: no OS headers, no threads of its own; the overlay thread calls update () / touch () / tick (),
// with the instance as IInstanceView. The element has two types: an LFO (LFO 1 .. 10: a shape of one cycle, the drawn shape in the content state) and a RANDOM LFO (RND 1 .. 4: a style; the display is the trace of its output).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it shows (UiStack, ui_stack.h; the element id of this UI is kElementLfo, the pages above it are ours): main = the compact view in the right third of the Play page; full = the full screen over the whole plugin area
// of the Play page or the page of the LFO tab; detail pages above it: the shape editor, the modulation of the selected points (PointModModel) and the name keyboard of a saved shape (TextEntry, kept until the native keyboard
// is wired) above that, the targets of the LFO (the modulation matrix seen from the source), the list menu (the destination to add, the source to add, the SHAPES and MORE lists of the editor) and the modulations of a
// control. The editor's file work (the saved shapes in SynthyBoy/LFO Shapes/ on the card, lfo_shape_files.h) happens in the touch handler on the overlay thread, never in draw.
// What it does with an edit of the host: the native cell reports { field, value }; the row of the field says which parameter and how the value converts; the write goes through IInstanceView::writeParam (normalised; the
// plugin tells the host). The drawn shape is not a parameter: the editor changes a shape model, which goes to the content state through IInstanceView::setLfoShape (at most every kShapeSendMs while a finger is down, and
// at the end of the gesture). Every update () reads every parameter back, so host automation, presets and the host's own widgets show on the next frame.
// Q-Link: the compact view and the full screen have Q groups of table parameters (q_router.h); the shape editor's group EDIT (X, Y, CURVE, GRID of the selected point) is VIRTUAL: the router keeps the values and hands
// the host's writes back through takeQVirtual.
#pragma once

#include <algorithm>
#include <array>
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
#include "../native_lfo_pick.h"
#include "../native_play_env_lfo.h"
#include "../ui/binding.h"
#include "../ui/lfo_names.h"
#include "../ui/native_apertures.h"
#include "../ui/play_rows.h"
#include "../ui/shape_editor.h"
#include "../ui/text_entry.h"
#include "../safe_file.h"
#include "../q_router.h"
#include "backgrounds_manifest.h"
#include "q_publish.h"
#include "q_publish_mod.h"
#include "instance_view.h"
#include "lfo_layout.h"
#include "modulation_window.h"
#include "mod_rows_page.h"
#include "native_mod_targets_ui.h"
#include "lfo_shape_files.h"
#include "overlay_layout.h"
#include "page_view.h"
#include "touch_decoder.h"
#include "ui_bind.h"
#include "native_marker_covers.h"
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
    // ---- the pages (UiStack ids)
    static constexpr int kElementPage = 3;                                    // the page of an LFO tab is the full screen of the element (page_view.h)
    static constexpr int kElementLfo = 3;                                     // the full screen of the LFO (the oscillator is 1, the envelope 2)
    enum Page { kPageEditor = 40, kPageTargets = 41, kPageList = 42, kPageMods = 43, kPagePointMod = 44, kPageKeyboard = 45 };
    /** The virtual Q targets of the router (QRouter::kNumVirtual = 10) this page uses: 0 .. 7 are the editor's (POINT, X, Y, CURVE, GRID X, GRID Y, SNAP, BEND ALL), 8 and 9 belong to the page of the point modulation (MODULATE, AMOUNT). */
    static constexpr int kVirtEditorCount = 8, kVirtPmTarget = 8, kVirtPmAmount = 9;
    static_assert (kVirtPmAmount < QRouter::kNumVirtual, "the router must have a virtual target for every Q knob of the page");

    // ---- the ids of the controls (the page-local id of a row of the native bridge)
    enum Id : int
    {
        kClose = 1, kFullButton, kInstance, kSelector, kSyncMode, kSyncTrip, kSyncDot, kTargets, kPicker, kPagePicker, kSmoothPick, kHostPick, kEdit,   // kInstance: compact choice; kSelector: Play full-screen cells
        kShape = 100, kStyle, kRate, kDivision, kRise, kSmoothTime, kSync, kMode, kPhase, kDelay, kDirection, kSwing, kStereo, kMono, kStereoToggle, kKeyTranspose, kKeyTune, kModeList,
        kEdBack = 200, kEdTools, kEdUndo, kEdRedo, kEdDelete, kEdSnap, kEdLoop, kEdSmooth, kEdCycleLink, kEdLoopLink, kEdCanvas, kEdX, kEdY, kEdCurve, kEdGridX, kEdGridY, kEdPoint, kEdShapes, kEdMore, kEdMod, kEdMode, kEdPhase, kEdBend,
        kTgBack = 300, kTgAdd, kTgPage, kTgRow = 310,    // the cells of the targets page (310 + 4 * row + 0 depth, 1 polarity, 2 on / off, 3 remove)
        kMdBack = 340, kMdAdd, kMdPage, kMdRow = 350,    // the cells of the modulations page (same numbering)
        kMpBack = 230, kMpTarget, kMpAmount, kMpRemove, kMpBoard, kKbOk = 250, kKbCancel,
        kMemberRate = 900, kMemberSync   // the members of a Q group that are no row: the rate (the rate, or the division in the BPM sync) and the sync mode list
    };

    /** What a value is and how it converts to the engine's plain value. */
    enum class T : uint8_t
    {
        Dial,      // normalised 0 .. 1 of the parameter
        Number,    // a number in the units of the parameter
        Choice,   // a choice parameter
        Toggle,
        List,      // a choice parameter as a list (every option visible)
        Picker,    // the shape / style picker: the index is the value of the parameter
        Inert
    };
    /** One binding row of the page: the id of the control, the suffix of its engine key ("rate" -> "lfo3.rate": the prefix is the LFO shown) for a standard LFO and for a random LFO ("" = the control is not there), the type. */
    struct RowSpec { int id; const char* standard; const char* random; T type; };
    static constexpr RowSpec kRows[] = {
        { kShape, "shape", "", T::Choice },           { kStyle, "", "style", T::Choice },             { kRate, "rate", "rate", T::Dial },
        { kDivision, "division", "division", T::Choice }, { kRise, "fade_in", "", T::Dial },             { kSmoothTime, "smooth_time", "", T::Dial },
        { kSync, "sync", "sync", T::Choice },         { kStereoToggle, "", "stereo", T::Toggle },      { kPicker, "shape", "style", T::Picker },
        { kPagePicker, "shape", "style", T::Choice }, { kSmoothPick, "smooth", "", T::Toggle },        { kHostPick, "host", "", T::Toggle },
        { kMode, "sync_type", "", T::Choice },        { kModeList, "", "sync_type", T::List },         { kPhase, "phase", "", T::Dial },
        { kDelay, "delay", "", T::Dial },              { kDirection, "direction", "", T::List },        { kSwing, "swing", "", T::Dial },
        { kStereo, "stereo", "", T::Dial },            { kMono, "mono", "", T::Toggle },                { kKeyTranspose, "keytrack_transpose", "keytrack_transpose", T::Number },
        { kKeyTune, "keytrack_tune", "keytrack_tune", T::Number },
        { kInstance, "", "", T::Inert },               { kFullButton, "", "", T::Inert },               { kSyncMode, "", "", T::Inert },
        { kTargets, "", "", T::Inert },                { kEdit, "", "", T::Inert }
    };
    /** The binding of a row to the engine parameter of the LFO shown. */
    struct Item
    {
        int id = 0;
        T type = T::Inert;
        int param = -1;
    };

    static constexpr int kCanvasW = 1280, kCanvasH = 800, kTabY = 110, kTabH = 628;
    static constexpr int kCompactX = 858, kCompactW = 408;                    // the right third of the Play page
    static constexpr int kInstances = 14;                                      // LFO 1 .. 10, RND 1 .. 4
    static constexpr int kCurvePoints = 256, kTracePoints = 192;
    static constexpr double kModsRefreshMs = 100.0, kShapeSendMs = 90.0, kTraceMs = 33.0;
    static constexpr int kLfos = 10;

    LfoUi() { setCanvas (kCanvasW, kCanvasH); }

    void setCanvas (int w, int h)
    {
        w_ = w; h_ = h;
        geomKey_ = ~0u;
        relayout();
    }

    /** Selects LFO 1 .. 10 or RND 1 .. 4 (0 .. 13) without a touch: the gallery and the tests. */
    void select (int s, IInstanceView& v)
    {
        s = std::clamp (s, 0, kInstances - 1);
        uibind::chooseLfo (v, s < kLfos ? s + 1 : uibind::kRndBase + (s - kLfos + 1));
        adoptSelection (v);
    }


    /** Reads the instance: the selected LFO (the type and the number), every value of the visible controls, the modulations of the knobs, the pages of the stack, the shape, the cursors of the voices, the Q group. */
    void update (IInstanceView& inst, int highlight, const UiStack& stack, double nowMs, const synthyboy::LiveValues* live)
    {
        highlight_ = highlight;
        adoptSelection (inst);   // the page of a tab: the LFO of its hardware sub-tab; the Play page's expansion: the Play selection (Play LFO + mpcTabView.rnd, chunk saved)
        const bool open = stack.depth() > 0 && stack.element() == kElementLfo;
        if (open && stack.top() == kPageMods)
        {
            const Item* control = item (modsTarget_);
            const int parameter = control != nullptr ? control->param : -1;
            const uint64_t preset = inst.stateGeneration();
            if (modsPreset_ != preset || modsTargetParam_ != parameter)
            {
                dropGesture();
                modsPreset_ = preset;
                modsDirty_ = true;
            }
        }
        if (open && stackDepth_ == 0)
            setActiveGroup (0);
        stackDepth_ = open ? stack.depth() : 0;
        syncPages (open ? stack : idle_, inst);
        content_ = inst.content();
        bpm_ = inst.bpm();
        if (nowMs - modsAtMs_ >= kModsRefreshMs || modsDirty_)
        {
            refreshMods (inst);
            modsAtMs_ = nowMs;
            modsDirty_ = false;
        }
        if (! page_)   // the page of a tab has no selector: the hardware sub-tab names the LFO
        {
            uibind::fillSelector (uibind::SelKind::Lfo, scan_, inst, selector_);
            uibind::cellsFromSelector (uibind::SelKind::Lfo, selector_, playCells_);   // the Play page's choice: the SAME cells as its full-screen selector
            playDots_ = 0;
            for (int k = 0; k < playCells_.n && k < 32; ++k)
                if (isSourceAssigned (playCells_.id[k]))
                    playDots_ |= (1u << k);
        }
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
            inst.setUiFullOpen (! page_ || stack.depth() > 1);
        else if (stack.isMain())
            inst.setUiFullOpen (false);
        publishQGroup (inst);
    }

    /** The page of an LFO tab (page_view.h): the full screen of the LFO or random LFO the hardware sub-tab names (LFO 1 .. LFO 10, RND 1 .. RND 4: the view's own instance number 1 .. 14, no selector on the screen,
        nothing shared with the Play page), nothing to close. */
    void update (IInstanceView& inst, UiStack& stack, double nowMs, const page::Input& in)
    {
        if (! page_)
        {
            page_ = true;
            configured_ = false;
            geomKey_ = ~0u;
        }
        pageSlot_ = std::clamp (in.instance, 1, kInstances) - 1;
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
        if (entering)
            geomKey_ = ~0u;
        nativePlayEditor_ = true;
        updateHostedEditor (inst, stack, highlight, nowMs, live, entering);
    }

    void hideNativePlayEditor (IInstanceView& inst)
    {
        if (! nativePlayEditor_)
            return;
        nativePlayEditor_ = false;
        nativePage_ = false;
        geomKey_ = ~0u;
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
    void setHostBackground (bool confirmed) noexcept { nativeGround_ = confirmed; }
    bool hostBackground() const noexcept { return nativeGround_ && nativePage_ && pageTop_ == 0; }

    void drawPageGround (gpu::SceneBuilder&) const {}

    ui::Rect nativeRect (int field, int action = nativeui::lfo::kTargets) const noexcept
    {
        namespace n = nativeui::lfo;
        const lfolayout::Full& f = fullG_;
        switch (field)
        {
            case n::kShape: case n::kStyle: return f.namedPicker;
            case n::kAction: return action == n::kExpand ? f.edit : f.targets;
            case n::kSmooth: case n::kSmoothTime:
            {
                if (random() || ! nativeSmooth_ || f.smoothArea.empty())
                    return {};
                const ui::Rect r = f.smoothArea;
                constexpr int buttonW = 72;
                return field == n::kSmooth ? ui::Rect { r.x, r.y, buttonW, r.h } : ui::Rect { r.x + buttonW, r.y, r.w - buttonW, r.h };
            }
            case n::kRate: case n::kDivision: return f.rate;
            case n::kRise: return f.rise;
            case n::kSync: return f.syncMode;
            case n::kMode: return f.mode;
            case n::kRandomMode: return f.modeList;
            case n::kPhase: return f.phase;
            case n::kDelay: return f.delay;
            case n::kHost: return f.host;
            case n::kDirection: return f.direction;
            case n::kSwing: return f.swing;
            case n::kStereo: return f.stereo;
            case n::kMono: return f.mono;
            case n::kRandomStereo: return f.stereoToggle;
            case n::kTranspose: return f.keyTranspose;
            case n::kTune: return f.keyTune;
            default: return {};
        }
    }
    ui::Rect nativeEditorRect (int field) const noexcept { return editor_.rect (field); }
    ui::Rect nativeDetailRect (int field) const noexcept
    {
        namespace n = nativeui::lfodetail;
        if (field < n::Header)
            return pointMod_.rect (field);
        if (field == n::Header)
            return menu_.headerRect();
        const int row = field - n::FirstRow;
        return row < listNativeRows() ? menu_.rowRect (menu_.firstRow() + row) : ui::Rect {};
    }

    /** The LFO shown: 1 .. 10 (LFO) or 1 .. 4 (RND, `random ()`). */
    bool random() const noexcept { return rnd_ != 0; }
    int selection() const noexcept { return rnd_ != 0 ? rnd_ : lfoNum_; }
    int topPage() const noexcept { return pageTop_; }
    int activeQ() const noexcept { return activeGroup(); }
    const int* qTargets() const noexcept { return qSlots_.data(); }
    int qSlotCount() const noexcept { return QRouter::kCount; }
    const char* qGroupName() const noexcept { return qName_; }
    /** The selector of the full screen (tests). */
    const binding::SelectorSet& selector() const noexcept { return selector_; }
    /** The modulation source id (choice of modNN.source) of the LFO shown. */
    int source() const noexcept { return rnd_ != 0 ? 14 + rnd_ : uibind::lfoSource (lfoNum_); }
    /** The rectangle the picture of the LFO goes into now (empty when the current page shows none). */
    ui::Rect displayRect() const noexcept
    {
        if (stackDepth_ == 0)
            return compG_.display;
        if (pageTop_ == 0 || pageTop_ == kPageList)
            return fullG_.display;
        return {};
    }

    // ---- the drawing (a picture is one scene of cached cells, then the live parts, then the popups)
    void drawCompact (gpu::SceneBuilder& sb, bool nativeBackground = false) const
    {
        const ui::theme::Look& L = ui::theme::L();
        auto ground = [&] (int x, int y, int w, int h)
        {
            if (nativeBackground)
                return;   // Native Play background also owns the gaps between controls.
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
            const ui::Rect rise = compG_.rise;
            const ui::Rect smooth = compG_.smoothTime;
            ground (kGroundX0, midY, kGroundX1 - kGroundX0, rise.y - midY);
            ground (kGroundX0, rise.y, rise.x - kGroundX0, rise.h);
            ground (rise.right(), rise.y, smooth.x - rise.right(), rise.h);
            ground (smooth.right(), rise.y, kGroundX1 - smooth.right(), rise.h);
            ground (kGroundX0, rise.bottom(), kGroundX1 - kGroundX0, kTabY + kTabH - rise.bottom());
        }
        else
            ground (kGroundX0, midY, kGroundX1 - kGroundX0, kTabY + kTabH - midY);
        {
            ui::Pen pen (sb, true);
            ui::Cell cell (sb, compG_.display);
            pen.block (compG_.display);   // the frame of the display
        }
        ui::lfodisplay::draw (sb, compG_.display, compactData(), ! displayHot_, displayHot_ ? ui::lfodisplay::Part::Ground : ui::lfodisplay::Part::All);   // hot: the data follows with the live parts (drawLive), the cells keep their groups
    }
    /** The opaque ground of the whole tab and the page of the stack (the full screen, the editor, the targets, the modulations of a control). */
    void drawPage (gpu::SceneBuilder& sb) const
    {
        if (page_ && pageTop_ == 0)
            drawNativeMarkerCovers (sb, "lfo1");
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
            std::array<ui::Rect, nativeui::lfodetail::Header> cells {};
            for (size_t i = 0; i < cells.size(); ++i)
                cells[i] = pointMod_.rect (static_cast<int> (i));
            ui::outsideNativeCells (ui::Rect { 0, kTabY, w_, kTabH }, cells,
                                    [&] (ui::Rect strip) { drawRootGround (sb, strip); });
        }
        else if (pageTop_ != 0 && pageTop_ != kPageList)
            drawRootGround (sb, { 0, kTabY, w_, kTabH });
        if (pageTop_ == kPageEditor || (pageTop_ == kPageList && underList_ == kPageEditor))
        {
            drawEditor (sb);
            return;
        }
        if (pageTop_ == kPagePointMod)
        {
            drawPointMod (sb);
            return;
        }
        if (pageTop_ == kPageKeyboard)
        {
            entry_.draw (sb);
            return;
        }
        if (pageTop_ == kPageTargets || (pageTop_ == kPageList && underList_ == kPageTargets))
            targetsView_.draw (sb);
        else if (pageTop_ == kPageMods || (pageTop_ == kPageList && underList_ == kPageMods))
            modsView_.draw (sb);
        else
        {
            if (! page_)
                drawFullFurniture (sb);
            ui::lfodisplay::draw (sb, fullG_.display, fullDisplayData(), ! displayHot_, displayHot_ ? ui::lfodisplay::Part::Ground : ui::lfodisplay::Part::All);
        }
    }
    void drawPopup (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ > 0 && pageTop_ == kPageList)
        {
            namespace n = nativeui::lfodetail;
            drawMenuChrome (sb);
            auto cut = [&] (ui::Rect cell)
            {
                const gpu::ScopedClip clip (sb, cell.x, cell.y, cell.right(), cell.bottom());
                if (clip)
                    sb.clear (0u);
            };
            cut (menu_.headerRect());
            if (listNativeRows() > 0)
                cut (ui::Rect::unite (nativeDetailRect (n::FirstRow), nativeDetailRect (n::FirstRow + listNativeRows() - 1)));
        }
    }
    /** The cursors of the voices / the newest value of the trace (the display that is on screen). */
    void drawLive (gpu::SceneBuilder& sb) const
    {
        if (stackDepth_ == 0)
        {
            if (displayHot_)
                ui::lfodisplay::draw (sb, compG_.display, compactData(), false, ui::lfodisplay::Part::Data);   // the data of a hot display (drawCompact left it out): live, after every cell
            ui::lfodisplay::drawLive (sb, compG_.display, compactData());
        }
        else if (pageTop_ == 0 || (pageTop_ == kPageList && underList_ == 0))
        {
            const auto data = fullDisplayData();
            if (displayHot_)
                ui::lfodisplay::draw (sb, fullG_.display, data, false, ui::lfodisplay::Part::Data);
            ui::lfodisplay::drawLive (sb, fullG_.display, data);
            if (nativePlayEditor_ && pageTop_ == 0)
                ui::clearNativeCell (sb, fullG_.close);
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
        if ((pageTop_ == kPageTargets && targetsView_.nativeHit (x, y))
            || (pageTop_ == kPageMods && modsView_.nativeHit (x, y)))
            return false;
        if (stackDepth_ > 0)
        {
            if ((nativePlayEditor_ && pageTop_ == 0 && fullG_.close.contains (x, y))
                || nativeFieldAt (x, y) >= 0 || nativeEditorHit (x, y) || nativeDetailHit (x, y))
                return false;
            return ui::Rect { 0, kTabY, w_, kTabH }.contains (x, y);
        }
        if (rnd_ == 0 && (compG_.rise.contains (x, y) || compG_.smoothTime.contains (x, y)))
            return false;
        return ui::Rect { kCompactX, kTabY + 4, kCompactW, kTabH - 8 }.contains (x, y);
    }

    // ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
    /** One touch event in UI pixels (the whole screen), `stack` is the instance's stack (the caller holds its lock). true: the touch belongs to the plugin-drawn pages. */
    bool touch (const TouchEvent& ev, UiStack& stack, IInstanceView& inst, double nowMs)
    {
        const int x = ev.x, y = ev.y;
        if (ev.kind == TouchEvent::Down)
        {
            int targetRow = -1;
            const bool modulations = pageTop_ == kPageMods;
            if (modulations ? modsView_.nativeHit (x, y, &targetRow)
                            : pageTop_ == kPageTargets && targetsView_.nativeHit (x, y, &targetRow))
            {
                dropGesture();
                if (targetRow >= 0)
                {
                    if (modulations)
                        modsView_.selectRow (targetRow);
                    else
                        targetsView_.selectRow (targetRow);
                    publishQGroup (inst);
                }
                return false;
            }
            gesture_ = Gesture::None;
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
            if (! covers (x, y))
                return false;
            gesture_ = gestureOfPage();
            gestureDown_ = true;
            virtualUndoActive_ = false;
            touchDown (x, y, stack, inst, nowMs);
            publishQGroup (inst);
            return true;
        }
        if (gesture_ == Gesture::None || ! gestureDown_)
            return false;
        if (ev.kind == TouchEvent::Move)
            touchMove (x, y, stack, inst, nowMs);
        else
        {
            touchUp (x, y, stack, inst, nowMs);
            gestureDown_ = false;
            gesture_ = Gesture::None;
            virtualUndoActive_ = false;
            if (shapeDirty_ && ! editorContentExternal_)
                flushShape (inst, nowMs);   // the end of a gesture: the shape goes to the content at once
        }
        return true;
    }
    /** The clock while a finger is down (the long press of a point of the shape). */
    void tick (double nowMs, UiStack& stack, IInstanceView& inst)
    {
        if (gesture_ == Gesture::Canvas && gestureDown_)
            handle (editor_.canvas.tick (nowMs), stack, inst, nowMs);
    }
    /** The mouse wheel at (x, y) (UI pixels, `notches` positive away from the user): the list menu scrolls; every other control is a native cell that takes its own wheel. */
    bool wheel (int x, int y, int notches, UiStack&, IInstanceView&, double)
    {
        if (gestureDown_ || pageTop_ != kPageList || ! covers (x, y) || ! menu_.box().contains (x, y))
            return false;
        menu_.scroll (-notches);
        return true;
    }

    bool fingerDown() const noexcept { return gestureDown_; }
    void dropGesture() noexcept { gesture_ = Gesture::None; gestureDown_ = false; }
    /** The page went off the screen: nothing of the compact view stays open. */
    void closePopups() {}
    /** A full screen opened or closed somewhere: the router keeps what the last page set, so this UI publishes its Q group again at the next update (). */
    void forgetQGroup() noexcept { qSlots_.forget(); }

    // ---- access for the tests and the other pages
    ui::Rect compactNativeRect (int field) const noexcept
    {
        namespace n = nativeui::playenvlfo;
        switch (field)
        {
            case n::LfoExpand: return compG_.full;
            case n::Shape: return compG_.shape;
            case n::Rate: return compG_.rateKnob;
            case n::Division: return compG_.division;
            case n::Style: return compG_.style;
            case n::Sync: return compG_.sync;
            case n::StandardSync: return compG_.modeCells;
            case n::Stereo: return compG_.stereo;
            case n::Choose: return compG_.instance;
            default: return {};
        }
    }
    void setCompactNativeParts (const nativeui::playenvlfo::State& view)
    {
        namespace n = nativeui::playenvlfo;
        compactNativeKey_ = view.context.view == n::LfoKey || view.context.view == n::RndKey;
    }
    void prepareCompactPresentation (IInstanceView& inst)
    {
        adoptSelection (inst);
        const int sync = paramOf ("sync");
        syncMode_ = ui::lfo::syncModeOf (static_cast<int> (std::lround (inst.param (sync))));
    }

    int compactNativeTarget (int field) const noexcept
    {
        namespace n = nativeui::playenvlfo;
        if (field == n::StandardSync && ! random())
            return paramOf ("sync");
        const Item* it = item (compactItemOf (field));
        return it != nullptr && it->type != T::Inert ? it->param : -1;
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
            const int next = nextSync (SyncPart::Mode, mode);
            if (target < 0 || static_cast<float> (next) == v.param (target))
                return false;
            uibind::writePlain (v, target, next);
            return true;
        }
        const Item* it = item (compactItemOf (field));
        if (it == nullptr || it->type == T::Inert || it->param < 0)
            return false;
        const auto& info = uibind::info (it->param);
        if (field == n::Rate)
        {
            if (std::fabs (uibind::normOf (it->param, v.param (it->param)) - value) <= 1.0e-6)
                return false;
            write (*it, ui::Change::normalised (it->id, value), v);
        }
        else
        {
            const auto wanted = synthyboy::constrainValue (info, synthyboy::fromNormalised (info, value));
            if (wanted == v.param (it->param))
                return false;
            write (*it, ui::Change::indexed (it->id, static_cast<int> (std::lround (wanted - info.min)), wanted), v);
        }
        return true;
    }
    /** What the Play view's native bridge asks of the compact view: the sync mode of its rate (`compact ().rate.mode ()`). */
    struct CompactView
    {
        struct Rate
        {
            ui::lfo::SyncMode m;
            ui::lfo::SyncMode mode() const noexcept { return m; }
        } rate;
    };
    CompactView compact() const noexcept { return { { syncMode_ } }; }
    ui::ShapeEditorModel& editor() noexcept { return editor_; }
    void syncNativeTargets (IInstanceView& inst, UiStack& stack, bool fromPlay)
    {
        using namespace nativeui::targets;
        const auto before = stack.changes();
        targetsNative_.update (inst, targetsView_, pageTop_ == kPageTargets, fromPlay ? PlayLfo : Lfo, source(), targetRows_,
                               [&] (ui::Change c) { rowsCommand (false, c, stack, inst); });
        const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (modsTargetParam_));
        modsNative_.update (inst, modsView_, modsContextValid (inst), fromPlay ? PlayLfo : Lfo, static_cast<int> (destination), modRows_,
                            [&] (ui::Change c) { rowsCommand (true, c, stack, inst); }, Modulations);
        if (stack.changes() != before)
        {
            stackDepth_ = stack.depth();
            syncPages (stack, inst);
        }
        updateNativePick (inst, stack);
        if (pageTop_ == kPageTargets || pageTop_ == kPageMods || stack.changes() != before)
            publishQGroup (inst);
    }
    void hideNativeTargets (IInstanceView& inst)
    {
        targetsNative_.hide (inst, targetsView_);
        modsNative_.hide (inst, modsView_);
        hideNativePick (inst);
    }
    LfoShapeLibrary& library() noexcept { return lib_; }
    ui::TextEntry& keyboard() noexcept { return entry_; }
    const std::vector<Item>& items() const noexcept { return items_; }
    const Item* item (int id) const noexcept { for (const Item& it : items_) if (it.id == id) return &it; return nullptr; }
    const ui::lfodisplay::Data& displayData() const noexcept { return data_; }
    /** The data of the full page: the picture of the LFO below the edit button and the page title. */
    ui::lfodisplay::Data fullDisplayData() const noexcept
    {
        auto data = data_;
        const auto edit = fullG_.edit;
        if (! edit.empty())
        {
            const auto graph = fullG_.display.inset (8);
            const int top = std::max (edit.bottom() + 8, graph.y + ui::lfodisplay::titleRoom (data));   // below the edit button and the page title (inner margin to the border)
            data.pageTitle = true;
            data.area = { graph.x, top, graph.w, std::max (0, graph.bottom() - 6 - top) };
            data.aperture = edit;
        }
        return data;
    }

    /** The data of the compact view (the Play third): the same picture with the plot area below the header band. The full page and the shape editor draw data_ as it is (no area). */
    ui::lfodisplay::Data compactData() const noexcept
    {
        ui::lfodisplay::Data d = data_;
        d.area = ui::playrows::plotRect (compG_.display);
        d.aperture = compG_.full;
        return d;
    }
    int gridOf (int lfo) const noexcept { return gridXOf (lfo); }
    int gridXOf (int lfo) const noexcept { return lfo >= 1 && lfo <= kLfos ? gridX_[lfo - 1] : 8; }
    int gridYOf (int lfo) const noexcept { return lfo >= 1 && lfo <= kLfos ? gridY_[lfo - 1] : 8; }

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
    using ModRow = uibind::ModRow;
    enum class Gesture : uint8_t { None, Canvas, Keyboard, Menu, Ground };
    enum class SyncPart : uint8_t { Mode, Trip, Dot };

    // ======================================================================================================================================================================
    // building: the selected LFO (the type and the number) decides the parameters behind the ids
    void buildItems()
    {
        items_.clear();
        for (const RowSpec& row : kRows)
        {
            Item it;
            it.id = row.id;
            it.type = row.type;
            const char* suffix = rnd_ != 0 ? row.random : row.standard;
            it.param = suffix != nullptr && suffix[0] != '\0' ? uibind::indexOfKey (prefix_ + suffix) : -1;
            items_.push_back (it);
        }
    }
    /** The row id of the control the native Play view calls `field`. */
    static int compactItemOf (int field) noexcept
    {
        namespace n = nativeui::playenvlfo;
        switch (field)
        {
            case n::Shape: return kShape;
            case n::Rate: return kRate;
            case n::Division: return kDivision;
            case n::Style: return kStyle;
            case n::Sync: return kSync;
            case n::Stereo: return kStereoToggle;
            default: return 0;
        }
    }
    /** The sync value after a change of the mode (HZ / BPM / KEY: `index`) or of TRIP / DOT: the mode keeps the switches, a switch excludes the other. */
    int nextSync (SyncPart part, int index) const noexcept
    {
        using M = ui::lfo::SyncMode;
        M m = ui::lfo::syncModeOf (sync_);
        bool tr = ui::lfo::syncTrip (sync_), dt = ui::lfo::syncDot (sync_);
        if (part == SyncPart::Mode)
            m = static_cast<M> (std::clamp (index, 0, 2));
        else if (part == SyncPart::Trip) { tr = index != 0; if (tr) dt = false; }
        else { dt = index != 0; if (dt) tr = false; }
        return ui::lfo::syncOf (m, tr, dt);
    }

    /** Recomputes the rectangles of the pages when something that decides them changed (the type, the sync, the page mode, the editor button). */
    void relayout()
    {
        const uint32_t key = static_cast<uint32_t> ((random() ? 1 : 0) | (page_ ? 2 : 0) | (keyActive_ ? 4 : 0) | (nativePlayEditor_ ? 8 : 0) | (editShown() ? 16 : 0) | (w_ << 8));
        if (key == geomKey_)
            return;
        geomKey_ = key;
        const ui::Rect tab { 0, kTabY, w_, kTabH };
        fullG_ = lfolayout::full (tab, random(), page_, keyActive_, nativePlayEditor_, editShown());
        compG_ = lfolayout::compact ({ kCompactX, kTabY + 4, kCompactW, kTabH - 8 }, random());
        editor_.place (tab);
        pointMod_.place (tab);
        menu_.parent = tab;
    }
    bool editShown() const noexcept { return editShown_ && ! random(); }


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
            return stack.depth() == 1 && stack.element() == kElementLfo;
        };
        const auto pull = [&]
        {
            nativePage_ = useNative();
            update (inst, highlight, stack, nowMs, live);
        };
        if (refresh || nativePage_ != useNative())
            pull();
        if (page_)
            syncNativeTargets (inst, stack, false);
        nativePage_ = useNative();
        const auto changes = stack.changes();
        updateNativeEditor (inst, stack, nowMs);
        updateNativeDetail (inst, stack, nowMs);
        updateNativePick (inst, stack);
        if (stack.changes() != changes)
        {
            pull();
            publishNativeEditor (inst, nativeEditorView (inst, stack));
            publishNativeDetail (inst, nativeDetailView (inst, stack));
            updateNativePick (inst, stack);
        }
        if (! nativePage_)
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

    int nativeFieldAt (int x, int y) const noexcept
    {
        namespace n = nativeui::lfo;
        if (! nativePage_)
            return -1;
        for (int field = 0; field < n::kCount; ++field)
            if ((nativeFields_ & n::bit (field)) != 0 && nativeRect (field).contains (x, y))
                return field;
        return ! random() && nativeRect (n::kAction, n::kExpand).contains (x, y) ? n::kAction : -1;
    }

    /** A touch on (or an edit of) the native control of `field` makes the Q group it is a member of the active one. */
    void selectNativeQGroup (int field)
    {
        const int target = nativeTargets_[field];
        if (target < 0)
            return;
        buildGroups();
        for (int group = 0; group < nGroups_; ++group)
            for (int slot = 0; slot < 4; ++slot)
                if (paramOfMember (groups_[group].member[slot]) == target)
                {
                    setActiveGroup (group);
                    return;
                }
    }


    nativeui::lfo::ViewState nativeView (const IInstanceView& inst) noexcept
    {
        namespace n = nativeui::lfo;
        nativeFields_ = 0;
        if (! nativePage_)
            return {};
        n::ViewState state;
        state.selection = slot() + 1;
        state.preset = inst.stateGeneration();
        const int syncTarget = nativeTargets_[n::kSync];
        const auto sync = ui::lfo::syncModeOf (syncTarget >= 0 ? static_cast<int> (std::lround (inst.param (syncTarget))) : 0);
        const bool key = sync == ui::lfo::SyncMode::Key, beats = sync == ui::lfo::SyncMode::Bpm;
        state.layout = (random() ? 3 : 0) + (key ? 3 : beats ? 2 : 1);
        state.values[n::kLayout] = static_cast<float> (state.layout) / n::kLayoutLast;
        state.fields = n::bit (n::kAction);
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
            return false;   // reserved: the page has no selector (the hardware sub-tab names the LFO)
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
        buildItems();
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
        gesture_ = Gesture::None; gestureDown_ = false;
        editorContent_.reset();
        editorContentExternal_ = false;
        editorShapeIdx_ = -1;
        pageSig_ = -1;
        nCache_ = 0;
        geomKey_ = ~0u;
        relayout();
    }

    // ======================================================================================================================================================================
    // the pages above the full screen
    void syncPages (const UiStack& stack, IInstanceView& inst)
    {
        const int top = stack.depth() >= 2 ? stack.top() : 0;
        const int sig = top * 16 + (top == kPageMods ? modsTarget_ & 0xff : 0);
        if (top == pageTop_ && sig == pageSig_)
            return;
        if (pageTop_ == kPageList && top != kPageList)
        {
            menu_.items.clear();
            menu_.header = nullptr;
            listKind_ = ListKind::None;
        }
        if (gesture_ != Gesture::None && pageTop_ != top)
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
            case kPageEditor: syncEditor (inst); break;
            case kPagePointMod: loadPointMod (inst, true); break;
            case kPageKeyboard: beginKeyboard(); break;
            default: break;
        }
        modsDirty_ = true;
        qSlots_.forget();
    }

    // ---- the targets page: the rows of the matrix whose source is this LFO
    ModRow targetRows_[ModRowsPage::kPerPage];
    int targetTotal_ = 0;
    void loadTargets (const IInstanceView& v, bool fresh)
    {
        constexpr int perPage = ModRowsPage::kPerPage;
        int slots[uibind::kMaxTargetSlots];
        const int total = uibind::slotsOfSource (v, source(), slots, uibind::kMaxTargetSlots);
        const int pages = std::max (1, (total + perPage - 1) / perPage);
        targetsPageIndex_ = std::clamp (targetsPageIndex_, 0, pages - 1);
        const int first = targetsPageIndex_ * perPage, n = std::clamp (total - first, 0, perPage);
        ModRowsPage::Entry e[perPage];
        for (int i = 0; i < n; ++i)
        {
            const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (slots[first + i]);
            targetName (static_cast<size_t> (std::lround (v.param (sp.destination))), e[i].name, sizeof e[i].name);
            e[i].depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
            e[i].bidirectional = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
            e[i].bypassed = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
            targetRows_[i] = { slots[first + i], source() };
        }
        // only a change of the rows is loaded again
        bool same = ! fresh && n == targetsView_.numRows() && total == targetTotal_;
        for (int i = 0; same && i < n; ++i)
        {
            same = targetsView_.sameRow (i, e[i]);
        }
        if (same)
            return;
        targetTotal_ = total;
        char title[16];
        std::snprintf (title, sizeof title, rnd_ != 0 ? "RND %d" : "LFO %d", selection());
        targetsView_.place ({ 0, kTabY, w_, kTabH });
        targetsView_.setIds (kTgBack, kTgAdd, kTgPage, kTgRow);
        targetsView_.load (e, n, title, ui::theme::sourceColour (rnd_ != 0 ? "RND" : "LFO"), total, targetsPageIndex_);
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
        constexpr int rows = ModRowsPage::kPerPage;
        const int position = uibind::targetPosition (v, modulationSource, slot);
        targetsPageIndex_ = position / rows;
        loadTargets (v, true);
        targetsView_.selectRow (position % rows);
        modsDirty_ = true;
    }

    // ---- the list menu of the shape editor (SHAPES, MORE): the rows are native cells of the host (nativeui::lfodetail menu); the plugin holds the names and the window. The sources and the targets that are added
    // here are NOT in it: ADD SOURCE, ADD TARGET and the SOURCE of a point modulation are host choices (nativeui::lfopick) that open the MenuOverlay of the host.
    enum class ListKind : uint8_t { None, Editor };
    ListKind listKind_ = ListKind::None;
    int underList_ = 0;
    std::vector<std::string> listText_;
    void placePageList() { menu_.parent = { 0, kTabY, w_, kTabH }; menu_.first = 0; }
    bool isSourceAssigned (int id) const noexcept
    {
        if (id >= 1 && id <= kLfos)
            return scan_.used[uibind::lfoSource (id)];
        if (id > uibind::kRndBase && id <= uibind::kRndBase + uibind::kSelRnds)
            return scan_.used[uibind::rndSource (id - uibind::kRndBase)];
        return false;
    }
    /** The names of the editor list that is open. */
    void fillList (const IInstanceView* v)
    {
        (void) v;
        if (listKind_ == ListKind::Editor)
            fillEditorList();
    }
    int listNativeRows() const noexcept { return menu_.nativeRows (nativeui::lfodetail::kRows); }
    /** The header of the list was tapped: one level up (the editor lists), or the list closes. */
    void listUp (UiStack& stack, IInstanceView& v)
    {
        (void) v;
        if (listKind_ == ListKind::Editor)
            editorListUp();
        else
            stack.back();
    }
    /** A row of the list was chosen. */
    void listChosen (int index, UiStack& stack, IInstanceView& v)
    {
        if (listKind_ == ListKind::Editor)
            editorListChosen (index, stack, v);
    }

    static const char* const* instanceNames() noexcept
    {
        static const char* const n[kInstances] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "LFO 9", "LFO 10", "RND 1", "RND 2", "RND 3", "RND 4" };
        return n;
    }
    static const char* instanceName (int i) noexcept { return instanceNames()[std::clamp (i, 0, kInstances - 1)]; }
    /** The instance as one number 0 .. 13: LFO 1 .. 10, then RND 1 .. 4. */
    int slot() const noexcept { return rnd_ != 0 ? kLfos + rnd_ - 1 : lfoNum_ - 1; }
    /** The shown LFO: on the page of a tab the one of its sub-tab; on the Play page's expansion the shared view state (Play LFO and mpcTabView.rnd): a change reconfigures the page. */
    void adoptSelection (IInstanceView& v)
    {
        int num, r;
        if (page_ && pageSlot_ >= 0)   // the hardware sub-tab names the LFO: nothing is read from the Play selection or the chunk view state
        {
            num = pageSlot_ < kLfos ? pageSlot_ + 1 : 1;
            r = pageSlot_ < kLfos ? 0 : pageSlot_ - kLfos + 1;
        }
        else
        {
            const int id = uibind::selectedLfo (v);   // 1 .. 10, or kRndBase + m
            num = std::clamp (v.playSelection (2), 1, kLfos);
            r = id > uibind::kRndBase ? id - uibind::kRndBase : 0;
        }
        if (! configured_ || num != lfoNum_ || r != rnd_)
        {
            lfoNum_ = num;
            rnd_ = r;
            configure (v);
        }
    }
    int pageSlot_ = -1;   // the page of a tab: the LFO of its sub-tab (0 .. 13: LFO 1 .. 10, RND 1 .. 4); -1 in the Play page's expansion
    uibind::SelectorCells playCells_;
    unsigned playDots_ = 0;
public:
    const uibind::SelectorCells& playCells() const noexcept { return playCells_; }
    const binding::SelectorSet& tabSelector() const noexcept { return selector_; }
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
        if (! page_)   // the sub-tab's LFO is its own: no shared selection to fall back
            uibind::leaveSelector (uibind::SelKind::Lfo, v);
    }
private:

    // ---- the modulations page of one control
    ModRow modRows_[ModRowsPage::kPerPage];
    NativeModTargets targetsNative_, modsNative_;
    int modsPageIndex_ = 0;
    uint64_t modsPreset_ = 0;
    int modsTarget_ = 0, modsTargetParam_ = -1;
    bool modsContextValid (const IInstanceView& inst) const noexcept
    {
        const Item* control = item (modsTarget_);
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
            std::snprintf (title, sizeof title, "%.12s", controlLabel (t->id));
        const auto window = uibind::modulationWindow (v, modsTargetParam_, source(), modsPageIndex_, modRows_, focusSlot);
        modsPageIndex_ = window.page;
        ModRowsPage::Entry e[ModRowsPage::kPerPage];
        const int n = std::clamp (window.entries.numMods, 0, ModRowsPage::kPerPage);
        for (int i = 0; i < n; ++i)
        {
            const ui::ModEntry& m = window.entries.mods[i];
            std::snprintf (e[i].name, sizeof e[i].name, "%s", m.name);
            e[i].depth = std::clamp (m.depth, -1.0, 1.0);
            e[i].bidirectional = m.bidirectional;
            e[i].bypassed = m.bypassed;
            e[i].macro = m.macro;
        }
        modsView_.place ({ 0, kTabY, w_, kTabH });
        modsView_.setIds (kMdBack, kMdAdd, kMdPage, kMdRow);
        modsView_.load (e, n, title, 0xffffffu, window.total, window.page);
        if (window.focusRow >= 0)
            modsView_.selectRow (window.focusRow);
        char tv[16] = "";
        if (modsTargetParam_ >= 0)
            uibind::formatValue (uibind::info (modsTargetParam_), v.param (modsTargetParam_), tv, sizeof tv);
        modsView_.setTargetValue (tv);
    }
    /** The label of the control a modulations page is about (the title of the page). */
    const char* controlLabel (int id) const noexcept
    {
        switch (id)
        {
            case kRate: return "RATE"; case kRise: return "RISE"; case kSmoothTime: return "SMOOTH"; case kPhase: return "PHASE"; case kDelay: return "DELAY";
            case kSwing: return "SWING"; case kStereo: return "STEREO"; case kKeyTranspose: return "TRANSPOSE"; case kKeyTune: return "TUNE";
            default: return "MODULATIONS";
        }
    }
    void addModulation (IInstanceView& v, int src)
    {
        if (! modsContextValid (v))
            return;
        if (const int slot = uibind::ensureModulationSource (v, modsTargetParam_, src); slot >= 0)
            loadMods (v, slot);
    }
    void refreshMods (const IInstanceView& v)
    {
        scan_ = uibind::scanUsed (v);   // which LFOs the selector shows
        if (pageTop_ == kPageTargets)
            loadTargets (v, false);
        else if (pageTop_ == kPageMods && modsTargetParam_ >= 0)
            loadMods (v);
    }
    /** The commands of the native cells of a row page (targets, modulations of a control). */
    void rowsCommand (bool modulations, const ui::Change& c, UiStack& stack, IInstanceView& v)
    {
        using K = ui::ChangeKind;
        const int idBack = modulations ? kMdBack : kTgBack, idAdd = modulations ? kMdAdd : kTgAdd, idPage = modulations ? kMdPage : kTgPage, idRow = modulations ? kMdRow : kTgRow;
        if (c.id == idBack && c.kind == K::Request)
        {
            stack.back();
            return;
        }
        if (c.id == idAdd && c.kind == K::Request)
            return;   // ADD SOURCE and ADD TARGET are host choices (nativeui::lfopick): the cell of the shared page is covered by them and adds nothing itself
        if (c.id == idPage && c.kind == K::Index)
        {
            if (modulations && modsContextValid (v))
            {
                modsPageIndex_ = c.index;
                loadMods (v);
            }
            else if (! modulations)
            {
                targetsPageIndex_ = c.index;
                loadTargets (v, true);
            }
            return;
        }
        if (c.id < idRow || c.id >= idRow + 4 * ModRowsPage::kPerPage)
            return;
        const int row = (c.id - idRow) / 4, what = (c.id - idRow) % 4;
        const ModRow r = modulations ? modRows_[row] : targetRows_[row];
        if (modulations && ! (modsContextValid (v) && uibind::modulationMatches (v, modsTargetParam_, r)))
            return;
        if (r.slot < 0)
            return;
        (modulations ? modsView_ : targetsView_).selectRow (row);
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (r.slot);
        if (what == 3 && c.kind == K::Request)
        {
            uibind::writePlain (v, sp.source, 0); uibind::writePlain (v, sp.destination, 0); uibind::writePlain (v, sp.amount, 0);
            modulations ? loadMods (v) : loadTargets (v, true);
        }
        else if (what == 0 && c.kind == K::Value)
            uibind::writeNorm (v, sp.amount, c.value);
        else if (what == 1 && c.kind == K::Index)
            uibind::writePlain (v, sp.bipolar, c.index != 0 ? 1.0 : 0.0);
        else if (what == 2 && c.kind == K::Index)
            uibind::writePlain (v, sp.bypass, c.index != 0 ? 0.0 : 1.0);   // the switch says ON (active): bypassed is its opposite
        modsDirty_ = true;
    }

    // ======================================================================================================================================================================
    // reading: the state of the page from the parameters
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
        // ---- what depends on the sync, the mode and the smoothing
        const int syncP = paramOf ("sync");
        sync_ = syncP >= 0 ? static_cast<int> (std::lround (v.param (syncP))) : 0;
        syncMode_ = ui::lfo::syncModeOf (sync_);
        const bool key = syncMode_ == ui::lfo::SyncMode::Key;
        if (key != keyActive_)
        {
            keyActive_ = key;
            qSlots_.forget();
        }
        const int smoothP = paramOf ("smooth");
        smoothOn_ = smoothP < 0 || v.param (smoothP) >= 0.5f;
        const int modeP = paramOf ("sync_type");
        syncType_ = modeP >= 0 ? static_cast<int> (std::lround (v.param (modeP))) : 0;
        const int directionP = paramOf ("direction");
        const int swingP = paramOf ("swing");
        direction_ = directionP >= 0 ? static_cast<int> (std::lround (v.param (directionP))) : 0;
        swing_ = swingP >= 0 ? static_cast<float> (v.param (swingP) * 0.01) : 0.0f;
        const int shapeP = paramOf (rnd_ != 0 ? "style" : "shape");
        shapeIdx_ = shapeP >= 0 ? static_cast<int> (std::lround (v.param (shapeP))) : 0;
        editShown_ = rnd_ == 0 && (page_ || nativePlayEditor_ || shapeIdx_ == ui::lfo::kShapeCustom);   // standalone expands any LFO shape; Play popup expands only Custom
        relayout();
        if (stackDepth_ > 0 && pageTop_ == kPageEditor)
            syncEditor (v);
        // the nameplate
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
        d.traceMarkerCached = ! displayHot_;
        data_ = d;
        data_.label = label_;
    }
    // ======================================================================================================================================================================
    // the shape editor
    /** Opens a local editable model. The first edit commits its points to the instrument. */
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
        shapeDirty_ = false;
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
        const ui::lfo::ShapeModel m = shape != nullptr ? modelOf (*shape) : ui::lfo::ShapeModel::fromBuiltin (shapeIdx_ == ui::lfo::kShapeCustom ? 1 : shapeIdx_);
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
        const ui::lfo::ShapeModel incoming = shape != nullptr ? modelOf (*shape) : ui::lfo::ShapeModel::fromBuiltin (shapeIdx_ == ui::lfo::kShapeCustom ? 1 : shapeIdx_);
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
    void markGridDirty() noexcept
    {
        // Built-ins have no stored grid. The first shape edit saves the current grid with its points.
        if (editorShapeIdx_ == ui::lfo::kShapeCustom)
            shapeDirty_ = true;
    }
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
        if (editorShapeIdx_ != ui::lfo::kShapeCustom)
        {
            editorShapeIdx_ = ui::lfo::kShapeCustom;
            uibind::writePlain (v, paramOf ("shape"), editorShapeIdx_);
        }
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
            if (gestureDown_ && gesture_ == Gesture::Canvas)
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
            markGridDirty();
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

    // ---- the native controls around the canvas: the id of a field (the Change of the editor handlers) and what the page knows of it
    static int editorIdOf (int field) noexcept
    {
        static const int ids[nativeui::lfoeditor::kCount] = { kEdBack, kEdTools, kEdShapes, kEdMore, kEdUndo, kEdRedo, kEdDelete, kEdMod, kEdSnap, kEdLoop, kEdSmooth, kEdCycleLink, kEdLoopLink,
                                                              kEdPoint, kEdX, kEdY, kEdCurve, kEdGridX, kEdGridY, kEdPhase, kEdBend, kEdMode };
        return ids[std::clamp (field, 0, nativeui::lfoeditor::kCount - 1)];
    }
    bool nativeEditorHit (int x, int y) const noexcept
    {
        if (pageTop_ != kPageEditor) return false;
        for (int field = 0; field < nativeui::lfoeditor::kCount; ++field)
            if (editor_.fields[field].enabled && nativeEditorRect (field).contains (x, y))
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
        const int selected = state.context.selection.count();
        for (int field = 0; field < n::kCount; ++field)
        {
            const ui::EditorField& control = editor_.fields[field];
            if (control.enabled)
                state.enabled |= 1u << field;
            const auto range = n::rangeOf (field, state.context.points, selected > 1);
            switch (n::kFields[field].type)
            {
                case n::kAction: break;
                case n::kToolChoice: state.values[field] = range.encode (editor_.canvas.state.tool); break;
                case n::kToggle: state.values[field] = control.on ? 1.0f : 0.0f; break;
                case n::kNumber:
                    if (field != n::kPoint || selected == 1)
                        state.values[field] = range.encode (control.value);
                    break;
                case n::kChoice: state.values[field] = range.encode (control.index); break;
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
            for (int field = 0; field < n::kCount && state.context.owner != 0; ++field)
            {
                float value;
                if (! v.takeNativeLfoEditor (state, field, value))
                    continue;
                const auto type = n::kFields[field].type;
                const auto range = n::rangeOf (field, state.context.points, state.scale());
                const double next = range.decode (value);
                const double current = range.decode (state.values[field]);
                const int id = editorIdOf (field);
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
            case kEdTools: if (c.kind == K::Index) cs.tool = std::clamp (c.index, 0, ui::ShapeTool::kTools - 1); break;
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
                    markGridDirty();
                }
                break;
            case kEdPoint:
                if (value && m.n > 0)
                    cv.selectOnly (std::clamp (static_cast<int> (std::lround (c.value)) - 1, 0, m.n - 1));
                break;
            case kEdMode:
                if (c.kind == K::Index && paramOf ("sync_type") >= 0)
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
        placePageList();
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
        listText_.clear();
        for (const std::string& r : edRows_)
            listText_.push_back (r);
        menu_.items.clear();
        for (const std::string& r : listText_)
            menu_.items.push_back (r.c_str());
        menu_.header = header;
        menu_.headerBack = back;
        menu_.first = 0;
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
        markGridDirty();
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
        ui::PointSelectionMod mix = ui::PointSelectionMod::Common;
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
        using Mix = ui::PointSelectionMod;
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
    /** The source of the modulation of the selected points' value (the choice of the source: the value keeps its amount, else half). */
    void setPointSource (int src, IInstanceView& v)
    {
        ui::lfo::ShapeModel& m = editor_.model;
        const ui::lfo::PointSet set = editor_.canvas.selection();
        if (set.first() < 0 || src <= 0 || editorContentExternal_)
            return;
        const ui::lfo::PointMod cur = pointModSelection().common;
        const ui::lfo::ShapeModel before = m;
        m.setMod (set, pmTarget_, src, cur.on() ? cur.amount : 0.5f);
        if (! (m == before))
        {
            editor_.undo.push (before);
            markDirty();
        }
        loadPointMod (v, false);
        editor_.sync();
    }

    bool nativeDetailHit (int x, int y) const noexcept
    {
        namespace n = nativeui::lfodetail;
        if (pageTop_ == kPagePointMod)
            for (int field = 0; field < n::Header; ++field)
                if (pointMod_.fields[field].enabled && pointMod_.rect (field).contains (x, y)) return true;
        if (pageTop_ == kPageList)
        {
            if (menu_.headerRect().contains (x, y)) return true;
            for (int row = 0; row < listNativeRows(); ++row)
                if (nativeDetailRect (n::FirstRow + row).contains (x, y)) return true;
        }
        return false;
    }

    nativeui::lfodetail::State nativeDetailView (IInstanceView& v, const UiStack& stack) const
    {
        namespace n = nativeui::lfodetail;
        const bool editorList = pageTop_ == kPageList && listKind_ == ListKind::Editor;
        if ((! page_ && ! nativePlayEditor_) || ! v.nativeLfoShapeDetails() || (rnd_ != 0 && editorList) || (editorContentExternal_ && (editorList || pageTop_ == kPagePointMod))
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
            using Mix = ui::PointSelectionMod;
            if (! state.context.editor.selection.any())
                state.point = n::NoPoints;
            else if (mod.mix == Mix::MixedSource)
                state.point = n::MixedSources;
            else if (mod.mix == Mix::MixedAmount)
                state.point = n::MixedAmounts;
            else
                state.point = mod.common.on() ? n::Assigned : n::NoMod;
            for (int field = 0; field < n::Header; ++field)
                if (pointMod_.fields[field].enabled)
                    state.fields |= 1u << field;
            state.values[n::Target] = n::rangeOf (n::Target).encode (pmTarget_);
            state.values[n::Amount] = n::rangeOf (n::Amount).encode (state.point == n::Assigned ? pointMod_.fields[ui::PointModModel::kAmount].value : -101.0);
        }
        else if (stack.top() == kPageList && listKind_ != ListKind::None)
        {
            state.context.view = menu_.scrolls() ? n::ScrollingMenu : n::Menu;
            state.context.menu = nativeMenuRevision_;
            state.context.firstRow = menu_.firstRow();
            state.headerBack = menu_.headerBack;
            state.rows = listNativeRows();
            state.names[0] = menu_.header;
            if (state.headerBack || (menu_.header != nullptr && listKind_ != ListKind::Editor))
                state.fields |= 1u << n::Header;
            for (int i = 0; i < state.rows; ++i)
            {
                state.names[i + 1] = menu_.items[static_cast<size_t> (state.context.firstRow + i)];
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
        menu_.reserveZone = pageTop_ == kPageList;
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
                    listUp (stack, v);
                else if (field >= n::FirstRow)
                    listChosen (state.context.firstRow + field - n::FirstRow, stack, v);
                else
                    pointModChange (ui::Change::request (field == n::Back ? kMpBack : kMpRemove), stack, v);
            }
            state = nativeDetailView (v, stack);
        }
        if (shapeDirty_ && ! editorContentExternal_ && ! gestureDown_ && nativeDetailView_ != n::Hidden)
            flushShape (v, nowMs);
        publishNativeDetail (v, state);
    }


    // ---- the host choices of the LFO (nativeui::lfopick): the SOURCE of a point modulation, ADD SOURCE of the MODULATIONS page, ADD TARGET (GROUP, then the TARGET of that group) of the TARGETS page. The host opens its own
    // MenuOverlay for each (row tap marks, the tap on the header applies); the plugin only publishes the current index and takes the one the host applied. A choice that adds resets to its placeholder row.
    static nativeui::lfoeditor::Range pickRange (int field) noexcept { return nativeui::lfopick::rangeOf (uibind::lfoPickLast (field)); }

    nativeui::lfopick::State nativePickView (IInstanceView& v, const UiStack& stack) const
    {
        namespace n = nativeui::lfopick;
        n::State state;
        if (! v.nativeLfoPick())
            return state;
        const auto bit = [] (int field) { return uint64_t (1) << field; };
        const auto detail = nativeDetailView (v, stack);
        if (detail.context.view == nativeui::lfodetail::PointMod)
        {
            state.context.view = n::PointSource;
            state.context.editor = detail.context.editor;
            state.context.target = pmTarget_;
            if (detail.allows (nativeui::lfodetail::Target))   // points are selected
                state.fields |= bit (n::Source);
            state.values[n::Source] = pickRange (n::Source).encode (detail.context.pointSource);
        }
        else if (modsNative_.active())
        {
            state.context.view = n::AddSource;
            state.context.subject = modsTargetParam_;
            state.fields |= bit (n::AddSourceChoice);
        }
        else if (targetsNative_.active())
        {
            state.context.view = n::AddTarget;
            state.context.subject = source();
            const int groups = std::min (n::kMaxGroups, static_cast<int> (uibind::DestIndex::get().groups.size()));
            state.fields |= bit (n::Group);
            state.values[n::Group] = pickRange (n::Group).encode (std::clamp (pickGroup_, 0, groups));
            if (pickGroup_ > 0 && pickGroup_ <= groups)
                state.fields |= bit (n::FirstTarget + pickGroup_ - 1);
        }
        else
            return {};
        state.context.owner = page_ ? 1 : 2;
        state.context.slot = slot();
        state.context.preset = v.stateGeneration();
        return state;
    }

    void publishNativePick (IInstanceView& v, const nativeui::lfopick::State& state)
    {
        if (state.context.view == nativeui::lfopick::Hidden && ! pickShown_)
            return;
        v.setNativeLfoPick (state);
        pickShown_ = state.context.view != nativeui::lfopick::Hidden;
    }

    void hideNativePick (IInstanceView& v)
    {
        pickGroup_ = 0;
        publishNativePick (v, {});
    }

    void updateNativePick (IInstanceView& v, const UiStack& stack)
    {
        namespace n = nativeui::lfopick;
        auto state = nativePickView (v, stack);
        for (int field = 0; field < n::kCount && state.context.view != n::Hidden; ++field)
        {
            float value;
            if (! v.takeNativeLfoPick (state, field, value))
                continue;
            const int index = static_cast<int> (pickRange (field).decode (value));
            if (field == n::Source)
            {
                if (index > 0)
                    setPointSource (index, v);
            }
            else if (field == n::AddSourceChoice)
            {
                if (index > 0)
                    addModulation (v, index);
            }
            else if (field == n::Group)
                pickGroup_ = index;
            else if (index > 0)
            {
                const auto& choices = uibind::DestIndex::get().choices;
                const auto group = static_cast<size_t> (field - n::FirstTarget);
                if (group < choices.size() && index <= static_cast<int> (choices[group].size()))
                    addTarget (v, choices[group][static_cast<size_t> (index) - 1]);
                pickGroup_ = 0;
            }
            state = nativePickView (v, stack);
        }
        if (state.context.view != n::AddTarget)
            pickGroup_ = 0;
        publishNativePick (v, state);
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
    // the touch of the parts the plugin owns: the canvas of the editor, the name keyboard, the list menu, the blocks of the full screen
    Gesture gestureOfPage() const noexcept
    {
        if (stackDepth_ == 0)
            return Gesture::Ground;
        if (pageTop_ == kPageEditor)
            return Gesture::Canvas;
        if (pageTop_ == kPageKeyboard)
            return Gesture::Keyboard;
        if (pageTop_ == kPageList)
            return Gesture::Menu;
        return Gesture::Ground;
    }
    void touchDown (int x, int y, UiStack& stack, IInstanceView& v, double nowMs)
    {
        switch (gesture_)
        {
            case Gesture::Canvas: editor_.canvas.setId (kEdCanvas); handle (editor_.canvas.touchDown (x, y, nowMs), stack, v, nowMs); break;
            case Gesture::Keyboard: handle (entry_.touchDown (x, y, nowMs), stack, v, nowMs); break;
            case Gesture::Menu:
            {
                const ui::Rect box = menu_.box();
                menuMode_ = ! box.contains (x, y) ? MenuTouch::Backdrop : menu_.scrolls() && menu_.zoneRect().contains (x, y) ? MenuTouch::Zone : MenuTouch::Box;
                menuX0_ = x; menuY0_ = y; menuFirst0_ = menu_.firstRow();
                if (menuMode_ == MenuTouch::Zone)
                    menu_.dragThumb (y);
                break;
            }
            case Gesture::Ground: if (stackDepth_ > 0 && pageTop_ == 0 && ! page_) selectGroupAtBlocks (x, y); break;
            default: break;
        }
    }
    void touchMove (int x, int y, UiStack& stack, IInstanceView& v, double nowMs)
    {
        switch (gesture_)
        {
            case Gesture::Canvas: handle (editor_.canvas.touchMove (x, y), stack, v, nowMs); break;
            case Gesture::Keyboard: handle (entry_.touchMove (x, y), stack, v, nowMs); break;
            case Gesture::Menu:
                if (menuMode_ == MenuTouch::Zone)
                    menu_.dragThumb (y);
                else if (menuMode_ == MenuTouch::Box && std::abs (y - menuY0_) > 12)
                    menu_.first = std::clamp (menuFirst0_ + (menuY0_ - y) / lfolayout::Menu::kRowH, 0, std::max (0, menu_.count() - menu_.visibleRows()));
                break;
            default: break;
        }
    }
    void touchUp (int x, int y, UiStack& stack, IInstanceView& v, double nowMs)
    {
        switch (gesture_)
        {
            case Gesture::Canvas: handle (editor_.canvas.touchUp (x, y), stack, v, nowMs); break;
            case Gesture::Keyboard: handle (entry_.touchUp (x, y), stack, v, nowMs); break;
            case Gesture::Menu:
                if (menuMode_ == MenuTouch::Backdrop && std::abs (x - menuX0_) <= 12 && std::abs (y - menuY0_) <= 12)
                    stack.back();   // a tap on the dimmed ground closes the list
                menuMode_ = MenuTouch::None;
                break;
            default: break;
        }
    }
    /** A touch in a block of the bottom band of the full screen (outside the native cells) makes that block's group the active one. */
    void selectGroupAtBlocks (int x, int y)
    {
        buildGroups();
        for (int g = 0; g < nGroups_; ++g)
        {
            const int b = groups_[g].block;
            if (b >= 0 && b < fullG_.nBlocks && fullG_.blocks[b].contains (x, y))
            {
                setActiveGroup (g);
                return;
            }
        }
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
        if (c.kind == K::Request)
        {
            switch (id)
            {
                case kKbOk: if (pageTop_ == kPageKeyboard) keyboardOk (stack); return;
                case kKbCancel: if (pageTop_ == kPageKeyboard) stack.back(); return;
                default: break;
            }
        }
    }
    void write (const Item& it, const ui::Change& c, IInstanceView& v)
    {
        using K = ui::ChangeKind;
        const int p = it.param;
        if (p < 0)
            return;
        const synthyboy::ParamInfo& pi = uibind::info (p);
        switch (it.type)
        {
            case T::Dial:
                if (c.kind == K::Value)
                    uibind::writeNorm (v, p, c.value);
                break;
            case T::Number: if (c.kind == K::Index || c.kind == K::Value) uibind::writePlain (v, p, c.value); break;
            case T::Choice:
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
    Item* find (int id) noexcept
    {
        for (Item& it : items_)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    // ======================================================================================================================================================================
    // the Q groups: the four targets the hardware knobs are mapped to
    struct GroupDef
    {
        char name[16] = {};
        int member[4] = { -1, -1, -1, -1 };   // the row id of every member (kMemberRate / kMemberSync: no row), -1 = none
        int block = -1;                       // the block of the bottom band the group stands in (-1: none)
        int numMembers() const noexcept { int n = 0; for (int m : member) n += m >= 0; return n; }
    };
    static constexpr int kMaxGroups = 6;
    mutable GroupDef groups_[kMaxGroups];
    mutable int nGroups_ = 0, keyGroup_ = -1;
    char activeName_[16] = "Rate";

    /** The groups of the page in reading order (the blocks left to right, the members of a block top to bottom): the shape page RATE, [KEY], TIMING, MOTION, SMOOTH; a random LFO RANDOM, [KEY], TRIGGER. The KEY group (TRANSPOSE
        and TUNE, in the first block where the rate knob is not) is there in the key follow sync only. */
    void buildGroups() const
    {
        nGroups_ = 0;
        keyGroup_ = -1;
        auto next = [&] (const char* name, int block, int a, int b = -1, int c = -1, int d = -1)
        {
            GroupDef& g = groups_[nGroups_++];
            std::snprintf (g.name, sizeof g.name, "%s", name);
            g.member[0] = a; g.member[1] = b; g.member[2] = c; g.member[3] = d;
            g.block = block;
        };
        const int pick = page_ ? kPagePicker : kPicker;
        if (! random())
        {
            next ("Rate", 0, pick, keyActive_ ? -1 : kMemberRate, kRise, kSmoothTime);
            const bool smoothFirst = ui::QGroup::wideDevice() || page_;   // next knob on the XL after the time dial, adjacent in standalone page mode
            if (smoothFirst)
                next ("Smooth", -1, kSmoothPick);
            if (keyActive_)
            {
                keyGroup_ = nGroups_;
                next ("Key", 0, kKeyTranspose, kKeyTune);
            }
            next ("Timing", 1, kMemberSync, kPhase, kDelay, kMode);
            next ("Motion", 2, kDirection, kSwing, kStereo, kMono);
            if (! smoothFirst)
                next ("Smooth", -1, kSmoothPick);
        }
        else
        {
            next ("Random", 0, pick, keyActive_ ? -1 : kMemberRate, kMemberSync, kStereoToggle);
            if (keyActive_)
            {
                keyGroup_ = nGroups_;
                next ("Key", 0, kKeyTranspose, kKeyTune);
            }
            next ("Trigger", 0, kModeList);
        }
    }
    int activeGroup() const noexcept
    {
        for (int i = 0; i < nGroups_; ++i)
            if (std::strcmp (groups_[i].name, activeName_) == 0)
                return i;
        return 0;
    }
    void setActiveGroup (int i)
    {
        buildGroups();
        if (i >= 0 && i < nGroups_ && i == keyGroup_ && ! keyActive_)
            i = 0;
        if (i >= 0 && i < nGroups_)
            std::snprintf (activeName_, sizeof activeName_, "%s", groups_[i].name);
    }
    int paramOfMember (int id) const noexcept
    {
        if (id == kMemberRate)
            return syncMode_ == ui::lfo::SyncMode::Bpm ? paramOf ("division") : paramOf ("rate");   // the rate is the division in the BPM sync
        if (id == kMemberSync)
            return paramOf ("sync");   // the HZ / BPM / KEY row is the member: a knob step moves the selection (the sync values: Free, Tempo, dotted, triplet, Key follow)
        if (id < 0)
            return -1;
        for (const Item& it : items_)
            if (it.id == id && it.type != T::Inert)
                return it.param;
        return -1;
    }
public:
    /** The four targets of the RATE (or RANDOM) group of the compact view (the Play view maps the groups of the three thirds together). */
    void compactTargets (int* t) const noexcept
    {
        t[0] = paramOfMember (random() ? kStyle : kShape);
        t[1] = paramOfMember (kMemberRate);
        t[2] = paramOfMember (random() ? kSync : kRise);
        t[3] = paramOfMember (random() ? kStereoToggle : kSmoothTime);
        if (compactNativeKey_)
            t[1] = -1;
    }
    void setCompactBlock (int block) noexcept { compactBlock_ = block; }
private:
    /** The groups of the page in reading order, published to the sixteen Q slots (q_publish.h): the compact view has RATE (RANDOM); the full screen its blocks (RATE, TIMING, MOTION, KEY when it is there, SHAPE; RANDOM,
        TRIGGER, KEY for a random LFO), the active one on Q 1 .. 4, the next on 5 .. 16; the editor its four virtual knobs; the targets and modulation pages one group per row. */
    void publishQGroup (IInstanceView& v)
    {
        const char* name = "";
        if (stackDepth_ == 0)
            return;   // the main state: the Play view publishes the three groups of the page together (PlayView::publishQ)
        else if (pageTop_ == kPageEditor)
        {
            const int act = std::clamp (editor_.activeGroup(), 0, ui::ShapeEditorModel::kGroups - 1);
            name = act == 0 ? "Edit" : (act == 1 ? "Grid" : "Trigger");
            // EDIT = the virtual targets 0 .. 3 (POINT, X, Y, CURVE), GRID = 4 .. 6 (GRID X, GRID Y, SNAP), TRIGGER = MODE and PHASE (the parameters of the table) and the virtual 7 (BEND ALL); the active one is on Q 1 .. 4, the others after it
            const int modeP = paramOf ("sync_type"), phaseP = paramOf ("phase");
            qSlots_.publish (v, ui::ShapeEditorModel::kGroups, act, [modeP, phaseP] (int g, int* t)
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
        }
        else if (pageTop_ == kPagePointMod)
        {
            name = "Modulation";
            // ONE group, always the active one: Q 1 = MODULATE (virtual 8), Q 2 = AMOUNT (virtual 9); the editor's own targets come back with the editor page (the slots are published again at the first update there)
            qSlots_.publish (v, pointMod_.fields[ui::PointModModel::kTarget].enabled ? 1 : 0, 0, [this] (int, int* t)
            {
                t[0] = QRouter::encode (kVirtPmTarget, QRouter::kVirtual);
                t[1] = pointModSelection().mix == ui::PointSelectionMod::MixedSource
                    ? -1 : QRouter::encode (kVirtPmAmount, QRouter::kVirtual);
                t[2] = t[3] = -1;
            });
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
            const ModRowsPage& view = tg ? targetsView_ : modsView_;
            const int n = view.numRows(), act = view.activeRow();
            qSlots_.publish (v, n, act, [&] (int row, int* t) { modRowTargets (row >= 0 && row < ModRowsPage::kPerPage ? rows[row].slot : -1, t); });
        }
        else if (pageTop_ == kPageList)
        {
            name = "List";
            qSlots_.publish (v, 0, 0, [] (int, int*) {});
        }
        else
        {
            buildGroups();
            const int act = activeGroup();
            name = act >= 0 && act < nGroups_ ? groups_[act].name : "";
            int ids[kMaxGroups], used = 0, pos = -1;
            for (int g = 0; g < nGroups_; ++g)
                if (groups_[g].numMembers() > 0 && ! (g == keyGroup_ && ! keyActive_))   // the KEY block is there only with the key follow sync
                {
                    if (g == act)
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
                    const GroupDef& group = groups_[ids[k]];
                    int next = first;
                    for (int i = 0; i < 4 && first + i < QRouter::kCount; ++i)
                        if (group.member[i] >= 0)
                        {
                            targets[first + i] = paramOfMember (group.member[i]);
                            next = first + i + 1;
                        }
                    first = next;
                }
                qSlots_.publishRaw (v, targets);
            }
            else
            {
                int targets[QRouter::kCount];
                QRouter::mapGroups (used, pos, [&] (int k, int* t) { for (int i = 0; i < 4; ++i) t[i] = paramOfMember (groups_[ids[k]].member[i]); }, targets);
                qSlots_.publishRaw (v, targets);
            }
        }
        ui::setText (qName_, sizeof qName_, name);
    }

    // ======================================================================================================================================================================
    // the parts of the pages the host cannot draw: the texts and frames of the ground, the canvas, the menu chrome
    void drawEditor (gpu::SceneBuilder& sb) const
    {
        editor_.canvas.draw (sb);
        ui::Pen p (sb, true);
        ui::Cell cell (sb, { 0, kTabY, w_, kTabH });
        const ui::theme::Look& T = ui::theme::L();
        const ui::Rect titleEdit = editor_.titleRect (0);
        if (! titleEdit.empty())
            p.textLeft (titleEdit, "SELECTED POINT", T.fLabel - 1.0f, p.c (T.textDim, 255), T.fMin);
        if (editor_.canvas.selection().count() == 0 && ! titleEdit.empty())
        {
            const ui::Rect hint { titleEdit.x + 170, titleEdit.y, std::max (0, titleEdit.w - 170), titleEdit.h };
            p.textRight (hint, "TAP A POINT OR TURN Q1", T.fLabel - 2.0f, p.c (T.textFaint, 255), T.fMin);
        }
        p.textLeft (editor_.titleRect (1), "GRID", T.fLabel - 1.0f, p.c (T.textDim, 255), T.fMin);
        p.textLeft (editor_.titleRect (2), "TRIGGER", T.fLabel - 1.0f, p.c (T.textDim, 255), T.fMin);
        const ui::Rect cr = editor_.canvasRect();
        p.textRight ({ cr.right() - 8 - 220, cr.y + 6, 220, 18 }, editor_.info(), T.fLabel - 3.0f, p.c (T.textFaint, 255), T.fMin);
        if (editor_.status()[0] != '\0')
            p.textLeft ({ cr.x + 14, cr.y + 6, std::max (0, cr.w - 260), 22 }, editor_.status(), T.fLabel - 2.0f, p.c (T.flow[0], 255), T.fMin);
    }
    void drawPointMod (gpu::SceneBuilder& sb) const
    {
        ui::Pen p (sb, true);
        ui::Cell cell (sb, { 0, kTabY, w_, kTabH });
        const ui::theme::Look& T = ui::theme::L();
        const int y = kTabY + 8 + ui::kQLane;
        p.textLeft ({ 640, y, std::max (0, w_ - 640 - 8), 56 }, pointMod_.summary(), T.fLabel + 2.0f, p.c (T.text, 255), T.fMin);
    }
    /** The frames and the captions of the full screen of the Play page's expansion: the display block, the blocks of the bottom band with their titles. */
    void drawFullFurniture (gpu::SceneBuilder& sb) const
    {
        ui::Pen p (sb, true);
        ui::Cell cell (sb, fullG_.tab);
        const ui::theme::Look& T = ui::theme::L();
        if (fullG_.edit.empty())
            p.block (fullG_.display);
        buildGroups();
        const int act = activeGroup();
        const int activeBlock = act >= 0 && act < nGroups_ ? groups_[act].block : -1;
        for (int i = 0; i < fullG_.nBlocks; ++i)
            p.rrect (fullG_.blocks[i], T.rBlock, p.c (ui::colour::rgba (i == activeBlock ? T.keylineHi : T.keyline, 255)), T.frame);
        p.textLeft ({ fullG_.tab.x + 8 + 84 + lfolayout::kGap, fullG_.tab.y + 2 + ui::kQLane, 56, 56 }, "LFO", T.fLabel + 2.0f, p.c (T.textDim, 255), T.fMin);
        static const char* const kShape[3] = { "RATE", "TIMING", "MOTION" }, * const kRandom[1] = { "RANDOM" };
        for (int i = 0; i < fullG_.nBlocks; ++i)
        {
            const char* title = random() ? kRandom[i] : kShape[i];
            if (i == 0 && keyActive_)
                title = random() ? "RANDOM - RATE FOLLOWS THE KEY" : "RATE - FOLLOWS THE KEY";
            p.textLeft ({ fullG_.blocks[i].x + 10, fullG_.blocks[i].y + 4, std::max (0, fullG_.blocks[i].w - 20), 18 }, title, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
        }
    }
    /** The dimmed ground and the frame of the list menu (the rows and the header are native cells of the host). */
    void drawMenuChrome (gpu::SceneBuilder& sb) const
    {
        const ui::theme::Look& T = ui::theme::L();
        ui::Pen p (sb, true);
        ui::Cell cell (sb, menu_.parent);
        p.rect (menu_.parent, ui::colour::rgba (T.bg1, 190));
        const ui::Rect b = menu_.box();
        p.block (b, true);
        p.rrect (b, T.rBlock, p.c (T.flow[0], 255), 1.6f);
        if (menu_.scrolls())
        {
            const ui::Rect th = menu_.thumbRect();
            p.rrect ({ th.x, b.y + lfolayout::Menu::kPad + menu_.headerH(), lfolayout::Menu::kBarW, b.h - 2 * lfolayout::Menu::kPad - menu_.headerH() }, 5.0f, p.c (T.track, 255));
            p.paintY2 (static_cast<float> (th.y), static_cast<float> (th.bottom()), T.flow[0], T.flow[3], 255);
            p.rrect (th, 5.0f, 255);
            p.paintOff();
        }
    }

    // ======================================================================================================================================================================
    enum class MenuTouch : uint8_t { None, Backdrop, Zone, Box };
    MenuTouch menuMode_ = MenuTouch::None;
    int menuX0_ = 0, menuY0_ = 0, menuFirst0_ = 0;
    lfolayout::Full fullG_;
    lfolayout::Compact compG_;
    uint32_t geomKey_ = ~0u;
    ui::ShapeEditorModel editor_;
    ui::PointModModel pointMod_;
    ui::TextEntry entry_;
    ModRowsPage targetsView_ { ModRowsPage::Kind::Targets }, modsView_ { ModRowsPage::Kind::Modulations };
    lfolayout::Menu menu_;
    LfoShapeLibrary lib_;
    binding::SelectorSet selector_;
    UiStack idle_;                       // an empty stack: the pages are read from it while another element's page is open
    std::vector<Item> items_;
    Gesture gesture_ = Gesture::None;
    bool gestureDown_ = false, virtualUndoActive_ = false, configured_ = false, modsDirty_ = true, shapeDirty_ = false, editorContentExternal_ = false, editorSentValid_ = false, haveCurve_ = false, displayHot_ = false;
    bool nativePage_ = false, nativeSmooth_ = false, editShown_ = false, keyActive_ = false, smoothOn_ = true;
    int nativeTargets_[nativeui::lfo::kCount] {};
    uint32_t nativeFields_ = 0;
    bool nativePublished_ = false;
    bool compactNativeKey_ = false;
    int compactBlock_ = -1;
    int sync_ = 0;
    ui::lfo::SyncMode syncMode_ = ui::lfo::SyncMode::Hz;
    uint64_t nativeEditorShape_ = 0;
    nativeui::lfoeditor::State nativeEditorState_ {};
    nativeui::lfodetail::View nativeDetailView_ = nativeui::lfodetail::Hidden;
    int pickGroup_ = 0;      // the group chosen by ADD TARGET (0 none, 1 ..): the view state of the choice
    bool pickShown_ = false;
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
