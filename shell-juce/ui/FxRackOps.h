// SynthyBoy - UI: the edits of the effect racks on the live parameters (docs/ui-design/18-FX-INSTANCES.md section 2.8).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The rack view, the module sheet of a slot and the tests use the same functions: read the live values, let core/fx_rack.h compute the writes, write the
// parameters that changed (one host gesture each). Message thread.
#pragma once

#include <cstdint>

#include "UiContext.h"
#include "synthyboy/fx_rack.h"

namespace synthyboy::ui::fx_ops
{
/** The live values of all parameters. */
ParamValues values (const UiContext& ctx);
/** The racks as the live parameters say. */
FxRacks racks (const UiContext& ctx);

bool setOn (UiContext& ctx, FxRackItem item, bool on);
bool toggleOn (UiContext& ctx, FxRackItem item);
/** Adds a module of `type` to the end of `rack` in the first free slot; returns the slot, -1 if the pool is full. */
int addModule (UiContext& ctx, int type, int rack);
bool removeSlot (UiContext& ctx, int slot);
/** Copies a module of instance 0 or a slot into the first free slot, behind the original; returns the new slot, -1 if there is none or the module has no type
    of the pool (the splitters). */
int duplicate (UiContext& ctx, FxRackItem item);
bool move (UiContext& ctx, FxRackItem item, int rack, int index);
/** Every module off (the powers are kept in `savedMask`: bit k = module k of instance 0, bit 16 + s = slot s); with nothing on, the saved ones come back
    (all of instance 0 if nothing was saved). */
void bypassAll (UiContext& ctx, uint32_t& savedMask);
} // namespace synthyboy::ui::fx_ops
