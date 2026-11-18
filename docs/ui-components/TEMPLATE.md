# The reusable UI kit and the starter template (MpcKit)

Goal: another Akai MPC VST2 plugin uses SynthyBoy's plugin-drawn UI - the component library (`shell-mpc/ui/`) and the overlay runtime (`shell-mpc/overlay/`) - **without SynthyBoy's engine**, and a starter plugin (`template-mpc/`, "MpcKit Demo") shows how: "you only assemble your VST from ready UI elements". This document is the survey, the design and the plan. Branch `work/ui-template`, base `c6a39140`. Component pages: `docs/ui-components/` (written by the documentation worker); the template's own docs: `template-mpc/README.md` and `template-mpc/AGENTS.md`.

Licence: AGPL-3.0-or-later (the file headers).

## 1. Summary

- **Dependency map** (section 2): the UI library is nearly engine free (20 of 44 headers GENERIC, the rest are SynthyBoy pages). The overlay runtime is generic except its **instance interface** (`IInstanceView`, 30 methods), its **view table** (`overlay_layout.h`, generated for 24 SynthyBoy views), its **view factory** (`pages.h`, `Controller::makeView`) and a few names (`/tmp/synthyboy-*`, the helper file name).
- **The adapter** (section 3): `shell-mpc/kit/` (namespace `mpckit`, header only, C++17, no engine include): `IParamSource`, `IInstance` (+ `IInstanceState`), `IQTargets` / `QProxyBank`, `IPageSet` / `IPageFactory`, `IContentSource`, and the page helpers `Binder` and `PageBase`. `kit/synthyboy_adapter.h` and `kit/synthyboy_params.h` implement them over SynthyBoy's existing code.
- **Migration plan** (section 4): 13 steps, each keeps SynthyBoy's behaviour bit for bit; **step 1 is done** (`q_publish.h`), the rest waits until the current UI changes have landed.
- **Template** (section 5): `template-mpc/`, complete as a plugin; its pages are built on the kit and compile against the generic files only.
- Sections 6 and 7: what a template user writes versus what the kit does; the open risks.

## 2. Dependency map

Verdicts: **GENERIC** = no SynthyBoy knowledge (the engine, its parameter table, its content or its names); **ADAPTER** = generic once a small interface replaces N named dependencies (named in the last column); **SPECIFIC** = a SynthyBoy page or feature. "Depends on" lists what comes from SynthyBoy's engine / core (`synthyboy/...`) or from SynthyBoy's own layout; includes of other generic files are shown only where they explain the closure. The include closure of the GENERIC components (`core.h` <- `base.h`, `composite.h`, `qgroup.h`, `led_*.h`, `list.h`, `env_graph.h`, `mixer_strip.h`, ...) is: the ui headers, `overlay/gpu/scene.h`, `overlay/sdf_font.h(+_data.h)`; nothing else.

### 2.1 `shell-mpc/ui/` (44 headers: 20 GENERIC, 1 ADAPTER, 23 SPECIFIC)


| File | Classes / what | Depends on (SynthyBoy knowledge) | Verdict | What the adapter must supply / note |
| --- | --- | --- | --- | --- |
| `core.h` | Rect, Change, Pen, Cell, TapGesture, Component | overlay/gpu/scene.h, overlay/sdf_font.h, theme.h (all generic) | **GENERIC** | - |
| `theme.h` | Look (3 looks), Detail, colours | none | **GENERIC** | a plugin may add a Look |
| `base.h` | Knob, NumberField, Stepper, UpDownStepper, ToggleIcon, MenuButton, HBar, DetailButton, RouteButton, PictogramSelector | core.h | **GENERIC** | - |
| `carousel.h` | Carousel | base.h | **GENERIC** | - |
| `content_name.h` | contentname:: (display, fitMiddle) | overlay/sdf_font.h | **GENERIC** | - |
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


Notes. The `fx_*` headers are the only ui headers that include engine headers (`synthyboy/fx_types.h`, `params.h`); `choice_names.h` includes no engine header. `ui.h` is the only ADAPTER: including it pulls `fx_screen.h` and with it the engine. The oscillator composites in `composite.h` and the oscillator `display.h` are generic CODE with a synth vocabulary; a plugin without oscillators simply does not use them.

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
| `curve_source.h` | IInstanceView (30 methods) + CurveSource (filter curve) | synthyboy/content_state.h, content_store.h, filter_response.h, params.h, readout.h, state.h; ../spectrum.h | **ADAPTER** | IInstanceView = IInstance (kit) + engine methods; CurveSource stays specific (split, step 7) |
| `view.h` | IView, ViewInput, ViewCounters | curve_source.h (IInstanceView), synthyboy/readout.h (LiveValues), overlay_layout.h | **ADAPTER** | instance as mpckit::IInstance; live as an opaque block through IInstance::readLive (step 5) |
| `page_view.h` | PageView<Ui>, page::Input, page::ground | macro_names.h (MacroNames, NamesScope: the macro names of the instance), page_detector.h, view.h; IInstanceView::content () | **ADAPTER** | a Hooks template parameter replaces the macro-name scope; page::Input derives from mpckit::PageInput (step 6) |
| `pages.h` | the factory pages::make, filterTargets () | all SynthyBoy page headers, synthyboy::FilterResponse::readoutIndices | **ADAPTER** | IPageFactory (kit/page_set.h): SynthyBoyPageFactory (step 10) |
| `controller.h` | Controller, Paths, Timing, Detection use | view.h, instance_slots.h, page_detector.h, kit/page_set.h, kit/branding.h (no engine include since step 8) | **GENERIC** | takes IPageSet and IPageFactory (step 8); SynthyBoyController (controller_synthyboy.h) is its form for the tests |
| `service.h` | Service (the process wide overlay thread), gpuHelperPath () | device_profile.h (synthyboy/quality.h) for the status line, output_peak.h, play_log.h, poll_guard.h, controller.h, ui/qgroup.h; helper name synthyboy-overlay-gpu-<arch> | **ADAPTER** | Branding (helper name, log paths) and an extra-status hook for the quality line (step 8); becomes mpckit::Overlay |
| `instance_slots.h` | slots 0..15, instance colours <-> frames | kit/page_set.h (InstanceColours, kMaxInstances, kInstanceSteps), kit/instance.h | **GENERIC** | done in step 8 |
| `page_detector.h` | PageDetector, Detection (kMaxViews = 24) | instance_slots.h, standby.h | **GENERIC** | marker table and instance colours from the page set (step 8) |
| `overlay_layout.h / .json` | the generated table of the 24 views of SynthyBoy | generated by scripts/gen-overlay-layout-header.py | **ADAPTER** | per plugin: generate with --kinds / --namespace (step 9) |
| `ui_bind.h` | uibind::: table lookups, writes, formatValue, the modulation matrix helpers | ../ui/ui.h, curve_source.h, macro_names.h, synthyboy/modulation.h, params.h | **ADAPTER** | lookups / writes / formatValue forward to IParamSource + IInstance (step 3); the modulation part stays specific |
| `q_publish.h` | QSlots (the 16 Q slots of a page), usedGroups, setBlocks | ../q_router.h, ../ui/qgroup.h | **GENERIC** | QSlots::publish takes any instance (step 1); modRowTargets and setRowBlocks moved to `q_publish_mod.h` (step 1b, SPECIFIC: synthyboy/modulation.h) |
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
| `content`, `contentStore`, `contentRoots`, `requestContent`, `contentLoad`, `setLfoShape`, `setMacroName`, `tablesLoading`, `voiceStatus`, `missingContent` | stay in IInstanceView | SynthyBoy content, names and diagnostics (SPECIFIC) |
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
| `event_inbox.h, startup_timing.h, write_files.h` | MIDI event ring, start-up milestones, compile switch for diagnostic files | none | **GENERIC** | - |
| `synthyboy_mpc.cpp` | the VST2 shell (3031 lines) | the engine, state, presets, content; extractable generic blocks listed below | **SPECIFIC** | see "Blocks of synthyboy_mpc.cpp" |


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
| `scripts/build-mpc.sh`, `mpc-container-build.sh`, `gen-mpc-manifest.py` | build target `synthyboy_mpc`, names `synthyboy-arm32.so`, `Safian - VST - SynthyBoy`, `uid SfSB`, helper `synthyboy-overlay-gpu-<arch>` hard-coded | `--plugin` option with target / names / manifest (step 12, done: `mpc-plugin.conf`) |

## 3. The adapter interfaces (`shell-mpc/kit/`)

Header only, C++17, namespace `mpckit`, standard library only (plus the generic `q_router.h`, `host_strings.h`, `ui/`, `overlay/touch_decoder.h`, `overlay/ui_stack.h`). `kit.h` includes the lot. Threads: everything a page calls runs on the overlay thread and is lock free; the table interfaces are const and thread safe.

| Header | Types | Methods / contents |
| --- | --- | --- |
| `param_source.h` | `ParamDesc` (key, name, group, min, max, def, skew, interval, scale, unit, choices, numChoices, decimals), `Scale`, `Unit`, `IParamSource`, `ParamTable`, `formatValue`, `stepCountOf`, `unitLabel` | `IParamSource`: `count`, `desc (i)`, `indexOfKey`, `toNormalised`, `fromNormalised`, `constrain`, `valueText` (page text with unit), `hostText` (host display text), `stepCount` (0 = absolute knob, n >= 2 = stepped: the Q rule). `ParamTable` is a ready implementation over a constant array. |
| `instance.h` | `IInstance`, `IInstanceState`, `SettingSpec`, `SettingsStore` | `IInstance`: `params ()`, `param (i)`, `writeParam (i, normalised)`, `sampleRate`, `bpm`, `setQGroup (targets, n)`, `setQVirtual`, `takeQVirtual`, `qWriteCount`, `qStatus`, `setUiFullOpen`, `requestOverlayReport`, `readLive (void*, size_t)`, `contentSource ()`, `state ()`. The generic half of `IInstanceView`, same signatures. `IInstanceState`: `numSettings`, `setting (i)`, `get (i, v)`, `set (i, v)` (the settings a plugin saves in its chunk); `SettingsStore` keeps them in atomics with a `key=value;` text form. |
| `q_targets.h` | `IQTargets`, `QProxyBank` | `IQTargets`: `setGroup`, `target (q)`, `mode (q)`, `setVirtual`, `takeVirtual`, `writeCount`, `describe`, `proxyName / proxyLabel / proxyDisplay (q, out, cap)` (what the host shows for a proxy). `QProxyBank` implements them over `QRouter` and adds `value (q)`, `hostWrite (q, v, restoring, write)`, `poll (reportQ, reportReal, groupChanged)`, `readNorm`. |
| `page_set.h` | `PageDef`, `MarkerDef`, `Rect`, `InstanceColours`, `IPageSet`, `IPageFactory`, `validatePages`, `colourClose` | `IPageSet`: `numPages`, `page (i)`, `instanceColours`. `PageDef`: name, tab, kind, index, region, opaque, 2 markers, instance picture. `IPageFactory`: `make (PageDef) -> IView`, `readoutTargets`. `validatePages` = the checks of `gen-overlay-layout-header.py`. |
| `content_source.h` | `IContentSource`, `StaticContent` | optional list: `title`, `count`, `name (i)`, `selected`, `select (i)`. |
| `binder.h` | `Binder` | binds components to parameter keys: `bind (id, component, Type, key)`, `resolve`, `pull`, `apply (Change)`, `item`, `paramOf`; types Knob, Number, Toggle, Stepper, List, Fader, Inert. |
| `page_base.h` | `PageBase`, `PageInput` | the page contract of `PageView<Ui>`: `update`, `draw`, `touch`, `wheel`, `tick`, `fingerDown`, `dropGesture`, `forgetQGroup`, `topPage`, `qTargets`, `qSlotCount`; for the page author: `bind`, `addPart`, `group`, `relayout`, `layout`, `pullExtra`, `onChange`, `drawOver`, `modal`. |
| `branding.h` | `Branding`, `branding()`, `setBranding`, `tmpFile` | the prefix of the files the runtime keeps in /tmp, the name of the GPU helper and the variable that sets the number of Q-Link knobs: SynthyBoy's by default (step 8). |
| `display_thread.h` | `DisplayThread` | the thread that tells the host what changed: `add (name, fn)` registers a feature that runs each tick inside a `PollGuard`, `setBusy`, `run (stop, ready)` (step 11). |
| `overlay.h` | `Overlay`, `PageView`, `NoHooks` | `Overlay::attach (instance, pages, factory, &branding)` / `detach (instance)`: the runtime that draws the pages on the device (header only, Linux code; step 8); `PageView<MyPage>` is the view of a page of the kit. |
| `log.h` | `Level`, `devlog::Logger`, `devlog::Settings`, `devlog::Scope`, `MPCKIT_LOG`, `MPCKIT_SCOPE`, `MPCKIT_LOG_START` | developer log (`MPCKIT_DEV_LOG`, default 0 = compiled out). |
| `synthyboy_params.h`, `synthyboy_adapter.h` | `synthyboy_adapter::Params`, `descOf`, `State`, `Instance`, `PageSet` | SynthyBoy's implementations: forward to `paramTable ()`, `IInstanceView`, `layout::kViews`; every value is the old direct call's value (`valueText` = `uibind::formatValue`, `hostText` = `valueToText`, `stepCount` = `stepCountOf`). Not part of the kit proper. |

Which SynthyBoy dependency each interface replaces:

| Interface | Replaces (the N named dependencies) |
| --- | --- |
| `IParamSource` | `synthyboy::paramTable / paramCount / findParamByKey / toNormalised / fromNormalised / constrainValue / valueToText / ParamInfo`, `uibind::indexOfKey / info / normOf / formatValue`, `stepCountOf` |
| `IInstance` | the 11 generic members of `IInstanceView` |
| `IQTargets` / `QProxyBank` | the shell's `qRouter`, `qReadNorm`, `qPoll`, the proxy cases of `effGetParamName / Label / Display`, `macroParamName` (SynthyBoy keeps the macro-name hook) |
| `IInstanceState` | `outputLimiter / setOutputLimiter / padPressure / setPadPressure` and the chunk members |
| `IPageSet` | `overlay_layout.h` (`kViews`, `kNumViews`, `kMaxInstances`, instance colours) |
| `IPageFactory` | `pages::make`, `Controller::makeView`, `Controller::readoutTargets`, `filterTargets` |
| `IContentSource` | (new; SynthyBoy's browser stays specific) |

## 4. Migration plan (executed later, after the current UI changes have landed)

Rule for every step: SynthyBoy's behaviour and its tests stay unchanged (bit-identical picture and values). The step edits only signatures, includes and forwards; the adapter supplies exactly the old values. After each step the integration session runs the host tests named in the last column. Steps are ordered by dependency; a step may be committed alone.

| # | File | Function / place | Current | Replacement | Tests to run |
| --- | --- | --- | --- | --- | --- |
| 1 **DONE** | `overlay/q_publish.h` | `QSlots::publish`, `publishRaw` | `(IInstanceView& v, ...)` | `template <class Inst> (Inst& v, ...)`, the type is deduced, all callers unchanged | `qslots_host`, every `*_ui_host` |
| 1b **DONE** | `overlay/q_publish.h` -> `overlay/q_publish_mod.h` | `modRowTargets`, `setRowBlocks` | in `q_publish.h`, which includes `synthyboy/params.h` and `curve_source.h` for them | move both to the new header (includes `synthyboy/params.h`); add `#include "q_publish_mod.h"` to `env_ui.h`, `lfo_ui.h`, `macro_ui.h`, `filter_ui.h`, `mod_ui.h`, `osc_ui.h` (the 6 users); drop the two engine includes from `q_publish.h` | `qslots_host`, `env_ui_host`, `lfo_ui_host`, `filter_ui_host`, `mod_ui_host`, `osc_ui_host`, `macro_names_ui_host` |
| 2 **DONE** | `overlay/curve_source.h` | `class IInstanceView` | `class IInstanceView` with 24 methods | `#include "../kit/instance.h"`, `#include "../kit/synthyboy_params.h"`; `class IInstanceView : public mpckit::IInstance`; delete the declarations of `param`, `writeParam`, `sampleRate`, `bpm`, `setQGroup`, `setQVirtual`, `takeQVirtual`, `qWriteCount`, `qStatus`, `setUiFullOpen`, `requestOverlayReport` (identical signatures in the base: the `override`s in `OverlayView` and in `tests/fakes.h` stay valid; DECISION: `writeParam` and `setQGroup` are re-declared as `override {}` in `IInstanceView`, because the kit's interface has them pure and several tools and tests - `env_ui_demo.cpp`, `tour_dump.cpp` - rely on the old do-nothing defaults); add `const mpckit::IParamSource& params() const noexcept override { return mpckit::synthyboy_adapter::Params::get(); }` and `bool readLive (void* out, size_t bytes) const noexcept override { return out != nullptr && bytes == sizeof (synthyboy::LiveValues) && live (*static_cast<synthyboy::LiveValues*> (out)); }` (with the null check) | all overlay host tests |
| 3 **DONE** | `overlay/ui_bind.h` | `indexOfKey`, `info`, `normOf`, `writePlain`, `writeNorm`, `formatValue` | direct `synthyboy::` calls | `indexOfKey (key)` -> `Params::get().indexOfKey (key.c_str())`; `normOf (idx, plain)` -> `Params::get().toNormalised`; `writePlain` -> `v.writeParam (idx, Params::get().toNormalised (idx, Params::get().constrain (idx, plain)))`; `formatValue (const ParamInfo& pi, ...)` -> `mpckit::formatValue (mpckit::synthyboy_adapter::descOf (pi), ...)` (the same algorithm: delete the local copy and `noNegativeZero`); `info` stays (the pages read `ParamInfo`). The modulation helpers (`sourceName` ... `DestIndex`) stay. `uibind::params()` returns the kit's source. The parity test `overlay/tests/kit_parity_host.cpp` (CMake and `accept.sh`) compares the kit with the old direct calls (a verbatim copy of the old `formatValue` is its reference) for every parameter. The kit's text has one more case than the old one (a key that is exactly `pan`; no SynthyBoy parameter has that key) and a guard for a buffer of size 0. | `kit_parity_host`, `env_ui_host`, `mixfx_ui_host`, `mastervoice_ui_host`, `ui_gallery_host` |
| 4 **DONE** | `ui/ui.h` | umbrella | includes all 14 headers | new `ui/ui_generic.h` = `display.h led_list.h led_toggle.h note_mask.h list.h text_entry.h composite.h mixer_strip.h env_graph.h qgroup.h mod_board.h lfo_display.h lfo_model.h voice_grid.h theme.h`; `ui.h` = `#include "ui_generic.h"` + the existing specific includes (unchanged set) | `ui_gallery_host`, `ui_cov_new_parts_host`, `ui_fx_tables_host` |
| 5 **DONE** | `overlay/view.h`, `overlay/views.h`, `overlay/play_view.h`, `overlay/controller.h` | `ViewInput`, `IView::onTouch / onWheel / onTick`, `Controller` slots | `IInstanceView* instance / io`, `const synthyboy::LiveValues* live` | `ViewInput`: `const mpckit::IInstance* instance; mpckit::IInstance* io; const void* live` (opaque, valid for the call); `IView::onTouch / onWheel / onTick (..., mpckit::IInstance&, ...)`; every SynthyBoy view adds one line at the top of those overrides: `IInstanceView& io = static_cast<IInstanceView&> (base);` (all SynthyBoy instances are `IInstanceView`) and `static_cast<const synthyboy::LiveValues*> (in.live)` (DECISION: the casts of an update are three small functions in the new `overlay/view_cast.h`: `instanceOf (in)`, `ioOf (in)`, `liveOf (in)`, so the views do not repeat the cast expressions; `PlayView` and `PageView` take the `base` / `io` parameter of the three overrides as written); `Controller`: `InstanceSlots` holds `mpckit::IInstance*`; `v->live (lv)` -> `v->readLive (&lv, sizeof lv)`; `setUiFullOpen`, `requestOverlayReport` unchanged (in the base). Drop `#include "curve_source.h"` and `synthyboy/readout.h` from `view.h` (done: `view.h` includes `kit/instance.h`; `view_cast.h` includes the two). `Controller::attach / detach / detachWithin / slotOf` and `Service::attach / detach` take `mpckit::IInstance*`; `voiceStatus` (SynthyBoy's) is called through a `static_cast<IInstanceView*>` until step 8 | `views_host`, `controller_host`, `controller_review_host`, `page_view_host`, `display_host` |
| 6 **DONE** | `overlay/page_view.h` | `page::Input`, `PageView::update / onTouch / onWheel / onTick`, `names_` | `page::Input { live, targets, instance }`, `MacroNames names_`, `NamesScope scope (names_)`, `in.io->content ()` | `struct Input : mpckit::PageInput { const synthyboy::LiveValues* live = nullptr; }`; `template <class Ui, class Hooks = MacroNameHooks> class PageView`: `Hooks` has `void update (IInstance&)` (loads the macro names from `content ()`) and `auto scope ()` (the `NamesScope`); `MacroNameHooks` (SynthyBoy, in `macro_names.h`) is the old code; `struct NoHooks {}` for kit pages; `ui_.update (*in.io, ...)` casts `*in.io` to `IInstanceView&` for SynthyBoy pages via a `Hooks::Instance` typedef (`IInstanceView` / `mpckit::IInstance`). DECISIONS: the hooks class also has `Instance` and `loading (const IInstance&)` (the loading bar uses `IInstanceView::tablesLoading`, which a kit instance has not: `NoHooks::loading` is false); `NoHooks` is a complete small class (`Instance`, an empty `update`, an empty `scope`, `loading`), not an empty struct; `PageView<KitPage, NoHooks>` is exercised by a kit page in `page_view_host.cpp`. | `page_view_host`, `env_ui_host`, `mixfx_ui_host`, `qlink_panel_host` |
| 7 **DONE** | `overlay/curve_source.h` | file split | `IInstanceView` and `CurveSource` together | `overlay/instance_view.h` = `IInstanceView` (+ `ContentRoot`); `curve_source.h` keeps `CurveSource` and includes it; `view.h` includes only `instance_view.h` or nothing (after step 5: it includes `kit/instance.h`). DECISION: every header that only needs `IInstanceView` (the pages, `controller.h`, `ui_bind.h`, `macro_names.h`, `view_cast.h`, `kit/synthyboy_adapter.h`, `tests/fakes.h`) includes `instance_view.h`; only `filter_ui.h` and `views.h`, which use `CurveSource`, include `curve_source.h` | `curve_host` |
| 8 **DONE** | `overlay/instance_slots.h`, `page_detector.h`, `controller.h`, `service.h`, `drm_linux.h`, `gpu_clock.h`, `gpu/helper_transport_linux.h`, `q_device.h` | the runtime's names and tables | `layout::kViews / kNumViews / kMaxInstances / kInstanceSlotRgb ...` (generated header), `Paths` defaults `/tmp/synthyboy-overlay-*`, `gpuHelperPath ()` `synthyboy-overlay-gpu-<arch>`, `SYNTHYBOY_MPC_Q_KNOBS`, `device_profile.h` in `cpuStatus ()` | a `mpckit::Branding { const char* prefix; const char* helperName; const char* qKnobsEnv; }` (default `"synthyboy"` = the old strings) builds the `Paths` and the helper path; `Controller (..., const mpckit::IPageSet&, mpckit::IPageFactory&)` instead of `viewTable / numViews` + `makeView` + the static `readoutTargets` (an instance method); `InstanceSlots` / `PageDetector` take the colours and markers from the `IPageSet`; `static_assert (kNumViews <= kMaxViews)` -> `validatePages`; the `quality_profile` line of `cpuStatus ()` becomes an `extraStatus` hook SynthyBoy fills; `Service` becomes `mpckit::Overlay` (`attach (IInstance&)`, `detach`) with `Service::instance ()` kept as an alias for the shell. `template-mpc/src/overlay_hook.h` is then filled and `MPCKIT_OVERLAY` switched on; the template's CMake builds the runtime sources and the GPU helper DONE as follows (decisions the document did not make): `kit/branding.h` (`Branding`, `branding()`, `setBranding`, `tmpFile`; the default is SynthyBoy's strings, `Paths::forBranding` builds the paths, the zpos rest file, the GPU clock state file, the helper log, the memfd name, the helper path and the Q knob variable follow it; the helper's own debug files of `gpu-helper/main.cpp` and `q_router.h`'s step file keep their `/tmp/synthyboy-` names: they are not in the file list of this step); the `Controller` is GENERIC (`Controller (backend, frames, paths, timing, gpu, touch, const IPageSet&, IPageFactory&, deferViews)`, no renderer and no fixed cost: only the factory needs them) and knows no engine: the live block is read through `IPageFactory::liveBytes / viewTargets` (new, in `kit/page_set.h`), the diagnostics through `IInstance::diagnostics` (new; `IInstanceView` forwards `voiceStatus`), the instance type through `mpckit::IInstance`; SynthyBoy's old constructor is `SynthyBoyController` (`overlay/controller_synthyboy.h`, used by the host tests: it owns a `SynthyBoyPageFactory`); `PageDetector` and `InstanceSlots` take the colours from the page set (`classifyInstanceRgb (rgb, tolerance, colours)`, the default colours are SynthyBoy's); the `static_assert` on the number of views became the clamp to `Detection::kMaxViews` in the controller and `validatePages` in `Service::attach` (the checks of the generator, plus the instance colours against the background and the colours under the markers; the kind check was dropped: SynthyBoy's Play kind 5 is below `kFirstPageKind`); `Service` keeps its name and `Service::instance()` for the shell, `attach (instance, pages, factory, branding)` takes the plugin's tabs, factory and branding (the first attach decides) and `setStatus (f)` is the hook for the plugin's own status lines (SynthyBoy fills the quality profile in `synthyboy_mpc.cpp`; `service.h` no longer includes `device_profile.h`); the name `mpckit::Overlay` is the static facade in `kit/overlay.h` (`attach (IInstance&, IPageSet&, IPageFactory&, Branding*)`, `detach (IInstance&)`, `setStatus`) over `Service`; `PageView` became `BasicPageView<Ui, Hooks>` in `overlay/page_view_base.h` (generic: default `NoHooks`, no engine include) and `overlay/page_view.h` keeps SynthyBoy's `PageView<Ui>` = `BasicPageView<Ui, MacroNameHooks>`, `page::Input` (with the live values) and `MacroNameHooks` (moved from `macro_names.h`; `mpckit::PageView<Page>` in `kit/overlay.h` is the kit's alias); `template-mpc/src/overlay_hook.h` is filled (`DemoPageFactory`, `kBranding`), `MPCKIT_OVERLAY` no longer stops with an `#error`, the template's CMake adds the compile definitions, `Threads` / `dl`, the GPU helper (`add_subdirectory` of `shell-mpc/overlay/gpu-helper`, output renamed to `mpckit-demo-overlay-gpu`) and `make_package.py --helper32 / --helper64`. NOT verified: a build for the device (the Linux files `drm_linux.h`, `gpu_clock.h`, `helper_transport_linux.h`, `service.h` Linux parts, the helper) and any run; the generic runtime headers and the template's hook were syntax-checked without any engine include path (macOS, Linux code skipped by the compiler). | `controller_host`, `page_detector_host`, `stacking_host`, `qslots_host`, `gpu_backend_host`, `standby_host`, `layout_lint_host` |
| 9 **DONE** | `scripts/gen-overlay-layout-header.py`, `scripts/mpc-skin-extras/50_filter_overlay.py`, `82_play.py`, `83_pages.py` | `KINDS`, `LAYOUT_JSON`, parameter counts | hard-coded SynthyBoy kinds, layout path and `LFO_CURSORS, SPECTRUM_COLUMNS, PAD_PRESSURE_PARAMS` | generator: `--kinds name=n,...`, `--namespace`, `--out`, JSON key `"paramsBefore"`; modules: layout path from `ctx.meta` / an environment variable, `param_index` from the JSON (`paramsBefore`) with the SynthyBoy numbers as default; the template's `50_markers.py` then calls the real module functions. DONE as follows: the generator has `--kinds name=n,...`, `--namespace`, `--out` (exists), `--check` and names the JSON it was made from in its first line (SynthyBoy's default output is byte for byte what it was: `--check` was run); the generated kind constants are `kKind<Name>`, and `isPageKind` is `kind >= <lowest page kind>` for a table without SynthyBoy's `play` and `filterui`; the `meterRow` check only runs for a layout that has a `spectrum` view; the JSON keys are `paramsBefore` (default 38) and, DECISION (the document names one key; the Q proxies of another plugin follow "Overlay Instance" directly, SynthyBoy's follow its 27 Play selectors and proxies), `paramsAfterInstance` (default 27); `50_filter_overlay.py` has `layout_path (ctx)` and `params_before (layout)`, all three modules take the layout from `ctx.meta["layout_path"]` or the environment `MPC_OVERLAY_LAYOUT`, `83_pages.py` places `Q 1` by `paramsAfterInstance`, `82_play.py` is idle for a layout without `play` views, and the generic kind `page` is a known kind of `50_` and a page kind of `83_`; the template's `50_markers.py` is now two calls (it sets a skin meta with the overlay variant and the layout path and runs `50_filter_overlay.apply` and `83_pages.apply`). The two tests of `test_mpc_skin_overlay.py` for the new options were written, not run | `scripts/tests/test_mpc_skin_overlay.py`, `test_mpc_skin_extras.py` |
| 10 **DONE** | `overlay/pages.h` | `pages::make`, `filterTargets` | free functions on `layout::View` | `class SynthyBoyPageFactory : public mpckit::IPageFactory` with the same switch (`make (const PageDef&)` maps `kind` / `index`), `readoutTargets` = the old `Controller::readoutTargets` body. DECISIONS: step 10 was done BEFORE step 8 (step 8 needs the factory: the document says so in section 5, "needs 2, 5, 6, 7, 10"); the class takes the controller's renderer and fixed cost for the views of the first overlay (the filter curve); `pages::make` stays as free functions on `(name, kind, index)` and on `layout::View` (the gallery test uses them) and the factory's switch calls it for the page kinds; `Controller` makes its views through the factory (`kit/synthyboy_adapter.h` `pageDefOf` turns an entry of a table into a `PageDef`) and `Controller::readoutTargets` (static) forwards to the factory until step 8 makes it an instance method | `controller_host`, `page_view_host` |
| 11 **DONE** | `shell-mpc/synthyboy_mpc.cpp` | Q proxy members and cases (`qRouter`, `qReadNorm`, `qPoll`, `qNotifyGroup`, the Q cases of `getParameter`, `setParameter`, `effGetParamName/Label/Display`, `effGetParameterProperties`), display thread, `uiWrite / uiPoll` | inline in `PluginInstance` | `mpckit::QProxyBank qBank { params }` replaces `qRouter` + the Q cases (`qBank.value`, `hostWrite`, `proxyName ...`, `poll`); the display thread's registration of features moves to a kit `DisplayThread`. DONE as follows: `qBank` is a member of `PluginInstance` (`bind (&overlayView)` in its constructor: the values are read through the overlay view, i.e. the parameter mirror); `OverlayView::setQGroup / setQVirtual / takeQVirtual / qWriteCount / qStatus`, `qPoll`, `qReadNorm`, the Q cases of `getParameter`, `setParameter`, `effGetParameterProperties` and of `effGetParamName / Label / Display` use it (a macro keeps the name it was given: `macroParamName` is asked first, then `qBank.proxyName`); `kit/display_thread.h` is new: `DisplayThread (owner, threadName, { active, idle })`, `add (name, fn)` (a feature inside a `PollGuard`), `addUnguarded (fn)`, `setBusy`, `setAlwaysActive`, `run (stop, ready)`, `tick ()`; SynthyBoy's `liveLoop` registers its nine overlay features, the LFO cursors and the spectrum columns in the order they ran before, the template's `displayLoop` registers its three; the new host test is `shell-mpc/tests/display_thread_host.cpp` (CMake and `accept.sh` plain / ASan / TSan loop). NOT run: the host-sim acceptance; `synthyboy_mpc.cpp` was syntax-checked with the overlay build's macros (overlay, LFO live, notify, spectrum, pad pressure, and the two self-test macros) and without them | `host-sim` acceptance (`shell-mpc/accept.sh`), `q_router_host`, `play_proxy_host`, `display_thread_host` |
| 12 **DONE** | `scripts/build-mpc.sh`, `mpc-container-build.sh`, `gen-mpc-manifest.py` | target and names | `synthyboy_mpc`, `synthyboy-<arch>.so`, `Safian - VST - SynthyBoy` | `--plugin DIR` (default SynthyBoy): target, binary base name, manifest, skin inputs from the plugin's folder; `template-mpc/tools/make_package.py` is then replaced by `build-mpc.sh --plugin template-mpc`. DONE as follows (decisions the document did not make): the plugin folder holds `mpc-plugin.conf` (plain `NAME=value` lines, read and not sourced: `TARGET`, `CMAKE_SOURCE`, `CMAKE_ARGS`, `MODULE`, `BINARY_BASE`, `HELPER`, `MANIFEST`, `SKIN_SCRIPT`); the script defaults are SynthyBoy's and the SynthyBoy path of the three files is unchanged (the container gets extra `-e PLUGIN_*` only with `--plugin`); with `--plugin` the presets, content packs, preset browser and the SynthyBoy skin generation are skipped, the skin comes from `SKIN_SCRIPT` (or `--skins`), the package manifest from `MANIFEST` through `gen-mpc-manifest.py --package-manifest FILE` (`--folder-name` prints the folder name, `--plugin-dir` writes `plugin.json` with the `file32` / `file64` that exist and `version.xml`), the SDK validator runs; `mpc-container-build.sh` configures the plugin's own CMake project (`-S /src/<CMAKE_SOURCE>`, no SynthyBoy option) and builds `PLUGIN_TARGET`, strips `PLUGIN_MODULE`, builds the helper named by `PLUGIN_HELPER`; `template-mpc/mpc-plugin.conf` is new, the template's CMake got the version script / `-static-libstdc++` options the container's ELF checks need, and `make_package.py` is deleted (README updated). NOT run: no Docker, no build, no validator, none of the tests (written: `test_build_mpc.py` - the template with both architectures, with one, the usage errors, the container script's text, and the two texts of the old helper test updated - and `test_gen_mpc_manifest.py` - `PackageManifestTests`); `bash -n` and `py_compile` only, and the manifest modes were run by hand on the template's `plugin.json` | `scripts/tests/test_build_mpc.py`, `test_gen_mpc_manifest.py` |
| 13 | `kit/page_base.h` | `publishQ` | its own 25 lines | `QSlots` (q_publish.h, step 1) once the generic `q_publish.h` is in the kit | `mpckit_demo_pages_host` |

Executed so far: step 1 (commit `81d66352`). Not executed on purpose: steps 2 .. 13 touch files that other workers are changing now (`curve_source.h` changed 30 minutes before this branch was cut, `view.h`, `page_view.h`, `ui_bind.h`, `controller.h`, `synthyboy_mpc.cpp`, the page headers) or need the integration build to check them.

## 5. The starter template (`template-mpc/`)

"MpcKit Demo": a VST2 instrument (sine + state variable filter + attack / release), 9 parameters (Level, Pan, Pitch, Cutoff, Resonance, Filter On, Mode (4-way), Attack, Release), three pages.

| File | Purpose |
| --- | --- |
| `README.md`, `AGENTS.md` | human and agent instructions (rename, build, package, host requirements, rules, logging) |
| `CMakeLists.txt` | targets `mpckit_demo` (MODULE) and `mpckit_demo_pages_host`; **to add it to the build, one line in the top-level `CMakeLists.txt` after `add_subdirectory(shell-mpc)` (line 203, inside `if(SYNTHYBOY_BUILD_MPC_SHELL)`): `add_subdirectory(template-mpc)`** |
| `src/demo_params.h` | the parameter table (`ParamDesc` array), `ParamId`, presets |
| `src/demo_voice.h` | the sound (replace it) |
| `src/demo_instance.h` | `DemoInstance : mpckit::IInstance`: mirror, engine, `QProxyBank`, preset list (`IContentSource`), "Overlay Instance" value |
| `src/demo_plugin.cpp` | VST2 entry point, dispatcher, display thread, chunk |
| `src/demo_page_set.h` | `DemoPageSet : mpckit::IPageSet` (3 tabs, marker colours) |
| `src/overlay_hook.h` | the one place that attaches to the overlay runtime (step 8) |
| `src/log_compat.h` | `MPCKIT_LOG` / `MPCKIT_SCOPE` (`shell-mpc/kit/log.h`, if present; empty fallback otherwise) |
| `src/pages/main_page.h` | page 1 "Main": 3 knobs + a `LedToggle` in one Q group; a `NumberField` and a `LedList` in a second one |
| `src/pages/shape_page.h` | page 2 "Shape": `EnvGraph` fed by the plugin (attack / release), 4 knobs in 2 Q groups |
| `src/pages/list_page.h` | page 3 "List": `Stepper` with the list pop-up (`ListPopup`), a `Fader`, a PRESET button with a second `ListPopup` driven by an `IContentSource` |
| `skin/params.json`, `skin/pages.json`, `skin/overlay_layout.json` | inputs of the skin generators (the schemas of `docs/params.json`, `docs/kick-pages.json`, `overlay_layout.json`) |
| `skin/extras/50_markers.py` | markers, instance picture, hidden widgets, Q-Link mapping; reuses the helpers of `scripts/mpc-skin-extras/` |
| `tools/gen_skin.py`, `mpc-plugin.conf` | `gen_skin.py` runs `gen-mpc-ui.py` -> SDK `gen_ui.py` -> `apply-mpc-skin-extras.py --extras-dir`; `scripts/build-mpc.sh --plugin template-mpc` (reads `mpc-plugin.conf`) builds both architectures and assembles `Example - VST - MpcKit Demo/` with `plugin.json`, `version.xml` (step 12) |
| `package/plugin.json` | the manifest (`uid` `McKD`) |
| `tests/pages_host.cpp` | host test: table, tab colours, the three pages, a drag, the stepper list, a preset |

### Status

| Piece | State | Needs |
| --- | --- | --- |
| Parameters, sound, VST2 entry, display thread, chunk | complete, standalone | the MPC SDK header `mpc_vst2_abi.h` to compile (`MPC_SDK_DIR`) |
| Pages (3), `PageBase`, `Binder`, kit interfaces | complete, host-testable with the generic files only | nothing |
| Q proxies | complete (`QProxyBank` over `QRouter`) | - |
| Skin inputs and package tools | written against the repository's generators; **not run** in this phase (no build, no tests were allowed) | `MPC_SDK`, Python 3 |
| Drawing the pages on the MPC (overlay runtime, page detector, touch, mouse, standby, GPU helper) | **linked by `-DMPCKIT_OVERLAY=ON`** (a Linux cross build): `overlay_hook.h` calls `mpckit::Overlay::attach` with `DemoPageFactory` (`mpckit::PageView<Page>` of each page). **Never built or run**: the headers were syntax-checked without any engine include path, the Linux parts of the runtime and the GPU helper were not compiled | a cross build and a run on the device |
| `log.h` | optional (another worker adds `shell-mpc/kit/log.h`) | the debug level identifier in `log_compat.h` (`DEMO_DEBUG`) must match `LOGGING.md` |

No runtime code was copied into the template: the three one-liners of the "Overlay Instance" frame numbers in `demo_instance.h` and the display thread of `demo_plugin.cpp` are the only places that repeat what `synthyboy_mpc.cpp` does; both go to the kit in steps 8 and 11.

## 6. What a template user writes, and what the kit does

| The user writes | Where |
| --- | --- |
| the parameters (key, name, range, unit, choices) | `demo_params.h` + `skin/params.json` |
| the sound | `demo_voice.h` (reads the mirror) |
| the pages: components, binds to keys, Q groups, rectangles | `pages/*.h` (10 .. 40 lines each) |
| the tabs: names, marker colours | `demo_page_set.h`, `skin/overlay_layout.json`, `skin/pages.json` |
| the identity: name, vendor, four character uid, version | `package/plugin.json`, `demo_plugin.cpp` |
| optional: lists (`IContentSource`), settings (`IInstanceState`), display feeding (`pullExtra`) | the instance, the page |

| The kit does | Where |
| --- | --- |
| drawing on a free display plane over the host's picture (CPU, or GPU in a helper process), pacing, rotation, standby dim, the host's Q-Link panel strip | `shell-mpc/overlay/` (step 8) |
| finding which page is on screen and which instance it belongs to (markers, instance colours) | `page_detector.h`, `instance_slots.h` |
| the touch screen (multitouch decoder), the mouse and pointer, the wheel | `touch_*.h`, `mouse_*.h`, `PageBase::touch / wheel` |
| components: drawing, gestures, values in, `Change` out; one finger gesture capture; long press; double tap = default | `shell-mpc/ui/`, `PageBase` |
| values <-> components, writes, reports to the host (automation) | `Binder`, `IInstance::writeParam`, the display thread |
| the 16 Q-Links: proxies "Q 1" .. "Q 16", groups of four, the active group on Q 1 .. 4, the next ones on 5 .. 16, stepping of stepped targets, names / units / texts for the host | `QRouter`, `QProxyBank`, `QGroup`, `PageBase::publishQ` |
| standby, the "Overlay Instance" bookkeeping, hiding the host widgets while a page is up | `overlay/standby.h`, skin extras |
| the skin (tabs, widgets, markers, Q-Link mapping) and the package (`plugin.json`, `version.xml`, folder name, validator) | `tools/`, the repository's generators |

## 7. Open risks

1. **The page detector depends on the host's skin markers.** The overlay finds the page by reading pixels of the host's framebuffer. Anything that covers or alters the markers (a pop-up, a menu, the host's Q-Link panel, a standby dim, a new firmware that re-scales or filters the skin images, a different host theme background than `1e1d1e`) hides the page. SynthyBoy handles the panel strip and the standby blur (measured); another plugin inherits those assumptions. Marker colours must not collide with another plugin's markers in the same project (two overlay services in one process would also fight over the one display plane: the runtime is a per-library singleton; two MpcKit plugins loaded together are untested).
2. **The host's parameter count and the Q proxies are fixed at load.** The skin binds to parameter NUMBERS; the order `[own][Overlay Instance][Q 1 .. 16]` must never change between versions of a plugin (saved projects store the Q-Link mapping by index). A Q proxy's name, unit and text change at run time only because the plugin calls `audioMasterUpdateDisplay` (measured on the Key 61 for SynthyBoy; not for other plugins). The host limit of 32 bytes for name strings is a convention (`host_strings.h`), not a measured size.
3. **arm32 / arm64.** The MPC loads the binary that matches its CPU (32-bit ARMv7 hard-float: Key 61 and others; AArch64: XL and others); two binaries per package, built in the Debian 12 container (glibc 2.36). The template was not built for either in this phase. The GPU helper (a separate executable, `synthyboy-overlay-gpu-<arch>`) must be packaged next to the plugin; a plugin without it falls back to CPU drawing.
4. **Host versions.** Everything about the overlay (the free display plane, the framebuffer read, the Q-Link panel look, the standby dim, the 32 tab limit with 34 tabs accepted) was tested on MPC OS 3.10.1 on the MPC Key 61 and the MPC XL only. Other MPCs (Live, One, ...) and other firmware versions are untested; the `/tmp` files and `/data/Settings/MPC/MPC.settings` are read-only probes that may not exist.
5. **Not compiled, not run.** This branch was written without a compiler (rule of the integration session): the kit headers, the template and the one migration step have never been built. Expect small compile errors; section 8 says what to build first.
6. **Duplicated algorithms until the migration is done**: `stepCountOf` exists in the kit and in `q_step_rule.h` (the kit's source forwards to the old one: `kit_parity_host` compares them); `formatValue` is the kit's only since step 3; `PageBase::publishQ` repeats `QSlots::publish` (step 13).
7. **The template's display thread** runs on the kit's `DisplayThread` since step 11 (every feature inside a `PollGuard`); it still has a plain join at close and no restore window after a chunk restore (SynthyBoy's `restoreActive`): fine for an example, to be added for a product.

## 8. What the integration session builds first

1. `mpckit_demo_pages_host` (native, `EXCLUDE_FROM_ALL`; `add_subdirectory(template-mpc)` first, `MPC_SDK_DIR` is not needed for this target): it compiles `shell-mpc/kit/*.h` (except `synthyboy_*`), the three pages and `ui/` components. Expected warnings: none intended; likely first errors: a missing `#include <cmath>` / `<cstdio>`, a brace-initialisation of `ParamDesc` (field order in `demo_params.h`), `-Wpedantic` on the `kParams` initialiser. Run it: expected output ends with `OK`.
2. `mpckit_demo` (needs `MPC_SDK_DIR`; native Linux or the cross toolchain): `src/demo_plugin.cpp` against `mpc_vst2_abi.h` (the VST2 names are taken from `synthyboy_mpc.cpp`).
3. The one edited existing file: `overlay/q_publish.h` (step 1): `qslots_host` and every overlay host test that includes it (`env_ui_host`, `filter_ui_host`, `lfo_ui_host`, `mixfx_ui_host`, `mastervoice_ui_host`, `mod_ui_host`, `osc_ui_host`, `macro_names_ui_host`).
4. A compile-only check of the SynthyBoy adapter: a translation unit that includes `kit/synthyboy_adapter.h` (needs `core/include`); nothing in the build includes it yet.
5. The skin: `python3 template-mpc/tools/gen_skin.py --out <dir> --sdk $MPC_SDK`; then `$MPC_SDK/tools/validate.py` on the package from `scripts/build-mpc.sh --plugin template-mpc` (the script runs it) (expect the missing `file32` / `file64` finding of a one-architecture package).
