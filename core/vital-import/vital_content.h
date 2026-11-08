// SynthyBoy - core: parsing of the embedded content of a .vital file (wavetable objects, LFO lines), internal header.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "synthyboy/wavetable_render.h"
#include "vital_import_internal.h"

namespace synthyboy
{
struct ImportedSample;

namespace vitalimport
{
/** What went wrong while parsing a piece of content; turned into report items by the caller. */
struct ContentNotes
{
    std::vector<std::string> unsupportedTypes; // component "type" strings that are not one of the ten
    int badFields = 0;                         // a field of the wrong type / invalid base64 that was dropped
    bool limitExceeded = false;                // groups / components / keyframes / points / base64 size beyond the limits
    size_t decodedBytes = 0;                   // running total of decoded base64 (per file limit 48 MiB)
};

constexpr size_t kMaxBase64Decoded = 16u << 20;
constexpr size_t kMaxBase64PerFile = 48u << 20;

bool base64Decode (const std::string& in, size_t maxBytes, std::vector<uint8_t>& out);

struct ParsedWavetable
{
    wtrender::Description desc;
    std::string name, author;
};

struct ParsedLfo
{
    wtrender::Line line; // y is the VALUE (up = high): the file stores 1 - value (SPEC-GAP: orientation of the line object)
    std::string name;
};

/** `node` is the wavetable object (or a bare LFO line object, which becomes a one keyframe Line Source). False when it is neither. */
bool parseWavetable (const json::Doc& doc, int node, ParsedWavetable& out, ContentNotes& notes);
/** The LFO line object, sanitised as in design 1.3.2. False when invalid (fewer than 2 usable points). */
bool parseLfoLine (const json::Doc& doc, int node, ParsedLfo& out, ContentNotes& notes);
/** Decodes embedded Vital sample object (base64 or float array) and creates 16-bit PCM WAV bytes in out. */
bool parseVitalSample (const json::Doc& doc, int node, ImportedSample& out, ContentNotes& notes);
/** Creates standard 16-bit PCM RIFF/WAVE file bytes in memory. */
std::vector<uint8_t> createWav16 (const int16_t* pcmLeft, const int16_t* pcmRight, size_t numFrames, int sampleRate, int channels);
} // namespace vitalimport
} // namespace synthyboy
