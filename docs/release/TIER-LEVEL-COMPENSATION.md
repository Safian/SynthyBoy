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
5. Program: the models read the program at the place of the bypassed effect (a control rate meter: band shares, width, steadiness, section 8); with no signal seen yet they use the standard program (a power spectrum falling as f^-1.5 from 30 Hz to 12 kHz, channels sharing 70 % of their power).
6. Equal power effects (reverb, flanger): `cos^2 + sin^2 W`, W the power of the wet path alone (calibration tables of `scripts/tier-makeup-calibrate.py` from the bench `synthyboy_fx_makeup_calibrate`: reverb by type / room / decay / size, feedback scaling the decay of types 4 and 5, curves for low cut, high cut, shelves and width; flanger by feedback and centre; the table values of long decays are limited to 12.8 s). Hyper / Dimension: the table of the whole module by unison, hyper mix and dim mix (its mix is linear and Dimension adds on top). Bode: linear mix, wet power by feed, a shift under 20 Hz counts as coherent (a comb), mono input x 0.85.
7. Convolve: the data of the loaded impulse. A helper thread (the same worker as the convolver build, never the audio thread) shapes the impulse exactly as the real build does (`Convolve::shapedIr`: size, attack, decay, damp, phi MIN, IR GAIN, law) and stores per sixth of an octave the mean power and the mean complex gain of the impulse (`BandSpectrum`). `convolvePower` adds dry and wet coherently per band with the mix law, the tone filter and the pre delay (phase and loss of coherence by the width of the band), weighted by the program spectrum. Offline renders (`Quality::realtime` false) wait for the thread, so the output stays deterministic; live, the module counts as 1 until the first spectrum is there.
8. Time smear: a reverb tail is not at its steady state power in the loudest 100 ms of a played note, so the wet power of the reverb is scaled by 0.4 + 0.6 x steadiness^4 (section 8); Hyper carries a bias factor 0.91 (the bias of its model over 36 programs); Flanger, Bode and Convolve 1.0 (`fx_makeup.h`).
9. State and versions: no new preset parameter, nothing in the saved state; old presets and projects load unchanged. `Quality` gets one field at its end (positional initialisers are unchanged).
10. Chain order and racks: a module on a bus or in the band of a splitter gets its gain there; a module whose splitter is bypassed gets none.

Not covered: the Vital adapter engine (`core/engine-vital`, not built for the MPC); effects other than the five (no model, so no gain: only the five can be masked by a profile today, `fx=low` / `fx=all`).

## 3. Calibration data and the script

`scripts/tier-makeup-calibrate.py --bin build/bench/synthyboy_fx_makeup_calibrate --out core/engine-own/fx/fx_makeup_tables.h` runs the bench on the grids and regenerates `fx_makeup_tables.h` (about 20 s). The curves of the reverb filters (low cut, high cut, shelves) are the means of three measurements (types 0, 2, 4) listed in `fx_makeup.cpp`; they are the ones measured with this bench in this branch (low cut 100 Hz -3.0 dB, 1 kHz -11.4 dB, 4 kHz -23.1 dB against 20 Hz).

## 4. Result (all 160 presets, `scripts/measure-loudness.py`, Key 61 minus XL, LU)

State of the tree `feature/mpc-overlay` (meter, band models, bus makeup; NO unison cap makeup, it was reverted: section 10), measured again in integration round 10 on the merged tree (`.hw-local/mpc-perf/round10/loudness-all160.csv` has every preset; the XL column of round 10 equals round 9 within 4e-6 LU for all 160). `before-after.csv` in the results folder of the branch is the state with the unison makeup, which the tree does not have.

| Preset | Before | After |
|---|---|---|
| Reese Dual Phase Bass | -7.13 | -1.66 (Convolve -0.79 effect, unison cap -0.85) |
| Dusty Rhodes Keys | -5.38 | -0.44 |
| Bode Barber Riser | +3.57 | +0.95 |
| Hyper Dimension Pad | +3.46 | +0.56 |
| Bode Shifter Drone | +3.29 | -0.14 |
| Cathedral Choir | +2.54 | +0.43 |
| Lush Analog Pad | -2.51 | -2.83 (unison cap, no makeup for it) |
| Frozen Pluck Drone | +2.27 | +0.52 (bus makeup) |
| Marimba Layer Keys | -2.08 | -0.15 |

All 160 (round 10, the tree as it is): maximum 7.13 to 2.83 LU, RMS 1.00 to 0.45 LU, presets over 1 LU 13 to 5, over 2 LU 9 to 2. (With the unison makeup of section 10 the branch had maximum 2.03, RMS 0.42, 6 over 1 LU, 1 over 2 LU.) The XL column is identical to the last digit for all 160. Effects only (profile `low;unison=16`, no unison cap in force, minus XL): maximum 1.87, RMS 0.32, 2 presets over 1 LU (before: maximum 6.28, RMS 0.96 as `low` minus `low;fx=all`).

Still over 1 LU, and exactly why:

- **Lush Analog Pad (-2.83)** and **Infinite Horizon (-2.23)**: the unison cap (6 and 7 voices under 3). Without the cap (`unison=16`, measured on the branch) Lush Analog Pad is at +0.16 and Infinite Horizon at -0.40. The effect of the cap on a single preset is the chance of the beating in its loudest 100 ms (Velvet Strings, the same settings as Lush Analog Pad, loses 0.20). No setting based gain predicts that and the unison makeup that was tried made single presets worse: section 10 (not in the code).
- **Hyper Saw Lead (+0.55)**: unison cap, the other way (7 + 7 voices): the same chance; without the unison makeup it is back near +0.5 (with it +1.52).
- **Reese Dual Phase Bass (-1.66)**: Convolve -0.79 (the cabinet impulse on a bass: the model is -0.8 dB under the measurement) and the unison cap -0.85.
- **Cosmic Drift Lead (-1.69)**: reverb mix 50 on a lead (-1.87 effects only, the reverb makes this lead LOUDER, which the models do not see) and the unison cap.
- **Sonar Ping (+1.35)**: reverb mix 25 on a percussive FX: the reverb takes 1.68 dB, the model 0.3; the steadiness follower does not see it (section 8).

"None worse than before" does not hold literally: a makeup that is right on average exposes non effect residuals that an opposite effect residual hid.

## 5. Proof that the full quality output does not change

`core/tests/test_own_fx_makeup.cpp`: the output of five patches (reverb, flanger, Hyper, Bode and the heavy patch of the bench) with `fxMakeup` on and off, memcmp equal; a bypassed effect that is off in the preset changes nothing. Over the lab: all 160 presets under the XL profile give `fast_max` identical to the last digit before and after (the five gestures, 160 x about 6 renders = about 1000 renders compared through the loudness figure).

## 6. Cost

Per sample, per bypassed effect with a model and under a mask only: the ramp (2 multiplies and an add) and the meter (about 30 operations: four one pole splits, five squares, the side), so at most five times that. A 30 s render of an 8 note preset with a bypassed reverb and Hyper takes 0.31 s with and without the makeup on the Mac (the difference is below the timer); the Key 61 figure is for the integration run. Per control period: the cached models cost about 15 compares; a change of a parameter recomputes a table interpolation (tens of operations) or, for Convolve, the 54 band sum (a few hundred operations, only when the mix, tone or pre delay moved). A new impulse or a new shape key costs one shaping and one FFT of up to 2^21 points on the helper thread (the same work as a convolver build without the partitions). Not measured on the device (no device access in this task); the Key 61 numbers have to come from the integration run (`synthyboy_tier_bench --set ladder`).

## 7. Run list for the integration

Build `synthyboy_sound_lab`, `synthyboy_test_own_fx_makeup`, `synthyboy_test_own_profile`, `synthyboy_test_own_fx_mute`, `synthyboy_fx_makeup_calibrate`; run the three tests; `scripts/measure-loudness.py measure --profile xl` and `--profile low` and compare with `before-after.csv`; one `synthyboy_tier_bench` run of Cathedral Choir and Reese on the Key 61 for the CPU figure.

## 8. The program meter and the band models (second round)

The first round used one fixed program (a power spectrum falling as f^-1.5, 70 % shared stereo power) for every model. Measured through the real modules (`scripts/tier-makeup-programs.py`, the bench `synthyboy_fx_makeup_calibrate`) the gain of an effect depends on the program: Hyper on the same settings loses 2 to 3 dB more on a wide input than on a mono one; the flanger gives +0.6 dB on noise and -6.8 dB on a low sine; the reverb reaches its steady state power only for a held sound. A fixed program cannot serve both within 1 dB, so the models read the program.

**What decides it.** (1) The energy per band (the tilt of the spectrum) and (2) the width (side against mid) decide Hyper and Bode, and the shape of the flanger on broadband input. (3) How sustained the program is decides the reverb. A tone against a comb (a flanger on a pure sine) is a fourth property that no control rate measure captures: see the limits below.

**The meter** (`makeup::ProgramMeter`, `fx_makeup.cpp`) sits at the place of every bypassed effect and sees the input of the gain. Per sample it takes the mid signal through four one pole splits (120, 480, 1900, 7600 Hz) and sums the squares of the five bands and of the side (about 30 operations); once per control period it folds the sums into energies with a 100 ms time constant, and into a slow mean (1 s) and a held peak (4 s) for the steadiness. The descriptor is: the K-weighted share of each band (the BS.1770 shelf above 1.7 kHz, weights 1, 1, 1, 1.35, 2.1), the width (`2 side / (mid + side)`, 0 mono, 1 independent channels) and the steadiness (slow mean over held peak, 1 for a held note, near 0 for a pluck). The cost is paid only while an effect is bypassed and has a model. This is the one control rate follower of the makeup; the audio path keeps one multiply.

**The models** use the descriptor. For every cell of an effect's parameters, ten gains (five bands, for a mono and for a wide input) were fitted by non negative least squares on the gain measured through the real module on 27 synthetic programs (saw, square and sine chords in three registers, a pluck, a slow attack pad, noise of several tilts and of five bands, mono and wide) and 12 steady renders of the engine (`scripts/tier-makeup-dry.py` makes dry renders of factory presets, the steady ones come from `OwnEngine` with one oscillator), loudest 100 ms, K-weighted (`scripts/tier-makeup-regress.py`, `scripts/tier-makeup-calibrate.py`). The model gain is the sum over the bands of descriptor share x cell gain. Flanger: cells by feedback and centre (equal power mix on top). Hyper: cells by unison, hyper mix and dim mix (the whole module, its mix is linear and Dimension adds on top). Bode: cells by feed, shifted or not (linear mix, a shift under 2 Hz coherent, over 20 Hz not). Reverb: the table of the first round for the wet power, and the share of it that the loudest 100 ms carries is `0.4 + 0.6 x steadiness^4` (the factory gestures about 0.4, a held note towards 1.0). Convolve: the weights of its 54 bands come from the meter (the share of the meter band, spread by the standard program), so a bass and a bell see the same impulse differently.

**Results.** See section 11 (model against measurement on every program, loudest 100 ms and plain RMS).

**What was tried and did not work.** (a) Fitting the band gains also on 24 real gesture renders (factory presets with every effect off, with pauses and decays) made the fit worse (mean fit error 1.21 dB against 0.59 dB): the loudest 100 ms of a gesture is not a function of the band shares alone. The gestures are therefore used to validate, not to train; the training set is 27 synthetic programs and 12 steady renders of the engine (saw and wavetable chords in five registers, one and three unison voices). (b) The steadiness follower for the reverb: on the 62 factory presets with a reverb the error of the reverb makeup is RMS 0.51 dB for a constant 0.4, 0.51 for 0.25 + 0.75 x steadiness, 0.51 for 0.5 - 0.2 x steadiness (inverted), 0.63 for 0.25 + 0.75 x steadiness with a steady state of 1.0: the gesture of a factory preset says nothing that the follower sees about how much of the reverb the loudest 100 ms carries. Sonar Ping (+1.35) and Cosmic Drift Lead (-1.87) are not fixed by it. It is kept because it does fix a held sound (the test: held saw chords, reverb mix 70, -1.75 dB with a constant 0.4, -0.10 dB with the follower), and the steepness (power 4) keeps the factory gestures (steadiness 0.12 to 0.66, median 0.47) at about 0.4.

**Limits that no meter fixes.** The flanger is a comb whose teeth sweep with an LFO. On a pure low sine (131 to 196 Hz) its gain at mix 100 is -6.8 dB (RMS), -5.0 (loudest 100 ms), on a saw chord -2.0, on noise +0.6: what decides it is the position of the partials against the teeth, not the band energy. A model that knew it would need the spectrum at a resolution of a few Hz. At the mix of the factory presets (30) and on harmonic programs the spread is under 2 dB.

## 9. Bypassed effect on a bus beside a dry copy of its source

Routings that qualify: a source (an oscillator, the sub, the noise, a filter) that plays in Main and sends a copy to Bus 1 or Bus 2 (`oscN.bus1_send`, `.bus2_send`, `noise.`, `sub.`, `filterN.`) whose bus rack holds a bypassed effect with a model. Frozen Pluck Drone is the case: oscillator 1 plays in Main and sends 55 % to Bus 1, where a Convolve with mix 100 is the only module. With the effect active the bus carries the source filtered by the impulse, unrelated to the dry copy in Main; bypassed, the bus carries the copy itself, which adds up with Main in amplitude (up to +6 dB). A source with the bus as its destination instead of Main (no dry copy) does not qualify; neither does a bus that nothing else shares.

The engine decides from the signals, not from the routing table: before the racks it measures, over 100 ms, the power of Main (Pm), of the bus input (Pb) and their cross term (C). With Pmod and A the power gain and the coherent amplitude of the bypassed effects of the rack (their product, the level trims included; A is the dry part of the mix, for Convolve the real part of the impulse spectrum per band), the active case adds Pb Pmod + 2 A C to Pm. The bypassed bus is multiplied by G that gives the same: G^2 Pb + 2 G C = Pb Pmod + 2 A C, so G = (-C + sqrt (C^2 + Pb (Pb Pmod + 2 A C))) / Pb. With C = 0 this is sqrt (Pmod), the gain per effect of the first round. G is applied at the bus output (after the rack and the bus level, one-pole smoothed 20 ms), the effects of the rack itself run as before. The gain of a bypassed effect on Main or in the band of a splitter stays at its place in the chain; on a bus it is applied at the output of the bus (an effect after the bypassed one on the same bus sees the level without it).

Test: `tier_makeup_bus_copy_of_main` (osc 1 sends 55 % to Bus 1, reverb mix 100 on Bus 1): bypassed +3.08 dB against the active case, with the makeup +0.15 dB. Frozen Pluck Drone: +1.44 LU with the gain per effect alone, +0.52 with the bus makeup.

## 10. Unison cap makeup (designed and measured, NOT in the code)

Cause (measured): the cap of tier `low` (3 voices) takes voices away from presets that ask for more. The blend law normalises the summed power of the voices, so the expected power does not change; what changes is the loudest 100 ms: more voices at a small detune beat with higher coherent peaks. Measured on all 160 presets (`low;fx=all` against `low;fx=all;unison=16`, loudest 100 ms), 16 presets ask for more than 3 voices: the change runs from -2.73 (Lush Analog Pad, 6 + 6 voices) to +0.54 (Hyper Saw Lead), mean -0.6. It does not follow the settings: Velvet Strings (6 voices, detune 25, spread 100) changes by -0.20, Lush Analog Pad (6, 28 and 32, 100) by -2.73. A gain by the number of voices w x level^2 x n^alpha fits at best alpha = 0.3 (RMS error 0.78 dB against 0.94 dB for no makeup); detune, blend and spread do not improve it: the summed power under the blend law is the same, the rest is the chance of the beating in the loudest 100 ms of the gesture.

Design (it was implemented in commit 62fb831f and reverted again by the coordinator before the merge into `feature/mpc-overlay`; the tree has no unison makeup, `Quality::maxUnison` only caps the voices): per oscillator, the gain (wanted / cap) ^ 0.15 in amplitude (0.3 in power) on the oscillator level when the cap is in force and the preset asks for more voices than it allows; wanted = the voices of the preset after modulation, cap = `Quality::maxUnison` (and the low tier cap), computed once per block into a table of 17 entries and read in the voice tick where the level is set. With no cap in force or no oscillator over the cap the path was bit equal to the path without it (tested). It corrected the mean (+0.9 dB for 6 voices under 3; over the 48 presets with a cap effect: mean -0.21 to 0.00 LU, RMS 0.56 to 0.49) but not the spread, which is chance.

Why it is not in the code: it made single presets worse. Hyper Saw Lead (7 + 7 voices) has a cap effect of +0.54 LU without the makeup and +1.18 with it (Key 61 minus XL +1.52 in total, over the 1 LU line), and Lush Analog Pad and Infinite Horizon, whose cap effect is large by chance (-2.73 and -1.54 LU), keep most of their difference (Infinite Horizon is not moved by the gain at all). A gain that fixes the mean and moves a preset the wrong way by chance is not a better sound; the decision of the coordinator is to leave the cap without a gain. The numbers of the "with the makeup" columns of section 4 below that were measured with it are marked as such; the tree as it is has the figures of the round 10 table in section 4.

## 11. Model against measurement (second round)

`scripts/tier-makeup-validate.py`: the model of the engine (`FxModule::makeupPower`, fed by the meter on the same input) against the gain measured through the real module, on 36 programs: 12 steady engine renders and 24 factory preset gestures with every effect off (`.hw-local/.../all-programs`). "Loudest 100 ms" is the figure of the factory levels (K-weighted), "RMS" the plain RMS of the last 6 s. Error = model minus measurement, dB: maximum / RMS over the 36 programs / bias.

| Effect and settings | Loudest 100 ms | RMS |
|---|---|---|
| Flanger mix 30, feedback 45 (the factory setting) | 1.70 / 0.70 / -0.23 | 0.95 / 0.43 / +0.23 |
| Flanger mix 50, feedback 25 | 2.10 / 0.92 / -0.33 | 1.54 / 0.58 / +0.27 |
| Flanger mix 50, feedback 0, centre 1 kHz | 2.29 / 0.68 / +0.32 | 2.84 / 0.83 / +0.45 |
| Flanger mix 80, feedback 70 | 3.50 / 1.72 / +0.11 | 3.81 / 2.07 / +1.80 |
| Flanger mix 100, feedback -50, centre 2 kHz | 0.52 / 0.17 / -0.02 | 0.52 / 0.16 / -0.01 |
| Hyper mix 60, unison 5, dim 40 (factory pad) | 2.14 / 1.13 / +0.02 | 1.93 / 1.17 / +1.04 |
| Hyper mix 100, unison 4 | 2.48 / 0.92 / -0.03 | 2.13 / 1.20 / +1.09 |
| Hyper mix 50, unison 3, dim 0 | 2.16 / 0.94 / -0.58 | 0.89 / 0.50 / +0.34 |
| Hyper mix 25, unison 7, dim 100 | 2.05 / 0.81 / -0.43 | 1.12 / 0.62 / -0.19 |
| Bode mix 60, shift 25 % of 400 Hz, feed 40 (factory) | 0.62 / 0.23 / +0.03 | 1.03 / 0.36 / +0.20 |
| Bode mix 100, shift 50 % of 400 Hz, feed 40 | 0.80 / 0.28 / -0.12 | 1.43 / 0.41 / +0.24 |
| Bode mix 40, shift 100 % of 2 kHz, feed 0 | 1.38 / 1.03 / -0.99 | 0.34 / 0.10 / +0.05 |
| Bode mix 60, shift 0, feed 60 | 1.53 / 0.68 / +0.41 | 1.77 / 0.88 / +0.49 |

What it says. Bode is within 1 dB RMS everywhere and within 1.5 dB at worst, except that a shift of kilohertz moves energy into the K-weighted treble, which the tables (fitted at a shift of 100 Hz) do not know: the loudness is 1 dB higher than the model, the RMS matches. Hyper and Flanger meet 1 dB in RMS error over the programs for most settings but not everywhere: at the loudest 100 ms the worst program is 2 to 2.5 dB off for Hyper (a wide, detuned input such as a brass swell or a bell loses 2 dB more than the band shares say) and, for the flanger, 3.5 dB at mix 80 with feedback 70 (the feedback resonance picks single partials of a tonal program, section 8), 1.7 dB at the factory setting. The unit tests (`test_own_fx_makeup`) hold every model to 1 dB on sustained saw notes through the engine: reverb -0.10, flanger +0.83 (mix 50, feedback 25), Hyper +0.61, Bode +0.19 dB, loudest 100 ms. The coordinator's target of 1 dB for every model on every program is not reached for Hyper and the flanger; the reason is not a missing band: it is the dependence on the partials and on the detune of the input, which the meter does not see.
