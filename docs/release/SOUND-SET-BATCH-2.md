# Sound set, batch 2: 24 new presets on the factory content pack

Status: **built, partly measured, NOT HEARD.** Nothing here was listened to (the author cannot hear): every claim below is a measurement of the offline render of the engine the MPC runs (own engine, no Vital), or an intention the measurements support or do not contradict. The owner's ears decide; the lines "Not settled for the ear" name what to listen for first. **Not measured at all in this batch: the CPU of any preset (section 4), the 8-note velocity-127 peak, the five-register, velocity and long-hold report of `scripts/sound-lab.py`, and anything that needs a controller (MPE, the pad pressure on the device).** The reason and the one command that closes the gap are in section 1. Written by `scripts/gen-sound-set-batch2-doc.py` from `docs/release/sound-set-batch2-measurements.json` (`scripts/measure-batch2.py report`).

The 24 presets are added to the set (160 now); the 136 before them are unchanged, byte for byte. Each was built from Init (the default values), not from the generator's template: on average 80 parameters differ from the defaults. Files: `content/presets/<Category>/*.sbpreset`; source: `scripts/factory-presets-batch2.py` and `scripts/batch2/*.py` (data written with `scripts/sb_preset_dsl.py`), names of the macros in `scripts/factory-preset-macro-names.json`, levels in `scripts/factory-preset-levels.json`. They use the factory content pack of section 2 (`content/factory-wavetables`, `content/factory-samples`, made by `scripts/gen-content-pack.py`): a preset names its files by pack id, path and SHA-256 in its `content` section.

| Role | Presets |
|---|---|
| Sample oscillator (6) | Dusty Rhodes Keys (Keys), Glass Ping Pluck (Pluck), Breakbeat Slice Keys (Keys), Bowed MPE Strings (Pad), Reverse Gong Swell (FX), Breath Flute Lead (Lead) |
| Granular oscillator (6) | Chord Cloud Pad (Pad), Vowel Scrub Choir (Lead), Frozen Pluck Drone (Pad), Gong Grain Swarm (FX), Stutter Grain Kit (Pluck), Rising Grain Riser (FX) |
| Spectral oscillator (4) | Spectral Vowel Freeze (Pad), Spectral Bowed Glass (Lead), Spectral Gong Pad (Pad), Spectral Chord Morph (FX) |
| Multisample, key-mapped (3) | Nylon Zone Pluck (Pluck), Marimba Layer Keys (Keys), Glass Zone Bells (Keys) |
| Wavetables of the pack (5) | Reese Dual Phase Bass (Bass), Vowel Table Lead (Lead), Drawbar Organ Keys (Keys), Throat Singer Drone (Pad), Ladder FM Bass (Bass) |

By feature: **Convolve** in Dusty Rhodes Keys (cabinet), Glass Ping Pluck, Vowel Scrub Choir, Frozen Pluck Drone (on bus 1), Reverse Gong Swell, Glass Zone Bells (plate), Breakbeat Slice Keys, Marimba Layer Keys (small room), Reese Dual Phase Bass (cabinet); **buses** in Breakbeat Slice Keys (parallel crush, bus 1), Frozen Pluck Drone (wet-only plate, bus 1), Gong Grain Swarm and Rising Grain Riser (delay and reverb, bus 2), Drawbar Organ Keys (overdrive, bus 1); **voice control lanes** in Chord Cloud Pad (6 steps) and Nylon Zone Pluck (8 steps); **scales** in Glass Zone Bells (Hirajoshi on D) and Throat Singer Drone (Phrygian dominant on D); **MPE** in Bowed MPE Strings; **pad pressure** on the held ones (Bowed MPE Strings, Breath Flute Lead, Chord Cloud Pad, Vowel Scrub Choir, Frozen Pluck Drone, Gong Grain Swarm, Spectral Vowel Freeze, Spectral Bowed Glass, Spectral Gong Pad, Spectral Chord Morph, Reese Dual Phase Bass, Vowel Table Lead, Throat Singer Drone, Ladder FM Bass). The name of the Reese starts with the sub-style word ("Reese ..."), so that a browser sorted by name groups it; the folders are the six that exist.

## 1. How it was measured, and what could not be

**The rule of this batch: no build, no test suite.** So the tools were the ones already built for batch 1 (`work-sound-set/build-sound/`), and they have a hole: **`synthyboy_sound_lab` and `synthyboy_tier_bench` do not set a content store** (they call `engine->setContent` only), so in them a Sample / Granular / Spectral / Multisample oscillator is silent and Convolve absent. Only `synthyboy_preset_render` has `--content-dir`. Their sources now read `$SYNTHYBOY_CONTENT_DIR/<pack id>/<file>` (`tools/sound-lab/sound_lab.cpp`, `bench/tier_bench.cpp`; `scripts/sound-lab.py` links the factory packs there by itself): **after a rebuild, `python3 scripts/sound-lab.py report content/presets/*/<the 24 files> --json out.json` and `synthyboy_tier_bench --preset-dir` (with `SYNTHYBOY_CONTENT_DIR` set) measure the rest.** I did not rebuild.

What was measured instead (`scripts/measure-batch2.py`, which runs `synthyboy_preset_render --engine own --content-dir ... --sr 44100` and analyses the WAVs it writes with the functions of `scripts/sound-lab.py`):

- **Level**: the RMS of the loudest 300 ms of a four note chord (MIDI 60 64 67 72, or 36 40 43 48 for a Bass) at velocity 1.0 held 1 s, the measure of `scripts/tune-preset-levels.py`, reference -20 dBFS. `master.volume` is tuned to -20.0 on tier `high` (the XL's profile is `high,voices=16,control=economy`; the tier of the renderer has the quality of `high` without the voice and control limits). As in batch 1 the level is taken at the sustain: for a preset whose envelope 1 attack is over 150 ms the level render is a copy with the attack cut to 5 ms (`LEVEL_OVERRIDES` in the script; for Rising Grain Riser also the filter envelope and the last 28 % of the texture, the top of the build). The table shows both: the level of the real preset at 1 s and at the sustain.
- **Peak**: the peak of the same four-note chord (velocity 1.0), raw and after the +18 dB of the MPC output stage. This is *not* the eight-note velocity-127 chord of batch 1: that one needs `sound-lab.py`; eight notes are about 3 dB higher than four. The conflict between the level and the stage (section 2 of batch 1) is the same: every preset sits at -20 dBFS RMS, so a chord peak of -1.5 .. -14.7 dBFS raw = 16.5 .. 3.3 dBFS at the stage.
- **Stereo, clean**: of the single note (MIDI 60, velocity 0.8, 1.5 s held and its tail; MIDI 36 for a Bass): correlation, mono sum against stereo level and the worst 1/3 octave band, DC, click ratio, spectral centroid and its movement. The tail is the time after the note off until under -90 dBFS.
- **Response**: for every macro, the single note with the macro at 0 and at 100 (a copy of the preset with the macro's value changed), for the wheel, the pad pressure and the velocity routes the same with the routes re-pointed at an unused macro (the route law is the same: 0 .. 1 of the source times the amount), as the distance of the two renders (the measures of batch 1: mean difference of the 1/3 octave bands, level, centroid, side level, beat, HF, LF, fine structure; audible when one of: distance >= 3 dB, level >= 3 dB, centroid >= 0.3 oct, side >= 3 dB, beat >= 1 dB, HF or LF >= 4 dB, fine >= 6 dB), the larger of the held note (0.1 .. 1.4 s) and the whole render with its tail (to 3 s). A macro of the Zone Shift kind (it moves the zone lookup of a key) is measured on the Bass note (MIDI 36), because on MIDI 60 the lookup stays in the same zone. **Macros that act on a rate (an LFO's rate, a vibrato), on the speed of a scan or on the texture of grains do not show in a spectral distance** and are listed as such with the amount of their route; they were not heard.
- **Key 61** is the `low` tier of the same renderer: the level and the chord peak are given for it; the sounds that lose Convolve there (all the Convolve presets) are noted.
- **Without the content pack**: the same presets rendered with an empty content store. The engine does not fall back to anything: a wavetable that is not there becomes the saw, a Sample / Granular / Spectral / Multisample oscillator is silent and Convolve is bypassed (section 7, item 1). So every preset that has a Sample-type main oscillator also has a layer of an ordinary wavetable oscillator, and the table shows what remains.
- **Checked on the way with `synthyboy_sound_lab` (no content needed)**: the voice control lanes (a lane value of 80 on pan: 16 dB to one side, in the pattern of the steps; cutoff +-60: the centroid of a saw moves 767 / 1579 / 2994 Hz over three steps; detune +-80: 249.6 / 261.8 / 273.9 Hz), the buses (a delay on Bus 1 with the oscillator's destination None and its send at 100: only the echo is there, at the level the dry signal had: -43 dBFS both; with no send: nothing of it) and the scale conform (Hirajoshi on D maps C, C#, D# ... to A#, D, D, D ...; Phrygian dominant on D maps C# to C, E to Eb, G# to G).

## 2. The factory content pack

`scripts/gen-content-pack.py` synthesises everything (numpy and the standard library; a fixed seed per file, deterministic; `--check` and the test `synthyboy_factory_content_reproducible` fail if the files differ from it). No third-party material, CC0-1.0, attribution Safian Szabolcs. Two packs in the format of `tools/contentpack/contentpack.py` (a `contentpack.json` with the SHA-256 and size of every file; tests `synthyboy_factory-wavetables_verifies`, `synthyboy_factory-samples_verifies`):

- `content/factory-wavetables/` = pack `com.safian.synthyboy.factory-wavetables`, kind wavetables.
- `content/factory-samples/` = pack `com.safian.synthyboy.factory-samples`, kind samples: `samples/` (12 files), `instruments/<name>/<name>.sfz` with `samples/` below it (3 instruments), `impulses/` (3 files).

**Size: 26.2 MB (26188945 bytes) for 110 files** (wavetables 18.5 MB, samples, instruments and impulses 7.7 MB), the wavetables under the 25 MB line. They are committed (the convention of `content/presets`) and `scripts/build-mpc.sh` copies them to `Content/packs/<pack id>/` next to `Content/presets`, where the plugin's content store (`UserContentStore`, `addPackRoot (Content/packs)`) looks for the files a preset names. `plugin.json` does not declare them yet (a release decision: `contentpack.py build-all` would then build two more zips); the desktop shells have no pack root, so the batch 2 presets that need files are for the MPC build today.

**Wavetables (72, mono 16 bit WAV, 2048 samples per frame, the format `core/engine-own/osc/content_set.cpp` reads: a file of N x 2048 samples is a table of N frames):**

| File | Frames | Bytes | What it is |
|---|---|---|---|
| `Analog/octave_saw_stack.wav` | 64 | 262188 | A saw to which a saw an octave up and one two octaves up are added (the 2x weight 0 to 1, the 4x weight 0 to 0.8): the layered octaves of an organ-like analog stack; transpose down for a sub-octave blend. |
| `Analog/pulse_width_sweep.wav` | 64 | 262188 | A plain pulse whose width narrows from 50 % (square) to 5 % (thin reedy pulse). |
| `Analog/pwm_dual_phase.wav` | 64 | 262188 | Two opposite saws, the second a variable distance behind: the distance breathes 8 % to 50 % to 8 %, pulse to thin comb. |
| `Analog/pwm_saw_blend.wav` | 64 | 262188 | A pulse whose width sweeps 50 % to 4 % while a saw fades in under it: the width modulated saw of string machines. |
| `Analog/pwm_triple_stack.wav` | 64 | 262188 | Three pulse waves stacked, their widths walking apart from 50 % towards 6 % and 94 %: a PWM unison in one table. |
| `Analog/saw_filter_sweep.wav` | 64 | 262188 | A saw through a resonant 4 pole low pass: the cutoff opens from the 2nd to the 150th harmonic (slow start, fast end), resonance peak riding on it. |
| `Analog/saw_soft_clip.wav` | 64 | 262188 | A saw driven into a tanh soft clipper, the drive rising 0.5 to 14: saw to a rounded square-like overdrive. |
| `Analog/saw_stack_spread.wav` | 64 | 262188 | Seven saws whose phases fan out from together to +-0.45 cycle: a single saw thins into a staircase and a comb, the supersaw look frozen in one cycle. |
| `Analog/sync_saw_sweep.wav` | 64 | 262188 | Hard sync with a saw slave, ratio 1 to 7: a rougher, brighter sweep than the sine slave. |
| `Analog/sync_sine_sweep.wav` | 64 | 262188 | Hard sync: a sine slave resets every cycle and its ratio rises 1 to 9; the classic searing formant sweep. |
| `Analog/triangle_fold.wav` | 64 | 262188 | A triangle through a folder, the gain 1 to 7.5: triangle, then folded humps appear (the west coast fold on a smooth source). |
| `Analog/triangle_to_saw.wav` | 64 | 262188 | A cross fade, level matched, from a pure triangle (frame 0) to a saw (frame 63): the odd-only soft tone grows the even partials. |
| `Digital/bit_reduced_sine.wav` | 64 | 262188 | A sine with a little saw, quantised to fewer and fewer bits (about 10 down to 1.8): smooth tone to a stepped, gritty one. |
| `Digital/fm_chain_123.wav` | 64 | 262188 | A three operator chain: carrier 1, modulator 2 (index 0 to 3) itself modulated by an operator at 3 (index 0 to 2.5): the sweep passes through brass-like and bell-like colours. |
| `Digital/fm_feedback_saw.wav` | 64 | 262188 | One operator with feedback 0 to 1.9: sine, saw, noisy saw. |
| `Digital/fm_index_sweep.wav` | 64 | 262188 | 2:1 FM, the index rising 0 to 9: sine, brass, metal. |
| `Digital/fm_ratio_ladder.wav` | 64 | 262188 | Modulator ratios 1 to 8, eight frames each, the index rising 1 to 5 inside each step: the character jumps at every step and sweeps inside it. |
| `Digital/fm_triple_index.wav` | 64 | 262188 | FM with the modulator at 3 times the carrier, the index rising 0 to 8 (slow start): sine, hollow odd-ish tone, bright glassy FM. |
| `Digital/gauss_formant_sweep.wav` | 64 | 262188 | A gaussian windowed cosine burst once per cycle (a digital formant), its centre frequency swept 1.5 to 26 harmonics: a hard, nasal, vowel-less formant sweep. |
| `Digital/pd_knee.wav` | 64 | 262188 | A cosine whose phase has a knee moved from the middle (0.5) to 0.04 of the cycle: sine to a pulse-like pinched wave. |
| `Digital/pd_resonance.wav` | 64 | 262188 | A sine with a falling window whose frequency is swept 1 to 16 cycles per period: the resonant filter sweep of phase distortion synthesis. |
| `Digital/sine_fold_sweep.wav` | 64 | 262188 | A sine through a wave folder, the gain 0.4 to 14: sine, triangle, dense folded metal. |
| `Digital/sine_to_square_shaper.wav` | 64 | 262188 | A sine pushed through a power-law shaper, the exponent falling 1.0 to 0.06: sine, rounded square, hard square. |
| `Digital/step_sequence_morph.wav` | 64 | 262188 | Eight levels per cycle read like a step sequencer pattern that morphs through five random patterns (slewed corners): wave sequencing staircases. |
| `Metallic/bell_pitch_walk.wav` | 64 | 262188 | A church-bell family of twelve partials (hum 0.5, prime, tierce 1.18, quint 1.5, nominal 2 ...) whose prime walks from the 2nd to the 12th harmonic: the bell grows from a dull clang to a bright shimmer. |
| `Metallic/bowl_beating_pairs.wav` | 64 | 262188 | Singing bowl modes (1, 2.7, 5.2, 8.4 ...) each split into a close pair whose spacing opens from 0 to 1.4 harmonics: the slow beating of a rubbed bowl becomes a wide chorus. |
| `Metallic/cymbal_cloud.wav` | 64 | 262188 | Thirty-six partials scattered between the 6th and the 160th harmonic (seeded), each wobbling by up to 6 harmonics once over the scan: a shimmering, never repeating metal cloud. |
| `Metallic/drum_membrane_modes.wav` | 64 | 262188 | The modes of an ideal circular membrane (1, 1.59, 2.14, 2.30, 2.65 ...) with the fundamental walking from harmonic 2 to 10: a tom to a metallic tambourine-like drum. |
| `Metallic/free_bar_modes.wav` | 64 | 262188 | The modes of a free bar (1, 2.756, 5.404, 8.933, 13.34, 18.64 ...) with the fundamental walking from harmonic 1 to 6: vibraphone and marimba bar colours from soft to bright. |
| `Metallic/gong_modes_swell.wav` | 64 | 262188 | The thirteen modes of the factory gong sample around the 3rd harmonic; the upper modes swell from 45 % to full weight (bright^n): the bloom of a struck gong. |
| `Metallic/metal_comb_sweep.wav` | 64 | 262188 | A comb sweeps through a saw's spectrum (spacing 3 to 16 harmonics): ringing metal. |
| `Metallic/random_metal_morph.wav` | 64 | 262188 | Six seeded random sets of 14 inharmonic partials (5th to 150th harmonic) cross faded one into the next: an anvil, a pipe, a can, each blending into the following. |
| `Metallic/resonant_comb_metal.wav` | 64 | 262188 | A falling saw spectrum through a resonant (feedback) comb, delay 0.03 to 0.4 of the cycle, feedback 0.9, phases scattered: sharp ringing peaks that drift like a flanged cymbal. |
| `Metallic/ring_mod_glide.wav` | 64 | 262188 | A sine with its octave, ring modulated by a sine whose ratio glides continuously from 1 to 9.5: the sum and difference lines slide through the spectrum (non-integer ratios give inharmonic clang). |
| `Metallic/ring_mod_ladder.wav` | 64 | 262188 | Ring modulation of two sines at the integer ratios 2 to 17, four frames each, the depth rising inside a step. |
| `Metallic/ring_mod_saw_glide.wav` | 64 | 262188 | A 24 harmonic saw ring modulated by a sine gliding 0.5 to 12.5 harmonics (with a quarter of the dry saw kept): dense, inharmonic metal that resolves at the integer ratios. |
| `Noise/breath_air_bumps.wav` | 64 | 262188 | Noise through two broad resonances (like the chiff and tube of a flute): the lower rises from the 18th to the 60th harmonic, the upper from the 70th to the 190th. |
| `Noise/colour_tilt_sweep.wav` | 64 | 262188 | One frozen noise spectrum whose tilt moves from -1 (blue, hissy) through white and pink to 2.2 (brown, rumbling): the colour of noise as a scan. |
| `Noise/crackle_pulses.wav` | 64 | 262188 | Three clicks per cycle growing to 64 clicks (seeded positions, signs and a short decay tail): a few ticks become a crackling, then a dense rattle. |
| `Noise/frozen_noise_variations.wav` | 64 | 262188 | Four different frozen noises (seeds 11 to 14) cross faded one into the next: the texture changes character while the colour stays the same. |
| `Noise/noise_band_sweep.wav` | 64 | 262188 | Frozen white noise with one narrow band (a quarter octave) lifted 30 dB, its centre rising from the 3rd to the 300th harmonic: whistling wind sweeping up. |
| `Noise/noise_burst_widening.wav` | 64 | 262188 | A gaussian burst of noise once per cycle that widens from 6 % to the whole cycle: a click, a puff, a steady noise (the scan is the duty cycle of a noise gate). |
| `Noise/noise_notch_sweep.wav` | 64 | 262188 | Frozen noise (slightly dark) with a deep notch (34 dB) sweeping from the 4th to the 250th harmonic: the phaser-on-noise sound. |
| `Noise/noise_to_saw.wav` | 64 | 262188 | A level matched cross fade from frozen noise to a 200 harmonic saw: noise that gathers into a buzzing tone. |
| `Noise/noise_to_sine.wav` | 64 | 262188 | A level matched cross fade from frozen noise to a pure sine: a breath that condenses into a tone. |
| `Noise/pink_lowpass_sweep.wav` | 64 | 262188 | Frozen pink noise through a resonant 4 pole low pass whose cutoff falls from the 300th to the 2nd harmonic (quickly at first): a filter closing on noise. |
| `Noise/rumble_to_hiss.wav` | 64 | 262188 | A cross fade, level matched, from low rumble (noise below the 14th harmonic) to bright hiss (noise above the 120th): the energy moves from the bottom to the top. |
| `Noise/sparse_spectrum_fill.wav` | 64 | 262188 | A noise spectrum with only 2 % of the harmonics present, filling to 100 %: a few ringing lines, a crackling comb, then full noise. |
| `Spectral/additive_bell_strike.wav` | 64 | 262188 | Additive bell partials (a hum, a prime, a tierce, a nominal cluster) whose high members fade as the frames go on. |
| `Spectral/additive_glass_harmonica.wav` | 48 | 196652 | A sparse set of partials (1 3 5 7 ...) that opens one by one and closes again. |
| `Spectral/bore_odd_to_even.wav` | 64 | 262188 | A cross fade from odd harmonics only (a cylindrical reed) to the full series (a conical bore), level matched: clarinet colour to saxophone colour. |
| `Spectral/comb_spectrum_slide.wav` | 64 | 262188 | A comb of period 6 harmonics, depth 90 %, slides across the spectrum of a gently falling series by one full period: the notches walk through the partials. |
| `Spectral/dispersion_sweep.wav` | 64 | 262188 | A flat 1/n spectrum whose partial phases rotate with n squared: the magnitudes never change, the waveform sweeps from a saw through chirps to noise-like ripples (a phase-only scan). |
| `Spectral/even_harmonic_ladder.wav` | 64 | 262188 | The fundamental plus even harmonics only (octave, 4th, 6th ...) up to the 60th: a sine grows an octave-stacked, reedy tone. |
| `Spectral/harmonic_band_sweep.wav` | 64 | 262188 | A 1/n series with one band of harmonics lifted 24 dB, its centre rising from the 1.5th to the 120th partial (accelerating): a swept bandpass written into the cycle. |
| `Spectral/harmonic_sieve.wav` | 32 | 131116 | A saw with only every n-th harmonic kept, n = 1 to 16, two frames each: octaves, fifths, ghost chords. |
| `Spectral/odd_harmonic_ladder.wav` | 64 | 262188 | Odd harmonics (1/n) are added one after the other, 1 up to the 60th: a pure sine grows into a hollow square-like tone. |
| `Spectral/organ_drawbars_16.wav` | 31 | 127020 | Sixteen drawbar registrations of a tonewheel organ with an interpolated frame between each (31 frames); the cycle is the 16 foot fundamental: transpose +12. |
| `Spectral/prime_harmonics.wav` | 64 | 262188 | Only prime-numbered harmonics (2 3 5 7 11 ...) plus the fundamental, more of them as the frames go on: an irregular, bell-ish spectrum that is still a pitch. |
| `Spectral/spectral_tilt_sweep.wav` | 64 | 262188 | The harmonic roll-off steepens from 0.3 (buzzy) to 2.6 (almost a sine): the brightness control as a table. |
| `Vocal/breathy_to_pressed.wav` | 64 | 262188 | One vowel (ah, men, 150 Hz) with the voice source changing: steep tilt, boosted fundamental and noise (breathy) to a flat tilt (pressed, shouted). |
| `Vocal/choir_blend.wav` | 64 | 262188 | Men, women and children sing the vowel sequence ah, aw, oo, eh, ee, ah together (scattered phases, level matched mix) at 175 Hz. |
| `Vocal/choir_singers_shift.wav` | 48 | 196652 | The vowels A, O, U through a male, a female and a child's vocal tract and back: a choir whose singers change (48 frames). |
| `Vocal/diphthong_series.wav` | 64 | 262188 | Eight diphthongs of eight frames each (ah-ih, ah-oo, aw-ih, eh-ih, ee-oo, oo-ah, ae-aw, eh-ah), each gliding from its first to its second vowel; a stepped table, the jump between diphthongs is intended. |
| `Vocal/formant_sweep_aeiou.wav` | 64 | 262188 | The vowels A E I O U in turn through the formants of a voice at 220 Hz (best range A2 to A4). |
| `Vocal/overtone_duet.wav` | 64 | 262188 | Two narrow resonances over the same drone cross in opposite directions: one climbs from the 4th to the 20th partial while the other falls from the 20th to the 4th. |
| `Vocal/overtone_throat_sweep.wav` | 64 | 262188 | A narrow resonance walks up the harmonic series from the 4th to the 20th partial and back over a fixed drone. |
| `Vocal/tract_length_sweep.wav` | 64 | 262188 | The vowel ah with all formants scaled 0.6 to 1.7: a giant to a chipmunk (the vocal tract length), pitch fixed at 150 Hz. |
| `Vocal/vowel_cycle_children.wav` | 64 | 262188 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of children (Peterson and Barney 1952), voice at 262 Hz; best range about an octave around that pitch. |
| `Vocal/vowel_cycle_men.wav` | 64 | 262188 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of men (Peterson and Barney 1952), voice at 130 Hz; best range about an octave around that pitch. |
| `Vocal/vowel_cycle_women.wav` | 64 | 262188 | The vowel triangle walked in eight steps (ee, ih, eh, ae, ah, aw, uh-oo, oo) with the average formants of women (Peterson and Barney 1952), voice at 220 Hz; best range about an octave around that pitch. |
| `Vocal/whisper_to_voiced.wav` | 64 | 262188 | The vowel ah (men, 150 Hz): frame 0 is a whisper (formant shaped noise: random phases and amplitudes), the last frame the voiced pulse train; a level matched cross fade. |

**Samples (12, 44.1 kHz, 16 bit, peak -1 dBFS; pitched ones tuned to MIDI 60 except where the name says; a `smpl` chunk with the root and the loop where there is one; for the loop and the root the engine reads the preset's own parameters, section 7 item 3):**

| File | Seconds | Channels | Bytes | What it is |
|---|---|---|---|---|
| `bowed_tone.wav` | 3.15 | 1 | 277942 | bowed tone MIDI 60, 5 Hz vibrato; loop 14.3 .. 77.7 % closes on whole vibrato cycles (mismatch 54 dB under the signal) |
| `breath_noise_burst.wav` | 1.20 | 1 | 105884 | a breath: three bands of noise, 1.2 s, unpitched |
| `chord_pad_sections.wav` | 8.00 | 1 | 705688 | C maj7, A min7, F maj7, G 6, 2 s each, crossfaded, 8 s |
| `drum_loop_120bpm.wav` | 4.00 | 1 | 352912 | two bars of 4/4 at 120 BPM: kick, snare, ghost snares, hats, open hats; 32 sixteenths of 0.125 s, the loop wraps |
| `glass_ping.wav` | 3.00 | 1 | 264688 | a struck glass, MIDI 72, six inharmonic partials and a beating twin |
| `gong_metal.wav` | 4.00 | 1 | 352888 | 13 inharmonic modes, each a close pair, sizzle, tuned to MIDI 36 |
| `mallet_marimba.wav` | 1.80 | 1 | 158848 | a marimba bar (1 : 3.99 : 10.1), MIDI 60 |
| `pluck_string.wav` | 2.50 | 1 | 220588 | additive plucked string, tuned to MIDI 60 |
| `rising_texture.wav` | 6.00 | 1 | 529244 | noise whose band climbs 250 Hz -> 7 kHz with a harmonic stack gliding two octaves, 6 s |
| `vinyl_noise_bed.wav` | 4.00 | 2 | 705712 | hiss, rumble, 50 Hz hum, crackle and pops, stereo, 4 s loop (wraps round) |
| `voice_sweep_aeiou.wav` | 5.00 | 1 | 441088 | a voice at 130.8 Hz singing A E I O U in 5 s, vibrato, breath, MIDI 48 |
| `vowel_pad.wav` | 4.10 | 1 | 361732 | three voices singing A / O, MIDI 60, loop closes like the bowed tone |

**Instruments for the Multisample type (SFZ, the samples below the folder of the file):**

| Instrument | Zones | Files | Bytes | What it is |
|---|---|---|---|---|
| `pluck_nylon` | 6 | 6 | 794328 | nylon plucks in six key zones (roots 36 45 54 63 72 81) |
| `mallet_marimba` | 10 | 10 | 971080 | marimba bars in five zones and two velocity layers (1 .. 84 soft, 85 .. 127 hard) |
| `glass_bells` | 4 | 4 | 847072 | struck glass in four zones (roots 48 60 72 84) |

**Impulse responses (3, stereo, 24 bit, 44.1 kHz, scaled to an L2 norm of 1, so that the wet level is about the dry level at `convolve.ir_gain` 0 with the Vital law):**

| File | Seconds | Bytes | What it is |
|---|---|---|---|
| `cabinet_colour.wav` | 0.09 | 23858 | a guitar cabinet colour: open-back roll-off under 70 Hz, a bump at 95 Hz, presence at 2.5 / 3.3 kHz (two 'microphones'), a low pass at 5.2 kHz, 90 ms |
| `plate.wav` | 1.45 | 383714 | a plate: dense and bright from the first millisecond, RT60 1.25 s, 1.45 s long, a high sheen that dies in 0.35 s |
| `small_room.wav` | 0.70 | 185264 | a small room: eight early reflections in the first 46 ms, a 0.42 s tail that darkens |

**How the engine resolves a missing file** (`core/engine-own/osc/content_set.cpp`, `voice.cpp`, `fx_modules.cpp`; measured in the "without the pack" column of the table below): a file that cannot be read or whose SHA-256 differs is skipped. A `Preset table` without its table plays the **saw**; a Sample, Granular, Spectral or Multisample oscillator without its file is **silent** (docs/engine-spec/07 section 1: "no fallback tone"); Convolve without its impulse **does not convolve** (the dry signal). There is no generator to fall back to at run time (`docs/ARCHITECTURE.md` section 6 promises "tolerates absence": a sound is produced either way; for the sample types that holds only because these presets carry a wavetable layer).

## 3. Summary table

Level = chord RMS in dBFS at the sustain (tier high = the XL, and tier low = the Key 61; section 1). Peak = four-note chord at velocity 1.0, raw dBFS and after +18 dB. Mono = level of the mono sum against the stereo level of the single note, worst 1/3 octave band. Without the pack = the chord RMS with no content store (below -100 means silent). DC as a fraction of full scale.

| Preset | Level XL / 61 | Peak raw -> at +18 | Mono / worst band | DC | Click | Tail s | Without the pack |
|---|---|---|---|---|---|---|---|
| Chord Cloud Pad | -20.0 / -19.2 | -8.0 -> 10.0 | -1.3 / -2.8 @ 252 Hz | 0.0000 | 0.0 | 4.97 | -23.4 |
| Vowel Scrub Choir | -20.0 / -19.9 | -6.8 -> 11.2 | -1.4 / -1.7 @ 8064 Hz | 0.0000 | 0.0 | 1.60 | -20.1 |
| Frozen Pluck Drone | -20.0 / -17.1 | -8.1 -> 9.9 | -2.5 / -3.4 @ 400 Hz | 0.0000 | 0.0 | 1.85 | -23.6 |
| Gong Grain Swarm | -20.0 / -19.2 | -7.8 -> 10.2 | -2.3 / -3.2 @ 318 Hz | 0.0000 | 0.0 | 7.31 | -27.8 |
| Stutter Grain Kit | -20.0 / -20.0 | -11.7 -> 6.3 | -0.1 / -1.6 @ 50 Hz | 0.0001 | 0.6 | 1.88 | -20.8 |
| Rising Grain Riser | -20.0 / -19.8 | -7.3 -> 10.7 | -1.1 / -2.7 @ 2540 Hz | 0.0000 | 0.0 | 5.93 | -23.6 |
| Nylon Zone Pluck | -20.0 / -20.0 | -1.5 -> 16.5 | -0.7 / -1.4 @ 2016 Hz | 0.0000 | 0.6 | 3.15 | -21.9 |
| Marimba Layer Keys | -20.0 / -23.1 | -7.4 -> 10.6 | -0.8 / -5.8 @ 200 Hz | 0.0000 | 1.0 | 0.00 | -32.8 |
| Glass Zone Bells | -20.0 / -20.0 | -5.5 -> 12.5 | -0.9 / -3.0 @ 1600 Hz | 0.0000 | 0.7 | 3.42 | -22.9 |
| Dusty Rhodes Keys | -20.0 / -25.3 | -2.2 -> 15.8 | -1.1 / -1.3 @ 2540 Hz | 0.0000 | 0.7 | 0.33 | -25.4 |
| Glass Ping Pluck | -20.0 / -19.9 | -4.9 -> 13.1 | -1.3 / -9.0 @ 3200 Hz | 0.0000 | 0.8 | 3.81 | -24.1 |
| Breakbeat Slice Keys | -20.0 / -21.4 | -5.7 -> 12.3 | -0.0 / -6.3 @ 159 Hz | 0.0002 | 0.7 | 0.71 | -26.1 |
| Bowed MPE Strings | -20.0 / -19.1 | -8.1 -> 9.9 | -1.6 / -1.5 @ 252 Hz | 0.0000 | 0.2 | 2.44 | -23.3 |
| Reverse Gong Swell | -20.0 / -19.7 | -8.9 -> 9.1 | -1.8 / -2.7 @ 635 Hz | 0.0000 | 0.0 | 3.47 | -28.2 |
| Breath Flute Lead | -20.0 / -20.3 | -14.7 -> 3.3 | -0.0 / -0.2 @ 400 Hz | 0.0001 | 0.0 | 2.29 | -20.0 |
| Spectral Vowel Freeze | -20.0 / -19.6 | -9.4 -> 8.6 | -1.2 / -2.0 @ 252 Hz | 0.0000 | 0.0 | 3.78 | -29.8 |
| Spectral Bowed Glass | -20.0 / -20.2 | -11.9 -> 6.1 | -0.2 / -0.2 @ 504 Hz | 0.0001 | 0.0 | 2.55 | -27.4 |
| Spectral Gong Pad | -20.0 / -19.4 | -6.3 -> 11.7 | -1.5 / -4.7 @ 2540 Hz | 0.0000 | 0.0 | 5.54 | -27.9 |
| Spectral Chord Morph | -20.0 / -19.8 | -9.2 -> 8.8 | -1.2 / -2.7 @ 504 Hz | 0.0000 | 0.0 | 4.78 | -27.5 |
| Reese Dual Phase Bass | -20.0 / -26.2 | -6.6 -> 11.4 | -0.2 / -1.4 @ 2016 Hz | 0.0002 | 0.2 | 0.22 | -25.8 |
| Vowel Table Lead | -20.0 / -19.6 | -10.3 -> 7.7 | -0.4 / -0.9 @ 1600 Hz | 0.0000 | 0.0 | 2.58 | -17.9 |
| Drawbar Organ Keys | -20.0 / -20.0 | -7.3 -> 10.7 | -1.4 / -4.1 @ 400 Hz | 0.0000 | 0.9 | 0.27 | -19.9 |
| Throat Singer Drone | -20.0 / -20.0 | -7.3 -> 10.7 | -0.5 / -1.3 @ 252 Hz | 0.0000 | 0.0 | 6.16 | -29.5 |
| Ladder FM Bass | -20.0 / -20.0 | -8.5 -> 9.5 | 0.0 / 0.0 @ 32 Hz | 0.0001 | 0.9 | 0.14 | -20.7 |

The level of the real preset at 1 s (before the attack is over) is in the per-preset lines. The Key 61 figure differs from the XL by more than 1.5 dB where Convolve is bypassed or a reverb is lost: Frozen Pluck Drone (2.9 dB), Marimba Layer Keys (-3.1 dB), Dusty Rhodes Keys (-5.3 dB), Reese Dual Phase Bass (-6.2 dB). They are levelled on the XL.

## 4. CPU: not measured; what each preset spends

Rule (owner): nothing new may be dearer than the dearest existing preset (batch 1, section 4: Key 61 median 4.78 / p99 6.02 % of the block, XL median 11.53 / p99 14.06 %, in that run on the Mac). **That could not be checked**: `synthyboy_tier_bench` has no content store, so a sample-type oscillator costs nothing in it, and a rebuild was not allowed. The command after a rebuild: `SYNTHYBOY_CONTENT_DIR=<store> synthyboy_tier_bench --preset-dir content/presets --repeat 3` (the store: `scripts/sound-lab.py` makes one in its work folder, links named by the pack ids). **The Spectral oscillator is the dearest voice type in the engine (docs/engine-spec/07 section 7) and the four Spectral presets are the ones to check first, then the Granular ones with the most grains** (Chord Cloud Pad: 26 grains a second x 180 ms x 2 unison x polyphony 6; Rising Grain Riser: 60 x 150 ms). What was done to keep them cheap: polyphony 1 .. 6 (the heavy ones 1 .. 4: `master.polyphony`), unison 2, no warp except one spectral warp on each Spectral preset (Add Harmonics, Spectral Comb, Add Subharmonics, Spectral Shift) and no morph, only the Analog and Digital filter models (the cheap ones of batch 1: -0.1 and 0), the effects of batch 1's catalogue (0.1 .. 0.8 % each), no wavetable warp at all (the 4x warps cost 7 .. 9 % of the XL block per oscillator and per unison voice). The cost of Convolve is not in batch 1's catalogue either; it is bypassed on the Key 61.

| Preset | Polyphony | Oscillators (type x unison voices) | Filter | Effects |
|---|---|---|---|---|
| Chord Cloud Pad | 6 | 1: Granular x2; 2: Sine to Saw x1 | Analog | reverb, eq |
| Vowel Scrub Choir | 4 | 1: Granular x2; 2: Preset table x3 | Digital | chorus, convolve |
| Frozen Pluck Drone | 6 | 1: Granular x3; 2: Triangle x2 | Analog | chorus, eq, convolve |
| Gong Grain Swarm | 4 | 1: Granular x2; 2: Bell Partials x3 | Analog | delay, reverb, eq |
| Stutter Grain Kit | 6 | 1: Granular x2; 2: Sine x1 | Analog | delay, distortion |
| Rising Grain Riser | 2 | 1: Granular x2; 2: Saw x3 | Analog | delay, reverb |
| Nylon Zone Pluck | 10 | 1: Multisample x2; 2: Saw x2 | Analog | delay, reverb, eq |
| Marimba Layer Keys | 12 | 1: Multisample x2; 2: Sine x1 | Analog | chorus, utility, convolve |
| Glass Zone Bells | 10 | 1: Multisample x2; 2: Bell Partials x1; sub | Digital | delay, eq, convolve |
| Dusty Rhodes Keys | 8 | 1: Preset table x2; 2: Bell Partials x1; 3: Sample x1 | Analog | chorus, eq, utility, convolve |
| Glass Ping Pluck | 10 | 1: Sample x2; 2: Bell Partials x1 | Digital | delay, eq, convolve |
| Breakbeat Slice Keys | 6 | 1: Sample x1; 2: Sine x1 | Analog | distortion, compressor, convolve |
| Bowed MPE Strings | 8 | 1: Sample x3; 2: Saw x3; noise | Analog | chorus, reverb, eq |
| Reverse Gong Swell | 4 | 1: Sample x2; noise | Analog | reverb, eq, convolve |
| Breath Flute Lead | 1 | 1: Sine to Saw x1; 2: Sample x1; noise | Digital | delay, reverb |
| Spectral Vowel Freeze | 4 | 1: Spectral + warp Add Harmonics x2; 2: Sine to Saw x1 | Analog | reverb, eq |
| Spectral Bowed Glass | 1 | 1: Spectral + warp Spectral Comb x1; 2: Preset table x2 | Digital | delay, reverb |
| Spectral Gong Pad | 4 | 1: Spectral + warp Add Subharmonics x2; 2: Bell Partials x1 | Analog | reverb, eq, utility |
| Spectral Chord Morph | 3 | 1: Spectral + warp Spectral Shift x2; 2: Sine to Saw x1 | Analog | chorus, reverb, eq |
| Reese Dual Phase Bass | 6 | 1: Preset table x5; 2: Saw x3 | Analog | distortion, eq, utility, convolve |
| Vowel Table Lead | 1 | 1: Preset table x3; 2: Triangle x1 | Digital | delay, reverb |
| Drawbar Organ Keys | 10 | 1: Preset table x1; 2: Sine x1; noise | Analog | chorus, phaser, distortion, eq |
| Throat Singer Drone | 3 | 1: Preset table x2; 2: Sine x1 | Analog | reverb, eq |
| Ladder FM Bass | 4 | 1: Preset table x1; sub | Analog | distortion, eq |

## 5. Distance to the nearest preset

The survey's similarity measure (`scripts/survey-presets.py`): the number of parameters whose values differ. The first 112 were within 4 .. 71 of each other, batch 1 71 .. 123 from the nearest of the set; the 24 here are 35 .. 89 from the nearest of the 160.

| Preset | Parameters differing from the defaults | Nearest preset of the 160 | Distance | Nearest of the new ones | Distance |
|---|---|---|---|---|---|
| Chord Cloud Pad | 109 | Spectral Vowel Freeze | 89 | Spectral Vowel Freeze | 89 |
| Vowel Scrub Choir | 88 | Infinite Horizon | 85 | Marimba Layer Keys | 87 |
| Frozen Pluck Drone | 83 | Spectral Vowel Freeze | 74 | Spectral Vowel Freeze | 74 |
| Gong Grain Swarm | 98 | Rising Grain Riser | 81 | Rising Grain Riser | 81 |
| Stutter Grain Kit | 70 | Glass Ping Pluck | 67 | Glass Ping Pluck | 67 |
| Rising Grain Riser | 78 | Bell Chime Lead | 75 | Glass Ping Pluck | 76 |
| Nylon Zone Pluck | 97 | Glass Ping Pluck | 83 | Glass Ping Pluck | 83 |
| Marimba Layer Keys | 66 | Glass Zone Bells | 56 | Glass Zone Bells | 56 |
| Glass Zone Bells | 69 | Glass Ping Pluck | 35 | Glass Ping Pluck | 35 |
| Dusty Rhodes Keys | 88 | Ladder FM Bass | 74 | Ladder FM Bass | 74 |
| Glass Ping Pluck | 67 | Glass Zone Bells | 35 | Glass Zone Bells | 35 |
| Breakbeat Slice Keys | 68 | Dirty Distortion Bass | 67 | Glass Ping Pluck | 69 |
| Bowed MPE Strings | 108 | Bowed Pressure Strings | 74 | Reverse Gong Swell | 107 |
| Reverse Gong Swell | 59 | Acoustic Style Pluck | 51 | Glass Ping Pluck | 70 |
| Breath Flute Lead | 83 | Vowel Table Lead | 72 | Vowel Table Lead | 72 |
| Spectral Vowel Freeze | 77 | Spectral Gong Pad | 54 | Spectral Gong Pad | 54 |
| Spectral Bowed Glass | 86 | Vowel Table Lead | 76 | Vowel Table Lead | 76 |
| Spectral Gong Pad | 82 | Spectral Vowel Freeze | 54 | Spectral Vowel Freeze | 54 |
| Spectral Chord Morph | 70 | Spectral Vowel Freeze | 57 | Spectral Vowel Freeze | 57 |
| Reese Dual Phase Bass | 75 | Ladder FM Bass | 59 | Ladder FM Bass | 59 |
| Vowel Table Lead | 74 | MPE Pressure Lead | 69 | Breath Flute Lead | 72 |
| Drawbar Organ Keys | 93 | Ladder FM Bass | 82 | Ladder FM Bass | 82 |
| Throat Singer Drone | 75 | Dark Cinema Drone | 68 | Ladder FM Bass | 72 |
| Ladder FM Bass | 60 | Dirty Distortion Bass | 56 | Reese Dual Phase Bass | 59 |

## 6. The presets

### Chord Cloud Pad (Pad)

*Intention.* A cloud of grains cut from a four-chord progression (C major 7, A minor 7, F major 7, G 6): a slow LFO walks the play head through the file, so the harmony drifts on its own; the voice control gives every new note its own pan, tuning and brightness, so a chord is never a copy of itself.

*What to play.* Slow chords C2 .. C4, held for ten seconds and more: the play head walks the progression in a 20 second cycle (LFO 0.05 Hz), and the Section macro moves where it starts. The voice control gives six different pan, tuning and brightness settings in turn: play a chord note by note and hear the notes of it stand in different places. Pressure: brighter and more scattered. Wheel: longer grains and a pitch scatter. Macros: Section, Thin Out, Short Grains, Scatter, Bright, Space, Drift.

*Macros.* Section (audible: 3.5 dB, -0.1 dB level, +0.00 oct); Thin Out (audible: 3.5 dB, -1.2 dB level, -0.23 oct); Short Grains (audible: 3.3 dB, -1.0 dB level, -0.14 oct); Scatter (audible: 4.6 dB, +0.1 dB level, +0.02 oct); Bright (audible: 0.7 dB, +0.0 dB level, +0.03 oct; by HF +10.0 dB); Space (audible: 1.9 dB, +7.7 dB level, -0.21 oct); Drift (acts on a rate: not seen by the spectral measure; route lfo1.rate +50 %).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.2 (Key 61); the real preset at 1 s: -21.5. Four-note chord peak -8.0 dBFS raw = 10.0 at +18 dB. Single note: mono sum -1.3 dB vs stereo, worst band -2.8 dB at 252 Hz; DC 0.0000, click ratio 0.0, tail 4.97 s; centroid 178 Hz, moving 0.35 oct and 31.2 dB over the note. Controls (0 -> 100): wheel: 2.0 dB (+0.1 dB, -0.04 oct); pressure: 1.6 dB (-0.3 dB, -0.01 oct); velocity route: 2.0 dB (+0.1 dB, +0.05 oct). Without the content pack: chord -23.4 dBFS. Nearest preset: Spectral Vowel Freeze (89).

*Spends.* polyphony 6; 1: Granular x2; 2: Sine to Saw x1; filter Analog; effects reverb, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether the grain cloud is a pad or a swarm at the default density (26 a second, 180 ms): the cloud sounds richer with more density, but each grain is another voice of CPU. Whether the six-step voice control lanes are audible as variety or as sloppiness (measured on a plain saw: a lane value of 80 pans 16 dB to one side, 60 on the cutoff moves the centroid by about an octave, 80 on the detune is 0.8 semitone; here pan up to 75, detune up to 12 cents, cutoff up to 24, about half an octave).

### Vowel Scrub Choir (Lead)

*Intention.* A choir you scrub with the mod wheel: the Granular oscillator holds a voice that sings A, E, I, O, U in five seconds and the wheel is its play head, so the vowel follows your hand and holds wherever you stop; a formant table layered on top moves with it. Plate impulse response, vibrato that arrives late.

*What to play.* Single notes or two-note intervals C3 .. C5: the mod wheel is the vowel: at 0 the voice sings A, at 127 U, with E, I, O between, and it holds where you stop (the play head is the wheel). Press the pad for a pitch scatter in the grains, play softer for a darker voice. Macros: Vowel (a second hand on the same axis), Cloud (scatter), Short Grains, Table Layer (the formant table that moves with the wheel), Space, Bright.

*Macros.* Vowel (audible: 6.3 dB, -0.3 dB level, -0.24 oct); Cloud (audible: 5.5 dB, +0.1 dB level, -0.08 oct); Short Grains (audible: 5.4 dB, -1.3 dB level, -0.10 oct); Table Layer (audible: 4.8 dB, +7.2 dB level, -0.16 oct); Space (audible: 2.5 dB, +2.3 dB level, -0.08 oct; by side +5.0 dB); Bright (audible: 2.4 dB, +0.2 dB level, +0.07 oct; by HF +6.3 dB).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.9 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -6.8 dBFS raw = 11.2 at +18 dB. Single note: mono sum -1.4 dB vs stereo, worst band -1.7 dB at 8064 Hz; DC 0.0000, click ratio 0.0, tail 1.60 s; centroid 945 Hz, moving 0.79 oct and 7.1 dB over the note. Controls (0 -> 100): wheel: 9.8 dB (+0.3 dB, -0.78 oct); pressure: 5.1 dB (+0.1 dB, +0.10 oct); velocity route: 4.5 dB (+0.9 dB, +0.19 oct). Without the content pack: chord -20.1 dBFS. Nearest preset: Infinite Horizon (85).

*Spends.* polyphony 4; 1: Granular x2; 2: Preset table x3; filter Digital; effects chorus, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* That a grain cloud can sing a vowel without a pitch of its own: the grains read a voice at 130 Hz and are pitched by the key, so at 261 Hz the vowel is an octave up and its formants move with it (the table layer is built for 220 Hz and does not shift the same way). At a density of 58 grains a second with 95 ms the grains overlap about five times: the voice is smooth but not as clear as the source.

### Frozen Pluck Drone (Pad)

*Intention.* A plucked string held still: the play head is parked on the first hundred milliseconds of the pluck, so the Granular oscillator sustains one instant of it for as long as the key is down; a second copy runs on bus 1 through the plate impulse response alone (wet only), the dry copy stays in front.

*What to play.* Chords C2 .. C4, held: the plucked string never decays. Wheel: the play head moves a little away from the frozen instant (brighter, rougher) and the plate bus gets louder; pressure: more grains. Macros: Freeze Point (which instant), Cloud, Short Grains, Plate Bus, Pitch Scatter (grains up to six semitones either way), Bright.

*Macros.* Freeze Point (audible: 7.8 dB, -2.4 dB level, -0.41 oct); Cloud (audible: 1.1 dB, -0.5 dB level, -0.01 oct; by HF +4.6 dB); Short Grains (audible: 8.5 dB, -1.7 dB level, -0.07 oct); Plate Bus (audible: 4.0 dB, +3.0 dB level, +0.21 oct); Pitch Scatter (audible: 6.5 dB, +0.1 dB level, +0.23 oct); Bright (audible: 1.3 dB, +0.2 dB level, +0.07 oct; by HF +5.9 dB).

*Measured.* Level -20.0 dBFS at the sustain (XL), -17.1 (Key 61); the real preset at 1 s: -19.5. Four-note chord peak -8.1 dBFS raw = 9.9 at +18 dB. Single note: mono sum -2.5 dB vs stereo, worst band -3.4 dB at 400 Hz; DC 0.0000, click ratio 0.0, tail 1.85 s; centroid 400 Hz, moving 0.86 oct and 26.7 dB over the note. Controls (0 -> 100): wheel: 4.0 dB (-0.7 dB, -0.18 oct); pressure: 1.3 dB (-0.2 dB, +0.14 oct); velocity route: 1.7 dB (+0.5 dB, +0.18 oct). Without the content pack: chord -23.6 dBFS. Nearest preset: Spectral Vowel Freeze (74).

*Spends.* polyphony 6; 1: Granular x3; 2: Triangle x2; filter Analog; effects chorus, eq, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* That the frozen instant is not a click train: the Gaussian grains of 110 ms at 42 a second overlap about four times, but each grain starts at the same 3 % of the file, so the phase relation between the grains decides how smooth it is (a Perlin LFO and a 3 % offset scatter the start).

### Gong Grain Swarm (FX)

*Intention.* The gong shattered into a swarm: sixty grains a second of the metal read in real time, each one with its own pitch (up to a fifth either way), pan and level, some of them backwards; the swarm goes through a second bus with its own delay and reverb so the dry cloud stays close and the echo cloud floats.

*What to play.* Single notes C2 .. C4, held for five seconds and more (the play head reads the gong in real time and loops). Wheel: more grains and a louder echo cloud (bus 2); pressure: pitch scatter. Macros: Sparse (the grain rate drops to a quarter), Pitch Chaos, Scatter (position, level and direction), Short Grains, Echo Cloud, Dark.

*Macros.* Sparse (audible: 1.8 dB, -1.0 dB level, +0.03 oct; by LF -6.1 dB); Pitch Chaos (audible: 4.0 dB, -0.3 dB level, +0.17 oct); Scatter (audible: 2.0 dB, -2.3 dB level, +0.32 oct); Short Grains (audible: 5.2 dB, -4.2 dB level, +0.57 oct); Echo Cloud (audible: 2.8 dB, +3.6 dB level, -0.35 oct); Dark (audible: 8.9 dB, -3.0 dB level, -1.08 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.2 (Key 61); the real preset at 1 s: -20.8. Four-note chord peak -7.8 dBFS raw = 10.2 at +18 dB. Single note: mono sum -2.3 dB vs stereo, worst band -3.2 dB at 318 Hz; DC 0.0000, click ratio 0.0, tail 7.31 s; centroid 902 Hz, moving 0.56 oct and 18.9 dB over the note. Controls (0 -> 100): wheel: 1.9 dB (+2.5 dB, -0.40 oct); pressure: 1.9 dB (-0.0 dB, +0.09 oct); velocity route: 1.3 dB (+0.2 dB, +0.27 oct). Without the content pack: chord -27.8 dBFS. Nearest preset: Rising Grain Riser (81).

*Spends.* polyphony 4; 1: Granular x2; 2: Bell Partials x3; filter Analog; effects delay, reverb, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether the swarm is a metal texture or noise: the grains are 70 ms of an inharmonic spectrum with pitch scatter up to a fifth, which can sound like a bag of bells; the Nitrous reverb at 6 s on bus 2 may smear it.

### Stutter Grain Kit (Pluck)

*Intention.* A drum loop played as a stutter: the grains start on the sixteenth notes of the host tempo and are a thirty-second long, and the key you press chooses where in the two bars the stutter sits (the play head follows the key: about a quarter of the way in at C2, three quarters at C7), pitched like a sampler.

*What to play.* Any key C2 .. C6, played as a sampler: the key chooses where in the two bar break the stutter sits (the play head is the key number over 127: about 28 % into the loop at C2, 45 % at C4, 75 % at C7) and transposes it; the grains start on the sixteenth notes of the host tempo and are 1/32 long, so a held key is a rolling stutter on one place. Wheel: scatter of position and pitch (a glitch). Macros: Nudge (the position), Scatter, Snap (the window), Pitch Rand, Dark, Echo.

*Macros.* Nudge (audible: 20.8 dB, +1.7 dB level, -0.88 oct); Scatter (audible: 6.6 dB, +0.2 dB level, -0.32 oct); Snap (audible: 5.8 dB, -0.1 dB level, -0.83 oct); Pitch Rand (audible: 5.1 dB, +0.1 dB level, -0.05 oct); Dark (audible: 21.1 dB, -0.3 dB level, -0.81 oct); Echo (audible: 3.4 dB, -1.9 dB level, +1.09 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -20.0 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -11.7 dBFS raw = 6.3 at +18 dB. Single note: mono sum -0.1 dB vs stereo, worst band -1.6 dB at 50 Hz; DC 0.0001, click ratio 0.6, tail 1.88 s; centroid 123 Hz, moving 1.27 oct and 4.5 dB over the note. Controls (0 -> 100): wheel: 6.5 dB (+0.2 dB, -0.22 oct); velocity route: 11.0 dB (+9.9 dB, +0.28 oct). Without the content pack: chord -20.8 dBFS. Nearest preset: Glass Ping Pluck (67).

*Spends.* polyphony 6; 1: Granular x2; 2: Sine x1; filter Analog; effects delay, distortion. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Tempo: the grain rate follows the host's tempo, the break does not (it is 120 BPM): at 140 BPM the stutter grains are shorter than the break's own sixteenths. Whether the sub layer (a sine two octaves down, there so that something plays if the sample is missing) belongs there.

### Rising Grain Riser (FX)

*Intention.* A six second build: the Granular oscillator reads the rising texture in real time (the grains are a hundred and fifty milliseconds, sixty a second), a filter climbs, a delay and reverb on bus 2 throw the grains after it; hold the key to the top, let go for the tail. Play C2 for the plain build, other keys transpose it.

*What to play.* One key, C2 for the plain build (the sample's root; other keys transpose it and change its length): hold six seconds, let go for the tail. The filter climbs with envelope 2 over 5.6 s. Wheel: louder bus 2 (delay and reverb) and denser grains. Macros: Slow (the read speed down to a fifth), Sparse, Scatter, Throw (bus 2), Edge (resonance), Short Grains.

*Macros.* Slow (audible: 11.8 dB, -0.3 dB level, -0.48 oct); Sparse (audible: 3.0 dB, -0.1 dB level, -0.10 oct); Scatter (audible: 3.8 dB, -0.0 dB level, +0.02 oct); Throw (audible: 5.2 dB, +1.1 dB level, +0.96 oct); Edge (audible: 3.5 dB, +2.5 dB level, +1.09 oct); Short Grains (audible: 4.0 dB, -0.1 dB level, -0.10 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.8 (Key 61); the real preset at 1 s: -23.4. Four-note chord peak -7.3 dBFS raw = 10.7 at +18 dB. Single note: mono sum -1.1 dB vs stereo, worst band -2.7 dB at 2540 Hz; DC 0.0000, click ratio 0.0, tail 5.93 s; centroid 417 Hz, moving 1.12 oct and 7.8 dB over the note. Controls (0 -> 100): wheel: 3.2 dB (+0.5 dB, +0.56 oct); velocity route: 3.5 dB (+1.3 dB, +0.19 oct). Without the content pack: chord -23.6 dBFS. Nearest preset: Bell Chime Lead (75).

*Spends.* polyphony 2; 1: Granular x2; 2: Saw x3; filter Analog; effects delay, reverb. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* That the sample's own rise (24 filter slices that climb from 250 Hz to 7 kHz, a stack of 12 harmonics that glides two octaves) and the filter envelope on top of it do not fight: both rise, the sound may get thin at the top. The level is set for the last seconds of the build (it is 10 dB lower at the start).

### Nylon Zone Pluck (Pluck)

*Intention.* Plucked nylon strings in six key zones, one recording to a zone and every zone at its own pitch and brightness; the voice control gives each note of a run its own pan, tuning and cutoff in a repeating pattern of eight (a human hand never plays the same note twice), a short saw pluck sits under it and the wheel doubles the strings (two unison voices that drift apart).

*What to play.* Single notes and arpeggios C1 .. C6. Six recordings of a nylon pluck, one per zone: the zone borders are at E2/F2, C#3/D3, A#3/B3, G4/G#4 and E5/F5 (MIDI 40/41, 49/50, 58/59, 67/68, 76/77; see the SFZ file). Play the same note again and again: the voice control gives every note of eight its own pan, tuning and cutoff, so repeated notes are never the same. Wheel: the strings are doubled (two unison voices that drift apart) and the echo comes up. Macros: Bright, Saw Pluck, Double, Sustain, Echo, Room.

*Macros.* Bright (audible: 1.0 dB, +0.1 dB level, +0.19 oct; by HF +5.0 dB); Saw Pluck (audible: 10.4 dB, +9.2 dB level, +0.19 oct); Double (audible: 0.9 dB, +1.2 dB level, -0.08 oct; by fine structure +7.1 dB); Sustain (audible: 17.5 dB, +17.9 dB level, +0.18 oct); Echo (audible: 9.7 dB, +7.5 dB level, +0.01 oct); Room (audible: 8.3 dB, +6.5 dB level, +0.09 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -20.0 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -1.5 dBFS raw = 16.5 at +18 dB. Single note: mono sum -0.7 dB vs stereo, worst band -1.4 dB at 2016 Hz; DC 0.0000, click ratio 0.6, tail 3.15 s; centroid 665 Hz, moving 1.05 oct and 31.2 dB over the note. Controls (0 -> 100): wheel: 7.3 dB (+7.2 dB, +0.14 oct); velocity route: 7.8 dB (+2.7 dB, +0.42 oct). Without the content pack: chord -21.9 dBFS. Nearest preset: Glass Ping Pluck (83).

*Spends.* polyphony 10; 1: Multisample x2; 2: Saw x2; filter Analog; effects delay, reverb, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* The zone seams: each zone is one recording shifted by up to four semitones either way in the middle zones (the outer zones more: down to a whole octave at the bottom, over two octaves at the top), so a seam can be heard as a step in the timbre. Whether the saw pluck under it (it is what plays if the pack is missing) should be there at all at its 16 %.

### Marimba Layer Keys (Keys)

*Intention.* A marimba in five key zones and two velocity layers: soft keys play the dark, round bars, hard keys the bright ones with the knock of the mallet in front, and the change between them is a real change of recording, not a filter; a small-room impulse response, a slow tremolo from the wheel.

*What to play.* Lines and chords C2 .. C6. Velocity below 85 plays the soft recording of the bar, above it the hard one with the knock in front: play the same note at 40 and 120 to hear the change. Wheel: tremolo and more of the octave sine. Macros: Zone Shift, Room, Octave Sine, Mellow, Ring, Chorus.

*Macros.* Zone Shift (audible: 4.1 dB, +0.9 dB level, +0.16 oct); Room (audible: 6.5 dB, +5.7 dB level, +0.16 oct); Octave Sine (audible: 5.5 dB, +11.9 dB level, +0.36 oct); Mellow (audible: 20.1 dB, -11.2 dB level, -0.45 oct); Ring (audible: 12.8 dB, +14.8 dB level, +0.16 oct); Chorus (audible: 9.7 dB, +4.0 dB level, +0.01 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -23.1 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -7.4 dBFS raw = 10.6 at +18 dB. Single note: mono sum -0.8 dB vs stereo, worst band -5.8 dB at 200 Hz; DC 0.0000, click ratio 1.0, tail 0.00 s; centroid 414 Hz, moving 0.80 oct and 75.3 dB over the note. Controls (0 -> 100): wheel: 4.7 dB (+5.1 dB, +0.07 oct); velocity route: 0.9 dB (+0.1 dB, +0.01 oct). Without the content pack: chord -32.8 dBFS. Nearest preset: Glass Zone Bells (56).

*Spends.* polyphony 12; 1: Multisample x2; 2: Sine x1; filter Analog; effects chorus, utility, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* That the velocity switch at 85 is a step: there is no crossfade between the layers (a crossfade needs xfin / xfout in the SFZ). The wheel's effect on the zone lookup is large at the ends of the keyboard and nil in the middle.

### Glass Zone Bells (Keys)

*Intention.* Struck glass in four zones, locked to the Hirajoshi scale on D: whatever you play is moved to the nearest note of the scale, so a hand on the keys cannot hit a wrong one; the plate impulse response and a soft sub make a hall of it, the wheel shifts the zones.

*What to play.* Any keys: every note is moved to the nearest note of the Hirajoshi scale on D (D, E, F, A, A#), so a hand can lay any chord on the keys. Single notes and slow chords C2 .. C6. Wheel: the zone lookup shifts and the plate gets louder. Macros: Bright (a lowpass at 2.8 kHz opens), Plate, Bell Layer, Echo, Ring, Sub.

*Macros.* Bright (audible: 1.5 dB, +0.2 dB level, +0.30 oct; by HF +8.5 dB); Plate (audible: 2.6 dB, -3.0 dB level, +0.30 oct); Bell Layer (audible: 9.5 dB, +7.6 dB level, +1.14 oct); Echo (audible: 5.8 dB, +4.0 dB level, -0.01 oct); Ring (audible: 6.8 dB, +7.9 dB level, +0.13 oct); Sub (audible: 5.2 dB, +13.0 dB level, -1.77 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -20.0 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -5.5 dBFS raw = 12.5 at +18 dB. Single note: mono sum -0.9 dB vs stereo, worst band -3.0 dB at 1600 Hz; DC 0.0000, click ratio 0.7, tail 3.42 s; centroid 462 Hz, moving 1.14 oct and 24.5 dB over the note. Controls (0 -> 100): wheel: 1.5 dB (-0.2 dB, +0.21 oct); velocity route: 8.3 dB (+1.3 dB, +1.08 oct). Without the content pack: chord -22.9 dBFS. Nearest preset: Glass Ping Pluck (35).

*Spends.* polyphony 10; 1: Multisample x2; 2: Bell Partials x1; sub; filter Digital; effects delay, eq, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether Conform is what you want from a keyboard: with the scale on, the black keys of the chromatic scale are not available (key 'mark only' would show the scale on the pads without moving notes: `master.scale.mode` Mark only). The four zones of glass (roots C3, C4, C5, C6) are one recording each, stretched by up to six semitones in the middle zones and by two octaves at the ends of the keyboard.

### Dusty Rhodes Keys (Keys)

*Intention.* A tine electric piano from a frequency-modulation table, an amp cabinet convolved on it, and a loop of vinyl crackle under every note that comes from the Sample oscillator (it bypasses the filter, so it stays dusty and wide).

*What to play.* Chords and lines C2 .. C5, velocity 50 .. 127: the harder, the more bark in the tine. Wheel up: the suitcase tremolo comes in and the chorus widens. Macros: Tine Bark, Dust (the vinyl loop), Chorus, Dry Amp (0 = the full cabinet), Sustain, Tone. Pad pressure is not used (the sound dies away).

*Macros.* Tine Bark (audible: 7.7 dB, -1.7 dB level, +2.00 oct); Dust (audible: 7.5 dB, +0.1 dB level, +0.04 oct); Chorus (audible: 4.9 dB, +4.6 dB level, +0.15 oct); Dry Amp (audible: 5.0 dB, -4.9 dB level, -0.03 oct); Sustain (audible: 9.2 dB, +9.3 dB level, -0.20 oct); Tone (audible: 2.3 dB, +0.1 dB level, +0.08 oct; by HF +6.4 dB).

*Measured.* Level -20.0 dBFS at the sustain (XL), -25.3 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -2.2 dBFS raw = 15.8 at +18 dB. Single note: mono sum -1.1 dB vs stereo, worst band -1.3 dB at 2540 Hz; DC 0.0000, click ratio 0.7, tail 0.33 s; centroid 502 Hz, moving 1.50 oct and 17.7 dB over the note. Controls (0 -> 100): wheel: 5.7 dB (-0.2 dB, +1.43 oct); velocity route: 9.0 dB (-2.0 dB, +1.37 oct). Without the content pack: chord -25.4 dBFS. Nearest preset: Ladder FM Bass (74).

*Spends.* polyphony 8; 1: Preset table x2; 2: Bell Partials x1; 3: Sample x1; filter Analog; effects chorus, eq, utility, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether the vinyl bed is too present with five or more notes down: every voice carries its own copy of the loop (random start), so a chord has a chord of crackle (the bed sits at 16 % of the oscillator level). Whether the cabinet at 85 % is the right amount of amp. How the FM table's frame 14 sounds at the top of the keyboard (the table was made for a reading at any pitch, its overtones were measured, not heard).

### Glass Ping Pluck (Pluck)

*Intention.* A struck wine glass: the Sample oscillator plays the ping at every key (it is tuned, so the whole keyboard is a glass harmonica), a thin bell table adds the inharmonic body, and the plate impulse response carries the ring into a long tail.

*What to play.* Single notes and slow runs C3 .. C6, velocity 40 .. 127. The sample is tuned to C5: below it the glass slows down and darkens (an octave down it rings twice as long), above it it gets short and thin. Wheel: more plate and a wider pair. Macros: Dark (the lowpass closes), Plate, Shimmer (the unison pair beats), Echo, Body (the bell table), Ring.

*Macros.* Dark (audible: 15.9 dB, -2.2 dB level, -1.06 oct); Plate (audible: 2.5 dB, -3.4 dB level, -0.32 oct); Shimmer (audible: 4.0 dB, -1.1 dB level, +0.16 oct); Echo (audible: 4.5 dB, +3.7 dB level, +0.04 oct); Body (audible: 8.8 dB, +6.4 dB level, +1.11 oct); Ring (audible: 5.2 dB, +5.7 dB level, +0.70 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.9 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -4.9 dBFS raw = 13.1 at +18 dB. Single note: mono sum -1.3 dB vs stereo, worst band -9.0 dB at 3200 Hz; DC 0.0000, click ratio 0.8, tail 3.81 s; centroid 639 Hz, moving 1.43 oct and 17.4 dB over the note. Controls (0 -> 100): wheel: 3.1 dB (-1.2 dB, +0.55 oct); velocity route: 4.1 dB (+1.2 dB, +0.82 oct). Without the content pack: chord -24.1 dBFS. Nearest preset: Glass Zone Bells (35).

*Spends.* polyphony 10; 1: Sample x2; 2: Bell Partials x1; filter Digital; effects delay, eq, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether the whole keyboard sounds like one glass or like a pitched sample (the sample is shifted by up to three octaves: its resonances move with it, which a real glass's would not). The plate impulse response at 38 % is long and bright: how much it blurs fast runs.

### Breakbeat Slice Keys (Keys)

*Intention.* A two bar break cut into 32 slices that you play from C2 (MIDI 36): every key is one sixteenth, so a rhythm is a melody; a second, crushed and compressed copy runs on bus 1 in parallel, a small-room impulse response glues it together.

*What to play.* Keys C2 (MIDI 36) to G4 (67): thirty-two keys, one sixteenth of the two-bar break each, played in the order of the loop from the lowest key up. A slice plays to the start of the next one, or until the key is up (60 ms release). The pitch does not follow the key (that is how a sliced sample works): the Shift macro moves every key on by up to 20 slices. Macros: Smash (the crushed bus 1 louder and harder), Room (the small-room impulse response), Shift, Dark, Snap (the slice decays to 15 % after 450 ms), Sub Layer (a sine two octaves under the key, which is also what remains if the sample pack is not installed). The break is at 120 BPM and does not follow the host tempo.

*Macros.* Smash (audible: 18.4 dB, +0.3 dB level, +0.00 oct); Room (audible: 6.8 dB, -2.8 dB level, +0.00 oct); Shift (audible: 7.2 dB, -0.0 dB level, +0.00 oct); Dark (audible: 3.8 dB, -1.6 dB level, +0.00 oct); Snap (audible: 13.3 dB, -27.2 dB level, +0.00 oct); Sub Layer (audible: 17.0 dB, +17.5 dB level, -0.00 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -21.4 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -5.7 dBFS raw = 12.3 at +18 dB. Single note: mono sum -0.0 dB vs stereo, worst band -6.3 dB at 159 Hz; DC 0.0002, click ratio 0.7, tail 0.71 s; centroid 67 Hz, moving 0.08 oct and 1.5 dB over the note. Controls (0 -> 100): wheel: 16.8 dB (+0.2 dB, +0.00 oct); velocity route: 1.7 dB (+0.0 dB, +0.00 oct). Without the content pack: chord -26.1 dBFS. Nearest preset: Dirty Distortion Bass (67).

*Spends.* polyphony 6; 1: Sample x1; 2: Sine x1; filter Analog; effects distortion, compressor, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether bus 1 crushed (bit crush at 80 %, compressor at 70:1) is a useful second copy or too harsh at the Smash macro at 100. At any other tempo than 120 BPM the slices drift against the grid; there is no tempo-sync of the sample in this build of the preset (the scan lock options belong to the scanning types, not to slicing). Slice 31 (the last) is also what every key above G4 plays.

### Bowed MPE Strings (Pad)

*Intention.* A bowed string recorded from nothing: the Sample oscillator loops the sustain of the bow tone (the loop closes on whole cycles of its vibrato, so it does not pump) and plays the release after the key goes up; a saw layer under it carries the pitch even where the sample is thin. Set up for an MPE controller: slide opens the filter, pressure adds bow noise and vibrato.

*What to play.* Chords or lines C2 .. C5, long. Set up for an MPE controller (the MPE zone on, bend range 48): slide opens the filter, pressure brings in the rosin noise and a vibrato; on a keyboard without MPE the wheel does what the slide does and the pad's pressure the rest (Full Level OFF). Release a note to hear the bow lift: the sample plays on past the loop end. Macros: Brightness, Rosin, Vibrato Rate, Bow Speed (attack and release), Hall, Saw Layer.

*Macros.* Brightness (audible: 3.2 dB, +0.1 dB level, +0.33 oct); Rosin (audible: 7.4 dB, +0.0 dB level, +0.00 oct); Vibrato Rate (acts on a rate: not seen by the spectral measure; route lfo1.rate +40 %, lfo2.rate +40 %); Bow Speed (audible: 3.4 dB, -3.5 dB level, -0.01 oct); Hall (audible: 1.5 dB, -1.1 dB level, +0.19 oct; by LF +8.4 dB); Saw Layer (audible: 6.9 dB, +6.0 dB level, +0.24 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.1 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -8.1 dBFS raw = 9.9 at +18 dB. Single note: mono sum -1.6 dB vs stereo, worst band -1.5 dB at 252 Hz; DC 0.0000, click ratio 0.2, tail 2.44 s; centroid 666 Hz, moving 1.44 oct and 9.6 dB over the note. Controls (0 -> 100): wheel: 3.2 dB (-1.3 dB, +0.33 oct); pressure: 3.5 dB (+1.2 dB, +0.10 oct); velocity route: 13.0 dB (+0.4 dB, +0.33 oct). Without the content pack: chord -23.3 dBFS. Nearest preset: Bowed Pressure Strings (74).

*Spends.* polyphony 8; 1: Sample x3; 2: Saw x3; noise; filter Analog; effects chorus, reverb, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether the loop of the sustain is really smooth by ear: it closes on whole cycles of the vibrato in the file (the mismatch of the last and the first sample of the loop is 54 dB under the signal, a 25 ms crossfade is on), but a loop in a sustained tone can show as a periodic swell every two seconds. Whether MPE on does no harm with a plain keyboard: it only widens the bend range of member channels, notes on channel 1 behave as before.

### Reverse Gong Swell (FX)

*Intention.* A gong played backwards: the whole decay of the metal is a four second crescendo that ends in the strike; the lower you play, the slower it runs (C1 swells for about eight seconds, C3 for two), noise lifts with it and a plate carries the tail past the hit.

*What to play.* Single notes C1 .. C4, held until the strike: the length of the swell follows the key (the sample is four seconds long and tuned to C2: C2 swells for 4 s, C3 for 2 s, C1 for 8 s; it plays backwards at the pitch ratio of the key to C2). Let go after the hit for the plate tail. Macros: Slow Down (to a fifth of the speed: a long, nearly still swell), Shimmer (unison detune), Air (noise), Space, Bright.

*Macros.* Slow Down (audible: 4.9 dB, -7.5 dB level, -0.20 oct); Shimmer (weak in the measure: 1.6 dB, +0.4 dB level, -0.08 oct; route osc1.unison.detune +80 %: a texture or scan control, check by ear); Air (audible: 8.2 dB, +4.2 dB level, -0.01 oct); Space (audible: 2.7 dB, -0.8 dB level, +0.10 oct; by LF -5.2 dB); Bright (audible: 8.0 dB, +1.5 dB level, +1.28 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.7 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -8.9 dBFS raw = 9.1 at +18 dB. Single note: mono sum -1.8 dB vs stereo, worst band -2.7 dB at 635 Hz; DC 0.0000, click ratio 0.0, tail 3.47 s; centroid 369 Hz, moving 0.74 oct and 11.7 dB over the note. Controls (0 -> 100): wheel: 4.9 dB (-7.5 dB, -0.20 oct); velocity route: 4.0 dB (+1.8 dB, +0.42 oct). Without the content pack: chord -28.2 dBFS. Nearest preset: Acoustic Style Pluck (51).

*Spends.* polyphony 4; 1: Sample x2; noise; filter Analog; effects reverb, eq, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* That the strike at the end of the swell is a cut, not a rise: the reversed gong ends where the original begins, with its 2 ms attack, so the end is a hard edge (the plate and the reverb round it off). Whether notes above C3 are too short to be a swell at all.

### Breath Flute Lead (Lead)

*Intention.* A solo flute: a sine-to-saw tone with a vibrato that fades in, and the Sample oscillator puts a breath chiff on the front of every separate note; pressure on the pad adds the air that blows through the tone and opens the sound. Mono, legato.

*What to play.* Mono, legato: single lines C4 .. C6 with a little glide. Each note that is not played legato starts with a breath chiff (the Sample oscillator): velocity makes it louder; in a legato line there is none, which is how a flute is played. Press the pad for air and a brighter tone. Wheel: more overblown tone and a deeper vibrato. Macros: Breath, Chiff, Vibrato Rate, Bright, Overblow, Space.

*Macros.* Breath (audible: 5.2 dB, +0.1 dB level, +0.03 oct); Chiff (audible: 6.1 dB, +0.2 dB level, +0.15 oct); Vibrato Rate (acts on a rate: not seen by the spectral measure; route lfo1.rate +40 %, lfo2.rate +40 %); Bright (audible: 2.3 dB, +0.0 dB level, +0.02 oct; by HF +7.4 dB); Overblow (audible: 5.7 dB, +0.7 dB level, +0.25 oct); Space (audible: 0.5 dB, -3.2 dB level, +0.02 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -20.3 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -14.7 dBFS raw = 3.3 at +18 dB. Single note: mono sum -0.0 dB vs stereo, worst band -0.2 dB at 400 Hz; DC 0.0001, click ratio 0.0, tail 2.29 s; centroid 270 Hz, moving 0.32 oct and 1.9 dB over the note. Controls (0 -> 100): wheel: 3.6 dB (+2.1 dB, +0.06 oct); pressure: 6.0 dB (+0.3 dB, +0.16 oct); velocity route: 9.3 dB (+0.1 dB, +0.06 oct). Without the content pack: chord -20.0 dBFS. Nearest preset: Vowel Table Lead (72).

*Spends.* polyphony 1; 1: Sine to Saw x1; 2: Sample x1; noise; filter Digital; effects delay, reverb. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* The chiff is a band of noise at a fixed pitch (not keytracked): on a very high note it may sound too low. The vibrato is the same LFO for everything, its fade-in is 800 ms.

### Spectral Vowel Freeze (Pad)

*Intention.* A sung vowel frozen into the spectrum: the Spectral oscillator re-synthesises one instant of the voice pad that a slow Perlin LFO moves along the file, so the choir changes its vowel by itself without ever beating like grains do; added harmonics make it brighter, the shift moves its formants.

*What to play.* Slow chords C2 .. C4, held for ten seconds and more. A Perlin LFO walks the play head along the voice pad, so the vowel changes by itself; the wheel is a second hand on the same axis (and a frequency shift). Pressure: more added harmonics and a brighter filter. Macros: Vowel, Harmonics, Shift (the frequencies of the whole spectrum up by up to 240 Hz), Hi Cut, Space, Saw Layer.

*Macros.* Vowel (audible: 16.2 dB, -8.4 dB level, -2.03 oct); Harmonics (audible: 3.6 dB, +0.2 dB level, +0.06 oct); Shift (audible: 6.0 dB, +0.2 dB level, +0.34 oct); Hi Cut (audible: 6.3 dB, -0.5 dB level, -0.16 oct); Space (audible: 2.2 dB, -1.6 dB level, -0.04 oct; by beat +1.6 dB); Saw Layer (audible: 3.7 dB, +6.3 dB level, -1.46 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.6 (Key 61); the real preset at 1 s: -19.7. Four-note chord peak -9.4 dBFS raw = 8.6 at +18 dB. Single note: mono sum -1.2 dB vs stereo, worst band -2.0 dB at 252 Hz; DC 0.0000, click ratio 0.0, tail 3.78 s; centroid 549 Hz, moving 0.80 oct and 31.9 dB over the note. Controls (0 -> 100): wheel: 16.3 dB (-7.4 dB, -1.84 oct); pressure: 2.3 dB (+0.1 dB, +0.05 oct); velocity route: 1.5 dB (+0.1 dB, +0.04 oct). Without the content pack: chord -29.8 dBFS. Nearest preset: Spectral Gong Pad (54).

*Spends.* polyphony 4; 1: Spectral + warp Add Harmonics x2; 2: Sine to Saw x1; filter Analog; effects reverb, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* The spectral oscillator is the dearest voice type: polyphony 4 with two unison voices each; its CPU is the first thing to check on the device. A frequency shift (not a pitch shift) makes the spectrum inharmonic: 5 Hz sounds like a slow phasing, 100 Hz and more is metallic.

### Spectral Bowed Glass (Lead)

*Intention.* A lead that is half a bow and half a glass harmonica: the Spectral oscillator re-synthesises the bowed tone (the sustain loop, read in real time) through a spectral comb that the wheel and the pressure close, and a glass table sings the fundamental with it. Mono, legato, glide.

*What to play.* Mono, legato, C3 .. C5 with some glide. Wheel and pressure close the spectral comb on the bowed tone (the harmonics at every n-th multiple are favoured): hollower, more glass-like. Macros: Comb, Glass (the table layer), Glass Shape, Bright, Vibrato Rate, Space.

*Macros.* Comb (audible: 2.9 dB, -3.3 dB level, +0.31 oct); Glass (audible: 1.0 dB, +3.4 dB level, -0.20 oct); Glass Shape (audible: 1.5 dB, -0.0 dB level, +0.20 oct; by fine structure +6.8 dB); Bright (audible: 2.5 dB, +0.1 dB level, +0.11 oct; by HF +7.1 dB); Vibrato Rate (acts on a rate: not seen by the spectral measure; route lfo1.rate +40 %); Space (audible: 2.1 dB, -2.9 dB level, +0.09 oct; by side +5.3 dB, beat +1.8 dB).

*Measured.* Level -20.0 dBFS at the sustain (XL), -20.2 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -11.9 dBFS raw = 6.1 at +18 dB. Single note: mono sum -0.2 dB vs stereo, worst band -0.2 dB at 504 Hz; DC 0.0001, click ratio 0.0, tail 2.55 s; centroid 410 Hz, moving 0.45 oct and 3.2 dB over the note. Controls (0 -> 100): wheel: 2.5 dB (-3.2 dB, +0.44 oct); pressure: 2.5 dB (-1.9 dB, +0.23 oct); velocity route: 5.4 dB (-0.5 dB, +0.23 oct). Without the content pack: chord -27.4 dBFS. Nearest preset: Vowel Table Lead (76).

*Spends.* polyphony 1; 1: Spectral + warp Spectral Comb x1; 2: Preset table x2; filter Digital; effects delay, reverb. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether the comb sounds like glass or like a phaser: it is a comb in the spectrum of the file, so the notes of the line move it relative to the harmonics. Spectral oscillator CPU: polyphony 1, unison 1.

### Spectral Gong Pad (Pad)

*Intention.* The gong's spectrum held and bowed: the Spectral oscillator scans a slice of the gong's decay back and forth (loop forward-reverse), so the inharmonic partials swell and fade like a bowed metal plate, and every unison voice reads it from another place; the bass below 200 Hz is kept in the middle.

*What to play.* Chords C2 .. C4, held long; with a big hall. The gong spectrum is read back and forth (loop forward-reverse between 8 and 55 % of the file) at a quarter of the speed. Pressure: a frequency shift of 120 Hz (inharmonic). Macros: Scan Speed (up to four times), Shift, Subharmonics (the spectral warp that adds partials an octave and more below), Bright, Space, Bell Layer.

*Macros.* Scan Speed (weak in the measure: 1.3 dB, +1.2 dB level, +0.01 oct; route osc1.sample.scan +75 %: a texture or scan control, check by ear); Shift (audible: 9.3 dB, +0.0 dB level, +0.07 oct); Subharmonics (audible: 7.7 dB, +0.0 dB level, -0.08 oct); Bright (audible: 3.0 dB, +0.6 dB level, +0.35 oct); Space (audible: 4.7 dB, -1.2 dB level, -0.36 oct); Bell Layer (audible: 8.3 dB, +11.3 dB level, +0.20 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.4 (Key 61); the real preset at 1 s: -26.0. Four-note chord peak -6.3 dBFS raw = 11.7 at +18 dB. Single note: mono sum -1.5 dB vs stereo, worst band -4.7 dB at 2540 Hz; DC 0.0000, click ratio 0.0, tail 5.54 s; centroid 1143 Hz, moving 0.23 oct and 34.4 dB over the note. Controls (0 -> 100): wheel: 6.8 dB (+1.3 dB, +0.03 oct); pressure: 7.2 dB (+0.8 dB, +0.39 oct); velocity route: 5.3 dB (+1.2 dB, +0.40 oct). Without the content pack: chord -27.9 dBFS. Nearest preset: Spectral Vowel Freeze (54).

*Spends.* polyphony 4; 1: Spectral + warp Add Subharmonics x2; 2: Bell Partials x1; filter Analog; effects reverb, eq, utility. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* That a re-synthesised gong is a pad at all: the partials are inharmonic and keytracked by the shift of the whole spectrum, so a chord of three notes is a cluster of inharmonic partials; this is a drone for film, not for songs. The level (the measurement is made at the sustain, after an attack of 1.2 s).

### Spectral Chord Morph (FX)

*Intention.* A four-chord progression re-synthesised as one evolving spectrum: the Spectral oscillator plays the file once, in real time, so holding a key walks through C major 7, A minor 7, F major 7 and G 6 as a morph; the spectral shift and the gate turn the harmony into something stranger as the wheel goes up.

*What to play.* One key, held 8 seconds or longer: the file plays once in real time, C major 7, A minor 7, F major 7, G 6 in turn (2 s each), and the oscillator re-synthesises them from their spectrum, so the changes are smooth morphs; other keys transpose all of it. Wheel: a spectral shift warp and a frequency shift (the harmony turns strange). Macros: Slow (to a fifth of the speed), Shift Warp, Freq Shift, Hi Cut, Space, Bright.

*Macros.* Slow (audible: 5.0 dB, -1.0 dB level, -0.23 oct); Shift Warp (audible: 23.2 dB, -0.7 dB level, +1.84 oct); Freq Shift (audible: 15.6 dB, -0.2 dB level, +0.89 oct); Hi Cut (audible: 19.0 dB, -4.9 dB level, -0.73 oct); Space (audible: 1.1 dB, -1.8 dB level, +0.20 oct; by beat +1.2 dB); Bright (audible: 0.6 dB, +0.0 dB level, +0.03 oct; by HF +9.2 dB).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.8 (Key 61); the real preset at 1 s: -20.1. Four-note chord peak -9.2 dBFS raw = 8.8 at +18 dB. Single note: mono sum -1.2 dB vs stereo, worst band -2.7 dB at 504 Hz; DC 0.0000, click ratio 0.0, tail 4.78 s; centroid 203 Hz, moving 0.76 oct and 21.2 dB over the note. Controls (0 -> 100): wheel: 21.7 dB (-0.0 dB, +1.53 oct); pressure: 0.6 dB (+0.0 dB, +0.03 oct); velocity route: 2.1 dB (+0.1 dB, +0.07 oct). Without the content pack: chord -27.5 dBFS. Nearest preset: Spectral Vowel Freeze (57).

*Spends.* polyphony 3; 1: Spectral + warp Spectral Shift x2; 2: Sine to Saw x1; filter Analog; effects chorus, reverb, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* Whether the loop at the end of the file (forward loop over the whole file) is a jump: the file's last chord (G 6) is faded out and the first one faded in, so the loop is a dip. The wheel changes the spectrum a great deal (see the measured line): at the top of its travel the shift warp is at 60 % and the frequency shift 160 Hz.

### Reese Dual Phase Bass (Bass)

*Intention.* A Reese from a table that is two opposite saws whose distance breathes: the cycle itself is the phasing, five unison voices and a second saw an octave down thicken it, a slow LFO walks the table, a tube and the amp cabinet impulse response put it in a speaker; the bottom stays in the middle.

*What to play.* Long notes on C1 .. C3 (MIDI 24 .. 48), chords of fifths; velocity: harder is brighter and dirtier. Wheel: cutoff and drive. Pressure: the cutoff. Macros: Phase (the table's frame: the distance between the two saws), Cutoff, Growl (tube drive), Dry Cab (the cabinet convolved at 55 %: 0 is the full cabinet), Octave Saw, Movement Rate.

*Macros.* Phase (audible: 2.6 dB, -3.9 dB level, +0.66 oct); Cutoff (audible: 14.1 dB, +0.2 dB level, +0.76 oct); Growl (audible: 21.6 dB, +7.8 dB level, +0.73 oct); Dry Cab (audible: 4.3 dB, -5.0 dB level, +0.28 oct); Octave Saw (audible: 2.7 dB, +1.0 dB level, +0.18 oct; by HF +6.4 dB); Movement Rate (acts on a rate: not seen by the spectral measure; route lfo1.rate +60 %).

*Measured.* Level -20.0 dBFS at the sustain (XL), -26.2 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -6.6 dBFS raw = 11.4 at +18 dB. Single note: mono sum -0.2 dB vs stereo, worst band -1.4 dB at 2016 Hz; DC 0.0002, click ratio 0.2, tail 0.22 s; centroid 144 Hz, moving 1.61 oct and 7.8 dB over the note. Controls (0 -> 100): wheel: 18.2 dB (+4.2 dB, +0.92 oct); pressure: 14.1 dB (+0.2 dB, +0.76 oct); velocity route: 21.2 dB (+7.6 dB, +0.51 oct). Without the content pack: chord -25.8 dBFS. Nearest preset: Ladder FM Bass (59).

*Spends.* polyphony 6; 1: Preset table x5; 2: Saw x3; filter Analog; effects distortion, eq, utility, convolve. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* The cabinet impulse response on a bass: it is a guitar-amp colour (hump at 2.5 kHz, 80 Hz roll-off); on a bass it may thin the low mid. On the Key 61 (`low`) Convolve is bypassed: the sound is 6 dB quieter and without the cabinet.

### Vowel Table Lead (Lead)

*Intention.* A solo voice from a table of five vowels: the wheel walks A, E, I, O, U on the frame axis, pressure on the pad opens the mouth and brings in a vibrato, velocity decides how bright the attack is; three unison voices make a small section, the legato mono line glides.

*What to play.* Mono, legato, C3 .. C5 with glide. The wheel walks the five vowels A, E, I, O, U on the frame axis (at 127 it is U); pressure opens the sound and brings in the vibrato; velocity brightens the attack. Macros: Vowel, Dark, Choir (the unison spread), Octave Down, Vibrato Rate, Space.

*Macros.* Vowel (audible: 4.5 dB, +1.5 dB level, -0.46 oct); Dark (audible: 16.7 dB, -5.0 dB level, -1.20 oct); Choir (audible: 4.5 dB, -0.1 dB level, -0.06 oct); Octave Down (audible: 4.1 dB, +5.9 dB level, -1.60 oct); Vibrato Rate (acts on a rate: not seen by the spectral measure; route lfo1.rate +40 %); Space (audible: 1.7 dB, -1.0 dB level, +0.01 oct; by side +4.1 dB).

*Measured.* Level -20.0 dBFS at the sustain (XL), -19.6 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -10.3 dBFS raw = 7.7 at +18 dB. Single note: mono sum -0.4 dB vs stereo, worst band -0.9 dB at 1600 Hz; DC 0.0000, click ratio 0.0, tail 2.58 s; centroid 720 Hz, moving 0.49 oct and 4.6 dB over the note. Controls (0 -> 100): wheel: 4.5 dB (+1.5 dB, -0.46 oct); pressure: 3.4 dB (-0.6 dB, +0.16 oct); velocity route: 2.7 dB (+0.1 dB, -0.03 oct). Without the content pack: chord -17.9 dBFS. Nearest preset: MPE Pressure Lead (69).

*Spends.* polyphony 1; 1: Preset table x3; 2: Triangle x1; filter Digital; effects delay, reverb. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* The table's formants are fixed in Hz for a voice at 220 Hz: on a note an octave higher the same frame has the formants between the harmonics, an octave lower they sit on harmonics of a different number; the best range is A2 .. A4 and above it the vowels turn into bright tones.

### Drawbar Organ Keys (Keys)

*Intention.* A tonewheel organ from a table of sixteen drawbar registrations (the table holds the 16 foot fundamental, so the oscillator is set an octave up): the frame is the registration, a key click and a fast second-harmonic percussion sit on the front of every note, the rotary is a chorus and a phaser, and a second copy overdrives on bus 1.

*What to play.* Chords C2 .. C5. The Registration macro walks the sixteen drawbar registrations (888000000, 888800000, 008800000, 800000008 ... jazz, gospel, full, whistle ...). A key click and a fast second-harmonic percussion sit on every note; the wheel is the rotary's vibrato and brings in more chorus and phaser. Macros: Registration, Percussion, Overdrive (bus 1), Click, Rotary Speed, Bright.

*Macros.* Registration (audible: 17.6 dB, +1.8 dB level, -0.30 oct); Percussion (audible: 3.4 dB, +3.2 dB level, -0.47 oct); Overdrive (audible: 18.2 dB, +11.1 dB level, +0.47 oct); Click (audible: 0.6 dB, +0.0 dB level, +0.00 oct; by LF +20.8 dB); Rotary Speed (acts on a rate: not seen by the spectral measure; route chorus.rate +55 %, phaser.rate +45 %); Bright (audible: 1.4 dB, +1.0 dB level, +0.50 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -20.0 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -7.3 dBFS raw = 10.7 at +18 dB. Single note: mono sum -1.4 dB vs stereo, worst band -4.1 dB at 400 Hz; DC 0.0000, click ratio 0.9, tail 0.27 s; centroid 1593 Hz, moving 0.34 oct and 2.6 dB over the note. Controls (0 -> 100): wheel: 2.2 dB (+0.9 dB, +0.06 oct); velocity route: 47.9 dB (+3.3 dB, +0.21 oct). Without the content pack: chord -19.9 dBFS. Nearest preset: Ladder FM Bass (82).

*Spends.* polyphony 10; 1: Preset table x1; 2: Sine x1; noise; filter Analog; effects chorus, phaser, distortion, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* That the 16 foot fundamental in the cycle with the oscillator set an octave up (transpose +12) is what a tonewheel organ's bottom is: it is the same pitch, but the oscillator's transpose and the table's lowest octave mean that the keys below C2 sound at the lowest frame's pitch minus one octave. The Leslie is a chorus and a phaser at the same 0.8 Hz: there is no amplitude modulation.

### Throat Singer Drone (Pad)

*Intention.* Overtone singing: a drone with one narrow resonance that a slow LFO walks up and down the harmonic series from the fourth to the twentieth partial, so a tune of whistling overtones floats over a steady note; kept in the Phrygian dominant scale on D so that chords of fingers stay consonant.

*What to play.* One or two notes C3 .. C4 (they sound an octave lower), held; legato with a long glide (120 ms). The key is moved to the Phrygian dominant scale on D (D, E flat, F sharp, G, A, B flat, C; measured: C sharp plays C, E plays E flat, G sharp plays G). A slow LFO (0.07 Hz) and a Perlin LFO walk the resonance up and down the overtones, so a melody of whistling partials sings over the drone; the wheel speeds the walk up. Macros: Overtone, Walk Speed, Drone (the sub sine two octaves down), Bright, Space, Choir.

*Macros.* Overtone (audible: 5.6 dB, +0.2 dB level, +0.00 oct); Walk Speed (acts on a rate: not seen by the spectral measure; route lfo1.rate +60 %); Drone (audible: 1.9 dB, +8.4 dB level, -1.94 oct); Bright (audible: 2.2 dB, +0.3 dB level, +0.22 oct; by HF +8.0 dB); Space (audible: 2.1 dB, -1.9 dB level, -0.18 oct; by beat +1.1 dB); Choir (audible: 6.5 dB, -0.4 dB level, -0.06 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -20.0 (Key 61); the real preset at 1 s: -30.9. Four-note chord peak -7.3 dBFS raw = 10.7 at +18 dB. Single note: mono sum -0.5 dB vs stereo, worst band -1.3 dB at 252 Hz; DC 0.0000, click ratio 0.0, tail 6.16 s; centroid 523 Hz, moving 1.07 oct and 39.5 dB over the note. Controls (0 -> 100): wheel: 6.3 dB (+0.1 dB, -0.41 oct); pressure: 4.1 dB (-0.1 dB, +0.11 oct); velocity route: 3.1 dB (+0.6 dB, +0.73 oct). Without the content pack: chord -29.5 dBFS. Nearest preset: Dark Cinema Drone (68).

*Spends.* polyphony 3; 1: Preset table x2; 2: Sine x1; filter Analog; effects reverb, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* The level (measured at the sustain; an attack of 1.5 s) and whether the overtone resonance is audible as a melody and not just a filter sweep: the narrow resonance (one partial wide) is chosen for it; the Overtone macro's two ends differ by the distance in the macro line above.

### Ladder FM Bass (Bass)

*Intention.* An FM bass from the ratio ladder: the table steps through modulator ratios 1 to 8 (eight frames each, the index rising inside every step), an envelope throws the frame up at the front of the note so a hard key lands on a higher ratio and settles back into the fundamental, a sine sub keeps it big.

*What to play.* Single notes C1 .. C3 (MIDI 24 .. 48), short and held. Velocity decides how far the ratio is thrown at the front of the note (a hard key starts on a higher ratio of the FM ladder, with the index rising; it settles into the fundamental in 260 ms). Wheel: the ratio and the drive. Macros: Ratio (the frame: steps through the ratios 1 .. 8, eight frames each), Snap Length, Drive, Sub, Cutoff, Decay.

*Macros.* Ratio (audible: 31.4 dB, -0.3 dB level, +2.32 oct); Snap Length (audible: 15.1 dB, +0.2 dB level, +0.96 oct); Drive (audible: 33.0 dB, +9.0 dB level, +0.83 oct); Sub (audible: 6.7 dB, +6.5 dB level, +0.01 oct); Cutoff (audible: 0.1 dB, +0.0 dB level, +0.02 oct; by HF +18.6 dB); Decay (audible: 6.1 dB, +4.2 dB level, +0.00 oct).

*Measured.* Level -20.0 dBFS at the sustain (XL), -20.0 (Key 61); the real preset at 1 s: -20.0. Four-note chord peak -8.5 dBFS raw = 9.5 at +18 dB. Single note: mono sum 0.0 dB vs stereo, worst band 0.0 dB at 32 Hz; DC 0.0001, click ratio 0.9, tail 0.14 s; centroid 167 Hz, moving 0.80 oct and 2.2 dB over the note. Controls (0 -> 100): wheel: 33.4 dB (+7.9 dB, +1.99 oct); pressure: 0.1 dB (+0.0 dB, +0.02 oct); velocity route: 24.1 dB (+1.0 dB, +1.20 oct). Without the content pack: chord -20.7 dBFS. Nearest preset: Dirty Distortion Bass (56).

*Spends.* polyphony 4; 1: Preset table x1; sub; filter Analog; effects distortion, eq. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.

*Not settled for the ear.* That the frame steps through the ratios are audible as steps and not as one sound getting brighter: each step is a different set of sidebands, the index rising inside it; with the cutoff at 1.5 kHz at C1 the higher ratios are hidden by the filter (open it with Cutoff).

## 7. Engine limits and defects found (Sample, Granular, Spectral, Multisample, Convolve)

Everything below was seen in a render or in the code, with the numbers; nothing was worked around silently (the workaround, where there is one, is named). Items 1 and 2 of the list of batch 1 (Ladder at the top, Direct out and `master.volume`) and the other engine items of that list are being fixed elsewhere and are not repeated; batch 1's item 13 ("could not be shown") is what this batch closes.

1. **A missing content file is not replaced by anything.** `docs/ARCHITECTURE.md` section 6 says the plugin tolerates absence and that a sound is produced either way. Measured with an empty store: before the presets got their wavetable layers, Frozen Pluck Drone, Gong Grain Swarm, Stutter Grain Kit, Rising Grain Riser, Spectral Chord Morph and Breakbeat Slice Keys were **silent** (chord RMS -180 dBFS: a Sample, Granular, Spectral or Multisample oscillator without its file produces nothing, docs/engine-spec/07 section 1), Convolve without its impulse does not convolve, a `Preset table` plays the saw. Nothing tells the player: there is no `missing_content` indicator on the MPC. Consequence for the set: every batch 2 preset has a plain wavetable layer (level 10 .. 30 % of its oscillator) that is what you hear if the packs are not installed (the table's column "Without the pack" shows -17.9 .. -32.8 dBFS), and the `synthyboy_preset_sanity` gate (no store by default) would fail "silent" for them; its CMake call now passes `--content-dir` to a store of links to the packs.
2. **`synthyboy_sound_lab` and `synthyboy_tier_bench` could not load content** (no `setContentStore`), so batch 1 could not show these paths and this batch could not use them for the CPU and the full report; fixed in the sources, needs a rebuild (section 1).
3. **The root note and the loop points of a WAV (`smpl` chunk) are ignored by the Sample, Granular and Spectral oscillators**: `lib_osc.cpp` pitches by `oscN.sample.root` (default 60) and loops by the oscillator's own `sample.loop_start / loop_end` in percent of the file; `WavInfo::rootNote`, `loopStart` and `loopEnd` are read by the file loader (the UI copies the root to the parameter when it loads a file) but not used by a preset that names the file. So a preset must carry `sample.root` and the loop percentages itself (the bowed tone's 14.3 .. 77.7 % are computed from its frames); the chunk in the pack's files is information for other tools. Also `ref.name` is passed to the WAV reader and a note suffix in it ("_C3", " F#2") overrides the chunk's root: the pack's names have none.
4. **`oscN.multi.timbre` barely changes what you hear with an instrument of stretched zones.** It moves the *lookup key* of the zone (`timbreKey`: `key + t * (127 - 2 key)`), not the pitch: the note stays in tune and a zone from another register is stretched to it. Measured on Nylon Zone Pluck at MIDI 36 (lookup key 91 at +100 %, zone root 81, a shift of 45 semitones): the fundamental stays 65 Hz, the centroid 556 -> 520 Hz, the spectral distance 1.3 dB; on MIDI 60 and +25 / 50 / 100 % it is exactly 0.0 dB because 60 .. 67 are one zone. It is a working control for an instrument with different recordings per zone (the marimba's Zone Shift: 2.0 dB at the Bass note), not for the pluck. The macro of Nylon Zone Pluck was changed to a unison double for that reason.
5. **Granular and spectral texture controls are invisible to a spectral distance.** Density 6 / 26 / 100 grains a second changes the 1/3 octave bands by 1.5 / 0 / 0.9 dB (Chord Cloud Pad), grain length 30 vs 180 ms 2.6 dB, random pitch 12 semitones 5.5 dB, random offset 60 % 1.8 dB; the controls that move the spectrum (pitch scatter, filter, frequency shift) are the audible ones. The macros of density and length are kept (they are the point of the type) and are marked as texture controls above.
6. **The modulation range of a dB parameter is the whole range**: `bus1.level` and `bus2.level` run -60 .. +6 dB, so a route of 50 % is **+33 dB** (the amount is a share of the range: `docs/engine-spec/04`). The first build of Frozen Pluck Drone, Gong Grain Swarm, Rising Grain Riser and Breakbeat Slice Keys had such routes (macros of 40 .. 50, wheel 20 .. 30) and would have jumped by 13 .. 33 dB; they are 12 .. 14 % (macros) and 8 % (wheel) now. Not a defect, a trap: the same holds for `convolve.ir_gain` (64 dB range) and `master.volume`.
7. **Convolve follows the Vital law's level: the impulse is used as it is** (`convolve.law` 0; with the Serum law the engine normalises the impulse to a peak of 1 and applies `ir_gain`, default -18 dB: `core/fx-serum-b/convolve.cpp`). A file's own level decides the wet level, so the impulses of the pack are scaled to an L2 norm of 1 (wet about equal to dry for noise) and the presets leave `ir_gain` at 0. A user's impulse straight from a reverb plugin, peak-normalised, would be 20 dB or more louder. The Key 61 (`low`) bypasses Convolve: Dusty Rhodes Keys and Reese Dual Phase Bass are 5 .. 6 dB quieter there (the cabinet's gain is part of their level) and Marimba Layer Keys 3 dB.
8. **Spectral oscillator**: the spectral warps all act (distance to no warp at amount 70 on the gong: Spectral Pitch Shift 14.2 dB, Spectral Shift 13.0, Add Subharmonics 11.4, Peak Harmonic Down 9.9, Spectral Spread 8.4, Spectral Gate 7.0, Shepard Narrow 6.0 ... down to Spectral Mirror and Phase Twist 0.9 dB and Spectral Smear 1.0, which change this file very little), the spectral filter keys `spec.cut`, `spec.mix` do nothing without `spec.flt_use` (no defect: they belong to the filter's own table), `spec.transients` did not change a smooth sound. The oscillator's level is rate-aware in the own engine (chord level on the Key 61 within 1 dB of the XL for all four).
9. **`check-presets.py` rule `mod_slot_duplicate`** (batch 1, item 10) hit again (Breath Flute Lead: a vibrato and the wheel's deepening of it on one LFO); worked around with a second LFO of the same rate and macros that move both rates.
10. **Things that work as documented and were measured, for the record**: the voice control lanes (pan 16 dB at a lane value of 80; cutoff about an octave at 60; detune 0.8 semitone at 80; the pattern follows the note count), the buses (parallel copy, wet-only rack, destination None), the scale conform (Hirajoshi, Phrygian dominant), the sample reverse (the gong's level in 0.2 s steps at MIDI 60: -39 -37 -32 -33 -27 dB then falling, forward: -30 -32 -32 -37 ...), the slice mode with manual slices (key = slice from `slice_root`: slice 0 is the kick, 98 % of its energy under 200 Hz; slice 4 the snare, 77 / 14 / 9 % low / mid / high), the SFZ velocity layers (the same note with the two layers' velocity ranges swapped in a temporary copy of the pack: the soft file's first 30 ms has its centroid at 556 Hz, the hard one's at 1016 Hz, 0.07 % against 0.80 % of the energy above 2 kHz), the 24 bit stereo impulse and the SFZ parser (every zone of all three instruments plays).
11. **MPE was not measurable**: `sound-lab` has no per-channel expression; `master.mpe.on` was checked by reading `own_engine.cpp` (it changes the bend range of channels above 0 to `master.mpe.bend_range` and maps the channel pressure to Poly Aftertouch; the manager channel behaves as before) and by the set-up of Bowed MPE Strings, not by a render. `master.mpe.y_modwheel` makes the slide act as the mod wheel in the matrix (the preset routes both).

## 8. Not shown, and why

- CPU of every preset (section 4) and the eight-note velocity-127 peak, the register / velocity / long-hold report of `sound-lab.py`: need the rebuilt tools.
- The numbers are those of the own engine the MPC runs.
- Wavetable warps and morphs: not used (4x warp costs 7 .. 9 % of the XL block per oscillator; batch 1 spent them). The filter models Sample Hold, New Wsp, EQ 6 dB, Misc stay unused; the Spectral filter (`spec.flt_use`) and the drawn spectral mask, the multisample envelope override (`multi.override`), key switches and round robin in an SFZ, release triggers: not used.
- Per-note effects are not possible (batch 1, item 8); the Key 61 loses Convolve, reverb, flanger, Hyper/Dimension and Bode.

## 9. Batch 3

1. A listening round on these 24 and on batch 1 (the "Not settled" lines), the first things: the loop of Bowed MPE Strings, the grain clouds' smoothness at their densities, the zone seams of the instruments, the vinyl bed under chords, the level of the four Spectral presets against the rest.
2. The rebuild and `sound-lab.py report` / `tier_bench` for the 24 (section 1), and a decision about the content packs in `plugin.json` and about the desktop shells' pack root.
3. A second content pack round from what the ear says: more instruments (an electric piano and a string section with velocity layers and release triggers), more impulse responses (a hall, a spring, a tape), a drum kit for the slice mode, real loops.
4. A pass of the first 136 against the same measures: the velocity (batch 1's list) and the pad pressure on the held ones.

