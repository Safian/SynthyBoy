# Report: Osc header dropdowns touch & focus fix (`agy/oschdr-focus`)

## Summary of Changes (point by point)

1. **Header form `ignoreMousePresses: true` (`components/native/header/header.py:526`)** — DONE
   - Updated `header_definitions` to call `_definition(comps, ignore_presses=True)` so every generated header form definition (`oscHeaderAbc`, `oscHeaderSub`, `oscHeaderNoise`, `oscHeaderNoiseSample`, `envHeader`, `lfoHeader`, `curveHeader`, `modHeader`, `tuningHeader`, `voiceHeader`, `masterHeader`, `macroHeader`) sets `"ignoreMousePresses": true` while keeping `"disableCoarseDataWheel": false` and keeping `ignoreMousePresses: false` on action-bearing cell definitions.
   - Updated `header.py:456-458` (`kind=skin.get("title_kind", "Name")`) so `oscHeaderAbc` preserves `"type": "Value"` on its `Title` label (driven by `"title_kind": "Value"` on `osc-abc` in `components/native/header/catalog.json:296`, matching commit `41b2bef9`'s shared `Parameter 3026` retargeted title).
   - Updated `header.py:509-511` so `"wrapper": "NativeActive"` passes `cell.get("hw_focus", "No")` instead of hardcoded `"No"`.

2. **Osc header instances `acceptsHWFocus: "Children"` (`components/native/header/skin-osc.json`, `scripts/header-skin.py`)** — DONE
   - Aligned `components/native/header/skin-osc.json:11-88` with the shared ABC refactor (`41b2bef9`): replaced the 3 stale `Osc A/B/C Header` entries with `"Osc ABC Header"` (`Parameter 3026` for `HdrTitle`, `RangedEnabling/1/3/5/Parameter 2876` for `gate`/`HdrOscGate`), and set `"focus": "Children"` on `"Osc ABC Header"`, `"Sub Header"`, `"Noise Header"`, and `"Noise Sample Header"`.
   - Updated `scripts/header-skin.py:55-63` so an instance with `"power"` may override `HdrTitle` via `remap["HdrTitle"]` without duplicating the key.

3. **Osc header cells `hw_focus` (`components/native/header/catalog.json`)** — DONE
   - `osc-abc`: `Type` (`oscHeaderType`) `"Yes"`, `Table` (`oscHeaderTable`) `"Children"`, `Source` (`oscHeaderSource`) `"Yes"`, `Browse` (`oscHeaderBrowse`) `"Children"`, `Route` (`NativeActive oscHeaderRoute`) `"Children"`.
   - `osc-sub`: `Waveform` (`oscHeaderWaveform`) `"Yes"`, `Octave` (`oscHeaderOct`) `"Yes"`, `Route` (`NativeActive oscHeaderRoute`) `"Children"`.
   - `osc-noise`: `Kind` (`oscHeaderKind`) `"Yes"`, `Sound` (`oscHeaderSound`) `"Yes"`, `Route` (`NativeActive oscHeaderRoute`) `"Children"`.
   - `osc-noise-sample`: `Kind` (`oscHeaderKindWide`) `"Yes"`, `Route` (`NativeActive oscHeaderRoute`) `"Children"`.

4. **Validator extension (`scripts/validate_synthyboy_skin.py:53-59`, `:355-373`)** — DONE (narrowed to `HEADER_FORMS`)
   - Added a check right after `SCREEN_FAMILIES` verifying that header form definitions with `ignoreMousePresses: false` and empty `actions` cannot wrap custom components with non-empty `actions` or nested `Button`/`Knob` widgets with `acceptsHWFocus: "Yes"`.
   - When evaluated across all skin definitions, the unrestricted rule fired on 104 definitions outside header forms (listed below); per task instructions, those screens were not modified and the validator rule was narrowed to `HEADER_FORMS`.

5. **Skin regeneration & baseline (`skin/defs_*.json`, `scripts/skin-baseline/TUI.keys.json`)** — DONE
   - Ran `scripts/header-skin.py` for `skin-osc.json`, `skin-env.json`, `skin-lfo.json`, `skin-curve.json`, `skin-mod.json`, `skin-master.json`, `skin-macro.json`, `skin-voice.json`.
   - For `skin/defs_tuning.json`, updated only the `tuningHeader` definition from `header.header_definitions` instead of running `header-skin.py` on `skin-tuning.json` (which is outside `May edit` and holds a stale `HdrView` `IndexedEnabling/2/3/Parameter 2879` vs `RangedEnabling/0/18/33/Parameter 2675` in `skin/TUI.json` from commit `96a2d791`).
   - Ran `scripts/skin-format.py` and `scripts/skin-baseline-update.py`.
   - Verified `git diff ng/oscdropdown -- skin/` contains **only** `ignoreMousePresses` (`false -> true` x12) and `acceptsHWFocus` (`Yes -> Children` x4, `No -> Children` x6, `No -> Yes` x7) changes across 8 files (`29 insertions(+), 29 deletions(-)`).

6. **Tests & Documentation** — DONE
   - `scripts/tests/test_native_header_catalog.py:245-268`: added `test_header_forms_ignore_presses_and_osc_focus_chain` covering the three changed properties.
   - `docs/ui-components/header.md:81,107`: updated `hw_focus` table row and Route focus-chain note in present tense.
   - `docs/dsp-knowledge/skin-host-behaviour.md:83-86`: added HYPOTHESIS entry and device check instructions.

## Every Changed Definition and Instance (`old -> new`)

### Header form definitions (`ignoreMousePresses`)
- `skin/defs_env.json:1508` — `envHeader`: `false -> true`
- `skin/defs_lfo.json:17497` — `lfoHeader`: `false -> true`
- `skin/defs_macro.json:921` — `macroHeader`: `false -> true`
- `skin/defs_master.json:3659` — `masterHeader`: `false -> true`
- `skin/defs_mod.json:2888` — `curveHeader`: `false -> true`
- `skin/defs_mod.json:3469` — `modHeader`: `false -> true`
- `skin/defs_osc.json:22744` — `oscHeaderAbc`: `false -> true`
- `skin/defs_osc.json:23510` — `oscHeaderSub`: `false -> true`
- `skin/defs_osc.json:24071` — `oscHeaderNoise`: `false -> true`
- `skin/defs_osc.json:24530` — `oscHeaderNoiseSample`: `false -> true`
- `skin/defs_tuning.json:2741` — `tuningHeader`: `false -> true`
- `skin/defs_voice.json:3727` — `voiceHeader`: `false -> true`

### Osc header instances in `oscNativeSettingsLayer` (`skin/defs_osc.json`, `acceptsHWFocus`)
- `skin/defs_osc.json:18225` — `Osc ABC Header` (`oscHeaderAbc`): `"Yes" -> "Children"`
- `skin/defs_osc.json:18297` — `Sub Header` (`oscHeaderSub`): `"Yes" -> "Children"`
- `skin/defs_osc.json:18369` — `Noise Header` (`oscHeaderNoise`): `"Yes" -> "Children"`
- `skin/defs_osc.json:18437` — `Noise Sample Header` (`oscHeaderNoiseSample`): `"Yes" -> "Children"`

### Osc header cell instances inside header forms (`skin/defs_osc.json`, `acceptsHWFocus`)
- `oscHeaderAbc`:
  - `skin/defs_osc.json:22849` — `Type` (`oscHeaderType`): `"No" -> "Yes"`
  - `skin/defs_osc.json:22958` — `Table` (`oscHeaderTable`): `"No" -> "Children"`
  - `skin/defs_osc.json:23067` — `Source` (`oscHeaderSource`): `"No" -> "Yes"`
  - `skin/defs_osc.json:23176` — `Browse` (`oscHeaderBrowse`): `"No" -> "Children"`
  - `skin/defs_osc.json:23247` — `Route` (`NativeActive oscHeaderRoute`): `"No" -> "Children"`
- `oscHeaderSub`:
  - `skin/defs_osc.json:23615` — `Waveform` (`oscHeaderWaveform`): `"No" -> "Yes"`
  - `skin/defs_osc.json:23721` — `Octave` (`oscHeaderOct`): `"No" -> "Yes"`
  - `skin/defs_osc.json:23835` — `Route` (`NativeActive oscHeaderRoute`): `"No" -> "Children"`
- `oscHeaderNoise`:
  - `skin/defs_osc.json:24176` — `Kind` (`oscHeaderKind`): `"No" -> "Yes"`
  - `skin/defs_osc.json:24282` — `Sound` (`oscHeaderSound`): `"No" -> "Yes"`
  - `skin/defs_osc.json:24396` — `Route` (`NativeActive oscHeaderRoute`): `"No" -> "Children"`
- `oscHeaderNoiseSample`:
  - `skin/defs_osc.json:24635` — `Kind` (`oscHeaderKindWide`): `"No" -> "Yes"`
  - `skin/defs_osc.json:24749` — `Route` (`NativeActive oscHeaderRoute`): `"No" -> "Children"`

## Validator Findings Outside Header Forms (104 definitions, unchanged)

When the broad check (`ignoreMousePresses == false`, `actions == []`, child custom component with non-empty `actions` or nested `Button`/`Knob` with `acceptsHWFocus == "Yes"`) was run across all skin definitions, these 104 definitions matched outside `HEADER_FORMS`:

- `skin/defs_common.json` (3): `compactNativeDial`, `compactNativeRiseDial`, `native keyboard overlay`
- `skin/defs_filter.json` (5): `filterNativeChainChoices`, `filterNativeDial_top_uni_on_100x178`, `filterNativeDial_top_bi_on_100x178`, `filterNativeDial_side_uni_on_216x135`, `filterNativeDial_side_bi_on_216x135`
- `skin/defs_fx.json` (6): `fxNativeCellUni`, `fxNativeCellBi`, `fx editor rack menu`, `fx editor main bus rack menu`, `fx chain add menu`, `fxNativeGalleryCard`
- `skin/defs_lfo.json` (22): `lfoNativeChoiceDrawer1`, `lfoNativeChoiceDrawer2`, `lfoNativeChoiceDrawer8`, `lfoNativeChoiceDrawer11`, `lfoNativeChoiceDrawer12`, `lfoNativeChoiceDrawer16`, `lfoNativeField7_162x246_0`, `lfoNativeField9_196x114_0`, `lfoNativeField13_164x168_0`, `lfoNativeField14_164x168_0`, `lfoNativeField17_239x114_0`, `lfoNativeField18_239x114_0`, `lfoNativeField7_220x152_0`, `lfoEditAction`, `lfoEditAction0`, `lfoEditAction2`, `lfoEditAction3`, `lfoEditAction4`, `lfoEditAction5`, `lfoEditAction6`, `lfoEditAction7`, `lfoDetailRow`
- `skin/defs_macro.json` (2): `macroSubLevel`, `macroCard` (`macroSubLevel -> Knob`)
- `skin/defs_mixer.json` (3): `mixerSliderPAN56`, `mixerSliderB156`, `mixerSliderB256`
- `skin/defs_mod.json` (8): `modNative_dial_uni_215x213`, `modNative_dial_bi_215x213`, `modNative_dial_bi_312x390`, `modNative_dial_uni_227x213`, `curveHeaderInit`, `curveHeaderUndo`, `curveHeaderDelPoint`, `modHeaderAdd`
- `skin/defs_osc.json` (43): `oscNativeCompactPan`, `oscNativeCompactLevel`, `oscNativeSetting_dial_uni`, `oscNativeSetting_dial_bi`, `oscNativeSetting_toggle`, `oscNativeSetting_menu_WAVEFORMChoices`, `oscNativeSetting_dial_bi_216x128`, `oscNativeSetting_dial_uni_216x128`, `oscNativeSetting_toggle_ENV 1_218x128`, `oscNativeSetting_toggle_MONO_218x128`, `oscNativeSetting_toggle_FREE RUN_218x128`, `oscNativeSetting_menu_KINDChoices`, `oscNativeSetting_menu_SOUNDChoices`, `oscNativeSetting_dial_bi_173x128`, `oscNativeSetting_dial_uni_173x128`, `oscNativeSetting_toggle_KEY TRACK_171x128`, `oscNativeSetting_toggle_ENV 1_173x128`, `oscNativeSetting_dial_bi_107x128`, `oscNativeSetting_dial_uni_107x128`, `oscNativeSetting_toggle_KEY TRACK_105x128`, `oscNativeSetting_toggle_ENV 1_107x128`, `oscNativeSetting_toggle_RANDOM_107x128`, `oscNativeSetting_toggle_LOOP_107x128`, `oscNativeSetting_toggle_BOUNCE_107x128`, `oscNativeSetting_menu_TYPEChoices`, `oscNativeSetting_action_detail_181x56`, `oscNativeSetting_dial_uni_181x132`, `oscWarpChoices`, `oscNativeSetting_dial_uni_side_146x48`, `oscNativeSetting_dial_uni_side_136x48`, `oscNativeSetting_dial_uni_side_138x48`, `oscNativeSetting_action_map`, `oscNativeSetting_dial_uni_side_184x58`, `oscNativeSetting_dial_bi_side_278x48`, `oscNativeSetting_dial_uni_184x132`, `oscNativeSetting_dial_uni_side_278x48`, `oscNativeSetting_dial_uni_153x128`, `oscNativeSetting_dial_uni_155x128`, `oscNativeSetting_action_detail_274x56`, `oscNativeSetting_dial_uni_274x132`, `oscNativeTune` (`sharedNativeNameValue` with actions), `oscHeaderTable`, `oscHeaderBrowse`
- `skin/defs_play.json` (9): `oscNativeSourceChoices`, `play sub waveform`, `play osc octave`, `playOscSourceTitled`, `compactNativeDial56`, `playOscLevel56`, `playOscFilterDial56`, `compactNativeRiseDial56`, `playEnvLfoRate56`
- `skin/defs_tuning.json` (1): `tuningHeaderScale`
- `skin/defs_voice.json` (2): `voiceStepButtons`, `voiceStepValueButtons`

## Checks (verbatim output)

### 1. `python3 scripts/validate_synthyboy_skin.py --schema-only skin` (exit code 0)
```
WARNING skin $.pageData.componentDefinitions.localComponentDefinitions[20].value.componentsData[2].componentData.data.buttonId (first of 2): Button buttonId above 98 (largest proven on a device)
WARNING skin $.pageData.componentDefinitions.localComponentDefinitions[20].value.componentsData[2].componentData.data.numButtonsInGroup (first of 84): Button numButtonsInGroup above 99 (largest proven on a device)
WARNING skin $.pageData.componentDefinitions.localComponentDefinitions[20].value.componentsData[2]["handle remapping"].map[0].value (first of 84): handle form not proven on a device (SDK-documented only): 'ResetOnGestureEnd/Parameter N'
WARNING skin $.pageData.componentDefinitions.localComponentDefinitions[20].value.componentsData[5].bounds.additionalInvalidatingHandles[0] (first of 90): Local handle id 'layer' is not the proven one (synthyboyWarpPage); a Local state shared by several tabs is only proven for that id
skin schema: 0 errors, 4 kinds of warnings
```

### 2. `python3 -m pytest scripts/tests/test_native_header_catalog.py -q` (exit code 1 — pre-existing HTML assertion)
```
...F................                                                     [100%]
=================================== FAILURES ===================================
_ HeaderCatalogTests.test_html_lists_every_example_with_caption_outside_the_image _

self = <test_native_header_catalog.HeaderCatalogTests testMethod=test_html_lists_every_example_with_caption_outside_the_image>

    def test_html_lists_every_example_with_caption_outside_the_image(self):
        html = HTML.read_text(encoding="utf-8")
        self.assertIn('<article class="wide">', html)
        for example in EXAMPLES:
            self.assertIn(f"components/native/header/img/{example['file']}", html)
>       self.assertIn("400 px", html)
E       AssertionError: '400 px' not found in '<!-- Edit this file directly. -->...'

scripts/tests/test_native_header_catalog.py:162: AssertionError
=========================== short test summary info ============================
FAILED scripts/tests/test_native_header_catalog.py::HeaderCatalogTests::test_html_lists_every_example_with_caption_outside_the_image
1 failed, 19 passed in 1.10s
```
All 11 `HeaderSkinSpecTests` and `HeaderNoPowerSkinTests` tests passed (including `test_header_forms_ignore_presses_and_osc_focus_chain` and `test_the_committed_skin_is_what_the_tool_writes`). `HeaderCatalogTests.test_html_lists_every_example_with_caption_outside_the_image` fails on pre-existing missing `"400 px"` text in `docs/ui-components/kepes-katalogus.html` (which is outside `May edit`).

## What Could Not Be Done / Remaining Items
- **Pre-existing pytest failure**: `test_html_lists_every_example_with_caption_outside_the_image` (`scripts/tests/test_native_header_catalog.py:162`) still fails because `docs/ui-components/kepes-katalogus.html` is outside `May edit` and the test expectation change was restricted to the three changed properties.
- **Stale `components/native/header/skin-tuning.json`**: `skin-tuning.json:22` still lists `"HdrView": "IndexedEnabling/2/3/Parameter 2879"` while `skin/TUI.json` uses `"RangedEnabling/0/18/33/Parameter 2675"` (commit `96a2d791`); `skin-tuning.json` was outside `May edit`, so only the `tuningHeader` definition in `skin/defs_tuning.json` was updated.
- **Live hardware verification** (no builds/deploys permitted in this task):
  - On both MPC Key 61 and MPC XL, open Osc A/B/C, tap `TYPE`, `SOURCE`, `ROUTE`, and `BROWSE` (and pick a row in `TYPE`, tap list header to apply), plus Sub (`WAVEFORM`, `OCT`, `ROUTE`) and Noise (`KIND`, `SOUND`, `ROUTE`), and confirm touch and hardware focus work.
  - Note for follow-up if needed: `oscNativeTune` (`defs_osc.json`) and `macroCard` (`defs_macro.json`) also matched the broad container check (`ignoreMousePresses: false`, `actions: []`, nested interactive children); check Tune touch on device separately.
