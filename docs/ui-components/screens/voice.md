# Voice screen

**Tab:** Voice (`voice`, function key Master, third sub-tab): one tab body. **Picture:** `../../screens/voice.jpg`.

## What it shows

Four blocks: SEQUENCE (`voiceNativeBlock_sequence`, 8 66 1264 296: the row list `voiceNativeRowList` (PAN, DETUNE, CUTOFF, ENV, MOD rows), the step buttons `voiceStepButtons`, the step knobs `voiceNativeStepLevel` (8 steps per row, 16 for the MOD rows), the column values, the step value buttons, the selected-value strip, with the step selector, LENGTH and step value fields in the header), RANDOM (`..._random`: PAN, DETUNE, CUTOFF, ENV dials and the 10X DETUNE switch), SCALING (`..._scaling`: ENV TIME, LFO TIME dials and the LFO MODE choice) and TARGETS (`..._targets`: the oscillator scope `voiceNativeTargets`: OSC A, B, C, SUB). The sounding-step indicators are a live display drawn by the plugin ([../voice-displays.md](../voice-displays.md)).

## Header

The header row is the Header component (`Voice Header`, form `voiceHeader`; [../header.md](../header.md)): the title `VOICE CONTROL`, the step selector (`voiceHeaderStep`), LENGTH (`voiceHeaderLength`) and the selected step value (`voiceHeaderStepValue`).

## Definitions

`skin/defs_voice.json` (21).

## Data

Blocks of `shell-mpc/native_ui_params.h`: `kVoiceSequence` (`kVoiceSequenceCount` handles; the sequence selection the bridge reports, `native_voice_controls.h`), `kNativeText`. The step rows and the random and scaling controls bind engine parameters directly.

## Q groups

STEP [STEP, ROW, LENGTH, VALUE], RANDOM [PAN, DETUNE, CUTOFF, ENV], SCALING [10x DETUNE, ENV TIME, LFO TIME, LFO MODE], TARGETS [OSC A, B, C, SUB]; NOISE is touch only.

## C++ owners

`shell-mpc/overlay/voice_ui.h`; `shell-mpc/ui/voice_native_layout.h`, `voice_grid.h`; `shell-mpc/native_voice_controls.h`.
