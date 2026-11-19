---
name: wavetable-lab
description: Use when generating, scoring, auditioning and promoting wavetable candidates for the SynthyBoy content packs, or when writing new wavetable recipes (JSON) for them.
---

# Wavetable lab (SynthyBoy)

Read this whole file; it is self-contained. Do the job the user asked for with the commands below.

## Where things are
- Find the repository root: the folder that contains `scripts/wavetable-lab.py` (try `git rev-parse --show-toplevel`; in a git worktree that is the worktree). All paths below are relative to it.
- Script: `scripts/wavetable-lab.py`. Library and recipe format: `scripts/wavetable_lib.py`. Pack generator: `scripts/gen-content-pack.py`.
- Factory recipes: `content-src/wavetables/<Category>/<slug>.json` (72 today). Add-on pack recipes: `content-src/addons/<dir>/` (+ `pack.json`). Generated packs: `content/factory-wavetables/`, `content-addons/<dir>/`.
- Run with `python3` (numpy needed; no other package). Nothing needs the network or a model.
- Categories (exactly these six): `Analog` `Digital` `Spectral` `Vocal` `Metallic` `Noise`.

## Rules
1. Never promote into the factory pack unless the user named the candidates (names like `ana-1-018`). Showing, scoring and previewing is always fine.
2. Generated material stays OUTSIDE the repository (default `/Users/szabi/Documents/Projects/Safian/.hw-local/wavetable-lab/<date>-<seed>/`) until promoted. The script refuses a run folder inside the repository.
3. No network, no model calls from the script. A model only writes recipe text; `propose` validates it.
4. Deterministic: every random choice comes from `--seed` or the recipe's `seed`. Report the seed.
5. CC0 only. Never copy, imitate or name a commercial synthesizer's tables or presets. Invent names that describe the sound or the scan.
6. Do not build or run test suites for this job. Do not edit `content/` by hand (it is generated). Do not push.
7. The score is a junk filter, not a judge of beauty. You cannot hear: say so, and let the user listen.

## Commands (run in the repository root)
- `python3 scripts/wavetable-lab.py generate [--seed 1] [--per-category 40] [--categories Analog Vocal] [--out DIR]`: writes `DIR/candidates/<Category>/<name>.json` + `.wav` (name `ana-<seed>-<nnn>`), `DIR/run.json`. Prints the folder.
- `... score --run DIR [--keep 8]`: renders and measures every candidate, writes `DIR/scores.json` (measures, quality 0..1, reject reasons, nearest pack table, kept list). Prints per category: quality min/median/max, rejects by reason, the kept names.
- `... preview --run DIR [--all]`: for the kept candidates (all with `--all`) writes `DIR/preview/<name>_{C2,C3,C4,chord}.wav` (4 s slow scan frame 0 to last, 44.1 kHz) and `DIR/index.html` (waveforms of frames 0/16/32/48/63, spectrum-over-scan picture, scores, audio players, tick boxes that list the chosen names). Prints the index path.
- `... propose --from-file FILE [--category C] [--run DIR]`: reads recipes a model wrote (JSON object, list, `{"recipes":[...]}`, or prose with fenced JSON), validates strictly, clamps values (prints warnings), prints every error of a rejected recipe, writes accepted ones as `<cat>-ai-<nnn>` candidates into the run folder (new folder if `--run` is not given). Then run `score` and `preview` on that folder.
- `... promote NAME --run DIR --category C --title "Title" [--slug s] [--pack com.safian.synthyboy.addon-x [--pack-name "Text"]] [--force]`: copies the recipe to `content-src/wavetables/C/<slug>.json` (or the add-on pack folder), then regenerates that category (`gen-content-pack.py --only C`, or `--addon dir`) and the manifest. Refuses to overwrite without `--force`. Afterwards run `python3 scripts/gen-wavetable-docs.py` and commit `content-src/`, the regenerated `content/` (or `content-addons/`) and `docs/`.
- `... list [--run DIR]`: factory recipes (category, slug, frames, title); with `--run` the candidates with quality and kept / rejected.
- `... check [--run DIR]`: validates and renders every recipe (factory, add-on, and a run's candidates). Exit 1 on an error.
- `python3 scripts/gen-content-pack.py --list | --check | --only <Category>`: catalogue; verify files and manifests are what the recipes make; regenerate one category.

Typical job: `generate` -> `score` -> `preview` -> give the user `DIR/index.html` -> user names favourites -> `promote` each -> `gen-wavetable-docs.py` -> commit.

## Recipe JSON (schema "synthyboy-wavetable-recipe/1")
```
{"schema":"synthyboy-wavetable-recipe/1","title":"Human Name","category":"Analog","description":"one sentence: what the scan does",
 "frames":64,"seed":"any text","source":<node>,"post":{"limit":400,"smooth":"yes","level":"rms","rms":0.35,"peak":0.9}}
```
`frames` 2..64. `post` is optional (`limit` highest harmonic 16..1000, `smooth` yes|no taper near the limit, `level` rms|peak|none, `rms` 0.05..0.6, `peak` 0.3..0.98). No other top-level key but `tags`. Unknown keys and parameters are errors.
Frame i of n is evaluated at scan position `u = i/(n-1)` (0..1); the result has no DC, is band limited at `limit`, and every frame gets the same RMS (unless the peak limit stops it).

A **node**: `{"type":T,"params":{...},"input":<node>}` (one input for `shape` `spec` `ring`) or `"inputs":[<node>,...]` (1..4, for `mix`). Nest at most 6 deep.
A **parameter** is a number (constant), a string (enum), a list, or a **law** (how it moves with u):
- `{"law":"const","v":x}` `{"law":"linear","from":a,"to":b}` `{"law":"curve","from":a,"to":b,"power":p}` (u^p, p>1 slow start)
- `{"law":"steps","from":a,"to":b,"n":4}` stepped; `{"law":"lfo","center":c,"depth":d,"cycles":1,"phase":0,"shape":"sine|tri|saw"}`
- `{"law":"keys","points":[[0,a],[0.5,b],[1,c]],"ease":"linear|smooth"}` (u rising, 2..32 points)
Values outside the range are clamped (warning). Numbers below are `default [min..max]`; `int` = rounded. "Harmonic" numbers count partials of the cycle (the engine plays harmonic 1 at the played pitch).

Nodes (all frequencies are in harmonic numbers unless Hz is said):
- `additive`: count 64 [1..512] (fractional = the last harmonic fades in), rolloff 1 [0..4] (amp 1/k^rolloff), mask all|odd|even|prime|every (+ mask_n 2 [1..32] for every), mask_soft 0 [0..1] (level of masked-out harmonics), stretch 0 [0..0.02], comb_period 0 [0..64] (0 off) comb_depth 1 comb_offset 0 [0..64], peak_center 8 [1..300] peak_width 1 [0.04..4] (octaves) peak_gain_db 0 [-30..36], phase_quad 0 [0..0.05] (phase = q k^2: dispersion, magnitudes unchanged), phase_random 0 [0..1], fundamental 1 [0..4].
- `formant`: f0 130 [60..600] Hz (pitch the vowel is made for), voice men|women|children, voice_b, voice_mix 0 [0..1], vowel_a / vowel_b (i I E ae a o U u V), mix 0 [0..1] (vowel morph), sequence ["i","a",...] (up to 32) with pos 0 [0..31] (position in the list, smoothly interpolated; overrides vowel_a/b), shift 1 [0.4..2.5] (formant scale), tilt 1 [0..3], bw_floor 0.7 [0.3..1.5], count 200, breath 0 [0..1] (whisper: random phases and amplitudes), scatter 0 [0..1] (random phases only), seed, h1_boost_db 0 [-12..12].
- `fm`: carrier 1 [1..8]int, chain out = sin(carrier t + i1 op1), op1 = sin(r1 t + i2 op2), op2 = sin(r2 t + i3 op3), op3 = sin(r3 t); r1 r2 r3 [0..16]int (0 = off), i1 i2 i3 [0..20], feedback 0 [0..2.2], phase, mod_phase [0..1].
- `pulse`: width 0.5 [0.01..0.5], stack 1 [1..7]int, width_spread 0 [0..0.45], shift_spread 0 [0..0.5], saw_mix 0 [0..1], stack_gain 1.
- `classic`: sine 0, tri 0, saw 1, square 0 (weights 0..1), skew 0.5 [0.02..0.98] (triangle), oct2 0, oct4 0 [0..1] (octave-up copies), count 300 [8..500], stack 1 [1..9]int, stack_spread 0 [0..0.5] (phase fan).
- `pd` (phase distortion): mode knee|resonance|pulsar, amount 0.5 [0..48] (knee position 0.02..0.5, or resonance/centre in cycles), width 0.15 [0.03..0.5].
- `sync`: ratio 1 [0.5..16] (slave frequency), slave sine|saw|tri|square, window none|fade|hann.
- `shape` (input): mode tanh|fold|clip|power|bits|rectify, drive 1 [0.1..30], bias 0 [-1..1], power 1 [0.05..3], bits 8 [1.5..12], wet 1 [0..1].
- `spec` (input; spectral operations on a frame): tilt 0 [-3..3], shift 0 [-32..64], blur 0 [0..20], band_center 8 [1..400] band_width 1 [0.1..4] octaves band_gain_db 0 [-40..36] (negative = notch), lp_cut 0 [0..800] (0 off) lp_res 0 [0..8] lp_poles 2|4, hp_cut 0 [0..500] hp_res, comb_period/comb_depth/comb_offset as in additive, rcomb_d 0 [0..0.5] rcomb_g 0 [0..0.97] (resonant comb), scatter 0 [0..1] (random phases), seed.
- `ring` (input): c 2 [0.25..40] (carrier in harmonics, may be fractional: inharmonic), dry 0 [0..1], max_k 48 [1..300]int.
- `mix` (inputs): g1..g4 [0..2] gains (laws make a cross fade), match rms|none (rms = level match each input first).
- `partials`: set bell|bar|membrane|gong|bowl|harmonic|cloud|random, scale 3 [0.5..40] (harmonic number of the set's fundamental), count 10 [1..48]int, rolloff 1, bright 1 [0.3..1] (gain^index: upper modes), pair_delta 0 [0..3] (beating pairs), jitter 0 [0..0.6], cloud/random: lo 4, hi 120, wobble 0 [0..20], wobble_cycles, keys 4 [2..8] with pos [0..7], seed; custom `ratios`/`amps` lists.
- `noise`: frozen spectrum (fixed random phases/amplitudes per seed): tilt 0 [-3..3] (0 white, 1 pink, 2 brown), density 1.1 [0..1.2] (<1 sparse), count 380 [8..500], seed.
- `clicks`: count 8 [1..120] (impulses per cycle), tail 0 [0..200], seed. `burst`: width 0.2 [0.01..1] (gaussian noise burst), tilt, seed.
- `stepseq`: steps 8 [2..32]int, keys 5 [2..8]int, pos 0 [0..7] (morph through random patterns), slew 0.15 [0.01..0.6], seed. `gaussformant`: m 4 [1..48], width 0.12 [0.03..0.5].
- `legacy`: `{"fn":"<name>"}` only for the first 16 factory tables (hand written, byte-identical). Do not use it in new recipes.

### Example 1, additive (spectral tilt: dark to bright)
```
{"schema":"synthyboy-wavetable-recipe/1","title":"Tilt Sweep","category":"Spectral","description":"The roll-off exponent falls from 2.6 (almost a sine) to 0.3 (buzzy).","frames":64,"seed":"tilt",
 "source":{"type":"additive","params":{"count":200,"rolloff":{"law":"linear","from":2.6,"to":0.3},"phase_quad":0.01}}}
```
### Example 2, FM (index sweep with a second operator)
```
{"schema":"synthyboy-wavetable-recipe/1","title":"FM Two Step","category":"Digital","description":"Carrier 1, modulator 2 with index 0 to 3, itself modulated by an operator at 3.","frames":64,"seed":"fm2",
 "source":{"type":"fm","params":{"carrier":1,"r1":2,"i1":{"law":"linear","from":0,"to":3},"r2":3,"i2":{"law":"linear","from":0,"to":2.5}}},"post":{"limit":300}}
```
### Example 3, vocal / formant (a vowel walk, men at 130 Hz)
```
{"schema":"synthyboy-wavetable-recipe/1","title":"Vowel Walk","category":"Vocal","description":"ee, eh, ah, aw, oo in turn, men, 130 Hz: best range an octave around it.","frames":64,"seed":"vw",
 "source":{"type":"formant","params":{"f0":130,"voice":"men","sequence":["i","E","a","o","u"],"pos":{"law":"linear","from":0,"to":4},"tilt":1.0}},"post":{"limit":250}}
```
Cross fade example: `{"type":"mix","inputs":[<A>,<B>],"params":{"g1":{"law":"linear","from":1,"to":0},"g2":{"law":"linear","from":0,"to":1}}}`.

## Inventing recipes: what makes a good scan
- A scan moves smoothly and clearly: neighbouring frames differ a little, frame 0 and frame 63 differ a lot. One idea per table; two moving parameters at most, at different speeds. Use `curve` power 1.5..3 for filter-like sweeps.
- The score rejects: static tables (the farthest frame is under about 2 dB / 0.18 of the waveform from frame 0), sudden jumps (one step over 4.5 units: stepped tables are rejected on purpose), more than -20 dB of energy above harmonic 383 (aliasing), level spread over 12 dB, DC over 0.001, peaks over 0.98, crest over 26 dB (a single click), and tables that never get richer than a sine. It also drops near-duplicates of the pack and of each other (spectral trajectory distance under 2 / 3 dB).
- Analog: saw / square / pulse / triangle families, filter sweeps (`spec` lp_cut with `curve`, lp_res 1..5), sync ratio 1..12, PWM width 0.04..0.5, stacks with spread 0..0.45, soft clip drive 0.5..14, fold drive 1..8. Keep `limit` 300..400.
- Digital: FM index 0..10 on ratios 1..8 (integers only), feedback 0.2..1.2, phase distortion (knee 0.04..0.5, resonance 1..24), bits 2..10, stepseq slew 0.05..0.4, gaussformant m 1.5..36. Use `limit` 300.
- Spectral: additive masks with count 1..120, rolloff 0.3..2.5, peak sweeps (peak_width 0.1..0.5 octaves, gain 12..30 dB), comb period 3..20, phase_quad 0..0.02, `spec` shift 0..30.
- Vocal: `formant` with a sequence of 3..8 vowels, f0 near the voice (men 110..150, women 200..250, children 250..300), `limit` 250, tilt 0.8..1.4; whisper-to-voiced and voice morphs via `mix` or voice_b / voice_mix. Always state the pitch in the description.
- Metallic: `partials` sets with scale 2..12 walking, pair_delta 0..1.5, ring `c` 0.5..14 gliding (non-integer = clang), resonant comb d 0.03..0.45 g 0.8..0.95 with scatter 0.3..1, clouds with 12..40 partials. Keep `limit` 300.
- Noise: use `noise` with a moving tilt (-1..2.4), `spec` band / notch sweeps (width 0.15..0.7 octaves), low pass sweeps, density 0.02..1.1, clicks 2..90, bursts; set `post.limit` 320 and `noise.count` 300 so nothing exceeds the alias limit.
- Give a `description` that says what the scan does in one sentence (it becomes the user guide line). Titles are plain words, no product names.

## Reporting back to the user
1. Say what you ran (commands, seed, folder) and the counts (generated, rejected by reason, kept per category). Quote the score line per category.
2. Give the absolute path of `DIR/index.html` and ask the user to open it, tick favourites and tell you the names.
3. Give a shortlist (about 3 per category) with one reason each from the measures (movement, largest step, level spread, distance to the pack) and the recipe description. Say plainly that nothing was heard.
4. After a promotion: say which recipe files and regenerated files changed, and that the integration session must regenerate preset-pack hashes only if factory file names changed (new tables change none).
