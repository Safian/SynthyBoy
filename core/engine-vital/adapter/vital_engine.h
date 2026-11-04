// SynthyBoy - engine-vital adapter: IEngine over the Vital-derived DSP (sbvital::SoundEngine).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This header is Vital-free (it only declares the factory and diagnostics). Parameter mapping: core/engine-vital/README.md.
#pragma once

#include <cstddef>
#include <memory>

#include "synthyboy/engine.h"

namespace synthyboy
{
/** The Vital-derived engine (only part of the build with SYNTHYBOY_WITH_VITAL=ON). Use createEngine (EngineKind::vital). */
std::unique_ptr<IEngine> createVitalEngine();

/** True if the engine came from createVitalEngine(), has been prepared, and every Vital control the adapter addresses
    exists (a typo in a control name would otherwise silently do nothing). For tests and diagnostics. */
bool isVitalEngineConfigured (const IEngine& engine) noexcept;

/** Diagnostics for the tests (the engine must be a prepared Vital engine). */
double vitalControlValue (const IEngine& engine, const char* control) noexcept;          // current value of an engine control, NaN if unknown
bool vitalHasModulationDestination (const IEngine& engine, size_t paramIndex) noexcept; // the parameter's control is a modulation destination
int vitalConnectedSlots (const IEngine& engine) noexcept;                                // modulation slots currently wired in the engine
int vitalEffectiveVoiceLimit (const IEngine& engine) noexcept;                           // value of the engine's polyphony control
} // namespace synthyboy
