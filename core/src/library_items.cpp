// SynthyBoy - core: the formats of the items of the user library (library_items.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/library_items.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "synthyboy/formula.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

namespace synthyboy
{
namespace
{
void appendEscaped (std::string& out, const std::string& text)
{
    out += '"';
    for (char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (c == '\n')
            out += "\\n";
        else if (static_cast<unsigned char> (c) < 0x20)
            out += ' ';
        else
            out += c;
    }
    out += '"';
}

/** The string member `key` of a flat object (escapes \" \\ \n \t). */
bool stringMember (const std::string& text, const char* key, std::string& out)
{
    const std::string needle = std::string ("\"") + key + "\"";
    size_t pos = text.find (needle);
    while (pos != std::string::npos)
    {
        size_t i = pos + needle.size();
        while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\t'))
            ++i;
        if (i < text.size() && text[i] == ':')
        {
            ++i;
            while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\t'))
                ++i;
            if (i < text.size() && text[i] == '"')
            {
                std::string value;
                for (++i; i < text.size() && text[i] != '"'; ++i)
                {
                    if (text[i] == '\\' && i + 1 < text.size())
                    {
                        ++i;
                        value += text[i] == 'n' ? '\n' : (text[i] == 't' ? '\t' : text[i]);
                    }
                    else
                        value += text[i];
                    if (value.size() > 8192)
                        return false;
                }
                out = std::move (value);
                return true;
            }
        }
        pos = text.find (needle, pos + 1);
    }
    return false;
}

bool startsWith (const char* s, const char* prefix) noexcept { return std::strncmp (s, prefix, std::strlen (prefix)) == 0; }
} // namespace

const std::vector<FormulaExample>& formulaExamples()
{
    static const std::vector<FormulaExample> examples = {
        { "Sine", "sin(2*pi*x)" },
        { "Saw", "1 - 2*w" },
        { "Square", "x < 0 ? -1 : 1" },
        { "Triangle", "abs(2*x) - 1" },
        { "2nd harmonic", "sin(2*pi*x) + 0.5*sin(4*pi*x)" },
        { "Bell partials", "sin(2*pi*x) + 0.7*sin(5.5*pi*x)" },
        { "Pulse 25 %", "w < 0.25 ? 1 : -1" },
        { "Half-wave rectified sine", "max(sin(2*pi*x), 0)" },
        { "Sine folded", "sin(2*pi*x + 2*sin(2*pi*x))" },
        { "Odd harmonics", "sin(2*pi*x) + sin(6*pi*x)/3 + sin(10*pi*x)/5" },
        { "Saw to square (y)", "(1 - 2*w) * (1 - y) + (x < 0 ? -1 : 1) * y" },
        { "Harmonic sweep (y)", "sin(2*pi*x * (1 + y * 7))" },
        { "Formant sweep (y)", "sin(2*pi*x) * (1 - y) + sin(6*pi*x) * y" },
        { "Pulse width sweep (y)", "w < 0.05 + 0.9*y ? 1 : -1" },
        { "Sine to saw (y)", "sin(2*pi*x) * (1 - y) + (1 - 2*w) * y" },
        { "Spectral partials (q)", "q < 16 ? 1.0 / q : 0.0" },
        { "Spectral odd partials (q)", "(q % 2 == 1) ? 1.0 / q : 0.0" },
    };
    return examples;
}

std::string serializeFormulaItem (const std::string& name, const std::string& expression)
{
    std::string s = "{\"format\":\"synthyboy-formula\",\"version\":1,\"name\":";
    appendEscaped (s, name);
    s += ",\"expression\":";
    appendEscaped (s, expression);
    s += "}";
    return s;
}

bool parseFormulaItem (const std::string& json, std::string& name, std::string& expression)
{
    std::string format, n, e;
    if (! stringMember (json, "format", format) || format != "synthyboy-formula" || ! stringMember (json, "expression", e) || e.empty()
        || e.size() > formula::kMaxExpressionLength)
        return false;
    stringMember (json, "name", n);
    name = std::move (n);
    expression = std::move (e);
    return true;
}

bool isFxChainKey (const char* key) noexcept
{
    if (key == nullptr)
        return false;
    if (startsWith (key, "fx.order.") || startsWith (key, "splitter_"))
        return true;
    if (startsWith (key, "fxi") && key[3] >= '0' && key[3] <= '9')
        return true;
    for (int t = 1; t < kNumFxInstanceTypes; ++t)
    {
        const std::string prefix = std::string (fxTypeDescriptor (t).prefix) + ".";
        if (startsWith (key, prefix.c_str()))
            return true;
    }
    return false;
}

std::string serializeFxRackPreset (const std::string& name, const ParamValues& values)
{
    std::string s = "{\"format\":\"synthyboy-fx-rack\",\"version\":2,\"name\":";
    appendEscaped (s, name);
    s += ",\"values\":{";
    bool first = true;
    const ParamInfo* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
    {
        if (! isFxChainKey (table[i].key))
            continue;
        char buf[160];
        std::snprintf (buf, sizeof buf, "%s\"%s\":%.9g", first ? "" : ",", table[i].key, static_cast<double> (values.plain[i]));
        s += buf;
        first = false;
    }
    s += "}}";
    return s;
}

int applyFxRackPreset (const std::string& json, ParamValues& values)
{
    std::string format;
    if (! stringMember (json, "format", format) || format != "synthyboy-fx-rack")
        return 0;
    const size_t block = json.find ("\"values\"");
    if (block == std::string::npos)
        return 0;
    size_t i = json.find ('{', block);
    if (i == std::string::npos)
        return 0;
    ++i;

    // what the document states, by table index (the migration of version 1 needs all of it before anything is written)
    std::vector<double> value (paramCount(), 0.0);
    std::vector<char> inDocument (paramCount(), 0);
    while (i < json.size())
    {
        const size_t q1 = json.find ('"', i);
        if (q1 == std::string::npos)
            break;
        const size_t q2 = json.find ('"', q1 + 1);
        if (q2 == std::string::npos)
            break;
        const std::string key = json.substr (q1 + 1, q2 - q1 - 1);
        size_t colon = json.find (':', q2);
        if (colon == std::string::npos)
            break;
        char* end = nullptr;
        const double v = std::strtod (json.c_str() + colon + 1, &end);
        if (end == json.c_str() + colon + 1)
            break;
        i = static_cast<size_t> (end - json.c_str());
        if (const ParamInfo* info = findParamByKey (key.c_str()); info != nullptr && isFxChainKey (key.c_str()) && std::isfinite (v))
        {
            const int index = findParamIndex (info->id);
            if (index >= 0)
            {
                value[static_cast<size_t> (index)] = v;
                inDocument[static_cast<size_t> (index)] = 1;
            }
        }
        const size_t next = json.find_first_of (",}", i);
        if (next == std::string::npos || json[next] == '}')
            break;
        i = next + 1;
    }

    // version 1 (no "version" member counts as 1): the pool values of the EQ and the compressor were stored in the ranges of state version 3 (fxPoolMigrateFromV3)
    int version = 1;
    if (const size_t at = json.find ("\"version\""); at != std::string::npos && at < block)
        if (const size_t colon = json.find (':', at); colon != std::string::npos)
            version = static_cast<int> (std::strtol (json.c_str() + colon + 1, nullptr, 10));
    const size_t count = paramCount();
    std::unique_ptr<bool[]> hasArr (new bool[count]), derivedArr (new bool[count]);
    for (size_t k = 0; k < count; ++k)
    {
        hasArr[k] = inDocument[k] != 0;
        derivedArr[k] = false;
    }
    if (version < 2)
    {
        // a slot whose type the document does not state keeps the type of `values`
        for (int slot = 0; slot < kNumFxInstances; ++slot)
        {
            const int t = fxInstanceKeys (slot).type;
            if (t >= 0 && ! hasArr[static_cast<size_t> (t)])
            {
                value[static_cast<size_t> (t)] = static_cast<double> (values.plain[static_cast<size_t> (t)]);
                hasArr[static_cast<size_t> (t)] = true;
            }
        }
        fxPoolMigrateFromV3 (value.data(), hasArr.get(), derivedArr.get());
    }

    int taken = 0;
    const ParamInfo* table = paramTable();
    for (size_t k = 0; k < count; ++k)
    {
        if (! inDocument[k] && ! (derivedArr[k] && hasArr[k]))
            continue;
        const ParamInfo& info = table[k];
        values.plain[k] = static_cast<float> (std::max (static_cast<double> (info.min), std::min (static_cast<double> (info.max), value[k])));
        if (inDocument[k])
            ++taken;
    }
    return taken;
}

bool isVoiceControlKey (const char* key) noexcept
{
    return key != nullptr && startsWith (key, "voicectl.");
}

std::string serializeVoiceControlPreset (const std::string& name, const ParamValues& values)
{
    std::string s = "{\"format\":\"synthyboy-voice-control\",\"version\":1,\"name\":";
    appendEscaped (s, name);
    s += ",\"values\":{";
    bool first = true;
    const ParamInfo* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
    {
        if (! isVoiceControlKey (table[i].key))
            continue;
        char buf[160];
        std::snprintf (buf, sizeof buf, "%s\"%s\":%.9g", first ? "" : ",", table[i].key, static_cast<double> (values.plain[i]));
        s += buf;
        first = false;
    }
    s += "}}";
    return s;
}

int applyVoiceControlPreset (const std::string& json, ParamValues& values)
{
    std::string format;
    if (! stringMember (json, "format", format) || format != "synthyboy-voice-control")
        return 0;
    const size_t block = json.find ("\"values\"");
    if (block == std::string::npos)
        return 0;
    size_t i = json.find ('{', block);
    if (i == std::string::npos)
        return 0;
    ++i;
    for (size_t k = 0; k < paramCount(); ++k)
        if (isVoiceControlKey (paramTable()[k].key))
            values.plain[k] = paramTable()[k].def;
    int taken = 0;
    while (i < json.size())
    {
        const size_t q1 = json.find ('"', i);
        if (q1 == std::string::npos)
            break;
        const size_t q2 = json.find ('"', q1 + 1);
        if (q2 == std::string::npos)
            break;
        const std::string key = json.substr (q1 + 1, q2 - q1 - 1);
        const size_t colon = json.find (':', q2);
        if (colon == std::string::npos)
            break;
        char* end = nullptr;
        const double v = std::strtod (json.c_str() + colon + 1, &end);
        if (end == json.c_str() + colon + 1)
            break;
        i = static_cast<size_t> (end - json.c_str());
        if (const ParamInfo* info = findParamByKey (key.c_str()); info != nullptr && isVoiceControlKey (key.c_str()) && std::isfinite (v))
        {
            const int index = findParamIndex (info->id);
            if (index >= 0)
            {
                values.plain[static_cast<size_t> (index)] = static_cast<float> (std::max (static_cast<double> (info->min), std::min (static_cast<double> (info->max), v)));
                ++taken;
            }
        }
        const size_t next = json.find_first_of (",}", i);
        if (next == std::string::npos || json[next] == '}')
            break;
        i = next + 1;
    }
    return taken;
}
} // namespace synthyboy
