# Voice UI map

The page is1280×628 at screenY110. It edits62 `voicectl.*` parameters: length,8×6 sequence values,4random depths,10×detune,ENV/LFO scaling+mode and5oscillator targets. Existing parameter IDs and native proxy offsets are unchanged.

The layout source is [`voice_layout.json`](../../scripts/native_voice/voice_layout.json); C++ uses its generated [`voice_native_layout.h`](../../shell-mpc/ui/voice_native_layout.h). Production native bindings live in [`voice_tab.py`](../../scripts/native_voice/voice_tab.py), while [`VoiceUi`](../../shell-mpc/overlay/voice_ui.h) owns selection/proxies/live grid and Q routing.

| Area | Actual component / behavior |
|---|---|
| HeaderSTEP | Shared native Selector, host choice drawer, STEP1…8 |
| HeaderLENGTH | Name–Value,1…8 |
| Headerselected value | Name–Value with actual row caption, MID/L/R/cents/percent formatting |
| Rowchooser | Own segmented radio faces, six options |
| Eightbars | Native Vertical ColourBar bipolar sliders, one shared definition and48 independent bindings; overlay selection/baseline/live step indicators |
| RANDOM | Own blue dials, PAN unipolar depth variant;10× ActiveText |
| SCALING | Top LFO MODE title+48px face/56px choice hit area; ENV TIME and LFO TIME/RATE below |
| TARGETS | ActiveText A/B/C/Sub/Noise, scopePAN/DETUNE |

Native bars exclusively own value drags. Column headers/footers select without painting a value. Legacy overlay-mode multi-column painting is retained but not double-captured over native sliders. The selected editor step is independent of the engine note sequence.

Four physical Q groups remain STEP, RANDOM, SCALING, TARGETS; NOISE is touch-only. Gen1 adjacent-bank framing uses oneQ with no numbers. Gen2 has no Q/frame code. Existing intentionally spread-out groups remain frameless.

Current offline pictures: [Percent](voice-current/voice-percent-on-gen2.png), [Rate](voice-current/voice-rate-on-gen2.png), [OFF text states](voice-current/voice-percent-off-gen2.png), [Gen1](voice-current/voice-rate-on-gen1.png). [Rendering evidence and limits](voice-current/README.md). These are actual generated nativeTUI + C++ scene fixtures, never hardware captures.

See [component documentation](../ui-components/voice-screen.md), [engine facts](../dsp-knowledge/voice-control.md) and [documentation review](../review/VOICE-CONTROL-REVIEW-2026-11-24.md). Exact third-party seed/stealing/nonlinear behavior parity is not established. MPC XL/Key61 touch, native drawer appearance and audible acceptance remain device checks.
