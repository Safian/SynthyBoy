// SynthyBoy - MPC overlay: the casts of the generic view types. overlay/view.h speaks the kit's language (ViewInput carries an mpckit::IInstance and an opaque live block); every instance SynthyBoy puts on the
// overlay is an IInstanceView and its live block is synthyboy::LiveValues, so the views and pages of SynthyBoy take them back with these (migration step 5 of docs/ui-components/TEMPLATE.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "curve_source.h"
#include "synthyboy/readout.h"
#include "view.h"

namespace synthyboy_mpc { namespace overlay {

/** The instance of an update as SynthyBoy's view of it (`in.instance` must not be null). */
inline const IInstanceView& instanceOf (const ViewInput& in) noexcept { return static_cast<const IInstanceView&> (*in.instance); }
/** The writable instance of an update (null where a test has none). */
inline IInstanceView* ioOf (const ViewInput& in) noexcept { return static_cast<IInstanceView*> (in.io); }
/** The live block of an update (null: the engine has written nothing yet). */
inline const synthyboy::LiveValues* liveOf (const ViewInput& in) noexcept { return static_cast<const synthyboy::LiveValues*> (in.live); }

}} // namespace synthyboy_mpc::overlay
