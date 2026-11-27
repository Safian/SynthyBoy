# AGENTS.md

Plugin-drawn layer (`shell-mpc/ui/`; it holds only graphs, meters, curve displays, animation, indicators and the Q-Link/parameter model: controls are native components, no C++ widget library for others; owner 2026-11-27): instructions for AI coding agents are in [shell-mpc/ui/AGENTS.md](shell-mpc/ui/AGENTS.md); human docs in [docs/ui-components/README.md](docs/ui-components/README.md).

For native components follow [components/AGENTS.md](components/AGENTS.md). Screen hooks register each shared MPC component definition once and bind instances to it so the serialized TUI contains one copy of the control subtree.

## Product scope (owner, 2026-11-23)

Develop and release SynthyBoy exclusively for standalone Akai MPC hardware. Follow the [Open MPC community release workflow](https://open-mpc.github.io/workflow.html). Assign work and release checks to the device plugin, skin, shared engine, content and MPC tooling. Use the Mac for offline builds and tests. MPC Desktop compatibility, desktop project interchange, Kick and other plugin hosts are outside this release.

## Release packaging and which architecture to build (owner, 2026-11-20)

- A release has TWO zips, built with the Open MPC tool: `armv7` (MPC Key 61 and the other 32-bit devices) and `aarch64` (MPC XL). Both hold the same plugin, with the display layer (overlay) enabled.
- Use the [overlay skin](scripts/overlay-skin/mpc-ui-pages-overlay.json) to review the release UI, tabs and Q-Link mappings. The legacy 32/34-tab host-only layouts are historical. The host can draw static backgrounds beneath the overlay.
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
- All project screen captures and full-screen previews live only in `docs/screens/`. This is one flat directory: no aliases, metadata, README, comparison sheets, state variants or other files. The only subdirectory is `docs/screens/old/` (owner, 2026-11-27): a safety archive of earlier screens, one `<name>_old.jpg` per screen (copies of the earlier current images and old-UI hardware captures, including popups and zoomed views; validated by `scripts/validate-screen-images.py`). The flat `<screen>_old.jpg` references stay where they are.
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

Dropdowns (Selector) keep the old look: a dark blue fill with a subtle vertical gradient (a thin highlight line at the top, the lower half darkening slightly: measured #131930 at the top to #0d1122 at the bottom on the page ground #0c0f18, a lighter blue border), a "bubble" effect that is not loud. Do not replace it with a flat fill. The gradient colours and where it starts are tokens (`selector.surface`, `selector.border` and the gradient parameters), never literals. Whether the other controls (buttons, ActiveText, fields) get the same treatment is decided per component when its design is reviewed. The overlay controls drawn through `Pen::control` already take the Button gradient from the same tokens (generated `theme_tokens.h`: `controlTop`, `controlBottom`, `controlBorder`, `controlBorderBottom`, `controlGradStart/End`).

## Dropdowns replace left/right selectors (owner, 2026-11-27)

On every screen the left/right chooser (stepper, carousel, the stepper half of the source picker) is being phased out; the dropdown replaces it everywhere, the Play panels included. Dropdown rules: the title is left aligned (above the box), the value inside is centred; a value that does not fit is left aligned and cut at the end with an ellipsis, and it never touches the chevron (a fixed gap, token `selector.dropdown.chevron_gap`). The variant with the small caption inside the box is being phased out. Catalogue entries of the phased-out components stay, marked "KIVEZETÉSRE", until the screens are migrated.

## Radio family replaces the old RadioButton (owner, 2026-11-27)

The old RadioButton (segmented faces) takes too much room and is being phased out. The compact family in our own design replaces it: RadioList (LED rows, many options), RadioGrid (titled button matrix, 2 to 8 options), SlideSwitch (2 to 4 ordered positions) and RotarySelector (stepped dial with pictograms, few options). Dropdowns remain for long lists. The catalogue keeps the old RadioButton entry marked "KIVEZETÉSRE" until the screens are migrated.

## Every control comes from its component (owner, 2026-11-27)

A screen never draws a control face with its own code: buttons, dropdowns, selectors, fields, radios, dials and sliders are drawn by their shared component recipe (`components/native/<name>`; in the plugin-drawn layer, while any control remains there, by `Pen::control`, driven by the same tokens; the removal status is in `docs/review/OVERLAY-CONTROLS-REMOVAL-2026-11-27.md`). Test: change the component's tokens loudly and regenerate; every control of that kind on every screen must change. A control that does not change bypasses the component and is a defect to fix (see `docs/review/BUTTON-MUTATION-AUDIT-2026-11-27.md`). If a control needs a variant the component does not have, add the variant to the component and its catalogue entry, not to the screen file.

## Lists keep the factory behaviour (owner, 2026-11-27)

- Lists use the host MenuOverlay as it is: a row tap marks the row, a tap on the list header applies and closes. No select-and-close, no custom badges in rows, names only.
- Evidence: the Key 61 probes A/B closed the list but lost the selection, because the skin action triggers cannot be ordered (SDK `ui-reference.md`, section on onAction); report in `/private/tmp/claude-501/list-probe/results/`. A plugin-drawn drawer (SDK-PORTING) is the only way to get select-and-close and is not planned.

## Text input is a native keyboard (owner, 2026-11-27)

Text input (Macro rename, LFO shape name) must be a NATIVE keyboard: no C++ overlay control. The host TextEditOverlay does not deliver typed text to a third-party plugin on the XL (our disassembly of /usr/bin/MPC 3.10.1.12: the string setter is never called for plugin parameters; unverified on the Key 61). The solution is a keyboard built from native Button components, each key writing a code into a plugin parameter (`TextEdit Context`, `TextEdit Key`, `TextEdit Text`), the text shown by a Label from the display string of the text parameter; the plan, risks and the Key 61 probe are in /private/tmp/claude-501/textentry-research/REPORT.md (copied to docs/review/TEXT-ENTRY-NATIVE-KEYBOARD-2026-11-27.md). Until the probe proves it, the C++ popup keyboard (`shell-mpc/ui/text_entry.h`) stays in the code and is removed with the native keyboard.

