# Releasing SynthyBoy

How a release is cut, what is in it, what the Kick plugin manager reads from it, and how to test the same flow locally without GitHub.
Design background: [ARCHITECTURE.md](ARCHITECTURE.md) sections 6 (content packs) and 7 (manifest).

## What a release contains

| Asset | Made by | Content |
|-------|---------|---------|
| `com.safian.synthyboy-<version>-<platform>.zip` for `linux-armv7`, `linux-aarch64`, `linux-x86_64`, `macos-universal`, `windows-x64` | `scripts/package-release.py` | `synthyboy.clap` (the native module: the file Kick loads. It contains the bitmap GUI, `plugin.json` "gui", and a private statically linked JUCE 8; no JUCE of its own is visible to the host), `juce/SynthyBoy.clap` and `juce/SynthyBoy.vst3` for DAWs (all platforms except linux-armv7, which has the native shell only), `LICENSE`, `NOTICE`, `THIRD_PARTY.md`, `README.txt` |
| Two MPC archives (armv7 and aarch64) | Open MPC release tool / scripts/package-mpc-release.py, see [Release package for the MPC](#release-package-for-the-mpc) | Each archive carries the matching plugin and display helper, skins and content. |
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
  library `synthyboy_static` (iOS) never contains it: `scripts/check-no-vital.sh` stays clean.

## Product versioning

The product version is set in CMakeLists.txt through the project VERSION and SYNTHYBOY_BUILD_NUMBER, and in plugin.json through version and build. All other versions derive from these entries. The VST2 integer version follows the formula major multiplied by 1000 plus minor multiplied by 100 plus patch multiplied by 10 plus the build number, meaning version 0.1.0 build 1 evaluates to 101. Format versions for state and presets remain completely separate, so earlier projects and presets continue to load because the plugin only validates the state document format version. The packs carry their own version field, and for the first release all of them are 0.1.0.

## Cutting a release

1. Set the version in `CMakeLists.txt` (`project(SynthyBoy VERSION x.y.z)` and `set(SYNTHYBOY_BUILD_NUMBER n)`) and in `plugin.json` (`version` and `build`); `scripts/check-manifest.py` fails if they differ.
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

The macOS Audio Unit (`SynthyBoy.component`, `SynthyBoy_AU` target) is built by `scripts/build.sh` next to the VST3 and CLAP, but is not yet part of the release zips (`--with-juce` packs CLAP and VST3 only); it needs the same signing and notarisation as the other bundles. The MPC VST2 build is independent of it.

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

Declared today: `com.safian.synthyboy.factory-presets` (CC0, 184 presets).
Not declared yet: `com.safian.synthyboy.factory-wavetables` (`content/factory-wavetables`, 72 wavetables) and `com.safian.synthyboy.factory-samples` (`content/factory-samples`, 12 samples, 3 instruments, 3 impulse responses), generated by `scripts/gen-content-pack.py` (CC0, synthesised); the presets of sound set batch 2 name their files (docs/release/SOUND-SET-BATCH-2.md section 2), `scripts/build-mpc.sh` installs them as `Content/packs/<pack id>/`. Declaring them in `plugin.json` is a release decision (two more assets for `contentpack.py build-all`); the desktop shells have no pack root.
Not declared, and why: the factory multisample instruments of the Serum kind
(4.6 GB, morning question 91) exist only in a user library built from third party Serum factory files, which are not part of this repository and not
redistributable; see `docs/ui-design/RELEASE-REPORT.md`.

## Release package for the MPC

The [release rules](../AGENTS.md) require TWO archives, both with the display overlay enabled:
* **armv7**: for MPC Key 61 and all other 32-bit ARM standalone devices (MPC One, One+, MPC Live, Live II, MPC X, X SE, Key 37, Force).
* **aarch64**: for MPC XL and MPC Live III (64-bit ARM / Gen2).

During development and testing, build only for the architecture of the device being worked on (`--arch arm32` for Key 61, `--arch arm64` for XL). Both architectures are built together only for a release or a release candidate.

Two release packaging workflows are supported and verified:
1. **Open MPC release tool (`tools/release.py`)**: packages portable community release archives (`SynthyBoy-<version>-mpc-armv7.zip` and `SynthyBoy-<version>-mpc-aarch64.zip`) with device-side `install.sh` / `uninstall.sh`, `INSTALL.md`, catalog manifest (`mpc-plugin.json`), and self-contained `portable/Safian - VST - SynthyBoy/` plugin directory. Verified on mock artifacts in `scripts/tests/test_open_mpc_release.py`.
2. **Standalone Akai MPC packager (`scripts/package-mpc-release.py`)**: packages built standalone plugin folder(s) directly into two native release archives for extraction to `VST2 Plugins/` on internal drive, SD card, or USB storage:
   - `SynthyBoy-MPC-<version>-armv7.zip`
   - `SynthyBoy-MPC-<version>-aarch64.zip`

### Packaging with the Open MPC release tool

```bash
# 32-bit ARM (Key 61 and 32-bit devices)
python3 tools/release.py --so build/synthyboy-arm32.so --skin "build/Safian - VST - SynthyBoy" \
    --entry build/entry-arm32.xml --version <version> --so-path VST/synthyboy-arm32.so -o dist/

# 64-bit AArch64 (MPC XL)
python3 tools/release.py --so build/synthyboy-arm64.so --skin "build/Safian - VST - SynthyBoy" \
    --entry build/entry-arm64.xml --version <version> --so-path VST/synthyboy-arm64.so -o dist/
```

### Packaging with package-mpc-release.py

The standalone Akai MPC plugin folder built by `scripts/build-mpc.sh` (`build-mpc/Safian - VST - SynthyBoy/`) is packaged for release with `scripts/package-mpc-release.py`:

```bash
# Package both architecture archives (default):
scripts/package-mpc-release.py --plugin-dir "build-mpc/Safian - VST - SynthyBoy" --out dist/ --source-url "https://github.com/Safian/SynthyBoy" --arch all

# Or package for a specific architecture:
scripts/package-mpc-release.py --plugin-dir "build-mpc/Safian - VST - SynthyBoy" --out dist/ --source-url "https://github.com/Safian/SynthyBoy" --arch armv7
scripts/package-mpc-release.py --plugin-dir "build-mpc/Safian - VST - SynthyBoy" --out dist/ --source-url "https://github.com/Safian/SynthyBoy" --arch aarch64
```

The packager performs strict verification of the plugin folder before creating the archives:
* **Binaries**:
  - `armv7`: 32-bit ARM shared library (`VST/synthyboy-arm32.so`) must exist and match ELF header requirements (ELF 32-bit ARM, machine 40, little-endian). If present, GPU helper `VST/synthyboy-overlay-gpu-arm32` must be 32-bit ARM ELF and executable.
  - `aarch64`: 64-bit AArch64 shared library (`VST/synthyboy-arm64.so`) must exist and match ELF header requirements (ELF 64-bit AArch64, machine 183, little-endian). If present, GPU helper `VST/synthyboy-overlay-gpu-arm64` must be 64-bit AArch64 ELF and executable.
  - `--arch all`: requires both binaries.
* **Manifest & Metadata**: `plugin.json` must declare the matching binary (`file32` for armv7, `file64` for aarch64); the resulting archive includes a clean single-architecture `plugin.json` without dangling references. `version.xml` must exist.
* **Skins**: `Plugin Skins/` must contain `TUI.json` and `Q-Links.json`.
* **Presets**: `Content/presets/` must contain at least 100 `*.sbpreset` files.

On any verification failure, the script aborts with a descriptive error message and exit code 1.

The output consists of `SynthyBoy-MPC-<version>-armv7.zip` and `SynthyBoy-MPC-<version>-aarch64.zip` (or `SynthyBoy-MPC-<version>.zip` if `--arch combined` is specified for reproducing legacy packages), each holding exactly the single top-level plugin folder `Safian - VST - SynthyBoy/` with architecture-tailored `INSTALL.txt` and `LICENSE` included inside the folder. The archives are reproducible (sorted entries, fixed timestamps, macOS metadata files such as `._*` and `.DS_Store` excluded) and accompanied by `<zip>.sha256`.

`INSTALL.txt` explains installation to `VST2 Plugins` on an internal drive or external USB/SD storage, required MPC OS version (3.10.1 or later), enabling the enhancement switches under **Preferences → Enhancements (Beta)** (**Enable Enhancements** and **External Plugin Support**), how to load the instrument, removal, third-party plugin stability notes, the AGPL-3.0-or-later license and source repository URL, and the Akai / inMusic disclaimer.

## MPC Plugin Program Presets (.xpl)

The Akai MPC host stores instrument preset files in its proprietary Plugin Program (`.xpl`) format under the user preset directory (`MPC Documents/Plugin Presets/Instruments/Safian - VST - SynthyBoy/`) and within the plugin package folder itself (`Presets/NNN-<Category>-<Name>.xpl`).

On the MPC XL, programmatically generated `.xpl` preset files placed directly in the package directory (`<package>/Presets/`) have been verified to appear in and load from the host's **Sounds** browser without manual saving on the device.

### Package integration and generation

The build script `scripts/build-mpc.sh` supports packaging XPL presets via the `--xpl-presets <xl|k61|both|off>` option (default: `xl`):
```bash
scripts/build-mpc.sh --xpl-presets xl --dump-tool build-native/shell-mpc/dump_mpc_chunks
```
1. **Naming rule**: `NNN-<Category>-<Name>.xpl` (3-digit zero-padded category-order index, category name, FAT/exFAT-sanitized preset name, e.g. `000-Bass-Space Wobble Bass.xpl`), generated by `scripts/gen-xpl-presets.py`.
2. **Device paths**: Each `.xpl` specifies the target plugin binary path via `<PLUGIN file="...">`:
   - MPC XL: `/nvme/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm64.so`
   - MPC Key 61: `/sdcard/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm32.so`
3. **One shared set (default)**: `XPL_PATH_MODE="shared"`: the package holds ONE set in `<package>/Presets/`, written with the XL path; the Key 61 loads it as it is (checked on the Key 61: the list appears and the presets load). `XPL_PATH_MODE="device"` writes the XL set into `<package>/Presets/` and a separate Key 61 set into `<out>/Presets-k61/` next to the package folder.
4. **Path rewriting**: Installers can rewrite the `file=` attribute of all `.xpl` files in a folder to any target device path byte-for-byte (preserving CRLF line endings) using `scripts/mpc-fix-xpl-paths.py`:
   ```bash
   python3 scripts/mpc-fix-xpl-paths.py --dir /path/to/presets --path "/sdcard/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm32.so"
   ```
5. **Host indexing requirement**: The Akai host only indexes newly copied or updated presets in `Presets/` after restarting the MPC application or rebooting the hardware.

### Standalone .xpl generation

1. Build the native MPC shell and the chunk dumper helper:
   ```bash
   cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
       -DSYNTHYBOY_BUILD_PLUGIN=OFF \
       -DSYNTHYBOY_BUILD_MPC_SHELL=ON -DMPC_SDK_DIR="$MPC_SDK"
   cmake --build build-native --target dump_mpc_chunks synthyboy_mpc
   ```
2. Dump raw preset chunks from the built plugin:
   ```bash
   ./build-native/shell-mpc/dump_mpc_chunks ./build-native/shell-mpc/synthyboy_mpc.so content/presets /tmp/synthyboy_chunks
   ```
3. Generate package `.xpl` files using `gen-xpl-presets.py`:
   ```bash
   python3 scripts/gen-xpl-presets.py --chunks /tmp/synthyboy_chunks --device xl --out dist/Presets/
   ```
4. Alternatively, generate individual `.xpl` files using a host-saved template with `gen-mpc-xpl.py` (which defaults `--num-programs` to the computed preset count + 1):
   ```bash
   python3 scripts/gen-mpc-xpl.py \
       --chunks /tmp/synthyboy_chunks \
       --template /path/to/template.xpl \
       --plugin-path "/sdcard/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm32.so" \
       --out dist/mpc-presets/
   ```
5. Inspect or verify any generated file:
   ```bash
   python3 scripts/gen-mpc-xpl.py --check dist/mpc-presets/"000-Bass-Space Wobble Bass.xpl"
   ```

> [!NOTE]
> **Checked on the Key 61:** a set written with the XL path (`/nvme/VST2 Plugins/.../synthyboy-arm64.so`) shows its list and loads its presets on the Key 61, so the `file=` path of `<PLUGIN>` need not match the device; one shared set serves both (`XPL_PATH_MODE=shared`, the default). `scripts/mpc-fix-xpl-paths.py` stays as a tool. Not verified: the shared set on the MPC XL itself after a package install.


## Pins and secrets

All actions in `release.yml` are pinned by commit SHA (checkout, upload-artifact), everything else is the preinstalled `gh` CLI with the job's `GITHUB_TOKEN`
(`contents: write` only in the last job). The manifest and the scripts hold no secret.

[mpc-tasks]: ../TODO.md
