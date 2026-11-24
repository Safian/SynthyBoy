# Master displays and native readouts

`shell-mpc/ui/voice_master.h` keeps two overlay displays: `PortamentoCurveDisplay` draws the glide slope; `MiniKeyboardDisplay` draws the scale and reports a touched note without storing parameter truth. Their minimum sizes are 100 x 60 and 240 x 90. The keyboard state contains `scaleType`, `scaleKey` and `scaleMode`.

`PeakMeterReadout`, `VoiceMeterReadout` and `ClipReadout` are plain state/geometry adapters for host-native widgets. They have no GPU drawing or touch methods. Peaks retain independent L/R hold levels; `updateHold` falls by 0.5 per second. Voice state contains active voice count and polyphony. Clip state is supplied by `IInstanceView::pollMasterClip`; its real `ClipHold` latch remains in `ui/clip_hold.h` and clears through the native host callback.

`MasterUi` publishes those values with `setNativeMasterOutput`. Only the glide curve and scale keyboard remain overlay children. The native callback and readout checks live in `shell-mpc/overlay/tests/mastervoice_ui_host.cpp`; the real VST engine-index path is exercised in `shell-mpc/tests/play_env_lfo_host.cpp`.

The current screen image is [master](../screens/master.jpg).
