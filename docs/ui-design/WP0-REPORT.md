# WP0 report - table and state infrastructure for the 604 new keys (2026-11-08)

Branch `worktree-agent-a96ce42e160747796`, based on develop `9c97a69`. Spec: `13-new-keys.md` (§11 WP0). Result: 1484 keys, state version 3, `content` section, EngineCaps; the first 880 keys, their ids, positions and sound are unchanged.

## Gates (run once at the end)

`cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=ON && cmake --build build -j8 && ctest --test-dir build`: build clean (VST3 + CLAP built, no compiler warnings; only the linker's pre-existing "duplicate libraries" note), `ctest` 15/15 passed (including `synthyboy_test_rt`, the RT-allocation test, and the 5 Vital suites). `build/clap_smoke build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap` (JUCE shell, `applyValues` exact-float state round trip, parity with the core table): all checks passed with 1484 parameters. `scripts/check-params-doc.sh` says `docs/params.json` and `docs/PARAMETERS.md` are up to date.

## Done (TODO list of the task)

1. **Sizes.** `kMaxParams` 2048; `kNumModSources` 38; `kNumModDestinations` 404 (now in `modulation.h`, was local to `param_table.cpp`); `kNumFxSlots` 16; `kNumWavetableChoices` 14; `kLfoShapeCustom` 12. Every place that assumed the old sizes was found by grep and fixed without behaviour change: `engine-vital` (`fx.order` permutation, LFO and wavetable lookups by key suffix would have indexed out of bounds for `lfo9`, `lfo10`, `fx.order.10..16`; source outputs; supportsParam), `vital_bindings` (`resolveFxOrder`), and the tests. New constants for what the old numbers still mean: `kNumKeysV2` = 880, `kNumLegacyFxSlots` = 9, `kNumModSourcesV2` = 28.
2. **604 keys appended** at the end of `kRaw[]` in block order B01..B12 (`core/src/param_table.cpp`, one macro per repeated group, `static_assert (kCount == 1484)`), plus all choice appends of §3 (wavetable, oscillator and noise destination, LFO shape, `rlfoN.style`, 14 filter models, 6 styles, 7 effects, `modNN.source`, destination). Golden `sbgolden::fingerprint (880)` = `0xd0dcc938695354b2`, computed from the table of `9c97a69` before any change (position, id, key, name, group, min, default, skew, interval, scale, unit, flags, decimals, choice names of the version 2 lists; stepped maxima left out because they grow).
3. **Mod sources / destinations** per §2 (`modulation.cpp`), `ModSlotParams` gained `auxSource`, `auxInv`, `auxPower`, `auxOutput`. Test: version 2 destinations keep 1..269, all seven new ranges (270-273, 274-289, 290-319, 320-337, 338-356, 357-365, 366-403) match the spec.
4. **State version 3.** `migrateV2ToV3` writes `fx.order.10..16` = 9..15 (derived values: not counted in `LoadResult::applied`, so `applied` of a v2 document is still 880 and the existing tests keep their numbers). New `content_state.h/.cpp` (`StateContent`, caps, `validateContent`), `saveState (values, content)`, `loadStateWithContent`, `LoadResult::content` (`absent / ok / rejected / ignored`), `savePreset` / `loadPreset` overloads with content, the JUCE and CLAP shells keep the content in their host state (A25). `loadState ()` stays allocation-free and skips the section. v1, v2 documents and the 100 factory `.sbpreset` (v1 container, v2 state) load as before.
5. **`docs/params.json`, `docs/PARAMETERS.md`** regenerated with `scripts/check-params-doc.sh --update` (paramdump). `kick-pages.json` not generated (as instructed).
6. **EngineCaps** (`engine_caps.h/.cpp`): 26 bits (one per area of WP1-WP8), `IEngine::capabilities ()` (default none), `requiredCaps (paramIndex)`, `requiredCapsForValue (paramIndex, value)` for the appended choice entries, `capName`. `supportsParam` is false for a new key until its bits are set (own engine: unchanged subset). Both engines accept and store every new key, ignore it, and never crash.
7. **Tests** (new: `test_new_keys.cpp`, `test_state_v3.cpp`, `test_engine_caps.cpp`; golden helpers `golden_v2.h`, `golden_audio.h`): counts (1484, 403 modulatable, 134 new), key / hash / display-name uniqueness and unique names per choice list, append-position golden, first-880 fingerprint, choice-append golden, version 2 -> 3 load of the byte-exact version 2 text (fingerprints of the old writer: defaults `0xac26fc69f32f2e64`, busy patch `0x83fd0940eca3bc0f`) gives the same 880 values and, in both engines, the same audio as commit `9c97a69` (bit-exact on Apple arm64 Release, segment RMS +-0.1 % elsewhere), 40 random round trips of all 1484 keys with exact floats and byte-stable text, content round trip, 41 rejection cases, malformed-JSON cases, size bound (largest legal state 402 KB), 500 mutated documents (loader verdict identical with and without content, no crash) plus truncation and junk, preset round trip.

## Deviations from the spec

* **`content` lives inside the state object**, not as a sibling of `state` in the `.sbpreset` (11 C3). One writer / parser for host state and preset; `13-new-keys.md` §5 and M4 corrected.
* **Malformed content is dropped, not fatal**: a section outside the caps / ranges is rejected as a whole (`content = rejected`), the parameters still apply; only broken JSON invalidates the document. Extra validation rules (path safety, hex hash, ranges) are listed in §5.
* **`import` section of 11 C3 is not part of WP0** (only `content`).
* **Engine behaviour for stored values beyond the engines' lists** (needed so `Preset table`, `Custom`, new filter models, Rossler, `Filter blend` / `None` do not push out-of-range values into Vital controls): the Vital adapter clamps them to the last implemented entry (new `Binding::maxIndex`), `Preset table` plays the saw, `Custom` the sine.
* **Positions inside blocks the spec left open** (B03 LFO-major, B10 sends source by source, B11 filter-major) and the display names / groups it did not give (Utility keys, B05, B07, `fxfilter` additions): fixed in the code, written down in the new §4.13 of the spec.

## What the spec got wrong (and the fix applied to `13-new-keys.md`)

1. §10 size estimate: "state text 1484 keys x ~30 B = ~45 KB" - the default patch is 29 KB (45 KB only with full-precision floats everywhere). Corrected.
2. §10 content estimate "< 200 KB worst case" - the largest legal content (10 shapes x 128 points with both point modulations, 255-byte names, 3 sample references with 128 slices, 64 KiB scl, 4 KiB kbm) measures 402 KB. Still far below 1 MiB. Corrected.
3. §5 / M4 / 11 C3: placement of `content` (see deviations). Corrected in §5 and M4.
4. §11 WP0 listed `kick-pages.json` as a WP0 output; it belongs to the Kick pages branch. Row corrected.
5. §6 / R5 silent on `kNumFxSlots`: raising it to 16 breaks every consumer that means "the nine effects of version 2"; `kNumLegacyFxSlots` added (§4.13).
6. The numeric tables (positions 880..1483 per block, destination ranges, totals 604 / 134 / 403) were verified by tests and are correct as written.

## Not done / left for later packages

* `kick-pages.json` (other branch). WP1-WP9 (events, DSP, UI): the keys are inert; every engine reports `capabilities () == kCapNone`.
* The JUCE shell and CLAP shell keep `StateContent` only for the host state round trip; the clap smoke test does not exercise content (the core tests do).
* Vital's destination values `Filter blend` / `None` map to the last implemented destination until WP5.
