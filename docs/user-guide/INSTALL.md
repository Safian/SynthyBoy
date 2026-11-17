# SynthyBoy for Akai MPC — Installation and First Steps

This guide covers how to install, configure, and get started with the SynthyBoy standalone package on Akai MPC hardware running MPC OS 3.10.1 or later.

> [!IMPORTANT]
> **Third-Party Plugin Notice & Stability Warning**  
> SynthyBoy is an independent open-source synthesizer loaded via Akai's experimental external plugin support. As noted in the Akai user guides (*MPC Standalone OS User Guide v3.10.1*, page 9; *MPC Live III, MPC XL User Guide v3.10.1*, page 7):  
> *"These plugins are not tested or supported by inMusic. A faulty plugin can freeze or crash this product, use more processing power than it has available, or cause audio glitches. Save your work often when this is enabled."*

---

## 1. What the Package Contains

When you download and extract the SynthyBoy MPC release zip, you receive a single root folder named exactly:

```text
Safian - VST - SynthyBoy
```

This self-contained directory contains all binaries, interfaces, and content necessary to run on both 32-bit and 64-bit standalone MPC hardware. Inside this folder, you will find:

- `plugin.json` — The MPC plugin manifest file. It registers the plugin with the MPC OS host and specifies the entry point shared library for each CPU architecture: `file32` points to `VST/synthyboy-arm32.so` and `file64` points to `VST/synthyboy-arm64.so`.
- `version.xml` — The XML version manifest declaring the package content version and the Touch User Interface skin version (`uiversion`). MPC OS validates this file during scanning.
- `VST/` — The directory holding the executable shared objects and background helpers:
  - `synthyboy-arm32.so`: The stripped 32-bit ARMv7 hard-float shared library (compiled with GCC for ARMv7-A with NEON, glibc 2.36 compatibility, exporting only `VSTPluginMain`). Loaded by 32-bit (Generation 1) MPC units.
  - `synthyboy-arm64.so`: The stripped 64-bit AArch64 shared library (compiled with GCC for ARMv8-A, glibc 2.36 compatibility, exporting only `VSTPluginMain`). Loaded by 64-bit (Generation 2) MPC units.
  - `synthyboy-overlay-gpu-arm32` and `synthyboy-overlay-gpu-arm64` *(helper programs)*: Stripped helper executables used by builds with the experimental direct GPU overlay enabled (`SYNTHYBOY_MPC_OVERLAY`). The plugin binary spawns the appropriate helper process to manage hardware DRM plane drawing and EGL rendering without blocking the real-time audio threads or primary display plane.
- `Plugin Skins/` — The complete Touch User Interface (TUI) and hardware controller mapping definitions:
  - `TUI.json`: Defines the screen tabs, sub-tabs, widgets, visual styles, and layout elements shown on the MPC touchscreen.
  - `Q-Links.json`: Maps the hardware capacitive Q-Link rotary encoders to plugin parameters across function banks.
  - `browser_images/soundsmode.png`: An exact 270 × 110 pixel PNG graphic tile displayed on the instrument selection screen in **Sounds > INSTRUMENTS** mode.
- `Content/presets/` — The collection of 112 factory presets arranged in categories (`Bass/`, `Lead/`, `Pad/`, `Pluck/`, `Keys/`, `FX/`). The plugin resolves these files relative to its `plugin.json` location and exposes them as VST programs.
- `Presets/` *(if present in the release)* — Standalone MPC preset files (`.xpl`) that allow presets to be loaded directly through the MPC OS Sounds browser.

---

## 2. Supported Hardware

SynthyBoy packages both 32-bit and 64-bit binaries in one folder. The MPC OS dynamic loader automatically selects the binary matching the hardware architecture:

- **Generation 1 (32-bit ARMv7 hard-float):**  
  Tested and verified on the **MPC Key 61** (running MPC OS 3.10.1.12).  
  *Untested models:* MPC Live, MPC Live II, MPC One, MPC One+, MPC X, MPC X SE, and MPC Key 37 share the same 32-bit ARMv7 CPU platform, but have **not** been verified on hardware by the author.
- **Generation 2 (64-bit AArch64):**  
  Tested and verified on the **MPC XL** (running MPC OS 3.10.1.12).  
  *Untested models:* MPC Live III shares the 64-bit AArch64 platform, but has **not** been verified on hardware by the author.

---

## 3. Where to Copy the Package

To make the plugin visible to MPC OS, copy the entire folder `Safian - VST - SynthyBoy` into a folder named exactly `VST2 Plugins` at the root of your internal drive or any connected external storage:

```text
<Drive Root>/
└── VST2 Plugins/
    └── Safian - VST - SynthyBoy/
        ├── plugin.json
        ├── version.xml
        ├── VST/
        ├── Plugin Skins/
        └── Content/
```

### Supported Storage Locations

According to the official Akai user guides (*MPC Standalone OS User Guide v3.10.1*, page 9; *MPC Live III, MPC XL User Guide v3.10.1*, page 7):
- **Internal Drive:** The internal SATA/NVMe SSD or internal user storage (e.g. `/nvme/VST2 Plugins/` or `/internal/VST2 Plugins/`).
- **External Attached Drive:** The root of any attached SD card (e.g. `/sdcard/VST2 Plugins/`) or USB flash drive/hard drive.

> [!NOTE]
> **Drive Path Observation**  
> As verified in developer hardware testing (`docs/MPC.md`): on an MPC Key 61, the plugin folder was confirmed working under `/sdcard/VST2 Plugins/Safian - VST - SynthyBoy/`. On an MPC XL, it was confirmed under `/nvme/VST2 Plugins/Safian - VST - SynthyBoy/`.

---

## 4. Enabling External Plugins in MPC OS

External third-party VST2 plugins remain completely hidden until experimental external plugin support is explicitly switched on in system preferences.

### Exact Configuration Steps

*(Citations: MPC Standalone OS User Guide v3.10.1, page 9; MPC Live III, MPC XL User Guide v3.10.1, page 7)*

1. Press the hardware **Menu** button to open the Mode Menu.
2. Tap the **Preferences** (gear) icon at the bottom of the screen.  
   *(On the MPC XL, you can alternatively press and hold **Shift** and press the **Menu / Prefs** button).*
3. In the Preferences screen, scroll down the left sidebar and select the **Enhancements** tab.
4. Tap **Enable Enhancements** to set it to **On**.
5. Once enhancements are enabled, scroll through the list to find **External Plugin Support** and toggle it to **On**.

> [!WARNING]
> Both switches (**Enable Enhancements** AND **External Plugin Support**) must be turned **On**. If either switch is off, SynthyBoy will not appear anywhere in the plugin browser.

### Restart Required (Observed Host Behavior)

The Akai manuals do not mention scan timing for newly placed files. However, direct device measurements on both the MPC Key 61 and MPC XL (`docs/MPC.md`) revealed:
- **A newly copied plugin appears only after the MPC application is restarted.**
- After copying `Safian - VST - SynthyBoy` to your drive, reboot your MPC or restart the MPC software.

---

## 5. Loading and Using SynthyBoy

Once external plugin support is enabled and the unit has been restarted, follow these steps to load the instrument.

### Loading on a Plugin Track

*(Citations: MPC Standalone OS User Guide v3.10.1, pages 16, 50, 119; MPC Live III, MPC XL User Guide v3.10.1, pages 15, 51, 120)*

1. In **Main Mode**, tap **+ New Track** at the bottom-left corner of the touchscreen (or select an existing track).
2. In the track type selector window, choose **Plugin**.
3. Tap the **Plugin** field to open the plugin selection window:
   - SynthyBoy is listed under the **External Plugins** category.
   - You can tap **Type** (categorized as *Synth* / *Instrument*) or **Manufacturer** (listed under *Safian*) at the bottom of the screen to filter your plugins.
   - Select **SynthyBoy** and tap **Select** to confirm.
4. Tap the plugin preview icon or tap **Edit Instrument** (in Sounds Mode) to enter Track Edit Mode and view the plugin interface.

### Selecting a Preset

- In Main Mode or Track View, tap the **Preset** field on the plugin track to browse and select from the 112 factory presets (VST programs).
- If standalone `.xpl` presets are installed in `Presets/`, presets can also be browsed through the **Sounds** mode browser.

### Saving a Project

*(Citations: MPC Standalone OS User Guide v3.10.1, page 67; MPC Live III, MPC XL User Guide v3.10.1, page 69)*

1. Press **Menu** and tap **Save** at the bottom of the screen (or press the hardware **Save** button / **Shift + Save**).
2. In the Save Window, tap **Project** (to overwrite the current project) or **Project As** (to save as a new file).
3. The MPC saves the entire plugin state (all parameters, modulation assignments, and current oscillator modes) inside the project file (`.xpj`).

> [!TIP]
> **Q-Link Snapshot Behavior in Saved Projects**  
> As documented in `docs/MPC.md`: When a plugin is inserted onto a track, the MPC host saves a snapshot of the current `Q-Links.json` mapping inside that track's data in the `.xpj` project. If you subsequently update SynthyBoy to a version with a new skin/knob layout, existing projects retain their old Q-Link mapping on existing tracks. To apply the newest Q-Link layout to an existing project, remove and re-insert SynthyBoy on the track.

### Saving a Plugin Program (Preset Export)

*(Citations: MPC Standalone OS User Guide v3.10.1, page 67; MPC Live III, MPC XL User Guide v3.10.1, page 69)*

To export your custom sound design as an individual preset file reusable across different projects:
1. Ensure your SynthyBoy plugin track is currently selected.
2. Open the Save Window (**Menu** > **Save**).
3. Tap **Plugin Program**.
4. Enter a name using the on-screen keyboard and choose the destination folder on your drive to save an `.xpl` preset file.

---

## 6. Updating to a New Version

When updating SynthyBoy:

1. Connect your drive to your computer or switch the MPC to USB Storage / Controller mode.
2. Replace the existing `Safian - VST - SynthyBoy` folder inside `VST2 Plugins/` with the new folder.
3. Eject/disconnect cleanly and **restart the MPC application or reboot the hardware**.

### Why You Must Never Overwrite a Running Plugin In-Place

> [!CAUTION]
> **Do NOT copy or overwrite plugin files while the plugin is loaded in a running session!**  
> Under Linux (MPC OS), shared libraries (`.so`) are demand-paged directly from disk into process memory using memory-mapped files (`mmap`). If you overwrite or truncate a binary file on disk while the MPC application has it loaded:
> - The Linux kernel's page cache for the file is invalidated or disrupted.
> - The next time the audio engine or UI thread executes code from an unmapped page or dispatches a function, the kernel raises a fatal `SIGBUS` or `SIGSEGV` signal.
> - This causes the entire MPC host application process to crash instantly, losing any unsaved project data!
> - Furthermore, replacing `Plugin Skins` while the host is running leads to visual corruptions because decoded skin assets are cached in `/var/tmp/filmstrips`.
> 
> Always update with the project closed, the plugin removed from active tracks, or the unit completely restarted after file transfer.

---

## 7. Removing SynthyBoy

To uninstall SynthyBoy:
1. Delete the folder `Safian - VST - SynthyBoy` from the `VST2 Plugins` directory on your drive.
2. If an existing project referencing SynthyBoy is opened, the MPC OS host will open the project safely, skipping the missing plugin track or displaying a standard missing plugin notification without data loss.

---

## 8. MPC Key 61 Recommendation: Disable "Show Q-Link Status When Touched"

If you are using the **MPC Key 61**, it is strongly recommended to turn off the Q-Link touch pop-up.

### How to Disable It

*(Citation: MPC Standalone OS User Guide v3.10.1, page 65)*

1. Go to **Preferences > General**.
2. Locate **Show Q-Link Status When Touched**.
3. Toggle it to **Off**.  
   *(Alternatively, press and hold the hardware **Q-Link** button and tap **Q-Link Status** in the window that appears).*

### Technical Reason

The hardware Q-Link knobs on the Key 61 feature capacitive touch sensors. By default, touching any knob triggers the host firmware to draw a full-height status panel along the right edge of the display (screen coordinates x = 1024 to 1280), showing current assignments.

Because the MPC host draws this panel into an opaque primary display plane directly over the plugin's graphical area:
- The plugin must continuously run an optical panel keying routine (scanning pixel strips and clipping the plugin's hardware DRM overlay plane) so the host pop-up can show through without visual tearing or blinking.
- When **Show Q-Link Status When Touched** is set to **Off**, the host records `ShowQLinksStatusOverlay="0"` in `/data/Settings/MPC/MPC.settings`.
- SynthyBoy detects this setting and deactivates its panel-keying detector completely. This eliminates all comparison CPU overhead (zero background cost) and ensures that plugin graphics on the right edge remain completely stable and unclipped.

---

## 9. Generation 1 vs. Generation 2 Quality Profiles

SynthyBoy automatically detects the hardware model from the system device tree (`/proc/device-tree/model`) and assigns an optimized DSP performance profile:

> On **Generation 1** hardware (MPC Key 61 and unbenchmarked 32-bit units), the engine runs the conservative `low` quality profile: polyphony is capped at 8 voices, oscillator unison at 3 voices, warp modes use 2x oversampling, spectral morphing runs at half rate, fast math approximations are used, and 5 heavy effects (Reverb, Flanger, Hyper/Dimension, Bode frequency shifter, and Convolve) are bypassed to guarantee that processing fits comfortably within real-time audio block deadlines (2.902 ms per 128 frames at 44.1 kHz).  
> On **Generation 2** hardware (MPC XL and 64-bit octa-core units), the engine enables the `high,voices=16,control=economy` profile: providing 16 voices of polyphony, up to 16 unison voices per oscillator, all 16 effects fully active, 4x warp oversampling, full-rate morphing, and exact math calculations, running smoothly with ample headroom. Any unrecognised hardware model safely defaults to the `low` profile.

---

## 10. Troubleshooting

All troubleshooting points below are based strictly on documented behavior and root causes identified in `docs/MPC.md`:

| Problem | Documented Cause & Resolution |
|---|---|
| **Plugin not listed in plugin browser** | 1. **Firmware outdated**: Requires MPC OS version 3.10.1 or later.<br>2. **Enhancements disabled**: In **Preferences > Enhancements**, ensure **both** "Enable Enhancements" and "External Plugin Support" are set to **On**.<br>3. **Incorrect folder location**: Folder must be named exactly `Safian - VST - SynthyBoy` and placed directly inside `VST2 Plugins` at the drive root.<br>4. **Missing manifest**: Check that `plugin.json` is present in the plugin directory.<br>5. **MPC not restarted**: The host only indexes external plugins upon application start. Reboot the MPC. |
| **No sound output** | 1. **Polyphony cap reached**: On Gen 1, polyphony is capped at 8 voices. Long release times or complex chords can cause voice stealing (stolen voices fade in 3 ms to prevent CPU overruns).<br>2. **Bypassed effects on Gen 1**: If a preset has its mix set 100% wet through Reverb, Flanger, Hyper/Dimension, Bode, or Convolve, these effects are bypassed under the `low` profile, resulting in silence for that processing path.<br>3. **Track / Monitor routing**: Ensure the plugin track's Monitor setting is set to **Auto** or **In** and the track is record-armed (*SEQ REC ARM* enabled). |
| **Drawn pages / visual overlay do not appear** | 1. **Standby screen dimming**: When the MPC dims the screen in standby mode, the hardware DRM overlay plane is disabled until a wake-up touch occurs.<br>2. **Modal dialog open**: Opening a full-screen selector or system menu temporarily suppresses the plugin's drawn layer until the menu is closed.<br>3. **Stale Q-Link mappings from older projects**: Projects created with earlier skin layouts retain their track snapshot; load a new project or re-insert the plugin on the track.<br>4. **Helper initialization**: In experimental overlay builds, the GPU helper starts up in ~70–120 ms. If it fails to appear, restart the unit. |
| **Factory presets missing** | 1. **Missing Content folder**: SynthyBoy searches for presets relative to its binary path by finding `plugin.json` and locating `Content/presets/`. If the `Content/` folder was not copied, presets will be empty.<br>2. **Browsing via Sounds mode**: Factory presets are exposed as VST programs via the track's **Preset** field. Browsing through the standalone MPC Sounds screen requires `.xpl` files in `Presets/`. |

---

## 11. Licence and Support

### Licence

Copyright (C) 2026 Safian Szabolcs.

SynthyBoy is free software released under the terms of the **GNU Affero General Public License, version 3 or (at your option) any later version** (`AGPL-3.0-or-later`).
- For full license details, see the [LICENSE](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-g13-install-doc/LICENSE) file in the repository.
- Third-party dependency licenses and notices are documented in [THIRD_PARTY.md](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-g13-install-doc/THIRD_PARTY.md) and [NOTICE](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-g13-install-doc/NOTICE).
- *Disclaimer:* SynthyBoy is an independent development and is not affiliated with, sponsored by, or endorsed by inMusic Brands or Akai Professional.

### Reporting Issues

Please report any bugs, performance issues, or feature requests through the official issue tracker:
- **GitHub Issues:** [https://github.com/Safian/SynthyBoy/issues](https://github.com/Safian/SynthyBoy/issues)

*(Please do not send support requests to private personal addresses).*

---

## 12. Items to Confirm on the Device

The following items are derived from developer measurements in `docs/MPC.md`, test fixtures, or host source analysis, and could not be verified directly from Akai user manuals:

1. **Other MPC Hardware Models**: Operation and thermal stability on untested hardware models (MPC Live I/II, MPC One/One+, MPC X/X SE, MPC Key 37, MPC Live III) have not been tested and must be confirmed on real units.
2. **Settings Flush Timing**: Whether toggling "Show Q-Link Status When Touched" in Preferences immediately flushes `/data/Settings/MPC/MPC.settings` to storage or waits until application exit/shutdown must be confirmed on the device.
3. **Sounds Browser Preset Navigation**: Whether tapping the graphic artwork tile in Sounds mode correctly lists `.xpl` presets from `Presets/` on all firmware revisions of MPC OS 3.10.1+.
4. **Behavior on Future Firmware Revisions**: Akai denotes "External Plugin Support" as an experimental enhancement; future firmware revisions beyond 3.10.1.12 may alter folder scanning, memory limits, or DRM plane allocation.
