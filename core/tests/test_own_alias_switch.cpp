// SynthyBoy - own engine tests: the anti-aliasing A/B switch (docs/plans/distortion-antialiasing.md task A0). Test and benchmark hook only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdint>

#include "synthyboy/aa_switch.h"
#include "test_runner.h"

using namespace synthyboy;

TEST (own_alias_switch_defaults_to_all_on_and_holds_the_mask)
{
    CHECK_EQ (aa::off(), 0u);
    aa::setOff (aa::kBlep | aa::kBlamp);
    CHECK_EQ (aa::off(), 12u);
    aa::setOff (aa::kAll);
    CHECK_EQ (aa::off(), 63u);
    aa::setOff (0);
    CHECK_EQ (aa::off(), 0u);
}
