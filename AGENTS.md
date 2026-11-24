# AGENTS.md

UI component library (`shell-mpc/ui/`): instructions for AI coding agents are in [shell-mpc/ui/AGENTS.md](shell-mpc/ui/AGENTS.md); human docs in [docs/ui-components/README.md](docs/ui-components/README.md).

For native components follow [components/AGENTS.md](components/AGENTS.md). Screen hooks register each shared MPC component definition once and bind instances to it so the serialized TUI contains one copy of the control subtree.

## Product scope (owner, 2026-11-23)

Develop and release SynthyBoy exclusively for standalone Akai MPC hardware. Follow the [Open MPC community release workflow](https://open-mpc.github.io/workflow.html). Assign work and release checks to the device plugin, skin, shared engine, content and MPC tooling. Use the Mac for offline builds and tests. MPC Desktop compatibility, desktop project interchange, Kick and other plugin hosts are outside this release.

## Release packaging and which architecture to build (owner, 2026-11-20)

- A release has TWO zips, built with the Open MPC tool: `armv7` (MPC Key 61 and the other 32-bit devices) and `aarch64` (MPC XL). Both hold the same plugin, with the display layer (overlay) enabled.
- Use the [overlay skin](scripts/overlay-skin/mpc-ui-pages-overlay.json) to review the release UI, tabs and Q-Link mappings. The legacy 32/34-tab host-only layouts are historical. The host can draw static backgrounds beneath the overlay.
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

- After changing a screen, refresh its current image from the deployed hardware when it is available. Capture the host and overlay together so the image shows what the owner sees.
- All project screen captures and full-screen previews live only in `docs/screens/`. This is one flat directory: no subdirectories, aliases, metadata, README, comparison sheets, state variants or other files.
- Each screen has one current image named `<screen>.jpg` and, when available, one fixed historical reference named `<screen>_old.jpg`. `_old` means the chosen historical reference; never overwrite it with a previous current image or update it during rendering. Screen names and provenance metadata belong outside the image directory.
- Screen renderers write their canonical current image to that one path. Documentation and HTML link directly to it. Do not create copies in `docs/ui-map`, `docs/user-guide/img`, `docs/ui-components/komponensek` or elsewhere in the project. Alternative states, comparison renders and debugging captures must use explicit temporary paths outside the repository.
- Component artwork remains in `components/native/<name>/img/`. Icons, fonts, filmstrips and actual runtime skin/overlay background assets are not screen captures; keep their production paths. Do not present an unverified or rejected preview as an approved current hardware capture.
- Changing screenshot paths and documentation must preserve the screen implementation, fixture semantics, native bindings and ongoing screen work. Update the producer and every consumer together so removed screenshots are not recreated.

## MPC: dokumentációból dolgozunk (owner, 2026-11-24)

MPC hardver-, működési és host-bekötési kérdéssel először az `mpc-expert` szakértőhöz fordulj: `/Users/szabi/Documents/Projects/Safian/.claude/agents/mpc-expert.md`. A szerep a konfiguráció betöltésével Sonnet-workerként is futtatható. Indulásakor az expert a `mpc-expert start "kérdés" --prepare` paranccsal párhuzamos forrásfrissítést és RAG-kontextusbetöltést indít; saját kutatását és tudástárát is karbantartja. Ő végzi a helyi és netes kutatást, karbantartja a tudástárát, kikeresi a modellhelyes aktuális kézikönyvet, SDK-t és igazolt host-adatutat, majd konkrét, forrásolt megoldást ad. A fő szál erre támaszkodva fejleszt és az érintett megvalósítást ellenőrzi. A szakértő közös tudástára: `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/`; friss forrásjegyzéke: `findings/mpc-expert-current-sources.md`; projektbeli host-tények: `docs/dsp-knowledge/`.
