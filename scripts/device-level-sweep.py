#!/usr/bin/env python3
"""device-level-sweep.py - Unattended overnight level sweep and verifier for Akai MPC devices.

Modes:
  run      - Run unattended level sweep on connected MPC device (k61 or xl).
  verify   - Offline verifier for sweep output directory.
  selftest - Run verifier against synthetic test suites (pass and fail cases).

Usage:
  python3 scripts/device-level-sweep.py run --device k61|xl [--limit N] [--resume] [--outdir DIR] [--dry-run]
  python3 scripts/device-level-sweep.py verify [DIR] [--csv FILE] [--full-target T] [--full-tol TOL] [--half-diff D] [--half-tol TOL]
  python3 scripts/device-level-sweep.py selftest

Copyright (C) 2026 Safian Szabolcs
SPDX-License-Identifier: AGPL-3.0-or-later
"""

import argparse
import csv
import datetime
import math
import json
from pathlib import Path
import os
import re
import shutil
import signal
import select
import subprocess
import sys
import tempfile
import threading
import time
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mpc_instance_snapshot import read_instance_snapshot

# Default targets per owner requirements
DEFAULT_FULL_TARGET = -10.0  # dBFS
DEFAULT_FULL_TOL = 2.0       # dB
DEFAULT_HALF_DIFF = 11.9     # dB (full_peak - half_peak)
DEFAULT_HALF_TOL = 3.0       # dB
PRESET_COUNT = 160           # Factory presets only
FIRST_INDEX = 1              # Program index 0 is the empty "Init" program: not swept
LAST_INDEX = FIRST_INDEX + PRESET_COUNT - 1   # 160 (program_list_length is 161)
# The offline level table (docs/release/OUTPUT-LEVEL-TABLE-2026-11-20.md) holds the note for 3 s; slow-attack pads and risers peak after seconds, so a 0.8 s hold
# measured them 10..23 dB too low (first full sweep 2026-11-20: 39 of 159 presets). Hold like the offline gesture and measure the whole note.
SWEEP_HOLD_S = 3.0
SWEEP_WINDOW_S = 4.5
SWEEP_NOTE = 48              # C3, the note of the offline level table (OUTPUT-LEVEL-TABLE)
QUIET_PEAK = 1e-3            # a status write below this (-60 dBFS) counts as silence

# Device configurations
DEVICES = {
    "k61": {
        "name": "Akai MPC Key 61",
        "ip": "192.168.0.30",
        "user": "root",
        "lock_dir": "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-tools/DEVICE-LOCK",
        "tools_dir": "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-tools",
        "hwctl": "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-tools/hwctl/hwctl.sh",
        "shot_sh": "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-tools/shot.sh",
        "tap_coords": (38, 698),  # green peak-hold readout under strip 1 (tap resets it to -INF)
        # Key 61 navigation: docs/MPC.md#key-61-track-mixer-route
    },
    "xl": {
        "name": "Akai MPC XL",
        "ip": "192.168.0.126",
        "user": "root",
        "lock_dir": "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-tools/DEVICE-LOCK-XL",
        "tools_dir": "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/xl-ov9",
        "xshot_sh": "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/xl-ov9/tools/xshot.sh",
        "tap_coords": (355, 698),  # UI Plugin 001 mixer page
        "touch_mirrored": (924, 101),  # (1279-355, 799-698) for mtouch on XL
    },
}

# Hook / status file paths on the device
DEV_SELECT_PROGRAM = "/tmp/synthyboy-select-program"
DEV_SELECT_ACK = "/tmp/synthyboy-select-program.ack"
DEV_STATUS = "/tmp/synthyboy-overlay-status.txt"
DEV_STATUS_ON = "/tmp/synthyboy-overlay-status-on"
DEV_PLAY_LOG = "/tmp/synthyboy-play-log"
DEV_PLAY_LOG_TXT = "/tmp/synthyboy-play-log.txt"
DEV_PLAY_LOG_DUMP = "/tmp/synthyboy-play-log-dump"
DEV_PLAY_LOG_DUMP_TXT = "/tmp/synthyboy-play-log-dump.txt"


def slugify(name: str) -> str:
    """Convert preset name to filesystem-safe slug using hyphens."""
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return s or "preset"


def linear_to_dbfs(val: float) -> float:
    """Convert linear amplitude to dBFS."""
    if val <= 1e-9:
        return -120.0
    return round(20.0 * math.log10(val), 2)


def parse_ack_line(line: str) -> Tuple[bool, Optional[int], Optional[int], str]:
    """Parse one line of /tmp/synthyboy-select-program.ack.

    Format:
      <counter> ok <index> <name>
      <counter> error <reason>

    Returns:
      (ok, counter, index, name_or_reason)
    """
    line = line.strip()
    if not line:
        return False, None, None, "empty ack line"
    parts = line.split(maxsplit=3)
    if len(parts) < 2:
        return False, None, None, f"invalid ack line: {line}"
    try:
        counter = int(parts[0])
    except ValueError:
        return False, None, None, f"invalid counter in ack: {line}"

    status = parts[1]
    if status == "ok":
        if len(parts) < 3:
            return False, counter, None, "missing index in ok ack"
        try:
            idx = int(parts[2])
        except ValueError:
            return False, counter, None, f"invalid index in ack: {parts[2]}"
        name = parts[3] if len(parts) > 3 else ""
        return True, counter, idx, name
    elif status == "error":
        reason = line.split(maxsplit=2)[2] if len(parts) > 2 else "unknown error"
        return False, counter, None, reason
    else:
        return False, counter, None, f"unexpected status: {status}"


def parse_status_output_peak(text: str) -> Optional[float]:
    """Extract output_peak from status file text.

    Explicitly ignores output_peak_total.
    Line format: output_peak: 0.12345
    """
    m = re.search(r"^output_peak:\s*([0-9.]+(?:[eE][-+]?[0-9]+)?)", text, re.MULTILINE)
    if m:
        try:
            return float(m.group(1))
        except ValueError:
            return None
    return None


def parse_play_log_velocities(text: str) -> List[int]:
    """Extract velocities from note_on events in play log text.

    Line format: <counter> <ms> note_on channel <ch> note <num> vel <host_velocity> ...
    """
    vels = []
    for line in text.splitlines():
        if "note_on" in line:
            m = re.search(r"\bvel\s+(\d+)\b", line)
            if m:
                vels.append(int(m.group(1)))
    return vels


def parse_instance_dump(text: str) -> Dict:
    """Parse instance dump text from /tmp/synthyboy-play-log-dump.txt.

    Extracts:
      - program: int (or None)
      - protected: int
      - window: int
      - params: Dict[str, float] (param key -> float value)
      - proxies: Dict[int, float]
    """
    res = {
        "program": None,
        "protected": 0,
        "window": 0,
        "params": {},
        "proxies": {},
    }
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        m_prog = re.match(r"^program\s+(\d+)(?:\s+protected\s+(\d+))?(?:\s+window\s+(\d+))?", line)
        if m_prog:
            res["program"] = int(m_prog.group(1))
            if m_prog.group(2) is not None:
                res["protected"] = int(m_prog.group(2))
            if m_prog.group(3) is not None:
                res["window"] = int(m_prog.group(3))
            continue
        m_param = re.match(r"^p\s+\d+\s+(\S+)\s+([0-9.eE+-]+)", line)
        if m_param:
            try:
                res["params"][m_param.group(1)] = float(m_param.group(2))
            except ValueError:
                pass
            continue
        m_proxy = re.match(r"^proxy\s+(\d+)\s+([0-9.eE+-]+)", line)
        if m_proxy:
            try:
                res["proxies"][int(m_proxy.group(1))] = float(m_proxy.group(2))
            except ValueError:
                pass
            continue
    return res


def load_expected_levels(table_path: Optional[str] = None) -> Dict[str, float]:
    """Factory v127 minus v64 level differences from the measured reference table."""
    if table_path is None:
        table_path = os.path.join(os.path.dirname(__file__), "..", "docs", "release",
                                  "OUTPUT-LEVEL-TABLE-2026-11-20.md")
    levels = {}
    with open(table_path, encoding="utf-8") as table:
        for line in table:
            cells = line.strip().strip("|").split("|")
            if len(cells) < 10:
                continue
            try:
                delta = float(cells[9])
            except ValueError:
                continue
            levels[slugify(cells[1])] = delta
    if not levels:
        raise ValueError(f"No factory level references in {table_path}")
    return levels


def parse_play_log_events(text: str, after_counter: int = 0) -> List[Tuple[int, int, int]]:
    """Return (counter, note, velocity) of note_on lines with counter > after_counter.

    The velocity is the engine velocity when the line has one, else the host velocity.
    Line format: <counter> <ms> note_on channel <ch> note <n> vel <v> engine <e> dropped <d>

    Handles counter restarts/resets (e.g. after MPC app restart where /tmp/synthyboy-play-log.txt
    contains leftover lines from previous runs with higher counters).
    """
    lines = text.splitlines()
    if not lines:
        return []

    # Parse all lines that start with an integer counter
    parsed_lines: List[Tuple[int, str]] = []
    for line in lines:
        m = re.match(r"^\s*(\d+)\b", line)
        if m:
            parsed_lines.append((int(m.group(1)), line))

    if not parsed_lines:
        return []

    # Detect counter restarts: where a counter drops below the previous counter
    # Split into session segments; the last segment is the active session
    last_reset_idx = 0
    for i in range(1, len(parsed_lines)):
        if parsed_lines[i][0] < parsed_lines[i - 1][0]:
            last_reset_idx = i

    active_lines = parsed_lines[last_reset_idx:]
    latest_counter = active_lines[-1][0] if active_lines else 0

    # If after_counter is greater than latest_counter in the active session, or if a reset
    # occurred between after_counter and active_lines, after_counter belongs to a dead session
    effective_after = 0 if after_counter > latest_counter else after_counter

    events = []
    for counter, line in active_lines:
        if "note_on" not in line:
            continue
        m = re.match(r"^\s*(\d+)\s+\d+\s+note_on\b.*?\bnote\s+(\d+)\b.*?\bvel\s+(\d+)\b(?:.*?\bengine\s+(\d+)\b)?", line)
        if not m:
            continue
        if counter <= effective_after:
            continue
        vel = int(m.group(4)) if m.group(4) is not None else int(m.group(3))
        events.append((counter, int(m.group(2)), vel))
    return events


def parse_sample_line(line: str) -> Optional[Tuple[int, float]]:
    """Parse one sampler line 'output_peak: <p> output_blocks: <n>' -> (blocks, peak)."""
    mp = re.search(r"output_peak:\s*([0-9.]+(?:[eE][-+]?[0-9]+)?)", line)
    mb = re.search(r"output_blocks:\s*(\d+)", line)
    if not mp or not mb:
        return None
    return int(mb.group(1)), float(mp.group(1))


def peak_in_window(samples: List[Tuple[float, int, float]], t_start: float, t_end: float) -> Tuple[float, int]:
    """Max output_peak over the status writes that were first seen inside [t_start, t_end].

    samples: (receipt_time, output_blocks, output_peak), in receipt order. output_peak is the peak since the
    previous status write and the write is identified by its output_blocks counter. A write first seen
    before t_start was written before the hit and is discarded. Returns (max_peak, number_of_writes_used).
    """
    first_seen: Dict[int, Tuple[float, float]] = {}
    for t, blocks, peak in samples:
        if blocks not in first_seen:
            first_seen[blocks] = (t, peak)
    used = [pk for (t, pk) in first_seen.values() if t_start <= t <= t_end]
    return (max(used) if used else 0.0), len(used)


def quiet_writes_seen(samples: List[Tuple[float, int, float]], need: int = 2) -> bool:
    """True when the last `need` distinct status writes are all below QUIET_PEAK."""
    seen: Dict[int, float] = {}
    order: List[int] = []
    for _, blocks, peak in samples:
        if blocks not in seen:
            order.append(blocks)
        seen[blocks] = peak
    if len(order) < need:
        return False
    return all(seen[b] < QUIET_PEAK for b in order[-need:])


def hit_reached_plugin(events: List[Tuple[int, int, int]], velocity: int) -> bool:
    """True when the play log shows exactly one new note_on, SWEEP_NOTE at `velocity`."""
    return len(events) == 1 and events[0][1] == SWEEP_NOTE and events[0][2] == velocity


def restoration_evidence(log_path: str) -> str:
    """Summarize only the most recent acquisition in an append-only sweep log."""
    result = "Unverified (no completed parameter comparison)."
    if not os.path.isfile(log_path):
        return result
    expected_index = None
    acknowledged = False
    remaining = 0
    changes = []
    with open(log_path, encoding="utf-8") as log:
        for line in log:
            message = line.partition("] ")[2].strip()
            if message.startswith("Starting unattended level sweep on "):
                expected_index = None
                acknowledged = False
                remaining = 0
                changes.clear()
                result = "Unverified (no completed parameter comparison)."
            elif message.startswith("Sweep restoration failed:"):
                return_reason = message.partition(":")[2].strip()
                result = f"Failed: {return_reason}"
                acknowledged = False
            elif match := re.fullmatch(r"--- Restoring initial preset (\d+) \(.*\) via hook ---", message):
                expected_index = match[1]
                acknowledged = False
                remaining = 0
                changes.clear()
                result = "Unverified (restore was attempted)."
            elif match := re.fullmatch(r"Successfully restored device to initial preset (\d+) \(.*\)", message):
                acknowledged = match[1] == expected_index
            elif acknowledged and message == "Parameter status: clean (all parameters match initial state)":
                result = f"Clean: preset {expected_index}, all captured parameters match."
            elif acknowledged and (match := re.fullmatch(r"Changed parameters after restore \((\d+)\):", message)):
                remaining = int(match[1])
                changes.clear()
                result = "Unverified (incomplete parameter differences)."
            elif acknowledged and remaining and re.fullmatch(r"[^:]+: initial=[^,]+, current=.+", message):
                remaining -= 1
                changes.append(message)
                if remaining == 0:
                    result = f"Changed: preset {expected_index}\n\n" + "\n".join("- " + change for change in changes)
    return result


def get_factory_programs(presets_dir: Optional[str] = None) -> List[str]:
    """Get the program names in host program order: index 0 = Init, then the 160 factory presets (1..160).

    Program 0 is always 'Init'. Presets follow in sortPrograms order:
    category (plain <), then case-insensitive name, then path.
    """
    if presets_dir is None:
        # Determine relative to script location
        here = os.path.dirname(os.path.abspath(__file__))
        cand = os.path.join(here, "..", "content", "presets")
        if os.path.isdir(cand):
            presets_dir = os.path.abspath(cand)

    programs = ["Init"]
    if presets_dir and os.path.isdir(presets_dir):
        import json
        entries = []
        for root, _, files in os.walk(presets_dir):
            for f in files:
                if f.endswith(".sbpreset"):
                    full = os.path.join(root, f)
                    rel = os.path.relpath(full, presets_dir).replace(os.sep, "/")
                    name = f[:-9]
                    cat = rel.split("/")[0]
                    try:
                        with open(full, "r", encoding="utf-8") as fh:
                            data = json.load(fh)
                            meta = data.get("meta", {})
                            name = meta.get("name", name)
                            cat = meta.get("category", cat)
                    except Exception:
                        pass
                    entries.append((cat, name.lower(), rel, name))
        entries.sort(key=lambda e: (e[0], e[1], e[2]))
        programs.extend([e[3] for e in entries])

    # If presets could not be read or are fewer than 160, fill with sensible fallback names
    while len(programs) < PRESET_COUNT + 1:
        programs.append(f"Preset {len(programs)}")

    return programs[:PRESET_COUNT + 1]


class DeviceLock:
    """Lock directory manager for hardware MPC devices."""

    def __init__(self, lock_dir: str, dry_run: bool = False, logger=None):
        self.lock_dir = lock_dir
        self.dry_run = dry_run
        self.logger = logger or print
        self.acquired = False

    def acquire(self):
        if self.dry_run:
            self.logger(f"[dry-run] mkdir {self.lock_dir}")
            self.acquired = True
            return
        try:
            os.mkdir(self.lock_dir)
            self.acquired = True
            self.logger(f"Acquired device lock {self.lock_dir}")
        except FileExistsError:
            raise RuntimeError(
                f"Device lock {self.lock_dir} already exists! "
                "Another sweep or session is currently using the device. Aborting."
            )

    def release(self):
        if not self.acquired:
            return
        if self.dry_run:
            self.logger(f"[dry-run] rmdir {self.lock_dir}")
            self.acquired = False
            return
        try:
            if os.path.exists(self.lock_dir):
                os.rmdir(self.lock_dir)
                self.logger(f"Released device lock {self.lock_dir}")
        except Exception as e:
            self.logger(f"Warning: Failed to remove device lock {self.lock_dir}: {e}")
        finally:
            self.acquired = False

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.release()


class DeviceExecutor:
    """Manages commands and device interaction over SSH."""

    def __init__(self, device_key: str, outdir: str, dry_run: bool = False, logger=None):
        self.device_key = device_key
        self.cfg = DEVICES[device_key]
        self.outdir = outdir
        self.dry_run = dry_run
        self.logger = logger or print

    def ssh(self, cmd: str, timeout: int = 10) -> Tuple[int, str, str]:
        """Execute a remote shell command via ssh."""
        if self.dry_run:
            self.logger(f"[dry-run] ssh {self.cfg['user']}@{self.cfg['ip']} '{cmd}'")
            return 0, "", ""
        ssh_cmd = [
            "ssh",
            "-o", "BatchMode=yes",
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "LogLevel=ERROR",
            "-o", f"ConnectTimeout={timeout}",
            f"{self.cfg['user']}@{self.cfg['ip']}",
            cmd,
        ]
        try:
            res = subprocess.run(ssh_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout + 5)
            return res.returncode, res.stdout, res.stderr
        except subprocess.TimeoutExpired:
            return 124, "", "ssh timed out"
        except Exception as e:
            return 1, "", str(e)

    def local(self, cmd_args: List[str], env: Optional[Dict[str, str]] = None, timeout: int = 20) -> Tuple[int, str, str]:
        """Execute a local shell command."""
        cmd_str = " ".join(cmd_args)
        if self.dry_run:
            env_str = f"MPC_SHOT_DIR={env['MPC_SHOT_DIR']} " if env and "MPC_SHOT_DIR" in env else ""
            self.logger(f"[dry-run] {env_str}{cmd_str}")
            return 0, "", ""
        cur_env = os.environ.copy()
        if env:
            cur_env.update(env)
        try:
            res = subprocess.run(cmd_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=cur_env, timeout=timeout)
            return res.returncode, res.stdout, res.stderr
        except subprocess.TimeoutExpired:
            return 124, "", "command timed out"
        except Exception as e:
            return 1, "", str(e)

    def preflight_check(self, expected_prog0_name: str = "Init") -> Tuple[bool, str]:
        """Verify device responsiveness, debug hook ack, and take preflight picture."""
        self.logger("Running preflight checks...")

        # 1. SSH responsiveness & ensure switch files
        code, out, err = self.ssh(f"touch {DEV_STATUS_ON} {DEV_PLAY_LOG} && echo ok")
        if code != 0 and not self.dry_run:
            return False, f"SSH connection failed to {self.cfg['ip']}: {err.strip()}"

        # 2. Check hook ack with program 0
        self.ssh(f"rm -f {DEV_SELECT_PROGRAM} {DEV_SELECT_ACK}")
        self.ssh(f"printf '0\\n' > {DEV_SELECT_PROGRAM}")

        if not self.dry_run:
            # Poll for ack up to 5s
            ack_ok = False
            ack_text = ""
            for _ in range(25):
                time.sleep(0.2)
                c, aout, _ = self.ssh(f"cat {DEV_SELECT_ACK} 2>/dev/null")
                if c == 0 and aout.strip():
                    ack_ok = True
                    ack_text = aout.strip()
                    break
            if not ack_ok:
                return False, f"Preflight select program 0 failed: no ack received from {DEV_SELECT_ACK}"

            ok, seq, idx, name = parse_ack_line(ack_text)
            if not ok or idx != 0:
                return False, f"Preflight select program 0 returned error ack: {ack_text}"
            if name != expected_prog0_name:
                self.logger(f"Preflight note: expected program 0 name '{expected_prog0_name}', ack returned '{name}'")
        else:
            self.logger(f"[dry-run] poll {DEV_SELECT_ACK} for up to 5.0 s (expect ok 0 {expected_prog0_name})")

        # 3. Take preflight picture
        preflight_img = os.path.join(self.outdir, "preflight.png")
        ok_shot = self.capture_screenshot("preflight", target_file=preflight_img)
        if not ok_shot and not self.dry_run:
            return False, "Failed to capture preflight screenshot of host track strip"

        self.logger(f"Preflight check PASSED. Host track strip screenshot saved to {preflight_img}")
        return True, "Preflight passed"

    def select_program(self, index: int, fallback_name: str = "", timeout_s: float = 5.0) -> Tuple[bool, str]:
        """Select program by index via remote debug hook and wait for ack."""
        self.ssh(f"rm -f {DEV_SELECT_PROGRAM} {DEV_SELECT_ACK}")
        self.ssh(f"printf '{index}\\n' > {DEV_SELECT_PROGRAM}")

        if self.dry_run:
            name = fallback_name or f"Preset {index}"
            self.logger(f"[dry-run] wait for {DEV_SELECT_ACK} up to {timeout_s}s and 1.5s settle (expect ok {index} {name})")
            return True, name

        # Poll for ack
        start_t = time.time()
        ack_text = ""
        while time.time() - start_t < timeout_s:
            time.sleep(0.2)
            c, aout, _ = self.ssh(f"cat {DEV_SELECT_ACK} 2>/dev/null")
            if c == 0 and aout.strip():
                ack_text = aout.strip()
                break

        if not ack_text:
            return False, f"Timeout waiting for ack on preset {index}"

        ok, seq, idx, name = parse_ack_line(ack_text)
        if not ok:
            return False, f"Error ack on preset {index}: {name}"
        if idx != index:
            return False, f"Ack index mismatch: expected {index}, got {idx} ({name})"

        # Settle
        time.sleep(1.5)
        return True, name

    def tap_reset_peak_hold(self):
        """Send a single touch tap to reset the mixer meter peak hold number."""
        if self.device_key == "k61":
            x, y = self.cfg["tap_coords"]
            self.ssh(f"/tmp/mtouch tap {x} {y} 90")
        elif self.device_key == "xl":
            mx, my = self.cfg["touch_mirrored"]
            self.ssh(f"/tmp/mtouch tap {mx} {my} 90")
        if not self.dry_run:
            time.sleep(0.2)

    def send_hit(self, velocity: int, hold_s: float = 0.8):
        """Play SWEEP_NOTE (C3 = 48, the note of the offline level table) at `velocity` for `hold_s`.

        Both devices: seqnote writes the note straight into the MPC's ALSA sequencer input (client 129 port 0),
        which the host routes to the selected plugin track. The Key 61 pads cannot be used: every pad / pad
        note of the host's Pad Perform maps to MIDI 72..98 (pad 0x25 plays 72 = C5), never to 48.
        """
        if self.device_key == "xl":
            self.logger("  (XL: using seqnote 129 0 - MIDI input path, not the pads: XL has no pad injection)")
        cmd = f"SEQNOTE_VEL={velocity} /tmp/seqnote 129 0 {hold_s} {SWEEP_NOTE}"
        self.ssh(cmd, timeout=int(hold_s) + 10)

    def play_log_offset(self) -> int:
        code, out, _ = self.ssh(f"if [ -f {DEV_PLAY_LOG_TXT} ]; then wc -c < {DEV_PLAY_LOG_TXT}; else echo 0; fi")
        if code or not out.strip().isdigit():
            raise RuntimeError("Cannot determine play-log position before hit")
        return int(out.strip())

    def play_log_events(self, offset: int) -> List[Tuple[int, int, int]]:
        code, out, _ = self.ssh(f"tail -c +{offset + 1} {DEV_PLAY_LOG_TXT} 2>/dev/null")
        return parse_play_log_events(out) if code == 0 else []

    def hit_and_measure(self, velocity: int, hold_s: float = 0.8, window_s: float = 3.0,
                        preroll_max_s: float = 8.0) -> Tuple[float, List[Tuple[int, int, int]]]:
        """Play one hit and return (peak_linear, note_on_events) of exactly that hit.

        A sampler streams `output_peak` + `output_blocks` from the status file every 0.1 s over one ssh
        session. The status file is rewritten about once a second and output_peak is the peak since the
        previous write, so a reading belongs to the hit only when its write (a new output_blocks value) was
        first seen after the hit started. The window starts at the hit and ends `window_s` later; writes seen
        earlier are discarded. Before the hit the sampler waits until two consecutive writes are silent, so no
        tail of the previous hit or program can leak into the number.
        """
        if self.dry_run:
            self.logger(f"[dry-run] stream {DEV_STATUS} output_peak/output_blocks every 0.1s; wait for 2 silent writes "
                        f"(< {QUIET_PEAK}); hit; window starts at the hit and lasts {window_s}s (max over writes first seen inside)")
            self.send_hit(velocity, hold_s)
            self.logger(f"[dry-run] check {DEV_PLAY_LOG_TXT} for a new note_on {SWEEP_NOTE} vel {velocity}")
            return 0.31622, [(0, SWEEP_NOTE, velocity)]  # ~ -10.0 dBFS

        iters = int((preroll_max_s + hold_s + window_s + 4.0) / 0.1)
        remote = (f"i=0; while [ $i -lt {iters} ]; do "
                  f"echo $(grep -E '^(output_peak|output_blocks):' {DEV_STATUS} 2>/dev/null); "
                  f"i=$((i+1)); sleep 0.1; done")
        proc = subprocess.Popen(
            ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
             "-o", "LogLevel=ERROR", "-o", "ConnectTimeout=10", f"{self.cfg['user']}@{self.cfg['ip']}", remote],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        samples: List[Tuple[float, int, float]] = []
        lock = threading.Lock()

        def reader():
            for line in proc.stdout:
                parsed = parse_sample_line(line)
                if parsed:
                    with lock:
                        samples.append((time.monotonic(), parsed[0], parsed[1]))

        th = threading.Thread(target=reader, daemon=True)
        th.start()
        try:
            t0 = time.monotonic()
            quiet = False
            while time.monotonic() - t0 < preroll_max_s:
                time.sleep(0.1)
                with lock:
                    quiet = quiet_writes_seen(list(samples), 2)
                if quiet:
                    break
            if not quiet:
                self.logger(f"  WARNING: status not silent before the hit after {preroll_max_s}s; measuring anyway")
            before = self.play_log_offset()
            t_hit = time.monotonic()
            self.send_hit(velocity, hold_s)
            t_end = t_hit + window_s
            while time.monotonic() < t_end + 0.3:
                time.sleep(0.1)
            with lock:
                snap = list(samples)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except Exception:
                proc.kill()
            th.join(timeout=2)
        peak, used = peak_in_window(snap, t_hit, t_end)
        if used == 0:
            self.logger("  WARNING: no status write seen inside the hit window")
        events = self.play_log_events(before)
        return peak, events

    def capture_screenshot(self, name: str, target_file: Optional[str] = None) -> bool:
        """Capture host track view screenshot and place it in the outdir."""
        dest_png = target_file or os.path.join(self.outdir, f"{name}.png")

        if self.dry_run:
            if self.device_key == "k61":
                self.local(["sh", self.cfg["shot_sh"], name], env={"MPC_SHOT_DIR": self.outdir})
            else:
                self.local(["bash", self.cfg["xshot_sh"], name])
                self.logger(f"[dry-run] mv /Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/xl-shots/{name}.png {dest_png}")
            return True

        if self.device_key == "k61":
            code, _, err = self.local(["sh", self.cfg["shot_sh"], name], env={"MPC_SHOT_DIR": self.outdir})
            if code != 0 or not os.path.isfile(dest_png) or os.path.getsize(dest_png) == 0:
                self.logger(f"Screenshot capture failed for {name}: {err.strip()}")
                return False
            return True
        elif self.device_key == "xl":
            code, _, err = self.local(["bash", self.cfg["xshot_sh"], name])
            xl_default_src = f"/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/xl-shots/{name}.png"
            if os.path.isfile(xl_default_src):
                shutil.move(xl_default_src, dest_png)
            if code != 0 or not os.path.isfile(dest_png) or os.path.getsize(dest_png) == 0:
                self.logger(f"Screenshot capture failed for {name}: {err.strip()}")
                return False
            return True
        return False

    def get_instance_dump(self, timeout_s: float = 3.0) -> Optional[Dict]:
        """Request and parse instance dump from /tmp/synthyboy-play-log-dump.txt."""
        if self.dry_run:
            self.logger(f"[dry-run] touch {DEV_PLAY_LOG_DUMP} and read {DEV_PLAY_LOG_DUMP_TXT}")
            return {
                "program": 0,
                "name": "Init",
                "protected": 0,
                "window": 0,
                "params": {"master.volume": 0.0, "master.pan": 0.0},
                "proxies": {},
            }

        code, _, _ = self.ssh(f"touch {DEV_PLAY_LOG} && rm -f {DEV_PLAY_LOG_DUMP_TXT} && touch {DEV_PLAY_LOG_DUMP}")
        if code != 0:
            return None
        start_t = time.time()
        catalog = Path(__file__).resolve().parents[1] / "docs" / "params.json"
        expected = tuple(p["key"] for p in json.loads(catalog.read_text())["params"])
        while time.time() - start_t < timeout_s:
            time.sleep(0.2)
            code, text, _ = self.ssh(f"cat {DEV_PLAY_LOG_DUMP_TXT} 2>/dev/null")
            if code == 0 and text:
                try:
                    index, name, params = read_instance_snapshot(text, expected)
                except (KeyError, ValueError):
                    continue  # The device may still be appending the chunk.
                return {"program": index, "name": name, "params": params}
        return None

    def query_initial_state(self) -> Tuple[Optional[int], str, Dict[str, float]]:
        """Query the initial program index, name, and parameter state before sweeping."""
        dump = self.get_instance_dump(timeout_s=3.0)
        if dump and dump.get("program") is not None and dump["params"]:
            prog_idx = dump["program"]
            params = dump.get("params", {})
            factory_progs = get_factory_programs()
            prog_name = factory_progs[prog_idx] if 0 <= prog_idx < len(factory_progs) else f"Preset {prog_idx}"
            return prog_idx, dump.get("name", prog_name), params
        return None, "", {}

    def restore_initial_state(self, index: int, fallback_name: str = "") -> Tuple[bool, str]:
        """Restore device to initial preset via remote debug hook (factory clean state)."""
        return self.select_program(index, fallback_name=fallback_name, timeout_s=5.0)


def parse_sweep_row(row):
    raw_index, name, full, half, delta, velocities, timestamp = row
    if not name.strip() or velocities != "127/64":
        raise ValueError("expected a named preset and measured velocities 127/64")
    datetime.datetime.fromisoformat(timestamp)
    peaks = [float(value) for value in (full, half, delta)]
    if not all(math.isfinite(value) for value in peaks):
        raise ValueError("non-finite sweep reading")
    return {"index": int(raw_index), "name": name, "full_peak": peaks[0],
            "half_peak": peaks[1], "diff": peaks[2], "vels": velocities, "ts": timestamp}


def consistent_peaks(entry):
    # All three readings are independently rounded to hundredths.
    return abs(entry["full_peak"] - entry["half_peak"] - entry["diff"]) <= 0.0151


def resumable_presets(csv_path, outdir):
    seen, completed = set(), set()
    with open(csv_path, encoding="utf-8", newline="") as stream:
        for row in csv.reader(stream):
            if not row or not row[0].isdigit():
                continue
            idx = int(row[0])
            if idx in seen:
                completed.discard(idx)
                continue
            seen.add(idx)
            try:
                entry = parse_sweep_row(row)
            except ValueError:
                continue
            stem = f"{idx:03d}-{slugify(entry['name'])}"
            if consistent_peaks(entry) and all(os.path.isfile(os.path.join(outdir, f"{stem}-{hit}.png"))
                                               for hit in ("full", "half")):
                completed.add(idx)
    return completed


def store_sweep_row(path: str, row: list):
    """Replace prior readings for this index only after both new captures exist."""
    rows = []
    if os.path.isfile(path):
        with open(path, newline="", encoding="utf-8") as stream:
            rows = [previous for previous in csv.reader(stream)
                    if not previous or not previous[0].isdigit() or int(previous[0]) != row[0]]
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", newline="", encoding="utf-8",
                                         dir=os.path.dirname(os.path.abspath(path)), delete=False) as stream:
            temporary = stream.name
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerows(rows)
            writer.writerow(row)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            os.unlink(temporary)


def run_sweep(args, logger=None) -> int:
    """Run the unattended level sweep across all 160 factory presets."""
    device_key = args.device
    if device_key not in DEVICES:
        sys.exit(f"Unknown device: {device_key}")

    date_str = datetime.date.today().strftime("%Y%m%d")
    outdir = args.outdir or f"/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/level-sweep-{device_key}-{date_str}"
    outdir = os.path.abspath(outdir)
    os.makedirs(outdir, exist_ok=True)

    log_path = os.path.join(outdir, "sweep.log")
    csv_path = args.csv or os.path.join(outdir, "sweep.csv")

    log_file = open(log_path, "a", encoding="utf-8") if not args.dry_run else None

    def log(msg: str):
        t_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        line = f"[{t_str}] {msg}"
        print(line)
        if logger:
            try:
                logger(line)
            except Exception:
                pass
        if log_file and not log_file.closed:
            log_file.write(line + "\n")
            log_file.flush()

    log(f"Starting unattended level sweep on {DEVICES[device_key]['name']} ({device_key})")
    log(f"Output directory: {outdir}")
    log(f"CSV path: {csv_path}")

    # Device rule: lock directory must be taken and released (also on error/Ctrl-C)
    lock = DeviceLock(DEVICES[device_key]["lock_dir"], dry_run=args.dry_run, logger=log)

    def sig_handler(sig, frame):
        log(f"Received signal {sig}. Releasing device lock and exiting...")
        sys.exit(130)

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    executor = DeviceExecutor(device_key, outdir, dry_run=args.dry_run, logger=log)
    factory_progs = get_factory_programs()

    initial_idx = None

    limit = args.limit if args.limit is not None else PRESET_COUNT
    last_index = FIRST_INDEX + min(limit, PRESET_COUNT) - 1
    consecutive_failures = 0
    total_failed = 0

    try:
        lock.acquire()
        completed_presets = set()
        if args.resume and os.path.isfile(csv_path):
            completed_presets = resumable_presets(csv_path, outdir)
            log(f"Resume mode: found {len(completed_presets)} complete measurements")
        initial_idx, initial_name, initial_params = executor.query_initial_state()
        if initial_idx is None:
            log("Unable to capture initial program; sweep cancelled before preflight.")
            return 1

        # Preflight check
        pre_ok, pre_msg = executor.preflight_check(expected_prog0_name="Init")
        if not pre_ok:
            log(f"PREFLIGHT CHECK FAILED: {pre_msg}")
            return 1

        for idx in range(FIRST_INDEX, last_index + 1):
            fallback_name = factory_progs[idx] if idx < len(factory_progs) else f"Preset {idx}"
            if idx in completed_presets:
                log(f"Preset {idx:03d} ({fallback_name}) already completed; skipping (--resume)")
                consecutive_failures = 0
                continue

            # Preset sweep with up to 2 retries (3 attempts total)
            passed = False
            for attempt in range(3):
                if attempt > 0:
                    log(f"Retrying preset {idx:03d} (attempt {attempt + 1}/3)...")
                    # Delete any partially created images for this preset before retry
                    for f in os.listdir(outdir):
                        if f.startswith(f"{idx:03d}-") and f.endswith(".png"):
                            try:
                                os.remove(os.path.join(outdir, f))
                            except OSError:
                                pass
                    time.sleep(1.0)

                log(f"--- Preset {idx:03d}/{last_index}: {fallback_name} ---")

                # 1. Select program by index
                sel_ok, ack_name = executor.select_program(idx, fallback_name=fallback_name, timeout_s=5.0)
                if not sel_ok:
                    log(f"Failed to select program index {idx}: {ack_name}")
                    continue

                actual_name = ack_name or fallback_name
                actual_slug = slugify(actual_name)
                full_img_name = f"{idx:03d}-{actual_slug}-full"
                half_img_name = f"{idx:03d}-{actual_slug}-half"

                # 2. Tap-reset the peak hold
                executor.tap_reset_peak_hold()

                # 3. FULL hit (velocity 127) measured over its own window
                full_peak_linear, full_events = executor.hit_and_measure(velocity=127, hold_s=SWEEP_HOLD_S, window_s=SWEEP_WINDOW_S)
                full_peak_dbfs = linear_to_dbfs(full_peak_linear)
                if not hit_reached_plugin(full_events, 127):
                    log(f"FULL hit did not reach the plugin as note {SWEEP_NOTE} vel 127 (play log: {full_events})")
                    continue

                # 4. Screenshot FULL hit (the host readout holds the peak)
                shot_full_ok = executor.capture_screenshot(full_img_name)
                if not shot_full_ok and not args.dry_run:
                    log(f"Failed to capture FULL screenshot for preset {idx}")
                    continue

                # 5. Tap-reset the peak hold
                executor.tap_reset_peak_hold()

                # 6. HALF hit (velocity 64)
                half_peak_linear, half_events = executor.hit_and_measure(velocity=64, hold_s=SWEEP_HOLD_S, window_s=SWEEP_WINDOW_S)
                half_peak_dbfs = linear_to_dbfs(half_peak_linear)
                if not hit_reached_plugin(half_events, 64):
                    log(f"HALF hit did not reach the plugin as note {SWEEP_NOTE} vel 64 (play log: {half_events})")
                    continue

                # 7. Screenshot HALF hit
                shot_half_ok = executor.capture_screenshot(half_img_name)
                if not shot_half_ok and not args.dry_run:
                    log(f"Failed to capture HALF screenshot for preset {idx}")
                    continue

                vel_str = f"{full_events[-1][2]}/{half_events[-1][2]}"

                # 11. Record CSV line
                diff = round(full_peak_dbfs - half_peak_dbfs, 2)
                ts = datetime.datetime.now().isoformat(timespec="seconds")
                csv_row = [idx, actual_name, f"{full_peak_dbfs:.2f}", f"{half_peak_dbfs:.2f}", f"{diff:.2f}", vel_str, ts]

                if not args.dry_run:
                    store_sweep_row(csv_path, csv_row)
                else:
                    log(f"[dry-run] append to {csv_path}: {csv_row}")

                log(f"Preset {idx:03d} {actual_name}: Full={full_peak_dbfs:.2f} dBFS, Half={half_peak_dbfs:.2f} dBFS, Diff={diff:.2f} dB")
                passed = True
                break

            if passed:
                consecutive_failures = 0
            else:
                total_failed += 1
                consecutive_failures += 1
                log(f"ERROR: Preset {idx:03d} failed after 3 attempts.")
                if consecutive_failures >= 5:
                    log("ABORTING: 5 consecutive presets failed! Likely hardware or device communication problem.")
                    return 1

        if total_failed == 0:
            log(f"All {min(limit, PRESET_COUNT)} presets successfully swept and captured!")
            return 0
        else:
            log(f"Sweep completed with {total_failed} failed preset(s).")
            return 1

    finally:
        try:
            if initial_idx is not None:
                log(f"--- Restoring initial preset {initial_idx} ({initial_name}) via hook ---")
                res_ok, res_name = executor.restore_initial_state(initial_idx, fallback_name=initial_name)
                if res_ok and res_name == initial_name:
                    log(f"Successfully restored device to initial preset {initial_idx} ({res_name})")
                else:
                    raise RuntimeError(f"Failed to restore initial preset {initial_idx}: {res_name}")

                # Check parameter status after restore
                end_dump = executor.get_instance_dump(timeout_s=3.0)
                if (not end_dump or end_dump.get("program") != initial_idx or end_dump.get("name") != initial_name
                        or end_dump["params"].keys() != initial_params.keys()):
                    raise RuntimeError("Initial preset or its parameter dump could not be verified")
                if initial_params:
                    end_params = end_dump["params"]
                    changed = []
                    for p_name, p_val in initial_params.items():
                        if p_name in end_params:
                            diff = abs(p_val - end_params[p_name])
                            if diff > 1e-4:
                                changed.append((p_name, p_val, end_params[p_name]))
                    if changed:
                        log(f"Changed parameters after restore ({len(changed)}):")
                        for p_name, before_v, after_v in changed:
                            log(f"  {p_name}: initial={before_v}, current={after_v}")
                    else:
                        log("Parameter status: clean (all parameters match initial state)")
        except Exception as e:
            log(f"Sweep restoration failed: {e}")
            raise RuntimeError("Sweep restoration failed") from e
        finally:
            lock.release()
            if log_file:
                log_file.close()


def verify_sweep(
    outdir: str,
    csv_path: Optional[str] = None,
    full_target: float = DEFAULT_FULL_TARGET,
    full_tol: float = DEFAULT_FULL_TOL,
    half_diff: Optional[float] = None,
    half_tol: float = DEFAULT_HALF_TOL,
    report_path: Optional[str] = None,
    logger=None,
    count: int = PRESET_COUNT,
    table_path: Optional[str] = None,
) -> Tuple[bool, Dict]:
    """Offline verification of sweep results (factory presets 1..count; count < 160 for partial trials)."""
    log = logger or print
    last_index = FIRST_INDEX + count - 1
    outdir = os.path.abspath(outdir)
    if not os.path.isdir(outdir):
        return False, {"error": f"Output directory does not exist: {outdir}"}

    report_file = report_path or os.path.join(outdir, "verify-report.md")
    csv_file = csv_path or os.path.join(outdir, "sweep.csv")

    errors = []
    preset_data = {}  # idx -> dict

    expected_table = load_expected_levels(table_path) if table_path != "" and half_diff is None else {}
    if half_diff is None:
        half_diff = DEFAULT_HALF_DIFF

    # 1. Read and verify CSV
    if not os.path.isfile(csv_file):
        errors.append(f"Missing CSV file: {csv_file}")
    else:
        with open(csv_file, encoding="utf-8", newline="") as f:
            rows = [row for row in csv.reader(f) if row]
        if len(rows) != count:
            errors.append(f"CSV row count is {len(rows)}, expected {count}")

        for row in rows:
            try:
                entry = parse_sweep_row(row)
                idx = entry["index"]
                if not FIRST_INDEX <= idx <= last_index or idx in preset_data:
                    raise ValueError("unexpected or duplicate preset index")
                preset_data[idx] = entry
            except (ValueError, IndexError) as e:
                errors.append(f"Unparsable CSV row: {row} ({e})")

    # 2. Check images in outdir
    all_files = os.listdir(outdir)
    image_files = [f for f in all_files if f.lower().endswith(".png")]

    # Group image files by preset prefix (NNN-)
    preset_images = {i: [] for i in range(FIRST_INDEX, last_index + 1)}
    stray_images = []

    for img in image_files:
        if img == "preflight.png":
            continue
        m = re.match(r"^(\d{3})-.*-(full|half)\.png$", img)
        if m:
            idx = int(m.group(1))
            if FIRST_INDEX <= idx <= last_index:
                preset_images[idx].append(img)
            else:
                stray_images.append(img)
        else:
            stray_images.append(img)

    if stray_images:
        errors.append(f"Found {len(stray_images)} stray or unexpected image file(s): {', '.join(stray_images[:10])}")

    # Check each factory preset index 1..160 (index 0 = Init is not swept)
    results = []
    failing_presets = []

    for idx in range(FIRST_INDEX, last_index + 1):
        imgs = preset_images[idx]
        imgs_full = [f for f in imgs if f.endswith("-full.png")]
        imgs_half = [f for f in imgs if f.endswith("-half.png")]

        preset_errs = []
        if len(imgs) != 2:
            preset_errs.append(f"expected exactly 2 images, found {len(imgs)}: {imgs}")
        if len(imgs_full) != 1:
            preset_errs.append(f"missing or multiple -full images ({len(imgs_full)})")
        if len(imgs_half) != 1:
            preset_errs.append(f"missing or multiple -half images ({len(imgs_half)})")

        # Check mtimes if both exist
        if len(imgs_full) == 1 and len(imgs_half) == 1:
            full_path = os.path.join(outdir, imgs_full[0])
            half_path = os.path.join(outdir, imgs_half[0])
            mtime_full = os.path.getmtime(full_path)
            mtime_half = os.path.getmtime(half_path)
            if mtime_full > mtime_half:
                preset_errs.append(f"-full ({mtime_full}) written after -half ({mtime_half})")

        # Check CSV entry
        c_entry = preset_data.get(idx)
        if not c_entry:
            preset_errs.append("missing from CSV")
            name = imgs_full[0].split("-", 1)[1].rsplit("-", 1)[0] if imgs_full else f"Preset {idx}"
            full_peak = -999.0
            half_peak = -999.0
            diff = 0.0
        else:
            name = c_entry["name"]
            full_peak = c_entry["full_peak"]
            half_peak = c_entry["half_peak"]
            diff = c_entry["diff"]
            for hit, files in (("full", imgs_full), ("half", imgs_half)):
                expected = f"{idx:03d}-{slugify(name)}-{hit}.png"
                if len(files) == 1 and files[0] != expected:
                    preset_errs.append(f"CSV preset expects {expected}, found {files[0]}")
            if not consistent_peaks(c_entry):
                preset_errs.append("reported difference disagrees with full and half peaks")

        # Level tolerance checks
        key = slugify(name)
        used_target = expected_table.get(key, half_diff)
        used_source = "table" if key in expected_table else "default"
        full_ok = abs(full_peak - full_target) <= full_tol
        half_ok = abs(diff - used_target) <= half_tol
        if not full_ok:
            preset_errs.append(f"full peak {full_peak:.2f} outside target {full_target:.2f} ± {full_tol:.2f}")
        if not half_ok:
            preset_errs.append(f"diff {diff:.2f} outside target {used_target:.2f} ± {half_tol:.2f} ({used_source})")

        overall_ok = (len(preset_errs) == 0)

        res_item = {
            "index": idx,
            "name": name,
            "full_peak": full_peak,
            "full_ok": full_ok,
            "half_peak": half_peak,
            "diff": diff,
            "target_diff": used_target,
            "target_source": used_source,
            "half_ok": half_ok,
            "overall_ok": overall_ok,
            "errors": preset_errs,
        }
        results.append(res_item)
        if not overall_ok:
            failing_presets.append(res_item)

    total_pass = sum(1 for r in results if r["overall_ok"])
    total_fail = len(failing_presets)
    all_ok = (total_fail == 0 and len(errors) == 0)

    # Write verify-report.md
    with open(report_file, "w", encoding="utf-8") as f:
        f.write("# SynthyBoy MPC Level Sweep Verification Report\n\n")
        f.write(f"- **Directory:** `{outdir}`\n")
        f.write(f"- **Generated:** {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"- **Total Presets:** {count}\n")
        f.write(f"- **Passed:** {total_pass}\n")
        f.write(f"- **Failed:** {total_fail}\n")
        f.write(f"- **Overall Status:** {'**PASS**' if all_ok else '**FAIL**'}\n\n")

        f.write("## Targets and Tolerances\n")
        f.write(f"- **Full Hit Peak:** {full_target:+.2f} dBFS ± {full_tol:.2f} dB (range [{full_target - full_tol:.2f}, {full_target + full_tol:.2f}])\n")
        if expected_table:
            f.write(f"- **Half Hit Difference:** per-preset from output level table (fallback: {half_diff:+.2f} dB) ± {half_tol:.2f} dB\n\n")
        else:
            f.write(f"- **Half Hit Difference:** {half_diff:+.2f} dB ± {half_tol:.2f} dB (range [{half_diff - half_tol:.2f}, {half_diff + half_tol:.2f}])\n\n")

        if errors:
            f.write("## Global Integrity Errors\n")
            for err in errors:
                f.write(f"- ❌ {err}\n")
            f.write("\n")

        if failing_presets:
            f.write(f"## Failing Presets ({len(failing_presets)})\n\n")
            f.write("| Index | Name | Full Peak | Full Status | Half Peak | Diff | Target Diff | Diff Status | Overall | Issues |\n")
            f.write("|:-----:|:-----|:---------:|:-----------:|:---------:|:----:|:-----------:|:-----------:|:-------:|:-------|\n")
            for r in failing_presets:
                f_st = "PASS" if r["full_ok"] else "**FAIL**"
                d_st = "PASS" if r["half_ok"] else "**FAIL**"
                tgt_str = f"{r['target_diff']:.2f} ({r['target_source']})"
                issues = "; ".join(r["errors"])
                f.write(f"| {r['index']:03d} | {r['name']} | {r['full_peak']:.2f} | {f_st} | {r['half_peak']:.2f} | {r['diff']:.2f} | {tgt_str} | {d_st} | **FAIL** | {issues} |\n")
            f.write("\n")

        f.write(f"## All Presets ({count})\n\n")
        f.write("| Index | Name | Full Peak | Full Status | Half Peak | Diff | Target Diff | Diff Status | Overall |\n")
        f.write("|:-----:|:-----|:---------:|:-----------:|:---------:|:----:|:-----------:|:-----------:|:-------:|\n")
        for r in results:
            f_st = "PASS" if r["full_ok"] else "FAIL"
            d_st = "PASS" if r["half_ok"] else "FAIL"
            tgt_str = f"{r['target_diff']:.2f} ({r['target_source']})"
            o_st = "PASS" if r["overall_ok"] else "**FAIL**"
            f.write(f"| {r['index']:03d} | {r['name']} | {r['full_peak']:.2f} | {f_st} | {r['half_peak']:.2f} | {r['diff']:.2f} | {tgt_str} | {d_st} | {o_st} |\n")

        f.write("\n## Preset restoration\n\n")
        f.write(restoration_evidence(os.path.join(outdir, "sweep.log")) + "\n")

    log(f"Verification report written to {report_file}")
    log(f"Verification result: {total_pass}/{count} passed. Status: {'PASS' if all_ok else 'FAIL'}")

    return all_ok, {
        "all_ok": all_ok,
        "total_pass": total_pass,
        "total_fail": total_fail,
        "errors": errors,
        "failing_presets": failing_presets,
    }


def selftest(logger=None) -> int:
    """Run comprehensive verifier selftest against synthetic directory."""
    log = logger or print
    log("Running selftest: verifier against synthetic datasets...")

    factory_progs = get_factory_programs()

    with tempfile.TemporaryDirectory() as tmpdir:
        expected_diffs = load_expected_levels()
        # 1. Populate ideal 160 preset dataset
        base_time = 1700000000.0
        csv_lines = []
        for idx in range(FIRST_INDEX, LAST_INDEX + 1):
            name = factory_progs[idx]
            slug = slugify(name)
            full_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-full.png")
            half_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-half.png")

            with open(full_img, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\nfake_full")
            with open(half_img, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\nfake_half")

            # Set mtimes: -full written before -half
            t_full = base_time + idx * 10
            t_half = t_full + 2.0
            os.utime(full_img, (t_full, t_full))
            os.utime(half_img, (t_half, t_half))

            full_pk = -10.0
            half_pk = full_pk - expected_diffs[slugify(name)]
            diff = round(full_pk - half_pk, 2)
            ts = "2026-11-20T12:00:00"
            csv_lines.append(f"{idx},{name},{full_pk:.2f},{half_pk:.2f},{diff:.2f},127/64,{ts}\n")

        csv_path = os.path.join(tmpdir, "sweep.csv")
        with open(csv_path, "w", encoding="utf-8") as f:
            f.writelines(csv_lines)

        # Preflight image (allowed)
        with open(os.path.join(tmpdir, "preflight.png"), "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\nfake_preflight")

        # Test A: Ideal dataset must PASS
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert ok, f"Ideal synthetic set failed: {res}"
        log("  [PASS] Test A: Ideal 160-preset dataset verified successfully")

        first = f"001-{slugify(factory_progs[1])}-half.png"
        wrong = "001-other-preset-half.png"
        os.rename(os.path.join(tmpdir, first), os.path.join(tmpdir, wrong))
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert not ok and any("CSV preset expects" in err for err in res["failing_presets"][0]["errors"])
        os.rename(os.path.join(tmpdir, wrong), os.path.join(tmpdir, first))

        fields = csv_lines[0].split(",")
        fields[3] = "-60.00"
        with open(csv_path, "w", encoding="utf-8") as f:
            f.writelines([",".join(fields)] + csv_lines[1:])
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert not ok and "reported difference disagrees with full and half peaks" in res["failing_presets"][0]["errors"]

        for invalid in [csv_lines[0].split(",127/64")[0], csv_lines[0].replace("127/64", "63/64")]:
            with open(csv_path, "w", encoding="utf-8") as f:
                f.writelines([invalid.rstrip() + "\n"] + csv_lines[1:])
            ok, res = verify_sweep(tmpdir, logger=lambda m: None)
            assert not ok and any("Unparsable CSV row" in err for err in res["errors"])

        # Test B: Out of range full peak must FAIL
        with open(csv_path, "w", encoding="utf-8") as f:
            lines = list(csv_lines)
            # preset 5 full peak -5.0 (outside -10.0 +- 2.0)
            lines[5 - FIRST_INDEX] = f"5,{factory_progs[5]},-5.00,-21.90,16.90,127/64,2026-11-20T12:00:00\n"
            f.writelines(lines)
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert not ok, "Expected out-of-range full peak to fail"
        assert any(r["index"] == 5 for r in res["failing_presets"])
        log("  [PASS] Test B: Detected out-of-tolerance full peak")

        # Restore
        with open(csv_path, "w", encoding="utf-8") as f:
            f.writelines(csv_lines)

        # Test C: Missing image must FAIL
        p10_half = os.path.join(tmpdir, f"010-{slugify(factory_progs[10])}-half.png")
        os.remove(p10_half)
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert not ok, "Expected missing image to fail"
        assert any(r["index"] == 10 for r in res["failing_presets"])
        log("  [PASS] Test C: Detected missing half image")

        # Restore
        with open(p10_half, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\nfake_half")
        t_half = base_time + 10 * 10 + 2.0
        os.utime(p10_half, (t_half, t_half))

        # Test D: Inverted mtime ordering must FAIL (-full after -half)
        p20_full = os.path.join(tmpdir, f"020-{slugify(factory_progs[20])}-full.png")
        p20_half = os.path.join(tmpdir, f"020-{slugify(factory_progs[20])}-half.png")
        os.utime(p20_full, (base_time + 500, base_time + 500))
        os.utime(p20_half, (base_time + 400, base_time + 400))  # earlier than full!
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert not ok, "Expected inverted mtime to fail"
        assert any(r["index"] == 20 for r in res["failing_presets"])
        log("  [PASS] Test D: Detected inverted mtime ordering (-full after -half)")

        # Restore
        os.utime(p20_full, (base_time + 200, base_time + 200))
        os.utime(p20_half, (base_time + 202, base_time + 202))

        # Test E: Stray image in directory must FAIL
        stray_file = os.path.join(tmpdir, "unexpected_test_shot.png")
        with open(stray_file, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\nstray")
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert not ok, "Expected stray image to fail"
        assert len(res["errors"]) > 0
        log("  [PASS] Test E: Detected stray image in output directory")
        os.remove(stray_file)

        # Test F: CSV count != 160 must FAIL
        with open(csv_path, "w", encoding="utf-8") as f:
            f.writelines(csv_lines[:150])  # only 150 lines
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert not ok, "Expected incomplete CSV to fail"
        assert any("CSV row count" in e for e in res["errors"])
        log("  [PASS] Test F: Detected CSV row count != 160")

        # Test G: Saturating preset with table threshold
        # Bend Formant Growl (preset 3 in factory list) has offline table expected diff 6.21 dB
        with open(csv_path, "w", encoding="utf-8") as f:
            lines = list(csv_lines)
            bfg_idx = factory_progs.index("Bend Formant Growl")
            bfg_name = factory_progs[bfg_idx]
            lines[bfg_idx - FIRST_INDEX] = f"{bfg_idx},{bfg_name},-10.00,-16.25,6.25,127/64,2026-11-20T12:00:00\n"
            f.writelines(lines)
        # With table loaded, diff 6.25 matches table 6.21 +- 3.0 dB -> PASS
        ok, res = verify_sweep(tmpdir, logger=lambda m: None)
        assert ok, f"Expected saturating preset {bfg_name} with diff 6.25 to pass with table threshold: {res}"
        log("  [PASS] Test G1: Saturating preset verified against offline table threshold")
        # With table disabled (table_path=""), diff 6.25 is outside 11.9 +- 3.0 dB -> FAIL
        ok, res = verify_sweep(tmpdir, table_path="", logger=lambda m: None)
        assert not ok, "Expected saturating preset to fail when table lookup is disabled"
        assert any(r["index"] == bfg_idx for r in res["failing_presets"])
        log("  [PASS] Test G2: Saturating preset fails with fixed default diff when table disabled")

    # Test H: Play log counter reset recovery
    restart_log = (
        "500 120000 setParameter 2703 0.5\n"
        "501 120050 note_on channel 1 note 48 vel 127 engine 127 dropped 0\n"
        "502 120850 note_off channel 1 note 48 vel 0 engine 0 dropped 0\n"
        "# App restart happens here, counter resets to 1\n"
        "1 0 effSetProgram 0\n"
        "2 100 note_on channel 1 note 48 vel 127 engine 127 dropped 0\n"
        "3 900 note_off channel 1 note 48 vel 0 engine 0 dropped 0\n"
    )
    # after_counter was 502 from stale session; active session has counters 1..3
    ev = parse_play_log_events(restart_log, after_counter=502)
    assert len(ev) == 1 and ev[0] == (2, 48, 127), f"Expected event (2, 48, 127), got {ev}"
    assert hit_reached_plugin(ev, 127)
    log("  [PASS] Test H: Play log counter reset recovery successfully isolated active session")

    # Test I: Instance dump parsing
    sample_dump = (
        "selectors 0 1 2\n"
        "program 2 protected 0 window 1\n"
        "ui_write_calls 5 ui_automate_calls 12\n"
        "p 0 master.volume -4.0\n"
        "p 1 master.pan 0.0\n"
        "proxy 0 0.5\n"
        "chunk 1234\n"
    )
    dump_res = parse_instance_dump(sample_dump)
    assert dump_res["program"] == 2
    assert dump_res["params"]["master.volume"] == -4.0
    assert dump_res["proxies"][0] == 0.5
    log("  [PASS] Test I: Instance dump parser verified")

    with tempfile.TemporaryDirectory() as tmpdir:
        path = os.path.join(tmpdir, "sweep.log")
        def record(message):
            with open(path, "a", encoding="utf-8") as stream:
                stream.write("[time] " + message + "\n")
        def begin():
            record("Starting unattended level sweep on test")
            record("--- Restoring initial preset 7 (test) via hook ---")
            record("Successfully restored device to initial preset 7 (test)")
        begin()
        record("Parameter status: clean (all parameters match initial state)")
        assert restoration_evidence(path).startswith("Clean:")
        record("Starting unattended level sweep on test")
        assert restoration_evidence(path).startswith("Unverified")
        begin()
        record("Changed parameters after restore (2):")
        record("x: initial=0, current=1")
        assert restoration_evidence(path).startswith("Unverified")
        record("y: initial=1, current=0")
        result = restoration_evidence(path)
        assert result.startswith("Changed:") and "x: initial=0" in result and "y: initial=1" in result
        record("Sweep restoration failed: missing dump")
        assert restoration_evidence(path) == "Failed: missing dump"

    log("Selftest PASSED completely.")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Unattended overnight level sweep and verifier for Akai MPC devices."
    )
    subparsers = parser.add_subparsers(dest="mode", required=True)

    # run mode
    run_p = subparsers.add_parser("run", help="Run level sweep on device")
    run_p.add_argument("--device", choices=["k61", "xl"], default="k61", help="Device to sweep (k61 or xl)")
    run_p.add_argument("--outdir", default=None, help="Output directory")
    run_p.add_argument("--limit", type=int, default=None, help="Limit sweep to first N presets (indices 1..N)")
    run_p.add_argument("--dry-run", action="store_true", help="Print commands without executing or touching hardware")
    run_p.add_argument("--resume", action="store_true", help="Resume an interrupted sweep")
    run_p.add_argument("--csv", default=None, help="CSV file path (defaults to <outdir>/sweep.csv)")

    # verify mode
    ver_p = subparsers.add_parser("verify", help="Verify sweep output offline")
    ver_p.add_argument("outdir", nargs="?", default=None, help="Directory containing sweep results to verify")
    ver_p.add_argument("--outdir", dest="outdir_opt", default=None, help="Directory containing sweep results to verify")
    ver_p.add_argument("--csv", default=None, help="CSV file path (defaults to <outdir>/sweep.csv)")
    ver_p.add_argument("--full-target", type=float, default=DEFAULT_FULL_TARGET, help="Target full hit peak dBFS (default: -10.0)")
    ver_p.add_argument("--full-tol", type=float, default=DEFAULT_FULL_TOL, help="Tolerance for full hit peak dBFS (default: 2.0)")
    ver_p.add_argument("--half-diff", type=float, default=None, help="Override the per-preset full minus half target (dB)")
    ver_p.add_argument("--half-tol", type=float, default=DEFAULT_HALF_TOL, help="Tolerance for difference full - half dBFS (default: 3.0)")
    ver_p.add_argument("--table", default=None, help="Path to OUTPUT-LEVEL-TABLE markdown file for per-preset expected half difference")
    ver_p.add_argument("--limit", type=int, default=PRESET_COUNT, help="Verify only presets 1..N (partial trial); default 160")
    ver_p.add_argument("--report", default=None, help="Path for verify-report.md (defaults to <outdir>/verify-report.md)")

    # selftest mode
    subparsers.add_parser("selftest", help="Run verifier on synthetic directory")

    args = parser.parse_args()

    if args.mode == "run":
        sys.exit(run_sweep(args))
    elif args.mode == "verify":
        target_dir = args.outdir_opt or args.outdir
        if not target_dir:
            sys.exit("Error: outdir must be specified for verify mode")
        ok, _ = verify_sweep(
            outdir=target_dir,
            csv_path=args.csv,
            full_target=args.full_target,
            full_tol=args.full_tol,
            half_diff=args.half_diff,
            half_tol=args.half_tol,
            table_path=args.table,
            report_path=args.report,
            count=args.limit,
        )
        sys.exit(0 if ok else 1)
    elif args.mode == "selftest":
        sys.exit(selftest())


if __name__ == "__main__":
    main()
