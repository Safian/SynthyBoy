# FXINSTANCES report - several instances of one effect (G73, design 18-FX-INSTANCES.md) (2026-11-09)

Branch `work-fxinst`, cut from develop `c52c602` (table 1603 keys, 520 destinations, cap bits to 49, 119 Kick pages). Spec: `13-new-keys.md` 4.23 (block **B22**, positions 1603..2050), assumptions A290 .. A298, morning-question rows 39 .. 43. Manual reference for the rack behaviour: Serum 2 FX chapter (racks, modules in any number).

## Result

Table **2051 keys** (+448), `kMaxParams` **4096**, `kNumModDestinations` 904 (choices 520 .. 903 are the 384 slot parameters), cap bit **50** `kCapFxInstances` (`kCapAllKnown` = 2^51 - 1), Kick pages **135** (+16), state version 3 unchanged, `params.json`, `PARAMETERS.md`, `kick-pages.json` and the 112 presets regenerated (`gen-factory-presets.py --check`, `check-params-doc.sh`, `paramdump --kick-pages` all pass).

| Item | Where |
|------|-------|
| (1) `kMaxParams` 2048 -> 4096 | `params.h` (static_assert for the uint16_t indices). Dependents checked: `ParamValues` (16 KB), `state.cpp` Parsed, `params.cpp` IdIndex, `modulation.cpp` destination lists, `vital_engine` / `own_engine` arrays, `PluginProcessor` arrays (sizes only; no file format holds it, ids are `hashKey(key)`, the first 880 keys unchanged: the golden v2 fingerprint test passes) |
| (2) descriptor table | `core/src/fx_types.cpp` / `fx_types.h`: `fxTypeDescriptor`, `fxInstanceKeys`, `fxInstanceSource`, normalise / plain / defaults |
| (3) pool keys, **P = 24** | `param_table.cpp` `SB_FXI`. Counted: Chorus 12, Compressor **24**, Delay 13, Distortion 9, EQ 13, FX Filter **18**, Flanger 9, Phaser 11, Reverb **19**, Hyper 8, Utility 10, Bode 14, Convolve 13. With 16, three types would not fit (8 + 3 + 2 parameters), so P = 24 for all slots (A290) |
| (4) engine | `reorderable_effect_chain.*` (`FxInstanceModule`, pool slots, rack items sorted by order, 10 ms crossfade, retire ring), `vital_fx_pool.*` (builder thread, settle, atomic handover; offline: the audio thread waits for the builder in the same block), `vital_engine.cpp` (`applyPool`, `handoverPool`) |
| (5) modulation, automation | slot parameters are ordinary host parameters; destinations 520 .. 903; a handover unwires and rewires the matrix slots (`disconnectPoolSlots`) |
| (6) Kick | `kick_pages.cpp`: pages `fxi01 .. fxi16` (type, on, rack, order, p01 .. p04 on the page, p05 .. p24 behind More) |
| (7) UI | `FxKeySet` (model), `ParamHandle` / `UiContext::resolve`, `FxRackView.h` (strips), `FxChainPanel.h` (the panel of the selected slot below the strips, built from the registry with the key set), `ModuleSheet` family `fxiNN` (+ Bypass / Duplicate / Remove row), `FxRackOps`, core `fx_rack.h` (the edits as pure functions) |
| (8) tests | below |
| (9) docs | `13-new-keys.md` 4.23 + A290 .. A298, `14-morning-questions.md` 39 .. 43, `UI-GESTURE-AUDIT.md`, `UPSTREAM.md` (chain, sound engine), this file |

## Tests (section 3 of the design)

* `synthyboy_test_vital_fxinstances` (`test_fxinstances.cpp`): a slot of **every type with random values on all parameters renders bit-identically (relative difference 0) what instance 0 renders**; **each of the 190 parameters alone** (descriptor mapping, tolerance 1e-4 for the short note); two instances of one type differ from one and from none, instance 0 + slot equals two slots; order inside a rack (slot orders and against the positions of instance 0); racks Main / Bus 1 / Bus 2 route, bus and main bypass; off / none / tier mask; type change while sounding: eight replacements, largest deviation from the unchanged render **-138 dBFS** (limit -60); realtime mode passes dry until the module is ready; host automation of a `pNN`; modulation of a `pNN` (macro -> slot parameter), survives replacing the module; 16 heavy slots with min / max / random settings stay finite; the edits of `fx_rack.h` give the order the engine plays.
* `synthyboy_test_vital_fxinstances_rt` (`test_fxinstances_rt.cpp`): **0 allocations on the audio thread** during 600 blocks with 16 active slots and type changes (offline and realtime); CPU with 16 heavy slots 0.31 ms mean per 128-frame block on this machine (budget 2.667 ms); handover under load with a second thread changing types and parameters. **ThreadSanitizer** (separate build `-fsanitize=thread`): clean for this test after the fix below.
* `synthyboy_test_fxinstance_keys`, `synthyboy_test_fxrack` (core): block positions, defaults, descriptor, caps, state round trip; the edits (random walk of 4000 add / remove / duplicate / move operations against a hand kept model).
* `synthyboy_test_ui_fxinst` (`test_fxinstances_ui.cpp`): key set, handles, every panel of a type resolves all its keys and covers every parameter of the type, a slot panel edits the slot only, the racks on phone and tablet (add / remove / duplicate / move / bypass, pool full), gestures, module sheet on a slot, assign by touch, state round trip, screenshots (`/tmp/ui-shots/fxinst/`).
* Existing: `uig_fx_chain_slot_power_reorder_and_bypass_alternatives` rewritten for the cards; counts of the keys, caps, params, Kick pages updated. Full ctest once at the end: **61 / 61 passed**. `state_random_walk_saves_loads_and_renders_the_same` needed no change.

## Defects found

1. **Data race (ThreadSanitizer)**: the builder thread's settle blocks read the engine's key track / tempo outputs while the audio thread wrote them (and the note counter). Fixed: `FxInstanceModule` reads copies of its own that the audio thread refreshes in `run ()` and that `prime ()` sets from values the requesting audio thread read.
2. **Allocation on the audio thread** (found by the existing sweep tests): the first version built a module on the calling thread offline. Now the builder thread builds and the audio thread waits (the Convolve pattern).
3. A free running LFO (chorus, flanger, phaser) of a module is not reset by `reset ()`: a slot equals instance 0 only for equal processing history (A295); the tests give both the same.
4. `ReorderableEffectChain::clone ()` was never used and cannot copy the pool's ring: it asserts now.
5. The Reverb of Vital at mix 0 is not exactly dry (3.5 % in instance 0 as well): not a defect of the pool, noted because the click test cannot use it.

## Deviations from the design

* P = 24 (the design allowed it; decided by counting).
* The order scale has a base (q * 64 + 32) so a slot can sit in front of the first module.
* The FX page shows the strips *and keeps* the 17 panels of instance 0 as tiles (they are the panels of instance 0); the panel of a pool module opens inside the racks tile, not as a separate tile.
* Strips wrap their cards into rows instead of scrolling sideways (touch friendly on 390 pt).
* "Lock / Copy / Paste of racks include pool slots": done per module (module sheet of `fxiNN`: copy, paste, swap, init, lock); there is no whole-rack copy.
* Modules in the band racks of a splitter are listed in their own strips (no "+", no drop target).

## Left / open

* Splitters in the pool (follow-up, morning question 41); per instance content (40); Kick dynamic names (42); EQ analyser / compressor meters for slots (43).
* No sanitizer run beyond TSan of the rt test; ASan not run.
* Timing of the CPU test is machine dependent (skipped under sanitizers).
* `agy/filters80` was not touched; filter model lists flow into slots automatically through the descriptor.
