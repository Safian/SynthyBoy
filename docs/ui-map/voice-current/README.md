# Current Voice fixture evidence

All six images are **offline** production-generated TUI + current C++ `VoiceUi` / `VoiceGrid` / `scene_cpu` composites. They are1280×628 tab images, not device screenshots. There is no copied historical graph, hand-painted dial or VERIFIED badge.

- `voice-percent-on-gen2.png`: all target and10× textON, Percent mode.
- `voice-rate-on-gen2.png`: same values, Rate mode and LFO RATE caption.
- `voice-percent-off-gen2.png`: targets and10× textOFF.
- `*-gen1.png`: same fixture states, four-knob visual policy and activeRANDOM bank showing one unnumberedQ. Gen2 has none.
- `native-host.png` and `live-overlay.png`: real input layers. `layers.png` stacks native, overlay, composite vertically; canonical component breakdown is the same artifact.
- MatchingJSON contains only the62 relevant Voice parameter states plus proxy selection. It is fixture data, not a release skin. `manifest.json` records generated whole-skin/asset sizes and unsupported-native decoder diagnostics.

Fixture: randomPAN25%,DETUNE14c,CUTOFF15%,ENV10%; ENV125%,LFO150%; eightPAN values−40/−15/+20/+65/+40/0/−30/−55; live voices atsteps1and4. Selectionstep1,PAN. All ON/OFF variants change only target/10× state; Rate changes only mode. The skin adapter resolves actual handles, zero-based radio IDs, filmstrip frames and SDK native ColourBar properties.

Regenerate with `python3 scripts/render_voice_screen_preview.py`. It generates fresh skin into a temporary folder and compiles only its fixture. Set `VOICE_CORE_BUILD` to an existing core build if automatic discovery cannot find one. No full gallery or full suite is needed. `--work-dir` is for explicit inspectable evidence; use a fresh directory after source changes.

A matching base4803867b C++ scene, nativeJSON and fixture state were saved during development. That baseline contains host `GenericNoLabel` knobs whose exact portable artwork is not shipped in this repo; the adapter flags them as unsupported and does **not** paint an invented replacement. Consequently there is no falsely complete same-state native before-image. Historical guide images remain historical; a full actual before capture requires the host/device renderer.

Measured on the same base/generator state: TUI886887→886157 bytes (−730); nativePNG assets1133458→1445373 bytes (+311915). The existing whole-skin750000-byte cap is still exceeded; it was not relaxed. This is not a claim of release-size acceptance or hardware performance.
