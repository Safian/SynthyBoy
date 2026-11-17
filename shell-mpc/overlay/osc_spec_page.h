// SynthyBoy - MPC overlay: the DETAIL PAGES of the oscillator types that hold the parameters the full screen has no room for (docs/MPC.md "Oscillator UI", table "type -> page -> block -> controls").
// A page is DATA (a Spec: title, whether the display of the type is above it, blocks, controls with the key of the parameter, the state in which the control is shown); `OscSpecPage` is the one
// component that lays any spec out: titled blocks (each one Q group of up to four controls in reading order), a control that does not apply in the current state is hidden and its room goes to the
// others, a block with nothing to show is left out. Pure: no OS headers; the owner (OscUi) binds the controls to the parameters and says which controls are visible.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>

#include "../ui/ui.h"

namespace synthyboy_mpc { namespace overlay { namespace oscspec {

enum class K : uint8_t { Knob, Stepper, Toggle, Number, List, Mask };   // List: a LedList (a short list, all options visible); Mask: the twelve notes of the transpose snap (NoteMask)
/** The state a control is shown in (Always: every state); the owner evaluates them from the parameters of the oscillator. */
enum class Cond : uint8_t { Always, LoopOn, SliceAuto, SliceManual, SliceAny, KeytrackOn, DensSync, DensGrains, LenSync, LenPercent, RemapOn, FltOn, SpecYAxis, GranYAxis, TrackOff, TempoLen, Count };
struct Ctl { K kind; const char* label; const char* key; Cond cond; uint8_t weight; };
struct Block { const char* title; uint8_t row, first, n; };
struct Spec
{
    int page;               // the id of the page in the UiStack
    const char* title;
    bool display;           // the picture of the type above the blocks (at least 240 px)
    const Block* blocks; int numBlocks;
    const Ctl* ctls; int numCtls;
    int rows;               // rows of blocks
};

enum PageId { kWtMap = 16, kMore = 17, kSampleRegion = 18, kSampleSlice = 19, kGrain = 20, kSpecFilter = 21, kSpecScan = 22, kFirstPage = 16, kLastPage = 22 };

// ---- Wavetable: MAP (the remap curve of the frame position and the spectral morph)
constexpr Ctl kWtMapCtls[] = {
    { K::Toggle, "REMAP", "remap.on", Cond::Always, 1 }, { K::List, "CURVE", "remap.slot", Cond::RemapOn, 3 }, { K::Knob, "RISE", "remap.rise", Cond::RemapOn, 1 }, { K::Knob, "FALL", "remap.fall", Cond::RemapOn, 1 },
    { K::Stepper, "TYPE", "morph.type", Cond::Always, 2 }, { K::Knob, "AMOUNT", "morph.amount", Cond::Always, 1 }, { K::Knob, "SPREAD", "morph.spread", Cond::Always, 1 },
};
constexpr Block kWtMapBlocks[] = { { "FRAME REMAP", 0, 0, 4 }, { "SPECTRAL MORPH", 1, 4, 3 } };
// ---- every type: MORE (switches, pitch, phase, the key and velocity zone, the unison law)
constexpr Ctl kMoreCtls[] = {
    { K::Toggle, "ON", "on", Cond::Always, 1 }, { K::Toggle, "ENV 1", "env1", Cond::Always, 1 }, { K::Toggle, "SMOOTH", "smooth_interp", Cond::Always, 1 }, { K::Toggle, "TRACK", "pitch_track", Cond::Always, 1 },
    { K::List, "MODE", "pitch_mode", Cond::Always, 3 }, { K::Toggle, "BEND", "pitch_bend_track", Cond::Always, 1 }, { K::Knob, "FIXED", "fixed_pitch", Cond::TrackOff, 1 },
    { K::List, "MEMORY", "phase_memory", Cond::Always, 3 }, { K::Toggle, "LEGATO", "phase_legato", Cond::Always, 1 }, { K::List, "RATIO", "ratio_src", Cond::Always, 4 },
    { K::Number, "LOW", "map.key_low", Cond::Always, 1 }, { K::Number, "HIGH", "map.key_high", Cond::Always, 1 }, { K::Toggle, "FOLD", "map.key_fold", Cond::Always, 1 }, { K::Toggle, "WARP", "map.key_warp", Cond::Always, 1 },
    { K::Number, "LOW", "map.vel_low", Cond::Always, 1 }, { K::Number, "HIGH", "map.vel_high", Cond::Always, 1 }, { K::Toggle, "OVERRIDE", "multi.vel_override", Cond::Always, 1 },
    { K::Mask, "NOTES", "transpose_snap", Cond::Always, 7 }, { K::Toggle, "SNAP NOTE", "transpose_snap", Cond::Always, 2 },
    { K::Knob, "CURVE", "unison.detune_curve", Cond::Always, 2 }, { K::Knob, "SHAPE", "unison.blend_shape", Cond::Always, 2 }, { K::List, "LAW", "unison.law", Cond::Always, 3 }, { K::List, "STACK", "unison.stack_repeat", Cond::Always, 3 },
};
constexpr Block kMoreBlocks[] = { { "OSCILLATOR", 0, 0, 4 }, { "PITCH", 0, 4, 3 }, { "PHASE", 1, 7, 3 }, { "KEY ZONE", 1, 10, 4 }, { "VELOCITY ZONE", 1, 14, 3 }, { "TRANSPOSE SNAP", 2, 17, 2 }, { "UNISON SHAPE", 2, 19, 4 } };
// ---- Sample: REGION (region, loop, edit) and SLICE (slices, playback, scan, tempo)
constexpr Ctl kSampleRegionCtls[] = {
    { K::Knob, "START", "sample.start", Cond::Always, 1 }, { K::Knob, "END", "sample.end", Cond::Always, 1 }, { K::Number, "ROOT", "sample.root", Cond::KeytrackOn, 1 }, { K::Toggle, "KEY TRACK", "sample.keytrack", Cond::Always, 1 },
    { K::Stepper, "MODE", "sample.loop_mode", Cond::Always, 2 }, { K::Knob, "START", "sample.loop_start", Cond::LoopOn, 1 }, { K::Knob, "END", "sample.loop_end", Cond::LoopOn, 1 }, { K::Knob, "XFADE", "sample.loop_xfade", Cond::LoopOn, 1 },
    { K::Toggle, "RELATIVE", "sample.loop_relative", Cond::LoopOn, 1 }, { K::Toggle, "LINK", "sample.loop_link", Cond::LoopOn, 1 }, { K::Toggle, "EXIT", "sample.loop_exit", Cond::LoopOn, 1 },
    { K::Stepper, "FADE", "sample.fade_edges", Cond::Always, 2 }, { K::Toggle, "NORMALIZE", "sample.normalize", Cond::Always, 1 }, { K::Toggle, "REVERSE", "sample.reverse", Cond::Always, 1 },
};
constexpr Block kSampleRegionBlocks[] = { { "REGION", 0, 0, 4 }, { "LOOP", 0, 4, 4 }, { "LOOP OPTIONS", 1, 8, 3 }, { "EDIT", 1, 11, 3 } };
constexpr Ctl kSampleSliceCtls[] = {
    { K::List, "MODE", "sample.slice_mode", Cond::Always, 3 }, { K::Knob, "THRESHOLD", "sample.slice_threshold", Cond::SliceAuto, 1 }, { K::Knob, "SLICE", "sample.slice", Cond::SliceManual, 1 },
    { K::Stepper, "PLAY", "sample.slice_play", Cond::SliceAny, 2 }, { K::Number, "ROOT", "sample.slice_root", Cond::SliceAny, 1 },
    { K::List, "RANGE", "sample.scan_range", Cond::Always, 3 }, { K::Toggle, "REVERSE", "sample.scan_reverse", Cond::Always, 1 },
    { K::Toggle, "LOCK", "sample.scan_lock_tempo", Cond::Always, 1 }, { K::Toggle, "TO BPM", "sample.scan_to_bpm", Cond::Always, 1 }, { K::Knob, "BEATS", "sample.length_beats", Cond::TempoLen, 1 },
};
constexpr Block kSampleSliceBlocks[] = { { "SLICE", 0, 0, 3 }, { "PLAYBACK", 0, 3, 2 }, { "SCAN", 1, 5, 2 }, { "TEMPO", 1, 7, 3 } };
// ---- Granular: GRAIN (density, length, spawn, random warp, position, y axis)
constexpr Ctl kGrainCtls[] = {
    { K::List, "MODE", "gran.density_mode", Cond::Always, 3 }, { K::Stepper, "DIVISION", "gran.density_sync", Cond::DensSync, 2 }, { K::Knob, "GRAINS", "gran.density_grains", Cond::DensGrains, 1 },
    { K::List, "MODE", "gran.length_mode", Cond::Always, 3 }, { K::Stepper, "DIVISION", "gran.length_sync", Cond::LenSync, 2 }, { K::Knob, "PERCENT", "gran.length_percent", Cond::LenPercent, 1 },
    { K::List, "PATTERN", "gran.spawn_pattern", Cond::Always, 3 }, { K::Knob, "MAX", "gran.max_grains", Cond::Always, 1 }, { K::Toggle, "JUMP", "gran.jump_start", Cond::Always, 1 }, { K::Toggle, "REVERSE", "gran.reverse_grains", Cond::Always, 1 },
    { K::Knob, "WARP 1", "gran.rand_warp", Cond::Always, 1 }, { K::Knob, "WARP 2", "gran.rand_warp2", Cond::Always, 1 },
    { K::Toggle, "MANUAL", "gran.manual", Cond::Always, 1 }, { K::Toggle, "KEY TRACK", "gran.scan_keytrack", Cond::Always, 1 }, { K::Knob, "XFADE", "sample.loop_xfade", Cond::Always, 1 },
    { K::Stepper, "AXIS", "gran.y_axis", Cond::Always, 2 }, { K::Knob, "Y", "gran.xy_y", Cond::GranYAxis, 1 },
};
constexpr Block kGrainBlocks[] = { { "DENSITY", 0, 0, 3 }, { "LENGTH", 0, 3, 3 }, { "SPAWN", 0, 6, 4 }, { "RANDOM WARP", 1, 10, 2 }, { "POSITION", 1, 12, 3 }, { "Y AXIS", 1, 15, 2 } };
// ---- Spectral: FILTER (band, filter, mask) and SCAN (scan, morph, y axis)
constexpr Ctl kSpecFilterCtls[] = {
    { K::Knob, "LOW", "spec.freq_lo", Cond::Always, 1 }, { K::Knob, "HIGH", "spec.freq_hi", Cond::Always, 1 }, { K::Toggle, "SMOOTH", "spec.lohi_smooth", Cond::Always, 1 }, { K::Toggle, "AFTER WARP", "spec.lohi_post_warp", Cond::Always, 1 },
    { K::Knob, "CUT", "spec.cut", Cond::Always, 1 }, { K::Knob, "MIX", "spec.mix", Cond::Always, 1 }, { K::Knob, "SHIFT", "spec.freq_shift", Cond::Always, 1 },
    { K::Toggle, "WAVETABLE", "spec.flt_use", Cond::Always, 1 }, { K::Stepper, "TABLE", "spec.flt_table", Cond::FltOn, 2 }, { K::Knob, "FRAME", "spec.flt_frame", Cond::FltOn, 1 },
};
constexpr Block kSpecFilterBlocks[] = { { "BAND", 0, 0, 4 }, { "FILTER", 0, 4, 3 }, { "FILTER FROM WAVETABLE", 1, 7, 3 } };
constexpr Ctl kSpecScanCtls[] = {
    { K::Toggle, "MANUAL", "spec.manual", Cond::Always, 1 }, { K::Toggle, "KEY TRACK", "spec.scan_keytrack", Cond::Always, 1 }, { K::Toggle, "PHASE LOCK", "spec.phase_lock", Cond::Always, 1 }, { K::Toggle, "TRANSIENTS", "spec.transients", Cond::Always, 1 },
    { K::Stepper, "TYPE", "morph.type", Cond::Always, 2 }, { K::Knob, "AMOUNT", "morph.amount", Cond::Always, 1 }, { K::Knob, "SPREAD", "morph.spread", Cond::Always, 1 },
    { K::Stepper, "AXIS", "spec.y_axis", Cond::Always, 2 }, { K::Knob, "Y", "spec.xy_y", Cond::SpecYAxis, 1 },
};
constexpr Block kSpecScanBlocks[] = { { "SCAN", 0, 0, 4 }, { "SPECTRAL MORPH", 0, 4, 3 }, { "Y AXIS", 1, 7, 2 } };

#define SB_SPEC(arr) arr, static_cast<int> (sizeof arr / sizeof arr[0])
constexpr Spec kSpecs[] = {
    { kWtMap, "FRAME MAP", true, SB_SPEC (kWtMapBlocks), SB_SPEC (kWtMapCtls), 2 },
    { kMore, "OSCILLATOR OPTIONS", false, SB_SPEC (kMoreBlocks), SB_SPEC (kMoreCtls), 3 },
    { kSampleRegion, "SAMPLE REGION", true, SB_SPEC (kSampleRegionBlocks), SB_SPEC (kSampleRegionCtls), 2 },
    { kSampleSlice, "SAMPLE SLICES AND SCAN", true, SB_SPEC (kSampleSliceBlocks), SB_SPEC (kSampleSliceCtls), 2 },
    { kGrain, "GRAIN ENGINE", true, SB_SPEC (kGrainBlocks), SB_SPEC (kGrainCtls), 2 },
    { kSpecFilter, "SPECTRAL FILTER", true, SB_SPEC (kSpecFilterBlocks), SB_SPEC (kSpecFilterCtls), 2 },
    { kSpecScan, "SPECTRAL SCAN", true, SB_SPEC (kSpecScanBlocks), SB_SPEC (kSpecScanCtls), 2 },
};
#undef SB_SPEC
inline const Spec* specOf (int page) noexcept { for (const Spec& s : kSpecs) if (s.page == page) return &s; return nullptr; }
inline bool isSpecPage (int page) noexcept { return specOf (page) != nullptr; }

// ===============================================================================================================================================================
class OscSpecPage : public ui::Composite
{
public:
    static constexpr int kBarH = 64, kDisplayH = 244, kMaxCtl = 24, kMaxBlocks = 8, kBlockGap = 8, kBlockPad = 4, kTitleH = 32, kMaxExtra = 60;
    ui::MenuButton back { "BACK", ui::MenuButton::Icon::None };
    ui::MenuButton load { "LOAD SAMPLE", ui::MenuButton::Icon::None };   // the empty state of a type with a file: one tap to the browser (the owner shows it, places it over the display)
    ui::QGroup groups[kMaxBlocks];

    OscSpecPage() { addKid (back); addKid (load, false); }

    /** Takes the spec: the controls come back to their defaults (labels from the table), all shown. */
    void loadSpec (const Spec& s)
    {
        spec_ = &s;
        clearKids();
        addKid (back);
        addKid (load, loadShown_);
        for (int i = 0; i < s.numCtls && i < kMaxCtl; ++i)
            addKid (*compOf (i), false);
        for (int i = 0; i < kMaxCtl; ++i)
        {
            vis_[i] = i < s.numCtls;
            if (i >= s.numCtls)
                continue;
            const Ctl& c = s.ctls[i];
            switch (c.kind)
            {
                case K::Knob: { ui::Knob& k = knobs_[i]; k.state = ui::Knob::State {}; k.setLabel (c.label); break; }
                case K::Stepper: { ui::Stepper& st = steppers_[i]; st.state = ui::Stepper::State {}; st.setCaption (c.label); st.setItem (c.label); st.minWidth = 100; break; }
                case K::Toggle: { ui::LedToggle& t = toggles_[i]; t.state = ui::ToggleIcon::State {}; t.state.icon = ui::ToggleIcon::Icon::Generic; t.setLabel (c.label); break; }
                case K::List: { ui::LedList& l = lists_[i]; l.state = ui::LedList::State {}; l.setTitle (c.label); l.state.titleInLane = true; break; }
                case K::Mask: { ui::NoteMask& m = masks_[i]; m.state = ui::NoteMask::State {}; m.setLabel (c.label); break; }
                case K::Number: { ui::NumberField& n = numbers_[i]; n.state = ui::NumberField::State {}; n.state.format = ui::NumberField::Format::IntPlain; n.setLabel (c.label); break; }
            }
        }
        relayout();
    }
    const Spec* spec() const noexcept { return spec_; }
    /** The options of a List control (names, at most 8); the page lays out again with them (the lines of a list decide the height of its block). */
    void setListOptions (int i, const char* const* names, int n)
    {
        if (spec_ != nullptr && i >= 0 && i < spec_->numCtls && spec_->ctls[i].kind == K::List)
        {
            lists_[i].setOptions (names, n);
            lists_[i].state.index = std::clamp (lists_[i].state.index, 0, std::max (0, n - 1));
        }
    }
    /** The title of a List control (where the shown label differs from the table's). */
    void setListTitle (int i, const char* t)
    {
        if (spec_ != nullptr && i >= 0 && i < spec_->numCtls && spec_->ctls[i].kind == K::List)
            lists_[i].setTitle (t);
    }
    void relayoutNow() { layout(); }
    ui::Component* control (int i) noexcept { return i >= 0 && spec_ != nullptr && i < spec_->numCtls ? compOf (i) : nullptr; }
    bool visible (int i) const noexcept { return spec_ != nullptr && i >= 0 && i < spec_->numCtls && vis_[i]; }
    /** The owner says which controls apply now; the page is laid out again when the set changed. */
    void setVisible (const bool* v)
    {
        if (spec_ == nullptr)
            return;
        bool changed = false;
        for (int i = 0; i < spec_->numCtls; ++i)
            if (vis_[i] != v[i]) { vis_[i] = v[i]; changed = true; }
        if (changed)
            relayout();
    }
    ui::Rect displayRect() const noexcept { return display_; }
    ui::Rect blockRect (int b) const noexcept { return b >= 0 && b < kMaxBlocks ? blockRect_[b] : ui::Rect {}; }
    /** The blocks that are on the page now (a block with no visible control is left out), the Q groups are theirs in this order. */
    int numGroups() const noexcept { return nGroups_; }
    const char* groupTitle (int g) const noexcept { return g >= 0 && g < nGroups_ ? spec_->blocks[groupBlock_[g]].title : ""; }
    int activeGroup() const noexcept { return active_; }
    void setActiveGroup (int g)
    {
        for (int k = 0; k < nGroups_; ++k)
            groups[k].setActive (k == g);
        active_ = g >= 0 && g < nGroups_ ? g : -1;
    }
    ui::Change turnQ (int knob, double delta) { return active_ >= 0 ? groups[active_].turn (knob, delta) : ui::Change::none(); }
    void setLoadShown (bool s)
    {
        if (loadShown_ == s)
            return;
        loadShown_ = s;
        show (load, s);
        layout();
    }
    bool loadShown() const noexcept { return loadShown_; }
    ui::Size minSize() const override { return { 1280, 628 }; }

    ui::Change touchDown (int x, int y, double nowMs) override
    {
        const ui::Change c = Composite::touchDown (x, y, nowMs);
        if (cap_)
            for (int i = 0; i < nGroups_; ++i)
                if (blockRect_[groupBlock_[i]].contains (x, y))
                {
                    setActiveGroup (i);
                    break;
                }
        return c;
    }
    void draw (gpu::SceneBuilder& sb) const override
    {
        // the blocks, the controls, the chips of the Q groups, and the titles last (a chip drawn after a title would count as part of the title's cache cell)
        ui::Pen pen (sb, enabled_);
        {
            ui::Cell cell (sb, rect_);
            drawUnder (pen);
        }
        for (int i = 0; i < n_; ++i)
            if (shown_[i])
                kids_[i]->draw (sb);
        for (int i = 0; i < nGroups_; ++i)
            groups[i].draw (sb);
        for (int i = 0; i < nGroups_; ++i)
            if (i != active_)
                groups[i].drawMapped (sb);
        ui::Cell cell (sb, rect_);
        drawExtra (pen);
    }

protected:
    ui::Component* compOf (int i) noexcept
    {
        // every index has all four component kinds; the spec says which is the real one
        if (spec_ != nullptr && i < spec_->numCtls)
            switch (spec_->ctls[i].kind)
            {
                case K::Knob: return &knobs_[i];
                case K::Stepper: return &steppers_[i];
                case K::Toggle: return &toggles_[i];
                case K::Number: return &numbers_[i];
                case K::List: return &lists_[i];
                case K::Mask: return &masks_[i];
            }
        return &knobs_[i];
    }
    void layout() override
    {
        back.place ({ rect_.x + 4, rect_.y + 4, 120, kBarH - 8 });
        display_ = spec_ != nullptr && spec_->display ? ui::Rect { rect_.x + 4, rect_.y + kBarH + 4, rect_.w - 8, kDisplayH } : ui::Rect {};
        load.place (display_.empty() ? ui::Rect {} : ui::Rect { display_.x + display_.w / 2 - 130, display_.y + display_.h / 2 - 32, 260, 64 });
        if (spec_ == nullptr)
            return;
        const int top = rect_.y + kBarH + (display_.empty() ? 8 : kDisplayH + 12), bottom = rect_.bottom() - 4;
        const int rows = std::max (1, spec_->rows);
        // which controls and blocks are on the page
        int nvis[kMaxBlocks] = {}, wsum[kMaxBlocks] = {};
        nGroups_ = 0;
        for (int b = 0; b < spec_->numBlocks && b < kMaxBlocks; ++b)
        {
            const Block& bl = spec_->blocks[b];
            for (int k = 0; k < bl.n; ++k)
                if (vis_[bl.first + k]) { ++nvis[b]; wsum[b] += spec_->ctls[bl.first + k].weight; }
            blockRect_[b] = {};
        }
        for (int i = 0; i < spec_->numCtls; ++i)
            show (*compOf (i), false);
        // the rows of blocks: only rows that have a block are given room
        int usedRow[4] = {}, nUsedRows = 0;
        for (int r = 0; r < rows && r < 4; ++r)
            for (int b = 0; b < spec_->numBlocks; ++b)
                if (spec_->blocks[b].row == r && nvis[b] > 0) { usedRow[r] = 1; break; }
        for (int r = 0; r < rows && r < 4; ++r)
            nUsedRows += usedRow[r];
        // pass 1: the x and width of every block of a row, and what height its controls need (a short list needs a row of 56 px per line of options)
        int rowNeed[4] = {}, rowTop[4] = {}, rowHeight[4] = {};
        for (int r = 0; r < rows && r < 4; ++r)
        {
            if (! usedRow[r])
                continue;
            int total = 0, count = 0;
            for (int b = 0; b < spec_->numBlocks; ++b)
                if (spec_->blocks[b].row == r && nvis[b] > 0) { total += std::max (2, wsum[b]) + 1; ++count; }
            const int avail = rect_.w - 8 - kBlockGap * (count - 1);
            int x = rect_.x + 4, used = 0;
            for (int b = 0; b < spec_->numBlocks; ++b)
            {
                if (spec_->blocks[b].row != r || nvis[b] == 0)
                    continue;
                const int before = avail * used / total;
                used += std::max (2, wsum[b]) + 1;
                const int w = avail * used / total - before;
                blockRect_[b] = { x, 0, w, 0 };
                x += w + kBlockGap;
                rowNeed[r] = std::max (rowNeed[r], needOf (b, w) + kTitleH + kBlockPad);
            }
        }
        int sumNeed = 0;
        for (int r = 0; r < rows && r < 4; ++r)
            sumNeed += usedRow[r] ? rowNeed[r] : 0;
        const int surplus = std::max (0, bottom - top - sumNeed - kBlockGap * std::max (0, nUsedRows - 1));
        const int extra = nUsedRows > 0 ? std::min (surplus / nUsedRows, kMaxExtra) : 0;
        int yy = top;
        for (int r = 0; r < rows && r < 4; ++r)
            if (usedRow[r])
            {
                rowTop[r] = yy;
                rowHeight[r] = rowNeed[r] + extra;
                yy += rowHeight[r] + kBlockGap;
            }
        // pass 2: the blocks get their height and place their controls
        for (int b = 0; b < spec_->numBlocks && b < kMaxBlocks; ++b)
            if (nvis[b] > 0)
            {
                const int r = std::min<int> (spec_->blocks[b].row, 3);
                blockRect_[b].y = rowTop[r];
                blockRect_[b].h = rowHeight[r];
                layoutBlock (b);
            }
        // the Q groups: one per block on the page, the visible controls in reading order
        for (ui::QGroup& g : groups)
            for (int i = 0; i < 4; ++i)
                g.set (i, nullptr);
        nGroups_ = 0;
        for (int b = 0; b < spec_->numBlocks && b < kMaxBlocks; ++b)
        {
            if (nvis[b] == 0)
                continue;
            const Block& bl = spec_->blocks[b];
            int m = 0;
            for (int k = 0; k < bl.n && m < 4; ++k)
                if (vis_[bl.first + k])
                    groups[nGroups_].set (m++, compOf (bl.first + k));
            groupBlock_[nGroups_++] = b;
        }
        const int keep = active_ >= 0 && active_ < nGroups_ ? active_ : (nGroups_ > 0 ? 0 : -1);
        active_ = -1;
        setActiveGroup (keep);
    }
    bool blockHasVisible (int b) const noexcept
    {
        const Block& bl = spec_->blocks[b];
        for (int k = 0; k < bl.n; ++k)
            if (vis_[bl.first + k])
                return true;
        return false;
    }
    /** The columns a short list of n options takes in a cell of width w (the most that keep the columns as wide as the longest name needs) and so the lines it has. */
    static int listColumns (const ui::LedList& l, int n, int w) noexcept
    {
        int best = 1;
        for (int c = 2; c <= std::min (ui::LedList::kMaxColumns, n); ++c)
            if (w / c >= l.columnNeed())
                best = c;
        return best;
    }
    /** The cells of the visible controls of block b (indices in idx, n of them) in a block of width w. */
    void cellsOf (int b, int w, int* idx, int& n, ui::Rect* cells) const
    {
        const Block& bl = spec_->blocks[b];
        n = 0;
        for (int k = 0; k < bl.n && n < 8; ++k)
            if (vis_[bl.first + k])
                idx[n++] = bl.first + k;
        int fixed[8] = {}, weight[8] = {};
        for (int k = 0; k < n; ++k)
            weight[k] = spec_->ctls[idx[k]].weight;
        ui::layoutRow ({ 0, 0, std::max (0, w - 2 * kBlockPad), 100 }, fixed, weight, n, ui::kGap, cells);
    }
    int needOf (int b, int w) const
    {
        int idx[8], n;
        ui::Rect cells[8];
        cellsOf (b, w, idx, n, cells);
        int need = 0;
        for (int k = 0; k < n; ++k)
        {
            const int ci = idx[k];
            switch (spec_->ctls[ci].kind)
            {
                case K::Knob: need = std::max (need, 84); break;
                case K::Stepper: case K::Number: need = std::max (need, 64); break;
                case K::Toggle: need = std::max (need, 56); break;
                case K::Mask: need = std::max (need, 120); break;
                case K::List:
                {
                    const int cnt = std::max (1, lists_[ci].state.count), c = listColumns (lists_[ci], cnt, cells[k].w);
                    need = std::max (need, ui::led::kRowH * ((cnt + c - 1) / c));
                    break;
                }
            }
        }
        return need;
    }
    void layoutBlock (int b)
    {
        const ui::Rect br = blockRect_[b];
        const ui::Rect in { br.x + kBlockPad, br.y + kTitleH, std::max (0, br.w - 2 * kBlockPad), std::max (0, br.h - kTitleH - kBlockPad) };
        int idx[8], n;
        ui::Rect cells[8];
        cellsOf (b, br.w, idx, n, cells);
        // one row of controls per block (a Q group is one row: its chips sit on the lane above the first row), all with the same top
        for (int k = 0; k < n; ++k)
        {
            const int ci = idx[k];
            const Ctl& c = spec_->ctls[ci];
            ui::Rect cr = cells[k];
            cr.x += in.x; cr.y = in.y; cr.h = in.h;
            int cap = 0;
            switch (c.kind)
            {
                case K::Stepper: case K::Number: cap = 88; break;
                case K::Mask: cap = 120; break;
                case K::List:
                {
                    const int cnt = std::max (1, lists_[ci].state.count), cols = listColumns (lists_[ci], cnt, cr.w);
                    cap = ui::led::kRowH * ((cnt + cols - 1) / cols);
                    break;
                }
                default: break;
            }
            if (cap > 0 && cr.h > cap && n > 1)   // a lone control fills its block
                cr.h = cap;
            if (c.kind == K::Knob)
                knobs_[ci].state.side = cr.w >= 172;
            show (*compOf (ci), true);
            compOf (ci)->place (cr);
        }
    }
    void drawUnder (ui::Pen& p) const override
    {
        if (! display_.empty())
            p.block (display_);
        for (int b = 0; b < kMaxBlocks; ++b)
            if (! blockRect_[b].empty())
                p.block (blockRect_[b]);
    }
    void drawExtra (ui::Pen& p) const override
    {
        const ui::theme::Look& T = ui::theme::L();
        p.rect (rect_.x + 4, rect_.y + kBarH - 1, rect_.right() - 4, rect_.y + kBarH, p.c (T.keyline, 255));
        if (spec_ != nullptr)
            p.textLeft ({ rect_.x + 140, rect_.y + 4, std::max (0, rect_.w - 150), kBarH - 8 }, spec_->title, T.fTitle, p.c (T.text, 255), T.fMin);
        for (int b = 0; b < kMaxBlocks && spec_ != nullptr && b < spec_->numBlocks; ++b)
            if (! blockRect_[b].empty())
                p.textLeft ({ blockRect_[b].x + kBlockPad + 4, blockRect_[b].y + 3, std::max (0, blockRect_[b].w - 2 * kBlockPad - 4), 16 }, spec_->blocks[b].title, T.fLabel - 2.0f, p.c (T.textDim, 255), T.fMin);
    }
    void relayout() { layout(); }

    const Spec* spec_ = nullptr;
    ui::Knob knobs_[kMaxCtl];
    ui::Stepper steppers_[kMaxCtl];
    ui::LedToggle toggles_[kMaxCtl];
    ui::LedList lists_[kMaxCtl];
    ui::NoteMask masks_[kMaxCtl];
    ui::NumberField numbers_[kMaxCtl];
    bool vis_[kMaxCtl] = {};
    ui::Rect display_, blockRect_[kMaxBlocks];
    int groupBlock_[kMaxBlocks] = {};
    int nGroups_ = 0, active_ = -1;
    bool loadShown_ = false;
};

}}} // namespace synthyboy_mpc::overlay::oscspec
