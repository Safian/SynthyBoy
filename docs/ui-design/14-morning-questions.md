# Questions collected during the autonomous run (answer in the morning)

Rule for the run: no question blocks work; every question gets a stated default, work continues on the default.

| # | Date | Question | Default used | Where |
|---|------|----------|--------------|-------|
| 1 | 2026-11-08 | Importer: component formulas of 5 Vital wavetable component types, LFO line orientation and reference curves are my-own definitions (SPEC-GAP in code, `grep SPEC-GAP core/`). Need real `.vital` presets in `tests/vital-presets/` for A/B | proceeded with own definitions | core/wavetable-render, core/vital-import |
| 2 | 2026-11-08 | savePreset writes empty author as "Safian Szabolcs"; imported presets with no author must keep it empty (C3 overload) | to be fixed in C3 work | core/src/preset.cpp |
| 3 | 2026-11-08 | 13-new-keys.md assumptions A1-A28 (e.g. A8 20-entry scale list, A12 custom Vital sample imports into first free osc as Sample, A19 one instance per effect type with rack assignment key) | spec defaults used | docs/ui-design/13-new-keys.md §12 |
| 4 | 2026-11-08 | Custom LFO shape: point modulation by per-voice sources (velocity, envelopes) is stored but ignored (one shared shape table per LFO); `lfoN.mono` has no effect on envelope/loop trigger types | accepted limitation; macros, mod wheel, pitch wheel, channel pressure work | docs/ui-design/WP123-REPORT.md |
| 5 | 2026-11-08 | MTS-ESP: only the seam exists; libMTSClient licence believed permissive but NOT verified | not linked | THIRD_PARTY.md |
| 6 | 2026-11-08 | clap-validator was not run by the WP1-3 worker (I ran it on the earlier tree: 0 failed); re-run on final tree before push | pending | |
| 7 | 2026-11-08 | Cosmetic: -Wunused-const-variable (kNumEnv in vital_engine.cpp) and -Wmissing-field-initializers in test_wp123.cpp | fix in cleanup pass | |
