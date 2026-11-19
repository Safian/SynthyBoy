# Levelling to the stock MPC instruments (2026-11-19)

Branch `work/loudness-fix`. The owner's decision: the factory presets sit beside the stock MPC instruments at EQUAL loudness ("a gyárihoz hasonló működés legyen ... a gyári presetekkel együtt pont egyforma lesz a hangerő"); the output limiter (`core/include/synthyboy/output_stage.h`) is a safety that normally idles; the level ends up clearly lower than package `ov40`; the ceiling stays just under 0 dBFS; the raw engine peaks stay inside the engine's safety knee.

## Method

- **What is compared.** The sample peak of what the host receives (the own engine's output through the shells' output stage: make-up gain `synthyboy::kMakeupGainDb`, limiter on), per preset, against the medians of the stock instruments of its category (`scripts/stock-level-targets.json`, a copy of `.hw-local/mpc-perf/level-analysis/stock-targets.json`: the Key 61 host track meter, fader 0 dB, Hype "Almighty" and nine stock presets; see its `notes`). Categories are the preset folders: Bass, Lead, Pad, Keys, Pluck, FX.
- **Gestures** (`tools/preset-render/level_probe.cpp`, `synthyboy_level_probe`; own engine, offline, 48 kHz, 128 frame blocks, a fresh engine per gesture, held 3 s, the factory content packs as the store): the note key 48 (C3 with C4 = 60) and the chord 36 48 55 60 at velocity 127 and 100, the six note chord 36 43 48 55 60 64 at velocity 127; under the device profiles of the Key 61 (`low`) and of the MPC XL (`high,voices=16,control=economy`). Per gesture: raw peak, the peak the limiter sees (raw + gain), the peak after the stage, the sustained level (RMS 0.3 .. 2.0 s), the loudest 400 ms (K-weighted momentary loudness), the deepest reduction and the share of blocks in which the limiter reduced.
- **The fit** (`scripts/level-to-stock.py solve`): per preset one change of `master.volume` = the least-squares fit, equal weights, of the four dB errors (note peak v127 - target), (chord peak v127 - target) on the Key 61 and on the XL profile, i.e. the mean of (target - peak). `master.volume` is the last gain before the engine's safety knee, so the peaks follow it 1:1 while the raw peak stays under the knee; rounds of measure, set, regenerate (`scripts/factory-preset-levels.json`, `python3 -I scripts/gen-factory-presets.py`) until no preset moves by more than 0.05 dB (or the last round). Clamped to the range of `master.volume` (-60 .. +6 dB), rounded to 0.1 dB.
- **The make-up gain** (`scripts/level-to-stock.py gains`): the gain at which the median preset needs no `master.volume` change, in steps of half a decibel, raised until no preset would need more than +6 dB of `master.volume`.
- **The velocity tracking** (`master.velocity_track` per category in `scripts/gen-factory-presets.py`) is not changed by this levelling: it is compared with the stock dynamics below.

To repeat after an engine change: build `synthyboy_level_probe`, then `python3 -I scripts/level-to-stock.py solve --probe BUILD/tools/preset-render/synthyboy_level_probe --content-dir BUILD/tools/preset-render/content-store --gain <kMakeupGainDb> --work DIR` and `report --measure DIR/<last>.jsonl --before <earlier>.jsonl`. The check is the test `output_stage_presets_sit_at_the_stock_level` in `core/tests/test_output_stage_presets.cpp`.


## Targets

| category | note peak v127 | chord peak v127 | note peak v100 | chord peak v100 | six note chord v127 | note sustained | chord sustained |
|---|---|---|---|---|---|---|---|
| Bass | -9.1 | -7.0 | -9.2 | -8.0 | -8 | -20 | -22 |
| Lead | -11.5 | -8.0 | -10.6 | -10.0 | -8 | -19.5 | -19 |
| Pad | -9.6 | -5.6 | -12.4 | -8.9 | -3.5 | -19 | -15 |
| Keys | -10.5 | -4.0 | -18.6 | -12.0 | -3.6 | -27 | -27 |
| Pluck | -16.2 | -8.1 | -19.9 | -11.7 | -7 | -31 | -26 |
| FX | -11.0 | -7.0 | -12.0 | -10.0 | -6 | -22 | -20 |

The Bass and Lead references are monophonic plugins: their chord targets are a judgement, not a measurement; FX is interpolated (the file's `notes`). Only the two velocity 127 peak rows are fitted; the rest are checked below.

## The make-up gain

Needed `master.volume` change per preset at candidate gains (measured on the presets as they were, the `ov40` levels):

```
make-up gain: median / p10 / p90 / min / max of the needed master.volume change (dB); presets that would need master.volume above +6 or below -60
   +0.0 dB: median -5.74  p10 -13.46  p90 -1.31  min -17.05  max +5.45   over +6: 0 []  under -60: 0
   -6.0 dB: median +0.26  p10 -7.46  p90 +4.69  min -11.05  max +11.45   over +6: 4 ['Diode Bite Lead', 'Diode Saturated Bass', 'Rising Grain Riser', 'Spectral Gong Pad']  under -60: 0
   +3.0 dB: median -8.74  p10 -16.46  p90 -4.31  min -20.05  max +2.45   over +6: 0 []  under -60: 0
   -5.5 dB: median -0.24  p10 -7.96  p90 +4.19  min -11.55  max +10.95   over +6: 3 ['Diode Bite Lead', 'Diode Saturated Bass', 'Rising Grain Riser']  under -60: 0
   -3.0 dB: median -2.74  p10 -10.46  p90 +1.69  min -14.05  max +8.45   over +6: 1 ['Diode Saturated Bass']  under -60: 0
   -2.5 dB: median -3.24  p10 -10.96  p90 +1.19  min -14.55  max +7.95   over +6: 0 []  under -60: 0
rule (median change about 0, nobody above +6 dB of master.volume): kMakeupGainDb = -2.5 dB
```

**`kMakeupGainDb` = -2.5 dB** (`core/include/synthyboy/output_stage.h`): the rule's value. At -5.5 dB (median change about 0) three presets (Diode Bite Lead, Diode Saturated Bass, Rising Grain Riser) would need more than +6 dB of `master.volume`; at -2.5 dB none does. The applied changes (after the limits of the fit): median -3.6 dB, p10 -11.0, p90 +0.8, min -18.1, max +8.0; `master.volume` now runs from -20.6 to +5.0 dB (median -7.0), no preset at the +6 dB top.

## The fit's limits

- **Chord window**: after the least-squares fit the velocity 127 chord peak is pulled within 2.5 dB of its target on both profiles (`CHORD_WINDOW`). Where the Key 61 and the XL peaks lie more than 5 dB apart (Infinite Horizon) the middle is taken: both miss by 2.9 dB.
- **Knee**: no raw engine peak (note, chord, six note chord, velocity 127, both profiles) above 1.5 of full scale (+3.52 dBFS); the engine's knee starts at 1.8.
- **Smoothing of `master.volume`**: the engine ramps `master.volume` from its default (-12 dB) after a state is loaded; the probe plays 0.3 s of silence before each note (without it a quiet preset's first attack carried the default level: Fast Arp Bass measured 13 dB too loud).
- Converged: the largest move of the last round 0.0 dB.

## Result (`scripts/level-to-stock.py report`)

Targets (`scripts/stock-level-targets.json`): Key 61 host track meter, Hype + 9 stock presets (report add671aa). Bass/Lead refs are MONOPHONIC plugins: chord targets there are conservative judgement (-7/-8), not measured. FX has no stock measurement: interpolated. Stock never exceeded -3 dBFS with 6-note v127 chord, no limiter proven; sustain values are approximate live-bar readings. Velocity: bass/lead insensitive; pianos/plucks/strings drop 5-9 dB v127->v100.

Make-up gain of the measurement: -2.5 dB. Peaks in dBFS after the output stage (what the host receives), sustained = RMS 0.3 .. 2.0 s after the strike, medians per category.

**Key 61 (low) profile**

| category | n | note peak v127 (target) | chord peak v127 (target) | note sustained | chord sustained | note v127-v100 (stock) | chord6 peak | limiter engaged, chord v127 (median share of blocks) | before: note / chord peak, note / chord sustained |
|---|---|---|---|---|---|---|---|---|---|
| Bass | 29 | -14.7 (-9.1) | -4.5 (-7.0) | -21.3 | -15.3 | 1.1 (0.1) | -2.1 | 0.0 % | -0.5 / -0.5, -7.5 / -8.5 |
| Lead | 30 | -15.6 (-11.5) | -6.0 (-8.0) | -23.4 | -17.2 | 1.6 (-0.9) | -4.3 | 0.0 % | -0.5 / -0.5, -8.7 / -8.4 |
| Pad | 36 | -12.4 (-9.6) | -4.2 (-5.6) | -24.1 | -16.4 | 1.8 (2.8) | -1.7 | 0.0 % | -0.5 / -0.5, -10.8 / -9.5 |
| Keys | 25 | -12.0 (-10.5) | -2.5 (-4.0) | -34.0 | -26.3 | 2.7 (8.1) | -0.5 | 0.0 % | -0.5 / -0.5, -22.0 / -15.3 |
| Pluck | 22 | -16.7 (-16.2) | -7.5 (-8.1) | -51.2 | -43.1 | 2.9 (3.7) | -5.9 | 0.0 % | -0.5 / -0.5, -29.6 / -23.5 |
| FX | 18 | -14.3 (-11.0) | -5.6 (-7.0) | -24.8 | -18.9 | 1.8 (1.0) | -3.1 | 0.0 % | -0.5 / -0.5, -10.6 / -9.7 |
| ALL | 160 | -13.5 (-) | -5.2 (-) | -24.6 | -17.5 | 1.8 (-) | -2.7 | 0.0 % | -0.5 / -0.5, -10.4 / -9.5 |

**XL (high, 16 voices, economy control) profile**

| category | n | note peak v127 (target) | chord peak v127 (target) | note sustained | chord sustained | note v127-v100 (stock) | chord6 peak | limiter engaged, chord v127 (median share of blocks) | before: note / chord peak, note / chord sustained |
|---|---|---|---|---|---|---|---|---|---|
| Bass | 29 | -14.7 (-9.1) | -4.5 (-7.0) | -21.3 | -15.6 | 1.1 (0.1) | -2.1 | 0.0 % | -0.5 / -0.5, -7.5 / -8.5 |
| Lead | 30 | -14.6 (-11.5) | -5.5 (-8.0) | -23.4 | -17.1 | 1.6 (-0.9) | -3.6 | 0.0 % | -0.5 / -0.5, -8.7 / -8.4 |
| Pad | 36 | -11.3 (-9.6) | -3.1 (-5.6) | -23.6 | -16.3 | 1.8 (2.8) | -1.4 | 0.0 % | -0.5 / -0.5, -10.8 / -9.5 |
| Keys | 25 | -12.1 (-10.5) | -2.5 (-4.0) | -34.4 | -26.6 | 2.7 (8.1) | -0.5 | 0.0 % | -0.5 / -0.5, -21.9 / -15.4 |
| Pluck | 22 | -16.8 (-16.2) | -7.5 (-8.1) | -51.1 | -42.8 | 2.9 (3.7) | -6.0 | 0.0 % | -0.5 / -0.5, -28.8 / -22.0 |
| FX | 18 | -12.7 (-11.0) | -4.5 (-7.0) | -24.9 | -19.3 | 1.8 (1.0) | -3.4 | 0.0 % | -0.5 / -0.5, -11.1 / -9.8 |
| ALL | 160 | -12.8 (-) | -4.5 (-) | -24.6 | -17.4 | 1.8 (-) | -2.5 | 0.0 % | -0.5 / -0.5, -10.4 / -9.5 |

Residuals (dB, after the fit; per preset the largest of note / chord on Key 61 / XL): median 2.65, p90 6.31, max 7.43; above 3 dB: 75 presets.

| preset | category | note error Key 61 / XL | chord error Key 61 / XL | chord - note (ours) | chord - note (target) |
|---|---|---|---|---|---|
| Pulse Width Bass | Bass | -7.4 / -7.4 | +2.5 / +2.5 | +12.0 | +2.1 |
| Hyper Dimension Pad | Pad | -7.3 / -4.4 | -0.3 / +2.5 | +10.9 | +4.0 |
| Warm Analog Bass | Bass | -7.3 / -7.3 | +2.5 / +2.5 | +11.9 | +2.1 |
| Acid Saw Reso | Bass | -7.1 / -7.1 | +2.5 / +2.5 | +11.7 | +2.1 |
| Fat Reese | Bass | -7.1 / -6.8 | +2.2 / +2.5 | +11.5 | +2.1 |
| Scream Filter Bass | Bass | -7.1 / -7.1 | +2.5 / +2.5 | +11.6 | +2.1 |
| Infinite Horizon | Pad | -7.1 / -3.1 | -2.9 / +2.9 | +8.2 | +4.0 |
| Fast Arp Bass | Bass | -7.1 / -7.1 | +2.5 / +2.5 | +11.7 | +2.1 |
| Custom Shape Wobble | Bass | -7.0 / -7.0 | +2.5 / +2.5 | +11.7 | +2.1 |
| Moog Ladder Bass | Bass | -7.0 / -7.0 | +2.5 / +2.5 | +11.6 | +2.1 |
| Comb Metal Bass | Bass | -7.0 / -7.0 | +2.5 / +2.5 | +11.6 | +2.1 |
| Diode Saturated Bass | Bass | -6.9 / -6.9 | +2.5 / +2.5 | +11.6 | +2.1 |
| EMS Squelch Bass | Bass | -6.9 / -6.9 | +2.5 / +2.5 | +11.4 | +2.1 |
| Twin Warp Lead | Lead | -6.6 / -6.7 | +2.5 / +2.5 | +12.6 | +3.5 |
| Dual Octave Hero | Lead | -6.5 / -6.5 | +2.5 / +2.5 | +12.6 | +3.5 |
| Bode Shifter Drone | FX | -6.3 / +0.0 | -0.4 / +2.5 | +10.0 | +4.0 |
| Sub Duo Octave | Bass | -6.2 / -6.2 | +2.5 / +2.5 | +10.8 | +2.1 |
| Diode Bite Lead | Lead | -6.1 / -6.1 | +2.5 / +2.5 | +12.0 | +3.5 |
| Dirty Peak Lead | Lead | -6.1 / -6.1 | +2.5 / +2.5 | +12.0 | +3.5 |
| Phaser Sweep Lead | Lead | -6.0 / -3.9 | +0.3 / +2.5 | +9.8 | +3.5 |
| Space Wobble Bass | Bass | -5.9 / -5.9 | +2.5 / +2.5 | +10.5 | +2.1 |
| Odd Harmonics Bass | Bass | -5.8 / -5.8 | +2.5 / +2.5 | +10.4 | +2.1 |
| Metal Gong Strike | FX | -5.7 / -2.9 | +1.7 / +2.5 | +11.5 | +4.0 |
| MPE Pressure Lead | Lead | -5.6 / -4.6 | +2.0 / +2.5 | +11.1 | +3.5 |
| Pentatonic Step Pluck | Pluck | +5.6 / +5.6 | -2.5 / -2.5 | +0.0 | +8.1 |
| Karplus Harp | Pluck | +5.6 / +5.6 | -2.5 / -2.5 | +0.0 | +8.1 |
| Sonar Ping | FX | -5.6 / -3.9 | +0.9 / +2.5 | +10.5 | +4.0 |
| Formant Vowel Bass | Bass | -5.6 / -5.6 | +2.5 / +2.5 | +10.1 | +2.1 |
| Hyper Saw Lead | Lead | -5.4 / -2.9 | +1.0 / +2.5 | +9.9 | +3.5 |
| Cosmic Drift Lead | Lead | -5.4 / +0.1 | +0.6 / +2.5 | +9.5 | +3.5 |
| Plucked Lead Tone | Lead | -5.3 / -5.3 | +2.5 / +2.5 | +11.4 | +3.5 |
| Click Attack Bass | Bass | -5.0 / -5.3 | +2.5 / +2.4 | +9.6 | +2.1 |
| Bell FM Ripple Lead | Lead | -4.9 / -5.3 | +2.3 / +2.5 | +10.7 | +3.5 |
| Harmonic Sine Bass | Bass | -5.2 / -5.2 | +2.5 / +2.5 | +9.8 | +2.1 |
| 808 Boom | Bass | -5.0 / -5.0 | +2.5 / +2.5 | +9.7 | +2.1 |
| Sync Cry Lead | Lead | -5.0 / -4.4 | +1.9 / +2.5 | +10.4 | +3.5 |
| Stutter Grain Kit | Pluck | +5.0 / +5.0 | -2.5 / -2.5 | +0.6 | +8.1 |
| Tri Soft Solo | Lead | -4.9 / -3.4 | +0.0 / +2.5 | +8.4 | +3.5 |
| Laser Beam Lead | Lead | -4.8 / -4.8 | +2.5 / +2.5 | +10.8 | +3.5 |
| Lush Analog Pad | Pad | -4.8 / -0.9 | +0.4 / +2.5 | +9.2 | +4.0 |
| Sci-Fi Laser Sweep | FX | -4.7 / -4.7 | +2.5 / +2.5 | +11.2 | +4.0 |
| Frozen Aurora | Pad | -4.7 / -0.2 | +0.6 / +2.5 | +9.3 | +4.0 |
| Solid Sub | Bass | -4.7 / -4.7 | +2.5 / +2.5 | +9.3 | +2.1 |
| Starlight Shimmer | Pad | -4.6 / -2.5 | +1.7 / +2.5 | +10.3 | +4.0 |
| Space Odyssey Pad | Pad | -4.5 / -0.2 | +0.6 / +2.5 | +9.2 | +4.0 |
| FM Digital Lead | Lead | -4.4 / -4.4 | +2.5 / +2.5 | +10.3 | +3.5 |
| Bell Chime Lead | Lead | -4.2 / -4.2 | +2.5 / +2.5 | +10.2 | +3.5 |
| Cathedral Choir | Pad | -4.2 / -2.7 | +0.1 / +2.5 | +8.3 | +4.0 |
| Electric Mute Pluck | Pluck | +4.1 / +4.1 | -2.5 / -2.5 | +1.5 | +8.1 |
| Vocaloid Lead | Lead | -4.0 / -4.0 | +2.5 / +2.5 | +10.1 | +3.5 |
| Comb Reso Swell | Pad | -4.0 / -4.0 | +2.5 / +2.5 | +10.4 | +4.0 |
| Lorenz Garden | Pad | -3.9 / -3.9 | +2.5 / +2.4 | +10.4 | +4.0 |
| Punchy Pluck Bass | Bass | -3.9 / -3.9 | +2.5 / +2.5 | +8.4 | +2.1 |
| Diffusor Cloud Pad | Pad | -3.9 / -2.4 | +1.3 / +2.5 | +9.2 | +4.0 |
| Velvet Strings | Pad | -1.0 / -3.8 | +2.3 / +2.5 | +7.2 | +4.0 |
| Cosmic Teleport | FX | -3.8 / -3.8 | +2.5 / +2.5 | +10.3 | +4.0 |
| Slow Motion Waves | Pad | -3.6 / -3.6 | +2.5 / +2.5 | +10.1 | +4.0 |
| Stretched Glass Choir | Pad | -2.6 / -3.6 | +2.1 / +2.5 | +8.7 | +4.0 |
| Clavinet Funk | Keys | +3.6 / +3.6 | -2.5 / -2.5 | +0.4 | +6.5 |
| Vowel Choir Pad | Pad | -3.5 / -2.5 | +1.3 / +2.5 | +8.8 | +4.0 |
| Ladder FM Bass | Bass | -3.5 / -3.5 | +2.5 / +2.5 | +8.1 | +2.1 |
| Space Wind Storm | FX | -3.4 / -3.4 | +2.3 / +2.5 | +9.8 | +4.0 |
| Vinyl Dust Crackle | FX | -3.4 / -3.3 | +1.6 / +2.5 | +9.0 | +4.0 |
| Sub Drop Boom | FX | -3.4 / -3.4 | +2.5 / +2.5 | +9.9 | +4.0 |
| Alien Communication | FX | -3.4 / -3.4 | +2.5 / +2.5 | +9.9 | +4.0 |
| Ethereal Glass | Pad | -3.4 / -1.5 | +0.4 / +2.5 | +7.7 | +4.0 |
| Dark Cinema Drone | Pad | -3.3 / -1.3 | +1.0 / +2.5 | +8.3 | +4.0 |
| Harmonic Cloud | Pad | -3.3 / -0.2 | -0.1 / +2.5 | +7.2 | +4.0 |
| Detonation Tail | FX | +3.3 / +2.8 | -2.5 / -2.3 | -1.7 | +4.0 |
| Breath of Machines | FX | -3.3 / -2.0 | +2.5 / +2.2 | +9.8 | +4.0 |
| Shepard Ascent | Pad | -2.8 / -3.2 | +2.5 / +2.3 | +9.3 | +4.0 |
| Vibraphone Bars | Keys | -3.2 / -1.5 | +0.2 / +2.0 | +10.0 | +6.5 |
| Staccato Bleep Lead | Lead | -3.1 / -3.1 | +2.5 / +2.5 | +9.1 | +3.5 |
| Bowed MPE Strings | Pad | -2.9 / -3.0 | +2.3 / +2.5 | +9.2 | +4.0 |
| Harmonic Shimmer Lead | Lead | -3.0 / -3.0 | +2.5 / +2.5 | +9.0 | +3.5 |

Limiter, velocity 127 chord: engaged in at most 5 % of the blocks for 100.0 % of the preset renders (Key 61 and XL); median share 0.00 %, p90 0.00 %, max 0.0 %; deepest reduction max 0.00 dB.
Six note chord (36 43 48 55 60 64, velocity 127): limiter engaged at all in 41 of 320 renders, median share 0.00 %, deepest reduction max 1.51 dB.
Raw engine peaks (note, chord, six note chord, velocity 127): max +3.51 dBFS (Golden Dawn Pad); above +3.52 dBFS (1.5): 0; above the knee +5.11 dBFS (1.8): 0.
Key 61 against XL, chord peak velocity 127: median difference 0.04 dB; more than 1 dB apart: 24 presets: Infinite Horizon (-8.5 / -2.7), Bode Shifter Drone (-7.4 / -4.5), Hyper Dimension Pad (-5.9 / -3.1), Harmonic Cloud (-5.7 / -3.1), Tri Soft Solo (-8.0 / -5.5), Cathedral Choir (-5.5 / -3.1), Phaser Sweep Lead (-7.7 / -5.5), Lush Analog Pad (-5.2 / -3.1), Ethereal Glass (-5.2 / -3.1), Golden Dawn Pad (-5.1 / -3.1), Frozen Aurora (-5.0 / -3.1), Space Odyssey Pad (-5.0 / -3.1), Cosmic Drift Lead (-7.4 / -5.5), Bode Barber Riser (-6.4 / -4.5), Deep Sea Ambient (-4.9 / -3.1), Marimba Layer Keys (-3.4 / -1.7), Breakbeat Slice Keys (-5.9 / -4.1), Vibraphone Bars (-3.8 / -2.0), Sonar Ping (-6.1 / -4.5), Dark Cinema Drone (-4.6 / -3.1), Hyper Saw Lead (-7.0 / -5.5), Vowel Choir Pad (-4.3 / -3.1), Diffusor Cloud Pad (-4.3 / -3.1), Frozen Pluck Drone (-3.6 / -4.8)

### Velocity 100, six note chord and sustained level against the stock medians (Key 61 profile, medians; stock in brackets)

| category | note v100 peak | chord v100 peak | chord v127 - v100 | six note chord v127 peak | note sustained | chord sustained |
|---|---|---|---|---|---|---|
| Bass | -15.7 (-9.2) | -5.6 (-8.0) | 1.1 (1.0) | -2.1 (-8.0) | -21.3 (-20) | -15.3 (-22) |
| Lead | -17.3 (-10.6) | -7.6 (-10.0) | 1.6 (2.0) | -4.3 (-8.0) | -23.4 (-19.5) | -17.2 (-19) |
| Pad | -14.4 (-12.4) | -6.1 (-8.9) | 1.8 (3.3) | -1.7 (-3.5) | -24.1 (-19) | -16.4 (-15) |
| Keys | -15.0 (-18.6) | -5.3 (-12.0) | 2.7 (8.0) | -0.5 (-3.6) | -34.0 (-27) | -26.3 (-27) |
| Pluck | -19.7 (-19.9) | -10.5 (-11.7) | 2.9 (3.6) | -5.9 (-7.0) | -51.2 (-31) | -43.1 (-26) |
| FX | -16.0 (-12.0) | -7.3 (-10.0) | 1.8 (3.0) | -3.1 (-6.0) | -24.8 (-22) | -18.9 (-20) |

(Sustained medians leave out the presets that are silent in the 0.3 .. 2 s window.)

## Why 75 presets miss by more than 3 dB

Almost all for one reason: **our chord peak lies 8 to 12 dB above our note peak, the stock one 2 to 4 dB** (Keys 6.5, Pluck 8). The chord (36 48 55 60) adds an octave below the note and three notes above it, and our four voices start together: the first crests add up. The fit cannot meet both: with the chord window the chord sits at most 2.5 dB over its target, and the note of a bass or a lead ends 3 to 7 dB under the stock note. An experiment with `osc1..3.random_phase` = 100 on ten presets moved the chord-to-note distance by 0 to 3 dB only (the attack, not the start phase, carries it). The plucks that miss the other way (Karplus Harp, Pentatonic Step Pluck, Stutter Grain Kit: note about 5 dB over its target, chord at the floor of the window) have a chord no louder than their note (a single-voice design). The stock 4-note chord of the reference may also be voiced differently (the first Hype measurement used 48 55 60 64, all at or above the note).

## Velocity

`master.velocity_track` per category (`scripts/gen-factory-presets.py` `CATEGORY_VELOCITY_TRACK`: Bass 30, Lead 45, Pad 50, FX 50, Keys 70, Pluck 75) scales the level by `1 - track * (1 - v * v)`: at velocity 100 out of 127 that is -1.1 / -1.6 / -1.8 / -1.8 / -2.7 / -2.9 dB, measured the same. The stock drop: Bass 0.1, Lead -0.9 (louder at 100), Pad 2.8 (Solina about 1, strings 4.7), Keys 8.1, Pluck 3.7 (note) / 3.6 (chord), FX 1.0. **Keys clearly contradicts**: even 100 percent tracking gives only -4.1 dB at velocity 100 (the square law of the engine); the stock pianos drop 8 dB. Not changed (owner's decision); proposals: Keys needs a steeper curve than the parameter allows (an engine change, or velocity to level through the modulation matrix per preset); Pad 50 -> 75 (-2.9 dB), Pluck 75 -> 95 (-3.9 dB), Bass 30 -> 5 and Lead 45 -> 0 (the stock basses and leads are velocity-insensitive).

## Six note chord, limiter, knee, Key 61 against XL

See the result block: the limiter never reduced on any velocity 127 four note chord (0 percent of the blocks on all 320 renders); on the six note chord it acts on 41 of 320 renders, at most 1.5 dB. Our six note chord peaks lie 1.5 to 6 dB above the stock medians (the same chord-to-note distance), still under the -0.5 dBFS ceiling. No raw peak above 1.5 of full scale. 24 presets differ by more than 1 dB between the Key 61 and the XL chord peak: the effects the Key 61 skips (reverb, Hyper Dimension, Bode, Convolve) carry the peak; the tier makeup of the engine evens the loudness, not the peak.

## Checks

- `synthyboy_test_output_stage_presets` (`core/tests/test_output_stage_presets.cpp`): `output_stage_on_every_factory_preset` (no sample above the ceiling at make-up gains -2.5 .. 15 dB, no inversion) and `output_stage_presets_sit_at_the_stock_level` (every velocity 127 chord within 3 dB of its target on both profiles: 320 of 320; the limiter reduces in at most 5 percent of the blocks for 320 of 320 renders): pass.
- `synthyboy_preset_render --engine own --summary-only` over the three tiers (the gate `synthyboy_preset_sanity` with the own engine): PASS, 0 failures (before: 57 chord-peak failures at tier low).
- `gen-factory-presets.py --check`, `check-presets.py` (0 errors, the same 79 warnings as before), the preset pytest files: pass.
