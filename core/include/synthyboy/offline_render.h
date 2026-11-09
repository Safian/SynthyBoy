// SynthyBoy - core: the offline render entry of the engine (GROUPD WP-15, docs/ui-design/13-new-keys.md 4.40).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A render of notes into audio that is not the live signal path: Resample to wavetable (S p.324), Render OSC Warp, share / drag-out of the last
// note. The renderer works on its OWN instance of the engine (a copy of the parameters and the content of the live engine), so the live engine
// is only read: it is not reset, not retuned, no voice of it is touched, and the render is deterministic (Quality::realtime = false: helper
// thread work is waited for) at the full quality of the high tier, without the caps of the tier the plugin runs in. "Ultra quality" is the
// 4x oversampling of IEngine::setUltraRender (preferences.h ultraRender; the callers pass the preference).
//
// Message thread only (it allocates and may take a while); never from the audio thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace synthyboy
{
struct ParamValues;
struct StateContent;
class IContentStore;
class IEngine;
enum class EngineKind;

/** A new value for a parameter (plain value, constrained by the engine). */
struct OfflineOverride
{
    uint32_t id = 0;
    float value = 0.0f;
};

/** One note of a render: key, velocity, start (relative to the step) and length in seconds. */
struct OfflineNote
{
    int key = 60;
    float velocity = 0.8f;
    double startSeconds = 0.0;
    double lengthSeconds = 1.0;
    int channel = 0;
};

/** A part of a render. The overrides are applied before it (with `reset` the engine snaps its smoothers to them and silences the voices first,
    so a step does not glide from the one before: 256 steps = 256 independent renders in one instance). */
struct OfflineStep
{
    std::vector<OfflineOverride> overrides;
    std::vector<OfflineNote> notes;
    double seconds = 1.0;
    bool reset = true;
};

struct OfflineRequest
{
    double sampleRate = 48000.0;
    /** "Use Ultra quality when rendering" (Preferences::ultraRender): 4x oversampling instead of the engine's setting. */
    bool ultra = false;
    double bpm = 120.0;
    /** Start from these values instead of the ones the live engine holds (the shell passes the values of its parameter tree: the engine only has
        them after the next audio block). Null: the live engine's values. */
    const ParamValues* values = nullptr;
    /** Applied once before the first step (after `values`). */
    std::vector<OfflineOverride> overrides;
    std::vector<OfflineStep> steps;
    /** After the last step the voices ring out for at most this long; the render ends earlier when the output stayed below -90 dB for 250 ms. */
    double tailSeconds = 0.0;
};

struct OfflineAudio
{
    double sampleRate = 0.0;
    std::vector<float> left, right;
    /** First frame of every step (the steps follow each other; the tail comes after the last). */
    std::vector<size_t> stepStart;
    size_t frames() const noexcept { return left.size(); }
};

/** The longest render accepted (all steps and the tail), in seconds. */
constexpr double kMaxOfflineSeconds = 180.0;

/**
 * Runs `request` on `twin`, a fresh engine instance that nobody else uses: prepare () at the quality of an offline render (the high tier: every voice,
 * unison voice and effect, spectral interpolation; 2x oversampling, 4x for `ultra`; deterministic), `afterPrepare` (content and store go
 * in here), the values of the request, the steps and the tail. False for a request that is empty, longer than kMaxOfflineSeconds or has a bad sample
 * rate. The engine implementations call this on their private copy; so does renderOfflineOnTwin ().
 */
bool runOfflineRequest (IEngine& twin, const OfflineRequest& request, OfflineAudio& out, const std::function<void (IEngine&)>& afterPrepare = {});

/**
 * The generic entry for an engine without a render of its own (engine-own on iOS): a new engine of `kind`, the parameters of `live` copied through
 * getParam (and `request.values`), `content` / `store` handed to it, then runOfflineRequest. `live` is only read.
 */
bool renderOfflineOnTwin (const IEngine& live, EngineKind kind, const StateContent* content, const IContentStore* store, const OfflineRequest& request, OfflineAudio& out);

/** IEngine::renderOffline of `live`; when the engine has none, renderOfflineOnTwin with `fallbackKind`. */
bool renderOfflineAny (IEngine& live, EngineKind fallbackKind, const StateContent* content, const IContentStore* store, const OfflineRequest& request, OfflineAudio& out);
} // namespace synthyboy
