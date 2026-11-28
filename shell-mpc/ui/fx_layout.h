// SynthyBoy - MPC plugin-drawn layer: the TABLES of the effect pages (FxLayout). ONE mechanism for every effect the FX page can show: for each of the 13 effect types of the pool (kFxTypeChorus .. kFxTypeConvolve) and for
// the three splitters (kKindSplitterLh / Lmh / Ms, modules of the fixed chain without a type of the pool) a table of its controls in the standard rows of four cells. A ROW is one block of the effect and ONE Q group (in reading
// order: the rows of the page on Q 1 .. 16). A control is named by its ENGINE KEY ("chorus.rate"), so the name, the range, the unit and the choices come from the parameter table (one source of truth); the table says only
// where it stands and when it is live.
//
// THE RULE (one, for every effect): every control has a condition (a parameter of the same effect and the values of it the control does something under: `Live`; none = always).
//   1. A cell can hold TWO controls that exclude each other (RATE / DIVISION, TIME / DIVISION, COMB OFFSET / FORMANT X ...): the cell shows the one that is live, the other is not on the page.
//   2. A control that is not live is ALWAYS HIDDEN (bold cut, owner 2026-11-21: never dim): the live controls of a row stand together from the left (its Q group has fewer members), a row with nothing live is gone and the rows
//      below move up.
// FUNCTIONAL PANELS: panelsOf(kind) assigns every row to one named panel.
// Small effects show all rows together; larger effects group related parameters
// into two or three explicit tabs. Conditional rows compact within their panel.
// The display keeps its own renderer and yields space only for a dense panel.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "choice_names.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace ui { namespace fxl {

/** The pages of the effects: kind 1 .. 13 is a type of the pool (synthyboy::FxInstanceType), then the three splitters (modules of the fixed chain: item index 13 .. 15 of the racks, no type of the pool). */
constexpr int kKindSplitterLh = synthyboy::kNumFxInstanceTypes, kKindSplitterLmh = kKindSplitterLh + 1, kKindSplitterMs = kKindSplitterLh + 2;
constexpr int kNumKinds = kKindSplitterMs + 1;
constexpr int kMaxRows = 8, kMaxPageRows = 4;

/** The condition a control is live under: the parameter `key` (of the same effect) rounds to one of the values whose bit is set in `modes`; key == nullptr: always live. */
struct Live
{
    const char* key = nullptr;
    uint32_t modes = 0;
    const char* key2 = nullptr;   // an optional second condition (both must hold): the second time of the Delay is live in the sync AND in a style that has a second time
    uint32_t modes2 = 0;
    bool always() const noexcept { return key == nullptr; }
    static bool in (uint32_t set, float plainValue) noexcept
    {
        const long v = std::lround (plainValue);
        return v >= 0 && v < 32 && ((set >> v) & 1u) != 0;
    }
    bool holds (float plainValue) const noexcept { return in (modes, plainValue); }
};

/** One control of a cell: its engine key, when it is live, and a label where the one of the engine would not read well (nullptr: derived from the engine's name). */
struct Alt
{
    const char* key = nullptr;
    Live live;
    const char* name = nullptr;
};
/** A cell: one control, or two that exclude each other (the first live one is shown). A cell without a key is empty. */
constexpr int kAlts = 3;   // the most controls that exclude each other in one cell (the compressor's THRESHOLD of the selected band: LOW / MID / HIGH)
struct Cell
{
    Alt alt[kAlts];
    Cell() = default;
    Cell (const Alt& a) { alt[0] = a; }   // a cell of one control
    Cell (const Alt& a, const Alt& b) { alt[0] = a; alt[1] = b; }
    Cell (const Alt& a, const Alt& b, const Alt& c) { alt[0] = a; alt[1] = b; alt[2] = c; }
};
/** The VIEW-LOCAL selector of the compressor (not an engine parameter, no Q slot): a cell whose control key is kViewBand is the BAND selector (LOW / MID / HIGH) of the editor, and the condition key kViewBand in a `Live`
    stands for the EFFECTIVE selected band (0 low, 1 mid, 2 high; the editor clamps the selection to the bands the mode has: single band = mid). */
constexpr const char* kViewBand = "@band";
inline bool isView (const char* key) noexcept { return key != nullptr && key[0] == '@'; }
struct Row
{
    Cell cell[4];
    const char* title = "";   // the name of the block: the name of the Q group on the Q-Link panel
};
struct Layout
{
    int kind = 0;
    const char* name = "";
    int rowsPerPage = 3;      // the rows of a page (3 or 4 without a display, 2 or 3 under one)
    int displayH = 0;         // the height of the display (0: the effect has none)
    int numRows = 0;
    Row row[kMaxRows];
    /** DISPLAY LEFT, CONTROLS RIGHT: the display takes the left half of the page over its whole height and the controls stand in two columns (cells of two per line) at the right (a Q group of four is a block of two
        lines). Used when the visible rows of the panel fit the height at the right (side mode); otherwise the standard stacked layout applies. Effects with a
        display (Compressor, Delay, Distortion, EQ, FX Filter) have it. */
    bool side = false;
};

/** View-local functional panels. Rows still own the Q groups; panel selection
    only chooses which of those rows is visible, never an engine parameter. */
struct Panels
{
    int count = 1;
    const char* names[3] = { "MAIN", nullptr, nullptr };
    int rows[kMaxRows] = {};
};
inline Panels panelsOf (int kind) noexcept
{
    using namespace synthyboy;
    switch (kind)
    {
        case kFxTypeCompressor: return { 3, { "MAIN", "BANDS", "UPWARD" }, { 0, 0, 1, 1, 2, 2 } };
        case kFxTypeReverb: return { 3, { "MAIN", "TONE", "MOD" }, { 0, 0, 0, 1, 1, 2, 2 } };
        // FX screen review 2026-11-26 (docs/review/FX-SCREEN-REVIEW-2026-11-26.md): a panel of a side page holds at most two rows of four cells under the tabs, so the
        // second mid band of the EQ has a panel of its own and every EQ panel keeps the response curve; Bode and Convolve (no display, four rows) are one page each.
        case kFxTypeDelay: return { 2, { "MAIN", "OUTPUT" }, { 0, 0, 1, 1 } };
        case kFxTypeEq: return { 3, { "MAIN", "TONE", "MID 2" }, { 0, 0, 1, 1, 2 } };
        case kFxTypeFilter: return { 2, { "MAIN", "TONE" }, { 0, 0, 1, 1, 1 } };
        default: return {};
    }
}

// the sync of the effects: 0 Free (Hz), 1 Tempo, 2 Tempo dotted, 3 Tempo triplet
constexpr uint32_t kSyncFree = 1u << 0, kSyncTempo = (1u << 1) | (1u << 2) | (1u << 3);
// the styles of the Delay (delay.style: 0 Mono, 1 Stereo, 2 Ping pong, 3 Mid ping pong, 4 Tap->Delay): every style but Mono reads the second time (core/engine-own/fx/fx_effects.h: the right side of Mono uses the left time)
constexpr uint32_t kStyleSecondTime = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);

namespace dsl {
inline Alt k (const char* key, const char* name = nullptr) { return Alt { key, {}, name }; }
inline Alt when (const char* key, const char* liveKey, uint32_t modes, const char* name = nullptr) { return Alt { key, Live { liveKey, modes }, name }; }
inline Alt when2 (const char* key, const char* liveKey, uint32_t modes, const char* liveKey2, uint32_t modes2, const char* name = nullptr) { return Alt { key, Live { liveKey, modes, liveKey2, modes2 }, name }; }
inline Cell triple (Alt a, Alt b, Alt c3) { return Cell (a, b, c3); }
/** The BAND selector cell: live while the compressor has more than one band (its `bands` mode is not Single band). */
inline Cell bandSelector() { return Cell (Alt { kViewBand, Live { "compressor.bands", (1u << 0) | (1u << 1) | (1u << 2) }, "BAND" }); }
inline Cell c (Alt a) { return Cell (a); }
inline Cell c (const char* key, const char* name = nullptr) { return c (k (key, name)); }
inline Cell pair (Alt a, Alt b) { return Cell (a, b); }
inline Cell none() { return Cell {}; }
inline Row row (const char* title, Cell a, Cell b = none(), Cell cc = none(), Cell d = none())
{
    Row r;
    r.title = title;
    r.cell[0] = a; r.cell[1] = b; r.cell[2] = cc; r.cell[3] = d;
    return r;
}
/** RATE / DIVISION: the value that is live in the sync of the effect (`sync` the key of its sync). */
inline Cell rate (const char* rateKey, const char* divKey, const char* syncKey, const char* name = nullptr, const char* divName = nullptr)
{
    return pair (when (rateKey, syncKey, kSyncFree, name), when (divKey, syncKey, kSyncTempo, divName));
}
}

inline const Layout& layoutOf (int kind) noexcept
{
    using namespace dsl;
    static const Layout kAll[kNumKinds] = {
        // 0: none
        Layout { 0, "None", 3, 0, 0, {} },
        Layout { synthyboy::kFxTypeChorus, "Chorus", 3, 0, 3, {
            row ("MODULATION", rate ("chorus.rate", "chorus.division", "chorus.sync"), c ("chorus.sync"), c ("chorus.depth"), c ("chorus.feedback")),
            row ("VOICES, DELAYS AND MIX", c ("chorus.voices"), c ("chorus.delay1"), c ("chorus.delay2"), c ("chorus.mix")),
            row ("FILTER AND OUTPUT", c ("chorus.filter_cutoff"), c ("chorus.filter_spread"), c ("chorus.level")) } },
        // 2: Compressor (24 values). Default view (8 cells): MIX, ATTACK, RELEASE, BANDS / MODE, THRESHOLD, RATIO of the SELECTED band, and the BAND selector (LOW / MID / HIGH, view-local: not a parameter, no Q slot; hidden in
        // Single band). THRESHOLD and RATIO are the UPPER pair of the band (the downward compression above the threshold: the pair that makes the character; the lower pair is the upward stage, ratio 0 by default); in Limit mode the
        // threshold is the ceiling and the upper ratio is not used (fx_compressor.h: the limiter replaces the downward stage), so RATIO is hidden there. The MODE (Compress / Limit / Serum peak compressor) changes what the
        // one tap of the selector away.
        Layout { synthyboy::kFxTypeCompressor, "Compressor", 2, 160, 6, {
            row ("COMPRESSOR", c ("compressor.mix"), c ("compressor.attack"), c ("compressor.release"), c ("compressor.bands")),
            row ("BAND", c ("compressor.limit"),
                 triple (when ("compressor.low.upper_threshold", kViewBand, 1u << 0, "THRESHOLD"), when ("compressor.mid.upper_threshold", kViewBand, 1u << 1, "THRESHOLD"), when ("compressor.high.upper_threshold", kViewBand, 1u << 2, "THRESHOLD")),
                 triple (when2 ("compressor.low.upper_ratio", kViewBand, 1u << 0, "compressor.limit", (1u << 0) | (1u << 2), "RATIO"), when2 ("compressor.mid.upper_ratio", kViewBand, 1u << 1, "compressor.limit", (1u << 0) | (1u << 2), "RATIO"),
                         when2 ("compressor.high.upper_ratio", kViewBand, 1u << 2, "compressor.limit", (1u << 0) | (1u << 2), "RATIO")),
                 bandSelector()),
            row ("OUTPUT AND CROSSOVERS", c ("compressor.level"), when ("compressor.limit_latency_comp", "compressor.limit", 1u << 1),
                 when ("compressor.crossover_low", "compressor.bands", (1u << 0) | (1u << 1)), when ("compressor.crossover_high", "compressor.bands", (1u << 0) | (1u << 2))),
            row ("GAINS", when ("compressor.low.gain", "compressor.bands", (1u << 0) | (1u << 1)), c ("compressor.mid.gain"), when ("compressor.high.gain", "compressor.bands", (1u << 0) | (1u << 2))),
            row ("LOWER (UPWARD) LOW AND MID", when ("compressor.low.lower_threshold", "compressor.bands", (1u << 0) | (1u << 1), "LOW THRESH"), when ("compressor.low.lower_ratio", "compressor.bands", (1u << 0) | (1u << 1), "LOW RATIO"),
                 c ("compressor.mid.lower_threshold", "MID THRESH"), c ("compressor.mid.lower_ratio", "MID RATIO")),
            row ("LOWER (UPWARD) HIGH", when ("compressor.high.lower_threshold", "compressor.bands", (1u << 0) | (1u << 2), "HIGH THRESH"), when ("compressor.high.lower_ratio", "compressor.bands", (1u << 0) | (1u << 2), "HIGH RATIO")) }, true },
        // Delay: the times of the two sides stand together (TIME: Q 1 .. 4), then the loop (the feedback and its band pass) and the mix. OFFSET L / R are the factors of the left / right time
        // (core/engine-own/fx/fx_modules.cpp: delay.offset_left -> offsetL, delay.offset -> offsetR; the engine name "Delay Offset" of the right one read like a common offset).
        Layout { synthyboy::kFxTypeDelay, "Delay", 2, 176, 4, {
            row ("TIME", c ("delay.style"), c ("delay.sync"), pair (when ("delay.time", "delay.sync", kSyncFree), when ("delay.division", "delay.sync", kSyncTempo)),
                 pair (when2 ("delay.time2", "delay.sync", kSyncFree, "delay.style", kStyleSecondTime), when2 ("delay.division2", "delay.sync", kSyncTempo, "delay.style", kStyleSecondTime))),
            row ("FEEDBACK AND MIX", c ("delay.feedback"), c ("delay.filter_cutoff"), c ("delay.filter_spread"), c ("delay.mix")),
            row ("OUTPUT AND QUALITY", c ("delay.level"), c ("delay.hq")),
            row ("TIME OFFSETS", c ("delay.offset_left", "OFFSET L"), c ("delay.offset", "OFFSET R")) }, true },
        Layout { synthyboy::kFxTypeDistortion, "Distortion", 3, 160, 3, {
            row ("DISTORTION", c ("distortion.type"), c ("distortion.drive"), c ("distortion.mix"), c ("distortion.level")),
            row ("FILTER", c ("distortion.filter_order"), when ("distortion.filter_cutoff", "distortion.filter_order", (1u << 1) | (1u << 2)),
                 when ("distortion.filter_resonance", "distortion.filter_order", (1u << 1) | (1u << 2))),
            row ("FILTER SHAPE", when ("distortion.filter_blend", "distortion.filter_order", (1u << 1) | (1u << 2)), when ("distortion.filter_keytrack", "distortion.filter_order", (1u << 1) | (1u << 2))) }, true },
        // EQ: a GAIN is live only in the band's mode that has one (shelf, peak): the high pass, the notch and the low pass of the engine do not read it (core/engine-own/fx/fx_eq.h, both laws)
        Layout { synthyboy::kFxTypeEq, "EQ", 3, 160, 5, {
            row ("LOW AND MID BANDS", c ("eq.low.cutoff"), when ("eq.low.gain", "eq.low.mode", 1u << 0), c ("eq.mid.cutoff"), when ("eq.mid.gain", "eq.mid.mode", 1u << 0)),
            row ("HIGH BAND AND OUTPUT", c ("eq.high.cutoff"), when ("eq.high.gain", "eq.high.mode", 1u << 0), c ("eq.level")),
            row ("MODES", c ("eq.low.mode"), c ("eq.mid.mode"), c ("eq.high.mode"), c ("eq.law")),
            row ("RESONANCE", c ("eq.low.resonance"), c ("eq.mid.resonance"), c ("eq.high.resonance")),
            row ("MID BAND 2", c ("eq.mid2.mode"), when ("eq.mid2.cutoff", "eq.mid2.mode", (1u << 1) | (1u << 2)), when ("eq.mid2.gain", "eq.mid2.mode", 1u << 1),
                 when ("eq.mid2.resonance", "eq.mid2.mode", (1u << 1) | (1u << 2))) }, true },
        Layout { synthyboy::kFxTypeFilter, "FX Filter", 3, 160, 5, {
            row ("FILTER", c ("fxfilter.model"), c ("fxfilter.style"), c ("fxfilter.cutoff"), c ("fxfilter.resonance")),
            row ("MODEL", c ("fxfilter.drive"), c ("fxfilter.mix"), pair (when ("fxfilter.comb_offset", "fxfilter.model", 1u << 6), when ("fxfilter.formant.x", "fxfilter.model", 1u << 5)),
                 when ("fxfilter.formant.y", "fxfilter.model", 1u << 5)),
            row ("FORMANT", when ("fxfilter.formant.resonance", "fxfilter.model", 1u << 5), when ("fxfilter.formant.spread", "fxfilter.model", 1u << 5), when ("fxfilter.formant.transpose", "fxfilter.model", 1u << 5)),
            row ("SHAPE", c ("fxfilter.blend"), c ("fxfilter.keytrack"), c ("fxfilter.clean_mode"), c ("fxfilter.var")),
            row ("OUTPUT", c ("fxfilter.pan"), c ("fxfilter.level")) }, true },
        // Flanger and Phaser: the same first two blocks (MODULATION, then FEEDBACK / PHASE OFFSET / MIX / LEVEL on Q 5 .. 8); the phaser's second sweep stands last
        Layout { synthyboy::kFxTypeFlanger, "Flanger", 3, 0, 2, {
            row ("MODULATION", rate ("flanger.rate", "flanger.division", "flanger.sync"), c ("flanger.sync"), c ("flanger.depth"), c ("flanger.center")),
            row ("FEEDBACK AND OUTPUT", c ("flanger.feedback"), c ("flanger.phase_offset"), c ("flanger.mix"), c ("flanger.level")) } },
        Layout { synthyboy::kFxTypePhaser, "Phaser", 3, 0, 3, {
            row ("MODULATION", rate ("phaser.rate", "phaser.division", "phaser.sync"), c ("phaser.sync"), c ("phaser.depth"), c ("phaser.center")),
            row ("FEEDBACK AND OUTPUT", c ("phaser.feedback"), c ("phaser.phase_offset"), c ("phaser.mix"), c ("phaser.level")),
            row ("SWEEP", c ("phaser.depth2"), c ("phaser.blend")) } },
        Layout { synthyboy::kFxTypeReverb, "Reverb", 3, 0, 7, {
            row ("REVERB", c ("reverb.mix"), c ("reverb.type"), c ("reverb.size"), c ("reverb.decay")),
            row ("ALGORITHM", c ("reverb.predelay"), c ("reverb.width"), when ("reverb.room", "reverb.type", 1u << 4), when ("reverb.feedback", "reverb.type", (1u << 4) | (1u << 5))),
            row ("OUTPUT", c ("reverb.level"), c ("reverb.force_enable", "FORCE ON")),
            row ("INPUT FILTERS", c ("reverb.pre_low_cut"), c ("reverb.pre_high_cut")),
            row ("DAMPING", c ("reverb.low_shelf_cutoff"), c ("reverb.low_shelf_gain"), c ("reverb.high_shelf_cutoff"), c ("reverb.high_shelf_gain")),
            row ("DIFFUSION", c ("reverb.er_size"), c ("reverb.diff_a"), c ("reverb.diff_b")),
            row ("MODULATION", c ("reverb.chorus_amount"), c ("reverb.chorus_rate")) } },
        // Hyper / Dimension: the unison block (rate, voices, detune, retrigger), then the dimension (size and mix) and the output
        Layout { synthyboy::kFxTypeHyperDimension, "Hyper/Dimension", 3, 0, 2, {
            row ("HYPER", c ("hyperdim.rate"), c ("hyperdim.unison"), c ("hyperdim.detune"), c ("hyperdim.retrig")),
            row ("DIMENSION AND OUTPUT", c ("hyperdim.dim_size"), c ("hyperdim.dim_mix"), c ("hyperdim.mix"), c ("hyperdim.level", "LEVEL")) } },
        Layout { synthyboy::kFxTypeUtility, "Utility", 3, 0, 3, {
            row ("STEREO AND FILTERS", c ("utility.width"), c ("utility.pan"), c ("utility.lpf"), c ("utility.hpf")),
            row ("MONO BASS AND MIX", c ("utility.mono_bass"), when ("utility.mono_freq", "utility.mono_bass", 1u << 1), c ("utility.mix")),
            row ("POLARITY AND OUTPUT", c ("utility.polarity_l"), c ("utility.polarity_r"), c ("utility.level")) } },
        Layout { synthyboy::kFxTypeBode, "Bode", 4, 0, 4, {
            row ("SHIFT", c ("bode.shift"), c ("bode.range"), c ("bode.dir"), c ("bode.balance")),
            row ("FEEDBACK AND OUTPUT", c ("bode.feed"), c ("bode.mix")),
            row ("DELAY", rate ("bode.delay", "bode.division", "bode.sync", "DELAY", "DIVISION"), c ("bode.sync"), c ("bode.blur"), c ("bode.width")),
            row ("STEREO AND OUTPUT", c ("bode.mono_input"), c ("bode.retrig"), c ("bode.level")) } },
        Layout { synthyboy::kFxTypeConvolve, "Convolve", 4, 0, 4, {
            row ("IMPULSE", c ("convolve.ir"), c ("convolve.size"), c ("convolve.tone"), rate ("convolve.predelay", "convolve.division", "convolve.sync", "PRE-DELAY", "PRE-DELAY")),
            row ("ENVELOPE AND OUTPUT", c ("convolve.attack"), c ("convolve.decay"), c ("convolve.mix")),
            row ("TAIL", c ("convolve.sync"), c ("convolve.phi_min"), c ("convolve.damp"), c ("convolve.ir_gain")),
            row ("LAW AND OUTPUT", c ("convolve.law"), c ("convolve.level")) } },
        Layout { kKindSplitterLh, "Splitter L/H", 1, 206, 1, {
            row ("SPLIT", c ("splitter_lh.freq", "SPLIT FREQUENCY"), c ("splitter_lh.low_bypass", "LOW BYPASS"), c ("splitter_lh.high_bypass", "HIGH BYPASS"), c ("splitter_lh.level", "LEVEL")) } },
        Layout { kKindSplitterLmh, "Splitter L/M/H", 2, 240, 2, {
            row ("SPLIT", c ("splitter_lmh.freq_low", "LOW SPLIT"), c ("splitter_lmh.freq_high", "HIGH SPLIT"), c ("splitter_lmh.level", "LEVEL")),
            row ("BYPASS", c ("splitter_lmh.low_bypass", "LOW BYPASS"), c ("splitter_lmh.mid_bypass", "MID BYPASS"), c ("splitter_lmh.high_bypass", "HIGH BYPASS")) } },
        Layout { kKindSplitterMs, "Splitter M/S", 1, 206, 1, {
            row ("MID / SIDE", c ("splitter_ms.mid_bypass", "MID BYPASS"), c ("splitter_ms.side_bypass", "SIDE BYPASS"), c ("splitter_ms.level", "LEVEL")) } },
    };
    return kAll[kind >= 0 && kind < kNumKinds ? kind : 0];
}

// THE RACKS OF THE BANDS (engine racks 3 .. 9, docs/params.json "FX / Racks"): rack 3 L/H low, 4 L/H high, 5 L/M/H low, 6 mid, 7 high, 8 M/S mid, 9 M/S side. Only the 13 modules of instance 0 that are no splitters sit in
// them (the slots of the pool and the splitters stay in Main / Bus 1 / Bus 2).
constexpr int kFirstBandRack = 3, kNumBandRacks = 7, kNumRacks = kFirstBandRack + kNumBandRacks;
/** The bands of a splitter kind (kKindSplitter*): 2, 3, 2; 0 for any other kind. */
inline int bandsOfSplitter (int kind) noexcept { return kind == kKindSplitterLmh ? 3 : (kind == kKindSplitterLh || kind == kKindSplitterMs ? 2 : 0); }
/** The engine rack of band `band` of the splitter `kind`, -1 where there is none. */
inline int bandRack (int kind, int band) noexcept
{
    if (band < 0 || band >= bandsOfSplitter (kind))
        return -1;
    return kFirstBandRack + (kind == kKindSplitterLh ? 0 : (kind == kKindSplitterLmh ? 2 : 5)) + band;
}
/** The splitter kind and the band of a band rack (3 .. 9); false for the buses. */
inline bool bandOfRack (int rack, int& kind, int& band) noexcept
{
    if (rack < kFirstBandRack || rack >= kNumRacks)
        return false;
    const int r = rack - kFirstBandRack;
    kind = r < 2 ? kKindSplitterLh : (r < 5 ? kKindSplitterLmh : kKindSplitterMs);
    band = r < 2 ? r : (r < 5 ? r - 2 : r - 5);
    return true;
}
/** The name of a band: LOW / HIGH, LOW / MID / HIGH, MID / SIDE. */
inline const char* bandName (int kind, int band) noexcept
{
    static const char* const kLh[2] = { "LOW", "HIGH" };
    static const char* const kLmh[3] = { "LOW", "MID", "HIGH" };
    static const char* const kMs[2] = { "MID", "SIDE" };
    if (band < 0 || band >= bandsOfSplitter (kind))
        return "";
    return kind == kKindSplitterLmh ? kLmh[band] : (kind == kKindSplitterLh ? kLh[band] : kMs[band]);
}
/** The name of a rack as the rack switch and the tag of a card show it: MAIN, BUS 1, BUS 2, L/H LOW ... M/S SIDE. */
inline const char* rackName (int rack) noexcept
{
    static const char* const k[kNumRacks] = { "MAIN", "BUS 1", "BUS 2", "L/H LOW", "L/H HIGH", "L/M/H LOW", "L/M/H MID", "L/M/H HIGH", "M/S MID", "M/S SIDE" };
    return rack >= 0 && rack < kNumRacks ? k[rack] : "";
}

/** The label of a control: the table's name, else the engine's name for it in capitals without the name of its effect in front ("Delay Time" -> TIME); the laws and types with their own label (choice_names.h). */
inline void controlName (const Alt& a, const synthyboy::ParamInfo& pi, const char* typeName, char* out, size_t cap)
{
    if (a.name != nullptr)
    {
        std::snprintf (out, cap, "%.*s", static_cast<int> (cap) - 1, a.name);
        return;
    }
    if (const char* lab = shownLabel (pi.key))
    {
        std::snprintf (out, cap, "%.*s", static_cast<int> (cap) - 1, lab);
        return;
    }
    std::string s = pi.name != nullptr ? pi.name : "";
    const char* prefixes[] = { typeName, "Compressor", "Distortion", "EQ", "Delay", "Chorus", "Flanger", "Phaser", "Reverb", "FX Filter", "Hyper", "Utility", "Bode", "Convolve" };
    for (const char* pre : prefixes)
    {
        const size_t n = pre != nullptr ? std::strlen (pre) : 0;
        if (n > 0 && s.size() > n + 1 && s.compare (0, n, pre) == 0 && s[n] == ' ')
        {
            s = s.substr (n + 1);
            break;
        }
    }
    for (char& ch : s)
        ch = static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));
    std::snprintf (out, cap, "%.*s", static_cast<int> (cap) - 1, s.c_str());
}

}}} // namespace synthyboy_mpc::ui::fxl
