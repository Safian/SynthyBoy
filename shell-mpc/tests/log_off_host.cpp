// The developer log compiled out (shell-mpc/kit/log.h with MPCKIT_DEV_LOG 0, the release): every macro is an empty statement and its arguments are not evaluated, not even compiled (the names below do not exist).
// Usage: log_off_host. The acceptance proof for a release binary is in docs/ui-components/LOGGING.md ("strings" finds no log tag).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#define MPCKIT_DEV_LOG 0
#include <cstdio>

#include "kit/log.h"

int main()
{
    int sideEffects = 0;
    MPCKIT_LOG_START ("never-started");
    MPCKIT_LOG ("startup", info, "%d %s", ++sideEffects, this_name_does_not_exist);
    MPCKIT_LOG ("startup", error, "also %d", ++sideEffects);
    MPCKIT_SCOPE ("startup", (++sideEffects, nothing_either));
    if (sideEffects != 0 || true)
        for (int i = 0; i < 3; ++i)
            MPCKIT_LOG ("loop", trace, "in an unbraced if and a loop %d", ++sideEffects);   // an empty statement must still be one statement
    if (sideEffects != 0)
    {
        std::printf ("FAIL a compiled-out log call evaluated its arguments (%d)\n", sideEffects);
        return 1;
    }
    std::printf ("log_off_host: all passed\n");
    return 0;
}
