# Bold cut on Mac

The target images and the tables in [SIMPLIFY.md](SIMPLIFY.md) describe the original controls and where to find them. This audit uses the generated SDK skin, the current page and VST input paths, and the Mac reference renderer. The timing reference is `c251bf060a4af66dc88df523f296b0a7e0d45d49`. Hardware touch delivery, composition, helper CPU and GPU time remain part of the grouped XL and Key 61 run.

## Control comparison

| Surface | What the audit checks | Documented changes or discrepancies |
| --- | --- | --- |
| [Play and Osc](osc-play-bold-cut/README.md) | POWER, TYPE, source, zoom, destination and the compact PAN/LEVEL shortcuts, plus the ENV, LFO and RND chooser and Expand routes in the SDK skin and real VST host tests. Standalone Osc retains its per-type controls, MORE and read-only route indicators. | Play's stock name menus provide the full named choices. Standalone used/free selection remains on Env and LFO. The standalone OSC detail and empty LOAD work has its own tests and owner. |
| [Env](env-lfo-bold-cut/README.md#env-13-controls-before-and-after) | The ENV 1–6 used/free selector, TARGETS, four ADSR, three SHAPE, DELAY, HOLD, BEATS and RETRIG. The scalar targets follow the selected envelope and units. The curve handles and routed destination tags stay in the overlay. | The native TARGETS button in the timing reference lost the count displayed in the target image and opaque editor. The native button now shows the count using the existing action handle’s VST display text. The normalized action still opens the correct detail without an engine write. |
| [LFO and RND](env-lfo-bold-cut/README.md#lfo-the-recount) | Standard and RND bindings in Hz/BPM/KEY, including HOST in Timing, mode-dependent RATE, division and key tracking, separate SMOOTH switch/time, and TARGETS. | Standard built-in shapes can now open the same upper-right editor as a custom shape. RND has no editor there. The standalone native controls and the expanded Play paths are checked separately. |
| [Filter](filter-bold-cut/README.md) | F1/F2, F1+F2, NOTE/VELO, model/style, cutoff, resonance, drive, mix, track, blend, routing and conditional Comb/Formant fields. The Mixer retains ownership of editable OUT settings. | The host owns the standalone F1/F2 PZ launcher and toolbar actions. The overlay handles point edits on the curve. See the [Filter map](filter.md). |
| [FX](fx-pilot/README.md) | All 16 effect kinds and the 28 default/MORE pages that their initial conditions expose. The native dial, toggle and named or integer domains resolve to the active effect; the filter model/style, compressor mode, Delay sync and splitters have separate dynamic tests. | The chain gallery, page actions and MOVE have their own geometry. The standalone Compressor BAND selector now uses a source-bound native control. Its selection is local view state. See the [FX map](fx.md). |
| [Mixer and Master](mixer-master-bold-cut/README.md) | Mixer has 50 strip controls without a Master OUT. The native destinations keep the same parameters. MAIN/TUNING documents Link, bend and MPE visibility, PAD and both output meters. | Mixer PEEK and the levels-only Q groups supersede the old routing and one-strip Q examples. At the measured reference the native linked bend occupied half the intended cell and retained BEND UP. The separate Master change restores the combined BEND cell and checks the native panel switch. The Master VOICE OPTS Q-Link decision remains tracked in TODO. |
| [Macros](macros.md), [Mod](mod.md), [Voice](voice.md) | Included in timing for the full 15-tab tour. They were unchanged by the original cut. Native control contracts and Q/gesture tests cover their later ownership changes. | Later layouts are documented in their own maps. |

The packaged FX contract tests check the stock delegates and exact header hit boxes. The C++ default page scan records each active engine key and layout. The native page tests also cover preset/instance retargeting and OFF guards. The staged audit evidence, the source of the timing probe and the raw samples are under `.hw-local/mpc-perf/mac-bold-cut/`.

## Overlay work per update

The original revisions are the gallery references: FX `26d9c4f0`, Mixer and Master `0473eb47`, Env and LFO `f6ae52d0`, Filter `7147266b`, Play and Osc `6e89be80`. Macros, Mod and Voice use `26d9c4f0` as a tour reference.

Each cell below compares **original / opaque fallback / native background** at the audit reference. Later [Play and oscillator edit measurements](../dsp-knowledge/mpc-display.md#play-and-oscillator-edit-cost-mac-arm64) cover the FRAME cache and removal of duplicate LFO calculations. Times are microseconds per display tick on an Apple M5 Pro. Edit means alternating a bound parameter through the normal model read path. Playback feeds repeatable voice cursors, envelope stages, voice steps and strip levels. The probe sums the time in `IView::update` and `IView::describe`. It counts submissions and runs the helper's cache planning separately. It excludes VST report transport, rasterization, the stock host and the GPU. Each build ran nine batches of 160 ticks per phase per surface. The samples are interleaved across three original runs and two runs of each current path.

| Surface | Idle | Playback | Edit | Playback frames |
| --- | ---: | ---: | ---: | ---: |
| Play | 160.9 / 121.6 / 139.6 | 219.8 / 173.8 / 207.8 | 370.2 / 328.6 / 421.6 | 160 / 160 / 160 |
| Macros | 48.0 / 35.1 / 31.1 | 48.2 / 35.2 / 30.7 | 73.0 / 35.3 / 31.1 | 0 / 0 / 0 |
| Osc A | 131.1 / 75.1 / 90.1 | 126.8 / 74.8 / 89.8 | 1247.8 / 891.8 / 1249.0 | 0 / 0 / 0 |
| Osc B | 137.9 / 73.0 / 91.6 | 138.3 / 74.6 / 91.5 | 1342.5 / 896.3 / 1201.1 | 0 / 0 / 0 |
| Osc C | 129.9 / 79.1 / 87.4 | 130.1 / 79.4 / 93.9 | 1231.2 / 864.5 / 1153.9 | 0 / 0 / 0 |
| Sub | 100.6 / 73.9 / 89.1 | 138.0 / 102.2 / 118.7 | 584.4 / 417.2 / 502.8 | 160 / 160 / 160 |
| Noise | 69.2 / 55.4 / 61.4 | 70.5 / 55.6 / 61.0 | 256.1 / 195.9 / 230.9 | 0 / 0 / 0 |
| Env | 115.9 / 93.6 / 95.9 | 180.5 / 138.8 / 145.9 | 173.8 / 141.4 / 134.6 | 160 / 160 / 160 |
| LFO | 99.8 / 64.3 / 26.8 | 175.4 / 96.2 / 36.8 | 291.9 / 267.5 / 203.5 | 160 / 160 / 160 |
| Filter | 181.6 / 185.3 / 167.9 | 187.6 / 185.6 / 172.5 | 1473.6 / 1457.2 / 1424.4 | 0 / 0 / 0 |
| FX Delay | 102.1 / 57.1 / 17.7 | 87.8 / 57.5 / 17.9 | 114.7 / 85.0 / 22.0 | 0 / 0 / 0 |
| FX Delay MORE | 76.0 / 46.0 / 17.4 | 73.9 / 46.5 / 17.2 | 90.3 / 67.9 / 19.9 | 0 / 0 / 0 |
| Mixer | 107.1 / 126.1 / 22.9 | 172.2 / 197.6 / 31.7 | 165.9 / 172.4 / 23.9 | 160 / 160 / 160 |
| Master | 115.7 / 77.2 / 16.1 | 162.8 / 76.5 / 16.4 | 160.8 / 76.8 / 16.0 | 160 / 0 / 0 |
| Voice | 142.6 / 133.0 / 120.1 | 155.0 / 131.4 / 121.3 | 175.5 / 129.8 / 118.8 | 32 / 32 / 32 |
| Mod | 20.9 / 18.8 / 9.4 | 20.6 / 18.8 / 9.2 | 31.7 / 19.1 / 9.2 | 0 / 0 / 0 |

The strongest separation is on LFO, FX and the native Mixer and Master paths. For example, per-run LFO playback medians are 143.7–180.2 µs on the original view and 36.4–37.4 µs on the native view. FX Delay MORE idle is 64.3–86.2 µs against 17.1–18.0 µs. Native Mixer playback is 29.7–33.9 µs against 135.8–180.2 µs. The Mixer still submits scenes under this changing strip-level feed, which supplies peaks even to inactive strips. The native Master sends no overlay scene in the measured playback window while the stock host owns the output meters. The Play edit series overlaps the original range. Filter edit varies enough between runs to preclude a gain claim.

Each phase uses a simulated 40 ms clock. Playback frame counts are submissions per 160 ticks. Their zeros indicate that the particular feed did not move that page’s overlay. FX Delay and MORE compare against the original first and second editor pages with the same Delay selected. Device acceptance retains the actual page switch, notes, gestures, render timing and screenshots.

## Native bindings and outstanding hardware checks

Generators are under `scripts/mpc-skin-extras/` and event handlers under `shell-mpc/overlay/`. The live graphs and their editing gestures still use the overlay. The Mac results above cover the listed fixtures. The last column identifies checks for the release packages on the XL and Key 61.

| Page | Skin generator | Event handler | Check on hardware |
|---|---|---|---|
| Play | `82_play.py`, `88_play_env_lfo.py`, `98_play_expanded.py` | `play_view.h` | Compact and expanded editors; verify the physical Q-Link targets after CLOSE. |
| Macros | `60_macros.py` | `macro_ui.h` | Macro dials and readouts; check target routing after a preset change. |
| OSC A/B/C, Sub, Noise | `54_osc_selector.py`, `55_osc_routing.py`, `56_osc_power.py`, `57_osc_settings.py`, `58_osc_detail.py` | `osc_page.h` | Source, routing, power and settings; check selection and OFF protection. |
| Env | `96_env_native.py` | `env_ui.h` | Envelope selection and parameters; check live gestures and the TARGETS return path. |
| LFO/RND | `53_lfo_smooth.py` | `lfo_ui.h` | Mode-dependent controls; check Q-Link names and the live curve. |
| FX | `20_fx.py` | `fx_ui.h` | Effect parameter pages; check the default and MORE graphs. |
| Mixer | `84_mixer.py` | `mixer_ui.h` | Strip controls and PEEK routing; check clipping and return to the selected filter. |
| Filter | `98_filter_native.py` | `filter_ui.h` | Filter parameters and PZ toolbar; check graph editing and OFF protection. |
| Mod | `70_mod.py`, `98_mod_targets.py` | `mod_ui.h` | Matrix and route editor; check paging, row targeting and stale input. |
| Master | `75_master.py` | `master_ui.h` | Output and tuning controls; confirm the GLIDE Q-Link assignment. |
| Voice | `84_voice.py` | `voice_ui.h` | Voice controls; confirm final SCALING and TARGETS placement. |
