# MPC XL Atomic Commit Measurement Shim (`xl-commit-shim`)

Copyright (C) 2026 Safian Szabolcs  
SPDX-License-Identifier: AGPL-3.0-or-later

## Background

A read-only survey of the MPC XL host application (`/usr/bin/MPC`, AArch64 PIE, statically linked JUCE, software CPU renderer, no EGL/GBM) established that:
- The host sets `DRM_CLIENT_CAP_ATOMIC` and acts as the DRM master on `/dev/dri/card0` (one fd).
- The host calls `drmModeAtomicCommit` via the libdrm PLT from exactly two call sites:
  1. **Call site (a) - Frame Present**: request with ~13 properties (`FB_ID`, `CRTC_ID`, `CRTC_X/Y/W/H`, `SRC_X/Y/W/H`, `FB_DAMAGE_CLIPS` blob) built with `drmModeAtomicAlloc` / `drmModeAtomicAddProperty`, flags `DRM_MODE_PAGE_FLIP_EVENT` (0x1).
  2. **Call site (b) - Cursor Move**: flags `DRM_MODE_ATOMIC_NONBLOCK` (0x200), `user_data == NULL`.
- Commits originate from one of seven `JUCE Message Th` threads.
- Hardware configuration: CRTC 82 (`video_port3`, DSI-1, 800x1280@60), primary plane 34 (XR24 800x1280 dumb buffers fb 92/93), cursor plane 40, free overlay plane 46 (`Cluster2-win0`).

## Purpose

To investigate whether our plane 46 can be added directly into the host's atomic commit cycle safely and cleanly, starting with a non-intrusive Stage 1 measurement shim, followed by an opt-in Stage 2 `DRM_MODE_ATOMIC_TEST_ONLY` probe, and finally Stage 3 live atomic commit merging with test buffer rendering.

## Architecture

### Stage 1: Passive Measurement (Zero Interference)
- Wraps `drmModeAtomicAlloc`, `drmModeAtomicFree`, `drmModeAtomicAddProperty`, `drmModeAtomicGetCursor`, and `drmModeAtomicCommit` via `dlsym(RTLD_NEXT, ...)`.
- **Mutex-free Thread-Local Storage**: Each host thread tracks in-flight request pointers in a small thread-local table (`tl_requests`), recording object ID, property ID, and value without contention.
- **Lock-free Ring Buffer**: Fast, bounded, lock-free ring buffer (2048 slots) for commit records. The calling host threads perform zero memory allocation, zero disk I/O, and zero printf calls.
- **Background Logger Thread**:
  - Low priority (`SCHED_OTHER`, nice 10).
  - Started lazily upon the very first host commit.
  - Flushes ring records to `/tmp/xl-commit-shim.log` once per second.
  - Generates a summary line every 5 seconds (commits/sec per call site, seen thread IDs with frequencies, min/median/p99/max durations, return code histogram).
  - Strictly caps `/tmp/xl-commit-shim.log` at 4 MB, writes a final termination line, and stops writing to avoid filling the device's volatile storage.

### Stage 2: `TEST_ONLY` Probe (Opt-in via `/tmp/xl-commit-shim-stage2`)
- The logger thread samples `/tmp/xl-commit-shim-stage2` at most once per second and updates an atomic flag.
- When enabled, right after the real frame-present commit (call site a) completes:
  - Builds a fresh request (`drmModeAtomicAlloc`).
  - Copies the host's properties into the new request.
  - Resolves property IDs for plane 46 (and logs plane 46 & 34 property sets once).
  - Creates a 400x300 dumb buffer lazily via `drmIoctl(DRM_IOCTL_MODE_CREATE_DUMB)` and imports it with `drmModeAddFB2`.
  - Appends 10 plane-46 properties: `FB_ID`, `CRTC_ID=82`, `CRTC_X=200`, `CRTC_Y=200`, `CRTC_W=400`, `CRTC_H=300`, `SRC_X=0`, `SRC_Y=0`, `SRC_W=400<<16`, `SRC_H=300<<16`.
  - Executes `drmModeAtomicCommit(fd, req2, DRM_MODE_ATOMIC_TEST_ONLY, NULL)` at most **once per second**.
  - **Never** uses `DRM_MODE_PAGE_FLIP_EVENT` with `TEST_ONLY`.
  - **Never** touches or alters the host's live request.
  - Frees the request immediately and logs the return value (`errno`) and call duration.

### Stage 3: Live Atomic Request Merge (Opt-in via `/tmp/xl-commit-shim-stage3`)
- Enabled by the file `/tmp/xl-commit-shim-stage3` (sampled once per second by the logger thread).
- **Single atomic commit per frame**: For every frame commit (call site a), the shim builds a fresh merged atomic request combining the host's recorded properties + 10 plane-46 properties:
  - `FB_ID = <test_fb_id>` (400x300 dumb buffer)
  - `CRTC_ID = 82`
  - `CRTC_X = 200`, `CRTC_Y = 200`
  - `CRTC_W = 400`, `CRTC_H = 300`
  - `SRC_X = 0`, `SRC_Y = 0`
  - `SRC_W = 400 << 16`, `SRC_H = 300 << 16`
  - Plane `zpos`, `alpha`, and `pixel blend mode` remain at hardware defaults.
- Commits this merged request with the host's own flags and `user_data` in place of the original request.
- **Fail-safe Fallback**:
  - If the merged commit returns an error, the shim IMMEDIATELY commits the host's ORIGINAL request (same flags/user_data).
  - Disables Stage 3 atomically (logged once with the errno and removes `/tmp/xl-commit-shim-stage3`).
  - Returns the original commit's return value and errno to the host so the host never encounters a failure caused by the shim.
- **Cursor Commits Untouched**: Call site (b) non-blocking cursor commits pass through completely unmerged.
- **Test Buffer & Visible Pattern**:
  - 400x300 XR24 dumb buffer mapped via `DRM_IOCTL_MODE_MAP_DUMB` and filled once at creation on the host thread:
    - 16 px border in solid white (`0x00FFFFFF`).
    - Mid-red interior (`0x00C03030`).
    - Diagonal white stripe from top-left to bottom-right so panel orientation is immediately visible.
  - Unmapped immediately after filling (`munmap`).
- **Clean Switching Off & Buffer Lifecycle**:
  - When `/tmp/xl-commit-shim-stage3` disappears, the NEXT frame commit disables plane 46 in the merged request (`FB_ID=0`, `CRTC_ID=0`).
  - Only after that disable commit returns 0 are the test framebuffer (`drmModeRmFB`) and dumb buffer (`DRM_IOCTL_MODE_DESTROY_DUMB`) destroyed.
  - Shared owner with use count: when both Stage 2 and Stage 3 are off, the test buffer is completely freed on the next frame commit.
- **Screen & Hardware Verification**:
  - **On-Screen**: Look for a white-bordered red 400x300 rectangle with a white diagonal stripe at panel coordinates (200, 200).
  - **DRM debugfs**: Inspect `/sys/kernel/debug/dri/0/state` to verify plane 46 is active on CRTC 82 with its FB attached:
    ```bash
    cat /sys/kernel/debug/dri/0/state | grep -A 10 "plane\[46\]"
    ```
- **Logging & Summary Metrics**:
  - Full property item list logged on any frame commit whose item count differs from the previous frame commit (e.g., transition from 15 to 23 items).
  - Every 5-second summary window reports:
    - Merged commits per second (`rate(merged)=...`).
    - Non-merged commit durations (`dur_ns min=... med=... p99=... max=...`).
    - Merged commit durations (`merged_dur_ns min=... med=... p99=... max=...`).
    - Fallback count (`fallbacks=...`).

## Building

Cross-compile for AArch64 using the repo's Debian 12 Docker build image (`kick-armv7-builder:latest`):

```bash
bash tools/xl-commit-shim/build.sh
```

Verifying binary architecture:
```bash
file tools/xl-commit-shim/xl-commit-shim.so
# Output: ELF 64-bit LSB shared object, ARM aarch64, version 1 (SYSV), dynamically linked, ...
```

## Running Host Unit Tests

A comprehensive host unit test suite (`test_shim_host.c`) compiles and runs natively on macOS (with direct link and fake libdrm mocks):

```bash
clang -O2 -Wall -Wextra -pthread -DSHIM_TEST tools/xl-commit-shim/xl-commit-shim.c tools/xl-commit-shim/test_shim_host.c -o tools/xl-commit-shim/test_shim_host
tools/xl-commit-shim/test_shim_host
```

Tests verify:
- Argument and return value pass-through for real commits.
- Item tracking of 13 properties.
- Ring record fields (time, tid, flags, item counts, userdata, ret, duration).
- First 8 commits contain items; subsequent commits contain item counts only.
- Stage 2 disabled: exactly one commit performed.
- Stage 2 enabled: exactly one extra `TEST_ONLY` commit per second, containing 23 properties (13 host + 10 plane 46), host request untouched, no `PAGE_FLIP_EVENT` on `TEST_ONLY`.
- Cursor call site (b) does not trigger Stage 2 or Stage 3 merging.
- Error forwarding and errno preservation.
- Summary line generation (including `rate(merged)`, `merged_dur_ns`, and `fallbacks`).
- 4 MB log cap enforcement.
- Thread startup and clean join.
- Stage 3 on: 15-item host frame commit results in exactly ONE commit with 15 + 10 = 25 items, flags and user_data identical, host request cursor untouched (15), plane 46 FB_ID matches test FB, return value passes through.
- Stage 3 visible pattern fill: 16 px white border, mid-red interior (`0x00C03030`), diagonal white stripe.
- Stage 3 failing merged commit: fake libdrm returns error, immediate fallback commit with original 15 items, stage 3 disabled atomically, host receives fallback result.
- Stage 3 cursor commits: pass through unmerged.
- Stage 3 off: one frame commit disabling plane 46 (`FB_ID=0`, `CRTC_ID=0`), then `drmModeRmFB` and `DRM_IOCTL_MODE_DESTROY_DUMB` called.
- Stage 3 safety: requests with 70 items (> 64 limit) or incomplete property sets are not merged.
- Item count change logging: full property list is logged when host frame item count changes from 15 to 23.
- Stage 2 off buffer cleanup: dumb buffer and FB are freed when both stage 2 and stage 3 are off.

## Deployment Script (`xlshim.sh`)

Deploy and manage the shim on the MPC XL (`root@192.168.0.126`):

```bash
# Print remote commands without connecting:
tools/xl-commit-shim/xlshim.sh --dry-run start
tools/xl-commit-shim/xlshim.sh --dry-run status
tools/xl-commit-shim/xlshim.sh --dry-run stage2 on
tools/xl-commit-shim/xlshim.sh --dry-run stage2 off
tools/xl-commit-shim/xlshim.sh --dry-run stage3 on
tools/xl-commit-shim/xlshim.sh --dry-run stage3 off
tools/xl-commit-shim/xlshim.sh --dry-run recover
tools/xl-commit-shim/xlshim.sh --dry-run stop

# Install and restart MPC with shim preloaded:
tools/xl-commit-shim/xlshim.sh start

# Check status (PID, maps, stage flags, last summary, log tail):
tools/xl-commit-shim/xlshim.sh status

# Enable/disable Stage 2:
tools/xl-commit-shim/xlshim.sh stage2 on
tools/xl-commit-shim/xlshim.sh stage2 off

# Enable/disable Stage 3 (live merged plane 46 with visible pattern):
tools/xl-commit-shim/xlshim.sh stage3 on
tools/xl-commit-shim/xlshim.sh stage3 off

# View logs:
tools/xl-commit-shim/xlshim.sh log 50

# One-command safe recovery (removes stage 2/3 flags, removes drop-in, restarts MPC normally):
tools/xl-commit-shim/xlshim.sh recover

# Normal restore (removes runtime drop-in and restarts MPC):
tools/xl-commit-shim/xlshim.sh stop
```
