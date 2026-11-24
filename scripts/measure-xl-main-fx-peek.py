#!/usr/bin/env python3
# SynthyBoy - XL Acceptance Measurement Tool: MAIN FX PEEK across Breath of Machines <-> Acid Saw Reso
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Automates the XL hardware acceptance procedure for Mixer MAIN FX PEEK and physical Q-Link targets:
  - Preset A: 'Breath of Machines' (Slot 0: EQ, 5 rows, 16 Q targets)
  - Preset B: 'Acid Saw Reso' (Slot 0: Distortion, 2 rows, 5 Q targets)
  - Open A -> B -> A2 preset changes with PEEK panel open.
  - Reopened control comparisons (A/A-reopen, B/B-reopen, A2/A2-reopen) asserting delta == 0.
  - Rejection of stale A/B response graphs (assert delta_AB > 0).
  - Target-only parameter check on physical Q-Link encoder turn (only distortion.drive changed, 2636 untouched).
  - Validation of parameter dump integrity and rejection of empty/malformed dumps.
  - Factory preset restoration and complete parameter comparison on success and failure.
  - Distinct reports for simulation, fixture replay and physical XL runs.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time
from typing import NamedTuple
import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mpc_instance_snapshot import read_instance_snapshot

# Exact coordinates on MPC XL (1280x800)
MAIN_FX_BUTTON_TAP = (461, 137)  # Routing bar MAIN FX target (rect: 414, 114, 96, 56)
CLOSE_BUTTON_TAP = (40, 205)     # Native PEEK header close button
PANEL_ROI = (8, 134, 1272, 714)  # PEEK panel full area
EDITOR_ROI = (16, 210, 1264, 700)# Slot editor area
GRAPH_ROI = (65, 310, 1215, 430) # Response curve / waveshaper area

# Expected Q-Link target vectors
A_Q_TARGETS = [837, 838, 841, 842, 845, 846, 1500, -1, 836, 840, 844, 2635, 839, 843, 847, -1]
B_Q_TARGETS = [828, 829, 830, 1499, 831, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1]

# Total engine parameter count in SynthyBoy (docs/params.json)
TOTAL_ENGINE_PARAMS = 2637
TARGET_ONLY_UNTOUCHED_COUNT = TOTAL_ENGINE_PARAMS - 1  # 2636

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PARAMS_JSON = REPO_ROOT / "docs" / "params.json"


def parse_args():
    p = argparse.ArgumentParser(
        description="XL MAIN FX PEEK acceptance procedure",
        epilog="Hardware mode needs one SynthyBoy instance, the Mixer root visible and an operator at Q2.",
    )
    p.add_argument(
        "--device-dir",
        type=Path,
        default=Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/inactive-xl-2026-11-21"),
        help="Path to helper directory containing xl/tap/shot wrappers",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/mainfx-peek-xl"),
        help="Directory to save raw shots, statuses, and diff logs",
    )
    offline = p.add_mutually_exclusive_group()
    offline.add_argument(
        "--dry-run",
        action="store_true",
        help="Run simulated acceptance checks offline without hardware execution",
    )
    offline.add_argument(
        "--fixture-dir",
        type=Path,
        default=None,
        help="Path to pre-recorded offline fixture directory for replay without hardware",
    )
    return p.parse_args()


PLAY_LOG = "/tmp/synthyboy-play-log"
SELECT = "/tmp/synthyboy-select-program"


class ProgramSnapshot(NamedTuple):
    index: int
    name: str
    params: dict[str, float]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_default_params(params_path: Path = DEFAULT_PARAMS_JSON) -> dict[str, float]:
    data = json.loads(params_path.read_text(encoding="utf-8"))
    result = {param["key"]: param["default"] for param in data["params"]}
    if len(result) != TOTAL_ENGINE_PARAMS:
        raise ValueError(f"Invalid parameter catalog: {params_path}")
    return result


def validate_and_normalize_param_dump(data: dict) -> dict[str, float]:
    """Require one finite value for each engine key in a snapshot."""
    if not isinstance(data, dict) or not data:
        raise ValueError("Invalid parameter dump: dump is empty or not a JSON dictionary")

    if isinstance(data.get("params"), list):
        entries = data["params"]
        if len(entries) != TOTAL_ENGINE_PARAMS:
            raise ValueError(f"Invalid parameter dump: expected {TOTAL_ENGINE_PARAMS} parameters, found {len(entries)}")
        params = {}
        for entry in entries:
            key = entry.get("key") if isinstance(entry, dict) else None
            if not isinstance(key, str) or key in params or "value" not in entry:
                raise ValueError("Invalid parameter dump: missing or duplicate parameter key or value")
            params[key] = entry["value"]
        data = params

    if len(data) != TOTAL_ENGINE_PARAMS:
        raise ValueError(f"Invalid parameter dump: expected {TOTAL_ENGINE_PARAMS} parameter keys, got {len(data)}")
    if "distortion.drive" not in data:
        raise ValueError("Invalid parameter dump: required key 'distortion.drive' missing")
    expected = load_default_params().keys()
    if data.keys() != expected:
        raise ValueError(f"Invalid parameter dump: missing {sorted(expected - data.keys())}, unexpected {sorted(data.keys() - expected)}")
    if any(type(value) not in (int, float) or not math.isfinite(value) for value in data.values()):
        raise ValueError("Invalid parameter dump: expected finite numeric values for every key")
    return {key: float(value) for key, value in data.items()}


def make_snapshot(index: int, name: str, params: dict) -> ProgramSnapshot:
    if type(index) is not int or index < 0 or not isinstance(name, str) or not name.strip():
        raise ValueError(f"Invalid program identity: {index!r} {name!r}")
    if index == 0 and name != "Init":
        raise ValueError(f"Program {name!r} has no restorable factory index")
    return ProgramSnapshot(index, name, validate_and_normalize_param_dump(params))


def parse_instance_dump(text: str) -> ProgramSnapshot:
    return ProgramSnapshot(*read_instance_snapshot(text, tuple(load_default_params())))


def parameter_diff(before: dict[str, float], after: dict[str, float]) -> dict:
    return {
        key: {"before": before.get(key), "after": after.get(key)}
        for key in sorted(before.keys() | after.keys())
        if before.get(key) != after.get(key)
    }


def make_synthetic_shot(program_name: str, panel_open: bool) -> np.ndarray:
    """
    Generates a deterministic, non-zero 1280x800 RGB frame reflecting the
    active preset and PEEK panel status for offline simulation.
    """
    img = np.full((800, 1280, 3), 24, dtype=np.uint8)  # Dark gray background
    if not panel_open:
        img[500:750, 100:1180] = 40  # Mixer strips
        return img

    x1, y1, x2, y2 = PANEL_ROI
    img[y1:y2, x1:x2] = 32

    ex1, ey1, ex2, ey2 = EDITOR_ROI
    img[ey1:ey2, ex1:ex2] = 45

    gx1, gy1, gx2, gy2 = GRAPH_ROI
    img[gy1:gy2, gx1:gx2] = 16  # Graph display canvas

    gw = gx2 - gx1
    gh = gy2 - gy1
    xs = np.arange(gw)

    if "Breath" in program_name:
        # EQ response curve (green/cyan curve in graph area)
        curve_y = np.clip(
            (gh // 2) + (gh * 0.25 * np.sin(2 * np.pi * xs / 280) - gh * 0.15 * np.cos(2 * np.pi * xs / 550)).astype(int),
            2,
            gh - 3,
        )
        for dy in range(-2, 3):
            img[gy1 + curve_y + dy, gx1 + xs] = [60, 220, 140]
    elif "Acid" in program_name:
        # Distortion waveshaper transfer curve (orange/red tanh curve)
        norm_x = (xs - (gw / 2.0)) / (gw / 4.0)
        curve_y = np.clip((gh // 2) + (gh * 0.4 * np.tanh(norm_x)).astype(int), 2, gh - 3)
        for dy in range(-2, 3):
            img[gy1 + curve_y + dy, gx1 + xs] = [240, 110, 40]
    else:
        img[gy1:gy2, gx1:gx2] = 80

    return img


def validate_shot(img: np.ndarray, name: str, roi: tuple[int, int, int, int] = GRAPH_ROI) -> None:
    """Validates that a captured image is non-empty, has expected dimensions, and is not all zeros."""
    if img is None:
        raise ValueError(f"Shot '{name}' is None")
    if not isinstance(img, np.ndarray):
        raise TypeError(f"Shot '{name}' is not a numpy array: {type(img)}")
    if img.shape != (800, 1280, 3):
        raise ValueError(f"Shot '{name}' has unexpected shape: {img.shape}, expected (800, 1280, 3)")
    if np.all(img == 0):
        raise AssertionError(f"Invalid shot '{name}': frame contains only zeros (blank capture)")
    if roi:
        x1, y1, x2, y2 = roi
        roi_crop = img[y1:y2, x1:x2]
        if np.all(roi_crop == 0):
            raise AssertionError(f"Invalid shot '{name}': ROI {roi} contains only zeros (blank canvas)")


def delta_pixels(a: np.ndarray, b: np.ndarray, roi: tuple[int, int, int, int] = None) -> int:
    """Computes differing pixel count between two RGB images, optionally within an ROI."""
    if a is None or b is None:
        raise ValueError("Cannot compute delta: image array is None")
    if not isinstance(a, np.ndarray) or not isinstance(b, np.ndarray):
        raise TypeError("Images must be numpy ndarrays")
    if a.shape != b.shape:
        raise ValueError(f"Image shape mismatch: {a.shape} vs {b.shape}")
    if roi:
        x1, y1, x2, y2 = roi
        a = a[y1:y2, x1:x2]
        b = b[y1:y2, x1:x2]
    if a.size == 0 or b.size == 0:
        raise ValueError("ROI yields empty image region")
    return int(np.any(a != b, axis=2).sum())


class XLDeviceRunner:
    def __init__(self, dev_dir: Path, out_dir: Path, dry_run: bool = False, fixture_dir: Path = None):
        if dry_run and fixture_dir is not None:
            raise ValueError("Choose either simulation or fixture replay")
        self.dev_dir = Path(dev_dir)
        self.out_dir = Path(out_dir)
        self.dry_run = dry_run
        self.fixture_dir = Path(fixture_dir) if fixture_dir is not None else None
        self.out_dir.mkdir(parents=True, exist_ok=True)
        if self.fixture_dir is not None and self.fixture_dir.resolve() == self.out_dir.resolve():
            raise ValueError("Fixture replay requires a separate output directory")
        self.current_program = "Init"
        self.current_index = 0
        self.last_ack = 0
        self.panel_open = False
        self.params = load_default_params() if dry_run else None

    @property
    def offline(self) -> bool:
        return self.dry_run or self.fixture_dir is not None

    def remote(self, command: str) -> str:
        if self.offline:
            raise RuntimeError("Offline acceptance cannot invoke a device helper")
        return subprocess.check_output([str(self.dev_dir / "xl"), command], text=True, timeout=20)

    def tap(self, x: int, y: int, delay: float = 0.7) -> None:
        print(f"Tap ({x}, {y})")
        if not self.offline:
            subprocess.check_call([str(self.dev_dir / "tap"), str(x), str(y)], timeout=20)
            time.sleep(delay)
        if (x, y) == MAIN_FX_BUTTON_TAP:
            self.panel_open = True
        elif (x, y) == CLOSE_BUTTON_TAP:
            self.panel_open = False

    def status(self, label: str, **expected) -> dict:
        want = {"view": "mixer", "gpu_state": "running", "instances": "1", **expected}
        for attempt in range(1 if self.offline else 16):
            if self.dry_run:
                targets = A_Q_TARGETS if self.current_program == "Breath of Machines" else B_Q_TARGETS
                state = {"view": "mixer", "page_top": "26" if self.panel_open else "0",
                         "gpu_state": "running", "instances": "1", "page_q_slots": " ".join(map(str, targets))}
                text = "".join(f"{key}: {value}\n" for key, value in state.items())
            else:
                text = ((self.fixture_dir / f"{label}.status").read_text(encoding="utf-8") if self.fixture_dir
                        else self.remote("cat /tmp/synthyboy-overlay-status.txt"))
                state = dict(line.split(": ", 1) for line in text.splitlines() if ": " in line)
            (self.out_dir / f"{label}.status").write_text(text, encoding="utf-8")
            if all(state.get(key) == str(value) for key, value in want.items()):
                return state
            if not self.offline and attempt < 15:
                time.sleep(0.25)
        raise AssertionError(f"{label}: expected status {want}, got {state}")

    def select_program(self, program: int | str, label: str) -> None:
        print(f"Select program {program!r} ({label})")
        if self.dry_run:
            programs = ["Init", "Breath of Machines", "Acid Saw Reso"]
            index = program if isinstance(program, int) else programs.index(program)
            text = f"{self.last_ack + 1} ok {index} {programs[index]}\n"
        elif self.fixture_dir:
            text = (self.fixture_dir / f"{label}.ack").read_text(encoding="utf-8")
        else:
            request = shlex.quote(str(program))
            text = self.remote(
                f"rm -f {SELECT} {SELECT}.ack && printf '%s\\n' {request} > {SELECT} && "
                f"for i in $(seq 1 35); do test -s {SELECT}.ack && break; sleep .2; done && "
                f"cat {SELECT}.ack"
            )
        (self.out_dir / f"{label}.ack").write_text(text, encoding="utf-8")
        match = re.fullmatch(r"([1-9][0-9]*) ok ([0-9]+) ([^\r\n]+)\n?", text)
        require(match is not None, f"{label}: program selection failed: {text.strip()!r}")
        counter, index, name = int(match[1]), int(match[2]), match[3]
        require(counter > self.last_ack, f"{label}: stale selection ACK {counter}")
        recorded = index if isinstance(program, int) else name
        require(recorded == program, f"{label}: program ACK expected {program!r}, got {recorded!r}")
        self.last_ack = counter
        self.current_program, self.current_index = name, index
        if self.dry_run:
            self.params = load_default_params()
        if not self.offline:
            time.sleep(0.8)

    def shot(self, name: str, targets: list[int]) -> np.ndarray:
        print(f"Capture shot {name}")
        status = self.status(name, page_top=26, page_q_slots=" ".join(map(str, targets)))
        composed = self.out_dir / f"{name}-composed.png"
        if self.dry_run:
            image = make_synthetic_shot(self.current_program, self.panel_open)
        elif self.fixture_dir:
            with Image.open(self.fixture_dir / composed.name) as capture:
                image = np.asarray(capture.convert("RGB"))
        else:
            plane = int(status["display_overlay_plane"])
            require(plane > 0, f"{name}: invalid overlay plane {plane}")
            composed.unlink(missing_ok=True)
            subprocess.check_call([str(self.dev_dir / "shot"), str(self.out_dir.resolve() / name), str(plane)], timeout=45)
            with Image.open(composed) as capture:
                image = np.asarray(capture.convert("RGB"))
        validate_shot(image, name, GRAPH_ROI)
        if self.offline:
            Image.fromarray(image).save(composed)
        return image

    def turn_q(self, knob: int, delta_db: float) -> None:
        if self.dry_run:
            self.params["distortion.drive"] += delta_db
        elif self.fixture_dir:
            print(f"Replay the recorded Q{knob} edit")
        else:
            input(f"Turn the physical Q{knob} encoder by {delta_db:+g} dB on the XL. Press Enter after the readout settles. ")

    def snapshot(self, label: str) -> ProgramSnapshot:
        if self.dry_run:
            snapshot = make_snapshot(self.current_index, self.current_program, self.params)
        elif self.fixture_dir:
            record = json.loads((self.fixture_dir / f"{label}-instance.json").read_text(encoding="utf-8"))
            params_file = record["params_file"]
            if not isinstance(params_file, str) or Path(params_file).name != params_file:
                raise ValueError(f"{label}: invalid fixture parameter file name")
            params = json.loads((self.fixture_dir / params_file).read_text(encoding="utf-8"))
            snapshot = make_snapshot(record["program"], record["name"], params)
        else:
            raw = self.remote(
                f"touch {PLAY_LOG} && rm -f {PLAY_LOG}-dump {PLAY_LOG}-dump.txt && touch {PLAY_LOG}-dump && "
                f"for i in $(seq 1 30); do test -s {PLAY_LOG}-dump.txt && break; sleep .2; done && "
                f"cat {PLAY_LOG}-dump.txt"
            )
            (self.out_dir / f"{label}-instance.txt").write_text(raw, encoding="utf-8")
            snapshot = parse_instance_dump(raw)
        params_name = f"{label}-params.json"
        (self.out_dir / params_name).write_text(json.dumps(snapshot.params, indent=2) + "\n", encoding="utf-8")
        (self.out_dir / f"{label}-instance.json").write_text(
            json.dumps({"program": snapshot.index, "name": snapshot.name, "params_file": params_name}) + "\n",
            encoding="utf-8",
        )
        return snapshot


def run_checks(runner: XLDeviceRunner) -> None:
    runner.select_program("Breath of Machines", "init-A")
    runner.tap(*MAIN_FX_BUTTON_TAP)
    pic_a = runner.shot("peek-mainfx-A", A_Q_TARGETS)

    def check_reopened(image: np.ndarray, label: str, targets: list[int]) -> None:
        runner.tap(*CLOSE_BUTTON_TAP)
        runner.tap(*MAIN_FX_BUTTON_TAP)
        reopened = runner.shot(f"peek-mainfx-{label}-reopen", targets)
        different = delta_pixels(image, reopened, GRAPH_ROI)
        print(f"Delta {label} / {label}-reopen (GRAPH_ROI): {different} px")
        require(different == 0, f"{label}/{label}-reopen mismatch: {different} px")

    check_reopened(pic_a, "A", A_Q_TARGETS)

    runner.select_program("Acid Saw Reso", "step-B")
    pic_b = runner.shot("peek-mainfx-B", B_Q_TARGETS)
    delta_ab = delta_pixels(pic_a, pic_b, GRAPH_ROI)
    print(f"Delta A / B (GRAPH_ROI): {delta_ab} px (expected non-zero)")
    require(delta_ab > 0, "Stale A/B graph detected: delta A/B in GRAPH_ROI is 0 px")
    check_reopened(pic_b, "B", B_Q_TARGETS)

    before = runner.snapshot("before-q2-turn")
    runner.turn_q(2, 5.0)
    after = runner.snapshot("after-q2-turn")
    require((before.index, before.name) == (after.index, after.name) == (runner.current_index, "Acid Saw Reso"),
            "Q2 snapshots do not belong to the selected Acid Saw Reso program")
    changed = parameter_diff(before.params, after.params)
    print(f"Engine parameter changes after Q2: {json.dumps(changed)}")
    require(list(changed) == ["distortion.drive"], f"Unexpected parameters modified: {changed}")
    require(round(after.params["distortion.drive"] - before.params["distortion.drive"], 1) == 5.0,
            f"distortion.drive did not change by expected delta: {changed['distortion.drive']}")
    print(f"Q2 changed only distortion.drive. {TARGET_ONLY_UNTOUCHED_COUNT} parameters match.")

    runner.select_program("Breath of Machines", "step-A2")
    pic_a2 = runner.shot("peek-mainfx-A2", A_Q_TARGETS)
    check_reopened(pic_a2, "A2", A_Q_TARGETS)
    delta_aa2 = delta_pixels(pic_a, pic_a2, GRAPH_ROI)
    print(f"Delta A / A2 round-trip (GRAPH_ROI): {delta_aa2} px")
    require(delta_aa2 == 0, f"A/A2 round-trip mismatch: {delta_aa2} px")
    runner.tap(*CLOSE_BUTTON_TAP)
    runner.status("closed", page_top=0)


def restore_initial_preset(runner: XLDeviceRunner, initial: ProgramSnapshot) -> tuple[dict, BaseException | None]:
    report = {"status": "FAILED", "initial": {"program": initial.index, "name": initial.name},
              "observed": None, "differences": None}
    select_error = verification_error = None
    try:
        runner.select_program(initial.index, "restore-initial")
    except (Exception, KeyboardInterrupt) as exc:
        select_error = exc
        report["selection_error"] = f"{type(exc).__name__}: {exc}"

    # An ACK failure does not prove that the command did not take effect.
    try:
        restored = runner.snapshot("restored")
        report["observed"] = {"program": restored.index, "name": restored.name}
        report["differences"] = parameter_diff(initial.params, restored.params)
        require((restored.index, restored.name) == (initial.index, initial.name),
                f"Restored preset identity differs: {report['observed']} != {report['initial']}")
        require(not report["differences"],
                f"{len(report['differences'])} restored parameters differ from the initial snapshot")
    except (Exception, KeyboardInterrupt) as exc:
        verification_error = exc
        report["verification_error"] = f"{type(exc).__name__}: {exc}"

    failure = select_error if select_error is not None else verification_error
    if failure is None:
        report["status"] = "VERIFIED"
    return report, failure


def run_acceptance(runner: XLDeviceRunner) -> str:
    summary = runner.out_dir / "acceptance-summary.txt"
    restore_log = runner.out_dir / "restore-diff.json"
    summary.unlink(missing_ok=True)
    restore_log.unlink(missing_ok=True)

    error = restore_error = None
    restore = {"status": "NOT_ATTEMPTED", "initial": None, "observed": None, "differences": None}
    initial = None
    try:
        if not runner.offline and not sys.stdin.isatty():
            raise RuntimeError("The hardware run needs an operator at the physical Q2 encoder")
        runner.status("initial", page_top=0)
        initial = runner.snapshot("initial")
        run_checks(runner)
    except (Exception, KeyboardInterrupt) as exc:
        error = exc
    finally:
        if initial is not None:
            restore, restore_error = restore_initial_preset(runner, initial)
        restore_log.write_text(json.dumps(restore, indent=2) + "\n", encoding="utf-8")

    if error is not None or restore_error is not None:
        status = "FAILED"
    elif runner.dry_run:
        status = "DRY_RUN_PASS"
    elif runner.fixture_dir:
        status = "FIXTURE_PASS"
    else:
        status = "MEASURED_PASS"
    lines = [f"STATUS={status}", f"RESTORE={restore['status']}"]
    if error is not None:
        lines.append(f"CHECK_ERROR={type(error).__name__}: {error}")
    if restore_error is not None:
        lines.append(f"RESTORE_ERROR={type(restore_error).__name__}: {restore_error}")
    summary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if error is not None:
        raise error from restore_error
    if restore_error is not None:
        raise restore_error
    print(f"{status}: acceptance and initial preset restoration verified. Evidence: {summary}")
    if runner.offline:
        print("Offline verification only. Physical XL acceptance remains unmeasured.")
    return status


if __name__ == "__main__":
    args = parse_args()
    run_acceptance(XLDeviceRunner(args.device_dir, args.out_dir, args.dry_run, args.fixture_dir))
