# MPC display path

## Facts (XL, 2026-11-20/21)

- The host commits frames from **one thread** (its main thread), with atomic commits:
  frame commits (flags 0x1 PAGE_FLIP_EVENT, user data set, 15-23 property items) and cursor
  commits (0x200 NONBLOCK). A real frame commit takes a median 13-16 ms (it waits for vsync).
  `measured: tools/xl-commit-shim, .hw-local/mpc-perf/xl-commit-shim/`
- **The host presents only on UI events:** 0 frame commits per second when idle; a tab tap
  gives a burst of about 5 commits within 0.4 s. Anything that animates on its own cannot rely
  on host frames. `measured: stage3-live-2026-11-21.log`
- **Merging our plane into the host's commit works:** 45 merged commits, all ret 0, no extra
  frame time (median 14.7-15.0 ms vs 15-16 ms). `measured: same log`
- **Our overlay helper already owns plane 46 (XL) / 38 (Key 61)** via SETPLANE. Two writers on
  one plane fight: the test picture showed only on some pages. One plane needs one writer.
- **A plane is above the host's primary plane:** merging into the host commit gives the host's
  timing (page change and our layer change in the same frame), NOT host z-order (host popups
  still cannot cover a plane above them).
- **Drawing:** two persistent layer textures, per-cell damage, live ops drawn every picture
  (protocol 8); all live ops of a page must be one contiguous pass after the static cells
  (`shell-mpc/overlay/gpu/cell_plan.h`). Plan: `docs/plans/overlay-static-anim-layers.md`.

## Plane stacking facts (measured read-only 2026-11-21, plugin page open)

- Primary plane FB format is **XR24 (no alpha)** on both devices: XL plane 34 (JUCE message thread allocates, 800x1280, pitch 3200), Key 61 plane 33. Source: `/sys/kernel/debug/dri/0/state` and `drmprobe info`.
- The host writes alpha byte **255 in every pixel** of its primary buffer (drmgrab dump of both devices, 1,024,000 of 1,024,000 pixels). With XR24 the plane scans out opaque in any case, so a hole in the host's skin background cannot be transparent.
- XL (RK3588 VOP2): planes have `zpos` (range, mutable; primary 0, cursor plane 40 = 1), `alpha` (65535) and `pixel blend mode` (value 0) properties. Key 61 (RK3288 VOP): planes 33/35/38/40 have NO zpos, alpha or blend property; order is fixed (primary 33 lowest, our plane 38 above it).
- Consequence: an under-plane design needs the primary to be non-opaque (the shim would have to re-wrap the host buffer as AR24, which only works if the host writes alpha 0 in the holes; it writes 255 today) and, on the Key 61, is impossible because the primary cannot be moved above our plane. Open: whether XL accepts the AR24 re-wrap (`ADDFB2` on the same GEM handle) and what the blend mode value 0 means in the enum list.

## Host filmstrip cache (XL, read-only, 2026-11-21)

- The MPC process does NOT map `/var/tmp/filmstrips` (0 mappings in `/proc/<pid>/maps`, no open fd): the decoded images are read into heap; the files are a cache from an EARLIER run (mtime 2026-11-20 17:58, the app started 01:43 on 21st). Writing into the files cannot change what the host draws: hypothesis rejected.
- 838 files `temp_<hex>.img` on tmpfs (`/var/tmp` -> `/var/volatile/tmp`, 8 GB), mode 0644 root:root. Raw pixels, no header: the size equals w*h*4. 85 files are exactly 3,215,360 B = 1280x628x4 (a full tab page), 90 are 2,419,200 B, 52 are 576,000 B.
- The stock skins carry full-page backgrounds as plain PNGs: of the 211 PNGs over 60 kB under `/usr/share/Akai`, 62 are 1280x629 and 8 are 1280x630 (the stock synth skins, `Plugin Skins`), a few 1280x628 (MPC60, SP1200, MPC3000 skins, Program Edit). So the host accepts full-page images and a decoded full page costs 3.2 MB in the cache.

## Host redraw rate of native widgets (XL, measured 2026-11-21, shim stage 1 + DRM poll)

- Page with 32 native Meters bound to plugin parameters (self-test 25 steps/s): the host commits **a fixed 20 frames/s** (199 commits in 10 s, 1199 in 60 s, gaps 50.0 ms median, 44.7..55.2), each commit flags 0x1 with 23 items. With the plugin page hidden: 0 commits. So the host redraws a visible page at 20 fps only while something on it changes, nothing when idle.
- About 80 % of the 25/s steps are visible (201 distinct states in 10 s; 41 gaps of 80 ms and 9 of 90 ms); one torn frame (a bar half-updated, the host's software render read mid-draw) was seen.
- Decision (owner / coordinator, 2026-11-21): level and gain-reduction meters may use host Meters (20 fps is enough, correct popup z-order); moving playhead dots and drawn curves stay on our plane (30 fps, no tearing).

## Key 61: host Meter redraw rate and the Info / Plotter handles (measured 2026-11-21)

- Trial skin on the Macros tab (self-test LFO cursor at 25 steps/s): only Meters bound through `Parameter/Named/<name>` and `Parameter <index>` show live data. Every Plotter (`Info/Index/0..7`, `Info/Named/<name>`, `Parameter/Named/<name>`, `Parameter 2637` with drawHistory true and false) and the Meter on `Info/Named/...` stayed empty. So the Info namespaces and the Plotter give nothing to an external plugin (XL not run; the stock skins' `Info/Named/` data comes from the built-in engines).
- The host redraws a visible Meter page on the Key 61 at 8.0 picture changes/s (81 in 10 s, intervals 99-150 ms, median 109 ms, alternating two framebuffers), 0 on another tab. The XL does 20/s. A host Meter therefore updates at about 8 fps on the Key 61 (poller `flipwatch`, raw logs in the scratchpad `k61-trial/`).

## Host popups on the plugin track view: blur and dim (XL, long press ~700 ms, measured 2026-11-21)

- A short tap on the PRESET / PLUGIN / TRACK fields only focuses them; a hold of about 700 ms opens the popup (TRACK opens none).
- PRESET field: a left side panel, x 0..639, full height (the preset list). The page behind it (x 640 and up) is BLURRED and DIMMED: fit `popup = a * blur_r(normal) + b` gives r = 4, a = 0.194, b = -0.10, MAE 0.50 (without blur: a 0.109, b 2.41, MAE 0.82). A flat area goes from 29.67 to 5.67, about 19 % of its brightness: a dark scrim of about 81 %.
- PLUGIN field: a centred modal dialog (x 122..1158, y 10..728, TYPE / MANUFACTURER / CLOSE / SELECT). The page around it is blurred and dimmed the same way: r = 5, a = 0.216, b = -0.45, MAE 0.97; flat areas 29.67 -> 5.67 again.
- Our overlay hides by itself in both cases (status `hidden: no view on screen`, `view: none`, `view_hides` counts up): the helper does not draw over these host popups today.
- The host drawers (left Browser / Mixer panel, top transport panel) do NOT blur or dim the page (earlier measurement); the Load dialog replaces the whole screen. The Monitor popup was not found on this view and the Q-Link touch overlay is not reachable by touchscreen.
- The Mixer PEEK panel freezes its Mixer background, blurs it and retains 20 % brightness. The GPU uses the existing separable blur in a cached cell. The CPU fallback uses a radius-4 box filter. Test: `shell-mpc/overlay/tests/gpu_scene_host.cpp` pins the CPU edge spread, dim level and unchanged foreground / host border. The GPU kernel is an approximation of the measured host treatment.
- Raw: scratchpad `popup3/` (grabs, `an.py`).

## Hardware generation Q-Link frame visual policy (measured source and test facts, 2026-11-24)

- **Gen 1 (ARMv7 32-bit: MPC Key 61, Live, One, X):**
  - Q-Link bank framing is active for four-knob devices (`QVisualPolicy::kHasQFrame == true`, `SYNTHYBOY_MPC_UI_GEN=1`, `SYNTHYBOY_MPC_QFRAME_ENABLED=1`).
  - Active bank quad draws one outline frame and one unnumbered Q marker in the top-left corner (`Pen::qFocus`), enclosed in a single cached cell.
  - Member components receive the Q focus accent colour on four-knob setups.
  - Numbered chips (Q1–Q4 / Q1–Q16) are suppressed across all screens (0 numbered text ops emitted).
  - On Gen 1 16-knob setups (e.g. MPC X runtime mode), `wideDevice()` suppresses the bank frame dynamically because all 16 knobs map concurrently.
- **Gen 2 (AArch64 64-bit: MPC XL, Live III):**
  - Bank framing and Q markers are eliminated entirely across all screens (`QVisualPolicy::kHasQFrame == false`, `SYNTHYBOY_MPC_UI_GEN=2`, `SYNTHYBOY_MPC_QFRAME_ENABLED=0`).
  - Drawing routines for Q frames (`Pen::qFocus`, `Pen::qMarkerRect`, and `QGroup::draw` frame branch) are compiled out at compile time, eliminating frame outline and marker scene ops (0 ops emitted in both 4-knob and 16-knob modes).
  - Member components do not receive Q focus accent marks (`mark = false`), maintaining frameless appearance.
  - Functional physical Q-Link routing (`QGroup::turn`, `qslots`) is completely preserved and identical to Gen 1.
- **Verification evidence:**
  - `shell-mpc/ui/tests/q_visual_policy_host.cpp` verifies both Gen 1 and Gen 2 compilation modes:
    - Gen 1: 1 bank frame, 1 unnumbered Q marker, 0 numbers; 16-knob runtime produces 0 ops; knob turn delta routing functional.
    - Gen 2: 0 frame ops, 0 marker ops, 0 member Q marks; compile-time static assertions confirm `QVisualPolicy::kHasQFrame == false` and `Pen::qMarkerRect().empty() == true`; knob turn delta routing functional.
  - Preprocessor output confirms the elimination of `rrect` and `textCentered("Q")` in `Pen::qFocus` and `QGroup::draw` in Gen 2 mode.

## Decisions

- g117: full takeover, one plane, one writer: the shim merges the helper's finished buffer
  into the next host frame commit; if the host does not present within 20 ms the helper
  presents itself. Host TEST_ONLY commits are never merged.

## Historical Key 61 in-host drawing experiment

The six commits on `origin/work/inhost-draw` (base `a9251b1b`, tip `3b6f8772`) tested compositing into the host's primary framebuffer. The release uses stock native skin widgets and a separate overlay plane for custom drawings. The experiment finished with the following disposition.

| Commit | Experiment | Release path |
| --- | --- | --- |
| `f5b13a93` | Optional host hook, primary-framebuffer copies, a picture ring, and an idle front-buffer writer | The GPU helper presents finished buffers to a separate plane and the CPU presenter has its own back buffer. The Q-Link panel key clips the overlay, and the detector withdraws it for host drawers and other pages. `qlink_panel_host`, `panel_key_host`, `controller_host` and `gpu_backend_host` exercise those paths. |
| `3bef82c9` | Preserved the original binary when the experiment was disabled | The experiment's build switch is absent from the release path. The branch's byte-identity check applies to its own build. |
| `7e2c55df` | Documented the alternate backend and its firmware dependencies | Historical implementation notes remain on the branch. |
| `5dd66a65` | Discovered the host's framebuffer property and counted entries into the hook | The release discovers the output for its own plane and keeps the host framebuffer as an input to detection. Hook counters and private commit-request parsing apply to the alternate backend. |
| `8f811c18` | Timed then interrupted long copies, filled the primary when a picture was late, erased idle pixels on hide and paced front writes | The release waits for the rendered buffer, paces plane updates and withdraws the plane on page exit. A skin sentinel verifies host artwork, with an opaque fallback when the sentinel is absent. Standby and popup handling remain in `standby_host` and `qlink_panel_host`. |
| `3b6f8772` | Finished every copy before committing, added a 12 ms fault threshold and selected a copy kernel | `Presenter::present` changes buffers and counts a frame after the plane update succeeds. The GPU helper waits for rendering before setting its plane. `display_host`, `controller_host` and `gpu_backend_host` cover pacing, failed plane updates and recovery. Copy kernels and their budgets apply to the alternate backend. |

The Key 61 run-4 status recorded 44 copies on 49 detected own-page flips, five misses, a 4.143 ms mean and 9.404 ms maximum hook time, one over-limit hook and zero fallbacks. A separate 14-second Q-Link-panel sample reported zero missing-picture and popup-overdraw episodes. The copy microbenchmark on its own write-combined dumb buffer measured 1.83 ms mean cold and 1.80 ms mean warm for packed page-row `memcpy`, versus 2.20 ms and 2.22 ms for the prefetched 64-byte NEON loop. The microbenchmark isolates row copying, while the status measures the complete hook. The archived runs establish feasibility on Key 61. Comparative performance against the current release and XL behavior remain unmeasured. Evidence is under `.hw-local/mpc-perf/inhost/` (`k61-run4-status.txt`, `k61-run4-ihwatch.txt`, `k61-ihbench.txt`). The audit keeps the release plane path and found no test that needs the retired backend.

## Measured Host Commit Characteristics (MPC XL, 2026-11-21)

Data recorded during device testing on the MPC XL (`.hw-local/mpc-perf/xl-takeover-2026-11-21/`):

1. **Commit Nature**:
   - Host commits are **purely event-driven**.
   - When idle: **0 commits/second** (the host does not perform periodic vsync flips).
   - During UI interactions (page transitions, touch, wheel, Q-Link adjustments): commits arrive in bursts of **3–4 commits/second** on average.
   - Commit interval during interaction bursts:
     - Count: $N = 345$ intervals $< 200$ ms
     - Minimum: 1.3 ms
     - Median: 18.3 ms
     - 90th percentile ($P_{90}$): 91.2 ms
     - Maximum: 198.4 ms
     - Fraction in 30–70 ms range: 0.00 (flips are irregular and driven by CPU event dispatch)

2. **Call Sites in `/usr/bin/MPC`**:
   - **Call Site (a) - Frame Present**:
     - Flags: `DRM_MODE_PAGE_FLIP_EVENT` (`0x1`)
     - `user_data != NULL`
     - Properties: Primary plane `FB_ID`, `CRTC_ID`, `CRTC_X/Y/W/H`, `SRC_X/Y/W/H`, `FB_DAMAGE_CLIPS` blob.
     - Blocking commit: Median commit duration ~13 ms (including hardware vsync wait).
   - **Call Site (b) - Cursor Move**:
     - Flags: `DRM_MODE_ATOMIC_NONBLOCK` (`0x200`)
     - `user_data == NULL`
     - Affects cursor plane (plane 40 on XL).

On the measured `Akai Professional MPC XL` topology (CRTC 82, primary 34), the host owns plane 40 even with its pointer hidden. All nine original frame requests carrying property lists in `.hw-local/mpc-perf/xl-commit-shim/stage3-live-2026-11-21.log` write plane 40. Eight set both FB_ID and CRTC_ID to zero; the ninth assigns a 64×64 sprite. None writes plane 46. Planes 40 and 46 report the same Overlay type, possible-CRTC mask 0x1 and fourteen pixel formats (`.hw-local/mpc-tools/overlay-probe/xl/logs/drmprobe-info.log`). Those capabilities cannot distinguish the sprite from the plugin plane. The runtime candidate list excludes this plane on the identified model and topology, preserving the remaining eligible planes for discovery and migration. The Key 61 has a separate DRM Cursor plane (35) and retains its free overlay plane 40. The contemporaneous XL plane census is in `.hw-local/mpc-perf/native-bg-xl-2026-11-21/p46-drm-state.txt`.

The plane-46 diagnostic (same native-root Macros skin, `.hw-local/mpc-perf/native-bg-xl-2026-11-21/p46-main/analysis.txt`) observed zero grey frames in 30 entries and 30 returns. The native Macros background was present at the first sampled destination marker on all 30 entries. On each entry the composed output still held the old Play picture for 1–7 sampled frames after that marker. Some sample intervals were missed. These are separate native and composed results for this diagnostic build.

The source of the normal plugin/skin navigation path is `Controller::nativeTabTouch`. It recognises the XL Play/Macros function-key interior from the recorded host screen (`.hw-local/mpc-perf/native-bg-xl-2026-11-21/root-play-idle-composed.png`, active bar x=17..195 at y=740). An eligible Down temporarily withdraws the plugin plane while the detected page's native artwork remains visible. The detector checks during this transition and decides which current page and instance to render. Release outside, an ignored switch and lost-release recovery return to the verified source. Hardware, mouse, and touches outside that key use the existing page detector. `native_tab_handoffs` counts the precautionary plane withdrawals. `page_view_host` contains the bidirectional, cancellation and unknown-host-screen cases. Device acceptance requires separate primary, foreground ARGB and composed samples.

**XL scanout sampling (2026-11-21).** A DRM framebuffer number can be reused after the helper removes its framebuffer in `dropImports()` when the overlay goes off. `scan-root.c` kept its original DMA-BUF mapping by framebuffer number across those removals. During the same 260 refreshes on native Play with FB 91, the old reader returned the 19-point Macros signature while a fresh reader returned the 140-point Play signature. Four different DMA-BUF inodes occupied FB 91 during the control. A separately opened capture of the returned Play overlay matched the initial Play capture byte for byte. See `.hw-local/mpc-perf/foreground-xl-2026-11-21/control/`. The reported stale Macros foreground in the previous `final-bg-xl-2026-11-21` scan is a sampling artifact. Resolve the current framebuffer via GETFB2 and obtain its DMA-BUF on every sample, or establish the backing object's continuing identity before reusing a mapping. Report ambiguous or missing refreshes separately.

The fresh-object 30-round-trip run on 978c1635 (`.hw-local/mpc-perf/foreground-xl-2026-11-21/main/`) observed the correct native root at every first target marker. Two returned Play foregrounds temporarily differed. Return 30 had five refreshes resembling opaque Macros; return 2 lacked parts of the oscillator/envelope graphs for 52 refreshes. Two source paths reproduce those symptoms. Sampling markers and sentinel in separate acquisitions could combine the source marker with the destination artwork and trigger the opaque source fallback. The detector now reads both from the same mapped primary frame, and an already withdrawn source stays withdrawn while its artwork changes within the existing bounded handoff. The once-per-second detail check could treat a healthy, temporarily hidden GPU as the CPU and suppress graph glow until its next check. It now uses renderer readiness. Host regressions exercise both paths; the observations contain no per-frame plugin trace identifying which source path actually ran.

**XL verification on b55fb41e.** The fresh-object repeat covered 30 Play→Macros and 30 Macros→Play F1 transitions (`.hw-local/mpc-perf/verified-handoff-xl-2026-11-21/README.md`). Each first sampled target marker had the correct native asset at all 140 primary RGB points and a transparent overlay at those points. Every subsequent valid foreground matched the target whenever present. First live Macros arrived in 16.85–53.26 ms and Play in 16.45–53.20 ms after the first observed target marker. Neither discrepancy recurred across 6,508 valid observations. The host browser, drawer and cancelled F1 drag restored the expected state. There were 107 ambiguous and 55 missing refreshes; independent register reads recovered none of those gaps. Every transition had at least one unsampled refresh and the spatial comparison sampled 140 points, so continuous frame-by-frame correctness remains unverified.

---

## Analysis: Why the 20 ms Publish/Claim Takeover Failed

The initial takeover design attempted to merge GPU helper presentations into the host's atomic commits:
1. Helper rendered a frame, wrote its FB ID to `/tmp/synthyboy-layer-block`, and waited up to 20 ms for `xl-commit-shim` to claim and merge it into a host commit.
2. If unclaimed after 20 ms, the helper fell back to presenting via legacy `SETPLANE`.

### Measured Findings on Device:
- **Publish vs Claim**:
  - Published frames: 226
  - Claimed by shim hook: **4 frames (1.8%)**
  - Self-claimed by helper (timed out after 20 ms): **222 frames (98.2%)**
- **Coincidence with Host Flips**:
  - Overlay changes coincident with a host flip ($|\Delta t| \le 3$ ms): **0 out of 43 changes**
- **Overlay Lag After Host Page Flip**:
  - Minimum: 17.5 ms (~1 frame)
  - Median: 35.4 ms (~2 frames)
  - $P_{90}$: 124.0 ms (~7 frames)
  - Maximum: 212.4 ms (~12 frames)

### Root Cause:
The helper's render loop is reactive: it discovers a new page only **after** the host has completed its flip and exposed new marker pixels in framebuffer memory. Because host commits are bursty and event-driven, the host does not commit again immediately after flipping to a new static screen. The helper published to `LayerBlock` and waited 20 ms in vain, only to fall back to `SETPLANE`.

**The Visual Defect**: When the user navigated to a new page, the **stale overlay picture from the previous page remained on screen over the new host page for 2 to 12 frames** before the helper's `SETPLANE` updated it.

---

## Redesign: The Page Guard Architecture

Instead of attempting to merge overlay presentations into sporadic host commits, the commit shim acts as an event-driven **Page Guard**:

```
Host Atomic Commit (Flip Event)
          │
          ▼
   [xl-commit-shim]
          │
          ├─► Extract Host FB_ID
          ├─► Lookup / Map Dumb Buffer (HostFbCache)
          ├─► Read Markers: (36, 137) & (1244, 137) (Panel Rotation)
          │
          ├── Markers MATCH active page in LayerBlock v3?
          │     └── Pass host commit through UNTOUCHED
          │
          └── Markers MISMATCH or UNKNOWN PAGE?
                ├── Inject Plane-Off (FB_ID=0, CRTC_ID=0 for overlay plane)
                ├── Commit atomically WITH host flip
                └── Set guard_plane_off = 1 in LayerBlock
                                │
                                ▼
                       [GPU Helper]
                         ├── Detects guard_plane_off, reports kMsgGuard(epoch) to the plugin
                         ├── Stays HIDDEN (no render, no SETPLANE) until the plugin's page detection,
                         │   made after the report (run at once, same tick, >= 16 ms apart; protocol 9),
                         │   finds one of our views on the current host fb (kMsgPageOk)
                         └── Then renders and presents directly via SETPLANE
```

### Key Technical Mechanisms

1. **Host Dumb Buffer Cache (`HostFbCache`)**:
   - The host cycles through 2–3 dumb buffers.
   - The shim maps each buffer once via `drmModeGetFB2` and `DRM_IOCTL_MODE_MAP_DUMB` `mmap`.
   - Mappings are cached per FB ID in a fixed-size table (zero allocations during commits).
   - Hooked `drmModeRmFB` automatically evicts and unmaps freed buffers.

2. **Marker Pixel Inspection**:
   - The published page index selects both marker centers from the skin layout. Play uses `(36, 137)` and `(1244, 137)`. The LFO tab uses `(36, 114)` and `(1244, 114)`. The Mac shim host test paints both pairs at their actual locations and checks that a matching flip preserves the overlay.
   - Coordinate transformation to physical panel coordinates:
     - XL ($270^\circ$ rotation, panel 800x1280): $P_x = 800 - 1 - Y_{ui}$, $P_y = X_{ui}$.
     - Key 61 ($90^\circ$ rotation, panel 800x1280): $P_x = Y_{ui}$, $P_y = 1280 - 1 - X_{ui}$.
   - Pixel format: `DRM_FORMAT_XRGB8888` / XR24. RGB channel tolerance: $\le 2$.

3. **Atomic Plane-Off Injection**:
   - When markers mismatch or an unrecognized page is loaded, the shim adds `FB_ID = 0, CRTC_ID = 0` for our overlay plane directly into the host commit.
   - Stale overlay content vanishes on the exact hardware scanout frame the host flips to the new screen.
   - At worst, the user sees 1–2 blank frames before the new overlay appears, eliminating visual artifacts.

4. **NONBLOCK Frame Commits**:
   - Both blocking and `DRM_MODE_ATOMIC_NONBLOCK` frame commits carry plane-off on mismatch. Disabling a plane requires no presentation sequence synchronization or completion notification.

5. **Direct Presentation Path**:
   - GPU helper presents all frames directly via `SETPLANE` (`planeSet`) as fast as possible.
   - The 20 ms claim/publish timeout is completely removed.

## Helper pace after a confirmation, and the idle wake-ups (g134)

- `measured (XL, g133 tour, .hw-local/mpc-perf/xl-g133-2026-11-21/README.md)`: flip -> first present median 51.7 ms (p90 51.9), split into flip -> flag read 8.8 ms and `page_ok_sent` -> first present 35.1 ms. The 35.1 ms is one helper pace stride (2 vblanks, 35.4 ms at period 17.71): the first picture after the confirmation waited for the next stride boundary of the old grid.
- `hypothesis until the device run`: skipping that wait once (`FramePacer::presentNextImmediately`, armed by the helper when the gate lets a held picture through) brings the step to about one vblank, flip -> first present about 17-25 ms. The update lands on the next vblank and starts a new grid; the picture after it is a stride later as before. All other pictures keep the vblank grid (`test: display_host "g134"`).
- `measured (XL, g133)`: at rest the helper's main loop woke once a second (62 voluntary context switches in 62 s), the 1000 ms cap of its `poll`. The duties of that cap, read from the loop: the GETPLANE check of a still picture (once a second), the debug dump file, the bench, the guard flag when no futex watcher runs. The LayerBlock heartbeat is written by the commit shim (not by the helper; `LAYER_BLOCK_HEARTBEAT_NS` 250 ms is the shim's freshness rule and is untouched), the status file is written by the plugin, the socket and the guard eventfd wake the poll. Now the sleep at rest is 3 s while a picture is on the plane (the still-picture check is made every 3 s instead of every 1 s; the check right after an update stays at 1 s) and 60 s otherwise (a bound for a signal that arrives between the stop test and the poll); debug files, the bench and a flag without a watcher keep 1 s (`gpu/guard_state.h` `LoopTimeout`, test `guard_state_host`).
- `hypothesis until the device run`: loop wake-ups at rest 62/min -> at most 20/min. Counters in the status file: `loop_wakes` (every return of the loop's wait) and `loop_wakes_idle` (timeouts that had nothing to do).
- Consequence worth knowing: a plane taken away by another program while our picture stays is noticed after up to 10 s (was 1 s); host commits that remove it are handled at once by the guard flag.

See also: [mpc-host-scripting.md](mpc-host-scripting.md) (host Lua API and why the plugin cannot change the host tab).

On 2026-11-21 the owner tested both Play ↔ Macros directions on the XL with build `b55fb41e`, saw no flash and accepted the handoff for develop. The gaps in the instrumented capture remain as documented above.

**Key 61 Mixer Filter PEEK (2026-11-23).** With the caption hitbox fix and Acid Saw Reso, opening F1 from a settled Mixer showed the previous faders through the Filter controls. With frame dumping off, primary-plane samples first matched the settled Filter at 3.09 and 3.60 seconds after the tap command. The embedded Filter and Mixer also requested opposite values of the native widget gate in the same update. The Mixer now owns the gate. The display thread reports new parameter values first, then combines the Filter and Q metadata refreshes when the primary plane shows the full-screen instance frame. A missing acknowledgement has a bounded fallback so host names still update.

On the resulting ARMv7 plugin (MD5 `93d4c32987846aa9b662c94a97161ec6`), both Acid Saw Reso samples already matched the Filter at 0.35 and 0.33 seconds. On Karplus Harp, alternating F1/F2/F1/F2 reached its settled pixel sample on the first or second capture every time. The comparison sampled every fourth pixel of the left knob area, with fresh DRM acquisition for each frame. The captures do not resolve the precise scanout latency. Injected Key 61 encoder events changed only the selected cutoff or modulation amount in the 2,637-parameter dumps. Back restored the same filter and changed zero engine parameters. Evidence is in the sibling directory `.hw-local/mpc-perf/key61-ui-20261123-DUJa/` (`time-fresh-baseline.log`, `ack-fresh.log`, `ack-repeat.log`, `ack-accepted.log`).

**XL Mixer and FX native backgrounds (2026-11-21).** In the factory look, the host skin supplies the fixed ground on the Mixer and FX pages. The first XL package reported `page_host_background: on` on both pages. A visible green marker on FX showed that recognition alone does not verify the composed picture. Compare the marker area with the adjacent ground on the composed output. The saved XL evidence is under `.hw-local/mpc-perf/native-mixer-fx-xl/` alongside the project. This observation does not quantify CPU savings. In the current skin the host owns the fifty Mixer strip controls, ten peak/hold meters and five PEEK buttons. The Mixer PEEK surface reuses the standalone host control trees and their Filter and FX parameter banks. Its transient view selects one editor while the base Mixer widgets are hidden. The overlay retains the panel backdrop, curves, Q-Link focus and the FX MOVE drag. Its opaque fallback remains available. The Mac test `mixer_native_ui_host.cpp` checks final composition and hit ownership for all five targets including the opaque fallback. The VST-dispatcher test in `play_env_lfo_host.cpp` verifies the exact engine writes, all sixteen Q slots and the rejection of late input after closure or a tab switch. The new native PEEK composition awaits physical XL verification. The overlay draws source-off controls in grey and leaves the dry rack level, destination and meters available under FX bypass.

**Preset changes during a deferred display calculation.** The oscillator display and compact Play LFO must acknowledge content identity only after the calculation using that content succeeds. A compute-budget deferral leaves the new content pending for a later update. The regression in `shell-mpc/overlay/tests/views_host.cpp` covers a deferred update. For a device comparison, keep the view open across A→B→A, verify each selected program, and compare the plotted shape with a freshly reopened view on the same program.

**Granular and spectral preset changes on XL.** On the open Osc A page, changing between two file-backed presets could show a different graph from the same preset after reopening the page. A long run on the earlier package ended with seven GPU acknowledgement failures and a CPU fallback. The timed CPU fallback captures converged within three seconds for the measured spectral pair. Treat GPU and CPU captures separately and require the helper to stay running for a GPU measurement. See the statuses and images in `.hw-local/mpc-perf/native-mixer-fx-xl/`.

The MPC display now receives the decoded oscillator content paired with the same published state snapshot. It uses the engine's immutable sample and spectral analysis during the display computation and keeps only the plotted data between calls. Audio processing continues using the existing raw-pointer publication path. The scene acknowledgement timer is timestamped at GPU submission after scene construction.

On the corrected XL package (SHA-256 `89c85acc3683802437e961b2a6e5fff2162efe3ae190e3196e6ad4f265cc9bfd`), the sample, granular and spectral Osc A plots changed between A and B and matched their A, B and A2 reopened references pixel for pixel. One running GPU helper served all 18 captures without restarting. Evidence: `.hw-local/mpc-perf/prepared-osc-xl/final-run.log`, raw planes and status snapshots.

### XL preset-curve checks

The evidence directories below are under the sibling `.hw-local/mpc-perf/` directory. Each entry describes the measured surface. Other instances or presentations of the same renderer still require identification.

| Surface | Saved evidence |
| --- | --- |
| Standalone ENV 1, LFO 1, Filter 1, Master Glide, FX Distortion | `native-mixer-fx-xl/curve-roi-results.md` |
| Play Sub, ENV and LFO, compact and expanded | `native-mixer-fx-xl/play-zoom-acceptance-20261121.md`, `native-mixer-fx-xl/reopen-reference-xl-20261121.md` |
| Filter NOTE and VELO with distinct stored curves | `native-mixer-fx-xl/qa-fixture-acceptance-20261121.md` |
| Standalone OSC A sample, granular and spectral, and expanded Play OSC A in five modes | `prepared-osc-xl/README.md`, `gesture-final-xl/graph-results.txt` |
| BUS 1 and BUS 2 Distortion PEEK with wet fixtures, and Macros spectrum with a repeatable note input | `prepared-osc-xl/README.md` |
| LFO 1 EDIT canvas and grid | `gesture-final-xl/editor-fixed-results.txt`, `gesture-final-xl/README.md` |
| Mixer MAIN FX PEEK across Breath of Machines ↔ Acid Saw Reso with panel open | `docs/dsp-knowledge/xl-main-fx-peek-acceptance.md`, `integrated-xl-ui-v6/README.md` |

**Mac preset recall through PageView.** `shell-mpc/overlay/tests/preset_graph_checks.h` opens the production views, decodes two preset fixtures, and compares the graph crops of each held A → B → A step with fresh views on separate instances. It compares the stored LFO points with a fixed grid, then changes the grid with fixed points. File-backed OSC fixtures use the own engine's prepared content, with repeatable sample data held in memory. Each static graph must publish the recall, match the corresponding reopened crop, and then stop reporting scene changes after the post-change cache handoff. No live readout is supplied to these cases.

| Mac surface | Covered presets and presentations |
| --- | --- |
| ENV and LFO | Standalone and expanded Play ADSR, built-in LFO shapes, stored LFO shapes, LFO EDIT, and resting RND styles. LFO EDIT also covers a grid-only change. |
| OSC | Standalone and expanded Play Sub, Noise generator and sample, Osc A/B/C wavetables, and Osc A sample, granular, spectral and multisample. Osc A also covers Frame Map, Unison, Region, Slice, Grain Engine, Grain Window, Granular Unison, Spectral Filter, Spectral Scan and Multisample ENV on both paths. |
| Filter, Master and Voice | F1 and F2 response, F1+F2 result, Mixer F1/F2 PEEK, stored NOTE, VELO and F1/F2 PZ SVF curves, Master GLIDE and the Voice PAN sequence. |
| FX and Mixer PEEK | EQ, Compressor, Delay, FX Filter and Distortion on the FX page and in MAIN, BUS 1 and BUS 2 PEEK. EQ, Delay, FX Filter and Distortion keep their plot in the tested MORE layouts. MAIN also recalls a changed module, EQ ↔ Distortion. Chorus MORE has controls only. |

The tests found two premature closures at preset recall. The standalone OSC page now keeps a plotted detail for the same powered source and oscillator type. The PZ editor keeps the same powered PZ filter open and reloads its saved points, discarding a previous draft and any pending Apply. The standalone native toolbar owns Back, Reset, Delete Point and Apply, while the overlay owns the point editor. The actual VST route exercises source isolation, preset recall and draft persistence. The PZ result covers the editor and stored points. The saved-point DSP path is tracked in [filters.md](filters.md). Macros spectrum uses live output and has its own `spectrum_view_host` checks.

On XL build `97318619`, Mixer Filter 1 and Filter 2 PEEK and Chorus MAIN followed open A/B/A preset changes. All three reopened controls matched on each surface. MAIN FX PEEK differed from its first reopened control by 138000 pixels. Chorus MORE had identical references and requires distinct fixtures. Raw captures and logs are in `grouped-xl-ui-v6/`; the comparison table and helper timings are in `integrated-xl-ui-v6/README.md`. The held-note FX timings used different editor selections and cannot establish a version-to-version performance change.

The 138,000-pixel difference on MAIN FX PEEK in `97318619` was traced to two issues: (1) a test script sequencing defect in `grouped-xl-ui-v6/curves-extra.py` where `tap(461, 137)` opened MAIN FX PEEK *before* selecting program A (`Acid Saw Reso`), capturing an invalid intermediate state left over from the preceding Filter 2 test (`Ring Mod Gamelan`), and (2) runtime stale-card retention across state generation increments before commit `ef9ff7d6` ("Retain matching FX selection across preset loads"). Commit `ef9ff7d6` ensures that when active modules differ between presets, the stale disabled selection is cleared (`selectedItem_ = {-1}`) and `rebuildChain()` selects slot 0 of the new preset's active rack.

The source-backed acceptance procedure for MAIN FX PEEK across `Breath of Machines` (`content/presets/FX/breath_of_machines.sbpreset`, fixed modules in Rack 0: EQ on [slot 0], Delay on [slot 1], Reverb on [slot 2]) and `Acid Saw Reso` (`content/presets/Bass/acid_saw_reso.sbpreset`, fixed modules in Rack 0: Distortion on [slot 0], Chorus on [slot 1]) with the panel held open is specified in [xl-main-fx-peek-acceptance.md](xl-main-fx-peek-acceptance.md) and automated via `scripts/measure-xl-main-fx-peek.py`. On the XL's 16 physical Q-Link encoders, wide mapping publishes the active editor parameters:
- Preset A (EQ selected): `q_targets` = `837 838 841 842 845 846 1500 -1 836 840 844 2635 839 843 847 -1` (OLEDs: `EQ Low Cutoff`, `EQ Low Gain`, `EQ Mid Cutoff`, `EQ Mid Gain`, `EQ High Cutoff`, `EQ High Gain`, `EQ Level`, blank, `EQ Low Mode`, `EQ Mid Mode`, `EQ High Mode`, `EQ Law`, `EQ Low Resonance`, `EQ Mid Resonance`, `EQ High Resonance`, blank).
- Preset B (Distortion selected): `q_targets` = `828 829 830 1499 831 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1` (OLEDs: `Distortion Type`, `Distortion Drive`, `Distortion Mix`, `Distortion Level`, `Distortion Filter Order`, blanks).
- Target-only verification: turning physical Q-Link encoder Q2 on Preset B modifies only `distortion.drive` (param index 829), leaving all other 2,636 engine parameters unchanged.
- Idempotence: all three open states match their reopened control byte-for-byte (`delta == 0` for `A/A-reopen`, `B/B-reopen`, `A2/A2-reopen`, and `A/A2` round-trip).
- `UNMEASURED ON HARDWARE`: exact differing pixel count between A and B in ROI `(65,310)-(1215,430)` (expected > 100,000 px) and RK3588 frame render timing during open-panel transition are unmeasured pending owner device execution.

In the LFO editor, the old picture persisted after the host selected program 78 from program 7. Reopening changed 146214 pixels. After the first fix, the points updated but the grid dimensions from 78 carried over to 7. On the corrected XL build, all three open views matched the reopened views and returning to 7 restored the original picture exactly. The host regression checks that releasing a drag after a passive preset change does not write the old points into the new preset.

The factory Voice and Master MAIN tab roots contain fixed panel backgrounds and block captions. The overlay draws each header title over the marker covers. The Master publishes a single transient visibility value: MAIN and TUNING each expose their own native widgets and the modal frame hides both sets. The fallback MAIN and the TUNING compositor cut out the active native cells from their furniture. `mastervoice_ui_host.cpp` checks the actual `PageView` pixel and touch ownership where Volume and Scale Type overlap, including the opaque modal frame. The test also compares the overlay-only static/live pass with the opaque reference at 1280 × 800. Master MAIN also has host-owned L/R peak and hold, voice count, CLIP and LIMIT. The engine accumulates separate channel maxima in its existing stereo peak pass. Mixer keeps the louder channel. The overlay sends these display values from its existing readout and updates the per-instance two-second clip latch. The Mac engine test compares silent, left-only and right-only readouts, and the Master page test exercises host apertures and clear. These pages await XL timing and composition measurements.

The five standalone oscillator tabs expose the selected source’s native POWER button on ON and OFF roots. The foreground leaves its entire rectangle transparent and hides the binding in oscillator details. `osc_page_host.cpp` checks both alpha and touch ownership and rejects commands from a previous source, detail, tab or preset. The packaged-skin test `test_native_osc_routing.py` verifies the five source handles and ON/OFF artwork. The Sub, Noise and Osc A/B/C roots expose their bound top-row and lower-panel values through SDK menus, keypad fields, dials and toggles. Their field manifest comes from `osc_page_host --native-settings-contract`. The same regression checks all five A/B/C oscillator types, the generated and sample Noise layouts, exact engine targets, Warp geometry, OCT remainder, SOURCE browser and stepping, OFF input and preset or detail invalidation. The routing menu uses separate source handles, and the host regression rejects a delayed write to the old source after the new source is published. An event first delivered by firmware after reopening the same source and instance has the same VST2 index and value as a fresh event. That delivery order remains a device check.

The Mod matrix publishes the route in each of its seven visible rows, after filtering and scrolling. The host owns the mode, group filter, ADD, and each row’s SOURCE, TARGET and MORE actions in both matrix widths. The overlay retains the quick-add board, scroll bar, live row decoration and grouped route pickers. MORE switches the native visibility value to the actual route editor. The grouped route pickers close all native apertures while open. `mod_ui_host.cpp` checks alpha and touch ownership, resolves the actual route under scrolling and filtering, and rejects queued actions from a previous row, view or preset. `test_native_mod.py` checks the generated skin rectangles and visibility conditions.


### XL Q-Link strip and the full-screen LFO

The XL front panel has 16 physical Q-Link encoders in a horizontal strip. The SDK's 4×4 row/column notation describes virtual mapping coordinates. Each encoder has its own OLED. [Akai MPC XL top panel, pp. 377–378](https://cdn.inmusicbrands.com/Software/37/MPC%20Live%20III%2C%20MPC%20XL%20-%20User%20Guide%20-%20v3.7.pdf#page=378)

On the XL at dccb5c73, the two chips on the LFO smoothing dial appeared 24 pixels apart and addressed Q4 (time) and Q13 (enable). The same pair appeared for all ten standard LFO instances. The updated XL layout places the pair on Q4/Q5, preserves contiguous timing and motion controls, and makes room for both KEY controls in key-follow mode. The host parameter-name test requests the two mapped names through VST2 and checks “LFO 1 Smooth Time” and “LFO 1 Smooth”. The four-knob layout retains its existing banks. Physical OLED rendering and the new layout on the installed unit require the grouped device gate.

**Compact Play ENV Sustain.** The normal Play skin binds its native dial to the existing Play Env Sustain proxy. The selected envelope supplies the real parameter in either time-unit mode. The compact foreground leaves the dial cell transparent and passes its pointer input to the host. The expanded envelope retains its own Sustain control and the native skin component is disabled at full-screen frame 24. Source and host tests: `82_play.py`, `test_mpc_skin_overlay.py`, `test_mpc_skin_no_fallback.py` and `env_ui_host.cpp`. The existing `play_restore_host.cpp` automation test covers isolation to the selected envelope.

**Standalone ENV.** The skin gates its eleven scalar cells, used/free selector and TARGETS action on the editor view. `env_ui_host.cpp` checks every scalar binding in both time modes for all six envelopes, including edits invalidated by a pending preset or selection change. Its composition checks distinguish transparent cells from opaque spaces between them. `controller_host.cpp` verifies suspension and resumption around the marked SDK keypad and rejects its queued edits after changing preset or tab.

**XL verification, 2026-11-22.** At fef53ace, all 20,736 pixels in the compact Sustain cell matched the primary framebuffer and had zero overlay alpha. The stock GenericLegacy knob draws a numerical value itself. A separate value label duplicated that readout in 15e86b01; the final skin draws one caption over the tab artwork and uses the stock readout. On Acid Saw Reso → Punchy Pluck Bass → Acid Saw Reso the selected Sustain was 100 → 10 → 100, with 1,229 changed pixels on A/B and zero on A/A2. A drag on ENV 1 and a drag after selecting ENV 3 each changed only that envelope's Sustain in the full 2,637-parameter snapshot. The standalone RND 2 list closure was verified independently on the same plugin binary by comparing four sampled frames with a fresh RND 2 selection. Raw data: `.hw-local/mpc-perf/native-dial-lfo-xl-final/` and `.hw-local/mpc-perf/native-dial-lfo-xl/rnd-confirm.log`, both in the workspace alongside the repository.

**Standalone LFO host controls.** The release skin binds 74 active control rectangles across six Standard/RND timing layouts. The C++ `LfoUi` emits this geometry, publishes the selected source and available controls, and validates writes against the same preset and layout. The graph and Q-Link decorations stay on the overlay. Native rectangles are transparent to the overlay and take their pointer input through the host. A grey readout replaces the Smooth time control while its switch is off. The host modal reuses the FX choice and numeric markers. The controller suspends its overlay and Q-Link routing while the host modal is visible, then restores the same instance if its page returns. The Mac tests cover the skin geometry, host menu ownership and stale writes on exit. The physical screen and encoder controls remain to be checked on MPC. Sources: `53_lfo_smooth.py`, `native_lfo_controls.h`, `lfo_ui.h`, `controller_host.cpp`, `lfo_ui_host.cpp` and `test_mpc_skin_lfo_native.py`.

**Standalone LFO/RND host background.** The two families share the factory tab gradient. The graph and family-specific panels stay in the foreground because their rectangles and captions depend on the source and timing mode. The native root uses the host gradient after the detector sees its sentinel. Fallback paints the same gradient outside active native control rectangles, and the shape editor and Targets remain opaque. The three markers, including their one-pixel compositor margins, occupy the first eight tab rows, above the native selectors. On the Mac the fallback and composed host paths differ by at most one RGB unit from 8-bit alpha rounding in each of the six layouts. Sources: `lfo_ui.h`, `lfo_ui_host.cpp`, `test_mpc_skin_lfo_native.py`, and the native background exporter. Device timing and compositing remain open for the XL pass.

**Standalone OSC readout selection.** Five tabs and Play reuse the same twelve readout subscriptions for Osc A–C. The factory and controller use the same deduplication for ordered parameter ranges, so the controller supplies the corresponding slice of `LiveValues` without growing its readout block. `OscDisplay` chooses the four values of the selected oscillator. The standalone page's transient source also selects the routing destination and overlay/Q-Link bindings. The Mac tests check distinct frame values for A, B and C, source changes from OFF, and host popup dismissal versus a real tab exit. Sources: `page_set.h`, `pages.h`, `controller.h`, `osc_page.h`, `osc_display.h`, `osc_page_host.cpp`, and `page_view_host.cpp`.

**Mac release-skin measurement, 2026-11-23 (1dcd797b).** The production SDK generation used by `test_mpc_skin_no_fallback` produced a 687,839-byte TUI and 147 PNG files totaling 237,605 bytes on disk. Summing width × height × 4 gives 43,490,244 bytes for one RGBA decode of each unique PNG. Of that, 35,368,960 bytes belong to the 11 distinct root backgrounds shared by 15 tabs. Firmware cache allocation and per-instance memory require device measurement.

**Mac SDK package, 2026-11-23 (a93b2e6e).** With the native Shape Editor and stock Play and oscillator destination menus integrated, the same generator produced a 676,341-byte TUI and 104 PNGs totaling 161,629 bytes on disk. One RGBA decode per unique PNG totals 37,502,548 bytes, 5,987,696 fewer than at 1dcd797b across all intervening changes.

**Mac CPU reference renderer, 2026-11-23.** A 1280 × 800 scene cleared the canvas once, then cleared six transparent 100 × 55 native-widget rectangles at y=200 and x=0,175,…,875. With clang++ -O2, one warm-up and 100 renders per sample, three alternating before/after runs measured 3035.8/62.5, 2869.5/62.3 and 2886.8/63.2 µs per frame. Before this change (80395d3e) each aperture copied and restored a whole canvas. The CPU fallback now clears the clipped rectangle directly. The GPU helper uses its existing scissor. These timings cover this synthetic CPU scene only; full-page costs on XL still need device measurements.


**Current Macros PageView.** The release skin owns eight writable macro dials and separate read-only captions and values. The overlay owns the editable names, Q-Link decorations and spectrum. The Macros root keeps the instance frame visible. The overlays for renaming and target editing request frame 24, which invalidates both sets of host widgets. A verified host background keeps the plane transparent over the controls. Without its sentinel, the fallback draws the static page around the controls and retains a GPU cell plan. The Mac test follows both directions through the Play/Macros key and checks native apertures, names, values, a real spectrum sample, and preset cancellation across two instances. The runtime and tests are in `60_macros.py`, `macro_ui.h`, `controller.h` and `macro_names_ui_host.cpp`. The Mac skin-only build for the Macros change (`555dec83`, 2026-11-23) produced a 557,036-byte `Plugin Skins/TUI.json`. The validator found 15 tabs and 15 Q-Link maps with in-range values. This is the serialized UI definition size. At merge `107a4c02`, the combined Play, ENV and Voice Mac SDK fixture emitted a 575,723-byte `TUI.json`. All 14 Voice checks ran against that generated skin and its corresponding Mac host binary, including the 14 control geometry and parameter bindings.


**Native Play Rise on XL, 2026-11-22.** The native host and overlay can both observe a touch in the same rectangle. On 59c2ba18, expanding Play LFO, selecting `+` → RND 1 and closing the expanded panel left compact RND 1 visible. Dragging in its STEREO region changed the obscured standard `lfo1.fade_in` from 0 to 2714.57422 ms. On c6949cf6 the VST setter rejected this Rise proxy write: the same drag changed none of 2,637 parameters, and tapping STEREO changed only `rlfo1.stereo`. On standard LFOs 1 and 3 each native drag changed only the selected Rise. All 14,112 Rise pixels and the neighboring LFO header and ENV Sustain matched the native primary scanout with transparent foreground. Reopening the original preset and LFO selection reproduced the Rise, graph and Sustain crops exactly. Raw reports: `.hw-local/mpc-perf/native-rise-xl/` and `.hw-local/mpc-perf/native-rise-xl-rnd/`.

**Earlier compact Play PAN/LEVEL and LFO Smooth.** The SDK skin pairs each oscillator choice with its own power parameter. While OFF the native PAN and LEVEL cells display the real proxy values in grey labels, with no focus or gesture handlers. The OSC foreground renders its plot, ground and decorations in grey while leaving the power control red and the source menu reachable. Play's marked host menus suspend all underlying Q-Link and pointer routes until the page returns. Standard LFOs keep a separate Smooth switch available when time editing is disabled. The Play LFO Standard handle hides Rise and both Smooth cells for RND, and the full-screen frame hides the compact widgets. The Mac checks cover the packaged gate combinations, foreground colours and held phase, menu pause and return, and blocked VST proxy writes. The composed device output and host touch timing require the XL gate. Sources: `82_play.py`, `test_mpc_skin_overlay.py`, `controller_host.cpp`, `qslots_host.cpp`, `lfo_ui_host.cpp` and `play_host.cpp`. The Play visibility poll publishes a first value even when a project restores RND before the first display tick. The Mac VST test holds the earlier Overlay Instance callback, restores RND, then checks the first visibility callback at zero.

**Native compact Play OSC.** The C++ control bindings and skin agree on seven source-local positions. Sub leaves the route position empty. The primary choices are ABC TYPE, Sub WAVEFORM and Noise SOUND. The second cell opens or steps the existing source browser for ABC, opens a nine-row signed octave selector for Sub, or edits Noise FILTER. The routing list keeps access to the balance and send controls. The route, waveform and octave overlays carry distinct action markers. An OSC source revision and preset generation invalidate delayed writes, including A–B–A while the same drawer remains open. `controller_host.cpp` checks the live stack, all seven foreground apertures, the plotted wave, OFF, preset and source transitions, physical tab and instance changes through the fake framebuffer, and unchanged Q bindings through the common Q suite. Opening an ABC compact panel reports seven controls and its frame once. A source swap sends seven new controls and two view updates. The unchanged panel emits zero further parameter reports.

On the physical XL the disabled compact SUB waveform and octave cells were entirely overlay-painted (14,544/14,544 pixels each). Pan and level were entirely host-painted (21,816/21,816 pixels each). Measured: `.hw-local/mpc-perf/xl-peek-tour-20261123-xCGw/new-tour-play1.{host,overlay}.raw`. The skin now provides passive waveform/octave readouts and a disabled expand image. The foreground clears all SUB control rectangles before painting any open popup; the graph stays on the foreground plane. Tests: `scripts/tests/test_native_play_osc.py` (generated skin), `shell-mpc/overlay/tests/controller_host.cpp` (SUB on/off coverage, Q1–4, target writes and preset invalidation). After the apertures were cleared, the XL still showed blank active fields and a pan drag wrote nothing. Moving the controls directly onto the tab with resolved handles restored the displayed values. A native pan drag changed only `sub.pan` (0 to 100) among 2,637 parameters. With SUB disabled all four readouts remained visible and tapping them changed no parameters. Measured: same directory, `flat-subcheck.log`, `sub-enabled-host.png`, `sub-pan-edit.params`.

The XL stock waveform menu showed one unlabelled row, and its numeric keypad converted an entered octave of 1 to −4. The two Sub drawers now bind native radio buttons to their source-local fields. The wave drawer lists six engine choices and the octave drawer lists −4 through +4. Selecting a row highlights it. Tapping the upper-left drawer header then commits the selection. On XL the full 2,637-parameter snapshots showed only `sub.waveform` changing from 0 (Sine) to 3 (Saw), then only `sub.octave` changing from −1 to +1. See `.hw-local/mpc-perf/xl-peek-tour-20261123-xCGw/` for the radio captures and saved parameter snapshots. The minified TUI with these 15 rows and its apply instructions measures 712,235 bytes.

**Native standalone FX editor.** The live FX table supplies each visible cell's geometry and parameter. The host draws the editor widgets and owns the menu and number popups. The overlay detects each FX popup's three colour patches in one primary framebuffer read, withdraws its plane and pauses Q-Link routing until FX returns. Mac host tests cover selection, cancellation, tab and instance changes, and an unrelated host popup (`fx_native_ui_host.cpp`, `controller_host.cpp`). Popup composition and input on MPC firmware await the XL device pass. The POWER, RACK, MORE/LESS and REMOVE header widgets use source-bound handles. The host only receives reports for the active source. The Rack menu adds a fourth marker to the same framebuffer read. Only that popup authorizes a rack write for the published source and preset generation. The Mac Controller test covers A/B/A with the same popup held open, followed by a late callback after the original item returns.

**Compressor BAND.** The stock three-button selector uses separate handles for the fixed and pool FX items. The enabled choices follow the compressor's band configuration. The selector stays in its right-hand cell when LIMIT removes RATIO. Mac SDK and dispatcher tests cover the visible and passive options, one transparent input area, the selected band's Q targets, callbacks arriving from another compressor and power-off priority. Band selection changes the active editor bindings and leaves the preset bytes intact. The Mac SDK skin-only build measured 697,426 bytes for the TUI.
**Standalone FX chain and gallery.** The C++ contract exports the two rows of sixteen hit targets, the page control, and the plot and button locations used by the SDK skin. `fx_native_ui_host.cpp` covers separate native selection and POWER, the full 32-card chain, reorder across pages, cancellation outside the chain, and the return to the native gallery after removing the last effect. The foreground pixel test preserves the preview plot and leaves the native gallery label visible. `controller_host.cpp` exercises the dedicated ADD menu's marker, repeat selection, cancel, a second preset recall while open, and a plugin instance switch. The filtered gallery exposes only eligible types when adding to a band or when the pool is full. These are Mac regression results. Firmware must still verify the repeated SDK control placement and actual menu timing.

**Standalone Filter markers.** The former POWER rectangle covered both the left identity marker and the instance picture. The release Filter skin places its three identity pictures in a widget-free top lane and uses a separate background sentinel. The Mac skin check verifies every native widget rectangle against those three pictures. `filter_ui_host.cpp` verifies that the foreground leaves live native cells transparent while the curve editor and OFF-state controls keep their overlay ownership. The combined scanout still needs the grouped XL pass. The release factory initializes native ownership independently of the background sentinel. For F1 and F2 the Mac root probe preserves the native Power and enabled controls when the foreground must supply the gradient. With the factory gradient verified, the overlay omits that full-tab paint and covers the three marker rectangles and sentinel. Detail pages keep an opaque foreground and hide native root widgets.


**Native compact Play Expand.** An ENV, LFO or RND Expand callback opens the corresponding full graph and publishes its Q-Link group in the same display update. The final stack state disables all compact Play banks before the first expanded frame is reported. CLOSE restores the compact graphs and Q-Link mapping. `play_env_lfo_host.cpp` checks the stock handles through the real VST dispatcher and verifies the rendered graph, CLOSE, and rejection of hidden ENV/LFO Expand and OSC PAN callbacks before the next display tick.

**Shared modulation editor controls.** The same native surface supplies navigation, paging, amount, polarity, active and remove controls to the source's TARGETS view and to the destination's modulation view. The overlay still supplies the grouped destination list and the source board. The bank is scoped to the presenting view, selected source or destination, visible page and preset generation. Installing a new route selects its actual row in table order when it fills a hole. `native_mod_targets_checks.h` and `native_modulations_checks.h` exercise the VST handles through the live page paths. The packed skin uses pass-through compounds for identical passive Name and Value labels. The ordered-key round-trip test expands them and compares the complete skin with the unshared form.

**Standalone OSC details.** All five OSC tabs share the same native component hierarchy. The active sheet derives its controls from the live oscillator bindings. The host paints the enabled controls in transparent cells, and the overlay draws the graphs and note mask. The C++ page and preset generations invalidate stale edit bindings. The root LOAD control for an empty sample, granular, spectral or multisample oscillator uses the same bank with separate A/B/C handles. Its aperture leaves the adjacent graph touchable and the existing file browser opens from the VST command. `osc_page_host.cpp` checks cell coverage, aperture transparency, navigation, power-off priority and draw state. The actual VST and Controller fixture in `play_env_lfo_host.cpp` checks dial drag and long-press ownership, the selected modulation destination and the marked Warp 2 drawer.

**Native Master navigation and linked bend.** MAIN and TUNING & MPE share a transient Master Panel handle. The Mac VST fixture verifies transparent apertures, the two Q-Link groups, identical chunk bytes and rejection of pending navigation across preset recall and tab exit. Pitch shows a single wide BEND dial while LINK is on and two separate dials while LINK is off. The SDK skin uses two size-independent Master dial definitions. The Mac SDK skin-only build produced a 692,151-byte TUI with 15 tabs and 15 valid Q-Link maps.

**Play foreground hashing and RND input.** OSC, ENV and LFO share the page scene hash, including curve samples and the text pool. The old compact Rise hit-priority branch has no role with the native RND STEREO binding. The actual VST fixture checks a yielded RND STEREO touch, one write to the active RND and an unchanged standard Rise parameter. The compact overlay fallback keeps its own STEREO gesture.

**Mixer ownership during background fallback.** The production instance declares its stock Mixer widgets independently of the framebuffer sentinel. If the sentinel is unavailable, the overlay supplies the static ground and clears those same widget rectangles. `play_env_lfo_host.cpp` checks a transparent OSC A fader and Master bypass, measures a single engine value change for each host callback, and opens the two filter and three FX PEEK panels with the sentinel disabled. Fixtures without the stock widgets retain the complete interactive overlay.


**Restored OFF editors.** The Mac VST regression restores a bypassed pool effect and both bypassed filters, opens their native editor or PEEK, and checks unchanged project bytes and zero engine writes after disabled gestures and all 16 Q proxy callbacks. The fallback FX chain and band lanes reject moves when the card is bypassed or the rack, power or preset changed during a drag. The FX preset test uses two pool slots to verify that a reopened MORE view follows the last rendered effect identity. The fallback choice list closes on recall or bypass and rejects a choice made before the next display tick. Returning power to Delay or either filter rearms exactly one handle for its feedback or cutoff.

The standalone VST regression in `saved_disabled_root_checks.h` exercises Sub, Osc A/B/C, Noise, Filter 1/2 and a pool Delay through the actual display controller. On the old path every tested root could still write through its earlier powered Q map after reading a disabled project and before the first display tick, once the restore window expired. Project and preset loads now invalidate the Q map and any queued virtual input. The controller republishes the map after a complete update for the loaded state. The regression asserts byte-identical chunks and zero UI writes or engine reports after OFF navigation, pointer gestures, same-value echoes and all sixteen Q inputs. It also verifies one genuine edit after re-enabling each module and the first fresh cutoff Q write after an ON-to-ON recall. Device touch, Q-Link delivery and save prompts remain part of the hardware check.


### Historical XL UI failures on the Mac

The 5f006ab8 XL notes distinguish the primary host framebuffer from the composited screen. ENV Sustain, LFO Smooth and FX Chorus POWER changed their intended values while the foreground obscured the native artwork. The Mixer target was MAIN FX Bypass, the Voice target was Rand Pan, and the Master test found overlapping MAIN and TUNING controls. One Voice probe at (160, 519) was in the Detune slot. The current packaged dial puts the input probe’s other coordinate inside the Rand Pan knob, (76, 583).

| XL observation | Mac evidence in the current tree |
| --- | --- |
| The overlay obscures ENV, LFO and FX controls | `env_ui_host.cpp`, `lfo_ui_host.cpp` and `fx_native_ui_host.cpp` verify native apertures and the active targets. The packaged SDK tests check the same controls and their visibility gates. |
| Oscillator name menu fails to open | `test_native_osc_selector.py` and the packaged overlay tests check the stock drawer binding and its action markers. The controller and oscillator tests cover menu suspension, choice, cancellation and target changes. |
| Filter and Mod left markers lose recognition | `controller_host.cpp` finds all 15 production views while alternating between two instances. Replacing each affected left marker with the RGB from the 5f006ab8 capture reproduces the failure. Restoring the production colour recovers the view. The packaged skin tests check marker placement and artwork. |
| MAIN FX Bypass and Rand Pan fail to edit, and Master Volume overlaps TUNING | `play_env_lfo_host.cpp` checks a transparent input aperture with zero overlay writes and confirms that only the intended value changes across all 2,637 engine parameters when its release binding is delivered through VST. The SDK checks verify each binding, and the Master page test verifies the separate MAIN, TUNING and modal visibility states. |

The Mac framebuffer and VST tests exercise this plugin's renderer, detector and input dispatch. The final XL tour still needs physical gestures and paired host and composited captures.

## Native LFO Shape Editor

The standalone MPC LFO skin exposes the 22 controls around the Shape Editor through append-only transient handles (3196–3230, including the visibility, availability and CURVE/SCALE display handles). The bridge uses the existing editor operations for undo, point and range edits, mode, phase, shape content and navigation. Binding revisions discard queued input when the preset, LFO or current point selection changes. Disabled widgets keep the same bounds with passive grey labels. The canvas, point gestures and Q-Link annotations remain overlay-owned. The Play expansion continues to use its own overlay controls.

The standalone Expand button shares the upper-right graph bounds used by Play. The built-in graph’s panel omits that cell, and the curve, phase marker and live voice positions draw below it. The Mac host compares the rendered cell at rest and with live phase/cursor data and verifies transparent touch ownership on the standalone tab. See `shell-mpc/overlay/tests/lfo_ui_host.cpp` and the generated skin contract. The XL still needs verification of the host's fractional NumericOverlay entry for X, Y and SCALE.

The Mac SDK package with the deduplicated Mod route rows and the FX header (prior to FX chain navigation) measured 692,578 TUI bytes and 43,741,124 bytes for one RGBA decode of each of its 161 PNG files. The seven Tool choices use stock Buttons with distinct artwork for each state. Each conditional field shares an SDK container for its active control and same-size passive readout.

The standalone Point Mod view uses the host's BACK, MODULATE, AMOUNT and REMOVE widgets around the overlay source board. The host selects one of three fixed targets or enters a signed amount. Empty and mixed selections have separate passive modes. SHAPES and MORE bind live captions and guarded actions to the eight visible entries, including scrolls and nested confirmations. The native SDK's MenuOverlay reads static `replacementStrings`, so the dynamic entries use stock Buttons and Labels with the list's current names and geometry. The overlay retains the scrollbar, backdrop dismissal and text entry for naming a shape. The Mac tests exercise a 12-file scrollable library, current-row loading, cancellation, exact apertures and preset recall while the copy list stays open. The Mac skin-only package with the FX chain and these LFO details measured 670,834 TUI bytes. Device touch delivery and popup composition remain in the XL acceptance round.

**Compact Play ENV and LFO.** Both graph corners use native Expand buttons, while the overlay draws the plots, live Env dots and LFO cursors. The compact widget contract comes from the same PlayView geometry used for raster apertures and touch. The native standard SHAPE and HZ/BPM/KEY controls follow the selected LFO. KEY disables RATE and releases its Q-Link. A BPM reselection keeps its straight, dotted or triplet subdivision. The RND layout adds STYLE, SYNC and STEREO. Standard mode uses handle 3348 and RND SYNC uses 3309. The Mac VST test reproduces a delayed RND drawer callback against a freshly reported standard mode with the old shared handle, then checks that the separate handles reject it and both controls accept fresh input. The legacy standard selector remains part of project state. The native menu contains LFO 1–10 and RND 1–4. Selecting the previous standard index clears the retained RND view. Marked popup sessions expire on preset, source or tab changes. The Mac VST regression exercises restored RND with report echoes, A/B/A source and preset changes, a tab departure, and a selection and shape event arriving in the same display tick.

The Mac Controller test reads the packed framebuffer marker patches for the source and waveform drawers on the standalone LFO tab and compact Play. In both four- and sixteen-Q-Link layouts it sends a real Q proxy write, wheel input and a held touch through the same controller paths. Cancel restores the original RATE route without applying the held touch or changing the selection or synth values. Compact Play restores RATE on Q2 in the active four-knob group and on the fixed Q10 in the sixteen-knob layout. The actual MPC drawer, touchscreen and physical knobs remain part of the XL test.

## FX overlay update cost (Mac arm64)

The standalone FX editor and Mixer FX PEEK now read a common 112-index rack snapshot for membership, order, pool types and power. The selected editor still reads its live controls directly. ADD and REMOVE read the affected pool slot's values when comparing them with the defaults. The rack snapshot also supplies the indices for the structure signature and removes the per-frame splitter key lookups.

On an Apple M5 Pro, the instrumented FX fixture measured 1.73–2.45 µs per update in the old full-table read and 14.91–21.64 µs in the old structure signature. The separate uninstrumented A/B/A/B/A comparison used nine samples of 200 updates per state and binary. Median update times for an empty rack, active Delay and open Delay MORE were 20.29→0.31 µs, 25.53→5.10 µs and 25.18→4.74 µs. Instance reads fell from 2,637 to 112 when empty and from 2,663 to 138 with Delay active. This fixture uses the real `FxUi::update` with a counted demo instance and measures the UI update alone on Mac, with no screen rendering or physical hardware. Probe and raw samples are in the shared private `.hw-local/mpc-perf/mac-fx-update/` directory.

## LFO and FX animation cache (Mac)

A Mac probe feeds the production Controller through the GPU submission interface and runs the helper's cell planner and layer cache. It uses a fixed 40 ms tick, sixteen changing live samples, a stop, a settled window and a fresh edit. The A/B/A/B/A runs agree for the native background and opaque fallback paths. They describe submitted scenes and planned damage on Mac.

- The standard LFO submits 16 playback scenes with cursor marks and zero cached damage rectangles. It submits one scene to remove the cursor, then none at rest. Changing PHASE sends a live curve and a second scene to return the curve to the cache.
- RND holds the last trace on missing readout or when the voice list is empty. A real midpoint sample is an ordinary zero, including an entirely flat trace. Stop now submits one cache handoff after the 350 ms settling window, down from 192 scenes that filled the trace with zeros. The handoff and last live picture are pixel-identical. Recording resumes at the new note, while a preset or RND selection change starts a fresh history.
- Distortion live input marks are drawn after the cached transfer curve. Over 16 active scenes, the helper's layer damage goes from 16 rectangles to zero, then the normal partial paint clears the old marks on stop. The active and stopped reference images match the preceding implementation. A drive edit still invalidates the curve. The standalone and Mixer PEEK host tests verify the same cache boundary.

The probe and raw samples are in the private `.hw-local/mpc-perf/mac-lfo-fx-animation/` directory. Helper GPU cost, CPU and wake-ups still require the device runs.

## Play and oscillator edit cost (Mac arm64)

The interactive Play page now uses the LFO samples already computed for its visible graph. The read-only view still supplies its own samples. The interactive diagnostics read the visible standard shape and cursor, or report the selected RND style and dump the retained random trace. The standard shape hashes can differ from earlier builds because they now describe the rendered point set.

The oscillator display retains the fixed backing frames during a FRAME-only edit. The current wave and its visible spectrum still update. Changes to source, prepared content, display detail, warp or morph refresh the backing frames. The Sub and the non-wavetable types skip the harmonic spectrum calculation that their graph does not show.

An Apple M5 Pro ran the same controller probe before and after this change with identical core libraries, compiler options and inputs. The table gives the pooled median wall time inside the view update in µs per tick. It uses nine 160-tick samples per surface and state per run, in A/B/A/B/A order. Native and fallback refer to the overlay with and without the verified host sentinel.

| Edit | Native before → after | Fallback before → after |
|---|---:|---:|
| Play LFO PHASE | 157.7 → 139.8 | 165.3 → 143.1 |
| Osc A FRAME | 497.6 → 200.0 | 494.6 → 198.6 |
| Osc B FRAME | 499.2 → 201.9 | 494.3 → 199.8 |
| Osc C FRAME | 494.2 → 203.3 | 496.1 → 200.7 |
| Sub WAVEFORM | 216.1 → 103.5 | 216.6 → 105.7 |
| Noise FILTER | 90.0 → 28.3 | 92.1 → 30.3 |

On the separate instrumented run, the native Osc A edit went from 160 backing-stack calculations to zero and retained all 160 front-wave and spectrum updates. The Play LFO edit went from 160 duplicate legacy calculations to zero and retained all 160 visible LFO updates. Idle, held-note and edit checkpoints have identical overlay pixel digests before and after on both paths. The host tests also compare a cached FRAME and modulated WARP change with a fresh oscillator display and verify an immediate refresh when prepared content arrives for the same state object. The timings cover the synthetic display fixture. Firmware composition, touch delivery and physical device timing remain part of the XL and Key 61 runs.

The probe, exact source patch, link identities, paired pixel digests and raw samples are in the private `.hw-local/mpc-perf/mac-playosc-update/` directory.

### Expanded Play control ownership

Play mounts the standalone OSC, ENV and LFO/RND control definitions only while the corresponding editor is open. The OSC editor uses the same `OscUi` and `OscDisplay` objects as the compact Play view, so opening it requires no additional readout subscription. The expanded view and its three CLOSE actions use transient indices 3537–3540. `play_env_lfo_host.cpp` drives their VST callbacks, checks the transparent CLOSE geometry and verifies that the compact Q-Link routes return after closing. The overlay still draws the custom curves and their live indicators.

### Mixer Filter PEEK modulation routing on the Mac host

The Mixer can open the native modulation editor from a long press on F1 or F2 cutoff. The Play VST host fixture checks that the Q proxy edits the selected route, Back restores the same Mixer filter and its cutoff binding, and the standalone Filter tab retains the view saved in the recalled preset. The Mixer overlay fixture checks that native field apertures remain transparent outside the Q-Link badge and that input outside the panel stays modal. These checks exercise the Mac fixtures. Physical Q-Link and touch checks remain on the device checklist.

Evidence: `shell-mpc/tests/native_modulations_checks.h` and `shell-mpc/overlay/tests/mixer_native_ui_host.cpp`.

## XL Mixer Filter PEEK and 15-tab tour, 2026-11-23

On the XL with the 4bb84e82 ARM64 plugin, all 15 tabs reported their expected view and running GPU with paired primary/composite captures. With the db99d6f2 caption fix, tapping FLT 1 or FLT 2 opened the corresponding native panel. On Breath of Machines, Q1 targeted filter1.cutoff (90) or filter2.cutoff (105). Holding cutoff opened Modulations; Q1 then targeted mod02.amount (336) or mod05.amount (357). Native BACK returned to the same cutoff and CLOSE restored the original Mixer target list. Target identity was observed in live status, independent of physical knob travel. Raw statuses and images: measured: .hw-local/mpc-perf/xl-peek-tour-20261123-xCGw/.

Physical XL Q1, Breath of Machines, ARM64 binary from 487b7e98: in separate full 2637-parameter comparisons, only filter1.cutoff changed (1500 to 5050.604 Hz), then only filter2.cutoff (130.800003 to 398.331146 Hz). The initial F1 turn included motion in both directions. After CLOSE the 16 Mixer slot assignments exactly matched the pre-PEEK snapshot. These are direct cutoff turns; physical travel in the modulation amount editor is a separate check. Evidence: measured: .hw-local/mpc-perf/xl-peek-tour-20261123-xCGw/owner-f{1,2}-{before,after}.params.


### Standby on the native XL Play skin

On the XL running 487b7e98, the one-minute screen dim dropped the backlight from 29 to 3. The host blurred the three Play identity squares against a blue row gradient, with no RGB darkening. For example the Play marker centre went from (114,58,44) to (80,46,48), against (20,26,56) on its scanline. The previous plain-grey backdrop check lost Play and hid the plugin layer. Raw awake and resting frames and timestamps are in `.hw-local/mpc-perf/xl-mainfx-20261123-G5hD/` (`idle-return.raw`, `one-minute-rest-host.raw`, `one-minute-timeline.txt`). The native-skin check uses the host's backing from the same scanline outside all marker footprints and requires the full blur profiles of both view markers and the known instance marker. `standby_host` replays the measured pixels and rejects the wrong instance.

### Native-only compact Pan/Level and MacroCard

The compact Play Pan/Level and MacroCard retain their existing Knob state, geometry and Q adapters outside the overlay child list. They have no GPU base-control drawing or pointer/wheel route. Macro names/rename, spectrum and Q focus decoration remain overlay-owned; TARGETS arrives through the native action callback. Host availability never selects the retired dials or destination chips. Adapter enable/inactive state follows its parent explicitly.

Evidence: `shell-mpc/ui/tests/ui_composite_host.cpp` (`testNativePanLevel`), `shell-mpc/overlay/tests/osc_ui_host.cpp` and `shell-mpc/overlay/tests/macro_names_ui_host.cpp` (`testStandaloneMacrosNativeControls`, native availability/draw-pass regression). These are Mac source/scene/input checks; device input delivery still requires the hardware acceptance.

### Native Voice step knob ownership

Voice step selection, all eight column values and the selected step summary use native buttons and labels. Each of the six rows binds the same shared bipolar dial definition to eight fixed engine parameters. The display reads only the current cell for Q4. VoiceGrid paints the sounding-step indicators. Both Master panels and Voice leave the native canvas visible before the background sentinel is detected. Their native titles start to the right of the host marker and instance patches.

The generated skin is compared against the compiled input geometry in `scripts/tests/test_native_voice.py`. The rendering and input checks are in `shell-mpc/overlay/tests/mastervoice_ui_host.cpp` and `shell-mpc/ui/tests/ui_voice_grid_host.cpp`.

The Key 61 production skin `be4b5924cbe3` and ARMv7 plugin `466df9a0e8fd` were measured on Sbtest / Acid Saw Reso with the 85 ms footer-tap probe. In two returns per page the first fully settled sample ended at 436.10 and 436.46 ms for Master/Main, and 439.20 and 436.76 ms for Voice/PAN. All four matched their resting full-screen references across the plugin and footer. Raw timing traces, reference comparisons and package digests are under `.hw-local/mpc-perf/key61-native-master-voice-be4b5924cbe3/`.

### FX native ownership across overlays

The standalone FX native control tree remains native when host-background detection is absent or an overlay list is open. A modal list conceals/disarms the native chain, header and cell bindings without selecting the retired base widgets. The shared legacy editor is still used by Mixer FX PEEK when `MixerUi::useNativeWidgets()` is false; removing that shared branch depends on completing the Mixer native contract for every supported production canvas.

Evidence: `shell-mpc/overlay/tests/fx_native_ui_host.cpp` checks host-background loss and a retained type-list transition, including hidden native bindings while the list owns input. These are Mac source/contract checks.

### Native-only Master controls and readouts

Master no longer has a GPU control mode, side-selector fallback, choice popup or numeric-entry keyboard. Its native controls keep geometry, formatting and Q/state adapters outside the overlay children, with explicit parent enable/inactive propagation. The portamento curve and scale keyboard remain overlay-owned. Peak/voice/clip readouts are plain state/geometry structs; the actual ClipHold latch remains in IInstanceView. Native side callbacks select the panel; native numeric controls write their original VST engine indices.

Evidence: `mastervoice_ui_host` checks transparent native controls/selectors, readout updates, native panel callbacks, parent disable/inactive behavior, Q edits and 95-parameter reachability. `play_env_lfo_host` exercises the actual Master volume VST engine-index write and native navigation. Its full run currently reports two separate Play ENV/LFO/RND Expand assertion failures at `shell-mpc/tests/play_env_lfo_host.cpp:545`; no Master assertion failed. No device verification is claimed.

### Factory text entry and the external-plugin return path

The actual XL 3.10.1.12 factory overlay and host binary were inspected read-only.
Its submitted-text helper checks an element string capability before calling its
string setter. Both `PluginWrapperElement` and `ProgramEditParameterElement`
implement that capability as false and the string setter as a no-op. A JSON
keyboard or a plugin-side `effString2Parameter` override therefore does not
establish a text return path through those elements. The physical XL Numeric
Keypad is documented for selected numeric fields. Macro naming and LFO shape
Save As/Rename retain working `ui::TextEntry` pending a real plugin-owned text
handle. Exact source identity, call addresses, scope limits and acceptance
prerequisite: [native-text-entry.md](native-text-entry.md).


### Native Mod grouped pickers (2026-11-24)
`ModUi::publishNativeBoard` also publishes a seven-row native picker for source, destination, AUX and new-target selection. The quick board retains shortcut chips and adds ALL SOURCES for all 57 engine sources. BACK cancels, GROUPS returns to group selection, PREVIOUS/NEXT page the actual group. Fields877..879 keep their existing meanings; new list fields880..899 append after them. Native cell revisions reject stale page events and the bridge invalidates its bank on preset-generation changes. `mod_ui_host --native-lists` verifies selection, paging, cancellation, source57/new-target creation and zero overlay operations for the picker. This is Mac source/host evidence; physical XL confirmation remains in the combined device run.


### Mixer true stereo native meters (2026-11-24)
Source: MPC SDK `docs/ui-reference.md:1333–1357` §6.15: each native Meter reveals `peakImage` over `inactiveImage`, with separate `peakHandle`, `peakHoldHandle`, `peakHoldSize` and `peakHoldColour`. The stereo block instantiates one documented Meter per side plus one passive Meter per clip cap. No RMS handle/image is bound because no RMS measurement exists.

Source: `Voice::render` adds each source/filter stereo output into fixed 7×2×32 sample sums, before peak detection across voices. Source/filter taps are post level/pan, before routing/ENV1/Mute/voice-count compensation; bus taps are post rack/level/Mute before destination routing, including upstream-bus input; Master is final stereo output after safety/Mute, before mono fold. Audio destination buffers and computations are untouched. Peak display uses the existing -60..0 dBFS/300 ms release, independent per side; holds stay one second then release, and actual raw sample peaks ≥1 latch each side's clip for two seconds. Reset clears the telemetry.

The native geometry is 95 px fader +2 px gap+18 px stereo block (8+2+8), same 115 px strip. Full-size 8×192/408 artwork uses green signal, amber -6..-1 dBFS, red -1..0 only; clip caps are separately driven by the raw signal latch. Appended read-only fields900..959 leave prior IDs untouched. Test: `mixer_stereo_host` (the narrow actual-VST host target) reports zero failures for all ten hard-left/right strips, centered source, 24-voice waveform clip at unchanged gain, independent holds/reset, bit-identical audio with metering enabled/disabled and all 60 read-only/non-automatable VST fields. `UiScreenMixerTests.test_true_stereo_meter_contract` checks the generated production skin: ten shared strip instances, 60 unique parameter bindings, four passive Meter children, no RMS alias and exact-size native artwork. The canonical Mixer preview can consume that engine fixture through `render_mixer_preview.py --meter-readout`; it remains an offline composition, not a hardware screenshot. Native on-device composition and performance are not claimed.

## Play native control padding (XL capture, 2026-11-24)

The `deploy-4cfb7c90/XL-current-20261124-214335-{primary,composite}.png`
frames under `.hw-local/mpc-perf/xl-load-incident-7b5a79da/` show the cause
of the blue rectangular margins around the Sub buttons. At screen (18,220)
both frames and the installed `overlay_bg_play.png` (tab coordinate 18,110)
have RGB (20,26,56). Immediately outside the native aperture, (82,220), the
primary remains (20,26,56) while the composite is (18,18,18). The installed
64×56 power and expand artwork has alpha 0 at (0,0), (1,28), (6,3), (63,28).
The rectangular hole therefore exposes a blue native background next to the
overlay's disabled grey ground; it is not a second native button style.

The background producer additionally copied the old expand control from
screenshot rectangle (766,192,830,248) beneath each live native expand button,
and baked carousel frames beneath native selectors. Those copies are removed.
The native compact Play path now keeps its host background intact and renders
oscillator data without another ground pass; curve dimming remains independent.
The existing raster was patched only in those duplicate-control rectangles,
since the current gallery image differs from the historical generator input.


## Active skin page and Play layout (SDK checked 2026-11-24)

Sources opened: public SDK `docs/host-contract.md` (UI and host information),
`docs/ui-reference.md` §3.5 tabs and §7.2 modifiers, plus the local framework
at commit `febd2ca5bd9eb16c4a412837196e145566bc981b`. The current upstream
versions were also read at
https://github.com/inmusic-software/mpc-plugin-builder-sdk/tree/main/docs .
Neither this VST host contract nor its framework exposes a current skin tab
name/index callback. `tabName` configures the host's labels; `TabLimited`
restricts a host data binding and `Local` stores host-only UI state. These
are not documented plugin page notifications. This is a limit of the checked
public contract, not a claim that no internal host implementation exists.

The overlay therefore retains its measured primary-framebuffer page + instance
markers. This is a project-specific visibility check, not an SDK API. Play's
6×6 markers and instance stamp now occupy tab y=1..6, above selectors starting
at y=10. The source selector uses the full panel width (x14,w408), and LFO
uses x864,w396. The previous x84 offset existed only to avoid markers at y24;
there is no SDK requirement for that gap. Skin and detector consume the same
layout manifest and generated header.

The f4eb8f0e XL composite revealed a second source of rectangular seams:
`ui/display.h` applied the disabled veil outside the native apertures even in
its data-only pass. Native-header graphs now dim their curve without that
whole-panel veil. Compact ENV/LFO ground-strip gradients also restarted per
strip around native controls; Play now leaves those gaps to the native root
background. Graph drawing and native controls retain their existing geometry.

### XL native control backgrounds, 2026-11-25

On the XL with the 4fd1ab00 ARM64 package, the two Play expand slots and their adjoining graph backgrounds match at y=220 (RGB 9,13,25 on each side at x=764/768 and 1188/1192). The Macros capture shows eight LEVEL controls. With the same Acid Saw Reso preset, OSC A shows the enabled TUNE controls; B and C show one disabled MORE button and one routing readout. See the current Play, Macros and OSC photographs under `docs/screens/` in the capture commit following 4fd1ab00. These observations cover rendering.

### Key 61 native Mixer with the overlay disabled (2026-11-25)

On the Acid Saw Reso preset in Sbtest, OSC B was inactive. Dragging its PAN control left the captured native cell pixel-identical. With the overlay kill switch set, the DRM overlay plane had no framebuffer. Three primary-plane samples of the seven source strips' PAN/send area matched the same area before shutdown. Removing the switch restored the overlay without a tap. Before the controller fix, the instance marker could remain at the blank frame (14c8d8) indefinitely after shutdown. The device run used TUI SHA-256 6c408451… and arm32 binary d1dd25f5…; evidence and sampling scripts are in the shared hardware directory, key61-native-only-20261125. These measurements cover the installed skin and sampled frames. The MPC SDK ui-reference.md §§1.2–1.3 documents host-generated sliders when the skin file cannot be found at all.

With TUI e5214421… and arm32 binary 5c223c71…, each of the 15 physical host tabs was opened on Key 61. In each case the overlay was disabled, its plane was confirmed detached, and both custom tab marker colours were read from the primary framebuffer twice. Each overlay recovered after removal of the switch. This samples frame states at rest; it does not establish what firmware would do if the installed TUI were externally deleted. The stock SDK knob grid, focus frames and parameter captions removed from the generator produced no byte change in the complete 560-file custom skin.

On the same device, the five Mixer editor tabs opened by touch and each returned to MIXER by touching the first tab. Dragging the inactive OSC B PAN, B1 and B2 controls left all 2637 engine values unchanged. The refreshed Mod root showed seven readable routes and a single add action. Touching EDIT then BACK kept all engine parameters at their initial values.

### Key 61 Voice ↔ Master navigation (2026-11-25)

With Acid Saw Reso idle in Sbtest, the old native tab marker appeared well before the controls. The native Master root requested the full-screen/modal instance frame, which also hides the native Voice controls. Leaving Master separately set its panel visibility to Hidden. The host could therefore paint either destination with its controls missing. The native roots now share the normal instance frame. Master retains the selected panel for painting while disarming input on exit. Voice retains the selected row, step and parameter index for host reads. Its value comes from the current parameter mirror. The active binding still gates all writes.

The sequence reports just the visible values to the host. A row change also reports the value if its formatting changes at the same normalized position (for example PAN midpoint versus zero cents). Q-Link remapping still invalidates the host's cached metadata. Warm baseline switches showed no additional sectors read from the sampled block devices and no major faults in the MPC process. Those counters cannot exclude reads satisfied from the filesystem cache.

On firmware running the unchanged TUI (SHA-256 0a1d33ede6dd…), the interleaved original, intermediate and final ARMv7 packages were measured with an 85 ms tap on the shared footer. The endpoint was the first sampled composite matching the settled plugin and footer across all 1280 × 690 pixels, including the last displayed value. Original package 2ddb600bc0f1… had sampled upper bounds of 2.32–2.34 s to Master/Main and 2.56–2.93 s to Voice in the two repeated control arms. The intermediate package brought the controls back early but left the 109 pixels of the Voice MID label blank until 2.75–2.78 s. The read-only selection cache in the final 1b505c0a3779… package covers that last field too. In four transitions each over two installations the final upper bounds were 435–444 ms to Master/Main and 437–467 ms to Voice. The final frames matched the original settled images byte for byte. DETUNE kept the selected row and its zero-cent readout on return. Timings bound software-visible frames. The electrical touch latency and physical panel scanout were not measured. Raw captures, per-switch bounds, package identities and method are in the sibling directory `.hw-local/mpc-perf/key61-voice-master-latency-20261125/`.

### Key 61 screen review, 2026-11-25

The physical Key 61 showed an empty SOURCE drawer until the LFO skin supplied its own named rows. The two-column native drawer exposes ten LFOs and four RND sources. Selecting RND 1 changed the editor on the device. The Voice TARGETS group needs a valid parent binding for the five native switches to appear. Screenshots use the primary and overlay planes together at 1280×800.

With Acid Saw Reso and a three-second C4, the Play and Filter graphs and Macro spectrum changed between samples. On Mixer the native meters moved in the primary plane while the GPU overlay frame counter stayed steady. For device measurements see the shared hardware directory `.hw-local/mpc-perf/key61-screen-review-20261125/`.


On the same Key 61, switching Distortion and Chorus originally settled in the sampled range 5.95–6.83 s. The FX editor had instantiated all 149 cell positions for each of 38 header states. The skin now has two cell layers, a single foreground selection target per chain card, and shared header labels. Slot changes report the affected FX controls directly. Q-Link name invalidation runs once on the following Q poll. Omitting it altogether left Distortion names on the Chorus Q-Link panel even though the values had changed.

On the accepted package (plugin SHA-256 prefix `c7663816b7ad`, skin `78e28bb02cc7`), the composed Key 61 editor first matched the entire settled reference by 254 and 271 ms for Chorus and 255 and 246 ms for Distortion. Each trace remained equal through 6.5 s. These are upper bounds from four touch injections, with roughly 0.2 s between samples. The original baseline used slower remote frame sampling. The host Q-Link popup showed Distortion Type/Drive/Mix/Level, then Chorus Rate/Sync/Depth/Feedback, and then the original Distortion names. The test restored the original popup setting and restarted the app without the test adapter.

### Key 61 FX → Play, 2026-11-25

On Sbtest / Acid Saw Reso, four FX footer → Play taps on the original ARM32 binary (466df9a0) first matched the complete resting Play controls and footer by 4.46, 4.51, 4.84 and 4.19 s. The visible graphs preceded a blank body. The standalone native FX root asserted the full-screen marker that masks Play. Leaving Play also discarded its compact OSC and ENV/LFO display bindings. Keeping the ordinary instance marker at the native FX root, retaining the compact bindings read-only while away, and coalescing metadata refreshes gave 1.13–1.15 s across four returns on two installations (e7ac6dd8, identical TUI be4b5924). Gesture revisions reject writes from an earlier visit. Embedded FX racks and plugin-drawn lists retain their mask. In the same experiment, changing only the FX marker or only retaining bindings and coalescing notifications left a multi-second delay.

The probe tapped Play for 85 ms and sampled both display planes. Times are acquisition upper bounds over all 1280 × 690 plugin and footer pixels with approximately 310 ms between samples. The unmodified binary was reinstalled between the two combined runs. Raw logs, package identities, the reference comparison and the separate denser sampling run are in the sibling directory .hw-local/mpc-perf/key61-fx-play-20261125/.

### Key 61 · first visible Play controls after FX, ENV and LFO

Measured on the unchanged e7ac6dd8 ARM32 binary and be4b5924 TUI. In two FX returns every sampled plugin/footer pixel matched either the outgoing frame or the settled Play frame. All measured regions matched Play by 0.95 and 0.96 s. ENV and LFO returns had a different sequence, including a nearly uniform plugin body. After ENV the full Play image matched by 2.25 and 2.41 s, after LFO by 3.29 and 3.32 s. In both routes the selector strip and oscillator controls settled one sample before the ENV dials and graphs.

The standalone ENV and LFO updates enter their open editor paths, which request the full-screen instance frame. Play gates native widgets on that frame. Between the two normal runs an isolated diagnostic forced the existing sole instance's ordinary marker, keeping the same skin and plugin. All measured regions then matched by 0.76 s from ENV and 0.77 s from LFO without a sampled blank body. The override was removed. This diagnoses the transition; a shipping fix must still distinguish the actual modal/expanded editor. Raw frames, regions, and acquisition bounds are in the sibling directory .hw-local/mpc-perf/key61-play-control-arrival-n9Bc5K/. These measurements do not apportion the remaining pre-paint time to host construction, I/O or scheduling.
