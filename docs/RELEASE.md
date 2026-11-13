# Releasing SynthyBoy

How a release is cut, what is in it, what the Kick plugin manager reads from it, and how to test the same flow locally without GitHub.
Design background: [ARCHITECTURE.md](ARCHITECTURE.md) sections 6 (content packs) and 7 (manifest).

## What a release contains

| Asset | Made by | Content |
|-------|---------|---------|
| `com.safian.synthyboy-<version>-<platform>.zip` for `linux-armv7`, `linux-aarch64`, `linux-x86_64`, `macos-universal`, `windows-x64` | `scripts/package-release.py` | `synthyboy.clap` (the native module: the file Kick loads. It contains the bitmap GUI, `plugin.json` "gui", and a private statically linked JUCE 8; no JUCE of its own is visible to the host), `juce/SynthyBoy.clap` and `juce/SynthyBoy.vst3` for DAWs (all platforms except linux-armv7, which has the native shell only), `LICENSE`, `NOTICE`, `THIRD_PARTY.md`, `README.txt` |
| `SynthyBoy-MPC-<version>.zip` for Akai MPC hardware | `scripts/package-mpc-release.py` | `Safian - VST - SynthyBoy/` plugin directory containing `plugin.json`, `version.xml`, both 32-bit and 64-bit binaries in `VST/`, `Plugin Skins/`, `Content/presets`, `INSTALL.txt`, and `LICENSE` |
| `<packid>-<packversion>.zip` for every entry of `contentPacks` | `tools/contentpack/contentpack.py build-all` | `contentpack.json` + the files, all SHA-256 verified |
| `plugin.json.release` | `scripts/fill-manifest.py` | the copy of `plugin.json` with status `released`, urls, SHA-256, sizes, `entry_point`, `assets[]` |
| `SHA256SUMS` | `scripts/fill-manifest.py --sums` | `<sha256>  <file>` for every asset and the manifest |

The zips are reproducible (sorted entries, fixed timestamps). `package-release.py` refuses a native binary of the wrong architecture for the platform name
(ELF class / machine / ARM hard-float ABI, universal Mach-O with arm64 and x86_64, PE x86-64).

## The bitmap GUI in the native module

`synthyboy.clap` of every platform renders the editor itself and offers it through the CLAP extension `com.safian.bitmap-gui/1` ([BITMAP-GUI-EXTENSION.md](BITMAP-GUI-EXTENSION.md)):
a host that cannot run the plug-in's toolkit (the Kick app on the MPC, a framebuffer) copies pixels and forwards input. `plugin.json` says so in its `gui` section
(`kind: bitmap`, the extension ids, the preferred / minimum / maximum size, the builds that contain it); `scripts/check-manifest.py` compares it with
`shell-clap/bitmap_gui_ext.h`, `shell-juce/ui/Editor.h` and `shell-clap/bitmap/BitmapSession.h`, so the manifest cannot promise what the module does not do, and
`fill-manifest.py` lists only the builds of the release in it. What the build checks for a module:

* **Exports**: `scripts/check-clap-exports.sh synthyboy.clap` (ctest `synthyboy_clap_exports`, the release workflow, the ARMv7 script): only `clap_entry` and
  `synthyboy_clap_get_entry` are visible (version script on Linux, exported symbols list on macOS, `__declspec(dllexport)` on Windows). The JUCE inside is private.
* **Dependencies (Linux)**: the module needs only libc, libm, libstdc++ and libgcc_s (and the dynamic loader). FreeType is linked statically, the glyphs are the embedded Roboto
  (no fontconfig), ALSA / JACK are compiled out, libX11 is not linked (JUCE loads it on demand and the GUI never opens a display). `scripts/build-armv7-clap.sh` fails the build otherwise.
* **Option**: `SYNTHYBOY_BITMAP_GUI` (CMake) is ON with the JUCE plug-in and in the ARMv7 script, OFF otherwise (`--no-ui` of the script builds the JUCE free module without it). The static
  library `synthyboy_static` (iOS, `SYNTHYBOY_WITH_VITAL=OFF`) never contains it: `scripts/check-no-vital.sh` stays clean.

## Cutting a release

1. Set the version in `CMakeLists.txt` (`project(SynthyBoy VERSION x.y.z)`) and in `plugin.json` (`version`); `scripts/check-manifest.py` fails if they differ.
   If a content pack changed, raise its version in its `contentpack.json` and in `plugin.json` (same check) and regenerate its file list
   (`python3 tools/contentpack/contentpack.py manifest --dir content/presets`).
2. Merge to the release commit, wait for the `build` workflow to be green on it. The release workflow refuses a tagged commit without a successful `build.yml` run.
3. `git tag vX.Y.Z && git push origin vX.Y.Z`. The tag must be `v<version of plugin.json>`. (Or run the `release` workflow by hand from the Actions tab: it uses the
   tag name `v<version>`; GitHub creates the tag only when the draft is published.) The workflow never runs on a normal push or pull request.
4. `.github/workflows/release.yml`: `verify` (tag, `check-manifest.py`, green build) -> `build` (five platform jobs in parallel; linux-armv7 is the Docker build of
   `scripts/build-armv7-clap.sh`, Debian 12, JUCE sources and helper tool fetched / built in the container) and `packs` -> `draft`: downloads the zips, runs `fill-manifest.py --repo ... --tag ...` and `check-manifest.py --released --assets`,
   creates the **draft** release with all assets.
5. Open the draft, check the assets (`SHA256SUMS`, `plugin.json.release`), then publish it by hand. Nothing in the workflow publishes.
6. Kick side: give the plugin manager the `plugin.json.release` of the release (Kick signs it with the project key, see below).

A failed run leaves no release. A draft for a tag that already has a release is refused by `gh release create`; delete the old draft first.

## What the Kick side reads

Kick's `PluginManifest` (`Kick/clap_host/src/PluginManifest.cpp`) parses `plugin.json.release`:
`id`, `name`, `version`, `author`, `license`, `homepage`, `description`, `kind`, `min_host_version`, `builds{platform: url, sha256, size, entry_point}`,
`assets[]` (`id`, `name`, `version`, `url`, `sha256`, `size`, `required`, `type`, `license`), `permissions`, `key_id`, `signature`.
It picks the build of the host platform (`macos-universal` serves arm64 and x86_64) and uses `entry_point` (the `.clap` inside the zip) to find the module; entries
without a SHA-256 are rejected, which is why a partial (local) manifest lists only the platforms that were built. `assets[]` are the content packs
(`required: false` packs are optional downloads); the plugin itself never downloads anything.

Open point on the Kick side (not verifiable from this repo): the `PluginInstaller` in the Kick checkout this was written against (`fo-repo-mozgas-51f099`)
installs from an unpacked directory that holds `manifest.json` and the `.clap`; fetching `url`, checking size / SHA-256 and unpacking the zip is not in that
code. Until the downloader exists, test by hand: unzip the platform zip, save `plugin.json.release` next to `synthyboy.clap` as `manifest.json`.

**Which fields Kick signs:** `canonicalSigningPayload()` is the manifest re-serialised from the fields above without `signature`, so `key_id`, `builds` (url, sha256, size,
entry_point) and `assets[]` are covered. The fields Kick does not parse (`schemaVersion`, `clap_id`, `static`, `contentPacks`, `path`, `status`) are not signed
and not used by Kick; they are for this repo's checks and the static (iOS / Android) loader. The repo never contains a signature or a key: the checked-in file
has neither `key_id` nor `signature`, and `signature`, if present, must be 128 hex characters.

## Testing the flow locally (no GitHub)

```
scripts/make-local-release.sh                 # linux-armv7 (Docker) + the host platform + the packs -> dist/
scripts/make-local-release.sh --with-juce     # also the JUCE CLAP / VST3 of the host platform in its zip
scripts/make-local-release.sh --no-armv7      # without Docker
scripts/make-local-release.sh --file-urls     # absolute file:// urls instead of urls relative to dist/plugin.json.release
```

Result in `dist/` (git-ignored): the zips, `plugin.json.release` (urls relative to it, so the folder can be copied to a USB stick or served with
`python3 -m http.server`), `SHA256SUMS`. The script clears old zips in the output folder first, and finishes with `check-manifest.py --local`.
Docker not usable: the script prints the exact commands for a Debian 12 / Ubuntu Linux host (`scripts/build-armv7-clap.sh --print-manual`) and warns loudly
that linux-armv7 was not built; `--require-armv7` makes that an error, `--armv7-clap FILE` takes a binary built elsewhere. Building the ARMv7 module on a
newer distribution is not advised: its glibc is what the device needs (Debian 12: 2.36, the Kick app's toolchain). An image that already has the toolchain
can be given with `ARMV7_IMAGE` (e.g. `kick-armv7-builder:latest`); missing packages are installed in the throw-away container.

Nothing in these scripts talks to a device.

## Content packs

`plugin.json` `contentPacks` lists every pack: `path` is the pack directory (with its `contentpack.json`), `required` / `recommended` flags, and the placeholders
the release fills. `check-manifest.py` verifies every declared pack (manifest against `schema/contentpack.schema.json`, same id / version / kind / licence as in
`plugin.json`, `requires_plugin` satisfied by the plugin version, every file and SHA-256, total below 2 GB).
Tool: `python3 tools/contentpack/contentpack.py {manifest,verify,build,build-all,split}` (`--help`). A pack larger than one asset is split per instrument with `split`
(one pack per top level directory, id `<id>-<directory>`), every part is declared in `plugin.json`.

Declared today: `com.safian.synthyboy.factory-presets` (CC0, 112 presets, 0.9 MB).
Not declared, and why: there are no generated factory wavetables (the engine's built-in tables are computed at run time) and the factory multisample instruments
(4.6 GB, morning question 91) exist only in a user library built from third party Serum factory files, which are not part of this repository and not
redistributable; see `docs/ui-design/RELEASE-REPORT.md`.

## Release package for the MPC

The standalone Akai MPC plugin folder built by `scripts/build-mpc.sh` (`build-mpc/Safian - VST - SynthyBoy/`) is packaged for release with `scripts/package-mpc-release.py`:

```bash
scripts/package-mpc-release.py --plugin-dir "build-mpc/Safian - VST - SynthyBoy" --out dist/ --source-url "https://github.com/Safian/SynthyBoy" [--version <version>]
```

The packager performs strict verification of the plugin folder before creating the archive:
* **Binaries**: both 32-bit ARM (`VST/synthyboy-arm32.so`) and 64-bit AArch64 (`VST/synthyboy-arm64.so`) shared libraries must exist and match ELF header requirements (ELF 32-bit ARM / ELF 64-bit AArch64).
* **Manifest & Metadata**: `plugin.json` must be present and declare both `file32` and `file64`; `version.xml` must exist.
* **Skins**: `Plugin Skins/` must contain `TUI.json` and `Q-Links.json`.
* **Presets**: `Content/presets/` must contain at least 100 `*.sbpreset` files.

On any verification failure, the script aborts with a descriptive error message and exit code 1.

The output is `SynthyBoy-MPC-<version>.zip` holding exactly the single top-level plugin folder `Safian - VST - SynthyBoy/` with `INSTALL.txt` and `LICENSE` included inside the folder. The archive is reproducible (sorted entries, fixed timestamps, macOS metadata files such as `._*` and `.DS_Store` excluded) and accompanied by `<zip>.sha256`.

`INSTALL.txt` explains installation to `VST2 Plugins` on an internal drive or external USB/SD storage, required MPC OS version (3.10.1 or later), enabling the enhancement switches under **Preferences → Enhancements (Beta)** (**Enable Enhancements** and **External Plugin Support**), how to load the instrument, removal, third-party plugin stability notes, and the AGPL-3.0-or-later license and source repository URL.

## Pins and secrets

All actions in `release.yml` are pinned by commit SHA (checkout, upload-artifact), everything else is the preinstalled `gh` CLI with the job's `GITHUB_TOKEN`
(`contents: write` only in the last job). The manifest and the scripts hold no secret.
