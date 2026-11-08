// SynthyBoy - paramdump: the whole parameter table as JSON, the machine readable description that Kick's own UI consumes
// (docs/ARCHITECTURE.md R4). docs/params.json is this output; scripts/gen-params-doc.py turns it into docs/PARAMETERS.md.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   synthyboy_paramdump            writes the parameter table JSON to stdout
//   synthyboy_paramdump --kick-pages  writes the Kick remote-control pages JSON to stdout
//
// Keys: key, id, name, group, min, max, default, scale (linear | exp_skew | stepped), skew, interval, decimals, unit,
// flags, choices (stepped parameters with a name list) or choiceSet ("modSource" | "modDestination", the lists are at the top
// of the document), modulatable (1-based choice value of the parameter in the destination list, 0 = not a destination),
// ownEngine (the own engine acts on it). Floats are "%.9g" (exact, locale independent).

#include <cstdio>
#include <cstring>
#include <string>

#include "number.h"
#include "synthyboy/engine.h"
#include "synthyboy/kick_pages.h"
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

std::string deriveShortLabel (const ParamInfo& p)
{
    std::string name = p.name;
    const char* prefixes[] = {
        "Master ", "Filter 1 ", "Filter 2 ", "FX Filter ", "FX ", "Filter ",
        "Osc 1 ", "Osc 2 ", "Osc 3 ", "Noise ", "Envelope 1 ", "Envelope 2 ",
        "Envelope 3 ", "Envelope 4 ", "Envelope 5 ", "Envelope 6 ",
        "Env 1 ", "Env 2 ", "Env 3 ", "Env 4 ", "Env 5 ", "Env 6 ",
        "LFO 1 ", "LFO 2 ", "LFO 3 ", "LFO 4 ", "LFO 5 ", "LFO 6 ", "LFO 7 ", "LFO 8 ",
        "Random LFO 1 ", "Random LFO 2 ", "Random LFO 3 ", "Random LFO 4 ",
        "Chorus ", "Compressor ", "Delay ", "Distortion ", "EQ ", "Flanger ", "Phaser ", "Reverb ",
        "Mod 01 ", "Mod 02 ", "Mod 03 ", "Mod 04 ", "Mod 05 ", "Mod 06 ", "Mod 07 ", "Mod 08 ",
        "Mod 09 ", "Mod 10 ", "Mod 11 ", "Mod 12 ", "Mod 13 ", "Mod 14 ", "Mod 15 ", "Mod 16 "
    };
    for (const char* pre : prefixes)
    {
        const size_t len = std::strlen (pre);
        if (name.compare (0, len, pre) == 0)
        {
            name = name.substr (len);
            break;
        }
    }
    if (name.length() > 10)
    {
        const struct { const char* from; const char* to; } abbrevs[] = {
            { "Formant Resonance", "Fmt Reso" },
            { "Formant Transpose", "Fmt Trans" },
            { "Formant Spread", "Fmt Spread" },
            { "Comb Offset", "Comb Off" },
            { "Unison Voices", "Voices" },
            { "Unison Detune", "Detune" },
            { "Unison Spread", "Spread" },
            { "Unison Detune Curve", "Det Curve" },
            { "Unison Stack", "Stack" },
            { "Unison Blend", "Blend" },
            { "Morph Amount", "Morph Amt" },
            { "Morph Spread", "Morph Spd" },
            { "Warp Amount", "Warp Amt" },
            { "Warp Spread", "Warp Spd" },
            { "Frame Spread", "Frame Spd" },
            { "Random Phase", "Rand Phase" },
            { "Attack Curve", "Atk Curve" },
            { "Decay Curve", "Dec Curve" },
            { "Release Curve", "Rel Curve" },
            { "Lower Threshold", "Low Thresh" },
            { "Upper Threshold", "Upper Thr" },
            { "Lower Ratio", "Low Ratio" },
            { "Upper Ratio", "Up Ratio" },
            { "Filter Resonance", "Filt Reso" },
            { "Filter Cutoff", "Filt Cut" },
            { "Filter Blend", "Filt Blend" },
            { "Filter Order", "Filt Order" },
            { "Phase Offset", "Phase Off" },
            { "Chorus Amount", "Chor Amt" },
            { "Chorus Rate", "Chor Rate" },
            { "Low Shelf Cutoff", "Low Cut" },
            { "Low Shelf Gain", "Low Gain" },
            { "High Shelf Cutoff", "High Cut" },
            { "High Shelf Gain", "High Gain" },
            { "Pre High Cut", "Pre High" },
            { "Pre Low Cut", "Pre Low" },
            { "Portamento Time", "Porta Time" },
            { "Portamento Slope", "Porta Slp" },
            { "Portamento Always", "Porta Alw" },
            { "Portamento Scaled", "Porta Scl" },
            { "Velocity Track", "Vel Track" },
            { "Key Track Transpose", "Key Trans" },
            { "Key Track Tune", "Key Tune" },
            { "Key Track", "Key Track" },
            { "Destination", "Dest" },
            { "Wavetable", "Wave" },
            { "Transpose", "Trans" },
            { "Polyphony", "Poly" },
            { "Bend Range", "Bend" }
        };
        for (const auto& a : abbrevs)
        {
            if (name == a.from)
            {
                name = a.to;
                break;
            }
        }
        if (name.length() > 10)
            name = name.substr (0, 10);
    }
    return name;
}

const char* deriveWidget (const ParamInfo& p)
{
    const std::string key = p.key;
    const size_t len = key.length();
    if ((len >= 3 && key.compare (len - 3, 3, ".on") == 0) ||
        (len >= 7 && key.compare (len - 7, 7, ".bypass") == 0))
    {
        return "toggle";
    }
    if (p.scale == Scale::stepped)
        return "stepped";
    if (p.min < 0.0f)
        return "bipolar_knob";
    return "knob";
}

int dumpParams()
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

int dumpKickPages()
{
    std::string out;
    out += "{\n";
    out += "  \"format\": \"synthyboy-kick-pages\",\n";
    out += "  \"version\": 1,\n";
    out += "  \"pluginId\": \"com.safian.synthyboy\",\n";
    out += "  \"macroDefaults\": {\n";

    const size_t catCount = categoryMacroDefaultCount();
    for (size_t c = 0; c < catCount; ++c)
    {
        const auto& cat = categoryMacroDefaults()[c];
        out += (c > 0 ? ",\n" : "");
        out += "    " + quote (cat.category) + ": [\n";
        for (size_t m = 0; m < 4; ++m)
        {
            const auto& mac = cat.macros[m];
            out += (m > 0 ? ",\n" : "");
            out += "      {\n";
            out += "        \"macroKey\": " + quote (mac.macroKey) + ",\n";
            out += "        \"label\": " + quote (mac.label) + ",\n";
            out += "        \"targets\": [";
            for (size_t t = 0; t < mac.targetCount; ++t)
            {
                const auto& tgt = mac.targets[t];
                out += (t > 0 ? ",\n          " : "\n          ");
                out += "{\"destination\": " + quote (tgt.destination)
                     + ", \"amount\": " + num (tgt.amount)
                     + ", \"bipolar\": " + (tgt.bipolar ? "true" : "false") + "}";
            }
            out += "\n        ]\n";
            out += "      }";
        }
        out += "\n    ]";
    }
    out += "\n  },\n";

    out += "  \"pages\": [";
    const size_t pageCount = kickPageCount();
    for (size_t p = 0; p < pageCount; ++p)
    {
        const auto& page = kickPageTable()[p];
        out += (p > 0 ? ",\n" : "\n");
        out += "    {\n";
        out += "      \"id\": " + quote (page.id) + ",\n";
        out += "      \"section\": " + quote (page.section) + ",\n";
        out += "      \"title\": " + quote (page.title) + ",\n";
        out += "      \"knobs\": [";

        for (size_t k = 0; k < kMaxKnobsPerPage; ++k)
        {
            out += (k > 0 ? ",\n        " : "\n        ");
            if (k < page.count && page.keys[k] != nullptr)
            {
                const ParamInfo* info = findParamByKey (page.keys[k]);
                const std::string shortLabel = (info != nullptr) ? deriveShortLabel (*info) : "";
                const std::string widget = (info != nullptr) ? deriveWidget (*info) : "knob";
                out += "{\"key\": " + quote (page.keys[k])
                     + ", \"shortLabel\": " + quote (shortLabel.c_str())
                     + ", \"widget\": " + quote (widget.c_str()) + "}";
            }
            else
            {
                out += "null";
            }
        }
        out += "\n      ]";

        if (page.moreKeys != nullptr && page.moreCount > 0)
        {
            out += ",\n      \"moreKeys\": [";
            for (size_t m = 0; m < page.moreCount; ++m)
            {
                out += (m > 0 ? ",\n        " : "\n        ");
                out += quote (page.moreKeys[m]);
            }
            out += "\n      ]\n";
        }
        else
        {
            out += "\n";
        }
        out += "    }";
    }
    out += "\n  ]\n";
    out += "}\n";

    std::fputs (out.c_str(), stdout);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc > 1 && std::strcmp (argv[1], "--kick-pages") == 0)
        return dumpKickPages();

    return dumpParams();
}
