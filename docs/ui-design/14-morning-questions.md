# Questions collected during the autonomous run (answer in the morning)

Rule for the run: no question blocks work; every question gets a stated default, work continues on the default.

| # | Date | Question | Default used | Where |
|---|------|----------|--------------|-------|
| 1 | 2026-11-08 | Importer: component formulas of 5 Vital wavetable component types, LFO line orientation and reference curves are my-own definitions (SPEC-GAP in code, `grep SPEC-GAP core/`). Need real `.vital` presets in `tests/vital-presets/` for A/B | proceeded with own definitions | core/wavetable-render, core/vital-import |
| 2 | 2026-11-08 | savePreset writes empty author as "Safian Szabolcs"; imported presets with no author must keep it empty (C3 overload) | to be fixed in C3 work | core/src/preset.cpp |
| 3 | 2026-11-08 | 13-new-keys.md assumptions A1-A28 (e.g. A8 20-entry scale list, A12 custom Vital sample imports into first free osc as Sample, A19 one instance per effect type with rack assignment key) | spec defaults used | docs/ui-design/13-new-keys.md §12 |
