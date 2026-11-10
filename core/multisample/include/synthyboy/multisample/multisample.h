// SynthyBoy - core/multisample: the multisample oscillator (Serum 2 manual p.58 - 67, docs/ui-design/13-new-keys.md 4.39): an SFZ parser, the instrument (zones and their
// decoded samples), the choice of the zones for a note and a layer voice with the SFZ / override envelope.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Instrument::build () runs on a non real-time thread (it decodes the samples); selectZones () and LayerVoice are real time safe (no allocation, no locks).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "synthyboy/sampleosc/sample_data.h"
#include "synthyboy/sampleosc/sample_types.h"
#include "synthyboy/sampleosc/sample_voice.h"

namespace synthyboy
{
namespace multisample
{
constexpr int kMaxLayers = 2;            // zones that play for one note (velocity layers that overlap, round robins are one of several)
constexpr int kMaxLayerSlots = 2 * kMaxLayers; // ... and as many zones that play when the key is released (trigger=release)
constexpr int kMaxGroups = 256;          // SFZ group / off_by numbers that are tracked
constexpr int64_t kMaxInstrumentFrames = 24 * 1000 * 1000; // decoded frames of all samples of an instrument (about 100 MB with the mip levels); later zones are left out

/** One SFZ region with the opcodes of its <control>, <global>, <master> and <group> headers applied. The sample is a path relative to the folder of the SFZ file. */
struct Region
{
    std::string sample;
    int lokey = 0, hikey = 127, lovel = 1, hivel = 127, center = 60;
    float tuneCents = 0.0f;
    int transpose = 0;
    float volumeDb = 0.0f;
    float pan = 0.0f;                         // -1 .. 1
    int64_t offset = 0, end = -1;             // frames of the sample (end < 0: to the end)
    int loopMode = -1;                        // -1 not set (the sample's own loop, else none), 0 no_loop, 1 one_shot, 2 loop_continuous, 3 loop_sustain
    int64_t loopStart = -1, loopEnd = -1;
    float loopCrossfadeSec = 0.0f;
    float delay = 0.0f, attack = 0.0f, hold = 0.0f, decay = 0.0f, sustain = 100.0f, release = 0.0f; // ampeg_*: seconds, sustain in percent
    bool hasEnvelope = false;                 // the SFZ gave at least one ampeg_ opcode
    float velTrack = 100.0f;                  // amp_veltrack, percent
    float keyTrackDb = 0.0f;                  // amp_keytrack, dB per key from the key centre
    std::vector<std::pair<int, float>> velCurve; // amp_velcurve_N = gain
    int group = 0, offBy = 0, offMode = 0;    // choke groups; offMode 0 fast, 1 normal
    int seqLength = 1, seqPosition = 1;       // round robin
    float lorand = 0.0f, hirand = 1.0f;       // random layers
    int swLokey = -1, swHikey = -1, swLast = -1; // key switches
    // REMAINDER item 4 (S12, 13-new-keys.md 4.43): trigger=release (the zone plays when the key is released, with the velocity of the note), the crossfades by key and velocity
    // (xfin_* rise from 0 at lo to 1 at hi, xfout_* fall from 1 at lo to 0 at hi; -1: not given) and the filter (fil_type, cutoff, resonance, fil_keytrack, fil_keycenter, fil_veltrack)
    bool releaseTrigger = false;
    int xfInLoKey = -1, xfInHiKey = -1, xfOutLoKey = -1, xfOutHiKey = -1;
    int xfInLoVel = -1, xfInHiVel = -1, xfOutLoVel = -1, xfOutHiVel = -1;
    int filType = 0;                          // 0 none, 1 lpf_1p, 2 lpf_2p, 3 hpf_1p, 4 hpf_2p, 5 bpf_2p, 6 brf_2p
    float filCutoffHz = 0.0f, filResonanceDb = 0.0f, filKeytrackCents = 0.0f, filVeltrackCents = 0.0f;
    int filKeycenter = 60;
};

struct SfzInstrument
{
    std::vector<Region> regions;
    int swDefault = -1;
    std::string defaultLoopMode;              // for the report only
};

/** Parses SFZ text (<control> default_path, <global>, <master>, <group>, <region>; comments, #define variables, note names). Never throws; unknown opcodes are ignored. */
SfzInstrument parseSfz (const std::string& text);
/** A sample path made safe to store below the folder of an instrument: '/' separators, "." and ".." segments removed (a ".." never climbs out), no drive letter or leading '/', the
    extension replaced by `.wav` when `asWav`. */
std::string storedSamplePath (const std::string& path, bool asWav);
/** The canonical SFZ text of an instrument (every region with all the opcodes this package reads; the sample paths as they are in the regions): parseSfz (writeSfz (x)) equals x. */
std::string writeSfz (const SfzInstrument& instrument);
/** "c#4" / "Eb3" / "60" -> a MIDI note (c4 = 60), -1 when it is neither. */
int parseNoteName (const std::string& text) noexcept;

/** Reads the bytes of a sample, the path relative to the folder of the SFZ file. False: missing. */
using SampleReader = std::function<bool (const std::string& relativePath, std::vector<uint8_t>& bytes)>;

class Instrument
{
public:
    struct Zone
    {
        Region region;
        int sample = -1;                      // index into the decoded samples, -1: the file was missing
        float loopStartFraction = 0.0f, loopEndFraction = 1.0f, startFraction = 0.0f, endFraction = 1.0f;
        int loopType = 0;                     // 0 none, 1 one shot, 2 continuous, 3 sustain
    };

    /** Decodes the samples the regions name. Returns the number of zones that have audio (0: nothing playable). */
    int build (const SfzInstrument& sfz, const SampleReader& reader);

    int numZones() const noexcept { return static_cast<int> (zones_.size()); }
    int numPlayable() const noexcept { return playable_; }
    int numMissing() const noexcept { return missing_; }
    int numSkipped() const noexcept { return skipped_; }
    const Zone& zone (int index) const noexcept { return zones_[static_cast<size_t> (index)]; }
    const sampleosc::SampleView* view (int index) const noexcept { return index >= 0 && index < static_cast<int> (views_.size()) ? &views_[static_cast<size_t> (index)] : nullptr; }
    const sampleosc::SampleData* data (int index) const noexcept { return index >= 0 && index < static_cast<int> (samples_.size()) ? samples_[static_cast<size_t> (index)].get() : nullptr; }
    int switchDefault() const noexcept { return switchDefault_; }
    bool hasKeySwitches() const noexcept { return hasKeySwitches_; }
    bool isKeySwitchKey (int key) const noexcept;

    /** The zones that play for `key` (already looked up through the timbre) at `velocity` (1 .. 127): inside the key and velocity range, the random range of `rand`, the
        round robin position of `counter`, the key switch `switchKey`. At most `maxOut` indices into the zones. Real time safe. */
    int selectZones (int key, int velocity, float rand, uint32_t counter, int switchKey, int* out, int maxOut) const noexcept;
    /** The zones with trigger=release that play when the key of that note is released (same rules as selectZones, `velocity` is the one of the note). */
    int selectReleaseZones (int key, int velocity, float rand, uint32_t counter, int switchKey, int* out, int maxOut) const noexcept;
    /** The gain of the key / velocity crossfades (xfin_* / xfout_*) of a zone for a note: 1 for a zone without any. Equal power curves. */
    float crossfadeGain (int zoneIndex, int key, int velocity) const noexcept;

    /** The key to look a zone up with: TIMBRE -1 .. 1 moves it against the pitch (1: the mapping is inverted: key 0 plays the highest zone), -1 stretches it (3 key - 127). */
    static int timbreKey (int key, float timbre) noexcept;

private:
    int selectZonesOf (bool release, int key, int velocity, float rand, uint32_t counter, int switchKey, int* out, int maxOut) const noexcept;
    std::vector<Zone> zones_;
    std::vector<std::unique_ptr<sampleosc::SampleData>> samples_;
    std::vector<sampleosc::SampleView> views_;
    int playable_ = 0, missing_ = 0, skipped_ = 0, switchDefault_ = -1;
    bool hasKeySwitches_ = false;
    int switchLo_ = 127, switchHi_ = 0;
};

/** The envelope of a note (the SFZ's ampeg_* or the OVERRIDE of the oscillator): delay, attack, hold, decay (seconds), sustain 0 .. 1, release (seconds). */
struct EnvelopeSettings
{
    float delay = 0.0f, attack = 0.0f, hold = 0.0f, decay = 0.0f, sustain = 1.0f, release = 0.003f;
};

/** One zone played for one unison voice: the sample voice, the delay / attack / hold / decay / sustain / release envelope, the gains of the zone. */
class LayerVoice
{
public:
    void prepare (double hostSampleRate) noexcept;
    void reset() noexcept;
    /** `velocity` 1 .. 127. `velTrackPercent` < 0: the zone's amp_veltrack. `envelope` null: the zone's ampeg_* (a zone without any plays at once and releases in 3 ms). */
    void noteOn (const Instrument& instrument, int zoneIndex, int key, int velocity, const EnvelopeSettings* envelope, float velTrackPercent, uint32_t startSequence,
                 sampleosc::QualityTier quality) noexcept;
    void seekFraction (double fraction) noexcept { voice_.seekFraction (fraction); }
    /** Note off: a one shot zone plays on, loop_sustain leaves its loop, every zone releases (`fast`: the 10 ms of off_mode=fast). */
    void noteOff (bool fast) noexcept;
    bool isActive() const noexcept { return active_; }
    int zoneIndex() const noexcept { return zone_; }
    int offBy() const noexcept { return offBy_; }
    bool offFast() const noexcept { return offMode_ == 0; }
    int group() const noexcept { return group_; }
    uint32_t startSequence() const noexcept { return startSequence_; }
    /** Renders numFrames and ADDS them to left / right (tmpLeft / tmpRight are the work buffers of the caller, numFrames long). `semitones` = the pitch relative to the recorded pitch
        of the zone (note - key centre + tune + transpose + unison detune); `gain` an extra linear gain (the unison weight). */
    void processAdd (float* left, float* right, float* tmpLeft, float* tmpRight, int numFrames, float semitones, const float* rateMod, float gainLeft, float gainRight) noexcept;

private:
    enum class Stage : uint8_t { delay, attack, hold, decay, sustain, release, done };
    sampleosc::SampleVoice voice_;
    double rate_ = 48000.0;
    bool active_ = false, oneShot_ = false, sustainLoop_ = false, released_ = false;
    Stage stage_ = Stage::done;
    EnvelopeSettings env_;
    float level_ = 0.0f, gain_ = 1.0f, pan_ = 0.0f, panL_ = 1.0f, panR_ = 1.0f;
    double stageSamples_ = 0.0, stageLength_ = 0.0;
    float releaseFrom_ = 0.0f;
    int group_ = 0, offBy_ = 0, offMode_ = 0, zone_ = -1;
    uint32_t startSequence_ = 0;
    float tuneSemitones_ = 0.0f;
    // the filter of the zone (fil_*): a fixed cutoff (key and velocity tracked at the note start), 1 or 2 poles, no envelope
    int filType_ = 0;
    float fb0_ = 1.0f, fb1_ = 0.0f, fb2_ = 0.0f, fa1_ = 0.0f, fa2_ = 0.0f;
    float fzl_[2] = {}, fzr_[2] = {};
    void setupFilter (const Region& r, int key, int velocity) noexcept;
    void enter (Stage stage, double seconds) noexcept;
};
} // namespace multisample
} // namespace synthyboy
