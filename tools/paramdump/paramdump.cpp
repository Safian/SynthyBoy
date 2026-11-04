// SynthyBoy - paramdump: the whole parameter table as JSON, the machine readable description that Kick's own UI consumes
// (docs/ARCHITECTURE.md R4). docs/params.json is this output; scripts/gen-params-doc.py turns it into docs/PARAMETERS.md.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_paramdump            writes the document to stdout (deterministic, one parameter per line)
//
// Keys: key, id, name, group, min, max, default, scale (linear | exp_skew | stepped), skew, interval, decimals, unit,
// flags, choices (stepped parameters with a name list) or choiceSet ("modSource" | "modDestination", the lists are at the top
// of the document), modulatable (1-based choice value of the parameter in the destination list, 0 = not a destination),
// ownEngine (the own engine acts on it). Floats are "%.9g" (exact, locale independent).

#include <cstdio>
#include <string>

#include "number.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"

using namespace synthyboy;

namespace
{
std::string quote (const char* text)
{
    std::string s = "\"";
    for (const char* p = text; *p != '\0'; ++p)
    {
        const unsigned char c = static_cast<unsigned char> (*p);
        if (c == '"' || c == '\\')
        {
            s += '\\';
            s += static_cast<char> (c);
        }
        else if (c < 0x20)
        {
            char buf[8];
            std::snprintf (buf, sizeof buf, "\\u%04x", c);
            s += buf;
        }
        else
        {
            s += static_cast<char> (c);
        }
    }
    s += '"';
    return s;
}

std::string num (float v)
{
    char buf[32];
    number::formatG9 (v, buf, sizeof buf);
    return buf;
}

const char* scaleName (Scale s)
{
    switch (s)
    {
        case Scale::linear:   return "linear";
        case Scale::exp_skew: return "exp_skew";
        case Scale::stepped:  return "stepped";
    }
    return "?";
}

const char* unitName (Unit u)
{
    switch (u)
    {
        case Unit::none:     return "none";
        case Unit::dB:       return "dB";
        case Unit::Hz:       return "Hz";
        case Unit::ms:       return "ms";
        case Unit::percent:  return "percent";
        case Unit::semitone: return "semitone";
        case Unit::cents:    return "cents";
        case Unit::beats:    return "beats";
        case Unit::degrees:  return "degrees";
        case Unit::voices:   return "voices";
    }
    return "?";
}

std::string flagList (uint32_t flags)
{
    std::string s = "[";
    const struct { uint32_t bit; const char* name; } names[] = {
        { kParamAutomatable, "automatable" }, { kParamModulatable, "modulatable" }, { kParamPerNote, "per_note" },
        { kParamHidden, "hidden" },           { kParamStepped, "stepped" },         { kParamEngineSpecific, "engine_specific" }
    };
    bool first = true;
    for (const auto& n : names)
        if ((flags & n.bit) != 0)
        {
            s += first ? "" : ",";
            s += quote (n.name);
            first = false;
        }
    return s + "]";
}
} // namespace

int main()
{
    const auto engine = createOwnEngine();

    std::string out;
    out += "{\"format\":\"synthyboy-params\",\"version\":1,\"stateVersion\":" + std::to_string (kStateVersion) + ",\"count\":"
           + std::to_string (paramCount()) + ",\n";

    out += "\"modSources\":[";
    for (size_t k = 0; k < modSourceCount(); ++k)
    {
        const auto& s = modSourceTable()[k];
        out += (k > 0 ? ",\n  " : "\n  ");
        out += "{\"choice\":" + std::to_string (k + 1) + ",\"key\":" + quote (s.key) + ",\"name\":" + quote (s.name)
               + ",\"bipolar\":" + (s.bipolar ? "true" : "false") + "}";
    }
    out += "\n],\n\"modDestinations\":[";
    for (size_t k = 1; k <= modDestinationCount(); ++k)
    {
        const auto& p = paramTable()[modDestinationParamIndex (k)];
        out += (k > 1 ? ",\n  " : "\n  ");
        out += "{\"choice\":" + std::to_string (k) + ",\"key\":" + quote (p.key) + ",\"name\":" + quote (p.name) + "}";
    }
    out += "\n],\n\"params\":[";

    for (size_t i = 0; i < paramCount(); ++i)
    {
        const auto& p = paramTable()[i];
        out += (i > 0 ? ",\n  " : "\n  ");
        out += "{\"key\":" + quote (p.key) + ",\"id\":" + std::to_string (p.id) + ",\"name\":" + quote (p.name) + ",\"group\":" + quote (p.group)
               + ",\"min\":" + num (p.min) + ",\"max\":" + num (p.max) + ",\"default\":" + num (p.def) + ",\"scale\":" + quote (scaleName (p.scale))
               + ",\"skew\":" + num (p.skew) + ",\"interval\":" + num (p.interval) + ",\"decimals\":" + std::to_string (p.decimals)
               + ",\"unit\":" + quote (unitName (p.unit)) + ",\"flags\":" + flagList (p.flags);
        if (p.choiceSet == ChoiceSet::list)
        {
            out += ",\"choices\":[";
            for (size_t k = 0; k < choiceCount (p); ++k)
                out += (k > 0 ? "," : "") + quote (choiceName (p, k));
            out += "]";
        }
        else if (p.choiceSet == ChoiceSet::modSource)
        {
            out += ",\"choiceSet\":\"modSource\"";
        }
        else if (p.choiceSet == ChoiceSet::modDestination)
        {
            out += ",\"choiceSet\":\"modDestination\"";
        }
        out += ",\"modulatable\":" + std::to_string (modDestinationChoiceOf (i)) + ",\"ownEngine\":" + (engine->supportsParam (p.id) ? "true" : "false") + "}";
    }
    out += "\n]}\n";

    std::fputs (out.c_str(), stdout);
    return 0;
}
