# PARAM-AUDIT-REPORT: does the sound change, and the way it should, when a value changes?

Branch `param-audit` (cut from develop 2ccc471). The owner's question: "test that when I change a value, the sound at the output really changes, and the way it should".
This report is the answer for the whole parameter table of the Vital-derived engine (`SYNTHYBOY_ENGINE vital`, all capability bits), the permanent gate that keeps it true, and the
list of what the audit found.

## 1. What was built

| Piece | Where |
|---|---|
| Harness library: one engine, deterministic offline renders, the content fixtures (wavetable, samples, instrument, impulse, LFO shapes, curves ...), note scripts, 36 metrics, fault injection | `tools/param-audit/{audit.h,rig.cpp,metrics.cpp}` |
| Contexts: for every key group the ladder of states in which the key is audible, the sweep values (every step / 65 points + default + neighbours + edges) | `tools/param-audit/contexts.cpp` |
| Stage 1, the scan (`synthyboy_param_audit`): every key at every value, plus the same sweep through a routed modulation source | `tools/param-audit/param_audit.cpp` |
| Stage 2, the semantics (`synthyboy_test_param_semantics`): 31 test cases, expected curve against measured curve point by point | `tools/param-audit/test_param_semantics.cpp` |
| The single entry point (build, shards, stage 1 + 2, merge, baseline, self test) | `scripts/run-param-audit.sh`, `scripts/param_audit_report.py` |
| Exemptions (DEAD keys with a reason) and the committed baseline / compact key table | `tools/param-audit/exemptions.tsv`, `docs/ui-design/param-audit-baseline.json`, `docs/ui-design/param-audit-keys.csv` |
| Gates | ctest `synthyboy_param_audit_scan` (quick, label `audit`), `synthyboy_test_param_semantics`, `synthyboy_param_audit_selftest`; CI job `param-audit` |

```
scripts/run-param-audit.sh --quick                  # minutes: instance 1 + the last instance of every family, 9 values per key (CI)
scripts/run-param-audit.sh --full --jobs 8          # every value of every key (instances 2 .. N of a family at the quick density; --all-instances for all)
scripts/run-param-audit.sh --group filter1. --full  # one group;   --keys '^osc2\.warp\.' also works;   --engine own for the keys engine-own claims
scripts/run-param-audit.sh --keys '^filter1\.cutoff$' --keep-wavs /tmp/w          # the audio of a key to listen to
scripts/run-param-audit.sh --quick --update-baseline                              # accept an intended change (git diff shows it)
scripts/run-param-audit.sh --self-test                                            # the mutation proof
```

Exit codes: 0 clean, 1 regression / unexplained DEAD / failed semantic check (self test: a fault slipped through), 2 usage, 3 build problem.
Output of a run: `build/param-audit/<UTC>/` (+ `latest`): `report.md`, `keys.csv`, `keys-compact.csv`, `keys.json` (with the metric curves), `summary.json`, `semantics.log`.

## 2. Method

### 2.1 The reference patch and the renders
A saw (`oscN.wavetable` Saw) through the default filter, two notes (C3 + G3) at velocity 100, held 0.6 s, 1.0 s in all; no random phase; master volume -12 dB; the engine is
created at the offline quality (high tier, 2x oversampling, `Quality::realtime` off) and every value is rendered after `reset ()`. The scripts of the other contexts: `long` (2 s: delay,
reverb, convolve, bode, effect instances), `single`, `chord8`, `legato`, `seq`, `same`, `fast`, `bend`, `mpe`, `scale`. The parameter smoothing is off in the audit (`quality.no_smoothing`) except in
the zipper test, which measures it.

### 2.2 Contexts
A key is only audible in a state: `chorus.depth` needs `chorus.on`, `osc2.warp.amount` needs `osc2.on` and a warp type, `lfo3.rate` needs an LFO routed to something, `filter2.formant.x` needs
a formant model, `osc1.gran.*` needs the granular oscillator, `fxi05.p07` needs the effect type that maps it. `contexts.cpp` holds a ladder of such states per key group (the first state in which the
key sounds different is used; DEAD means the whole ladder was tried), with the content fixtures (an 8 frame wavetable, samples, an instrument, an impulse response, custom LFO shapes, remaps,
filter curves, modulation curves on slots 61 .. 63) always loaded.

### 2.3 Values and metrics
Stepped and choice keys: every step (up to 129 values, then 65 evenly spread + the default and its neighbours). Continuous keys: 65 points linear in the NORMALISED value (so a skewed key is swept
in its own display scale) + the default + both neighbours of the default (1/64) + 0.001 and 0.999 + both edges; the two values just outside the range are rendered too and must sound like the edge.
`--quick`: nine points. Metrics per render: RMS and peak (dB), spectral centroid, a 24 band spectrum (40 Hz .. 18 kHz), stereo balance, fundamental frequency (autocorrelation), attack time,
decay level, release time, zero crossings, click (largest step against the bound of the pitch), level modulation depth and rate, tail level. A moved metric is a change above 0.1 dB (level, balance,
bands), 5 Hz (centroid), 0.2 Hz (f0), 0.5 ms (attack), 2 ms (release), 2 crossings/s, 2 % (click).

### 2.4 ALIVE / DEAD and the noise floor
A key is ALIVE if some value renders differently from the default by more than the NOISE FLOOR: the peak of the difference against the peak of the render must exceed -60 dB, and 3 dB more than the
run to run noise of the context when the context is random (random LFOs, free running effect state). Why not "exactly equal": renders of one engine instance are not independent of each other
(section 4: the first renders after many changed keys differ from the later ones by up to -55 dB; a reused voice keeps about -70 dB of its last note for 40 ms). Everything below -60 dB re peak
is inaudible next to the signal; the harness plays a warm-up pass over the values before it measures.
DEAD = no value of the key changed the sound in any context of the ladder. Modulatable keys are swept a second time through macro 1 in slot 64 (full amount, direction toward the larger headroom).

### 2.5 Assertions on every value
Finite output; peak below +12 dBFS; no click (a step of more than half the peak AND 12 times the bound of the pitch, unless the key is abrupt by design); values outside the range render as the edge; the
default renders the same after the sweep (up to the floor); plateaus: neighbouring values of an alive continuous key that render identically are reported (a plateau inside the range is a finding, one at an
end of the range is saturation and is marked INFO).

## 3. Numbers (full run, engine vital, all capability bits, 3700 s on 8 processes)

| | |
|---|---|
| keys in the table | 2628 |
| supported by the engine (supportsParam, all capability bits set) | 2628 |
| ALIVE (the sound changes with the key beyond the noise floor) | 2561 |
| DEAD | 65: explained 65 (open owner question 115: 42, tested by a dedicated semantic test: 18, editor only: 3, instance at the quick density with a live twin: 2), UNEXPLAINED 0 |
| EXEMPT (needs an MTS-ESP master, the core has only the seam) | 2 |
| modulatable keys swept through a routed macro: alive / dead | 1076 / 103 (dead: explained 103, unexplained 0; all in question 115, or the keys of the dead list) |
| per value findings | 432, of which INFO 412 (saturation at an end of the range, nondeterministic context), unexplained 0 |
| keys with a quantitative semantic assertion (stage 2) | 280 (89 named in a test, plus source / destination / amount of the 64 slots) in 31 test cases, 133 checks, 0 failed |
| regressions against the committed baseline | 0 |
| defects fixed in the engine | 3 (section 4), 2 regression tests |

Per group (`osc1` is instance 1, `oscN` the group of osc2 / osc3 and so on):

| group | keys | alive | dead | exempt | unsupported | modulation alive | modulation dead |
|---|---|---|---|---|---|---|---|
| bode | 16 | 15 | 1 | 0 | 0 | 10 | 0 |
| bus1 | 3 | 3 | 0 | 0 | 0 | 1 | 0 |
| bus2 | 3 | 3 | 0 | 0 | 0 | 1 | 0 |
| chorus | 14 | 14 | 0 | 0 | 0 | 9 | 0 |
| compressor | 26 | 25 | 1 | 0 | 0 | 9 | 0 |
| convolve | 15 | 15 | 0 | 0 | 0 | 9 | 0 |
| curve | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| delay | 16 | 16 | 0 | 0 | 0 | 9 | 0 |
| direct | 1 | 1 | 0 | 0 | 0 | 1 | 0 |
| distortion | 11 | 11 | 0 | 0 | 0 | 7 | 0 |
| envN | 96 | 96 | 0 | 0 | 0 | 66 | 0 |
| eq | 15 | 15 | 0 | 0 | 0 | 10 | 0 |
| filter | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| filter1 | 23 | 23 | 0 | 0 | 0 | 17 | 0 |
| filter2 | 23 | 23 | 0 | 0 | 0 | 17 | 0 |
| flanger | 11 | 11 | 0 | 0 | 0 | 7 | 0 |
| fx | 16 | 15 | 1 | 0 | 0 | 0 | 0 |
| fxfilter | 20 | 20 | 0 | 0 | 0 | 15 | 0 |
| fxi (16 instances) | 448 | 417 | 31 | 0 | 0 | 288 | 96 |
| hyperdim | 10 | 10 | 0 | 0 | 0 | 6 | 0 |
| lfoN | 170 | 170 | 0 | 0 | 0 | 90 | 0 |
| macro | 8 | 8 | 0 | 0 | 0 | 8 | 0 |
| main | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| master | 32 | 29 | 1 | 2 | 0 | 4 | 0 |
| mod (64 slots) | 1024 | 1024 | 0 | 0 | 0 | 256 | 0 |
| noise | 19 | 19 | 0 | 0 | 0 | 10 | 0 |
| osc1 | 142 | 133 | 9 | 0 | 0 | 56 | 2 |
| osc2 | 142 | 132 | 10 | 0 | 0 | 56 | 2 |
| osc3 | 142 | 133 | 9 | 0 | 0 | 56 | 2 |
| phaser | 13 | 13 | 0 | 0 | 0 | 9 | 0 |
| quality | 2 | 1 | 1 | 0 | 0 | 0 | 0 |
| reverb | 23 | 23 | 0 | 0 | 0 | 16 | 1 |
| rlfoN | 32 | 31 | 1 | 0 | 0 | 12 | 0 |
| splitter_lh | 6 | 6 | 0 | 0 | 0 | 2 | 0 |
| splitter_lmh | 8 | 8 | 0 | 0 | 0 | 3 | 0 |
| splitter_ms | 5 | 5 | 0 | 0 | 0 | 1 | 0 |
| sub | 14 | 14 | 0 | 0 | 0 | 8 | 0 |
| utility | 12 | 12 | 0 | 0 | 0 | 7 | 0 |
| voice | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| voicectl | 62 | 62 | 0 | 0 | 0 | 0 | 0 |

The full per key table is generated (`keys.csv`, 650 KB, not committed); `docs/ui-design/param-audit-keys.csv` is the compact committed version (key, verdict, moved metrics, modulation verdict) and
`docs/ui-design/param-audit-baseline.json` the baseline (verdict + a nine point curve of RMS / centroid / f0 / balance per key and of the modulation sweep, 430 KB).


## 4. What the audit found and what was fixed

### 4.1 FIXED: the modulation of the Serum effect modules and the splitters never reached the DSP
45 keys were flagged modulatable (and listed among the 1245 destinations of the matrix) but a macro, LFO or envelope routed to them did nothing: `hyperdim.{rate,detune,mix,dim_size,dim_mix,level}`,
`utility.{lpf,hpf,mono_freq,width,pan,mix,level}`, `bode.*` (10), `convolve.*` (9), `splitter_lh.{freq,level}`, `splitter_lmh.{freq_low,freq_high,level}`, `splitter_ms.level`.
Cause: `StereoFxModule::processWithInput` and `SplitterBase::split` (core/engine-vital/upstream/src/synthesis/modules/serum_fx_modules.cpp) never ran the control processors of their
`SynthModule` (the sums base value + modulation of the `createMonoModControl` controls), which every Vital module does (`SynthModule::process (num_samples)` at the start of `processWithInput`).
Fix: that call. Regression test: `regression_modulation_reaches_the_serum_effects_the_splitters_and_the_level_trims` (macro 0 against macro 100 differ by more than -10 dB re peak for 19 keys).

### 4.2 FIXED: the level trims `<fx>.level` of the effect chain were not modulated
`compressor.level`, `delay.level`, `eq.level` did not follow a modulation source. Cause: `ReorderableEffectChain::process` did not run the control processors of its own router. Fix: `SynthModule::process (num_samples)`
at its start. Same regression test (chorus, flanger, reverb, compressor, delay, eq levels).

### 4.3 FIXED: `reset ()` did not restart the effect LFOs
The second render of the same patch with chorus / flanger / phaser differed from the first by up to +1.5 dB re peak (the free running phase of the effect LFO continued after the reset), so an
offline render, a bounce and the steps of `renderOffline` were not independent of each other. Fix: `engine->correctToTime (0.0)` in `VitalEngine::reset ()` (the adapter already did it for the
LFOs of the voices). Regression test: `regression_a_render_after_reset_is_repeatable_with_effect_lfos` (chorus and phaser repeat bit exactly). State version 3 content and the 112 factory presets are not touched.

### 4.4 NOT fixed, owner question 116: renders are not bit repeatable
After `reset ()` the random generators (one `RandomGenerator` per module: random LFOs, the Random source, noise) and the reverb's modulation are not restarted: two renders of the same reverb patch differ by up
to -7 dB re peak, of a random LFO by -5 .. -10 dB. A voice that is reused keeps about -70 dB of its last note for 40 ms. The audit handles it with a noise floor of -60 dB (3 dB above the run to run noise of a
random context), a warm-up pass over the values and `quality.no_smoothing` in the harness. A thread local reseed epoch in `utils.h` was tried and removed: it did not change the reverb.

### 4.5 Contexts the audit needed (in the ladder, not engine defects)
Examples of states a key needs that are now in the ladder: `filter2.*` needs `osc1.destination` = Filter 2; `env1.decay` / `hold` need a sustain below 100 %; the delay / chorus / flanger / phaser / bode time keys act only with
`sync` = Free (the delay's default is Tempo); `oscN.env1` and the other `env1` buttons act when the channel goes straight to the effects with a slow ENV 1 attack (test_warpenv.cpp); `oscN.pitch_mode` needs `pitch_coarse`;
`oscN.fixed_pitch` needs `pitch_track` off; `oscN.gran.rand_warp` acts only on the pointwise warps (Quantize and the distortion group); `oscN.map.key_warp` needs another oscillator that FMs from it; `voicectl.osc.*` needs the
per voice detune / pan; the pool compressor needs `bands` and a quiet input for the lower thresholds. 111 keys were DEAD in the first full run; about 60 of them were such contexts.

### 4.6 Keys with no context found (OPEN, owner question 115, 42 keys)
`bode.retrig`, `fx.order.16`, `fxNN.order`, `master.voice_override`, `oscN.multi.vel_override`, `oscN.phase_memory`, `oscN.remap.fall`, `oscN.sample.loop_relative / scan_lock_tempo / slice_play`, `oscN.spec.flt_frame / transients`
(and the macro modulation of the pool compressor high band / level / limiter, the pool reverb room / width / diffusion, `oscN.sample.slice`, `oscN.spec.flt_frame`, `reverb.low_shelf_cutoff`, `rlfoN.keytrack_tune`). Each is either a state the
audit does not know or a key that is not wired; the list with the contexts tried is `tools/param-audit/exemptions.tsv`.

### 4.7 Deviations from the references, measured (owner questions 112 .. 114)
Sustain is the envelope value squared in amplitude (Vital), the cutoff is the pole frequency (the -3 dB point of a 12 dB low pass at resonance 0 is at 0.52 .. 0.63 of it), velocity tracking is the square of the velocity and
-100 % is not the exact inverse. Kept as Vital has them; the tests assert them tightly.

### 4.8 What the audit proved correct (selection of the quantitative results)
Pitch is exact: `oscN.transpose` -48 .. +48 (97 values) 0.0001 cent, `oscN.tune` / `master.tune` 0.0004 cent, `master.transpose` 0.0009 cent, `master.bend_range` 0 .. 24 0.0004 cent, `voicectl.sN.detune` 0.0004 cent; `master.volume` is 1 dB per dB to
0.0001 dB; the pan law is constant power to 0.011 dB; the filter resonance peak is at the displayed cutoff to 1/24 octave and the filter keytrack is octave per octave (within 0.06 octave); the LFO rate is exact to 0.001 Hz (3 .. 30 Hz); `delay.time`
is the echo spacing to 1 ms; the modulation amount is exactly the fraction of the destination's range in its own scale (0.0004 cent) in all 64 slots; envelope times are proportional to the display (attack 0.95, release -40 dB at 0.49, decay 0.41 .. 0.49);
portamento reaches its target after the displayed time (within 20 ms of 1.6 s); `osc1.phase` is the start phase to 2.1 degrees; effect mix 0 is the dry signal (bit exact for chorus, delay, flanger, phaser, distortion, hyperdim, convolve; level and spectrum
for reverb, compressor, bode); every parameter jump in the middle of a note is click free with the smoothing on (10 keys) and the smoothing option changes the landing of three of them; all 2628 keys clamp to their range and ignore NaN.


## 5. Stage 2: the semantic checks

Thirty-one test cases in `tools/param-audit/test_param_semantics.cpp` (ctest `synthyboy_test_param_semantics`, label `slow;audit`, 57 s). Each asserts a measured curve against the expected function point by point, with the tolerance
and its reason at the check. The printed lines of a run (worst deviation and tolerance per curve):

```
osc1.transpose (cents against the played key): 97 points, worst deviation 0.0001 cents at -46 (tolerance 1)
osc1.tune: 65 points, worst deviation 0.0004 cents at 12.5 (tolerance 0.5)
master.tune: 65 points, worst deviation 0.0004 cents at 12.5 (tolerance 0.5)
master.transpose: 49 points, worst deviation 0.0009 cents at -20 (tolerance 1)
master.bend_range (cents of a +1 bend): 25 points, worst deviation 0.0004 cents at 10 (tolerance 1)
master.volume (output dB): 61 points, worst deviation 0.0001 dB at -52 (tolerance 0.15)
osc1.pan (total power deviation): 65 points, worst deviation 0.0114 dB at -81.25 (tolerance 0.2)
filter1.cutoff (resonance peak against the display, cents): 5 points, worst deviation 50.0000 cents at 400 (tolerance 75)
filter1.resonance (peak above the pass band, dB): 21 points, end to end +24.8523 (min 6)
env1.attack (90 % level): ratio measured / display from 0.950 to 1.020 (allowed [0.87, 1.03], drift at most x1.10)
env1.release (-40 dB): ratio measured / display from 0.492 to 0.540 (allowed [0.44, 0.56], drift at most x1.10)
env1.decay (to the sustain): ratio measured / display from 0.410 to 0.490 (allowed [0.38, 0.52], drift at most x1.25)
env1.sustain (dB below full): 10 points, worst deviation 0.0000 dB at 10 (tolerance 0.6)
lfo1.rate (modulation rate, Hz): 11 points, worst deviation 0.0008 Hz at 30 (tolerance 0.02)
mod05.amount (cents at macro 100 %): 21 points, worst deviation 0.0004 cents at 30 (tolerance 0.5)
macro.1 (cents at amount +50 %): 21 points, worst deviation 0.0004 cents at 60 (tolerance 0.5)
modNN (slot 1 .. 64, cents): 64 points, worst deviation 0.0001 cents at 1 (tolerance 0.5)
voicectl.s1.detune (cents): 21 points, worst deviation 0.0004 cents at 60 (tolerance 0.5)
voicectl.s1.pan (L - R, dB, from left to right): 21 points, end to end -339.7212 (min 30)
master.polyphony (power re one voice, dB): 16 points, worst deviation 0.0792 dB at 5 (tolerance 1.5)
osc1.frame (centroid, Hz): 65 points, end to end +170.8475 (min 100)
distortion.drive (centroid, Hz): 61 points, end to end +93.8841 (min 50)
delay.time (first sound after, ms): 10 points, worst deviation 0.0000 ms at 40 (tolerance 3)
eq.mid.gain (peak of the band re the untouched bands, dB): 13 points, worst deviation 2.3372 dB at 0 (tolerance 2.5)
unison: level at 16 voices -2.96 dB, width 2.475
velocity: +100 %: -33.1 dB (vel 0.25) -9.0 dB (vel 1); 0 %: -9.0 / -9.0; -100 %: 0.7 / -9.0
velocity at track +100 % (dB): 18 points, end to end +39.1089 (min 6)
velocity at track +100 % (dB below velocity 1): 18 points, worst deviation 0.0000 dB at 0.1 (tolerance 0.5)
portamento 100 ms: within 10 cents after 95 ms
portamento 1600 ms: within 10 cents after 1580 ms
chorus: consecutive renders differ by 1.42109e-14
phaser: consecutive renders differ by 0
osc1.phase: 25 points, worst deviation 2.10 degrees (tolerance 4); phase 0 fitted at 176.3 degrees
[ RUN  ] filter_keytrack_follows_the_note_octave_per_octave
keytrack -100 %: cutoff ratio one octave up 0.500 (expected 0.500)
keytrack -50 %: cutoff ratio one octave up 0.707 (expected 0.707)
keytrack 0 %: cutoff ratio one octave up 1.000 (expected 1.000)
keytrack 50 %: cutoff ratio one octave up 1.456 (expected 1.414)
keytrack 100 %: cutoff ratio one octave up 2.059 (expected 2.000)
[  OK  ] filter_keytrack_follows_the_note_octave_per_octave
latency: limiter + comp 72 samples, limiter without comp 0, comp without limiter 0 (1.5 ms = 72 samples)
```


## 6. Mutation proof

`scripts/run-param-audit.sh --self-test` (ctest `synthyboy_param_audit_selftest`) injects each fault through `SYNTHYBOY_AUDIT_FAULT` into the value the harness hands to the engine and expects the audit to fail; the faults live in `faultFor ()`
(tools/param-audit/rig.cpp) and are inactive without the environment variable, so there is nothing to revert. Result of the run (2 min 24 s):

| fault | caught by | result |
|---|---|---|
| `cutoff_scaled` (filter1.cutoff x1.5, a 7 semitone error) | `filter_resonance_peak_sits_at_the_displayed_cutoff_and_grows_with_resonance` | caught |
| `pan_swapped` (osc1.pan sign) | `level_pan_law_is_constant_power` | caught |
| `delay_time_2x` (delay line twice as long) | `delay_time_is_the_echo_spacing` | caught |
| `tune_cents` (osc1.tune +2 cents) | `pitch_tune_cents_and_master_transpose_and_tune` | caught |
| `volume_db_as_linear` (master.volume at half the dB) | `level_master_volume_is_one_db_per_db` | caught |
| `env_attack_half` (attack time halved) | `envelope_times_follow_the_display` | caught |
| `chorus_unwired` (the chorus controls do not reach the engine) | stage 1: the chorus keys become DEAD, the report exits 1 | caught |

The self test first checks that the clean run of the test passes.


## 7. Owner questions (docs/ui-design/14-morning-questions.md rows 112 .. 114)

* 112: the envelope sustain follows Vital's law (amplitude = knob squared), Serum 2 shows dB. Kept (Vital).
* 113: the filter cutoff is the pole frequency (resonance peak exactly at the display); the -3 dB point at resonance 0 sits at 0.52 .. 0.63 of it. Kept.
* 114: velocity law (amplitude = velocity squared; -100 % is not the exact inverse). Kept (Vital).

## 8. What could not be verified

* Windows and ARMv7: the tool is built with the rest (`tools/param-audit`, Vital builds only) but was neither run nor compiled on Windows or under QEMU in this session; the CI job `param-audit` is Linux only.
* The own engine (`--engine own`) was not run: the audit covers the Vital-derived engine; the harness takes `--engine own` and skips what `supportsParam` refuses.
* The 42 OPEN keys (question 115): whether they are unwired or only need a state the audit does not know is not decided.
* Instances 2 .. N of a family (osc2/3, lfo2..10, rlfo2..4, env3..6, mod02..64, fxi02..16) were swept at the quick density in the full run, and their verdicts compared with instance 1 (the differences are explained); `--all-instances` sweeps them at 65 points (not run: hours).
* The reverb, the random LFOs and the noise source are not bit repeatable (question 116); plateaus and edge checks in such contexts are marked INFO, and a small effect inside the run to run noise of such a context cannot be proven.
* Sounds were not listened to; the audit measures. `--keep-wavs DIR` writes the audio of any key.
* Where the manuals give no exact number the checks use physics or the measured Vital law (filter -3 dB point, EQ gain at low resonance, reverb RT60, compressor ratios, the spectra of the warps, the granular / spectral / multisample controls are covered by stage 1 only, not by new quantitative curves).
* Time dependent keys (delay, release, hold ... of 2 s and longer) are measured inside a 1 .. 2 s render: values whose effect lies beyond it are reported as INFO plateaus.
* The modulation sweep uses one source (macro 1) and one slot (64); other sources and the per slot RISE / FALL / DELAY are covered by their own tests (test_modslew, test_modamt) and by the key sweeps, not by new quantitative curves.

