# FX cost budget

## Facts

- The worst allowed effect stacks do not fit the block at tier low on the Key 61 (2026-11-21, Acid Saw Reso 8 notes, block 2.902 ms, 3 runs, core 3, 1608 MHz): base without effects 21.0 % median / 44.5 % p99
  of the block; 16 multiband Limit compressors 175.7 / 230.8 %, every block over 100 %; the 16 dearest distinct configs 103.4 / 143.5 %; two of the dearest of each type 103.1 / 147.2 %.
  `measured: .hw-local/mpc-perf/key61-night-2026-11-21/fx-stack/final-table.md`
- Per-instance cost, % of the block, median of 16 identical instances minus the base, divided by 16: multiband Limit compressor 9.44, chorus 6.0, FX filter model 13 (Smear) 5.8-6.5, Compress / Serum 5.5, model 18
  4.9, phaser 4.3, delay hq 4.1 (hq off 2.2), distortion 06 with the pre filter 3.6, EQ with the second mid band 3.0, utility mono bass 1.5; the screened configs in `docs/perf/fx-budget.md`.
  `measured: fx-stack/screen-table.md`
- The p99 of a stack is about 1.2-1.25 times its summed median cost above the base p99: 16 Limit compressors summed 154.7 %, p99 +186.3 % (1.20); the 16 dearest distinct 82.4 %, +99.0 % (1.20); two of the
  dearest of each type 82.1 %, +102.7 % (1.25). `measured: final-table.md arithmetic`
- With the 35% budget, the median of three runs' p99 for V1 / V2 / V3 was 68.4 / 67.2 / 70.7% of a block. The p99 ranges were 65.6–69.5 / 67.1–70.6 / 68.8–73.1%. The **largest individual blocks across all three rounds** were 90.5 / 83.4 / 90.3%, with zero blocks over deadline. Without the budget, median p99 was 187.4 / 118.3 / 116.2%. The interleaved runs used eight held notes and the same three stacks on the Key 61. `measured: c7b707b0, 2026-11-21, .hw-local/mpc-perf/key61-night-2026-11-21/fx-budget/progress.log and raw/r{1,2,3}-FXV{1,2,3}-{A,B}.txt, CSV p99_pct, max_pct and blocks_over_100pct`

## Decisions

- Budget at tier low 35 % (`kFxBudgetLowPct`): base p99 44.5 + 1.25 x 35 = 88.3 %, under 90 %. 40 % (the first suggestion) gives 94.5 %, too close to a dropout (100 %) once the note starts, the display and a slower
  clock are added. HYPOTHESIS: the 1.25 factor holds for stacks that were not measured; V1–V3 are the three measured stacks above.
- Prefix admission in the chain order, not "skip the one that does not fit": the later effects are dropped, the user hears the first ones as in the chain; predictable and cheap to explain.
- A profile with `fx=all` lifts the budget (it asks for every effect); reverb with `reverb.force_enable` and the splitters are counted and never bypassed.

## Open

- Measure the outstanding [per-instance cost rows](../perf/fx-budget.md) on the Key 61. The three tested stacks bound the evidence for the 35% budget.
- The device figures above (and the table in `core/engine-own/fx/fx_budget.cpp`) were measured on 2026-11-21, before the CPU review round of 2026-11-26. That round made these cheaper on the host (below), so the table is an upper bound until it is measured again: the Limit rows of the compressor most of all. HYPOTHESIS: the device ratios are of the same order; only a Key 61 A/B/A (`measurement.md`) decides, and the three admitted stacks V1-V3 have to be checked against the 35% budget again with the new table.

## Host numbers of the CPU review round (2026-11-26, x86-64 VM, labelled host numbers, not the device)

`measured:` executed instructions per 128 sample block (4 control periods of 32: `set ()` then `process ()` each), valgrind callgrind, scratch micro benchmark of the effect classes against the headers of 391febd and of the branch, `-O3`, 44.1 kHz, default-like settings:

| Effect (setting) | before | after | after / before |
|---|---|---|---|
| Compressor, single band Limit | 144 014 | 70 441 | 0.49 |
| Compressor, Multiband Limit | 453 749 | 210 421 | 0.46 |
| Compressor, Multiband Compress / Serum | 184 282 / 215 129 | 160 122 / 188 281 | 0.87 / 0.88 |
| Delay, cubic / hq | 38 386 / 83 442 | 33 523 / 37 106 | 0.87 / 0.44 |
| EQ, law Vital / law Serum, 4 bands | 50 005 / 43 362 | 39 650 / 34 378 | 0.79 / 0.79 |
| Distortion, type 0 / 0 with the filter / 9 with the filter / 6 / 3 | 67 271 / 84 192 / 82 884 / 70 088 / 55 059 | 43 799 / 59 280 / 48 353 / 44 999 / 37 695 | 0.65 / 0.70 / 0.58 / 0.64 / 0.68 |
| Distortion type 0, 2x oversampled | 162 699 | 117 396 | 0.72 |
| Phaser (fast, moving LFO) / Chorus / Flanger | 76 873 / 236 318 / 33 470 | 71 034 / 235 658 / 33 141 | 0.92 / 1.00 / 0.99 |
| `FxChain::update`, Main rack, 3 / 5 modules on, inputs unchanged (per control period) | 8 330 / 8 698 (the derivation of every period) | 1 493 / 2 191 | 0.18 / 0.25 |
| XL only: Reverb type 0 / HalfBand2x up and down | 571 252 / 51 826 | 504 041 / 37 056 | 0.88 / 0.72 |

Whole preset renders (valgrind, whole `synthyboy_preset_render` run of one preset): Pentatonic Step Pluck (Limit compressor, tier low) 3.935 G -> 3.581 G instructions (-9.0 %), Acid Saw Reso (tier high) -2.9 %, Cymbal Cloud Riser (plate reverb, tier high) -1.8 %, Lorenz Garden (tier low) -0.6 %, Tine Glass Keys (phaser, tier low) -0.4 %. Paired host A/B/A (`synthyboy_tier_bench --modules --module-group fx --module-reps 5 --seconds 1 --config low`, core 3, base new base new base, median of the cost above the base load, us per block): Delay 7.7 -> 3.7, Distortion 9.4 -> 6.2, EQ 7.8 -> 5.6, Phaser 16.9 -> 14.0, Utility 3.2 -> 1.5, FX Filter 5.2 -> 3.4 (the last two are the chain's derivation); Chorus 23.2 -> 20.9 and Compressor 24.3 -> 24.7 (default settings, not Limit) are inside the spread of the runs. The Mac / device speed of these changes is not measured. Of the 1104 float renders (184 presets, tiers low / medium / high, single note and chord) 1088 are bit identical to the code before (`scripts/compare-renders.sh` rules); the 16 that differ are the Limit compressor presets Karplus Harp, Pentatonic Step Pluck and the Detonation Tail chord (the gain of the box filter, 3e-6 relative at most; -130 dB against the rms of the render for the first two, -83 dB at worst for the chord of Detonation Tail at tier low, where the ADAA distortion after the compressor conditions the difference) and Tine Glass Keys at tier low (the phaser guard, 9e-20 absolute).

**Splitter gain memo (2026-11-26, x86-64 VM, host number).** `SplitterLH / LMH / MS::merge ()` and `reset ()` made one `pow (10, ..)` per dB parameter (3 to 5) on every call, also at 0 dB. `DbGainCache` (`core/fx-serum-a/include/synthyboy/fx/splitter_gain_cache.h`) keeps the last result per parameter keyed on the exact float bits and answers exactly 0 dB (either sign) with 1 (`pow (x, +-0) == 1`, C99 Annex F). `measured:` callgrind, `synthyboy_preset_render --tier low`, one preset with the splitter on: Mid Side Halo (M/S) `merge ()` 1,611 -> 1,523 instructions per call (10.10 M -> 9.55 M over 6,272 calls), whole run 3,025.30 M -> 3,024.75 M (-0.02 %); Detonation Tail (L/H) 2,955.40 M -> 2,954.32 M, Tri Band Split Texture (L/M/H) 2,677.95 M -> 2,677.25 M (-0.03 %). The three renders (single note and chord, float32) are byte identical to the code before. The saving is small on this libm; the Cortex-A17 / A76 gain is not measured. `test: core/fx-serum-a/tests/test_splitter_gain_cache.cpp` (frozen copies of the old `reset ()` / `merge ()` / `process ()`: the memo against `dbToGain ()` over specials, denormals, NaN payloads, infinities, 3,000 random values and 3,000 random bit patterns; the three splitters over randomised parameter walks and blocks longer than `maxBlock`; mutation: a key that ignores the sign bit fails all four tests).

`test: core/tests/test_own_fx_budget.cpp` (admission, order, racks, instance 0, determinism, no click, bit identity within budget and at tier high).
