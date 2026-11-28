# AGENTS.md

Plugin-drawn layer (`shell-mpc/ui/`; it holds only graphs, meters, curve displays, animation, indicators, the binding data rows and Q tables, plus the kept `ListPopup` and C++ `TextEntry` keyboard: controls are native components, there is no C++ widget library; owner 2026-11-27): instructions for AI coding agents are in [shell-mpc/ui/AGENTS.md](shell-mpc/ui/AGENTS.md); human docs in [docs/ui-components/README.md](docs/ui-components/README.md).

For native components follow [components/AGENTS.md](components/AGENTS.md). In `skin/TUI.json` each shared MPC component definition exists once and the instances bind to it, so the TUI contains one copy of the control subtree.

## Product scope (owner, 2026-11-23)

Develop and release SynthyBoy exclusively for standalone Akai MPC hardware. Follow the [Open MPC community release workflow](https://open-mpc.github.io/workflow.html). Assign work and release checks to the device plugin, skin, shared engine, content and MPC tooling. Use the Mac for offline builds and tests. MPC Desktop compatibility, desktop project interchange, Kick and other plugin hosts are outside this release.

## Skin editing (owner, 2026-11-28)

`skin/TUI.json` is edited directly and stays formatted one element per line (`python3 scripts/skin-format.py` after every edit; lossless, keeps duplicate keys), so edits of different screens merge without conflicts. Work is split by screen (tab) wherever the change allows it.

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

Finished packages live outside the repository in `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-builds/` (see its README.md). Install one without checking out its branch: `scripts/xl-install.sh <package folder>` or `scripts/k61-install.sh <package folder>`. The installer keeps no backup of the replaced package, so save a build there before replacing one worth keeping.

- `old-ui-3bb27424`: the old UI (before the sub-tab conversions of 2026-11-26/27, in-screen tab strips on Osc/Env/Master/Filter/LFO, Mixer peek bar), built from develop commit 3bb27424 for both architectures. Kept for comparison and as a fallback.

## Control look: the old gradient design, not flat (owner, 2026-11-27)

Dropdowns (Selector) keep the old look: a dark blue fill with a subtle vertical gradient (a thin highlight line at the top, the lower half darkening slightly: measured #131930 at the top to #0d1122 at the bottom on the page ground #0c0f18, a lighter blue border), a "bubble" effect that is not loud. Do not replace it with a flat fill. The gradient colours and where it starts are tokens (`selector.surface`, `selector.border` and the gradient parameters), never literals. Whether the other controls (buttons, ActiveText, fields) get the same treatment is decided per component when its design is reviewed. The overlay controls drawn through `Pen::control` already take the Button gradient from the same tokens (`shell-mpc/ui/theme_tokens.h`, hand-kept: `controlTop`, `controlBottom`, `controlBorder`, `controlBorderBottom`, `controlGradStart/End`).

## Dropdowns replace left/right selectors (owner, 2026-11-27)

On every screen the left/right chooser (stepper, carousel, the stepper half of the source picker) is being phased out; the dropdown replaces it everywhere, the Play panels included. Dropdown rules: the title is left aligned (above the box), the value inside is centred; a value that does not fit is left aligned and cut at the end with an ellipsis, and it never touches the chevron (a fixed gap, token `selector.dropdown.chevron_gap`). The variant with the small caption inside the box is being phased out. Catalogue entries of the phased-out components stay, marked "KIVEZETÉSRE", until the screens are migrated.

## Radio family replaces the old RadioButton (owner, 2026-11-27)

The old RadioButton (segmented faces) takes too much room and is being phased out. The compact family in our own design replaces it: RadioList (LED rows, many options), RadioGrid (titled button matrix, 2 to 8 options), SlideSwitch (2 to 4 ordered positions) and RotarySelector (stepped dial with pictograms, few options). Dropdowns remain for long lists. The catalogue keeps the old RadioButton entry marked "KIVEZETÉSRE" until the screens are migrated.

## Every control comes from its component (owner, 2026-11-27)

A screen never draws a control face with its own code: buttons, dropdowns, selectors, fields, radios, dials and sliders are drawn by their shared component recipe (`components/native/<name>`; in the plugin-drawn layer, while any control remains there, by `Pen::control`, driven by the same tokens; the removal status is in `docs/review/OVERLAY-CONTROLS-REMOVAL-2026-11-27.md`). Test: change the component's tokens loudly and rebuild the skin images; every control of that kind on every screen must change. A control that does not change bypasses the component and is a defect to fix (see `docs/review/BUTTON-MUTATION-AUDIT-2026-11-27.md`). If a control needs a variant the component does not have, add the variant to the component and its catalogue entry, not to the screen file.

## Lists keep the factory behaviour (owner, 2026-11-27)

- Lists use the host MenuOverlay as it is: a row tap marks the row, a tap on the list header applies and closes. No select-and-close, no custom badges in rows, names only.
- Evidence: the Key 61 probes A/B closed the list but lost the selection, because the skin action triggers cannot be ordered (SDK `ui-reference.md`, section on onAction); report in `/private/tmp/claude-501/list-probe/results/`. A plugin-drawn drawer (SDK-PORTING) is the only way to get select-and-close and is not planned.

## Text input is our own C++ keyboard (owner, 2026-11-28)

Text input (Macro rename, LFO shape name) uses our own plugin-drawn keyboard (`shell-mpc/ui/text_entry.h`). There is no system keyboard: the host TextEditOverlay does not deliver typed text to a third-party plugin (XL disassembly of /usr/bin/MPC 3.10.1.12; unverified on the Key 61). The native Button keyboard (components/native/keyboard, `TextEdit` parameters) was tried and deleted; do not rebuild it.

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

