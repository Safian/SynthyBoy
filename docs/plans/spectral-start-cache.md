# Spectral start cache (2026-11-21)

Owner idea: when the same note is struck again, do not recompute what is identical. Whole-effect caching is out (bus effects mix all voices, tails, free-running LFOs, offline == live bit identity). The Spectral oscillator's start work (the first FFT frames and shapings of each voice, about 89 us per frame and 67 us per shaping on the A17, `start_ledger.h`) is a pure function of a small input set, so it can be memoised. This is the analysis (part 1) and the decision.

Clean room: written from our own source (`core/spectral/src/spectral.cpp`, `core/engine-own/osc/lib_osc.cpp`), no third-party code.

## What the start path does

`SpectralVoice::noteOn` clears the smear memory, the ring and the phases of the channels it uses, computes `startFrame_ = head_ * fileFrames / kHop` and calls `startFrames (p, strikeMask, smears)`. The frames 1..3 of the priming that are not in the strike are made later by `process` / `endFirstHop` with the settings `p` has at that time (`startJoin_`). `startFrames` per channel: `shapeFrame` (one shaping for all frames of the call, unless Smear), then per frame j `startPhases` (analysis phase, phase lock, advance j times), `spectrumFromAngles`, inverse real FFT, `overlapAdd` into `c.ring` from sample `(3 - j) * kHop` on. State written: `ring`, `synthPhase` (of the highest frame made), `phaseFrame_`, `phaseInit_`, and `smear` (Smear warp only).

## Exact inputs of one start frame (channel ch, frame j)

All of these decide the bits; nothing else does (no random draw is read inside the voice: the draws are made by `LibOsc::beginType` before `noteOn`, see below).

| Input | Where it comes from |
|---|---|
| analysis content | `Analysis` (magnitudes, phases, frequencies, file rate, file frames). Identity needs a unique id per `build()` (a pointer can be reused after a reload). New: `Analysis::id()` |
| `startFrame_` (double) | `head_`: region lo/hi, loop mode / loop lo / hi (reverse), scan direction (sign of `scanSpeed`), manual + position, and `startFraction` (the note's start draw), `fileFrames` |
| `hostRate_` | sample rate |
| `pitchRatio` | `ratioOf (uni[u])` = semitones of the note + unison detune + pitch bend, glide, vibrato at the note-on (exact float) |
| band | `freqLoHz`, `freqHiHz`, `bandSmooth`, `bandPostWarp` |
| spectral filter | `filterGain` contents (kBins floats; pointer is not an identity: the buffer is rebuilt in place) |
| frequency shift | `freqShiftHz` |
| warps | `warpType[2]`, `warpAmount[2]`, `warpVar[2]` (after modulation) |
| external warps (mask / vocode) | `external[]` = the live magnitude spectrum of another oscillator or the noise: changes every block |
| `phaseLock` | flag |
| channel and frame | `ch` (and the analysis channel count), `j` |
| code path | NEON on / off, the find-peaks variant (`kernels::findPeaksVector()`), reference kernels, legacy start OLA: not cached when reference / legacy |

Not inputs: `gain`, `scanSpeed` (only moves the head after the start), `transients` (the start never reads it), the start offset / quarter, the unison index, the note serial, the mask of the call, the channel's earlier state (noteOn zeroes it).

Consequences that make a cache possible:

1. **Mask independence (no Smear).** Frame j's contribution to the ring (`(zr*w)*scale` per sample, explicit mul, mul, add, no FMA, `-ffp-contract=off`) and the phases after frame j do not depend on which frames were made before it or on the offset: `startPhases` either resumes from `synthPhase` of an earlier frame or starts from the analysis phase, and both run the identical operation sequence. `shapeFrame` of the non-Smear start depends on the inputs above only. So the cache is keyed per (inputs, ch, j), and the start offset (the strike quarter, which changes with the note serial: the same note struck again falls on another quarter) does not matter.
2. **Order of the ring sums is kept.** A hit adds the cached contribution to the ring at the same moment and in the same order as the computation would have, so the float sums are the same bits. The cache stores the contribution (the value that is added), not the ring.
3. **Smear is not cached.** Each shaping changes the smear memory, so the frames depend on the order and on the phantom shaping, i.e. on the offset; the key would have to hold the call history for a rare case. Smear voices take today's path. External warps (live input) are not cached either.

## Random per strike

The only random input is `startFraction`: `LibOsc::beginType` draws `rng.uni()` per unison voice and `randomPerNote` and uses them when `sample.uni_start` > 0 or `random_phase` > 0. Then `startFrame_` is a fresh double every strike and the key never repeats: the cache is correct (a different key is a miss) but useless for that preset, and it pays the fill cost. The draw is a continuous float, so no quantised key is allowed (not bit-identical). Presets with unison start / random phase 0 (the default) repeat. Modulated warp amounts, filters, pitch (velocity, free-running LFO, vibrato, glide) behave the same way: a repeat with the same value hits, a changed value misses. A different velocity misses only when it reaches the inputs above (a warp, filter or pitch routing); `gain` is not an input.

## Decision: bit-identical memoisation is possible, with these limits

The cached frames are the same bits as the computed ones (the test: a repeated 4x2 chord renders bit-identical cache on vs off, `SPECSTART-CHECKSUM` unchanged). Key equality is exact: every scalar input is compared as bits (no struct memcmp), the filter contents by a 128 bit content hash (two independent 64 bit mixes; a collision has to happen in both). Everything is switchable: off = today's path, no cache memory touched.

The ledger does **not** look at the cache: it books the full cost whether the frames hit or not. The ledger decides when a voice starts, and the start time changes the sound; if it depended on the cache state, offline and live renders (different cache contents) would differ. So the cache removes work (headroom inside a block) and never changes timing. A later, separate question: a cheaper booking for guaranteed hits would have to be deterministic (e.g. a per-note flag in the preset), not a peek into the cache.

## Where it lives, size, eviction

- One process-wide table (`core/spectral/include/synthyboy/spectral/start_cache.h`, function-local static), shared by all engine instances. The storage is allocated once from the non real-time side (`StartCache::prepare()` when the quality switch turns it on); the audio thread never allocates. Not prepared: every lookup misses and nothing is stored.
- Entry = (key words, 1025 phases, up to 2048 contribution floats) = one (inputs, ch, j) = about 12.5 KB (the contribution of frame j is its last (j + 1) * 512 samples, 1024 .. 2048 floats). 256 entries in 64 sets of 4 ways = **3.2 MB**, fixed. One stereo voice start = 6 entries = 75 KB; one 4x2 chord (8 voices) = 600 KB, so about 5 such chords fit before eviction; mono samples need half.
- Eviction: per set, an empty way first, then the least recently used (a relaxed stamp refreshed by a hit). No allocation, no list.
- Lock-free: a per-entry sequence counter. Reader: read counter (even), compare the key words, copy the entry into the scratch (`olaTmp`, `phaseTmp`), re-read the counter; changed or odd = miss (the computation is the fallback and is identical). Writer: compare-and-swap the counter even to odd, write, counter + 2 with release; a lost race skips the store. Nobody ever waits. (The copy while a writer is active is the usual seqlock data race; the re-check discards it.)
- Fill: on the audio thread at the moment of a miss, since the frame has just been computed (the existing builder thread has no analysis state and the Key 61 has no free core). Cost of a miss: the contribution goes through the zeroed `olaTmp` (as the `legacyStartOla` path) and one add pass instead of straight into the ring, plus a copy of up to 12 KB into the entry; estimate 3-6 us on a 89 us frame (to be measured; Mac numbers are indicative only, see measurement.md). A hit costs a 12 KB copy and an add pass instead of the frame (about 89 us plus its share of the 67 us shaping on the A17).

## Expected hit rate (the same note, the same preset)

- Static preset (no random start, no modulated warp/filter/pitch at the note-on), the note or chord struck again while its entries are alive: all frames hit, so a repeated 4x2 chord drops from 8 voices * 3 frames * 2 channels = 48 frames to 0 (the first strike makes them; unison voices with equal detune share entries even inside one strike).
- Chord of N notes repeated: hits after the first strike as long as the working set (N * unison * about 75 KB, stereo) stays under about 3 MB: about 40 voice starts. An arpeggio over many pitches or a 16 note chord of unison 4 exceeds it: LRU thrash, the hit rate falls toward zero and the fill cost remains (about 5 %); measure before enabling by default.
- Random start / random phase / modulated warp or filter at the strike / glide / vibrato: no hit.
- Live playing of different notes: the first time of each pitch misses; the benefit is for repeated notes (sequences, drums, repeated chords).

## Open

- Default on or off: decide from a device A/B/A (`tier_bench --start-cache`, interleaved with the cache off, `measurement.md`); decided by the owner 2026-11-21: `Quality::spectralStartCache` defaults to -1 = by tier, on at low (`spectralStartCacheOf`), off at medium / high.
- Whether to extend the cache to the regular frames after the start (no: the pitch, position and phases move every frame).
