# MPC host scripting (Lua) and control surface

Fact sheet only. **Decision (owner side, 2026-11-21): we do NOT write the device rootfs or the
device scripts, and the Lua API is not a release path for the plugin.** It is recorded so that
nobody re-derives it and so that "can the plugin change the host tab / turn a Q-Link?" has an answer.

Scratchpad paths below are the session scratchpad
`/private/tmp/claude-501/-Users-szabi-Documents-Projects-Safian/9036973d-a78c-4289-9836-9c430de8deaf/scratchpad/plotter-info/`
(short: `$SP`); they are local to the owner's Mac and may be cleaned.

## Facts

- The host (MPC OS 3.10.1) embeds a **sol2 Lua scripting API** (Lua 5.4: search path
  `/usr/local/share/lua/5.4/?.lua`, sol2 type-name strings, `ScriptRunner::Impl::RunScriptNow(juce::File)`
  and `DriveAdded` run on a `ThreadPool`). `opened source: $SP/MPC.strings` (strings of the host binary;
  lines ~159002, ~7690-7711, ~123664).
- **API surface for Q-Links:** `mpc.controlSurface.qlinks` described as "16 Q-Link knobs, with Touch and
  Twist functionality"; members `GetNumQLinks`, `Touch` ("a single, instantaneous, on/off touch of the
  knob"), `Rotate` (rotate position by a delta -127..127, optional fine adjust), `SetPosition` (0-127),
  `GetPosition`, `SetValue` / `GetValue`, target name, `ValueString`. `opened source: $SP/MPC.strings`
  lines ~123975-123995 (the doc strings are the host's own). Stock use: `$SP/mpkMiniMkIV.lua` reads
  `mpc.controlSurface.qlinks[i]` and `.GetNumQLinks()` (lines 262, 437) and registers CC mappings through
  `...controlSurface.qlinks.AddInfiniteCCToQLink / RemoveInfiniteCCToQLink / SetQLinkFineAdjust`
  (lines 993-1093). Opened 2026-11-21.
- **Device scripts live in `/usr/share/Akai/SME0/Devices/Akai/*.lua` on the device**:
  HYPOTHESIS. The strings contain `/usr/share/Akai/SME0/...` (Firmware, UpdateImages) and the stock script
  `mpkMiniMkIV.lua` is a device script, but the exact `Devices/Akai` directory string was NOT found in
  `MPC.strings`; the path came from an earlier on-device look and is not re-checked here.
- **XL control surface = ALSA rawmidi `hw:0,0,1`, only sysex seen at idle**: HYPOTHESIS / not
  re-verified. The host links `snd_rawmidi_*` (`$SP/MPC.strings` line ~7117), but no log of the device
  node or of the idle sysex was found under `.hw-local/mpc-perf/xl-qlink-turn-2026-11-21/` (it holds only
  `pq.sh` and `shots/final-composed.png`) nor in `docs/`.
- **An `LD_PRELOAD` shim (hwctl) could inject MIDI, but the host ignored CC ticks**: the shim exists
  (`docs/MPC.md` line ~928, `docs/release/DEVICE-CHECKLIST.md` line 9: on the Key 61 `hwctl` / `stepq.py`
  inject knob increments; the XL has no knob injector there). The "host ignored CC ticks" result on the XL
  is HYPOTHESIS: no raw log was found in the evidence dirs searched.
- **No path found for a plugin to change the host tab or page.** `docs/release/DEVICE-CHECKLIST.md` line 86
  lists the `audioMaster` attempt as still `[OPEN]` (so "not found" = not tried / not found, not
  "proven impossible"). Skin action strings present in the host: `SubTab/`, `CurrentSubTab`,
  `PageIndex/Set/`, `PageIndex ` (`$SP/MPC.strings` lines ~129700-129706). `SubTab/<n>` is only reached
  by a host touch (HYPOTHESIS: from skin behaviour; no script/plugin route seen). `PageIndex/Set/1,N` is
  the Q-Link bank selector (HYPOTHESIS: matches the skin generator's use, not re-checked on the device).

## Pad Perform data on MPC OS 3.10.1.12

The gzip-compressed XPJ project contains JSON after its five-line ACVS header. In the saved project, `data.padPerformSettings` supplies the mode (`type`), root (`baseNote`), scale (`scaleType`), starting octave and bank behaviour. The project key lives separately at `data.key`. For example, the XL remix stores project key C Major and Pad Perform A# Harmonic Minor in Scales mode. The Key 61 fixture stores C Major in Scales mode. Both use “Start on root note”.

The selected track's program also stores `padNoteMap.noteForPad`. The XPJ fields alone do not establish the resolved live pad labels, the identity of the Pad Perform bank, or a notification on bank changes. In particular, the separate field `currentAssignablePadBank` needs independent identification. The Akai XL manual (v3.10.1, pp. 81–82) documents the factory Pad Perform window as displaying the current bank.

Opened sources: the XL and Key 61 project originals under `.hw-local/mpc-perf/xl-load-incident-7b5a79da/project-originals/`; the XL manual under `.hw-local/mpc-knowledge/manuals-extended/`. Earlier Key 61 hardware measurements correlated raw pad input with its resulting MIDI pitch (see `docs/MPC.md`, device level sweep). Those measurements required sending pad events.

## Rejected / out of scope

- Writing scripts or files under `/usr/share/Akai` (rootfs): owner decision, not done, not planned.
- Using `qlinks.Touch/Rotate/SetPosition` to drive knobs from the plugin: no route from a VST2 plugin to the
  host's Lua state was found; it would need a device script (see above).

## Open questions

- Does any `audioMaster` opcode switch tab / page (DEVICE-CHECKLIST line 86)?
- Re-measure and keep a raw log for the XL rawmidi node and the CC-tick injection.
