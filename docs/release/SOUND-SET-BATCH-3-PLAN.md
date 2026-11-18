# Sound Set Batch 3 Plan: 72-Wavetable Factory Content Integration

Status: **Proposed sound-set plan (document only; no preset files, no script changes).**  
Author: DeepMind Antigravity Agent  
Reference: Task `g45-batch3-plan`, `docs/release/WAVETABLE-PACK.md`, `docs/user-guide/factory-content.md`, `docs/release/SOUND-SET-BATCH-1.md`, `docs/release/SOUND-SET-BATCH-2.md`, `docs/release/SOUND-SET-CATEGORIES.md`, `docs/release/SOUND-FIXES-1.md`, and `docs/params.json`.

---

## Executive Summary

The SynthyBoy factory content pack provides **72 custom wavetables** organized across six distinct categories: `Analog`, `Digital`, `Metallic`, `Noise`, `Spectral`, and `Vocal` (12 wavetables each), synthesized mathematically from scratch under Creative Commons Zero v1.0 Universal (`CC0-1.0`) licensing (`docs/release/WAVETABLE-PACK.md`, `docs/user-guide/factory-content.md`).

A comprehensive grep survey across all 160 existing factory presets (`content/presets/**/*.sbpreset`) and generator scripts (`scripts/gen-factory-presets.py`, `scripts/factory-presets-batch1.py`, `scripts/factory-presets-batch2.py`, and `scripts/batch2/*.py`) reveals that **only 7 of the 72 wavetables are currently utilized** across 8 preset patches. The remaining **65 wavetables have zero preset references** in the codebase. Two entire categories—`Metallic` (12 tables) and `Noise` (12 tables)—have zero presets utilizing them.

This document presents the detailed engineering and sound design plan for **Sound Set Batch 3**: **24 new production presets** (4 presets per wavetable category), increasing the factory library from 160 to 184 presets. Every proposal from `docs/release/WAVETABLE-PACK.md` is fully specified with exact parameter keys from `docs/params.json`, official Akai MPC Sounds browser groups from `docs/release/SOUND-SET-CATEGORIES.md`, four hardware macro mappings, playing registers, and complete generator implementation checklists.

---

## 1. Survey of the 72 Factory Wavetables

### 1.1 Methodology

Every table file name from `content/factory-wavetables/` was searched using full path specifiers (`<Category>/<name>.wav`), file basenames (`<name>.wav`), and symbol identifiers (`"<name>"`) across:
1. **`content/presets/`**: All 160 factory `.sbpreset` JSON containers (`Bass/` 30, `Lead/` 32, `Pad/` 32, `Pluck/` 22, `Keys/` 25, `FX/` 19).
2. **Preset Generator Scripts**: `scripts/gen-factory-presets.py`, `scripts/factory-presets-batch1.py`, `scripts/factory-presets-batch2.py`, and `scripts/batch2/*.py` (`tables.py`, `sample.py`, `granular.py`, `spectral.py`, `multisample.py`).

### 1.2 Summary of Usage Statistics

| Wavetable Category | Total Tables | Active Tables | Unused Tables | Presets Using Category |
|---|---|---|---|---|
| **Analog** | 12 | 1 | 11 | 1 (`Reese Dual Phase Bass`) |
| **Digital** | 12 | 2 | 10 | 2 (`Ladder FM Bass`, `Dusty Rhodes Keys`) |
| **Metallic** | 12 | 0 | 12 | **0 (None)** |
| **Noise** | 12 | 0 | 12 | **0 (None)** |
| **Spectral** | 12 | 2 | 10 | 2 (`Drawbar Organ Keys`, `Spectral Bowed Glass`) |
| **Vocal** | 12 | 2 | 10 | 3 (`Vowel Table Lead`, `Vowel Scrub Choir`, `Throat Singer Drone`) |
| **Total** | **72** | **7** | **65** | **8 preset references** |

*Note*: `Vocal/formant_sweep_aeiou.wav` is used in two presets (`Vowel Table Lead` and `Vowel Scrub Choir`), which accounts for 8 preset references across 7 active tables.

---

### 1.3 Active Wavetables (7 Tables in Use)

| # | Category | Table Title | File Path | Preset(s) Using Asset | Generator Script Reference |
|---|---|---|---|---|---|
| 1 | **Analog** | PWM Dual Phase | `Analog/pwm_dual_phase.wav` | `content/presets/Bass/reese_dual_phase_bass.sbpreset` | `scripts/batch2/tables.py:30` |
| 2 | **Digital** | FM Index Sweep | `Digital/fm_index_sweep.wav` | `content/presets/Keys/dusty_rhodes_keys.sbpreset` | `scripts/batch2/sample.py:16` |
| 3 | **Digital** | FM Ratio Ladder | `Digital/fm_ratio_ladder.wav` | `content/presets/Bass/ladder_fm_bass.sbpreset` | `scripts/batch2/tables.py:147` |
| 4 | **Spectral** | Glass Harmonica | `Spectral/additive_glass_harmonica.wav` | `content/presets/Lead/spectral_bowed_glass.sbpreset` | `scripts/batch2/spectral.py:44` |
| 5 | **Spectral** | Organ Drawbars | `Spectral/organ_drawbars_16.wav` | `content/presets/Keys/drawbar_organ_keys.sbpreset` | `scripts/batch2/tables.py:92` |
| 6 | **Vocal** | Vowel Sweep AEIOU | `Vocal/formant_sweep_aeiou.wav` | `content/presets/Lead/vowel_table_lead.sbpreset`<br>`content/presets/Lead/vowel_scrub_choir.sbpreset` | `scripts/batch2/tables.py:58`<br>`scripts/batch2/granular.py:53` |
| 7 | **Vocal** | Overtone Throat Sweep | `Vocal/overtone_throat_sweep.wav` | `content/presets/Pad/throat_singer_drone.sbpreset` | `scripts/batch2/tables.py:120` |

---

### 1.4 Unused Wavetables Catalogue (65 Tables)

The following 65 wavetables return **0 matches** in `content/presets/` and all generator scripts. Descriptions of their scan behaviour are drawn verbatim from `docs/user-guide/factory-content.md` (lines 55–236) and `docs/release/WAVETABLE-PACK.md` (lines 24–95).

#### Analog (11 Unused Tables)

| Table Title | File Path | Frames | What the Scan Does | Grep Result |
|---|---|---|---|---|
| **Octave Saw Stack** | `Analog/octave_saw_stack.wav` | 64 | A saw to which a saw an octave up and one two octaves up are added (the 2x weight 0 to 1, the 4x weight 0 to 0.8): the layered octaves of an organ-like analog stack; transpose down for a sub-octave blend. | 0 hits |
| **Pulse Width Sweep** | `Analog/pulse_width_sweep.wav` | 64 | A plain pulse whose width narrows from 50 % (square) to 5 % (thin reedy pulse). | 0 hits |
| **PWM Saw Blend** | `Analog/pwm_saw_blend.wav` | 64 | A pulse whose width sweeps 50 % to 4 % while a saw fades in under it: the width modulated saw of string machines. | 0 hits |
| **PWM Triple Stack** | `Analog/pwm_triple_stack.wav` | 64 | Three pulse waves stacked, their widths walking apart from 50 % towards 6 % and 94 %: a PWM unison in one table. | 0 hits |
| **Saw Filter Sweep** | `Analog/saw_filter_sweep.wav` | 64 | A saw through a resonant 4 pole low pass: the cutoff opens from the 2nd to the 150th harmonic (slow start, fast end), resonance peak riding on it. | 0 hits |
| **Saw Soft Clip** | `Analog/saw_soft_clip.wav` | 64 | A saw driven into a tanh soft clipper, the drive rising 0.5 to 14: saw to a rounded square-like overdrive. | 0 hits |
| **Saw Stack Spread** | `Analog/saw_stack_spread.wav` | 64 | Seven saws whose phases fan out from together to +-0.45 cycle: a single saw thins into a staircase and a comb, the supersaw look frozen in one cycle. | 0 hits |
| **Sync Saw Sweep** | `Analog/sync_saw_sweep.wav` | 64 | Hard sync with a saw slave, ratio 1 to 7: a rougher, brighter sweep than the sine slave. | 0 hits |
| **Sync Sine Sweep** | `Analog/sync_sine_sweep.wav` | 64 | Hard sync: a sine slave resets every cycle and its ratio rises 1 to 9; the classic searing formant sweep. | 0 hits |
| **Triangle Fold** | `Analog/triangle_fold.wav` | 64 | A triangle through a folder, the gain 1 to 7.5: triangle, then folded humps appear (the west coast fold on a smooth source). | 0 hits |
| **Triangle To Saw** | `Analog/triangle_to_saw.wav` | 64 | A cross fade, level matched, from a pure triangle (frame 0) to a saw (frame 63): the odd-only soft tone grows the even partials. | 0 hits |

#### Digital (10 Unused Tables)

| Table Title | File Path | Frames | What the Scan Does | Grep Result |
|---|---|---|---|---|
| **Bit Reduced Sine** | `Digital/bit_reduced_sine.wav` | 64 | A sine with a little saw, quantised to fewer and fewer bits (about 10 down to 1.8): smooth tone to a stepped, gritty one. | 0 hits |
| **FM Chain 1-2-3** | `Digital/fm_chain_123.wav` | 64 | A three operator chain: carrier 1, modulator 2 (index 0 to 3) itself modulated by an operator at 3 (index 0 to 2.5): the sweep passes through brass-like and bell-like colours. | 0 hits |
| **FM Feedback Saw** | `Digital/fm_feedback_saw.wav` | 64 | One operator with feedback 0 to 1.9: sine, saw, noisy saw. | 0 hits |
| **FM 3 To 1 Index** | `Digital/fm_triple_index.wav` | 64 | FM with the modulator at 3 times the carrier, the index rising 0 to 8 (slow start): sine, hollow odd-ish tone, bright glassy FM. | 0 hits |
| **Gauss Formant Sweep** | `Digital/gauss_formant_sweep.wav` | 64 | A gaussian windowed cosine burst once per cycle (a digital formant), its centre frequency swept 1.5 to 26 harmonics: a hard, nasal, vowel-less formant sweep. | 0 hits |
| **Phase Distortion Knee** | `Digital/pd_knee.wav` | 64 | A cosine whose phase has a knee moved from the middle (0.5) to 0.04 of the cycle: sine to a pulse-like pinched wave. | 0 hits |
| **Phase Distortion Resonance** | `Digital/pd_resonance.wav` | 64 | A sine with a falling window whose frequency is swept 1 to 16 cycles per period: the resonant filter sweep of phase distortion synthesis. | 0 hits |
| **Sine Fold Sweep** | `Digital/sine_fold_sweep.wav` | 64 | A sine through a wave folder, the gain 0.4 to 14: sine, triangle, dense folded metal. | 0 hits |
| **Sine To Square Shaper** | `Digital/sine_to_square_shaper.wav` | 64 | A sine pushed through a power-law shaper, the exponent falling 1.0 to 0.06: sine, rounded square, hard square. | 0 hits |
| **Step Sequence Morph** | `Digital/step_sequence_morph.wav` | 64 | Eight levels per cycle read like a step sequencer pattern that morphs through five random patterns (slewed corners): wave sequencing staircases. | 0 hits |

#### Metallic (12 Unused Tables — Category Completely Unused)

| Table Title | File Path | Frames | What the Scan Does | Grep Result |
|---|---|---|---|---|
| **Bell Pitch Walk** | `Metallic/bell_pitch_walk.wav` | 64 | A church-bell family of twelve partials (hum 0.5, prime, tierce 1.18, quint 1.5, nominal 2 ...) whose prime walks from the 2nd to the 12th harmonic: the bell grows from a dull clang to a bright shimmer. | 0 hits |
| **Bowl Beating Pairs** | `Metallic/bowl_beating_pairs.wav` | 64 | Singing bowl modes (1, 2.7, 5.2, 8.4 ...) each split into a close pair whose spacing opens from 0 to 1.4 harmonics: the slow beating of a rubbed bowl becomes a wide chorus. | 0 hits |
| **Cymbal Cloud** | `Metallic/cymbal_cloud.wav` | 64 | Thirty-six partials scattered between the 6th and the 160th harmonic (seeded), each wobbling by up to 6 harmonics once over the scan: a shimmering, never repeating metal cloud. | 0 hits |
| **Drum Membrane Modes** | `Metallic/drum_membrane_modes.wav` | 64 | The modes of an ideal circular membrane (1, 1.59, 2.14, 2.30, 2.65 ...) with the fundamental walking from harmonic 2 to 10: a tom to a metallic tambourine-like drum. | 0 hits |
| **Free Bar Modes** | `Metallic/free_bar_modes.wav` | 64 | The modes of a free bar (1, 2.756, 5.404, 8.933, 13.34, 18.64 ...) with the fundamental walking from harmonic 1 to 6: vibraphone and marimba bar colours from soft to bright. | 0 hits |
| **Gong Modes Swell** | `Metallic/gong_modes_swell.wav` | 64 | The thirteen modes of the factory gong sample around the 3rd harmonic; the upper modes swell from 45 % to full weight (bright^n): the bloom of a struck gong. | 0 hits |
| **Metal Comb Sweep** | `Metallic/metal_comb_sweep.wav` | 64 | A comb sweeps through a saw's spectrum (spacing 3 to 16 harmonics): ringing metal. | 0 hits |
| **Random Metal Morph** | `Metallic/random_metal_morph.wav` | 64 | Six seeded random sets of 14 inharmonic partials (5th to 150th harmonic) cross faded one into the next: an anvil, a pipe, a can, each blending into the following. | 0 hits |
| **Resonant Comb Metal** | `Metallic/resonant_comb_metal.wav` | 64 | A falling saw spectrum through a resonant (feedback) comb, delay 0.03 to 0.4 of the cycle, feedback 0.9, phases scattered: sharp ringing peaks that drift like a flanged cymbal. | 0 hits |
| **Ring Mod Glide** | `Metallic/ring_mod_glide.wav` | 64 | A sine with its octave, ring modulated by a sine whose ratio glides continuously from 1 to 9.5: the sum and difference lines slide through the spectrum (non-integer ratios give inharmonic clang). | 0 hits |
| **Ring Mod Ladder** | `Metallic/ring_mod_ladder.wav` | 64 | Ring modulation of two sines at the integer ratios 2 to 17, four frames each, the depth rising inside a step. | 0 hits |
| **Ring Mod Saw Glide** | `Metallic/ring_mod_saw_glide.wav` | 64 | A 24 harmonic saw ring modulated by a sine gliding 0.5 to 12.5 harmonics (with a quarter of the dry saw kept): dense, inharmonic metal that resolves at the integer ratios. | 0 hits |

#### Noise (12 Unused Tables — Category Completely Unused)

| Table Title | File Path | Frames | What the Scan Does | Grep Result |
|---|---|---|---|---|
| **Breath Air Bumps** | `Noise/breath_air_bumps.wav` | 64 | Noise through two broad resonances (like the chiff and tube of a flute): the lower rises from the 18th to the 60th harmonic, the upper from the 70th to the 190th. | 0 hits |
| **Colour Tilt Sweep** | `Noise/colour_tilt_sweep.wav` | 64 | One frozen noise spectrum whose tilt moves from -1 (blue, hissy) through white and pink to 2.2 (brown, rumbling): the colour of noise as a scan. | 0 hits |
| **Crackle Pulses** | `Noise/crackle_pulses.wav` | 64 | Three clicks per cycle growing to 64 clicks (seeded positions, signs and a short decay tail): a few ticks become a crackling, then a dense rattle. | 0 hits |
| **Frozen Noise Variations** | `Noise/frozen_noise_variations.wav` | 64 | Four different frozen noises (seeds 11 to 14) cross faded one into the next: the texture changes character while the colour stays the same. | 0 hits |
| **Noise Band Sweep** | `Noise/noise_band_sweep.wav` | 64 | Frozen white noise with one narrow band (a quarter octave) lifted 30 dB, its centre rising from the 3rd to the 300th harmonic: whistling wind sweeping up. | 0 hits |
| **Noise Burst Widening** | `Noise/noise_burst_widening.wav` | 64 | A gaussian burst of noise once per cycle that widens from 6 % to the whole cycle: a click, a puff, a steady noise (the scan is the duty cycle of a noise gate). | 0 hits |
| **Noise Notch Sweep** | `Noise/noise_notch_sweep.wav` | 64 | Frozen noise (slightly dark) with a deep notch (34 dB) sweeping from the 4th to the 250th harmonic: the phaser-on-noise sound. | 0 hits |
| **Noise To Saw** | `Noise/noise_to_saw.wav` | 64 | A level matched cross fade from frozen noise to a 200 harmonic saw: noise that gathers into a buzzing tone. | 0 hits |
| **Noise To Sine** | `Noise/noise_to_sine.wav` | 64 | A level matched cross fade from frozen noise to a pure sine: a breath that condenses into a tone. | 0 hits |
| **Pink Lowpass Sweep** | `Noise/pink_lowpass_sweep.wav` | 64 | Frozen pink noise through a resonant 4 pole low pass whose cutoff falls from the 300th to the 2nd harmonic (quickly at first): a filter closing on noise. | 0 hits |
| **Rumble To Hiss** | `Noise/rumble_to_hiss.wav` | 64 | A cross fade, level matched, from low rumble (noise below the 14th harmonic) to bright hiss (noise above the 120th): the energy moves from the bottom to the top. | 0 hits |
| **Sparse Spectrum Fill** | `Noise/sparse_spectrum_fill.wav` | 64 | A noise spectrum with only 2 % of the harmonics present, filling to 100 %: a few ringing lines, a crackling comb, then full noise. | 0 hits |

#### Spectral (10 Unused Tables)

| Table Title | File Path | Frames | What the Scan Does | Grep Result |
|---|---|---|---|---|
| **Bell Strike** | `Spectral/additive_bell_strike.wav` | 64 | Additive bell partials (a hum, a prime, a tierce, a nominal cluster) whose high members fade as the frames go on. | 0 hits |
| **Bore Odd To Even** | `Spectral/bore_odd_to_even.wav` | 64 | A cross fade from odd harmonics only (a cylindrical reed) to the full series (a conical bore), level matched: clarinet colour to saxophone colour. | 0 hits |
| **Comb Spectrum Slide** | `Spectral/comb_spectrum_slide.wav` | 64 | A comb of period 6 harmonics, depth 90 %, slides across the spectrum of a gently falling series by one full period: the notches walk through the partials. | 0 hits |
| **Dispersion Sweep** | `Spectral/dispersion_sweep.wav` | 64 | A flat 1/n spectrum whose partial phases rotate with n squared: the magnitudes never change, the waveform sweeps from a saw through chirps to noise-like ripples (a phase-only scan). | 0 hits |
| **Even Harmonic Ladder** | `Spectral/even_harmonic_ladder.wav` | 64 | The fundamental plus even harmonics only (octave, 4th, 6th ...) up to the 60th: a sine grows an octave-stacked, reedy tone. | 0 hits |
| **Harmonic Band Sweep** | `Spectral/harmonic_band_sweep.wav` | 64 | A 1/n series with one band of harmonics lifted 24 dB, its centre rising from the 1.5th to the 120th partial (accelerating): a swept bandpass written into the cycle. | 0 hits |
| **Harmonic Sieve** | `Spectral/harmonic_sieve.wav` | 32 | A saw with only every n-th harmonic kept, n = 1 to 16, two frames each: octaves, fifths, ghost chords. | 0 hits |
| **Odd Harmonic Ladder** | `Spectral/odd_harmonic_ladder.wav` | 64 | Odd harmonics (1/n) are added one after the other, 1 up to the 60th: a pure sine grows into a hollow square-like tone. | 0 hits |
| **Prime Harmonics** | `Spectral/prime_harmonics.wav` | 64 | Only prime-numbered harmonics (2 3 5 7 11 ...) plus the fundamental, more of them as the frames go on: an irregular, bell-ish spectrum that is still a pitch. | 0 hits |
| **Spectral Tilt Sweep** | `Spectral/spectral_tilt_sweep.wav` | 64 | The harmonic roll-off steepens from 0.3 (buzzy) to 2.6 (almost a sine): the brightness control as a table. | 0 hits |

#### Vocal (10 Unused Tables)

| Table Title | File Path | Frames | What the Scan Does | Grep Result |
|---|---|---|---|---|
| **Breathy To Pressed** | `Vocal/breathy_to_pressed.wav` | 64 | One vowel (ah, men, 150 Hz) with the voice source changing: steep tilt, boosted fundamental and noise (breathy) to a flat tilt (pressed, shouted). | 0 hits |
| **Choir Blend** | `Vocal/choir_blend.wav` | 64 | Men, women and children sing the vowel sequence ah, aw, oo, eh, ee, ah together (scattered phases, level matched mix) at 175 Hz. | 0 hits |
| **Choir Singers Shift** | `Vocal/choir_singers_shift.wav` | 48 | The vowels A, O, U through a male, a female and a child's vocal tract and back: a choir whose singers change (48 frames). | 0 hits |
| **Diphthong Series** | `Vocal/diphthong_series.wav` | 64 | Eight diphthongs of eight frames each (ah-ih, ah-oo, aw-ih, eh-ih, ee-oo, oo-ah, ae-aw, eh-ah), each gliding from its first to its second vowel; a stepped table, the jump between diphthongs is intended. | 0 hits |
| **Overtone Duet** | `Vocal/overtone_duet.wav` | 64 | Two narrow resonances over the same drone cross in opposite directions: one climbs from the 4th to the 20th partial while the other falls from the 20th to the 4th. | 0 hits |
| **Tract Length Sweep** | `Vocal/tract_length_sweep.wav` | 64 | The vowel ah with all formants scaled 0.6 to 1.7: a giant to a chipmunk (the vocal tract length), pitch fixed at 150 Hz. | 0 hits |
| **Vowel Cycle Children** | `Vocal/vowel_cycle_children.wav` | 64 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of children (Peterson and Barney 1952), voice at 262 Hz; best range about an octave around that pitch. | 0 hits |
| **Vowel Cycle Men** | `Vocal/vowel_cycle_men.wav` | 64 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of men (Peterson and Barney 1952), voice at 130 Hz; best range about an octave around that pitch. | 0 hits |
| **Vowel Cycle Women** | `Vocal/vowel_cycle_women.wav` | 64 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of women (Peterson and Barney 1952), voice at 220 Hz; best range about an octave around that pitch. | 0 hits |
| **Whisper To Voiced** | `Vocal/whisper_to_voiced.wav` | 64 | The vowel ah (men, 150 Hz): frame 0 is a whisper (formant shaped noise: random phases and amplitudes), the last frame the voiced pulse train; a level matched cross fade. | 0 hits |

---

## 2. Sound Set Batch 3 Preset Plan (24 Presets)

### 2.1 Design Principles & Conventions

1. **Preset Naming Style (`docs/NAMING.md`, `scripts/factory-presets-batch2.py:36`)**:
   - Program name length is strictly capped at **24 characters** (`kVstMaxProgNameLen`).
   - Clean ASCII characters, no trailing whitespace, evocative musical terminology.
   - Strictly **no third-party product names** (e.g. references to other brands, commercial synthesizers, or proprietary trademarks are forbidden).
2. **Akai MPC Sounds Browser Taxonomy (`docs/release/SOUND-SET-CATEGORIES.md`)**:
   - Presets map to the 15 standard browser sub-styles (`Bass Sub+808`, `Bass Reese`, `Bass Acid`, `Bass Synth`, `Lead Synth`, `Lead Sync+FM`, `Pad Warm`, `Pad Evolving`, `Keys`, `Pluck`, `Bells+Mallets`, `Strings+Brass`, `Sequence`, `Texture`, `FX`).
   - Category folder remains one of the six root directories: `Bass`, `Lead`, `Pad`, `Pluck`, `Keys`, `FX`.
3. **Hardware Macro Assignments (`scripts/factory-preset-macro-names.json`)**:
   - Each preset configures **four primary macro controls** on `macro.1`, `macro.2`, `macro.3`, and `macro.4` (corresponding to Akai MPC Q-Link knobs 1–4).
   - Macro names conform to the 3–14 character ASCII limit.
4. **Perceptual Loudness & Playing Registers (`docs/release/SOUND-FIXES-1.md`)**:
   - All proposed presets adopt the ITU-R BS.1770-4 K-weighted loudness method targeting **-23.0 LUFS** (`fast_max` 100 ms window) with the category gesture offsets established in `SOUND-FIXES-1.md`: Bass (+5.3 dB), Lead (+3.1 dB), Pad (-1.6 dB), Keys (-2.5 dB), Pluck (-1.0 dB), and FX (+3.1 dB).
   - Playing gestures and registers follow the exact note ranges defined in `SOUND-FIXES-1.md:66-75`.

---

### 2.2 Category 1: Analog Wavetable Presets (4 Presets)

#### Preset 01: `Analog Sweep Bass` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 1 (Analog / Saw Filter Sweep: bass preset with built-in filter sweep driven by macro or mod wheel).
- **Browser Group**: `Bass Synth` (Folder: `Bass`)
- **Wavetable Asset**: `Analog/saw_filter_sweep.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `macro1` (Cutoff Sweep) routes +60% to `osc1.frame`; `mod_wheel` routes +35% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0, `osc1.level` = 70.0
  - `osc1.unison.voices` = 2, `osc1.unison.detune` = 8.0, `osc1.unison.spread` = 40.0
  - `sub.on` = 1, `sub.waveform` = 0 ("Sine"), `sub.octave` = -1, `sub.level` = 35.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 2400.0, `filter1.resonance` = 15.0, `filter1.drive` = 4.0, `filter1.keytrack` = 35.0
- **Effects**:
  - `distortion.on` = 1, `distortion.type` = 0 ("Soft clip"), `distortion.drive` = 3.5, `distortion.mix` = 40.0
  - `eq.on` = 1, `eq.low.mode` = 0 ("High pass"), `eq.low.cutoff` = 35.0
  - `utility.on` = 1, `utility.mono_bass` = 1, `utility.mono_freq` = 140.0
- **The Four Macros**:
  - Macro 1: `Sweep Cutoff` (`macro.1`) -> routes to `osc1.frame` (+60%)
  - Macro 2: `Sub Level` (`macro.2`) -> routes to `sub.level` (+40%)
  - Macro 3: `Filter Drive` (`macro.3`) -> routes to `filter1.drive` (+50%)
  - Macro 4: `Resonance` (`macro.4`) -> routes to `filter1.resonance` (+45%)
- **Playing Register**: C1–G1 (MIDI 36–43); `master.transpose` = 0.
- **Sound Intention**: A punchy analog synth bass with an organic wavetable filter sweep built directly into the cycle, leaving engine filters free for additional shaping and overdrive.

#### Preset 02: `Trance Stack Lead` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 2 (Analog / Saw Stack Spread: supersaw-like leads with phase spread).
- **Browser Group**: `Lead Synth` (Folder: `Lead`)
- **Wavetable Asset**: `Analog/saw_stack_spread.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 24.0
  - Modulators: `lfo1` (slow sine) routes +20% to `osc1.frame`; `velocity` routes +25% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 24.0, `osc1.level` = 68.0
  - `osc1.unison.voices` = 3, `osc1.unison.detune` = 12.0, `osc1.unison.spread` = 75.0
  - `osc2.on` = 1, `osc2.wavetable` = 1 ("Saw"), `osc2.level` = 28.0, `osc2.transpose` = 7.0, `osc2.tune` = 4.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 1 ("Style B 24 dB"), `filter1.cutoff` = 5200.0, `filter1.resonance` = 22.0, `filter1.keytrack` = 60.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 0.6, `chorus.depth` = 45.0, `chorus.mix` = 30.0
  - `delay.on` = 1, `delay.sync` = 2 ("Tempo dotted"), `delay.division` = 5, `delay.feedback` = 35.0, `delay.mix` = 20.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 2400.0, `reverb.mix` = 25.0
- **The Four Macros**:
  - Macro 1: `Spread Width` (`macro.1`) -> routes to `osc1.frame` (+50%)
  - Macro 2: `Cutoff` (`macro.2`) -> routes to `filter1.cutoff` (+45%)
  - Macro 3: `Fifth Layer` (`macro.3`) -> routes to `osc2.level` (+50%)
  - Macro 4: `Space Wash` (`macro.4`) -> routes to `reverb.mix` (+35%) and `delay.mix` (+25%)
- **Playing Register**: C3–C5 (MIDI 60–84); `master.transpose` = 0.
- **Sound Intention**: A massive trance anthem lead whose single-cycle phase spread delivers thick supersaw width with light CPU overhead.

#### Preset 03: `Sync Sine Screamer` (18 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 3 (Analog / Sync Sine Sweep: screaming mono lead with frame on pitch envelope).
- **Browser Group**: `Lead Sync+FM` (Folder: `Lead`)
- **Wavetable Asset**: `Analog/sync_sine_sweep.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 8.0
  - Modulators: `env2` (fast decay pitch snap) routes +45% to `osc1.frame`; `velocity` routes +20% to `osc1.frame`.
- **Oscillator Configuration**:
  - `master.polyphony` = 1, `master.legato` = 1, `master.portamento.time` = 45.0
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 8.0, `osc1.level` = 72.0
  - `osc2.on` = 1, `osc2.wavetable` = 1 ("Saw"), `osc2.transpose` = -12.0, `osc2.level` = 30.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 1 ("Style B 24 dB"), `filter1.cutoff` = 4800.0, `filter1.resonance` = 32.0, `filter1.drive` = 5.0, `filter1.keytrack` = 45.0
- **Effects**:
  - `distortion.on` = 1, `distortion.type` = 6 ("Tube"), `distortion.drive` = 8.0, `distortion.mix` = 65.0
  - `delay.on` = 1, `delay.sync` = 1 ("Tempo"), `delay.division` = 4, `delay.feedback` = 30.0, `delay.mix` = 18.0
- **The Four Macros**:
  - Macro 1: `Sync Formant` (`macro.1`) -> routes to `osc1.frame` (+60%)
  - Macro 2: `Sync Decay` (`macro.2`) -> routes to `env2.decay` (+50%)
  - Macro 3: `Tube Drive` (`macro.3`) -> routes to `distortion.drive` (+50%)
  - Macro 4: `Sub Octave` (`macro.4`) -> routes to `osc2.level` (+45%)
- **Playing Register**: C3–G4 (MIDI 60–79); `master.transpose` = 0.
- **Sound Intention**: A searing monophonic rock lead with a pure resonant sync formant that screams on every keystroke through saturated tube drive.

#### Preset 04: `Sync Saw Raw Bass` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 3 (Analog / Sync Saw Sweep: rough, bright sync sweep on bass).
- **Browser Group**: `Bass Synth` (Folder: `Bass`)
- **Wavetable Asset**: `Analog/sync_saw_sweep.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 4.0
  - Modulators: `env2` (punchy decay envelope) routes +35% to `osc1.frame`; `macro1` routes +65% to `osc1.frame`.
- **Oscillator Configuration**:
  - `master.polyphony` = 4, `master.legato` = 1, `master.portamento.time` = 30.0
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 4.0, `osc1.level` = 66.0
  - `sub.on` = 1, `sub.waveform` = 0 ("Sine"), `sub.octave` = -1, `sub.level` = 42.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 1800.0, `filter1.resonance` = 24.0, `filter1.drive` = 6.0, `filter1.keytrack` = 30.0
- **Effects**:
  - `distortion.on` = 1, `distortion.type` = 1 ("Hard clip"), `distortion.drive` = 5.0, `distortion.mix` = 45.0
  - `utility.on` = 1, `utility.mono_bass` = 1, `utility.mono_freq` = 150.0
  - `eq.on` = 1, `eq.low.mode` = 0 ("High pass"), `eq.low.cutoff` = 32.0
- **The Four Macros**:
  - Macro 1: `Sync Ratio` (`macro.1`) -> routes to `osc1.frame` (+65%)
  - Macro 2: `Pluck Snap` (`macro.2`) -> routes to `env2.decay` (+45%)
  - Macro 3: `Clip Drive` (`macro.3`) -> routes to `distortion.drive` (+50%)
  - Macro 4: `Sub Level` (`macro.4`) -> routes to `sub.level` (+40%)
- **Playing Register**: C1–G2 (MIDI 36–55); `master.transpose` = 0.
- **Sound Intention**: An aggressive, buzzing analog bass featuring hard-synced sawtooth overtones that tear through the mix before settling onto a firm sub foundation.

---

### 2.3 Category 2: Digital Wavetable Presets (4 Presets)

#### Preset 05: `FM Chain Tine Keys` (18 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 4 (Digital / FM Chain 1-2-3: electric piano with frame on decaying envelope and sine sub an octave down).
- **Browser Group**: `Keys` (Folder: `Keys`)
- **Wavetable Asset**: `Digital/fm_chain_123.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 10.0
  - Modulators: `env2` (fast tine decay) routes +40% to `osc1.frame`; `velocity` routes +25% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 10.0, `osc1.level` = 68.0
  - `osc2.on` = 1, `osc2.wavetable` = 0 ("Sine"), `osc2.transpose` = -12.0, `osc2.level` = 28.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 4200.0, `filter1.resonance` = 10.0, `filter1.keytrack` = 45.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 0.8, `chorus.depth` = 40.0, `chorus.mix` = 28.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 1800.0, `reverb.mix` = 22.0
- **The Four Macros**:
  - Macro 1: `Tine Bell` (`macro.1`) -> routes to `osc1.frame` (+50%)
  - Macro 2: `Tine Decay` (`macro.2`) -> routes to `env2.decay` (+45%)
  - Macro 3: `Body Sub` (`macro.3`) -> routes to `osc2.level` (+40%)
  - Macro 4: `Space Chorus` (`macro.4`) -> routes to `chorus.mix` (+35%) and `reverb.mix` (+20%)
- **Playing Register**: C2–G4 (MIDI 48–79); `master.transpose` = 0.
- **Sound Intention**: A sparkling 80s-inspired FM electric piano with shimmering metallic tines and a deep acoustic body.

#### Preset 06: `PD Reso Acid Bass` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 5 (Digital / Phase Distortion Resonance: acid bass with fast decay envelope).
- **Browser Group**: `Bass Acid` (Folder: `Bass`)
- **Wavetable Asset**: `Digital/pd_resonance.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `env2` (fast squelch decay) routes +55% to `osc1.frame`; `velocity` routes +20% to `osc1.frame`.
- **Oscillator Configuration**:
  - `master.polyphony` = 1, `master.legato` = 1, `master.portamento.time` = 35.0
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 70.0
  - `sub.on` = 1, `sub.waveform` = 0 ("Sine"), `sub.octave` = -1, `sub.level` = 30.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 4 ("Diode"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 1200.0, `filter1.resonance` = 40.0, `filter1.drive` = 12.0, `filter1.keytrack` = 35.0
- **Effects**:
  - `distortion.on` = 1, `distortion.type` = 7 ("Diode 1"), `distortion.drive` = 8.0, `distortion.mix` = 55.0
  - `delay.on` = 1, `delay.sync` = 2 ("Tempo dotted"), `delay.division` = 4, `delay.feedback` = 25.0, `delay.mix` = 15.0
  - `utility.on` = 1, `utility.mono_bass` = 1, `utility.mono_freq` = 140.0
- **The Four Macros**:
  - Macro 1: `PD Cutoff` (`macro.1`) -> routes to `osc1.frame` (+65%)
  - Macro 2: `Env Decay` (`macro.2`) -> routes to `env2.decay` (+50%)
  - Macro 3: `Diode Drive` (`macro.3`) -> routes to `filter1.drive` (+50%)
  - Macro 4: `Resonance` (`macro.4`) -> routes to `filter1.resonance` (+40%)
- **Playing Register**: C1–C3 (MIDI 36–60); `master.transpose` = 0.
- **Sound Intention**: A biting, squelchy acid bass driven by phase-distortion resonant filter sweeps that rip through saturated diode clipping.

#### Preset 07: `Step Morph Pluck` (16 chars)
- **WAVETABLE-PACK.md Proposal**: Extension for Digital Category coverage (Digital / Step Sequence Morph: wave-sequencing digital pluck).
- **Browser Group**: `Sequence` (Folder: `Pluck`)
- **Wavetable Asset**: `Digital/step_sequence_morph.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `lfo1` (synced staircase pattern) routes +50% to `osc1.frame`; `velocity` routes +25% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 66.0
  - `osc1.unison.voices` = 2, `osc1.unison.detune` = 6.0, `osc1.unison.spread` = 50.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 3 ("Digital"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 2800.0, `filter1.resonance` = 18.0, `filter1.keytrack` = 50.0
- **Effects**:
  - `delay.on` = 1, `delay.sync` = 2 ("Tempo dotted"), `delay.division` = 5, `delay.feedback` = 40.0, `delay.mix` = 24.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 1600.0, `reverb.mix` = 18.0
- **The Four Macros**:
  - Macro 1: `Pattern Morph` (`macro.1`) -> routes to `osc1.frame` (+80%)
  - Macro 2: `Step Rate` (`macro.2`) -> routes to `lfo1.rate` (+60%)
  - Macro 3: `Filter Cutoff` (`macro.3`) -> routes to `filter1.cutoff` (+45%)
  - Macro 4: `Echo Space` (`macro.4`) -> routes to `delay.mix` (+35%)
- **Playing Register**: C3–C5 (MIDI 60–84); `master.transpose` = 0.
- **Sound Intention**: A rhythmic wave-sequencing pluck whose timbre steps and morphs through evolving digital staircase waveforms on every beat.

#### Preset 08: `West Coast Wavefold` (19 chars)
- **WAVETABLE-PACK.md Proposal**: Extension for Digital Category coverage (Digital / Sine Fold Sweep: dense folded metal lead).
- **Browser Group**: `Lead Sync+FM` (Folder: `Lead`)
- **Wavetable Asset**: `Digital/sine_fold_sweep.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 5.0
  - Modulators: `env2` (expressive decay) routes +40% to `osc1.frame`; `mod_wheel` routes +50% to `osc1.frame`.
- **Oscillator Configuration**:
  - `master.polyphony` = 1, `master.legato` = 1, `master.portamento.time` = 50.0
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 5.0, `osc1.level` = 70.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 6000.0, `filter1.resonance` = 12.0, `filter1.keytrack` = 40.0
- **Effects**:
  - `distortion.on` = 1, `distortion.type` = 3 ("Sine fold"), `distortion.drive` = 4.0, `distortion.mix` = 40.0
  - `reverb.on` = 1, `reverb.type` = 5 ("Basin"), `reverb.decay` = 2200.0, `reverb.mix` = 20.0
- **The Four Macros**:
  - Macro 1: `Fold Depth` (`macro.1`) -> routes to `osc1.frame` (+70%)
  - Macro 2: `Fold Drive` (`macro.2`) -> routes to `distortion.drive` (+50%)
  - Macro 3: `Tone Dark` (`macro.3`) -> routes to `filter1.cutoff` (-40%)
  - Macro 4: `Basin Space` (`macro.4`) -> routes to `reverb.mix` (+30%)
- Playing Register: C2–C5 (MIDI 48–84); `master.transpose` = 0.
- **Sound Intention**: A rich West Coast lead that expands a pure sine wave into dense folded metallic timbres through harmonic wavefolding.

---

### 2.4 Category 3: Metallic Wavetable Presets (4 Presets)

#### Preset 09: `Shimmer Bell Walk` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 10 (Metallic / Bell Pitch Walk: bell preset where frame follows velocity, harder = brighter).
- **Browser Group**: `Bells+Mallets` (Folder: `Keys`)
- **Wavetable Asset**: `Metallic/bell_pitch_walk.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 12.0
  - Modulators: `velocity` routes +45% to `osc1.frame` (harder strike = brighter shimmer); `macro1` routes +60% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 12.0, `osc1.level` = 66.0
  - `osc1.unison.voices` = 2, `osc1.unison.detune` = 5.0, `osc1.unison.spread` = 60.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 3 ("Digital"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 5500.0, `filter1.resonance` = 15.0, `filter1.keytrack` = 60.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 0.5, `chorus.depth` = 50.0, `chorus.mix` = 25.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 2800.0, `reverb.mix` = 30.0
- **The Four Macros**:
  - Macro 1: `Harmonic Walk` (`macro.1`) -> routes to `osc1.frame` (+60%)
  - Macro 2: `Decay Length` (`macro.2`) -> routes to `env1.decay` (+40%)
  - Macro 3: `Dampening` (`macro.3`) -> routes to `filter1.cutoff` (-45%)
  - Macro 4: `Chime Space` (`macro.4`) -> routes to `reverb.mix` (+35%)
- **Playing Register**: C3–G5 (MIDI 60–91); `master.transpose` = 0.
- **Sound Intention**: A crystalline church chime whose partials open dynamically from deep metallic clangs to high glittering shimmers as keys are struck harder.

#### Preset 10: `Free Bar Vibraphone` (19 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 10 (Metallic / Free Bar Modes: vibraphone and marimba bar colours with velocity frame shift).
- **Browser Group**: `Bells+Mallets` (Folder: `Keys`)
- **Wavetable Asset**: `Metallic/free_bar_modes.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 8.0
  - Modulators: `velocity` routes +35% to `osc1.frame`; `lfo1` (motor tremolo) routes +15% to `osc1.level`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 8.0, `osc1.level` = 68.0
  - `sub.on` = 1, `sub.waveform` = 0 ("Sine"), `sub.octave` = -1, `sub.level` = 18.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 3800.0, `filter1.resonance` = 8.0, `filter1.keytrack` = 50.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 3.2, `chorus.depth` = 30.0, `chorus.mix` = 20.0
  - `reverb.on` = 1, `reverb.type` = 3 ("Vintage"), `reverb.decay` = 1500.0, `reverb.mix` = 20.0
- **The Four Macros**:
  - Macro 1: `Bar Hardness` (`macro.1`) -> routes to `osc1.frame` (+55%)
  - Macro 2: `Motor Speed` (`macro.2`) -> routes to `lfo1.rate` (+50%)
  - Macro 3: `Resonator Bar` (`macro.3`) -> routes to `sub.level` (+35%)
  - Macro 4: `Hall Space` (`macro.4`) -> routes to `reverb.mix` (+30%)
- **Playing Register**: F2–F5 (MIDI 53–89); `master.transpose` = 0.
- **Sound Intention**: An acoustic vibraphone and marimba emulation with authentic physical bar modes and an adjustable rotary motor pulse.

#### Preset 11: `Gong Bloom Pad` (14 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 11 (Metallic / Gong Modes Swell: reverse-swell pad with slow rising envelope).
- **Browser Group**: `Pad Evolving` (Folder: `Pad`)
- **Wavetable Asset**: `Metallic/gong_modes_swell.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `env1` (long swell attack) routes +50% to `osc1.frame`; `macro1` routes +60% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 62.0
  - `osc1.unison.voices` = 2, `osc1.unison.detune` = 6.0, `osc1.unison.spread` = 70.0
  - `osc2.on` = 1, `osc2.wavetable` = 0 ("Sine"), `osc2.transpose` = -12.0, `osc2.level` = 24.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 3200.0, `filter1.resonance` = 14.0, `filter1.keytrack` = 30.0
- **Effects**:
  - `phaser.on` = 1, `phaser.rate` = 0.15, `phaser.depth` = 40.0, `phaser.mix` = 25.0
  - `reverb.on` = 1, `reverb.type` = 5 ("Basin"), `reverb.decay` = 4500.0, `reverb.size` = 80.0, `reverb.mix` = 35.0
- **The Four Macros**:
  - Macro 1: `Bloom Depth` (`macro.1`) -> routes to `osc1.frame` (+60%)
  - Macro 2: `Swell Time` (`macro.2`) -> routes to `env1.attack` (+50%)
  - Macro 3: `Drone Sub` (`macro.3`) -> routes to `osc2.level` (+40%)
  - Macro 4: `Basin Space` (`macro.4`) -> routes to `reverb.mix` (+30%)
- **Playing Register**: C2–G4 (MIDI 48–79); `master.transpose` = 0.
- **Sound Intention**: A meditative ambient pad that recreates the physical bloom of a struck bronze temple gong as high vibrational modes swell slowly into a vast reverb basin.

#### Preset 12: `Cymbal Cloud Riser` (18 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 11 (Metallic / Cymbal Cloud: cymbal-like risers on a slow rising envelope).
- **Browser Group**: `FX` (Folder: `FX`)
- **Wavetable Asset**: `Metallic/cymbal_cloud.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `env1` (upward build) routes +70% to `osc1.frame`; `macro1` routes +80% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 66.0
  - `osc1.unison.voices` = 3, `osc1.unison.detune` = 18.0, `osc1.unison.spread` = 80.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 1 ("Style B 24 dB"), `filter1.cutoff` = 8000.0, `filter1.resonance` = 25.0, `filter1.keytrack` = 0.0
- **Effects**:
  - `flanger.on` = 1, `flanger.rate` = 0.2, `flanger.depth` = 60.0, `flanger.mix` = 30.0
  - `delay.on` = 1, `delay.sync` = 2 ("Tempo dotted"), `delay.division` = 5, `delay.feedback` = 45.0, `delay.mix` = 25.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 3500.0, `reverb.mix` = 35.0
- **The Four Macros**:
  - Macro 1: `Cloud Motion` (`macro.1`) -> routes to `osc1.frame` (+80%)
  - Macro 2: `Riser Length` (`macro.2`) -> routes to `env1.attack` (+50%)
  - Macro 3: `Flange Jet` (`macro.3`) -> routes to `flanger.mix` (+40%)
  - Macro 4: `Echo Wash` (`macro.4`) -> routes to `delay.mix` (+30%)
- **Playing Register**: C3 (MIDI 60); `master.transpose` = 0.
- **Sound Intention**: A dramatic transitional riser built from thirty-six wobbling metallic partials, sweeping from a dull hum into a blinding cloud of cymbal shimmer.

---

### 2.5 Category 4: Noise Wavetable Presets (4 Presets)

#### Preset 13: `Wind Sweep Riser` (16 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 12 (Noise / Noise Band Sweep: wind riser layer with frame on a rising envelope).
- **Browser Group**: `FX` (Folder: `FX`)
- **Wavetable Asset**: `Noise/noise_band_sweep.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `env1` (long riser attack) routes +75% to `osc1.frame`; `mod_wheel` routes +60% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 68.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 3 ("Digital"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 7000.0, `filter1.resonance` = 20.0, `filter1.keytrack` = 0.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 0.4, `chorus.depth` = 60.0, `chorus.mix` = 35.0
  - `delay.on` = 1, `delay.sync` = 2 ("Tempo dotted"), `delay.division` = 4, `delay.feedback` = 35.0, `delay.mix` = 20.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 3200.0, `reverb.mix` = 30.0
- **The Four Macros**:
  - Macro 1: `Band Pitch` (`macro.1`) -> routes to `osc1.frame` (+80%)
  - Macro 2: `Sweep Time` (`macro.2`) -> routes to `env1.attack` (+50%)
  - Macro 3: `Reso Whistle` (`macro.3`) -> routes to `filter1.resonance` (+40%)
  - Macro 4: `Space` (`macro.4`) -> routes to `reverb.mix` (+30%)
- **Playing Register**: C3 (MIDI 60); `master.transpose` = 0.
- **Sound Intention**: An atmospheric wind riser swept by a narrow bandpass peak across white noise, rising like gale-force winds through a cavern.

#### Preset 14: `Air Chiff Flute` (15 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 12 (Noise / Breath Air Bumps: breath layer under tonal oscillator).
- **Browser Group**: `Lead Synth` (Folder: `Lead`)
- **Wavetable Asset**: `Noise/breath_air_bumps.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 10.0
  - Modulators: `velocity` routes +25% to `osc1.frame`; `poly_aftertouch` routes +30% to `osc1.level`.
- **Oscillator Configuration**:
  - `master.polyphony` = 1, `master.legato` = 1, `master.portamento.time` = 40.0
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 10.0, `osc1.level` = 32.0
  - `osc2.on` = 1, `osc2.wavetable` = 3 ("Triangle"), `osc2.level` = 66.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 3500.0, `filter1.resonance` = 10.0, `filter1.keytrack` = 60.0
- **Effects**:
  - `delay.on` = 1, `delay.sync` = 1 ("Tempo"), `delay.division` = 4, `delay.feedback` = 25.0, `delay.mix` = 18.0
  - `reverb.on` = 1, `reverb.type` = 3 ("Vintage"), `reverb.decay` = 2000.0, `reverb.mix` = 24.0
- **The Four Macros**:
  - Macro 1: `Breath Tone` (`macro.1`) -> routes to `osc1.frame` (+60%)
  - Macro 2: `Chiff Amount` (`macro.2`) -> routes to `osc1.level` (+50%)
  - Macro 3: `Filter Cutoff` (`macro.3`) -> routes to `filter1.cutoff` (+45%)
  - Macro 4: `Echo Space` (`macro.4`) -> routes to `delay.mix` (+30%)
- **Playing Register**: C3–C6 (MIDI 60–96); `master.transpose` = 0.
- **Sound Intention**: A breathy solo flute lead layering acoustic tube airflow resonances over a mellow triangle fundamental with responsive pad aftertouch.

#### Preset 15: `Pink Noise Sub` (14 chars)
- **WAVETABLE-PACK.md Proposal**: Extension for Noise Category coverage (Noise / Pink Lowpass Sweep: closing filter on noise for snappy sub bass).
- **Browser Group**: `Bass Sub+808` (Folder: `Bass`)
- **Wavetable Asset**: `Noise/pink_lowpass_sweep.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 40.0
  - Modulators: `env2` (snappy filter decay) routes -30% to `osc1.frame`; `macro1` routes +60% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 40.0, `osc1.level` = 35.0
  - `sub.on` = 1, `sub.waveform` = 0 ("Sine"), `sub.octave` = -1, `sub.level` = 55.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 1200.0, `filter1.resonance` = 15.0, `filter1.keytrack` = 30.0
- **Effects**:
  - `distortion.on` = 1, `distortion.type` = 0 ("Soft clip"), `distortion.drive` = 3.0, `distortion.mix` = 40.0
  - `utility.on` = 1, `utility.mono_bass` = 1, `utility.mono_freq` = 150.0
  - `eq.on` = 1, `eq.low.mode` = 0 ("High pass"), `eq.low.cutoff` = 30.0
- **The Four Macros**:
  - Macro 1: `Pink Filter` (`macro.1`) -> routes to `osc1.frame` (+60%)
  - Macro 2: `Sub Boom` (`macro.2`) -> routes to `sub.level` (+40%)
  - Macro 3: `Attack Decay` (`macro.3`) -> routes to `env2.decay` (+45%)
  - Macro 4: `Drive Squelch` (`macro.4`) -> routes to `distortion.drive` (+50%)
- **Playing Register**: C1–G2 (MIDI 36–55); `master.transpose` = 0.
- **Sound Intention**: A rumbling sub bass whose transient bite comes from a resonant 4-pole low-pass sweep closing swiftly over pink noise.

#### Preset 16: `Vinyl Crackle Bed` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Extension for Noise Category coverage (Noise / Crackle Pulses: vintage analog surface noise texture).
- **Browser Group**: `Texture` (Folder: `FX`)
- **Wavetable Asset**: `Noise/crackle_pulses.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 10.0
  - Modulators: `lfo1` (slow random drift) routes +20% to `osc1.frame`; `macro1` routes +75% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 10.0, `osc1.level` = 55.0
  - `osc2.on` = 1, `osc2.wavetable` = 0 ("Sine"), `osc2.transpose` = -24.0, `osc2.level` = 18.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 2500.0, `filter1.resonance` = 10.0, `filter1.keytrack` = 0.0
- **Effects**:
  - `eq.on` = 1, `eq.low.cutoff` = 100.0, `eq.high.cutoff` = 4000.0, `eq.high.gain` = -6.0
  - `reverb.on` = 1, `reverb.type` = 5 ("Basin"), `reverb.decay` = 3000.0, `reverb.mix` = 25.0
- **The Four Macros**:
  - Macro 1: `Crackle Density` (`macro.1`) -> routes to `osc1.frame` (+75%)
  - Macro 2: `Tone Dampen` (`macro.2`) -> routes to `filter1.cutoff` (-50%)
  - Macro 3: `Low Rumble` (`macro.3`) -> routes to `osc2.level` (+40%)
  - Macro 4: `Space Depth` (`macro.4`) -> routes to `reverb.mix` (+30%)
- **Playing Register**: C3 (MIDI 60); `master.transpose` = 0.
- **Sound Intention**: An authentic vintage vinyl background texture evolving from sparse sporadic ticks into a dense, dusty analog dust rattle.

---

### 2.6 Category 5: Spectral Wavetable Presets (4 Presets)

#### Preset 17: `Acoustic Bore Reed` (18 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 6 (Spectral / Bore Odd To Even: reed, clarinet and sax-like leads with frame on aftertouch).
- **Browser Group**: `Lead Synth` (Folder: `Lead`)
- **Wavetable Asset**: `Spectral/bore_odd_to_even.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `poly_aftertouch` (pad pressure) routes +60% to `osc1.frame`; `velocity` routes +20% to `osc1.frame`.
- **Oscillator Configuration**:
  - `master.polyphony` = 1, `master.legato` = 1, `master.portamento.time` = 35.0
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 70.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 4200.0, `filter1.resonance` = 14.0, `filter1.keytrack` = 55.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 0.7, `chorus.depth` = 30.0, `chorus.mix` = 18.0
  - `delay.on` = 1, `delay.sync` = 1 ("Tempo"), `delay.division` = 4, `delay.feedback` = 20.0, `delay.mix` = 16.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 1600.0, `reverb.mix` = 20.0
- **The Four Macros**:
  - Macro 1: `Bore Morph` (`macro.1`) -> routes to `osc1.frame` (+70%)
  - Macro 2: `Filter Bright` (`macro.2`) -> routes to `filter1.cutoff` (+45%)
  - Macro 3: `Pressure Sense` (`macro.3`) -> routes to `poly_aftertouch` (+50%)
  - Macro 4: `Room Space` (`macro.4`) -> routes to `reverb.mix` (+30%)
- **Playing Register**: C3–G5 (MIDI 60–91); `master.transpose` = 0.
- **Sound Intention**: An expressive acoustic reed solo instrument that morphs seamlessly from hollow cylindrical clarinet woodwinds to rich conical saxophone brass via pad pressure.

#### Preset 18: `Harmonic Wah Bass` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 7 (Spectral / Harmonic Band Sweep: wah and talking basses with frame on LFO).
- **Browser Group**: `Bass Synth` (Folder: `Bass`)
- **Wavetable Asset**: `Spectral/harmonic_band_sweep.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 10.0
  - Modulators: `lfo1` (tempo-synced triangle) routes +40% to `osc1.frame`; `mod_wheel` routes +35% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 10.0, `osc1.level` = 66.0
  - `sub.on` = 1, `sub.waveform` = 0 ("Sine"), `sub.octave` = -1, `sub.level` = 45.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 2200.0, `filter1.resonance` = 20.0, `filter1.drive` = 4.0, `filter1.keytrack` = 30.0
- **Effects**:
  - `distortion.on` = 1, `distortion.type` = 6 ("Tube"), `distortion.drive` = 4.5, `distortion.mix` = 45.0
  - `utility.on` = 1, `utility.mono_bass` = 1, `utility.mono_freq` = 150.0
  - `eq.on` = 1, `eq.low.mode` = 0 ("High pass"), `eq.low.cutoff` = 32.0
- **The Four Macros**:
  - Macro 1: `Wah Freq` (`macro.1`) -> routes to `osc1.frame` (+65%)
  - Macro 2: `Wah Rate` (`macro.2`) -> routes to `lfo1.rate` (+60%)
  - Macro 3: `Tube Drive` (`macro.3`) -> routes to `distortion.drive` (+45%)
  - Macro 4: `Sub Bottom` (`macro.4`) -> routes to `sub.level` (+40%)
- **Playing Register**: C1–C3 (MIDI 36–60); `master.transpose` = 0.
- **Sound Intention**: A vocal talking funk bass featuring an accelerated harmonic bandpass sweep that produces a throaty wah sound locked to the beat.

#### Preset 19: `Cathedral Bell Ring` (19 chars)
- **WAVETABLE-PACK.md Proposal**: Extension for Spectral Category coverage (Spectral / Bell Strike: additive bell strike decaying to hum).
- **Browser Group**: `Bells+Mallets` (Folder: `Keys`)
- **Wavetable Asset**: `Spectral/additive_bell_strike.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `env2` (decaying frame sweep) routes +50% to `osc1.frame`; `velocity` routes +25% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 68.0
  - `osc1.unison.voices` = 2, `osc1.unison.detune` = 4.0, `osc1.unison.spread` = 65.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 3 ("Digital"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 6200.0, `filter1.resonance` = 10.0, `filter1.keytrack` = 60.0
- **Effects**:
  - `reverb.on` = 1, `reverb.type` = 2 ("Hall"), `reverb.decay` = 3600.0, `reverb.mix` = 32.0
- **The Four Macros**:
  - Macro 1: `Bell Bright` (`macro.1`) -> routes to `osc1.frame` (+60%)
  - Macro 2: `Strike Ring` (`macro.2`) -> routes to `env1.decay` (+45%)
  - Macro 3: `Dampen Cutoff` (`macro.3`) -> routes to `filter1.cutoff` (-40%)
  - Macro 4: `Hall Reverb` (`macro.4`) -> routes to `reverb.mix` (+35%)
- **Playing Register**: C3–G5 (MIDI 60–91); `master.transpose` = 0.
- **Sound Intention**: A resonant additive bronze cathedral bell whose high harmonic clusters ring brightly on the strike before decaying gracefully into a fundamental hum.

#### Preset 20: `Dispersion Phase Pad` (20 chars)
- **WAVETABLE-PACK.md Proposal**: Extension for Spectral Category coverage (Spectral / Dispersion Sweep: phase-only scan with swirling chirps).
- **Browser Group**: `Pad Evolving` (Folder: `Pad`)
- **Wavetable Asset**: `Spectral/dispersion_sweep.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 20.0
  - Modulators: `lfo1` (slow sine) routes +35% to `osc1.frame`; `macro1` routes +70% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 20.0, `osc1.level` = 64.0
  - `osc1.unison.voices` = 3, `osc1.unison.detune` = 10.0, `osc1.unison.spread` = 75.0
  - `osc2.on` = 1, `osc2.wavetable` = 0 ("Sine"), `osc2.transpose` = -12.0, `osc2.level` = 24.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 3600.0, `filter1.resonance` = 16.0, `filter1.keytrack` = 35.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 0.4, `chorus.depth` = 45.0, `chorus.mix` = 28.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 3800.0, `reverb.mix` = 30.0
- **The Four Macros**:
  - Macro 1: `Phase Swirl` (`macro.1`) -> routes to `osc1.frame` (+70%)
  - Macro 2: `Swirl Speed` (`macro.2`) -> routes to `lfo1.rate` (+50%)
  - Macro 3: `Filter Warmth` (`macro.3`) -> routes to `filter1.cutoff` (+40%)
  - Macro 4: `Space Depth` (`macro.4`) -> routes to `reverb.mix` (+35%)
- **Playing Register**: C2–G4 (MIDI 48–79); `master.transpose` = 0.
- **Sound Intention**: An ethereal ambient pad whose constant harmonic spectrum ripples with swirling chirp-like textures through parabolic phase dispersion.

---

### 2.7 Category 6: Vocal Wavetable Presets (4 Presets)

#### Preset 21: `Soprano Vowel Pad` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 8 (Vocal / Vowel Cycle Women: vocal pads cycling vowel triangle around 220 Hz).
- **Browser Group**: `Pad Warm` (Folder: `Pad`)
- **Wavetable Asset**: `Vocal/vowel_cycle_women.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `lfo1` (slow sine) routes +40% to `osc1.frame`; `mod_wheel` routes +45% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 66.0
  - `osc1.unison.voices` = 3, `osc1.unison.detune` = 8.0, `osc1.unison.spread` = 70.0
  - `osc2.on` = 1, `osc2.wavetable` = 0 ("Sine"), `osc2.transpose` = -12.0, `osc2.level` = 20.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 3 ("Digital"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 4800.0, `filter1.resonance` = 10.0, `filter1.keytrack` = 40.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 0.5, `chorus.depth` = 40.0, `chorus.mix` = 25.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 3200.0, `reverb.mix` = 32.0
- **The Four Macros**:
  - Macro 1: `Vowel Morph` (`macro.1`) -> routes to `osc1.frame` (+80%)
  - Macro 2: `Cycle Rate` (`macro.2`) -> routes to `lfo1.rate` (+55%)
  - Macro 3: `Choir Unison` (`macro.3`) -> routes to `osc1.unison.detune` (+50%)
  - Macro 4: `Space Reverb` (`macro.4`) -> routes to `reverb.mix` (+30%)
- **Playing Register**: A2–A4 (centered around 220 Hz / A3; MIDI 57–81); `master.transpose` = 0.
- **Sound Intention**: A lush female vocal choir pad smoothly cycling through natural vowel formants with rich stereo chorus and luminous hall reverb.

#### Preset 22: `Talk Box Baritone` (17 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 8 (Vocal / Vowel Cycle Men: talk-box lead cycling vowel triangle around 130 Hz).
- **Browser Group**: `Lead Synth` (Folder: `Lead`)
- **Wavetable Asset**: `Vocal/vowel_cycle_men.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `mod_wheel` routes +65% to `osc1.frame`; `poly_aftertouch` routes +25% to `osc1.frame`.
- **Oscillator Configuration**:
  - `master.polyphony` = 1, `master.legato` = 1, `master.portamento.time` = 55.0
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 70.0
  - `osc1.unison.voices` = 2, `osc1.unison.detune` = 5.0, `osc1.unison.spread` = 40.0
  - `sub.on` = 1, `sub.waveform` = 0 ("Sine"), `sub.octave` = -1, `sub.level` = 32.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 3600.0, `filter1.resonance` = 15.0, `filter1.drive` = 4.0, `filter1.keytrack` = 45.0
- **Effects**:
  - `distortion.on` = 1, `distortion.type` = 6 ("Tube"), `distortion.drive` = 5.0, `distortion.mix` = 45.0
  - `delay.on` = 1, `delay.sync` = 1 ("Tempo"), `delay.division` = 4, `delay.feedback` = 28.0, `delay.mix` = 20.0
- **The Four Macros**:
  - Macro 1: `Mouth Open` (`macro.1`) -> routes to `osc1.frame` (+85%)
  - Macro 2: `Tube Drive` (`macro.2`) -> routes to `distortion.drive` (+50%)
  - Macro 3: `Filter Bright` (`macro.3`) -> routes to `filter1.cutoff` (+40%)
  - Macro 4: `Echo Mix` (`macro.4`) -> routes to `delay.mix` (+30%)
- **Playing Register**: C2–C4 (centered around 130 Hz / C3; MIDI 48–72); `master.transpose` = 0.
- **Sound Intention**: A gritty, expressive baritone talk-box lead that glides smoothly across male vocal formants under mod wheel control.

#### Preset 23: `Choir Swell Ensemble` (20 chars)
- **WAVETABLE-PACK.md Proposal**: Proposal 9 (Vocal / Choir Blend: choir pads with long attack and octave layer).
- **Browser Group**: `Strings+Brass` (Folder: `Pad`)
- **Wavetable Asset**: `Vocal/choir_blend.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 5.0
  - Modulators: `env1` (long swell attack) routes +35% to `osc1.frame`; `macro1` routes +60% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 5.0, `osc1.level` = 64.0
  - `osc1.unison.voices` = 4, `osc1.unison.detune` = 14.0, `osc1.unison.spread` = 80.0
  - `osc2.on` = 1, `osc2.wavetable` = 13 ("Preset table" or "Sine to Saw"), `osc2.transpose` = 12.0, `osc2.level` = 22.0, `osc2.tune` = 3.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 0 ("Analog"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 4200.0, `filter1.resonance` = 8.0, `filter1.keytrack` = 35.0
- **Effects**:
  - `chorus.on` = 1, `chorus.rate` = 0.6, `chorus.depth` = 50.0, `chorus.mix` = 32.0
  - `reverb.on` = 1, `reverb.type` = 2 ("Hall"), `reverb.decay` = 4000.0, `reverb.size` = 85.0, `reverb.mix` = 35.0
- **The Four Macros**:
  - Macro 1: `Vowel Blend` (`macro.1`) -> routes to `osc1.frame` (+60%)
  - Macro 2: `Attack Time` (`macro.2`) -> routes to `env1.attack` (+50%)
  - Macro 3: `High Voices` (`macro.3`) -> routes to `osc2.level` (+45%)
  - Macro 4: `Hall Reverb` (`macro.4`) -> routes to `reverb.mix` (+35%)
- **Playing Register**: C2–G4 (MIDI 48–79); `master.transpose` = 0.
- **Sound Intention**: A grand choral ensemble blending male, female, and child voices into a soaring atmospheric pad with layered octave shimmer.

#### Preset 24: `Diphthong Morph Vox` (18 chars)
- **WAVETABLE-PACK.md Proposal**: Extension for Vocal Category coverage (Vocal / Diphthong Series: stepped English diphthongs on talking synth).
- **Browser Group**: `Lead Synth` (Folder: `Lead`)
- **Wavetable Asset**: `Vocal/diphthong_series.wav` (64 frames)
- **Frame Modulation**:
  - Base frame: `osc1.frame` = 0.0
  - Modulators: `lfo1` (stepped glissando) routes +50% to `osc1.frame`; `mod_wheel` routes +40% to `osc1.frame`.
- **Oscillator Configuration**:
  - `osc1.on` = 1, `osc1.wavetable` = 13 ("Preset table"), `osc1.frame` = 0.0, `osc1.level` = 68.0
  - `osc1.unison.voices` = 2, `osc1.unison.detune` = 6.0, `osc1.unison.spread` = 55.0
- **Filter**:
  - `filter1.on` = 1, `filter1.model` = 3 ("Digital"), `filter1.style` = 0 ("Style A 12 dB"), `filter1.cutoff` = 5000.0, `filter1.resonance` = 12.0, `filter1.keytrack` = 45.0
- **Effects**:
  - `delay.on` = 1, `delay.sync` = 2 ("Tempo dotted"), `delay.division` = 4, `delay.feedback` = 30.0, `delay.mix` = 20.0
  - `reverb.on` = 1, `reverb.type` = 1 ("Plate"), `reverb.decay` = 2200.0, `reverb.mix` = 24.0
- **The Four Macros**:
  - Macro 1: `Diphthong Step` (`macro.1`) -> routes to `osc1.frame` (+85%)
  - Macro 2: `Glissando Speed` (`macro.2`) -> routes to `lfo1.rate` (+50%)
  - Macro 3: `Filter Cutoff` (`macro.3`) -> routes to `filter1.cutoff` (+40%)
  - Macro 4: `Echo Space` (`macro.4`) -> routes to `delay.mix` (+30%)
- **Playing Register**: C3–C5 (MIDI 60–84); `master.transpose` = 0.
- **Sound Intention**: An animated vocal synth lead that shifts distinctly across eight gliding English diphthongs for an authentic robotic talking effect.

---

### 2.8 Summary of Batch 3 Planned Presets

| # | Preset Name | Category | Wavetable Asset | Browser Group | Main Frame Modulator | Key Macro 1 |
|---|---|---|---|---|---|---|
| 01 | **Analog Sweep Bass** | Analog | `Analog/saw_filter_sweep.wav` | `Bass Synth` | `macro1` / `mod_wheel` | `Sweep Cutoff` |
| 02 | **Trance Stack Lead** | Analog | `Analog/saw_stack_spread.wav` | `Lead Synth` | `lfo1` / `velocity` | `Spread Width` |
| 03 | **Sync Sine Screamer** | Analog | `Analog/sync_sine_sweep.wav` | `Lead Sync+FM` | `env2` / `velocity` | `Sync Formant` |
| 04 | **Sync Saw Raw Bass** | Analog | `Analog/sync_saw_sweep.wav` | `Bass Synth` | `env2` / `macro1` | `Sync Ratio` |
| 05 | **FM Chain Tine Keys** | Digital | `Digital/fm_chain_123.wav` | `Keys` | `env2` / `velocity` | `Tine Bell` |
| 06 | **PD Reso Acid Bass** | Digital | `Digital/pd_resonance.wav` | `Bass Acid` | `env2` / `velocity` | `PD Cutoff` |
| 07 | **Step Morph Pluck** | Digital | `Digital/step_sequence_morph.wav` | `Sequence` | `lfo1` / `velocity` | `Pattern Morph` |
| 08 | **West Coast Wavefold** | Digital | `Digital/sine_fold_sweep.wav` | `Lead Sync+FM` | `env2` / `mod_wheel` | `Fold Depth` |
| 09 | **Shimmer Bell Walk** | Metallic | `Metallic/bell_pitch_walk.wav` | `Bells+Mallets` | `velocity` / `macro1` | `Harmonic Walk` |
| 10 | **Free Bar Vibraphone** | Metallic | `Metallic/free_bar_modes.wav` | `Bells+Mallets` | `velocity` / `lfo1` | `Bar Hardness` |
| 11 | **Gong Bloom Pad** | Metallic | `Metallic/gong_modes_swell.wav` | `Pad Evolving` | `env1` / `macro1` | `Bloom Depth` |
| 12 | **Cymbal Cloud Riser** | Metallic | `Metallic/cymbal_cloud.wav` | `FX` | `env1` / `macro1` | `Cloud Motion` |
| 13 | **Wind Sweep Riser** | Noise | `Noise/noise_band_sweep.wav` | `FX` | `env1` / `mod_wheel` | `Band Pitch` |
| 14 | **Air Chiff Flute** | Noise | `Noise/breath_air_bumps.wav` | `Lead Synth` | `velocity` / `poly_aftertouch` | `Breath Tone` |
| 15 | **Pink Noise Sub** | Noise | `Noise/pink_lowpass_sweep.wav` | `Bass Sub+808` | `env2` / `macro1` | `Pink Filter` |
| 16 | **Vinyl Crackle Bed** | Noise | `Noise/crackle_pulses.wav` | `Texture` | `lfo1` / `macro1` | `Crackle Density` |
| 17 | **Acoustic Bore Reed** | Spectral | `Spectral/bore_odd_to_even.wav` | `Lead Synth` | `poly_aftertouch` / `velocity` | `Bore Morph` |
| 18 | **Harmonic Wah Bass** | Spectral | `Spectral/harmonic_band_sweep.wav` | `Bass Synth` | `lfo1` / `mod_wheel` | `Wah Freq` |
| 19 | **Cathedral Bell Ring** | Spectral | `Spectral/additive_bell_strike.wav` | `Bells+Mallets` | `env2` / `velocity` | `Bell Bright` |
| 20 | **Dispersion Phase Pad** | Spectral | `Spectral/dispersion_sweep.wav` | `Pad Evolving` | `lfo1` / `macro1` | `Phase Swirl` |
| 21 | **Soprano Vowel Pad** | Vocal | `Vocal/vowel_cycle_women.wav` | `Pad Warm` | `lfo1` / `mod_wheel` | `Vowel Morph` |
| 22 | **Talk Box Baritone** | Vocal | `Vocal/vowel_cycle_men.wav` | `Lead Synth` | `mod_wheel` / `poly_aftertouch` | `Mouth Open` |
| 23 | **Choir Swell Ensemble** | Vocal | `Vocal/choir_blend.wav` | `Strings+Brass` | `env1` / `macro1` | `Vowel Blend` |
| 24 | **Diphthong Morph Vox** | Vocal | `Vocal/diphthong_series.wav` | `Lead Synth` | `lfo1` / `mod_wheel` | `Diphthong Step` |

---

## 3. Generator Implementation Checklist

To turn this plan into working `.sbpreset` files, Akai host `.xpl` files, and calibrated levels without manual errors, the following script architecture, fields, and workflow must be executed:

### 3.1 Source Data Scripts & Structure

- [ ] **Create `scripts/factory-presets-batch3.py`**:
  - Main module defining the 24 presets using `scripts/sb_preset_dsl.py`.
  - Alternatively, structure modularly under `scripts/batch3/` matching Batch 2:
    - `scripts/batch3/analog.py` (4 presets)
    - `scripts/batch3/digital.py` (4 presets)
    - `scripts/batch3/metallic.py` (4 presets)
    - `scripts/batch3/noise.py` (4 presets)
    - `scripts/batch3/spectral.py` (4 presets)
    - `scripts/batch3/vocal.py` (4 presets)
- [ ] **Contract of `add()` function** (from `scripts/factory-presets-batch2.py:35`):
  Each preset requires eight arguments:
  ```python
  add(name, category, tags, description, params, routes, macro_names, content=None)
  ```
  - `name` (str): Max 24 chars, ASCII only, unique across the library.
  - `category` (str): One of `"Bass"`, `"Lead"`, `"Pad"`, `"Pluck"`, `"Keys"`, `"FX"`.
  - `tags` (str): Comma-separated descriptive tags.
  - `description` (str): Clear one-sentence summary of sound intention.
  - `params` (dict): Parameter key-value overrides verified against `docs/params.json`.
  - `routes` (list): Modulation matrix routes `R(source, dest, amount, ...)`.
  - `macro_names` (list of 8 str): Custom macro names for `macro.1` through `macro.8`.
  - `content` (`C()` object): `C(wavetables=[WT(osc, "Category/name")])` referencing pack `com.safian.synthyboy.factory-wavetables`.

### 3.2 Required Preset DSL Helpers

- [ ] **`WT(osc, table)`**: Resolves `content/factory-wavetables/<table_rel>.wav`, extracts frame count from file size `(filesize - 44) // 4096`, and computes SHA-256 hash.
- [ ] **`pressure(amount)`**: Generates default pad pressure routing `R("poly_aftertouch", "filter1.cutoff", amount)`.
- [ ] **`R(source, dest, amount, ...)`**: Generates modulation slot mapping with exact source and destination choice indices from `docs/params.json`.

### 3.3 Build & Integration Pipeline Checklist

- [ ] **Step 1: Wire into Master Preset Generator**:
  In `scripts/gen-factory-presets.py`:
  - Import `factory_presets_batch3.ENTRIES`.
  - Append to the master preset list in `build()`.
- [ ] **Step 2: Sync Macro Names**:
  Run `python3 scripts/factory-presets-batch3.py --sync-names` to export the 24 new presets into `scripts/factory-preset-macro-names.json`.
- [ ] **Step 3: Perceptual Loudness Calibration**:
  - Run `scripts/measure-loudness.py` on the 24 new presets using the category gestures defined in `SOUND-FIXES-1.md:66-75`.
  - Calculate `fast_max` (100 ms K-weighted window) and move `master.volume` until `fast_max + gesture_offset == -23.0 LUFS`.
  - Commit calibrated levels into `scripts/factory-preset-levels.json`.
- [ ] **Step 4: Update Browser Category Mapping**:
  - Add each of the 24 presets to `scripts/factory-preset-categories.json` under its designated browser group.
  - Verify with `python3 scripts/assign-preset-categories.py --check`.
- [ ] **Step 5: Generate Production Files**:
  - Run `python3 scripts/gen-factory-presets.py`: Writes `.sbpreset` files to `content/presets/<Category>/` and updates `content/presets/contentpack.json`.
  - Run `python3 scripts/gen-xpl-presets.py`: Writes `.xpl` files for the Akai MPC Sounds browser (renumbering sequentially `000` to `183`).
- [ ] **Step 6: Update Automated Tests**:
  - Update `scripts/tests/test_gen_xpl_presets.py` to assert 184 presets.
  - Update `scripts/tests/test_assign_preset_categories.py` to assert 184 presets.
- [ ] **Step 7: Update Documentation**:
  - Update `docs/user-guide/factory-sounds.md` with the 24 new presets, descriptions, playing advice, and first macros.
  - Update `docs/release/SOUND-SET-CATEGORIES.md` with the new group preset counts and tables.
  - Create release document `docs/release/SOUND-SET-BATCH-3.md`.

---

## 4. Open Questions for the Owner

The following architectural and sound design questions are identified from repository evidence and reserved for owner decision:

1. **Batch Size Confirmation**:
   - *Question*: Is a 24-preset batch size desired for Batch 3 (bringing the library from 160 to 184 presets), continuing the exact 24-preset release rhythm established in Batch 1 (`SOUND-SET-BATCH-1.md`) and Batch 2 (`SOUND-SET-BATCH-2.md`)?
2. **Ear Evaluation on Hardware**:
   - *Context*: Because agent sound design is conducted via offline DSP measurement and acoustic formulas without physical auditioning, all filter cutoffs, modulation depths, and resonance amounts represent initial estimates.
   - *Question*: Which of the new physical model emulations (such as `Free Bar Vibraphone` bar modes, `Shimmer Bell Walk` partial spacing, and `Acoustic Bore Reed` cylinder-to-cone morphing) should be prioritized during initial hardware listening on the MPC XL and Key 61?
3. **Hardware Headroom & Limiter Policy**:
   - *Context*: `SOUND-SET-BATCH-1.md:20` and `SOUND-FIXES-1.md` record that the MPC shell applies a fixed +18 dB output gain stage without a master limiter (`SYNTHYBOY_MPC_OUTPUT_GAIN_DB`). Batch 1 used `limiter_last()` (compressor in look-ahead single-band limit mode at -1.5 dBFS) on transient-heavy sounds.
   - *Question*: Should Batch 3 presets with sharp physical transients (such as `Cathedral Bell Ring`, `Shimmer Bell Walk`, and `Step Morph Pluck`) include `limiter_last()`, or remain completely dynamic without compression?
4. **Fixed-Formant Vocal Tables & Transposition**:
   - *Context*: `docs/release/WAVETABLE-PACK.md:116` notes that vocal tables are synthesized at fixed fundamental pitches (e.g., Men at 130 Hz / ~C3, Women at 220 Hz / ~A3, Choir Blend at 175 Hz / ~F3); transposing far outside their target octave shifts formants unrealistically.
   - *Question*: Should presets utilizing vocal tables enforce pitch limits (via `filter1.keytrack` or narrower playing instructions in `factory-sounds.md`), or is formant-shifting across multi-octave ranges acceptable for creative sound design?
5. **Script Modularity**:
   - *Question*: Should the generator implementation follow the modular file layout of Batch 2 (`scripts/batch3/{analog,digital,metallic,noise,spectral,vocal}.py` loaded by `scripts/factory-presets-batch3.py`), or a single consolidated script?
