# WP4 to WP8 report - content store, sample oscillator, mixer / racks, filters, new FX, reverb force (2026-11-08)

Spec: `13-new-keys.md` section 11. Engine: `core/engine-vital` (our AGPL fork, every upstream change is listed in `docs/UPSTREAM.md`). A capability bit (`engine_caps.h`) is set only with a test that renders the key off and on and asserts a measurable difference. engine-own (iOS) sets none of the bits of WP4 to WP8.

## WP4 - content store, sample oscillator, user wavetable, warp slot 2

Bits set: `kCapSampleOsc`, `kCapUserWavetable`, `kCapWarp2`. Tests: `core/engine-vital/tests/test_wp4.cpp` (8 cases), `process_does_not_allocate_with_the_new_events_content_and_keys` (now also runs the sample oscillator, the user tables and content replacement under the counting `operator new`).

### Done

* **ContentStore** (`core/include/synthyboy/content_store.h`, `core/src/content_store.cpp`): `IContentStore::readFile (ContentFileRef, bytes)` (checks the SHA-256 when the reference has one), `MemoryContentStore`, `DirectoryContentStore` (`<root>/<pack>/<file>`). `IEngine::setContentStore ()` (message thread). `setContent ()` / `setContentStore ()` decode the referenced files on the message thread into the content snapshot, which the audio thread takes over without a lock (existing WP2 mechanism). The replaced snapshot is retired (may be freed) only after `applyParams ()` has repointed the wavetables and sample oscillators at the new one.
* **User wavetable (`oscN.wavetable` = Preset table, C1)**: a WAV whose length is a multiple of 2048 is that many frames (at most 256), anything else is one cycle resampled to 2048. Built into a Vital `Wavetable` (`vitaltables::buildUserWavetable`) and pointed at with `useExternalData` between blocks (no allocation). `oscN.frame` / `frame_spread` scale with the frame count of the table (`Binding::frameOsc`). Missing content: the saw, as before.
* **Sample oscillator (`oscN.type` = Sample)**: `sample_oscillator.h/.cpp` in the fork, rendering with `core/sample-osc` (`SampleVoice`), inside the voice (same note, transpose, tune, unison, level, pan, phase inputs and the same raw / levelled outputs as the wavetable oscillator, so filters, destinations and the mixer are unchanged). `core/sample-osc` was extended (all backwards compatible, its own tests pass): `LoopMode::tailed`, `setSemitoneOffset`, `setSpeedMultiplier` (signed), `releaseLoop`, `processModulated`, `seekFraction`, `edgeFadeMs`, `SampleData::normalizeGain`, `detectOnsets` / `detectSlices`.
  * start / end, root, key tracking, the five loop modes, loop start / end, loop crossfade (ms), reverse, fade edges (1 .. 128 ms, linear at both ends of the region), normalize (peak of the whole file), unison start / span (random / evenly spread start offsets), `phase` and `random_phase` as the start position.
  * **Slices**: Auto = deterministic onset detection of the file (2 ms RMS envelope, rise against the mean of the previous 20 ms, minimum distance 20 ms, no onset within 20 ms of the file start) filtered by `slice_threshold`; Manual = `content.samples[].slices`. The key picks the slice (`key - slice_root`, clamped), the pitch is the recorded pitch (+ transpose + tune), `slice_play` = to the end of the region or the single slice.
  * **Scan** (A13): speed = `scan` x range (x1, x2, x4, x8), sign flipped by `scan_reverse`, x host tempo / 120 with `scan_lock_tempo`, `scan_to_bpm` makes the region last `length_beats` at the host tempo. The speed multiplies the pitch ratio: it is varispeed (pitch follows speed). Pitch-preserving time stretching would be the granular oscillator (Q6: stays GAP). `scan = 0` freezes the read head.
  * `loop_exit` and Tailed: on key release the loop is left and the sample plays on to the end of the region.
  * Unison: voices 1 .. 16 as for the wavetable oscillator (detune range x detune, detune power, stereo spread, blend, equal-power normalisation); the quality tier caps the voices.
* **Warp on a sample (decision, section 4.9)**: a sample has no cycle, so the phase-domain warps (Sync, Formant, Bend, Squeeze, Pulse Width) have nothing to bend and are inert on a sample voice. **Quantize** reduces the resolution of the output (2^(1 + 14 (1 - amount)^3) levels), **FM** (osc A, osc B) modulates the read speed of the voice with the source, **RM** (osc A, osc B) multiplies the output with the source. Both slots work this way. (Wavetable oscillators: all 13 types in both slots.)
* **Warp slot 2** (`oscN.warp2.*`): runs on the phase slot 1 produced (A15), all 13 types, skipped completely when None (the sound of a patch with slot 2 off is bit-identical to before: golden test). An oscillator waits for the FM / RM sources of both slots.
* **Quality tiers**: sample interpolation 4-tap (tier low: oversampling 1), 8-tap (medium), 16-tap with mip blending (high); `maxUnison` caps the sample unison too. No module is bypassed.
* **engine-own (iOS)**: none of the bits.

### Differences / limits (not hidden)

* `loop_link` is an editor action (keep the loop length while dragging the start): no DSP effect. The key is stored.
* `loop_relative` On: loop start / end are fractions of the playback region instead of the file.
* A change of `oscN.type` while a note sounds takes effect at once for the running voices (the wavetable path stops, the sample path starts from the next note); no 10 ms crossfade yet.
* Normalize uses the peak of the whole file, not of the start..end region.
* The wavetable file format is WAV (the `.sbwt` of C4 does not exist in the repository).
* The Vital import (11 Annex) still maps nothing to `oscN.type` (WP9).

## WP5 - mixer graph and racks

Bits set: `kCapMixerGraph`, `kCapFxRacks`. Tests: `core/engine-vital/tests/test_wp5.cpp` (9 cases: filter blend crossfade, None as modulation source, sends and buses and Other bus and Direct, filter destinations, racks, per-rack order, 16 slot repair, random settings), plus the random key runs of `test_rt_alloc` (0 allocations).

* **Sources**: `oscN.destination` / `noise.destination` 5 = Filter blend (equal power between the filter inputs; a switched off filter lets its share through to the effects, as for the engine's own filter destinations), 6 = None (no audio, still a warp source). `oscN.filter_blend`, `oscN.busM_send`, `noise.*` are poly modulation destinations ramped over the block; a send taps the source after level and pan whatever the destination is.
* **Filters**: `filterN.destination` (Effects only / Direct out / None; in series the filter that feeds the other has no destination of its own) and `filterN.busM_send` (tap after the filter's mix).
* **Buses**: sends -> (Other bus) -> rack -> `busN.level` -> Main / Direct / Other bus (both on Other: bus 2 goes to Main; bus 2 runs first when it feeds bus 1), `busN.fx_bypass`. Main = voices + buses to Main -> Main rack (`main.fx_bypass`) -> master volume.
* **DIRECT**: skips every rack, scaled by `direct.level`, added after the racks. **Deviation from A16**: it still passes `master.volume`. In version 2 the engine's Direct out is summed before the volume, and R5 (a version 2 patch with an oscillator at Direct out must sound the same) wins over the assumption that Direct skips the master volume.
* **Racks**: `<fx>.rack` for the 16 modules (Main, Bus 1, Bus 2, 7 band racks). `ReorderableEffectChain` was rewritten: each effect processes the signal of its own rack, in the order of `fx.order.1..16` filtered to that rack (per-effect segment processing of the Vital effects: every module is called on its own); `BandSplitterModule` is the interface the splitters of WP7 implement (split, run the band racks, merge). The modules 9..15 are pass-through placeholders in WP5.
* **`fx.order` repair** for 16: `resolveFxOrder (choices, order, 16)` (a later position takes the lowest module not yet placed; tested). The float encoded `effect_chain_order` (9! fits a float, 16! does not) is replaced by 16 controls; the Vital import still decodes the old code in `core/vital-import`.
* **Version 2 sound**: the default patch and the busy patch of `test_new_keys` render bit-identically (the effects run in the same order on the same signal; a silent bus costs one scan of the block).
* **Tier low / medium**: no module is bypassed by the mixer graph; the racks run the effects the tier already allows (`fxMask`). A bus that is silent and has no enabled effect is skipped.
* engine-own: none of the bits.
