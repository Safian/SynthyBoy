// SynthyBoy - MPC UI component library: the TABLES of the effect pages (FxLayout). ONE mechanism for every effect the FX page can show: for each of the 13 effect types of the pool (kFxTypeChorus .. kFxTypeConvolve) and for
// the three splitters (kKindSplitterLh / Lmh / Ms, modules of the fixed chain without a type of the pool) a table of its controls in the standard rows of four cells. A ROW is one block of the effect and ONE Q group (in reading
// order: the rows of the page on Q 1 .. 16). A control is named by its ENGINE KEY ("chorus.rate"), so the name, the range, the unit and the choices come from the parameter table (one source of truth); the table says only
// where it stands and when it is live.
//
// THE RULE (one, for every effect): every control has a condition (a parameter of the same effect and the values of it the control does something under: `Live`; none = always).
//   1. A cell can hold TWO controls that exclude each other (RATE / DIVISION, TIME / DIVISION, COMB OFFSET / FORMANT X ...): the cell shows the one that is live, the other is not on the page.
//   2. A control that is not live is HIDDEN when no live control follows it in its row (the row closes up from the right: its Q group has fewer members, a row with nothing live is gone and the rows below move up, the pages
//      are made of the rows that are left); else it is drawn DIM in its place (it keeps the row together) and takes no touch.
// So a value that does nothing in the state of the effect is never bright, and never leaves a gap in the middle of a row where a hand would look for it.
//
// What the page of an effect looks like: `rowsPerPage` rows of cells (the standard row height of the page: the room shared by the rows a page holds), under a display of `displayH` pixels where the effect has one.
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
    bool always() const noexcept { return key == nullptr; }
    bool holds (float plainValue) const noexcept
    {
        const long v = std::lround (plainValue);
        return v >= 0 && v < 32 && ((modes >> v) & 1u) != 0;
    }
};

/** One control of a cell: its engine key, when it is live, and a label where the one of the engine would not read well (nullptr: derived from the engine's name). */
struct Alt
{
    const char* key = nullptr;
    Live live;
    const char* name = nullptr;
};
/** A cell: one control, or two that exclude each other (the first live one is shown). A cell without a key is empty. */
struct Cell
{
    Alt alt[2];
    Cell() = default;
    Cell (const Alt& a) { alt[0] = a; }   // a cell of one control
    Cell (const Alt& a, const Alt& b) { alt[0] = a; alt[1] = b; }
};
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
};

// the sync of the effects: 0 Free (Hz), 1 Tempo, 2 Tempo dotted, 3 Tempo triplet
constexpr uint32_t kSyncFree = 1u << 0, kSyncTempo = (1u << 1) | (1u << 2) | (1u << 3);

namespace dsl {
inline Alt k (const char* key, const char* name = nullptr) { return Alt { key, {}, name }; }
inline Alt when (const char* key, const char* liveKey, uint32_t modes, const char* name = nullptr) { return Alt { key, Live { liveKey, modes }, name }; }
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
        // 1: Chorus (12 values)
        Layout { synthyboy::kFxTypeChorus, "Chorus", 3, 0, 3, {
            row ("MODULATION", rate ("chorus.rate", "chorus.division", "chorus.sync"), c ("chorus.sync"), c ("chorus.depth"), c ("chorus.feedback")),
            row ("VOICES AND DELAY", c ("chorus.voices"), c ("chorus.delay1"), c ("chorus.delay2")),
            row ("FILTER AND OUTPUT", c ("chorus.filter_cutoff"), c ("chorus.filter_spread"), c ("chorus.mix"), c ("chorus.level")) } },
        // 2: Compressor (24 values): the settings, then one block per band, the gains
        Layout { synthyboy::kFxTypeCompressor, "Compressor", 2, 220, 6, {
            row ("COMPRESSOR", c ("compressor.mix"), c ("compressor.attack"), c ("compressor.release"), c ("compressor.bands")),
            row ("MODE AND CROSSOVERS", c ("compressor.limit"), when ("compressor.limit_latency_comp", "compressor.limit", 1u << 1), when ("compressor.crossover_low", "compressor.bands", (1u << 0) | (1u << 1)),
                 when ("compressor.crossover_high", "compressor.bands", (1u << 0) | (1u << 2))),
            row ("LOW BAND", when ("compressor.low.lower_threshold", "compressor.bands", (1u << 0) | (1u << 1)), when ("compressor.low.upper_threshold", "compressor.bands", (1u << 0) | (1u << 1)),
                 when ("compressor.low.lower_ratio", "compressor.bands", (1u << 0) | (1u << 1)), when ("compressor.low.upper_ratio", "compressor.bands", (1u << 0) | (1u << 1))),
            row ("MID BAND", c ("compressor.mid.lower_threshold"), c ("compressor.mid.upper_threshold"), c ("compressor.mid.lower_ratio"), c ("compressor.mid.upper_ratio")),
            row ("HIGH BAND", when ("compressor.high.lower_threshold", "compressor.bands", (1u << 0) | (1u << 2)), when ("compressor.high.upper_threshold", "compressor.bands", (1u << 0) | (1u << 2)),
                 when ("compressor.high.lower_ratio", "compressor.bands", (1u << 0) | (1u << 2)), when ("compressor.high.upper_ratio", "compressor.bands", (1u << 0) | (1u << 2))),
            row ("GAINS", when ("compressor.low.gain", "compressor.bands", (1u << 0) | (1u << 1)), c ("compressor.mid.gain"), when ("compressor.high.gain", "compressor.bands", (1u << 0) | (1u << 2)), c ("compressor.level")) } },
        // 3: Delay (14 values)
        Layout { synthyboy::kFxTypeDelay, "Delay", 2, 176, 3, {
            row ("TIME", c ("delay.sync"), pair (when ("delay.time", "delay.sync", kSyncFree), when ("delay.division", "delay.sync", kSyncTempo)),
                 pair (when ("delay.time2", "delay.sync", kSyncFree), when ("delay.division2", "delay.sync", kSyncTempo)), c ("delay.style")),
            row ("FEEDBACK AND OUTPUT", c ("delay.feedback"), c ("delay.mix"), c ("delay.level"), c ("delay.hq")),
            row ("FILTER AND OFFSET", c ("delay.filter_cutoff"), c ("delay.filter_spread"), c ("delay.offset"), c ("delay.offset_left")) } },
        // 4: Distortion (9 values): the filter values are live while the filter is in use
        Layout { synthyboy::kFxTypeDistortion, "Distortion", 3, 160, 3, {
            row ("DISTORTION", c ("distortion.type"), c ("distortion.drive"), c ("distortion.mix"), c ("distortion.level")),
            row ("FILTER", c ("distortion.filter_order"), when ("distortion.filter_cutoff", "distortion.filter_order", (1u << 1) | (1u << 2)),
                 when ("distortion.filter_resonance", "distortion.filter_order", (1u << 1) | (1u << 2)), when ("distortion.filter_blend", "distortion.filter_order", (1u << 1) | (1u << 2))),
            row ("FILTER KEY", when ("distortion.filter_keytrack", "distortion.filter_order", (1u << 1) | (1u << 2))) } },
        // 5: EQ (18 values): three bands, the second middle band (its values are live unless it is Off), the level and the law
        Layout { synthyboy::kFxTypeEq, "EQ", 3, 160, 5, {
            row ("LOW BAND", c ("eq.low.mode"), c ("eq.low.cutoff"), c ("eq.low.gain"), c ("eq.low.resonance")),
            row ("MID BAND", c ("eq.mid.mode"), c ("eq.mid.cutoff"), c ("eq.mid.gain"), c ("eq.mid.resonance")),
            row ("HIGH BAND", c ("eq.high.mode"), c ("eq.high.cutoff"), c ("eq.high.gain"), c ("eq.high.resonance")),
            row ("MID BAND 2", c ("eq.mid2.mode"), when ("eq.mid2.cutoff", "eq.mid2.mode", (1u << 1) | (1u << 2)), when ("eq.mid2.gain", "eq.mid2.mode", (1u << 1) | (1u << 2)),
                 when ("eq.mid2.resonance", "eq.mid2.mode", (1u << 1) | (1u << 2))),
            row ("OUTPUT", c ("eq.level"), c ("eq.law")) } },
        // 6: FX Filter (18 values): the model's own values (the comb offset or the formant values) are live in that model only
        Layout { synthyboy::kFxTypeFilter, "FX Filter", 3, 160, 5, {
            row ("FILTER", c ("fxfilter.model"), c ("fxfilter.style"), c ("fxfilter.cutoff"), c ("fxfilter.resonance")),
            row ("MODEL", pair (when ("fxfilter.comb_offset", "fxfilter.model", 1u << 6), when ("fxfilter.formant.x", "fxfilter.model", 1u << 5)),
                 when ("fxfilter.formant.y", "fxfilter.model", 1u << 5), when ("fxfilter.formant.resonance", "fxfilter.model", 1u << 5), when ("fxfilter.formant.spread", "fxfilter.model", 1u << 5)),
            row ("SHAPE", c ("fxfilter.drive"), c ("fxfilter.blend"), c ("fxfilter.keytrack"), c ("fxfilter.clean_mode")),
            row ("OUTPUT", c ("fxfilter.var"), c ("fxfilter.pan"), c ("fxfilter.mix"), c ("fxfilter.level")),
            row ("FORMANT PITCH", when ("fxfilter.formant.transpose", "fxfilter.model", 1u << 5)) } },
        // 7: Flanger (9 values)
        Layout { synthyboy::kFxTypeFlanger, "Flanger", 3, 0, 2, {
            row ("MODULATION", rate ("flanger.rate", "flanger.division", "flanger.sync"), c ("flanger.sync"), c ("flanger.depth"), c ("flanger.center")),
            row ("FEEDBACK AND OUTPUT", c ("flanger.feedback"), c ("flanger.phase_offset"), c ("flanger.mix"), c ("flanger.level")) } },
        // 8: Phaser (11 values)
        Layout { synthyboy::kFxTypePhaser, "Phaser", 3, 0, 3, {
            row ("MODULATION", rate ("phaser.rate", "phaser.division", "phaser.sync"), c ("phaser.sync"), c ("phaser.depth"), c ("phaser.depth2")),
            row ("SWEEP", c ("phaser.center"), c ("phaser.blend"), c ("phaser.feedback"), c ("phaser.phase_offset")),
            row ("OUTPUT", c ("phaser.mix"), c ("phaser.level")) } },
        // 9: Reverb (20 values + the force enable of the engine's tiers): the algorithm, the tail, the colour; the room and the feedback are live in the algorithms that have them
        Layout { synthyboy::kFxTypeReverb, "Reverb", 4, 0, 6, {
            row ("REVERB", c ("reverb.mix"), c ("reverb.size"), c ("reverb.decay"), c ("reverb.predelay")),
            row ("ALGORITHM", c ("reverb.type"), c ("reverb.width"), c ("reverb.er_size"), when ("reverb.room", "reverb.type", 1u << 4)),
            row ("DIFFUSION", c ("reverb.diff_a"), c ("reverb.diff_b"), c ("reverb.level"), when ("reverb.feedback", "reverb.type", (1u << 4) | (1u << 5))),
            row ("MODULATION AND INPUT", c ("reverb.chorus_amount"), c ("reverb.chorus_rate"), c ("reverb.pre_low_cut"), c ("reverb.pre_high_cut")),
            row ("DAMPING", c ("reverb.low_shelf_cutoff"), c ("reverb.low_shelf_gain"), c ("reverb.high_shelf_cutoff"), c ("reverb.high_shelf_gain")),
            row ("TIER", c ("reverb.force_enable", "FORCE ON")) } },
        // 10: Hyper / Dimension (8 values)
        Layout { synthyboy::kFxTypeHyperDimension, "Hyper/Dimension", 3, 0, 2, {
            row ("HYPER", c ("hyperdim.rate"), c ("hyperdim.unison"), c ("hyperdim.detune"), c ("hyperdim.retrig")),
            row ("DIMENSION AND OUTPUT", c ("hyperdim.dim_size"), c ("hyperdim.dim_mix"), c ("hyperdim.mix"), c ("hyperdim.level", "LEVEL")) } },
        // 11: Utility (10 values): the mono bass frequency is live while the mono bass is on
        Layout { synthyboy::kFxTypeUtility, "Utility", 3, 0, 3, {
            row ("POLARITY AND STEREO", c ("utility.polarity_l"), c ("utility.polarity_r"), c ("utility.width"), c ("utility.pan")),
            row ("FILTERS", c ("utility.lpf"), c ("utility.hpf"), c ("utility.mono_bass"), when ("utility.mono_freq", "utility.mono_bass", 1u << 1)),
            row ("OUTPUT", c ("utility.mix"), c ("utility.level")) } },
        // 12: Bode frequency shifter (14 values)
        Layout { synthyboy::kFxTypeBode, "Bode", 4, 0, 4, {
            row ("SHIFT", c ("bode.shift"), c ("bode.range"), c ("bode.dir"), c ("bode.balance")),
            row ("DELAY", rate ("bode.delay", "bode.division", "bode.sync", "DELAY", "DIVISION"), c ("bode.sync"), c ("bode.feed"), c ("bode.blur")),
            row ("STEREO", c ("bode.width"), c ("bode.mono_input"), c ("bode.retrig")),
            row ("OUTPUT", c ("bode.mix"), c ("bode.level")) } },
        // 13: Convolve (14 values)
        Layout { synthyboy::kFxTypeConvolve, "Convolve", 4, 0, 4, {
            row ("IMPULSE", c ("convolve.ir"), c ("convolve.size"), c ("convolve.tone"), c ("convolve.phi_min")),
            row ("TIME", rate ("convolve.predelay", "convolve.division", "convolve.sync", "PRE-DELAY", "PRE-DELAY"), c ("convolve.sync"), c ("convolve.attack"), c ("convolve.decay")),
            row ("TAIL", c ("convolve.damp"), c ("convolve.ir_gain"), c ("convolve.law")),
            row ("OUTPUT", c ("convolve.mix"), c ("convolve.level")) } },
        // 14: the splitters: the crossover frequencies, the band bypasses, the level
        Layout { kKindSplitterLh, "Splitter L/H", 1, 240, 1, {
            row ("SPLIT", c ("splitter_lh.freq", "SPLIT FREQUENCY"), c ("splitter_lh.low_bypass", "LOW BYPASS"), c ("splitter_lh.high_bypass", "HIGH BYPASS"), c ("splitter_lh.level", "LEVEL")) } },
        Layout { kKindSplitterLmh, "Splitter L/M/H", 2, 220, 2, {
            row ("SPLIT", c ("splitter_lmh.freq_low", "LOW SPLIT"), c ("splitter_lmh.freq_high", "HIGH SPLIT"), c ("splitter_lmh.level", "LEVEL")),
            row ("BYPASS", c ("splitter_lmh.low_bypass", "LOW BYPASS"), c ("splitter_lmh.mid_bypass", "MID BYPASS"), c ("splitter_lmh.high_bypass", "HIGH BYPASS")) } },
        Layout { kKindSplitterMs, "Splitter M/S", 1, 240, 1, {
            row ("MID / SIDE", c ("splitter_ms.mid_bypass", "MID BYPASS"), c ("splitter_ms.side_bypass", "SIDE BYPASS"), c ("splitter_ms.level", "LEVEL")) } },
    };
    return kAll[kind >= 0 && kind < kNumKinds ? kind : 0];
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
