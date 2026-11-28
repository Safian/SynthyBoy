# AGENTS.md

Plugin-drawn layer (`shell-mpc/ui/`; it holds only graphs, meters, curve displays, animation, indicators, the binding data rows and Q tables: controls are native components, there is no C++ widget library; owner 2026-11-27): instructions for AI coding agents are in [shell-mpc/ui/AGENTS.md](shell-mpc/ui/AGENTS.md); human docs in [docs/ui-components/README.md](docs/ui-components/README.md).

For native components follow [components/AGENTS.md](components/AGENTS.md). Each shared MPC component definition exists once, in `skin/defs_<family>.json` (imported by `skin/TUI.json`), and the instances bind to it, so the skin contains one copy of the control subtree.

## Product scope (owner, 2026-11-23)

Develop and release SynthyBoy exclusively for standalone Akai MPC hardware. Follow the [Open MPC community release workflow](https://open-mpc.github.io/workflow.html). Assign work and release checks to the device plugin, skin, shared engine, content and MPC tooling. Use the Mac for offline builds and tests. MPC Desktop compatibility, desktop project interchange, Kick and other plugin hosts are outside this release.

## Skin editing (owner, 2026-11-28)

The skin is split into files with the host's own `importFiles` (owner, 2026-11-28). `skin/TUI.json` keeps tabs, info and replacementStrings; the component definitions live in `skin/defs_<family>.json` (common, env, filter, fx, lfo, macro, master, mixer, mod, osc, play, tuning, voice), listed in its `importFiles`. A definition used by several families lives in `defs_common.json`; keys stay unique across all files. Edit the family file of the screen you change, then run `python3 scripts/skin-format.py` (formats every `skin/*.json`: one element per line; lossless, keeps duplicate keys), so edits of different screens merge without conflicts. The repeating screens are ONE component each (`oscScreen`, `envScreen`, `lfoScreen`, `filterScreen`, each with its `...Top` parts; the tabs Osc A/B/C, Sub, Noise, Env 1-6, LFO 1-10, RND 1-4, Filter 1-2 hold instances; the plugin binds the shown oscillator, envelope or LFO to them); Play, Macros, Mixer, FX, Mod, Note, Velo, Master, Tuning and Voice are one tab body each. Edit the screen component, not the tab. Work is split by screen wherever the change allows it. One page per screen component (data, definitions, Q groups, C++ owners): [docs/ui-components/screens/README.md](docs/ui-components/screens/README.md). Host facts about `importFiles`: [docs/dsp-knowledge/skin-host-behaviour.md](docs/dsp-knowledge/skin-host-behaviour.md); how to add a family file: [docs/MPC.md](docs/MPC.md), skin section.

Every skin change passes `scripts/validate_synthyboy_skin.py` (it runs the JSON Schemas `schemas/skin-tui.schema.json` and `schemas/skin-qlinks.schema.json` and the semantic checks before every install; `--schema-only` runs just those). A new host finding about the skin format (the host accepts or silently drops a shape) is added as a schema rule in the same commit; see "Skin install checks" and "Skin format schema" in [docs/MPC.md](docs/MPC.md). A warning is not a pass for the Key 61: prove warned forms on the device first.

## Release packaging and which architecture to build (owner, 2026-11-20)

- A release has TWO zips, built with the Open MPC tool: `armv7` (MPC Key 61 and the other 32-bit devices) and `aarch64` (MPC XL). Both hold the same plugin, with the display layer (overlay) enabled.
- Use the committed skin ([skin/TUI.json](skin/TUI.json), [skin/Q-Links.json](skin/Q-Links.json)) to review the release UI, tabs and Q-Link mappings. The legacy 32/34-tab host-only layouts are historical. The host can draw static backgrounds beneath the overlay. `skin/` is the only skin source and is edited directly: there is no skin generation, and the UI C++ headers (`theme_tokens.h`, `overlay_layout.h`, `*_native_layout.h`, `macro_card_contract.h`, `sdf_font_data.h`, `backgrounds_manifest.h`) and the catalogue HTML files are hand-maintained too.
- Ship only the SynthyBoy native editor. Give disabled controls their own visible, read-only presentation. Build, release and installation must reject a missing or broken native skin before replacing the working package. For display-path changes, check the active and inactive states on the requested MPC and check what the host displays while the overlay is off and after it returns.
- During development and testing build ONLY for the architecture of the device being worked on (Key 61: armv7, XL: aarch64). Both architectures are built together only for a release or a release candidate.
- Leave the XL on the package required by the current task. Keep the accepted build installed between tasks. Save new device measurements, including package identity, raw evidence and sampling limitations, under the shared `.hw-local/mpc-perf/` directory. Remove disproved derived results and link their replacement.
- Open items live in [TODO.md](TODO.md): a finished item is deleted there, the commit message is the history.

## DSP knowledge (owner, 2026-11-21)

- Before planning or changing DSP, real-time code, memory use or the MPC display path, read the matching topic files of [docs/dsp-knowledge/](docs/dsp-knowledge/README.md) and follow its rules (clean room, every claim measured / opened source / test, hypotheses marked). A finding that changes a fact updates the topic file in the same commit.

## Work in the shared checkout (owner, 2026-11-21)

- Before staging, committing or merging in the shared checkout, check git status, unmerged paths and any merge or rebase in progress. Another session may own the index. In that case work in a separate worktree or wait for the operation to finish.
- An explicit owner request to try an idea starts the work that turn, including any measurement it depends on. Record unfinished acceptance criteria in [TODO.md](TODO.md), correct a plan that presents an assigned task as optional, and check for unstarted requests before reporting. Bring forward concrete technical proposals when the evidence supports them.
- Close each worker handoff with cleanup. Once the worker has exited and its accepted changes are in develop or the active integration branch, keep still-relevant findings in the owning documentation or TODO, discard obsolete handoff notes and temporary outputs, then remove the temporary worktree and local branch. Do not create separate archives of proven obsolete material. Preserve outstanding work and any potentially valuable unique content.

## Screen image library (owner, 2026-11-24)

For every current MPC screen capture or photo refresh, use the [mpc-screen-photo skill](.agents/skills/mpc-screen-photo/SKILL.md).

- After changing a screen, refresh its current image from the deployed hardware when it is available. Capture the host and overlay together so the image shows what the owner sees.
- All project screen captures and full-screen previews live only in `docs/screens/`. This is one flat directory: no aliases, metadata, README, comparison sheets, state variants or other files. The only subdirectory is `docs/screens/old/` (owner, 2026-11-27): a safety archive of earlier screens, one `<name>_old.jpg` per screen (copies of the earlier current images and old-UI hardware captures, including popups and zoomed views). The flat `<screen>_old.jpg` references stay where they are.
- Each screen has one current image named `<screen>.jpg` and, when available, one fixed historical reference named `<screen>_old.jpg`. `_old` means the chosen historical reference; never overwrite it with a previous current image or update it during rendering. Screen names and provenance metadata belong outside the image directory.
- Screen renderers write their canonical current image to that one path. Documentation and HTML link directly to it. Do not create copies in `docs/ui-map`, `docs/user-guide/img`, `docs/ui-components/komponensek` or elsewhere in the project. Alternative states, comparison renders and debugging captures must use explicit temporary paths outside the repository.
- Component artwork remains in `components/native/<name>/img/`. Icons, fonts, filmstrips and actual runtime skin/overlay background assets are not screen captures; keep their production paths. Do not present an unverified or rejected preview as an approved current hardware capture.
- Changing screenshot paths and documentation must preserve the screen implementation, fixture semantics, native bindings and ongoing screen work. Update the producer and every consumer together so removed screenshots are not recreated.

## MPC: dokumentációból dolgozunk (owner, 2026-11-24)

MPC hardver-, működési és host-bekötési kérdéssel először az `mpc-expert` szakértőhöz fordulj: `/Users/szabi/Documents/Projects/Safian/.claude/agents/mpc-expert.md`. A szerep a konfiguráció betöltésével Sonnet-workerként is futtatható. Indulásakor az expert a `mpc-expert start "kérdés" --prepare` paranccsal párhuzamos forrásfrissítést és RAG-kontextusbetöltést indít; saját kutatását és tudástárát is karbantartja. Ő végzi a helyi és netes kutatást, karbantartja a tudástárát, kikeresi a modellhelyes aktuális kézikönyvet, SDK-t és igazolt host-adatutat, majd konkrét, forrásolt megoldást ad. A fő szál erre támaszkodva fejleszt és az érintett megvalósítást ellenőrzi. A szakértő közös tudástára: `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/`; friss forrásjegyzéke: `findings/mpc-expert-current-sources.md`; projektbeli host-tények: `docs/dsp-knowledge/`.

## Saved device builds (owner, 2026-11-27)

Finished packages live outside the repository in `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-builds/` (see its README.md). Install one without checking out its branch: `scripts/xl-install.sh <package folder>` or `scripts/k61-install.sh <package folder>`. The installer keeps no backup of the replaced package, so save a build there before replacing one worth keeping. Build, archive and install in one step: `scripts/build-k61.sh` (Key 61, armv7) or `scripts/build-xl.sh` (XL, aarch64), `--release` for the release mode, `--no-install` to only archive (details: [docs/MPC.md](docs/MPC.md), "One step device build").

- `old-ui-3bb27424`: the old UI (before the sub-tab conversions of 2026-11-26/27, in-screen tab strips on Osc/Env/Master/Filter/LFO, Mixer peek bar), built from develop commit 3bb27424 for both architectures. Kept for comparison and as a fallback.

## Control look: the old gradient design, not flat (owner, 2026-11-27)

Dropdowns (Selector) keep the old look: a dark blue fill with a subtle vertical gradient (a thin highlight line at the top, the lower half darkening slightly: measured #131930 at the top to #0d1122 at the bottom on the page ground #0c0f18, a lighter blue border), a "bubble" effect that is not loud. Do not replace it with a flat fill. The gradient colours and where it starts are tokens (`selector.surface`, `selector.border` and the gradient parameters), never literals. Whether the other controls (buttons, ActiveText, fields) get the same treatment is decided per component when its design is reviewed. The overlay controls drawn through `Pen::control` already take the Button gradient from the same tokens (`shell-mpc/ui/theme_tokens.h`, hand-kept: `controlTop`, `controlBottom`, `controlBorder`, `controlBorderBottom`, `controlGradStart/End`).

## Dropdowns replace left/right selectors (owner, 2026-11-27)

On every screen the left/right chooser (stepper, carousel, the stepper half of the source picker) is being phased out; the dropdown replaces it everywhere, the Play panels included. Dropdown rules: the title is left aligned (above the box), the value inside is centred; a value that does not fit is left aligned and cut at the end with an ellipsis, and it never touches the chevron (a fixed gap, token `selector.dropdown.chevron_gap`). The variant with the small caption inside the box is being phased out. Catalogue entries of the phased-out components stay, marked "KIVEZETÉSRE", until the screens are migrated.

## Radio family replaces the old RadioButton (owner, 2026-11-27)

The old RadioButton (segmented faces) takes too much room and is being phased out. The compact family in our own design replaces it: RadioList (LED rows, many options), RadioGrid (titled button matrix, 2 to 8 options), SlideSwitch (2 to 4 ordered positions) and RotarySelector (stepped dial with pictograms, few options). Dropdowns remain for long lists. The catalogue keeps the old RadioButton entry marked "KIVEZETÉSRE" until the screens are migrated.

## Every control comes from its component (owner, 2026-11-27)

A screen never draws a control face with its own code: buttons, dropdowns, selectors, fields, radios, dials and sliders are drawn by their shared component recipe (`components/native/<name>`; in the plugin-drawn layer, while any control remains there, by `Pen::control`, driven by the same tokens; the removal status is in `docs/review/OVERLAY-CONTROLS-REMOVAL-2026-11-27.md`). Test: change the component's tokens loudly and rebuild the skin images; every control of that kind on every screen must change. A control that does not change bypasses the component and is a defect to fix (see `docs/review/BUTTON-MUTATION-AUDIT-2026-11-27.md`). If a control needs a variant the component does not have, add the variant to the component and its catalogue entry, not to the screen file.

## Lists: one tap chooses and closes (owner, 2026-11-28)

- The list is a plain string list on the LEFT panel (owner, 2026-11-28: "the lists are too complicated; no custom list items with check marks; a string list, and touching an item on the left panel chooses it, not a tap on the header"): the shared `oscNativeList` is a 640 px wide panel at the left edge (`x 0..639`, `0..628`, opaque ground `osc_routing_overlay_bg.png`), header band `y 0..64` (26 px title at `x 24` on the top level; a 64x64 back arrow `native_list_back_arrow.png` at `x 0..64` and title at `x 72` on a deeper level), 7 rows (`nativeListRow_640x72`, `y = 64 + 72 k`) with 26 px left-aligned names at `x 24` and a gated right-edge `Arrow` (`native_list_row_arrow.png`, `584 16 40 40`) on rows that open a deeper level, and gated `PREVIOUS` (`8 572 150 56`) / `NEXT` (`482 572 150 56`) buttons; `nativeListModal` wraps every list so a tap beside the panel closes without a change, and returning up a level restores the page holding the row just left. Do not give a list its own row design.
- Every list is the plugin's own paged native list (the `nativeui::modboard` cells + `nativeui::liststate` 0/1 flags, `overlay/native_list_cells.h`, 7 rows a page, header back arrow, PREVIOUS and NEXT): one row tap applies the choice and closes the list; an arrow row opens its level. Names only, no custom badges in rows. The owner confirmed this behaviour on the XL (Play Source / LFO / Noise / Shape / Division lists) and wants it everywhere.
- The host MenuOverlay is not used for lists: for a VST2 it draws no names (`docs/dsp-knowledge/skin-host-behaviour.md`, "Host list: who draws the names") and its row tap only marks, the header applies (the Key 61 probes A/B could not order the skin triggers for select-and-close; SDK `ui-reference.md`, onAction). This replaces the 2026-11-27 rule "Lists keep the factory behaviour".
- How a field opens the list of its own choices (every screen except Play's browser-type lists): the field definition holds a transparent "list open" Button (handle `ListOpen`, id 1 of 2); the instance whose `Data` is `Parameter N` maps `ListOpen` to `ResetOnGestureEnd/Parameter <3904 + slot>`, where slot is the index of N in `shell-mpc/native_choice_list.h` `kSlotHandles` (append only; a parent that routes Data by an alias routes `<alias>List` the same way). The plugin (display thread, works without the overlay) opens the paged list with the names the host display of that handle uses and applies a row through the same path as the host's own write; `Parameter 3903` (Choice List Open) gates the shared modal `nativeListModal`. The choice list writes the same `nativeui::liststate` flags as the screen lists (no row arrows, no back arrow, PREVIOUS / NEXT only where a page exists); while it is open the screen views cannot overwrite them. `scripts/validate_synthyboy_skin.py` checks every pairing against the table and rejects a MenuOverlay without own rows. Lists with levels or screen data (the Play source browser, LFO / RND choosers) stay screen-owned: a code on the screen's list handle (`shell-mpc/native_play_list.h`, `overlay/play_view.h`). Header cells get the variant from `components/native/header` (`"list": true`).

## Text input is the native keyboard (owner, 2026-11-28)

Text input (Macro rename, LFO shape name) is a NATIVE keyboard: the component `components/native/keyboard` (recipe from tokens, built from the Button recipe), ONE definition in `skin/TUI.json` instanced on the Macros, LFO, RND and Play tabs. It is NOT drawn by us with the GPU; the C++ `ui::TextEntry` is deleted. The host TextEditOverlay does not deliver typed text to a third-party plugin (XL disassembly of /usr/bin/MPC 3.10.1.12), so each key writes a code into the plugin parameter `TextEdit Key` (ResetOnGestureEnd), the plugin keeps the text (`shell-mpc/native_text_entry.h`: `textentry::Model`, plain data; `KeyPort`: 250 ms per-key debounce, the host delivers one tap as two press events) and a Label shows it from the display string of `TextEdit Text`; `TextEdit Context` opens the keyboard (its display string is the title). Community-proven by the OpenMPC Plugin Manager. Design and the open device checks: `docs/dsp-knowledge/native-text-entry.md`, `TODO.md`.

## MPC questions: no guessing, ask the expert, teach it once (owner, 2026-11-27)

Everything about the MPC host, the SDK, the skin and the hardware goes to the `mpc-expert` FIRST. No guessing, no "probably", no trial and error on a device for something that can be looked up. If the expert does not know the answer:
1. The expert finds it out: manuals, the official SDK, the community sources, the web, our host disassembly (its own research duty, see `.claude/agents/mpc-expert.md`).
2. If research cannot settle it, the expert and the coordinator define ONE minimal measurement (the exact question, the smallest test), it is run once, and the result is written by the expert into its knowledge: `docs/dsp-knowledge/skin-host-behaviour.md` (or the matching topic page) with the raw evidence under `.hw-local/mpc-perf/`, then `mpc-expert index`.
3. From then on the question is answered from the knowledge base; the same thing is never asked, researched or measured twice. A result that exists only in `/tmp`, `TODO.md` or a chat does not count: it must be in the expert's knowledge before the task is called done.
A task brief that touches the host or the skin tells the worker to run `mpc-expert start "<keywords>" --prepare` first.

## No tests during UI rewrites (owner decision)

While controls are being removed from the plugin layer or pages are converted to native cells, run no tests and no builds; the next integration round's compile is the check (details in [shell-mpc/ui/AGENTS.md](shell-mpc/ui/AGENTS.md)).

## Mechanical changes: bulk find-and-replace, not file-by-file editing (owner, 2026-11-27)

A change that is mechanical (renaming a class or token, replacing a widget by a model, removing a file and every reference to it, switching a recipe call) is done as ONE scripted bulk find-and-replace over the whole file set (`git grep -l` to list the files, then perl/sed/python regex to rewrite them), followed by the compile or the touched tests as the check; the compile/test errors are the work list for the few places that need hand work. Delete first and let `git grep` / the compiler show the remaining references. Do not edit dozens of files by hand one after another and do not split a mechanical change into stages or workers. Keep the replacement script in the commit message or in the report so the change can be repeated.


## Page-loading pattern: every screen loads the same way (owner, 2026-11-28)

The proven fast Key 61 page-load pattern (Voice, Master, FX card switch) applies to EVERY screen; a screen that breaks a rule is a defect. Evidence, per-screen audit, ranking and the worst-five remediations: [docs/review/page-load-pattern.md](docs/review/page-load-pattern.md); measured facts: [docs/dsp-knowledge/mpc-display.md](docs/dsp-knowledge/mpc-display.md). Checklist:

1. **Compact tab tree:** keep each tab ≤ ~450 expanded components (never > 1,000); measure with the ordered-pairs expander over `skin/TUI.json` + `skin/defs_*.json`. COUNT `repeats`: a definition with `"repeats": N` costs N times its nodes (Osc detail grids: 8 columns; the real Osc tab was 3631 nodes, not 1377). A closed (hidden) layer still costs the same: the Osc closed Detail layer alone was ~0.7 s of the 1.3 s arrival (Key 61 bisect, `.hw-local/mpc-perf/key61-osc-pattern-20261128/`). Measure with a bisect (empty one layer in the installed package, 3 samples), not by guessing.
2. **Retarget, do not duplicate:** share one `<family>Screen` per repeating tab family and retarget one shared slot bank instead of per-state, per-item, per-source, or per-model copies.
3. **No blank-at-0 root gates:** bind standalone tab roots to direct engine params or always-valid cached handles, never a zero-default or cross-tab view gate; gate text-owned cells only on 0/1 `kTextGate` (`UNSURE` on Key 61).
4. **Lean artwork:** use plain `backgroundData` colour (`ff090d19`, `image: ""`) and shared control filmstrips; avoid embedding large hidden modals (e.g. keyboard) across repeating tabs.
5. **Ordinary root frame:** keep `setUiFullOpen(false)` at every native root and on `leftTab()`; assert Frame 24 (`Parameter 2675`) only for true plugin-drawn modals.
6. **Read-only cache on exit:** on `leftTab()`/`onHidden()`, call `suspend()` (disarm writes, keep display state readable) and `renewInputRevisions()` on return—never reset layout/view handles or text lines to `0`/empty.
7. **Cold pre-population:** seed read-only display banks before host exposure and after every `effSetProgram`/`effSetChunk` without claiming gestures or Q groups.
8. **Ordered batched writes:** in `native*Poll()`, report cell values/kinds/texts *before* the layout/view gate, suppress unchanged values via `nativeUiReported`, and force-report only on rebind/ack/format change.
9. **Paint-window protection:** arm `nativePagePaintUntil` (`kNativePaintWindowMs = 150 ms`) on *every* layout entry and visible-to-visible layout change, and call `requestNativeControlNames()`, never bare `requestProxyNames()`.
10. **Trailing-edge metadata deferral:** send Q 1..16 values immediately, coalesce all `audioMasterUpdateDisplay` triggers onto `pageSwitchNamesPending` (`kPageSwitchSettleMs = 800 ms`, cap `2500 ms`; device timing `UNSURE`), and set `statusDue_ = true` on view change.

A measured win on any screen is written back into this list in the same commit.
