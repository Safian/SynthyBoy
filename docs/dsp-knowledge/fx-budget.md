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

`test: core/tests/test_own_fx_budget.cpp` (admission, order, racks, instance 0, determinism, no click, bit identity within budget and at tier high).
