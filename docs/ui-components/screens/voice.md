# Voice screen

**Tab:** Voice (`voice`, function key Master, third sub-tab): one tab body. **Picture:** `../../screens/voice.jpg`.

## What it shows

Four blocks: SEQUENCE (`voiceNativeBlock_sequence`, 8 66 1264 296: the row list `voiceNativeRowList` (PAN, DETUNE, CUTOFF, ENV, MOD rows), the step buttons `voiceStepButtons`, the step knobs `voiceNativeStepLevel` (8 steps per row), the column values, the step value buttons, the selected-value strip, with the step selector, LENGTH and step value fields in the header), RANDOM (`..._random`: PAN, DETUNE, CUTOFF, ENV dials and the 10X DETUNE switch), SCALING (`..._scaling`: ENV TIME, LFO TIME dials and the LFO MODE choice) and TARGETS (`..._targets`: the oscillator scope `voiceNativeTargets`: OSC A, B, C, SUB). The sounding-step indicators are a live display drawn by the plugin ([../voice-displays.md](../voice-displays.md)).

## Header

The header row is the Header component (`Voice Header`, form `voiceHeader`; [../header.md](../header.md)): the title `VOICE CONTROL`, the step selector (`voiceHeaderStep`), LENGTH (`voiceHeaderLength`) and the selected step value (`voiceHeaderStepValue`).

## Definitions

`skin/defs_voice.json` (20; `voiceNativeBlockKeyline` merged into `masterNativeBlockKeyline` in `defs_master.json`, commit `65eb91ab`).

## Data

Blocks of `shell-mpc/native_ui_params.h`: `kVoiceSequence` (`kVoiceSequenceCount` handles; the sequence selection the bridge reports, `native_voice_controls.h`). The step rows and the random and scaling controls bind engine parameters directly.

## Tab exit and page loading

On leaving the tab (`VoiceUi::leftTab`, `shell-mpc/overlay/voice_ui.h:230`), `inst.setNativeVoiceState({})` disarms writes while `nativeVoice.displayCell()` (`native_voice_controls.h:41-59`) keeps the read-only selection and row cache readable for host paint. On the display thread, `nativeVoicePoll` (`synthyboy_mpc.cpp:2740-2763`) forces reports on row text formatting changes so labels update cleanly.

## Q groups

STEP [STEP, ROW, LENGTH, VALUE], RANDOM [PAN, DETUNE, CUTOFF, ENV], SCALING [10x DETUNE, ENV TIME, LFO TIME, LFO MODE], TARGETS [OSC A, B, C, SUB]; NOISE is touch only.

## C++ owners

`shell-mpc/overlay/voice_ui.h`; `shell-mpc/ui/voice_native_layout.h`, `voice_grid.h`; `shell-mpc/native_voice_controls.h`. Retired unreferenced symbols (`8525ae50`): dead `VoiceUi` accessors, name helpers, and constants.
