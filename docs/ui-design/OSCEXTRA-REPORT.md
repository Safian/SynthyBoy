# OSCEXTRA report - sub oscillator (G23) and noise colours / samples (G38)

Block **B18** (drafted as "B13" by the first worker on branch `agy/oscextra`, cut from commit 629805b; taken over and merged onto develop 1879711). Spec: `13-new-keys.md` 4.18, assumptions A60-A69, morning question 27.

## Numbers

| Item | Value |
|------|-------|
| Keys | 18 (sub 11, noise 7), table positions 1558..1575, table 1576 keys |
| Modulatable | 10, destination choices 432..441, `kNumModDestinations` 442 |
| Capability bits | 35 `kCapSubOsc`, 36 `kCapNoiseExtras` |
| Kick | page 116 `sub_main` (8 knobs + 3 sends on the more sheet); `noise` page gets the 7 noise extras on its more sheet; 116 pages, all 1576 keys reachable |
| UI rows | 02-27 (Sub waveform picker), 02-28 (Noise sample `< >`) closed |

## Merge

Merged `agy/oscextra` into `work-oscextra` (from develop). Conflicts in the param table, caps, modulation / kick constants, five test files, the three docs and the 112 presets were resolved by hand: keys appended after B17, bits renumbered after 34, destinations after 431, Kick page appended at the end (the worker had inserted it in the middle, which would have renumbered pages 79+). `docs/params.json`, `PARAMETERS.md`, `kick-pages.json` and the 112 presets were regenerated with the repo tools (`check-params-doc.sh`, `gen-factory-presets.py --check` pass). The worker's accidental `Testing/Temporary/LastTest.log` was dropped.

## Defects found in the Gemini commit and fixed

1. **Sub square was inverted and its PolyBLEP had the wrong sign** (overshoot to -2 at every edge); **pulse was not band-limited** although the spec claimed it. Rewrote `sub_source.cpp` with one PolyBLEP helper and a shared edge function (saw, square, 25 % pulse); triangle now starts at zero like the sine; rounded rect is normalised to peak 1.
2. **Mono hand-over clicked** (hard mask). Gate is now ramped over the block.
3. **`noise.loop` off held the last sample value forever** (DC at about -0.24) instead of ending. `sample_source.cpp` now outputs silence after the end of a one-shot (only the noise uses `SampleSource`; loop was not reachable before).
4. Tests were weak (waveform "differs" checks that any noise passes; no tests for fine, phase, random phase, loop, filter blend, sends, matrix modulation, defaults). Replaced by `core/engine-vital/tests/test_oscextra.cpp`: 14 cases, 1695 checks (spectra of all 6 waveforms with harmonic levels and an alias bound, pitch of octave / tune, pan and every destination, filter blend, both sends, mono hand-over, noise colour tilt and crest factor of all 9 samples, pitch / fine / keytrack through a spectral corner, start phase against a delayed copy, random phase, loop, defaults bit identical, every modulatable key of the block moved by a macro).
5. The old tests compared renders on one engine; the Vital sample oscillator keeps interpolation history per voice, so two notes are never sample identical. The new tests render each case on a fresh engine.
6. Misc: stale counts in `test_fxextra_keys.cpp`, `UI-GESTURE-AUDIT.md` header counts, section numbering / totals / Kick page counts in `13-new-keys.md`.

## What was finished

* Engine: sub oscillator (module, source, voice-handler lowest-note input, producers wiring, mixer graph), noise sample sets, start phase, pitch / fine / keytrack / random phase / loop bindings; defaults neutral (test `oscextra_defaults_leave_the_engine_unchanged`, presets regenerate with only the new keys appended).
* UI tests: `uig_sub_and_noise_panels_on_phone_and_tablet` (390x844 and 1024x768: touch targets, no overlap, stepper wrap, level / pan drag, More sheet, screenshots `/tmp/ui-shots/gaps/sub-noise-*.png`) and `uig_sub_and_noise_knobs_take_modulation_by_touch` (assign by touch for `sub.level`, `noise.pitch`, `sub.bus1_send`; choices and switches are not targets).

## Notes

* `state_random_walk`: the fixed seed of the test went silent with the 1576-key table, changed 2025 to 2026 (comment in the test).
* Open (morning question 27, updated by MANUALCHECK): the waveform list and the four noise colours are the manual's; the other noise sample names and the pulse width are our own; `sub.tune` is now CRS in semitones, `sub.mono` defaults Off and `sub.destination` defaults Effects only (13-new-keys.md 4.18, 4.19).
* The panel text of an externally changed `noise.sample` (automation) updates through `ParamHandle::onChange` like every other panel; the headless rig does not pump that callback, so only user steps are asserted.
