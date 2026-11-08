# Work Package MODAMT Report – Modulation Amount, Curves, Stereo Routing, Voice Override (B20, G1-G6)

Branch: `agy/modamt`
Worktree: `/Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-modamt`
Report: `docs/ui-design/MODAMT-REPORT.md`
Block: **B20** (positions 1576..1577)
Assumptions: **A240 .. A248**
Capability Bit: `kCapModAmount` = `1ull << 37`

---

## 1. Summary of Completed Work

This work package resolves gaps **G1 through G6** from `docs/ui-design/VITAL-IMPORT-AUDIT-REPORT.md` and implements full engine, parameter table, additive state v3, UI, Kick remote control pages, importer mapping, and test suites for modulation amount destinations, per-connection modulation curves, stereo modulation source, stereo routing, voice override, and quantize transpose import.

### G1 – Modulate Amount of Another Modulation Connection (`modNN.amount`)
- **Modulation Destinations**: Added 64 modulation destinations corresponding to `mod01.amount` through `mod64.amount` at destination choices 443..506.
- **Engine Adapter**: Wired in `sbvital::VitalEngine` (`core/engine-vital/adapter/vital_engine.cpp`). `applyParams()` dynamically resolves mono modulations directed to another slot's amount control.
- **Cycle & Self-Modulation Protection**: In `wouldCloseSlotLoop()`, direct self-modulation (`modK` modulating `modK.amount`) and multi-hop cyclic modulation loops (`modA` -> `modB.amount` -> `modA.amount`) are detected in $O(1)$ without allocating, and rejected/neutralized so base amount is preserved and infinite recursion or graph feedback cycles are prevented.
- **Audible Difference**: Verified with dedicated test `synthyboy_test_vital_modamt` case `mod_amount_modulation_audible_difference` (LFO modulating slot amount produces distinct RMS difference).

### G2 – Per-Connection Modulation Curve (`line_mapping`)
- **State v3 Additive Content**: Added `ContentModCurve` (`kMaxContentModCurves = 64`, `kMaxModCurvePoints = 100`) to `StateContent` (`core/include/synthyboy/content_state.h`). Stored under `"mod_curves"` in state JSON.
- **Engine Adapter**: Each slot's `LineGenerator` reads and applies `content.modCurves[k]` to the source value prior to modulation amount scaling.
- **UI Curve Editor**: Integrated multi-point bezier/power curve editor in `shell-juce/ui/panels/ModCurvePanel.cpp` reusing graph editing logic. GAP badge removed.
- **Vital Importer**: `parseLfoLine` parses Vital's `line_mapping` objects and serializes them into `content.modCurves`. Across the 35 real Vital presets, 31 `line_mapping` curves are imported with 0 mismatches.

### G3 – Modulation of Portamento Time, Polyphony, and Stereo Routing
- **Destination 507 (`master.portamento.time`)**: Modulates portamento glide time in engine.
- **Destination 508 (`master.polyphony`)**: Modulates active polyphony count in engine (enabling dynamic mono/poly transitions like Macro 4 in *LD - Nostalgia*).
- **Destination 442 (`master.stereo_routing`)**: Modulates master stereo spread/routing width.
- Bound in `resolveModulation()` and verified with `portamento_and_polyphony_modulation_destinations`.

### G4 – Modulation Source `stereo`
- Added modulation source choice 39 (`stereo`, 0-based index 38) in `core/include/synthyboy/modulation.h` and `core/src/modulation.cpp`.
- Engine adapter maps source to Vital's internal `"stereo"` source (bipolar $\pm 1$ per-channel sign source: $+1$ left, $-1$ right). Verified with `stereo_modulation_source_produces_stereo_difference`.

### G5 – Keys `master.stereo_routing` and `master.voice_override` (Block B20)
- Appended trailing Block B20 at table positions 1576..1577:
  - 1576: `master.stereo_routing` (0..100 %, default 100 %, continuous, `kMod`).
  - 1577: `master.voice_override` (choice `kVoiceOverrides`: Kill = 0, Steal = 1, default Kill, `kAuto`).
- Bound in `core/engine-vital/adapter/vital_bindings.cpp` to Vital's `"stereo_routing"` and `"voice_override"` controls.
- Added controls to `shell-juce/ui/panels/MasterVoicingPanel.cpp`.
- Added to Kick Page 66 (`master_voicing`).

### G6 – Oscillator Transpose Quantize Import
- Vital's `osc_N_transpose_quantize` integer scale bitmask (0..8191) imported into SynthyBoy's `oscN.transpose_snap` (Block B14, positions 1512, 1524, 1536).

---

## 2. File & Line Changes

| Area | File | Details |
|---|---|---|
| Core Specs | `core/include/synthyboy/modulation.h` | `kNumModSources` = 39, `kNumModDestinations` = 509 |
| Core Specs | `core/include/synthyboy/content_state.h` | `struct ContentModCurve`, `kMaxContentModCurves = 64`, `kMaxModCurvePoints = 100`, `modCurves` in `StateContent` |
| Core Specs | `core/include/synthyboy/engine_caps.h` | `kCapModAmount = 1ull << 37`, updated `kCapAllKnown` |
| Core Specs | `core/include/synthyboy/vital_import.h` | `modCurves` in `ImportResult` |
| Core State | `core/src/content_state.cpp` | `validateContent` and `operator==` updated for `modCurves` |
| Core State | `core/src/state.cpp` | JSON parser and writer support for `"mod_curves"` |
| Param Table | `core/src/param_table.cpp` | Block B20 keys at 1576 (`master.stereo_routing`) and 1577 (`master.voice_override`); static asserts updated (`kCount == 1578`) |
| Modulation | `core/src/modulation.cpp` | Added `stereo` source; `DestinationLists` populates table modulatable params (1..442), `modNN.amount` (443..506), `portamento.time` (507), `polyphony` (508) |
| Engine Caps | `core/src/engine_caps.cpp` | Mapped B20 keys, source 39, and destinations $\ge 443$ to `kCapModAmount` |
| Kick Pages | `core/src/kick_pages.cpp` | Added B20 keys to Page 66 |
| Vital Adapter | `core/engine-vital/adapter/vital_bindings.cpp` | Added bindings for `master.stereo_routing` and `master.voice_override` |
| Vital Adapter | `core/engine-vital/adapter/vital_engine.cpp` | Added `kCapModAmount` to `capabilities()`; mapped `"stereo"` source (index 38); wired mono mod destinations $\ge 443$; `LineGenerator` curve evaluation; $O(1)$ cycle rejection in `wouldCloseSlotLoop()` |
| Vital Import | `core/vital-import/vital_import.cpp` | Destination mapping for `modNN.amount` and `stereo_routing`; parsed `line_mapping` into `res.modCurves`; mapped `stereo_routing`, `voice_override`, and `osc_N_transpose_quantize` |
| Shell UI | `shell-juce/ui/model/ModModel.cpp` | Updated `isTarget` and `beginDrag` to support non-param destinations (`modDestinationChoiceOf > 0`) |
| Shell UI | `shell-juce/ui/panels/MasterVoicingPanel.cpp` | Added `master.stereo_routing` and `master.voice_override` UI controls |
| Shell UI | `shell-juce/ui/panels/ModCurvePanel.cpp` | Multi-point curve editing and rendering wired to `content.modCurves`; GAP badge removed |
| Tests | `core/engine-vital/tests/test_modamt.cpp` | Comprehensive test suite for all B20 / G1-G6 features (8/8 passed, 132 checks) |
| Tests | `core/tests/test_new_keys.cpp` | Updated key counts (1578), sources (39), destinations (508) |
| Tests | `core/tests/test_params.cpp` | Updated parameter count golden assertions |
| Tests | `core/tests/test_engine_caps.cpp` | Added bit 37 mask |
| Tests | `core/tests/test_fxextra_keys.cpp` | Updated parameter count assertion to 1578 |
| Tests | `core/tests/test_shell_clap.cpp` | Updated reachable keys assertion to 1578 |
| Tests | `core/vital-import/tests/test_vital_import.cpp` | Verified import of mod amounts, curves, and stereo routing |
| Docs | `docs/params.json`, `docs/kick-pages.json`, `docs/PARAMETERS.md` | Regenerated |
| Presets | `content/presets/*.sbpreset` | Regenerated 112 factory presets with neutral B20 defaults (`python3 -I scripts/gen-factory-presets.py --check` passes) |
| Audit Script | `scripts/audit-vital-import.py` | Added handling for B20 keys, `mod_curves` curve checks, and stem fallback |
| Docs | `docs/ui-design/13-new-keys.md` | Added Section 4.19 (B20) and assumptions A240..A248 |
| Docs | `docs/ui-design/VITAL-IMPORT-AUDIT-REPORT.md` | Updated Section 8 to mark G1-G6 resolved; updated audit stats |
| Audit Audit | `docs/ui-design/UI-GESTURE-AUDIT.md` | Row 06-10 updated with file:line |

---

## 3. Test Verification & Gate Results

### 3.1 Dedicated MODAMT Suite (`synthyboy_test_vital_modamt`)
```
[ RUN  ] mod_amount_capability_bit
[  OK  ] mod_amount_capability_bit
[ RUN  ] mod_amount_modulation_audible_difference
[  OK  ] mod_amount_modulation_audible_difference
[ RUN  ] mod_self_modulation_and_cycles_rejected
[  OK  ] mod_self_modulation_and_cycles_rejected
[ RUN  ] per_connection_modulation_curve_audible_difference
[  OK  ] per_connection_modulation_curve_audible_difference
[ RUN  ] stereo_modulation_source_produces_stereo_difference
[  OK  ] stereo_modulation_source_produces_stereo_difference
[ RUN  ] master_stereo_routing_control_and_modulation
[  OK  ] master_stereo_routing_control_and_modulation
[ RUN  ] master_voice_override_control
[  OK  ] master_voice_override_control
[ RUN  ] portamento_and_polyphony_modulation_destinations
[  OK  ] portamento_and_polyphony_modulation_destinations
8 test case(s), 132 check(s), 0 failed case(s), 1.06 s
```

### 3.2 Vital Import Audit (All 35 Presets)
```
files audited: 35
value rows checked: 17852, mismatches: 0
expected deviations noted: {}
engine control read-back equal to the file: 17287
ERRORS: 0  {}
```
*PAD - Hanging Gardens of Ra* previously had 14 dropped connections (13 mod amount + 1 stereo routing). Now has **0 dropped connections**; macro 1 successfully drives mod amounts and renders at expected level.

### 3.3 Factory Presets Check
```
python3 -I scripts/gen-factory-presets.py --check
factory presets up to date (112 presets)
```

### 3.4 Full CTest Test Suite
```
100% tests passed out of 45
Total Test time (real) = 122.49 sec
```

---

## 4. Assumptions (A240 .. A248)

- **A240**: `modNN.amount` modulation: Direct self-modulation (`modK` modulating `modK.amount`) and cyclic dependency chains (`modA` -> `modB.amount` -> `modA.amount`) are detected before evaluation and treated as neutral (amount modulation ignored, base amount preserved).
- **A241**: Modulation curves (`line_mapping`) stored in additive `content.mod_curves[]` (up to 64 curves, up to 100 points `{x, y, power}` matching LFO shape format). Applied to source value before amount.
- **A242**: Backward-compatible destination indexing: non-parameter modulatable destinations (`modNN.amount`, `master.portamento.time`, `master.polyphony`) are appended in `DestinationLists` after all table-modulatable parameters. `master.stereo_routing` is placed in `kRaw[]` at pos 1576 with `kMod` (choice 442). Choices 443..506 are `mod01.amount` .. `mod64.amount`, choice 507 is `master.portamento.time`, choice 508 is `master.polyphony`. Total destinations = 508 (choiceCount = 509).
- **A243**: `stereo` modulation source (choice 39): per-voice left/right channel sign source (+1.0 left, -1.0 right). Evaluates to 0.0 in mono rendering.
- **A244**: `master.stereo_routing` (pos 1576, 0..100%, default 100%, `kMod`): controls stereo routing spread across voice chain.
- **A245**: `master.voice_override` (pos 1577, choice Kill/Steal, default Kill, `kAuto`): sets voice stealing policy when voice limit is reached.
- **A246**: `master.portamento.time` (choice 507) and `master.polyphony` (choice 508) modulation: bound in engine adapter to Vital's `"portamento_time"` and `"polyphony"` modulation destinations.
- **A247**: `osc_N_transpose_quantize` (G6): Vital's integer transpose quantize bitmask (0..8191) imported directly into SynthyBoy's `oscN.transpose_snap` (Block B14, positions 1512, 1524, 1536).
- **A248**: Capability bit `kCapModAmount` = `1ull << 37`: set on `sbvital` engine adapter to indicate support for mod amount modulation, mod curves, stereo source, stereo routing / voice override, and portamento/polyphony modulation.

---

## 5. Open Questions

None. All items (engine, keys, Kick pages, UI panels, importer mapping, test suites) are complete and verified with passing gates.
