// SynthyBoy - core: the user content library (WP-01, docs/ui-design/13-new-keys.md 4.25).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// JUCE free. The shells (JUCE and native CLAP) own one UserContentStore on `<userDir>/content/` and hand it to the engine with
// IEngine::setContentStore (): the references of the state (`content.wavetables / samples / impulses`, pack + file + sha256) resolve to the files
// the user imported or saved. Writing is a message thread operation, reading is what the engine does when a content section is set.
//
//   <root>/wavetables/<sha12>/<name>.wav     pack "user.wavetables"   16-bit mono, a multiple of 2048 samples = that many frames
//   <root>/samples/<sha12>/<name>.wav        pack "user.samples"      24-bit PCM or the original WAV (the file name keeps the root note: "Piano Eb3.wav")
//   <root>/impulses/<sha12>/<name>.wav       pack "user.impulses"
//   <root>/embedded/<sha12>/<name>.wav       pack "user.embedded"     content that "Embed in Preset" saved (not listed in the library)
//   <root>/<pack>/...                        any other pack directory (user.imported-vital of the Vital importer, installed packs)
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/state.h"
#include "synthyboy/wavetable_edit.h"

namespace synthyboy
{
enum class ContentKind : uint8_t { wavetable, sample, impulse };

constexpr const char* kPackWavetables = "user.wavetables";
constexpr const char* kPackSamples = "user.samples";
constexpr const char* kPackImpulses = "user.impulses";
constexpr const char* kPackInstruments = "user.instruments"; // GROUPC WP-13: <root>/instruments/<sha12>/<name>.sfz and the samples next to it
constexpr const char* kPackEmbedded = "user.embedded";
constexpr const char* kPackImportedVital = "user.imported-vital";

/** A file of the library as the browser lists it. */
struct UserContentItem
{
    ContentKind kind = ContentKind::wavetable;
    ContentFileRef ref; // pack, file, sha256 (verified at listing), name (the stem)
    uint64_t size = 0;
    bool embedded = false;
};

/** Why a reference of a state does not resolve. */
enum class MissingReason : uint8_t { notFound, hashMismatch };

struct MissingContent
{
    ContentKind kind = ContentKind::wavetable;
    int osc = 0; // 1..3 for a wavetable / sample, 0 for the impulse response
    ContentFileRef ref;
    MissingReason reason = MissingReason::notFound;
};

/** File name made safe for a directory entry ("My Table: v2" -> "My Table_ v2"); keeps letters, digits, space, - _ # ( ) . and non ASCII bytes,
    never empty, at most 80 bytes. */
std::string sanitizeFileStem (const std::string& name);
/** "dir/Piano Eb3.flac" -> "Piano Eb3". */
std::string fileStemOf (const std::string& path);

class UserContentStore final : public IContentStore
{
public:
    /** `userDir` is the user data directory (userDataDirectory ()); the store root is `<userDir>/content`. Nothing is created until a write. */
    explicit UserContentStore (const std::string& userDir);

    const std::string& rootDirectory() const noexcept { return root_; }
    /** A read only directory of installed packs: `<dir>/<pack>/<file>`, searched after the user directory. */
    void addPackRoot (const std::string& dir);

    /** The user packs map to their directories, every other pack is a directory below the root (or below an extra root). The SHA-256 of the
        reference is verified when it has one. */
    bool readFile (const ContentFileRef& ref, std::vector<uint8_t>& bytes) const override;
    bool contains (const ContentFileRef& ref) const;

    /** Writes `bytes` as `<pack dir>/<sha12>/<stem of fileName>.wav` (content addressed: the same bytes are stored once) and fills `ref` with the
        pack, file, SHA-256 and name. `embedded` stores in the embedded pack. False when the directory cannot be written or the bytes are empty
        / above kMaxContentFileBytes. */
    bool addFile (ContentKind kind, const std::string& fileName, const std::vector<uint8_t>& bytes, ContentFileRef& ref, bool embedded = false);

    /** GROUPC WP-13: stores a multisample instrument: the SFZ text as `<instruments>/<sha12>/<stem>.sfz` and every sample below the same folder under the (relative, safe) path it
        has in the SFZ. `ref` becomes the SFZ file (pack user.instruments, file `<sha12>/<stem>.sfz`). Content addressed like addFile. */
    bool addInstrument (const std::string& name, const std::string& sfzText, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& samples, ContentFileRef& ref);

    /** The files of one kind: the user pack and (wavetables, samples) the `user.imported-vital` pack, ascending by name. */
    std::vector<UserContentItem> list (ContentKind kind) const;
    /** Deletes the file of a user pack (never an installed pack); true if it is gone afterwards. */
    bool remove (const ContentFileRef& ref);

    static const char* packFor (ContentKind kind, bool embedded = false) noexcept;
    /** The directory name below the root of a user pack ("user.wavetables" -> "wavetables"), "" for any other pack. */
    static const char* directoryOfUserPack (const std::string& pack) noexcept;

private:
    std::string root_;
    std::vector<std::string> extraRoots_;
};

// ---- the state side: attach / detach a stored file -------------------------------------------------------------------------------------

/** Sets content.wavetables[osc] (replacing the entry of the oscillator). `frames` is the frame count of the file. */
void attachWavetable (StateContent& content, int osc, const ContentFileRef& ref, const std::string& author, int frames);
void attachSample (StateContent& content, int osc, const ContentFileRef& ref, int sampleRate, int frames, int channels, std::vector<float> slices = {});
void attachImpulse (StateContent& content, const ContentFileRef& ref);
void detachWavetable (StateContent& content, int osc);
void detachSample (StateContent& content, int osc);
void detachImpulse (StateContent& content);
void attachInstrument (StateContent& content, int osc, const ContentFileRef& ref, int zones);
void detachInstrument (StateContent& content, int osc);

/** Reads the stored sample/table behind `ref`, fills the content entry of the oscillator (frames, rate, channels) and, for a sample, sets
    `osc<N>.sample.root` in `values` from the file name / WAV chunk (when it names a note; `values` may be null). False when the store cannot read it
    or the file is not decodable. */
bool attachStoredSample (StateContent& content, ParamValues* values, int osc, const ContentFileRef& ref, const IContentStore& store);
bool attachStoredWavetable (StateContent& content, int osc, const ContentFileRef& ref, const IContentStore& store, const std::string& author = {});

/** Every reference of the content the store cannot resolve (not found, or found with another SHA-256). */
std::vector<MissingContent> findMissingContent (const StateContent& content, const IContentStore& store);

/** The answer to one entry of the Missing Files dialog: `bytes` is the file the user located. The file is added to the library and the
    reference of `content` is rewritten to it. Returns false (nothing changed) when the bytes are empty, or when the reference has a SHA-256 and
    the file has another one and `acceptDifferent` is false. */
bool relinkMissing (StateContent& content, const MissingContent& item, const std::vector<uint8_t>& bytes, const std::string& fileName,
                    UserContentStore& store, bool acceptDifferent);
/** The answer "ignore" for the session: removes the entry from the content (the engine falls back as documented: saw / silence / bypass). */
void dropMissing (StateContent& content, const MissingContent& item);

// ---- audio files ---------------------------------------------------------------------------------------------------------------------

/** Complete RIFF/WAVE bytes. `right` null = mono. bits: 16, 24 (PCM) or 32 (IEEE float). Empty on bad arguments. */
std::vector<uint8_t> encodeWav (const float* left, const float* right, size_t frames, int sampleRate, int bits);

struct SampleFacts
{
    int sampleRate = 0, channels = 0;
    int64_t frames = 0;
    int rootNote = -1;        // from the file name (`Name F2`, `Eb3`), else the WAV instrument / sampler chunk; -1 = none
    bool rootFromName = false;
};
/** Reads the header and the root note of a WAV file. False when the bytes are not a WAV file the engine can decode. */
bool probeSampleWav (const std::vector<uint8_t>& wav, const std::string& fileName, SampleFacts& facts);

/** A WAV table as the editor holds it: a length that is a multiple of 2048 is that many frames (at most 256), anything else one resampled cycle. */
bool wavetableFromWav (const std::vector<uint8_t>& wav, const std::string& name, wtedit::Wavetable& out);
/** The 16-bit WAV of a table (the format of the user pack). Empty when the table has no frames. */
std::vector<uint8_t> wavetableToWav (const wtedit::Wavetable& table);

/** The part [startNorm, endNorm] (fractions of the file) of a WAV sample as a new 24-bit WAV; the sample rate and channels are kept. Empty on
    failure or when the region is shorter than 16 frames. */
std::vector<uint8_t> trimSampleWav (const std::vector<uint8_t>& wav, float startNorm, float endNorm);

/** The decoded sample for the display and the snap / slice tools of the editor (the same data the engine plays). */
class SamplePreview
{
public:
    static std::shared_ptr<const SamplePreview> create (const std::vector<uint8_t>& wav, const std::string& fileName);

    int sampleRate() const noexcept { return sampleRate_; }
    int channels() const noexcept { return channels_; }
    int64_t frames() const noexcept { return static_cast<int64_t> (mono_.size()); }
    double seconds() const noexcept { return sampleRate_ > 0 ? static_cast<double> (mono_.size()) / sampleRate_ : 0.0; }

    /** Min / max of the mono mix in `columns` equal parts of [startNorm, endNorm] (the display of the zoomed view). */
    void peaks (float startNorm, float endNorm, int columns, std::vector<float>& minOut, std::vector<float>& maxOut) const;
    /** The slice starts the engine's Auto mode would use for the threshold 0..1 (SampleData::detectSlices rule: the first is 0, then the onsets
        with strength >= 1 - threshold). */
    std::vector<float> autoSlices (float threshold) const;
    /** All onsets (position, strength) the Auto mode chooses from. */
    const std::vector<std::pair<float, float>>& onsets() const noexcept { return onsets_; }
    /** The nearest positive going zero crossing of the mono mix at or after `norm` within `windowFrames` either side (the position itself when none). */
    float snapToZero (float norm, int windowFrames = 2048) const;
    /** The sample value (mono mix) at a fraction. */
    float valueAt (float norm) const;
    /** The spectrogram of [startNorm, endNorm] for the display of the spectral oscillator: `columns` x `rows` values 0..1 (column major: out[column * rows + row], row 0 = 20 Hz, the last
        row = 20 kHz, log axis; level in dB over a 60 dB range, normalised to the loudest cell). */
    void spectrogram (float startNorm, float endNorm, int columns, int rows, std::vector<float>& out) const;

private:
    int sampleRate_ = 0, channels_ = 0;
    std::vector<float> mono_;
    std::vector<std::pair<float, float>> onsets_;
};

/** The marker snap modes of the sample view (design 02, S p.72-73). */
enum class SnapMode : uint8_t { off, zero, beats, loop };
/** `norm` snapped for the mode: zero = the nearest positive going zero crossing of the sample, beats = the nearest multiple of `beatGrid` (the fraction
    of the file one beat lasts: region length / length_beats), loop = the nearer of the two loop markers when within `loopTolerance`. */
float snapMarker (SnapMode mode, float norm, const SamplePreview* sample, float beatGrid, float loopStart, float loopEnd, float loopTolerance = 0.01f);

// ---- Vital files ---------------------------------------------------------------------------------------------------------------------

struct LfoShapeFile
{
    std::string name;
    bool smooth = false;
    std::vector<ContentPoint> points;
};
/** `{"format":"synthyboy-lfo-shape","version":1,"name":..,"smooth":..,"points":[{"x","y","power"}]}` (the `.lfoshape.json` of tools/vital-import). */
std::string serializeLfoShapeFile (const LfoShapeFile& shape);
/** Parses the file above (also accepts a point without "power"); false on anything else. Bounded: at most kMaxLfoShapePoints points. */
bool parseLfoShapeFile (const std::string& text, LfoShapeFile& out);
} // namespace synthyboy
