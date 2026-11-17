// MpcKit Demo - the developer log of the kit, if this branch has it. shell-mpc/kit/log.h (MPCKIT_LOG, MPCKIT_SCOPE; docs/ui-components/LOGGING.md) is compiled out of release builds and switched
// per category through a control file on the device. When the header is absent the macros below are empty, so the template compiles either way.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Rule: log through MPCKIT_LOG, never printf. DEMO_DEBUG is the debug level: set it to the identifier log.h uses for it (check LOGGING.md) when you wire the real header in.
#pragma once

#if defined(__has_include)
#if __has_include("../../shell-mpc/kit/log.h")
#include "../../shell-mpc/kit/log.h"
#define MPCKIT_HAVE_LOG 1
#endif
#endif

#ifndef MPCKIT_HAVE_LOG
#define MPCKIT_LOG(category, level, ...) ((void) 0)
#define MPCKIT_SCOPE(category, name) ((void) 0)
#endif

#ifndef DEMO_DEBUG
#define DEMO_DEBUG 3
#endif
