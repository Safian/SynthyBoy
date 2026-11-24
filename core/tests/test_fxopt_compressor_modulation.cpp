// SynthyBoy - regression: useful continuous FX destinations, without renumbering old routes.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "fx/fx_module.h"
#include "synthyboy/engine.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;
namespace
{
int indexOf (const char* key)
{
    const auto* p = findParamByKey (key);
    CHECK (p != nullptr);
    return p != nullptr ? findParamIndex (p->id) : 0;
}
void set (IEngine& e, const char* key, float value)
{
    e.setParam (paramTable()[indexOf (key)].id, value);
}
std::vector<float> defaults()
{
    std::vector<float> b (paramCount());
    for (size_t i = 0; i < b.size(); ++i) b[i] = paramTable()[i].def;
    return b;
}
const char* const bands[] = { "low", "mid", "high" };
const char* const fields[] = { "lower_threshold", "upper_threshold", "lower_ratio", "upper_ratio" };
std::string bandKey (int b, int f) { return std::string ("compressor.") + bands[b] + "." + fields[f]; }
std::vector<float> moduleRender (const std::vector<float>& base, const std::vector<float>& delta)
{
    auto m = own::makeFxModule (1);
    m->prepare (48000.0, 128);
    own::FxFrame frame;
    frame.sampleRate = 48000.0;
    m->configure (frame, base.data(), delta.data());
    std::vector<float> l (8192), r (8192);
    sbtest::Rng rng (53);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = static_cast<float> (0.01 * (rng.uniform() * 2 - 1)
            + 0.025 * (std::sin (6.283185307179586 * 70.0 * i / 48000.0)
                     + std::sin (6.283185307179586 * 800.0 * i / 48000.0)
                     + std::sin (6.283185307179586 * 8000.0 * i / 48000.0)));
    m->run (l.data(), r.data(), static_cast<int> (l.size()));
    return l;
}
double difference (const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += std::fabs (static_cast<double> (a[i]) - b[i]);
    return d;
}
std::unique_ptr<IEngine> patch (bool enabled)
{
    auto e = createOwnEngine();
    Quality q = qualityForTier (Tier::high);
    e->prepare (48000, 128, q);
    set (*e, "osc1.on", 1); set (*e, "osc2.on", 0); set (*e, "osc3.on", 0);
    set (*e, "filter1.on", 0); set (*e, "filter2.on", 0);
    set (*e, "env1.attack", 0); set (*e, "env1.decay", 0); set (*e, "env1.sustain", 100);
    set (*e, "env2.attack", 80); set (*e, "env2.decay", 0); set (*e, "env2.sustain", 100);
    set (*e, "compressor.on", enabled ? 1 : 0); set (*e, "compressor.bands", 3);
    set (*e, "compressor.mix", 100); set (*e, "compressor.mid.upper_threshold", -40);
    set (*e, "compressor.mid.upper_ratio", 80);
    return e;
}
void route (IEngine& e, const char* destination, float amount)
{
    const int d = indexOf (destination);
    set (e, "mod01.source", 2); // Env 2: a voice envelope on a global FX destination.
    set (e, "mod01.destination", static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (d))));
    set (e, "mod01.amount", amount);
}
std::vector<float> sounding (IEngine& e)
{
    return sbtest::render (e, { sbtest::noteOn (0, 48) }, 16384, 128).left;
}
}

TEST (fx_compressor_new_destinations_append_without_changing_legacy_choices)
{
    // FNV-1a over every legacy destination key plus a NUL separator, captured from develop6016f24c params.json.
    uint64_t hash = UINT64_C (14695981039346656037);
    for (size_t choice = 1; choice <= 1249; ++choice)
    {
        const int idx = modDestinationParamIndex (choice);
        CHECK (idx >= 0);
        if (idx < 0) continue;
        const char* p = paramTable()[idx].key;
        do { hash ^= static_cast<unsigned char> (*p); hash *= UINT64_C (1099511628211); } while (*p++);
    }
    CHECK_EQ (hash, UINT64_C (5817392498058343415));
    CHECK_EQ (modDestinationCount(), 1261u);
    for (int b = 0; b < 3; ++b)
        for (int f = 0; f < 4; ++f)
        {
            const int idx = indexOf (bandKey (b, f).c_str());
            CHECK ((paramTable()[idx].flags & kParamModulatable) != 0);
            const size_t choice = 1250u + static_cast<size_t> (4 * b + f);
            CHECK_EQ (modDestinationChoiceOf (static_cast<size_t> (idx)), choice);
            CHECK_EQ (modDestinationParamIndex (choice), idx);
        }
    for (const char* key : { "compressor.bands", "compressor.limit", "chorus.voices", "reverb.type", "reverb.room", "reverb.er_size", "convolve.ir" })
        CHECK_EQ (modDestinationChoiceOf (static_cast<size_t> (indexOf (key))), 0u);
}

TEST (fx_compressor_all_twelve_modulated_values_reach_dsp_and_clamp)
{
    auto base = defaults();
    std::vector<float> delta (paramCount(), 0);
    base[indexOf ("compressor.bands")] = 0;
    for (int b = 0; b < 3; ++b)
    {
        base[indexOf (bandKey (b, 0).c_str())] = -30;
        base[indexOf (bandKey (b, 1).c_str())] = -50;
        base[indexOf (bandKey (b, 2).c_str())] = 20;
        base[indexOf (bandKey (b, 3).c_str())] = 50;
    }
    const auto dry = moduleRender (base, delta);
    for (int b = 0; b < 3; ++b)
        for (int f = 0; f < 4; ++f)
        {
            const int idx = indexOf (bandKey (b, f).c_str());
            const auto& info = paramTable()[idx];
            auto changed = base;
            // dB controls use a plain delta; percent controls use control units (1 ==100%).
            delta[idx] = f < 2 ? 8.0f : 0.25f;
            changed[idx] += f < 2 ? 8.0f : 25.0f;
            const auto mod = moduleRender (base, delta);
            const auto expected = moduleRender (changed, std::vector<float> (paramCount(), 0));
            CHECK_EQ (mod, expected);
            const double change = difference (mod, dry);
            if (change <= 1.0e-4) std::printf ("    no output change for %s: %.9g\n", info.key, change);
            CHECK (change > 1.0e-4);
            for (float extreme : { -1000.0f, 1000.0f })
            {
                delta[idx] = extreme;
                changed[idx] = extreme < 0 ? info.min : info.max;
                const auto edge = moduleRender (base, delta);
                CHECK (sbtest::allFinite (edge));
                CHECK_EQ (edge, moduleRender (changed, std::vector<float> (paramCount(), 0)));
            }
            delta[idx] = 0;
            CHECK_EQ (moduleRender (base, delta), dry);
        }
}

TEST (fx_compressor_envelope_changes_output_and_zero_depth_or_off_is_identical)
{
    auto plain = patch (true); const auto baseline = sounding (*plain);
    auto zero = patch (true); route (*zero, "compressor.mid.upper_threshold", 0);
    CHECK_EQ (sounding (*zero), baseline);
    auto env = patch (true); route (*env, "compressor.mid.upper_threshold", 25);
    const auto mod = sounding (*env);
    CHECK (sbtest::allFinite (mod));
    CHECK (difference (mod, baseline) > 1.0);
    auto off = patch (false); const auto dry = sounding (*off);
    auto offEnv = patch (false); route (*offEnv, "compressor.mid.upper_threshold", 25);
    CHECK_EQ (sounding (*offEnv), dry);
    CHECK_EQ (env->getParam (paramTable()[indexOf ("compressor.mid.upper_threshold")].id), -40.0f);
}

TEST (fx_compressor_destination_state_roundtrip_preserves_old_and_new_routes)
{
    for (float destination : { 1.0f, 258.0f, 519.0f, 1249.0f, 1250.0f, 1261.0f })
    {
        ParamValues before = ParamValues::defaults();
        before.plain[indexOf ("mod01.destination")] = destination;
        before.plain[indexOf ("compressor.mid.upper_threshold")] = -37;
        const auto saved = saveState (before);
        ParamValues after = ParamValues::defaults();
        CHECK_EQ (loadState (saved.data(), saved.size(), after).status, LoadStatus::ok);
        CHECK_EQ (after.plain[indexOf ("mod01.destination")], destination);
        CHECK_EQ (after.plain[indexOf ("compressor.mid.upper_threshold")], -37.0f);
    }
}

TEST (fx_compressor_pool_envelope_targets_only_its_own_slot)
{
    const auto& type = fxTypeDescriptor (kFxTypeCompressor);
    auto render = [&] (int activeSlot, int targetSlot, float amount)
    {
        auto e = patch (false);
        int thresholdNumber = -1;
        for (int s = 0; s < 2; ++s)
        {
            const auto& keys = fxInstanceKeys (s);
            e->setParam (paramTable()[keys.type].id, kFxTypeCompressor);
            for (int n = 0; n < type.count; ++n)
            {
                const auto& src = paramTable()[type.source[n]];
                float plain = src.def;
                if (std::strcmp (src.key, "compressor.bands") == 0) plain = 3;
                if (std::strcmp (src.key, "compressor.mix") == 0) plain = 100;
                if (std::strcmp (src.key, "compressor.mid.upper_threshold") == 0) { plain = -40; thresholdNumber = n; }
                if (std::strcmp (src.key, "compressor.mid.upper_ratio") == 0) plain = 80;
                e->setParam (paramTable()[keys.p[n]].id, fxInstanceNormalise (kFxTypeCompressor, n, plain));
            }
            e->setParam (paramTable()[keys.on].id, s == activeSlot ? 1.0f : 0.0f);
        }
        CHECK (thresholdNumber >= 0);
        const auto& target = paramTable()[fxInstanceKeys (targetSlot).p[thresholdNumber]];
        route (*e, target.key, amount);
        return sounding (*e);
    };
    for (int activeSlot = 0; activeSlot < 2; ++activeSlot)
    {
        const auto plain = render (activeSlot, activeSlot, 0);
        const auto own = render (activeSlot, activeSlot, 25);
        CHECK (sbtest::allFinite (own));
        CHECK (difference (plain, own) > 1.0);
        CHECK_EQ (render (activeSlot, 1 - activeSlot, 25), plain);
    }
}
