# SynthyBoy Factory Content Pack Guide

This chapter details the factory content pack included with SynthyBoy, covering the complete library of wavetables, samples, multisample instruments, and impulse responses, how to load and browse content on the oscillator screens, engine fallback behavior, and licensing terms.

---

## 1. Overview & Content License

The SynthyBoy factory content pack provides **26.2 MB (112 files)** of custom-synthesized acoustic and electronic sound assets, optimized specifically for the Akai MPC hardware platform:

- **Factory Wavetables**: 72 wavetables (18.5 MB) in six categories (Analog, Digital, Spectral, Vocal, Metallic, Noise) covering filter and sync sweeps, PWM stacks, FM and phase distortion, additive series, vowels and choirs, inharmonic metals and frozen noise.
- **Factory Samples**: 12 audio samples (44.1 kHz, 16-bit PCM) including sustained bowed strings, breath noise chiffs, chord progressions, drum breaks, crystal glass, metal gongs, acoustic marimbas, plucks, rising risers, vinyl noise beds, and vocal sweeps.
- **Multisample Instruments**: 3 key-mapped SFZ instruments with multi-zone recordings and velocity layer switching.
- **Impulse Responses**: 3 stereo 24-bit impulse responses for the zero-latency Convolve effect (guitar amp cabinet, studio plate, acoustic small room).

### License & Attribution

All content included in the factory content pack is generated algorithmically from mathematical equations and physical modeling synthesis using `scripts/gen-content-pack.py`. It contains **no third-party samples, copyrighted audio recordings, or proprietary materials**.

As declared in the generator scripts and package manifests (`contentpack.json`):

> **License:** Creative Commons Zero v1.0 Universal (`CC0-1.0`)  
> **Attribution:** Safian Szabolcs  
> **Specification:** Synthesized from nothing (CC0-1.0, no third-party material).

You are completely free to use all factory wavetables, samples, instruments, presets, and impulse responses in commercial music releases, film scores, broadcast productions, sound design projects, and live performances with no royalty obligations or licensing restrictions.

---

## 2. Factory Wavetables (`com.safian.synthyboy.factory-wavetables`)

The wavetable pack contains **72 wavetables in six categories**, one sub folder each: `Analog`, `Digital`, `Spectral`, `Vocal`, `Metallic`, `Noise` (12 tables each). Every table is a 16-bit mono PCM WAV with 2048 samples per single-cycle frame and, with a few stepped exceptions, 64 frames, conforming to the format read by `core/engine-own/osc/content_set.cpp`. The manifest gives every file a `category` and a human `title`; the wavetable browser lists them by these.

They are designed for the **Wavetable oscillator** (Osc A, B, C) and can also serve as spectral filter tables in the **Spectral oscillator**. Every table is a *scan*: moving the frame position (with an LFO, an envelope, velocity or a macro) sweeps the sound smoothly from the first frame to the last, and the loudness stays roughly even along the way. Vocal tables are made for one pitch (stated in the description): play them within about an octave of it.

All tables are written by `scripts/gen-content-pack.py` from small JSON recipes (`content-src/wavetables/<Category>/<name>.json`); see `docs/CONTENT-PACKS.md` for how to add a table or an add-on pack, and `docs/release/WAVETABLE-PACK.md` for the measurements.

<!-- wavetable-catalogue:begin -->

_Edited by hand (the catalogue generator was removed on 2026-11-28)._

### Analog (12 tables)

| Table | File | Frames | What the scan does |
|---|---|---|---|
| Octave Saw Stack | `Analog/octave_saw_stack.wav` | 64 | A saw to which a saw an octave up and one two octaves up are added (the 2x weight 0 to 1, the 4x weight 0 to 0.8): the layered octaves of an organ-like analog stack; transpose down for a sub-octave blend. |
| Pulse Width Sweep | `Analog/pulse_width_sweep.wav` | 64 | A plain pulse whose width narrows from 50 % (square) to 5 % (thin reedy pulse). |
| PWM Dual Phase | `Analog/pwm_dual_phase.wav` | 64 | Two opposite saws, the second a variable distance behind: the distance breathes 8 % to 50 % to 8 %, pulse to thin comb. |
| PWM Saw Blend | `Analog/pwm_saw_blend.wav` | 64 | A pulse whose width sweeps 50 % to 4 % while a saw fades in under it: the width modulated saw of string machines. |
| PWM Triple Stack | `Analog/pwm_triple_stack.wav` | 64 | Three pulse waves stacked, their widths walking apart from 50 % towards 6 % and 94 %: a PWM unison in one table. |
| Saw Filter Sweep | `Analog/saw_filter_sweep.wav` | 64 | A saw through a resonant 4 pole low pass: the cutoff opens from the 2nd to the 150th harmonic (slow start, fast end), resonance peak riding on it. |
| Saw Soft Clip | `Analog/saw_soft_clip.wav` | 64 | A saw driven into a tanh soft clipper, the drive rising 0.5 to 14: saw to a rounded square-like overdrive. |
| Saw Stack Spread | `Analog/saw_stack_spread.wav` | 64 | Seven saws whose phases fan out from together to +-0.45 cycle: a single saw thins into a staircase and a comb, the supersaw look frozen in one cycle. |
| Sync Saw Sweep | `Analog/sync_saw_sweep.wav` | 64 | Hard sync with a saw slave, ratio 1 to 7: a rougher, brighter sweep than the sine slave. |
| Sync Sine Sweep | `Analog/sync_sine_sweep.wav` | 64 | Hard sync: a sine slave resets every cycle and its ratio rises 1 to 9; the classic searing formant sweep. |
| Triangle Fold | `Analog/triangle_fold.wav` | 64 | A triangle through a folder, the gain 1 to 7.5: triangle, then folded humps appear (the west coast fold on a smooth source). |
| Triangle To Saw | `Analog/triangle_to_saw.wav` | 64 | A cross fade, level matched, from a pure triangle (frame 0) to a saw (frame 63): the odd-only soft tone grows the even partials. |

### Digital (12 tables)

| Table | File | Frames | What the scan does |
|---|---|---|---|
| Bit Reduced Sine | `Digital/bit_reduced_sine.wav` | 64 | A sine with a little saw, quantised to fewer and fewer bits (about 10 down to 1.8): smooth tone to a stepped, gritty one. |
| FM Chain 1-2-3 | `Digital/fm_chain_123.wav` | 64 | A three operator chain: carrier 1, modulator 2 (index 0 to 3) itself modulated by an operator at 3 (index 0 to 2.5): the sweep passes through brass-like and bell-like colours. |
| FM Feedback Saw | `Digital/fm_feedback_saw.wav` | 64 | One operator with feedback 0 to 1.9: sine, saw, noisy saw. |
| FM Index Sweep | `Digital/fm_index_sweep.wav` | 64 | 2:1 FM, the index rising 0 to 9: sine, brass, metal. |
| FM Ratio Ladder | `Digital/fm_ratio_ladder.wav` | 64 | Modulator ratios 1 to 8, eight frames each, the index rising 1 to 5 inside each step: the character jumps at every step and sweeps inside it. |
| FM 3 To 1 Index | `Digital/fm_triple_index.wav` | 64 | FM with the modulator at 3 times the carrier, the index rising 0 to 8 (slow start): sine, hollow odd-ish tone, bright glassy FM. |
| Gauss Formant Sweep | `Digital/gauss_formant_sweep.wav` | 64 | A gaussian windowed cosine burst once per cycle (a digital formant), its centre frequency swept 1.5 to 26 harmonics: a hard, nasal, vowel-less formant sweep. |
| Phase Distortion Knee | `Digital/pd_knee.wav` | 64 | A cosine whose phase has a knee moved from the middle (0.5) to 0.04 of the cycle: sine to a pulse-like pinched wave. |
| Phase Distortion Resonance | `Digital/pd_resonance.wav` | 64 | A sine with a falling window whose frequency is swept 1 to 16 cycles per period: the resonant filter sweep of phase distortion synthesis. |
| Sine Fold Sweep | `Digital/sine_fold_sweep.wav` | 64 | A sine through a wave folder, the gain 0.4 to 14: sine, triangle, dense folded metal. |
| Sine To Square Shaper | `Digital/sine_to_square_shaper.wav` | 64 | A sine pushed through a power-law shaper, the exponent falling 1.0 to 0.06: sine, rounded square, hard square. |
| Step Sequence Morph | `Digital/step_sequence_morph.wav` | 64 | Eight levels per cycle read like a step sequencer pattern that morphs through five random patterns (slewed corners): wave sequencing staircases. |

### Spectral (12 tables)

| Table | File | Frames | What the scan does |
|---|---|---|---|
| Bell Strike | `Spectral/additive_bell_strike.wav` | 64 | Additive bell partials (a hum, a prime, a tierce, a nominal cluster) whose high members fade as the frames go on. |
| Glass Harmonica | `Spectral/additive_glass_harmonica.wav` | 48 | A sparse set of partials (1 3 5 7 ...) that opens one by one and closes again. |
| Bore Odd To Even | `Spectral/bore_odd_to_even.wav` | 64 | A cross fade from odd harmonics only (a cylindrical reed) to the full series (a conical bore), level matched: clarinet colour to saxophone colour. |
| Comb Spectrum Slide | `Spectral/comb_spectrum_slide.wav` | 64 | A comb of period 6 harmonics, depth 90 %, slides across the spectrum of a gently falling series by one full period: the notches walk through the partials. |
| Dispersion Sweep | `Spectral/dispersion_sweep.wav` | 64 | A flat 1/n spectrum whose partial phases rotate with n squared: the magnitudes never change, the waveform sweeps from a saw through chirps to noise-like ripples (a phase-only scan). |
| Even Harmonic Ladder | `Spectral/even_harmonic_ladder.wav` | 64 | The fundamental plus even harmonics only (octave, 4th, 6th ...) up to the 60th: a sine grows an octave-stacked, reedy tone. |
| Harmonic Band Sweep | `Spectral/harmonic_band_sweep.wav` | 64 | A 1/n series with one band of harmonics lifted 24 dB, its centre rising from the 1.5th to the 120th partial (accelerating): a swept bandpass written into the cycle. |
| Harmonic Sieve | `Spectral/harmonic_sieve.wav` | 32 | A saw with only every n-th harmonic kept, n = 1 to 16, two frames each: octaves, fifths, ghost chords. |
| Odd Harmonic Ladder | `Spectral/odd_harmonic_ladder.wav` | 64 | Odd harmonics (1/n) are added one after the other, 1 up to the 60th: a pure sine grows into a hollow square-like tone. |
| Organ Drawbars | `Spectral/organ_drawbars_16.wav` | 31 | Sixteen drawbar registrations of a tonewheel organ with an interpolated frame between each (31 frames); the cycle is the 16 foot fundamental: transpose +12. |
| Prime Harmonics | `Spectral/prime_harmonics.wav` | 64 | Only prime-numbered harmonics (2 3 5 7 11 ...) plus the fundamental, more of them as the frames go on: an irregular, bell-ish spectrum that is still a pitch. |
| Spectral Tilt Sweep | `Spectral/spectral_tilt_sweep.wav` | 64 | The harmonic roll-off steepens from 0.3 (buzzy) to 2.6 (almost a sine): the brightness control as a table. |

### Vocal (12 tables)

| Table | File | Frames | What the scan does |
|---|---|---|---|
| Breathy To Pressed | `Vocal/breathy_to_pressed.wav` | 64 | One vowel (ah, men, 150 Hz) with the voice source changing: steep tilt, boosted fundamental and noise (breathy) to a flat tilt (pressed, shouted). |
| Choir Blend | `Vocal/choir_blend.wav` | 64 | Men, women and children sing the vowel sequence ah, aw, oo, eh, ee, ah together (scattered phases, level matched mix) at 175 Hz. |
| Choir Singers Shift | `Vocal/choir_singers_shift.wav` | 48 | The vowels A, O, U through a male, a female and a child's vocal tract and back: a choir whose singers change (48 frames). |
| Diphthong Series | `Vocal/diphthong_series.wav` | 64 | Eight diphthongs of eight frames each (ah-ih, ah-oo, aw-ih, eh-ih, ee-oo, oo-ah, ae-aw, eh-ah), each gliding from its first to its second vowel; a stepped table, the jump between diphthongs is intended. |
| Vowel Sweep AEIOU | `Vocal/formant_sweep_aeiou.wav` | 64 | The vowels A E I O U in turn through the formants of a voice at 220 Hz (best range A2 to A4). |
| Overtone Duet | `Vocal/overtone_duet.wav` | 64 | Two narrow resonances over the same drone cross in opposite directions: one climbs from the 4th to the 20th partial while the other falls from the 20th to the 4th. |
| Overtone Throat Sweep | `Vocal/overtone_throat_sweep.wav` | 64 | A narrow resonance walks up the harmonic series from the 4th to the 20th partial and back over a fixed drone. |
| Tract Length Sweep | `Vocal/tract_length_sweep.wav` | 64 | The vowel ah with all formants scaled 0.6 to 1.7: a giant to a chipmunk (the vocal tract length), pitch fixed at 150 Hz. |
| Vowel Cycle Children | `Vocal/vowel_cycle_children.wav` | 64 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of children (Peterson and Barney 1952), voice at 262 Hz; best range about an octave around that pitch. |
| Vowel Cycle Men | `Vocal/vowel_cycle_men.wav` | 64 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of men (Peterson and Barney 1952), voice at 130 Hz; best range about an octave around that pitch. |
| Vowel Cycle Women | `Vocal/vowel_cycle_women.wav` | 64 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of women (Peterson and Barney 1952), voice at 220 Hz; best range about an octave around that pitch. |
| Whisper To Voiced | `Vocal/whisper_to_voiced.wav` | 64 | The vowel ah (men, 150 Hz): frame 0 is a whisper (formant shaped noise: random phases and amplitudes), the last frame the voiced pulse train; a level matched cross fade. |

### Metallic (12 tables)

| Table | File | Frames | What the scan does |
|---|---|---|---|
| Bell Pitch Walk | `Metallic/bell_pitch_walk.wav` | 64 | A church-bell family of twelve partials (hum 0.5, prime, tierce 1.18, quint 1.5, nominal 2 ...) whose prime walks from the 2nd to the 12th harmonic: the bell grows from a dull clang to a bright shimmer. |
| Bowl Beating Pairs | `Metallic/bowl_beating_pairs.wav` | 64 | Singing bowl modes (1, 2.7, 5.2, 8.4 ...) each split into a close pair whose spacing opens from 0 to 1.4 harmonics: the slow beating of a rubbed bowl becomes a wide chorus. |
| Cymbal Cloud | `Metallic/cymbal_cloud.wav` | 64 | Thirty-six partials scattered between the 6th and the 160th harmonic (seeded), each wobbling by up to 6 harmonics once over the scan: a shimmering, never repeating metal cloud. |
| Drum Membrane Modes | `Metallic/drum_membrane_modes.wav` | 64 | The modes of an ideal circular membrane (1, 1.59, 2.14, 2.30, 2.65 ...) with the fundamental walking from harmonic 2 to 10: a tom to a metallic tambourine-like drum. |
| Free Bar Modes | `Metallic/free_bar_modes.wav` | 64 | The modes of a free bar (1, 2.756, 5.404, 8.933, 13.34, 18.64 ...) with the fundamental walking from harmonic 1 to 6: vibraphone and marimba bar colours from soft to bright. |
| Gong Modes Swell | `Metallic/gong_modes_swell.wav` | 64 | The thirteen modes of the factory gong sample around the 3rd harmonic; the upper modes swell from 45 % to full weight (bright^n): the bloom of a struck gong. |
| Metal Comb Sweep | `Metallic/metal_comb_sweep.wav` | 64 | A comb sweeps through a saw's spectrum (spacing 3 to 16 harmonics): ringing metal. |
| Random Metal Morph | `Metallic/random_metal_morph.wav` | 64 | Six seeded random sets of 14 inharmonic partials (5th to 150th harmonic) cross faded one into the next: an anvil, a pipe, a can, each blending into the following. |
| Resonant Comb Metal | `Metallic/resonant_comb_metal.wav` | 64 | A falling saw spectrum through a resonant (feedback) comb, delay 0.03 to 0.4 of the cycle, feedback 0.9, phases scattered: sharp ringing peaks that drift like a flanged cymbal. |
| Ring Mod Glide | `Metallic/ring_mod_glide.wav` | 64 | A sine with its octave, ring modulated by a sine whose ratio glides continuously from 1 to 9.5: the sum and difference lines slide through the spectrum (non-integer ratios give inharmonic clang). |
| Ring Mod Ladder | `Metallic/ring_mod_ladder.wav` | 64 | Ring modulation of two sines at the integer ratios 2 to 17, four frames each, the depth rising inside a step. |
| Ring Mod Saw Glide | `Metallic/ring_mod_saw_glide.wav` | 64 | A 24 harmonic saw ring modulated by a sine gliding 0.5 to 12.5 harmonics (with a quarter of the dry saw kept): dense, inharmonic metal that resolves at the integer ratios. |

### Noise (12 tables)

| Table | File | Frames | What the scan does |
|---|---|---|---|
| Breath Air Bumps | `Noise/breath_air_bumps.wav` | 64 | Noise through two broad resonances (like the chiff and tube of a flute): the lower rises from the 18th to the 60th harmonic, the upper from the 70th to the 190th. |
| Colour Tilt Sweep | `Noise/colour_tilt_sweep.wav` | 64 | One frozen noise spectrum whose tilt moves from -1 (blue, hissy) through white and pink to 2.2 (brown, rumbling): the colour of noise as a scan. |
| Crackle Pulses | `Noise/crackle_pulses.wav` | 64 | Three clicks per cycle growing to 64 clicks (seeded positions, signs and a short decay tail): a few ticks become a crackling, then a dense rattle. |
| Frozen Noise Variations | `Noise/frozen_noise_variations.wav` | 64 | Four different frozen noises (seeds 11 to 14) cross faded one into the next: the texture changes character while the colour stays the same. |
| Noise Band Sweep | `Noise/noise_band_sweep.wav` | 64 | Frozen white noise with one narrow band (a quarter octave) lifted 30 dB, its centre rising from the 3rd to the 300th harmonic: whistling wind sweeping up. |
| Noise Burst Widening | `Noise/noise_burst_widening.wav` | 64 | A gaussian burst of noise once per cycle that widens from 6 % to the whole cycle: a click, a puff, a steady noise (the scan is the duty cycle of a noise gate). |
| Noise Notch Sweep | `Noise/noise_notch_sweep.wav` | 64 | Frozen noise (slightly dark) with a deep notch (34 dB) sweeping from the 4th to the 250th harmonic: the phaser-on-noise sound. |
| Noise To Saw | `Noise/noise_to_saw.wav` | 64 | A level matched cross fade from frozen noise to a 200 harmonic saw: noise that gathers into a buzzing tone. |
| Noise To Sine | `Noise/noise_to_sine.wav` | 64 | A level matched cross fade from frozen noise to a pure sine: a breath that condenses into a tone. |
| Pink Lowpass Sweep | `Noise/pink_lowpass_sweep.wav` | 64 | Frozen pink noise through a resonant 4 pole low pass whose cutoff falls from the 300th to the 2nd harmonic (quickly at first): a filter closing on noise. |
| Rumble To Hiss | `Noise/rumble_to_hiss.wav` | 64 | A cross fade, level matched, from low rumble (noise below the 14th harmonic) to bright hiss (noise above the 120th): the energy moves from the bottom to the top. |
| Sparse Spectrum Fill | `Noise/sparse_spectrum_fill.wav` | 64 | A noise spectrum with only 2 % of the harmonics present, filling to 100 %: a few ringing lines, a crackling comb, then full noise. |

<!-- wavetable-catalogue:end -->

---

## 3. Factory Samples (`com.safian.synthyboy.factory-samples`)

The sample pack includes **12 audio recordings/textures**, **3 multisample instruments**, and **3 impulse responses** recorded at 44.1 kHz, peak-normalized to -1 dBFS headroom with embedded `smpl` metadata.

### Audio Samples (12 Files)

| File Name | Length | Ch | Target Oscillator & Usage | Description & Sound Character |
|---|---|---|---|---|
| `bowed_tone.wav` | 3.15 s | 1 | **Sample / Granular / Spectral** | Bowed string tone tuned to MIDI 60 with 5 Hz natural vibrato and seamless loop points (14.3%..77.7%); delivers expressive solo strings and sustained orchestral pads. |
| `breath_noise_burst.wav` | 1.20 s | 1 | **Sample / Granular** | Three resonant bands of acoustic breath noise; provides realistic wind transients (flute chiffs, pipe organ air) on non-legato note attacks. |
| `chord_pad_sections.wav` | 8.00 s | 1 | **Granular / Spectral** | Evolving 4-chord harmonic progression (Cmaj7, Amin7, Fmaj7, G6; 2 seconds each, crossfaded); ideal for ambient chord clouds and spectral morphing. |
| `drum_loop_120bpm.wav` | 4.00 s | 1 | **Sample (Slice) / Granular** | 2-bar 4/4 acoustic drum break at 120 BPM (kick, snare, ghost snares, closed and open hats); sliced across 32 keys in Slice mode or glitched via granular playback. |
| `glass_ping.wav` | 3.00 s | 1 | **Sample / Granular / Spectral** | Struck crystal wine glass at MIDI 72 with six inharmonic partials and natural acoustic beating; creates tuned wine glass harps and crystalline plucks. |
| `gong_metal.wav` | 4.00 s | 1 | **Sample / Granular / Spectral** | Deep metal gong tuned to MIDI 36 with 13 inharmonic modes and high-frequency sizzle; used for backward crescendos and granular metal swarms. |
| `mallet_marimba.wav` | 1.80 s | 1 | **Sample / Granular** | Single struck marimba bar tuned to MIDI 60 with physical acoustic ratios (1 : 3.99 : 10.1); delivers round wooden percussive plucks. |
| `pluck_string.wav` | 2.50 s | 1 | **Sample / Granular** | Additive acoustic plucked string with natural high-frequency decay, tuned to MIDI 60; ideal for harp plucks and granular frozen string drones. |
| `rising_texture.wav` | 6.00 s | 1 | **Granular / Sample** | 6-second tension riser featuring noise band climbing 250 Hz to 7 kHz and a 2-octave gliding harmonic stack; builds tension before drops. |
| `vinyl_noise_bed.wav` | 4.00 s | 2 | **Sample (Texture)** | Continuous stereo vinyl record noise with hiss, turntable rumble, 50 Hz electrical hum, crackles, and pops; adds vintage warmth under keys and pads. |
| `voice_sweep_aeiou.wav` | 5.00 s | 1 | **Granular / Spectral** | Singing voice at 130.8 Hz (MIDI 48) articulating A, E, I, O, U over 5 seconds with natural vibrato and breath; scrubbed via mod wheel or granular playhead. |
| `vowel_pad.wav` | 4.10 s | 1 | **Spectral / Granular / Sample** | Three voices singing sustained A and O choral vowels at MIDI 60 with seamless vibrato loop; creates lush sacred choirs and spectral freezes. |

### Multisample SFZ Instruments (3 Instruments)

The Multisample oscillator (`oscN.type` Multisample) loads key-mapped instruments formatted as `.sfz` files with associated audio sample folders:

| Instrument | Zones | Velocity Layers | Files | Sound Character & Playing Tips |
|---|---|---|---|---|
| `pluck_nylon` | 6 key zones | 1 layer | 6 WAVs | Acoustic nylon-string guitar recorded across six keyboard zones (roots MIDI 36, 45, 54, 63, 72, 81); responds with authentic guitar body resonance. |
| `mallet_marimba` | 5 key zones | 2 layers | 10 WAVs | Acoustic marimba with dynamic velocity switching: soft playing (vel 1..84) triggers round, dark wooden bars; hard strikes (vel 85..127) trigger sharp mallet knock. |
| `glass_bells` | 4 key zones | 1 layer | 4 WAVs | Struck wine glass samples mapped across four octave registers (roots MIDI 48, 60, 72, 84); delivers ethereal tuned glass bells. |

### Impulse Responses for Convolve (3 Files)

The **Convolve** effect slot incorporates zero-latency partition convolution. All impulse responses are stereo, 24-bit PCM at 44.1 kHz, normalized to an L2-norm of 1.0 (so that wet signal level matches dry signal level at `convolve.ir_gain` 0 dB):

| Impulse Response | Length | Size | Sound Character & Recommended Preset Use |
|---|---|---|---|
| `cabinet_colour.wav` | 0.09 s (90 ms) | 23 KB | Open-back guitar amplifier cabinet profile (low-end roll-off below 70 Hz, 95 Hz cabinet resonance thump, dual-mic presence peaks at 2.5 kHz and 3.3 kHz, 5.2 kHz lowpass roll-off); adds warm speaker coloration to electric pianos and basslines. |
| `plate.wav` | 1.45 s | 375 KB | Classic bright studio plate reverb with instant high-density diffusion, RT60 of 1.25 seconds, and a high-frequency sheen decaying in 0.35 seconds; provides sparkling space for vocals, strings, and bells. |
| `small_room.wav` | 0.70 s | 181 KB | Intimate acoustic room with eight distinct early reflections in the first 46 milliseconds and a smooth darkening 0.42 s reverberant tail; glues drum breaks and acoustic mallets into a realistic physical space. |

---

## 4. How to Browse and Load Content on Oscillator Screens

SynthyBoy provides a unified **Content Browser** accessible directly from the touchscreen UI (overlay build) on the **Osc A**, **Osc B**, and **Osc C** pages.

### Loading Wavetables (Wavetable Oscillator): the wavetable browser
1. Select the oscillator tab (**Osc A**, **Osc B**, or **Osc C**) and set the type to **WAVE**.
2. Tap the name in the table selector (or its list button). It works on the Osc A / B / C pages and on the oscillator third of the Play page (which opens the full screen first).
3. The wavetable browser opens as a list over the screen. It opens in the **category** of the table you are playing, with that table marked. The bar at the top is the category name with a back arrow: tap it to see all categories (**Basic**, **Morph**, the categories of the installed packs such as Analog or Vocal, the folders you made yourself, **User**), tap a category to see its tables.
4. Tap a table to play it. A built-in table is selected at once. A table from a pack or from your own files is read from the card or the drive, and the amber bar at the top of the page shows while that happens; the old table keeps playing until the new one is ready. The selector then shows the name of the table.
5. A table is made of **frames**; scan through them with **WT POS** (the frame position). Tables with one frame are single cycles.
6. **Stepping**: with a built-in table the arrows of the selector (and the Q knob that is on it) step through the 15 built-in tables. With a table from a file the arrows and the knob step to the **previous / next table of the same category**, so you can audition a whole category with the knob. At the first and the last table of the category nothing happens (no wrap). To go back to the built-in tables, open the browser.
7. A file that cannot be used (too large, empty, not a table) says why in its row ("not a wavetable file: ...") and the display shows the same for a few seconds; the sound does not change.

### Adding your own wavetables
Copy WAV files to the folder **`SynthyBoy/Wavetables/`** at the top level of the **SD card** (`/sdcard/SynthyBoy/Wavetables/`), of a **USB drive** (`<drive>/SynthyBoy/Wavetables/`), or inside the plugin's own **`Content`** folder (`Content/SynthyBoy/Wavetables/` next to the VST folder; the way to add files on a machine without a card). Create the folders if they are not there.

- A file directly in `SynthyBoy/Wavetables/` is listed in the category **User**.
- A folder inside it is a **category** named like the folder, and its WAV files are the tables of it (one folder level; files in deeper folders are not listed). The same folder name on the card and on a drive is one category.
- There is no index to build and no restart: the browser lists what is there **each time it opens**.
- A preset that uses one of your files remembers it by folder and name (and a checksum). On another device the same file under the same folder name makes it sound the same; without the file the oscillator plays a **saw** and the display says "content missing: <name>".
- Which files work: WAV, **mono** (a stereo file uses its left channel), 16, 24 or 32 bit integer or 32 bit float, up to **16 MB**. The usual "wavetable WAV" is **N frames of 2048 samples one after the other** (up to 256 frames; more are cut off). A file with a `clm` chunk (the Serum style "<!>1024 ..." chunk) uses the frame size in the chunk (1024, 4096 and so on are resampled to 2048). A file of 16 to 4096 samples whose length is not a multiple of 2048 is one **single cycle** and is stretched to one frame. Anything else (a long recording, 8 bit, compressed WAV, FLAC, MP3) is not a table and is refused with a reason.
- A **pack** (folder with a `contentpack.json` of kind `wavetables` in `Content/packs/<pack>/`) is listed too: the files are grouped by the `category` of the manifest entry or, without one, by the folder they are in; `title` gives the shown name.

### Loading Samples, Grains, and Spectra (Sample, Granular, Spectral Oscillators)
1. Set the oscillator type to **SAMPLE** (Type 1), **GRAIN** (Type 2), or **SPEC** (Type 3).
2. If no file is loaded, a prominent **LOAD SAMPLE** button appears over the display area. If a file is already loaded, the source stepper displays the filename.
3. Tap **LOAD SAMPLE** or tap the source name/dropdown to open the **Content Browser**:
   - **Header Bar**: Displays the current directory path. Tapping the up-arrow button (`..`) navigates up to the parent directory.
   - **Root Directories**: Tapping the top-level roots takes you directly to:
     - **PLUGIN CONTENT**: Internal factory packs (`Content/packs/`).
     - **SD CARD**: Hardware SD card mount (`/sdcard`).
     - **USB <NAME>**: Any external USB drives or thumb sticks mounted under `/media`.
   - **File Filtering**: The browser automatically displays only compatible `.wav` audio files. Incompatible formats (FLAC, MP3, TXT) are omitted.
4. Tap any file to load it. The display thread loads and decodes the file asynchronously in the background. The audio thread never waits, ensuring completely glitch-free playback.
5. The display updates with the waveform overview, loop points, and real-time playhead indicators.

### Loading Multisample SFZ Instruments
1. Set the oscillator type to **MULTI** (Type 4).
2. If empty, the display presents a **LOAD INSTRUMENT** button.
3. Tap the button to open the Content Browser. Navigate into `instruments/` within `PLUGIN CONTENT` or your external drive.
4. Select any `.sfz` file. SynthyBoy parses the key zones, root notes, and velocity ranges.
5. Alternatively, selecting a single `.wav` file automatically creates a single-zone instrument mapped across the entire 128-note MIDI range.

---

## 5. Engine Fallback Behavior When Files Are Missing

SynthyBoy is designed with robust error-handling to prevent plugin crashes or frozen audio threads when referenced content files cannot be located (for instance, when opening a project on an MPC without external content packs):

1. **Missing Wavetables**: If a preset references a factory or user wavetable that cannot be loaded or fails SHA-256 validation, the engine automatically falls back to a clean **sawtooth** waveform.
2. **Missing Sample, Granular, Spectral, or Multisample Files**: In accordance with the engine specification ("no fallback tone"), an unresolvable sample file causes that specific oscillator to remain **silent** rather than emitting an unpredictable harsh tone.
3. **Missing Convolve Impulse Responses**: If an impulse response WAV cannot be located, the Convolve module bypasses convolution and passes the **dry input signal** cleanly through to subsequent effect slots.
4. **Dual-Layer Factory Sound Design**: To ensure that all Sound Set Batch 2 presets remain auditionable and musically useful even without the content pack installed, every sample-based preset incorporates a secondary wavetable layer (10% to 30% level). Without the pack, presets play at -18 dB to -32 dBFS rather than muting entirely.

---

## 6. Installation Paths

On standalone Akai MPC hardware, factory content packs are installed alongside factory presets within the plugin bundle directory:

```text
Safian - VST - SynthyBoy/
├── Content/
│   ├── packs/
│   │   ├── com.safian.synthyboy.factory-wavetables/
│   │   │   ├── contentpack.json
│   │   │   └── *.wav (16 wavetable files)
│   │   └── com.safian.synthyboy.factory-samples/
│   │       ├── contentpack.json
│   │       ├── samples/ (*.wav)
│   │       ├── instruments/ (<name>/<name>.sfz + samples/)
│   │       └── impulses/ (*.wav)
│   └── presets/
│       ├── Bass/
│       ├── FX/
│       ├── Keys/
│       ├── Lead/
│       ├── Pad/
│       └── Pluck/
├── Presets/ (.xpl files for MPC Sounds browser)
├── Plugin Skins/
├── plugin.json
└── VST/
    ├── synthyboy-arm32.so (MPC Key 61, MPC One, MPC Live)
    └── synthyboy-arm64.so (MPC XL)
```
