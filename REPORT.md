# REPORT — Documentation, AGENTS.md, and TODO.md Sync (2026-11-28)

Branch: `agy/docs-fix` (branched from `ng/listpanel` at `f8bc700e`).
Build / tests: not run (documentation-only task; only Markdown files modified).

## Commits on `agy/docs-fix`

1. `ec9a5f5a` — `docs(dsp-knowledge): reconcile superseded list and importFiles statements in skin-host-behaviour`
2. `77e4f343` — `docs(agents): unify Lists section and add device-measured skin rules`
3. `f3c07320` — `docs(ui-components,mpc): align component, screen, and MPC docs with 2026-11-28 list, skin, and preset facts`
4. `e63b1a52` — `docs(todo): merge duplicate list items, remove stale branch tags, and update Key 61 findings`
5. `cdc2201f` — `docs(todo): keep stepper inventory in present tense`
6. (final commit) — `REPORT.md`

---

## 1. Per-file changes and sources

### `docs/dsp-knowledge/skin-host-behaviour.md` (`ec9a5f5a`)

Only internal contradictions against later-dated facts in the same file were fixed:

- **Removed line 9** (`- Decision: lists keep the factory behaviour (row marks, header applies). Names only, no custom row badges.`): superseded by owner decision 2026-11-28 in the same file (`docs/dsp-knowledge/skin-host-behaviour.md:15`).
- **Corrected line 18** (`BACK / UP or GROUPS / PREVIOUS / NEXT` and `Not yet measured on a device`): contradicted later Key 61 facts in the same file (`docs/dsp-knowledge/skin-host-behaviour.md:102,118`, `.hw-local/mpc-knowledge/device-observed/lists-key61-20261128/README.md:72-79`). Replaced with `header back arrow on deeper levels, PREVIOUS / NEXT; measured on the Key 61 below`.
- **Corrected lines 56, 62, 67** (`XL not yet checked`, `the XL has not been checked`, `Still UNMEASURED: ... the XL`): contradicted the later 2026-11-28 XL `importFiles` measurement (`docs/dsp-knowledge/skin-host-behaviour.md:79`). Updated to state the trial run vs. the later XL full split-skin verification.

### `AGENTS.md`, `components/AGENTS.md`, `shell-mpc/ui/AGENTS.md`, `CLAUDE.md` (`77e4f343`)

- **`CLAUDE.md`, `components/CLAUDE.md`, `shell-mpc/ui/CLAUDE.md`**: all three are git symlinks (`120000`) pointing to `AGENTS.md`; left untouched.
- **`components/AGENTS.md` (`1-18`), `shell-mpc/ui/AGENTS.md` (`1-39`)**: no contradictions with A–E found; left untouched.
- **`AGENTS.md` (`77e4f343`)**:
  - **Merged into `## Lists (owner, 2026-11-28)`** (`AGENTS.md:73-84`, replacing `## Lists: one tap chooses and closes` lines 73–78):
    - Removed narrative `"This replaces the 2026-11-27 rule 'Lists keep the factory behaviour'"`.
    - Stated Decision A in present tense: unified 640 px left-edge panel (`oscNativeList`, `nativeListRow_640x72`, `nativeListModal` in `skin/defs_common.json:269-397,2482-3011,6475-6582`), plain left-aligned names, no row faces/check marks/badges, right-edge arrow (`native_list_row_arrow.png`, `liststate::kRowArrow`), deeper-level header back arrow (`native_list_back_arrow.png`, `liststate::kDeeperLevel`) + open level name, one-tap select-and-close, tap beside panel closes, no `BACK`/`UP`/`GROUPS` buttons, gated `PREVIOUS`/`NEXT`, page restore on level return (`shell-mpc/overlay/native_list_cells.h:19-121`, `shell-mpc/native_ui_params.h:172-200`), Fabric XL `PRESET` two-level model and open data-dial/press requirement (`.hw-local/mpc-knowledge/findings/list-patterns.md:11-33,39-57`, `docs/dsp-knowledge/skin-host-behaviour.md:116-120`).
    - Kept `ListOpen` (`shell-mpc/native_choice_list.h:26-37`, `Parameter 3903..4016`), `scripts/validate_synthyboy_skin.py`, Play lists (`shell-mpc/native_play_list.h`, `shell-mpc/overlay/play_view.h:657-735`), and screen-owned browser pointers (`osc_ui.h`, `env_ui.h`, `filter_ui.h`).
  - **Kept `## Dropdowns replace left/right selectors (owner, 2026-11-27)`** (`AGENTS.md:61-63`): defines closed dropdown controls vs. steppers and dropdown box typography; no list or `MenuOverlay` rules inside.
  - **Added `## Skin rules measured on the device (2026-11-28)`** (`AGENTS.md:86-94`): 5 one-line rules from B (`Data` required in non-empty remap, plain aliases in definitions / modifiers on `Parameter N` in outermost instance, `IndexedEnabling/1/2/` for exact 0/1 instead of `NonZero/`, overlapping `Label`/`Image` takes touch even when empty, duplicate `version` keys + `python3 scripts/skin-format.py` `rewritten` check), pointing to `docs/dsp-knowledge/skin-host-behaviour.md:89-105` and `scripts/skin-format.py:61-71`.
  - **Corrected `## Text input is the native keyboard` (`AGENTS.md:98`, formerly line 82)**: `skin/TUI.json` -> `skin/defs_common.json` (`skin/defs_common.json:3013`).

### `docs/ui-components/` and `docs/MPC.md` (`f3c07320`)

Note: `docs/presets.md` and `docs/host-contract.md` do not exist in this repository.

- **`docs/ui-components/README.md:72`**: replaced `Show Overlay` / `fx editor choice menu` claim with pointer to `AGENTS.md` Lists and `TODO.md` (`fx editor choice menu` is absent from `skin/`, while `components/native/selector/selector.py:642` still carries the default).
- **`docs/ui-components/header.md:82`**: added Key 61 verified `oscHeader*` `TYPE` / `SOURCE` / `BROWSE` / `ROUTE` fact under `Native Osc Detail Title` gate `IndexedEnabling/1/2/Parameter 3901` (`skin/defs_osc.json:21659-21660`, `docs/dsp-knowledge/skin-host-behaviour.md:91-94`).
- **`docs/ui-components/filter_selector.md:13`**: fixed `skin/TUI.json` -> `skin/defs_filter.json` (`skin/defs_filter.json:32`), noted `MenuOverlay` deprecation and `TODO.md` (`AGENTS.md` Lists).
- **`docs/ui-components/keyboard.md:64`**: fixed `skin/TUI.json` -> `skin/defs_common.json` (`skin/defs_common.json:3013`), noted instances in `skin/TUI.json:8790` and `skin/defs_lfo.json:17391`, added `python3 scripts/skin-format.py` `rewritten` check (`scripts/skin-format.py:71`).
- **`docs/ui-components/native-mixer-strip.md:10,152-155`**: fixed `skin/TUI.json` -> `skin/defs_mixer.json`, replaced outdated `useNativeWidgets() && hostConfirmed() && ! listOpen_` popup text with `useNativeCells()` (`shell-mpc/overlay/mixer_ui.h:149-213`) and `nativeListModal` (`Parameter 3903`).
- **`docs/ui-components/screens/README.md:7,31`**: unified list description (`nativeListModal` wrapping `oscNativeList`, including `envScreenTop` at `skin/defs_env.json:1335` and Play via `kPlayList`) with link to `AGENTS.md` Lists; updated gating rule (`IndexedEnabling/1/2/` rather than `NonZero/` on exact 0/1 handles) and linked `AGENTS.md` skin rules (`docs/dsp-knowledge/skin-host-behaviour.md:104`).
- **`docs/ui-components/screens/play.md:25,44`**: removed `BACK, UP/GROUPS` and `on BACK at the top level`; aligned with `nativeListModal` / `oscNativeList` (`640 px`, header back arrow on deeper levels, tap beside panel closes without change; `shell-mpc/overlay/play_view.h:696-697`, `skin/defs_common.json:2482-3011`).
- **`docs/ui-components/screens/osc.md:7`**: linked `AGENTS.md` Lists for `nativeListModal`/`oscNativeList`, added `Native Osc Detail Title` gate `IndexedEnabling/1/2/Parameter 3901` (`skin/defs_osc.json:21659-21660`) and Key 61 verified `oscHeader*` touch/focus chain (`skin/defs_osc.json:18118,22936,23041,23150`, `docs/dsp-knowledge/skin-host-behaviour.md:91-94`).
- **`docs/ui-components/screens/env.md:7`**, **`filter.md:7`**, **`lfo.md:10`**, **`fx.md:7`**: described current `nativeListModal` / `oscNativeList` usage concisely and linked `AGENTS.md` Lists (`skin/defs_env.json:1335`, `skin/defs_filter.json:7590,7770`, `skin/defs_lfo.json:17418`, `skin/TUI.json:23840`).
- **`docs/ui-components/screens/macros.md:8`**, **`mod.md:7,9`**: clarified that the inline 1200×56 picker (`modNative_list_1200x56`, `BACK`, `GROUPS`) remains in `skin/TUI.json:7465-7687,30708-30980` (migration tracked in `TODO.md`), while Mod header dropdowns open `nativeListModal`.
- **`docs/MPC.md:42,54-55,394-396,584,590`**:
  - `42`, `394-396`: aligned `Presets/` (`184` `.xpl` files in 15 categories, `content/mpc-presets/`) and `PRESET` list with Fact D (`Sounds` grouped from `Presets/`, `PRESET` flat VST2 list because host calls `effGetProgramNameIndexed` with `-1` and `program_calls_effGetNumProgramCategories: 0`, `numPrograms` 0/1 open hypothesis; `docs/dsp-knowledge/skin-host-behaviour.md:117,122-129`).
  - `54-55`, `584`: added Key 61 skin rules and `scripts/skin-format.py` `rewritten` check (`docs/dsp-knowledge/skin-host-behaviour.md:89-104`, `scripts/skin-format.py:71`).
  - `590`: replaced `the Play page keeps its carousel and its own selectLfo` with `the Play page keeps its own selectors and dropdowns` (`skin/defs_play.json`, `shell-mpc/overlay/play_view.h`).

---

## 2. `TODO.md` changes (`e63b1a52`, `cdc2201f`)

### Deleted as DONE or false-premise duplicates
- **Old line 46** (`Közös, balról nyíló natív listaválasztó a Play OSC/ENV/LFO/RND modulfejlécekbe... gyári lista: sor-érintés jelöl, fejléc-érintés alkalmaz és zár`): false premise (`MenuOverlay` header-tap expectation superseded by Decision A, `docs/dsp-knowledge/skin-host-behaviour.md:15`); Play lists are already on `playNativeList` / `nativeListModal` (`shell-mpc/overlay/play_view.h:657-735`); remaining XL check merged into the Play list item (new line 76).
- **Old line 47** (`LFO host-listák... a host MenuOverlay 57 forrás / 36 csoport, fejléc-érintésre alkalmaz`): false premise (`lfoNativePickTarget` in `skin/defs_lfo.json:15381-16676` already uses `ListOpen` -> `native_choice_list.h`, not `MenuOverlay`); still-open point-modulation condition (`a SOURCE minden kijelölt pontra érvényes`) merged into line 6.

### Merged items (old line numbers -> surviving item)
- **Old lines 5 + 9 + 206 -> surviving item at line 5** (`Egyszerű, kétszintű natív lista a gyári (Fabric XL PRESET) minta szerint`, `(fut: ng/listpanel, agy/list-levels)`):
  - Removed superseded `426 px`, `nativeListRow_426x68`, non-modal Osc/Env/Filter claim, `BACK/UP` buttons, and `18 px` title (`skin/defs_common.json:269-397,2482-3011,6475-6582`, commit `8ffc6385`).
  - Recorded Key 61 wavetable SOURCE browser result (`listpanel-f8bc700e` / `osclist-774e84cf`, `.hw-local/mpc-knowledge/device-observed/lists-key61-20261128/README.md:72-79`).
  - Preserved all still-open conditions: visible panel ground / dimmed backdrop & uppercase deeper-level title (`lists-key61-20261128/README.md:79`), `Toggle Switch` row-tap parity flakiness (`skin-host-behaviour.md:105`), `components/native` list component, owner finger multisample SOURCE/BROWSE Key 61, Env/Filter/Note/Velo `nativeListModal` checks, Mod/Macros `modNative_list_1200x56` migration, XL check, `docs/screens/` images.
- **Old lines 6 + 47 -> surviving item at line 6** (`Egykoppintásos választólisták a többi képernyőn`): replaced `BACK zár` with `a panelen kívüli érintés zár` (`oscNativeList` has no `BACK` button) and merged `az LFO/RND pontmodulációnál a SOURCE minden kijelölt pontra érvényes`.
- **Old lines 46 + 79 + 207 -> surviving item at line 76** (`🔴 Play listák az átírt oscNativeList szerződéssel`): replaced `7 sor, BACK, UP, PREVIOUS, NEXT` with 640 px two-level `nativeListModal` / `oscNativeList` contract (`FromGesture/Parameter 3584`, `Data -> Parameter 3588`, commit `b47b101f`), kept checks (1)–(6), fresh `armv7`/`aarch64` build check on Key 61 + XL, and `skin-host-behaviour.md` documentation condition.

### Corrected items and `(fut: ...)` branch markers
- **Line 29 (new line 28)**: removed false premise `a lista aktuális sora nincs jelölve (gyári szabály),` (Decision A / `list-patterns.md:44`: plain names, no check marks).
- **Line 65 (new line 62, `cdc2201f`)**: removed `oscNativeSetting_table` and `oscDetailUnisonMode` (not in `skin/defs_osc.json`), kept `oscDetailStepper` (`skin/defs_osc.json:18476`), replaced `MenuOverlay`/`gyári listával` with `ListOpen` (`native_choice_list.h`).
- **Line 66 (new line 63)**: removed `(fut: ng/pageload)` (branch absent from `git branch -a`).
- **Line 74 (new line 71)**: removed `(fut: ng/keyline)` (branch absent from `git branch -a`).
- **Line 93 (new line 90)**: changed `FX +ADD gyári lista` -> `FX +ADD lista`.
- **Line 196 (new line 193)**: clarified `(a)` as `nativeListModal` (`oscNativeList`) check.
- **Line 203 (new line 200)**: shortened to remaining `runlat.py` body-tap script update (`skin-host-behaviour.md:89-95` already records the Label gate and body-tap fact).
- **Line 204 (new line 201)**: updated premise so `TYPE`/`SOURCE`/`BROWSE`/`ROUTE` and `POWER` are no longer listed as broken; narrowed to `MORE` and remaining body controls.
- **Line 205 (new line 202)**: updated per Decision C (`TYPE`, `SOURCE`, `BROWSE`, `ROUTE` work on Key 61 with `oschdr-titlegate-cb7bd5b1` + header focus chain); narrowed open scope to Sub/Noise header cells on Key 61 and all Osc header cells on XL.
- **Line 209 (new line 204)**: removed `(fut: agy/alias-rule)` (branch absent from `git branch -a`).
- **Line 210 (new line 205)**: kept `(fut: agy/label-overlap)` (branch exists in `git branch -a`).

---

## 3. Left as it is, unsure

- **`docs/ui-components/header.md:158` (`NonZero/Parameter 3806`) & `skin/defs_mod.json`**: `modHeader` still uses `NonZero/Parameter 3806` in the skin on this branch; left `header.md:158` matching the committed skin rather than claiming it already uses `IndexedEnabling/1/2/`.
- **`docs/ui-components/screens/fx.md:7`, `lfo.md:10`, `filter_selector.md:13`**: remaining `MenuOverlay` definitions (`fx chain add menu`, `lfoNativeChoiceDrawer*`, `filterNativeChainChoices`) are still present in `skin/defs_fx.json`, `skin/defs_lfo.json`, and `skin/defs_filter.json` on this branch; kept their screen descriptions matching the current skin while linking `AGENTS.md` Lists / `TODO.md`.
- **`TODO.md` line 214 (`Osc MORE oldal üres...`) vs. lines 201 / 209**: kept line 214 (new line 209) separate because line 209 (new line 204) tracks the skin-wide alias-rule validator/audit while line 214 tracks device verification of MORE/OPTIONS/grain/sample/spectral.
- **`TODO.md` line 111 (`Env- és LFO-fül eszközön: választó (üres / részben / mind használt, +)...`)**: left untouched per `"Do not reword items you have no new fact for"`, though Env/LFO tabs use hardware sub-tabs (`env.md`, `lfo.md`).
- **`docs/dsp-knowledge/native-text-entry.md:7`**: still says `ONE definition native keyboard overlay in skin/TUI.json` (actual definition is in `skin/defs_common.json:3013`); left untouched because `docs/dsp-knowledge/*.md` may only be edited for internal contradictions with a later statement in the same file.
- **`docs/review/page-load-pattern.md:290,298`**: mentions `MenuOverlay` optimization options; left untouched as a dated review analysis outside the three target doc groups.
