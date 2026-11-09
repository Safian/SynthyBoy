// SynthyBoy - UI: the content pipeline flows (WP-01): audio file decoding (juce::AudioFormatManager), the user content library and the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every function runs on the message thread and goes through the UiHost: the files land in host.contentLibrary (), the state `content` is changed with
// host.setContent () (which hands it to the engine), the parameters are set as host gestures. The panels only call these; nothing here draws.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "UiContext.h"
#include "synthyboy/user_content.h"
#include "synthyboy/vital_plugin_import.h"
#include "synthyboy/wavetable_edit.h"

namespace synthyboy::ui
{
/** A file the user picked, decoded to floats. */
struct DecodedAudio
{
    std::vector<float> left, right; // right empty = mono
    double sampleRate = 0.0;
    juce::String name; // file name without the extension
    int channels() const noexcept { return right.empty() ? 1 : 2; }
};

/** Every format juce::AudioFormatManager::registerBasicFormats () reads (WAV, AIFF, FLAC, Ogg Vorbis, MP3 where the platform has it, CAF). */
juce::String audioFileWildcard();
bool decodeAudioFile (const juce::File& file, DecodedAudio& out, juce::String* error = nullptr);
/** The bytes of a file the user picked; empty on failure or above the size cap of the content store. */
std::vector<uint8_t> readFileBytes (const juce::File& file);
bool writeFileBytes (const juce::File& file, const std::vector<uint8_t>& bytes);

/** Sets a parameter of the table by key as a host gesture (a choice by its index, a float by its plain value). False for an unknown key. */
bool setParameter (UiHost& host, const char* key, float plain);
float readParameter (UiHost& host, const char* key, float fallback = 0.0f);
/** The index of the entry named `name` (case insensitive prefix match) in the choice list of `key`, or -1. */
int choiceIndexOf (const char* key, const char* name);

/** The result line every flow reports (shown in the sheet / status line, asserted by the tests). */
struct FlowResult
{
    bool ok = false;
    juce::String message;
};

/** Sample: decode `file`, store it (a WAV file is stored as it is, any other format as 24-bit WAV), attach it to the oscillator, set the root note
    from the file name / WAV chunk, switch `oscN.type` to Sample. */
FlowResult loadSampleFile (UiContext& ctx, int osc, const juce::File& file);
/** The stored sample behind the oscillator (a copy of its bytes), empty when there is none. */
std::vector<uint8_t> storedSampleBytes (UiHost& host, int osc);
/** Cuts the sample of the oscillator to its START .. END region: stores the new WAV, rescales the slices, resets START / END (and the loop markers). */
FlowResult trimSample (UiContext& ctx, int osc);
FlowResult removeSample (UiContext& ctx, int osc);

/** Wavetable: store the table as a 16-bit WAV (the user pack, or the embedded pack for `embed`), attach it to the oscillator and set oscN.wavetable to
    the Preset table entry. */
FlowResult saveWavetable (UiContext& ctx, int osc, const wtedit::Wavetable& table, bool embed);
/** The table behind the oscillator: the stored file when oscN.wavetable is the Preset table and the engine content has one, false otherwise. */
bool loadWavetableOf (UiHost& host, int osc, wtedit::Wavetable& out);
/** Impulse response for Convolve: decode, store, `content.impulses[0]`, convolve.ir = Preset IR. */
FlowResult loadImpulseFile (UiContext& ctx, const juce::File& file);
FlowResult removeImpulse (UiContext& ctx);
/** The stored impulse response as a mono mix (empty when there is none), for the display. */
bool loadImpulsePreview (UiHost& host, std::vector<float>& mono, double& sampleRate);

/** Vital file (.vital / .vitalbank / .vitaltable / .vitallfo). A preset replaces the patch (host.loadPatch), a table goes to the library and the
    oscillator `osc`, a shape to the LFO `lfo` (1..10; 0 = not a target). The result carries what the report sheet shows. */
struct VitalFlowResult
{
    FlowResult flow;
    VitalPluginImport imported;
};
VitalFlowResult importVitalFile (UiContext& ctx, const juce::File& file, int osc, int lfo);

/** Decoded forms of library files for the views, keyed by the SHA-256 of the reference (so a replaced file is decoded again). Message thread. */
class ContentCache
{
public:
    std::shared_ptr<const SamplePreview> samplePreview (UiHost& host, const ContentFileRef& ref);
    std::shared_ptr<const wtedit::Wavetable> table (UiHost& host, const ContentFileRef& ref);
    void clear();

private:
    std::map<std::string, std::shared_ptr<const SamplePreview>> samples;
    std::map<std::string, std::shared_ptr<const wtedit::Wavetable>> tables;
};
ContentCache& contentCache (UiContext& ctx);

/** The reference of the oscillator's sample / table in the host content, or null. */
const ContentSample* sampleOf (const StateContent& content, int osc) noexcept;
const ContentWavetable* wavetableOf (const StateContent& content, int osc) noexcept;
} // namespace synthyboy::ui
