# Master displays and native readouts

The overlay draws the glide slope with `PortamentoCurveDisplay`. On Tuning, the native scale keyboard binds twelve radio keys to `master.scale.key` and uses `master.scale.type` to highlight scale notes.

`PeakMeterReadout`, `VoiceMeterReadout` and `ClipReadout` are plain state/geometry adapters for host-native widgets. They have no GPU drawing or touch methods. Peaks retain independent L/R hold levels; `updateHold` falls by 0.5 per second. Voice state contains active voice count and polyphony. Clip state is supplied by `IInstanceView::pollMasterClip`; its real `ClipHold` latch remains in `ui/clip_hold.h` and clears through the native host callback.

`MasterUi` publishes those values with `setNativeMasterOutput`. The native callback and readout checks live in `shell-mpc/overlay/tests/mastervoice_ui_host.cpp`; the real VST engine-index path is exercised in `shell-mpc/tests/play_env_lfo_host.cpp`.

The current screen image is [master](../screens/master.jpg).
