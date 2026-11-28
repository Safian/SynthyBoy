# Content packs: developer note

A content pack is a folder with a `contentpack.json` manifest and the files it lists (format: `schema/contentpack.schema.json`, tool: `tools/contentpack/contentpack.py`, architecture: `docs/ARCHITECTURE.md` section 5). The plugin lists the packs it finds under `Content/packs/<pack id>/` (the MPC package copies `content/factory-wavetables` and `content/factory-samples` there; `scripts/build-mpc.sh`).

## Licence

Everything in the factory packs is synthesised by `scripts/gen-content-pack.py` from mathematics, "Synthesised from nothing (CC0-1.0, no third-party material)". The manifests say `"license": "CC0-1.0"`, `"attribution": "Safian Szabolcs"`. Do not copy or imitate any commercial product's tables, names or presets.

## The wavetable file

16 bit mono PCM WAV, 2048 samples per frame, N frames = N x 2048 samples (1..64 written; the engine reads up to 256). `core/engine-own/osc/content_set.cpp` reads it with `sampleosc::readWav` (unknown RIFF chunks are skipped, so extra chunks such as `clm ` are harmless; none is written), removes each frame's mean, and builds its own mip levels. Frames should be clean up to their intended harmonic count, DC free, with headroom; a scan should not jump in level.

## Layout

```
content-src/wavetables/<Category>/<slug>.json     the recipes of the factory pack (one table each)
content-src/addons/<dir>/pack.json                an add-on pack: {"id","name","version","description"}
content-src/addons/<dir>/<Category>/<slug>.json   its recipes
content/factory-wavetables/<Category>/<slug>.wav  generated (committed), with contentpack.json
content-addons/<dir>/<Category>/<slug>.wav        generated add-on pack, with its own contentpack.json
```
Categories are the six folders `Analog Digital Spectral Vocal Metallic Noise`. The manifest lists per wavetable `path`, optional `category` and `title` (the browser's category and its human name; defaults: the sub folder name and the file stem), `sha256`, `size`.

## Add a table to the factory pack

1. Write `content-src/wavetables/<Category>/<slug>.json` (copy a neighbour; format below). `slug` = file name = what presets will name, so choose it once.
2. `python3 scripts/wavetable-lab.py check` (validates and renders every recipe).
3. `python3 scripts/gen-content-pack.py --only <Category>` writes the wav and the manifest of that category; `--list` prints the catalogue; `--check` verifies everything is what the recipes make (the numpy of the machine that made the files; a different numpy release may differ by one least-significant bit: regenerate then).
4. Update `docs/release/WAVETABLE-PACK.md` and the catalogue in `docs/user-guide/factory-content.md` by hand.
5. Presets that use the table name it by pack id + path + SHA-256 (`WT(1, "Analog/pwm_dual_phase")`); the preset files and `content/presets/contentpack.json` are edited directly.
Never rename or regenerate a published table with different content: presets pin the hash.

## An add-on pack beside the factory one

Create `content-src/addons/<dir>/pack.json` (`id` like `com.safian.synthyboy.addon-<name>`, a name, a version) and recipes below it, or let `wavetable-lab.py promote NAME --run RUN --category C --title T --pack <id>` do it. `python3 scripts/gen-content-pack.py --addon <dir>` writes `content-addons/<dir>/` with its own manifest; copy that folder to `Content/packs/<pack id>/` (or ship it as a zip: `tools/contentpack/contentpack.py build --dir content-addons/<dir>`). The plugin lists every pack folder under `Content/packs/` next to the factory one.

## The wavetable lab

`scripts/wavetable-lab.py` generates candidates from per-category grammars, scores them, renders audition files and promotes the chosen ones. The agent-neutral manual (commands, full schema, guidance) is `.agents/skills/wavetable-lab/SKILL.md`; the summary:

```
python3 scripts/wavetable-lab.py generate --seed 1 --per-category 40     # -> /Users/szabi/Documents/Projects/Safian/.hw-local/wavetable-lab/<date>-1/ (outside the repository)
python3 scripts/wavetable-lab.py score    --run DIR --keep 8             # scores.json, kept list
python3 scripts/wavetable-lab.py preview  --run DIR                      # preview/*.wav and index.html (open it, tick the ones you like)
python3 scripts/wavetable-lab.py promote  NAME --run DIR --category Vocal --title "Soft Choir"
python3 scripts/wavetable-lab.py propose  --from-file model-answer.txt --category Vocal   # recipes written by a local model
```

**Recipe** (`"schema": "synthyboy-wavetable-recipe/1"`): `title`, `category`, `description`, `frames` (2..64), `seed`, `source` (a tree of nodes) and optional `post` (`limit`, `smooth`, `level`, `rms`, `peak`). A node has a `type` (`additive formant fm pulse classic pd sync shape spec ring mix partials noise clicks burst stepseq gaussformant`), `params` and `input` / `inputs`. A parameter is a number or a law (`const linear curve steps lfo keys`) evaluated at u = frame / (frames - 1). Parameters, ranges and three examples: the SKILL.md. `propose` validates strictly (unknown keys and parameters are errors, numbers are clamped with a warning).

**Score** (a filter, not a judge of beauty), per candidate, on the 32 band log-harmonic envelope `env[frame, band]` (dB relative to the frame's energy, floor -60 dB):
- movement `M = max(max_i RMS(env_i - env_0) / 6 dB, max_i RMS(f_i - f_0) / RMS(f_0) / 0.5)`; component `min(M,1)^0.5`; static if `M < 0.35`.
- smoothness: step unit `u_i = 0.5 (RMS(env_{i+1} - env_i) / 2 dB + RMS(f_{i+1} - f_i) / (mean RMS * 0.35))`; component `1 / (1 + max(0, (max u - 2) / 2)^2)`; jumpy if `max u > 4.5`.
- richness: effective harmonics `exp(entropy)` per frame; `0.5 min(mean / 6, 1) + 0.5 min(ln(max / min) / ln 3, 1)`; rejected if the maximum stays under 1.6.
- aliasing: `10 log10(energy above harmonic 383 / total)` of the worst frame; component `clamp((-dB - 25) / 25)`; rejected above -20 dB.
- level evenness: spread of per-frame RMS in dB, `1 / (1 + (spread / 4)^2)`; rejected above 12 dB.
- crest: mean crest `1 / (1 + max(0, (dB - 10) / 4)^2)`; rejected above 26 dB (max); DC rejected above 0.001; peak above 0.98 rejected.
- quality = geometric mean of the six components.
- diversity: descriptor = the envelope at 16 frames x 32 bands; distance = RMS dB difference, the smaller of forward and reversed. A candidate is dropped if it is under 2 dB from a pack table, and the kept list per category is chosen greedily by quality with at least 3 dB between kept candidates.

**Preview**: a band limited wavetable oscillator in the script (tables per pitch cut at 0.45 sample rate, linear interpolation between frames and samples) renders a 4 s scan from frame 0 to the last at C2, C3, C4 and a C major 7 chord; `index.html` shows frames 0/16/32/48/63, the spectrum over the scan (inline PNG, written with the standard library: no PIL needed), the numbers and audio players. The page writes nothing; the owner tells the names.

**Local model**: paste the schema section of the SKILL.md and the request of `docs/wavetable-recipe-prompt.md` into the model, save the answer, run `propose`, then `score` and `preview`. The script never calls a model or the network.

**Promote** copies the recipe (title, category, a slug of the title, seed = slug) into `content-src/`, regenerates the category with the factory code path (`gen-content-pack.py --only`, or `--addon`) and updates the manifest. Commit `content-src/`, the regenerated folder and the docs.
