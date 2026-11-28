# DSP knowledge (SynthyBoy own engine)

What we know for sure, so that a plan starts from the best known solution instead of a guess.
Every planner and worker that designs or changes DSP, real-time code, memory use or the MPC
display path reads the topic files below that touch its work **before** writing the plan.

## Rules

1. **Clean room.** Nothing here comes from Vital, Serum or any other product's code. No code
   excerpts from third-party synths, no reading of `core/vital-import/` or
   `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/` to write these files.
   Published papers, books, datasheets and standards are fine, cited.
2. **Every claim has its evidence**, one of:
   - `measured:` a measurement of ours: where the raw data lives (device files under
     `.hw-local/mpc-perf/` are local to the owner's Mac; the numbers are copied here), the
     commit, the device, the date, the method (A/B/A, runs, spread);
   - `source:` a source that was **opened** (URL or book with chapter/section/page, read on a
     given date); a source remembered but not opened does not go in;
   - `test:` the test in the repo that proves it (`core/tests/...`, the check name).
3. **A topic file** = one question area: the facts, the decisions taken with their reason, the
   rejected options with the number that rejected them, and the open questions. Short: if it
   grows past about 150 lines, split it.
4. **Hypotheses are marked** `HYPOTHESIS` and are not used as a decision without a measurement.
5. **Update on every finding.** A measurement that changes a fact changes the file in the same
   commit that lands the change (or the revert). Wrong facts are corrected, not left with a note.

## Topics

| File | Area |
|---|---|
| [measurement.md](measurement.md) | How to measure on the devices (A/B/A, drift, spread) and on the Mac; what a number is worth |
| [platform-key61.md](platform-key61.md) | MPC Key 61 / XL CPU, memory, cores, what is fast and slow on the Cortex-A17 |
| [arm-denormals.md](arm-denormals.md) | ARM32 (`FPSCR.FZ`) and AArch64 (`FPCR.FZ`) denormal protection in the MPC shell and verification status |
| [vector-paths.md](vector-paths.md) | Every NEON path of the engine: its scalar reference, the bit-identity test, status; the compiled-code check for fused multiply-add and estimates |
| [mpc-display.md](mpc-display.md) | The MPC host's display pipeline (DRM planes, commits) and our overlay |
| [native-text-entry.md](native-text-entry.md) | Native text keyboard design, factory text keyboard, XL numeric input and verified host string-setter gate |
| [skin-host-behaviour.md](skin-host-behaviour.md) | How the host treats our skin: list selection and closing, a skin change that froze the host, tab backgrounds, importFiles (measured on the Key 61) |
| [community-practices.md](community-practices.md) | Ranked code/skin organisation practices from the community plugins and the SDK (skin structure, importFiles, size comparison with stock skins, plugin/UI separation, lint, release) |
| [mpc-expert-runtime.md](mpc-expert-runtime.md) | Expert startup, background source refresh and source-cited lexical retrieval index |
| [mpc-host-scripting.md](mpc-host-scripting.md) | The host's embedded Lua API (Q-Link Touch / Rotate / SetPosition), device scripts, control surface; fact sheet, not a release path |
| [spectral-start.md](spectral-start.md) | Note-on spikes of the Spectral oscillator and the start ledger |
| [antialiasing.md](antialiasing.md) | ADAA, oversampling, band limits: what each bought, measured |
| [filters.md](filters.md) | Diode / ladder filters: solvers, cost, accuracy |
| [fx-modulation.md](fx-modulation.md) | Useful continuous FX modulation destinations and stable stored route numbers |
| [fx-budget.md](fx-budget.md) | The FX cost budget of a tier: per-instance costs on the Key 61, the budget number, the admission rule |
| [voice-control.md](voice-control.md) | Voice sequence/random/scaling execution and target-mask scope |
| [testing.md](testing.md) | Test recipes that catch real defects; bit-identity; checksums |
| [test-overlap-report.md](test-overlap-report.md) | Which tests appear to check the same thing twice; mutation evidence per candidate; no test was deleted |
| [sources.md](sources.md) | The opened literature, with what each source is used for |
