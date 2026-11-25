# Voice UI map

The page is1280×628 at screenY110. It edits62 `voicectl.*` parameters: length,8×6 sequence values,4random depths,10×detune,ENV/LFO scaling+mode and5oscillator targets. Existing parameter IDs and native proxy offsets are unchanged.

The layout source is [`voice_layout.json`](../../scripts/native_voice/voice_layout.json); C++ uses its generated [`voice_native_layout.h`](../../shell-mpc/ui/voice_native_layout.h). Production native bindings live in [`voice_tab.py`](../../scripts/native_voice/voice_tab.py), while [`VoiceUi`](../../shell-mpc/overlay/voice_ui.h) owns selection/proxies/live grid and Q routing.

| Area | Actual component / behavior |
|---|---|
| HeaderSTEP | Shared native Selector, host choice drawer, STEP1…8 |
| HeaderLENGTH | Name–Value,1…8 |
| Headerselected value | Name–Value with actual row caption, MID/L/R/cents/percent formatting |
| Rowchooser | Shared segmented radio group, six options |
| Sequence | Eight native step buttons and eight bipolar dials bound to the selected row. Native labels show values. Sounding steps light up on the overlay. |
| RANDOM | Shared dials, PAN unipolar depth variant, 10× ActiveText |
| SCALING | Top LFO MODE title+48px face/56px choice hit area; ENV TIME and LFO TIME/RATE below |
| TARGETS | ActiveText A/B/C/Sub/Noise, scopePAN/DETUNE |

Drag a dial to edit its step. Tap a step number or its value area to select it. The selected editor step is independent of the engine note sequence.

Four physical Q groups remain STEP, RANDOM, SCALING, TARGETS; NOISE is touch-only. Gen1 adjacent-bank framing uses oneQ with no numbers. Gen2 has no Q/frame code. Existing intentionally spread-out groups remain frameless.

[Current Voice screen on the MPC Key 61](../screens/voice.jpg). [Offline fixture details](voice-current/README.md).

See [component documentation](../ui-components/voice-screen.md), [engine facts](../dsp-knowledge/voice-control.md) and [documentation review](../review/VOICE-CONTROL-REVIEW-2026-11-24.md). Exact third-party seed/stealing/nonlinear behavior parity is not established. MPC XL/Key61 touch, native drawer appearance and audible acceptance remain device checks.
