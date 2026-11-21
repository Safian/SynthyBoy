# Plan: take the Vital engine out (one engine: the own engine)

**Owner, 2026-11-20:** "Ha ez megvan akkor egy motor legyen!" The precondition holds: the own engine acts on 2634 of 2637 keys (`docs/ui-design/OWN-ENGINE-GAP.md`, generated 2026-11-20). The MPC and iOS builds already ship the own engine (`scripts/build-mpc.sh:174`, `.github/workflows/build.yml:429-431`); the desktop JUCE and CLAP builds still default to Vital (`CMakeLists.txt:89-92`, `shell-juce/PluginProcessor.cpp:38-47`, `shell-clap/synthyboy_plugin.cpp:22-25`).

**Clean room:** no step reads inside `core/engine-vital/`, `core/vital-import/` (except where V1 says it stays and is only moved), or `.hw-local/synth-knowledge/`. Removing a directory is `git rm -r`, no reading.

## Owner decisions (the tasks they gate are marked)

**Decided by the owner on 2026-11-21: every row as recommended** ("Igen mehet!"). V6 is the owner's own step.

| # | Question | Recommendation |
|---|---|---|
| V1 | Keep the Vital **preset importer** (`core/vital-import/`, 16 files; `tools/vital-import/` CLI; Editor import menu; `user.imported-vital` pack)? It reads the `.vital` JSON file format (interoperability), it is not engine code (`NOTICE:34` lists it as not Vital-derived). | Keep it. Users bring Vital presets; only `synthyboy_vital_roundtrip` (links the engine) goes. |
| V2 | Keep the `wtedit::vital` wavetable-editor data model (`core/include/synthyboy/wavetable_edit.h:400-502`, `core/wavetable-edit/src/vital_model.cpp`; open licence question `OWN-ENGINE-GAP.md:133-137`)? | Keep it, rename the namespace to `wtedit::layers` (it is our own model of the file format). |
| V3 | (Correction 2026-11-21: `tools/param-audit` and `tools/ref-compare` also serve Serum 2 and the own engine, so they stay and are ported to the own engine only; `tools/own-oracle` goes, its header `own_oracle.h` stays for `test_own_*`.) The reference tools that compare against Vital disappear: `tools/own-oracle`, `tools/param-audit` (4 ctests), `tools/ref-compare` (1 ctest), `bench --engine vital`, CI informational benchmark. | Remove. Before removal, freeze their last results as documents (one run each, already in `docs/` where it exists). |
| V4 | Licence after removal: stays AGPL-3.0 (JUCE is the stated reason, `README.md:209-210`), Vital lines leave `NOTICE` and `THIRD_PARTY.md`, `plugin.json:9` loses "derived from Vital". | Stay AGPL-3.0. |
| V5 | Bitmap golden pictures: `tests/bitmap-gui/golden/` (Vital) goes, `golden-own/` becomes the only set. | Yes. |
| V6 | `.hw-local/synth-knowledge/vital/` (Vital source outside the repo, `TODO.md`): delete or move away from the agents' reach? | Move it out of `.hw-local` (owner does it; agents never touch it). |
| V7 | Old states that name `EngineKind::vital` or presets saved under the Vital engine: load them into the own engine (the parameter table is shared). | Yes, with a test that a v2 state loads. |

## Tasks (each: test commit that fails today, then the change; no build inside a task; one batch build at the end)

| Task | Files | Change | Test |
|---|---|---|---|
| R1 default engine | `CMakeLists.txt:57,89-102` | `SYNTHYBOY_WITH_VITAL` default OFF, `SHELL_ENGINE` default own. | configure without flags: `SYNTHYBOY_HAS_VITAL=0` (cmake script test). |
| R2 shells | `shell-juce/PluginProcessor.cpp:38-47,167,514`, `shell-clap/synthyboy_plugin.cpp:22-25`, `shell-clap/synthyboy_clap.cpp:29,133`, `shell-clap/bitmap/NativeUiHost.cpp:200,228`, `shell-mpc/synthyboy_mpc.cpp:1696`, `shell-mpc/CMakeLists.txt:25-30`, `shell-mpc/accept.sh:47-50,332-339` | Own engine only; engine name string; accept.sh checks own. | existing shell tests built without Vital; CLAP test with a real tail on the own engine (the TODO line on the CLAP test). |
| R3 core API | `core/include/synthyboy/engine.h:222-233`, `core/src/engine_factory.cpp`, `core/tests/test_engine.cpp:76-89`, `core/tests/test_engines.h`, comments in `engine_caps.h`, `readout.h`, `params.h` | `EngineKind::vital` kept only as a state-load alias (V7) mapping to own; factory has one engine. | V7 state-load test. |
| R4 CMake test defines | `CMakeLists.txt:339-342,415-666`, `tests/bitmap-gui/CMakeLists.txt:16,58` | Drop `SYNTHYBOY_SHELL_ENGINE_VITAL`; golden-own only (V5). | the 13 UI tests and `synthyboy_test_bitmap_golden` pass. |
| R5 tools and scripts (V3) | `CMakeLists.txt:216-221`, `tools/own-oracle`, `tools/param-audit`, `tools/ref-compare`, `tools/vital-import/vital_roundtrip.cpp` + its CMake lines, `bench/bench_engine.cpp`, `scripts/run-param-audit.sh`, `scripts/param_audit_report.py`, `scripts/run-ref-compare.sh`, `scripts/audit-vital-import.py` (roundtrip part) | `git rm`; bench has one engine. | `scripts/tests` pass; no dangling references (`git grep`). |
| R6 CI | `.github/workflows/build.yml:11-12,408-447` | Jobs build own; `ios-vital-free` becomes the general rule: `scripts/check-no-vital.sh` runs for every build with the importer allow-list. | workflow lint (actionlint if available). |
| R7 delete the engine | `core/CMakeLists.txt:69-82,94-97`, `core/engine-vital/` (329 files) | `git rm -r core/engine-vital`; core no longer links it. | full build + full ctest in the batch build; `check-no-vital.sh` on the whole tree. |
| R8 importer stays (V1) and model rename (V2) | `core/wavetable-edit`, `wavetable_edit.h`, `shell-juce/ui/panels/WavetableEditorSheet.*`, tests | Namespace rename only. | existing importer and wavetable tests. |
| R9 docs and legal (V4) | `NOTICE`, `THIRD_PARTY.md`, `plugin.json:9`, `README.md:12,55,76,88-103,192-194`, `docs/ARCHITECTURE.md:33-34,130,391`, `docs/UPSTREAM.md` (mark as history), generated docs (`gen-params-doc.py:99`, `gen-factory-presets.py:567`, `gen-sound-set-batch2-doc.py`) | Text only; regenerate generated docs. | `scripts/tests/test_docs_release.py` and the generators' `--check`. |
| R10 gate | `scripts/check-no-vital.sh` | Fails if `core/engine-own/` gets a Vital identifier or GPL header (the TODO line "Automatikus ellenőrzés"). | its self-test. |

**Order:** R1-R4 (one owner: build files and shells), R5-R6 (scripts/CI owner), R8-R9 (docs/model owner) can run in parallel; R7 last, after the batch build of R1-R6 passes. **Done when** a clean configure without flags builds only the own engine, the full ctest passes, `git grep -i vital` outside the importer, the history docs and the allow-list is empty, and the four Vital lines of `TODO.md` (removal plan, CLAP test, synth-knowledge source, automatic check) are deleted.
