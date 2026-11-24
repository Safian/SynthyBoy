# MPC Main-Layer Atomic Commit & Takeover Shim (`xl-commit-shim`)

Copyright (C) 2026 Safian Szabolcs  
SPDX-License-Identifier: AGPL-3.0-or-later

## Background

A read-only survey of the MPC host application (`/usr/bin/MPC`, statically linked JUCE, software CPU renderer, no EGL/GBM) established that:
- The host sets `DRM_CLIENT_CAP_ATOMIC` and acts as the DRM master on `/dev/dri/card0` (one fd).
- The host calls `drmModeAtomicCommit` via the libdrm PLT from two call sites:
  1. **Call site (a) - Frame Present**: request with primary-plane properties (`FB_ID`, `CRTC_ID`, `CRTC_X/Y/W/H`, `SRC_X/Y/W/H`, `FB_DAMAGE_CLIPS` blob), flags `DRM_MODE_PAGE_FLIP_EVENT` (0x1) and non-NULL `user_data`. Presents only on UI events (median commit ~13 ms on XL including vsync wait).
  2. **Call site (b) - Cursor Move**: flags `DRM_MODE_ATOMIC_NONBLOCK` (0x200), `user_data == NULL`.
- Measured hardware topologies:
  - **MPC XL (aarch64)**: CRTC 82 (`800x1280`), primary plane 34, cursor plane 40, overlay plane 46 (spares 52, 58, 64, 70, 76).
  - **MPC Key 61 (armv7)**: CRTC 37 (`800x1280`), primary plane 33, cursor plane 35, overlay plane 38 (spare 40).

## Purpose

To eliminate `SETPLANE` display-lock contention and eliminate stale overlay frames during host page transitions:
- **Stage 1**: Non-intrusive measurement ring + 5-second summaries.
- **Stage 2**: `DRM_MODE_ATOMIC_TEST_ONLY` probe (`/tmp/xl-commit-shim-stage2`).
- **Stage 3**: Live merged test rectangle on the discovered free overlay plane (`/tmp/xl-commit-shim-stage3`).
- **Page Guard**: Synchronous host atomic commit guard via `/tmp/synthyboy-layer-block` (`/tmp/xl-commit-shim-takeover` or `layershim.sh guard on`).

## Runtime Discovery (Architecture-Independent)

No CRTC, plane ID, or panel dimension constants are compiled into the shim:
- On the first live frame commit, the shim discovers the host's `primary_plane_id` and `crtc_id` from the host's own request items (with `drmModeGetResources` / `drmModeGetCrtc` fallback) and reads the panel resolution from the CRTC mode.
- Plane property IDs (`FB_ID`, `CRTC_ID`, `CRTC_X/Y/W/H`, `SRC_X/Y/W/H`, `type`) are resolved once by name via `drmModeObjectGetProperties` / `drmModeGetProperty` and cached in a fixed-size table (no heap allocations on subsequent commits).
- In **Page Guard** mode, the target overlay `plane_id` comes from `LayerBlock::plane_id` published by the GPU helper (or discovered spare overlay plane on Key 61).
- In **Stage 2 / Stage 3** test-rectangle mode, the shim discovers the first free overlay plane on the CRTC (`drmModeGetPlaneResources`, `fb_id == 0`, `crtc_id == 0`, `possible_crtcs` matching, `type == OVERLAY`) and sizes the test rectangle to the discovered panel.

## Page Guard Mode (`/tmp/xl-commit-shim-takeover` or `layershim.sh guard on`)

1. **Shared Block (`shell-mpc/overlay/gpu/layer_block.h`)**:
   - File-backed mmap at `/tmp/synthyboy-layer-block` (overridable via `SYNTHYBOY_LAYER_BLOCK_PATH` for tests).
   - Protocol version 3: lock-free seqlock (`seq` odd while writing, even when complete) with `page_id`, `expected_marker_l`, `expected_marker_r`, `rotation`, `guard_plane_off`, `hook_heartbeat_ns` (`CLOCK_MONOTONIC`), and `hook_active`.
2. **Helper Side (`gpu-helper/main.cpp`)**:
   - Helper presents its rendered frames directly via `SETPLANE` (`planeSet`) as fast as possible (no 20 ms claim/publish wait).
   - Publishes active page/view ID, expected marker colors (left & right from layout table), and panel rotation into `LayerBlock` immediately before `SETPLANE` to eliminate race windows where host commits could see new host markers with stale page ID (causing spurious blinks).
   - When the shim turns the plane off due to a host page flip (`guard_plane_off == 1`), the helper detects this, clears `guard_plane_off = 0`, and stays hidden until the plugin's page detector (made after the helper's `kMsgGuard` report) finds one of our views on the current host framebuffer (`kMsgPageOk`; the plugin runs that detection at once on receiving the report, at most once per 16 ms; message set = protocol version 9); only then it renders and presents via `SETPLANE` (`gpu/guard_state.h`).
3. **Shim Side (`tools/xl-commit-shim/xl-commit-shim.c`)**:
   - On each live host frame commit (`CALL_SITE_FRAME`, not `TEST_ONLY`): updates `hook_heartbeat_ns` and `hook_active = 1`.
   - **Host Dumb Buffer Mapping & Cache (`HostFbCache`)**:
     - Extracts primary plane FB ID from host commit items.
     - Calls `drmModeGetFB2` to retrieve GEM handle and buffer geometry.
     - Maps buffer memory once via `DRM_IOCTL_MODE_MAP_DUMB` and caches mapping per FB ID (host alternates between 2–3 framebuffers).
     - Intercepts `drmModeRmFB` to evict and unmap freed framebuffers cleanly.
   - **Marker Check at Commit Time**:
     - Reads the two marker centers for the published page from the generated skin layout and rotates them to panel orientation with `ui_to_panel`.
     - Compares pixel colors (XR24 format, RGB tolerance <= 2 per channel) against `expected_marker_l` and `expected_marker_r`.
   - **Atomic Plane-Off on Mismatch or Unknown Page**:
     - If markers mismatch the helper's published page, or do not match any known SynthyBoy page: shim injects `FB_ID = 0, CRTC_ID = 0` for our overlay plane into THAT host commit. The plane turns off atomically with the host flip. Sets `guard_plane_off = 1` in `LayerBlock`.
     - If markers match the published page: host commit passes through untouched.
     - **NONBLOCK frame commits**: Injected with plane-off as well, because turning off a plane presents no buffer and requires no sequence synchronization, ensuring zero stale overlay frames on all flip paths.
   - **Fail-Safe Fallback**:
     - Any mapping failure, invalid property, or IOCTL error passes the host commit through untouched (incrementing `guard_map_errors`) and never fails the host's commit.
   - **Statistics in 5-second summary line**:
     - `guard_checked`, `guard_match`, `guard_mismatch`, `guard_unknown`, `guard_map_errors`, `plane_off_added`.

## Building

Cross-compile using the Debian 12 Docker build container (`debian:12-slim` by default):

```bash
tools/xl-commit-shim/build.sh aarch64   # -> tools/xl-commit-shim/xl-commit-shim-aarch64.so (XL)
tools/xl-commit-shim/build.sh armv7     # -> tools/xl-commit-shim/xl-commit-shim-armv7.so (Key 61)
tools/xl-commit-shim/build.sh all       # -> both architectures
```

Verify binary architecture:
```bash
file tools/xl-commit-shim/xl-commit-shim-aarch64.so tools/xl-commit-shim/xl-commit-shim-armv7.so
```

## Running Host Unit Tests

Compile and run natively on macOS/Linux:

```bash
# 1. Shim host tests (fake libdrm + page guard checks):
clang -O2 -Wall -Wextra -Werror -pthread -DSHIM_TEST tools/xl-commit-shim/xl-commit-shim.c tools/xl-commit-shim/test_shim_host.c -o tools/xl-commit-shim/test_shim_host
tools/xl-commit-shim/test_shim_host

# 2. LayerBlock multi-threaded stress test (1e6 publishes) + ThreadSanitizer:
clang++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=thread -pthread shell-mpc/overlay/tests/layer_block_host.cpp -o /tmp/layer_block_host_tsan
/tmp/layer_block_host_tsan
```

## Deployment Script (`layershim.sh`)

Deploy and control on MPC XL (`xl`, `root@192.168.0.126`) or MPC Key 61 (`k61`, `root@192.168.0.30`):

```bash
# Dry run:
tools/xl-commit-shim/layershim.sh --dry-run xl start
tools/xl-commit-shim/layershim.sh --dry-run k61 start

# Install & restart acvs (combines LD_PRELOAD with hwctl.conf on k61 if active):
tools/xl-commit-shim/layershim.sh <xl|k61> start

# Check status, enable/disable page guard, view log, recover:
tools/xl-commit-shim/layershim.sh <xl|k61> status
tools/xl-commit-shim/layershim.sh <xl|k61> guard on    # or 'takeover on'
tools/xl-commit-shim/layershim.sh <xl|k61> guard off   # or 'takeover off'
tools/xl-commit-shim/layershim.sh <xl|k61> stage3 on
tools/xl-commit-shim/layershim.sh <xl|k61> stage3 off
tools/xl-commit-shim/layershim.sh <xl|k61> log 50
tools/xl-commit-shim/layershim.sh <xl|k61> recover
tools/xl-commit-shim/layershim.sh <xl|k61> stop
```
