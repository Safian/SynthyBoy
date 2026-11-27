// SynthyBoy - MPC overlay: the DETAIL PAGES of the oscillator types that hold the parameters the full screen has no room for (docs/MPC.md "Oscillator UI", table "type -> page -> block -> controls").
// A page is DATA (a Spec: title, whether the display of the type is above it, blocks, controls with the key of the parameter, the state in which the control is shown). The controls are native cells of the host skin (owner decision
// 2026-11-27: the C++ layer has no UI elements); `OscSpecPage` is plain data: which controls apply in the current state, the Q groups (each titled block with a visible control is one group of up to four controls in reading order, a
// block with nothing to show is left out) and the rectangle of the display. Pure: no OS headers; the owner (OscUi) binds the controls to the parameters and says which controls are visible.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>

#include "../ui/model.h"

namespace synthyboy_mpc { namespace overlay { namespace oscspec {

enum class K : uint8_t { Knob, Stepper, Toggle, Number, List, Mask, Page };   // List: a short list (all options visible); Mask: the twelve notes of the transpose snap; Page: a button that opens another detail page (`key` is its token, osc_ui.h pageOfToken; no parameter, no Q slot)
/** The state a control is shown in (Always: every state); the owner evaluates them from the parameters of the oscillator. */
enum class Cond : uint8_t { Always, LoopOn, SliceAuto, SliceManual, SliceAny, KeytrackOn, DensSync, DensGrains, LenSync, LenPercent, RemapOn, FltOn, SpecYAxis, GranYAxis, TrackOff, TempoLen,
                            // the type of the oscillator (the bold cut: one MORE page for the five types, a control of another type is hidden, not dimmed) and the warps that are not Off
                            KWave, KMulti, KSample, KSpec, KGran, NotGran, MultiSampleSpec, PhaseKinds, RandKinds, Warp1On, Warp2On, Warp2AmtOn, Count };
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

enum PageId { kWtMap = 16, kMore = 17, kSampleRegion = 18, kSampleSlice = 19, kGrain = 20, kSpecFilter = 21, kSpecScan = 22, kOptions = 23, kFirstPage = 16, kLastPage = 23 };

// ---- Wavetable: MAP (the remap curve of the frame position and the spectral morph)
constexpr Ctl kWtMapCtls[] = {
    { K::Toggle, "REMAP", "remap.on", Cond::Always, 1 }, { K::List, "CURVE", "remap.slot", Cond::RemapOn, 3 }, { K::Knob, "RISE", "remap.rise", Cond::RemapOn, 1 }, { K::Knob, "FALL", "remap.fall", Cond::RemapOn, 1 },
    { K::Stepper, "TYPE", "morph.type", Cond::Always, 2 }, { K::Knob, "AMOUNT", "morph.amount", Cond::Always, 1 }, { K::Knob, "SPREAD", "morph.spread", Cond::Always, 1 },
};
constexpr Block kWtMapBlocks[] = { { "FRAME REMAP", 0, 0, 4 }, { "SPECTRAL MORPH", 1, 4, 3 } };
// ---- every type: MORE (the bold cut: THE ONE MORE of the full screen). What the default view of the type has no room for: the pitch (SEM, CRS, TRACK), the unison blend, the second warp and the warp phases, the start phase,
// the velocity tracking of the multisample, the random grain values and the loop, and the buttons of the detail pages of the type (MAP, ENV, REGION, SLICE, FILTER, SCAN, GRAIN, WINDOW, UNISON) and of OPTIONS (the rest).
// A control that does not apply to the type is hidden (its Cond), a block with nothing to show is left out.
constexpr Ctl kMoreCtls[] = {
    { K::Number, "SEM", "transpose_sem", Cond::Always, 1 }, { K::Number, "CRS", "pitch_coarse", Cond::Always, 1 }, { K::Toggle, "TRACK", "pitch_track", Cond::Always, 1 }, { K::Number, "FIN", "tune", Cond::KSpec, 1 },
    { K::Knob, "BLEND", "unison.blend", Cond::NotGran, 1 }, { K::Number, "VOICES", "unison.voices", Cond::KGran, 1 }, { K::Page, "UNISON", "unison", Cond::KGran, 1 },
    { K::Knob, "W1 PHASE", "warp.phase", Cond::Warp1On, 1 }, { K::Stepper, "WARP 2", "warp2.type", Cond::MultiSampleSpec, 2 }, { K::Knob, "W2 AMOUNT", "warp2.amount", Cond::Warp2AmtOn, 1 }, { K::Knob, "W2 PHASE", "warp2.phase", Cond::Warp2On, 1 },
    { K::Number, "PHASE", "phase", Cond::PhaseKinds, 1 }, { K::Number, "RAND", "random_phase", Cond::RandKinds, 1 },
    { K::Page, "MAP", "map", Cond::KWave, 1 }, { K::Page, "ENV", "env", Cond::KMulti, 1 }, { K::Page, "REGION", "region", Cond::KSample, 1 }, { K::Page, "SLICE", "slice", Cond::KSample, 1 },
    { K::Page, "FILTER", "filter", Cond::KSpec, 1 }, { K::Page, "SCAN", "scan", Cond::KSpec, 1 }, { K::Page, "GRAIN", "grain", Cond::KGran, 1 }, { K::Page, "WINDOW", "window", Cond::KGran, 1 }, { K::Page, "OPTIONS", "options", Cond::Always, 1 },
    { K::Toggle, "OVERRIDE", "multi.vel_override", Cond::KMulti, 1 }, { K::Number, "VEL TRACK", "multi.vel_track", Cond::KMulti, 1 },
    { K::Knob, "LEN RAND", "gran.rand_length", Cond::KGran, 1 }, { K::Knob, "PAN RAND", "gran.rand_pan", Cond::KGran, 1 }, { K::Knob, "LVL RAND", "gran.rand_level", Cond::KGran, 1 }, { K::Toggle, "LOOP", "gran.loop_grains", Cond::KGran, 1 },
};
constexpr Block kMoreBlocks[] = { { "PITCH", 0, 0, 4 }, { "UNISON", 0, 4, 3 }, { "WARP", 0, 7, 4 }, { "PHASE", 0, 11, 2 }, { "PAGES", 1, 13, 9 }, { "VELOCITY", 1, 22, 2 }, { "GRAIN", 1, 24, 4 } };
// ---- every type: OPTIONS (switches, pitch mode, phase, the key and velocity zone, the unison law); reached from MORE
constexpr Ctl kOptionsCtls[] = {
    { K::Toggle, "ON", "on", Cond::Always, 1 }, { K::Toggle, "ENV 1", "env1", Cond::Always, 1 }, { K::Toggle, "SMOOTH", "smooth_interp", Cond::Always, 1 },
    { K::List, "MODE", "pitch_mode", Cond::Always, 3 }, { K::Toggle, "BEND", "pitch_bend_track", Cond::Always, 1 }, { K::Knob, "FIXED", "fixed_pitch", Cond::TrackOff, 1 },
    { K::List, "MEMORY", "phase_memory", Cond::Always, 3 }, { K::Toggle, "LEGATO", "phase_legato", Cond::Always, 1 }, { K::List, "RATIO", "ratio_src", Cond::Always, 4 },
    { K::Number, "LOW", "map.key_low", Cond::Always, 1 }, { K::Number, "HIGH", "map.key_high", Cond::Always, 1 }, { K::Toggle, "FOLD", "map.key_fold", Cond::Always, 1 }, { K::Toggle, "WARP", "map.key_warp", Cond::Always, 1 },
    { K::Number, "LOW", "map.vel_low", Cond::Always, 1 }, { K::Number, "HIGH", "map.vel_high", Cond::Always, 1 },
    { K::Mask, "NOTES", "transpose_snap", Cond::Always, 7 }, { K::Toggle, "SNAP NOTE", "transpose_snap", Cond::Always, 2 },
    { K::Knob, "CURVE", "unison.detune_curve", Cond::Always, 2 }, { K::Knob, "SHAPE", "unison.blend_shape", Cond::Always, 2 }, { K::List, "LAW", "unison.law", Cond::Always, 3 }, { K::List, "STACK", "unison.stack_repeat", Cond::Always, 3 },
};
constexpr Block kOptionsBlocks[] = { { "OSCILLATOR", 0, 0, 3 }, { "PITCH", 0, 3, 3 }, { "PHASE", 1, 6, 3 }, { "KEY ZONE", 1, 9, 4 }, { "VELOCITY ZONE", 1, 13, 2 }, { "TRANSPOSE SNAP", 2, 15, 2 }, { "UNISON SHAPE", 2, 17, 4 } };
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
    { kMore, "MORE", false, SB_SPEC (kMoreBlocks), SB_SPEC (kMoreCtls), 2 },
    { kSampleRegion, "SAMPLE REGION", true, SB_SPEC (kSampleRegionBlocks), SB_SPEC (kSampleRegionCtls), 2 },
    { kSampleSlice, "SAMPLE SLICES AND SCAN", true, SB_SPEC (kSampleSliceBlocks), SB_SPEC (kSampleSliceCtls), 2 },
    { kGrain, "GRAIN ENGINE", true, SB_SPEC (kGrainBlocks), SB_SPEC (kGrainCtls), 2 },
    { kSpecFilter, "SPECTRAL FILTER", true, SB_SPEC (kSpecFilterBlocks), SB_SPEC (kSpecFilterCtls), 2 },
    { kSpecScan, "SPECTRAL SCAN", true, SB_SPEC (kSpecScanBlocks), SB_SPEC (kSpecScanCtls), 2 },
    { kOptions, "OSCILLATOR OPTIONS", false, SB_SPEC (kOptionsBlocks), SB_SPEC (kOptionsCtls), 3 },
};
#undef SB_SPEC
inline const Spec* specOf (int page) noexcept { for (const Spec& s : kSpecs) if (s.page == page) return &s; return nullptr; }
inline bool isSpecPage (int page) noexcept { return specOf (page) != nullptr; }

// ===============================================================================================================================================================
class OscSpecPage
{
public:
    static constexpr int kBarH = 64, kDisplayH = 244, kMaxCtl = 32, kMaxBlocks = 8;

    void place (const ui::Rect& r) { rect_ = r; relayout(); }
    ui::Rect rect() const noexcept { return rect_; }

    /** Takes the spec: all its controls apply (the owner says which do not, `setVisible`). */
    void loadSpec (const Spec& s)
    {
        spec_ = &s;
        for (int i = 0; i < kMaxCtl; ++i)
            vis_[i] = i < s.numCtls;
        relayout();
    }
    const Spec* spec() const noexcept { return spec_; }
    void relayoutNow() { relayout(); }
    bool visible (int i) const noexcept { return spec_ != nullptr && i >= 0 && i < spec_->numCtls && vis_[i]; }
    /** The owner says which controls apply now; the groups are made again when the set changed. */
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
    ui::Rect backRect() const noexcept { return { rect_.x + 4, rect_.y + 4, 120, kBarH - 8 }; }

    /** The blocks that are on the page now (a block with no visible control, or with page buttons only, is left out): the Q groups are theirs in this order. */
    int numGroups() const noexcept { return nGroups_; }
    const char* groupTitle (int g) const noexcept { return g >= 0 && g < nGroups_ ? spec_->blocks[groupBlock_[g]].title : ""; }
    /** The index of the control that is member `slot` (0 .. 3) of group `g` (-1: none). */
    int member (int g, int slot) const noexcept { return g >= 0 && g < nGroups_ && slot >= 0 && slot < 4 ? members_[g][slot] : -1; }
    int groupOf (int ctl, int* slot = nullptr) const noexcept
    {
        for (int g = 0; g < nGroups_; ++g)
            for (int k = 0; k < 4; ++k)
                if (members_[g][k] == ctl && ctl >= 0)
                {
                    if (slot != nullptr)
                        *slot = k;
                    return g;
                }
        return -1;
    }
    int activeGroup() const noexcept { return active_; }
    void setActiveGroup (int g) noexcept { active_ = g >= 0 && g < nGroups_ ? g : -1; }

private:
    void relayout()
    {
        display_ = spec_ != nullptr && spec_->display ? ui::Rect { rect_.x + 4, rect_.y + kBarH + 4, rect_.w - 8, kDisplayH } : ui::Rect {};
        nGroups_ = 0;
        if (spec_ != nullptr)
            for (int b = 0; b < spec_->numBlocks && b < kMaxBlocks; ++b)
            {
                const Block& bl = spec_->blocks[b];
                int m[4] = { -1, -1, -1, -1 }, n = 0;
                for (int k = 0; k < bl.n && n < 4; ++k)
                    if (vis_[bl.first + k] && spec_->ctls[bl.first + k].kind != K::Page)   // a page button has no parameter: no Q slot
                        m[n++] = bl.first + k;
                if (n == 0)
                    continue;   // a block of page buttons only, or with nothing to show: no Q group
                for (int k = 0; k < 4; ++k)
                    members_[nGroups_][k] = m[k];
                groupBlock_[nGroups_++] = b;
            }
        const int keep = active_ >= 0 && active_ < nGroups_ ? active_ : (nGroups_ > 0 ? 0 : -1);
        active_ = keep;
    }

    ui::Rect rect_ {}, display_ {};
    const Spec* spec_ = nullptr;
    bool vis_[kMaxCtl] = {};
    int members_[kMaxBlocks][4] = {};
    int groupBlock_[kMaxBlocks] = {};
    int nGroups_ = 0, active_ = -1;
};

}}} // namespace synthyboy_mpc::overlay::oscspec
