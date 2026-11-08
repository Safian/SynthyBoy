# MODAMT report - modulation of connection amounts, per-connection curves, stereo source, stereo routing, voice override (block B20, G1-G6)

Branch `work-modamt` (merge of the Gemini branch `agy/modamt`, cut from d90e1a0, into `develop` bc376e4, then reviewed and completed).
Block **B20**: table positions **1594 .. 1595**, destination choices **453 .. 519**, capability bit **43** (`kCapModAmount`), assumptions **A197 .. A205**, spec section **4.21**,
morning-question rows **33, 34**. Table: 1596 keys (1594 before), `kNumModDestinations` 520, mod sources 39, Kick pages unchanged (118).

## 1. What B20 is

| Gap | What it became | Where |
|---|---|---|
| G1 amount of another connection | 64 appended destinations `mod01.amount .. mod64.amount` (choices 454 .. 517); Vital's own `modulation_N_amount` input; loops are not wired | `core/src/modulation.cpp` (`DestinationLists`), `core/engine-vital/adapter/vital_engine.cpp` (`wouldCloseSlotLoop`, `wouldCloseLoop`) |
| G2 per-connection curve | `content.mod_curves[]` (`{slot 1..64, points}`, <= 100 points, additive state v3), the connection's own line map in the adapter (`updateCurve`, kind 3); importer maps `line_mapping` | `content_state.h/.cpp`, `state.cpp`, `vital_engine.cpp`, `vital_import.cpp` |
| G3 portamento / polyphony / stereo routing | destinations 518, 519, 453 | `modulation.cpp`, `vital_engine.cpp` (`d.valid` accepts poly-only targets) |
| G4 source `stereo` | source choice 39 (`stereo`, bipolar) | `modulation.cpp`, `vital_engine.cpp` (`vitalSourceName`) |
| G5 `stereo_routing`, `voice_override` | keys `master.stereo_routing` (%, default 100, modulatable) and `master.voice_override` (Kill / Steal) bound to Vital's controls | `param_table.cpp`, `vital_bindings.cpp` |
| G6 `osc_N_transpose_quantize` | maps onto the existing `oscN.transpose_snap` (B14, 0 .. 8191); **no new key, no new cap bit** (per the coordinator note) | `vital_import.cpp` |

UI: the Slot Curve panel got a **Custom** button that shows the remap graph widget (`RemapEditor`, new mod curve mode: add / move / delete points, curve power, pencil,
presets, undo, reset) for the slot shown; the matrix destination menu has a "Mod Amounts" submenu (8 x 8, entries that would close a loop greyed), the row shows the
loop badge for amount loops and a star on the Crv button when a curve is drawn; `master.stereo_routing` / `master.voice_override` are on the Master voicing panel and on the
Kick page `master_expression`. Details and assumptions: `13-new-keys.md` 4.21 and A197 .. A205.

## 2. Merge into develop (renumbering)

Draft numbers -> final: table positions 1576/1577 -> 1594/1595; destinations 442 -> 453, 443..506 -> 454..517, 507 -> 518, 508 -> 519; cap bit 37 -> 43 (B15 holds 37 .. 42,
`kCapAllKnown` = bits 0 .. 43); assumptions A240..A248 -> A197..A205; section 4.19 -> 4.21; `kNumModDestinations` 509 -> 520; `kCount` 1578 -> 1596. Generated files
(`docs/params.json`, `PARAMETERS.md`, `kick-pages.json`, the 112 presets, `contentpack.json`) were regenerated with the repo tools after the conflicts were resolved
(`scripts/check-params-doc.sh`, `scripts/gen-factory-presets.py --check` both pass; no preset sounds different: the 112 presets only gained the two keys at their defaults).

## 3. Defects found in the Gemini work and what was done

| # | Defect | Fix |
|---|---|---|
| 1 | The plugin target was never built on the branch (`SYNTHYBOY_BUILD_PLUGIN` off): `ModCurvePanel.cpp` did not compile (`shapes::evaluateShape` does not exist, `std::erase_if` is C++20) although the report said the UI was done | rewritten (below); full build with the plugin and all UI tests is green |
| 2 | Free-draw "editor": ad hoc mouse code writing the content on every drag event, no undo, no add / delete of points, no tests; the curve preview ignored power and mapped bipolar sources wrongly | the existing remap graph widget got a mod curve mode (`RemapEditor::ModCurveMode`), the panel's Custom button shows it; the preview now draws line map -> power like Vital; 4 headless UI tests |
| 3 | Capability bit set without an off / on proof for `voice_override`, `master.portamento.time`, `master.polyphony`, the stereo routing modulation (the tests only counted wired slots; two used source choice 1, which is Env 1, as "Macro 1") | new audible tests for each (section 4) |
| 4 | Destination numbering: the 66 appended destinations were added behind **all** modulatable keys, so the next modulatable key of any later block would have shifted choices 443..508 that presets store | the appended group is anchored behind the last key of B20 (`kLastIndexOfB20`), later keys continue at 520; `test_new_keys.cpp` pins every number |
| 5 | The cycle check was only partly tested (one 2-cycle), no recovery test, no allocation test | `mod_cycles_are_neutral_and_recover` (self loop, a 3-slot chain that is fine, the loop that closes it, opening it again; allocations of the audio thread counted: 0) |
| 6 | The curve test only compared two renders; orientation (y up) and "before the amount" were unproven | a curve that is 0 everywhere equals the render without modulation, a curve that is 1 everywhere with the source at 50 % equals the straight line with the source at 100 %, a curve of another slot does not leak |
| 7 | UI: amount loops were not flagged, 64 amount destinations were dumped into the "Mod Matrix" menu group | `modAmountWouldLoop` / `modDetectFeedbackLoop`, "Mod Amounts" submenu with greyed loop entries, star marker on the Crv button |
| 8 | Docs: `stereo_routing` described as the stereo spread (it is Vital's decoder at the end of the chain, 0 % = centre signal in both channels), "O(1) cycle detection" (it is O(slots)), Kick page given as `master_voicing` (it is `master_expression`), the report claimed a UI test run and 45 ctest tests from a build without the plugin | rewritten from what the code does (A197 .. A205) |
| 9 | The state size test did not contain `mod_curves` (64 x 100 points) nor `remaps`; no content tests for `mod_curves` | added to the largest-legal-state test (worst case 795 707 of 1 048 576 bytes, 76 %, test bound lowered from "half" to 80 %), `fullContent` round trip, 8 rejection cases, 2 accepted cases |
| 10 | Importer: only stereo_routing as a direct key had a test | added destinations `stereo_routing`, `portamento_time`, `polyphony`, `modulation_64_amount` |

Not defects, kept: the engine's own check `wouldCloseLoop` (Vital `isDownstream`) stays the second line; `wouldCloseSlotLoop` runs first and decides the amount chains.

## 4. Tests

- `synthyboy_test_vital_modamt` (15 cases, all green, includes the allocation hook): capability bit; destination numbers; amount modulated by a macro (audible); amount driven by the note
  source follows the key; curve before the amount (3 exact equivalences); curve of another slot does not leak; cycles neutral and recoverable (0 allocations); stereo source
  (channels differ); stereo routing 100 % wide / 0 % mono and modulated by a macro (side energy drops below 5 %); voice override Kill vs Steal audible with polyphony 2;
  portamento time modulated by a macro (glide audible); polyphony modulated 8 -> 1 (audible).
- `synthyboy_test_ui_wtremap` (10 cases): amount destinations and loop detection (`uim_amount_destinations_are_found_and_loops_are_flagged`), the curve editor draws into
  `content.modCurves` per slot, reset removes the entry, preset load is picked up (`uim_slot_curve_editor_draws_into_the_host_content_per_slot`), fits phone and tablet,
  the two master keys are on a panel, screenshots (`/tmp/ui-shots/wtremap/mod-slot-curve-*.png`).
- Core / import: `test_new_keys`, `test_params`, `test_engine_caps` (bit 43, source 39, destinations 453 / 454 / 519), `test_fxextra_keys`, `test_shell_clap` (all 1596 keys reachable),
  `test_state_v3`, `test_vital_import` (`vi_mod_amount_stereo_source_and_line_mapping_imported` extended).
- Full `ctest`: see the last section.

## 5. Importer on the 35 real presets

`scripts/audit-vital-import.py` on the 35 presets (the loose files and the unzipped vitalbank, outside the repo): **35 presets, 0 mismatches, errors 0**, 17852 value rows,
17287 engine read-backs equal; dropped items 5 (all sample objects, G7). 53 connections with a `modulation_N_amount` destination in 17 presets are imported (53 of 53), the
`stereo` source connection, `macro_control_4 -> stereo_routing` and 31 `line_mapping` curves are carried; classes of the "ignored" list unchanged. **PAD - Hanging Gardens of Ra**: 0 dropped,
0 approximated (before 14 dropped); two renders with macro 1 at 0 and 100 % differ by 19.6 % of the signal RMS: the macro now drives the amounts. G7 and G8 untouched.

## 6. Open

- Serum's slot RISE / FALL slew is not built (morning question 33). `master.stereo_routing` overlaps `master.stereo_spread` (row 34).
- The Velocity / Note curve panel still shows a stale GAP badge (`ModVelNotePanel.cpp:225`), not part of this block.
