# RELEASE - download-ready release pipeline

Branch `agy/release` (from `develop` d5cf633). Task: release workflow, fill-manifest, content-pack tool, local release script, docs. Continued and finished
by a second worker after the first (agy) worker ran out of quota; the first worker's files were reviewed line by line and run.

## Done

| Item | Files |
|------|-------|
| Release workflow: tag `v*.*.*` and `workflow_dispatch` only, `verify` -> 5 platform builds + packs -> `draft` (draft GitHub release, `plugin.json.release`, `SHA256SUMS`); actions pinned by SHA like `build.yml`, no tag or release is created by the workflow itself | `.github/workflows/release.yml` |
| Packager (zip per platform, binary architecture check, reproducible zip, `.sha256` sidecar) | `scripts/package-release.py` |
| Manifest filler (size and SHA-256 computed from the files, zip inspected, tag must equal `v<version>`, result checked) | `scripts/fill-manifest.py` |
| Checker: modes checked-in / `--released [--assets DIR]` / `--local`, pack verification, schema subset validator refuses unknown keywords | `scripts/check-manifest.py`, `schema/plugin-manifest.schema.json` |
| ContentPack tool: `manifest`, `verify`, `build`, `build-all`, `split` | `tools/contentpack/contentpack.py` (+ tests, CTest) |
| ARMv7 CLAP in Docker (Debian 12) with ELF check | `scripts/build-armv7-clap.sh` |
| Local release (`dist/`, relative or `file://` urls) | `scripts/make-local-release.sh`, `scripts/tests/test_make_local_release.sh` |
| `plugin.json`: `author`, `description`, factory-presets pack entry with placeholders | `plugin.json` |
| Docs | `docs/RELEASE.md`, `docs/ARCHITECTURE.md` section 7, `README.md` |
| Build fix found by the ARMv7 build: `core/src/fx_rack.cpp` lacked `#include <string>` (GCC 12 / Debian 12) | `core/src/fx_rack.cpp` |

## Review of the first worker's code

Read in full against ARCHITECTURE 6-7, the schemas and Kick's `PluginManifest.cpp`. Format matches what Kick parses (`assets[]`, `entry_point`, `key_id`, `signature` as a
128-hex string, not an object). No silent fallback or loosened check found; the schema validator raises on a keyword it does not implement. The tests were
mutation-checked (wrong size, skipped hash comparison, skipped architecture check, skipped https check: each made tests fail). Missing before: the file
`scripts/tests/test_make_local_release.sh` that the CTest registration referenced, `build-all`, the workflow, the local script, the docs. The `--help` of
`build-armv7-clap.sh` printed past its header (fixed).

## Verified locally

- `docker info` works (Docker 29.9.2, aarch64 VM). **linux-armv7 CLAP built for real** with `scripts/build-armv7-clap.sh` (image `kick-armv7-builder:latest`, GCC 12 Debian 12):
  ELF32 ARM EABI5 hard-float, NEON, needs GLIBC <= 2.34, exports `clap_entry`; `build-armv7-clap/synthyboy.clap` (4.7 MB) in the worktree, zipped as
  `dist/com.safian.synthyboy-0.1.0-linux-armv7.zip` (1.8 MB). The first attempt failed on the missing `<string>` include (fixed, see above).
- Target `synthyboy_clap` -> `shell-clap/synthyboy.clap` exists; host build is a universal Mach-O (arm64 + x86_64), packed to `dist/...-macos-universal.zip`.
- `scripts/make-local-release.sh` end to end (armv7 via Docker + macOS + presets pack): `check-manifest.py --local` OK.
- `released` mode: the two real zips plus fake-header zips for the other three platforms, `fill-manifest.py --repo ... --tag v0.1.0`, `check-manifest.py --released --assets`: OK (5 builds, 1 pack); wrong tag refused.
- `check-manifest.py` on the checked-in `plugin.json`: OK. YAML of `release.yml` parses (ruby and python yaml). CTest in a build dir inside the worktree
  (`FETCHCONTENT_SOURCE_DIR_CLAP`): `synthyboy_test_preset`, `synthyboy_test_contentpack`, `synthyboy_factory_pack_verifies`, `synthyboy_test_release_scripts`,
  `synthyboy_manifest_check`, `synthyboy_local_release_script`: 6/6 passed.

## Only CI can verify

- The workflow itself (no run was possible): the five runner builds, `scripts/build.sh -- -DSYNTHYBOY_BUILD_TESTS=OFF` on all platforms incl. the universal JUCE build with
  `CMAKE_OSX_ARCHITECTURES`, the Windows native path `build/shell-clap/Release/synthyboy.clap`, `gh run list/download/release create` with the job token and permissions,
  the "build.yml was green for this SHA" gate, the Docker build on `ubuntu-latest` with `--jobs $(nproc)` (memory), `python3` on the Windows runner (fallback `python`).
- The JUCE builds are packed only in CI (`--with-juce` of the local script was not run here: JUCE sources are not in this worktree).
- linux-aarch64 / linux-x86_64 / windows-x64 packages were tested with fake headers only (the packager's architecture checks), not with real binaries.
- Not tested on a device (not allowed): nothing was run on the MPC.

## Not done, needs the owner

- **Factory wavetables and factory multisample instrument packs are not in `plugin.json`.** No generated wavetables exist (the engine computes its built-in tables) and
  the 97 factory instruments (4.6 GB, question 91) are not produced by anything in this repo: they are written into the user library by the Serum importer from the
  owner's Serum 2 files (`tests/serum-presets`, git-ignored, third party). Declaring a pack for them would need a redistributable source, which does not exist,
  and `check-manifest.py` rightly fails on a declared pack without files. The tool is ready (`contentpack.py manifest/split/build`, 2 GB limit check in the tool and
  in `check-manifest.py`); once an owned instrument source exists: a directory per instrument, `contentpack.py split`, one `contentPacks` entry per part with
  `"required": false, "recommended": false`.
- The Kick-side installer: see `docs/RELEASE.md` "Open point on the Kick side".
- `build.yml`'s armv7 job builds the core only; a CI job that builds `synthyboy_clap` for ARMv7 on every push would have caught the `<string>` bug earlier (the
  release job now builds it, only at release time).
