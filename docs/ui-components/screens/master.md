# Master and Tuning screens

Two tab bodies of the Master function key (third sub-tab: [voice.md](voice.md)). The sub-tab Master is the MAIN side and Tuning the TUNING & MPE side of the page model (view `master`, index 1 and `tuning`, index 2; no side buttons). **Picture:** `../../screens/master.jpg` (Master); Tuning has none.

## Master (`master`)

**Header:** the Header component's title `MASTER` (`Master Header`, form `masterHeader`; [../header.md](../header.md)).

Four blocks: OUTPUT (`masterNativeBlock_output`, 8 66 756 252: VOLUME, STEREO SPREAD, STEREO ROUTING, voice override selector, NO SMOOTHING, OVERSAMPLE 4X, output limiter, clip indicator buttons, peak meters `masterNativeOutputMeters`, QUALITY caption), PITCH (`..._pitch`: TUNE, TRANSPOSE, BEND RANGE up / down or the linked single range, LINK), VOICES (`..._voices`: POLYPHONY, LEGATO, POLY LEGATO, PHASE LEGATO, VELOCITY TRACK, SAME NOTE, STEAL PRIORITY, PAD PRESS with PAD amount, voice meter `masterNativeVoiceMeter`), GLIDE (`..._glide`: TIME, SLOPE, ALWAYS, SCALED, curve frame `masterGlideFrame`). Hidden controls: BEND DOWN while LINK is on (the linked range replaces the pair), the PAD amount while PAD PRESS is off. Definitions: `skin/defs_master.json` (32).

## Tuning (`tuning`)

**Header:** the Header component (`Tuning Header`, form `tuningHeader`): the title `TUNING & MPE` and the scale-input toggle (cell `tuningHeaderScale`).

Three blocks: SCALE (`masterNativeBlock_scale`: type, key, mode selectors and the scale input button), TUNING (A tuning, scale selector, MTS-ESP, MTS NOTE-ON), MPE (MPE on, bend range, XYZ to Macros, Y to wheel, BEND to X, Y bipolar: gated off while MPE is off, with `NativeInactive` twins). Definitions: `skin/defs_tuning.json` (28).

## Data

Blocks of `shell-mpc/native_ui_params.h`: `kMasterOutput` (`master::kOutputFields`), `kMasterScaleInput` (`Parameter 3657`), and legacy `kMasterView` (`master::View`: Hidden, Main, Tuning; `native_master_controls.h`). Both `Master` and `Tuning` tabs bind their root controls directly to always-valid engine, shell, meter, and scale-input handles without a cross-tab `kMasterView` (`Parameter 2879`) gate (`Tuning Header` `HdrView` binds to the root frame gate `RangedEnabling/0/18/33/Parameter 2675`). On the plugin side, `MasterUi` still calls `setNativeMasterView` / `leaveNativeMasterView` so `synthyboy_mpc.cpp` (`nativeMasterView`) gates `Limiter`/`ClearClip`/`scaleInput` writes and `PeakL..VoiceCount` output updates only when the corresponding tab is active.

## Q groups

Master side: OUTPUT, PITCH, VOICES, GLIDE (VOICE OPTS and LINK are touch only). Tuning side: SCALE, TUNING, MPE, MPE Y. Hidden controls are no members.

## C++ owners

`shell-mpc/overlay/master_ui.h` (rows, nine Q groups, rules, native bridge, meters); `shell-mpc/ui/master_native_layout.h`, `shell-mpc/native_master_controls.h`.
