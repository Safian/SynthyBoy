// SynthyBoy - core: internal declarations of the Vital preset importer (JSON reader, table row, reference shapes).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace synthyboy
{
/** Last valid index of a choice list before the state-v3 appends (13-new-keys.md section 3), or -1 when the key has none. */
int legacyChoiceMax (const char* key);

namespace vitalimport
{
/** Conversion of the row: OUR value -> the value in the file (the importer applies the inverse). */
enum class Conv : uint8_t
{
    linear,       // plain * a + b
    sqrtScaled,   // sqrt (plain * a)
    quarticMs,    // (plain / 1000) ^ (1/4)
    log2Ms,       // max (a, log2 (plain / 1000))
    log2Hz,       // log2 (plain)
    log2PeriodMs, // log2 (1000 / plain)
    noteFromHz,   // 12 * log2 (plain / 8.1757989)
    index,        // plain + b
    indexMap,     // warp type map {0..8, 10, 11}
    volumeDb      // (dB + 80)^2
};

struct VitalRow
{
    const char* key;     // our parameter
    const char* control; // name in the .vital settings
    Conv conv;
    double a, b;
};

/** The table (vital_map.cpp). */
const VitalRow* vitalRows (size_t& count) noexcept;

// ---------------------------------------------------------------------------------------------------------------------
// bounded, iterative JSON reader (json_reader.cpp)
// ---------------------------------------------------------------------------------------------------------------------
namespace json
{
enum class Type : uint8_t { null, boolean, number, string, array, object };

struct Node
{
    Type type = Type::null;
    bool boolean = false;
    double number = 0.0;
    int32_t str = -1;   // string value (index into Doc::strings)
    int32_t key = -1;   // key of this member inside an object
    int32_t first = -1; // first child
    int32_t last = -1;  // last child
    int32_t next = -1;  // next sibling
    uint32_t count = 0; // children
};

enum class Status : uint8_t { ok, malformed, tooDeep, budget };

struct Doc
{
    std::vector<Node> nodes;
    std::vector<std::string> strings;
    size_t errorOffset = 0;

    const Node& root() const { return nodes[0]; }
    const std::string& text (int32_t index) const { return strings[static_cast<size_t> (index)]; }
    /** Member `key` of the object node (the last one if duplicated), -1 if absent or not an object. */
    int find (int object, const char* key) const noexcept;
};

/** Parses a complete document. Skips a UTF-8 BOM and surrounding white space. `maxNodeBytes` bounds the working memory of
    the tree (nodes + strings). */
Status parse (const char* data, size_t size, int maxDepth, size_t maxWorkingBytes, Doc& out) noexcept;
} // namespace json

// ---------------------------------------------------------------------------------------------------------------------
// reference shapes of the built-in wavetables and LFO shapes (builtin_ref.cpp)
// ---------------------------------------------------------------------------------------------------------------------
constexpr int kBuiltinWavetables = 13;
constexpr int kBuiltinLfoShapes = 12;
constexpr int kLfoRefSamples = 256;

/** One frame (2048 samples, peak 1) of built-in wavetable `table` at relative position t in 0..1. */
void builtinWavetableFrame (int table, double t, float* out2048);
/** Number of frames the built-in table has (1 for the single cycle tables 0..4). */
int builtinWavetableFrames (int table) noexcept;
/** The LFO shape (values 0..1) as kLfoRefSamples samples. */
void builtinLfoShape (int shape, float* out) noexcept;

/** Nearest built-in table by the spectral distance of the design (1.3.1): positions 0, .25, .5, .75, 1; the first 64 harmonics in dB
    (floor -90), mean absolute difference. `frames` is numFrames * 2048. Returns the index, distance in `distance`. */
int nearestBuiltinWavetable (const float* frames, int numFrames, double& distance);
/** 0..4 (Sine, Saw, Square, Triangle, Pulse 25%) if the table is a single cycle (or all frames equal) that matches within a
    normalised RMS error of 0.01, else -1. */
int matchBuiltinSingleCycle (const float* frames, int numFrames);
/** The built-in LFO shape within an RMS error of 0.01, else -1; nearest and its error always returned. */
int matchBuiltinLfoShape (const float* samples256, int& nearest, double& error);

// ---------------------------------------------------------------------------------------------------------------------
// text helpers (vital_import.cpp)
// ---------------------------------------------------------------------------------------------------------------------
/** UTF-8 repaired (invalid -> U+FFFD), control characters removed (newline -> space if `newlineToSpace`), cut at a code point boundary
    to maxBytes. `cut` is set when the text was shortened. */
std::string cleanText (const std::string& in, size_t maxBytes, bool newlineToSpace, bool* cut = nullptr);
} // namespace vitalimport
} // namespace synthyboy
