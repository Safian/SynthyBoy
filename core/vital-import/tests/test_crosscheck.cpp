// SynthyBoy - cross-check tests of the Vital importer against the Vital-derived engine (built only with SYNTHYBOY_WITH_VITAL).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// docs/ui-design/11-vital-preset-import.md Annex C.1: (b) the effect order decoder equals the engine's for all 362 880 codes,
// (c) every adapter binding has an importer row with the same control, key, conversion; plus the reference shapes of the built-in
// wavetables equal the engine's tables.

#include <cmath>
#include <map>
#include <string>

#include "synthyboy/vital_import.h"
#include "test_runner.h"
#include "utils.h"
#include "vital_bindings.h"
#include "vital_import_internal.h"
#include "vital_tables.h"

using namespace synthyboy;

TEST (crosscheck_effect_order_decoder_equals_engine)
{
    long mismatches = 0;
    for (int code = 0; code < 362880; ++code)
    {
        int mine[9], theirs[9];
        decodeVitalEffectOrder (code, mine);
        sbvital::utils::decodeFloatToOrder (theirs, static_cast<float> (code), 9);
        for (int i = 0; i < 9; ++i)
            if (mine[i] != theirs[i])
            {
                ++mismatches;
                break;
            }
    }
    CHECK_EQ (mismatches, 0L);
}

TEST (crosscheck_table_equals_adapter_bindings)
{
    size_t n = 0;
    const auto* rows = vitalimport::vitalRows (n);
    std::map<std::string, const vitalimport::VitalRow*> byKey;
    for (size_t i = 0; i < n; ++i)
        byKey[rows[i].key] = &rows[i];
    CHECK_EQ (vitalBindings().size(), n);
    for (const auto& b : vitalBindings())
    {
        const char* key = paramTable()[b.param].key;
        const auto it = byKey.find (key);
        CHECK (it != byKey.end());
        if (it == byKey.end())
            continue;
        CHECK (b.control == it->second->control);
        CHECK_EQ (static_cast<int> (b.conv), static_cast<int> (it->second->conv));
        CHECK_NEAR (b.a, it->second->a, 1e-12);
        CHECK_NEAR (b.b, it->second->b, 1e-12);
        // the control exists in the engine
        CHECK (vitalControlRange (b.control).exists);
    }
}

TEST (crosscheck_builtin_wavetable_reference_equals_engine)
{
    std::vector<float> mine (2048), theirs (2048);
    for (int table = 0; table < vitalimport::kBuiltinWavetables; ++table)
    {
        const int frames = vitalimport::builtinWavetableFrames (table);
        for (int f : { 0, frames - 1, frames / 2 })
        {
            vitalimport::builtinWavetableFrame (table, frames > 1 ? static_cast<double> (f) / (frames - 1) : 0.0, mine.data());
            vitaltables::renderFrame (table, f, theirs.data());
            double m = 0.0;
            for (int i = 0; i < 2048; ++i)
                m = std::max (m, static_cast<double> (std::fabs (mine[static_cast<size_t> (i)] - theirs[static_cast<size_t> (i)])));
            CHECK (m < 2e-3);
        }
    }
}
