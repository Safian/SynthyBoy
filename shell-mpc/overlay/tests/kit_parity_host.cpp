// SynthyBoy - MPC overlay tests: the KIT PARITY test (migration steps 2 and 3 of docs/ui-components/TEMPLATE.md). The pages of SynthyBoy now reach the parameters through the kit's parameter source
// (mpckit::synthyboy_adapter::Params) and the kit's text function (mpckit::formatValue); this test compares them with the old direct calls of the engine (a verbatim copy of the old ui_bind.h formatValue is kept
// here as the reference) for EVERY parameter of the table at a spread of values: the lookups, the conversions, the page text, the host text, the stepped rule, and the write of uibind::writePlain / writeNorm.
// It also holds the parity of the overlay runtime's tables (migration step 8): the page set of SynthyBoy against overlay_layout.h, the default instance colours, validatePages on SynthyBoy's layout and on broken ones,
// and the file names of the default branding against the old literal paths.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../kit/branding.h"
#include "../../kit/synthyboy_adapter.h"
#include "../../q_step_rule.h"
#include "../controller.h"
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

    // ---- the page set of SynthyBoy (kit/synthyboy_adapter.h) is the generated layout, and passes the checks of the generator
    {
        namespace L = layout;
        const mpckit::IPageSet& set = mpckit::synthyboy_adapter::PageSet::get();
        check (set.numPages() == L::kNumViews, "the page set has the " + std::to_string (L::kNumViews) + " views of overlay_layout.h");
        bool same = true;
        for (int i = 0; i < L::kNumViews && same; ++i)
        {
            const mpckit::PageDef& d = set.page (i);
            const L::View& v = L::kViews[i];
            same = std::string (d.name) == v.name && d.kind == v.kind && d.index == v.index && d.opaque == v.opaque && d.region.x == v.region.x && d.region.y == v.region.y && d.region.w == v.region.w && d.region.h == v.region.h
                   && d.instance.x == v.instance.x && d.instance.y == v.instance.y && d.instance.w == v.instance.w && d.instance.h == v.instance.h;
            for (int m = 0; m < 2; ++m)
                same = same && d.markers[m].rgb == v.markers[m].rgb && d.markers[m].cover == v.markers[m].cover && d.markers[m].rect.x == v.markers[m].rect.x && d.markers[m].rect.y == v.markers[m].rect.y;
            same = same && mpckit::isPageKind (d.kind) == L::isPageKind (d.kind);
        }
        check (same, "every page of the set is the entry of the generated table (name, kind, index, region, markers, instance picture, page kind)");
        const mpckit::InstanceColours& c = set.instanceColours();
        const mpckit::InstanceColours defaults;
        bool colours = c.none == L::kInstanceNoneRgb && c.blank == L::kInstanceBlankRgb && defaults.none == L::kInstanceNoneRgb && defaults.blank == L::kInstanceBlankRgb;
        for (int s = 0; s < L::kMaxInstances; ++s)
            colours = colours && c.slot[s] == L::kInstanceSlotRgb[s] && defaults.slot[s] == L::kInstanceSlotRgb[s];
        check (colours, "the instance colours of the set, and the kit's defaults, are those of overlay_layout.h");
        std::string why;
        check (mpckit::validatePages (set, why), "SynthyBoy's page set passes validatePages (" + why + ")");

        // a table with a fault is refused, each with its own reason
        struct Broken : mpckit::IPageSet
        {
            std::vector<mpckit::PageDef> defs;
            mpckit::InstanceColours colours;
            int numPages() const noexcept override { return static_cast<int> (defs.size()); }
            const mpckit::PageDef& page (int i) const noexcept override { return defs[static_cast<size_t> (i)]; }
            const mpckit::InstanceColours& instanceColours() const noexcept override { return colours; }
        };
        auto fresh = [&] { Broken b; for (int i = 0; i < set.numPages(); ++i) b.defs.push_back (set.page (i)); return b; };
        { Broken b = fresh(); b.defs[1].markers[0].rgb = b.defs[0].markers[0].rgb + 3; check (! mpckit::validatePages (b, why), "two marker colours within the tolerance are refused: " + why); }
        { Broken b = fresh(); b.colours.slot[3] = b.defs[2].markers[1].rgb; check (! mpckit::validatePages (b, why), "an instance colour that is a marker colour is refused: " + why); }
        { Broken b = fresh(); b.colours.blank = b.colours.none + 2; check (! mpckit::validatePages (b, why), "two instance colours within the tolerance are refused: " + why); }
        { Broken b = fresh(); b.defs[0].markers[1].cover = b.colours.slot[0]; check (! mpckit::validatePages (b, why), "an instance colour that is the colour under a marker is refused: " + why); }
        { Broken b = fresh(); b.defs[4].instance = { 2000, 2000, 6, 6 }; check (! mpckit::validatePages (b, why), "an instance picture outside its region is refused: " + why); }
        { Broken b = fresh(); b.defs[4].instance = { b.defs[4].markers[0].rect.x + 3, b.defs[4].markers[0].rect.y, 6, 6 }; check (! mpckit::validatePages (b, why), "an instance picture on a marker is refused: " + why); }
        { Broken b = fresh(); while (b.numPages() < 25) b.defs.push_back (b.defs[0]); check (! mpckit::validatePages (b, why), "more than 24 pages are refused: " + why); }
        { Broken b = fresh(); b.defs.clear(); check (! mpckit::validatePages (b, why), "no page is refused: " + why); }
    }
    // ---- the branding: the default is SynthyBoy's (the old literal names), a plugin's own prefix gives its own
    {
        const Paths old;
        const Paths now = Paths::forBranding (mpckit::Branding());
        check (now.killSwitch == old.killSwitch && now.rotation == old.rotation && now.noGpu == old.noGpu && now.status == old.status && now.statusRequest == old.statusRequest && now.detail == old.detail && now.gpuClock == old.gpuClock
               && now.rate == old.rate && now.pointerHotSpot == old.pointerHotSpot && now.look == old.look && now.dumpRequest == old.dumpRequest && now.dump == old.dump && now.qlinkSettings == old.qlinkSettings && now.backlightDir == old.backlightDir,
               "Paths::forBranding (default) is the paths the controller always had");
        const mpckit::Branding own { "mpckit-demo", "mpckit-demo-overlay-gpu", "MPCKIT_DEMO_Q_KNOBS" };
        const Paths mine = Paths::forBranding (own);
        check (mine.killSwitch == "/tmp/mpckit-demo-overlay-off" && mine.status == "/tmp/mpckit-demo-overlay-status.txt" && mine.look == "/tmp/mpckit-demo-overlay-look" && mine.qlinkSettings == old.qlinkSettings, "a plugin's branding gives its own files in /tmp, the host's own files stay");
        check (std::string (mpckit::branding().prefix) == "synthyboy" && mpckit::tmpFile ("overlay-zpos-rest") == "/tmp/synthyboy-overlay-zpos-rest" && mpckit::tmpFile ("overlay-gpu-minfreq.orig") == "/tmp/synthyboy-overlay-gpu-minfreq.orig" && mpckit::tmpFile (own, "overlay-gpu.log") == "/tmp/mpckit-demo-overlay-gpu.log",
               "the runtime's other files (zpos rest, GPU clock state, helper log) follow the branding; the default is the old names");
        check (std::string (mpckit::branding().helperName) == "synthyboy-overlay-gpu" && std::string (mpckit::branding().qKnobsEnv) == "SYNTHYBOY_MPC_Q_KNOBS", "the default helper name and Q knob variable are SynthyBoy's");
    }
    std::printf ("%s: %d checks, %d failed\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
