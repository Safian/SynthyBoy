# FX modulation eligibility

## Facts (source and host tests, 2026-11-24)

- Continuous FX parameters already marked `kParamModulatable` retain their existing behavior. Twelve additional fixed compressor parameters now accept modulation: LOW / MID / HIGH, each with lower and upper threshold and ratio. They alter dynamics without switching the band topology or algorithm. Source: `core/src/param_table.cpp`, `SB_COMP`; `core/engine-own/fx/fx_modules.cpp`, `CompressorModule::configure`.
- The compressor reads those values through the existing control-domain `fxValue` / `pct` path once per control period. Thresholds clamp to -120..+12 dB, lower ratios to -100..+100%, upper ratios to 0..100%. Its existing detector attack/release behavior is retained; no new per-sample parameter interpolator or audio-thread allocation is introduced. This is control-rate envelope/LFO modulation, not an audio-rate smoothing guarantee. Source: `fx_modules.cpp`, `fx_compressor.h`.
- Generic pool parameters already accept modulation. `FxChain::configureSlot` applies the selected slot's normalized delta before converting to the compressor's plain source values. The additional fixed-parameter eligibility does not change pool keys or descriptor order. Source: `core/engine-own/fx/fx_chain.cpp`.
- All 1,249 existing destination choice numbers remain unchanged, including master.transpose at 1,249. The twelve compressor destinations append at 1,250..1,261; the choice includes NONE, giving 1,262 entries. No parameter keys, IDs, ranges, defaults or state version change. Source: `core/src/modulation.cpp`; test: `fx_compressor_new_destinations_append_without_changing_legacy_choices` hashes the complete old destination prefix.
- Tests compare all twelve modulated DSP values with their equivalent static settings, verify finite clamped extremes and zero deltas, render an actual Env 2 route, check zero-depth/OFF sample identity, isolate two pool slots and round-trip old/new destination values through state serialization. Test: `core/tests/test_fxopt_compressor_modulation.cpp`.

## Decisions

- Keep structural choices excluded as fixed FX destinations: power, effect model/type, room/IR selection, rack/order, sync/division, band count, quality and latency switches. Chorus voices and Hyper unison remain excluded because they change discrete voice/lane counts.
- Reverb ER SIZE remains excluded. Its current early-reflection tap scaling reads a changed delay directly; no dedicated moving-tap interpolation has been established here. A separate audible-transition study would be needed before making that a supported continuous modulation destination. Source: `core/engine-own/fx/fx_reverb.h`, `prepareCore` / `coreSample`.
- No new device performance claim: only offline host correctness was checked. Existing device FX budgets remain unchanged; see [fx-budget.md](fx-budget.md).
