// SynthyBoy (AGPL-3.0-or-later): the Serum modules of the effect chain (docs/ui-design/13-new-keys.md 4.12), rendered by core/fx-serum-a and
// core/fx-serum-b: Hyper / Dimension, Utility, Bode, Convolve and the three splitters.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// New file in the engine-vital fork (not part of the imported Vital sources, listed in docs/UPSTREAM.md).
#pragma once

#include <memory>
#include <vector>

#include "reorderable_effect_chain.h"

namespace sbvital {
  /** The impulse response of the Convolve module (decoded content.impulses[0]); channels 1 or 2, interleaved. */
  struct ImpulseData {
    std::vector<float> samples;
    int frames = 0;
    int channels = 1;
    double sample_rate = 48000.0;
  };

  /** Creates the module with the index (ReorderableEffectChain::kHyperDimension .. kSplitterMs). beats_per_second and note_counter outlive it. */
  SynthModule* createSerumModule(int index, const Output* beats_per_second, const int* note_counter);

  /** Deterministic (false) or realtime (true) handover of rebuilt convolvers, see ConvolveModule::setRealtime. */
  void setConvolveRealtime(SynthModule* convolve, bool realtime);

  /** Gives the Convolve module its impulse response (message thread; the audio thread never touches the pointer). */
  void setConvolveImpulse(SynthModule* convolve, std::shared_ptr<const ImpulseData> impulse);
} // namespace sbvital
