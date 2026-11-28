# Master displays and native readouts

The overlay draws the glide slope itself (`MasterUi::drawGlideCurve`, `shell-mpc/overlay/master_ui.h`).

The output peaks, the voice count and the clip state are plain state in `MasterUi`, shown by host-native widgets. Peaks keep independent L/R hold levels; `updateHold` falls by 0.5 per second. The clip state comes from `IInstanceView::pollMasterClip`; its `ClipHold` latch lives in `ui/clip_hold.h` and clears through the native host callback.

`MasterUi` publishes those values with `setNativeMasterOutput`.

The current screen image is [master](../screens/master.jpg).
