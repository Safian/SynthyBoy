# Mixer screen

**Tab:** Mix (`mixer`, function key Mix): one tab body of ten channel strips; no peek tab bar. **Picture:** `../../screens/mixer.jpg`.

## What it shows

Ten strips left to right: SUB, A, B, C, NOISE (`mixerStripSourceFrame`), F1, F2 (`mixerStripFilterFrame`), BUS 1, BUS 2 (`mixerStripBusFrame`, level fader 470 high) and MAIN (`mixerStripMasterFrame`). Each strip (115 x 606) has: the name label, solo and mute buttons with their visual knobs (MAIN: FX enable and master mute), the destination field (`mixerDestination56LabelStyle`), the level fader (`mixerSliderLEVEL254LabelStyle`, 470 for buses and MAIN) with stereo peak meters and clip indicators, and (sources, filters) the pan and the two sends (`mixerSliderPAN56`, `mixerSliderB156`, `mixerSliderB256`, each with an `Inactive` twin).

## Definitions

`skin/defs_mixer.json` (13): the four strip frames, the pan and send sliders with their inactive twins, and the three label styles (destination, level 254, level 470).

## Data

Blocks of `shell-mpc/native_ui_params.h`: `kMixerPeakBase`, `kMixerHoldBase` (the meters), `kMixerControl` (`mixer::kSolo`, `kMute`), `kMixerSoloVisual`, `kMixerMuteVisual`, `kMixerStereoBase` (PeakL, PeakR, HoldL, HoldR, ClipL, ClipR per strip). The faders, pans, sends and destinations bind engine parameters directly.

## Tab exit and page loading

On layout entry (`MixerUi::update`), `inst.setUiFullOpen(!nativeBase())` keeps the ordinary Overlay Instance frame for native root operation. On leaving the tab (`MixerUi::leftTab`, `shell-mpc/overlay/mixer_ui.h:242-251`), `inst.setNativeMixerActive(false)` is set, meters are zeroed, and `inst.setUiFullOpen(false)` is explicitly called to eliminate the Frame-24 exit leak into destination tabs (`d49bca22`).

## Q groups

On the XL Q 1 .. Q 10 follow the ten LEVEL faders; the four-knob banks are [SUB, A, B, C], [NOISE, F1, F2, MAIN], [BUS 1, BUS 2]. PAN, sends and destinations are touch only.

## C++ owners

`shell-mpc/overlay/mixer_ui.h` (rows, Q banks, meters); strip component: [../mixer_strip.md](../mixer_strip.md), [../native-mixer-strip.md](../native-mixer-strip.md). Retired unreferenced C++ symbols (`b87ef437`): `kElementMixer`, cell geometry helpers, `formatRow`, `valueText`, `qGroupName`, `writeCell`, `listOpen`, and `peak_`.
