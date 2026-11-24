# SHELLFEAT report: MIDI learn (G07), Save as Default (G02), module Lock (G08), Vital bank / table / LFO files

Branch `worktree-agent-a16274e897ab7ba30`, taken over from `agy/shellfeat` (Gemini, stopped by quota after one commit). Spec and assumptions A180 .. A196:
[15-shell-features.md](15-shell-features.md). No parameter keys were added (table, key count 1484 and the factory presets are untouched).

## What the agy commit really contained

`git diff develop...agy/shellfeat`: only **item 3, partly**: `core/include/synthyboy/zip_reader.h`, `core/vital-import/zip_reader.cpp` (a complete stored +
inflate reader, reviewed line by line: bounded buffers, CRC-32, exact-size streams, zip64 / encryption refused), `core/vital-import/vital_batch.cpp`
(`importVitalDocument`, `importVitalBank`, LFO shape serialiser) and the declarations in `vital_import.h`. What was **missing / wrong**:

- `vital_batch.cpp` was **not in the CMake source list** (the library did not link the new functions), no CLI change, **no test at all**.
- `PresetStore::exists (name, srcTag)` was asked without the bank folder, so two banks with one preset name collided; the name now carries the folder.
- The wavetable store call did `hasWavetable || addWavetable` and then a second `addWavetable` for the reference (confusing, one store call is enough: the store
  dedups by SHA-256).
- Items 1 (MIDI learn) and 2 (Save as Default, Lock) were **not started** (the UI still had a disabled "Lock module" and a `canMidiLearn() == false` hook).

## What was built

| Item | Where | Notes |
|------|-------|-------|
| shell core | `core/include/synthyboy/shell_features.h`, `core/src/shell_features.cpp` | `MidiCcMap` (atomic CC table, learn), lock helpers (`keepLockedValues`, `keepLockedContent`), the `shell` state section writer / bounded reader, user default file (atomic write) |
| G07 shell-juce | `shell-juce/PluginProcessor.cpp:263-278` (CC in `processBlock`), `:576-612` (learn API), `ui/Sheet.cpp:85-150`, `ui/Overlays.cpp:107-125`, `ui/UiContext.cpp refresh` | learn from the long-press sheet, LEARN frame / `CCn` tag on the knob, saved in the state |
| G07 native CLAP | `shell-clap/synthyboy_clap.cpp:228-262` | CC from `CLAP_EVENT_MIDI` sets the value and goes to `out_events`; map from state / user default (no editor there) |
| G02 | `PluginProcessor.cpp:50-66,613-622`, `ui/panels/GlbSettingsPanel.cpp:42-52,157-190`, `synthyboy_clap.cpp:85-100` | Save as Default / Clear Default buttons, Init Preset = default, new instances of both shells start from it |
| G08 | `core/src/shell_features.cpp` lock helpers, `PluginProcessor.cpp:447-480,484-520`, `ui/ModuleSheet.cpp:102-116,179-185`, `ui/GestureKit.cpp ModuleMenuButton` | the module sheet button is real (Lock / Unlock), header button reads LOCK, content of the module is kept too |
| Vital bank | `core/vital-import/CMakeLists.txt` (vital_batch.cpp added), `tools/vital-import/vital_import_cli.cpp` (rewritten on `importVitalDocument` / `importVitalBank`) | `.vitalbank`, `.vitaltable`, `.vitallfo`, directories of them, `--duplicates` |

## Tests (all new ones are in ctest)

- `core/tests/test_shell_features.cpp` (`synthyboy_test_shell_features`, 9 cases, 4380 checks): table, scaling, stepped snap, learn rules, reserved CCs, bindings
  round trip, locks on values and content, shell section round trip / absent / rejected / truncations, user default round trip / damaged file.
- `core/tests/test_shell_clap.cpp` (+2 cases): the native shell applies a mapped CC and reports it on `out_events`, saves the section, keeps a locked module on a
  preset-like load, and starts from the user default.
- `shell-juce/ui/tests/test_ui_shellfeat.cpp` (`synthyboy_test_ui_shellfeat`, 9 cases, 96 checks): learn through the sheet with real MIDI blocks (reserved CC
  skipped, first CC mapped without moving the knob, omni channel, scaling, ring shows `CC21` / learning, remove, cancel, re-map), state round trip of map and
  locks incl. session vs preset, lock on parameters and on the LFO shape content, module sheet / header button, Save as Default -> new instance -> Init Preset
  -> locks survive Init -> Clear Default, broken default file. Screenshots (phone 390x844, tablet 1024x768): `/tmp/ui-shots/shellfeat/`
  (`phone-filter-learn`, `tablet-filter-learn`, `phone-osc-locked`, `phone-settings-default`, `tablet-settings-default`).
- `core/vital-import/tests/test_vital_bank.cpp` (`synthyboy_test_vital_bank`, 11 cases, 119 checks): CRC-32 vector, a zlib-made dynamic-Huffman stream, a test-side
  fixed-Huffman encoder and zip writer (stored, deflate, stored blocks), refusals (not a zip, truncation, CRC, encryption, method, limits, bomb, unsafe names,
  every prefix and every 5th flipped byte), batch kinds, stand-alone table / LFO, a synthetic bank (presets, table, shape, readme, `../evil`, broken file), duplicate
  policies, bank refusals, and an optional real-bank case (`tests/vital-presets/*.vitalbank`).
- Existing test adapted: `test_ui_gaps.cpp` expected the Lock button disabled (the GAP); it now expects it enabled.

## Real bank

`synthyboy_vital_import --in tests/vital-presets/Black_Lotus_Audio_-Division_DnB.vitalbank --out <dir>` (exit 0):

```
Black_Lotus_Audio_-Division_DnB.vitalbank: bank, 25 entries
summary: 1 input files (1 banks), 25 entries: 25 presets, 0 wavetables, 0 lfo shapes imported; 0 skipped (same source), 0 need a decision, 0 failed, 0 ignored entries
presets: fidelity exact=0 close=4 degraded=21; items imported=12768 approximated=86 dropped=71; wavetables=75 samples=1
```

A second run on the same output: `25 skipped (same source)`. The bank is gitignored and was not committed.

## Verification

Incremental builds per item, then one full `cmake --build build -j8` (no compiler warning; the linker prints its usual "ignoring duplicate libraries") and one
`ctest -j8`: **49 / 49 passed** (includes the 3 new suites and all existing ones; `scripts/gen-factory-presets.py --check` via `synthyboy_factory_presets_reproducible`
passed, `synthyboy_test_keys` unchanged). The ASan config was not run: the audio-thread code added (`MidiCcMap::handleCc`, the CC branch of `processBlock`) is
atomics plus the existing buffers only.

## Open questions (14-morning-questions.md rows 20 .. 23)

MIDI scope (omni, 7 bit), `modNN` slots not part of a module lock, `.vitallfo` has no editor consumer yet, and Kick's preset browser must call
`UiHost::loadPatch` for locks to apply (it is not in this repository).
