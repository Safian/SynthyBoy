# Developer log (`shell-mpc/kit/log.h`, namespace `mpckit`)

A header-only, C++17, POSIX-only log for plugins that run inside a host you cannot attach a debugger to (the Akai MPC application). No dependency on SynthyBoy headers.

## How to log

```cpp
#include "kit/log.h"

MPCKIT_LOG_START ("myplugin");                       // once, from a thread that may allocate (the plugin's constructor)
MPCKIT_LOG ("startup", info, "engine built in %.1f ms", ms);   // any thread, the audio thread included
MPCKIT_SCOPE ("startup", "engine construct");        // logs "engine construct: 12.34 ms" when the scope ends (level info)
```

Levels: `error warn info debug trace`. A category is any string; use a few stable ones (`startup`, `q`, `overlay`, `audio`).
Each line in the file: `<ms since start> <INF|WRN|...> <thread tag> <category> <text>`. Lines longer than 176 bytes are cut.
The audio thread may log: the call formats into a fixed record and publishes it into a lock free ring (no allocation, no lock, no I/O); a full ring drops the record and counts it (the writer then writes `-- N records dropped --`). The thread tag is the low bits of `pthread_self ()` (no thread local storage).

## Switching it on, on the device

Dev builds start with every category OFF. Create `/tmp/<plugin>-log.conf` (for SynthyBoy `/tmp/synthyboy-log.conf`):

```
# category=level; all= is the default of every category without a line
all=info
startup=debug
q=trace
```

The writer thread reads the file at start and about once a second: edit, create or delete it while the host runs. The log goes to `/tmp/<plugin>-dev.log`, at most 2 MB, then it is renamed to `<file>.1` (the old `.1` is replaced): at most 4 MB on disk. The file is opened `O_NOFOLLOW | O_NONBLOCK` and used only when it is a regular file. Nothing is written, and no file exists, while no category is on.

SynthyBoy: `ssh` to the MPC, `echo all=info > /tmp/synthyboy-log.conf`, load the plugin, `cat /tmp/synthyboy-dev.log`. The `startup` category carries the same numbers as the `open_ms_*` lines of the status file (docs/MPC.md "Start-up time").

## How the release strips it

One CMake option, `SYNTHYBOY_MPC_DEV_LOG` (default ON), maps to the macro `MPCKIT_DEV_LOG`. `scripts/build-mpc.sh --release` passes `-DSYNTHYBOY_MPC_DEV_LOG=OFF`. With the macro 0 (also the default of the header when nothing defines it):

- `MPCKIT_LOG`, `MPCKIT_SCOPE`, `MPCKIT_LOG_START` expand to `do {} while (0)`; their arguments are not evaluated and not compiled;
- no log string, no thread, no file, no symbol of the header is in the binary.

The same option sets `SYNTHYBOY_MPC_WRITE_FILES` (`shell-mpc/write_files.h`): the report and probe files of the plugin and of the GPU helper are not written, the helper's output goes to `/dev/null`, and the periodic status file is written only while `/tmp/synthyboy-overlay-status-on` exists.

Proof line for the accept script (not run by the worker who wrote it), on the release binaries of `build-mpc.sh --release`:

```
for f in VST/synthyboy-arm32.so VST/synthyboy-arm64.so VST/synthyboy-overlay-gpu-*; do
  strings -a "$f" | grep -E -e "-dev\.log|-log\.conf|mpckit|engine created at|scan factory presets|VSTPluginMain returns" && { echo "ACCEPT FAIL: log strings in $f"; exit 1; }
done
```

(The patterns are the file name suffixes the log builds and the texts of the `startup` log calls. The status text `open_ms_*` and `gpu_helper_startup_ms` stay in the binary: the status file exists on request.)

## Reusing the header in another plugin

Copy `kit/log.h`. Compile with `-DMPCKIT_DEV_LOG=1` in dev builds, 0 or nothing in the release. Call `MPCKIT_LOG_START ("name")` once, then `MPCKIT_LOG`. Everything is in `mpckit::devlog` (`Logger`, `Settings`); a plugin with other paths or a size cap builds a `Settings` and calls `devlog::logger ().start (settings)`. Tests: `shell-mpc/tests/log_host.cpp` (switching, rotation, overflow, writer thread), `log_off_host.cpp` (compiled out: arguments not evaluated), `log_rt_host.cpp` (no `operator new` on the calling thread; `malloc` inside `vsnprintf` is not counted by that test).

## What the existing code logs, and what it became

| Where | What | Now |
|---|---|---|
| `shell-mpc/play_log.h` (debug hooks only) | ring of host call lines to `/tmp/synthyboy-play-log.txt`, chunk copies, instance dump | unchanged: the capture and acceptance scripts read these formats |
| `shell-mpc/callback_stats.h` (option, OFF) | audio callback statistics file | unchanged (own option) |
| overlay status file `/tmp/synthyboy-overlay-status.txt` | the status text | unchanged format; in a release build only while `/tmp/synthyboy-overlay-status-on` exists |
| GPU helper `fprintf (stderr ...)` (6 lines) | start / stop / error lines into `/tmp/synthyboy-overlay-gpu.log` | unchanged; release: output goes to `/dev/null` |
| `device_profile.h` report | `/tmp/synthyboy-mpc-profile.txt` once | unchanged; release: not written |
| `poll_guard.h` | failure report `/tmp/synthyboy-mpc-status.txt` | unchanged; release: kept in memory only |
| start-up timing | new | status lines `open_ms_*` and the `startup` category of this log |

There was no other plain `printf` / `std::cerr` logging in the plugin.
