// SynthyBoy - MPC UI component library tests: data consistency tests for the FX page tables (ui/fx_layout.h, ui/choice_names.h).
// Checks for every effect table (13 effect types + 3 splitters):
// 1. Every key in the table exists in the parameter table (synthyboy::paramTable / the same lookup the pages use).
// 2. No key appears twice in one effect's table.
// 3. Every parameter of that effect in the parameter table is in the table or in an explicit, commented list of chain-level keys (on / rack / order / type).
// 4. Every row has 1..4 Q members and the Q groups of a page are in reading order.
// 5. Every label fits its cell at the page's font (using Pen::textWidth / sdf::textWidth).
// 6. Every choice shown through choice_names has a non-empty shown name and no shown name contains a third-party product name (Vital, Serum, Massive, Sylenth, Diva, Pigments).
// 7. The hide/dim conditions reference existing keys.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "choice_names.h"
#include "fx_layout.h"
#include "fx_slot_editor.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"
#include "ui_test.h"

using namespace uit;
using namespace synthyboy_mpc::ui;
namespace ui = synthyboy_mpc::ui;
namespace fxl = synthyboy_mpc::ui::fxl;

namespace
{

/** The prefix of the effect kind: the 13 pool types use their descriptor, the splitters their module prefix. */
const char* prefixOfKind (int kind) noexcept
{
    if (kind >= 1 && kind < synthyboy::kNumFxInstanceTypes)
        return synthyboy::fxTypeDescriptor (kind).prefix;
    if (kind == fxl::kKindSplitterLh)
        return "splitter_lh";
    if (kind == fxl::kKindSplitterLmh)
        return "splitter_lmh";
    if (kind == fxl::kKindSplitterMs)
        return "splitter_ms";
    return "";
}

/**
 * Explicit, commented list of chain-level keys (on / rack / order / type) that sit outside
 * the individual effect layout tables:
 * - <prefix>.on: module bypass/power toggle (instance 0 module on/off, operated on chain cards)
 * - <prefix>.rack: rack assignment (Main, Bus 1, Bus 2, or Splitter band racks 3..9)
 * - fx.order.*: chain reorder positions (fx.order.1 .. fx.order.16 for instance 0 modules)
 * - fxiNN.type: pool slot effect type selection choice (0 = None, 1..13 = effect type)
 * - fxiNN.on: pool slot bypass/power toggle
 * - fxiNN.rack: pool slot rack assignment
 * - fxiNN.order: pool slot sort position
 */
bool isChainLevelKey (const char* prefix, const std::string& key)
{
    const std::string pre = prefix;
    if (key == pre + ".on" || key == pre + ".rack")
        return true;
    if (key.rfind ("fx.order.", 0) == 0)
        return true;
    if (key.size() >= 7 && key.rfind ("fxi", 0) == 0)
    {
        if (key.find (".type") != std::string::npos || key.find (".on") != std::string::npos
            || key.find (".rack") != std::string::npos || key.find (".order") != std::string::npos)
            return true;
    }
    return false;
}

/** Checks whether a string contains any third-party synthesizer product name (case-insensitive). */
bool containsThirdPartyName (const std::string& str)
{
    static const char* const kBadWords[] = {
        "vital", "serum", "massive", "sylenth", "diva", "pigments"
    };
    std::string lower = str;
    for (char& c : lower)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    for (const char* bad : kBadWords)
        if (lower.find (bad) != std::string::npos)
            return true;
    return false;
}

// Cell width on the MPC FX screen: 1280 px screen width, 8 px margin left/right,
// 4 cells with 8 px gaps in between:
// knobArea.w = 1280 - 2 * 8 = 1264
// cellW = (1264 - 3 * 8) / 4 = 1240 / 4 = 310 px.
constexpr int kCellW = 310;

// -------------------------------------------------------------------------------------------------------------------------------
// Check 1: Every key in the table exists in the parameter table (synthyboy::paramTable / the same lookup the pages use)
void testEveryKeyExistsInParamTable()
{
    const synthyboy::ParamInfo* table = synthyboy::paramTable();
    const size_t totalParams = synthyboy::paramCount();

    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;
        int checkedKeys = 0;
        bool allExist = true;

        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (int a = 0; a < 2; ++a)
                {
                    const char* k = lay.row[r].cell[c].alt[a].key;
                    if (k == nullptr)
                        continue;
                    ++checkedKeys;
                    const synthyboy::ParamInfo* pi = synthyboy::findParamByKey (k);
                    if (pi == nullptr)
                    {
                        allExist = false;
                        check (false, name + ": key '" + k + "' not found in parameter table");
                        continue;
                    }
                    // verify it maps to the exact table index
                    const size_t idx = static_cast<size_t> (pi - table);
                    const bool inTable = idx < totalParams && std::strcmp (table[idx].key, k) == 0;
                    allExist = allExist && inTable;
                }

        check (allExist && checkedKeys > 0, name + ": every key (" + std::to_string (checkedKeys) + ") exists in synthyboy::paramTable");
    }
}

// -------------------------------------------------------------------------------------------------------------------------------
// Check 2: No key appears twice in one effect's table
void testNoDuplicateKeysInTable()
{
    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;
        std::set<std::string> seen;
        bool allUnique = true;
        int count = 0;

        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (int a = 0; a < 2; ++a)
                {
                    const char* k = lay.row[r].cell[c].alt[a].key;
                    if (k == nullptr)
                        continue;
                    ++count;
                    if (! seen.insert (k).second)
                    {
                        allUnique = false;
                        check (false, name + ": key '" + k + "' appears multiple times in table");
                    }
                }

        check (allUnique && count > 0, name + ": no key appears twice in table (" + std::to_string (count) + " keys)");
    }
}

// -------------------------------------------------------------------------------------------------------------------------------
// Check 3: Every parameter of that effect in the parameter table is in the table or in an explicit, commented list of chain-level keys (on / rack / order / type)
void testParametersCoveredOrChainLevel()
{
    const synthyboy::ParamInfo* table = synthyboy::paramTable();
    const size_t totalParams = synthyboy::paramCount();

    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;
        const char* prefix = prefixOfKind (kind);
        check (prefix[0] != '\0', name + ": has known prefix");

        // Collect all keys defined in this effect's layout table
        std::set<std::string> tableKeys;
        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (int a = 0; a < 2; ++a)
                {
                    const char* k = lay.row[r].cell[c].alt[a].key;
                    if (k != nullptr)
                        tableKeys.insert (k);
                }

        const std::string expPrefix = std::string (prefix) + ".";
        int matchedInEngine = 0;
        int inTableCount = 0;
        int chainLevelCount = 0;
        bool allAccountedFor = true;

        for (size_t i = 0; i < totalParams; ++i)
        {
            if (table[i].key == nullptr)
                continue;
            const std::string k = table[i].key;
            if (k.rfind (expPrefix, 0) == 0)   // key starts with <prefix>.
            {
                ++matchedInEngine;
                const bool inTable = tableKeys.count (k) > 0;
                const bool isChain = isChainLevelKey (prefix, k);

                if (inTable)
                    ++inTableCount;
                if (isChain)
                    ++chainLevelCount;

                if (! inTable && ! isChain)
                {
                    allAccountedFor = false;
                    check (false, name + ": engine parameter '" + k + "' is neither in table nor in explicit chain-level keys list");
                }
                if (inTable && isChain)
                {
                    allAccountedFor = false;
                    check (false, name + ": chain-level key '" + k + "' must not appear in effect parameter table");
                }
            }
        }

        // Also verify that every key in tableKeys belongs to this effect's prefix
        bool allKeysBelong = true;
        for (const std::string& tk : tableKeys)
            if (tk.rfind (expPrefix, 0) != 0)
            {
                allKeysBelong = false;
                check (false, name + ": table contains foreign key '" + tk + "' not starting with '" + expPrefix + "'");
            }

        check (allAccountedFor && allKeysBelong && matchedInEngine > 0,
               name + ": every engine parameter (" + std::to_string (matchedInEngine) + ") is in table ("
               + std::to_string (inTableCount) + ") or chain-level (" + std::to_string (chainLevelCount) + ")");
    }
}

// -------------------------------------------------------------------------------------------------------------------------------
// Check 4: Every row has 1..4 Q members and the Q groups of a page are in reading order
void testRowQMembersAndReadingOrder()
{
    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;

        check (lay.numRows >= 1 && lay.numRows <= fxl::kMaxRows, name + ": numRows in valid range (" + std::to_string (lay.numRows) + ")");
        check (lay.rowsPerPage >= 1 && lay.rowsPerPage <= fxl::kMaxPageRows, name + ": rowsPerPage in valid range (" + std::to_string (lay.rowsPerPage) + ")");

        bool rowsValid = true;
        for (int r = 0; r < lay.numRows; ++r)
        {
            int members = 0;
            int lastNonEmptyCol = -1;
            for (int c = 0; c < 4; ++c)
            {
                const fxl::Cell& cell = lay.row[r].cell[c];
                if (cell.alt[0].key != nullptr)
                {
                    ++members;
                    lastNonEmptyCol = c;
                    // Cells in a row must be contiguous from the left (no holes like col 0 empty but col 1 populated)
                    if (members != c + 1)
                        rowsValid = false;
                }
            }
            if (members < 1 || members > 4 || lastNonEmptyCol != members - 1)
                rowsValid = false;
            check (members >= 1 && members <= 4, name + " row " + std::to_string (r) + " ('" + lay.row[r].title + "'): has 1..4 Q members (" + std::to_string (members) + ")");
        }
        check (rowsValid, name + ": all rows have contiguous 1..4 members");

        // The Q groups of a page are in reading order:
        // Pages are formed by grouping consecutive rows up to rowsPerPage.
        // Row indices on each page are strictly increasing (top-to-bottom reading order).
        // Within each row, column indices 0..3 are strictly increasing (left-to-right reading order).
        const int numPages = (lay.numRows + lay.rowsPerPage - 1) / lay.rowsPerPage;
        bool readingOrder = true;
        for (int p = 0; p < numPages; ++p)
        {
            const int firstRow = p * lay.rowsPerPage;
            const int pageRows = std::min (lay.rowsPerPage, lay.numRows - firstRow);
            readingOrder = readingOrder && pageRows >= 1;
            for (int r = 0; r < pageRows - 1; ++r)
                readingOrder = readingOrder && ((firstRow + r) < (firstRow + r + 1));
        }
        check (readingOrder, name + ": Q groups of every page are in top-to-bottom reading order (" + std::to_string (numPages) + " page(s))");

        // Verify with live FxSlotEditor instance
        {
            FxSlotEditor ed;
            ed.place ({ 0, 100, 1280, 524 });
            ed.setType (kind, kind < synthyboy::kNumFxInstanceTypes);
            bool liveMembersOk = true;
            for (int v = 0; v < ed.numRows(); ++v)
            {
                const int liveMembers = ed.groups[v].numMembers();
                liveMembersOk = liveMembersOk && liveMembers >= 1 && liveMembers <= 4;
            }
            check (liveMembersOk && ed.numRows() >= 1, name + ": live slot editor has 1..4 Q members in every visible row");
        }
    }
}

// -------------------------------------------------------------------------------------------------------------------------------
// Check 5: Every label fits its cell at the page's font (use the library text measure the other ui tests use)
void testLabelsFitCellAtPageFont()
{
    const float labelFont = theme::L().fLabel;

    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;
        bool allFit = true;
        float maxW = 0.0f;
        std::string longestLabel;
        int checkedLabels = 0;

        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (int a = 0; a < 2; ++a)
                {
                    const fxl::Alt& alt = lay.row[r].cell[c].alt[a];
                    if (alt.key == nullptr)
                        continue;
                    const synthyboy::ParamInfo* pi = synthyboy::findParamByKey (alt.key);
                    if (pi == nullptr)
                        continue;

                    char label[64] = {};
                    fxl::controlName (alt, *pi, lay.name, label, sizeof label);
                    check (label[0] != '\0', name + ": label of '" + alt.key + "' is non-empty");

                    const float tw = Pen::textWidth (label, labelFont);
                    ++checkedLabels;
                    if (tw > maxW)
                    {
                        maxW = tw;
                        longestLabel = label;
                    }
                    if (tw > static_cast<float> (kCellW))
                    {
                        allFit = false;
                        check (false, name + " [" + alt.key + "]: label '" + label + "' width " + std::to_string (tw) + " exceeds cell width " + std::to_string (kCellW));
                    }
                }

        check (allFit && checkedLabels > 0,
               name + ": every label (" + std::to_string (checkedLabels) + ") fits cell width "
               + std::to_string (kCellW) + " px (longest: '" + longestLabel + "' at " + std::to_string (maxW) + " px)");
    }
}

// -------------------------------------------------------------------------------------------------------------------------------
// Check 6: Every choice shown through choice_names has a non-empty shown name and no shown name contains a third-party product name (Vital, Serum, Massive, Sylenth, Diva, Pigments)
void testChoiceNamesNoThirdParty()
{
    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;
        std::set<std::string> keys;

        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
                for (int a = 0; a < 2; ++a)
                {
                    const char* k = lay.row[r].cell[c].alt[a].key;
                    if (k != nullptr)
                        keys.insert (k);
                }

        bool choicesOk = true;
        int checkedChoices = 0;

        for (const std::string& key : keys)
        {
            const synthyboy::ParamInfo* pi = synthyboy::findParamByKey (key.c_str());
            if (pi == nullptr || pi->numChoices <= 0)
                continue;

            check (pi->choices != nullptr, name + " [" + key + "]: choice parameter has non-null choices array");
            if (pi->choices == nullptr)
            {
                choicesOk = false;
                continue;
            }

            for (int ch = 0; ch < pi->numChoices; ++ch)
            {
                const char* engChoice = (ch < pi->numChoices) ? pi->choices[ch] : "";
                const char* shown = ui::shownChoice (key.c_str(), ch, engChoice);
                ++checkedChoices;

                if (shown == nullptr || shown[0] == '\0')
                {
                    choicesOk = false;
                    check (false, name + " [" + key + "] choice " + std::to_string (ch) + ": shown choice name is empty");
                    continue;
                }

                if (containsThirdPartyName (shown))
                {
                    choicesOk = false;
                    check (false, name + " [" + key + "] choice " + std::to_string (ch) + ": shown name '" + shown + "' contains third-party product name");
                }
            }
        }

        check (choicesOk, name + ": all choices (" + std::to_string (checkedChoices) + ") have non-empty shown names with no third-party product names");
    }
}

// -------------------------------------------------------------------------------------------------------------------------------
// Check 7: The hide/dim conditions reference existing keys
void testHideDimConditions()
{
    for (int kind = 1; kind < fxl::kNumKinds; ++kind)
    {
        const fxl::Layout& lay = fxl::layoutOf (kind);
        const std::string name = lay.name;
        const char* prefix = prefixOfKind (kind);
        const std::string expPrefix = std::string (prefix) + ".";

        bool conditionsOk = true;
        int checkedConditions = 0;
        int checkedPairs = 0;

        for (int r = 0; r < lay.numRows; ++r)
            for (int c = 0; c < 4; ++c)
            {
                const fxl::Cell& cell = lay.row[r].cell[c];
                for (int a = 0; a < 2; ++a)
                {
                    const fxl::Alt& alt = cell.alt[a];
                    if (alt.key == nullptr || alt.live.always())
                        continue;

                    ++checkedConditions;
                    if (alt.live.key == nullptr)
                    {
                        conditionsOk = false;
                        check (false, name + " [" + alt.key + "]: non-always condition has null key");
                        continue;
                    }

                    const synthyboy::ParamInfo* condPi = synthyboy::findParamByKey (alt.live.key);
                    if (condPi == nullptr)
                    {
                        conditionsOk = false;
                        check (false, name + " [" + alt.key + "]: condition key '" + alt.live.key + "' not found in parameter table");
                        continue;
                    }

                    if (alt.live.modes == 0)
                    {
                        conditionsOk = false;
                        check (false, name + " [" + alt.key + "]: condition modes bitmask is 0 (never live)");
                    }

                    // Condition parameter must belong to the same effect
                    const std::string condKey = alt.live.key;
                    if (condKey.rfind (expPrefix, 0) != 0)
                    {
                        conditionsOk = false;
                        check (false, name + " [" + alt.key + "]: condition key '" + condKey + "' belongs to foreign effect (expected prefix '" + expPrefix + "')");
                    }
                }

                // Paired cell check: two alts sharing a cell must reference the same condition key and have mutually exclusive modes
                if (cell.alt[1].key != nullptr)
                {
                    ++checkedPairs;
                    const fxl::Alt& a0 = cell.alt[0];
                    const fxl::Alt& a1 = cell.alt[1];

                    if (a0.key == nullptr || a0.live.always() || a1.live.always() || a0.live.key == nullptr || a1.live.key == nullptr)
                    {
                        conditionsOk = false;
                        check (false, name + " row " + std::to_string (r) + " col " + std::to_string (c) + ": paired cell must have conditions on both alts");
                        continue;
                    }

                    if (std::strcmp (a0.live.key, a1.live.key) != 0)
                    {
                        conditionsOk = false;
                        check (false, name + " row " + std::to_string (r) + " col " + std::to_string (c) + ": paired alts condition keys differ ('" + a0.live.key + "' vs '" + a1.live.key + "')");
                        continue;
                    }

                    const synthyboy::ParamInfo* condPi = synthyboy::findParamByKey (a0.live.key);
                    if (condPi != nullptr && condPi->numChoices > 0)
                    {
                        bool disjoint = true;
                        for (int m = 0; m < condPi->numChoices; ++m)
                            if (a0.live.holds (static_cast<float> (m)) && a1.live.holds (static_cast<float> (m)))
                                disjoint = false;
                        if (! disjoint)
                        {
                            conditionsOk = false;
                            check (false, name + " row " + std::to_string (r) + " col " + std::to_string (c) + ": paired alts are live in the same condition state");
                        }
                    }
                }
            }

        check (conditionsOk, name + ": hide/dim conditions (" + std::to_string (checkedConditions)
               + ") and paired cell exclusions (" + std::to_string (checkedPairs) + ") reference existing keys and are disjoint");
    }
}

} // namespace

int main()
{
    std::printf ("=== FX Page Tables Data Consistency Tests ===\n");
    testEveryKeyExistsInParamTable();
    testNoDuplicateKeysInTable();
    testParametersCoveredOrChainLevel();
    testRowQMembersAndReadingOrder();
    testLabelsFitCellAtPageFont();
    testChoiceNamesNoThirdParty();
    testHideDimConditions();

    std::printf ("\nui_fx_tables_host: %s (%d failure(s))\n", g_fails == 0 ? "ALL PASS" : "FAILED", g_fails);
    return g_fails ? 1 : 0;
}
