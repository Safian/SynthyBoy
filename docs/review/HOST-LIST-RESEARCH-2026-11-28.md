# Host List Research: Can a Third-Party VST2 Use the MPC Host's Scrolling List?

Date: 2026-11-28
Branch / base commit: `agy/host-list-research` (from `07eabc09`)
Question (owner, 2026-11-28): Can a third-party VST2 plugin on the MPC use the **host's own scrolling list** (`MenuOverlay` / `ComboBox`) for its choices, and **did we use it wrongly** when we observed blank rows, row-tap only moving the check mark, header-tap applying, and tap-outside not closing?

## Epistemic Tagging Legend

Every substantive claim below is tagged with its provenance:

- `[DOC]` — Official inMusic / Akai documentation (MPC Plugin Builder SDK commit `febd2ca` / runtime-sdk-docs `e78bad5`, or MPC Standalone OS User Guide v3.10.1).
- `[STOCK]` — Directly observed in stock MPC/AIR/Qubiq `TUI.json` skins or factory overlay files pulled from hardware (`/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/private-do-not-publish/reference-skins/`, `findings/xl-midi-TUI.json`, `.hw-local/mpc-perf/native-text-contract-xl-20261124/Generic Text Overlay.json`).
- `[MEASURED]` — Directly measured on our MPC Key 61 or MPC XL hardware (citing exact artifact directory / screenshot / log / git commit).
- `[COMMUNITY]` — Reported by the Open MPC community (`docs-community/open-mpc/docs/`), measured by community authors on their devices.
- `[HYPOTHESIS]` — Inferred explanation not yet isolated by a dedicated device experiment.

---

## Executive Summary

**Two separate failure modes were conflated in our earlier conclusion. One of them WAS our mistake; the other was reproduced even with an unmodified official SDK skin — leaving one narrow, testable `unknown` for static flat lists and definitive `no` answers for two-level parameter lists and dynamic browsers.**

1. **YES — we used `MenuOverlay` wrongly in our own-row hybrid overlays (`oscNativeSourceChoices`, `oscNativeRoutingChoices`, `oscNativeSetting_menu_TYPEChoices`, `oscNativeSetting_menu_WAVEFORMChoices`, `oscWarpChoices`), and those mistakes caused the "row tap only marks, header applies, tap outside does not close" defects observed on Key 61 (2026-11-27).**
   - `[MEASURED]` The Key 61 list probe (`list-probe-results/none-*`, `A-*`, `B-*`, recorded in `docs/dsp-knowledge/skin-host-behaviour.md:5-14`) tested `oscNativeSourceChoices` (`a6732940~1:skin/defs_play.json:7-452`), **not** a bare stock `MenuOverlay`.
   - That overlay definition had **four compounding defects** compared to stock skins and SDK §6.9/§6.11:
     1. It placed our own radio `Button`s (`Choose Sub`..`Choose Noise`, `0 110+80k 426 80`) **after (on top of)** `MenuOverlay` in `componentsData`, so a row tap hit our `Button` (updating the parameter and moving `MenuOverlay`'s check mark without `MenuOverlay` ever receiving the row tap to close). Only tapping the uncovered header (`y = 0..110`) hit `MenuOverlay` and closed it.
     2. Its `MenuOverlay` `data` object contained only **one** `"version": 1` key instead of the required duplicate `"version": 1, "version": 1` (`[DOC]` SDK `ui-reference.md` §1.8 L176-206, §6.11 L1185-1198).
     3. It set `"ignoreMousePresses": false`, an opaque `"ff1e1d1e"` `backgroundData`, and a full-screen opaque `Image` (`osc_routing_overlay_bg.png`, `0 0 1280 800`), preventing the translucent blurred backdrop and blocking tap-outside dismiss.
     4. Its opener passed `"handleName": "Data"` in `Show Overlay`, whereas all 7 inspected stock skins pass `"handleName": ""`.
2. **HOWEVER — even when we ran the official inMusic SDK `tools/gen_ui.py` verbatim (bare `MenuOverlay`, NO own rows, BOTH `"version": 1` keys preserved, `ignoreMousePresses: true`, AND `pageData.replacementStrings` populated), the host `MenuOverlay` STILL rendered ZERO row text on our MPC XL (3.10.1.12).**
   - `[MEASURED]` On MPC XL (`compact-native-q-xl-r4`, commit `f670f2f1`, 2026-11-22, `menu-osc-host.png`, `menu-env-host.png`, `menu-lfo-host.png`), double-tapping `PLAY OSC` (5 strings), `PLAY ENV` (6 strings), and `PLAY LFO` (10 strings) opened genuine 426 px host drawers. The host **did** honour the step count `N` from `replacementStrings` (placing the check mark at row 4 for Noise and row 9 for LFO 10, and drawing a **vertical scroll bar** on 10-item `PLAY LFO`), **yet every row had zero rendered text** (`x 10..359, y 110..759`: uniform background colour; OCR found only the panel title).
   - `[MEASURED]` Still, as `compact-native-q-xl-r5/baseline.md:1` itself noted, `r4` only tested 3 Play proxy parameters (`Parameter 2676/2677/2678`) that had `effCanBeAutomated == 0`, opaque `backgroundData: "ff1e1d1e"`, `Show Overlay` `handleName: "Data"` (instead of stock `""`), `acceptsHWFocus: "No"` (instead of SDK §9.1 `"Yes"`), and `effGetParameterProperties` without `kVstParameterUsesIntStep`. Nobody has yet tested a clean stock-shape bare `MenuOverlay` (with `backgroundData: "0"`, no own rows, `handleName: ""` vs `"Data"`) on an automatable engine choice parameter, nor verified row-tap-to-close on a bare VST2 `MenuOverlay`.
3. **Two-level parameter lists and dynamic runtime browsers are definitively `no` for the host list.**
   - `[DOC]` / `[STOCK]` No stock skin or SDK widget supports a two-level parameter list; every stock parameter list is flat (Fabric XL uses two separate flat parameters, Bank + Samples; the two-level `PRESET` drawer is built solely from `Presets/NNNN-<Category>-<Name>.xpl` filenames).
   - `[DOC]` / `[COMMUNITY]` / `[MEASURED]` `replacementStrings` is static skin JSON loaded once; VST2 has no opcode for a plugin to supply a runtime list of N choice strings to `MenuOverlay`.

---

## 1. Sources Inspected

| Source | Path / Reference | Role |
|---|---|---|
| Project host facts | `docs/dsp-knowledge/skin-host-behaviour.md` (L1-130) | Key 61 & XL measured skin/list/overlay facts |
| Project rules | `AGENTS.md` ("Lists", "Skin rules measured on the device") | Current policy and baseline |
| Expert KB: lists | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/findings/list-patterns.md` (L1-59) | Key 61 Fabric XL & preset list captures |
| Expert KB: UI choice guide | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/findings/native-ui-choice-guide.md` (L1-43) | Native widget selection table |
| Expert KB: index | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/INDEX.md` (L1-160) | Knowledge provenance & versions |
| Key 61 Fabric XL captures | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/device-observed/lists-key61-20261128/README.md` + PNGs | Measured stock flat & 2-level list behaviour |
| Key 61 list probe (2026-11-27) | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/host-skin-findings-2026-11-27/list-probe-results/` (`none-*`, `A-*`, `B-*`) | Measured `oscNativeSourceChoices` tap/close probe |
| XL bare `MenuOverlay` probe (2026-11-22) | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/compact-native-q-xl-r{2,3,4,5,6}/` (`README.md`, `baseline.md`, `menu-osc-host.png`, `menu-lfo-host.png`) | Measured SDK `gen_ui.py` `MenuOverlay` + `replacementStrings` on XL |
| XL disassembly & factory overlay | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/native-text-contract-xl-20261124/` (`Generic Text Overlay.json`, `vtables.txt`, `plugin-element-vtables.txt`) | Factory overlay wrapper JSON & XL vtables |
| Official MPC Plugin Builder SDK | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk/` (`docs/ui-reference.md`, `docs/ui-guide.md`, `docs/host-contract.md`, `tools/gen_ui.py`, `framework/mpc_plugin.h`, `framework/mpc_vst2_abi.h`, `examples/*/ui.json`) and `sources/runtime-sdk-docs/docs/` | Official widget schema, generator, VST2 ABI & framework |
| MPC Standalone OS User Guide v3.10.1 | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/manuals-extended/MPC Standalone OS - User Guide - v3.10.1.txt` (p. 40, L1222-1228) | Official user interaction spec for drop-down menus/lists |
| Open MPC community docs | `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-community/open-mpc/docs/` (`NOTES.md`, `SDK-PORTING.md`, `ROADMAP.md`) | Community `poc/menuprobe.c`, `.vstxml` test, Welle 2 notes |
| Stock skins (7 files) | `private-do-not-publish/reference-skins/` (`MPC_-_Fabric_XL.TUI.json`, `MPC_-_OPx-4.TUI.json`, `Qubiq_-_Sub_Factory.TUI.json`, `Qubiq_-_AIR_Visual_EQ4.TUI.json`, `Qubiq_-_AIR_Multiband_Filterbank.TUI.json`, `MPC_-_AIR_Reverb_Pro.TUI.json`) + `findings/xl-midi-TUI.json` | Factory dropdown/list component and action patterns |
| SynthyBoy C++ & skin history | `shell-mpc/synthyboy_mpc.cpp`, `skin/TUI.json`, `skin/defs_*.json`, commits `f670f2f1`, `e3693360`, `67713013`, `a6732940`, `e22fd809`, `c35441a0`, `f755d56d` | Our VST2 opcode handlers & historical `MenuOverlay` defs |

Note on other stock synth skins mentioned in the prompt (TubeSynth, Hype, Electric, Mini Grand, DrumSynth): `[COMMUNITY]` `NOTES.md:397-426` inspects Hype, TubeSynth, and Electric on hardware and reports the same pattern (`comboBox` / `blueComboBox` custom components opening `menu overlay`, no embedded option list in JSON); their full `TUI.json` files are not stored in the local `reference-skins/` folder (which holds the 6 skins above plus `findings/xl-midi-TUI.json`).

---

## 2. How Stock Plugin Skins Present Parameters with Many Named Choices

### 2.1 Audit Across All 7 Locally Available Stock Skins

`[STOCK]` Scanning every component primitive, `importFiles` entry, `replacementStrings` table, and `Show Overlay` action across all 7 stock `TUI.json` files yields a completely uniform pattern:

| Stock skin | `importFiles` | `replacementStrings` count | Uses `ComboBox` widget primitive? | Custom dropdown defs (`Show Overlay` -> `"menu overlay"`) | Triggers & `handleName` on `Show Overlay` |
|---|---|---:|---|---|---|
| `MPC_-_Fabric_XL.TUI.json` | `../../Generic/Generic Knob Overlay.json`, `../../Generic/Generic Menu Overlay.json` | 0 | **No** | `comboBank1`, `comboBank2`, `comboBox`, `comboBoxFilter`, `comboSampleBank2`, `comboSamples1`, `comboSamples2`, `comboSamplesSample1`, `filterType1/2`, `lfoType1/2`, `flavor`, … (18 defs) | `Double Click` (`""`), `Enter Pressed` (`""`) |
| `MPC_-_OPx-4.TUI.json` | `../../Generic/Generic Knob Overlay.json`, `../../Generic/Generic Menu Overlay.json` | 0 | **No** | `comboBox`, `comboBoxSm`, `comboBoxFx`, `comboBoxSample`, … (19 defs) | `Double Click` (`""`), `Enter Pressed` (`""`) |
| `Qubiq_-_Sub_Factory.TUI.json` | `../../Generic/Generic Knob Overlay.json`, `../../Generic/Generic Menu Overlay.json` | 0 | **No** | `comboBox`, `comboBoxRight`, `comboBoxSample`, `comboPlus`, `comboPlusSample`, `comboPolarity` (6 defs) | `Double Click` (`""`), `Enter Pressed` (`""`), AND **`Mouse Down` (`""`)** on `comboPlus` / `comboPlusSample` |
| `Qubiq_-_AIR_Multiband_Filterbank.TUI.json` | `../../Generic/Generic Knob Overlay.json`, `../../Generic/Generic Menu Overlay.json` | 0 | **No** | `select_menu` (1 def) | **`Mouse Down` (`""`)** only |
| `Qubiq_-_AIR_Visual_EQ4.TUI.json` | `../../Generic/Generic Knob Overlay.json`, `../../Generic/Generic Menu Overlay.json` | 0 | **No** | `comboBoxDefault`, `comboBoxDisabled` (2 defs) | `Double Click` (`""`), `Enter Pressed` (`""`) |
| `MPC_-_AIR_Reverb_Pro.TUI.json` | `../../AIR Components/AIR Generic Components.json` | 0 | **No** | `comboBoxOrange`, `iconTail*` (4 defs) | `Double Click` (`""`), `Enter Pressed` (`""`) |
| `findings/xl-midi-TUI.json` | `../../Generic/Generic Slider.json`, `../../Generic/Generic Menu Overlay.json`, `../../Generic/Generic Text Overlay.json` | 0 | **No** | `comboBox` (1 def) | `Double Click` (`""`), `Enter Pressed` (`""`) |

Key structural facts (`[STOCK]`):
- **Not a single stock skin uses the built-in `ComboBox` widget primitive** (`ui-reference.md` §6.8). Every component named `comboBox*`, `comboBank*`, `comboSamples*`, or `select_menu` is a **custom composite** built from a `Focus` rectangle and a `Label` (`type: "Value"`, `handleName: "Data"`), optionally plus a background/chevron `Image`.
- **Every stock skin opens the shared `"menu overlay"`** imported from `../../Generic/Generic Menu Overlay.json` (or `../../AIR Components/AIR Generic Components.json`).
- **Every stock skin writes `"handleName": ""`** (empty string, inheriting the control's own `Data` handle) on its `Show Overlay` action, unlike SDK `gen_ui.py` / `ui-reference.md` §9.1 which writes `"handleName": "Data"`.
- **Zero stock skins use `replacementStrings`** (`0` entries in all 7 files).
- **Single-tap opening exists in stock skins:** `Qubiq_-_AIR_Multiband_Filterbank.TUI.json` (`select_menu`, L747) and `Qubiq_-_Sub_Factory.TUI.json` (`comboPlus` L4477, `comboPlusSample` L4558) attach `Show Overlay` -> `"menu overlay"` to `Mouse Down`.

### 2.2 Exact JSON Quotes from Three Stock Controls

#### Example 1: MPC Fabric XL — Bank & Sample selectors (`comboBank1` / `comboSamplesSample1`)
File: `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/private-do-not-publish/reference-skins/MPC_-_Fabric_XL.TUI.json`

Import (L6-9):
```json
"importFiles": [
    "../../Generic/Generic Knob Overlay.json",
    "../../Generic/Generic Menu Overlay.json"
]
```

Component definition `comboBank1` (L2222-L2346; `comboSamplesSample1` at L3104-L3228 is identical except `colour: "ffffffff"`):
```json
{
  "key": "comboBank1",
  "value": {
    "version": 4,
    "actions": [
      {
        "version": 2,
        "onAction": "Double Click",
        "handler": "Show Overlay",
        "handleName": "",
        "additionalData": "menu overlay",
        "handle remapping": { "version": 1, "map": [] }
      },
      {
        "version": 2,
        "onAction": "Enter Pressed",
        "handler": "Show Overlay",
        "handleName": "",
        "additionalData": "menu overlay",
        "handle remapping": { "version": 1, "map": [] }
      },
      {
        "version": 2,
        "onAction": "Mouse Down",
        "handler": "Q-Link",
        "handleName": "Data",
        "additionalData": "",
        "handle remapping": { "version": 1, "map": [] }
      }
    ],
    "backgroundData": {
      "version": 1,
      "focussed": { "version": 1, "colour": "0", "image": "" },
      "unfocussed": { "version": 1, "colour": "0", "image": "" }
    },
    "ignoreMousePresses": false,
    "disableCoarseDataWheel": true,
    "repeats": 1,
    "hideQLinkBounds": false,
    "componentsData": [
      {
        "version": 2,
        "componentData": {
          "version": 1,
          "name": "Focus",
          "type": "Focus",
          "data": {
            "version": 1,
            "backgroundColour": "19ffffff",
            "outlineColour": "ffe81f40",
            "backgroundInset": 4.0,
            "outlineThickness": 1.0
          }
        },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": {
          "version": 2,
          "acceptsHWFocus": "No",
          "showWhenDataModelInvalid": "Show",
          "whenVisible": "WhenFocussed",
          "boundsType": "Proportional",
          "bounds": "0 0 1 1",
          "additionalInvalidatingHandles": []
        }
      },
      {
        "version": 2,
        "componentData": {
          "version": 1,
          "name": "Label",
          "type": "Label",
          "data": {
            "version": 1,
            "textStyle": {
              "version": 1,
              "font": { "version": 1, "name": "Titillium Web", "style": "Light", "height": 55.0 },
              "colour": "ffe85560",
              "justification": "left",
              "case": "Original"
            },
            "type": "Value",
            "handleName": "Data"
          }
        },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": {
          "version": 2,
          "acceptsHWFocus": "No",
          "showWhenDataModelInvalid": "Show",
          "whenVisible": "Always",
          "boundsType": "Proportional",
          "bounds": "0.04 0.04 0.95 0.9",
          "additionalInvalidatingHandles": []
        }
      }
    ]
  }
}
```

Instances inside `sample1Header` (L15017-L15075):
```json
{
  "version": 2,
  "componentData": { "version": 1, "name": "Sampler 1 Bank", "type": "comboBank1", "data": { "version": 1, "handleName": "Data" } },
  "handle remapping": { "version": 1, "map": [{ "key": "Data", "value": "Sampler 1 Bank" }] },
  "bounds": { "version": 2, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "131 18 365 50", "additionalInvalidatingHandles": [] }
},
{
  "version": 2,
  "componentData": { "version": 1, "name": "Sampler 1 Samples", "type": "comboSamplesSample1", "data": { "version": 1, "handleName": "Data" } },
  "handle remapping": { "version": 1, "map": [{ "key": "Data", "value": "Sampler 1 Samples" }] },
  "bounds": { "version": 2, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "131 68 365 50", "additionalInvalidatingHandles": [] }
}
```

#### Example 2: Qubiq Sub Factory — Long sample/wavetable lists (`comboBoxSample` + `comboPlusSample`)
File: `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/private-do-not-publish/reference-skins/Qubiq_-_Sub_Factory.TUI.json`

Component definition `comboBoxSample` (L4351-L4475; opens on `Double Click` / `Enter Pressed`) and its paired single-tap icon `comboPlusSample` (L4558-L4645; opens on `Mouse Down` / `Enter Pressed`):
```json
{
  "key": "comboBoxSample",
  "value": {
    "version": 4,
    "actions": [
      { "version": 2, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": "", "handle remapping": { "version": 1, "map": [] } },
      { "version": 2, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "", "additionalData": "menu overlay", "handle remapping": { "version": 1, "map": [] } },
      { "version": 2, "onAction": "Enter Pressed", "handler": "Show Overlay", "handleName": "", "additionalData": "menu overlay", "handle remapping": { "version": 1, "map": [] } }
    ],
    "backgroundData": { "version": 1, "focussed": { "version": 1, "colour": "0", "image": "" }, "unfocussed": { "version": 1, "colour": "0", "image": "" } },
    "ignoreMousePresses": false,
    "disableCoarseDataWheel": true,
    "repeats": 1,
    "hideQLinkBounds": false,
    "componentsData": [
      {
        "version": 2,
        "componentData": { "version": 1, "name": "Focus", "type": "Focus", "data": { "version": 1, "backgroundColour": "10ffffff", "outlineColour": "ffe81f40", "backgroundInset": 4.0, "outlineThickness": 1.0 } },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": { "version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "WhenFocussed", "boundsType": "Proportional", "bounds": "0 0 1 1", "additionalInvalidatingHandles": [] }
      },
      {
        "version": 2,
        "componentData": {
          "version": 1, "name": "Label", "type": "Label",
          "data": {
            "version": 1,
            "textStyle": { "version": 1, "font": { "version": 1, "name": "Titillium Web", "style": "Regular", "height": 50.0 }, "colour": "ffffffff", "justification": "left verticallyCentred", "case": "Original" },
            "type": "Value", "handleName": "Data"
          }
        },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": { "version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Proportional", "bounds": "0 0 0.8 1", "additionalInvalidatingHandles": [] }
      }
    ]
  }
}
```
Instance on `Main` (L9118-L9165, bound to `Parameter 24`; wavetable lists `Sub A WT` / `Sub B WT` use `comboBox` bound to `Parameter 10` / `Parameter 17`):
```json
{
  "version": 2,
  "componentData": { "version": 1, "name": "Sample List", "type": "comboBoxSample", "data": { "version": 1, "handleName": "Data" } },
  "handle remapping": { "version": 1, "map": [{ "key": "Data", "value": "Parameter 24" }] },
  "bounds": { "version": 2, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "662 102 286 76", "additionalInvalidatingHandles": [] }
}
```

#### Example 3: Qubiq AIR Multiband Filterbank — Single-tap filter-type selector (`select_menu`)
File: `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/private-do-not-publish/reference-skins/Qubiq_-_AIR_Multiband_Filterbank.TUI.json`

Component definition `select_menu` (L747-L845) and instance `filtertype_menu_placement` (L2572-L2599, bound to `Parameter 11`):
```json
{
  "key": "select_menu",
  "value": {
    "version": 2,
    "actions": [
      { "version": 1, "onAction": "Mouse Down", "handler": "Show Overlay", "handleName": "", "additionalData": "menu overlay" }
    ],
    "backgroundData": { "version": 1, "focussed": { "version": 1, "colour": "", "image": "" }, "unfocussed": { "version": 1, "colour": "", "image": "" } },
    "ignoreMousePresses": false,
    "disableCoarseDataWheel": false,
    "componentsData": [
      {
        "version": 2,
        "componentData": { "version": 1, "name": "focus_box", "type": "Focus", "data": { "version": 1, "backgroundColour": "00ffffff", "outlineColour": "ffe81f40", "backgroundInset": 0, "outlineThickness": 1 } },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": { "version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "WhenFocussed", "boundsType": "Absolute", "bounds": "0 0 162 48" }
      },
      {
        "version": 2,
        "componentData": {
          "version": 1, "name": "label", "type": "Label",
          "data": {
            "version": 1,
            "textStyle": { "version": 1, "font": { "version": 1, "name": "Nexa", "style": "Book", "height": 30 }, "colour": "ccEEF9FF", "justification": "left verticallyCentred", "case": "Original" },
            "type": "Value", "handleName": "Data"
          }
        },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": { "version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "5 0 157 48" }
      }
    ]
  }
}
```

#### The Overlay Definition (`menu overlay`)
The device file `/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json` is not copied verbatim in the local knowledge folder, but its sibling pulled from the XL (`Generic Text Overlay.json`, `.hw-local/mpc-perf/native-text-contract-xl-20261124/Generic Text Overlay.json:1-68`) even retained `"withNumbers": false` from `Generic Menu Overlay.json`, confirming the exact factory wrapper shape, which matches `[DOC]` SDK `ui-reference.md` §6.11 (L1178-1199) and §9.1 (L1928-1961):

```json
{
  "key": "menu overlay",
  "value": {
    "version": 2,
    "actions": [],
    "backgroundData": {
      "version": 1,
      "focussed": { "version": 1, "colour": "0", "image": "" },
      "unfocussed": { "version": 1, "colour": "0", "image": "" }
    },
    "ignoreMousePresses": true,
    "disableCoarseDataWheel": false,
    "componentsData": [
      {
        "version": 2,
        "componentData": {
          "version": 1,
          "name": "overlay",
          "type": "MenuOverlay",
          "data": {
            "version": 1,
            "version": 1,
            "actions": [
              { "version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": "" }
            ],
            "withNumbers": false,
            "handleName": "Data"
          }
        },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": {
          "version": 1,
          "acceptsHWFocus": "Yes",
          "showWhenDataModelInvalid": "Show",
          "whenVisible": "Always",
          "boundsType": "Absolute",
          "bounds": "0 0 1280 800"
        }
      }
    ]
  }
}
```
Note one minor discrepancy between `[STOCK]` `Generic Text Overlay.json:56` + SDK `tools/gen_ui.py:593` (`"acceptsHWFocus": "No"`) and `[DOC]` `ui-reference.md:1957` (`"acceptsHWFocus": "Yes"`), plus `tools/gen_ui.py:594,673` putting opaque `style["background"]` (`ff1e1d1e`) on `backgroundData` instead of `"0"`.

### 2.3 What the User Sees (Manual + Key 61 Captures)

- `[DOC]` **MPC Standalone OS User Guide v3.10.1**, *"Drop-Down Menus / Lists"* (printed p. 40, `manuals-extended/MPC Standalone OS - User Guide - v3.10.1.txt:1222-1228`):
  - Tap the field to select it and turn the data dial or press `-`/`+` to step through options; OR double-tap the field (or press the data dial / Enter) to open the list, then tap the desired option (or use the data dial / `-`/`+`).
- `[MEASURED]` **MPC Key 61 (3.10.1.12) Fabric XL captures** (`device-observed/lists-key61-20261128/README.md:10-15`, `fabric-xl-layer1-bank-list.png`, `fabric-xl-after-row-tap-leads.png`, `fabric-xl-layer1-samples-list.png`):
  - Double-tapping `LAYER 1 BANK` opens a **426 px wide left drawer**, full screen height (`0..800`), with a ~110 px dark-grey header showing the uppercase parameter name (`LAYER 1 BANK`), 80 px tall plain-text rows left-aligned at `x ~ 50`, a light-grey highlight bar + white right-edge check mark on the active row, a **thin vertical scroll bar** on long lists (scrollable by touch drag), and the underlying plugin screen **blurred and dimmed** across `x = 426..1280`.
  - **One tap on a row** (`Leads`) immediately applies the choice AND closes the drawer (`fabric-xl-after-row-tap-leads.png`).
  - **One tap beside the drawer** (`x > 426`) closes without changing the parameter (`findings/list-patterns.md:10`).

---

## 3. Where Host List Row Names Come From, and VST2 Opcode Comparison

### 3.1 All Candidate Data Sources for `MenuOverlay` Row Names

| Mechanism | Status for stock internal plugins | Status for external VST2 plugins | Evidence |
|---|---|---|---|
| **Internal host parameter model** (`MpcPluginDataModel` / `NamedPluginParameterDataModel`) | **Primary source.** Stock synths (`Hype`, `TubeSynth`, `Fabric XL`, `OPx-4`, `Sub Factory`, etc.) are compiled into `/usr/bin/MPC` or loaded via internal MPC-format engines; their C++ parameter object supplies its live choice list directly without a VST2 ABI boundary. | **Unavailable.** External VST2 plugins are wrapped in `juce::VSTPluginInstance::VSTParameter` + `PluginWrapperElement`. | `[COMMUNITY]` `NOTES.md:412-426`; `[MEASURED]` XL vtables (`native-text-contract-xl-20261124/vtables.txt:10-21`, `plugin-element-vtables.txt:2-6`) |
| **`pageData.replacementStrings` in `TUI.json`** | Unused (`0` entries in all 7 stock skins). | **Contradiction between SDK doc and XL hardware measurement:** `[DOC]` SDK `ui-reference.md` §3.3 (L321-326), §6.11 (L1180-1181), §9.4 (L2134-2135), and `tools/gen_ui.py:675-696` state that `replacementStrings` supplies N discrete steps AND their display labels to `MenuOverlay`. **HOWEVER**, `[MEASURED]` on MPC XL 3.10.1.12 (`compact-native-q-xl-r4`, `f670f2f1`, `menu-osc-host.png`, `menu-lfo-host.png`), `MenuOverlay` with `replacementStrings` on `Parameter 2676/2677/2678` allocated the exact N rows, placed the check mark at step 4 / step 9, and drew a scroll bar for N=10 — **but rendered ZERO row text**. `[COMMUNITY]` `SDK-PORTING.md:97` also claims `MenuOverlay` opens empty even with `replacementStrings`, though it cites `NOTES.md` 2026-09-24 (which predates `replacementStrings`), and `ROADMAP.md:43-45` notes it was not separately verified. | `[DOC]` `ui-reference.md:312-340, 1178-1199, 2134-2135`; `[MEASURED]` `compact-native-q-xl-r4/README.md:11`, `menu-osc-host.png`, `menu-lfo-host.png`; `[COMMUNITY]` `SDK-PORTING.md:97`, `ROADMAP.md:43-45` |
| **`effGetParamDisplay` (VST2 opcode 7)** | Not applicable. | **Cannot supply a list.** Returns only the text of the parameter's *current* value; there is no VST2 parameter to query text for an arbitrary step `k` without mutating the parameter. | `[DOC]` `mpc_vst2_abi.h:101`, `host-contract.md:57-58` |
| **`effGetParameterProperties` (VST2 opcode 56)** | Not applicable. | **Cannot supply row names** (`VstParameterProperties` has only `minInteger`, `maxInteger`, `stepInteger`, `label`, `shortLabel`, `flags` — no string-array field in the VST2 ABI). Moreover, `[DOC]` `host-contract.md:59` states *"The host does not use the extended parameter properties (steps, integer ranges)"* and `[COMMUNITY]` `NOTES.md:408-409` logged zero opcode-56 calls on `poc/menuprobe.c`. **Note nuance:** `[MEASURED]` `shell-mpc/synthyboy_mpc.cpp:6015-6016` records a Key 61 observation (2026-11-16) where setting `kVstParameterUsesIntegerMinMax`/`stepInteger` *did* cause the host to quantize Q-Link increments on stepped targets, so the host does inspect `effGetParameterProperties` in at least one code path on 3.10.1.12. | `[DOC]` `mpc_vst2_abi.h:363-391`, `host-contract.md:59`; `[COMMUNITY]` `NOTES.md:408-409`; `[MEASURED]` `shell-mpc/synthyboy_mpc.cpp:6015-6016` |
| **`effString2Parameter` (VST2 opcode 27)** | Not applicable. | **Unused by host.** `[DOC]` `host-contract.md:59`: *"it does not call the text-to-value opcode either."* `[MEASURED]` XL 3.10.1.12 disassembly (`docs/dsp-knowledge/native-text-entry.md:74-76`) confirms `PluginWrapperElement` string setter is a no-op. | `[DOC]` `host-contract.md:59`; `[MEASURED]` `docs/dsp-knowledge/native-text-entry.md:74-76, 119-121` |
| **`.vstxml` (`VSTParametersStructure` / `ValueType`)** | Not applicable. | **Ignored by `MenuOverlay`.** `[COMMUNITY]` `NOTES.md:428-435`: JUCE's `juce::VSTXMLInfo` compiled into `/usr/bin/MPC` parsed a valid `.vstxml` (stopping `effGetParamName` calls for XML-named params), yet all four `MenuOverlay` menus still opened empty. | `[COMMUNITY]` `NOTES.md:428-435` |
| **`ComboBox` widget items list in `TUI.json`** | Unused (0 stock skins). | **No `items` field exists.** Schema has only `version`, `textStyle`, `handleName`. SDK explicitly marks it empty for VST parameters. | `[DOC]` `ui-reference.md` §6.8 (L1060-1081), §10 (L2220-2221) |
| **Program names (`effGetProgramNameIndexed`)** | Populates `PRESET` drawer (or `.xpl` files do). | **Only populates the top-bar `PRESET` drawer** (flat list; `effGetNumProgramCategories` is never called — `0` calls in Key 61 status log — and `effGetProgramNameIndexed` always passes category `-1`). Never feeds parameter overlays. | `[MEASURED]` `docs/dsp-knowledge/skin-host-behaviour.md:116, 127`; `device-observed/lists-key61-20261128/README.md:28` |

### 3.2 Opcode Comparison: `shell-mpc/synthyboy_mpc.cpp` vs Official SDK `framework/mpc_plugin.h`

Comparing what a choice parameter reports in our plugin (`shell-mpc/synthyboy_mpc.cpp`) versus the official SDK reference implementation (`docs-official/mpc-sdk/framework/mpc_plugin.h:668-705`):

| VST2 Opcode | Official SDK `framework/mpc_plugin.h` (for `Param::Kind::Choice`) | SynthyBoy `shell-mpc/synthyboy_mpc.cpp` (for stepped choice params & Play selectors) | Differences |
|---|---|---|---|
| **`effGetParameterProperties`** (56) | `mpc_plugin.h:693-705`: copies `pr.name` into `pp->label` and `pp->shortLabel`; sets `pp->flags = kVstParameterUsesIntegerMinMax \| kVstParameterUsesIntStep` (NO `kVstParameterCanRamp`); `minInteger = pr.min`, `maxInteger = pr.max`, `stepInteger = 1`, `largeStepInteger = 1`; returns `1`. | Engine choices (`fillParameterProperties`, `synthyboy_mpc.cpp:3703-3723`, called at `6096`): copies `unitLabel(info.unit)` (empty `""` for choices) into `props->label`; leaves `shortLabel` empty; sets `props->flags = kVstParameterCanRamp \| kVstParameterUsesIntegerMinMax` (**NO `kVstParameterUsesIntStep`**); sets `minInteger`, `maxInteger`, `stepInteger = 1`, `largeStepInteger = 0`. Play selectors (`6033-6043`, tested in XL `r4`): `label = ""`, `flags = kVstParameterUsesIntegerMinMax` (no `kVstParameterUsesIntStep`), `minInteger = 0`, `maxInteger = n - 1`, `stepInteger = 1`, `largeStepInteger = 0`, `stepFloat = 1/(n-1)`. | 1. Missing `kVstParameterUsesIntStep` flag in SynthyBoy.<br>2. `kVstParameterCanRamp` set on stepped engine params in SynthyBoy.<br>3. `props->label` is unit (`""`) instead of parameter name, and `shortLabel` is empty.<br>4. `largeStepInteger` is `0` instead of `1`. |
| **`effGetParamDisplay`** (7) | `mpc_plugin.h:671-673`: writes current choice string into `ptr` (`kVstMaxParamStrLen` cap), returns `0`. | `synthyboy_mpc.cpp:3731-3765, 5157-5159` (and `5071-5090` for Play): writes current choice string into `ptr` via `engineParamDisplay` / `valueToText`, returns `1`. | Functionally equivalent for display text; return code differs (`1` vs `0`, harmless since host reads `ptr`). |
| **`effString2Parameter`** (27) | `mpc_plugin.h:679-692`: matches `str` against `pr.choices[i]` and calls `p->set(index, (float)i)`, returning `1`. | **Not handled** (no `case effString2Parameter:` in `synthyboy_mpc.cpp`; falls through to default `0`). | Unimplemented in SynthyBoy, though `[DOC]` `host-contract.md:59` and `[MEASURED]` XL disassembly confirm MPC never calls it. |
| **`effCanBeAutomated`** (26) | `mpc_plugin.h:677-678`: returns `1` for every automatable parameter (all `Choice` params in `SimpleSynth` and `StereoDelay` return `1`). | `synthyboy_mpc.cpp:5311-5321`: returns `1` for engine parameters, but **`0` for `isPlayParam`, `isNativeUiParam`, `isOverlayParam`, `isQParam`**. | **Critical difference for XL `r4`:** all three parameters tested with bare `MenuOverlay` + `replacementStrings` in `r4` (`Parameter 2676/2677/2678`) returned `effCanBeAutomated == 0`. Engine choice parameters (e.g. `Parameter 21`, `Parameter 88`) return `1`. |

---

## 4. Audit of Our Old SynthyBoy `MenuOverlay` Definitions vs the Stock Pattern

### 4.1 Relevant Git History (`git log --all --oneline -S'MenuOverlay' -- skin`)

Selected commits tracing our `MenuOverlay` usage before and during migration:

- `f670f2f1` (2026-11-22) — `Keep Play markers outside native selectors` (generated by official SDK `tools/gen_ui.py` + `scripts/mpc-skin-extras/82_play.py`; tested on XL in `compact-native-q-xl-r4`).
- `ddfc962f` — `Freeze the skin: skin/ is the committed source`.
- `e3693360` — `Skin split into per-screen definition files with the host importFiles`.
- `67713013` (2026-11-28) — `Knowledge: the host MenuOverlay draws no item names for a VST2 (own rows required)`.
- `a6732940` (2026-11-28) — `Play skin: own-row MenuOverlays, plugin list for long lists, Master focus pattern`.
- `e22fd809` (2026-11-28) — `Choice lists: the skin opens the plugin list from every dropdown; validator checks the pairing` (removed bare `fx editor choice menu` and `oscNativeRoutingChoices`).
- `c35441a0` (2026-11-28) — `feat(mpc-skin): wire WARP choice fields to plugin two-level list` (removed `oscWarpChoices`).
- `f755d56d`, `4d0049ee`, `0c050490`, `9988b274` (2026-11-28) — migrated remaining own-row `MenuOverlay` defs on Osc (`oscNativeSetting_menu_TYPEChoices`, `WAVEFORMChoices`, `KINDChoices`, `SOUNDChoices`), LFO, Filter, FX.

### 4.2 Exact Inspection of Our Old Definitions

Our repository had **two fundamentally different `MenuOverlay` patterns**, and conflating their failure symptoms caused our confusion:

#### Pattern A: Bare `MenuOverlay` (e.g. `f670f2f1` `"menu overlay"`, `a6732940~1:skin/defs_common.json:7-85` `fx editor choice menu`, `a6732940~1:skin/defs_play.json:453-563` `play osc routing`, `HEAD:skin/defs_filter.json:7-80` `filterNativeChainChoices`)

Excerpt of `fx editor choice menu` (`git show a6732940~1:skin/defs_common.json`, L7-61):
```json
{
  "key": "fx editor choice menu",
  "value": {
    "version": 2,
    "actions": [],
    "backgroundData": {
      "version": 1,
      "focussed": { "version": 1, "colour": "ff1e1d1e", "image": "" },
      "unfocussed": { "version": 1, "colour": "ff1e1d1e", "image": "" }
    },
    "ignoreMousePresses": true,
    "disableCoarseDataWheel": false,
    "componentsData": [
      {
        "version": 2,
        "componentData": {
          "version": 1,
          "name": "overlay",
          "type": "MenuOverlay",
          "data": {
            "version": 1,
            "version": 1,
            "actions": [
              { "version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": "" }
            ],
            "withNumbers": false,
            "handleName": "Data"
          }
        },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": { "version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 1280 800" }
      },
      {
        "version": 2,
        "componentData": { "version": 1, "name": "Native Modal Markers", "type": "Image", ... },
        ...
      }
    ]
  }
}
```
Note: `f670f2f1`'s `"menu overlay"` (generated by `docs-official/mpc-sdk/tools/gen_ui.py:515-517,592-594,673` with duplicate `"version": 1` preserved by `apply-mpc-skin-extras.py:24-45`) had the exact same shape (without `Native Modal Markers`), and on XL `r4` (`menu-osc-host.png`, `menu-lfo-host.png`) it still rendered blank rows while drawing the check mark and scroll bar.

#### Pattern B: Own-Row Hybrid `MenuOverlay` (e.g. `a6732940~1:skin/defs_play.json:7-452` `oscNativeSourceChoices`, `e3693360:skin/defs_osc.json:390-500` `oscNativeRoutingChoices`, `HEAD:skin/defs_osc.json:4927-5050` `oscNativeSetting_menu_TYPEChoices`, `c35441a0~1:skin/defs_osc.json:5719-5790` `oscWarpChoices`)

Excerpt of `oscNativeSetting_menu_TYPEChoices` (`skin/defs_osc.json:4927-5050`; `oscNativeSourceChoices` in `a6732940~1:skin/defs_play.json:7-200` is identical in structure):
```json
{
  "key": "oscNativeSetting_menu_TYPEChoices",
  "value": {
    "version": 2,
    "actions": [],
    "backgroundData": {
      "version": 1,
      "focussed": { "version": 1, "colour": "ff1e1d1e", "image": "" },
      "unfocussed": { "version": 1, "colour": "ff1e1d1e", "image": "" }
    },
    "ignoreMousePresses": false,
    "disableCoarseDataWheel": false,
    "hideQLinkBounds": true,
    "componentsData": [
      { "componentData": { "name": "oscNativeSetting_menu_TYPEChoices background", "type": "Image", "data": { "version": 2, "imageType": "Regular", "colour": "0", "image": "osc_routing_overlay_bg.png" } }, "bounds": { "bounds": "0 0 1280 800" } },
      {
        "componentData": {
          "name": "oscNativeSetting_menu_TYPEChoices close",
          "type": "MenuOverlay",
          "data": {
            "version": 1,
            "actions": [
              { "version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": "" }
            ],
            "withNumbers": false,
            "handleName": "Data"
          }
        },
        "bounds": { "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 1280 800" }
      },
      { "componentData": { "name": "Wavetable", "type": "Label", "data": { "type": "Text", "handleName": "Data", "text": "Wavetable" } }, "bounds": { "bounds": "42 110 342 80" } },
      { "componentData": { "name": "Choose Wavetable", "type": "Button", "data": { "version": 2, "onImage": "native_button_hit.png", "offImage": "native_button_hit.png", "buttonId": 0, "numButtonsInGroup": 5, "handleName": "Data", "gestureBehaviour": "Instant" } }, "bounds": { "acceptsHWFocus": "Yes", "bounds": "0 110 426 80" } }
    ]
  }
}
```
Notice the exact same missing duplicate `"version": 1` bug also affected `oscNativeRoutingChoices` (`e3693360:skin/defs_osc.json:442-460`), `oscWarpChoices` (`c35441a0~1:skin/defs_osc.json:5771-5785`), and all remaining own-row drawers in `skin/defs_osc.json`, `defs_fx.json`, and `defs_lfo.json` (verified programmatically: only `filterNativeChainChoices` in `defs_filter.json:32-36` kept both `"version": 1` keys).

### 4.3 Side-by-Side Diff Against Stock Pattern — And Why Each Observed Symptom Happened

| Field / Aspect | Stock pattern (`Fabric XL` / `Sub Factory` + `Generic Menu Overlay.json`) | Our Bare `MenuOverlay` (`f670f2f1` `r4` / `fx editor choice menu`) | Our Own-Row Hybrid (`oscNativeSourceChoices` / `TYPEChoices` / `oscWarpChoices`) | Effect on Observed Symptoms |
|---|---|---|---|---|
| **Duplicate `"version": 1` in `MenuOverlay.data`** | Two keys: `"version": 1, "version": 1` (`[STOCK]` `Generic Text Overlay.json:35-36`, `[DOC]` `ui-reference.md` §1.8) | Two keys (`"version": 1, "version": 1`) | **BROKEN: only ONE `"version": 1`** | `[HYPOTHESIS]` May break `MenuOverlay` base-class action (`Focus Lost`) parsing on own-row overlays; does NOT explain blank rows in `r4` (where both keys were present). |
| **`ignoreMousePresses` on overlay root** | `true` (`[STOCK]` `Generic Text Overlay.json:25`, `[DOC]` §6.9) | `true` (except `filterNativeChainChoices`: `false`) | **`false`** | `[HYPOTHESIS]` Intercepts clicks outside the drawer (`x > 426`) on the root custom component instead of letting `MenuOverlay` handle `Focus Lost`. |
| **Overlay `backgroundData`** | Transparent `"colour": "0"` (`[STOCK]` `Generic Text Overlay.json:16,21`, `[DOC]` §6.9) | Opaque `"ff1e1d1e"` (copied from SDK `gen_ui.py:673`) | Opaque `"ff1e1d1e"` **plus** full-screen `Image` `osc_routing_overlay_bg.png` (`0 0 1280 800`) | `[MEASURED]` Directly explains solid dark-grey right half (`x = 426..1280`) instead of blurred/dimmed page (`none-1open-host.png`, `menu-osc-host.png` vs `fabric-xl-layer1-bank-list.png`). |
| **Own `Label` + radio `Button` rows on top of `MenuOverlay`** | **None** (single child: `MenuOverlay`) | None (except small `Native Modal Markers` `Image` at `16 120 30 6`) | **Yes:** `Label` + radio `Button` (`0 110+80k 426 80`, `acceptsHWFocus: "Yes"`) drawn AFTER `MenuOverlay` | **`[MEASURED]` + `[HYPOTHESIS]` Directly explains the 2026-11-27 Key 61 list probe (`list-probe-results/none-*`, `A-*`, `B-*`):** tapping a row hit our own `Choose ...` `Button`, which wrote the parameter (moving `MenuOverlay`'s check mark from row 0 to row 1 in `none-2row-host.png`) while **never passing the touch to `MenuOverlay`** — so `MenuOverlay` never closed! Only tapping the uncovered header `(106, 49)` finally reached `MenuOverlay` and closed it. |
| **Opener `Show Overlay` `handleName`** | **`""`** (empty string) in all 7 stock skins | `"Data"` (from SDK `gen_ui.py:523-524`) | `"Data"` | Untested difference (`r4` still resolved the parameter name and step count via `"Data"`, so binding works, but `""` has never been tested for VST2). |
| **`MenuOverlay` `bounds.acceptsHWFocus`** | `"No"` (`Generic Text Overlay.json:56`, `gen_ui.py:593`), vs `"Yes"` (`ui-reference.md:1957`) | `"No"` | `"No"` | Untested whether `"Yes"` affects `Focus Lost` or text rendering. |
| **Raw parameter name in header** (`"OSC ABC SETTING 1"` / `"PLAY OSC"`) | Shows parameter's `effGetParamName` uppercase (`"LAYER 1 BANK"`). | Same (`"PLAY OSC"`). | Same (`"PLAY OSC"`). | `[DOC]` `docs/MPC.md:685` / `[MEASURED]`: `MenuOverlay` always draws the bound parameter's host name (`effGetParamName`) as its header; there is no skin key to override the header text. |

### 4.4 Is There a Stock `closeOnSelect` Option?

- **`[DOC]` / `[STOCK]` No.** `MenuOverlay.data` has no `closeOnSelect` or `autoClose` field — its entire schema is `version` (x2), `actions` (`Focus Lost` only), `withNumbers` (bool), and `handleName` (`ui-reference.md` §6.11 L1178-1199; `Generic Text Overlay.json:34-48`).
- **`[MEASURED]`** In stock plugins (Fabric XL on Key 61, `fabric-xl-after-row-tap-leads.png`), **one tap on a row selects AND closes by default** with the standard `MenuOverlay` (`Focus Lost` action only). Our 2026-11-27 Key 61 conclusion *"Select-and-close is therefore not possible with the stock MenuOverlay"* (`docs/dsp-knowledge/skin-host-behaviour.md:8`) was drawn from probes `A` and `B` on **`oscNativeSourceChoices`**, where our own `Choose ...` `Button`s were sitting on top of `MenuOverlay` and intercepting row taps. Whether a **bare** `MenuOverlay` (no own buttons on top) closes on one row tap for an external VST2 has **not** been measured on hardware.

---

## 5. Two-Level Lists for a Parameter

**Conclusion: The host's parameter list (`MenuOverlay`) is strictly flat.**

- `[DOC]` **Official SDK (`ui-reference.md` §6):** There is no `ListOverlay`, tree, nested menu, or grouped-choice widget in the skin format. `MenuOverlay` (§6.11) takes a single scalar `handleName` and renders one flat list.
- `[STOCK]` **All 7 stock skins inspected** (`Fabric XL`, `OPx-4`, `Sub Factory`, `AIR Reverb Pro`, `Visual EQ4`, `Multiband Filterbank`, `xl-midi-TUI.json`): Zero stock skins implement a two-level list for a parameter.
  - **Fabric XL** (`MPC_-_Fabric_XL.TUI.json:15017-15075`, `[MEASURED]` Key 61 `fabric-xl-main.png`, `fabric-xl-layer1-bank-list.png`, `fabric-xl-layer1-samples-list.png`) achieves two-level category → sound browsing using **two separate flat parameters** (`Sampler 1 Bank` + `Sampler 1 Samples`) and two flat `MenuOverlay` drawers; picking a bank updates the second parameter's flat list inside the internal C++ instrument.
  - **Sub Factory** (`Qubiq_-_Sub_Factory.TUI.json:9118-9146`) and **OPx-4** (`MPC_-_OPx-4.TUI.json:34865-34890`) expose long wavetable/sample parameters (`Parameter 10`, `Parameter 17`, `Parameter 24`, `Parameter 436`) as single flat `MenuOverlay` drawers.
- `[MEASURED]` **The host's only two-level left panel is the top-bar `PRESET` drawer** (Fabric XL `preset-list-top-level.png` → `preset-list-leads-classic.png`). Key 61 inspection (`device-observed/lists-key61-20261128/README.md:64-70`, `docs/dsp-knowledge/skin-host-behaviour.md:121-128`) proved the host builds that two-level view exclusively from flat `Presets/NNNN-<Category>-<Name>.xpl` filenames (not from any skin widget, and not from VST2 program categories — `effGetNumProgramCategories` is called 0 times).

---

## 6. Verdict Table & Minimal Device Measurement

| Use case | Host list possible? | Evidence |
|---|---|---|
| **1. Flat list of a parameter's static choices** (fixed names known at build time, via `replacementStrings` + bare `MenuOverlay`) | **`unknown`** *(strong prior toward **`no`** for row text)* | `[DOC]` Official SDK `ui-reference.md` §3.3, §6.11, §9.4 and `tools/gen_ui.py` explicitly claim `replacementStrings` + `MenuOverlay` renders named choices. `[MEASURED]` On MPC XL 3.10.1.12 (`compact-native-q-xl-r4`, `f670f2f1`), a bare `gen_ui.py` `MenuOverlay` + `replacementStrings` **did** allocate N rows, place the check mark, and draw a scroll bar for 10 items, **but rendered zero row text**. However, `r4` tested only 3 non-automatable Play proxy params (`effCanBeAutomated == 0`), lacked `kVstParameterUsesIntStep`, used `Show Overlay` `handleName: "Data"` instead of stock `""`, used opaque `backgroundData: "ff1e1d1e"` instead of `"0"`, used `acceptsHWFocus: "No"` instead of SDK §9.1 `"Yes"`, and never tested tapping a bare row or tapping outside. Our Key 61 tap-to-close failure (`list-probe-results`) tested a broken own-row hybrid (`oscNativeSourceChoices`), not a bare `MenuOverlay`. One minimal probe below settles every remaining variable. |
| **2. Two-level / grouped list for a parameter** (e.g. WARP grouped by kind, Mod Source/Target grouped by category) | **`no`** | `[DOC]` No nested/grouped list widget exists in `ui-reference.md`. `[STOCK]` Zero of 7 stock skins have a two-level parameter list (Fabric XL uses two separate flat parameters, which for a VST2 would still hit Case 3 because the second parameter's `replacementStrings` cannot change at runtime when the category changes). `[MEASURED]` The host's only two-level left panel is the `.xpl` `PRESET` drawer, unreachable from skin parameters. |
| **3. File / wavetable / dynamic browser with runtime-generated names** (e.g. user wavetables, samples, per-model filter styles, live mod targets) | **`no`** | `[DOC]` `replacementStrings` is static JSON loaded once from `TUI.json` (`ui-reference.md` §3.3); VST2 has no opcode to pass a dynamic N-string choice table to `MenuOverlay` (`effGetParamDisplay` returns only the current value's string; `VstParameterProperties` has no string-array field; `host-contract.md:57-59`). `[COMMUNITY]` `.vstxml` value strings are ignored by `MenuOverlay` (`NOTES.md:428-435`). `[MEASURED]` Confirmed on Key 61 (`docs/dsp-knowledge/skin-host-behaviour.md:16-17`). Dynamic lists must stay plugin-driven (`nativeListModal` / `NativeListCells`). |

### ONE Minimal Device Measurement to Settle Case 1 (`unknown`)

Run **once** on MPC Key 61 (3.10.1.12) using one trial skin edit on the existing SynthyBoy build (plus, optionally, one check of the unmodified official SDK `SimpleSynth` `LFO Shape` / `LFO Div` as an independent positive control on the same firmware):

#### Exact Trial Skin Snippet

1. Keep `Parameter 88` (`filter1.model`, 27 choices, `effCanBeAutomated == 1`) in `pageData.replacementStrings` (already present in `skin/TUI.json`: `["Analog", "Dirty", "Ladder", ...]`).
2. Add one **exact stock-shape** bare overlay definition (`probeMenuOverlayClean` — zero extra children, transparent `"0"` background, `ignoreMousePresses: true`, BOTH `"version": 1` keys, `acceptsHWFocus: "Yes"`):

```json
{
  "key": "probeMenuOverlayClean",
  "value": {
    "version": 2,
    "actions": [],
    "backgroundData": {
      "version": 1,
      "focussed": { "version": 1, "colour": "0", "image": "" },
      "unfocussed": { "version": 1, "colour": "0", "image": "" }
    },
    "ignoreMousePresses": true,
    "disableCoarseDataWheel": false,
    "componentsData": [
      {
        "version": 2,
        "componentData": {
          "version": 1,
          "name": "overlay",
          "type": "MenuOverlay",
          "data": {
            "version": 1,
            "version": 1,
            "actions": [
              { "version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": "" }
            ],
            "withNumbers": false,
            "handleName": "Data"
          }
        },
        "handle remapping": { "version": 1, "map": [] },
        "bounds": {
          "version": 1,
          "acceptsHWFocus": "Yes",
          "showWhenDataModelInvalid": "Show",
          "whenVisible": "Always",
          "boundsType": "Absolute",
          "bounds": "0 0 1280 800"
        }
      }
    ]
  }
}
```

3. On one test tab (e.g. Filter 1), place two side-by-side bare opener definitions bound to `Parameter 88` (`filter1.model`):
   - **Opener A (stock `handleName: ""`, `Double Click`):**
     ```json
     {
       "key": "probeOpenerStock",
       "value": {
         "version": 4,
         "actions": [
           { "version": 2, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "", "additionalData": "probeMenuOverlayClean", "handle remapping": { "version": 1, "map": [] } },
           { "version": 2, "onAction": "Enter Pressed", "handler": "Show Overlay", "handleName": "", "additionalData": "probeMenuOverlayClean", "handle remapping": { "version": 1, "map": [] } },
           { "version": 2, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": "", "handle remapping": { "version": 1, "map": [] } }
         ],
         "backgroundData": { "version": 1, "focussed": { "version": 1, "colour": "0", "image": "" }, "unfocussed": { "version": 1, "colour": "0", "image": "" } },
         "ignoreMousePresses": false,
         "disableCoarseDataWheel": true,
         "repeats": 1,
         "hideQLinkBounds": false,
         "componentsData": [
           {
             "version": 2,
             "componentData": {
               "version": 1, "name": "Label", "type": "Label",
               "data": { "version": 1, "textStyle": { "version": 1, "font": { "version": 1, "name": "Titillium Web", "style": "Regular", "height": 28.0 }, "colour": "ffffffff", "justification": "left verticallyCentred", "case": "Original" }, "type": "Value", "handleName": "Data" }
             },
             "handle remapping": { "version": 1, "map": [] },
             "bounds": { "version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Proportional", "bounds": "0 0 1 1", "additionalInvalidatingHandles": [] }
           }
         ]
       }
     }
     ```
   - **Opener B (SDK `handleName: "Data"`, `Mouse Down`):** identical to Opener A, except `onAction: "Mouse Down"` and `"handleName": "Data"`.
   - *(Optional C++ variant if Opener A & B both render blank rows: add `| kVstParameterUsesIntStep` and `props->largeStepInteger = 1` + `props->label = info.name` in `fillParameterProperties`, matching `framework/mpc_plugin.h:699-703`, and retest Opener A.)*

#### What to Tap and Look For (5 Checks)

1. **Double-tap Opener A** (`Parameter 88`, 27 choices in `replacementStrings`), capture host plane:
   - Do the 27 row names (`Analog`, `Dirty`, `Ladder`, …) render inside `x = 0..426, y = 110..800`?
   - Is `x = 426..1280` blurred/dimmed (proving transparent `backgroundData: "0"` works)?
   - Is a vertical scroll bar drawn and does dragging vertically scroll the drawer?
2. **Tap row 2 once** (`x = 150, y = 310`, `"Ladder"`), capture host plane:
   - Does `Parameter 88` change to `"Ladder"` **AND** does the drawer close immediately on that single tap (matching Fabric XL)?
3. **Double-tap Opener A again, then tap outside the drawer** (`x = 900, y = 400`), capture host plane:
   - Does the drawer close without changing `Parameter 88`?
4. **Single-tap Opener B** (`Mouse Down`, `handleName: "Data"`):
   - Does single-tap open cleanly without immediately dismissing on release, and does it behave identically to Opener A regarding row text and one-tap close?
5. **Positive control (only if A and B still show blank rows):** Load the unmodified official SDK `SimpleSynth` example on the same Key 61 (3.10.1.12) and double-tap `LFO Shape` (`overlay: "menu"`). If `SimpleSynth` also opens blank rows, the defect is 100% inside the MPC 3.10.1.12 host's VST2 `MenuOverlay` text binding (`no` for Case 1 on this firmware); if `SimpleSynth` shows row names, diff its `TUI.json` and `mpc_plugin.h` `effGetParameterProperties` against ours.

Save the captures under `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/` and record the final `yes`/`no` verdict in `docs/dsp-knowledge/skin-host-behaviour.md`.
