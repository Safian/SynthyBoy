// SynthyBoy - core: conversions between samples, rendered audio, wavetables and LFO shapes (GROUPD WP-14 / WP-15 / WP-16, 13-new-keys.md 4.40).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// JUCE-free and shared by both shells: the JUCE shell calls these through ContentIO, the native CLAP shell through the synthyboy.content extension.
// Nothing here touches a store or the state: the callers write the result back (the content pipeline of WP-01).
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "content_state.h"
#include "offline_render.h"
#include "wavetable_edit.h"

namespace synthyboy::convert
{
// ---- WP-14: Switch to wavetable (Sample, Granular, Spectral) ------------------------------------------------------------------------------------

/** One entry of the "Switch to Wavetable" menu: the import options of manual p.291-294 (the same list as the Import dialog of the editor). */
struct ImportChoice
{
    const char* label;
    wtedit::ImportMode mode;
};
const std::vector<ImportChoice>& importChoices();

/**
 * The region [startNorm, endNorm] (fractions of the sample file, the START / END of the oscillator) of a mono sample as a wavetable, by
 * wtedit::importAudioSamples. The table is named `name`. False for an empty region or when the import yields no frame; `error` receives the reason.
 */
bool sampleRegionToWavetable (const float* mono, size_t frames, double sampleRate, float startNorm, float endNorm, const wtedit::ImportOptions& options,
                              const std::string& name, wtedit::Wavetable& out, std::string* error = nullptr);

// ---- WP-15: renders ------------------------------------------------------------------------------------------------------------------------

/** How the helpers reach the engine: the shell gives a function that runs an OfflineRequest (UiHost::renderOffline, the CLAP shell's engine). */
using OfflineRunner = std::function<bool (const OfflineRequest&, OfflineAudio&)>;

/** Resample (manual p.324): `bars` bars at `bpm` of one note of the patch (key, velocity), captured stereo. */
struct ResampleOptions
{
    int key = 60;
    float velocity = 0.8f;
    int bars = 1;
    double bpm = 120.0;
    double sampleRate = 48000.0;
    bool ultra = false;
    double tailSeconds = 0.5;
};
bool renderResample (const OfflineRunner& run, const ResampleOptions& options, OfflineAudio& audio);

/**
 * The captured audio as wavetable(s): mono mix (or the `channel` of ImportOptions) into `left`; with `right` not null the right channel into `right`
 * (Resample to Osc A + Osc B: A gets the left channel, B the right one). Names are set from `name`.
 */
bool audioToWavetables (const OfflineAudio& audio, const wtedit::ImportOptions& options, const std::string& name, wtedit::Wavetable& left, wtedit::Wavetable* right,
                        std::string* error = nullptr);

/** Render OSC Warp (manual p.324): the current frame of oscillator `osc` swept over WARP 0..100 % in `frames` steps. */
struct WarpRenderOptions
{
    int osc = 1;
    int frames = 256;
    int key = 24;               // C1: a long cycle (1468 samples at 48 kHz) keeps the harmonics up to 1024 of the 2048-sample frame
    double sampleRate = 48000.0;
    bool ultra = false;
    bool rawFrame = false;      // the frame as the table has it: warp and morph off (Wavetable A to LFO); use frames = 2
    bool normalise = false;     // true: the whole table to a peak of 1; false: the level the oscillator had (a sine of the same chain calibrates it), scaled down only if a peak exceeds 1
};
/**
 * `values`: the parameters of the patch (required: the render starts from them). The oscillator must be a wavetable oscillator. `levelScale` (not null) receives the one gain the
 * whole table was multiplied with after the level calibration: 1 = the table plays at the level the oscillator had; below 1 = a peak above full scale had to be brought back,
 * warpBakeParameterChanges () then raises oscN.level to make up for it.
 */
bool renderOscWarp (const OfflineRunner& run, const ParamValues* values, const WarpRenderOptions& options, const std::string& name, wtedit::Wavetable& out, std::string* error = nullptr,
                    float* levelScale = nullptr);

/** The parameter changes that make the baked table sound like the warped oscillator did: WARP type None, Morph None, Remap off, WT POS = the warp amount it was baked at, and the
    oscillator level raised by 1 / levelScale (up to 100 %) when the table was scaled down. */
std::vector<std::pair<std::string, float>> warpBakeParameterChanges (int osc, float warpAmountPercent, float levelScale = 1.0f, float currentLevelPercent = 50.0f);

/** The last note (key, velocity, how long it was held) as audio with its release tail: the share / drag-out source. */
struct NoteRender
{
    int key = 60;
    float velocity = 0.8f;
    double heldSeconds = 1.0;
    double sampleRate = 48000.0;
    bool ultra = false;
    double tailSeconds = 4.0;
};
bool renderNote (const OfflineRunner& run, const NoteRender& note, OfflineAudio& audio);
/** A WAV file (24-bit stereo) of the audio, peaks above 0 dBFS scaled to 0 dBFS; empty for empty audio. */
std::vector<uint8_t> audioToWav (const OfflineAudio& audio, int bits = 24);

// ---- WP-16: wavetable frame <-> LFO shape ---------------------------------------------------------------------------------------------------

/** The frame at table position `position` (0..1 = first..last frame, linear between neighbours, like the engine's linear interpolation). */
wtedit::Frame interpolatedFrame (const wtedit::Wavetable& table, float position);

/**
 * A frame as LFO point list (manual p.197): the 2048 samples (and the closing point = the first sample) are reduced to at most `maxPoints` points of
 * straight segments, adding points where the error is largest until the largest deviation is <= `tolerance` (in LFO units: the LFO range 0..1 is the
 * frame range -1..+1, so a tolerance of 0.002 is 0.4 % of full scale). A frame whose peak exceeds 1 is scaled down to fit. The lfo slot is `lfo`.
 * `error` (not null) receives the largest deviation actually reached. False for maxPoints < 2.
 */
bool frameToLfoShape (const wtedit::Frame& frame, int lfo, ContentLfoShape& out, float tolerance = 0.002f, int maxPoints = kMaxLfoShapePoints, float* error = nullptr);

/** The built-in lfoN.shape choice 0..11 as the point list of LFO `lfo` (what "Custom" starts from); anything else gives a flat centre line. */
ContentLfoShape builtInLfoShape (int lfo, int builtInShape);

/** An LFO shape as a frame (manual p.198): the shape (points, curve powers, smoothing) sampled at phase i / 2048, range 0..1 -> -1..+1. */
void lfoShapeToFrame (const ContentLfoShape& shape, wtedit::Frame& out);
} // namespace synthyboy::convert
