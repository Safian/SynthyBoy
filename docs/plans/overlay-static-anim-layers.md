# Plan: static layer + animation layer in the plugin-drawn overlay (helper-internal)

Status: plan only, from code and document reading (no build, no test, no device run). Owner request 2026-11-21 (`TODO.md:14`, section "Rajzolás"); it is to be done AFTER job g117 (single-plane takeover; no g117 text exists in the repository, see open question Q4).
Conventions: `A:l` = file:line opened for this plan. "cell" = scene protocol 8 cached cell (`gpu/scene.h:84-91`). "live op" = a scene op outside any cell (drawn every picture). Key 61 = Mali-T760, 30 pictures/s (`docs/MPC.md:1069`).

## 0. Finding that shapes the whole plan

**Most of the requested architecture already exists** (protocol 8, `kProtocolVersion` 8 at `gpu/scene.h:25`): a persistent static layer (two textures the size of the picture, `main.cpp:1519-1555`), per-cell damage by (rectangle, hash) (`gpu/cell_plan.h:20-26,135-170`), live ops drawn every picture over the layers, partial pictures by EGL buffer age (`main.cpp:1569-1597`, `gpu/cell_cache.h:39-80`), and an ad hoc anim/static state machine in two elements: the data of the oscillator display and of the LFO display is a live op while "hot" (changed in the last 350 ms) and a cell again afterwards (`osc_display.h:201,295`; `lfo_ui.h:1083,218,253`; `lfo_display.h:123-157`; doc `MPC.md:1068`). Measured effect of protocol 8 on the Key 61 (trial 19, `MPC.md:289-304`): compact view, 3 held notes, full detail: mean 30.93 ms -> 3.94 ms per picture, p99 64 -> 28 ms; full screen, knob dragged: mean 39.62 -> 15.92 ms, p99 64 -> 41 ms.

So the work is NOT "build a layer system". It is: (a) make the per-element state machine one shared thing, (b) split the coarse cells of the elements that still bake everything again on every change (FX, filter, mixer meters, shape editor, envelope editing), (c) measure per element before and after, (d) prove pixel identity. The helper changes are small (one switch, optional counters).

## 1. What the helper redraws per frame today

| # | Fact | Where |
|---|---|---|
| a | The controller sends a COMPLETE scene on every tick that has a change or `needFrame_`: `view.describe (sb, ...)` builds all ops of the view (static ones too), then `gpu_->submit`. No change and no `needFrame_`: no scene, the helper sleeps (idle = no draw, plane verified once a second). Rate: one scene per grid slot (30/s on the Key 61), never faster than 5 ms. | `controller.h:1031,1050,1055,1063-1070,1652`; `MPC.md:869` (30/s), `:304` (idle) |
| b | The scene is a display list: ops (32 bytes each), text pool, vertex pool; limits 2048 ops, 65536 vertices, 2048 per curve; only the used part is copied under a seqlock ("a few kilobytes"). | `gpu/scene.h:28-32,98-120,669-676`; `MPC.md:1058` |
| c | Helper per scene: `readScene`, `planCells` (cells matched by rectangle + hash, at most two layers), `renderLayers` (damaged cells and the cells lying over them are redrawn into the layer inside a scissor box), `buildScene` (vertices of the live ops + one textured quad per layer), partial boxes by buffer age, `glFinish`, swap, `SETPLANE`. | `main.cpp:1741-1800`, `:1599-1656`, `:1569-1597`, `:840-860` |
| d | The hash of a cell is made by the builder from rectangle, ops, text and curve rows (not their pool offsets): any value change of a cell makes it damage; a cell of the previous scene that is gone is damage too. | `gpu/scene.h:260-275`; `cell_plan.h:135-170` |
| e | Layer rule: cells before the first drawing op that is not a cell are layer 0, cells after it layer 1; a cell after a second such group makes the scene uncached (drawn whole). Style / paint / curve / clip / clear ops do not start a group. | `cell_plan.h:35-37,89-95`; `scene.h:90-91` |
| f | The layers are clip-independent: the cells of a layer are rebuilt without the scene's clip op and the clip (Q-Link panel strip, `edgeX_`) is applied when the layer is put on the picture. A changed clip forces a whole picture, not a rebuilt layer. | `main.cpp:1620-1646` (cell ops built from `begin + 1`, own scissor), `:1672,1693` (`clipOn_`), `cell_cache.h:63-73`; `page_view_base.h:201`, `play_view.h:436` |
| g | The CPU reference ignores cells (only resets style and paint) and draws the ops in order; `gpu_scene_host` compares every view's CPU picture with the scene reference (largest channel difference <= 3, no pixel above 2). | `scene_cpu.h:133`; `tests/gpu_scene_host.cpp:51-62` |

What is animated today, and how it is drawn:

| Element | Static part (cell) | Live part (every picture) | Where |
|---|---|---|---|
| LFO display (Play compact, LFO page) | ground (block, grid), data cell (shape/trace) | cursor of the voices; data too while `displayHot_` (350 ms after a change) | `lfo_display.h:133,155-157,274`; `lfo_ui.h:218,253,262-270,1083` |
| Oscillator display | block cell, data cell | data while `hot_` (350 ms), read-outs (grains, heads) | `display.h:195,217,426`; `osc_display.h:201,295` |
| Envelope graph | whole graph cell | running dots | `env_graph.h:243-247` |
| Shape editor canvas | ground cell (inside `lfodisplay::draw`), then ONE cell around points, handles, mod bars | the curve data (`cached = false`) | `shape_editor.h:276-277` |
| FX display (EQ, comp, delay, filter, distortion) | ONE cell: block + title + grid + curve | nothing | `fx_display.h:150-170` |
| Filter display | ONE cell: block + curve / formant pad | nothing | `filter_display.h:183-196` |
| Mixer strip fader + level meter | ONE leaf cell per strip (default `Component::draw`), meter fill inside | nothing | `core.h:507-509`; `mixer_strip.h:147,176-200` |
| Knob, button, label, frame (every leaf) | one cell each | nothing | `core.h:505-509` |

Consequence: in the FX page, filter page, mixer and shape editor, every change of the animated value changes the hash of a cell that also contains the block, texts and grid; the helper redraws that whole cell into the layer (plus every cell lying over its box, e.g. the page ground), then draws the layer onto the picture: the bake and the composite are paid on EVERY moving picture. `MPC.md:1068` says the same for a cell that changes every picture ("only costs the layers under it too"). The coarse cell is the defect; the helper machinery is right.

Measured numbers that exist (all Key 61, trial 19 rows, `MPC.md:289-304`): idle 0.93-0.98 pictures/s (mean 24-25 ms = a waking GPU in the forced bench pictures, `MPC.md:304`); compact + 3 notes 26.6 /s mean 3.94 ms p99 28; full screen + 3 notes mean 1.24 ms p99 27; full screen knob dragged 17.4 /s mean 15.92 ms p99 41 (reduced detail: 10.71 / 36). Open items in `TODO.md:16,18`: editor curve drag time not measured; knob-turn p99 36-41 ms against the 14 ms budget (`MPC.md:253`). NOT measured anywhere: per-element cost of FX drag, filter cutoff drag, mixer meters, LFO editor drag, the plugin-side `describe` cost per scene (only the smoothed `render_ms` exists: `controller.h:1077,1554`), the memory the layer textures take.

## 2. Design

### 2.1 Layer model (no new texture, no new protocol op)

* STATIC layer = the existing layer 0 / layer 1 textures (RGBA8, premultiplied, picture size: panel 800 x 1280 = 4 096 000 bytes = 3.9 MiB each; `MPC.md:173` for the panel size, `main.cpp:1519-1555`). Persistent across pictures; owned by the helper, never published.
* ANIMATION layer = the live ops (ops outside cells), drawn after the layer that lies under them and before the layer that lies over them (`cell_plan.h:89-95`). It is not a texture; it is the draws of the scene, painted only inside the partial boxes (live op bounding boxes of this picture and the previous one, merged to at most 4, whole picture above 70 percent: `main.cpp:1569-1597`, `cell_cache.h:46-79`).
* COMPOSE per picture (unchanged): per partial box: quad of layer 0, live ops, quad of layer 1 (`main.cpp:840-860`). So "static texture as one quad then the animated ops" is today's code; the copy-dirty-rects-only variant is `partialBoxes`.
* Decision: the animation layer stays the scene's live ops. A new offscreen animation texture would add a full-size texture (3.9 MiB) and a blend pass for no gain: the live ops are already restricted to boxes.

### 2.2 The element state machine (plugin side; the helper stays hash-driven)

One shared class (new, `ui/anim_gate.h`), replacing the two ad hoc copies (`osc_display.h:201`, `lfo_ui.h:1083`):

| State | Meaning | Drawn as | Enter | Leave |
|---|---|---|---|---|
| `Static` | final picture known | cell (baked in the layer) | default; end of `Ending` | any `touch (now)` |
| `Anim` | the value changes now | live ops (no cell) | `touch (now)`: a drag event, a parameter change, a playing voice, a meter above its floor | `now - lastTouch >= hold` |
| `Ending` | one scene | the final state as a CELL (this is the bake: the helper sees a new cell, redraws it in the layer inside its box) | first scene after the hold has expired | next scene: `Static`; a `touch` in this scene returns to `Anim` |

`hold` = 350 ms as today. Properties: (1) the ending content must be exactly what the element would have drawn live (same ops, same order), so the bake changes no pixel beyond rounding of the layer path (<= 3 per channel, same tolerance as `gpu_scene_host`); (2) the gate has no helper side: a cell appears and disappears in the scene and the existing damage logic does the rest (cell gone = its old rectangle redrawn without it, cell new = drawn); (3) the one-frame cost of the bake is the cell's box times the cells lying over it, paid after the movement stopped (hold 350 ms), not during it.

The helper keeps no per-element state. Rejected: helper-side auto-detection (promote a cell that changes in N consecutive pictures to live): it only saves the composite, the coarse cell still redraws block and texts every picture, so the split below is needed anyway; keep as a fallback idea, not part of this plan.

### 2.3 Split granularity: how an element declares its parts

`Component` gets one more virtual, `drawAnim (sb)` (default empty) and `bool animating (now)`; `draw (sb)` keeps meaning "the static part". Pages draw in the order the layers want (already the shape of `PlayView::describe`: `play_view.h:469-480`, `lfo_ui.h:180-284`): pass 1 every static cell, pass 2 every `drawAnim` of the elements in `Anim` state, pass 3 the cells that lie over (popups, full-screen button: layer 1). Reason: a second group of live ops after layer-1 cells makes the scene uncached (`cell_plan.h:89-95`), so all live ops of a page MUST be one contiguous pass; with eight mixer strips each emitting its meter next to its cell the scene would break. The CPU reference draws the ops in the same order, so a split is pixel-identical on the CPU only when the anim ops of an element are drawn after its static ops and nothing static of ANOTHER element overlapped them in the old order (the gallery compare, section 2.6, decides).

Ops per element in each state: `Static` = [cell: block, grid, texts, final curve]; `Anim` = [cell: block, grid, texts (unchanged hash, so not redrawn)] + [live: the curve, fill]; `Ending` = [cell: block...] + [cell: final curve]. The curve gets its own cell in `Static`/`Ending` so a later change redraws only the curve box, not the block and texts.

### 2.4 When the baked image is invalidated (all by hash or size, none by a new rule)

| Event | Mechanism | Verified |
|---|---|---|
| Parameter change that alters a static op | the cell's hash changes; if it keeps changing it is `Anim` | `scene.h:260-275` |
| Page / view change | new scene has other cells; unmatched cells are damage; a view change re-shows the region | `cell_plan.h:135-170`; `main.cpp:680` (`dropLayers` in the surface teardown) |
| Resize / rotation | layer texture size differs from the buffer: layers dropped and made again | `main.cpp:1521-1525,1545-1555` |
| Theme / look / detail level (`theme::L()`, `theme::D()`) | colours and detail enter the ops, so the hashes change | `MPC.md:1074`; HYPOTHESIS for the detail switch: check that a full-detail to reduced switch changes the ops of every cell (task T9 test) |
| Standby dim | the builder's hash is changed on purpose so cells are redrawn | `standby.h:482` |
| Q-Link panel overlap (clip) | layers are clip-independent (row f above); the clip changes the whole picture, not the layers | `main.cpp:1672,1693`; `cell_cache.h:63-73` |
| Page change in the middle of an animation | the element disappears from the scene (cell gone or live ops gone); nothing is baked for it; its gate object lives in the page's UI that is closed by the `UiStack` group switch, so it restarts `Static` when the page opens again | `controller.h` page switch via `View::pageElement`, `MPC.md:1316` ("a change of tab closes the detail pages"); behaviour of the gate object itself: new, test T2 |

### 2.5 Memory and buffers; g117

* Today: 2 layer textures = 2 x 4 096 000 = 8 192 000 bytes (7.8 MiB) when both are used (`ensureLayer` makes a layer when `plan.layer[l]`: `main.cpp:1607-1612`), plus the Feedback texture of the picture size (3.9 MiB, always made: `main.cpp:660`), plus the GBM swap chain (buffer age is 0 for two pictures and 2 afterwards = two buffers in rotation: `MPC.md:304`), plus the glow layers at 1/2 .. 1/8 size when a bloom is used. This plan adds NO texture. Key 61 MemAvailable was 2.54 GB with three tracks (`MPC.md:1485-1495`), so 8 MiB is not a memory risk by the numbers; GPU-visible CMA / shared limits of the Mali-T760 are NOT measured: HYPOTHESIS, decided by M0 (`/proc/meminfo` and the helper RSS before and after).
* Publish protocol: the published buffer is always the composite (layer quads + live ops painted into the swap-chain buffer, whole or by boxes), never a layer. The persistent static texture is the helper's own GL texture, outside the swap chain, so the buffer rotation is independent of it (`main.cpp:1789-1830`: `glFinish`, swap, lock front buffer, `SETPLANE`). Partial paint relies on the buffer age (`partialOk_`, `main.cpp:1569-1597`); when the age is unknown (0) the whole picture is composed (one quad per layer + live ops), still complete and correct, only slower.
* g117 interplay (ASSUMPTION, no source in the repo): the helper still renders into its own buffer and hands one finished buffer per frame to the host path. Then nothing changes here. If g117 changes the buffer source so that the EGL age is not available, `partialBoxes` returns the whole picture and the gain of this plan shrinks to the saved bake redraws, never to a wrong picture. T0 reads g117's final code before any edit of `main.cpp`.

### 2.6 Pixel identity: how it is proved

1. CPU path. `scene_cpu.h` ignores cells (`:133`), so the cell / live decision never changes the CPU picture; what changes it is the ORDER of ops when an element is split. Proof: render `ui_gallery_host` (`scripts/render-ui-gallery.py`, plus `ui_gallery_extra_host.cpp` states, `lfo_editor_*`) before the first task (directory A) and after each element task (directory B); `scripts/gallery-compare.py A B` must exit 0 (identical). A difference is reviewed by eye and either fixed (order) or recorded as intended. Also `gpu_scene_host` (`checkSameAsCpu`) for every view.
2. Gate states. A host test (new, `anim_gate_host.cpp`) renders each split element in `Static`, `Anim`, `Ending` with the same data and asserts the three scenes give the same `renderSceneReference` pixels (maxDiff 0 on the CPU reference) and that `Ending` has exactly one more cell than `Anim`.
3. GPU path (device only). New helper switch `/tmp/synthyboy-overlay-nocells` (read next to `partialOk_`, `main.cpp:1592`; `planCells` forced false = whole scene drawn without layers). With the existing dump (`/tmp/synthyboy-overlay-gpu-dump`, `MPC.md:872`, once a second) take the frame of the same held state with cells and without, and the frames just before and just after the bake; compare with a small script (RGBA premultiplied, bottom-up rows). Pass: largest channel difference <= 3 and 0 pixels above 2 (the tolerance of `gpu_scene_host.cpp:51-62`; layer compositing rounds in 8 bits).
4. Mac logic tests: `cell_cache_host.cpp` and `cell_plan_host.cpp` stay green (nothing in `gpu/` logic changes except possibly the switch).

## 3. Elements: static, animated, trigger, end condition

All triggers use `touch (now)` of the gate; end = no touch for `hold` (350 ms) unless noted. Names in CAPITALS in the last column are things the owner must confirm (section 5).

| Page / element | Static | Animated (trigger) | End condition | Today |
|---|---|---|---|---|
| LFO compact + LFO page display | frame, ground, grid, buttons, labels, the finished wave | the wave line while the shape / rate / smooth / modulation changes; the cursor of every sounding voice (a note is held) | wave: no change for hold; cursor: last voice released | already so (`lfo_ui.h:1083`); cursor cell-free. Unify on the gate (T3) |
| LFO editor (`shape_editor.h`) | page ground, canvas ground + grid (cell), tool bar, buttons (BACK, tools, SHAPES, MORE, UNDO, DELETE, MOD), Q groups, LED toggles | the curve while a point or handle is dragged or a tool draws; the dragged point and handle; the PHASE marker while dragged; selection rectangle | finger up + hold, then the curve and points are baked as cells | curve live always (`:276`), points cell redrawn on any change (`:277`); no gate. T7 |
| ZOOM / full-screen button of the LFO | static (an `Expand` menu button, a leaf cell) | never | - | `lfo_screen.h:24` is the only button of that kind found; there is NO zoom button in `shape_editor.h` (grep). Which control the owner means: Q1 |
| BACKGROUND | page ground, display block, group frames: static | never | - | cells already; Q1 |
| Envelope (`env_graph.h`, `env_ui.h`) | graph block + curve when not edited | running dots (note held); the curve while a stage knob moves | dots: voices end; curve: hold | dots live already (`:247`); curve not gated. T6 |
| Filter display (`filter_display.h`) | block, grid, axis texts, the response curve at rest | the curve while cutoff / resonance / drive / type changes (knob, Q-Link, modulation) | hold | ONE cell, all redrawn (`:183`). T5 |
| FX page (`fx_display.h`, `fx_slot_editor.h`, chain bar, band board) | everything except the curve of the selected slot | only the curve (EQ / comp / delay / filter / distortion) while a control of that slot is dragged or Q-Linked | hold (owner: "only the curve dragging") | ONE cell per display (`:150`). T4 |
| Mixer meters (`mixer_strip.h`) | strip: scale texts, track, thumb, frame | the meter fill + peak line per strip while the level is above the floor or still falling; the fader thumb while dragged | level at floor for hold | meter inside the strip cell, so the strip is redrawn on every level step (`mixer_strip.h:176-200`, `core.h:507`). T8 |
| Play page | ground, oscillator block, env / LFO panel blocks, full-screen buttons | oscillator data while the WT position moves (gate exists: `osc_display.h`), read-outs, env dots, LFO cursor | as above | mostly done (`play_view.h:437-501`). Gate unification T3 |
| Mod, Macros, Master, Voice pages | all (knobs at rest are cells) | the knob / value being dragged is a cell that changes every picture: treated as `Anim` only if M0 shows it costs more than 3 ms (decision by measurement, not by rule) | - | leaf cells (`core.h:505-509`) |
| Spectrum (Macros tab, `kKindSpectrum`) | frame | curve every picture | never ends while sound plays | live already (`views.h:636-645`) |

## 4. Task list

Rules for the whole list (`Safian/CLAUDE.md` section 4): a worker task checks only its own file(s) (one host test target); no full build, no device. The coordinator builds the merged tree once (`buildslot.sh`, `-j4`) and runs the Mac tests; the Key 61 tasks are run by the coordinator (the device is exclusive, `.agents` device lock). Commit after every step.

**T0. Read g117 and fix the baseline commit.** Files: none. Change: read the merged g117 diff of `gpu-helper/main.cpp`, `controller.h`, `service.h`, `gpu_overlay.h`; list what changed in the render loop, buffer source and publish. Test: none. Acceptance: a 10-line note appended to section 2.5 of this document saying whether (a) buffer age is still available, (b) layers are still helper-owned textures, (c) one finished buffer per frame; every `main.cpp:` reference below is re-checked.

**T1. Pixel-identity baseline (host only).** Files: none (scripts only). Change: run `scripts/render-ui-gallery.py` and `render-ui-gallery-extra.py` on the T0 tree into `/tmp/anim-A`. Test: second run into `/tmp/anim-A2`, `gallery-compare.py` exits 0 (the renderer is deterministic). Acceptance: A and A2 identical; directory kept for T4-T8.

**T2. `AnimGate` (new `ui/anim_gate.h`) + test.** Change: the class of section 2.2: `touch (now)`, `state (now)` returning `Static / Anim / Ending`, `hold` constructor argument (default 350 ms), `Ending` lasts exactly one `state()` read after the hold expired (the read that renders the scene), idempotent for repeated reads at the same `now`. Test: `tests/anim_gate_host.cpp` (add to the overlay test list): sequences touch / read with fake clocks; page-close mid animation (`reset()`) returns `Static` without an `Ending`. Acceptance: all branches pass; no allocation, no clock inside the class (the time is passed in).

**T3. Replace the two ad hoc gates.** Files: `overlay/osc_display.h` (`hot_`, `dataMs_`, `:201,295`), `overlay/lfo_ui.h` (`displayHot_`, `displayChangedMs_`, `:218,253,1083`). Change: use `AnimGate`; behaviour identical (350 ms). Test: existing `lfo_ui_host`, `osc_ui_host`, `views_host` unchanged and green; `gallery-compare` vs T1 exit 0. Acceptance: no diff in any scene (the `describe` ops of a static view are byte-identical before and after: assert in the test by hashing `SceneData`).

**T4. FX display split.** Files: `ui/fx_display.h`, `ui/fx_slot_editor.h` (gate fed by the slot's control changes), `ui/fx_screen.h`. Change: `draw` = cell of block + title + grid + axes; the curve + fill in `drawAnim` while `Anim`, as its own cell in `Static` / `Ending`; the FX screen runs the anim pass after the static pass (section 2.3). Test: `mixfx_ui_host` + new gate-state cases (section 2.6 item 2); `gallery-compare` vs T1 exit 0 for the FX states. Acceptance: Static and Ending scenes give identical CPU pixels; the `Anim` scene has the block cell hash equal to the `Static` one (so the helper does not redraw it).

**T5. Filter display split.** Files: `ui/filter_display.h` (`draw` at `:183`, the formant pad at `drawFormantPad`). Same change and test as T4 with `filter_ui_host`. Acceptance: same as T4.

**T6. Envelope graph curve gate.** Files: `ui/env_graph.h` (`:243-247`), `overlay/env_ui.h`. Change: the curve is `Anim` while a stage value changes (knob, Q-Link, modulation); the dots stay in `drawLive`. Test: `env_ui_host`, gallery compare. Acceptance: as T4.

**T7. Shape editor split.** Files: `ui/shape_editor.h` (`:276-277` and the marker / handle drawing), `ui/lfo_display.h` (the `cached` flag becomes the gate state), `overlay/lfo_ui.h`. Change: canvas ground + grid cell (already), curve: `Anim` while a gesture runs (`touchDown..touchUp`, tool strokes, SOFT / BEND), cell otherwise; points / handles / mod bars: one cell per point class is NOT wanted (up to 128 points), so ONE cell while `Static`, live ops while `Anim`; the marker handle live while dragged. Test: `ui_lfo_host`, `lfo_ui_host`, gallery `lfo_editor_*` compare vs T1. Acceptance: exit 0; a drag gesture test asserts the scene during the drag has no cell whose hash changes between two consecutive events.

**T8. Mixer meter split.** Files: `ui/mixer_strip.h` (`drawBody` `:147-200`), `ui/mixer_screen.h`, `ui/composite.h` if the two-pass draw needs a hook. Change: the strip cell keeps scale, track, thumb at rest; meter fill and peak line move to `drawAnim` of the strip, emitted by the screen in ONE pass after all strips (section 2.3), `Anim` while the level is above the floor or falling. Test: `mixfx_ui_host`, `layout_lint_host` unchanged, gallery compare for the mixer states. Acceptance: 8 strips with moving meters give a scene that `planCells` keeps cached (assert `planCells` true, one live group): new test in `cell_plan_host.cpp` or `mixfx_ui_host.cpp`.

**T9. Helper: switch and counters (no protocol change).** Files: `gpu-helper/main.cpp` (switch next to `:1592`), `controller.h` status lines (`:1589-1594` already print `ops`, `cached_ops`, `redrawn`, `painted_px`, `full_frames`: live ops = `ops - cached_ops`, bakes = `redrawn`; nothing to add). Change: `/tmp/synthyboy-overlay-nocells` (forces `layersOk_` false for the run). Test: Mac: none (GL); device: T11. Acceptance: with the file present the status shows `cached_ops` 0; removing it restores cells without restart (checked once a second like the other switches).

**T10. Device measurement script.** Files: new `scripts/overlay-frame-table.py` derived from the local `.hw-local/mpc-perf/overlay-pkgs/ov17-osc-ui-results/scripts/loads17.py` (two status snapshots a window apart: `gpu_render_sums`, `gpu_render_hist_ms`, pace intervals, CPU of the overlay thread and the helper, GPU MHz; a FAILED row on a helper restart). Scenarios, each 10 s, three held notes (48, 55, 60), full detail and reduced: (1) idle, (2) LFO compact + notes (cursor only), (3) LFO editor, one point dragged back and forth, (4) FX page, a drag on a delay / filter control, (5) filter page, cutoff drag, (6) mixer with notes (meters), (7) Play, WT POS dragged (the trial 19 row), (8) envelope stage knob. Touch via `.hw-local/mpc-tools/mtouch`. Test: the script runs on the T0 helper and reproduces the trial 19 rows within noise (compact + notes mean 3.9 ms, knob dragged 15.9 ms). Acceptance: a table with mean and p99 helper ms per picture, `ops`, `cached_ops`, `redrawn`, `painted_px / full_frames`, overlay-thread `render_ms`, helper and thread CPU, `MemAvailable` and helper RSS.

**T11. Device run BEFORE (M0).** The coordinator runs T10 on the package of the T0 tree on the Key 61 (`root@192.168.0.30`), once; the files go to `.hw-local/mpc-perf/overlay-static-anim/before/`. Acceptance: all eight rows without FAILED; the memory columns filled.

**T12. Device run AFTER + GPU identity.** After T3-T9 merged: the coordinator builds the package once, runs T10 into `.../after/`, then the GPU identity of section 2.6 item 3 for the states `LFO editor`, `FX delay`, `filter`, `mixer`: frame with cells, frame with `nocells`, frame before and after the bake. Acceptance: for the changed elements (scenarios 3-6, 8) the mean helper ms per picture is lower than before by more than the run-to-run noise of T11 (run T11 twice to measure the noise; no number is promised here), `painted_px` per picture lower, p99 not higher; the identity frames differ by <= 3 per channel with 0 pixels above 2; no scenario slower than before. A scenario that is not better is reported as it is and its task is reviewed, not tuned silently.

**T13. Docs.** Files: `docs/MPC.md` ("Cached cells and layers": the state machine, the gate, the order rule; "Observed on the device": the T11 / T12 tables), `TODO.md:14` (item done or the rest listed). Acceptance: every number in the docs is from the files of T11 / T12.

## 5. Risks and open questions

Risks:
1. Two-layer limit. A second live group after layer-1 cells makes the scene uncached and slower than before (`cell_plan.h:89-95`). The one-pass rule (2.3) and the `planCells` asserts (T8) guard it; a page with popups that open during an animation is a case to test.
2. Order change on the CPU reference. Splitting an element can change the paint order when something static of another element overlapped the animated part; the gallery compare (T1 baseline) catches it.
3. Bake frame spike. The first picture after `hold` redraws the baked cell and the cells lying over it in the layer; for the shape editor canvas (956 x 540 px, `MPC.md:1294`) this is a big scissor box. Measured in T12 (scenario 3, the picture right after the hold); if the spike is above the 14 ms budget (`MPC.md:253`) the Ending cell may be split per column band (new step, not in this list).
4. Buffer age / g117 (2.5): an unknown age composes the whole picture; correct, slower.
5. Idle redraw. Today idle sends no scene (`controller.h:1050`); an `Ending` scene must be sent although nothing else changed: the gate must set `needFrame_` when the hold expires (a missed bake would leave the element drawn live forever, which is correct pixels but costs every picture). T2 test + a controller test.
6. Hash cost on the overlay thread. `describe` still builds every op of every cell each scene and hashes it (`controller.h:1055-1063`, `scene.h:276`); this plan does not remove that cost; it is unmeasured (only `render_ms` exists), T10 records it. Caching the op range of a static cell between scenes would be a later step, only if T11 shows it matters.
7. Mali-T760 memory: 8 MiB of layer textures exist today; no new texture here (2.5).
8. Rounding at the bake: an `Anim` to `Ending` change may move a pixel by 1 LSB (layer path vs direct); accepted only inside the <= 3 tolerance, see 2.6 item 3.

Questions that need the owner:
- Q1. His exact list of elements he calls unnecessary. In code the only "enlarge" control of the LFO is the full-screen `Expand` button of the compact view (`lfo_screen.h:24`); the editor has none. "Background" could be the page ground (`page_view_base.h`, `play_view.h:437`), the display block (`lfo_display.h:133`) or the group frames. And does he mean "not drawn at all" or "not part of the animation, drawn once"? This plan assumes the second (they are already static cells and are never animated); a removal is a design change that is his call.
- Q2. Hold time 350 ms (today's value) before the baked picture replaces the live one: keep?
- Q3. Knob drag on the Play page costs 15.9 ms mean even with live data (`MPC.md:297`; the display itself is heavy: 7 frames of the wavetable, fills, glow): is it in scope to animate less of it (for example only the main curve while dragging), or is this item only the layer split?
- Q4. g117 text / branch: where is its design and final code (the repository has only `TODO.md:19` "Fő layer kísérlet" and the commit-shim tools)? T0 depends on it.
- Q5. Any element the owner wants animated but we classed static (knob under the finger, Mod page values)? Default here: measured first (section 3, row "Mod, Macros, Master, Voice").



### FX page inventory of moving things (T2, code reading 2026-11-21; paths under `shell-mpc/`)

Only Distortion has anything that moves by itself; every other FX display changes only on a parameter edit (touch / Q-Link). The Meter-vs-drawing decision for (V) items is taken from the measured host redraw rate (open measurement).

| Element | What moves | Source | Rate | Area | Class | Note |
|---|---|---|---|---|---|---|
| Distortion, level dots | two dots at +/- input level on the curve, a thin line from zero to each | reader `overlay/fx_ui.h:761-764`, drawn `ui/fx_display.h:432-441`, engine `core/engine-own/own_engine.cpp:1997` | engine once per block (about 300 ms release), overlay repaint at most every 33.3 ms (`overlay/controller.h:102`), only on scene-hash change | plot about 1204x114 in a display cell 1264x160 | V on the data side (one scalar), but the dot sits ON our curve | a host Meter cannot place a dot on a curve; keep as an animated part of the cell unless the host Meter can carry a marker |
| Distortion transfer curve | none, edit only | `ui/fx_display.h:421-427` | edit | plot 1204x114 | D, static between edits | |
| EQ curve and band dots | none, edit only | `ui/fx_display.h:216-239` | edit | 1264x160 | D | 192 points |
| Compressor curves (1 or 3 bands) | none, edit only; there is NO gain-reduction or level meter | `ui/fx_display.h:243-336` | edit | 1264x220 | D | |
| Delay taps / ping-pong | none, edit only (tempo-synced time converted by `FxUi::delayMs`, `overlay/fx_ui.h:744`; a host tempo change would move it: unverified) | `ui/fx_display.h:338-390` | edit | 1264x176 | D | up to 12 stems per lane |
| FX Filter response and cutoff marker | none, edit only (modulation of the filter parameters is not read) | `overlay/fx_ui.h:770-810`, `ui/fx_display.h:392-406` | edit | 1264x160 | D | 256 points |
| Chorus, Flanger, Phaser, Reverb, Hyper/Dimension, Utility, Bode, Convolve | no display, no LFO indicator | `ui/fx_display.h:5-6, 55-65` | n/a | 0 | n/a | |
| Splitters, chain bar | no motion | `ui/fx_band_board.h`, `ui/fx_chain_bar.h` | n/a | 1264x206..240 | n/a | |

Consequences: 1 V item (Distortion level), 0 other autonomous movers; the busiest page is Distortion with 2 dots and 2 lines driven by one scalar; at most about 202k px (the Distortion display cell) is repainted while it moves, at most 30 times a second. The plan section 3 FX row names only curve drags as the animated part: the AnimGate (T1) and T4 must ALSO be woken by the `inLevel` change of Distortion, not only by control drags. The owner's expectation that several things move at once on the FX page is not borne out by the code on this page; it holds for the Mixer and Master meters (`docs/MPC.md:1342`).


## Meter split and meter tasks (owner decision 2026-11-21: "oké legyen így")

Decided: level and gain-reduction METERS are host-drawn native `Meter` widgets bound to plugin parameters (the host redraws a visible page at a fixed 20 fps only while a value changes, 0 when idle; correct popup z-order; about 80 % of 25/s steps visible; one torn frame seen: `docs/dsp-knowledge/mpc-display.md`). Moving playhead dots and drawn curves stay on OUR plane (30 fps, no tearing). Plan, after g119 (layout cleanup) and g121 (host-skin background prototype) have landed, because all of it edits the same skin files (`scripts/mpc-skin-extras/*`, the overlay layout):
- **M1. Mixer and Master meters -> host Meters.** One native Meter per existing meter, bound to an output parameter the plugin updates (the pattern of `scripts/mpc-skin-extras/40_spectrum.py`); remove their drawing from our live layer; keep the value path the plugin already has (`docs/MPC.md:1342`). Acceptance: pixel/position check against the current meter cells, host test of the parameter feed, the page stays gated like the other host-skin elements.
- **M2. New FX meters on the FX page as host Meters:** compressor gain reduction plus input / output level, reverb and delay output level. Needs engine-side level readers per FX slot (the engine publishes only the Distortion input level today: `core/engine-own/own_engine.cpp:1997`), output parameters for them, and the Meter elements in the FX tab of the overlay skin.
- **M3. Device check:** XL and Key 61, meters visible, the 20 fps cap, popups over the meters (z-order), no audio gap (callback statistics), the modified-star TODO in `TODO.md`.
Not in scope: the Distortion level dots (they sit on our curve: stay on our plane).


## Disabled blocks (owner rule 2026-11-21)

A block whose on/off switch is OFF (Sub, Noise, Osc A/B/C, Filter 1/2, every FX slot, any other toggled block) is drawn entirely grey and takes no touch except its own switch (central `disabled` state in `shell-mpc/ui/`, bound per page to the block's `*.on` parameter; TODO.md has the full rule). For this plan: a disabled block needs NO animation, its live parts are not drawn and its AnimGate (T1) stays closed; switching it on invalidates the block's cells (colour and live parts return at once).


## Measured baseline (Key 61, 2026-11-21, develop aa4d18f7, 3 held notes MIDI 48 55 60, Acid Saw Reso; raw `.hw-local/mpc-perf/key61-night-2026-11-21/overlay-split/`)

Every step of this plan is measured against this table (same method: instrumented package with a CPU scope timer per section, overhead about 0.25 pt; no perf on the device).

| Page | sb-overlay thread, % of one core | helper process % | pictures/s |
|---|---|---|---|
| Macros | 6.5 | 7.0 | ~28.7 |
| FX | 5.4 | 6.7 | ~28.7 |
| Play | 9.3-9.7 | 5.2 | ~28.7 |

Split of the sb-overlay thread, % of the thread:

| section | Macros | FX | Play |
|---|---|---|---|
| FFT (`SpectrumCurve::analyse`) | 40.1 | 0 | 0 |
| rest of `view.update` (parameter reads, UI state, smoothing, hash) | 33.0 | 59.7 | 56.5 |
| scene build (`describe`) | 9.5 | 17.4 | 29.2 |
| draw, other | 2.5 | 3.5 | 2.1 |
| show / submit to the helper | 1.4 | 3.2 | 2.5 |
| other (page detect, status file, gpu poll, touch) | 13.5 | 17.9 | 9.9 |

Helper per picture (wall clock, us): Macros build 885, submit 434, glFinish 11426, tail 996 = 13745; FX 822 / 431 / 11224 / 1044 = 13527; Play 62 / 444 / 2693 / 965 = 4175. About 83 % of a Macros / FX picture is `glFinish` (the picture is GPU-bound); the helper's real CPU is 5-7 % of a core (about 2.4 ms per picture).

Conclusions (coordinator, 2026-11-21): a GPU-compute FFT is NOT worth it (the FFT is at most 2.7 pt of one core, on Macros only). The real costs are `view.update` + `describe` (45-88 % of the overlay thread) and the GPU time per picture. Priorities: (1) skip `view.update` / `describe` when the instance data and the input did not change (a dirty check per page), (2) the static / anim split, (3) host-skin backgrounds (less to draw on the GPU), (4) the disabled-block rule (a switched-off block draws nothing live). A slot that the FX cost budget of tier low (in work) bypasses is shown greyed with a small 'CPU' mark: plan it with the disabled-block rule.

## Owner decisions (relayed 2026-11-21, verbatim sources in the coordinator's notes)

* Q1: the background and the zoom / Expand button are NOT on the animation path: drawn once as static cells, not removed.
* Q3: only the moving curve animates (FX, and the Play-page knob drag): block, texts and grid are static cells; target: the 15.9 ms mean of the Play-page knob drag.
* Q2: the hold stays 350 ms by default, exposed as ONE constant; T10 reports the Key 61 per-picture cost at 350 ms and at 150 ms so the value is chosen by number.
* Q5: borderline elements are decided by measurement.
* Order: the tasks start after g117 (the layer takeover) has landed and been reviewed (same files); each task is a commit pair; pixel identity by the gallery compare and the GPU nocells switch.


## Pre-generated page backgrounds (owner request 2026-11-21)

Owner, verbatim: "generáld le előre a háttereket olyan formában, hogy a legkönnyebb legyen betölteni háttérnek, kevés performanciát használjon el a betöltés, pont az a pixelméretre tömörítés legyen, ami a legoptimálisabb erre, és minden háttérnek legyen egy full takaró háttérszíne." Task **T-bg** (after T1; before T10 so that the measurement covers it):

* **What**: a generator (`scripts/gen-overlay-backgrounds.py`, reusing the CPU reference renderer `draw.h` / `scene_cpu.h` through the existing gallery tool) renders the STATIC content of every plugin-drawn page (frame, labels, knob surrounds, slider scales, LED off states, buttons at rest; NOT the live curves, the playhead, the meters, the values) once per device into a file. The helper loads the file at start (or lazily on first use) instead of building the static cells at run time.
* **Pixel size and orientation**: the device's native plane size (800x1280 panel, our plane is AR24 800x1280 as in the DRM state), in the plane's own orientation: no rotation, scaling or format conversion on the GPU at run time; one file per device (Key 61 and XL differ in the plane/crop) and per page; the generator takes the geometry from the same constants as the helper.
* **Full covering base colour**: every background starts with a fully opaque base fill (the theme's page ground colour token) over the whole page area, so nothing of the host's page below shows through where our page is meant to cover it; the file is opaque (alpha 255 over the page rectangle), cheaper for the compositor (no blending) and without halo at the edges; the areas that must stay transparent (host header and tab strip when the page does not cover them) are the only alpha 0 regions and are listed in the file header as rectangles.
* **Format (the choice is made by the measurement, default first)**: (1) DEFAULT: raw uncompressed, the exact bytes of the texture (RGBA8 or XRGB8888, 4 bytes/pixel, header with magic, version, width, height, stride, rectangles, a hash), loaded with `mmap` and ONE `glTexImage2D`/`glTexSubImage2D` (no decode step, lossless text and edges); about 4.1 MB per 800x1280 page; load on demand and keep only the pages in use (the Key 61 has 4 GB RAM and no swap, the GPU shares RAM); (2) only if the memory or load time measured on the Key 61 is not acceptable: RGB565 (2 bytes/pixel, banding on gradients: check by the gallery compare), or a GPU-native block format (ETC2 on the Mali-T760 of the Key 61, ASTC on the Mali-G610 of the XL): lossy, needs a per-device encoder and a text/edge quality check: not for sharp UI without a measured need; (3) NOT PNG/JPEG: they need a CPU decode at load time, the opposite of the goal. Deliverable of the T-bg measurement on the Key 61: load time per page (read + upload) for raw RGBA8 versus RGB565, memory per page, and the helper's first picture time after a page change; the decision goes to this section.
* **Integrity**: the file carries the hash of the scene description that produced it; at start the helper compares it with the hash of the static ops it would draw (cell_plan hash) and falls back to drawing the cells at run time if they differ (a stale file must never show a wrong picture); the generator is run by the package build (`scripts/build-mpc.sh`) so the files are always current, and a test fails if a file is missing for a page.
* **Pixel identity**: the baked background of a page must be pixel-identical (tolerance 0 on the CPU path) to what the static cells of the same page draw today: the gallery compare covers it.

* **Two-state elements (owner 2026-11-21: "a ledek pl legyenek rajta és amikor aktív akkor rajzolunk rá")**: LEDs, toggle buttons, mode lamps and similar elements with a REST state and an ACTIVE state: the REST state (LED off, button at rest) is part of the pre-generated background; only the ACTIVE state is drawn, as a small live op on top of the background (a lit LED disc, a pressed button face) while it is active, and removed again when it ends (no re-bake of the background, no cell invalidation). The same rule holds for the zoom/Expand button and other controls that change only on a tap. The active-state ops of one page are emitted in the same contiguous live pass after the cells (see the two-layer rule). The gate for them is trivial (Static while off, Anim while on), so `AnimGate` is not needed for pure on/off elements: they just add or drop their live op. The generator renders the background with ALL such elements in their rest state, and a test compares "background + active ops" with the full CPU render of the page in each state (pixel identity).

## Popup rule (owner, relayed 2026-11-21; part of the host-skin background work, not an option)

Owner: it is rare that anything must animate behind a popup; when we know a popup is shown, blur the background and that is all.
- State machine (plugin side, from the existing popup/marker detection): `NoPopup -> PopupShown -> NoPopup`.
- On entering `PopupShown`: (a) stop animating our live parts, no new pictures while the popup is up; (b) our plane must not cover the popup: live rects overlapping the popup region are dropped (transparent); the remaining live parts are drawn ONCE with the same treatment the host applies to its own background under that popup (blur radius and/or dim factor, measured from a host screenshot, like the standby dim matching in `standby.h`), so they look like part of the blurred host page.
- On leaving it: restore the normal live drawing and resume animation.
- Host test for the state machine; device check on XL and Key 61 with the Q-Link panel and a Load dialog (owner looking). First deliverable: the measured host treatment (blur radius / dim) from a screenshot with a popup open; no such state is in the screen library yet.

Measured so far (XL, 2026-11-21, `docs/dsp-knowledge/mpc-display.md` to be updated): the host's left drawer and top transport panel leave the page behind them exactly unchanged (no blur, no dim); the Load dialog replaces the whole screen. Rule: host drawers/panels = drop our live rects that the panel covers and pause the animation of covered parts, NO blur; a full-screen host screen (Load) = hide our whole layer; OUR OWN popups = we blur/dim our own background (design choice, one consistent look); if the Preset list or a Q-Link touch overlay blur the page, copy the measured treatment. Trigger: the popup rule fires from the input our layer already sees (our own popups are ours; host popups follow a touch/button), the marker detection is the safety net only (owner).

Popup treatment measured (XL, 2026-11-21, `docs/dsp-knowledge/mpc-display.md`): host popups opened by a long press on the Preset / Plugin fields blur the page (radius 4-5 px) and dim it to about 19-22 % of its brightness; the left drawer and top panel do neither. Our helper already hides the overlay for them (`hidden: no view on screen`). For OUR layer: while such a host popup is up the live parts are not shown (current behaviour: hide); if the live parts must stay visible beside a popup, draw them once with the same treatment (blur r 4-5, brightness x0.2). Our own popups use the same look.

