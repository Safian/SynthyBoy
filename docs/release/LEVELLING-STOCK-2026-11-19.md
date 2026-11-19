# Levelling to the stock MPC instruments (2026-11-19)

Branch `work/loudness-fix`. The owner's decision: the factory presets sit beside the stock MPC instruments at EQUAL loudness ("a gyárihoz hasonló működés legyen ... a gyári presetekkel együtt pont egyforma lesz a hangerő"); what is heard is the sustained level; the output limiter (`core/include/synthyboy/output_stage.h`, ceiling -0.5 dBFS) is a guard that normally idles; the raw engine peaks stay inside the engine's safety knee.

Two rounds were made the same day. Round 1 fitted the velocity 127 note and chord PEAKS; round 2 (this state) fits the LOUDNESS (sustained levels and the note peak) and treats the chord peak as a limit. Round 1's figures are in the git history of this file.

## Method

- **Targets** (`scripts/stock-level-targets.json`, a copy of `.hw-local/mpc-perf/level-analysis/stock-targets.json`): medians per category of the stock instruments on the Key 61 host track meter (Hype "Almighty" and nine stock presets; Bass and Lead references are monophonic plugins, so their chord targets are a judgement; FX is interpolated). Categories are the preset folders.
- **Gestures** (`tools/preset-render/level_probe.cpp`, `synthyboy_level_probe`): own engine, offline, 48 kHz, 128 frame blocks, a fresh engine per gesture, 0.3 s of silence first (the engine ramps `master.volume` from its default after a state is loaded), held 3 s, the factory content packs as the store, the device profiles of the Key 61 (`low`) and of the MPC XL (`high,voices=16,control=economy`); through the output stage at `kMakeupGainDb`, limiter on. Note 48 (C3 with C4 = 60); the four note chord in the stock voicing 48 55 60 64 and in ours 36 48 55 60; the six note chord 36 43 48 55 60 64; velocity 127 and 100; and the chord of the preset gate (C1 for Bass, else 60, plus 4, 7, 12, released after 1 s). Per gesture: raw peak, the peak the limiter sees, the peak after the stage (the host meter's equivalent), the sustained level (RMS 0.3 .. 2 s after the strike, after the stage), the loudest 400 ms, the strike peak (first 100 ms), the deepest reduction and the share of blocks reduced.
- **Objective** (`scripts/level-to-stock.py solve`): per preset ONE change of `master.volume` = the least-squares fit, equal weights, of the dB errors against the category's targets, on both profiles: Bass, Lead, Pad, FX: note peak v127, note sustained v127, sustained of the stock-voiced chord v127; Keys, Pluck: the note peak only. A sustained level under -80 dBFS (the sound has decayed in the window) is left out. The 2:1 weighting of peak against sustained levels for Keys and Pluck was tried and rejected: our keys and plucks decay faster than the stock ones (their 0.3 .. 2 s level lies 10 to 50 dB under the stock bar readings), the sustained errors pulled the volume up to the raw-peak limit, and 16 of 50 Keys and 18 of 44 Pluck note peaks ended more than 3 dB over their target (peak only: 1 of 50 and 0 of 44).
- **Limits**: the limiter budget on both four note chords at velocity 127 (at most 3 dB of reduction: the peak it sees at most 2.5 dBFS; at most 25 percent of the blocks reduced: a chord over that share goes down 0.3 dB per round); no raw engine peak above 1.4 of full scale (+2.92 dBFS; the knee starts at 1.8) on any gesture; the preset gate's own raw peaks (`synthyboy_preset_render`, every tier) under +2.85 dBFS (its limit is +3); `master.volume` within -60 .. +6 dB; 0.1 dB steps. Rounds of measure, set (`scripts/factory-preset-levels.json`), regenerate (`python3 -I scripts/gen-factory-presets.py`) until the largest move is 0.3 dB (the last rounds moved Game Over Zap by the 0.3 dB step of the limiter share).
- **Velocity tracking** (`CATEGORY_VELOCITY_TRACK` in `scripts/gen-factory-presets.py`): Bass 30 -> 5, Lead 45 -> 0, Pad 50 -> 75, Pluck 75 -> 95; FX 50 and Keys 70 unchanged. The engine scales the level by `1 - track * (1 - v * v)`: at velocity 127 the factor is 1 for any track, so the fit at 127 does not move; at velocity 100 it is -4.1 dB at most.

To repeat after an engine change: build `synthyboy_level_probe` and `synthyboy_preset_render`, then `python3 -I scripts/level-to-stock.py solve --probe BUILD/tools/preset-render/synthyboy_level_probe --content-dir BUILD/tools/preset-render/content-store --gain <kMakeupGainDb> --gate BUILD/tools/preset-render/synthyboy_preset_render --work DIR` and `report --measure DIR/<last round>.jsonl`. The check is the test `output_stage_presets_sit_at_the_stock_level` in `core/tests/test_output_stage_presets.cpp`.

## Targets

| category | note peak v127 | chord peak v127 | note peak v100 | chord peak v100 | six note chord v127 | note sustained | chord sustained |
|---|---|---|---|---|---|---|---|
| Bass | -9.1 | -7.0 | -9.2 | -8.0 | -8 | -20 | -22 |
| Lead | -11.5 | -8.0 | -10.6 | -10.0 | -8 | -19.5 | -19 |
| Pad | -9.6 | -5.6 | -12.4 | -8.9 | -3.5 | -19 | -15 |
| Keys | -10.5 | -4.0 | -18.6 | -12.0 | -3.6 | -27 | -27 |
| Pluck | -16.2 | -8.1 | -19.9 | -11.7 | -7 | -31 | -26 |
| FX | -11.0 | -7.0 | -12.0 | -10.0 | -6 | -22 | -20 |

## The make-up gain

**`kMakeupGainDb` = -1.0 dB**: the rule's value under the round 2 objective (round 1 had chosen -2.5 dB). The `master.volume` change each preset would need, measured on the round 1 levels with the new velocity tracking:

```
make-up gain: median / p10 / p90 / min / max of the needed master.volume change (dB); presets that would need master.volume above +6 or below -60
   -2.5 dB: median +1.48  p10 -1.57  p90 +5.24  min -8.82  max +32.61   over +6: 2 ['Bode Barber Riser', 'Spectral Gong Pad']  under -60: 0
   +0.0 dB: median -1.02  p10 -4.07  p90 +2.74  min -11.32  max +30.11   over +6: 0 []  under -60: 0
   -6.0 dB: median +4.98  p10 +1.93  p90 +8.74  min -5.32  max +36.11   over +6: 8 ['Bode Barber Riser', 'Chord Cloud Pad', 'Diode Saturated Bass', 'Game Over Zap', 'Pitch Drop 808', 'Rising Grain Riser']  under -60: 0
   +3.0 dB: median -4.02  p10 -7.07  p90 -0.26  min -14.32  max +27.11   over +6: 0 []  under -60: 0
   -1.0 dB: median -0.02  p10 -3.07  p90 +3.74  min -10.32  max +31.11   over +6: 0 []  under -60: 0
   -0.5 dB: median -0.52  p10 -3.57  p90 +3.24  min -10.82  max +30.61   over +6: 0 []  under -60: 0
rule (median change about 0, nobody above +6 dB of master.volume): kMakeupGainDb = -1.0 dB
```

At -1.0 dB the median change is 0.0 dB (p90 +3.7) and, with the limits applied, no preset needs more than +6 dB; at -2.5 dB two would have (Bode Barber Riser, Spectral Gong Pad). After the levelling one preset sits at the +6 dB top (Bode Barber Riser, a riser, quiet in the 0.3 .. 2 s window: held back below). `master.volume` now runs from -21.1 to +6.0 dB (median -7.9); against the -2.5 dB state of round 2 every preset moved by -1.5 dB or less (the ones held back by the raw-peak limit did not move: they are 1.5 dB louder).

## Result (`scripts/level-to-stock.py report`, the final round)

Targets (`scripts/stock-level-targets.json`): Key 61 host track meter, Hype + 9 stock presets (report add671aa). Bass/Lead refs are MONOPHONIC plugins: chord targets there are conservative judgement (-7/-8), not measured. FX has no stock measurement: interpolated. Stock never exceeded -3 dBFS with 6-note v127 chord, no limiter proven; sustain values are approximate live-bar readings. Velocity: bass/lead insensitive; pianos/plucks/strings drop 5-9 dB v127->v100.

Make-up gain of the measurement: -1.0 dB. Medians per category; peaks in dBFS after the output stage (what the host receives; the limiter's ceiling is -0.5 dBFS), sustained = RMS 0.3 .. 2.0 s after the strike (presets silent there left out); chord = the stock voicing 48 55 60 64 unless marked "ours" (36 48 55 60); target in brackets.

**Key 61 (low) profile**

| category | n | note peak | note sustained | chord sustained | chord peak | chord peak, ours | six note chord peak | note v127-v100 | chord v127-v100 | before: note peak / note sus / chord peak / chord sus (ours, ov40 levels at +9 dB) |
|---|---|---|---|---|---|---|---|---|---|---|
| Bass | 29 | -14.2 (-9.1) | -21.4 (-20.0) | -16.1 (-22.0) | -4.8 (-7.0) | -4.5 | -1.8 (-8.0) | 0.2 (0.1) | 0.2 (1.0) | -0.5 / -7.5 / -0.5 / -8.5 |
| Lead | 30 | -12.7 (-11.5) | -21.8 (-19.5) | -15.8 (-19.0) | -3.6 (-8.0) | -3.5 | -1.2 (-8.0) | 0.0 (-0.9) | 0.0 (2.0) | -0.5 / -8.7 / -0.5 / -8.4 |
| Pad | 36 | -10.0 (-9.6) | -21.3 (-19.0) | -14.4 (-15.0) | -1.0 (-5.6) | -1.9 | -0.5 (-3.5) | 2.9 (2.8) | 2.9 (3.3) | -0.5 / -10.8 / -0.5 / -9.5 |
| Keys | 25 | -10.5 (-10.5) | -33.1 (-27.0) | -26.7 (-27.0) | -1.4 (-4.0) | -1.3 | -0.5 (-3.6) | 2.7 (8.1) | 2.7 (8.0) | -0.5 / -22.0 / -0.5 / -15.3 |
| Pluck | 22 | -16.2 (-16.2) | -50.4 (-31.0) | -44.1 (-26.0) | -7.2 (-8.1) | -6.9 | -5.8 (-7.0) | 3.9 (3.7) | 3.9 (3.6) | -0.5 / -29.6 / -0.5 / -23.5 |
| FX | 18 | -12.1 (-11.0) | -23.5 (-22.0) | -17.7 (-20.0) | -4.9 (-7.0) | -4.3 | -2.0 (-6.0) | 1.8 (1.0) | 1.8 (3.0) | -0.5 / -10.6 / -0.5 / -9.7 |
| ALL | 160 | -11.9 (-) | -22.4 (-) | -16.4 (-) | -3.5 (-) | -3.2 | -0.8 (-) | 1.9 (-) | 1.8 (-) | -0.5 / -10.4 / -0.5 / -9.5 |

**XL (high, 16 voices, economy control) profile**

| category | n | note peak | note sustained | chord sustained | chord peak | chord peak, ours | six note chord peak | note v127-v100 | chord v127-v100 | before: note peak / note sus / chord peak / chord sus (ours, ov40 levels at +9 dB) |
|---|---|---|---|---|---|---|---|---|---|---|
| Bass | 29 | -13.9 (-9.1) | -21.1 (-20.0) | -16.1 (-22.0) | -4.8 (-7.0) | -4.5 | -1.8 (-8.0) | 0.2 (0.1) | 0.2 (1.0) | -0.5 / -7.5 / -0.5 / -8.5 |
| Lead | 30 | -12.0 (-11.5) | -21.2 (-19.5) | -15.7 (-19.0) | -3.4 (-8.0) | -3.4 | -1.2 (-8.0) | 0.0 (-0.9) | 0.0 (2.0) | -0.5 / -8.7 / -0.5 / -8.4 |
| Pad | 36 | -8.9 (-9.6) | -21.2 (-19.0) | -14.0 (-15.0) | -1.0 (-5.6) | -0.9 | -0.5 (-3.5) | 2.9 (2.8) | 2.9 (3.3) | -0.5 / -10.8 / -0.5 / -9.5 |
| Keys | 25 | -10.5 (-10.5) | -33.2 (-27.0) | -27.8 (-27.0) | -1.5 (-4.0) | -1.3 | -0.5 (-3.6) | 2.7 (8.1) | 2.7 (8.0) | -0.5 / -21.9 / -0.5 / -15.4 |
| Pluck | 22 | -16.2 (-16.2) | -51.1 (-31.0) | -43.0 (-26.0) | -7.3 (-8.1) | -6.9 | -5.8 (-7.0) | 3.9 (3.7) | 3.9 (3.6) | -0.5 / -28.8 / -0.5 / -22.0 |
| FX | 18 | -11.1 (-11.0) | -23.5 (-22.0) | -17.7 (-20.0) | -4.1 (-7.0) | -3.6 | -1.7 (-6.0) | 1.8 (1.0) | 1.8 (3.0) | -0.5 / -11.1 / -0.5 / -9.8 |
| ALL | 160 | -11.3 (-) | -22.2 (-) | -16.3 (-) | -3.2 (-) | -2.6 | -0.7 (-) | 1.8 (-) | 1.9 (-) | -0.5 / -10.4 / -0.5 / -9.5 |

**Voicing** (Key 61, medians of ours 36 48 55 60 minus the stock voicing 48 55 60 64, dB): Bass peak +0.1 / sustained +0.6, Lead peak +0.0 / sustained +0.4, Pad peak -0.5 / sustained +0.5, Keys peak +0.0 / sustained +0.5, Pluck peak +0.0 / sustained +0.2, FX peak +0.1 / sustained +0.1.

**Chord against note** (Key 61, stock voicing, medians, dB): peak of the chord minus peak of the note, raw: Bass +10.4 (strike, first 100 ms: +9.4; stock +2.1), Lead +9.7 (strike, first 100 ms: +9.4; stock +3.5), Pad +9.1 (strike, first 100 ms: +8.7; stock +4.0), Keys +9.3 (strike, first 100 ms: +9.1; stock +6.5), Pluck +9.0 (strike, first 100 ms: +9.0; stock +8.1), FX +8.9 (strike, first 100 ms: +8.6; stock +4.0); sustained chord minus note: Bass +5.9 (stock -2.0), Lead +6.3 (stock +0.5), Pad +6.9 (stock +4.0), Keys +6.2 (stock +0.0), Pluck +6.3 (stock +5.0), FX +6.4 (stock +2.0).

Residuals of the objective (dB; per preset the largest of its errors on both profiles): median 2.90, p90 7.77, max 42.43; above 3 dB: 78 presets.

| preset | category | note peak error Key 61 / XL | note sustained error | chord sustained error | limited by |
|---|---|---|---|---|---|
| Game Over Zap | FX | +10.8 / +10.8 | -42.4 / -42.4 | -38.7 / -38.7 | the spread of its own errors |
| Punchy Pluck Bass | Bass | -1.1 / -1.1 | -30.5 / -30.5 | -22.8 / -22.8 | raw peak |
| 808 Boom | Bass | -1.2 / -1.2 | -27.6 / -27.6 | -19.6 / -19.6 | raw peak |
| Shepard Ascent | Pad | +2.1 / +1.7 | -18.0 / -17.7 | -12.3 / -12.1 | raw peak |
| Sub Drop Boom | FX | +0.1 / +0.1 | -15.8 / -15.8 | -11.8 / -11.8 | raw peak |
| EMS Squelch Bass | Bass | -2.5 / -2.5 | -15.5 / -15.5 | -7.1 / -7.1 | raw peak |
| Lorenz Garden | Pad | -0.6 / -0.6 | -12.6 / -12.6 | -9.7 / -9.6 | raw peak |
| Breath of Machines | FX | -0.6 / +0.7 | -10.7 / -11.6 | -1.6 / -3.2 | raw peak |
| Bode Barber Riser | FX | +2.8 / +4.7 | -8.9 / -9.6 | -4.5 / -5.2 | the spread of its own errors |
| Infinite Horizon | Pad | -3.2 / +0.8 | -8.9 / -7.2 | -6.9 / -5.8 | raw peak |
| Plucked Lead Tone | Lead | -0.0 / -0.0 | -8.6 / -8.6 | -2.4 / -2.4 | raw peak |
| Sub Duo Octave | Bass | -7.5 / -7.5 | -0.7 / -0.7 | +8.3 / +8.3 | the spread of its own errors |
| Odd Harmonics Bass | Bass | -7.9 / -7.9 | -0.1 / -0.1 | +8.1 / +8.1 | the spread of its own errors |
| Pulse Width Bass | Bass | -7.9 / -7.9 | +0.2 / +0.2 | +7.7 / +7.7 | the spread of its own errors |
| Warm Analog Bass | Bass | -6.9 / -6.9 | -0.9 / -0.9 | +7.8 / +7.8 | the spread of its own errors |
| Gritty Square Bass | Bass | -7.8 / -7.8 | +2.6 / +2.6 | +5.1 / +5.1 | the spread of its own errors |
| Dirty Distortion Bass | Bass | -7.7 / -7.7 | +3.0 / +3.0 | +4.8 / +4.8 | the spread of its own errors |
| Comb Metal Bass | Bass | -4.8 / -4.8 | -2.8 / -2.8 | +7.6 / +7.6 | the spread of its own errors |
| Moog Ladder Bass | Bass | -5.1 / -5.1 | -2.7 / -2.7 | +7.6 / +7.6 | the spread of its own errors |
| Scream Filter Bass | Bass | -6.5 / -6.5 | -1.1 / -1.1 | +7.5 / +7.5 | the spread of its own errors |
| Lush Analog Pad | Pad | -1.6 / +2.3 | -7.4 / -4.7 | -3.3 / -0.7 | raw peak |
| Acid Saw Reso | Bass | -4.6 / -4.6 | -2.8 / -2.8 | +7.4 / +7.4 | the spread of its own errors |
| Space Wobble Bass | Bass | -5.8 / -5.8 | -1.4 / -1.4 | +7.2 / +7.2 | the spread of its own errors |
| Harmonic Sine Bass | Bass | -7.1 / -7.1 | +0.3 / +0.3 | +7.0 / +7.0 | the spread of its own errors |
| Click Attack Bass | Bass | -6.1 / -6.4 | -0.9 / -0.9 | +7.1 / +7.1 | the spread of its own errors |
| Fat Reese | Bass | -5.1 / -4.8 | -1.9 / -2.2 | +7.1 / +6.9 | the spread of its own errors |
| Diode Saturated Bass | Bass | -6.9 / -6.9 | +0.5 / +0.5 | +6.4 / +6.4 | the spread of its own errors |
| Sonar Ping | FX | -6.8 / -5.1 | -0.2 / +0.0 | +6.1 / +5.9 | the spread of its own errors |
| Custom Shape Wobble | Bass | -4.3 / -4.3 | -2.4 / -2.4 | +6.6 / +6.6 | the spread of its own errors |
| Diode Bite Lead | Lead | -6.5 / -6.5 | +0.5 / +0.5 | +5.9 / +5.9 | the spread of its own errors |
| Cosmic Teleport | FX | -6.5 / -6.5 | +0.0 / +0.0 | +6.3 / +6.3 | the spread of its own errors |
| Solid Sub | Bass | -6.1 / -6.1 | +0.4 / +0.4 | +5.6 / +5.6 | the spread of its own errors |
| Twin Warp Lead | Lead | -3.9 / -4.0 | -2.3 / -2.3 | +6.1 / +6.1 | the spread of its own errors |
| Dual Octave Hero | Lead | -3.2 / -3.2 | -2.9 / -2.9 | +6.0 / +6.0 | the spread of its own errors |
| Formant Vowel Bass | Bass | -5.7 / -5.7 | -0.4 / -0.4 | +5.9 / +5.9 | the spread of its own errors |
| Laser Beam Lead | Lead | -3.0 / -3.0 | -2.4 / -2.4 | +5.5 / +5.5 | the spread of its own errors |
| Subterranean Duo | Bass | -1.9 / -1.9 | -3.7 / -3.7 | +5.5 / +5.5 | the spread of its own errors |
| Metal Gong Strike | FX | -5.1 / -2.3 | -0.1 / +0.1 | +3.6 / +3.7 | the spread of its own errors |
| Phaser Sweep Lead | Lead | -5.1 / -3.0 | -0.4 / -0.2 | +4.6 / +4.3 | the spread of its own errors |
| Pulse Width Mod Lead | Lead | +4.5 / +4.3 | -5.1 / -5.1 | -0.3 / -0.3 | raw peak |
| Pitch Drop 808 | Bass | +5.0 / +5.0 | -5.0 / -5.0 | -3.6 / -3.6 | raw peak |
| Solo Square Lead | Lead | -4.9 / -4.9 | +2.7 / +2.7 | +2.1 / +2.1 | the spread of its own errors |
| Fast Arp Bass | Bass | -4.0 / -4.0 | -4.2 / -4.2 | +4.9 / +4.9 | raw peak |
| FM Digital Lead | Lead | -4.2 / -4.2 | -0.7 / -0.7 | +4.8 / +4.8 | the spread of its own errors |
| MPE Pressure Lead | Lead | -3.5 / -2.5 | -1.5 / -1.7 | +4.6 / +4.7 | the spread of its own errors |
| Sci-Fi Laser Sweep | FX | -4.4 / -4.4 | -0.2 / -0.2 | +4.7 / +4.7 | the spread of its own errors |
| Sync Growl Bass | Bass | -3.0 / -3.0 | -1.8 / -1.8 | +4.7 / +4.7 | the spread of its own errors |
| Hyper Dimension Pad | Pad | -4.7 / -1.8 | +2.2 / -1.1 | +4.2 / +1.1 | the spread of its own errors |
| Dirty Peak Lead | Lead | -3.5 / -3.5 | -1.3 / -1.3 | +4.7 / +4.7 | the spread of its own errors |
| Detonation Tail | FX | +4.6 / +4.1 | -1.5 / -1.5 | -2.9 / -2.9 | the spread of its own errors |
| Ladder FM Bass | Bass | -2.6 / -2.6 | -1.8 / -1.8 | +4.5 / +4.5 | the spread of its own errors |
| Throat Singer Drone | Pad | +3.2 / +4.5 | -3.1 / -2.6 | -1.1 / -1.1 | the spread of its own errors |
| Tri Soft Solo | Lead | -4.2 / -2.7 | -1.0 / -0.8 | +4.5 / +4.1 | the spread of its own errors |
| Alien Communication | FX | -4.2 / -4.2 | -0.1 / -0.1 | +4.3 / +4.3 | the spread of its own errors |
| Triple Fifth Lead | Lead | +2.3 / +2.1 | -4.2 / -4.0 | +1.7 / +2.0 | the spread of its own errors |
| Bell Chime Lead | Lead | -0.9 / -0.9 | -3.2 / -3.2 | +4.2 / +4.2 | the spread of its own errors |
| Vowel Scrub Choir | Lead | +1.3 / +0.9 | -4.1 / -3.9 | +2.7 / +3.3 | the spread of its own errors |
| Harmonic Shimmer Lead | Lead | -0.8 / -0.8 | -3.3 / -3.3 | +4.0 / +4.0 | the spread of its own errors |
| Sync Cry Lead | Lead | +0.5 / +1.1 | -4.0 / -3.9 | +3.0 / +3.1 | the spread of its own errors |
| Chord Cloud Pad | Pad | +2.4 / +2.3 | -4.0 / -2.3 | +0.6 / +1.2 | the spread of its own errors |
| Cosmic Drift Lead | Lead | -3.9 / +1.5 | -2.3 / -1.7 | +3.3 / +3.2 | the spread of its own errors |
| Hyper Saw Lead | Lead | -1.6 / +0.9 | -3.4 / -3.4 | +3.9 / +3.5 | the spread of its own errors |
| Vocaloid Lead | Lead | -3.4 / -3.4 | -0.5 / -0.5 | +3.8 / +3.8 | the spread of its own errors |
| Chorused Saw Solo | Lead | +0.3 / +0.8 | -3.8 / -3.6 | +3.1 / +3.3 | the spread of its own errors |
| Warm Vintage Lead | Lead | +0.5 / +0.6 | -3.7 / -3.7 | +3.2 / +3.2 | the spread of its own errors |
| Metallic Edge Lead | Lead | +1.4 / +1.4 | -3.6 / -3.6 | +2.1 / +2.1 | the spread of its own errors |
| Velvet Strings | Pad | +2.2 / -0.6 | -3.6 / -3.5 | +0.3 / +0.4 | raw peak |
| Vintage Brass Pad | Pad | +1.6 / +1.4 | -3.5 / -3.6 | +0.3 / -0.0 | raw peak |
| Bell FM Ripple Lead | Lead | +0.2 / -0.2 | -3.5 / -1.3 | +1.5 / +2.1 | raw peak |
| Ethereal Glass | Pad | +0.5 / +2.4 | -3.4 / -3.4 | -0.6 / -0.3 | raw peak |
| Gong Grain Swarm | FX | +0.8 / +1.4 | -3.4 / -2.2 | +1.1 / +2.3 | the spread of its own errors |
| Bend Formant Growl | Bass | -3.0 / -2.3 | -0.5 / -0.5 | +3.4 / +3.1 | the spread of its own errors |
| Harmonic Cloud | Pad | -0.3 / +2.8 | -3.3 / -3.0 | -1.3 / -1.2 | raw peak |
| Bode Shifter Drone | FX | -3.3 / +3.0 | -2.2 / -2.6 | +2.9 / +2.2 | the spread of its own errors |
| Vinyl Dust Crackle | FX | -1.9 / -1.8 | -1.3 / -1.4 | +3.3 / +3.3 | the spread of its own errors |
| Reese Dual Phase Bass | Bass | -2.2 / -1.3 | -1.5 / -0.9 | +3.1 / +3.0 | the spread of its own errors |
| Golden Dawn Pad | Pad | -3.1 / -1.0 | -0.1 / +0.4 | +1.9 / +1.7 | the spread of its own errors |
| Cathedral Choir | Pad | -1.8 / -0.3 | -3.0 / -2.0 | -0.1 / +1.1 | raw peak |

Limiter, four note chord velocity 127 (ours, 36 48 55 60, 3 s): within the budget (at most 3 dB, at most 25 % of the blocks) on 318 of 320 renders (99.4 %); deepest reduction median 0.00, max 2.32 dB; share of blocks median 0.0 %, p90 3.9 %, max 34.8 %.
Limiter, four note chord velocity 127 (stock voicing, 48 55 60 64, 3 s): within the budget (at most 3 dB, at most 25 % of the blocks) on 318 of 320 renders (99.4 %); deepest reduction median 0.00, max 2.04 dB; share of blocks median 0.0 %, p90 3.6 %, max 34.5 %.
Six note chord (36 43 48 55 60 64, velocity 127): the limiter acts in 144 of 320 renders; deepest reduction median 0.00, max 2.42 dB; share of blocks median 0.0 %, max 99.1 %.
Highest sample the host receives on any of these gestures: -0.50 dBFS (the ceiling).
Raw engine peaks: max +2.92 dBFS (Pitch Drop 808); above +2.92 dBFS (1.4): 0; above the knee +5.11 dBFS (1.8): 0.
Key 61 against XL, sustained chord velocity 127: median difference 0.04 dB; more than 1 dB apart: 17 presets: Chime Drop (-64.3 / -45.7), Celesta Bells (-51.6 / -40.6), Marimba Layer Keys (-40.7 / -37.5), Hyper Dimension Pad (-10.8 / -13.9), Karplus Harp (-44.5 / -41.4), Lush Analog Pad (-18.3 / -15.7), Harp Drops (-40.8 / -39.1), Ring Mod Gamelan (-35.7 / -34.2), Breath of Machines (-21.6 / -23.2), Dream Grand Piano (-30.6 / -29.2), Cathedral Choir (-15.1 / -13.9), Breakbeat Slice Keys (-26.7 / -27.9), Gong Grain Swarm (-18.9 / -17.7), Infinite Horizon (-21.9 / -20.8), Glass Ping Pluck (-33.6 / -32.5), Breath Flute Lead (-18.1 / -17.1), Stretched Glass Choir (-14.3 / -13.3)

## Presets held back by the limits

The test allows these to miss their loudness (the mean of their errors) by more than 3 dB; all are louder at their peaks than the stock medians allow for their sustained level (at -1.0 dB):

| preset | category | mean error | held back by |
|---|---|---|---|
| 808 Boom | Bass | -16.1 | raw peak (a boom that decays: its 0.3 .. 2 s level) |
| Punchy Pluck Bass | Bass | -18.1 | raw peak (decaying) |
| EMS Squelch Bass | Bass | -8.3 | raw peak |
| Plucked Lead Tone | Lead | -3.7 | raw peak |
| Shepard Ascent | Pad | -9.4 | raw peak (an evolving pad) |
| Lorenz Garden | Pad | -7.6 | raw peak |
| Infinite Horizon | Pad | -5.2 | raw peak (its reverb, absent on the Key 61, carries the XL peak) |
| Sub Drop Boom | FX | -9.1 | raw peak (decaying) |
| Breath of Machines | FX | -4.5 | raw peak |
| Game Over Zap | FX | -23.5 | the limiter budget and the raw peak (a short zap: nothing left in the window) |
| Bode Barber Riser | FX | -3.4 | the top of `master.volume` (a riser) |

Lush Analog Pad lies at 2.6 dB. The single windows (each error within 3 dB) hold for 503 of 768 checks: the stock instruments' note / chord / sustain ratios differ from ours (next section).

## Chord against note

Our four note chord peaks 9 to 10 dB above our note (raw, both voicings alike: ours minus the stock voicing is 0.0 to 0.1 dB in peak, 0.1 to 0.6 dB in sustained level); the stock medians: 2 to 8 dB. The difference is made at the strike: in the first 100 ms our chord already lies 8.6 to 9.4 dB above the note (all four voices start together, their first crests add: four equal crests are +12 dB). Our sustained chord lies about 6 dB above our note (four uncorrelated voices: +6 dB), the stock one 0 to 5 dB. **No cheap engine-side cure was found**: with `osc1..3.random_phase` = 100 on ten presets the strike difference fell from 10.7 to 9.8 dB (median) and the 3 s peak from 11.1 to 10.6 dB: over a held chord the relative phases of four different pitches drift, so the crests line up within seconds whatever the start phase. The stock figures suggest a gain per voice that falls with the number of voices, or a meter that does not read sample peaks: a proposal for the owner, not changed (it would change the balance of every chord against single notes).

## Velocity

Velocity 127 against 100 (the table above, "v127-v100"): Bass 0.2 dB (stock 0.1), Lead 0.0 (-0.9), Pad 2.9 (2.8 note, 3.3 chord), Pluck 3.9 (3.7 / 3.6), FX 1.8 (1.0 / 3.0): now within about 1 dB of the stock. **Keys 2.7 dB against the stock pianos' 8.1** (owner item): the parameter cannot reach it (100 percent gives -4.1 dB); it needs a steeper velocity curve in the engine or a velocity-to-level modulation per preset.

## Checks

- `synthyboy_test_output_stage_presets` (`core/tests/test_output_stage_presets.cpp`): `output_stage_on_every_factory_preset` (no sample above the ceiling at make-up gains -2.5 .. 15 dB, no loudness inversion: pass) and `output_stage_presets_sit_at_the_stock_level` (every preset's mean error within 3 dB but the 11 held back; the limiter budget on both four note chords held on 636 of 640 renders, at least 90 percent required; no sample above the ceiling): pass.
- `synthyboy_preset_render --engine own --summary-only`, tiers low, medium, high (the gate `synthyboy_preset_sanity` with the own engine): PASS, 0 failures.
- `gen-factory-presets.py --check`, `check-presets.py` (0 errors, the same 79 warnings as before), the preset pytest files, `test_docs_release.py`, `test_docs_mpc.py`: pass.
