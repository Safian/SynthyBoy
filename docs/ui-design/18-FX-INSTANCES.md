# 18 - Several instances of one effect (owner decision 2026-11-09, WP-19 item 3)

Decision: the synth supports several instances of the same effect and an arbitrary module count per rack (Main, Bus 1, Bus 2), as Serum 2 does. This file is the design; the implementation worker follows it and records deviations in `FXINSTANCES-REPORT.md`.

## 1. Problem

Every effect today is one fixed module with its own flat keys (`chorus.*`, `delay.*`, ...; 16 modules, order in `fx.order.1..16`, rack in `<fx>.rack`). A second delay would need a second copy of every key. The flat table has 1603 of 2048 keys used, so copying keys per instance does not scale.

## 2. Design: a pool of generic instance slots

1. **The 16 existing modules stay exactly as they are** (keys, ids, presets, Kick pages). They are "instance 0" of their type and cannot be removed, only bypassed and moved between racks.
2. **A pool of 16 extra slots `fxi01 .. fxi16`.** Per slot these keys (block B22, positions after the current last key):
   - `fxiNN.type` choice: None, then every module type that has parameters (the 9 Vital effects, Hyper/Dimension, Utility, Bode, Convolve; not the splitters, see 7).
   - `fxiNN.on` bool, `fxiNN.rack` choice (Main, Bus 1, Bus 2), `fxiNN.order` int (position inside the rack; ties broken by slot number).
   - `fxiNN.p01 .. p16` float 0..1 **normalised** parameters. What parameter k means is defined by the type: the k-th key of that type's existing key list (`chorus.*` in table order, skipping `.on`, `.rack`, `.level` handled as a normal parameter). Range, curve, unit, default, labels and choice names are taken from the `ParamInfo` of that source key (single source of truth: no second table of ranges). Types with more than 16 parameters (check Hyper/Dimension, Convolve, Compressor): the descriptor lists the first 16 and the worker documents any parameter that does not fit; if more than a few do not fit, raise P for all slots to 24 (key budget below).
   - Cost: 16 x (4 + 16) = 320 keys (16 x 28 = 448 with P = 24).
3. **`kMaxParams` raised from 2048 to 4096** (the worker verifies every dependent: state arrays, JUCE/CLAP parameter exposure, mod destination index width, `ParamValues`, golden tests; ids stay `hashKey(key)` and the first 880 keys stay frozen). Reason: 1603 + 320 + the remaining backlog (about 200) exceeds 2048.
4. **Type descriptor table** (`core/src/fx_types.cpp`, one function `fxTypeDescriptor(type)` giving param count and the source key per parameter, derived at startup from the param table, plus an engine factory id). The UI, Kick and the engine all use it.
5. **Engine.** The racks are built from instance-0 modules plus the active pool slots, ordered by `fx.order.*` / `fxiNN.order` per rack. Each active slot owns a processor object of its type, created off the audio thread and handed over atomically (same pattern as the Convolve builder thread, deterministic and synchronous in offline mode `Quality::realtime == false`); until the object is ready the slot passes audio through dry, and a type change crossfades 10 ms. No allocation on the audio thread. A pool slot with type None costs nothing. Memory: processors are created only for active slots. The reused code is the existing processor classes (Vital effect processors need a second instance inside the sound engine chain: the worker records the `docs/UPSTREAM.md` changes; the Serum-only effects in `core/fx-serum-a/b` are standalone classes and instantiate freely).
6. **Modulation and automation.** `fxiNN.pNN` are modulatable when the source parameter is modulatable (destination count grows by up to 256; the worker checks the destination index width and `kNumModDestinations`); they are automatable by the host like any key (generic host name "FX Slot NN P kk"; the UI shows the true labels from the descriptor).
7. **Splitters** (`fx_splitter_*`) stay single instance 0 modules in this step; allowing splitters in the pool needs band-rack semantics and is listed as a follow-up in the report.
8. **UI.** The FX page shows the three racks as strips of module cards (instance 0 and pool modules mixed, drag to reorder and to move between racks), "+" in each strip opens the type picker and claims the first free slot, long-press menu: duplicate (copies the parameters), remove (sets the type to None), bypass, copy/paste/swap/init (existing ModuleSheet operations work on a pool slot through the descriptor). A card opens the same panel of its type, bound to the slot's `pNN` keys instead of the type's own keys (the panels take a "key set" parameter: the worker introduces a small `FxKeySet` indirection, so one panel class serves instance 0 and any slot).
9. **Kick.** Pages are static, so Kick sees generic names; one page per slot (16 pages: type, on, rack, order, p01..p16 split over two screens if the page size demands). Dynamic names via the descriptor are a Kick-side feature (open question in the report: the paramdump can export the descriptor table).
10. **Presets and state.** Keys only, no content section needed. Defaults: all slots None/off, so the 112 factory presets sound identical (regenerate them with the new keys at default). Vital import: untouched. Lock/Copy/Paste of racks include pool slots.

## 3. Proof obligations (tests)

- For every type: a pool slot of that type with the parameters of instance 0 (normalised values equal) renders **bit-identical** (or within -120 dB) to instance 0 alone; this proves the descriptor mapping for all keys.
- Two instances of one type in series differ from one instance and from none; order within a rack changes the sound; rack assignment (Main / Bus 1 / Bus 2) routes correctly.
- All 16 slots active with the heaviest types: no audio-thread allocation, CPU within the tier budget, no NaN under the extreme-settings sweep.
- Type change while sounding: no click above -60 dBFS, no crash under TSan.
- State round trip, preset round trip, host automation of a `pNN`, modulation of a `pNN`, Kick page coverage (every key reachable), UI headless tests for add / remove / duplicate / move / bypass on phone and tablet.
