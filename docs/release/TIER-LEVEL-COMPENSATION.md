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

## 8. The program meter and the band models (second round)

The first round used one fixed program (a power spectrum falling as f^-1.5, 70 % shared stereo power) for every model. Measured through the real modules (`scripts/tier-makeup-programs.py`, the bench `synthyboy_fx_makeup_calibrate`) the gain of an effect depends on the program: Hyper on the same settings loses 2 to 3 dB more on a wide input than on a mono one; the flanger gives +0.6 dB on noise and -6.8 dB on a low sine; the reverb reaches its steady state power only for a held sound. A fixed program cannot serve both within 1 dB, so the models read the program.

**What decides it.** (1) The energy per band (the tilt of the spectrum) and (2) the width (side against mid) decide Hyper and Bode, and the shape of the flanger on broadband input. (3) How sustained the program is decides the reverb. A tone against a comb (a flanger on a pure sine) is a fourth property that no control rate measure captures: see the limits below.

**The meter** (`makeup::ProgramMeter`, `fx_makeup.cpp`) sits at the place of every bypassed effect and sees the input of the gain. Per sample it takes the mid signal through four one pole splits (120, 480, 1900, 7600 Hz) and sums the squares of the five bands and of the side (about 30 operations); once per control period it folds the sums into energies with a 100 ms time constant, and into a slow mean (1 s) and a held peak (4 s) for the steadiness. The descriptor is: the K-weighted share of each band (the BS.1770 shelf above 1.7 kHz, weights 1, 1, 1, 1.35, 2.1), the width (`2 side / (mid + side)`, 0 mono, 1 independent channels) and the steadiness (slow mean over held peak, 1 for a held note, near 0 for a pluck). The cost is paid only while an effect is bypassed and has a model. This is the one control rate follower of the makeup; the audio path keeps one multiply.

**The models** use the descriptor. For every cell of an effect's parameters, ten gains (five bands, for a mono and for a wide input) were fitted by non negative least squares on the gain measured through the real module on 27 synthetic programs (saw, square and sine chords in three registers, a pluck, a slow attack pad, noise of several tilts and of five bands, mono and wide) and 24 real programs (dry renders of factory presets, `scripts/tier-makeup-dry.py`), loudest 100 ms, K-weighted (`scripts/tier-makeup-regress.py`, `scripts/tier-makeup-calibrate.py`). The model gain is the sum over the bands of descriptor share x cell gain. Flanger: cells by feedback and centre (equal power mix on top). Hyper: cells by unison, hyper mix and dim mix (the whole module, its mix is linear and Dimension adds on top). Bode: cells by feed, shifted or not (linear mix, a shift under 2 Hz coherent, over 20 Hz not). Reverb: the table of the first round for the wet power, and the share of it that the loudest 100 ms carries is `0.25 + 0.75 x steadiness` (a pluck 0.25, a held note 1.0). Convolve: the weights of its 54 bands come from the meter (the share of the meter band, spread by the standard program), so a bass and a bell see the same impulse differently.

**Results.** See section 11 (model against measurement on every program, loudest 100 ms and plain RMS).

**Limits that no meter fixes.** The flanger is a comb whose teeth sweep with an LFO. On a pure low sine (131 to 196 Hz) its gain at mix 100 is -6.8 dB (RMS), -5.0 (loudest 100 ms), on a saw chord -2.0, on noise +0.6: what decides it is the position of the partials against the teeth, not the band energy. A model that knew it would need the spectrum at a resolution of a few Hz. At the mix of the factory presets (30) and on harmonic programs the spread is under 2 dB.

## 9. Bypassed effect on a bus beside a dry copy of its source

Routings that qualify: a source (an oscillator, the sub, the noise, a filter) that plays in Main and sends a copy to Bus 1 or Bus 2 (`oscN.bus1_send`, `.bus2_send`, `noise.`, `sub.`, `filterN.`) whose bus rack holds a bypassed effect with a model. Frozen Pluck Drone is the case: oscillator 1 plays in Main and sends 55 % to Bus 1, where a Convolve with mix 100 is the only module. With the effect active the bus carries the source filtered by the impulse, unrelated to the dry copy in Main; bypassed, the bus carries the copy itself, which adds up with Main in amplitude (up to +6 dB). A source with the bus as its destination instead of Main (no dry copy) does not qualify; neither does a bus that nothing else shares.

The engine decides from the signals, not from the routing table: before the racks it measures, over 100 ms, the power of Main (Pm), of the bus input (Pb) and their cross term (C). With Pmod and A the power gain and the coherent amplitude of the bypassed effects of the rack (their product, the level trims included; A is the dry part of the mix, for Convolve the real part of the impulse spectrum per band), the active case adds Pb Pmod + 2 A C to Pm. The bypassed bus is multiplied by G that gives the same: G^2 Pb + 2 G C = Pb Pmod + 2 A C, so G = (-C + sqrt (C^2 + Pb (Pb Pmod + 2 A C))) / Pb. With C = 0 this is sqrt (Pmod), the gain per effect of the first round. G is applied at the bus output (after the rack and the bus level, one-pole smoothed 20 ms), the effects of the rack itself run as before. The gain of a bypassed effect on Main or in the band of a splitter stays at its place in the chain; on a bus it is applied at the output of the bus (an effect after the bypassed one on the same bus sees the level without it).

Test: `tier_makeup_bus_copy_of_main` (osc 1 sends 55 % to Bus 1, reverb mix 100 on Bus 1): bypassed +3.08 dB against the active case, with the makeup +0.44 dB (the same patch without the bus rule would have been the 3.08 dB of the module gain alone: the first round left +1.44 LU on the preset).

## 10. Unison cap makeup (design, last commit)

Cause (measured): the cap of tier `low` (3 voices) takes voices away from presets that ask for more. The blend law normalises the summed power of the voices, so the expected power does not change; what changes is the loudest 100 ms: more voices at a small detune beat with higher coherent peaks. Measured on all 160 presets (`low;fx=all` against `low;fx=all;unison=16`, loudest 100 ms), 16 presets ask for more than 3 voices: the change runs from -2.73 (Lush Analog Pad, 6 + 6 voices) to +0.54 (Hyper Saw Lead), mean -0.6. It does not follow the settings: Velvet Strings (6 voices, detune 25, spread 100) changes by -0.20, Lush Analog Pad (6, 28 and 32, 100) by -2.73. A gain by the number of voices w x level^2 x n^alpha fits at best alpha = 0.3 (RMS error 0.78 dB against 0.94 dB for no makeup); detune, blend and spread do not improve it: the summed power under the blend law is the same, the rest is the chance of the beating in the loudest 100 ms of the gesture.

Design: per oscillator, the gain (wanted / cap) ^ 0.15 in amplitude (0.3 in power) on the oscillator level when the cap is in force and the preset asks for more voices than it allows; wanted = the voices of the preset after modulation, cap = `Quality::maxUnison` (and the low tier cap), computed once per block into a table of 17 entries (`unisonMakeupTable`) and read in the voice tick where the level is set (`voice.cpp`, one line). Nothing else changes: with no cap in force or no oscillator over the cap the path is the one of before (bit equal, tested). It corrects the mean (+0.9 dB for 6 voices under 3), not the spread, which is chance; the presets whose cap effect is large by chance (Lush Analog Pad, Infinite Horizon) keep part of their difference. It is the last commit of the branch and touches `voice.cpp` (3 lines), `voice.h` (1), `own_engine.cpp` / `.h` (14); the other worker's branch changes `voice.cpp` in the sample / granular parameters only (merge checked: no conflict).
