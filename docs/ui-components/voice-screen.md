# Voice Control screen

The production Voice page uses the existing native Pan, Level, ActiveText, Selector, Name–Value and segmented radio component APIs. The host displays step selection and parameter readouts. The C++ VoiceGrid layer draws the sounding-step indicators.

![Current native Voice composite](../screens/voice.jpg)

The image is a synthetic preview of the 2026-11-26 layout (`scripts/render_voice_screen_preview.py`: the generated skin and the C++ overlay scene), not a hardware capture. Design record: [MASTER-VOICE-UI-REDESIGN-2026-11-26](../review/MASTER-VOICE-UI-REDESIGN-2026-11-26.md).

## Layout (2026-11-26)

Four titled native panels ([`components/native/panel`](../../components/native/panel/panel.py)): SEQUENCE across the width (ROW list, the eight step columns, the strip of the selected step's six values), then RANDOM, SCALING and TARGETS side by side, so each Q group lies on its own card in Q-Link order. The header holds the title, STEP, LENGTH and the selected value.

## Controls

- Header STEP uses the shared Selector and the host's `fx editor choice menu`; LENGTH and the selected row value use Name–Value fields. The value title follows PAN / DETUNE / CUTOFF / ENV / MOD1 / MOD2 and shares the grid's MID/L/R/cents/percent formatting.
- RANDOM uses shared dials for the four variation depths. PAN ranges from 0 to 100%. The 10× DETUNE ActiveText switch multiplies random detune depth.
- SCALING puts the LFO MODE selector above ENV TIME and LFO TIME / LFO RATE. Its face is 56 px high under a 24 px title with a title above, constructed portably using `build_titled_selector` from [`components/native/selector/selector.py`](../../components/native/selector/selector.py). Percent scales cycle duration (200%=half frequency); Rate scales frequency (200%=double frequency).
- TARGETS uses ActiveText for A/B/C/SUB/NOISE grouped into a passive composite via `build_titled_active_text_group_definition` from [`components/native/active_text/active_text.py`](../../components/native/active_text/active_text.py). The PAN/DETUNE scope label stands right of the panel title; it and the child relative positions are derived from [`scripts/native_voice/voice_layout.json`](../../scripts/native_voice/voice_layout.json): CUTOFF and ENV sequence/random variation still act on the shared per-voice filters/envelopes. GlobalENV/LFO scaling is global; MOD1/MOD2 are matrix sources.

## Geometry and ownership

[`scripts/native_voice/voice_layout.json`](../../scripts/native_voice/voice_layout.json) is the geometry source of the controls, the step grid, the strip and the panels. [`scripts/gen-voice-native-layout.py`](../../scripts/gen-voice-native-layout.py) generates [`voice_native_layout.h`](../../shell-mpc/ui/voice_native_layout.h) for the C++ screen.

All 48 sequence parameters share one bipolar dial definition. Row selection exposes eight 90×90 px touch targets. Native buttons select the step. Native labels display the column values and the six values of the selected step. The overlay draws the sounding-voice dots. Relative knob drags stay with their starting step; cross-column bar painting was removed. The obsolete native voice_grid component, assets, catalog entry and bar-only tooling were deleted. The C++ VoiceGrid remains the sequence model/format/live-indicator adapter.


In production, dragging a knob edits its fixed parameter through the host. Tap a numbered step to select it.

## Q-Link

Four existing groups are preserved: STEP [step,row,length,value], RANDOM [four depths], SCALING [10×,ENV time,LFO scale,mode], TARGETS [A,B,C,Sub]. NOISE remains touch-only. Gen1 adjacent RANDOM/TARGETS banks draw the shared single unnumberedQ frame; spread-out STEP/SCALING groups retain their frameless contract. Gen2 compiles out Q frames and markers.

## Safe updates and verification

Row/step proxies carry source/selection/preset identity. A queued value from the previous row, step, preset, tab or modal cannot write the current field. Native sequence knobs bind fixed engine parameters; the native readouts bind to the same parameters.

Targeted Python tests cover bindings, actual ActiveText buttons≥56px, assets/state masks, geometry, shared knob definitions, mode ordering and the actual C++ geometry dump. C++ Voice-only tests cover production native knob apertures/no duplicate capture, selection/preset/modal safety and both generation policies. Engine evidence and remaining parity/coverage limits: [Voice Control facts](../dsp-knowledge/voice-control.md), [review](../review/VOICE-CONTROL-REVIEW-2026-11-24.md).
