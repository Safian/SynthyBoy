# Factory presets report - state v3, macro wiring, level normalisation, showcase presets (2026-11-08)

Branch cut from develop `c44ce1f`. Generator: `scripts/gen-factory-presets.py` (`--out DIR`, `--check`); levels: `scripts/factory-preset-levels.json`, written by `scripts/tune-preset-levels.py`. Measure: `synthyboy_preset_render` (Vital engine, offline render).

## 1. Generator (state v3, deterministic)

* The 112 `.sbpreset` files are state version 3 documents with all 1484 keys explicit in table order (values as `formatG9`), plus a `content` section only where the preset needs one (Custom Shape Wobble: `lfo_shapes`).
* Reproduces the shipped sounds: for all 100 old presets the 880 version-2 keys equal the shipped values (checked by a script against the files of `develop`, 0 differences; the two hand-fixed presets, FM Digital Lead and Pulse Width Bass, come out of the script's own data). Byte-identical on re-run (`diff -r` of two output directories; `--check` and ctest `synthyboy_factory_presets_reproducible` guard the repository files).
* Two findings in the old data, kept as they were (a sound change is the owner's call):
  * Four presets set `filter1.drive` above its range 0 .. 20 (Dirty Distortion Bass 65, Diode Saturated Bass 50, Dirty Peak Lead 55, Diode Bite Lead 40). The loader always clamped them, so the files now carry 20 (same sound).
  * Ten spellings in the data of the old presets are not keys of the table and were silently dropped by the old generator: `fx.reverb.on` (26 presets), `fx.delay.on` (14), `fx.chorus.on` (15), `fx.distortion.on` (8), `fx.distortion.drive`, `fx.chorus.wet`, `fx.reverb.wet`, `noise.volume`, `osc2.volume`, `master.portamento`. The shipped presets therefore have those effects, noise and second-oscillator levels at their defaults, i.e. no reverb / delay / chorus / distortion unless a macro opens it. The script lists them (`IGNORED_LEGACY_KEYS`) and still ignores them; new presets may not use unknown keys (strict). Making them real (`reverb.on`, `delay.on`, `chorus.on`, `distortion.on`, `noise.level`, `osc2.level`, `master.portamento.time`) is the one-line-per-key decision for the listening session; the levels would then be re-tuned with `tune-preset-levels.py`.

## 2. Macro wiring (Q29)

Every preset: macro 1 .. 4 routed to the destinations of its category (09 section 2.3; `chorus.mix` etc. are the table keys of the same name) in mod slots after the highest slot the preset already uses (Space Wobble Bass keeps its own `mod01.amount`), source `macroN`, amount = the percentage of the table, unipolar, power 0. Macros stay 0 at default, so the sound at the default positions is the sound without the wiring. Macros 5 .. 8, sparingly: 5 unison stereo spread (osc 1 / 2 with more than one unison voice), 6 filter 1 resonance +30 (filter on, not in Pad), 7 osc 2 detune and warp (osc 2 on), 8 noise level (noise on). Where a macro opens an effect that the preset has off (almost all, see the dead spellings above), the effect is switched on with its mix at 0 and the macro also opens its mix (distortion +100, delay and reverb +40, FX filter +70), otherwise "Space" or "Drive" would do nothing. Rendered at the macro defaults the wired presets equal the unwired ones except two plucks (Acoustic Style Pluck, Bell Mallet Pluck: peak -1.1 dB, RMS -0.3 dB; any enabled effect module shifts the first transient of the engine by that much, reproduced with delay and with reverb at mix 0).

Macro audibility test (macro N set to 100, chord RMS against -29.0 dBFS): Acid Saw Reso macro 1 -9.1 dB, 2 +8.1 dB, 4 +1.3 dB (reverb), 6 +1.6 dB; Solid Sub macros 1, 2, 4, 6 +1.9 .. +5.3 dB; Vibraphone Bars macros 2, 4, 6; Hyper Saw Lead macros 3, 4, 6. Macros 3, 5, 7, 8 are inert where the preset has no LFO routing, one unison voice, no second oscillator or no noise.
`lfo1.rate` macros (Bass 3, Pad 3, Keys 2, FX 2) act only where LFO 1 is routed; no factory preset routes LFO 1 (the old Space Wobble Bass has the amount without a source), so these macros are inert until the sounds get a routing.

## 3. Level normalisation

Measure: RMS of the loudest 300 ms of the four-note chord (velocity 1.0, held 1 s, C1 root for Bass else C3), master.volume moved 1:1 (the stage is last in the path; one round converges). Target -29 dBFS, tolerance +-3 dB, chord peaks held below -1.5 dBFS (none needed holding).

| | before (100 presets) | after (112 presets) |
|---|---|---|
| chord RMS min / median / max (high, medium) | -34.7 / -19.8 / -8.7 dBFS (sd 6.2 dB) | -29.0 / -29.0 / -29.0 dBFS (sd 0.0) |
| chord RMS, low tier | - | -29.7 / -29.0 / -21.9 (Hyper Dimension Pad: the low tier skips the effect, +7 dB) |
| highest chord peak | +0.9 dBFS (Sub Drop Boom) | -6.8 dBFS |
| master.volume range | -12 dB for 95 presets (-4 .. -8 for five) | -32.3 .. -6.3 dB |

Single notes (velocity 0.8) now sit at -38 dBFS RMS median (-45.7 .. -29.6), peaks -6.8 dBFS at most: a quiet set by design, one key at velocity 1.0 is about 10 dB below a chord. The gate (`synthyboy_preset_sanity`) passes for all 112 presets at low, medium, high.

Columns: master.volume (dB) before / after, chord RMS (dBFS) before / after, chord peak (dBFS) before / after, at the high tier. "new": showcase preset (no before).

| Cat | Preset | vol before | vol after | chord RMS before | chord RMS after | chord peak before | chord peak after |
|-----|--------|-----------:|----------:|-----------------:|----------------:|------------------:|-----------------:|
| Bass | 808 Boom | -5.0 | -18.6 | -15.4 | -29.0 | -3.5 | -17.1 |
| Bass | Acid Saw Reso | -12.0 | -20.3 | -20.7 | -29.0 | -10.6 | -18.9 |
| Bass | Click Attack Bass | -12.0 | -28.3 | -12.7 | -29.0 | -3.6 | -19.9 |
| Bass | Comb Metal Bass | -12.0 | -19.7 | -21.3 | -29.0 | -10.8 | -18.5 |
| Bass | Custom Shape Wobble | - | -21.3 | new | -29.0 | new | -17.7 |
| Bass | Diode Saturated Bass | -12.0 | -20.0 | -21.0 | -29.0 | -10.0 | -18.0 |
| Bass | Dirty Distortion Bass | -12.0 | -32.3 | -8.7 | -29.0 | -3.2 | -23.5 |
| Bass | EMS Squelch Bass | - | -16.7 | new | -29.0 | new | -16.2 |
| Bass | Fast Arp Bass | -12.0 | -6.3 | -34.7 | -29.0 | -12.5 | -6.8 |
| Bass | Fat Reese | -12.0 | -24.7 | -16.3 | -29.0 | -5.9 | -18.6 |
| Bass | Formant Vowel Bass | -12.0 | -24.7 | -16.3 | -29.0 | -8.0 | -20.7 |
| Bass | Gritty Square Bass | -12.0 | -27.1 | -13.9 | -29.0 | -7.3 | -22.4 |
| Bass | Harmonic Sine Bass | -12.0 | -25.4 | -15.6 | -29.0 | -6.9 | -20.3 |
| Bass | Moog Ladder Bass | -8.0 | -19.7 | -17.3 | -29.0 | -5.5 | -17.2 |
| Bass | Odd Harmonics Bass | -12.0 | -25.8 | -15.2 | -29.0 | -6.9 | -20.7 |
| Bass | Pulse Width Bass | -12.0 | -25.1 | -15.9 | -29.0 | -11.1 | -24.2 |
| Bass | Punchy Pluck Bass | -12.0 | -13.2 | -27.8 | -29.0 | -12.4 | -13.6 |
| Bass | Scream Filter Bass | - | -28.7 | new | -29.0 | new | -19.4 |
| Bass | Solid Sub | -6.0 | -24.1 | -10.9 | -29.0 | -1.3 | -19.4 |
| Bass | Space Wobble Bass | -12.0 | -21.2 | -19.8 | -29.0 | -10.6 | -19.8 |
| Bass | Sub Duo Octave | -12.0 | -30.0 | -11.0 | -29.0 | -1.9 | -19.9 |
| Bass | Sync Growl Bass | -12.0 | -13.2 | -27.8 | -29.0 | -19.5 | -20.7 |
| Bass | Warm Analog Bass | -12.0 | -18.3 | -22.7 | -29.0 | -12.6 | -18.9 |
| Lead | Bell Chime Lead | -12.0 | -22.1 | -18.9 | -29.0 | -7.7 | -17.8 |
| Lead | Chorused Saw Solo | -12.0 | -21.9 | -19.1 | -29.0 | -9.6 | -19.5 |
| Lead | Cosmic Drift Lead | -12.0 | -26.0 | -15.0 | -29.0 | -5.0 | -19.0 |
| Lead | Diode Bite Lead | -12.0 | -21.5 | -19.5 | -29.0 | -10.0 | -19.5 |
| Lead | Dirty Peak Lead | -12.0 | -27.5 | -13.5 | -29.0 | -4.9 | -20.4 |
| Lead | Dual Octave Hero | -12.0 | -24.3 | -16.7 | -29.0 | -6.5 | -18.8 |
| Lead | FM Digital Lead | -12.0 | -24.1 | -16.9 | -29.0 | -6.9 | -19.0 |
| Lead | Harmonic Shimmer Lead | -12.0 | -25.9 | -15.1 | -29.0 | -6.7 | -20.6 |
| Lead | Hyper Saw Lead | -12.0 | -26.0 | -15.0 | -29.0 | -2.6 | -16.6 |
| Lead | Laser Beam Lead | -12.0 | -22.1 | -18.9 | -29.0 | -9.3 | -19.4 |
| Lead | MPE Pressure Lead | - | -4.4 | new | -29.0 | new | -18.9 |
| Lead | Metallic Edge Lead | -12.0 | -15.7 | -25.3 | -29.0 | -8.8 | -12.5 |
| Lead | Phaser Sweep Lead | -12.0 | -25.3 | -15.7 | -29.0 | -5.5 | -18.8 |
| Lead | Plucked Lead Tone | -12.0 | -15.2 | -25.8 | -29.0 | -11.6 | -14.8 |
| Lead | Pulse Width Mod Lead | -12.0 | -15.0 | -26.0 | -29.0 | -9.1 | -12.1 |
| Lead | Screaming Sync Lead | -12.0 | -19.3 | -21.7 | -29.0 | -12.6 | -19.9 |
| Lead | Solo Square Lead | -12.0 | -21.0 | -20.0 | -29.0 | -18.9 | -27.9 |
| Lead | Staccato Bleep Lead | -12.0 | -12.8 | -28.2 | -29.0 | -8.9 | -9.7 |
| Lead | Tri Soft Solo | -12.0 | -24.0 | -17.0 | -29.0 | -8.0 | -20.0 |
| Lead | Twin Warp Lead | - | -19.1 | new | -29.0 | new | -19.9 |
| Lead | Vocaloid Lead | -12.0 | -26.8 | -14.2 | -29.0 | -6.1 | -20.9 |
| Lead | Warm Vintage Lead | -12.0 | -19.2 | -21.8 | -29.0 | -11.5 | -18.7 |
| Pad | Cathedral Choir | -12.0 | -14.5 | -26.5 | -29.0 | -14.5 | -17.0 |
| Pad | Comb Reso Swell | -12.0 | -19.2 | -21.8 | -29.0 | -9.3 | -16.5 |
| Pad | Dark Cinema Drone | -12.0 | -17.2 | -23.8 | -29.0 | -14.3 | -19.5 |
| Pad | Deep Sea Ambient | -12.0 | -10.1 | -30.9 | -29.0 | -21.1 | -19.2 |
| Pad | Diffusor Cloud Pad | - | -21.4 | new | -29.0 | new | -17.9 |
| Pad | Ethereal Glass | -12.0 | -16.1 | -24.9 | -29.0 | -12.6 | -16.7 |
| Pad | Frozen Aurora | -12.0 | -18.6 | -22.4 | -29.0 | -11.2 | -17.8 |
| Pad | Gentle Flute Pad | -12.0 | -24.4 | -16.6 | -29.0 | -7.5 | -19.9 |
| Pad | Golden Dawn Pad | -12.0 | -22.2 | -18.8 | -29.0 | -8.6 | -18.8 |
| Pad | Harmonic Cloud | -12.0 | -11.2 | -29.8 | -29.0 | -18.2 | -17.4 |
| Pad | Hyper Dimension Pad | - | -19.3 | new | -29.0 | new | -19.0 |
| Pad | Infinite Horizon | -12.0 | -9.5 | -31.5 | -29.0 | -19.1 | -16.6 |
| Pad | Lush Analog Pad | -12.0 | -23.7 | -17.3 | -29.0 | -4.3 | -16.0 |
| Pad | Odd Harmonics Pad | -12.0 | -25.8 | -15.2 | -29.0 | -7.0 | -20.8 |
| Pad | Radiant Pulse Pad | -12.0 | -21.6 | -19.4 | -29.0 | -10.4 | -20.0 |
| Pad | Slow Motion Waves | -12.0 | -11.6 | -29.4 | -29.0 | -18.3 | -17.9 |
| Pad | Space Odyssey Pad | -12.0 | -21.5 | -19.5 | -29.0 | -8.4 | -17.9 |
| Pad | Starlight Shimmer | -12.0 | -21.2 | -19.8 | -29.0 | -8.7 | -17.9 |
| Pad | Subtle Tape Drift | -12.0 | -21.1 | -19.9 | -29.0 | -10.3 | -19.4 |
| Pad | Tri-Band Split Texture | - | -28.0 | new | -29.0 | new | -18.3 |
| Pad | Velvet Strings | -12.0 | -21.2 | -19.8 | -29.0 | -9.1 | -18.3 |
| Pad | Vintage Brass Pad | -12.0 | -21.8 | -19.2 | -29.0 | -8.2 | -18.0 |
| Pad | Vowel Choir Pad | -12.0 | -25.6 | -15.4 | -29.0 | -6.2 | -19.8 |
| Pluck | Acoustic Style Pluck | -12.0 | -8.9 | -31.9 | -29.0 | -12.7 | -10.7 |
| Pluck | Aux Keyscale Pluck | - | -13.8 | new | -29.0 | new | -13.0 |
| Pluck | Bell Mallet Pluck | -12.0 | -10.9 | -29.8 | -29.0 | -10.6 | -9.5 |
| Pluck | Chime Drop | -12.0 | -12.7 | -28.3 | -29.0 | -9.9 | -10.6 |
| Pluck | Comb Zither | -12.0 | -11.4 | -29.6 | -29.0 | -11.8 | -11.2 |
| Pluck | Digital Glass Tap | -12.0 | -9.7 | -31.3 | -29.0 | -11.9 | -9.6 |
| Pluck | Electric Mute Pluck | -12.0 | -7.4 | -33.6 | -29.0 | -14.1 | -9.5 |
| Pluck | Formant Pop Pluck | -12.0 | -13.4 | -27.6 | -29.0 | -11.4 | -12.8 |
| Pluck | Harmonic Tine Pluck | -12.0 | -14.3 | -26.6 | -29.0 | -8.8 | -11.1 |
| Pluck | Harp Drops | -12.0 | -14.6 | -26.4 | -29.0 | -9.2 | -11.8 |
| Pluck | Kalimba Tines | -12.0 | -16.6 | -24.4 | -29.0 | -5.2 | -9.8 |
| Pluck | Marimba Synth | -12.0 | -12.0 | -28.9 | -29.0 | -10.6 | -10.6 |
| Pluck | Pizzicato Strings | -12.0 | -9.0 | -32.0 | -29.0 | -13.2 | -10.2 |
| Pluck | Rubber Bass Pluck | -12.0 | -13.8 | -27.2 | -29.0 | -8.6 | -10.4 |
| Pluck | Spruce Guitar | -12.0 | -10.4 | -30.6 | -29.0 | -12.7 | -11.1 |
| Pluck | Trance Short Pluck | -12.0 | -7.4 | -33.6 | -29.0 | -14.4 | -9.8 |
| Keys | Accordion Reed | -12.0 | -26.0 | -15.0 | -29.0 | -4.7 | -18.7 |
| Keys | B3 Drawbar Organ | -12.0 | -25.9 | -15.1 | -29.0 | -6.7 | -20.6 |
| Keys | Celesta Bells | -12.0 | -13.5 | -27.5 | -29.0 | -10.8 | -12.3 |
| Keys | Clavinet Funk | -12.0 | -15.4 | -25.6 | -29.0 | -6.7 | -10.1 |
| Keys | Dream Grand Piano | -12.0 | -25.9 | -15.1 | -29.0 | -2.0 | -15.9 |
| Keys | Dyno Electric Piano | -12.0 | -8.4 | -32.6 | -29.0 | -14.5 | -10.9 |
| Keys | FM Soft Organ | -12.0 | -24.0 | -17.0 | -29.0 | -6.9 | -18.9 |
| Keys | Glass Organ Tones | -12.0 | -21.9 | -19.1 | -29.0 | -8.2 | -18.1 |
| Keys | Harpsichord Bar | -12.0 | -12.0 | -29.0 | -29.0 | -11.7 | -11.7 |
| Keys | Just Intonation Keys | - | -21.0 | new | -29.0 | new | -15.6 |
| Keys | Lo-Fi Rhodes | -12.0 | -18.3 | -22.7 | -29.0 | -8.4 | -14.7 |
| Keys | Muted Honky Tonk | -12.0 | -15.0 | -26.0 | -29.0 | -10.0 | -13.0 |
| Keys | Rotary Rock Organ | -12.0 | -25.9 | -15.1 | -29.0 | -6.7 | -20.6 |
| Keys | Utility Width Keys | - | -25.5 | new | -29.0 | new | -15.9 |
| Keys | Vibraphone Bars | -12.0 | -25.9 | -15.1 | -29.0 | -6.7 | -20.6 |
| Keys | Vintage Electric Tine | -12.0 | -23.2 | -17.8 | -29.0 | -4.3 | -15.5 |
| Keys | Warm Toy Piano | -8.0 | -16.6 | -20.4 | -29.0 | -4.7 | -13.3 |
| FX | Alien Communication | -12.0 | -26.8 | -14.2 | -29.0 | -6.1 | -20.9 |
| FX | Bode Shifter Drone | - | -16.4 | new | -29.0 | new | -15.5 |
| FX | Cosmic Teleport | -12.0 | -25.9 | -15.1 | -29.0 | -6.7 | -20.6 |
| FX | Game Over Zap | -12.0 | -17.4 | -23.6 | -29.0 | -8.0 | -13.4 |
| FX | Metal Gong Strike | -12.0 | -27.0 | -14.0 | -29.0 | -6.6 | -21.6 |
| FX | Noise Riser | -12.0 | -11.7 | -29.3 | -29.0 | -14.4 | -14.1 |
| FX | Sci-Fi Laser Sweep | -12.0 | -22.0 | -19.0 | -29.0 | -9.4 | -19.4 |
| FX | Sonar Ping | -12.0 | -21.4 | -19.6 | -29.0 | -11.2 | -20.6 |
| FX | Space Wind Storm | -12.0 | -24.0 | -17.0 | -29.0 | -5.8 | -17.8 |
| FX | Sub Drop Boom | -4.0 | -21.1 | -11.9 | -29.0 | 0.9 | -16.2 |
| FX | Vinyl Dust Crackle | -12.0 | -21.7 | -19.3 | -29.0 | -8.5 | -18.2 |

## 4. Showcase presets (12)

All CC0, author Safian Szabolcs, own sound design, category and tags valid (six categories, lowercase comma list). Keys are strict (every key exists in the table, values in range); `synthyboy_preset_render` now also fails a preset that sets a key, or a choice value, whose capability bits the engine does not report (`supportsParam`, `requiredCapsForValue`); engine-vital reports all bits. The Sample oscillator is not shown (no sample content). "Feature test": the single-note render with the featured key reset to its default, difference = RMS(on - off) / RMS(on), level change on vs off.

| Preset | Cat | Feature | Feature test |
|--------|-----|---------|--------------|
| Scream Filter Bass | Bass | `filter1.model` Scream, env 2 to cutoff | 0.50, +5.0 dB |
| EMS Squelch Bass | Bass | `filter1.model` EMS Ladder, env 2 to cutoff | 0.53, -1.0 dB |
| Custom Shape Wobble | Bass | `lfo1.shape` Custom with `content.lfo_shapes` (3 hops, smoothed) | 0.26, 0.0 dB |
| Twin Warp Lead | Lead | `osc1.warp` Sync + `osc1.warp2` Bend, LFO to warp 2 | 0.37, +0.8 dB |
| MPE Pressure Lead | Lead | `master.mpe.on`, bend range 48, sources `poly_aftertouch` and `slide` | not testable by the offline note render (needs note expression events) |
| Diffusor Cloud Pad | Pad | `filter1.model` Diffusor, LFO to cutoff | 0.24, +0.9 dB |
| Hyper Dimension Pad | Pad | Hyper/Dimension effect | 1.42, -4.6 dB |
| Tri-Band Split Texture | Pad | `splitter_lmh` with distortion (low rack), Utility (mid rack), chorus (high rack) | 1.11, +11.3 dB |
| Aux Keyscale Pluck | Pluck | `mod01.aux_source` = Note (LFO depth follows the key), a second route with aux Mod Wheel | 0.19, 0.0 dB |
| Utility Width Keys | Keys | Utility effect: width 170 %, mono bass below 150 Hz, high pass | 1.20, +1.6 dB |
| Just Intonation Keys | Keys | `master.tuning.scale` Just intonation, A4 = 440 Hz | 1.19, 0.0 dB |
| Bode Shifter Drone | FX | Bode frequency shifter with feedback | 1.78, -4.0 dB |

Not covered by a showcase preset: Convolve (needs a preset IR), Sample oscillator, user wavetable (`Preset table`), key scale quantiser, the M/S splitter (the L/M/H splitter shows the racks).

## 5. Open points for the listening session

* The dead spellings of section 1 (effects off in 100 classics), then a re-tune.
* Low tier: the Hyper/Dimension and Bode presets are +7 / +5 dB louder than at medium and high because the tier skips those modules (tier design).
* Single notes are quiet (-38 dBFS RMS median at velocity 0.8) because the target is the chord.
* LFO 1 is routed by no classic preset, so the `lfo1.rate` macros of Bass 3, Pad 3, Keys 2, FX 2 are inert.
