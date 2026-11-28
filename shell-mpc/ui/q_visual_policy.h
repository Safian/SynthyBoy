// SynthyBoy - MPC plugin-drawn layer: Q-Link visual build policy.
// Defines hardware generation framing rules:
//   Gen 1 (ARMv7 32-bit standalone MPCs: Key 61, Live, One, X, etc.):
//     Factory-style bank frame and single unnumbered Q marker on 4-knob setups; zero numbered chips.
//   Gen 2 (AArch64 64-bit standalone MPCs: XL, Live III):
//     No bank frame, no Q marker, specific Q-frame drawing code compiled out.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#if defined(SYNTHYBOY_MPC_UI_GEN)
  #if SYNTHYBOY_MPC_UI_GEN == 2
    #ifndef SYNTHYBOY_MPC_QFRAME_ENABLED
      #define SYNTHYBOY_MPC_QFRAME_ENABLED 0
    #endif
  #else
    #ifndef SYNTHYBOY_MPC_QFRAME_ENABLED
      #define SYNTHYBOY_MPC_QFRAME_ENABLED 1
    #endif
  #endif
#elif defined(SYNTHYBOY_MPC_QFRAME_ENABLED)
  #if SYNTHYBOY_MPC_QFRAME_ENABLED
    #define SYNTHYBOY_MPC_UI_GEN 1
  #else
    #define SYNTHYBOY_MPC_UI_GEN 2
  #endif
#else
  #if defined(__aarch64__) && defined(__linux__)
    #define SYNTHYBOY_MPC_UI_GEN 2
    #define SYNTHYBOY_MPC_QFRAME_ENABLED 0
  #else
    #define SYNTHYBOY_MPC_UI_GEN 1
    #define SYNTHYBOY_MPC_QFRAME_ENABLED 1
  #endif
#endif

namespace synthyboy_mpc { namespace ui {


}} // namespace synthyboy_mpc::ui
