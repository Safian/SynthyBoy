# FX cost budget (Quality::fxBudgetPct)

Tier low (MPC Key 61) has no limit on how many effects run: 16 instances (`fxi01..16`) plus the 13 fixed modules in three racks. The worst allowed stack does not fit the block (see the numbers in
`docs/dsp-knowledge/fx-budget.md`). The budget bounds the summed cost of the effect instances that run, so the sound drops the last effects of the chain instead of the audio dropping out.

## Rule

- `Quality::fxBudgetPct`: percent of a 128 frame block at 44.1 kHz (Key 61 calibration). 0 = unlimited (medium, high, XL: nothing changes, the samples are those of every earlier version). Tier low: `kFxBudgetLowPct` = 35.
- Every effect instance that runs (on, its type allowed by the quality's `fxMask`, in a rack that is reached) has a cost from the table below. They are admitted in the chain order: rack Main, Bus 1, Bus 2 (an
  effect in a band of a splitter at the place of the splitter), inside a rack by the order key (modules of instance 0 by `fx.order`, slots by `fxiNN.order`; tie: the module first, then the slots by number).
- An instance is admitted while the running sum stays within the budget. The first one that does not fit and every later one are over budget: bypassed like an effect the mask switches off (the 10 ms crossfade of
  a slot, the one control period fade of a module, the loudness left as a gain when `fxMakeup` is on and the effect has a loudness model). The parameters keep their values.
- Exempt (counted, never bypassed): the Reverb (runs only with `reverb.force_enable`, a deliberate trade) and the three Splitters (bypassing one would remove the effects in its bands).
- The admission is a function of the effect configuration (types, on/off, rack, order and the config keys of the cost table) only, read from the unmodulated values: the same configuration always admits the same
  instances, live or offline. It is redone only when the parameter snapshot, the mask or the budget changed, never per block, and allocates nothing (`core/engine-own/fx/fx_budget.cpp`).
- The result is read with `IEngine::fxBudgetStatus` (`FxBudgetStatus`: budget, admitted and requested cost in hundredths of a percent, `overModules` bit m = module m of instance 0, `overSlots` bit s = slot s) and
  `FxChain::overBudgetModules() / overBudgetSlots()`, for the UI and the status line.
- The device profile (`shell-mpc/device_profile.h`) carries it: the base `low` has 35, `fx=all` lifts it (a profile that asks for every effect is a measurement or the user's choice), medium and high have none.
  `tier_bench --fx-budget PCT` overrides it, `--fx-budget-status` prints the state (docs/perf/README.md).

## Cost table (hundredths of a percent are used in the code; shown here in percent of a block)

Calibration: Key 61, 1608 MHz, core 3, tier low, Acid Saw Reso 8 notes, 16 identical instances in the Main rack, cost = (median of the load - median of the base) / 16, one run per config
(`measured: .hw-local/mpc-perf/key61-night-2026-11-21/fx-stack/screen-table.md`, 2026-11-21; the figures of the final runs where they are higher).

| Module | Config | Cost % |
|---|---|---|
| Compressor | Multiband: Compress / Limit / Serum | 5.55 / 9.44 / 5.49 |
| Compressor | Single band: Compress / Limit / Serum | 2.55 / 3.72 / 2.58 |
| Compressor | Low / High band (two bands) | not measured: the Multiband figures |
| Chorus | default settings | 6.01 |
| Phaser | default settings | 4.28 |
| Flanger | not measured | 6.01 (the Chorus figure) |
| Delay | hq off, by style Mono / Stereo / Ping pong / Mid ping pong / Tap | 2.17 / 2.22 / 2.18 / 2.14 / 2.19 |
| Delay | hq on, same styles | 4.05 / 4.06 / 4.06 / 4.12 / 4.07 |
| Distortion | type 0 .. 13, no filter | 2.86 .91 1.53 1.98 .78 1.22 3.06 2.46 2.83 1.30 2.28 1.45 1.17 1.71 |
| Distortion | type 0 .. 13, filter Pre (Post: not measured, the Pre figure) | 3.39 1.95 2.45 2.78 1.96 2.10 3.60 3.07 2.97 2.25 2.93 2.45 2.17 2.56 |
| EQ | law Vital with the second mid band / law Serum with it (without it: not measured, the same) | 2.98 / 2.13 |
| FX filter | model 0 .. 26 | .97 2.76 3.19 .84 4.48 1.25 2.02 4.06 2.53 3.58 2.18 1.60 1.68 6.50 3.72 2.73 2.36 2.93 4.90 2.63 2.68 2.37 3.57 2.50 1.94 3.69 2.54 |
| Utility | mono bass on (off: not measured, the same) | 1.50 |
| Reverb, Hyper / Dimension, Bode, Convolve | not measured on the device | 20.00 (twice the dearest measured) |
| Splitters | L/H, M/S ; L/M/H (not measured) | 1.50 ; 3.00 |

Only the default settings of the other keys were measured (chorus voices, delay times, drive ...): the one figure is used for every setting.

## Open

- The cost of the unmeasured rows (Flanger, Reverb, Hyper, Bode, Convolve, Splitters, bands 1 / 2 of the compressor, the cheaper forms of EQ and Utility) on the Key 61 (A/B/A, `docs/dsp-knowledge/measurement.md`).
- The three V1–V3 admitted stacks were checked on the Key 61 over three rounds. See [the results, conditions and raw evidence](../dsp-knowledge/fx-budget.md). Other combinations remain unmeasured.
