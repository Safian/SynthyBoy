# Audible changes, round 13 (branch `work/opt-audible`)

Two engine changes the owner decided ("javítsd", "igen, ez mehet"): one fixes the Spectral oscillator's Phase Lock, one moves the morph rebuilds of the wavetable oscillator to other instants. This page says what is heard, per preset, with the measurements. Commits: 313cb391 and 2fa23314 (Phase Lock), 58588494 (morph stagger), 22f4a35c (re-levelling).

## 1. Spectral Phase Lock: both halves of every peak region

**What changed.** Phase Lock is identity phase locking: every bin around a spectral peak keeps the phase relation to the peak that it had in the analysis, so a partial's bins stay coherent. The regions are centred on the peaks, with their boundaries at the midpoints between neighbouring peaks. The old code locked only the half of each region right of its peak (and the bins below the first peak). The half left of every later peak kept its own advanced phase: the test found 144363 of 151920 such bins off by up to pi. Now every bin of every region is locked. The rule is in `docs/engine-spec/07-sample-granular-spectral.md` section 6.

**Which presets.** Only presets with a Spectral oscillator and Phase Lock on: **Spectral Gong Pad** and **Spectral Vowel Freeze** (grep `spec.phase_lock` = 1 in `content/presets`). Spectral Chord Morph and Spectral Bowed Glass have Phase Lock off: their renders are bit for bit the same. No other preset has a Spectral oscillator.

**What is heard.** The partials are more focused and less phasey. Before, the two halves of a partial's main lobe drifted against each other, which smeared the partial a little in time and frequency and took some of its energy. Now the lobe adds up coherently:
- *Spectral Vowel Freeze*: about 0.2 to 0.35 LU louder, mostly in the vowel's formant region (the 1 and 2 kHz third-octave bands +1.9 to +2.8 dB). The vowels sound clearer and less "underwater". The fast level flutter of the held chord is a little lower (envelope fluctuation 1.84 -> 1.64 dB on the single note, tier low).
- *Spectral Gong Pad*: about 0.1 to 0.35 LU louder. The inharmonic partials stand out more: the crest factor of the chord is 1.4 to 1.7 dB higher, the peak 1.6 to 1.9 dB higher, and the partials' beating is a little more pronounced (envelope fluctuation 1.01 -> 1.03 dB chord, 0.99 -> 1.11 dB single). There are no new transients or clicks. The band balance hardly moves (mean 0.2 dB, at most +0.8 dB at 794 Hz on the chord).

**Measurements** (`synthyboy_preset_render --tier low|high --chord-wav --wav --content-dir <store>`, 48 kHz; chord = four notes at velocity 1.0 held 1 s, single = one note at velocity 0.8 held 1.5 s; at the OLD master.volume, so that the change itself shows). Columns: loudest 100 ms (K-weighted, BS.1770); peak; crest factor (peak minus RMS); third-octave bands 40 Hz .. 20 kHz (mean absolute difference over the bands within 30 dB of the loudest, and the largest difference); 5 ms envelope fluctuation over the loudest second; null = RMS of the difference relative to the render (the waveform is different because the phases are different).

| render | loudest 100 ms LUFS before / after (diff) | peak dBFS before / after | crest dB before / after | bands mean / max dB | env. fluct. dB before / after | null dB |
|---|---|---|---|---|---|---|
| Vowel Freeze chord low | -14.58 / -14.42 (+0.16) | -7.71 / -7.20 | 14.13 / 14.41 | 0.59 / +2.47 at 2 kHz | 1.49 / 1.45 | -7.3 |
| Vowel Freeze chord high | -14.70 / -14.45 (+0.25) | -7.81 / -7.53 | 14.17 / 14.24 | 0.60 / +2.80 at 2 kHz | 1.48 / 1.43 | -7.2 |
| Vowel Freeze single low | -24.04 / -23.84 (+0.20) | -17.27 / -17.66 | 16.45 / 15.75 | 0.71 / +1.91 at 1 kHz | 1.84 / 1.64 | -1.9 |
| Vowel Freeze single high | -24.39 / -24.14 (+0.25) | -17.02 / -17.19 | 18.59 / 17.96 | 0.78 / +2.19 at 1 kHz | 2.01 / 1.91 | -1.7 |
| Gong Pad chord low | -18.94 / -18.63 (+0.31) | -10.72 / -9.16 | 17.78 / 19.14 | 0.20 / +0.73 at 794 Hz | 1.01 / 1.03 | -2.1 |
| Gong Pad chord high | -19.38 / -19.02 (+0.35) | -11.54 / -9.64 | 17.46 / 19.13 | 0.22 / +0.80 at 794 Hz | 1.03 / 1.07 | -2.3 |
| Gong Pad single low | -25.47 / -25.26 (+0.21) | -17.09 / -15.95 | 19.68 / 20.40 | 0.23 / +1.39 at 630 Hz | 0.99 / 1.11 | -1.2 |
| Gong Pad single high | -26.09 / -25.81 (+0.28) | -17.88 / -17.07 | 20.88 / 21.34 | 0.27 / +1.29 at 630 Hz | 1.05 / 1.11 | -2.0 |

The kernels and the reference code (`--scalar-content`) still agree as before: Gong Pad -96.3 dB (old binary -96.2), Vowel Freeze -123.4 dB (old -123.3), on the single note at tier high. Largest sample difference over peak: 1.7e-5, within `--tolerance 3e-5`.

**Cost.** Only bin 0 and the peaks are advanced now; the lock writes every other bin. Mac, per frame with Phase Lock (`synthyboy_test_spectral`, best of 5 runs): rich tone 5.26 -> 4.84 us, noise 6.60 -> 6.40 us. `tier_bench --config low` on the Mac, mean of the block: Gong Pad 1.21 -> 1.14 %, Vowel Freeze 1.38 -> 1.30 %. Key 61 estimate: 4 to 9 us less per frame (`docs/MPC.md`, round 13).

## 2. Morph rebuild stagger (wavetable oscillator)

**What changed.** A wavetable oscillator with a spectral morph rebuilds its morph cycle every 512 samples at tier low (256 elsewhere) and cross-fades from the old cycle to the new one. All voices of a chord used to rebuild in the same control period. Now each voice keeps the build at its note start, and its later builds are offset by a per-voice slot: one of 8 evenly spaced periods of the rebuild period. The slot comes from the note serial and the oscillator index (`WtOsc::setMorphStagger`). The rebuild period, the cross-fade and the cache are unchanged.

**What is heard.** With a moving morph, a voice's morph steps at other instants: up to one rebuild period earlier (at most 11 ms at tier low, 6 ms at the full rate). The stepping itself is the same, so nothing new is heard. A held morph (nothing modulates the morph amount or the frame) gives the very same samples. The voices of a chord no longer update their morph in lockstep, which nobody can hear at these intervals.

**Which presets.** The presets with a morph: Stretched Glass Choir (osc 1 Harmonic Stretch, moved by a random LFO), Shepard Ascent (osc 1 and 2 Shepard, moved by LFO 1), Slow Motion Waves (osc 1 morph type 1) and Vowel Whistle Lead (osc 1 Formant Scale, a mono lead). The last two render bit for bit the same: their morph does not move in the renders.

| render | loudest 100 ms LUFS before / after | peak | bands mean / max dB | null dB |
|---|---|---|---|---|
| Stretched Glass Choir chord (4 notes, 1 s) low | -17.98 / -17.98 | -10.75 / -10.75 | 0.00 / 0.00 | -79.6 |
| Stretched Glass Choir chord high | -17.95 / -17.95 | -10.22 / -10.22 | 0.00 / 0.00 | -102.7 |
| Stretched Glass Choir 8 notes C2..G4, 8 s, low | -15.17 / -15.17 | -6.70 / -6.70 | 0.00 / 0.00 | -71.9 |
| Stretched Glass Choir 8 notes, 8 s, XL | -14.72 / -14.72 | -6.14 / -6.14 | 0.00 / 0.00 | -93.5 |
| Shepard Ascent chord low | -34.50 / -34.50 | -26.02 / -26.02 | 0.00 / 0.00 | -75.1 |
| Shepard Ascent chord high | -34.40 / -34.40 | -26.12 / -26.12 | 0.00 / 0.00 | -110.7 |
| Shepard Ascent single low / high | -40.90 / -40.90, -40.82 / -40.82 | unchanged | 0.00 / 0.00 | -103.1 / -112.4 |
| Shepard Ascent 8 notes, 8 s, low | -15.59 / -15.59 | -6.48 / -6.48 | 0.00 / 0.00 | -79.7 |
| Shepard Ascent 8 notes, 8 s, XL | -16.14 / -16.14 | -7.02 / -7.02 | 0.00 / 0.00 | -132.9 |
| Slow Motion Waves (all renders), Vowel Whistle Lead (all renders), Stretched Glass Choir single | bit for bit the same | | | |

The bound: on these presets the level, the peak and every third-octave band move by less than 0.005 dB, and the difference signal is at least 72 dB below the render. The 8-note renders were made with `synthyboy_sound_lab` (profile `low` and `high;voices=16;control=economy`, velocity 0.8, held 8 s).

**Cost.** Mac, oscillator test (8 Harmonic Stretch voices, tier low, moving morph): longest block 40.7 -> 13.3 us, median block 3.9 -> 9.3 us. `tier_bench --config low --seconds 10 --repeat 3` on the Mac, median / p99 % of the block: Stretched Glass Choir 1.79 / 3.34 -> 2.12 / 2.30, Shepard Ascent 1.52 / 2.95 -> 1.83 / 2.00. The mean is unchanged. Key 61 estimate (`docs/MPC.md`, round 13): Stretched Glass Choir p99 85.7 % falls by 6 to 14 points and the median rises by about 4.5; Shepard Ascent p99 73.8 % falls by 5 to 12 points and the median rises by about 4.

## 3. Re-levelling

`scripts/measure-loudness.py measure --profile xl` and `--profile low` were run on all 160 presets with the build before and after both changes. `fast_max` is the loudest 100 ms of the category gesture. Only the two Phase Lock presets moved. Shepard Ascent and Stretched Glass Choir moved by less than 0.001 LU, and the other 156 presets measured exactly the same. Corrected the way `docs/release/SOUND-FIXES-1.md` describes: `master.volume` in `scripts/factory-preset-levels.json`, presets regenerated with `python3 -I scripts/gen-factory-presets.py` (only the two presets and their `contentpack.json` hashes changed), then measured again.

| preset | fast_max XL before / after fix / after re-level | fast_max low before / after fix / after re-level | master.volume old -> new | chord8 peak XL before -> now |
|---|---|---|---|---|
| Spectral Gong Pad | -21.220 / -21.121 (+0.099) / -21.221 (-0.001) | -20.944 / -20.873 (+0.071) / -20.973 (-0.029) | -1.8 -> -1.9 dB | -7.23 -> -6.48 dBFS |
| Spectral Vowel Freeze | -21.326 / -20.994 (+0.332) / -21.294 (+0.032) | -21.158 / -20.900 (+0.258) / -21.200 (-0.042) | -11.7 -> -12.0 dB | -5.66 -> -7.15 dBFS |
| Shepard Ascent, Stretched Glass Choir | unchanged (< 0.001) | unchanged | not touched | unchanged |

## 4. To check on the device

- Key 61, the p99 effect of the stagger and the cost of the lock: `synthyboy_tier_bench --presets <dir> --core 3 --config low --load "Stretched Glass Choir" --load "Shepard Ascent" --load "Slow Motion Waves" --load "Vowel Whistle Lead" --load "Spectral Gong Pad" --load "Spectral Vowel Freeze" --seconds 10 --repeat 3` with this build and with the round 12 build (8e5cf113), interleaved; then the same with `--sections` (a `-DSYNTHYBOY_SECTION_TIMING=ON` build). Expected: the section "osc: morph cycle build" has the same mean and a lower maximum, the median of the morph presets is about 4 points higher and the p99 lower, and the "sample / granular / spectral" section of the two Phase Lock presets is a little lower.
- XL: the same loads with `--core 6 --config "high,voices=16,control=economy"`.
