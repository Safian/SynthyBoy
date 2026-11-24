# Sound set, batch 3: 24 new presets on the factory wavetables that no preset used

Status: **built and measured in the offline lab, NOT HEARD, NOT MERGED.** Branch `work/sound-batch-3`, made from `feature/mpc-overlay`. Nothing here was listened to. Every statement about sound is an intention that a measurement supports or does not contradict; a preset marked "not audible" in the control table is one where the measure could not see the change, and the ear decides. No project build and no test suite were run for it, only tools that were already built (section 2).

The plan is `docs/release/SOUND-SET-BATCH-3-PLAN.md` (written by a weaker model; every value was checked against `docs/params.json` and every table path against `content/factory-wavetables/`, and section 6 lists where this batch differs from it and why). The set goes from 160 to 184 presets: 4 presets for each of the 6 wavetable categories, every one built on at least one table that none of the first 160 used (7 of the 72 tables are used by batch 2 presets, 65 had no preset; the 24 presets here use 24 of those 65).

Files: `content/presets/<Category>/*.sbpreset` (generated); source `scripts/factory-presets-batch3.py` and `scripts/batch3/*.py` (data written with `scripts/sb_preset_dsl.py`, helpers shared with batch 2); macro names in `scripts/factory-preset-macro-names.json`; levels in `scripts/factory-preset-levels.json`; browser groups in `scripts/factory-preset-categories.json`; measurements in `docs/release/sound-set-batch3-measurements.json`. This document is written by `scripts/gen-sound-set-batch3-doc.py` from the measurements and `scripts/sound-set-batch3-texts.py`.

## 1. The 24 presets

| # | Preset | Category | Browser group | Wavetable | What it is for | What to play |
|---|---|---|---|---|---|---|
| 01 | **Analog Sweep Bass** | Bass | Bass Synth | `saw_filter_sweep.wav` | A punchy analog bass whose filter sweep is in the table. | Roots and fifths C1 to G2. The wheel keeps the sweep open. |
| 02 | **Trance Stack Lead** | Lead | Lead Synth | `saw_stack_spread.wav` | A supersaw lead with the width in the table. | Chords and lines C3 to C5. The wheel opens the spread. |
| 03 | **Sync Sine Screamer** | Lead | Lead Sync+FM | `sync_sine_sweep.wav` | A mono hard sync lead that screams once at every key and then sings. | Lines C3 to G4, legato with a glide. The wheel pushes the sync ratio. |
| 04 | **Sync Saw Raw Bass** | Bass | Bass Synth | `sync_saw_sweep.wav` | A raw hard sync bass with a firm sub. | Notes C1 to G2, legato slides. The wheel and macro 1 walk the sync ratio. |
| 05 | **FM Chain Tine Keys** | Keys | Keys | `fm_chain_123.wav` | An electric piano with a bright tine strike that falls to a mellow body. | Chords C2 to G4. Play hard for a brighter strike. |
| 06 | **PD Reso Acid Bass** | Bass | Bass Acid | `pd_resonance.wav` | An acid line whose squelch is in the oscillator. | Mono lines C1 to C3 with legato slides. Strike hard for a higher resonance peak. |
| 07 | **Step Morph Pluck** | Pluck | Sequence | `step_sequence_morph.wav` | A wave sequencing pluck: every note steps through the patterns of the table. | Short notes and arpeggios C3 to C5. |
| 08 | **West Coast Wavefold** | Lead | Lead Sync+FM | `sine_fold_sweep.wav` | A west coast lead: a sine that the frame axis folds into dense metal. | Mono lines C2 to C5. Wheel and pressure fold it harder. |
| 09 | **Shimmer Bell Walk** | Keys | Bells+Mallets | `bell_pitch_walk.wav` | A bell whose upper partials open with velocity. | Notes and slow chords C3 to G5. Hard keys glitter, soft keys clang. |
| 10 | **Free Bar Vibraphone** | Keys | Bells+Mallets | `free_bar_modes.wav` | Vibraphone bars with a motor tremolo that the wheel switches on. | Melodies and chords F2 to F5. Wheel up is motor on. |
| 11 | **Gong Bloom Pad** | Pad | Pad Evolving | `gong_modes_swell.wav` | A pad that opens like a struck gong: dark hum first, bronze after. | Held chords C2 to G4, two seconds or more. |
| 12 | **Cymbal Cloud Riser** | FX | FX | `cymbal_cloud.wav` | A four second riser from a dull hum to a cymbal cloud, cut at the top. | One note held for five seconds (C3 or any key). |
| 13 | **Wind Sweep Riser** | FX | FX | `noise_band_sweep.wav` | A wind riser: a narrow band whistles up through frozen noise. | One key held for five seconds. Every key sounds the same. |
| 14 | **Air Chiff Flute** | Lead | Lead Synth | `breath_air_bumps.wav` | A breathy flute: a triangle with an air layer from the noise table. | Mono lines C3 to C6, legato. Pressure and wheel add breath. |
| 15 | **Pink Noise Sub** | Bass | Bass Sub+808 | `pink_lowpass_sweep.wav` | A sub bass with a pitched noise thump that closes within a quarter second. | Notes C1 to G2. |
| 16 | **Vinyl Crackle Bed** | FX | Texture | `crackle_pulses.wav` | A bed of dust, crackle and rattle for the back of a track. | Hold any key. Every key sounds the same. |
| 17 | **Acoustic Bore Reed** | Lead | Lead Synth | `bore_odd_to_even.wav` | A reed solo that moves from clarinet to saxophone with pad pressure. | Mono lines C3 to G5, legato. Press harder for the fuller bore. |
| 18 | **Harmonic Wah Bass** | Bass | Bass Synth | `harmonic_band_sweep.wav` | A talking funk bass with a wah in the oscillator. | Lines C1 to C3. |
| 19 | **Cathedral Bell Ring** | Keys | Bells+Mallets | `additive_bell_strike.wav` | A bronze cathedral bell that settles from the strike to the hum. | Notes and slow chords C3 to G5. Hard keys ring brighter. |
| 20 | **Dispersion Phase Pad** | Pad | Pad Evolving | `dispersion_sweep.wav` | A pad whose magnitudes never change and whose waveform does. | Held chords C2 to G4 (they sound an octave lower: master.transpose is minus 12). |
| 21 | **Soprano Vowel Pad** | Pad | Pad Warm | `vowel_cycle_women.wav` | A choir of women walking the vowels. | Chords A2 to A4 (the table is a voice at 220 Hz; the preset sounds an octave below the key). |
| 22 | **Talk Box Baritone** | Lead | Lead Synth | `vowel_cycle_men.wav` | A talk box lead: the wheel is the mouth. | Mono lines C2 to C4 (an octave below the key, men's voice at 130 Hz). |
| 23 | **Choir Swell Ensemble** | Pad | Pad Warm | `choir_blend.wav` | A swelling choir of men, women and children. | Held chords C2 to G4. |
| 24 | **Diphthong Morph Vox** | Lead | Lead Synth | `diphthong_series.wav` | A talking synth lead that says a word on a held key. | Mono lines C2 to C4 (an octave below the key). |

Pad pressure (`Poly Aftertouch` to Filter 1 cutoff) is on the held sounds (basses, leads, pads, textures); the decaying keys, the plucks and the risers have none, as in batch 2. The wheel moves the frame (or the effect named in the preset) in every preset. Six macros are named and wired in each, macros 7 and 8 are empty, names are 14 characters at most.

## 2. How it was measured, and what was not

**No project build, no test suite, no gate script.** The tools of the integration build (`build-int/` of the `sb-mpc-overlay` worktree, built from the same commit) were run read only: `synthyboy_sound_lab`, `synthyboy_tier_bench` and `synthyboy_preset_render` exist there and read `$SYNTHYBOY_CONTENT_DIR`, so the factory wavetables load (nothing was missing). The Python tools of this worktree were used as they are. `scripts/tune-preset-levels.py` was not run.

- **Level** (`scripts/measure-loudness.py`, the method of `docs/release/SOUND-FIXES-1.md`): the loudest 100 ms of the category gesture (Bass single notes C1 to C3, Lead C3 to C5, Pad and Keys triads, Pluck a played line, FX one note for its natural length), K weighted (BS.1770), plus the category offset, set to the target of minus 23 LUFS by `master.volume`. The solved volumes are in `scripts/factory-preset-levels.json` (the file that batches 1 and 2 use; `gen-factory-presets.py` writes them into the presets), measured again after writing them, on the XL profile (`high;voices=16;control=economy`) and on the Key 61 profile (`low`).
- **Peak**: the eight note chord (C2 to G4) at velocity 127, held 1.5 s, raw engine output. The MPC shell adds 18 dB after it (no limiter), so the second figure is the raw one plus 18. The conflict between a common level and a clean peak at that stage is the one described in `docs/release/SOUND-SET-BATCH-1.md` section 2; these presets are in the range of batches 1 and 2.
- **CPU**: `synthyboy_tier_bench --all-presets` on all 184 presets of this worktree, `--config low` (Key 61, 8 notes) and `--config high,voices=16,control=economy` (XL, 16 notes), 4 s measured per run, 3 passes, a chord struck again every second, median of the 99th percentiles of the passes, as percent of the 2.9 ms block, on the Mac (arm64), one core, other work was running (load average about 3). The Mac is 15 to 25 times faster than the devices, so only the comparison inside the same run counts. The rule is that nothing new may be dearer than the dearest existing preset of the same run.
- **Controls** (`scripts/sound-lab.py report`, both profiles): macros 1 to 8 at 0 and 100, the wheel, the pressure ramp, velocity, stereo, click and tail. A control counts as audible when one of the thresholds of `docs/release/SOUND-SET-BATCH-1.md` section 1 is passed.

What was **not** done: no listening. No Hungarian listening guide was written. The pad pressure and the wheel on a real device were not tried. The tools were not rebuilt, so the preset files were checked against the engine as it is in `build-int/` only.

## 3. Numbers

Loudness in LUFS (calibrated fast max, target minus 23.0), XL and Key 61. Peak: 8 note velocity 127 chord, raw dBFS and at the +18 dB stage (XL). CPU: 99th percentile, percent of the block (Key 61 profile / XL profile).

| # | Preset | master.volume dB | Loudness XL | Loudness Key 61 | Peak raw | Peak at +18 | CPU Key 61 | CPU XL |
|---|---|---|---|---|---|---|---|---|
| 01 | **Analog Sweep Bass** | -18.3 | -23.0 | -23.0 | -8.2 | +9.8 | 0.90 | 0.96 |
| 02 | **Trance Stack Lead** | -10.7 | -23.0 | -22.9 | -4.3 | +13.7 | 1.86 | 2.45 |
| 03 | **Sync Sine Screamer** | -15.2 | -23.0 | -23.0 | -19.7 | -1.7 | 0.39 | 0.41 |
| 04 | **Sync Saw Raw Bass** | -18.8 | -23.1 | -23.1 | -16.4 | +1.6 | 0.51 | 0.57 |
| 05 | **FM Chain Tine Keys** | -10.9 | -23.0 | -22.5 | 0.2 | +18.2 | 1.69 | 2.37 |
| 06 | **PD Reso Acid Bass** | -14.3 | -23.0 | -23.0 | -15.9 | +2.1 | 0.76 | 0.82 |
| 07 | **Step Morph Pluck** | -3.2 | -23.0 | -22.7 | -2.0 | +16.0 | 1.11 | 1.60 |
| 08 | **West Coast Wavefold** | -17.9 | -23.0 | -23.5 | -21.1 | -3.1 | 0.28 | 0.72 |
| 09 | **Shimmer Bell Walk** | -11.7 | -23.0 | -22.1 | -3.1 | +14.9 | 1.59 | 2.34 |
| 10 | **Free Bar Vibraphone** | -10.1 | -23.0 | -22.5 | -3.4 | +14.6 | 1.67 | 3.02 |
| 11 | **Gong Bloom Pad** | -10.1 | -23.0 | -22.8 | -9.5 | +8.5 | 1.35 | 1.95 |
| 12 | **Cymbal Cloud Riser** | -6.4 | -23.0 | -22.2 | -46.4 | -28.4 | 0.46 | 0.98 |
| 13 | **Wind Sweep Riser** | -14.6 | -23.0 | -22.3 | -30.2 | -12.2 | 0.65 | 1.15 |
| 14 | **Air Chiff Flute** | -11.8 | -23.0 | -22.3 | -18.3 | -0.3 | 0.30 | 1.08 |
| 15 | **Pink Noise Sub** | -19.2 | -23.0 | -23.0 | -9.8 | +8.2 | 0.90 | 0.88 |
| 16 | **Vinyl Crackle Bed** | -2.9 | -23.1 | -23.3 | -4.9 | +13.1 | 0.58 | 1.05 |
| 17 | **Acoustic Bore Reed** | -10.3 | -23.0 | -22.8 | -17.4 | +0.6 | 0.52 | 1.06 |
| 18 | **Harmonic Wah Bass** | -18.8 | -23.0 | -23.0 | -9.9 | +8.1 | 0.89 | 0.98 |
| 19 | **Cathedral Bell Ring** | -12.6 | -23.0 | -22.0 | -4.7 | +13.3 | 1.35 | 1.80 |
| 20 | **Dispersion Phase Pad** | -14.2 | -23.0 | -22.5 | -7.4 | +10.6 | 1.53 | 1.96 |
| 21 | **Soprano Vowel Pad** | -14.1 | -23.0 | -22.2 | -4.1 | +13.9 | 1.37 | 1.83 |
| 22 | **Talk Box Baritone** | -13.1 | -23.0 | -23.0 | -15.8 | +2.2 | 0.37 | 0.39 |
| 23 | **Choir Swell Ensemble** | -11.8 | -23.0 | -21.9 | -7.2 | +10.8 | 1.48 | 2.04 |
| 24 | **Diphthong Morph Vox** | -12.5 | -23.0 | -22.6 | -14.9 | +3.1 | 0.26 | 0.62 |

**CPU result.** The dearest existing preset of the run is Spectral Vowel Freeze on both profiles: Key 61 11.40 %, XL 12.34 % of the block (99th percentile). The dearest new preset is Trance Stack Lead on the Key 61 profile (1.86 %) and Free Bar Vibraphone on the XL profile (3.02 %). None of the 24 is dearer than the dearest existing preset under either profile; the dearest new ones are about 6 (Key 61) and 4 (XL) times cheaper. No wavetable preset here uses more than 3 unison voices on one oscillator, polyphony is 10 at most.

Level and peak notes. Every preset is within 1.1 dB of the target on the Key 61 profile (that profile drops the reverb and other effects of the high tier, which changes the level a little). The 8 note velocity 127 peaks at the +18 dB stage reach +18.2 dBFS at most (FM Chain Tine Keys); that is the known clip risk of the whole set (batches 1 and 2 reach +16.7), a matter for the output stage and not for the single preset. Cymbal Cloud Riser and Wind Sweep Riser show very low chord peaks because the riser has not started within the 3.5 s of that render; their gesture is the natural length of the note.

## 4. Which controls the measure could hear

| Preset | Macros audible | Wheel | Pressure | Velocity 30 to 127, level change | Click ratio | Tail s |
|---|---|---|---|---|---|---|
| **Analog Sweep Bass** | 1, 2, 3, 4, 6 (not: 5) | yes | yes | 25 dB | 0.9 | 0.13 |
| **Trance Stack Lead** | 1, 3, 4, 5, 6 (not: 2) | yes | no | 25 dB | 0.1 | 3.04 |
| **Sync Sine Screamer** | 1, 2, 3, 4 (not: 5, 6) | yes | yes | 25 dB | 0.8 | 2.26 |
| **Sync Saw Raw Bass** | 1, 2, 3, 4, 5, 6 | yes | yes | 25 dB | 1.4 | 0.12 |
| **FM Chain Tine Keys** | 1, 2, 3, 4, 5, 6 | yes | no | 25 dB | 0.7 | 1.11 |
| **PD Reso Acid Bass** | 1, 2, 3, 4, 5, 6 | yes | yes | 23 dB | 1.4 | 1.88 |
| **Step Morph Pluck** | 1, 2, 3, 4, 5, 6 | yes | no | 25 dB | 0.9 | 1.39 |
| **West Coast Wavefold** | 1, 2, 3, 4, 5 (not: 6) | yes | yes | 25 dB | 0.2 | 2.25 |
| **Shimmer Bell Walk** | 1, 2, 3, 4, 5, 6 | yes | no | 26 dB | 1.0 | 1.45 |
| **Free Bar Vibraphone** | 1, 3, 4, 5, 6 (not: 2) | yes | no | 25 dB | 0.8 | 0.6 |
| **Gong Bloom Pad** | 1, 2, 3, 4, 5, 6 | yes | yes | 25 dB | 0.0 | 4.0 |
| **Cymbal Cloud Riser** | 1, 2, 4, 5, 6 (not: 3) | yes | no | 29 dB | 0.0 | 3.37 |
| **Wind Sweep Riser** | 1, 2, 4, 5, 6 (not: 3) | yes | no | 25 dB | 0.0 | 2.84 |
| **Air Chiff Flute** | 1, 2, 3, 6 (not: 4, 5) | yes | yes | 25 dB | 0.0 | 2.22 |
| **Pink Noise Sub** | 1, 2, 3, 4, 5, 6 | yes | yes | 25 dB | 1.0 | 0.16 |
| **Vinyl Crackle Bed** | 2, 3, 4, 6 (not: 1, 5) | no | yes | 26 dB | 0.0 | 3.41 |
| **Acoustic Bore Reed** | 1, 2, 6 (not: 3, 4, 5) | yes | yes | 25 dB | 0.0 | 2.44 |
| **Harmonic Wah Bass** | 1, 2, 3, 4, 5, 6 | yes | yes | 24 dB | 0.9 | 0.13 |
| **Cathedral Bell Ring** | 1, 2, 3, 4, 5, 6 | yes | no | 25 dB | 1.2 | 2.97 |
| **Dispersion Phase Pad** | 3, 4, 5, 6 (not: 1, 2) | no | no | 24 dB | 0.0 | 4.0 |
| **Soprano Vowel Pad** | 1, 2, 3, 4, 5, 6 | yes | no | 25 dB | 0.0 | 4.0 |
| **Talk Box Baritone** | 1, 2, 3, 6 (not: 4, 5) | yes | yes | 24 dB | 0.0 | 3.01 |
| **Choir Swell Ensemble** | 1, 2, 3, 4, 5, 6 | yes | yes | 25 dB | 0.0 | 4.0 |
| **Diphthong Morph Vox** | 1, 2, 3, 4, 6 (not: 5) | no | yes | 25 dB | 0.0 | 2.63 |

A "no" is not always a fault: it can be a route that the measure cannot see. Known cases: macros on an LFO rate or on the vibrato rate (the measure looks at the spectrum, not at the rate), macros that change a time (decay, attack: the window is fixed), Dispersion Phase Pad (the table changes only phases, so the magnitude spectrum is the same by construction), Vinyl Crackle Bed (density is temporal), the wheel of Diphthong Morph Vox (the stepped LFO already covers the same frames), the Keys and Pluck presets (no pad pressure route by convention, so the pressure measure is 0 there). Macros that were inaudible while I built the set were changed (section 6); the ones that remain are listed above.

## 5. Method of the sound: what was checked by hand

- The wavetable files were read for their real content before the presets were set: frame 0 of `additive_bell_strike` is the strike and the last frame the hum; the cutoff of `pink_lowpass_sweep` falls as the frame goes up; `noise_band_sweep` peaks at the harmonics 3, 18, 73, 167 and 278 on the frames 0, 16, 32, 48 and 63; the first formant of the diphthong table sits at harmonic 5, which puts its voice near 146 Hz (the pack names 130 Hz for the men's cycle, 175 Hz for the choir, 220 Hz for the women's cycle).
- A frozen noise in one table cycle repeats once per cycle and is heard as a pitch. The wind, the crackle and the breath of the flute therefore switch pitch tracking off and fix the cycle (`osc1.pitch_track` 0, `osc1.fixed_pitch`): checked that three keys give the same render.
- A free running LFO (`lfo1.sync_type` Free-run) did not respond to its rate macro in the offline lab, so Step Morph Pluck uses the default restart mode.
- The vowel tables are one cycle at a fixed voice pitch, so their formants are right only near that pitch (about an octave around it). The four vocal presets and the dispersion pad set `master.transpose` to minus 12 (section 6).

## 6. Differences from the plan, and why

General:

1. **Frame values are percent.** `osc1.frame` runs 0 to 100 over the 64 frames. The plan wrote frame values and modulation amounts as if they were frame numbers in some places.
2. **Six macros, not four**, as in batches 1 and 2 (macros 7 and 8 empty). Names are at most 14 characters. Two names of the plan were longer than that (Glissando Speed, Pressure Sense).
3. **A macro cannot route to a source.** The plan's Acoustic Bore Reed macro 3 (Pressure Sense, to `poly_aftertouch`) is not possible. It is replaced.
4. **Pad pressure and the wheel follow the conventions of batch 2.** Pressure is `Poly Aftertouch` to Filter 1 cutoff on the held sounds; the decaying keys and plucks and the risers have none.
5. **Wavetable is a choice name** (`Preset table`), the index 13 of the plan is the same thing.
6. **Level method.** The plan's target of minus 23 LUFS with the category offsets is what was used; the volumes go through the levels file, not as a parameter value written into the generator (that is how batches 1 and 2 do it, and `gen-factory-presets.py` applies it).
7. **Tables the plan called used.** The plan counted 7 used tables. They are used (through `content`) by batch 2 presets; none of the 24 here is among them.
8. **The preset names and the 24 tables are the plan's.** One browser group differs (Choir Swell Ensemble). The parameter values were set again from the real content of the tables and from renders; most numbers differ from the plan.

Per preset:

| # | Preset | Differs from the plan, and why |
|---|---|---|
| 01 | **Analog Sweep Bass** | The engine filter is open (5200 Hz, the plan said 2400) so the sweep of the table is what is heard. An envelope throws the frame up at every note (the plan had only the macro and the wheel). Six macros, not four. Frame values are percent of the table, not frame numbers. |
| 02 | **Trance Stack Lead** | Base frame 30 (the plan said 24, as if it were a frame number). Added Detune and Breath Rate macros. Pad pressure opens the filter (convention of batch 2). |
| 03 | **Sync Sine Screamer** | Added a vibrato that comes in late and the pressure route. The plan's Sub Octave macro is Octave Down. Tube drive and echo as planned. |
| 04 | **Sync Saw Raw Bass** | An envelope also opens the filter a little at the front of the note. Otherwise as planned. |
| 05 | **FM Chain Tine Keys** | Base cutoff 3200 (the plan said 4200): the tone at the low frames is almost a sine and has no highs to filter. Macro 5 is Mellow (closing), because an opening cutoff did nothing measurable. No pad pressure route (a decaying key, as in batch 2). |
| 06 | **PD Reso Acid Bass** | Diode filter and diode clip as planned. Macro 3 drives the filter and the distortion together (the filter drive alone was inaudible), macro 4 has a larger range. |
| 07 | **Step Morph Pluck** | The stepped LFO runs free in Hz and restarts at every key, it is not tempo synced as the plan said. Two reasons: the Step Speed macro needs a free rate, and with the free running mode the rate macro had no effect in the offline lab. A random value per key moves the start of every pluck. Base cutoff 1800 so that the Dark macro is audible. |
| 08 | **West Coast Wavefold** | A vibrato that the pressure brings and an envelope on the fold were added. The Basin reverb and the sine fold are as planned (mix 30, drive 3). |
| 09 | **Shimmer Bell Walk** | An envelope on the frame lets the strike fall back to the hum. Added a Shimmer macro. No pad pressure route (a decaying key). |
| 10 | **Free Bar Vibraphone** | The tremolo depth is on the wheel (the plan had it on always). The sub is 14 (plan 18). Chorus is slow, not 3.2 Hz. Macro 1 is named Mallet Hard (the plan said Bar Hardness). |
| 11 | **Gong Bloom Pad** | As planned. The amp envelope also drives the frame. Phaser depth 20 and a Dark macro were added. |
| 12 | **Cymbal Cloud Riser** | The filter opens with a second envelope. Polyphony 2. The riser is cut at the top (sustain 0), so the natural length is 5.1 s. |
| 13 | **Wind Sweep Riser** | Pitch tracking of the oscillator is off and the cycle is fixed at MIDI note 24. At the plan's C3 the band above the 85th harmonic would be above the audio band and the second half of the sweep would be silent. The key is only the trigger. |
| 14 | **Air Chiff Flute** | The noise table is a layer that does not follow the keys (fixed pitch, MIDI 36), so the air is the same on every note. The plan's Pressure Sense macro cannot exist (a macro cannot route to a source): replaced by Echo and Room macros. |
| 15 | **Pink Noise Sub** | The direction of the plan was wrong: in this table the cutoff falls as the frame goes up, so the note starts low and an envelope with a negative amount (minus 65) starts it bright. Oscillator 48, sub 52 (plan 35 and 55), filter cutoff 3000 (plan 1200) so that the burst is not filtered away. The thump is a tint, not a loud transient (macro 1 moves it by about 1 dB). |
| 16 | **Vinyl Crackle Bed** | The cycle is fixed at MIDI note 9 (about 14 Hz), key independent. The table alone is dull (centroid about 700 Hz), so the noise source adds sharp vinyl ticks. A crackle table repeats once per cycle, so the rate is a pitch: this is a dense crackle and rattle bed, not sparse single ticks. Density is a temporal change that the spectral measure does not see. |
| 17 | **Acoustic Bore Reed** | The plan's Pressure Sense macro cannot exist: replaced by Dark, Vibrato Rate and Grit. The odd to even change of the table is modest in the measure (3.9 dB), but audible by the thresholds. |
| 18 | **Harmonic Wah Bass** | The wah LFO is free in Hz, not tempo synced, so that the Wah Rate macro works. Macro 5 is Dark. |
| 19 | **Cathedral Bell Ring** | The plan started at frame 0 and added a positive envelope: that would move away from the strike. Here the base frame is 62 and the envelope amount is minus 62, so the note starts at the strike (frame 0) and settles on the hum. Velocity amounts are negative for the same reason. |
| 20 | **Dispersion Phase Pad** | The table changes only phases, so the spectral measure sees nothing (macros 1 and 2 and the wheel read as not audible, by construction). Phase is heard only at low pitch and through a nonlinear stage, so a soft clip was added and the pad is kept an octave lower. NOT HEARD: whether the swirl is audible must be judged by ear. |
| 21 | **Soprano Vowel Pad** | master.transpose is minus 12 (the plan had 0), so the standard gestures and the played register sit near the pitch of the table. |
| 22 | **Talk Box Baritone** | master.transpose is minus 12. No sub (the plan had one at 32): it would sit below the range of the voice. |
| 23 | **Choir Swell Ensemble** | Browser group Pad Warm, not Strings+Brass (a voice choir; the existing choirs are in Pad Warm). Second oscillator is Sine to Saw at frame 30 (the plan left the choice open). master.transpose is minus 12. |
| 24 | **Diphthong Morph Vox** | The stepped LFO is free in Hz and restarts at every key. master.transpose is minus 12. The wheel is read as not audible by the measure because the steps already cover the same frames; it moves the word by hand. |

## 7. Registration: where the count 160 is, and what was changed

Changed in this branch (generated or data): `scripts/gen-factory-presets.py` (`BATCH_FILES`), `content/presets/contentpack.json` (generated: 184 files, description counts itself; the pack version stays 1.2.0 as in batches 1 and 2), `scripts/factory-preset-categories.json` (24 entries), `scripts/factory-preset-macro-names.json` (24), `scripts/factory-preset-levels.json` (24), `docs/release/SOUND-SET-CATEGORIES.md` (rebuilt from the categories file: every `.xpl` number after the first insertion moves; the file also had two sections out of step with the categories file, Lead Synth and Pluck, now consistent), `docs/user-guide/factory-sounds.md` (a batch 3 section; that guide still says 136, it was not brought up to 160 by batch 2 either), and two tests that the new presets broke by their own registration: `scripts/tests/test_assign_preset_categories.py` (160 to 184, two places) and `scripts/tests/test_propose_pad_pressure.py` (batch 3 added to the list of batches that carry their own pressure route).

Python tests run once at the end (`python3 -m pytest`, no build): `test_check_presets`, `test_loudness`, `test_name_preset_macros`, `test_assign_preset_categories`, `test_propose_pad_pressure` pass (78 tests). Nine tests of the files below fail only because they pin 160 (`test_docs_mpc`, `test_gen_xpl_presets`, `test_embed_presets` three, `test_gen_mpc_xpl` two, `test_survey_presets`, `test_docs_release`); the same run showed no other failure. `python3 -I scripts/gen-factory-presets.py --check` says up to date (184 presets), `scripts/check-presets.py` 0 errors, `scripts/assign-preset-categories.py --check` 0 errors.

Not changed, to be changed at integration (they say 160 or 161, or 136 / 112 of an earlier state):

| Where | What it says | Kind |
|---|---|---|
| `core/tests/test_macro_names.cpp:297` | `CHECK_EQ (presets, 160)` | C++ test pin |
| `core/tests/test_preset.cpp:302, 311` | all 160 presets against the manifest and the directory scanner | C++ test text |
| `shell-mpc/tests/preset_index_host.cpp:44` | `scanned.size() == 160` | C++ test pin |
| `shell-mpc/accept.sh:56` | comment: all 160 presets | comment |
| `tools/contentpack/tests/test_contentpack.py:191` | files == 160 for the presets pack | Python test pin |
| `scripts/tests/test_assign_preset_categories.py:37, 175` | 160 presets | Python test pin |
| `scripts/tests/test_embed_presets.py:50-149` | 160 entries, `p160_json`, 160 arrays | Python test pin |
| `scripts/tests/test_gen_mpc_xpl.py:308-353` | exactly 160 `.xpl` files, 161 programs | Python test pin |
| `scripts/tests/test_gen_xpl_presets.py:124-133` | 160 factory presets planned, numbers 0 to 159 | Python test pin |
| `scripts/tests/test_survey_presets.py:361-363` | 160 presets, 12720 pairs (160 * 159 / 2); becomes 184 and 16836 | Python test pin |
| `scripts/tests/test_docs_mpc.py:129` | `len(files) == 112 + 48` (the 160 files) | Python test pin |
| `scripts/tests/test_docs_release.py:125-126` | count 160 and the text `160 factory presets` in the changelog draft | Python test pin |
| `README.md:20, 25` | `synthyboy_preset_sanity` renders the 160 presets; pack of 160 presets | doc |
| `docs/RELEASE.md:96` | factory presets pack: 160 presets, 1.5 MB (size grows) | doc |
| `docs/release/CHANGELOG-draft.md:6, 29, 34, 36` | 160 factory presets, 161 programs | doc, the test above reads it |
| `docs/release/DEVICE-CHECKLIST.md:99` | verify 160 presets' loudness and register | doc |
| `docs/user-guide/SynthyBoy-MPC-User-Guide.md:51, 816, 828, 848, 1029` | 160 presets, 161 programs; 136 presets of the library; 136 searched for routes | doc |
| `docs/user-guide/hu/SynthyBoy-MPC-felhasznaloi-kezikonyv.md:49, 796, 808` | 160 presets, 161 programs (and 136 at lines 828, 1034, 1067) | doc, Hungarian |
| `docs/user-guide/hu/mpce-padok.md:179` and `docs/user-guide/mpce-pads.md:177` | of the 136 factory presets 65 carry the pad pressure route | doc: batch 3 adds 17 presets with the route, the count changes (and batch 2 was not added either) |
| `docs/user-guide/factory-sounds.md:5` | 136 production presets | doc, already stale |
| `docs/release/SOUND-SET-CATEGORIES.md:10` | (`000` .. `183`) was updated; the status line is unchanged | doc |
| the generated preset index (`scripts/build-preset-index.py`) | built at package time | left alone, as the brief says |
| `plugin.json` and the two tests that name `factory-presets-1.2.0.zip` | pack version 1.2.0 | a version bump (1.3.0) is an integration decision; batches 1 and 2 did not bump |

## 8. For the integration round

1. Change the pins of section 7 in one pass (code tests and docs), then run the generator check: `python3 -I scripts/gen-factory-presets.py --check` (184 presets), `python3 scripts/check-presets.py` (0 errors, the 79 warnings are those of the earlier presets), `python3 scripts/assign-preset-categories.py --check`, and the Python tests named in section 7. The embedded preset tables (`scripts/embed-presets.py` output) and the `.xpl` set (`scripts/gen-mpc-xpl.py`, `scripts/gen-xpl-presets.py`) are generated at package time and were not regenerated here.
2. Build once, on the merged tree, through the build slot: the core tests that scan the presets (`test_preset`, `test_macro_names`, `synthyboy_preset_sanity` at every tier) and the MPC shell test `preset_index_host`.
3. The `.xpl` numbers move (section 7). Any project of the owner that stored a program number is affected, the names are the key.
4. Listening, in this order: Dispersion Phase Pad (is the swirl audible), Vinyl Crackle Bed (dense rattle or crackle), Pink Noise Sub (is the thump enough), Wind Sweep Riser and Cymbal Cloud Riser (the riser time), the four vocal presets (formants an octave below the key), Step Morph Pluck.
5. Wavetable presets work only with the factory wavetable pack installed; without a file the engine plays a saw (docs/release/SOUND-SET-BATCH-2.md section 7).

