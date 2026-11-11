# REFCOMPARE - the imported Serum 2 presets against the real Serum 2.1.5

Owner request: "get reference outputs as WAV for the presets, and after loading the preset check that our waveform is the same as in Serum" and "fix them too". Everything here was measured against the Serum 2.1.5 plug-in installed on the Mac (VST3), offline, 48 kHz, 32 bit float stereo, blocks of 512. No Serum audio and none of the 656 presets is committed (`tests/serum-reference/` and `tests/serum-presets/` are gitignored); the committed numbers are `docs/ui-design/refcompare-baseline.json` (score and class per render) and `docs/ui-design/refcompare-variants.json` (the calibration). Entry point: `scripts/run-ref-compare.sh` (usage in its header). Assumptions A488 .. A500 are in `13-new-keys.md` 4.46 (round 1) and A501 .. A507 in 4.47 (round 2, section 9), owner questions 117 .. 122 and 123 .. 128 in `14-morning-questions.md`.

## 1. Result

Renders: 956 (656 presets with the C3 note, 300 with a second pass: C1 for the basses, C5 for leads / plucks / chords / bells / arps). The same scoring (`tools/ref-compare/scoring.json`) was applied to the importer as it was (before) and as it is now (after), both with the content (tables, samples, instruments) loaded.

| class | before | after |
|---|---:|---:|
| MATCH | 0 | 1 |
| CLOSE | 33 | 64 |
| DIFFERENT | 916 | 884 |
| FAILED | 7 | 7 |

Mean score 34.9 -> 40.4, median 33.8 -> 39.8 (0 .. 100).

FAILED renders are the presets whose Serum render is silent with the note script (reference_silent 7): the seven drum kits, whose key zones do not contain C3 (MIDI 60).

The baseline and these tables are the default run (global floors). With the second Serum render of every preset as a per-preset floor (`--ref2-dir`, random presets are not punished for Serum's own noise) the same renders give MATCH 2 -> 2, CLOSE 38 -> 71, DIFFERENT 909 -> 876, mean 35.3 -> 40.8.

| score | before | after |
|---|---:|---:|
| 0-9 | 27 | 16 |
| 10-19 | 137 | 87 |
| 20-29 | 217 | 161 |
| 30-39 | 232 | 215 |
| 40-49 | 184 | 204 |
| 50-59 | 89 | 147 |
| 60-69 | 53 | 89 |
| 70-79 | 9 | 26 |
| 80-89 | 1 | 3 |
| 90-100 | 0 | 1 |

Median distance per metric over all renders (smaller is closer; the unit is in the name):

| metric | before | after |
|---|---:|---:|
| loudness difference (LU) | 12.57 | 7.47 |
| peak difference (dB) | 10.38 | 6.32 |
| log-mel spectrogram (dB) | 17.35 | 15.73 |
| multi-resolution STFT loss | 1.58 | 1.71 |
| 10 ms RMS envelope (dB) | 15.01 | 10.73 |
| attack level (dB) | 12.35 | 7.03 |
| sustain level (dB) | 12.58 | 7.73 |
| release level (dB) | 13.26 | 8.74 |
| spectral centroid (octaves) | 0.78 | 0.75 |
| spectral rolloff (octaves) | 0.97 | 0.95 |
| fundamental of tonal presets (cents) | 13.05 | 12.05 |
| side / mid (dB) | 3.57 | 3.46 |
| tail to -60 dB (s) | 0.14 | 0.06 |

Mean sub-score of a feature family (0 .. 100):

| family | before | after |
|---|---:|---:|
| spectrum | 22.5 | 22.7 |
| envelope | 36.5 | 46.3 |
| loudness | 37.6 | 48.6 |
| pitch | 50.1 | 50.5 |
| stereo | 69.4 | 70.1 |

The fixes moved the loudness, the envelopes and the levels (median loudness difference 12.6 -> 7.5 LU, envelope 15.0 -> 10.7 dB); the spectra did not move (the spectrum family 22.5 -> 22.7, the median STFT loss 1.58 -> 1.71): what keeps the spectra apart is listed in section 6, not a level or a scale.

Mean score per category (the folder below Factory, User = the owner folder):

| category | n | before | after |
|---|---:|---:|---:|
| Arp | 36 | 18.1 | 21.5 |
| Bass | 228 | 36.5 | 43.3 |
| Bell | 10 | 26.6 | 34.5 |
| Brass | 12 | 36.0 | 44.2 |
| Chord | 18 | 34.2 | 35.6 |
| Drum | 19 | 36.3 | 43.4 |
| Drumkit | 1 | 43.3 | 25.9 |
| E Piano | 15 | 35.3 | 37.6 |
| Guitar | 20 | 41.7 | 51.3 |
| Hit | 4 | 22.3 | 34.8 |
| Hoover | 2 | 39.1 | 55.9 |
| Instrument | 3 | 40.9 | 49.7 |
| Keyboard | 22 | 33.4 | 39.7 |
| Lead | 108 | 37.5 | 41.1 |
| Loop | 18 | 25.8 | 32.8 |
| Mallet | 14 | 35.1 | 46.5 |
| Orchestral | 8 | 24.3 | 36.2 |
| Organ | 17 | 37.0 | 48.6 |
| Pad | 78 | 38.3 | 44.6 |
| Piano | 12 | 38.2 | 37.1 |
| Pluck | 94 | 35.4 | 37.6 |
| SFX | 36 | 27.1 | 41.3 |
| Seq | 21 | 23.9 | 29.0 |
| Soundscape | 13 | 29.8 | 32.9 |
| String | 15 | 33.3 | 38.3 |
| Synth | 48 | 36.8 | 41.6 |
| Template | 4 | 34.5 | 53.1 |
| User | 57 | 37.7 | 37.5 |
| Vox | 12 | 42.0 | 45.7 |
| Woodwind | 4 | 34.1 | 39.9 |

## 2. Loading a preset into Serum 2 (tools/serum-reference)

The plug-in has 128 programs ("Prog 1" ..), 2623 parameters and no file-open API without its editor. What works is the plug-in state: `getStateInformation` returns a JUCE VST3 wrapper (`VC2!`, a size, an XML text with the base64 of the IComponent and the IEditController state); the IComponent state is an XferJson container (header member `component: processor`) whose body is the same CBOR map as a preset file. Route (a) with the raw file bytes does nothing; with the file wrapped as a state it is refused while the preset body carries the interface members (Osc, WTOsc, Filter, SerumGUI, preset name ..). The working route: take the members of the plug-in's own processor state, put in the preset's value for each (the plug-in's own value where the preset has none), pack the body as a zstd frame of raw blocks, write the MD5 of that into the header and put the container into the wrapper of the state the plug-in reported. The plug-in loads headless, needs no licence dialog, reports a latency of 0 samples and renders much faster than real time (a 4 s note in 0.02 .. 0.7 s, the load of the plug-in 1.3 s). The host is a JUCE console program (`synthyboy_serum_reference`, macOS only, not built elsewhere), the batch runner (`render_all.py`) starts one process per render with a timeout, runs at nice 19, skips what has a sidecar with the same preset hash, and records a failure per render (`.fail.json`) instead of stopping. 956 renders took 22 minutes; 2 failed once through an exit race of the first host version (fixed, retried).

## 3. Noise floor (Serum against Serum)

All 949 renders were made twice. **842 (88.7 %) are bit identical**; the others differ in the random start phase / random modulation / unison, and that distance is the tolerance floor of every metric: the 95th percentile of each distance is the floor `scoring.json` subtracts before a distance costs points (and for a preset that has two reference renders, its own distance when larger, `--ref2-dir`).

| metric | median | 95th percentile (floor) | 99th | max |
|---|---:|---:|---:|---:|
| d_mel | 0.000 | 1.309 | 4.498 | 30.21 |
| d_stft | 0.000 | 0.141 | 0.717 | 90.97 |
| d_env_corr | 0.000 | 0.004 | 0.109 | 0.81 |
| d_env_db | 0.000 | 0.799 | 4.316 | 46.09 |
| d_lufs | 0.000 | 0.196 | 1.551 | 75.61 |
| d_peak | 0.000 | 0.143 | 1.164 | 62.19 |
| d_centroid | 0.000 | 0.050 | 0.375 | 2.05 |
| d_rolloff | 0.000 | 0.057 | 0.599 | 2.27 |
| d_f0_cents | 0.000 | 0.726 | 4.250 | 600.00 |
| d_sidemid | 0.000 | 0.207 | 3.353 | 20.57 |
| d_tail | 0.000 | 0.000 | 0.271 | 1.44 |

## 4. The score

Per render, after the time alignment (the first transient, cross-correlation of the 1 ms envelopes, reported; most renders have offset 0): (1) 10 ms RMS envelope correlation and per segment level (attack 0 .. 0.25 s, sustain, release); (2) log-mel (80 bands, 2048 / 512) and multi-resolution STFT (512, 1024, 2048: spectral convergence + log magnitude); (3) spectral centroid and 85 % rolloff trajectories; (4) fundamental in cents for tonal presets (an octave counts as 600 cents); (5) L/R balance, side/mid, inter-channel correlation; (6) integrated loudness (BS.1770-4 LUFS) and peak; (7) the quietest 50 ms of the release; (8) tail to -60 dB. Each distance becomes a sub-score `100 * clip(1 - max(0, d - floor) / span)`, the score is the weighted mean (weights in `scoring.json`: spectrum 40, envelope and segments 31, loudness 15, fundamental 16, stereo 10, others; applicable features only). MATCH >= 85, CLOSE >= 65.

**Calibration.** The spans were set so that deliberately broken versions of our own render score clearly lower: 5 crafted presets whose render matches Serum (scores 85, 86, 89, 93, 96) and 4 breaks of each (50 cents sharp, oscillator A off, cutoff three octaves down, 6 dB down) = 20 variants. The smallest drop is **12.4 points**, none of the 20 is a MATCH, the drops of the four breaks have the medians tune50 14, osc_off 89, cutoff 24, level6 24 (`catalog.py --group calib`, `docs/ui-design/refcompare-variants.json`, exit 1 of `run-ref-compare.sh --calibrate` if one is not lower by 10 points). The same checks on synthetic signals are the ctest `synthyboy_ref_compare_metrics`.

## 5. What was found and fixed

Found with controlled single-parameter experiments (a crafted preset = the init state of the plug-in plus one edit, rendered by Serum and by us, fitted: `tools/ref-compare/experiment.py`). Every fix has an assumption id (13-new-keys.md 4.46) and importer tests (`synthyboy_test_serum_import`, 35 cases).

- Levels (A488, A489): the file values of the master, oscillator, sub and noise volume are linear gains (the plug-in shows their square root); defaults master 0.25, oscillator 0.5625; the earlier importer assumed percent / our defaults, so every preset was 6 .. 10 dB off. Serum does not scale the level with the velocity (a velocity 64 note was 8 dB too quiet).
- Defaults of absent parameters (A490): "default" blocks and empty maps took our defaults instead of Serum's (envelope 1 attack 1 ms, release 15 ms, MG Low 12 filter, cutoff 0.5).
- Filter (A491, A492): the cutoff scale is 2 semitones above the 135 note law; the drive is mostly a louder output (2 .. 13 dB off at drive 50 .. 100).
- Envelopes (A493): the attack curve had the wrong sign; the curve knobs map through a measured table (the envelope catalog score 85.5 -> 95.6).
- Wavetable position (A494): position / 256, not (position - 1) / 255 (a 19 dB spectral difference on 9 frame tables).
- Compressor threshold (A495): it is not a linear level; the measured ceiling is mapped (the error went from 40 dB to 2 .. 10 dB).
- Unison level (A496): it follows the blend and the parity of the voice count in Serum; corrected.
- Content (A497): the first comparison rendered without the stored tables / samples (47 silent presets); the render tool now uses the import's content store.

## 6. What could not be matched, and why (measured)

- **Effects.** The effect modules differ by 3 .. 18 LU on single module presets (catalog below). Only the compressor threshold was fitted; reverb, delay, chorus, flanger, phaser, hyper / dimension, Bode, convolve and the distortion types still use the earlier parameter laws (question 121). The voice alone ("nofx" set, effects removed in both) scored 48.9 in the first run against 40.2 with the effects: the effects are 9 points of the gap.
- **EQ.** The Serum EQ has two bands, each Peak / High pass or Low pass / Shelf; ours has one peak band. Two peaks (most of the 738 EQ modules) end as a peak and a shelf; the high / low pass has another resonance law (7 .. 8 dB off at resonance 0 .. 20) (question 118).
- **Unison.** The level is corrected, the spectra are not: at low blend Serum silences the outer voices, ours does not (mel distance 3 .. 14 dB) (question 119).
- **Compressor depth.** Our compressor stays 3 .. 10 dB short of Serum's reduction above threshold 0.6 (19 % of the compressor modules) (question 120).
- **Modulation timing and depth.** The amplitude envelope and the modulating envelopes are fitted (A493), the swept filter is not exact: with an envelope on the cutoff the centroid is 1.2 .. 1.7 times too bright in the first frames at 25 .. 50 % depth and, at 100 %, 220 Hz too high 0.6 s after the note on; a negative cutoff depth (-50 %) reaches only 220 Hz where Serum closes to 22 Hz. The law of a modulation of the oscillator volume differs too (an envelope at 100 % on an oscillator volume of 0 settles at sustain 0.2 where Serum is 55 dB under its peak and ours 27 dB) and is not fitted.
- **Randomness.** Serum itself differs between two renders for 11.3 % of the presets; the scores of those presets have that noise built in (the floor above).
- **Spectra of complex presets.** After the loudness is matched the median log-mel distance is still 13 dB (15 dB before): effects, warp modes (ring-mod, self-PD, linear fold and sync 15 .. 30 points under the catalog mean of 79), sample / granular / spectral oscillators and the modulation matrix together; these are not single causes.

## 7. Module catalog (single module presets, scored like the presets)

| group | cases | mean score | MATCH | CLOSE | DIFFERENT |
|---|---:|---:|---:|---:|---:|
| filters | 38 | 74.3 | 10 | 22 | 6 |
| warps | 44 | 78.7 | 13 | 25 | 6 |
| fx | 78 | 69.4 | 18 | 31 | 29 |
| unison | 15 | 79.1 | 2 | 13 | 0 |
| env | 5 | 96.1 | 5 | 0 | 0 |
| osc | 21 | 89.7 | 19 | 2 | 0 |

The effect cases are real effect entries of the presets (realistic units), the others crafted values; `run-ref-compare.sh --catalog GROUP` reproduces the table.

## 8. Files

- `tools/serum-reference/` host (C++/JUCE) and `render_all.py`; `tools/ref-compare/` `ref_render.cpp` (our offline render), `ref_compare.py`, `metrics.py`, `score.py`, `scoring.json`, `report.py`, `experiment.py`, `catalog.py`, `ablate.py`, `make_doc.py`, `test_metrics.py`; round 2: `fit_filter.py`, `make_filter_laws.py` (tables `core/serum-import/serum_filter_laws.inc`, `serum_filter_drive_laws.inc` from `refcompare-filter-laws.json`), `fit_unison.py`, `make_doc_round2.py`; `scripts/run-ref-compare.sh`.
- Reports of a run: `tests/serum-reference/report/<timestamp>/` (report.md, results.csv / json, summary.json, `ab/` with the reference and our render of the worst 30 and 20 random renders, `.ab.wav` = reference, 1 s gap, ours; `.lr.wav` = reference left, ours right).


## 9. Round 2 (branch refcmp-a): level laws, the unison law of Serum as an engine capability

Same scoring, same 956 renders, the Serum references of round 1 (not rendered again). Tip: 775ba09.

| | before | after |
|---|---:|---:|
| MATCH | 1 | 2 |
| CLOSE | 64 | 85 |
| DIFFERENT | 884 | 862 |
| FAILED | 7 | 7 |
| mean score | 40.35 | 42.94 |
| median score | 39.81 | 42.80 |
| median loudness gap ours - Serum (LU) | -3.84 | -1.45 |
| mean absolute loudness gap (LU) | 9.62 | 7.75 |
| renders more than 6 LU off | 561 of 945 | 467 of 936 |
| median log-mel distance (dB) | 15.73 | 15.06 |
| median multi-resolution STFT loss | 1.71 | 1.70 |
| median 10 ms envelope distance (dB) | 10.73 | 9.82 |
| median spectral centroid distance (octaves) | 0.75 | 0.74 |

| family (mean sub-score) | before | after |
|---|---:|---:|
| spectrum | 22.7 | 23.7 |
| envelope | 46.3 | 49.9 |
| loudness | 48.6 | 52.4 |
| pitch | 50.5 | 53.3 |
| stereo | 70.1 | 70.7 |

### 9.1 What was found (measured on the real plug-in, controlled single parameter experiments; assumptions A501 .. A507 in `13-new-keys.md` 4.47, questions 123 .. 128)

- **The 211 renders that got worse in round 1 were not the master level.** A master sweep 0.02 .. 1 on a crafted preset agrees with the plug-in within 0.1 dB above 0.1 (the linear gain law of A488 is right). They came from the same level law that was assumed wrongly in the rows of the filter and of every effect: the level knob `kParamLevelOut` is the gain **(2 v) ^ 2** in Serum (default 0.5 = unity, 1 = +12.04 dB), the importer had 20 log10 v (6 dB too quiet at the default value, 12 dB at 1): the presets with a high master (or any value of the knob above its default) came out too loud after the round 1 master fix, the others too quiet. Fixed for the voice filters and the 12 effect rows (A507). The DIRECT and BUS level knobs are linear gain x 2 (default 0.5) and the bus sends are cubic (a send of 50 % adds 1.02 dB), A502.
- **A measurement artefact on every first note.** The offline render of our engine heard the smoothers ramp from the defaults to the loaded values for about 100 ms (a 25 dB thump on a closed filter that Serum, rendered after eight silent blocks, does not have). `synthyboy_ref_render` calls `engine->reset()` after the load (A504); every earlier first-note comparison was affected.
- **Unison (A503).** The voices of Serum are evenly spaced, the outer ones at 200 x d ^ 1.25 cents (d = 0.25 by default); the level of the centre voice (one for an odd count, two for an even count) and of the outer voices follows the blend by two shapes that are the same for 3 .. 16 voices times a gain per voice count (0.4384 at 3, 0.3016 at 16, 0.32 each at 2); the stereo spread moves a voice into its own channel with the measured gains (2.52 at 100 %) and does not move or change the centre voice. Ours had the Vital law (the centre voice 6 dB too loud at high blend for odd counts, the voices not evenly spaced). This is now the engine capability `oscN.unison.law = Serum` (key block B33, `kCap2SerumUnison`, off / on proof and the measured levels in `core/engine-vital/tests/test_serum_unison.cpp`); the importer sets it and the linear detune; the level correction of A496 is gone. The experiments: `tools/ref-compare/fit_unison.py --what levels|pan|detune`.
- **Filters (A501).** The resonance of the state variable types (L / H / B 12 .. 24) takes level (-6.5 dB at 30, -13 dB at 100: the 7 .. 8 dB difference at resonance 0 .. 20 for HP / LP in the earlier note was this and the first note artefact), the ladders keep their level with the resonance and get louder with the drive, DirtyMg gets quieter; `tools/ref-compare/fit_filter.py` measures a type on a grid, `make_filter_laws.py` writes the tables. The shapes of the MG and state variable types match (residual 0.2 .. 1.5 dB over 40 harmonics). LadderEMS and HP12 stay uncorrected (the static law made the real presets worse, question 123).
- **Multisample instruments (A505):** 10.9 dB quieter than Serum at the same oscillator volume (30 instruments, spread 1.6 dB); the importer adds it to the zones.
- **Checked and fine:** the master level law, the envelope sustain law (the amplitude squared in both), the filter cutoff scale and the modulation of the cutoff by an envelope (+100 / +50 / -50 % at the centroid within 5 %), unison detune of a single voice, the bus level law after the fix.

### 9.2 What remains and why

- **Effect chains.** Single effect modules are within 3 dB (catalog), but chains (two compressors and two EQs: 10 dB short; flanger, phaser, bode, utility, convolve, splitters have the largest per module level gaps) leave a median loudness bias and a wide scatter (mean absolute gap 7.8 LU); the effects round (questions 118, 120, 121, 124).
- **Arpeggiator and clips (34 presets, question 127):** not modelled, the median loudness gap of those presets is about -9 LU.
- **Sample, granular and spectral oscillators (question 125),** the start thump and the cutoff floor of a closed filter (question 126), the static filter law of LadderEMS / HP12 (question 123), warp modes (FM from the sub, tube distortion: 5 .. 10 LU) and the modulation matrix timing.
- **No gate was lowered, no preset was special cased**; the scoring and the thresholds are those of round 1.
