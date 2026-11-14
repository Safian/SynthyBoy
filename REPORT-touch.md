# Touch-Screen Input Module Report (`REPORT-touch.md`)

## 1. What Was Done
Created the touch-screen input subsystem for the MPC overlay in `shell-mpc/overlay/`:

- **`shell-mpc/overlay/touch_decoder.h`**:
  - Pure C++17 header with no OS headers.
  - Decodes raw Linux evdev Multitouch Protocol B events (`type`, `code`, `value`, `timeUs`).
  - Tracks slots (up to 32 slots supported) and adheres to single-finger tracking policy: tracks only the first finger that touches down while no finger is tracked; ignores all other fingers until the tracked finger lifts.
  - Maps panel coordinates (`sx`, `sy`) to upright UI pixels for rotations 0, 90, 180, and 270 degrees according to panel dimensions (`panelW`, `panelH`) derived from device ABS ranges. On MPC Key 61 (800 x 1280 panel, rotation 90), maps to 1280 x 800 upright UI pixels.
  - Coordinate clamping ensures out-of-range touch coordinates are clamped to panel boundaries before rotation mapping.
  - Handles `SYN_DROPPED` (code 3): emits an `Up` event if a finger was tracked, resets all slot state, and resynchronises from the next complete frame.
  - Handles tracking ID replacement without an `Up`: synthesizes an `Up` for the previous tracking ID and emits `Down` for the replacement tracking ID.
  - Fixed-size ring buffer for events: zero heap allocation, no locks, no exceptions (`noexcept`).

- **`shell-mpc/overlay/touch_linux.h`**:
  - Linux evdev touch reader module.
  - Abstracted syscall interface `ITouchSyscalls` (`open`, `close`, `read`, `ioctl`, `poll`, `nowMs`) allowing full host testing on macOS without target hardware or kernel evdev drivers.
  - Real implementation `LinuxTouchSyscalls` under `#if defined(__linux__)`.
  - Discovers touch screens by scanning `/dev/input/event0..31`, selecting nodes with `EV_ABS`, `ABS_MT_POSITION_X`, `ABS_MT_POSITION_Y`, and `INPUT_PROP_DIRECT` property using `EVIOCGBIT` and `EVIOCGPROP`.
  - Reads device ranges with `EVIOCGABS` and configures the decoder.
  - Opens nodes strictly with `O_RDONLY | O_NONBLOCK | O_CLOEXEC`; never writes to device nodes and never issues `EVIOCGRAB`.
  - `poll(timeoutMs)` drains pending `struct input_event` records into a partial-read buffer and feeds the decoder; handles `EAGAIN`/`EWOULDBLOCK` gracefully.
  - Handles `ENODEV` and read errors by closing the descriptor and enforcing at least a 1000 ms back-off before attempting re-discovery.
  - Provides `fd()` accessor for multiplexing with other descriptors.

- **`shell-mpc/overlay/tests/touch_host.cpp`**:
  - Standalone host test runner matching the `*_host.cpp` style in the repository.
  - Verifies:
    1. Tap (Down then Up at same position).
    2. Drag (Down, Move, Move, Up, ignoring redundant moves).
    3. Two fingers (second ignored; first lifts while second stays; second stays ignored; then a new first finger touches down).
    4. All 4 rotations (0, 90, 180, 270) with all corner points and center points.
    5. `SYN_DROPPED` handling (emits Up, resets state, resynchronises next frame).
    6. Tracking ID replacement without Up.
    7. Out-of-range coordinate clamping.
    8. Microsecond timestamps fidelity.
    9. Linux reader discovery picking the right node among non-matching devices.
    10. `EAGAIN` non-blocking drain.
    11. Device removal (`ENODEV`), 1-second backoff period, and automatic reconnection.
    12. Partial `struct input_event` record assembly across multiple reads.

- **`shell-mpc/overlay/tests/run_touch_tests.sh`**:
  - Shell runner compiling and executing `touch_host.cpp` with `-std=c++17 -Wall -Wextra -Werror`.
  - Recompiles and executes under `-fsanitize=address,undefined` to verify memory safety and undefined-behavior freedom.

## 2. What Was Not Done / Remaining
- None. All deliverables are complete and verified. No files outside the 4 deliverables and report were modified.

## 3. How It Was Tested
- Executed `bash shell-mpc/overlay/tests/run_touch_tests.sh` in the worktree `/Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-touch-input`.
- 100% test passes under standard `-Wall -Wextra -Werror` build and under `-fsanitize=address,undefined`.
