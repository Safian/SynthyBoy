// SynthyBoy - own engine tests: libKeys (), the key table of the library oscillators, is the only first-use initialiser of the audio path that used to run on the audio thread (its first call
// runs snprintf and hashing, CPU review section 5). OwnEngine::prepare () makes it now. This is a test executable of its own: the table is built once per process, so no other test may have
// built it before this one looks.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <vector>

#include "osc/lib_osc.h"
#include "synthyboy/engine.h"
#include "synthyboy/quality.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

TEST (prepare_builds_the_lib_keys_table_before_the_first_block)
{
    CHECK (! own::libKeysBuilt());   // (nothing in this process has built it yet: the check below can fail)
    auto e = createOwnEngine();
    CHECK (! own::libKeysBuilt());   // construction does not
    Quality q;
    q.realtime = false;
    e->prepare (48000.0, 128, q);
    CHECK (own::libKeysBuilt());     // prepare () does: the first process () call finds the table built
    std::vector<float> l (128), r (128);
    std::vector<Event> ev;
    ev.push_back (sbtest::noteOn (0, 60));
    e->process (ev.data(), 1, l.data(), r.data(), 128);
    CHECK (own::libKeysBuilt());
}
