# UI design (mobile-first)

These are design records and implementation reports. For the shipped MPC control layout and page relationships, start with the [MPC screen map](../ui-map/README.md).

Brief and rules: [00-BRIEF.md](00-BRIEF.md).

| File | Area | Author | Status |
|------|------|--------|--------|
| 01-shell-layout.md | top bar, macros, page tabs, keyboard/performance strip, gestures, landscape/tablet/MPC | sonnet-worker | historical design draft (880 keys) |
| 02-oscillators.md | osc 1-3, noise, unison, morph, warp, tuning/level/pan | sonnet-worker | historical design draft (880 keys) |
| 03-wavetable.md | wavetable view and editor (no browser) | sonnet-worker | historical design draft (880 keys) |
| 04-filters.md | filter 1/2, fx filter, formant, routing | sonnet-worker | historical design draft (880 keys) |
| 05-envelopes-lfos.md | envelopes, LFOs, random LFOs, shape drawing | sonnet-worker | historical design draft (880 keys) |
| 06-modulation.md | mod matrix, assign gestures, curves, stereo/bipolar, macros | agy (gemini-4-argon-high) | historical design draft (880 keys) |
| 07-voice-midi-global.md | voicing, portamento, legato, bend, MPE, tuning/microtonal, quality tiers, settings | agy | historical design draft (880 keys) |
| 08-effects.md | 9 effects, chain order | agy | historical design draft (880 keys) |
| 09-kick-integration.md | remote-control pages, ParamInfo mapping, JSON page definition | agy | historical design draft (1484 keys) |
| 11-vital-preset-import.md | import of Vital `.vital` presets (and wavetables/LFO shapes inside): format, key mapping to our 880 keys, unmappable values, report UI, licence rules | sonnet-worker (after 01-05) | historical design draft (880 keys) |
| 15-shell-features.md | key-free shell features: MIDI learn (G07), Save as Default (G02), module Lock (G08), .vitalbank / .vitaltable / .vitallfo import | sonnet-worker | built, A180-A196 |
| 10-coverage-matrix.md | Serum/Vital function -> where in our UI; historical engine GAP list (G01-G84), reconciled with current implementation | main thread | historical baseline reconciled |
| 12-decisions.md | owner decisions on the open questions of 10 §5.2 | owner + main thread | decided / proposed |
| 16-COVERAGE-FINAL.md | final per-function audit of the whole synth against both manuals, checked against the code (supersedes 10 as the active status reference) | main thread (work-coverage) | active status reference |
| 17-BACKLOG.md | the remaining work as ordered work packages with key estimates, dependencies and a parallel plan | main thread (work-coverage) | work packages (WP01-WP18 landed) |
