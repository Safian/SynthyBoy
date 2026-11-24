#!/usr/bin/env python3
# SynthyBoy - Unit and integration tests for XL MAIN FX PEEK acceptance tooling
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import numpy as np

# Load scripts/measure-xl-main-fx-peek.py dynamically
SCRIPT_PATH = Path(__file__).resolve().parent.parent / "measure-xl-main-fx-peek.py"
spec = importlib.util.spec_from_file_location("measure_xl_main_fx_peek", str(SCRIPT_PATH))
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)

FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures" / "main_fx_peek"


class MeasureXlMainFxPeekTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.device_dir = self.tmp / "mock_device"
        self.out_dir = self.tmp / "out"
        self.device_dir.mkdir(parents=True, exist_ok=True)
        self.out_dir.mkdir(parents=True, exist_ok=True)

    def tearDown(self):
        self._tmp.cleanup()

    def restore_report(self):
        return json.loads((self.out_dir / "restore-diff.json").read_text())

    def copy_fixtures(self):
        fixture_dir = self.tmp / "fixtures"
        shutil.copytree(FIXTURES_DIR, fixture_dir)
        from PIL import Image
        for state, preset in (("A", "Breath of Machines"), ("A2", "Breath of Machines"), ("B", "Acid Saw Reso")):
            image = Image.fromarray(measure.make_synthetic_shot(preset, True))
            for suffix in ("", "-reopen"):
                image.save(fixture_dir / f"peek-mainfx-{state}{suffix}-composed.png")
        return fixture_dir

    def fixture_runner(self, directory):
        return measure.XLDeviceRunner(self.device_dir, self.out_dir, fixture_dir=directory)

    def test_dry_run_acceptance_distinguishes_from_measured_pass(self):
        """Dry-run must verify script flow and parameters but explicitly flag hardware as UNMEASURED."""
        runner = measure.XLDeviceRunner(
            dev_dir=self.device_dir,
            out_dir=self.out_dir,
            dry_run=True,
        )
        status = measure.run_acceptance(runner)
        self.assertEqual(status, "DRY_RUN_PASS")

        summary_file = self.out_dir / "acceptance-summary.txt"
        self.assertTrue(summary_file.is_file())
        self.assertIn("STATUS=DRY_RUN_PASS", summary_file.read_text())

        # Verify artifacts were generated
        self.assertTrue((self.out_dir / "peek-mainfx-A-composed.png").is_file())
        self.assertTrue((self.out_dir / "peek-mainfx-B-composed.png").is_file())
        self.assertTrue((self.out_dir / "before-q2-turn-params.json").is_file())
        self.assertTrue((self.out_dir / "after-q2-turn-params.json").is_file())

    def test_offline_fixture_replay(self):
        """Complete fixtures verify the scripted checks and preset restore offline."""
        self.assertTrue(FIXTURES_DIR.is_dir(), f"Fixtures directory missing: {FIXTURES_DIR}")
        runner = measure.XLDeviceRunner(
            dev_dir=self.device_dir,
            out_dir=self.out_dir,
            dry_run=False,
            fixture_dir=self.copy_fixtures(),
        )
        status = measure.run_acceptance(runner)
        self.assertEqual(status, "FIXTURE_PASS")

        summary_file = self.out_dir / "acceptance-summary.txt"
        self.assertTrue(summary_file.is_file())
        self.assertIn("STATUS=FIXTURE_PASS\nRESTORE=VERIFIED", summary_file.read_text())
        self.assertEqual(self.restore_report()["differences"], {})

    def test_reject_wrong_slot_outside_active_q_group(self):
        directory = self.copy_fixtures()
        fixture = directory / "peek-mainfx-A.status"
        status = fixture.read_text().replace(" 847 -1", " 847 0")
        fixture.write_text(status + "\npage_q_targets: 837 838 841 842\n")
        with self.assertRaisesRegex(AssertionError, "page_q_slots"):
            self.fixture_runner(directory).shot("peek-mainfx-A", measure.A_Q_TARGETS)

    def test_reject_stale_ab_graph(self):
        """When Preset B retains Preset A's graph canvas (delta_AB == 0), the run must fail."""
        runner = measure.XLDeviceRunner(
            dev_dir=self.device_dir,
            out_dir=self.out_dir,
            dry_run=True,
        )
        original_shot = runner.shot

        def stale_shot_mock(name: str, targets):
            img = original_shot(name, targets)
            # Simulate stale graph bug: Preset B returns Breath of Machines (Preset A) visual curve
            if name == "peek-mainfx-B":
                return measure.make_synthetic_shot("Breath of Machines", True)
            return img

        runner.shot = stale_shot_mock

        with self.assertRaises(AssertionError) as ctx:
            measure.run_acceptance(runner)
        self.assertIn("Stale A/B graph detected: delta A/B in GRAPH_ROI is 0 px", str(ctx.exception))
        self.assertEqual(runner.current_index, 0)
        self.assertEqual(self.restore_report()["status"], "VERIFIED")
        self.assertIn("STATUS=FAILED\nRESTORE=VERIFIED", (self.out_dir / "acceptance-summary.txt").read_text())

    def test_reject_blank_zero_shot(self):
        """Blank frames with all zeros must be rejected as invalid captures."""
        zero_frame = np.zeros((800, 1280, 3), dtype=np.uint8)
        with self.assertRaises(AssertionError) as ctx:
            measure.validate_shot(zero_frame, "zero-test", measure.GRAPH_ROI)
        self.assertIn("contains only zeros (blank capture)", str(ctx.exception))

    def test_reject_blank_roi_shot(self):
        """Frames where only the ROI contains zeros must be rejected."""
        frame = np.full((800, 1280, 3), 50, dtype=np.uint8)
        x1, y1, x2, y2 = measure.GRAPH_ROI
        frame[y1:y2, x1:x2] = 0
        with self.assertRaises(AssertionError) as ctx:
            measure.validate_shot(frame, "zero-roi-test", measure.GRAPH_ROI)
        self.assertIn("ROI (65, 310, 1215, 430) contains only zeros", str(ctx.exception))

    def test_reject_empty_param_dump(self):
        """Empty parameter dumps must be rejected immediately."""
        with self.assertRaises(ValueError) as ctx:
            measure.validate_and_normalize_param_dump({})
        self.assertIn("empty or not a JSON dictionary", str(ctx.exception))

        with self.assertRaises(ValueError) as ctx:
            measure.validate_and_normalize_param_dump(None)
        self.assertIn("empty or not a JSON dictionary", str(ctx.exception))

    def test_reject_malformed_param_count(self):
        """Parameter dumps missing parameters (< 2637) must be rejected."""
        short_dump = {"distortion.drive": 0.0, "param_001": 0.0}
        with self.assertRaises(ValueError) as ctx:
            measure.validate_and_normalize_param_dump(short_dump)
        self.assertIn("expected 2637 parameter keys, got 2", str(ctx.exception))

        structured_short = {
            "format": "synthyboy-params",
            "params": [{"key": "distortion.drive", "value": 0.0}],
        }
        with self.assertRaises(ValueError) as ctx:
            measure.validate_and_normalize_param_dump(structured_short)
        self.assertIn("expected 2637 parameters, found 1", str(ctx.exception))

    def test_reject_missing_distortion_drive(self):
        """Parameter dumps missing the required target parameter must be rejected."""
        dump_missing_drive = {f"param_{i:04d}": 0.0 for i in range(2637)}
        with self.assertRaises(ValueError) as ctx:
            measure.validate_and_normalize_param_dump(dump_missing_drive)
        self.assertIn("required key 'distortion.drive' missing", str(ctx.exception))

    def test_reject_unexpected_param_mutation(self):
        """If turning Q2 mutates extra parameters, target-only check must fail."""
        runner = measure.XLDeviceRunner(
            dev_dir=self.device_dir,
            out_dir=self.out_dir,
            dry_run=True,
        )
        original_turn_q = runner.turn_q

        def leaky_turn_q(knob: int, delta: float):
            original_turn_q(knob, delta)
            if knob == 2 and delta > 0:
                # Unintended side-effect mutation
                runner.params["master.volume"] = float(runner.params.get("master.volume", 0.0) + 1.0)

        runner.turn_q = leaky_turn_q

        with self.assertRaises(AssertionError) as ctx:
            measure.run_acceptance(runner)
        self.assertIn("Unexpected parameters modified", str(ctx.exception))

    def test_reject_incorrect_q2_delta(self):
        """If distortion.drive delta is wrong, target-only check must fail."""
        runner = measure.XLDeviceRunner(
            dev_dir=self.device_dir,
            out_dir=self.out_dir,
            dry_run=True,
        )
        original_turn_q = runner.turn_q

        def bad_delta_turn_q(knob: int, delta: float):
            if knob == 2 and delta > 0:
                runner.params["distortion.drive"] = float(runner.params.get("distortion.drive", 0.0) + 1.0)
            else:
                original_turn_q(knob, delta)

        runner.turn_q = bad_delta_turn_q

        with self.assertRaises(AssertionError) as ctx:
            measure.run_acceptance(runner)
        self.assertIn("distortion.drive did not change by expected delta", str(ctx.exception))

    def test_fixture_rejects_missing_preset_or_screenshot_evidence(self):
        folder = self.copy_fixtures()
        for evidence in ("step-B.ack", "peek-mainfx-B.status", "after-q2-turn-instance.json"):
            with self.subTest(evidence=evidence):
                item = folder / evidence
                original = item.read_bytes()
                item.unlink()
                with self.assertRaises(FileNotFoundError):
                    measure.run_acceptance(self.fixture_runner(folder))
                item.write_bytes(original)
                self.assertEqual(self.restore_report()["status"], "VERIFIED")
                self.assertIn("STATUS=FAILED", (self.out_dir / "acceptance-summary.txt").read_text())

    def test_original_and_restore_failures_are_both_recorded(self):
        folder = self.copy_fixtures()
        (folder / "step-B.ack").write_text("2 error unknown name\n")
        (folder / "restore-initial.ack").write_text("3 ok 9 Wrong index\n")
        with self.assertRaisesRegex(AssertionError, "step-B: program selection failed") as failure:
            measure.run_acceptance(self.fixture_runner(folder))
        self.assertIn("restore-initial: program ACK expected 0, got 9", str(failure.exception.__cause__))
        report = self.restore_report()
        self.assertEqual(report["status"], "FAILED")
        self.assertIn("expected 0, got 9", report["selection_error"])
        self.assertEqual(report["observed"], {"program": 0, "name": "Init"})
        self.assertEqual(report["differences"], {})
        summary = (self.out_dir / "acceptance-summary.txt").read_text()
        self.assertIn("CHECK_ERROR=", summary)
        self.assertIn("RESTORE_ERROR=", summary)
        self.assertNotIn("PASS", summary)

    def test_restore_uses_the_actual_initial_index(self):
        folder = self.copy_fixtures()
        initial = {"program": 15, "name": "Filter's Start", "params_file": "before-q2-turn-params.json"}
        for phase in ("initial", "restored"):
            (folder / f"{phase}-instance.json").write_text(json.dumps(initial))
        (folder / "restore-initial.ack").write_text("4 ok 15 Filter's Start\n")
        runner = self.fixture_runner(folder)
        self.assertEqual(measure.run_acceptance(runner), "FIXTURE_PASS")
        self.assertEqual(runner.current_index, 15)
        self.assertEqual(self.restore_report()["initial"], {"program": 15, "name": "Filter's Start"})

    def test_parameter_drift_blocks_pass_and_records_exact_values(self):
        folder = self.copy_fixtures()
        (folder / "restored-instance.json").write_text(json.dumps({
            "program": 0, "name": "Init", "params_file": "after-q2-turn-params.json",
        }))
        with self.assertRaisesRegex(AssertionError, "1 restored parameters differ"):
            measure.run_acceptance(self.fixture_runner(folder))
        report = self.restore_report()
        self.assertEqual(report["status"], "FAILED")
        self.assertEqual(report["differences"], {"distortion.drive": {"before": 0.0, "after": 5.0}})

    def test_fresh_snapshot_required_even_when_restore_ack_matches(self):
        folder = self.copy_fixtures()
        (folder / "restored-instance.json").unlink()
        with self.assertRaises(FileNotFoundError):
            measure.run_acceptance(self.fixture_runner(folder))
        self.assertEqual(self.restore_report()["status"], "FAILED")
        self.assertIsNone(self.restore_report()["observed"])
        self.assertIsNone(self.restore_report()["differences"])

    def test_verify_restored_index_independently_of_ack(self):
        folder = self.copy_fixtures()
        (folder / "restored-instance.json").write_text(json.dumps({
            "program": 2, "name": "Acid Saw Reso", "params_file": "before-q2-turn-params.json",
        }))
        with self.assertRaisesRegex(AssertionError, "Restored preset identity differs"):
            measure.run_acceptance(self.fixture_runner(folder))
        self.assertEqual(self.restore_report()["observed"]["program"], 2)
        self.assertEqual(self.restore_report()["differences"], {})

    def test_failed_preflight_does_not_recall_a_preset_or_leave_an_old_pass(self):
        folder = self.copy_fixtures()
        (folder / "initial-instance.json").unlink()
        summary = self.out_dir / "acceptance-summary.txt"
        summary.write_text("STATUS=MEASURED_PASS\n")
        with self.assertRaises(FileNotFoundError):
            measure.run_acceptance(self.fixture_runner(folder))
        self.assertFalse((self.out_dir / "init-A.ack").exists())
        self.assertFalse((self.out_dir / "restore-initial.ack").exists())
        self.assertEqual(summary.read_text().splitlines()[:2], ["STATUS=FAILED", "RESTORE=NOT_ATTEMPTED"])

    def test_interrupt_after_selection_attempts_restore(self):
        runner = self.fixture_runner(FIXTURES_DIR)
        runner.turn_q = lambda *args: self.interrupt()
        with self.assertRaises(KeyboardInterrupt):
            measure.run_acceptance(runner)
        self.assertEqual(self.restore_report()["status"], "VERIFIED")
        self.assertEqual(runner.current_index, 0)

    @staticmethod
    def interrupt():
        raise KeyboardInterrupt()

    def test_parse_actual_instance_dump_layout(self):
        values = measure.load_default_params()
        chunk = json.dumps({"mpcProgram": 5, "mpcProgramName": "Régi preset"}, ensure_ascii=False)
        entries = [f"p {i} {key} {value}\n" for i, (key, value) in enumerate(values.items())]
        prefix = "selectors 1 1 1\nprogram 5 protected -1 window 0\nui_write_calls 0 ui_automate_calls 0\n"
        suffix = f"proxy 0 0.5\nchunk {len(chunk.encode('utf-8'))}\n{chunk}\n"
        captured = prefix + "".join(entries) + suffix
        self.assertEqual(measure.parse_instance_dump(captured), (5, "Régi preset", values))
        with self.assertRaisesRegex(ValueError, "truncated project chunk"):
            measure.parse_instance_dump(captured[:-3])
        with self.assertRaisesRegex(ValueError, "Invalid parameter index or duplicate key"):
            measure.parse_instance_dump(prefix + entries[0] + "".join(entries) + suffix)

    def test_hardware_mode_waits_for_a_physical_operator_before_contact(self):
        runner = measure.XLDeviceRunner(self.device_dir, self.out_dir)
        with mock.patch.object(measure.sys, "stdin", io.StringIO()), mock.patch.object(runner, "remote") as remote:
            with self.assertRaisesRegex(RuntimeError, "physical Q2 encoder"):
                measure.run_acceptance(runner)
        remote.assert_not_called()
        self.assertEqual(self.restore_report()["status"], "NOT_ATTEMPTED")

    def test_optimized_python_still_rejects_invalid_ack(self):
        folder = self.copy_fixtures()
        (folder / "step-B.ack").write_text("1 ok 2 Acid Saw Reso\n")
        result = subprocess.run(
            [sys.executable, "-O", str(SCRIPT_PATH), "--fixture-dir", str(folder), "--out-dir", str(self.out_dir)],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("stale selection ACK", result.stderr)
        self.assertNotIn("PASS", result.stdout)
        self.assertEqual(self.restore_report()["status"], "VERIFIED")

    def test_cli_dry_run_invocation(self):
        """CLI execution in dry-run mode must distinguish output and exit 0."""
        r = subprocess.run(
            [
                sys.executable,
                str(SCRIPT_PATH),
                "--dry-run",
                "--out-dir",
                str(self.out_dir),
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("DRY_RUN_PASS", r.stdout)
        self.assertIn("Physical XL acceptance remains unmeasured.", r.stdout)
        self.assertNotIn("ALL XL MAIN FX PEEK ACCEPTANCE CHECKS PASSED", r.stdout)
        self.assertNotIn("STATUS=MEASURED_PASS", r.stdout)

    def test_cli_fixture_replay_invocation(self):
        """CLI fixture replay cannot report a hardware result."""
        r = subprocess.run(
            [
                sys.executable,
                str(SCRIPT_PATH),
                "--fixture-dir",
                str(FIXTURES_DIR),
                "--out-dir",
                str(self.out_dir),
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("FIXTURE_PASS", r.stdout)
        self.assertNotIn("MEASURED_PASS", r.stdout)
        self.assertIn("Physical XL acceptance remains unmeasured.", r.stdout)


if __name__ == "__main__":
    unittest.main()
