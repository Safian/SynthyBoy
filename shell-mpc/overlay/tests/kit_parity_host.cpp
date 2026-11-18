// SynthyBoy - MPC overlay tests: the KIT PARITY test (migration steps 2 and 3 of docs/ui-components/TEMPLATE.md). The pages of SynthyBoy now reach the parameters through the kit's parameter source
// (mpckit::synthyboy_adapter::Params) and the kit's text function (mpckit::formatValue); this test compares them with the old direct calls of the engine (a verbatim copy of the old ui_bind.h formatValue is kept
// here as the reference) for EVERY parameter of the table at a spread of values: the lookups, the conversions, the page text, the host text, the stepped rule, and the write of uibind::writePlain / writeNorm.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../q_step_rule.h"
#include "../ui_bind.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
int g_fails = 0, g_checks = 0;
void check (bool ok, const std::string& what)
{
    ++g_checks;
    if (! ok)
    {
        ++g_fails;
        if (g_fails <= 40)
            std::printf ("FAIL  %s\n", what.c_str());
    }
}

/** The old text function of ui_bind.h, before migration step 3 (the reference). */
namespace legacy
{
inline double noNegativeZero (double v, int decimals) noexcept
{
    double half = 0.5;
    for (int i = 0; i < decimals; ++i)
        half *= 0.1;
    return std::fabs (v) < half ? 0.0 : v;
}
inline void formatValue (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
{
    using synthyboy::Unit;
    double v = static_cast<double> (plain);
    const int dec = std::min<int> (pi.decimals, 2);
    if (pi.unit == Unit::percent && pi.min < 0.0f && pi.max > 0.0f)
    {
        const size_t n = std::strlen (pi.key);
        if (n >= 4 && std::strcmp (pi.key + n - 4, ".pan") == 0)
        {
            const double a = std::fabs (v);
            if (a < 0.5)
                std::snprintf (out, cap, "C");
            else
                std::snprintf (out, cap, v < 0.0 ? "L %.0f" : "R %.0f", a);
            return;
        }
    }
    switch (pi.unit)
    {
        case Unit::percent: v = noNegativeZero (v, dec); std::snprintf (out, cap, pi.min < 0.0f ? "%+.*f%%" : "%.*f%%", dec, v); break;
        case Unit::cents: v = noNegativeZero (v, 0); std::snprintf (out, cap, "%+.0f ct", v); break;
        case Unit::semitone: v = noNegativeZero (v, dec); std::snprintf (out, cap, "%+.*f st", dec, v); break;
        case Unit::Hz:
            if (v >= 1000.0) std::snprintf (out, cap, "%.2f kHz", v / 1000.0);
            else if (v < 10.0) std::snprintf (out, cap, "%.2f Hz", v);
            else if (v < 100.0) std::snprintf (out, cap, "%.1f Hz", v);
            else std::snprintf (out, cap, "%.0f Hz", v);
            break;
        case Unit::ms: if (v >= 1000.0) std::snprintf (out, cap, "%.2f s", v / 1000.0); else std::snprintf (out, cap, v < 10.0 ? "%.1f ms" : "%.0f ms", v); break;
        case Unit::dB: v = noNegativeZero (v, 1); std::snprintf (out, cap, "%+.1f dB", v); break;
        case Unit::degrees: v = noNegativeZero (v, 0); std::snprintf (out, cap, "%.0f\xc2\xb0", v); break;
        case Unit::beats: std::snprintf (out, cap, "%.2f beat", v); break;
        case Unit::voices: std::snprintf (out, cap, v == 1.0 ? "%.0f voice" : "%.0f voices", v); break;
        default: v = noNegativeZero (v, dec); std::snprintf (out, cap, "%.*f", dec, v); break;
    }
}
} // namespace legacy

/** An instance that records the last write (what a page's write reaches the plugin as). */
class Recorder final : public IInstanceView
{
public:
    float param (int) const noexcept override { return 0.0f; }
    bool live (synthyboy::LiveValues&) const noexcept override { return false; }
    double sampleRate() const noexcept override { return 48000.0; }
    void writeParam (int index, float normalised) noexcept override { lastIndex = index; lastNorm = normalised; ++writes; }
    int lastIndex = -1, writes = 0;
    float lastNorm = -1.0f;
};

/** The values a parameter is tried at: the ends of the range, the default, 33 steps across, a little beyond both ends, zero and a tiny negative number (the negative zero of the text). */
std::vector<float> valuesOf (const synthyboy::ParamInfo& p)
{
    std::vector<float> v;
    for (int j = 0; j <= 32; ++j)
        v.push_back (p.min + (p.max - p.min) * (static_cast<float> (j) / 32.0f));
    v.push_back (p.def);
    v.push_back (p.min - 1.0f);
    v.push_back (p.max + 1.0f);
    v.push_back (0.0f);
    v.push_back (-0.001f);
    v.push_back (0.499f);
    v.push_back (-0.499f);
    return v;
}
} // namespace

int main()
{
    const mpckit::IParamSource& kit = uibind::params();
    const synthyboy::ParamInfo* table = synthyboy::paramTable();
    const int n = static_cast<int> (synthyboy::paramCount());
    check (kit.count() == n && n > 100, "the kit's source has every parameter of the table (" + std::to_string (kit.count()) + " of " + std::to_string (n) + ")");
    check (uibind::indexOfKey (std::string()) == -1 && uibind::indexOfKey ("no.such.key") == -1, "an empty key and an unknown key are -1");

    Recorder rec;
    for (int i = 0; i < n; ++i)
    {
        const synthyboy::ParamInfo& p = table[i];
        const std::string key = p.key;
        const mpckit::ParamDesc& d = kit.desc (i);
        check (d.key == key || std::strcmp (d.key, p.key) == 0, "desc key " + key);
        check (d.min == p.min && d.max == p.max && d.def == p.def && d.skew == p.skew && d.interval == p.interval && d.decimals == p.decimals && d.numChoices == p.numChoices && d.choices == p.choices
               && static_cast<int> (d.scale) == static_cast<int> (p.scale) && static_cast<int> (d.unit) == static_cast<int> (p.unit), "desc fields of " + key);
        check (uibind::indexOfKey (key) == i && kit.indexOfKey (p.key) == i, "indexOfKey " + key);
        check (kit.stepCount (i) == stepCountOf (p), "stepped rule of " + key);

        for (float plain : valuesOf (p))
        {
            const std::string at = key + " at " + std::to_string (plain);
            check (kit.toNormalised (i, plain) == synthyboy::toNormalised (p, plain) && uibind::normOf (i, plain) == static_cast<double> (synthyboy::toNormalised (p, plain)), "toNormalised " + at);
            check (kit.constrain (i, plain) == synthyboy::constrainValue (p, plain), "constrain " + at);
            const float norm = synthyboy::toNormalised (p, plain);
            check (kit.fromNormalised (i, norm) == synthyboy::fromNormalised (p, norm), "fromNormalised " + at);

            char want[64] = {}, got[64] = {}, viaKit[64] = {};
            legacy::formatValue (p, plain, want, sizeof want);
            uibind::formatValue (p, plain, got, sizeof got);
            kit.valueText (i, plain, viaKit, sizeof viaKit);
            check (std::strcmp (want, got) == 0 && std::strcmp (want, viaKit) == 0, "page text " + at + ": old '" + want + "', uibind '" + got + "', kit '" + viaKit + "'");

            char hostWant[64] = {}, hostGot[64] = {};
            const size_t a = synthyboy::valueToText (p, plain, hostWant, sizeof hostWant);
            const size_t b = kit.hostText (i, plain, hostGot, sizeof hostGot);
            check (a == b && std::strcmp (hostWant, hostGot) == 0, "host text " + at);

            // the write of a page: writePlain constrains and normalises, writeNorm clamps
            uibind::writePlain (rec, i, static_cast<double> (plain));
            check (rec.lastIndex == i && rec.lastNorm == synthyboy::toNormalised (p, synthyboy::constrainValue (p, plain)), "writePlain " + at);
        }
        uibind::writeNorm (rec, i, 1.5);
        check (rec.lastIndex == i && rec.lastNorm == 1.0f, "writeNorm clamps above 1 " + key);
        uibind::writeNorm (rec, i, -0.5);
        check (rec.lastNorm == 0.0f, "writeNorm clamps below 0 " + key);
    }
    // a buffer of no size is left alone by the kit's text (the old one wrote into it): nothing to compare, but it must not crash
    char tiny[1] = { 'x' };
    kit.valueText (0, 0.0f, tiny, 0);
    check (tiny[0] == 'x', "a buffer of size 0 is not touched");

    std::printf ("%s: %d checks, %d failed\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
