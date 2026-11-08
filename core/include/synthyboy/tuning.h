// SynthyBoy - core/tuning: microtonal tuning (Scala .scl, .kbm, AnaMark .tun) and dynamic tuning interface.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace synthyboy
{
namespace tuning
{
/** Number of standard MIDI keys (0..127). */
constexpr int kNumKeys = 128;

/** Default concert pitch A4 frequency in Hz. */
constexpr double kDefaultA4 = 440.0;

/** Default base frequency for MIDI note 0 (A4=440 Hz): 440 * 2^(-69/12) Hz. */
constexpr double kDefaultMidi0Freq = 8.175798915643707;

/** Maximum allowed file size for tuning documents (1 MB). */
constexpr size_t kMaxTuningFileSize = 1024 * 1024;

/** Maximum number of scale notes supported in a single scale. */
constexpr int kMaxScaleNotes = 1024;

/** Maximum keyboard mapping cycle size. */
constexpr int kMaxMappingSize = 1024;

/** Sentinel value for unmapped MIDI keys in keyboard mapping. */
constexpr int kUnmappedKey = -1;

/** 128-entry table of frequencies in Hz for MIDI keys 0..127. */
using Table = std::array<double, kNumKeys>;

/** Parsing error codes for tuning format parsers. */
enum class ParseError
{
    ok = 0,
    emptyInput,
    tooLarge,
    invalidHeader,
    invalidScaleSize,
    invalidPitch,
    missingPitchEntries,
    invalidMapping,
    invalidTunSection,
    invalidTunEntry,
    ioError
};

/** Result returned by parsers with error details. */
struct ParseResult
{
    ParseError error = ParseError::ok;
    size_t line = 0;
    const char* message = "";

    bool ok() const noexcept { return error == ParseError::ok; }
    explicit operator bool() const noexcept { return ok(); }
};

/** Representation of a single scale pitch interval above 1/1 tonic. */
struct PitchEntry
{
    double cents = 0.0;
    double ratio = 1.0;
    std::string text; // original representation (e.g. "3/2" or "100.0")
};

/** Scala keyboard mapping (.kbm) configuration. */
struct KeyboardMapping
{
    int mapSize = 0;                   // 0 = linear mapping (key - middleNote)
    int firstMidi = 0;                 // lowest MIDI key to tune (0..127)
    int lastMidi = 127;                // highest MIDI key to tune (0..127)
    int middleNote = 60;               // MIDI key mapped to degree 0 (usually 60)
    int referenceNote = 69;            // reference MIDI key (usually 69)
    double referenceFrequency = 440.0; // frequency in Hz at referenceNote
    int scaleDegreeForReference = 9;   // scale degree for referenceNote (9 for A4 in 12-TET)
    std::vector<int> keys;             // scale degree for each key in cycle; kUnmappedKey if unmapped

    static KeyboardMapping defaultMapping (int scaleSize = 12) noexcept;
    bool isValid() const noexcept;
};

/** Interface for an external dynamic tuning source (MTS-ESP style seam).
    Plain abstract class providing a 128-key query seam without linking MTS-ESP. */
class ITuningSource
{
public:
    virtual ~ITuningSource() = default;

    /** Returns true if this external dynamic tuning source is currently active/connected. */
    virtual bool isConnected() const noexcept = 0;

    /** Returns the frequency in Hz for the specified MIDI key (0..127).
        Returns a fallback frequency or <= 0.0 if not connected or unmapped. */
    virtual double frequencyForKey (int midiKey) const noexcept = 0;

    /** Queries all 128 MIDI key frequencies (0..127) at once into outFrequencies.
        Returns true if the tuning source is active and populated outFrequencies;
        returns false if disconnected. */
    virtual bool query128Keys (std::array<double, kNumKeys>& outFrequencies) const noexcept = 0;

    /** Optional name of the active tuning source / MTS master. */
    virtual const char* name() const noexcept { return ""; }
};

/** Microtonal tuning representation and 128-key lock-free frequency lookup table.
    Default is 12-TET at A4 = 440.0 Hz. */
class Tuning
{
public:
    Tuning() noexcept;

    /** Returns the 128-entry frequency table precomputed at A4 = 440.0 Hz.
        Usable lock-free on the audio thread: O(1) array access, no allocations. */
    const Table& table() const noexcept { return table_; }

    /** Returns the frequency in Hz for midiKey (0..127) at concert pitch a4Hz.
        Usable lock-free on the audio thread: O(1), no allocations, never throws.
        Returns 0.0 if midiKey is outside 0..127 or unmapped. */
    double frequencyForKey (int midiKey, double a4Hz = kDefaultA4) const noexcept;

    /** Computes and returns a 128-entry table scaled for a given concert pitch a4Hz. */
    Table computeTable (double a4Hz) const noexcept;

    /** Resets the tuning to default 12-TET at A4 = 440.0 Hz. */
    void setDefault() noexcept;

    /** Whether this tuning is standard 12-TET at A4 = 440.0 Hz. */
    bool isDefault() const noexcept { return isDefault_; }

    const std::string& name() const noexcept { return name_; }
    void setName (const std::string& n) { name_ = n; }

    const std::string& description() const noexcept { return description_; }
    void setDescription (const std::string& d) { description_ = d; }

    int scaleSize() const noexcept { return static_cast<int> (scale_.size()); }
    const std::vector<PitchEntry>& pitches() const noexcept { return scale_; }

    const KeyboardMapping& mapping() const noexcept { return mapping_; }
    void setMapping (const KeyboardMapping& m) noexcept;

    /** Sets raw scale pitches (degrees 1..N) and recalculates table. */
    bool setScale (const std::vector<PitchEntry>& pitches) noexcept;

    /** Loads a Scala .scl scale from buffer. If parsing fails, existing tuning is unchanged. */
    ParseResult loadScl (const char* text, size_t size) noexcept;
    ParseResult loadScl (const std::string& text) noexcept { return loadScl (text.data(), text.size()); }

    /** Loads a Scala .kbm keyboard mapping from buffer. If parsing fails, existing mapping is unchanged. */
    ParseResult loadKbm (const char* text, size_t size) noexcept;
    ParseResult loadKbm (const std::string& text) noexcept { return loadKbm (text.data(), text.size()); }

    /** Loads an AnaMark .tun tuning file from buffer. If parsing fails, existing tuning is unchanged. */
    ParseResult loadTun (const char* text, size_t size) noexcept;
    ParseResult loadTun (const std::string& text) noexcept { return loadTun (text.data(), text.size()); }

    /** Serializes the current scale to Scala .scl text format. */
    std::string toScl() const;

    /** Serializes the current keyboard mapping to Scala .kbm text format. */
    std::string toKbm() const;

    /** Serializes the current tuning to AnaMark .tun text format. */
    std::string toTun() const;

    /** Recalculates the internal 128-key table from current scale and mapping. */
    void recalculateTable() noexcept;

    /** Direct helper for raw 128-entry frequencies (used by .tun parser). */
    void setDirectFrequencies (const Table& freqs, const std::string& name) noexcept;

private:
    Table table_ {};
    std::vector<PitchEntry> scale_;
    KeyboardMapping mapping_ {};
    std::string name_;
    std::string description_;
    bool isDefault_ = true;
    bool isRawTun_ = false;
};

/** Free parser functions. */
ParseResult parseScl (const char* text, size_t size, Tuning& out) noexcept;
ParseResult parseKbm (const char* text, size_t size, Tuning& out) noexcept;
ParseResult parseTun (const char* text, size_t size, Tuning& out) noexcept;

inline ParseResult parseScl (const std::string& text, Tuning& out) noexcept
{
    return parseScl (text.data(), text.size(), out);
}

inline ParseResult parseKbm (const std::string& text, Tuning& out) noexcept
{
    return parseKbm (text.data(), text.size(), out);
}

inline ParseResult parseTun (const std::string& text, Tuning& out) noexcept
{
    return parseTun (text.data(), text.size(), out);
}

} // namespace tuning
} // namespace synthyboy
