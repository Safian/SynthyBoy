# Tier level compensation

Branch `work/tier-makeup`. Measured with the sound lab (`scripts/measure-loudness.py`, the loudest 100 ms of the gesture of the preset, K-weighted LUFS, "LU" below). Nothing here was heard. Data: `.hw-local/mpc-perf/round9-measure/tier-makeup/` (outside the repository).

## 1. Cause of the differences (before the change)

Method: `scripts/tier-makeup-attribution.py`. The XL profile (`high;voices=16;control=economy`), `low`, `low;fx=all` (every other setting of `low`, no effect bypassed) and the XL profile with exactly one effect bypassed (the lab key `fxoff=NAME`). Figures are the change of the loudest 100 ms against the XL profile, LU.

| Preset | Key 61 (low) | low, fx=all (not effects) | Reverb | Flanger | Hyper | Bode | Convolve | Cause |
|---|---|---|---|---|---|---|---|---|
| Reese Dual Phase Bass | -7.13 | -0.85 | 0 | 0 | 0 | 0 | -6.26 | Convolve (mix 55), the rest the unison cap |
| Dusty Rhodes Keys | -5.38 | 0 | 0 | 0 | 0 | 0 | -5.38 | Convolve (mix 85) |
| Bode Barber Riser | +3.57 | -0.02 | +0.72 | +0.07 | 0 | +2.76 | 0 | Bode, reverb |
| Hyper Dimension Pad | +3.46 | 0 | +0.54 | 0 | +3.26 | 0 | 0 | Hyper / Dimension |
| Bode Shifter Drone | +3.29 | 0 | +0.08 | 0 | 0 | +3.44 | 0 | Bode |
| Cathedral Choir | +2.54 | 0 | +2.54 | 0 | 0 | 0 | 0 | Reverb (mix 70) |
| Lush Analog Pad | -2.51 | -2.73 | +0.48 | 0 | 0 | 0 | 0 | Not an effect: the unison cap (6 voices to 3) |
| Frozen Pluck Drone | +2.27 | 0 | 0 | 0 | 0 | 0 | +2.27 | Convolve (mix 100 on bus 1) |
| Marimba Layer Keys | -2.08 | -0.01 | 0 | 0 | 0 | 0 | -2.08 | Convolve (mix 26) |

Non-effect part: only the unison cap matters. `low;fx=all;unison=16` removes the residual of Lush Analog Pad (-2.73), Infinite Horizon (-1.54) and Reese (-0.85) to 0.00; the oversampling (`samples=high`), the warp, morph, math and control groups and the voice count change none of them by more than 0.01 LU. Over all 160 presets the non-effect residual is within 1 LU except Lush Analog Pad and Infinite Horizon.

Sign: bypassing makes a sound LOUDER or QUIETER. The equal power mix (`cos` dry, `sin` wet) takes dry away; a wet path that is quieter than the dry (reverb, Hyper) makes the bypassed sound louder, a wet path with more energy at the notes' frequencies (Convolve with a low pass cabinet, mix 55 to 85) makes it quieter. Convolve adds wet and dry coherently (the same notes, a short impulse): `|cos a + sin a H(f)|^2` per frequency, not the sum of powers (checked: Reese +5.8 predicted from the cabinet impulse, +6.3 measured).

## 2. Design

1. `Quality::fxMakeup` (default true). The chain (`FxChain::update`) finds the modules that are ON in the preset, bypassed by the mask and in this rack (`bypassed[m]`, also pool slots). Nothing else changes: with no such module the plan and the signal path are the ones of before (bit equal, section 5).
2. The gain stands at the place of the bypassed effect: the plan keeps an item with the module's sort key, `runMakeup` multiplies the signal there (a linear ramp across the control period). The effect sits where the user put it, so what comes after it (compressor, distortion, EQ, buses) sees the level the effect would have given. The item adds no latency and is skipped by the idle logic.
3. `FxModule::makeupGain = level trim x sqrt (makeupPower)`: the Level key of the module is also bypassed, so it is part of the gain. `makeupPower` is the power the module gives the program against the bypassed module, from the module's own keys (including the global modulation), cached while the keys are the same (about 13 compares per control period).
4. Smoothing: the target is followed by a 20 ms one pole in the control period (at once with `quality.no_smoothing`); the first value of a bypassed module ramps from 1 across one control period. The gain is limited to -30 .. +24 dB.
5. Program: the models are for sustained music with a power spectrum falling as f^-1.5 from 30 Hz to 12 kHz (`fx_makeup.h`), stereo channels sharing 70 % of their power.
6. Equal power effects (reverb, flanger): `cos^2 + sin^2 W`, W the power of the wet path alone (calibration tables of `scripts/tier-makeup-calibrate.py` from the bench `synthyboy_fx_makeup_calibrate`: reverb by type / room / decay / size, feedback scaling the decay of types 4 and 5, curves for low cut, high cut, shelves and width; flanger by feedback and centre; the table values of long decays are limited to 12.8 s). Hyper / Dimension: the table of the whole module by unison, hyper mix and dim mix (its mix is linear and Dimension adds on top). Bode: linear mix, wet power by feed, a shift under 20 Hz counts as coherent (a comb), mono input x 0.85.
7. Convolve: the data of the loaded impulse. A helper thread (the same worker as the convolver build, never the audio thread) shapes the impulse exactly as the real build does (`Convolve::shapedIr`: size, attack, decay, damp, phi MIN, IR GAIN, law) and stores per sixth of an octave the mean power and the mean complex gain of the impulse (`BandSpectrum`). `convolvePower` adds dry and wet coherently per band with the mix law, the tone filter and the pre delay (phase and loss of coherence by the width of the band), weighted by the program spectrum. Offline renders (`Quality::realtime` false) wait for the thread, so the output stays deterministic; live, the module counts as 1 until the first spectrum is there.
8. Time smear: a reverb tail and a Convolve tail are not at their steady state power in the loudest 100 ms of a note, so the wet power of the reverb is scaled by `kReverbLoudness` = 0.4 and that of Bode by `kBodeLoudness` = 1.4 (`fx_makeup.h`), measured against the engine's own loudness figure (section 4); Flanger, Hyper and Convolve 1.0.
9. State and versions: no new preset parameter, nothing in the saved state; old presets and projects load unchanged. `Quality` gets one field at its end (positional initialisers are unchanged).
10. Chain order and racks: a module on a bus or in the band of a splitter gets its gain there; a module whose splitter is bypassed gets none.

Not covered: the Vital adapter engine (`core/engine-vital`, not built for the MPC); effects other than the five (no model, so no gain: only the five can be masked by a profile today, `fx=low` / `fx=all`).

## 3. Calibration data and the script

`scripts/tier-makeup-calibrate.py --bin build/bench/synthyboy_fx_makeup_calibrate --out core/engine-own/fx/fx_makeup_tables.h` runs the bench on the grids and regenerates `fx_makeup_tables.h` (about 20 s). The curves of the reverb filters (low cut, high cut, shelves) are the means of three measurements (types 0, 2, 4) listed in `fx_makeup.cpp`; they are the ones measured with this bench in this branch (low cut 100 Hz -3.0 dB, 1 kHz -11.4 dB, 4 kHz -23.1 dB against 20 Hz).

## 4. Result (all 160 presets, `scripts/measure-loudness.py`, Key 61 minus XL, LU)

| Preset | Before | After |
|---|---|---|
| Reese Dual Phase Bass | -7.13 | -1.74 (Convolve -0.89, unison cap -0.85) |
| Dusty Rhodes Keys | -5.38 | -0.10 |
| Bode Barber Riser | +3.57 | +0.60 |
| Hyper Dimension Pad | +3.46 | -0.30 |
| Bode Shifter Drone | +3.29 | 0.00 |
| Cathedral Choir | +2.54 | +0.32 |
| Lush Analog Pad | -2.51 | -2.85 (unison cap, not an effect) |
| Frozen Pluck Drone | +2.27 | +1.44 (see below) |
| Marimba Layer Keys | -2.08 | -0.87 |

All 160: maximum 7.13 to 2.85 LU, RMS 1.00 to 0.47 LU, presets over 1 LU 13 to 6, over 2 LU 9 to 2. The XL column is identical to the last digit for all 160 (`before-after.csv`). The effect only error (after minus the non-effect residual) has RMS 0.30 LU, maximum 1.44.

Still over 1 LU, and why:

- Lush Analog Pad (-2.85) and Infinite Horizon (-2.30): the unison cap of `low` (3 voices against 6 and 7), not an effect (`low;fx=all;unison=16` gives 0.00). General fix, not done here because it is in the oscillator code (another branch): the unison cap makeup, a gain by the number of capped voices (the energy of the unison blend of N and of the cap voices at the preset's detune, computed once per note on or per parameter change), at the oscillator level, same design rules as above. Reese keeps -0.85 for the same reason.
- Frozen Pluck Drone (+1.44): the wet only Convolve sits on bus 1 beside a dry copy of the same oscillator in the main path. Active, the two are not coherent; bypassed, the bus is the dry signal and adds coherently to the main one. A gain at the module's place can not remove a coherence that depends on the other path. General fix: a coherence aware makeup at the bus (the gain of the bus from the correlation of the bus content and the main path), not a per effect model.
- Sonar Ping (+1.35, reverb mix 25) and Cosmic Drift Lead (-1.95, reverb mix 50, of which the unison cap -0.86): the reverb makeup uses one program for every sound; a percussive FX and a lead react differently from a pad (the loudest 100 ms depends on the envelope). A makeup that follows the played signal would need a level measurement per sample, which this design leaves out on purpose.
- Marimba Layer Keys (-0.87) is inside 1 LU.

"None worse than before" does not hold literally: 23 presets move away from 0 by 0.1 to 1.7 LU. In every one of them the reverb makeup is right on average and the preset had a non-effect residual (unison cap) that an equal and opposite effect residual hid before. The tolerance of the effect only error is the 0.30 LU RMS above.

Calibration was done against the 160 presets and the sustained program; Hyper, Bode and Flanger have two factory presets each, so their factors are weakly fitted. On a tonal test (four sustained saw notes) the models leave: reverb -0.8 dB, flanger +0.6, Hyper -1.9 (of 5.5), Bode +1.2 (of 1.3) (`test_own_fx_makeup`). The calibration script reproduces the tables, not those two factors; they stay to be checked with the integration run.

## 5. Proof that the full quality output does not change

`core/tests/test_own_fx_makeup.cpp`: the output of five patches (reverb, flanger, Hyper, Bode and the heavy patch of the bench) with `fxMakeup` on and off, memcmp equal; a bypassed effect that is off in the preset changes nothing. Over the lab: all 160 presets under the XL profile give `fast_max` identical to the last digit before and after (the five gestures, 160 x about 6 renders = about 1000 renders compared through the loudness figure).

## 6. Cost

Per sample: one multiply by a ramp (2 multiplies and an add for the ramp) per bypassed effect with a gain, so at most five times that, only under a mask. Per control period: the cached models cost about 15 compares; a change of a parameter recomputes a table interpolation (tens of operations) or, for Convolve, the 54 band sum (a few hundred operations, only when the mix, tone or pre delay moved). A new impulse or a new shape key costs one shaping and one FFT of up to 2^21 points on the helper thread (the same work as a convolver build without the partitions). Not measured on the device (no device access in this task); the Key 61 numbers have to come from the integration run (`synthyboy_tier_bench --set ladder`).

## 7. Run list for the integration

Build `synthyboy_sound_lab`, `synthyboy_test_own_fx_makeup`, `synthyboy_test_own_profile`, `synthyboy_test_own_fx_mute`, `synthyboy_fx_makeup_calibrate`; run the three tests; `scripts/measure-loudness.py measure --profile xl` and `--profile low` and compare with `before-after.csv`; one `synthyboy_tier_bench` run of Cathedral Choir and Reese on the Key 61 for the CPU figure.
