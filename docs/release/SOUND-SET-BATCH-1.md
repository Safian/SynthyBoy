# Sound set, batch 1: 24 new presets

Status: **built and measured, NOT HEARD.** Nothing here was listened to (the author cannot hear): every claim below is a measurement of the offline render of the engine the MPC runs (own engine, no Vital), through the two quality profiles of the devices, and every statement about sound is an intention that the measurements support or do not contradict. The owner's ears decide; the lines "Not settled for the ear" name what to listen for first. Written by `scripts/gen-sound-set-batch1-doc.py` from `docs/release/sound-set-batch1-measurements.json`.

The 24 presets are added to the set (136 now); the 112 of the first sets are unchanged, byte for byte. Each was built from Init (the default values), not from the generator's template: on average 97 parameters differ from the defaults (the first hundred and twelve: 34 .. 64). Files: `content/presets/<Category>/*.sbpreset`; source: `scripts/factory-presets-batch1.py` and `scripts/batch1/*.py` (data written with `scripts/sb_preset_dsl.py`), names of the macros in `scripts/factory-preset-macro-names.json`, levels in `scripts/factory-preset-levels.json`.

| Role | Presets |
|---|---|
| Bass (4) | Subterranean Duo (Bass), Bend Formant Growl (Bass), Triple Fold Reese (Bass), Pitch Drop 808 (Bass) |
| Leads (4) | Sync Cry Lead (Lead), Vowel Whistle Lead (Lead), Triple Fifth Lead (Lead), Bell FM Ripple Lead (Lead) |
| Pads (4) | Stretched Glass Choir (Pad), Mid Side Halo (Pad), Parallel Dusk Pad (Pad), Brass Swell Ensemble (Pad) |
| Keys / plucks (3) | Tine Glass Keys (Keys), Karplus Harp (Pluck), Ring Mod Gamelan (Pluck) |
| Textures / atmospheres (3) | Shepard Ascent (Pad), Lorenz Garden (Pad), Breath of Machines (FX) |
| Sequence / rhythm (2) | Pentatonic Step Pluck (Pluck), Tempo Gate Chords (Keys) |
| FX / one-shots (2) | Bode Barber Riser (FX), Detonation Tail (FX) |
| Performance, pad pressure + macros (2) | Bowed Pressure Strings (Pad), Wah Pressure Clav (Keys) |

## 1. How it was measured

`build-sound/tools/sound-lab/synthyboy_sound_lab` (new, `tools/sound-lab/`) renders one preset offline under a device profile with scripted notes, pressure, wheel and macro settings (44.1 kHz, 128 frame blocks, quality of the profile); `scripts/sound-lab.py` analyses the files. Profiles: **XL** = `high,voices=16,control=economy` (what the MPC XL runs), **Key 61** = `low` (8 voices, unison cap 3, 2x warp, reverb / flanger / Hyper-Dimension / Bode / Convolve bypassed). The raw engine output is measured; the MPC shell adds a fixed +18 dB after it (`SYNTHYBOY_MPC_OUTPUT_GAIN_DB`, no limiter).

- **Level**: RMS of the loudest 300 ms of a four note chord at velocity 1.0 (the measure of `scripts/tune-preset-levels.py`, reference -20 dBFS), where a swelling sound is held until its attack is over (attack + 0.4 s, at most 5 s; the first hundred are levelled after one second, which leaves slow pads far louder at the sustain). `master.volume` is tuned to -20.0 on the XL profile; the Key 61 profile is then within the table's figures.
- **Clipping**: the peak of an eight note chord at velocity 127, raw and after the +18 dB (see section 2).
- **Stereo**: correlation of L and R, the level of the mono sum against the stereo level (0 dB = fully correlated, -3 dB = uncorrelated), and the worst 1/3 octave band of the mono sum against the stereo band (a hole would be below -10 dB).
- **Clean**: DC (mean of a held note), the largest first difference in the first 3 ms against the next 100 ms (click ratio; 3 or more looks like a click), the tail (seconds after the note off until under -90 dBFS; the render window ends at 4 s, so 4.00 means 'longer').
- **Response**: velocity 30 / 80 / 127 level and centroid; for every macro, the wheel and a pressure ramp: the distance between the sound at 0 and at 100 (mean difference of the 1/3 octave bands, level, centroid, side level, beat movement, level above 6 kHz and below 200 Hz, fine-structure distance of the 5 Hz spectrum). A control counts as audible when one of: distance >= 3 dB, level >= 3 dB, centroid >= 0.3 oct, side >= 3 dB, beat >= 1 dB, HF or LF >= 4 dB, fine >= 6 dB. Macros that act on a tempo-synced or free LFO rate, on a vibrato, or on a route that only works with pressure (Wah Range) are not seen by these measures and were checked by hand (listed per preset).
- **Movement**: over a 10 s held note, the spread (5th to 95th percentile) of the spectral centroid in octaves and of the level in dB, and the mean change of the 8 Hz spectrum in 0.5 s.
- **Distance to the nearest preset**: the survey's similarity measure (`scripts/survey-presets.py`): the number of parameters whose values differ. The survey found the first 112 within 4 .. 71 of each other (mean 37.7, a template cluster at 15 or fewer); the 24 here are 71 .. 123 from the nearest of the 136 (table in section 5).
- **CPU**: `synthyboy_tier_bench` on the Mac (arm64, build Release, 3 passes, 4 s each), the engine as the MPC shell runs it: Key 61 profile `low` with 8 notes, XL profile with 16 notes, a chord struck again every second. The Mac is 15 to 25 times faster than the devices: only the comparison with the dearest existing presets in the same run counts (section 5).

## 2. The level and the +18 dB output stage: the two rules cannot both hold

The brief asks for presets that sit at the set's reference level (chord RMS -20 dBFS) **and** do not clip at the +18 dB stage with an eight note chord at velocity 127. They are incompatible: -20 dBFS RMS plus 18 dB is -2 dBFS RMS, so any signal with a crest factor above 2 dB is over full scale. Measured: the 8-note velocity-127 peak of the 24 is -1.3 .. -12.0 dBFS raw, which is **6.0 .. 16.7 dBFS after the +18 dB**; the first 112 are in the same range (Lush Analog Pad -4.8 raw, Fat Reese -4.4 raw, Screaming Sync Lead -12.9 raw: +13.2, +13.6, +5.1 at the stage). I levelled to the reference, because the first hundred are levelled to it and the owner compares by ear, and I report the number per preset so the decision is his:

- to be sure of 0 dBFS at the stage with the eight note chord, every preset has to go down by its figure in the table (about 11 to 22 dB), and then sits 11 to 22 dB under the first 112: not comparable;
- or the stage gets a limiter (`SYNTHYBOY_MPC_OUTPUT_LIMITER=ON` is the existing switch; the track's own limiter does it too).

What I did within the brief: the percussive presets (Karplus Harp, Pentatonic Step Pluck, Detonation Tail) end with the compressor in **Limit** mode as the last effect (`fx.order` with the compressor last: the default order has it third, before the EQ, the chorus and the utility, which then add to the peaks), so their peak is a ceiling, not a crest; they have the smallest overshoot of the batch.

## 3. Summary table

Level = chord RMS in dBFS (XL / Key 61). Peak = 8-note velocity 127: raw dBFS, and after +18 dB. Mono = level of the mono sum against the stereo level (XL), worst 1/3 octave band. DC as a fraction of full scale.

| Preset | Level XL / 61 | Peak raw -> at +18 | Mono / worst band | DC | Click | Tail s |
|---|---|---|---|---|---|---|
| Subterranean Duo | -20.0 / -20.0 | -7.1 -> 10.9 | -0.5 / -2.1 @ 5080 Hz | 0.0000 | 0.2 | 0.13 |
| Bend Formant Growl | -20.0 / -20.0 | -6.7 -> 11.3 | 0.0 / 0.0 @ 32 Hz | 0.0002 | 0.9 | 0.14 |
| Triple Fold Reese | -20.0 / -20.0 | -7.4 -> 10.6 | -1.5 / -4.7 @ 2016 Hz | 0.0004 | 1.2 | 0.15 |
| Pitch Drop 808 | -20.0 / -20.0 | -1.7 -> 16.3 | 0.0 / 0.0 @ 635 Hz | 0.0001 | 3.4 | 0.00 |
| Sync Cry Lead | -20.0 / -19.8 | -1.8 -> 16.2 | -0.2 / -1.1 @ 318 Hz | 0.0000 | 0.1 | 3.50 |
| Vowel Whistle Lead | -20.0 / -20.2 | -9.6 -> 8.4 | -0.6 / -1.0 @ 800 Hz | 0.0014 | 0.0 | 2.00 |
| Triple Fifth Lead | -20.0 / -18.1 | -4.5 -> 13.5 | -1.9 / -2.8 @ 800 Hz | 0.0008 | 0.1 | 1.55 |
| Bell FM Ripple Lead | -20.0 / -19.4 | -1.3 -> 16.7 | -0.8 / -3.3 @ 1008 Hz | 0.0001 | 1.1 | 2.67 |
| Stretched Glass Choir | -20.0 / -19.8 | -3.8 -> 14.2 | -0.7 / -1.5 @ 3200 Hz | 0.0000 | 0.0 | 3.78 |
| Mid Side Halo | -20.0 / -19.9 | -2.6 -> 15.4 | -1.1 / -2.4 @ 5080 Hz | 0.0003 | 0.0 | 3.49 |
| Parallel Dusk Pad | -20.0 / -19.4 | -8.4 -> 9.6 | -2.1 / -5.6 @ 4032 Hz | 0.0000 | 0.0 | 3.45 |
| Brass Swell Ensemble | -20.0 / -19.7 | -6.5 -> 11.5 | -1.7 / -4.5 @ 100 Hz | 0.0000 | 0.0 | 1.95 |
| Tine Glass Keys | -20.0 / -20.6 | -1.4 -> 16.6 | -0.2 / -2.5 @ 100 Hz | 0.0012 | 1.4 | 0.71 |
| Karplus Harp | -20.0 / -20.0 | -12.0 -> 6.0 | -1.8 / -8.9 @ 32 Hz | 0.0005 | 1.0 | 0.84 |
| Ring Mod Gamelan | -20.0 / -20.1 | -3.6 -> 14.4 | -0.7 / -2.1 @ 200 Hz | 0.0000 | 0.6 | 3.63 |
| Pentatonic Step Pluck | -20.0 / -20.2 | -7.8 -> 10.2 | -1.5 / -6.5 @ 200 Hz | 0.0004 | 0.7 | 4.00+ |
| Tempo Gate Chords | -20.0 / -19.9 | -2.8 -> 15.2 | -1.1 / -2.5 @ 100 Hz | 0.0001 | 1.2 | 3.01 |
| Shepard Ascent | -20.0 / -18.6 | -5.8 -> 12.2 | -1.4 / -2.2 @ 800 Hz | 0.0001 | 0.0 | 4.00+ |
| Lorenz Garden | -20.0 / -19.8 | -4.2 -> 13.8 | -1.3 / -3.0 @ 504 Hz | 0.0001 | 0.0 | 4.00+ |
| Breath of Machines | -20.0 / -19.6 | -3.4 -> 14.6 | -1.3 / -9.7 @ 252 Hz | 0.0000 | 0.0 | 4.00+ |
| Bode Barber Riser | -20.0 / -16.0 | -6.6 -> 11.4 | -0.2 / -2.1 @ 400 Hz | 0.0000 | 0.0 | 1.91 |
| Detonation Tail | -20.0 / -19.1 | -3.5 -> 14.5 | -0.4 / -1.1 @ 635 Hz | 0.0004 | 0.4 | 4.00+ |
| Bowed Pressure Strings | -20.0 / -19.5 | -3.5 -> 14.5 | -0.8 / -1.2 @ 504 Hz | 0.0000 | 0.1 | 2.01 |
| Wah Pressure Clav | -20.0 / -20.0 | -9.8 -> 8.2 | -1.5 / -3.8 @ 200 Hz | 0.0000 | 0.9 | 0.12 |

## 4. CPU against the budget

Rule (owner): nothing new may be dearer than the current dearest preset, on the Key 61 profile (`low`, 8 notes: p99 of the dearest about 60 % of the block on the device) and on the XL profile. The same run, medians of 3 passes, share of the block: **dearest existing presets in this run**:

| Existing preset | Key 61 median / p99 | XL median / p99 |
|---|---|---|
| Deep Sea Ambient | 2.86 / 4.06 | 11.53 / 13.32 |
| Acid Saw Reso | 2.87 / 4.34 | 10.89 / 13.32 |
| Moog Ladder Bass | 2.87 / 4.01 | 10.65 / 12.59 |
| Phaser Sweep Lead | 4.78 / 6.02 | 10.46 / 12.93 |
| Twin Warp Lead | 2.47 / 4.01 | 10.07 / 13.20 |
| Diode Saturated Bass | 3.72 / 5.01 | 9.97 / 12.46 |
| Sync Growl Bass | 2.24 / 3.77 | 9.87 / 14.06 |
| Diode Bite Lead | 3.40 / 4.67 | 9.38 / 11.92 |

Dearest existing: Key 61 median 4.78, p99 6.02; XL median 11.53, p99 14.06. **The 24 new presets:**

| Preset | Polyphony | Key 61 median / p99 | XL median / p99 | Verdict |
|---|---|---|---|---|
| Subterranean Duo | 6 | 2.92 / 4.19 | 2.92 / 4.37 | within |
| Bend Formant Growl | 6 | 3.20 / 4.79 | 4.98 / 6.86 | within |
| Triple Fold Reese | 6 | 3.42 / 5.27 | 4.25 / 6.14 | within |
| Pitch Drop 808 | 1 | 0.58 / 1.05 | 0.60 / 0.91 | within |
| Sync Cry Lead | 8 | 2.45 / 3.75 | 4.72 / 6.52 | within |
| Vowel Whistle Lead | 1 | 1.12 / 2.03 | 2.73 / 4.39 | within |
| Triple Fifth Lead | 8 | 3.87 / 5.33 | 4.51 / 6.20 | within |
| Bell FM Ripple Lead | 8 | 2.97 / 4.62 | 5.87 / 8.56 | within |
| Stretched Glass Choir | 8 | 2.62 / 6.16 | 7.93 / 10.42 | within |
| Mid Side Halo | 12 | 3.46 / 5.37 | 7.00 / 9.43 | within |
| Parallel Dusk Pad | 6 | 3.50 / 4.92 | 5.04 / 7.14 | within |
| Brass Swell Ensemble | 8 | 2.98 / 4.41 | 4.50 / 7.20 | within |
| Tine Glass Keys | 10 | 3.02 / 4.76 | 8.28 / 10.48 | within |
| Karplus Harp | 16 | 2.75 / 4.17 | 6.05 / 8.80 | within |
| Ring Mod Gamelan | 16 | 4.04 / 5.80 | 8.61 / 11.64 | within |
| Pentatonic Step Pluck | 16 | 3.23 / 4.66 | 7.06 / 9.44 | within |
| Tempo Gate Chords | 16 | 2.51 / 3.77 | 6.68 / 8.92 | within |
| Shepard Ascent | 4 | 2.36 / 5.79 | 7.53 / 9.89 | within |
| Lorenz Garden | 6 | 3.64 / 5.26 | 5.07 / 6.97 | within |
| Breath of Machines | 16 | 3.65 / 5.09 | 8.48 / 11.08 | within |
| Bode Barber Riser | 4 | 1.82 / 2.99 | 2.90 / 4.62 | within |
| Detonation Tail | 4 | 1.95 / 2.96 | 2.81 / 4.50 | within |
| Bowed Pressure Strings | 16 | 2.32 / 3.63 | 4.86 / 7.72 | within |
| Wah Pressure Clav | 8 | 3.59 / 5.12 | 3.76 / 5.31 | within |

The tier bench plays 16 notes on the XL and 8 on the Key 61 (a chord struck every second); a preset with a polyphony below that (the heavy ones carry 4 .. 12 in `master.polyphony`) plays that many voices. That is part of the sound's definition: raise it, and the cost goes up in proportion.

What costs what (Mac, one oscillator, base: a saw through the Digital filter = 1.31 % at `low`, 2.56 % on the XL; median share of the block):

| Feature | Key 61 (added) | XL (added) |
|---|---|---|
| warp Sync | +0.40 | +7.07 |
| warp Formant | +0.77 | +8.91 |
| warp Bend | +0.83 | +9.02 |
| warp Quantize | +0.67 | +7.72 |
| warp Squeeze | +0.75 | +8.45 |
| warp Sync Var | +0.38 | +7.06 |
| warp Dist Sine Fold | +0.73 | +7.68 |
| warp Dist Tube | +0.91 | +9.41 |
| warp Spectral Comb | +0.59 | +7.89 |
| warp PD Self | +0.90 | +9.26 |
| warp FM from Osc 2 (osc2 mod) | +1.00 | +8.92 |
| warp Sync + warp2 Bend | +1.16 | +10.43 |
| unison 3 | +0.12 | +0.46 |
| unison 5 | +0.19 | +0.76 |
| 2 osc | +0.20 | +0.63 |
| 3 osc | +0.46 | +1.16 |
| 3 osc unison 3 each | +1.05 | +2.42 |
| sub | +0.08 | +0.14 |
| noise | +0.13 | +0.59 |
| morph Harmonic Stretch | +0.06 | +0.15 |
| morph Inharmonic Stretch | +0.06 | +0.12 |
| morph Shepard Tone | +0.05 | +0.20 |
| morph Formant Scale | +0.06 | +0.14 |
| morph Smear | +0.05 | +0.16 |
| morph + LFO-modulated amount | +0.22 | +9.17 |
| filter Analog | -0.10 | -0.04 |
| filter Ladder | +0.97 | +2.19 |
| filter French LP | +1.36 | +2.82 |
| filter Slope | +2.05 | +5.78 |
| filter Multi SVF | +1.45 | +2.94 |
| filter Multi Morph | +0.81 | +2.04 |
| filter Normal | +2.02 | +5.78 |
| filter New | +1.91 | +3.87 |
| filter DJ Mixer | +1.94 | +4.03 |
| filter Diffusor | +0.87 | +1.76 |
| filter Smear | +2.80 | +5.66 |
| filter Flanges | +0.64 | +1.42 |
| filter Comb | -0.07 | +0.07 |
| filter Ring Mod | +0.36 | +0.70 |
| filter Formant | +0.48 | +0.52 |
| filter Add Bass | +0.80 | +1.68 |
| filter Diode | +2.33 | +6.00 |
| filter Scream | +1.12 | +2.60 |
| filter EMS Ladder | +2.06 | +5.79 |
| filter Phaser | +3.06 | +7.71 |
| 2 filters series (Digital) | +0.46 | +0.94 |
| 2 filters parallel (Digital) | +0.44 | +0.94 |
| fx chorus | +0.40 | +0.61 |
| fx phaser | +0.38 | +0.45 |
| fx delay | +0.12 | +0.11 |
| fx reverb Hall | -0.04 | +0.79 |
| fx reverb Plate | -0.03 | +0.35 |
| fx reverb Nitrous | -0.05 | +0.69 |
| fx flanger | -0.02 | +0.19 |
| fx compressor | +0.16 | +0.15 |
| fx compressor limit | +0.60 | +0.54 |
| fx eq | +0.16 | +0.11 |
| fx distortion Tube | +0.13 | +0.17 |
| fx hyperdim | -0.07 | +0.27 |
| fx utility | +0.03 | +0.08 |
| fx bode | -0.01 | +0.32 |
| fx splitter M/S + chorus side | +0.40 | +0.64 |
| fx splitter L/H + dist low + reverb high | +0.18 | +1.33 |
| LFO routes x6 | +0.29 | +1.52 |
| custom LFO tempo 16 steps | +0.10 | +0.28 |

What this taught the design (and what had to be cut after the first bench of the batch, which exceeded the budget by 2 to 6 times): a **4x warp costs 7 .. 9 % of the XL block per oscillator and per unison voice** (so warps live on a single-voice oscillator and the width comes from an unwarped one); **any change of the frame or the morph amount of an oscillator that has a morph rebuilds its morph cycle in every voice** (+5 .. 9 % XL): one morph moves, the others are fixed and their frame is not modulated; the dear filter models (Slope, Normal, Smear, Diode, EMS, Phaser: 8 .. 10 % XL) are used once at most and never together with a warp; the effects are cheap (0.2 .. 0.8 %), so they carry the width and the depth instead.

## 5. Distance to the nearest preset

| Preset | Parameters differing from the defaults | Nearest preset of the 136 | Distance | Nearest of the new ones | Distance |
|---|---|---|---|---|---|
| Subterranean Duo | 83 | Scream Filter Bass | 79 | Wah Pressure Clav | 88 |
| Bend Formant Growl | 103 | Acid Saw Reso | 102 | Subterranean Duo | 109 |
| Triple Fold Reese | 93 | Fat Reese | 90 | Triple Fifth Lead | 99 |
| Pitch Drop 808 | 75 | 808 Boom | 72 | Ring Mod Gamelan | 81 |
| Sync Cry Lead | 96 | Bell FM Ripple Lead | 83 | Bell FM Ripple Lead | 83 |
| Vowel Whistle Lead | 123 | MPE Pressure Lead | 118 | Bell FM Ripple Lead | 124 |
| Triple Fifth Lead | 89 | Fat Reese | 85 | Bell FM Ripple Lead | 89 |
| Bell FM Ripple Lead | 87 | Golden Dawn Pad | 81 | Sync Cry Lead | 83 |
| Stretched Glass Choir | 93 | Golden Dawn Pad | 88 | Tempo Gate Chords | 94 |
| Mid Side Halo | 121 | Brass Swell Ensemble | 108 | Brass Swell Ensemble | 108 |
| Parallel Dusk Pad | 99 | Custom Shape Wobble | 93 | Shepard Ascent | 95 |
| Brass Swell Ensemble | 95 | Gentle Flute Pad | 90 | Triple Fifth Lead | 94 |
| Tine Glass Keys | 102 | Bell FM Ripple Lead | 93 | Bell FM Ripple Lead | 93 |
| Karplus Harp | 90 | Comb Reso Swell | 86 | Ring Mod Gamelan | 93 |
| Ring Mod Gamelan | 78 | Tri Soft Solo | 71 | Pitch Drop 808 | 81 |
| Pentatonic Step Pluck | 102 | Tempo Gate Chords | 85 | Tempo Gate Chords | 85 |
| Tempo Gate Chords | 85 | Dual Octave Hero | 80 | Pentatonic Step Pluck | 85 |
| Shepard Ascent | 96 | Chorused Saw Solo | 91 | Tempo Gate Chords | 92 |
| Lorenz Garden | 129 | Bell Chime Lead | 123 | Stretched Glass Choir | 126 |
| Breath of Machines | 101 | Vocaloid Lead | 95 | Shepard Ascent | 105 |
| Bode Barber Riser | 94 | Dual Octave Hero | 89 | Parallel Dusk Pad | 99 |
| Detonation Tail | 112 | Pitch Drop 808 | 94 | Pitch Drop 808 | 94 |
| Bowed Pressure Strings | 95 | Lush Analog Pad | 90 | Tempo Gate Chords | 95 |
| Wah Pressure Clav | 80 | Acid Saw Reso | 76 | Subterranean Duo | 88 |

## 6. The presets

### Subterranean Duo (Bass)

*Intention.* A bass for the XL that is clean at the bottom and angry in the middle: a pure sine sub that goes round the filter and a saw growl layer that is folded (key range 36 .. 59 with key fold) so it stays in the two octaves where a growl sounds like one, whatever you play. French LP does the growl, an EQ high pass keeps the saw out of the sub's way, the compressor tightens it.

*What to play.* Single notes from C1 to B2 (MIDI 24 .. 47), short and held. Velocity 60 .. 127: harder is brighter and angrier. Hold a note and lean on the pad: a little more cutoff. Wheel up: drive and resonance. Macros: Bite (cutoff), Growl (drive and detune), Punch (depth of the filter snap), Sub Weight, Squeeze (compressor).

*Macros.* Bite (audible: 15.9 dB, +0.2 dB level, +0.97 oct); Growl (audible: 5.7 dB, +0.1 dB level, -0.30 oct); Punch (audible: 7.4 dB, +0.0 dB level, +0.20 oct); Sub Weight (audible: 1.3 dB, +5.4 dB level, +0.00 oct); Squeeze (audible: 4.1 dB, +3.9 dB level, -0.00 oct).

*Measured.* Level -20.0 dBFS (XL), -20.0 (Key 61); 8-note velocity-127 peak -7.1 dBFS raw = 10.9 at +18 dB; mono sum -0.5 dB vs stereo, worst band -2.1 dB at 5080 Hz; DC 0.0000, click ratio 0.2, tail 0.13 s. Velocity 30 / 80 / 127: -45.8 / -33.2 / -26.3 dB, centroid +0.54 oct. Pressure ramp: 10.9 dB distance (+0.1 dB, +0.54 oct); wheel: 10.1 dB (+0.6 dB, +0.54 oct). Movement over 10 s: centroid 0.82 oct, level 8.0 dB, flux 1.3 dB per 0.5 s. CPU Key 61 2.92 / 4.19 %, XL 2.92 / 4.37 % (median / p99). Nearest preset: Scream Filter Bass (79).

*On the Key 61 (`low`).* Level -20.0 dBFS (difference +0.0 dB), peak -7.1 dBFS raw, velocity centroid +0.54 oct, mono sum -0.5 dB.

*Not settled for the ear.* The balance of the sub and the saw layer (the saw sits about 10 dB under the sub at C2). Whether the key fold is audible as a jump at the edges (an octave jump of the growl layer at C2 / B3 is its definition). French LP's resonance at 38 % is untried by ear.

### Bend Formant Growl (Bass)

*Intention.* The dubstep-style talking bass at a price the XL can pay: one oscillator with the Bend warp swept by a free LFO that the wheel deepens, an analog low pass and the Formant filter model side by side, a second LFO route moving the vowel, diode drive and an EQ clean-up. Mono, so it collapses to mono trivially.

*What to play.* C1 .. G2, staccato eighth notes with the wheel moving. Velocity moves the vowel and the drive. Macros: Mouth (the vowel), Growl Depth, Wobble Rate (free LFO, 3 Hz up to about 12), Dirt, Sub.

*Macros.* Mouth (audible: 5.2 dB, +0.9 dB level, -0.07 oct); Growl Depth (audible: 7.8 dB, +0.3 dB level, -0.03 oct); Wobble Rate (audible: 3.0 dB, -0.1 dB level, +0.02 oct); Dirt (audible: 8.5 dB, +0.8 dB level, +0.78 oct); Sub (audible: 2.2 dB, +2.4 dB level, +0.20 oct).

*Measured.* Level -20.0 dBFS (XL), -20.0 (Key 61); 8-note velocity-127 peak -6.7 dBFS raw = 11.3 at +18 dB; mono sum 0.0 dB vs stereo, worst band 0.0 dB at 32 Hz; DC 0.0002, click ratio 0.9, tail 0.14 s. Velocity 30 / 80 / 127: -41.5 / -26.7 / -23.2 dB, centroid +0.53 oct. Pressure ramp: 0.0 dB distance (+0.0 dB, +0.00 oct); wheel: 6.9 dB (+0.8 dB, +0.72 oct). Movement over 10 s: centroid 0.13 oct, level 3.0 dB, flux 10.9 dB per 0.5 s. CPU Key 61 3.20 / 4.79 %, XL 4.98 / 6.86 % (median / p99). Nearest preset: Acid Saw Reso (102).

*On the Key 61 (`low`).* Level -20.0 dBFS (difference +0.0 dB), peak -7.6 dBFS raw, velocity centroid +0.51 oct, mono sum 0.0 dB.

*Not settled for the ear.* The Formant model's vowel positions (x 30 .. 100) are chosen from the response curves, not by ear. The wobble depth at wheel 0 (about 24 % of the warp range) may be too much or too little.

### Triple Fold Reese (Bass)

*Intention.* The Reese with all three oscillators: a detuned saw pair, a square an octave down, a 24 dB low pass into the Add Bass model, a slow phaser, a mono bottom, and a sine-fold distortion whose drive is held by envelope 3 so the attack snarls and melts into a smooth body. (The fold was first a warp on oscillator 2; that cost 3 times the CPU budget, see the CPU section.)

*What to play.* Long notes on C1 .. C2, chords of fifths. Macro Fold opens the fold and the filter together (a big change), Drive is the other big one, Phase Drift moves the detune. Wheel: cutoff and fold.

*Macros.* Fold (audible: 26.6 dB, +0.5 dB level, +1.98 oct); Phase Drift (audible: 4.4 dB, -0.5 dB level, +0.42 oct); Open (audible: 9.3 dB, -0.0 dB level, +0.40 oct); Width (audible: 1.7 dB, +0.4 dB level, -0.00 oct); Drive (audible: 24.5 dB, -2.1 dB level, +3.67 oct).

*Measured.* Level -20.0 dBFS (XL), -20.0 (Key 61); 8-note velocity-127 peak -7.4 dBFS raw = 10.6 at +18 dB; mono sum -1.5 dB vs stereo, worst band -4.7 dB at 2016 Hz; DC 0.0004, click ratio 1.2, tail 0.15 s. Velocity 30 / 80 / 127: -37.3 / -21.5 / -18.7 dB, centroid +0.37 oct. Pressure ramp: 0.0 dB distance (+0.0 dB, +0.00 oct); wheel: 17.9 dB (-2.4 dB, +0.81 oct). Movement over 10 s: centroid 0.44 oct, level 3.3 dB, flux 5.1 dB per 0.5 s. CPU Key 61 3.42 / 5.27 %, XL 4.25 / 6.14 % (median / p99). Nearest preset: Fat Reese (90).

*On the Key 61 (`low`).* Level -20.0 dBFS (difference -0.0 dB), peak -7.4 dBFS raw, velocity centroid +0.37 oct, mono sum -1.5 dB.

*Not settled for the ear.* Whether the fold's DC and its upper partials are pleasant in the 2 .. 5 kHz range with Fold at 100 (the hf figure grows by 60 dB). The distortion drive follows the NEWEST note, not each voice (the effect is global): with a chord the fold of the earlier notes follows the last one.

### Pitch Drop 808 (Bass)

*Intention.* A kick-drum 808: a sine whose pitch falls from ten semitones above in 110 ms (envelope 3 on the oscillator transpose), a short pulse knock from envelope 4, a tube drive so it speaks on a small speaker, an EQ shelf and a compressor. Mono (polyphony 1).

*What to play.* Single notes C1 .. G1 (MIDI 24 .. 31), a long note for a long tail. Harder velocity: more pitch fall and a brighter knock. Macros: Drop, Tail, Knock, Grit, Boom.

*Macros.* Drop (audible: 10.6 dB, +0.0 dB level, +0.01 oct); Tail (audible: 40.9 dB, +28.0 dB level, +7.54 oct); Knock (audible: 30.8 dB, +0.0 dB level, +0.00 oct); Grit (audible: 48.9 dB, +24.6 dB level, +0.00 oct); Boom (audible: 6.8 dB, +6.8 dB level, +2.72 oct).

*Measured.* Level -20.0 dBFS (XL), -20.0 (Key 61); 8-note velocity-127 peak -1.7 dBFS raw = 16.3 at +18 dB; mono sum 0.0 dB vs stereo, worst band 0.0 dB at 635 Hz; DC 0.0001, click ratio 3.4, tail 0.00 s. Velocity 30 / 80 / 127: -33.6 / -22.8 / -18.9 dB, centroid +0.43 oct. Pressure ramp: 0.0 dB distance (+0.0 dB, +0.00 oct); wheel: 48.0 dB (+16.4 dB, -0.00 oct). Movement over 10 s: centroid 7.54 oct, level 250.8 dB, flux 3.4 dB per 0.5 s. CPU Key 61 0.58 / 1.05 %, XL 0.60 / 0.91 % (median / p99). Nearest preset: 808 Boom (72).

*On the Key 61 (`low`).* Level -20.0 dBFS (difference +0.0 dB), peak -1.7 dBFS raw, velocity centroid +0.43 oct, mono sum 0.0 dB.

*Not settled for the ear.* Whether ten semitones of fall reads as a 'zip' or a kick; the Drop macro takes it to about 14. The click at the start is intended (click ratio 3.4).

### Sync Cry Lead (Lead)

*Intention.* The singing hard-sync lead: one sync oscillator (a 4x warp: it costs what it costs, so one voice of it) whose sync sweep follows velocity, pad pressure and an attack envelope; a three-voice detuned saw a fifth below carries the width instead of unison on the sync oscillator; the wheel brings in a vibrato that fades in.

*What to play.* C3 .. C5, legato phrases, velocity 90 .. 127. Press the pad harder after the note starts: the sync opens up and the cutoff follows. Wheel for vibrato. Macros: Bright, Cry (sync amount), Thick, Space, Fifth.

*Macros.* Bright (audible: 1.6 dB, +0.5 dB level, +0.56 oct); Cry (audible: 5.1 dB, -0.3 dB level, +0.75 oct); Thick (audible: 6.9 dB, +2.2 dB level, -0.52 oct); Space (audible: 5.1 dB, -4.9 dB level, +0.01 oct); Fifth (audible: 3.7 dB, +3.3 dB level, -0.48 oct).

*Measured.* Level -20.0 dBFS (XL), -19.8 (Key 61); 8-note velocity-127 peak -1.8 dBFS raw = 16.2 at +18 dB; mono sum -0.2 dB vs stereo, worst band -1.1 dB at 318 Hz; DC 0.0000, click ratio 0.1, tail 3.50 s. Velocity 30 / 80 / 127: -53.0 / -35.2 / -26.7 dB, centroid +0.91 oct. Pressure ramp: 5.5 dB distance (+0.2 dB, +1.14 oct); wheel: 3.8 dB (-0.4 dB, +0.45 oct). Movement over 10 s: centroid 0.50 oct, level 1.7 dB, flux 2.1 dB per 0.5 s. CPU Key 61 2.45 / 3.75 %, XL 4.72 / 6.52 % (median / p99). Nearest preset: Bell FM Ripple Lead (83).

*On the Key 61 (`low`).* Level -19.8 dBFS (difference +0.2 dB), peak -2.6 dBFS raw, velocity centroid +0.94 oct, mono sum -0.2 dB.

*Not settled for the ear.* The sync range (amount 18 % + envelope + velocity + pressure, up to 100 %) may be too wild at full pressure. The ping-pong delay (eighth notes at the host tempo) is on at 16 %.

### Vowel Whistle Lead (Lead)

*Intention.* A mono legato lead with a human voice: Formant warp and the Formant Scale morph on the oscillator, the Formant filter in series with a Multi SVF peak (style 9: a +9 dB bell). The wheel sweeps the vowel, pressure opens the mouth, a vibrato fades in. Mono, so it is cheap on both devices.

*What to play.* C4 .. C6, legato lines with a little glide. Wheel slowly from 0 to 127 while holding a note: the vowel travels. Macros: Vowel, Reson, Shape, Air (the position of the peak), Vibrato Rate, Space.

*Macros.* Vowel (audible: 4.1 dB, +0.9 dB level, -0.28 oct); Reson (audible: 5.6 dB, +3.5 dB level, +0.45 oct); Shape (audible: 4.8 dB, -0.9 dB level, -0.32 oct); Air (audible: 8.1 dB, -2.8 dB level, -1.41 oct); Vibrato Rate (audible: 1.0 dB, +0.4 dB level, +0.06 oct); Space (weak (see note): 3.0 dB, -2.7 dB level, +0.11 oct).

*Measured.* Level -20.0 dBFS (XL), -20.2 (Key 61); 8-note velocity-127 peak -9.6 dBFS raw = 8.4 at +18 dB; mono sum -0.6 dB vs stereo, worst band -1.0 dB at 800 Hz; DC 0.0014, click ratio 0.0, tail 2.00 s. Velocity 30 / 80 / 127: -45.4 / -28.4 / -21.4 dB, centroid +0.54 oct. Pressure ramp: 2.2 dB distance (-0.4 dB, +0.36 oct); wheel: 5.3 dB (+0.9 dB, +0.41 oct). Movement over 10 s: centroid 1.02 oct, level 4.4 dB, flux 6.4 dB per 0.5 s. CPU Key 61 1.12 / 2.03 %, XL 2.73 / 4.39 % (median / p99). Nearest preset: MPE Pressure Lead (118).

*On the Key 61 (`low`).* Level -20.2 dBFS (difference -0.1 dB), peak -9.8 dBFS raw, velocity centroid +0.66 oct, mono sum -0.6 dB.

*Not settled for the ear.* The vowels are positions on the Formant model's x / y plane (35 .. 100 / 45 ..), not tuned to named vowels by ear.

### Triple Fifth Lead (Lead)

*Intention.* A lead that sounds like a stacked band: three oscillators (a power-chord stacked saw, a pulse an octave up, a triangle a twelfth up), one Multi SVF low pass, Hyper/Dimension, a utility width with a mono bottom. At `low` the stack has 3 voices and Hyper/Dimension is bypassed, which is why the Key 61 sounds about 1.5 dB louder and less wide.

*What to play.* C3 .. C5, single notes, not chords (the stack is the chord). Velocity opens the pulse and the cutoff. Press for the triangle. Macros: Bright, Stack (levels of the upper two), Spread, Space, Edge.

*Macros.* Bright (audible: 2.1 dB, -0.6 dB level, +0.09 oct); Stack (audible: 3.3 dB, +3.7 dB level, +0.02 oct); Spread (audible: 3.9 dB, +0.1 dB level, -0.03 oct); Space (audible: 5.3 dB, -5.4 dB level, +0.02 oct); Edge (audible: 3.6 dB, +1.1 dB level, +0.79 oct).

*Measured.* Level -20.0 dBFS (XL), -18.1 (Key 61); 8-note velocity-127 peak -4.5 dBFS raw = 13.5 at +18 dB; mono sum -1.9 dB vs stereo, worst band -2.8 dB at 800 Hz; DC 0.0008, click ratio 0.1, tail 1.55 s. Velocity 30 / 80 / 127: -51.7 / -35.0 / -26.9 dB, centroid +0.49 oct. Pressure ramp: 2.7 dB distance (+0.9 dB, -0.08 oct); wheel: 5.3 dB (-0.5 dB, +0.18 oct). Movement over 10 s: centroid 0.49 oct, level 3.4 dB, flux 5.5 dB per 0.5 s. CPU Key 61 3.87 / 5.33 %, XL 4.51 / 6.20 % (median / p99). Nearest preset: Fat Reese (85).

*On the Key 61 (`low`).* Level -18.1 dBFS (difference +1.8 dB), peak -1.6 dBFS raw, velocity centroid +0.53 oct, mono sum -1.4 dB.

*Not settled for the ear.* The power-chord stack on the Key 61 (3 unison voices) has not been compared to the XL by ear.

### Bell FM Ripple Lead (Lead)

*Intention.* A sine carrier phase-modulated from a second oscillator (a twelfth up, level 55 with its destination set to None so only the modulation is heard); the index rides envelope 3, velocity and pressure, so a soft note is glassy and a hard one rings like struck metal; Bell Partials add a shimmer.

*What to play.* C4 .. C6, slow melodies, velocity from 40 to 127 (the difference is the point). Press while a note rings. Macros: Index, Ring (sustain), Shimmer, Space, Ratio (moves the modulator up to 14 semitones: the colour changes, so use it on held notes).

*Macros.* Index (audible: 2.4 dB, +0.1 dB level, +0.39 oct); Ring (audible: 9.9 dB, +9.9 dB level, -0.00 oct); Shimmer (audible: 8.5 dB, +1.3 dB level, +0.86 oct); Space (audible: 4.2 dB, -5.6 dB level, +0.35 oct); Ratio (audible: 3.1 dB, -0.2 dB level, +0.36 oct).

*Measured.* Level -20.0 dBFS (XL), -19.4 (Key 61); 8-note velocity-127 peak -1.3 dBFS raw = 16.7 at +18 dB; mono sum -0.8 dB vs stereo, worst band -3.3 dB at 1008 Hz; DC 0.0001, click ratio 1.1, tail 2.67 s. Velocity 30 / 80 / 127: -52.4 / -35.1 / -26.7 dB, centroid +0.52 oct. Pressure ramp: 7.7 dB distance (+0.8 dB, +0.75 oct); wheel: 3.0 dB (+3.9 dB, +0.13 oct). Movement over 10 s: centroid 0.53 oct, level 3.6 dB, flux 5.5 dB per 0.5 s. CPU Key 61 2.97 / 4.62 %, XL 5.87 / 8.56 % (median / p99). Nearest preset: Golden Dawn Pad (81).

*On the Key 61 (`low`).* Level -19.4 dBFS (difference +0.7 dB), peak -2.4 dBFS raw, velocity centroid +0.48 oct, mono sum -0.6 dB.

*Not settled for the ear.* The ratio of 19 semitones is an inharmonic bell: it is meant to be, but the Ratio macro makes it harmonic and then inharmonic again on the way.

### Stretched Glass Choir (Pad)

*Intention.* A pad whose partials are stretched: oscillator 1 (Sine to Saw, Harmonic Stretch morph, its amount wandering on a Perlin random LFO) and a three-voice Harmonic Series layer above it, chorus, a plate reverb, an EQ that thins the lows. One modulated morph only: a morph that moves costs 5 to 9 % of the XL block per oscillator (see the engine notes).

*What to play.* Chords C3 .. C5, hold 4 s and more; pressure opens the filter and the stretch. Macros: Bright, Stretch, Shimmer, Space, Slow (attack and release: the longest changes).

*Macros.* Bright (audible: 4.3 dB, +0.1 dB level, +0.33 oct); Stretch (audible: 5.4 dB, -0.3 dB level, -0.12 oct); Shimmer (audible: 4.7 dB, +1.2 dB level, -0.11 oct); Space (audible: 4.2 dB, -5.3 dB level, +0.15 oct); Slow (audible: 36.7 dB, -36.7 dB level, +0.00 oct).

*Measured.* Level -20.0 dBFS (XL), -19.8 (Key 61); 8-note velocity-127 peak -3.8 dBFS raw = 14.2 at +18 dB; mono sum -0.7 dB vs stereo, worst band -1.5 dB at 3200 Hz; DC 0.0000, click ratio 0.0, tail 3.78 s. Velocity 30 / 80 / 127: -50.1 / -33.1 / -25.0 dB, centroid +0.17 oct. Pressure ramp: 4.0 dB distance (-0.1 dB, +0.28 oct); wheel: 2.9 dB (+0.4 dB, +0.39 oct). Movement over 10 s: centroid 0.64 oct, level 8.5 dB, flux 5.1 dB per 0.5 s. CPU Key 61 2.62 / 6.16 %, XL 7.93 / 10.42 % (median / p99). Nearest preset: Golden Dawn Pad (88).

*On the Key 61 (`low`).* Level -19.8 dBFS (difference +0.2 dB), peak -3.3 dBFS raw, velocity centroid +0.19 oct, mono sum -0.7 dB.

*Not settled for the ear.* Harmonic Stretch at 60 .. 100 % moves the character from choir to bell-like; the wandering range (30 % +- 40 %) may be too wide.

### Mid Side Halo (Pad)

*Intention.* Three oscillators and a sub as one warm pad, then split into mid and side: the centre is compressed and EQ'd (it stays dry and solid), the sides get a chorus and a phaser, so the halo grows wide while the middle holds its place in mono. (The first idea, reverb on the side only, did nothing audible because the side content of a pad is small.)

*What to play.* Wide chords C2 .. C4, slow. Compare mono and stereo: the mono sum loses only about 1 dB. Macros: Brightness, Halo (width and the side effects), Colour, Space, Weight.

*Macros.* Brightness (audible: 7.2 dB, -0.0 dB level, +0.37 oct); Halo (audible: 2.7 dB, +1.5 dB level, +0.19 oct); Colour (audible: 5.9 dB, +1.0 dB level, +0.77 oct); Space (audible: 3.8 dB, -4.2 dB level, +0.06 oct); Weight (audible: 2.0 dB, +5.4 dB level, -0.77 oct).

*Measured.* Level -20.0 dBFS (XL), -19.9 (Key 61); 8-note velocity-127 peak -2.6 dBFS raw = 15.4 at +18 dB; mono sum -1.1 dB vs stereo, worst band -2.4 dB at 5080 Hz; DC 0.0003, click ratio 0.0, tail 3.49 s. Velocity 30 / 80 / 127: -44.3 / -28.2 / -23.0 dB, centroid +0.32 oct. Pressure ramp: 4.2 dB distance (+0.3 dB, +0.59 oct); wheel: 1.7 dB (+0.7 dB, +0.09 oct). Movement over 10 s: centroid 1.13 oct, level 6.0 dB, flux 4.8 dB per 0.5 s. CPU Key 61 3.46 / 5.37 %, XL 7.00 / 9.43 % (median / p99). Nearest preset: Brass Swell Ensemble (108).

*On the Key 61 (`low`).* Level -19.9 dBFS (difference +0.1 dB), peak -3.0 dBFS raw, velocity centroid +0.35 oct, mono sum -1.2 dB.

*Not settled for the ear.* Whether the side phaser is too obvious at Halo 100 on headphones.

### Parallel Dusk Pad (Pad)

*Intention.* A slow, dark pad whose image breathes: the two filters run in parallel on opposite halves of the stereo field (an analog 24 dB low pass on the left, a Flanges comb on the right), an LFO opposes their cutoffs and a four second envelope opens the comb. Oscillator 2 keeps its phase per voice (phase memory).

*What to play.* Slow chords C2 .. C4, best on headphones. Macros: Left Open, Comb Colour, Breath Rate, Space, Upper Layer.

*Macros.* Left Open (audible: 3.9 dB, +0.4 dB level, +0.11 oct); Comb Colour (audible: 2.7 dB, +0.1 dB level, -0.02 oct); Breath Rate (audible: 2.0 dB, -0.1 dB level, +0.03 oct); Space (audible: 4.0 dB, -5.1 dB level, +0.14 oct); Upper Layer (audible: 3.0 dB, +3.8 dB level, +0.02 oct).

*Measured.* Level -20.0 dBFS (XL), -19.4 (Key 61); 8-note velocity-127 peak -8.4 dBFS raw = 9.6 at +18 dB; mono sum -2.1 dB vs stereo, worst band -5.6 dB at 4032 Hz; DC 0.0000, click ratio 0.0, tail 3.45 s. Velocity 30 / 80 / 127: -55.2 / -41.8 / -25.0 dB, centroid -0.05 oct. Pressure ramp: 2.9 dB distance (+1.0 dB, +0.03 oct); wheel: 2.8 dB (+2.0 dB, +0.01 oct). Movement over 10 s: centroid 0.38 oct, level 14.5 dB, flux 6.4 dB per 0.5 s. CPU Key 61 3.50 / 4.92 %, XL 5.04 / 7.14 % (median / p99). Nearest preset: Custom Shape Wobble (93).

*On the Key 61 (`low`).* Level -19.4 dBFS (difference +0.5 dB), peak -6.5 dBFS raw, velocity centroid -0.02 oct, mono sum -1.9 dB.

*Not settled for the ear.* The mono sum is the weakest of the batch (-2.8 dB, worst third octave -6 dB at 1.6 kHz): it is a stereo pad by construction.

### Brass Swell Ensemble (Pad)

*Intention.* A 1980s brass section pad: three detuned saws and a sub, breath noise on the front of the note, a filter that swells with the note and with harder playing, an EQ high pass that keeps the low mids clear.

*What to play.* Chords C2 .. C4. Hard playing makes the swell faster and brighter. Macros: Bright, Swell Time (attack of the swell and of the level), Breath, Hall, Section (detune).

*Macros.* Bright (audible: 5.1 dB, +0.2 dB level, +0.72 oct); Swell Time (audible: 45.3 dB, -38.5 dB level, -1.39 oct); Breath (audible: 3.2 dB, +0.4 dB level, +1.12 oct); Hall (weak (see note): 2.9 dB, -2.9 dB level, +0.28 oct); Section (audible: 1.5 dB, +1.6 dB level, -0.36 oct).

*Measured.* Level -20.0 dBFS (XL), -19.7 (Key 61); 8-note velocity-127 peak -6.5 dBFS raw = 11.5 at +18 dB; mono sum -1.7 dB vs stereo, worst band -4.5 dB at 100 Hz; DC 0.0000, click ratio 0.0, tail 1.95 s. Velocity 30 / 80 / 127: -49.5 / -32.4 / -24.3 dB, centroid +0.51 oct. Pressure ramp: 5.4 dB distance (+0.2 dB, +0.65 oct); wheel: 4.0 dB (-0.7 dB, +0.45 oct). Movement over 10 s: centroid 1.14 oct, level 8.1 dB, flux 5.1 dB per 0.5 s. CPU Key 61 2.98 / 4.41 %, XL 4.50 / 7.20 % (median / p99). Nearest preset: Gentle Flute Pad (90).

*On the Key 61 (`low`).* Level -19.7 dBFS (difference +0.2 dB), peak -6.1 dBFS raw, velocity centroid +0.62 oct, mono sum -1.5 dB.

*Not settled for the ear.* Whether the swell of 650 ms feels like brass or like strings.

### Tine Glass Keys (Keys)

*Intention.* An electric piano from two sines (the second phase-modulates the first with an index that dies away under envelope 3) plus a bell-partial tine that pings on hard hits; the wheel brings in an auto-pan tremolo, a phaser and a flanger finish it (the flanger is bypassed on the Key 61).

*What to play.* C2 .. C6, chords and single notes, velocity 40 .. 127: soft is round, hard barks and pings. Wheel: tremolo. Macros: Bell, Bark, Sustain, Phase, Mellow.

*Macros.* Bell (audible: 26.4 dB, +2.0 dB level, +2.47 oct); Bark (audible: 5.4 dB, -0.5 dB level, +0.15 oct); Sustain (audible: 18.6 dB, +18.1 dB level, +0.01 oct); Phase (audible: 11.5 dB, +3.8 dB level, +0.10 oct); Mellow (audible: 17.4 dB, -1.5 dB level, -0.05 oct).

*Measured.* Level -20.0 dBFS (XL), -20.6 (Key 61); 8-note velocity-127 peak -1.4 dBFS raw = 16.6 at +18 dB; mono sum -0.2 dB vs stereo, worst band -2.5 dB at 100 Hz; DC 0.0012, click ratio 1.4, tail 0.71 s. Velocity 30 / 80 / 127: -51.7 / -34.5 / -26.3 dB, centroid +0.07 oct. Pressure ramp: 0.2 dB distance (-0.4 dB, +0.00 oct); wheel: 1.2 dB (+0.1 dB, +0.00 oct). Movement over 10 s: centroid 8.08 oct, level 254.6 dB, flux 3.0 dB per 0.5 s. CPU Key 61 3.02 / 4.76 %, XL 8.28 / 10.48 % (median / p99). Nearest preset: Bell FM Ripple Lead (93).

*On the Key 61 (`low`).* Level -20.6 dBFS (difference -0.5 dB), peak -1.9 dBFS raw, velocity centroid +0.10 oct, mono sum -0.0 dB.

*Not settled for the ear.* The FM bark at velocity 127 (index 12 + 38 + 40 %) may be too hard; the tine at +24 semitones is a high 'ping' that may need to be quieter.

### Karplus Harp (Pluck)

*Intention.* A plucked string made of a filter: envelope 4 puts a 16 ms noise burst into the Comb filter model, which is keytracked and calibrated to the note (the cutoff is 264.6 Hz, because the comb sounds 1.1 % flat at 261.6) and rings for about a second; a second filter damps the highs as it fades; a limiter at the end of the chain catches the pick.

*What to play.* Arpeggios C3 .. C6, velocity 50 .. 127. The wheel mutes the strings (palm mute). Macros: Brightness, Ring Tone (the comb offset adds the upper partials), Pick Softness (long bursts sound bowed), Space, Body.

*Macros.* Brightness (audible: 4.2 dB, +0.7 dB level, +0.29 oct); Ring Tone (audible: 7.2 dB, +9.6 dB level, +0.47 oct); Pick Softness (audible: 11.0 dB, +11.3 dB level, -0.39 oct); Space (audible: 9.5 dB, +9.1 dB level, +0.12 oct); Body (audible: 10.6 dB, -3.2 dB level, -0.42 oct).

*Measured.* Level -20.0 dBFS (XL), -20.0 (Key 61); 8-note velocity-127 peak -12.0 dBFS raw = 6.0 at +18 dB; mono sum -1.8 dB vs stereo, worst band -8.9 dB at 32 Hz; DC 0.0005, click ratio 1.0, tail 0.84 s. Velocity 30 / 80 / 127: -52.2 / -33.8 / -25.9 dB, centroid +0.63 oct. Pressure ramp: 0.0 dB distance (+0.0 dB, +0.00 oct); wheel: 3.3 dB (-4.6 dB, +0.50 oct). Movement over 10 s: centroid 10.85 oct, level 237.9 dB, flux 2.2 dB per 0.5 s. CPU Key 61 2.75 / 4.17 %, XL 6.05 / 8.80 % (median / p99). Nearest preset: Comb Reso Swell (86).

*On the Key 61 (`low`).* Level -20.0 dBFS (difference +0.0 dB), peak -12.0 dBFS raw, velocity centroid +0.00 oct, mono sum -0.1 dB.

*Not settled for the ear.* Whether the comb's ring is string-like or flute-like; the burst is white noise. Notes above C6 ring for very short times.

### Ring Mod Gamelan (Pluck)

*Intention.* Struck bars whose overtones are not harmonic: a triangle through the Ring Mod filter model with a carrier at 2.76 times the note (a bar overtone), a bell-partial ping, a Multi SVF low pass that closes as the sound dies, a dotted-eighth ping-pong delay.

*What to play.* C3 .. C6 pentatonic patterns. Velocity makes it more metallic. Macros: Metal (the ring-mod mix), Inharmonic (the carrier), Ring Time, Space, Shimmer.

*Macros.* Metal (audible: 3.2 dB, +1.1 dB level, +0.04 oct); Inharmonic (audible: 21.7 dB, -1.7 dB level, +1.96 oct); Ring Time (audible: 15.8 dB, +17.4 dB level, +0.00 oct); Space (audible: 7.4 dB, +7.4 dB level, -0.05 oct); Shimmer (audible: 9.0 dB, +1.3 dB level, +0.31 oct).

*Measured.* Level -20.0 dBFS (XL), -20.1 (Key 61); 8-note velocity-127 peak -3.6 dBFS raw = 14.4 at +18 dB; mono sum -0.7 dB vs stereo, worst band -2.1 dB at 200 Hz; DC 0.0000, click ratio 0.6, tail 3.63 s. Velocity 30 / 80 / 127: -51.6 / -34.1 / -25.5 dB, centroid +0.18 oct. Pressure ramp: 0.0 dB distance (+0.0 dB, +0.00 oct); wheel: 2.7 dB (+0.9 dB, +0.07 oct). Movement over 10 s: centroid 0.52 oct, level 102.3 dB, flux 4.6 dB per 0.5 s. CPU Key 61 4.04 / 5.80 %, XL 8.61 / 11.64 % (median / p99). Nearest preset: Tri Soft Solo (71).

*On the Key 61 (`low`).* Level -20.1 dBFS (difference -0.0 dB), peak -3.5 dBFS raw, velocity centroid +0.32 oct, mono sum -0.2 dB.

*Not settled for the ear.* The 2.76 ratio sounds like a marimba bar in theory; the Inharmonic macro moves it (a large change).

### Pentatonic Step Pluck (Pluck)

*Intention.* Hold one key and it plays itself: a drawn 16-step LFO (grid 16 x 24) walks the oscillators through the minor pentatonic over one bar and every step is plucked by a second tempo-synced LFO (Exp Down at sixteenths); transpose snap keeps every step in the scale. A limiter at the end catches the plucks.

*What to play.* One held key at a time, C3 .. C4, at the host tempo (120 BPM when the host does not send one). Wheel: the notes hold between the plucks. Macros: Bright, Hold, Squelch, Echo, Fat. The pattern restarts with every key press.

*Macros.* Bright (audible: 1.4 dB, +0.3 dB level, +0.45 oct); Hold (audible: 4.3 dB, +4.6 dB level, -0.22 oct); Squelch (audible: 4.4 dB, -1.7 dB level, +0.81 oct); Echo (audible: 4.1 dB, -5.0 dB level, -0.19 oct); Fat (weak (see note): 2.3 dB, -0.0 dB level, -0.19 oct).

*Measured.* Level -20.0 dBFS (XL), -20.2 (Key 61); 8-note velocity-127 peak -7.8 dBFS raw = 10.2 at +18 dB; mono sum -1.5 dB vs stereo, worst band -6.5 dB at 200 Hz; DC 0.0004, click ratio 0.7, tail 4.00 s+. Velocity 30 / 80 / 127: -48.8 / -30.3 / -22.3 dB, centroid +0.51 oct. Pressure ramp: 0.0 dB distance (+0.0 dB, +0.00 oct); wheel: 3.0 dB (+3.4 dB, -0.29 oct). Movement over 10 s: centroid 0.88 oct, level 18.1 dB, flux 7.8 dB per 0.5 s. CPU Key 61 3.23 / 4.66 %, XL 7.06 / 9.44 % (median / p99). Nearest preset: Tempo Gate Chords (85).

*On the Key 61 (`low`).* Level -20.2 dBFS (difference -0.2 dB), peak -7.8 dBFS raw, velocity centroid +0.51 oct, mono sum -1.4 dB.

*Not settled for the ear.* The pattern (0 7 12 7 3 10 15 12 0 3 7 10 12 15 19 12 semitones) is mine; the owner may want another. A chord plays the pattern in every voice, which is a mush: play single notes.

### Tempo Gate Chords (Keys)

*Intention.* Play a chord and it is chopped by a drawn 16-step gate (values 1, 0.5, 0) that runs with the tempo over one bar, with a quarter-note filter pump (Exp Up LFO on the cutoff) under it; the Smooth macro takes the gate away and leaves a pad.

*What to play.* Chords C3 .. C5, held. Macros: Smooth, Pump, Bright, Echo, Fat. Wheel: resonance.

*Macros.* Smooth (audible: 7.9 dB, +9.0 dB level, -0.06 oct); Pump (audible: 2.8 dB, +0.3 dB level, +0.12 oct); Bright (audible: 4.8 dB, +0.3 dB level, +0.83 oct); Echo (audible: 3.7 dB, -3.8 dB level, +0.13 oct); Fat (audible: 1.9 dB, +1.2 dB level, -0.16 oct).

*Measured.* Level -20.0 dBFS (XL), -19.9 (Key 61); 8-note velocity-127 peak -2.8 dBFS raw = 15.2 at +18 dB; mono sum -1.1 dB vs stereo, worst band -2.5 dB at 100 Hz; DC 0.0001, click ratio 1.2, tail 3.01 s. Velocity 30 / 80 / 127: -50.8 / -33.6 / -25.5 dB, centroid +0.38 oct. Pressure ramp: 1.1 dB distance (+0.1 dB, +0.34 oct); wheel: 1.9 dB (-0.6 dB, +0.62 oct). Movement over 10 s: centroid 0.97 oct, level 16.4 dB, flux 7.8 dB per 0.5 s. CPU Key 61 2.51 / 3.77 %, XL 6.68 / 8.92 % (median / p99). Nearest preset: Dual Octave Hero (80).

*On the Key 61 (`low`).* Level -19.9 dBFS (difference +0.1 dB), peak -2.6 dBFS raw, velocity centroid +0.28 oct, mono sum -1.0 dB.

*Not settled for the ear.* The pump is a cutoff pump, not a level pump (a level pump cannot be multiplied with the gate in this engine's matrix); it may be less obvious than a sidechain.

### Shepard Ascent (Pad)

*Intention.* A drone that climbs: two layers an octave apart ride the Shepard Tone morph up an octave in turn under a slow saw LFO (11.8 s), the upper fading out as the lower arrives where it began; a Multi SVF high pass creeps up, a Lorenz random LFO colours it. Polyphony 4: a drone is not played with fists.

*What to play.* One or two notes C2 .. C3 held for 30 s. Press to speed the climb. Macros: Bright, Climb Speed, Thin, Space, Drift.

*Macros.* Bright (audible: 2.2 dB, +0.1 dB level, +0.25 oct); Climb Speed (audible: 16.5 dB, -3.3 dB level, +0.07 oct); Thin (audible: 19.9 dB, -8.0 dB level, +2.16 oct); Space (audible: 6.1 dB, -6.1 dB level, +0.02 oct); Drift (audible: 6.2 dB, +0.0 dB level, +0.08 oct).

*Measured.* Level -20.0 dBFS (XL), -18.6 (Key 61); 8-note velocity-127 peak -5.8 dBFS raw = 12.2 at +18 dB; mono sum -1.4 dB vs stereo, worst band -2.2 dB at 800 Hz; DC 0.0001, click ratio 0.0, tail 4.00 s+. Velocity 30 / 80 / 127: -67.5 / -50.5 / -42.4 dB, centroid +0.17 oct. Pressure ramp: 6.5 dB distance (-0.6 dB, +0.22 oct); wheel: 11.8 dB (-8.4 dB, +1.59 oct). Movement over 10 s: centroid 1.08 oct, level 19.5 dB, flux 6.3 dB per 0.5 s. CPU Key 61 2.36 / 5.79 %, XL 7.53 / 9.89 % (median / p99). Nearest preset: Chorused Saw Solo (91).

*On the Key 61 (`low`).* Level -18.6 dBFS (difference +1.4 dB), peak -4.0 dBFS raw, velocity centroid +0.18 oct, mono sum -0.4 dB.

*Not settled for the ear.* THE ASCENT IS NOT PROVEN. Measured: the harmonic emphasis cycles every 11.8 s and the level moves about 3 dB over the cycle; a seamless endless rise is the aim and must be heard.

### Lorenz Garden (Pad)

*Intention.* A slow bed of bell partials and harmonic series where four random LFOs (Lorenz, Rossler, Perlin, sine interpolate) wander over the wavetable frames, the pan and the filters, so no two moments are alike; a Smear filter into an analog low pass, chorus, a dotted ping-pong delay (nothing in it needs the reverb the Key 61 lacks).

*What to play.* Chords C3 .. C5 held for 10 s and more; Sparkle brings the bell partials up an octave. Macros: Bright, Wander Speed, Sparkle, Echo, Depth.

*Macros.* Bright (weak (see note): 0.5 dB, +0.1 dB level, +0.12 oct); Wander Speed (audible: 5.0 dB, -0.2 dB level, +0.02 oct); Sparkle (audible: 6.2 dB, +1.1 dB level, +1.02 oct); Echo (audible: 3.7 dB, -3.9 dB level, +0.43 oct); Depth (audible: 1.1 dB, +4.8 dB level, -1.41 oct).

*Measured.* Level -20.0 dBFS (XL), -19.8 (Key 61); 8-note velocity-127 peak -4.2 dBFS raw = 13.8 at +18 dB; mono sum -1.3 dB vs stereo, worst band -3.0 dB at 504 Hz; DC 0.0001, click ratio 0.0, tail 4.00 s+. Velocity 30 / 80 / 127: -64.7 / -47.2 / -38.5 dB, centroid +0.77 oct. Pressure ramp: 2.7 dB distance (+1.4 dB, +0.58 oct); wheel: 2.0 dB (+1.0 dB, +0.45 oct). Movement over 10 s: centroid 1.14 oct, level 23.2 dB, flux 6.6 dB per 0.5 s. CPU Key 61 3.64 / 5.26 %, XL 5.07 / 6.97 % (median / p99). Nearest preset: Bell Chime Lead (123).

*On the Key 61 (`low`).* Level -19.8 dBFS (difference +0.2 dB), peak -4.2 dBFS raw, velocity centroid +0.77 oct, mono sum -1.3 dB.

*Not settled for the ear.* The Lorenz LFO's slow rate of 0.18 Hz may be too slow to notice inside a short phrase.

### Breath of Machines (FX)

*Intention.* A machine room that breathes: noise through the Formant filter swept by a Lorenz LFO, then a keytracked Comb filter that gives the hum a pitch, a sample-and-hold stepping the colour, a slow breathing LFO on the noise level.

*What to play.* One low note C1 .. C2 held for 10 s, or a fifth. Macros: Vowel, Breath Rate, Hum, Room, Stutter.

*Macros.* Vowel (audible: 3.4 dB, +1.0 dB level, +0.37 oct); Breath Rate (weak (see note): 1.7 dB, +0.7 dB level, +0.12 oct); Hum (audible: 4.5 dB, +3.2 dB level, -0.28 oct); Room (audible: 6.1 dB, -6.8 dB level, +0.08 oct); Stutter (audible: 8.0 dB, +3.5 dB level, -0.12 oct).

*Measured.* Level -20.0 dBFS (XL), -19.6 (Key 61); 8-note velocity-127 peak -3.4 dBFS raw = 14.6 at +18 dB; mono sum -1.3 dB vs stereo, worst band -9.7 dB at 252 Hz; DC 0.0000, click ratio 0.0, tail 4.00 s+. Velocity 30 / 80 / 127: -69.6 / -45.1 / -38.9 dB, centroid -0.48 oct. Pressure ramp: 2.8 dB distance (+1.1 dB, -0.05 oct); wheel: 2.8 dB (+1.1 dB, +0.30 oct). Movement over 10 s: centroid 0.99 oct, level 24.8 dB, flux 6.9 dB per 0.5 s. CPU Key 61 3.65 / 5.09 %, XL 8.48 / 11.08 % (median / p99). Nearest preset: Vocaloid Lead (95).

*On the Key 61 (`low`).* Level -19.6 dBFS (difference +0.4 dB), peak -3.5 dBFS raw, velocity centroid -0.46 oct, mono sum -0.1 dB.

*Not settled for the ear.* Whether it is a pleasant atmosphere or an unpleasant one: noise through a resonant comb.

### Bode Barber Riser (FX)

*Intention.* A five second riser: a saw that climbs two octaves, noise through a rising Multi SVF high pass, and the Bode frequency shifter fed back on itself; it holds at the top until the key is released. Polyphony 4. On the Key 61 the Bode and the flanger are bypassed, the riser still rises (it is 4 dB louder there).

*What to play.* One note C2 .. C4 held 6 s, then release for the drop. Macros: Height, Spin, Noise, Space, Length. Pressure: the shifter feeds back harder.

*Macros.* Height (audible: 1.6 dB, +1.3 dB level, -0.06 oct); Spin (audible: 6.8 dB, +1.6 dB level, +0.07 oct); Noise (audible: 7.5 dB, +4.4 dB level, +0.07 oct); Space (audible: 3.7 dB, -3.7 dB level, -0.00 oct); Length (audible: 19.8 dB, -20.1 dB level, -0.01 oct).

*Measured.* Level -20.0 dBFS (XL), -16.0 (Key 61); 8-note velocity-127 peak -6.6 dBFS raw = 11.4 at +18 dB; mono sum -0.2 dB vs stereo, worst band -2.1 dB at 400 Hz; DC 0.0000, click ratio 0.0, tail 1.91 s. Velocity 30 / 80 / 127: -66.7 / -50.2 / -42.5 dB, centroid +0.62 oct. Pressure ramp: 5.2 dB distance (+3.2 dB, +0.01 oct); wheel: 1.4 dB (+1.8 dB, -0.00 oct). Movement over 10 s: centroid 1.18 oct, level 24.1 dB, flux 6.8 dB per 0.5 s. CPU Key 61 1.82 / 2.99 %, XL 2.90 / 4.62 % (median / p99). Nearest preset: Dual Octave Hero (89).

*On the Key 61 (`low`).* Level -16.0 dBFS (difference +4.0 dB), peak -2.8 dBFS raw, velocity centroid +0.64 oct, mono sum -0.0 dB.

*Not settled for the ear.* Whether the Bode feedback reads as a barber pole on the XL; on the Key 61 it does not exist.

### Detonation Tail (FX)

*Intention.* A cinematic impact: a noise burst, a sine boom whose pitch collapses, a split at 300 Hz so the low band is saturated while the high band goes into a delay and a big reverb, an EQ and a limiter at the end. Polyphony 4. On the Key 61 the reverb is bypassed (the tail is the delay only).

*What to play.* One strike C1 .. C2, let it ring 6 s. Macros: Impact, Boom, Tail, Crush, Air.

*Macros.* Impact (audible: 3.4 dB, -1.4 dB level, +1.46 oct); Boom (audible: 1.8 dB, +1.8 dB level, -0.62 oct); Tail (audible: 15.9 dB, +17.9 dB level, -1.06 oct); Crush (audible: 13.9 dB, +21.3 dB level, -2.58 oct); Air (audible: 3.7 dB, -1.1 dB level, -1.45 oct).

*Measured.* Level -20.0 dBFS (XL), -19.1 (Key 61); 8-note velocity-127 peak -3.5 dBFS raw = 14.5 at +18 dB; mono sum -0.4 dB vs stereo, worst band -1.1 dB at 635 Hz; DC 0.0004, click ratio 0.4, tail 4.00 s+. Velocity 30 / 80 / 127: -33.7 / -18.3 / -17.8 dB, centroid +0.65 oct. Pressure ramp: 0.0 dB distance (+0.0 dB, +0.00 oct); wheel: 0.9 dB (-0.2 dB, -0.26 oct). Movement over 10 s: centroid 1.56 oct, level 81.4 dB, flux 5.3 dB per 0.5 s. CPU Key 61 1.95 / 2.96 %, XL 2.81 / 4.50 % (median / p99). Nearest preset: Pitch Drop 808 (94).

*On the Key 61 (`low`).* Level -19.1 dBFS (difference +1.0 dB), peak -3.2 dBFS raw, velocity centroid +1.03 oct, mono sum -0.0 dB.

*Not settled for the ear.* The limiter holds all velocities near the same level above velocity 80 (80 -> 127: 0.5 dB); the velocity then changes the colour, not the loudness.

### Bowed Pressure Strings (Pad)

*Intention.* Strings you play with the weight of your finger: harder pressure opens the filter, brings in the bow's scratch, a vibrato that was not there (the second LFO route is scaled by the pressure) and a brighter pulse layer; at rest it is a soft, slow-speaking ensemble.

*What to play.* Chords or lines C2 .. C5 with the pad's Full Level OFF; press harder and softer while holding. Macros: Brightness, Bow Speed (the attack and release), Rosin (bow noise), Vibrato Rate, Hall.

*Macros.* Brightness (audible: 13.9 dB, +1.3 dB level, +1.17 oct); Bow Speed (audible: 46.4 dB, -44.5 dB level, -0.15 oct); Rosin (audible: 4.6 dB, +0.0 dB level, +0.01 oct); Vibrato Rate (audible: 0.2 dB, -0.1 dB level, -0.02 oct); Hall (audible: 3.0 dB, +0.6 dB level, -0.40 oct).

*Measured.* Level -20.0 dBFS (XL), -19.5 (Key 61); 8-note velocity-127 peak -3.5 dBFS raw = 14.5 at +18 dB; mono sum -0.8 dB vs stereo, worst band -1.2 dB at 504 Hz; DC 0.0000, click ratio 0.1, tail 2.01 s. Velocity 30 / 80 / 127: -53.6 / -35.3 / -26.6 dB, centroid +0.61 oct. Pressure ramp: 14.2 dB distance (+1.6 dB, +1.37 oct); wheel: 11.3 dB (-0.4 dB, +0.95 oct). Movement over 10 s: centroid 1.07 oct, level 9.5 dB, flux 6.5 dB per 0.5 s. CPU Key 61 2.32 / 3.63 %, XL 4.86 / 7.72 % (median / p99). Nearest preset: Lush Analog Pad (90).

*On the Key 61 (`low`).* Level -19.5 dBFS (difference +0.6 dB), peak -2.9 dBFS raw, velocity centroid +0.50 oct, mono sum -0.7 dB.

*Not settled for the ear.* How much pressure range the pad really gives; the layer is tuned for 0 .. 127 over the full travel.

### Wah Pressure Clav (Keys)

*Intention.* A clav with a pedal under the fingertip: pressure sweeps a Multi SVF band-pass wah and tightens its resonance, velocity sets how long the note rings and how hard the diode bites, a pick envelope, an EQ high pass.

*What to play.* Funk comping C2 .. C4, short chords; press during the chord for the wah; Full Level OFF. Macros: Wah Range (the pressure route's depth: it only acts with pressure), Quack, Pick, Dirt, Phase.

*Macros.* Wah Range (weak (see note): 0.0 dB, +0.0 dB level, +0.00 oct); Quack (audible: 8.9 dB, +10.2 dB level, +0.13 oct); Pick (audible: 12.7 dB, -3.8 dB level, +1.25 oct); Dirt (audible: 28.5 dB, +23.0 dB level, +0.08 oct); Phase (weak (see note): 1.7 dB, -1.2 dB level, +0.12 oct).

*Measured.* Level -20.0 dBFS (XL), -20.0 (Key 61); 8-note velocity-127 peak -9.8 dBFS raw = 8.2 at +18 dB; mono sum -1.5 dB vs stereo, worst band -3.8 dB at 200 Hz; DC 0.0000, click ratio 0.9, tail 0.12 s. Velocity 30 / 80 / 127: -53.7 / -33.1 / -25.2 dB, centroid +1.32 oct. Pressure ramp: 41.6 dB distance (-33.3 dB, +2.84 oct); wheel: 1.1 dB (-0.7 dB, +0.02 oct). Movement over 10 s: centroid 0.16 oct, level 4.4 dB, flux 3.6 dB per 0.5 s. CPU Key 61 3.59 / 5.12 %, XL 3.76 / 5.31 % (median / p99). Nearest preset: Acid Saw Reso (76).

*On the Key 61 (`low`).* Level -20.0 dBFS (difference -0.0 dB), peak -9.8 dBFS raw, velocity centroid +1.32 oct, mono sum -1.5 dB.

*Not settled for the ear.* The wah range of 46 % (59 semitones) at full pressure may be too much for a finger; the Wah Range macro scales it.

## 7. Engine limits and defects found while building

Everything below was seen in a render or a measurement, with the numbers; nothing was worked around silently (the workaround, where there is one, is named).

1. **Ladder filter (model 2) collapses at the top of the range.** Saw C4, resonance 22, 44.1 kHz, RMS of the output by cutoff: 10 kHz -43 dB, 14 kHz -44, 16 kHz -39, 17 kHz -36, 18 kHz -35, **19 kHz -54, 20 kHz -68**. Dirty, EMS Ladder, French LP, Normal and Digital are flat over the same range. A macro, a pressure route or an envelope that pushes a Ladder cutoff over about 18 kHz makes the sound vanish. Worked around by not using Ladder in this batch (Analog instead).
2. **A source sent to `Direct out` ignores `master.volume`**: the chord level of the sub bass on `Direct out` was +3 dBFS at `master.volume` -12, -30 and -45 alike; only `direct.level` scales it. The levelling tool moves `master.volume` only, so a preset with a Direct-out source cannot be levelled by it. The sub of Subterranean Duo uses `Effects only` (it goes round the filters all the same).
3. **Spectral morph**: *Smear* at 100 % and *High Pass* at 60 % and more give silence (-inf dB); *Random Amplitudes* is not monotonic (on Bell Partials 30 %: -73 dB, 60 %: +6 dB); *Vocode* does nothing without a modulator oscillator; *Phase Disperse* does not change the magnitude spectrum (fine). The Harmonic Stretch, Inharmonic Stretch, Formant Scale and Shepard Tone morphs behave (Shepard Tone at 0 .. 100 % turns the saw into its octave: the odd partials fade out, the even ones stay).
4. **Cost of a moving morph.** Any change of the frame or of the morph amount of an oscillator with a morph type rebuilds that oscillator's morph cycle in every voice: +5 .. 9 % of the XL block per oscillator (Stretched Glass Choir: 14.5 % with three such modulations, 8.2 % with one). The cost is the same whether the amount or the frame moves, and a static morph is free.
5. **The 4x warp costs per unison voice**: one single-voice warped oscillator is +7 .. 9 % of the XL block (Sync 9.6 %, Dist Sine Fold 10.2, Bend 11.6 including the 2.6 % base), and the same oscillator with 3 unison voices three times that (Bend Formant Growl with Bend + Formant warps on 3 voices: 44 %).
6. **Comb filter pitch**: with keytrack 100 the comb sounds 1.1 % flat (about -19 cents; 258.7 Hz for 261.6) at every note; compensated in Karplus Harp by a cutoff of 264.6 Hz. Its output is also 30 dB or more under a noise burst's level: a pluck needs +22 dB of make-up after it, and `master.volume` (maximum +6 dB) is not enough, so Karplus Harp uses `eq.level` +12 and `utility.level` +10.
7. **Filter styles**: for most models the twelve styles are identical (Diode, Comb, Ring Mod, Sample Hold, French LP, Add Bass, New Wsp, DJ Mixer, Diffusor, EMS Ladder, PZ SVF, Formant from style 2) and `EQ 6 dB` and `Misc` (styles 0 .. 2) did not change a noise spectrum at cutoff 1 kHz and resonance 30 (they may need `blend` / `var`; not used). DJ Mixer is a low pass below a cutoff of about 600 Hz and a high pass above 800 Hz, not a high pass at any cutoff. The model table of what is a low, high or band pass in which style is in the build log of this work (`scripts/sound-lab.py` renders it).
8. **Effects are global, their modulation follows the newest note**: a voice source (envelope, velocity, LFO) that moves an effect parameter (Triple Fold Reese: `distortion.drive` from envelope 3) is read from the most recently started voice, so with a chord the earlier notes' fold follows the last note's envelope. By design (document 04 section 3.1); it limits what can be called per-note in an effect.
9. **The macro on a route's amount (`modNN.amount` as a destination) works, and `check-presets.py` and `name-preset-macros.py` see a macro through its route's source only**: a macro that acts through the aux source of a route is invisible to them and counts as dead. The new presets avoid it.
10. **`check-presets.py` rule `mod_slot_duplicate`** counts two routes with the same source and destination as duplicates also when their aux sources differ (a vibrato that is always on plus a vibrato that the wheel adds). The engine sums them. Worked around with a second LFO of the same rate.
11. **`scripts/tune-preset-levels.py` holds the chord for 1 s**: a swelling pad (attack 1 .. 3 s) is levelled where it has hardly started and is then 5 .. 10 dB too loud at its sustain (the first hundred's pads have `master.volume` up to +2.9). The new presets are levelled at the sustain (`scripts/sound-lab.py`); the old ones are untouched.
12. **`own_support.cpp` does not list keys the voice implements**: `osc1.unison.stack` (and `stack_repeat`, `blend_shape`), `oscN.map.key_fold`, `oscN.transpose_snap`, `oscN.phase_memory`, `phase_legato`, `map.key_warp`, `fixed_pitch` are read by `core/engine-own/voice/voice.cpp` and work (measured: with the key range 36 .. 59 and key fold on, note 24 sounds at 65.3 Hz and note 72 at 130.9 Hz, without the fold note 24 is silent; transpose snap with the minor pentatonic mask 1193 turns a transpose of 2.49 into 3.00 and of 6.05 into 5.00 semitones; unison stack Power Chord puts the fifth and the octave at -3 and 0 dB against -110 and -6 dB for plain unison), but `implementedKey()` in `core/engine-own/own_support.cpp` says they are not, so `supportsParam` is false for them and `synthyboy_preset_render --engine own` fails the four presets that use them with "needs engine capabilities the engine does not have" (Subterranean Duo, Triple Fifth Lead, Parallel Dusk Pad, Pentatonic Step Pluck: 8 findings). The gate `synthyboy_preset_sanity` runs the Vital engine and does not see it. The fix is the keys in that list; I did not touch the engine. (`sub.mono` is not implemented: removed from the presets.)
13. **Sample, granular, spectral, multisample oscillators and Convolve could not be shown**: the package ships no sample, wavetable or impulse (`content/` has only the presets), and the engine without content gives silence for a sample type and no convolution. Not referenced, as asked.
14. **At `low` the Key 61 loses**: reverb, flanger, Hyper/Dimension, Bode, Convolve (profile `low`). Bode Barber Riser, Detonation Tail, Triple Fifth Lead and Shepard Ascent are the ones that lose most; every preset is levelled on the XL, the Key 61's level is in the table (the largest difference is Bode Barber Riser, +4 dB, because the dry riser is louder than the shifted one).
15. **State version of the LFO shape grid**: `grid_x` / `grid_y` of a drawn LFO shape belong to state version 5 (`core/include/synthyboy/content_state.h`), the factory documents are version 4 (the checker and a pinned test say so). The two presets with a drawn shape carry the keys in a version 4 document: the loader of any version reads them (`core/src/state.cpp`), a version 4 reader skips them. Decide: raise those two documents to 5 and the checker's expectation with them, or keep it.
16. **MPC program files**: `scripts/gen-mpc-xpl.py` takes the program count (113 = Init + 112 presets) from the template file the device wrote; with 136 presets the plugin offers 137 programs, so the template has to be saved again from the new package before `.xpl` files are generated (the tests that need the helper binary or the fixtures were not run here).

## 8. Not shown, and why

- Sample, granular, spectral and multisample oscillators, Convolve: no content in the package (above). A small CC0 pack made by the generator (a few synthesized one-shots, loops and two impulse responses) would open them: that is the first thing for batch 2.
- Filter models: 6 of the 16 the survey lists are in the batch (French LP, Multi SVF in six presets, Ring Mod, Smear, Add Bass, Flanges); Comb and Formant, which the first 112 hardly use, are in two and three. Not used: Slope, Normal, New, DJ Mixer, Multi Morph (the first builds of Subterranean Duo, Triple Fold Reese, Parallel Dusk Pad, Brass Swell Ensemble, Wah Pressure Clav, Shepard Ascent, Bode Barber Riser and Triple Fifth Lead used them; they were replaced because Slope, Normal, New and DJ Mixer cost 3 .. 4 % of the Key 61 block and 6 .. 8 % of the XL's per voice set; the EQ's high-pass mode is the cheap high pass), and Sample Hold, New Wsp, EQ 6 dB, Misc, PZ SVF.
- The oscillator options: key fold and key range (Subterranean Duo), transpose snap (Pentatonic Step Pluck), phase memory (Parallel Dusk Pad): one each; the key-fold and key-range options show up only where the key range of the sound is the point.
- The mixer buses, voice control (`voicectl`), the tuning scales, MPE, the unison stack modes Major / Minor / Harmonic Series: none used.

## 9. Batch 2

1. A listening round on these 24 first: the "Not settled for the ear" lines, the macros that were measured as weak, and the level policy (section 2).
2. A generated CC0 content pack (samples, loops, impulse responses) and the presets that need it: sample / granular / spectral / multisample, convolution; each shown at least twice.
3. The remaining filter models (Sample Hold, New Wsp, EQ 6 dB) and the other warp families (Spectral Comb, Add Harmonics, Spectral Gate, PD, Mask, Vocode with a real modulator), each on a cost that the XL can pay.
4. The buses (bus 1 / bus 2 with their own effect racks, sends), voice control (per-voice pan / detune / cutoff patterns), tuning scales and MPE presets.
5. Per-note effects are not possible; presets for the Key 61 that do not lose a reverb (the plate-like sound built in the oscillator, the delay as the space) as a pair to the XL versions.
6. A pass of the existing 112 against the same measures: the velocity (none of the first hundred changes its tone with velocity: centroid 0.00 oct from 30 to 127) and the pad pressure on the held ones.

