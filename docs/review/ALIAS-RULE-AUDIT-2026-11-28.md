# Alias Rule Audit (B1, B2, and NonZero Gates) — 2026-11-28

Generated from `python3 scripts/validate_synthyboy_skin.py --list-alias-rules skin` (post-Part A).

- **B1** (modifier over non-`Parameter ...` operand): **357** findings across **13** definitions (`native keyboard overlay` and `oscWarpChoices` wrap `Local/...` handles; all other **11** definitions wrap aliases).
- **B2** (custom-component instance with non-empty remap, no `Data` key, `showWhenDataModelInvalid: "Hide"`): **76** instances across **11** component types.
- Total distinct definitions involved: **23**.

## Osc Detail Grid Slot Arithmetic (`oscDetailGrid`)

- `oscDetailGrid` (`skin/defs_osc.json:18966`) has `version: 3`, `repeats: 8` (`col = 0..7`), with 4 tile rows (`row = 0..3`).
- Slot index per `(col, row)`: `slot = col * 4 + row` (`0..31`).
- Parameters passed by `oscDetailGrid` row entries (`Base + col * 4`, `Step = 4`):
  - Row 0 (`0`, `oscDetailTileNoMenu`): `Value = 3335 + 4*col`, `Step = 3367 + 4*col`, `Kind = 3399 + 4*col`
  - Row 1 (`1`, `oscDetailTilem0m1m2m4m7m8`): `Value = 3336 + 4*col`, `Step = 3368 + 4*col`, `Kind = 3400 + 4*col`
  - Row 2 (`2`, `oscDetailTilem5m6m9`): `Value = 3337 + 4*col`, `Step = 3369 + 4*col`, `Kind = 3401 + 4*col`
  - Row 3 (`3`, `oscDetailTilem3`): `Value = 3338 + 4*col`, `Step = 3370 + 4*col`, `Kind = 3402 + 4*col`
- Menu handle parameters (`3434..3443` = `kMenusBase + menu_idx`, fixed across columns):
  - `m0..m9` (`kind = 7..16`): `Parameter 3434` (`7`) .. `Parameter 3443` (`16`).
- Rewrite without unrolling `repeats: 8`: `oscDetailGrid` applies the modifiers directly to `Parameter {Increment:Base=...,Step=4}` on its `"0".."3"` tile instances (`Kind1..Kind16`, `KindMenu`, `DataAct`, `Step`, `Data`, gate `RangedEnabling/1/16/17/Parameter {Increment:Base=3399+row,Step=4}`), and `oscDetailTile*` uses plain aliases only and maps `Data` (and `Step` for `oscDetailStepper`) on every child.

## Audit by Definition

### 1. `defs_osc.json:oscDetailGrid` (`skin/defs_osc.json:18966`, `repeats=8`, instances=1)

**B1 violations (4):**

- `0` (`skin/defs_osc.json:18991`), `gate`: `NonZero/Kind` (operand `Kind`)
- `1` (`skin/defs_osc.json:19031`), `gate`: `NonZero/Kind` (operand `Kind`)
- `2` (`skin/defs_osc.json:19071`), `gate`: `NonZero/Kind` (operand `Kind`)
- `3` (`skin/defs_osc.json:19111`), `gate`: `NonZero/Kind` (operand `Kind`)

**Instances (1):**

- `defs_osc.json:oscNativeDetail` :: `Grid` (`skin/defs_osc.json:19175`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3334`]; gates [`RangedEnabling/3/5/6/Parameter 3334`]

### 2. `defs_osc.json:oscDetailTileNoMenu` (`skin/defs_osc.json:24848`, `repeats=None`, instances=1)

**B1 violations (7):**

- `1` (`skin/defs_osc.json:24872`), `gate`: `IndexedEnabling/1/17/Kind` (operand `Kind`)
- `2` (`skin/defs_osc.json:24904`), `gate`: `IndexedEnabling/2/17/Kind` (operand `Kind`)
- `3` (`skin/defs_osc.json:24936`), `gate`: `IndexedEnabling/3/17/Kind` (operand `Kind`)
- `4` (`skin/defs_osc.json:24968`), `gate`: `IndexedEnabling/4/17/Kind` (operand `Kind`)
- `5` (`skin/defs_osc.json:25000`), `gate`: `IndexedEnabling/5/17/Kind` (operand `Kind`)
- `6` (`skin/defs_osc.json:25032`), `remap[Data]`: `FromGesture/Data` (operand `Data`)
- `6` (`skin/defs_osc.json:25032`), `gate`: `IndexedEnabling/6/17/Kind` (operand `Kind`)

**Instances (1):**

- `defs_osc.json:oscDetailGrid` :: `0` (`skin/defs_osc.json:18991`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter {Increment:Base=3335,Step=4}`, `Step->FromGesture/Parameter {Increment:Base=3367,Step=4}`, `Kind->Parameter {Increment:Base=3399,Step=4}`]; gates [`NonZero/Kind`]

### 3. `defs_osc.json:oscDetailTilem0m1m2m4m7m8` (`skin/defs_osc.json:25068`, `repeats=None`, instances=1)

**B1 violations (14):**

- `1` (`skin/defs_osc.json:25092`), `gate`: `IndexedEnabling/1/17/Kind` (operand `Kind`)
- `2` (`skin/defs_osc.json:25124`), `gate`: `IndexedEnabling/2/17/Kind` (operand `Kind`)
- `3` (`skin/defs_osc.json:25156`), `gate`: `IndexedEnabling/3/17/Kind` (operand `Kind`)
- `4` (`skin/defs_osc.json:25188`), `gate`: `IndexedEnabling/4/17/Kind` (operand `Kind`)
- `5` (`skin/defs_osc.json:25220`), `gate`: `IndexedEnabling/5/17/Kind` (operand `Kind`)
- `6` (`skin/defs_osc.json:25252`), `remap[Data]`: `FromGesture/Data` (operand `Data`)
- `6` (`skin/defs_osc.json:25252`), `gate`: `IndexedEnabling/6/17/Kind` (operand `Kind`)
- `Menu face` (`skin/defs_osc.json:25288`), `gate`: `RangedEnabling/7/16/17/Kind` (operand `Kind`)
- `7` (`skin/defs_osc.json:25320`), `gate`: `IndexedEnabling/7/17/Kind` (operand `Kind`)
- `8` (`skin/defs_osc.json:25356`), `gate`: `IndexedEnabling/8/17/Kind` (operand `Kind`)
- `9` (`skin/defs_osc.json:25392`), `gate`: `IndexedEnabling/9/17/Kind` (operand `Kind`)
- `11` (`skin/defs_osc.json:25428`), `gate`: `IndexedEnabling/11/17/Kind` (operand `Kind`)
- `14` (`skin/defs_osc.json:25464`), `gate`: `IndexedEnabling/14/17/Kind` (operand `Kind`)
- `15` (`skin/defs_osc.json:25500`), `gate`: `IndexedEnabling/15/17/Kind` (operand `Kind`)

**Instances (1):**

- `defs_osc.json:oscDetailGrid` :: `1` (`skin/defs_osc.json:19031`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter {Increment:Base=3336,Step=4}`, `Step->FromGesture/Parameter {Increment:Base=3368,Step=4}`, `Kind->Parameter {Increment:Base=3400,Step=4}`]; gates [`NonZero/Kind`]

### 4. `defs_osc.json:oscDetailTilem5m6m9` (`skin/defs_osc.json:25536`, `repeats=None`, instances=1)

**B1 violations (11):**

- `1` (`skin/defs_osc.json:25560`), `gate`: `IndexedEnabling/1/17/Kind` (operand `Kind`)
- `2` (`skin/defs_osc.json:25592`), `gate`: `IndexedEnabling/2/17/Kind` (operand `Kind`)
- `3` (`skin/defs_osc.json:25624`), `gate`: `IndexedEnabling/3/17/Kind` (operand `Kind`)
- `4` (`skin/defs_osc.json:25656`), `gate`: `IndexedEnabling/4/17/Kind` (operand `Kind`)
- `5` (`skin/defs_osc.json:25688`), `gate`: `IndexedEnabling/5/17/Kind` (operand `Kind`)
- `6` (`skin/defs_osc.json:25720`), `remap[Data]`: `FromGesture/Data` (operand `Data`)
- `6` (`skin/defs_osc.json:25720`), `gate`: `IndexedEnabling/6/17/Kind` (operand `Kind`)
- `Menu face` (`skin/defs_osc.json:25756`), `gate`: `RangedEnabling/7/16/17/Kind` (operand `Kind`)
- `12` (`skin/defs_osc.json:25788`), `gate`: `IndexedEnabling/12/17/Kind` (operand `Kind`)
- `13` (`skin/defs_osc.json:25824`), `gate`: `IndexedEnabling/13/17/Kind` (operand `Kind`)
- `16` (`skin/defs_osc.json:25860`), `gate`: `IndexedEnabling/16/17/Kind` (operand `Kind`)

**Instances (1):**

- `defs_osc.json:oscDetailGrid` :: `2` (`skin/defs_osc.json:19071`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter {Increment:Base=3337,Step=4}`, `Step->FromGesture/Parameter {Increment:Base=3369,Step=4}`, `Kind->Parameter {Increment:Base=3401,Step=4}`]; gates [`NonZero/Kind`]

### 5. `defs_osc.json:oscDetailTilem3` (`skin/defs_osc.json:25896`, `repeats=None`, instances=1)

**B1 violations (9):**

- `1` (`skin/defs_osc.json:25920`), `gate`: `IndexedEnabling/1/17/Kind` (operand `Kind`)
- `2` (`skin/defs_osc.json:25952`), `gate`: `IndexedEnabling/2/17/Kind` (operand `Kind`)
- `3` (`skin/defs_osc.json:25984`), `gate`: `IndexedEnabling/3/17/Kind` (operand `Kind`)
- `4` (`skin/defs_osc.json:26016`), `gate`: `IndexedEnabling/4/17/Kind` (operand `Kind`)
- `5` (`skin/defs_osc.json:26048`), `gate`: `IndexedEnabling/5/17/Kind` (operand `Kind`)
- `6` (`skin/defs_osc.json:26080`), `remap[Data]`: `FromGesture/Data` (operand `Data`)
- `6` (`skin/defs_osc.json:26080`), `gate`: `IndexedEnabling/6/17/Kind` (operand `Kind`)
- `Menu face` (`skin/defs_osc.json:26116`), `gate`: `RangedEnabling/7/16/17/Kind` (operand `Kind`)
- `10` (`skin/defs_osc.json:26148`), `gate`: `IndexedEnabling/10/17/Kind` (operand `Kind`)

**Instances (1):**

- `defs_osc.json:oscDetailGrid` :: `3` (`skin/defs_osc.json:19111`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter {Increment:Base=3338,Step=4}`, `Step->FromGesture/Parameter {Increment:Base=3370,Step=4}`, `Kind->Parameter {Increment:Base=3402,Step=4}`]; gates [`NonZero/Kind`]

### 6. `defs_osc.json:oscDetailStepper` (`skin/defs_osc.json:18571`, `repeats=None`, instances=4)

**B2 violations (4 instances missing `Data` with `Hide`):**

- `defs_osc.json:oscDetailTileNoMenu` :: `5` (`skin/defs_osc.json:25000`), remap keys `['Caption']`, gates `['IndexedEnabling/5/17/Kind']`
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `5` (`skin/defs_osc.json:25220`), remap keys `['Caption']`, gates `['IndexedEnabling/5/17/Kind']`
- `defs_osc.json:oscDetailTilem5m6m9` :: `5` (`skin/defs_osc.json:25688`), remap keys `['Caption']`, gates `['IndexedEnabling/5/17/Kind']`
- `defs_osc.json:oscDetailTilem3` :: `5` (`skin/defs_osc.json:26048`), remap keys `['Caption']`, gates `['IndexedEnabling/5/17/Kind']`

**Instances (4):**

- `defs_osc.json:oscDetailTileNoMenu` :: `5` (`skin/defs_osc.json:25000`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/5/17/Kind`]
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `5` (`skin/defs_osc.json:25220`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/5/17/Kind`]
- `defs_osc.json:oscDetailTilem5m6m9` :: `5` (`skin/defs_osc.json:25688`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/5/17/Kind`]
- `defs_osc.json:oscDetailTilem3` :: `5` (`skin/defs_osc.json:26048`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/5/17/Kind`]

### 7. `defs_osc.json:oscDetailMenuFace` (`skin/defs_osc.json:18879`, `repeats=None`, instances=3)

**B2 violations (3 instances missing `Data` with `Hide`):**

- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `Menu face` (`skin/defs_osc.json:25288`), remap keys `['Caption']`, gates `['RangedEnabling/7/16/17/Kind']`
- `defs_osc.json:oscDetailTilem5m6m9` :: `Menu face` (`skin/defs_osc.json:25756`), remap keys `['Caption']`, gates `['RangedEnabling/7/16/17/Kind']`
- `defs_osc.json:oscDetailTilem3` :: `Menu face` (`skin/defs_osc.json:26116`), remap keys `['Caption']`, gates `['RangedEnabling/7/16/17/Kind']`

**Instances (3):**

- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `Menu face` (`skin/defs_osc.json:25288`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`RangedEnabling/7/16/17/Kind`]
- `defs_osc.json:oscDetailTilem5m6m9` :: `Menu face` (`skin/defs_osc.json:25756`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`RangedEnabling/7/16/17/Kind`]
- `defs_osc.json:oscDetailTilem3` :: `Menu face` (`skin/defs_osc.json:26116`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`RangedEnabling/7/16/17/Kind`]

### 8. `defs_osc.json:oscNativeSetting_dial_uni` (`skin/defs_osc.json:1598`, `repeats=None`, instances=11)

**B2 violations (8 instances missing `Data` with `Hide`):**

- `defs_osc.json:oscDetailTileNoMenu` :: `1` (`skin/defs_osc.json:24872`), remap keys `['Caption']`, gates `['IndexedEnabling/1/17/Kind']`
- `defs_osc.json:oscDetailTileNoMenu` :: `4` (`skin/defs_osc.json:24968`), remap keys `['Caption']`, gates `['IndexedEnabling/4/17/Kind']`
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `1` (`skin/defs_osc.json:25092`), remap keys `['Caption']`, gates `['IndexedEnabling/1/17/Kind']`
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `4` (`skin/defs_osc.json:25188`), remap keys `['Caption']`, gates `['IndexedEnabling/4/17/Kind']`
- `defs_osc.json:oscDetailTilem5m6m9` :: `1` (`skin/defs_osc.json:25560`), remap keys `['Caption']`, gates `['IndexedEnabling/1/17/Kind']`
- `defs_osc.json:oscDetailTilem5m6m9` :: `4` (`skin/defs_osc.json:25656`), remap keys `['Caption']`, gates `['IndexedEnabling/4/17/Kind']`
- `defs_osc.json:oscDetailTilem3` :: `1` (`skin/defs_osc.json:25920`), remap keys `['Caption']`, gates `['IndexedEnabling/1/17/Kind']`
- `defs_osc.json:oscDetailTilem3` :: `4` (`skin/defs_osc.json:26016`), remap keys `['Caption']`, gates `['IndexedEnabling/4/17/Kind']`

**Instances (11):**

- `defs_osc.json:oscNativeDetail` :: `sub_blend` (`skin/defs_osc.json:19207`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3335`, `Caption->WithName[BLEND]/Parameter 3335`]; gates [`IndexedEnabling/2/6/Parameter 3334`, `IndexedEnabling/1/9/Parameter 2978`]
- `defs_osc.json:oscNativeDetail` :: `noise_blend` (`skin/defs_osc.json:19244`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3335`, `Caption->WithName[BLEND]/Parameter 3335`]; gates [`IndexedEnabling/2/6/Parameter 3334`, `IndexedEnabling/2/9/Parameter 2978`]
- `defs_osc.json:oscNativeDetail` :: `noise_sample_blend` (`skin/defs_osc.json:19281`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3335`, `Caption->WithName[BLEND]/Parameter 3335`]; gates [`IndexedEnabling/2/6/Parameter 3334`, `IndexedEnabling/3/9/Parameter 2978`]
- `defs_osc.json:oscDetailTileNoMenu` :: `1` (`skin/defs_osc.json:24872`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/1/17/Kind`]
- `defs_osc.json:oscDetailTileNoMenu` :: `4` (`skin/defs_osc.json:24968`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/4/17/Kind`]
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `1` (`skin/defs_osc.json:25092`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/1/17/Kind`]
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `4` (`skin/defs_osc.json:25188`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/4/17/Kind`]
- `defs_osc.json:oscDetailTilem5m6m9` :: `1` (`skin/defs_osc.json:25560`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/1/17/Kind`]
- `defs_osc.json:oscDetailTilem5m6m9` :: `4` (`skin/defs_osc.json:25656`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/4/17/Kind`]
- `defs_osc.json:oscDetailTilem3` :: `1` (`skin/defs_osc.json:25920`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/1/17/Kind`]
- `defs_osc.json:oscDetailTilem3` :: `4` (`skin/defs_osc.json:26016`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/4/17/Kind`]

### 9. `defs_osc.json:oscNativeSetting_dial_bi` (`skin/defs_osc.json:1725`, `repeats=None`, instances=4)

**B2 violations (4 instances missing `Data` with `Hide`):**

- `defs_osc.json:oscDetailTileNoMenu` :: `2` (`skin/defs_osc.json:24904`), remap keys `['Caption']`, gates `['IndexedEnabling/2/17/Kind']`
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `2` (`skin/defs_osc.json:25124`), remap keys `['Caption']`, gates `['IndexedEnabling/2/17/Kind']`
- `defs_osc.json:oscDetailTilem5m6m9` :: `2` (`skin/defs_osc.json:25592`), remap keys `['Caption']`, gates `['IndexedEnabling/2/17/Kind']`
- `defs_osc.json:oscDetailTilem3` :: `2` (`skin/defs_osc.json:25952`), remap keys `['Caption']`, gates `['IndexedEnabling/2/17/Kind']`

**Instances (4):**

- `defs_osc.json:oscDetailTileNoMenu` :: `2` (`skin/defs_osc.json:24904`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/2/17/Kind`]
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `2` (`skin/defs_osc.json:25124`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/2/17/Kind`]
- `defs_osc.json:oscDetailTilem5m6m9` :: `2` (`skin/defs_osc.json:25592`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/2/17/Kind`]
- `defs_osc.json:oscDetailTilem3` :: `2` (`skin/defs_osc.json:25952`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/2/17/Kind`]

### 10. `defs_osc.json:oscNativeSetting_toggle` (`skin/defs_osc.json:1852`, `repeats=None`, instances=4)

**B2 violations (4 instances missing `Data` with `Hide`):**

- `defs_osc.json:oscDetailTileNoMenu` :: `3` (`skin/defs_osc.json:24936`), remap keys `['Caption']`, gates `['IndexedEnabling/3/17/Kind']`
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `3` (`skin/defs_osc.json:25156`), remap keys `['Caption']`, gates `['IndexedEnabling/3/17/Kind']`
- `defs_osc.json:oscDetailTilem5m6m9` :: `3` (`skin/defs_osc.json:25624`), remap keys `['Caption']`, gates `['IndexedEnabling/3/17/Kind']`
- `defs_osc.json:oscDetailTilem3` :: `3` (`skin/defs_osc.json:25984`), remap keys `['Caption']`, gates `['IndexedEnabling/3/17/Kind']`

**Instances (4):**

- `defs_osc.json:oscDetailTileNoMenu` :: `3` (`skin/defs_osc.json:24936`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/3/17/Kind`]
- `defs_osc.json:oscDetailTilem0m1m2m4m7m8` :: `3` (`skin/defs_osc.json:25156`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/3/17/Kind`]
- `defs_osc.json:oscDetailTilem5m6m9` :: `3` (`skin/defs_osc.json:25624`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/3/17/Kind`]
- `defs_osc.json:oscDetailTilem3` :: `3` (`skin/defs_osc.json:25984`, `showWhenDataModelInvalid=Hide`): remap [`Caption->Data`]; gates [`IndexedEnabling/3/17/Kind`]

### 11. `defs_osc.json:oscNoteMask` (`skin/defs_osc.json:20639`, `repeats=None`, instances=1)

**B2 violations (1 instances missing `Data` with `Hide`):**

- `defs_osc.json:oscScreenTop` :: `Osc Native Note Mask` (`skin/defs_osc.json:21592`), remap keys `['Note0', 'Note1', 'Note2', 'Note3', 'Note4', 'Note5', 'Note6', 'Note7', 'Note8', 'Note9', 'Note10', 'Note11']`, gates `['NonZero/Parameter 3823']`

**Instances (1):**

- `defs_osc.json:oscScreenTop` :: `Osc Native Note Mask` (`skin/defs_osc.json:21592`, `showWhenDataModelInvalid=Hide`): remap [`Note0->Parameter 3811`, `Note1->Parameter 3812`, `Note2->Parameter 3813`, `Note3->Parameter 3814`, `Note4->Parameter 3815`, `Note5->Parameter 3816`, `Note6->Parameter 3817`, `Note7->Parameter 3818`, `Note8->Parameter 3819`, `Note9->Parameter 3820`, `Note10->Parameter 3821`, `Note11->Parameter 3822`]; gates [`NonZero/Parameter 3823`]

### 12. `defs_fx.json:fxNativeHeader` (`skin/defs_fx.json:3336`, `repeats=None`, instances=32) (`*Header*` definition generated by `scripts/header-skin.py`; listed only, not hand-edited)

**B1 violations (8):**

- `rack` (`skin/defs_fx.json:3397`), `gate`: `IndexedEnabling/1/2/Power` (operand `Power`)
- `rack pool` (`skin/defs_fx.json:3430`), `gate`: `IndexedEnabling/1/2/Power` (operand `Power`)
- `remove` (`skin/defs_fx.json:3463`), `gate`: `NonZero/Power` (operand `Power`)
- `Band LOW` (`skin/defs_fx.json:4116`), `remap[Data]`: `WithName[LOW]/FromGesture/Band` (operand `Band`)
- `Band MID` (`skin/defs_fx.json:4153`), `remap[Data]`: `WithName[MID]/FromGesture/Band` (operand `Band`)
- `Band HIGH` (`skin/defs_fx.json:4190`), `remap[Data]`: `WithName[HIGH]/FromGesture/Band` (operand `Band`)
- `Band LOW disabled` (`skin/defs_fx.json:4227`), `remap[Data]`: `WithName[LOW]/FromGesture/Band` (operand `Band`)
- `Band HIGH disabled` (`skin/defs_fx.json:4261`), `remap[Data]`: `WithName[HIGH]/FromGesture/Band` (operand `Band`)

**B2 violations (32 instances missing `Data` with `Hide`):**

- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 1` (`skin/defs_fx.json:20479`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/1/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 2` (`skin/defs_fx.json:20523`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/2/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 3` (`skin/defs_fx.json:20571`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/3/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 4` (`skin/defs_fx.json:20615`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/4/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 5` (`skin/defs_fx.json:20659`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/5/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 6` (`skin/defs_fx.json:20703`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/6/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 7` (`skin/defs_fx.json:20747`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/7/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 8` (`skin/defs_fx.json:20791`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/8/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 9` (`skin/defs_fx.json:20835`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/9/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 10` (`skin/defs_fx.json:20879`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/10/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 11` (`skin/defs_fx.json:20923`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/11/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 12` (`skin/defs_fx.json:20967`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/12/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 13` (`skin/defs_fx.json:21011`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/13/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 14` (`skin/defs_fx.json:21055`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/14/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 15` (`skin/defs_fx.json:21099`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/15/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 16` (`skin/defs_fx.json:21143`), remap keys `['Power', 'Rack', 'Page', 'Remove']`, gates `['IndexedEnabling/16/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 17` (`skin/defs_fx.json:21187`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/17/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 18` (`skin/defs_fx.json:21235`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/18/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 19` (`skin/defs_fx.json:21283`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/19/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 20` (`skin/defs_fx.json:21331`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/20/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 21` (`skin/defs_fx.json:21379`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/21/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 22` (`skin/defs_fx.json:21427`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/22/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 23` (`skin/defs_fx.json:21475`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/23/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 24` (`skin/defs_fx.json:21523`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/24/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 25` (`skin/defs_fx.json:21571`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/25/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 26` (`skin/defs_fx.json:21619`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/26/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 27` (`skin/defs_fx.json:21667`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/27/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 28` (`skin/defs_fx.json:21715`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/28/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 29` (`skin/defs_fx.json:21763`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/29/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 30` (`skin/defs_fx.json:21811`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/30/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 31` (`skin/defs_fx.json:21859`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/31/33/Parameter 3058']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 32` (`skin/defs_fx.json:21907`), remap keys `['Power', 'Rack', 'Page', 'Remove', 'Band']`, gates `['IndexedEnabling/32/33/Parameter 3058']`

**Instances (32):**

- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 1` (`skin/defs_fx.json:20479`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3060`, `Rack->Parameter 3061`, `Page->Parameter 3062`, `Remove->Parameter 3063`]; gates [`IndexedEnabling/1/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 2` (`skin/defs_fx.json:20523`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3064`, `Rack->Parameter 3065`, `Page->Parameter 3066`, `Remove->Parameter 3067`, `Band->Parameter 3445`]; gates [`IndexedEnabling/2/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 3` (`skin/defs_fx.json:20571`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3068`, `Rack->Parameter 3069`, `Page->Parameter 3070`, `Remove->Parameter 3071`]; gates [`IndexedEnabling/3/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 4` (`skin/defs_fx.json:20615`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3072`, `Rack->Parameter 3073`, `Page->Parameter 3074`, `Remove->Parameter 3075`]; gates [`IndexedEnabling/4/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 5` (`skin/defs_fx.json:20659`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3076`, `Rack->Parameter 3077`, `Page->Parameter 3078`, `Remove->Parameter 3079`]; gates [`IndexedEnabling/5/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 6` (`skin/defs_fx.json:20703`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3080`, `Rack->Parameter 3081`, `Page->Parameter 3082`, `Remove->Parameter 3083`]; gates [`IndexedEnabling/6/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 7` (`skin/defs_fx.json:20747`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3084`, `Rack->Parameter 3085`, `Page->Parameter 3086`, `Remove->Parameter 3087`]; gates [`IndexedEnabling/7/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 8` (`skin/defs_fx.json:20791`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3088`, `Rack->Parameter 3089`, `Page->Parameter 3090`, `Remove->Parameter 3091`]; gates [`IndexedEnabling/8/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 9` (`skin/defs_fx.json:20835`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3092`, `Rack->Parameter 3093`, `Page->Parameter 3094`, `Remove->Parameter 3095`]; gates [`IndexedEnabling/9/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 10` (`skin/defs_fx.json:20879`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3096`, `Rack->Parameter 3097`, `Page->Parameter 3098`, `Remove->Parameter 3099`]; gates [`IndexedEnabling/10/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 11` (`skin/defs_fx.json:20923`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3100`, `Rack->Parameter 3101`, `Page->Parameter 3102`, `Remove->Parameter 3103`]; gates [`IndexedEnabling/11/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 12` (`skin/defs_fx.json:20967`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3104`, `Rack->Parameter 3105`, `Page->Parameter 3106`, `Remove->Parameter 3107`]; gates [`IndexedEnabling/12/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 13` (`skin/defs_fx.json:21011`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3108`, `Rack->Parameter 3109`, `Page->Parameter 3110`, `Remove->Parameter 3111`]; gates [`IndexedEnabling/13/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 14` (`skin/defs_fx.json:21055`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3112`, `Rack->Parameter 3113`, `Page->Parameter 3114`, `Remove->Parameter 3115`]; gates [`IndexedEnabling/14/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 15` (`skin/defs_fx.json:21099`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3116`, `Rack->Parameter 3117`, `Page->Parameter 3118`, `Remove->Parameter 3119`]; gates [`IndexedEnabling/15/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 16` (`skin/defs_fx.json:21143`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3120`, `Rack->Parameter 3121`, `Page->Parameter 3122`, `Remove->Parameter 3123`]; gates [`IndexedEnabling/16/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 17` (`skin/defs_fx.json:21187`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3124`, `Rack->Parameter 3125`, `Page->Parameter 3126`, `Remove->Parameter 3127`, `Band->Parameter 3460`]; gates [`IndexedEnabling/17/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 18` (`skin/defs_fx.json:21235`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3128`, `Rack->Parameter 3129`, `Page->Parameter 3130`, `Remove->Parameter 3131`, `Band->Parameter 3461`]; gates [`IndexedEnabling/18/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 19` (`skin/defs_fx.json:21283`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3132`, `Rack->Parameter 3133`, `Page->Parameter 3134`, `Remove->Parameter 3135`, `Band->Parameter 3462`]; gates [`IndexedEnabling/19/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 20` (`skin/defs_fx.json:21331`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3136`, `Rack->Parameter 3137`, `Page->Parameter 3138`, `Remove->Parameter 3139`, `Band->Parameter 3463`]; gates [`IndexedEnabling/20/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 21` (`skin/defs_fx.json:21379`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3140`, `Rack->Parameter 3141`, `Page->Parameter 3142`, `Remove->Parameter 3143`, `Band->Parameter 3464`]; gates [`IndexedEnabling/21/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 22` (`skin/defs_fx.json:21427`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3144`, `Rack->Parameter 3145`, `Page->Parameter 3146`, `Remove->Parameter 3147`, `Band->Parameter 3465`]; gates [`IndexedEnabling/22/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 23` (`skin/defs_fx.json:21475`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3148`, `Rack->Parameter 3149`, `Page->Parameter 3150`, `Remove->Parameter 3151`, `Band->Parameter 3466`]; gates [`IndexedEnabling/23/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 24` (`skin/defs_fx.json:21523`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3152`, `Rack->Parameter 3153`, `Page->Parameter 3154`, `Remove->Parameter 3155`, `Band->Parameter 3467`]; gates [`IndexedEnabling/24/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 25` (`skin/defs_fx.json:21571`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3156`, `Rack->Parameter 3157`, `Page->Parameter 3158`, `Remove->Parameter 3159`, `Band->Parameter 3468`]; gates [`IndexedEnabling/25/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 26` (`skin/defs_fx.json:21619`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3160`, `Rack->Parameter 3161`, `Page->Parameter 3162`, `Remove->Parameter 3163`, `Band->Parameter 3469`]; gates [`IndexedEnabling/26/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 27` (`skin/defs_fx.json:21667`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3164`, `Rack->Parameter 3165`, `Page->Parameter 3166`, `Remove->Parameter 3167`, `Band->Parameter 3470`]; gates [`IndexedEnabling/27/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 28` (`skin/defs_fx.json:21715`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3168`, `Rack->Parameter 3169`, `Page->Parameter 3170`, `Remove->Parameter 3171`, `Band->Parameter 3471`]; gates [`IndexedEnabling/28/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 29` (`skin/defs_fx.json:21763`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3172`, `Rack->Parameter 3173`, `Page->Parameter 3174`, `Remove->Parameter 3175`, `Band->Parameter 3472`]; gates [`IndexedEnabling/29/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 30` (`skin/defs_fx.json:21811`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3176`, `Rack->Parameter 3177`, `Page->Parameter 3178`, `Remove->Parameter 3179`, `Band->Parameter 3473`]; gates [`IndexedEnabling/30/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 31` (`skin/defs_fx.json:21859`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3180`, `Rack->Parameter 3181`, `Page->Parameter 3182`, `Remove->Parameter 3183`, `Band->Parameter 3474`]; gates [`IndexedEnabling/31/33/Parameter 3058`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Native Header 32` (`skin/defs_fx.json:21907`, `showWhenDataModelInvalid=Hide`): remap [`Power->Parameter 3184`, `Rack->Parameter 3185`, `Page->Parameter 3186`, `Remove->Parameter 3187`, `Band->Parameter 3475`]; gates [`IndexedEnabling/32/33/Parameter 3058`]

### 13. `defs_fx.json:fxNativeBandCard` (`skin/defs_fx.json:7523`, `repeats=None`, instances=13)

**B2 violations (13 instances missing `Data` with `Hide`):**

- `defs_fx.json:fxNativeBandLane` :: `Card 1` (`skin/defs_fx.json:6368`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present0']`
- `defs_fx.json:fxNativeBandLane` :: `Card 2` (`skin/defs_fx.json:6454`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present1']`
- `defs_fx.json:fxNativeBandLane` :: `Card 3` (`skin/defs_fx.json:6540`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present2']`
- `defs_fx.json:fxNativeBandLane` :: `Card 4` (`skin/defs_fx.json:6626`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present3']`
- `defs_fx.json:fxNativeBandLane` :: `Card 5` (`skin/defs_fx.json:6712`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present4']`
- `defs_fx.json:fxNativeBandLane` :: `Card 6` (`skin/defs_fx.json:6798`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present5']`
- `defs_fx.json:fxNativeBandLane` :: `Card 7` (`skin/defs_fx.json:6884`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present6']`
- `defs_fx.json:fxNativeBandLane` :: `Card 8` (`skin/defs_fx.json:6970`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present7']`
- `defs_fx.json:fxNativeBandLane` :: `Card 9` (`skin/defs_fx.json:7056`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present8']`
- `defs_fx.json:fxNativeBandLane` :: `Card 10` (`skin/defs_fx.json:7142`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present9']`
- `defs_fx.json:fxNativeBandLane` :: `Card 11` (`skin/defs_fx.json:7228`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present10']`
- `defs_fx.json:fxNativeBandLane` :: `Card 12` (`skin/defs_fx.json:7314`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present11']`
- `defs_fx.json:fxNativeBandLane` :: `Card 13` (`skin/defs_fx.json:7400`), remap keys `['Present', 'Select', 'Selected', 'Unselected', 'Power', 'Powered', 'Bypassed']`, gates `['Present12']`

**Instances (13):**

- `defs_fx.json:fxNativeBandLane` :: `Card 1` (`skin/defs_fx.json:6368`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present0`, `Select->Select0`, `Selected->Selected0`, `Unselected->Unselected0`, `Power->Power0`, `Powered->Powered0`, `Bypassed->Bypassed0`]; gates [`Present0`]
- `defs_fx.json:fxNativeBandLane` :: `Card 2` (`skin/defs_fx.json:6454`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present1`, `Select->Select1`, `Selected->Selected1`, `Unselected->Unselected1`, `Power->Power1`, `Powered->Powered1`, `Bypassed->Bypassed1`]; gates [`Present1`]
- `defs_fx.json:fxNativeBandLane` :: `Card 3` (`skin/defs_fx.json:6540`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present2`, `Select->Select2`, `Selected->Selected2`, `Unselected->Unselected2`, `Power->Power2`, `Powered->Powered2`, `Bypassed->Bypassed2`]; gates [`Present2`]
- `defs_fx.json:fxNativeBandLane` :: `Card 4` (`skin/defs_fx.json:6626`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present3`, `Select->Select3`, `Selected->Selected3`, `Unselected->Unselected3`, `Power->Power3`, `Powered->Powered3`, `Bypassed->Bypassed3`]; gates [`Present3`]
- `defs_fx.json:fxNativeBandLane` :: `Card 5` (`skin/defs_fx.json:6712`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present4`, `Select->Select4`, `Selected->Selected4`, `Unselected->Unselected4`, `Power->Power4`, `Powered->Powered4`, `Bypassed->Bypassed4`]; gates [`Present4`]
- `defs_fx.json:fxNativeBandLane` :: `Card 6` (`skin/defs_fx.json:6798`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present5`, `Select->Select5`, `Selected->Selected5`, `Unselected->Unselected5`, `Power->Power5`, `Powered->Powered5`, `Bypassed->Bypassed5`]; gates [`Present5`]
- `defs_fx.json:fxNativeBandLane` :: `Card 7` (`skin/defs_fx.json:6884`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present6`, `Select->Select6`, `Selected->Selected6`, `Unselected->Unselected6`, `Power->Power6`, `Powered->Powered6`, `Bypassed->Bypassed6`]; gates [`Present6`]
- `defs_fx.json:fxNativeBandLane` :: `Card 8` (`skin/defs_fx.json:6970`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present7`, `Select->Select7`, `Selected->Selected7`, `Unselected->Unselected7`, `Power->Power7`, `Powered->Powered7`, `Bypassed->Bypassed7`]; gates [`Present7`]
- `defs_fx.json:fxNativeBandLane` :: `Card 9` (`skin/defs_fx.json:7056`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present8`, `Select->Select8`, `Selected->Selected8`, `Unselected->Unselected8`, `Power->Power8`, `Powered->Powered8`, `Bypassed->Bypassed8`]; gates [`Present8`]
- `defs_fx.json:fxNativeBandLane` :: `Card 10` (`skin/defs_fx.json:7142`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present9`, `Select->Select9`, `Selected->Selected9`, `Unselected->Unselected9`, `Power->Power9`, `Powered->Powered9`, `Bypassed->Bypassed9`]; gates [`Present9`]
- `defs_fx.json:fxNativeBandLane` :: `Card 11` (`skin/defs_fx.json:7228`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present10`, `Select->Select10`, `Selected->Selected10`, `Unselected->Unselected10`, `Power->Power10`, `Powered->Powered10`, `Bypassed->Bypassed10`]; gates [`Present10`]
- `defs_fx.json:fxNativeBandLane` :: `Card 12` (`skin/defs_fx.json:7314`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present11`, `Select->Select11`, `Selected->Selected11`, `Unselected->Unselected11`, `Power->Power11`, `Powered->Powered11`, `Bypassed->Bypassed11`]; gates [`Present11`]
- `defs_fx.json:fxNativeBandLane` :: `Card 13` (`skin/defs_fx.json:7400`, `showWhenDataModelInvalid=Hide`): remap [`Present->Present12`, `Select->Select12`, `Selected->Selected12`, `Unselected->Unselected12`, `Power->Power12`, `Powered->Powered12`, `Bypassed->Bypassed12`]; gates [`Present12`]

### 14. `defs_mod.json:modNativeRowNormal` (`skin/defs_mod.json:567`, `repeats=None`, instances=7)

**B1 violations (6):**

- `AMOUNT` (`skin/defs_mod.json:591`), `remap[Caption]`: `WithName[AMOUNT]/AMOUNT` (operand `AMOUNT`)
- `POLARITY` (`skin/defs_mod.json:624`), `remap[Caption]`: `WithName[POLARITY]/POLARITY` (operand `POLARITY`)
- `ROUTE` (`skin/defs_mod.json:657`), `remap[Caption]`: `WithName[ROUTE]/ROUTE` (operand `ROUTE`)
- `SOURCE` (`skin/defs_mod.json:690`), `remap[Caption]`: `WithName[SOURCE]/SOURCE` (operand `SOURCE`)
- `TARGET` (`skin/defs_mod.json:723`), `remap[Caption]`: `WithName[TARGET]/TARGET` (operand `TARGET`)
- `MORE` (`skin/defs_mod.json:756`), `remap[Caption]`: `WithName[EDIT]/MORE` (operand `MORE`)

**Instances (7):**

- `TUI.json:tab:Mod` :: `Mod Native Normal Row 1` (`skin/TUI.json:27381`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2881`, `Caption->WithName[01]/Parameter 2881`, `AMOUNT->Parameter 2888`, `Amount->Parameter 2888`, `POLARITY->Parameter 2889`, `Polarity->Parameter 2889`, `ROUTE->Parameter 2890`, `Route->Parameter 2890`, `Bypass->Parameter 2890`, `SOURCE->Parameter 2940`, `Source->Parameter 2940`, `TARGET->Parameter 2941`, `Target->Parameter 2941`, `EDIT->Parameter 2942`, `Edit->Parameter 2942`, `MORE->Parameter 2942`, `More->Parameter 2942`, `OpenMore->Parameter 2942`]; gates [`IndexedEnabling/1/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2881`]
- `TUI.json:tab:Mod` :: `Mod Native Normal Row 2` (`skin/TUI.json:27482`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2882`, `Caption->WithName[02]/Parameter 2882`, `AMOUNT->Parameter 2891`, `Amount->Parameter 2891`, `POLARITY->Parameter 2892`, `Polarity->Parameter 2892`, `ROUTE->Parameter 2893`, `Route->Parameter 2893`, `Bypass->Parameter 2893`, `SOURCE->Parameter 2943`, `Source->Parameter 2943`, `TARGET->Parameter 2944`, `Target->Parameter 2944`, `EDIT->Parameter 2945`, `Edit->Parameter 2945`, `MORE->Parameter 2945`, `More->Parameter 2945`, `OpenMore->Parameter 2945`]; gates [`IndexedEnabling/1/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2882`]
- `TUI.json:tab:Mod` :: `Mod Native Normal Row 3` (`skin/TUI.json:27583`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2883`, `Caption->WithName[03]/Parameter 2883`, `AMOUNT->Parameter 2894`, `Amount->Parameter 2894`, `POLARITY->Parameter 2895`, `Polarity->Parameter 2895`, `ROUTE->Parameter 2896`, `Route->Parameter 2896`, `Bypass->Parameter 2896`, `SOURCE->Parameter 2946`, `Source->Parameter 2946`, `TARGET->Parameter 2947`, `Target->Parameter 2947`, `EDIT->Parameter 2948`, `Edit->Parameter 2948`, `MORE->Parameter 2948`, `More->Parameter 2948`, `OpenMore->Parameter 2948`]; gates [`IndexedEnabling/1/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2883`]
- `TUI.json:tab:Mod` :: `Mod Native Normal Row 4` (`skin/TUI.json:27684`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2884`, `Caption->WithName[04]/Parameter 2884`, `AMOUNT->Parameter 2897`, `Amount->Parameter 2897`, `POLARITY->Parameter 2898`, `Polarity->Parameter 2898`, `ROUTE->Parameter 2899`, `Route->Parameter 2899`, `Bypass->Parameter 2899`, `SOURCE->Parameter 2949`, `Source->Parameter 2949`, `TARGET->Parameter 2950`, `Target->Parameter 2950`, `EDIT->Parameter 2951`, `Edit->Parameter 2951`, `MORE->Parameter 2951`, `More->Parameter 2951`, `OpenMore->Parameter 2951`]; gates [`IndexedEnabling/1/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2884`]
- `TUI.json:tab:Mod` :: `Mod Native Normal Row 5` (`skin/TUI.json:27785`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2885`, `Caption->WithName[05]/Parameter 2885`, `AMOUNT->Parameter 2900`, `Amount->Parameter 2900`, `POLARITY->Parameter 2901`, `Polarity->Parameter 2901`, `ROUTE->Parameter 2902`, `Route->Parameter 2902`, `Bypass->Parameter 2902`, `SOURCE->Parameter 2952`, `Source->Parameter 2952`, `TARGET->Parameter 2953`, `Target->Parameter 2953`, `EDIT->Parameter 2954`, `Edit->Parameter 2954`, `MORE->Parameter 2954`, `More->Parameter 2954`, `OpenMore->Parameter 2954`]; gates [`IndexedEnabling/1/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2885`]
- `TUI.json:tab:Mod` :: `Mod Native Normal Row 6` (`skin/TUI.json:27886`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2886`, `Caption->WithName[06]/Parameter 2886`, `AMOUNT->Parameter 2903`, `Amount->Parameter 2903`, `POLARITY->Parameter 2904`, `Polarity->Parameter 2904`, `ROUTE->Parameter 2905`, `Route->Parameter 2905`, `Bypass->Parameter 2905`, `SOURCE->Parameter 2955`, `Source->Parameter 2955`, `TARGET->Parameter 2956`, `Target->Parameter 2956`, `EDIT->Parameter 2957`, `Edit->Parameter 2957`, `MORE->Parameter 2957`, `More->Parameter 2957`, `OpenMore->Parameter 2957`]; gates [`IndexedEnabling/1/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2886`]
- `TUI.json:tab:Mod` :: `Mod Native Normal Row 7` (`skin/TUI.json:27987`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2887`, `Caption->WithName[07]/Parameter 2887`, `AMOUNT->Parameter 2906`, `Amount->Parameter 2906`, `POLARITY->Parameter 2907`, `Polarity->Parameter 2907`, `ROUTE->Parameter 2908`, `Route->Parameter 2908`, `Bypass->Parameter 2908`, `SOURCE->Parameter 2958`, `Source->Parameter 2958`, `TARGET->Parameter 2959`, `Target->Parameter 2959`, `EDIT->Parameter 2960`, `Edit->Parameter 2960`, `MORE->Parameter 2960`, `More->Parameter 2960`, `OpenMore->Parameter 2960`]; gates [`IndexedEnabling/1/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2887`]

### 15. `defs_mod.json:modNativeRowScroll` (`skin/defs_mod.json:789`, `repeats=None`, instances=7)

**B1 violations (6):**

- `AMOUNT` (`skin/defs_mod.json:813`), `remap[Caption]`: `WithName[AMOUNT]/AMOUNT` (operand `AMOUNT`)
- `POLARITY` (`skin/defs_mod.json:846`), `remap[Caption]`: `WithName[POLARITY]/POLARITY` (operand `POLARITY`)
- `ROUTE` (`skin/defs_mod.json:879`), `remap[Caption]`: `WithName[ROUTE]/ROUTE` (operand `ROUTE`)
- `SOURCE` (`skin/defs_mod.json:912`), `remap[Caption]`: `WithName[SOURCE]/SOURCE` (operand `SOURCE`)
- `TARGET` (`skin/defs_mod.json:945`), `remap[Caption]`: `WithName[TARGET]/TARGET` (operand `TARGET`)
- `MORE` (`skin/defs_mod.json:978`), `remap[Caption]`: `WithName[EDIT]/MORE` (operand `MORE`)

**Instances (7):**

- `TUI.json:tab:Mod` :: `Mod Native Scroll Row 1` (`skin/TUI.json:28088`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2881`, `Caption->WithName[01]/Parameter 2881`, `AMOUNT->Parameter 2888`, `Amount->Parameter 2888`, `POLARITY->Parameter 2889`, `Polarity->Parameter 2889`, `ROUTE->Parameter 2890`, `Route->Parameter 2890`, `Bypass->Parameter 2890`, `SOURCE->Parameter 2940`, `Source->Parameter 2940`, `TARGET->Parameter 2941`, `Target->Parameter 2941`, `EDIT->Parameter 2942`, `Edit->Parameter 2942`, `MORE->Parameter 2942`, `More->Parameter 2942`, `OpenMore->Parameter 2942`]; gates [`IndexedEnabling/2/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2881`]
- `TUI.json:tab:Mod` :: `Mod Native Scroll Row 2` (`skin/TUI.json:28189`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2882`, `Caption->WithName[02]/Parameter 2882`, `AMOUNT->Parameter 2891`, `Amount->Parameter 2891`, `POLARITY->Parameter 2892`, `Polarity->Parameter 2892`, `ROUTE->Parameter 2893`, `Route->Parameter 2893`, `Bypass->Parameter 2893`, `SOURCE->Parameter 2943`, `Source->Parameter 2943`, `TARGET->Parameter 2944`, `Target->Parameter 2944`, `EDIT->Parameter 2945`, `Edit->Parameter 2945`, `MORE->Parameter 2945`, `More->Parameter 2945`, `OpenMore->Parameter 2945`]; gates [`IndexedEnabling/2/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2882`]
- `TUI.json:tab:Mod` :: `Mod Native Scroll Row 3` (`skin/TUI.json:28290`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2883`, `Caption->WithName[03]/Parameter 2883`, `AMOUNT->Parameter 2894`, `Amount->Parameter 2894`, `POLARITY->Parameter 2895`, `Polarity->Parameter 2895`, `ROUTE->Parameter 2896`, `Route->Parameter 2896`, `Bypass->Parameter 2896`, `SOURCE->Parameter 2946`, `Source->Parameter 2946`, `TARGET->Parameter 2947`, `Target->Parameter 2947`, `EDIT->Parameter 2948`, `Edit->Parameter 2948`, `MORE->Parameter 2948`, `More->Parameter 2948`, `OpenMore->Parameter 2948`]; gates [`IndexedEnabling/2/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2883`]
- `TUI.json:tab:Mod` :: `Mod Native Scroll Row 4` (`skin/TUI.json:28391`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2884`, `Caption->WithName[04]/Parameter 2884`, `AMOUNT->Parameter 2897`, `Amount->Parameter 2897`, `POLARITY->Parameter 2898`, `Polarity->Parameter 2898`, `ROUTE->Parameter 2899`, `Route->Parameter 2899`, `Bypass->Parameter 2899`, `SOURCE->Parameter 2949`, `Source->Parameter 2949`, `TARGET->Parameter 2950`, `Target->Parameter 2950`, `EDIT->Parameter 2951`, `Edit->Parameter 2951`, `MORE->Parameter 2951`, `More->Parameter 2951`, `OpenMore->Parameter 2951`]; gates [`IndexedEnabling/2/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2884`]
- `TUI.json:tab:Mod` :: `Mod Native Scroll Row 5` (`skin/TUI.json:28492`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2885`, `Caption->WithName[05]/Parameter 2885`, `AMOUNT->Parameter 2900`, `Amount->Parameter 2900`, `POLARITY->Parameter 2901`, `Polarity->Parameter 2901`, `ROUTE->Parameter 2902`, `Route->Parameter 2902`, `Bypass->Parameter 2902`, `SOURCE->Parameter 2952`, `Source->Parameter 2952`, `TARGET->Parameter 2953`, `Target->Parameter 2953`, `EDIT->Parameter 2954`, `Edit->Parameter 2954`, `MORE->Parameter 2954`, `More->Parameter 2954`, `OpenMore->Parameter 2954`]; gates [`IndexedEnabling/2/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2885`]
- `TUI.json:tab:Mod` :: `Mod Native Scroll Row 6` (`skin/TUI.json:28593`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2886`, `Caption->WithName[06]/Parameter 2886`, `AMOUNT->Parameter 2903`, `Amount->Parameter 2903`, `POLARITY->Parameter 2904`, `Polarity->Parameter 2904`, `ROUTE->Parameter 2905`, `Route->Parameter 2905`, `Bypass->Parameter 2905`, `SOURCE->Parameter 2955`, `Source->Parameter 2955`, `TARGET->Parameter 2956`, `Target->Parameter 2956`, `EDIT->Parameter 2957`, `Edit->Parameter 2957`, `MORE->Parameter 2957`, `More->Parameter 2957`, `OpenMore->Parameter 2957`]; gates [`IndexedEnabling/2/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2886`]
- `TUI.json:tab:Mod` :: `Mod Native Scroll Row 7` (`skin/TUI.json:28694`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 2887`, `Caption->WithName[07]/Parameter 2887`, `AMOUNT->Parameter 2906`, `Amount->Parameter 2906`, `POLARITY->Parameter 2907`, `Polarity->Parameter 2907`, `ROUTE->Parameter 2908`, `Route->Parameter 2908`, `Bypass->Parameter 2908`, `SOURCE->Parameter 2958`, `Source->Parameter 2958`, `TARGET->Parameter 2959`, `Target->Parameter 2959`, `EDIT->Parameter 2960`, `Edit->Parameter 2960`, `MORE->Parameter 2960`, `More->Parameter 2960`, `OpenMore->Parameter 2960`]; gates [`IndexedEnabling/2/5/Parameter 2880`, `RangedEnabling/1/64/65/Parameter 2887`]

### 16. `defs_common.json:nativeModulationRoute` (`skin/defs_common.json:1693`, `repeats=None`, instances=6)

**B1 violations (4):**

- `Target` (`skin/defs_common.json:1717`), `gate`: `IndexedEnabling/2/3/Present` (operand `Present`)
- `Polarity` (`skin/defs_common.json:1786`), `remap[Data]`: `FromGesture/Polarity` (operand `Polarity`)
- `Active` (`skin/defs_common.json:1820`), `remap[Data]`: `FromGesture/Active` (operand `Active`)
- `Remove` (`skin/defs_common.json:1854`), `remap[Data]`: `FromGesture/Remove` (operand `Remove`)

**Instances (6):**

- `defs_common.json:nativeModulationEditor` :: `Route 1` (`skin/defs_common.json:2124`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3514`, `Present->Parameter 3514`, `Amount->Parameter 3488`, `Polarity->Parameter 3489`, `Active->Parameter 3490`, `Remove->Parameter 3491`]; gates [`NonZero/Parameter 3514`]
- `defs_common.json:nativeModulationEditor` :: `Route 2` (`skin/defs_common.json:2176`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3515`, `Present->Parameter 3515`, `Amount->Parameter 3492`, `Polarity->Parameter 3493`, `Active->Parameter 3494`, `Remove->Parameter 3495`]; gates [`NonZero/Parameter 3515`]
- `defs_common.json:nativeModulationEditor` :: `Route 3` (`skin/defs_common.json:2228`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3516`, `Present->Parameter 3516`, `Amount->Parameter 3496`, `Polarity->Parameter 3497`, `Active->Parameter 3498`, `Remove->Parameter 3499`]; gates [`NonZero/Parameter 3516`]
- `defs_common.json:nativeModulationEditor` :: `Route 4` (`skin/defs_common.json:2280`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3517`, `Present->Parameter 3517`, `Amount->Parameter 3500`, `Polarity->Parameter 3501`, `Active->Parameter 3502`, `Remove->Parameter 3503`]; gates [`NonZero/Parameter 3517`]
- `defs_common.json:nativeModulationEditor` :: `Route 5` (`skin/defs_common.json:2332`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3518`, `Present->Parameter 3518`, `Amount->Parameter 3504`, `Polarity->Parameter 3505`, `Active->Parameter 3506`, `Remove->Parameter 3507`]; gates [`NonZero/Parameter 3518`]
- `defs_common.json:nativeModulationEditor` :: `Route 6` (`skin/defs_common.json:2384`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3519`, `Present->Parameter 3519`, `Amount->Parameter 3508`, `Polarity->Parameter 3509`, `Active->Parameter 3510`, `Remove->Parameter 3511`]; gates [`NonZero/Parameter 3519`]

### 17. `defs_fx.json:fxNativeBandLane` (`skin/defs_fx.json:5952`, `repeats=None`, instances=3)

**B2 violations (3 instances missing `Data` with `Hide`):**

- `defs_fx.json:fxNativeEditorSurface` :: `FX Band Lane 1` (`skin/defs_fx.json:22251`), remap keys `['State', 'Open', 'Add', 'AddAvail', 'Shown', 'Face1', 'Face2', 'Face3', 'Face4', 'Face5', 'Face6', 'Face7', 'Face8', 'Present0', 'Select0', 'Selected0', 'Unselected0', 'Power0', 'Powered0', 'Bypassed0', 'Present1', 'Select1', 'Selected1', 'Unselected1', 'Power1', 'Powered1', 'Bypassed1', 'Present2', 'Select2', 'Selected2', 'Unselected2', 'Power2', 'Powered2', 'Bypassed2', 'Present3', 'Select3', 'Selected3', 'Unselected3', 'Power3', 'Powered3', 'Bypassed3', 'Present4', 'Select4', 'Selected4', 'Unselected4', 'Power4', 'Powered4', 'Bypassed4', 'Present5', 'Select5', 'Selected5', 'Unselected5', 'Power5', 'Powered5', 'Bypassed5', 'Present6', 'Select6', 'Selected6', 'Unselected6', 'Power6', 'Powered6', 'Bypassed6', 'Present7', 'Select7', 'Selected7', 'Unselected7', 'Power7', 'Powered7', 'Bypassed7', 'Present8', 'Select8', 'Selected8', 'Unselected8', 'Power8', 'Powered8', 'Bypassed8', 'Present9', 'Select9', 'Selected9', 'Unselected9', 'Power9', 'Powered9', 'Bypassed9', 'Present10', 'Select10', 'Selected10', 'Unselected10', 'Power10', 'Powered10', 'Bypassed10', 'Present11', 'Select11', 'Selected11', 'Unselected11', 'Power11', 'Powered11', 'Bypassed11', 'Present12', 'Select12', 'Selected12', 'Unselected12', 'Power12', 'Powered12', 'Bypassed12', 'Range', 'Info']`, gates `['RangedEnabling/1/8/9/Parameter 3663']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Band Lane 2` (`skin/defs_fx.json:22703`), remap keys `['State', 'Open', 'Add', 'AddAvail', 'Shown', 'Face1', 'Face2', 'Face3', 'Face4', 'Face5', 'Face6', 'Face7', 'Face8', 'Present0', 'Select0', 'Selected0', 'Unselected0', 'Power0', 'Powered0', 'Bypassed0', 'Present1', 'Select1', 'Selected1', 'Unselected1', 'Power1', 'Powered1', 'Bypassed1', 'Present2', 'Select2', 'Selected2', 'Unselected2', 'Power2', 'Powered2', 'Bypassed2', 'Present3', 'Select3', 'Selected3', 'Unselected3', 'Power3', 'Powered3', 'Bypassed3', 'Present4', 'Select4', 'Selected4', 'Unselected4', 'Power4', 'Powered4', 'Bypassed4', 'Present5', 'Select5', 'Selected5', 'Unselected5', 'Power5', 'Powered5', 'Bypassed5', 'Present6', 'Select6', 'Selected6', 'Unselected6', 'Power6', 'Powered6', 'Bypassed6', 'Present7', 'Select7', 'Selected7', 'Unselected7', 'Power7', 'Powered7', 'Bypassed7', 'Present8', 'Select8', 'Selected8', 'Unselected8', 'Power8', 'Powered8', 'Bypassed8', 'Present9', 'Select9', 'Selected9', 'Unselected9', 'Power9', 'Powered9', 'Bypassed9', 'Present10', 'Select10', 'Selected10', 'Unselected10', 'Power10', 'Powered10', 'Bypassed10', 'Present11', 'Select11', 'Selected11', 'Unselected11', 'Power11', 'Powered11', 'Bypassed11', 'Present12', 'Select12', 'Selected12', 'Unselected12', 'Power12', 'Powered12', 'Bypassed12', 'Range', 'Info']`, gates `['RangedEnabling/1/8/9/Parameter 3696']`
- `defs_fx.json:fxNativeEditorSurface` :: `FX Band Lane 3` (`skin/defs_fx.json:23155`), remap keys `['State', 'Open', 'Add', 'AddAvail', 'Shown', 'Face1', 'Face2', 'Face3', 'Face4', 'Face5', 'Face6', 'Face7', 'Face8', 'Present0', 'Select0', 'Selected0', 'Unselected0', 'Power0', 'Powered0', 'Bypassed0', 'Present1', 'Select1', 'Selected1', 'Unselected1', 'Power1', 'Powered1', 'Bypassed1', 'Present2', 'Select2', 'Selected2', 'Unselected2', 'Power2', 'Powered2', 'Bypassed2', 'Present3', 'Select3', 'Selected3', 'Unselected3', 'Power3', 'Powered3', 'Bypassed3', 'Present4', 'Select4', 'Selected4', 'Unselected4', 'Power4', 'Powered4', 'Bypassed4', 'Present5', 'Select5', 'Selected5', 'Unselected5', 'Power5', 'Powered5', 'Bypassed5', 'Present6', 'Select6', 'Selected6', 'Unselected6', 'Power6', 'Powered6', 'Bypassed6', 'Present7', 'Select7', 'Selected7', 'Unselected7', 'Power7', 'Powered7', 'Bypassed7', 'Present8', 'Select8', 'Selected8', 'Unselected8', 'Power8', 'Powered8', 'Bypassed8', 'Present9', 'Select9', 'Selected9', 'Unselected9', 'Power9', 'Powered9', 'Bypassed9', 'Present10', 'Select10', 'Selected10', 'Unselected10', 'Power10', 'Powered10', 'Bypassed10', 'Present11', 'Select11', 'Selected11', 'Unselected11', 'Power11', 'Powered11', 'Bypassed11', 'Present12', 'Select12', 'Selected12', 'Unselected12', 'Power12', 'Powered12', 'Bypassed12', 'Range', 'Info']`, gates `['RangedEnabling/1/8/9/Parameter 3729']`

**Instances (3):**

- `defs_fx.json:fxNativeEditorSurface` :: `FX Band Lane 1` (`skin/defs_fx.json:22251`, `showWhenDataModelInvalid=Hide`): remap [`State->Parameter 3663`, `Open->Parameter 3667`, `Add->Parameter 3668`, `AddAvail->IndexedEnabling/1/2/Parameter 3669`, `Shown->RangedEnabling/1/8/9/Parameter 3663`, `Face1->IndexedEnabling/1/9/Parameter 3663`, `Face2->IndexedEnabling/2/9/Parameter 3663`, `Face3->IndexedEnabling/3/9/Parameter 3663`, `Face4->IndexedEnabling/4/9/Parameter 3663`, `Face5->IndexedEnabling/5/9/Parameter 3663`, `Face6->IndexedEnabling/6/9/Parameter 3663`, `Face7->IndexedEnabling/7/9/Parameter 3663`, `Face8->IndexedEnabling/8/9/Parameter 3663`, `Present0->RangedEnabling/1/13/14/Parameter 3666`, `Select0->Parameter 3670`, `Selected0->IndexedEnabling/1/2/Parameter 3670`, `Unselected0->IndexedEnabling/0/2/Parameter 3670`, `Power0->Parameter 3683`, `Powered0->IndexedEnabling/1/2/Parameter 3683`, `Bypassed0->IndexedEnabling/0/2/Parameter 3683`, `Present1->RangedEnabling/2/13/14/Parameter 3666`, `Select1->Parameter 3671`, `Selected1->IndexedEnabling/1/2/Parameter 3671`, `Unselected1->IndexedEnabling/0/2/Parameter 3671`, `Power1->Parameter 3684`, `Powered1->IndexedEnabling/1/2/Parameter 3684`, `Bypassed1->IndexedEnabling/0/2/Parameter 3684`, `Present2->RangedEnabling/3/13/14/Parameter 3666`, `Select2->Parameter 3672`, `Selected2->IndexedEnabling/1/2/Parameter 3672`, `Unselected2->IndexedEnabling/0/2/Parameter 3672`, `Power2->Parameter 3685`, `Powered2->IndexedEnabling/1/2/Parameter 3685`, `Bypassed2->IndexedEnabling/0/2/Parameter 3685`, `Present3->RangedEnabling/4/13/14/Parameter 3666`, `Select3->Parameter 3673`, `Selected3->IndexedEnabling/1/2/Parameter 3673`, `Unselected3->IndexedEnabling/0/2/Parameter 3673`, `Power3->Parameter 3686`, `Powered3->IndexedEnabling/1/2/Parameter 3686`, `Bypassed3->IndexedEnabling/0/2/Parameter 3686`, `Present4->RangedEnabling/5/13/14/Parameter 3666`, `Select4->Parameter 3674`, `Selected4->IndexedEnabling/1/2/Parameter 3674`, `Unselected4->IndexedEnabling/0/2/Parameter 3674`, `Power4->Parameter 3687`, `Powered4->IndexedEnabling/1/2/Parameter 3687`, `Bypassed4->IndexedEnabling/0/2/Parameter 3687`, `Present5->RangedEnabling/6/13/14/Parameter 3666`, `Select5->Parameter 3675`, `Selected5->IndexedEnabling/1/2/Parameter 3675`, `Unselected5->IndexedEnabling/0/2/Parameter 3675`, `Power5->Parameter 3688`, `Powered5->IndexedEnabling/1/2/Parameter 3688`, `Bypassed5->IndexedEnabling/0/2/Parameter 3688`, `Present6->RangedEnabling/7/13/14/Parameter 3666`, `Select6->Parameter 3676`, `Selected6->IndexedEnabling/1/2/Parameter 3676`, `Unselected6->IndexedEnabling/0/2/Parameter 3676`, `Power6->Parameter 3689`, `Powered6->IndexedEnabling/1/2/Parameter 3689`, `Bypassed6->IndexedEnabling/0/2/Parameter 3689`, `Present7->RangedEnabling/8/13/14/Parameter 3666`, `Select7->Parameter 3677`, `Selected7->IndexedEnabling/1/2/Parameter 3677`, `Unselected7->IndexedEnabling/0/2/Parameter 3677`, `Power7->Parameter 3690`, `Powered7->IndexedEnabling/1/2/Parameter 3690`, `Bypassed7->IndexedEnabling/0/2/Parameter 3690`, `Present8->RangedEnabling/9/13/14/Parameter 3666`, `Select8->Parameter 3678`, `Selected8->IndexedEnabling/1/2/Parameter 3678`, `Unselected8->IndexedEnabling/0/2/Parameter 3678`, `Power8->Parameter 3691`, `Powered8->IndexedEnabling/1/2/Parameter 3691`, `Bypassed8->IndexedEnabling/0/2/Parameter 3691`, `Present9->RangedEnabling/10/13/14/Parameter 3666`, `Select9->Parameter 3679`, `Selected9->IndexedEnabling/1/2/Parameter 3679`, `Unselected9->IndexedEnabling/0/2/Parameter 3679`, `Power9->Parameter 3692`, `Powered9->IndexedEnabling/1/2/Parameter 3692`, `Bypassed9->IndexedEnabling/0/2/Parameter 3692`, `Present10->RangedEnabling/11/13/14/Parameter 3666`, `Select10->Parameter 3680`, `Selected10->IndexedEnabling/1/2/Parameter 3680`, `Unselected10->IndexedEnabling/0/2/Parameter 3680`, `Power10->Parameter 3693`, `Powered10->IndexedEnabling/1/2/Parameter 3693`, `Bypassed10->IndexedEnabling/0/2/Parameter 3693`, `Present11->RangedEnabling/12/13/14/Parameter 3666`, `Select11->Parameter 3681`, `Selected11->IndexedEnabling/1/2/Parameter 3681`, `Unselected11->IndexedEnabling/0/2/Parameter 3681`, `Power11->Parameter 3694`, `Powered11->IndexedEnabling/1/2/Parameter 3694`, `Bypassed11->IndexedEnabling/0/2/Parameter 3694`, `Present12->RangedEnabling/13/13/14/Parameter 3666`, `Select12->Parameter 3682`, `Selected12->IndexedEnabling/1/2/Parameter 3682`, `Unselected12->IndexedEnabling/0/2/Parameter 3682`, `Power12->Parameter 3695`, `Powered12->IndexedEnabling/1/2/Parameter 3695`, `Bypassed12->IndexedEnabling/0/2/Parameter 3695`, `Range->Parameter 3664`, `Info->Parameter 3665`]; gates [`RangedEnabling/1/8/9/Parameter 3663`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Band Lane 2` (`skin/defs_fx.json:22703`, `showWhenDataModelInvalid=Hide`): remap [`State->Parameter 3696`, `Open->Parameter 3700`, `Add->Parameter 3701`, `AddAvail->IndexedEnabling/1/2/Parameter 3702`, `Shown->RangedEnabling/1/8/9/Parameter 3696`, `Face1->IndexedEnabling/1/9/Parameter 3696`, `Face2->IndexedEnabling/2/9/Parameter 3696`, `Face3->IndexedEnabling/3/9/Parameter 3696`, `Face4->IndexedEnabling/4/9/Parameter 3696`, `Face5->IndexedEnabling/5/9/Parameter 3696`, `Face6->IndexedEnabling/6/9/Parameter 3696`, `Face7->IndexedEnabling/7/9/Parameter 3696`, `Face8->IndexedEnabling/8/9/Parameter 3696`, `Present0->RangedEnabling/1/13/14/Parameter 3699`, `Select0->Parameter 3703`, `Selected0->IndexedEnabling/1/2/Parameter 3703`, `Unselected0->IndexedEnabling/0/2/Parameter 3703`, `Power0->Parameter 3716`, `Powered0->IndexedEnabling/1/2/Parameter 3716`, `Bypassed0->IndexedEnabling/0/2/Parameter 3716`, `Present1->RangedEnabling/2/13/14/Parameter 3699`, `Select1->Parameter 3704`, `Selected1->IndexedEnabling/1/2/Parameter 3704`, `Unselected1->IndexedEnabling/0/2/Parameter 3704`, `Power1->Parameter 3717`, `Powered1->IndexedEnabling/1/2/Parameter 3717`, `Bypassed1->IndexedEnabling/0/2/Parameter 3717`, `Present2->RangedEnabling/3/13/14/Parameter 3699`, `Select2->Parameter 3705`, `Selected2->IndexedEnabling/1/2/Parameter 3705`, `Unselected2->IndexedEnabling/0/2/Parameter 3705`, `Power2->Parameter 3718`, `Powered2->IndexedEnabling/1/2/Parameter 3718`, `Bypassed2->IndexedEnabling/0/2/Parameter 3718`, `Present3->RangedEnabling/4/13/14/Parameter 3699`, `Select3->Parameter 3706`, `Selected3->IndexedEnabling/1/2/Parameter 3706`, `Unselected3->IndexedEnabling/0/2/Parameter 3706`, `Power3->Parameter 3719`, `Powered3->IndexedEnabling/1/2/Parameter 3719`, `Bypassed3->IndexedEnabling/0/2/Parameter 3719`, `Present4->RangedEnabling/5/13/14/Parameter 3699`, `Select4->Parameter 3707`, `Selected4->IndexedEnabling/1/2/Parameter 3707`, `Unselected4->IndexedEnabling/0/2/Parameter 3707`, `Power4->Parameter 3720`, `Powered4->IndexedEnabling/1/2/Parameter 3720`, `Bypassed4->IndexedEnabling/0/2/Parameter 3720`, `Present5->RangedEnabling/6/13/14/Parameter 3699`, `Select5->Parameter 3708`, `Selected5->IndexedEnabling/1/2/Parameter 3708`, `Unselected5->IndexedEnabling/0/2/Parameter 3708`, `Power5->Parameter 3721`, `Powered5->IndexedEnabling/1/2/Parameter 3721`, `Bypassed5->IndexedEnabling/0/2/Parameter 3721`, `Present6->RangedEnabling/7/13/14/Parameter 3699`, `Select6->Parameter 3709`, `Selected6->IndexedEnabling/1/2/Parameter 3709`, `Unselected6->IndexedEnabling/0/2/Parameter 3709`, `Power6->Parameter 3722`, `Powered6->IndexedEnabling/1/2/Parameter 3722`, `Bypassed6->IndexedEnabling/0/2/Parameter 3722`, `Present7->RangedEnabling/8/13/14/Parameter 3699`, `Select7->Parameter 3710`, `Selected7->IndexedEnabling/1/2/Parameter 3710`, `Unselected7->IndexedEnabling/0/2/Parameter 3710`, `Power7->Parameter 3723`, `Powered7->IndexedEnabling/1/2/Parameter 3723`, `Bypassed7->IndexedEnabling/0/2/Parameter 3723`, `Present8->RangedEnabling/9/13/14/Parameter 3699`, `Select8->Parameter 3711`, `Selected8->IndexedEnabling/1/2/Parameter 3711`, `Unselected8->IndexedEnabling/0/2/Parameter 3711`, `Power8->Parameter 3724`, `Powered8->IndexedEnabling/1/2/Parameter 3724`, `Bypassed8->IndexedEnabling/0/2/Parameter 3724`, `Present9->RangedEnabling/10/13/14/Parameter 3699`, `Select9->Parameter 3712`, `Selected9->IndexedEnabling/1/2/Parameter 3712`, `Unselected9->IndexedEnabling/0/2/Parameter 3712`, `Power9->Parameter 3725`, `Powered9->IndexedEnabling/1/2/Parameter 3725`, `Bypassed9->IndexedEnabling/0/2/Parameter 3725`, `Present10->RangedEnabling/11/13/14/Parameter 3699`, `Select10->Parameter 3713`, `Selected10->IndexedEnabling/1/2/Parameter 3713`, `Unselected10->IndexedEnabling/0/2/Parameter 3713`, `Power10->Parameter 3726`, `Powered10->IndexedEnabling/1/2/Parameter 3726`, `Bypassed10->IndexedEnabling/0/2/Parameter 3726`, `Present11->RangedEnabling/12/13/14/Parameter 3699`, `Select11->Parameter 3714`, `Selected11->IndexedEnabling/1/2/Parameter 3714`, `Unselected11->IndexedEnabling/0/2/Parameter 3714`, `Power11->Parameter 3727`, `Powered11->IndexedEnabling/1/2/Parameter 3727`, `Bypassed11->IndexedEnabling/0/2/Parameter 3727`, `Present12->RangedEnabling/13/13/14/Parameter 3699`, `Select12->Parameter 3715`, `Selected12->IndexedEnabling/1/2/Parameter 3715`, `Unselected12->IndexedEnabling/0/2/Parameter 3715`, `Power12->Parameter 3728`, `Powered12->IndexedEnabling/1/2/Parameter 3728`, `Bypassed12->IndexedEnabling/0/2/Parameter 3728`, `Range->Parameter 3697`, `Info->Parameter 3698`]; gates [`RangedEnabling/1/8/9/Parameter 3696`]
- `defs_fx.json:fxNativeEditorSurface` :: `FX Band Lane 3` (`skin/defs_fx.json:23155`, `showWhenDataModelInvalid=Hide`): remap [`State->Parameter 3729`, `Open->Parameter 3733`, `Add->Parameter 3734`, `AddAvail->IndexedEnabling/1/2/Parameter 3735`, `Shown->RangedEnabling/1/8/9/Parameter 3729`, `Face1->IndexedEnabling/1/9/Parameter 3729`, `Face2->IndexedEnabling/2/9/Parameter 3729`, `Face3->IndexedEnabling/3/9/Parameter 3729`, `Face4->IndexedEnabling/4/9/Parameter 3729`, `Face5->IndexedEnabling/5/9/Parameter 3729`, `Face6->IndexedEnabling/6/9/Parameter 3729`, `Face7->IndexedEnabling/7/9/Parameter 3729`, `Face8->IndexedEnabling/8/9/Parameter 3729`, `Present0->RangedEnabling/1/13/14/Parameter 3732`, `Select0->Parameter 3736`, `Selected0->IndexedEnabling/1/2/Parameter 3736`, `Unselected0->IndexedEnabling/0/2/Parameter 3736`, `Power0->Parameter 3749`, `Powered0->IndexedEnabling/1/2/Parameter 3749`, `Bypassed0->IndexedEnabling/0/2/Parameter 3749`, `Present1->RangedEnabling/2/13/14/Parameter 3732`, `Select1->Parameter 3737`, `Selected1->IndexedEnabling/1/2/Parameter 3737`, `Unselected1->IndexedEnabling/0/2/Parameter 3737`, `Power1->Parameter 3750`, `Powered1->IndexedEnabling/1/2/Parameter 3750`, `Bypassed1->IndexedEnabling/0/2/Parameter 3750`, `Present2->RangedEnabling/3/13/14/Parameter 3732`, `Select2->Parameter 3738`, `Selected2->IndexedEnabling/1/2/Parameter 3738`, `Unselected2->IndexedEnabling/0/2/Parameter 3738`, `Power2->Parameter 3751`, `Powered2->IndexedEnabling/1/2/Parameter 3751`, `Bypassed2->IndexedEnabling/0/2/Parameter 3751`, `Present3->RangedEnabling/4/13/14/Parameter 3732`, `Select3->Parameter 3739`, `Selected3->IndexedEnabling/1/2/Parameter 3739`, `Unselected3->IndexedEnabling/0/2/Parameter 3739`, `Power3->Parameter 3752`, `Powered3->IndexedEnabling/1/2/Parameter 3752`, `Bypassed3->IndexedEnabling/0/2/Parameter 3752`, `Present4->RangedEnabling/5/13/14/Parameter 3732`, `Select4->Parameter 3740`, `Selected4->IndexedEnabling/1/2/Parameter 3740`, `Unselected4->IndexedEnabling/0/2/Parameter 3740`, `Power4->Parameter 3753`, `Powered4->IndexedEnabling/1/2/Parameter 3753`, `Bypassed4->IndexedEnabling/0/2/Parameter 3753`, `Present5->RangedEnabling/6/13/14/Parameter 3732`, `Select5->Parameter 3741`, `Selected5->IndexedEnabling/1/2/Parameter 3741`, `Unselected5->IndexedEnabling/0/2/Parameter 3741`, `Power5->Parameter 3754`, `Powered5->IndexedEnabling/1/2/Parameter 3754`, `Bypassed5->IndexedEnabling/0/2/Parameter 3754`, `Present6->RangedEnabling/7/13/14/Parameter 3732`, `Select6->Parameter 3742`, `Selected6->IndexedEnabling/1/2/Parameter 3742`, `Unselected6->IndexedEnabling/0/2/Parameter 3742`, `Power6->Parameter 3755`, `Powered6->IndexedEnabling/1/2/Parameter 3755`, `Bypassed6->IndexedEnabling/0/2/Parameter 3755`, `Present7->RangedEnabling/8/13/14/Parameter 3732`, `Select7->Parameter 3743`, `Selected7->IndexedEnabling/1/2/Parameter 3743`, `Unselected7->IndexedEnabling/0/2/Parameter 3743`, `Power7->Parameter 3756`, `Powered7->IndexedEnabling/1/2/Parameter 3756`, `Bypassed7->IndexedEnabling/0/2/Parameter 3756`, `Present8->RangedEnabling/9/13/14/Parameter 3732`, `Select8->Parameter 3744`, `Selected8->IndexedEnabling/1/2/Parameter 3744`, `Unselected8->IndexedEnabling/0/2/Parameter 3744`, `Power8->Parameter 3757`, `Powered8->IndexedEnabling/1/2/Parameter 3757`, `Bypassed8->IndexedEnabling/0/2/Parameter 3757`, `Present9->RangedEnabling/10/13/14/Parameter 3732`, `Select9->Parameter 3745`, `Selected9->IndexedEnabling/1/2/Parameter 3745`, `Unselected9->IndexedEnabling/0/2/Parameter 3745`, `Power9->Parameter 3758`, `Powered9->IndexedEnabling/1/2/Parameter 3758`, `Bypassed9->IndexedEnabling/0/2/Parameter 3758`, `Present10->RangedEnabling/11/13/14/Parameter 3732`, `Select10->Parameter 3746`, `Selected10->IndexedEnabling/1/2/Parameter 3746`, `Unselected10->IndexedEnabling/0/2/Parameter 3746`, `Power10->Parameter 3759`, `Powered10->IndexedEnabling/1/2/Parameter 3759`, `Bypassed10->IndexedEnabling/0/2/Parameter 3759`, `Present11->RangedEnabling/12/13/14/Parameter 3732`, `Select11->Parameter 3747`, `Selected11->IndexedEnabling/1/2/Parameter 3747`, `Unselected11->IndexedEnabling/0/2/Parameter 3747`, `Power11->Parameter 3760`, `Powered11->IndexedEnabling/1/2/Parameter 3760`, `Bypassed11->IndexedEnabling/0/2/Parameter 3760`, `Present12->RangedEnabling/13/13/14/Parameter 3732`, `Select12->Parameter 3748`, `Selected12->IndexedEnabling/1/2/Parameter 3748`, `Unselected12->IndexedEnabling/0/2/Parameter 3748`, `Power12->Parameter 3761`, `Powered12->IndexedEnabling/1/2/Parameter 3761`, `Bypassed12->IndexedEnabling/0/2/Parameter 3761`, `Range->Parameter 3730`, `Info->Parameter 3731`]; gates [`RangedEnabling/1/8/9/Parameter 3729`]

### 18. `defs_filter.json:filterNativeOutOff` (`skin/defs_filter.json:626`, `repeats=None`, instances=2)

**B2 violations (2 instances missing `Data` with `Hide`):**

- `defs_filter.json:filterNativeEditorSurface` :: `Filter Native OUT F1 off` (`skin/defs_filter.json:3100`), remap keys `['Readout0', 'Readout1', 'Readout2', 'Readout3', 'Readout4', 'Static']`, gates `['IndexedEnabling/1/5/Parameter 2842', 'RangedEnabling/0/1/4/Parameter 2875']`
- `defs_filter.json:filterNativeEditorSurface` :: `Filter Native OUT F2 off` (`skin/defs_filter.json:3153`), remap keys `['Readout0', 'Readout1', 'Readout2', 'Readout3', 'Readout4', 'Static']`, gates `['IndexedEnabling/2/5/Parameter 2842', 'RangedEnabling/0/1/4/Parameter 2875']`

**Instances (2):**

- `defs_filter.json:filterNativeEditorSurface` :: `Filter Native OUT F1 off` (`skin/defs_filter.json:3100`, `showWhenDataModelInvalid=Hide`): remap [`Readout0->Parameter 1376`, `Readout1->Parameter 1372`, `Readout2->Parameter 1373`, `Readout3->Parameter 1388`, `Readout4->Parameter 1387`, `Static->Parameter 0`]; gates [`IndexedEnabling/1/5/Parameter 2842`, `RangedEnabling/0/1/4/Parameter 2875`]
- `defs_filter.json:filterNativeEditorSurface` :: `Filter Native OUT F2 off` (`skin/defs_filter.json:3153`, `showWhenDataModelInvalid=Hide`): remap [`Readout0->Parameter 1377`, `Readout1->Parameter 1374`, `Readout2->Parameter 1375`, `Readout3->Parameter 1391`, `Readout4->Parameter 1390`, `Static->Parameter 0`]; gates [`IndexedEnabling/2/5/Parameter 2842`, `RangedEnabling/0/1/4/Parameter 2875`]

### 19. `defs_filter.json:filterNativeOutOn` (`skin/defs_filter.json:1025`, `repeats=None`, instances=2)

**B2 violations (2 instances missing `Data` with `Hide`):**

- `defs_filter.json:filterNativeEditorSurface` :: `Filter Native OUT F1 on` (`skin/defs_filter.json:3206`), remap keys `['Readout0', 'Readout1', 'Readout2', 'Readout3', 'Readout4', 'Static']`, gates `['IndexedEnabling/1/5/Parameter 2842', 'RangedEnabling/2/3/4/Parameter 2875']`
- `defs_filter.json:filterNativeEditorSurface` :: `Filter Native OUT F2 on` (`skin/defs_filter.json:3259`), remap keys `['Readout0', 'Readout1', 'Readout2', 'Readout3', 'Readout4', 'Static']`, gates `['IndexedEnabling/2/5/Parameter 2842', 'RangedEnabling/2/3/4/Parameter 2875']`

**Instances (2):**

- `defs_filter.json:filterNativeEditorSurface` :: `Filter Native OUT F1 on` (`skin/defs_filter.json:3206`, `showWhenDataModelInvalid=Hide`): remap [`Readout0->Parameter 1376`, `Readout1->Parameter 1372`, `Readout2->Parameter 1373`, `Readout3->Parameter 1388`, `Readout4->Parameter 1387`, `Static->Parameter 0`]; gates [`IndexedEnabling/1/5/Parameter 2842`, `RangedEnabling/2/3/4/Parameter 2875`]
- `defs_filter.json:filterNativeEditorSurface` :: `Filter Native OUT F2 on` (`skin/defs_filter.json:3259`, `showWhenDataModelInvalid=Hide`): remap [`Readout0->Parameter 1377`, `Readout1->Parameter 1374`, `Readout2->Parameter 1375`, `Readout3->Parameter 1391`, `Readout4->Parameter 1390`, `Static->Parameter 0`]; gates [`IndexedEnabling/2/5/Parameter 2842`, `RangedEnabling/2/3/4/Parameter 2875`]

### 20. `defs_filter.json:filterNativePzToolbar` (`skin/defs_filter.json:427`, `repeats=None`, instances=2)

**B1 violations (4):**

- `Filter Native PZ Back` (`skin/defs_filter.json:468`), `remap[Data]`: `WithName[Back]/FromGesture/Data` (operand `Data`)
- `Filter Native PZ Reset` (`skin/defs_filter.json:502`), `remap[Data]`: `WithName[Reset]/FromGesture/Data` (operand `Data`)
- `Filter Native PZ Delete Point` (`skin/defs_filter.json:536`), `remap[Data]`: `WithName[Delete Point]/FromGesture/Data` (operand `Data`)
- `Filter Native PZ Apply` (`skin/defs_filter.json:573`), `remap[Data]`: `WithName[Apply]/FromGesture/Data` (operand `Data`)

**Instances (2):**

- `defs_filter.json:filterScreen` :: `Filter Native PZ Toolbar F1` (`skin/defs_filter.json:6584`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3477`]; gates [`IndexedEnabling/1/3/Parameter 3479`]
- `defs_filter.json:filterScreen` :: `Filter Native PZ Toolbar F2` (`skin/defs_filter.json:6616`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3478`]; gates [`IndexedEnabling/2/3/Parameter 3479`]

### 21. `defs_common.json:native keyboard overlay` (`skin/defs_common.json:2930`, `repeats=None`, instances=2) (direct `Local/...` SDK handle, not a remapped alias)

**B1 violations (84):**

- `gate`: `IndexedEnabling/0/3/Local/3/layer` (operand `Local/3/layer`) on 28 entries (e.g. `Key L0 q` at `skin/defs_common.json:3114`)
- `gate`: `IndexedEnabling/1/3/Local/3/layer` (operand `Local/3/layer`) on 28 entries (e.g. `Key L1 Q` at `skin/defs_common.json:3151`)
- `gate`: `IndexedEnabling/2/3/Local/3/layer` (operand `Local/3/layer`) on 28 entries (e.g. `Key L2 1` at `skin/defs_common.json:3188`)

**Instances (2):**

- `defs_lfo.json:lfoScreenTop3` :: `Native Keyboard` (`skin/defs_lfo.json:17187`, `showWhenDataModelInvalid=Hide`): remap []; gates [`RangedEnabling/9/10/11/Parameter 3897`]
- `TUI.json:tab:Macros` :: `Native Keyboard` (`skin/TUI.json:8505`, `showWhenDataModelInvalid=Hide`): remap []; gates [`RangedEnabling/1/8/11/Parameter 3897`]

### 22. `defs_mod.json:modNative_delete_120x56` (`skin/defs_mod.json:1858`, `repeats=None`, instances=1)

**B1 violations (2):**

- `Delete unarmed` (`skin/defs_mod.json:1882`), `gate`: `IndexedEnabling/0/2/Confirm` (operand `Confirm`)
- `Delete confirm` (`skin/defs_mod.json:1919`), `gate`: `IndexedEnabling/1/2/Confirm` (operand `Confirm`)

**Instances (1):**

- `TUI.json:tab:Mod` :: `Mod Native Detail Delete` (`skin/TUI.json:29300`, `showWhenDataModelInvalid=Hide`): remap [`Data->Parameter 3549`, `Caption->WithName[DELETE]/Parameter 3549`, `Confirm->Parameter 3553`]; gates [`RangedEnabling/3/4/5/Parameter 2880`]

### 23. `defs_osc.json:oscWarpChoices` (`skin/defs_osc.json:5830`, `repeats=None`, instances=0) (direct `Local/...` SDK handle, not a remapped alias)

**B1 violations (198):**

- `gate`: `IndexedEnabling/0/7/Local/7/synthyboyWarpPage` (operand `Local/7/synthyboyWarpPage`) on 32 entries (e.g. `None` at `skin/defs_osc.json:5941`)
- `gate`: `IndexedEnabling/1/7/Local/7/synthyboyWarpPage` (operand `Local/7/synthyboyWarpPage`) on 32 entries (e.g. `Asym -` at `skin/defs_osc.json:7109`)
- `gate`: `IndexedEnabling/2/7/Local/7/synthyboyWarpPage` (operand `Local/7/synthyboyWarpPage`) on 32 entries (e.g. `Dist Linear Fold` at `skin/defs_osc.json:8277`)
- `gate`: `IndexedEnabling/3/7/Local/7/synthyboyWarpPage` (operand `Local/7/synthyboyWarpPage`) on 32 entries (e.g. `PD from Osc 3` at `skin/defs_osc.json:9445`)
- `gate`: `IndexedEnabling/4/7/Local/7/synthyboyWarpPage` (operand `Local/7/synthyboyWarpPage`) on 32 entries (e.g. `PD from Noise` at `skin/defs_osc.json:10613`)
- `gate`: `IndexedEnabling/5/7/Local/7/synthyboyWarpPage` (operand `Local/7/synthyboyWarpPage`) on 32 entries (e.g. `Shepard Narrow` at `skin/defs_osc.json:11781`)
- `gate`: `IndexedEnabling/6/7/Local/7/synthyboyWarpPage` (operand `Local/7/synthyboyWarpPage`) on 6 entries (e.g. `Vocode from Osc 2` at `skin/defs_osc.json:12949`)

## `NonZero/<plugin-written handle>` Gate Audit

Checked against `shell-mpc/synthyboy_mpc.cpp` `nativeUiValue()` (`nativeUiBase = 2719`, `docs/params.json` `count = 2637` + 82):

### Exact 0/1 handles (replaceable by `IndexedEnabling/1/2/Parameter N`)

| Skin Location | Gate | C++ Source (`file:line`) | Action |
|---|---|---|---|
| `defs_osc.json:oscScreenTop` (`Osc Native Note Mask`) | `NonZero/Parameter 3823` | `shell-mpc/synthyboy_mpc.cpp:1943` (`nativeNoteMask.layout() != 0 ? 1.0f : 0.0f`) | Replace with `IndexedEnabling/1/2/Parameter 3823` |
| `defs_env.json:envNativeFullFields` (11 entries) | `NonZero/Parameter 2921` | `shell-mpc/synthyboy_mpc.cpp:2140` (`static_cast<float>(nativeEnvEditor.layout())`, 0/1) | Replace with `IndexedEnabling/1/2/Parameter 2921` |
| `defs_fx.json:fxNativeHeader` (`remove`) | `NonZero/Power` | `shell-mpc/native_fx_header.h:61-62` (`on() ? 1.0f : 0.0f`), `synthyboy_mpc.cpp:2114` | Alias `PowerGate` -> `IndexedEnabling/1/2/Parameter 3060..3184` in `fxNativeEditorSurface` |
| `defs_env.json:envHeader` (`Targets`) | `NonZero/Parameter 2921` | `shell-mpc/synthyboy_mpc.cpp:2140` (0/1) | `*Header*` generated by `scripts/header-skin.py`; listed only, not hand-edited |

### Non-0/1 handles (needs a plugin gate / multi-step gate)

| Skin Location | Gate | C++ Source (`file:line`) | Value Range / Status |
|---|---|---|---|
| `TUI.json:tab:Macros` (`Macro Targets Rule/Title/Count`) | `NonZero/Parameter 3832` | `shell-mpc/synthyboy_mpc.cpp:1929`, `native_text.h:81-86` | `BasicLines::value()` returns `(revision-1)%250+1 / 251.0f` when non-empty; **needs a plugin gate** (`kTextGate`) |
| `TUI.json:tab:Macros` (`Macro Targets Empty`) | `NonZero/Parameter 3834` | `shell-mpc/synthyboy_mpc.cpp:1929`, `native_text.h:81-86` | `BasicLines::value()` fractional `(0, 1]`; **needs a plugin gate** (`kTextGate`) |
| `TUI.json:tab:Mod` (`Mod Header` `HdrTitleGate`) | `NonZero/Parameter 3806` | `shell-mpc/synthyboy_mpc.cpp:1927`, `native_text.h:81-86` | `BasicLines::value()` fractional `(0, 1]`; **needs a plugin gate** (`kTextGate`) |
| `TUI.json:tab:Note`, `Velo` (`Curve Native Used Dot *`) | `NonZero/Parameter 3882..3889` | `shell-mpc/synthyboy_mpc.cpp:1927`, `native_text.h:81-86` | `BasicLines::value()` fractional `(0, 1]`; **needs a plugin gate** (`kTextGate`) |
| `defs_env.json:envScreenTop` (`Env Native Tag Dot 0..2`) | `NonZero/Parameter 3868..3870` | `shell-mpc/synthyboy_mpc.cpp:1927`, `native_text.h:81-86` | `BasicLines::value()` fractional `(0, 1]`; **needs a plugin gate** (`kTextGate`) |
| `defs_osc.json:oscScreenTop` (`Native Osc Graph Frame`) | `NonZero/Parameter 3859` | `shell-mpc/synthyboy_mpc.cpp:1927`, `native_text.h:81-86`, `osc_ui.h:1338` | `BasicLines::value()` fractional `(0, 1]`; **needs a plugin gate** (or `RangedEnabling/4/5/6/Parameter 3334`) |
| `defs_common.json:nativeModulationEditor` (`Page arrows`, `Page`) | `NonZero/Parameter 3513` | `shell-mpc/synthyboy_mpc.cpp:1881-1886, 5453` | Reports `0, 1/3, 2/3, 1` (`maxInteger=3`); **needs a plugin gate** / multi-step check (`1..3` vs `0`) |
| `defs_common.json:nativeModulationEditor` (`Route 1..6`) | `NonZero/Parameter 3514..3519` | `shell-mpc/synthyboy_mpc.cpp:1888-1892` | Reports `0, 0.5, 1.0` (`Targets` vs `Modulations`); **needs a plugin gate** (`0/1` or 3-step) |
| `defs_fx.json:fxNativeHeader` (`power`, `fx_header_path`, `fx_header_title`) | `NonZero/Parameter 3059` | `shell-mpc/synthyboy_mpc.cpp:2111` | `header.layout / kLastLayout` (`0..18`, multi-state); **needs a plugin gate** / `RangedEnabling` |
| `defs_fx.json:fxNativeHeader` (`Band MID`) | `NonZero/Parameter 3476` | `shell-mpc/synthyboy_mpc.cpp:2036` | `bandMode / MidAndHigh` (`0..3`, multi-state); **needs a plugin gate** / `RangedEnabling` |
| `defs_osc.json:oscNativeSurface` (`Pan`, `Level`) | `NonZero/Parameter 2878` | `shell-mpc/synthyboy_mpc.cpp:2155`, `native_osc_controls.h:89` | `visibility() / kRootOn` (`0, 0.5, 1.0`, 3 states); **needs a plugin gate** |
| `defs_osc.json:oscNativeSurface` (`Settings`, `Tune`) | `NonZero/Parameter 2978` | `shell-mpc/synthyboy_mpc.cpp:2119` | `layout() / kLastSettings` (`0..8`, 9 states); **needs a plugin gate** |
| `defs_osc.json:oscNativeSurface` (`Detail`) | `NonZero/Parameter 3334` | `shell-mpc/synthyboy_mpc.cpp:2045` | `layout() / LastFrame` (`0..5`, 6 states); **needs a plugin gate** |
| `defs_osc.json:oscHeader*` (`Power`, `Route`) | `NonZero/Parameter 2878` | `shell-mpc/native_osc_controls.h:89` | `*Header*` generated by `scripts/header-skin.py`; listed only, not hand-edited |
