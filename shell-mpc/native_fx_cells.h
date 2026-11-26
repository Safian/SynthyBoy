// SynthyBoy - native FX controls, between the selected overlay editor and the VST skin.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "native_ui_params.h"
#include "synthyboy/params.h"
#include "synthyboy/filters/filter_types.h"

#include <cstdint>
#include <cstring>

namespace synthyboy_mpc::nativeui
{
struct FxLayout
{
    int top, rows;   // 1280 x 628 tab coordinates, before the Q-Link lane
    // side = 1: DISPLAY LEFT, CONTROLS RIGHT. The cells stand in two columns at x = kFxSideX, two per line; a row of `lines[row]`
    // lines has the Q lane above it (kFxSideLane) and `kFxSideLine` px per line, the rows follow each other from `top`.
    int side = 0;
    int lines[4] = {};
};
constexpr int kFxSideX = 644, kFxSideStride = 318, kFxSideLane = 16, kFxSideLine = 72, kFxSideCellW = 310;
/** The height of a cell of a STACKED layout of rows `rowH` apart: the row under its 16 px Q lane, at least 72 and at most kFxCellMaxH px (FX screen review 2026-11-26: a page of one or two rows
    no longer stretches a switch into a box of 190 px; the rows keep their pitch, the cell stands at the top of its row). The skin reads the cap from the contract ("cell_max_h"). */
constexpr int kFxCellMaxH = 104;
constexpr int fxStackedCellH (int rowH) noexcept { return rowH - kFxSideLane < 72 ? 72 : (rowH - kFxSideLane > kFxCellMaxH ? kFxCellMaxH : rowH - kFxSideLane); }
/** Tab y of the first cell line of `row` of a side layout. */
constexpr int fxSideRowY (const FxLayout& g, int row) noexcept
{
    int y = g.top;
    for (int r = 0; r < row; ++r)
        y += kFxSideLane + kFxSideLine * g.lines[r];
    return y + kFxSideLane;
}
constexpr FxLayout kFxLayouts[] = {
    { 208, 2 }, { 208, 4 }, { 404, 2 }, { 360, 2 },
    { 344, 2 }, { 344, 3 }, { 390, 1 }, { 424, 2 },
    { 208, 3 }, { 272, 2 }, { 272, 3 }, { 408, 2 }, { 424, 3 }, { 272, 1 },
    { 208, 3, 1, { 2, 2, 1, 0 } },   // Distortion: the waveshaper at the left, TYPE / DRIVE / MIX / LEVEL, FILTER ORDER / CUTOFF / RESONANCE, BLEND / KEYTRACK at the right
    // the side layouts of the FX screen review (2026-11-26): a side layout is named by the lines of the rows on the panel now (FxSlotEditor::nativeLayout)
    { 208, 2, 1, { 2, 2, 0, 0 } },   // Distortion without its filter (the FILTER SHAPE row is gone)
    { 272, 2, 1, { 2, 2, 0, 0 } },   // under the tabs, two blocks of four: Compressor MAIN / BANDS, Delay MAIN, EQ MAIN / TONE, FX Filter MAIN
    { 272, 2, 1, { 2, 1, 0, 0 } },   // Compressor UPWARD (multiband), FX Filter TONE (every model but Formant)
    { 272, 2, 1, { 1, 1, 0, 0 } },   // Delay OUTPUT (LEVEL / HQ, OFFSET L / R)
    { 272, 1, 1, { 2, 0, 0, 0 } },   // EQ MID 2, Compressor UPWARD of a single band
    { 408, 1 },                      // EQ MID 2 stacked (the docked Mixer peek)
};
static_assert (sizeof kFxLayouts / sizeof *kFxLayouts == kFxLayoutLast);

// Each slot refers to a canonical choice list. The right delay has a dedicated
// handle because the two divisions can be visible and editable simultaneously.
struct FxChoiceDomain { int id; const char* key; };
constexpr FxChoiceDomain kFxChoiceDomains[] = {
    { kFxModel, "fxfilter.model" },
    { kFxSync, "chorus.sync" }, { kFxSync, "delay.sync" }, { kFxSync, "flanger.sync" },
    { kFxSync, "phaser.sync" }, { kFxSync, "bode.sync" }, { kFxSync, "convolve.sync" },
    { kFxLongDivision, "chorus.division" }, { kFxLongDivision, "flanger.division" }, { kFxLongDivision, "phaser.division" },
    { kFxBands, "compressor.bands" },
    { kFxShortDivision, "delay.division" }, { kFxShortDivision, "bode.division" }, { kFxShortDivision, "convolve.division" },
    { kFxDelayRightDivision, "delay.division2" }, { kFxDelayStyle, "delay.style" },
    { kFxDistortion, "distortion.type" }, { kFxFilterOrder, "distortion.filter_order" },
    { kFxLowMode, "eq.low.mode" }, { kFxMidMode, "eq.mid.mode" }, { kFxHighMode, "eq.high.mode" },
    { kFxConvolveIr, "convolve.ir" }, { kFxCompressorMode, "compressor.limit" },
    { kFxReverbType, "reverb.type" }, { kFxReverbRoom, "reverb.room" },
    { kFxMid2Mode, "eq.mid2.mode" }, { kFxLaw, "eq.law" }, { kFxConvolveLaw, "convolve.law" },
    { kFxVoices, "chorus.voices" }, { kFxUnison, "hyperdim.unison" },
};

inline int fxDomainKind (const synthyboy::ParamInfo& source, int styleModel) noexcept
{
    if (std::strcmp (source.key, "fxfilter.style") == 0)
        return kFxFirstDomain + kFxStyleBase + std::clamp (styleModel, 0, kFxStyleModels - 1);
    for (const auto& domain : kFxChoiceDomains)
        if (std::strcmp (source.key, domain.key) == 0)
            return kFxFirstDomain + domain.id;
    return kFxHidden;
}

struct FxCell
{
    int target = -1;     // engine parameter of the actual selected instance
    int source = -1;     // canonical parameter format of the selected effect type
    int kind = kFxHidden;
};

inline int fxStyleModel (int kind) noexcept
{
    const int model = kind - kFxFirstDomain - kFxStyleBase;
    return model >= 0 && model < kFxStyleModels ? model : -1;
}

inline const synthyboy::ParamInfo* fxDomainSource (int domain) noexcept
{
    if (domain >= kFxStyleBase && domain < kFxDomains)
        return synthyboy::findParamByKey ("fxfilter.style");
    for (const auto& item : kFxChoiceDomains)
        if (item.id == domain)
            return synthyboy::findParamByKey (item.key);
    return nullptr;
}

inline float fxUiValue (const FxCell& cell, float engineNormalised) noexcept
{
    const int model = fxStyleModel (cell.kind);
    if (model < 0)
        return engineNormalised;
    const int last = synthyboy::filters::filterModelStyleCount (model) - 1;
    const auto& pi = synthyboy::paramTable()[cell.source];
    const float plain = synthyboy::fromNormalised (pi, engineNormalised);
    return last > 0 ? std::clamp (static_cast<float> (std::lround (plain)) / last, 0.0f, 1.0f) : 0.0f;
}

inline float fxEngineValue (const FxCell& cell, float hostNormalised) noexcept
{
    const auto& pi = synthyboy::paramTable()[cell.source];
    const int model = fxStyleModel (cell.kind);
    const float plain = model >= 0 ? std::round (hostNormalised * (synthyboy::filters::filterModelStyleCount (model) - 1))
                                  : synthyboy::fromNormalised (pi, hostNormalised);
    return synthyboy::toNormalised (pi, synthyboy::constrainValue (pi, plain));
}

inline bool fxCellProxy (int kind) noexcept { return kind == kFxDial || kind == kFxToggle || kind == kFxBipolar; }

/** One writer publishes the editor on the overlay thread. VST input is queued
    with the exact reported cell revision and can only be applied by that editor.
    Each bound value is atomically published with its canonical format and kind. */
template <int CellCount>
class NativeCellBank
{
public:
    struct Snapshot
    {
        uint64_t link = 0;
        int slot = -1;
        FxCell cell() const noexcept { return decode (static_cast<uint32_t> (link)); }
    };

    void publish (int layout, const FxCell* cells) noexcept
    {
        if (layout_.exchange (layout, std::memory_order_relaxed) != layout)
            invalidate();
        for (int i = 0; i < CellCount; ++i)
        {
            const uint32_t code = layout > 0 && cells != nullptr ? encode (cells[i]) : 0u;
            const uint64_t old = links_[i].load (std::memory_order_relaxed);
            if (code != static_cast<uint32_t> (old))
                links_[i].store (next (old, code), std::memory_order_release);
        }
    }

    void invalidate () noexcept
    {
        for (int i = 0; i < CellCount; ++i)
        {
            const uint64_t old = links_[i].load (std::memory_order_relaxed);
            links_[i].store (next (old, 0), std::memory_order_release);
        }
    }

    // A returning tab can reuse its display bindings while invalidating gestures from
    // the previous visit. An unreported binding still needs its normal first report.
    void renewInputRevisions() noexcept
    {
        for (int i = 0; i < CellCount; ++i)
        {
            auto old = links_[i].load (std::memory_order_relaxed);
            const auto renewed = next (old, static_cast<uint32_t> (old));
            links_[i].store (renewed, std::memory_order_release);
            reported_[i].compare_exchange_strong (old, renewed, std::memory_order_acq_rel);
        }
    }

    int layout() const noexcept { return layout_.load (std::memory_order_relaxed); }
    Snapshot at (int slot) const noexcept
    {
        return validSlot (slot) ? Snapshot { links_[slot].load (std::memory_order_acquire), slot } : Snapshot {};
    }
    Snapshot domain (int kind) const noexcept
    {
        for (int i = 0; i < CellCount; ++i)
        {
            const auto snapshot = at (i);
            if (snapshot.cell().kind == kind)
                return snapshot;
        }
        return {};
    }

    bool report (Snapshot current) noexcept
    {
        return validSlot (current.slot) && reported_[current.slot].exchange (current.link, std::memory_order_release) != current.link;
    }
    bool needsReport (Snapshot current) const noexcept
    {
        return validSlot (current.slot) && reported_[current.slot].load (std::memory_order_acquire) != current.link;
    }

    void post (Snapshot current, float value) noexcept
    {
        const FxCell cell = current.cell();
        const int model = fxStyleModel (cell.kind);
        if (! validSlot (current.slot) || cell.target < 0 || ! std::isfinite (value)
            || (model >= 0 && synthyboy::filters::filterModelStyleCount (model) <= 1)
            || reported_[current.slot].load (std::memory_order_acquire) != current.link)
            return;
        value = std::clamp (value, 0.0f, 1.0f);
        uint32_t bits;
        std::memcpy (&bits, &value, sizeof bits);
        pending_[current.slot].store ((current.link & kRevisionMask) | bits, std::memory_order_release);
    }

    bool take (int slot, const FxCell& expected, float& value) noexcept
    {
        if (! validSlot (slot))
            return false;
        const uint64_t pending = pending_[slot].exchange (0, std::memory_order_acq_rel);
        const auto active = at (slot);
        if (pending == 0 || expected.target < 0 || encode (expected) != static_cast<uint32_t> (active.link)
            || (pending & kRevisionMask) != (active.link & kRevisionMask))
            return false;
        const auto bits = static_cast<uint32_t> (pending);
        std::memcpy (&value, &bits, sizeof bits);
        return true;
    }

private:
    static bool validSlot (int slot) noexcept { return slot >= 0 && slot < CellCount; }
    static constexpr int kIndexBits = 13;
    static constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1;
    static constexpr uint64_t kRevisionMask = 0xffffffff00000000ull;
    static_assert (synthyboy::kMaxParams <= kIndexMask && kFxKindLast < 64);
    static uint32_t encode (const FxCell& cell) noexcept
    {
        if (cell.kind == kFxHidden || cell.target < 0 || cell.source < 0)
            return 0;
        return (static_cast<uint32_t> (cell.kind) << (2 * kIndexBits))
             | (static_cast<uint32_t> (cell.source + 1) << kIndexBits)
             | static_cast<uint32_t> (cell.target + 1);
    }
    static FxCell decode (uint32_t code) noexcept
    {
        return { static_cast<int> (code & kIndexMask) - 1,
                 static_cast<int> ((code >> kIndexBits) & kIndexMask) - 1,
                 static_cast<int> (code >> (2 * kIndexBits)) };
    }
    static uint64_t next (uint64_t old, uint32_t code) noexcept
    {
        return ((old & kRevisionMask) + (1ull << 32)) | code;
    }

    std::atomic<int> layout_ { 0 };
    std::atomic<uint64_t> links_[CellCount] {}, reported_[CellCount] {}, pending_[CellCount] {};
};
using FxCellBank = NativeCellBank<kFxCells>;
} // namespace synthyboy_mpc::nativeui
