// SynthyBoy - MPC overlay: what the overlay element UIs (env_ui.h, and the ones to come) share when they bind library components to the engine's parameters: the table lookups, the writes through
// IInstanceView (normalised, the plugin tells the host), the text of a value with its unit, and the modulation matrix seen from a parameter (which slots move it) and from a source (which slots it drives).
// Pure: no OS headers, no state. The same code is in osc_ui.h as private members (the oscillator UI was first); the element UIs use this file.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../kit/param_source.h"
#include "../kit/synthyboy_params.h"
#include "../native_lfo_pick.h"
#include "../ui/binding.h"
#include "../ui/model.h"
#include "instance_view.h"
#include "macro_names.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace overlay { namespace uibind {

/** The parameters of SynthyBoy through the kit's parameter source (kit/param_source.h IParamSource): the lookups, the conversions and the text of a value are the kit's, so a page written on them runs on any plugin. */
inline const mpckit::IParamSource& params() noexcept { return mpckit::synthyboy_adapter::Params::get(); }

inline int indexOfKey (const std::string& key) noexcept { return params().indexOfKey (key.c_str()); }   // (an empty key: -1)
inline const synthyboy::ParamInfo& info (int idx) noexcept { return synthyboy::paramTable()[idx]; }   // the pages read ParamInfo (choices, ranges)
inline double normOf (int idx, float plain) noexcept { return params().toNormalised (idx, plain); }
inline void writePlain (IInstanceView& v, int idx, double plain) noexcept
{
    const mpckit::IParamSource& p = params();
    v.writeParam (idx, p.toNormalised (idx, p.constrain (idx, static_cast<float> (plain))));
}
inline void writeNorm (IInstanceView& v, int idx, double norm) noexcept { v.writeParam (idx, static_cast<float> (std::clamp (norm, 0.0, 1.0))); }

/** A value of a parameter as text with its unit: "35%", "+12 ct", "420 Hz", "1.20 s", "-6.0 dB" (at most 15 characters). Rates under 100 Hz keep their decimals (two under 10 Hz, one under 100 Hz: "0.25 Hz", "33.5 Hz",
    "420 Hz"); a value that rounds to zero has no sign; a pan (a percentage from -100 to 100 whose key ends in ".pan") reads like the pans of the mixer: C, L 50, R 50. The kit's function (mpckit::formatValue, the one algorithm). */
inline void formatValue (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
{
    mpckit::formatValue (mpckit::synthyboy_adapter::descOf (pi), plain, out, cap);
}

/** The matrix and route editor share a signed depth with one significant fractional digit. */
inline void formatModAmount (float plain, char* out, size_t cap) noexcept
{
    const int tenths = static_cast<int> (std::lround (plain * 10.0f));
    if (tenths % 10 == 0)
        std::snprintf (out, cap, "%+d%%", tenths / 10);
    else
        std::snprintf (out, cap, "%+.1f%%", tenths / 10.0);
}

// ---- the modulation matrix
inline const char* sourceName (int choice) noexcept
{
    if (const MacroNames* names = activeMacroNames())   // a macro answers with its own name (macro_names.h)
    {
        const int m = macroOfSource (choice);
        if (m >= 0)
            return names->full (m);
    }
    const synthyboy::ModSourceInfo* t = synthyboy::modSourceTable();
    return choice >= 1 && choice <= static_cast<int> (synthyboy::modSourceCount()) ? t[choice - 1].name : "";
}
/** True for the sources that are macros (their chips take the colour of the macros whatever the name says). */
inline bool isMacroSource (int choice) noexcept { return macroOfSource (choice) >= 0; }
/** The name of a modulation source in at most 6 characters that still says which one it is ("Macro 3" -> "MAC 3", "Random LFO 2" -> "RLFO 2", "Velocity" -> "VEL", the others their first six characters). */
inline void shortSourceName (const char* full, char out[7])
{
    const std::string n = full;
    auto number = [&] { const size_t sp = n.rfind (' '); return sp == std::string::npos ? std::string() : n.substr (sp); };
    std::string s;
    if (n.compare (0, 6, "Macro ") == 0) s = "MAC" + number();
    else if (n.compare (0, 11, "Random LFO ") == 0) s = "RLFO" + number();
    else if (n == "Velocity") s = "VEL";
    else if (n == "Aftertouch") s = "AT";
    else if (n == "Poly Aftertouch") s = "PAT";
    else if (n == "Mod Wheel") s = "MOD WH";
    else if (n == "Pitch Wheel") s = "PITCH";
    else if (n == "Random per Note") s = "RND";
    else s = n;
    std::snprintf (out, 7, "%.6s", s.c_str());
}
/** The short name (at most six bytes) of the source `choice`: a macro answers with the short form of its name ("MAC 3", "Filter"), the others as shortSourceName. */
inline void shortName (int choice, char out[7])
{
    const MacroNames* names = activeMacroNames();
    const int m = macroOfSource (choice);
    if (m >= 0)
    {
        if (names != nullptr)
            std::snprintf (out, 7, "%s", names->shortName (m));
        else
            std::snprintf (out, 7, "MAC %d", m + 1);   // outside a scope: the default names
        return;
    }
    shortSourceName (sourceName (choice), out);
}
/** The modulation source choice value (1 ..) of the macro n (1 .. 8); 0 when the table has none. */
inline int macroSource (int n) noexcept
{
    char key[12];
    std::snprintf (key, sizeof key, "macro%d", n);
    const synthyboy::ModSourceInfo* t = synthyboy::modSourceTable();
    for (size_t i = 0; i < synthyboy::modSourceCount(); ++i)
        if (std::strcmp (t[i].key, key) == 0)
            return static_cast<int> (i) + 1;
    return 0;
}
/** The groups of the sources in a list: 1 envelopes, 2 LFOs (random ones too), 3 macros, 4 the player (velocity, note, wheels, pressure ...), 5 the sound (the outputs of the sources and filters, the voice values). 0: none. */
constexpr int kSourceGroups = 5;
inline const char* sourceGroupName (int g) noexcept
{
    static const char* const n[kSourceGroups + 1] = { "NONE", "ENVELOPES", "LFOS", "MACROS", "PLAYER", "SOUND" };
    return g >= 0 && g <= kSourceGroups ? n[g] : "";
}
inline int sourceGroup (int choice) noexcept
{
    if (choice < 1 || choice > static_cast<int> (synthyboy::modSourceCount()))
        return 0;
    const char* k = synthyboy::modSourceTable()[choice - 1].key;
    auto starts = [&] (const char* p) { return std::strncmp (k, p, std::strlen (p)) == 0; };
    if (starts ("env")) return 1;
    if (starts ("lfo") || starts ("rlfo")) return 2;
    if (starts ("macro")) return 3;
    if (starts ("osc_") || starts ("noise") || starts ("sub") || starts ("filter") || starts ("stereo") || starts ("voice_") || starts ("active_voices")) return 5;
    return 4;
}
// ---- the board of the sources (ui/mod_board.h): every source the engine has as a chip in its group; the matrix page and the page of the modulations of one control use the same
/** The text of a chip: the engine's name of the source in capitals ("Poly Aftertouch" -> "POLY AFTERTOUCH"; the random LFOs by their short name, "RLFO 2": the whole LFO family fits one row of the board). */
inline void boardLabel (int choice, char* out, size_t cap)
{
    const char* key = synthyboy::modSourceTable()[choice - 1].key;
    if (std::strncmp (key, "rlfo", 4) == 0)
        shortSourceName (sourceName (choice), out);
    else
        std::snprintf (out, cap, "%s", sourceName (choice));
    for (char* c = out; *c != '\0'; ++c)
        *c = static_cast<char> (std::toupper (static_cast<unsigned char> (*c)));
}
/** The name that picks the colour of a chip (theme::sourceColour): the short name of the source, the random sources as RND (the colour of the random LFO of the LFO page), the macros as MAC whatever they are called. */
inline void boardColourName (int choice, char* out, size_t cap)
{
    const char* key = synthyboy::modSourceTable()[choice - 1].key;
    if (std::strncmp (key, "rlfo", 4) == 0 || std::strcmp (key, "random") == 0)
        std::snprintf (out, cap, "RND");
    else if (isMacroSource (choice))
        std::snprintf (out, cap, "MAC");
    else
    {
        char sn[7];
        shortSourceName (sourceName (choice), sn);
        std::snprintf (out, cap, "%s", sn);
    }
}
/** The modulation source ids (the choice value of modNN.source) of envelope n (1 .. 6) and LFO n (1 .. 10); ui::kNoSource for anything else. */
inline int envSource (int n) noexcept { return n >= 1 && n <= 6 ? n : ui::kNoSource; }
inline int lfoSource (int n) noexcept { return n >= 1 && n <= 8 ? 6 + n : (n == 9 ? 33 : (n == 10 ? 34 : ui::kNoSource)); }

inline int rndSource (int n) noexcept { return n >= 1 && n <= 4 ? 14 + n : ui::kNoSource; }   // random LFO n (1 .. 4) = source 15 .. 18

// ---- the selector of the merged Env / LFO tabs ("used + one free", docs/ui-map/env-filter-lfo-plan.md section (b)); the component is binding::SelectorSet -----------------------------------------------------------------
constexpr int kSelEnvs = 6, kSelLfos = 10, kSelRnds = 4, kSourceIds = 64;   // the items; the source ids (choice values of modNN.source) are below 64
constexpr int kRndBase = kSelLfos;   // the selector id of random LFO m (1 .. 4) is kRndBase + m (11 .. 14); LFO n and ENV n have the id n

/** One modulation slot as the selector sees it: the choice values of its source, destination and auxiliary source (0 = none). */
struct SlotUse { int source = 0, destination = 0, aux = 0; };

/** PURE. The sources that are USED: the source of a slot that has a source AND a destination, and the auxiliary source of such a slot (an aux of a slot without source or without destination does not count: the slot is free).
    A bypassed or switched-off slot still counts (it is assigned). `used[id]` for every source id below kSourceIds. */
inline void usedSourcesOf (const SlotUse* slots, int n, bool used[kSourceIds]) noexcept
{
    for (int i = 0; i < kSourceIds; ++i)
        used[i] = false;
    for (int i = 0; i < n; ++i)
    {
        const SlotUse& s = slots[i];
        if (s.source <= 0 || s.destination <= 0)
            continue;
        if (s.source < kSourceIds)
            used[s.source] = true;
        if (s.aux > 0 && s.aux < kSourceIds)
            used[s.aux] = true;
    }
}

/** What counts as "edited" beyond the routing: a parameter of the item differs from its default, or (an LFO) a drawn shape exists (the owner: an LFO with a drawn shape or changed parameters but no routing counts as used). */
struct Edited
{
    bool env[kSelEnvs + 1] = {}, lfo[kSelLfos + 1] = {}, rnd[kSelRnds + 1] = {};   // index 1 ..
};

enum class SelKind { Env, Lfo };

/** PURE. Fills `sel` with the candidates of the selector `kind` from the used sources and the edited flags: ENV 1 .. 6 (ENV 1 always shown); LFO 1 .. 10, then R1 .. R4 (group 1). The selected id is NOT set here. */
inline void selectorCandidates (SelKind kind, const bool usedSrc[kSourceIds], const Edited& ed, binding::SelectorSet& sel)
{
    sel.clear();
    char label[8];
    if (kind == SelKind::Env)
    {
        for (int n = 1; n <= kSelEnvs; ++n)
        {
            std::snprintf (label, sizeof label, "%d", n);
            sel.add (n, label, usedSrc[envSource (n)] || ed.env[n], n == 1, 0);
        }
        return;
    }
    for (int n = 1; n <= kSelLfos; ++n)
    {
        std::snprintf (label, sizeof label, "%d", n);
        sel.add (n, label, usedSrc[lfoSource (n)] || ed.lfo[n], false, 0);
    }
    for (int m = 1; m <= kSelRnds; ++m)
    {
        std::snprintf (label, sizeof label, "R%d", m);
        sel.add (kRndBase + m, label, usedSrc[rndSource (m)] || ed.rnd[m], false, 1);
    }
}

/** One scan of the instance (the 64 slots, the parameters of the items, the drawn shapes): done once per refresh, never per frame. */
struct UsedScan
{
    bool used[kSourceIds] = {};
    Edited edited;
};
inline UsedScan scanUsed (const IInstanceView& v)
{
    UsedScan sc;
    SlotUse slots[synthyboy::kNumModSlots];
    int n = 0;
    for (int s = 0; s < synthyboy::kNumModSlots; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source < 0 || sp.destination < 0)
            continue;
        SlotUse& u = slots[n++];
        u.source = static_cast<int> (std::lround (v.param (sp.source)));
        u.destination = static_cast<int> (std::lround (v.param (sp.destination)));
        u.aux = sp.auxSource >= 0 ? static_cast<int> (std::lround (v.param (sp.auxSource))) : 0;
    }
    usedSourcesOf (slots, n, sc.used);
    // the parameters of every item that differ from their defaults (the index lists are built once)
    struct Lists
    {
        std::vector<int> env[kSelEnvs + 1], lfo[kSelLfos + 1], rnd[kSelRnds + 1];
        Lists()
        {
            const synthyboy::ParamInfo* t = synthyboy::paramTable();
            const size_t count = synthyboy::paramCount();
            auto fill = [&] (std::vector<int>& out, const std::string& prefix)
            {
                for (size_t i = 0; i < count; ++i)
                    if (std::strncmp (t[i].key, prefix.c_str(), prefix.size()) == 0)
                        out.push_back (static_cast<int> (i));
            };
            for (int n = 1; n <= kSelEnvs; ++n) fill (env[n], "env" + std::to_string (n) + ".");
            for (int n = 1; n <= kSelLfos; ++n) fill (lfo[n], "lfo" + std::to_string (n) + ".");
            for (int m = 1; m <= kSelRnds; ++m) fill (rnd[m], "rlfo" + std::to_string (m) + ".");
        }
    };
    static const Lists lists;   // built once, thread safely
    auto differs = [&] (const std::vector<int>& l)
    {
        for (int i : l)
            if (std::fabs (v.param (i) - synthyboy::paramTable()[i].def) > 1.0e-6f)
                return true;
        return false;
    };
    for (int n = 1; n <= kSelEnvs; ++n) sc.edited.env[n] = differs (lists.env[n]);
    for (int n = 1; n <= kSelLfos; ++n) sc.edited.lfo[n] = differs (lists.lfo[n]);
    for (int m = 1; m <= kSelRnds; ++m) sc.edited.rnd[m] = differs (lists.rnd[m]);
    if (const auto content = v.content())
        for (const synthyboy::ContentLfoShape& sh : content->lfoShapes)
            if (sh.lfo >= 1 && sh.lfo <= kSelLfos && ! sh.points.empty())
                sc.edited.lfo[sh.lfo] = true;
    return sc;
}

/** The view state: the selected id. Env: Play Env (1 .. 6); LFO: Play LFO (1 .. 10), or kRndBase + m while the random LFO m (1 .. 4) is the one edited (mpcTabView.rnd, chunk only). The tab and the Play page read and write the same state. */
inline int selectedEnv (const IInstanceView& v) noexcept { return std::clamp (v.playSelection (1), 1, kSelEnvs); }
inline int selectedLfo (const IInstanceView& v) noexcept
{
    const int r = v.tabView (IInstanceView::kTabViewRnd);
    return r >= 1 && r <= kSelRnds ? kRndBase + r : std::clamp (v.playSelection (2), 1, kSelLfos);
}
/** A choice of the selector: view state only, NO parameter is written. A standard LFO keeps Play LFO and clears the random one; a random LFO leaves Play LFO as it was (the last standard number). */
inline void chooseEnv (IInstanceView& v, int id) noexcept { if (id >= 1 && id <= kSelEnvs) v.setPlaySelection (1, id); }
inline void chooseLfo (IInstanceView& v, int id) noexcept
{
    if (id >= 1 && id <= kSelLfos)
    {
        v.setPlaySelection (2, id);
        v.setTabView (IInstanceView::kTabViewRnd, 0);
    }
    else if (id > kRndBase && id <= kRndBase + kSelRnds)
        v.setTabView (IInstanceView::kTabViewRnd, id - kRndBase);
}
/** Fills the selector from a scan and the view state (candidates, selected id). */
inline void fillSelector (SelKind kind, const UsedScan& sc, const IInstanceView& v, binding::SelectorSet& sel)
{
    selectorCandidates (kind, sc.used, sc.edited, sel);
    sel.setSelected (kind == SelKind::Env ? selectedEnv (v) : selectedLfo (v));
}
/** The cells of a selector as a list of positions: what the Play page's selector steps over (the neighbours, the list of its name). It is made FROM the filled SelectorSet (`cellsFromSelector`), so the tab and the Play page cannot drift
    apart: the same used items, the selected one and ONE `+`. `id[k]` is the candidate of cell k, `kPlusCell` (0) for the `+` cell; `names[k]` its row name ("ENV 3", "LFO 3", "RND 2", "+"). */
struct SelectorCells
{
    static constexpr int kMax = binding::SelectorSet::kMaxCells, kPlusCell = 0;
    int n = 0, selectedPos = 0;
    int id[kMax] = {};
    char text[kMax][10] = {};
    const char* names[kMax] = {};
    bool same (const SelectorCells& o) const noexcept
    {
        if (n != o.n || selectedPos != o.selectedPos)
            return false;
        for (int k = 0; k < n; ++k)
            if (id[k] != o.id[k])
                return false;
        return true;
    }
};
inline std::string selectorRowName (SelKind kind, int id);
inline void cellsFromSelector (SelKind kind, const binding::SelectorSet& sel, SelectorCells& out)
{
    int cs[binding::SelectorSet::kMaxCells];
    out.n = sel.cells (cs, binding::SelectorSet::kMaxCells);
    out.selectedPos = 0;
    for (int k = 0; k < out.n; ++k)
    {
        if (cs[k] == binding::SelectorSet::kPlus)
        {
            out.id[k] = SelectorCells::kPlusCell;
            std::snprintf (out.text[k], sizeof out.text[k], "+");
        }
        else
        {
            out.id[k] = sel.state.items[cs[k]].id;
            std::snprintf (out.text[k], sizeof out.text[k], "%s", selectorRowName (kind, out.id[k]).c_str());
            if (out.id[k] == sel.state.selected)
                out.selectedPos = k;
        }
        out.names[k] = out.text[k];
    }
}
/** The id a tap on cell `pos` selects: the candidate of the cell, or for the `+` cell the lowest free one of the FIRST group (the only one for an envelope; for the LFO with two free kinds the owner opens the list instead: `plusChoices`);
    0 when there is nothing to select. */
inline int cellChoice (const binding::SelectorSet& sel, const SelectorCells& cells, int pos) noexcept
{
    if (pos < 0 || pos >= cells.n)
        return 0;
    if (cells.id[pos] != SelectorCells::kPlusCell)
        return cells.id[pos];
    int ch[binding::SelectorSet::kMaxChoices];
    return sel.plusChoices (ch, binding::SelectorSet::kMaxChoices) > 0 ? sel.state.items[ch[0]].id : 0;
}
/** The id the selection falls back to when the tab is LEFT while an item that is neither used nor edited is selected (a `+` pick the user did not edit; decided 2026-11-21, `docs/ui-map/osc-play-bold-cut/README.md`): the last used item
    in number order before it, else the first used one after it, else the first candidate (ENV 1, LFO 1). 0: the selection is used (or `always`): nothing changes. Pure: no parameter is written, the owner changes the view state only. */
inline int fallbackSelection (const binding::SelectorSet& sel) noexcept
{
    int cur = -1;
    for (int i = 0; i < sel.state.count; ++i)
        if (sel.state.items[i].id == sel.state.selected)
            cur = i;
    if (cur < 0 || sel.state.items[cur].used || sel.state.items[cur].always)
        return 0;
    for (int i = cur - 1; i >= 0; --i)
        if (sel.state.items[i].used || sel.state.items[i].always)
            return sel.state.items[i].id;
    for (int i = cur + 1; i < sel.state.count; ++i)
        if (sel.state.items[i].used || sel.state.items[i].always)
            return sel.state.items[i].id;
    return sel.state.count > 0 ? sel.state.items[0].id : 0;
}
/** The tab was left: an unused, unedited selection falls back (see `fallbackSelection`); the scan is made here (once, not per frame). */
inline void leaveSelector (SelKind kind, IInstanceView& v)
{
    binding::SelectorSet sel;
    fillSelector (kind, scanUsed (v), v, sel);
    const int id = fallbackSelection (sel);
    if (id <= 0)
        return;
    if (kind == SelKind::Env)
        chooseEnv (v, id);
    else
        chooseLfo (v, id);
}
/** The label of an id ("1", "R2") and the long name for a list row ("LFO 3", "RND 2"). */
inline std::string selectorRowName (SelKind kind, int id)
{
    if (kind == SelKind::Env)
        return "ENV " + std::to_string (id);
    return id > kRndBase ? "RND " + std::to_string (id - kRndBase) : "LFO " + std::to_string (id);
}

constexpr int kMaxTargetSlots = synthyboy::kNumModSlots;   // the most slots a source can have

struct ModRow { int slot = -1; int source = 0; };

/** The first slot without a source (-1: the matrix is full). */
inline int freeSlot (const IInstanceView& v) noexcept
{
    for (int s = 0; s < synthyboy::kNumModSlots; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source >= 0 && std::lround (v.param (sp.source)) == 0)
            return s;
    }
    return -1;
}
/** Every slot whose source is `source`: slots[] gets them in table order; returns the count (at most `max`). */
inline int slotsOfSource (const IInstanceView& v, int source, int* slots, int max) noexcept
{
    int n = 0;
    for (int s = 0; s < synthyboy::kNumModSlots && n < max; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source < 0 || sp.destination < 0)
            continue;
        if (std::lround (v.param (sp.source)) == source && std::lround (v.param (sp.destination)) > 0)
            slots[n++] = s;
    }
    return n;
}
/** The first slot that has the source and the destination parameter `destChoice` (the choice value of modNN.destination), -1 none. */
inline int slotOfPair (const IInstanceView& v, int source, long destChoice) noexcept
{
    for (int s = 0; s < synthyboy::kNumModSlots; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source >= 0 && sp.destination >= 0 && std::lround (v.param (sp.source)) == source && std::lround (v.param (sp.destination)) == destChoice)
            return s;
    }
    return -1;
}

/** Find a route even when its source is outside the visible modulation rows. */
inline int slotOf (const IInstanceView& v, int param, int source) noexcept
{
    if (param < 0 || source <= 0)
        return -1;
    const auto destination = synthyboy::modDestinationChoiceOf (static_cast<size_t> (param));
    return destination == 0 ? -1 : slotOfPair (v, source, static_cast<long> (destination));
}

/** Add a target to the first free slot. The source is written last so that a slot appears whole. */
inline int addTargetSlot (IInstanceView& inst, int source, int destination)
{
    if (source <= 0 || destination <= 0 || slotOfPair (inst, source, destination) >= 0)
        return -1;
    const int slot = freeSlot (inst);
    if (slot < 0)
        return -1;
    const int param = synthyboy::modDestinationParamIndex (static_cast<size_t> (destination));
    const auto sp = synthyboy::modSlotParams (slot);
    writePlain (inst, sp.destination, destination);
    writePlain (inst, sp.amount, 50.0);
    if (sp.bypass >= 0)
        writePlain (inst, sp.bypass, 0.0);
    if (sp.bipolar >= 0)
        writePlain (inst, sp.bipolar, param >= 0 && info (param).min < 0.0f && info (param).max > 0.0f ? 1.0 : 0.0);
    writePlain (inst, sp.source, source);
    return slot;
}

/** The new slot's position in the table-ordered target list. The first free slot can be on any page. */
inline int targetPosition (const IInstanceView& inst, int source, int slot) noexcept
{
    int position = 0;
    for (int i = 0; i < slot; ++i)
    {
        const auto sp = synthyboy::modSlotParams (i);
        if (sp.source >= 0 && sp.destination >= 0 && std::lround (inst.param (sp.source)) == source
            && std::lround (inst.param (sp.destination)) > 0)
            ++position;
    }
    return position;
}

/** The destinations of the modulation matrix grouped for a list (the group of the parameter before " / "): the groups, then per group the destination choice values (1 ..) in table order. ONE list for every
    page that adds a target (the envelope's and the LFO's ADD TARGET, the destination of a slot of the matrix). */
struct DestIndex
{
    std::vector<std::string> groups;
    std::vector<std::vector<int>> choices;
    static const DestIndex& get()
    {
        static const DestIndex d = build();
        return d;
    }
    static DestIndex build()
    {
        DestIndex d;
        const size_t n = synthyboy::modDestinationCount();
        for (size_t c = 1; c <= n; ++c)
        {
            const int p = synthyboy::modDestinationParamIndex (c);
            if (p < 0)
                continue;
            std::string g = info (p).group != nullptr ? info (p).group : "";
            const size_t cut = g.find (" / ");
            if (cut != std::string::npos)
                g.resize (cut);
            size_t i = 0;
            while (i < d.groups.size() && d.groups[i] != g)
                ++i;
            if (i == d.groups.size())
            {
                d.groups.push_back (g);
                d.choices.emplace_back();
            }
            d.choices[i].push_back (static_cast<int> (c));
        }
        return d;
    }
    /** The group and the position inside it of a destination choice value; false when the choice is in no group. */
    bool locate (int choice, int& group, int& index) const
    {
        for (size_t g = 0; g < choices.size(); ++g)
            for (size_t i = 0; i < choices[g].size(); ++i)
                if (choices[g][i] == choice)
                {
                    group = static_cast<int> (g);
                    index = static_cast<int> (i);
                    return true;
                }
        return false;
    }
};

/** The last index of a host choice of the LFO (nativeui::lfopick): the sources (SOURCE of a point modulation, ADD SOURCE), the groups of the matrix (GROUP) and the destinations of one group (TARGET of group g). */
inline int lfoPickLast (int field) noexcept
{
    namespace n = nativeui::lfopick;
    if (field == n::Source || field == n::AddSourceChoice)
        return static_cast<int> (synthyboy::modSourceCount());
    const DestIndex& d = DestIndex::get();
    if (field == n::Group)
        return std::min (n::kMaxGroups, static_cast<int> (d.groups.size()));
    const auto group = static_cast<size_t> (field - n::FirstTarget);
    return field >= n::FirstTarget && group < d.choices.size() ? static_cast<int> (d.choices[group].size()) : 0;
}

}}} // namespace synthyboy_mpc::overlay::uibind
