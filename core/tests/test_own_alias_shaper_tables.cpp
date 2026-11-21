// SynthyBoy - own engine tests: the X-Shaper curve tables of the ContentSet (docs/plans/distortion-antialiasing.md task X1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>

#include "osc/content_set.h"
#include "synthyboy/content_state.h"
#include "test_runner.h"

using namespace synthyboy;

TEST (own_alias_shaper_tables_sample_the_drawn_curves_and_their_integrals)
{
    StateContent c;
    c.shaperA = { ContentPoint (0.0f, 0.0f), ContentPoint (0.5f, 0.8f), ContentPoint (1.0f, 1.0f) };
    const auto set = own::ContentSet::build (c, nullptr);
    CHECK_EQ (set->shaper[0].size(), 513u);
    CHECK_EQ (set->shaper[0][0], 0.0f);
    CHECK_EQ (set->shaper[0][256], 0.8f);
    CHECK_EQ (set->shaper[0][512], 1.0f);
    CHECK_NEAR (set->shaper[0][128], 0.4, 1e-6);
    CHECK_EQ (set->shaperInt[0].size(), 513u);
    CHECK_NEAR (set->shaperInt[0][256], 0.2, 1e-6);  // 0.5 * 0.5 * 0.8
    CHECK_NEAR (set->shaperInt[0][512], 0.65, 1e-6); // 0.2 + 0.5 * (0.8 + 1) / 2
    CHECK_EQ (set->shaperInt[0][0], 0.0f);
    // B is not drawn: the identity (empty)
    CHECK (set->shaper[1].empty());
    CHECK (set->shaperInt[1].empty());
}

TEST (own_alias_shaper_tables_one_point_curve_is_the_identity)
{
    StateContent c;
    c.shaperB = { ContentPoint (0.5f, 0.5f) };
    const auto set = own::ContentSet::build (c, nullptr);
    CHECK (set->shaper[1].empty());
    CHECK (set->shaperInt[1].empty());
}
