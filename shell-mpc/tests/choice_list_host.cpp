// The plain-data half of the native choice lists (shell-mpc/native_choice_list.h): the slot table and its handle block, the per-slot debounce of the openers (one tap arrives as two press events), the
// session that opens on the page holding the current row, pages with PREVIOUS / NEXT, gives a row's value and closes on one tap, and closes on BACK. No plugin, no host.
// Usage: choice_list_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "native_choice_list.h"
#include "native_ui_params.h"
#include "warp_choice_groups.h"

namespace cl = synthyboy_mpc::nativeui::choicelist;
namespace nu = synthyboy_mpc::nativeui;
namespace wg = synthyboy_mpc::nativeui::warpgroups;

static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FAIL %s\n", what); ++fails; } }

static void fill (cl::Items& items, int count)
{
    items.clear();
    items.setTitle ("TEST");
    char name[16];
    for (int i = 0; i < count; ++i)
    {
        std::snprintf (name, sizeof name, "Item %d", i);
        items.add (name, count > 1 ? static_cast<float> (i) / static_cast<float> (count - 1) : 0.0f);
    }
}

int main()
{
    // ---- the table and the handle block
    {
        check (cl::kSlots == 162, "162 slots (113 + 3 warp + 4 osc + 6 lfo + 3 filter + 1 fx add + 32 fx rack)");
        check (cl::slotOfHandle (21) == 0 && cl::slotOfHandle (3801) == 112 && cl::slotOfHandle (3012) == 113
               && cl::slotOfHandle (3014) == 114 && cl::slotOfHandle (3442) == 115 && cl::slotOfHandle (22) == -1,
               "slotOfHandle finds existing and appended warp slots, -1 for no opener");
        check (cl::slotOfHandle (3004) == 116 && cl::slotOfHandle (2979) == 117 && cl::slotOfHandle (2986) == 118 && cl::slotOfHandle (2987) == 119,
               "slotOfHandle resolves Osc TYPE, WAVEFORM, KIND, SOUND");
        check (cl::slotOfHandle (2820) == 120 && cl::slotOfHandle (2821) == 121 && cl::slotOfHandle (2827) == 122
               && cl::slotOfHandle (2830) == 123 && cl::slotOfHandle (2831) == 124 && cl::slotOfHandle (2835) == 125,
               "slotOfHandle resolves LFO WAVEFORM, STYLE, DIVISION, MODE, DIRECTION");
        check (cl::slotOfHandle (3658) == 126 && cl::slotOfHandle (3659) == 127 && cl::slotOfHandle (3660) == 128,
               "slotOfHandle resolves Filter CHAIN handles");
        check (cl::slotOfHandle (3273) == 129, "slotOfHandle resolves FX ADD handle");
        bool fxRackOk = true;
        for (int i = 0; i < 32; ++i)
        {
            const int handle = 3061 + 4 * i;
            fxRackOk = fxRackOk && cl::slotOfHandle (handle) == 130 + i && cl::slotParameter (130 + i) == 4044 + i;
        }
        check (fxRackOk, "slotOfHandle and slotParameter resolve all 32 FX Rack handles");
        check (cl::slotParameter (0) == 3904 && cl::slotParameter (112) == 4016
               && cl::slotParameter (113) == 4027 && cl::slotParameter (115) == 4029
               && cl::slotParameter (116) == 4030 && cl::slotParameter (120) == 4034
               && cl::slotParameter (126) == 4040 && cl::slotParameter (129) == 4043
               && cl::slotParameter (161) == 4075,
               "slotParameter keeps 3904..4016 and appends tail 4027..4075 after liststate");
        check (nu::kChoiceListOpen == 3903 - 2719 && nu::kChoiceList == 3904 - 2719
               && nu::kListState == 4017 - 2719 && nu::kChoiceListTail == 4027 - 2719
               && nu::kCount == nu::kChoiceListTail + nu::kChoiceListTailSlots,
               "liststate stays at 4017..4026 and choice-list tail follows at 4027+");
        check (! nu::isTextGateOffset (nu::kChoiceListOpen) && nu::isChoiceListOffset (nu::kChoiceList) && nu::isChoiceListOffset (nu::kListState - 1)
               && ! nu::isChoiceListOffset (nu::kChoiceListOpen) && ! nu::isChoiceListOffset (nu::kListState)
               && nu::isChoiceListOffset (nu::kChoiceListTail) && nu::isChoiceListOffset (nu::kCount - 1) && ! nu::isChoiceListOffset (nu::kCount),
               "the predicates of both choice-list blocks");
        check (nu::choiceListSlotOfOffset (nu::kChoiceList) == 0 && nu::choiceListSlotOfOffset (nu::kListState - 1) == 112
               && nu::choiceListSlotOfOffset (nu::kChoiceListTail) == 113 && nu::choiceListSlotOfOffset (nu::kCount - 1) == 161,
               "choiceListSlotOfOffset maps both blocks to 0..161");
        check (nu::isListStateOffset (nu::kListState) && nu::isListStateOffset (nu::kChoiceListTail - 1)
               && ! nu::isListStateOffset (nu::kListState - 1) && ! nu::isListStateOffset (nu::kChoiceListTail)
               && nu::liststate::kHandles == 10, "the predicates of the list-state block");
        char rowBuf[32];
        nu::liststate::copyRowName (rowBuf, sizeof rowBuf, "Basic \xe2\x96\xb6");
        check (std::strcmp (rowBuf, "Basic") == 0, "copyRowName strips trailing folder/category arrow");
        nu::liststate::copyRowName (rowBuf, sizeof rowBuf, "Sine");
        check (std::strcmp (rowBuf, "Sine") == 0, "copyRowName keeps plain row names unchanged");
    }
    // ---- the port: one tap is one request, latest wins, per-slot debounce
    {
        cl::RequestPort port;
        check (port.take() == -1, "nothing pending at first");
        port.post (5, 1000.0);
        port.post (5, 1080.0);   // the second press event of the same tap
        check (port.take() == 5, "the first press is taken");
        check (port.take() == -1, "the second press inside the window is dropped");
        port.post (5, 1300.0);
        check (port.take() == 5, "a new tap after the window is a new request");
        port.post (7, 2000.0);
        port.post (9, 2010.0);
        check (port.take() == 9, "another slot replaces the pending one (latest wins)");
        port.post (9, 2100.0);
        check (port.take() == -1, "the debounce is per slot: the second press of slot 9 is dropped");
        port.post (7, 2100.0);
        check (port.take() == -1, "slot 7 was taken at 2000 (then replaced): its own window still drops it");
        port.post (7, 2300.0);
        check (port.take() == 7, "slot 7 after its own window is a new request");
        port.post (-1, 3000.0);
        port.post (cl::kSlots, 3000.0);
        check (port.take() == -1, "slots out of range are ignored");
        port.post (3, 4000.0);
        port.clear();
        check (port.take() == -1, "clear forgets what is pending");
        port.post (3, 4100.0);
        check (port.take() == -1, "clear keeps the debounce time: the late second press of a tap stays one tap");
    }
    // ---- items: the cap and the signature
    {
        auto items = std::make_unique<cl::Items>();
        fill (*items, cl::kMaxItems);
        check (items->count == cl::kMaxItems && ! items->add ("x", 0.0f) && items->count == cl::kMaxItems, "a full list drops a further row");
        const uint32_t a = items->signature();
        fill (*items, 10);
        const uint32_t b = items->signature();
        fill (*items, 10);
        check (items->signature() == b && a != b, "the same rows give the same signature, others another");
        items->values[3] = 0.5f;
        check (items->signature() != b, "a value change changes the signature");
        char longName[200];
        std::memset (longName, 'a', sizeof longName - 1);
        longName[sizeof longName - 1] = '\0';
        items->add (longName, 1.0f);
        check (std::strlen (items->names[items->count - 1]) == static_cast<size_t> (cl::kNameLen - 1), "a long name is cut to the row length");
    }
    // ---- the session: open on the page of the current row, paging, row -> value + close, BACK
    {
        auto session = std::make_unique<cl::Session>();
        fill (session->items, 0);
        check (! session->open (4, 0) && ! session->isOpen(), "an empty list does not open");
        fill (session->items, 5);    // single page: no prev, no next
        check (session->open (1, 0) && ! session->deeperLevel() && ! session->hasPrevious() && ! session->hasNext(), "single-page choice list: not deeper, no prev, no next");
        for (int r = 0; r < cl::kRows; ++r)
            check (! session->rowOpensLevel (r), "choice list rows are always final");
        fill (session->items, 17);   // pages of 7: 0..6, 7..13, 14..16
        check (session->open (4, 0) && ! session->hasPrevious() && session->hasNext(), "first page of 3: no prev, has next");
        check (session->open (4, 9) && session->isOpen() && session->slot() == 4, "opens for its slot");
        check (session->first() == 7 && session->rowsOnPage() == 7 && std::strcmp (session->rowName (2), "Item 9") == 0, "opens on the page that holds the current row");
        check (session->hasPrevious() && session->hasNext() && ! session->deeperLevel(), "middle page of 3: has prev, has next, not deeper");
        check (session->next() && session->first() == 14 && session->rowsOnPage() == 3, "NEXT: the last page holds the rest");
        check (session->hasPrevious() && ! session->hasNext(), "last page of 3: has prev, no next");
        check (! session->next() && session->first() == 14, "no page after the last one");
        check (std::strcmp (session->rowName (3), "") == 0, "a row past the end shows nothing");
        float value = -1.0f;
        check (! session->choose (3, value) && session->isOpen() && value == -1.0f, "a tap on an empty row does nothing");
        check (session->previous() && session->previous() && session->first() == 0 && ! session->previous(), "PREVIOUS down to the first page, not past it");
        check (! session->hasPrevious() && session->hasNext(), "back to first page: no prev, has next");
        check (session->choose (5, value) && value == 5.0f / 16.0f && ! session->isOpen() && session->slot() == -1, "one tap on a row gives its value and closes");
        check (! session->hasPrevious() && ! session->hasNext(), "closed list has no prev/next");
        check (! session->choose (0, value), "a closed list takes no row");
        check (session->open (2, 99) && session->first() == 0, "a current row out of range opens the first page");
        check (session->open (2, -1) && session->first() == 0, "no current row opens the first page");
        check (session->open (2, 16) && session->first() == 14, "the current last row opens the last page");
        session->close();   // BACK
        check (! session->isOpen() && session->rowsOnPage() == 0, "BACK closes");
        check (! session->open (cl::kSlots, 0), "a slot out of range does not open");
    }
    // ---- two-level WARP group table, spectral availability, selection, reopening and back-to-page
    {
        check (wg::isValidGroupTable() && wg::kGroupCount == 10, "warp group table partitions 1..98 into 10 groups exactly once");
        int counts[wg::kNumWarpTypes] {};
        for (int g = 0; g < wg::kGroupCount; ++g)
            for (int e = 0; e < wg::kGroups[g].count; ++e)
                ++counts[wg::kGroups[g].entries[e]];
        bool exactOnce = counts[0] == 0;
        for (int i = 1; i < wg::kNumWarpTypes; ++i)
            exactOnce = exactOnce && counts[i] == 1;
        check (exactOnce, "runtime verification: union of warp groups is 1..98 with no duplicates (0 is None)");

        auto warpName = [] (int idx) -> const char*
        {
            static char buf[wg::kNumWarpTypes][24];
            if (idx == 0)
                return "None";
            std::snprintf (buf[idx], sizeof buf[idx], "Warp %d", idx);
            return buf[idx];
        };
        auto warpValue = [] (int idx) -> float
        {
            return static_cast<float> (idx) / static_cast<float> (wg::kNumWarpTypes - 1);
        };

        auto session = std::make_unique<cl::Session>();
        const int warpSlot = cl::slotOfHandle (3012);

        // Spectral oscillator (spectralAvailable == true): None + all 10 groups
        check (wg::populateWarpItems (session->items, warpName, warpValue, [] (int) { return true; }), "populateWarpItems succeeds with spectral");
        check (session->open (warpSlot, 0) && session->isOpen(), "WARP opens at None");
        check (std::strcmp (session->title(), "WARP") == 0 && ! session->deeperLevel(), "Level 1 title is WARP");
        check (session->first() == 0 && session->rowsOnPage() == 7 && ! session->hasPrevious() && session->hasNext(), "Level 1 page 1 of 2");
        check (std::strcmp (session->rowName (0), "None") == 0 && ! session->rowOpensLevel (0), "row 0 is final None");
        check (std::strcmp (session->rowName (1), "Sync + Shape") == 0 && session->rowOpensLevel (1), "row 1 is Sync + Shape with arrow");
        check (std::strcmp (session->rowName (6), "FM") == 0 && session->rowOpensLevel (6), "row 6 is FM with arrow");
        check (session->next() && session->first() == 7 && session->rowsOnPage() == 4 && session->hasPrevious() && ! session->hasNext(), "Level 1 page 2 holds PD, AM + RM, Spectral, Mask + Vocode");
        check (std::strcmp (session->rowName (2), "Spectral") == 0 && session->rowOpensLevel (2), "Spectral on page 2");
        check (std::strcmp (session->rowName (3), "Mask + Vocode") == 0 && session->rowOpensLevel (3), "Mask + Vocode on page 2");

        // Enter Mask + Vocode (page 2 row 3) -> Level 2 -> header back arrow returns to Level 1 page 2
        float val = -1.0f;
        check (! session->choose (3, val) && session->isOpen() && session->deeperLevel(), "tapping group enters Level 2 without closing");
        check (std::strcmp (session->title(), "Mask + Vocode") == 0 && session->rowsOnPage() == 6 && ! session->rowOpensLevel (0), "Mask + Vocode has 6 final rows");
        check (std::strcmp (session->rowName (0), "Warp 93") == 0 && std::strcmp (session->rowName (5), "Warp 98") == 0, "Mask + Vocode entries 93..98");
        check (session->up() && ! session->deeperLevel() && std::strcmp (session->title(), "WARP") == 0 && session->first() == 7,
               "header back arrow returns to Level 1 on page 2 holding Mask + Vocode");

        // Choose None on page 1 -> yields index 0 and closes
        check (session->previous() && session->first() == 0, "back to Level 1 page 1");
        check (session->choose (0, val) && val == 0.0f && ! session->isOpen(), "choosing None yields 0 and closes");

        // Choose an entry from Sync + Shape (row 1: entries 1, 11, 2, 3, 5, 6, 18, 19)
        check (session->open (warpSlot, 0), "reopen at None");
        check (! session->choose (1, val) && std::strcmp (session->title(), "Sync + Shape") == 0 && session->rowsOnPage() == 7 && session->hasNext(),
               "Sync + Shape opens at page 1 of 2");
        check (session->next() && session->rowsOnPage() == 1 && std::strcmp (session->rowName (0), "Warp 19") == 0, "Sync + Shape page 2 holds 19 Mirror");
        check (session->choose (0, val) && std::fabs (val - 19.0f / 98.0f) < 1.0e-6f && ! session->isOpen(), "choosing entry yields its table index and closes");

        // Reopening lands on the group and page of the current value
        check (session->open (warpSlot, 63) && session->deeperLevel() && std::strcmp (session->title(), "FM") == 0
               && session->first() == 14 && std::strcmp (session->rowName (5), "Warp 63") == 0,
               "reopen at 63 (FM Linear from Filter 2) lands in FM on page 3");
        check (session->open (warpSlot, 85) && session->deeperLevel() && std::strcmp (session->title(), "Spectral") == 0
               && session->first() == 7 && std::strcmp (session->rowName (2), "Warp 85") == 0,
               "reopen at 85 lands in Spectral on page 2");

        const uint32_t specSig = session->signature();

        // Spectral-less oscillator (no Spectral / Mask + Vocode group)
        check (wg::populateWarpItems (session->items, warpName, warpValue, [] (int idx) { return idx < wg::kFirstSpectralWarp; }),
               "populateWarpItems succeeds without spectral");
        check (session->open (warpSlot, 0) && session->signature() != specSig, "spectral-less signature differs");
        check (session->items.rootCount == 9 && session->items.groupCount == 8, "spectral-less Level 1 has None + 8 non-spectral groups");
        check (session->next() && session->first() == 7 && session->rowsOnPage() == 2
               && std::strcmp (session->rowName (0), "PD") == 0 && std::strcmp (session->rowName (1), "AM + RM") == 0,
               "spectral-less Level 1 page 2 ends at AM + RM (no Spectral / Mask + Vocode)");
        check (session->open (warpSlot, 85) && ! session->deeperLevel() && session->first() == 0,
               "reopen at unavailable spectral warp on spectral-less oscillator falls back to Level 1 page 1");
    }
    // ---- migrated flat choice lists (Osc TYPE/WAVEFORM/KIND/SOUND, LFO, Filter CHAIN, FX RACK/ADD)
    {
        auto session = std::make_unique<cl::Session>();
        float val = -1.0f;

        // Osc TYPE (slot 116 -> 3004): matches kTypeChoices { 0, 4, 1, 2, 3 }
        static constexpr const char* kOscTypes[5] = { "Wavetable", "Sample", "Granular", "Spectral", "Multisample" };
        session->items.clear();
        session->items.setTitle ("TYPE");
        for (int i = 0; i < 5; ++i)
            session->items.add (kOscTypes[nu::osc::kTypeChoices[i]], static_cast<float> (i) / 4.0f);
        check (session->open (cl::slotOfHandle (3004), 1) && std::strcmp (session->rowName (1), "Multisample") == 0,
               "Osc TYPE lists Multisample at index 1 in pictogram order");
        check (session->choose (1, val) && std::fabs (val - 0.25f) < 1.0e-6f && ! session->isOpen(),
               "choosing Osc TYPE Multisample yields 0.25 and closes");

        // Sub WAVEFORM (2979), Noise KIND (2986), Noise SOUND (2987)
        session->items.clear();
        session->items.setTitle ("SOUND");
        static constexpr const char* kNoiseSounds[9] = {
            "White Noise", "Pink Noise", "Brown Noise", "Geiger", "Tape Hiss", "Vinyl Clicks", "Digital Hash", "Velvet Noise", "Atmosphere"
        };
        for (int i = 0; i < 9; ++i)
            session->items.add (kNoiseSounds[i], static_cast<float> (i) / 8.0f);
        check (session->open (cl::slotOfHandle (2987), 8) && session->first() == 7 && std::strcmp (session->rowName (1), "Atmosphere") == 0,
               "Noise SOUND opens on page 2 for Atmosphere");
        check (session->choose (1, val) && std::fabs (val - 1.0f) < 1.0e-6f && ! session->isOpen(),
               "choosing Noise SOUND Atmosphere yields 1.0 and closes");

        // LFO WAVEFORM (2820): 13th entry reads "Drawn" (matching effGetParamDisplay)
        session->items.clear();
        session->items.setTitle ("WAVEFORM");
        for (int i = 0; i <= nu::lfo::kShapeLast; ++i)
            session->items.add (i == nu::lfo::kShapeLast ? "Drawn" : "Wave", static_cast<float> (i) / nu::lfo::kShapeLast);
        check (session->open (cl::slotOfHandle (2820), 12) && session->first() == 7 && std::strcmp (session->rowName (5), "Drawn") == 0,
               "LFO WAVEFORM opens on page 2 with Drawn");
        check (session->choose (5, val) && std::fabs (val - 1.0f) < 1.0e-6f && ! session->isOpen(),
               "choosing LFO Drawn yields 1.0 and closes");

        // Filter CHAIN (3658/3659/3660): matches kFilterChainNames
        session->items.clear();
        session->items.setTitle ("FILTER CHAIN");
        for (int i = 0; i < nu::kFilterChainRoutes; ++i)
            session->items.add (nu::kFilterChainNames[i], static_cast<float> (i) / nu::kFilterChainLastChoice);
        check (session->open (cl::slotOfHandle (3659), 1) && std::strcmp (session->rowName (1), "F1 to F2") == 0,
               "Filter CHAIN opens with F1 to F2");
        check (session->choose (1, val) && std::fabs (val - 0.5f) < 1.0e-6f && ! session->isOpen(),
               "choosing Filter CHAIN F1 to F2 yields 0.5 and closes");

        // FX Rack (3061..3185) and FX Add (3273)
        session->items.clear();
        session->items.setTitle ("RACK");
        static constexpr const char* kRacks[10] = { "MAIN", "BUS 1", "BUS 2", "L/H LOW", "L/H HIGH", "L/M/H LOW", "L/M/H MID", "L/M/H HIGH", "M/S MID", "M/S SIDE" };
        for (int r = 0; r < 10; ++r)
            session->items.add (kRacks[r], static_cast<float> (r) / 9.0f);
        check (session->open (cl::slotOfHandle (3061), 9) && session->first() == 7 && std::strcmp (session->rowName (2), "M/S SIDE") == 0,
               "FX Rack opens on page 2 for M/S SIDE");
        check (session->choose (2, val) && std::fabs (val - 1.0f) < 1.0e-6f && ! session->isOpen(),
               "choosing FX Rack M/S SIDE yields 1.0 and closes");

        session->items.clear();
        session->items.setTitle ("ADD EFFECT");
        // Offered bitmask with Chorus (1) and Splitter M/S (16) enabled
        const uint32_t offered = (1u << 0) | (1u << 15);
        for (int k = 1; k <= 16; ++k)
            if ((offered & (1u << (k - 1))) != 0)
                session->items.add (k == 1 ? "Chorus" : "Splitter M/S", static_cast<float> (k) / 16.0f);
        check (session->open (cl::slotOfHandle (3273), 0) && session->rowsOnPage() == 2 && std::strcmp (session->rowName (1), "Splitter M/S") == 0,
               "FX Add lists only offered effects");
        check (session->choose (1, val) && std::fabs (val - 1.0f) < 1.0e-6f && ! session->isOpen(),
               "choosing FX Add Splitter M/S yields 16/16 and closes");
    }
    if (fails == 0)
        std::printf ("choice_list_host: all checks passed\n");
    return fails == 0 ? 0 : 1;
}
