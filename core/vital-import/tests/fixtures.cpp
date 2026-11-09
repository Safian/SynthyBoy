// SynthyBoy - tests of the Vital importer: fixture writer.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "fixtures.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "synthyboy/params.h"
#include "synthyboy/vital_import.h"

namespace sbfx
{
using namespace synthyboy;
using synthyboy::vitalimport::Conv;
using synthyboy::vitalimport::VitalRow;

namespace
{
constexpr int kWarpMap[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11 };
}

double forward (const VitalRow& r, double p)
{
    const std::string control = r.control;
    if (control.size() > 11 && control.compare (0, 4, "osc_") == 0
        && (control.compare (6, std::string::npos, "wave_frame") == 0 || control.compare (6, std::string::npos, "frame_spread") == 0))
        return p * 0.01 * 256.0; // F = 257 (no imported table)
    switch (r.conv)
    {
        case Conv::linear: return p * r.a + r.b;
        case Conv::sqrtScaled: return std::sqrt (std::max (0.0, p * r.a));
        case Conv::quarticMs: return std::pow (std::max (0.0, p) * 0.001, 0.25);
        case Conv::log2Ms: return std::max (r.a, std::log2 (std::max (p * 0.001, 1.0e-12)));
        case Conv::log2Hz: return std::log2 (std::max (p, 1.0e-6));
        case Conv::log2PeriodMs: return std::log2 (1000.0 / std::max (p, 1.0e-3));
        case Conv::noteFromHz: return 12.0 * std::log2 (std::max (p, 1.0) / 8.1757989156437073);
        case Conv::index: return p + r.b;
        case Conv::indexMap: return kWarpMap[std::max (0, std::min (10, static_cast<int> (std::floor (p + 0.5))))];
        case Conv::volumeDb:
        {
            const double shown = p <= -60.0 ? -80.0 : std::min (p, 6.0);
            return (shown + 80.0) * (shown + 80.0);
        }
    }
    return p;
}

std::string fmt (double v)
{
    char b[64];
    std::snprintf (b, sizeof b, "%.17g", v);
    return b;
}

std::string quote (const std::string& s)
{
    std::string o = "\"";
    for (const char ch : s)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char> (c); }
        else if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); o += b; }
        else o += static_cast<char> (c);
    }
    return o + "\"";
}

std::string b64 (const void* data, size_t size)
{
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* p = static_cast<const unsigned char*> (data);
    std::string o;
    for (size_t i = 0; i < size; i += 3)
    {
        const uint32_t v = (static_cast<uint32_t> (p[i]) << 16) | (i + 1 < size ? static_cast<uint32_t> (p[i + 1]) << 8 : 0) | (i + 2 < size ? p[i + 2] : 0u);
        o += t[(v >> 18) & 63];
        o += t[(v >> 12) & 63];
        o += i + 1 < size ? t[(v >> 6) & 63] : '=';
        o += i + 2 < size ? t[v & 63] : '=';
    }
    return o;
}

std::string Fixture::json() const
{
    std::string s = "{";
    if (! omitVersion)
        s += "\"synth_version\":" + quote (version) + ",";
    s += "\"preset_name\":" + quote (name) + ",\"author\":" + quote (author) + ",\"comments\":" + quote (comments) + ",\"preset_style\":" + quote (style);
    for (const auto& kv : top)
        s += "," + quote (kv.first) + ":" + kv.second;
    s += ",\"settings\":{";
    bool first = true;
    for (const auto& kv : settings)
    {
        if (! first) s += ',';
        first = false;
        s += quote (kv.first) + ":" + kv.second;
    }
    if (! mods.empty())
    {
        if (! first) s += ',';
        first = false;
        s += "\"modulations\":[";
        for (size_t i = 0; i < mods.size(); ++i)
        {
            if (i) s += ',';
            s += "{\"source\":" + quote (mods[i].first) + ",\"destination\":" + quote (mods[i].second);
            const auto it = modExtra.find (std::to_string (i));
            if (it != modExtra.end())
                s += "," + it->second;
            s += "}";
        }
        s += "]";
    }
    for (const auto& kv : { std::make_pair ("wavetables", &wavetables), std::make_pair ("lfos", &lfos), std::make_pair ("sample", &sample), std::make_pair ("tuning", &tuning) })
        if (! kv.second->empty())
        {
            if (! first) s += ',';
            first = false;
            s += std::string ("\"") + kv.first + "\":" + *kv.second;
        }
    s += "}}";
    return s;
}

void writeValues (Fixture& f, const ParamValues& values)
{
    size_t n = 0;
    const VitalRow* rows = vitalimport::vitalRows (n);
    for (size_t i = 0; i < n; ++i)
    {
        const ParamInfo* p = findParamByKey (rows[i].key);
        if (p == nullptr)
            continue;
        f.set (rows[i].control, forward (rows[i], values.plain[findParamIndex (p->id)]));
    }
}

int encodeEffectOrder (const ParamValues& values)
{
    int want[9];
    for (int i = 0; i < 9; ++i)
        want[i] = static_cast<int> (values.plain[findParamIndex (findParamByKey (("fx.order." + std::to_string (i + 1)).c_str())->id)]);
    for (int code = 0; code < 362880; ++code)
    {
        int order[9];
        decodeVitalEffectOrder (code, order);
        if (std::equal (order, order + 9, want))
            return code;
    }
    return 0;
}

Fixture defaultPatch()
{
    Fixture f;
    ParamValues d = ParamValues::defaults();
    writeValues (f, d);
    // the specials at their defaults: routing (parallel), effect order (identity = code 0 is the identity in the decoder's convention, see tests), bend range
    f.set ("filter_1_filter_input", 0);
    f.set ("filter_2_filter_input", 0);
    f.set ("pitch_bend_range", 2);
    f.set ("effect_chain_order", encodeEffectOrder (d));
    return f;
}

std::string waveKeyframe (int position, const std::vector<float>& wave)
{
    std::vector<uint8_t> bytes (wave.size() * 4);
    for (size_t i = 0; i < wave.size(); ++i)
    {
        uint32_t u;
        std::memcpy (&u, &wave[i], 4);
        for (int k = 0; k < 4; ++k)
            bytes[i * 4 + static_cast<size_t> (k)] = static_cast<uint8_t> (u >> (8 * k));
    }
    return "{\"position\":" + std::to_string (position) + ",\"wave_data\":\"" + b64 (bytes.data(), bytes.size()) + "\"}";
}

std::string component (const std::string& type, const std::string& extra, const std::vector<std::string>& keyframes)
{
    std::string s = "{\"type\":" + quote (type);
    if (! extra.empty())
        s += "," + extra;
    s += ",\"keyframes\":[";
    for (size_t i = 0; i < keyframes.size(); ++i)
        s += (i ? "," : "") + keyframes[i];
    return s + "]}";
}

std::string wavetable (const std::string& name, const std::vector<std::string>& components, const std::string& flags)
{
    std::string s = "{\"name\":" + quote (name) + ",\"author\":\"A\",\"version\":\"1.5.5\"";
    if (! flags.empty())
        s += "," + flags;
    s += ",\"groups\":[{\"components\":[";
    for (size_t i = 0; i < components.size(); ++i)
        s += (i ? "," : "") + components[i];
    return s + "]}]}";
}

std::vector<float> sineWave (int harmonic)
{
    std::vector<float> w (2048);
    for (int i = 0; i < 2048; ++i)
        w[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * harmonic * i / 2048.0));
    return w;
}

std::string lineObject (const std::vector<std::pair<double, double>>& points, const std::vector<double>& powers, bool smooth)
{
    std::string s = "{\"name\":\"L\",\"num_points\":" + std::to_string (points.size()) + ",\"points\":[";
    for (size_t i = 0; i < points.size(); ++i)
        s += (i ? "," : "") + fmt (points[i].first) + "," + fmt (points[i].second);
    s += "],\"powers\":[";
    for (size_t i = 0; i < powers.size(); ++i)
        s += (i ? "," : "") + fmt (powers[i]);
    return s + "],\"smooth\":" + (smooth ? "true" : "false") + "}";
}
} // namespace sbfx
