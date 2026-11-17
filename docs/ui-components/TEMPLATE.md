# The reusable UI kit and the starter template (MpcKit)

Goal: another Akai MPC VST2 plugin uses SynthyBoy's plugin-drawn UI - the component library (`shell-mpc/ui/`) and the overlay runtime (`shell-mpc/overlay/`) - **without SynthyBoy's engine**, and a starter plugin (`template-mpc/`, "MpcKit Demo") shows how: "you only assemble your VST from ready UI elements". This document is the survey, the design and the plan. Branch `work/ui-template`, base `c6a39140`. Component pages: `docs/ui-components/` (written by the documentation worker); the template's own docs: `template-mpc/README.md` and `template-mpc/AGENTS.md`.

Licence: AGPL-3.0-or-later (the file headers).

## 1. Summary

- **Dependency map** (section 2): the UI library is nearly engine free (18 of 42 headers GENERIC, the rest are SynthyBoy pages). The overlay runtime is generic except its **instance interface** (`IInstanceView`, 24 methods), its **view table** (`overlay_layout.h`, generated for 24 SynthyBoy views), its **view factory** (`pages.h`, `Controller::makeView`) and a few names (`/tmp/synthyboy-*`, the helper file name).
- **The adapter** (section 3): `shell-mpc/kit/` (namespace `mpckit`, header only, C++17, no engine include): `IParamSource`, `IInstance` (+ `IInstanceState`), `IQTargets` / `QProxyBank`, `IPageSet` / `IPageFactory`, `IContentSource`, and the page helpers `Binder` and `PageBase`. `kit/synthyboy_adapter.h` and `kit/synthyboy_params.h` implement them over SynthyBoy's existing code.
- **Migration plan** (section 4): 13 steps, each keeps SynthyBoy's behaviour bit for bit; **step 1 is done** (`q_publish.h`), the rest waits until the current UI changes have landed.
- **Template** (section 5): `template-mpc/`, complete as a plugin; its pages are built on the kit and compile against the generic files only.
- Sections 6 and 7: what a template user writes versus what the kit does; the open risks.

## 2. Dependency map

Verdicts: **GENERIC** = no SynthyBoy knowledge (the engine, its parameter table, its content or its names); **ADAPTER** = generic once a small interface replaces N named dependencies (named in the last column); **SPECIFIC** = a SynthyBoy page or feature. "Depends on" lists what comes from SynthyBoy's engine / core (`synthyboy/...`) or from SynthyBoy's own layout; includes of other generic files are shown only where they explain the closure. The include closure of the GENERIC components (`core.h` <- `base.h`, `composite.h`, `qgroup.h`, `led_*.h`, `list.h`, `env_graph.h`, `mixer_strip.h`, ...) is: the ui headers, `overlay/gpu/scene.h`, `overlay/sdf_font.h(+_data.h)`; nothing else.

### 2.1 `shell-mpc/ui/` (42 headers: 18 GENERIC, 1 ADAPTER, 23 SPECIFIC)


| File | Classes / what | Depends on (SynthyBoy knowledge) | Verdict | What the adapter must supply / note |
| --- | --- | --- | --- | --- |
| `core.h` | Rect, Change, Pen, Cell, TapGesture, Component | overlay/gpu/scene.h, overlay/sdf_font.h, theme.h (all generic) | **GENERIC** | - |
| `theme.h` | Look (3 looks), Detail, colours | none | **GENERIC** | a plugin may add a Look |
| `base.h` | Knob, NumberField, Stepper, UpDownStepper, ToggleIcon, MenuButton, HBar, DetailButton, RouteButton, PictogramSelector | core.h | **GENERIC** | - |
| `qgroup.h` | QGroup | core.h | **GENERIC** | - |
| `led_toggle.h` | LedToggle | base.h | **GENERIC** | - |
| `led_list.h` | LedList | led_toggle.h | **GENERIC** | - |
| `list.h` | ListPopup | core.h | **GENERIC** | - |
| `text_entry.h` | TextEntry (on-screen keyboard) | core.h, theme.h | **GENERIC** | - |
| `time_ruler.h` | round time marks | none | **GENERIC** | - |
| `env_graph.h` | EnvGraph, envgraph:: | core.h, time_ruler.h; knows no engine; time axis 0 .. 10 000 ms skew 0.3 (parameters must use it) | **GENERIC** | the plugin feeds State and writes the Edit (shape_page.h) |
| `mixer_strip.h` | Fader, SliderBar, StripPower, RouteDropdown, MixerStrip | base.h, composite.h, core.h | **GENERIC** | - |
| `mod_board.h` | ModSourceBoard (chips by name and colour) | core.h | **GENERIC** | - |
| `note_mask.h` | NoteMask | led_toggle.h | **GENERIC** | - |
| `voice_grid.h` | VoiceGrid | core.h, theme.h | **GENERIC** | - |
| `display.h` | oscillator display (display::Data, draw) | core.h; synth vocabulary, no engine | **GENERIC** | - |
| `lfo_display.h` | LFO display (Data) | core.h | **GENERIC** | - |
| `lfo_model.h` | LFO shape model | none | **GENERIC** | - |
| `composite.h` | Composite (base), ToggleNumber, TypeBar, KnobRandPair + oscillator composites TuneRow, UnisonBox, WarpSlot, WarpPair, SourcePicker, BasicControls, RoutePanel | base.h, led_toggle.h, qgroup.h; oscillator vocabulary, no engine | **GENERIC** | - |
| `ui.h` | umbrella include of the whole library | includes fx_screen.h -> synthyboy/fx_types.h and voice_*.h | **ADAPTER** | split into ui_generic.h (the GENERIC headers) and ui.h (= ui_generic.h + the specific ones): migration step 4 |
| `panel.h` | KnobPanel, DetailPage, ModulationsPage, presets; enum PanelSlot | composite.h, mod_board.h; the slot names are SynthyBoy oscillator parts (WtPos, Unison, Warp, Sub*, Noise*) | **SPECIFIC** | - |
| `play_rows.h` | rows of the Play thirds | core.h | **SPECIFIC** | - |
| `screen.h` | OscillatorCompact / Full | panel.h, play_rows.h | **SPECIFIC** | - |
| `env_screen.h` | EnvSelector, ENV compact / full | env_graph.h, panel.h, play_rows.h | **SPECIFIC** | - |
| `lfo_parts.h` | ShapePicker ... | led_list.h, lfo_model.h, panel.h | **SPECIFIC** | - |
| `lfo_screen.h` | LfoCompact / Full | lfo_parts.h, play_rows.h | **SPECIFIC** | - |
| `shape_editor.h` | ToolSelector, ShapeCanvas, ShapeEditorPage | lfo_display.h, lfo_model.h, panel.h | **SPECIFIC** | - |
| `filter_display.h` | FilterDisplay, FilterRoutePanel, FilterKnob, FilterFullScreen | composite.h, list.h, panel.h; the filter models of SynthyBoy | **SPECIFIC** | - |
| `fx_chain_bar.h` | FxSlotCard, FxChainBar | synthyboy/fx_types.h: FxInstanceType, kFxType* (13 types), kNumFxInstanceTypes | **SPECIFIC** | - |
| `fx_band_board.h` | FxBandBoard | fx_chain_bar.h (-> fx_types.h) | **SPECIFIC** | - |
| `fx_display.h` | FxDisplay | synthyboy/fx_types.h (kFxTypeDelay, Eq, Compressor, Distortion, Filter) | **SPECIFIC** | - |
| `fx_gallery.h` | FxGallery | fx_display.h, synthyboy/fx_types.h | **SPECIFIC** | - |
| `fx_layout.h` | FxLayout tables | synthyboy/fx_types.h, synthyboy/params.h (ParamInfo) | **SPECIFIC** | - |
| `fx_screen.h` | FxFullScreen | fx_chain_bar.h, fx_gallery.h, fx_slot_editor.h | **SPECIFIC** | - |
| `fx_slot_editor.h` | FxSlotEditor | synthyboy/fx_types.h (FxTypeDescriptor, fxTypeDescriptor, kFxInstanceParams), params.h (findParamByKey, paramTable) | **SPECIFIC** | - |
| `mixer_screen.h` | MixerRoutingBar, MixerFullScreen | mixer_strip.h | **SPECIFIC** | - |
| `mod_detail.h` | ModSlotDetail | mod_matrix.h | **SPECIFIC** | - |
| `mod_macros.h` | macros page, SpectrumStrip | list/led_list; the 8 macros of SynthyBoy | **SPECIFIC** | - |
| `mod_matrix.h` | ModRowSelector, the modulation matrix | list.h, mod_board.h | **SPECIFIC** | - |
| `mod_targets.h` | ModTargetsPage | panel.h | **SPECIFIC** | - |
| `voice_master.h` | MasterScreen, displays (portamento, keyboard, meters) | composite.h, led_list.h | **SPECIFIC** | - |
| `voice_screen.h` | VoiceScreen | voice_grid.h, composite.h | **SPECIFIC** | - |
| `choice_names.h` | names of the engine choices on the screen | none (names of SynthyBoy choices) | **SPECIFIC** | - |


Notes. `choice_names.h` and the `fx_*` headers are the only ui headers that include engine headers (`synthyboy/fx_types.h`, `params.h`). `ui.h` is the only ADAPTER: including it pulls `fx_screen.h` and with it the engine. The oscillator composites in `composite.h` and the oscillator `display.h` are generic CODE with a synth vocabulary; a plugin without oscillators simply does not use them.

### 2.2 `shell-mpc/overlay/` (the runtime and the pages; about 35 GENERIC, 12 ADAPTER, 19 SPECIFIC files)


| File | Classes / what | Depends on (SynthyBoy knowledge) | Verdict | What the adapter must supply / note |
| --- | --- | --- | --- | --- |
| `draw.h` | drawing helpers (CPU) | sdf_font.h | **GENERIC** | - |
| `sdf_font.h / sdf_font_data.h` | SDF font (Titillium) | none | **GENERIC** | - |
| `geometry.h` | Rect, rotations | none | **GENERIC** | - |
| `renderer.h` | CPU renderer interface | draw.h, geometry.h | **GENERIC** | - |
| `scene_cpu.h` | CPU rendering of a scene | draw.h, gpu/scene.h | **GENERIC** | - |
| `presenter.h` | plane presentation | drm_interfaces.h | **GENERIC** | - |
| `drm_interfaces.h` | IPlaneBackend, IFrameSource | geometry.h, vblank_pacer.h | **GENERIC** | - |
| `drm_linux.h` | Linux DRM plane / framebuffer reader | discovery.h, stacking.h, mouse_input.h, vblank_linux.h, safe_file.h; the file name /tmp/synthyboy-overlay-zpos-rest | **GENERIC** | Branding: prefix of the /tmp file names (step 8) |
| `discovery.h` | finds CRTC, planes, panel, refresh at run time | none | **GENERIC** | - |
| `stacking.h` | plane stacking order | discovery.h | **GENERIC** | - |
| `cpu_plan_linux.h` | cores the overlay may use | discovery.h | **GENERIC** | - |
| `vblank_pacer.h / vblank_linux.h` | frame pacing | none | **GENERIC** | - |
| `gpu_clock.h` | GPU clock governor | safe_file.h; file name /tmp/synthyboy-overlay-gpu-minfreq.orig | **GENERIC** | Branding |
| `gpu/scene.h` | SceneBuilder, SceneData (display list) | none | **GENERIC** | - |
| `gpu/cell_plan.h, gpu/cell_cache.h` | cached cells | scene.h | **GENERIC** | - |
| `gpu/gpu_overlay.h` | GPU helper backend | drm_interfaces.h, geometry.h, safe_file.h | **GENERIC** | - |
| `gpu/helper_transport_linux.h` | helper process transport | gpu_overlay.h; log /tmp/synthyboy-overlay-gpu.log, memfd name | **GENERIC** | Branding |
| `gpu-helper/main.cpp, gl_api.h` | the GPU helper executable | gpu_clock.h, sdf_font.h, gpu/*, vblank_*; its file name synthyboy-overlay-gpu-<arch> is chosen by service.h and the packaging | **GENERIC** | Branding: helper file name |
| `touch_decoder.h, touch_input.h, touch_linux.h` | touch screen (evdev, protocol B) | none | **GENERIC** | - |
| `mouse_input.h, mouse_linux.h, mouse_router.h` | mouse and pointer plane | geometry.h, touch_*.h | **GENERIC** | - |
| `panel_key.h` | the host Q-Link panel key (the strip the host draws over the right edge) | drm_interfaces.h, geometry.h | **GENERIC** | - |
| `qlink_setting.h` | reads the host option "Show Q-Link Status When Touched" | safe_file.h | **GENERIC** | - |
| `standby.h` | the host standby dim (page held across the blur) | drm_interfaces.h, geometry.h, gpu/scene.h; the host background colour 1e1d1e | **GENERIC** | - |
| `ui_stack.h` | UiStack: main -> full -> detail pages of one instance | none | **GENERIC** | - |
| `q_device.h` | 16 or 4 Q-Link knobs (model name, SYNTHYBOY_MPC_Q_KNOBS) | none | **GENERIC** | Branding: env variable name |
| `content_browser.h` | file browser state | none | **GENERIC** | - |
| `spectrum_curve.h` | spectrum drawing data | ../spectrum*.h | **GENERIC** | - |
| `curve_source.h` | IInstanceView (24 methods) + CurveSource (filter curve) | synthyboy/content_state.h, content_store.h, filter_response.h, params.h, readout.h, state.h; ../spectrum.h | **ADAPTER** | IInstanceView = IInstance (kit) + engine methods; CurveSource stays specific (split, step 7) |
| `view.h` | IView, ViewInput, ViewCounters | curve_source.h (IInstanceView), synthyboy/readout.h (LiveValues), overlay_layout.h | **ADAPTER** | instance as mpckit::IInstance; live as an opaque block through IInstance::readLive (step 5) |
| `page_view.h` | PageView<Ui>, page::Input, page::ground | macro_names.h (MacroNames, NamesScope: the macro names of the instance), page_detector.h, view.h; IInstanceView::content () | **ADAPTER** | a Hooks template parameter replaces the macro-name scope; page::Input derives from mpckit::PageInput (step 6) |
| `pages.h` | the factory pages::make, filterTargets () | all SynthyBoy page headers, synthyboy::FilterResponse::readoutIndices | **ADAPTER** | IPageFactory (kit/page_set.h): SynthyBoyPageFactory (step 10) |
| `controller.h` | Controller, Paths, Timing, Detection use | curve_source.h, view.h, pages.h, play_view.h, views.h; layout::kViews / kNumViews / kMaxInstances / kKind*; makeView (); readoutTargets (); synthyboy::LiveValues; the /tmp names "synthyboy-overlay-*" | **ADAPTER** | IPageSet (table), IPageFactory (makeView, readoutTargets), IInstance::readLive, Branding (step 8) |
| `service.h` | Service (the process wide overlay thread), gpuHelperPath () | device_profile.h (synthyboy/quality.h) for the status line, output_peak.h, play_log.h, poll_guard.h, controller.h, ui/qgroup.h; helper name synthyboy-overlay-gpu-<arch> | **ADAPTER** | Branding (helper name, log paths) and an extra-status hook for the quality line (step 8); becomes mpckit::Overlay |
| `instance_slots.h` | slots 0..15, instance colours <-> frames | overlay_layout.h (layout::kMaxInstances, kInstanceSlotRgb, none, blank, steps), IInstanceView (a pointer type only) | **ADAPTER** | mpckit::InstanceColours from IPageSet; the pointer type becomes IInstance (step 8) |
| `page_detector.h` | PageDetector, Detection (kMaxViews = 24) | instance_slots.h, overlay_layout.h, standby.h | **ADAPTER** | marker table from IPageSet (step 8) |
| `overlay_layout.h / .json` | the generated table of the 24 views of SynthyBoy | generated by scripts/gen-overlay-layout-header.py | **ADAPTER** | per plugin: generate with --kinds / --namespace (step 9) |
| `ui_bind.h` | uibind::: table lookups, writes, formatValue, the modulation matrix helpers | ../ui/ui.h, curve_source.h, macro_names.h, synthyboy/modulation.h, params.h | **ADAPTER** | lookups / writes / formatValue forward to IParamSource + IInstance (step 3); the modulation part stays specific |
| `q_publish.h` | QSlots (the 16 Q slots of a page), usedGroups, setBlocks; modRowTargets, setRowBlocks | ../q_router.h, curve_source.h, synthyboy/params.h (modSlotParams, only modRowTargets) | **ADAPTER** | QSlots::publish takes any instance (DONE, step 1); modRowTargets moves to a specific header (step 1b) |
| `env_ui.h / filter_ui.h / lfo_ui.h / master_ui.h / mixer_ui.h / mod_ui.h / macro_ui.h / voice_ui.h / fx_ui.h / osc_ui.h` | the page of each SynthyBoy tab | their ui/ screens, uibind, q_publish, synthyboy/params.h, modulation.h, readout.h, fx_rack.h, fx_types.h, filter_response.h, lfo_display.h, content_state.h ... | **SPECIFIC** | stay SynthyBoy pages; template for how to write a page: shell-mpc/overlay/master_ui.h |
| `osc_page.h, osc_display.h, osc_marks.h, osc_spec_page.h, play_osc.h` | oscillator page, wave display, marks, spectral page, selector table | synthyboy/osc_cycle.h, params.h, content_state.h | **SPECIFIC** | - |
| `play_view.h` | the Play view (compact elements) | ../play_params.h, env_ui, lfo_ui, osc_ui, synthyboy/env_display.h, lfo_display.h, osc_cycle.h, content_store.h | **SPECIFIC** | - |
| `views.h, env_geometry.h` | FilterView, LfoView, EnvView, SpectrumView (first overlay) | synthyboy/filter_response.h, lfo_display.h, osc_cycle.h, params.h, state.h | **SPECIFIC** | - |
| `macro_names.h` | the macro names of an instance | synthyboy/content_state.h, modulation.h | **SPECIFIC** | - |


What the IInstanceView members are (`overlay/curve_source.h`), and where they go:

| IInstanceView member | Goes to | Why |
| --- | --- | --- |
| `param`, `writeParam`, `sampleRate`, `bpm`, `setQGroup`, `setQVirtual`, `takeQVirtual`, `qWriteCount`, `qStatus`, `setUiFullOpen`, `requestOverlayReport` | `mpckit::IInstance` (same signatures) | every page needs them |
| `live (LiveValues&)` | `mpckit::IInstance::readLive (void*, size_t)` (opaque block) + stays in IInstanceView | the Controller reads the engine's readout per view |
| `content`, `contentStore`, `contentRoots`, `requestContent`, `contentLoad`, `setLfoShape`, `setMacroName` | stay in IInstanceView | SynthyBoy content and names (SPECIFIC) |
| `spectrumRing` | stays | the Macros page (could move to IInstance as a generic optional) |
| `playSelection`, `setPlaySelection`, `playLastSelector` | stay | the Play page |
| `padPressure`, `setPadPressure`, `outputLimiter`, `setOutputLimiter` | stay in IInstanceView; the kit's generic form is `IInstanceState` (`kit/instance.h`), `synthyboy_adapter::State` maps them | shell-level settings |
| (new) `params ()` | `mpckit::IInstance::params ()` -> `IParamSource` | the pages need the table |
| (new) `contentSource ()` | `mpckit::IInstance::contentSource ()` -> `IContentSource` | optional lists |

### 2.3 Shell files (`shell-mpc/*.h`, `synthyboy_mpc.cpp`)


| File | Classes / what | Depends on (SynthyBoy knowledge) | Verdict | What the adapter must supply / note |
| --- | --- | --- | --- | --- |
| `callback_stats.h` | audio callback statistics (diagnostics) | mpc_vst2_abi.h, safe_file.h, thread_policy.h | **GENERIC** | - |
| `host_strings.h` | bounded strings for the host (putBounded, shortName, nameWithSuffix) | none | **GENERIC** | - |
| `output_peak.h, output_stage.h` | output peak, make-up gain / limiter | none (default gain 18 dB is SynthyBoy's choice) | **GENERIC** | - |
| `play_log.h, poll_guard.h` | debug log, PollGuard / PollStatus | safe_file.h; /tmp/synthyboy-* names | **GENERIC** | Branding (the kit log: shell-mpc/kit/log.h, another worker) |
| `plugin_paths.h, safe_file.h, thread_policy.h` | plugin folder, safe file reads, thread scheduling | none | **GENERIC** | - |
| `q_router.h` | QRouter (the 16 Q proxies) | none: it knows no parameter, the plugin passes in how to read and write | **GENERIC** | QProxyBank (kit/q_targets.h) wraps it |
| `spectrum.h, spectrum_bands.h, spectrum_fft.h` | spectrum analyser (ring, FFT, 32 bands) | none | **GENERIC** | - |
| `q_step_rule.h` | stepCountOf (ParamInfo) | synthyboy/params.h (ParamInfo, Scale) | **ADAPTER** | IParamSource::stepCount (kit: the same rule; synthyboy_params.h forwards to this one) |
| `device_profile.h` | engine quality profile per device | synthyboy/quality.h | **SPECIFIC** | - |
| `lfo_live.h, pad_pressure.h, play_params.h, program_list.h, macro_names.h` | LFO cursors, pad pressure, Play proxies, program list, macro names of the shell | synthyboy/params.h, preset.h, content_state.h | **SPECIFIC** | - |
| `synthyboy_mpc.cpp` | the VST2 shell (2889 lines) | the engine, state, presets, content; extractable generic blocks listed below | **SPECIFIC** | see "Blocks of synthyboy_mpc.cpp" |


### 2.4 Blocks of `synthyboy_mpc.cpp` that the kit needs in generic form

`synthyboy_mpc.cpp` is SPECIFIC (engine, presets, content, programs). These blocks are generic in behaviour and are what a second plugin has to write again (the template's `demo_plugin.cpp` / `demo_instance.h` do it in a simplified form):

| Block (lines of the base) | What it does | Generic form |
| --- | --- | --- |
| The display thread `liveLoop` / `startLive` (about 1200 .. 1340), `PollGuard` per feature (poll_guard.h) | a normal-priority thread that tells the host (audioMasterAutomate / UpdateDisplay) what changed: overlay frame, Play proxies, Q proxies, dirty parameters; guarded so no exception ends the host | `mpckit::DisplayThread` with registered features (step 11) |
| Q proxies: `qBase`, `isQParam`, `qReadNorm`, `qPoll`, `qNotifyGroup`, `getParameter` / `setParameter` / `effGetParamName/Label/Display` / `effGetParameterProperties` for a Q index (about 1910, 2290, 2580, 2680) | the proxy answers with its target's value, name, unit, text; a write is the target's write; stepped targets are driven in steps | `mpckit::QProxyBank` (done in the kit; the shell switches to it in step 11) |
| `uiWrite`, `uiDirty`, `uiPoll` (about 826 .. 870) | a page's write is the host's write, then the display thread reports it | `IInstance::writeParam` + the display thread's dirty mask (done in the template) |
| The chunk members: `chunkWithOutputLimiter`, `chunkWithPadPressure`, `chunkWithPlay` (output_stage.h, pad_pressure.h, play_params.h), `buildStateChunk`, `effSetChunk` | shell-level settings appended to the state chunk as JSON members, read back from the end | `IInstanceState` + `SettingsStore::save / load` (key=value text); SynthyBoy keeps its JSON members |
| The parameter `Overlay Instance` (`overlayParamIndex`, `overlayWantedValue`, `overlayPoll`, `uiFullOpen`, frames 0 / 1..16 / 17 / 24) | which instance's page is on screen, and "a full screen page is up: hide the host widgets" | `mpckit::Overlay` (step 8); the template has the 4 numbers locally |
| Parameter order: engine parameters, 4 LFO cursors, 32 spectrum columns, 2 pad pressure, `Overlay Instance`, Play selectors + proxies, `Q 1 .. Q 16` | the skin binds to these numbers | a plugin's order is `[own] [Overlay Instance] [Q 1 .. Q 16]` (template); SynthyBoy keeps its order |
| `startOverlay` / `stopOverlay`, `Service::attach` / `detach` | registers an instance with the overlay, 16 slots | `mpckit::Overlay::attach / detach` (step 8) |
| `effGetParameterProperties` for a Q proxy of a stepped target | the host must see a plain knob (`stepFloat = 1/128`), not the target's integer range | in `demo_plugin.cpp`; to the kit in step 11 |

### 2.5 The skin and the package

| File | Generic? | What a second plugin needs |
| --- | --- | --- |
| `shell-mpc/overlay/overlay_layout.json` (+ `scripts/gen-overlay-layout-header.py`) | schema generic; contents SynthyBoy's (24 views); `KINDS` dict hard-coded (`play, filterui, ...`) | its own JSON; the generator needs `--kinds` (`page`) and `--namespace` / `--out` (step 9) |
| `scripts/overlay-skin/mpc-ui-pages-overlay.json` (the tab list of the overlay skin: `name`, `fn`, `kick`) + `docs/params.json` + `docs/kick-pages.json` -> `scripts/gen-mpc-ui.py` | generic generators, SynthyBoy inputs | the template ships `skin/params.json`, `skin/pages.json` (same schemas, reused unchanged) |
| `scripts/mpc-skin-extras/50_filter_overlay.py` (markers, instance film strip), `83_pages.py` (hide widgets, Q-Link mapping), `82_play.py` | helpers generic; `LAYOUT_JSON`, the parameter counts (LFO cursors 4, spectrum 32, pad pressure 2) and the view `kind`s hard-coded | the template reuses their helper functions from `template-mpc/skin/extras/50_markers.py` with `--extras-dir`; parameterising them (layout path and "parameters before Overlay Instance" from the JSON) is step 9 |
| `scripts/apply-mpc-skin-extras.py` | generic (`--extras-dir`) | used as is |
| `scripts/build-mpc.sh`, `mpc-container-build.sh`, `gen-mpc-manifest.py` | build target `synthyboy_mpc`, names `synthyboy-arm32.so`, `Safian - VST - SynthyBoy`, `uid SfSB`, helper `synthyboy-overlay-gpu-<arch>` hard-coded | `--plugin` option with target / names / manifest (step 12); the template ships `tools/make_package.py` meanwhile |
