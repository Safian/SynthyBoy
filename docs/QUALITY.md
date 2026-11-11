# Audio quality gates

What the test suite measures about the sound of the Vital-derived engine through SynthyBoy's own parameter model, the numbers it
measured, and the tolerance of every assertion. The tests are `core/engine-vital/tests/test_vital_audio.cpp`
(`synthyboy_test_vital_audio`, about 25 s in a release build, label `slow`), `test_vital_params.cpp` (converters, tiers, built-in tables,
matrix wiring) and `core/tests/test_rt_alloc.cpp` (no allocation while patches, matrices and the quality change). Every number below
was measured on the development laptop (Apple M5 Pro, clang 21, 48 kHz); the thresholds in the code are those numbers plus the margin
named next to them. Nothing was loosened to make a test pass.

## Parameter sweep (item a)

Every one of the 880 parameters alone at minimum, default and maximum while four notes play (keys 41, 60, 67, 88; high tier), plus 20
whole patches with every parameter random (both tiers): all output finite, **peak at most 4.0**. The engine ends in a hard clamp at
+-2.1, so the highest peak measured is 2.1 (master volume at +6 dB, and a few random patches); 4.0 is the documented ceiling that
would expose a missing clamp. No allocation in `process()` in any of the 2 640 + 40 runs.

*Defect found and fixed:* "Inharmonic Stretch" (spectral morph type 4) returned NaN from about 92 % amount on (and a NaN stays in
the filters and effects for ever). Two defects in `spectral_morph.h`, see docs/UPSTREAM.md; regression test `morph_never_gives_nan`
(330 combinations of type, amount, spread and register).

## Tuning (item b)

Oscillators 1, 2, 3 with Sine, Saw, Square and Triangle at keys 33, 57, 69 and 93, one unison voice, filter off: fundamental by FFT
(0.8 s, 2^17 points, parabolic peak interpolation) against 440 * 2^((key - 69) / 12). **Worst error 0.010 cent** (oscillator 1, Saw,
key 33). Limit 2 cents (432 checks). No warp mode was measured for pitch (a warp mode may shift the fundamental by design).

## Alias budget (item c; docs/ARCHITECTURE.md 3.2)

Metric: energy that is not within +-8 bins (5.9 Hz) of a harmonic k * f0 (k * f0 below Nyquist), relative to the total energy, 0.83 s of a
held note at 48 kHz after 0.25 s, 4-term Blackman-Harris window (sidelobes below -92 dB), 2^16 points. Worst over the keys (every third key
21 .. 120 for the plain shapes, ten keys 21 .. 120 for the morphs), filter off. The guard in the test is the measured worst case plus 3 dB.

| Mode | high tier (2x oversampling) | low tier (1x) |
|------|-----------------------------|---------------|
| Saw | -35.5 dB (key 84) | -10.9 dB (key 120) |
| Square | -34.3 dB (key 84) | -10.0 dB (key 120) |
| Pulse 25 % | -36.0 dB (key 84) | -11.6 dB (key 120) |
| Saw + Vocode (50 %) | -59.9 dB | -50.6 dB |
| Saw + Formant Scale | -59.4 dB | -47.9 dB |
| Saw + Harmonic Stretch | -64.8 dB | -48.3 dB |
| Saw + Smear | -40.5 dB | -2.7 dB |
| Saw + Random Amplitudes | -52.0 dB | -34.0 dB |
| Saw + Low Pass | -67.4 dB | -10.9 dB |
| Saw + High Pass | -30.7 dB | -16.2 dB |
| Saw + Phase Disperse | -51.6 dB | -10.9 dB |
| Saw + Time Skew | -51.6 dB | -10.9 dB |
| Saw + Inharmonic Stretch, Shepard Tone | not a budget: the partials leave the harmonic series on purpose | |

**Findings (reported, not hidden): the engine does not meet an alias bound of -60 dB.**

* High tier: a plain Saw / Square / Pulse reaches **-35 dB** at some keys (key 84 = 1046 Hz is the worst of the 34 measured; the
  per-key values go from -35 dB to below -90 dB, depending on where the first partial above Nyquist falls). The cause is visible in the
  spectrum: the band limit of the wavetable lets partials up to about 26 kHz through, the 2x oversampling's halfband decimator
  attenuates them only partly in its transition band, and they fold back to 22 .. 24 kHz (key 69: a line at 23.8 kHz, the 55th partial
  of 440 Hz, -57 dB). The 80 dB below the fundamental that docs/ARCHITECTURE.md 3.2 states as the example target is not reached.
* Low tier (1x): the table's band limit is not matched to a sample rate without oversampling: **-11 dB at the top octave** (key 120), -38 dB at key 66 rising to
  -11 dB (every third key measured). This is the setting for the MPC; it is a property of the tier as the engine implements it.
* The spectral morphs add content that is not harmonic of the fundamental (Smear, High Pass at -31 .. -41 dB on the high tier).
* Not changed in Phase 3: the guard keeps the measured behaviour from getting worse; improving it (a steeper decimator, a lower band limit
  per tier) is engine work for a later phase.

## Filters (item d)

Noise (the noise oscillator at 100 %) through filter 1, 1 s per measurement. RMS by cutoff 150 Hz .. 9.6 kHz (octave steps) rises
monotonically for the five low-pass models (Analog 0.0084 .. 0.0613, Dirty 0.0113 .. 0.0729, Ladder 0.0091 .. 0.0625, Digital 0.0107 ..
0.0830, Diode 0.0029 .. 0.0461); tolerance 3 % per step. Resonance 20 % -> 80 % raises the power in 0.8 .. 1.25 x the cutoff by +7.1 / +8.5 /
+6.2 / +12.3 / +7.2 dB (guard: more than 3 dB). Resonance 100 %, 2 s: finite, peak below 4.0, the late half not above 2x the early half
(no blow-up). An LFO on the cutoff moves the spectral centroid by a factor of 5.1 (1.11 without; guards 1.5 and 1.15). The Formant filter
does not follow the cutoff (it is steered by X / Y), Comb and Phaser move the centroid from 6.4 to 10.7 kHz and from 10.8 to 12.7 kHz over 200 .. 3200 Hz; all
three are bounded (peak 0.23 .. 0.50).

## Modulation matrix (item e)

* LFO 1 (4 Hz sine) on the oscillator level (bipolar, 40 %): envelope max / min 2.42 (7.7 dB) against 1.016 without (guard: 2.0).
* Envelope 2 (400 ms attack) on the cutoff: centroid 317 Hz at the start, 3247 Hz after the attack, 209 Hz without the slot (guard 1.5x).
* Macro 1 on the level: +21.9 dB between macro 0 and 100 % (guard 3x). Velocity (engine velocity tracking off): loud / soft 2.71 with the slot, 1.000
  without. Mod wheel event: 12.4x between 0 and 1 (guard 2x).
* Slots that do nothing - no destination, no source, bypassed, amount 0 - give exactly the output of the unwired patch (difference 0, limit 1e-6).
* A stereo slot modulates the two channels in opposite directions: correlation of the L / R envelopes -0.994 (mono slot +1.000).

## Unison (item f)

Saw, key 57, filter off. Seven voices (detune 25 %, spread 100 %, blend 80 %) against one: 79 % of the power stays within +-2 % of the
fundamental (100 % for one voice; guard 90 %), L / R correlation 0.167 (1.0000 for one voice; guard 0.9), level -0.18 dB (guard +-3 dB).
Detune 0 and spread 0: a copy of the single voice (residual 1.5e-7) at +9.03 dB (the engine's level scaling of 7 coherent voices);
spread 0 with detune: L and R identical (difference 0).

## Effects (item g)

Reverb (mix 50 %, decay 2 s): RMS 0.0109 in 0.3 .. 0.8 s after the note off, exactly 0 without. Delay (free time, mono): echo of a noise burst at
120.021 / 250.021 / 400.021 ms (limit +-1 ms). Compressor: peak -31.9 dB. EQ mid +12 dB at 1 kHz: +10.1 dB there, +0.1 dB at 6 .. 9 kHz. Chorus 10.3 dB, flanger 0.68
dB, phaser 3.5 dB, distortion 8.4 dB mean spectral change (guard 0.5 dB). Effect order: distortion >> delay against delay >> distortion differ by 0.46 (relative
RMS, guard 0.05); nine equal choices still make a valid order. All of it without allocation.

## Voices (item h)

Polyphony 4 with eight held notes: 4 voices. Stealing with two voices (60, 64, 67): the oldest note (60) is gone (0.0026 against 1800 for the others). All notes
off ends in exact silence and 0 voices. Legato (one voice, 600 ms attack): the level after the second note is 1.002 of the level before, 0.017 without legato (it
restarts the attack). Portamento 150 / 300 / 600 ms, linear slope, one voice: 90 % of the octave after 91.9 / 90.6 / 90.3 % of the set time (limit +-10 % of the
time; the glide is linear in pitch and ends exactly at the set time). Pitch bend (range 2 / 7 / 12 / 24, bend +1 and -0.5): within 2 cents (limit 2 cents). **Sustain
pedal: not exposed** (the event contract has no pedal; the engine has it).

## Tiers (item i)

20 notes held with a patch that asks for 32 voices and 16 unison voices on three oscillators: 8 voices sound on the low tier, 20 on the high tier, the stored parameters are
unchanged. The same patch on the low and the high tier: spectral centroid within +-15 % (measured ratios 1.009, 0.986, 0.960 for Saw, Square, Formant Sweep).

## State (item j)

A random walk of 300 steps over the whole table, saved with `saveState`, loaded into a new `ParamValues`, played by a fresh engine: **bit identical** to the engine that
played the original values, on both tiers, with the noise oscillator, random phase and random LFO stereo on, and another engine in between. This needed one change to the
imported code: the seed counter of its random generators is thread local and `prepare()` starts it at 0 (docs/UPSTREAM.md); before it, two engines of the same patch
sounded different.

## Known behaviour that is not a defect but not obvious

* The first note of an engine differs in its attack from the notes after it (docs/UPSTREAM.md); `reset()` does not return to the very first state (steady state
  identical, the attack of the first note after a reset can differ by 0.2 on a 0.4 peak). The adapter plays two notes in `prepare()`.
* A modulation amount of 100 % is a sweep of the destination's own range in the engine's scale (docs/PARAMETERS.md).
* A slot that would close a loop in the engine's graph stays unwired (docs/UPSTREAM.md); the tests only assert that this never allocates.


## Tiers and the modules of state version 3 (WP4 to WP8)

* `Quality::fxMask` has one bit per effect module (bits 9 .. 15: Hyper / Dimension, Utility, Bode, Convolve, the three splitters). **Tier low bypasses Hyper / Dimension, Bode and Convolve** (in addition to the reverb and the flanger); medium and high run all modules. `reverb.force_enable` puts the reverb back on a tier that masks it (`effectiveFxMask ()`); the other masked effects stay off.
* Sample oscillator: the interpolation follows the tier (4 taps on low = oversampling 1, 8 taps on medium, 16 taps with mip blending on high), `maxUnison` caps the unison voices of a sample too. No other bypass.
* Serum filter models: four scalar filters per voice pair and filter slot; no bypass on any tier. Memory: about 180 KB per filter at 96 kHz (136 voice filters about 25 MB).
* Racks, buses and DIRECT: a silent bus without enabled effects costs one scan of the block.
* Measured by the new tests (not a performance claim, docs/PERFORMANCE.md has the numbers): no allocation in `process ()` with the sample oscillator, user tables, the 37 filter types, buses, racks, the Serum modules and content replacement under a counting `operator new` (`test_rt_alloc.cpp`).

### Tier table of the modules of state version 3

What each tier does with the new modules (`qualityForTier`, `core/include/synthyboy/quality.h`; the tier only caps or bypasses, it never changes stored values).
"bypassed" means the module is switched off by `Quality::fxMask` and costs nothing.

| Module | low (MPC class) | medium (phone) | high (desktop) | Test |
|--------|-----------------|----------------|----------------|------|
| Voices / unison per oscillator | 8 / 4 | 16 / 8 | 32 / 16 | `test_vital_params` (tiers) |
| Oversampling, wavetable interpolation | 1x, linear | 2x, linear | 2x, spectral | alias table above |
| Sample oscillator | 4-tap, oversampling 1 | 8-tap | 16-tap with mip blending | `test_wp4` |
| Serum filter models (37 types) | runs | runs | runs | `test_wp7`, `synthyboy_test_filters_serum` |
| Buses, DIRECT, FX racks | runs | runs | runs | `test_wp7` |
| Reverb, flanger | bypassed | runs | runs | `test_vital_audio` (tiers) |
| Hyper / Dimension | bypassed | runs | runs | `test_wp7`, `synthyboy_test_fx_serum_a` |
| Bode (frequency shifter) | bypassed | runs | runs | `test_wp7`, `synthyboy_test_fx_serum_b` |
| Convolve (partitioned FFT) | bypassed | runs | runs | `test_wp7`, `synthyboy_test_fx_serum_b` |
| Utility, splitters | runs | runs | runs | `test_wp7` |
| Tuning, key scale, MPE, curves, custom LFO shapes, oscillator mapping | no tier dependence | | | `test_wp123` |

Measured behaviour of the tiers on the 100 factory presets (below) is identical between low, medium and high to within 2.2 dB in the peak of any
preset: the factory presets use one oscillator, one filter and at most the unison voices the low tier allows, so the caps do not bite.

## Sanitizer, fuzz and factory preset gates (2026-11-08)

* ASan + UBSan over every core test, the GUI tests (plugin build) and `synthyboy_fuzz_smoke`; ThreadSanitizer over every test except the `slow` audio gates (the
  thread tests, the Convolve builder thread with `Quality::realtime` on and off, the content hand-over). Findings and fixes: `docs/ui-design/SWEEP-REPORT.md`.
* `synthyboy_fuzz_smoke` (fixed seeds, 5 s under ASan + UBSan; `SYNTHYBOY_FUZZ_SCALE=N` multiplies the iterations): state / preset loaders, content pack manifest, Vital import,
  SCL / KBM / TUN, formula compiler and renderer, three WAV readers, garbled state through both engines. Clean at 20x.
* `synthyboy_preset_sanity` (label `slow`, about 50 s native): every factory preset at every tier renders finite, is not silent (peak of a single note at least -60 dBFS),
  stays below +3 dBFS with a four-note chord at velocity 1.0, and its tail ends (below -90 dBFS for 250 ms within 12 s of the note off). Numbers: SWEEP-REPORT.

## Parameter effect audit (2026-11-11)

Does the sound at the output really change, and the way it should, when a value changes? `scripts/run-param-audit.sh` answers that for the whole parameter table of the
Vital-derived engine (`tools/param-audit`, report and numbers: `docs/ui-design/PARAM-AUDIT-REPORT.md`):

* **Stage 1, the scan** (`synthyboy_param_audit`): every key the engine supports is put into its context (the first state of a ladder in which it is audible: effect on, oscillator
  type, routed LFO / envelope, filter model ...), rendered offline at every value (`--full`: every step of a stepped key, 65 points + default, both neighbours and the edges of a
  continuous one; `--quick`: nine) and measured (RMS, peak, centroid, 24 bands, balance, f0, envelope, zero crossings, click, level modulation). Modulatable keys are swept again
  through a routed source (macro 1 in a modulation slot). A key is ALIVE if the audio changes by more than the noise floor of the harness (-60 dB re peak, or 3 dB above the run to run
  noise of a random context), DEAD if not in any context of the ladder; DEAD keys need an entry with a reason in `tools/param-audit/exemptions.tsv` or the run fails.
* **Stage 2, the semantics** (`synthyboy_test_param_semantics`, ctest label `slow;audit`): pitch in cents, levels in dB, the pan law, filter cutoff / resonance, envelope times, LFO rate,
  mod matrix amount (all 64 slots), delay time, EQ gain, effect mix 0 = dry, velocity law, portamento time ... against the expected function, point by point over the sweep.
* **Baseline**: `docs/ui-design/param-audit-baseline.json` holds verdict + a compact metric curve (nine points) per key. A run reports REGRESSIONS (changed verdict or curve) with the old and
  new numbers and exits 1; intended changes are accepted with `--update-baseline` and show up in `git diff`. Keys whose context holds a random source (granular grains, the reverb, the Random LFO; `curve:` lines of `tools/param-audit/exemptions.tsv`) are compared by verdict only.
* **Mutation self test** (`--self-test`, ctest `synthyboy_param_audit_selftest`): seven faults are injected into the harness through `SYNTHYBOY_AUDIT_FAULT` (cutoff scaled, pan swapped, delay
  time x2, transpose 2 cents off, master volume in half the dB, attack halved, chorus controls unwired); every one must be caught.

```
scripts/run-param-audit.sh --quick                  # minutes: the CI subset (job `param-audit`, ctest `synthyboy_param_audit_scan`)
scripts/run-param-audit.sh --full --jobs 8          # every value of every key (hours)
scripts/run-param-audit.sh --group filter1. --full  # only a part;  --keys REGEX also works
scripts/run-param-audit.sh --keys '^filter1\.cutoff$' --keep-wavs /tmp/w   # listen to what a key does
scripts/run-param-audit.sh --quick --update-baseline                       # accept an intended change
```

Output: `build/param-audit/<UTC timestamp>/` (+ `latest`): `report.md`, `keys.csv`, `keys.json` (with the curves), `semantics.log`. Exit codes: 0 clean, 1 regression / unexplained DEAD /
failed semantic check, 2 usage, 3 build problem.

## CI: what runs where (2026-11-11, `ci-repair`)

Tests that are slower than a native run by an order of magnitude (ThreadSanitizer, ASan, QEMU user mode) use `sbtest::instrumented ()` (`core/tests/test_runner.h`): true when the
executable is built with a sanitizer, or when `SBTEST_INSTRUMENTED=1` is in the environment (the `core tests (linux-armv7, qemu)` job sets it). An instrumented run keeps every
correctness check and reduces the amount of work with `sbtest::scaled (native, instrumented)`; wall clock budgets are not asserted. The native jobs run everything in full.

| test | native (macOS, Windows, Linux x86_64, AArch64) | ASan + UBSan | ThreadSanitizer | ARMv7 under QEMU (`SBTEST_INSTRUMENTED=1`) |
|------|------|------|------|------|
| `synthyboy_test_threads` | 3000 blocks against the parameter and reader threads | 100 blocks, the same threads | 100 blocks | 100 blocks |
| `synthyboy_test_vital_fxinstances`, parameter mapping test | all 140 parameters | every 4th parameter of each type | every 4th | every 4th |
| `synthyboy_test_vital_fxinstances_rt`, 16 heavy slots block budget | asserts `mean < 2.667 ms` per 128 frame block | renders, checks the output, no time assertion | same | same |
| `synthyboy_test_vital_warpenv` (label `slow`, `TIMEOUT 3600`) | in full, about 300 s | in full (1300 s) | not run (single threaded audio checks, `-LE slow`) | not run (`-LE slow`) |
| the other `slow` tests | in full | in full | not run | not run |

Platform differences that were found by these jobs and are fixed in the code, not in the tests: the oscillator frame index used `toInt`, which rounds to nearest on x86 and
truncates on ARM (now `floorToInt` in `synth_oscillator.cpp`, reproduced on x86_64 with the old code and gone with the new); the Convolve module never freed its active convolver and
every engine leaked one `Value` (`lowest_active_note_`), both found by LeakSanitizer; `ContentRegistry::resolvePresetPath` returned a path with mixed separators on Windows. The
exact text of the busy patch in `test_state_v3.cpp` depends on the C library's `exp` / `log` (Windows, ARMv7), so it is fingerprinted with the numbers rounded to 3 digits (the
same value on macOS arm64, Linux x86_64 and ARMv7; the defaults keep the byte exact fingerprint). Byte-exact generated files (`content/presets/**`, `docs/params.json`, `docs/PARAMETERS.md`, `docs/kick-pages.json`) are `eol=lf` in
`.gitattributes` so that a Windows checkout does not change the bytes the generators and tests compare.
