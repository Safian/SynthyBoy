# MpcKit Demo - the starter template for an Akai MPC VST2 plugin with plugin-drawn pages

You only assemble your VST from ready UI elements: you write the **parameters**, the **sound** and the **pages**; the kit does the drawing on the display plane, the page detection, the touch screen and mouse, the 16 Q-Links, standby and the package.

This folder is a complete, small VST2 instrument ("MpcKit Demo": a sine through a state variable filter, 9 parameters) with three example pages built only from ready components. Licence: **AGPL-3.0-or-later**, as the file headers state (the library under `shell-mpc/` is the same licence; keep the headers when you copy files).

Status of this template: see `docs/ui-components/TEMPLATE.md` ("Status"). In short: the plugin, the pages and the skin are complete; the overlay runtime that actually draws the pages on the MPC is linked in with the CMake option `-DMPCKIT_OVERLAY=ON` (a Linux cross build; migration step 8 of that document: written, never built or run); without it the plugin plays and shows the host's own widgets.

## The file map

| File | What it is | Do you edit it? |
| --- | --- | --- |
| `src/demo_params.h` | THE parameter table (key, name, range, unit, choices) and the presets | yes: your parameters |
| `src/demo_voice.h` | the sound (sine + filter + attack / release) | yes: replace by your engine |
| `src/pages/main_page.h` | page 1 "Main": 3 knobs + a toggle in one Q group, a number field and a radio list in another | yes: copy it for your pages |
| `src/pages/shape_page.h` | page 2 "Shape": a display (envelope graph) fed by the plugin, 4 knobs in 2 Q groups | yes |
| `src/pages/list_page.h` | page 3 "List": stepper with list pop-up, a fader, a pop-up of your own | yes |
| `src/demo_page_set.h` | the tabs: names, kinds, marker colours | yes, with `skin/overlay_layout.json` |
| `src/demo_instance.h` | one instance: parameter mirror, engine, Q proxies, the "Overlay Instance" bookkeeping | rarely |
| `src/demo_plugin.cpp` | the VST2 entry point and the display thread (plumbing) | rarely |
| `src/overlay_hook.h` | the only place that touches the overlay runtime (`mpckit::Overlay::attach` / `detach`, the page factory, the branding) | when you add a page |
| `src/log_compat.h` | the developer log (`MPCKIT_LOG`) with an empty fallback | no |
| `skin/params.json`, `skin/pages.json` | the parameters and the host widgets of each tab, for the skin generator | yes, in the same order as `demo_params.h` |
| `skin/overlay_layout.json` | the tabs with their marker colours (the skin AND the plugin follow it) | yes |
| `skin/extras/50_markers.py` | adds markers, instance picture, hidden widgets and Q-Link mapping to the skin | no |
| `tools/gen_skin.py`, `mpc-plugin.conf` | skin generation; what `scripts/build-mpc.sh --plugin template-mpc` builds and assembles | no |
| `package/plugin.json` | the manifest the MPC reads | yes: name, vendor, uid |
| `tests/pages_host.cpp` | host test of the table, the tab colours and the three pages | extend it |
| `CMakeLists.txt` | targets `mpckit_demo` (the module) and `mpckit_demo_pages_host` | add files |

The kit itself is `shell-mpc/kit/` (`param_source.h`, `instance.h`, `q_targets.h`, `page_set.h`, `content_source.h`, `binder.h`, `page_base.h`); the components are `shell-mpc/ui/` (see `shell-mpc/ui/AGENTS.md` and `docs/ui-components/README.md` for which component to use for what).

## What a page looks like (the whole idea)

```cpp
class MainPage : public mpckit::PageBase {
public:
    static constexpr int kElementPage = 1;
    MainPage() : level_("Level"), pan_("Pan") {
        bind(kIdLevel, level_, Type::Knob, "level");   // this component shows THIS parameter
        bind(kIdPan,   pan_,   Type::Knob, "pan");
        group({ &level_, &pan_ });                      // Q-Link 1, 2 while the player touches one of them
        relayout();
    }
protected:
    void layout(const ui::Rect& tab) override {         // one rectangle per component
        level_.place({ 40, tab.y + 40, 280, 240 });
        pan_.place({ 340, tab.y + 40, 280, 240 });
    }
private:
    ui::Knob level_, pan_;
};
```

To add a knob: a member, one `bind`, one `place`. The value shown, the drag, the double tap (default), the mouse wheel, the Q-Link, the automation report to the host and the write to the engine all come from the kit.

## Rename it into your own plugin

1. **Copy the folder** `template-mpc/` to `<yourplugin>-mpc/` (next to `shell-mpc/`) and rename the target in `CMakeLists.txt` (`mpckit_demo` -> `yourplugin`).
2. **Identity** - three places must agree: `package/plugin.json` (`name`, `manufacturer`, `uid`: exactly four ASCII characters), the four bytes of `effect->uniqueID` in `src/demo_plugin.cpp` (`'M','c','K','D'` -> your `uid`), the effect / product / vendor strings in the same file. The folder the MPC loads is `<manufacturer> - VST - <name>` (characters `" # @ , ; : < > * ^ | ? \ /` removed; `scripts/gen-mpc-manifest.py --folder-name` does it).
3. **Parameters** - edit `kParams` in `src/demo_params.h` (and the `ParamId` enum in the same order), then `skin/params.json` in the same order. A key is forever: it is stored in saved projects. Names the host shows are at most 31 bytes.
4. **Sound** - replace `src/demo_voice.h`; read the values from the parameter mirror as `DemoInstance::process` does.
5. **Pages** - copy a page file, change the members, the binds, the groups and the layout. Register the page: `src/demo_page_set.h` (a `PageDef`), `skin/overlay_layout.json` (a new view with a new pair of marker colours: no other tab, also of other plugins in the same project, may be within 4 levels per channel of them), `skin/pages.json` (the tab's host widgets), and the factory case in `demo_plugin.cpp` (after step 8: in the page factory).
6. **Colours** - markers must not look like the markers of another plugin that may be loaded in the same project (SynthyBoy uses `10..c6 3a 2c` and `3a 10..c6 64`). The template uses `e0a020 .. e0c020` / `20e0a0 .. 20e0c0`. `mpckit::validatePages` (`shell-mpc/kit/page_set.h`) checks your table; `tests/pages_host.cpp` runs it.

## Build and package

Requirements: the Akai MPC plugin SDK (`MPC_SDK` = its folder, which contains `framework/mpc_vst2_abi.h` and `tools/gen_ui.py`; it is not part of this repository), Docker for the cross build (the repository's Debian 12 image: glibc 2.36, below the 2.39 of the MPC OS), Python 3.

1. **Build and package in one command** (migration step 12 of `docs/ui-components/TEMPLATE.md`): `MPC_SDK=<SDK> scripts/build-mpc.sh --plugin template-mpc --out build-template`. It builds the module (and, with the overlay, the GPU helper) in the Debian 12 container for both architectures (`--arch arm32|arm64|all`), checks the binaries, generates the skin (`tools/gen_skin.py`: the repository's `gen-mpc-ui.py` and the SDK's `gen_ui.py`, then `skin/extras/50_markers.py`), writes `plugin.json` (`package/plugin.json`, `file32` / `file64` for the binaries that were built) and `version.xml`, runs the SDK validator and leaves `build-template/Example - VST - MpcKit Demo/`. What it builds is in `mpc-plugin.conf` (target, module, binary names, helper, manifest, skin script); the overlay is on there (`CMAKE_ARGS=-DMPCKIT_OVERLAY=ON`); take it out for a plugin without plugin-drawn pages. Heavy builds go through the build slot: `bash .hw-local/tools/buildslot.sh scripts/build-mpc.sh ...`.
2. **By hand** (no Docker, a Debian 12 Linux host): `cmake -S template-mpc -B build-template/tree/arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake -DMPC_SDK_DIR=$MPC_SDK -DMPCKIT_OVERLAY=ON` (`armv7-linux-gnueabihf.cmake` for the 32-bit MPCs, e.g. the Key 61) and `cmake --build ... --target mpckit_demo`; strip the module; `python3 template-mpc/tools/gen_skin.py --out <package folder> --sdk $MPC_SDK` makes the skin; put the module as `VST/mpckitdemo-arm64.so` and the helper as `VST/mpckit-demo-overlay-gpu-arm64` into the folder and `plugin.json` / `version.xml` there (the files of `package/`; `scripts/gen-mpc-manifest.py --package-manifest template-mpc/package/plugin.json --plugin-dir <folder>` writes both).
3. **Add the template to the CMake build of the repository** (only for the host test): one line in the top-level `CMakeLists.txt` after `add_subdirectory(shell-mpc)` (inside `if(SYNTHYBOY_BUILD_MPC_SHELL)`): `add_subdirectory(template-mpc)`.
4. **Validate**: `python3 $MPC_SDK/tools/validate.py "build-template/Example - VST - MpcKit Demo"`. A one-architecture package gets exactly one finding (the missing `file32` or `file64`), as `scripts/build-mpc.sh` explains.
5. **Install**: copy the folder into the `VST2 Plugins` folder of the MPC (internal storage or the SD card / a USB drive, see the MPC's manual: the host scans folders named `<Vendor> - VST - <Name>` there). Restart the MPC application; add the plugin to a track.
6. **Host test** (a computer, no MPC): `cmake --build <dir> --target mpckit_demo_pages_host && <dir>/template-mpc/mpckit_demo_pages_host`.

## What the host requires

- The folder layout above, a `plugin.json` with `name`, `manufacturer`, `uid`, `format: "vst2"`, `kind`, `numInputs`, `numOutputs`, `file32` and / or `file64` (the binary the CPU needs: 32-bit ARMv7 hard-float or AArch64) and `version`; `version.xml` next to it.
- A skin (`Plugin Skins/TUI.json`, `Q-Links.json`, the images) if the plugin has pages: the tabs are what the player sees on the function keys; at most 32 tabs, at most 8 sub-tabs per function key.
- The parameter list in the order of `skin/params.json`, then `Overlay Instance`, then `Q 1` .. `Q 16` (`demo_plugin.cpp` does it; the skin binds to these numbers).

## Developer log

Log through `MPCKIT_LOG (category, level, fmt, ...)` and `MPCKIT_SCOPE (category, name)` (`shell-mpc/kit/log.h`, `docs/ui-components/LOGGING.md`); never `printf`. The release build strips them; a debug build switches categories per control file on the device. The template already logs the start-up (`MPCKIT_SCOPE`), one line per page built and per parameter changed by a page at debug level. `src/log_compat.h` makes the template compile where `log.h` is absent.

## Known limits of the template (see TEMPLATE.md "Open risks")

The page detector needs the host's skin markers to be on screen untouched; the host's parameter count and the Q proxies are fixed at plugin load; the 32-bit and 64-bit builds are different binaries; tested hosts: MPC OS 3.10.1 on the MPC Key 61 and the MPC XL only.
